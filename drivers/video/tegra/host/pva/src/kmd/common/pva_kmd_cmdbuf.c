// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#include "pva_kmd_cmdbuf.h"
#include "pva_api_cmdbuf.h"
#include "pva_kmd_utils.h"
#include "pva_math_utils.h"

#define CHUNK_STATE_INVALID 0
#define CHUNK_STATE_FENCE_TRIGGERED 1

static uint32_t *
get_chunk_states(struct pva_kmd_cmdbuf_chunk_pool *cmdbuf_chunk_pool)
{
	return (uint32_t *)pva_offset_pointer(
		cmdbuf_chunk_pool->mem_base_va,
		cmdbuf_chunk_pool->chunk_states_offset);
}

static void *get_chunk(struct pva_kmd_cmdbuf_chunk_pool *cmdbuf_chunk_pool,
		       uint32_t chunk_id)
{
	return pva_offset_pointer(cmdbuf_chunk_pool->mem_base_va,
				  cmdbuf_chunk_pool->chunk_size * chunk_id);
}

static uint32_t get_chunk_id_from_res_offset(
	struct pva_kmd_cmdbuf_chunk_pool *cmdbuf_chunk_pool, uint64_t offset)
{
	ASSERT(offset >= cmdbuf_chunk_pool->mem_offset);
	offset -= cmdbuf_chunk_pool->mem_offset;
	return offset / cmdbuf_chunk_pool->chunk_size;
}

enum pva_error pva_kmd_cmdbuf_chunk_pool_init(
	struct pva_kmd_cmdbuf_chunk_pool *cmdbuf_chunk_pool,
	uint32_t mem_resource_id, uint64_t mem_offset, uint32_t mem_size,
	uint16_t chunk_size, uint32_t num_chunks, void *mem_base_va)
{
	uint32_t *chunk_states;
	uint32_t i;
	enum pva_error err;

	ASSERT(mem_size >= pva_kmd_cmdbuf_pool_get_required_mem_size(
				   chunk_size, num_chunks));

	cmdbuf_chunk_pool->mem_resource_id = mem_resource_id;
	cmdbuf_chunk_pool->mem_offset = mem_offset;
	cmdbuf_chunk_pool->mem_size = mem_size;
	cmdbuf_chunk_pool->chunk_size = chunk_size;
	cmdbuf_chunk_pool->num_chunks = num_chunks;
	cmdbuf_chunk_pool->mem_base_va = mem_base_va;
	cmdbuf_chunk_pool->chunk_states_offset = chunk_size * num_chunks;
	chunk_states = get_chunk_states(cmdbuf_chunk_pool);
	for (i = 0; i < num_chunks; i++) {
		chunk_states[i] = CHUNK_STATE_INVALID;
	}

	err = pva_kmd_block_allocator_init(&cmdbuf_chunk_pool->block_allocator,
					   mem_base_va, 0, chunk_size,
					   num_chunks);
	pva_kmd_mutex_init(&cmdbuf_chunk_pool->chunk_state_lock);
	return err;
}

void pva_kmd_cmdbuf_chunk_pool_deinit(struct pva_kmd_cmdbuf_chunk_pool *pool)
{
	pva_kmd_mutex_deinit(&pool->chunk_state_lock);
	pva_kmd_block_allocator_deinit(&pool->block_allocator);
}

void pva_kmd_free_linked_cmdbuf_chunks(struct pva_kmd_cmdbuf_chunk_pool *pool,
				       uint32_t chunk_id)
{
	struct pva_cmd_link_chunk *begin;
	uint32_t *chunk_states;
	uint64_t offset;
	uint32_t resource_id;

	chunk_states = get_chunk_states(pool);
	while (true) {
		begin = get_chunk(pool, chunk_id);
		chunk_states[chunk_id] = CHUNK_STATE_INVALID;
		offset = assemble_addr(begin->next_chunk_offset_hi,
				       begin->next_chunk_offset_lo);
		resource_id = begin->next_chunk_resource_id;
		pva_kmd_free_block(&pool->block_allocator, chunk_id);
		if (resource_id == PVA_RESOURCE_ID_INVALID) {
			break;
		}
		ASSERT(resource_id == pool->mem_resource_id);
		/* Free next chunk */
		chunk_id = get_chunk_id_from_res_offset(pool, offset);
	}
}

static bool recycle_chunks(struct pva_kmd_cmdbuf_chunk_pool *pool)
{
	uint32_t *chunk_states;
	uint32_t i;
	bool freed = false;

	chunk_states = get_chunk_states(pool);
	for (i = 0; i < pool->num_chunks; i++) {
		if (chunk_states[i] == CHUNK_STATE_FENCE_TRIGGERED) {
			pva_kmd_free_linked_cmdbuf_chunks(pool, i);
			freed = true;
			break;
		}
	}

	return freed;
}

enum pva_error
pva_kmd_alloc_cmdbuf_chunk(struct pva_kmd_cmdbuf_chunk_pool *pool,
			   uint32_t *out_chunk_id)
{
	enum pva_error err = PVA_SUCCESS;
	void *chunk;

	pva_kmd_mutex_lock(&pool->chunk_state_lock);
	chunk = pva_kmd_alloc_block(&pool->block_allocator, out_chunk_id);
	if (chunk == NULL) {
		if (recycle_chunks(pool)) {
			chunk = pva_kmd_alloc_block(&pool->block_allocator,
						    out_chunk_id);
			ASSERT(chunk != NULL);
		} else {
			err = PVA_NOMEM;
		}
	}
	pva_kmd_mutex_unlock(&pool->chunk_state_lock);
	return err;
}

void pva_kmd_get_free_notifier_fence(struct pva_kmd_cmdbuf_chunk_pool *pool,
				     uint32_t chunk_id,
				     struct pva_fw_postfence *fence)
{
	uint64_t offset_sum =
		safe_addu64(pool->mem_offset, pool->chunk_states_offset);
	uint64_t chunk_size =
		(uint64_t)safe_mulu32((uint32_t)sizeof(uint32_t), chunk_id);
	uint64_t state_offset = safe_addu64(offset_sum, chunk_size);
	memset(fence, 0, sizeof(*fence));
	fence->resource_id = pool->mem_resource_id;
	fence->offset_lo = iova_lo(state_offset);
	fence->offset_hi = iova_hi(state_offset);
	fence->value = CHUNK_STATE_FENCE_TRIGGERED;
	fence->ts_resource_id = PVA_RESOURCE_ID_INVALID;
}

static void *current_cmd(struct pva_kmd_cmdbuf_builder *builder)
{
	return pva_offset_pointer(
		pva_kmd_get_cmdbuf_chunk_va(builder->pool,
					    builder->current_chunk_id),
		builder->current_chunk_offset);
}

static void begin_chunk(struct pva_kmd_cmdbuf_builder *builder)
{
	struct pva_cmd_link_chunk *cmd = pva_kmd_get_cmdbuf_chunk_va(
		builder->pool, builder->current_chunk_id);
	memset(cmd, 0, sizeof(*cmd));
	cmd->header.opcode = PVA_CMD_OPCODE_LINK_CHUNK;
	cmd->header.len = sizeof(*cmd) / sizeof(uint32_t);
	cmd->next_chunk_resource_id = PVA_RESOURCE_ID_INVALID;
	builder->current_chunk_offset = sizeof(*cmd);
}

static void end_chunk(struct pva_kmd_cmdbuf_builder *builder)
{
	/* Size of this chunk is now known. Update the header of the previous chunk. */
	*builder->chunk_size_ptr = builder->current_chunk_offset;
}

static void link_chunk(struct pva_kmd_cmdbuf_builder *builder,
		       uint32_t new_chunk_id)
{
	struct pva_cmd_link_chunk *old_link;
	uint64_t new_chunk_offset;

	old_link = (struct pva_cmd_link_chunk *)pva_kmd_get_cmdbuf_chunk_va(
		builder->pool, builder->current_chunk_id);
	new_chunk_offset = pva_kmd_get_cmdbuf_chunk_res_offset(builder->pool,
							       new_chunk_id);
	old_link->next_chunk_resource_id = builder->pool->mem_resource_id;
	old_link->next_chunk_offset_lo = iova_lo(new_chunk_offset);
	old_link->next_chunk_offset_hi = iova_hi(new_chunk_offset);
	/* The new chunk size is still unknown. We record the pointer here. */
	builder->chunk_size_ptr = &old_link->next_chunk_size;
}

void *pva_kmd_reserve_cmd_space(struct pva_kmd_cmdbuf_builder *builder,
				uint16_t size)
{
	uint16_t max_size;
	enum pva_error err;
	void *cmd_start;

	max_size = safe_subu16(builder->pool->chunk_size,
			       (uint16_t)sizeof(struct pva_cmd_link_chunk));

	ASSERT(size <= max_size);

	if ((builder->current_chunk_offset + size) >
	    builder->pool->chunk_size) {
		/* Not enough space in the current chunk. Allocate a new one. */
		uint32_t new_chunk_id;

		err = pva_kmd_alloc_cmdbuf_chunk(builder->pool, &new_chunk_id);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err("No more chunk in the pool");
			goto err_out;
		}
		end_chunk(builder);
		link_chunk(builder, new_chunk_id);

		builder->current_chunk_id = new_chunk_id;
		builder->current_chunk_offset = 0;
		begin_chunk(builder);
	}

	cmd_start = current_cmd(builder);
	(void)memset(cmd_start, 0, size);

	builder->current_chunk_offset += size;

	return cmd_start;
err_out:
	return NULL;
}

enum pva_error
pva_kmd_cmdbuf_builder_init(struct pva_kmd_cmdbuf_builder *builder,
			    struct pva_kmd_cmdbuf_chunk_pool *chunk_pool)
{
	enum pva_error err = PVA_SUCCESS;
	uint32_t const min_chunk_size = sizeof(struct pva_cmd_link_chunk);

	ASSERT(chunk_pool->chunk_size >= min_chunk_size);

	builder->pool = chunk_pool;
	err = pva_kmd_alloc_cmdbuf_chunk(chunk_pool,
					 &builder->current_chunk_id);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}
	builder->current_chunk_offset = 0;
	builder->first_chunk_size = 0;
	builder->first_chunk_id = builder->current_chunk_id;
	builder->chunk_size_ptr = &builder->first_chunk_size;

	begin_chunk(builder);

	return PVA_SUCCESS;
err_out:
	return err;
}

void pva_kmd_cmdbuf_builder_finalize(struct pva_kmd_cmdbuf_builder *builder,
				     uint32_t *out_first_chunk_id,
				     uint16_t *out_first_chunk_size)
{
	end_chunk(builder);
	*out_first_chunk_id = builder->first_chunk_id;
	*out_first_chunk_size = builder->first_chunk_size;
}

void pva_kmd_cmdbuf_builder_cancel(struct pva_kmd_cmdbuf_builder *builder)
{
	pva_kmd_free_linked_cmdbuf_chunks(builder->pool,
					  builder->first_chunk_id);
}
