// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 */

#include <linux/host1x.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/version.h>
#include <linux/errno.h>
#include "nvvse-linux-common.h"
#include "se_job_completion_hal.h"
#include "nvvse-job-completion-priv.h"

#define SE_MAX_SCHEDULE_TIMEOUT					LONG_MAX

const struct of_device_id host1x_match[] = {
	{ .compatible = "nvidia,tegra234-host1x", },
	{ .compatible = "nvidia,tegra264-host1x", },
	{},
};

static SeJobCompletionStatus ConvertLinuxErrorToSeJobCompletionStatus(int err)
{
	switch (err) {
	case 0:
		return SE_JOB_COMPLETION_STATUS_SUCCESS;
	case -EINVAL:
		return SE_JOB_COMPLETION_STATUS_INVALID_PARAM;
	case -ENOMEM:
		return SE_JOB_COMPLETION_STATUS_ALLOCATION_FAILED;
	case -ENODEV:
		return SE_JOB_COMPLETION_STATUS_DEVICE_NOT_FOUND;
	case -EAGAIN:
		return SE_JOB_COMPLETION_STATUS_OPERATION_FAILED;
	case -ETIMEDOUT:
		return SE_JOB_COMPLETION_STATUS_TIMEOUT;
	default:
		return SE_JOB_COMPLETION_STATUS_OPERATION_FAILED;
	}
}

SeJobCompletionStatus se_job_completion_init(SeJobCompletionDeviceAttributesOS *os_dev_attr,
			se_job_completion_device_handle *device_hdl)
{
	struct se_job_completion_device_handle_priv *device_hdl_priv;
	struct SeJobCompletionDeviceAttributes *dev_attr;
	struct platform_device *host1x_pdev = NULL;
	struct host1x *host1x;
	struct device_node *np = NULL;
	int err = 0;
	SeJobCompletionStatus status = SE_JOB_COMPLETION_STATUS_SUCCESS;

	if (os_dev_attr == NULL || device_hdl == NULL) {
		NVVSE_ERR("Invalid input parameters\n");
		return SE_JOB_COMPLETION_STATUS_INVALID_PARAM;
	}
	dev_attr = (struct SeJobCompletionDeviceAttributes *)os_dev_attr;

	device_hdl_priv = devm_kzalloc((struct device *)dev_attr->dev,
			sizeof(struct se_job_completion_device_handle_priv), GFP_KERNEL);
	if (!device_hdl_priv) {
		NVVSE_ERR("Failed to allocate se_job_completion_handle\n");
		return SE_JOB_COMPLETION_STATUS_ALLOCATION_FAILED;
	}
	device_hdl_priv->dev = dev_attr->dev;

	np = of_find_matching_node(NULL, host1x_match);
	if (!np) {
		NVVSE_ERR("Failed to find host1x, syncpt support disabled");
		err = -ENODATA;
		status = ConvertLinuxErrorToSeJobCompletionStatus(err);
		goto exit;
	}

	host1x_pdev = of_find_device_by_node(np);
	if (!host1x_pdev) {
		NVVSE_ERR("host1x device not available");
		err = -ENODEV;
		status = ConvertLinuxErrorToSeJobCompletionStatus(err);
		goto exit;
	}

	host1x = platform_get_drvdata(host1x_pdev);
	if (!host1x) {
		NVVSE_ERR("No platform data for host1x!\n");
		err = -ENODATA;
		status = ConvertLinuxErrorToSeJobCompletionStatus(err);
		goto exit;
	}

	device_hdl_priv->host1x = host1x;
	*device_hdl = (se_job_completion_device_handle)device_hdl_priv;

exit:
	if (np)
		of_node_put(np);
	if (host1x_pdev)
		put_device(&host1x_pdev->dev);
	return status;
}
EXPORT_SYMBOL(se_job_completion_init);

SeJobCompletionStatus se_job_completion_deinit(se_job_completion_device_handle dev_hdl)
{
	// return success as we don't need to deinitialize the device
	return SE_JOB_COMPLETION_STATUS_SUCCESS;
}
EXPORT_SYMBOL(se_job_completion_deinit);

SeJobCompletionStatus se_job_completion_waiter_allocate(
	se_job_completion_device_handle dev_hdl,
	se_job_completion_wait_handle *wait_handle)
{
	struct se_job_completion_device_handle_priv *dev_hdl_priv;
	struct se_job_completion_wait_handle_priv *wait_hdl;
	int err = 0;
	SeJobCompletionStatus status = SE_JOB_COMPLETION_STATUS_SUCCESS;


	if (dev_hdl == NULL || wait_handle == NULL) {
		NVVSE_ERR("Invalid input parameters\n");
		err = -EINVAL;
		status = ConvertLinuxErrorToSeJobCompletionStatus(err);
		goto exit;
	}
	dev_hdl_priv = (struct se_job_completion_device_handle_priv *)dev_hdl;

	wait_hdl = devm_kzalloc((struct device *)dev_hdl_priv->dev,
		sizeof(struct se_job_completion_wait_handle_priv), GFP_KERNEL);
	if (!wait_hdl) {
		NVVSE_ERR("Failed to allocate se_job_completion_wait_handle\n");
		err = -ENOMEM;
		status = ConvertLinuxErrorToSeJobCompletionStatus(err);
		goto exit;
	}
	wait_hdl->host1x = dev_hdl_priv->host1x;
	wait_hdl->dev = dev_hdl_priv->dev;

	*wait_handle = (se_job_completion_wait_handle)wait_hdl;
	status = SE_JOB_COMPLETION_STATUS_SUCCESS;
exit:
	return status;
}
EXPORT_SYMBOL(se_job_completion_waiter_allocate);

SeJobCompletionStatus se_job_completion_wait(se_job_completion_wait_handle wait_handle,
			SeResponseInfo *response_info)
{
	struct se_job_completion_wait_handle_priv *wait_hdl;
	struct virt_se_msg_resp_soc_t *response_info_priv;
	struct host1x_syncpt *sp;
	int err = 0;
	SeJobCompletionStatus status = SE_JOB_COMPLETION_STATUS_SUCCESS;

	if (wait_handle == NULL || response_info == NULL) {
		NVVSE_ERR("Invalid input parameters\n");
		err = -EINVAL;
		status = ConvertLinuxErrorToSeJobCompletionStatus(err);
		goto exit;
	}
	wait_hdl = (struct se_job_completion_wait_handle_priv *)wait_handle;
	response_info_priv = (struct virt_se_msg_resp_soc_t *)response_info;

	if (response_info_priv->soc_status[0].syncpt_id_valid == 0) {
		status = SE_JOB_COMPLETION_STATUS_SUCCESS;
		goto exit;
	}

	sp = host1x_syncpt_get_by_id_noref(wait_hdl->host1x,
			response_info_priv->soc_status[0].syncpt_id);
	if (!sp) {
		NVVSE_ERR("No syncpt for syncpt id %d\n",
				response_info_priv->soc_status[0].syncpt_id);
		err = -ENODATA;
		status = ConvertLinuxErrorToSeJobCompletionStatus(err);
		goto exit;
	}

	err = host1x_syncpt_wait(sp, response_info_priv->soc_status[0].syncpt_threshold,
			SE_MAX_SCHEDULE_TIMEOUT, NULL);
	if (err) {
		NVVSE_ERR("timed out for syncpt %u threshold %u err %d\n",
			response_info_priv->soc_status[0].syncpt_id,
			response_info_priv->soc_status[0].syncpt_threshold, err);
		err = -ETIMEDOUT;
		status = ConvertLinuxErrorToSeJobCompletionStatus(err);
		goto exit;
	}

	status = SE_JOB_COMPLETION_STATUS_SUCCESS;
exit:
	return status;
}
EXPORT_SYMBOL(se_job_completion_wait);

SeJobCompletionStatus se_job_completion_wait_legacy(se_job_completion_wait_handle wait_handle,
			SeResponseInfo *response_info)
{
	// return failed as we don't support legacy wait
	return SE_JOB_COMPLETION_STATUS_OPERATION_FAILED;
}
EXPORT_SYMBOL(se_job_completion_wait_legacy);
