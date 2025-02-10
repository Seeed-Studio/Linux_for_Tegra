/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, NVIDIA Corporation.  All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation.  Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>
#include <linux/debugfs.h>
#include <linux/firmware.h>
#include <linux/version.h>
#include <linux/nvhost.h>
#include <linux/nvhost_t194.h>
#include <linux/iommu.h>
#include <linux/dma-mapping.h>
#include <soc/tegra/virt/syscalls.h>
#include <asm/io.h>

#include "pva_kmd_device.h"
#include "pva_kmd_linux_device.h"
#include "pva_kmd_device_memory.h"
#include "pva_kmd_constants.h"
#include "pva_kmd_silicon_utils.h"
#include "pva_kmd_silicon_boot.h"

struct nvhost_device_data *
pva_kmd_linux_device_get_properties(struct platform_device *pdev)
{
	struct nvhost_device_data *props = platform_get_drvdata(pdev);
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
	struct nvhost_device_data *props = device_data->pva_device_properties;
	err = nvhost_syncpt_read_ext_check(props->pdev, syncpt_id,
					   syncpt_value);
	if (err < 0) {
		FAULT("Failed to read syncpoint value\n");
	}
}

void pva_kmd_get_syncpt_iova(struct pva_kmd_device *pva, uint32_t syncpt_id,
			     uint64_t *syncpt_iova)
{
	uint32_t offset = 0;
	struct pva_kmd_linux_device_data *device_data =
		pva_kmd_linux_device_get_data(pva);
	struct nvhost_device_data *props = device_data->pva_device_properties;

	struct platform_device *host_pdev =
		to_platform_device(props->pdev->dev.parent);

	offset = nvhost_syncpt_unit_interface_get_byte_offset_ext(host_pdev,
								  syncpt_id);
	*syncpt_iova = safe_addu64(pva->syncpt_ro_iova, (uint64_t)offset);
}

void pva_kmd_linux_host1x_init(struct pva_kmd_device *pva)
{
	phys_addr_t base;
	size_t size;
	int err = 0;
	uint32_t syncpt_page_size;
	uint32_t syncpt_offset[PVA_NUM_RW_SYNCPTS];
	dma_addr_t sp_start;
	struct platform_device *host_pdev;
	struct device *dev;
	struct pva_kmd_linux_device_data *device_data =
		pva_kmd_linux_device_get_data(pva);
	struct nvhost_device_data *props = device_data->pva_device_properties;
	nvhost_syncpt_unit_interface_init(props->pdev);

	host_pdev = to_platform_device(props->pdev->dev.parent);
	err = nvhost_syncpt_unit_interface_get_aperture(host_pdev, &base,
							&size);
	if (err < 0) {
		FAULT("Failed to get syncpt aperture\n");
	}
	/** Get page size of a syncpoint */
	syncpt_page_size =
		nvhost_syncpt_unit_interface_get_byte_offset_ext(host_pdev, 1);
	dev = &device_data->smmu_contexts[PVA_R5_SMMU_CONTEXT_ID]->dev;
	if (iommu_get_domain_for_dev(dev)) {
		sp_start = dma_map_resource(dev, base, size, DMA_TO_DEVICE,
					    DMA_ATTR_SKIP_CPU_SYNC);
		if (dma_mapping_error(dev, sp_start)) {
			FAULT("Failed to pin RO syncpoints\n");
		}
	} else {
		FAULT("Failed to pin RO syncpoints\n");
	}
	pva->syncpt_ro_iova = sp_start;
	pva->syncpt_offset = syncpt_page_size;
	pva->num_syncpts = (size / syncpt_page_size);

	for (uint32_t i = 0; i < PVA_NUM_RW_SYNCPTS; i++) {
		pva->syncpt_rw[i].syncpt_id = nvhost_get_syncpt_client_managed(
			props->pdev, "pva_syncpt");
		if (pva->syncpt_rw[i].syncpt_id == 0) {
			FAULT("Failed to get syncpt\n");
		}
		syncpt_offset[i] =
			nvhost_syncpt_unit_interface_get_byte_offset_ext(
				host_pdev, pva->syncpt_rw[i].syncpt_id);
		err = nvhost_syncpt_read_ext_check(
			props->pdev, pva->syncpt_rw[i].syncpt_id,
			&pva->syncpt_rw[i].syncpt_value);
		if (err < 0) {
			FAULT("Failed to read syncpoint value\n");
		}
	}

	pva->syncpt_rw_iova =
		dma_map_resource(dev,
				 safe_addu64(base, (uint64_t)syncpt_offset[0]),
				 safe_mulu64((uint64_t)pva->syncpt_offset,
					     (uint64_t)PVA_NUM_RW_SYNCPTS),
				 DMA_BIDIRECTIONAL, DMA_ATTR_SKIP_CPU_SYNC);
	if (dma_mapping_error(dev, pva->syncpt_rw_iova)) {
		FAULT("Failed to pin RW syncpoints\n");
	}
	pva->syncpt_rw[0].syncpt_iova = pva->syncpt_rw_iova;
	for (uint32_t i = 1; i < PVA_NUM_RW_SYNCPTS; i++) {
		if (safe_addu32(syncpt_offset[i - 1], pva->syncpt_offset) !=
		    syncpt_offset[i]) {
			FAULT("RW syncpts are not contiguous\n");
		}
		pva->syncpt_rw[i].syncpt_iova = safe_addu64(
			pva->syncpt_rw_iova,
			safe_mulu64((uint64_t)pva->syncpt_offset, (uint64_t)i));
	}
}

void pva_kmd_allocate_syncpts(struct pva_kmd_device *pva)
{
}

void pva_kmd_linux_host1x_deinit(struct pva_kmd_device *pva)
{
	int err = 0;
	phys_addr_t base;
	size_t size;
	struct device *dev;
	struct pva_kmd_linux_device_data *device_data =
		pva_kmd_linux_device_get_data(pva);
	struct nvhost_device_data *props = device_data->pva_device_properties;
	struct platform_device *host_pdev =
		to_platform_device(props->pdev->dev.parent);

	err = nvhost_syncpt_unit_interface_get_aperture(host_pdev, &base,
							&size);
	if (err < 0) {
		FAULT("Failed to get syncpt aperture\n");
	}

	dev = &device_data->smmu_contexts[PVA_R5_SMMU_CONTEXT_ID]->dev;
	if (iommu_get_domain_for_dev(dev)) {
		dma_unmap_resource(dev, pva->syncpt_ro_iova, size,
				   DMA_TO_DEVICE, DMA_ATTR_SKIP_CPU_SYNC);
		dma_unmap_resource(dev, pva->syncpt_rw_iova,
				   safe_mulu64((uint64_t)pva->syncpt_offset,
					       (uint64_t)PVA_NUM_RW_SYNCPTS),
				   DMA_BIDIRECTIONAL, DMA_ATTR_SKIP_CPU_SYNC);
	} else {
		FAULT("Failed to unmap syncpts\n");
	}
	for (uint32_t i = 0; i < PVA_NUM_RW_SYNCPTS; i++) {
		nvhost_syncpt_put_ref_ext(props->pdev,
					  pva->syncpt_rw[i].syncpt_id);
		pva->syncpt_rw[i].syncpt_id = 0;
		pva->syncpt_rw[i].syncpt_iova = 0;
		pva->syncpt_rw[i].syncpt_value = 0;
	}
	pva->syncpt_ro_iova = 0;
	pva->syncpt_rw_iova = 0;
	pva->syncpt_offset = 0;
	nvhost_syncpt_unit_interface_deinit(props->pdev);
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

enum pva_error pva_kmd_power_on(struct pva_kmd_device *pva)
{
	int err = 0;
	struct pva_kmd_linux_device_data *device_data =
		pva_kmd_linux_device_get_data(pva);
	struct nvhost_device_data *props = device_data->pva_device_properties;

	err = pm_runtime_get_sync(&props->pdev->dev);
	if (err < 0) {
		pm_runtime_put_noidle(&props->pdev->dev);
		goto out;
	}

	/* Power management operation is asynchronous. PVA may not be power
	 * cycled between power_off -> power_on call. Therefore, we need to
	 * reset it here to make sure it is in a clean state. */
	reset_control_acquire(props->reset_control);
	reset_control_reset(props->reset_control);
	reset_control_release(props->reset_control);

out:
	return kernel_err2pva_err(err);
}

void pva_kmd_power_off(struct pva_kmd_device *pva)
{
	struct pva_kmd_linux_device_data *device_data =
		pva_kmd_linux_device_get_data(pva);
	struct nvhost_device_data *props = device_data->pva_device_properties;

	pm_runtime_mark_last_busy(&props->pdev->dev);
	pm_runtime_put(&props->pdev->dev);

	/* Power management operation is asynchronous. We don't control when PVA
	 * will really be powered down. However, we need to free memories after
	 * this call. Therefore, we assert the reset line to stop PVA from any
	 * further activity. */
	reset_control_acquire(props->reset_control);
	reset_control_assert(props->reset_control);
	reset_control_release(props->reset_control);
}

uint32_t pva_kmd_get_syncpt_ro_offset(struct pva_kmd_device *pva)
{
	return safe_subu64(pva->syncpt_ro_iova, FW_SHARED_MEMORY_START);
}
uint32_t pva_kmd_get_syncpt_rw_offset(struct pva_kmd_device *pva)
{
	return safe_subu64(pva->syncpt_rw_iova, FW_SHARED_MEMORY_START);
}

enum pva_error pva_kmd_read_fw_bin(struct pva_kmd_device *pva)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_kmd_linux_device_data *device_data =
		pva_kmd_linux_device_get_data(pva);
	struct nvhost_device_data *device_props =
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
	struct nvhost_device_data *device_props =
		device_data->pva_device_properties;

	void __iomem *addr = device_props->aperture[aperture] + reg;

	writel(val, addr);
}

uint32_t pva_kmd_aperture_read(struct pva_kmd_device *pva,
			       enum pva_kmd_reg_aperture aperture, uint32_t reg)
{
	struct pva_kmd_linux_device_data *device_data =
		pva_kmd_linux_device_get_data(pva);
	struct nvhost_device_data *device_props =
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

/* TODO: Enable HVC call once HVC fix is available on dev-main */
//static void pva_kmd_config_regs(void)
//{
//bool hv_err = true;
//hv_err = hyp_pva_config_regs();
//ASSERT(hv_err == true);
//ASSERT(false);
//}

void pva_kmd_config_evp_seg_scr_regs(struct pva_kmd_device *pva)
{
	pva_kmd_config_evp_seg_regs(pva);
	pva_kmd_config_scr_regs(pva);
}

void pva_kmd_config_sid_regs(struct pva_kmd_device *pva)
{
	pva_kmd_config_sid(pva);
}
