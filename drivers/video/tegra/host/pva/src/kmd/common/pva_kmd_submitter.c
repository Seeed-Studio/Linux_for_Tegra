// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_submitter.h"
#include "pva_api_types.h"
#include "pva_kmd_utils.h"
#include "pva_kmd_abort.h"

void pva_kmd_submitter_init(struct pva_kmd_submitter *submitter,
			    struct pva_kmd_queue *queue,
			    pva_kmd_mutex_t *submit_lock,
			    struct pva_kmd_cmdbuf_chunk_pool *chunk_pool,
			    pva_kmd_mutex_t *chunk_pool_lock,
			    uint32_t *post_fence_va,
			    struct pva_fw_postfence const *post_fence)
{
	submitter->queue = queue;
	submitter->submit_lock = submit_lock;
	submitter->post_fence_va = post_fence_va;
	submitter->post_fence = *post_fence;
	submitter->fence_future_value = 0;
	submitter->chunk_pool = chunk_pool;
	submitter->chunk_pool_lock = chunk_pool_lock;

	*submitter->post_fence_va = submitter->fence_future_value;
}

enum pva_error pva_kmd_submitter_prepare(struct pva_kmd_submitter *submitter,
					 struct pva_kmd_cmdbuf_builder *builder)
{
	enum pva_error err;

	err = pva_kmd_cmdbuf_builder_init(builder, submitter->chunk_pool);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	return PVA_SUCCESS;
err_out:
	return err;
}

enum pva_error
pva_kmd_submitter_submit_with_fence(struct pva_kmd_submitter *submitter,
				    struct pva_kmd_cmdbuf_builder *builder,
				    struct pva_fw_postfence *fence)
{
	enum pva_error err;
	uint32_t first_chunk_id;
	uint16_t first_chunk_size;
	uint64_t first_chunk_offset;
	struct pva_fw_cmdbuf_submit_info submit_info = { 0 };
	struct pva_fw_postfence free_notifier_fence;

	pva_kmd_cmdbuf_builder_finalize(builder, &first_chunk_id,
					&first_chunk_size);

	pva_kmd_get_free_notifier_fence(submitter->chunk_pool, first_chunk_id,
					&free_notifier_fence);
	first_chunk_offset = pva_kmd_get_cmdbuf_chunk_res_offset(
		submitter->chunk_pool, first_chunk_id);

	submit_info.postfences[0] = free_notifier_fence;
	submit_info.num_postfence = 1;
	if (fence->resource_id != PVA_RESOURCE_ID_INVALID) {
		submit_info.postfences[1] = *fence;
		submit_info.num_postfence = 2;
	}
	submit_info.first_chunk_resource_id =
		submitter->chunk_pool->mem_resource_id;
	submit_info.first_chunk_offset_lo = iova_lo(first_chunk_offset);
	submit_info.first_chunk_offset_hi = iova_hi(first_chunk_offset);
	submit_info.first_chunk_size = first_chunk_size;
	submit_info.execution_timeout_ms = PVA_EXEC_TIMEOUT_INF;

	pva_kmd_mutex_lock(submitter->submit_lock);
	err = pva_kmd_queue_submit(submitter->queue, &submit_info);
	if (err != PVA_SUCCESS) {
		pva_kmd_cmdbuf_builder_cancel(builder);
	}
	pva_kmd_mutex_unlock(submitter->submit_lock);

	return err;
}

enum pva_error pva_kmd_submitter_submit(struct pva_kmd_submitter *submitter,
					struct pva_kmd_cmdbuf_builder *builder,
					uint32_t *out_fence_val)
{
	enum pva_error err;
	uint32_t first_chunk_id;
	uint16_t first_chunk_size;
	uint64_t first_chunk_offset;
	struct pva_fw_cmdbuf_submit_info submit_info = { 0 };
	struct pva_fw_postfence free_notifier_fence;

	pva_kmd_cmdbuf_builder_finalize(builder, &first_chunk_id,
					&first_chunk_size);

	pva_kmd_get_free_notifier_fence(submitter->chunk_pool, first_chunk_id,
					&free_notifier_fence);
	first_chunk_offset = pva_kmd_get_cmdbuf_chunk_res_offset(
		submitter->chunk_pool, first_chunk_id);

	submit_info.num_postfence = 2;
	submit_info.postfences[0] = submitter->post_fence;
	submit_info.postfences[1] = free_notifier_fence;
	submit_info.first_chunk_resource_id =
		submitter->chunk_pool->mem_resource_id;
	submit_info.first_chunk_offset_lo = iova_lo(first_chunk_offset);
	submit_info.first_chunk_offset_hi = iova_hi(first_chunk_offset);
	submit_info.first_chunk_size = first_chunk_size;
	submit_info.execution_timeout_ms = PVA_EXEC_TIMEOUT_INF;
	/* TODO: remove these flags after FW execute command buffer with no engines. */
	submit_info.flags =
		PVA_INSERT8(0x3, PVA_CMDBUF_FLAGS_ENGINE_AFFINITY_MSB,
			    PVA_CMDBUF_FLAGS_ENGINE_AFFINITY_LSB);

	pva_kmd_mutex_lock(submitter->submit_lock);
	submitter->fence_future_value =
		safe_wraparound_inc_u32(submitter->fence_future_value);
	submit_info.postfences[0].value = submitter->fence_future_value;
	err = pva_kmd_queue_submit(submitter->queue, &submit_info);
	if (err == PVA_SUCCESS) {
		*out_fence_val = submitter->fence_future_value;
	} else {
		submitter->fence_future_value =
			safe_wraparound_dec_u32(submitter->fence_future_value);
		pva_kmd_cmdbuf_builder_cancel(builder);
	}
	pva_kmd_mutex_unlock(submitter->submit_lock);

	return err;
}

enum pva_error pva_kmd_submitter_wait(struct pva_kmd_submitter *submitter,
				      uint32_t fence_val,
				      uint32_t poll_interval_us,
				      uint32_t timeout_us)
{
	uint32_t volatile *fence_addr = submitter->post_fence_va;
	uint32_t time_spent = 0;
	struct pva_kmd_device *pva = submitter->queue->pva;

#if (PVA_BUILD_MODE == PVA_BUILD_MODE_L4T) ||                                  \
	(PVA_BUILD_MODE == PVA_BUILD_MODE_QNX)
	if (!pva->is_silicon) {
		timeout_us = safe_mulu32(timeout_us,
					 PVA_KMD_WAIT_FW_TIMEOUT_SCALER_SIM);
	}
#endif
	while (*fence_addr < fence_val) {
		if (pva->recovery) {
			return PVA_ERR_FW_ABORTED;
		}
		pva_kmd_sleep_us(poll_interval_us);
		time_spent = safe_addu32(time_spent, poll_interval_us);
		if (time_spent >= timeout_us) {
			pva_kmd_log_err("pva_kmd_submitter_wait Timed out");
			pva_kmd_abort_fw(submitter->queue->pva, PVA_TIMEDOUT);
			return PVA_TIMEDOUT;
		}
	}

	return PVA_SUCCESS;
}

enum pva_error pva_kmd_submit_cmd_sync(struct pva_kmd_submitter *submitter,
				       void *cmds, uint32_t size,
				       uint32_t poll_interval_us,
				       uint32_t timeout_us)
{
	struct pva_kmd_cmdbuf_builder builder = { 0 };
	enum pva_error err;
	void *cmd_dst = NULL;
	uint32_t fence_val = 0;

	err = pva_kmd_submitter_prepare(submitter, &builder);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	cmd_dst = pva_kmd_reserve_cmd_space(&builder, size);
	if (cmd_dst == NULL) {
		err = PVA_INVAL;
		pva_kmd_log_err(
			"Trying to submit too many commands using pva_kmd_submit_cmd_sync.");
		goto cancel_builder;
	}

	memcpy(cmd_dst, cmds, size);
	err = pva_kmd_submitter_submit(submitter, &builder, &fence_val);
	if (err != PVA_SUCCESS) {
		goto cancel_builder;
	}

	err = pva_kmd_submitter_wait(submitter, fence_val, poll_interval_us,
				     timeout_us);
	if (err != PVA_SUCCESS) {
		goto cancel_builder;
	}

	return err;

cancel_builder:
	pva_kmd_cmdbuf_builder_cancel(&builder);
err_out:
	return err;
}
