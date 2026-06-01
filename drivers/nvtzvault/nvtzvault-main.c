// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "nvtzvault-helper.h"
#include "teec-soc-plugin.h"
#include <nvidia/conftest.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/miscdevice.h>
#include <uapi/misc/nvtzvault-ioctl.h>

#define NVTZVAULT_MAX_TA_ID			(99U)
#define NVTZVAULT_MAX_DEV_COUNT			(40U)
#define NVTZVAULT_TA_UUID_LEN			(16U)
#define NVTZVAULT_TA_DEVICE_NAME_LEN		(17U)
#define NVTZVAULT_BUFFER_SIZE			(8192U)
#define NVTZVAULT_MAX_SESSIONS_PER_TOKEN	(8U)
#define NVTZVAULT_MAX_SESSIONS			(NVTZVAULT_MAX_DEV_COUNT * \
							NVTZVAULT_MAX_SESSIONS_PER_TOKEN)
#define NVTZVAULT_PROCESS_NAME_LENGTH		(16U)
#define NVTZVAULT_TIMEOUT_MS			(300U * 1000U)	/* 5 minutes */

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

/**
 * @brief Represents one Trusted Application (TA) with its device node
 * Each TA gets one /dev/nvtzvault-ta-XX device node and communication interface
 */
struct nvtzvault_ta {
	struct miscdevice *dev;
	uint8_t uuid[NVTZVAULT_TA_UUID_LEN];
	uint32_t id;
	uint32_t task_opcode;
	uint32_t driver_id;
	uint8_t session_bitmap[NVTZVAULT_MAX_SESSIONS / 8U];
	atomic_t active_session_count;
	char process_name[NVTZVAULT_MAX_SESSIONS][NVTZVAULT_PROCESS_NAME_LENGTH];
	TeeClient tee_client; /* Per-TA communication interface */
	struct mutex lock;    /* Per-TA mutex for parallel access */
	void *data_buf;       /* Per-TA buffer */
};

/**
 * @brief Global driver state managing all TAs in the system
 * Single instance holds array of all configured Trusted Applications
 */
struct nvtzvault_dev {
	struct nvtzvault_ta ta[NVTZVAULT_MAX_DEV_COUNT];
	uint32_t ta_count;
	struct device *dev;
	atomic_t in_suspend_state;
} g_nvtzvault_dev;

/**
 * @brief Application session context for one specific TA
 * Created when app opens /dev/nvtzvault-ta-XX, points to ta[node_id]
 */
struct nvtzvault_ctx {
	bool is_session_open;
	uint32_t node_id;
	struct nvtzvault_tee_buf_context buf_ctx;
	uint8_t session_bitmap[NVTZVAULT_MAX_SESSIONS / 8U];
	uint32_t task_opcode;
	uint32_t driver_id;
	char process_name[NVTZVAULT_PROCESS_NAME_LENGTH];
};

static bool is_session_open(struct nvtzvault_ctx *ctx, uint32_t session_id)
{
	uint32_t byte_idx = session_id / 8U;
	uint32_t bit_idx = session_id % 8U;

	if (session_id >= NVTZVAULT_MAX_SESSIONS) {
		NVTZVAULT_ERR("%s: invalid session id %u\n", __func__, session_id);
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
		NVTZVAULT_ERR("%s: invalid session id %u\n", __func__, session_id);
		return;
	}

	byte_idx = session_id / 8U;
	bit_idx = session_id % 8U;
	bitmap_val = (1U << bit_idx);

	if (bitmap_val > UINT8_MAX) {
		NVTZVAULT_ERR("%s: bitmap_val overflow %u\n", __func__, bitmap_val);
		return;
	}

	ctx->session_bitmap[byte_idx] |= (uint8_t)bitmap_val;
	g_nvtzvault_dev.ta[ctx->node_id].session_bitmap[byte_idx] |= (uint8_t)bitmap_val;
	atomic_inc(&g_nvtzvault_dev.ta[ctx->node_id].active_session_count);
	memcpy(g_nvtzvault_dev.ta[ctx->node_id].process_name[session_id], ctx->process_name,
			NVTZVAULT_PROCESS_NAME_LENGTH);
}

static void set_session_closed(struct nvtzvault_ctx *ctx, uint32_t session_id)
{
	uint32_t byte_idx;
	uint32_t bit_idx;
	uint32_t bitmap_val;

	if (session_id >= NVTZVAULT_MAX_SESSIONS) {
		NVTZVAULT_ERR("%s: invalid session id %u\n", __func__, session_id);
		return;
	}

	byte_idx = session_id / 8U;
	bit_idx = session_id % 8U;
	bitmap_val = (1U << bit_idx);

	if (bitmap_val > UINT8_MAX) {
		NVTZVAULT_ERR("%s: bitmap_val overflow %u\n", __func__, bitmap_val);
		return;
	}

	ctx->session_bitmap[byte_idx] &= ~((uint8_t)bitmap_val);
	g_nvtzvault_dev.ta[ctx->node_id].session_bitmap[byte_idx] &= ~((uint8_t)bitmap_val);
	memset(g_nvtzvault_dev.ta[ctx->node_id].process_name[session_id], 0,
			NVTZVAULT_PROCESS_NAME_LENGTH);

	atomic_dec(&g_nvtzvault_dev.ta[ctx->node_id].active_session_count);
}

static int nvtzvault_write_process_name(struct nvtzvault_ctx *ctx)
{
	char padded_name[12];
	size_t name_len;
	int ret;
	struct nvtzvault_tee_buf_context *buf_ctx = &ctx->buf_ctx;

	// Get actual process name length (max 16 chars)
	name_len = strnlen(ctx->process_name, NVTZVAULT_PROCESS_NAME_LENGTH);

	// Copy process name (up to 12 chars) and zero-pad the rest
	memset(padded_name, 0, sizeof(padded_name));
	memcpy(padded_name, ctx->process_name, min(name_len, (size_t)12));

	ret = nvtzvault_tee_check_overflow_and_write(buf_ctx, padded_name, 12, false);
	if (ret != 0)
		NVTZVAULT_ERR("%s: Failed to write process name: %d\n", __func__, ret);

	return ret;
}

static void nvtzvault_print_active_sessions(void)
{
	uint32_t byte_idx;
	uint32_t bit_idx;
	uint8_t bitmap_val;
	uint32_t active_session_count;

	for (uint32_t i = 0; i < g_nvtzvault_dev.ta_count; i++) {
		active_session_count = atomic_read(&g_nvtzvault_dev.ta[i].active_session_count);
		if (active_session_count > 0) {
			NVTZVAULT_ERR("%s(): ta %u has %u active sessions\n", __func__, i,
					active_session_count);
			for (uint32_t j = 0; j < NVTZVAULT_MAX_SESSIONS; j++) {
				byte_idx = j / 8U;
				bit_idx = j % 8U;
				bitmap_val = g_nvtzvault_dev.ta[i].session_bitmap[byte_idx];

				if (bitmap_val & (1U << bit_idx)) {
					NVTZVAULT_ERR("%s(): session %u is opened by process %s\n",
							__func__, j,
							g_nvtzvault_dev.ta[i].process_name[j]);
				}
			}
		}
	}
}

/**
 * @brief Common SOC plugin message exchange function
 *
 * Handles the common pattern of SOC plugin communication:
 * - Performs send/receive via TeeClient interface
 *
 * @param ctx Application context with prepared message buffer
 * @return 0 on success, negative error code on failure
 *
 */
static int nvtzvault_soc_plugin_exchange(struct nvtzvault_ctx *ctx)
{
	struct nvtzvault_ta *ta;
	TeeClientStatus tee_status;

	ta = &g_nvtzvault_dev.ta[ctx->node_id];

	/* SOC PLUGIN SEQUENCE: Use abstracted TeeClient interface for hardware communication */
	/* Step 1: Acquire lock */
	tee_status = ta->tee_client.teec_comms_acq_lock(ta->tee_client.tee_comms_priv);
	if (tee_status != TEE_CLIENT_STATUS_OK) {
		NVTZVAULT_ERR("%s: Failed to acquire TeeClient lock: %d\n", __func__, tee_status);
		return nvtzvault_tee_translate_teeclient_to_syserror(tee_status);
	}

	/* Step 2: Send request data to TEE via hardware transport */
	tee_status = ta->tee_client.teec_comms_send_msg(ta->tee_client.tee_comms_priv,
			ctx->buf_ctx.buf_ptr, ctx->buf_ctx.current_offset);
	if (tee_status != TEE_CLIENT_STATUS_OK) {
		NVTZVAULT_ERR("%s: Failed to send message via TeeClient: %d\n", __func__, tee_status);
		goto release_lock;
	}

	/* Step 3: Wait for TEE processing completion (5-minute timeout for slow operations) */
	tee_status = ta->tee_client.teec_comms_wait_event(ta->tee_client.tee_comms_priv,
			NVTZVAULT_TIMEOUT_MS);
	if (tee_status != TEE_CLIENT_STATUS_OK) {
		NVTZVAULT_ERR("%s: Failed to wait for response via TeeClient: %d\n",
				__func__, tee_status);
		goto release_lock;
	}

	/* Step 4: Read response data back from TEE (reuse request buffer size) */
	tee_status = ta->tee_client.teec_comms_read_msg(ta->tee_client.tee_comms_priv,
			ctx->buf_ctx.buf_ptr, ctx->buf_ctx.current_offset);
	if (tee_status != TEE_CLIENT_STATUS_OK) {
		NVTZVAULT_ERR("%s: Failed to read message via TeeClient: %d\n", __func__, tee_status);
		goto release_lock;
	}

release_lock:
	/* Step 5: Release lock */
	ta->tee_client.teec_comms_rel_lock(ta->tee_client.tee_comms_priv);
	return nvtzvault_tee_translate_teeclient_to_syserror(tee_status);
}

static int nvtzvault_close_session(struct nvtzvault_ctx *ctx,
			struct nvtzvault_close_session_ctl *close_session_ctl)
{
	int ret = 0;
	struct nvtzvault_session_req_hdr req_hdr = {NVTZVAULT_SESSION_OP_CLOSE,
			close_session_ctl->session_id, 0U};
	struct nvtzvault_session_resp_hdr resp_hdr;

	nvtzvault_tee_buf_context_reset(&ctx->buf_ctx);

	// Validate session id
	if (close_session_ctl->session_id >= NVTZVAULT_MAX_SESSIONS) {
		NVTZVAULT_ERR("%s: invalid session id %u\n", __func__,
				close_session_ctl->session_id);
		return -EINVAL;
	}

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
	ret = nvtzvault_write_process_name(ctx);
	if (ret != 0)
		return ret;

	// SOC plugin exchange
	ret = nvtzvault_soc_plugin_exchange(ctx);
	if (ret != 0) {
		NVTZVAULT_ERR("%s: Failed to exchange message via SOC plugin: %d\n", __func__, ret);
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


static int nvtzvault_open_session(struct nvtzvault_ctx *ctx,
			struct nvtzvault_open_session_ctl *open_session_ctl)
{
	int ret = 0;
	struct nvtzvault_session_req_hdr req_hdr = {NVTZVAULT_SESSION_OP_OPEN, 0xFFFFFFFFU, 0U};
	struct nvtzvault_session_resp_hdr resp_hdr;
	uint32_t cmd_id;
	struct nvtzvault_close_session_ctl close_session_ctl = {0};

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
	ret = nvtzvault_write_process_name(ctx);
	if (ret != 0)
		return ret;

	// SOC plugin exchange
	ret = nvtzvault_soc_plugin_exchange(ctx);
	if (ret != 0) {
		NVTZVAULT_ERR("%s: Failed to exchange message via SOC plugin: %d\n", __func__, ret);
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

	if (resp_hdr.session_id >= NVTZVAULT_MAX_SESSIONS) {
		NVTZVAULT_ERR("%s: invalid session id %u\n", __func__, resp_hdr.session_id);
		close_session_ctl.session_id = resp_hdr.session_id;
		ret = nvtzvault_close_session(ctx, &close_session_ctl);
		if (ret != 0)
			NVTZVAULT_ERR("%s: close session failed\n", __func__);
		return -EINVAL;
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

	// Validate session id
	if (invoke_cmd_ctl->session_id >= NVTZVAULT_MAX_SESSIONS) {
		NVTZVAULT_ERR("%s: invalid session id %u\n", __func__, invoke_cmd_ctl->session_id);
		return -EINVAL;
	}

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
	ret = nvtzvault_write_process_name(ctx);
	if (ret != 0)
		return ret;

	// SOC plugin exchange (releases and reacquires mutex internally)
	ret = nvtzvault_soc_plugin_exchange(ctx);
	if (ret != 0) {
		NVTZVAULT_ERR("%s: Failed to exchange message via SOC plugin: %d\n", __func__, ret);
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

	ret = nvtzvault_tee_buf_context_init(&ctx->buf_ctx,
			g_nvtzvault_dev.ta[ctx->node_id].data_buf, NVTZVAULT_BUFFER_SIZE);
	if (ret != 0) {
		NVTZVAULT_ERR("%s: Failed to initialize buffer context\n", __func__);
		kfree(ctx);
		return ret;
	}
	memset(ctx->session_bitmap, 0, sizeof(ctx->session_bitmap));

	memset(ctx->process_name, 0, NVTZVAULT_PROCESS_NAME_LENGTH);
	memcpy(ctx->process_name, current->comm, min((size_t)NVTZVAULT_PROCESS_NAME_LENGTH,
			(size_t)TASK_COMM_LEN));

	if (ctx->process_name[NVTZVAULT_PROCESS_NAME_LENGTH - 1U] != '\0')
		ctx->process_name[NVTZVAULT_PROCESS_NAME_LENGTH - 1U] = '\0';

	filp->private_data = ctx;

	return 0;
}

static int nvtzvault_ta_dev_release(struct inode *inode, struct file *filp)
{
	struct nvtzvault_ctx *ctx = filp->private_data;
	struct nvtzvault_close_session_ctl close_session_ctl;
	int ret;

	mutex_lock(&g_nvtzvault_dev.ta[ctx->node_id].lock);
	for (uint32_t i = 0; i < NVTZVAULT_MAX_SESSIONS; i++) {
		if (is_session_open(ctx, i)) {
			close_session_ctl.session_id = i;
			ret = nvtzvault_close_session(ctx, &close_session_ctl);
			if (ret != 0)
				NVTZVAULT_ERR("%s: close session %u failed: %d\n", __func__, i, ret);
		}
	}
	mutex_unlock(&g_nvtzvault_dev.ta[ctx->node_id].lock);

	kfree(ctx);

	return 0;
}

static long nvtzvault_ta_dev_ioctl(struct file *filp, unsigned int ioctl_num, unsigned long arg)
{
	struct nvtzvault_ctx *ctx = filp->private_data;
	struct nvtzvault_ta *ta;
	struct nvtzvault_open_session_ctl *open_session_ctl;
	struct nvtzvault_invoke_cmd_ctl *invoke_cmd_ctl;
	struct nvtzvault_close_session_ctl *close_session_ctl;
	int ret = 0;
	uint64_t result;

	if (!ctx) {
		NVTZVAULT_ERR("%s(): ctx not allocated\n", __func__);
		return -EPERM;
	}

	ta = &g_nvtzvault_dev.ta[ctx->node_id];
	mutex_lock(&ta->lock);

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
			NVTZVAULT_ERR("%s(): Failed to copy_to_user open_session_ctl:%d\n",
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
			NVTZVAULT_ERR("%s(): Failed to copy_to_user invoke_cmd_ctl:%d\n",
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
			NVTZVAULT_ERR("%s(): Failed to copy_to_user close_session_ctl:%d\n",
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
	mutex_unlock(&ta->lock);
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
	TeeClientStatus tee_status;
	int len, i;
	int ret;
	uint32_t plugin_major = 0;
	uint32_t plugin_minor = 0;
	CommsParamsOS comms_params;

	dev_info(dev, "probe start\n");

	if (!nvtzvault_node)
		return -ENODEV;

	teec_soc_get_interface_version(&plugin_major, &plugin_minor);

	dev_dbg(dev, "Driver supports interface version: %u.%u\n",
		TEEC_SOC_PLUGIN_INTERFACE_MAJOR_VERSION, TEEC_SOC_PLUGIN_INTERFACE_MINOR_VERSION);
	dev_dbg(dev, "Plugin provides interface version: %u.%u\n", plugin_major, plugin_minor);

	if (plugin_major != TEEC_SOC_PLUGIN_INTERFACE_MAJOR_VERSION) {
		dev_err(dev, "Interface version check FAILED: major version mismatch (got %u, expected %u)\n",
			plugin_major, TEEC_SOC_PLUGIN_INTERFACE_MAJOR_VERSION);
		return -EINVAL;
	}

	if (plugin_minor > TEEC_SOC_PLUGIN_INTERFACE_MINOR_VERSION) {
		dev_err(dev, "Interface version check FAILED: got minor version %u, max supported %u\n",
			plugin_minor, TEEC_SOC_PLUGIN_INTERFACE_MINOR_VERSION);
		return -EINVAL;
	}

	dev_info(dev, "TEEC SOC plugin interface version: %u.%u\n", plugin_major, plugin_minor);

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

		/* Initialize SOC plugin interface (function pointers + DT parsing) */
		comms_params.ta_node = ta_node;
		tee_status = teec_initialize_interface(&ta->tee_client, comms_params);
		if (tee_status != TEE_CLIENT_STATUS_OK) {
			dev_err(dev, "failed to initialize TeeClient interface for TA %u: %d\n",
				ta_id, tee_status);
			ret = nvtzvault_tee_translate_teeclient_to_syserror(tee_status);
			goto fail;
		}

		/* Initialize hardware resources (memory mapping, IRQ setup) */
		tee_status = ta->tee_client.teec_comms_init(ta->tee_client.tee_comms_priv, (void *) dev);
		if (tee_status != TEE_CLIENT_STATUS_OK) {
			dev_err(dev,
			"failed to initialize TeeClient communication for TA %u: %d\n",
			ta_id, tee_status);
			ret = nvtzvault_tee_translate_teeclient_to_syserror(tee_status);
			goto fail;
		}

		/* Reset transport to clean state for this TA */
		tee_status = ta->tee_client.teec_comms_reset(ta->tee_client.tee_comms_priv);
		if (tee_status != TEE_CLIENT_STATUS_OK) {
			dev_err(dev, "failed to reset TeeClient communication for TA %u: %d\n",
				ta_id, tee_status);
			ret = nvtzvault_tee_translate_teeclient_to_syserror(tee_status);
			goto fail;
		}

		ret = nvtzvault_ta_create_dev_node(ta->dev, ta->id);
		if (ret != 0)
			goto fail;

		g_nvtzvault_dev.ta_count = (i + 1U);
	}

	/* Initialize per-TA resources */
	for (i = 0; i < g_nvtzvault_dev.ta_count; i++) {
		atomic_set(&g_nvtzvault_dev.ta[i].active_session_count, 0);
		memset(g_nvtzvault_dev.ta[i].session_bitmap, 0,
				sizeof(g_nvtzvault_dev.ta[i].session_bitmap));
		mutex_init(&g_nvtzvault_dev.ta[i].lock);

		/* Allocate per-TA buffer once during probe (avoids runtime allocation) */
		g_nvtzvault_dev.ta[i].data_buf = kzalloc(NVTZVAULT_BUFFER_SIZE, GFP_KERNEL);
		if (!g_nvtzvault_dev.ta[i].data_buf) {
			ret = -ENOMEM;
			goto fail;
		}
	}

	dev_info(dev, "probe success\n");

	return 0;

fail:
	for (i = 0; i < g_nvtzvault_dev.ta_count; i++) {
		ta = &g_nvtzvault_dev.ta[i];

		// Cleanup TeeClient interface - follow proper teardown sequence
		if (ta->tee_client.tee_comms_priv != NULL) {
			tee_status = ta->tee_client.teec_comms_reset_memory(
					ta->tee_client.tee_comms_priv);
			if (tee_status != TEE_CLIENT_STATUS_OK)
				dev_err(&pdev->dev,
					"Failed to reset memory for TA %u: %d\n",
					ta->id, tee_status);

			tee_status = ta->tee_client.teec_comms_deinit(
					ta->tee_client.tee_comms_priv);
			if (tee_status != TEE_CLIENT_STATUS_OK)
				dev_err(&pdev->dev,
					"Failed to deinit TeeClient for TA %u: %d\n",
					ta->id, tee_status);
			ta->tee_client.tee_comms_priv = NULL;
		}

		if (ta->dev) {
			misc_deregister(ta->dev);
			kfree(ta->dev->name);
			kfree(ta->dev);
			ta->dev = NULL;
		}

		/* Free per-TA buffer if allocated */
		kfree(ta->data_buf);
		ta->data_buf = NULL;
	}

	return ret;
}

static int nvtzvault_remove(struct platform_device *pdev)
{
	struct nvtzvault_ta *ta;
	uint32_t i;
	TeeClientStatus tee_status;

	for (i = 0; i < g_nvtzvault_dev.ta_count; i++) {
		ta = &g_nvtzvault_dev.ta[i];

		// TODO: For mailbox calling reset_memory once is enough, possible optimization
		if (ta->tee_client.tee_comms_priv != NULL) {
			tee_status = ta->tee_client.teec_comms_reset_memory(
					ta->tee_client.tee_comms_priv);
			if (tee_status != TEE_CLIENT_STATUS_OK)
				dev_err(&pdev->dev,
					"Failed to reset memory for TA %u: %d\n",
					ta->id, tee_status);

			tee_status = ta->tee_client.teec_comms_deinit(
					ta->tee_client.tee_comms_priv);
			if (tee_status != TEE_CLIENT_STATUS_OK)
				dev_err(&pdev->dev,
					"Failed to deinit TeeClient for TA %u: %d\n",
					ta->id, tee_status);
			ta->tee_client.tee_comms_priv = NULL;
		}

		if (ta->dev) {
			misc_deregister(ta->dev);
			kfree(ta->dev->name);
			kfree(ta->dev);
			ta->dev = NULL;
		}

		/* Free per-TA buffer allocated during probe */
		kfree(ta->data_buf);
		ta->data_buf = NULL;
	}

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

	// Check if any TA has active sessions (atomic reads, no lock needed)
	for (int i = 0; i < g_nvtzvault_dev.ta_count; i++) {
		if (atomic_read(&g_nvtzvault_dev.ta[i].active_session_count) > 0) {
			NVTZVAULT_ERR("%s(): TA %d has active sessions\n", __func__, i);
			nvtzvault_print_active_sessions();
			return -EBUSY;
		}
	}

	// Atomically set suspend state (no lock needed)
	atomic_set(&g_nvtzvault_dev.in_suspend_state, 1);

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
MODULE_SOFTDEP("pre: nvtzvault-soc-plugin");
