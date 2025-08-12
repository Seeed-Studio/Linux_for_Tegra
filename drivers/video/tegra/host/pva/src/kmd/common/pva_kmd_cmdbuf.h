/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_CMDBUF_H
#define PVA_KMD_CMDBUF_H
#include "pva_fw.h"
#include "pva_resource.h"
#include "pva_kmd_block_allocator.h"
#include "pva_kmd_mutex.h"
#include "pva_api_cmdbuf.h"
#include "pva_utils.h"
#include "pva_math_utils.h"

struct pva_kmd_queue;

/**
 * A fixed-size pool of command buffer chunks.
 *
 * We can allocate chunks from this pool. When submitting the chunks, we should
 * request a post fence from the pool for the first chunk. When the post fence
 * is triggered, the chain of chunks will be considered free by the pool.
 */
struct pva_kmd_cmdbuf_chunk_pool {
	uint16_t chunk_size;
	uint32_t num_chunks;
	uint32_t mem_resource_id;
	uint64_t mem_size;
	uint64_t mem_offset; /**< Starting offset in the resource that can be
			      * used by this pool */
	uint64_t chunk_states_offset;
	void *mem_base_va;
	struct pva_kmd_block_allocator block_allocator;
	pva_kmd_mutex_t chunk_state_lock;
};

static inline uint64_t
pva_kmd_cmdbuf_pool_get_required_mem_size(uint16_t chunk_size,
					  uint32_t num_chunks)
{
	/* Add storage required for free notifier fences */
	return (chunk_size + sizeof(uint32_t)) * num_chunks;
}

/**
 * Initialize the chunk pool.
 *
 * @param[out] Pointer to the pool.
 *
 * @param[in] mem_resource_id Resource ID of the memory to be used for the pool.
 *
 * @param[in] mem_offset Offset of the memory to be used for the pool.

 * @param[in] mem_size Size of the memory to be used for the pool.
 *
 * @param[in] chunk_size Size of each chunk in the pool.
 *
 * @param[in] num_chunks Number of chunks in the pool.
 *
 * @param[in] mem_base_va Virtual address of the memory to be used for the pool.
 *            The virtual address is the base address of the resource.
 */
enum pva_error pva_kmd_cmdbuf_chunk_pool_init(
	struct pva_kmd_cmdbuf_chunk_pool *cmdbuf_chunk_pool,
	uint32_t mem_resource_id, uint64_t mem_offset, uint32_t mem_size,
	uint16_t chunk_size, uint32_t num_chunks, void *mem_base_va);

void pva_kmd_cmdbuf_chunk_pool_deinit(
	struct pva_kmd_cmdbuf_chunk_pool *cmdbuf_chunk_pool);

/**
 * Allocate a chunk from the pool.
 *
 * If the chunk is submitted, then free will be done automatically when
 * free-notifier fence is triggered.
 */
enum pva_error
pva_kmd_alloc_cmdbuf_chunk(struct pva_kmd_cmdbuf_chunk_pool *cmdbuf_chunk_pool,
			   uint32_t *out_chunk_id);

/**
 * Free a linked list of chunks.
 *
 * We only need to call this function if we decide not to submit the chunks,
 * usually in error path.
 */
void pva_kmd_free_linked_cmdbuf_chunks(
	struct pva_kmd_cmdbuf_chunk_pool *cmdbuf_chunk_pool, uint32_t chunk_id);

/**
 * Get the free-notifier fence.
 *
 * @param[in] The first chunk of the command buffer to be submitted.
 *
 * @param[out] The free-notifier fence that should be submitted with the command buffer.
 */
void pva_kmd_get_free_notifier_fence(
	struct pva_kmd_cmdbuf_chunk_pool *cmdbuf_chunk_pool, uint32_t chunk_id,
	struct pva_fw_postfence *fence);

static inline void *
pva_kmd_get_cmdbuf_chunk_va(struct pva_kmd_cmdbuf_chunk_pool *cmdbuf_chunk_pool,
			    uint32_t chunk_id)
{
	return (void *)((uintptr_t)cmdbuf_chunk_pool->mem_base_va +
			chunk_id * cmdbuf_chunk_pool->chunk_size);
}

static inline uint64_t pva_kmd_get_cmdbuf_chunk_res_offset(
	struct pva_kmd_cmdbuf_chunk_pool *cmdbuf_chunk_pool, uint32_t chunk_id)
{
	uint64_t chunk_size = (uint64_t)safe_mulu32(
		chunk_id, (uint32_t)cmdbuf_chunk_pool->chunk_size);
	return safe_addu64(cmdbuf_chunk_pool->mem_offset, chunk_size);
}

/**
 * Utility for building a command buffer with multiple chunks.
 *
 * The builder will automatically allocate chunks from the pool when the current
 * chunk is full.
 */
struct pva_kmd_cmdbuf_builder {
	uint16_t first_chunk_size;
	uint16_t current_chunk_offset;
	uint32_t first_chunk_id;
	uint32_t current_chunk_id;
	struct pva_kmd_cmdbuf_chunk_pool *pool;
	uint16_t *chunk_size_ptr; /**< Pointer to the chunk size field of the previous link_chunk command  */
};

enum pva_error
pva_kmd_cmdbuf_builder_init(struct pva_kmd_cmdbuf_builder *builder,
			    struct pva_kmd_cmdbuf_chunk_pool *chunk_pool);

void *pva_kmd_reserve_cmd_space(struct pva_kmd_cmdbuf_builder *builder,
				uint16_t size);
void pva_kmd_cmdbuf_builder_finalize(struct pva_kmd_cmdbuf_builder *builder,
				     uint32_t *out_first_chunk_id,
				     uint16_t *out_first_chunk_size);

void pva_kmd_cmdbuf_builder_cancel(struct pva_kmd_cmdbuf_builder *builder);

static inline void pva_kmd_set_cmd_init_resource_table(
	struct pva_cmd_init_resource_table *cmd, uint8_t resource_table_id,
	uint64_t iova_addr, uint32_t max_num_entries)
{
	memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_INIT_RESOURCE_TABLE;
	cmd->header.len = sizeof(*cmd) / sizeof(uint32_t);
	cmd->resource_table_id = resource_table_id;
	cmd->resource_table_addr_lo = iova_lo(iova_addr);
	cmd->resource_table_addr_hi = iova_hi(iova_addr);
	cmd->max_n_entries = max_num_entries;
}

static inline void
pva_kmd_set_cmd_deinit_resource_table(struct pva_cmd_deinit_resource_table *cmd,
				      uint8_t resource_table_id)
{
	memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_DEINIT_RESOURCE_TABLE;
	cmd->header.len = sizeof(*cmd) / sizeof(uint32_t);
	cmd->resource_table_id = resource_table_id;
}

static inline void pva_kmd_set_cmd_init_queue(struct pva_cmd_init_queue *cmd,
					      uint8_t ccq_id, uint8_t queue_id,
					      uint64_t queue_addr,
					      uint32_t max_num_submit,
					      uint32_t syncpt_id,
					      uint64_t syncpt_addr)
{
	memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_INIT_QUEUE;
	cmd->header.len = sizeof(*cmd) / sizeof(uint32_t);
	cmd->ccq_id = ccq_id;
	cmd->queue_id = queue_id;
	cmd->queue_addr_lo = iova_lo(queue_addr);
	cmd->queue_addr_hi = iova_hi(queue_addr);
	cmd->max_n_submits = max_num_submit;
	cmd->syncpt_id = syncpt_id;
	cmd->syncpt_addr_lo = iova_lo(syncpt_addr);
	cmd->syncpt_addr_hi = iova_hi(syncpt_addr);
}

static inline void
pva_kmd_set_cmd_deinit_queue(struct pva_cmd_deinit_queue *cmd, uint8_t ccq_id,
			     uint8_t queue_id)
{
	memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_DEINIT_QUEUE;
	cmd->header.len = sizeof(*cmd) / sizeof(uint32_t);
	cmd->ccq_id = ccq_id;
	cmd->queue_id = queue_id;
}

static inline void pva_kmd_set_cmd_update_resource_table(
	struct pva_cmd_update_resource_table *cmd, uint32_t resource_table_id,
	uint32_t resource_id, struct pva_resource_entry const *entry,
	struct pva_resource_aux_info const *aux_info)
{
	memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_UPDATE_RESOURCE_TABLE;
	cmd->header.len = sizeof(*cmd) / sizeof(uint32_t);
	cmd->resource_table_id = resource_table_id;
	cmd->resource_id = resource_id;
	cmd->entry = *entry;
	if (aux_info) {
		cmd->aux_info = *aux_info;
	}
}

static inline void
pva_kmd_set_cmd_unregister_resource(struct pva_cmd_unregister_resource *cmd,
				    uint32_t resource_id)
{
	memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_UNREGISTER_RESOURCE;
	cmd->header.len = sizeof(*cmd) / sizeof(uint32_t);
	cmd->resource_id = resource_id;
}

static inline void
pva_kmd_set_cmd_enable_fw_profiling(struct pva_cmd_enable_fw_profiling *cmd,
				    uint32_t filter, uint8_t timestamp_type)
{
	memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_ENABLE_FW_PROFILING;
	cmd->header.len = sizeof(*cmd) / sizeof(uint32_t);
	cmd->filter = filter;
	cmd->timestamp_type = timestamp_type;
}

static inline void
pva_kmd_set_cmd_disable_fw_profiling(struct pva_cmd_disable_fw_profiling *cmd)
{
	memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_DISABLE_FW_PROFILING;
	cmd->header.len = sizeof(*cmd) / sizeof(uint32_t);
}

static inline void pva_kmd_set_cmd_get_tegra_stats(
	struct pva_cmd_get_tegra_stats *cmd, uint32_t buffer_resource_id,
	uint32_t buffer_size, uint64_t offset, bool enabled)
{
	memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_GET_TEGRA_STATS;
	cmd->header.len = sizeof(*cmd) / sizeof(uint32_t);
	cmd->buffer_resource_id = buffer_resource_id;
	cmd->buffer_offset_hi = iova_hi(offset);
	cmd->buffer_offset_lo = iova_lo(offset);
	cmd->buffer_size = buffer_size;
	cmd->enabled = enabled;
}

static inline void
pva_kmd_set_cmd_set_trace_level(struct pva_cmd_set_trace_level *cmd,
				uint32_t trace_level)
{
	memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_SET_TRACE_LEVEL;
	cmd->header.len = sizeof(*cmd) / sizeof(uint32_t);
	cmd->trace_level = trace_level;
}

static inline void pva_kmd_set_cmd_suspend_fw(struct pva_cmd_suspend_fw *cmd)
{
	memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_SUSPEND_FW;
	cmd->header.len = sizeof(*cmd) / sizeof(uint32_t);
}

static inline void pva_kmd_set_cmd_resume_fw(struct pva_cmd_resume_fw *cmd)
{
	memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_RESUME_FW;
	cmd->header.len = sizeof(*cmd) / sizeof(uint32_t);
}

static inline void pva_kmd_set_cmd_init_shared_dram_buffer(
	struct pva_cmd_init_shared_dram_buffer *cmd, uint8_t interface,
	uint32_t buffer_iova, uint32_t buffer_size)
{
	memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_INIT_SHARED_DRAM_BUFFER;
	cmd->header.len = sizeof(*cmd) / sizeof(uint32_t);
	cmd->buffer_iova_hi = iova_hi(buffer_iova);
	cmd->buffer_iova_lo = iova_lo(buffer_iova);
	cmd->buffer_size = buffer_size;
	cmd->interface = interface;
}

static inline void pva_kmd_set_cmd_deinit_shared_dram_buffer(
	struct pva_cmd_deinit_shared_dram_buffer *cmd, uint8_t interface)
{
	memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_DEINIT_SHARED_DRAM_BUFFER;
	cmd->header.len = sizeof(*cmd) / sizeof(uint32_t);
	cmd->interface = interface;
}

static inline void
pva_kmd_set_cmd_set_profiling_level(struct pva_cmd_set_profiling_level *cmd,
				    uint32_t level)
{
	memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_SET_PROFILING_LEVEL;
	cmd->header.len = sizeof(*cmd) / sizeof(uint32_t);
	cmd->level = level;
}

static inline void pva_kmd_set_cmd_get_version(struct pva_cmd_get_version *cmd,
					       uint64_t buffer_iova)
{
	memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_GET_VERSION;
	cmd->header.len = sizeof(*cmd) / sizeof(uint32_t);
	cmd->buffer_iova_hi = iova_hi(buffer_iova);
	cmd->buffer_iova_lo = iova_lo(buffer_iova);
}

#define CMD_LEN(cmd_type) (sizeof(cmd_type) / sizeof(uint32_t))

#endif // PVA_KMD_CMDBUF_H
