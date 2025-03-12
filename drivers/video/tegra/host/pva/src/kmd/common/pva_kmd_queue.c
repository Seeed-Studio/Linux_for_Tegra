// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#include "pva_kmd_utils.h"
#include "pva_fw.h"
#include "pva_kmd_device_memory.h"
#include "pva_kmd_queue.h"
#include "pva_kmd_context.h"
#include "pva_kmd_block_allocator.h"
#include "pva_utils.h"
#include "pva_kmd_device.h"
#include "pva_kmd_constants.h"

void pva_kmd_queue_init(struct pva_kmd_queue *queue, struct pva_kmd_device *pva,
			uint8_t ccq_id, uint8_t queue_id,
			pva_kmd_mutex_t *ccq_lock,
			struct pva_kmd_device_memory *queue_memory,
			uint32_t max_num_submit)
{
	queue->pva = pva;
	queue->queue_memory = queue_memory;
	queue->ccq_id = ccq_id;
	queue->queue_id = queue_id;
	queue->max_num_submit = max_num_submit;
	queue->queue_header = queue_memory->va;
	queue->ccq_lock = ccq_lock;
}

uint32_t pva_kmd_queue_space(struct pva_kmd_queue *queue)
{
	uint32_t head = queue->queue_header->cb_head;
	uint32_t tail = queue->queue_header->cb_tail;
	uint32_t size = queue->max_num_submit;
	return pva_fw_queue_space(head, tail, size);
}

enum pva_error
pva_kmd_queue_submit(struct pva_kmd_queue *queue,
		     struct pva_fw_cmdbuf_submit_info const *submit_info)
{
	uint32_t head = queue->queue_header->cb_head;
	uint32_t tail = queue->queue_header->cb_tail;
	uint32_t size = queue->max_num_submit;
	uint64_t ccq_entry;
	enum pva_error err;
	struct pva_fw_cmdbuf_submit_info *items = pva_offset_pointer(
		queue->queue_header, sizeof(*queue->queue_header));

	if (pva_fw_queue_space(head, tail, size) == 0) {
		return PVA_QUEUE_FULL;
	}

	items[tail] = *submit_info;

	/* Update tail  */
	tail = wrap_add(tail, 1, size);
	ccq_entry =
		PVA_INSERT64(PVA_FW_CCQ_OP_UPDATE_TAIL, PVA_FW_CCQ_OPCODE_MSB,
			     PVA_FW_CCQ_OPCODE_LSB) |
		PVA_INSERT64(queue->queue_id, PVA_FW_CCQ_QUEUE_ID_MSB,
			     PVA_FW_CCQ_QUEUE_ID_LSB) |
		PVA_INSERT64(tail, PVA_FW_CCQ_TAIL_MSB, PVA_FW_CCQ_TAIL_LSB);

	pva_kmd_mutex_lock(queue->ccq_lock);
	/* TODO: memory write barrier is needed here */
	err = pva_kmd_ccq_push_with_timeout(queue->pva, queue->ccq_id,
					    ccq_entry,
					    PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
					    PVA_KMD_WAIT_FW_TIMEOUT_US);
	if (err == PVA_SUCCESS) {
		queue->queue_header->cb_tail = tail;
	}
	pva_kmd_mutex_unlock(queue->ccq_lock);

	return err;
}

void pva_kmd_queue_deinit(struct pva_kmd_queue *queue)
{
	queue->queue_memory = NULL;
	queue->ccq_id = PVA_INVALID_QUEUE_ID;
	queue->max_num_submit = 0;
}

static enum pva_error notify_fw_queue_deinit(struct pva_kmd_context *ctx,
					     struct pva_kmd_queue *queue)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_kmd_cmdbuf_builder builder;
	struct pva_cmd_deinit_queue *queue_cmd;
	uint32_t fence_val;

	err = pva_kmd_submitter_prepare(&ctx->submitter, &builder);
	if (err != PVA_SUCCESS) {
		goto end;
	}

	queue_cmd = pva_kmd_reserve_cmd_space(&builder, sizeof(*queue_cmd));
	if (queue_cmd == NULL) {
		err = PVA_NOMEM;
		goto cancel_submitter;
	}
	pva_kmd_set_cmd_deinit_queue(queue_cmd, queue->ccq_id, queue->queue_id);

	err = pva_kmd_submitter_submit(&ctx->submitter, &builder, &fence_val);
	if (err != PVA_SUCCESS) {
		goto cancel_submitter;
	}

	err = pva_kmd_submitter_wait(&ctx->submitter, fence_val,
				     PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
				     PVA_KMD_WAIT_FW_TIMEOUT_US);
	if (err != PVA_SUCCESS) {
		goto end;
	}
	return PVA_SUCCESS;
cancel_submitter:
	pva_kmd_cmdbuf_builder_cancel(&builder);
end:
	return err;
}

enum pva_error
pva_kmd_queue_create(struct pva_kmd_context *ctx,
		     struct pva_kmd_queue_create_in_args *in_args,
		     uint32_t *queue_id)
{
	struct pva_kmd_device_memory *submission_mem_kmd = NULL;
	struct pva_kmd_queue *queue = NULL;
	struct pva_kmd_cmdbuf_builder builder;
	struct pva_cmd_init_queue *queue_cmd;
	uint32_t fence_val;
	enum pva_error err, tmperr;

	queue = pva_kmd_zalloc_block(&ctx->queue_allocator, queue_id);
	if (queue == NULL) {
		err = PVA_NOMEM;
		goto err_out;
	}

	/* Get handle from mapped memory */
	submission_mem_kmd = pva_kmd_device_memory_acquire(
		in_args->queue_memory_handle, in_args->queue_memory_offset,
		pva_get_submission_queue_memory_size(
			in_args->max_submission_count),
		ctx);
	if (submission_mem_kmd == NULL) {
		err = PVA_INVAL;
		goto err_free_queue;
	}

	pva_kmd_queue_init(queue, ctx->pva, ctx->ccq_id, *queue_id,
			   &ctx->ccq_lock, submission_mem_kmd,
			   in_args->max_submission_count);

	/* Get device mapped IOVA to share with FW */
	err = pva_kmd_device_memory_iova_map(submission_mem_kmd, ctx->pva,
					     PVA_ACCESS_RW,
					     PVA_R5_SMMU_CONTEXT_ID);
	if (err != PVA_SUCCESS) {
		goto err_free_kmd_memory;
	}

	err = pva_kmd_submitter_prepare(&ctx->submitter, &builder);
	if (err != PVA_SUCCESS) {
		goto unmap_iova;
	}

	queue_cmd = pva_kmd_reserve_cmd_space(&builder, sizeof(*queue_cmd));
	if (queue_cmd == NULL) {
		err = PVA_NOMEM;
		goto cancel_submitter;
	}
	ASSERT(queue_cmd != NULL);
	pva_kmd_set_cmd_init_queue(queue_cmd, queue->ccq_id, queue->queue_id,
				   queue->queue_memory->iova,
				   queue->max_num_submit);

	err = pva_kmd_submitter_submit(&ctx->submitter, &builder, &fence_val);
	if (err != PVA_SUCCESS) {
		goto cancel_submitter;
	}

	err = pva_kmd_submitter_wait(&ctx->submitter, fence_val,
				     PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
				     PVA_KMD_WAIT_FW_TIMEOUT_US);
	if (err != PVA_SUCCESS) {
		goto cancel_submitter;
	}

	return PVA_SUCCESS;

cancel_submitter:
	pva_kmd_cmdbuf_builder_cancel(&builder);
unmap_iova:
	pva_kmd_device_memory_iova_unmap(submission_mem_kmd);
err_free_kmd_memory:
	pva_kmd_device_memory_free(queue->queue_memory);
	pva_kmd_queue_deinit(queue);
err_free_queue:
	tmperr = pva_kmd_free_block(&ctx->queue_allocator, *queue_id);
	ASSERT(tmperr == PVA_SUCCESS);

	*queue_id = PVA_INVALID_QUEUE_ID;
err_out:
	return err;
}

enum pva_error
pva_kmd_queue_destroy(struct pva_kmd_context *ctx,
		      struct pva_kmd_queue_destroy_in_args *in_args)
{
	struct pva_kmd_queue *queue;
	enum pva_error err = PVA_SUCCESS;

	/*
	 * TODO :
	 * Send command to FW to stop queue usage. Wait for ack.
	 * This call needs to be added after syncpoint and ccq functions are ready.
	 */
	pva_kmd_mutex_lock(&ctx->queue_allocator.allocator_lock);
	queue = pva_kmd_get_block_unsafe(&ctx->queue_allocator,
					 in_args->queue_id);
	if (queue == NULL) {
		pva_kmd_mutex_unlock(&ctx->queue_allocator.allocator_lock);
		return PVA_INVAL;
	}
	if (!ctx->pva->recovery) {
		err = notify_fw_queue_deinit(ctx, queue);
		if (err != PVA_SUCCESS) {
			pva_kmd_mutex_unlock(
				&ctx->queue_allocator.allocator_lock);
			return err;
		}
	}

	pva_kmd_device_memory_iova_unmap(queue->queue_memory);

	pva_kmd_device_memory_free(queue->queue_memory);

	pva_kmd_queue_deinit(queue);
	pva_kmd_mutex_unlock(&ctx->queue_allocator.allocator_lock);

	err = pva_kmd_free_block(&ctx->queue_allocator, in_args->queue_id);
	ASSERT(err == PVA_SUCCESS);
	return PVA_SUCCESS;
}
