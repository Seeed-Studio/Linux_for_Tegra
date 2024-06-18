// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 *
 * Tegra NVVSE crypto device for crypto operation to NVVSE linux library.
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <linux/uaccess.h>
#include <linux/nospec.h>
#include <linux/mutex.h>
#include <linux/version.h>
#include <linux/string.h>
#include <linux/platform/tegra/common.h>
#include <soc/tegra/fuse.h>
#include <crypto/rng.h>
#include <crypto/hash.h>
#include <crypto/akcipher.h>
#include <crypto/aead.h>
#include <crypto/internal/skcipher.h>
#include <crypto/internal/rng.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/aead.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <crypto/sha3.h>
#include <uapi/misc/tegra-nvvse-cryptodev.h>
#include <asm/barrier.h>

#include "tegra-hv-vse.h"

#define AES_IV_SIZE			16
#define CRYPTO_KEY_LEN_MASK		0x3FF
#define TEGRA_CRYPTO_KEY_512_SIZE	64
#define TEGRA_CRYPTO_KEY_256_SIZE	32
#define TEGRA_CRYPTO_KEY_192_SIZE	24
#define TEGRA_CRYPTO_KEY_128_SIZE	16
#define AES_KEYSLOT_NAME_SIZE		32

#define NVVSE_CHUNK_SIZE               (1024*1024) /* 1MB */

/** Defines the Maximum Random Number length supported */
#define NVVSE_MAX_RANDOM_NUMBER_LEN_SUPPORTED		512U

#define INT32_BYTES 4U
#define CTR_TO_INT32 4U

/**
 * Define preallocated SHA result buffer size, if digest size is bigger
 * than this then allocate new buffer
 */
#define NVVSE_MAX_ALLOCATED_SHA_RESULT_BUFF_SIZE	256U

#define MISC_DEVICE_NAME_LEN		32U

struct nvvse_devnode {
	struct miscdevice *g_misc_devices;
	bool sha_init_done;
} nvvse_devnode[MAX_NUMBER_MISC_DEVICES];

static struct tegra_nvvse_get_ivc_db ivc_database;

/* SHA Algorithm Names */
static const char *sha_alg_names[] = {
	"sha256-vse",
	"sha384-vse",
	"sha512-vse",
	"sha3-256-vse",
	"sha3-384-vse",
	"sha3-512-vse",
	"shake128-vse",
	"shake256-vse",
};

struct tnvvse_crypto_completion {
	struct completion restart;
	int req_err;
};

typedef enum {
	SHA_OP_INIT = 1,
	SHA_OP_SUCCESS = 2,
	SHA_OP_FAIL = 3,
} sha_op_state;

struct crypto_sha_state {
	uint32_t			sha_type;
	uint32_t			digest_size;
	uint64_t			total_bytes;
	uint64_t			remaining_bytes;
	uint8_t				*in_buf;
	struct tnvvse_crypto_completion	sha_complete;
	struct ahash_request		*req;
	struct crypto_ahash		*tfm;
	char				*result_buff;
	sha_op_state			sha_done_success;
};

/* Tegra NVVSE crypt context */
struct tnvvse_crypto_ctx {
	struct mutex			lock;
	struct crypto_sha_state		sha_state;
	uint8_t				intermediate_counter[TEGRA_NVVSE_AES_IV_LEN];
	char				*rng_buff;
	uint32_t			max_rng_buff;
	char				*sha_result;
	uint32_t			node_id;
};

enum tnvvse_gmac_request_type {
	GMAC_INIT = 0u,
	GMAC_SIGN,
	GMAC_VERIFY
};

/* GMAC request data */
struct tnvvse_gmac_req_data {
	enum tnvvse_gmac_request_type request_type;
	/* Return IV after GMAC_INIT and Pass IV during GMAC_VERIFY */
	char *iv;
	uint8_t is_first;
	/* For GMAC_VERIFY tag comparison result */
	uint8_t result;
};

enum tnvvse_cmac_request_type {
	CMAC_SIGN,
	CMAC_VERIFY
};

/* CMAC request data */
struct tnvvse_cmac_req_data {
	enum tnvvse_cmac_request_type request_type;
	/* For CMAC_VERIFY tag comparison result */
	uint8_t result;
};

#if (KERNEL_VERSION(6, 3, 0) <= LINUX_VERSION_CODE)
static void tnvvse_crypto_complete(void *data, int err)
{
	struct tnvvse_crypto_completion *done = data;
#else
static void tnvvse_crypto_complete(struct crypto_async_request *req, int err)
{
	struct tnvvse_crypto_completion *done = req->data;
#endif

	if (err != -EINPROGRESS) {
		done->req_err = err;
		complete(&done->restart);
	}
}

static int wait_async_op(struct tnvvse_crypto_completion *tr, int ret)
{
	if (ret == -EINPROGRESS || ret == -EBUSY) {
		wait_for_completion(&tr->restart);
		reinit_completion(&tr->restart);
		ret = tr->req_err;
	}

	return ret;
}

static int update_counter(uint8_t *pctr_be, uint32_t size)
{
	int status;
	uint32_t index;
	int32_t  count;
	uint64_t ctr_le[CTR_TO_INT32] = {0};
	uint64_t result_le[CTR_TO_INT32];
	uint64_t increment;

	for (index = 0U; index < TEGRA_NVVSE_AES_CTR_LEN; index++) {
		ctr_le[index / INT32_BYTES] |= (((uint64_t)(pctr_be[index]))
			<< (8U * (INT32_BYTES - (index % INT32_BYTES) - 1U)));
	}

	increment = size;
	/* As the constant CTR_TO_INT32 - 1U is converted, overflow is not possible */
	for (count = (int32_t)(CTR_TO_INT32 - 1U); count >= 0; count--) {
		result_le[count] = ctr_le[count] + increment;
		increment = result_le[count] >> 32U;
		result_le[count] = result_le[count] & 0xFFFFFFFFU;
	}

	if (increment != 0U) {
		pr_err("%s():AES-CTR Counter overflowed", __func__);
		status = 60;	//NVVSE_STATUS_INVALID_PARAMETER;
		goto fail;
	}

	for (index = 0U; index < TEGRA_NVVSE_AES_CTR_LEN; index++) {
		pctr_be[index] =
		    (uint8_t)((result_le[index / INT32_BYTES] >>
			(8U * (INT32_BYTES - (index % INT32_BYTES) - 1U))) & 0xFFU);
	}

	status = 0;	// NVVSE_STATUS_OK;

fail:
	return status;
}

static int tnvvse_crypto_sha_init(struct tnvvse_crypto_ctx *ctx,
		struct tegra_nvvse_sha_init_ctl *init_ctl)
{
	struct crypto_sha_state *sha_state = &ctx->sha_state;
	struct tegra_virtual_se_sha_context *sha_ctx;
	struct crypto_ahash *tfm;
	struct ahash_request *req;
	const char *driver_name;
	int ret = -ENOMEM;
	char *result_buff = NULL;

	if (nvvse_devnode[ctx->node_id].sha_init_done) {
		pr_err("%s: Sha init already done for this node_id %u\n", __func__, ctx->node_id);
		return -EAGAIN;
	}

	if (init_ctl->sha_type < TEGRA_NVVSE_SHA_TYPE_SHA256  ||
			init_ctl->sha_type >= TEGRA_NVVSE_SHA_TYPE_MAX) {
		pr_err("%s(): SHA Type requested %d is not supported\n",
					__func__, init_ctl->sha_type);
		return -EINVAL;
	}

	tfm = crypto_alloc_ahash(sha_alg_names[init_ctl->sha_type], 0, 0);
	if (IS_ERR(tfm)) {
		pr_err("%s(): Failed to load transform for %s:%ld\n",
					__func__, sha_alg_names[init_ctl->sha_type], PTR_ERR(tfm));
		ret = PTR_ERR(tfm);
		goto out;
	}

	sha_ctx = crypto_ahash_ctx(tfm);
	sha_ctx->node_id = ctx->node_id;

	driver_name = crypto_tfm_alg_driver_name(crypto_ahash_tfm(tfm));;
	if (driver_name == NULL) {
		pr_err("%s(): Failed to get driver name\n", __func__);
		goto free_tfm;
	}
	pr_debug("%s(): Algo name %s, driver name %s\n",
					__func__, sha_alg_names[init_ctl->sha_type], driver_name);

	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("%s(): Failed to allocate request for %s\n",
					__func__, sha_alg_names[init_ctl->sha_type]);
		goto free_tfm;
	}

	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   tnvvse_crypto_complete, &sha_state->sha_complete);

	init_completion(&sha_state->sha_complete.restart);
	sha_state->sha_complete.req_err = 0;

	/* Shake128/Shake256 have variable digest size */
	if ((init_ctl->sha_type == TEGRA_NVVSE_SHA_TYPE_SHAKE128) ||
	     (init_ctl->sha_type == TEGRA_NVVSE_SHA_TYPE_SHAKE256)) {
		sha_ctx->digest_size = init_ctl->digest_size;
		if (init_ctl->digest_size > NVVSE_MAX_ALLOCATED_SHA_RESULT_BUFF_SIZE) {
			result_buff = kzalloc(init_ctl->digest_size, GFP_KERNEL);
			if (!result_buff) {
				ret = -ENOMEM;
				goto free_req;
			}
		}
	}

	ret = wait_async_op(&sha_state->sha_complete, crypto_ahash_init(req));
	if (ret) {
		pr_err("%s(): Failed to ahash_init for %s: ret=%d\n",
					__func__, sha_alg_names[init_ctl->sha_type], ret);
		goto free_result_buf;
	}

	sha_state->req = req;
	sha_state->tfm = tfm;
	sha_state->result_buff = (result_buff) ? result_buff : ctx->sha_result;
	sha_state->sha_type = init_ctl->sha_type;
	sha_state->total_bytes = init_ctl->total_msg_size;
	sha_state->digest_size = init_ctl->digest_size;
	sha_state->remaining_bytes = init_ctl->total_msg_size;
	sha_state->sha_done_success = SHA_OP_INIT;
	nvvse_devnode[ctx->node_id].sha_init_done = true;

	memset(sha_state->result_buff , 0, 64);

	ret = 0;
	goto out;

free_result_buf:
	kfree(result_buff);
free_req:
	ahash_request_free(req);
free_tfm:
	crypto_free_ahash(tfm);
out:
	return ret;
}

static int tnvvse_crypto_sha_update(struct tnvvse_crypto_ctx *ctx,
		struct tegra_nvvse_sha_update_ctl *update_ctl)
{
	struct crypto_sha_state *sha_state = &ctx->sha_state;
	char *result_buff;
	struct ahash_request *req;
	char *input_buffer = update_ctl->in_buff;
	struct scatterlist sg;
	int ret = 0, buffer_size;

	if (update_ctl->input_buffer_size < 0) {
		pr_err("%s: Invalid Msg size of %d Bytes\n", __func__, update_ctl->input_buffer_size);
		ret = -EINVAL;
		goto stop_sha;
	}

	if (update_ctl->input_buffer_size > ivc_database.max_buffer_size[ctx->node_id]) {
		pr_err("%s: Msg size is greater than supported size of %d Bytes\n", __func__,
						ivc_database.max_buffer_size[ctx->node_id]);
		ret = -EINVAL;
		goto stop_sha;
	}

	result_buff = sha_state->result_buff;
	req = sha_state->req;

	/* allocate buffer size as 1 to perform SHA operation
	 * if SHA buffer size passed is zero
	 */
	if (update_ctl->input_buffer_size == 0)
		buffer_size = 1;
	else
		buffer_size = update_ctl->input_buffer_size;

	sha_state->in_buf = krealloc(sha_state->in_buf, buffer_size, GFP_KERNEL);
	if (sha_state->in_buf == NULL) {
		ret = -ENOMEM;
		goto stop_sha;
	}
	memset(sha_state->in_buf, 0, buffer_size);

	/* copy input buffer */
	if (copy_from_user((void *)sha_state->in_buf, input_buffer, update_ctl->input_buffer_size)) {
		pr_err("%s(): Failed to copy_from_user input data\n", __func__);
		ret = -EFAULT;
		goto stop_sha;
	}

	sg_init_one(&sg, sha_state->in_buf, update_ctl->input_buffer_size);
	ahash_request_set_crypt(req, &sg, result_buff, update_ctl->input_buffer_size);
	ret = wait_async_op(&sha_state->sha_complete, crypto_ahash_update(req));
	if (ret) {
		pr_err("%s(): Failed to ahash_update for %s: %d\n",
				__func__, sha_alg_names[sha_state->sha_type], ret);
		goto stop_sha;
	}

	if (update_ctl->last_buffer) {
		ret = wait_async_op(&sha_state->sha_complete, crypto_ahash_final(req));
		if (ret) {
			pr_err("%s(): Failed to ahash_final for %s: %d\n",
					__func__, sha_alg_names[sha_state->sha_type], ret);
			goto stop_sha;
		}
		sha_state->sha_done_success = SHA_OP_SUCCESS;
	}

	goto done;

stop_sha:
	sha_state->sha_done_success = SHA_OP_FAIL;

done:
	return ret;
}

static int tnvvse_crypto_sha_final(struct tnvvse_crypto_ctx *ctx,
		struct tegra_nvvse_sha_final_ctl *final_ctl)
{
	struct crypto_sha_state *sha_state = &ctx->sha_state;
	struct crypto_ahash *tfm = sha_state->tfm;
	struct scatterlist sg;
	struct ahash_request *req;
	unsigned long size = 0;
	char *result_buff;
	int ret = -ENOMEM;

	if (sha_state->sha_done_success == SHA_OP_INIT) {
		result_buff = sha_state->result_buff;
		req = sha_state->req;

		sg_init_one(&sg, sha_state->in_buf, size);
		ahash_request_set_crypt(req, &sg, result_buff, size);

		ret = wait_async_op(&sha_state->sha_complete, crypto_ahash_final(req));
		if (ret) {
			pr_err("%s(): Failed to ahash_final for %s: %d\n",
					__func__, sha_alg_names[sha_state->sha_type], ret);
		}

		pr_err("%s(): SHA is not completed successfully\n", __func__);
		ret = -EFAULT;
		goto stop_sha;
	}

	if (sha_state->sha_done_success == SHA_OP_FAIL) {
		pr_err("%s(): SHA is either aborted or not initialized\n", __func__);
		ret = -EFAULT;
		goto stop_sha;
	}

	/* Shake128/Shake256 have variable digest size */
	if ((sha_state->sha_type == TEGRA_NVVSE_SHA_TYPE_SHAKE128) ||
	    (sha_state->sha_type == TEGRA_NVVSE_SHA_TYPE_SHAKE256)) {
		ret = copy_to_user((void __user *)final_ctl->digest_buffer,
				(const void *)sha_state->result_buff,
				final_ctl->digest_size);
	} else {
		if (final_ctl->digest_size != crypto_ahash_digestsize(tfm)) {
			pr_err("%s(): digest size not matching req %d and calculated %d for %s\n",
				__func__, final_ctl->digest_size, crypto_ahash_digestsize(tfm),
				sha_alg_names[sha_state->sha_type]);
			ret = -EINVAL;
			goto stop_sha;
		}

		ret = copy_to_user((void __user *)final_ctl->digest_buffer,
				(const void *)sha_state->result_buff,
				crypto_ahash_digestsize(tfm));
	}
	if (ret) {
		pr_err("%s(): Failed to copy_to_user for %s: %d\n",
					__func__, sha_alg_names[sha_state->sha_type], ret);
	}

stop_sha:
	if (sha_state->in_buf) {
		kfree(sha_state->in_buf);
		sha_state->in_buf = NULL;
	}
	ahash_request_free(sha_state->req);
	crypto_free_ahash(sha_state->tfm);

	sha_state->req = NULL;
	sha_state->tfm = NULL;
	if (sha_state->result_buff != ctx->sha_result) {
		kfree(sha_state->result_buff);
		sha_state->result_buff = NULL;
	}
	sha_state->total_bytes = 0;
	sha_state->digest_size = 0;
	sha_state->remaining_bytes = 0;
	nvvse_devnode[ctx->node_id].sha_init_done = false;

	return ret;
}

static int tnvvse_crypto_tsec_get_keyload_status(struct tnvvse_crypto_ctx *ctx,
				struct tegra_nvvse_tsec_get_keyload_status *tsec_keyload_status)
{
	return tegra_hv_vse_safety_tsec_get_keyload_status(ctx->node_id,
			&tsec_keyload_status->err_code);
}

static int tnvvtsec_crypto_aes_cmac_sign_verify(struct tnvvse_crypto_ctx *ctx,
				struct tegra_nvvse_aes_cmac_sign_verify_ctl *aes_cmac_ctl)
{
	struct crypto_ahash *tfm;
	char *result, *src_buffer;
	const char *driver_name;
	struct ahash_request *req;
	struct tnvvse_crypto_completion sha_complete;
	struct tegra_virtual_se_aes_cmac_context *cmac_ctx;
	char key_as_keyslot[AES_KEYSLOT_NAME_SIZE] = {0,};
	struct tnvvse_cmac_req_data priv_data;
	int ret = -ENOMEM;
	struct scatterlist sg;
	uint32_t total = 0;
	uint8_t *hash_buff;

	if (aes_cmac_ctl->data_length > ivc_database.max_buffer_size[ctx->node_id]) {
		pr_err("%s(): Input size is (data = %d) is not supported\n",
					__func__, aes_cmac_ctl->data_length);
		return -EINVAL;
	}

	result = kzalloc(64, GFP_KERNEL);
	if (!result)
		return -ENOMEM;

	tfm = crypto_alloc_ahash("cmac-tsec(aes)", 0, 0);
	if (IS_ERR(tfm)) {
		ret = PTR_ERR(tfm);
		pr_err("%s(): Failed to allocate ahash for cmac-tsec(aes): %d\n", __func__, ret);
		goto free_result;
	}

	cmac_ctx = crypto_ahash_ctx(tfm);
	cmac_ctx->node_id = ctx->node_id;

	driver_name = crypto_tfm_alg_driver_name(crypto_ahash_tfm(tfm));
	if (driver_name == NULL) {
		pr_err("%s(): get_driver_name for cmac-tsec(aes) returned NULL", __func__);
		ret = -EINVAL;
		goto free_tfm;
	}

	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("%s(): Failed to allocate request for cmac-tsec(aes)\n", __func__);
		goto free_tfm;
	}

	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   tnvvse_crypto_complete, &sha_complete);

	init_completion(&sha_complete.restart);
	sha_complete.req_err = 0;

	crypto_ahash_clear_flags(tfm, ~0U);

	if (aes_cmac_ctl->cmac_type == TEGRA_NVVSE_AES_CMAC_SIGN)
		priv_data.request_type = CMAC_SIGN;
	else
		priv_data.request_type = CMAC_VERIFY;

	ret = snprintf(key_as_keyslot, AES_KEYSLOT_NAME_SIZE, "NVSEAES ");
	memcpy(key_as_keyslot + KEYSLOT_OFFSET_BYTES, aes_cmac_ctl->key_slot, KEYSLOT_SIZE_BYTES);

	req->priv = &priv_data;
	priv_data.result = 0;
	ret = crypto_ahash_setkey(tfm, key_as_keyslot, aes_cmac_ctl->key_length);
	if (ret) {
		pr_err("%s(): Failed to set keys for cmac-tsec(aes): %d\n", __func__, ret);
		ret = -EINVAL;
		goto free_req;
	}

	ret = wait_async_op(&sha_complete, crypto_ahash_init(req));
	if (ret) {
		pr_err("%s(): Failed to initialize ahash: %d\n", __func__, ret);
		ret = -EINVAL;
		goto free_req;
	}

	if (aes_cmac_ctl->cmac_type == TEGRA_NVVSE_AES_CMAC_VERIFY) {
		/* Copy digest */
		ret = copy_from_user((void *)result, (void __user *)aes_cmac_ctl->cmac_buffer,
						TEGRA_NVVSE_AES_CMAC_LEN);
		if (ret) {
			pr_err("%s(): Failed to copy_from_user: %d\n", __func__, ret);
			ret = -EINVAL;
			goto free_req;
		}
	}

	total = aes_cmac_ctl->data_length;
	src_buffer = aes_cmac_ctl->src_buffer;

	if (total > ivc_database.max_buffer_size[ctx->node_id]) {
		pr_err("%s(): Unsupported buffer size: %u\n", __func__, total);
		ret = -EINVAL;
		goto free_req;
	}

	hash_buff = kcalloc(total, sizeof(uint8_t), GFP_KERNEL);
	if (hash_buff == NULL) {
		ret = -ENOMEM;
		goto free_req;
	}

	ret = copy_from_user((void *)hash_buff, (void __user *)src_buffer, total);
	if (ret) {
		pr_err("%s(): Failed to copy_from_user: %d\n", __func__, ret);
		goto free_xbuf;
	}

	sg_init_one(&sg, hash_buff, total);
	ahash_request_set_crypt(req, &sg, result, total);

	ret = wait_async_op(&sha_complete, crypto_ahash_finup(req));
	if (ret) {
		pr_err("%s(): Failed to ahash_finup: %d\n", __func__, ret);
		goto free_xbuf;
	}

	if (aes_cmac_ctl->cmac_type == TEGRA_NVVSE_AES_CMAC_SIGN) {
		ret = copy_to_user((void __user *)aes_cmac_ctl->cmac_buffer, (const void *)result,
								crypto_ahash_digestsize(tfm));
		if (ret)
			pr_err("%s(): Failed to copy_to_user: %d\n", __func__, ret);
	} else {
		aes_cmac_ctl->result = priv_data.result;
	}

free_xbuf:
	kfree(hash_buff);
free_req:
	ahash_request_free(req);
free_tfm:
	crypto_free_ahash(tfm);
free_result:
	kfree(result);

	return ret;
}

static int tnvvse_crypto_aes_cmac_sign_verify(struct tnvvse_crypto_ctx *ctx,
				struct tegra_nvvse_aes_cmac_sign_verify_ctl *aes_cmac_ctl)
{
	struct crypto_ahash *tfm;
	char *result, *src_buffer;
	const char *driver_name;
	struct ahash_request *req;
	struct tnvvse_crypto_completion sha_complete;
	struct tegra_virtual_se_aes_cmac_context *cmac_ctx;
	char key_as_keyslot[AES_KEYSLOT_NAME_SIZE] = {0,};
	struct tnvvse_cmac_req_data priv_data;
	struct scatterlist sg;
	int ret = -ENOMEM;
	uint32_t in_sz;
	uint8_t *in_buf;

	if (aes_cmac_ctl->data_length > ivc_database.max_buffer_size[ctx->node_id]) {
		pr_err("%s(): Input size is (data = %d) is not supported\n",
					__func__, aes_cmac_ctl->data_length);
		return -EINVAL;
	}

	result = kzalloc(64, GFP_KERNEL);
	if (!result)
		return -ENOMEM;

	tfm = crypto_alloc_ahash("cmac-vse(aes)", 0, 0);
	if (IS_ERR(tfm)) {
		ret = PTR_ERR(tfm);
		pr_err("%s(): Failed to allocate ahash for cmac-vse(aes): %d\n", __func__, ret);
		goto free_result;
	}

	cmac_ctx = crypto_ahash_ctx(tfm);
	cmac_ctx->node_id = ctx->node_id;

	driver_name = crypto_tfm_alg_driver_name(crypto_ahash_tfm(tfm));
	if (driver_name == NULL) {
		pr_err("%s(): Failed to get_driver_name for cmac-vse(aes) returned NULL", __func__);
		goto free_tfm;
	}

	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("%s(): Failed to allocate request for cmac-vse(aes)\n", __func__);
		goto free_tfm;
	}

	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   tnvvse_crypto_complete, &sha_complete);

	in_sz = aes_cmac_ctl->data_length;
	in_buf = kzalloc(in_sz, GFP_KERNEL);
	if (in_buf == NULL) {
		ret = -ENOMEM;
		goto free_req;
	}

	init_completion(&sha_complete.restart);
	sha_complete.req_err = 0;

	crypto_ahash_clear_flags(tfm, ~0U);

	if (aes_cmac_ctl->cmac_type == TEGRA_NVVSE_AES_CMAC_SIGN)
		priv_data.request_type = CMAC_SIGN;
	else
		priv_data.request_type = CMAC_VERIFY;

	ret = snprintf(key_as_keyslot, AES_KEYSLOT_NAME_SIZE, "NVSEAES ");
	memcpy(key_as_keyslot + KEYSLOT_OFFSET_BYTES, aes_cmac_ctl->key_slot, KEYSLOT_SIZE_BYTES);

	req->priv = &priv_data;
	priv_data.result = 0;
	ret = crypto_ahash_setkey(tfm, key_as_keyslot, aes_cmac_ctl->key_length);
	if (ret) {
		pr_err("%s(): Failed to set keys for cmac-vse(aes): %d\n", __func__, ret);
		goto free_buf;
	}

	ret = wait_async_op(&sha_complete, crypto_ahash_init(req));
	if (ret) {
		pr_err("%s(): Failed to initialize ahash: %d\n", __func__, ret);
		goto free_buf;
	}

	if (aes_cmac_ctl->cmac_type == TEGRA_NVVSE_AES_CMAC_VERIFY) {
		/* Copy digest */
		ret = copy_from_user((void *)result,
				(void __user *)aes_cmac_ctl->cmac_buffer,
						TEGRA_NVVSE_AES_CMAC_LEN);
		if (ret) {
			pr_err("%s(): Failed to copy_from_user: %d\n", __func__, ret);
			goto free_buf;
		}
	}

	src_buffer = aes_cmac_ctl->src_buffer;

	/* copy input buffer */
	ret = copy_from_user(in_buf, src_buffer, aes_cmac_ctl->data_length);
	if (ret) {
		pr_err("%s(): Failed to copy user input data: %d\n", __func__, ret);
		goto free_buf;
	}

	sg_init_one(&sg, in_buf, aes_cmac_ctl->data_length);
	ahash_request_set_crypt(req, &sg, result, aes_cmac_ctl->data_length);

	ret = wait_async_op(&sha_complete, crypto_ahash_finup(req));
	if (ret) {
		pr_err("%s(): Failed to ahash_finup: %d\n", __func__, ret);
		goto free_buf;
	}

	if (aes_cmac_ctl->cmac_type == TEGRA_NVVSE_AES_CMAC_SIGN) {
		ret = copy_to_user((void __user *)aes_cmac_ctl->cmac_buffer, (const void *)result,
								crypto_ahash_digestsize(tfm));
		if (ret)
			pr_err("%s(): Failed to copy_to_user: %d\n", __func__, ret);
	} else {
		aes_cmac_ctl->result = priv_data.result;
	}

free_buf:
	kfree(in_buf);
free_req:
	ahash_request_free(req);
free_tfm:
	crypto_free_ahash(tfm);
free_result:
	kfree(result);

	return ret;
}

static int tnvvse_crypto_aes_gmac_init(struct tnvvse_crypto_ctx *ctx,
		struct tegra_nvvse_aes_gmac_init_ctl *gmac_init_ctl)
{
	struct crypto_sha_state *sha_state = &ctx->sha_state;
	struct tegra_virtual_se_aes_gmac_context *gmac_ctx;
	char key_as_keyslot[AES_KEYSLOT_NAME_SIZE] = {0,};
	struct crypto_ahash *tfm;
	struct ahash_request *req;
	const char *driver_name;
	uint8_t iv[TEGRA_NVVSE_AES_GCM_IV_LEN];
	struct tnvvse_gmac_req_data priv_data;
	int ret = -ENOMEM, klen;

	tfm = crypto_alloc_ahash("gmac-vse(aes)", 0, 0);
	if (IS_ERR(tfm)) {
		pr_err("%s(): Failed to allocate transform for gmac-vse(aes):%ld\n", __func__,
						PTR_ERR(tfm));
		ret = PTR_ERR(tfm);
		goto out;
	}

	gmac_ctx = crypto_ahash_ctx(tfm);
	gmac_ctx->node_id = ctx->node_id;

	driver_name = crypto_tfm_alg_driver_name(crypto_ahash_tfm(tfm));
	if (driver_name == NULL) {
		pr_err("%s(): Failed to get driver name\n", __func__);
		goto free_tfm;
	}
	pr_debug("%s(): Algo name gmac-vse(aes), driver name %s\n", __func__, driver_name);

	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("%s(): Failed to allocate request for gmac-vse(aes)\n", __func__);
		goto free_tfm;
	}

	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   tnvvse_crypto_complete, &sha_state->sha_complete);

	init_completion(&sha_state->sha_complete.restart);
	sha_state->sha_complete.req_err = 0;

	ret = snprintf(key_as_keyslot, AES_KEYSLOT_NAME_SIZE, "NVSEAES ");
	memcpy(key_as_keyslot + KEYSLOT_OFFSET_BYTES, gmac_init_ctl->key_slot, KEYSLOT_SIZE_BYTES);

	klen = gmac_init_ctl->key_length;
	ret = crypto_ahash_setkey(tfm, key_as_keyslot, klen);
	if (ret) {
		pr_err("%s(): Failed to set keys for gmac-vse(aes): %d\n", __func__, ret);
		goto free_req;
	}

	memset(iv, 0, TEGRA_NVVSE_AES_GCM_IV_LEN);
	priv_data.request_type = GMAC_INIT;
	priv_data.iv = iv;
	req->priv = &priv_data;

	ret = wait_async_op(&sha_state->sha_complete, crypto_ahash_init(req));
	if (ret) {
		pr_err("%s(): Failed to ahash_init for gmac-vse(aes): ret=%d\n",
					__func__, ret);
	}

	memcpy(gmac_init_ctl->IV, priv_data.iv, TEGRA_NVVSE_AES_GCM_IV_LEN);

free_req:
	ahash_request_free(req);
free_tfm:
	crypto_free_ahash(tfm);
out:
	return ret;
}

static int tnvvse_crypto_aes_gmac_sign_verify_init(struct tnvvse_crypto_ctx *ctx,
		struct tegra_nvvse_aes_gmac_sign_verify_ctl *gmac_sign_verify_ctl)
{
	struct crypto_sha_state *sha_state = &ctx->sha_state;
	struct tegra_virtual_se_aes_gmac_context *gmac_ctx;
	char key_as_keyslot[AES_KEYSLOT_NAME_SIZE] = {0,};
	struct crypto_ahash *tfm;
	struct ahash_request *req;
	struct tnvvse_gmac_req_data priv_data;
	const char *driver_name;
	int ret = -EINVAL, klen;

	tfm = crypto_alloc_ahash("gmac-vse(aes)", 0, 0);
	if (IS_ERR(tfm)) {
		pr_err("%s(): Failed to load transform for gmac-vse(aes):%ld\n", __func__,
							PTR_ERR(tfm));
		ret = PTR_ERR(tfm);
		goto out;
	}

	gmac_ctx = crypto_ahash_ctx(tfm);
	gmac_ctx->node_id = ctx->node_id;

	driver_name = crypto_tfm_alg_driver_name(crypto_ahash_tfm(tfm));
	if (driver_name == NULL) {
		pr_err("%s(): Failed to get driver name\n", __func__);
		goto free_tfm;
	}
	pr_debug("%s(): Algo name gmac-vse(aes), driver name %s\n", __func__, driver_name);

	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("%s(): Failed to allocate request for gmac-vse(aes)\n", __func__);
		goto free_tfm;
	}

	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   tnvvse_crypto_complete, &sha_state->sha_complete);

	sha_state->in_buf = kzalloc(gmac_sign_verify_ctl->data_length, GFP_KERNEL);
	if (sha_state->in_buf == NULL) {
		ret = -ENOMEM;
		goto free_req;
	}

	init_completion(&sha_state->sha_complete.restart);
	sha_state->sha_complete.req_err = 0;

	ret = snprintf(key_as_keyslot, AES_KEYSLOT_NAME_SIZE, "NVSEAES ");
	memcpy(key_as_keyslot + KEYSLOT_OFFSET_BYTES, gmac_sign_verify_ctl->key_slot,
		KEYSLOT_SIZE_BYTES);

	klen = gmac_sign_verify_ctl->key_length;
	ret = crypto_ahash_setkey(tfm, key_as_keyslot, klen);
	if (ret) {
		pr_err("%s(): Failed to set keys for gmac-vse(aes): %d\n", __func__, ret);
		goto free_buf;
	}

	if (gmac_sign_verify_ctl->gmac_type == TEGRA_NVVSE_AES_GMAC_SIGN)
		priv_data.request_type = GMAC_SIGN;
	else
		priv_data.request_type = GMAC_VERIFY;
	req->priv = &priv_data;

	ret = wait_async_op(&sha_state->sha_complete, crypto_ahash_init(req));
	if (ret) {
		pr_err("%s(): Failed to ahash_init for gmac-vse(aes): ret=%d\n",
					__func__, ret);
		goto free_buf;
	}

	sha_state->req = req;
	sha_state->tfm = tfm;
	sha_state->result_buff = ctx->sha_result;

	memset(sha_state->result_buff, 0, TEGRA_NVVSE_AES_GCM_TAG_SIZE);

	ret = 0;
	goto out;

free_buf:
	kfree(sha_state->in_buf);
free_req:
	ahash_request_free(req);
free_tfm:
	crypto_free_ahash(tfm);
out:
	return ret;
}

static int tnvvse_crypto_aes_gmac_sign_verify(struct tnvvse_crypto_ctx *ctx,
		struct tegra_nvvse_aes_gmac_sign_verify_ctl *gmac_sign_verify_ctl)
{
	struct crypto_sha_state *sha_state = &ctx->sha_state;
	char *result_buff;
	uint8_t iv[TEGRA_NVVSE_AES_GCM_IV_LEN];
	struct ahash_request *req;
	char *src_buffer = gmac_sign_verify_ctl->src_buffer;
	struct tnvvse_gmac_req_data priv_data;
	struct scatterlist sg;
	int ret = -EINVAL;

	if (gmac_sign_verify_ctl->data_length > ivc_database.max_buffer_size[ctx->node_id] ||
			gmac_sign_verify_ctl->data_length == 0) {
		pr_err("%s(): Failed due to invalid input size: %d\n", __func__, ret);
		goto done;
	}

	if (gmac_sign_verify_ctl->is_last &&
			gmac_sign_verify_ctl->tag_length != TEGRA_NVVSE_AES_GCM_TAG_SIZE) {
		pr_err("%s(): Failed due to invalid tag length (%d) invalid", __func__,
					gmac_sign_verify_ctl->tag_length);
		goto done;
	}

	ret = tnvvse_crypto_aes_gmac_sign_verify_init(ctx, gmac_sign_verify_ctl);
	if (ret) {
		pr_err("%s(): Failed to init: %d\n", __func__, ret);
		goto done;
	}

	result_buff = sha_state->result_buff;
	req = sha_state->req;

	if (gmac_sign_verify_ctl->gmac_type == TEGRA_NVVSE_AES_GMAC_SIGN)
		priv_data.request_type = GMAC_SIGN;
	else
		priv_data.request_type = GMAC_VERIFY;
	priv_data.iv = NULL;
	priv_data.is_first = gmac_sign_verify_ctl->is_first;
	req->priv = &priv_data;

	/* copy input buffer */
	ret = copy_from_user(sha_state->in_buf, src_buffer, gmac_sign_verify_ctl->data_length);
	if (ret) {
		pr_err("%s(): Failed to copy user input data: %d\n", __func__, ret);
		goto stop_sha;
	}

	sg_init_one(&sg, sha_state->in_buf, gmac_sign_verify_ctl->data_length);
	ahash_request_set_crypt(req, &sg, result_buff,
			gmac_sign_verify_ctl->data_length);
	if (gmac_sign_verify_ctl->is_last == 0) {
		ret = wait_async_op(&sha_state->sha_complete,
				crypto_ahash_update(req));
		if (ret) {
			pr_err("%s(): Failed to ahash_update for gmac-vse(aes): %d\n",
					__func__, ret);
			goto stop_sha;
		}
	} else {
		if (gmac_sign_verify_ctl->gmac_type ==
				TEGRA_NVVSE_AES_GMAC_VERIFY) {
			/* Copy tag/digest */
			ret = copy_from_user((void *)result_buff,
					(void __user *)gmac_sign_verify_ctl->tag_buffer,
					TEGRA_NVVSE_AES_GCM_TAG_SIZE);
			if (ret) {
				pr_err("%s(): Failed to copy_from_user: %d\n",
						__func__, ret);
				goto stop_sha;
			}

			memcpy(iv, gmac_sign_verify_ctl->initial_vector,
					TEGRA_NVVSE_AES_GCM_IV_LEN);
			priv_data.iv = iv;
		}
		ret = wait_async_op(&sha_state->sha_complete,
				crypto_ahash_finup(req));
		if (ret) {
			pr_err("%s(): Failed to ahash_finup for gmac-vse(aes): %d\n",
					__func__, ret);
			goto stop_sha;
		}
	}

	if (gmac_sign_verify_ctl->is_last) {
		if (gmac_sign_verify_ctl->gmac_type == TEGRA_NVVSE_AES_GMAC_SIGN) {
			ret = copy_to_user((void __user *)gmac_sign_verify_ctl->tag_buffer,
				(const void *)result_buff,
				gmac_sign_verify_ctl->tag_length);
			if (ret)
				pr_err("%s(): Failed to copy_to_user:%d\n", __func__, ret);
		} else {
			gmac_sign_verify_ctl->result = priv_data.result;
		}
	}

stop_sha:
	kfree(sha_state->in_buf);
	if (sha_state->req)
		ahash_request_free(sha_state->req);
	if (sha_state->tfm)
		crypto_free_ahash(sha_state->tfm);

	sha_state->req = NULL;
	sha_state->tfm = NULL;
	sha_state->result_buff = NULL;

done:
	return ret;
}

static int tnvvse_crypto_aes_enc_dec(struct tnvvse_crypto_ctx *ctx,
					struct tegra_nvvse_aes_enc_dec_ctl *aes_enc_dec_ctl)
{
	struct crypto_skcipher *tfm;
	struct skcipher_request *req = NULL;
	struct scatterlist in_sg;
	struct scatterlist out_sg;
	uint8_t *in_buf, *out_buf;
	int ret = 0;
	struct tnvvse_crypto_completion tcrypt_complete;
	struct tegra_virtual_se_aes_context *aes_ctx;
	char aes_algo[5][15] = {"cbc-vse(aes)", "ctr-vse(aes)"};
	const char *driver_name;
	char key_as_keyslot[AES_KEYSLOT_NAME_SIZE] = {0,};
	uint8_t next_block_iv[TEGRA_NVVSE_AES_IV_LEN];
	uint32_t in_sz, out_sz;

	if (aes_enc_dec_ctl->aes_mode >= TEGRA_NVVSE_AES_MODE_MAX) {
		pr_err("%s(): The requested AES ENC/DEC (%d) is not supported\n",
					__func__, aes_enc_dec_ctl->aes_mode);
		ret = -EINVAL;
		goto out;
	}

	if (aes_enc_dec_ctl->data_length > ivc_database.max_buffer_size[ctx->node_id]) {
		pr_err("%s(): Input size is (data = %d) is not supported\n",
					__func__, aes_enc_dec_ctl->data_length);
		ret = -EINVAL;
		goto out;
	}

	tfm = crypto_alloc_skcipher(aes_algo[aes_enc_dec_ctl->aes_mode],
						CRYPTO_ALG_TYPE_SKCIPHER | CRYPTO_ALG_ASYNC, 0);
	if (IS_ERR(tfm)) {
		pr_err("%s(): Failed to load transform for %s: %ld\n",
					__func__, aes_algo[aes_enc_dec_ctl->aes_mode], PTR_ERR(tfm));
		ret = PTR_ERR(tfm);
		goto out;
	}

	aes_ctx = crypto_skcipher_ctx(tfm);
	aes_ctx->node_id = ctx->node_id;
	aes_ctx->user_nonce = aes_enc_dec_ctl->user_nonce;
	if (aes_enc_dec_ctl->is_non_first_call != 0U)
		aes_ctx->b_is_first = 0U;
	else {
		aes_ctx->b_is_first = 1U;
		memset(ctx->intermediate_counter, 0, TEGRA_NVVSE_AES_IV_LEN);
	}

	req = skcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("%s(): Failed to allocate skcipher request\n", __func__);
		ret = -ENOMEM;
		goto free_tfm;
	}

	driver_name = crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm));
	if (driver_name == NULL) {
		pr_err("%s(): Failed to get driver name for %s\n", __func__,
						aes_algo[aes_enc_dec_ctl->aes_mode]);
		goto free_req;
	}
	pr_debug("%s(): The skcipher driver name is %s for %s\n",
				__func__, driver_name, aes_algo[aes_enc_dec_ctl->aes_mode]);

	if (((aes_enc_dec_ctl->key_length & CRYPTO_KEY_LEN_MASK) != TEGRA_CRYPTO_KEY_128_SIZE) &&
		((aes_enc_dec_ctl->key_length & CRYPTO_KEY_LEN_MASK) != TEGRA_CRYPTO_KEY_192_SIZE) &&
		((aes_enc_dec_ctl->key_length & CRYPTO_KEY_LEN_MASK) != TEGRA_CRYPTO_KEY_256_SIZE) &&
		((aes_enc_dec_ctl->key_length & CRYPTO_KEY_LEN_MASK) != TEGRA_CRYPTO_KEY_512_SIZE)) {
		ret = -EINVAL;
		pr_err("%s(): crypt_req keylen(%d) invalid", __func__, aes_enc_dec_ctl->key_length);
		goto free_req;
	}

	crypto_skcipher_clear_flags(tfm, ~0);

	ret = snprintf(key_as_keyslot, AES_KEYSLOT_NAME_SIZE, "NVSEAES ");
	memcpy(key_as_keyslot + KEYSLOT_OFFSET_BYTES, aes_enc_dec_ctl->key_slot,
		KEYSLOT_SIZE_BYTES);

	/* Null key is only allowed in SE driver */
	if (!strstr(driver_name, "tegra")) {
		ret = -EINVAL;
		pr_err("%s(): Failed to identify as tegra se driver\n", __func__);
		goto free_req;
	}

	ret = crypto_skcipher_setkey(tfm, key_as_keyslot, aes_enc_dec_ctl->key_length);
	if (ret < 0) {
		pr_err("%s(): Failed to set key: %d\n", __func__, ret);
		goto free_req;
	}

	in_sz = aes_enc_dec_ctl->data_length;
	out_sz = aes_enc_dec_ctl->data_length;
	in_buf = kzalloc(in_sz, GFP_KERNEL);
	if (in_buf == NULL) {
		ret = -ENOMEM;
		goto free_req;
	}

	out_buf = kzalloc(out_sz, GFP_KERNEL);
	if (out_buf == NULL) {
		ret = -ENOMEM;
		goto free_in_buf;
	}

	init_completion(&tcrypt_complete.restart);

	skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
					tnvvse_crypto_complete, &tcrypt_complete);

	if (aes_ctx->b_is_first == 1U || !aes_enc_dec_ctl->is_encryption) {
		if (aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_CBC)
			memcpy(next_block_iv, aes_enc_dec_ctl->initial_vector,
				TEGRA_NVVSE_AES_IV_LEN);
		else if (aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_CTR)
			memcpy(next_block_iv, aes_enc_dec_ctl->initial_counter,
				TEGRA_NVVSE_AES_CTR_LEN);
		else
			memset(next_block_iv, 0, TEGRA_NVVSE_AES_IV_LEN);
	} else {
		if (aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_CTR)
			memcpy(next_block_iv, ctx->intermediate_counter, TEGRA_NVVSE_AES_CTR_LEN);
		else		//As CBC uses IV stored in SE server
			memset(next_block_iv, 0, TEGRA_NVVSE_AES_IV_LEN);
	}
	pr_debug("%s(): %scryption\n", __func__, (aes_enc_dec_ctl->is_encryption ? "en" : "de"));


	/* copy input buffer */
	ret = copy_from_user(in_buf, aes_enc_dec_ctl->src_buffer, in_sz);
	if (ret) {
		pr_err("%s(): Failed to copy_from_user input data: %d\n", __func__, ret);
		goto free_out_buf;
	}

	sg_init_one(&in_sg, in_buf, in_sz);
	sg_init_one(&out_sg, out_buf, out_sz);
	skcipher_request_set_crypt(req, &in_sg, &out_sg, in_sz, next_block_iv);

	reinit_completion(&tcrypt_complete.restart);
	skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
			tnvvse_crypto_complete, &tcrypt_complete);
	tcrypt_complete.req_err = 0;

	/* Set first byte of next_block_iv to 1 for first encryption request and 0 for other
	 * encryption requests. This is used to invoke generation of random IV.
	 * If userNonce is not provided random IV generation is needed.
	 */
	if (aes_enc_dec_ctl->is_encryption && (aes_enc_dec_ctl->user_nonce == 0U)) {
		if (!aes_enc_dec_ctl->is_non_first_call)
			next_block_iv[0] = 1;
		else
			next_block_iv[0] = 0;
	}
	ret = aes_enc_dec_ctl->is_encryption ? crypto_skcipher_encrypt(req) :
		crypto_skcipher_decrypt(req);
	if ((ret == -EINPROGRESS) || (ret == -EBUSY)) {
		/* crypto driver is asynchronous */
		ret = wait_for_completion_timeout(&tcrypt_complete.restart,
				msecs_to_jiffies(5000));
		if (ret == 0)
			goto free_out_buf;

		if (tcrypt_complete.req_err < 0) {
			ret = tcrypt_complete.req_err;
			goto free_out_buf;
		}
	} else if (ret < 0) {
		pr_err("%s(): Failed to %scrypt: %d\n",
				__func__, aes_enc_dec_ctl->is_encryption ? "en" : "de", ret);
		goto free_out_buf;
	}

	/* copy output buffer to userspace */
	ret = copy_to_user(aes_enc_dec_ctl->dest_buffer, out_buf, out_sz);
	if (ret) {
		pr_err("%s(): Failed to copy_to_user output: %d\n", __func__, ret);
		goto free_out_buf;
	}

	if ((aes_enc_dec_ctl->is_encryption) &&
			(aes_enc_dec_ctl->user_nonce == 0U)) {
		if (aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_CBC)
			memcpy(aes_enc_dec_ctl->initial_vector, req->iv, TEGRA_NVVSE_AES_IV_LEN);
		else if (aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_CTR)
			memcpy(aes_enc_dec_ctl->initial_counter, req->iv, TEGRA_NVVSE_AES_CTR_LEN);
	}

	if (aes_enc_dec_ctl->user_nonce == 1U) {
		if (aes_enc_dec_ctl->is_encryption != 0U &&
				aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_CTR) {
			ret = update_counter(&next_block_iv[0], aes_enc_dec_ctl->data_length >> 4U);
			if (ret) {
				pr_err("%s(): Failed to update counter: %d\n",
						__func__, ret);
				goto free_out_buf;
			}

			memcpy(ctx->intermediate_counter, &next_block_iv[0],
					TEGRA_NVVSE_AES_CTR_LEN);
		}
	}

free_out_buf:
	kfree(out_buf);

free_in_buf:
	kfree(in_buf);

free_req:
	skcipher_request_free(req);

free_tfm:
	crypto_free_skcipher(tfm);

out:
	return ret;
}

static int tnvvse_crypto_aes_enc_dec_gcm(struct tnvvse_crypto_ctx *ctx,
				struct tegra_nvvse_aes_enc_dec_ctl *aes_enc_dec_ctl)
{
	struct crypto_aead *tfm;
	struct aead_request *req = NULL;
	struct scatterlist in_sg;
	struct scatterlist out_sg;
	uint8_t *in_buf, *out_buf;
	int32_t ret = 0;
	uint32_t in_sz, out_sz, aad_length, data_length, tag_length;
	struct tnvvse_crypto_completion tcrypt_complete;
	struct tegra_virtual_se_aes_context *aes_ctx;
	const char *driver_name;
	char key_as_keyslot[AES_KEYSLOT_NAME_SIZE] = {0,};
	uint8_t iv[TEGRA_NVVSE_AES_GCM_IV_LEN];
	bool enc;

	if (aes_enc_dec_ctl->aes_mode != TEGRA_NVVSE_AES_MODE_GCM) {
		pr_err("%s(): The requested AES ENC/DEC (%d) is not supported\n",
					__func__, aes_enc_dec_ctl->aes_mode);
		ret = -EINVAL;
		goto out;
	}

	if (aes_enc_dec_ctl->data_length > ivc_database.max_buffer_size[ctx->node_id]
			|| aes_enc_dec_ctl->aad_length > ivc_database.max_buffer_size[ctx->node_id] ) {
		pr_err("%s(): Input size is (data = %d, aad = %d) is not supported\n",
					__func__, aes_enc_dec_ctl->data_length,
					aes_enc_dec_ctl->aad_length);
		ret = -EINVAL;
		goto out;
	}

	tfm = crypto_alloc_aead("gcm-vse(aes)", CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_ASYNC, 0);
	if (IS_ERR(tfm)) {
		pr_err("%s(): Failed to load transform for gcm-vse(aes): %ld\n",
					__func__, PTR_ERR(tfm));
		ret = PTR_ERR(tfm);
		goto out;
	}

	aes_ctx = crypto_aead_ctx(tfm);
	aes_ctx->node_id = ctx->node_id;
	aes_ctx->user_nonce = aes_enc_dec_ctl->user_nonce;

	req = aead_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("%s(): Failed to allocate skcipher request\n", __func__);
		ret = -ENOMEM;
		goto free_tfm;
	}

	driver_name = crypto_tfm_alg_driver_name(crypto_aead_tfm(tfm));
	if (driver_name == NULL) {
		pr_err("%s(): Failed to get driver name for gcm-vse(aes)\n", __func__);
		goto free_req;
	}
	pr_debug("%s(): The aead driver name is %s for gcm-vse(aes)\n",
						__func__, driver_name);

	if ((aes_enc_dec_ctl->key_length != TEGRA_CRYPTO_KEY_128_SIZE) &&
		(aes_enc_dec_ctl->key_length != TEGRA_CRYPTO_KEY_192_SIZE) &&
		(aes_enc_dec_ctl->key_length != TEGRA_CRYPTO_KEY_256_SIZE)) {
		ret = -EINVAL;
		pr_err("%s(): crypt_req keylen(%d) invalid", __func__, aes_enc_dec_ctl->key_length);
		goto free_req;
	}

	if (aes_enc_dec_ctl->tag_length != TEGRA_NVVSE_AES_GCM_TAG_SIZE) {
		ret = -EINVAL;
		pr_err("%s(): crypt_req taglen(%d) invalid", __func__, aes_enc_dec_ctl->tag_length);
		goto free_req;
	}

	crypto_aead_clear_flags(tfm, ~0);

	ret = snprintf(key_as_keyslot, AES_KEYSLOT_NAME_SIZE, "NVSEAES ");
	memcpy(key_as_keyslot + KEYSLOT_OFFSET_BYTES, aes_enc_dec_ctl->key_slot,
		KEYSLOT_SIZE_BYTES);

	ret = crypto_aead_setkey(tfm, key_as_keyslot, aes_enc_dec_ctl->key_length);
	if (ret < 0) {
		pr_err("%s(): Failed to set key: %d\n", __func__, ret);
		goto free_req;
	}

	ret = crypto_aead_setauthsize(tfm, aes_enc_dec_ctl->tag_length);
	if (ret < 0) {
		pr_err("%s(): Failed to set tag size: %d\n", __func__, ret);
		goto free_req;
	}

	init_completion(&tcrypt_complete.restart);
	tcrypt_complete.req_err = 0;

	enc = aes_enc_dec_ctl->is_encryption;
	data_length = aes_enc_dec_ctl->data_length;
	tag_length = aes_enc_dec_ctl->tag_length;
	aad_length = aes_enc_dec_ctl->aad_length;

	aead_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
					tnvvse_crypto_complete, &tcrypt_complete);
	aead_request_set_ad(req, aad_length);

	memset(iv, 0, TEGRA_NVVSE_AES_GCM_IV_LEN);
	if (!enc || aes_enc_dec_ctl->user_nonce != 0U)
		memcpy(iv, aes_enc_dec_ctl->initial_vector, TEGRA_NVVSE_AES_GCM_IV_LEN);
	else if (enc && !aes_enc_dec_ctl->is_non_first_call)
		/* Set first byte of iv to 1 for first encryption request. This is used to invoke
		 * generation of random IV.
		 * If userNonce is not provided random IV generation is needed.
		 */
		iv[0] = 1;

	/* Prepare buffers
	 * - AEAD encryption input:  assoc data || plaintext
	 * - AEAD encryption output: assoc data || ciphertext || auth tag
	 * - AEAD decryption input:  assoc data || ciphertext || auth tag
	 * - AEAD decryption output: assoc data || plaintext
	 */
	in_sz = enc ? aad_length + data_length :
			aad_length + data_length + tag_length;
	in_buf = kzalloc(in_sz, GFP_KERNEL);
	if (in_buf == NULL) {
		ret = -ENOMEM;
		goto free_req;
	}

	out_sz = enc ? aad_length + data_length + tag_length :
			aad_length + data_length;
	out_buf = kzalloc(out_sz, GFP_KERNEL);
	if (out_buf == NULL) {
		ret = -ENOMEM;
		goto free_in_buf;
	}

	/* copy AAD buffer */
	ret = copy_from_user((void *)in_buf, (void __user *)(aes_enc_dec_ctl->aad_buffer), aad_length);
	if (ret) {
		pr_err("%s(): Failed to copy_from_user assoc data: %d\n", __func__, ret);
		goto free_buf;
	}

	/* copy data buffer */
	ret = copy_from_user((void *)in_buf + aad_length, (void __user *)(aes_enc_dec_ctl->src_buffer), data_length);
	if (ret) {
		pr_err("%s(): Failed to copy_from_user src data: %d\n", __func__, ret);
		goto free_buf;
	}

	/* copy TAG buffer in case of decryption */
	if (!enc) {
		ret = copy_from_user((void *)in_buf + aad_length + data_length,
				(void __user *)aes_enc_dec_ctl->tag_buffer, tag_length);
		if (ret) {
			pr_err("%s(): Failed copy_from_user tag data: %d\n", __func__, ret);
			goto free_buf;
		}
	}

	sg_init_one(&in_sg, in_buf, in_sz);
	sg_init_one(&out_sg, out_buf, out_sz);
	aead_request_set_crypt(req, &in_sg, &out_sg,
				enc ? data_length : data_length + tag_length,
				iv);

	ret =  enc ? crypto_aead_encrypt(req) : crypto_aead_decrypt(req);
	if ((ret == -EINPROGRESS) || (ret == -EBUSY)) {
		/* crypto driver is asynchronous */
		ret = wait_for_completion_timeout(&tcrypt_complete.restart,
					msecs_to_jiffies(5000));
		if (ret == 0)
			goto free_buf;

		if (tcrypt_complete.req_err < 0) {
			ret = tcrypt_complete.req_err;
			goto free_buf;
		}
	} else if (ret < 0) {
		pr_err("%s(): Failed to %scrypt: %d\n",
				__func__, enc ? "en" : "de", ret);
		goto free_buf;
	}

	/* copy data buffer */
	ret = copy_to_user((void __user *)aes_enc_dec_ctl->dest_buffer, (const void *)out_buf + aad_length, data_length);
	if (ret) {
		pr_err("%s(): Failed to copy_from_user assoc data: %d\n", __func__, ret);
		goto free_buf;
	}

	if (enc) {
		ret = copy_to_user((void __user *)aes_enc_dec_ctl->tag_buffer,
				(const void *)out_buf + aad_length + data_length, tag_length);
		if (ret) {
			pr_err("%s(): Failed copy_from_user tag data: %d\n", __func__, ret);
			goto free_buf;
		}

		if (aes_enc_dec_ctl->user_nonce == 0U)
			memcpy(aes_enc_dec_ctl->initial_vector, req->iv,
					TEGRA_NVVSE_AES_GCM_IV_LEN);
	}

free_buf:
	kfree(out_buf);
free_in_buf:
	kfree(in_buf);
free_req:
	aead_request_free(req);
free_tfm:
	crypto_free_aead(tfm);
out:
	return ret;
}

static int tnvvse_crypto_get_aes_drng(struct tnvvse_crypto_ctx *ctx,
		struct tegra_nvvse_aes_drng_ctl *aes_drng_ctl)
{
	struct tegra_virtual_se_rng_context *rng_ctx;
	struct crypto_rng *rng;
	int ret = -ENOMEM;

	rng = crypto_alloc_rng("rng_drbg", 0, 0);
	if (IS_ERR(rng)) {
		ret = PTR_ERR(rng);
		pr_err("(%s(): Failed to allocate crypto for rng_dbg, %d\n", __func__, ret);
		goto out;
	}

	rng_ctx = crypto_rng_ctx(rng);
	rng_ctx->node_id = ctx->node_id;

	memset(ctx->rng_buff, 0, ctx->max_rng_buff);
	ret = crypto_rng_get_bytes(rng, ctx->rng_buff, aes_drng_ctl->data_length);
	if (ret < 0) {
		pr_err("%s(): Failed to obtain the correct amount of random data for (req %d), %d\n",
				__func__, aes_drng_ctl->data_length, ret);
		goto free_rng;
	}

	ret = copy_to_user((void __user *)aes_drng_ctl->dest_buff,
				(const void *)ctx->rng_buff, aes_drng_ctl->data_length);
	if (ret) {
		pr_err("%s(): Failed to copy_to_user for length %d: %d\n",
				__func__, aes_drng_ctl->data_length, ret);
	}

free_rng:
	crypto_free_rng(rng);
out:
	return ret;
}

static int tnvvse_crypto_get_ivc_db(struct tegra_nvvse_get_ivc_db *get_ivc_db)
{
	struct crypto_dev_to_ivc_map *hv_vse_db;
	int ret = 0;
	int i;

	hv_vse_db = tegra_hv_vse_get_db();
	if (hv_vse_db == NULL)
		return -ENOMEM;

	for (i = 0; i < MAX_NUMBER_MISC_DEVICES; i++) {
		get_ivc_db->ivc_id[i] = hv_vse_db[i].ivc_id;
		get_ivc_db->se_engine[i] = hv_vse_db[i].se_engine;
		get_ivc_db->node_id[i] = hv_vse_db[i].node_id;
		get_ivc_db->priority[i] = hv_vse_db[i].priority;
		get_ivc_db->max_buffer_size[i] = hv_vse_db[i].max_buffer_size;
		get_ivc_db->channel_grp_id[i] = hv_vse_db[i].channel_grp_id;
		get_ivc_db->gcm_dec_supported[i] = hv_vse_db[i].gcm_dec_supported;
		get_ivc_db->gcm_dec_buffer_size[i] = hv_vse_db[i].gcm_dec_buffer_size;
	}

	return ret;
}

static int tnvvse_crypto_dev_open(struct inode *inode, struct file *filp)
{
	struct tnvvse_crypto_ctx *ctx;
	char root_path_buf[512];
	const char *root_path, *str;
	int ret = 0;
	uint32_t node_id;

	/* get the node id from file name */
	root_path = dentry_path_raw(filp->f_path.dentry, root_path_buf, sizeof(root_path_buf));
	str = strrchr(root_path, '-');
	if (str == NULL) {
		pr_err("%s: invalid dev node name\n", __func__);
		return -EINVAL;
	}

	if (kstrtou32(str+1, 10, &node_id)) {
		pr_err("%s: invalid crypto dev instance passed\n", __func__);
		return -EINVAL;
	}

	ctx = kzalloc(sizeof(struct tnvvse_crypto_ctx), GFP_KERNEL);
	if (!ctx) {
		return -ENOMEM;
	}
	ctx->node_id = node_id;

	mutex_init(&ctx->lock);

	ctx->rng_buff = kzalloc(NVVSE_MAX_RANDOM_NUMBER_LEN_SUPPORTED, GFP_KERNEL);
	if (!ctx->rng_buff) {
		ret = -ENOMEM;
		goto free_mutex;
	}
	ctx->max_rng_buff = NVVSE_MAX_RANDOM_NUMBER_LEN_SUPPORTED;

	/* Allocate buffer for SHA result */
	ctx->sha_result = kzalloc(NVVSE_MAX_ALLOCATED_SHA_RESULT_BUFF_SIZE, GFP_KERNEL);
	if (!ctx->sha_result) {
		ret = -ENOMEM;
		goto free_rng_buf;
	}

	filp->private_data = ctx;

	return ret;

free_rng_buf:
	kfree(ctx->rng_buff);
free_mutex:
	mutex_destroy(&ctx->lock);
	kfree(ctx);
	return ret;
}

static int tnvvse_crypto_dev_release(struct inode *inode, struct file *filp)
{
	struct tnvvse_crypto_ctx *ctx = filp->private_data;
	int ret = 0;

	mutex_destroy(&ctx->lock);
	kfree(ctx->sha_result);
	kfree(ctx->rng_buff);
	kfree(ctx);
	filp->private_data = NULL;

	return ret;
}


static long tnvvse_crypto_dev_ioctl(struct file *filp,
	unsigned int ioctl_num, unsigned long arg)
{
	struct tnvvse_crypto_ctx *ctx = filp->private_data;
	struct tegra_nvvse_aes_enc_dec_ctl __user *arg_aes_enc_dec_ctl = (void __user *)arg;
	struct tegra_nvvse_aes_gmac_init_ctl __user *arg_aes_gmac_init_ctl = (void __user *)arg;
	struct tegra_nvvse_aes_gmac_sign_verify_ctl __user *arg_aes_gmac_sign_verify_ctl;
	struct tegra_nvvse_aes_cmac_sign_verify_ctl __user *arg_aes_cmac_sign_verify_ctl;
	struct tegra_nvvse_sha_init_ctl *sha_init_ctl;
	struct tegra_nvvse_sha_update_ctl *sha_update_ctl;
	struct tegra_nvvse_sha_final_ctl *sha_final_ctl;
	struct tegra_nvvse_aes_enc_dec_ctl *aes_enc_dec_ctl;
	struct tegra_nvvse_aes_cmac_sign_verify_ctl *aes_cmac_sign_verify_ctl;
	struct tegra_nvvse_aes_drng_ctl *aes_drng_ctl;
	struct tegra_nvvse_aes_gmac_init_ctl *aes_gmac_init_ctl;
	struct tegra_nvvse_aes_gmac_sign_verify_ctl *aes_gmac_sign_verify_ctl;
	struct tegra_nvvse_get_ivc_db get_ivc_db;
	struct tegra_nvvse_tsec_get_keyload_status *tsec_keyload_status;
	int ret = 0;

	/*
	 * Avoid processing ioctl if the file has been closed.
	 * This will prevent crashes caused by NULL pointer dereference.
	 */
	if (!ctx) {
		pr_err("%s(): ctx not allocated\n", __func__);
		return -EPERM;
	}

	mutex_lock(&ctx->lock);

	switch (ioctl_num) {
	case NVVSE_IOCTL_CMDID_INIT_SHA:
		sha_init_ctl = kzalloc(sizeof(*sha_init_ctl), GFP_KERNEL);
		if (!sha_init_ctl) {
			pr_err("%s(): failed to allocate memory\n", __func__);
			return -ENOMEM;
		}

		ret = copy_from_user(sha_init_ctl, (void __user *)arg, sizeof(*sha_init_ctl));
		if (ret) {
			pr_err("%s(): Failed to copy_from_user sha_init_ctl:%d\n", __func__, ret);
			kfree(sha_init_ctl);
			goto out;
		}

		ret = tnvvse_crypto_sha_init(ctx, sha_init_ctl);

		kfree(sha_init_ctl);
		break;

	case NVVSE_IOCTL_CMDID_UPDATE_SHA:
		sha_update_ctl = kzalloc(sizeof(*sha_update_ctl), GFP_KERNEL);
		if (!sha_update_ctl) {
			pr_err("%s(): failed to allocate memory\n", __func__);
			return -ENOMEM;
		}

		ret = copy_from_user(sha_update_ctl, (void __user *)arg, sizeof(*sha_update_ctl));
		if (ret) {
			pr_err("%s(): Failed to copy_from_user sha_update_ctl:%d\n", __func__, ret);
			kfree(sha_update_ctl);
			goto out;
		}

		ret = tnvvse_crypto_sha_update(ctx, sha_update_ctl);

		kfree(sha_update_ctl);
		break;


	case NVVSE_IOCTL_CMDID_FINAL_SHA:
		sha_final_ctl = kzalloc(sizeof(*sha_final_ctl), GFP_KERNEL);
		if (!sha_final_ctl) {
			pr_err("%s(): failed to allocate memory\n", __func__);
			return -ENOMEM;
		}

		ret = copy_from_user(sha_final_ctl, (void __user *)arg, sizeof(*sha_final_ctl));
		if (ret) {
			pr_err("%s(): Failed to copy_from_user sha_final_ctl:%d\n", __func__, ret);
			kfree(sha_final_ctl);
			goto out;
		}

		ret = tnvvse_crypto_sha_final(ctx, sha_final_ctl);

		kfree(sha_final_ctl);
		break;

	case NVVSE_IOCTL_CMDID_AES_ENCDEC:
		aes_enc_dec_ctl = kzalloc(sizeof(*aes_enc_dec_ctl), GFP_KERNEL);
		if (!aes_enc_dec_ctl) {
			pr_err("%s(): failed to allocate memory\n", __func__);
			return -ENOMEM;
		}

		ret = copy_from_user(aes_enc_dec_ctl, (void __user *)arg, sizeof(*aes_enc_dec_ctl));
		if (ret) {
			pr_err("%s(): Failed to copy_from_user aes_enc_dec_ctl:%d\n", __func__, ret);
			kfree(aes_enc_dec_ctl);
			goto out;
		}

		if (aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_GCM)
			ret = tnvvse_crypto_aes_enc_dec_gcm(ctx, aes_enc_dec_ctl);
		else
			ret = tnvvse_crypto_aes_enc_dec(ctx, aes_enc_dec_ctl);

		if (ret) {
			kfree(aes_enc_dec_ctl);
			goto out;
		}

		/* Copy IV returned by VSE */
		if (aes_enc_dec_ctl->is_encryption) {
			if (aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_CBC ||
				aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_GCM)
				ret = copy_to_user(arg_aes_enc_dec_ctl->initial_vector,
							aes_enc_dec_ctl->initial_vector,
							sizeof(aes_enc_dec_ctl->initial_vector));
			else if (aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_CTR)
				ret = copy_to_user(arg_aes_enc_dec_ctl->initial_counter,
							aes_enc_dec_ctl->initial_counter,
							sizeof(aes_enc_dec_ctl->initial_counter));
			if (ret) {
				pr_err("%s(): Failed to copy_to_user:%d\n", __func__, ret);
				kfree(aes_enc_dec_ctl);
				goto out;
			}
		}

		kfree(aes_enc_dec_ctl);
		break;

	case NVVSE_IOCTL_CMDID_AES_GMAC_INIT:
		aes_gmac_init_ctl = kzalloc(sizeof(*aes_gmac_init_ctl), GFP_KERNEL);
		if (!aes_gmac_init_ctl) {
			pr_err("%s(): failed to allocate memory\n", __func__);
			return -ENOMEM;
		}

		ret = copy_from_user(aes_gmac_init_ctl, (void __user *)arg,
						sizeof(*aes_gmac_init_ctl));
		if (ret) {
			pr_err("%s(): Failed to copy_from_user aes_gmac_init_ctl:%d\n",
								__func__, ret);
			kfree(aes_gmac_init_ctl);
			goto out;
		}

		ret = tnvvse_crypto_aes_gmac_init(ctx, aes_gmac_init_ctl);
		if (ret) {
			kfree(aes_gmac_init_ctl);
			goto out;
		}

		/* Copy IV returned by VSE */
		ret = copy_to_user(arg_aes_gmac_init_ctl->IV, aes_gmac_init_ctl->IV,
							sizeof(aes_gmac_init_ctl->IV));
		if (ret) {
			pr_err("%s(): Failed to copy_to_user:%d\n", __func__, ret);
			kfree(aes_gmac_init_ctl);
			goto out;
		}

		kfree(aes_gmac_init_ctl);
		break;

	case NVVSE_IOCTL_CMDID_AES_GMAC_SIGN_VERIFY:
		aes_gmac_sign_verify_ctl = kzalloc(sizeof(*aes_gmac_sign_verify_ctl), GFP_KERNEL);
		if (!aes_gmac_sign_verify_ctl) {
			pr_err("%s(): failed to allocate memory\n", __func__);
			return -ENOMEM;
		}

		arg_aes_gmac_sign_verify_ctl = (void __user *)arg;
		ret = copy_from_user(aes_gmac_sign_verify_ctl, (void __user *)arg,
						sizeof(*aes_gmac_sign_verify_ctl));
		if (ret) {
			pr_err("%s(): Failed to copy_from_user aes_gmac_sign_verify_ctl:%d\n",
								 __func__, ret);
			kfree(aes_gmac_sign_verify_ctl);
			goto out;
		}

		ret = tnvvse_crypto_aes_gmac_sign_verify(ctx, aes_gmac_sign_verify_ctl);
		if (ret) {
			kfree(aes_gmac_sign_verify_ctl);
			goto out;
		}

		if (aes_gmac_sign_verify_ctl->gmac_type == TEGRA_NVVSE_AES_GMAC_VERIFY) {
			ret = copy_to_user(&arg_aes_gmac_sign_verify_ctl->result,
						&aes_gmac_sign_verify_ctl->result,
								sizeof(uint8_t));
			if (ret)
				pr_err("%s(): Failed to copy_to_user:%d\n", __func__, ret);
		}

		kfree(aes_gmac_sign_verify_ctl);
		break;

	case NVVSE_IOCTL_CMDID_AES_CMAC_SIGN_VERIFY:
		aes_cmac_sign_verify_ctl = kzalloc(sizeof(*aes_cmac_sign_verify_ctl), GFP_KERNEL);
		if (!aes_cmac_sign_verify_ctl) {
			pr_err("%s(): failed to allocate memory\n", __func__);
			return -ENOMEM;
		}

		arg_aes_cmac_sign_verify_ctl = (void __user *)arg;
		ret = copy_from_user(aes_cmac_sign_verify_ctl, (void __user *)arg,
					sizeof(*aes_cmac_sign_verify_ctl));
		if (ret) {
			pr_err("%s(): Failed to copy_from_user aes_cmac_sign_verify:%d\n",
						__func__, ret);
			kfree(aes_cmac_sign_verify_ctl);
			goto out;
		}

		ret = tnvvse_crypto_aes_cmac_sign_verify(ctx, aes_cmac_sign_verify_ctl);
		if (ret) {
			kfree(aes_cmac_sign_verify_ctl);
			goto out;
		}

		if (aes_cmac_sign_verify_ctl->cmac_type == TEGRA_NVVSE_AES_CMAC_VERIFY) {
			ret = copy_to_user(&arg_aes_cmac_sign_verify_ctl->result,
						&aes_cmac_sign_verify_ctl->result,
								sizeof(uint8_t));
			if (ret)
				pr_err("%s(): Failed to copy_to_user:%d\n", __func__, ret);
		}

		kfree(aes_cmac_sign_verify_ctl);
		break;

	case NVVSE_IOCTL_CMDID_AES_DRNG:
		aes_drng_ctl = kzalloc(sizeof(*aes_drng_ctl), GFP_KERNEL);
		if (!aes_drng_ctl) {
			pr_err("%s(): failed to allocate memory\n", __func__);
			return -ENOMEM;
		}

		ret = copy_from_user(aes_drng_ctl, (void __user *)arg, sizeof(*aes_drng_ctl));
		if (ret) {
			pr_err("%s(): Failed to copy_from_user aes_drng_ctl:%d\n", __func__, ret);
			kfree(aes_drng_ctl);
			goto out;
		}
		ret = tnvvse_crypto_get_aes_drng(ctx, aes_drng_ctl);

		kfree(aes_drng_ctl);
		break;

	case NVVSE_IOCTL_CMDID_GET_IVC_DB:
		ret = tnvvse_crypto_get_ivc_db(&get_ivc_db);
		if (ret) {
			pr_err("%s(): Failed to get ivc database get_ivc_db:%d\n", __func__, ret);
			goto out;
		}

		ret = copy_to_user((void __user *)arg, &ivc_database, sizeof(ivc_database));
		if (ret) {
			pr_err("%s(): Failed to copy_to_user ivc_database:%d\n", __func__, ret);
			goto out;
		}

		break;

	case NVVSE_IOCTL_CMDID_TSEC_SIGN_VERIFY:
		aes_cmac_sign_verify_ctl = kzalloc(sizeof(*aes_cmac_sign_verify_ctl), GFP_KERNEL);
		if (!aes_cmac_sign_verify_ctl) {
			pr_err("%s(): failed to allocate memory\n", __func__);
			return -ENOMEM;
		}

		arg_aes_cmac_sign_verify_ctl = (void __user *)arg;
		ret = copy_from_user(aes_cmac_sign_verify_ctl, (void __user *)arg,
					sizeof(*aes_cmac_sign_verify_ctl));
		if (ret) {
			pr_err("%s(): Failed to copy_from_user tsec_sign_verify:%d\n",
						__func__, ret);
			kfree(aes_cmac_sign_verify_ctl);
			goto out;
		}
		ret = tnvvtsec_crypto_aes_cmac_sign_verify(ctx, aes_cmac_sign_verify_ctl);
		if (ret) {
			kfree(aes_cmac_sign_verify_ctl);
			goto out;
		}

		if (aes_cmac_sign_verify_ctl->cmac_type == TEGRA_NVVSE_AES_CMAC_VERIFY) {
			ret = copy_to_user(&arg_aes_cmac_sign_verify_ctl->result,
						&aes_cmac_sign_verify_ctl->result,
								sizeof(uint8_t));
			if (ret)
				pr_err("%s(): Failed to copy_to_user:%d\n", __func__, ret);
		}

		kfree(aes_cmac_sign_verify_ctl);
		break;

	case NVVSE_IOCTL_CMDID_TSEC_GET_KEYLOAD_STATUS:
		tsec_keyload_status = kzalloc(sizeof(*tsec_keyload_status), GFP_KERNEL);
		if (!tsec_keyload_status) {
			pr_err("%s(): failed to allocate memory\n", __func__);
			return -ENOMEM;
		}

		ret = tnvvse_crypto_tsec_get_keyload_status(ctx, tsec_keyload_status);
		if (ret) {
			pr_err("%s(): Failed to get keyload status:%d\n", __func__, ret);
			kfree(tsec_keyload_status);
			goto out;
		}

		ret = copy_to_user((void __user *)arg, tsec_keyload_status,
				sizeof(*tsec_keyload_status));
		if (ret) {
			pr_err("%s(): Failed to copy_to_user tsec_keyload_status:%d\n",
					__func__, ret);
			kfree(tsec_keyload_status);
			goto out;
		}

		kfree(tsec_keyload_status);
		break;

	default:
		pr_err("%s(): invalid ioctl code(%d[0x%08x])", __func__, ioctl_num, ioctl_num);
		ret = -EINVAL;
		break;
	}

out:
	mutex_unlock(&ctx->lock);

	return ret;
}

static const struct file_operations tnvvse_crypto_fops = {
	.owner			= THIS_MODULE,
	.open			= tnvvse_crypto_dev_open,
	.release		= tnvvse_crypto_dev_release,
	.unlocked_ioctl		= tnvvse_crypto_dev_ioctl,
};

static int __init tnvvse_crypto_device_init(void)
{
	uint32_t cnt, ctr;
	int ret = 0;
	struct miscdevice *misc;

	/* get ivc databse */
	tnvvse_crypto_get_ivc_db(&ivc_database);

	for (cnt = 0; cnt < MAX_NUMBER_MISC_DEVICES; cnt++) {

		/* Dynamic initialisation of misc device */
		misc = kzalloc(sizeof(struct miscdevice), GFP_KERNEL);
		if (misc == NULL) {
			ret = -ENOMEM;
			goto fail;
		} else {

			misc->minor = MISC_DYNAMIC_MINOR;
			misc->fops = &tnvvse_crypto_fops;

			misc->name = kzalloc(MISC_DEVICE_NAME_LEN, GFP_KERNEL);
			if (misc->name == NULL) {
				ret = -ENOMEM;
				goto fail;
			}

			ret = snprintf((char *)misc->name, MISC_DEVICE_NAME_LEN,
								"tegra-nvvse-crypto-%u", cnt);
			if (ret >= MISC_DEVICE_NAME_LEN) {
				pr_err("%s: buffer overflown for misc dev %u\n", __func__, cnt);
				ret = -EINVAL;
				goto fail;
			}
		}

		ret = misc_register(misc);
		if (ret != 0) {
			pr_err("%s: misc dev %u registeration failed err %d\n", __func__, cnt, ret);
			goto fail;
		}
		nvvse_devnode[cnt].g_misc_devices = misc;
	}

	return ret;

fail:
	for (ctr = 0; ctr < cnt; ctr++) {
		misc_deregister(nvvse_devnode[ctr].g_misc_devices);
		kfree(nvvse_devnode[ctr].g_misc_devices->name);
		kfree(nvvse_devnode[ctr].g_misc_devices);
		nvvse_devnode[ctr].g_misc_devices = NULL;
	}
	return ret;
}
module_init(tnvvse_crypto_device_init);

static void __exit tnvvse_crypto_device_exit(void)
{
	uint32_t ctr;

	for (ctr = 0; ctr < MAX_NUMBER_MISC_DEVICES; ctr++) {
		if (nvvse_devnode[ctr].g_misc_devices != NULL) {
			misc_deregister(nvvse_devnode[ctr].g_misc_devices);
			kfree(nvvse_devnode[ctr].g_misc_devices->name);
			kfree(nvvse_devnode[ctr].g_misc_devices);
			nvvse_devnode[ctr].g_misc_devices = NULL;
		}
	}
}
module_exit(tnvvse_crypto_device_exit);

MODULE_DESCRIPTION("Tegra NVVSE Crypto device driver.");
MODULE_AUTHOR("NVIDIA Corporation");
MODULE_LICENSE("GPL v2");

