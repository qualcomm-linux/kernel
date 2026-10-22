// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * HFI commands for device initialization, capability parsing, and resource setup.
 */

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "dcp_hfi.h"
#include "dcp_hfi_priv.h"
#include "hfi/hfi_commands_device.h"
#include "hfi/hfi_defs_common.h"
#include "hfi/hfi_pack_unpack_common.h"
#include "hfi/hfi_properties_device.h"
#include "hfi/hfi_properties_display.h"
#include "hfi/hfi_utils.h"

static void dcp_hfi_caps_cache_init(struct dcp_hfi_caps_cache *cache)
{
	init_completion(&cache->device_done);
	init_completion(&cache->display_done);
}

static void dcp_hfi_caps_cache_start(struct dcp_hfi *hfi)
{
	memset(&hfi->caps_cache, 0, sizeof(hfi->caps_cache));
	dcp_hfi_caps_cache_init(&hfi->caps_cache);
}

static void dcp_hfi_caps_cache_stop(struct dcp_hfi *hfi)
{
	memset(&hfi->caps_cache, 0, sizeof(hfi->caps_cache));
}

int dcp_hfi_send_device_init(struct dcp_hfi *hfi)
{
	int ret;

	mutex_lock(&hfi->caps_cache_lock);
	dcp_hfi_caps_cache_start(hfi);

	/* Payload and flags follow the HFI device-command definition. */
	dev_dbg(hfi->dev,
		"HFI DEVICE_INIT cmd=0x%x flags=0x%x timeout=%ums\n",
		HFI_COMMAND_DEVICE_INIT,
		HFI_TX_FLAGS_RESPONSE_REQUIRED | HFI_TX_FLAGS_NON_DISCARDABLE,
		DCP_HFI_TIMEOUT_INIT_MS);

	ret = dcp_hfi_send_sync(hfi,
				HFI_COMMAND_DEVICE_INIT,
				HFI_CMD_BUFF_DEVICE, 0,
				HFI_PAYLOAD_NONE, NULL, 0,
				HFI_TX_FLAGS_RESPONSE_REQUIRED |
				HFI_TX_FLAGS_NON_DISCARDABLE,
				NULL, 0,
				DCP_HFI_TIMEOUT_INIT_MS);
	if (ret) {
		dev_err(hfi->dev, "HFI DEVICE_INIT failed: %d\n", ret);
		dcp_hfi_caps_cache_stop(hfi);
	} else {
		dev_dbg(hfi->dev, "HFI DEVICE_INIT succeeded\n");
	}

	mutex_unlock(&hfi->caps_cache_lock);
	return ret;
}

static void dcp_hfi_parse_init_caps_kv(struct dcp_caps *caps,
				       u32 prop_key,
				       const u32 *payload,
				       u32 max_words)
{
	u32 prop_id = HFI_PROP_ID(prop_key);
	u32 count, i;

	if (!payload || !max_words)
		return;

	switch (prop_id) {
	case HFI_PROPERTY_DEVICE_INIT_DCP_HW_VERSION:
		caps->dcp_hw_rev = payload[0];
		break;
	case HFI_PROPERTY_DEVICE_INIT_MDSS_HW_VERSION:
		caps->hw_rev = payload[0];
		break;
	case HFI_PROPERTY_DEVICE_INIT_DCP_FW_VERSION:
		caps->fw_rev = payload[0];
		break;
	case HFI_PROPERTY_DEVICE_INIT_VIG_INDICES:
		if (max_words < 1)
			break;
		count = payload[0];
		if (count > max_words - 1)
			count = max_words - 1;
		caps->num_vig_pipes = 0;
		caps->virt_vig_count = 0;
		for (i = 0; i < count; i++) {
			u32 idx = payload[1 + i];

			if (DCP_REC_ID(idx) == 0) {
				if (caps->num_vig_pipes < DCP_MAX_SSPP_COUNT)
					caps->vig_pipe_ids[caps->num_vig_pipes++] = idx;
			} else {
				if (caps->virt_vig_count < DCP_MAX_SSPP_COUNT)
					caps->vig_r1_indices[caps->virt_vig_count++] = idx;
			}
		}
		break;
	case HFI_PROPERTY_DEVICE_INIT_DMA_INDICES:
		if (max_words < 1)
			break;
		count = payload[0];
		if (count > max_words - 1)
			count = max_words - 1;
		caps->num_dma_pipes = 0;
		caps->virt_dma_count = 0;
		for (i = 0; i < count; i++) {
			u32 idx = payload[1 + i];

			if (DCP_REC_ID(idx) == 0) {
				if (caps->num_dma_pipes < DCP_MAX_SSPP_COUNT)
					caps->dma_pipe_ids[caps->num_dma_pipes++] = idx;
			} else {
				if (caps->virt_dma_count < DCP_MAX_SSPP_COUNT)
					caps->dma_r1_indices[caps->virt_dma_count++] = idx;
			}
		}
		break;
	case HFI_PROPERTY_DEVICE_INIT_MAX_DISPLAY_COUNT:
		caps->num_displays = payload[0];
		break;
	case HFI_PROPERTY_DEVICE_INIT_DSI_INDICES:
		if (max_words < 1)
			break;
		count = min_t(u32, payload[0], DCP_MAX_BLOCKS);
		if (count > max_words - 1)
			count = max_words - 1;
		caps->dsi_count = count;
		for (i = 0; i < count; i++)
			caps->dsi_indices[i] = payload[1 + i];
		break;
	case HFI_PROPERTY_DEVICE_INIT_MAX_DSI_RESOLUTION:
		{
			u32 w = (payload[0] >> 16) & 0xFFFF;
			u32 h = payload[0] & 0xFFFF;

			if (w > caps->max_width)
				caps->max_width  = w;
			if (h > caps->max_height)
				caps->max_height = h;
		}
		break;
	default:
		break;
	}
}

static void dcp_hfi_log_pipe_ids(struct device *dev, const char *label,
				 const u32 *ids, u32 count)
{
	char buf[128];
	int pos = 0;
	u32 i;

	for (i = 0; i < count && pos < (int)sizeof(buf) - 8; i++)
		pos += scnprintf(buf + pos, sizeof(buf) - pos,
				  "%s0x%x", i ? "," : "", ids[i]);

	dev_dbg(dev, "%s: count=%u ids=[%s]\n", label, count, buf);
}

static int dcp_hfi_parse_kv_buf(struct dcp_hfi *hfi, struct dcp_caps *caps,
				const u8 *buf, u32 buf_len)
{
	struct hfi_util_kv_parser kv_parser;
	u32 prop_key, *prop_payload, max_words, last_read;
	int ret;

	ret = hfi_util_kv_parser_init(&kv_parser, buf_len, (u32 *)buf);
	if (ret) {
		dev_err(hfi->dev, "KV parser init failed: %d\n", ret);
		return ret;
	}

	last_read = 0;
	ret = hfi_util_kv_parser_get_next(&kv_parser, last_read,
					  &prop_key, &prop_payload, &max_words);
	while (!ret && prop_key) {
		dcp_hfi_parse_init_caps_kv(caps, prop_key,
					   prop_payload, max_words);
		last_read = HFI_PROP_SZ(prop_key) ?: max_words;
		ret = hfi_util_kv_parser_get_next(&kv_parser, last_read,
						  &prop_key, &prop_payload,
						  &max_words);
	}
	return ret;
}

int dcp_hfi_get_capabilities(struct dcp_hfi *hfi, struct dcp_caps *caps)
{
	struct dcp_hfi_caps_cache *cache;
	int ret = 0, opt_ret;

	memset(caps, 0, sizeof(*caps));

	mutex_lock(&hfi->caps_cache_lock);
	cache = &hfi->caps_cache;

	if (!wait_for_completion_timeout(&cache->device_done,
					 msecs_to_jiffies(DCP_HFI_TIMEOUT_CAPS_MS))) {
		dev_err(hfi->dev,
			"DEVICE_INIT_DEVICE_CAPS timeout after %ums\n",
			DCP_HFI_TIMEOUT_CAPS_MS);
		ret = -ETIMEDOUT;
		goto out_free_cache;
	}

	if (!cache->device_len) {
		dev_err(hfi->dev, "DEVICE_INIT_DEVICE_CAPS: empty payload\n");
		ret = -ENODATA;
		goto out_free_cache;
	}

	ret = dcp_hfi_parse_kv_buf(hfi, caps, cache->device_buf,
				   cache->device_len);
	if (ret)
		goto out_free_cache;

	if (!wait_for_completion_timeout(&cache->display_done,
					 msecs_to_jiffies(DCP_HFI_TIMEOUT_CAPS_MS))) {
		dev_warn(hfi->dev,
			 "DEVICE_INIT_DISPLAY_CAPS not received\n");
	} else if (cache->display_len > 0) {
		opt_ret = dcp_hfi_parse_kv_buf(hfi, caps, cache->display_buf,
					       cache->display_len);
		if (opt_ret) {
			dev_warn(hfi->dev,
				 "DISPLAY_CAPS parse failed: %d (non-fatal)\n",
				 opt_ret);
		}
	}
	if (cache->vig_len > 0) {
		opt_ret = dcp_hfi_parse_kv_buf(hfi, caps, cache->vig_buf,
					       cache->vig_len);
		if (opt_ret)
			dev_warn(hfi->dev,
				 "VIG_CAPS parse failed: %d (non-fatal)\n",
				 opt_ret);
		else
			dcp_hfi_log_pipe_ids(hfi->dev, "VIG_CAPS",
					     caps->vig_pipe_ids,
					     caps->num_vig_pipes);
	}

	if (cache->dma_len > 0) {
		opt_ret = dcp_hfi_parse_kv_buf(hfi, caps, cache->dma_buf,
					       cache->dma_len);
		if (opt_ret)
			dev_warn(hfi->dev,
				 "DMA_CAPS parse failed: %d (non-fatal)\n",
				 opt_ret);
		else
			dcp_hfi_log_pipe_ids(hfi->dev, "DMA_CAPS",
					     caps->dma_pipe_ids,
					     caps->num_dma_pipes);
	}

	if (cache->vig_r1_len > 0) {
		opt_ret = dcp_hfi_parse_kv_buf(hfi, caps, cache->vig_r1_buf,
					       cache->vig_r1_len);
		if (opt_ret)
			dev_warn(hfi->dev,
				 "VIG_R1_CAPS parse failed: %d (non-fatal)\n",
				 opt_ret);
		else
			dcp_hfi_log_pipe_ids(hfi->dev, "VIG_R1_CAPS",
					     caps->vig_r1_indices,
					     caps->virt_vig_count);
	}

	if (cache->dma_r1_len > 0) {
		opt_ret = dcp_hfi_parse_kv_buf(hfi, caps, cache->dma_r1_buf,
					       cache->dma_r1_len);
		if (opt_ret)
			dev_warn(hfi->dev,
				 "DMA_R1_CAPS parse failed: %d (non-fatal)\n",
				 opt_ret);
		else
			dcp_hfi_log_pipe_ids(hfi->dev, "DMA_R1_CAPS",
					     caps->dma_r1_indices,
					     caps->virt_dma_count);
	}
	caps->max_planes = caps->num_vig_pipes + caps->num_dma_pipes;
	if (!caps->num_displays) {
		dev_err(hfi->dev, "invalid DCP caps: zero display count\n");
		ret = -EINVAL;
		goto out_free_cache;
	}

	if (!caps->max_width || !caps->max_height) {
		dev_err(hfi->dev, "invalid DCP caps: max resolution %ux%u\n",
			caps->max_width, caps->max_height);
		ret = -EINVAL;
		goto out_free_cache;
	}

	if (!caps->max_planes) {
		dev_err(hfi->dev, "invalid DCP caps: no scanout pipes\n");
		ret = -EINVAL;
		goto out_free_cache;
	}

	dev_dbg(hfi->dev,
		"DCP caps: fw=0x%x hw=0x%x dcp=0x%x disp=%u max=%ux%u vig=%u dma=%u\n",
		caps->fw_rev, caps->hw_rev, caps->dcp_hw_rev,
		caps->num_displays, caps->max_width, caps->max_height,
		caps->num_vig_pipes, caps->num_dma_pipes);

out_free_cache:
	dcp_hfi_caps_cache_stop(hfi);
	mutex_unlock(&hfi->caps_cache_lock);
	return ret;
}

int dcp_hfi_send_resource_register(struct dcp_hfi *hfi)
{
	static const u32 resource_ids[] = {
		HFI_PROPERTY_DISPLAY_DRAM_IB,
		HFI_PROPERTY_DISPLAY_DRAM_AB,
		HFI_PROPERTY_DISPLAY_LLCC_IB,
		HFI_PROPERTY_DISPLAY_LLCC_AB,
		HFI_PROPERTY_DISPLAY_CORE_IB,
		HFI_PROPERTY_DISPLAY_CORE_AB,
		HFI_PROPERTY_DISPLAY_CORE_CLK,
	};
	u32 payload[1 + ARRAY_SIZE(resource_ids)];
	u32 i;
	int ret;

	payload[0] = ARRAY_SIZE(resource_ids);
	for (i = 0; i < ARRAY_SIZE(resource_ids); i++)
		payload[1 + i] = resource_ids[i];

	dev_dbg(hfi->dev, "sending HFI_COMMAND_DEVICE_RESOURCE_REGISTER (%zu resources)\n",
		ARRAY_SIZE(resource_ids));

	ret = dcp_hfi_send_sync(hfi,
				HFI_COMMAND_DEVICE_RESOURCE_REGISTER,
				HFI_CMD_BUFF_DEVICE, 0,
				HFI_PAYLOAD_U32_ARRAY,
				payload, sizeof(payload),
				HFI_TX_FLAGS_RESPONSE_REQUIRED |
				HFI_TX_FLAGS_NON_DISCARDABLE,
				NULL, 0,
				DCP_HFI_TIMEOUT_RESOURCE_REGISTER_MS);
	if (ret)
		dev_err(hfi->dev,
			"HFI RESOURCE_REGISTER failed: %d\n", ret);
	else
		dev_dbg(hfi->dev, "HFI RESOURCE_REGISTER succeeded\n");

	return ret;
}
