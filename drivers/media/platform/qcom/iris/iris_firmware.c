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

#define MAX_FIRMWARE_NAME_SIZE	128

static int iris_load_fw_to_memory(struct iris_core *core, const char *fw_name)
{
	u32 pas_id = core->iris_platform_data->pas_id;
	struct qcom_scm_pas_context *ctx;
	const struct firmware *firmware = NULL;
	struct device *dev = core->dev;
	struct reserved_mem *rmem;
	struct device_node *node;
	phys_addr_t mem_phys;
	size_t res_size;
	ssize_t fw_size;
	void *mem_virt;
	int ret;

	if (strlen(fw_name) >= MAX_FIRMWARE_NAME_SIZE - 4)
		return -EINVAL;

	node = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!node)
		return -EINVAL;

	rmem = of_reserved_mem_lookup(node);
	of_node_put(node);
	if (!rmem)
		return -EINVAL;

	mem_phys = rmem->base;
	res_size = rmem->size;

	dev = core->fw.dev ? : core->dev;

	ctx = devm_qcom_scm_pas_context_alloc(dev, pas_id, mem_phys, res_size);
	if (!ctx)
		return -ENOMEM;

	ctx->use_tzmem = core->fw.dev;

	ret = request_firmware(&firmware, fw_name, dev);
	if (ret)
		return ret;

	fw_size = qcom_mdt_get_size(firmware);
	if (fw_size < 0 || res_size < (size_t)fw_size) {
		ret = -EINVAL;
		goto err_release_fw;
	}

	mem_virt = memremap(mem_phys, res_size, MEMREMAP_WC);
	if (!mem_virt) {
		ret = -ENOMEM;
		goto err_release_fw;
	}

	ret = qcom_mdt_pas_load(ctx, firmware, fw_name, mem_virt, NULL);
	qcom_scm_pas_metadata_release(ctx);
	if (ret)
		goto err_mem_unmap;

	if (core->fw.iommu_domain) {
		ret = iommu_map(core->fw.iommu_domain, 0, mem_phys, res_size,
				IOMMU_READ | IOMMU_WRITE | IOMMU_PRIV, GFP_KERNEL);
		if (ret)
			goto err_mem_unmap;
	}

	ret = qcom_scm_pas_prepare_and_auth_reset(ctx);
	if (ret)
		goto err_iommu_unmap;

	core->fw.ctx = ctx;

	return ret;

err_iommu_unmap:
	iommu_unmap(core->fw.iommu_domain, 0, res_size);
err_mem_unmap:
	memunmap(mem_virt);
err_release_fw:
	release_firmware(firmware);

	return ret;
}

int iris_fw_load(struct iris_core *core)
{
	struct tz_cp_config *cp_config = core->iris_platform_data->tz_cp_config_data;
	const char *fwpath = NULL;
	int ret;

	ret = of_property_read_string_index(core->dev->of_node, "firmware-name", 0,
					    &fwpath);
	if (ret)
		fwpath = core->iris_platform_data->fwname;

	ret = iris_load_fw_to_memory(core, fwpath);
	if (ret) {
		dev_err(core->dev, "firmware download failed\n");
		return -ENOMEM;
	}

	ret = qcom_scm_mem_protect_video_var(cp_config->cp_start,
					     cp_config->cp_size,
					     cp_config->cp_nonpixel_start,
					     cp_config->cp_nonpixel_size);
	if (ret) {
		dev_err(core->dev, "protect memory failed\n");
		iris_fw_unload(core);
		return ret;
	}

	return ret;
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
