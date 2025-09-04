/*
 * Copyright (c) 2021, NVIDIA Corporation & AFFILIATES. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <argp.h>
#include <err.h>
#include <hwkey_agent_ta.h>
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

#define AES_BLOCK_SIZE 16
#define CRYPTO_PAYLOAD_SIZE 4096
#define RNG_SRV_DATA_SIZE 2048

/*
 * Note that, the default_iv can be a input factor of the CA program.
 */
static uint8_t default_iv[AES_BLOCK_SIZE] = {
	0x36, 0xeb, 0x39, 0xfe, 0x3a, 0xcf, 0x1a, 0xf5,
	0x68, 0xc1, 0xb8, 0xe6, 0xf4, 0x8e, 0x5c, 0x79,
};

static char args_doc[] = "-e [-d] -i <file> -o <out-file> or -r <random size>";

static struct argp_option options[] = {
	{ 0, 'e', 0, 0, "Encryption mode"},
	{ 0, 'd', 0, 0, "Decryption mode"},
	{"in", 'i', "file", 0, "Input file for encrypt/decrypt"},
	{"out", 'o', "outfile", 0, "Output file" },
	{"get_random", 'r', "len", 0, "Get random number (input random number length)"},
	{ 0 }
};

struct arguments {
	bool encryption;
	char* in_file;
	char* out_file;
	bool get_random;
	int rng_size;
};

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	struct arguments *argus = state->input;

	switch (key) {
	case 'e':
		argus->encryption = true;
		break;
	case 'd':
		argus->encryption = false;
		break;
	case 'i':
		if (arg)
			argus->in_file = strdup(arg);
		break;
	case 'o':
		if (arg)
			argus->out_file = strdup(arg);
		break;
	case 'r':
		argus->get_random = true;
		if (arg)
			argus->rng_size = atoi(arg);
		else
			argus->rng_size = 16;
		break;
	case ARGP_KEY_ARG:
	case ARGP_KEY_END:
		if (state->argc <= 1)
			argp_usage(state);
		if (argus->in_file && state->argc <= 5)
			argp_usage(state);
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

static struct argp argp = { options, parse_opt, args_doc, 0, 0, 0, 0 };

typedef struct hwkey_ca_ctx {
	TEEC_Context ctx;
	TEEC_Session sess;
	FILE *infptr;
	FILE *outfptr;
	struct arguments *argus;
} hwkey_ca_ctx_t;
static hwkey_ca_ctx_t ca_sess;

static uint8_t iv[AES_BLOCK_SIZE] = { 0 };

static TEEC_Result prepare_tee_session(hwkey_ca_ctx_t *ctx)
{
	TEEC_UUID uuid = HWKEY_AGENT_TA_UUID;
	uint32_t origin;
	TEEC_Result rc;

	rc = TEEC_InitializeContext(NULL, &ctx->ctx);
	if (rc != TEEC_SUCCESS)
		goto tee_session_fail;

	rc = TEEC_OpenSession(&ctx->ctx, &ctx->sess, &uuid,
			      TEEC_LOGIN_PUBLIC, NULL, NULL, &origin);
	if (rc != TEEC_SUCCESS) {
		TEEC_FinalizeContext(&ctx->ctx);
		goto tee_session_fail;
	}

tee_session_fail:
	return rc;
}

static void terminate_tee_session(hwkey_ca_ctx_t *ctx)
{
	TEEC_CloseSession(&ctx->sess);
	TEEC_FinalizeContext(&ctx->ctx);

	if (ctx->infptr != NULL)
		fclose(ctx->infptr);
	if (ctx->outfptr != NULL)
		fclose(ctx->outfptr);
	if (ctx->argus->in_file)
		free(ctx->argus->in_file);
	if (ctx->argus->out_file)
		free(ctx->argus->out_file);
}

static void fail_handler(int i)
{
	terminate_tee_session(&ca_sess);

	exit(i);
}

static void crypto_srv_handler(hwkey_ca_ctx_t *ctx, bool enc, uint8_t *in_data,
			       int data_len, FILE *outfptr, uint8_t *out_data)
{
	TEEC_Operation op;
	TEEC_Result rc;
	uint32_t origin, cmd_id;

	memset(&op, 0, sizeof(op));
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
					 TEEC_MEMREF_TEMP_INPUT,
					 TEEC_MEMREF_TEMP_OUTPUT,
					 TEEC_NONE);
	op.params[0].tmpref.buffer = &iv;
	op.params[0].tmpref.size = AES_BLOCK_SIZE;
	op.params[1].tmpref.buffer = in_data;
	op.params[1].tmpref.size = data_len;
	op.params[2].tmpref.buffer = out_data;
	op.params[2].tmpref.size = CRYPTO_PAYLOAD_SIZE;

	if (enc)
		cmd_id = HWKEY_AGENT_TA_CMD_ENCRYPTION;
	else
		cmd_id = HWKEY_AGENT_TA_CMD_DECRYPTION;

	/* Send command to TA. */
	rc = TEEC_InvokeCommand(&ctx->sess, cmd_id, &op, &origin);
	if (rc != TEEC_SUCCESS) {
		printf("TEEC_InvokeCommand failed 0x%x origin 0x%x\n", rc,
		       origin);
		fail_handler(1);
	}

	/* Write output buffer to file. */
	fwrite(out_data, op.params[2].tmpref.size, 1, outfptr);

	/* Update IV for the next block. */
	if (enc)
		memcpy(iv, out_data, AES_BLOCK_SIZE);
	else
		memcpy(iv, in_data, AES_BLOCK_SIZE);
}

static void handle_file_encryption(hwkey_ca_ctx_t *ctx)
{
	uint8_t *input_buf, *output_buf;

	/* Create and open the input/output files */
	ctx->infptr = fopen(ctx->argus->in_file, "rb");
	if (ctx->infptr == NULL) {
		printf("Fail to open the input file: %s\n",
		       ctx->argus->in_file);
		fail_handler(1);
	}

	ctx->outfptr = fopen(ctx->argus->out_file, "wb");
	if (ctx->outfptr == NULL) {
		printf("Fail to open the output file: %s\n",
		       ctx->argus->out_file);
		fail_handler(1);
	}

	input_buf = calloc(1, CRYPTO_PAYLOAD_SIZE);
	output_buf = calloc(1, CRYPTO_PAYLOAD_SIZE);
	if ((input_buf == NULL) || (output_buf == NULL)) {
		printf("Fail to allocate buffers.\n");
		fail_handler(1);
	}

	/* Copy the IV for the first block. */
	memcpy(iv, default_iv, AES_BLOCK_SIZE);

	do {
		int f_rc;

		f_rc = fread(input_buf, 1, CRYPTO_PAYLOAD_SIZE, ctx->infptr);
		if (f_rc < 0) {
			printf("Unexpected failure when reading input file.\n");
			fail_handler(1);
		}

		crypto_srv_handler(ctx, ctx->argus->encryption, input_buf, f_rc,
				   ctx->outfptr, output_buf);

		if (f_rc < CRYPTO_PAYLOAD_SIZE)
			break;
	} while(true);

	free(input_buf);
	free(output_buf);
}

static void dump_random_num(const uint8_t *ptr, size_t len)
{
	const uint8_t *addr = ptr;
	size_t count;
	size_t i;

	for (count = 0 ; count < len; count += 16) {
		for (i = 0; i < MIN(len - count, 16); i++)
			fprintf(stderr, "%02hhx", *(addr + i));
		fprintf(stderr, "\n");
		addr += 16;
	}
}

static void handle_get_random(hwkey_ca_ctx_t *ctx)
{
	TEEC_Operation op;
	TEEC_Result rc;
	uint8_t *rng_buff;
	uint32_t origin;

	if (ctx->argus->rng_size == 0)
		return;

	if (ctx->argus->rng_size > RNG_SRV_DATA_SIZE) {
		printf("%s: the maximum random number lenght is %d.\n",
		       __func__, RNG_SRV_DATA_SIZE);
		return;
	}

	rng_buff = calloc(1, ctx->argus->rng_size);
	if (rng_buff == NULL) {
		printf("%s: calloc fail.\n", __func__);
		return;
	}

	memset(&op, 0, sizeof(op));
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT,
					 TEEC_NONE,
					 TEEC_NONE,
					 TEEC_NONE);
	op.params[0].tmpref.buffer = rng_buff;
	op.params[0].tmpref.size = ctx->argus->rng_size;

	/* Send command to TA. */
	rc = TEEC_InvokeCommand(&ctx->sess, HWKEY_AGENT_TA_CMD_GET_RANDOM,
				&op, &origin);
	if (rc != TEEC_SUCCESS) {
		printf("TEEC_InvokeCommand failed 0x%x origin 0x%x\n", rc,
		       origin);
		goto err_get_random;
	}

	if (op.params[0].tmpref.size > 0)
		dump_random_num(rng_buff, op.params[0].tmpref.size);
	else
		printf("%s: get_random fail.\n", __func__);

err_get_random:
	free(rng_buff);
}

int main(int argc, char *argv[])
{
	struct arguments argus;

	/* Initialize the arguments */
	argus.in_file = NULL;
	argus.out_file = NULL;
	argus.encryption = false;
	argus.get_random = false;

	/* Handle the break signal */
	signal(SIGINT, fail_handler);

	/* Handle the input parameters */
	argp_parse(&argp, argc, argv, 0, 0, &argus);
	ca_sess.argus = &argus;

	if(prepare_tee_session(&ca_sess))
		goto err_out;

	if (argus.in_file)
		handle_file_encryption(&ca_sess);

	if (argus.get_random)
		handle_get_random(&ca_sess);

err_out:
	terminate_tee_session(&ca_sess);

	return 0;
}
