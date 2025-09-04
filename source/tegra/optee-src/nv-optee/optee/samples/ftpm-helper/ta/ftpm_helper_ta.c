/*
 * Copyright (c) 2023-2024, NVIDIA Corporation & AFFILIATES. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <conf.h>
#include <tee_internal_api.h>
#ifdef CFG_JETSON_FTPM_HELPER_PTA
#include <ftpm_helper_ta.h>
#include <inttypes.h>
#include <pta_jetson_ftpm_helper.h>
#include <stdlib.h>
#include <string.h>
#include <tee_internal_api_extensions.h>
#include <utee_defines.h>
#include <mbedtls/asn1.h>
#include <mbedtls/asn1write.h>
#include <mbedtls/bignum.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/error.h>

/* EK CSR structure */
typedef struct ek_csr_t {
	uint8_t *csr_head;
	uint32_t csr_total_len;
	uint8_t *tbs_ptr;
	uint32_t tbs_len;
	uint8_t *sig;
	uint32_t sig_len;
} ek_csr_t;

static TEE_Result invoke_ftpm_helper_pta(uint32_t cmd_id,
					 uint32_t param_types,
					 TEE_Param params[TEE_NUM_PARAMS])
{
	static TEE_TASessionHandle pta_sess = TEE_HANDLE_NULL;
	const TEE_UUID uuid = FTPM_HELPER_PTA_UUID;

	if (TEE_HANDLE_NULL == pta_sess) {
		TEE_Result rc = TEE_OpenTASession(&uuid, TEE_TIMEOUT_INFINITE,
						   0, NULL, &pta_sess, NULL);

		if (rc)
			return rc;
	}

	return TEE_InvokeTACommand(pta_sess, TEE_TIMEOUT_INFINITE, cmd_id,
				   param_types, params, NULL);
}

static TEE_Result convert_raw_ec_signature_to_asn1_der(uint8_t *raw_sig,
						       size_t *sig_len,
						       uint32_t sig_buf_len)
{
	TEE_Result rc = TEE_SUCCESS;
	mbedtls_mpi r;
	mbedtls_mpi s;
	uint32_t r_len;
	uint8_t *buf = NULL;
	uint8_t *p = NULL;
	uint32_t len = 0;
	int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;

	if ((raw_sig == NULL) ||
	    (*sig_len == 0) ||
	    (sig_buf_len == 0))
	    return TEE_ERROR_BAD_PARAMETERS;

	buf = calloc(1, MBEDTLS_ECDSA_MAX_LEN);
	if (!buf)
		return TEE_ERROR_OUT_OF_MEMORY;
	p = buf + MBEDTLS_ECDSA_MAX_LEN;

	/* Convert the raw signature to ASN1 DER format. */
	r_len = *sig_len / 2;
	mbedtls_mpi_init(&r);
	mbedtls_mpi_init(&s);

	mbedtls_mpi_read_binary(&r, raw_sig, r_len);
	mbedtls_mpi_read_binary(&s, raw_sig + r_len, r_len);

	MBEDTLS_ASN1_CHK_ADD(len, mbedtls_asn1_write_mpi(&p, buf, &s));
	MBEDTLS_ASN1_CHK_ADD(len, mbedtls_asn1_write_mpi(&p, buf, &r));

	MBEDTLS_ASN1_CHK_ADD(len, mbedtls_asn1_write_len(&p, buf, len));
	MBEDTLS_ASN1_CHK_ADD(len, mbedtls_asn1_write_tag(&p, buf, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE));

	if (len <= sig_buf_len) {
		memcpy(raw_sig, p, len);
		*sig_len = len;
	} else {
		rc = TEE_ERROR_SHORT_BUFFER;
	}

	mbedtls_mpi_free(&r);
	mbedtls_mpi_free(&s);
	free(buf);

	return rc;
}

static TEE_Result query_pta(uint32_t cmd_id,
			    uint32_t data_len,
			    uint32_t ptypes,
			    TEE_Param params[TEE_NUM_PARAMS])
{
	uint32_t exp_pt;
	TEE_Param pta_params[TEE_NUM_PARAMS] = { };
	TEE_Result rc = TEE_SUCCESS;

	/* Validate the input parameters. */
	exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
				 TEE_PARAM_TYPE_NONE,
				 TEE_PARAM_TYPE_NONE,
				 TEE_PARAM_TYPE_NONE);
	if (exp_pt != ptypes)
		return TEE_ERROR_BAD_PARAMETERS;

	if (data_len != params[0].memref.size)
		return TEE_ERROR_BAD_PARAMETERS;

	pta_params[0].memref.buffer = params[0].memref.buffer;
	pta_params[0].memref.size = params[0].memref.size;

	rc = invoke_ftpm_helper_pta(cmd_id,
				    exp_pt, pta_params);
	if (rc != TEE_SUCCESS)
		return rc;

	params[0].memref.size = pta_params[0].memref.size;

	return rc;
}

#if defined(CFG_JETSON_FTPM_HELPER_INJECT_EPS)
static TEE_Result ta_inject_eps(uint32_t ptypes, TEE_Param params[TEE_NUM_PARAMS])
{
	uint32_t exp_pt;

	/* Validate the input parameters. */
	exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
				 TEE_PARAM_TYPE_NONE,
				 TEE_PARAM_TYPE_NONE,
				 TEE_PARAM_TYPE_NONE);
	if (exp_pt != ptypes)
		return TEE_ERROR_BAD_PARAMETERS;

	if (params[0].memref.size != FTPM_HELPER_TA_EPS_BYTES)
		return TEE_ERROR_BAD_PARAMETERS;

	return invoke_ftpm_helper_pta(FTPM_HELPER_PTA_CMD_INJECT_EPS, exp_pt, params);
}
#else
static TEE_Result ta_inject_eps(uint32_t __unused ptypes,
				TEE_Param __unused params[TEE_NUM_PARAMS])
{
	return TEE_ERROR_NOT_SUPPORTED;
}
#endif

TEE_Result TA_CreateEntryPoint(void)
{
	TEE_Result rc = TEE_SUCCESS;

	return rc;
}

void TA_DestroyEntryPoint(void)
{
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
				      uint32_t __unused cmd,
				      uint32_t __unused param_types,
				      TEE_Param __unused params[TEE_NUM_PARAMS])
{
	TEE_Result rc = TEE_SUCCESS;

	switch (cmd) {
	case FTPM_HELPER_TA_CMD_QUERY_SN:
		return query_pta(FTPM_HELPER_PTA_CMD_QUERY_SN,
				 FTPM_HELPER_TA_SN_LENGTH,
				 param_types, params);
	case FTPM_HELPER_TA_CMD_QUERY_ECID:
		return query_pta(FTPM_HELPER_PTA_CMD_QUERY_ECID,
				 FTPM_HELPER_TA_ECID_LENGTH,
				 param_types, params);
	case FTPM_HELPER_TA_CMD_GET_EVT_LOG_SIG_MB2:
		rc = query_pta(FTPM_HELPER_PTA_CMD_GET_EVT_LOG_SIG_MB2,
			       FTPM_EVT_LOG_SIG_BUF_SIZE,
			       param_types, params);
		if (rc)
			return rc;
		return convert_raw_ec_signature_to_asn1_der(params[0].memref.buffer,
							    &params[0].memref.size,
							    FTPM_EVT_LOG_SIG_BUF_SIZE);
	case FTPM_HELPER_TA_CMD_GET_EVT_LOG_SIG_TOS:
		rc = query_pta(FTPM_HELPER_PTA_CMD_GET_EVT_LOG_SIG_TOS,
			       FTPM_EVT_LOG_SIG_BUF_SIZE,
			       param_types, params);
		if (rc)
			return rc;
		return convert_raw_ec_signature_to_asn1_der(params[0].memref.buffer,
							    &params[0].memref.size,
							    FTPM_EVT_LOG_SIG_BUF_SIZE);
	case FTPM_HELPER_TA_CMD_GET_RSA_EK_CERT:
		return query_pta(FTPM_HELPER_PTA_CMD_GET_RSA_EK_CERT,
				 FTPM_EK_CERT_BUF_SIZE, param_types, params);
	case FTPM_HELPER_TA_CMD_GET_EC_EK_CERT:
		return query_pta(FTPM_HELPER_PTA_CMD_GET_EC_EK_CERT,
				 FTPM_EK_CERT_BUF_SIZE, param_types, params);
	case FTPM_HELPER_TA_CMD_INJECT_EPS:
		return ta_inject_eps(param_types, params);
	default:
		return TEE_ERROR_NOT_SUPPORTED;
	}
}
#else /* CFG_JETSON_FTPM_HELPER_PTA */
TEE_Result TA_CreateEntryPoint(void)
{
	return TEE_ERROR_NOT_SUPPORTED;
}

void TA_DestroyEntryPoint(void)
{
}

TEE_Result TA_OpenSessionEntryPoint(uint32_t __unused param_types,
				    TEE_Param __unused params[TEE_NUM_PARAMS],
				    void __unused **session)
{
	return TEE_ERROR_NOT_SUPPORTED;
}

void TA_CloseSessionEntryPoint(void __unused *session)
{
}

TEE_Result TA_InvokeCommandEntryPoint(void __unused *session,
				      uint32_t __unused cmd,
				      uint32_t __unused param_types,
				      TEE_Param __unused params[TEE_NUM_PARAMS])
{
	return TEE_ERROR_NOT_SUPPORTED;
}
#endif /* CFG_JETSON_FTPM_HELPER_PTA */
