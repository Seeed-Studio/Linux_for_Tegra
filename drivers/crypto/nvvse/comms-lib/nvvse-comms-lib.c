// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 */

#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include "nvvse-linux-common.h"
#include "comms_lib_hal.h"
#include "nvvse-comms-lib-priv.h"

#define TEGRA_VIRTUAL_SE_TIMEOUT_1S				1000000

static CommsLibStatus_t ConvertLinuxErrorToCommsLibStatus(int err)
{
	switch (err) {
	case 0:
		return COMMS_LIB_STATUS_SUCCESS;
	case -EINVAL:
		return COMMS_LIB_STATUS_INVALID_PARAMS;
	case -ENOMEM:
		return COMMS_LIB_STATUS_OUT_OF_MEMORY;
	case -EBUSY:
		return COMMS_LIB_STATUS_OPERATION_BUSY;
	case -ENODEV:
		return COMMS_LIB_STATUS_INVALID_STATE;
	case -EAGAIN:
		return COMMS_LIB_STATUS_INVALID_OPERATION;
	case -ETIMEDOUT:
		return COMMS_LIB_STATUS_CONNECTION_TIMEOUT;
	case -EPERM:
		return COMMS_LIB_STATUS_PERMISSION_ERROR;
	default:
		return COMMS_LIB_STATUS_FAILED;
	}
}

CommsLibStatus_t comms_lib_init(void)
{
	// return success as we don't need to initialize the channel
	return COMMS_LIB_STATUS_SUCCESS;
}

CommsLibStatus_t comms_lib_init_channel(CommsDeviceAttributeOS comms_info,
			CommsLibHandle_t *comms_handle)
{
	struct comms_handle_priv *handle_priv = NULL;
	struct CommsDeviceAttribute *comms_dev_attr;
	CommsLibStatus_t status = COMMS_LIB_STATUS_SUCCESS;
	int err;

	if (comms_info == NULL) {
		NVVSE_ERR("Invalid comms info\n");
		err = -EINVAL;
		status = ConvertLinuxErrorToCommsLibStatus(err);
		goto exit;
	}
	comms_dev_attr = (struct CommsDeviceAttribute *)comms_info;

	if (comms_dev_attr->dev == NULL) {
		NVVSE_ERR("Invalid comms device\n");
		err = -EINVAL;
		status = ConvertLinuxErrorToCommsLibStatus(err);
		goto exit;
	}

	if (comms_handle == NULL) {
		NVVSE_ERR("comms_handle is NULL\n");
		err = -EINVAL;
		status = ConvertLinuxErrorToCommsLibStatus(err);
		goto exit;
	}

	handle_priv = devm_kzalloc(comms_dev_attr->dev,
			sizeof(struct comms_handle_priv), GFP_KERNEL);
	if (!handle_priv) {
		NVVSE_ERR("Failed to allocate comms handle\n");
		err = -ENOMEM;
		status = ConvertLinuxErrorToCommsLibStatus(err);
		goto exit;
	}
	handle_priv->dev = (struct device *)comms_dev_attr->dev;
	handle_priv->comm_id = comms_dev_attr->comms_id;

	handle_priv->ivck = tegra_hv_ivc_reserve(NULL, handle_priv->comm_id, NULL);
	if (IS_ERR_OR_NULL(handle_priv->ivck)) {
		NVVSE_ERR("Failed reserve channel number\n");
		err = -ENODEV;
		status = ConvertLinuxErrorToCommsLibStatus(err);
		goto free_handle_priv;
	}
	tegra_hv_ivc_channel_reset(handle_priv->ivck);

	*comms_handle = (CommsLibHandle_t)handle_priv;

	return COMMS_LIB_STATUS_SUCCESS;

free_handle_priv:
	devm_kfree(comms_dev_attr->dev, handle_priv);

exit:
	return status;
}

CommsLibStatus_t comms_lib_deinit_channel(CommsLibHandle_t comms_handle)
{
	struct comms_handle_priv *comms_handle_priv;
	int err = 0;
	CommsLibStatus_t status = COMMS_LIB_STATUS_SUCCESS;

	if (comms_handle == NULL) {
		NVVSE_ERR("comms_handle is NULL\n");
		err = -EINVAL;
		status = ConvertLinuxErrorToCommsLibStatus(err);
		goto exit;
	}
	comms_handle_priv = (struct comms_handle_priv *)comms_handle;

	if (comms_handle_priv->ivck != NULL)
		(void)tegra_hv_ivc_unreserve(comms_handle_priv->ivck);

	devm_kfree(comms_handle_priv->dev, comms_handle_priv);

exit:
	return status;
}

CommsLibStatus_t comms_lib_deinit(void)
{
	/* No need to deinitialize the channel */
	return COMMS_LIB_STATUS_SUCCESS;
}

CommsLibStatus_t comms_lib_connect(CommsLibHandle_t comms_handle, int64_t timeout)
{
	struct comms_handle_priv *handle_priv;
	int err = 0;
	CommsLibStatus_t status = COMMS_LIB_STATUS_SUCCESS;

	if (comms_handle == NULL) {
		NVVSE_ERR("comms_handle is NULL\n");
		err = -EINVAL;
		status = ConvertLinuxErrorToCommsLibStatus(err);
		goto exit;
	}
	handle_priv = (struct comms_handle_priv *)comms_handle;

	while (tegra_hv_ivc_channel_notified(handle_priv->ivck) != 0) {
		if (!timeout) {
			NVVSE_ERR("tegra_hv_ivc_channel_notified timeout\n");
			err = -EINVAL;
			status = ConvertLinuxErrorToCommsLibStatus(err);
			goto exit;
		}
		udelay(1);
		timeout--;
	}

exit:
	return status;
}

CommsLibStatus_t comms_lib_disconnect(CommsLibHandle_t comms_handle)
{
	/* No need to disconnect the channel */
	return COMMS_LIB_STATUS_SUCCESS;
}

CommsLibStatus_t comms_lib_reset(CommsLibHandle_t comms_handle)
{
	/* No need to reset the channel */
	return COMMS_LIB_STATUS_SUCCESS;
}

CommsLibStatus_t comms_lib_wait_for_comms_event(CommsLibHandle_t comms_handle,
			int64_t timeout_ms, uint32_t event_type)
{
	struct comms_handle_priv *handle_priv;
	int err = 0;
	CommsLibStatus_t status = COMMS_LIB_STATUS_SUCCESS;
	int64_t timeout = timeout_ms;

	if (comms_handle == NULL) {
		NVVSE_ERR("comms_handle is NULL\n");
		err = -EINVAL;
		status = ConvertLinuxErrorToCommsLibStatus(err);
		goto exit;
	}
	handle_priv = (struct comms_handle_priv *)comms_handle;

	if ((event_type != IPC_READ_EVENT) && (event_type != IPC_WRITE_EVENT)) {
		NVVSE_ERR("Invalid event_type: %u\n", event_type);
		err = -EINVAL;
		status = ConvertLinuxErrorToCommsLibStatus(err);
		goto exit;
	}

	while (tegra_hv_ivc_channel_notified(handle_priv->ivck) != 0) {
		if (!timeout) {
			NVVSE_ERR("tegra_hv_ivc_channel_notified timeout\n");
			err = -EINVAL;
			status = ConvertLinuxErrorToCommsLibStatus(err);
			goto exit;
		}
		udelay(1);
		timeout--;
	}

	timeout = timeout_ms;

	if (event_type == IPC_WRITE_EVENT) {
		while (tegra_hv_ivc_can_write(handle_priv->ivck) == 0) {
			if (!timeout) {
				NVVSE_ERR("ivc send message timeout\n");
				err = -EINVAL;
				status = ConvertLinuxErrorToCommsLibStatus(err);
				goto exit;
			}
			udelay(1);
			timeout--;
		}
	} else {
		while (tegra_hv_ivc_can_read(handle_priv->ivck) == 0) {
			if (!timeout) {
				NVVSE_ERR("ivc receive message timeout\n");
				err = -EINVAL;
				status = ConvertLinuxErrorToCommsLibStatus(err);
				goto exit;
			}
			udelay(1);
			timeout--;
		}
	}
exit:
	return status;
}

CommsLibStatus_t comms_lib_send_message(CommsLibHandle_t comms_handle,
			uint32_t message_size, const void *message, uint32_t *size_written)
{
	struct comms_handle_priv *handle_priv;
	int err = 0;
	CommsLibStatus_t status = COMMS_LIB_STATUS_SUCCESS;

	if (comms_handle == NULL) {
		NVVSE_ERR("comms_handle is NULL\n");
		err = -EINVAL;
		status = ConvertLinuxErrorToCommsLibStatus(err);
		goto exit;
	}
	handle_priv = (struct comms_handle_priv *)comms_handle;

	if (message == NULL) {
		NVVSE_ERR("message is NULL\n");
		err = -EINVAL;
		status = ConvertLinuxErrorToCommsLibStatus(err);
		goto exit;
	}

	if (size_written == NULL) {
		NVVSE_ERR("size_written is NULL\n");
		err = -EINVAL;
		status = ConvertLinuxErrorToCommsLibStatus(err);
		goto exit;
	}

	err = tegra_hv_ivc_write(handle_priv->ivck, message, message_size);
	if (err < 0) {
		NVVSE_ERR("ivc write error!!! error=%d\n", err);
		status = ConvertLinuxErrorToCommsLibStatus(err);
		goto exit;
	}
	*size_written = err;

exit:
	return status;
}

CommsLibStatus_t comms_lib_receive_message(CommsLibHandle_t comms_handle,
			uint32_t message_size, void *message, uint32_t *size_read)
{
	struct comms_handle_priv *handle_priv;
	int err = 0;
	CommsLibStatus_t status = COMMS_LIB_STATUS_SUCCESS;

	if (comms_handle == NULL) {
		NVVSE_ERR("comms_handle is NULL\n");
		err = -EINVAL;
		status = ConvertLinuxErrorToCommsLibStatus(err);
		goto exit;
	}
	handle_priv = (struct comms_handle_priv *)comms_handle;

	if (message == NULL) {
		NVVSE_ERR("message is NULL\n");
		err = -EINVAL;
		status = ConvertLinuxErrorToCommsLibStatus(err);
		goto exit;
	}

	if (size_read == NULL) {
		NVVSE_ERR("size_read is NULL\n");
		err = -EINVAL;
		status = ConvertLinuxErrorToCommsLibStatus(err);
		goto exit;
	}

	memset(message, 0, message_size);
	err = tegra_hv_ivc_read(handle_priv->ivck, message, message_size);
	/* Partial read is not supported */
	if ((err > 0 && err < message_size) || (err < 0)) {
		NVVSE_ERR("ivc read error!!! error=%d\n", err);
		err = -EINVAL;
		status = ConvertLinuxErrorToCommsLibStatus(err);
		goto exit;
	}
	*size_read = err;

exit:
	return status;
}
