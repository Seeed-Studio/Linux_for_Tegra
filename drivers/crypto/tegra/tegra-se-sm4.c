// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2023-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * Crypto driver to handle SM4 block cipher algorithms using NVIDIA Security Engine.
 */

#include <nvidia/conftest.h>

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/host1x-next.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <crypto/aead.h>
#include <crypto/aes.h>
#include <crypto/engine.h>
#include <crypto/gcm.h>
#include <crypto/scatterwalk.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/skcipher.h>

#include "tegra-se.h"

struct tegra_sm4_ctx {
#ifndef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
	struct crypto_engine_ctx enginectx;
#endif
	struct tegra_se *se;
	u32 alg;
	u32 keylen;
	u32 ivsize;
	u32 key1_id;
	u32 key2_id;
	u8 key1[AES_MAX_KEY_SIZE];
	u8 key2[AES_MAX_KEY_SIZE];
};

struct tegra_sm4_reqctx {
	struct tegra_se_datbuf datbuf;
	struct tegra_se *se;
	bool encrypt;
	u32 cfg;
	u32 crypto_cfg;
	u32 key1_id;
	u32 key2_id;
};

struct tegra_sm4_gcm_ctx {
#ifndef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
	struct crypto_engine_ctx enginectx;
#endif
	struct tegra_se *se;
	unsigned int authsize;
	u32 alg;
	u32 mac_alg;
	u32 final_alg;
	u32 verify_alg;
	u32 keylen;
	u32 key_id;
	u8 key[AES_MAX_KEY_SIZE];
};

struct tegra_sm4_gcm_reqctx {
	struct tegra_se_datbuf inbuf;
	struct tegra_se_datbuf outbuf;
	struct scatterlist *src_sg;
	struct scatterlist *dst_sg;
	unsigned int assoclen;
	unsigned int cryptlen;
	unsigned int authsize;
	bool encrypt;
	u32 config;
	u32 crypto_config;
	u32 key_id;
	u32 iv[4];
	u8 authdata[16];
};

struct tegra_sm4_cmac_ctx {
#ifndef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
	struct crypto_engine_ctx enginectx;
#endif
	struct tegra_se *se;
	u32 alg;
	u32 final_alg;
	u32 key_id;
	u32 keylen;
	u8 key[AES_MAX_KEY_SIZE];
	struct crypto_shash *fallback_tfm;
};

struct tegra_sm4_cmac_reqctx {
	struct scatterlist *src_sg;
	struct tegra_se_datbuf datbuf;
	struct tegra_se_datbuf digest;
	struct tegra_se_datbuf residue;
	unsigned int total_len;
	unsigned int blk_size;
	unsigned int task;
	u32 config;
	u32 crypto_config;
	u32 key_id;
	u32 result[CMAC_RESULT_REG_COUNT];
	u32 *iv;
};

/* increment counter (128-bit int) */
static void ctr_iv_inc(__u8 *counter, __u8 bits, __u32 nums)
{
	do {
		--bits;
		nums += counter[bits];
		counter[bits] = nums & 0xff;
		nums >>= 8;
	} while (bits && nums);
}

static void tegra_cbc_iv_copyback(struct skcipher_request *req, struct tegra_sm4_ctx *ctx)
{
	struct tegra_sm4_reqctx *rctx = skcipher_request_ctx(req);
	unsigned int off;

	off = req->cryptlen - ctx->ivsize;

	if (rctx->encrypt)
		memcpy(req->iv, rctx->datbuf.buf + off, ctx->ivsize);
	else
		sg_pcopy_to_buffer(req->src, sg_nents(req->src),
				   req->iv, ctx->ivsize, off);
}

static void tegra_sm4_update_iv(struct skcipher_request *req, struct tegra_sm4_ctx *ctx)
{
	int sz;

	if (ctx->alg == SE_ALG_SM4_CBC) {
		tegra_cbc_iv_copyback(req, ctx);
	} else if (ctx->alg == SE_ALG_SM4_CTR) {
		sz = req->cryptlen / ctx->ivsize;
		if (req->cryptlen % ctx->ivsize)
			sz++;

		ctr_iv_inc(req->iv, ctx->ivsize, sz);
	}
}

static int tegra_sm4_prep_cmd(struct tegra_se *se, u32 *cpuvaddr, u32 *iv,
				     int len, dma_addr_t addr, int cfg, int cryp_cfg)
{
	int i = 0, j;

	if (iv) {
		cpuvaddr[i++] = host1x_opcode_setpayload(SE_CRYPTO_CTR_REG_COUNT);
		cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->linear_ctr);
		for (j = 0; j < SE_CRYPTO_CTR_REG_COUNT; j++)
			cpuvaddr[i++] = iv[j];
	}

	cpuvaddr[i++] = se_host1x_opcode_nonincr(se->hw->regs->last_blk, 1);
	cpuvaddr[i++] = (len / AES_BLOCK_SIZE) - 1;
	cpuvaddr[i++] = se_host1x_opcode_incr(se->hw->regs->config, 6);
	cpuvaddr[i++] = cfg;
	cpuvaddr[i++] = cryp_cfg;

	/* Source address setting */
	cpuvaddr[i++] = lower_32_bits(addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(addr)) | SE_ADDR_HI_SZ(len);

	/* Destination address setting */
	cpuvaddr[i++] = lower_32_bits(addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(addr)) |
			SE_ADDR_HI_SZ(len);

	cpuvaddr[i++] = se_host1x_opcode_nonincr(se->hw->regs->op, 1);
	cpuvaddr[i++] = SE_AES_OP_WRSTALL | SE_AES_OP_LASTBUF |
			SE_AES_OP_START;

	cpuvaddr[i++] = se_host1x_opcode_nonincr(host1x_uclass_incr_syncpt_r(), 1);
	cpuvaddr[i++] = host1x_uclass_incr_syncpt_cond_f(1) |
			host1x_uclass_incr_syncpt_indx_f(se->syncpt_id);

	dev_dbg(se->dev, "cfg %#x crypto cfg %#x\n", cfg, cryp_cfg);

	return i;
}

static int tegra_sm4_do_one_req(struct crypto_engine *engine, void *areq)
{
	unsigned int len, src_nents, dst_nents, size;
	u32 *cpuvaddr, *iv, config, crypto_config;
	struct tegra_sm4_reqctx *rctx;
	struct skcipher_request *req;
	struct tegra_sm4_ctx *ctx;
	struct tegra_se *se;
	int ret;

	req = container_of(areq, struct skcipher_request, base);
	rctx = skcipher_request_ctx(req);
	ctx = crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	se = ctx->se;
	iv = (u32 *)req->iv;

	/* Keys in ctx might be stored in KDS. Copy it to request ctx */
	if (ctx->key1_id)
		rctx->key1_id = tegra_key_get_idx(ctx->se, ctx->key1_id);

	/* Use reserved keyslots if keyslots are unavailable */
	if (!ctx->key1_id || !rctx->key1_id) {
		ret = tegra_key_submit_reserved_aes(ctx->se, ctx->key1,
					ctx->keylen, ctx->alg, &rctx->key1_id);
		if (ret)
			goto out;
	}

	rctx->key2_id = 0;

	/* If there are 2 keys stored (for XTS), retrieve them both */
	if (ctx->alg == SE_ALG_SM4_XTS) {
		if (ctx->key2_id)
			rctx->key1_id = tegra_key_get_idx(ctx->se, ctx->key1_id);

		/* Use reserved keyslots if keyslots are unavailable */
		if (!ctx->key2_id || !rctx->key2_id) {
			ret = tegra_key_submit_reserved_aes(ctx->se, ctx->key2,
						ctx->keylen, ctx->alg, &rctx->key2_id);
			if (ret)
				goto key1_free;
		}
	}

	rctx->datbuf.size = req->cryptlen;
	rctx->datbuf.buf = dma_alloc_coherent(ctx->se->dev, rctx->datbuf.size,
					     &rctx->datbuf.addr, GFP_KERNEL);
	if (!rctx->datbuf.buf) {
		ret = -ENOMEM;
		goto key2_free;
	}

	cpuvaddr = se->cmdbuf->addr;
	len = req->cryptlen;

	/* Pad input to AES block size */
	if (len % AES_BLOCK_SIZE)
		len += AES_BLOCK_SIZE - (len % AES_BLOCK_SIZE);

	src_nents = sg_nents(req->src);
	sg_copy_to_buffer(req->src, src_nents, rctx->datbuf.buf, req->cryptlen);

	config = se->regcfg->cfg(ctx->alg, rctx->encrypt);
	crypto_config = se->regcfg->crypto_cfg(ctx->alg, rctx->encrypt);
	crypto_config |= SE_AES_KEY_INDEX(rctx->key1_id);
	if (rctx->key2_id)
		crypto_config |= SE_AES_KEY2_INDEX(rctx->key2_id);

	/* Prepare the command and submit */
	size = tegra_sm4_prep_cmd(se, cpuvaddr, iv, len, rctx->datbuf.addr,
					 config, crypto_config);

	ret = tegra_se_host1x_submit(se, se->cmdbuf, size);

	/* Copy the result */
	dst_nents = sg_nents(req->dst);
	tegra_sm4_update_iv(req, ctx);
	sg_copy_from_buffer(req->dst, dst_nents, rctx->datbuf.buf, req->cryptlen);

	dma_free_coherent(ctx->se->dev, rctx->datbuf.size,
			  rctx->datbuf.buf, rctx->datbuf.addr);

key2_free:
	if (tegra_key_is_reserved(rctx->key2_id))
		tegra_key_invalidate_reserved(ctx->se, rctx->key2_id, ctx->alg);
	else if (rctx->key2_id != ctx->key2_id)
		tegra_key_invalidate(ctx->se, rctx->key2_id, ctx->alg);
key1_free:
	if (tegra_key_is_reserved(rctx->key1_id))
		tegra_key_invalidate_reserved(ctx->se, rctx->key1_id, ctx->alg);
	else if (rctx->key1_id != ctx->key1_id)
		tegra_key_invalidate(ctx->se, rctx->key1_id, ctx->alg);
out:
	crypto_finalize_skcipher_request(se->engine, req, ret);

	return ret;
}

static int tegra_sm4_cra_init(struct crypto_skcipher *tfm)
{
	struct tegra_sm4_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);
	struct tegra_se_alg *se_alg;
	const char *algname;
	int ret;

#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
	se_alg = container_of(alg, struct tegra_se_alg, alg.skcipher.base);
#else
	se_alg = container_of(alg, struct tegra_se_alg, alg.skcipher);
#endif

	crypto_skcipher_set_reqsize(tfm, sizeof(struct tegra_sm4_reqctx));

	ctx->se = se_alg->se_dev;
	ctx->key1_id = 0;
	ctx->key2_id = 0;
	ctx->keylen = 0;
	ctx->ivsize = crypto_skcipher_ivsize(tfm);

	algname = crypto_tfm_alg_name(&tfm->base);
	ret = se_algname_to_algid(algname);
	if (ret < 0) {
		dev_err(ctx->se->dev, "Invalid algorithm\n");
		return ret;
	}

	ctx->alg = ret;

#ifndef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
	ctx->enginectx.op.do_one_request = tegra_sm4_do_one_req;
#endif

	return 0;
}

static void tegra_sm4_cra_exit(struct crypto_skcipher *tfm)
{
	struct tegra_sm4_ctx *ctx = crypto_tfm_ctx(&tfm->base);

	if (ctx->key1_id)
		tegra_key_invalidate(ctx->se, ctx->key1_id, ctx->alg);

	if (ctx->key2_id)
		tegra_key_invalidate(ctx->se, ctx->key2_id, ctx->alg);
}

static int tegra_sm4_setkey(struct crypto_skcipher *tfm,
			       const u8 *key, u32 keylen)
{
	struct tegra_sm4_ctx *ctx = crypto_skcipher_ctx(tfm);
	int ret;

	if (aes_check_keylen(keylen)) {
		dev_err(ctx->se->dev, "key length validation failed\n");
		return -EINVAL;
	}

	ret = tegra_key_submit(ctx->se, key, keylen, ctx->alg, &ctx->key1_id);
	if (ret) {
		ctx->keylen = keylen;
		memcpy(ctx->key1, key, keylen);
	}

	return 0;
}

static int tegra_sm4_xts_setkey(struct crypto_skcipher *tfm,
			       const u8 *key, u32 keylen)
{
	struct tegra_sm4_ctx *ctx = crypto_skcipher_ctx(tfm);
	int ret;
	u32 len;

	len = keylen / 2;
	if (aes_check_keylen(len)) {
		dev_err(ctx->se->dev, "key length validation failed\n");
		return -EINVAL;
	}

	ret = tegra_key_submit(ctx->se, key, len,
			ctx->alg, &ctx->key1_id);
	if (ret) {
		ctx->keylen = len;
		memcpy(ctx->key1, key, len);
	}

	ret = tegra_key_submit(ctx->se, key + len, len,
			ctx->alg, &ctx->key2_id);
	if (ret) {
		ctx->keylen = len;
		memcpy(ctx->key2, key + len, len);
	}

	return 0;
}

static int tegra_sm4_kac2_manifest(u32 user, u32 alg, u32 keylen)
{
	int manifest;

	manifest = SE_KAC2_USER(user) | SE_KAC2_ORIGIN_SW;
	manifest |= SE_KAC2_DECRYPT_EN | SE_KAC2_ENCRYPT_EN;
	manifest |= SE_KAC2_TYPE_SYM | SE_KAC2_SUBTYPE_SM4;

	switch (alg) {
	case SE_ALG_SM4_CBC:
	case SE_ALG_SM4_ECB:
	case SE_ALG_SM4_CTR:
	case SE_ALG_SM4_OFB:
		manifest |= SE_KAC2_ENC;
		break;
	case SE_ALG_SM4_XTS:
		manifest |= SE_KAC2_XTS;
		break;
	case SE_ALG_SM4_GCM:
		manifest |= SE_KAC2_GCM;
		break;
	case SE_ALG_SM4_CMAC:
		manifest |= SE_KAC2_CMAC;
		break;

	default:
		return -EINVAL;
	}

	switch (keylen) {
	case AES_KEYSIZE_128:
		manifest |= SE_KAC2_SIZE_128;
		break;
	case AES_KEYSIZE_192:
		manifest |= SE_KAC2_SIZE_192;
		break;
	case AES_KEYSIZE_256:
		manifest |= SE_KAC2_SIZE_256;
		break;
	default:
		return -EINVAL;
	}

	return manifest;
}

static inline int tegra264_sm4_crypto_cfg(u32 alg, bool encrypt)
{
	u32 cfg = SE_AES_CRYPTO_CFG_SCC_DIS;

	switch (alg) {
	case SE_ALG_SM4_ECB:
		break;

	case SE_ALG_SM4_CTR:
		cfg |= SE_AES_IV_SEL_REG |
		       SE_AES_CRYPTO_CFG_CTR_CNTN(1);
		break;
	case SE_ALG_SM4_CBC:
	case SE_ALG_SM4_OFB:
	case SE_ALG_SM4_XTS:
		cfg |= SE_AES_IV_SEL_REG;
		break;
	default:
		return -EINVAL;
	case SE_ALG_SM4_CMAC:
	case SE_ALG_SM4_GMAC:
		break;
	case SE_ALG_SM4_GCM:
	case SE_ALG_SM4_GCM_FINAL:
	case SE_ALG_SM4_GCM_VERIFY:
		cfg |= SE_AES_IV_SEL_REG;
		break;
	}

	return cfg;
}

static int tegra264_sm4_cfg(u32 alg, bool encrypt)
{
	switch (alg) {
	case SE_ALG_SM4_CBC:
		if (encrypt)
			return SE_CFG_SM4_CBC_ENCRYPT;
		else
			return SE_CFG_SM4_CBC_DECRYPT;
	case SE_ALG_SM4_ECB:
		if (encrypt)
			return SE_CFG_SM4_ECB_ENCRYPT;
		else
			return SE_CFG_SM4_ECB_DECRYPT;
	case SE_ALG_SM4_CTR:
		if (encrypt)
			return SE_CFG_SM4_CTR_ENCRYPT;
		else
			return SE_CFG_SM4_CTR_DECRYPT;
	case SE_ALG_SM4_OFB:
		if (encrypt)
			return SE_CFG_SM4_OFB_ENCRYPT;
		else
			return SE_CFG_SM4_OFB_DECRYPT;
	case SE_ALG_SM4_XTS:
		if (encrypt)
			return SE_CFG_SM4_XTS_ENCRYPT;
		else
			return SE_CFG_SM4_XTS_DECRYPT;
	case SE_ALG_SM4_GMAC:
		if (encrypt)
			return SE_CFG_SM4_GMAC_ENCRYPT;
		else
			return SE_CFG_SM4_GMAC_DECRYPT;

	case SE_ALG_SM4_GCM:
		if (encrypt)
			return SE_CFG_SM4_GCM_ENCRYPT;
		else
			return SE_CFG_SM4_GCM_DECRYPT;

	case SE_ALG_SM4_GCM_FINAL:
		if (encrypt)
			return SE_CFG_SM4_GCM_FINAL_ENCRYPT;
		else
			return SE_CFG_SM4_GCM_FINAL_DECRYPT;

	case SE_ALG_SM4_GCM_VERIFY:
		return SE_CFG_SM4_GCM_VERIFY;

	case SE_ALG_SM4_CMAC:
		return SE_CFG_SM4_CMAC | SE_AES_DST_KEYTABLE;

	case SE_ALG_SM4_CMAC_FINAL:
		return SE_CFG_SM4_CMAC;
	}

	return -EINVAL;
}

static int tegra_sm4_encrypt(struct skcipher_request *req)
{
	struct tegra_sm4_ctx *ctx;
	struct tegra_sm4_reqctx *rctx;

	ctx  = crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	rctx = skcipher_request_ctx(req);
	rctx->encrypt = true;

	return crypto_transfer_skcipher_request_to_engine(ctx->se->engine, req);
}

static int tegra_sm4_decrypt(struct skcipher_request *req)
{
	struct tegra_sm4_ctx *ctx;
	struct tegra_sm4_reqctx *rctx;

	ctx  = crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	rctx = skcipher_request_ctx(req);
	rctx->encrypt = false;

	return crypto_transfer_skcipher_request_to_engine(ctx->se->engine, req);
}

static struct tegra_se_alg tegra_sm4_algs[] = {
	{
		.alg.skcipher = {
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			.base = {
#endif
			.init			= tegra_sm4_cra_init,
			.exit			= tegra_sm4_cra_exit,
			.setkey			= tegra_sm4_xts_setkey,
			.encrypt		= tegra_sm4_encrypt,
			.decrypt		= tegra_sm4_decrypt,
			.min_keysize		= 2 * AES_MIN_KEY_SIZE,
			.max_keysize		= 2 * AES_MAX_KEY_SIZE,
			.ivsize			= AES_BLOCK_SIZE,
			.base = {
				.cra_name	   = "xts(sm4)",
				.cra_driver_name   = "xts-sm4-tegra",
				.cra_priority	   = 500,
				.cra_flags	   = CRYPTO_ALG_TYPE_SKCIPHER |
						    CRYPTO_ALG_ASYNC,
				.cra_blocksize	   = AES_BLOCK_SIZE,
				.cra_ctxsize	   = sizeof(struct tegra_sm4_ctx),
				.cra_alignmask	   = 0,
				.cra_module	   = THIS_MODULE,
			},
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			},
			.op.do_one_request = tegra_sm4_do_one_req,
#endif
		}
	}, {
		.alg.skcipher = {
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			.base = {
#endif
			.init			= tegra_sm4_cra_init,
			.exit			= tegra_sm4_cra_exit,
			.setkey			= tegra_sm4_setkey,
			.encrypt		= tegra_sm4_encrypt,
			.decrypt		= tegra_sm4_decrypt,
			.min_keysize		= AES_MIN_KEY_SIZE,
			.max_keysize		= AES_MAX_KEY_SIZE,
			.ivsize			= AES_BLOCK_SIZE,
			.base = {
				.cra_name	   = "cbc(sm4)",
				.cra_driver_name   = "cbc-sm4-tegra",
				.cra_priority	   = 500,
				.cra_flags	   = CRYPTO_ALG_TYPE_SKCIPHER |
						    CRYPTO_ALG_ASYNC,
				.cra_blocksize	   = AES_BLOCK_SIZE,
				.cra_ctxsize	   = sizeof(struct tegra_sm4_ctx),
				.cra_alignmask	   = 0,
				.cra_module	   = THIS_MODULE,
			},
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			},
			.op.do_one_request = tegra_sm4_do_one_req,
#endif
		}
	}, {
		.alg.skcipher = {
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			.base = {
#endif
			.init			= tegra_sm4_cra_init,
			.exit			= tegra_sm4_cra_exit,
			.setkey			= tegra_sm4_setkey,
			.encrypt		= tegra_sm4_encrypt,
			.decrypt		= tegra_sm4_decrypt,
			.min_keysize		= AES_MIN_KEY_SIZE,
			.max_keysize		= AES_MAX_KEY_SIZE,
			.ivsize			= AES_BLOCK_SIZE,
			.base = {
				.cra_name	   = "ecb(sm4)",
				.cra_driver_name   = "ecb-sm4-tegra",
				.cra_priority	   = 500,
				.cra_flags	   = CRYPTO_ALG_TYPE_SKCIPHER |
						    CRYPTO_ALG_ASYNC,
				.cra_blocksize	   = AES_BLOCK_SIZE,
				.cra_ctxsize	   = sizeof(struct tegra_sm4_ctx),
				.cra_alignmask	   = 0,
				.cra_module	   = THIS_MODULE,
			},
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			},
			.op.do_one_request = tegra_sm4_do_one_req,
#endif
		}
	}, {
		.alg.skcipher = {
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			.base = {
#endif
			.init			= tegra_sm4_cra_init,
			.exit			= tegra_sm4_cra_exit,
			.setkey			= tegra_sm4_setkey,
			.encrypt		= tegra_sm4_encrypt,
			.decrypt		= tegra_sm4_decrypt,
			.min_keysize		= AES_MIN_KEY_SIZE,
			.max_keysize		= AES_MAX_KEY_SIZE,
			.ivsize			= AES_BLOCK_SIZE,
			.base = {
				.cra_name	   = "ctr(sm4)",
				.cra_driver_name   = "ctr-sm4-tegra",
				.cra_priority	   = 500,
				.cra_flags	   = CRYPTO_ALG_TYPE_SKCIPHER |
						    CRYPTO_ALG_ASYNC,
				.cra_blocksize	   = AES_BLOCK_SIZE,
				.cra_ctxsize	   = sizeof(struct tegra_sm4_ctx),
				.cra_alignmask	   = 0,
				.cra_module	   = THIS_MODULE,
			},
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			},
			.op.do_one_request = tegra_sm4_do_one_req,
#endif
		}
	}, {
		.alg.skcipher = {
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			.base = {
#endif
			.init			= tegra_sm4_cra_init,
			.exit			= tegra_sm4_cra_exit,
			.setkey			= tegra_sm4_setkey,
			.encrypt		= tegra_sm4_encrypt,
			.decrypt		= tegra_sm4_decrypt,
			.min_keysize		= AES_MIN_KEY_SIZE,
			.max_keysize		= AES_MAX_KEY_SIZE,
			.ivsize			= AES_BLOCK_SIZE,
			.base = {
				.cra_name	   = "ofb(sm4)",
				.cra_driver_name   = "ofb-sm4-tegra",
				.cra_priority	   = 500,
				.cra_flags	   = CRYPTO_ALG_TYPE_SKCIPHER |
						    CRYPTO_ALG_ASYNC,
				.cra_blocksize	   = AES_BLOCK_SIZE,
				.cra_ctxsize	   = sizeof(struct tegra_sm4_ctx),
				.cra_alignmask	   = 0,
				.cra_module	   = THIS_MODULE,
			},
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			},
			.op.do_one_request = tegra_sm4_do_one_req,
#endif
		}
	},
};

static int tegra_sm4_gmac_prep_cmd(struct tegra_se *se, u32 *cpuvaddr,
		struct tegra_sm4_gcm_reqctx *rctx)
{
	unsigned int i = 0, j;
	unsigned int data_count, res_bits;

	data_count = (rctx->assoclen/AES_BLOCK_SIZE);
	res_bits = (rctx->assoclen % AES_BLOCK_SIZE) * 8;

	/*
	 * Hardware processes data_count + 1 blocks.
	 * Reduce 1 block if there is no residue
	 */
	if (!res_bits)
		data_count--;

	cpuvaddr[i++] = host1x_opcode_setpayload(SE_CRYPTO_CTR_REG_COUNT);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->linear_ctr);
	for (j = 0; j < SE_CRYPTO_CTR_REG_COUNT; j++)
		cpuvaddr[i++] = rctx->iv[j];

	cpuvaddr[i++] = se_host1x_opcode_nonincr(se->hw->regs->last_blk, 1);
	cpuvaddr[i++] = SE_LAST_BLOCK_VAL(data_count) |
			SE_LAST_BLOCK_RES_BITS(res_bits);

	cpuvaddr[i++] = se_host1x_opcode_incr(se->hw->regs->config, 4);
	cpuvaddr[i++] = rctx->config;
	cpuvaddr[i++] = rctx->crypto_config;

	cpuvaddr[i++] = lower_32_bits(rctx->inbuf.addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(rctx->inbuf.addr)) |
			SE_ADDR_HI_SZ(rctx->assoclen);

	cpuvaddr[i++] = se_host1x_opcode_nonincr(se->hw->regs->op, 1);
	cpuvaddr[i++] = SE_AES_OP_WRSTALL | SE_AES_OP_FINAL |
			SE_AES_OP_INIT | SE_AES_OP_LASTBUF |
			SE_AES_OP_START;

	cpuvaddr[i++] = se_host1x_opcode_nonincr(host1x_uclass_incr_syncpt_r(), 1);
	cpuvaddr[i++] = host1x_uclass_incr_syncpt_cond_f(1) |
			host1x_uclass_incr_syncpt_indx_f(se->syncpt_id);

	dev_dbg(se->dev, "cfg %#x crypto cfg %#x\n", rctx->config, rctx->crypto_config);

	return i;
}

static int tegra_sm4_gcm_crypt_prep_cmd(struct tegra_se *se, u32 *cpuvaddr,
		struct tegra_sm4_gcm_reqctx *rctx)
{
	unsigned int i = 0, j;
	unsigned int data_count, res_bits;
	u32 op;

	data_count = (rctx->cryptlen/AES_BLOCK_SIZE);
	res_bits = (rctx->cryptlen % AES_BLOCK_SIZE) * 8;
	op = SE_AES_OP_WRSTALL | SE_AES_OP_FINAL |
	     SE_AES_OP_LASTBUF | SE_AES_OP_START;

	/*
	 * If there is no assoc data,
	 * this will be the init command
	 */
	if (!rctx->assoclen)
		op |= SE_AES_OP_INIT;

	/*
	 * Hardware processes data_count + 1 blocks.
	 * Reduce 1 block if there is no residue
	 */
	if (!res_bits)
		data_count--;

	cpuvaddr[i++] = host1x_opcode_setpayload(SE_CRYPTO_CTR_REG_COUNT);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->linear_ctr);
	for (j = 0; j < SE_CRYPTO_CTR_REG_COUNT; j++)
		cpuvaddr[i++] = rctx->iv[j];

	cpuvaddr[i++] = se_host1x_opcode_nonincr(se->hw->regs->last_blk, 1);
	cpuvaddr[i++] = SE_LAST_BLOCK_VAL(data_count) |
			SE_LAST_BLOCK_RES_BITS(res_bits);

	cpuvaddr[i++] = se_host1x_opcode_incr(se->hw->regs->config, 6);
	cpuvaddr[i++] = rctx->config;
	cpuvaddr[i++] = rctx->crypto_config;

	/* Source Address */
	cpuvaddr[i++] = lower_32_bits(rctx->inbuf.addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(rctx->inbuf.addr)) |
			SE_ADDR_HI_SZ(rctx->cryptlen);

	/* Destination Address */
	cpuvaddr[i++] = lower_32_bits(rctx->outbuf.addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(rctx->outbuf.addr)) |
			SE_ADDR_HI_SZ(rctx->cryptlen);

	cpuvaddr[i++] = se_host1x_opcode_nonincr(se->hw->regs->op, 1);
	cpuvaddr[i++] = op;

	cpuvaddr[i++] = se_host1x_opcode_nonincr(host1x_uclass_incr_syncpt_r(), 1);
	cpuvaddr[i++] = host1x_uclass_incr_syncpt_cond_f(1) |
			host1x_uclass_incr_syncpt_indx_f(se->syncpt_id);

	dev_dbg(se->dev, "cfg %#x crypto cfg %#x\n", rctx->config, rctx->crypto_config);

	return i;
}

static int tegra_sm4_gcm_prep_final_cmd(struct tegra_se *se, u32 *cpuvaddr,
		struct tegra_sm4_gcm_reqctx *rctx)
{
	int i = 0, j;
	u32 op;

	op = SE_AES_OP_WRSTALL | SE_AES_OP_FINAL |
	     SE_AES_OP_LASTBUF | SE_AES_OP_START;

	/*
	 * Set init for zero sized vector
	 */
	if (!rctx->assoclen && !rctx->cryptlen)
		op |= SE_AES_OP_INIT;

	cpuvaddr[i++] = se_host1x_opcode_incr(se->hw->regs->aad_len, 2);
	cpuvaddr[i++] = rctx->assoclen * 8;
	cpuvaddr[i++] = 0;

	cpuvaddr[i++] = se_host1x_opcode_incr(se->hw->regs->cryp_msg_len, 2);
	cpuvaddr[i++] = rctx->cryptlen * 8;
	cpuvaddr[i++] = 0;

	cpuvaddr[i++] = host1x_opcode_setpayload(SE_CRYPTO_CTR_REG_COUNT);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->linear_ctr);
	for (j = 0; j < SE_CRYPTO_CTR_REG_COUNT; j++)
		cpuvaddr[i++] = rctx->iv[j];

	cpuvaddr[i++] = se_host1x_opcode_incr(se->hw->regs->config, 6);
	cpuvaddr[i++] = rctx->config;
	cpuvaddr[i++] = rctx->crypto_config;
	cpuvaddr[i++] = lower_32_bits(rctx->inbuf.addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(rctx->outbuf.addr)) |
			SE_ADDR_HI_SZ(rctx->authsize);

	/* Destination Address */
	cpuvaddr[i++] = lower_32_bits(rctx->outbuf.addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(rctx->outbuf.addr)) |
			SE_ADDR_HI_SZ(rctx->authsize);

	cpuvaddr[i++] = se_host1x_opcode_nonincr(se->hw->regs->op, 1);
	cpuvaddr[i++] = op;

	cpuvaddr[i++] = se_host1x_opcode_nonincr(host1x_uclass_incr_syncpt_r(), 1);
	cpuvaddr[i++] = host1x_uclass_incr_syncpt_cond_f(1) |
			host1x_uclass_incr_syncpt_indx_f(se->syncpt_id);

	dev_dbg(se->dev, "cfg %#x crypto cfg %#x\n", rctx->config, rctx->crypto_config);


	return i;
}

static int tegra_sm4_gcm_do_gmac(struct tegra_sm4_gcm_ctx *ctx, struct tegra_sm4_gcm_reqctx *rctx)
{
	struct tegra_se *se = ctx->se;
	u32 *cpuvaddr = se->cmdbuf->addr;
	unsigned int nents, size;

	nents = sg_nents(rctx->src_sg);
	scatterwalk_map_and_copy(rctx->inbuf.buf,
			rctx->src_sg, 0, rctx->assoclen, 0);

	rctx->config = se->regcfg->cfg(ctx->mac_alg, rctx->encrypt);
	rctx->crypto_config = se->regcfg->crypto_cfg(ctx->mac_alg, rctx->encrypt) |
			      SE_AES_KEY_INDEX(rctx->key_id);

	size = tegra_sm4_gmac_prep_cmd(se, cpuvaddr, rctx);

	return tegra_se_host1x_submit(se, se->cmdbuf, size);
}

static int tegra_sm4_gcm_do_crypt(struct tegra_sm4_gcm_ctx *ctx, struct tegra_sm4_gcm_reqctx *rctx)
{
	struct tegra_se *se = ctx->se;
	u32 *cpuvaddr = se->cmdbuf->addr;
	int size, ret;

	scatterwalk_map_and_copy(rctx->inbuf.buf, rctx->src_sg,
				 rctx->assoclen, rctx->cryptlen, 0);

	rctx->config = se->regcfg->cfg(ctx->alg, rctx->encrypt);
	rctx->crypto_config = se->regcfg->crypto_cfg(ctx->alg, rctx->encrypt) |
			      SE_AES_KEY_INDEX(rctx->key_id);

	/* Prepare command and submit */
	size = tegra_sm4_gcm_crypt_prep_cmd(se, cpuvaddr, rctx);
	ret = tegra_se_host1x_submit(se, se->cmdbuf, size);
	if (ret)
		return ret;

	/* Copy the result */
	scatterwalk_map_and_copy(rctx->outbuf.buf, rctx->dst_sg,
				 rctx->assoclen, rctx->cryptlen, 1);

	return 0;
}

static int tegra_sm4_gcm_do_final(struct tegra_sm4_gcm_ctx *ctx, struct tegra_sm4_gcm_reqctx *rctx)
{
	struct tegra_se *se = ctx->se;
	u32 *cpuvaddr = se->cmdbuf->addr;
	int size, ret, off;

	rctx->config = se->regcfg->cfg(ctx->final_alg, rctx->encrypt);
	rctx->crypto_config = se->regcfg->crypto_cfg(ctx->final_alg, rctx->encrypt) |
			      SE_AES_KEY_INDEX(rctx->key_id);

	/* Prepare command and submit */
	size = tegra_sm4_gcm_prep_final_cmd(se, cpuvaddr, rctx);
	ret = tegra_se_host1x_submit(se, se->cmdbuf, size);
	if (ret)
		return ret;

	if (rctx->encrypt) {
		/* Copy the result */
		off = rctx->assoclen + rctx->cryptlen;
		scatterwalk_map_and_copy(rctx->outbuf.buf, rctx->dst_sg,
					 off, rctx->authsize, 1);
	}

	return 0;
}

static int tegra_sm4_gcm_hw_verify(struct tegra_sm4_gcm_ctx *ctx, struct tegra_sm4_gcm_reqctx *rctx, u8 *mac)
{
	struct tegra_se *se = ctx->se;
	u32 result, *cpuvaddr = se->cmdbuf->addr;
	int size, ret;

	memcpy(rctx->inbuf.buf, mac, rctx->authsize);
	rctx->inbuf.size = rctx->authsize;

	rctx->config = se->regcfg->cfg(ctx->verify_alg, rctx->encrypt);
	rctx->crypto_config = se->regcfg->crypto_cfg(ctx->verify_alg, rctx->encrypt) |
			      SE_AES_KEY_INDEX(rctx->key_id);

	/* Prepare command and submit */
	size = tegra_sm4_gcm_prep_final_cmd(se, cpuvaddr, rctx);
	ret = tegra_se_host1x_submit(se, se->cmdbuf, size);
	if (ret)
		return ret;

	memcpy(&result, rctx->outbuf.buf, 4);

	if (result != SE_GCM_VERIFY_OK)
		return -EBADMSG;

	return 0;
}

static int tegra_sm4_gcm_do_verify(struct tegra_sm4_gcm_ctx *ctx, struct tegra_sm4_gcm_reqctx *rctx)
{
	struct tegra_se *se = ctx->se;
	int off, ret;
	u8 mac[16];

	off = rctx->assoclen + rctx->cryptlen;
	scatterwalk_map_and_copy(mac, rctx->src_sg, off, rctx->authsize, 0);

	if (se->hw->support_aad_verify)
		ret = tegra_sm4_gcm_hw_verify(ctx, rctx, mac);
	else
		ret = crypto_memneq(rctx->outbuf.buf, mac, rctx->authsize);

	if (ret)
		return -EBADMSG;

	return 0;
}

static int tegra_sm4_gcm_do_one_req(struct crypto_engine *engine, void *areq)
{
	struct aead_request *req = container_of(areq, struct aead_request, base);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct tegra_sm4_gcm_ctx *ctx = crypto_aead_ctx(tfm);
	struct tegra_sm4_gcm_reqctx *rctx = aead_request_ctx(req);
	struct tegra_se *se = ctx->se;
	int ret;

	rctx->src_sg = req->src;
	rctx->dst_sg = req->dst;
	rctx->assoclen = req->assoclen;
	rctx->authsize = crypto_aead_authsize(tfm);

	if (rctx->encrypt)
		rctx->cryptlen = req->cryptlen;
	else
		rctx->cryptlen = req->cryptlen - ctx->authsize;

	/* Keys in ctx might be stored in KDS. Copy it to local keyslot */
	if (ctx->key_id)
		rctx->key_id = tegra_key_get_idx(ctx->se, ctx->key_id);

	if (!ctx->key_id || !rctx->key_id) {
		ret = tegra_key_submit_reserved_aes(ctx->se, ctx->key,
					ctx->keylen, ctx->alg, &rctx->key_id);
		if (ret)
			goto out;
	}

	rctx->inbuf.size = rctx->assoclen + rctx->authsize + rctx->cryptlen;
	rctx->inbuf.buf = dma_alloc_coherent(ctx->se->dev, rctx->inbuf.size,
					     &rctx->inbuf.addr, GFP_KERNEL);
	if (!rctx->inbuf.buf)
		goto key_free;

	rctx->outbuf.size = rctx->assoclen + rctx->authsize + rctx->cryptlen;
	rctx->outbuf.buf = dma_alloc_coherent(ctx->se->dev, rctx->outbuf.size,
					     &rctx->outbuf.addr, GFP_KERNEL);
	if (!rctx->outbuf.buf)
		goto inbuf_free;

	memcpy(rctx->iv, req->iv, GCM_AES_IV_SIZE);
	rctx->iv[3] = (1 << 24);

	/* If there is associated data perform GMAC operation */
	if (rctx->assoclen) {
		ret = tegra_sm4_gcm_do_gmac(ctx, rctx);
		if (ret)
			goto outbuf_free;
	}

	/* GCM Encryption/Decryption operation */
	if (rctx->cryptlen) {
		ret = tegra_sm4_gcm_do_crypt(ctx, rctx);
		if (ret)
			goto outbuf_free;
	}

	/* GCM_FINAL operation */
	/* Need not do FINAL operation if hw supports MAC verification */
	if (rctx->encrypt || !se->hw->support_aad_verify) {
		ret = tegra_sm4_gcm_do_final(ctx, rctx);
		if (ret)
			goto outbuf_free;
	}

	if (!rctx->encrypt)
		ret = tegra_sm4_gcm_do_verify(ctx, rctx);

outbuf_free:
	dma_free_coherent(ctx->se->dev, rctx->outbuf.size,
			  rctx->outbuf.buf, rctx->outbuf.addr);
inbuf_free:
	dma_free_coherent(ctx->se->dev, rctx->inbuf.size,
			  rctx->inbuf.buf, rctx->inbuf.addr);
key_free:
	if (tegra_key_is_reserved(rctx->key_id))
		tegra_key_invalidate_reserved(ctx->se, rctx->key_id, ctx->alg);
	else if (rctx->key_id != ctx->key_id)
		tegra_key_invalidate(ctx->se, rctx->key_id, ctx->alg);

out:
	crypto_finalize_aead_request(se->engine, req, ret);

	return 0;
}

static int tegra_sm4_gcm_cra_init(struct crypto_aead *tfm)
{
	struct tegra_sm4_gcm_ctx *ctx = crypto_aead_ctx(tfm);
	struct aead_alg *alg = crypto_aead_alg(tfm);
	struct tegra_se_alg *se_alg;

#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
	se_alg = container_of(alg, struct tegra_se_alg, alg.aead.base);
#else
	se_alg = container_of(alg, struct tegra_se_alg, alg.aead);
#endif

	crypto_aead_set_reqsize(tfm, sizeof(struct tegra_sm4_gcm_reqctx));

	ctx->se = se_alg->se_dev;
	ctx->key_id = 0;
	ctx->keylen = 0;
	ctx->alg = SE_ALG_SM4_GCM;
	ctx->final_alg = SE_ALG_SM4_GCM_FINAL;
	ctx->verify_alg = SE_ALG_SM4_GCM_VERIFY;
	ctx->mac_alg = SE_ALG_SM4_GMAC;

#ifndef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
	ctx->enginectx.op.do_one_request = tegra_sm4_gcm_do_one_req;
#endif

	return 0;
}

static int tegra_sm4_gcm_setauthsize(struct crypto_aead *tfm,  unsigned int authsize)
{
	struct tegra_sm4_gcm_ctx *ctx = crypto_aead_ctx(tfm);
	int ret;

	ret = crypto_gcm_check_authsize(authsize);
	if (ret)
		return ret;

	ctx->authsize = authsize;

	return 0;
}

static void tegra_sm4_gcm_cra_exit(struct crypto_aead *tfm)
{
	struct tegra_sm4_gcm_ctx *ctx = crypto_tfm_ctx(&tfm->base);

	if (ctx->key_id)
		tegra_key_invalidate(ctx->se, ctx->key_id, ctx->alg);
}

static int tegra_sm4_gcm_crypt(struct aead_request *req, bool encrypt)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct tegra_sm4_gcm_ctx *ctx = crypto_aead_ctx(tfm);
	struct tegra_sm4_gcm_reqctx *rctx = aead_request_ctx(req);

	rctx->encrypt = encrypt;

	return crypto_transfer_aead_request_to_engine(ctx->se->engine, req);
}

static int tegra_sm4_gcm_encrypt(struct aead_request *req)
{
	return tegra_sm4_gcm_crypt(req, true);
}

static int tegra_sm4_gcm_decrypt(struct aead_request *req)
{
	return tegra_sm4_gcm_crypt(req, false);
}

static int tegra_sm4_gcm_setkey(struct crypto_aead *tfm,
			       const u8 *key, u32 keylen)
{
	struct tegra_sm4_gcm_ctx *ctx = crypto_aead_ctx(tfm);

	if (aes_check_keylen(keylen)) {
		dev_err(ctx->se->dev, "key length validation failed\n");
		return -EINVAL;
	}

	return tegra_key_submit(ctx->se, key, keylen, ctx->alg, &ctx->key_id);
}

static int tegra_sm4_cmac_prep_cmd(struct tegra_se *se, u32 *cpuvaddr, struct tegra_sm4_cmac_reqctx *rctx)
{
	unsigned int data_count, res_bits = 0;
	int i = 0, j;
	u32 op;

	data_count = (rctx->datbuf.size / AES_BLOCK_SIZE);

	op = SE_AES_OP_WRSTALL | SE_AES_OP_START | SE_AES_OP_LASTBUF;

	if (!(rctx->task & SHA_UPDATE)) {
		op |= SE_AES_OP_FINAL;
		res_bits = (rctx->datbuf.size % AES_BLOCK_SIZE) * 8;
	}

	if (!res_bits && data_count)
		data_count--;

	if (rctx->task & SHA_FIRST) {
		op |= SE_AES_OP_INIT;
		rctx->task &= ~SHA_FIRST;

		cpuvaddr[i++] = host1x_opcode_setpayload(SE_CRYPTO_CTR_REG_COUNT);
		cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->linear_ctr);
		/* Load 0 IV */
		for (j = 0; j < SE_CRYPTO_CTR_REG_COUNT; j++)
			cpuvaddr[i++] = 0;
	}

	cpuvaddr[i++] = se_host1x_opcode_nonincr(se->hw->regs->last_blk, 1);
	cpuvaddr[i++] = SE_LAST_BLOCK_VAL(data_count) |
			SE_LAST_BLOCK_RES_BITS(res_bits);

	cpuvaddr[i++] = se_host1x_opcode_incr(se->hw->regs->config, 6);
	cpuvaddr[i++] = rctx->config;
	cpuvaddr[i++] = rctx->crypto_config;

	/* Source Address */
	cpuvaddr[i++] = lower_32_bits(rctx->datbuf.addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(rctx->datbuf.addr)) |
			SE_ADDR_HI_SZ(rctx->datbuf.size);

	/* Destination Address */
	cpuvaddr[i++] = rctx->digest.addr;
	cpuvaddr[i++] = (u32)(SE_ADDR_HI_MSB(upper_32_bits(rctx->digest.addr)) |
				SE_ADDR_HI_SZ(rctx->digest.size));

	cpuvaddr[i++] = se_host1x_opcode_nonincr(se->hw->regs->op, 1);
	cpuvaddr[i++] = op;

	cpuvaddr[i++] = se_host1x_opcode_nonincr(host1x_uclass_incr_syncpt_r(), 1);
	cpuvaddr[i++] = host1x_uclass_incr_syncpt_cond_f(1) |
			host1x_uclass_incr_syncpt_indx_f(se->syncpt_id);

	dev_dbg(se->dev, "cfg %#x\n", rctx->config);

	return i;
}

static void tegra_sm4_cmac_copy_result(struct tegra_se *se, struct tegra_sm4_cmac_reqctx *rctx)
{
	int i;

	for (i = 0; i < CMAC_RESULT_REG_COUNT; i++)
		rctx->result[i] = readl(se->base + se->hw->regs->result + (i * 4));
}

static void tegra_sm4_cmac_paste_result(struct tegra_se *se, struct tegra_sm4_cmac_reqctx *rctx)
{
	int i;

	for (i = 0; i < CMAC_RESULT_REG_COUNT; i++)
		writel(rctx->result[i],
		       se->base + se->hw->regs->result + (i * 4));
}

static int tegra_sm4_cmac_do_init(struct ahash_request *req)
{
	struct tegra_sm4_cmac_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sm4_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_se *se = ctx->se;
	int i;

	rctx->total_len = 0;
	rctx->datbuf.size = 0;
	rctx->residue.size = 0;
	rctx->key_id = 0;
	rctx->task = SHA_FIRST;
	rctx->blk_size = crypto_ahash_blocksize(tfm);
	rctx->digest.size = crypto_ahash_digestsize(tfm);

	rctx->digest.buf = dma_alloc_coherent(se->dev, rctx->digest.size,
				&rctx->digest.addr, GFP_KERNEL);
	if (!rctx->digest.buf)
		goto digbuf_fail;

	rctx->residue.buf = dma_alloc_coherent(se->dev, rctx->blk_size * 2,
					&rctx->residue.addr, GFP_KERNEL);
	if (!rctx->residue.buf)
		goto resbuf_fail;

	rctx->residue.size = 0;
	rctx->datbuf.size = 0;

	/* Clear any previous result */
	for (i = 0; i < CMAC_RESULT_REG_COUNT; i++)
		writel(0, se->base + se->hw->regs->result + (i * 4));

	return 0;

resbuf_fail:
	dma_free_coherent(se->dev, rctx->blk_size, rctx->digest.buf,
				rctx->digest.addr);
digbuf_fail:
	return -ENOMEM;
}

static int tegra_sm4_cmac_do_update(struct ahash_request *req)
{
	struct tegra_sm4_cmac_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sm4_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_se *se = ctx->se;
	unsigned int nblks, nresidue, size;
	int ret;

	nresidue = (req->nbytes + rctx->residue.size) % rctx->blk_size;
	nblks = (req->nbytes + rctx->residue.size) / rctx->blk_size;

	/*
	 * Reserve the last block as residue during final() to process.
	 */
	if (!nresidue && nblks) {
		nresidue += rctx->blk_size;
		nblks--;
	}

	rctx->src_sg = req->src;
	rctx->datbuf.size = (req->nbytes + rctx->residue.size) - nresidue;
	rctx->total_len += rctx->datbuf.size;
	rctx->config = se->regcfg->cfg(ctx->alg, 0);
	rctx->crypto_config = se->regcfg->crypto_cfg(ctx->alg, 0) |
			      SE_AES_KEY_INDEX(rctx->key_id);

	/*
	 * Keep one block and residue bytes in residue and
	 * return. The bytes will be processed in final()
	 */
	if (nblks < 1) {
		scatterwalk_map_and_copy(rctx->residue.buf + rctx->residue.size,
				rctx->src_sg, 0, req->nbytes, 0);

		rctx->residue.size += req->nbytes;
		return 0;
	}

	rctx->datbuf.buf = dma_alloc_coherent(se->dev, rctx->datbuf.size,
					      &rctx->datbuf.addr, GFP_KERNEL);
	if (!rctx->datbuf.buf)
		return -ENOMEM;

	/* Copy the previous residue first */
	if (rctx->residue.size)
		memcpy(rctx->datbuf.buf, rctx->residue.buf, rctx->residue.size);

	scatterwalk_map_and_copy(rctx->datbuf.buf + rctx->residue.size,
			rctx->src_sg, 0, req->nbytes - nresidue, 0);

	scatterwalk_map_and_copy(rctx->residue.buf, rctx->src_sg,
			req->nbytes - nresidue, nresidue, 0);

	/* Update residue value with the residue after current block */
	rctx->residue.size = nresidue;

	/*
	 * If this is not the first task, paste the previous copied
	 * intermediate results to the registers so that it gets picked up.
	 */
	if (!(rctx->task & SHA_FIRST))
		tegra_sm4_cmac_paste_result(ctx->se, rctx);

	size = tegra_sm4_cmac_prep_cmd(se, se->cmdbuf->addr, rctx);
	ret = tegra_se_host1x_submit(se, se->cmdbuf, size);
	tegra_sm4_cmac_copy_result(ctx->se, rctx);

	return ret;
}

static int tegra_sm4_cmac_do_final(struct ahash_request *req)
{
	struct tegra_sm4_cmac_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sm4_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_se *se = ctx->se;
	int ret = 0, i, size;

	rctx->datbuf.size = rctx->residue.size;
	rctx->total_len += rctx->residue.size;
	rctx->config = se->regcfg->cfg(ctx->final_alg, 0);

	if (rctx->residue.size) {
		rctx->datbuf.buf = dma_alloc_coherent(se->dev, rctx->residue.size,
						      &rctx->datbuf.addr, GFP_KERNEL);
		if (!rctx->datbuf.buf) {
			ret = -ENOMEM;
			goto out_free;
		}

		memcpy(rctx->datbuf.buf, rctx->residue.buf, rctx->residue.size);
	}

	/*
	 * If this is not the first task, paste the previous copied
	 * intermediate results to the registers so that it gets picked up.
	 */
	if (!(rctx->task & SHA_FIRST))
		tegra_sm4_cmac_paste_result(ctx->se, rctx);

	/* Prepare command and submit */
	size = tegra_sm4_cmac_prep_cmd(se, se->cmdbuf->addr, rctx);
	ret = tegra_se_host1x_submit(se, se->cmdbuf, size);
	if (ret)
		goto out;

	/* Read and clear Result register */
	memcpy(req->result, rctx->digest.buf, rctx->digest.size);

	for (i = 0; i < CMAC_RESULT_REG_COUNT; i++)
		writel(0, se->base + se->hw->regs->result + (i * 4));

	if (rctx->key_id != ctx->key_id)
		tegra_key_invalidate(ctx->se, rctx->key_id, ctx->alg);

out:
	if (rctx->residue.size)
		dma_free_coherent(se->dev, rctx->datbuf.size,
				rctx->datbuf.buf, rctx->datbuf.addr);
out_free:
	dma_free_coherent(se->dev, crypto_ahash_blocksize(tfm) * 2,
			rctx->residue.buf, rctx->residue.addr);
	dma_free_coherent(se->dev, rctx->digest.size, rctx->digest.buf,
			rctx->digest.addr);

	return ret;
}

static int tegra_sm4_cmac_do_one_req(struct crypto_engine *engine, void *areq)
{
	struct ahash_request *req = ahash_request_cast(areq);
	struct tegra_sm4_cmac_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sm4_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_se *se = ctx->se;
	int ret = -EINVAL;

	if (rctx->task & SHA_INIT) {
		ret = tegra_sm4_cmac_do_init(req);
		if (ret)
			goto out;

		rctx->task &= ~SHA_INIT;
	}

	/* Retrieve the key slot for CMAC */
	if (ctx->key_id)
		rctx->key_id = tegra_key_get_idx(ctx->se, ctx->key_id);

	if (!ctx->key_id || !rctx->key_id) {
		ret = tegra_key_submit_reserved_aes(ctx->se, ctx->key,
					ctx->keylen, ctx->alg, &rctx->key_id);
		if (ret)
			goto out;
	}

	if (rctx->task & SHA_UPDATE) {
		ret = tegra_sm4_cmac_do_update(req);
		if (ret)
			goto out;

		rctx->task &= ~SHA_UPDATE;
	}

	if (rctx->task & SHA_FINAL) {
		ret = tegra_sm4_cmac_do_final(req);
		if (ret)
			goto out;

		rctx->task &= ~SHA_FINAL;
	}

out:
	if (tegra_key_is_reserved(rctx->key_id))
		tegra_key_invalidate_reserved(ctx->se, rctx->key_id, ctx->alg);
	else if (rctx->key_id != ctx->key_id)
		tegra_key_invalidate(ctx->se, rctx->key_id, ctx->alg);

	crypto_finalize_hash_request(se->engine, req, ret);

	return ret;
}

static int tegra_sm4_cmac_cra_init(struct crypto_tfm *tfm)
{
	struct tegra_sm4_cmac_ctx *ctx = crypto_tfm_ctx(tfm);
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->__crt_alg);
	struct tegra_se_alg *se_alg;
	const char *algname;

	algname = crypto_tfm_alg_name(tfm);

#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
	se_alg = container_of(alg, struct tegra_se_alg, alg.ahash.base);
#else
	se_alg = container_of(alg, struct tegra_se_alg, alg.ahash);
#endif

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct tegra_sm4_cmac_reqctx));

	ctx->se = se_alg->se_dev;
	ctx->key_id = 0;
	ctx->alg = SE_ALG_SM4_CMAC;
	ctx->final_alg = SE_ALG_SM4_CMAC_FINAL;

#ifndef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
	ctx->enginectx.op.do_one_request = tegra_sm4_cmac_do_one_req;
#endif

	return 0;
}

static void tegra_sm4_cmac_cra_exit(struct crypto_tfm *tfm)
{
	struct tegra_sm4_cmac_ctx *ctx = crypto_tfm_ctx(tfm);

	tegra_key_invalidate(ctx->se, ctx->key_id, ctx->alg);
}

static int tegra_sm4_cmac_setkey(struct crypto_ahash *tfm, const u8 *key,
			     unsigned int keylen)
{
	struct tegra_sm4_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	int ret;

	if (aes_check_keylen(keylen)) {
		dev_err(ctx->se->dev, "key length validation failed\n");
		return -EINVAL;
	}

	ret = tegra_key_submit(ctx->se, key, keylen, ctx->alg, &ctx->key_id);
	if (ret) {
		ctx->keylen = keylen;
		memcpy(ctx->key, key, keylen);
	}

	return 0;
}

static int tegra_sm4_cmac_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sm4_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_sm4_cmac_reqctx *rctx = ahash_request_ctx(req);

	rctx->task = SHA_INIT;

	return crypto_transfer_hash_request_to_engine(ctx->se->engine, req);
}

static int tegra_sm4_cmac_update(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sm4_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_sm4_cmac_reqctx *rctx = ahash_request_ctx(req);

	rctx->task |= SHA_UPDATE;

	return crypto_transfer_hash_request_to_engine(ctx->se->engine, req);
}

static int tegra_sm4_cmac_final(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sm4_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_sm4_cmac_reqctx *rctx = ahash_request_ctx(req);

	rctx->task |= SHA_FINAL;

	return crypto_transfer_hash_request_to_engine(ctx->se->engine, req);
}

static int tegra_sm4_cmac_finup(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sm4_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_sm4_cmac_reqctx *rctx = ahash_request_ctx(req);

	rctx->task |= SHA_UPDATE | SHA_FINAL;

	return crypto_transfer_hash_request_to_engine(ctx->se->engine, req);
}

static int tegra_sm4_cmac_digest(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sm4_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_sm4_cmac_reqctx *rctx = ahash_request_ctx(req);

	rctx->task |= SHA_INIT | SHA_UPDATE | SHA_FINAL;

	return crypto_transfer_hash_request_to_engine(ctx->se->engine, req);
}

static int tegra_sm4_cmac_export(struct ahash_request *req, void *out)
{
	struct tegra_sm4_cmac_reqctx *rctx = ahash_request_ctx(req);

	memcpy(out, rctx, sizeof(*rctx));

	return 0;
}

static int tegra_sm4_cmac_import(struct ahash_request *req, const void *in)
{
	struct tegra_sm4_cmac_reqctx *rctx = ahash_request_ctx(req);

	memcpy(rctx, in, sizeof(*rctx));

	return 0;
}

static struct tegra_se_alg tegra_sm4_gcm_algs[] = {
	{
		.alg.aead = {
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			.base = {
#endif

			.init			= tegra_sm4_gcm_cra_init,
			.exit			= tegra_sm4_gcm_cra_exit,
			.setkey			= tegra_sm4_gcm_setkey,
			.setauthsize		= tegra_sm4_gcm_setauthsize,
			.encrypt		= tegra_sm4_gcm_encrypt,
			.decrypt		= tegra_sm4_gcm_decrypt,
			.maxauthsize		= AES_BLOCK_SIZE,
			.ivsize			= GCM_AES_IV_SIZE,
			.base = {
				.cra_name	   = "gcm(sm4)",
				.cra_driver_name   = "gcm-sm4-tegra",
				.cra_priority	   = 500,
				.cra_blocksize	   = AES_BLOCK_SIZE,
				.cra_ctxsize	   = sizeof(struct tegra_sm4_gcm_ctx),
				.cra_alignmask	   = 0,
				.cra_module	   = THIS_MODULE,
			},
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			},
			.op.do_one_request = tegra_sm4_gcm_do_one_req,
#endif
		}
	}
};

static struct tegra_se_alg tegra_sm4_cmac_algs[] = {
	{
		.alg.ahash = {
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			.base = {
#endif
			.init = tegra_sm4_cmac_init,
			.setkey	= tegra_sm4_cmac_setkey,
			.update = tegra_sm4_cmac_update,
			.final = tegra_sm4_cmac_final,
			.finup = tegra_sm4_cmac_finup,
			.digest = tegra_sm4_cmac_digest,
			.export = tegra_sm4_cmac_export,
			.import = tegra_sm4_cmac_import,
			.halg.digestsize = AES_BLOCK_SIZE,
			.halg.statesize = sizeof(struct tegra_sm4_cmac_reqctx),

			.halg.base = {
				.cra_name = "cmac(sm4)",
				.cra_driver_name = "cmac-sm4-tegra",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize = AES_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct tegra_sm4_cmac_ctx),
				.cra_alignmask = 0,
				.cra_module = THIS_MODULE,
				.cra_init = tegra_sm4_cmac_cra_init,
				.cra_exit = tegra_sm4_cmac_cra_exit,
			},
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			},
			.op.do_one_request = tegra_sm4_cmac_do_one_req,
#endif
		}
	}
};

struct tegra_se_regcfg tegra264_sm4_regcfg = {
	.cfg = tegra264_sm4_cfg,
	.crypto_cfg = tegra264_sm4_crypto_cfg,
	.manifest = tegra_sm4_kac2_manifest
};

#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
int tegra_init_sm4(struct tegra_se *se)
{
	struct aead_engine_alg *aead_alg;
	struct ahash_engine_alg *ahash_alg;
	struct skcipher_engine_alg *sk_alg;
	int i, ret;

	se->regcfg = &tegra264_sm4_regcfg;

	for (i = 0; i < ARRAY_SIZE(tegra_sm4_algs); i++) {
		tegra_sm4_algs[i].se_dev = se;
		sk_alg = &tegra_sm4_algs[i].alg.skcipher;
		ret = CRYPTO_REGISTER(skcipher, sk_alg);
		if (ret) {
			dev_err(se->dev, "failed to register %s\n",
				sk_alg->base.base.cra_name);
			goto sm4_err;
		}
	}

	for (i = 0; i < ARRAY_SIZE(tegra_sm4_gcm_algs); i++) {
		tegra_sm4_gcm_algs[i].se_dev = se;
		aead_alg = &tegra_sm4_gcm_algs[i].alg.aead;
		ret = CRYPTO_REGISTER(aead, aead_alg);
		if (ret) {
			dev_err(se->dev, "failed to register %s\n",
				aead_alg->base.base.cra_name);
			goto aead_err;
		}
	}

	for (i = 0; i < ARRAY_SIZE(tegra_sm4_cmac_algs); i++) {
		tegra_sm4_cmac_algs[i].se_dev = se;
		ahash_alg = &tegra_sm4_cmac_algs[i].alg.ahash;
		ret = CRYPTO_REGISTER(ahash, ahash_alg);
		if (ret) {
			dev_err(se->dev, "failed to register %s\n",
				ahash_alg->base.halg.base.cra_name);
			goto cmac_err;
		}
	}

	return 0;


cmac_err:
	for (--i; i >= 0; i--)
		CRYPTO_UNREGISTER(ahash, &tegra_sm4_cmac_algs[i].alg.ahash);

	i = ARRAY_SIZE(tegra_sm4_gcm_algs);
aead_err:
	for (--i; i >= 0; i--)
		CRYPTO_UNREGISTER(aead, &tegra_sm4_gcm_algs[i].alg.aead);

	i = ARRAY_SIZE(tegra_sm4_algs);
sm4_err:
	for (--i; i >= 0; i--)
		CRYPTO_UNREGISTER(skcipher, &tegra_sm4_algs[i].alg.skcipher);

	return ret;
}
#else
int tegra_init_sm4(struct tegra_se *se)
{
	struct aead_alg *aead_alg;
	struct ahash_alg *ahash_alg;
	struct skcipher_alg *sk_alg;
	int i, ret;

	se->regcfg = &tegra264_sm4_regcfg;

	for (i = 0; i < ARRAY_SIZE(tegra_sm4_algs); i++) {
		tegra_sm4_algs[i].se_dev = se;
		sk_alg = &tegra_sm4_algs[i].alg.skcipher;
		ret = CRYPTO_REGISTER(skcipher, sk_alg);
		if (ret) {
			dev_err(se->dev, "failed to register %s\n",
				sk_alg->base.cra_name);
			goto sm4_err;
		}
	}

	for (i = 0; i < ARRAY_SIZE(tegra_sm4_gcm_algs); i++) {
		tegra_sm4_gcm_algs[i].se_dev = se;
		aead_alg = &tegra_sm4_gcm_algs[i].alg.aead;
		ret = CRYPTO_REGISTER(aead, aead_alg);
		if (ret) {
			dev_err(se->dev, "failed to register %s\n",
				aead_alg->base.cra_name);
			goto aead_err;
		}
	}

	for (i = 0; i < ARRAY_SIZE(tegra_sm4_cmac_algs); i++) {
		tegra_sm4_cmac_algs[i].se_dev = se;
		ahash_alg = &tegra_sm4_cmac_algs[i].alg.ahash;
		ret = CRYPTO_REGISTER(ahash, ahash_alg);
		if (ret) {
			dev_err(se->dev, "failed to register %s\n",
				ahash_alg->halg.base.cra_name);
			goto cmac_err;
		}
	}

	return 0;


cmac_err:
	for (--i; i >= 0; i--)
		CRYPTO_UNREGISTER(ahash, &tegra_sm4_cmac_algs[i].alg.ahash);

	i = ARRAY_SIZE(tegra_sm4_gcm_algs);
aead_err:
	for (--i; i >= 0; i--)
		CRYPTO_UNREGISTER(aead, &tegra_sm4_gcm_algs[i].alg.aead);

	i = ARRAY_SIZE(tegra_sm4_algs);
sm4_err:
	for (--i; i >= 0; i--)
		CRYPTO_UNREGISTER(skcipher, &tegra_sm4_algs[i].alg.skcipher);

	return ret;
}
#endif

void tegra_deinit_sm4(struct tegra_se *se)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tegra_sm4_gcm_algs); i++)
		CRYPTO_UNREGISTER(aead, &tegra_sm4_gcm_algs[i].alg.aead);

	for (i = 0; i < ARRAY_SIZE(tegra_sm4_cmac_algs); i++)
		CRYPTO_UNREGISTER(ahash, &tegra_sm4_cmac_algs[i].alg.ahash);

	for (i = 0; i < ARRAY_SIZE(tegra_sm4_algs); i++)
		CRYPTO_UNREGISTER(skcipher, &tegra_sm4_algs[i].alg.skcipher);
}
