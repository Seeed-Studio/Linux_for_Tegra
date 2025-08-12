// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#include "pva_kmd_shim_trace_event.h"
#include "pva_kmd_linux_device.h"
#define CREATE_TRACE_POINTS
#include "trace/events/nvpva_ftrace.h"

static uint32_t get_job_id(uint32_t queue_id, uint64_t submit_id)
{
	return (queue_id & 0x000000FF) << 24 | (submit_id & 0xFFFFFFU);
}

void pva_kmd_shim_add_trace_vpu_exec(
	struct pva_kmd_device *pva,
	struct pva_kmd_fw_msg_vpu_trace const *trace_info)
{
	uint64_t vpu_start = trace_info->vpu_start_time;
	uint64_t vpu_end = trace_info->vpu_end_time;

	// Unlike in PVA V2 stack, submissions do not go through KMD.
	// So, the concept of a task being enqueued by KMD does not exist.
	// We can request FW to record timestamps of when command buffers
	// were submitted to it, but that would introduce a lot of overhead.
	uint64_t queue_start = vpu_start;
	uint64_t queue_end = vpu_start;

	// In V2, each kernel launch is independent and has a distinct setup
	// and teardown phase. In V3, several kernels may share a command buffer
	// and it is difficult to distincitly determine the setup and teardown
	// phase for each kernel.
	// So, we use the vpu_start time as the prepare_start and prepare_end time.
	uint64_t prepare_start = vpu_start;
	uint64_t prepare_end = vpu_start;

	// In V2, each kernel launch has a distinct postfence.
	// In V3, several kernel launches may share a command buffer and therefore
	// the same postfence. Using this postfence time for all kernel launches
	// may be confusing for the user. So, we use vpu_end time instead.
	uint64_t post_start = vpu_end;
	uint64_t post_end = vpu_end;

	// In V2, Job ID is a 32-bit value with the top 8 bits being the queue ID
	// and the bottom 24 bits being a per-task counter. In V3, we only use the
	// queue ID.
	uint32_t job_id =
		get_job_id(trace_info->queue_id, trace_info->submit_id);

	trace_pva_job_ext_event(job_id, trace_info->ccq_id,
				0, // syncpt_thresh,
				trace_info->engine_id, queue_start, queue_end,
				prepare_start, prepare_end, vpu_start, vpu_end,
				post_start, post_end);

	trace_job_submit(NULL, pva_kmd_get_device_class_id(pva), job_id,
			 trace_info->num_prefences, trace_info->prog_id,
			 trace_info->submit_id, vpu_start);
}

void pva_kmd_shim_add_trace_fence(
	struct pva_kmd_device *pva,
	struct pva_kmd_fw_msg_fence_trace const *trace_info)
{
	uint32_t job_id;

	// We want to log events only for user workloads
	if (trace_info->ccq_id == PVA_PRIV_CCQ_ID) {
		return;
	}

	job_id = get_job_id(trace_info->queue_id, trace_info->submit_id);

	if (trace_info->action == PVA_KMD_FW_BUF_MSG_FENCE_ACTION_WAIT) {
		if (trace_info->type == PVA_KMD_FW_BUF_MSG_FENCE_TYPE_SYNCPT) {
			trace_job_prefence(job_id, trace_info->fence_id,
					   trace_info->value);
		} else if (trace_info->type ==
			   PVA_KMD_FW_BUF_MSG_FENCE_TYPE_SEMAPHORE) {
			trace_job_prefence_semaphore(
				job_id, trace_info->fence_id,
				PVA_LOW32(trace_info->offset),
				trace_info->value);
		}
	} else if (trace_info->action ==
		   PVA_KMD_FW_BUF_MSG_FENCE_ACTION_SIGNAL) {
		if (trace_info->type == PVA_KMD_FW_BUF_MSG_FENCE_TYPE_SYNCPT) {
			trace_job_postfence(job_id, trace_info->fence_id,
					    trace_info->value);
		} else if (trace_info->type ==
			   PVA_KMD_FW_BUF_MSG_FENCE_TYPE_SEMAPHORE) {
			trace_job_postfence_semaphore(
				job_id, trace_info->fence_id,
				PVA_LOW32(trace_info->offset),
				trace_info->value);
		}
	}
}
