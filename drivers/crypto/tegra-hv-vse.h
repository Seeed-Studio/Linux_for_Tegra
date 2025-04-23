/* SPDX-License-Identifier: GPL-2.0-only
 *
 * SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 */

#ifndef __TEGRA_HV_VSE_H
#define __TEGRA_HV_VSE_H

#define KEYSLOT_SIZE_BYTES		16
#define KEYSLOT_OFFSET_BYTES		8
#define MAX_SE_DMA_BUFS	4
#define TEGRA_HV_VSE_AES_IV_LEN		16U
#define MAX_ZERO_COPY_BUFS		6U

struct tegra_vse_soc_info {
	bool cmac_hw_verify_supported;
	bool sm_supported;
	bool gcm_hw_iv_supported;
	bool hmac_verify_hw_support;
	bool zero_copy_supported;
	bool allocate_key_slot_supported;
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

struct tegra_vse_membuf_ctx {
	int fd;
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attach;
};

struct tegra_vse_key_slot_ctx {
	uint8_t key_id[KEYSLOT_SIZE_BYTES];
	uint8_t key_usage;
	uint8_t token_id;
	uint8_t key_instance_idx;
	uint32_t key_grp_id;
};

struct tegra_vse_node_dma {
	struct device *se_dev;
	struct device *gpcdma_dev;
	struct tegra_vse_dma_buf se_dma_buf[MAX_SE_DMA_BUFS];
	struct tegra_vse_dma_buf gpc_dma_buf;
	struct tegra_vse_membuf_ctx membuf_ctx[MAX_ZERO_COPY_BUFS];
	uint32_t mapped_membuf_count;
};

struct crypto_dev_to_ivc_map {
	uint32_t ivc_id;
	/* Engine ID - Specific HW engine instance used for performing crypto operations */
	uint32_t engine_id;
	/* Node ID - Global ID, used for internal mapping between Cryptodev and VSE driver */
	uint32_t node_id;
	/* Instance ID - Device node index for a particular engine instance, read from DT */
	uint32_t instance_id;
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
	bool node_in_use;
	bool is_zero_copy_node;
	struct tegra_virtual_se_dev *se_dev;
	struct tegra_vse_priv_data *priv;
	struct tegra_virtual_se_ivc_msg_t *ivc_msg;
	struct tegra_virtual_se_ivc_msg_t *ivc_resp_msg;
};

struct tegra_virtual_se_dev {
	struct device *dev;
	/* Engine id */
	unsigned int engine_id;
	/* Engine suspend state */
	atomic_t se_suspended;
	const struct tegra_vse_soc_info *chipdata;
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
	struct tegra_vse_dma_buf hwrng_dma_buf;
	struct tegra_vse_priv_data *priv;
	struct tegra_virtual_se_ivc_msg_t *ivc_msg;
	/*Crypto dev instance*/
	uint32_t node_id;
};

/* Security Engine AES context */
struct tegra_virtual_se_aes_context {
	/* Security Engine device */
	struct tegra_virtual_se_dev *se_dev;
	struct skcipher_request *req;
	/** [in] Holds the key id */
	uint8_t key_slot[KEYSLOT_SIZE_BYTES];
	/** [in] Holds the token id */
	uint8_t token_id;
	/** [inout] Holds the Key instance index */
	uint32_t key_instance_idx;
	/** [in] Holds the release key flag */
	uint32_t release_key_flag;

	/* AES operation mode */
	u32 op_mode;
	/* Is key slot */
	bool is_key_slot_allocated;
	/*Crypto dev instance*/
	uint32_t node_id;
	/* Flag to indicate user nonce*/
	uint8_t user_nonce;
	/* Flag to indicate first request*/
	uint8_t b_is_first;
	/* Flag to indicate if sm4 is enabled*/
	uint8_t b_is_sm4;
	uint8_t *user_aad_buf;
	uint8_t *user_src_buf;
	uint8_t *user_tag_buf;
	uint8_t *user_dst_buf;
	uint32_t user_src_buf_size;
	uint32_t user_aad_buf_size;
	uint32_t user_tag_buf_size;
	uint8_t iv[TEGRA_HV_VSE_AES_IV_LEN];
};

enum cmac_request_type {
	TEGRA_HV_VSE_CMAC_SIGN,
	TEGRA_HV_VSE_CMAC_VERIFY
};

/* Security Engine/TSEC AES CMAC context */
struct tegra_virtual_se_aes_cmac_context {
	unsigned int digest_size;
	bool is_first;			/* Represents first block */
	bool req_context_initialized;	/* Mark initialization status */
	/** [in] Holds the key id */
	uint8_t key_slot[KEYSLOT_SIZE_BYTES];
	/** [in] Holds the token id */
	uint8_t token_id;
	bool is_key_slot_allocated;
	/*Crypto dev instance*/
	uint32_t node_id;
	/* Flag to indicate if sm4 is enabled*/
	uint8_t b_is_sm4;
	uint8_t *user_src_buf;
	uint8_t *user_mac_buf;
	uint32_t user_src_buf_size;
	enum cmac_request_type request_type;
	/* For CMAC_VERIFY tag comparison result */
	uint8_t result;
};

enum gmac_request_type {
	TEGRA_HV_VSE_GMAC_INIT = 0U,
	TEGRA_HV_VSE_GMAC_SIGN,
	TEGRA_HV_VSE_GMAC_VERIFY
};

/* Security Engine AES GMAC context */
struct tegra_virtual_se_aes_gmac_context {
	/* size of GCM tag*/
	u32 authsize;
	/* Mark initialization status */
	bool req_context_initialized;
	/** [in] Holds the key id */
	uint8_t key_slot[KEYSLOT_SIZE_BYTES];
	/** [inout] Holds the Key instance index */
	uint32_t key_instance_idx;
	/** [in] Holds the release key flag */
	uint32_t release_key_flag;
	/* Flag to indicate if key slot is allocated*/
	bool is_key_slot_allocated;
	/*Crypto dev instance*/
	uint32_t node_id;
	/* Flag to indicate if sm4 is enabled*/
	uint8_t b_is_sm4;
	uint8_t *user_aad_buf;
	uint8_t *user_tag_buf;
	uint32_t user_aad_buf_size;
	enum gmac_request_type request_type;
	/* Return IV after GMAC_INIT and pass IV during GMAC_VERIFY*/
	unsigned char *iv;
	bool is_first;
	/* For GMAC_VERIFY tag comparison result */
	uint8_t result;
	uint64_t user_aad_iova;
	uint64_t user_tag_iova;
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
	uint8_t *user_src_buf;
	uint8_t *user_digest_buffer;
	uint32_t user_src_buf_size;
	uint64_t user_src_iova;
};

enum hmac_sha_request_type {
	TEGRA_HV_VSE_HMAC_SHA_SIGN = 0U,
	TEGRA_HV_VSE_HMAC_SHA_VERIFY
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
	/** [in] Holds the key id */
	uint8_t key_slot[KEYSLOT_SIZE_BYTES];
	/** [in] Holds the token id */
	uint8_t token_id;
	/*Crypto dev instance*/
	uint32_t node_id;
	uint8_t *user_src_buf;
	uint8_t *user_digest_buffer;
	uint32_t user_src_buf_size;
	enum hmac_sha_request_type request_type;
	uint8_t result;
};

struct tegra_virtual_se_membuf_context {
	int fd;
	uint64_t iova;
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

/* API to Map memory buffer corresponding to an FD and return IOVA */
int tegra_hv_vse_safety_map_membuf(struct tegra_virtual_se_membuf_context *ctx);

/* API to Unmap memory buffer corresponding to an FD */
int tegra_hv_vse_safety_unmap_membuf(struct tegra_virtual_se_membuf_context *ctx);

/* API to Unmap all memory buffers corresponding to a node id */
void tegra_hv_vse_safety_unmap_all_membufs(uint32_t node_id);

int tegra_hv_vse_allocate_keyslot(struct tegra_vse_key_slot_ctx *key_slot_params, uint32_t node_id);

int tegra_hv_vse_release_keyslot(struct tegra_vse_key_slot_ctx *key_slot_params, uint32_t node_id);

int tegra_hv_vse_close_keyslot(uint32_t node_id, uint32_t key_grp_id);

#endif /*__TEGRA_HV_VSE_H*/
