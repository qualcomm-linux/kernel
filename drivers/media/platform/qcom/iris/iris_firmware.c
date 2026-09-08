// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/iommu.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/soc/qcom/mdt_loader.h>

#include "iris_core.h"
#include "iris_firmware.h"

#define IRIS_PAS_ID				9

#define MAX_FIRMWARE_NAME_SIZE	128

/* Detect Gen2 firmware by scanning the blob for:
 *   QC_IMAGE_VERSION_STRING=<version>
 * and then checking:
 *   - version starts with "vfw", OR
 *   - version matches "video-firmware.N.M" with N >= 2
 */

static bool iris_detect_gen2_from_fwdata(const u8 *data, size_t size)
{
	const char *marker = "QC_IMAGE_VERSION_STRING=";
	const size_t mlen = strlen(marker);
	int major = 0, minor = 0;
	char version_buf[64];
	size_t max;

	max = (size > mlen) ? size - mlen : 0;
	for (size_t i = 0; i < max; i++) {
		if (!memcmp(data + i, marker, mlen)) {
			const char *found = (const char *)(data + i + mlen);

			strscpy(version_buf, found, sizeof(version_buf));
			if (!strncmp(version_buf, "vfw", 3))
				return true;
			if (sscanf(version_buf, "video-firmware.%d.%d", &major, &minor) == 2 &&
			    major >= 2)
				return true;
			break;
		}
	}

	return false;
}

static const struct firmware *iris_detect_firmware(struct iris_core *core,
						   const char **fw_name)
{
	const struct firmware *firmware;
	bool has_both_gens;
	int ret;

	*fw_name = NULL;
	if (core->iris_platform_data->firmware_desc_gen2)
		core->iris_firmware_desc = core->iris_platform_data->firmware_desc_gen2;
	else if (core->iris_platform_data->firmware_desc_gen1)
		core->iris_firmware_desc = core->iris_platform_data->firmware_desc_gen1;
	else
		return ERR_PTR(-EINVAL);

	has_both_gens = core->iris_platform_data->firmware_desc_gen2 &&
		core->iris_platform_data->firmware_desc_gen1;

	ret = of_property_read_string_index(dev_of_node(core->dev), "firmware-name", 0, fw_name);
	if (ret) {
		*fw_name = core->iris_firmware_desc->fwname;
		ret = request_firmware(&firmware, *fw_name, core->dev);
		if (ret && has_both_gens) {
			core->iris_firmware_desc = core->iris_platform_data->firmware_desc_gen1;
			*fw_name = core->iris_firmware_desc->fwname;
			ret = request_firmware(&firmware, *fw_name, core->dev);
		}

		return ret ? ERR_PTR(ret) : firmware;
	}

	ret = request_firmware(&firmware, *fw_name, core->dev);
	if (ret)
		return ERR_PTR(ret);

	if (has_both_gens &&
	    !iris_detect_gen2_from_fwdata((const u8 *)firmware->data, firmware->size)) {
		dev_info(core->dev, "Gen1 FW detected in %s\n", *fw_name);
		core->iris_firmware_desc = core->iris_platform_data->firmware_desc_gen1;
	}

	return firmware;
}

static int iris_load_fw_to_memory(struct iris_core *core)
{
	struct qcom_scm_pas_context *ctx;
	const struct firmware *firmware = NULL;
	struct device *dev = core->dev;
	struct resource res;
	phys_addr_t mem_phys;
	const char *fw_name;
	size_t res_size;
	ssize_t fw_size;
	int ret;

	ret = of_reserved_mem_region_to_resource(dev->of_node, 0, &res);
	if (ret)
		return ret;

	mem_phys = res.start;
	res_size = resource_size(&res);

	dev = core->fw.dev ? : core->dev;

	ctx = devm_qcom_scm_pas_context_alloc(dev, IRIS_PAS_ID, mem_phys, res_size);
	if (!ctx)
		return -ENOMEM;

	ctx->use_tzmem = core->fw.dev;

	firmware = iris_detect_firmware(core, &fw_name);
	if (IS_ERR(firmware))
		return PTR_ERR(firmware);

	core->iris_firmware_data = core->iris_firmware_desc->firmware_data;

	fw_size = qcom_mdt_get_size(firmware);
	if (fw_size < 0 || res_size < (size_t)fw_size) {
		ret = -EINVAL;
		goto err_release_fw;
	}

	ret = qcom_mdt_pas_load(ctx, firmware, fw_name, NULL);
	qcom_scm_pas_metadata_release(ctx);
	if (ret)
		goto err_release_fw;

	if (core->fw.iommu_domain) {
		ret = iommu_map(core->fw.iommu_domain, 0, mem_phys, res_size,
				IOMMU_READ | IOMMU_WRITE | IOMMU_PRIV, GFP_KERNEL);
		if (ret)
			goto err_release_fw;
	}

	ret = qcom_scm_pas_prepare_and_auth_reset(ctx);
	if (ret)
		goto err_iommu_unmap;

	core->fw.ctx = ctx;

	return ret;

err_iommu_unmap:
	iommu_unmap(core->fw.iommu_domain, 0, res_size);
err_release_fw:
	release_firmware(firmware);

	return ret;
}

int iris_fw_load(struct iris_core *core)
{
	const struct tz_cp_config *cp_config;
	int i, ret;

	ret = iris_load_fw_to_memory(core);
	if (ret) {
		dev_err(core->dev, "firmware download failed %d\n", ret);
		return ret;
	}

	for (i = 0; i < core->iris_platform_data->tz_cp_config_data_size; i++) {
		cp_config = &core->iris_platform_data->tz_cp_config_data[i];
		ret = qcom_scm_mem_protect_video_var(cp_config->cp_start,
						     cp_config->cp_size,
						     cp_config->cp_nonpixel_start,
						     cp_config->cp_nonpixel_size);
		if (ret) {
			dev_err(core->dev, "qcom_scm_mem_protect_video_var failed: %d\n", ret);
			iris_fw_unload(core);
			return ret;
		}
	}

	return 0;
}

int iris_fw_unload(struct iris_core *core)
{
	struct qcom_scm_pas_context *ctx = core->fw.ctx;
	int ret;

	if (!ctx)
		return -EINVAL;

	ret = qcom_scm_pas_shutdown(ctx->pas_id);
	if (core->fw.iommu_domain)
		iommu_unmap(core->fw.iommu_domain, 0, ctx->mem_size);

	core->fw.ctx = NULL;
	return ret;
}

int iris_set_hw_state(struct iris_core *core, bool resume)
{
	return qcom_scm_set_remote_state(resume, 0);
}

int iris_fw_init(struct iris_core *core)
{
	struct platform_device_info info;
	struct iommu_domain *iommu_dom;
	struct platform_device *pdev;
	struct device_node *np;
	int ret;

	np = of_get_child_by_name(core->dev->of_node, "video-firmware");
	if (!np)
		return 0;

	memset(&info, 0, sizeof(info));
	info.fwnode = &np->fwnode;
	info.parent = core->dev;
	info.name = np->name;
	info.dma_mask = DMA_BIT_MASK(32);

	pdev = platform_device_register_full(&info);
	if (IS_ERR(pdev)) {
		of_node_put(np);
		return PTR_ERR(pdev);
	}

	pdev->dev.of_node = np;

	ret = of_dma_configure(&pdev->dev, np, true);
	if (ret)
		goto err_unregister;

	core->fw.dev = &pdev->dev;

	iommu_dom = iommu_get_domain_for_dev(core->fw.dev);
	if (!iommu_dom) {
		ret = -EINVAL;
		goto err_unset_fw_dev;
	}

	ret = iommu_attach_device(iommu_dom, core->fw.dev);
	if (ret)
		goto err_unset_fw_dev;

	core->fw.iommu_domain = iommu_dom;

	of_node_put(np);

	return 0;

err_unset_fw_dev:
	core->fw.dev = NULL;
err_unregister:
	platform_device_unregister(pdev);
	of_node_put(np);
	return ret;
}

void iris_fw_deinit(struct iris_core *core)
{
	if (!core->fw.dev)
		return;

	if (core->fw.iommu_domain) {
		iommu_detach_device(core->fw.iommu_domain, core->fw.dev);
		core->fw.iommu_domain = NULL;
	}

	platform_device_unregister(to_platform_device(core->fw.dev));
	core->fw.dev = NULL;
}
