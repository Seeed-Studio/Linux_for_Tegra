// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES.
 */

#include <stdio.h>
#include <string.h>
#include "utee_defines.h"
#include "crypto/crypto.h"
#include "drivers/tegra/tegra_se_keyslot.h"
#include "jetson_decrypt_cpubl_payload.h"

static TEE_Result get_one_and_zeros_padding(uint8_t *input,
					    uint32_t input_len,
					    size_t *data_len)
{
	uint32_t i;
	unsigned char done = 0, prev_done, bad;

	if (NULL == input || NULL == data_len)
		return TEE_ERROR_BAD_PARAMETERS;

	if ((input_len % TEGRA_SE_AES_BLOCK_SIZE) != 0)
		return TEE_ERROR_BAD_PARAMETERS;

	bad = 0x80;
	*data_len = 0;
	for (i = input_len; i > 0; i--) {
		prev_done = done;
		done |= (input[i - 1] != 0);
		*data_len |= (i - 1) * (done != prev_done);
		bad ^= input[i - 1] * (done != prev_done);
	}

	if (bad != 0)
		*data_len = input_len;

	return TEE_SUCCESS;
}

TEE_Result jetson_decrypt_cpubl_payload_process_params(struct dec_ctx *dec_ctx,
						       uint8_t *key,
						       uint32_t key_len,
						       void *img,
						       size_t *img_size)
{
	TEE_Result res = TEE_SUCCESS;
	uint8_t *bch;

	if (!dec_ctx)
		return TEE_ERROR_BAD_PARAMETERS;

	if (!key || (key_len == 0))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!img || !img_size)
		return TEE_ERROR_BAD_PARAMETERS;

	if (*img_size < T194_BCH_HEADER_SIZE)
		return TEE_ERROR_BAD_PARAMETERS;

	bch = img;

	if (memcmp(bch + BCH_HEADER_MAGIC_OFFSET, BCH_HEADER_MAGIC,
		   BCH_HEADER_MAGIC_SIZE))
		return TEE_ERROR_BAD_PARAMETERS;

	memcpy(&(dec_ctx->payload_size),
	       bch + T194_BCH_HEADER_BINARY_LEN_OFFSET,
	       T194_BCH_HEADER_BINARY_LEN_SIZE);
	memset(dec_ctx->iv_in_hdr, 0, T194_BCH_HEADER_IV_SIZE);
	memcpy(dec_ctx->hash_in_hdr, bch + T194_BCH_HEADER_DIGEST_OFFSET,
	       T194_BCH_HEADER_DIGEST_LEN);
	dec_ctx->key_len = key_len;
	memcpy(dec_ctx->key, key, key_len);

	return res;
}

TEE_Result jetson_decrypt_cpubl_payload_init(struct dec_ctx *dec_ctx)
{
	TEE_Result res = TEE_ERROR_GENERIC;

	if (!dec_ctx)
		return TEE_ERROR_BAD_PARAMETERS;

	res = crypto_hash_alloc_ctx(&(dec_ctx->hash_ctx), TEE_ALG_SHA256);
	if (res != TEE_SUCCESS) {
		EMSG("crypto_hash_alloc_ctx failed with res(%x)", res);
		goto out;
	}

	res = crypto_hash_init(dec_ctx->hash_ctx);
	if (res != TEE_SUCCESS) {
		EMSG("crypto_hash_init failed with res(%x)", res);
		goto out;
	}

	res = crypto_cipher_alloc_ctx(&(dec_ctx->ctx), TEE_ALG_AES_CBC_NOPAD);
	if (res != TEE_SUCCESS) {
		EMSG("crypto_cipher_alloc_ctx failed with res(%x)", res);
		goto out;
	}

	res = crypto_cipher_init(dec_ctx->ctx, TEE_MODE_DECRYPT,
				 dec_ctx->key, dec_ctx->key_len,
				 NULL, 0,
				 dec_ctx->iv_in_hdr, T194_BCH_HEADER_IV_SIZE);
	if (res != TEE_SUCCESS)
		EMSG("crypto_cipher_init failed with res(%x)", res);

out:
	if (res != TEE_SUCCESS) {
		if (dec_ctx->ctx)
			crypto_cipher_free_ctx(dec_ctx->ctx);

		if (dec_ctx->hash_ctx)
			crypto_hash_free_ctx(dec_ctx->hash_ctx);
	}

	return res;
}

TEE_Result jetson_decrypt_cpubl_payload_update(struct dec_ctx *dec_ctx,
					       const uint8_t *src_data,
					       uint32_t src_len,
					       uint8_t *dst_data,
					       size_t *dst_len)
{
	TEE_Result res = TEE_ERROR_GENERIC;

	if (!dec_ctx)
		return TEE_ERROR_BAD_PARAMETERS;

	if (!src_data || src_len <= 0)
		return TEE_ERROR_BAD_PARAMETERS;

	if (!dst_data || !dst_len)
		return TEE_ERROR_BAD_PARAMETERS;

	res = crypto_hash_update(dec_ctx->hash_ctx, src_data, src_len);
	if (res != TEE_SUCCESS) {
		goto out;
		EMSG("crypto_hash_update failed with res(%x)", res);
	}

	res = crypto_cipher_update(dec_ctx->ctx, TEE_MODE_DECRYPT,
				   false, src_data,
				   src_len, dst_data);
	if (res != TEE_SUCCESS)
		EMSG("crypto_cipher_update failed with res(%x)", res);

out:
	if (res != TEE_SUCCESS) {
		if (dec_ctx->ctx)
			crypto_cipher_free_ctx(dec_ctx->ctx);

		if (dec_ctx->hash_ctx)
			crypto_hash_free_ctx(dec_ctx->hash_ctx);
	}

	return res;
}

TEE_Result jetson_decrypt_cpubl_payload_final(struct dec_ctx *dec_ctx,
					      const uint8_t *src_data,
					      uint32_t src_len,
					      uint8_t *dst_data,
					      size_t *dst_len)
{
	TEE_Result res = TEE_ERROR_GENERIC;
	size_t final_len;
	uint8_t digest[TEE_SHA256_HASH_SIZE] = {0};

	if (!dec_ctx)
		return TEE_ERROR_BAD_PARAMETERS;

	if (!src_data || src_len <= 0)
		return TEE_ERROR_BAD_PARAMETERS;

	if (!dst_data || !dst_len)
		return TEE_ERROR_BAD_PARAMETERS;

	final_len = *dst_len;

	res = crypto_hash_update(dec_ctx->hash_ctx, src_data, src_len);
	if (res != TEE_SUCCESS) {
		EMSG("crypto_hash_update failed with res(%x)", res);
		goto out;
	}

	res = crypto_cipher_update(dec_ctx->ctx, TEE_MODE_DECRYPT,
				   false, src_data,
				   src_len, dst_data);
	if (res != TEE_SUCCESS) {
		EMSG("crypto_cipher_update failed with res(%x)", res);
		goto out;
	}

	res = crypto_hash_final(dec_ctx->hash_ctx, digest,
				dec_ctx->payload_size);
	if (res != TEE_SUCCESS) {
		EMSG("crypto_hash_final failed with res(%x)", res);
		goto out;
	}

	crypto_cipher_final(dec_ctx->ctx);

	if (memcmp(dec_ctx->hash_in_hdr, digest, T194_BCH_HEADER_DIGEST_LEN)) {
		res = TEE_ERROR_SECURITY;
		goto out;
	}

	res = get_one_and_zeros_padding(dst_data, final_len, dst_len);
	if (res != TEE_SUCCESS)
		EMSG("unpadding failed with res(%x)", res);

out:
	if (dec_ctx->ctx)
		crypto_cipher_free_ctx(dec_ctx->ctx);

	if (dec_ctx->hash_ctx)
		crypto_hash_free_ctx(dec_ctx->hash_ctx);

	return res;
}
