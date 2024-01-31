// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2023, NVIDIA Corporation.  All rights reserved.
 *
 * NVDLA driver for T194/T23x
 */

#include <linux/arm64-barrier.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/nvmem-consumer.h>
#include <linux/pm_runtime.h>
#include <soc/tegra/fuse-helper.h>
#include <soc/tegra/fuse.h>
#include <uapi/linux/nvhost_nvdla_ioctl.h>
#include <uapi/linux/tegra-soc-hwpm-uapi.h>

#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
#include <linux/tegra-hsierrrptinj.h>
#endif /* CONFIG_TEGRA_HSIERRRPTINJ */

#if !IS_ENABLED(CONFIG_TEGRA_GRHOST)
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#endif

#include "nvdla.h"
#include "nvdla_hw_flcn.h"
#include "nvdla_t194.h"
#include "nvdla_t234.h"
#include "dla_queue.h"
#include "nvdla_buffer.h"
#include "nvdla_debug.h"
#include "dla_os_interface.h"

#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
int nvdla_error_inj_handler(unsigned int instance_id,
	struct epl_error_report_frame frame,
	void *data)
{
	int err = 0;

	struct nvdla_device *nvdla_dev = (struct nvdla_device *) data;
	struct platform_device *pdev = nvdla_dev->pdev;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	unsigned int device_instance_id;
	unsigned int device_ue_reporter_id;
	unsigned int device_ce_reporter_id;
	unsigned int device_ue_error_code;
	unsigned int device_ce_error_code;

	if (pdata->class == NV_DLA0_CLASS_ID) {
		device_instance_id = 0U;
		device_ue_reporter_id = NVDLA0_UE_HSM_REPORTER_ID;
		device_ce_reporter_id = NVDLA0_CE_HSM_REPORTER_ID;
		device_ue_error_code = NVDLA0_UE_HSM_ERROR_CODE;
		device_ce_error_code = NVDLA0_CE_HSM_ERROR_CODE;
	} else {
		device_instance_id = 1U;
		device_ue_reporter_id = NVDLA1_UE_HSM_REPORTER_ID;
		device_ce_reporter_id = NVDLA1_CE_HSM_REPORTER_ID;
		device_ue_error_code = NVDLA1_UE_HSM_ERROR_CODE;
		device_ce_error_code = NVDLA1_CE_HSM_ERROR_CODE;
	}

	if (device_instance_id != instance_id) {
		nvdla_dbg_err(pdev, "Invalid instance ID: %u", instance_id);
		err = -EINVAL;
		goto fail;
	}

	err = nvhost_module_busy(pdev);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to power on\n");
		err = -ENODEV;
		goto fail;
	}

	if ((frame.reporter_id == device_ue_reporter_id) &&
		 (frame.error_code == device_ue_error_code)) {
		/* Inject uncorrected error. */
		host1x_writel(pdev, flcn_safety_erb_r(),
			flcn_safety_erb_data_uncorrected_err_v());
	} else if ((frame.reporter_id == device_ce_reporter_id) &&
		 (frame.error_code == device_ce_error_code)) {
		/* Inject corrected error. */
		host1x_writel(pdev, flcn_safety_erb_r(),
			flcn_safety_erb_data_corrected_err_v());
	} else {
		nvdla_dbg_err(pdev, "Invalid Reported ID: %x, Error Code: %x",
			frame.reporter_id, frame.error_code);
		err = -EINVAL;
	}

	nvhost_module_idle(pdev);

fail:
	return err;
}

static int nvdla_error_inj_handler_init(struct nvdla_device *nvdla_dev)
{
	struct platform_device *pdev = nvdla_dev->pdev;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	unsigned int instance_id;

	if (pdata->class == NV_DLA0_CLASS_ID)
		instance_id = 0U;
	else
		instance_id = 1U;

	return hsierrrpt_reg_cb(IP_DLA, instance_id,
				nvdla_error_inj_handler,
				(void *) nvdla_dev);
}

static void nvdla_error_inj_handler_deinit(struct nvdla_device *nvdla_dev)
{
	struct platform_device *pdev = nvdla_dev->pdev;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	unsigned int instance_id;

	if (pdata->class == NV_DLA0_CLASS_ID)
		instance_id = 0U;
	else
		instance_id = 1U;

	hsierrrpt_dereg_cb(IP_DLA, instance_id);
}
#endif /* CONFIG_TEGRA_HSIERRRPTINJ */

/*
 * Work to handle engine reset for error recovery
 */
static void nvdla_reset_handler(struct work_struct *work)
{
	struct nvdla_device *nvdla_dev = container_of(work,
					struct nvdla_device, reset_work);

	struct platform_device *pdev = nvdla_dev->pdev;

	/* reset engine */
	nvhost_module_reset(pdev, true);

	nvdla_dbg_info(pdev, "Engine reset done\n");
}

static void nvdla_reset_handler_init(struct nvdla_device *nvdla_dev)
{
	INIT_WORK(&nvdla_dev->reset_work, nvdla_reset_handler);
}

int nvhost_nvdla_flcn_isr(struct platform_device *pdev)
{
	uint32_t message;
	uint32_t mailbox0;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;

	/* dump falcon data if debug enabled */
	mailbox0 = host1x_readl(pdev, flcn_mailbox0_r());

	message = mailbox0 & DLA_RESPONSE_MSG_MASK;

	/* handles engine timeout,
	 * schedule work for reset handler and clears interrupt
	 */
	if (message == DLA_MSG_TASK_TIMEOUT) {
		nvdla_dbg_err(pdev, "engine timeout detected");
		schedule_work(&nvdla_dev->reset_work);
		goto clear_interrupt;
	}
	if (message == DLA_MSG_DEBUG_PRINT)
		nvdla_dbg_fw(pdev, "falcon: %s",
				(char *)nvdla_dev->debug_dump_va);

	if ((message == DLA_MSG_CMD_COMPLETE ||
				message == DLA_MSG_CMD_ERROR) &&
				nvdla_dev->waiting) {
		nvdla_dev->cmd_status =
				(mailbox0 >> DLA_RESPONSE_ERROR_SHIFT) &
						DLA_RESPONSE_ERROR_MASK;
		nvdla_dev->waiting = 0;
		complete(&nvdla_dev->cmd_completion);
	}

clear_interrupt:
	/* logic to clear the interrupt */
	host1x_writel(pdev, flcn_irqmclr_r(), flcn_irqmclr_swgen1_set_f());
	host1x_writel(pdev, flcn_thi_int_stat_r(), flcn_thi_int_stat_clr_f());
	host1x_readl(pdev, flcn_thi_int_stat_r());
	host1x_writel(pdev, flcn_irqsclr_r(), flcn_irqsclr_swgen1_set_f());
	/* Notify FW that interuppt handling is complete */
	host1x_writel(pdev, flcn_mailbox0_r(), DLA_MSG_INTERRUPT_HANDLING_COMPLETE);

	return 0;
}

/* Helper API's */
static int nvdla_alloc_cmd_memory(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	int err = 0;

	/* allocate memory for command */
	nvdla_dev->cmd_mem.va = dma_alloc_attrs(&pdev->dev,
			MAX_CMD_SIZE * MAX_COMMANDS_PER_DEVICE,
			&nvdla_dev->cmd_mem.pa, GFP_KERNEL,
			0);

	if (nvdla_dev->cmd_mem.va == NULL) {
		err = -ENOMEM;
		goto err_alloc_cmd_mem;
	}

	mutex_init(&nvdla_dev->cmd_mem.lock);
	nvdla_dev->cmd_mem.alloc_table = 0;

err_alloc_cmd_mem:
	return err;
}

static int nvdla_free_cmd_memory(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;

	/* free memory for command */
	dma_free_attrs(&pdev->dev,
			MAX_CMD_SIZE * MAX_COMMANDS_PER_DEVICE,
			nvdla_dev->cmd_mem.va, nvdla_dev->cmd_mem.pa,
			0);

	nvdla_dev->cmd_mem.alloc_table = 0;

	return 0;
}

int nvdla_get_cmd_memory(struct platform_device *pdev,
		struct nvdla_cmd_mem_info *cmd_mem_info)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	int err = 0, index, offset;

	mutex_lock(&nvdla_dev->cmd_mem.lock);

	index = find_first_zero_bit(&nvdla_dev->cmd_mem.alloc_table,
			MAX_COMMANDS_PER_DEVICE);
	if (index >= MAX_COMMANDS_PER_DEVICE) {
		nvdla_dbg_err(pdev, "failed to get cmd mem from pool\n");
		err = -EAGAIN;
		goto err_get_mem;
	}

	/* assign mem */
	set_bit(index, &nvdla_dev->cmd_mem.alloc_table);

	offset = NVDLA_CMD_OFFSET(index);
	cmd_mem_info->va = nvdla_dev->cmd_mem.va + offset;
	cmd_mem_info->pa = nvdla_dev->cmd_mem.pa + offset;
	cmd_mem_info->index = index;

	/* check if IOVA is correctly aligned */
	if (cmd_mem_info->pa & 0xff) {
		err = -EFAULT;
		goto fail_to_aligned_dma;
	}
	memset(cmd_mem_info->va, 0, MAX_CMD_SIZE);

fail_to_aligned_dma:
err_get_mem:
	mutex_unlock(&nvdla_dev->cmd_mem.lock);
	return err;
}

int nvdla_put_cmd_memory(struct platform_device *pdev, int index)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;

	mutex_lock(&nvdla_dev->cmd_mem.lock);
	clear_bit(index, &nvdla_dev->cmd_mem.alloc_table);
	mutex_unlock(&nvdla_dev->cmd_mem.lock);

	return 0;
}

int nvdla_send_cmd(struct platform_device *pdev,
			struct nvdla_cmd_data *cmd_data)
{
	unsigned long timeout;
	int ret = 0;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	uint32_t method_id = cmd_data->method_id;
	uint32_t method_data = cmd_data->method_data;
	bool wait = cmd_data->wait;

	mutex_lock(&nvdla_dev->cmd_lock);

	/*
	 * enable notification for command completion or error if
	 * wait if required
	 */
	if (wait)
		method_id |= (1 << DLA_INT_ON_COMPLETE_SHIFT) |
					(1 << DLA_INT_ON_ERROR_SHIFT);

	nvdla_dev->waiting = 1;

	nvdla_dbg_reg(pdev, "method_id=[0x%x]", method_id);
	host1x_writel(pdev, NV_DLA_THI_METHOD_ID, method_id);

	nvdla_dbg_reg(pdev, "method_data=[0x%x]", method_data);
	host1x_writel(pdev, NV_DLA_THI_METHOD_DATA, method_data);

	if (!wait) {
		nvdla_dev->waiting = 0;
		mutex_unlock(&nvdla_dev->cmd_lock);
		return 0;
	}

	timeout = msecs_to_jiffies(CMD_TIMEOUT_MSEC);

	if (!wait_for_completion_timeout(&nvdla_dev->cmd_completion, timeout)) {
		nvdla_dev->waiting = 0;
		mutex_unlock(&nvdla_dev->cmd_lock);
		return -ETIMEDOUT;
	}

	if (nvdla_dev->cmd_status != DLA_ERR_NONE) {
		nvdla_dbg_err(pdev, "Command %u failed\n", method_id);
		ret = -EINVAL;
	}

	/* Reset command status after use for next command */
	nvdla_dev->cmd_status = DLA_ERR_NONE;
	nvdla_dev->waiting = 0;

	mutex_unlock(&nvdla_dev->cmd_lock);

	return ret;
}

static int nvdla_set_gcov_region(struct platform_device *pdev, bool unset_region)
{
	int err = 0;
	struct nvdla_cmd_mem_info gcov_cmd_mem_info;
	struct nvdla_cmd_data cmd_data;
	struct dla_region_printf *gcov_region = NULL;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;

	if (!pdata->flcn_isr)
		return 0;

	err = nvhost_module_busy(pdev);
	if (err) {
		nvdla_dbg_err(pdev, "failed to power on\n");
		err = -ENODEV;
		goto fail_to_power_on;
	}

	/* assign memory for gcov command */
	err = nvdla_get_cmd_memory(pdev, &gcov_cmd_mem_info);
	if (err) {
		nvdla_dbg_err(pdev,
			"dma allocation failed for gcov command.");
		goto alloc_gcov_cmd_failed;
	}

	gcov_region = (struct dla_region_printf *)(gcov_cmd_mem_info.va);
	gcov_region->region = DLA_REGION_GCOV;
	if (nvdla_dev->submit_mode == NVDLA_SUBMIT_MODE_CHANNEL
		|| unset_region)
		gcov_region->address = 0;
	else
		gcov_region->address = nvdla_dev->gcov_dump_pa;
	gcov_region->size = GCOV_BUFFER_SIZE;

	cmd_data.method_id = DLA_CMD_SET_REGIONS;
	cmd_data.method_data = ALIGNED_DMA(gcov_cmd_mem_info.pa);
	cmd_data.wait = true;

	err = nvdla_send_cmd(pdev, &cmd_data);

	/* release memory allocated for gcov command */
	nvdla_put_cmd_memory(pdev, gcov_cmd_mem_info.index);

	if (err != 0) {
		nvdla_dbg_err(pdev, "failed to send gcov command");
		goto gcov_send_cmd_failed;
	}

	nvhost_module_idle(pdev);

	return err;

gcov_send_cmd_failed:
alloc_gcov_cmd_failed:
	nvhost_module_idle(pdev);
fail_to_power_on:
	return err;
}

int nvdla_free_gcov_region(struct platform_device *pdev, bool update_region)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	int ret = 0;

	if (update_region) {
		ret = nvdla_set_gcov_region(pdev, true);
		if (ret)
			return ret;
	}

	if (nvdla_dev->gcov_dump_pa) {
		dma_free_attrs(&pdev->dev, GCOV_BUFFER_SIZE,
			       nvdla_dev->gcov_dump_va,
			       nvdla_dev->gcov_dump_pa,
			       0);
		nvdla_dev->gcov_dump_va = NULL;
		nvdla_dev->gcov_dump_pa = 0;
	}

	return 0;
}

int nvdla_alloc_gcov_region(struct platform_device *pdev)
{
	int err = 0;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;

	/* Gcov buffer allocation must be done at once only. */
	if (!nvdla_dev->gcov_dump_va) {
		/* allocate gcov region */
		nvdla_dev->gcov_dump_va = dma_alloc_attrs(&pdev->dev,
				   GCOV_BUFFER_SIZE, &nvdla_dev->gcov_dump_pa,
				   GFP_KERNEL, 0);

		if (!nvdla_dev->gcov_dump_va) {
			nvdla_dbg_err(pdev,
				"dma gcov memory allocation failed");
			err = -ENOMEM;
			goto fail_alloc_gcov_dma;
		}
	}
	err = nvdla_set_gcov_region(pdev, false);
	if (err)
		nvdla_free_gcov_region(pdev, false);

fail_alloc_gcov_dma:
	return err;
}

static int nvdla_alloc_trace_region(struct platform_device *pdev)
{
	int err = 0;
	struct nvdla_cmd_mem_info trace_cmd_mem_info;
	struct nvdla_cmd_data cmd_data;
	struct dla_region_printf *trace_region = NULL;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;

	if (!pdata->flcn_isr)
		return 0;

	/* Trace buffer allocation must be done at once only. */
	if (!nvdla_dev->trace_dump_va) {
		/* allocate trace region */
		nvdla_dev->trace_dump_va = dma_alloc_attrs(&pdev->dev,
				   TRACE_BUFFER_SIZE, &nvdla_dev->trace_dump_pa,
				   GFP_KERNEL, 0);

		if (!nvdla_dev->trace_dump_va) {
			nvdla_dbg_err(pdev,
				"dma trace memory allocation failed");
			err = -ENOMEM;
			goto fail_alloc_trace_dma;
		}
	}

	/* assign memory for trace command */
	err = nvdla_get_cmd_memory(pdev, &trace_cmd_mem_info);
	if (err) {
		nvdla_dbg_err(pdev,
			"dma allocation failed for trace command.");
		goto alloc_trace_cmd_failed;
	}

	trace_region = (struct dla_region_printf *)(trace_cmd_mem_info.va);

	trace_region->region = DLA_REGION_TRACE;
	trace_region->address = nvdla_dev->trace_dump_pa;
	trace_region->size = TRACE_BUFFER_SIZE;
	if (nvdla_dev->submit_mode == NVDLA_SUBMIT_MODE_CHANNEL)
		trace_region->address = 0;

	cmd_data.method_id = DLA_CMD_SET_REGIONS;
	cmd_data.method_data = ALIGNED_DMA(trace_cmd_mem_info.pa);
	cmd_data.wait = true;

	err = nvdla_send_cmd(pdev, &cmd_data);

	/* release memory allocated for trace command */
	nvdla_put_cmd_memory(pdev, trace_cmd_mem_info.index);

	if (err != 0) {
		nvdla_dbg_err(pdev, "failed to send trace command");
		goto trace_send_cmd_failed;
	}

	return err;

trace_send_cmd_failed:
alloc_trace_cmd_failed:
	if (nvdla_dev->trace_dump_pa) {
		dma_free_attrs(&pdev->dev, TRACE_BUFFER_SIZE,
			nvdla_dev->trace_dump_va, nvdla_dev->trace_dump_pa,
			0);
		nvdla_dev->trace_dump_va = NULL;

		nvdla_dev->trace_dump_pa = 0;
	}
fail_alloc_trace_dma:

	return err;
}

static int nvdla_alloc_dump_region(struct platform_device *pdev)
{
	int err = 0;
	struct dla_region_printf *region;
	struct nvdla_cmd_mem_info debug_cmd_mem_info;
	struct nvdla_cmd_data cmd_data;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;

	if (!pdata->flcn_isr)
		return 0;

	nvdla_dbg_fn(pdev, "");

	/* allocate dump region only once */
	if (!nvdla_dev->debug_dump_va) {
		nvdla_dev->debug_dump_va = dma_alloc_attrs(&pdev->dev,
				   DEBUG_BUFFER_SIZE, &nvdla_dev->debug_dump_pa,
				   GFP_KERNEL, 0);
		if (!nvdla_dev->debug_dump_va) {
			nvdla_dbg_err(pdev, "debug dump dma alloc failed");
			err = -ENOMEM;
			goto fail_to_alloc_debug_dump;
		}
	}

	/* assign memory for command */
	err = nvdla_get_cmd_memory(pdev, &debug_cmd_mem_info);
	if (err) {
		nvdla_dbg_err(pdev, "dma alloc for command failed");
		goto set_region_failed;
	}

	region = (struct dla_region_printf *)debug_cmd_mem_info.va;
	region->region = DLA_REGION_PRINTF;
	region->size = DEBUG_BUFFER_SIZE;
	region->address = nvdla_dev->debug_dump_pa;
	if (nvdla_dev->submit_mode == NVDLA_SUBMIT_MODE_CHANNEL)
		region->address = 0;

	/* prepare command data */
	cmd_data.method_id = DLA_CMD_SET_REGIONS;
	cmd_data.method_data = ALIGNED_DMA(debug_cmd_mem_info.pa);
	cmd_data.wait = true;

	/* pass dump region to falcon */
	err = nvdla_send_cmd(pdev, &cmd_data);

	/* release memory allocated for debug print command */
	nvdla_put_cmd_memory(pdev, debug_cmd_mem_info.index);

	if (err != 0) {
		nvdla_dbg_err(pdev, "failed to send printf command");
		goto region_send_cmd_failed;
	}

	return 0;

region_send_cmd_failed:
set_region_failed:
	if (nvdla_dev->debug_dump_pa) {
		dma_free_attrs(&pdev->dev, DEBUG_BUFFER_SIZE,
			nvdla_dev->debug_dump_va, nvdla_dev->debug_dump_pa,
			0);
		nvdla_dev->debug_dump_va = NULL;
		nvdla_dev->debug_dump_pa = 0;
	}
fail_to_alloc_debug_dump:
	return err;
}

/* power management API */
int nvhost_nvdla_finalize_poweron(struct platform_device *pdev)
{
	int ret;
	uint32_t fw_ver_read_bin;
	uint32_t firmware_version;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;

	nvdla_dbg_fn(pdev, "");

	ret = nvhost_flcn_finalize_poweron(pdev);
	if (ret) {
		nvdla_dbg_err(pdev, "failed to poweron\n");
		goto fail;
	}

	fw_ver_read_bin = host1x_readl(pdev, NV_DLA_OS_VERSION);

	firmware_version = pdata->version;

	if ((firmware_version & 0xffff00) != (fw_ver_read_bin & 0xffff00)) {
		nvdla_dbg_err(pdev,
		"Fw version of kernel [%u.%u.%u] doesn't match with actual version[%u.%u.%u]",
		(firmware_version >> 16) & 0xff, (firmware_version >> 8) & 0xff, firmware_version & 0xff,
		(fw_ver_read_bin >> 16 ) & 0xff, (fw_ver_read_bin >> 8) & 0xff, fw_ver_read_bin & 0xff);

		ret = -EINVAL;
		goto fail_to_val_ver;
	}

	nvdla_dbg_info(pdev, "Fw version : [%u.%u.%u]\n",
		(fw_ver_read_bin >> 16) & 0xff,
		(fw_ver_read_bin >> 8) & 0xff,
		fw_ver_read_bin & 0xff);

	nvdla_dev->fw_version = fw_ver_read_bin;

	ret = nvdla_alloc_dump_region(pdev);
	if (ret) {
		nvdla_dbg_err(pdev, "fail alloc dump region\n");
		goto fail_to_alloc_dump_reg;
	}

	ret = nvdla_alloc_trace_region(pdev);
	if (ret) {
		nvdla_dbg_err(pdev, "fail alloc trace region\n");
		goto fail_to_alloc_trace;
	}

	return 0;

fail_to_alloc_trace:
fail_to_alloc_dump_reg:
fail_to_val_ver:
	nvhost_nvdla_prepare_poweroff(pdev);
fail:
	return ret;
}

int nvhost_nvdla_prepare_poweroff(struct platform_device *pdev)
{
	int ret;

	nvdla_dbg_fn(pdev, "");

	ret = nvhost_flcn_prepare_poweroff(pdev);
	if (ret) {
		nvdla_dbg_err(pdev, "failed to poweroff\n");
		goto out;
	}

out:
	return ret;
}

/* Free utilization rate memory */
static void nvdla_free_utilization_rate_memory(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;

	if (nvdla_dev->utilization_mem_pa) {
		dma_free_attrs(&pdev->dev, sizeof(unsigned int),
			       nvdla_dev->utilization_mem_va,
			       nvdla_dev->utilization_mem_pa,
			       0);
		nvdla_dev->utilization_mem_va = NULL;
		nvdla_dev->utilization_mem_pa = 0;
	}
}

/* Allocate memory to store the resource utilization rate */
static int nvdla_alloc_utilization_rate_memory(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	int err = 0;

	/* allocate memory for utilization rate */
	nvdla_dev->utilization_mem_va = dma_alloc_attrs(&pdev->dev,
			sizeof(unsigned int), &nvdla_dev->utilization_mem_pa,
			GFP_KERNEL, 0);

	if (nvdla_dev->utilization_mem_va == NULL) {
		nvdla_dbg_err(pdev, "utilization rate dma alloc failed");
		err = -ENOMEM;
	}

	return err;
}

/* Free window size memory */
static void nvdla_free_window_size_memory(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;

	if (nvdla_dev->window_mem_pa) {
		dma_free_attrs(&pdev->dev, sizeof(unsigned int),
			       nvdla_dev->window_mem_va,
			       nvdla_dev->window_mem_pa,
			       0);
		nvdla_dev->window_mem_va = NULL;
		nvdla_dev->window_mem_pa = 0;
	}
}

/* Allocate memory to store the window size for which the utilization rate is computed */
static int nvdla_alloc_window_size_memory(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	int err = 0;

	/* allocate memory for window_size */
	nvdla_dev->window_mem_va = dma_alloc_attrs(&pdev->dev,
			sizeof(unsigned int), &nvdla_dev->window_mem_pa,
			GFP_KERNEL, 0);

	if (nvdla_dev->window_mem_va == NULL) {
		nvdla_dbg_err(pdev, "window size dma alloc failed");
		err = -ENOMEM;
	}

	return err;
}

static int nvdla_hwpm_ip_pm(void *ip_dev, bool disable)
{
	int err = 0;
	struct platform_device *dev = (struct platform_device *)ip_dev;

	nvdla_dbg_fn(dev, "ip power management %s",
			disable ? "disable" : "enable");

	if (disable) {
		err = nvhost_module_busy(ip_dev);
		if (err < 0)
			nvdla_dbg_err(dev, "nvhost_module_busy failed");
	} else {
		nvhost_module_idle(ip_dev);
	}

	return err;
}

static int nvdla_hwpm_ip_reg_op(void *ip_dev,
	enum tegra_soc_hwpm_ip_reg_op reg_op,
	u32 inst_element_index, u64 reg_offset, u32 *reg_data)
{
	struct platform_device *dev = (struct platform_device *)ip_dev;

	if (reg_offset > UINT_MAX)
		return -EINVAL;

	nvdla_dbg_fn(dev, "reg_op %d reg_offset %llu", reg_op, reg_offset);

	if (reg_op == TEGRA_SOC_HWPM_IP_REG_OP_READ)
		*reg_data = host1x_readl(dev, (unsigned int)reg_offset);
	else if (reg_op == TEGRA_SOC_HWPM_IP_REG_OP_WRITE)
		host1x_writel(dev, (unsigned int)reg_offset, *reg_data);

	return 0;
}

static uint32_t nvdla_read_soft_sku_scratch_register(void)
{
	uint32_t dla_soft_sku_opt_disable = 0U;
	void __iomem *scratch_base;

	/*
	 * Map the scratch physical address base, read the register
	 * from the correct offset and then unmap
	 */
	scratch_base = ioremap(SCRATCH_REG_BASE_ADDRESS, SCRATCH_REG_MMAP_SIZE);
	if (scratch_base) {
		dla_soft_sku_opt_disable = __raw_readl(scratch_base + SCRATCH_REG_SW_SKU_OFFSET);
		iounmap(scratch_base);
	}

	return dla_soft_sku_opt_disable;
}

#if KERNEL_VERSION(5, 11, 0) >= LINUX_VERSION_CODE
static int nvhost_nvdla_read_chip_option_register(struct platform_device *pdev)
{
	/* Read floor sweeping info using nvmem api
	 * See Bug 200748079
	 */
	struct nvmem_cell *cell = NULL;
	struct device *dev = &pdev->dev;
	size_t len = 0ULL;
	int *pbuf = NULL;
	int ret = 0;

	cell = nvmem_cell_get(dev, "dla-disable");

	if (IS_ERR(cell)) {

		dev_err(dev,
		"nvmem_cell_get error %ld. Assuming DLA instances are available\n"
		, PTR_ERR(cell));

		ret = 0;
		/* Throwing a fuse read error
		 * and reverting to default
		 * behaviour assuming that the
		 * DLA instance exists
		 */
		goto out;
	}

	pbuf = nvmem_cell_read(cell, &len);

	nvmem_cell_put(cell);

	if (IS_ERR(pbuf)) {

		dev_err(dev,
		"nvmem_cell_read buffer error %ld. Assuming DLA instances are available\n"
		, PTR_ERR(pbuf));

		ret = 0;
		/* Throwing a fuse read error
		 * and reverting to default
		 * behaviour assuming that the
		 * DLA instance exists
		 */
		goto out;
	}

	if (len != FUSE_OPT_DLA_DISABLE_SIZE) {

		dev_err(dev,
		"nvmem_cell_read len mismatch error. Assuming DLA instances are available\n"
		);

		ret = 0;
		/* Throwing a fuse read error
		 * and reverting to default
		 * behaviour assuming that the
		 * DLA instance exists
		 */
		goto out;
	}

	ret  = (int)(*pbuf);

out:
	kfree(pbuf);

	return ret;
}
#endif

#if !IS_ENABLED(CONFIG_TEGRA_GRHOST)
static ssize_t clk_cap_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct nvhost_device_data *pdata =
		container_of(kobj, struct nvhost_device_data, clk_cap_kobj);
	struct nvdla_device *nvdla = pdata->private_data;
	/* i is indeed 'index' here after type conversion */
	int ret, i = attr - pdata->clk_cap_attrs;
	struct clk_bulk_data *clks = &pdata->clks[i];
	struct clk *clk = clks->clk;
	unsigned long freq_cap;
	long freq_cap_signed;
	u32 emc_kbps;

	ret = kstrtoul(buf, 0, &freq_cap);
	if (ret)
		return -EINVAL;
	/* Remove previous freq cap to get correct rounted rate for new cap */
	ret = clk_set_max_rate(clk, UINT_MAX);
	if (ret < 0)
		return ret;

	freq_cap_signed = clk_round_rate(clk, freq_cap);
	if (freq_cap_signed < 0)
		return -EINVAL;

	freq_cap = (unsigned long)freq_cap_signed;
	/* Apply new freq cap */
	ret = clk_set_max_rate(clk, freq_cap);
	if (ret < 0)
		return ret;

	/* Update the clock rate */
	clk_set_rate(clks->clk, freq_cap);
	if (ret < 0)
		return ret;

	/* Update bandwidth requirement based on dla frequency */
	if (i == 0 && nvdla->icc_write && !pm_runtime_suspended(nvdla->dev)) {
		freq_cap = clk_get_rate(clk);
		emc_kbps = freq_cap * NVDLA_AXI_DBB_BW_BPC / 1024;
		ret = icc_set_bw(nvdla->icc_write, kbps_to_icc(emc_kbps), 0);
		if (ret)
			dev_warn(&nvdla->pdev->dev,
				 "failed to set icc_write bw: %d\n", ret);
	}

	return count;
}

static ssize_t clk_cap_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	struct nvhost_device_data *pdata =
		container_of(kobj, struct nvhost_device_data, clk_cap_kobj);
	/* i is indeed 'index' here after type conversion */
	int i = attr - pdata->clk_cap_attrs;
	struct clk_bulk_data *clks = &pdata->clks[i];
	struct clk *clk = clks->clk;
	long max_rate;

	max_rate = clk_round_rate(clk, UINT_MAX);
	if (max_rate < 0)
		return max_rate;

	return snprintf(buf, PAGE_SIZE, "%ld\n", max_rate);
}

static struct kobj_type nvdla_kobj_ktype = {
	.sysfs_ops  = &kobj_sysfs_ops,
};

#endif

/* driver probe and init */
static struct of_device_id tegra_nvdla_of_match[] = {
	{
		.name = "nvdla0",
		.compatible = "nvidia,tegra194-nvdla",
		.data = (struct nvhost_device_data *)&t19_nvdla0_info },
	{
		.name = "nvdla1",
		.compatible = "nvidia,tegra194-nvdla",
		.data = (struct nvhost_device_data *)&t19_nvdla1_info },
	{
		.name = "nvdla0",
		.compatible = "nvidia,tegra234-nvdla",
		.data = (struct nvhost_device_data *)&t23x_nvdla0_info },
	{
		.name = "nvdla1",
		.compatible = "nvidia,tegra234-nvdla",
		.data = (struct nvhost_device_data *)&t23x_nvdla1_info },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_nvdla_of_match);

static uint32_t num_enabled_dla_instances(uint32_t soft_fuse_ret,
					int hw_reg_fuse_ret)
{
	uint32_t num_active_modules = 0U;

	if ((soft_fuse_ret & SOFT_SKU_OVERRIDE_ENABLE_MASK) != 0U) {
		if ((soft_fuse_ret & FUSE_OPT_DLA_0_DISABLED_SOFT) == 0U)
			num_active_modules++;

		if ((soft_fuse_ret & FUSE_OPT_DLA_1_DISABLED_SOFT) == 0U)
			num_active_modules++;
	} else {
		if ((hw_reg_fuse_ret & FUSE_OPT_DLA_0_DISABLED) == 0U)
			num_active_modules++;

		if ((hw_reg_fuse_ret & FUSE_OPT_DLA_1_DISABLED) == 0U)
			num_active_modules++;
	}

	return num_active_modules;
}

static int nvdla_probe(struct platform_device *pdev)
{
	int err = 0;
	struct nvhost_device_data *pdata = NULL;
	struct nvdla_device *nvdla_dev = NULL;
	struct device *dev = &pdev->dev;
	uint32_t soft_fuse_ret = 0U;
	int fuse_register_ret = 0U;
	uint32_t register_value = 0U;
	struct tegra_soc_hwpm_ip_ops hwpm_ip_ops;

#if !IS_ENABLED(CONFIG_TEGRA_GRHOST)
	struct kobj_attribute *attr = NULL;
	int i = 0;
	struct clk_bulk_data *clks;
	struct clk *c;
#endif

	if (pdev->dev.of_node) {
		const struct of_device_id *match;

		match = of_match_device(tegra_nvdla_of_match, dev);
		if (match)
			pdata = (struct nvhost_device_data *)match->data;
	} else {
		pdata = (struct nvhost_device_data *)pdev->dev.platform_data;
	}

	WARN_ON(!pdata);
	if (!pdata) {
		dev_info(dev, "no platform data\n");
		err = -ENODATA;
		goto err_get_pdata;
	}

	if (pdata->version == FIRMWARE_ENCODE_VERSION(T19X) &&
			tegra_get_sku_id() == 0x9E) {
		dev_err(dev, "NVDLA IP is disabled in SKU\n");
		err = -ENODEV;
		goto err_no_ip;
	}

	if (pdata->version == FIRMWARE_ENCODE_VERSION(T19X) &&
			tegra_get_sku_id() == 0x9F &&
			pdata->class == NV_DLA1_CLASS_ID) {
		dev_err(dev, "NVDLA1 IP is disabled in SKU\n");
		err = -ENODEV;
		goto err_no_ip;
	}

	if (pdata->version == FIRMWARE_ENCODE_VERSION(T23X)) {

		soft_fuse_ret = nvdla_read_soft_sku_scratch_register();
		if (soft_fuse_ret & SOFT_SKU_OVERRIDE_ENABLE_MASK) {

			if ((soft_fuse_ret & FUSE_OPT_DLA_0_DISABLED_SOFT)
					&& (pdata->class == NV_DLA0_CLASS_ID)) {
				dev_err(dev, "NVDLA0 IP is disabled in Soft Fuse\n");
				err = -ENODEV;
				goto err_no_ip;
			}

			if ((soft_fuse_ret & FUSE_OPT_DLA_1_DISABLED_SOFT)
					&& (pdata->class == NV_DLA1_CLASS_ID)) {
				dev_err(dev, "NVDLA1 IP is disabled in Soft Fuse\n");
				err = -ENODEV;
				goto err_no_ip;
			}
		} else {
#if KERNEL_VERSION(5, 11, 0) >= LINUX_VERSION_CODE
			fuse_register_ret = nvhost_nvdla_read_chip_option_register(pdev);
#else
			err = tegra_fuse_readl(NVDLA_DISABLE_FUSE_REGISTER_OFFSET, &register_value);
			fuse_register_ret = (int)register_value;
#endif
			if ((fuse_register_ret & FUSE_OPT_DLA_0_DISABLED)
					&& (pdata->class == NV_DLA0_CLASS_ID)) {
				dev_err(dev, "NVDLA0 IP is disabled in Fuse\n");
				err = -ENODEV;
				goto err_no_ip;
			}

			if ((fuse_register_ret & FUSE_OPT_DLA_1_DISABLED)
					&& (pdata->class == NV_DLA1_CLASS_ID)) {
				dev_err(dev, "NVDLA1 IP is disabled in Fuse\n");
				err = -ENODEV;
				goto err_no_ip;
			}
		}
	}

	dma_set_mask(dev, DMA_BIT_MASK(39));

	nvdla_dev = devm_kzalloc(dev, sizeof(*nvdla_dev), GFP_KERNEL);
	if (!nvdla_dev) {
		err = -ENOMEM;
		goto err_alloc_nvdla;
	}

	nvdla_dev->icc_write = devm_of_icc_get(dev, "write");
	if (IS_ERR(nvdla_dev->icc_write)) {
		dev_info(dev, "failed to get icc write handle\n");
		nvdla_dev->icc_write = NULL;
	}

	nvdla_dev->dev = dev;
	nvdla_dev->pdev = pdev;
	pdata->pdev = pdev;
	mutex_init(&pdata->lock);
	mutex_init(&nvdla_dev->cmd_lock);
	init_completion(&nvdla_dev->cmd_completion);
	mutex_init(&nvdla_dev->ping_lock);
	pdata->private_data = nvdla_dev;
	platform_set_drvdata(pdev, pdata);
	nvdla_dev->dbg_mask = debug_err;

	err = nvhost_client_device_get_resources(pdev);
	if (err)
		goto err_get_resources;

	err = nvhost_module_init(pdev);
	if (err)
		goto err_module_init;

	if (pdata->version == FIRMWARE_ENCODE_VERSION(T23X)) {
		if (num_enabled_dla_instances(soft_fuse_ret, fuse_register_ret) == 1) {
			pdev->dev.of_node->name = "nvdla0";
		}
	}

	err = nvhost_client_device_init(pdev);
	if (err)
		goto err_client_device_init;

	/* create debugfs entries */
	nvdla_debug_init(pdev);

	if (pdata->flcn_isr)
		flcn_intr_init(pdev);

	nvdla_dev->pool = nvdla_queue_init(pdev, &nvdla_queue_ops,
				MAX_NVDLA_QUEUE_COUNT);
	if (IS_ERR(nvdla_dev->pool)) {
		err = PTR_ERR(nvdla_dev->pool);
		goto err_queue_init;
	}

	/* init reset handler workqueue */
	nvdla_reset_handler_init(nvdla_dev);

	err = nvhost_syncpt_unit_interface_init(pdev);
	if (err)
		goto err_mss_init;

	err = nvdla_alloc_cmd_memory(pdev);
	if (err)
		goto err_alloc_cmd_mem;

	err = nvdla_alloc_utilization_rate_memory(pdev);
	if (err)
		goto err_alloc_utilization_rate_mem;

	err = nvdla_alloc_window_size_memory(pdev);
	if (err)
		goto err_alloc_window_size_mem;

	nvdla_dbg_info(pdev, "hwpm ip %s register", pdev->name);
	hwpm_ip_ops.ip_dev = (void *)pdev;
	hwpm_ip_ops.ip_base_address = pdev->resource[0].start;
	hwpm_ip_ops.resource_enum = TEGRA_SOC_HWPM_RESOURCE_NVDLA;
	hwpm_ip_ops.hwpm_ip_pm = &nvdla_hwpm_ip_pm;
	hwpm_ip_ops.hwpm_ip_reg_op = &nvdla_hwpm_ip_reg_op;
	tegra_soc_hwpm_ip_register(&hwpm_ip_ops);

#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
	err = nvdla_error_inj_handler_init(nvdla_dev);
	if (err) {
		dev_err(dev, "Failed to register error injection\n");
		goto err_inj_handler_init;
	}
#endif /* CONFIG_TEGRA_HSIERRRPTINJ */

#if !IS_ENABLED(CONFIG_TEGRA_GRHOST)
	if (pdata->num_clks > 0) {
		err = kobject_init_and_add(&pdata->clk_cap_kobj, &nvdla_kobj_ktype,
				&pdev->dev.kobj, "%s", "clk_cap");
		if (err) {
			dev_err(dev, "Could not add dir 'clk_cap'\n");
				goto err_clk_cap_fail;
		}

		pdata->clk_cap_attrs = devm_kcalloc(dev, pdata->num_clks,
			sizeof(*attr), GFP_KERNEL);
		if (!pdata->clk_cap_attrs)
			goto err_cleanup_sysfs;

		for (i = 0; i < pdata->num_clks; ++i) {
			clks = &pdata->clks[i];
			c = clks->clk;
			if (!c)
				continue;

			attr = &pdata->clk_cap_attrs[i];
			attr->attr.name = __clk_get_name(c);
			/* octal permission is preferred nowadays */
			attr->attr.mode = 0644;
			attr->show = clk_cap_show;
			attr->store = clk_cap_store;
			sysfs_attr_init(&attr->attr);
			if (sysfs_create_file(&pdata->clk_cap_kobj, &attr->attr)) {
				dev_err(dev, "Could not create sysfs attribute %s\n",
					__clk_get_name(c));
				err = -EIO;
				goto err_cleanup_sysfs;
			}
		}
	}
#endif

	nvdla_dbg_info(pdev, "pdata:%p initialized\n", pdata);

	return 0;

#if !IS_ENABLED(CONFIG_TEGRA_GRHOST)
err_cleanup_sysfs:
	/* kobj of nvdla_kobj_ktype cleans up sysfs entries automatically */
	kobject_put(&pdata->clk_cap_kobj);
err_clk_cap_fail:
#endif
#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
err_inj_handler_init:
	tegra_soc_hwpm_ip_unregister(&hwpm_ip_ops);
	nvdla_free_window_size_memory(pdev);
#endif /* CONFIG_TEGRA_HSIERRRPTINJ */
err_alloc_window_size_mem:
	nvdla_free_utilization_rate_memory(pdev);
err_alloc_utilization_rate_mem:
	nvdla_free_cmd_memory(pdev);
err_alloc_cmd_mem:
	nvhost_syncpt_unit_interface_deinit(pdev);
err_mss_init:
	nvdla_queue_deinit(nvdla_dev->pool);
err_queue_init:
	nvhost_client_device_release(pdev);
err_client_device_init:
	nvhost_module_deinit(pdev);
err_module_init:
err_get_resources:
	mutex_destroy(&nvdla_dev->ping_lock);
	devm_kfree(dev, nvdla_dev);
err_alloc_nvdla:
err_no_ip:
err_get_pdata:

	return err;
}

static int __exit nvdla_remove(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	struct tegra_soc_hwpm_ip_ops hwpm_ip_ops;

#if !IS_ENABLED(CONFIG_TEGRA_GRHOST)
	int i;
	struct kobj_attribute *attr = NULL;

	if (pdata->clk_cap_attrs) {
		for (i = 0; i < pdata->num_clks; i++) {
			attr = &pdata->clk_cap_attrs[i];
			sysfs_remove_file(&pdata->clk_cap_kobj, &attr->attr);
		}

		kobject_put(&pdata->clk_cap_kobj);
	}
#endif

	nvdla_dbg_info(pdev, "hwpm ip %s unregister", pdev->name);
	hwpm_ip_ops.ip_dev = (void *)pdev;
	hwpm_ip_ops.ip_base_address = pdev->resource[0].start;
	hwpm_ip_ops.resource_enum = TEGRA_SOC_HWPM_RESOURCE_NVDLA;
	hwpm_ip_ops.hwpm_ip_pm = NULL;
	hwpm_ip_ops.hwpm_ip_reg_op = NULL;
	tegra_soc_hwpm_ip_unregister(&hwpm_ip_ops);

#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
	nvdla_error_inj_handler_deinit(nvdla_dev);
#endif /* CONFIG_TEGRA_HSIERRRPTINJ */

	nvhost_syncpt_unit_interface_deinit(pdev);
	nvdla_queue_deinit(nvdla_dev->pool);
	nvhost_client_device_release(pdev);
	nvhost_module_deinit(pdev);
	mutex_destroy(&nvdla_dev->ping_lock);
	nvdla_free_gcov_region(pdev, false);

	if (nvdla_dev->trace_dump_pa) {
		dma_free_attrs(&pdev->dev, TRACE_BUFFER_SIZE,
			       nvdla_dev->trace_dump_va,
			       nvdla_dev->trace_dump_pa,
			       0);
		nvdla_dev->trace_dump_va = NULL;
		nvdla_dev->trace_dump_pa = 0;
	}

	if (nvdla_dev->debug_dump_pa) {
		dma_free_attrs(&pdev->dev, DEBUG_BUFFER_SIZE,
			       nvdla_dev->debug_dump_va,
			       nvdla_dev->debug_dump_pa,
			       0);
		nvdla_dev->debug_dump_va = NULL;
		nvdla_dev->debug_dump_pa = 0;
	}

	nvdla_free_utilization_rate_memory(pdev);
	nvdla_free_window_size_memory(pdev);

	/* free command mem in last */
	nvdla_free_cmd_memory(pdev);

	nvdla_dbg_fn(pdev, "");

	return 0;
}

#ifdef CONFIG_PM
static int nvdla_module_runtime_suspend(struct device *dev)
{
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	struct nvdla_device *nvdla = pdata->private_data;
	int err;

	if (nvhost_module_pm_ops.runtime_suspend != NULL) {
		err = nvhost_module_pm_ops.runtime_suspend(dev);
		if (!err && nvdla->icc_write) {
			err = icc_set_bw(nvdla->icc_write, 0, 0);
			if (err)
				dev_warn(&nvdla->pdev->dev,
					 "failed to set icc_write bw: %d\n", err);

			return 0;
		}
		return err;
	}

	return -EOPNOTSUPP;
}

static int nvdla_module_runtime_resume(struct device *dev)
{
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	struct nvdla_device *nvdla = pdata->private_data;
	struct clk *clk = pdata->clks[0].clk;
	unsigned long rate;
	u32 emc_kbps;
	int err;

	if (nvhost_module_pm_ops.runtime_resume != NULL) {
		err = nvhost_module_pm_ops.runtime_resume(dev);
		if (!err && nvdla->icc_write) {
			rate = clk_get_rate(clk);
			emc_kbps = rate * NVDLA_AXI_DBB_BW_BPC / 1024;
			err = icc_set_bw(nvdla->icc_write, kbps_to_icc(emc_kbps), 0);
			if (err)
				dev_warn(&nvdla->pdev->dev,
					 "failed to set icc_write bw: %d\n", err);

			return 0;
		}
		return err;
	}

	return -EOPNOTSUPP;
}

static int nvdla_module_suspend(struct device *dev)
{
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	int err = 0;

	if (nvhost_module_pm_ops.suspend != NULL) {
		err = nvhost_module_pm_ops.suspend(dev);
		if (err != 0) {
			dev_err(dev, "(FAIL) NvHost suspend\n");
			goto fail_nvhost_module_suspend;
		}
	} else {
		err = pm_runtime_force_suspend(dev);
		if (err != 0) {
			dev_err(dev, "(FAIL) PM suspend\n");
			goto fail_nvhost_module_suspend;
		}
	}

	if (nvdla_dev->icc_write) {
		err = icc_set_bw(nvdla_dev->icc_write, 0, 0);
		if (err)
			dev_warn(&nvdla_dev->pdev->dev,
				 "failed to set icc_write bw: %d\n", err);
	}

	/* Mark module to be in suspend state. */
	nvdla_dev->is_suspended = true;

fail_nvhost_module_suspend:
	return err;
}

static int nvdla_module_resume(struct device *dev)
{
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	int err;

	/* Confirm if module is in suspend state. */
	if (!nvdla_dev->is_suspended) {
		dev_warn(dev, "NvDla is not in suspend state.\n");
		goto fail_not_in_suspend;
	}

	if (nvhost_module_pm_ops.resume != NULL) {
		err = nvhost_module_pm_ops.resume(dev);
		if (err != 0) {
			dev_err(dev, "(FAIL) NvHost resume\n");
			goto fail_nvhost_module_resume;
		}
	} else {
		err = pm_runtime_force_resume(dev);
		if (err != 0) {
			dev_err(dev, "(FAIL) PM resume\n");
			goto fail_nvhost_module_resume;
		}
	}

	return 0;

fail_nvhost_module_resume:
fail_not_in_suspend:
	return err;
}

static int nvdla_module_prepare_suspend(struct device *dev)
{
	int err = 0;
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	struct nvdla_device *nvdla_dev = pdata->private_data;

	/* Confirm if module is not in suspend state. */
	if (nvdla_dev->is_suspended) {
		dev_warn(dev, "NvDla is already in suspend state.\n");
		goto fail_already_in_suspend;
	}

	/* Prepare for queue pool suspension. */
	err = nvdla_queue_pool_prepare_suspend(nvdla_dev->pool);
	if (err != 0) {
		dev_err(dev, "(FAIL) Queue suspend\n");
		goto fail_nvdla_queue_pool_prepare_suspend;
	}

	/* NvHost prepare suspend - callback */
	if (nvhost_module_pm_ops.prepare != NULL) {
		err = nvhost_module_pm_ops.prepare(dev);
		if (err != 0) {
			dev_err(dev, "(FAIL) NvHost prepare suspend\n");
			goto fail_nvhost_module_prepare_suspend;
		}
	} else {
		/* If we took an extra reference, drop it now to prevent
		 * the device from automatically resuming upon system
		 * resume.
		 */
		pm_runtime_put_sync(dev);
	}


	return 0;

fail_nvhost_module_prepare_suspend:
fail_nvdla_queue_pool_prepare_suspend:
fail_already_in_suspend:
	return err;
}

static void nvdla_module_complete_resume(struct device *dev)
{
	struct nvhost_device_data *pdata = dev_get_drvdata(dev);
	struct nvdla_device *nvdla_dev = pdata->private_data;

	if (nvhost_module_pm_ops.complete != NULL) {
		nvhost_module_pm_ops.complete(dev);
	} else {
		/* Retake reference dropped above */
		pm_runtime_get_noresume(dev);
	}

	/* Module is no longer in suspend and has resumed successfully */
	nvdla_dev->is_suspended = false;
}

/**
 * SC7 suspend sequence
 * - prepare_suspend
 * - suspend
 *
 * SC7 resume sequence
 * - resume
 * - complete_resume
 **/
const struct dev_pm_ops nvdla_module_pm_ops = {
	SET_RUNTIME_PM_OPS(nvdla_module_runtime_suspend,
		nvdla_module_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(nvdla_module_suspend,
		nvdla_module_resume)
	.prepare = nvdla_module_prepare_suspend,
	.complete = nvdla_module_complete_resume,
};
#endif /* CONFIG_PM */

static struct platform_driver nvdla_driver = {
	.probe = nvdla_probe,
	.remove = __exit_p(nvdla_remove),
	.driver = {
		.owner = THIS_MODULE,
		.name = "nvdla",
#ifdef CONFIG_OF
		.of_match_table = tegra_nvdla_of_match,
#endif
#ifdef CONFIG_PM
		.pm = &nvdla_module_pm_ops,
#endif
	},
};

#if IS_ENABLED(CONFIG_TEGRA_GRHOST)
module_platform_driver(nvdla_driver);
#else
static struct host1x_driver host1x_nvdla_driver = {
	.driver = {
		.name = "host1x-nvdla",
	},
	.subdevs = tegra_nvdla_of_match,
};

static int __init nvdla_init(void)
{
	int err;

	err = host1x_driver_register(&host1x_nvdla_driver);
	if (err < 0)
		return err;

	err = platform_driver_register(&nvdla_driver);
	if (err < 0)
		host1x_driver_unregister(&host1x_nvdla_driver);

	return err;
}
module_init(nvdla_init);

static void __exit nvdla_exit(void)
{
	platform_driver_unregister(&nvdla_driver);
	host1x_driver_unregister(&host1x_nvdla_driver);
}
module_exit(nvdla_exit);
#endif

MODULE_IMPORT_NS(DMA_BUF);
MODULE_AUTHOR("Shridhar Rasal <srasal@nvidia.com>");
MODULE_LICENSE("GPL v2");
