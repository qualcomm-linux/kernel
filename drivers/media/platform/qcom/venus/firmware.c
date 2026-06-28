// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 Linaro Ltd.
 */

#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/iommu.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/sizes.h>
#include <linux/soc/qcom/mdt_loader.h>
#include <dt-bindings/media/qcom,qcs615-venus.h>

#include "core.h"
#include "firmware.h"
#include "hfi_venus_io.h"

#define VENUS_PAS_ID			9
#define VENUS_FW_MEM_SIZE		(6 * SZ_1M)
#define VENUS_FW_START_ADDR		0x0

static void venus_reset_cpu(struct venus_core *core)
{
	u32 fw_size = core->fw.mapped_mem_size;
	void __iomem *wrapper_base;

	if (IS_IRIS2(core) || IS_IRIS2_1(core))
		wrapper_base = core->wrapper_tz_base;
	else
		wrapper_base = core->wrapper_base;

	writel(0, wrapper_base + WRAPPER_FW_START_ADDR);
	writel(fw_size, wrapper_base + WRAPPER_FW_END_ADDR);
	writel(0, wrapper_base + WRAPPER_CPA_START_ADDR);
	writel(fw_size, wrapper_base + WRAPPER_CPA_END_ADDR);
	writel(fw_size, wrapper_base + WRAPPER_NONPIX_START_ADDR);
	writel(fw_size, wrapper_base + WRAPPER_NONPIX_END_ADDR);

	if (IS_IRIS2(core) || IS_IRIS2_1(core)) {
		/* Bring XTSS out of reset */
		writel(0, wrapper_base + WRAPPER_TZ_XTSS_SW_RESET);
	} else {
		writel(0x0, wrapper_base + WRAPPER_CPU_CGC_DIS);
		writel(0x0, wrapper_base + WRAPPER_CPU_CLOCK_CONFIG);

		/* Bring ARM9 out of reset */
		writel(0, wrapper_base + WRAPPER_A9SS_SW_RESET);
	}
}

int venus_set_hw_state(struct venus_core *core, bool resume)
{
	int ret;

	if (core->use_tz) {
		ret = qcom_scm_set_remote_state(resume, 0);
		if (resume && ret == -EINVAL)
			ret = 0;
		return ret;
	}

	if (resume) {
		venus_reset_cpu(core);
	} else {
		if (IS_IRIS2(core) || IS_IRIS2_1(core))
			writel(WRAPPER_XTSS_SW_RESET_BIT,
			       core->wrapper_tz_base + WRAPPER_TZ_XTSS_SW_RESET);
		else
			writel(WRAPPER_A9SS_SW_RESET_BIT,
			       core->wrapper_base + WRAPPER_A9SS_SW_RESET);
	}

	return 0;
}

static int venus_load_fw_prepare(struct venus_core *core, const char *fwname,
				 phys_addr_t *mem_phys, size_t *res_size,
				 const struct firmware **mdt)
{
	struct resource res;
	ssize_t fw_size;
	int ret;

	ret = of_reserved_mem_region_to_resource(core->dev->of_node, 0, &res);
	if (ret) {
		dev_err(core->dev, "failed to lookup reserved memory-region\n");
		return -EINVAL;
	}

	*mem_phys = res.start;
	*res_size = resource_size(&res);

	ret = request_firmware(mdt, fwname, core->dev);
	if (ret < 0) {
		dev_err(core->dev, "%s: request_firmware: %d\n", __func__, ret);
		return ret;
	}

	fw_size = qcom_mdt_get_size(*mdt);
	if (fw_size < 0) {
		ret = fw_size;
		goto err_release;
	}

	if (*res_size < fw_size || fw_size > VENUS_FW_MEM_SIZE) {
		ret = -EINVAL;
		goto err_release;
	}

	return 0;

err_release:
	release_firmware(*mdt);
	return ret;
}

static int venus_load_fw(struct venus_core *core,
			 const struct firmware *mdt, const char *fwname,
			 phys_addr_t mem_phys, size_t res_size)
{
	struct qcom_scm_pas_context *ctx;
	struct device *dev;
	int ret;

	dev = core->fw.dev ? core->fw.dev : core->dev;
	ctx = devm_qcom_scm_pas_context_alloc(dev, VENUS_PAS_ID, mem_phys, res_size);
	if (!ctx) {
		dev_err(core->dev, "%s: ctx is null\n", __func__);
		return -ENOMEM;
	}

	ctx->use_tzmem = !!core->fw.dev;

	ret = qcom_mdt_pas_load(ctx, mdt, fwname, NULL);
	qcom_scm_pas_metadata_release(ctx);
	if (ret) {
		dev_err(core->dev, "%s: qcom_mdt_pas_load: %d\n", __func__, ret);
		return ret;
	}

	if (core->fw.iommu_domain) {
		ret = iommu_map(core->fw.iommu_domain, 0, mem_phys, res_size,
				IOMMU_READ | IOMMU_WRITE | IOMMU_PRIV, GFP_KERNEL);
		if (ret) {
			dev_err(core->dev, "%s: iommu_map: %d\n", __func__, ret);
			return ret;
		}
	}

	core->fw.mapped_mem_size = res_size;

	ret = qcom_scm_pas_prepare_and_auth_reset(ctx);
	if (ret) {
		dev_err(core->dev, "%s: qcom_scm_pas_prepare_and_auth_reset: %d\n", __func__, ret);
		if (core->fw.iommu_domain)
			iommu_unmap(core->fw.iommu_domain, 0, res_size);
		core->fw.mapped_mem_size = 0;
		return ret;
	}

	core->fw.ctx = ctx;
	return 0;
}

static int venus_load_fw_no_tz(struct venus_core *core,
			       const struct firmware *mdt, const char *fwname,
			       phys_addr_t mem_phys, size_t res_size)
{
	void *mem_va;
	int ret;

	mem_va = memremap(mem_phys, res_size, MEMREMAP_WC);
	if (!mem_va) {
		dev_err(core->dev,
			"unable to map memory region %pa size %#zx\n", &mem_phys, res_size);
		return -ENOMEM;
	}

	ret = qcom_mdt_load_no_init(core->fw.dev, mdt, fwname, mem_va, mem_phys, res_size, NULL);
	memunmap(mem_va);
	if (ret) {
		dev_err(core->dev, "%s: qcom_mdt_load_no_init: %d\n", __func__, ret);
		return ret;
	}

	ret = iommu_map(core->fw.iommu_domain, VENUS_FW_START_ADDR, mem_phys,
			res_size, IOMMU_READ | IOMMU_WRITE | IOMMU_PRIV, GFP_KERNEL);
	if (ret) {
		dev_err(core->dev, "could not map video firmware region\n");
		return ret;
	}

	core->fw.mapped_mem_size = res_size;
	venus_reset_cpu(core);
	return 0;
}

static int venus_shutdown_no_tz(struct venus_core *core)
{
	const size_t mapped = core->fw.mapped_mem_size;
	struct iommu_domain *iommu;
	size_t unmapped;
	u32 reg;
	struct device *dev = core->fw.dev;
	void __iomem *wrapper_base = core->wrapper_base;
	void __iomem *wrapper_tz_base = core->wrapper_tz_base;

	if (IS_IRIS2(core) || IS_IRIS2_1(core)) {
		/* Assert the reset to XTSS */
		reg = readl(wrapper_tz_base + WRAPPER_TZ_XTSS_SW_RESET);
		reg |= WRAPPER_XTSS_SW_RESET_BIT;
		writel(reg, wrapper_tz_base + WRAPPER_TZ_XTSS_SW_RESET);
	} else {
		/* Assert the reset to ARM9 */
		reg = readl(wrapper_base + WRAPPER_A9SS_SW_RESET);
		reg |= WRAPPER_A9SS_SW_RESET_BIT;
		writel(reg, wrapper_base + WRAPPER_A9SS_SW_RESET);
	}

	iommu = core->fw.iommu_domain;

	if (core->fw.mapped_mem_size && iommu) {
		unmapped = iommu_unmap(iommu, VENUS_FW_START_ADDR, mapped);

		if (unmapped != mapped)
			dev_err(dev, "failed to unmap firmware\n");
		else
			core->fw.mapped_mem_size = 0;
	}

	return 0;
}

int venus_firmware_cfg(struct venus_core *core)
{
	void __iomem *cpu_cs_base = core->cpu_cs_base;

	if (IS_AR50_LITE(core))
		writel(CPU_CS_VCICMD_ARP_OFF, cpu_cs_base + CPU_CS_VCICMD);

	return 0;
}

int venus_boot(struct venus_core *core)
{
	struct device *dev = core->dev;
	const struct venus_resources *res = core->res;
	const struct firmware *mdt;
	const char *fwpath = NULL;
	phys_addr_t mem_phys;
	size_t res_size;
	int ret;

	if (!IS_ENABLED(CONFIG_QCOM_MDT_LOADER) ||
	    (!core->use_tz && !core->fw.dev))
		return driver_deferred_probe_check_state(core->dev);

	ret = of_property_read_string_index(dev->of_node, "firmware-name", 0, &fwpath);
	if (ret)
		fwpath = core->res->fwname;

	ret = venus_load_fw_prepare(core, fwpath, &mem_phys, &res_size, &mdt);
	if (ret)
		return ret;

	if (core->use_tz)
		ret = venus_load_fw(core, mdt, fwpath, mem_phys, res_size);
	else
		ret = venus_load_fw_no_tz(core, mdt, fwpath, mem_phys, res_size);

	release_firmware(mdt);

	if (ret) {
		dev_err(dev, "fail to load video firmware\n");
		return ret;
	}

	if (core->use_tz && res->cp_size) {
		/*
		 * Clues for porting using downstream data:
		 * cp_start = 0
		 * cp_size = venus_ns/virtual-addr-pool[0] - yes, address and not size!
		 *   This works, as the non-secure context bank is placed
		 *   contiguously right after the Content Protection region.
		 *
		 * cp_nonpixel_start = venus_sec_non_pixel/virtual-addr-pool[0]
		 * cp_nonpixel_size = venus_sec_non_pixel/virtual-addr-pool[1]
		 */
		ret = qcom_scm_mem_protect_video_var(res->cp_start,
						     res->cp_size,
						     res->cp_nonpixel_start,
						     res->cp_nonpixel_size);
		if (ret) {
			venus_shutdown(core);
			dev_err(dev, "set virtual address ranges fail (%d)\n",	ret);
			return ret;
		}
	}

	return ret;
}

int venus_shutdown(struct venus_core *core)
{
	int ret;

	if (core->use_tz) {
		ret = qcom_scm_pas_shutdown(VENUS_PAS_ID);
		if (core->fw.iommu_domain && core->fw.mapped_mem_size) {
			iommu_unmap(core->fw.iommu_domain, 0, core->fw.mapped_mem_size);
			core->fw.mapped_mem_size = 0;
		}
		core->fw.ctx = NULL;
	} else {
		ret = venus_shutdown_no_tz(core);
	}

	return ret;
}

int venus_firmware_check(struct venus_core *core)
{
	const struct firmware_version *req = core->res->min_fw;
	const struct firmware_version *run = &core->venus_ver;

	if (!req)
		return 0;

	if (!is_fw_rev_or_newer(core, req->major, req->minor, req->rev))
		goto error;

	return 0;
error:
	dev_err(core->dev, "Firmware v%d.%d.%d < v%d.%d.%d\n",
		run->major, run->minor, run->rev,
		req->major, req->minor, req->rev);

	return -EINVAL;
}

static struct device *venus_firmware_alloc_platform_dev(struct venus_core *core,
							const char *name, const u32 *f_id)
{
	struct platform_device *pdev;
	int ret;

	pdev = platform_device_alloc(name, 0);
	if (!pdev) {
		dev_err(core->dev, "%s: platform_device_alloc err\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	pdev->dev.parent = core->dev;

	ret = platform_device_add(pdev);
	if (ret) {
		dev_err(core->dev, "%s: platform_device_add err(%d)\n", __func__, ret);
		platform_device_put(pdev);
		return ERR_PTR(ret);
	}

	ret = of_dma_configure_id(&pdev->dev, core->dev->of_node, true, f_id);
	if (ret) {
		dev_err(core->dev, "%s: of_dma_configure_id err(%d)\n", __func__, ret);
		platform_device_unregister(to_platform_device(&pdev->dev));
		return ERR_PTR(ret);
	}

	return &pdev->dev;
}

static int venus_firmware_setup_iommu_dev(struct venus_core *core)
{
	const u32 f_id = VENUS_FIRMWARE;
	struct device *dev;
	int ret = 0;

	dev = venus_firmware_alloc_platform_dev(core, "video_firmware", &f_id);
	if (IS_ERR(dev)) {
		dev_err(core->dev, "%s: err\n", __func__);
		return PTR_ERR(dev);
	}

	if (!device_iommu_mapped(dev)) {
		device_unregister(dev);
		return -ENODEV;
	}

	ret = dma_set_mask_and_coherent(dev, core->res->dma_mask);
	if (ret) {
		device_unregister(dev);
		return ret;
	}

	core->fw.dev = dev;
	core->fw.iommu_domain = iommu_get_domain_for_dev(core->fw.dev);
	core->fw.iommu_domain_owned = false;

	return 0;
}

static int venus_firmware_init_auto_detect(struct venus_core *core)
{
	int ret;

	core->use_tz = false;
	if (qcom_scm_is_available()) {
		if (qcom_scm_pas_supported(VENUS_PAS_ID))
			core->use_tz = true;
	} else {
		ret = driver_deferred_probe_check_state(core->dev);
		if (ret == -EPROBE_DEFER)
			return ret;
	}

	/*
	 * 1. use_tz is false: No authentication is performed.
	 * 2. use_tz is true: TZ perform authentication.
	 *    a. device_iommu_mapped true: Linux config smmu
	 *    b. device_iommu_mapped false: TZ config smmu
	 */
	ret = venus_firmware_setup_iommu_dev(core);
	if (ret == -ENODEV && core->use_tz)
		ret = 0;

	return ret;
}

int venus_firmware_init(struct venus_core *core)
{
	struct platform_device_info info;
	struct iommu_domain *iommu_dom;
	struct platform_device *pdev;
	struct device_node *np;
	int ret;

	np = of_get_child_by_name(core->dev->of_node, "video-firmware");
	if (!np) {
		ret = venus_firmware_init_auto_detect(core);
		return ret;
	}

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
	if (ret) {
		dev_err(core->dev, "dma configure fail\n");
		goto err_unregister;
	}

	core->fw.dev = &pdev->dev;

	iommu_dom = iommu_paging_domain_alloc(core->fw.dev);
	if (IS_ERR(iommu_dom)) {
		dev_err(core->fw.dev, "Failed to allocate iommu domain\n");
		ret = PTR_ERR(iommu_dom);
		goto err_unregister;
	}

	ret = iommu_attach_device(iommu_dom, core->fw.dev);
	if (ret) {
		dev_err(core->fw.dev, "could not attach device\n");
		goto err_iommu_free;
	}

	core->fw.iommu_domain = iommu_dom;
	core->fw.iommu_domain_owned = true;

	of_node_put(np);

	return 0;

err_iommu_free:
	iommu_domain_free(iommu_dom);
err_unregister:
	core->fw.dev = NULL;
	platform_device_unregister(pdev);
	of_node_put(np);
	return ret;
}

void venus_firmware_deinit(struct venus_core *core)
{
	struct iommu_domain *iommu;

	if (!core->fw.dev)
		return;

	if (!core->use_tz && core->fw.iommu_domain_owned) {
		iommu = core->fw.iommu_domain;

		if (iommu) {
			iommu_detach_device(iommu, core->fw.dev);
			iommu_domain_free(iommu);
		}
	}
	platform_device_unregister(to_platform_device(core->fw.dev));
	core->fw.dev = NULL;
	core->fw.ctx = NULL;
	core->fw.iommu_domain = NULL;
	core->fw.iommu_domain_owned = false;
}
