/*
 * Copyright (c) 2023-2024, NVIDIA Corporation & AFFILIATES. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <argp.h>
#include <ftpm_helper_ta.h>
#include <err.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/types.h>
#include <tee_client_api.h>
#include <unistd.h>

#define FTPM_HELPER_GET_RSA_EK_CERT	(1 << 0)
#define FTPM_HELPER_GET_EC_EK_CERT	(1 << 1)
#define FTPM_HELPER_GET_EVT_LOG_MB2_SIG	(1 << 2)
#define FTPM_HELPER_GET_EVT_LOG_TOS_SIG	(1 << 3)
#define FTPM_HELPER_QUERY_ECID		(1 << 4)
#define FTPM_HELPER_QUERY_SN		(1 << 5)
#define FTPM_HELPER_INJECT_EPS		(1 << 6)

static struct argp_option options[] = {
	{0, 'a', "OUTFILE", 0, "Output file of the fTPM RSA EK Certificate"},
	{0, 'b', "OUTFILE", 0, "Output file of the fTPM EC EK Certificate"},
	{0, 'c', "OUTFILE", 0, "Output file of the signature of the MB2 event log"},
	{0, 'd', "OUTFILE", 0, "Output file of the signature of the TOS event log"},
	{0, 'e', NULL, 0, "Query the device ECID value"},
	{0, 'f', NULL, 0, "Query the device serial number"},
	{0, 'g', "EPS", 0, "Inject an EPS(starts with \"0x\", 64 bytes, big endian) into fTPM"},
	{ 0 },
};

struct arguments {
	uint32_t ftpm_helper_options;
	char *out_rsa_ek_cert;
	char *out_ec_ek_cert;
	char *out_evt_log_mb2_sig;
	char *out_evt_log_tos_sig;
	char *inject_eps_value;
};

typedef struct ftpm_helper_ca_ctx {
	TEEC_Context ctx;
	TEEC_Session sess;
	struct arguments *argus;
	FILE *fd_out_rsa_ek_cert;
	FILE *fd_out_ec_ek_cert;
	FILE *fd_out_evt_log_mb2_sig;
	FILE *fd_out_evt_log_tos_sig;
} ftpm_helper_ca_ctx_t;
static ftpm_helper_ca_ctx_t ca_sess;

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	struct arguments *argus = state->input;

	switch (key) {
	case 'a':
		argus->ftpm_helper_options |= FTPM_HELPER_GET_RSA_EK_CERT;
		if (arg)
			argus->out_rsa_ek_cert = arg;
		break;
	case 'b':
		argus->ftpm_helper_options |= FTPM_HELPER_GET_EC_EK_CERT;
		if (arg)
			argus->out_ec_ek_cert = arg;
		break;
	case 'c':
		argus->ftpm_helper_options |= FTPM_HELPER_GET_EVT_LOG_MB2_SIG;
		if (arg)
			argus->out_evt_log_mb2_sig = arg;
		break;
	case 'd':
		argus->ftpm_helper_options |= FTPM_HELPER_GET_EVT_LOG_TOS_SIG;
		if (arg)
			argus->out_evt_log_tos_sig = arg;
		break;
	case 'e':
		argus->ftpm_helper_options |= FTPM_HELPER_QUERY_ECID;
		break;
	case 'f':
		argus->ftpm_helper_options |= FTPM_HELPER_QUERY_SN;
		break;
	case 'g':
		argus->ftpm_helper_options |= FTPM_HELPER_INJECT_EPS;
		if (arg)
			argus->inject_eps_value = arg;
		break;
	case ARGP_KEY_END:
		if (argus->ftpm_helper_options == 0)
			argp_usage(state);
		break;
	case ARGP_KEY_ARG:
		if (state->argc <= 1)
			argp_usage(state);
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static struct argp argp = { options, parse_opt, NULL, 0, 0, 0, 0 };

static TEEC_Result prepare_tee_session(ftpm_helper_ca_ctx_t *ctx)
{
	TEEC_UUID uuid = FTPM_HELPER_TA_UUID;
	uint32_t origin;
	TEEC_Result rc;

	rc = TEEC_InitializeContext(NULL, &ctx->ctx);
	if (TEEC_SUCCESS != rc)
		goto tee_session_fail;

	rc = TEEC_OpenSession(&ctx->ctx, &ctx->sess, &uuid,
			      TEEC_LOGIN_PUBLIC, NULL, NULL, &origin);

	if (TEEC_SUCCESS != rc)
		TEEC_FinalizeContext(&ctx->ctx);

tee_session_fail:
	return rc;
}

static void terminate_tee_session(ftpm_helper_ca_ctx_t *ctx)
{
	struct arguments *argus = ctx->argus;

	if (ctx->sess.session_id > 0)
		TEEC_CloseSession(&ctx->sess);

	if (ctx->ctx.fd > 0)
		TEEC_FinalizeContext(&ctx->ctx);

	if (FTPM_HELPER_GET_RSA_EK_CERT & argus->ftpm_helper_options) {
		if (NULL != ctx->fd_out_rsa_ek_cert)
			fclose(ctx->fd_out_rsa_ek_cert);
	}

	if (FTPM_HELPER_GET_EC_EK_CERT & argus->ftpm_helper_options) {
		if (NULL != ctx->fd_out_ec_ek_cert)
			fclose(ctx->fd_out_ec_ek_cert);
	}

	if (FTPM_HELPER_GET_EVT_LOG_MB2_SIG & argus->ftpm_helper_options) {
		if (NULL != ctx->fd_out_evt_log_mb2_sig)
			fclose(ctx->fd_out_evt_log_mb2_sig);
	}

	if (FTPM_HELPER_GET_EVT_LOG_TOS_SIG & argus->ftpm_helper_options) {
		if (NULL != ctx->fd_out_evt_log_tos_sig)
			fclose(ctx->fd_out_evt_log_tos_sig);
	}
}

static void fail_handler(int i)
{
	terminate_tee_session(&ca_sess);

	exit(i);
}

static void ca_query_ecid(void)
{
	TEEC_Operation op;
	TEEC_Result rc;
	uint8_t *ecid_buf = NULL;
	uint32_t cmd, origin;

	ecid_buf = calloc(1, FTPM_HELPER_TA_ECID_LENGTH);
	if (!ecid_buf) {
		fprintf(stderr, "%s: out of memory.\n", __func__);
		return;
	}

	memset(&op, 0, sizeof(op));
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT,
					 TEEC_NONE,
					 TEEC_NONE,
					 TEEC_NONE);
	op.params[0].tmpref.buffer = ecid_buf;
	op.params[0].tmpref.size = FTPM_HELPER_TA_ECID_LENGTH;
	cmd = FTPM_HELPER_TA_CMD_QUERY_ECID;

	/* Send command to TA. */
	rc = TEEC_InvokeCommand(&ca_sess.sess, cmd, &op, &origin);
	if (rc != TEEC_SUCCESS) {
		fprintf(stderr, "%s: TEEC_InvokeCommand failed 0x%x origin 0x%x\n",
			__func__, rc, origin);
		goto out;
	}

	fprintf(stdout, "%16lx", *(uint64_t*)&ecid_buf[0]);
	fprintf(stdout, "\n");

out:
	free(ecid_buf);
}

static void ca_query_sn(void)
{
	TEEC_Operation op;
	TEEC_Result rc;
	uint8_t *sn_buf = NULL;
	uint32_t cmd, origin;
	int i = 0;

	sn_buf = calloc(1, FTPM_HELPER_TA_SN_LENGTH);
	if (!sn_buf) {
		fprintf(stderr, "%s: out of memory.\n", __func__);
		return;
	}

	memset(&op, 0, sizeof(op));
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT,
					 TEEC_NONE,
					 TEEC_NONE,
					 TEEC_NONE);
	op.params[0].tmpref.buffer = sn_buf;
	op.params[0].tmpref.size = FTPM_HELPER_TA_SN_LENGTH;
	cmd = FTPM_HELPER_TA_CMD_QUERY_SN;

	/* Send command to TA. */
	rc = TEEC_InvokeCommand(&ca_sess.sess, cmd, &op, &origin);
	if (rc != TEEC_SUCCESS) {
		fprintf(stderr, "%s: TEEC_InvokeCommand failed 0x%x origin 0x%x\n",
			__func__, rc, origin);
		goto out;
	}

	for (i = 0; i < FTPM_HELPER_TA_SN_LENGTH; i++)
		fprintf(stdout, "%02x", sn_buf[i]);
	fprintf(stdout, "\n");

out:
	free(sn_buf);
}

static TEEC_Result ca_query_ftpm_prop(uint32_t ta_cmd,
			       uint32_t buf_size,
			       FILE *out_fptr)
{
	TEEC_Operation op;
	TEEC_Result rc;
	uint8_t *buf = NULL;
	uint32_t cmd, origin;

	if (!out_fptr) {
		fprintf(stderr, "%s: invalid file ptr.\n", __func__);
		return TEEC_ERROR_BAD_PARAMETERS;
	}

	buf = calloc(1, buf_size);
	if (!buf) {
		fprintf(stderr, "%s: out of memory.\n", __func__);
		return TEEC_ERROR_OUT_OF_MEMORY;
	}

	memset(&op, 0, sizeof(op));
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT,
					 TEEC_NONE,
					 TEEC_NONE,
					 TEEC_NONE);
	op.params[0].tmpref.buffer = buf;
	op.params[0].tmpref.size = buf_size;
	cmd = ta_cmd;

	/* Send command to TA. */
	rc = TEEC_InvokeCommand(&ca_sess.sess, cmd, &op, &origin);
	if (rc != TEEC_SUCCESS) {
		fprintf(stderr, "%s: TEEC_InvokeCommand failed 0x%x origin 0x%x\n",
			__func__, rc, origin);
		goto out;
	}

	/* Store the signature. */
	if (op.params[0].tmpref.size > 0)
		fwrite(buf, op.params[0].tmpref.size, 1, out_fptr);

out:
	free(buf);

	return rc;
}

static TEEC_Result ca_inject_eps(uint8_t *eps, int eps_len)
{
	TEEC_Operation op;
	TEEC_Result rc = TEEC_SUCCESS;
	uint32_t cmd, origin;

	if (eps == NULL || eps_len != FTPM_HELPER_TA_EPS_BYTES)
		return TEEC_ERROR_BAD_PARAMETERS;

	memset(&op, 0, sizeof(op));
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
					 TEEC_NONE,
					 TEEC_NONE,
					 TEEC_NONE);
	op.params[0].tmpref.buffer = eps;
	op.params[0].tmpref.size = eps_len;
	cmd = FTPM_HELPER_TA_CMD_INJECT_EPS;

	/* Send command to TA. */
	rc = TEEC_InvokeCommand(&ca_sess.sess, cmd, &op, &origin);
	if (rc != TEEC_SUCCESS) {
		fprintf(stderr, "%s: TEEC_InvokeCommand failed 0x%x origin 0x%x\n",
			__func__, rc, origin);
		goto out;
	}

out:
	return rc;
}

static int hex_char_to_nibble(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	return TEEC_ERROR_BAD_PARAMETERS;
}

static TEEC_Result parse_eps_value(const char *input, uint8_t *eps, int eps_len)
{
	int i, j;
	int high, low;
	int eps_string_len = FTPM_HELPER_TA_EPS_BYTES * 2 + 2;

	if (input == NULL || eps == NULL || eps_len != FTPM_HELPER_TA_EPS_BYTES) {
		fprintf(stderr, "Invalid EPS parameters or length.\n");
		return TEEC_ERROR_BAD_PARAMETERS;
	}

	if (strlen(input) != eps_string_len) {
		fprintf(stderr, "Invalid length of the EPS string. The length must be %d\n", eps_string_len);
		return TEEC_ERROR_BAD_PARAMETERS;
	}

	if (input[0] != '0' || (input[1] != 'x' && input[1] != 'X')) {
		fprintf(stderr, "The EPS value must start with \'0x\'.\n");
		return TEEC_ERROR_BAD_PARAMETERS;
	}

	memset(eps, 0, eps_len);
	for (i = 2, j = 0; i < (eps_len * 2 + 2) && j < eps_len; i += 2, j++) {
		high = hex_char_to_nibble(input[i]);
		low = hex_char_to_nibble(input[i + 1]);

		if (high == TEEC_ERROR_BAD_PARAMETERS || low == TEEC_ERROR_BAD_PARAMETERS) {
			fprintf(stderr, "Invalid EPS hex string.\n");
			return TEEC_ERROR_BAD_PARAMETERS;
		}

		eps[j] = (high << 4) | low;
	}

	return TEEC_SUCCESS;
}

void handle_ftpm_helper_options(ftpm_helper_ca_ctx_t *ctx)
{
	struct arguments *argus = ctx->argus;
	TEEC_Result rc = TEEC_SUCCESS;
	uint8_t eps[FTPM_HELPER_TA_EPS_BYTES];

	if (FTPM_HELPER_GET_RSA_EK_CERT & argus->ftpm_helper_options) {
		if (!argus->out_rsa_ek_cert) {
			fprintf(stderr, "Error: missing -a option.\n");
			fail_handler(1);
		}

		ctx->fd_out_rsa_ek_cert = fopen(argus->out_rsa_ek_cert, "wb");
		rc = ca_query_ftpm_prop(FTPM_HELPER_TA_CMD_GET_RSA_EK_CERT,
					FTPM_EK_CERT_BUF_SIZE,
					ctx->fd_out_rsa_ek_cert);
		if (rc != TEEC_SUCCESS)
			fail_handler(1);
	}

	if (FTPM_HELPER_GET_EC_EK_CERT & argus->ftpm_helper_options) {
		if (!argus->out_ec_ek_cert) {
			fprintf(stderr, "Error: missing -b option.\n");
			fail_handler(1);
		}

		ctx->fd_out_ec_ek_cert = fopen(argus->out_ec_ek_cert, "wb");
		rc = ca_query_ftpm_prop(FTPM_HELPER_TA_CMD_GET_EC_EK_CERT,
					FTPM_EK_CERT_BUF_SIZE,
					ctx->fd_out_ec_ek_cert);
		if (rc != TEEC_SUCCESS)
			fail_handler(1);
	}

	if (FTPM_HELPER_GET_EVT_LOG_MB2_SIG & argus->ftpm_helper_options) {
		if (!argus->out_evt_log_mb2_sig) {
			fprintf(stderr, "Error: missing -c option.\n");
			fail_handler(1);
		}

		ctx->fd_out_evt_log_mb2_sig = fopen(argus->out_evt_log_mb2_sig, "wb");
		ca_query_ftpm_prop(FTPM_HELPER_TA_CMD_GET_EVT_LOG_SIG_MB2,
				   FTPM_EVT_LOG_SIG_BUF_SIZE,
				   ctx->fd_out_evt_log_mb2_sig);
	}

	if (FTPM_HELPER_GET_EVT_LOG_TOS_SIG & argus->ftpm_helper_options) {
		if (!argus->out_evt_log_tos_sig) {
			fprintf(stderr, "Error: missing -d option.\n");
			fail_handler(1);
		}

		ctx->fd_out_evt_log_tos_sig = fopen(argus->out_evt_log_tos_sig, "wb");
		ca_query_ftpm_prop(FTPM_HELPER_TA_CMD_GET_EVT_LOG_SIG_TOS,
				   FTPM_EVT_LOG_SIG_BUF_SIZE,
				   ctx->fd_out_evt_log_tos_sig);
	}

	if (FTPM_HELPER_QUERY_ECID & argus->ftpm_helper_options)
		ca_query_ecid();

	if (FTPM_HELPER_QUERY_SN & argus->ftpm_helper_options)
		ca_query_sn();

	if (FTPM_HELPER_INJECT_EPS & argus->ftpm_helper_options) {
		if (!argus->inject_eps_value) {
			fprintf(stderr, "Error: missing -g option.\n");
			fail_handler(1);
		}

		rc = parse_eps_value(argus->inject_eps_value, eps, FTPM_HELPER_TA_EPS_BYTES);
		if (rc != TEEC_SUCCESS)
			fail_handler(1);

		rc = ca_inject_eps(eps, FTPM_HELPER_TA_EPS_BYTES);
		if (rc != TEEC_SUCCESS)
			fail_handler(1);
	}
}

int main(int argc, char *argv[])
{
	struct arguments argus;

	/* Initialize the arguments */
	memset(&argus, 0, sizeof(struct arguments));

	/* Handle the break signal */
	signal(SIGINT, fail_handler);

	/* Handle the input parameters */
	argp_parse(&argp, argc, argv, 0, 0, &argus);
	ca_sess.argus = &argus;
	if(prepare_tee_session(&ca_sess))
		goto err_out;

	handle_ftpm_helper_options(&ca_sess);

err_out:
	terminate_tee_session(&ca_sess);

	return 0;
}
