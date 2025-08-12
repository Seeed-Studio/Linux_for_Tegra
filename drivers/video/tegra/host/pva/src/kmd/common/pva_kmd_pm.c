// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_utils.h"
#include "pva_fw.h"
#include "pva_kmd_device_memory.h"
#include "pva_kmd_device.h"
#include "pva_kmd_queue.h"
#include "pva_kmd_context.h"
#include "pva_kmd_block_allocator.h"
#include "pva_utils.h"
#include "pva_kmd_device.h"
#include "pva_kmd_constants.h"
#include "pva_kmd_pm.h"

enum pva_error pva_kmd_prepare_suspend(struct pva_kmd_device *pva)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_cmd_suspend_fw cmd = { 0 };

	pva_kmd_set_cmd_suspend_fw(&cmd);

	err = pva_kmd_submit_cmd_sync(&pva->submitter, &cmd, sizeof(cmd),
				      PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
				      PVA_KMD_WAIT_FW_TIMEOUT_US);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("PVA: Failed to submit FW suspend command\n");
		goto err_out;
	}

err_out:
	return err;
}

enum pva_error pva_kmd_complete_resume(struct pva_kmd_device *pva)
{
	struct pva_kmd_cmdbuf_builder builder;
	struct pva_kmd_submitter *dev_submitter = &pva->submitter;
	struct pva_cmd_init_resource_table *res_cmd;
	struct pva_cmd_init_queue *queue_cmd;
	struct pva_cmd_resume_fw *fw_resume;
	struct pva_cmd_init_shared_dram_buffer *shared_buf_cmd;
	enum pva_error err;
	uint32_t fence_val;
	struct pva_kmd_queue *queue;
	const struct pva_syncpt_rw_info *syncpt_info;

	err = pva_kmd_submitter_prepare(dev_submitter, &builder);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err(
			"PVA: Prepare submitter for FW resume command failed\n");
		goto err_out;
	}

	fw_resume = pva_kmd_reserve_cmd_space(&builder, sizeof(*fw_resume));
	if (fw_resume == NULL) {
		pva_kmd_log_err(
			"PVA: Memory alloc for FW resume command failed\n");
		err = PVA_NOMEM;
		goto cancel_builder;
	}

	pva_kmd_set_cmd_resume_fw(fw_resume);

	for (uint8_t i = 0; i < pva->max_n_contexts; i++) {
		struct pva_kmd_context *ctx = pva_kmd_get_context(
			pva, sat_add8(i, PVA_KMD_USER_CONTEXT_ID_BASE));
		if (ctx != NULL) {
			/**Initialize resource table */
			res_cmd = pva_kmd_reserve_cmd_space(&builder,
							    sizeof(*res_cmd));
			if (res_cmd == NULL) {
				pva_kmd_log_err(
					"PVA: Memory alloc for context registration in FW resume command failed\n");
				err = PVA_NOMEM;
				goto cancel_builder;
			}

			pva_dbg_printf(
				"PVA: Resume init resource table for context %d\n",
				ctx->ccq_id);
			pva_kmd_set_cmd_init_resource_table(
				res_cmd, ctx->resource_table_id,
				ctx->ctx_resource_table.table_mem->iova,
				ctx->ctx_resource_table.n_entries);

			queue_cmd = pva_kmd_reserve_cmd_space(
				&builder, sizeof(*queue_cmd));
			if (queue_cmd == NULL) {
				pva_kmd_log_err(
					"PVA: Memory alloc for queue registration in FW resume command failed\n");
				err = PVA_NOMEM;
				goto cancel_builder;
			}

			/* Initialize shared buffer */
			shared_buf_cmd = pva_kmd_reserve_cmd_space(
				&builder, sizeof(*shared_buf_cmd));
			if (shared_buf_cmd == NULL) {
				pva_kmd_log_err(
					"PVA: Memory alloc for shared buffer registration in FW resume command failed\n");
				err = PVA_NOMEM;
				goto cancel_builder;
			}

			pva_dbg_printf(
				"PVA: Resume shared buffer for context %d\n",
				ctx->ccq_id);
			pva_kmd_set_cmd_init_shared_dram_buffer(
				shared_buf_cmd, ctx->ccq_id,
				pva->kmd_fw_buffers[ctx->ccq_id]
					.resource_memory->iova,
				pva->kmd_fw_buffers[ctx->ccq_id]
					.resource_memory->size);

			pva_dbg_printf(
				"PVA: Resume priv queue for context %d\n",
				ctx->ccq_id);
			syncpt_info = pva_kmd_queue_get_rw_syncpt_info(
				pva, PVA_PRIV_CCQ_ID, ctx->ccq_id);
			pva_kmd_set_cmd_init_queue(
				queue_cmd, PVA_PRIV_CCQ_ID,
				ctx->ccq_id, /* For privileged queues, queue ID == user CCQ ID*/
				ctx->ctx_queue.queue_memory->iova,
				ctx->ctx_queue.max_num_submit,
				syncpt_info->syncpt_id,
				syncpt_info->syncpt_iova);

			/**Initialize resource table */
			for (uint32_t j = 0; j < ctx->max_n_queues; j++) {
				pva_kmd_mutex_lock(
					&ctx->queue_allocator.allocator_lock);
				queue = pva_kmd_get_block_unsafe(
					&ctx->queue_allocator, j);
				if (queue != NULL) {
					pva_dbg_printf(
						"PVA: Resume queue for context %d, queue %d\n",
						queue->ccq_id, queue->queue_id);
					queue_cmd = pva_kmd_reserve_cmd_space(
						&builder, sizeof(*queue_cmd));
					if (queue_cmd == NULL) {
						pva_kmd_log_err(
							"PVA: Memory alloc for queue registration in FW resume command failed\n");
						err = PVA_NOMEM;
						goto cancel_builder;
					}

					syncpt_info =
						pva_kmd_queue_get_rw_syncpt_info(
							pva, ctx->ccq_id,
							queue->queue_id);
					pva_kmd_set_cmd_init_queue(
						queue_cmd, queue->ccq_id,
						queue->queue_id,
						queue->queue_memory->iova,
						queue->max_num_submit,
						syncpt_info->syncpt_id,
						syncpt_info->syncpt_iova);
				}
				pva_kmd_mutex_unlock(
					&ctx->queue_allocator.allocator_lock);
			}
		}
	}

	err = pva_kmd_submitter_submit(dev_submitter, &builder, &fence_val);
	if (err != PVA_SUCCESS) {
		// Error is either QUEUE_FULL or TIMEDOUT
		pva_kmd_log_err(
			"PVA: Submission for FW resume command failed\n");
		goto cancel_builder;
	}

	err = pva_kmd_submitter_wait(dev_submitter, fence_val,
				     PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
				     PVA_KMD_WAIT_FW_TIMEOUT_US);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err(
			"Waiting for FW timed out when resuming from suspend state");
		goto cancel_builder;
	}

	return PVA_SUCCESS;

cancel_builder:
	pva_kmd_cmdbuf_builder_cancel(&builder);

err_out:
	return err;
}
