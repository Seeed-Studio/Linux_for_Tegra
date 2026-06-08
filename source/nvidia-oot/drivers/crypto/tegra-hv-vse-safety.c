// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 *
 * Cryptographic API.
 */

#include <nvidia/conftest.h>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <crypto/scatterwalk.h>
#include <crypto/algapi.h>
#include <crypto/skcipher.h>
#include <crypto/internal/rng.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/skcipher.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <crypto/sha3.h>
#include <crypto/sm3.h>
#include <linux/delay.h>
#include <soc/tegra/virt/hv-ivc.h>
#include <linux/iommu.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/host1x.h>
#include <linux/version.h>
#include <linux/dma-buf.h>

#include "tegra-hv-vse.h"

#define SE_MAX_SCHEDULE_TIMEOUT					LONG_MAX
#define TEGRA_HV_VSE_SHA_MAX_LL_NUM_1				1
#define TEGRA_HV_VSE_AES_CMAC_MAX_LL_NUM			1
#define TEGRA_HV_VSE_MAX_TASKS_PER_SUBMIT			1
#define TEGRA_HV_VSE_MAX_TSEC_TASKS_PER_SUBMIT		1
#define TEGRA_HV_VSE_TIMEOUT			(msecs_to_jiffies(10000))
#define TEGRA_HV_VSE_SHA_MAX_BLOCK_SIZE				128
#define TEGRA_VIRTUAL_SE_AES_BLOCK_SIZE				16
#define TEGRA_VIRTUAL_SE_AES_GCM_TAG_SIZE			16
#define TEGRA_VIRTUAL_SE_AES_GCM_TAG_IV_SIZE		32
#define TEGRA_VIRTUAL_SE_AES_MIN_KEY_SIZE			16
#define TEGRA_VIRTUAL_SE_AES_MAX_KEY_SIZE			32
#define TEGRA_VIRTUAL_SE_AES_IV_SIZE				16
#define TEGRA_VIRTUAL_SE_AES_GCM_IV_SIZE			12
#define TEGRA_VIRTUAL_SE_AES_MAX_IV_SIZE			TEGRA_VIRTUAL_SE_AES_IV_SIZE

/* Virtual Security Engines */
#define TEGRA_VIRTUAL_SE_CMD_ENG_AES     0x01000000U
#define TEGRA_VIRTUAL_SE_CMD_ENG_SHA     0x02000000U
#define TEGRA_VIRTUAL_SE_CMD_ENG_TSEC    0x03000000U

/* Command categories for AES Engine */
#define TEGRA_VIRTUAL_SE_CMD_CATEGORY_ENC_DEC      0x00010000U
#define TEGRA_VIRTUAL_SE_CMD_CATEGORY_AUTH         0x00020000U
#define TEGRA_VIRTUAL_SE_CMD_CATEGORY_RNG          0x00030000U

/* Command categories for SHA Engine */
#define TEGRA_VIRTUAL_SE_CMD_CATEGORY_SHA          0x00010000U
#define TEGRA_VIRTUAL_SE_CMD_CATEGORY_HMAC         0x00030000U

/* Command categories for TSEC Engine */
#define TEGRA_VIRTUAL_SE_CMD_CATEGORY_TSEC_KEYS    0x00010000U
#define TEGRA_VIRTUAL_SE_CMD_CATEGORY_TSEC_AUTH    0x00020000U

/* Command sets for Encryption/Decryption with AES Engine */
#define TEGRA_VIRTUAL_SE_CMD_SET_AES_ENC_DEC    0x00001000U
#define TEGRA_VIRTUAL_SE_CMD_SET_GCM_ENC_DEC    0x00002000U

/* Command sets for Authentication using AES engine */
#define TEGRA_VIRTUAL_SE_CMD_SET_CMAC           0x00001000U
#define TEGRA_VIRTUAL_SE_CMD_SET_GMAC           0x00002000U

/* Commands in the AES Encryption/Decryption set */
#define TEGRA_VIRTUAL_SE_CMD_OP_AES_ENC_INIT            0x00000001U
#define TEGRA_VIRTUAL_SE_CMD_OP_AES_ENC                 0x00000002U
#define TEGRA_VIRTUAL_SE_CMD_OP_AES_DEC                 0x00000003U

/* Commands in the GCM Encryption/Decryption set */
#define TEGRA_VIRTUAL_SE_CMD_OP_GCM_ENC                 0x00000001U
#define TEGRA_VIRTUAL_SE_CMD_OP_GCM_DEC                 0x00000002U
#define TEGRA_VIRTUAL_SE_CMD_OP_GCM_GET_DEC             0x00000003U

/* Commands in the CMAC Authentication set*/
#define TEGRA_VIRTUAL_SE_CMD_OP_CMAC_SIGN               0x00000001U
#define TEGRA_VIRTUAL_SE_CMD_OP_CMAC_VERIFY             0x00000002U
#define TEGRA_VIRTUAL_SE_CMD_OP_CMAC_GET_SIGN           0x00000003U
#define TEGRA_VIRTUAL_SE_CMD_OP_CMAC_GET_VERIFY         0x00000004U

/* Commands in the GMAC Authentication set */
#define TEGRA_VIRTUAL_SE_CMD_OP_GMAC_INIT               0x00000001U
#define TEGRA_VIRTUAL_SE_CMD_OP_GMAC_SIGN               0x00000002U
#define TEGRA_VIRTUAL_SE_CMD_OP_GMAC_VERIFY             0x00000003U
#define TEGRA_VIRTUAL_SE_CMD_OP_GMAC_GET_VERIFY         0x00000004U
#define TEGRA_VIRTUAL_SE_CMD_OP_GMAC_GET_IV             0x00000005U

/* Commands in the AES RNG Category*/
#define TEGRA_VIRTUAL_SE_CMD_OP_AES_RNG                 0x00000001U

/* Commands in the SHA Category */
#define TEGRA_VIRTUAL_SE_CMD_OP_SHA                     0x00000001U

/* Commands in the HMAC Category */
#define TEGRA_VIRTUAL_SE_CMD_OP_HMAC_SIGN               0x00000001U
#define TEGRA_VIRTUAL_SE_CMD_OP_HMAC_VERIFY             0x00000002U
#define TEGRA_VIRTUAL_SE_CMD_OP_HMAC_GET_VERIFY         0x00000004U

/* Commands in the TSEC keys category */
#define TEGRA_VIRTUAL_SE_CMD_OP_TSEC_KEYLOAD_STATUS     0x00000001U

/* Commands in the TSEC Authentication category */
#define TEGRA_VIRTUAL_SE_CMD_OP_TSEC_CMAC_SIGN          \
        TEGRA_VIRTUAL_SE_CMD_OP_CMAC_SIGN
#define TEGRA_VIRTUAL_SE_CMD_OP_TSEC_GET_CMAC_SIGN      \
        TEGRA_VIRTUAL_SE_CMD_OP_CMAC_GET_SIGN
#define TEGRA_VIRTUAL_SE_CMD_OP_TSEC_CMAC_VERIFY        \
        TEGRA_VIRTUAL_SE_CMD_OP_CMAC_VERIFY
#define TEGRA_VIRTUAL_SE_CMD_OP_TSEC_GET_CMAC_VERIFY    \
        TEGRA_VIRTUAL_SE_CMD_OP_CMAC_GET_VERIFY

#define TEGRA_VIRTUAL_SE_CMD_AES_SET_KEY			0xF1
#define TEGRA_VIRTUAL_SE_CMD_AES_ALLOC_KEY			0xF0

#define TEGRA_VIRTUAL_SE_CMD_ALLOC_KEY				0x00040001U
#define TEGRA_VIRTUAL_SE_CMD_RELEASE_KEY			0x00040002U
#define TEGRA_VIRTUAL_SE_CMD_RELEASE_KEY_USING_GRIP	0x00040003U

#define TEGRA_VIRTUAL_SE_CMD_AES_ENCRYPT_INIT    (TEGRA_VIRTUAL_SE_CMD_ENG_AES \
        | TEGRA_VIRTUAL_SE_CMD_CATEGORY_ENC_DEC \
        | TEGRA_VIRTUAL_SE_CMD_SET_AES_ENC_DEC \
        | TEGRA_VIRTUAL_SE_CMD_OP_AES_ENC_INIT)
#define TEGRA_VIRTUAL_SE_CMD_AES_ENCRYPT    (TEGRA_VIRTUAL_SE_CMD_ENG_AES \
        | TEGRA_VIRTUAL_SE_CMD_CATEGORY_ENC_DEC \
        | TEGRA_VIRTUAL_SE_CMD_SET_AES_ENC_DEC \
        | TEGRA_VIRTUAL_SE_CMD_OP_AES_ENC)
#define TEGRA_VIRTUAL_SE_CMD_AES_DECRYPT    (TEGRA_VIRTUAL_SE_CMD_ENG_AES \
        | TEGRA_VIRTUAL_SE_CMD_CATEGORY_ENC_DEC \
        | TEGRA_VIRTUAL_SE_CMD_SET_AES_ENC_DEC \
        | TEGRA_VIRTUAL_SE_CMD_OP_AES_DEC)

#define TEGRA_VIRTUAL_SE_CMD_AES_CMAC				0x23
#define TEGRA_VIRTUAL_SE_CMD_AES_CMAC_GEN_SUBKEY	0x24

#define TEGRA_VIRTUAL_SE_CMD_AES_RNG_DBRG    (TEGRA_VIRTUAL_SE_CMD_ENG_AES \
        | TEGRA_VIRTUAL_SE_CMD_CATEGORY_RNG \
        | TEGRA_VIRTUAL_SE_CMD_OP_AES_RNG)

#define TEGRA_VIRTUAL_SE_CMD_AES_GCM_CMD_ENCRYPT    (TEGRA_VIRTUAL_SE_CMD_ENG_AES \
        | TEGRA_VIRTUAL_SE_CMD_CATEGORY_ENC_DEC \
        | TEGRA_VIRTUAL_SE_CMD_SET_GCM_ENC_DEC \
        | TEGRA_VIRTUAL_SE_CMD_OP_GCM_ENC)
#define TEGRA_VIRTUAL_SE_CMD_AES_GCM_CMD_DECRYPT    (TEGRA_VIRTUAL_SE_CMD_ENG_AES \
        | TEGRA_VIRTUAL_SE_CMD_CATEGORY_ENC_DEC \
        | TEGRA_VIRTUAL_SE_CMD_SET_GCM_ENC_DEC \
        | TEGRA_VIRTUAL_SE_CMD_OP_GCM_DEC)
#define TEGRA_VIRTUAL_SE_CMD_AES_CMD_GET_GCM_DEC    (TEGRA_VIRTUAL_SE_CMD_ENG_AES \
        | TEGRA_VIRTUAL_SE_CMD_CATEGORY_ENC_DEC \
        | TEGRA_VIRTUAL_SE_CMD_SET_GCM_ENC_DEC \
        | TEGRA_VIRTUAL_SE_CMD_OP_GCM_GET_DEC)

#define TEGRA_VIRTUAL_SE_CMD_AES_CMAC_SIGN    (TEGRA_VIRTUAL_SE_CMD_ENG_AES \
        | TEGRA_VIRTUAL_SE_CMD_CATEGORY_AUTH \
        | TEGRA_VIRTUAL_SE_CMD_SET_CMAC \
        | TEGRA_VIRTUAL_SE_CMD_OP_CMAC_SIGN)
#define TEGRA_VIRTUAL_SE_CMD_AES_CMAC_VERIFY    (TEGRA_VIRTUAL_SE_CMD_ENG_AES \
        | TEGRA_VIRTUAL_SE_CMD_CATEGORY_AUTH \
        | TEGRA_VIRTUAL_SE_CMD_SET_CMAC \
        | TEGRA_VIRTUAL_SE_CMD_OP_CMAC_VERIFY)
#define TEGRA_VIRTUAL_SE_CMD_AES_CMD_GET_CMAC_SIGN    (TEGRA_VIRTUAL_SE_CMD_ENG_AES \
        | TEGRA_VIRTUAL_SE_CMD_CATEGORY_AUTH \
        | TEGRA_VIRTUAL_SE_CMD_SET_CMAC \
        | TEGRA_VIRTUAL_SE_CMD_OP_CMAC_GET_SIGN)
#define TEGRA_VIRTUAL_SE_CMD_AES_CMD_GET_CMAC_VERIFY    (TEGRA_VIRTUAL_SE_CMD_ENG_AES \
        | TEGRA_VIRTUAL_SE_CMD_CATEGORY_AUTH \
        | TEGRA_VIRTUAL_SE_CMD_SET_CMAC \
        | TEGRA_VIRTUAL_SE_CMD_OP_CMAC_GET_VERIFY)

#define TEGRA_VIRTUAL_SE_CMD_AES_GMAC_CMD_INIT    (TEGRA_VIRTUAL_SE_CMD_ENG_AES \
        | TEGRA_VIRTUAL_SE_CMD_CATEGORY_AUTH \
        | TEGRA_VIRTUAL_SE_CMD_SET_GMAC \
        | TEGRA_VIRTUAL_SE_CMD_OP_GMAC_INIT)
#define TEGRA_VIRTUAL_SE_CMD_AES_GMAC_CMD_SIGN    (TEGRA_VIRTUAL_SE_CMD_ENG_AES \
        | TEGRA_VIRTUAL_SE_CMD_CATEGORY_AUTH \
        | TEGRA_VIRTUAL_SE_CMD_SET_GMAC \
        | TEGRA_VIRTUAL_SE_CMD_OP_GMAC_SIGN)
#define TEGRA_VIRTUAL_SE_CMD_AES_GMAC_CMD_VERIFY    (TEGRA_VIRTUAL_SE_CMD_ENG_AES \
        | TEGRA_VIRTUAL_SE_CMD_CATEGORY_AUTH \
        | TEGRA_VIRTUAL_SE_CMD_SET_GMAC \
        | TEGRA_VIRTUAL_SE_CMD_OP_GMAC_VERIFY)
#define TEGRA_VIRTUAL_SE_CMD_AES_CMD_GET_GMAC_IV    (TEGRA_VIRTUAL_SE_CMD_ENG_AES \
        | TEGRA_VIRTUAL_SE_CMD_CATEGORY_AUTH \
        | TEGRA_VIRTUAL_SE_CMD_SET_GMAC \
        | TEGRA_VIRTUAL_SE_CMD_OP_GMAC_GET_IV)
#define TEGRA_VIRTUAL_SE_CMD_AES_CMD_GET_GMAC_VERIFY    (TEGRA_VIRTUAL_SE_CMD_ENG_AES \
        | TEGRA_VIRTUAL_SE_CMD_CATEGORY_AUTH \
        | TEGRA_VIRTUAL_SE_CMD_SET_GMAC \
        | TEGRA_VIRTUAL_SE_CMD_OP_GMAC_GET_VERIFY)

#define TEGRA_VIRTUAL_TSEC_CMD_GET_KEYLOAD_STATUS    (TEGRA_VIRTUAL_SE_CMD_ENG_TSEC \
        | TEGRA_VIRTUAL_SE_CMD_CATEGORY_TSEC_KEYS \
        | TEGRA_VIRTUAL_SE_CMD_OP_TSEC_KEYLOAD_STATUS)
#define TEGRA_VIRTUAL_SE_CMD_TSEC_SIGN    (TEGRA_VIRTUAL_SE_CMD_ENG_TSEC \
        | TEGRA_VIRTUAL_SE_CMD_CATEGORY_TSEC_AUTH \
        | TEGRA_VIRTUAL_SE_CMD_OP_TSEC_CMAC_SIGN)
#define TEGRA_VIRTUAL_SE_CMD_TSEC_VERIFY    (TEGRA_VIRTUAL_SE_CMD_ENG_TSEC \
        | TEGRA_VIRTUAL_SE_CMD_CATEGORY_TSEC_AUTH \
        | TEGRA_VIRTUAL_SE_CMD_OP_TSEC_CMAC_VERIFY)
#define TEGRA_VIRTUAL_SE_CMD_AES_CMD_GET_TSEC_VERIFY    (TEGRA_VIRTUAL_SE_CMD_ENG_TSEC \
        | TEGRA_VIRTUAL_SE_CMD_CATEGORY_TSEC_AUTH \
        | TEGRA_VIRTUAL_SE_CMD_OP_TSEC_GET_CMAC_VERIFY)

#define TEGRA_VIRTUAL_SE_AES_GMAC_SV_CFG_FIRST_REQ_SHIFT	(0x00U)
#define TEGRA_VIRTUAL_SE_AES_GMAC_SV_CFG_LAST_REQ_SHIFT		(0x01U)

#define TEGRA_VIRTUAL_SE_CMD_SHA_HASH    (TEGRA_VIRTUAL_SE_CMD_ENG_SHA \
        | TEGRA_VIRTUAL_SE_CMD_CATEGORY_SHA \
        | TEGRA_VIRTUAL_SE_CMD_OP_SHA)

#define TEGRA_VIRTUAL_SE_CMD_HMAC_SIGN   (TEGRA_VIRTUAL_SE_CMD_ENG_SHA \
	| TEGRA_VIRTUAL_SE_CMD_CATEGORY_HMAC \
	| TEGRA_VIRTUAL_SE_CMD_OP_HMAC_SIGN)

#define TEGRA_VIRTUAL_SE_CMD_HMAC_VERIFY   (TEGRA_VIRTUAL_SE_CMD_ENG_SHA \
	| TEGRA_VIRTUAL_SE_CMD_CATEGORY_HMAC \
	| TEGRA_VIRTUAL_SE_CMD_OP_HMAC_VERIFY)

#define TEGRA_VIRTUAL_SE_CMD_HMAC_GET_VERIFY   (TEGRA_VIRTUAL_SE_CMD_ENG_SHA \
	| TEGRA_VIRTUAL_SE_CMD_CATEGORY_HMAC \
	| TEGRA_VIRTUAL_SE_CMD_OP_HMAC_GET_VERIFY)

#define TEGRA_VIRTUAL_SE_SHA_HASH_BLOCK_SIZE_512BIT		(512 / 8)
#define TEGRA_VIRTUAL_SE_SHA_HASH_BLOCK_SIZE_576BIT		(576 / 8)
#define TEGRA_VIRTUAL_SE_SHA_HASH_BLOCK_SIZE_832BIT		(832 / 8)
#define TEGRA_VIRTUAL_SE_SHA_HASH_BLOCK_SIZE_1024BIT	(1024 / 8)
#define TEGRA_VIRTUAL_SE_SHA_HASH_BLOCK_SIZE_1088BIT	(1088 / 8)
#define TEGRA_VIRTUAL_SE_SHA_HASH_BLOCK_SIZE_1344BIT	(1344 / 8)

#define TEGRA_VIRTUAL_SE_SHA_MAX_HMAC_SHA_LENGTH    (32U)

#define SHA3_STATE_SIZE	200

#define TEGRA_VIRTUAL_SE_TIMEOUT_1S				1000000

#define TEGRA_VIRTUAL_SE_AES_CMAC_DIGEST_SIZE			16U

#define TEGRA_VIRTUAL_SE_AES_CMAC_STATE_SIZE			16

#define TEGRA_VIRTUAL_SE_MAX_BUFFER_SIZE			0x1000000

#define TEGRA_VIRTUAL_SE_AES_KEYTBL_TYPE_KEY			1
#define TEGRA_VIRTUAL_SE_AES_KEYTBL_TYPE_OIV			2
#define TEGRA_VIRTUAL_SE_AES_KEYTBL_TYPE_UIV			4

#define TEGRA_VIRTUAL_SE_AES_KEYSLOT_LABEL_SIZE		16
#define TEGRA_VIRTUAL_SE_AES_KEYSLOT_LABEL			"NVSEAES"

#define TEGRA_VIRTUAL_SE_AES_LCTR_SIZE				16
#define TEGRA_VIRTUAL_SE_AES_LCTR_CNTN				1

#define TEGRA_VIRTUAL_SE_AES_CMAC_CONFIG_NONLASTBLK		0x00
#define TEGRA_VIRTUAL_SE_AES_CMAC_CONFIG_LASTBLK		0x01
#define TEGRA_VIRTUAL_SE_AES_CMAC_CONFIG_FINAL			0x02

#define TEGRA_VIRTUAL_SE_AES_CMAC_SV_CONFIG_FIRSTREQ	0x01
#define TEGRA_VIRTUAL_SE_AES_CMAC_SV_CONFIG_LASTREQ		0x02

#define TEGRA_VIRTUAL_SE_RNG_IV_SIZE	16
#define TEGRA_VIRTUAL_SE_RNG_DT_SIZE	16
#define TEGRA_VIRTUAL_SE_RNG_KEY_SIZE	16
#define TEGRA_VIRTUAL_SE_RNG_SEED_SIZE (TEGRA_VIRTUAL_SE_RNG_IV_SIZE + \
					TEGRA_VIRTUAL_SE_RNG_KEY_SIZE + \
					TEGRA_VIRTUAL_SE_RNG_DT_SIZE)

#define TEGRA_VIRTUAL_SE_MAX_SUPPORTED_BUFLEN		((1U << 24) - 1U)
#define TEGRA_VIRTUAL_SE_MAX_GCMDEC_BUFLEN			(0x500000U)		/* 5 MB	*/
#define TEGRA_VIRTUAL_TSEC_MAX_SUPPORTED_BUFLEN			(8U * 1024U)		/* 8 KB	*/

#define TEGRA_VIRTUAL_SE_ERR_MAC_INVALID	11

#define MAX_NUMBER_MISC_DEVICES			70U
#define MAX_IVC_Q_PRIORITY				2U
#define TEGRA_IVC_ID_OFFSET				0U
#define TEGRA_SE_ENGINE_ID_OFFSET			1U
#define TEGRA_CRYPTO_DEV_ID_OFFSET			2U
#define TEGRA_IVC_PRIORITY_OFFSET			3U
#define TEGRA_MAX_BUFFER_SIZE				4U
#define TEGRA_CHANNEL_GROUPID_OFFSET			5U
#define TEGRA_GCM_SUPPORTED_FLAG_OFFSET			7U
#define TEGRA_GCM_DEC_BUFFER_SIZE			8U
#define TEGRA_GCM_DEC_MEMPOOL_ID			9U
#define TEGRA_GCM_DEC_MEMPOOL_SIZE			10U
#define TEGRA_IVCCFG_ARRAY_LEN				11U

#define VSE_MSG_ERR_TSEC_KEYLOAD_FAILED			21U
#define VSE_MSG_ERR_TSEC_KEYLOAD_STATUS_CHECK_TIMEOUT	20U

#define NVVSE_STATUS_SE_SERVER_TSEC_KEYLOAD_FAILED		105U
#define NVVSE_STATUS_SE_SERVER_TSEC_KEYLOAD_TIMEOUT		150U
#define NVVSE_STATUS_SE_SERVER_ERROR				102U
#define SE_HW_VALUE_MATCH_CODE						0x5A5A5A5A
#define SE_HW_VALUE_MISMATCH_CODE					0xBDBDBDBD
#define RESULT_COMPARE_BUF_SIZE				4U
#define AES_TAG_BUF_SIZE					64U
#define SHA_HASH_BUF_SIZE					1024U

#define NVVSE_TSEC_CMD_STATUS_ERR_MASK		((uint32_t)0xFFFFFFU)
#define UINT8_MAX (255)

#define VSE_ERR(...) pr_err("tegra_hv_vse_safety " __VA_ARGS__)

static struct crypto_dev_to_ivc_map g_crypto_to_ivc_map[MAX_NUMBER_MISC_DEVICES];
static struct tegra_vse_node_dma g_node_dma[MAX_NUMBER_MISC_DEVICES];

static bool gcm_supports_dma;
static struct device *gpcdma_dev;

/* Security Engine Linked List */
struct tegra_virtual_se_ll {
	dma_addr_t addr; /* DMA buffer address */
	u32 data_len; /* Data length in DMA buffer */
};

/* Tegra Virtual Security Engine commands */
enum tegra_virtual_se_command {
	VIRTUAL_SE_AES_CRYPTO,
	VIRTUAL_SE_KEY_SLOT,
	VIRTUAL_SE_PROCESS,
	VIRTUAL_CMAC_PROCESS,
	VIRTUAL_SE_AES_GCM_ENC_PROCESS
};

enum rng_call {
	HW_RNG = 0x5A5A5A5A,
	CRYPTODEV_RNG = 0xABABABAB
};

/* CMAC response */
struct tegra_vse_cmac_data {
	u32 status;
	u8 data[TEGRA_VIRTUAL_SE_AES_BLOCK_SIZE];
};

/*
 * @enum vse_sym_cipher_choice
 * @brief Symmetric cipher to be used for CMAC sign/verify
 * Currently two choices are supported - AES, SM4.
 */
enum vse_sym_cipher_choice {
	VSE_SYM_CIPH_AES = 0,
	VSE_SYM_CIPH_SM4 = 0xFFFFFFFF
};

struct tegra_vse_priv_data {
	struct skcipher_request *req;
	struct tegra_virtual_se_dev *se_dev;
	struct completion alg_complete;
	int cmd;
	uint8_t slot_num;
	struct scatterlist sg;
	void *buf;
	dma_addr_t buf_addr;
	u32 rx_status;
	u8 iv[TEGRA_VIRTUAL_SE_AES_MAX_IV_SIZE];
	struct tegra_vse_cmac_data cmac;
	uint32_t syncpt_id;
	uint32_t syncpt_threshold;
	uint32_t syncpt_id_valid;
};

struct tegra_virtual_se_addr {
	u32 lo;
	u32 hi;
};

struct tegra_virtual_se_addr64_buf_size {
	u64 addr;
	u32 buf_size;
};

struct key_args {
	uint8_t keyslot[KEYSLOT_SIZE_BYTES];
	uint32_t key_usage;
	uint32_t key_instance;
	uint32_t key_grp_id;
	uint32_t token_id;
};

union tegra_virtual_se_aes_args {
	struct keyiv {
		u8 slot[KEYSLOT_SIZE_BYTES];
		u32 length;
		u32 type;
		u8 data[32];
		u8 oiv[TEGRA_VIRTUAL_SE_AES_IV_SIZE];
		u8 uiv[TEGRA_VIRTUAL_SE_AES_IV_SIZE];
	} key;
	struct aes_encdec {
		u8 keyslot[KEYSLOT_SIZE_BYTES];
		uint32_t key_instance;
		uint32_t release_keyslot;
		u32 mode;
		u32 ivsel;
		u8 lctr[TEGRA_VIRTUAL_SE_AES_LCTR_SIZE];
		u32 ctr_cntn;
		u64 src_addr;
		u32 src_buf_size;
		u64 dst_addr;
		u32 dst_buf_size;
	} op;
	struct aes_gcm {

		/**
		 * keyslot handle returned by TOS as part of load key operation.
		 * It must be the first variable in the structure.
		 */
		uint8_t keyslot[KEYSLOT_SIZE_BYTES];

		uint32_t key_instance;
		uint32_t token_id;
		uint32_t release_keyslot;
		uint64_t dst_addr;
		uint32_t dst_buf_size;
		uint64_t src_addr;
		uint32_t src_buf_size;

		uint64_t aad_addr;
		uint32_t aad_buf_size;

		uint64_t tag_addr;
		uint32_t tag_buf_size;

		/* TODO: ESLC-6207: use lctr instead*/
		uint8_t iv[12];
		/* Config for AES-GMAC request */
		uint32_t config;
		u8 expected_tag[TEGRA_VIRTUAL_SE_AES_BLOCK_SIZE];
		uint64_t gcm_vrfy_res_addr;
		enum vse_sym_cipher_choice sym_ciph;
	} op_gcm;
	struct aes_cmac_sv {
		u8 keyslot[KEYSLOT_SIZE_BYTES];
		uint32_t token_id;
		u32 config;
		u32 lastblock_len;
		u8 lastblock[TEGRA_VIRTUAL_SE_AES_BLOCK_SIZE];
		u64 src_addr;
		u32 src_buf_size;
		u8 cmac_result[TEGRA_VIRTUAL_SE_AES_BLOCK_SIZE];
		u64 mac_addr;
		u64 mac_comp_res_addr;
		enum vse_sym_cipher_choice sym_ciph;
	} op_cmac_sv;
	struct aes_rng {
		struct tegra_virtual_se_addr dst_addr;
	} op_rng;
};

union tegra_virtual_se_sha_args {
	struct hash {
		u32 msg_total_length[4];
		u32 msg_left_length[4];
		u32 hash[50];
		u64 dst;
		u64 src_addr;
		u32 src_buf_size;
		u32 mode;
		u32 hash_length;
	} op_hash;
} __attribute__((__packed__));

struct tegra_virtual_se_hmac_sha_args {
	u8 keyslot[KEYSLOT_SIZE_BYTES];
	uint32_t token_id;
	u32 mode;
	u32 lastblock_len;
	u8 lastblock[TEGRA_VIRTUAL_SE_SHA_HASH_BLOCK_SIZE_512BIT];
	u32 msg_total_length[4];
	u32 msg_left_length[4];
	u64 dst_addr;
	u64 src_addr;
	u32 src_buf_size;
	u8 expected_hmac_sha[TEGRA_VIRTUAL_SE_SHA_MAX_HMAC_SHA_LENGTH];
	uint64_t hmac_addr;
};

struct tegra_virtual_tsec_args {
	/**
	 * Keyslot index for keyslot containing TSEC key
	 */
	uint64_t keyslot;

	/**
	 * IOVA address of the input buffer.
	 * Although it is a 64-bit integer, only least significant 40 bits are
	 * used because only a 40-bit address space is supported.
	 */
	uint64_t src_addr;

	/**
	 * IOVA address of the output buffer.
	 * It is expected to point to a buffer with size at least 16 bytes.
	 * Although it is a 64-bit integer, only least significant 40 bits are
	 * used because only a 40-bit address space is supported.
	 */
	uint64_t dst_addr;

	/**
	 * IOVA address of the buffer for status returned by TSEC firmware.
	 * It is expected to point to a buffer with size at least 4 bytes
	 * Although it is a 64-bit integer, only least significant 40 bits are
	 * used because only a 40-bit address space is supported.
	 */
	uint64_t fw_status_addr;

	/**
	 * Size of input buffer in bytes.
	 * The maximum size is given by the macro TEGRA_VIRTUAL_TSEC_MAX_SUPPORTED_BUFLEN
	 */
	uint32_t src_buf_size;

	/**
	 * For CMAC Verify, this array contains the value to be verified.
	 * Not used for CMAC Sign.
	 */
	uint8_t cmac_result[TEGRA_VIRTUAL_SE_AES_BLOCK_SIZE];
};

struct tegra_virtual_se_ivc_resp_msg_t {
	u32 tag;
	u32 cmd;
	u32 status;
	union {
		/** The init vector of AES-CBC encryption */
		unsigned char iv[TEGRA_VIRTUAL_SE_AES_IV_SIZE];
		/** Hash result for AES CMAC */
		unsigned char cmac_result[TEGRA_VIRTUAL_SE_AES_CMAC_DIGEST_SIZE];
		/** Keyslot for non */
		unsigned char keyslot;
	};
	uint32_t syncpt_id;
	uint32_t syncpt_threshold;
	uint32_t syncpt_id_valid;
};

struct tegra_virtual_se_ivc_tx_msg_t {
	u32 tag;
	u32 cmd;
	union {
		struct key_args keys;
		union tegra_virtual_se_aes_args aes;
		union tegra_virtual_se_sha_args sha;
		struct tegra_virtual_tsec_args tsec[TEGRA_HV_VSE_MAX_TSEC_TASKS_PER_SUBMIT];
		struct tegra_virtual_se_hmac_sha_args hmac;
	};
};

struct tegra_virtual_se_ivc_hdr_tag {
	struct tegra_vse_priv_data *priv_data;
	uint8_t unused[16U - sizeof(struct tegra_vse_priv_data *)];
} __attribute__((__packed__));

static_assert(sizeof(struct tegra_virtual_se_ivc_hdr_tag) == 16U);

struct tegra_virtual_se_ivc_hdr_t {
	u8 header_magic[4];
	u32 num_reqs;
	u32 engine;
	struct tegra_virtual_se_ivc_hdr_tag tag;
	u32 status;
};

struct tegra_virtual_se_ivc_msg_t {
	struct tegra_virtual_se_ivc_hdr_t ivc_hdr;
	union {
		struct tegra_virtual_se_ivc_tx_msg_t tx[TEGRA_HV_VSE_MAX_TASKS_PER_SUBMIT];
		struct tegra_virtual_se_ivc_resp_msg_t rx[TEGRA_HV_VSE_MAX_TSEC_TASKS_PER_SUBMIT];
	};
};

struct sha_zero_length_vector {
	unsigned int size;
	char *digest;
};

/* Tegra Virtual Security Engine operation modes */
enum tegra_virtual_se_op_mode {
	/* (SM3-256) mode */
	VIRTUAL_SE_OP_MODE_SM3 = 0,
	/* Secure Hash Algorithm-256  (SHA256) mode */
	VIRTUAL_SE_OP_MODE_SHA256 = 5,
	/* Secure Hash Algorithm-384  (SHA384) mode */
	VIRTUAL_SE_OP_MODE_SHA384,
	/* Secure Hash Algorithm-512  (SHA512) mode */
	VIRTUAL_SE_OP_MODE_SHA512,
	/* Secure Hash Algorithm-3  (SHA3-256) mode */
	VIRTUAL_SE_OP_MODE_SHA3_256 = 10,
	/* Secure Hash Algorithm-3  (SHA3-384) mode */
	VIRTUAL_SE_OP_MODE_SHA3_384,
	/* Secure Hash Algorithm-3  (SHA3-512) mode */
	VIRTUAL_SE_OP_MODE_SHA3_512,
	/* Secure Hash Algorithm-3  (SHAKE128) mode */
	VIRTUAL_SE_OP_MODE_SHAKE128,
	/* Secure Hash Algorithm-3  (SHAKE256) mode */
	VIRTUAL_SE_OP_MODE_SHAKE256,
};

enum tegra_virtual_se_aes_op_mode {
	AES_CBC = 0U,
	AES_CTR = 2U,
	AES_SM4_CBC = 0x10000U,
	AES_SM4_CTR = 0x10002U,
};

/* Security Engine request context */
struct tegra_virtual_se_aes_req_context {
	/* Security Engine device */
	struct tegra_virtual_se_dev *se_dev;
	/* Security Engine operation mode */
	enum tegra_virtual_se_aes_op_mode op_mode;
	/* Operation type */
	bool encrypt;
	/* Engine id */
	uint32_t engine_id;
};

enum se_engine_id {
	VIRTUAL_SE_AES0,
	VIRTUAL_SE_AES1,
	VIRTUAL_SE_SHA = 2,
	VIRTUAL_SE_TSEC = 6,
	VIRTUAL_GCSE1_AES0 = 7,
	VIRTUAL_GCSE1_AES1 = 8,
	VIRTUAL_GCSE1_SHA = 9,
	VIRTUAL_GCSE2_AES0 = 10,
	VIRTUAL_GCSE2_AES1 = 11,
	VIRTUAL_GCSE2_SHA = 12,
	VIRTUAL_MAX_SE_ENGINE_NUM = 13
};

enum tegra_virtual_se_aes_iv_type {
	AES_ORIGINAL_IV,
	AES_UPDATED_IV,
	AES_IV_REG
};

enum aes_buf_idx {
	AES_SRC_BUF_IDX,
	AES_AAD_BUF_IDX,
	AES_TAG_BUF_IDX,
	AES_COMP_BUF_IDX
};

enum sha_buf_idx {
	SHA_SRC_BUF_IDX,
	SHA_HASH_BUF_IDX,
	HMAC_SHA_COMP_BUF_IDX
};

enum tsec_buf_idx {
	TSEC_SRC_BUF_IDX,
	TSEC_MAC_BUF_IDX,
	TSEC_FW_STATUS_BUF_IDX
};

struct crypto_dev_to_ivc_map *tegra_hv_vse_get_db(void)
{
	return &g_crypto_to_ivc_map[0];
}
EXPORT_SYMBOL(tegra_hv_vse_get_db);

static int status_to_errno(u32 err)
{
	int32_t ret = 0;

	switch (err) {
	case 0:
		ret = 0;
		break;
	case 1:		/* VSE_MSG_ERR_INVALID_CMD */
	case 3:		/* VSE_MSG_ERR_INVALID_ARGS */
	case 11:	/* VSE_MSG_ERR_MAC_INVALID */
		ret = -EINVAL;
		break;
	case 4:		/* VSE_MSG_ERR_INVALID_KEY */
	case 5:		/* VSE_MSG_ERR_CTR_OVERFLOW */
	case 6:		/* VSE_MSG_ERR_INVALID_SUBKEY */
	case 7:		/* VSE_MSG_ERR_CTR_NONCE_INVALID */
	case 8:		/* VSE_MSG_ERR_GCM_IV_INVALID */
	case 9:		/* VSE_MSG_ERR_GCM_NONCE_INVALID */
	case 10:	/* VSE_MSG_ERR_GMAC_INVALID_PARAMS */
		ret = -EPERM;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int32_t validate_header(
	struct tegra_virtual_se_dev *se_dev,
	struct tegra_virtual_se_ivc_hdr_t *pivc_hdr,
	bool *is_dummy)
{
	int32_t ret = -EIO;

	if ((pivc_hdr->header_magic[0] == (uint8_t)'N') &&
			(pivc_hdr->header_magic[1] == (uint8_t)'V') &&
			(pivc_hdr->header_magic[2] == (uint8_t)'D') &&
			(pivc_hdr->header_magic[3] == (uint8_t)'A')) {
		pr_debug("Message header\n");
		*is_dummy = false;
		ret = 0;
	} else if ((pivc_hdr->header_magic[0] == (uint8_t)'D') &&
			(pivc_hdr->header_magic[1] == (uint8_t)'I') &&
			(pivc_hdr->header_magic[2] == (uint8_t)'S') &&
			(pivc_hdr->header_magic[3] == (uint8_t)'C')) {
		pr_debug("Filler\n");
		*is_dummy = true;
		ret = 0;
	} else {
		dev_err(se_dev->dev, "Invalid message header value.\n");
	}

	return ret;
}

static int is_aes_mode_valid(uint32_t opmode)
{
	int ret = 0;

	if ((opmode == (uint32_t)AES_CBC) || (opmode == (uint32_t)AES_SM4_CBC) ||
		(opmode == (uint32_t)AES_SM4_CTR) || (opmode == (uint32_t)AES_CTR)) {
		ret = 1;
	}
	return ret;
}

static int read_and_validate_dummy_msg(
	struct tegra_virtual_se_dev *se_dev,
	struct tegra_hv_ivc_cookie *pivck,
	uint32_t node_id, bool *is_dummy)
{
	int err = 0, read_size = -1;
	struct tegra_virtual_se_ivc_msg_t *ivc_msg =
		g_crypto_to_ivc_map[node_id].ivc_resp_msg;
	struct tegra_virtual_se_ivc_hdr_t *ivc_hdr;
	size_t size_ivc_msg = sizeof(struct tegra_virtual_se_ivc_msg_t);

	memset(ivc_msg, 0, sizeof(struct tegra_virtual_se_ivc_msg_t));
	read_size = tegra_hv_ivc_read(pivck, ivc_msg, size_ivc_msg);
	if (read_size > 0 && read_size < size_ivc_msg) {
		dev_err(se_dev->dev, "Wrong read msg len %d\n", read_size);
		return -EINVAL;
	}
	ivc_hdr = &(ivc_msg->ivc_hdr);

	err = validate_header(se_dev, ivc_hdr, is_dummy);

	return err;
}

static int read_and_validate_valid_msg(
	struct tegra_virtual_se_dev *se_dev,
	struct tegra_hv_ivc_cookie *pivck,
	uint32_t node_id, bool *is_dummy, bool waited)
{
	struct tegra_vse_priv_data *priv;
	struct tegra_virtual_se_ivc_hdr_t *ivc_hdr;
	struct tegra_virtual_se_ivc_msg_t *ivc_msg =
		g_crypto_to_ivc_map[node_id].ivc_resp_msg;
	struct tegra_virtual_se_aes_req_context *req_ctx;
	struct tegra_virtual_se_ivc_resp_msg_t *ivc_rx;
	enum ivc_irq_state *irq_state;

	int read_size = -1, err = 0;
	size_t size_ivc_msg = sizeof(struct tegra_virtual_se_ivc_msg_t);

	memset(ivc_msg, 0, sizeof(struct tegra_virtual_se_ivc_msg_t));
	irq_state = &(g_crypto_to_ivc_map[node_id].wait_interrupt);

	if (!tegra_hv_ivc_can_read(pivck)) {
		*irq_state = INTERMEDIATE_REQ_INTERRUPT;
		dev_info(se_dev->dev, "%s(): no valid message, await interrupt.\n", __func__);
		return -EAGAIN;
	}

	read_size = tegra_hv_ivc_read(pivck, ivc_msg, size_ivc_msg);
	if (read_size > 0 && read_size < size_ivc_msg) {
		dev_err(se_dev->dev, "Wrong read msg len %d\n", read_size);
		err = -EINVAL;
		goto deinit;
	}
	ivc_hdr = &(ivc_msg->ivc_hdr);
	err = validate_header(se_dev, ivc_hdr, is_dummy);
	if (err != 0)
		goto deinit;
	if (*is_dummy) {
		dev_err(se_dev->dev, "%s(): Wrong response sequence\n", __func__);
		goto deinit;
	}
	priv = ivc_msg->ivc_hdr.tag.priv_data;
	if (!priv) {
		dev_err(se_dev->dev, "%s no call back info\n", __func__);
		goto deinit;
	}
	priv->syncpt_id = ivc_msg->rx[0].syncpt_id;
	priv->syncpt_threshold = ivc_msg->rx[0].syncpt_threshold;
	priv->syncpt_id_valid = ivc_msg->rx[0].syncpt_id_valid;

	switch (priv->cmd) {
	case VIRTUAL_SE_AES_CRYPTO:
		priv->rx_status = ivc_msg->rx[0].status;
		req_ctx = skcipher_request_ctx(priv->req);
		if ((!priv->rx_status) && (req_ctx->encrypt == true) &&
				(is_aes_mode_valid(req_ctx->op_mode) == 1)) {
			memcpy(priv->iv, ivc_msg->rx[0].iv,
					TEGRA_VIRTUAL_SE_AES_IV_SIZE);
		}
		break;
	case VIRTUAL_SE_KEY_SLOT:
		priv->rx_status = ivc_msg->rx[0].status;
		ivc_rx = &ivc_msg->rx[0];
		priv->slot_num = ivc_rx->keyslot;
		break;
	case VIRTUAL_SE_PROCESS:
		ivc_rx = &ivc_msg->rx[0];
		priv->rx_status = ivc_rx->status;
		break;
	case VIRTUAL_CMAC_PROCESS:
		ivc_rx = &ivc_msg->rx[0];
		priv->rx_status = ivc_rx->status;
		priv->cmac.status = ivc_rx->status;
		if (!ivc_rx->status) {
			memcpy(priv->cmac.data, ivc_rx->cmac_result,
				TEGRA_VIRTUAL_SE_AES_CMAC_DIGEST_SIZE);
		}
		break;
	case VIRTUAL_SE_AES_GCM_ENC_PROCESS:
		ivc_rx = &ivc_msg->rx[0];
		priv->rx_status = ivc_rx->status;
		if (!ivc_rx->status)
			memcpy(priv->iv, ivc_rx->iv,
					TEGRA_VIRTUAL_SE_AES_GCM_IV_SIZE);

		break;
	default:
		dev_err(se_dev->dev, "Unknown command\n");
		waited = false;
		err = -EINVAL;
	}
	if (waited)
		complete(&priv->alg_complete);

deinit:
	return err;

}

static int tegra_hv_vse_safety_send_ivc(
	struct tegra_virtual_se_dev *se_dev,
	struct tegra_hv_ivc_cookie *pivck,
	void *pbuf,
	int length)
{
	u32 timeout;
	int err = 0;

	timeout = TEGRA_VIRTUAL_SE_TIMEOUT_1S;
	while (tegra_hv_ivc_channel_notified(pivck) != 0) {
		if (!timeout) {
			dev_err(se_dev->dev, "ivc reset timeout\n");
			return -EINVAL;
		}
		udelay(1);
		timeout--;
	}

	timeout = TEGRA_VIRTUAL_SE_TIMEOUT_1S;
	while (tegra_hv_ivc_can_write(pivck) == 0) {
		if (!timeout) {
			dev_err(se_dev->dev, "ivc send message timeout\n");
			return -EINVAL;
		}
		udelay(1);
		timeout--;
	}

	if ((length <= 0) ||
		length > sizeof(struct tegra_virtual_se_ivc_msg_t)) {
		dev_err(se_dev->dev,
				"Wrong write msg len %d\n", length);
		return -E2BIG;
	}

	err = tegra_hv_ivc_write(pivck, pbuf, length);
	if (err < 0) {
		dev_err(se_dev->dev, "ivc write error!!! error=%d\n", err);
		return err;
	}
	return 0;
}

static int tegra_hv_vse_safety_send_ivc_wait(
	struct tegra_virtual_se_dev *se_dev,
	struct tegra_hv_ivc_cookie *pivck,
	struct tegra_vse_priv_data *priv,
	void *pbuf, int length, uint32_t node_id)
{
	struct host1x_syncpt *sp;
	struct host1x *host1x;
	int err;
	bool is_dummy = false;
	u64 time_left;
	enum ivc_irq_state *irq_state;

	mutex_lock(&g_crypto_to_ivc_map[node_id].se_ivc_lock);

	if (!se_dev->host1x_pdev) {
		dev_err(se_dev->dev, "host1x pdev not initialized\n");
		err = -ENODATA;
		goto exit;
	}

	host1x = platform_get_drvdata(se_dev->host1x_pdev);
	if (!host1x) {
		dev_err(se_dev->dev, "No platform data for host1x!\n");
		err = -ENODATA;
		goto exit;
	}

	/* Return error if engine is in suspended state */
	if (atomic_read(&se_dev->se_suspended)) {
		dev_err(se_dev->dev, "Engine is in suspended state\n");
		err = -ENODEV;
		goto exit;
	}
	err = tegra_hv_vse_safety_send_ivc(se_dev, pivck, pbuf, length);
	if (err) {
		dev_err(se_dev->dev,
			"\n %s send ivc failed %d\n", __func__, err);
		goto exit;
	}

	mutex_lock(&(g_crypto_to_ivc_map[node_id].irq_state_lock));
	irq_state = &(se_dev->crypto_to_ivc_map[node_id].wait_interrupt);
	if (*irq_state == NO_INTERRUPT) {
		err = read_and_validate_dummy_msg(se_dev, pivck, node_id, &is_dummy);
		if (err != 0) {
			dev_err(se_dev->dev, "Failed to read and validate dummy message.\n");
			mutex_unlock(&(g_crypto_to_ivc_map[node_id].irq_state_lock));
			goto exit;
		}
		if (is_dummy) {
			err = read_and_validate_valid_msg(se_dev, pivck, node_id, &is_dummy, false);
			if (err != 0 && err != -EAGAIN) {
				dev_err(se_dev->dev, "Failed to read & validate valid message.\n");
				mutex_unlock(&(g_crypto_to_ivc_map[node_id].irq_state_lock));
				goto exit;
			}
			mutex_unlock(&(g_crypto_to_ivc_map[node_id].irq_state_lock));
			if (err == -EAGAIN) {
				err = 0;
				pr_debug("%s(): wait_interrupt = %u", __func__, *irq_state);
				time_left = wait_for_completion_timeout(&priv->alg_complete,
						TEGRA_HV_VSE_TIMEOUT);
				if (time_left == 0) {
					dev_err(se_dev->dev, "%s timeout\n", __func__);
					err = -ETIMEDOUT;
					goto exit;
				}
			}
			pr_debug("%s(): wait_interrupt = %u", __func__, *irq_state);
		} else {
			dev_err(se_dev->dev,
				"%s(): Invalid resonse sequence, expected dummy message.\n",
				__func__);
			mutex_unlock(&(g_crypto_to_ivc_map[node_id].irq_state_lock));
			goto exit;
		}
	} else {
		mutex_unlock(&(g_crypto_to_ivc_map[node_id].irq_state_lock));
		time_left = wait_for_completion_timeout(&priv->alg_complete, TEGRA_HV_VSE_TIMEOUT);
		if (time_left == 0) {
			dev_err(se_dev->dev, "%s timeout\n", __func__);
			err = -ETIMEDOUT;
			goto exit;
		}
	}

	/* If this is not last request then wait using nvhost API*/
	if (priv->syncpt_id_valid && priv->rx_status == 0) {
		sp = host1x_syncpt_get_by_id_noref(host1x, priv->syncpt_id);
		if (!sp) {
			dev_err(se_dev->dev, "No syncpt for syncpt id %d\n", priv->syncpt_id);
			err = -ENODATA;
			goto exit;
		}

		err = host1x_syncpt_wait(sp, priv->syncpt_threshold,
				SE_MAX_SCHEDULE_TIMEOUT, NULL);
		if (err) {
			dev_err(se_dev->dev, "timed out for syncpt %u threshold %u err %d\n",
						 priv->syncpt_id, priv->syncpt_threshold, err);
			err = -ETIMEDOUT;
			goto exit;
		}
	}

exit:
	mutex_unlock(&g_crypto_to_ivc_map[node_id].se_ivc_lock);
	return err;
}

static const struct tegra_vse_dma_buf *tegra_hv_vse_get_dma_buf(
	uint32_t node_id, uint32_t buf_idx, uint32_t buf_size)
{
	if (buf_idx >= MAX_SE_DMA_BUFS) {
		VSE_ERR("%s offset invalid\n", __func__);
		return NULL;
	}
	if (node_id >= MAX_NUMBER_MISC_DEVICES) {
		VSE_ERR("%s node_id invalid\n", __func__);
		return NULL;
	}
	if (buf_size > g_node_dma[node_id].se_dma_buf[buf_idx].buf_len) {
		VSE_ERR("%s requested buffer size is too large\n", __func__);
		return NULL;
	}
	return &g_node_dma[node_id].se_dma_buf[buf_idx];
}

int tegra_hv_vse_allocate_keyslot(struct tegra_vse_key_slot_ctx *key_slot,
	uint32_t node_id)
{
	struct tegra_virtual_se_dev *se_dev = NULL;
	struct tegra_virtual_se_ivc_msg_t *ivc_req_msg;
	struct tegra_virtual_se_ivc_hdr_t *ivc_hdr;
	struct tegra_virtual_se_ivc_tx_msg_t *ivc_tx;
	struct tegra_hv_ivc_cookie *pivck;
	struct tegra_vse_priv_data *priv = NULL;
	int err = 0;

	if (node_id >= MAX_NUMBER_MISC_DEVICES) {
		VSE_ERR("%s: node_id is invalid\n", __func__);
		return -EINVAL;
	}

	se_dev = g_crypto_to_ivc_map[node_id].se_dev;
	if (!se_dev) {
		VSE_ERR("%s: se_dev is NULL\n", __func__);
		return -EINVAL;
	}

	if (!se_dev->chipdata->allocate_key_slot_supported) {
		dev_err(se_dev->dev, "%s: Allocate Keyslot is not supported\n", __func__);
		return -EINVAL;
	}

	if (!key_slot) {
		dev_err(se_dev->dev, "%s: key slot params is NULL\n", __func__);
		return -EINVAL;
	}

	if (atomic_read(&se_dev->se_suspended)) {
		dev_err(se_dev->dev, "Engine is in suspended state\n");
		return -ENODEV;
	}

	pivck = g_crypto_to_ivc_map[node_id].ivck;
	if (!pivck) {
		dev_err(se_dev->dev, "No IVC channel\n");
		return -ENODEV;
	}

	ivc_req_msg = g_crypto_to_ivc_map[node_id].ivc_msg;
	priv = g_crypto_to_ivc_map[node_id].priv;

	memset(ivc_req_msg, 0, sizeof(struct tegra_virtual_se_ivc_msg_t));
	ivc_tx = &ivc_req_msg->tx[0];
	ivc_hdr = &ivc_req_msg->ivc_hdr;
	ivc_hdr->num_reqs = 1;
	ivc_hdr->header_magic[0] = 'N';
	ivc_hdr->header_magic[1] = 'V';
	ivc_hdr->header_magic[2] = 'D';
	ivc_hdr->header_magic[3] = 'A';
	ivc_hdr->engine = g_crypto_to_ivc_map[node_id].engine_id;
	ivc_hdr->tag.priv_data = priv;

	priv->cmd = VIRTUAL_SE_KEY_SLOT;
	priv->se_dev = se_dev;

	ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_ALLOC_KEY;
	memcpy(ivc_tx->keys.keyslot, key_slot->key_id, KEYSLOT_SIZE_BYTES);

	ivc_tx->keys.key_usage = key_slot->key_usage;
	ivc_tx->keys.key_grp_id = key_slot->key_grp_id;
	ivc_tx->keys.token_id = key_slot->token_id;
	g_crypto_to_ivc_map[node_id].vse_thread_start = true;
	init_completion(&priv->alg_complete);

	err = tegra_hv_vse_safety_send_ivc_wait(se_dev, pivck, priv, ivc_req_msg,
		sizeof(struct tegra_virtual_se_ivc_msg_t), node_id);
	if (err) {
		dev_err(se_dev->dev, "Failed to send IVC message: %d\n", err);
		goto exit;
	}

	if (priv->rx_status) {
		dev_err(se_dev->dev, "Key slot allocation failed with error: %d\n",
		priv->rx_status);
		err = -EINVAL;
		goto exit;
	}

	key_slot->key_instance_idx = priv->slot_num;

exit:
	return err;
}
EXPORT_SYMBOL(tegra_hv_vse_allocate_keyslot);

int tegra_hv_vse_close_keyslot(uint32_t node_id, uint32_t key_grp_id)
{
	struct tegra_virtual_se_dev *se_dev = NULL;
	struct tegra_virtual_se_ivc_msg_t *ivc_req_msg;
	struct tegra_virtual_se_ivc_hdr_t *ivc_hdr;
	struct tegra_virtual_se_ivc_tx_msg_t *ivc_tx;
	struct tegra_hv_ivc_cookie *pivck;
	struct tegra_vse_priv_data *priv = NULL;
	int err = 0;

	if (node_id >= MAX_NUMBER_MISC_DEVICES) {
		VSE_ERR("%s: node_id is invalid\n", __func__);
		return -EINVAL;
	}

	se_dev = g_crypto_to_ivc_map[node_id].se_dev;
	if (!se_dev) {
		VSE_ERR("%s: se_dev is NULL\n", __func__);
		return -EINVAL;
	}

	if (!se_dev->chipdata->allocate_key_slot_supported) {
		dev_err(se_dev->dev, "%s: allocate_key_slot_supported is not supported\n", __func__);
		return -EINVAL;
	}

	if (atomic_read(&se_dev->se_suspended)) {
		dev_err(se_dev->dev, "Engine is in suspended state\n");
		return -ENODEV;
	}

	pivck = g_crypto_to_ivc_map[node_id].ivck;
	if (!pivck) {
		dev_err(se_dev->dev, "No IVC channel\n");
		return -ENODEV;
	}

	ivc_req_msg = g_crypto_to_ivc_map[node_id].ivc_msg;
	priv = g_crypto_to_ivc_map[node_id].priv;

	memset(ivc_req_msg, 0, sizeof(struct tegra_virtual_se_ivc_msg_t));
	ivc_tx = &ivc_req_msg->tx[0];
	ivc_hdr = &ivc_req_msg->ivc_hdr;
	ivc_hdr->num_reqs = 1;
	ivc_hdr->header_magic[0] = 'N';
	ivc_hdr->header_magic[1] = 'V';
	ivc_hdr->header_magic[2] = 'D';
	ivc_hdr->header_magic[3] = 'A';
	ivc_hdr->tag.priv_data = priv;

	ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_RELEASE_KEY_USING_GRIP;
	ivc_tx->keys.key_grp_id = key_grp_id;

	priv->cmd = VIRTUAL_SE_KEY_SLOT;
	priv->se_dev = se_dev;

	g_crypto_to_ivc_map[node_id].vse_thread_start = true;
	init_completion(&priv->alg_complete);

	err = tegra_hv_vse_safety_send_ivc_wait(se_dev, pivck, priv, ivc_req_msg,
		sizeof(struct tegra_virtual_se_ivc_msg_t), node_id);
	if (err) {
		dev_err(se_dev->dev, "Failed to send IVC message: %d\n", err);
		goto exit;
	}

	if (priv->rx_status) {
		dev_err(se_dev->dev, "Key slot release failed with error: %d\n",
		priv->rx_status);
		err = -EINVAL;
	}

exit:
	return err;
}
EXPORT_SYMBOL(tegra_hv_vse_close_keyslot);

int tegra_hv_vse_release_keyslot(struct tegra_vse_key_slot_ctx *key_slot, uint32_t node_id)
{
	struct tegra_virtual_se_dev *se_dev = NULL;
	struct tegra_virtual_se_ivc_msg_t *ivc_req_msg;
	struct tegra_virtual_se_ivc_hdr_t *ivc_hdr;
	struct tegra_virtual_se_ivc_tx_msg_t *ivc_tx;
	struct tegra_hv_ivc_cookie *pivck;
	struct tegra_vse_priv_data *priv = NULL;
	int err = 0;

	if (node_id >= MAX_NUMBER_MISC_DEVICES) {
		VSE_ERR("%s: node_id is invalid\n", __func__);
		return -EINVAL;
	}

	se_dev = g_crypto_to_ivc_map[node_id].se_dev;
	if (!se_dev) {
		VSE_ERR("%s: se_dev is NULL\n", __func__);
		return -EINVAL;
	}

	if (!se_dev->chipdata->allocate_key_slot_supported) {
		dev_err(se_dev->dev, "%s: allocate_key_slot_supported is not supported\n", __func__);
		return -EINVAL;
	}

	if (!key_slot) {
		dev_err(se_dev->dev, "%s: key slot params is NULL\n", __func__);
		return -EINVAL;
	}

	/* Return error if engine is in suspended state */
	if (atomic_read(&se_dev->se_suspended)) {
		dev_err(se_dev->dev, "Engine is in suspended state\n");
		return -ENODEV;
	}

	pivck = g_crypto_to_ivc_map[node_id].ivck;
	if (!pivck) {
		dev_err(se_dev->dev, "No IVC channel\n");
		return -ENODEV;
	}

	priv = g_crypto_to_ivc_map[node_id].priv;
	ivc_req_msg = g_crypto_to_ivc_map[node_id].ivc_msg;

	memset(ivc_req_msg, 0, sizeof(struct tegra_virtual_se_ivc_msg_t));
	ivc_tx = &ivc_req_msg->tx[0];
	ivc_hdr = &ivc_req_msg->ivc_hdr;
	ivc_hdr->num_reqs = 1;
	ivc_hdr->header_magic[0] = 'N';
	ivc_hdr->header_magic[1] = 'V';
	ivc_hdr->header_magic[2] = 'D';
	ivc_hdr->header_magic[3] = 'A';
	ivc_hdr->engine = g_crypto_to_ivc_map[node_id].engine_id;
	ivc_hdr->tag.priv_data = priv;

	ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_RELEASE_KEY;
	memcpy(ivc_tx->keys.keyslot, key_slot->key_id, KEYSLOT_SIZE_BYTES);
	ivc_tx->keys.token_id = key_slot->token_id;
	if (key_slot->key_instance_idx > UINT8_MAX) {
		dev_err(se_dev->dev, "Key instance index is greater than UINT8_MAX\n");
		err = -EINVAL;
		goto exit;
	}
	ivc_tx->keys.key_instance = key_slot->key_instance_idx;

	priv->cmd = VIRTUAL_SE_KEY_SLOT;
	priv->se_dev = se_dev;

	g_crypto_to_ivc_map[node_id].vse_thread_start = true;
	init_completion(&priv->alg_complete);

	err = tegra_hv_vse_safety_send_ivc_wait(se_dev, pivck, priv, ivc_req_msg,
		sizeof(struct tegra_virtual_se_ivc_msg_t), node_id);
	if (err) {
		dev_err(se_dev->dev, "Failed to send IVC message: %d\n", err);
		goto exit;
	}

	if (priv->rx_status) {
		dev_err(se_dev->dev, "Key slot release failed with error: %d\n",
		priv->rx_status);
		err = -EINVAL;
	}

exit:
	return err;
}
EXPORT_SYMBOL(tegra_hv_vse_release_keyslot);

static int tegra_vse_validate_hmac_sha_params(struct tegra_virtual_se_hmac_sha_context *hmac_ctx,
		bool is_last)
{
	if ((hmac_ctx->user_src_buf_size == 0) ||
		(hmac_ctx->user_src_buf_size > TEGRA_VIRTUAL_SE_MAX_SUPPORTED_BUFLEN)) {
		VSE_ERR("%s: input buffer size is invalid\n", __func__);
		return -EINVAL;
	}

	if (hmac_ctx->node_id >= MAX_NUMBER_MISC_DEVICES) {
		VSE_ERR("%s: Node id is not valid\n", __func__);
		return -EINVAL;
	}

	if (hmac_ctx->digest_size == 0) {
		VSE_ERR("%s: Digest size is not valid\n", __func__);
		return -EINVAL;
	}

	if (!hmac_ctx->is_key_slot_allocated) {
		VSE_ERR("%s key is not allocated\n", __func__);
		return -EINVAL;
	}

	if (!is_last) {
		if (hmac_ctx->mode == VIRTUAL_SE_OP_MODE_SHA256) {
			if (hmac_ctx->user_src_buf_size %
				TEGRA_VIRTUAL_SE_SHA_HASH_BLOCK_SIZE_512BIT != 0) {
				VSE_ERR("%s: non-last buffer size is invalid\n", __func__);
				return -EINVAL;
			}
		}
	}

	if (hmac_ctx->user_src_buf == NULL) {
		VSE_ERR("%s: src buf is NULL\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int tegra_vse_validate_aes_param(struct tegra_virtual_se_aes_context *aes_ctx)
{
	if (aes_ctx->node_id >= MAX_NUMBER_MISC_DEVICES) {
		VSE_ERR("%s: Node id is not valid\n", __func__);
		return -EINVAL;
	}

	if (aes_ctx->user_src_buf == NULL) {
		VSE_ERR("%s: src buf is NULL\n", __func__);
		return -EINVAL;
	}

	if (!aes_ctx->is_key_slot_allocated) {
		VSE_ERR("AES Key slot not allocated\n");
		return -EINVAL;
	}

	if (aes_ctx->user_dst_buf == NULL) {
		VSE_ERR("%s: dst buf is NULL\n", __func__);
		return -EINVAL;
	}

	if (aes_ctx->user_src_buf_size == 0 ||
		(aes_ctx->user_src_buf_size > TEGRA_VIRTUAL_SE_MAX_SUPPORTED_BUFLEN)) {
		VSE_ERR("%s: src buffer size is invalid\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int tegra_vse_validate_cmac_params(struct tegra_virtual_se_aes_cmac_context *cmac_ctx,
	bool is_last)
{
	if (cmac_ctx->node_id >= MAX_NUMBER_MISC_DEVICES) {
		VSE_ERR("%s: Node id is not valid\n", __func__);
		return -EINVAL;
	}

	if (cmac_ctx->user_src_buf == NULL) {
		VSE_ERR("%s: src buf is NULL\n", __func__);
		return -EINVAL;
	}

	if (!cmac_ctx->req_context_initialized) {
		VSE_ERR("%s Request ctx not initialized\n", __func__);
		return -EINVAL;
	}

	if (cmac_ctx->user_src_buf_size <= 0 ||
		(cmac_ctx->user_src_buf_size > TEGRA_VIRTUAL_SE_MAX_SUPPORTED_BUFLEN)) {
		VSE_ERR("%s: src buffer size is invalid\n", __func__);
		return -EINVAL;
	}

	if (!cmac_ctx->is_key_slot_allocated) {
		VSE_ERR("%s key is not allocated\n", __func__);
		return -EINVAL;
	}

	if (cmac_ctx->user_mac_buf == NULL) {
		VSE_ERR("%s: mac buf is NULL\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int tegra_vse_validate_aes_rng_param(struct tegra_virtual_se_rng_context *rng_ctx)
{
	if (rng_ctx == NULL) {
		VSE_ERR("%s: rng_ctx is NULL\n", __func__);
		return -EINVAL;
	}

	if (rng_ctx->node_id >= MAX_NUMBER_MISC_DEVICES) {
		VSE_ERR("%s: Node id is not valid\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int tegra_hv_vse_safety_sha_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm;
	struct tegra_virtual_se_req_context *req_ctx;
	struct tegra_virtual_se_sha_context *sha_ctx;
	struct tegra_virtual_se_dev *se_dev;
	uint32_t engine_id;

	if (!req) {
		VSE_ERR("%s: SHA request invalid\n", __func__);
		return -EINVAL;
	}

	req_ctx = ahash_request_ctx(req);
	if (!req_ctx) {
		VSE_ERR("%s: SHA req_ctx not valid\n", __func__);
		return -EINVAL;
	}

	tfm = crypto_ahash_reqtfm(req);
	if (!tfm) {
		VSE_ERR("%s: SHA transform not valid\n", __func__);
		return -EINVAL;
	}

	sha_ctx = crypto_ahash_ctx(tfm);
	engine_id = g_crypto_to_ivc_map[sha_ctx->node_id].engine_id;
	se_dev = g_crypto_to_ivc_map[sha_ctx->node_id].se_dev;

	/* Return error if engine is in suspended state */
	if (atomic_read(&se_dev->se_suspended))
		return -ENODEV;

	if (strcmp(crypto_ahash_alg_name(tfm), "sha256-vse") == 0) {
		sha_ctx->mode = VIRTUAL_SE_OP_MODE_SHA256;
		sha_ctx->blk_size = TEGRA_VIRTUAL_SE_SHA_HASH_BLOCK_SIZE_512BIT;
		sha_ctx->intermediate_digest_size = SHA256_DIGEST_SIZE;
		sha_ctx->digest_size = crypto_ahash_digestsize(tfm);
	} else if (strcmp(crypto_ahash_alg_name(tfm), "sha384-vse") == 0) {
		sha_ctx->mode = VIRTUAL_SE_OP_MODE_SHA384;
		sha_ctx->blk_size =
			TEGRA_VIRTUAL_SE_SHA_HASH_BLOCK_SIZE_1024BIT;
		/*
		 * The intermediate digest size of SHA384 is same as SHA512
		 */
		sha_ctx->intermediate_digest_size = SHA512_DIGEST_SIZE;
		sha_ctx->digest_size = crypto_ahash_digestsize(tfm);
	} else if (strcmp(crypto_ahash_alg_name(tfm), "sha512-vse") == 0) {
		sha_ctx->mode = VIRTUAL_SE_OP_MODE_SHA512;
		sha_ctx->blk_size = TEGRA_VIRTUAL_SE_SHA_HASH_BLOCK_SIZE_1024BIT;
		sha_ctx->intermediate_digest_size = SHA512_DIGEST_SIZE;
		sha_ctx->digest_size = crypto_ahash_digestsize(tfm);
	} else if (strcmp(crypto_ahash_alg_name(tfm), "sha3-256-vse") == 0) {
		sha_ctx->mode = VIRTUAL_SE_OP_MODE_SHA3_256;
		sha_ctx->blk_size = TEGRA_VIRTUAL_SE_SHA_HASH_BLOCK_SIZE_1088BIT;
		sha_ctx->intermediate_digest_size = SHA3_STATE_SIZE;
		sha_ctx->digest_size = crypto_ahash_digestsize(tfm);
	} else if (strcmp(crypto_ahash_alg_name(tfm), "sha3-384-vse") == 0) {
		sha_ctx->mode = VIRTUAL_SE_OP_MODE_SHA3_384;
		sha_ctx->blk_size = TEGRA_VIRTUAL_SE_SHA_HASH_BLOCK_SIZE_832BIT;
		sha_ctx->intermediate_digest_size = SHA3_STATE_SIZE;
		sha_ctx->digest_size = crypto_ahash_digestsize(tfm);
	} else if (strcmp(crypto_ahash_alg_name(tfm), "sha3-512-vse") == 0) {
		sha_ctx->mode = VIRTUAL_SE_OP_MODE_SHA3_512;
		sha_ctx->blk_size = TEGRA_VIRTUAL_SE_SHA_HASH_BLOCK_SIZE_576BIT;
		sha_ctx->intermediate_digest_size = SHA3_STATE_SIZE;
		sha_ctx->digest_size = crypto_ahash_digestsize(tfm);
	} else if (strcmp(crypto_ahash_alg_name(tfm), "shake128-vse") == 0) {
		sha_ctx->mode = VIRTUAL_SE_OP_MODE_SHAKE128;
		sha_ctx->blk_size = TEGRA_VIRTUAL_SE_SHA_HASH_BLOCK_SIZE_1344BIT;
		sha_ctx->intermediate_digest_size = SHA3_STATE_SIZE;
	} else if (strcmp(crypto_ahash_alg_name(tfm), "shake256-vse") == 0) {
		sha_ctx->mode = VIRTUAL_SE_OP_MODE_SHAKE256;
		sha_ctx->blk_size = TEGRA_VIRTUAL_SE_SHA_HASH_BLOCK_SIZE_1088BIT;
		sha_ctx->intermediate_digest_size = SHA3_STATE_SIZE;
	} else if ((strcmp(crypto_ahash_alg_name(tfm), "sm3-vse") == 0) &&
		(se_dev->chipdata->sm_supported)) {
		sha_ctx->mode = VIRTUAL_SE_OP_MODE_SM3;
		sha_ctx->blk_size = SM3_BLOCK_SIZE;
		sha_ctx->intermediate_digest_size = SM3_DIGEST_SIZE;
		sha_ctx->digest_size = crypto_ahash_digestsize(tfm);
	} else {
		dev_err(se_dev->dev, "Invalid SHA Mode\n");
		return -EINVAL;
	}

	req_ctx->req_context_initialized = true;

	return 0;
}

static int tegra_vse_validate_sha_params(struct tegra_virtual_se_sha_context *sha_ctx,
	bool is_last)
{
	int ret = 0;
	bool is_zero_copy;

	if (sha_ctx->node_id >= MAX_NUMBER_MISC_DEVICES) {
		VSE_ERR("%s: Node id is not valid\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	is_zero_copy = g_crypto_to_ivc_map[sha_ctx->node_id].is_zero_copy_node;
	if (is_last == 0 && is_zero_copy) {
		VSE_ERR("%s(): Multipart SHA is not supported for zero-copy\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	if (!is_zero_copy) {
		if (sha_ctx->user_src_buf_size > 0 && sha_ctx->user_src_buf == NULL) {
			VSE_ERR("%s: src buf is NULL\n", __func__);
			ret = -EINVAL;
			goto exit;
		}
	}

	if (sha_ctx->intermediate_digest == NULL) {
		VSE_ERR("%s: intermediate_digest is NULL\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	if (sha_ctx->digest_size == 0) {
		VSE_ERR("%s: Digest size is not valid\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	if (sha_ctx->user_src_buf_size > TEGRA_VIRTUAL_SE_MAX_SUPPORTED_BUFLEN) {
		VSE_ERR("%s: input buffer size is invalid\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	if (is_last) {
		if (sha_ctx->user_digest_buffer == NULL) {
			VSE_ERR("%s: user digest buffer is NULL\n", __func__);
			ret = -EINVAL;
			goto exit;
		}
	}

	if (sha_ctx->blk_size == 0U) {
		VSE_ERR("SHA blk_size is invalid\n");
		ret = -EINVAL;
		goto exit;
	}

	if ((!is_last) && (sha_ctx->user_src_buf_size % sha_ctx->blk_size != 0)) {
		VSE_ERR("%s: non-last buffer size is invalid\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

exit:
	return ret;
}

static int tegra_hv_vse_safety_sha_op(struct tegra_virtual_se_sha_context *sha_ctx, bool is_last)
{
	struct tegra_virtual_se_dev *se_dev;
	struct tegra_virtual_se_ivc_hdr_t *ivc_hdr;
	struct tegra_virtual_se_ivc_tx_msg_t *ivc_tx;
	struct tegra_virtual_se_ivc_msg_t *ivc_req_msg =
			g_crypto_to_ivc_map[sha_ctx->node_id].ivc_msg;
	union tegra_virtual_se_sha_args *psha;
	struct tegra_hv_ivc_cookie *pivck = g_crypto_to_ivc_map[sha_ctx->node_id].ivck;
	struct tegra_vse_priv_data *priv = g_crypto_to_ivc_map[sha_ctx->node_id].priv;
	u64 msg_len = 0, temp_len = 0;
	uint32_t engine_id;
	uint64_t ret = 0;
	int err = 0;
	const struct tegra_vse_dma_buf *plaintext, *hash_result;
	bool is_zero_copy;

	engine_id = g_crypto_to_ivc_map[sha_ctx->node_id].engine_id;
	se_dev = g_crypto_to_ivc_map[sha_ctx->node_id].se_dev;
	is_zero_copy = g_crypto_to_ivc_map[sha_ctx->node_id].is_zero_copy_node;

	if (sha_ctx->mode == VIRTUAL_SE_OP_MODE_SHAKE128 ||
			sha_ctx->mode == VIRTUAL_SE_OP_MODE_SHAKE256) {
		if (sha_ctx->digest_size == 0) {
			dev_info(se_dev->dev, "digest size is 0\n");
			return 0;
		}
	}

	g_crypto_to_ivc_map[sha_ctx->node_id].vse_thread_start = true;

	msg_len = sha_ctx->user_src_buf_size;
	if (!is_zero_copy) {
		plaintext = tegra_hv_vse_get_dma_buf(sha_ctx->node_id, SHA_SRC_BUF_IDX,
						sha_ctx->user_src_buf_size);
		if (!plaintext) {
			dev_err(se_dev->dev, "%s src_buf is NULL\n", __func__);
			return -ENOMEM;
		}

		if (msg_len > 0) {
			err = copy_from_user(plaintext->buf_ptr, sha_ctx->user_src_buf, msg_len);
			if (err) {
				dev_err(se_dev->dev, "%s(): Failed to copy plaintext: %d\n",
				__func__, err);
				goto exit;
			}
		}
	} else {
		if (g_node_dma[sha_ctx->node_id].mapped_membuf_count == 0U) {
			dev_err(se_dev->dev, "%s no mapped membuf found\n", __func__);
			return -ENOMEM;
		}
	}

	hash_result = tegra_hv_vse_get_dma_buf(sha_ctx->node_id, SHA_HASH_BUF_IDX,
			sha_ctx->digest_size);
	if (!hash_result) {
		dev_err(se_dev->dev, "%s hash_result is NULL\n", __func__);
		return -ENOMEM;
	}

	memset(ivc_req_msg, 0, sizeof(struct tegra_virtual_se_ivc_msg_t));
	ivc_tx = &ivc_req_msg->tx[0];
	ivc_hdr = &ivc_req_msg->ivc_hdr;
	ivc_hdr->num_reqs = 1;
	ivc_hdr->header_magic[0] = 'N';
	ivc_hdr->header_magic[1] = 'V';
	ivc_hdr->header_magic[2] = 'D';
	ivc_hdr->header_magic[3] = 'A';
	ivc_hdr->engine = VIRTUAL_SE_SHA;
	ivc_hdr->tag.priv_data = priv;
	ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_SHA_HASH;

	psha = &(ivc_tx->sha);
	psha->op_hash.mode = sha_ctx->mode;
	psha->op_hash.msg_total_length[2] = 0;
	psha->op_hash.msg_total_length[3] = 0;
	psha->op_hash.msg_left_length[2] = 0;
	psha->op_hash.msg_left_length[3] = 0;
	psha->op_hash.hash_length = sha_ctx->digest_size;
	psha->op_hash.dst = hash_result->buf_iova;

	if (!sha_ctx->is_first)
		memcpy(psha->op_hash.hash, sha_ctx->intermediate_digest,
			sha_ctx->intermediate_digest_size);

	if (is_last == true &&
			(sha_ctx->mode == VIRTUAL_SE_OP_MODE_SHAKE128 ||
			 sha_ctx->mode == VIRTUAL_SE_OP_MODE_SHAKE256)) {
		((uint8_t *)plaintext->buf_ptr)[msg_len] = 0xff;
		msg_len++;
		sha_ctx->total_count++;
	}

	temp_len = msg_len;

	if (is_last) {
		/* Set msg left length equal to input buffer size */
		psha->op_hash.msg_left_length[0] = msg_len & 0xFFFFFFFF;
		psha->op_hash.msg_left_length[1] = msg_len >> 32;

		/* Set msg total length equal to sum of all input buffer size */
		psha->op_hash.msg_total_length[0] = sha_ctx->total_count & 0xFFFFFFFF;
		psha->op_hash.msg_total_length[1] = sha_ctx->total_count >> 32;
	} else {
		/* Set msg left length greater than input buffer size */
		temp_len += 8;
		psha->op_hash.msg_left_length[0] = temp_len & 0xFFFFFFFF;
		psha->op_hash.msg_left_length[1] = temp_len >> 32;

		/* Set msg total length greater than msg left length for non-first request */
		if (!sha_ctx->is_first)
			temp_len += 8;

		psha->op_hash.msg_total_length[0] = temp_len & 0xFFFFFFFF;
		psha->op_hash.msg_total_length[1] = temp_len >> 32;
	}

	if (!is_zero_copy)
		psha->op_hash.src_addr = plaintext->buf_iova;
	else
		psha->op_hash.src_addr = sha_ctx->user_src_iova;

	psha->op_hash.src_buf_size = msg_len;

	priv->cmd = VIRTUAL_SE_PROCESS;
	priv->se_dev = se_dev;
	init_completion(&priv->alg_complete);

	err = tegra_hv_vse_safety_send_ivc_wait(se_dev, pivck, priv, ivc_req_msg,
			sizeof(struct tegra_virtual_se_ivc_msg_t), sha_ctx->node_id);
	if (err) {
		dev_err(se_dev->dev, "failed to send data over ivc err %d\n", err);
		goto exit;
	}

	if (priv->rx_status != 0) {
		err = status_to_errno(priv->rx_status);
		dev_err(se_dev->dev, "%s: SE server returned error %u\n",
				__func__, priv->rx_status);
		goto exit;
	}

	if (is_last && sha_ctx->digest_size > 0) {
		ret = copy_to_user(sha_ctx->user_digest_buffer, hash_result->buf_ptr,
				sha_ctx->digest_size);
		if (ret) {
			dev_err(se_dev->dev, "%s(): Failed to copy dst_buf\n", __func__);
			err = -EFAULT;
			goto exit;
		}
	}
	else
		memcpy(sha_ctx->intermediate_digest, hash_result->buf_ptr,
				sha_ctx->intermediate_digest_size);

exit:
	return err;
}

static int tegra_hv_vse_safety_sha_update(struct ahash_request *req)
{
	struct tegra_virtual_se_req_context *req_ctx;
	struct tegra_virtual_se_sha_context *sha_ctx;
	struct tegra_virtual_se_dev *se_dev;
	uint32_t engine_id;
	int ret = 0;

	if (!req) {
		VSE_ERR("%s SHA request not valid\n", __func__);
		return -EINVAL;
	}

	req_ctx = ahash_request_ctx(req);
	if (!req_ctx) {
		VSE_ERR("%s SHA req not valid\n", __func__);
		return -EINVAL;
	}

	if (!req_ctx->req_context_initialized) {
		VSE_ERR("%s Request ctx not initialized\n", __func__);
		return -EINVAL;
	}

	sha_ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	if (!sha_ctx) {
		VSE_ERR("%s SHA req_ctx not valid\n", __func__);
		return -EINVAL;
	}

	ret = tegra_vse_validate_sha_params(sha_ctx, false);
	if (ret) {
		VSE_ERR("%s: invalid SHA params\n", __func__);
		return ret;
	}

	engine_id = g_crypto_to_ivc_map[sha_ctx->node_id].engine_id;
	se_dev = g_crypto_to_ivc_map[sha_ctx->node_id].se_dev;

	/* Return error if engine is in suspended state */
	if (atomic_read(&se_dev->se_suspended)) {
		VSE_ERR("%s: Engine is in suspended state\n", __func__);
		return -ENODEV;
	}

	ret = tegra_hv_vse_safety_sha_op(sha_ctx, false);
	if (ret)
		dev_err(se_dev->dev, "tegra_se_sha_update failed - %d\n", ret);

	return ret;
}

static int tegra_hv_vse_safety_sha_finup(struct ahash_request *req)
{
	struct tegra_virtual_se_req_context *req_ctx;
	struct tegra_virtual_se_sha_context *sha_ctx = NULL;
	struct tegra_virtual_se_dev *se_dev;
	uint32_t engine_id;
	int ret = 0;

	if (!req) {
		VSE_ERR("%s SHA request not valid\n", __func__);
		return -EINVAL;
	}

	sha_ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	if (!sha_ctx) {
		VSE_ERR("%s SHA req_ctx not valid\n", __func__);
		return -EINVAL;
	}

	req_ctx = ahash_request_ctx(req);
	if (!req_ctx) {
		VSE_ERR("%s SHA req not valid\n", __func__);
		return -EINVAL;
	}

	if (!req_ctx->req_context_initialized) {
		VSE_ERR("%s Request ctx not initialized\n", __func__);
		return -EINVAL;
	}

	ret = tegra_vse_validate_sha_params(sha_ctx, true);
	if (ret) {
		VSE_ERR("%s: invalid SHA params\n", __func__);
		return ret;
	}

	engine_id = g_crypto_to_ivc_map[sha_ctx->node_id].engine_id;
	se_dev = g_crypto_to_ivc_map[sha_ctx->node_id].se_dev;

	/* Return error if engine is in suspended state */
	if (atomic_read(&se_dev->se_suspended))
		return -ENODEV;

	ret = tegra_hv_vse_safety_sha_op(sha_ctx, true);
	if (ret)
		dev_err(se_dev->dev, "tegra_se_sha_finup failed - %d\n", ret);

	req_ctx->req_context_initialized = false;

	return ret;
}

static int tegra_hv_vse_safety_sha_final(struct ahash_request *req)
{
	// Unsupported
	VSE_ERR("%s: This callback is not supported\n", __func__);
	return -EINVAL;
}

static int tegra_hv_vse_safety_sha_digest(struct ahash_request *req)
{
	// Unsupported
	VSE_ERR("%s: This callback is not supported\n", __func__);
	return -EINVAL;
}

static int tegra_hv_vse_safety_hmac_sha_setkey(struct crypto_ahash *tfm, const u8 *key,
		unsigned int keylen)
{
	struct tegra_virtual_se_hmac_sha_context *hmac_ctx;
	struct tegra_virtual_se_dev *se_dev;
	int err = 0;
	s8 label[TEGRA_VIRTUAL_SE_AES_MAX_KEY_SIZE];
	bool is_keyslot_label;

	if (!tfm) {
		VSE_ERR("HMAC SHA transform not valid\n");
		return -EINVAL;
	}

	hmac_ctx = crypto_tfm_ctx(crypto_ahash_tfm(tfm));
	if (!hmac_ctx) {
		VSE_ERR("%s: HMAC SHA ctx not valid\n", __func__);
		return -EINVAL;
	}

	if (hmac_ctx->node_id >= MAX_NUMBER_MISC_DEVICES) {
		VSE_ERR("%s: Node id is not valid\n", __func__);
		return -EINVAL;
	}

	se_dev = g_crypto_to_ivc_map[hmac_ctx->node_id].se_dev;

	/* format: 'NVSEAES 1234567\0' */
	is_keyslot_label = sscanf(key, "%s", label) == 1 &&
		!strcmp(label, TEGRA_VIRTUAL_SE_AES_KEYSLOT_LABEL);

	if (is_keyslot_label) {
		memcpy(hmac_ctx->key_slot, key + KEYSLOT_OFFSET_BYTES, KEYSLOT_SIZE_BYTES);

		hmac_ctx->is_key_slot_allocated = true;
	} else {
		dev_err(se_dev->dev, "%s: Invalid keyslot label %s\n", __func__, key);
		return -EINVAL;
	}

	return err;
}

static int tegra_hv_vse_safety_hmac_sha_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm;
	struct tegra_virtual_se_req_context *req_ctx;
	struct tegra_virtual_se_hmac_sha_context *hmac_ctx;
	struct tegra_virtual_se_dev *se_dev;

	if (!req) {
		VSE_ERR("%s HMAC SHA request not valid\n", __func__);
		return -EINVAL;
	}

	req_ctx = ahash_request_ctx(req);
	if (!req_ctx) {
		VSE_ERR("%s HMAC SHA req_ctx not valid\n", __func__);
		return -EINVAL;
	}

	tfm = crypto_ahash_reqtfm(req);
	if (!tfm) {
		VSE_ERR("%s HMAC SHA transform not valid\n", __func__);
		return -EINVAL;
	}

	hmac_ctx = crypto_ahash_ctx(tfm);
	hmac_ctx->digest_size = crypto_ahash_digestsize(tfm);
	se_dev = g_crypto_to_ivc_map[hmac_ctx->node_id].se_dev;

	/* Return error if engine is in suspended state */
	if (atomic_read(&se_dev->se_suspended))
		return -ENODEV;

	if (!hmac_ctx->is_key_slot_allocated) {
		dev_err(se_dev->dev, "%s key is not allocated\n", __func__);
		return -EINVAL;
	}

	if (strcmp(crypto_ahash_alg_name(tfm), "hmac-sha256-vse") == 0) {
		hmac_ctx->mode = VIRTUAL_SE_OP_MODE_SHA256;
		hmac_ctx->blk_size = TEGRA_VIRTUAL_SE_SHA_HASH_BLOCK_SIZE_512BIT;
	} else {
		dev_err(se_dev->dev, "Invalid HMAC-SHA Alg\n");
		return -EINVAL;
	}

	req_ctx->req_context_initialized = true;

	return 0;
}

static int tegra_hv_vse_safety_hmac_sha_sv_op(struct ahash_request *req,
		struct tegra_virtual_se_hmac_sha_context *hmac_ctx, bool is_last)
{
	struct tegra_virtual_se_dev *se_dev =
			g_crypto_to_ivc_map[hmac_ctx->node_id].se_dev;
	struct tegra_virtual_se_ivc_hdr_t *ivc_hdr;
	struct tegra_virtual_se_ivc_tx_msg_t *ivc_tx;
	struct tegra_virtual_se_ivc_msg_t *ivc_req_msg =
			g_crypto_to_ivc_map[hmac_ctx->node_id].ivc_msg;
	struct tegra_virtual_se_hmac_sha_args *phmac;
	struct tegra_hv_ivc_cookie *pivck = g_crypto_to_ivc_map[hmac_ctx->node_id].ivck;
	int err = 0;
	uint64_t ret = 0;
	struct tegra_vse_priv_data *priv = g_crypto_to_ivc_map[hmac_ctx->node_id].priv;
	const struct tegra_vse_dma_buf *src, *hash, *match;

	u32 cmd = 0;
	u32 matchcode = SE_HW_VALUE_MATCH_CODE;
	u32 mismatch_code = SE_HW_VALUE_MISMATCH_CODE;

	u32 blocks_to_process, last_block_bytes = 0;
	u64 msg_len = 0, temp_len = 0;

	src = tegra_hv_vse_get_dma_buf(hmac_ctx->node_id, SHA_SRC_BUF_IDX,
			hmac_ctx->user_src_buf_size);
	if (!src) {
		VSE_ERR("%s src buf is NULL\n", __func__);
		return -ENOMEM;
	}

	if (hmac_ctx->request_type == TEGRA_HV_VSE_HMAC_SHA_SIGN) {
		hash = tegra_hv_vse_get_dma_buf(hmac_ctx->node_id,
			SHA_HASH_BUF_IDX, hmac_ctx->digest_size);
		if (!hash) {
			VSE_ERR("%s hash buf is NULL\n", __func__);
			return -ENOMEM;
		}

		memset(hash->buf_ptr, 0, TEGRA_VIRTUAL_SE_SHA_MAX_HMAC_SHA_LENGTH);
		cmd = TEGRA_VIRTUAL_SE_CMD_HMAC_SIGN;
	} else {
		cmd = TEGRA_VIRTUAL_SE_CMD_HMAC_VERIFY;
	}

	if ((se_dev->chipdata->hmac_verify_hw_support == true)
			&& (is_last && (hmac_ctx->request_type == TEGRA_HV_VSE_HMAC_SHA_VERIFY))) {
		hash = tegra_hv_vse_get_dma_buf(hmac_ctx->node_id, SHA_HASH_BUF_IDX,
					TEGRA_VIRTUAL_SE_SHA_MAX_HMAC_SHA_LENGTH);
		if (!hash) {
			VSE_ERR("%s verify result buf is NULL\n", __func__);
			return -ENOMEM;
		}

		match = tegra_hv_vse_get_dma_buf(hmac_ctx->node_id, HMAC_SHA_COMP_BUF_IDX,
				RESULT_COMPARE_BUF_SIZE);
		if (!match) {
			VSE_ERR("%s match code buf is NULL\n", __func__);
			return -ENOMEM;
		}
	}

	g_crypto_to_ivc_map[hmac_ctx->node_id].vse_thread_start = true;

	msg_len = hmac_ctx->user_src_buf_size;
	temp_len = msg_len;

	memset(ivc_req_msg, 0, sizeof(struct tegra_virtual_se_ivc_msg_t));
	ivc_tx = &ivc_req_msg->tx[0];
	ivc_hdr = &ivc_req_msg->ivc_hdr;
	ivc_hdr->num_reqs = 1;
	ivc_hdr->header_magic[0] = 'N';
	ivc_hdr->header_magic[1] = 'V';
	ivc_hdr->header_magic[2] = 'D';
	ivc_hdr->header_magic[3] = 'A';
	ivc_hdr->engine = VIRTUAL_SE_SHA;
	ivc_hdr->tag.priv_data = priv;
	ivc_tx->cmd = cmd;

	phmac = &(ivc_tx->hmac);
	phmac->mode = hmac_ctx->mode;
	phmac->msg_total_length[2] = 0;
	phmac->msg_total_length[3] = 0;
	phmac->msg_left_length[2] = 0;
	phmac->msg_left_length[3] = 0;
	memcpy(phmac->keyslot, hmac_ctx->key_slot, KEYSLOT_SIZE_BYTES);
	if (se_dev->chipdata->allocate_key_slot_supported)
		phmac->token_id = hmac_ctx->token_id;
	phmac->src_addr = src->buf_iova;

	if (hmac_ctx->request_type == TEGRA_HV_VSE_HMAC_SHA_SIGN)
		phmac->dst_addr = hash->buf_iova;

	if (is_last) {
		/* Set msg left length equal to input buffer size */
		phmac->msg_left_length[0] = msg_len & 0xFFFFFFFF;
		phmac->msg_left_length[1] = msg_len >> 32;

		/* Set msg total length equal to sum of all input buffer size */
		phmac->msg_total_length[0] = hmac_ctx->total_count & 0xFFFFFFFF;
		phmac->msg_total_length[1] = hmac_ctx->total_count >> 32;

	} else {
		/* Set msg left length greater than input buffer size */
		temp_len += 8;
		phmac->msg_left_length[0] = temp_len & 0xFFFFFFFF;
		phmac->msg_left_length[1] = temp_len >> 32;

		/* Set msg total length greater than msg left length for non-first request */
		if (!hmac_ctx->is_first)
			temp_len += 8;

		phmac->msg_total_length[0] = temp_len & 0xFFFFFFFF;
		phmac->msg_total_length[1] = temp_len >> 32;
	}

	if (se_dev->chipdata->hmac_verify_hw_support == false) {
		if (is_last && (hmac_ctx->request_type == TEGRA_HV_VSE_HMAC_SHA_VERIFY)) {
			blocks_to_process = msg_len / hmac_ctx->blk_size;
			/* num of bytes less than block size */

			if ((hmac_ctx->user_src_buf_size % hmac_ctx->blk_size) ||
				blocks_to_process == 0) {
				last_block_bytes =
					msg_len % hmac_ctx->blk_size;
			} else {
				/* decrement num of blocks */
				blocks_to_process--;
				last_block_bytes = hmac_ctx->blk_size;
			}

			if (blocks_to_process > 0) {
				err = copy_from_user(src->buf_ptr, hmac_ctx->user_src_buf,
					blocks_to_process * hmac_ctx->blk_size);
				if (err) {
					VSE_ERR("%s(): Failed to copy src_buf: %d\n",
					__func__, err);
					goto unmap_exit;
				}
			}

			phmac->src_buf_size = blocks_to_process * hmac_ctx->blk_size;
			phmac->lastblock_len = last_block_bytes;
			err = copy_from_user(phmac->expected_hmac_sha,
				hmac_ctx->user_digest_buffer,
				TEGRA_VIRTUAL_SE_SHA_MAX_HMAC_SHA_LENGTH);
			if (err) {
				VSE_ERR("%s(): Failed to copy digest buf: %d\n",
				__func__, err);
				goto unmap_exit;
			}
			if (last_block_bytes > 0) {
				err = copy_from_user(phmac->lastblock,
				&hmac_ctx->user_src_buf[blocks_to_process * hmac_ctx->blk_size],
				last_block_bytes);
				if (err) {
					VSE_ERR("%s(): Failed to copy src_buf: %d\n",
					__func__, err);
					goto unmap_exit;
				}
			}
		} else {
			phmac->src_buf_size = msg_len;
			phmac->lastblock_len = 0;
			if (msg_len > 0) {
				err = copy_from_user(src->buf_ptr, hmac_ctx->user_src_buf,
						msg_len);
				if (err) {
					VSE_ERR("%s(): Failed to copy src_buf: %d\n",
					__func__, err);
					goto unmap_exit;
				}
			}
		}
	} else {
		phmac->src_buf_size = msg_len;
		phmac->lastblock_len = 0;
		if (msg_len > 0) {
			err = copy_from_user(src->buf_ptr, hmac_ctx->user_src_buf,
						msg_len);
			if (err) {
				VSE_ERR("%s(): Failed to copy src_buf: %d\n", __func__, err);
				goto unmap_exit;
			}
		}
		if (is_last && (hmac_ctx->request_type == TEGRA_HV_VSE_HMAC_SHA_VERIFY)) {
			phmac->hmac_addr = hash->buf_iova;
			err = copy_from_user(hash->buf_ptr, hmac_ctx->user_digest_buffer,
					TEGRA_VIRTUAL_SE_SHA_MAX_HMAC_SHA_LENGTH);
			if (err) {
				VSE_ERR("%s(): Failed to copy dst_buf: %d\n", __func__, err);
				goto unmap_exit;
			}
			phmac->dst_addr = match->buf_iova;
		}
	}

	priv->cmd = VIRTUAL_SE_PROCESS;
	priv->se_dev = se_dev;
	init_completion(&priv->alg_complete);

	err = tegra_hv_vse_safety_send_ivc_wait(se_dev, pivck, priv, ivc_req_msg,
			sizeof(struct tegra_virtual_se_ivc_msg_t), hmac_ctx->node_id);
	if (err) {
		dev_err(se_dev->dev, "failed to send data over ivc err %d\n", err);
		goto unmap_exit;
	}

	if (priv->rx_status != 0) {
		err = status_to_errno(priv->rx_status);
		dev_err(se_dev->dev, "%s: SE server returned error %u\n",
				__func__, priv->rx_status);
		goto unmap_exit;
	}

	if (is_last) {
		if (hmac_ctx->request_type == TEGRA_HV_VSE_HMAC_SHA_VERIFY) {
			if (se_dev->chipdata->hmac_verify_hw_support == false) {
				ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_HMAC_GET_VERIFY;
				priv->cmd = VIRTUAL_SE_PROCESS;
				init_completion(&priv->alg_complete);
				err = tegra_hv_vse_safety_send_ivc_wait(se_dev, pivck, priv,
					ivc_req_msg, sizeof(struct tegra_virtual_se_ivc_msg_t),
					hmac_ctx->node_id);
				if (err) {
					dev_err(se_dev->dev,
						"failed to send data over ivc err %d\n", err);
					goto unmap_exit;
				}

				if (priv->rx_status == 0) {
					hmac_ctx->result = 0;
				} else if (priv->rx_status == TEGRA_VIRTUAL_SE_ERR_MAC_INVALID) {
					dev_info(se_dev->dev, "%s: tag mismatch", __func__);
					hmac_ctx->result = 1;
				} else {
					err = status_to_errno(priv->rx_status);
					dev_err(se_dev->dev, "%s: SE server returned error %u\n",
							__func__, priv->rx_status);
				}
			} else {
				if (memcmp(match->buf_ptr, &matchcode, 4) == 0) {
					hmac_ctx->result = 0;
				} else if (memcmp(match->buf_ptr, &mismatch_code, 4) == 0) {
					dev_info(se_dev->dev, "%s: tag mismatch", __func__);
					hmac_ctx->result = 1;
				} else {
					dev_err(se_dev->dev, "%s: invalid tag match code",
							__func__);
					err = -EINVAL;
				}
			}
		} else {
			ret = copy_to_user(hmac_ctx->user_digest_buffer,
					hash->buf_ptr,
					TEGRA_VIRTUAL_SE_SHA_MAX_HMAC_SHA_LENGTH);
			if (ret) {
				VSE_ERR("%s(): Failed to copy dst_buf\n", __func__);
				err = -EFAULT;
				goto unmap_exit;
			}
		}
	}

unmap_exit:
	return err;
}

static int tegra_hv_vse_safety_hmac_sha_update(struct ahash_request *req)
{
	struct tegra_virtual_se_req_context *req_ctx;
	struct tegra_virtual_se_hmac_sha_context *hmac_ctx;
	struct tegra_virtual_se_dev *se_dev;
	int ret = 0;

	if (!req) {
		VSE_ERR("%s HMAC SHA request not valid\n", __func__);
		return -EINVAL;
	}

	hmac_ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	if (!hmac_ctx) {
		VSE_ERR("%s HMAC SHA req_ctx not valid\n", __func__);
		return -EINVAL;
	}

	req_ctx = ahash_request_ctx(req);
	if (!req_ctx) {
		VSE_ERR("%s HMAC SHA req not valid\n", __func__);
		return -EINVAL;
	}

	if (!req_ctx->req_context_initialized) {
		VSE_ERR("%s Request ctx not initialized\n", __func__);
		return -EINVAL;
	}

	ret = tegra_vse_validate_hmac_sha_params(hmac_ctx, false);
	if (ret) {
		VSE_ERR("%s: invalid HMAC SHA params\n", __func__);
		return ret;
	}

	se_dev = g_crypto_to_ivc_map[hmac_ctx->node_id].se_dev;

	/* Return error if engine is in suspended state */
	if (atomic_read(&se_dev->se_suspended)) {
		VSE_ERR("%s: Engine is in suspended state\n", __func__);
		return -ENODEV;
	}

	ret = tegra_hv_vse_safety_hmac_sha_sv_op(req, hmac_ctx, false);
	if (ret)
		dev_err(se_dev->dev, "tegra_se_hmac_sha_update failed - %d\n", ret);

	return ret;
}

static int tegra_hv_vse_safety_hmac_sha_finup(struct ahash_request *req)
{
	struct tegra_virtual_se_req_context *req_ctx;
	struct tegra_virtual_se_hmac_sha_context *hmac_ctx = NULL;
	struct tegra_virtual_se_dev *se_dev;
	int ret = 0;

	if (!req) {
		VSE_ERR("%s HMAC-SHA request not valid\n", __func__);
		return -EINVAL;
	}

	hmac_ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	if (!hmac_ctx) {
		VSE_ERR("%s HMAC-SHA req_ctx not valid\n", __func__);
		return -EINVAL;
	}

	req_ctx = ahash_request_ctx(req);
	if (!req_ctx) {
		VSE_ERR("%s HMAC-SHA req not valid\n", __func__);
		return -EINVAL;
	}

	if (!req_ctx->req_context_initialized) {
		VSE_ERR("%s Request ctx not initialized\n", __func__);
		return -EINVAL;
	}

	ret = tegra_vse_validate_hmac_sha_params(hmac_ctx, true);
	if (ret) {
		VSE_ERR("%s: invalid HMAC SHA params\n", __func__);
		return ret;
	}

	se_dev = g_crypto_to_ivc_map[hmac_ctx->node_id].se_dev;

	/* Return error if engine is in suspended state */
	if (atomic_read(&se_dev->se_suspended)) {
		VSE_ERR("%s: Engine is in suspended state\n", __func__);
		return -ENODEV;
	}

	ret = tegra_hv_vse_safety_hmac_sha_sv_op(req, hmac_ctx, true);
	if (ret)
		dev_err(se_dev->dev, "tegra_se_hmac_sha_finup failed - %d\n", ret);

	hmac_ctx->is_key_slot_allocated = false;

	req_ctx->req_context_initialized = false;

	return ret;
}

static int tegra_hv_vse_safety_hmac_sha_final(struct ahash_request *req)
{
	// Unsupported
	VSE_ERR("%s: This callback is not supported\n", __func__);
	return -EINVAL;
}

static int tegra_hv_vse_safety_hmac_sha_digest(struct ahash_request *req)
{
	// Unsupported
	VSE_ERR("%s: This callback is not supported\n", __func__);
	return -EINVAL;
}

static int tegra_hv_vse_safety_sha_export(struct ahash_request *req, void *out)
{
	struct tegra_virtual_se_req_context *req_ctx = ahash_request_ctx(req);

	memcpy(out, req_ctx, sizeof(*req_ctx));
	return 0;
}

static int tegra_hv_vse_safety_sha_import(struct ahash_request *req, const void *in)
{
	struct tegra_virtual_se_req_context *req_ctx = ahash_request_ctx(req);

	memcpy(req_ctx, in, sizeof(*req_ctx));
	return 0;
}

static int tegra_hv_vse_safety_sha_cra_init(struct crypto_tfm *tfm)
{
	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct tegra_virtual_se_req_context));

	return 0;
}

static void tegra_hv_vse_safety_sha_cra_exit(struct crypto_tfm *tfm)
{
}

static void tegra_hv_vse_safety_prepare_cmd(struct tegra_virtual_se_dev *se_dev,
	struct tegra_virtual_se_ivc_tx_msg_t *ivc_tx,
	struct tegra_virtual_se_aes_req_context *req_ctx,
	struct tegra_virtual_se_aes_context *aes_ctx,
	struct skcipher_request *req)
{
	union tegra_virtual_se_aes_args *aes;

	aes = &ivc_tx->aes;
	if (req_ctx->encrypt == true)
		ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_AES_ENCRYPT;
	else
		ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_AES_DECRYPT;

	memcpy(aes->op.keyslot, aes_ctx->key_slot, KEYSLOT_SIZE_BYTES);
	aes->op.release_keyslot = aes_ctx->release_key_flag;
	aes->op.key_instance = aes_ctx->key_instance_idx;
	aes->op.mode = req_ctx->op_mode;
	aes->op.ivsel = AES_ORIGINAL_IV;
	memcpy(aes->op.lctr, aes_ctx->iv,
			TEGRA_VIRTUAL_SE_AES_LCTR_SIZE);
	if ((req_ctx->op_mode == AES_CTR) || (req_ctx->op_mode == AES_SM4_CTR))
		aes->op.ctr_cntn = TEGRA_VIRTUAL_SE_AES_LCTR_CNTN;
	else if ((req_ctx->op_mode == AES_CBC) || (req_ctx->op_mode == AES_SM4_CBC)) {
		if (req_ctx->encrypt == true && aes_ctx->user_nonce == 1U &&
		    aes_ctx->b_is_first != 1U)
			aes->op.ivsel = AES_UPDATED_IV;
		else
			aes->op.ivsel = AES_IV_REG;
	}
}

static int tegra_hv_vse_safety_aes_gen_random_iv(
		struct tegra_virtual_se_dev *se_dev,
		struct skcipher_request *req,
		struct tegra_virtual_se_aes_context *aes_ctx,
		struct tegra_vse_priv_data *priv,
		struct tegra_virtual_se_ivc_msg_t *ivc_req_msg)
{
	struct tegra_virtual_se_ivc_tx_msg_t *ivc_tx = &ivc_req_msg->tx[0];
	struct tegra_hv_ivc_cookie *pivck;
	union tegra_virtual_se_aes_args *aes = &ivc_tx->aes;
	int err = 0;

	ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_AES_ENCRYPT_INIT;
	priv->cmd = VIRTUAL_SE_PROCESS;
	memcpy(aes->op.keyslot, aes_ctx->key_slot, KEYSLOT_SIZE_BYTES);
	aes->op.key_instance = aes_ctx->key_instance_idx;
	pivck = g_crypto_to_ivc_map[aes_ctx->node_id].ivck;

	init_completion(&priv->alg_complete);

	err = tegra_hv_vse_safety_send_ivc_wait(se_dev, pivck, priv, ivc_req_msg,
			sizeof(struct tegra_virtual_se_ivc_msg_t), aes_ctx->node_id);
	if (err) {
		dev_err(se_dev->dev, "failed to send data over ivc err %d\n", err);
		return err;
	}

	err = status_to_errno(priv->rx_status);

	if (err) {
		dev_err(se_dev->dev,
			"\n %s IV generation failed %d\n", __func__, err);
	}

	return err;
}

static int tegra_hv_vse_safety_process_aes_req(struct tegra_virtual_se_dev *se_dev,
		struct tegra_virtual_se_aes_context *aes_ctx, struct skcipher_request *req)
{
	struct tegra_virtual_se_aes_req_context *req_ctx;
	struct tegra_virtual_se_ivc_tx_msg_t *ivc_tx = NULL;
	struct tegra_virtual_se_ivc_hdr_t *ivc_hdr = NULL;
	struct tegra_hv_ivc_cookie *pivck;
	int err = 0;
	uint64_t ret = 0;
	struct tegra_virtual_se_ivc_msg_t *ivc_req_msg =
			g_crypto_to_ivc_map[aes_ctx->node_id].ivc_msg;
	struct tegra_vse_priv_data *priv = g_crypto_to_ivc_map[aes_ctx->node_id].priv;
	union tegra_virtual_se_aes_args *aes;
	const struct tegra_vse_dma_buf *src;

	memset(ivc_req_msg, 0, sizeof(struct tegra_virtual_se_ivc_msg_t));
	priv->req = req;
	ivc_tx = &ivc_req_msg->tx[0];
	aes = &ivc_tx->aes;

	req_ctx = skcipher_request_ctx(req);

	src = tegra_hv_vse_get_dma_buf(aes_ctx->node_id, AES_SRC_BUF_IDX,
			aes_ctx->user_src_buf_size);
	if (!src) {
		dev_err(req_ctx->se_dev->dev, "%s src_buf is NULL\n", __func__);
		return -ENOMEM;
	}

	if (aes_ctx->user_src_buf_size > 0) {
		err = copy_from_user(src->buf_ptr, aes_ctx->user_src_buf,
			aes_ctx->user_src_buf_size);
		if (err) {
			dev_err(req_ctx->se_dev->dev, "%s(): Failed to copy src_buf: %d\n",
			__func__, err);
			goto exit;
		}
	}

	pivck = g_crypto_to_ivc_map[aes_ctx->node_id].ivck;
	ivc_hdr = &ivc_req_msg->ivc_hdr;
	//Currently we support only one request per IVC message
	ivc_hdr->num_reqs = 1U;
	ivc_hdr->header_magic[0] = 'N';
	ivc_hdr->header_magic[1] = 'V';
	ivc_hdr->header_magic[2] = 'D';
	ivc_hdr->header_magic[3] = 'A';
	ivc_hdr->engine = g_crypto_to_ivc_map[aes_ctx->node_id].engine_id;
	ivc_hdr->tag.priv_data = priv;

	priv->se_dev = se_dev;
	g_crypto_to_ivc_map[aes_ctx->node_id].vse_thread_start = true;

	/*
	 * If aes_ctx->iv[0] is 1 and the request is for AES CBC/CTR encryption,
	 * it means that generation of random IV is required.
	 * If userNonce is not provided random IV generation is needed.
	 */
	if (req_ctx->encrypt &&
			(is_aes_mode_valid(req_ctx->op_mode) == 1) && (aes_ctx->user_nonce == 0U) &&
			(aes_ctx->iv[0] == 1)) {
		//Random IV generation is required
		err = tegra_hv_vse_safety_aes_gen_random_iv(se_dev, req, aes_ctx,
				priv, ivc_req_msg);
		if (err)
			goto exit;
	}
	priv->cmd = VIRTUAL_SE_AES_CRYPTO;

	tegra_hv_vse_safety_prepare_cmd(se_dev, ivc_tx, req_ctx, aes_ctx, req);

	aes->op.src_addr = (u64)src->buf_iova;
	aes->op.src_buf_size = aes_ctx->user_src_buf_size;
	aes->op.dst_addr = (u64)src->buf_iova;
	aes->op.dst_buf_size = aes_ctx->user_src_buf_size;

	init_completion(&priv->alg_complete);

	err = tegra_hv_vse_safety_send_ivc_wait(se_dev, pivck, priv, ivc_req_msg,
			sizeof(struct tegra_virtual_se_ivc_msg_t), aes_ctx->node_id);
	if (err) {
		dev_err(se_dev->dev, "failed to send data over ivc err %d\n", err);
		goto exit;
	}

	if (priv->rx_status == 0U) {
		if (aes_ctx->user_src_buf_size > 0) {
			ret = copy_to_user(aes_ctx->user_dst_buf, src->buf_ptr,
				aes_ctx->user_src_buf_size);
			if (ret) {
				dev_err(se_dev->dev, "%s(): Failed to copy dst_buf\n", __func__);
				err = -EFAULT;
				goto exit;
			}
		}
		if ((is_aes_mode_valid(req_ctx->op_mode) == 1)
				&& (req_ctx->encrypt == true) && (aes_ctx->user_nonce == 0U))
			memcpy(aes_ctx->iv, priv->iv, TEGRA_VIRTUAL_SE_AES_IV_SIZE);
	} else {
		dev_err(se_dev->dev,
				"%s: SE server returned error %u\n",
				__func__, priv->rx_status);
	}

	err = status_to_errno(priv->rx_status);

exit:
	return err;
}

static int tegra_hv_vse_safety_aes_cra_init(struct crypto_skcipher *tfm)
{
	tfm->reqsize =
		sizeof(struct tegra_virtual_se_aes_req_context);

	return 0;
}

static void tegra_hv_vse_safety_aes_cra_exit(struct crypto_skcipher *tfm)
{
	/* nothing to do as user releases the keyslot through tzvault TA */
}

static int tegra_hv_vse_safety_aes_cbc_encrypt(struct skcipher_request *req)
{
	int err = 0;
	struct tegra_virtual_se_aes_req_context *req_ctx = NULL;
	struct tegra_virtual_se_aes_context *aes_ctx;

	if (!req) {
		VSE_ERR("NULL req received by %s", __func__);
		return -EINVAL;
	}

	aes_ctx = crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	if (!aes_ctx) {
		VSE_ERR("%s AES req_ctx not valid\n", __func__);
		return -EINVAL;
	}

	err = tegra_vse_validate_aes_param(aes_ctx);
	if (err) {
		VSE_ERR("%s: invalid AES params\n", __func__);
		return err;
	}

	req_ctx = skcipher_request_ctx(req);
	if (!req_ctx) {
		VSE_ERR("%s AES req not valid\n", __func__);
		return -EINVAL;
	}

	req_ctx->encrypt = true;
	req_ctx->engine_id = g_crypto_to_ivc_map[aes_ctx->node_id].engine_id;
	req_ctx->se_dev = g_crypto_to_ivc_map[aes_ctx->node_id].se_dev;
	if ((req_ctx->se_dev->chipdata->sm_supported == false) && (aes_ctx->b_is_sm4 == 1U)) {
		VSE_ERR("%s: SM4 CBC is not supported for selected platform\n", __func__);
		return -EINVAL;
	}
	if (aes_ctx->b_is_sm4 == 1U)
		req_ctx->op_mode = AES_SM4_CBC;
	else
		req_ctx->op_mode = AES_CBC;

	err = tegra_hv_vse_safety_process_aes_req(req_ctx->se_dev, aes_ctx, req);
	if (err)
		dev_err(req_ctx->se_dev->dev,
				"%s failed with error %d\n", __func__, err);
	return err;
}

static int tegra_hv_vse_safety_aes_cbc_decrypt(struct skcipher_request *req)
{
	int err = 0;
	struct tegra_virtual_se_aes_req_context *req_ctx = NULL;
	struct tegra_virtual_se_aes_context *aes_ctx;

	if (!req) {
		VSE_ERR("NULL req received by %s", __func__);
		return -EINVAL;
	}

	aes_ctx = crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	if (!aes_ctx) {
		VSE_ERR("%s AES req_ctx not valid\n", __func__);
		return -EINVAL;
	}

	err = tegra_vse_validate_aes_param(aes_ctx);
	if (err) {
		VSE_ERR("%s: invalid AES params\n", __func__);
		return err;
	}

	req_ctx = skcipher_request_ctx(req);
	if (!req_ctx) {
		VSE_ERR("%s AES req not valid\n", __func__);
		return -EINVAL;
	}

	req_ctx->encrypt = false;

	req_ctx->engine_id = g_crypto_to_ivc_map[aes_ctx->node_id].engine_id;
	req_ctx->se_dev = g_crypto_to_ivc_map[aes_ctx->node_id].se_dev;

	if ((req_ctx->se_dev->chipdata->sm_supported == false) &&
		(aes_ctx->b_is_sm4 == 1U)) {
		VSE_ERR("%s: SM4 CBC is not supported for selected platform\n", __func__);
		return -EINVAL;
	}

	if (aes_ctx->b_is_sm4 == 1U)
		req_ctx->op_mode = AES_SM4_CBC;
	else
		req_ctx->op_mode = AES_CBC;

	err = tegra_hv_vse_safety_process_aes_req(req_ctx->se_dev, aes_ctx, req);
	if (err)
		dev_err(req_ctx->se_dev->dev,
				"%s failed with error %d\n", __func__, err);
	return err;
}

static int tegra_hv_vse_safety_aes_ctr_encrypt(struct skcipher_request *req)
{
	int err = 0;
	struct tegra_virtual_se_aes_req_context *req_ctx = NULL;
	struct tegra_virtual_se_aes_context *aes_ctx;

	if (!req) {
		VSE_ERR("NULL req received by %s", __func__);
		return -EINVAL;
	}

	aes_ctx = crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	if (!aes_ctx) {
		VSE_ERR("%s AES req_ctx not valid\n", __func__);
		return -EINVAL;
	}

	err = tegra_vse_validate_aes_param(aes_ctx);
	if (err) {
		VSE_ERR("%s: invalid AES params\n", __func__);
		return err;
	}

	req_ctx = skcipher_request_ctx(req);
	if (!req_ctx) {
		VSE_ERR("%s AES req not valid\n", __func__);
		return -EINVAL;
	}

	req_ctx->encrypt = true;
	req_ctx->engine_id = g_crypto_to_ivc_map[aes_ctx->node_id].engine_id;
	req_ctx->se_dev = g_crypto_to_ivc_map[aes_ctx->node_id].se_dev;
	if ((req_ctx->se_dev->chipdata->sm_supported == false) && (aes_ctx->b_is_sm4 == 1U)) {
		VSE_ERR("%s: SM4 CTR is not supported for selected platform\n", __func__);
		return -EINVAL;
	}
	if (aes_ctx->b_is_sm4 == 1U)
		req_ctx->op_mode = AES_SM4_CTR;
	else
		req_ctx->op_mode = AES_CTR;

	err = tegra_hv_vse_safety_process_aes_req(req_ctx->se_dev, aes_ctx, req);
	if (err)
		dev_err(req_ctx->se_dev->dev,
				"%s failed with error %d\n", __func__, err);
	return err;
}

static int tegra_hv_vse_safety_aes_ctr_decrypt(struct skcipher_request *req)
{
	int err = 0;
	struct tegra_virtual_se_aes_req_context *req_ctx = NULL;
	struct tegra_virtual_se_aes_context *aes_ctx;

	if (!req) {
		VSE_ERR("NULL req received by %s", __func__);
		return -EINVAL;
	}
	aes_ctx = crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	if (!aes_ctx) {
		VSE_ERR("%s AES req_ctx not valid\n", __func__);
		return -EINVAL;
	}

	err = tegra_vse_validate_aes_param(aes_ctx);
	if (err) {
		VSE_ERR("%s: invalid AES params\n", __func__);
		return err;
	}

	req_ctx = skcipher_request_ctx(req);

	req_ctx->encrypt = false;

	req_ctx->engine_id = g_crypto_to_ivc_map[aes_ctx->node_id].engine_id;
	req_ctx->se_dev = g_crypto_to_ivc_map[aes_ctx->node_id].se_dev;
	if ((req_ctx->se_dev->chipdata->sm_supported == false) && (aes_ctx->b_is_sm4 == 1U)) {
		VSE_ERR("%s: SM4 CTR is not supported for selected platform\n", __func__);
		return -EINVAL;
	}
	if (aes_ctx->b_is_sm4 == 1U)
		req_ctx->op_mode = AES_SM4_CTR;
	else
		req_ctx->op_mode = AES_CTR;

	err = tegra_hv_vse_safety_process_aes_req(req_ctx->se_dev, aes_ctx, req);
	if (err)
		dev_err(req_ctx->se_dev->dev,
				"%s failed with error %d\n", __func__, err);
	return err;
}

static int tegra_hv_vse_safety_tsec_sv_op(struct ahash_request *req,
		struct tegra_virtual_se_aes_cmac_context *cmac_ctx)
{
	struct tegra_virtual_se_dev *se_dev =
			g_crypto_to_ivc_map[cmac_ctx->node_id].se_dev;
	struct tegra_virtual_se_ivc_hdr_t *ivc_hdr;
	struct tegra_virtual_se_ivc_tx_msg_t *ivc_tx;
	struct tegra_virtual_se_ivc_msg_t *ivc_req_msg =
			g_crypto_to_ivc_map[cmac_ctx->node_id].ivc_msg;
	struct tegra_hv_ivc_cookie *pivck = g_crypto_to_ivc_map[cmac_ctx->node_id].ivck;
	int err = 0;
	uint64_t ret = 0;
	struct tegra_vse_priv_data *priv = g_crypto_to_ivc_map[cmac_ctx->node_id].priv;
	uint32_t tsec_fw_err;
	const struct tegra_vse_dma_buf *src, *mac, *fw_status;

	memset(ivc_req_msg, 0, sizeof(struct tegra_virtual_se_ivc_msg_t));
	ivc_tx = &ivc_req_msg->tx[0];
	ivc_hdr = &ivc_req_msg->ivc_hdr;
	ivc_hdr->num_reqs = 1;
	ivc_hdr->header_magic[0] = 'N';
	ivc_hdr->header_magic[1] = 'V';
	ivc_hdr->header_magic[2] = 'D';
	ivc_hdr->header_magic[3] = 'A';
	ivc_hdr->engine = g_crypto_to_ivc_map[cmac_ctx->node_id].engine_id;
	ivc_hdr->tag.priv_data = priv;

	g_crypto_to_ivc_map[cmac_ctx->node_id].vse_thread_start = true;

	src = tegra_hv_vse_get_dma_buf(cmac_ctx->node_id, TSEC_SRC_BUF_IDX,
			cmac_ctx->user_src_buf_size);
	if (!src) {
		VSE_ERR("%s src buf is NULL\n", __func__);
		return -ENOMEM;
	}

	mac = tegra_hv_vse_get_dma_buf(cmac_ctx->node_id, TSEC_MAC_BUF_IDX,
				TEGRA_VIRTUAL_SE_AES_CMAC_DIGEST_SIZE);
	if (!mac)
		return -ENOMEM;

	fw_status = tegra_hv_vse_get_dma_buf(cmac_ctx->node_id, TSEC_FW_STATUS_BUF_IDX,
				RESULT_COMPARE_BUF_SIZE);
	if (!fw_status)
		return -ENOMEM;
	*((uint32_t *)fw_status->buf_ptr) = 0xFFFFFFFF;

	if (cmac_ctx->user_src_buf_size > 0) {
		err = copy_from_user(src->buf_ptr, cmac_ctx->user_src_buf,
				cmac_ctx->user_src_buf_size);
		if (err) {
			VSE_ERR("%s(): Failed to copy src_buf: %d\n", __func__, err);
			goto free_mem;
		}
	}

	ivc_tx->tsec[0U].src_addr = src->buf_iova;
	ivc_tx->tsec[0U].dst_addr = mac->buf_iova;
	ivc_tx->tsec[0U].fw_status_addr = fw_status->buf_iova;
	ivc_tx->tsec[0U].src_buf_size = cmac_ctx->user_src_buf_size;
	memcpy(&ivc_tx->tsec[0U].keyslot, cmac_ctx->key_slot, sizeof(uint64_t));

	if (cmac_ctx->request_type == TEGRA_HV_VSE_CMAC_SIGN) {
		ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_TSEC_SIGN;
	} else {
		ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_TSEC_VERIFY;

		err = copy_from_user(ivc_tx->tsec[0U].cmac_result, cmac_ctx->user_mac_buf,
						TEGRA_VIRTUAL_SE_AES_CMAC_DIGEST_SIZE);
		if (err) {
			VSE_ERR("%s(): Failed to copy mac_buf: %d\n", __func__, err);
			err = -EINVAL;
			goto free_mem;
		}
	}

	priv->cmd = VIRTUAL_SE_PROCESS;
	priv->se_dev = se_dev;

	init_completion(&priv->alg_complete);

	err = tegra_hv_vse_safety_send_ivc_wait(se_dev, pivck, priv, ivc_req_msg,
			sizeof(struct tegra_virtual_se_ivc_msg_t), cmac_ctx->node_id);
	if (err) {
		dev_err(se_dev->dev, "failed to send data over ivc err %d\n", err);
		goto free_mem;
	}

	if (priv->rx_status != 0) {
		err = status_to_errno(priv->rx_status);
		dev_err(se_dev->dev, "%s: SE server returned error %u\n",
				__func__, priv->rx_status);
		goto free_mem;
	}

	if (cmac_ctx->request_type == TEGRA_HV_VSE_CMAC_VERIFY) {
		ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_AES_CMD_GET_TSEC_VERIFY;

		priv->cmd = VIRTUAL_CMAC_PROCESS;
		init_completion(&priv->alg_complete);

		err = tegra_hv_vse_safety_send_ivc_wait(se_dev, pivck, priv, ivc_req_msg,
			sizeof(struct tegra_virtual_se_ivc_msg_t), cmac_ctx->node_id);
		if (err) {
			dev_err(se_dev->dev, "failed to send data over ivc err %d\n", err);
			goto free_mem;
		}
	}

	if (cmac_ctx->request_type == TEGRA_HV_VSE_CMAC_SIGN) {
		tsec_fw_err = (*((uint32_t *)fw_status->buf_ptr) & NVVSE_TSEC_CMD_STATUS_ERR_MASK);
		if (tsec_fw_err == 0U) {
			ret = copy_to_user(cmac_ctx->user_mac_buf,  mac->buf_ptr,
				TEGRA_VIRTUAL_SE_AES_CMAC_DIGEST_SIZE);
			if (ret) {
				VSE_ERR("%s(): Failed to copy mac_buf\n", __func__);
				err = -EFAULT;
				goto free_mem;
			}
		} else {
			err = -EINVAL;
			dev_err(se_dev->dev, "%s: TSEC FW returned error %u\n", __func__,
					tsec_fw_err);
			goto free_mem;
		}
	} else {
		if (priv->rx_status == 0)
			cmac_ctx->result = 0;
		else
			cmac_ctx->result = 1;
	}

	if ((priv->rx_status != 0) &&
			(priv->rx_status != TEGRA_VIRTUAL_SE_ERR_MAC_INVALID)) {
		err = status_to_errno(priv->rx_status);
		dev_err(se_dev->dev, "%s: SE server returned error %u\n",
				__func__, priv->rx_status);
	}

free_mem:
	return err;
}

static int tegra_hv_vse_safety_cmac_sv_op_hw_verify_supported(
	struct ahash_request *req,
	struct tegra_virtual_se_aes_cmac_context *cmac_ctx,
	bool is_last)
{
	struct tegra_virtual_se_dev *se_dev =
				g_crypto_to_ivc_map[cmac_ctx->node_id].se_dev;
	struct tegra_virtual_se_ivc_hdr_t *ivc_hdr;
	struct tegra_virtual_se_ivc_tx_msg_t *ivc_tx;
	struct tegra_virtual_se_ivc_msg_t *ivc_req_msg =
			g_crypto_to_ivc_map[cmac_ctx->node_id].ivc_msg;
	struct tegra_hv_ivc_cookie *pivck = g_crypto_to_ivc_map[cmac_ctx->node_id].ivck;
	int err = 0;
	uint64_t ret = 0;
	struct tegra_vse_priv_data *priv = g_crypto_to_ivc_map[cmac_ctx->node_id].priv;
	u32 match_code = SE_HW_VALUE_MATCH_CODE;
	u32 mac_buf_size = 16;
	const struct tegra_vse_dma_buf *src, *mac, *comp;

	src = tegra_hv_vse_get_dma_buf(cmac_ctx->node_id, AES_SRC_BUF_IDX,
			cmac_ctx->user_src_buf_size);
	if (!src) {
		VSE_ERR("%s src buf is NULL\n", __func__);
		return -ENOMEM;
	}

	mac = tegra_hv_vse_get_dma_buf(cmac_ctx->node_id, AES_TAG_BUF_IDX, mac_buf_size);
	if (!mac) {
		VSE_ERR("%s mac buf is NULL\n", __func__);
		return -ENOMEM;
	}

	memset(ivc_req_msg, 0, sizeof(struct tegra_virtual_se_ivc_msg_t));
	ivc_tx = &ivc_req_msg->tx[0];
	ivc_hdr = &ivc_req_msg->ivc_hdr;
	ivc_hdr->num_reqs = 1;
	ivc_hdr->header_magic[0] = 'N';
	ivc_hdr->header_magic[1] = 'V';
	ivc_hdr->header_magic[2] = 'D';
	ivc_hdr->header_magic[3] = 'A';

	if (cmac_ctx->request_type == TEGRA_HV_VSE_CMAC_VERIFY) {
		comp = tegra_hv_vse_get_dma_buf(cmac_ctx->node_id, AES_COMP_BUF_IDX,
							RESULT_COMPARE_BUF_SIZE);
		if (!comp) {
			VSE_ERR("%s mac comp buf is NULL\n", __func__);
			return -ENOMEM;
		}
	}

	g_crypto_to_ivc_map[cmac_ctx->node_id].vse_thread_start = true;

	if (cmac_ctx->user_src_buf_size > 0) {
		err = copy_from_user(src->buf_ptr, cmac_ctx->user_src_buf,
				cmac_ctx->user_src_buf_size);
		if (err) {
			VSE_ERR("%s(): Failed to copy src_buf: %d\n", __func__, err);
			goto free_mem;
		}
	}

	ivc_tx->aes.op_cmac_sv.src_addr = src->buf_iova;
	ivc_tx->aes.op_cmac_sv.src_buf_size = cmac_ctx->user_src_buf_size;
	ivc_hdr->engine = g_crypto_to_ivc_map[cmac_ctx->node_id].engine_id;
	if (cmac_ctx->request_type == TEGRA_HV_VSE_CMAC_SIGN)
		ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_AES_CMAC_SIGN;
	else {
		ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_AES_CMAC_VERIFY;
		ivc_tx->aes.op_cmac_sv.mac_comp_res_addr = comp->buf_iova;
	}
	memcpy(ivc_tx->aes.op_cmac_sv.keyslot, cmac_ctx->key_slot, KEYSLOT_SIZE_BYTES);
	ivc_tx->aes.op_cmac_sv.token_id = cmac_ctx->token_id;
	ivc_tx->aes.op_cmac_sv.config = 0;

	if (cmac_ctx->b_is_sm4 == 1U)
		ivc_tx->aes.op_cmac_sv.sym_ciph = VSE_SYM_CIPH_SM4;
	else
		ivc_tx->aes.op_cmac_sv.sym_ciph = VSE_SYM_CIPH_AES;

	if (is_last == true)
		ivc_tx->aes.op_cmac_sv.config |= TEGRA_VIRTUAL_SE_AES_CMAC_SV_CONFIG_LASTREQ;

	if (cmac_ctx->is_first) {
		ivc_tx->aes.op_cmac_sv.config |= TEGRA_VIRTUAL_SE_AES_CMAC_SV_CONFIG_FIRSTREQ;
		if (cmac_ctx->request_type == TEGRA_HV_VSE_CMAC_VERIFY) {
			err = copy_from_user((uint8_t *)mac->buf_ptr, cmac_ctx->user_mac_buf,
						TEGRA_VIRTUAL_SE_AES_CMAC_DIGEST_SIZE);
			if (err) {
				VSE_ERR("%s(): Failed to copy mac_buf: %d\n", __func__, err);
				goto free_mem;
			}
		}
		cmac_ctx->is_first = false;
	}
	ivc_tx->aes.op_cmac_sv.mac_addr = mac->buf_iova;
	ivc_hdr->tag.priv_data = priv;
	priv->cmd = VIRTUAL_CMAC_PROCESS;

	priv->se_dev = se_dev;
	init_completion(&priv->alg_complete);
	err = tegra_hv_vse_safety_send_ivc_wait(se_dev, pivck, priv, ivc_req_msg,
			sizeof(struct tegra_virtual_se_ivc_msg_t), cmac_ctx->node_id);
	if (err) {
		dev_err(se_dev->dev, "failed to send data over ivc err %d\n", err);
		goto free_mem;
	}
	if (priv->rx_status != 0) {
		err = status_to_errno(priv->rx_status);
		dev_err(se_dev->dev, "%s: SE server returned error %u\n",
				__func__, priv->rx_status);
		goto free_mem;
	}
	if (cmac_ctx->request_type == TEGRA_HV_VSE_CMAC_SIGN) {
		if (priv->rx_status == 0) {
			ret = copy_to_user(cmac_ctx->user_mac_buf, (uint8_t *)mac->buf_ptr,
				TEGRA_VIRTUAL_SE_AES_CMAC_DIGEST_SIZE);
			if (ret) {
				dev_err(se_dev->dev, "%s(): Failed to copy mac_buf\n",
				__func__);
				err = -EFAULT;
				goto free_mem;
			}
		}
	} else {
		if (memcmp((uint8_t *)comp->buf_ptr, &match_code, 4) == 0)
			cmac_ctx->result = 0;
		else
			cmac_ctx->result = 1;
	}

free_mem:
	return err;
}

static int tegra_hv_vse_safety_cmac_sv_op(struct ahash_request *req,
		struct tegra_virtual_se_aes_cmac_context *cmac_ctx, bool is_last)
{
	struct tegra_virtual_se_dev *se_dev =
				g_crypto_to_ivc_map[cmac_ctx->node_id].se_dev;
	struct tegra_virtual_se_ivc_hdr_t *ivc_hdr;
	struct tegra_virtual_se_ivc_tx_msg_t *ivc_tx;
	struct tegra_virtual_se_ivc_msg_t *ivc_req_msg =
			g_crypto_to_ivc_map[cmac_ctx->node_id].ivc_msg;
	struct tegra_hv_ivc_cookie *pivck = g_crypto_to_ivc_map[cmac_ctx->node_id].ivck;
	u32 blocks_to_process, last_block_bytes = 0;
	unsigned int total_len;
	int err = 0;
	uint64_t ret = 0;
	struct tegra_vse_priv_data *priv = g_crypto_to_ivc_map[cmac_ctx->node_id].priv;
	const struct tegra_vse_dma_buf *src;

	blocks_to_process = cmac_ctx->user_src_buf_size / TEGRA_VIRTUAL_SE_AES_BLOCK_SIZE;
	/* num of bytes less than block size */

	if ((cmac_ctx->user_src_buf_size % TEGRA_VIRTUAL_SE_AES_BLOCK_SIZE) ||
		blocks_to_process == 0) {
		last_block_bytes =
			cmac_ctx->user_src_buf_size % TEGRA_VIRTUAL_SE_AES_BLOCK_SIZE;
	} else {
		/* decrement num of blocks */
		blocks_to_process--;
		last_block_bytes = TEGRA_VIRTUAL_SE_AES_BLOCK_SIZE;
	}

	memset(ivc_req_msg, 0, sizeof(struct tegra_virtual_se_ivc_msg_t));
	ivc_tx = &ivc_req_msg->tx[0];
	ivc_hdr = &ivc_req_msg->ivc_hdr;
	ivc_hdr->num_reqs = 1;
	ivc_hdr->header_magic[0] = 'N';
	ivc_hdr->header_magic[1] = 'V';
	ivc_hdr->header_magic[2] = 'D';
	ivc_hdr->header_magic[3] = 'A';
	ivc_hdr->tag.priv_data = priv;

	g_crypto_to_ivc_map[cmac_ctx->node_id].vse_thread_start = true;

	src = tegra_hv_vse_get_dma_buf(cmac_ctx->node_id, AES_SRC_BUF_IDX,
			cmac_ctx->user_src_buf_size);
	if (!src) {
		dev_err(se_dev->dev, "%s src buf is NULL\n", __func__);
		err = -ENOMEM;
		goto free_mem;
	}

	/* first process all blocks except last block */
	if (blocks_to_process) {
		total_len = blocks_to_process * TEGRA_VIRTUAL_SE_AES_BLOCK_SIZE;

		if (total_len > 0) {
			err = copy_from_user(src->buf_ptr, cmac_ctx->user_src_buf, total_len);
			if (err) {
				dev_err(se_dev->dev, "%s(): Failed to copy src_buf: %d\n",
				__func__, err);
				goto free_mem;
			}
		}
		ivc_tx->aes.op_cmac_sv.src_addr = src->buf_iova;
		ivc_tx->aes.op_cmac_sv.src_buf_size = total_len;
	}
	ivc_tx->aes.op_cmac_sv.lastblock_len = last_block_bytes;

	if (cmac_ctx->b_is_sm4 == 1U) {
		ivc_tx->aes.op_cmac_sv.sym_ciph = VSE_SYM_CIPH_SM4;
	} else {
		ivc_tx->aes.op_cmac_sv.sym_ciph = VSE_SYM_CIPH_AES;
	}

	if (last_block_bytes > 0) {
		err = copy_from_user(ivc_tx->aes.op_cmac_sv.lastblock,
			&cmac_ctx->user_src_buf[blocks_to_process * TEGRA_VIRTUAL_SE_AES_BLOCK_SIZE],
			last_block_bytes);
		if (err) {
			dev_err(se_dev->dev, "%s(): Failed to copy src_buf: %d\n",
			__func__, err);
			goto free_mem;
		}
	}
	ivc_hdr->engine = g_crypto_to_ivc_map[cmac_ctx->node_id].engine_id;
	if (cmac_ctx->request_type == TEGRA_HV_VSE_CMAC_SIGN)
		ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_AES_CMAC_SIGN;
	else
		ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_AES_CMAC_VERIFY;

	memcpy(ivc_tx->aes.op_cmac_sv.keyslot, cmac_ctx->key_slot, KEYSLOT_SIZE_BYTES);
	ivc_tx->aes.op_cmac_sv.src_buf_size = blocks_to_process * TEGRA_VIRTUAL_SE_AES_BLOCK_SIZE;
	ivc_tx->aes.op_cmac_sv.config = 0;
	if (is_last == true)
		ivc_tx->aes.op_cmac_sv.config |= TEGRA_VIRTUAL_SE_AES_CMAC_SV_CONFIG_LASTREQ;

	if (cmac_ctx->is_first) {
		ivc_tx->aes.op_cmac_sv.config |= TEGRA_VIRTUAL_SE_AES_CMAC_SV_CONFIG_FIRSTREQ;
		if (cmac_ctx->request_type == TEGRA_HV_VSE_CMAC_VERIFY) {
			err = copy_from_user(ivc_tx->aes.op_cmac_sv.cmac_result,
						cmac_ctx->user_mac_buf,
						TEGRA_VIRTUAL_SE_AES_CMAC_DIGEST_SIZE);
			if (err) {
				dev_err(se_dev->dev, "%s(): Failed to copy mac_buf: %d\n",
				__func__, err);
				goto free_mem;
			}
		}
		cmac_ctx->is_first = false;
	}


	priv->cmd = VIRTUAL_SE_PROCESS;
	priv->se_dev = se_dev;
	init_completion(&priv->alg_complete);

	err = tegra_hv_vse_safety_send_ivc_wait(se_dev, pivck, priv, ivc_req_msg,
			sizeof(struct tegra_virtual_se_ivc_msg_t), cmac_ctx->node_id);
	if (err) {
		dev_err(se_dev->dev, "failed to send data over ivc err %d\n", err);
		goto free_mem;
	}

	if (priv->rx_status != 0) {
		err = status_to_errno(priv->rx_status);
		dev_err(se_dev->dev, "%s: SE server returned error %u\n",
				__func__, priv->rx_status);
		goto free_mem;
	}

	if (is_last) {

		if (cmac_ctx->request_type == TEGRA_HV_VSE_CMAC_SIGN)
			ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_AES_CMD_GET_CMAC_SIGN;
		else
			ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_AES_CMD_GET_CMAC_VERIFY;
		priv->cmd = VIRTUAL_CMAC_PROCESS;
		init_completion(&priv->alg_complete);
		err = tegra_hv_vse_safety_send_ivc_wait(se_dev, pivck, priv, ivc_req_msg,
			sizeof(struct tegra_virtual_se_ivc_msg_t), cmac_ctx->node_id);
		if (err) {
			dev_err(se_dev->dev, "failed to send data over ivc err %d\n", err);
			goto free_mem;
		}

		if (cmac_ctx->request_type == TEGRA_HV_VSE_CMAC_SIGN) {
			if (priv->rx_status == 0) {
				ret = copy_to_user(cmac_ctx->user_mac_buf, priv->cmac.data,
				TEGRA_VIRTUAL_SE_AES_CMAC_DIGEST_SIZE);
				if (ret) {
					dev_err(se_dev->dev, "%s(): Failed to copy mac_buf\n",
					__func__);
					err = -EFAULT;
					goto free_mem;
				}
			}
		} else {
			if (priv->rx_status == 0)
				cmac_ctx->result = 0;
			else
				cmac_ctx->result = 1;
		}

		if ((priv->rx_status != 0) &&
				(priv->rx_status != TEGRA_VIRTUAL_SE_ERR_MAC_INVALID)) {
			err = status_to_errno(priv->rx_status);
			dev_err(se_dev->dev, "%s: SE server returned error %u\n",
					__func__, priv->rx_status);
		}
	}

free_mem:
	return err;

}

static int tegra_hv_vse_safety_cmac_init(struct ahash_request *req)
{
	struct tegra_virtual_se_dev *se_dev;
	struct crypto_ahash *tfm;
	struct tegra_virtual_se_aes_cmac_context *cmac_ctx;

	if (!req) {
		VSE_ERR("%s AES-CMAC request not valid\n", __func__);
		return -EINVAL;
	}

	tfm = crypto_ahash_reqtfm(req);
	if (!tfm) {
		VSE_ERR("%s AES-CMAC transform not valid\n", __func__);
		return -EINVAL;
	}

	cmac_ctx = crypto_ahash_ctx(tfm);
	if (!cmac_ctx) {
		VSE_ERR("%s AES-CMAC req_ctx not valid\n", __func__);
		return -EINVAL;
	}

	se_dev = g_crypto_to_ivc_map[cmac_ctx->node_id].se_dev;

	/* Return error if engine is in suspended state */
	if (atomic_read(&se_dev->se_suspended)) {
		VSE_ERR("%s: Engine is in suspended state\n", __func__);
		return -ENODEV;
	}

	cmac_ctx->digest_size = crypto_ahash_digestsize(tfm);
	cmac_ctx->is_first = true;
	cmac_ctx->req_context_initialized = true;

	return 0;
}

static void tegra_hv_vse_safety_cmac_req_deinit(struct ahash_request *req)
{
	struct tegra_virtual_se_aes_cmac_context *cmac_ctx;

	cmac_ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	if (!cmac_ctx) {
		VSE_ERR("%s AES-CMAC req_ctx not valid\n", __func__);
		return;
	}

	cmac_ctx->req_context_initialized = false;
}

static int tegra_hv_vse_safety_cmac_update(struct ahash_request *req)
{
	struct tegra_virtual_se_aes_cmac_context *cmac_ctx;
	struct tegra_virtual_se_dev *se_dev;
	int ret = 0;

	if (!req) {
		VSE_ERR("%s AES-CMAC request not valid\n", __func__);
		return -EINVAL;
	}

	cmac_ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	if (!cmac_ctx) {
		VSE_ERR("%s AES-CMAC req_ctx not valid\n", __func__);
		return -EINVAL;
	}

	ret = tegra_vse_validate_cmac_params(cmac_ctx, false);
	if (ret) {
		VSE_ERR("%s: invalid AES CMAC params\n", __func__);
		return ret;
	}

	se_dev = g_crypto_to_ivc_map[cmac_ctx->node_id].se_dev;

	/* Return error if engine is in suspended state */
	if (atomic_read(&se_dev->se_suspended)) {
		VSE_ERR("%s: Engine is in suspended state\n", __func__);
		return -ENODEV;
	}
	/* Do not process data in given request */
	if (se_dev->chipdata->cmac_hw_verify_supported)
		ret = tegra_hv_vse_safety_cmac_sv_op_hw_verify_supported(req, cmac_ctx, false);
	else
		ret = tegra_hv_vse_safety_cmac_sv_op(req, cmac_ctx, false);

	if (ret)
		dev_err(se_dev->dev, "tegra_se_cmac_update failed - %d\n", ret);

	return ret;
}

static int tegra_hv_tsec_safety_cmac_update(struct ahash_request *req)
{
	VSE_ERR("%s cmac_update is not supported for tsec\n", __func__);
	return -EINVAL;
}

static int tegra_hv_vse_safety_cmac_final(struct ahash_request *req)
{
	struct tegra_virtual_se_aes_cmac_context *cmac_ctx =
					crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	struct tegra_virtual_se_dev *se_dev =
			g_crypto_to_ivc_map[cmac_ctx->node_id].se_dev;

	/* Return error if engine is in suspended state */
	if (atomic_read(&se_dev->se_suspended))
		return -ENODEV;

	VSE_ERR("%s cmac_final is not supported\n", __func__);
	return -EINVAL;
}

static int tegra_hv_vse_safety_cmac_finup(struct ahash_request *req)
{
	struct tegra_virtual_se_aes_cmac_context *cmac_ctx = NULL;
	struct tegra_virtual_se_dev *se_dev;
	int ret = 0;

	if (!req) {
		VSE_ERR("%s AES-CMAC request not valid\n", __func__);
		return -EINVAL;
	}

	cmac_ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	if (!cmac_ctx) {
		VSE_ERR("%s AES-CMAC req_ctx not valid\n", __func__);
		return -EINVAL;
	}

	ret = tegra_vse_validate_cmac_params(cmac_ctx, true);
	if (ret) {
		VSE_ERR("%s: invalid AES CMAC params\n", __func__);
		return ret;
	}

	se_dev = g_crypto_to_ivc_map[cmac_ctx->node_id].se_dev;

	/* Return error if engine is in suspended state */
	if (atomic_read(&se_dev->se_suspended))
		return -ENODEV;
	/* Do not process data in given request */
	if (se_dev->chipdata->cmac_hw_verify_supported)
		ret = tegra_hv_vse_safety_cmac_sv_op_hw_verify_supported(req, cmac_ctx, true);
	else
		ret = tegra_hv_vse_safety_cmac_sv_op(req, cmac_ctx, true);

	if (ret)
		dev_err(se_dev->dev, "tegra_se_cmac_finup failed - %d\n", ret);

	tegra_hv_vse_safety_cmac_req_deinit(req);

	return ret;
}

static int tegra_hv_tsec_safety_cmac_finup(struct ahash_request *req)
{
	struct tegra_virtual_se_aes_cmac_context *cmac_ctx = NULL;
	struct tegra_virtual_se_dev *se_dev = NULL;
	int ret = 0;

	if (!req) {
		VSE_ERR("%s TSEC request not valid\n", __func__);
		return -EINVAL;
	}

	cmac_ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	if (!cmac_ctx) {
		VSE_ERR("%s TSEC req_ctx not valid\n", __func__);
		return -EINVAL;
	}

	se_dev = g_crypto_to_ivc_map[cmac_ctx->node_id].se_dev;

	/* Return error if engine is in suspended state */
	if (atomic_read(&se_dev->se_suspended)) {
		VSE_ERR("%s: Engine is in suspended state\n", __func__);
		return -ENODEV;
	}

	ret = tegra_hv_vse_safety_tsec_sv_op(req, cmac_ctx);
	if (ret)
		dev_err(se_dev->dev, "tegra_se_tsec_finup failed - %d\n", ret);

	tegra_hv_vse_safety_cmac_req_deinit(req);

	return ret;
}

static int tegra_hv_vse_safety_cmac_digest(struct ahash_request *req)
{
	struct tegra_virtual_se_aes_cmac_context *cmac_ctx =
				crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	struct tegra_virtual_se_dev *se_dev =
				g_crypto_to_ivc_map[cmac_ctx->node_id].se_dev;

	/* Return error if engine is in suspended state */
	if (atomic_read(&se_dev->se_suspended)) {
		VSE_ERR("%s: Engine is in suspended state\n", __func__);
		return -ENODEV;
	}

	return tegra_hv_vse_safety_cmac_init(req) ?: tegra_hv_vse_safety_cmac_final(req);
}

int tegra_hv_vse_safety_tsec_get_keyload_status(uint32_t node_id, uint32_t *err_code)
{
	struct tegra_virtual_se_dev *se_dev = NULL;
	struct tegra_virtual_se_ivc_hdr_t *ivc_hdr = NULL;
	struct tegra_virtual_se_ivc_tx_msg_t *ivc_tx = NULL;
	struct tegra_hv_ivc_cookie *pivck = NULL;
	struct tegra_virtual_se_ivc_msg_t *ivc_req_msg;
	struct tegra_vse_priv_data *priv = NULL;
	int err = 0;

	if (node_id >= MAX_NUMBER_MISC_DEVICES)
		return -ENODEV;

	se_dev = g_crypto_to_ivc_map[node_id].se_dev;
	pivck = g_crypto_to_ivc_map[node_id].ivck;

	priv = g_crypto_to_ivc_map[node_id].priv;
	ivc_req_msg = g_crypto_to_ivc_map[node_id].ivc_msg;

	memset(ivc_req_msg, 0, sizeof(struct tegra_virtual_se_ivc_msg_t));
	ivc_hdr = &ivc_req_msg->ivc_hdr;
	ivc_tx = &ivc_req_msg->tx[0];
	ivc_hdr->num_reqs = 1;
	ivc_hdr->header_magic[0] = 'N';
	ivc_hdr->header_magic[1] = 'V';
	ivc_hdr->header_magic[2] = 'D';
	ivc_hdr->header_magic[3] = 'A';
	ivc_hdr->tag.priv_data = priv;

	g_crypto_to_ivc_map[node_id].vse_thread_start = true;

	ivc_hdr->engine = g_crypto_to_ivc_map[node_id].engine_id;
	ivc_tx->cmd = TEGRA_VIRTUAL_TSEC_CMD_GET_KEYLOAD_STATUS;

	priv->cmd = VIRTUAL_SE_PROCESS;
	priv->se_dev = se_dev;
	init_completion(&priv->alg_complete);

	err = tegra_hv_vse_safety_send_ivc_wait(se_dev, pivck, priv, ivc_req_msg,
		sizeof(struct tegra_virtual_se_ivc_msg_t), node_id);
	if (err) {
		dev_err(se_dev->dev, "failed to send data over ivc err %d\n", err);
		goto free_exit;
	}

	if (priv->rx_status != 0U) {
		err = -EINVAL;
		if (priv->rx_status == VSE_MSG_ERR_TSEC_KEYLOAD_FAILED)
			*err_code = NVVSE_STATUS_SE_SERVER_TSEC_KEYLOAD_FAILED;
		else if (priv->rx_status == VSE_MSG_ERR_TSEC_KEYLOAD_STATUS_CHECK_TIMEOUT)
			*err_code = NVVSE_STATUS_SE_SERVER_TSEC_KEYLOAD_TIMEOUT;
		else
			*err_code = NVVSE_STATUS_SE_SERVER_ERROR;
	} else {
		err = 0;
		*err_code = 0U;
	}

free_exit:
	return err;
}
EXPORT_SYMBOL(tegra_hv_vse_safety_tsec_get_keyload_status);

static int tegra_hv_vse_safety_validate_membuf_common(struct tegra_virtual_se_membuf_context *ctx)
{
	struct tegra_virtual_se_dev *se_dev = NULL;
	int err = 0;

	if (!ctx) {
		VSE_ERR("%s ctx is null\n", __func__);
		err = -EINVAL;
		goto exit;
	}

	if (ctx->node_id >= MAX_NUMBER_MISC_DEVICES) {
		VSE_ERR("%s node_id is invalid\n", __func__);
		err = -ENODEV;
		goto exit;
	}

	se_dev = g_crypto_to_ivc_map[ctx->node_id].se_dev;

	if (ctx->fd < 0) {
		dev_err(se_dev->dev, "%s fd is invalid\n", __func__);
		err = -EINVAL;
		goto exit;
	}

exit:
	return err;
}

int tegra_hv_vse_safety_map_membuf(struct tegra_virtual_se_membuf_context *ctx)
{
	struct tegra_virtual_se_dev *se_dev = NULL;
	struct tegra_vse_membuf_ctx *membuf_ctx = NULL;
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	dma_addr_t dma_addr;
	dma_addr_t phys_addr;
	uint32_t i;
	int err = 0;

	err = tegra_hv_vse_safety_validate_membuf_common(ctx);
	if (err != 0)
		return err;

	se_dev = g_crypto_to_ivc_map[ctx->node_id].se_dev;

	if (g_node_dma[ctx->node_id].mapped_membuf_count >= MAX_ZERO_COPY_BUFS) {
		dev_err(se_dev->dev, "%s no free membuf_ctx\n", __func__);
		return -ENOMEM;
	}

	for (i = 0U; i < MAX_ZERO_COPY_BUFS; i++) {
		membuf_ctx = &g_node_dma[ctx->node_id].membuf_ctx[i];

		if (membuf_ctx->fd == -1)
			break;
	}

	if (i == MAX_ZERO_COPY_BUFS) {
		dev_err(se_dev->dev, "%s no free membuf_ctx\n", __func__);
		return -ENOMEM;
	}

	dmabuf = dma_buf_get(ctx->fd);
	if (IS_ERR_OR_NULL(dmabuf)) {
		dev_err(se_dev->dev, "%s dma_buf_get failed\n", __func__);
		return -EFAULT;
	}
	membuf_ctx->dmabuf = dmabuf;

	attach = dma_buf_attach(dmabuf, se_dev->dev);
	if (IS_ERR_OR_NULL(attach)) {
		err = PTR_ERR(dmabuf);
		dev_err(se_dev->dev, "%s dma_buf_attach failed\n", __func__);
		goto buf_attach_err;
	}
	membuf_ctx->attach = attach;

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR_OR_NULL(sgt)) {
		err = PTR_ERR(sgt);
		dev_err(se_dev->dev, "%s dma_buf_map_attachment failed\n", __func__);
		goto buf_map_err;
	}

	phys_addr = sg_phys(sgt->sgl);
	dma_addr = sg_dma_address(sgt->sgl);

	if (!dma_addr)
		dma_addr = phys_addr;

	ctx->iova = dma_addr;
	membuf_ctx->fd = ctx->fd;
	g_node_dma[ctx->node_id].mapped_membuf_count += 1U;

	return err;

buf_map_err:
	dma_buf_detach(dmabuf, attach);
buf_attach_err:
	dma_buf_put(dmabuf);
	membuf_ctx->fd = -1;

	return err;
}
EXPORT_SYMBOL(tegra_hv_vse_safety_map_membuf);

void tegra_hv_vse_safety_unmap_all_membufs(uint32_t node_id)
{
	struct tegra_vse_membuf_ctx *membuf_ctx = NULL;
	uint32_t i;

	if (node_id >= MAX_NUMBER_MISC_DEVICES) {
		VSE_ERR("%s node_id is invalid\n", __func__);
		return;
	}

	for (i = 0U; i < MAX_ZERO_COPY_BUFS; i++) {
		membuf_ctx = &g_node_dma[node_id].membuf_ctx[i];

		if (membuf_ctx->fd == -1)
			continue;

		dma_buf_detach(membuf_ctx->dmabuf, membuf_ctx->attach);
		dma_buf_put(membuf_ctx->dmabuf);
		membuf_ctx->fd = -1;
	}

	g_node_dma[node_id].mapped_membuf_count = 0U;
}
EXPORT_SYMBOL(tegra_hv_vse_safety_unmap_all_membufs);

int tegra_hv_vse_safety_unmap_membuf(struct tegra_virtual_se_membuf_context *ctx)
{
	struct tegra_virtual_se_dev *se_dev;
	struct tegra_vse_membuf_ctx *membuf_ctx = NULL;
	uint32_t i;
	int err = 0;

	err = tegra_hv_vse_safety_validate_membuf_common(ctx);
	if (err != 0)
		return err;

	se_dev = g_crypto_to_ivc_map[ctx->node_id].se_dev;

	if (g_node_dma[ctx->node_id].mapped_membuf_count == 0U) {
		dev_err(se_dev->dev, "%s no mapped membuf to free\n", __func__);
		return -EINVAL;
	}


	for (i = 0U; i < MAX_ZERO_COPY_BUFS; i++) {
		membuf_ctx = &g_node_dma[ctx->node_id].membuf_ctx[i];

		if (membuf_ctx->fd == ctx->fd)
			break;
	}

	if (i == MAX_ZERO_COPY_BUFS) {
		dev_err(se_dev->dev, "%s fd not found\n", __func__);
		return -EINVAL;
	}

	dma_buf_detach(membuf_ctx->dmabuf, membuf_ctx->attach);
	dma_buf_put(membuf_ctx->dmabuf);
	membuf_ctx->fd = -1;
	g_node_dma[ctx->node_id].mapped_membuf_count -= 1U;

	return 0;
}
EXPORT_SYMBOL(tegra_hv_vse_safety_unmap_membuf);

static int tegra_hv_vse_safety_cmac_setkey(struct crypto_ahash *tfm, const u8 *key,
		unsigned int keylen)
{
	struct tegra_virtual_se_aes_cmac_context *ctx =
			crypto_tfm_ctx(crypto_ahash_tfm(tfm));
	struct tegra_virtual_se_dev *se_dev;
	int err = 0;
	s8 label[TEGRA_VIRTUAL_SE_AES_MAX_KEY_SIZE];
	bool is_keyslot_label;

	if (!ctx)
		return -EINVAL;

	se_dev = g_crypto_to_ivc_map[ctx->node_id].se_dev;

	/* format: 'NVSEAES 1234567\0' */
	is_keyslot_label = sscanf(key, "%s", label) == 1 &&
		!strcmp(label, TEGRA_VIRTUAL_SE_AES_KEYSLOT_LABEL);

	if (is_keyslot_label) {
		memcpy(ctx->key_slot, key + KEYSLOT_OFFSET_BYTES, KEYSLOT_SIZE_BYTES);
		ctx->is_key_slot_allocated = true;
	} else {
		dev_err(se_dev->dev, "%s: Invalid keyslot label %s\n", __func__, key);
		return -EINVAL;
	}

	return err;
}

static int tegra_hv_vse_safety_cmac_cra_init(struct crypto_tfm *tfm)
{
	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
			 sizeof(struct tegra_virtual_se_aes_cmac_context));

	return 0;
}

static void tegra_hv_vse_safety_cmac_cra_exit(struct crypto_tfm *tfm)
{
	/* nothing to do as user releases the keyslot through tzvault TA */
}

static int tegra_hv_vse_safety_aes_setkey(struct crypto_skcipher *tfm,
	const u8 *key, u32 keylen)
{
	struct tegra_virtual_se_aes_context *ctx = crypto_skcipher_ctx(tfm);
	struct tegra_virtual_se_dev *se_dev;
	s8 label[TEGRA_VIRTUAL_SE_AES_MAX_KEY_SIZE];
	int err = 0;
	bool is_keyslot_label;

	if (!ctx)
		return -EINVAL;

	se_dev = g_crypto_to_ivc_map[ctx->node_id].se_dev;

	/* format: 'NVSEAES 1234567\0' */
	is_keyslot_label = sscanf(key, "%s", label) == 1 &&
		!strcmp(label, TEGRA_VIRTUAL_SE_AES_KEYSLOT_LABEL);

	if (is_keyslot_label) {
		memcpy(ctx->key_slot, key + KEYSLOT_OFFSET_BYTES, KEYSLOT_SIZE_BYTES);
		ctx->is_key_slot_allocated = true;
	} else {
		dev_err(se_dev->dev, "%s: Invalid keyslot label %s", __func__, key);
		err = -EINVAL;
	}

	return err;
}

static int tegra_hv_vse_safety_rng_drbg_init(struct crypto_tfm *tfm)
{
	return 0;
}

static void tegra_hv_vse_safety_rng_drbg_exit(struct crypto_tfm *tfm)
{
	return;
}

static int tegra_hv_vse_safety_get_random(struct tegra_virtual_se_rng_context *rng_ctx,
	u8 *rdata, unsigned int dlen, enum rng_call is_hw_req)
{
	struct tegra_virtual_se_dev *se_dev =
				g_crypto_to_ivc_map[rng_ctx->node_id].se_dev;
	u8 *rdata_addr;
	int err = 0;
	struct tegra_virtual_se_ivc_tx_msg_t *ivc_tx;
	struct tegra_hv_ivc_cookie *pivck = g_crypto_to_ivc_map[rng_ctx->node_id].ivck;
	struct tegra_virtual_se_ivc_msg_t *ivc_req_msg = NULL;
	struct tegra_virtual_se_ivc_hdr_t *ivc_hdr = NULL;
	struct tegra_vse_priv_data *priv = NULL;
	const struct tegra_vse_dma_buf *src;
	unsigned int chunk_size, aligned_size, copy_size;
	unsigned int bytes_remaining;
	unsigned int offset = 0;

	if (atomic_read(&se_dev->se_suspended)) {
		VSE_ERR("%s: Engine is in suspended state\n", __func__);
		return -ENODEV;
	}

	if (dlen == 0) {
		VSE_ERR("%s: Zero Data length is not supported\n", __func__);
		return -EINVAL;
	}

	if (is_hw_req == CRYPTODEV_RNG) {
		if ((dlen % TEGRA_VIRTUAL_SE_RNG_DT_SIZE) == 0)
			aligned_size = dlen;
		else
			aligned_size = dlen - (dlen % TEGRA_VIRTUAL_SE_RNG_DT_SIZE) +
				TEGRA_VIRTUAL_SE_RNG_DT_SIZE;
		/* calculate aligned size to the next multiple of TEGRA_VIRTUAL_SE_RNG_DT_SIZE */
		src = tegra_hv_vse_get_dma_buf(rng_ctx->node_id, AES_SRC_BUF_IDX, aligned_size);
		if (!src) {
			if (aligned_size < TEGRA_VIRTUAL_SE_RNG_DT_SIZE) {
				dev_err(se_dev->dev,
					"%s: aligned_size %u is less than RNG_DT_SIZE %u\n",
					__func__, aligned_size, TEGRA_VIRTUAL_SE_RNG_DT_SIZE);
				return -EINVAL;
			}
			aligned_size -= TEGRA_VIRTUAL_SE_RNG_DT_SIZE;
		/* If the aligned size is greater than the max dma_buf,
		 * decrease the aligned size by one alignment unit.
		 */
			src = tegra_hv_vse_get_dma_buf(rng_ctx->node_id,
				AES_SRC_BUF_IDX, aligned_size);
			if (!src) {
				dev_err(se_dev->dev, "%s src is NULL\n", __func__);
				return -ENOMEM;
			}
		}
		priv = g_crypto_to_ivc_map[rng_ctx->node_id].priv;
		ivc_req_msg = g_crypto_to_ivc_map[rng_ctx->node_id].ivc_msg;
	} else {
		src = &rng_ctx->hwrng_dma_buf;
		priv = rng_ctx->priv;
		ivc_req_msg = rng_ctx->ivc_msg;
	}

	memset(ivc_req_msg, 0, sizeof(struct tegra_virtual_se_ivc_msg_t));
	ivc_tx = &ivc_req_msg->tx[0];
	ivc_hdr = &ivc_req_msg->ivc_hdr;
	ivc_hdr->num_reqs = 1;
	ivc_hdr->header_magic[0] = 'N';
	ivc_hdr->header_magic[1] = 'V';
	ivc_hdr->header_magic[2] = 'D';
	ivc_hdr->header_magic[3] = 'A';
	ivc_hdr->engine = g_crypto_to_ivc_map[rng_ctx->node_id].engine_id;
	ivc_hdr->tag.priv_data = priv;
	priv->cmd = VIRTUAL_SE_PROCESS;
	priv->se_dev = se_dev;
	ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_AES_RNG_DBRG;

	bytes_remaining = dlen;

	while (bytes_remaining > 0) {
		if (is_hw_req == CRYPTODEV_RNG)
			chunk_size = bytes_remaining;
		else
			chunk_size = min(bytes_remaining, rng_ctx->hwrng_dma_buf.buf_len);

		aligned_size = chunk_size & (~(TEGRA_VIRTUAL_SE_RNG_DT_SIZE - 1U));

		if (aligned_size < TEGRA_VIRTUAL_SE_RNG_DT_SIZE)
			aligned_size = TEGRA_VIRTUAL_SE_RNG_DT_SIZE;

		ivc_tx->aes.op_rng.dst_addr.lo = src->buf_iova & 0xFFFFFFFF;
		ivc_tx->aes.op_rng.dst_addr.hi = (src->buf_iova >> 32) | aligned_size;
		init_completion(&priv->alg_complete);
		g_crypto_to_ivc_map[rng_ctx->node_id].vse_thread_start = true;

		err = tegra_hv_vse_safety_send_ivc_wait(se_dev, pivck, priv, ivc_req_msg,
			sizeof(struct tegra_virtual_se_ivc_msg_t), rng_ctx->node_id);
		if (err) {
			dev_err(se_dev->dev, "failed to send data over ivc err %d\n", err);
			goto exit;
		}

		if (priv->rx_status != 0) {
			err = status_to_errno(priv->rx_status);
			dev_err(se_dev->dev, "%s: SE server returned error %u\n",
					__func__, priv->rx_status);
			goto exit;
		}

		copy_size = min(bytes_remaining, aligned_size);
		rdata_addr = (rdata + offset);
		memcpy(rdata_addr, src->buf_ptr, copy_size);
		bytes_remaining -= copy_size;
		if (offset > UINT_MAX - copy_size) {
			dev_err(se_dev->dev, "%s: offset %u is greater than UINT_MAX\n",
				__func__, offset);
			err = -EINVAL;
			goto exit;
		} else
			offset += copy_size;
	}

exit:
	return err;
}

static int tegra_hv_vse_safety_rng_drbg_get_random(struct crypto_rng *tfm,
	const u8 *src, unsigned int slen, u8 *rdata, unsigned int dlen)
{
	struct tegra_virtual_se_rng_context *rng_ctx = crypto_rng_ctx(tfm);
	int ret = 0;

	ret = tegra_vse_validate_aes_rng_param(rng_ctx);
	if (ret)
		return ret;

	return tegra_hv_vse_safety_get_random(crypto_rng_ctx(tfm), rdata, dlen, CRYPTODEV_RNG);
}

static int tegra_hv_vse_safety_rng_drbg_reset(struct crypto_rng *tfm,
	const u8 *seed, unsigned int slen)
{
	return 0;
}

static int tegra_vse_aes_gcm_setkey(struct crypto_aead *tfm, const u8 *key,
	u32 keylen)
{
	/* copied from normal aes keyset, will remove if no modification needed*/
	struct tegra_virtual_se_aes_context *ctx = crypto_aead_ctx(tfm);
	struct tegra_virtual_se_dev *se_dev;
	s8 label[TEGRA_VIRTUAL_SE_AES_MAX_KEY_SIZE];
	int err = 0;
	bool is_keyslot_label;

	if (!ctx)
		return -EINVAL;

	se_dev = g_crypto_to_ivc_map[ctx->node_id].se_dev;

	/* format: 'NVSEAES 1234567\0' */
	is_keyslot_label = sscanf(key, "%s", label) == 1 &&
		!strcmp(label, TEGRA_VIRTUAL_SE_AES_KEYSLOT_LABEL);

	if (is_keyslot_label) {
		memcpy(ctx->key_slot, key + KEYSLOT_OFFSET_BYTES, KEYSLOT_SIZE_BYTES);
		ctx->is_key_slot_allocated = true;
	} else {
		dev_err(se_dev->dev, "%s: Invalid keyslot label %s\n", __func__, key);
		err = -EINVAL;
	}

	return err;
}

static int tegra_vse_aes_gcm_setauthsize(struct crypto_aead *tfm,
	unsigned int authsize)
{
	struct tegra_virtual_se_aes_context *ctx = crypto_aead_ctx(tfm);

	switch (authsize) {
	case 16:
		ctx->user_tag_buf_size = authsize;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int tegra_vse_aes_gcm_init(struct crypto_aead *tfm)
{
	return 0;
}

static void tegra_vse_aes_gcm_exit(struct crypto_aead *tfm)
{
	/* nothing to do as user unloads the key manually with tzvault*/
}

static int tegra_vse_aes_gcm_check_params(struct aead_request *req)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct tegra_virtual_se_aes_context *aes_ctx = crypto_aead_ctx(tfm);

	if (!tfm) {
		VSE_ERR("%s: transform not valid\n", __func__);
		return -EINVAL;
	}

	if (!aes_ctx) {
		VSE_ERR("%s: aes ctx invalid\n", __func__);
		return -EINVAL;
	}

	if (aes_ctx->node_id >= MAX_NUMBER_MISC_DEVICES) {
		VSE_ERR("%s: Node id is not valid\n", __func__);
		return -EINVAL;
	}

	if ((aes_ctx->user_aad_buf_size > 0 && aes_ctx->user_aad_buf == NULL) ||
		(aes_ctx->user_aad_buf_size > TEGRA_VIRTUAL_SE_MAX_SUPPORTED_BUFLEN)) {
		VSE_ERR("%s: aad buf is invalid\n", __func__);
		return -EINVAL;
	}

	if ((aes_ctx->user_src_buf_size > 0 && aes_ctx->user_src_buf == NULL) ||
		(aes_ctx->user_src_buf_size > TEGRA_VIRTUAL_SE_MAX_SUPPORTED_BUFLEN)) {
		VSE_ERR("%s: src buf is invalid\n", __func__);
		return -EINVAL;
	}

	if (aes_ctx->user_src_buf_size > 0 && aes_ctx->user_dst_buf == NULL) {
		VSE_ERR("%s: dst buf is NULL\n", __func__);
		return -EINVAL;
	}

	if ((aes_ctx->user_tag_buf_size > 0 && aes_ctx->user_tag_buf == NULL) ||
		(aes_ctx->user_tag_buf_size != TEGRA_VIRTUAL_SE_AES_GCM_TAG_SIZE)) {
		VSE_ERR("%s: tag buf is invalid\n", __func__);
		return -EINVAL;
	}

	if (unlikely(!aes_ctx->is_key_slot_allocated)) {
		VSE_ERR("%s: AES Key slot not allocated\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int tegra_vse_aes_gcm_enc_dec(struct aead_request *req,
		struct tegra_virtual_se_aes_context *aes_ctx, bool encrypt)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct tegra_virtual_se_dev *se_dev =
				g_crypto_to_ivc_map[aes_ctx->node_id].se_dev;
	struct tegra_virtual_se_ivc_msg_t *ivc_req_msg =
			g_crypto_to_ivc_map[aes_ctx->node_id].ivc_msg;
	struct tegra_virtual_se_ivc_hdr_t *ivc_hdr;
	struct tegra_virtual_se_ivc_tx_msg_t *ivc_tx;
	struct tegra_hv_ivc_cookie *pivck = g_crypto_to_ivc_map[aes_ctx->node_id].ivck;
	struct tegra_vse_priv_data *priv = g_crypto_to_ivc_map[aes_ctx->node_id].priv;
	int err = 0;
	uint64_t ret = 0;
	const struct tegra_vse_dma_buf *src, *aad, *tag;

	if (aes_ctx->user_aad_buf_size > 0) {
		aad = tegra_hv_vse_get_dma_buf(aes_ctx->node_id, AES_AAD_BUF_IDX,
				aes_ctx->user_aad_buf_size);
		if (!aad) {
			dev_err(se_dev->dev, "%s aad_buf is NULL\n", __func__);
			err = -ENOMEM;
			goto free_exit;
		}
		err = copy_from_user(aad->buf_ptr, aes_ctx->user_aad_buf,
				aes_ctx->user_aad_buf_size);
		if (err) {
			dev_err(se_dev->dev, "%s(): Failed to copy aad data: %d\n", __func__, err);
			goto free_exit;
		}
	}

	if (aes_ctx->user_src_buf_size > 0) {
		if (!encrypt)
			if (aes_ctx->user_src_buf_size >
				g_crypto_to_ivc_map[aes_ctx->node_id].mempool.buf_len)
				src = &g_node_dma[aes_ctx->node_id].gpc_dma_buf;
			else
				src = &g_crypto_to_ivc_map[aes_ctx->node_id].mempool;
		else
			src = tegra_hv_vse_get_dma_buf(aes_ctx->node_id,
						AES_SRC_BUF_IDX, aes_ctx->user_src_buf_size);
		if (!src) {
			dev_err(se_dev->dev, "%s enc src_buf is NULL\n", __func__);
			err = -ENOMEM;
			goto free_exit;
		}
		err = copy_from_user(src->buf_ptr, aes_ctx->user_src_buf, aes_ctx->user_src_buf_size);
		if (err) {
			dev_err(se_dev->dev, "%s(): Failed to copy src_buf: %d\n",
			__func__, err);
			goto free_exit;
		}
	}

	if (encrypt) {
		tag = tegra_hv_vse_get_dma_buf(aes_ctx->node_id, AES_TAG_BUF_IDX,
					TEGRA_VIRTUAL_SE_AES_GCM_TAG_IV_SIZE);
		if (!tag) {
			dev_err(se_dev->dev, "%s tag_buf is NULL\n", __func__);
			err = -ENOMEM;
			goto free_exit;
		}
	}

	memset(ivc_req_msg, 0, sizeof(struct tegra_virtual_se_ivc_msg_t));
	ivc_tx = &ivc_req_msg->tx[0];
	ivc_hdr = &ivc_req_msg->ivc_hdr;
	ivc_hdr->num_reqs = 1;
	ivc_hdr->header_magic[0] = 'N';
	ivc_hdr->header_magic[1] = 'V';
	ivc_hdr->header_magic[2] = 'D';
	ivc_hdr->header_magic[3] = 'A';
	ivc_hdr->engine = g_crypto_to_ivc_map[aes_ctx->node_id].engine_id;
	ivc_hdr->tag.priv_data = priv;

	priv->se_dev = se_dev;

	g_crypto_to_ivc_map[aes_ctx->node_id].vse_thread_start = true;

	memcpy(ivc_tx->aes.op_gcm.keyslot, aes_ctx->key_slot, KEYSLOT_SIZE_BYTES);

	if (encrypt) {
		/*
		 * If aes_ctx->iv[0] is 1 and the request is for AES CBC/CTR encryption,
		 * it means that generation of random IV is required.
		 * IV generation is not required if user nonce is provided.
		 */
		if (aes_ctx->iv[0] == 1 && aes_ctx->user_nonce == 0U) {
			//Random IV generation is required
			ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_AES_ENCRYPT_INIT;
			priv->cmd = VIRTUAL_SE_PROCESS;
			init_completion(&priv->alg_complete);

			err = tegra_hv_vse_safety_send_ivc_wait(se_dev, pivck, priv, ivc_req_msg,
				sizeof(struct tegra_virtual_se_ivc_msg_t), aes_ctx->node_id);
			if (err) {
				dev_err(se_dev->dev, "failed to send data over ivc err %d\n", err);
				goto free_exit;
			}

			err = status_to_errno(priv->rx_status);
			if (err) {
				dev_err(se_dev->dev,
					"\n %s IV generation failed %d\n", __func__, err);
				goto free_exit;
			}
			priv->cmd = VIRTUAL_SE_AES_GCM_ENC_PROCESS;
		} else {
			priv->cmd = VIRTUAL_SE_PROCESS;
		}
		ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_AES_GCM_CMD_ENCRYPT;
	} else {
		priv->cmd = VIRTUAL_SE_PROCESS;
		ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_AES_GCM_CMD_DECRYPT;
	}

	if (!encrypt) {
		/* copy iv for decryption*/
		memcpy(ivc_tx->aes.op_gcm.iv, aes_ctx->iv, crypto_aead_ivsize(tfm));

		/* copy expected tag */
		err = copy_from_user(ivc_tx->aes.op_gcm.expected_tag, aes_ctx->user_tag_buf,
				TEGRA_VIRTUAL_SE_AES_GCM_TAG_SIZE);
		if (err) {
			dev_err(se_dev->dev, "%s(): Failed to copy tag_buf: %d\n", __func__, err);
			goto free_exit;
		}
	} else {
		if (aes_ctx->user_nonce != 0U)
			memcpy(ivc_tx->aes.op_gcm.iv, aes_ctx->iv, crypto_aead_ivsize(tfm));
		}

	ivc_tx->aes.op_gcm.src_buf_size = aes_ctx->user_src_buf_size;
	ivc_tx->aes.op_gcm.dst_buf_size = aes_ctx->user_src_buf_size;
	if (aes_ctx->user_src_buf_size > 0) {
		ivc_tx->aes.op_gcm.src_addr = src->buf_iova;
		/* same source buffer can be used for destination buffer */
		ivc_tx->aes.op_gcm.dst_addr = ivc_tx->aes.op_gcm.src_addr;
	}

	ivc_tx->aes.op_gcm.aad_buf_size = aes_ctx->user_aad_buf_size;
	if (aes_ctx->user_aad_buf_size > 0)
		ivc_tx->aes.op_gcm.aad_addr = aad->buf_iova;

	if (encrypt) {
		ivc_tx->aes.op_gcm.tag_buf_size = aes_ctx->user_tag_buf_size;
		ivc_tx->aes.op_gcm.tag_addr = tag->buf_iova;
	}

	init_completion(&priv->alg_complete);

	err = tegra_hv_vse_safety_send_ivc_wait(se_dev, pivck, priv, ivc_req_msg,
		sizeof(struct tegra_virtual_se_ivc_msg_t), aes_ctx->node_id);
	if (err) {
		dev_err(se_dev->dev, "failed to send data over ivc err %d\n", err);
		goto free_exit;
	}

	if (priv->rx_status != 0) {
		dev_err(se_dev->dev, "%s: SE Server returned error %u\n", __func__,
									priv->rx_status);
		err = status_to_errno(priv->rx_status);
		goto free_exit;
	}

	if (encrypt) {
		if (aes_ctx->user_nonce == 0U) {
			/* copy iv to req for encryption*/
			memcpy(aes_ctx->iv, priv->iv, crypto_aead_ivsize(tfm));
		}
		if (aes_ctx->user_tag_buf_size > 0) {
			ret = copy_to_user(aes_ctx->user_tag_buf, tag->buf_ptr,
				aes_ctx->user_tag_buf_size);
			if (ret) {
				dev_err(se_dev->dev, "%s(): Failed to copy tag_buf\n", __func__);
				err = -EFAULT;
				goto free_exit;
			}
		}
	} else {
		priv->cmd = VIRTUAL_SE_PROCESS;
		ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_AES_CMD_GET_GCM_DEC;
		init_completion(&priv->alg_complete);

		err = tegra_hv_vse_safety_send_ivc_wait(se_dev, pivck, priv, ivc_req_msg,
				sizeof(struct tegra_virtual_se_ivc_msg_t), aes_ctx->node_id);
		if (err) {
			dev_err(se_dev->dev, "failed to send data over ivc err %d\n", err);
			goto free_exit;
		}

		if (priv->rx_status != 0) {
			dev_err(se_dev->dev, "%s: SE Server returned error %u\n", __func__,
					priv->rx_status);
			err = status_to_errno(priv->rx_status);
			goto free_exit;
		}
	}

	if (aes_ctx->user_src_buf_size > 0) {
		ret = copy_to_user(aes_ctx->user_dst_buf, src->buf_ptr,
				aes_ctx->user_src_buf_size);
		if (ret) {
			dev_err(se_dev->dev, "%s(): Failed to copy dst_buf\n", __func__);
			err = -EFAULT;
			goto free_exit;
		}
	}

free_exit:
	return err;
}

static int tegra_vse_aes_gcm_enc_dec_hw_support(struct aead_request *req,
		struct tegra_virtual_se_aes_context *aes_ctx, bool encrypt)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct tegra_virtual_se_dev *se_dev =
				g_crypto_to_ivc_map[aes_ctx->node_id].se_dev;
	struct tegra_virtual_se_ivc_msg_t *ivc_req_msg =
			g_crypto_to_ivc_map[aes_ctx->node_id].ivc_msg;
	struct tegra_virtual_se_ivc_hdr_t *ivc_hdr;
	struct tegra_virtual_se_ivc_tx_msg_t *ivc_tx;
	struct tegra_hv_ivc_cookie *pivck = g_crypto_to_ivc_map[aes_ctx->node_id].ivck;
	struct tegra_vse_priv_data *priv = g_crypto_to_ivc_map[aes_ctx->node_id].priv;
	int err = 0;
	uint64_t ret = 0;
	u32 match_code = SE_HW_VALUE_MATCH_CODE;
	u32 mismatch_code = SE_HW_VALUE_MISMATCH_CODE;
	const struct tegra_vse_dma_buf *src, *aad, *tag, *comp;

	comp = tegra_hv_vse_get_dma_buf(aes_ctx->node_id, AES_COMP_BUF_IDX,
					RESULT_COMPARE_BUF_SIZE);
	if (!comp) {
		dev_err(se_dev->dev, "%s mac comp buf is NULL\n", __func__);
		return -ENOMEM;
	}

	if (aes_ctx->user_aad_buf_size > 0) {
		aad = tegra_hv_vse_get_dma_buf(aes_ctx->node_id,
				AES_AAD_BUF_IDX, aes_ctx->user_aad_buf_size);
		if (!aad) {
			dev_err(se_dev->dev, "%s aad buf is NULL\n", __func__);
			return -ENOMEM;
		}
		err = copy_from_user(aad->buf_ptr, aes_ctx->user_aad_buf,
				aes_ctx->user_aad_buf_size);
		if (err) {
			dev_err(se_dev->dev, "%s(): Failed to copy aad data: %d\n", __func__, err);
			return err;
		}
	}

	if (aes_ctx->user_src_buf_size > 0) {
		src = tegra_hv_vse_get_dma_buf(aes_ctx->node_id,
			AES_SRC_BUF_IDX, aes_ctx->user_src_buf_size);
		if (!src) {
			dev_err(se_dev->dev, "%s src buf is NULL\n", __func__);
			return -ENOMEM;
		}
		err = copy_from_user(src->buf_ptr, aes_ctx->user_src_buf,
			aes_ctx->user_src_buf_size);
		if (err) {
			dev_err(se_dev->dev, "%s(): Failed to copy src_buf: %d\n",
			__func__, err);
			return err;
		}
	}

	if (encrypt) {
		tag = tegra_hv_vse_get_dma_buf(aes_ctx->node_id, AES_TAG_BUF_IDX,
					TEGRA_VIRTUAL_SE_AES_GCM_TAG_IV_SIZE);

		if (!tag) {
			dev_err(se_dev->dev, "%s tag buf is NULL\n", __func__);
			return -ENOMEM;
		}
	}

	memset(ivc_req_msg, 0, sizeof(struct tegra_virtual_se_ivc_msg_t));
	ivc_tx = &ivc_req_msg->tx[0];
	ivc_hdr = &ivc_req_msg->ivc_hdr;
	ivc_hdr->num_reqs = 1;
	ivc_hdr->header_magic[0] = 'N';
	ivc_hdr->header_magic[1] = 'V';
	ivc_hdr->header_magic[2] = 'D';
	ivc_hdr->header_magic[3] = 'A';
	ivc_hdr->engine = g_crypto_to_ivc_map[aes_ctx->node_id].engine_id;
	ivc_hdr->tag.priv_data = priv;

	priv->se_dev = se_dev;

	g_crypto_to_ivc_map[aes_ctx->node_id].vse_thread_start = true;

	memcpy(ivc_tx->aes.op_gcm.keyslot, aes_ctx->key_slot, KEYSLOT_SIZE_BYTES);
	ivc_tx->aes.op_gcm.token_id = aes_ctx->token_id;

	if (encrypt) {
		ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_AES_GCM_CMD_ENCRYPT;
		priv->cmd = VIRTUAL_SE_PROCESS;
	} else {
		priv->cmd = VIRTUAL_SE_PROCESS;
		ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_AES_GCM_CMD_DECRYPT;
	}

	if (!encrypt) {
		/* copy iv for decryption*/
		memcpy(ivc_tx->aes.op_gcm.iv, aes_ctx->iv, crypto_aead_ivsize(tfm));

		/* copy expected tag */
		err = copy_from_user(ivc_tx->aes.op_gcm.expected_tag, aes_ctx->user_tag_buf,
				TEGRA_VIRTUAL_SE_AES_GCM_TAG_SIZE);
		if (err) {
			dev_err(se_dev->dev, "%s(): Failed to copy tag_buf: %d\n", __func__, err);
			goto free_exit;
		}
	} else {
		if (aes_ctx->user_nonce != 0U)
			memcpy(ivc_tx->aes.op_gcm.iv, aes_ctx->iv, crypto_aead_ivsize(tfm));
}

	ivc_tx->aes.op_gcm.src_buf_size = aes_ctx->user_src_buf_size;
	ivc_tx->aes.op_gcm.dst_buf_size = aes_ctx->user_src_buf_size;
	if (aes_ctx->user_src_buf_size > 0) {
		ivc_tx->aes.op_gcm.src_addr = src->buf_iova;
		ivc_tx->aes.op_gcm.src_buf_size |= (uint32_t)((src->buf_iova >> 8)
						& ~((1U << 24) - 1U));

		/* same source buffer can be used for destination buffer */
		ivc_tx->aes.op_gcm.dst_addr = ivc_tx->aes.op_gcm.src_addr;
		ivc_tx->aes.op_gcm.dst_buf_size = ivc_tx->aes.op_gcm.src_buf_size;
	}

	ivc_tx->aes.op_gcm.aad_buf_size = aes_ctx->user_aad_buf_size;
	if (aes_ctx->user_aad_buf_size > 0)
		ivc_tx->aes.op_gcm.aad_addr = aad->buf_iova;

	if (encrypt) {
		if (aes_ctx->user_nonce == 0U)
			ivc_tx->aes.op_gcm.tag_buf_size = TEGRA_VIRTUAL_SE_AES_GCM_TAG_IV_SIZE;
		else
			ivc_tx->aes.op_gcm.tag_buf_size = aes_ctx->user_tag_buf_size;
		ivc_tx->aes.op_gcm.tag_addr = tag->buf_iova;
	} else
		ivc_tx->aes.op_gcm.gcm_vrfy_res_addr = comp->buf_iova;

	init_completion(&priv->alg_complete);

	err = tegra_hv_vse_safety_send_ivc_wait(se_dev, pivck, priv, ivc_req_msg,
		sizeof(struct tegra_virtual_se_ivc_msg_t), aes_ctx->node_id);
	if (err) {
		dev_err(se_dev->dev, "failed to send data over ivc err %d\n", err);
		goto free_exit;
	}

	if (priv->rx_status != 0) {
		dev_err(se_dev->dev, "%s: SE Server returned error %u\n", __func__,
									priv->rx_status);
		err = status_to_errno(priv->rx_status);
		goto free_exit;
	}

	if (encrypt) {
		if (aes_ctx->user_nonce == 0U) {
			/* Copy the IV (located after the 16-byte tag) from the buffer to aes_ctx->iv,
			 * based on the required IV size.
			 */
			memcpy(aes_ctx->iv, &((uint8_t *)tag->buf_ptr)[16],
				crypto_aead_ivsize(tfm));
		}
		/* copy tag to req for encryption */
		if (aes_ctx->user_tag_buf_size > 0) {
			ret = copy_to_user(aes_ctx->user_tag_buf, tag->buf_ptr,
					aes_ctx->user_tag_buf_size);
			if (ret) {
				dev_err(se_dev->dev, "%s(): Failed to copy tag_buf\n", __func__);
				err = -EFAULT;
				goto free_exit;
			}
		}
	} else {
		if (memcmp(comp->buf_ptr, &match_code, 4) != 0) {
			if (memcmp(comp->buf_ptr, &mismatch_code, 4) == 0)
				dev_info(se_dev->dev, "%s: tag mismatch\n", __func__);
			err = -EINVAL;
			goto free_exit;
		}
	}

	if (aes_ctx->user_src_buf_size > 0) {
		ret = copy_to_user(aes_ctx->user_dst_buf, src->buf_ptr,
				aes_ctx->user_src_buf_size);
		if (ret) {
			dev_err(se_dev->dev, "%s(): Failed to copy dst_buf\n", __func__);
			err = -EFAULT;
			goto free_exit;
		}
	}

free_exit:
	return err;
}

static int tegra_vse_aes_gcm_encrypt(struct aead_request *req)
{
	struct crypto_aead *tfm;
	struct tegra_virtual_se_aes_context *aes_ctx;
	struct tegra_virtual_se_dev *se_dev;
	int err = 0;

	if (!req) {
		VSE_ERR("%s: req is invalid\n", __func__);
		return -EINVAL;
	}

	tfm = crypto_aead_reqtfm(req);
	aes_ctx = crypto_aead_ctx(tfm);

	err = tegra_vse_aes_gcm_check_params(req);
	if (err) {
		VSE_ERR("%s: invalid AES params\n", __func__);
		return err;
	}

	se_dev = g_crypto_to_ivc_map[aes_ctx->node_id].se_dev;

	if (se_dev->chipdata->gcm_hw_iv_supported)
		err = tegra_vse_aes_gcm_enc_dec_hw_support(req, aes_ctx, true);
	else
		err = tegra_vse_aes_gcm_enc_dec(req, aes_ctx, true);

	if (err)
		dev_err(se_dev->dev, "%s failed %d\n", __func__, err);

	return err;
}

static int tegra_vse_aes_gcm_decrypt(struct aead_request *req)
{
	struct crypto_aead *tfm;
	struct tegra_virtual_se_aes_context *aes_ctx;
	struct tegra_virtual_se_dev *se_dev;
	int err = 0;
	if (!req) {
		VSE_ERR("%s: req is invalid\n", __func__);
		return -EINVAL;
	}

	tfm = crypto_aead_reqtfm(req);
	aes_ctx = crypto_aead_ctx(tfm);

	err = tegra_vse_aes_gcm_check_params(req);
	if (err) {
		VSE_ERR("%s: invalid AES params\n", __func__);
		return err;
	}

	se_dev = g_crypto_to_ivc_map[aes_ctx->node_id].se_dev;

	if (se_dev->chipdata->gcm_hw_iv_supported)
		err = tegra_vse_aes_gcm_enc_dec_hw_support(req, aes_ctx, false);
	else
		err = tegra_vse_aes_gcm_enc_dec(req, aes_ctx, false);
	if (err)
		dev_err(se_dev->dev, "%s failed %d\n", __func__, err);

	return err;
}

static int tegra_vse_aes_gmac_sv_check_params(struct ahash_request *req, bool is_last)
{
	struct tegra_virtual_se_aes_gmac_context *gmac_ctx =
					crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	struct tegra_virtual_se_dev *se_dev =
				g_crypto_to_ivc_map[gmac_ctx->node_id].se_dev;
	int err = 0;
	bool is_zero_copy;

	if ((gmac_ctx->request_type != TEGRA_HV_VSE_GMAC_SIGN) &&
		(gmac_ctx->request_type != TEGRA_HV_VSE_GMAC_VERIFY)) {
		dev_err(se_dev->dev, "%s: Invalid request type\n", __func__);
		err = -EINVAL;
	}

	if (gmac_ctx->node_id >= MAX_NUMBER_MISC_DEVICES) {
		dev_err(se_dev->dev, "%s: Node id is not valid\n", __func__);
		return -EINVAL;
	}

	is_zero_copy = g_crypto_to_ivc_map[gmac_ctx->node_id].is_zero_copy_node;

	if (gmac_ctx->is_key_slot_allocated == false) {
		dev_err(se_dev->dev, "%s: keyslot is not allocated\n", __func__);
		err = -EINVAL;
	}

	if (gmac_ctx->user_aad_buf_size > TEGRA_VIRTUAL_SE_MAX_SUPPORTED_BUFLEN) {
		dev_err(se_dev->dev, "%s: aad buf length exceeds max supported size\n", __func__);
		err = -EINVAL;
	}

	if (!is_zero_copy) {
		if (gmac_ctx->user_aad_buf == NULL) {
			dev_err(se_dev->dev, "%s: aad buf is NULL\n", __func__);
			err = -EINVAL;
		}
		if (is_last != 0) {
			if (gmac_ctx->authsize > 0 && gmac_ctx->user_tag_buf == NULL) {
				dev_err(se_dev->dev,
				"%s: tag buf length exceeds max supported size\n", __func__);
				err = -EINVAL;
			}
		}
	} else {
		if (gmac_ctx->request_type == TEGRA_HV_VSE_GMAC_SIGN) {
			if (is_last == 1 && gmac_ctx->user_tag_iova == 0) {
				dev_err(se_dev->dev, "%s: user tag iova is invalid\n", __func__);
				err = -EINVAL;
			}
		}
	}

	return err;
}

static int tegra_hv_vse_safety_gmac_cra_init(struct crypto_tfm *tfm)
{
	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
			 sizeof(struct tegra_virtual_se_aes_gmac_context));

	return 0;
}

static void tegra_hv_vse_safety_gmac_cra_exit(struct crypto_tfm *tfm)
{
	/* nothing to do as user releases the keyslot through tzvault TA */
}

static int tegra_hv_vse_aes_gmac_setkey(struct crypto_ahash *tfm, const u8 *key,
		unsigned int keylen)
{
	struct tegra_virtual_se_aes_gmac_context *ctx = crypto_ahash_ctx(tfm);
	struct tegra_virtual_se_dev *se_dev;
	s8 label[TEGRA_VIRTUAL_SE_AES_KEYSLOT_LABEL_SIZE];
	int err = 0;
	bool is_keyslot_label;

	if (!ctx) {
		VSE_ERR("%s: gmac ctx invalid", __func__);
		err = -EINVAL;
		goto exit;
	}

	se_dev = g_crypto_to_ivc_map[ctx->node_id].se_dev;

	/* format: 'NVSEAES 1234567\0' */
	is_keyslot_label = sscanf(key, "%s", label) == 1 &&
		(!strcmp(label, TEGRA_VIRTUAL_SE_AES_KEYSLOT_LABEL));

	if (is_keyslot_label) {
		memcpy(ctx->key_slot, key + KEYSLOT_OFFSET_BYTES, KEYSLOT_SIZE_BYTES);
		ctx->is_key_slot_allocated = true;
	} else {
		dev_err(se_dev->dev,
			"\n %s: Invalid keyslot label: %s\n", __func__, key);
		err = -EINVAL;
	}

exit:
	return err;
}

static int tegra_hv_vse_aes_gmac_sv_init(struct ahash_request *req)
{
	struct tegra_virtual_se_dev *se_dev;
	struct crypto_ahash *tfm = NULL;
	struct tegra_virtual_se_aes_gmac_context *gmac_ctx = NULL;
	struct tegra_virtual_se_ivc_msg_t *ivc_req_msg = NULL;
	struct tegra_virtual_se_ivc_hdr_t *ivc_hdr = NULL;
	struct tegra_virtual_se_ivc_tx_msg_t *ivc_tx = NULL;
	struct tegra_hv_ivc_cookie *pivck;
	struct tegra_vse_priv_data *priv = NULL;
	int err = 0;

	if (!req) {
		VSE_ERR("%s: request invalid\n", __func__);
		err = -EINVAL;
		goto exit;
	}

	tfm = crypto_ahash_reqtfm(req);
	if (!tfm) {
		VSE_ERR("%s: transform not valid\n", __func__);
		err = -EINVAL;
		goto exit;
	}

	gmac_ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	if (!gmac_ctx) {
		VSE_ERR("%s: req ctx invalid\n", __func__);
		err = -EINVAL;
		goto exit;
	}

	if (gmac_ctx->is_key_slot_allocated == false) {
		VSE_ERR("%s: keyslot is not allocated\n", __func__);
		err = -EPERM;
		goto exit;
	}

	se_dev = g_crypto_to_ivc_map[gmac_ctx->node_id].se_dev;
	/* Return error if engine is in suspended state */
	if (atomic_read(&se_dev->se_suspended)) {
		dev_err(se_dev->dev, "%s: engine is in suspended state", __func__);
		err = -ENODEV;
		goto exit;
	}

	if ((gmac_ctx->request_type == TEGRA_HV_VSE_GMAC_VERIFY)
			|| (gmac_ctx->request_type == TEGRA_HV_VSE_GMAC_SIGN)) {
		/* Initialize GMAC ctx */
		gmac_ctx->authsize = crypto_ahash_digestsize(tfm);
		gmac_ctx->req_context_initialized = true;

		/* Exit as GMAC_INIT request need not be sent to SE Server for SIGN/VERIFY */
		err = 0;
		goto exit;
	}

	priv = g_crypto_to_ivc_map[gmac_ctx->node_id].priv;
	ivc_req_msg = g_crypto_to_ivc_map[gmac_ctx->node_id].ivc_msg;

	memset(ivc_req_msg, 0, sizeof(struct tegra_virtual_se_ivc_msg_t));
	ivc_tx = &ivc_req_msg->tx[0];
	ivc_hdr = &ivc_req_msg->ivc_hdr;
	ivc_hdr->num_reqs = 1;
	ivc_hdr->header_magic[0] = 'N';
	ivc_hdr->header_magic[1] = 'V';
	ivc_hdr->header_magic[2] = 'D';
	ivc_hdr->header_magic[3] = 'A';
	ivc_hdr->engine = g_crypto_to_ivc_map[gmac_ctx->node_id].engine_id;
	ivc_hdr->tag.priv_data = priv;
	priv->cmd = VIRTUAL_SE_PROCESS;
	priv->se_dev = se_dev;

	ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_AES_GMAC_CMD_INIT;
	memcpy(ivc_tx->aes.op_gcm.keyslot, gmac_ctx->key_slot, KEYSLOT_SIZE_BYTES);
	ivc_tx->aes.op_gcm.key_instance = gmac_ctx->key_instance_idx;

	g_crypto_to_ivc_map[gmac_ctx->node_id].vse_thread_start = true;
	pivck = g_crypto_to_ivc_map[gmac_ctx->node_id].ivck;
	init_completion(&priv->alg_complete);

	err = tegra_hv_vse_safety_send_ivc_wait(se_dev, pivck, priv, ivc_req_msg,
		sizeof(struct tegra_virtual_se_ivc_msg_t), gmac_ctx->node_id);
	if (err) {
		dev_err(se_dev->dev, "failed to send data over ivc err %d\n", err);
		goto exit;
	}

	if (priv->rx_status != 0) {
		dev_err(se_dev->dev, "%s: SE server returned error %u\n", __func__,
									priv->rx_status);
		err = status_to_errno(priv->rx_status);
		goto exit;
	}

	ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_AES_CMD_GET_GMAC_IV;
	ivc_tx->aes.op_gcm.key_instance = gmac_ctx->key_instance_idx;
	priv->cmd = VIRTUAL_SE_AES_GCM_ENC_PROCESS;
	init_completion(&priv->alg_complete);
	err = tegra_hv_vse_safety_send_ivc_wait(se_dev, pivck, priv, ivc_req_msg,
			sizeof(struct tegra_virtual_se_ivc_msg_t), gmac_ctx->node_id);
	if (err) {
		dev_err(se_dev->dev, "failed to send data over ivc err %d\n", err);
		goto exit;
	}

	if (priv->rx_status != 0) {
		dev_err(se_dev->dev, "%s: SE server returned error %u\n", __func__,
									priv->rx_status);
		err = status_to_errno(priv->rx_status);
		goto exit;
	}

	memcpy(gmac_ctx->iv, priv->iv, TEGRA_VIRTUAL_SE_AES_GCM_IV_SIZE);

exit:
	return err;
}

static void tegra_hv_vse_aes_gmac_deinit(struct ahash_request *req)
{
	struct tegra_virtual_se_aes_gmac_context *gmac_ctx =
					crypto_ahash_ctx(crypto_ahash_reqtfm(req));

	gmac_ctx->is_key_slot_allocated = false;
	gmac_ctx->req_context_initialized = false;
}

static int tegra_hv_vse_aes_gmac_sv_op(struct ahash_request *req,
		struct tegra_virtual_se_aes_gmac_context *gmac_ctx, bool is_last)
{
	struct tegra_virtual_se_dev *se_dev;
	struct tegra_virtual_se_ivc_msg_t *ivc_req_msg =
			g_crypto_to_ivc_map[gmac_ctx->node_id].ivc_msg;
	struct tegra_virtual_se_ivc_hdr_t *ivc_hdr;
	struct tegra_virtual_se_ivc_tx_msg_t *ivc_tx;
	struct tegra_hv_ivc_cookie *pivck;
	struct tegra_vse_priv_data *priv = g_crypto_to_ivc_map[gmac_ctx->node_id].priv;
	int err = 0;
	uint64_t ret = 0;
	const struct tegra_vse_dma_buf *aad, *tag;

	se_dev = g_crypto_to_ivc_map[gmac_ctx->node_id].se_dev;
	pivck = g_crypto_to_ivc_map[gmac_ctx->node_id].ivck;

	aad = tegra_hv_vse_get_dma_buf(gmac_ctx->node_id, AES_AAD_BUF_IDX,
		gmac_ctx->user_aad_buf_size);
	if (!aad) {
		dev_err(se_dev->dev, "%s aad buf is NULL\n", __func__);
		err = -ENOMEM;
		goto exit;
	}
	if (gmac_ctx->user_aad_buf_size > 0) {
		err = copy_from_user(aad->buf_ptr, gmac_ctx->user_aad_buf,
			gmac_ctx->user_aad_buf_size);
		if (err) {
			dev_err(se_dev->dev, "%s(): Failed to copy aad_buf: %d\n",
			__func__, err);
			goto exit;
		}
	}

	if (gmac_ctx->request_type == TEGRA_HV_VSE_GMAC_SIGN) {
		tag = tegra_hv_vse_get_dma_buf(gmac_ctx->node_id,
				AES_TAG_BUF_IDX, gmac_ctx->authsize);
		if (!tag) {
			dev_err(se_dev->dev, "%s tag buf is NULL\n", __func__);
			err = -ENOMEM;
			goto exit;
		}
	}

	memset(ivc_req_msg, 0, sizeof(struct tegra_virtual_se_ivc_msg_t));
	ivc_tx = &ivc_req_msg->tx[0];
	ivc_hdr = &ivc_req_msg->ivc_hdr;
	ivc_hdr->num_reqs = 1;
	ivc_hdr->header_magic[0] = 'N';
	ivc_hdr->header_magic[1] = 'V';
	ivc_hdr->header_magic[2] = 'D';
	ivc_hdr->header_magic[3] = 'A';
	ivc_hdr->engine = g_crypto_to_ivc_map[gmac_ctx->node_id].engine_id;
	ivc_hdr->tag.priv_data = priv;

	priv->cmd = VIRTUAL_SE_PROCESS;
	priv->se_dev = se_dev;

	if (gmac_ctx->request_type == TEGRA_HV_VSE_GMAC_SIGN)
		ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_AES_GMAC_CMD_SIGN;
	else
		ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_AES_GMAC_CMD_VERIFY;

	memcpy(ivc_tx->aes.op_gcm.keyslot, gmac_ctx->key_slot, KEYSLOT_SIZE_BYTES);
	ivc_tx->aes.op_gcm.aad_buf_size = gmac_ctx->user_aad_buf_size;
	ivc_tx->aes.op_gcm.aad_addr = (u32)(aad->buf_iova & U32_MAX);

	if (gmac_ctx->request_type == TEGRA_HV_VSE_GMAC_SIGN) {
		ivc_tx->aes.op_gcm.tag_buf_size = gmac_ctx->authsize;
		ivc_tx->aes.op_gcm.tag_addr = (u32)(tag->buf_iova & U32_MAX);
	}

	if (gmac_ctx->is_first)
		ivc_tx->aes.op_gcm.config |=
					(1 << TEGRA_VIRTUAL_SE_AES_GMAC_SV_CFG_FIRST_REQ_SHIFT);

	if (is_last == true) {
		ivc_tx->aes.op_gcm.config |= (1 << TEGRA_VIRTUAL_SE_AES_GMAC_SV_CFG_LAST_REQ_SHIFT);

		if (gmac_ctx->request_type == TEGRA_HV_VSE_GMAC_VERIFY) {
			memcpy(ivc_tx->aes.op_gcm.iv, gmac_ctx->iv,
								TEGRA_VIRTUAL_SE_AES_GCM_IV_SIZE);
			if (gmac_ctx->authsize > 0) {
				err = copy_from_user(ivc_tx->aes.op_gcm.expected_tag,
							gmac_ctx->user_tag_buf, gmac_ctx->authsize);
				if (err) {
					dev_err(se_dev->dev, "%s(): Failed to copy mac_buf: %d\n",
					__func__, err);
					goto exit;
				}
			}
		}
	}

	g_crypto_to_ivc_map[gmac_ctx->node_id].vse_thread_start = true;
	init_completion(&priv->alg_complete);

	err = tegra_hv_vse_safety_send_ivc_wait(se_dev, pivck, priv, ivc_req_msg,
		sizeof(struct tegra_virtual_se_ivc_msg_t), gmac_ctx->node_id);
	if (err) {
		dev_err(se_dev->dev, "failed to send data over ivc err %d\n", err);
		goto exit;
	}

	if (priv->rx_status != 0) {
		dev_err(se_dev->dev, "%s: SE server returned error %u\n", __func__,
				priv->rx_status);
		err = status_to_errno(priv->rx_status);
		goto exit;
	} else {
		if (is_last && gmac_ctx->request_type == TEGRA_HV_VSE_GMAC_SIGN) {
			/* copy tag to req for last GMAC_SIGN requests */
			if (gmac_ctx->authsize > 0) {
				ret = copy_to_user(gmac_ctx->user_tag_buf, tag->buf_ptr,
					gmac_ctx->authsize);
				if (ret) {
					dev_err(se_dev->dev, "%s(): Failed to copy mac_buf\n",
					__func__);
					err = -EFAULT;
					goto exit;
				}
			}
		}
	}

	if (is_last && gmac_ctx->request_type == TEGRA_HV_VSE_GMAC_VERIFY) {
		ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_AES_CMD_GET_GMAC_VERIFY;
		init_completion(&priv->alg_complete);
		err = tegra_hv_vse_safety_send_ivc_wait(se_dev, pivck, priv, ivc_req_msg,
				sizeof(struct tegra_virtual_se_ivc_msg_t), gmac_ctx->node_id);
		if (err) {
			dev_err(se_dev->dev, "failed to send data over ivc err %d\n", err);
			goto exit;
		}

		if (priv->rx_status != 0) {
			if (priv->rx_status == TEGRA_VIRTUAL_SE_ERR_MAC_INVALID) {
				dev_info(se_dev->dev, "%s: tag mismatch", __func__);
				gmac_ctx->result = 1;
			} else
				err = status_to_errno(priv->rx_status);
		} else {
			gmac_ctx->result = 0;
		}
	}

exit:
	return err;
}

static int tegra_hv_vse_aes_gmac_sv_op_hw_support(struct ahash_request *req,
		struct tegra_virtual_se_aes_gmac_context *gmac_ctx, bool is_last)
{
	struct tegra_virtual_se_dev *se_dev;
	struct tegra_virtual_se_ivc_msg_t *ivc_req_msg =
			g_crypto_to_ivc_map[gmac_ctx->node_id].ivc_msg;
	struct tegra_virtual_se_ivc_hdr_t *ivc_hdr;
	struct tegra_virtual_se_ivc_tx_msg_t *ivc_tx;
	struct tegra_hv_ivc_cookie *pivck;
	struct tegra_vse_priv_data *priv = g_crypto_to_ivc_map[gmac_ctx->node_id].priv;
	int err = 0;
	uint64_t ret = 0;
	u32 match_code = SE_HW_VALUE_MATCH_CODE;
	u32 mismatch_code = SE_HW_VALUE_MISMATCH_CODE;
	const struct tegra_vse_dma_buf *aad, *tag, *comp;
	dma_addr_t aad_addr = 0UL;
	dma_addr_t tag_addr = 0UL;
	bool is_zero_copy;

	se_dev = g_crypto_to_ivc_map[gmac_ctx->node_id].se_dev;
	pivck = g_crypto_to_ivc_map[gmac_ctx->node_id].ivck;
	is_zero_copy = g_crypto_to_ivc_map[gmac_ctx->node_id].is_zero_copy_node;
	err = tegra_vse_aes_gmac_sv_check_params(req, is_last);
	if (err != 0)
		goto exit;

	if (!is_zero_copy) {
		aad = tegra_hv_vse_get_dma_buf(gmac_ctx->node_id, AES_AAD_BUF_IDX,
			gmac_ctx->user_aad_buf_size);
		if (!aad) {
			dev_err(se_dev->dev, "%s aad buf is NULL\n", __func__);
			return -ENOMEM;
		}
		if (gmac_ctx->user_aad_buf_size > 0) {
			err = copy_from_user(aad->buf_ptr, gmac_ctx->user_aad_buf,
				gmac_ctx->user_aad_buf_size);
			if (err) {
				dev_err(se_dev->dev, "%s(): Failed to copy aad_buf: %d\n",
				__func__, err);
				goto exit;
			}
		}
		aad_addr = aad->buf_iova;

		if (gmac_ctx->request_type == TEGRA_HV_VSE_GMAC_SIGN) {
			tag = tegra_hv_vse_get_dma_buf(gmac_ctx->node_id,
					AES_TAG_BUF_IDX, gmac_ctx->authsize);
			if (!tag) {
				dev_err(se_dev->dev, "%s tag buf is NULL\n", __func__);
				return -ENOMEM;
			}
			tag_addr = tag->buf_iova;
		}
	} else {
		if (g_node_dma[gmac_ctx->node_id].mapped_membuf_count == 0U) {
			dev_err(se_dev->dev, "%s no mapped membuf found\n", __func__);
			return -ENOMEM;
		}

		aad_addr = gmac_ctx->user_aad_iova;
		if (gmac_ctx->request_type == TEGRA_HV_VSE_GMAC_SIGN)
			tag_addr = gmac_ctx->user_tag_iova;
	}

	if (gmac_ctx->request_type == TEGRA_HV_VSE_GMAC_VERIFY) {
		comp = tegra_hv_vse_get_dma_buf(gmac_ctx->node_id, AES_COMP_BUF_IDX,
							RESULT_COMPARE_BUF_SIZE);
		if (!comp) {
			dev_err(se_dev->dev, "%s mac comp buf is NULL\n", __func__);
			return -ENOMEM;
		}
	}

	memset(ivc_req_msg, 0, sizeof(struct tegra_virtual_se_ivc_msg_t));
	ivc_tx = &ivc_req_msg->tx[0];
	ivc_hdr = &ivc_req_msg->ivc_hdr;
	ivc_hdr->num_reqs = 1;
	ivc_hdr->header_magic[0] = 'N';
	ivc_hdr->header_magic[1] = 'V';
	ivc_hdr->header_magic[2] = 'D';
	ivc_hdr->header_magic[3] = 'A';
	ivc_hdr->engine = g_crypto_to_ivc_map[gmac_ctx->node_id].engine_id;
	ivc_hdr->tag.priv_data = priv;

	priv->cmd = VIRTUAL_SE_PROCESS;
	priv->se_dev = se_dev;

	if (gmac_ctx->request_type == TEGRA_HV_VSE_GMAC_SIGN)
		ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_AES_GMAC_CMD_SIGN;
	else
		ivc_tx->cmd = TEGRA_VIRTUAL_SE_CMD_AES_GMAC_CMD_VERIFY;

	memcpy(ivc_tx->aes.op_gcm.keyslot, gmac_ctx->key_slot, KEYSLOT_SIZE_BYTES);
	ivc_tx->aes.op_gcm.key_instance = gmac_ctx->key_instance_idx;
	ivc_tx->aes.op_gcm.release_keyslot = gmac_ctx->release_key_flag;
	ivc_tx->aes.op_gcm.aad_buf_size = gmac_ctx->user_aad_buf_size;
	ivc_tx->aes.op_gcm.aad_addr = aad_addr;

	if (gmac_ctx->request_type == TEGRA_HV_VSE_GMAC_SIGN) {
		ivc_tx->aes.op_gcm.tag_buf_size = gmac_ctx->authsize;
		ivc_tx->aes.op_gcm.tag_addr = tag_addr;
	}

	if (gmac_ctx->is_first)
		ivc_tx->aes.op_gcm.config |=
					(1 << TEGRA_VIRTUAL_SE_AES_GMAC_SV_CFG_FIRST_REQ_SHIFT);

	if (is_last == true) {
		ivc_tx->aes.op_gcm.config |= (1 << TEGRA_VIRTUAL_SE_AES_GMAC_SV_CFG_LAST_REQ_SHIFT);

		if (gmac_ctx->request_type == TEGRA_HV_VSE_GMAC_VERIFY) {
			memcpy(ivc_tx->aes.op_gcm.iv, gmac_ctx->iv,
								TEGRA_VIRTUAL_SE_AES_GCM_IV_SIZE);
			if (gmac_ctx->authsize > 0) {
				err = copy_from_user(ivc_tx->aes.op_gcm.expected_tag,
							gmac_ctx->user_tag_buf, gmac_ctx->authsize);
				if (err) {
					dev_err(se_dev->dev, "%s(): Failed to copy tag_buf: %d\n",
					__func__, err);
					goto exit;
				}
			}
			ivc_tx->aes.op_gcm.gcm_vrfy_res_addr = comp->buf_iova;
		}
	}

	if (gmac_ctx->b_is_sm4 == 1U)
		ivc_tx->aes.op_gcm.sym_ciph = VSE_SYM_CIPH_SM4;
	else
		ivc_tx->aes.op_gcm.sym_ciph = VSE_SYM_CIPH_AES;

	g_crypto_to_ivc_map[gmac_ctx->node_id].vse_thread_start = true;
	init_completion(&priv->alg_complete);

	err = tegra_hv_vse_safety_send_ivc_wait(se_dev, pivck, priv, ivc_req_msg,
		sizeof(struct tegra_virtual_se_ivc_msg_t), gmac_ctx->node_id);
	if (err) {
		dev_err(se_dev->dev, "failed to send data over ivc err %d\n", err);
		goto exit;
	}

	if (priv->rx_status != 0) {
		dev_err(se_dev->dev, "%s: SE server returned error %u\n", __func__,
				priv->rx_status);
		err = status_to_errno(priv->rx_status);
		goto exit;
	}

	if (is_last) {
		if (gmac_ctx->request_type == TEGRA_HV_VSE_GMAC_SIGN) {
			/* copy tag to req for last GMAC_SIGN requests */
			if (!is_zero_copy && (gmac_ctx->authsize > 0)) {
				ret = copy_to_user(gmac_ctx->user_tag_buf, tag->buf_ptr,
					gmac_ctx->authsize);
				if (ret) {
					dev_err(se_dev->dev, "%s(): Failed to copy mac_buf\n",
					__func__);
					err = -EFAULT;
					goto exit;
				}
			}
		} else {
			if (memcmp(comp->buf_ptr, &match_code, 4) == 0)
				gmac_ctx->result = 0;
			else if (memcmp(comp->buf_ptr, &mismatch_code, 4) == 0) {
				dev_info(se_dev->dev, "%s: tag mismatch", __func__);
				gmac_ctx->result = 1;
			} else {
				dev_err(se_dev->dev, "%s: invalid tag match code", __func__);
				err = -EINVAL;
			}
		}
	}

exit:
	return err;
}

static int tegra_hv_vse_aes_gmac_sv_update(struct ahash_request *req)
{
	struct tegra_virtual_se_aes_gmac_context *gmac_ctx = NULL;
	struct tegra_virtual_se_dev *se_dev;
	int ret = 0;

	if (!req) {
		VSE_ERR("%s: request not valid\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	ret = tegra_vse_aes_gmac_sv_check_params(req, false);
	if (ret != 0) {
		VSE_ERR("%s: Invalid params\n", __func__);
		goto exit;
	}

	gmac_ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	if (gmac_ctx == NULL) {
		VSE_ERR("%s: gmac_ctx is NULL\n", __func__);
		return -EINVAL;
	}

	if (!gmac_ctx->req_context_initialized) {
		VSE_ERR("%s Request ctx not initialized\n", __func__);
		ret = -EPERM;
		goto exit;
	}

	se_dev = g_crypto_to_ivc_map[gmac_ctx->node_id].se_dev;

	/* Return error if engine is in suspended state */
	if (atomic_read(&se_dev->se_suspended)) {
		dev_err(se_dev->dev, "%s: engine is in suspended state\n", __func__);
		ret = -ENODEV;
		goto exit;
	}
	if (se_dev->chipdata->gcm_hw_iv_supported)
		ret = tegra_hv_vse_aes_gmac_sv_op_hw_support(req, gmac_ctx, false);
	else
		ret = tegra_hv_vse_aes_gmac_sv_op(req, gmac_ctx, false);
	if (ret)
		dev_err(se_dev->dev, "%s failed %d\n", __func__, ret);

exit:
	return ret;
}

static int tegra_hv_vse_aes_gmac_sv_finup(struct ahash_request *req)
{
	struct tegra_virtual_se_aes_gmac_context *gmac_ctx = NULL;
	struct tegra_virtual_se_dev *se_dev;
	int ret = 0;

	if (!req) {
		VSE_ERR("%s: request not valid\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	ret = tegra_vse_aes_gmac_sv_check_params(req, true);
	if (ret != 0) {
		VSE_ERR("%s: Invalid params\n", __func__);
		goto exit;
	}

	gmac_ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	if (gmac_ctx == NULL) {
		VSE_ERR("%s: gmac_ctx is NULL\n", __func__);
		return -EINVAL;
	}

	if (!gmac_ctx->req_context_initialized) {
		VSE_ERR("%s: Request ctx not initialized\n", __func__);
		ret = -EPERM;
		goto exit;
	}

	se_dev = g_crypto_to_ivc_map[gmac_ctx->node_id].se_dev;

	/* Return error if engine is in suspended state */
	if (atomic_read(&se_dev->se_suspended)) {
		dev_err(se_dev->dev, "%s: engine is in suspended state\n", __func__);
		ret = -ENODEV;
		goto exit;
	}
	if (se_dev->chipdata->gcm_hw_iv_supported)
		ret = tegra_hv_vse_aes_gmac_sv_op_hw_support(req, gmac_ctx, true);
	else
		ret = tegra_hv_vse_aes_gmac_sv_op(req, gmac_ctx, true);
	if (ret)
		dev_err(se_dev->dev, "%s failed %d\n", __func__, ret);

	tegra_hv_vse_aes_gmac_deinit(req);

exit:
	return ret;
}

static int tegra_hv_vse_aes_gmac_sv_final(struct ahash_request *req)
{
	struct tegra_virtual_se_aes_gmac_context *gmac_ctx =
					crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	struct tegra_virtual_se_dev *se_dev =
				g_crypto_to_ivc_map[gmac_ctx->node_id].se_dev;

	dev_err(se_dev->dev, "%s: final not supported", __func__);
	return -EPERM;
}

#define HV_SAFETY_AES_CTX_SIZE sizeof(struct tegra_virtual_se_aes_context)

static struct rng_alg rng_alg = {
	.generate = tegra_hv_vse_safety_rng_drbg_get_random,
	.seed = tegra_hv_vse_safety_rng_drbg_reset,
	.seedsize = TEGRA_VIRTUAL_SE_RNG_SEED_SIZE,
	.base = {
		.cra_name = "rng_drbg",
		.cra_driver_name = "rng_drbg-aes-tegra",
		.cra_priority = 100,
		.cra_flags = CRYPTO_ALG_TYPE_RNG,
		.cra_ctxsize = sizeof(struct tegra_virtual_se_rng_context),
		.cra_module = THIS_MODULE,
		.cra_init = tegra_hv_vse_safety_rng_drbg_init,
		.cra_exit = tegra_hv_vse_safety_rng_drbg_exit,
	}
};

static struct aead_alg aead_algs[] = {
	{
		.setkey		= tegra_vse_aes_gcm_setkey,
		.setauthsize	= tegra_vse_aes_gcm_setauthsize,
		.encrypt	= tegra_vse_aes_gcm_encrypt,
		.decrypt	= tegra_vse_aes_gcm_decrypt,
		.init		= tegra_vse_aes_gcm_init,
		.exit		= tegra_vse_aes_gcm_exit,
		.ivsize		= TEGRA_VIRTUAL_SE_AES_GCM_IV_SIZE,
		.maxauthsize	= TEGRA_VIRTUAL_SE_AES_GCM_TAG_SIZE,
		.chunksize	= TEGRA_VIRTUAL_SE_AES_BLOCK_SIZE,
		.base = {
			.cra_name	= "gcm-vse(aes)",
			.cra_driver_name = "gcm-aes-tegra-safety",
			.cra_priority	= 1000,
			.cra_blocksize	= TEGRA_VIRTUAL_SE_AES_BLOCK_SIZE,
			.cra_ctxsize	= HV_SAFETY_AES_CTX_SIZE,
			.cra_module	= THIS_MODULE,
		}
	}
};

static struct skcipher_alg aes_algs[] = {
	{
		.base.cra_name		= "cbc-vse(aes)",
		.base.cra_driver_name	= "cbc-aes-tegra",
		.base.cra_priority	= 400,
		.base.cra_flags		= CRYPTO_ALG_TYPE_SKCIPHER |
					  CRYPTO_ALG_ASYNC,
		.base.cra_blocksize	= TEGRA_VIRTUAL_SE_AES_BLOCK_SIZE,
		.base.cra_ctxsize	= HV_SAFETY_AES_CTX_SIZE,
		.base.cra_alignmask	= 0,
		.base.cra_module	= THIS_MODULE,
		.init			= tegra_hv_vse_safety_aes_cra_init,
		.exit			= tegra_hv_vse_safety_aes_cra_exit,
		.setkey			= tegra_hv_vse_safety_aes_setkey,
		.encrypt		= tegra_hv_vse_safety_aes_cbc_encrypt,
		.decrypt		= tegra_hv_vse_safety_aes_cbc_decrypt,
		.min_keysize		= TEGRA_VIRTUAL_SE_AES_MIN_KEY_SIZE,
		.max_keysize		= TEGRA_VIRTUAL_SE_AES_MAX_KEY_SIZE,
		.ivsize			= TEGRA_VIRTUAL_SE_AES_IV_SIZE,
	},
	{
		.base.cra_name		= "ctr-vse(aes)",
		.base.cra_driver_name	= "ctr-aes-tegra-safety",
		.base.cra_priority	= 400,
		.base.cra_flags		= CRYPTO_ALG_TYPE_SKCIPHER |
					  CRYPTO_ALG_ASYNC,
		.base.cra_blocksize	= TEGRA_VIRTUAL_SE_AES_BLOCK_SIZE,
		.base.cra_ctxsize	= HV_SAFETY_AES_CTX_SIZE,
		.base.cra_alignmask	= 0,
		.base.cra_module	= THIS_MODULE,
		.init			= tegra_hv_vse_safety_aes_cra_init,
		.exit			= tegra_hv_vse_safety_aes_cra_exit,
		.setkey			= tegra_hv_vse_safety_aes_setkey,
		.encrypt		= tegra_hv_vse_safety_aes_ctr_encrypt,
		.decrypt		= tegra_hv_vse_safety_aes_ctr_decrypt,
		.min_keysize		= TEGRA_VIRTUAL_SE_AES_MIN_KEY_SIZE,
		.max_keysize		= TEGRA_VIRTUAL_SE_AES_MAX_KEY_SIZE,
		.ivsize			= TEGRA_VIRTUAL_SE_AES_IV_SIZE,
	},
};

static struct ahash_alg tsec_alg = {
	.init = tegra_hv_vse_safety_cmac_init,
	.update = tegra_hv_tsec_safety_cmac_update,
	.final = tegra_hv_vse_safety_cmac_final,
	.finup = tegra_hv_tsec_safety_cmac_finup,
	.digest = tegra_hv_vse_safety_cmac_digest,
	.setkey = tegra_hv_vse_safety_cmac_setkey,
	.halg.digestsize = TEGRA_VIRTUAL_SE_AES_CMAC_DIGEST_SIZE,
	.halg.statesize = TEGRA_VIRTUAL_SE_AES_CMAC_STATE_SIZE,
	.halg.base = {
		.cra_name = "cmac-tsec(aes)",
		.cra_driver_name = "tegra-hv-vse-safety-tsec(aes)",
		.cra_priority = 400,
		.cra_flags = CRYPTO_ALG_TYPE_AHASH,
		.cra_blocksize = TEGRA_VIRTUAL_SE_AES_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct tegra_virtual_se_aes_cmac_context),
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
		.cra_init = tegra_hv_vse_safety_cmac_cra_init,
		.cra_exit = tegra_hv_vse_safety_cmac_cra_exit,
	}
};

static struct ahash_alg cmac_alg = {
	.init = tegra_hv_vse_safety_cmac_init,
	.update = tegra_hv_vse_safety_cmac_update,
	.final = tegra_hv_vse_safety_cmac_final,
	.finup = tegra_hv_vse_safety_cmac_finup,
	.digest = tegra_hv_vse_safety_cmac_digest,
	.setkey = tegra_hv_vse_safety_cmac_setkey,
	.halg.digestsize = TEGRA_VIRTUAL_SE_AES_CMAC_DIGEST_SIZE,
	.halg.statesize = TEGRA_VIRTUAL_SE_AES_CMAC_STATE_SIZE,
	.halg.base = {
		.cra_name = "cmac-vse(aes)",
		.cra_driver_name = "tegra-hv-vse-safety-cmac(aes)",
		.cra_priority = 400,
		.cra_flags = CRYPTO_ALG_TYPE_AHASH,
		.cra_blocksize = TEGRA_VIRTUAL_SE_AES_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct tegra_virtual_se_aes_cmac_context),
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
		.cra_init = tegra_hv_vse_safety_cmac_cra_init,
		.cra_exit = tegra_hv_vse_safety_cmac_cra_exit,
	}
};

static struct ahash_alg gmac_alg = {
	.init = tegra_hv_vse_aes_gmac_sv_init,
	.update = tegra_hv_vse_aes_gmac_sv_update,
	.finup = tegra_hv_vse_aes_gmac_sv_finup,
	.final = tegra_hv_vse_aes_gmac_sv_final,
	.setkey = tegra_hv_vse_aes_gmac_setkey,
	.halg.digestsize = TEGRA_VIRTUAL_SE_AES_GCM_TAG_SIZE,
	.halg.statesize = TEGRA_VIRTUAL_SE_AES_GCM_TAG_SIZE,
	.halg.base = {
		.cra_name = "gmac-vse(aes)",
		.cra_driver_name = "tegra-hv-vse-gmac(aes)",
		.cra_priority = 400,
		.cra_flags = CRYPTO_ALG_TYPE_AHASH,
		.cra_blocksize = TEGRA_VIRTUAL_SE_AES_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct tegra_virtual_se_aes_gmac_context),
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
		.cra_init = tegra_hv_vse_safety_gmac_cra_init,
		.cra_exit = tegra_hv_vse_safety_gmac_cra_exit,
	}
};

static struct ahash_alg sha_algs[] = {
	{
		.init = tegra_hv_vse_safety_sha_init,
		.update = tegra_hv_vse_safety_sha_update,
		.final = tegra_hv_vse_safety_sha_final,
		.finup = tegra_hv_vse_safety_sha_finup,
		.digest = tegra_hv_vse_safety_sha_digest,
		.export = tegra_hv_vse_safety_sha_export,
		.import = tegra_hv_vse_safety_sha_import,
		.halg.digestsize = SHA256_DIGEST_SIZE,
		.halg.statesize = sizeof(struct tegra_virtual_se_req_context),
		.halg.base = {
			.cra_name = "sha256-vse",
			.cra_driver_name = "tegra-hv-vse-safety-sha256",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_TYPE_AHASH,
			.cra_blocksize = SHA256_BLOCK_SIZE,
			.cra_ctxsize =
				sizeof(struct tegra_virtual_se_sha_context),
			.cra_alignmask = 0,
			.cra_module = THIS_MODULE,
			.cra_init = tegra_hv_vse_safety_sha_cra_init,
			.cra_exit = tegra_hv_vse_safety_sha_cra_exit,
		}
	}, {
		.init = tegra_hv_vse_safety_sha_init,
		.update = tegra_hv_vse_safety_sha_update,
		.final = tegra_hv_vse_safety_sha_final,
		.finup = tegra_hv_vse_safety_sha_finup,
		.digest = tegra_hv_vse_safety_sha_digest,
		.export = tegra_hv_vse_safety_sha_export,
		.import = tegra_hv_vse_safety_sha_import,
		.halg.digestsize = SHA384_DIGEST_SIZE,
		.halg.statesize = sizeof(struct tegra_virtual_se_req_context),
		.halg.base = {
			.cra_name = "sha384-vse",
			.cra_driver_name = "tegra-hv-vse-safety-sha384",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_TYPE_AHASH,
			.cra_blocksize = SHA384_BLOCK_SIZE,
			.cra_ctxsize =
				sizeof(struct tegra_virtual_se_sha_context),
			.cra_alignmask = 0,
			.cra_module = THIS_MODULE,
			.cra_init = tegra_hv_vse_safety_sha_cra_init,
			.cra_exit = tegra_hv_vse_safety_sha_cra_exit,
		}
	}, {
		.init = tegra_hv_vse_safety_sha_init,
		.update = tegra_hv_vse_safety_sha_update,
		.final = tegra_hv_vse_safety_sha_final,
		.finup = tegra_hv_vse_safety_sha_finup,
		.digest = tegra_hv_vse_safety_sha_digest,
		.export = tegra_hv_vse_safety_sha_export,
		.import = tegra_hv_vse_safety_sha_import,
		.halg.digestsize = SHA512_DIGEST_SIZE,
		.halg.statesize = sizeof(struct tegra_virtual_se_req_context),
		.halg.base = {
			.cra_name = "sha512-vse",
			.cra_driver_name = "tegra-hv-vse-safety-sha512",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_TYPE_AHASH,
			.cra_blocksize = SHA512_BLOCK_SIZE,
			.cra_ctxsize =
				sizeof(struct tegra_virtual_se_sha_context),
			.cra_alignmask = 0,
			.cra_module = THIS_MODULE,
			.cra_init = tegra_hv_vse_safety_sha_cra_init,
			.cra_exit = tegra_hv_vse_safety_sha_cra_exit,
		}
	}, {
		.init = tegra_hv_vse_safety_sha_init,
		.update = tegra_hv_vse_safety_sha_update,
		.final = tegra_hv_vse_safety_sha_final,
		.finup = tegra_hv_vse_safety_sha_finup,
		.digest = tegra_hv_vse_safety_sha_digest,
		.export = tegra_hv_vse_safety_sha_export,
		.import = tegra_hv_vse_safety_sha_import,
		.halg.digestsize = SHA3_256_DIGEST_SIZE,
		.halg.statesize = sizeof(struct tegra_virtual_se_req_context),
		.halg.base = {
			.cra_name = "sha3-256-vse",
			.cra_driver_name = "tegra-hv-vse-safety-sha3-256",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_TYPE_AHASH,
			.cra_blocksize = SHA3_256_BLOCK_SIZE,
			.cra_ctxsize =
				sizeof(struct tegra_virtual_se_sha_context),
			.cra_alignmask = 0,
			.cra_module = THIS_MODULE,
			.cra_init = tegra_hv_vse_safety_sha_cra_init,
			.cra_exit = tegra_hv_vse_safety_sha_cra_exit,
		}
	}, {
		.init = tegra_hv_vse_safety_sha_init,
		.update = tegra_hv_vse_safety_sha_update,
		.final = tegra_hv_vse_safety_sha_final,
		.finup = tegra_hv_vse_safety_sha_finup,
		.digest = tegra_hv_vse_safety_sha_digest,
		.export = tegra_hv_vse_safety_sha_export,
		.import = tegra_hv_vse_safety_sha_import,
		.halg.digestsize = SHA3_384_DIGEST_SIZE,
		.halg.statesize = sizeof(struct tegra_virtual_se_req_context),
		.halg.base = {
			.cra_name = "sha3-384-vse",
			.cra_driver_name = "tegra-hv-vse-safety-sha3-384",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_TYPE_AHASH,
			.cra_blocksize = SHA3_384_BLOCK_SIZE,
			.cra_ctxsize =
				sizeof(struct tegra_virtual_se_sha_context),
			.cra_alignmask = 0,
			.cra_module = THIS_MODULE,
			.cra_init = tegra_hv_vse_safety_sha_cra_init,
			.cra_exit = tegra_hv_vse_safety_sha_cra_exit,
		}
	}, {
		.init = tegra_hv_vse_safety_sha_init,
		.update = tegra_hv_vse_safety_sha_update,
		.final = tegra_hv_vse_safety_sha_final,
		.finup = tegra_hv_vse_safety_sha_finup,
		.digest = tegra_hv_vse_safety_sha_digest,
		.export = tegra_hv_vse_safety_sha_export,
		.import = tegra_hv_vse_safety_sha_import,
		.halg.digestsize = SHA3_512_DIGEST_SIZE,
		.halg.statesize = sizeof(struct tegra_virtual_se_req_context),
		.halg.base = {
			.cra_name = "sha3-512-vse",
			.cra_driver_name = "tegra-hv-vse-safety-sha3-512",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_TYPE_AHASH,
			.cra_blocksize = SHA3_512_BLOCK_SIZE,
			.cra_ctxsize =
				sizeof(struct tegra_virtual_se_sha_context),
			.cra_alignmask = 0,
			.cra_module = THIS_MODULE,
			.cra_init = tegra_hv_vse_safety_sha_cra_init,
			.cra_exit = tegra_hv_vse_safety_sha_cra_exit,
		}
	}, {
		.init = tegra_hv_vse_safety_sha_init,
		.update = tegra_hv_vse_safety_sha_update,
		.final = tegra_hv_vse_safety_sha_final,
		.finup = tegra_hv_vse_safety_sha_finup,
		.digest = tegra_hv_vse_safety_sha_digest,
		.export = tegra_hv_vse_safety_sha_export,
		.import = tegra_hv_vse_safety_sha_import,
		.halg.digestsize = SHA3_512_DIGEST_SIZE,
		.halg.statesize = sizeof(struct tegra_virtual_se_req_context),
		.halg.base = {
			.cra_name = "shake128-vse",
			.cra_driver_name = "tegra-hv-vse-safety-shake128",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_TYPE_AHASH,
			.cra_blocksize = SHA3_512_BLOCK_SIZE,
			.cra_ctxsize =
				sizeof(struct tegra_virtual_se_sha_context),
			.cra_alignmask = 0,
			.cra_module = THIS_MODULE,
			.cra_init = tegra_hv_vse_safety_sha_cra_init,
			.cra_exit = tegra_hv_vse_safety_sha_cra_exit,
		}
	}, {
		.init = tegra_hv_vse_safety_sha_init,
		.update = tegra_hv_vse_safety_sha_update,
		.final = tegra_hv_vse_safety_sha_final,
		.finup = tegra_hv_vse_safety_sha_finup,
		.digest = tegra_hv_vse_safety_sha_digest,
		.export = tegra_hv_vse_safety_sha_export,
		.import = tegra_hv_vse_safety_sha_import,
		.halg.digestsize = SHA3_512_DIGEST_SIZE,
		.halg.statesize = sizeof(struct tegra_virtual_se_req_context),
		.halg.base = {
			.cra_name = "shake256-vse",
			.cra_driver_name = "tegra-hv-vse-safety-shake256",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_TYPE_AHASH,
			.cra_blocksize = SHA3_512_BLOCK_SIZE,
			.cra_ctxsize =
				sizeof(struct tegra_virtual_se_sha_context),
			.cra_alignmask = 0,
			.cra_module = THIS_MODULE,
			.cra_init = tegra_hv_vse_safety_sha_cra_init,
			.cra_exit = tegra_hv_vse_safety_sha_cra_exit,
		}
	}, {
		.init = tegra_hv_vse_safety_sha_init,
		.update = tegra_hv_vse_safety_sha_update,
		.final = tegra_hv_vse_safety_sha_final,
		.finup = tegra_hv_vse_safety_sha_finup,
		.digest = tegra_hv_vse_safety_sha_digest,
		.export = tegra_hv_vse_safety_sha_export,
		.import = tegra_hv_vse_safety_sha_import,
		.halg.digestsize = SM3_DIGEST_SIZE,
		.halg.statesize = sizeof(struct tegra_virtual_se_req_context),
		.halg.base = {
			.cra_name = "sm3-vse",
			.cra_driver_name = "tegra-hv-vse-safety-sm3",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_TYPE_AHASH,
			.cra_blocksize = SM3_BLOCK_SIZE,
			.cra_ctxsize =
				sizeof(struct tegra_virtual_se_sha_context),
			.cra_alignmask = 0,
			.cra_module = THIS_MODULE,
			.cra_init = tegra_hv_vse_safety_sha_cra_init,
			.cra_exit = tegra_hv_vse_safety_sha_cra_exit,
		}
	}, {
		.init = tegra_hv_vse_safety_hmac_sha_init,
		.update = tegra_hv_vse_safety_hmac_sha_update,
		.final = tegra_hv_vse_safety_hmac_sha_final,
		.finup = tegra_hv_vse_safety_hmac_sha_finup,
		.digest = tegra_hv_vse_safety_hmac_sha_digest,
		.export = tegra_hv_vse_safety_sha_export,
		.import = tegra_hv_vse_safety_sha_import,
		.setkey = tegra_hv_vse_safety_hmac_sha_setkey,
		.halg.digestsize = SHA256_DIGEST_SIZE,
		.halg.statesize = sizeof(struct tegra_virtual_se_req_context),
		.halg.base = {
			.cra_name = "hmac-sha256-vse",
			.cra_driver_name = "tegra-hv-vse-safety-hmac-sha256",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_TYPE_AHASH,
			.cra_blocksize = SHA256_BLOCK_SIZE,
			.cra_ctxsize =
				sizeof(struct tegra_virtual_se_hmac_sha_context),
			.cra_alignmask = 0,
			.cra_module = THIS_MODULE,
			.cra_init = tegra_hv_vse_safety_sha_cra_init,
			.cra_exit = tegra_hv_vse_safety_sha_cra_exit,
		}
	}
};

static const struct tegra_vse_soc_info t234_vse_sinfo = {
	.cmac_hw_verify_supported = false,
	.sm_supported = false,
	.gcm_hw_iv_supported = false,
	.hmac_verify_hw_support = false,
	.zero_copy_supported = false,
	.allocate_key_slot_supported = false,
};

static const struct tegra_vse_soc_info se_51_vse_sinfo = {
	.cmac_hw_verify_supported = true,
	.sm_supported = true,
	.gcm_hw_iv_supported = true,
	.hmac_verify_hw_support = true,
	.zero_copy_supported = true,
	.allocate_key_slot_supported = true,
};

static const struct of_device_id tegra_hv_vse_safety_of_match[] = {
	{ .compatible = "nvidia,tegra234-hv-vse-safety", .data = &t234_vse_sinfo, },
	{ .compatible = "nvidia,tegra-se-5.1-hv-vse-safety", .data = &se_51_vse_sinfo, },
	{},
};
MODULE_DEVICE_TABLE(of, tegra_hv_vse_safety_of_match);

static irqreturn_t tegra_vse_irq_handler(int irq, void *data)
{
	uint32_t node_id = *((uint32_t *)data);
	struct tegra_hv_ivc_cookie *ivck = g_crypto_to_ivc_map[node_id].ivck;

	if (tegra_hv_ivc_can_read(ivck))
		complete(&g_crypto_to_ivc_map[node_id].tegra_vse_complete);
	return IRQ_HANDLED;
}

static int tegra_vse_kthread(void *data)
{
	uint32_t node_id = *((uint32_t *)data);
	struct tegra_virtual_se_dev *se_dev = NULL;
	struct tegra_hv_ivc_cookie *pivck = g_crypto_to_ivc_map[node_id].ivck;
	int err = 0;
	int timeout;
	int ret;
	bool is_dummy = false;
	enum ivc_irq_state *irq_state;

	se_dev = g_crypto_to_ivc_map[node_id].se_dev;

	while (!kthread_should_stop()) {
		err = 0;
		ret = wait_for_completion_interruptible(
					&g_crypto_to_ivc_map[node_id].tegra_vse_complete);
		if (ret < 0) {
			VSE_ERR("%s completion err\n", __func__);
			reinit_completion(&g_crypto_to_ivc_map[node_id].tegra_vse_complete);
			continue;
		}

		if (!g_crypto_to_ivc_map[node_id].vse_thread_start) {
			reinit_completion(&g_crypto_to_ivc_map[node_id].tegra_vse_complete);
			continue;
		}
		timeout = TEGRA_VIRTUAL_SE_TIMEOUT_1S;
		while (tegra_hv_ivc_channel_notified(pivck) != 0) {
			if (!timeout) {
				reinit_completion(
					&g_crypto_to_ivc_map[node_id].tegra_vse_complete);
				VSE_ERR("%s:%d ivc channel_notifier timeout\n",
					__func__, __LINE__);
				err = -EAGAIN;
				break;
			}
			udelay(1);
			timeout--;
		}

		if (err == -EAGAIN) {
			err = 0;
			continue;
		}

		mutex_lock(&(se_dev->crypto_to_ivc_map[node_id].irq_state_lock));
		irq_state = &(se_dev->crypto_to_ivc_map[node_id].wait_interrupt);
		while (tegra_hv_ivc_can_read(pivck) && *irq_state != NO_INTERRUPT) {
			pr_debug("%s(): wait_interrupt = %u", __func__, *irq_state);
			if (*irq_state == INTERMEDIATE_REQ_INTERRUPT) {
				err = read_and_validate_valid_msg(se_dev, pivck, node_id,
					&is_dummy, true);
				if (err != 0) {
					dev_err(se_dev->dev,
						"%s(): Unable to read validate message",
						__func__);
				}
				*irq_state = NO_INTERRUPT;
				pr_debug("%s():%d wait_interrupt = %u\n",
						__func__, __LINE__, *irq_state);
				break;

			} else if (*irq_state == FIRST_REQ_INTERRUPT) {
				err = read_and_validate_dummy_msg(se_dev, pivck, node_id,
					&is_dummy);
				if (err != 0) {
					dev_err(se_dev->dev, "%s:%d Invalid response header\n",
						__func__, __LINE__);
					err = 0;
					continue;
				}
				if (is_dummy == true) {
					*irq_state = INTERMEDIATE_REQ_INTERRUPT;
					pr_debug("%s():%d Dummy message read. Read valid message.",
						__func__, __LINE__);
					continue;
				} else {
					dev_err(se_dev->dev, "Invalid response sequence");
					break;
				}
			} else {
				dev_err(se_dev->dev, "Invalid irq state - %u", *irq_state);
				return -EINVAL;
			}
		}
		mutex_unlock(&(se_dev->crypto_to_ivc_map[node_id].irq_state_lock));
	}

	return 0;
}

#if defined(CONFIG_HW_RANDOM)
static int tegra_hv_vse_safety_hwrng_read(struct hwrng *rng, void *buf, size_t size, bool wait)
{
	struct tegra_virtual_se_rng_context *ctx;

	if (!wait)
		return 0;

	ctx = (struct tegra_virtual_se_rng_context *)rng->priv;

	if (size > UINT_MAX) {
		VSE_ERR("%s: size %zu is greater than UINT_MAX\n", __func__, size);
		return -EINVAL;
	}
	return tegra_hv_vse_safety_get_random(ctx, buf, (unsigned int)size, HW_RNG);
}
#endif /* CONFIG_HW_RANDOM */

static int tegra_hv_vse_safety_register_hwrng(struct tegra_virtual_se_dev *se_dev)
{
#if defined(CONFIG_HW_RANDOM)
	int ret;
	struct hwrng *vse_hwrng = NULL;
	struct tegra_virtual_se_rng_context *rng_ctx = NULL;

	vse_hwrng = devm_kzalloc(se_dev->dev, sizeof(*vse_hwrng), GFP_KERNEL);
	if (!vse_hwrng) {
		ret = -ENOMEM;
		goto out;
	}

	rng_ctx = devm_kzalloc(se_dev->dev, sizeof(*rng_ctx), GFP_KERNEL);
	if (!rng_ctx) {
		ret = -ENOMEM;
		goto out;
	}

	rng_ctx->se_dev = se_dev;

	/* To ensure the memory is not overwritten when hwrng req arrives while
	 * AES CBC/CMAC or other AES operations are in progress
	 */
	rng_ctx->hwrng_dma_buf.buf_ptr = dma_alloc_coherent(se_dev->dev, TEGRA_VIRTUAL_SE_RNG_DT_SIZE,
		&rng_ctx->hwrng_dma_buf.buf_iova, GFP_KERNEL);
	if (!rng_ctx->hwrng_dma_buf.buf_ptr)
		return -ENOMEM;

	rng_ctx->priv = devm_kzalloc(se_dev->dev, sizeof(struct tegra_vse_priv_data), GFP_KERNEL);
	if (!rng_ctx->priv) {
		ret = -ENOMEM;
		goto out;
	}

	rng_ctx->ivc_msg = devm_kzalloc(se_dev->dev,
		sizeof(struct tegra_virtual_se_ivc_msg_t), GFP_KERNEL);
	if (!rng_ctx->ivc_msg) {
		ret = -ENOMEM;
		goto out;
	}

	vse_hwrng->name = "tegra_hv_vse_safety";
	vse_hwrng->read = tegra_hv_vse_safety_hwrng_read;
	vse_hwrng->quality = 1024;
	vse_hwrng->priv = (unsigned long)rng_ctx;

	ret = devm_hwrng_register(se_dev->dev, vse_hwrng);
out:
	if (ret) {
		if (rng_ctx) {
			dma_free_coherent(se_dev->dev, TEGRA_VIRTUAL_SE_RNG_DT_SIZE,
			rng_ctx->hwrng_dma_buf.buf_ptr, rng_ctx->hwrng_dma_buf.buf_iova);
			devm_kfree(se_dev->dev, rng_ctx->priv);
			devm_kfree(se_dev->dev, rng_ctx->ivc_msg);
			devm_kfree(se_dev->dev, rng_ctx);
		}
		if (vse_hwrng)
			devm_kfree(se_dev->dev, vse_hwrng);
	} else {
		se_dev->hwrng = vse_hwrng;
	}
	return ret;
#else
	return 0;
#endif /* CONFIG_HW_RANDOM */
}

static void tegra_hv_vse_safety_unregister_hwrng(struct tegra_virtual_se_dev *se_dev)
{
#if defined(CONFIG_HW_RANDOM)
	struct tegra_virtual_se_rng_context *rng_ctx;

	if (se_dev->hwrng) {
		devm_hwrng_unregister(se_dev->dev, se_dev->hwrng);
		rng_ctx = (struct tegra_virtual_se_rng_context *)se_dev->hwrng->priv;

		dma_free_coherent(se_dev->dev, TEGRA_VIRTUAL_SE_RNG_DT_SIZE,
			rng_ctx->hwrng_dma_buf.buf_ptr, rng_ctx->hwrng_dma_buf.buf_iova);

		devm_kfree(se_dev->dev, rng_ctx);
		devm_kfree(se_dev->dev, rng_ctx->ivc_msg);
		devm_kfree(se_dev->dev, se_dev->hwrng);
		se_dev->hwrng = NULL;
	}
#endif /* CONFIG_HW_RANDOM */
}

static const struct of_device_id host1x_match[] = {
	{ .compatible = "nvidia,tegra234-host1x", },
	{ .compatible = "nvidia,tegra264-host1x", },
	{},
};

static int se_get_nvhost_dev(struct tegra_virtual_se_dev *se_dev)
{
	struct platform_device *host1x_pdev;
	struct device_node *np;

	np = of_find_matching_node(NULL, host1x_match);
	if (!np) {
		dev_err(se_dev->dev, "Failed to find host1x, syncpt support disabled");
		return -ENODATA;
	}

	host1x_pdev = of_find_device_by_node(np);
	if (!host1x_pdev) {
		dev_err(se_dev->dev, "host1x device not available");
		return -EPROBE_DEFER;
	}

	se_dev->host1x_pdev = host1x_pdev;

	return 0;
}

static int tegra_vse_validate_ivc_node_id(uint32_t ivc_id, uint32_t instance_id,
	unsigned int engine_id)
{
	uint32_t cnt;

	for (cnt = 0; cnt < MAX_NUMBER_MISC_DEVICES; cnt++) {
		if (g_crypto_to_ivc_map[cnt].node_in_use != true)
			break;

		if (g_crypto_to_ivc_map[cnt].ivc_id == ivc_id) {
			VSE_ERR("%s: ivc id %u is already used\n", __func__, ivc_id);
			return -EINVAL;
		}

		if ((g_crypto_to_ivc_map[cnt].engine_id == engine_id)
				&& (g_crypto_to_ivc_map[cnt].instance_id == instance_id)) {
			VSE_ERR("%s: instance id %u is already used for engine id %d\n", __func__,
					instance_id, engine_id);
			return -EINVAL;
		}
	}

	return 0;
}

static bool tegra_mempool_check_entry(struct tegra_virtual_se_dev *se_dev, uint32_t mempool_id)
{
	uint32_t cnt;

	for (cnt = 0; cnt < MAX_NUMBER_MISC_DEVICES; cnt++) {
		if (g_crypto_to_ivc_map[cnt].mempool.buf_len > 0)
			if (g_crypto_to_ivc_map[cnt].mempool_id == mempool_id)
				return true;
	}
	return false;
}

static int tegra_hv_vse_allocate_gpc_dma_bufs(struct tegra_vse_node_dma *node_dma,
		struct device *gpcdma_dev,
		struct crypto_dev_to_ivc_map *ivc_map)
{
	int32_t err = -ENOMEM;

	if (!node_dma) {
		VSE_ERR("%s node_dma is null\n", __func__);
		err = -EINVAL;
		goto exit;
	}

	if (!gpcdma_dev) {
		VSE_ERR("%s gpcdma_dev is null\n", __func__);
		err = -EINVAL;
		goto exit;
	}

	if (!ivc_map) {
		VSE_ERR("%s ivc_map is null\n", __func__);
		err = -EINVAL;
		goto exit;
	}

	if ((ivc_map->engine_id != VIRTUAL_SE_AES0) && (ivc_map->engine_id != VIRTUAL_SE_AES1)) {
		/* No GPCDMA buffer allocation is needed in case of non AES engines */
		err = 0;
		goto exit;
	}

	if (ivc_map->gcm_dec_buffer_size > 0) {
		node_dma->gpc_dma_buf.buf_ptr = dma_alloc_coherent(gpcdma_dev,
						ALIGN(ivc_map->gcm_dec_buffer_size, 64U),
						&node_dma->gpc_dma_buf.buf_iova, GFP_KERNEL);
		if (!node_dma->gpc_dma_buf.buf_ptr) {
			dev_err(gpcdma_dev, "%s dma_alloc_coherent failed\n", __func__);
			err = -ENOMEM;
			goto exit;
		}
		node_dma->gpcdma_dev = gpcdma_dev;
		node_dma->gpc_dma_buf.buf_len = ivc_map->gcm_dec_buffer_size;
	}

	err = 0;

exit:
	return err;
}

static void tegra_hv_vse_release_gpc_dma_bufs(struct device *gpcdma_dev)
{
	uint32_t i;
	struct tegra_vse_dma_buf *dma_buf = NULL;

	if (!gpcdma_dev) {
		VSE_ERR("%s gpcdma_dev is null\n", __func__);
		return;
	}

	for (i = 0; i < MAX_NUMBER_MISC_DEVICES; i++) {
		if (g_node_dma[i].gpcdma_dev == gpcdma_dev) {
			dma_buf = &g_node_dma[i].gpc_dma_buf;
			if ((dma_buf->buf_len > 0U) && (dma_buf->buf_ptr != NULL)) {
				dma_free_coherent(gpcdma_dev,
					dma_buf->buf_len,
					dma_buf->buf_ptr,
					dma_buf->buf_iova);
				dma_buf->buf_len = 0U;
				dma_buf->buf_ptr = NULL;
			}
		}
	}
}


static int tegra_hv_vse_allocate_se_dma_bufs(struct tegra_vse_node_dma *node_dma,
		struct device *se_dev,
		struct crypto_dev_to_ivc_map *ivc_map)
{
	int32_t err = -ENOMEM;
	uint32_t buf_sizes[MAX_SE_DMA_BUFS] = {0U};
	uint32_t i;

	if (!node_dma) {
		VSE_ERR("%s node_dma is null\n", __func__);
		err = -EINVAL;
		goto exit;
	}

	if (!se_dev) {
		VSE_ERR("%s se_dev is null\n", __func__);
		err = -EINVAL;
		goto exit;
	}

	if (!ivc_map) {
		VSE_ERR("%s ivc_map is null\n", __func__);
		err = -EINVAL;
		goto exit;
	}

	switch (ivc_map->engine_id) {
	case VIRTUAL_SE_AES0:
	case VIRTUAL_SE_AES1:
	case VIRTUAL_GCSE1_AES0:
	case VIRTUAL_GCSE1_AES1:
	case VIRTUAL_GCSE2_AES0:
	case VIRTUAL_GCSE2_AES1:
		/*
		 * For AES algs, the worst case requirement is for AES-GCM encryption:
		 * 1. src buffer(requires up to max limit specified in DT)
		 * 2. aad buffer(requires up to max limit specified in DT)
		 * 3. mac/tag buffer(requires 64 bytes)
		 * 4. comp/match buffer(requires 4 bytes)
		 */
		buf_sizes[AES_SRC_BUF_IDX] = ivc_map->max_buffer_size;
		buf_sizes[AES_AAD_BUF_IDX] = ivc_map->max_buffer_size;
		buf_sizes[AES_TAG_BUF_IDX] = AES_TAG_BUF_SIZE;
		buf_sizes[AES_COMP_BUF_IDX] = RESULT_COMPARE_BUF_SIZE;
		break;
	case VIRTUAL_SE_SHA:
	case VIRTUAL_GCSE1_SHA:
	case VIRTUAL_GCSE2_SHA:
		/*
		 * For SHA algs, the worst case requirement for SHAKE128/SHAKE256:
		 * 1. plaintext buffer(requires up to max limit specified in DT)
		 * 2. digest buffer(support a maximum digest size of 1024 bytes for SHAKE)
		 * 3. match code/comp buffer(requires 4 bytes)
		 */
		buf_sizes[SHA_SRC_BUF_IDX] = ivc_map->max_buffer_size;
		buf_sizes[SHA_HASH_BUF_IDX] = SHA_HASH_BUF_SIZE;
		buf_sizes[HMAC_SHA_COMP_BUF_IDX] = RESULT_COMPARE_BUF_SIZE;
		break;
	case VIRTUAL_SE_TSEC:
		buf_sizes[TSEC_SRC_BUF_IDX] = ivc_map->max_buffer_size;
		buf_sizes[TSEC_MAC_BUF_IDX] = TEGRA_VIRTUAL_SE_AES_CMAC_DIGEST_SIZE;
		buf_sizes[TSEC_FW_STATUS_BUF_IDX] = RESULT_COMPARE_BUF_SIZE;
		break;
	default:
		err = 0;
		goto exit;
	}

	node_dma->se_dev = se_dev;
	for (i = 0; i < MAX_SE_DMA_BUFS; i++) {
		if (buf_sizes[i] == 0U)
			continue;

		node_dma->se_dma_buf[i].buf_ptr = dma_alloc_coherent(se_dev,
						buf_sizes[i],
						&node_dma->se_dma_buf[i].buf_iova, GFP_KERNEL);
		if (!node_dma->se_dma_buf[i].buf_ptr) {
			dev_err(se_dev, "%s dma_alloc_coherent failed\n", __func__);
			err = -ENOMEM;
			goto exit;
		}
		node_dma->se_dma_buf[i].buf_len = buf_sizes[i];
	}

	err = 0;

exit:
	return err;
}

static void tegra_hv_vse_release_se_dma_bufs(struct device *se_dev)
{
	uint32_t i, j;
	struct tegra_vse_dma_buf *dma_buf = NULL;

	if (!se_dev) {
		VSE_ERR("%s se_dev is null\n", __func__);
		return;
	}

	for (i = 0; i < MAX_NUMBER_MISC_DEVICES; i++) {
		if (g_node_dma[i].se_dev == se_dev) {
			for (j = 0; j < MAX_SE_DMA_BUFS; j++) {
				dma_buf = &g_node_dma[i].se_dma_buf[j];
				if ((dma_buf->buf_len > 0U) && (dma_buf->buf_ptr != NULL)) {
					dma_free_coherent(se_dev,
						dma_buf->buf_len,
						dma_buf->buf_ptr,
						dma_buf->buf_iova);
					dma_buf->buf_len = 0U;
					dma_buf->buf_ptr = NULL;
				}
			}
		}
	}
}

static void tegra_hv_vse_release_all_dma_bufs(void)
{
	uint32_t i, j;
	struct tegra_vse_dma_buf *dma_buf = NULL;

	for (i = 0; i < MAX_NUMBER_MISC_DEVICES; i++) {
		if (g_node_dma[i].se_dev) {
			for (j = 0; j < MAX_SE_DMA_BUFS; j++) {
				dma_buf = &g_node_dma[i].se_dma_buf[j];
				if ((dma_buf->buf_len > 0U) && (dma_buf->buf_ptr != NULL)) {
					dma_free_coherent(g_node_dma[i].se_dev,
						dma_buf->buf_len,
						dma_buf->buf_ptr,
						dma_buf->buf_iova);
					dma_buf->buf_len = 0U;
					dma_buf->buf_ptr = NULL;
				}
			}
		}

		if (g_node_dma[i].gpcdma_dev) {
			dma_buf = &g_node_dma[i].gpc_dma_buf;
			if ((dma_buf->buf_len > 0U) && (dma_buf->buf_ptr != NULL)) {
				dma_free_coherent(g_node_dma[i].gpcdma_dev,
					dma_buf->buf_len,
					dma_buf->buf_ptr,
					dma_buf->buf_iova);
				dma_buf->buf_len = 0U;
				dma_buf->buf_ptr = NULL;
			}
		}
	}
}

static int tegra_hv_vse_safety_probe(struct platform_device *pdev)
{
	struct tegra_virtual_se_dev *se_dev = NULL;
	struct crypto_dev_to_ivc_map *crypto_dev = NULL;
	struct device_node *np;
	int err = 0;
	int i;
	uint32_t ivc_id;
	unsigned int mempool_id;
	unsigned int engine_id;
	const struct of_device_id *match;
	const struct tegra_vse_soc_info *pdata = NULL;
	static uint32_t s_node_id;
	uint32_t ivc_cnt, cnt, instance_id;
	bool has_zero_copy_prop;
	static bool s_aes_alg_register_done;
	static bool s_sha_alg_register_done;
	static bool s_tsec_alg_register_done;
	bool is_aes_alg, is_sha_alg, is_tsec_alg;

	dev_info(&pdev->dev, "probe start\n");

	gcm_supports_dma = of_property_read_bool(pdev->dev.of_node, "nvidia,gcm-dma-support");

	if (gcm_supports_dma) {
		gpcdma_dev = &pdev->dev;
		for (i = 0; i < MAX_NUMBER_MISC_DEVICES; i++) {
			err = tegra_hv_vse_allocate_gpc_dma_bufs(&g_node_dma[i], gpcdma_dev,
				&g_crypto_to_ivc_map[i]);
			if (err) {
				dev_err(gpcdma_dev, "%s returned error %d for node id %d\n",
						 __func__, err, i);
				tegra_hv_vse_release_gpc_dma_bufs(gpcdma_dev);
				goto exit;
			}
		}
		return 0;
	}

	has_zero_copy_prop = of_property_read_bool(pdev->dev.of_node, "#zero-copy");

	se_dev = devm_kzalloc(&pdev->dev,
				sizeof(struct tegra_virtual_se_dev),
				GFP_KERNEL);
	if (!se_dev) {
		VSE_ERR("%s devm_kzalloc failed\n", __func__);
		err = -ENOMEM;
		goto exit;
	}

	/* set host1x platform device */
	err = se_get_nvhost_dev(se_dev);
	if (err) {
		dev_err(&pdev->dev, "Failed to get nvhost dev with err: %d\n", err);
		goto exit;
	}

	np = pdev->dev.of_node;
	se_dev->crypto_to_ivc_map = g_crypto_to_ivc_map;
	se_dev->dev = &pdev->dev;
	err = of_property_read_u32(np, "se-engine-id",
				&engine_id);
	if (err) {
		dev_err(&pdev->dev, "se-engine-id property not present\n");
		err = -ENODEV;
		goto exit;
	}

	switch (engine_id) {
	case VIRTUAL_SE_AES0:
	case VIRTUAL_SE_AES1:
	case VIRTUAL_GCSE1_AES0:
	case VIRTUAL_GCSE1_AES1:
	case VIRTUAL_GCSE2_AES0:
	case VIRTUAL_GCSE2_AES1:
		is_aes_alg = true;
		break;
	case VIRTUAL_SE_SHA:
	case VIRTUAL_GCSE1_SHA:
	case VIRTUAL_GCSE2_SHA:
		is_sha_alg = true;
		break;
	case VIRTUAL_SE_TSEC:
		is_tsec_alg = true;
		break;
	default:
		dev_err(se_dev->dev, "%s unsupported engine id %u\n", __func__, engine_id);
		err = -EINVAL;
		goto exit;
	}

	/* read ivccfg from dts */
	err = of_property_read_u32_index(np, "nvidia,ivccfg_cnt", 0, &ivc_cnt);
	if (err) {
		dev_err(se_dev->dev, "Error: failed to read ivc_cnt. err %u\n", err);
		err = -ENODEV;
		goto exit;
	}

	if (ivc_cnt > MAX_NUMBER_MISC_DEVICES) {
		dev_err(se_dev->dev, "%s Error: Unsupported IVC queue count %u\n", __func__, ivc_cnt);
		err = -EINVAL;
		goto exit;
	}

	if (s_node_id > (MAX_NUMBER_MISC_DEVICES - ivc_cnt)) {
		dev_err(se_dev->dev, "%s Error: IVC queue count exceeds maximum supported value of %u\n",
				__func__,
				MAX_NUMBER_MISC_DEVICES);
		err = -EINVAL;
		goto exit;
	}

	if (pdev->dev.of_node) {
		match = of_match_device(of_match_ptr(tegra_hv_vse_safety_of_match),
					&pdev->dev);
		if (!match) {
			dev_err(&pdev->dev, "Error: No device match found\n");
			return -ENODEV;
		}
		pdata = (const struct tegra_vse_soc_info *)match->data;
	} else {
		pdata =
		(struct tegra_vse_soc_info *)pdev->id_entry->driver_data;
	}

	se_dev->chipdata = pdata;

	for (cnt = 0; cnt < ivc_cnt; cnt++) {

		err = of_property_read_u32_index(np, "nvidia,ivccfg", cnt * TEGRA_IVCCFG_ARRAY_LEN
						 + TEGRA_CRYPTO_DEV_ID_OFFSET, &instance_id);
		if (err) {
			dev_err(se_dev->dev, "%s Error: failed to read instance id. err %d\n",
			__func__,  err);
			err = -ENODEV;
			goto exit;
		}

		crypto_dev = &g_crypto_to_ivc_map[s_node_id];
		crypto_dev->ivc_msg = devm_kzalloc(&pdev->dev,
				sizeof(struct tegra_virtual_se_ivc_msg_t), GFP_KERNEL);
		if (!crypto_dev->ivc_msg) {
			dev_err(se_dev->dev, "Error: failed to allocate ivc_msg\n");
			err = -ENOMEM;
			goto exit;
		}

		crypto_dev->ivc_resp_msg = devm_kzalloc(&pdev->dev,
				sizeof(struct tegra_virtual_se_ivc_msg_t), GFP_KERNEL);
		if (!crypto_dev->ivc_resp_msg) {
			dev_err(se_dev->dev, "Error: failed to allocate ivc_resp_msg\n");
			err = -ENOMEM;
			goto exit;
		}

		crypto_dev->priv = devm_kzalloc(&pdev->dev, sizeof(struct tegra_vse_priv_data), GFP_KERNEL);
		if (!crypto_dev->priv) {
			dev_err(se_dev->dev, "Error: failed to allocate priv data\n");
			err = -ENOMEM;
			goto exit;
		}

		err = of_property_read_u32_index(np, "nvidia,ivccfg", cnt * TEGRA_IVCCFG_ARRAY_LEN
							+ TEGRA_IVC_ID_OFFSET, &ivc_id);
		if (err) {
			dev_err(se_dev->dev, "Error: failed to read ivc_id. err %d\n", err);
			err = -ENODEV;
			goto exit;
		}

		err = tegra_vse_validate_ivc_node_id(ivc_id, instance_id, engine_id);
		if (err) {
			err = -ENODEV;
			goto exit;
		}
		crypto_dev->ivc_id = ivc_id;
		crypto_dev->node_id = s_node_id;
		crypto_dev->instance_id = instance_id;
		crypto_dev->se_dev = se_dev;
		crypto_dev->node_in_use = true;

		err = of_property_read_u32_index(np, "nvidia,ivccfg", cnt * TEGRA_IVCCFG_ARRAY_LEN
					 + TEGRA_SE_ENGINE_ID_OFFSET, &crypto_dev->engine_id);
		if (err) {
			dev_err(se_dev->dev, "Error: failed to read engine_id. err %d\n", err);
			err = -ENODEV;
			goto exit;
		}

		if (engine_id != crypto_dev->engine_id) {
			dev_err(se_dev->dev, "Error: se engine mismatch for ivc_id %u\n",
			crypto_dev->ivc_id);
			err = -ENODEV;
			goto exit;
		}

		err = of_property_read_u32_index(np, "nvidia,ivccfg", cnt * TEGRA_IVCCFG_ARRAY_LEN
					 + TEGRA_IVC_PRIORITY_OFFSET, &crypto_dev->priority);
		if (err || crypto_dev->priority > MAX_IVC_Q_PRIORITY) {
			dev_err(se_dev->dev, "Error: invalid queue priority. err %d\n", err);
			err = -ENODEV;
			goto exit;
		}

		err = of_property_read_u32_index(np, "nvidia,ivccfg",
				cnt * TEGRA_IVCCFG_ARRAY_LEN + TEGRA_MAX_BUFFER_SIZE,
				&crypto_dev->max_buffer_size);
		if (err) {
			dev_err(se_dev->dev, "Error: invalid max buffer size. err %d\n", err);
			err = -ENODEV;
			goto exit;
		}

		if (crypto_dev->max_buffer_size >= TEGRA_VIRTUAL_SE_MAX_BUFFER_SIZE) {
			dev_err(se_dev->dev, "Error: max buffer size must be less than %u\n",
			TEGRA_VIRTUAL_SE_MAX_BUFFER_SIZE);
			err = -EINVAL;
			goto exit;
		}

		if (has_zero_copy_prop) {
			if (!se_dev->chipdata->zero_copy_supported) {
				dev_err(se_dev->dev, "Error: zero copy is not supported on this platform\n");
				err = -ENODEV;
				goto exit;
			}

			if (crypto_dev->max_buffer_size > 0U) {
				dev_err(se_dev->dev, "Error: max buffer size must be 0 if 0-copy is supported\n");
				err = -ENODEV;
				goto exit;
			}
			crypto_dev->is_zero_copy_node = true;
		} else {
			crypto_dev->is_zero_copy_node = false;
		}

		err = of_property_read_u32_index(np, "nvidia,ivccfg", cnt * TEGRA_IVCCFG_ARRAY_LEN
				 + TEGRA_CHANNEL_GROUPID_OFFSET, &crypto_dev->channel_grp_id);
		if (err) {
			dev_err(se_dev->dev, "Error: invalid channel group id. err %d\n", err);
			err = -ENODEV;
			goto exit;
		}

		err = of_property_read_u32_index(np, "nvidia,ivccfg", cnt * TEGRA_IVCCFG_ARRAY_LEN
				 + TEGRA_GCM_SUPPORTED_FLAG_OFFSET, &crypto_dev->gcm_dec_supported);
		if (err || crypto_dev->gcm_dec_supported > GCM_DEC_OP_SUPPORTED) {
			dev_err(se_dev->dev, "Error: invalid gcm decrypt supported flag. err %d\n", err);
			err = -ENODEV;
			goto exit;
		}

		err = of_property_read_u32_index(np, "nvidia,ivccfg",
				cnt * TEGRA_IVCCFG_ARRAY_LEN + TEGRA_GCM_DEC_BUFFER_SIZE,
				&crypto_dev->gcm_dec_buffer_size);
		if (err || (crypto_dev->gcm_dec_supported != GCM_DEC_OP_SUPPORTED &&
				crypto_dev->gcm_dec_buffer_size != 0)) {
			dev_err(se_dev->dev,
				"Error: invalid gcm decrypt buffer size. err %d\n", err);
			err = -ENODEV;
			goto exit;
		}
		if (crypto_dev->gcm_dec_buffer_size >= TEGRA_VIRTUAL_SE_MAX_BUFFER_SIZE) {
			dev_err(se_dev->dev, "Error: gcm decrypt buffer size must be less than %u\n",
			TEGRA_VIRTUAL_SE_MAX_BUFFER_SIZE);
			err = -EINVAL;
			goto exit;
		}

		err = of_property_read_u32_index(np, "nvidia,ivccfg", cnt * TEGRA_IVCCFG_ARRAY_LEN
				 + TEGRA_GCM_DEC_MEMPOOL_ID, &mempool_id);
		if (err || ((crypto_dev->gcm_dec_supported != GCM_DEC_OP_SUPPORTED) &&
				(mempool_id != 0))) {
			dev_err(se_dev->dev, "Error: invalid mempool id. err %d\n", err);
			err = -ENODEV;
			goto exit;
		}

		err = of_property_read_u32_index(np, "nvidia,ivccfg", cnt * TEGRA_IVCCFG_ARRAY_LEN
				 + TEGRA_GCM_DEC_MEMPOOL_SIZE, &crypto_dev->mempool.buf_len);
		if (err || ((crypto_dev->gcm_dec_supported == GCM_DEC_OP_SUPPORTED) &&
				(crypto_dev->mempool.buf_len > crypto_dev->gcm_dec_buffer_size))) {
			dev_err(se_dev->dev, "Error: invalid mempool size err %d\n", err);
			err = -ENODEV;
			goto exit;
		}

		dev_info(se_dev->dev, "Virtual SE channel number: %d", ivc_id);

		if (ivc_id >= 0) {
			crypto_dev->ivck = tegra_hv_ivc_reserve(NULL, ivc_id, NULL);
			if (IS_ERR_OR_NULL(crypto_dev->ivck)) {
				dev_err(&pdev->dev, "Failed reserve channel number\n");
				err = -ENODEV;
				goto exit;
			}
		} else {
			dev_err(se_dev->dev, "Failed to get irq for node id\n");
			err = -EINVAL;
			goto exit;
		}

		tegra_hv_ivc_channel_reset(crypto_dev->ivck);

		if (!se_dev->chipdata->gcm_hw_iv_supported && (crypto_dev->mempool.buf_len > 0)) {
			dev_info(se_dev->dev, "Virtual SE mempool channel number: %d\n",
					mempool_id);

			if (tegra_mempool_check_entry(se_dev, mempool_id) == false) {
				crypto_dev->mempool_id = mempool_id;
			} else {
				dev_err(se_dev->dev, "Error: mempool id %u is already used\n",
				mempool_id);
				err = -ENODEV;
				goto exit;
			}

			crypto_dev->ivmk = tegra_hv_mempool_reserve(crypto_dev->mempool_id);
			if (IS_ERR_OR_NULL(crypto_dev->ivmk)) {
				dev_err(&pdev->dev, "Failed to reserve mempool channel %d\n",
						crypto_dev->mempool_id);
				err = -ENODEV;
				goto exit;
			}

			if (crypto_dev->ivmk->size < crypto_dev->mempool.buf_len) {
				dev_err(se_dev->dev, "Error: mempool %u size(%llu) is smaller than DT value(%u)",
						crypto_dev->mempool_id, crypto_dev->ivmk->size,
						crypto_dev->mempool.buf_len);
				err = -ENODEV;
				goto exit;
			}

			crypto_dev->mempool.buf_ptr = devm_memremap(&pdev->dev,
					crypto_dev->ivmk->ipa, crypto_dev->ivmk->size, MEMREMAP_WB);
			if (IS_ERR_OR_NULL(crypto_dev->mempool.buf_ptr)) {
				dev_err(&pdev->dev, "Failed to map mempool area %d\n",
						crypto_dev->mempool_id);
				err = -ENOMEM;
				goto exit;
			}
			/* For GCM decrypt buffer IOVA field represents offset */
			crypto_dev->mempool.buf_iova = 0;
		}

		init_completion(&crypto_dev->tegra_vse_complete);
		mutex_init(&crypto_dev->se_ivc_lock);
		mutex_init(&crypto_dev->irq_state_lock);

		crypto_dev->tegra_vse_task = kthread_run(tegra_vse_kthread, &crypto_dev->node_id,
								"tegra_vse_kthread-%u", s_node_id);
		if (IS_ERR(crypto_dev->tegra_vse_task)) {
			dev_err(se_dev->dev,
				"Couldn't create kthread for vse with node id %u\n", s_node_id);
			err = PTR_ERR(crypto_dev->tegra_vse_task);
			goto exit;
		}

		if (crypto_dev->ivck->irq >= 0) {
			if (request_irq((uint32_t)crypto_dev->ivck->irq,
				tegra_vse_irq_handler, 0, "vse", &crypto_dev->node_id)) {
				dev_err(se_dev->dev, "Failed to request irq %d for node id %u\n",
				crypto_dev->ivck->irq, s_node_id);
				err = -EINVAL;
				goto exit;
			}
		} else {
			dev_err(se_dev->dev, "Failed to get irq for node id\n");
			err = -EINVAL;
			goto exit;
		}
		crypto_dev->wait_interrupt = FIRST_REQ_INTERRUPT;
		err = tegra_hv_vse_allocate_se_dma_bufs(&g_node_dma[s_node_id], se_dev->dev,
				crypto_dev);
		if (err) {
			dev_err(se_dev->dev, "%s returned error %d for engine id %d, node id %d\n",
						 __func__, err, engine_id, crypto_dev->node_id);
			goto exit;
		}
		s_node_id++;
	}

	if (is_aes_alg && !s_aes_alg_register_done) {
		err = crypto_register_ahash(&cmac_alg);
		if (err) {
			dev_err(&pdev->dev,
				"cmac alg register failed. Err %d\n", err);
			goto release_bufs;
		}

		err = crypto_register_ahash(&gmac_alg);
		if (err) {
			dev_err(&pdev->dev,
				"gmac alg register failed. Err %d\n", err);
			goto release_bufs;
		}

		err = crypto_register_rng(&rng_alg);
		if (err) {
			dev_err(&pdev->dev,
				"rng alg register failed. Err %d\n", err);
			goto release_bufs;
		}

		err = tegra_hv_vse_safety_register_hwrng(se_dev);
		if (err) {
			dev_err(&pdev->dev,
				"hwrng register failed. Err %d\n", err);
			goto release_bufs;
		}

		err = crypto_register_skciphers(aes_algs, ARRAY_SIZE(aes_algs));
		if (err) {
			dev_err(&pdev->dev, "aes alg register failed: %d\n",
				err);
			goto release_bufs;
		}

		err = crypto_register_aeads(aead_algs, ARRAY_SIZE(aead_algs));
		if (err) {
			dev_err(&pdev->dev, "aead alg register failed: %d\n",
				err);
			goto release_bufs;
		}

		s_aes_alg_register_done = true;
	}

	if (is_sha_alg && !s_sha_alg_register_done) {
		for (i = 0; i < ARRAY_SIZE(sha_algs); i++) {
			err = crypto_register_ahash(&sha_algs[i]);
			if (err) {
				dev_err(&pdev->dev,
					"sha alg register failed idx[%d]\n", i);
				goto release_bufs;
			}
		}

		s_sha_alg_register_done = true;
	}

	if (is_tsec_alg && !s_tsec_alg_register_done) {
		err = crypto_register_ahash(&tsec_alg);
		if (err) {
			dev_err(&pdev->dev,
				"Tsec alg register failed. Err %d\n", err);
			goto release_bufs;
		}

		s_tsec_alg_register_done = true;
	}
	se_dev->engine_id = engine_id;

	/* Set Engine suspended state to false*/
	atomic_set(&se_dev->se_suspended, 0);
	platform_set_drvdata(pdev, se_dev);

	dev_info(&pdev->dev, "probe success\n");

	return 0;
release_bufs:
	tegra_hv_vse_release_se_dma_bufs(se_dev->dev);

exit:
	return err;
}

static void tegra_hv_vse_safety_shutdown(struct tegra_virtual_se_dev *se_dev)
{
	uint32_t cnt;

	/* skip checking pending request for the node with "nvidia,gcm-dma-support"
	 * which only used to allocate buffer for gpcdma
	 * for other vse nodes which doesn't have "nvidia,gcm-dma-support",
	 * it will still check pending request.
	 */
	if (gcm_supports_dma)
		return;

	/* Set engine to suspend state */
	atomic_set(&se_dev->se_suspended, 1);

	for (cnt = 0; cnt < MAX_NUMBER_MISC_DEVICES; cnt++) {
		if (g_crypto_to_ivc_map[cnt].engine_id == se_dev->engine_id
				&& g_crypto_to_ivc_map[cnt].ivck != NULL) {
			/* Wait for  SE server to be free*/
			while (mutex_is_locked(&g_crypto_to_ivc_map[cnt].se_ivc_lock)
				|| mutex_is_locked(&g_crypto_to_ivc_map[cnt].irq_state_lock))
				usleep_range(8, 10);
		}
	}
}

static void tegra_hv_vse_safety_shutdown_wrapper(struct platform_device *pdev)
{
	struct tegra_virtual_se_dev *se_dev = platform_get_drvdata(pdev);

	tegra_hv_vse_safety_shutdown(se_dev);
}

static int tegra_hv_vse_safety_remove(struct platform_device *pdev)
{
	int i;

	tegra_hv_vse_safety_unregister_hwrng(platform_get_drvdata(pdev));

	for (i = 0U; i < MAX_NUMBER_MISC_DEVICES; i++)	{
		if ((g_crypto_to_ivc_map[i].node_in_use)
				&& (g_crypto_to_ivc_map[i].se_dev->dev == &pdev->dev)) {
			devm_kfree(&pdev->dev, g_crypto_to_ivc_map[i].priv);
			devm_kfree(&pdev->dev, g_crypto_to_ivc_map[i].ivc_msg);
			devm_kfree(&pdev->dev, g_crypto_to_ivc_map[i].ivc_resp_msg);
		}
	}

	for (i = 0; i < ARRAY_SIZE(sha_algs); i++)
		crypto_unregister_ahash(&sha_algs[i]);

	tegra_hv_vse_release_all_dma_bufs();

	return 0;
}

#if defined(CONFIG_PM)
static int tegra_hv_vse_safety_suspend(struct device *dev)
{
	int i;

	if (gcm_supports_dma) {
		if (gpcdma_dev == dev)
			return 0;
	}

	for (i = 0U; i < MAX_NUMBER_MISC_DEVICES; i++) {
		if ((g_crypto_to_ivc_map[i].node_in_use)
			&& (g_crypto_to_ivc_map[i].se_dev->dev == dev))
			break;
	}

	/* Keep engine in suspended state */
	if (i >= MAX_NUMBER_MISC_DEVICES) {
		dev_err(dev, "Failed to find se_dev for dev %s\n", dev->kobj.name);
		return -ENODEV;
	}
	tegra_hv_vse_safety_shutdown(g_crypto_to_ivc_map[i].se_dev);
	return 0;
}

static int tegra_hv_vse_safety_resume(struct device *dev)
{
	int i;

	/* skip checking pending request for the node with "nvidia,gcm-dma-support"
	 * which only used to allocate buffer for gpcdma
	 * for other vse nodes which doesn't have "nvidia,gcm-dma-support",
	 * it will still set engine suspend state to 1.
	 */
	if (gcm_supports_dma) {
		if (gpcdma_dev == dev)
			return 0;
	}

	for (i = 0U; i < MAX_NUMBER_MISC_DEVICES; i++) {
		if ((g_crypto_to_ivc_map[i].node_in_use)
			&& (g_crypto_to_ivc_map[i].se_dev->dev == dev)) {
			break;
		}
	}

	/* Set engine to suspend state to 1 to make it as false */
	if (i >= MAX_NUMBER_MISC_DEVICES) {
		VSE_ERR("%s(): Failed to find se_dev for dev\n", __func__);
		return -ENODEV;
	}
	atomic_set(&g_crypto_to_ivc_map[i].se_dev->se_suspended, 0);

	return 0;
}

static const struct dev_pm_ops tegra_hv_pm_ops = {
	.suspend = tegra_hv_vse_safety_suspend,
	.resume = tegra_hv_vse_safety_resume,
};
#endif /* CONFIG_PM */

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void tegra_hv_vse_safety_remove_wrapper(struct platform_device *pdev)
{
	tegra_hv_vse_safety_remove(pdev);
}
#else
static int tegra_hv_vse_safety_remove_wrapper(struct platform_device *pdev)
{
	return tegra_hv_vse_safety_remove(pdev);
}
#endif

static struct platform_driver tegra_hv_vse_safety_driver = {
	.probe = tegra_hv_vse_safety_probe,
	.remove = tegra_hv_vse_safety_remove_wrapper,
	.shutdown = tegra_hv_vse_safety_shutdown_wrapper,
	.driver = {
		.name = "tegra_hv_vse_safety",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(tegra_hv_vse_safety_of_match),
#if defined(CONFIG_PM)
		.pm = &tegra_hv_pm_ops,
#endif
	},
};

static int __init tegra_hv_vse_safety_module_init(void)
{
	uint32_t i, j;

	for (i = 0U; i < MAX_NUMBER_MISC_DEVICES; i++) {
		g_crypto_to_ivc_map[i].node_in_use = false;
		for (j = 0; j < MAX_ZERO_COPY_BUFS; j++)
			g_node_dma[i].membuf_ctx[j].fd = -1;
	}

	return platform_driver_register(&tegra_hv_vse_safety_driver);
}

static void __exit tegra_hv_vse_safety_module_exit(void)
{
	platform_driver_unregister(&tegra_hv_vse_safety_driver);
}

module_init(tegra_hv_vse_safety_module_init);
module_exit(tegra_hv_vse_safety_module_exit);

#if defined(NV_MODULE_IMPORT_NS_CALLS_STRINGIFY)
MODULE_IMPORT_NS(DMA_BUF);
#else
MODULE_IMPORT_NS("DMA_BUF");
#endif

MODULE_AUTHOR("Mallikarjun Kasoju <mkasoju@nvidia.com>");
MODULE_DESCRIPTION("Virtual Security Engine driver over Tegra Hypervisor IVC channel");
MODULE_LICENSE("GPL");
