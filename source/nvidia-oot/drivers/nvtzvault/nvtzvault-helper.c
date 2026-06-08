// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "nvtzvault-helper.h"
#include "nvtzvault-common.h"
#include <linux/errno.h>
#include <linux/printk.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/slab.h>

#define NVTZVAULT_TEE_PARAM_TYPE_NONE            0U
#define NVTZVAULT_TEE_PARAM_TYPE_VALUE_INPUT     1U
#define NVTZVAULT_TEE_PARAM_TYPE_VALUE_OUTPUT    3U
#define NVTZVAULT_TEE_PARAM_TYPE_VALUE_INOUT     3U
#define NVTZVAULT_TEE_PARAM_TYPE_MEMREF_INPUT    5U
#define NVTZVAULT_TEE_PARAM_TYPE_MEMREF_OUTPUT   7U
#define NVTZVAULT_TEE_PARAM_TYPE_MEMREF_INOUT    7U

int nvtzvault_tee_translate_saerror_to_syserror(const uint32_t tzv_error)
{
	const uint32_t tzv_error_array[] = {
			TZVAULT_SUCCESS, TZVAULT_PENDING,
			TZVAULT_ERROR_GENERIC, TZVAULT_ERROR_ACCESS_CONFLICT,
			TZVAULT_ERROR_ACCESS_DENIED, TZVAULT_ERROR_AGAIN,
			TZVAULT_ERROR_BAD_FORMAT, TZVAULT_ERROR_BAD_PARAMETERS,
			TZVAULT_ERROR_BAD_STATE, TZVAULT_ERROR_BUSY,
			TZVAULT_ERROR_CANCEL, TZVAULT_ERROR_COMMUNICATION,
			TZVAULT_ERROR_EXCESS_DATA, TZVAULT_ERROR_ITEM_NOT_FOUND,
			TZVAULT_ERROR_MAC_INVALID, TZVAULT_ERROR_NO_DATA,
			TZVAULT_ERROR_NO_MESSAGE, TZVAULT_ERROR_NO_RESOURCE,
			TZVAULT_ERROR_NOT_IMPLEMENTED, TZVAULT_ERROR_NOT_SUPPORTED,
			TZVAULT_ERROR_OUT_OF_MEMORY, TZVAULT_ERROR_OVERFLOW,
			TZVAULT_ERROR_SECURITY, TZVAULT_ERROR_SHORT_BUFFER,
			TZVAULT_ERROR_SIGNATURE_INVALID, TZVAULT_ERROR_STORAGE_NO_SPACE,
			TZVAULT_ERROR_TARGET_DEAD, TZVAULT_ERROR_TIME_NEEDS_RESET,
			TZVAULT_ERROR_TIME_NOT_SET, TZVAULT_ERROR_TIMEOUT };
	const int sys_error_array[] = {
			0, -EINPROGRESS,
			-EUSERS, -EFAULT,
			-EACCES, -EAGAIN,
			-EBADF, -EINVAL,
			-ENOTCONN, -EBUSY,
			-ECANCELED, -ECONNRESET,
			-E2BIG, -ENOENT,
			-EBADMSG, -ENODATA,
			-ENOMSG, -ENOSR,
			-ENOSYS, -EOPNOTSUPP,
			-ENOMEM, -EOVERFLOW,
			-EPERM, -ENOBUFS,
			-EBADE, -ENOSPC,
			-EHOSTDOWN, -ERESTART,
			-ETIME, -ETIMEDOUT };

   // sizeof operator is used to calculate the number of elements in tzv_error_array
	const uint64_t num_tzv_errors =
			sizeof(tzv_error_array) / sizeof(uint32_t);
	int sys_error = 0;
	uint64_t index = 0ULL;

	for (index = 0ULL; index < num_tzv_errors; ++index) {
		if (tzv_error_array[index] == tzv_error)
			break;
	}

	if (index < num_tzv_errors) {
		sys_error = sys_error_array[index];
	} else {
		NVTZVAULT_ERR("Unknown SA error code: 0x%x\n", tzv_error);
		sys_error = -EUSERS;
	}

	return sys_error;
}

static inline uint32_t nvtzvault_tee_get_param_type_from_index(const uint32_t t, const uint32_t i)
{
	uint32_t result = 0U;

	if ((i << 2U) < 32U)
		result = (t >> (i << 2U)) & 0xFU;

	return result;
}

static bool nvtzvault_tee_is_param_membuf(const uint32_t t)
{
	return ((t & 4U) != 0U);
}

static bool nvtzvault_tee_is_param_output(const uint32_t t)
{
	return ((t & 2U) != 0U);
}

static bool nvtzvault_tee_is_param_input(const uint32_t t)
{
	return ((t & 1U) != 0U);
}

static bool nvtzvault_tee_is_param_valid(const uint32_t t)
{
	static const uint32_t validParamTypes[] = {
		NVTZVAULT_TEE_PARAM_TYPE_NONE,
		NVTZVAULT_TEE_PARAM_TYPE_VALUE_INPUT,
		NVTZVAULT_TEE_PARAM_TYPE_VALUE_OUTPUT,
		NVTZVAULT_TEE_PARAM_TYPE_VALUE_INOUT,
		NVTZVAULT_TEE_PARAM_TYPE_MEMREF_INPUT,
		NVTZVAULT_TEE_PARAM_TYPE_MEMREF_OUTPUT,
		NVTZVAULT_TEE_PARAM_TYPE_MEMREF_INOUT
	};
	uint32_t i;
	bool retval = false;

	for (i = 0U; i < sizeof(validParamTypes) / sizeof(uint32_t); ++i) {
		if (t == validParamTypes[i]) {
			retval = true;
			break;
		}
	}
	return retval;
}

static bool nvtzvault_tee_are_all_params_valid(uint32_t param_types)
{
	uint32_t i;
	uint32_t param_type;
	bool all_valid = true;

	for (i = 0; i < NVTZVAULT_TEE_PARAM_MAX_COUNT; i++) {
		param_type = nvtzvault_tee_get_param_type_from_index(param_types, i);
		if (!nvtzvault_tee_is_param_valid(param_type)) {
			NVTZVAULT_ERR("Invalid parameter type 0x%x at index %d\n", param_type, i);
			all_valid = false;
			break;
		}
	}

	return all_valid;
}

static int nvtzvault_tee_check_overflow_and_skip_bytes(struct nvtzvault_tee_buf_context *ctx,
			const uint32_t size)
{
	int result = 0;

	if (size == 0)
		goto end;

	if (size > ctx->buf_len) {
		NVTZVAULT_ERR("Invalid size\n");
		result = -EOVERFLOW;
		goto end;
	}

	if (ctx->current_offset > ctx->buf_len - size) {
		NVTZVAULT_ERR("Failed to write due to overflow\n");
		result = -EOVERFLOW;
		goto end;
	}

	ctx->current_offset += size;

end:
	return result;
}

int nvtzvault_tee_check_overflow_and_write(struct nvtzvault_tee_buf_context *ctx, void *data,
			const uint32_t size, bool is_user_space)
{
	int result = 0;
	uint32_t i;
	void *local_buf = NULL;

	if (size == 0)
		goto end;

	if (size > ctx->buf_len) {
		NVTZVAULT_ERR("Invalid size\n");
		result = -EOVERFLOW;
		goto end;
	}

	if (ctx->current_offset > ctx->buf_len - size) {
		NVTZVAULT_ERR("Failed to write due to overflow\n");
		result = -EOVERFLOW;
		goto end;
	}

	if (is_user_space) {
		result = copy_from_user(&ctx->buf_ptr[ctx->current_offset],
				(void __user *)data, size);
		if (result != 0) {
			NVTZVAULT_ERR("%s(): Failed to copy_from_user %d\n", __func__, result);
			goto end;
		}
	} else {
		for (i = 0U; i < size; i++)
			ctx->buf_ptr[ctx->current_offset + i] = ((uint8_t *)data)[i];
	}

	ctx->current_offset += size;

end:
	kfree(local_buf);
	return result;
}

int nvtzvault_tee_check_overflow_and_read(struct nvtzvault_tee_buf_context *ctx, void *data,
			const uint32_t size, bool is_user_space)
{
	int result = 0;
	uint64_t ret = 0UL;
	uint32_t i;

	if (size == 0)
		goto end;

	if (size > ctx->buf_len) {
		NVTZVAULT_ERR("Invalid size\n");
		result = -EOVERFLOW;
		goto end;
	}

	if (ctx->current_offset > ctx->buf_len - size) {
		NVTZVAULT_ERR("Failed to write due to overflow\n");
		result = -EOVERFLOW;
		goto end;
	}

	if (is_user_space) {
		ret = copy_to_user((void __user *)data, &ctx->buf_ptr[ctx->current_offset],
				size);
		if (ret != 0UL) {
			NVTZVAULT_ERR("%s(): Failed to copy_to_user %llu\n", __func__, ret);
			result = -EFAULT;
			goto end;
		}
	} else {
		for (i = 0U; i < size; i++)
			((uint8_t *)data)[i] = ctx->buf_ptr[ctx->current_offset + i];
	}
	ctx->current_offset += size;

end:
	return result;
}

static int nvtzvault_tee_write_value(struct nvtzvault_tee_buf_context *ctx,
			struct nvtzvault_teec_parameter *param, bool skip)
{
	int result;
	uint32_t size;

	size = sizeof(param->value);

	if (skip)
		result = nvtzvault_tee_check_overflow_and_skip_bytes(ctx, size);
	else
		result = nvtzvault_tee_check_overflow_and_write(ctx, &param->value, size, false);

	if (result != 0)
		NVTZVAULT_ERR("%s failed %d", __func__, result);

	return result;
}

static int nvtzvault_tee_read_value(struct nvtzvault_tee_buf_context *ctx,
			struct nvtzvault_teec_parameter *param, bool skip)
{
	int result;
	uint32_t size;

	size = sizeof(param->value);

	if (skip)
		result = nvtzvault_tee_check_overflow_and_skip_bytes(ctx, size);
	else
		result = nvtzvault_tee_check_overflow_and_read(ctx, &param->value, size, false);

	if (result != 0)
		NVTZVAULT_ERR("%s failed %d", __func__, result);

	return result;
}

static int nvtzvault_tee_write_memref(struct nvtzvault_tee_buf_context *ctx,
			struct nvtzvault_teec_parameter *param, bool skip)
{
	int result;
	uint32_t size;

	size = sizeof(param->memref.size);

	result = nvtzvault_tee_check_overflow_and_write(ctx, &param->memref.size, size, false);
	if (result != 0) {
		result = -EFAULT;
		NVTZVAULT_ERR("%s: failed to write memref size\n", __func__);
		goto end;
	}

	if (param->memref.size > UINT32_MAX) {
		result = -EOVERFLOW;
		NVTZVAULT_ERR("Invalid memref size\n");
		goto end;
	}

	size = (uint32_t)param->memref.size;

	if (skip)
		result = nvtzvault_tee_check_overflow_and_skip_bytes(ctx, size);
	else
		result = nvtzvault_tee_check_overflow_and_write(ctx, param->memref.buffer,
			size, true);

	if (result != 0)
		NVTZVAULT_ERR("%s failed %d", __func__, result);

end:
	return result;
}

static int nvtzvault_tee_read_memref(struct nvtzvault_tee_buf_context *ctx,
			struct nvtzvault_teec_parameter *param, bool skip)
{
	int result;
	uint32_t size;

	size = sizeof(param->memref.size);
	result = nvtzvault_tee_check_overflow_and_read(ctx, &param->memref.size, size, false);
	if (result != 0) {
		result = -EFAULT;
		NVTZVAULT_ERR("%s: failed to read memref size\n", __func__);
		goto end;
	}

	if (param->memref.size > UINT32_MAX) {
		result = -EOVERFLOW;
		NVTZVAULT_ERR("Invalid memref size\n");
		goto end;
	}

	size = (uint32_t)param->memref.size;

	if (skip)
		result = nvtzvault_tee_check_overflow_and_skip_bytes(ctx, size);
	else
		result = nvtzvault_tee_check_overflow_and_read(ctx, param->memref.buffer,
			size, true);

	if (size == 0U)
		param->memref.buffer = NULL;

end:
	if (result != 0)
		NVTZVAULT_ERR("%s failed %d", __func__, result);

	return result;
}

int nvtzvault_tee_write_all_params(struct nvtzvault_tee_buf_context *ctx, uint32_t cmd_id,
			uint32_t param_types,
			struct nvtzvault_teec_parameter params[NVTZVAULT_TEE_PARAM_MAX_COUNT])
{
	int result;
	uint32_t curr_param_type;
	bool skip;
	uint32_t i;

	if (!nvtzvault_tee_are_all_params_valid(param_types)) {
		NVTZVAULT_ERR("%s: invalid param types %u\n", __func__, param_types);
		result = -EINVAL;
		goto end;
	}

	result = nvtzvault_tee_check_overflow_and_write(ctx, &param_types, sizeof(param_types),
			false);
	if (result != 0) {
		NVTZVAULT_ERR("%s: failed to write param types %d\n", __func__, result);
		result = -EFAULT;
		goto end;
	}

	result = nvtzvault_tee_check_overflow_and_write(ctx, &cmd_id, sizeof(cmd_id), false);
	if (result != 0) {
		NVTZVAULT_ERR("%s: failed to write cmd id %d\n", __func__, result);
		result = -EFAULT;
		goto end;
	}

	for (i = 0; i < NVTZVAULT_TEE_PARAM_MAX_COUNT; i++) {
		curr_param_type = nvtzvault_tee_get_param_type_from_index(param_types, i);
		if (curr_param_type == NVTZVAULT_TEE_PARAM_TYPE_NONE)
			continue;

		skip = !nvtzvault_tee_is_param_input(curr_param_type);

		if (nvtzvault_tee_is_param_membuf(curr_param_type))
			result = nvtzvault_tee_write_memref(ctx, &params[i], skip);
		else
			result = nvtzvault_tee_write_value(ctx, &params[i], skip);

		if (result != 0) {
			NVTZVAULT_ERR("Failed to write parameter %d: %d\n", i, result);
			goto end;
		}
	}

end:
	return result;
}

int nvtzvault_tee_read_all_params(struct nvtzvault_tee_buf_context *ctx, uint32_t *p_param_types,
			uint32_t *p_cmd_id,
			struct nvtzvault_teec_parameter params[NVTZVAULT_TEE_PARAM_MAX_COUNT])
{
	int result;
	uint32_t curr_param_type;
	bool skip;
	uint32_t i;

	result = nvtzvault_tee_check_overflow_and_read(ctx, p_param_types,
			sizeof(*p_param_types), false);
	if (result != 0) {
		NVTZVAULT_ERR("%s: failed to read param types %d\n", __func__, result);
		result = -EFAULT;
		goto end;
	}

	if (!nvtzvault_tee_are_all_params_valid(*p_param_types)) {
		NVTZVAULT_ERR("%s: invalid param types %u\n", __func__, *p_param_types);
		result = -EINVAL;
		goto end;
	}

	result = nvtzvault_tee_check_overflow_and_read(ctx, p_cmd_id, sizeof(*p_cmd_id), false);
	if (result != 0) {
		NVTZVAULT_ERR("%s: failed to read cmd id %d\n", __func__, result);
		result = -EFAULT;
		goto end;
	}

	for (i = 0; i < NVTZVAULT_TEE_PARAM_MAX_COUNT; i++) {
		curr_param_type = nvtzvault_tee_get_param_type_from_index(*p_param_types, i);
		if (curr_param_type == NVTZVAULT_TEE_PARAM_TYPE_NONE)
			continue;

		skip = !nvtzvault_tee_is_param_output(curr_param_type);

		if (nvtzvault_tee_is_param_membuf(curr_param_type))
			result = nvtzvault_tee_read_memref(ctx, &params[i], skip);
		else
			result = nvtzvault_tee_read_value(ctx, &params[i], skip);

		if (result != 0) {
			NVTZVAULT_ERR("%s: failed to read param %u: %d\n", __func__, i, result);
			goto end;
		}
	}

end:
	return result;
}

int32_t nvtzvault_tee_buf_context_init(struct nvtzvault_tee_buf_context *ctx,
			void *buf_ptr, uint32_t buf_len)
{
	if (!ctx || !buf_ptr || (buf_len == 0)) {
		NVTZVAULT_ERR("Invalid arguments\n");
		return -EINVAL;
	}

	ctx->buf_ptr = buf_ptr;
	ctx->buf_len = buf_len;
	ctx->current_offset = 0;

	return 0;
}

void nvtzvault_tee_buf_context_reset(struct nvtzvault_tee_buf_context *ctx)
{
	if (!ctx) {
		NVTZVAULT_ERR("Invalid arguments\n");
		return;
	}

	ctx->current_offset = 0;
}
