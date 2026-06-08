// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#include "pva_constants.h"
#include "pva_kmd.h"
#include "pva_kmd_utils.h"
#include "pva_fw.h"
#include "pva_kmd_device_memory.h"
#include "pva_kmd_queue.h"
#include "pva_kmd_context.h"
#include "pva_kmd_block_allocator.h"
#include "pva_utils.h"
#include "pva_kmd_device.h"
#include "pva_kmd_constants.h"
#include "pva_kmd_limits.h"

void pva_kmd_queue_init(struct pva_kmd_queue *queue, struct pva_kmd_device *pva,
			uint8_t ccq_id, uint8_t queue_id,
			struct pva_kmd_device_memory *queue_memory,
			uint32_t max_num_submit)
{
	queue->pva = pva;
	queue->queue_memory = queue_memory;
	queue->ccq_id = ccq_id;
	queue->queue_id = queue_id;
	queue->max_num_submit = max_num_submit;
	queue->queue_header = queue_memory->va;
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
	struct pva_fw_cmdbuf_submit_info *items = pva_offset_pointer(
		queue->queue_header, sizeof(*queue->queue_header));

	if (pva_fw_queue_space(head, tail, size) == 0U) {
		return PVA_QUEUE_FULL;
	}

	items[tail] = *submit_info;

	/* Update tail  */
	tail = wrap_add(tail, 1, size);
	queue->queue_header->cb_tail = tail;
	__sync_synchronize();
	pva_kmd_ccq_push(queue->pva, queue->ccq_id, queue->queue_id);

	return PVA_SUCCESS;
}
static enum pva_error notify_fw_queue_deinit(struct pva_kmd_context *ctx,
					     struct pva_kmd_queue *queue)
{
	struct pva_cmd_deinit_queue cmd = { 0 };
	enum pva_error err;

	pva_kmd_set_cmd_deinit_queue(&cmd, queue->ccq_id, queue->queue_id);

	err = pva_kmd_submit_cmd_sync(&ctx->submitter, &cmd,
				      (uint32_t)sizeof(cmd),
				      PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
				      PVA_KMD_WAIT_FW_TIMEOUT_US);
	if (err != PVA_SUCCESS) {
		goto end;
	}

	return PVA_SUCCESS;

end:
	return err;
}

enum pva_error pva_kmd_queue_create(struct pva_kmd_context *ctx,
				    const struct pva_ops_queue_create *in_args,
				    uint32_t *queue_id)
{
	struct pva_kmd_device_memory *submission_mem_kmd = NULL;
	struct pva_kmd_queue *queue = NULL;
	struct pva_cmd_init_queue cmd = { 0 };
	enum pva_error err, tmperr;
	const struct pva_syncpt_rw_info *syncpt_info;

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

	/* Validate queue_id fits in uint8_t */
	/* MISRA C-2023 Rule 10.4: Both operands must have same essential type */
	if (*queue_id > (uint32_t)U8_MAX) {
		pva_kmd_log_err("Queue ID exceeds U8_MAX");
		err = PVA_INVAL;
		goto err_free_queue;
	}

	/* CERT INT31-C: queue_id validated to fit in uint8_t, safe to cast */
	pva_kmd_queue_init(queue, ctx->pva, ctx->ccq_id, (uint8_t)*queue_id,
			   submission_mem_kmd, in_args->max_submission_count);

	/* Get device mapped IOVA to share with FW */
	err = pva_kmd_device_memory_iova_map(submission_mem_kmd, ctx->pva,
					     PVA_ACCESS_RW,
					     PVA_R5_SMMU_CONTEXT_ID);
	if (err != PVA_SUCCESS) {
		goto err_free_kmd_memory;
	}

	syncpt_info = pva_kmd_queue_get_rw_syncpt_info(ctx->pva, ctx->ccq_id,
						       queue->queue_id);
	pva_kmd_set_cmd_init_queue(&cmd, queue->ccq_id, queue->queue_id,
				   queue->queue_memory->iova,
				   queue->max_num_submit,
				   syncpt_info->syncpt_id,
				   syncpt_info->syncpt_iova);

	err = pva_kmd_submit_cmd_sync(&ctx->submitter, &cmd,
				      (uint32_t)sizeof(cmd),
				      PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
				      PVA_KMD_WAIT_FW_TIMEOUT_US);
	if (err != PVA_SUCCESS) {
		goto unmap_iova;
	}

	return PVA_SUCCESS;

unmap_iova:
	pva_kmd_device_memory_iova_unmap(submission_mem_kmd);
err_free_kmd_memory:
	pva_kmd_device_memory_free(queue->queue_memory);
err_free_queue:
	tmperr = pva_kmd_free_block(&ctx->queue_allocator, *queue_id);
	ASSERT(tmperr == PVA_SUCCESS);

	*queue_id = PVA_INVALID_QUEUE_ID;
err_out:
	return err;
}

enum pva_error pva_kmd_queue_destroy(struct pva_kmd_context *ctx,
				     uint32_t queue_id)
{
	struct pva_kmd_queue *queue;
	enum pva_error err = PVA_SUCCESS;
	enum pva_error tmp_err;

	pva_kmd_mutex_lock(&ctx->queue_allocator.allocator_lock);
	queue = pva_kmd_get_block_unsafe(&ctx->queue_allocator, queue_id);
	if (queue == NULL) {
		pva_kmd_log_err("Destroying non-existent queue");
		err = PVA_INVAL;
		goto unlock;
	}

	err = notify_fw_queue_deinit(ctx, queue);
	if (err != PVA_SUCCESS) {
		//Might happen if FW is aborted. It's safe to keep going.
		pva_kmd_log_err("Failed to notify FW to destroy queue");
	}

	pva_kmd_device_memory_iova_unmap(queue->queue_memory);
	pva_kmd_device_memory_free(queue->queue_memory);
	tmp_err = pva_kmd_free_block_unsafe(&ctx->queue_allocator, queue_id);
	// This cannot fail as we have already checked for queue existence and we
	// are still holding the lock
	ASSERT(tmp_err == PVA_SUCCESS);
unlock:
	pva_kmd_mutex_unlock(&ctx->queue_allocator.allocator_lock);
	return err;
}

const struct pva_syncpt_rw_info *
pva_kmd_queue_get_rw_syncpt_info(struct pva_kmd_device *pva, uint8_t ccq_id,
				 uint8_t queue_id)
{
	uint8_t ctx_offset;
	uint8_t syncpt_index_u8;
	uint32_t syncpt_index;

	/* Use uint8_t arithmetic - all values fit in uint8_t range */
	ctx_offset =
		safe_mulu8(ccq_id, (uint8_t)PVA_NUM_RW_SYNCPTS_PER_CONTEXT);
	syncpt_index_u8 = safe_addu8(ctx_offset, queue_id);
	syncpt_index = (uint32_t)syncpt_index_u8;

	ASSERT(syncpt_index < PVA_NUM_RW_SYNCPTS);
	return &pva->rw_syncpts[syncpt_index];
}
