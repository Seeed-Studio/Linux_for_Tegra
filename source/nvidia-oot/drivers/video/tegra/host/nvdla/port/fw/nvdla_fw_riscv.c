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

struct riscv {
	struct device *dev;
	struct nvdla_device *nvdladev;
	void __iomem *regs;

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

static void s_riscv_write(struct riscv *riscv, uint32_t value, uint32_t offset)
{
	writel(value, riscv->regs + offset);
}

static uint32_t s_riscv_read(struct riscv *riscv, uint32_t offset)
{
	return readl(riscv->regs + offset);
}

static int32_t s_riscv_wait_mthdid_idle(struct riscv *riscv)
{
	uint32_t value;

	return readl_poll_timeout(riscv->regs + riscv_mthdid_r(),
			value,
			(riscv_mthdid_wpend_v(value) == riscv_mthdid_wpend_done_v()),
			10 /* sleep in us */,
			100000 /* timeout in us*/);
}

static int32_t s_riscv_wait_idle(struct riscv *riscv)
{
	uint32_t value;

	return readl_poll_timeout(riscv->regs + riscv_idlestate_r(),
			value,
			(value == 0),
			10 /* sleep in us */,
			100000 /* timeout in us*/);
}

static int32_t s_riscv_dmactl_wait_ready(struct riscv *riscv)
{
	uint32_t value;

	return readl_poll_timeout(riscv->regs + riscv_dmactl_r(),
			value,
			(value & (riscv_dmactl_imem_scrubbing_m() |
				riscv_dmactl_dmem_scrubbing_m())) == 0,
			10 /* sleep in us */,
			100000 /* timeout in us*/);
}

static int32_t s_riscv_dma_wait_idle(struct riscv *riscv)
{
	uint32_t value;

	return readl_poll_timeout(riscv->regs + riscv_dmatrfcmd_r(),
			value,
			(riscv_dmatrfcmd_idle_v(value) ==
			 riscv_dmatrfcmd_idle_true_v()),
			10 /* sleep in us */,
			100000 /* timeout in us*/);
}

static int32_t s_riscv_copy_dmem_size256(struct riscv *riscv,
	uint32_t offset)
{
	uint32_t cmd = (riscv_dmatrfcmd_size_256b_f() |
		riscv_dmatrfcmd_ctxdma_f(riscv_transcfg_falc_swid_v()));
	phys_addr_t base = riscv->ucode_data_offset + offset;

	s_riscv_write(riscv, offset, riscv_dmatrfmoffs_r());
	s_riscv_write(riscv, base, riscv_dmatrffboffs_r());
	s_riscv_write(riscv, cmd, riscv_dmatrfcmd_r());

	return s_riscv_dma_wait_idle(riscv);
}

static int32_t s_riscv_copy_imem_size256(struct riscv *riscv,
			     unsigned long offset)
{
	uint32_t cmd = (riscv_dmatrfcmd_size_256b_f() |
		riscv_dmatrfcmd_ctxdma_f(riscv_transcfg_falc_swid_v()) |
		riscv_dmatrfcmd_imem_true_f());
	phys_addr_t base = riscv->ucode_code_offset + offset;

	s_riscv_write(riscv, offset, riscv_dmatrfmoffs_r());
	s_riscv_write(riscv, base, riscv_dmatrffboffs_r());
	s_riscv_write(riscv, cmd, riscv_dmatrfcmd_r());

	return s_riscv_dma_wait_idle(riscv);
}

static int32_t s_riscv_load_mem_bitbang(struct riscv *riscv)
{
	uint32_t imem_offset = riscv->ucode_offset + riscv->ucode_code_offset;
	uint32_t *imem_cpuva = (uint32_t *) (riscv->cpuva + imem_offset);
	uint32_t dmem_offset = riscv->ucode_offset + riscv->ucode_data_offset;
	uint32_t *dmem_cpuva = (uint32_t *) (riscv->cpuva + dmem_offset);
	uint32_t i;

	s_riscv_write(riscv, riscv_imemc_aincw_true_f(), riscv_imemc_r(0U));
	for (i = 0; i < riscv->ucode_code_size / sizeof(uint32_t); i++)
		s_riscv_write(riscv, imem_cpuva[i], riscv_imemd_r(0U));
	s_riscv_write(riscv, 0U, riscv_imemc_r(0U));

	s_riscv_write(riscv, riscv_dmemc_aincw_true_f(), riscv_dmemc_r(0U));
	for (i = 0; i < riscv->ucode_data_size / sizeof(uint32_t); i++)
		s_riscv_write(riscv, dmem_cpuva[i], riscv_dmemd_r(0U));
	s_riscv_write(riscv, 0U, riscv_dmemc_r(0U));

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

	s_riscv_write(riscv, 0, riscv_dmactl_r());
	s_riscv_write(riscv, (riscv->iova + riscv->ucode_offset) >> 8,
			riscv_dmatrfbase_r());

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
	boot_vector_hi = s_riscv_read(riscv, riscv_boot_vector_hi_r());
	if (boot_vector_hi != 0) {
		dev_err(riscv->dev, "Invalid boot vector high address: %u\n",
			boot_vector_hi);
		err = -EINVAL;
		goto fail;
	}

	s_riscv_write(riscv, DLA_BOOTVECTOR_LO, riscv_boot_vector_lo_r());
	s_riscv_write(riscv, riscv_bcr_ctrl_core_select_riscv_f(),
		riscv_bcr_ctrl_r());

	bcr_ctrl = s_riscv_read(riscv, riscv_bcr_ctrl_r());
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
	s_riscv_write(riscv,
		riscv_itfen_ctxen_enable_f() | riscv_itfen_mthden_enable_f(),
		riscv_itfen_r());

	/* boot riscv */
	s_riscv_write(riscv, riscv_cpuctl_startcpu_true_f(), riscv_cpuctl_r());

	return 0;
fail:
	return err;
}

static int32_t s_riscv_finalize_poweron(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct riscv *riscv = (struct riscv *) pdata->falcon_data;
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

		bcr_ctrl = s_riscv_read(riscv, riscv_bcr_ctrl_r());
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
		nvdla_device_register_write(pdev, pdata->transcfg_addr,
			pdata->transcfg_val);

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

	if (pdata->flcn_isr)
		enable_irq(pdata->irq);

	return 0;
unload_firmware:
	s_riscv_unload_firmware(riscv);
fail:
	return err;
}

static void s_riscv_prepare_poweroff(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct riscv *riscv = (struct riscv *) pdata->falcon_data;

	if (pdata->flcn_isr)
		disable_irq(pdata->irq);

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

	fw_ver_read_bin = nvdla_device_register_read(pdev, riscv_os_version_r());

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

static irqreturn_t s_riscv_isr(int irq, void *dev_id)
{
	struct platform_device *pdev = (struct platform_device *)(dev_id);
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);

	if (pdata->flcn_isr)
		pdata->flcn_isr(pdev);

	return IRQ_HANDLED;
}

int32_t nvdla_fw_init(struct platform_device *pdev)
{
	int err = 0;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdladev = pdata->private_data;
	struct riscv *riscv;

	pdata->irq = platform_get_irq(pdev, 0);
	if (pdata->irq < 0) {
		nvdla_dbg_err(pdev, "failed to get IRQ\n");
		err = -ENXIO;
		goto fail;
	}

	err = devm_request_irq(&pdev->dev, pdata->irq, s_riscv_isr, 0,
			       dev_name(&pdev->dev), pdev);
	if (err) {
		dev_err(&pdev->dev, "failed to request irq. err %d\n", err);
		goto fail;
	}

	/* keep irq disabled */
	disable_irq(pdata->irq);

	/* Device managed allocation */
	riscv = devm_kzalloc(&pdev->dev, sizeof(*riscv), GFP_KERNEL);
	if (!riscv) {
		err = -ENOMEM;
		goto free_irq;
	}

	riscv->dev = &pdev->dev;
	riscv->nvdladev = nvdladev;
	riscv->regs = pdata->aperture[0];

	pdata->falcon_data = riscv;

	return 0;

free_irq:
	devm_free_irq(&pdev->dev, pdata->irq, pdev);
fail:
	return err;
}

void nvdla_fw_deinit(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct riscv *riscv = (struct riscv *) pdata->falcon_data;

	if (riscv) {
		devm_kfree(&pdev->dev, riscv);
		pdata->falcon_data = NULL;
	}

	devm_free_irq(&pdev->dev, pdata->irq, pdev);
}

int32_t nvdla_fw_reload(struct platform_device *pdev)
{
	(void) pdev;
	return -EOPNOTSUPP;
}

int32_t nvdla_fw_send_cmd(struct platform_device *pdev,
	struct nvdla_cmd_data *cmd_data)
{
	unsigned long timeout;
	int ret = 0;
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
	if (wait)
		method_id |= (1 << DLA_INT_ON_COMPLETE_SHIFT) |
					(1 << DLA_INT_ON_ERROR_SHIFT);

	ret = s_riscv_wait_mthdid_idle(riscv);
	if (ret < 0) {
		nvdla_dbg_err(pdev, "mthdid timed out\n");
		mutex_unlock(&nvdla_dev->cmd_lock);
		goto fail;
	}

	nvdla_dev->waiting = 1;

	nvdla_dbg_reg(pdev, "method_data=[0x%x]", method_data);
	nvdla_device_register_write(pdev, riscv_mthdwdat_r(), method_data);

	nvdla_dbg_reg(pdev, "method_id=[0x%x]", method_id);
	nvdla_device_register_write(pdev, riscv_mthdid_r(), method_id);

	if (!wait)
		goto reset_waiting_status;

	timeout = msecs_to_jiffies(CMD_TIMEOUT_MSEC);

	if (!wait_for_completion_timeout(&nvdla_dev->cmd_completion, timeout)) {
		nvdla_dbg_err(pdev, "Command %u timedout\n",
			method_id);
		ret = -ETIMEDOUT;
		goto unlock_cmd;
	}

	if (nvdla_dev->cmd_status != DLA_ERR_NONE) {
		nvdla_dbg_err(pdev, "Command %u failed (status:%x)\n",
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

int32_t nvdla_fw_send_ack(struct platform_device *pdev,
	int32_t ack)
{
	int32_t err = 0;

	if (pdev == NULL) {
		err = -EINVAL;
		goto fail;
	}

	nvdla_device_register_write(pdev, riscv_mailbox0_r(), ack);

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
	*stat = nvdla_device_register_read(pdev, riscv_mailbox0_r());
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

	nvdla_device_register_write(pdev, riscv_irqmclr_r(),
		riscv_irqmclr_swgen1_set_f());
	nvdla_device_register_write(pdev, riscv_irqsclr_r(),
		riscv_irqsclr_swgen1_set_f());

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
