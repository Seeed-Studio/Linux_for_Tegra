/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, NVIDIA Corporation.  All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation.  Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */

#include "pva_kmd_submitter.h"
#include "pva_kmd_utils.h"

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
	/* TODO: remove these flags after FW execute command buffer with no engines. */
	submit_info.flags =
		PVA_INSERT8(0x3, PVA_CMDBUF_FLAGS_ENGINE_AFFINITY_MSB,
			    PVA_CMDBUF_FLAGS_ENGINE_AFFINITY_LSB);

	pva_kmd_mutex_lock(submitter->submit_lock);
	submitter->fence_future_value += 1U;
	submit_info.postfences[0].value = submitter->fence_future_value;
	err = pva_kmd_queue_submit(submitter->queue, &submit_info);
	if (err == PVA_SUCCESS) {
		*out_fence_val = submitter->fence_future_value;
	} else {
		submitter->fence_future_value -= 1U;
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

	while (*fence_addr < fence_val) {
		pva_kmd_sleep_us(poll_interval_us);
		time_spent = safe_addu32(time_spent, poll_interval_us);
		if (time_spent >= timeout_us) {
			pva_kmd_log_err("pva_kmd_submitter_wait Timed out");
			return PVA_TIMEDOUT;
		}
	}

	return PVA_SUCCESS;
}
