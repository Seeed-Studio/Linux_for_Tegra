// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2025 NVIDIA CORPORATION & AFFILIATES.
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
#include <crypto/sm3.h>
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
#define NVVSE_MAX_RANDOM_NUMBER_LEN_SUPPORTED		1024U

#define INT32_BYTES 4U
#define CTR_TO_INT32 4U

/**
 * Define preallocated SHA result buffer size, if digest size is bigger
 * than this then allocate new buffer
 */
#define NVVSE_MAX_ALLOCATED_SHA_RESULT_BUFF_SIZE	256U

#define MISC_DEVICE_NAME_LEN		33U

#define CRYPTODEV_ERR(...) pr_err("tegra_nvvse_cryptodev " __VA_ARGS__)
#define CRYPTODEV_INFO(...) pr_info("tegra_nvvse_cryptodev " __VA_ARGS__)

struct nvvse_devnode {
	struct miscdevice *g_misc_devices;
	struct mutex lock;
	bool node_in_use;
} nvvse_devnode[MAX_NUMBER_MISC_DEVICES];

/* Info device node support */
static struct miscdevice *nvvse_info_device;

static struct tegra_nvvse_get_ivc_db ivc_database;

/* SHA Algorithm Names */
static const char * const sha_alg_names[] = {
	"sha256-vse",
	"sha384-vse",
	"sha512-vse",
	"sha3-256-vse",
	"sha3-384-vse",
	"sha3-512-vse",
	"shake128-vse",
	"shake256-vse",
	"sm3-vse",
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
	uint8_t				*in_buf;
	struct tnvvse_crypto_completion	sha_complete;
	struct ahash_request		*req;
	struct crypto_ahash		*tfm;
	char				*result_buff;
	bool				sha_init_done;
	uint64_t			sha_total_msg_length;
	char				*sha_intermediate_digest;
	bool				hmac_sha_init_done;
	uint64_t			hmac_sha_total_msg_length;
};

/* Tegra NVVSE crypt context */
struct tnvvse_crypto_ctx {
	struct crypto_sha_state		sha_state;
	uint8_t				intermediate_counter[TEGRA_NVVSE_AES_IV_LEN];
	char				*rng_buff;
	uint32_t			max_rng_buff;
	char				*sha_result;
	uint32_t			node_id;
	bool				is_zero_copy_node;
	uint32_t			allocated_key_slot_count;
	uint32_t			key_grp_id;
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
		CRYPTODEV_ERR("%s():AES-CTR Counter overflowed", __func__);
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

static int tnvvse_crypto_allocate_key_slot(struct tnvvse_crypto_ctx *ctx,
	struct tegra_nvvse_allocate_key_slot_ctl *key_slot_allocate_ctl)
{
	struct tegra_vse_key_slot_ctx key_slot_params;
	int err = 0;

	if (!ctx || !key_slot_allocate_ctl) {
		CRYPTODEV_ERR("%s: Invalid parameters\n", __func__);
		return -EINVAL;
	}

	if (current->pid >= 0) {
		ctx->key_grp_id = (uint32_t)current->pid;
	} else {
		CRYPTODEV_ERR("%s(): Invalid PID\n", __func__);
		return -EINVAL;
	}

	memset(&key_slot_params, 0, sizeof(key_slot_params));
	memcpy(key_slot_params.key_id, key_slot_allocate_ctl->key_id, KEYSLOT_SIZE_BYTES);
	key_slot_params.key_usage = key_slot_allocate_ctl->key_usage;
	key_slot_params.token_id = key_slot_allocate_ctl->token_id;
	key_slot_params.key_grp_id = ctx->key_grp_id;
	err = tegra_hv_vse_allocate_keyslot(&key_slot_params, ctx->node_id);
	if (err) {
		CRYPTODEV_ERR("%s: Failed to allocate key slot, error: %d\n",
		__func__, err);
		return err;
	}

	ctx->allocated_key_slot_count += 1U;

	key_slot_allocate_ctl->key_instance_idx = key_slot_params.key_instance_idx;

	return 0;
}

static int tnvvse_crypto_release_key_slot(struct tnvvse_crypto_ctx *ctx,
	struct tegra_nvvse_release_key_slot_ctl *key_slot_release_ctl)
{
	int err = 0;
	struct tegra_vse_key_slot_ctx vse_key_slot;

	if (!ctx) {
		CRYPTODEV_ERR("%s: Invalid context\n", __func__);
		return -EINVAL;
	}

	if (!key_slot_release_ctl) {
		CRYPTODEV_ERR("Key slot release ctl is NULL\n");
		return -EINVAL;
	}
	if (ctx->allocated_key_slot_count == 0U) {
		CRYPTODEV_ERR("No key slots allocated to release\n");
		return -EINVAL;
	}

	memset(&vse_key_slot, 0, sizeof(vse_key_slot));
	memcpy(vse_key_slot.key_id, key_slot_release_ctl->key_id, sizeof(vse_key_slot.key_id));

	if (key_slot_release_ctl->key_instance_idx > 255) {
		CRYPTODEV_ERR("key_instance_idx value %u exceeds maximum allowed value 255\n",
			key_slot_release_ctl->key_instance_idx);
		return -EINVAL;
	}
	vse_key_slot.key_instance_idx = key_slot_release_ctl->key_instance_idx;

	err = tegra_hv_vse_release_keyslot(&vse_key_slot, ctx->node_id);
	if (err) {
		CRYPTODEV_ERR("Failed to release key slot: %d\n", err);
		return err;
	}

	ctx->allocated_key_slot_count -= 1U;

	return 0;
}

static int tnvvse_crypto_validate_sha_update_req(struct tnvvse_crypto_ctx *ctx,
		struct tegra_nvvse_sha_update_ctl *sha_update_ctl)
{
	struct crypto_sha_state *sha_state = &ctx->sha_state;
	enum tegra_nvvse_sha_type sha_type = sha_update_ctl->sha_type;
	int32_t ret = 0;

	if ((sha_type < TEGRA_NVVSE_SHA_TYPE_SHA256) || (sha_type >= TEGRA_NVVSE_SHA_TYPE_MAX)) {
		CRYPTODEV_ERR("%s(): SHA Type requested %d is not supported\n", __func__, sha_type);
		ret = -EINVAL;
		goto exit;
	}

	if ((sha_type == TEGRA_NVVSE_SHA_TYPE_SHAKE128 ||
		sha_type == TEGRA_NVVSE_SHA_TYPE_SHAKE256) &&
		sha_update_ctl->digest_size == 0) {
		CRYPTODEV_ERR("%s: Digest Buffer Size is invalid\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	if (sha_update_ctl->init_only != 0U) {
		if (sha_state->sha_init_done != 0) {
			CRYPTODEV_INFO("%s(): SHA init is already done\n", __func__);
			ret = -EAGAIN;
			goto exit;
		} else {
			if (sha_update_ctl->is_first == 0U) {
				CRYPTODEV_ERR("%s(): When init_only is true, is_first can not be false\n"
				, __func__);
				ret = -EINVAL;
				goto exit;
			}

			/*
			 * Return success as other parameters don't need not be validated for
			 * init only request.
			 */
			ret = 0;
			goto exit;
		}
	}

	if ((sha_update_ctl->is_first != 0U) && (sha_state->sha_total_msg_length > 0U)) {
		CRYPTODEV_ERR("%s(): SHA First request is already received\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	if ((sha_state->sha_init_done == 0) && (sha_update_ctl->is_first == 0U)) {
		CRYPTODEV_ERR("%s(): SHA First req is not yet received\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	if (((sha_type == TEGRA_NVVSE_SHA_TYPE_SHAKE128) ||
		(sha_type == TEGRA_NVVSE_SHA_TYPE_SHAKE256)) &&
		sha_update_ctl->digest_size == 0) {
		CRYPTODEV_ERR("%s: Digest Buffer Size is invalid\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	if (sha_update_ctl->input_buffer_size == 0U) {
		if (sha_update_ctl->is_last == 0U) {
			CRYPTODEV_ERR("%s(): zero length non-last request is not supported\n",
			__func__);
			ret = -EINVAL;
			goto exit;
		}
	}

	if (sha_update_ctl->is_last == 0U) {
		if (sha_update_ctl->do_reset == 1U) {
			CRYPTODEV_ERR("%s(): do_reset is not supported for non-last request\n",
			__func__);
			ret = -EINVAL;
			goto exit;
		}
	}

	if (ctx->is_zero_copy_node) {
		if (sha_update_ctl->b_is_zero_copy == 0U) {
			CRYPTODEV_ERR("%s(): only zero copy operation is supported on this node\n",
									__func__);
			ret = -EINVAL;
			goto exit;
		}

		if ((sha_type != TEGRA_NVVSE_SHA_TYPE_SHA256)
				&& (sha_type != TEGRA_NVVSE_SHA_TYPE_SHA384)
				&& (sha_type != TEGRA_NVVSE_SHA_TYPE_SHA512)
				&& (sha_type != TEGRA_NVVSE_SHA_TYPE_SHA3_256)
				&& (sha_type != TEGRA_NVVSE_SHA_TYPE_SHA3_384)
				&& (sha_type != TEGRA_NVVSE_SHA_TYPE_SHA3_512)) {
			CRYPTODEV_ERR("%s(): unsupported SHA req type for zero-copy", __func__);
			ret = -EINVAL;
		}
	} else {
		if (sha_update_ctl->b_is_zero_copy != 0U) {
			CRYPTODEV_ERR("%s(): zero copy operation is not supported on this node\n",
									__func__);
			ret = -EINVAL;
			goto exit;
		}

		if (sha_update_ctl->input_buffer_size >
				ivc_database.max_buffer_size[ctx->node_id]) {
			CRYPTODEV_ERR("%s(): Msg size is greater than supported size of %d Bytes\n",
				__func__, ivc_database.max_buffer_size[ctx->node_id]);
			ret = -EINVAL;
		}
	}

exit:
	return ret;
}

static int tnvvse_crypto_sha_update(struct tnvvse_crypto_ctx *ctx,
				struct tegra_nvvse_sha_update_ctl *sha_update_ctl)
{
	struct crypto_sha_state *sha_state;
	struct tegra_virtual_se_sha_context *sha_ctx;
	struct crypto_ahash *tfm = NULL;
	struct ahash_request *req = NULL;
	struct tnvvse_crypto_completion sha_complete;
	enum tegra_nvvse_sha_type sha_type;
	int ret = -ENOMEM;

	sha_state = &ctx->sha_state;

	ret = tnvvse_crypto_validate_sha_update_req(ctx, sha_update_ctl);
	if (ret != 0) {
		if (ret != -EAGAIN) {
			/* Force reset SHA state and return */
			sha_state->sha_init_done = 0U;
			sha_state->sha_total_msg_length = 0U;
		}
		goto exit;
	}

	sha_type = sha_update_ctl->sha_type;

	if (sha_update_ctl->do_reset != 0U) {
		/* Force reset SHA state and return */
		sha_state->sha_init_done = 0U;
		sha_state->sha_total_msg_length = 0U;
		ret = 0;
		goto exit;
	}

	if (sha_update_ctl->init_only != 0U) {
		/* Only set state as SHA init done and return */
		sha_state->sha_init_done = 1U;
		ret = 0;
		goto exit;
	}

	if (sha_update_ctl->is_first != 0U)
		sha_state->sha_init_done = 1U;

	if ((sha_type < TEGRA_NVVSE_SHA_TYPE_SHA256) || (sha_type >= TEGRA_NVVSE_SHA_TYPE_MAX)) {
		CRYPTODEV_ERR("%s(): SHA Type requested %d is not supported\n", __func__, sha_type);
		ret = -EINVAL;
		goto exit;
	}

	tfm = crypto_alloc_ahash(sha_alg_names[sha_type], 0, 0);
	if (!tfm || IS_ERR(tfm)) {
		CRYPTODEV_ERR("%s(): Failed to load transform for %s:%ld\n",
				__func__, sha_alg_names[sha_type], PTR_ERR(tfm));
		ret = PTR_ERR(tfm);
		goto exit;
	}
	crypto_ahash_clear_flags(tfm, ~0U);

	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		crypto_free_ahash(tfm);
		CRYPTODEV_ERR("%s(): Failed to allocate request\n", __func__);
		ret = -ENOMEM;
		goto free_tfm;
	}

	sha_ctx = crypto_ahash_ctx(tfm);
	sha_ctx->node_id = ctx->node_id;

	init_completion(&sha_complete.restart);
	sha_complete.req_err = 0;
	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   tnvvse_crypto_complete, &sha_complete);

	if (sha_state->sha_total_msg_length > (ULONG_MAX - sha_ctx->user_src_buf_size)) {
		CRYPTODEV_ERR("%s(): Total message length overflow\n", __func__);
		ret = -EINVAL;
		goto free_req;
	}
	sha_ctx->user_src_buf_size = sha_update_ctl->input_buffer_size;
	sha_state->sha_total_msg_length += sha_ctx->user_src_buf_size;

	sha_ctx->digest_size = sha_update_ctl->digest_size;
	sha_ctx->total_count = sha_state->sha_total_msg_length;
	sha_ctx->intermediate_digest = sha_state->sha_intermediate_digest;
	sha_ctx->user_digest_buffer = sha_update_ctl->digest_buffer;

	if (ctx->is_zero_copy_node)
		sha_ctx->user_src_iova = sha_update_ctl->in_buff_iova;
	else
		sha_ctx->user_src_buf = sha_update_ctl->in_buff;

	if (sha_state->sha_total_msg_length == sha_ctx->user_src_buf_size)
		sha_ctx->is_first = true;
	else
		sha_ctx->is_first = false;

	ret = wait_async_op(&sha_complete, crypto_ahash_init(req));
	if (ret) {
		CRYPTODEV_ERR("%s(): Failed to initialize ahash: %d\n", __func__, ret);
		sha_state->sha_init_done = 0U;
		sha_state->sha_total_msg_length = 0U;
		goto free_req;
	}

	if (sha_update_ctl->is_last == 0U) {
		ret = wait_async_op(&sha_complete, crypto_ahash_update(req));
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed to ahash_update: %d\n", __func__, ret);
			sha_state->sha_init_done = 0U;
			sha_state->sha_total_msg_length = 0U;
			goto free_req;
		}
	} else {
		ret = wait_async_op(&sha_complete, crypto_ahash_finup(req));
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed to ahash_finup: %d\n", __func__, ret);
			sha_state->sha_init_done = 0U;
			sha_state->sha_total_msg_length = 0U;
			goto free_req;
		}

		if ((sha_type != TEGRA_NVVSE_SHA_TYPE_SHAKE128)
				&& (sha_type != TEGRA_NVVSE_SHA_TYPE_SHAKE256)) {
			if (sha_update_ctl->digest_size != crypto_ahash_digestsize(tfm)) {
				CRYPTODEV_ERR("%s(): %s: input digest size of %d is invalid\n",
					__func__, sha_alg_names[sha_type],
					sha_update_ctl->digest_size);
				ret = -EINVAL;
				goto free_req;
			}
		}

		/* Reset sha state */
		sha_state->sha_init_done = 0U;
		sha_state->sha_total_msg_length = 0U;
	}

free_req:
	if (req)
		ahash_request_free(req);
free_tfm:
	if (tfm)
		crypto_free_ahash(tfm);

exit:
	return ret;
}

static int tnvvse_crypto_hmac_sha_validate_req(struct tnvvse_crypto_ctx *ctx,
		struct tegra_nvvse_hmac_sha_sv_ctl *hmac_sha_ctl)
{
	struct crypto_sha_state *sha_state = &ctx->sha_state;
	int32_t ret = 0;

	if (hmac_sha_ctl->hmac_sha_mode != TEGRA_NVVSE_SHA_TYPE_SHA256) {
		CRYPTODEV_ERR("%s: Invalid HMAC SHA mode\n", __func__);
		return -EINVAL;
	}

	if ((hmac_sha_ctl->hmac_sha_type != TEGRA_NVVSE_HMAC_SHA_SIGN) &&
		(hmac_sha_ctl->hmac_sha_type != TEGRA_NVVSE_HMAC_SHA_VERIFY)) {
		CRYPTODEV_ERR("%s: Invalid HMAC_SHA request TYPE\n", __func__);
		return -EINVAL;
	}

	if ((hmac_sha_ctl->is_first != 0)
		&& (sha_state->hmac_sha_init_done != 0)) {
		CRYPTODEV_ERR("%s: HMAC-Sha init already done for this node_id %u\n", __func__,
		ctx->node_id);
		ret = -EAGAIN;
		goto exit;
	}

	if (hmac_sha_ctl->is_last != 0U) {
		if (hmac_sha_ctl->digest_buffer == NULL) {
			CRYPTODEV_ERR("%s: Invalid HMAC SHA digest buffer", __func__);
			return -EINVAL;
		}
	}

	if ((hmac_sha_ctl->is_first == 0U ||
		hmac_sha_ctl->is_last == 0U)  &&
		(hmac_sha_ctl->data_length == 0U ||
		hmac_sha_ctl->src_buffer == NULL)) {
		CRYPTODEV_ERR("%s: Invalid HMAC_SHA Input Buffer or size", __func__);
		return -EINVAL;
	}

	if (hmac_sha_ctl->data_length > ivc_database.max_buffer_size[ctx->node_id]) {
		CRYPTODEV_ERR("%s(): Input size is (data = %d) is not supported\n",
				__func__, hmac_sha_ctl->data_length);
		ret = -EINVAL;
		goto exit;
	}

exit:
	return ret;
}

static int tnvvse_crypto_hmac_sha_sign_verify(struct tnvvse_crypto_ctx *ctx,
				struct tegra_nvvse_hmac_sha_sv_ctl *hmac_sha_ctl)
{
	struct crypto_sha_state *sha_state = &ctx->sha_state;
	struct tegra_virtual_se_hmac_sha_context *hmac_ctx;
	struct crypto_ahash *tfm = NULL;
	struct ahash_request *req = NULL;
	struct tnvvse_crypto_completion hmac_sha_complete;
	char key_as_keyslot[AES_KEYSLOT_NAME_SIZE] = {0,};
	int ret = -ENOMEM;

	ret = tnvvse_crypto_hmac_sha_validate_req(ctx, hmac_sha_ctl);
	if (ret != 0) {
		sha_state->hmac_sha_init_done = 0;
		sha_state->hmac_sha_total_msg_length = 0;
		goto exit;
	}

	tfm = crypto_alloc_ahash("hmac-sha256-vse", 0, 0);
	if (!tfm || IS_ERR(tfm)) {
		ret = PTR_ERR(tfm);
		CRYPTODEV_ERR("%s(): Failed to allocate ahash for hmac-sha256-vse: %d\n",
				__func__, ret);
		ret = -ENOMEM;
		goto exit;
	}

	hmac_ctx = crypto_ahash_ctx(tfm);
	hmac_ctx->node_id = ctx->node_id;

	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		crypto_free_ahash(tfm);
		CRYPTODEV_ERR("%s(): Failed to allocate request for cmac-vse(aes)\n", __func__);
		ret = -ENOMEM;
		goto free_tfm;
	}

	init_completion(&hmac_sha_complete.restart);
	hmac_sha_complete.req_err = 0;
	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   tnvvse_crypto_complete, &hmac_sha_complete);

	(void)snprintf(key_as_keyslot, AES_KEYSLOT_NAME_SIZE, "NVSEAES ");
	memcpy(key_as_keyslot + KEYSLOT_OFFSET_BYTES, hmac_sha_ctl->key_slot,
		KEYSLOT_SIZE_BYTES);
	ret = crypto_ahash_setkey(tfm, key_as_keyslot, KEYSLOT_SIZE_BYTES);
	if (ret) {
		CRYPTODEV_ERR("%s(): Failed to set keys for hmac: %d\n", __func__, ret);
		goto free_req;
	}

	hmac_ctx->user_src_buf_size = hmac_sha_ctl->data_length;

	crypto_ahash_clear_flags(tfm, ~0U);

	if (hmac_sha_ctl->hmac_sha_type == TEGRA_NVVSE_HMAC_SHA_SIGN)
		hmac_ctx->request_type = TEGRA_HV_VSE_HMAC_SHA_SIGN;
	else
		hmac_ctx->request_type = TEGRA_HV_VSE_HMAC_SHA_VERIFY;

	hmac_ctx->result = 0;

	if (sha_state->hmac_sha_total_msg_length > (ULONG_MAX - hmac_sha_ctl->data_length)) {
		CRYPTODEV_ERR("%s(): Total message length would overflow\n", __func__);
		ret = -EOVERFLOW;
		goto free_req;
	}

	if (sha_state->hmac_sha_total_msg_length > (ULONG_MAX - hmac_sha_ctl->data_length)) {
		CRYPTODEV_ERR("%s(): Total message length would overflow\n", __func__);
		ret = -EOVERFLOW;
		goto free_tfm;
	}
	sha_state->hmac_sha_total_msg_length += hmac_sha_ctl->data_length;
	sha_state->hmac_sha_init_done = 1;
	hmac_ctx->total_count = sha_state->hmac_sha_total_msg_length;
	hmac_ctx->token_id = hmac_sha_ctl->token_id;

	if (hmac_sha_ctl->is_first == 1)
		hmac_ctx->is_first = true;
	else
		hmac_ctx->is_first = false;

	ret = wait_async_op(&hmac_sha_complete, crypto_ahash_init(req));
	if (ret) {
		CRYPTODEV_ERR("%s(): Failed to initialize ahash: %d\n", __func__, ret);
			sha_state->hmac_sha_init_done = 0;
			sha_state->hmac_sha_total_msg_length = 0UL;
		goto free_req;
	}

	hmac_ctx->user_src_buf = hmac_sha_ctl->src_buffer;
	hmac_ctx->user_digest_buffer = hmac_sha_ctl->digest_buffer;

	if (hmac_sha_ctl->is_last == 0) {
		ret = wait_async_op(&hmac_sha_complete, crypto_ahash_update(req));
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed to ahash_update: %d\n", __func__, ret);
			sha_state->hmac_sha_init_done = 0;
			sha_state->hmac_sha_total_msg_length = 0UL;
			goto free_req;
		}
	} else {
		ret = wait_async_op(&hmac_sha_complete, crypto_ahash_finup(req));
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed to ahash_finup: %d\n", __func__, ret);
			sha_state->hmac_sha_init_done = 0;
			sha_state->hmac_sha_total_msg_length = 0UL;
			goto free_req;
		}

		if (hmac_sha_ctl->hmac_sha_type == TEGRA_NVVSE_HMAC_SHA_VERIFY)
			hmac_sha_ctl->result = hmac_ctx->result;

		sha_state->hmac_sha_init_done = 0;
		sha_state->hmac_sha_total_msg_length = 0;
	}

free_req:
	if (req)
		ahash_request_free(req);
free_tfm:
	if (tfm)
		crypto_free_ahash(tfm);

exit:
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
	const char *driver_name;
	struct ahash_request *req;
	struct tnvvse_crypto_completion sha_complete;
	struct tegra_virtual_se_aes_cmac_context *cmac_ctx;
	char key_as_keyslot[AES_KEYSLOT_NAME_SIZE] = {0,};
	int ret = -ENOMEM;

	if (aes_cmac_ctl->data_length > ivc_database.max_buffer_size[ctx->node_id]) {
		CRYPTODEV_ERR("%s(): Input size is (data = %d) is not supported\n",
					__func__, aes_cmac_ctl->data_length);
		return -EINVAL;
	}

	if (aes_cmac_ctl->cmac_type != TEGRA_NVVSE_AES_CMAC_SIGN &&
		aes_cmac_ctl->cmac_type != TEGRA_NVVSE_AES_CMAC_VERIFY) {
		CRYPTODEV_ERR("%s: Invalid value for AES CMAC operations\n", __func__);
		return -EINVAL;
	}

	tfm = crypto_alloc_ahash("cmac-tsec(aes)", 0, 0);
	if (IS_ERR(tfm)) {
		ret = PTR_ERR(tfm);
		CRYPTODEV_ERR("%s(): Failed to allocate ahash for cmac-tsec(aes): %d\n", __func__, ret);
		goto out;
	}

	cmac_ctx = crypto_ahash_ctx(tfm);
	cmac_ctx->node_id = ctx->node_id;

	driver_name = crypto_tfm_alg_driver_name(crypto_ahash_tfm(tfm));
	if (driver_name == NULL) {
		CRYPTODEV_ERR("%s(): get_driver_name for cmac-tsec(aes) returned NULL", __func__);
		ret = -EINVAL;
		goto free_tfm;
	}

	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		CRYPTODEV_ERR("%s(): Failed to allocate request for cmac-tsec(aes)\n", __func__);
		goto free_tfm;
	}

	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   tnvvse_crypto_complete, &sha_complete);

	init_completion(&sha_complete.restart);
	sha_complete.req_err = 0;

	crypto_ahash_clear_flags(tfm, ~0U);

	if (aes_cmac_ctl->cmac_type == TEGRA_NVVSE_AES_CMAC_SIGN)
		cmac_ctx->request_type = TEGRA_HV_VSE_CMAC_SIGN;
	else
		cmac_ctx->request_type = TEGRA_HV_VSE_CMAC_VERIFY;

	ret = snprintf(key_as_keyslot, AES_KEYSLOT_NAME_SIZE, "NVSEAES ");
	memcpy(key_as_keyslot + KEYSLOT_OFFSET_BYTES, aes_cmac_ctl->key_slot, KEYSLOT_SIZE_BYTES);

	cmac_ctx->result = 0;
	ret = crypto_ahash_setkey(tfm, key_as_keyslot, KEYSLOT_SIZE_BYTES);
	if (ret) {
		CRYPTODEV_ERR("%s(): Failed to set keys for cmac-tsec(aes): %d\n", __func__, ret);
		ret = -EINVAL;
		goto free_req;
	}

	ret = wait_async_op(&sha_complete, crypto_ahash_init(req));
	if (ret) {
		CRYPTODEV_ERR("%s(): Failed to initialize ahash: %d\n", __func__, ret);
		ret = -EINVAL;
		goto free_req;
	}

	cmac_ctx->user_src_buf_size = aes_cmac_ctl->data_length;
	cmac_ctx->user_src_buf = aes_cmac_ctl->src_buffer;
	cmac_ctx->user_mac_buf = aes_cmac_ctl->cmac_buffer;

	if (cmac_ctx->user_src_buf_size > ivc_database.max_buffer_size[ctx->node_id]) {
		CRYPTODEV_ERR("%s(): Unsupported buffer size: %u\n",
			__func__, cmac_ctx->user_src_buf_size);
		ret = -EINVAL;
		goto free_req;
	}

	ret = wait_async_op(&sha_complete, crypto_ahash_finup(req));
	if (ret) {
		CRYPTODEV_ERR("%s(): Failed to ahash_finup: %d\n", __func__, ret);
		goto free_req;
	}

	if (aes_cmac_ctl->cmac_type == TEGRA_NVVSE_AES_CMAC_VERIFY)
		aes_cmac_ctl->result = cmac_ctx->result;

free_req:
	ahash_request_free(req);
free_tfm:
	crypto_free_ahash(tfm);
out:
	return ret;
}

static int tnvvse_crypto_aes_cmac_sign_verify(struct tnvvse_crypto_ctx *ctx,
				struct tegra_nvvse_aes_cmac_sign_verify_ctl *aes_cmac_ctl)
{
	struct crypto_ahash *tfm;
	const char *driver_name;
	struct ahash_request *req;
	struct tnvvse_crypto_completion sha_complete;
	struct tegra_virtual_se_aes_cmac_context *cmac_ctx;
	char key_as_keyslot[AES_KEYSLOT_NAME_SIZE] = {0,};
	int ret = -ENOMEM;

	if (aes_cmac_ctl->data_length > ivc_database.max_buffer_size[ctx->node_id]) {
		CRYPTODEV_ERR("%s(): Input size is (data = %d) is not supported\n",
					__func__, aes_cmac_ctl->data_length);
		return -EINVAL;
	}

	if (aes_cmac_ctl->cmac_type != TEGRA_NVVSE_AES_CMAC_SIGN &&
		aes_cmac_ctl->cmac_type != TEGRA_NVVSE_AES_CMAC_VERIFY) {
		CRYPTODEV_ERR("%s: Invalid value for AES CMAC operations\n", __func__);
		return -EINVAL;
	}

	tfm = crypto_alloc_ahash("cmac-vse(aes)", 0, 0);
	if (IS_ERR(tfm)) {
		ret = PTR_ERR(tfm);
		CRYPTODEV_ERR("%s(): Failed to allocate ahash for cmac-vse(aes): %d\n", __func__, ret);
		return ret;
	}

	cmac_ctx = crypto_ahash_ctx(tfm);
	cmac_ctx->node_id = ctx->node_id;
	cmac_ctx->b_is_sm4 = aes_cmac_ctl->is_SM4;

	driver_name = crypto_tfm_alg_driver_name(crypto_ahash_tfm(tfm));
	if (driver_name == NULL) {
		CRYPTODEV_ERR("%s(): Failed to get_driver_name for cmac-vse(aes) returned NULL",
		__func__);
		goto free_tfm;
	}

	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		CRYPTODEV_ERR("%s(): Failed to allocate request for cmac-vse(aes)\n", __func__);
		goto free_tfm;
	}

	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   tnvvse_crypto_complete, &sha_complete);

	cmac_ctx->user_src_buf_size = aes_cmac_ctl->data_length;

	init_completion(&sha_complete.restart);
	sha_complete.req_err = 0;

	crypto_ahash_clear_flags(tfm, ~0U);

	if (aes_cmac_ctl->cmac_type == TEGRA_NVVSE_AES_CMAC_SIGN)
		cmac_ctx->request_type = TEGRA_HV_VSE_CMAC_SIGN;
	else
		cmac_ctx->request_type = TEGRA_HV_VSE_CMAC_VERIFY;

	ret = snprintf(key_as_keyslot, AES_KEYSLOT_NAME_SIZE, "NVSEAES ");
	memcpy(key_as_keyslot + KEYSLOT_OFFSET_BYTES, aes_cmac_ctl->key_slot, KEYSLOT_SIZE_BYTES);
	cmac_ctx->token_id = aes_cmac_ctl->token_id;
	cmac_ctx->result = 0;
	ret = crypto_ahash_setkey(tfm, key_as_keyslot, KEYSLOT_SIZE_BYTES);
	if (ret) {
		CRYPTODEV_ERR("%s(): Failed to set keys for cmac-vse(aes): %d\n", __func__, ret);
		goto free_req;
	}

	ret = wait_async_op(&sha_complete, crypto_ahash_init(req));
	if (ret) {
		CRYPTODEV_ERR("%s(): Failed to initialize ahash: %d\n", __func__, ret);
		goto free_req;
	}

	cmac_ctx->user_src_buf = aes_cmac_ctl->src_buffer;
	cmac_ctx->user_mac_buf = aes_cmac_ctl->cmac_buffer;

	ret = wait_async_op(&sha_complete, crypto_ahash_finup(req));
	if (ret) {
		CRYPTODEV_ERR("%s(): Failed to ahash_finup: %d\n", __func__, ret);
		goto free_req;
	}

	if (aes_cmac_ctl->cmac_type == TEGRA_NVVSE_AES_CMAC_VERIFY)
		aes_cmac_ctl->result = cmac_ctx->result;

free_req:
	ahash_request_free(req);

free_tfm:
	crypto_free_ahash(tfm);

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
	int ret = -ENOMEM;

	tfm = crypto_alloc_ahash("gmac-vse(aes)", 0, 0);
	if (IS_ERR(tfm)) {
		CRYPTODEV_ERR("%s(): Failed to allocate transform for gmac-vse(aes):%ld\n", __func__,
						PTR_ERR(tfm));
		ret = PTR_ERR(tfm);
		goto out;
	}

	gmac_ctx = crypto_ahash_ctx(tfm);
	gmac_ctx->node_id = ctx->node_id;

	driver_name = crypto_tfm_alg_driver_name(crypto_ahash_tfm(tfm));
	if (driver_name == NULL) {
		CRYPTODEV_ERR("%s(): Failed to get driver name\n", __func__);
		goto free_tfm;
	}
	pr_debug("%s(): Algo name gmac-vse(aes), driver name %s\n", __func__, driver_name);

	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		CRYPTODEV_ERR("%s(): Failed to allocate request for gmac-vse(aes)\n", __func__);
		goto free_tfm;
	}

	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   tnvvse_crypto_complete, &sha_state->sha_complete);

	init_completion(&sha_state->sha_complete.restart);
	sha_state->sha_complete.req_err = 0;

	ret = snprintf(key_as_keyslot, AES_KEYSLOT_NAME_SIZE, "NVSEAES ");
	memcpy(key_as_keyslot + KEYSLOT_OFFSET_BYTES, gmac_init_ctl->key_slot, KEYSLOT_SIZE_BYTES);

	ret = crypto_ahash_setkey(tfm, key_as_keyslot, KEYSLOT_SIZE_BYTES);
	if (ret) {
		CRYPTODEV_ERR("%s(): Failed to set keys for gmac-vse(aes): %d\n", __func__, ret);
		goto free_req;
	}

	memset(iv, 0, TEGRA_NVVSE_AES_GCM_IV_LEN);
	gmac_ctx->request_type = TEGRA_HV_VSE_GMAC_INIT;
	gmac_ctx->key_instance_idx = gmac_init_ctl->key_instance_idx;
	gmac_ctx->iv = iv;

	ret = wait_async_op(&sha_state->sha_complete, crypto_ahash_init(req));
	if (ret) {
		CRYPTODEV_ERR("%s(): Failed to ahash_init for gmac-vse(aes): ret=%d\n",
					__func__, ret);
	}

	memcpy(gmac_init_ctl->IV, gmac_ctx->iv, TEGRA_NVVSE_AES_GCM_IV_LEN);

free_req:
	ahash_request_free(req);
free_tfm:
	crypto_free_ahash(tfm);
out:
	return ret;
}

static int tnvvse_crypto_aes_gmac_sign_verify_init(struct tnvvse_crypto_ctx *ctx,
		struct tegra_nvvse_aes_gmac_sign_verify_ctl *gmac_sign_verify_ctl,
		struct ahash_request *req)
{
	struct crypto_sha_state *sha_state = &ctx->sha_state;
	struct tegra_virtual_se_aes_gmac_context *gmac_ctx;
	char key_as_keyslot[AES_KEYSLOT_NAME_SIZE] = {0,};
	struct crypto_ahash *tfm;
	const char *driver_name;
	int ret = -EINVAL;

	if (!req) {
		CRYPTODEV_ERR("%s AES-GMAC request not valid\n", __func__);
		return -EINVAL;
	}

	tfm = crypto_ahash_reqtfm(req);
	if (!tfm) {
		CRYPTODEV_ERR("%s AES-GMAC transform not valid\n", __func__);
		return -EINVAL;
	}

	gmac_ctx = crypto_ahash_ctx(tfm);
	gmac_ctx->node_id = ctx->node_id;
	gmac_ctx->b_is_sm4 = gmac_sign_verify_ctl->b_is_sm4;
	gmac_ctx->release_key_flag = gmac_sign_verify_ctl->release_key_flag;
	gmac_ctx->key_instance_idx = gmac_sign_verify_ctl->key_instance_idx;

	driver_name = crypto_tfm_alg_driver_name(crypto_ahash_tfm(tfm));
	if (driver_name == NULL) {
		CRYPTODEV_ERR("%s(): Failed to get driver name\n", __func__);
		goto out;
	}
	pr_debug("%s(): Algo name gmac-vse(aes), driver name %s\n", __func__, driver_name);

	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   tnvvse_crypto_complete, &sha_state->sha_complete);

	init_completion(&sha_state->sha_complete.restart);
	sha_state->sha_complete.req_err = 0;

	ret = snprintf(key_as_keyslot, AES_KEYSLOT_NAME_SIZE, "NVSEAES ");
	memcpy(key_as_keyslot + KEYSLOT_OFFSET_BYTES, gmac_sign_verify_ctl->key_slot,
		KEYSLOT_SIZE_BYTES);
	ret = crypto_ahash_setkey(tfm, key_as_keyslot, KEYSLOT_SIZE_BYTES);
	if (ret) {
		CRYPTODEV_ERR("%s(): Failed to set keys for gmac-vse(aes): %d\n", __func__, ret);
		goto out;
	}

	if (gmac_sign_verify_ctl->gmac_type == TEGRA_NVVSE_AES_GMAC_SIGN)
		gmac_ctx->request_type = TEGRA_HV_VSE_GMAC_SIGN;
	else
		gmac_ctx->request_type = TEGRA_HV_VSE_GMAC_VERIFY;

	ret = wait_async_op(&sha_state->sha_complete, crypto_ahash_init(req));
	if (ret) {
		CRYPTODEV_ERR("%s(): Failed to ahash_init for gmac-vse(aes): ret=%d\n",
					__func__, ret);
		goto out;
	}

	sha_state->req = req;
	sha_state->tfm = tfm;
	sha_state->result_buff = ctx->sha_result;

	memset(sha_state->result_buff, 0, TEGRA_NVVSE_AES_GCM_TAG_SIZE);

	ret = 0;

out:
	return ret;
}

static int tnvvse_crypto_aes_gmac_sign_verify(struct tnvvse_crypto_ctx *ctx,
		struct tegra_nvvse_aes_gmac_sign_verify_ctl *gmac_sign_verify_ctl)
{
	struct crypto_sha_state *sha_state = &ctx->sha_state;
	struct tegra_virtual_se_aes_gmac_context *gmac_ctx;
	struct crypto_ahash *tfm;
	uint8_t iv[TEGRA_NVVSE_AES_GCM_IV_LEN];
	struct ahash_request *req = NULL;
	int ret = -EINVAL;

	if (ctx->is_zero_copy_node) {
		if (gmac_sign_verify_ctl->b_is_zero_copy == 0U) {
			CRYPTODEV_ERR("%s(): only zero copy operation is supported on this node\n",
									__func__);
			ret = -EINVAL;
			goto done;
		}
	} else {
		if (gmac_sign_verify_ctl->b_is_zero_copy != 0U) {
			CRYPTODEV_ERR("%s(): zero copy operation is not supported on this node\n",
									__func__);
			ret = -EINVAL;
			goto done;
		}
	}

	tfm = crypto_alloc_ahash("gmac-vse(aes)", 0, 0);
	if (IS_ERR(tfm)) {
		CRYPTODEV_ERR("%s(): Failed to load transform for gmac-vse(aes):%ld\n", __func__,
							PTR_ERR(tfm));
		ret = PTR_ERR(tfm);
		goto done;
	}

	gmac_ctx = crypto_ahash_ctx(tfm);
	gmac_ctx->node_id = ctx->node_id;
	gmac_ctx->b_is_sm4 = gmac_sign_verify_ctl->b_is_sm4;

	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		CRYPTODEV_ERR("%s(): Failed to allocate request for gmac-vse(aes)\n", __func__);
		goto free_tfm;
	}

	gmac_ctx->user_aad_buf_size = gmac_sign_verify_ctl->data_length;
	if (ctx->is_zero_copy_node) {
		gmac_ctx->user_aad_iova = gmac_sign_verify_ctl->src_buffer_iova;
	} else {
		gmac_ctx->user_aad_buf = gmac_sign_verify_ctl->src_buffer;
	}

	if ((gmac_sign_verify_ctl->is_last) &&
			(gmac_sign_verify_ctl->tag_length != TEGRA_NVVSE_AES_GCM_TAG_SIZE)) {
		CRYPTODEV_ERR("%s(): Failed due to invalid tag length (%d) invalid", __func__,
					gmac_sign_verify_ctl->tag_length);
		goto free_req;
	}

	if ((gmac_sign_verify_ctl->gmac_type != TEGRA_NVVSE_AES_GMAC_SIGN) &&
		(gmac_sign_verify_ctl->gmac_type != TEGRA_NVVSE_AES_GMAC_VERIFY)) {
		CRYPTODEV_ERR("%s: Invalid request type\n", __func__);
		ret = -EINVAL;
		goto done;
	}

	ret = tnvvse_crypto_aes_gmac_sign_verify_init(ctx, gmac_sign_verify_ctl, req);
	if (ret) {
		CRYPTODEV_ERR("%s(): Failed to init: %d\n", __func__, ret);
		goto free_req;
	}

	gmac_ctx->release_key_flag = gmac_sign_verify_ctl->release_key_flag;
	gmac_ctx->key_instance_idx = gmac_sign_verify_ctl->key_instance_idx;

	if (gmac_sign_verify_ctl->gmac_type == TEGRA_NVVSE_AES_GMAC_SIGN)
		gmac_ctx->request_type = TEGRA_HV_VSE_GMAC_SIGN;
	else
		gmac_ctx->request_type = TEGRA_HV_VSE_GMAC_VERIFY;
	gmac_ctx->iv = NULL;
	gmac_ctx->is_first = (gmac_sign_verify_ctl->is_first != 0);

	if (gmac_sign_verify_ctl->is_last == 0) {
		ret = wait_async_op(&sha_state->sha_complete,
				crypto_ahash_update(req));
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed to ahash_update for gmac-vse(aes): %d\n",
					__func__, ret);
			goto free_req;
		}
	} else {
		if (gmac_sign_verify_ctl->gmac_type == TEGRA_NVVSE_AES_GMAC_SIGN) {
			if (ctx->is_zero_copy_node)
				gmac_ctx->user_tag_iova = gmac_sign_verify_ctl->tag_buffer_iova;
			else
				gmac_ctx->user_tag_buf = gmac_sign_verify_ctl->tag_buffer;
		} else {
			gmac_ctx->user_tag_buf = gmac_sign_verify_ctl->tag_buffer;
			memcpy(iv, gmac_sign_verify_ctl->initial_vector,
				TEGRA_NVVSE_AES_GCM_IV_LEN);
			gmac_ctx->iv = iv;
		}

		ret = wait_async_op(&sha_state->sha_complete,
				crypto_ahash_finup(req));
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed to ahash_finup for gmac-vse(aes): %d\n",
					__func__, ret);
			goto free_req;
		}
	}

	if (gmac_sign_verify_ctl->release_key_flag)
		ctx->allocated_key_slot_count -= 1U;

	if (gmac_sign_verify_ctl->is_last) {
		if (gmac_sign_verify_ctl->gmac_type == TEGRA_NVVSE_AES_GMAC_VERIFY)
			gmac_sign_verify_ctl->result = gmac_ctx->result;
	}

free_req:
	ahash_request_free(req);

free_tfm:
	crypto_free_ahash(tfm);

done:
	return ret;
}

static int tnvvse_crypto_validate_aes_enc_dec_params(struct tnvvse_crypto_ctx *ctx,
					struct tegra_nvvse_aes_enc_dec_ctl *aes_enc_dec_ctl)
{
	int ret = 0;

	if (aes_enc_dec_ctl->aes_mode < 0 || aes_enc_dec_ctl->aes_mode >= TEGRA_NVVSE_AES_MODE_MAX) {
		CRYPTODEV_ERR("%s(): The requested AES ENC/DEC (%d) is not supported\n",
		__func__, aes_enc_dec_ctl->aes_mode);
		ret = -EINVAL;
		goto out;
	}

	if ((aes_enc_dec_ctl->data_length != 0U && aes_enc_dec_ctl->src_buffer == NULL) ||
		aes_enc_dec_ctl->dest_buffer == NULL) {
		CRYPTODEV_ERR("%s(): pSrcBuffer or pDstBuffer is null for AES Encrypt Decrypt",
		__func__);
		ret = -EINVAL;
		goto out;
	}

	if (aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_GCM) {
		if (aes_enc_dec_ctl->tag_buffer == NULL) {
			CRYPTODEV_ERR("%s(): pTagBuffer can't be NULL for AES-GCM", __func__);
			ret = -EINVAL;
			goto out;
		}
		if ((aes_enc_dec_ctl->aad_length != 0U) && (aes_enc_dec_ctl->aad_buffer == NULL)) {
			CRYPTODEV_ERR("%s(): pAadBuffer can't be NULL for uAadLength as non-zero",
			__func__);
			ret = -EINVAL;
			goto out;
		}
	}

	if (aes_enc_dec_ctl->data_length > ivc_database.max_buffer_size[ctx->node_id]) {
		CRYPTODEV_ERR("%s(): Input size is (data = %d) is not supported\n",
					__func__, aes_enc_dec_ctl->data_length);
		ret = -EINVAL;
		goto out;
	}

out:
	return ret;
}

static int tnvvse_crypto_aes_enc_dec(struct tnvvse_crypto_ctx *ctx,
					struct tegra_nvvse_aes_enc_dec_ctl *aes_enc_dec_ctl)
{
	struct crypto_skcipher *tfm;
	struct skcipher_request *req = NULL;
	int ret = 0;
	uint64_t err = 0;
	struct tnvvse_crypto_completion tcrypt_complete;
	struct tegra_virtual_se_aes_context *aes_ctx;
	char aes_algo[5][20] = {"cbc-vse(aes)", "ctr-vse(aes)", "gcm-vse(aes)", "cbc-vse(aes)",
				"ctr-vse(aes)"};
	const char *driver_name;
	char key_as_keyslot[AES_KEYSLOT_NAME_SIZE] = {0,};

	ret = tnvvse_crypto_validate_aes_enc_dec_params(ctx, aes_enc_dec_ctl);
	if (ret) {
		CRYPTODEV_ERR("%s(): Failed to validate params: %d\n", __func__, ret);
		goto out;
	}

	tfm = crypto_alloc_skcipher(aes_algo[aes_enc_dec_ctl->aes_mode],
						CRYPTO_ALG_TYPE_SKCIPHER | CRYPTO_ALG_ASYNC, 0);
	if (IS_ERR(tfm)) {
		CRYPTODEV_ERR("%s(): Failed to load transform for %s: %ld\n",
					__func__, aes_algo[aes_enc_dec_ctl->aes_mode], PTR_ERR(tfm));
		ret = PTR_ERR(tfm);
		goto out;
	}

	aes_ctx = crypto_skcipher_ctx(tfm);
	aes_ctx->node_id = ctx->node_id;
	aes_ctx->user_nonce = aes_enc_dec_ctl->user_nonce;
	aes_ctx->release_key_flag = aes_enc_dec_ctl->release_key_flag;

	if (aes_enc_dec_ctl->is_non_first_call != 0U)
		aes_ctx->b_is_first = 0U;
	else {
		aes_ctx->b_is_first = 1U;
		memset(ctx->intermediate_counter, 0, TEGRA_NVVSE_AES_IV_LEN);
	}

	req = skcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		CRYPTODEV_ERR("%s(): Failed to allocate skcipher request\n", __func__);
		ret = -ENOMEM;
		goto free_tfm;
	}

	driver_name = crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm));
	if (driver_name == NULL) {
		CRYPTODEV_ERR("%s(): Failed to get driver name for %s\n", __func__,
						aes_algo[aes_enc_dec_ctl->aes_mode]);
		goto free_req;
	}
	pr_debug("%s(): The skcipher driver name is %s for %s\n",
				__func__, driver_name, aes_algo[aes_enc_dec_ctl->aes_mode]);

	crypto_skcipher_clear_flags(tfm, ~0U);

	ret = snprintf(key_as_keyslot, AES_KEYSLOT_NAME_SIZE, "NVSEAES ");
	memcpy(key_as_keyslot + KEYSLOT_OFFSET_BYTES, aes_enc_dec_ctl->key_slot,
		KEYSLOT_SIZE_BYTES);

	/* Null key is only allowed in SE driver */
	if (!strstr(driver_name, "tegra")) {
		ret = -EINVAL;
		CRYPTODEV_ERR("%s(): Failed to identify as tegra se driver\n", __func__);
		goto free_req;
	}

	ret = crypto_skcipher_setkey(tfm, key_as_keyslot, KEYSLOT_SIZE_BYTES);
	if (ret < 0) {
		CRYPTODEV_ERR("%s(): Failed to set key: %d\n", __func__, ret);
		goto free_req;
	}

	aes_ctx->user_src_buf_size = aes_enc_dec_ctl->data_length;
	aes_ctx->key_instance_idx = aes_enc_dec_ctl->key_instance_idx;
	init_completion(&tcrypt_complete.restart);

	skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
					tnvvse_crypto_complete, &tcrypt_complete);

	if (aes_ctx->b_is_first == 1U || !aes_enc_dec_ctl->is_encryption) {
		if ((aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_CBC) ||
			(aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_SM4_CBC))
			memcpy(aes_ctx->iv, aes_enc_dec_ctl->initial_vector,
				TEGRA_NVVSE_AES_IV_LEN);
		else if ((aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_CTR) ||
			(aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_SM4_CTR))
			memcpy(aes_ctx->iv, aes_enc_dec_ctl->initial_counter,
				TEGRA_NVVSE_AES_CTR_LEN);
		else
			memset(aes_ctx->iv, 0, TEGRA_NVVSE_AES_IV_LEN);
	} else {
		if ((aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_CTR) ||
			(aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_SM4_CTR))
			memcpy(aes_ctx->iv, ctx->intermediate_counter,
				TEGRA_NVVSE_AES_CTR_LEN);
		else		//As CBC uses IV stored in SE server
			memset(aes_ctx->iv, 0, TEGRA_NVVSE_AES_IV_LEN);
	}
	pr_debug("%s(): %scryption\n", __func__, (aes_enc_dec_ctl->is_encryption ? "en" : "de"));

	if ((aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_SM4_CBC) ||
		(aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_SM4_CTR))
		aes_ctx->b_is_sm4 = 1U;
	else
		aes_ctx->b_is_sm4 = 0U;

	aes_ctx->user_src_buf = aes_enc_dec_ctl->src_buffer;
	aes_ctx->user_dst_buf = aes_enc_dec_ctl->dest_buffer;

	reinit_completion(&tcrypt_complete.restart);
	skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
			tnvvse_crypto_complete, &tcrypt_complete);
	tcrypt_complete.req_err = 0;

	/* Set first byte of aes_ctx->iv to 1 for first encryption request and 0 for other
	 * encryption requests. This is used to invoke generation of random IV.
	 * If userNonce is not provided random IV generation is needed.
	 */
	if (aes_enc_dec_ctl->is_encryption && (aes_enc_dec_ctl->user_nonce == 0U)) {
		if (!aes_enc_dec_ctl->is_non_first_call)
			aes_ctx->iv[0] = 1;
		else
			aes_ctx->iv[0] = 0;
	}
	ret = aes_enc_dec_ctl->is_encryption ? crypto_skcipher_encrypt(req) :
		crypto_skcipher_decrypt(req);
	if ((ret == -EINPROGRESS) || (ret == -EBUSY)) {
		/* crypto driver is asynchronous */
		err = wait_for_completion_timeout(&tcrypt_complete.restart,
				msecs_to_jiffies(5000));
		if (err == 0) {
			ret = -ETIMEDOUT;
			goto free_req;
		}

		if (tcrypt_complete.req_err < 0) {
			ret = tcrypt_complete.req_err;
			goto free_req;
		}
	} else if (ret < 0) {
		CRYPTODEV_ERR("%s(): Failed to %scrypt: %d\n",
				__func__, aes_enc_dec_ctl->is_encryption ? "en" : "de", ret);
		goto free_req;
	}

	if ((aes_enc_dec_ctl->is_encryption) &&	(aes_enc_dec_ctl->user_nonce == 0U)) {
		if ((aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_CBC) ||
			(aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_SM4_CBC))
			memcpy(aes_enc_dec_ctl->initial_vector, aes_ctx->iv,
				TEGRA_NVVSE_AES_IV_LEN);
		else if ((aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_CTR) ||
			(aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_SM4_CTR))
			memcpy(aes_enc_dec_ctl->initial_counter, aes_ctx->iv,
				TEGRA_NVVSE_AES_CTR_LEN);
	}

	if (aes_enc_dec_ctl->user_nonce == 1U) {
		if (aes_enc_dec_ctl->is_encryption != 0U &&
				aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_CTR) {
			ret = update_counter(&aes_ctx->iv[0], aes_enc_dec_ctl->data_length >> 4U);
			if (ret) {
				CRYPTODEV_ERR("%s(): Failed to update counter: %d\n",
						__func__, ret);
				goto free_req;
			}

			memcpy(ctx->intermediate_counter, &aes_ctx->iv[0],
					TEGRA_NVVSE_AES_CTR_LEN);
		}
	}

	if (aes_enc_dec_ctl->release_key_flag && ctx->allocated_key_slot_count > 0U)
		ctx->allocated_key_slot_count -= 1U;

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
	int32_t ret = 0;
	uint64_t err = 0;
	struct tnvvse_crypto_completion tcrypt_complete;
	struct tegra_virtual_se_aes_context *aes_ctx;
	const char *driver_name;
	char key_as_keyslot[AES_KEYSLOT_NAME_SIZE] = {0,};
	bool enc;

	if (aes_enc_dec_ctl->aes_mode != TEGRA_NVVSE_AES_MODE_GCM) {
		CRYPTODEV_ERR("%s(): The requested AES ENC/DEC (%d) is not supported\n",
					__func__, aes_enc_dec_ctl->aes_mode);
		ret = -EINVAL;
		goto out;
	}

	if (aes_enc_dec_ctl->data_length > ivc_database.max_buffer_size[ctx->node_id]
			|| aes_enc_dec_ctl->aad_length > ivc_database.max_buffer_size[ctx->node_id] ) {
		CRYPTODEV_ERR("%s(): Input size is (data = %d, aad = %d) is not supported\n",
					__func__, aes_enc_dec_ctl->data_length,
					aes_enc_dec_ctl->aad_length);
		ret = -EINVAL;
		goto out;
	}

	tfm = crypto_alloc_aead("gcm-vse(aes)", CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_ASYNC, 0);
	if (IS_ERR(tfm)) {
		CRYPTODEV_ERR("%s(): Failed to load transform for gcm-vse(aes): %ld\n",
					__func__, PTR_ERR(tfm));
		ret = PTR_ERR(tfm);
		goto out;
	}

	aes_ctx = crypto_aead_ctx(tfm);
	aes_ctx->node_id = ctx->node_id;
	aes_ctx->user_nonce = aes_enc_dec_ctl->user_nonce;

	req = aead_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		CRYPTODEV_ERR("%s(): Failed to allocate skcipher request\n", __func__);
		ret = -ENOMEM;
		goto free_tfm;
	}

	driver_name = crypto_tfm_alg_driver_name(crypto_aead_tfm(tfm));
	if (driver_name == NULL) {
		CRYPTODEV_ERR("%s(): Failed to get driver name for gcm-vse(aes)\n", __func__);
		goto free_req;
	}
	pr_debug("%s(): The aead driver name is %s for gcm-vse(aes)\n",
						__func__, driver_name);

	if (aes_enc_dec_ctl->tag_length != TEGRA_NVVSE_AES_GCM_TAG_SIZE) {
		ret = -EINVAL;
		CRYPTODEV_ERR("%s(): crypt_req taglen(%d) invalid",
		__func__, aes_enc_dec_ctl->tag_length);
		goto free_req;
	}

	crypto_aead_clear_flags(tfm, ~0U);

	ret = snprintf(key_as_keyslot, AES_KEYSLOT_NAME_SIZE, "NVSEAES ");
	memcpy(key_as_keyslot + KEYSLOT_OFFSET_BYTES, aes_enc_dec_ctl->key_slot,
		KEYSLOT_SIZE_BYTES);

	ret = crypto_aead_setkey(tfm, key_as_keyslot, KEYSLOT_SIZE_BYTES);
	if (ret < 0) {
		CRYPTODEV_ERR("%s(): Failed to set key: %d\n", __func__, ret);
		goto free_req;
	}

	ret = crypto_aead_setauthsize(tfm, aes_enc_dec_ctl->tag_length);
	if (ret < 0) {
		CRYPTODEV_ERR("%s(): Failed to set tag size: %d\n", __func__, ret);
		goto free_req;
	}

	init_completion(&tcrypt_complete.restart);
	tcrypt_complete.req_err = 0;

	enc = aes_enc_dec_ctl->is_encryption;

	aead_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
					tnvvse_crypto_complete, &tcrypt_complete);
	aead_request_set_ad(req, aes_enc_dec_ctl->aad_length);

	memset(aes_ctx->iv, 0, TEGRA_NVVSE_AES_GCM_IV_LEN);
	if (!enc || aes_enc_dec_ctl->user_nonce != 0U)
		memcpy(aes_ctx->iv, aes_enc_dec_ctl->initial_vector, TEGRA_NVVSE_AES_GCM_IV_LEN);
	else if (enc && !aes_enc_dec_ctl->is_non_first_call)
		/* Set first byte of iv to 1 for first encryption request. This is used to invoke
		 * generation of random IV.
		 * If userNonce is not provided random IV generation is needed.
		 */
		aes_ctx->iv[0] = 1;

	aes_ctx->user_src_buf_size = aes_enc_dec_ctl->data_length;
	aes_ctx->user_tag_buf_size = aes_enc_dec_ctl->tag_length;
	aes_ctx->user_aad_buf_size = aes_enc_dec_ctl->aad_length;

	aes_ctx->user_aad_buf = aes_enc_dec_ctl->aad_buffer;
	aes_ctx->user_src_buf = aes_enc_dec_ctl->src_buffer;
	aes_ctx->user_tag_buf = aes_enc_dec_ctl->tag_buffer;
	aes_ctx->user_dst_buf = aes_enc_dec_ctl->dest_buffer;
	aes_ctx->token_id = aes_enc_dec_ctl->token_id;
	/* this field is unused by VSE driver and is being set only to pass the validation
	 * check in crypto_aead_decrypt.
	 */
	req->cryptlen = crypto_aead_authsize(tfm);

	ret =  enc ? crypto_aead_encrypt(req) : crypto_aead_decrypt(req);
	if ((ret == -EINPROGRESS) || (ret == -EBUSY)) {
		/* crypto driver is asynchronous */
		err = wait_for_completion_timeout(&tcrypt_complete.restart,
					msecs_to_jiffies(5000));
		if (err == 0) {
			ret = -ETIMEDOUT;
			goto free_req;
		}

		if (tcrypt_complete.req_err < 0) {
			ret = tcrypt_complete.req_err;
			goto free_req;
		}
	} else if (ret < 0) {
		CRYPTODEV_ERR("%s(): Failed to %scrypt: %d\n",
				__func__, enc ? "en" : "de", ret);
		goto free_req;
	}

	if (enc) {
		if (aes_enc_dec_ctl->user_nonce == 0U)
			memcpy(aes_enc_dec_ctl->initial_vector, aes_ctx->iv,
					TEGRA_NVVSE_AES_GCM_IV_LEN);
	}

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
	uint64_t err = 0;

	if ((aes_drng_ctl->data_length > ctx->max_rng_buff) ||
		(aes_drng_ctl->data_length == 0U)) {
		CRYPTODEV_ERR("%s(): unsupported data length(%u)\n",
		__func__, aes_drng_ctl->data_length);
		ret = -EINVAL;
		goto out;
	}

	rng = crypto_alloc_rng("rng_drbg", 0, 0);
	if (IS_ERR(rng)) {
		ret = PTR_ERR(rng);
		CRYPTODEV_ERR("(%s(): Failed to allocate crypto for rng_dbg, %d\n",
		__func__, ret);
		goto out;
	}

	rng_ctx = crypto_rng_ctx(rng);
	rng_ctx->node_id = ctx->node_id;

	memset(ctx->rng_buff, 0, ctx->max_rng_buff);
	ret = crypto_rng_get_bytes(rng, ctx->rng_buff, aes_drng_ctl->data_length);
	if (ret < 0) {
		CRYPTODEV_ERR("%s(): Failed to obtain correct amount of random data for (req %d), %d\n",
		__func__, aes_drng_ctl->data_length, ret);
		goto free_rng;
	}

	err = copy_to_user((void __user *)aes_drng_ctl->dest_buff,
				(const void *)ctx->rng_buff, aes_drng_ctl->data_length);
	if (err) {
		CRYPTODEV_ERR("%s(): Failed to copy_to_user for length %d\n",
				__func__, aes_drng_ctl->data_length);
		ret = -EFAULT;
	} else
		ret = 0;

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
		get_ivc_db->se_engine[i] = hv_vse_db[i].engine_id;
		get_ivc_db->node_id[i] = hv_vse_db[i].instance_id;
		get_ivc_db->priority[i] = hv_vse_db[i].priority;
		get_ivc_db->max_buffer_size[i] = hv_vse_db[i].max_buffer_size;
		get_ivc_db->channel_grp_id[i] = hv_vse_db[i].channel_grp_id;
		get_ivc_db->gcm_dec_supported[i] = hv_vse_db[i].gcm_dec_supported;
		get_ivc_db->gcm_dec_buffer_size[i] = hv_vse_db[i].gcm_dec_buffer_size;
	}

	return ret;
}

static int tnvvse_crypto_map_membuf(struct tnvvse_crypto_ctx *ctx,
		struct tegra_nvvse_map_membuf_ctl *map_membuf_ctl)
{
	struct tegra_virtual_se_membuf_context membuf_ctx;
	int err = 0;

	membuf_ctx.node_id = ctx->node_id;
	membuf_ctx.fd = map_membuf_ctl->fd;

	err = tegra_hv_vse_safety_map_membuf(&membuf_ctx);
	if (err) {
		CRYPTODEV_ERR("%s(): map membuf failed %d\n", __func__, err);
		goto exit;
	}

	map_membuf_ctl->iova = membuf_ctx.iova;

exit:
	return err;
}

static int tnvvse_crypto_unmap_membuf(struct tnvvse_crypto_ctx *ctx,
		struct tegra_nvvse_unmap_membuf_ctl *unmap_membuf_ctl)
{
	struct tegra_virtual_se_membuf_context membuf_ctx;
	int err = 0;

	membuf_ctx.node_id = ctx->node_id;
	membuf_ctx.fd = unmap_membuf_ctl->fd;

	err = tegra_hv_vse_safety_unmap_membuf(&membuf_ctx);
	if (err) {
		CRYPTODEV_ERR("%s(): unmap membuf failed %d\n", __func__, err);
		goto exit;
	}

exit:
	return err;
}

static int tnvvse_crypto_dev_open(struct inode *inode, struct file *filp)
{
	struct tnvvse_crypto_ctx *ctx = NULL;
	struct crypto_sha_state *p_sha_state = NULL;
	int ret = 0;
	uint32_t node_id;
	struct miscdevice *misc;
	bool is_zero_copy_node;

	misc = filp->private_data;
	if (!misc) {
		CRYPTODEV_ERR("%s(): misc is NULL\n", __func__);
		return -EPERM;
	}

	node_id = misc->this_device->id;
	if (node_id >= MAX_NUMBER_MISC_DEVICES) {
		CRYPTODEV_ERR("%s(): node_id is out of range\n", __func__);
		return -EPERM;
	}

	is_zero_copy_node = tegra_hv_vse_get_db()[node_id].is_zero_copy_node;

	if (is_zero_copy_node) {
		mutex_lock(&nvvse_devnode[node_id].lock);
		if (nvvse_devnode[node_id].node_in_use) {
			mutex_unlock(&nvvse_devnode[node_id].lock);
			CRYPTODEV_ERR("%s zero copy node is already opened by another process\n",
					__func__);
			return -EPERM;
		}
		nvvse_devnode[node_id].node_in_use = true;
		mutex_unlock(&nvvse_devnode[node_id].lock);
	}

	ctx = kzalloc(sizeof(struct tnvvse_crypto_ctx), GFP_KERNEL);
	if (!ctx) {
		return -ENOMEM;
	}
	ctx->node_id = node_id;
	ctx->allocated_key_slot_count = 0U;
	ctx->is_zero_copy_node = is_zero_copy_node;

	ctx->rng_buff = kzalloc(NVVSE_MAX_RANDOM_NUMBER_LEN_SUPPORTED, GFP_KERNEL);
	if (!ctx->rng_buff) {
		ret = -ENOMEM;
		goto free_buf;
	}
	ctx->max_rng_buff = NVVSE_MAX_RANDOM_NUMBER_LEN_SUPPORTED;

	/* Allocate buffer for SHA result */
	ctx->sha_result = kzalloc(NVVSE_MAX_ALLOCATED_SHA_RESULT_BUFF_SIZE, GFP_KERNEL);
	if (!ctx->sha_result) {
		ret = -ENOMEM;
		goto free_buf;
	}

	p_sha_state = &ctx->sha_state;
	p_sha_state->sha_intermediate_digest = kzalloc(NVVSE_MAX_ALLOCATED_SHA_RESULT_BUFF_SIZE,
			GFP_KERNEL);
	if (!p_sha_state->sha_intermediate_digest) {
		ret = -ENOMEM;
		goto free_buf;
	}

	filp->private_data = ctx;

	return ret;

free_buf:
	if (ctx) {
		kfree(ctx->rng_buff);
		kfree(ctx->sha_result);
		if (p_sha_state)
			kfree(p_sha_state->sha_intermediate_digest);
	}
	kfree(ctx);
	return ret;
}

static int tnvvse_crypto_dev_release(struct inode *inode, struct file *filp)
{
	struct tnvvse_crypto_ctx *ctx = filp->private_data;

	if (ctx->is_zero_copy_node) {
		tegra_hv_vse_safety_unmap_all_membufs(ctx->node_id);
		nvvse_devnode[ctx->node_id].node_in_use = false;
	}

	if (ctx->allocated_key_slot_count > 0U)
		tegra_hv_vse_close_keyslot(ctx->node_id, ctx->key_grp_id);

	kfree(ctx->sha_result);
	kfree(ctx->rng_buff);
	kfree(ctx->sha_state.sha_intermediate_digest);
	kfree(ctx);

	filp->private_data = NULL;

	return 0;
}

static long tnvvse_crypto_dev_ioctl(struct file *filp,
	unsigned int ioctl_num, unsigned long arg)
{
	struct tnvvse_crypto_ctx *ctx = filp->private_data;
	struct tegra_nvvse_aes_enc_dec_ctl __user *arg_aes_enc_dec_ctl = (void __user *)arg;
	struct tegra_nvvse_aes_gmac_init_ctl __user *arg_aes_gmac_init_ctl = (void __user *)arg;
	struct tegra_nvvse_aes_gmac_sign_verify_ctl __user *arg_aes_gmac_sign_verify_ctl;
	struct tegra_nvvse_aes_cmac_sign_verify_ctl __user *arg_aes_cmac_sign_verify_ctl;
	struct tegra_nvvse_sha_update_ctl *sha_update_ctl;
	struct tegra_nvvse_hmac_sha_sv_ctl *hmac_sha_sv_ctl;
	struct tegra_nvvse_hmac_sha_sv_ctl __user *arg_hmac_sha_sv_ctl;
	struct tegra_nvvse_aes_enc_dec_ctl *aes_enc_dec_ctl;
	struct tegra_nvvse_aes_cmac_sign_verify_ctl *aes_cmac_sign_verify_ctl;
	struct tegra_nvvse_aes_drng_ctl *aes_drng_ctl;
	struct tegra_nvvse_aes_gmac_init_ctl *aes_gmac_init_ctl;
	struct tegra_nvvse_aes_gmac_sign_verify_ctl *aes_gmac_sign_verify_ctl;
	struct tegra_nvvse_tsec_get_keyload_status *tsec_keyload_status;
	struct tegra_nvvse_map_membuf_ctl __user *arg_map_membuf_ctl;
	struct tegra_nvvse_map_membuf_ctl *map_membuf_ctl;
	struct tegra_nvvse_unmap_membuf_ctl __user *arg_unmap_membuf_ctl;
	struct tegra_nvvse_unmap_membuf_ctl *unmap_membuf_ctl;
	struct tegra_nvvse_allocate_key_slot_ctl __user *arg_key_slot_allocate_ctl;
	struct tegra_nvvse_allocate_key_slot_ctl *key_slot_allocate_ctl;
	struct tegra_nvvse_release_key_slot_ctl __user *arg_key_slot_release_ctl;
	struct tegra_nvvse_release_key_slot_ctl *key_slot_release_ctl;
	int ret = 0;
	uint64_t err = 0;

	/*
	 * Avoid processing ioctl if the file has been closed.
	 * This will prevent crashes caused by NULL pointer dereference.
	 */
	if (!ctx) {
		CRYPTODEV_ERR("%s(): ctx not allocated\n", __func__);
		return -EPERM;
	}

	mutex_lock(&nvvse_devnode[ctx->node_id].lock);

	if (ctx->is_zero_copy_node) {
		switch (ioctl_num) {
		case NVVSE_IOCTL_CMDID_UPDATE_SHA:
		case NVVSE_IOCTL_CMDID_AES_GMAC_INIT:
		case NVVSE_IOCTL_CMDID_AES_GMAC_SIGN_VERIFY:
		case NVVSE_IOCTL_CMDID_MAP_MEMBUF:
		case NVVSE_IOCTL_CMDID_UNMAP_MEMBUF:
		case NVVSE_IOCTL_CMDID_ALLOCATE_KEY_SLOT:
		case NVVSE_IOCTL_CMDID_RELEASE_KEY_SLOT:
			break;
		default:
			CRYPTODEV_ERR("%s(): unsupported zero copy node command(%08x)\n", __func__,
					ioctl_num);
			ret = -EINVAL;
			break;
		};
	} else {
		switch (ioctl_num) {
		case NVVSE_IOCTL_CMDID_MAP_MEMBUF:
		case NVVSE_IOCTL_CMDID_UNMAP_MEMBUF:
			CRYPTODEV_ERR("%s(): unsupported node command(%08x)\n", __func__,
					ioctl_num);
			ret = -EINVAL;
			break;
		default:
			break;
		};

	}

	if (ret != 0)
		goto release_lock;

	switch (ioctl_num) {
	case NVVSE_IOCTL_CMDID_UPDATE_SHA:
		sha_update_ctl = kzalloc(sizeof(*sha_update_ctl), GFP_KERNEL);
		if (!sha_update_ctl) {
			CRYPTODEV_ERR("%s(): failed to allocate memory\n", __func__);
			ret = -ENOMEM;
			goto release_lock;
		}

		ret = copy_from_user(sha_update_ctl, (void __user *)arg, sizeof(*sha_update_ctl));
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed to copy_from_user sha_update_ctl:%d\n",
			__func__, ret);
			kfree(sha_update_ctl);
			goto release_lock;
		}

		if ((sha_update_ctl->sha_type < TEGRA_NVVSE_SHA_TYPE_SHA256) ||
			(sha_update_ctl->sha_type > TEGRA_NVVSE_SHA_TYPE_MAX)) {
			CRYPTODEV_ERR("%s(): Invalid sha_type value: %d\n", __func__,
			sha_update_ctl->sha_type);
			kfree(sha_update_ctl);
			ret = -EINVAL;
			goto release_lock;
		}

		ret = tnvvse_crypto_sha_update(ctx, sha_update_ctl);

		kfree(sha_update_ctl);
		break;
	case NVVSE_IOCTL_CMDID_HMAC_SHA_SIGN_VERIFY:
		hmac_sha_sv_ctl = kzalloc(sizeof(*hmac_sha_sv_ctl), GFP_KERNEL);
		if (!hmac_sha_sv_ctl) {
			ret = -ENOMEM;
			goto release_lock;
		}

		arg_hmac_sha_sv_ctl = (void __user *)arg;

		ret = copy_from_user(hmac_sha_sv_ctl, arg_hmac_sha_sv_ctl,
				sizeof(*hmac_sha_sv_ctl));
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed to copy_from_user hmac_sha_sv_ctl:%d\n", __func__,
					ret);
			kfree(hmac_sha_sv_ctl);
			goto release_lock;
		}

		ret = tnvvse_crypto_hmac_sha_sign_verify(ctx, hmac_sha_sv_ctl);
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed in tnvvse_crypto_hmac_sha_sign_verify:%d\n",
			__func__, ret);
			kfree(hmac_sha_sv_ctl);
			goto release_lock;
		}

		if (hmac_sha_sv_ctl->hmac_sha_type == TEGRA_NVVSE_HMAC_SHA_VERIFY) {
			ret = copy_to_user(&arg_hmac_sha_sv_ctl->result, &hmac_sha_sv_ctl->result,
					sizeof(uint8_t));
			if (ret)
				CRYPTODEV_ERR("%s(): Failed to copy_to_user\n", __func__);
		}

		kfree(hmac_sha_sv_ctl);
		break;

	case NVVSE_IOCTL_CMDID_AES_ENCDEC:
		aes_enc_dec_ctl = kzalloc(sizeof(*aes_enc_dec_ctl), GFP_KERNEL);
		if (!aes_enc_dec_ctl) {
			CRYPTODEV_ERR("%s(): failed to allocate memory\n", __func__);
			ret = -ENOMEM;
			goto release_lock;
		}

		ret = copy_from_user(aes_enc_dec_ctl, (void __user *)arg, sizeof(*aes_enc_dec_ctl));
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed to copy_from_user aes_enc_dec_ctl:%d\n",
			__func__, ret);
			kfree(aes_enc_dec_ctl);
			goto release_lock;
		}

		if (aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_GCM)
			ret = tnvvse_crypto_aes_enc_dec_gcm(ctx, aes_enc_dec_ctl);
		else
			ret = tnvvse_crypto_aes_enc_dec(ctx, aes_enc_dec_ctl);

		if (ret) {
			kfree(aes_enc_dec_ctl);
			goto release_lock;
		}

		/* Copy IV returned by VSE */
		if (aes_enc_dec_ctl->is_encryption) {
			if ((aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_CBC) ||
				(aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_SM4_CBC) ||
				(aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_GCM))
				ret = copy_to_user(arg_aes_enc_dec_ctl->initial_vector,
							aes_enc_dec_ctl->initial_vector,
							sizeof(aes_enc_dec_ctl->initial_vector));
			else if ((aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_CTR) ||
				(aes_enc_dec_ctl->aes_mode == TEGRA_NVVSE_AES_MODE_CTR))
				ret = copy_to_user(arg_aes_enc_dec_ctl->initial_counter,
							aes_enc_dec_ctl->initial_counter,
							sizeof(aes_enc_dec_ctl->initial_counter));
			if (ret) {
				CRYPTODEV_ERR("%s(): Failed to copy_to_user\n", __func__);
				kfree(aes_enc_dec_ctl);
				goto release_lock;
			}
		}

		kfree(aes_enc_dec_ctl);
		break;

	case NVVSE_IOCTL_CMDID_AES_GMAC_INIT:
		aes_gmac_init_ctl = kzalloc(sizeof(*aes_gmac_init_ctl), GFP_KERNEL);
		if (!aes_gmac_init_ctl) {
			CRYPTODEV_ERR("%s(): failed to allocate memory\n", __func__);
			ret = -ENOMEM;
			goto release_lock;
		}

		ret = copy_from_user(aes_gmac_init_ctl, (void __user *)arg,
						sizeof(*aes_gmac_init_ctl));
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed to copy_from_user aes_gmac_init_ctl:%d\n",
								__func__, ret);
			kfree(aes_gmac_init_ctl);
			goto release_lock;
		}

		ret = tnvvse_crypto_aes_gmac_init(ctx, aes_gmac_init_ctl);
		if (ret) {
			kfree(aes_gmac_init_ctl);
			goto release_lock;
		}

		/* Copy IV returned by VSE */
		err = copy_to_user(arg_aes_gmac_init_ctl->IV, aes_gmac_init_ctl->IV,
							sizeof(aes_gmac_init_ctl->IV));
		if (err) {
			CRYPTODEV_ERR("%s(): Failed to copy_to_user\n", __func__);
			ret = -EFAULT;
			kfree(aes_gmac_init_ctl);
			goto release_lock;
		}

		kfree(aes_gmac_init_ctl);
		break;

	case NVVSE_IOCTL_CMDID_AES_GMAC_SIGN_VERIFY:
		aes_gmac_sign_verify_ctl = kzalloc(sizeof(*aes_gmac_sign_verify_ctl), GFP_KERNEL);
		if (!aes_gmac_sign_verify_ctl) {
			CRYPTODEV_ERR("%s(): failed to allocate memory\n", __func__);
			ret = -ENOMEM;
			goto release_lock;
		}

		arg_aes_gmac_sign_verify_ctl = (void __user *)arg;
		ret = copy_from_user(aes_gmac_sign_verify_ctl, (void __user *)arg,
						sizeof(*aes_gmac_sign_verify_ctl));
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed to copy_from_user aes_gmac_sign_verify_ctl:%d\n",
								 __func__, ret);
			kfree(aes_gmac_sign_verify_ctl);
			goto release_lock;
		}

		ret = tnvvse_crypto_aes_gmac_sign_verify(ctx, aes_gmac_sign_verify_ctl);
		if (ret) {
			kfree(aes_gmac_sign_verify_ctl);
			goto release_lock;
		}

		if (aes_gmac_sign_verify_ctl->gmac_type == TEGRA_NVVSE_AES_GMAC_VERIFY) {
			err = copy_to_user(&arg_aes_gmac_sign_verify_ctl->result,
						&aes_gmac_sign_verify_ctl->result,
								sizeof(uint8_t));
			if (err) {
				CRYPTODEV_ERR("%s(): Failed to copy_to_user\n", __func__);
				ret = -EFAULT;
				kfree(aes_gmac_sign_verify_ctl);
				goto release_lock;
			}
		}

		kfree(aes_gmac_sign_verify_ctl);
		break;

	case NVVSE_IOCTL_CMDID_AES_CMAC_SIGN_VERIFY:
		aes_cmac_sign_verify_ctl = kzalloc(sizeof(*aes_cmac_sign_verify_ctl), GFP_KERNEL);
		if (!aes_cmac_sign_verify_ctl) {
			CRYPTODEV_ERR("%s(): failed to allocate memory\n", __func__);
			ret = -ENOMEM;
			goto release_lock;
		}

		arg_aes_cmac_sign_verify_ctl = (void __user *)arg;
		ret = copy_from_user(aes_cmac_sign_verify_ctl, (void __user *)arg,
					sizeof(*aes_cmac_sign_verify_ctl));
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed to copy_from_user aes_cmac_sign_verify:%d\n",
						__func__, ret);
			kfree(aes_cmac_sign_verify_ctl);
			goto release_lock;
		}

		ret = tnvvse_crypto_aes_cmac_sign_verify(ctx, aes_cmac_sign_verify_ctl);
		if (ret) {
			kfree(aes_cmac_sign_verify_ctl);
			goto release_lock;
		}

		if (aes_cmac_sign_verify_ctl->cmac_type == TEGRA_NVVSE_AES_CMAC_VERIFY) {
			ret = copy_to_user(&arg_aes_cmac_sign_verify_ctl->result,
						&aes_cmac_sign_verify_ctl->result,
								sizeof(uint8_t));
			if (ret)
				CRYPTODEV_ERR("%s(): Failed to copy_to_user:%d\n", __func__, ret);
		}

		kfree(aes_cmac_sign_verify_ctl);
		break;

	case NVVSE_IOCTL_CMDID_AES_DRNG:
		aes_drng_ctl = kzalloc(sizeof(*aes_drng_ctl), GFP_KERNEL);
		if (!aes_drng_ctl) {
			CRYPTODEV_ERR("%s(): failed to allocate memory\n", __func__);
			ret = -ENOMEM;
			goto release_lock;
		}

		ret = copy_from_user(aes_drng_ctl, (void __user *)arg, sizeof(*aes_drng_ctl));
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed to copy_from_user aes_drng_ctl:%d\n", __func__, ret);
			kfree(aes_drng_ctl);
			goto release_lock;
		}
		ret = tnvvse_crypto_get_aes_drng(ctx, aes_drng_ctl);

		kfree(aes_drng_ctl);
		break;


	case NVVSE_IOCTL_CMDID_TSEC_SIGN_VERIFY:
		aes_cmac_sign_verify_ctl = kzalloc(sizeof(*aes_cmac_sign_verify_ctl), GFP_KERNEL);
		if (!aes_cmac_sign_verify_ctl) {
			CRYPTODEV_ERR("%s(): failed to allocate memory\n", __func__);
			ret = -ENOMEM;
			goto release_lock;
		}

		arg_aes_cmac_sign_verify_ctl = (void __user *)arg;
		ret = copy_from_user(aes_cmac_sign_verify_ctl, (void __user *)arg,
					sizeof(*aes_cmac_sign_verify_ctl));
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed to copy_from_user tsec_sign_verify:%d\n",
						__func__, ret);
			kfree(aes_cmac_sign_verify_ctl);
			goto release_lock;
		}
		ret = tnvvtsec_crypto_aes_cmac_sign_verify(ctx, aes_cmac_sign_verify_ctl);
		if (ret) {
			kfree(aes_cmac_sign_verify_ctl);
			goto release_lock;
		}

		if (aes_cmac_sign_verify_ctl->cmac_type == TEGRA_NVVSE_AES_CMAC_VERIFY) {
			ret = copy_to_user(&arg_aes_cmac_sign_verify_ctl->result,
						&aes_cmac_sign_verify_ctl->result,
								sizeof(uint8_t));
			if (ret)
				CRYPTODEV_ERR("%s(): Failed to copy_to_user:%d\n", __func__, ret);
		}

		kfree(aes_cmac_sign_verify_ctl);
		break;

	case NVVSE_IOCTL_CMDID_TSEC_GET_KEYLOAD_STATUS:
		tsec_keyload_status = kzalloc(sizeof(*tsec_keyload_status), GFP_KERNEL);
		if (!tsec_keyload_status) {
			CRYPTODEV_ERR("%s(): failed to allocate memory\n", __func__);
			ret = -ENOMEM;
			goto release_lock;
		}

		ret = tnvvse_crypto_tsec_get_keyload_status(ctx, tsec_keyload_status);
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed to get keyload status:%d\n", __func__, ret);
			kfree(tsec_keyload_status);
			goto release_lock;
		}

		ret = copy_to_user((void __user *)arg, tsec_keyload_status,
				sizeof(*tsec_keyload_status));
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed to copy_to_user tsec_keyload_status:%d\n",
					__func__, ret);
			kfree(tsec_keyload_status);
			goto release_lock;
		}

		kfree(tsec_keyload_status);
		break;

	case NVVSE_IOCTL_CMDID_MAP_MEMBUF:
		map_membuf_ctl = kzalloc(sizeof(*map_membuf_ctl), GFP_KERNEL);
		if (!map_membuf_ctl) {
			ret = -ENOMEM;
			goto release_lock;
		}

		arg_map_membuf_ctl = (void __user *)arg;

		ret = copy_from_user(map_membuf_ctl, arg_map_membuf_ctl,
					sizeof(*map_membuf_ctl));
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed to copy_from_user map_membuf_ctl:%d\n",
						__func__, ret);
			kfree(map_membuf_ctl);
			goto release_lock;
		}

		ret = tnvvse_crypto_map_membuf(ctx, map_membuf_ctl);
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed to map membuf status:%d\n", __func__, ret);
			kfree(map_membuf_ctl);
			goto release_lock;
		}

		err = copy_to_user(arg_map_membuf_ctl, map_membuf_ctl,
				sizeof(*map_membuf_ctl));
		if (err) {
			CRYPTODEV_ERR("%s(): Failed to copy_to_user map_membuf_ctl\n",
					__func__);
			ret = -EFAULT;
			kfree(map_membuf_ctl);
			goto release_lock;
		}

		kfree(map_membuf_ctl);
		break;

	case NVVSE_IOCTL_CMDID_UNMAP_MEMBUF:
		unmap_membuf_ctl = kzalloc(sizeof(*unmap_membuf_ctl), GFP_KERNEL);
		if (!unmap_membuf_ctl) {
			ret = -ENOMEM;
			goto release_lock;
		}

		arg_unmap_membuf_ctl = (void __user *)arg;

		ret = copy_from_user(unmap_membuf_ctl, arg_unmap_membuf_ctl,
					sizeof(*unmap_membuf_ctl));
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed to copy_from_user unmap_membuf_ctl:%d\n",
						__func__, ret);
			kfree(unmap_membuf_ctl);
			goto release_lock;
		}

		ret = tnvvse_crypto_unmap_membuf(ctx, unmap_membuf_ctl);
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed to unmap membuf status:%d\n", __func__, ret);
			kfree(unmap_membuf_ctl);
			goto release_lock;
		}

		err = copy_to_user(arg_unmap_membuf_ctl, unmap_membuf_ctl,
				sizeof(*unmap_membuf_ctl));
		if (err) {
			CRYPTODEV_ERR("%s(): Failed to copy_to_user unmap_membuf_ctl\n",
					__func__);
			ret = -EFAULT;
			kfree(unmap_membuf_ctl);
			goto release_lock;
		}

		kfree(unmap_membuf_ctl);
		break;

	case NVVSE_IOCTL_CMDID_ALLOCATE_KEY_SLOT:
		key_slot_allocate_ctl = kzalloc(sizeof(*key_slot_allocate_ctl), GFP_KERNEL);
		if (!key_slot_allocate_ctl) {
			CRYPTODEV_ERR("%s(): failed to allocate memory\n", __func__);
			ret = -ENOMEM;
			goto release_lock;
		}

		arg_key_slot_allocate_ctl = (void __user *)arg;

		ret = copy_from_user(key_slot_allocate_ctl, arg_key_slot_allocate_ctl,
			sizeof(*key_slot_allocate_ctl));
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed to copy_from_user key_slot_allocate_ctl:%d\n",
			__func__, ret);
			kfree(key_slot_allocate_ctl);
			goto release_lock;
		}

		ret = tnvvse_crypto_allocate_key_slot(ctx, key_slot_allocate_ctl);
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed to allocate key slot:%d\n", __func__, ret);
			kfree(key_slot_allocate_ctl);
			goto release_lock;
		}

		err = copy_to_user(arg_key_slot_allocate_ctl, key_slot_allocate_ctl,
			sizeof(*key_slot_allocate_ctl));
		if (err) {
			CRYPTODEV_ERR("%s(): Failed to copy_to_user key_slot_allocate_ctl\n",
			__func__);
			ret = -EFAULT;
			kfree(key_slot_allocate_ctl);
			goto release_lock;
		}

		kfree(key_slot_allocate_ctl);
		break;

	case NVVSE_IOCTL_CMDID_RELEASE_KEY_SLOT:
		key_slot_release_ctl = kzalloc(sizeof(*key_slot_release_ctl), GFP_KERNEL);
		if (!key_slot_release_ctl) {
			CRYPTODEV_ERR("%s(): failed to allocate memory\n", __func__);
			ret = -ENOMEM;
			goto release_lock;
		}

		arg_key_slot_release_ctl = (void __user *)arg;
		ret = copy_from_user(key_slot_release_ctl, arg_key_slot_release_ctl,
			sizeof(*key_slot_release_ctl));
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed to copy_from_user key_slot_release_ctl:%d\n",
			__func__, ret);
			kfree(key_slot_release_ctl);
			goto release_lock;
		}

		ret = tnvvse_crypto_release_key_slot(ctx, key_slot_release_ctl);
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed to release key slot:%d\n", __func__, ret);
			kfree(key_slot_release_ctl);
			goto release_lock;
		}

		err = copy_to_user(arg_key_slot_release_ctl, key_slot_release_ctl,
			sizeof(*key_slot_release_ctl));
		if (err) {
			CRYPTODEV_ERR("%s(): Failed to copy_to_user key_slot_release_ctl\n",
			__func__);
			ret = -EFAULT;
			kfree(key_slot_release_ctl);
			goto release_lock;
		}

		kfree(key_slot_release_ctl);
		break;

	default:
		CRYPTODEV_ERR("%s(): invalid ioctl code(%d[0x%08x])",
		__func__, ioctl_num, ioctl_num);
		ret = -EINVAL;
		break;
	}

release_lock:
	mutex_unlock(&nvvse_devnode[ctx->node_id].lock);

	return ret;
}

static const struct file_operations tnvvse_crypto_fops = {
	.owner			= THIS_MODULE,
	.open			= tnvvse_crypto_dev_open,
	.release		= tnvvse_crypto_dev_release,
	.unlocked_ioctl		= tnvvse_crypto_dev_ioctl,
};

static int tnvvse_crypto_info_dev_open(struct inode *inode, struct file *filp)
{
	/* No context needed for the info device */
	return 0;
}

static int tnvvse_crypto_info_dev_release(struct inode *inode, struct file *filp)
{
	/* No cleanup needed for the info device */
	return 0;
}

static long tnvvse_crypto_info_dev_ioctl(struct file *filp,
	unsigned int ioctl_num, unsigned long arg)
{
	struct tegra_nvvse_get_ivc_db *get_ivc_db;
	int ret = 0;
	uint64_t err = 0;

	if (ioctl_num == NVVSE_IOCTL_CMDID_GET_IVC_DB) {
		get_ivc_db = kzalloc(sizeof(*get_ivc_db), GFP_KERNEL);
		if (!get_ivc_db) {
			CRYPTODEV_ERR("%s(): failed to allocate memory\n", __func__);
			ret = -ENOMEM;
			goto end;
		}

		ret = tnvvse_crypto_get_ivc_db(get_ivc_db);
		if (ret) {
			CRYPTODEV_ERR("%s(): Failed to get ivc database get_ivc_db:%d\n", __func__, ret);
			kfree(get_ivc_db);
			goto end;
		}

		err = copy_to_user((void __user *)arg, &ivc_database, sizeof(ivc_database));
		if (err) {
			CRYPTODEV_ERR("%s(): Failed to copy_to_user ivc_database\n", __func__);
			kfree(get_ivc_db);
			ret = -EFAULT;
			goto end;
		}

		kfree(get_ivc_db);
	} else {
		CRYPTODEV_ERR("%s(): invalid ioctl code(%d[0x%08x])", __func__, ioctl_num, ioctl_num);
		ret = -EINVAL;
	}

end:
	return ret;
}

static const struct file_operations tnvvse_crypto_info_fops = {
	.owner			= THIS_MODULE,
	.open			= tnvvse_crypto_info_dev_open,
	.release		= tnvvse_crypto_info_dev_release,
	.unlocked_ioctl		= tnvvse_crypto_info_dev_ioctl,
};

static int __init tnvvse_crypto_device_init(void)
{
	uint32_t cnt, ctr;
	int ret = 0;
	struct miscdevice *misc;
	struct crypto_dev_to_ivc_map *ivc_db;
	/* Device node type */
	const char * const node_prefix[] = {
		"tegra-nvvse-crypto-gpse-aes0-",
		"tegra-nvvse-crypto-gpse-aes1-",
		"tegra-nvvse-crypto-gpse-sha-",
		"",
		"",
		"",
		"tegra-nvvse-crypto-tsec-",
		"tegra-nvvse-crypto-gcse1-aes0-",
		"tegra-nvvse-crypto-gcse1-aes1-",
		"tegra-nvvse-crypto-gcse1-sha-",
		"tegra-nvvse-crypto-gcse2-aes0-",
		"tegra-nvvse-crypto-gcse2-aes1-",
		"tegra-nvvse-crypto-gcse2-sha-"
	};
	char const numbers[] = "0123456789";
	char *node_name;
	size_t str_len;

	CRYPTODEV_INFO("%s(): init start\n", __func__);

	/* get ivc databse */
	ret = tnvvse_crypto_get_ivc_db(&ivc_database);
	if (ret) {
		CRYPTODEV_ERR("%s(): tnvvse_crypto_get_ivc_db failed\n", __func__);
		return ret;
	}
	ivc_db = tegra_hv_vse_get_db();
	if (!ivc_db) {
		CRYPTODEV_ERR("%s(): tegra_hv_vse_get_db returned NULL\n", __func__);
		return -EINVAL;
	}

	/* Register the info device node */
	nvvse_info_device = kzalloc(sizeof(struct miscdevice), GFP_KERNEL);
	if (nvvse_info_device == NULL) {
		CRYPTODEV_ERR("%s(): failed to allocate memory for info device\n", __func__);
		return -ENOMEM;
	}

	nvvse_info_device->minor = MISC_DYNAMIC_MINOR;
	nvvse_info_device->fops = &tnvvse_crypto_info_fops;
	nvvse_info_device->name = "tegra-nvvse-crypto-info";

	ret = misc_register(nvvse_info_device);
	if (ret != 0) {
		CRYPTODEV_ERR("%s: info device registration failed err %d\n", __func__, ret);
		kfree(nvvse_info_device);
		return ret;
	}

	for (cnt = 0; cnt < MAX_NUMBER_MISC_DEVICES; cnt++) {

		if (ivc_db[cnt].node_in_use != true)
			break;

		/* Dynamic initialisation of misc device */
		misc = kzalloc(sizeof(struct miscdevice), GFP_KERNEL);
		if (misc == NULL) {
			CRYPTODEV_ERR("%s(): failed to allocate memory for misc device\n", __func__);
			ret = -ENOMEM;
			goto fail;
		}

		node_name = kzalloc(MISC_DEVICE_NAME_LEN, GFP_KERNEL);
		if (node_name == NULL) {
			CRYPTODEV_ERR("%s(): failed to allocate memory for node name\n", __func__);
			ret = -ENOMEM;
			goto fail;
		}

		misc->minor = MISC_DYNAMIC_MINOR;
		misc->fops = &tnvvse_crypto_fops;
		misc->name = node_name;

		if (ivc_db[cnt].engine_id >= sizeof(node_prefix)/sizeof(char *)) {
			CRYPTODEV_ERR("%s: invalid engine id %u\n", __func__,
				ivc_db[cnt].engine_id);
			ret = -EINVAL;
			goto fail;
		}

		if (node_prefix[ivc_db[cnt].engine_id][0U] == '\0') {
			CRYPTODEV_ERR("%s: unsupported engine id %u\n", __func__,
				ivc_db[cnt].engine_id);
			ret = -EINVAL;
			goto fail;
		}

		if (ivc_db[cnt].instance_id > 99U) {
			CRYPTODEV_ERR("%s: unsupported instance id %u\n", __func__,
				ivc_db[cnt].instance_id);
			ret = -EINVAL;
			goto fail;
		}

		str_len = strlen(node_prefix[ivc_db[cnt].engine_id]);
		if (str_len > (MISC_DEVICE_NAME_LEN - 3)) {
			CRYPTODEV_ERR("%s: buffer overflown for misc dev %u\n", __func__, cnt);
			ret = -EINVAL;
			goto fail;
		}
		memcpy(node_name, node_prefix[ivc_db[cnt].engine_id], str_len);

		node_name[str_len++] = numbers[(ivc_db[cnt].instance_id / 10U)];
		node_name[str_len++] = numbers[(ivc_db[cnt].instance_id % 10U)];
		node_name[str_len++] = '\0';

		ret = misc_register(misc);
		if (ret != 0) {
			CRYPTODEV_ERR("%s: misc dev %u registration failed err %d\n",
			__func__, cnt, ret);
			goto fail;
		}
		misc->this_device->id = cnt;
		nvvse_devnode[cnt].g_misc_devices = misc;
		mutex_init(&nvvse_devnode[cnt].lock);
	}

	CRYPTODEV_INFO("%s(): init success\n", __func__);

	return ret;

fail:
	/* Cleanup the info device if needed */
	if (nvvse_info_device) {
		misc_deregister(nvvse_info_device);
		kfree(nvvse_info_device);
		nvvse_info_device = NULL;
	}

	for (ctr = 0; ctr < cnt; ctr++) {
		misc_deregister(nvvse_devnode[ctr].g_misc_devices);
		kfree(nvvse_devnode[ctr].g_misc_devices->name);
		kfree(nvvse_devnode[ctr].g_misc_devices);
		nvvse_devnode[ctr].g_misc_devices = NULL;
		mutex_destroy(&nvvse_devnode[ctr].lock);
	}

	kfree(misc);

	return ret;
}
module_init(tnvvse_crypto_device_init);

static void __exit tnvvse_crypto_device_exit(void)
{
	uint32_t ctr;

	/* Unregister the info device node */
	if (nvvse_info_device != NULL) {
		misc_deregister(nvvse_info_device);
		kfree(nvvse_info_device);
		nvvse_info_device = NULL;
	}

	for (ctr = 0; ctr < MAX_NUMBER_MISC_DEVICES; ctr++) {
		if (nvvse_devnode[ctr].g_misc_devices != NULL) {
			misc_deregister(nvvse_devnode[ctr].g_misc_devices);
			kfree(nvvse_devnode[ctr].g_misc_devices->name);
			kfree(nvvse_devnode[ctr].g_misc_devices);
			nvvse_devnode[ctr].g_misc_devices = NULL;
			mutex_destroy(&nvvse_devnode[ctr].lock);
		}
	}
}
module_exit(tnvvse_crypto_device_exit);

MODULE_DESCRIPTION("Tegra NVVSE Crypto device driver.");
MODULE_AUTHOR("NVIDIA Corporation");
MODULE_LICENSE("GPL v2");
