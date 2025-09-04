/*
 * Copyright (c) 2023, NVIDIA Corporation & AFFILIATES. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <ta_cpubl_dec.h>
#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>

#include <string.h>
#include <util.h>
#include <pta_jetson_user_key.h>

#define JETSON_USER_KEY_TA_UUID \
		{ 0xe9e156e8, 0xe161, 0x4c8a, \
			{0x91, 0xa9, 0x0b, 0xba, 0x5e, 0x24, 0x7e, 0xe8} }

TEE_Result TA_CreateEntryPoint(void)
{
	return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void)
{
}

TEE_Result TA_OpenSessionEntryPoint(uint32_t pt __unused,
				    TEE_Param params[4] __unused,
				    void **session __unused)
{
	return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void *sess __unused)
{
}

static TEE_Result is_user_key_exists(uint32_t types, TEE_Param in_params[TEE_NUM_PARAMS])
{
	TEE_Result res = TEE_ERROR_GENERIC;
	TEE_TASessionHandle sess = TEE_HANDLE_NULL;
	uint32_t ret_orig = 0;
	uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
					       TEE_PARAM_TYPE_VALUE_OUTPUT,
					       TEE_PARAM_TYPE_NONE,
					       TEE_PARAM_TYPE_NONE);
	if (exp_pt != types) {
		EMSG("bad parameters types!\n");
		return TEE_ERROR_BAD_PARAMETERS;
	}

	res = TEE_OpenTASession(&(const TEE_UUID)JETSON_USER_KEY_TA_UUID,
				TEE_TIMEOUT_INFINITE, 0, NULL, &sess,
				&ret_orig);
	if (res) {
		EMSG("TEE_OpenTASession failed with res = 0x%08x\n", res);
		return res;
	}

	res = TEE_InvokeTACommand(sess, TEE_TIMEOUT_INFINITE,
				  JETSON_USER_KEY_CMD_IS_KEY_EXISTS,
				  types, in_params, &ret_orig);
	if (res) {
		EMSG("TEE_InvokeTACommand failed with res = 0x%08x\n", res);
	}

	TEE_CloseTASession(sess);

	return res;
}

static TEE_Result decrypt_image(uint32_t types, TEE_Param in_params[TEE_NUM_PARAMS])
{
	TEE_Result res = TEE_ERROR_GENERIC;
	TEE_TASessionHandle sess = TEE_HANDLE_NULL;
	uint32_t ret_orig = 0;
	uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INOUT,
					       TEE_PARAM_TYPE_VALUE_INPUT,
					       TEE_PARAM_TYPE_NONE,
					       TEE_PARAM_TYPE_NONE);
	if (exp_pt != types) {
		EMSG("bad parameters types!\n");
		return TEE_ERROR_BAD_PARAMETERS;
	}

	res = TEE_OpenTASession(&(const TEE_UUID)JETSON_USER_KEY_TA_UUID,
				TEE_TIMEOUT_INFINITE, 0, NULL, &sess,
				&ret_orig);
	if (res) {
		EMSG("TEE_OpenTASession failed with res = 0x%08x\n", res);
		return res;
	}

	res = TEE_InvokeTACommand(sess, TEE_TIMEOUT_INFINITE,
				  JETSON_USER_KEY_CMD_DECRYPT_CPUBL_PAYLOAD,
				  types, in_params, &ret_orig);
	if (res) {
		EMSG("TEE_InvokeTACommand failed with res = 0x%08x\n", res);
	}

	TEE_CloseTASession(sess);

	return res;
}

TEE_Result TA_InvokeCommandEntryPoint(void *sess __unused, uint32_t cmd,
				      uint32_t pt,
				      TEE_Param params[TEE_NUM_PARAMS])
{
	switch (cmd) {
	case CPUBL_PAYLOAD_DECRYPTION_CMD_IS_USER_KEY_EXISTS:
		return is_user_key_exists(pt, params);
	case CPUBL_PAYLOAD_DECRYPTION_CMD_DECRYPT_IMAGE:
		return decrypt_image(pt, params);
	default:
		return TEE_ERROR_NOT_SUPPORTED;
	}
}
