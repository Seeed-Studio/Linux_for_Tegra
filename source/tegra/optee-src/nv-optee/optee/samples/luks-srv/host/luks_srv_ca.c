/*
 * Copyright (c) 2023, NVIDIA Corporation & AFFILIATES. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <argp.h>
#include <err.h>
#include <luks_srv_ta.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tee_client_api.h>
#include <unistd.h>

typedef enum {
	OPT_NONE,
	OPT_NO_PASS_RESPONSE,
	OPT_GENERIC_PASSPHRASE,
	OPT_UNIQUE_PASSPHRASE,
} LUKS_APP_OPT_t;

static char args_doc[] = "-c <context string> -u | -n";

static struct argp_option options[] = {
	{"context-string", 'c', "CONTEXT STRING", 0, "The context string for passphrase generation (Max length: 40)."},
	{"get-generic-pass", 'g', 0, 0, "Get generic passphrase."},
	{"get-unique-pass", 'u', 0 , 0, "Get unique passphrase."},
	{"no-pass-response", 'n', 0, 0, "No passphrase response after this command."},
	{ 0 }
};

struct arguments {
	char *context_str;
	LUKS_APP_OPT_t opt;
};

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	struct arguments *argus = state->input;

	switch (key) {
	case 'c':
		argus->context_str = strdup(arg);
		break;
	case 'g':
		argus->opt = OPT_GENERIC_PASSPHRASE;
		break;
	case 'u':
		argus->opt = OPT_UNIQUE_PASSPHRASE;
		break;
	case 'n':
		argus->opt = OPT_NO_PASS_RESPONSE;
		break;
	case ARGP_KEY_ARG:
		if (state->argc < 1)
			argp_usage(state);
	case ARGP_KEY_END:
		if (!argus->context_str ||
		    strlen(argus->context_str) > LUKS_SRV_CONTEXT_STR_LEN)
			if (OPT_NO_PASS_RESPONSE != argus->opt)
				argp_usage(state);
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

static struct argp argp = { options, parse_opt, args_doc, 0, 0, 0, 0 };

typedef struct luks_ca_ctx {
	TEEC_Context ctx;
	TEEC_Session sess;
} luks_ca_ctx_t;

static void prepare_tee_session(luks_ca_ctx_t *ctx)
{
	TEEC_UUID uuid = LUKS_SRV_TA_UUID;
	uint32_t origin;
	TEEC_Result rc;

	rc = TEEC_InitializeContext(NULL, &ctx->ctx);
	if (rc != TEEC_SUCCESS)
		errx(1, "TEEC_InitializeContext failed with code 0x%x", rc);

	rc = TEEC_OpenSession(&ctx->ctx, &ctx->sess, &uuid,
			      TEEC_LOGIN_PUBLIC, NULL, NULL, &origin);
	if (rc != TEEC_SUCCESS) {
		TEEC_FinalizeContext(&ctx->ctx);
		errx(1, "TEEC_Opensession failed with code 0x%x origin 0x%x",
			rc, origin);
	}
}

static void terminate_tee_session(luks_ca_ctx_t *ctx)
{
	TEEC_CloseSession(&ctx->sess);
	TEEC_FinalizeContext(&ctx->ctx);
}

static void luks_srv_app_handler(struct arguments *argus, luks_ca_ctx_t *ctx)
{
	TEEC_Operation op;
	TEEC_Result rc;
	uint32_t origin, cmd_id;
	uint32_t i;
	uint8_t passphrase[LUKS_SRV_PASSPHRASE_LEN];

	memset(&op, 0, sizeof(op));

	switch (argus->opt) {
	case OPT_NO_PASS_RESPONSE:
		cmd_id = LUKS_SRV_TA_CMD_SRV_DOWN;
		break;
	case OPT_GENERIC_PASSPHRASE:
		cmd_id = LUKS_SRV_TA_CMD_GET_GENERIC_PASS;
		break;
	case OPT_UNIQUE_PASSPHRASE:
		cmd_id = LUKS_SRV_TA_CMD_GET_UNIQUE_PASS;
		break;
	default:
		printf("nvluks-srv: unknown option!\n");
		return;
	}

	if ((LUKS_SRV_TA_CMD_GET_UNIQUE_PASS == cmd_id) ||
	    (LUKS_SRV_TA_CMD_GET_GENERIC_PASS == cmd_id)) {
		op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
						 TEEC_MEMREF_TEMP_OUTPUT,
						 TEEC_NONE,
						 TEEC_NONE);
		op.params[0].tmpref.buffer = argus->context_str;
		op.params[0].tmpref.size = strlen(argus->context_str);
		op.params[1].tmpref.buffer = passphrase;
		op.params[1].tmpref.size = LUKS_SRV_PASSPHRASE_LEN;
	}

	/* Send command to TA. */
	rc = TEEC_InvokeCommand(&ctx->sess, cmd_id, &op, &origin);
	if (rc != TEEC_SUCCESS) {
		printf("TEEC_InvokeCommand failed 0x%x origin 0x%x\n", rc,
		       origin);
		return;
	}

	if ((LUKS_SRV_TA_CMD_GET_UNIQUE_PASS == cmd_id) ||
	    (LUKS_SRV_TA_CMD_GET_GENERIC_PASS == cmd_id)) {
		for (i = 0; i < LUKS_SRV_PASSPHRASE_LEN; i ++)
			fprintf(stdout, "%02x", passphrase[i]);
		fprintf(stdout, "\n");
	}

	if (argus->context_str)
		free(argus->context_str);
}

int main(int argc, char *argv[])
{
	struct arguments argus = {
		.context_str = NULL,
		.opt = OPT_NONE,
	};
	luks_ca_ctx_t ca_sess;

	/* Handle the input parameters */
	argp_parse(&argp, argc, argv, 0, 0, &argus);

	prepare_tee_session(&ca_sess);

	luks_srv_app_handler(&argus, &ca_sess);

	terminate_tee_session(&ca_sess);

	return 0;
}
