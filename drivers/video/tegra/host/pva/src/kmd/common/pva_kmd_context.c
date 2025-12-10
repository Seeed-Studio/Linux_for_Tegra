// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_device_memory.h"
#include "pva_kmd_utils.h"
#include "pva_constants.h"
#include "pva_api_cmdbuf.h"
#include "pva_kmd_resource_table.h"
#include "pva_kmd_device.h"
#include "pva_kmd_queue.h"
#include "pva_kmd_context.h"
#include "pva_kmd_constants.h"
#include "pva_kmd_msg.h"
#include "pva_kmd_limits.h"

struct pva_kmd_context *pva_kmd_context_create(struct pva_kmd_device *pva)
{
	uint32_t alloc_id;
	enum pva_error err = PVA_SUCCESS;
	struct pva_kmd_context *ctx = NULL;

	ctx = pva_kmd_zalloc_block(&pva->context_allocator, &alloc_id);
	if (ctx == NULL) {
		pva_kmd_log_err(
			"pva_kmd_context_create pva_kmd_context block alloc failed");
		goto err_out;
	}
	/* alloc_id bounded by PVA_MAX_NUM_USER_CONTEXTS (7) from allocator */
	/* MISRA 10.4: alloc_id is unsigned, only check upper bound */
	ASSERT(alloc_id <= (uint32_t)U8_MAX);
	ctx->ccq_id = (uint8_t)alloc_id;
	ctx->resource_table_id = ctx->ccq_id;
	ctx->smmu_ctx_id = ctx->ccq_id;
	ctx->pva = pva;
	ctx->max_n_queues = PVA_MAX_NUM_QUEUES_PER_CONTEXT;
	err = pva_kmd_mutex_init(&ctx->ocb_lock);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("pva_kmd_context_create mutex_init failed");
		goto free_ctx;
	}
	ctx->queue_allocator_mem = pva_kmd_zalloc(sizeof(struct pva_kmd_queue) *
						  ctx->max_n_queues);
	if (ctx->queue_allocator_mem == NULL) {
		pva_kmd_log_err(
			"pva_kmd_context_create queue_allocator_mem NULL");
		goto free_ctx;
	}

	/* MISRA C-2023 Rule 10.3: Explicit cast for narrowing conversion */
	err = pva_kmd_block_allocator_init(
		&ctx->queue_allocator, ctx->queue_allocator_mem, 0,
		(uint32_t)sizeof(struct pva_kmd_queue), ctx->max_n_queues);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err(
			"pva_kmd_context_create block allocator init failed");
		goto free_queue_mem;
	}
	/* Power on PVA if not already */
	err = pva_kmd_device_busy(ctx->pva);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("pva_kmd_context_create device busy failed");
		goto deinit_queue_allocator;
	}

	return ctx;

deinit_queue_allocator:
	pva_kmd_block_allocator_deinit(&ctx->queue_allocator);
free_queue_mem:
	pva_kmd_free(ctx->queue_allocator_mem);
free_ctx:
	pva_kmd_mutex_deinit(&ctx->ocb_lock);
	(void)pva_kmd_free_block(&pva->context_allocator, alloc_id);
err_out:
	pva_kmd_log_err("Failed to create PVA context");
	return NULL;
}

static enum pva_error setup_status_memory(struct pva_kmd_context *ctx,
					  uint64_t status_shm_hdl)
{
	enum pva_error err = PVA_SUCCESS;
	ctx->status_mem = pva_kmd_device_memory_acquire(
		status_shm_hdl, 0, sizeof(struct pva_fw_async_error), ctx);
	if (ctx->status_mem == NULL) {
		pva_kmd_log_err("Failed to acquire context status memory");
		err = PVA_INTERNAL;
		goto out;
	}

	err = pva_kmd_device_memory_iova_map(ctx->status_mem, ctx->pva,
					     PVA_ACCESS_RW,
					     PVA_R5_SMMU_CONTEXT_ID);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Failed to map context status memory");
		pva_kmd_device_memory_free(ctx->status_mem);
		ctx->status_mem = NULL;
	}

out:
	return err;
}

static enum pva_error
setup_submit_memory_and_chunk_pool(struct pva_kmd_context *ctx)
{
	enum pva_error err = PVA_SUCCESS;
	uint64_t chunk_mem_size;
	uint64_t size;

	/* Allocate memory for submission */
	chunk_mem_size = pva_kmd_cmdbuf_pool_get_required_mem_size(
		pva_kmd_get_max_cmdbuf_chunk_size(ctx->pva),
		PVA_KMD_MAX_NUM_PRIV_CHUNKS);
	/* Allocate one post fence at the end. This memory will be added to
	  * KMD's own resource table. We don't need to explicitly free it. It
	  * will be freed after we drop the resource. */
	size = safe_addu64(chunk_mem_size, (uint64_t)sizeof(uint32_t));
	ctx->submit_memory = pva_kmd_device_memory_alloc_map(
		size, ctx->pva, PVA_ACCESS_RW, PVA_R5_SMMU_CONTEXT_ID);
	if (ctx->submit_memory == NULL) {
		err = PVA_NOMEM;
		goto out;
	}

	/* Add submit memory to resource table */
	err = pva_kmd_add_dram_buffer_resource(&ctx->pva->dev_resource_table,
					       ctx->submit_memory,
					       &ctx->submit_memory_resource_id,
					       false);
	if (err != PVA_SUCCESS) {
		// Ownership of submit memory is transferred to KMD's resource table so
		// if adding to resource table fails, we need to free it here.
		pva_kmd_device_memory_free(ctx->submit_memory);
		goto out;
	}

	/* Init chunk pool */
	err = pva_kmd_cmdbuf_chunk_pool_init(
		&ctx->chunk_pool, ctx->submit_memory_resource_id,
		0 /* offset */, (uint32_t)chunk_mem_size,
		pva_kmd_get_max_cmdbuf_chunk_size(ctx->pva),
		PVA_KMD_MAX_NUM_PRIV_CHUNKS, ctx->submit_memory->va);
	if (err != PVA_SUCCESS) {
		pva_kmd_drop_resource(&ctx->pva->dev_resource_table,
				      ctx->submit_memory_resource_id);
		goto out;
	}

	/* Init fence */
	ctx->fence_offset = chunk_mem_size;

out:
	return err;
}

static enum pva_error notify_fw_context_init(struct pva_kmd_context *ctx)
{
	struct pva_kmd_submitter *dev_submitter = &ctx->pva->submitter;
	struct pva_cmd_init_resource_table *res_cmd;
	struct pva_cmd_init_queue *queue_cmd;
	struct pva_cmd_update_resource_table *update_cmd;
	struct pva_resource_entry entry = { 0 };
	const struct pva_syncpt_rw_info *syncpt_info;
	enum pva_error err;
	uint32_t current_offset = 0;
	uint32_t cmd_scratch[CMD_LEN(struct pva_cmd_init_resource_table) +
			     CMD_LEN(struct pva_cmd_init_queue) +
			     CMD_LEN(struct pva_cmd_update_resource_table)] = {
		0
	};

	res_cmd = (struct pva_cmd_init_resource_table *)pva_offset_pointer(
		&cmd_scratch[0], current_offset);
	current_offset += (uint32_t)sizeof(*res_cmd);

	queue_cmd = (struct pva_cmd_init_queue *)pva_offset_pointer(
		&cmd_scratch[0], current_offset);
	current_offset += (uint32_t)sizeof(*queue_cmd);

	update_cmd = (struct pva_cmd_update_resource_table *)pva_offset_pointer(
		&cmd_scratch[0], current_offset);
	current_offset += (uint32_t)sizeof(*update_cmd);

	pva_kmd_set_cmd_init_resource_table(
		res_cmd, ctx->resource_table_id,
		ctx->ctx_resource_table.table_mem->iova,
		ctx->ctx_resource_table.n_entries, ctx->status_mem->iova);

	syncpt_info = pva_kmd_queue_get_rw_syncpt_info(
		ctx->pva, PVA_PRIV_CCQ_ID, ctx->ccq_id);
	pva_kmd_set_cmd_init_queue(
		queue_cmd, PVA_PRIV_CCQ_ID,
		ctx->ccq_id, /* For privileged queues, queue ID == user CCQ ID*/
		ctx->ctx_queue.queue_memory->iova,
		ctx->ctx_queue.max_num_submit, syncpt_info->syncpt_id,
		syncpt_info->syncpt_iova);

	err = pva_kmd_make_resource_entry(&ctx->pva->dev_resource_table,
					  ctx->submit_memory_resource_id,
					  &entry);
	ASSERT(err == PVA_SUCCESS);
	pva_kmd_set_cmd_update_resource_table(update_cmd,
					      0, /* KMD's resource table ID */
					      ctx->submit_memory_resource_id,
					      &entry, NULL);

	err = pva_kmd_submit_cmd_sync(dev_submitter, cmd_scratch,
				      (uint32_t)sizeof(cmd_scratch),
				      PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
				      PVA_KMD_WAIT_FW_TIMEOUT_US);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err(
			"Failed to submit command for context init notify");
	}
	return err;
}

static enum pva_error notify_fw_context_deinit(struct pva_kmd_context *ctx)
{
	struct pva_kmd_submitter *dev_submitter = &ctx->pva->submitter;
	struct pva_cmd_deinit_resource_table *deinit_table_cmd;
	struct pva_cmd_deinit_queue *deinit_queue_cmd;
	uint32_t cmd_scratch[CMD_LEN(struct pva_cmd_deinit_queue) +
			     CMD_LEN(struct pva_cmd_deinit_resource_table)] = {
		0
	};
	enum pva_error err;

	deinit_queue_cmd = (struct pva_cmd_deinit_queue *)pva_offset_pointer(
		&cmd_scratch[0], 0);
	deinit_table_cmd =
		(struct pva_cmd_deinit_resource_table *)pva_offset_pointer(
			&cmd_scratch[0], sizeof(struct pva_cmd_deinit_queue));

	pva_kmd_set_cmd_deinit_queue(
		deinit_queue_cmd, PVA_PRIV_CCQ_ID,
		ctx->ccq_id /* For privileged queues, queue ID == user CCQ ID*/
	);

	pva_kmd_set_cmd_deinit_resource_table(deinit_table_cmd,
					      ctx->resource_table_id);

	err = pva_kmd_submit_cmd_sync(dev_submitter, cmd_scratch,
				      (uint32_t)sizeof(cmd_scratch),
				      PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
				      PVA_KMD_WAIT_FW_TIMEOUT_US);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err(
			"Failed to submit command for context deinit notify");
	}
	return err;
}

enum pva_error pva_kmd_context_init(struct pva_kmd_context *ctx,
				    uint32_t res_table_capacity,
				    uint64_t status_shm_hdl)
{
	enum pva_error err;
	uint32_t queue_mem_size;
	struct pva_fw_postfence post_fence = { 0 };

	if (ctx->inited) {
		err = PVA_INVAL;
		goto err_out;
	}

	if (res_table_capacity == 0u) {
		pva_kmd_log_err("Invalid resource capacity");
		err = PVA_BAD_PARAMETER_ERROR;
		goto err_out;
	}

	err = setup_status_memory(ctx, status_shm_hdl);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	/* Init resource table for this context */
	err = pva_kmd_resource_table_init(&ctx->ctx_resource_table, ctx->pva,
					  ctx->smmu_ctx_id, res_table_capacity);
	if (err != PVA_SUCCESS) {
		goto unmap_status_mem;
	}

	/* Init privileged queue for this context */
	queue_mem_size = pva_get_submission_queue_memory_size(
		PVA_KMD_MAX_NUM_PRIV_SUBMITS);
	ctx->ctx_queue_mem =
		pva_kmd_device_memory_alloc_map(queue_mem_size, ctx->pva,
						PVA_ACCESS_RW,
						PVA_R5_SMMU_CONTEXT_ID);
	if (ctx->ctx_queue_mem == NULL) {
		err = PVA_NOMEM;
		goto deinit_table;
	}

	pva_kmd_queue_init(
		&ctx->ctx_queue, ctx->pva, PVA_PRIV_CCQ_ID,
		ctx->ccq_id, /* Context's PRIV queue ID is identical to CCQ ID */
		ctx->ctx_queue_mem, PVA_KMD_MAX_NUM_PRIV_SUBMITS);

	err = setup_submit_memory_and_chunk_pool(ctx);
	if (err != PVA_SUCCESS) {
		goto queue_deinit;
	}

	/* Init submitter */
	err = pva_kmd_mutex_init(&ctx->submit_lock);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("pva_kmd_context_init submit_lock init failed");
		goto deinit_chunk_pool;
	}
	err = pva_kmd_mutex_init(&ctx->chunk_pool_lock);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err(
			"pva_kmd_context_init chunk_pool_lock init failed");
		goto deinit_submit_lock;
	}
	post_fence.resource_id = ctx->submit_memory_resource_id;
	post_fence.offset_lo = iova_lo(ctx->fence_offset);
	post_fence.offset_hi = iova_hi(ctx->fence_offset);
	post_fence.ts_resource_id = PVA_RESOURCE_ID_INVALID;
	pva_kmd_submitter_init(
		&ctx->submitter, &ctx->ctx_queue, &ctx->submit_lock,
		&ctx->chunk_pool, &ctx->chunk_pool_lock,
		(uint32_t *)pva_offset_pointer(ctx->submit_memory->va,
					       ctx->fence_offset),
		&post_fence);

	/* Use KMD's queue to inform FW */
	err = notify_fw_context_init(ctx);
	if (err != PVA_SUCCESS) {
		goto deinit_submitter;
	}

	err = pva_kmd_shared_buffer_init(ctx->pva, ctx->ccq_id,
					 (uint32_t)PVA_KMD_FW_BUF_ELEMENT_SIZE,
					 res_table_capacity,
					 pva_kmd_resource_table_lock,
					 pva_kmd_resource_table_unlock);
	if (err != PVA_SUCCESS) {
		goto deinit_fw_context;
	}

	ctx->inited = true;

	return PVA_SUCCESS;

deinit_fw_context:
	(void)notify_fw_context_deinit(ctx);
deinit_submitter:
	pva_kmd_mutex_deinit(&ctx->chunk_pool_lock);
deinit_submit_lock:
	pva_kmd_mutex_deinit(&ctx->submit_lock);
deinit_chunk_pool:
	pva_kmd_cmdbuf_chunk_pool_deinit(&ctx->chunk_pool);
	pva_kmd_drop_resource(&ctx->pva->dev_resource_table,
			      ctx->submit_memory_resource_id);
queue_deinit:
	pva_kmd_device_memory_free(ctx->ctx_queue_mem);
deinit_table:
	pva_kmd_resource_table_deinit(&ctx->ctx_resource_table);
unmap_status_mem:
	pva_kmd_device_memory_iova_unmap(ctx->status_mem);
	pva_kmd_device_memory_free(ctx->status_mem);
err_out:
	return err;
}

void pva_kmd_free_context(struct pva_kmd_context *ctx)
{
	enum pva_error err = PVA_SUCCESS;

	if (ctx->inited) {
		pva_kmd_mutex_deinit(&ctx->submit_lock);
		pva_kmd_mutex_deinit(&ctx->chunk_pool_lock);
		pva_kmd_cmdbuf_chunk_pool_deinit(&ctx->chunk_pool);

		pva_kmd_drop_resource(&ctx->pva->dev_resource_table,
				      ctx->submit_memory_resource_id);
		pva_kmd_device_memory_free(ctx->ctx_queue_mem);
		pva_kmd_resource_table_deinit(&ctx->ctx_resource_table);
		pva_kmd_device_memory_iova_unmap(ctx->status_mem);
		pva_kmd_device_memory_free(ctx->status_mem);
		ctx->inited = false;
	}

	pva_kmd_block_allocator_deinit(&ctx->queue_allocator);
	pva_kmd_free(ctx->queue_allocator_mem);
	pva_kmd_mutex_deinit(&ctx->ocb_lock);
	err = pva_kmd_free_block(&ctx->pva->context_allocator, ctx->ccq_id);
	ASSERT(err == PVA_SUCCESS);
}

static void set_sticky_error(enum pva_error *ret, enum pva_error err)
{
	if (*ret == PVA_SUCCESS) {
		*ret = err;
	}
}

static enum pva_error pva_kmd_destroy_all_queues(struct pva_kmd_context *ctx)
{
	enum pva_error ret = PVA_SUCCESS;

	for (uint32_t queue_id = 0u; queue_id < ctx->max_n_queues; queue_id++) {
		struct pva_kmd_queue *queue;

		pva_kmd_mutex_lock(&ctx->queue_allocator.allocator_lock);
		queue = pva_kmd_get_block_unsafe(&ctx->queue_allocator,
						 queue_id);
		pva_kmd_mutex_unlock(&ctx->queue_allocator.allocator_lock);
		if (queue != NULL) {
			set_sticky_error(&ret,
					 pva_kmd_queue_destroy(ctx, queue_id));
		}
	}
	return ret;
}

static enum pva_error notify_fw_context_destroy(struct pva_kmd_context *ctx)
{
	enum pva_error ret = PVA_SUCCESS;

	set_sticky_error(&ret, pva_kmd_destroy_all_queues(ctx));
	set_sticky_error(&ret, notify_fw_context_deinit(ctx));
	set_sticky_error(&ret,
			 pva_kmd_shared_buffer_deinit(ctx->pva, ctx->ccq_id));

	return ret;
}

void pva_kmd_context_destroy(struct pva_kmd_context *client)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_kmd_device *pva = client->pva;
	bool deferred_free = false;

	if (client->inited) {
		err = notify_fw_context_destroy(client);
		if (err != PVA_SUCCESS) {
			deferred_free = true;
			pva_kmd_add_deferred_context_free(pva, client->ccq_id);
			pva_kmd_log_err(
				"Failed to notify FW of context destroy; Deferring resource free until PVA is powered off.");
		}
	}

	if (!deferred_free) {
		pva_kmd_free_context(client);
	}
	pva_kmd_device_idle(pva);
}

struct pva_kmd_context *pva_kmd_get_context(struct pva_kmd_device *pva,
					    uint8_t alloc_id)
{
	return pva_kmd_get_block_unsafe(&pva->context_allocator, alloc_id);
}
