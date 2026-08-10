// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#include "pva_kmd_shim_trace_event.h"
#include "pva_kmd_linux_device.h"
#define CREATE_TRACE_POINTS

#include "pva_kmd_linux_ftrace.h"

void pva_kmd_nsys_cmdbuf_trace(
	struct pva_kmd_device *pva,
	struct pva_kmd_fw_msg_cmdbuf_trace const *trace_info)
{
	trace_pva_cmdbuf(trace_info->cmdbuf_id, trace_info->submit_id,
			 trace_info->cmdbuf_submit_time,
			 trace_info->cmdbuf_start_time,
			 trace_info->cmdbuf_end_time,
			 pva_kmd_get_device_class_id(pva),
			 trace_info->process_id, trace_info->thread_id,
			 trace_info->context_id, trace_info->queue_id,
			 trace_info->status);
}

void pva_kmd_nsys_vpu_exec_trace(
	struct pva_kmd_device *pva,
	struct pva_kmd_fw_msg_vpu_exec_trace const *trace_info)
{
	trace_pva_vpu_exec(trace_info->cmdbuf_id, trace_info->exec_id,
			   trace_info->vpu_start_time, trace_info->vpu_end_time,
			   trace_info->engine_id, trace_info->status);
}

void pva_kmd_nsys_engine_acquire_trace(
	struct pva_kmd_device *pva,
	struct pva_kmd_fw_msg_engine_acquire_trace const *trace_info)
{
	trace_pva_engine_acquire(trace_info->cmdbuf_id,
				 trace_info->engine_acquire_time,
				 trace_info->engine_release_time,
				 trace_info->engine_id);
}

void pva_kmd_nsys_fence_trace(
	struct pva_kmd_device *pva,
	struct pva_kmd_fw_msg_fence_trace const *trace_info)
{
	/* Use new unified fence tracepoint */
	trace_pva_fence(trace_info->cmdbuf_id, pva_kmd_get_device_class_id(pva),
			trace_info->action, trace_info->type,
			0, // fence_handle (not used today)
			trace_info->fence_id, trace_info->offset,
			trace_info->value, trace_info->fence_timestamp);
}
