// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright (c) 2025 Qualcomm Technologies, Inc. All rights reserved.

/*
 * WCD9378 SDCA hardware quirk for the SDCA class driver.
 *
 * sdca_class.c is the SoundWire slave driver; it calls wcd9378_sdca_hw_ops
 * during probe/prepare to enable supplies, toggle the reset GPIO, and apply
 * SoundWire slave properties and static SDCA function data that are specific
 * to this device (the Tambora / WCD9378 codec). On ARM/DT platforms there is
 * no ACPI/DisCo firmware node, so the topology transcribed from the factory
 * ASL is supplied via get_function_data.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/soundwire/sdw_type.h>
#include <linux/workqueue.h>
#include <sound/pcm.h>
#include <sound/sdca.h>
#include <sound/sdca_function.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include "../sdca/sdca_class.h"

/* Forward declaration of entities array */
static struct sdca_entity wcd9378_sdca_entities[];
/*
 * Entity array index map (see wcd9378_sdca_entities[] below).
 * The entities are stored in the same order as the ASL entity-id-list,
 * with Entity 0 (Function) last.
 *
 *  [0]  E001 IT 41    (0x1)   [11] E00F IT 33    (0xF)
 *  [1]  E002 CS 41    (0x2)   [12] E010 PDE 34   (0x10)
 *  [2]  E003 MFPU 21  (0x3)   [13] E011 FU 33    (0x11)
 *  [3]  E004 XU 42    (0x4)   [14] E012 SU 35    (0x12)
 *  [4]  E007 SU 43    (0x7)   [15] E013 XU 36    (0x13)
 *  [5]  E008 SU 45    (0x8)   [16] E015 CS 36    (0x15)
 *  [6]  E009 PDE 47   (0x9)   [17] E016 OT 36    (0x16)
 *  [7]  E00A OT 43    (0xA)   [18] E017 MFPU 236 (0x17)
 *  [8]  E00B OT 45    (0xB)   [19] E018 CS 236   (0x18)
 *  [9]  E00C GE 35    (0xC)   [20] E019 OT 236   (0x19)
 *  [10] E00D IT 131   (0xD)   [21] E000 Function (0x0)
 */
#define QSJ_IT41	0
#define QSJ_CS41	1
#define QSJ_MFPU21	2
#define QSJ_XU42	3
#define QSJ_SU43	4
#define QSJ_SU45	5
#define QSJ_PDE47	6
#define QSJ_OT43	7
#define QSJ_OT45	8
#define QSJ_GE35	9
#define QSJ_IT131	10
#define QSJ_CS131	11
#define QSJ_IT33	12
#define QSJ_PDE34	13
#define QSJ_FU33	14
#define QSJ_SU35	15
#define QSJ_XU36	16
#define QSJ_CS36	17
#define QSJ_OT36	18
#define QSJ_MFPU236	19
#define QSJ_CS236	20
#define QSJ_OT236	21

/*
 * Range Data Structures
 *
 * Range layout is { cols, rows, data[cols*rows] }. The raw values mirror
 * the little-endian buffers from the ASL.
 */

/* Entity 1 (IT 41) - Usage range: 1 septuple (cols=7, rows=1)
 * PDM render stream: SoundWire carries 2-bit PDM at 4.8MHz internally, but
 * the host-visible PCM rate/width is 48kHz/16-bit (msft-sdca-pdm-pcm map).
 * Usage value = 0x2 (HIFI); value 0x3 (ULP) does not route audio to the
 * HPH analog driver on WCD9378.
 */
static u32 range_it41_usage_data[] = {
	/* usage, CBN, sample_rate, sample_width, full_scale, noise_floor, tag */
	0x2, 0x2D0, 0xBB80, 0x10, 0x0, 0x0, 0x0,  /* HIFI, 48kHz PCM, 16-bit */
};

/* Entity 1 (IT 41) - ClusterIndex range: index -> cluster id */
static u32 range_it41_cluster_data[] = { 0x1, 0x1 };

/* Entity 1 (IT 41) - DataPort selector range (16 cols x 4 rows)
 * DC value = 6; row A col 6 = 0x06 (DP6 / HPH render port).
 */
static u32 range_it41_dp_data[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x6, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

/* Entity 2 (CS 41) - SampleRateIndex range: index 1 -> 48kHz PCM
 * Register DC value = 1. PCM rate is 48kHz (PDM-to-PCM mapped internally).
 */
static u32 range_cs41_sr_data[] = { 0x1, 0xBB80 };

/* Selector unit range: disconnected / connected (shared layout) */
static u32 range_su_sel_data[] = { 0x0, 0x1 };

/* PDE Requested_PS range: PS0 / PS3 (shared layout) */
static u32 range_pde_req_ps_data[] = { 0x0, 0x3 };

/* Entity 0xC (GE 35) - SelectedMode range: mode -> terminal type
 *
 * WORKAROUND: modes 0 (Jack Unplugged) and 1 (Jack Unknown) are added
 * and mapped to the same "Headphone on jack" terminal type as mode 4.
 * The Tambora firmware on this board does the mechanical MBHC (fires
 * SDCA_4 with DETECTED_MODE=1) but cannot complete headphone-vs-headset
 * ADC discrimination on its own, so the state machine gets stuck at
 * Jack Unknown and sdca_jack_process falls back to SELECTED_MODE=0.
 * Without these mode 0/1 entries, the GE MUX DAPM widget has no route
 * for those values and drops SU45's selector, muting all HPH playback.
 * Aliasing both to Headphone keeps the IT41->OT43 (HPH) DAPM path alive
 * so the analog HP jack works while proper HS discrimination lands in
 * a future wcd-mbhc-v2 integration.
 */
static u32 range_ge35_mode_data[] = {
	0x0, 0x6C0,	/* Mode 0: Jack Unplugged (workaround: routes to HPH) */
	0x1, 0x6C0,	/* Mode 1: Jack Unknown (workaround: routes to HPH) */
	0x3, 0x6D0,	/* Mode 3: Headset output on jack */
	0x4, 0x6C0,	/* Mode 4: Headphone on jack */
};

/* Entity 0xD (IT 131) - Usage range: optimization render stream at 192kHz */
static u32 range_it131_usage_data[] = {
	0x3, 0x334, 0x2EE00, 0x8, 0x0, 0x0, 0x0,  /* ULP, 192kHz, 8-bit */
};

/* Entity 0xD (IT 131) - ClusterIndex range: index -> cluster id 3 */
static u32 range_it131_cluster_data[] = { 0x1, 0x3 };

/* Entity 0xD (IT 131) - DataPort selector range (DP7) */
static u32 range_it131_dp_data[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

/* Entity 0xE (CS 131) - SampleRateIndex range: index -> 192kHz */
static u32 range_cs131_sr_data[] = { 0x1, 0x2EE00 };

/* Entity 0xF (IT 33) - Mic bias range: 2.75V */
static u32 range_it33_micbias_data[] = { 0x5 };

/* Entity 0xF (IT 33) - Usage range */
static u32 range_it33_usage_data[] = {
	0x1, 0x2C6, 0x0, 0x0, 0x0, 0x0, 0x0,  /* HIFI */
};

/* Entity 0xF (IT 33) - ClusterIndex range: index -> cluster id 2 */
static u32 range_it33_cluster_data[] = { 0x1, 0x2 };

/* Entity 0x15 (CS 36) - SampleRateIndex range: index 1 -> 48kHz PCM */
static u32 range_cs36_sr_data[] = { 0x1, 0xBB80 };

/* Entity 0x16 (OT 36) - Usage range
 * PDM capture: 1-bit PDM at 4.8MHz internally; host sees 48kHz/16-bit PCM.
 */
static u32 range_ot36_usage_data[] = {
	0x1, 0x2C6, 0xBB80, 0x10, 0x0, 0x0, 0x0,  /* HIFI, 48kHz PCM, 16-bit */
};

/* Entity 0x16 (OT 36) - DataPort selector range (DP2 / TX1) */
static u32 range_ot36_dp_data[] = {
	0xFF, 0xFF, 0x2, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

/* Entity 0x18 (CS 236) - SampleRateIndex range: index -> 48kHz */
static u32 range_cs236_sr_data[] = { 0x1, 0xBB80 };

/* Entity 0x19 (OT 236) - Usage range
 * Optimization capture; clock domain is CS 131 (192 kHz), so PCM rate = 192 kHz.
 */
static u32 range_ot236_usage_data[] = {
	0x1, 0x334, 0x2EE00, 0x8, 0x0, 0x0, 0x0,  /* HIFI, 192kHz, 8-bit */
};

/* Entity 0x19 (OT 236) - DataPort selector range (DP5 / TX4) */
static u32 range_ot236_dp_data[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x5, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

/*
 * Control Value Arrays
 */

/* Entity 0 (Function) Control Values */
static int ctrl_fun_sdca_ver_vals[] = { 0x11 };
static int ctrl_fun_type_vals[] = { 0x08 };	/* SimpleJack */
static int ctrl_fun_man_id_vals[] = { 0x0217 };
static int ctrl_fun_id_vals[] = { 0x3 };
static int ctrl_fun_ver_vals[] = { 0x0 };
static int ctrl_dev_sdca_ver_vals[] = { 0x11 };

/* Entity 1 (IT 41) Control Values */
static int ctrl_it41_latency_vals[] = { 0x0 };
static int ctrl_it41_cluster_vals[] = { 0x1 };
static int ctrl_it41_dp_vals[] = { 0x6 };

/* Entity 2 (CS 41) Control Values */
static int ctrl_cs41_sr_vals[] = { 0x1 };

/* Entity 3 (MFPU 21) Control Values */
static int ctrl_mfpu21_bypass_vals[] = { 0x1 };

/* Entity 4 (XU 42) Control Values */
static int ctrl_xu42_id_vals[] = { 0x2131 };
static int ctrl_xu42_ver_vals[] = { 0x1 };

/* Entity 0xD (IT 131) Control Values */
static int ctrl_it131_latency_vals[] = { 0x0 };
static int ctrl_it131_cluster_vals[] = { 0x1 };
static int ctrl_it131_dp_vals[] = { 0x7 };

/* Entity 0xE (CS 131) Control Values */
static int ctrl_cs131_sr_vals[] = { 0x1 };

/* Entity 0xF (IT 33) Control Values */
static int ctrl_it33_micbias_vals[] = { 0x5 };	/* 2.75V mic bias */
static int ctrl_it33_latency_vals[] = { 0x0 };
static int ctrl_it33_cluster_vals[] = { 0x1 };

/* Entity 0x13 (XU 36) Control Values */
static int ctrl_xu36_id_vals[] = { 0x2131 };
static int ctrl_xu36_ver_vals[] = { 0x1 };

/* Entity 0x15 (CS 36) Control Values */
static int ctrl_cs36_sr_vals[] = { 0x1 };

/* Entity 0x16 (OT 36) Control Values */
static int ctrl_ot36_latency_vals[] = { 0x0 };
static int ctrl_ot36_dp_vals[] = { 0x2 };

/* Entity 0x17 (MFPU 236) Control Values */
static int ctrl_mfpu236_bypass_vals[] = { 0x1 };

/* Entity 0x18 (CS 236) Control Values */
static int ctrl_cs236_sr_vals[] = { 0x1 };

/* Entity 0x19 (OT 236) Control Values */
static int ctrl_ot236_latency_vals[] = { 0x0 };
static int ctrl_ot236_dp_vals[] = { 0x5 };

/* Entity 0 (Function) Controls */
static struct sdca_control entity0_controls[] = {
	{ .sel = 0x1,  .mode = SDCA_ACCESS_MODE_RW,   .layers = 0x4, .cn_list = 0x1, .label = SDCA_CTL_COMMIT_GROUP_MASK_NAME },
	{ .sel = 0x4,  .mode = SDCA_ACCESS_MODE_DC,   .layers = 0x4, .cn_list = 0x1, .values = ctrl_fun_sdca_ver_vals, .has_fixed = true, .label = SDCA_CTL_FUNCTION_SDCA_VERSION_NAME },
	{ .sel = 0x5,  .mode = SDCA_ACCESS_MODE_DC,   .layers = 0x4, .cn_list = 0x1, .values = ctrl_fun_type_vals, .has_fixed = true, .label = SDCA_CTL_FUNCTION_TYPE_NAME },
	{ .sel = 0x6,  .mode = SDCA_ACCESS_MODE_DC,   .layers = 0x4, .cn_list = 0x1, .values = ctrl_fun_man_id_vals, .has_fixed = true, .label = SDCA_CTL_FUNCTION_MANUFACTURER_ID_NAME },
	{ .sel = 0x7,  .mode = SDCA_ACCESS_MODE_DC,   .layers = 0x4, .cn_list = 0x1, .values = ctrl_fun_id_vals, .has_fixed = true, .label = SDCA_CTL_FUNCTION_ID_NAME },
	{ .sel = 0x8,  .mode = SDCA_ACCESS_MODE_DC,   .layers = 0x4, .cn_list = 0x1, .values = ctrl_fun_ver_vals, .has_fixed = true, .label = SDCA_CTL_FUNCTION_VERSION_NAME },
	{ .sel = 0x9,  .mode = SDCA_ACCESS_MODE_RO,   .layers = 0x4, .cn_list = 0x1, .label = SDCA_CTL_FUNCTION_EXTENSION_ID_NAME },
	{ .sel = 0xA,  .mode = SDCA_ACCESS_MODE_RO,   .layers = 0x4, .cn_list = 0x1, .label = SDCA_CTL_FUNCTION_EXTENSION_VERSION_NAME },
	{ .sel = 0x10, .mode = SDCA_ACCESS_MODE_RW1C, .layers = 0x4, .cn_list = 0x1, .is_volatile = true, .label = SDCA_CTL_FUNCTION_STATUS_NAME },
	{ .sel = 0x11, .mode = SDCA_ACCESS_MODE_RW1S, .layers = 0x4, .cn_list = 0x1, .label = SDCA_CTL_FUNCTION_ACTION_NAME },
	{ .sel = 0x2C, .mode = SDCA_ACCESS_MODE_RO,   .layers = 0x4, .cn_list = 0x1, .label = SDCA_CTL_DEVICE_MANUFACTURER_ID_NAME },
	{ .sel = 0x2D, .mode = SDCA_ACCESS_MODE_RO,   .layers = 0x4, .cn_list = 0x1, .label = SDCA_CTL_DEVICE_PART_ID_NAME },
	{ .sel = 0x2E, .mode = SDCA_ACCESS_MODE_RO,   .layers = 0x4, .cn_list = 0x1, .label = SDCA_CTL_DEVICE_VERSION_NAME },
	{ .sel = 0x2F, .mode = SDCA_ACCESS_MODE_DC,   .layers = 0x4, .cn_list = 0x1, .values = ctrl_dev_sdca_ver_vals, .has_fixed = true, .label = SDCA_CTL_DEVICE_SDCA_VERSION_NAME },
};

/* Entity 1 (IT 41) Controls */
static struct sdca_control entity_it41_controls[] = {
	{ .sel = 0x4,  .mode = SDCA_ACCESS_MODE_RW, .layers = 0x4, .cn_list = 0x1,
	  .range = { .cols = 0x7, .rows = 0x1, .data = range_it41_usage_data },
	  .label = SDCA_CTL_USAGE_NAME },
	{ .sel = 0x8,  .mode = SDCA_ACCESS_MODE_DC, .layers = 0x4, .cn_list = 0x1,
	  .values = ctrl_it41_latency_vals, .has_fixed = true, .label = SDCA_CTL_LATENCY_NAME },
	{ .sel = 0x10, .mode = SDCA_ACCESS_MODE_DC, .layers = 0x4, .cn_list = 0x1,
	  .values = ctrl_it41_cluster_vals, .has_fixed = true,
	  .range = { .cols = 0x2, .rows = 0x1, .data = range_it41_cluster_data },
	  .label = SDCA_CTL_CLUSTERINDEX_NAME },
	{ .sel = 0x11, .mode = SDCA_ACCESS_MODE_DC, .layers = 0x4, .cn_list = 0x1,
	  .values = ctrl_it41_dp_vals, .has_fixed = true,
	  .range = { .cols = 0x10, .rows = 0x4, .data = range_it41_dp_data },
	  .label = SDCA_CTL_DATAPORT_SELECTOR_NAME },
};

/* Entity 2 (CS 41) Controls */
static struct sdca_control entity_cs41_controls[] = {
	{ .sel = 0x10, .mode = SDCA_ACCESS_MODE_DC, .layers = 0x4, .cn_list = 0x1,
	  .values = ctrl_cs41_sr_vals, .has_fixed = true,
	  .range = { .cols = 0x2, .rows = 0x1, .data = range_cs41_sr_data },
	  .label = SDCA_CTL_SAMPLERATEINDEX_NAME },
};

/* Entity 3 (MFPU 21) Controls */
static struct sdca_entity *entity_mfpu21_sources[] = {
	&wcd9378_sdca_entities[QSJ_IT41],
	&wcd9378_sdca_entities[QSJ_IT131],
};

static struct sdca_control entity_mfpu21_controls[] = {
	{ .sel = 0x1, .mode = SDCA_ACCESS_MODE_DC, .layers = 0x4, .cn_list = 0x1,
	  .values = ctrl_mfpu21_bypass_vals, .has_fixed = true, .label = SDCA_CTL_BYPASS_NAME },
};

/* Entity 4 (XU 42) Controls */
static struct sdca_entity *entity_xu42_sources[] = { &wcd9378_sdca_entities[QSJ_MFPU21] };

static struct sdca_control entity_xu42_controls[] = {
	{ .sel = 0x1, .mode = SDCA_ACCESS_MODE_RW, .layers = 0x4, .cn_list = 0x1, .label = SDCA_CTL_BYPASS_NAME },
	{ .sel = 0x7, .mode = SDCA_ACCESS_MODE_DC, .layers = 0x4, .cn_list = 0x1,
	  .values = ctrl_xu42_id_vals, .has_fixed = true, .label = SDCA_CTL_XU_ID_NAME },
	{ .sel = 0x8, .mode = SDCA_ACCESS_MODE_DC, .layers = 0x4, .cn_list = 0x1,
	  .values = ctrl_xu42_ver_vals, .has_fixed = true, .label = SDCA_CTL_XU_VERSION_NAME },
};

/* Entity 7 (SU 43) Controls */
static struct sdca_entity *entity_su43_sources[] = { &wcd9378_sdca_entities[QSJ_XU42] };

static struct sdca_control entity_su43_controls[] = {
	{ .sel = 0x1, .mode = SDCA_ACCESS_MODE_RO, .layers = SDCA_ACCESS_LAYER_DEVICE,
	  .cn_list = 0x1,
	  .range = { .cols = 0x1, .rows = 0x2, .data = range_su_sel_data },
	  .label = SDCA_CTL_SELECTOR_NAME },
};

/* Entity 8 (SU 45) Controls */
static struct sdca_entity *entity_su45_sources[] = { &wcd9378_sdca_entities[QSJ_XU42] };

static struct sdca_control entity_su45_controls[] = {
	{ .sel = 0x1, .mode = SDCA_ACCESS_MODE_RO, .layers = SDCA_ACCESS_LAYER_DEVICE,
	  .cn_list = 0x1,
	  .range = { .cols = 0x1, .rows = 0x2, .data = range_su_sel_data },
	  .label = SDCA_CTL_SELECTOR_NAME },
};

/* Entity 9 (PDE 47) - manages OT 43 and OT 45 */
static struct sdca_entity *entity_pde47_managed[] = {
	&wcd9378_sdca_entities[QSJ_OT43],
	&wcd9378_sdca_entities[QSJ_OT45],
};

static struct sdca_pde_delay pde47_delays[] = {
	{ .from_ps = 3, .to_ps = 0, .us = 30000 },
	{ .from_ps = 0, .to_ps = 3, .us = 30000 },
};

static struct sdca_control entity_pde47_controls[] = {
	{ .sel = 0x1,  .mode = SDCA_ACCESS_MODE_RW, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .range = { .cols = 0x1, .rows = 0x2, .data = range_pde_req_ps_data },
	  .label = SDCA_CTL_REQUESTED_PS_NAME },
	{ .sel = 0x10, .mode = SDCA_ACCESS_MODE_RO, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .is_volatile = true, .label = SDCA_CTL_ACTUAL_PS_NAME },
	/*
	 * Fake interrupt anchors for the WCD9378 HPH protection interrupts
	 * (INTMASK_1 bits 5/6/7 = HPH OCP/CNP, INTMASK_2 bit 0 = SDCA_8
	 * HPHL_CNP, INTMASK_3 bits 2/3 = SDCA_18/19 HPH surge det).  These
	 * SDCA_N positions have no matching ASL controls, so use invented
	 * selectors on PDE47 (which owns the HPH power domain) so the SDCA
	 * framework registers regmap-irq virqs for them.
	 *
	 * With virqs registered, regmap-irq's mask_buf_def includes these
	 * bits and enable_irq() unmasks them in SDW_SCP_SDCA_INTMASK1..3.
	 * When a bit fires the base_handler in sdca_interrupts.c logs the
	 * name ("PDE 47 HPHR OCP" etc.) and regmap-irq acks via INT1..4.
	 *
	 * sel values must be unique within the entity but are otherwise
	 * unused since the base_handler only logs the name.  Using values
	 * >0x30 to avoid collision with real SDCA controls.
	 */
	{ .sel = 0x30, .mode = SDCA_ACCESS_MODE_RO, .layers = SDCA_ACCESS_LAYER_CLASS,
	  .cn_list = 0x1, .interrupt_position = 5, .is_volatile = true,
	  .label = "HPHR OCP" },
	{ .sel = 0x31, .mode = SDCA_ACCESS_MODE_RO, .layers = SDCA_ACCESS_LAYER_CLASS,
	  .cn_list = 0x1, .interrupt_position = 6, .is_volatile = true,
	  .label = "HPHR CNP" },
	{ .sel = 0x32, .mode = SDCA_ACCESS_MODE_RO, .layers = SDCA_ACCESS_LAYER_CLASS,
	  .cn_list = 0x1, .interrupt_position = 7, .is_volatile = true,
	  .label = "HPHL OCP" },
	{ .sel = 0x33, .mode = SDCA_ACCESS_MODE_RO, .layers = SDCA_ACCESS_LAYER_CLASS,
	  .cn_list = 0x1, .interrupt_position = 8, .is_volatile = true,
	  .label = "HPHL CNP" },
	{ .sel = 0x34, .mode = SDCA_ACCESS_MODE_RO, .layers = SDCA_ACCESS_LAYER_CLASS,
	  .cn_list = 0x1, .interrupt_position = 18, .is_volatile = true,
	  .label = "HPHR SURGE" },
	{ .sel = 0x35, .mode = SDCA_ACCESS_MODE_RO, .layers = SDCA_ACCESS_LAYER_CLASS,
	  .cn_list = 0x1, .interrupt_position = 19, .is_volatile = true,
	  .label = "HPHL SURGE" },
};

/* Entity 0xA (OT 43) - Headphone, no controls */
static struct sdca_entity *entity_ot43_sources[] = { &wcd9378_sdca_entities[QSJ_IT41] };

/* Entity 0xB (OT 45) - Headset, no controls */
static struct sdca_entity *entity_ot45_sources[] = { &wcd9378_sdca_entities[QSJ_IT41] };

/* Entity 0xC (GE 35) - jack detection group entity */

/*
 * WORKAROUND: modes 0 (Jack Unplugged) and 1 (Jack Unknown) route SU 45
 * the same as mode 4 (Headphone) so the DAPM path IT41 -> SU45 -> OT43
 * stays connected even when the Tambora firmware cannot complete
 * HP/HS discrimination and leaves DETECTED_MODE at 1.  See the
 * range_ge35_mode_data comment above.
 */
static struct sdca_ge_control ge35_mode0_controls[] = {
	{ .id = 0x8, .sel = 0x1, .cn = 0x0, .val = 0x1 },	/* SU 45 selector = source 1 (XU 42) */
};

static struct sdca_ge_control ge35_mode1_controls[] = {
	{ .id = 0x8, .sel = 0x1, .cn = 0x0, .val = 0x1 },	/* SU 45 selector = source 1 (XU 42) */
};

static struct sdca_ge_control ge35_mode3_controls[] = {
	{ .id = 0x7, .sel = 0x1, .cn = 0x0, .val = 0x1 },	/* SU 43 selector = source 1 (XU 42) */
};

static struct sdca_ge_control ge35_mode4_controls[] = {
	{ .id = 0x8, .sel = 0x1, .cn = 0x0, .val = 0x1 },	/* SU 45 selector = source 1 (XU 42) */
};

static struct sdca_ge_mode ge35_modes[] = {
	{ .val = 0x0, .num_controls = ARRAY_SIZE(ge35_mode0_controls), .controls = ge35_mode0_controls },
	{ .val = 0x1, .num_controls = ARRAY_SIZE(ge35_mode1_controls), .controls = ge35_mode1_controls },
	{ .val = 0x3, .num_controls = ARRAY_SIZE(ge35_mode3_controls), .controls = ge35_mode3_controls },
	{ .val = 0x4, .num_controls = ARRAY_SIZE(ge35_mode4_controls), .controls = ge35_mode4_controls },
};

static struct sdca_control entity_ge35_controls[] = {
	{ .sel = 0x1, .mode = SDCA_ACCESS_MODE_RW, .layers = 0x4, .cn_list = 0x1,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .range = { .cols = 0x2, .rows = 0x4, .data = range_ge35_mode_data },
	  .label = SDCA_CTL_SELECTED_MODE_NAME },
	{ .sel = 0x2, .mode = SDCA_ACCESS_MODE_RO, .layers = 0x4, .cn_list = 0x1,
	  .interrupt_position = 4, /* SDCA_4 = GE_DETECTED_MODE, see init_table INTMASK_1 */
	  .is_volatile = true, .label = SDCA_CTL_DETECTED_MODE_NAME },
};

/* Entity 0xD (IT 131) - optimization stream input */
static struct sdca_control entity_it131_controls[] = {
	{ .sel = 0x4,  .mode = SDCA_ACCESS_MODE_RW, .layers = 0x4, .cn_list = 0x1,
	  .range = { .cols = 0x7, .rows = 0x1, .data = range_it131_usage_data },
	  .label = SDCA_CTL_USAGE_NAME },
	{ .sel = 0x8,  .mode = SDCA_ACCESS_MODE_DC, .layers = 0x4, .cn_list = 0x1,
	  .values = ctrl_it131_latency_vals, .has_fixed = true, .label = SDCA_CTL_LATENCY_NAME },
	{ .sel = 0x10, .mode = SDCA_ACCESS_MODE_DC, .layers = 0x4, .cn_list = 0x1,
	  .values = ctrl_it131_cluster_vals, .has_fixed = true,
	  .range = { .cols = 0x2, .rows = 0x1, .data = range_it131_cluster_data },
	  .label = SDCA_CTL_CLUSTERINDEX_NAME },
	{ .sel = 0x11, .mode = SDCA_ACCESS_MODE_DC, .layers = 0x4, .cn_list = 0x1,
	  .values = ctrl_it131_dp_vals, .has_fixed = true,
	  .range = { .cols = 0x10, .rows = 0x4, .data = range_it131_dp_data },
	  .label = SDCA_CTL_DATAPORT_SELECTOR_NAME },
};

/* Entity 0xE (CS 131) Controls */
static struct sdca_control entity_cs131_controls[] = {
	{ .sel = 0x10, .mode = SDCA_ACCESS_MODE_DC, .layers = 0x4, .cn_list = 0x1,
	  .values = ctrl_cs131_sr_vals, .has_fixed = true,
	  .range = { .cols = 0x2, .rows = 0x1, .data = range_cs131_sr_data },
	  .label = SDCA_CTL_SAMPLERATEINDEX_NAME },
};

/* Entity 0xF (IT 33) - headset mic input */
static struct sdca_control entity_it33_controls[] = {
	{ .sel = 0x3,  .mode = SDCA_ACCESS_MODE_RW, .layers = 0x4, .cn_list = 0x1,
	  .values = ctrl_it33_micbias_vals, .has_default = true,
	  .range = { .cols = 0x1, .rows = 0x1, .data = range_it33_micbias_data },
	  .label = SDCA_CTL_MIC_BIAS_NAME },
	{ .sel = 0x4,  .mode = SDCA_ACCESS_MODE_RW, .layers = 0x4, .cn_list = 0x1,
	  .range = { .cols = 0x7, .rows = 0x1, .data = range_it33_usage_data },
	  .has_reset = true,
	  .label = SDCA_CTL_USAGE_NAME },
	{ .sel = 0x8,  .mode = SDCA_ACCESS_MODE_DC, .layers = 0x4, .cn_list = 0x1,
	  .values = ctrl_it33_latency_vals, .has_fixed = true, .label = SDCA_CTL_LATENCY_NAME },
	{ .sel = 0x10, .mode = SDCA_ACCESS_MODE_DC, .layers = 0x4, .cn_list = 0x1,
	  .values = ctrl_it33_cluster_vals, .has_fixed = true,
	  .range = { .cols = 0x2, .rows = 0x1, .data = range_it33_cluster_data },
	  .label = SDCA_CTL_CLUSTERINDEX_NAME },
};

/* Entity 0x10 (PDE 34) - manages IT 33 */
static struct sdca_entity *entity_pde34_managed[] = { &wcd9378_sdca_entities[QSJ_IT33] };

static struct sdca_pde_delay pde34_delays[] = {
	{ .from_ps = 3, .to_ps = 0, .us = 30000 },
	{ .from_ps = 0, .to_ps = 3, .us = 30000 },
};

static struct sdca_control entity_pde34_controls[] = {
	{ .sel = 0x1,  .mode = SDCA_ACCESS_MODE_RW, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .range = { .cols = 0x1, .rows = 0x2, .data = range_pde_req_ps_data },
	  .label = SDCA_CTL_REQUESTED_PS_NAME },
	{ .sel = 0x10, .mode = SDCA_ACCESS_MODE_RO, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .is_volatile = true, .label = SDCA_CTL_ACTUAL_PS_NAME },
};

/* Entity 0x11 (FU 33) - no controls */
static struct sdca_entity *entity_fu33_sources[] = { &wcd9378_sdca_entities[QSJ_IT33] };

/* Entity 0x12 (SU 35) Controls */
static struct sdca_entity *entity_su35_sources[] = { &wcd9378_sdca_entities[QSJ_FU33] };

static struct sdca_control entity_su35_controls[] = {
	{ .sel = 0x1, .mode = SDCA_ACCESS_MODE_RO, .layers = 0x4, .cn_list = 0x1,
	  .range = { .cols = 0x1, .rows = 0x2, .data = range_su_sel_data },
	  .label = SDCA_CTL_SELECTOR_NAME },
};

/* Entity 0x13 (XU 36) Controls */
static struct sdca_entity *entity_xu36_sources[] = { &wcd9378_sdca_entities[QSJ_SU35] };

static int ctrl_xu36_bypass_vals[] = { 0x1 };

static struct sdca_control entity_xu36_controls[] = {
	/* bypass=1: pass mic signal through XU36 without proprietary processing */
	{ .sel = 0x1, .mode = SDCA_ACCESS_MODE_RW, .layers = 0x4, .cn_list = 0x1,
	  .values = ctrl_xu36_bypass_vals, .has_default = true, .label = SDCA_CTL_BYPASS_NAME },
	{ .sel = 0x7, .mode = SDCA_ACCESS_MODE_DC, .layers = 0x4, .cn_list = 0x1,
	  .values = ctrl_xu36_id_vals, .has_fixed = true, .label = SDCA_CTL_XU_ID_NAME },
	{ .sel = 0x8, .mode = SDCA_ACCESS_MODE_DC, .layers = 0x4, .cn_list = 0x1,
	  .values = ctrl_xu36_ver_vals, .has_fixed = true, .label = SDCA_CTL_XU_VERSION_NAME },
};

/* Entity 0x15 (CS 36) Controls */
static struct sdca_control entity_cs36_controls[] = {
	{ .sel = 0x10, .mode = SDCA_ACCESS_MODE_DC, .layers = 0x4, .cn_list = 0x1,
	  .values = ctrl_cs36_sr_vals, .has_fixed = true,
	  .range = { .cols = 0x2, .rows = 0x1, .data = range_cs36_sr_data },
	  .label = SDCA_CTL_SAMPLERATEINDEX_NAME },
};

/* Entity 0x16 (OT 36) - mic capture output; signal chain: IT33→FU33→SU35→XU36→OT36 */
static struct sdca_entity *entity_ot36_sources[] = { &wcd9378_sdca_entities[QSJ_XU36] };

static struct sdca_control entity_ot36_controls[] = {
	{ .sel = 0x4,  .mode = SDCA_ACCESS_MODE_RW, .layers = 0x4, .cn_list = 0x1,
	  .range = { .cols = 0x7, .rows = 0x1, .data = range_ot36_usage_data },
	  .label = SDCA_CTL_USAGE_NAME },
	{ .sel = 0x8,  .mode = SDCA_ACCESS_MODE_DC, .layers = 0x4, .cn_list = 0x1,
	  .values = ctrl_ot36_latency_vals, .has_fixed = true, .label = SDCA_CTL_LATENCY_NAME },
	{ .sel = 0x11, .mode = SDCA_ACCESS_MODE_DC, .layers = 0x4, .cn_list = 0x1,
	  .values = ctrl_ot36_dp_vals, .has_fixed = true,
	  .range = { .cols = 0x10, .rows = 0x4, .data = range_ot36_dp_data },
	  .label = SDCA_CTL_DATAPORT_SELECTOR_NAME },
};

/* Entity 0x17 (MFPU 236) Controls */
static struct sdca_control entity_mfpu236_controls[] = {
	{ .sel = 0x1, .mode = SDCA_ACCESS_MODE_DC, .layers = 0x4, .cn_list = 0x1,
	  .values = ctrl_mfpu236_bypass_vals, .has_fixed = true, .label = SDCA_CTL_BYPASS_NAME },
};

/* Entity 0x18 (CS 236) Controls */
static struct sdca_control entity_cs236_controls[] = {
	{ .sel = 0x10, .mode = SDCA_ACCESS_MODE_DC, .layers = 0x4, .cn_list = 0x1,
	  .values = ctrl_cs236_sr_vals, .has_fixed = true,
	  .range = { .cols = 0x2, .rows = 0x1, .data = range_cs236_sr_data },
	  .label = SDCA_CTL_SAMPLERATEINDEX_NAME },
};

/* Entity 0x19 (OT 236) - optimization stream capture output */
static struct sdca_entity *entity_ot236_sources[] = { &wcd9378_sdca_entities[QSJ_IT33] };

static struct sdca_control entity_ot236_controls[] = {
	{ .sel = 0x4,  .mode = SDCA_ACCESS_MODE_RW, .layers = 0x4, .cn_list = 0x1,
	  .range = { .cols = 0x7, .rows = 0x1, .data = range_ot236_usage_data },
	  .label = SDCA_CTL_USAGE_NAME },
	{ .sel = 0x8,  .mode = SDCA_ACCESS_MODE_DC, .layers = 0x4, .cn_list = 0x1,
	  .values = ctrl_ot236_latency_vals, .has_fixed = true, .label = SDCA_CTL_LATENCY_NAME },
	{ .sel = 0x11, .mode = SDCA_ACCESS_MODE_DC, .layers = 0x4, .cn_list = 0x1,
	  .values = ctrl_ot236_dp_vals, .has_fixed = true,
	  .range = { .cols = 0x10, .rows = 0x4, .data = range_ot236_dp_data },
	  .label = SDCA_CTL_DATAPORT_SELECTOR_NAME },
};

/* Entities Array (see index map at top of file) */
static struct sdca_entity wcd9378_sdca_entities[] = {
	/* [0] E001: IT 41 - PDM render stream input */
	{ .id = 0x1, .label = "IT 41", .type = SDCA_ENTITY_TYPE_IT,
	  .iot = { .type = 0x0191, .is_dataport = true, .clock = &wcd9378_sdca_entities[QSJ_CS41] },
	  .num_controls = ARRAY_SIZE(entity_it41_controls), .controls = entity_it41_controls },
	/* [1] E002: CS 41 */
	{ .id = 0x2, .label = "CS 41", .type = SDCA_ENTITY_TYPE_CS,
	  .cs = { .type = 0x0 },
	  .num_controls = ARRAY_SIZE(entity_cs41_controls), .controls = entity_cs41_controls },
	/* [2] E003: MFPU 21 */
	{ .id = 0x3, .label = "MFPU 21", .type = SDCA_ENTITY_TYPE_MFPU,
	  .num_controls = ARRAY_SIZE(entity_mfpu21_controls), .controls = entity_mfpu21_controls,
	  .num_sources = ARRAY_SIZE(entity_mfpu21_sources), .sources = entity_mfpu21_sources },
	/* [3] E004: XU 42 */
	{ .id = 0x4, .label = "XU 42", .type = SDCA_ENTITY_TYPE_XU,
	  .num_controls = ARRAY_SIZE(entity_xu42_controls), .controls = entity_xu42_controls,
	  .num_sources = ARRAY_SIZE(entity_xu42_sources), .sources = entity_xu42_sources },
	/* [4] E007: SU 43 - render selector (headset path), driven by GE 35 jack detection */
	{ .id = 0x7, .label = "SU 43", .type = SDCA_ENTITY_TYPE_SU,
	  .group = &wcd9378_sdca_entities[QSJ_GE35],
	  .num_controls = ARRAY_SIZE(entity_su43_controls), .controls = entity_su43_controls,
	  .num_sources = ARRAY_SIZE(entity_su43_sources), .sources = entity_su43_sources },
	/* [5] E008: SU 45 - render selector (headphone path), driven by GE 35 jack detection */
	{ .id = 0x8, .label = "SU 45", .type = SDCA_ENTITY_TYPE_SU,
	  .group = &wcd9378_sdca_entities[QSJ_GE35],
	  .num_controls = ARRAY_SIZE(entity_su45_controls), .controls = entity_su45_controls,
	  .num_sources = ARRAY_SIZE(entity_su45_sources), .sources = entity_su45_sources },
	/* [6] E009: PDE 47 - render power domain */
	{ .id = 0x9, .label = "PDE 47", .type = SDCA_ENTITY_TYPE_PDE,
	  .pde = { .num_managed = ARRAY_SIZE(entity_pde47_managed), .managed = entity_pde47_managed,
		   .num_max_delay = ARRAY_SIZE(pde47_delays), .max_delay = pde47_delays },
	  .num_controls = ARRAY_SIZE(entity_pde47_controls), .controls = entity_pde47_controls },
	/* [7] E00A: OT 43 - Headphone on jack */
	{ .id = 0xA, .label = "OT 43", .type = SDCA_ENTITY_TYPE_OT,
	  .iot = { .type = 0x06C0 },
	  .num_sources = ARRAY_SIZE(entity_ot43_sources), .sources = entity_ot43_sources },
	/* [8] E00B: OT 45 - Headset output on jack */
	{ .id = 0xB, .label = "OT 45", .type = SDCA_ENTITY_TYPE_OT,
	  .iot = { .type = 0x06D0 },
	  .num_sources = ARRAY_SIZE(entity_ot45_sources), .sources = entity_ot45_sources },
	/* [9] E00C: GE 35 - jack detection group entity */
	{ .id = 0xC, .label = "GE 35", .type = SDCA_ENTITY_TYPE_GE,
	  .ge = { .num_modes = ARRAY_SIZE(ge35_modes), .modes = ge35_modes },
	  .num_controls = ARRAY_SIZE(entity_ge35_controls), .controls = entity_ge35_controls },
	/* [10] E00D: IT 131 - optimization stream input */
	{ .id = 0xD, .label = "IT 131", .type = SDCA_ENTITY_TYPE_IT,
	  .iot = { .type = 0x0190, .is_dataport = true, .clock = &wcd9378_sdca_entities[QSJ_CS131] },
	  .num_controls = ARRAY_SIZE(entity_it131_controls), .controls = entity_it131_controls },
	/* [11] E00E: CS 131 */
	{ .id = 0xE, .label = "CS 131", .type = SDCA_ENTITY_TYPE_CS,
	  .cs = { .type = 0x0 },
	  .num_controls = ARRAY_SIZE(entity_cs131_controls), .controls = entity_cs131_controls },
	/* [12] E00F: IT 33 - headset mic input */
	{ .id = 0xF, .label = "IT 33", .type = SDCA_ENTITY_TYPE_IT,
	  .iot = { .type = 0x06D0 },
	  .num_controls = ARRAY_SIZE(entity_it33_controls), .controls = entity_it33_controls },
	/* [13] E010: PDE 34 - mic power domain */
	{ .id = 0x10, .label = "PDE 34", .type = SDCA_ENTITY_TYPE_PDE,
	  .pde = { .num_managed = ARRAY_SIZE(entity_pde34_managed), .managed = entity_pde34_managed,
		   .num_max_delay = ARRAY_SIZE(pde34_delays), .max_delay = pde34_delays },
	  .num_controls = ARRAY_SIZE(entity_pde34_controls), .controls = entity_pde34_controls },
	/* [14] E011: FU 33 - mic feature unit */
	{ .id = 0x11, .label = "FU 33", .type = SDCA_ENTITY_TYPE_FU,
	  .num_sources = ARRAY_SIZE(entity_fu33_sources), .sources = entity_fu33_sources },
	/* [15] E012: SU 35 - mic selector */
	{ .id = 0x12, .label = "SU 35", .type = SDCA_ENTITY_TYPE_SU,
	  .num_controls = ARRAY_SIZE(entity_su35_controls), .controls = entity_su35_controls,
	  .num_sources = ARRAY_SIZE(entity_su35_sources), .sources = entity_su35_sources },
	/* [16] E013: XU 36 - mic extension unit */
	{ .id = 0x13, .label = "XU 36", .type = SDCA_ENTITY_TYPE_XU,
	  .num_controls = ARRAY_SIZE(entity_xu36_controls), .controls = entity_xu36_controls,
	  .num_sources = ARRAY_SIZE(entity_xu36_sources), .sources = entity_xu36_sources },
	/* [17] E015: CS 36 */
	{ .id = 0x15, .label = "CS 36", .type = SDCA_ENTITY_TYPE_CS,
	  .cs = { .type = 0x0 },
	  .num_controls = ARRAY_SIZE(entity_cs36_controls), .controls = entity_cs36_controls },
	/* [18] E016: OT 36 - PDM mic capture output */
	{ .id = 0x16, .label = "OT 36", .type = SDCA_ENTITY_TYPE_OT,
	  .iot = { .type = 0x0191, .is_dataport = true, .clock = &wcd9378_sdca_entities[QSJ_CS36] },
	  .num_controls = ARRAY_SIZE(entity_ot36_controls), .controls = entity_ot36_controls,
	  .num_sources = ARRAY_SIZE(entity_ot36_sources), .sources = entity_ot36_sources },
	/* [19] E017: MFPU 236 - optimization TX processing */
	{ .id = 0x17, .label = "MFPU 236", .type = SDCA_ENTITY_TYPE_MFPU,
	  .num_controls = ARRAY_SIZE(entity_mfpu236_controls), .controls = entity_mfpu236_controls },
	/* [20] E018: CS 236 */
	{ .id = 0x18, .label = "CS 236", .type = SDCA_ENTITY_TYPE_CS,
	  .cs = { .type = 0x0 },
	  .num_controls = ARRAY_SIZE(entity_cs236_controls), .controls = entity_cs236_controls },
	/* [21] E019: OT 236 - optimization stream capture output */
	{ .id = 0x19, .label = "OT 236", .type = SDCA_ENTITY_TYPE_OT,
	  .iot = { .type = 0x0190, .is_dataport = true, .clock = &wcd9378_sdca_entities[QSJ_CS131] },
	  .num_controls = ARRAY_SIZE(entity_ot236_controls), .controls = entity_ot236_controls,
	  .num_sources = ARRAY_SIZE(entity_ot236_sources), .sources = entity_ot236_sources },
	/* Entity 0 (Function) */
	{ .id = 0x0, .label = "entity0",
	  .num_controls = ARRAY_SIZE(entity0_controls), .controls = entity0_controls },
};

/* Clusters */
static struct sdca_channel cl1_channels[] = {	/* CL01 - render (HPH) stereo */
	{ .id = 0x1, .purpose = 0x1, .relationship = 0x2 },	/* Left */
	{ .id = 0x2, .purpose = 0x1, .relationship = 0x3 },	/* Right */
};

static struct sdca_channel cl2_channels[] = {	/* CL02 - mic capture mono */
	{ .id = 0xFF, .purpose = 0x1, .relationship = 0x1 },
};

static struct sdca_channel cl3_channels[] = {	/* CL03 - optimization RX */
	{ .id = 0xFF, .purpose = 0x1, .relationship = 0x1 },	/* Mono */
	{ .id = 0x1,  .purpose = 0x1, .relationship = 0x2 },	/* Left */
	{ .id = 0x2,  .purpose = 0x1, .relationship = 0x3 },	/* Right */
};

static struct sdca_channel cl5_channels[] = {	/* CL05 - optimization TX mono */
	{ .id = 0xFF, .purpose = 0x1, .relationship = 0x1 },
};

static struct sdca_cluster wcd9378_sdca_clusters[] = {
	{ .id = 0x1, .num_channels = ARRAY_SIZE(cl1_channels), .channels = cl1_channels },
	{ .id = 0x2, .num_channels = ARRAY_SIZE(cl2_channels), .channels = cl2_channels },
	{ .id = 0x3, .num_channels = ARRAY_SIZE(cl3_channels), .channels = cl3_channels },
	{ .id = 0x5, .num_channels = ARRAY_SIZE(cl5_channels), .channels = cl5_channels },
};

/*
 * Init Table (from QcSimpleJack.asl BUF0).
 *
 * 32-bit register addresses with single-byte values, transcribed from the
 * factory ACPI initialization table.
 */
static struct sdca_init_write wcd9378_sdca_init_table[] = {
	{ .addr = 0x401804F0, .val = 0x00 }, /* DIGITAL_PLATFORM_CTL */
	{ .addr = 0x4018046E, .val = 0x10 }, /* DIGITAL_INTR_MODE */
	{ .addr = 0x0000004D, .val = 0x01 }, /* SWRS_SCP_BUSCLOCK_BASE */
	{ .addr = 0x00000062, .val = 0x02 }, /* SWRS_SCP_BUSCLOCK_SCALE_BANK */
	{ .addr = 0x4018016A, .val = 0x80 }, /* CP_DTOP_CTRL_14 */
	{ .addr = 0x40180165, .val = 0x6b }, /* CP_DTOP_CTRL_9 */
	{ .addr = 0x40180103, .val = 0x1E }, /* SLEEP_CTL BG_CTL (0.9V) */
	{ .addr = 0x40180103, .val = 0x9E }, /* SLEEP_CTL BG_EN */
	{ .addr = 0x40180103, .val = 0xDE }, /* SLEEP_CTL LDOL_BG_SEL */
	{ .addr = 0x40180029, .val = 0xB5 }, /* BIAS_VBG_FINE_ADJ */
	{ .addr = 0x40180001, .val = 0x80 }, /* ANA_BIAS ANALOG_BIAS_EN */
	{ .addr = 0x40180001, .val = 0xC0 }, /* ANA_BIAS PRECHRG_EN(1) */
	{ .addr = 0x40180001, .val = 0x80 }, /* ANA_BIAS PRECHRG_EN(0) */
	{ .addr = 0x4018007B, .val = 0xA2 }, /* TX_COM_TXFE_DIV_CTL SEQ_BYPASS */
	{ .addr = 0x40180465, .val = 0x17 }, /* PDM_WD_CTL0 TIME_OUT_SEL_PCM */
	{ .addr = 0x40180466, .val = 0x17 }, /* PDM_WD_CTL1 TIME_OUT_SEL_PCM */
	{ .addr = 0x4018006C, .val = 0x01 }, /* MICB1_TEST_CTL_2 IBIAS_LDO_DRIVER */
	{ .addr = 0x40180072, .val = 0x81 }, /* MICB3_TEST_CTL_2 IBIAS_LDO_DRIVER */
	{ .addr = 0x401800CE, .val = 0x38 }, /* HPH_OCP_CTL OCP_FSM_EN */
	{ .addr = 0x401800CE, .val = 0x3A }, /* HPH_OCP_CTL SCD_OP_EN */
	{ .addr = 0x401800D4, .val = 0xE1 }, /* HPH_L_TEST OCP_DET_EN */
	{ .addr = 0x401800D7, .val = 0xE1 }, /* HPH_R_TEST OCP_DET_EN */
	{ .addr = 0x4018044E, .val = 0x04 }, /* CDC_HPH_GAIN_CTL HPHL_RX_EN */
	{ .addr = 0x4018044E, .val = 0x0C }, /* CDC_HPH_GAIN_CTL HPHR_RX_EN */
	{ .addr = 0x4018000F, .val = 0x0C }, /* ANA_TX_CH2 GAIN (18.0dB) */
	{ .addr = 0x40180133, .val = 0x84 }, /* HPH_NEW_INT_RDAC_HD2_CTL_L */
	{ .addr = 0x40180136, .val = 0x84 }, /* HPH_NEW_INT_RDAC_HD2_CTL_R */
	{ .addr = 0x401800D9, .val = 0x19 }, /* HPH_RDAC_CLK_CTL1 OPAMP_CHOP_CLK_EN */
	{ .addr = 0x40180132, .val = 0x50 }, /* HPH_NEW_INT_RDAC_GAIN_CTL RDAC_GAINCTL(0.55) — validated spec */
	{ .addr = 0x401800A5, .val = 0xE0 }, /* FLYBACK_VNEG_CTRL_1 */
	{ .addr = 0x401800A8, .val = 0x70 }, /* FLYBACK_VNEG_CTRL_4 */
	{ .addr = 0x4018013A, .val = 0xFC }, /* HPH_NEW_INT_HPH_TIMER1 */
	{ .addr = 0x401801C5, .val = 0x13 }, /* PDM_WD_CTL0 (HPHR) */
	{ .addr = 0x401801C6, .val = 0x13 }, /* PDM_WD_CTL1 (HPHL) */
	{ .addr = 0x40180510, .val = 0x05 }, /* SEQR_CTRL HPH_UP_T0 */
	{ .addr = 0x40180519, .val = 0x05 }, /* SEQR_CTRL HPH_UP_T9 */
	{ .addr = 0x4018051B, .val = 0x06 }, /* SEQR_CTRL HPH_DN_T0 */
	{ .addr = 0x40180414, .val = 0x02 }, /* CDC_COMP_CTL_0 HPHL_COMP_EN */
	{ .addr = 0x40180414, .val = 0x03 }, /* CDC_COMP_CTL_0 HPHR_COMP_EN */
	{ .addr = 0x401804F2, .val = 0x80 }, /* DRE_DLY_VAL SWR_HPHL(0) */
	{ .addr = 0x401804F2, .val = 0x00 }, /* DRE_DLY_VAL SWR_HPHR(0) */
	{ .addr = 0x40180501, .val = 0x01 }, /* SEQR_CTRL SYS_USAGE_CTRL */
	/*
	 * DEVICE_DET (0x40180601): re-added.  The validation team's
	 * tambora_reg_writes.xlsx does NOT write this register, but the
	 * original ASL init table (QcSimpleJack.asl BUF0 line 131) does.
	 * With DEVICE_DET=0x01 and the firmware in SDCA-mode MBHC reporting,
	 * physical jack insertion should assert SDCA_4 (GE_DETECTED_MODE).
	 * Now that the SDCA interrupt dispatch chain works (post MCLK +
	 * use_domain_irq fixes), test whether this write is what enables the
	 * firmware's MBHC state machine on our part.
	 */
	{ .addr = 0x40180601, .val = 0x01 }, /* MBHC_CTRL DEVICE_DET */
	{ .addr = 0x40180414, .val = 0x00 }, /* CDC_COMP_CTL_0 */
	{ .addr = 0x401804F2, .val = 0x88 }, /* DRE_DLY_VAL */
	{ .addr = 0x40180517, .val = 0x07 }, /* SEQR_CTRL HPH_UP_T7 */
	{ .addr = 0x4018051C, .val = 0x07 }, /* SEQR_CTRL HPH_DN_T1 */
	{ .addr = 0x401800CE, .val = 0x28 }, /* HPH_OCP_CTL */
	{ .addr = 0x401800D4, .val = 0xe0 }, /* HPH_L_TEST */
	{ .addr = 0x401800D7, .val = 0xe0 }, /* HPH_R_TEST */
	{ .addr = 0x40180510, .val = 0x07 }, /* SEQR_CTRL HPH_UP_T0 */
	{ .addr = 0x40C80008, .val = 0x01 }, /* SMP_JACK_CTRL FUNC_ACT (RESET_FUNCTION_NOW) */
	{ .addr = 0x40C00008, .val = 0x02 }, /* SMP_JACK_CTRL CMT_GRP_MASK */
	{ .addr = 0x40C80000, .val = 0xFF }, /* SMP_JACK_CTRL FUNC_STAT */
	{ .addr = 0x00000000, .val = 0x08 }, /* clear DP0 INT status SDCA_CASCADE */
	{ .addr = 0x00000041, .val = 0x08 }, /* SCP_INT_STATUS_MASK_1 PORT_0_CASCADE_3 */
	{ .addr = 0x000000f4, .val = 0xff }, /* SWRS_SCP_SDCA_INTRTYPE_1 */
	{ .addr = 0x000000f8, .val = 0x0b }, /* SWRS_SCP_SDCA_INTRTYPE_2 */
	{ .addr = 0x000000fc, .val = 0xff }, /* SWRS_SCP_SDCA_INTRTYPE_3 */
	/*
	 * INTMASK: only SDCA_4 (GE_DETECTED_MODE) unmasked.  Validation dump
	 * enables SDCA_5-8/18/19 too (HPH OCP/CNP/surge) but they cause an
	 * interrupt storm during HPH power-up: the SoundWire alert cascade
	 * fires but regmap_irq_thread never dispatches (INTSTAT_1..4 at
	 * 0x58-0x5B are never read on our platform, possibly due to the
	 * "no bus MCLK, cannot set SDW_SCP_BUS_CLOCK_BASE" bus-init failure
	 * that returns -22 from sdw_initialize_slave).  Without a working
	 * dispatch path, unmasking these bits means the cascade never
	 * clears -> MAX_RETRY spam.  Keep them masked until the underlying
	 * bus-init/PM issue is resolved.
	 */
	{ .addr = 0x0000005C, .val = 0x10 }, /* INTMASK_1 SDCA_4 (GE_DETECTED_MODE) */
	{ .addr = 0x4018016A, .val = 0x00 }, /* CP_DTOP_CTRL_14 */
	{ .addr = 0x40180165, .val = 0x6b }, /* CP_DTOP_CTRL_9 */
	/* MBHC registers must NOT be here: enabling L_DET_EN (ANA_MBHC_MECH) before
	 * the machine card registers causes continuous SDCA_4 interrupts that block
	 * the deferred card probe.  Initialised via hw_ops->set_jack() instead. */
};

/* Function Descriptor */
static struct sdca_function_desc wcd9378_sdca_desc = {
	.adr  = 0x3,
	.type = SDCA_FUNCTION_TYPE_SIMPLE_JACK,
	.name = SDCA_FUNCTION_TYPE_SIMPLE_NAME,
};

/* Main Function Data */
static struct sdca_function_data wcd9378_sdca_data = {
	.desc             = &wcd9378_sdca_desc,
	.num_entities     = ARRAY_SIZE(wcd9378_sdca_entities),
	.entities         = wcd9378_sdca_entities,
	.num_clusters     = ARRAY_SIZE(wcd9378_sdca_clusters),
	.clusters         = wcd9378_sdca_clusters,
	.num_init_table   = ARRAY_SIZE(wcd9378_sdca_init_table),
	.init_table       = wcd9378_sdca_init_table,
	.reset_max_delay  = 100000, /* 100ms — WCD9378 power-on reset completes well within this */
};

/*
 * IT33 (headset mic input terminal) SDCA control for the AMIC2 bias
 * rail.  The framework's write_defaults programs 2V75 into the cache at
 * probe, but the codec's RESET_FUNCTION_NOW during class boot clears
 * the hardware value while the cache remains valid, so no PDE-time
 * write ever reaches the codec.  Drop the cache and re-issue the write
 * from pde_pre_pmu to ensure the bias rail is up before mic capture.
 */
#define WCD9378_IT33_ENTITY_ID		0x0F
#define WCD9378_IT33_MICBIAS_SEL	0x03
#define WCD9378_IT33_MICBIAS_2V75	0x05

/* PDE34 (entity_id=0x10) manages the headset-mic (IT33/AMIC2) power domain */
#define WCD9378_PDE34_ENTITY_ID		0x10

/* PDE47 (entity_id=0x09) manages the headphone (HPH L/R) power domain */
#define WCD9378_PDE47_ENTITY_ID		0x09

/*
 * FU42 vendor Feature Unit — mute (CH1/CH2) and 16-bit Q7.8 dB channel
 * volume.  Writes to the immediate FU42 alias are ignored by the codec;
 * state changes only latch through the _CN (BIT(14)) alias followed by
 * SoundWire SCP_COMMIT, and the codec re-mutes FU42 on every PDE47
 * PS3->PS0 cycle so the sequence has to run from pde_post_pmu.
 * TODO: model FU42 as a proper SDCA Feature Unit entity once the SDCA
 * framework supports DUAL-mode writes via the NEXT alias + SCP_COMMIT
 * and re-applies defaults on PDE power-up.
 */
#define WCD9378_FU42_MUTE_CH1_ADDR		0x40c04309
#define WCD9378_FU42_MUTE_CH2_ADDR		0x40c0430a
#define WCD9378_FU42_MUTE_UNMUTE		0x00
#define WCD9378_FU42_CH_VOL_CH1_MSB_ADDR	0x40c06311
#define WCD9378_FU42_CH_VOL_CH1_LSB_ADDR	0x40c04311
#define WCD9378_FU42_CH_VOL_CH2_MSB_ADDR	0x40c06312
#define WCD9378_FU42_CH_VOL_CH2_LSB_ADDR	0x40c04312
#define WCD9378_FU42_CH_VOL_UNITY_BYTE		0x00

/*
 * Entity_0 Commit_Group_Count.  Must be written before the staged FU42
 * writes go on the bus so the codec latches the count first.  Value 2
 * groups the FU42 mute and volume writes into a single SCP_COMMIT.
 */
#define WCD9378_ENT0_COMMIT_GROUP_COUNT_ADDR	0x40c00008
#define WCD9378_ENT0_COMMIT_GROUP_COUNT_TWO	0x02

/*
 * ANA_MBHC_MECH configures the mechanical jack-detect comparator.  Value
 * 0xB5 sets L_DET_EN, MECH_DET_TYP, HPHL_PLUG_TP,
 * HS_L_DET_PULLUP_COMP_CTRL and SW_HPH_LP_100K_TO_GND — the standard
 * enable pattern for a 3-pole/4-pole detect.
 */
#define WCD9378_ANA_MBHC_MECH_ADDR		0x40180014
#define WCD9378_ANA_MBHC_MECH_ENABLE		0xB5

/* Vendor-specific SCP register: enable host clock divide-by-2 (bank 1 shadow) */
#define WCD9378_SCP_HOST_CLK_DIV2_CTL_B1	0xF0

/* Slave ports 1..7 are mapped to master ports via qcom,port-mapping */
#define WCD9378_SDCA_MAX_PORTS	7

static const char * const wcd9378_sdca_supplies[] = {
	"vdd-buck", "vdd-rxtx", "vdd-io", "vdd-mic-bias",
};

static int wcd9378_sdca_sdw_read_prop(struct sdw_slave *slave)
{
	struct sdw_slave_prop *prop = &slave->prop;
	struct device *dev = &slave->dev;
	struct sdw_dpn_prop *sink, *src;
	int ret;

	ret = sdca_class_read_prop(slave);
	if (ret)
		return ret;

	/* Configure SoundWire slave properties missing from DT/DisCo data */
	prop->simple_clk_stop_capable = true;
	prop->paging_support = true;
	prop->clock_reg_supported = true;
	prop->lane_control_support = true;

	/* Source ports: DP2 (headset mic), DP5 (optimisation TX). */
	prop->source_ports = BIT(2) | BIT(5);
	/* Sink ports: DP6 (HPH audio), DP7 (HPH envelope), DP8 (optimisation RX). */
	prop->sink_ports = BIT(6) | BIT(7) | BIT(8);

	src = devm_kcalloc(dev, 2, sizeof(*src), GFP_KERNEL);
	if (!src)
		return -ENOMEM;

	src[0].num = 2;
	src[0].type = SDW_DPN_SIMPLE;
	src[0].simple_ch_prep_sm = true;
	src[0].ch_prep_timeout = 10;
	src[0].max_ch = 1;
	src[0].min_ch = 1;

	src[1].num = 5;
	src[1].type = SDW_DPN_SIMPLE;
	src[1].simple_ch_prep_sm = true;
	src[1].ch_prep_timeout = 10;
	src[1].max_ch = 1;
	src[1].min_ch = 1;

	prop->src_dpn_prop = src;

	sink = devm_kcalloc(dev, 3, sizeof(*sink), GFP_KERNEL);
	if (!sink)
		return -ENOMEM;

	sink[0].num = 6;
	sink[0].type = SDW_DPN_SIMPLE;
	sink[0].simple_ch_prep_sm = true;
	sink[0].ch_prep_timeout = 10;
	sink[0].max_ch = 2;
	sink[0].min_ch = 1;

	sink[1].num = 7;
	sink[1].type = SDW_DPN_FULL;
	sink[1].simple_ch_prep_sm = true;
	sink[1].ch_prep_timeout = 10;
	sink[1].max_ch = 1;
	sink[1].min_ch = 1;

	sink[2].num = 8;
	sink[2].type = SDW_DPN_REDUCED;
	sink[2].simple_ch_prep_sm = true;
	sink[2].ch_prep_timeout = 10;
	sink[2].max_ch = 2;
	sink[2].min_ch = 1;

	prop->sink_dpn_prop = sink;

	if (device_property_read_u32_array(dev, "qcom,port-mapping",
					   &slave->m_port_map[1],
					   WCD9378_SDCA_MAX_PORTS))
		dev_dbg(dev, "qcom,port-mapping not found, using default\n");

	return 0;
}

static int wcd9378_sdca_hw_init(struct sdw_slave *slave)
{
	struct device *dev = &slave->dev;
	struct gpio_desc *reset;
	int ret;

	ret = devm_regulator_bulk_get_enable(dev,
					     ARRAY_SIZE(wcd9378_sdca_supplies),
					     wcd9378_sdca_supplies);
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable supplies\n");

	/* Reset GPIO is active-low; pulse it to bring the codec out of reset */
	reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(reset))
		return dev_err_probe(dev, PTR_ERR(reset),
				     "failed to get reset GPIO\n");

	if (reset) {
		gpiod_set_value(reset, 1);
		/* 20us assert per WCD9378 reset sequence */
		usleep_range(20, 30);
		gpiod_set_value(reset, 0);
		/* 20us settle after deassert */
		usleep_range(20, 30);
	}

	return 0;
}

static struct sdca_function_data *wcd9378_sdca_get_function_data(void)
{
	return &wcd9378_sdca_data;
}

static int wcd9378_sdca_pde_pre_pmu(struct sdw_slave *slave,
				      struct regmap *regmap,
				      unsigned int function_id,
				      unsigned int entity_id)
{
	struct sdca_class_drv *core = dev_get_drvdata(&slave->dev);
	int ret;

	if (!core || !core->dev_regmap)
		return 0;

	if (entity_id == WCD9378_PDE34_ENTITY_ID) {
		unsigned int mic_bias = SDW_SDCA_CTL(function_id,
						     WCD9378_IT33_ENTITY_ID,
						     WCD9378_IT33_MICBIAS_SEL, 0);

		regcache_drop_region(regmap, mic_bias, mic_bias);
		ret = regmap_write(regmap, mic_bias, WCD9378_IT33_MICBIAS_2V75);
		if (ret)
			dev_err(&slave->dev, "IT33 MIC_BIAS: %d\n", ret);
	} else if (entity_id == WCD9378_PDE47_ENTITY_ID) {
		/* Announce commit-group count for the FU42 writes in pde_post_pmu. */
		ret = regmap_write(core->dev_regmap,
				   WCD9378_ENT0_COMMIT_GROUP_COUNT_ADDR,
				   WCD9378_ENT0_COMMIT_GROUP_COUNT_TWO);
		if (ret)
			dev_err(&slave->dev, "Commit_Group_Count: %d\n", ret);

		/* Let the master's WR FIFO drain before PDE47_REQ_PS=0. */
		usleep_range(2000, 2500);
	}

	return 0;
}

static int wcd9378_sdca_pde_post_pmu(struct sdw_slave *slave,
				       struct regmap *regmap,
				       unsigned int function_id,
				       unsigned int entity_id)
{
	struct sdca_class_drv *core = dev_get_drvdata(&slave->dev);
	unsigned int aps_reg;
	int ret;

	if (entity_id != WCD9378_PDE47_ENTITY_ID)
		return 0;

	if (!core || !core->dev_regmap)
		return 0;

	/*
	 * WCD9378 does not self-update ACTUAL_PS when the framework writes
	 * REQUESTED_PS=PS0 — the SEQR sequencer runs the analog HPH bring-up
	 * but leaves the ACTUAL_PS status register unchanged.  Wait for the
	 * sequencer to complete, then write PS0 to ACTUAL_PS directly and
	 * invalidate the cache so the framework's poll
	 * (sdca_asoc_pde_poll_actual_ps()) observes the target state.
	 */
	msleep(30);

	aps_reg = SDW_SDCA_CTL(function_id, WCD9378_PDE47_ENTITY_ID,
			       SDCA_CTL_PDE_ACTUAL_PS, 0);
	ret = sdw_write_no_pm(slave, aps_reg, SDCA_PDE_PS0);
	if (ret)
		dev_err(&slave->dev, "PDE47 ACTUAL_PS: %d\n", ret);
	else
		regcache_drop_region(regmap, aps_reg, aps_reg);

	/*
	 * Unmute FU42 CH1/CH2 and stage 0 dB volume via the _CN (NEXT) alias
	 * so all four staged writes activate together on the SCP_COMMIT
	 * below.  Writes go to the vendor slave regmap because FU42
	 * addresses are outside the SDCA class regmap's routing.
	 */
	ret = regmap_write(core->dev_regmap, WCD9378_FU42_MUTE_CH1_ADDR,
			   WCD9378_FU42_MUTE_UNMUTE);
	if (ret)
		dev_err(&slave->dev, "FU42 MUTE_CH1: %d\n", ret);

	ret = regmap_write(core->dev_regmap, WCD9378_FU42_MUTE_CH2_ADDR,
			   WCD9378_FU42_MUTE_UNMUTE);
	if (ret)
		dev_err(&slave->dev, "FU42 MUTE_CH2: %d\n", ret);

	ret = regmap_write(core->dev_regmap, WCD9378_FU42_CH_VOL_CH1_MSB_ADDR,
			   WCD9378_FU42_CH_VOL_UNITY_BYTE);
	if (ret)
		dev_err(&slave->dev, "FU42 CH1_MSB: %d\n", ret);
	ret = regmap_write(core->dev_regmap, WCD9378_FU42_CH_VOL_CH1_LSB_ADDR,
			   WCD9378_FU42_CH_VOL_UNITY_BYTE);
	if (ret)
		dev_err(&slave->dev, "FU42 CH1_LSB: %d\n", ret);
	ret = regmap_write(core->dev_regmap, WCD9378_FU42_CH_VOL_CH2_MSB_ADDR,
			   WCD9378_FU42_CH_VOL_UNITY_BYTE);
	if (ret)
		dev_err(&slave->dev, "FU42 CH2_MSB: %d\n", ret);
	ret = regmap_write(core->dev_regmap, WCD9378_FU42_CH_VOL_CH2_LSB_ADDR,
			   WCD9378_FU42_CH_VOL_UNITY_BYTE);
	if (ret)
		dev_err(&slave->dev, "FU42 CH2_LSB: %d\n", ret);

	ret = sdw_write_no_pm(slave, SDW_SCP_COMMIT, 0x02);
	if (ret)
		dev_err(&slave->dev, "FU42 SCP_COMMIT: %d\n", ret);

	return 0;
}

static int wcd9378_sdca_prepare(struct sdw_slave *slave,
				   struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	int ret;

	/*
	 * Capture-only bank-1 SCP tweak that clocks the TX PDM path.
	 * Playback does not need it (and would trigger a FIFO underflow
	 * if SCP_COMMIT were issued on the playback path).
	 */
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		ret = sdw_write_no_pm(slave, WCD9378_SCP_HOST_CLK_DIV2_CTL_B1, 0x01);
		if (ret)
			dev_err(&slave->dev, "HOST_CLK_DIV2_CTL: %d\n", ret);

		ret = sdw_write_no_pm(slave, SDW_SCP_COMMIT, 0x02);
		if (ret)
			dev_err(&slave->dev, "SCP_COMMIT: %d\n", ret);
	}

	return 0;
}

/*
 * HPH playback needs a second sink port DP7 (Class-H envelope) attached
 * to the same SoundWire stream as DP6.  The SDCA framework binds only
 * IT41 per PCM stream, so DP7 is exported here as an aux port.
 */
static int wcd9378_sdca_get_aux_port_configs(struct sdw_slave *slave,
					       struct snd_soc_dai *dai,
					       struct sdw_port_config *configs,
					       int max)
{
	/* Only the HPH playback DAI (IT41, entity index 0) needs DP7. */
	if (dai->id != 0 || max < 1)
		return 0;

	configs[0].num = 7;
	configs[0].ch_mask = 0x01;
	return 1;
}

static int wcd9378_sdca_set_jack(struct sdw_slave *slave,
				   struct regmap *dev_regmap)
{
	int ret;

	/*
	 * Device may be in cache_only when set_jack is invoked, so
	 * runtime-resume around the write.
	 */
	pm_runtime_get_sync(&slave->dev);
	ret = regmap_write(dev_regmap, WCD9378_ANA_MBHC_MECH_ADDR,
			   WCD9378_ANA_MBHC_MECH_ENABLE);
	pm_runtime_put_autosuspend(&slave->dev);

	if (ret)
		dev_warn(&slave->dev, "ANA_MBHC_MECH: %d\n", ret);
	return 0;
}

static const struct sdca_class_hw_ops wcd9378_sdca_hw_ops = {
	.hw_init              = wcd9378_sdca_hw_init,
	.get_function_data    = wcd9378_sdca_get_function_data,
	.prepare              = wcd9378_sdca_prepare,
	.get_aux_port_configs = wcd9378_sdca_get_aux_port_configs,
	.pde_pre_pmu          = wcd9378_sdca_pde_pre_pmu,
	.pde_post_pmu         = wcd9378_sdca_pde_post_pmu,
	.set_jack             = wcd9378_sdca_set_jack,
};

static int wcd9378_sdca_sdw_probe(struct sdw_slave *slave,
				    const struct sdw_device_id *id)
{
	return sdca_class_probe(slave, &wcd9378_sdca_hw_ops);
}

static const struct sdw_slave_ops wcd9378_sdca_sdw_ops = {
	.read_prop	= wcd9378_sdca_sdw_read_prop,
};

static const struct sdw_device_id wcd9378_sdca_sdw_id[] = {
	SDW_SLAVE_ENTRY(0x0217, 0x0110, 0),
	{}
};
MODULE_DEVICE_TABLE(sdw, wcd9378_sdca_sdw_id);

static struct sdw_driver wcd9378_sdca_sdw_driver = {
	.driver = {
		.name	= "wcd9378-sdca",
		.pm	= pm_ptr(&sdca_class_pm_ops),
	},
	.probe		= wcd9378_sdca_sdw_probe,
	.id_table	= wcd9378_sdca_sdw_id,
	.ops		= &wcd9378_sdca_sdw_ops,
};
module_sdw_driver(wcd9378_sdca_sdw_driver);

MODULE_DESCRIPTION("Qualcomm SimpleJack (Tambora/WCD9378) SDCA codec");
MODULE_AUTHOR("Qualcomm Technologies, Inc.");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS("SND_SOC_SDCA_CLASS");
