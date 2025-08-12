// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>
#include <linux/debugfs.h>
#include <linux/firmware.h>
#include <linux/version.h>
#include <linux/mm.h>
#include <linux/iommu.h>
#include <linux/dma-mapping.h>
#include <soc/tegra/virt/syscalls.h>
#include <asm/io.h>
#include <linux/host1x-next.h>

#include "pva_kmd_device.h"
#include "pva_kmd_linux_device.h"
#include "pva_kmd_device_memory.h"
#include "pva_kmd_constants.h"
#include "pva_kmd_silicon_utils.h"
#include "pva_kmd_silicon_boot.h"
#include "pva_kmd_linux_device_api.h"

#define HVC_NR_PVA_CONFIG_REGS_CALL 0x8136U

__attribute__((no_sanitize_address)) static inline bool
hyp_pva_config_regs(void)
{
	uint64_t args[4] = { 0U, 0U, 0U, 0U };
	hyp_call44(HVC_NR_PVA_CONFIG_REGS_CALL, args);

	return (args[0] == 0U);
}

struct nvpva_device_data *
pva_kmd_linux_device_get_properties(struct platform_device *pdev)
{
	struct nvpva_device_data *props = platform_get_drvdata(pdev);
	return props;
}

struct pva_kmd_linux_device_data *
pva_kmd_linux_device_get_data(struct pva_kmd_device *device)
{
	return (struct pva_kmd_linux_device_data *)device->plat_data;
}

void pva_kmd_linux_device_set_data(struct pva_kmd_device *device,
				   struct pva_kmd_linux_device_data *data)
{
	device->plat_data = (void *)data;
}

void pva_kmd_read_syncpt_val(struct pva_kmd_device *pva, uint32_t syncpt_id,
			     uint32_t *syncpt_value)
{
	int err = 0;
	struct pva_kmd_linux_device_data *device_data =
		pva_kmd_linux_device_get_data(pva);
	struct nvpva_device_data *props = device_data->pva_device_properties;
	err = nvpva_syncpt_read_ext_check(props->pdev, syncpt_id, syncpt_value);
	if (err < 0) {
		FAULT("Failed to read syncpoint value\n");
	}
}

int pva_kmd_linux_host1x_init(struct pva_kmd_device *pva)
{
	phys_addr_t syncpt_phys_base;
	size_t all_syncpt_size;
	int err = 0;
	uint32_t stride, num_syncpts;
	uint32_t syncpt_page_size;
	dma_addr_t sp_start;
	struct pva_kmd_linux_device_data *device_data =
		pva_kmd_linux_device_get_data(pva);
	struct nvpva_device_data *props = device_data->pva_device_properties;
	struct device *dev =
		&device_data->smmu_contexts[PVA_R5_SMMU_CONTEXT_ID]->dev;

	if (iommu_get_domain_for_dev(dev) == NULL) {
		dev_err(dev, "Cannot use syncpt without IOMMU");
		err = -EFAULT;
		goto err_out;
	}

	props->host1x = nvpva_device_to_host1x(props->pdev);

	err = nvpva_syncpt_unit_interface_init(props->pdev);
	if (err < 0) {
		dev_err(dev, "Failed syncpt unit interface init");
		goto err_out;
	}

	err = host1x_syncpt_get_shim_info(props->host1x, &syncpt_phys_base,
					  &stride, &num_syncpts);
	if (err < 0) {
		dev_err(dev, "Failed to get syncpt shim_info");
		goto err_out;
	}

	all_syncpt_size = stride * num_syncpts;
	syncpt_page_size = stride;
	sp_start = dma_map_resource(dev, syncpt_phys_base, all_syncpt_size,
				    DMA_BIDIRECTIONAL, DMA_ATTR_SKIP_CPU_SYNC);
	if (dma_mapping_error(dev, sp_start)) {
		dev_err(dev, "Failed to map RO syncpoints");
		goto err_out;
	}

	pva->ro_syncpt_base_iova = sp_start;
	pva->syncpt_page_size = syncpt_page_size;
	pva->num_ro_syncpts = num_syncpts;

	pva->rw_syncpt_base_iova = sp_start;
	pva->rw_syncpt_region_size = all_syncpt_size;

	dev_info(dev, "PVA syncpt (RO & RW) iova: %llx, size: %lx\n",
		 pva->ro_syncpt_base_iova, all_syncpt_size);

	for (uint32_t i = 0; i < PVA_NUM_RW_SYNCPTS; i++) {
		uint32_t syncpt_id;

		syncpt_id = nvpva_get_syncpt_client_managed(props->pdev,
							    "pva_syncpt");
		if (syncpt_id == 0) {
			dev_err(dev, "Failed to allocate RW syncpt");
			err = -EFAULT;
			goto free_syncpts;
		}

		pva->rw_syncpts[i].syncpt_id = syncpt_id;
		pva->rw_syncpts[i].syncpt_iova =
			safe_addu64(pva->rw_syncpt_base_iova,
				    safe_mulu32(syncpt_id, syncpt_page_size));
	}

	return 0;

free_syncpts:
	for (uint32_t i = 0; i < PVA_NUM_RW_SYNCPTS; i++) {
		if (pva->rw_syncpts[i].syncpt_id != 0) {
			nvpva_syncpt_put_ref_ext(props->pdev,
						 pva->rw_syncpts[i].syncpt_id);
			pva->rw_syncpts[i].syncpt_id = 0;
		}
	}

err_out:
	return err;
}

void pva_kmd_linux_host1x_deinit(struct pva_kmd_device *pva)
{
	int err = 0;
	phys_addr_t base;
	size_t size;
	uint32_t stride, num_syncpts;
	struct pva_kmd_linux_device_data *device_data =
		pva_kmd_linux_device_get_data(pva);
	struct nvpva_device_data *props = device_data->pva_device_properties;
	struct device *dev =
		&device_data->smmu_contexts[PVA_R5_SMMU_CONTEXT_ID]->dev;

	if (iommu_get_domain_for_dev(dev) == NULL) {
		dev_err(dev, "Cannot use syncpt without IOMMU");
		return;
	}

	err = host1x_syncpt_get_shim_info(props->host1x, &base, &stride,
					  &num_syncpts);
	if (err < 0) {
		dev_err(dev, "Failed to get syncpt shim_info when deiniting");
		return;
	}
	size = stride * num_syncpts;

	dma_unmap_resource(dev, pva->ro_syncpt_base_iova, size, DMA_TO_DEVICE,
			   DMA_ATTR_SKIP_CPU_SYNC);

	dma_unmap_sg_attrs(dev, device_data->syncpt_sg, PVA_NUM_RW_SYNCPTS,
			   DMA_BIDIRECTIONAL, DMA_ATTR_SKIP_CPU_SYNC);

	for (uint32_t i = 0; i < PVA_NUM_RW_SYNCPTS; i++) {
		nvpva_syncpt_put_ref_ext(props->pdev,
					 pva->rw_syncpts[i].syncpt_id);
		pva->rw_syncpts[i].syncpt_id = 0;
		pva->rw_syncpts[i].syncpt_iova = 0;
	}
	pva->ro_syncpt_base_iova = 0;
	pva->syncpt_page_size = 0;
	nvpva_syncpt_unit_interface_deinit(props->pdev);
}

void pva_kmd_device_plat_init(struct pva_kmd_device *pva)
{
	struct pva_kmd_linux_device_data *plat_data =
		pva_kmd_zalloc_nofail(sizeof(struct pva_kmd_linux_device_data));

	pva_kmd_linux_device_set_data(pva, plat_data);

	/* Get SMMU context devices that were probed earlier and their SIDs */
	pva_kmd_linux_device_smmu_contexts_init(pva);
}

void pva_kmd_device_plat_deinit(struct pva_kmd_device *pva)
{
	pva_kmd_linux_host1x_deinit(pva);
	pva_kmd_free(pva_kmd_linux_device_get_data(pva));
}

enum pva_error pva_kmd_device_busy(struct pva_kmd_device *pva)
{
	int err = 0;
	enum pva_error pva_err = PVA_SUCCESS;
	struct pva_kmd_linux_device_data *device_data =
		pva_kmd_linux_device_get_data(pva);
	struct nvpva_device_data *props = device_data->pva_device_properties;

	pva_kmd_mutex_lock(&pva->powercycle_lock);

	// Once firmware is aborted, we no longer allow incrementing PVA
	// refcount. This makes sure refcount will eventually reach 0 and allow
	// device to be powered off.
	if (pva->recovery) {
		pva_kmd_log_err(
			"PVA firmware aborted. Waiting for active PVA uses to finish");
		pva_err = PVA_ERR_FW_ABORTED;
		goto unlock;
	}

	err = pm_runtime_get_sync(&props->pdev->dev);
	if (err < 0) {
		pm_runtime_put_noidle(&props->pdev->dev);
		pva_kmd_log_err_u64(
			"pva_kmd_device_busy pm_runtime_get_sync failed",
			(uint64_t)(-err));
		goto convert_err;
	}

	pva->refcount = safe_addu32(pva->refcount, 1);

convert_err:
	pva_err = kernel_err2pva_err(err);
unlock:
	pva_kmd_mutex_unlock(&pva->powercycle_lock);
	return pva_err;
}

void pva_kmd_device_idle(struct pva_kmd_device *pva)
{
	struct pva_kmd_linux_device_data *device_data =
		pva_kmd_linux_device_get_data(pva);
	struct nvpva_device_data *props = device_data->pva_device_properties;
	int err = 0;

	pva_kmd_mutex_lock(&pva->powercycle_lock);

	pva->refcount = safe_subu32(pva->refcount, 1);

	if (pva->refcount == 0 && pva->recovery) {
		/*
		 * At this point, there are no active PVA users (refcount=0).
		 * Since PVA needs recovery (recovery=true), perform a forced
		 * power cycle to recover it.
		 */
		err = pm_runtime_force_suspend(&props->pdev->dev);
		if (err == 0) {
			err = pm_runtime_force_resume(&props->pdev->dev);
		}
		if (err < 0) {
			pva_kmd_log_err("Failed to recover PVA");
		}
	}

	pm_runtime_mark_last_busy(&props->pdev->dev);
	pm_runtime_put(&props->pdev->dev);

	pva_kmd_mutex_unlock(&pva->powercycle_lock);
}

void pva_kmd_set_reset_line(struct pva_kmd_device *pva)
{
	struct pva_kmd_linux_device_data *device_data =
		pva_kmd_linux_device_get_data(pva);
	struct nvpva_device_data *props = device_data->pva_device_properties;

	/* FW Reset recovery operation is asynchronous.
	 * we need to free memories after this call.
	 * Therefore, we assert the reset line to stop PVA from any
	 * further activity. */
	reset_control_acquire(props->reset_control);
	reset_control_assert(props->reset_control);
	reset_control_release(props->reset_control);
}

enum pva_error pva_kmd_read_fw_bin(struct pva_kmd_device *pva)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_kmd_linux_device_data *device_data =
		pva_kmd_linux_device_get_data(pva);
	struct nvpva_device_data *device_props =
		device_data->pva_device_properties;
	struct pva_kmd_device_memory *fw_bin_mem;

	const struct firmware *fw_ucode;
	int kerr = request_firmware(&fw_ucode, device_props->firmware_name,
				    &device_props->pdev->dev);
	if (kerr < 0) {
		err = kernel_err2pva_err(kerr);
		goto out;
	}

	fw_bin_mem = pva_kmd_device_memory_alloc_map(
		safe_pow2_roundup_u64(fw_ucode->size, SIZE_4KB), pva,
		PVA_ACCESS_RW, PVA_R5_SMMU_CONTEXT_ID);
	if (fw_bin_mem == NULL) {
		err = PVA_NOMEM;
		goto release;
	}

	memcpy(fw_bin_mem->va, fw_ucode->data, fw_ucode->size);

	pva->fw_bin_mem = fw_bin_mem;
release:
	release_firmware(fw_ucode);
out:
	return err;
}

void pva_kmd_aperture_write(struct pva_kmd_device *pva,
			    enum pva_kmd_reg_aperture aperture, uint32_t reg,
			    uint32_t val)
{
	struct pva_kmd_linux_device_data *device_data =
		pva_kmd_linux_device_get_data(pva);
	struct nvpva_device_data *device_props =
		device_data->pva_device_properties;

	void __iomem *addr = device_props->aperture[aperture] + reg;

	writel(val, addr);
}

uint32_t pva_kmd_aperture_read(struct pva_kmd_device *pva,
			       enum pva_kmd_reg_aperture aperture, uint32_t reg)
{
	struct pva_kmd_linux_device_data *device_data =
		pva_kmd_linux_device_get_data(pva);
	struct nvpva_device_data *device_props =
		device_data->pva_device_properties;

	void __iomem *addr = device_props->aperture[aperture] + reg;

	return readl(addr);
}

enum pva_error kernel_err2pva_err(int err)
{
	if (err >= 0) {
		return PVA_SUCCESS;
	}

	switch (err) {
	case -EINVAL:
		return PVA_INVAL;
	case -EINTR:
		return PVA_EINTR;
	default:
		return PVA_UNKNOWN_ERROR;
	}
}

unsigned long pva_kmd_copy_data_from_user(void *dst, const void *src,
					  uint64_t size)
{
	return copy_from_user(dst, src, size);
}

unsigned long pva_kmd_copy_data_to_user(void __user *to, const void *from,
					unsigned long size)
{
	return copy_to_user(to, from, size);
}

unsigned long pva_kmd_strtol(const char *str, int base)
{
	unsigned long val;
	int ret;

	ret = kstrtoul(str, base, &val);
	if (ret < 0)
		return 0;

	return val;
}

static void pva_kmd_config_regs(void)
{
	bool hv_err = true;
	hv_err = hyp_pva_config_regs();
	ASSERT(hv_err == true);
}

void pva_kmd_config_evp_seg_scr_regs(struct pva_kmd_device *pva)
{
	if (pva->load_from_gsc && pva->is_hv_mode) {
		/* HVC Call to program EVP, Segment config registers and SCR registers */
		pva_kmd_config_regs();
	} else {
		pva_kmd_config_evp_seg_regs(pva);
		pva_kmd_config_scr_regs(pva);
	}
}

void pva_kmd_config_sid_regs(struct pva_kmd_device *pva)
{
	if (!(pva->load_from_gsc && pva->is_hv_mode)) {
		pva_kmd_config_sid(pva);
	}
}

bool pva_kmd_device_maybe_on(struct pva_kmd_device *pva)
{
	struct pva_kmd_linux_device_data *device_data =
		pva_kmd_linux_device_get_data(pva);
	struct nvpva_device_data *device_props =
		device_data->pva_device_properties;
	struct device *dev = &device_props->pdev->dev;

	if (pm_runtime_active(dev)) {
		return true;
	} else {
		return false;
	}
}
void pva_kmd_report_error_fsi(struct pva_kmd_device *pva, uint32_t error_code)
{
	//TODO: Implement FSI error reporting once available for Linux
}
