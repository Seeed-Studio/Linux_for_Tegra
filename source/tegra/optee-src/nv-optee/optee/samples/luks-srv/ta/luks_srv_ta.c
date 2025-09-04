/*
 * Copyright (c) 2023, NVIDIA Corporation & AFFILIATES. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <inttypes.h>
#include <luks_srv_ta.h>
#include <pta_jetson_user_key.h>
#include <string.h>
#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>

#define AES128_KEY_BIT_SIZE		128
#define AES128_KEY_BYTE_SIZE		(AES128_KEY_BIT_SIZE / 8)

static uint8_t luks_key[AES128_KEY_BYTE_SIZE] = { 0 };

static TEE_Result invoke_jetson_user_key_pta(uint32_t cmd_id,
					     uint32_t param_types,
					     TEE_Param params[TEE_NUM_PARAMS])
{
	static TEE_TASessionHandle sess = TEE_HANDLE_NULL;
	static const TEE_UUID uuid = JETSON_USER_KEY_UUID;

	if (sess == TEE_HANDLE_NULL) {
		TEE_Result rc = TEE_OpenTASession(&uuid, TEE_TIMEOUT_INFINITE,
						   0, NULL, &sess, NULL);

		if (rc)
			return rc;
	}

	return TEE_InvokeTACommand(sess, TEE_TIMEOUT_INFINITE, cmd_id,
				   param_types, params, NULL);
}

static TEE_Result luks_srv_gen_unique_pass(uint32_t param_types,
					   TEE_Param params[TEE_NUM_PARAMS])
{
	TEE_Result rc = TEE_SUCCESS;
	uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
					  TEE_PARAM_TYPE_MEMREF_OUTPUT,
					  TEE_PARAM_TYPE_NONE,
					  TEE_PARAM_TYPE_NONE);
	uint32_t pa_for_luks_key, pa_for_pass;
	TEE_Param params_for_luks_key[TEE_NUM_PARAMS] = { };
	TEE_Param params_for_pass[TEE_NUM_PARAMS] = { };
	char label_str_for_luks_key[] = "luks-srv-ecid";
	char label_str_for_pass[] = "luks-srv-passphrase-unique";

	if (exp_pt != param_types)
		return TEE_ERROR_BAD_PARAMETERS;
	if ((params[0].memref.size > LUKS_SRV_CONTEXT_STR_LEN) ||
	    (params[1].memref.size > LUKS_SRV_PASSPHRASE_LEN))
		return TEE_ERROR_BAD_PARAMETERS;

	/* Generate a unique LUKS key */
	pa_for_luks_key = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
					  TEE_PARAM_TYPE_MEMREF_INPUT,
					  TEE_PARAM_TYPE_MEMREF_OUTPUT,
					  TEE_PARAM_TYPE_NONE);
	params_for_luks_key[0].value.a = EKB_USER_KEY_DISK_ENCRYPTION;
	params_for_luks_key[1].memref.buffer = label_str_for_luks_key;
	params_for_luks_key[1].memref.size = strlen(label_str_for_luks_key);
	params_for_luks_key[2].memref.buffer = luks_key;
	params_for_luks_key[2].memref.size = AES128_KEY_BYTE_SIZE;
	rc = invoke_jetson_user_key_pta(
		JETSON_USER_KEY_CMD_GEN_UNIQUE_KEY_BY_EKB,
		pa_for_luks_key, params_for_luks_key);
	if (rc)
		return rc;

	/* Generate the device unique passphrase. */
	pa_for_pass = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
				      TEE_PARAM_TYPE_MEMREF_INPUT,
				      TEE_PARAM_TYPE_MEMREF_INPUT,
				      TEE_PARAM_TYPE_MEMREF_OUTPUT);
	params_for_pass[0].memref.buffer = luks_key;
	params_for_pass[0].memref.size = AES128_KEY_BYTE_SIZE;
	params_for_pass[1].memref.buffer = params[0].memref.buffer;
	params_for_pass[1].memref.size = params[0].memref.size;
	params_for_pass[2].memref.buffer = label_str_for_pass;
	params_for_pass[2].memref.size = strlen(label_str_for_pass);
	params_for_pass[3].memref.buffer = params[1].memref.buffer;
	params_for_pass[3].memref.size = params[1].memref.size;
	rc = invoke_jetson_user_key_pta(JETSON_USER_KEY_CMD_GEN_KEY,
					pa_for_pass, params_for_pass);

	return rc;
}

static TEE_Result luks_srv_gen_generic_pass(uint32_t param_types,
					    TEE_Param params[TEE_NUM_PARAMS])
{
	TEE_Result rc = TEE_SUCCESS;
	uint32_t disk_key_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
					       TEE_PARAM_TYPE_MEMREF_OUTPUT,
					       TEE_PARAM_TYPE_NONE,
					       TEE_PARAM_TYPE_NONE);
	TEE_Param params_for_disk_key[TEE_NUM_PARAMS] = { };
	uint8_t disk_key[AES128_KEY_BYTE_SIZE] = { 0 };
	uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
					  TEE_PARAM_TYPE_MEMREF_OUTPUT,
					  TEE_PARAM_TYPE_NONE,
					  TEE_PARAM_TYPE_NONE);
	char label_str_for_generic_key[] = "luks-srv-generic";
	char context_str_for_generic_key[] = "generic-key";
	uint32_t pt_for_generic_key;
	TEE_Param params_for_generic_key[TEE_NUM_PARAMS] = { };
	uint8_t generic_key[AES128_KEY_BIT_SIZE] = { 0 };
	char label_str_for_pass[] = "luks-srv-passphrase-generic";
	uint32_t pt_for_generic_pass;
	TEE_Param params_for_generic_pass[TEE_NUM_PARAMS] = { };

	if (exp_pt != param_types)
		return TEE_ERROR_BAD_PARAMETERS;
	if ((params[0].memref.size > LUKS_SRV_CONTEXT_STR_LEN) ||
	    (params[1].memref.size > LUKS_SRV_PASSPHRASE_LEN))
		return TEE_ERROR_BAD_PARAMETERS;

	/* Get disk key from EKB. */
	params_for_disk_key[0].value.a = EKB_USER_KEY_DISK_ENCRYPTION;
	params_for_disk_key[1].memref.buffer = disk_key;
	params_for_disk_key[1].memref.size = AES128_KEY_BYTE_SIZE;
	rc = invoke_jetson_user_key_pta(JETSON_USER_KEY_CMD_GET_EKB_KEY,
					disk_key_pt, params_for_disk_key);
	if (rc)
		return rc;

	/* Generate a generic LUKS key. */
	pt_for_generic_key = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
					     TEE_PARAM_TYPE_MEMREF_INPUT,
					     TEE_PARAM_TYPE_MEMREF_INPUT,
					     TEE_PARAM_TYPE_MEMREF_OUTPUT);
	params_for_generic_key[0].memref.buffer = params_for_disk_key[1].memref.buffer;
	params_for_generic_key[0].memref.size = params_for_disk_key[1].memref.size;
	params_for_generic_key[1].memref.buffer = context_str_for_generic_key;
	params_for_generic_key[1].memref.size = strlen(context_str_for_generic_key);
	params_for_generic_key[2].memref.buffer = label_str_for_generic_key;
	params_for_generic_key[2].memref.size = strlen(label_str_for_generic_key);
	params_for_generic_key[3].memref.buffer = generic_key;
	params_for_generic_key[3].memref.size = AES128_KEY_BYTE_SIZE;
	rc = invoke_jetson_user_key_pta(JETSON_USER_KEY_CMD_GEN_KEY,
					pt_for_generic_key,
					params_for_generic_key);
	if (rc)
		return rc;

	/* Generate the generic passphrase. */
	pt_for_generic_pass = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
					      TEE_PARAM_TYPE_MEMREF_INPUT,
					      TEE_PARAM_TYPE_MEMREF_INPUT,
					      TEE_PARAM_TYPE_MEMREF_OUTPUT);
	params_for_generic_pass[0].memref.buffer = params_for_generic_key[3].memref.buffer;
	params_for_generic_pass[0].memref.size = params_for_generic_key[3].memref.size;
	params_for_generic_pass[1].memref.buffer = params[0].memref.buffer;
	params_for_generic_pass[1].memref.size = params[0].memref.size;
	params_for_generic_pass[2].memref.buffer = label_str_for_pass;
	params_for_generic_pass[2].memref.size = strlen(label_str_for_pass);
	params_for_generic_pass[3].memref.buffer = params[1].memref.buffer;
	params_for_generic_pass[3].memref.size = params[1].memref.size;
	rc = invoke_jetson_user_key_pta(JETSON_USER_KEY_CMD_GEN_KEY,
					pt_for_generic_pass,
					params_for_generic_pass);

	return rc;
}

TEE_Result TA_CreateEntryPoint(void)
{
	/* Nothing to do */
	return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void)
{
	/* Nothing to do */
}

TEE_Result TA_OpenSessionEntryPoint(uint32_t __unused param_types,
				    TEE_Param __unused params[TEE_NUM_PARAMS],
				    void __unused **session)
{
	return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void __unused *session)
{
}

TEE_Result TA_InvokeCommandEntryPoint(void __unused *session,
				      uint32_t cmd,
				      uint32_t param_types,
				      TEE_Param params[TEE_NUM_PARAMS])
{
	TEE_Result rc;
	uint32_t pa_luks_srv_flag;
	TEE_Param params_lusk_srv_flag[TEE_NUM_PARAMS] = { };

	/* Check LUKS SRV flag */
	pa_luks_srv_flag = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
					   TEE_PARAM_TYPE_VALUE_OUTPUT,
					   TEE_PARAM_TYPE_NONE,
					   TEE_PARAM_TYPE_NONE);
	params_lusk_srv_flag[0].value.a = LUKS_SRV_FLAG;
	rc = invoke_jetson_user_key_pta(JETSON_USER_KEY_CMD_GET_FLAG,
					pa_luks_srv_flag,
					params_lusk_srv_flag);
	if (rc)
		return rc;

	if (!params_lusk_srv_flag[1].value.a)
		return TEE_ERROR_ACCESS_DENIED;

	switch (cmd) {
	case LUKS_SRV_TA_CMD_GET_UNIQUE_PASS:
		return luks_srv_gen_unique_pass(param_types, params);
	case LUKS_SRV_TA_CMD_GET_GENERIC_PASS:
		return luks_srv_gen_generic_pass(param_types, params);
	case LUKS_SRV_TA_CMD_SRV_DOWN:
		pa_luks_srv_flag = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
						   TEE_PARAM_TYPE_NONE,
						   TEE_PARAM_TYPE_NONE,
						   TEE_PARAM_TYPE_NONE);
		params_lusk_srv_flag[0].value.a = LUKS_SRV_FLAG;
		params_lusk_srv_flag[0].value.b = false;
		invoke_jetson_user_key_pta(JETSON_USER_KEY_CMD_SET_FLAG,
					   pa_luks_srv_flag,
					   params_lusk_srv_flag);
		return TEE_SUCCESS;
	default:
		return TEE_ERROR_NOT_SUPPORTED;
	}
}
