// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/miscdevice.h>
#include <linux/io.h>
#include <uapi/misc/nvtzvault-ioctl.h>
#include "nvtzvault-helper.h"
#include "oesp-mailbox.h"
#include "nvtzvault-common.h"

#define NVTZVAULT_MAX_TA_ID		(99U)
#define NVTZVAULT_MAX_DEV_COUNT		(40U)
#define NVTZVAULT_TA_UUID_LEN		(16U)
#define NVTZVAULT_TA_DEVICE_NAME_LEN	(17U)
#define NVTZVAULT_BUFFER_SIZE		(8192U)
#define NVTZVAULT_MAX_SESSIONS		(72U)

enum nvtzvault_session_op_type {
	NVTZVAULT_SESSION_OP_OPEN,
	NVTZVAULT_SESSION_OP_INVOKE,
	NVTZVAULT_SESSION_OP_CLOSE
};

struct nvtzvault_session_req_hdr {
	enum nvtzvault_session_op_type op;
	uint32_t session_id;
	uint32_t guest_id;
};

struct nvtzvault_session_resp_hdr {
	uint32_t result;
	uint32_t session_id;
};

struct nvtzvault_ta {
	struct miscdevice *dev;
	uint8_t uuid[NVTZVAULT_TA_UUID_LEN];
	uint32_t id;
	uint32_t task_opcode;
	uint32_t driver_id;
};

struct nvtzvault_dev {
	struct nvtzvault_ta ta[NVTZVAULT_MAX_DEV_COUNT];
	uint32_t ta_count;
	struct device *dev;
	struct mutex lock;
	void *data_buf;
	atomic_t in_suspend_state;
	atomic_t total_active_session_count;
} g_nvtzvault_dev;

struct nvtzvault_ctx {
	bool is_session_open;
	uint32_t node_id;
	struct nvtzvault_tee_buf_context buf_ctx;
	uint8_t session_bitmap[NVTZVAULT_MAX_SESSIONS / 8];
	uint32_t task_opcode;
	uint32_t driver_id;
};

static bool is_session_open(struct nvtzvault_ctx *ctx, uint32_t session_id)
{
	uint32_t byte_idx = session_id / 8U;
	uint32_t bit_idx = session_id % 8U;

	if (session_id >= NVTZVAULT_MAX_SESSIONS) {
		pr_err("%s: invalid session id %u\n", __func__, session_id);
		return false;
	}

	return (ctx->session_bitmap[byte_idx] & (1U << bit_idx)) != 0;
}

static void set_session_open(struct nvtzvault_ctx *ctx, uint32_t session_id)
{
	uint32_t byte_idx;
	uint32_t bit_idx;
	uint32_t bitmap_val;

	if (session_id >= NVTZVAULT_MAX_SESSIONS) {
		pr_err("%s: invalid session id %u\n", __func__, session_id);
		return;
	}

	byte_idx = session_id / 8U;
	bit_idx = session_id % 8U;
	bitmap_val = (1U << bit_idx);

	if (bitmap_val > UINT8_MAX) {
		pr_err("%s: bitmap_val overflow %u\n", __func__, bitmap_val);
		return;
	}

	ctx->session_bitmap[byte_idx] |= (uint8_t)bitmap_val;
	atomic_inc(&g_nvtzvault_dev.total_active_session_count);
}

static void set_session_closed(struct nvtzvault_ctx *ctx, uint32_t session_id)
{
	uint32_t byte_idx;
	uint32_t bit_idx;
	uint32_t bitmap_val;

	if (session_id >= NVTZVAULT_MAX_SESSIONS) {
		pr_err("%s: invalid session id %u\n", __func__, session_id);
		return;
	}

	byte_idx = session_id / 8U;
	bit_idx = session_id % 8U;
	bitmap_val = (1U << bit_idx);

	if (bitmap_val > UINT8_MAX) {
		pr_err("%s: bitmap_val overflow %u\n", __func__, bitmap_val);
		return;
	}

	ctx->session_bitmap[byte_idx] &= ~((uint8_t)bitmap_val);

	atomic_dec(&g_nvtzvault_dev.total_active_session_count);
}

static int nvtzvault_write_process_name(struct nvtzvault_tee_buf_context *ctx)
{
	char padded_name[12];
	size_t name_len;
	int ret;

	// Get actual process name length (max 16 chars as per TASK_COMM_LEN)
	name_len = strnlen(current->comm, 16);

	// Copy process name (up to 12 chars) and zero-pad the rest
	memset(padded_name, 0, sizeof(padded_name));
	memcpy(padded_name, current->comm, min(name_len, (size_t)12));

	ret = nvtzvault_tee_check_overflow_and_write(ctx, padded_name, 12, false);
	if (ret != 0)
		NVTZVAULT_ERR("%s: Failed to write process name: %d\n", __func__, ret);

	return ret;
}

static int nvtzvault_open_session(struct nvtzvault_ctx *ctx,
			struct nvtzvault_open_session_ctl *open_session_ctl)
{
	int ret = 0;
	struct nvtzvault_session_req_hdr req_hdr = {NVTZVAULT_SESSION_OP_OPEN, 0xFFFFFFFFU, 0U};
	struct nvtzvault_session_resp_hdr resp_hdr;
	uint32_t cmd_id;

	nvtzvault_tee_buf_context_reset(&ctx->buf_ctx);
	// Write header
	ret = nvtzvault_tee_check_overflow_and_write(&ctx->buf_ctx, &req_hdr,
			sizeof(struct nvtzvault_session_req_hdr), false);
	if (ret != 0) {
		NVTZVAULT_ERR("%s: Failed to write request header: %d\n", __func__, ret);
		return ret;
	}

	// prepare request and serialize parameters
	ret = nvtzvault_tee_write_all_params(&ctx->buf_ctx, 0xFFFFFFFF,
			open_session_ctl->operation.param_types,
			open_session_ctl->operation.params);
	if (ret != 0) {
		NVTZVAULT_ERR("%s: Failed to write parameters: %d\n", __func__, ret);
		return ret;
	}

	// Write process name (12 chars, zero-padded if needed)
	ret = nvtzvault_write_process_name(&ctx->buf_ctx);
	if (ret != 0)
		return ret;

	// trigger mailbox send, wait for response
	ret = oesp_mailbox_send_and_read(ctx->buf_ctx.buf_ptr, ctx->buf_ctx.current_offset,
			ctx->task_opcode, ctx->driver_id);
	if (ret != 0) {
		NVTZVAULT_ERR("%s: Failed to read mailbox response: %d\n", __func__, ret);
		return ret;
	}

	nvtzvault_tee_buf_context_reset(&ctx->buf_ctx);

	// Read resp header
	ret = nvtzvault_tee_check_overflow_and_read(&ctx->buf_ctx, &resp_hdr,
			sizeof(struct nvtzvault_session_resp_hdr), false);
	if (ret != 0) {
		NVTZVAULT_ERR("%s: Failed to read response header: %d\n", __func__, ret);
		return ret;
	}

	ret = nvtzvault_tee_translate_saerror_to_syserror(
			resp_hdr.result);
	if (ret != 0) {
		NVTZVAULT_ERR("%s: SA returned error: %d(0x%x)\n", __func__, resp_hdr.result,
				resp_hdr.result);
		return ret;
	}

	// Validate session isn't already open
	if (is_session_open(ctx, resp_hdr.session_id)) {
		NVTZVAULT_ERR("%s: Session %u already open\n", __func__, resp_hdr.session_id);
		return -EINVAL;
	}

	// Track the newly opened session
	set_session_open(ctx, resp_hdr.session_id);

	ret = nvtzvault_tee_read_all_params(&ctx->buf_ctx, &cmd_id,
			&open_session_ctl->operation.param_types,
			open_session_ctl->operation.params);
	if (ret != 0) {
		NVTZVAULT_ERR("%s: Failed to read parameters: %d\n", __func__, ret);
		return ret;
	}

	open_session_ctl->session_id = resp_hdr.session_id;

	return ret;
}

static int nvtzvault_invoke_cmd(struct nvtzvault_ctx *ctx,
		struct nvtzvault_invoke_cmd_ctl *invoke_cmd_ctl)
{
	int ret = 0;
	struct nvtzvault_session_req_hdr req_hdr = {NVTZVAULT_SESSION_OP_INVOKE,
			invoke_cmd_ctl->session_id, 0U};
	struct nvtzvault_session_resp_hdr resp_hdr;
	uint32_t cmd_id;

	nvtzvault_tee_buf_context_reset(&ctx->buf_ctx);

	// Validate session is open
	if (!is_session_open(ctx, invoke_cmd_ctl->session_id)) {
		NVTZVAULT_ERR("%s: Session %u not open\n", __func__, invoke_cmd_ctl->session_id);
		return -EINVAL;
	}

	// Write header
	ret = nvtzvault_tee_check_overflow_and_write(&ctx->buf_ctx, &req_hdr,
			sizeof(struct nvtzvault_session_req_hdr), false);
	if (ret != 0) {
		NVTZVAULT_ERR("%s: Failed to write request header: %d\n", __func__, ret);
		return ret;
	}

	// Prepare request and serialize parameters
	ret = nvtzvault_tee_write_all_params(&ctx->buf_ctx, invoke_cmd_ctl->command_id,
			invoke_cmd_ctl->operation.param_types, invoke_cmd_ctl->operation.params);
	if (ret != 0) {
		NVTZVAULT_ERR("%s: Failed to write parameters: %d\n", __func__, ret);
		return ret;
	}

	// Write process name (12 chars, zero-padded if needed)
	ret = nvtzvault_write_process_name(&ctx->buf_ctx);
	if (ret != 0)
		return ret;

	// Trigger mailbox send, wait for response
	ret = oesp_mailbox_send_and_read(ctx->buf_ctx.buf_ptr, ctx->buf_ctx.current_offset,
			ctx->task_opcode, ctx->driver_id);
	if (ret != 0) {
		NVTZVAULT_ERR("%s: Failed to read mailbox response: %d\n", __func__, ret);
		return ret;
	}

	nvtzvault_tee_buf_context_reset(&ctx->buf_ctx);

	// Read response header
	ret = nvtzvault_tee_check_overflow_and_read(&ctx->buf_ctx, &resp_hdr,
			sizeof(struct nvtzvault_session_resp_hdr), false);
	if (ret != 0) {
		NVTZVAULT_ERR("%s: Failed to read response header: %d\n", __func__, ret);
		return ret;
	}

	ret = nvtzvault_tee_translate_saerror_to_syserror(
			resp_hdr.result);
	if (ret != 0) {
		NVTZVAULT_ERR("%s: SA returned error: %d(0x%x)\n", __func__, resp_hdr.result,
				resp_hdr.result);
		return ret;
	}

	// Read response parameters
	ret = nvtzvault_tee_read_all_params(&ctx->buf_ctx, &cmd_id,
			&invoke_cmd_ctl->operation.param_types, invoke_cmd_ctl->operation.params);
	if (ret != 0) {
		NVTZVAULT_ERR("%s: Failed to read parameters: %d\n", __func__, ret);
		return ret;
	}

	return ret;
}

static int nvtzvault_close_session(struct nvtzvault_ctx *ctx,
			struct nvtzvault_close_session_ctl *close_session_ctl)
{
	int ret = 0;
	struct nvtzvault_session_req_hdr req_hdr = {NVTZVAULT_SESSION_OP_CLOSE,
			close_session_ctl->session_id, 0U};
	struct nvtzvault_session_resp_hdr resp_hdr;

	nvtzvault_tee_buf_context_reset(&ctx->buf_ctx);

	// Validate session is open
	if (!is_session_open(ctx, close_session_ctl->session_id)) {
		NVTZVAULT_ERR("%s: Session %u not open\n", __func__, close_session_ctl->session_id);
		return -EINVAL;
	}

	// Write header
	ret = nvtzvault_tee_check_overflow_and_write(&ctx->buf_ctx, &req_hdr,
			sizeof(struct nvtzvault_session_req_hdr), false);
	if (ret != 0) {
		NVTZVAULT_ERR("%s: Failed to write request header: %d\n", __func__, ret);
		return ret;
	}

	// Prepare request and serialize parameters with no params
	ret = nvtzvault_tee_write_all_params(&ctx->buf_ctx, 0xFFFFFFFF, 0U, NULL);
	if (ret != 0) {
		NVTZVAULT_ERR("%s: Failed to write parameters: %d\n", __func__, ret);
		return ret;
	}

	// Write process name (12 chars, zero-padded if needed)
	ret = nvtzvault_write_process_name(&ctx->buf_ctx);
	if (ret != 0)
		return ret;

	// Trigger mailbox send, wait for response
	ret = oesp_mailbox_send_and_read(ctx->buf_ctx.buf_ptr, ctx->buf_ctx.current_offset,
			ctx->task_opcode, ctx->driver_id);
	if (ret != 0) {
		NVTZVAULT_ERR("%s: Failed to read mailbox response: %d\n", __func__, ret);
		return ret;
	}

	nvtzvault_tee_buf_context_reset(&ctx->buf_ctx);

	// Read response header
	ret = nvtzvault_tee_check_overflow_and_read(&ctx->buf_ctx, &resp_hdr,
			sizeof(struct nvtzvault_session_resp_hdr), false);
	if (ret != 0) {
		NVTZVAULT_ERR("%s: Failed to read response header: %d\n", __func__, ret);
		return ret;
	}

	ret = nvtzvault_tee_translate_saerror_to_syserror(
			resp_hdr.result);
	if (ret == 0) {
		// Only clear session if close was successful
		set_session_closed(ctx, close_session_ctl->session_id);
	} else {
		NVTZVAULT_ERR("%s: SA returned error: %d(0x%x)\n", __func__, resp_hdr.result,
				resp_hdr.result);
	}

	return ret;
}

static int nvtzvault_ta_dev_open(struct inode *inode, struct file *filp)
{
	struct miscdevice *misc;
	struct nvtzvault_ctx *ctx = NULL;
	int32_t ret;

	misc = filp->private_data;

	ctx = kzalloc(sizeof(struct nvtzvault_ctx), GFP_KERNEL);
	if (!ctx) {
		NVTZVAULT_ERR("%s: Failed to allocate context memory\n", __func__);
		return -ENOMEM;
	}

	ctx->node_id = misc->this_device->id;
	ctx->task_opcode = g_nvtzvault_dev.ta[ctx->node_id].task_opcode;
	ctx->driver_id = g_nvtzvault_dev.ta[ctx->node_id].driver_id;
	ctx->is_session_open = false;

	ret = nvtzvault_tee_buf_context_init(&ctx->buf_ctx, g_nvtzvault_dev.data_buf,
			NVTZVAULT_BUFFER_SIZE);
	if (ret != 0) {
		NVTZVAULT_ERR("%s: Failed to initialize buffer context\n", __func__);
		kfree(ctx);
		return ret;
	}
	memset(ctx->session_bitmap, 0, sizeof(ctx->session_bitmap));

	filp->private_data = ctx;

	return 0;
}

static int nvtzvault_ta_dev_release(struct inode *inode, struct file *filp)
{
	struct nvtzvault_ctx *ctx = filp->private_data;
	struct nvtzvault_close_session_ctl close_session_ctl;

	if (atomic_read(&g_nvtzvault_dev.total_active_session_count) > 0) {
		mutex_lock(&g_nvtzvault_dev.lock);
		for (uint32_t i = 0; i < NVTZVAULT_MAX_SESSIONS; i++) {
			if (is_session_open(ctx, i)) {
				close_session_ctl.session_id = i;
				nvtzvault_close_session(ctx, &close_session_ctl);
			}
		}
		mutex_unlock(&g_nvtzvault_dev.lock);
	}

	kfree(ctx);

	return 0;
}

static long nvtzvault_ta_dev_ioctl(struct file *filp, unsigned int ioctl_num, unsigned long arg)
{
	struct nvtzvault_ctx *ctx = filp->private_data;
	struct nvtzvault_open_session_ctl *open_session_ctl;
	struct nvtzvault_invoke_cmd_ctl *invoke_cmd_ctl;
	struct nvtzvault_close_session_ctl *close_session_ctl;
	int ret = 0;
	uint64_t result;

	if (!ctx) {
		NVTZVAULT_ERR("%s(): ctx not allocated\n", __func__);
		return -EPERM;
	}

	mutex_lock(&g_nvtzvault_dev.lock);

	if (atomic_read(&g_nvtzvault_dev.in_suspend_state)) {
		NVTZVAULT_ERR("%s(): device is in suspend state\n", __func__);
		ret = -EBUSY;
		goto release_lock;
	}

	switch (ioctl_num) {
	case NVTZVAULT_IOCTL_OPEN_SESSION:
		open_session_ctl = kzalloc(sizeof(*open_session_ctl), GFP_KERNEL);
		if (!open_session_ctl) {
			NVTZVAULT_ERR("%s(): failed to allocate memory\n", __func__);
			ret = -ENOMEM;
			goto release_lock;
		}

		ret = copy_from_user(open_session_ctl, (void __user *)arg,
				sizeof(*open_session_ctl));
		if (ret) {
			NVTZVAULT_ERR("%s(): Failed to copy_from_user open_session_ctl:%d\n",
					__func__, ret);
			kfree(open_session_ctl);
			goto release_lock;
		}

		ret = nvtzvault_open_session(ctx, open_session_ctl);
		if (ret) {
			NVTZVAULT_ERR("%s(): nvtzvault_open_session failed:%d\n", __func__, ret);
			kfree(open_session_ctl);
			goto release_lock;
		}

		result = copy_to_user((void __user *)arg, open_session_ctl,
				sizeof(*open_session_ctl));
		if (result != 0UL) {
			NVTZVAULT_ERR("%s(): Failed to copy_from_user open_session_ctl:%d\n",
					__func__, ret);
			kfree(open_session_ctl);
			goto release_lock;
		}
		kfree(open_session_ctl);
		break;
	case NVTZVAULT_IOCTL_INVOKE_CMD:
		invoke_cmd_ctl = kzalloc(sizeof(*invoke_cmd_ctl), GFP_KERNEL);
		if (!invoke_cmd_ctl) {
			NVTZVAULT_ERR("%s(): failed to allocate memory\n", __func__);
			ret = -ENOMEM;
			goto release_lock;
		}

		ret = copy_from_user(invoke_cmd_ctl, (void __user *)arg, sizeof(*invoke_cmd_ctl));
		if (ret) {
			NVTZVAULT_ERR("%s(): Failed to copy_from_user invoke_cmd_ctl:%d\n",
					__func__, ret);
			kfree(invoke_cmd_ctl);
			goto release_lock;
		}

		ret = nvtzvault_invoke_cmd(ctx, invoke_cmd_ctl);
		if (ret) {
			NVTZVAULT_ERR("%s(): nvtzvault_invoke_cmd failed:%d\n", __func__, ret);
			kfree(invoke_cmd_ctl);
			goto release_lock;
		}

		result = copy_to_user((void __user *)arg, invoke_cmd_ctl, sizeof(*invoke_cmd_ctl));
		if (result != 0UL) {
			NVTZVAULT_ERR("%s(): Failed to copy_from_user invoke_cmd_ctl:%d\n",
					__func__, ret);
			kfree(invoke_cmd_ctl);
			goto release_lock;
		}
		kfree(invoke_cmd_ctl);
		break;
	case NVTZVAULT_IOCTL_CLOSE_SESSION:
		close_session_ctl = kzalloc(sizeof(*close_session_ctl), GFP_KERNEL);
		if (!close_session_ctl) {
			NVTZVAULT_ERR("%s(): failed to allocate memory\n", __func__);
			ret = -ENOMEM;
			goto release_lock;
		}

		ret = copy_from_user(close_session_ctl, (void __user *)arg,
				sizeof(*close_session_ctl));
		if (ret) {
			NVTZVAULT_ERR("%s(): Failed to copy_from_user close_session_ctl:%d\n",
					__func__, ret);
			kfree(close_session_ctl);
			goto release_lock;
		}

		ret = nvtzvault_close_session(ctx, close_session_ctl);
		if (ret) {
			NVTZVAULT_ERR("%s(): nvtzvault_close_session failed:%d\n", __func__, ret);
			kfree(close_session_ctl);
			goto release_lock;
		}

		result = copy_to_user((void __user *)arg, close_session_ctl,
				sizeof(*close_session_ctl));
		if (result != 0UL) {
			NVTZVAULT_ERR("%s(): Failed to copy_from_user close_session_ctl:%d\n",
					__func__, ret);
			kfree(close_session_ctl);
			goto release_lock;
		}
		kfree(close_session_ctl);
		break;
	default:
		NVTZVAULT_ERR("%s(): Unsupported IOCTL command", __func__);
		ret = -EINVAL;
		break;
	}

release_lock:
	mutex_unlock(&g_nvtzvault_dev.lock);

	return ret;
}

static const struct file_operations nvtzvault_ta_fops = {
	.owner			= THIS_MODULE,
	.open			= nvtzvault_ta_dev_open,
	.release		= nvtzvault_ta_dev_release,
	.unlocked_ioctl		= nvtzvault_ta_dev_ioctl,
};

static int nvtzvault_validate_ta_params(uint32_t const ta_id, uint8_t const * const ta_uuid,
		uint32_t const ta_count)
{
	struct nvtzvault_ta *ta;
	uint32_t i;

	if (ta_id >= ta_count) {
		NVTZVAULT_ERR("%s: invalid ta id %u\n", __func__, ta_id);
		return -EINVAL;
	}

	if (ta_id > NVTZVAULT_MAX_TA_ID) {
		NVTZVAULT_ERR("%s: unsupported ta id %u\n", __func__, ta_id);
		return -EINVAL;
	}

	for (i = 0U; i < g_nvtzvault_dev.ta_count; i++) {
		ta = &g_nvtzvault_dev.ta[i];

		if (ta_id == ta->id) {
			NVTZVAULT_ERR("%s: ta id %u is already used\n", __func__, ta_id);
			return -EINVAL;
		}

		if (memcmp(ta_uuid, ta->uuid, NVTZVAULT_TA_UUID_LEN) == 0) {
			NVTZVAULT_ERR("%s: uuid for ta id %u is already used for ta id %u\n",
					__func__, ta_id, i);
			return -EINVAL;
		}
	}

	return 0;
}

static int nvtzvault_ta_create_dev_node(struct miscdevice *dev, uint32_t id)
{
	const char * const node_prefix = "nvtzvault-ta-";
	char *node_name;
	char const numbers[] = "0123456789";
	uint64_t result = 0UL;
	uint32_t str_len;
	int32_t ret;

	result = strlen(node_prefix);
	if (result > UINT32_MAX) {
		NVTZVAULT_ERR("%s: device name length is invalid", __func__);
		return -EINVAL;
	}

	str_len = (uint32_t)result;
	if (str_len > (NVTZVAULT_TA_DEVICE_NAME_LEN - 3U)) {
		NVTZVAULT_ERR("%s: device name length exceeds supported size", __func__);
		return -ENOMEM;
	}

	node_name = kzalloc(NVTZVAULT_TA_DEVICE_NAME_LEN, GFP_KERNEL);
	if (node_name == NULL)
		return -ENOMEM;

	dev->minor = MISC_DYNAMIC_MINOR;
	dev->fops = &nvtzvault_ta_fops;

	(void)memcpy(node_name, node_prefix, str_len);
	node_name[str_len++] = numbers[id / 10U];
	node_name[str_len++] = numbers[id % 10U];
	node_name[str_len++] = '\0';
	dev->name = node_name;

	ret = misc_register(dev);
	if (ret != 0) {
		NVTZVAULT_ERR("%s: misc dev %u registration failed err %d\n", __func__, id, ret);
		kfree(node_name);
		return -ENOMEM;
	}

	dev->this_device->id = id;

	return 0;
}

static int nvtzvault_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *nvtzvault_node = dev->of_node;
	struct device_node *ta_node;
	struct miscdevice *misc;
	struct nvtzvault_ta *ta;
	u32 ta_count;
	u32 offset = 0U;
	phandle ta_phandle;
	phandle ta_id;
	u8 ta_uuid[NVTZVAULT_TA_UUID_LEN];
	uint32_t task_opcode;
	uint32_t driver_id;
	int len, i;
	int ret;

	dev_info(dev, "probe start\n");

	if (!nvtzvault_node)
		return -ENODEV;

	ret = oesp_mailbox_init(pdev);
	if (ret) {
		dev_err(dev, "failed to initialize mailbox\n");
		return ret;
	}

	// Read the 'ta-mapping' property from supported-tas
	if (!of_get_property(nvtzvault_node, "ta-mapping", &len)) {
		dev_err(dev, "ta-mapping property missing\n");
		return -EINVAL;
	}

	if ((len % 2) != 0) {
		dev_err(dev, "ta-mapping property has invalid format\n");
		return -EINVAL;
	}

	ta_count = (len / (sizeof(u32) * 2U));

	if (ta_count >= NVTZVAULT_MAX_DEV_COUNT) {
		dev_err(dev, "ta count exceeds max supported value\n");
		return -EINVAL;
	}

	// Iterate over the entries in the 'supported-tas/config' array
	for (i = 0; i < ta_count; i++) {
		ta = &g_nvtzvault_dev.ta[i];
		offset = i * 2U;

		misc = kzalloc(sizeof(struct miscdevice), GFP_KERNEL);
		if (misc == NULL) {
			ret = -ENOMEM;
			goto fail;
		}
		ta->dev = misc;

		// Read the TA phandle
		if (of_property_read_u32_index(nvtzvault_node, "ta-mapping", offset,
				&ta_phandle)) {
			dev_err(dev, "failed to get ta phandle\n");
			ret = -EINVAL;
			goto fail;
		}

		ta_node = of_find_node_by_phandle(ta_phandle);
		if (!ta_node) {
			dev_err(dev, "ta node not found\n");
			ret = -EINVAL;
			goto fail;
		}

		// Read the TA ID
		if (of_property_read_u32_index(nvtzvault_node, "ta-mapping", (offset + 1U),
				&ta_id)) {
			dev_err(dev, "failed to get ta id\n");
			ret = -EINVAL;
			goto fail;
		}

		if (of_property_read_u8_array(ta_node, "uuid", ta_uuid, NVTZVAULT_TA_UUID_LEN)) {
			dev_err(dev, "failed to get ta uuid\n");
			ret = -EINVAL;
			goto fail;
		}

		if (of_property_read_u32(ta_node, "op-code", &task_opcode)) {
			dev_err(dev, "failed to get ta task-opcode\n");
			ret = -EINVAL;
			goto fail;
		}

		if (of_property_read_u32(ta_node, "driver-id", &driver_id)) {
			dev_err(dev, "failed to get ta driver-id\n");
			ret = -EINVAL;
			goto fail;
		}

		ret = nvtzvault_validate_ta_params(ta_id, ta_uuid, ta_count);
		if (ret != 0)
			goto fail;

		ta->id = ta_id;
		(void)memcpy(ta->uuid, ta_uuid, NVTZVAULT_TA_UUID_LEN);
		ta->task_opcode = task_opcode;
		ta->driver_id = driver_id;

		ret = nvtzvault_ta_create_dev_node(ta->dev, ta->id);
		if (ret != 0)
			goto fail;

		g_nvtzvault_dev.ta_count = (i + 1U);
	}

	g_nvtzvault_dev.data_buf = kzalloc(NVTZVAULT_BUFFER_SIZE, GFP_KERNEL);
	if (!g_nvtzvault_dev.data_buf) {
		dev_err(dev, "failed to allocate data buffer\n");
		ret = -ENOMEM;
		goto fail;
	}

	atomic_set(&g_nvtzvault_dev.total_active_session_count, 0);

	mutex_init(&g_nvtzvault_dev.lock);

	dev_info(dev, "probe success\n");

	return 0;

fail:
	for (i = 0; i < g_nvtzvault_dev.ta_count; i++) {
		ta = &g_nvtzvault_dev.ta[i];
		if (ta->dev) {
			misc_deregister(ta->dev);
			kfree(ta->dev->name);
			kfree(ta->dev);
		}
	}

	return ret;
}

static int nvtzvault_remove(struct platform_device *pdev)
{
	struct nvtzvault_ta *ta;
	uint32_t i;

	for (i = 0; i < g_nvtzvault_dev.ta_count; i++) {
		ta = &g_nvtzvault_dev.ta[i];
		if (ta->dev) {
			misc_deregister(ta->dev);
			kfree(ta->dev);
		}
	}

	kfree(g_nvtzvault_dev.data_buf);

	return 0;
}

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void nvtzvault_remove_wrapper(struct platform_device *pdev)
{
	nvtzvault_remove(pdev);
}
#else
static int nvtzvault_remove_wrapper(struct platform_device *pdev)
{
	return nvtzvault_remove(pdev);
}
#endif

static void nvtzvault_shutdown(struct platform_device *pdev)
{
	atomic_set(&g_nvtzvault_dev.in_suspend_state, 1);
}

#if defined(CONFIG_PM)
static int nvtzvault_suspend(struct device *dev)
{
	/* Add print to log in nvlog buffer  */
	dev_err(dev, "%s start\n", __func__);

	mutex_lock(&g_nvtzvault_dev.lock);

	if (atomic_read(&g_nvtzvault_dev.total_active_session_count) > 0) {
		mutex_unlock(&g_nvtzvault_dev.lock);
		return -EBUSY;
	}

	atomic_set(&g_nvtzvault_dev.in_suspend_state, 1);

	mutex_unlock(&g_nvtzvault_dev.lock);

	/* Add print to log in nvlog buffer  */
	dev_err(dev, "%s done\n", __func__);

	return 0;
}

static int nvtzvault_resume(struct device *dev)
{
	/* Add print to log in nvlog buffer  */
	dev_err(dev, "%s start\n", __func__);
	atomic_set(&g_nvtzvault_dev.in_suspend_state, 0);
	/* Add print to log in nvlog buffer  */
	dev_err(dev, "%s done\n", __func__);
	return 0;
}
static const struct dev_pm_ops nvtzvault_pm_ops = {
	.suspend = nvtzvault_suspend,
	.resume = nvtzvault_resume,
};

#endif /* CONFIG_PM */


static const struct of_device_id nvtzvault_match[] = {
	{.compatible = "nvidia,nvtzvault"},
	{}
};
MODULE_DEVICE_TABLE(of, nvtzvault_match);

static struct platform_driver nvtzvault_driver = {
	.probe		= nvtzvault_probe,
	.remove		= nvtzvault_remove_wrapper,
	.shutdown	= nvtzvault_shutdown,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "nvtzvault",
		.of_match_table = of_match_ptr(nvtzvault_match),
		.pm = &nvtzvault_pm_ops,
	}
};

module_platform_driver(nvtzvault_driver);

MODULE_DESCRIPTION("NVIDIA TZVault driver");
MODULE_AUTHOR("Nvidia Corporation");
MODULE_LICENSE("GPL v2");
