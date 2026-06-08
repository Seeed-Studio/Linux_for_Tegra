// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVDLA KMD-FALCON implementation
 */

#include "../nvdla_fw.h"
#include "../nvdla_host_wrapper.h"

#include "../nvdla_device.h"
#include "../../dla_os_interface.h"
#include "../../nvdla.h"
#include "../../nvdla_debug.h"
#include "../../nvdla_hw_flcn.h"
#include "nvdla_falcon.h"

#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>

int32_t nvdla_fw_poweron(struct platform_device *pdev)
{
	int32_t err = 0;

	uint32_t fw_ver_read_bin;
	uint32_t firmware_version;
	struct nvhost_device_data *pdata;
	struct nvdla_device *nvdla_dev;

	if (pdev == NULL) {
		err = -EINVAL;
		goto fail;
	}

	pdata = platform_get_drvdata(pdev);
	nvdla_dev = pdata->private_data;

	err = nvdla_flcn_finalize_poweron(pdev);
	if (err) {
		nvdla_dbg_err(pdev, "failed to poweron\n");
		goto fail;
	}

	fw_ver_read_bin = nvdla_device_register_read(pdev, NV_DLA_OS_VERSION);

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
	(void) nvdla_flcn_prepare_poweroff(pdev);
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

	err = nvdla_flcn_prepare_poweroff(pdev);

fail:
	return err;
}

int32_t nvdla_fw_init(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);

	if (pdata->flcn_isr)
		nvdla_flcn_intr_init(pdev);

	return 0;
}

void nvdla_fw_deinit(struct platform_device *pdev)
{
	(void) pdev;
}

int32_t nvdla_fw_reload(struct platform_device *pdev)
{
	return nvdla_flcn_reload_fw(pdev);
}

int32_t nvdla_fw_send_cmd(struct platform_device *pdev,
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

	/**
	 * If device is unavailable, then error out to retry after some time.
	 **/
	if (!nvdla_dev->available) {
		nvdla_dbg_err(pdev, "Command failed: device unavailable\n");
		mutex_unlock(&nvdla_dev->cmd_lock);
		return -EAGAIN;
	}

	/*
	 * enable notification for command completion or error if
	 * wait if required
	 */
	if (wait)
		method_id |= (1 << DLA_INT_ON_COMPLETE_SHIFT) |
					(1 << DLA_INT_ON_ERROR_SHIFT);

	nvdla_dev->waiting = 1;

	nvdla_dbg_reg(pdev, "method_id=[0x%x]", method_id);
	nvdla_device_register_write(pdev, NV_DLA_THI_METHOD_ID, method_id);

	nvdla_dbg_reg(pdev, "method_data=[0x%x]", method_data);
	nvdla_device_register_write(pdev, NV_DLA_THI_METHOD_DATA, method_data);

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

int32_t nvdla_fw_send_ack(struct platform_device *pdev,
	int32_t ack)
{
	int32_t err = 0;

	if (pdev == NULL) {
		err = -EINVAL;
		goto fail;
	}

	nvdla_device_register_write(pdev, flcn_mailbox0_r(), ack);

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
	*stat = nvdla_device_register_read(pdev, flcn_mailbox0_r());
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

	nvdla_device_register_write(pdev, flcn_irqmclr_r(),
		flcn_irqmclr_swgen1_set_f());
	nvdla_device_register_write(pdev, flcn_thi_int_stat_r(),
		flcn_thi_int_stat_clr_f());
	nvdla_device_register_read(pdev, flcn_thi_int_stat_r());
	nvdla_device_register_write(pdev, flcn_irqsclr_r(),
		flcn_irqsclr_swgen1_set_f());

	return 0;
fail:
	return err;
}

int32_t nvdla_fw_inject_corrected_error(struct platform_device *pdev)
{
	int32_t err = 0;

	if (pdev == NULL) {
		err = -EINVAL;
		goto fail;
	}

	nvdla_device_register_write(pdev, flcn_safety_erb_r(),
		flcn_safety_erb_data_corrected_err_v());
	return 0;

fail:
	return err;
}

int32_t nvdla_fw_inject_uncorrected_error(struct platform_device *pdev)
{
	int32_t err = 0;

	if (pdev == NULL) {
		err = -EINVAL;
		goto fail;
	}

	nvdla_device_register_write(pdev, flcn_safety_erb_r(),
		flcn_safety_erb_data_uncorrected_err_v());
	return 0;

fail:
	return err;
}
