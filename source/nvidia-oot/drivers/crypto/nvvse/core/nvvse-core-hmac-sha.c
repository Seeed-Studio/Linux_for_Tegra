// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 *
 * Cryptographic API.
 */

#include <linux/uaccess.h>
#include "nvvse-linux-common.h"
#include "core_common_hal.h"
#include "core_common_lib_rm.h"
#include "core_hmac_functions.h"

NvVseCoreStatus_t nvvse_core_read_hmac_header(NvVseInputOutputReqContext *pDevctlReqContext,
		NvVseHMACSHASignVerifyHeader *pHMACSHASignVerifyHeader)
{
	NvVseCoreStatus_t status = NVVSE_CORE_STATUS_FAIL;
	NvVseHMACSHASignVerifyHeader *p_hmac_sha_header = NULL;
	uint32_t msg_header_size = 0;

	if ((pDevctlReqContext == NULL) || (pHMACSHASignVerifyHeader == NULL)) {
		NVVSE_ERR("%s(): Invalid input parameters\n", __func__);
		status = NVVSE_CORE_STATUS_INVALID_PARAMETER;
		goto fail;
	}

	p_hmac_sha_header =
		(NvVseHMACSHASignVerifyHeader *)pDevctlReqContext->input_output_context.msg_header;
	if (p_hmac_sha_header == NULL) {
		NVVSE_ERR("%s(): msg_header is NULL\n", __func__);
		status = NVVSE_CORE_STATUS_INVALID_PARAMETER;
		goto fail;
	}

	msg_header_size = pDevctlReqContext->input_output_context.msg_header_size;

	if (msg_header_size < sizeof(NvVseHMACSHASignVerifyHeader)) {
		NVVSE_ERR("%s(): Header size is invalid\n", __func__);
		status = NVVSE_CORE_STATUS_INVALID_PARAMETER;
		goto fail;
	}

	memcpy(pHMACSHASignVerifyHeader, p_hmac_sha_header, sizeof(NvVseHMACSHASignVerifyHeader));

	status = NVVSE_CORE_STATUS_OK;
fail:
	return status;
}

NvVseCoreStatus_t nvvse_core_read_hmac_digest_data(NvVseInputOutputReqContext *pDevctlReqContext,
		uint64_t *offset, void *pData, uint32_t size)
{
	NvVseCoreStatus_t status = NVVSE_CORE_STATUS_FAIL;
	void *digest_buffer = NULL;
	uint32_t digest_buffer_size = 0;
	int err = 0;

	if ((pData == NULL) || (pDevctlReqContext == NULL) || (size == 0U) || (offset == NULL)) {
		NVVSE_ERR("%s(): Invalid input parameters\n", __func__);
		status = NVVSE_CORE_STATUS_INVALID_PARAMETER;
		goto fail;
	}

	// force offset to 0
	*offset = 0;

	digest_buffer = pDevctlReqContext->input_output_context.digest_buffer;
	if (digest_buffer == NULL) {
		NVVSE_ERR("%s(): digest_buffer is NULL\n", __func__);
		status = NVVSE_CORE_STATUS_INVALID_PARAMETER;
		goto fail;
	}

	// Additional validation for digest_buffer pointer
	if (!access_ok(digest_buffer, size)) {
		NVVSE_ERR("%s(): Invalid digest_buffer pointer or size\n", __func__);
		status = NVVSE_CORE_STATUS_INVALID_PARAMETER;
		goto fail;
	}

	digest_buffer_size = pDevctlReqContext->input_output_context.digest_buffer_size;
	if (digest_buffer_size < size) {
		NVVSE_ERR("%s(): Buffer size mismatch: buffer_size=%u, requested_size=%u\n",
		__func__, digest_buffer_size, size);
		status = NVVSE_CORE_STATUS_INVALID_PARAMETER;
		goto fail;
	}

	err = copy_from_user(pData, digest_buffer, size);
	if (err) {
		NVVSE_ERR("%s(): Failed to copy data to user, err=%d\n", __func__, err);
		status = NVVSE_CORE_STATUS_FAIL;
		goto fail;
	}

	status = NVVSE_CORE_STATUS_OK;
fail:
	return status;
}

NvVseCoreStatus_t nvvse_core_read_hmac_last_block_data(
		NvVseInputOutputReqContext *pDevctlReqContext,
		uint64_t *offset, void *pData, uint32_t size)
{
	NvVseCoreStatus_t status = NVVSE_CORE_STATUS_FAIL;
	void *input_buffer = NULL;
	int err = 0;

	if (size == 0U)
		return NVVSE_CORE_STATUS_OK;

	if ((pData == NULL) || (pDevctlReqContext == NULL) || (offset == NULL)) {
		NVVSE_ERR("%s(): Invalid input parameters\n", __func__);
		status = NVVSE_CORE_STATUS_INVALID_PARAMETER;
		goto fail;
	}

	input_buffer = pDevctlReqContext->input_output_context.src_buffer;
	if (input_buffer == NULL) {
		NVVSE_ERR("%s(): src_buffer is NULL\n", __func__);
		status = NVVSE_CORE_STATUS_INVALID_PARAMETER;
		goto fail;
	}

	// Additional validation for input_buffer pointer
	if (!access_ok(((uint8_t *)input_buffer + *offset), size)) {
		NVVSE_ERR("%s(): Invalid src_buffer pointer or size\n", __func__);
		status = NVVSE_CORE_STATUS_INVALID_PARAMETER;
		goto fail;
	}

	err = copy_from_user(pData, ((uint8_t *)input_buffer + *offset), size);
	if (err) {
		NVVSE_ERR("%s(): Failed to copy data from user, err=%d\n", __func__, err);
		status = NVVSE_CORE_STATUS_FAIL;
		goto fail;
	}

	status = NVVSE_CORE_STATUS_OK;
fail:
	return status;
}

NvVseCoreStatus_t nvvse_core_read_hmac_input_data(NvVseInputOutputReqContext *pDevctlReqContext,
		uint64_t *offset, void *pData, uint32_t size)
{
	NvVseCoreStatus_t status = NVVSE_CORE_STATUS_FAIL;
	void *input_buffer = NULL;
	uint32_t input_buffer_size = 0;
	int err = 0;

	if (size == 0U)
		return NVVSE_CORE_STATUS_OK;

	if ((pData == NULL) || (pDevctlReqContext == NULL) || (offset == NULL)) {
		NVVSE_ERR("%s(): Invalid input parameters\n", __func__);
		status = NVVSE_CORE_STATUS_INVALID_PARAMETER;
		goto fail;
	}

	// force offset to 0
	*offset = 0;

	input_buffer = pDevctlReqContext->input_output_context.src_buffer;
	if (input_buffer == NULL) {
		NVVSE_ERR("%s(): src_buffer is NULL\n", __func__);
		status = NVVSE_CORE_STATUS_INVALID_PARAMETER;
		goto fail;
	}

	// Additional validation for input_buffer pointer
	if (!access_ok(input_buffer, size)) {
		NVVSE_ERR("%s(): Invalid src_buffer pointer or size\n", __func__);
		status = NVVSE_CORE_STATUS_INVALID_PARAMETER;
		goto fail;
	}

	input_buffer_size = pDevctlReqContext->input_output_context.src_buffer_size;
	if (input_buffer_size < size) {
		NVVSE_ERR("%s(): Buffer size is invalid\n", __func__);
		status = NVVSE_CORE_STATUS_INVALID_PARAMETER;
		goto fail;
	}

	err = copy_from_user(pData, input_buffer, size);
	if (err) {
		NVVSE_ERR("%s(): Failed to copy data from user, err=%d\n", __func__, err);
		status = NVVSE_CORE_STATUS_FAIL;
		goto fail;
	}

	AddUInt64WithExitFunction(*offset, size, offset);

	status = NVVSE_CORE_STATUS_OK;
fail:
	return status;
}

NvVseCoreStatus_t nvvse_core_write_hmac_digest_data(
		NvVseInputOutputReqContext *pDevctlReqContext, uint64_t *offset,
		const void *pData, uint32_t size)
{
	NvVseCoreStatus_t status = NVVSE_CORE_STATUS_FAIL;
	void *digest_buffer = NULL;
	uint32_t digest_buffer_size = 0;
	int err = 0;

	if ((pData == NULL) || (pDevctlReqContext == NULL) || (size == 0U) || (offset == NULL)) {
		NVVSE_ERR("%s(): Invalid input parameters\n", __func__);
		status = NVVSE_CORE_STATUS_INVALID_PARAMETER;
		goto fail;
	}

	digest_buffer = pDevctlReqContext->input_output_context.digest_buffer;
	if (digest_buffer == NULL) {
		NVVSE_ERR("%s(): digest_buffer is NULL\n", __func__);
		status = NVVSE_CORE_STATUS_INVALID_PARAMETER;
		goto fail;
	}

	// Additional validation for digest_buffer pointer
	if (!access_ok(digest_buffer, size)) {
		NVVSE_ERR("%s(): Invalid digest_buffer pointer or size\n", __func__);
		status = NVVSE_CORE_STATUS_INVALID_PARAMETER;
		goto fail;
	}

	digest_buffer_size = pDevctlReqContext->input_output_context.digest_buffer_size;
	if (digest_buffer_size < size) {
		NVVSE_ERR("%s(): Buffer size mismatch: buffer_size=%u, requested_size=%u\n",
		__func__, digest_buffer_size, size);
		status = NVVSE_CORE_STATUS_INVALID_PARAMETER;
		goto fail;
	}

	err = copy_to_user(digest_buffer, pData, size);
	if (err) {
		NVVSE_ERR("%s(): Failed to copy data to user, err=%d\n", __func__, err);
		status = NVVSE_CORE_STATUS_FAIL;
		goto fail;
	}

	status = NVVSE_CORE_STATUS_OK;
fail:
	return status;
}

NvVseCoreStatus_t nvvse_core_write_hmac_hash_validation_result(
		NvVseInputOutputReqContext *pDevctlReqContext, uint64_t *offset,
		const void *pData, uint32_t size)
{
	NvVseCoreStatus_t status = NVVSE_CORE_STATUS_FAIL;
	void *validation_result_buffer = NULL;
	uint32_t validation_result_buffer_size = 0;

	if ((pDevctlReqContext == NULL) || (pData == NULL) || (offset == NULL)) {
		NVVSE_ERR("%s(): Invalid input parameters\n", __func__);
		status = NVVSE_CORE_STATUS_INVALID_PARAMETER;
		goto fail;
	}

	validation_result_buffer =
			pDevctlReqContext->input_output_context.validation_result_buffer;
	if (validation_result_buffer == NULL) {
		NVVSE_ERR("%s(): Validation result buffer is NULL\n", __func__);
		status = NVVSE_CORE_STATUS_INVALID_PARAMETER;
		goto fail;
	}

	validation_result_buffer_size =
			pDevctlReqContext->input_output_context.validation_result_buffer_size;
	if (validation_result_buffer_size != size) {
		NVVSE_ERR("%s(): Validation result buffer size is invalid\n", __func__);
		status = NVVSE_CORE_STATUS_INVALID_PARAMETER;
		goto fail;
	}

	memcpy(validation_result_buffer, pData, validation_result_buffer_size);

	status = NVVSE_CORE_STATUS_OK;
fail:
	return status;
}
