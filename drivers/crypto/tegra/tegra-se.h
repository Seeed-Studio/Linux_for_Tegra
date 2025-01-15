/* SPDX-License-Identifier: GPL-2.0-only
 * SPDX-FileCopyrightText: Copyright (c) 2023-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Header file for NVIDIA Security Engine driver.
 */

#ifndef _TEGRA_SE_H
#define _TEGRA_SE_H

#include <nvidia/conftest.h>

#include <linux/bitfield.h>
#include <linux/iommu.h>
#include <linux/host1x-next.h>
#include <crypto/aead.h>
#include <crypto/engine.h>
#include <crypto/hash.h>
#include <crypto/sha1.h>
#include <crypto/sha3.h>
#include <crypto/skcipher.h>

#define SE_OWNERSHIP					0x14
#define SE_OWNERSHIP_UID(x)				FIELD_GET(GENMASK(7, 0), x)
#define TEGRA_GPSE_ID					3

#define SE_STREAM_ID					0x90

#define SE_SHA_CFG					0x4004
#define SE_SHA_IN_ADDR					0x400c
#define SE_SHA_KEY_ADDR					0x4094
#define SE_SHA_KEY_DATA					0x4098
#define SE_SHA_KEYMANIFEST				0x409c
#define SE_SHA_KAC2_KEYMANIFEST				0x4178
#define SE_SHA_CRYPTO_CFG				0x40a4
#define SE_SHA_KEY_DST					0x40a8
#define SE_SHA_SRC_KSLT					0x4180
#define SE_SHA_TGT_KSLT					0x4184
#define SE_SHA_MSG_LENGTH				0x401c
#define SE_SHA_OPERATION				0x407c
#define SE_SHA_HASH_RESULT				0x40b0

#define SE_SHA_ENC_MODE(x)				FIELD_PREP(GENMASK(31, 24), x)
#define SE_SHA_ENC_MODE_SHA1				SE_SHA_ENC_MODE(0)
#define SE_SHA_ENC_MODE_SHA224				SE_SHA_ENC_MODE(4)
#define SE_SHA_ENC_MODE_SHA256				SE_SHA_ENC_MODE(5)
#define SE_SHA_ENC_MODE_SHA384				SE_SHA_ENC_MODE(6)
#define SE_SHA_ENC_MODE_SHA512				SE_SHA_ENC_MODE(7)
#define SE_SHA_ENC_MODE_SHA_CTX_INTEGRITY		SE_SHA_ENC_MODE(8)
#define SE_SHA_ENC_MODE_SHA3_224			SE_SHA_ENC_MODE(9)
#define SE_SHA_ENC_MODE_SHA3_256			SE_SHA_ENC_MODE(10)
#define SE_SHA_ENC_MODE_SHA3_384			SE_SHA_ENC_MODE(11)
#define SE_SHA_ENC_MODE_SHA3_512			SE_SHA_ENC_MODE(12)
#define SE_SHA_ENC_MODE_SHAKE128			SE_SHA_ENC_MODE(13)
#define SE_SHA_ENC_MODE_SHAKE256			SE_SHA_ENC_MODE(14)
#define SE_SHA_ENC_MODE_HMAC_SHA256_1KEY		SE_SHA_ENC_MODE(0)
#define SE_SHA_ENC_MODE_HMAC_SHA256_2KEY		SE_SHA_ENC_MODE(1)
#define SE_SHA_ENC_MODE_SM3_256				SE_SHA_ENC_MODE(0)

#define SE_SHA_CFG_ENC_ALG(x)				FIELD_PREP(GENMASK(15, 12), x)
#define SE_SHA_ENC_ALG_NOP				SE_SHA_CFG_ENC_ALG(0)
#define SE_SHA_ENC_ALG_SHA_ENC				SE_SHA_CFG_ENC_ALG(1)
#define SE_SHA_ENC_ALG_RNG				SE_SHA_CFG_ENC_ALG(2)
#define SE_SHA_ENC_ALG_SHA				SE_SHA_CFG_ENC_ALG(3)
#define SE_SHA_ENC_ALG_SM3				SE_SHA_CFG_ENC_ALG(4)
#define SE_SHA_ENC_ALG_HMAC				SE_SHA_CFG_ENC_ALG(7)
#define SE_SHA_ENC_ALG_KDF				SE_SHA_CFG_ENC_ALG(8)
#define SE_SHA_ENC_ALG_KEY_INVLD			SE_SHA_CFG_ENC_ALG(10)
#define SE_SHA_ENC_ALG_KEY_INQUIRE			SE_SHA_CFG_ENC_ALG(12)
#define SE_SHA_ENC_ALG_INS				SE_SHA_CFG_ENC_ALG(13)

#define SE_SHA_OP_LASTBUF				FIELD_PREP(BIT(16), 1)
#define SE_SHA_OP_WRSTALL				FIELD_PREP(BIT(15), 1)

#define SE_SHA_OP_OP(x)					FIELD_PREP(GENMASK(2, 0), x)
#define SE_SHA_OP_START					SE_SHA_OP_OP(1)
#define SE_SHA_OP_RESTART_OUT				SE_SHA_OP_OP(2)
#define SE_SHA_OP_RESTART_IN				SE_SHA_OP_OP(4)
#define SE_SHA_OP_RESTART_INOUT				SE_SHA_OP_OP(5)
#define SE_SHA_OP_DUMMY					SE_SHA_OP_OP(6)

#define SE_SHA_CFG_DEC_ALG(x)				FIELD_PREP(GENMASK(11, 8), x)
#define SE_SHA_DEC_ALG_NOP				SE_SHA_CFG_DEC_ALG(0)
#define SE_SHA_DEC_ALG_AES_DEC				SE_SHA_CFG_DEC_ALG(1)
#define SE_SHA_DEC_ALG_HMAC				SE_SHA_CFG_DEC_ALG(7)
#define SE_SHA_DEC_ALG_HMAC_VERIFY			SE_SHA_CFG_DEC_ALG(9)

#define SE_SHA_CFG_DST(x)				FIELD_PREP(GENMASK(4, 2), x)
#define SE_SHA_DST_MEMORY				SE_SHA_CFG_DST(0)
#define SE_SHA_DST_HASH_REG				SE_SHA_CFG_DST(1)
#define SE_SHA_DST_KEYTABLE				SE_SHA_CFG_DST(2)
#define SE_SHA_DST_SRK					SE_SHA_CFG_DST(3)

#define SE_SHA_TASK_HASH_INIT				BIT(0)

/* AES Configuration */
#define SE_AES0_CFG					0x1004
#define SE_AES0_CRYPTO_CONFIG				0x1008
#define SE_AES0_KEY_DST					0x1030
#define SE_AES0_OPERATION				0x1038
#define SE_AES0_LINEAR_CTR				0x101c
#define SE_AES0_LAST_BLOCK				0x102c
#define SE_AES0_KEY_ADDR				0x10bc
#define SE_AES0_KEY_DATA				0x10c0
#define SE_AES0_CMAC_RESULT				0x10c4
#define SE_AES0_SRC_KSLT				0x1100
#define SE_AES0_TGT_KSLT				0x1104
#define SE_AES0_KAC2_KEYMANIFEST			0x1108
#define SE_AES0_KEYMANIFEST				0x1114
#define SE_AES0_AAD_LEN					0x112c
#define SE_AES0_CRYPTO_MSG_LEN				0x1134

#define SE_AES1_CFG					0x2004
#define SE_AES1_CRYPTO_CONFIG				0x2008
#define SE_AES1_KEY_DST					0x2030
#define SE_AES1_OPERATION				0x2038
#define SE_AES1_LINEAR_CTR				0x201c
#define SE_AES1_LAST_BLOCK				0x202c
#define SE_AES1_KEY_ADDR				0x20bc
#define SE_AES1_KEY_DATA				0x20c0
#define SE_AES1_CMAC_RESULT				0x20c4
#define SE_AES1_SRC_KSLT				0x2100
#define SE_AES1_TGT_KSLT				0x2104
#define SE_AES1_KAC2_KEYMANIFEST			0x2108
#define SE_AES1_KEYMANIFEST				0x2114
#define SE_AES1_AAD_LEN					0x212c
#define SE_AES1_CRYPTO_MSG_LEN				0x2134

#define SE_AES_CFG_ENC_MODE(x)				FIELD_PREP(GENMASK(31, 24), x)
#define SE_AES_ENC_MODE_ECB				SE_AES_CFG_ENC_MODE(0)
#define SE_AES_ENC_MODE_CBC				SE_AES_CFG_ENC_MODE(1)
#define SE_AES_ENC_MODE_OFB				SE_AES_CFG_ENC_MODE(2)
#define SE_AES_ENC_MODE_GMAC				SE_AES_CFG_ENC_MODE(3)
#define SE_AES_ENC_MODE_GCM				SE_AES_CFG_ENC_MODE(4)
#define SE_AES_ENC_MODE_GCM_FINAL			SE_AES_CFG_ENC_MODE(5)
#define SE_AES_ENC_MODE_KW				SE_AES_CFG_ENC_MODE(6)
#define SE_AES_ENC_MODE_CMAC				SE_AES_CFG_ENC_MODE(7)
#define SE_AES_ENC_MODE_CTR				SE_AES_CFG_ENC_MODE(10)
#define SE_AES_ENC_MODE_XTS				SE_AES_CFG_ENC_MODE(11)
#define SE_AES_ENC_MODE_CBC_MAC				SE_AES_CFG_ENC_MODE(12)
#define SE_AES_ENC_MODE_AESKW_CRYPT			SE_AES_CFG_ENC_MODE(13)
#define SE_AES_ENC_MODE_CTR_DRBG_INSTANTIATE_DF		SE_AES_CFG_ENC_MODE(14)
#define SE_AES_ENC_MODE_CTR_DRBG_GENKEY			SE_AES_CFG_ENC_MODE(15)
#define SE_AES_ENC_MODE_CTR_DRBG_GENRND			SE_AES_CFG_ENC_MODE(16)
#define SE_AES_ENC_MODE_KDF_CMAC_AES			SE_AES_CFG_ENC_MODE(20)
#define SE_AES_ENC_MODE_GCM2				SE_AES_CFG_ENC_MODE(21)

#define SE_AES_CFG_DEC_MODE(x)				FIELD_PREP(GENMASK(23, 16), x)
#define SE_AES_DEC_MODE_ECB				SE_AES_CFG_DEC_MODE(0)
#define SE_AES_DEC_MODE_CBC				SE_AES_CFG_DEC_MODE(1)
#define SE_AES_DEC_MODE_OFB				SE_AES_CFG_DEC_MODE(2)
#define SE_AES_DEC_MODE_GMAC				SE_AES_CFG_DEC_MODE(3)
#define SE_AES_DEC_MODE_GCM				SE_AES_CFG_DEC_MODE(4)
#define SE_AES_DEC_MODE_GCM_FINAL			SE_AES_CFG_DEC_MODE(5)
#define SE_AES_DEC_MODE_KW				SE_AES_CFG_DEC_MODE(6)
#define SE_AES_DEC_MODE_CMAC				SE_AES_CFG_DEC_MODE(7)
#define SE_AES_DEC_MODE_CMAC_VERIFY			SE_AES_CFG_DEC_MODE(8)
#define SE_AES_DEC_MODE_GCM_VERIFY			SE_AES_CFG_DEC_MODE(9)
#define SE_AES_DEC_MODE_CTR				SE_AES_CFG_DEC_MODE(10)
#define SE_AES_DEC_MODE_XTS				SE_AES_CFG_DEC_MODE(11)
#define SE_AES_DEC_MODE_CBC_MAC				SE_AES_CFG_DEC_MODE(12)
#define SE_AES_DEC_MODE_AESKW_CRYPT			SE_AES_CFG_DEC_MODE(13)
#define SE_AES_DEC_MODE_GCM2				SE_AES_CFG_DEC_MODE(21)

#define SE_AES_CFG_ENC_ALG(x)				FIELD_PREP(GENMASK(15, 12), x)
#define SE_AES_ENC_ALG_NOP				SE_AES_CFG_ENC_ALG(0)
#define SE_AES_ENC_ALG_AES_ENC				SE_AES_CFG_ENC_ALG(1)
#define SE_AES_ENC_ALG_RNG				SE_AES_CFG_ENC_ALG(2)
#define SE_AES_ENC_ALG_SHA				SE_AES_CFG_ENC_ALG(3)
#define SE_AES_ENC_ALG_SM4_ENC				SE_AES_CFG_ENC_ALG(5)
#define SE_AES_ENC_ALG_HMAC				SE_AES_CFG_ENC_ALG(7)
#define SE_AES_ENC_ALG_KDF				SE_AES_CFG_ENC_ALG(8)
#define SE_AES_ENC_ALG_KEY_INVLD			SE_AES_CFG_ENC_ALG(10)
#define SE_AES_ENC_ALG_KEY_MOV				SE_AES_CFG_ENC_ALG(11)
#define SE_AES_ENC_ALG_KEY_INQUIRE			SE_AES_CFG_ENC_ALG(12)
#define SE_AES_ENC_ALG_INS				SE_AES_CFG_ENC_ALG(13)
#define SE_AES_ENC_ALG_CLONE				SE_AES_CFG_ENC_ALG(14)
#define SE_AES_ENC_ALG_LOCK				SE_AES_CFG_ENC_ALG(15)

#define SE_AES_CFG_DEC_ALG(x)				FIELD_PREP(GENMASK(11, 8), x)
#define SE_AES_DEC_ALG_NOP				SE_AES_CFG_DEC_ALG(0)
#define SE_AES_DEC_ALG_AES_DEC				SE_AES_CFG_DEC_ALG(1)
#define SE_AES_DEC_ALG_SM4_DEC				SE_AES_CFG_DEC_ALG(5)

#define SE_AES_CFG_DST(x)				FIELD_PREP(GENMASK(4, 2), x)
#define SE_AES_DST_MEMORY				SE_AES_CFG_DST(0)
#define SE_AES_DST_HASH_REG				SE_AES_CFG_DST(1)
#define SE_AES_DST_KEYTABLE				SE_AES_CFG_DST(2)
#define SE_AES_DST_SRK					SE_AES_CFG_DST(3)

/* AES Crypto Configuration */
#define SE_AES_KEY2_INDEX(x)				FIELD_PREP(GENMASK(31, 28), x)
#define SE_AES_KEY_INDEX(x)				FIELD_PREP(GENMASK(27, 24), x)

#define SE_AES_CRYPTO_CFG_SCC_DIS			FIELD_PREP(BIT(20), 1)

#define SE_AES_CRYPTO_CFG_CTR_CNTN(x)			FIELD_PREP(GENMASK(18, 11), x)

#define SE_AES_CRYPTO_CFG_IV_MODE(x)			FIELD_PREP(BIT(10), x)
#define SE_AES_IV_MODE_SWIV				SE_AES_CRYPTO_CFG_IV_MODE(0)
#define SE_AES_IV_MODE_HWIV				SE_AES_CRYPTO_CFG_IV_MODE(1)

#define SE_AES_CRYPTO_CFG_CORE_SEL(x)			FIELD_PREP(BIT(9), x)
#define SE_AES_CORE_SEL_DECRYPT				SE_AES_CRYPTO_CFG_CORE_SEL(0)
#define SE_AES_CORE_SEL_ENCRYPT				SE_AES_CRYPTO_CFG_CORE_SEL(1)

#define SE_AES_CRYPTO_CFG_IV_SEL(x)			FIELD_PREP(GENMASK(8, 7), x)
#define SE_AES_IV_SEL_UPDATED				SE_AES_CRYPTO_CFG_IV_SEL(1)
#define SE_AES_IV_SEL_REG				SE_AES_CRYPTO_CFG_IV_SEL(2)
#define SE_AES_IV_SEL_RANDOM				SE_AES_CRYPTO_CFG_IV_SEL(3)

#define SE_AES_CRYPTO_CFG_VCTRAM_SEL(x)			FIELD_PREP(GENMASK(6, 5), x)
#define SE_AES_VCTRAM_SEL_MEMORY			SE_AES_CRYPTO_CFG_VCTRAM_SEL(0)
#define SE_AES_VCTRAM_SEL_TWEAK				SE_AES_CRYPTO_CFG_VCTRAM_SEL(1)
#define SE_AES_VCTRAM_SEL_AESOUT			SE_AES_CRYPTO_CFG_VCTRAM_SEL(2)
#define SE_AES_VCTRAM_SEL_PREV_MEM			SE_AES_CRYPTO_CFG_VCTRAM_SEL(3)

#define SE_AES_CRYPTO_CFG_INPUT_SEL(x)			FIELD_PREP(GENMASK(4, 3), x)
#define SE_AES_INPUT_SEL_MEMORY				SE_AES_CRYPTO_CFG_INPUT_SEL(0)
#define SE_AES_INPUT_SEL_RANDOM				SE_AES_CRYPTO_CFG_INPUT_SEL(1)
#define SE_AES_INPUT_SEL_AESOUT				SE_AES_CRYPTO_CFG_INPUT_SEL(2)
#define SE_AES_INPUT_SEL_LINEAR_CTR			SE_AES_CRYPTO_CFG_INPUT_SEL(3)
#define SE_AES_INPUT_SEL_REG				SE_AES_CRYPTO_CFG_INPUT_SEL(1)

#define SE_AES_CRYPTO_CFG_XOR_POS(x)			FIELD_PREP(GENMASK(2, 1), x)
#define SE_AES_XOR_POS_BYPASS				SE_AES_CRYPTO_CFG_XOR_POS(0)
#define SE_AES_XOR_POS_BOTH				SE_AES_CRYPTO_CFG_XOR_POS(1)
#define SE_AES_XOR_POS_TOP				SE_AES_CRYPTO_CFG_XOR_POS(2)
#define SE_AES_XOR_POS_BOTTOM				SE_AES_CRYPTO_CFG_XOR_POS(3)

#define SE_AES_CRYPTO_CFG_HASH_EN(x)			FIELD_PREP(BIT(0), x)
#define SE_AES_HASH_DISABLE				SE_AES_CRYPTO_CFG_HASH_EN(0)
#define SE_AES_HASH_ENABLE				SE_AES_CRYPTO_CFG_HASH_EN(1)

#define SE_LAST_BLOCK_VAL(x)				FIELD_PREP(GENMASK(19, 0), x)
#define SE_LAST_BLOCK_RES_BITS(x)			FIELD_PREP(GENMASK(26, 20), x)

#define SE_AES_OP_LASTBUF				FIELD_PREP(BIT(16), 1)
#define SE_AES_OP_WRSTALL				FIELD_PREP(BIT(15), 1)
#define SE_AES_OP_FINAL					FIELD_PREP(BIT(5), 1)
#define SE_AES_OP_INIT					FIELD_PREP(BIT(4), 1)

#define SE_AES_OP_OP(x)					FIELD_PREP(GENMASK(2, 0), x)
#define SE_AES_OP_START					SE_AES_OP_OP(1)
#define SE_AES_OP_RESTART_OUT				SE_AES_OP_OP(2)
#define SE_AES_OP_RESTART_IN				SE_AES_OP_OP(4)
#define SE_AES_OP_RESTART_INOUT				SE_AES_OP_OP(5)
#define SE_AES_OP_DUMMY					SE_AES_OP_OP(6)

#define SE_KAC_SIZE(x)					FIELD_PREP(GENMASK(15, 14), x)
#define SE_KAC_SIZE_128					SE_KAC_SIZE(0)
#define SE_KAC_SIZE_192					SE_KAC_SIZE(1)
#define SE_KAC_SIZE_256					SE_KAC_SIZE(2)

#define SE_KAC_EXPORTABLE				FIELD_PREP(BIT(12), 1)

#define SE_KAC_PURPOSE(x)				FIELD_PREP(GENMASK(11, 8), x)
#define SE_KAC_ENC					SE_KAC_PURPOSE(0)
#define SE_KAC_CMAC					SE_KAC_PURPOSE(1)
#define SE_KAC_HMAC					SE_KAC_PURPOSE(2)
#define SE_KAC_GCM_KW					SE_KAC_PURPOSE(3)
#define SE_KAC_HMAC_KDK					SE_KAC_PURPOSE(6)
#define SE_KAC_HMAC_KDD					SE_KAC_PURPOSE(7)
#define SE_KAC_HMAC_KDD_KUW				SE_KAC_PURPOSE(8)
#define SE_KAC_XTS					SE_KAC_PURPOSE(9)
#define SE_KAC_GCM					SE_KAC_PURPOSE(10)

#define SE_KAC_USER_NS					FIELD_PREP(GENMASK(6, 4), 3)

#define SE_KAC2_USER(x)					FIELD_PREP(GENMASK(29, 22), x)
#define SE_KAC2_USER_GPSE				SE_KAC2_USER(3)
#define SE_KAC2_USER_GCSE				SE_KAC2_USER(5)

#define SE_KAC2_CLONEABLE				FIELD_PREP(BIT(21), 1)
#define SE_KAC2_EXPORTABLE				FIELD_PREP(BIT(20), 1)
#define SE_KAC2_DECRYPT_EN				FIELD_PREP(BIT(19), 1)
#define SE_KAC2_ENCRYPT_EN				FIELD_PREP(BIT(18), 1)

#define SE_KAC2_PURPOSE(x)				FIELD_PREP(GENMASK(17, 12), x)
#define SE_KAC2_ENC					SE_KAC2_PURPOSE(0)
#define SE_KAC2_CMAC					SE_KAC2_PURPOSE(1)
#define SE_KAC2_HMAC					SE_KAC2_PURPOSE(2)
#define SE_KAC2_GCM_KW					SE_KAC2_PURPOSE(3)
#define SE_KAC2_HMAC_KDK				SE_KAC2_PURPOSE(6)
#define SE_KAC2_HMAC_KDD				SE_KAC2_PURPOSE(7)
#define SE_KAC2_HMAC_KDD_KUW				SE_KAC2_PURPOSE(8)
#define SE_KAC2_XTS					SE_KAC2_PURPOSE(9)
#define SE_KAC2_GCM					SE_KAC2_PURPOSE(10)
#define SE_KAC2_CMAC_KDK				SE_KAC2_PURPOSE(12)
#define SE_KAC2_AES_KW					SE_KAC2_PURPOSE(13)
#define SE_KAC2_CTR_DRBG_ENT				SE_KAC2_PURPOSE(14)
#define SE_KAC2_CTR_DRBG_KV				SE_KAC2_PURPOSE(15)
#define SE_KAC2_GCM_HWIV				SE_KAC2_PURPOSE(16)
#define SE_KAC2_HARDEN_HMAC_KDK				SE_KAC2_PURPOSE(17)
#define SE_KAC2_HARDEN_HMAC_KDD				SE_KAC2_PURPOSE(18)

#define SE_KAC2_SIZE(x)					FIELD_PREP(GENMASK(11, 8), x)
#define SE_KAC2_SIZE_128				SE_KAC2_SIZE(0)
#define SE_KAC2_SIZE_192				SE_KAC2_SIZE(1)
#define SE_KAC2_SIZE_256				SE_KAC2_SIZE(2)

#define SE_KAC2_ORIGIN_SW				FIELD_PREP(BIT(7), 1)

#define SE_KAC2_SUBTYPE(x)				FIELD_PREP(GENMASK(6, 3), x)
#define SE_KAC2_SUBTYPE_AES				SE_KAC2_SUBTYPE(0)
#define SE_KAC2_SUBTYPE_SHA				SE_KAC2_SUBTYPE(0)
#define SE_KAC2_SUBTYPE_SM4				SE_KAC2_SUBTYPE(1)
#define SE_KAC2_SUBTYPE_SM3				SE_KAC2_SUBTYPE(1)

#define SE_KAC2_TYPE(x)					FIELD_PREP(GENMASK(2, 0), x)
#define SE_KAC2_TYPE_SYM				SE_KAC2_TYPE(2)

#define SE_KSLT_TABLE_ID_MASK				GENMASK(31, 26)
#define SE_KSLT_TABLE_ID(x)				FIELD_PREP(SE_KSLT_TABLE_ID_MASK, x)
#define SE_KSLT_TABLE_ID_GLOBAL				SE_KSLT_TABLE_ID(48)

#define SE_KSLT_REGION_ID(x)				FIELD_PREP(GENMASK(25, 16), x)
#define SE_KSLT_REGION_ID_SYM				SE_KSLT_REGION_ID(2)

#define SE_AES_KEY_DST_INDEX(x)				FIELD_PREP(GENMASK(11, 8), x)
#define SE_ADDR_HI_MSB(x)				FIELD_PREP(GENMASK(31, 24), x)
#define SE_ADDR_HI_SZ(x)				FIELD_PREP(GENMASK(23, 0), x)

#define SE_CFG_AES_ENCRYPT				(SE_AES_ENC_ALG_AES_ENC | \
							 SE_AES_DEC_ALG_NOP | \
							 SE_AES_DST_MEMORY)

#define SE_CFG_AES_DECRYPT				(SE_AES_ENC_ALG_NOP | \
							 SE_AES_DEC_ALG_AES_DEC | \
							 SE_AES_DST_MEMORY)

#define SE_CFG_GMAC_ENCRYPT				(SE_AES_ENC_ALG_AES_ENC | \
							 SE_AES_ENC_MODE_GMAC | \
							 SE_AES_DST_MEMORY)

#define SE_CFG_GMAC_DECRYPT				(SE_AES_DEC_ALG_AES_DEC | \
							 SE_AES_DEC_MODE_GMAC | \
							 SE_AES_DST_MEMORY)

#define SE_CFG_GCM_ENCRYPT				(SE_AES_ENC_ALG_AES_ENC | \
							 SE_AES_DEC_ALG_NOP | \
							 SE_AES_ENC_MODE_GCM | \
							 SE_AES_DST_MEMORY)

#define SE_CFG_GCM_DECRYPT				(SE_AES_DEC_ALG_AES_DEC | \
							 SE_AES_DEC_MODE_GCM | \
							 SE_AES_DST_MEMORY)

#define SE_CFG_GCM_FINAL_ENCRYPT			(SE_AES_ENC_ALG_AES_ENC | \
							 SE_AES_ENC_MODE_GCM_FINAL | \
							 SE_AES_DST_MEMORY)

#define SE_CFG_GCM_FINAL_DECRYPT			(SE_AES_DEC_ALG_AES_DEC | \
							 SE_AES_DEC_MODE_GCM_FINAL | \
							 SE_AES_DST_MEMORY)

#define SE_CFG_GCM_VERIFY				(SE_AES_DEC_ALG_AES_DEC | \
							 SE_AES_DEC_MODE_GCM_VERIFY | \
							 SE_AES_DST_MEMORY)

#define SE_CFG_CMAC					(SE_AES_ENC_ALG_AES_ENC | \
							 SE_AES_ENC_MODE_CMAC)

#define SE_CFG_CBC_MAC					(SE_AES_ENC_ALG_AES_ENC | \
							 SE_AES_ENC_MODE_CBC_MAC)

#define SE_CFG_SM4_ENCRYPT				(SE_AES_ENC_ALG_SM4_ENC | \
							 SE_AES_DEC_ALG_NOP | \
							 SE_AES_DST_MEMORY)

#define SE_CFG_SM4_DECRYPT				(SE_AES_ENC_ALG_NOP | \
							 SE_AES_DEC_ALG_SM4_DEC | \
							 SE_AES_DST_MEMORY)

#define SE_CFG_SM4_GMAC_ENCRYPT				(SE_AES_ENC_ALG_SM4_ENC | \
							 SE_AES_ENC_MODE_GMAC | \
							 SE_AES_DST_MEMORY)

#define SE_CFG_SM4_GMAC_DECRYPT				(SE_AES_DEC_ALG_SM4_DEC | \
							 SE_AES_DEC_MODE_GMAC | \
							 SE_AES_DST_MEMORY)

#define SE_CFG_SM4_GCM_ENCRYPT				(SE_AES_ENC_ALG_SM4_ENC | \
							 SE_AES_ENC_MODE_GCM | \
							 SE_AES_DST_MEMORY)

#define SE_CFG_SM4_GCM_DECRYPT				(SE_AES_DEC_ALG_SM4_DEC | \
							 SE_AES_DEC_MODE_GCM | \
							 SE_AES_DST_MEMORY)

#define SE_CFG_SM4_GCM_FINAL_ENCRYPT			(SE_AES_ENC_ALG_SM4_ENC | \
							 SE_AES_ENC_MODE_GCM_FINAL | \
							 SE_AES_DST_MEMORY)

#define SE_CFG_SM4_GCM_FINAL_DECRYPT			(SE_AES_DEC_ALG_SM4_DEC | \
							 SE_AES_DEC_MODE_GCM_FINAL | \
							 SE_AES_DST_MEMORY)

#define SE_CFG_SM4_GCM_VERIFY				(SE_AES_DEC_ALG_SM4_DEC | \
							 SE_AES_DEC_MODE_GCM_VERIFY | \
							 SE_AES_DST_MEMORY)

#define SE_CFG_SM4_CMAC					(SE_AES_ENC_ALG_SM4_ENC | \
							 SE_AES_ENC_MODE_CMAC)

#define SE_CFG_INS					(SE_AES_ENC_ALG_INS | \
							 SE_AES_DEC_ALG_NOP)

#define SE_CFG_MOV					(SE_AES_ENC_ALG_KEY_MOV | \
							 SE_AES_DEC_ALG_NOP)

#define SE_CFG_INVLD					(SE_AES_ENC_ALG_KEY_INVLD | \
							 SE_AES_DEC_ALG_NOP)

#define SE_CFG_ECB_ENCRYPT				(SE_AES_ENC_MODE_ECB | \
							 SE_CFG_AES_ENCRYPT)

#define SE_CFG_ECB_DECRYPT				(SE_AES_DEC_MODE_ECB | \
							 SE_CFG_AES_DECRYPT)

#define SE_CFG_CBC_ENCRYPT				(SE_AES_ENC_MODE_CBC | \
							 SE_CFG_AES_ENCRYPT)

#define SE_CFG_CBC_DECRYPT				(SE_AES_DEC_MODE_CBC | \
							 SE_CFG_AES_DECRYPT)

#define SE_CFG_OFB_ENCRYPT				(SE_AES_ENC_MODE_OFB | \
							 SE_CFG_AES_ENCRYPT)

#define SE_CFG_OFB_DECRYPT				(SE_AES_DEC_MODE_OFB | \
							 SE_CFG_AES_DECRYPT)

#define SE_CFG_CTR_ENCRYPT				(SE_AES_ENC_MODE_CTR | \
							 SE_CFG_AES_ENCRYPT)

#define SE_CFG_CTR_DECRYPT				(SE_AES_DEC_MODE_CTR | \
							 SE_CFG_AES_DECRYPT)

#define SE_CFG_XTS_ENCRYPT				(SE_AES_ENC_MODE_XTS | \
							 SE_CFG_AES_ENCRYPT)

#define SE_CFG_XTS_DECRYPT				(SE_AES_DEC_MODE_XTS | \
							 SE_CFG_AES_DECRYPT)

#define SE_CFG_SM4_ECB_ENCRYPT				(SE_AES_ENC_MODE_ECB | \
							 SE_CFG_SM4_ENCRYPT)

#define SE_CFG_SM4_ECB_DECRYPT				(SE_AES_DEC_MODE_ECB | \
							 SE_CFG_SM4_DECRYPT)

#define SE_CFG_SM4_CBC_ENCRYPT				(SE_AES_ENC_MODE_CBC | \
							 SE_CFG_SM4_ENCRYPT)

#define SE_CFG_SM4_CBC_DECRYPT				(SE_AES_DEC_MODE_CBC | \
							 SE_CFG_SM4_DECRYPT)

#define SE_CFG_SM4_OFB_ENCRYPT				(SE_AES_ENC_MODE_OFB | \
							 SE_CFG_SM4_ENCRYPT)

#define SE_CFG_SM4_OFB_DECRYPT				(SE_AES_DEC_MODE_OFB | \
							 SE_CFG_SM4_DECRYPT)

#define SE_CFG_SM4_CTR_ENCRYPT				(SE_AES_ENC_MODE_CTR | \
							 SE_CFG_SM4_ENCRYPT)

#define SE_CFG_SM4_CTR_DECRYPT				(SE_AES_DEC_MODE_CTR | \
							 SE_CFG_SM4_DECRYPT)

#define SE_CFG_SM4_XTS_ENCRYPT				(SE_AES_ENC_MODE_XTS | \
							 SE_CFG_SM4_ENCRYPT)

#define SE_CFG_SM4_XTS_DECRYPT				(SE_AES_DEC_MODE_XTS | \
							 SE_CFG_SM4_ENCRYPT)

#define SE_CRYPTO_CFG_ECB_ENCRYPT			(SE_AES_INPUT_SEL_MEMORY | \
							 SE_AES_XOR_POS_BYPASS | \
							 SE_AES_CORE_SEL_ENCRYPT)

#define SE_CRYPTO_CFG_ECB_DECRYPT			(SE_AES_INPUT_SEL_MEMORY | \
							 SE_AES_XOR_POS_BYPASS | \
							 SE_AES_CORE_SEL_DECRYPT)

#define SE_CRYPTO_CFG_CBC_ENCRYPT			(SE_AES_INPUT_SEL_MEMORY | \
							 SE_AES_VCTRAM_SEL_AESOUT | \
							 SE_AES_XOR_POS_TOP | \
							 SE_AES_CORE_SEL_ENCRYPT | \
							 SE_AES_IV_SEL_REG)

#define SE_CRYPTO_CFG_CBC_DECRYPT			(SE_AES_INPUT_SEL_MEMORY | \
							 SE_AES_VCTRAM_SEL_PREV_MEM | \
							 SE_AES_XOR_POS_BOTTOM | \
							 SE_AES_CORE_SEL_DECRYPT | \
							 SE_AES_IV_SEL_REG)

#define SE_CRYPTO_CFG_CTR				(SE_AES_INPUT_SEL_LINEAR_CTR | \
							 SE_AES_VCTRAM_SEL_MEMORY | \
							 SE_AES_XOR_POS_BOTTOM | \
							 SE_AES_CORE_SEL_ENCRYPT | \
							 SE_AES_CRYPTO_CFG_CTR_CNTN(1) | \
							 SE_AES_IV_SEL_REG)

#define SE_CRYPTO_CFG_XTS_ENCRYPT			(SE_AES_INPUT_SEL_MEMORY | \
							 SE_AES_VCTRAM_SEL_TWEAK | \
							 SE_AES_XOR_POS_BOTH | \
							 SE_AES_CORE_SEL_ENCRYPT | \
							 SE_AES_IV_SEL_REG)

#define SE_CRYPTO_CFG_XTS_DECRYPT			(SE_AES_INPUT_SEL_MEMORY | \
							 SE_AES_VCTRAM_SEL_TWEAK | \
							 SE_AES_XOR_POS_BOTH | \
							 SE_AES_CORE_SEL_DECRYPT | \
							 SE_AES_IV_SEL_REG)

#define SE_CRYPTO_CFG_XTS_DECRYPT			(SE_AES_INPUT_SEL_MEMORY | \
							 SE_AES_VCTRAM_SEL_TWEAK | \
							 SE_AES_XOR_POS_BOTH | \
							 SE_AES_CORE_SEL_DECRYPT | \
							 SE_AES_IV_SEL_REG)

#define SE_CRYPTO_CFG_CBC_MAC				(SE_AES_INPUT_SEL_MEMORY | \
							 SE_AES_VCTRAM_SEL_AESOUT | \
							 SE_AES_XOR_POS_TOP | \
							 SE_AES_CORE_SEL_ENCRYPT | \
							 SE_AES_HASH_ENABLE | \
							 SE_AES_IV_SEL_REG)

#define HASH_RESULT_REG_COUNT				50
#define CMAC_RESULT_REG_COUNT				4

#define SE_CRYPTO_CTR_REG_COUNT			4
#define SE_MAX_KEYSLOT				15
#define SE_MAX_MEM_ALLOC			SZ_4M

#define SE_GCM_VERIFY_OK	0x5a5a5a5a

#define SHA_FIRST	BIT(0)
#define SHA_INIT	BIT(1)
#define SHA_UPDATE	BIT(2)
#define SHA_FINAL	BIT(3)

#define TEGRA_AES_RESERVED_KSLT			14
#define TEGRA_XTS_RESERVED_KSLT			15

#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
#define CRYPTO_REGISTER(alg, x) \
		crypto_engine_register_##alg(x)
#else
#define CRYPTO_REGISTER(alg, x) \
		crypto_register_##alg(x)
#endif

#ifdef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
#define CRYPTO_UNREGISTER(alg, x) \
		crypto_engine_unregister_##alg(x)
#else
#define CRYPTO_UNREGISTER(alg, x) \
		crypto_unregister_##alg(x)
#endif

/* Security Engine operation modes */
enum se_aes_alg {
	SE_ALG_CBC,		/* Cipher Block Chaining (CBC) mode */
	SE_ALG_ECB,		/* Electronic Codebook (ECB) mode */
	SE_ALG_CTR,		/* Counter (CTR) mode */
	SE_ALG_XTS,		/* XTS mode */
	SE_ALG_GMAC,		/* GMAC mode */
	SE_ALG_GCM,		/* GCM mode */
	SE_ALG_GCM_FINAL,	/* GCM FINAL mode */
	SE_ALG_GCM_VERIFY,	/* GCM Verify */
	SE_ALG_CMAC,		/* Cipher-based MAC (CMAC) mode */
	SE_ALG_CMAC_FINAL,	/* Cipher-based MAC (CMAC) mode final task */
	SE_ALG_CBC_MAC,		/* CBC MAC mode */

	/* ShāngMì 4  Algorithms */
	SE_ALG_SM4_CBC,
	SE_ALG_SM4_ECB,
	SE_ALG_SM4_CTR,
	SE_ALG_SM4_OFB,
	SE_ALG_SM4_XTS,
	SE_ALG_SM4_GMAC,
	SE_ALG_SM4_GCM,
	SE_ALG_SM4_GCM_FINAL,
	SE_ALG_SM4_GCM_VERIFY,
	SE_ALG_SM4_CMAC,
	SE_ALG_SM4_CMAC_FINAL,
};

enum se_hash_alg {
	SE_ALG_RNG_DRBG,	/* Deterministic Random Bit Generator */
	SE_ALG_SHA1,		/* Secure Hash Algorithm-1 (SHA1) mode */
	SE_ALG_SHA224,		/* Secure Hash Algorithm-224  (SHA224) mode */
	SE_ALG_SHA256,		/* Secure Hash Algorithm-256  (SHA256) mode */
	SE_ALG_SHA384,		/* Secure Hash Algorithm-384  (SHA384) mode */
	SE_ALG_SHA512,		/* Secure Hash Algorithm-512  (SHA512) mode */
	SE_ALG_SHA3_224,	/* Secure Hash Algorithm3-224 (SHA3-224) mode */
	SE_ALG_SHA3_256,	/* Secure Hash Algorithm3-256 (SHA3-256) mode */
	SE_ALG_SHA3_384,	/* Secure Hash Algorithm3-384 (SHA3-384) mode */
	SE_ALG_SHA3_512,	/* Secure Hash Algorithm3-512 (SHA3-512) mode */
	SE_ALG_SM3_256,		/* ShangMi 3 - 256 */
	SE_ALG_SHAKE128,	/* Secure Hash Algorithm3 (SHAKE128) mode */
	SE_ALG_SHAKE256,	/* Secure Hash Algorithm3 (SHAKE256) mode */
	SE_ALG_HMAC_SHA224,	/* Hash based MAC (HMAC) - 224 */
	SE_ALG_HMAC_SHA256,	/* Hash based MAC (HMAC) - 256 */
	SE_ALG_HMAC_SHA384,	/* Hash based MAC (HMAC) - 384 */
	SE_ALG_HMAC_SHA512,	/* Hash based MAC (HMAC) - 512 */
};

struct tegra_se_alg {
	struct tegra_se *se_dev;
	const char *alg_base;

	union {
#ifndef NV_CONFTEST_REMOVE_STRUCT_CRYPTO_ENGINE_CTX
		struct skcipher_alg skcipher;
		struct aead_alg aead;
		struct ahash_alg ahash;
#else
		struct skcipher_engine_alg skcipher;
		struct aead_engine_alg aead;
		struct ahash_engine_alg ahash;
#endif
	} alg;
};

struct tegra_se_regs {
	u32 op;
	u32 config;
	u32 last_blk;
	u32 linear_ctr;
	u32 out_addr;
	u32 aad_len;
	u32 cryp_msg_len;
	u32 manifest;
	u32 key_addr;
	u32 key_data;
	u32 key_dst;
	u32 src_kslt;
	u32 tgt_kslt;
	u32 result;
};

struct tegra_se_regcfg {
	int (*cfg)(u32 alg, bool encrypt);
	int (*crypto_cfg)(u32 alg, bool encrypt);
	int (*manifest)(u32 user, u32 alg, u32 keylen);
};

struct tegra_se_hw {
	const struct tegra_se_regs *regs;
	int (*init_alg)(struct tegra_se *se);
	void (*deinit_alg)(struct tegra_se *se);
	bool support_kds;
	bool support_sm_alg;
	bool support_aad_verify;
	u32 host1x_class;
	u32 kac_ver;
};

struct tegra_se {
	const struct tegra_se_hw *hw;
	struct host1x_client client;
	struct host1x_channel *channel;
	struct tegra_se_regcfg *regcfg;
	struct tegra_se_cmdbuf *cmdbuf;
	struct tegra_se_cmdbuf *keybuf;
	struct crypto_engine *engine;
	struct host1x_syncpt *syncpt;
	struct device *dev;
	struct clk *clk;
	unsigned int opcode_addr;
	unsigned int stream_id;
	unsigned int syncpt_id;
	void __iomem *base;
	u32 owner;
};

struct tegra_se_cmdbuf {
	dma_addr_t iova;
	u32 *addr;
	struct device *dev;
	struct kref ref;
	struct host1x_bo bo;
	ssize_t size;
	u32 words;
};

struct tegra_se_datbuf {
	u8 *buf;
	dma_addr_t addr;
	ssize_t size;
};

static inline int se_algname_to_algid(const char *name)
{
	if (!strcmp(name, "cbc(aes)"))
		return SE_ALG_CBC;
	else if (!strcmp(name, "ecb(aes)"))
		return SE_ALG_ECB;
	else if (!strcmp(name, "ctr(aes)"))
		return SE_ALG_CTR;
	else if (!strcmp(name, "xts(aes)"))
		return SE_ALG_XTS;
	else if (!strcmp(name, "cmac(aes)"))
		return SE_ALG_CMAC;
	else if (!strcmp(name, "cmac(aes)-final"))
		return SE_ALG_CMAC_FINAL;
	else if (!strcmp(name, "gcm(aes)"))
		return SE_ALG_GCM;
	else if (!strcmp(name, "gcm(aes)-mac"))
		return SE_ALG_GMAC;
	else if (!strcmp(name, "gcm(aes)-final"))
		return SE_ALG_GCM_FINAL;
	else if (!strcmp(name, "ccm(aes)"))
		return SE_ALG_CBC_MAC;

	else if (!strcmp(name, "cbc(sm4)"))
		return SE_ALG_SM4_CBC;
	else if (!strcmp(name, "ecb(sm4)"))
		return SE_ALG_SM4_ECB;
	else if (!strcmp(name, "ofb(sm4)"))
		return SE_ALG_SM4_OFB;
	else if (!strcmp(name, "ctr(sm4)"))
		return SE_ALG_SM4_CTR;
	else if (!strcmp(name, "xts(sm4)"))
		return SE_ALG_SM4_XTS;
	else if (!strcmp(name, "cmac(sm4)"))
		return SE_ALG_SM4_CMAC;
	else if (!strcmp(name, "cmac(sm4)-final"))
		return SE_ALG_SM4_CMAC_FINAL;
	else if (!strcmp(name, "gcm(sm4)"))
		return SE_ALG_SM4_GCM;
	else if (!strcmp(name, "gcm(sm4)-mac"))
		return SE_ALG_SM4_GMAC;
	else if (!strcmp(name, "gcm(sm4)-final"))
		return SE_ALG_SM4_GCM_FINAL;

	else if (!strcmp(name, "sha1"))
		return SE_ALG_SHA1;
	else if (!strcmp(name, "sha224"))
		return SE_ALG_SHA224;
	else if (!strcmp(name, "sha256"))
		return SE_ALG_SHA256;
	else if (!strcmp(name, "sha384"))
		return SE_ALG_SHA384;
	else if (!strcmp(name, "sha512"))
		return SE_ALG_SHA512;
	else if (!strcmp(name, "sha3-224"))
		return SE_ALG_SHA3_224;
	else if (!strcmp(name, "sha3-256"))
		return SE_ALG_SHA3_256;
	else if (!strcmp(name, "sha3-384"))
		return SE_ALG_SHA3_384;
	else if (!strcmp(name, "sha3-512"))
		return SE_ALG_SHA3_512;
	else if (!strcmp(name, "sm3"))
		return SE_ALG_SM3_256;
	else if (!strcmp(name, "hmac(sha224)"))
		return SE_ALG_HMAC_SHA224;
	else if (!strcmp(name, "hmac(sha256)"))
		return SE_ALG_HMAC_SHA256;
	else if (!strcmp(name, "hmac(sha384)"))
		return SE_ALG_HMAC_SHA384;
	else if (!strcmp(name, "hmac(sha512)"))
		return SE_ALG_HMAC_SHA512;
	else
		return -EINVAL;
}

/* Functions */
int tegra_init_aes(struct tegra_se *se);
int tegra_init_hash(struct tegra_se *se);
int tegra_init_sm4(struct tegra_se *se);
void tegra_deinit_aes(struct tegra_se *se);
void tegra_deinit_hash(struct tegra_se *se);
void tegra_deinit_sm4(struct tegra_se *se);
int tegra_key_submit(struct tegra_se *se, const u8 *key,
		     u32 keylen, u32 alg, u32 *keyid);
void tegra_key_invalidate(struct tegra_se *se, u32 keyid, u32 alg);
int tegra_key_submit_reserved(struct tegra_se *se, const u8 *key,
				u32 keylen, u32 alg, u32 *keyid);
void tegra_key_invalidate_reserved(struct tegra_se *se, u32 keyid, u32 alg);
unsigned int tegra_key_get_idx(struct tegra_se *se, u32 keyid);
int tegra_se_host1x_submit(struct tegra_se *se, struct tegra_se_cmdbuf *cmdbuf, u32 size);

u32 tegra_kds_get_id(void);
void tegra_kds_free_id(u32 keyid);
bool tegra_key_in_kds(u32 keyid);

static inline int tegra_key_submit_reserved_aes(struct tegra_se *se, const u8 *key,
						u32 keylen, u32 alg, u32 *keyid)
{
	*keyid = TEGRA_AES_RESERVED_KSLT;
	return tegra_key_submit_reserved(se, key, keylen, alg, keyid);
}

static inline int tegra_key_submit_reserved_xts(struct tegra_se *se, const u8 *key,
						u32 keylen, u32 alg, u32 *keyid)
{
	*keyid = TEGRA_XTS_RESERVED_KSLT;
	return tegra_key_submit_reserved(se, key, keylen, alg, keyid);
}

static inline bool tegra_key_is_reserved(u32 keyid)
{
	return ((keyid == TEGRA_AES_RESERVED_KSLT) ||
		(keyid == TEGRA_XTS_RESERVED_KSLT));
}

/* HOST1x OPCODES */
static inline u32 host1x_opcode_setpayload(unsigned int payload)
{
	return (9 << 28) | payload;
}

static inline u32 host1x_opcode_incr_w(unsigned int offset)
{
	/* 22-bit offset supported */
	return (10 << 28) | offset;
}

static inline u32 host1x_opcode_nonincr_w(unsigned int offset)
{
	/* 22-bit offset supported */
	return (11 << 28) | offset;
}

static inline u32 host1x_opcode_incr(unsigned int offset, unsigned int count)
{
	return (1 << 28) | (offset << 16) | count;
}

static inline u32 host1x_opcode_nonincr(unsigned int offset, unsigned int count)
{
	return (2 << 28) | (offset << 16) | count;
}

static inline u32 host1x_uclass_incr_syncpt_cond_f(u32 v)
{
	return (v & 0xff) << 10;
}

static inline u32 host1x_uclass_incr_syncpt_indx_f(u32 v)
{
	return (v & 0x3ff) << 0;
}

static inline u32 host1x_uclass_wait_syncpt_r(void)
{
	return 0x8;
}

static inline u32 host1x_uclass_incr_syncpt_r(void)
{
	return 0x0;
}

#if !defined(NV_TEGRA_DEV_IOMMU_GET_STREAM_ID_PRESENT)
static inline bool tegra_dev_iommu_get_stream_id(struct device *dev, u32 *stream_id)
{
#ifdef CONFIG_IOMMU_API
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

	if (fwspec && fwspec->num_ids == 1) {
		*stream_id = fwspec->ids[0] & 0xffff;
		return true;
	}
#endif

	return false;
}
#endif

#define se_host1x_opcode_incr_w(x) host1x_opcode_incr_w((x) / 4)
#define se_host1x_opcode_nonincr_w(x) host1x_opcode_nonincr_w((x) / 4)
#define se_host1x_opcode_incr(x, y) host1x_opcode_incr((x) / 4, y)
#define se_host1x_opcode_nonincr(x, y) host1x_opcode_nonincr((x) / 4, y)

#endif /*_TEGRA_SE_H*/
