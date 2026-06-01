// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVDLA KMD-RISCV implementation
 */

#include "../nvdla_fw.h"
#include "../nvdla_host_wrapper.h"

#include "nvdla_fw_riscv_reg.h"

#include "../nvdla_device.h"
#include "../../dla_os_interface.h"
#include "../../nvdla.h"
#include "../../nvdla_ctx.h"
#include "../../nvdla_debug.h"

#include <linux/errno.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>

#define DLA_UCODE_BIN_HEADER_MAGIC 0x10fe
#define DLA_BOOTVECTOR_LO 0x100000

struct riscv_bin_header {
	uint32_t magic;
	uint32_t version;
	uint32_t size;
	uint32_t ucode_header_offset;
	uint32_t ucode_offset;
	uint32_t ucode_size;
};

struct riscv_ucode_header {
	uint32_t version;
	uint32_t bootloader_offset;
	uint32_t bootloader_size;
	uint32_t bootloader_param_offset;
	uint32_t bootloader_param_size;
	uint32_t riscv_elf_offset;
	uint32_t riscv_elf_size;
	uint32_t app_version;
	uint32_t manifest_offset;
	uint32_t manifest_size;
	uint32_t monitor_data_offset;
	uint32_t monitor_data_size;
	uint32_t monitor_code_offset;
	uint32_t monitor_code_size;
	uint32_t is_monitor_enabled;
	uint32_t swbrom_code_offset;
	uint32_t swbrom_code_size;
	uint32_t swbrom_data_offset;
	uint32_t swbrom_data_size;
};

enum {
	NVDLA_IRQ_TYPE_SWGEN = 0,
	NVDLA_IRQ_TYPE_MNOC_TX_READY = 1,
	NVDLA_IRQ_TYPE_MNOC_RX_READY = 2,
	NVDLA_IRQ_TYPE_MAX,
};

struct riscv {
	struct device *dev;
	struct platform_device *pdev;
	struct nvdla_device *nvdladev;
	int32_t irq[NVDLA_IRQ_TYPE_MAX];

	void *cpuva;
	dma_addr_t iova;
	size_t size;

	uint32_t ucode_offset;
	uint32_t ucode_size;
	uint32_t ucode_code_offset;
	uint32_t ucode_code_size;
	uint32_t ucode_data_offset;
	uint32_t ucode_data_size;
};

static int32_t s_riscv_parse_firmware(struct riscv *riscv)
{
	int32_t err;

	struct riscv_bin_header *bin_header;
	struct riscv_ucode_header *ucode_header;

	bin_header = (struct riscv_bin_header *) riscv->cpuva;

	if (bin_header->magic != DLA_UCODE_BIN_HEADER_MAGIC) {
		dev_err(riscv->dev, "Invalid ucode binary magic\n");
		err = -EINVAL;
		goto fail;
	}

	if (bin_header->size > riscv->size) {
		dev_err(riscv->dev, "Inconsistent binary size\n");
		err = -EINVAL;
		goto fail;
	}

	ucode_header = (struct riscv_ucode_header *)
		(riscv->cpuva + bin_header->ucode_header_offset);

	riscv->ucode_offset = bin_header->ucode_offset;
	riscv->ucode_size = bin_header->ucode_size;
	riscv->ucode_code_offset = ucode_header->monitor_code_offset;
	riscv->ucode_code_size = ucode_header->monitor_code_size;
	riscv->ucode_data_offset = ucode_header->monitor_data_offset;
	riscv->ucode_data_size = ucode_header->monitor_data_size;

	return 0;
fail:
	return err;
}

static int32_t s_riscv_load_firmware(struct riscv *riscv,
	char *firmware_name)
{
	int32_t err;
	const struct firmware *firmware;
	size_t i;
	void *cpuva;
	uint32_t *cpuva_u32;
	dma_addr_t iova;

	err = request_firmware(&firmware, firmware_name, riscv->dev);
	if (err < 0) {
		dev_err(riscv->dev, "failed to request firmware");
		goto fail;
	}

	riscv->size = firmware->size;
	cpuva = dma_alloc_coherent(riscv->dev, riscv->size,
				&iova, GFP_KERNEL);
	if (!cpuva) {
		err = -ENOMEM;
		goto unload_firmware;
	}

	cpuva_u32 = (uint32_t *) cpuva;
	for (i = 0; i < riscv->size / sizeof(uint32_t); i++)
		cpuva_u32[i] = le32_to_cpu(((__le32 *)firmware->data)[i]);
	riscv->cpuva = cpuva;
	riscv->iova = iova;

	err = s_riscv_parse_firmware(riscv);
	if (err < 0)
		goto free_firmware;

	release_firmware(firmware);


	return 0;

free_firmware:
	riscv->iova = 0;
	riscv->cpuva = NULL;
	dma_free_coherent(riscv->dev, riscv->size, riscv->cpuva, riscv->iova);
unload_firmware:
	riscv->size = 0ULL;
	release_firmware(firmware);
fail:
	return err;
}

static void s_riscv_unload_firmware(struct riscv *riscv)
{
	if (!riscv)
		goto done;

	dma_free_coherent(riscv->dev, riscv->size, riscv->cpuva, riscv->iova);

	riscv->size = 0ULL;
	riscv->cpuva = NULL;
	riscv->iova = 0;
	riscv->ucode_offset = 0U;
	riscv->ucode_size = 0U;
	riscv->ucode_code_offset = 0U;
	riscv->ucode_code_size = 0U;
	riscv->ucode_data_offset = 0U;
	riscv->ucode_data_size = 0U;

done:
	return;
}

static int32_t s_riscv_wait_mthdid_idle(struct riscv *riscv)
{
	int32_t err;
	uint32_t value;
	void __iomem *regs;

	regs = nvdla_device_register_get_aperture(riscv->pdev,
			NVDLA_APERTURE_TYPE_DLA);
	if (!regs) {
		err = -EINVAL;
		goto fail;
	}

	err = readl_poll_timeout(regs + riscv_mthdid_r(),
			value,
			(riscv_mthdid_wpend_v(value) == riscv_mthdid_wpend_done_v()),
			10 /* sleep in us */,
			100000 /* timeout in us*/);
	if (err < 0)
		goto fail;

	return 0;

fail:
	return err;
}

static int32_t s_riscv_wait_idle(struct riscv *riscv)
{
	int32_t err;
	uint32_t value;
	void __iomem *regs;

	regs = nvdla_device_register_get_aperture(riscv->pdev,
			NVDLA_APERTURE_TYPE_DLA);
	if (!regs) {
		err = -EINVAL;
		goto fail;
	}

	err = readl_poll_timeout(regs + riscv_idlestate_r(),
			value,
			(value == 0),
			10 /* sleep in us */,
			100000 /* timeout in us*/);
	if (err < 0)
		goto fail;

	return 0;

fail:
	return err;
}

static int32_t s_riscv_dmactl_wait_ready(struct riscv *riscv)
{
	int32_t err;
	uint32_t value;
	void __iomem *regs;

	regs = nvdla_device_register_get_aperture(riscv->pdev,
			NVDLA_APERTURE_TYPE_DLA);
	if (!regs) {
		err = -EINVAL;
		goto fail;
	}

	err = readl_poll_timeout(regs + riscv_dmactl_r(),
			value,
			(value & (riscv_dmactl_imem_scrubbing_m() |
				riscv_dmactl_dmem_scrubbing_m())) == 0,
			10 /* sleep in us */,
			100000 /* timeout in us*/);
	if (err < 0)
		goto fail;

	return 0;

fail:
	return err;
}

static int32_t s_riscv_dma_wait_idle(struct riscv *riscv)
{
	int32_t err;
	uint32_t value;
	void __iomem *regs;

	regs = nvdla_device_register_get_aperture(riscv->pdev,
			NVDLA_APERTURE_TYPE_DLA);
	if (!regs) {
		err = -EINVAL;
		goto fail;
	}

	err = readl_poll_timeout(regs + riscv_dmatrfcmd_r(),
			value,
			(riscv_dmatrfcmd_idle_v(value) ==
			 riscv_dmatrfcmd_idle_true_v()),
			10 /* sleep in us */,
			100000 /* timeout in us*/);
	if (err < 0)
		goto fail;

	return 0;

fail:
	return err;
}

static int32_t s_riscv_copy_dmem_size256(struct riscv *riscv,
	uint32_t offset)
{
	uint32_t cmd = (riscv_dmatrfcmd_size_256b_f() |
		riscv_dmatrfcmd_ctxdma_f(riscv_transcfg_falc_swid_v()));
	phys_addr_t base = riscv->ucode_data_offset + offset;

	nvdla_device_register_write(riscv->pdev, NVDLA_APERTURE_TYPE_DLA,
		riscv_dmatrfmoffs_r(), offset);
	nvdla_device_register_write(riscv->pdev, NVDLA_APERTURE_TYPE_DLA,
		riscv_dmatrffboffs_r(), base);
	nvdla_device_register_write(riscv->pdev, NVDLA_APERTURE_TYPE_DLA,
		riscv_dmatrfcmd_r(), cmd);

	return s_riscv_dma_wait_idle(riscv);
}

static int32_t s_riscv_copy_imem_size256(struct riscv *riscv,
				 unsigned long offset)
{
	uint32_t cmd = (riscv_dmatrfcmd_size_256b_f() |
		riscv_dmatrfcmd_ctxdma_f(riscv_transcfg_falc_swid_v()) |
		riscv_dmatrfcmd_imem_true_f());
	phys_addr_t base = riscv->ucode_code_offset + offset;

	nvdla_device_register_write(riscv->pdev, NVDLA_APERTURE_TYPE_DLA,
		riscv_dmatrfmoffs_r(), offset);
	nvdla_device_register_write(riscv->pdev, NVDLA_APERTURE_TYPE_DLA,
		riscv_dmatrffboffs_r(), base);
	nvdla_device_register_write(riscv->pdev, NVDLA_APERTURE_TYPE_DLA,
		riscv_dmatrfcmd_r(), cmd);

	return s_riscv_dma_wait_idle(riscv);
}

static int32_t s_riscv_load_mem_bitbang(struct riscv *riscv)
{
	uint32_t imem_offset = riscv->ucode_offset + riscv->ucode_code_offset;
	uint32_t *imem_cpuva = (uint32_t *) (riscv->cpuva + imem_offset);
	uint32_t dmem_offset = riscv->ucode_offset + riscv->ucode_data_offset;
	uint32_t *dmem_cpuva = (uint32_t *) (riscv->cpuva + dmem_offset);
	uint32_t i;

	nvdla_device_register_write(riscv->pdev, NVDLA_APERTURE_TYPE_DLA,
		riscv_imemc_r(0U), riscv_imemc_aincw_true_f());
	for (i = 0; i < riscv->ucode_code_size / sizeof(uint32_t); i++) {
		nvdla_device_register_write(riscv->pdev, NVDLA_APERTURE_TYPE_DLA,
			riscv_imemd_r(0U), imem_cpuva[i]);
	}
	nvdla_device_register_write(riscv->pdev, NVDLA_APERTURE_TYPE_DLA,
		riscv_imemc_r(0U), 0U);

	nvdla_device_register_write(riscv->pdev, NVDLA_APERTURE_TYPE_DLA,
		riscv_dmemc_r(0U), riscv_dmemc_aincw_true_f());
	for (i = 0; i < riscv->ucode_data_size / sizeof(uint32_t); i++) {
		nvdla_device_register_write(riscv->pdev, NVDLA_APERTURE_TYPE_DLA,
			riscv_dmemd_r(0U), dmem_cpuva[i]);
	}
	nvdla_device_register_write(riscv->pdev, NVDLA_APERTURE_TYPE_DLA,
		riscv_dmemc_r(0U), 0U);

	return 0;
}

static int32_t s_riscv_load_mem_dma(struct riscv *riscv)
{
	int32_t err;
	uint32_t i;

	/* Wait for DMA ready. */
	err = s_riscv_dmactl_wait_ready(riscv);
	if (err < 0)
		goto fail;

	nvdla_device_register_write(riscv->pdev, NVDLA_APERTURE_TYPE_DLA,
		riscv_dmactl_r(), 0);
	nvdla_device_register_write(riscv->pdev, NVDLA_APERTURE_TYPE_DLA,
		riscv_dmatrfbase_r(), (riscv->iova + riscv->ucode_offset) >> 8);

	/* copy the data segment into Falcon internal memory */
	for (i = 0; i < riscv->ucode_data_size; i += 256)
		s_riscv_copy_dmem_size256(riscv, i);

	/* copy the code segment into Falcon internal memory */
	for (i = 0; i < riscv->ucode_code_size; i += 256)
		s_riscv_copy_imem_size256(riscv, i);

	return 0;
fail:
	return err;
}

static int32_t s_riscv_boot(struct riscv *riscv)
{
	int32_t err;

	uint32_t boot_vector_hi;
	uint32_t bcr_ctrl;

	/* Select riscv. */
	boot_vector_hi = nvdla_device_register_read(riscv->pdev,
						NVDLA_APERTURE_TYPE_DLA,
						riscv_boot_vector_hi_r());
	if (boot_vector_hi != 0) {
		dev_err(riscv->dev, "Invalid boot vector high address: %u\n",
			boot_vector_hi);
		err = -EINVAL;
		goto fail;
	}

	nvdla_device_register_write(riscv->pdev, NVDLA_APERTURE_TYPE_DLA,
		riscv_boot_vector_lo_r(), DLA_BOOTVECTOR_LO);
	nvdla_device_register_write(riscv->pdev, NVDLA_APERTURE_TYPE_DLA,
		riscv_bcr_ctrl_r(), riscv_bcr_ctrl_core_select_riscv_f());

	bcr_ctrl = nvdla_device_register_read(riscv->pdev,
				NVDLA_APERTURE_TYPE_DLA, riscv_bcr_ctrl_r());
	if ((riscv_bcr_ctrl_core_select_v(bcr_ctrl) !=
			riscv_bcr_ctrl_core_select_riscv_v()) ||
		(riscv_bcr_ctrl_valid_v(bcr_ctrl) !=
			riscv_bcr_ctrl_valid_true_v())) {
		dev_err(riscv->dev, "Switch to RISCV MCU failed\n");
		err = -EINVAL;
		goto fail;
	}

	dev_info(riscv->dev, "Switched to RISCV MCU successfully\n");

	if (riscv->nvdladev->bitbang == 1U)
		s_riscv_load_mem_bitbang(riscv);
	else
		s_riscv_load_mem_dma(riscv);

	/* enable interface */
	nvdla_device_register_write(riscv->pdev, NVDLA_APERTURE_TYPE_DLA,
		riscv_itfen_r(),
		riscv_itfen_ctxen_enable_f() | riscv_itfen_mthden_enable_f());

	/* boot riscv */
	nvdla_device_register_write(riscv->pdev, NVDLA_APERTURE_TYPE_DLA,
		riscv_cpuctl_r(), riscv_cpuctl_startcpu_true_f());

	return 0;
fail:
	return err;
}

static int32_t s_riscv_finalize_poweron(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	struct riscv *riscv = (struct riscv *) pdata->falcon_data;
	struct nvdla_device *context_dev = NULL;
	struct nvhost_device_data *pdata_ctx = NULL;
	struct riscv *riscv_ctx = NULL;
	int err;
	bool skip_boot = false;

	err = s_riscv_load_firmware(riscv, pdata->firmware_name);
	if (err < 0) {
		nvdla_dbg_err(pdev, "firmware load err: %d\n", err);
		goto fail;
	}

#if defined(BUG_4960393) && (BUG_4960393 == 1)
	{
		uint32_t bcr_ctrl;

		bcr_ctrl = nvdla_device_register_read(riscv->pdev,
					NVDLA_APERTURE_TYPE_DLA,
					riscv_bcr_ctrl_r());
		if (riscv_bcr_ctrl_valid_v(bcr_ctrl) ==
			riscv_bcr_ctrl_valid_true_v()) {
			dev_warn(riscv->dev, "uC will continue from halt.\n");
			dev_warn(riscv->dev, "Skipping uC boot.\n");
			skip_boot = true;
		}
	}
#endif /* BUG_4960393 */

	if (!skip_boot) {
		/* Falcon will use ctxdma 2 to access ucode imem/dmem */
		nvdla_device_register_write(pdev, NVDLA_APERTURE_TYPE_DLA,
			pdata->transcfg_addr, pdata->transcfg_val);

		err = s_riscv_boot(riscv);
		if (err < 0) {
			nvdla_dbg_err(pdev, "boot err: %d\n", err);
			goto unload_firmware;
		}

		err = s_riscv_wait_idle(riscv);
		if (err < 0) {
			nvdla_dbg_err(pdev, "boot timed out\n");
			goto unload_firmware;
		}
	}

	/* Enable Parent IRQs */
	if (pdata->flcn_isr) {
		if (riscv->irq[NVDLA_IRQ_TYPE_SWGEN] > 0)
			enable_irq(riscv->irq[NVDLA_IRQ_TYPE_SWGEN]);
	}

	/* Enable Child Ctx IRQs */
	mutex_lock(&nvdla_dev->ctx_list_lock);
	list_for_each_entry(context_dev, &nvdla_dev->ctx_list_head, ctx_list_node) {
		pdata_ctx = platform_get_drvdata(context_dev->pdev);

		if (pdata_ctx->flcn_isr) {
			riscv_ctx = (struct riscv *) pdata_ctx->falcon_data;

			if (riscv_ctx->irq[NVDLA_IRQ_TYPE_MNOC_TX_READY] > 0)
				enable_irq(riscv_ctx->irq[NVDLA_IRQ_TYPE_MNOC_TX_READY]);
		}
	}
	mutex_unlock(&nvdla_dev->ctx_list_lock);

	return 0;
unload_firmware:
	s_riscv_unload_firmware(riscv);
fail:
	return err;
}

static void s_riscv_prepare_poweroff(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	struct riscv *riscv = (struct riscv *) pdata->falcon_data;
	struct nvdla_device *context_dev = NULL;
	struct nvhost_device_data *pdata_ctx = NULL;
	struct riscv *riscv_ctx = NULL;

	/* Disable Parent IRQs */
	if (pdata->flcn_isr) {
		if (riscv->irq[NVDLA_IRQ_TYPE_SWGEN] > 0)
			disable_irq(riscv->irq[NVDLA_IRQ_TYPE_SWGEN]);
	}

	/* Disable Child Ctx IRQs */
	mutex_lock(&nvdla_dev->ctx_list_lock);
	list_for_each_entry(context_dev, &nvdla_dev->ctx_list_head, ctx_list_node) {
		pdata_ctx = platform_get_drvdata(context_dev->pdev);

		if (pdata_ctx->flcn_isr) {
			riscv_ctx = (struct riscv *) pdata_ctx->falcon_data;

			if (riscv_ctx->irq[NVDLA_IRQ_TYPE_MNOC_TX_READY] > 0)
				disable_irq(riscv_ctx->irq[NVDLA_IRQ_TYPE_MNOC_TX_READY]);
		}
	}
	mutex_unlock(&nvdla_dev->ctx_list_lock);

	s_riscv_unload_firmware(riscv);
}

int32_t nvdla_fw_poweron(struct platform_device *pdev)
{
	int32_t err = 0;

	struct nvhost_device_data *pdata;
	uint32_t fw_ver_read_bin;
	uint32_t firmware_version;
	struct nvdla_device *nvdla_dev;

	if (pdev == NULL) {
		err = -EINVAL;
		goto fail;
	}

	pdata = platform_get_drvdata(pdev);
	if (pdata == NULL) {
		nvdla_dbg_err(pdev, "invalid pdata\n");
		err = -EINVAL;
		goto fail;
	}
	nvdla_dev = pdata->private_data;
	err = s_riscv_finalize_poweron(pdev);
	if (err) {
		nvdla_dbg_err(pdev, "failed to poweron\n");
		goto fail;
	}

	fw_ver_read_bin = nvdla_device_register_read(pdev,
						NVDLA_APERTURE_TYPE_DLA,
						riscv_os_version_r());

	firmware_version = pdata->version;

	if ((firmware_version & 0xffff00) != (fw_ver_read_bin & 0xffff00)) {
		nvdla_dbg_err(pdev,
		"Kernel fw ver[%u.%u.%u] mismatches loaded fw ver[%u.%u.%u]",
		(firmware_version >> 16) & 0xff,
		(firmware_version >> 8) & 0xff,
		(firmware_version & 0xff),
		(fw_ver_read_bin >> 16) & 0xff,
		(fw_ver_read_bin >> 8) & 0xff,
		(fw_ver_read_bin & 0xff));

		err = -EINVAL;
		goto poweroff;
	}

	nvdla_dbg_info(pdev, "Fw version : [%u.%u.%u]\n",
		(fw_ver_read_bin >> 16) & 0xff,
		(fw_ver_read_bin >> 8) & 0xff,
		fw_ver_read_bin & 0xff);

	nvdla_dev->fw_version = fw_ver_read_bin;

	return 0;

poweroff:
	 s_riscv_prepare_poweroff(pdev);
fail:
	return err;
}

int32_t nvdla_fw_poweroff(struct platform_device *pdev)
{
	int32_t err = 0;

	if (pdev == NULL) {
		err = -EINVAL;
		goto fail;
	}

	s_riscv_prepare_poweroff(pdev);

fail:
	return err;
}

static void handle_cmd_completion(struct nvdla_device *nvdla_dev, uint32_t error_status)
{
	nvdla_dev->cmd_status = error_status;
	nvdla_dev->waiting = 0;
	complete(&nvdla_dev->cmd_completion);
}

// Handle MNOC response
int nvdla_flcn_ctx_tx_isr(struct platform_device *pdev)
{
	uint32_t message;
	uint32_t mailbox0;
	uint32_t error_status;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	struct platform_device *parent_dev = nvdla_dev->parent_pdev;
	struct nvhost_device_data *parent_pdata = platform_get_drvdata(parent_dev);
	struct nvdla_device *parent_nvdla_dev = parent_pdata->private_data;

	/* dump falcon data if debug enabled */
	(void) nvdla_fw_interrupt_stat_read_mnoc(pdev, &mailbox0);

	message = mailbox0 & DLA_RESPONSE_MSG_MASK;

	error_status = (mailbox0 >> DLA_RESPONSE_ERROR_SHIFT) &
					DLA_RESPONSE_ERROR_MASK;

	/* Collect MNOC stat info for child */
	(void) nvdla_fw_update_response_stats_mnoc(pdev, error_status);

	/* handles engine timeout,
	 * schedule work for reset handler and clears interrupt
	 */
	if (message == DLA_MSG_TASK_TIMEOUT) {
		nvdla_dbg_err(pdev, "engine timeout detected");
		schedule_work(&nvdla_dev->reset_work);
		goto done;
	}
	if (message == DLA_MSG_DEBUG_PRINT) {
		nvdla_dbg_fw(pdev, "falcon: %s",
				(char *)nvdla_dev->debug_dump_va);
	}
	/* Handle CTX message */
	if ((message == DLA_MSG_CMD_COMPLETE ||
				message == DLA_MSG_CMD_ERROR)) {
		if (nvdla_dev->waiting)
			handle_cmd_completion(nvdla_dev, error_status);
		else if (parent_nvdla_dev->waiting)
			handle_cmd_completion(parent_nvdla_dev, error_status);
	}

	if (message == DLA_MSG_IDLE_TIMEOUT) {
		nvdla_dbg_info(pdev, "Idle notification detected");
		schedule_work(&nvdla_dev->poweroff_work);
	}

done:
	/* Clear the interrupt */
	(void) nvdla_fw_interrupt_stat_clear_mnoc(pdev);

	return 0;
}

static irqreturn_t s_riscv_isr(int irq, void *dev_id)
{
	struct platform_device *pdev = (struct platform_device *)(dev_id);
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);

	if (pdata->flcn_isr)
		pdata->flcn_isr(pdev);

	return IRQ_HANDLED;
}

static int32_t s_nvdla_fw_request_ctx_irq(struct riscv *riscv)
{
	int32_t err;
	int32_t irq_tx;
	struct platform_device *pdev = riscv->pdev;

	irq_tx = platform_get_irq_byname(pdev, "mnoctx");
	if (irq_tx < 0) {
		nvdla_dbg_err(pdev, "failed to get ctx TX IRQ\n");
		err = -ENXIO;
		goto fail;
	}

	err = devm_request_irq(&pdev->dev, irq_tx, s_riscv_isr, 0,
					dev_name(&pdev->dev), pdev);
	if (err) {
		dev_err(&pdev->dev, "failed to request ctx TX irq. err %d\n", err);
		goto fail;
	}

	riscv->irq[NVDLA_IRQ_TYPE_MNOC_TX_READY] = irq_tx;

	return 0;

fail:
	return err;
}

static int32_t s_nvdla_fw_request_default_irq(struct riscv *riscv)
{
	int32_t err;
	int32_t irq;
	struct platform_device *pdev = riscv->pdev;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		nvdla_dbg_err(pdev, "failed to get IRQ\n");
		err = -ENXIO;
		goto fail;
	}

	err = devm_request_irq(&pdev->dev, irq, s_riscv_isr, 0,
				   dev_name(&pdev->dev), pdev);
	if (err) {
		dev_err(&pdev->dev, "failed to request irq. err %d\n", err);
		goto fail;
	}

	riscv->irq[NVDLA_APERTURE_TYPE_DLA] = irq;

	return 0;

fail:
	return err;
}

int32_t nvdla_fw_init(struct platform_device *pdev)
{
	int err = 0;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdladev = pdata->private_data;
	struct riscv *riscv;
	int32_t i;

	// Device managed allocation
	riscv = devm_kzalloc(&pdev->dev, sizeof(*riscv), GFP_KERNEL);
	if (!riscv) {
		err = -ENOMEM;
		goto fail;
	}

	riscv->pdev = pdev;
	riscv->dev = &pdev->dev;
	riscv->nvdladev = nvdladev;
	pdata->falcon_data = riscv;

	// get the interrupt
	if (nvdla_is_ctx_device(pdev)) {
		err = s_nvdla_fw_request_ctx_irq(riscv);
		if (err < 0)
			goto free_riscv;
	} else {
		err = s_nvdla_fw_request_default_irq(riscv);
		if (err < 0)
			goto free_riscv;
	}

	// keep irq disabled that are valid.
	for (i = 0; i < NVDLA_IRQ_TYPE_MAX; i++) {
		if (riscv->irq[i] > 0)
			disable_irq(riscv->irq[i]);
	}

	return 0;

free_riscv:
	pdata->falcon_data = NULL;
	devm_kfree(&pdev->dev, riscv);
fail:
	return err;
}

void nvdla_fw_deinit(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct riscv *riscv = (struct riscv *) pdata->falcon_data;
	int32_t i;

	for (i = 0; i < NVDLA_IRQ_TYPE_MAX; i++) {
		if (riscv->irq[i] > 0) {
			devm_free_irq(&pdev->dev, riscv->irq[i], pdev);
			riscv->irq[i] = 0;
		}
	}

	if (riscv) {
		devm_kfree(&pdev->dev, riscv);
		pdata->falcon_data = NULL;
	}
}

int32_t nvdla_fw_reload(struct platform_device *pdev)
{
	(void) pdev;
	return -EOPNOTSUPP;
}

static int32_t s_riscv_wait_mnoc_receivembox_ready(struct riscv *riscv)
{
	int32_t err;
	uint32_t value;
	void __iomem *regs;

	regs = nvdla_device_register_get_aperture(riscv->pdev,
			NVDLA_APERTURE_TYPE_MNOCRX);
	if (!regs) {
		err = -EINVAL;
		goto fail;
	}

	err = readl_poll_timeout(regs + riscv_mnoc_message_info_0_receivembox_r(),
			value,
			(riscv_mnoc_message_info_0_receivembox_receive_ready_v(value) ==
				riscv_mnoc_message_info_0_receivembox_receive_ready_true_v()),
			10 /* sleep in us */,
			100000 /* timeout in us */);
	if (err < 0)
		goto fail;

	return 0;

fail:
	return err;
}

static int32_t s_riscv_wait_mnoc_receivembox_credit_available(
	struct riscv *riscv)
{
	int32_t err;
	uint32_t value;
	void __iomem *regs;

	regs = nvdla_device_register_get_aperture(riscv->pdev,
			NVDLA_APERTURE_TYPE_MNOCRX);
	if (!regs) {
		err = -EINVAL;
		goto fail;
	}

	err = readl_poll_timeout(regs + riscv_mnoc_message_info_0_receivembox_r(),
			value,
			(riscv_mnoc_message_info_0_receivembox_credit_available_v(value) ==
				riscv_mnoc_message_info_0_receivembox_credit_available_true_v()),
			10 /* sleep in us */,
			100000 /* timeout in us */);
	if (err < 0)
		goto fail;

	return 0;

fail:
	return err;
}

static int32_t s_fw_send_mnoc_cmd(struct platform_device *pdev,
	struct nvdla_cmd_data *cmd_data)
{
	int32_t err;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	struct riscv *riscv = (struct riscv *) pdata->falcon_data;
	uint32_t rxdata[2U] = {0U, 0U};
	uint32_t method_id = cmd_data->method_id;
	uint32_t method_data = cmd_data->method_data;
	bool wait = cmd_data->wait;
	bool context = nvdla_is_ctx_device(pdev);
	bool ctx_reply = nvdla_dev->response_info.response_mode;
	uint32_t ctx_id = nvdla_dev->ctx_id;
	unsigned long timeout;
	uint32_t message_size = DLA_MNOC_MSG_RX_SIZE_IN_BYTES;
	int32_t i;

	WARN_ON(DLA_MNOC_MSG_RX_SIZE_IN_BYTES / sizeof(uint32_t) != 2U);

	if (!nvdla_is_ctx_device(pdev)) {
		err = -EINVAL;
		goto fail;
	}

	mutex_lock(&nvdla_dev->cmd_lock);

	// Wait for the ready bit to be set.
	err = s_riscv_wait_mnoc_receivembox_ready(riscv);
	if (err < 0) {
		nvdla_dbg_err(pdev, "MNOC not ready.\n");
		goto unlock_cmd;
	}

	// Prepare the data and update the mnoc rx registers.
	nvdla_dev->waiting = 1;

	rxdata[0] = dla_ctx_submit_cmd(method_id, wait, wait, ctx_reply,
					ctx_id, context);
	rxdata[1] = method_data;

	nvdla_dbg_reg(pdev, "cmd_id=[0x%x]", rxdata[0]);
	nvdla_dbg_reg(pdev, "cmd_data=[0x%x]", rxdata[1]);

	// Set size and trigger bit for the MNOC
	nvdla_device_register_write(pdev, NVDLA_APERTURE_TYPE_MNOCRX,
		riscv_mnoc_message_info_0_receivembox_r(),
		riscv_mnoc_message_info_0_receivembox_message_size_f(message_size) |
			riscv_mnoc_message_info_0_receivembox_message_trigger_true_f());

	for (i = 0; i < 2U; i++) {
		// Wait for credit availability
		err = s_riscv_wait_mnoc_receivembox_credit_available(riscv);
		if (err < 0) {
			nvdla_dbg_err(pdev, "Credit unavailable.\n");
			goto reset_waiting_status;
		}

		// Update the data.
		nvdla_device_register_write(pdev, NVDLA_APERTURE_TYPE_MNOCRX,
			riscv_mnoc_wdata_0_receivembox_r(), rxdata[i]);
	}

	if (!wait)
		goto reset_waiting_status;

	timeout = msecs_to_jiffies(CMD_TIMEOUT_MSEC);

	if (!wait_for_completion_timeout(&nvdla_dev->cmd_completion, timeout)) {
		nvdla_dbg_err(pdev, "Command %x timedout\n",
			rxdata[0]);
		err = -ETIMEDOUT;
		goto reset_waiting_status;
	}

	if (nvdla_dev->cmd_status != DLA_ERR_NONE) {
		nvdla_dbg_err(pdev, "Command %x failed (status:%x)\n",
			method_id, nvdla_dev->cmd_status);
		err = -EINVAL;
		goto reset_cmd_status;
	}

	nvdla_dev->cmd_status = DLA_ERR_NONE;
	nvdla_dev->waiting = 0;
	mutex_unlock(&nvdla_dev->cmd_lock);

	return 0;

reset_cmd_status:
	nvdla_dev->cmd_status = DLA_ERR_NONE;
reset_waiting_status:
	nvdla_dev->waiting = 0;
unlock_cmd:
	mutex_unlock(&nvdla_dev->cmd_lock);
fail:
	return err;
}

static int32_t s_fw_send_mthd_cmd(struct platform_device *pdev,
	struct nvdla_cmd_data *cmd_data,
	bool context,
	bool ctx_reply,
	uint32_t ctx_id)
{
	int ret;
	unsigned long timeout;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct riscv *riscv = (struct riscv *) pdata->falcon_data;
	struct nvdla_device *nvdla_dev = pdata->private_data;
	uint32_t method_id = cmd_data->method_id;
	uint32_t method_data = cmd_data->method_data;
	bool wait = cmd_data->wait;

	mutex_lock(&nvdla_dev->cmd_lock);

	/**
	 * If device is unavailable, then error out to retry after some time.
	 **/
	if (!nvdla_dev->available) {
		nvdla_dbg_err(pdev, "Command failed: device unavailable\n");
		mutex_unlock(&nvdla_dev->cmd_lock);
		ret = -EAGAIN;
		goto fail;
	}

	/*
	 * enable notification for command completion or error if
	 * wait if required
	 */
	method_id = dla_ctx_submit_cmd(method_id, wait, wait, ctx_reply,
					ctx_id, context);

	ret = s_riscv_wait_mthdid_idle(riscv);
	if (ret < 0) {
		nvdla_dbg_err(pdev, "mthdid timed out\n");
		mutex_unlock(&nvdla_dev->cmd_lock);
		goto fail;
	}

	nvdla_dev->waiting = 1;

	nvdla_dbg_reg(pdev, "method_data=[0x%x]", method_data);
	nvdla_device_register_write(pdev, NVDLA_APERTURE_TYPE_DLA,
		riscv_mthdwdat_r(), method_data);

	nvdla_dbg_reg(pdev, "method_id=[0x%x]", method_id);
	nvdla_device_register_write(pdev, NVDLA_APERTURE_TYPE_DLA,
		riscv_mthdid_r(), method_id);

	if (!wait)
		goto reset_waiting_status;

	timeout = msecs_to_jiffies(CMD_TIMEOUT_MSEC);

	if (!wait_for_completion_timeout(&nvdla_dev->cmd_completion, timeout)) {
		nvdla_dbg_err(pdev, "Command %x timedout\n",
			method_id);
		ret = -ETIMEDOUT;
		goto unlock_cmd;
	}

	if (nvdla_dev->cmd_status != DLA_ERR_NONE) {
		nvdla_dbg_err(pdev, "Command %x failed (status:%x)\n",
			method_id, nvdla_dev->cmd_status);
		ret = -EINVAL;
		goto reset_cmd_status;
	}

reset_cmd_status:
	/* Reset command status after use for next command */
	nvdla_dev->cmd_status = DLA_ERR_NONE;
unlock_cmd:
	mutex_unlock(&nvdla_dev->cmd_lock);
reset_waiting_status:
	nvdla_dev->waiting = 0;
fail:
	return ret;
}

static void s_fw_update_submit_stat(struct platform_device *pdev,
	int32_t err)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	uint32_t submit_mode = nvdla_dev->submit_mode;
	struct nvdla_submit_stat *submit_stat;

	submit_stat = &nvdla_dev->submit_stat[submit_mode];

	if (err < 0) {
		if (err == -ETIMEDOUT)
			submit_stat->num_timeouts++;
		else
			submit_stat->num_fails++;

		submit_stat->last_error = err;
	} else {
		submit_stat->num_successes++;
		submit_stat->last_error = 0;
	}

	submit_stat->timestamp_ns = arch_timer_read_counter();
}

int32_t nvdla_fw_send_cmd(struct platform_device *pdev,
	struct nvdla_cmd_data *cmd_data)
{
	int err;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	uint32_t submit_mode = nvdla_dev->submit_mode;
	struct platform_device *parent_pdev;

	if (nvdla_is_ctx_device(pdev)) {
		if (submit_mode == NVDLA_SUBMIT_MODE_MMIO) {
			parent_pdev = nvdla_dev->parent_pdev;

			// Request through the parent device.
			err = s_fw_send_mthd_cmd(parent_pdev, cmd_data,
					true /* context */,
					nvdla_dev->response_info.response_mode,
					nvdla_dev->ctx_id);
			if (err < 0)
				goto fail;
		} else if (submit_mode == NVDLA_SUBMIT_MODE_MNOC) {
			err = s_fw_send_mnoc_cmd(pdev, cmd_data);
			if (err < 0)
				goto fail;
		} else {
			nvdla_dbg_err(pdev, "Unsupported submit mode: %u\n",
				submit_mode);
			err = -EINVAL;
			goto fail;
		}

	} else {
		err = s_fw_send_mthd_cmd(pdev, cmd_data,
				false /* context */,
				false /* ctx_reply */,
				nvdla_dev->ctx_id);
		if (err < 0)
			goto fail;
	}

	// Update the submit stats.
	s_fw_update_submit_stat(pdev, 0 /* success */);

	return 0;

fail:
	// Update the submit stats even with error.
	s_fw_update_submit_stat(pdev, err);
	return err;
}

int32_t nvdla_fw_send_ack(struct platform_device *pdev,
	int32_t ack)
{
	int32_t err = 0;

	if (pdev == NULL) {
		err = -EINVAL;
		goto fail;
	}

	nvdla_device_register_write(pdev, NVDLA_APERTURE_TYPE_DLA,
		riscv_mailbox0_r(), ack);

	return 0;

fail:
	return err;
}

int32_t nvdla_fw_interrupt_stat_read(struct platform_device *pdev,
	int32_t *stat)
{
	int32_t err = 0;

	if (pdev == NULL) {
		err = -EINVAL;
		goto fail;
	}

	if (stat == NULL) {
		nvdla_dbg_err(pdev, "NULL stat\n");
		err = -EINVAL;
		goto fail;
	}

	/* TODO: Check for spurious interrupts. */
	*stat = nvdla_device_register_read(pdev, NVDLA_APERTURE_TYPE_DLA,
				riscv_mailbox0_r());
	return 0;

fail:
	return err;
}

int32_t nvdla_fw_interrupt_stat_read_mnoc(struct platform_device *pdev,
	int32_t *stat)
{
	int32_t err = 0;
	int32_t msg_info = 0;
	int32_t msg_size = 0;
	int32_t unused;

	if (pdev == NULL) {
		err = -EINVAL;
		goto fail;
	}

	if (stat == NULL) {
		nvdla_dbg_err(pdev, "NULL stat\n");
		err = -EINVAL;
		goto fail;
	}

	/* Get tx message size */
	msg_info = nvdla_device_register_read(pdev, NVDLA_APERTURE_TYPE_MNOCTX,
			riscv_mnoc_message_info_r());
	msg_size = riscv_mnoc_message_info_size_v(msg_info);

	if (msg_size > DLA_MAX_MNOC_MESSAGE_SIZE) {
		nvdla_dbg_err(pdev, "Invalid Message size: %d\n", msg_size);
		err = -EINVAL;
		goto fail;
	}

	/* Read twice to clear the interrupt */
	*stat = nvdla_device_register_read(pdev, NVDLA_APERTURE_TYPE_MNOCTX,
			riscv_mnoc_rdata_r());

	unused = nvdla_device_register_read(pdev, NVDLA_APERTURE_TYPE_MNOCTX,
			riscv_mnoc_rdata_r());

	(void) unused;
	return 0;

fail:
	return err;
}

int32_t nvdla_fw_interrupt_stat_clear(struct platform_device *pdev)
{
	int32_t err = 0;

	if (pdev == NULL) {
		err = -EINVAL;
		goto fail;
	}

	nvdla_device_register_write(pdev, NVDLA_APERTURE_TYPE_DLA,
		riscv_irqmclr_r(), riscv_irqmclr_swgen1_set_f());
	nvdla_device_register_write(pdev, NVDLA_APERTURE_TYPE_DLA,
		riscv_irqsclr_r(),
		riscv_irqsclr_swgen1_set_f());

fail:
	return err;
}

int32_t nvdla_fw_interrupt_stat_clear_mnoc(struct platform_device *pdev)
{
	int32_t err = 0;
	int32_t val = 0;

	if (pdev == NULL) {
		err = -EINVAL;
		goto fail;
	}

	val = nvdla_device_register_read(pdev,
				NVDLA_APERTURE_TYPE_MNOCTX,
				riscv_mnoc_message_info_r());

	val |= riscv_mnoc_message_info_intr_status_w1clr_f();

	nvdla_device_register_write(pdev, NVDLA_APERTURE_TYPE_MNOCTX,
		riscv_mnoc_message_info_r(), val);

fail:
	return err;
}

int32_t nvdla_fw_inject_corrected_error(struct platform_device *pdev)
{
	(void) pdev;

	return -EOPNOTSUPP;
}

int32_t nvdla_fw_inject_uncorrected_error(struct platform_device *pdev)
{
	(void) pdev;

	return -EOPNOTSUPP;
}

int32_t nvdla_fw_update_response_stats(struct platform_device *pdev,
	uint32_t error_status)
{
	int32_t err = 0;
	struct nvhost_device_data *pdata;
	struct nvdla_device *nvdla_dev;

	if (pdev == NULL) {
		err = -EINVAL;
		goto fail;
	}

	pdata = platform_get_drvdata(pdev);
	nvdla_dev = pdata->private_data;

	nvdla_dev->response_info.num_interrupts_swgen++;
	nvdla_dev->response_info.last_err_status_swgen = error_status;
	nvdla_dev->response_info.last_err_timestamp_swgen = arch_timer_read_counter();

	return 0;

fail:
	return err;
}

int32_t nvdla_fw_update_response_stats_mnoc(struct platform_device *pdev,
	uint32_t error_status)
{
	int32_t err = 0;
	struct nvhost_device_data *pdata;
	struct nvdla_device *nvdla_dev;

	if (pdev == NULL) {
		err = -EINVAL;
		goto fail;
	}

	pdata = platform_get_drvdata(pdev);
	nvdla_dev = pdata->private_data;

	nvdla_dev->response_info.num_interrupts_mnoc++;
	nvdla_dev->response_info.last_err_status_mnoc = error_status;
	nvdla_dev->response_info.last_err_timestamp_mnoc = arch_timer_read_counter();

	return 0;

fail:
	return err;
}
