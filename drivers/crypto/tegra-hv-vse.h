/* SPDX-License-Identifier: GPL-2.0-only
 *
 * SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 */

#ifndef __TEGRA_HV_VSE_H
#define __TEGRA_HV_VSE_H

#define KEYSLOT_SIZE_BYTES		16
#define KEYSLOT_OFFSET_BYTES		8
#define MAX_SE_DMA_BUFS	3

struct tegra_vse_soc_info {
	bool gcm_decrypt_supported;
	bool cmac_hw_verify_supported;
	bool sm_supported;
	bool gcm_hw_iv_supported;
	bool hmac_verify_hw_support;
};

/* GCM Operation Supported Flag */
enum tegra_gcm_dec_supported {
	GCM_DEC_OP_NOT_SUPPORTED,
	GCM_DEC_OP_SUPPORTED,
};

enum ivc_irq_state {
	NO_INTERRUPT = 0U,
	FIRST_REQ_INTERRUPT = 1U,
	INTERMEDIATE_REQ_INTERRUPT = 2u,
};

struct tegra_vse_dma_buf {
	dma_addr_t buf_iova;
	void *buf_ptr;
	uint32_t buf_len;
};

struct tegra_vse_node_dma {
	struct device *se_dev;
	struct device *gpcdma_dev;
	struct tegra_vse_dma_buf se_dma_buf[MAX_SE_DMA_BUFS];
	struct tegra_vse_dma_buf gpc_dma_buf;
};

struct crypto_dev_to_ivc_map {
	uint32_t ivc_id;
	uint32_t se_engine;
	uint32_t node_id;
	uint32_t priority;
	uint32_t max_buffer_size;
	uint32_t channel_grp_id;
	enum tegra_gcm_dec_supported gcm_dec_supported;
	uint32_t gcm_dec_buffer_size;
	uint32_t mempool_id;
	struct tegra_hv_ivc_cookie *ivck;
	struct tegra_hv_ivm_cookie *ivmk;
	struct completion tegra_vse_complete;
	struct task_struct *tegra_vse_task;
	bool vse_thread_start;
	struct mutex se_ivc_lock;
	/*Wait for interrupt
	 * 0: No need to wait for interrupt
	 * 1: First request, wait for interrupt
	 * 2: awaiting actual message, wait for interrupt
	 */
	enum ivc_irq_state wait_interrupt;
	struct mutex irq_state_lock;
	struct tegra_vse_dma_buf mempool;
};

struct tegra_virtual_se_dev {
	struct device *dev;
	/* Engine id */
	unsigned int engine_id;
	/* Engine suspend state */
	atomic_t se_suspended;
	struct tegra_vse_soc_info *chipdata;
#if defined(CONFIG_HW_RANDOM)
	/* Integration with hwrng framework */
	struct hwrng *hwrng;
#endif /* CONFIG_HW_RANDOM */
	struct platform_device *host1x_pdev;
	struct crypto_dev_to_ivc_map *crypto_to_ivc_map;
};

/* Security Engine random number generator context */
struct tegra_virtual_se_rng_context {
	/* Security Engine device */
	struct tegra_virtual_se_dev *se_dev;
	/* RNG buffer pointer */
	u32 *rng_buf;
	/* RNG buffer dma address */
	dma_addr_t rng_buf_adr;
	/*Crypto dev instance*/
	uint32_t node_id;
};

/* Security Engine AES context */
struct tegra_virtual_se_aes_context {
	/* Security Engine device */
	struct tegra_virtual_se_dev *se_dev;
	struct skcipher_request *req;
	/* Security Engine key slot */
	u8 aes_keyslot[KEYSLOT_SIZE_BYTES];
	/* key length in bytes */
	u32 keylen;
	/* AES operation mode */
	u32 op_mode;
	/* Is key slot */
	bool is_key_slot_allocated;
	/* size of GCM tag*/
	u32 authsize;
	/*Crypto dev instance*/
	uint32_t node_id;
	/* Flag to indicate user nonce*/
	uint8_t user_nonce;
	/* Flag to indicate first request*/
	uint8_t b_is_first;
	/* Flag to indicate if sm4 is enabled*/
	uint8_t b_is_sm4;
	const struct tegra_vse_dma_buf *src;
	const struct tegra_vse_dma_buf *aad;
	const struct tegra_vse_dma_buf *tag;
};

/* Security Engine/TSEC AES CMAC context */
struct tegra_virtual_se_aes_cmac_context {
	unsigned int digest_size;
	u8 *hash_result;		/* Intermediate hash result */
	dma_addr_t hash_result_addr;	/* Intermediate hash result dma addr */
	bool is_first;			/* Represents first block */
	bool req_context_initialized;	/* Mark initialization status */
	u8 aes_keyslot[KEYSLOT_SIZE_BYTES];
	/* key length in bits */
	u32 keylen;
	bool is_key_slot_allocated;
	/*Crypto dev instance*/
	uint32_t node_id;
	/* Flag to indicate if sm4 is enabled*/
	uint8_t b_is_sm4;
	const struct tegra_vse_dma_buf *src;
};

/* Security Engine AES GMAC context */
struct tegra_virtual_se_aes_gmac_context {
	/* size of GCM tag*/
	u32 authsize;
	/* Mark initialization status */
	bool req_context_initialized;
	u8 aes_keyslot[KEYSLOT_SIZE_BYTES];
	/* key length in bits */
	u32 keylen;
	bool is_key_slot_allocated;
	/*Crypto dev instance*/
	uint32_t node_id;
	/* Flag to indicate if sm4 is enabled*/
	uint8_t b_is_sm4;
	const struct tegra_vse_dma_buf *aad;
	const struct tegra_vse_dma_buf *tag;
};

/* Security Engine SHA context */
struct tegra_virtual_se_sha_context {
	/* Security Engine device */
	struct tegra_virtual_se_dev *se_dev;
	/* SHA operation mode */
	uint32_t mode;
	u32 blk_size;
	unsigned int digest_size;
	uint8_t *intermediate_digest;
	unsigned int intermediate_digest_size;
	u64 total_count;		/* Total bytes in all the requests */
	bool is_first;
	/*Crypto dev instance*/
	uint32_t node_id;
	const struct tegra_vse_dma_buf *plaintext;
	const struct tegra_vse_dma_buf *hash_result;
};

struct tegra_virtual_se_hmac_sha_context {
	/* Security Engine device */
	struct tegra_virtual_se_dev *se_dev;
	/* SHA operation mode */
	u8 mode;
	u32 blk_size;
	unsigned int digest_size;
	/* Total bytes in all the requests */
	u64 total_count;
	/* Represents first block */
	bool is_first;
	bool is_key_slot_allocated;
	/* Keyslot for HMAC-SHA request */
	u8 aes_keyslot[KEYSLOT_SIZE_BYTES];
	/* key length in bits */
	u32 keylen;
	/*Crypto dev instance*/
	uint32_t node_id;
};


/* Security Engine request context */
struct tegra_virtual_se_req_context {
	/* Security Engine device */
	struct tegra_virtual_se_dev *se_dev;
	bool req_context_initialized;	/* Mark initialization status */
	/*Crypto dev instance*/
	uint32_t node_id;
};

/* API to get ivc db from hv_vse driver */
struct crypto_dev_to_ivc_map *tegra_hv_vse_get_db(void);

/* API to get tsec keyload status from vse driver */
int tegra_hv_vse_safety_tsec_get_keyload_status(uint32_t node_id, uint32_t *err_code);

#endif /*__TEGRA_HV_VSE_H*/
