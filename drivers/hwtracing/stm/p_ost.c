// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * MIPI OST framing protocol for STM devices.
 */

#include <linux/pid.h>
#include <linux/sched/clock.h>
#include <linux/slab.h>
#include <linux/stm.h>
#include "stm.h"

/*
 * OST Base Protocol Header
 *
 * Position	Bits	Field Name
 *      0       8       STARTSIMPLE
 *      1       8       Version
 *      2       8       Entity ID
 *      3       8       protocol ID
 */
#define OST_FIELD_STARTSIMPLE		0
#define OST_FIELD_VERSION		8
#define OST_FIELD_ENTITY		16
#define OST_FIELD_PROTOCOL		24

#define OST_TOKEN_STARTSIMPLE		0x10
#define OST_VERSION_MIPI1		0x10

/* entity id to identify the source*/
#define OST_ENTITY_FTRACE		0x01
#define OST_ENTITY_CONSOLE		0x02
#define OST_ENTITY_DIAG			0xEE

#define OST_CONTROL_PROTOCOL		0x0

#define DATA_HEADER ((OST_TOKEN_STARTSIMPLE << OST_FIELD_STARTSIMPLE) | \
		     (OST_VERSION_MIPI1 << OST_FIELD_PROTOCOL) | \
		     (OST_CONTROL_PROTOCOL << OST_FIELD_PROTOCOL))

#define STM_MAKE_VERSION(ma, mi)	(((ma) << 8) | (mi))
#define STM_HEADER_MAGIC		(0x5953)

enum ost_entity_type {
	OST_ENTITY_TYPE_NONE,
	OST_ENTITY_TYPE_FTRACE,
	OST_ENTITY_TYPE_CONSOLE,
	OST_ENTITY_TYPE_DIAG,
};

static const char * const str_ost_entity_type[] = {
	[OST_ENTITY_TYPE_NONE]		= "none",
	[OST_ENTITY_TYPE_FTRACE]	= "ftrace",
	[OST_ENTITY_TYPE_CONSOLE]	= "console",
	[OST_ENTITY_TYPE_DIAG]		= "diag",
};

static const u32 ost_entity_value[] = {
	[OST_ENTITY_TYPE_NONE]		= 0,
	[OST_ENTITY_TYPE_FTRACE]	= OST_ENTITY_FTRACE,
	[OST_ENTITY_TYPE_CONSOLE]	= OST_ENTITY_CONSOLE,
	[OST_ENTITY_TYPE_DIAG]		= OST_ENTITY_DIAG,
};

struct ost_policy_node {
	enum ost_entity_type	entity_type;
};

struct ost_output {
	struct ost_policy_node	node;
};

/* Set default entity type as none */
static void ost_policy_node_init(void *priv)
{
	struct ost_policy_node *pn = priv;

	pn->entity_type = OST_ENTITY_TYPE_NONE;
}

static int ost_output_open(void *priv, struct stm_output *output)
{
	struct ost_policy_node *pn = priv;
	struct ost_output *opriv;

	opriv = kzalloc(sizeof(*opriv), GFP_ATOMIC);
	if (!opriv)
		return -ENOMEM;

	memcpy(&opriv->node, pn, sizeof(opriv->node));
	output->pdrv_private = opriv;
	return 0;
}

static void ost_output_close(struct stm_output *output)
{
	kfree(output->pdrv_private);
}

static ssize_t ost_t_policy_entity_show(struct config_item *item,
					char *page)
{
	struct ost_policy_node *pn = to_pdrv_policy_node(item);
	ssize_t sz = 0;
	int i;

	for (i = 1; i < ARRAY_SIZE(str_ost_entity_type); i++) {
		if (i == pn->entity_type)
			sz += sysfs_emit_at(page, sz, "[%s] ", str_ost_entity_type[i]);
		else
			sz += sysfs_emit_at(page, sz, "%s ", str_ost_entity_type[i]);
	}

	sz += sysfs_emit_at(page, sz, "\n");
	return sz;
}

static int entity_index(const char *str)
{
	int i;

	for (i = 1; i < ARRAY_SIZE(str_ost_entity_type); i++) {
		if (sysfs_streq(str, str_ost_entity_type[i]))
			return i;
	}

	return 0;
}

static ssize_t
ost_t_policy_entity_store(struct config_item *item, const char *page,
			  size_t count)
{
	struct ost_policy_node *pn = to_pdrv_policy_node(item);
	int i;

	i = entity_index(page);
	if (i)
		pn->entity_type = i;
	else
		return -EINVAL;

	return count;
}
CONFIGFS_ATTR(ost_t_policy_, entity);

static struct configfs_attribute *ost_t_policy_attrs[] = {
	&ost_t_policy_attr_entity,
	NULL,
};

static ssize_t
notrace ost_write(struct stm_data *data, struct stm_output *output,
		  unsigned int chan, const char *buf, size_t count,
		  struct stm_source_data *source)
{
	struct ost_output *op = output->pdrv_private;
	unsigned int c = output->channel + chan;
	unsigned int m = output->master;
	const unsigned char nil = 0;
	u32 header = DATA_HEADER;
	struct trc_hdr {
		u16 version;
		u16 magic;
		u32 cpu;
		u64 timestamp;
		u64 tgid;
	} hdr;
	ssize_t sz;

	/*
	 * Identify the source by entity type.
	 * If entity type is not set, return error value.
	 */
	if (op->node.entity_type)
		header |= ost_entity_value[op->node.entity_type];
	else
		return -EINVAL;

	/*
	 * STP framing rules for OST frames:
	 *   * the first packet of the OST frame is marked;
	 *   * the last packet is a FLAG with timestamped tag.
	 */
	/* Message layout: HEADER / DATA / TAIL */
	/* HEADER */
	sz = data->packet(data, m, c, STP_PACKET_DATA, STP_PACKET_MARKED,
			  4, (u8 *)&header);
	if (sz <= 0)
		return sz;

	/* DATA */
	hdr.version	= STM_MAKE_VERSION(0, 3);
	hdr.magic	= STM_HEADER_MAGIC;
	hdr.cpu		= raw_smp_processor_id();
	hdr.timestamp	= sched_clock();
	hdr.tgid	= task_tgid_nr(current);
	sz = stm_data_write(data, m, c, false, &hdr, sizeof(hdr));
	if (sz <= 0)
		return sz;

	sz = stm_data_write(data, m, c, false, buf, count);

	/* TAIL */
	if (sz > 0)
		data->packet(data, m, c, STP_PACKET_FLAG,
			STP_PACKET_TIMESTAMPED, 0, &nil);

	return sz;
}

static const struct stm_protocol_driver ost_pdrv = {
	.owner			= THIS_MODULE,
	.name			= "p_ost",
	.write			= ost_write,
	.policy_attr		= ost_t_policy_attrs,
	.output_open		= ost_output_open,
	.output_close		= ost_output_close,
	.policy_node_init	= ost_policy_node_init,
};

static int ost_stm_init(void)
{
	return stm_register_protocol(&ost_pdrv);
}
module_init(ost_stm_init);

static void ost_stm_exit(void)
{
	stm_unregister_protocol(&ost_pdrv);
}
module_exit(ost_stm_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MIPI Open System Trace STM framing protocol driver");
