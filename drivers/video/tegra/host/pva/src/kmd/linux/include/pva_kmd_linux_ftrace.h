/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

/* This is here just to prevent header guard warnings */
/* Since define_trace.h file included at the bottom of this file needs to
 * include this file, we can't use header guards in this file.
 */
#ifndef PVA_KMD_LINUX_FTRACE_H
#define PVA_KMD_LINUX_FTRACE_H
#endif

#undef TRACE_SYSTEM
#define TRACE_SYSTEM tegra_pva

// clang-format off

#if !defined(_TRACE_PVA_FTRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PVA_FTRACE_H

#include <linux/tracepoint.h>
#include <linux/device.h>

TRACE_EVENT(pva_cmdbuf,
	TP_PROTO(u64 cmdbuf_id, u64 submit_id, u64 cmdbuf_submit_timestamp,
		u64 cmdbuf_begin_timestamp, u64 cmdbuf_end_timestamp, u32 class_id, u32 process_id,
		u32 thread_id, u8 context_id, u8 queue_id, u8 status),
	TP_ARGS(cmdbuf_id, submit_id, cmdbuf_submit_timestamp, cmdbuf_begin_timestamp,
		cmdbuf_end_timestamp, class_id, process_id, thread_id, context_id, queue_id, status),
	TP_STRUCT__entry(
		__field(u64, cmdbuf_id)
		__field(u64, submit_id)
		__field(u64, cmdbuf_submit_timestamp)
		__field(u64, cmdbuf_begin_timestamp)
		__field(u64, cmdbuf_end_timestamp)
		__field(u32, class_id)
		__field(u32, process_id)
		__field(u32, thread_id)
		__field(u8, context_id)
		__field(u8, queue_id)
		__field(u8, status)
	),
	TP_fast_assign(
		__entry->cmdbuf_id = cmdbuf_id;
		__entry->submit_id = submit_id;
		__entry->cmdbuf_submit_timestamp = cmdbuf_submit_timestamp;
		__entry->cmdbuf_begin_timestamp = cmdbuf_begin_timestamp;
		__entry->cmdbuf_end_timestamp = cmdbuf_end_timestamp;
		__entry->class_id = class_id;
		__entry->process_id = process_id;
		__entry->thread_id = thread_id;
		__entry->context_id = context_id;
		__entry->queue_id = queue_id;
		__entry->status = status;
	),
	TP_printk("cmdbuf_id=%llu submit_id=%llu submit_timestamp=%llu begin_timestamp=%llu "
		"end_timestamp=%llu class_id=%u process_id=%u thread_id=%u context_id=%u queue_id=%u status=%u",
		__entry->cmdbuf_id, __entry->submit_id, __entry->cmdbuf_submit_timestamp,
		__entry->cmdbuf_begin_timestamp, __entry->cmdbuf_end_timestamp,
		__entry->class_id,
		__entry->process_id, __entry->thread_id, __entry->context_id,
		__entry->queue_id, __entry->status)
);

TRACE_EVENT(pva_vpu_exec,
	TP_PROTO(u64 cmdbuf_id, u64 exec_id, u64 vpu_begin_timestamp,
		u64 vpu_end_timestamp, u8 engine_id, u8 status),
	TP_ARGS(cmdbuf_id, exec_id, vpu_begin_timestamp, vpu_end_timestamp, engine_id, status),
	TP_STRUCT__entry(
		__field(u64, cmdbuf_id)
		__field(u64, exec_id)
		__field(u64, vpu_begin_timestamp)
		__field(u64, vpu_end_timestamp)
		__field(u8, engine_id)
		__field(u8, status)
	),
	TP_fast_assign(
		__entry->cmdbuf_id = cmdbuf_id;
		__entry->exec_id = exec_id;
		__entry->vpu_begin_timestamp = vpu_begin_timestamp;
		__entry->vpu_end_timestamp = vpu_end_timestamp;
		__entry->engine_id = engine_id;
		__entry->status = status;
	),
	TP_printk("cmdbuf_id=%llu exec_id=%llu vpu_begin_timestamp=%llu vpu_end_timestamp=%llu "
		"engine_id=%u status=%u",
		__entry->cmdbuf_id, __entry->exec_id, __entry->vpu_begin_timestamp,
		__entry->vpu_end_timestamp, __entry->engine_id, __entry->status)
);

TRACE_EVENT(pva_engine_acquire,
	TP_PROTO(u64 cmdbuf_id, u64 engine_acquire_timestamp, u64 engine_release_timestamp,
		u8 engine_id),
	TP_ARGS(cmdbuf_id, engine_acquire_timestamp, engine_release_timestamp, engine_id),
	TP_STRUCT__entry(
		__field(u64, cmdbuf_id)
		__field(u64, engine_acquire_timestamp)
		__field(u64, engine_release_timestamp)
		__field(u8, engine_id)
	),
	TP_fast_assign(
		__entry->cmdbuf_id = cmdbuf_id;
		__entry->engine_acquire_timestamp = engine_acquire_timestamp;
		__entry->engine_release_timestamp = engine_release_timestamp;
		__entry->engine_id = engine_id;
	),
	TP_printk("cmdbuf_id=%llu engine_acquire_timestamp=%llu engine_release_timestamp=%llu "
		"engine_id=0x%x",
		__entry->cmdbuf_id, __entry->engine_acquire_timestamp,
		__entry->engine_release_timestamp, __entry->engine_id)
);

TRACE_EVENT(pva_fence,
	TP_PROTO(u64 cmdbuf_id, u32 class_id, u32 fence_kind, u32 fence_type,
		u32 fence_handle, u64 fence_unique_id, u64 fence_offset, u32 fence_threshold, u64 timestamp),
	TP_ARGS(cmdbuf_id, class_id, fence_kind, fence_type, fence_handle, fence_unique_id,
		fence_offset, fence_threshold, timestamp),
	TP_STRUCT__entry(
		__field(u64, cmdbuf_id)
		__field(u32, class_id)
		__field(u32, fence_kind)
		__field(u32, fence_type)
		__field(u32, fence_handle)
		__field(u64, fence_unique_id)
		__field(u64, fence_offset)
		__field(u32, fence_threshold)
		__field(u64, timestamp)
	),
	TP_fast_assign(
		__entry->cmdbuf_id = cmdbuf_id;
		__entry->class_id = class_id;
		__entry->fence_kind = fence_kind;
		__entry->fence_type = fence_type;
		__entry->fence_handle = fence_handle;
		__entry->fence_unique_id = fence_unique_id;
		__entry->fence_offset = fence_offset;
		__entry->fence_threshold = fence_threshold;
		__entry->timestamp = timestamp;
	),
	TP_printk("cmdbuf_id=%llu class_id=%u fence_kind=%u fence_type=%u fence_handle=%u fence_unique_id=%llu fence_offset=%llu fence_threshold=%u timestamp=%llu",
		__entry->cmdbuf_id, __entry->class_id, __entry->fence_kind, __entry->fence_type,
		__entry->fence_handle, __entry->fence_unique_id, __entry->fence_offset,
		__entry->fence_threshold, __entry->timestamp
	)
);

#endif /* End of _TRACE_PVA_FTRACE_H */

/* This part must be outside header guards */

/*
 * 'define_trace.h' creates trace events from the macros defined in this file.
 * To do so, it needs to include this pva trace header file. For this, we declare
 * TRACE_INCLUDE_PATH and TRACE_INCLUDE_FILE macros.Note that we are using a long
 * form for path here to minimize the risk of path collisions.
 *
 * Also, some build systems define 'linux' macro, which may expand and render
 * TRACE_INCLUDE_PATH incorrect. To avoid this, we undefine 'linux' macro before
 * defining TRACE_INCLUDE_PATH and TRACE_INCLUDE_FILE macros. This should not have
 * any impact on the rest of the code.
 */
#undef linux
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../kmd/linux/include
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE pva_kmd_linux_ftrace

// clang-format on

#include <trace/define_trace.h>
