/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 */

#ifndef _NVVSE_LINUX_H
#define _NVVSE_LINUX_H

#include <uapi/misc/nvvse-ioctl.h>
#include "se_job_completion_hal.h"
#include "comms_lib_hal.h"
#include "mem_serv_hal.h"
#include "core_common_hal.h"

#define UINT8_MAX (255)

#define KEYSLOT_SIZE_BYTES		16
#define KEYSLOT_OFFSET_BYTES		8
#define MAX_SE_DMA_BUFS	4
#define MAX_ZERO_COPY_BUFS		6U
#define MAX_NUMBER_SOC_PARAMS		10U

struct nvvse_soc_info {
	bool gcm_hw_iv_supported;
	bool zero_copy_supported;
};

/* GCM Operation Supported Flag */
enum tegra_gcm_dec_supported {
	GCM_DEC_OP_NOT_SUPPORTED,
	GCM_DEC_OP_SUPPORTED,
};

struct nvvse_dma_buf {
	dma_addr_t buf_iova;
	void *buf_ptr;
	uint32_t buf_len;
};

struct nvvse_membuf_ctx {
	int fd;
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attach;
};

struct nvvse_node_dma {
	struct device *se_dev;
	struct device *gpcdma_dev;
	struct nvvse_dma_buf se_dma_buf[MAX_SE_DMA_BUFS];
	struct nvvse_dma_buf gpc_dma_buf;
	struct nvvse_membuf_ctx membuf_ctx[MAX_ZERO_COPY_BUFS];
	uint32_t mapped_membuf_count;
};

struct nvvse_mem_serv_ctx {
	MemoryParams_t se_mem_params[MAX_SE_DMA_BUFS];
	MemoryParams_t gpcdma_mem_params;
};

struct crypto_dev_to_ivc_map {
	/* Node ID - Global ID, used for internal mapping between Cryptodev and VSE driver */
	uint32_t node_id;
	/* SE Communication ID */
	uint32_t se_comm_id;
	/* SE Engine Domain */
	uint32_t se_engine_domain;
	/* SE Engine Domain Instance ID */
	uint32_t se_engine_domain_instanceId;
	/* SE Port */
	uint32_t se_port;
	/* Virtualized Instance ID */
	uint32_t virtualized_instanceId;
	/* Stream ID */
	uint32_t stream_id;
	/* Group ID */
	uint32_t gid;
	/* Priority */
	uint32_t priority;
	/* Max Buffer Size */
	uint32_t max_buffer_size;
	/* GCM Dec Supported */
	enum tegra_gcm_dec_supported gcm_dec_supported;
	/* SOC Params */
	uint32_t soc_params[MAX_NUMBER_SOC_PARAMS];
	/* SOC Params Size */
	uint32_t soc_params_size;
	/* Flag to indicate if the node is in use */
	bool node_in_use;
	/* Flag to indicate if the node is a zero copy node */
	bool is_zero_copy_node;
	/* NVVSE Device handle */
	struct nvvse_dev *se_dev;
};

struct nvvse_dev {
	/* Device pointer */
	struct device *dev;
	/* SE Communication ID */
	uint32_t se_comm_id;
	/* SE Port */
	uint32_t se_port;
	/* SE Engine Domain Instance ID */
	uint32_t se_engine_domain_instanceId;
	/* Engine suspend state */
	atomic_t se_suspended;
	/* SOC Info */
	const struct nvvse_soc_info *chipdata;
	/* HW Random */
#if defined(CONFIG_HW_RANDOM)
	/* Integration with hwrng framework */
	struct hwrng *hwrng;
#endif /* CONFIG_HW_RANDOM */
	/* Crypto to IVC Map */
	struct crypto_dev_to_ivc_map *crypto_to_ivc_map;
};

struct nvvse_dev_membuf_ctx {
	/* File descriptor */
	int fd;
	/* IOVA */
	uint64_t iova;
	/* Node ID */
	uint32_t node_id;
};

/* Security Engine SHA context */
struct nvvse_sha_context {
	/* Security Engine device */
	struct nvvse_dev *se_dev;
	/* SHA operation mode */
	uint32_t mode;
	/* Block size */
	u32 blk_size;
	/* Digest size */
	unsigned int digest_size;
	/* Intermediate digest */
	uint8_t *intermediate_digest;
	/* Intermediate digest size */
	unsigned int intermediate_digest_size;
	/* Total bytes in all the requests */
	u64 total_count;
	/* Flag to indicate if the first request */
	bool is_first;
	/* Crypto Dev Node ID */
	uint32_t node_id;
	/* User source buffer pointer */
	uint8_t *user_src_buf;
	/* User digest buffer pointer */
	uint8_t *user_digest_buffer;
	/* User source buffer size */
	uint32_t user_src_buf_size;
	/* User source buffer IOVA */
	uint64_t user_src_iova;
};

/* Function to get the NVVSE context */
nvvse_priv *get_nvvse_context(void);

/* Function to get the Crypto to IVC map */
struct crypto_dev_to_ivc_map *nvvse_get_crypto_to_ivc_map(void);

/* Function to map the memory buffer */
int nvvse_map_membuf(struct nvvse_dev_membuf_ctx *ctx);

/* Function to unmap all the memory buffers */
void nvvse_unmap_all_membufs(uint32_t node_id);

/* Function to unmap the memory buffer */
int nvvse_unmap_membuf(struct nvvse_dev_membuf_ctx *ctx);

/* Function to update the SHA context */
int nvvse_sha_update(struct nvvse_sha_context *sha_ctx, bool is_last,
		enum nvvse_sha_type sha_type);

/* Function to create the NVVSE node */
int nvvse_dev_create_node(uint32_t se_comm_id, uint32_t node_id);

/* Function to remove the NVVSE node */
void nvvse_dev_remove_node(uint32_t node_id);

/* Function to create the NVVSE info node */
int nvvse_dev_create_info_node(void);

/* Function to remove the NVVSE info node */
void nvvse_dev_remove_info_node(void);

#endif /*_NVVSE_LINUX_H*/
