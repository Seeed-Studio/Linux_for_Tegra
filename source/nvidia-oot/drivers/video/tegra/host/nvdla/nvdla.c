// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2016-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVDLA driver for T194/T23x
 */

#include <nvidia/conftest.h>

#include <linux/acpi.h>
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
#if defined(NVDLA_HAVE_CONFIG_HW_PERFMON) && (NVDLA_HAVE_CONFIG_HW_PERFMON == 1)
#include <uapi/linux/tegra-soc-hwpm-uapi.h>
#endif /* NVDLA_HAVE_CONFIG_HW_PERFMON */

#if defined(NVDLA_HAVE_CONFIG_HSIERRINJ) && (NVDLA_HAVE_CONFIG_HSIERRINJ == 1)
#include <linux/tegra-hsierrrptinj.h>
#endif /* NVDLA_HAVE_CONFIG_HSIERRINJ */

#if !IS_ENABLED(CONFIG_TEGRA_GRHOST)
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#endif

#include "nvdla.h"
#include "nvdla_hw_flcn.h"
#if defined(NVDLA_HAVE_CONFIG_AXI) && (NVDLA_HAVE_CONFIG_AXI == 1)
#include "nvdla_t25x.h"
#include "nvdla_t264_sim.h"
#else
#include "nvdla_t194.h"
#include "nvdla_t234.h"
#endif /* NVDLA_HAVE_CONFIG_AXI */
#include "dla_t19x_fw_version.h"
#include "dla_t23x_fw_version.h"
#include "dla_queue.h"
#include "nvdla_buffer.h"
#include "nvdla_debug.h"
#include "dla_os_interface.h"
#include "port/nvdla_device.h"
#include "port/nvdla_fw.h"
#include "port/nvdla_pm.h"

#if defined(NVDLA_HAVE_CONFIG_HSIERRINJ) && (NVDLA_HAVE_CONFIG_HSIERRINJ == 1)
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

	err = nvdla_module_busy(pdev);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to power on\n");
		err = -ENODEV;
		goto fail;
	}

	if ((frame.reporter_id == device_ue_reporter_id) &&
		 (frame.error_code == device_ue_error_code)) {
		/* Inject uncorrected error. */
		nvdla_dbg_info(pdev, "UE Reported ID: %x, Error Code: %x",
			frame.reporter_id, frame.error_code);
		nvdla_fw_inject_uncorrected_error(pdev);
	} else if ((frame.reporter_id == device_ce_reporter_id) &&
		 (frame.error_code == device_ce_error_code)) {
		/* Inject corrected error. */
		nvdla_dbg_info(pdev, "CE Reported ID: %x, Error Code: %x",
			frame.reporter_id, frame.error_code);
		nvdla_fw_inject_corrected_error(pdev);
	} else {
		nvdla_dbg_err(pdev, "Invalid Reported ID: %x, Error Code: %x",
			frame.reporter_id, frame.error_code);
		err = -EINVAL;
	}

	nvdla_module_idle(pdev);

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
#endif /* NVDLA_HAVE_CONFIG_HSIERRINJ */

/*
 * Work to handle engine reset for error recovery
 */
static void nvdla_reset_handler(struct work_struct *work)
{
	struct nvdla_device *nvdla_dev = container_of(work,
					struct nvdla_device, reset_work);

	struct platform_device *pdev = nvdla_dev->pdev;

	/* reset engine */
	nvdla_module_reset(pdev, true);

	nvdla_dbg_info(pdev, "Engine reset done\n");
}

static void nvdla_reset_handler_init(struct nvdla_device *nvdla_dev)
{
	INIT_WORK(&nvdla_dev->reset_work, nvdla_reset_handler);
}

static void nvdla_poweroff_handler(struct work_struct *work)
{
	struct nvdla_device *nvdla_dev = container_of(work,
					struct nvdla_device, poweroff_work);

	struct platform_device *pdev = nvdla_dev->pdev;

	/* poweroff engine */
	nvdla_module_idle(pdev);

	nvdla_dbg_info(pdev, "Engine poweroff done\n");
}

static void nvdla_poweroff_handler_init(struct nvdla_device *nvdla_dev)
{
	INIT_WORK(&nvdla_dev->poweroff_work, nvdla_poweroff_handler);
}

int nvdla_flcn_isr(struct platform_device *pdev)
{
	uint32_t message;
	uint32_t mailbox0;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;

	/* dump falcon data if debug enabled */
	(void) nvdla_fw_interrupt_stat_read(pdev, &mailbox0);

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

	if (message == DLA_MSG_IDLE_TIMEOUT) {
		nvdla_dbg_info(pdev, "Idle notification detected");
		schedule_work(&nvdla_dev->poweroff_work);
	}

clear_interrupt:
	/* Clear the interrupt */
	(void) nvdla_fw_interrupt_stat_clear(pdev);

	/* Notify FW that interuppt handling is complete */
	(void) nvdla_fw_send_ack(pdev, DLA_MSG_INTERRUPT_HANDLING_COMPLETE);

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

	err = nvdla_module_busy(pdev);
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

	err = nvdla_fw_send_cmd(pdev, &cmd_data);

	/* release memory allocated for gcov command */
	nvdla_put_cmd_memory(pdev, gcov_cmd_mem_info.index);

	if (err != 0) {
		nvdla_dbg_err(pdev, "failed to send gcov command");
		goto gcov_send_cmd_failed;
	}

	nvdla_module_idle(pdev);

	return err;

gcov_send_cmd_failed:
alloc_gcov_cmd_failed:
	nvdla_module_idle(pdev);
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

	err = nvdla_fw_send_cmd(pdev, &cmd_data);

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
	err = nvdla_fw_send_cmd(pdev, &cmd_data);

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
static int32_t s_nvdla_poweron(struct platform_device *pdev)
{
	int32_t err;

	err = nvdla_pm_clock_ungate(pdev);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to ungate power, err=%d\n", err);
		goto fail;
	}

	err = nvdla_pm_rail_ungate(pdev);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to ungate power, err=%d\n", err);
		goto clockoff;
	}

	err = nvdla_pm_power_ungate(pdev);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to ungate power, err=%d\n", err);
		goto railoff;
	}

	return 0;

railoff:
	(void) nvdla_pm_rail_gate(pdev, true);
clockoff:
	(void) nvdla_pm_clock_gate(pdev, true);
fail:
	return err;
}

static int32_t s_nvdla_poweroff(struct platform_device *pdev)
{
	int32_t err;

	err = nvdla_pm_power_gate(pdev, true);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to gate power, err=%d\n", err);
		goto fail;
	}

	err = nvdla_pm_rail_gate(pdev, true);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to gate power, err=%d\n", err);
		goto poweron;
	}

	err = nvdla_pm_clock_gate(pdev, true);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to gate power, err=%d\n", err);
		goto railon;
	}

	return 0;

railon:
	(void) nvdla_pm_rail_ungate(pdev);
poweron:
	(void) nvdla_pm_power_ungate(pdev);
fail:
	return err;
}


int nvdla_finalize_poweron(struct platform_device *pdev)
{
	int ret;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	bool available;

	nvdla_dbg_fn(pdev, "");

	ret = s_nvdla_poweron(pdev);
	if (ret) {
		nvdla_dbg_err(pdev, "failed to poweron\n");
		goto fail;
	}

	ret = nvdla_fw_poweron(pdev);
	if (ret) {
		nvdla_dbg_err(pdev, "failed to fw poweron\n");
		goto poweroff;
	}

	/**
	 * At this point, the falcon & hardware is available to use.
	 **/
	mutex_lock(&nvdla_dev->cmd_lock);
	available = nvdla_dev->available;
	nvdla_dev->available = true;
	mutex_unlock(&nvdla_dev->cmd_lock);

	ret = nvdla_alloc_dump_region(pdev);
	if (ret) {
		nvdla_dbg_err(pdev, "fail alloc dump region\n");
		goto restore_device_availability_and_fw_poweroff;
	}

	ret = nvdla_alloc_trace_region(pdev);
	if (ret) {
		nvdla_dbg_err(pdev, "fail alloc trace region\n");
		goto restore_device_availability_and_fw_poweroff;
	}

	ret = nvdla_pm_reset(pdev);
	if (ret) {
		nvdla_dbg_err(pdev, "fail to reset pm\n");
		goto restore_device_availability_and_fw_poweroff;
	}

	return 0;

restore_device_availability_and_fw_poweroff:
	/* Mark the device to be unavailable. */
	mutex_lock(&nvdla_dev->cmd_lock);
	nvdla_dev->available = available;
	mutex_unlock(&nvdla_dev->cmd_lock);

	(void) nvdla_fw_poweroff(pdev);
poweroff:
	(void) s_nvdla_poweroff(pdev);
fail:
	return ret;
}

int nvdla_prepare_poweroff(struct platform_device *pdev)
{
	int ret;

	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	bool available;

	nvdla_dbg_fn(pdev, "");

	/* Mark the device to be unavailable. */
	mutex_lock(&nvdla_dev->cmd_lock);
	available = nvdla_dev->available;
	nvdla_dev->available = false;
	mutex_unlock(&nvdla_dev->cmd_lock);

	ret = nvdla_fw_poweroff(pdev);
	if (ret) {
		nvdla_dbg_err(pdev, "failed to fw poweroff\n");
		goto restore_device_availability;
	}

	ret = s_nvdla_poweroff(pdev);
	if (ret) {
		nvdla_dbg_err(pdev, "failed to poweroff\n");
		goto fw_poweron;
	}

	return 0;

fw_poweron:
	(void) nvdla_fw_poweron(pdev);
restore_device_availability:
	/* Mark the device to be available. */
	mutex_lock(&nvdla_dev->cmd_lock);
	nvdla_dev->available = available;
	mutex_unlock(&nvdla_dev->cmd_lock);

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

#if defined(NVDLA_HAVE_CONFIG_HW_PERFMON) && (NVDLA_HAVE_CONFIG_HW_PERFMON == 1)
static int nvdla_hwpm_ip_pm(void *ip_dev, bool disable)
{
	int err = 0;
	struct platform_device *dev = (struct platform_device *)ip_dev;

	nvdla_dbg_fn(dev, "ip power management %s",
			disable ? "disable" : "enable");

	if (disable) {
		err = nvdla_module_busy(ip_dev);
		if (err < 0)
			nvdla_dbg_err(dev, "nvdla_module_busy failed");
	} else {
		nvdla_module_idle(ip_dev);
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
		*reg_data = nvdla_device_register_read(dev,
			(unsigned int)reg_offset);
	else if (reg_op == TEGRA_SOC_HWPM_IP_REG_OP_WRITE)
		nvdla_device_register_write(dev, (unsigned int)reg_offset,
			*reg_data);

	return 0;
}
#endif

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
static int nvdla_read_chip_option_register(struct platform_device *pdev)
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
#if defined(NVDLA_HAVE_CONFIG_AXI) && (NVDLA_HAVE_CONFIG_AXI == 1)
static struct of_device_id tegra_nvdla_of_match[] = {
	{
		.name = "nvdla0",
		.compatible = "nvidia,tegra25x-nvdla",
		.data = (struct nvhost_device_data *)&t25x_nvdla0_info },
	{
		.name = "nvdla1",
		.compatible = "nvidia,tegra25x-nvdla",
		.data = (struct nvhost_device_data *)&t25x_nvdla1_info },
	{
		.name = "nvdla0",
		.compatible = "nvidia,tegra264-nvdla",
		.data = (struct nvhost_device_data *)&t264_sim_nvdla0_info },
	{ },
};

static struct acpi_device_id tegra_nvdla_acpi_match[] = {
	{
		.id = "NVDA200A",
		.driver_data = 0x0,
	},
	{ },
};

static void *acpi_data[] = {
	/*0x0*/ &t25x_nvdla0_info,
	NULL,
};

#else
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

static struct acpi_device_id tegra_nvdla_acpi_match[] = {
	{ },
};

static void *acpi_data[] = {
	NULL,
};

#endif /* NVDLA_HAVE_CONFIG_AXI */
MODULE_DEVICE_TABLE(of, tegra_nvdla_of_match);
MODULE_DEVICE_TABLE(acpi, tegra_nvdla_acpi_match);

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
#if defined(NVDLA_HAVE_CONFIG_HW_PERFMON) && (NVDLA_HAVE_CONFIG_HW_PERFMON == 1)
	struct tegra_soc_hwpm_ip_ops hwpm_ip_ops;
#endif /* NVDLA_HAVE_CONFIG_HW_PERFMON */

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
	} else if (ACPI_HANDLE(&pdev->dev)) {
		const struct acpi_device_id *match;

		match = acpi_match_device(tegra_nvdla_acpi_match, dev);
		if (match)
			pdata = (struct nvhost_device_data *)
				acpi_data[match->driver_data];
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
			fuse_register_ret = nvdla_read_chip_option_register(pdev);
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

	if (pdev->dev.of_node) {
		nvdla_dev->icc_write = devm_of_icc_get(dev, "write");
		if (IS_ERR(nvdla_dev->icc_write))
			return dev_err_probe(&pdev->dev, PTR_ERR(nvdla_dev->icc_write),
				     "failed to get icc write handle\n");
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

	err = nvdla_module_init(pdev);
	if (err != 0) {
		dev_err(dev, "Failed to init device\n");
		goto err_device_init;
	}

	if (pdata->version == FIRMWARE_ENCODE_VERSION(T23X)) {
		if (num_enabled_dla_instances(soft_fuse_ret, fuse_register_ret) == 1) {
			if (pdev->dev.of_node)
				pdev->dev.of_node->name = "nvdla0";
			else
				pdata->devfs_name = "nvdla0";
		}
	}

	/* create debugfs entries */
	nvdla_debug_init(pdev);

	(void) nvdla_fw_init(pdev);

	nvdla_dev->pool = nvdla_queue_init(pdev, &nvdla_queue_ops,
				MAX_NVDLA_QUEUE_COUNT);
	if (IS_ERR(nvdla_dev->pool)) {
		err = PTR_ERR(nvdla_dev->pool);
		goto err_queue_init;
	}

	/* init reset handler workqueue */
	nvdla_reset_handler_init(nvdla_dev);

	/* init poweroff handler workqueue */
	nvdla_poweroff_handler_init(nvdla_dev);

	nvdla_dev->sync_dev = nvdla_sync_device_create_syncpoint(pdev);
#if defined(BUG_4942853) && (BUG_4942853 == 1)
	/* Intentionally left empty until bug is resolved */
#else
	if (nvdla_dev->sync_dev == NULL) {
		err = -ENOMEM;
		goto err_mss_init;
	}
#endif // BUG_4942853

	err = nvdla_alloc_cmd_memory(pdev);
	if (err)
		goto err_alloc_cmd_mem;

	err = nvdla_alloc_utilization_rate_memory(pdev);
	if (err)
		goto err_alloc_utilization_rate_mem;

	err = nvdla_alloc_window_size_memory(pdev);
	if (err)
		goto err_alloc_window_size_mem;

#if defined(NVDLA_HAVE_CONFIG_HW_PERFMON) && (NVDLA_HAVE_CONFIG_HW_PERFMON == 1)
	nvdla_dbg_info(pdev, "hwpm ip %s register", pdev->name);
	hwpm_ip_ops.ip_dev = (void *)pdev;
	hwpm_ip_ops.ip_base_address = pdev->resource[0].start;
	hwpm_ip_ops.resource_enum = TEGRA_SOC_HWPM_RESOURCE_NVDLA;
	hwpm_ip_ops.hwpm_ip_pm = &nvdla_hwpm_ip_pm;
	hwpm_ip_ops.hwpm_ip_reg_op = &nvdla_hwpm_ip_reg_op;
	tegra_soc_hwpm_ip_register(&hwpm_ip_ops);
#endif

#if defined(NVDLA_HAVE_CONFIG_HSIERRINJ) && (NVDLA_HAVE_CONFIG_HSIERRINJ == 1)
	err = nvdla_error_inj_handler_init(nvdla_dev);
	if (err) {
		dev_err(dev, "Failed to register error injection\n");
		goto err_inj_handler_init;
	}
#endif /* NVDLA_HAVE_CONFIG_HSIERRINJ */

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
#if defined(NVDLA_HAVE_CONFIG_HSIERRINJ) && (NVDLA_HAVE_CONFIG_HSIERRINJ == 1)
err_inj_handler_init:
#if defined(NVDLA_HAVE_CONFIG_HW_PERFMON) && (NVDLA_HAVE_CONFIG_HW_PERFMON == 1)
	tegra_soc_hwpm_ip_unregister(&hwpm_ip_ops);
#endif /* NVDLA_HAVE_CONFIG_HW_PERFMON */
	nvdla_free_window_size_memory(pdev);
#endif /* NVDLA_HAVE_CONFIG_HSIERRINJ */
err_alloc_window_size_mem:
	nvdla_free_utilization_rate_memory(pdev);
err_alloc_utilization_rate_mem:
	nvdla_free_cmd_memory(pdev);
err_alloc_cmd_mem:
	nvdla_sync_device_destroy(nvdla_dev->sync_dev);
#if defined(BUG_4942853) && (BUG_4942853 == 1)
	/* Intentionally left empty until bug is resolved */
#else
err_mss_init:
#endif
	nvdla_queue_deinit(nvdla_dev->pool);
err_queue_init:
	nvdla_fw_deinit(pdev);
	nvdla_module_deinit(pdev);
err_device_init:
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
#if defined(NVDLA_HAVE_CONFIG_HW_PERFMON) && (NVDLA_HAVE_CONFIG_HW_PERFMON == 1)
	struct tegra_soc_hwpm_ip_ops hwpm_ip_ops;
#endif /* NVDLA_HAVE_CONFIG_HW_PERFMON */

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

#if defined(NVDLA_HAVE_CONFIG_HW_PERFMON) && (NVDLA_HAVE_CONFIG_HW_PERFMON == 1)
	nvdla_dbg_info(pdev, "hwpm ip %s unregister", pdev->name);
	hwpm_ip_ops.ip_dev = (void *)pdev;
	hwpm_ip_ops.ip_base_address = pdev->resource[0].start;
	hwpm_ip_ops.resource_enum = TEGRA_SOC_HWPM_RESOURCE_NVDLA;
	hwpm_ip_ops.hwpm_ip_pm = NULL;
	hwpm_ip_ops.hwpm_ip_reg_op = NULL;
	tegra_soc_hwpm_ip_unregister(&hwpm_ip_ops);
#endif /* NVDLA_HAVE_CONFIG_HW_PERFMON */

#if defined(NVDLA_HAVE_CONFIG_HSIERRINJ) && (NVDLA_HAVE_CONFIG_HSIERRINJ == 1)
	nvdla_error_inj_handler_deinit(nvdla_dev);
#endif /* NVDLA_HAVE_CONFIG_HSIERRINJ */

	nvdla_sync_device_destroy(nvdla_dev->sync_dev);
	nvdla_queue_deinit(nvdla_dev->pool);
	nvdla_module_deinit(pdev);
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

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void __exit nvdla_remove_wrapper(struct platform_device *pdev)
{
	nvdla_remove(pdev);
}
#else
static int __exit nvdla_remove_wrapper(struct platform_device *pdev)
{
	return nvdla_remove(pdev);
}
#endif

static struct platform_driver nvdla_driver = {
	.probe = nvdla_probe,
	.remove = __exit_p(nvdla_remove_wrapper),
	.driver = {
		.owner = THIS_MODULE,
		.name = "nvdla",
#ifdef CONFIG_OF
		.of_match_table = tegra_nvdla_of_match,
#endif
#ifdef CONFIG_ACPI
		.acpi_match_table = ACPI_PTR(tegra_nvdla_acpi_match),
#endif
#ifdef CONFIG_PM
		.pm = &nvdla_module_pm_ops,
#endif
	},
};

#if IS_ENABLED(CONFIG_TEGRA_GRHOST)
module_platform_driver(nvdla_driver);
#else

static int __init nvdla_init(void)
{
	return nvdla_driver_register(&nvdla_driver);
}
module_init(nvdla_init);

static void __exit nvdla_exit(void)
{
	nvdla_driver_unregister(&nvdla_driver);
}
module_exit(nvdla_exit);
#endif

#if defined(NV_MODULE_IMPORT_NS_CALLS_STRINGIFY)
MODULE_IMPORT_NS(DMA_BUF);
#else
MODULE_IMPORT_NS("DMA_BUF");
#endif
MODULE_AUTHOR("Shridhar Rasal <srasal@nvidia.com>");
MODULE_LICENSE("GPL v2");
