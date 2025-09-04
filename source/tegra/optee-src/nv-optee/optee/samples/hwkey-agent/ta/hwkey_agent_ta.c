/*
 * Copyright (c) 2021-2024, NVIDIA Corporation & AFFILIATES. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <inttypes.h>
#include <hwkey_agent_ta.h>
#include <pta_jetson_user_key.h>
#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>
#include <string.h>

#define AES128_KEY_BIT_SIZE		128
#define AES128_KEY_BYTE_SIZE		(AES128_KEY_BIT_SIZE / 8)

typedef struct hwkey_srv_session {
	bool		enc_mode;
	bool		initialized;
	uint8_t		ekb_key[AES128_KEY_BYTE_SIZE];
	TEE_OperationHandle op_handle;
	TEE_ObjectHandle key_handle;
} hwkey_srv_sess_t;

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

static TEE_Result prepare_crypto_op(hwkey_srv_sess_t *sess)
{
	TEE_Result rc = TEE_SUCCESS;
	uint32_t param_types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
					       TEE_PARAM_TYPE_MEMREF_INPUT,
					       TEE_PARAM_TYPE_MEMREF_OUTPUT,
					       TEE_PARAM_TYPE_NONE);
	TEE_Param pta_params[TEE_NUM_PARAMS] = { };
	TEE_Attribute attr;
	uint32_t mode;
	char label_str_for_hwkey_agent_key[] = "hwkey-agent-encryption-key";

	/* Generate hwkey-agent encryption key. */
	pta_params[0].value.a = EKB_USER_KEY_DISK_ENCRYPTION;
	pta_params[1].memref.buffer = label_str_for_hwkey_agent_key;
	pta_params[1].memref.size = strlen(label_str_for_hwkey_agent_key);
	pta_params[2].memref.buffer = sess->ekb_key;
	pta_params[2].memref.size = AES128_KEY_BYTE_SIZE;
	rc = invoke_jetson_user_key_pta(JETSON_USER_KEY_CMD_GEN_UNIQUE_KEY_BY_EKB,
					param_types, pta_params);
	if (rc)
		return rc;

	/* Prepare AES OP. */
	if (sess->enc_mode)
		mode = TEE_MODE_ENCRYPT;
	else
		mode = TEE_MODE_DECRYPT;

	rc = TEE_AllocateOperation(&sess->op_handle,
				   TEE_ALG_AES_CTR,
				   mode,
				   AES128_KEY_BIT_SIZE);
	if (rc != TEE_SUCCESS) {
		EMSG("%s: TEE_AllocationOperation failed.", __func__);
		sess->op_handle = TEE_HANDLE_NULL;
		goto err;
	}

	rc = TEE_AllocateTransientObject(TEE_TYPE_AES,
					 AES128_KEY_BIT_SIZE,
					 &sess->key_handle);
	if (rc != TEE_SUCCESS) {
		EMSG("%s: TEE_AllocateTransientObject failed.", __func__);
		sess->key_handle = TEE_HANDLE_NULL;
		goto err;
	}

	TEE_InitRefAttribute(&attr, TEE_ATTR_SECRET_VALUE,
			     sess->ekb_key, AES128_KEY_BYTE_SIZE);

	rc = TEE_PopulateTransientObject(sess->key_handle, &attr, 1);
	if (rc != TEE_SUCCESS) {
		EMSG("%s: TEE_PopulateTransientObject failed.", __func__);
		goto err;
	}

	rc = TEE_SetOperationKey(sess->op_handle, sess->key_handle);
	if (rc != TEE_SUCCESS) {
		EMSG("%s: TEE_SetOperationKey failed.", __func__);
		goto err;
	}

	return rc;

err:
	if (sess->op_handle != TEE_HANDLE_NULL)
		TEE_FreeOperation(sess->op_handle);
	sess->op_handle = TEE_HANDLE_NULL;

	if (sess->key_handle != TEE_HANDLE_NULL)
		TEE_FreeTransientObject(sess->key_handle);
	sess->key_handle = TEE_HANDLE_NULL;

	return rc;
}

static TEE_Result hwkey_crypto_op(hwkey_srv_sess_t *sess,
				  uint32_t param_types,
				  TEE_Param params[TEE_NUM_PARAMS])
{
	TEE_Result rc = TEE_SUCCESS;
	uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
					  TEE_PARAM_TYPE_MEMREF_INPUT,
					  TEE_PARAM_TYPE_MEMREF_OUTPUT,
					  TEE_PARAM_TYPE_NONE);

	if (exp_pt != param_types)
		return TEE_ERROR_BAD_PARAMETERS;

	if ((params[0].memref.size != AES128_KEY_BYTE_SIZE) ||
	    (params[2].memref.size < params[1].memref.size))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!sess->initialized) {
		rc = prepare_crypto_op(sess);
		if (rc == TEE_SUCCESS)
			sess->initialized = true;
		else
			return rc;
	}

	/* Init cipher with IV. */
	TEE_CipherInit(sess->op_handle,
		       params[0].memref.buffer,
		       params[0].memref.size);

	/* Cipher update. */
	rc = TEE_CipherUpdate(sess->op_handle,
			      params[1].memref.buffer, params[1].memref.size,
			      params[2].memref.buffer, &params[2].memref.size);

	return rc;
}

static TEE_Result hwkey_get_rng_op(uint32_t param_types,
				   TEE_Param params[TEE_NUM_PARAMS])
{
	TEE_Result rc = TEE_SUCCESS;
	uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
					  TEE_PARAM_TYPE_NONE,
					  TEE_PARAM_TYPE_NONE,
					  TEE_PARAM_TYPE_NONE);
	uint32_t pta_pa = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
					  TEE_PARAM_TYPE_NONE,
					  TEE_PARAM_TYPE_NONE,
					  TEE_PARAM_TYPE_NONE);
	TEE_Param pta_params[TEE_NUM_PARAMS] = { };

	if (exp_pt != param_types)
		return TEE_ERROR_BAD_PARAMETERS;

	/* Query random bytes. */
	pta_params[0].memref.buffer = params[0].memref.buffer;
	pta_params[0].memref.size = params[0].memref.size;
	rc = invoke_jetson_user_key_pta(JETSON_USER_KEY_CMD_GET_RANDOM,
					pta_pa, pta_params);
	if (rc)
		return rc;

	params[0].memref.size = pta_params[0].memref.size;

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
	hwkey_srv_sess_t *sess;

	sess = TEE_Malloc(sizeof(*sess), TEE_MALLOC_FILL_ZERO);
	if (!sess)
		return TEE_ERROR_OUT_OF_MEMORY;

	*session = (void*)sess;

	return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void *session)
{
	hwkey_srv_sess_t *sess;

	sess = (hwkey_srv_sess_t*)session;

	if (sess->key_handle != TEE_HANDLE_NULL)
		TEE_FreeTransientObject(sess->key_handle);
	if (sess->op_handle != TEE_HANDLE_NULL)
		TEE_FreeOperation(sess->op_handle);

	TEE_Free(sess);
}

TEE_Result TA_InvokeCommandEntryPoint(void *session,
				      uint32_t cmd,
				      uint32_t param_types,
				      TEE_Param params[TEE_NUM_PARAMS])
{
	hwkey_srv_sess_t *sess = (hwkey_srv_sess_t*)session;

	switch (cmd) {
	case HWKEY_AGENT_TA_CMD_ENCRYPTION:
		sess->enc_mode = true;
		return hwkey_crypto_op(sess, param_types, params);
	case HWKEY_AGENT_TA_CMD_DECRYPTION:
		sess->enc_mode = false;
		return hwkey_crypto_op(sess, param_types, params);
	case HWKEY_AGENT_TA_CMD_GET_RANDOM:
		return hwkey_get_rng_op(param_types, params);
	default:
		return TEE_ERROR_NOT_SUPPORTED;
	}
}
