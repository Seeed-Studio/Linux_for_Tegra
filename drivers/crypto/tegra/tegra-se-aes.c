// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * Crypto driver to handle block cipher algorithms using NVIDIA Security Engine.
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

struct tegra_aes_ctx {
#ifndef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
	struct crypto_engine_ctx enginectx;
#endif
	struct tegra_se *se;
	u32 alg;
	u32 keylen;
	u32 ivsize;
	u32 key1_id;
	u32 key2_id;
};

struct tegra_aes_reqctx {
	struct tegra_se_datbuf datbuf;
	struct tegra_se *se;
	bool encrypt;
	u32 cfg;
	u32 crypto_cfg;
};

struct tegra_aead_ctx {
#ifndef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
	struct crypto_engine_ctx enginectx;
#endif
	struct tegra_se *se;
	unsigned int authsize;
	u32 alg;
	u32 keylen;
	u32 key_id;
};

struct tegra_aead_reqctx {
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

struct tegra_cmac_ctx {
#ifndef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
	struct crypto_engine_ctx enginectx;
#endif
	struct crypto_shash *fallback_tfm;
	struct tegra_se *se;
	unsigned int alg;
	u32 key_id;
};

struct tegra_cmac_reqctx {
	struct scatterlist *src_sg;
	struct tegra_se_datbuf datbuf;
	struct tegra_se_datbuf residue;
	unsigned int total_len;
	unsigned int blk_size;
	unsigned int task;
	u32 crypto_config;
	u32 config;
	u32 key_id;
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

static void tegra_cbc_iv_copyback(struct skcipher_request *req, struct tegra_aes_ctx *ctx)
{
	struct tegra_aes_reqctx *rctx = skcipher_request_ctx(req);
	unsigned int off;

	off = req->cryptlen - ctx->ivsize;

	if (rctx->encrypt)
		memcpy(req->iv, rctx->datbuf.buf + off, ctx->ivsize);
	else
		sg_pcopy_to_buffer(req->src, sg_nents(req->src),
				   req->iv, ctx->ivsize, off);
}

static void tegra_aes_update_iv(struct skcipher_request *req, struct tegra_aes_ctx *ctx)
{
	int sz;

	if (ctx->alg == SE_ALG_CBC) {
		tegra_cbc_iv_copyback(req, ctx);
	} else if (ctx->alg == SE_ALG_CTR) {
		sz = req->cryptlen / ctx->ivsize;
		if (req->cryptlen % ctx->ivsize)
			sz++;

		ctr_iv_inc(req->iv, ctx->ivsize, sz);
	}
}

static int tegra234_aes_crypto_cfg(u32 alg, bool encrypt)
{
	switch (alg) {
	case SE_ALG_CMAC:
	case SE_ALG_GMAC:
	case SE_ALG_GCM:
	case SE_ALG_GCM_FINAL:
		return 0;
	case SE_ALG_CBC:
		if (encrypt)
			return SE_CRYPTO_CFG_CBC_ENCRYPT;
		else
			return SE_CRYPTO_CFG_CBC_DECRYPT;
	case SE_ALG_ECB:
		if (encrypt)
			return SE_CRYPTO_CFG_ECB_ENCRYPT;
		else
			return SE_CRYPTO_CFG_ECB_DECRYPT;
	case SE_ALG_XTS:
		if (encrypt)
			return SE_CRYPTO_CFG_XTS_ENCRYPT;
		else
			return SE_CRYPTO_CFG_XTS_DECRYPT;

	case SE_ALG_CTR:
		return SE_CRYPTO_CFG_CTR;
	case SE_ALG_OFB:
		return SE_CRYPTO_CFG_OFB;
	case SE_ALG_CBC_MAC:
		return SE_CRYPTO_CFG_CBC_MAC;

	default:
		break;
	}

	return -EINVAL;
}

static int tegra234_aes_cfg(u32 alg, bool encrypt)
{
	switch (alg) {
	case SE_ALG_CBC:
	case SE_ALG_ECB:
	case SE_ALG_XTS:
	case SE_ALG_CTR:
	case SE_ALG_OFB:
		if (encrypt)
			return SE_CFG_AES_ENCRYPT;
		else
			return SE_CFG_AES_DECRYPT;

	case SE_ALG_GMAC:
		if (encrypt)
			return SE_CFG_GMAC_ENCRYPT;
		else
			return SE_CFG_GMAC_DECRYPT;

	case SE_ALG_GCM:
		if (encrypt)
			return SE_CFG_GCM_ENCRYPT;
		else
			return SE_CFG_GCM_DECRYPT;

	case SE_ALG_GCM_FINAL:
		if (encrypt)
			return SE_CFG_GCM_FINAL_ENCRYPT;
		else
			return SE_CFG_GCM_FINAL_DECRYPT;

	case SE_ALG_CMAC:
		return SE_CFG_CMAC;

	case SE_ALG_CBC_MAC:
		return SE_AES_ENC_ALG_AES_ENC |
		       SE_AES_DST_HASH_REG;

	}
	return -EINVAL;

}

static int tegra_aes_prep_cmd(struct tegra_se *se, u32 *cpuvaddr, u32 *iv,
				     int len, dma_addr_t addr, int cfg, int cryp_cfg)
{
	int i = 0, j;

	if (iv) {
		cpuvaddr[i++] = host1x_opcode_setpayload(SE_CRYPTO_CTR_REG_COUNT);
		cpuvaddr[i++] = host1x_opcode_incr_w(se->hw->regs->linear_ctr);
		for (j = 0; j < SE_CRYPTO_CTR_REG_COUNT; j++)
			cpuvaddr[i++] = iv[j];
	}

	cpuvaddr[i++] = host1x_opcode_nonincr(se->hw->regs->last_blk, 1);
	cpuvaddr[i++] = (len / AES_BLOCK_SIZE) - 1;
	cpuvaddr[i++] = host1x_opcode_incr(se->hw->regs->config, 6);
	cpuvaddr[i++] = cfg;
	cpuvaddr[i++] = cryp_cfg;

	/* Source address setting */
	cpuvaddr[i++] = lower_32_bits(addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(addr)) | SE_ADDR_HI_SZ(len);

	/* Destination address setting */
	cpuvaddr[i++] = lower_32_bits(addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(addr)) |
			SE_ADDR_HI_SZ(len);

	cpuvaddr[i++] = host1x_opcode_nonincr(se->hw->regs->op, 1);
	cpuvaddr[i++] = SE_AES_OP_WRSTALL | SE_AES_OP_LASTBUF |
			SE_AES_OP_START;

	cpuvaddr[i++] = host1x_opcode_nonincr(host1x_uclass_incr_syncpt_r(), 1);
	cpuvaddr[i++] = host1x_uclass_incr_syncpt_cond_f(1) |
			host1x_uclass_incr_syncpt_indx_f(se->syncpt_id);

	dev_dbg(se->dev, "cfg %#x crypto cfg %#x\n", cfg, cryp_cfg);

	return i;
}

static int tegra_aes_prep_req(struct crypto_engine *engine, void *areq)
{
	struct skcipher_request *req;
	struct tegra_aes_ctx *ctx;
	struct tegra_aes_reqctx *rctx;
	int ret;

	req = container_of(areq, struct skcipher_request, base);
	ctx = crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	rctx = skcipher_request_ctx(req);

	rctx->datbuf.buf = dma_alloc_coherent(ctx->se->dev, SE_AES_BUFLEN,
					     &rctx->datbuf.addr, GFP_KERNEL);
	if (!rctx->datbuf.buf) {
		ret = -ENOMEM;
		goto buf_err;
	}

	rctx->datbuf.size = SE_AES_BUFLEN;
	memset(rctx->datbuf.buf, 0, SE_AES_BUFLEN);

	return 0;

buf_err:
	crypto_finalize_skcipher_request(ctx->se->engine, req, ret);

	return ret;
}

static int tegra_aes_unprep_req(struct crypto_engine *engine, void *areq)
{
	struct skcipher_request *req;
	struct tegra_aes_ctx *ctx;
	struct tegra_aes_reqctx *rctx;

	req = container_of(areq, struct skcipher_request, base);
	ctx = crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	rctx = skcipher_request_ctx(req);

	dma_free_coherent(ctx->se->dev, SE_AES_BUFLEN,
			  rctx->datbuf.buf, rctx->datbuf.addr);

	return 0;
}

static int tegra_aes_do_one_req(struct crypto_engine *engine, void *areq)
{
	unsigned int len, src_nents, dst_nents, size;
	u32 *cpuvaddr, *iv, config, crypto_config;
	struct tegra_aes_reqctx *rctx;
	struct skcipher_request *req;
	struct tegra_aes_ctx *ctx;
	struct tegra_se *se;
	int ret;

#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
	ret = tegra_aes_prep_req(engine, areq);
	if (ret != 0)
		return ret;
#endif

	req = container_of(areq, struct skcipher_request, base);
	rctx = skcipher_request_ctx(req);
	ctx = crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	se = ctx->se;
	iv = (u32 *)req->iv;

	cpuvaddr = se->cmdbuf->addr;
	len = req->cryptlen;

	/* Pad input to AES Block size */
	if (len % AES_BLOCK_SIZE)
		len += AES_BLOCK_SIZE - (len % AES_BLOCK_SIZE);

	src_nents = sg_nents(req->src);
	sg_copy_to_buffer(req->src, src_nents, rctx->datbuf.buf, req->cryptlen);

	config = tegra234_aes_cfg(ctx->alg, rctx->encrypt);
	crypto_config = tegra234_aes_crypto_cfg(ctx->alg, rctx->encrypt);
	crypto_config |= SE_AES_KEY_INDEX(ctx->key1_id);
	if (ctx->key2_id)
		crypto_config |= SE_AES_KEY2_INDEX(ctx->key2_id);

	size = tegra_aes_prep_cmd(se, cpuvaddr, iv, len, rctx->datbuf.addr,
					 config, crypto_config);

	ret = tegra_se_host1x_submit(se, size);

	/* Copy the result */
	dst_nents = sg_nents(req->dst);
	tegra_aes_update_iv(req, ctx);
	sg_copy_from_buffer(req->dst, dst_nents, rctx->datbuf.buf, req->cryptlen);

	crypto_finalize_skcipher_request(se->engine, req, ret);

#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
	if (ret != 0)
		return ret;

	ret = tegra_aes_unprep_req(engine, areq);
#endif
	return ret;
}

static int tegra_aes_cra_init(struct crypto_skcipher *tfm)
{
	struct tegra_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);
	struct tegra_se_alg *se_alg;
	const char *algname;
	int ret;

#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
	se_alg = container_of(alg, struct tegra_se_alg, alg.skcipher.base);
#else
	se_alg = container_of(alg, struct tegra_se_alg, alg.skcipher);
#endif

	crypto_skcipher_set_reqsize(tfm, sizeof(struct tegra_aes_reqctx));

	ctx->se = se_alg->se_dev;
	ctx->key1_id = 0;
	ctx->key2_id = 0;
	ctx->ivsize = crypto_skcipher_ivsize(tfm);

	algname = crypto_tfm_alg_name(&tfm->base);
	ret = se_algname_to_algid(algname);
	if (ret < 0) {
		dev_err(ctx->se->dev, "Invalid algorithm\n");
		return ret;
	}

	ctx->alg = ret;
#ifndef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
	ctx->enginectx.op.prepare_request = tegra_aes_prep_req;
	ctx->enginectx.op.do_one_request = tegra_aes_do_one_req;
	ctx->enginectx.op.unprepare_request = tegra_aes_unprep_req;
#endif

	return 0;
}

static void tegra_aes_cra_exit(struct crypto_skcipher *tfm)
{
	struct tegra_aes_ctx *ctx = crypto_tfm_ctx(&tfm->base);

	if (ctx->key1_id)
		tegra_key_invalidate(ctx->se, ctx->key1_id);

	if (ctx->key2_id)
		tegra_key_invalidate(ctx->se, ctx->key2_id);
}

static int tegra_aes_setkey(struct crypto_skcipher *tfm,
			       const u8 *key, u32 keylen)
{
	struct tegra_aes_ctx *ctx = crypto_skcipher_ctx(tfm);

	if (aes_check_keylen(keylen)) {
		dev_err(ctx->se->dev, "key length validation failed\n");
		return -EINVAL;
	}

	return tegra_key_submit(ctx->se, key, keylen, ctx->alg, &ctx->key1_id);
}

static int tegra_xts_setkey(struct crypto_skcipher *tfm,
			       const u8 *key, u32 keylen)
{
	struct tegra_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	int ret;
	u32 len;

	len = keylen / 2;
	if (aes_check_keylen(len)) {
		dev_err(ctx->se->dev, "key length validation failed\n");
		return -EINVAL;
	}

	ret = tegra_key_submit(ctx->se, key, len,
			ctx->alg, &ctx->key1_id);
	if (ret)
		return ret;

	ret = tegra_key_submit(ctx->se, key + len, len,
			ctx->alg, &ctx->key2_id);
	if (ret)
		return ret;

	return 0;
}

static int tegra_aes_kac_manifest(u32 user, u32 alg, u32 keylen)
{
	int manifest;

	manifest = SE_KAC_USER_NS;

	switch (alg) {
	case SE_ALG_CBC:
	case SE_ALG_ECB:
	case SE_ALG_CTR:
	case SE_ALG_OFB:
		manifest |= SE_KAC_ENC;
		break;
	case SE_ALG_XTS:
		manifest |= SE_KAC_XTS;
		break;
	case SE_ALG_GCM:
		manifest |= SE_KAC_GCM;
		break;
	case SE_ALG_CMAC:
		manifest |= SE_KAC_CMAC;
		break;
	case SE_ALG_CBC_MAC:
		manifest |= SE_KAC_ENC;
		break;
	default:
		return -EINVAL;
	}

	switch (keylen) {
	case AES_KEYSIZE_128:
		manifest |= SE_KAC_SIZE_128;
		break;
	case AES_KEYSIZE_192:
		manifest |= SE_KAC_SIZE_192;
		break;
	case AES_KEYSIZE_256:
		manifest |= SE_KAC_SIZE_256;
		break;
	default:
		return -EINVAL;
	}

	return manifest;
}

static int tegra_aes_encrypt(struct skcipher_request *req)
{
	struct tegra_aes_ctx *ctx;
	struct tegra_aes_reqctx *rctx;

	ctx  = crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	rctx = skcipher_request_ctx(req);
	rctx->encrypt = true;

	return crypto_transfer_skcipher_request_to_engine(ctx->se->engine, req);
}

static int tegra_aes_decrypt(struct skcipher_request *req)
{
	struct tegra_aes_ctx *ctx;
	struct tegra_aes_reqctx *rctx;

	ctx  = crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	rctx = skcipher_request_ctx(req);
	rctx->encrypt = false;

	return crypto_transfer_skcipher_request_to_engine(ctx->se->engine, req);
}

static struct tegra_se_alg tegra_aes_algs[] = {
	{
		.alg.skcipher = {
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			.base = {
#endif
			.init			= tegra_aes_cra_init,
			.exit			= tegra_aes_cra_exit,
			.setkey			= tegra_xts_setkey,
			.encrypt		= tegra_aes_encrypt,
			.decrypt		= tegra_aes_decrypt,
			.min_keysize		= 2 * AES_MIN_KEY_SIZE,
			.max_keysize		= 2 * AES_MAX_KEY_SIZE,
			.ivsize			= AES_BLOCK_SIZE,

			.base = {
				.cra_name	   = "xts(aes)",
				.cra_driver_name   = "xts-aes-tegra",
				.cra_priority	   = 500,
				.cra_flags	   = CRYPTO_ALG_TYPE_SKCIPHER |
						    CRYPTO_ALG_ASYNC,
				.cra_blocksize	   = AES_BLOCK_SIZE,
				.cra_ctxsize	   = sizeof(struct tegra_aes_ctx),
				.cra_alignmask	   = 0,
				.cra_module	   = THIS_MODULE,
			},
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			},
			.op.do_one_request = tegra_aes_do_one_req,
#endif
		}
	}, {
		.alg.skcipher = {
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			.base = {
#endif
			.init			= tegra_aes_cra_init,
			.exit			= tegra_aes_cra_exit,
			.setkey			= tegra_aes_setkey,
			.encrypt		= tegra_aes_encrypt,
			.decrypt		= tegra_aes_decrypt,
			.min_keysize		= AES_MIN_KEY_SIZE,
			.max_keysize		= AES_MAX_KEY_SIZE,
			.ivsize			= AES_BLOCK_SIZE,

			.base = {
				.cra_name	   = "cbc(aes)",
				.cra_driver_name   = "cbc-aes-tegra",
				.cra_priority	   = 500,
				.cra_flags	   = CRYPTO_ALG_TYPE_SKCIPHER |
						    CRYPTO_ALG_ASYNC,
				.cra_blocksize	   = AES_BLOCK_SIZE,
				.cra_ctxsize	   = sizeof(struct tegra_aes_ctx),
				.cra_alignmask	   = 0,
				.cra_module	   = THIS_MODULE,
			},
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			},
			.op.do_one_request = tegra_aes_do_one_req,
#endif
		}
	}, {
		.alg.skcipher = {
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			.base = {
#endif
			.init			= tegra_aes_cra_init,
			.exit			= tegra_aes_cra_exit,
			.setkey			= tegra_aes_setkey,
			.encrypt		= tegra_aes_encrypt,
			.decrypt		= tegra_aes_decrypt,
			.min_keysize		= AES_MIN_KEY_SIZE,
			.max_keysize		= AES_MAX_KEY_SIZE,
			.ivsize			= AES_BLOCK_SIZE,

			.base = {
				.cra_name	   = "ecb(aes)",
				.cra_driver_name   = "ecb-aes-tegra",
				.cra_priority	   = 500,
				.cra_flags	   = CRYPTO_ALG_TYPE_SKCIPHER |
						    CRYPTO_ALG_ASYNC,
				.cra_blocksize	   = AES_BLOCK_SIZE,
				.cra_ctxsize	   = sizeof(struct tegra_aes_ctx),
				.cra_alignmask	   = 0,
				.cra_module	   = THIS_MODULE,
			},
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			},
			.op.do_one_request = tegra_aes_do_one_req,
#endif
		}
	}, {
		.alg.skcipher = {
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			.base = {
#endif
			.init			= tegra_aes_cra_init,
			.exit			= tegra_aes_cra_exit,
			.setkey			= tegra_aes_setkey,
			.encrypt		= tegra_aes_encrypt,
			.decrypt		= tegra_aes_decrypt,
			.min_keysize		= AES_MIN_KEY_SIZE,
			.max_keysize		= AES_MAX_KEY_SIZE,
			.ivsize			= AES_BLOCK_SIZE,

			.base = {
				.cra_name	   = "ctr(aes)",
				.cra_driver_name   = "ctr-aes-tegra",
				.cra_priority	   = 500,
				.cra_flags	   = CRYPTO_ALG_TYPE_SKCIPHER |
						    CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize	   = AES_BLOCK_SIZE,
				.cra_ctxsize	   = sizeof(struct tegra_aes_ctx),
				.cra_alignmask	   = 0,
				.cra_module	   = THIS_MODULE,
			},
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			},
			.op.do_one_request = tegra_aes_do_one_req,
#endif
		}
	}, {
		.alg.skcipher = {
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			.base = {
#endif
			.init			= tegra_aes_cra_init,
			.exit			= tegra_aes_cra_exit,
			.setkey			= tegra_aes_setkey,
			.encrypt		= tegra_aes_encrypt,
			.decrypt		= tegra_aes_decrypt,
			.min_keysize		= AES_MIN_KEY_SIZE,
			.max_keysize		= AES_MAX_KEY_SIZE,
			.ivsize			= AES_BLOCK_SIZE,

			.base = {
				.cra_name	   = "ofb(aes)",
				.cra_driver_name   = "ofb-aes-tegra",
				.cra_priority	   = 500,
				.cra_flags	   = CRYPTO_ALG_TYPE_SKCIPHER |
						    CRYPTO_ALG_ASYNC,
				.cra_blocksize	   = AES_BLOCK_SIZE,
				.cra_ctxsize	   = sizeof(struct tegra_aes_ctx),
				.cra_alignmask	   = 0,
				.cra_module	   = THIS_MODULE,
			},
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			},
			.op.do_one_request = tegra_aes_do_one_req,
#endif
		}
	},
};

static int tegra_gmac_prep_cmd(struct tegra_se *se, u32 *cpuvaddr,
		struct tegra_aead_reqctx *rctx)
{
	unsigned int i = 0;
	unsigned int data_count, res_bits;

	data_count = (rctx->assoclen/AES_BLOCK_SIZE);
	res_bits = (rctx->assoclen % AES_BLOCK_SIZE) * 8;

	/*
	 * Hardware processes data_count + 1 blocks.
	 * Reduce 1 block if there is no residue
	 */
	if (!res_bits)
		data_count--;

	cpuvaddr[i++] = host1x_opcode_nonincr(se->hw->regs->last_blk, 1);
	cpuvaddr[i++] = SE_LAST_BLOCK_VAL(data_count) |
			SE_LAST_BLOCK_RES_BITS(res_bits);

	cpuvaddr[i++] = host1x_opcode_incr(se->hw->regs->config, 4);
	cpuvaddr[i++] = rctx->config;
	cpuvaddr[i++] = rctx->crypto_config;
	cpuvaddr[i++] = lower_32_bits(rctx->inbuf.addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(rctx->inbuf.addr)) |
			SE_ADDR_HI_SZ(rctx->assoclen);

	cpuvaddr[i++] = host1x_opcode_nonincr(se->hw->regs->op, 1);
	cpuvaddr[i++] = SE_AES_OP_WRSTALL | SE_AES_OP_FINAL |
			SE_AES_OP_INIT | SE_AES_OP_LASTBUF |
			SE_AES_OP_START;

	cpuvaddr[i++] = host1x_opcode_nonincr(host1x_uclass_incr_syncpt_r(), 1);
	cpuvaddr[i++] = host1x_uclass_incr_syncpt_cond_f(1) |
			host1x_uclass_incr_syncpt_indx_f(se->syncpt_id);

	return i;
}

static int tegra_gcm_crypt_prep_cmd(struct tegra_se *se, u32 *cpuvaddr,
		struct tegra_aead_reqctx *rctx)
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
	cpuvaddr[i++] = host1x_opcode_incr_w(se->hw->regs->linear_ctr);
	for (j = 0; j < SE_CRYPTO_CTR_REG_COUNT; j++)
		cpuvaddr[i++] = rctx->iv[j];

	cpuvaddr[i++] = host1x_opcode_nonincr(se->hw->regs->last_blk, 1);
	cpuvaddr[i++] = SE_LAST_BLOCK_VAL(data_count) |
			SE_LAST_BLOCK_RES_BITS(res_bits);

	cpuvaddr[i++] = host1x_opcode_incr(se->hw->regs->config, 6);
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

	cpuvaddr[i++] = host1x_opcode_nonincr(se->hw->regs->op, 1);
	cpuvaddr[i++] = op;

	cpuvaddr[i++] = host1x_opcode_nonincr(host1x_uclass_incr_syncpt_r(), 1);
	cpuvaddr[i++] = host1x_uclass_incr_syncpt_cond_f(1) |
			host1x_uclass_incr_syncpt_indx_f(se->syncpt_id);

	dev_dbg(se->dev, "cfg %#x crypto cfg %#x\n", rctx->config, rctx->crypto_config);
	return i;
}

static int tegra_gcm_prep_final_cmd(struct tegra_se *se, u32 *cpuvaddr,
		struct tegra_aead_reqctx *rctx)
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


	cpuvaddr[i++] = host1x_opcode_incr(se->hw->regs->aad_len, 2);
	cpuvaddr[i++] = rctx->assoclen * 8;
	cpuvaddr[i++] = 0;

	cpuvaddr[i++] = host1x_opcode_incr(se->hw->regs->cryp_msg_len, 2);
	cpuvaddr[i++] = rctx->cryptlen * 8;
	cpuvaddr[i++] = 0;

	cpuvaddr[i++] = host1x_opcode_setpayload(SE_CRYPTO_CTR_REG_COUNT);
	cpuvaddr[i++] = host1x_opcode_incr_w(se->hw->regs->linear_ctr);
	for (j = 0; j < SE_CRYPTO_CTR_REG_COUNT; j++)
		cpuvaddr[i++] = rctx->iv[j];

	cpuvaddr[i++] = host1x_opcode_incr(se->hw->regs->config, 6);
	cpuvaddr[i++] = rctx->config;
	cpuvaddr[i++] = rctx->crypto_config;
	cpuvaddr[i++] = 0;
	cpuvaddr[i++] = 0;

	/* Destination Address */
	cpuvaddr[i++] = lower_32_bits(rctx->outbuf.addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(rctx->outbuf.addr)) |
			SE_ADDR_HI_SZ(rctx->authsize);

	cpuvaddr[i++] = host1x_opcode_nonincr(se->hw->regs->op, 1);
	cpuvaddr[i++] = op;

	cpuvaddr[i++] = host1x_opcode_nonincr(host1x_uclass_incr_syncpt_r(), 1);
	cpuvaddr[i++] = host1x_uclass_incr_syncpt_cond_f(1) |
			host1x_uclass_incr_syncpt_indx_f(se->syncpt_id);

	dev_dbg(se->dev, "cfg %#x crypto cfg %#x\n", rctx->config, rctx->crypto_config);

	return i;
}

static int tegra_gcm_do_gmac(struct tegra_aead_ctx *ctx, struct tegra_aead_reqctx *rctx)
{
	struct tegra_se *se = ctx->se;
	u32 *cpuvaddr = se->cmdbuf->addr;
	unsigned int nents, size;

	nents = sg_nents(rctx->src_sg);
	scatterwalk_map_and_copy(rctx->inbuf.buf,
			rctx->src_sg, 0, rctx->assoclen, 0);

	rctx->config = tegra234_aes_cfg(SE_ALG_GMAC, rctx->encrypt);
	rctx->crypto_config = tegra234_aes_crypto_cfg(SE_ALG_GMAC, rctx->encrypt) |
			      SE_AES_KEY_INDEX(ctx->key_id);

	size = tegra_gmac_prep_cmd(se, cpuvaddr, rctx);

	return tegra_se_host1x_submit(se, size);
}

static int tegra_gcm_do_crypt(struct tegra_aead_ctx *ctx, struct tegra_aead_reqctx *rctx)
{
	struct tegra_se *se = ctx->se;
	u32 *cpuvaddr = se->cmdbuf->addr;
	int size, ret;

	scatterwalk_map_and_copy(rctx->inbuf.buf, rctx->src_sg,
				 rctx->assoclen, rctx->cryptlen, 0);

	rctx->config = tegra234_aes_cfg(SE_ALG_GCM, rctx->encrypt);
	rctx->crypto_config = tegra234_aes_crypto_cfg(SE_ALG_GCM, rctx->encrypt) |
			      SE_AES_KEY_INDEX(ctx->key_id);

	/* Prepare command and submit */
	size = tegra_gcm_crypt_prep_cmd(se, cpuvaddr, rctx);
	ret = tegra_se_host1x_submit(se, size);
	if (ret)
		return ret;

	/* Copy the result */
	scatterwalk_map_and_copy(rctx->outbuf.buf, rctx->dst_sg,
				 rctx->assoclen, rctx->cryptlen, 1);

	return 0;
}

static int tegra_gcm_do_final(struct tegra_aead_ctx *ctx, struct tegra_aead_reqctx *rctx)
{
	struct tegra_se *se = ctx->se;
	u32 *cpuvaddr = se->cmdbuf->addr;
	int size, ret, off;

	rctx->config = tegra234_aes_cfg(SE_ALG_GCM_FINAL, rctx->encrypt);
	rctx->crypto_config = tegra234_aes_crypto_cfg(SE_ALG_GCM_FINAL, rctx->encrypt) |
			      SE_AES_KEY_INDEX(ctx->key_id);

	/* Prepare command and submit */
	size = tegra_gcm_prep_final_cmd(se, cpuvaddr, rctx);
	ret = tegra_se_host1x_submit(se, size);
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

static int tegra_gcm_do_verify(struct tegra_se *se, struct tegra_aead_reqctx *rctx)
{
	unsigned int off;
	u8 mac[16];

	off = rctx->assoclen + rctx->cryptlen;
	scatterwalk_map_and_copy(mac, rctx->src_sg, off, rctx->authsize, 0);

	if (crypto_memneq(rctx->outbuf.buf, mac, rctx->authsize))
		return -EBADMSG;

	return 0;
}

static inline int tegra_ccm_check_iv(const u8 *iv)
{
	/* iv[0] gives value of q-1
	 * 2 <= q <= 8 as per NIST 800-38C notation
	 * 2 <= L <= 8, so 1 <= L' <= 7. as per rfc 3610 notation
	 */
	if (iv[0] < 1 || iv[0] > 7) {
		pr_err("ccm_check_iv failed %d\n", iv[0]);
		return -EINVAL;
	}

	return 0;
}

static int tegra_cbcmac_prep_cmd(struct tegra_se *se, u32 *cpuvaddr, struct tegra_aead_reqctx *rctx)
{
	unsigned int i = 0, data_count;

	data_count = (rctx->inbuf.size/AES_BLOCK_SIZE) - 1;

	cpuvaddr[i++] = host1x_opcode_nonincr(se->hw->regs->last_blk, 1);
	cpuvaddr[i++] = SE_LAST_BLOCK_VAL(data_count);

	cpuvaddr[i++] = host1x_opcode_incr(se->hw->regs->config, 6);
	cpuvaddr[i++] = rctx->config;
	cpuvaddr[i++] = rctx->crypto_config;

	cpuvaddr[i++] = lower_32_bits(rctx->inbuf.addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(rctx->inbuf.addr)) |
			SE_ADDR_HI_SZ(rctx->inbuf.size);

	cpuvaddr[i++] = lower_32_bits(rctx->outbuf.addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(rctx->outbuf.addr)) |
			SE_ADDR_HI_SZ(rctx->authsize);

	cpuvaddr[i++] = host1x_opcode_nonincr(se->hw->regs->op, 1);
	cpuvaddr[i++] = SE_AES_OP_WRSTALL |
			SE_AES_OP_LASTBUF | SE_AES_OP_START;

	cpuvaddr[i++] = host1x_opcode_nonincr(host1x_uclass_incr_syncpt_r(), 1);
	cpuvaddr[i++] = host1x_uclass_incr_syncpt_cond_f(1) |
			host1x_uclass_incr_syncpt_indx_f(se->syncpt_id);

	return i;
}

static int tegra_ctr_prep_cmd(struct tegra_se *se, u32 *cpuvaddr, struct tegra_aead_reqctx *rctx)
{
	int i = 0, j;

	cpuvaddr[i++] = host1x_opcode_setpayload(SE_CRYPTO_CTR_REG_COUNT);
	cpuvaddr[i++] = host1x_opcode_incr_w(se->hw->regs->linear_ctr);
	for (j = 0; j < SE_CRYPTO_CTR_REG_COUNT; j++)
		cpuvaddr[i++] = rctx->iv[j];

	cpuvaddr[i++] = host1x_opcode_nonincr(se->hw->regs->last_blk, 1);
	cpuvaddr[i++] = (rctx->inbuf.size / AES_BLOCK_SIZE) - 1;
	cpuvaddr[i++] = host1x_opcode_incr(se->hw->regs->config, 6);
	cpuvaddr[i++] = rctx->config;
	cpuvaddr[i++] = rctx->crypto_config;

	/* Source address setting */
	cpuvaddr[i++] = lower_32_bits(rctx->inbuf.addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(rctx->inbuf.addr)) |
			SE_ADDR_HI_SZ(rctx->inbuf.size);

	/* Destination address setting */
	cpuvaddr[i++] = lower_32_bits(rctx->outbuf.addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(rctx->outbuf.addr)) |
			SE_ADDR_HI_SZ(rctx->inbuf.size);

	cpuvaddr[i++] = host1x_opcode_nonincr(se->hw->regs->op, 1);
	cpuvaddr[i++] = SE_AES_OP_WRSTALL | SE_AES_OP_LASTBUF |
			SE_AES_OP_START;

	cpuvaddr[i++] = host1x_opcode_nonincr(host1x_uclass_incr_syncpt_r(), 1);
	cpuvaddr[i++] = host1x_uclass_incr_syncpt_cond_f(1) |
			host1x_uclass_incr_syncpt_indx_f(se->syncpt_id);

	dev_dbg(se->dev, "cfg %#x crypto cfg %#x\n",
			rctx->config, rctx->crypto_config);

	return i;
}

static int tegra_ccm_do_cbcmac(struct tegra_aead_ctx *ctx, struct tegra_aead_reqctx *rctx)
{
	struct tegra_se *se = ctx->se;
	u32 *cpuvaddr = se->cmdbuf->addr;
	int size;

	rctx->config = tegra234_aes_cfg(SE_ALG_CBC_MAC, rctx->encrypt);
	rctx->crypto_config = tegra234_aes_crypto_cfg(SE_ALG_CBC_MAC,
			      rctx->encrypt) | SE_AES_KEY_INDEX(ctx->key_id);

	/* Prepare command and submit */
	size = tegra_cbcmac_prep_cmd(se, cpuvaddr, rctx);

	return tegra_se_host1x_submit(se, size);
}

static int tegra_ccm_set_msg_len(u8 *block, unsigned int msglen, int csize)
{
	__be32 data;

	memset(block, 0, csize);
	block += csize;

	if (csize >= 4)
		csize = 4;
	else if (msglen > (1 << (8 * csize)))
		return -EOVERFLOW;

	data = cpu_to_be32(msglen);
	memcpy(block - csize, (u8 *)&data + 4 - csize, csize);

	return 0;
}

static int tegra_ccm_format_nonce(struct tegra_aead_reqctx *rctx, u8 *nonce)
{
	unsigned int q, t;
	u8 *q_ptr, *iv = (u8 *)rctx->iv;

	memcpy(nonce, rctx->iv, 16);

	/*** 1. Prepare Flags Octet ***/

	/* Encode t (mac length) */
	t = rctx->authsize;
	nonce[0] |= (((t - 2) / 2) << 3);

	/* Adata */
	if (rctx->assoclen)
		nonce[0] |= (1 << 6);

	/*** Encode Q - message length ***/
	q = iv[0] + 1;
	q_ptr = nonce + 16 - q;

	return tegra_ccm_set_msg_len(q_ptr, rctx->cryptlen, q);
}

static int tegra_ccm_format_adata(u8 *adata, unsigned int a)
{
	int len = 0;

	/* add control info for associated data
	 * RFC 3610 and NIST Special Publication 800-38C
	 */
	if (a < 65280) {
		*(__be16 *)adata = cpu_to_be16(a);
		len = 2;
	} else	{
		*(__be16 *)adata = cpu_to_be16(0xfffe);
		*(__be32 *)&adata[2] = cpu_to_be32(a);
		len = 6;
	}

	return len;
}

static int tegra_ccm_add_padding(u8 *buf, unsigned int len)
{
	unsigned int padlen = 16 - (len % 16);
	u8 padding[16] = {0};

	if (padlen == 16)
		return 0;

	memcpy(buf, padding, padlen);

	return padlen;
}

static int tegra_ccm_format_blocks(struct tegra_aead_reqctx *rctx)
{
	unsigned int alen = 0, off = 0;
	u8 nonce[16], adata[16];
	int ret;

	ret = tegra_ccm_format_nonce(rctx, nonce);
	if (ret)
		return ret;

	memcpy(rctx->inbuf.buf, nonce, 16);
	off = 16;

	if (rctx->assoclen) {
		alen = tegra_ccm_format_adata(adata, rctx->assoclen);
		memcpy(rctx->inbuf.buf + off, adata, alen);
		off += alen;

		scatterwalk_map_and_copy(rctx->inbuf.buf + off,
				rctx->src_sg, 0, rctx->assoclen, 0);

		off += rctx->assoclen;
		off += tegra_ccm_add_padding(rctx->inbuf.buf + off,
					 rctx->assoclen + alen);
	}

	return off;
}

static int tegra_ccm_mac_result(struct tegra_se *se, struct tegra_aead_reqctx *rctx)
{
	u32 result[16];
	int i, ret;

	/* Read and clear Result */
	for (i = 0; i < CMAC_RESULT_REG_COUNT; i++)
		result[i] = se_readl(se, se->hw->regs->result + (i * 4));

	for (i = 0; i < CMAC_RESULT_REG_COUNT; i++)
		se_writel(se, 0, se->hw->regs->result + (i * 4));

	if (rctx->encrypt) {
		memcpy(rctx->authdata, result, rctx->authsize);
	} else {
		ret = crypto_memneq(rctx->authdata, result, rctx->authsize);
		if (ret)
			return -EBADMSG;
	}

	return 0;
}

static int tegra_ccm_ctr_result(struct tegra_se *se, struct tegra_aead_reqctx *rctx)
{
	/* Copy result */
	scatterwalk_map_and_copy(rctx->outbuf.buf + 16, rctx->dst_sg,
				 rctx->assoclen, rctx->cryptlen, 1);

	if (rctx->encrypt)
		scatterwalk_map_and_copy(rctx->outbuf.buf, rctx->dst_sg,
					 rctx->assoclen + rctx->cryptlen,
					 rctx->authsize, 1);
	else
		memcpy(rctx->authdata, rctx->outbuf.buf, rctx->authsize);

	return 0;
}

static int tegra_ccm_compute_auth(struct tegra_aead_ctx *ctx, struct tegra_aead_reqctx *rctx)
{
	struct tegra_se *se = ctx->se;
	struct scatterlist *sg;
	int off, ret;

	off = tegra_ccm_format_blocks(rctx);
	if (off < 0)
		return -EINVAL;

	/* Copy plain text to the buffer */
	sg = rctx->encrypt ? rctx->src_sg : rctx->dst_sg;

	scatterwalk_map_and_copy(rctx->inbuf.buf + off,
				 sg, rctx->assoclen,
				 rctx->cryptlen, 0);
	off += rctx->cryptlen;
	off += tegra_ccm_add_padding(rctx->inbuf.buf + off, rctx->cryptlen);

	rctx->inbuf.size = off;

	ret = tegra_ccm_do_cbcmac(ctx, rctx);
	if (ret)
		return ret;

	return tegra_ccm_mac_result(se, rctx);
}

static int tegra_ccm_do_ctr(struct tegra_aead_ctx *ctx, struct tegra_aead_reqctx *rctx)
{
	struct tegra_se *se = ctx->se;
	unsigned int size, off = 0;
	struct scatterlist *sg = rctx->src_sg;
	int ret;

	rctx->config = tegra234_aes_cfg(SE_ALG_CTR, rctx->encrypt);
	rctx->crypto_config = tegra234_aes_crypto_cfg(SE_ALG_CTR, rctx->encrypt) |
			      SE_AES_KEY_INDEX(ctx->key_id);

	/* Copy authdata in the top of buffer for encryption/decryption */
	if (rctx->encrypt)
		memcpy(rctx->inbuf.buf, rctx->authdata, rctx->authsize);
	else
		scatterwalk_map_and_copy(rctx->inbuf.buf, sg,
					 rctx->assoclen + rctx->cryptlen,
					 rctx->authsize, 0);

	off += rctx->authsize;
	off += tegra_ccm_add_padding(rctx->inbuf.buf + off, rctx->authsize);

	/* If there is no cryptlen, proceed to submit the task */
	if (rctx->cryptlen) {
		scatterwalk_map_and_copy(rctx->inbuf.buf + off, sg,
					 rctx->assoclen, rctx->cryptlen, 0);
		off += rctx->cryptlen;
		off += tegra_ccm_add_padding(rctx->inbuf.buf + off, rctx->cryptlen);
	}

	rctx->inbuf.size = off;

	/* Prepare command and submit */
	size = tegra_ctr_prep_cmd(se, se->cmdbuf->addr, rctx);
	ret = tegra_se_host1x_submit(se, size);
	if (ret)
		return ret;

	return tegra_ccm_ctr_result(se, rctx);
}

static int tegra_ccm_crypt_init(struct aead_request *req, struct tegra_se *se,
				struct tegra_aead_reqctx *rctx)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	u8 *iv = (u8 *)rctx->iv;
	int ret, i;

	rctx->src_sg = req->src;
	rctx->dst_sg = req->dst;
	rctx->assoclen = req->assoclen;
	rctx->authsize = crypto_aead_authsize(tfm);

	memcpy(iv, req->iv, 16);

	ret = tegra_ccm_check_iv(iv);
	if (ret)
		return ret;

	/* Note: rfc 3610 and NIST 800-38C require counter (ctr_0) of
	 * zero to encrypt auth tag.
	 * req->iv has the formatted ctr_0 (i.e. Flags || N || 0).
	 */
	memset(iv + 15 - iv[0], 0, iv[0] + 1);

	/* Clear any previous result */
	for (i = 0; i < CMAC_RESULT_REG_COUNT; i++)
		se_writel(se, 0, se->hw->regs->result + (i * 4));

	return 0;
}

#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
static int tegra_aead_prep_req(struct crypto_engine *engine, void *areq);
static int tegra_aead_unprep_req(struct crypto_engine *engine, void *areq);
#endif

static int tegra_ccm_do_one_req(struct crypto_engine *engine, void *areq)
{
	struct aead_request *req = container_of(areq, struct aead_request, base);
	struct tegra_aead_reqctx *rctx = aead_request_ctx(req);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct tegra_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct tegra_se *se = ctx->se;
	int ret;

#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
	ret = tegra_aead_prep_req(engine, areq);
	if (ret != 0)
		return ret;
#endif

	ret = tegra_ccm_crypt_init(req, se, rctx);
	if (ret)
		goto out;

	if (rctx->encrypt) {
		rctx->cryptlen = req->cryptlen;

		/* CBC MAC Operation */
		ret = tegra_ccm_compute_auth(ctx, rctx);
		if (ret)
			goto out;

		/* CTR operation */
		ret = tegra_ccm_do_ctr(ctx, rctx);
		if (ret)
			goto out;
	} else {
		rctx->cryptlen = req->cryptlen - ctx->authsize;

		/* CTR operation */
		ret = tegra_ccm_do_ctr(ctx, rctx);
		if (ret)
			goto out;

		/* CBC MAC Operation */
		ret = tegra_ccm_compute_auth(ctx, rctx);
		if (ret)
			goto out;
	}

out:
	crypto_finalize_aead_request(se->engine, req, ret);

#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
	ret = tegra_aead_unprep_req(engine, areq);
	if (ret != 0)
		return ret;
#endif
	return 0;
}

static int tegra_gcm_do_one_req(struct crypto_engine *engine, void *areq)
{
	struct aead_request *req = container_of(areq, struct aead_request, base);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct tegra_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct tegra_aead_reqctx *rctx = aead_request_ctx(req);
	int ret;

#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
	ret = tegra_aead_prep_req(engine, areq);
	if (ret != 0)
		return ret;
#endif

	req = container_of(areq, struct aead_request, base);

	rctx->src_sg = req->src;
	rctx->dst_sg = req->dst;
	rctx->assoclen = req->assoclen;
	rctx->authsize = crypto_aead_authsize(tfm);

	if (rctx->encrypt)
		rctx->cryptlen = req->cryptlen;
	else
		rctx->cryptlen = req->cryptlen - ctx->authsize;

	memcpy(rctx->iv, req->iv, GCM_AES_IV_SIZE);
	rctx->iv[3] = (1 << 24);

	/* If there is associated data perform GMAC operation */
	if (rctx->assoclen) {
		ret = tegra_gcm_do_gmac(ctx, rctx);
		if (ret)
			goto out;
	}

	/* GCM Encryption/Decryption operation */
	if (rctx->cryptlen) {
		ret = tegra_gcm_do_crypt(ctx, rctx);
		if (ret)
			goto out;
	}

	/* GCM_FINAL operation */
	ret = tegra_gcm_do_final(ctx, rctx);
	if (ret)
		goto out;

	if (!rctx->encrypt)
		ret = tegra_gcm_do_verify(ctx->se, rctx);

out:
	crypto_finalize_aead_request(ctx->se->engine, req, ret);
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
	ret = tegra_aead_unprep_req(engine, areq);
	if (ret != 0)
		return ret;
#endif
	return 0;
}

static int tegra_aead_prep_req(struct crypto_engine *engine, void *areq)
{
	struct aead_request *req = container_of(areq, struct aead_request, base);
	struct tegra_aead_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct tegra_aead_reqctx *rctx = aead_request_ctx(req);

	rctx->inbuf.buf = dma_alloc_coherent(ctx->se->dev, SE_AES_BUFLEN,
					     &rctx->inbuf.addr, GFP_KERNEL);
	if (!rctx->inbuf.buf)
		goto inbuf_err;

	rctx->inbuf.size = SE_AES_BUFLEN;

	rctx->outbuf.buf = dma_alloc_coherent(ctx->se->dev, SE_AES_BUFLEN,
					     &rctx->outbuf.addr, GFP_KERNEL);
	if (!rctx->outbuf.buf)
		goto outbuf_err;

	rctx->outbuf.size = SE_AES_BUFLEN;

	return 0;

outbuf_err:
	dma_free_coherent(ctx->se->dev, SE_AES_BUFLEN,
			  rctx->inbuf.buf, rctx->inbuf.addr);
inbuf_err:
	return -ENOMEM;

}

static int tegra_aead_unprep_req(struct crypto_engine *engine, void *areq)
{
	struct aead_request *req = container_of(areq, struct aead_request, base);
	struct tegra_aead_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct tegra_aead_reqctx *rctx = aead_request_ctx(req);

	dma_free_coherent(ctx->se->dev, SE_AES_BUFLEN,
			  rctx->outbuf.buf, rctx->outbuf.addr);

	dma_free_coherent(ctx->se->dev, SE_AES_BUFLEN,
			  rctx->inbuf.buf, rctx->inbuf.addr);

	return 0;
}

static int tegra_ccm_cra_init(struct crypto_aead *tfm)
{
	struct tegra_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct aead_alg *alg = crypto_aead_alg(tfm);
	struct tegra_se_alg *se_alg;
	const char *algname;

	algname = crypto_tfm_alg_name(&tfm->base);

#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
	se_alg = container_of(alg, struct tegra_se_alg, alg.aead.base);
#else
	se_alg = container_of(alg, struct tegra_se_alg, alg.aead);
#endif

	crypto_aead_set_reqsize(tfm, sizeof(struct tegra_aead_reqctx));

	ctx->se = se_alg->se_dev;
	ctx->key_id = 0;
	ctx->alg = se_algname_to_algid(algname);

#ifndef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
	ctx->enginectx.op.prepare_request = tegra_aead_prep_req;
	ctx->enginectx.op.do_one_request = tegra_ccm_do_one_req;
	ctx->enginectx.op.unprepare_request = tegra_aead_unprep_req;
#endif

	return 0;
}

static int tegra_ccm_setauthsize(struct crypto_aead *tfm,  unsigned int authsize)
{
	struct tegra_aead_ctx *ctx = crypto_aead_ctx(tfm);

	switch (authsize) {
	case 4:
	case 6:
	case 8:
	case 10:
	case 12:
	case 14:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	ctx->authsize = authsize;

	return 0;
}

static int tegra_gcm_cra_init(struct crypto_aead *tfm)
{
	struct tegra_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct aead_alg *alg = crypto_aead_alg(tfm);
	struct tegra_se_alg *se_alg;
	const char *algname;

	algname = crypto_tfm_alg_name(&tfm->base);
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
	se_alg = container_of(alg, struct tegra_se_alg, alg.aead.base);
#else
	se_alg = container_of(alg, struct tegra_se_alg, alg.aead);
#endif

	crypto_aead_set_reqsize(tfm, sizeof(struct tegra_aead_reqctx));

	ctx->se = se_alg->se_dev;
	ctx->key_id = 0;
	ctx->alg = se_algname_to_algid(algname);

#ifndef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
	ctx->enginectx.op.prepare_request = tegra_aead_prep_req;
	ctx->enginectx.op.do_one_request = tegra_gcm_do_one_req;
	ctx->enginectx.op.unprepare_request = tegra_aead_unprep_req;
#endif

	return 0;
}

static int tegra_gcm_setauthsize(struct crypto_aead *tfm,  unsigned int authsize)
{
	struct tegra_aead_ctx *ctx = crypto_aead_ctx(tfm);
	int ret;

	ret = crypto_gcm_check_authsize(authsize);
	if (ret)
		return ret;

	ctx->authsize = authsize;

	return 0;
}

static void tegra_aead_cra_exit(struct crypto_aead *tfm)
{
	struct tegra_aead_ctx *ctx = crypto_tfm_ctx(&tfm->base);

	if (ctx->key_id)
		tegra_key_invalidate(ctx->se, ctx->key_id);
}

static int tegra_aead_crypt(struct aead_request *req, bool encrypt)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct tegra_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct tegra_aead_reqctx *rctx = aead_request_ctx(req);

	rctx->encrypt = encrypt;

	return crypto_transfer_aead_request_to_engine(ctx->se->engine, req);
}

static int tegra_aead_encrypt(struct aead_request *req)
{
	return tegra_aead_crypt(req, true);
}

static int tegra_aead_decrypt(struct aead_request *req)
{
	return tegra_aead_crypt(req, false);
}

static int tegra_aead_setkey(struct crypto_aead *tfm,
			       const u8 *key, u32 keylen)
{
	struct tegra_aead_ctx *ctx = crypto_aead_ctx(tfm);

	if (aes_check_keylen(keylen)) {
		dev_err(ctx->se->dev, "key length validation failed\n");
		return -EINVAL;
	}

	return tegra_key_submit(ctx->se, key, keylen, ctx->alg, &ctx->key_id);
}

static int tegra_cmac_prep_cmd(struct tegra_se *se, u32 *cpuvaddr, struct tegra_cmac_reqctx *rctx)
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
		rctx->task &= ~SHA_FIRST;

		cpuvaddr[i++] = host1x_opcode_setpayload(SE_CRYPTO_CTR_REG_COUNT);
		cpuvaddr[i++] = host1x_opcode_incr_w(se->hw->regs->linear_ctr);
		/* Load 0 IV */
		for (j = 0; j < SE_CRYPTO_CTR_REG_COUNT; j++)
			cpuvaddr[i++] = 0;
	}

	cpuvaddr[i++] = host1x_opcode_nonincr(se->hw->regs->last_blk, 1);
	cpuvaddr[i++] = SE_LAST_BLOCK_VAL(data_count) |
			SE_LAST_BLOCK_RES_BITS(res_bits);

	cpuvaddr[i++] = host1x_opcode_incr(se->hw->regs->config, 6);
	cpuvaddr[i++] = rctx->config;
	cpuvaddr[i++] = rctx->crypto_config;

	/* Source Address */
	cpuvaddr[i++] = lower_32_bits(rctx->datbuf.addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(rctx->datbuf.addr)) |
			SE_ADDR_HI_SZ(rctx->datbuf.size);
	cpuvaddr[i++] = 0;
	cpuvaddr[i++] = SE_ADDR_HI_SZ(AES_BLOCK_SIZE);

	cpuvaddr[i++] = host1x_opcode_nonincr(se->hw->regs->op, 1);
	cpuvaddr[i++] = op;

	cpuvaddr[i++] = host1x_opcode_nonincr(host1x_uclass_incr_syncpt_r(), 1);
	cpuvaddr[i++] = host1x_uclass_incr_syncpt_cond_f(1) |
			host1x_uclass_incr_syncpt_indx_f(se->syncpt_id);

	return i;
}

static int tegra_cmac_do_update(struct ahash_request *req)
{
	struct tegra_cmac_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_se *se = ctx->se;
	unsigned int nblks, nresidue, size;

	if (!req->nbytes)
		return 0;

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
	rctx->config = tegra234_aes_cfg(SE_ALG_CMAC, 0);
	rctx->crypto_config = SE_AES_KEY_INDEX(ctx->key_id);

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

	/* Copy the previous residue first */
	if (rctx->residue.size)
		memcpy(rctx->datbuf.buf, rctx->residue.buf, rctx->residue.size);

	scatterwalk_map_and_copy(rctx->datbuf.buf + rctx->residue.size,
			rctx->src_sg, 0, req->nbytes - nresidue, 0);

	scatterwalk_map_and_copy(rctx->residue.buf, rctx->src_sg,
			req->nbytes - nresidue, nresidue, 0);

	/* Update residue value with the residue after current block */
	rctx->residue.size = nresidue;

	size = tegra_cmac_prep_cmd(se, se->cmdbuf->addr, rctx);

	return tegra_se_host1x_submit(se, size);
}

static int tegra_cmac_do_final(struct ahash_request *req)
{
	struct tegra_cmac_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_se *se = ctx->se;
	u32 *result = (u32 *)req->result;
	int ret = 0, i, size;

	if (!req->nbytes && !rctx->total_len && ctx->fallback_tfm) {
		return crypto_shash_tfm_digest(ctx->fallback_tfm,
					rctx->datbuf.buf, 0, req->result);
	}

	memcpy(rctx->datbuf.buf, rctx->residue.buf, rctx->residue.size);
	rctx->datbuf.size = rctx->residue.size;
	rctx->total_len += rctx->residue.size;
	rctx->config = tegra234_aes_cfg(SE_ALG_CMAC, 0);

	/* Prepare command and submit */
	size = tegra_cmac_prep_cmd(se, se->cmdbuf->addr, rctx);
	ret = tegra_se_host1x_submit(se, size);
	if (ret)
		goto out;

	/* Read and clear Result register */
	for (i = 0; i < CMAC_RESULT_REG_COUNT; i++)
		result[i] = se_readl(ctx->se, se->hw->regs->result + (i * 4));

	for (i = 0; i < CMAC_RESULT_REG_COUNT; i++)
		se_writel(ctx->se, 0, se->hw->regs->result + (i * 4));

out:
	dma_free_coherent(se->dev, SE_SHA_BUFLEN,
			rctx->datbuf.buf, rctx->datbuf.addr);
	dma_free_coherent(se->dev, crypto_ahash_blocksize(tfm) * 2,
			rctx->residue.buf, rctx->residue.addr);
	return ret;
}

static int tegra_cmac_do_one_req(struct crypto_engine *engine, void *areq)
{
	struct ahash_request *req = ahash_request_cast(areq);
	struct tegra_cmac_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_se *se = ctx->se;
	int ret = -EINVAL;

	if (rctx->task & SHA_UPDATE) {
		ret = tegra_cmac_do_update(req);
		rctx->task &= ~SHA_UPDATE;
	}

	if (rctx->task & SHA_FINAL) {
		ret = tegra_cmac_do_final(req);
		rctx->task &= ~SHA_FINAL;
	}

	crypto_finalize_hash_request(se->engine, req, ret);

	return ret;
}

static int tegra_cmac_cra_init(struct crypto_tfm *tfm)
{
	struct tegra_cmac_ctx *ctx = crypto_tfm_ctx(tfm);
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
				 sizeof(struct tegra_cmac_reqctx));

	ctx->se = se_alg->se_dev;
	ctx->key_id = 0;
	ctx->alg = se_algname_to_algid(algname);
#ifndef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
	ctx->enginectx.op.prepare_request = NULL;
	ctx->enginectx.op.do_one_request = tegra_cmac_do_one_req;
	ctx->enginectx.op.unprepare_request = NULL;
#endif

	ctx->fallback_tfm = crypto_alloc_shash(algname, 0,
				    CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(ctx->fallback_tfm)) {
		dev_warn(ctx->se->dev, "failed to allocate fallback for CMAC\n");
		ctx->fallback_tfm = NULL;
	}

	return 0;
}

static void tegra_cmac_cra_exit(struct crypto_tfm *tfm)
{
	struct tegra_cmac_ctx *ctx = crypto_tfm_ctx(tfm);

	tegra_key_invalidate(ctx->se, ctx->key_id);

	if (ctx->fallback_tfm)
		crypto_free_shash(ctx->fallback_tfm);
}

static int tegra_cmac_init(struct ahash_request *req)
{
	struct tegra_cmac_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_se *se = ctx->se;
	int i;

	rctx->total_len = 0;
	rctx->datbuf.size = 0;
	rctx->residue.size = 0;
	rctx->task = SHA_FIRST;
	rctx->blk_size = crypto_ahash_blocksize(tfm);

	rctx->residue.buf = dma_alloc_coherent(se->dev, rctx->blk_size * 2,
					&rctx->residue.addr, GFP_KERNEL);
	if (!rctx->residue.buf)
		goto resbuf_fail;

	rctx->residue.size = 0;

	rctx->datbuf.buf = dma_alloc_coherent(se->dev, SE_SHA_BUFLEN,
					&rctx->datbuf.addr, GFP_KERNEL);
	if (!rctx->datbuf.buf)
		goto datbuf_fail;

	rctx->datbuf.size = 0;

	/* Clear any previous result */
	for (i = 0; i < CMAC_RESULT_REG_COUNT; i++)
		se_writel(se, 0, se->hw->regs->result + (i * 4));

	return 0;

datbuf_fail:
	dma_free_coherent(se->dev, rctx->blk_size, rctx->residue.buf,
				rctx->residue.addr);
resbuf_fail:
	return -ENOMEM;
}

static int tegra_cmac_setkey(struct crypto_ahash *tfm, const u8 *key,
			     unsigned int keylen)
{
	struct tegra_cmac_ctx *ctx = crypto_ahash_ctx(tfm);

	if (aes_check_keylen(keylen)) {
		dev_err(ctx->se->dev, "key length validation failed\n");
		return -EINVAL;
	}

	if (ctx->fallback_tfm)
		crypto_shash_setkey(ctx->fallback_tfm, key, keylen);

	return tegra_key_submit(ctx->se, key, keylen, ctx->alg, &ctx->key_id);
}

static int tegra_cmac_update(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_cmac_reqctx *rctx = ahash_request_ctx(req);

	rctx->task |= SHA_UPDATE;

	return crypto_transfer_hash_request_to_engine(ctx->se->engine, req);
}

static int tegra_cmac_final(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_cmac_reqctx *rctx = ahash_request_ctx(req);

	rctx->task |= SHA_FINAL;

	return crypto_transfer_hash_request_to_engine(ctx->se->engine, req);
}

static int tegra_cmac_finup(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_cmac_reqctx *rctx = ahash_request_ctx(req);

	rctx->task |= SHA_UPDATE | SHA_FINAL;

	return crypto_transfer_hash_request_to_engine(ctx->se->engine, req);
}

static int tegra_cmac_digest(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_cmac_reqctx *rctx = ahash_request_ctx(req);

	tegra_cmac_init(req);
	rctx->task |= SHA_UPDATE | SHA_FINAL;

	return crypto_transfer_hash_request_to_engine(ctx->se->engine, req);
}

static int tegra_cmac_export(struct ahash_request *req, void *out)
{
	struct tegra_cmac_reqctx *rctx = ahash_request_ctx(req);

	memcpy(out, rctx, sizeof(*rctx));

	return 0;
}

static int tegra_cmac_import(struct ahash_request *req, const void *in)
{
	struct tegra_cmac_reqctx *rctx = ahash_request_ctx(req);

	memcpy(rctx, in, sizeof(*rctx));

	return 0;
}

static struct tegra_se_alg tegra_aead_algs[] = {
	{
		.alg.aead = {
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			.base = {
#endif
			.init			= tegra_gcm_cra_init,
			.exit			= tegra_aead_cra_exit,
			.setkey			= tegra_aead_setkey,
			.setauthsize		= tegra_gcm_setauthsize,
			.encrypt		= tegra_aead_encrypt,
			.decrypt		= tegra_aead_decrypt,
			.maxauthsize		= AES_BLOCK_SIZE,
			.ivsize			= GCM_AES_IV_SIZE,
			.base = {
				.cra_name	   = "gcm(aes)",
				.cra_driver_name   = "gcm-aes-tegra",
				.cra_priority	   = 500,
				.cra_blocksize	   = AES_BLOCK_SIZE,
				.cra_ctxsize	   = sizeof(struct tegra_aead_ctx),
				.cra_alignmask	   = 0,
				.cra_module	   = THIS_MODULE,
			},
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			},
			.op.do_one_request = tegra_gcm_do_one_req,
#endif
		}
	}, {
		.alg.aead = {
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			.base = {
#endif
			.init			= tegra_ccm_cra_init,
			.exit			= tegra_aead_cra_exit,
			.setkey			= tegra_aead_setkey,
			.setauthsize		= tegra_ccm_setauthsize,
			.encrypt		= tegra_aead_encrypt,
			.decrypt		= tegra_aead_decrypt,
			.maxauthsize		= AES_BLOCK_SIZE,
			.ivsize			= AES_BLOCK_SIZE,
			.chunksize		= AES_BLOCK_SIZE,
			.base = {
				.cra_name	   = "ccm(aes)",
				.cra_driver_name   = "ccm-aes-tegra",
				.cra_priority	   = 500,
				.cra_blocksize	   = AES_BLOCK_SIZE,
				.cra_ctxsize	   = sizeof(struct tegra_aead_ctx),
				.cra_alignmask	   = 0,
				.cra_module	   = THIS_MODULE,
			},
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			},
			.op.do_one_request = tegra_ccm_do_one_req,
#endif
		}
	}
};

static struct tegra_se_alg tegra_cmac_algs[] = {
	{
		.alg.ahash = {
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			.base = {
#endif
			.init = tegra_cmac_init,
			.setkey	= tegra_cmac_setkey,
			.update = tegra_cmac_update,
			.final = tegra_cmac_final,
			.finup = tegra_cmac_finup,
			.digest = tegra_cmac_digest,
			.export = tegra_cmac_export,
			.import = tegra_cmac_import,
			.halg.digestsize = AES_BLOCK_SIZE,
			.halg.statesize = sizeof(struct tegra_cmac_reqctx),

			.halg.base = {
				.cra_name = "cmac(aes)",
				.cra_driver_name = "tegra-se-cmac",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize = AES_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct tegra_cmac_ctx),
				.cra_alignmask = 0,
				.cra_module = THIS_MODULE,
				.cra_init = tegra_cmac_cra_init,
				.cra_exit = tegra_cmac_cra_exit,
			},
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			},
			.op.do_one_request = tegra_cmac_do_one_req,
#endif
		}
	}
};

int tegra_init_aes(struct tegra_se *se)
{
	int i, ret;

	se->manifest = tegra_aes_kac_manifest;

	for (i = 0; i < ARRAY_SIZE(tegra_aes_algs); i++) {
		tegra_aes_algs[i].se_dev = se;
		ret = CRYPTO_REGISTER(skcipher, &tegra_aes_algs[i].alg.skcipher);
		if (ret) {
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			dev_err(se->dev, "failed to register %s\n",
				tegra_aes_algs[i].alg.skcipher.base.base.cra_name);
#else
			dev_err(se->dev, "failed to register %s\n",
				tegra_aes_algs[i].alg.skcipher.base.cra_name);
#endif
			goto err_aes;
		}
	}

	for (i = 0; i < ARRAY_SIZE(tegra_aead_algs); i++) {
		tegra_aead_algs[i].se_dev = se;
		ret = CRYPTO_REGISTER(aead, &tegra_aead_algs[i].alg.aead);
		if (ret) {
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			dev_err(se->dev, "failed to register %s\n",
				tegra_aead_algs[i].alg.aead.base.base.cra_name);
#else
			dev_err(se->dev, "failed to register %s\n",
				tegra_aead_algs[i].alg.aead.base.cra_name);
#endif
			goto err_aead;
		}
	}

	for (i = 0; i < ARRAY_SIZE(tegra_cmac_algs); i++) {
		tegra_cmac_algs[i].se_dev = se;
		ret = CRYPTO_REGISTER(ahash, &tegra_cmac_algs[i].alg.ahash);
		if (ret) {
#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
			dev_err(se->dev, "failed to register %s\n",
				tegra_cmac_algs[i].alg.ahash.base.halg.base.cra_name);
#else
			dev_err(se->dev, "failed to register %s\n",
				tegra_cmac_algs[i].alg.ahash.halg.base.cra_name);
#endif
			goto err_cmac;
		}
	}

	dev_info(se->dev, "registered AES algorithms\n");

	return 0;

err_cmac:
	for (--i; i >= 0; i--)
		CRYPTO_UNREGISTER(ahash, &tegra_cmac_algs[i].alg.ahash);

	i = ARRAY_SIZE(tegra_aead_algs);
err_aead:
	for (--i; i >= 0; i--)
		CRYPTO_UNREGISTER(aead, &tegra_aead_algs[i].alg.aead);

	i = ARRAY_SIZE(tegra_aes_algs);
err_aes:
	for (--i; i >= 0; i--)
		CRYPTO_UNREGISTER(skcipher, &tegra_aes_algs[i].alg.skcipher);

	return ret;
}

void tegra_deinit_aes(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tegra_aes_algs); i++)
		CRYPTO_UNREGISTER(skcipher, &tegra_aes_algs[i].alg.skcipher);

	for (i = 0; i < ARRAY_SIZE(tegra_aead_algs); i++)
		CRYPTO_UNREGISTER(aead, &tegra_aead_algs[i].alg.aead);

	for (i = 0; i < ARRAY_SIZE(tegra_cmac_algs); i++)
		CRYPTO_UNREGISTER(ahash, &tegra_cmac_algs[i].alg.ahash);

}
