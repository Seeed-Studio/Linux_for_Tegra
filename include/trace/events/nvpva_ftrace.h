// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, NVIDIA Corporation.  All rights reserved.
 *
 * NVPVA event logging to ftrace
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM tegra_pva

#if !defined(_TRACE_NVPVA_FTRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NVPVA_FTRACE_H

#include <linux/tracepoint.h>
#include <linux/device.h>

TRACE_EVENT(job_submit,
	TP_PROTO(struct device *dev, u32 class_id, u32 job_id, u32 num_fences,
		  u64 prog_id, u64 stream_id, u64 hw_timestamp),
	TP_ARGS(dev, class_id, job_id, num_fences, prog_id, stream_id, hw_timestamp),
	TP_STRUCT__entry(
		__field(struct device *, dev)
		__field(u32, class_id)
		__field(u32, job_id)
		__field(u32, num_fences)
		__field(u64, prog_id)
		__field(u64, stream_id)
		__field(u64, hw_timestamp)
	),
	TP_fast_assign(
		__entry->dev = dev;
		__entry->class_id = class_id;
		__entry->job_id = job_id;
		__entry->num_fences = num_fences;
		__entry->prog_id = prog_id;
		__entry->stream_id = stream_id;
		__entry->hw_timestamp = hw_timestamp;
	),
	TP_printk("%s class=%02x id=%u fences=%u stream_id=%llu prog_id=%llu ts=%llu",
		dev_name(__entry->dev), __entry->class_id, __entry->job_id,
		__entry->num_fences, __entry->prog_id, __entry->stream_id,
		__entry->hw_timestamp
	)
);

TRACE_EVENT(pva_job_base_event,
	TP_PROTO(u32 job_id, u32 syncpt_id, u32 threshold, u32 vpu_id,
		 u32 queue_id, u64 vpu_begin_timestamp, u64 vpu_end_timestamp),
	TP_ARGS(job_id, syncpt_id, threshold, vpu_id, queue_id,
		vpu_begin_timestamp, vpu_end_timestamp),
	TP_STRUCT__entry(
		__field(u64, vpu_begin_timestamp)
		__field(u64, vpu_end_timestamp)
		__field(u32, job_id)
		__field(u32, syncpt_id)
		__field(u32, threshold)
		__field(u32, vpu_id)
		__field(u32, queue_id)
	),
	TP_fast_assign(
		__entry->job_id    = job_id;
		__entry->syncpt_id = syncpt_id;
		__entry->threshold = threshold;
		__entry->vpu_begin_timestamp = vpu_begin_timestamp;
		__entry->vpu_end_timestamp = vpu_end_timestamp;
		__entry->queue_id  = queue_id;
		__entry->vpu_id    = vpu_id;
	),
	TP_printk("job_id=%u syncpt_id=%u threshold=%u vpu_id=%u "
		  "queue_id=%u vpu_begin=%llu vpu_end=%llu ",
		__entry->job_id, __entry->syncpt_id, __entry->threshold,
		__entry->vpu_id, __entry->queue_id, __entry->vpu_begin_timestamp,
		__entry->vpu_end_timestamp
	)
);

TRACE_EVENT(pva_job_ext_event,
	TP_PROTO(u32 job_id, u32 syncpt_id, u32 threshold, u32 vpu_id,
		 u64 queue_begin_timestamp, u64 queue_end_timestamp,
		 u64 prepare_begin_timestamp, u64 prepare_end_timestamp,
		 u64 vpu_begin_timestamp, u64 vpu_end_timestamp,
		 u64 post_begin_timestamp, u64 post_end_timestamp),
	TP_ARGS(job_id, syncpt_id, threshold, vpu_id,
		queue_begin_timestamp, queue_end_timestamp,
		prepare_begin_timestamp, prepare_end_timestamp,
		vpu_begin_timestamp, vpu_end_timestamp,
		post_begin_timestamp, post_end_timestamp),
	TP_STRUCT__entry(
		__field(u64, queue_begin_timestamp)
		__field(u64, queue_end_timestamp)
		__field(u64, prepare_begin_timestamp)
		__field(u64, prepare_end_timestamp)
		__field(u64, vpu_begin_timestamp)
		__field(u64, vpu_end_timestamp)
		__field(u64, post_begin_timestamp)
		__field(u64, post_end_timestamp)
		__field(u32, job_id)
		__field(u32, syncpt_id)
		__field(u32, threshold)
		__field(u32, vpu_id)
		__field(u32, queue_id)
	),
	TP_fast_assign(
		__entry->job_id    = job_id;
		__entry->syncpt_id = syncpt_id;
		__entry->threshold = threshold;
		__entry->queue_begin_timestamp = queue_begin_timestamp;
		__entry->queue_end_timestamp = queue_end_timestamp;
		__entry->prepare_begin_timestamp = prepare_begin_timestamp;
		__entry->prepare_end_timestamp = prepare_end_timestamp;
		__entry->vpu_begin_timestamp = vpu_begin_timestamp;
		__entry->vpu_end_timestamp = vpu_end_timestamp;
		__entry->post_begin_timestamp = post_begin_timestamp;
		__entry->post_end_timestamp = post_end_timestamp;
		__entry->queue_id  = (job_id >> 8);
		__entry->vpu_id    = vpu_id;
	),
	TP_printk("job_id=%u syncpt_id=%u threshold=%u vpu_id=%u queue_id=%u "
		  "queue_begin=%llu queue_end=%llu "
		  "prepare_begin=%llu prepare_end=%llu "
		  "vpu_begin=%llu vpu_end=%llu "
		  "post_begin=%llu post_end=%llu",
		  __entry->job_id, __entry->syncpt_id, __entry->threshold,
		  __entry->vpu_id, __entry->queue_id,
		  __entry->queue_begin_timestamp, __entry->queue_end_timestamp,
		  __entry->prepare_begin_timestamp, __entry->prepare_end_timestamp,
		  __entry->vpu_begin_timestamp, __entry->vpu_end_timestamp,
		  __entry->post_begin_timestamp, __entry->post_end_timestamp
	)
);

DECLARE_EVENT_CLASS(job_fence,
	TP_PROTO(u32 job_id, u32 syncpt_id, u32 threshold),
	TP_ARGS(job_id, syncpt_id, threshold),
	TP_STRUCT__entry(
		__field(u32, job_id)
		__field(u32, syncpt_id)
		__field(u32, threshold)
	),
	TP_fast_assign(
		__entry->job_id = job_id;
		__entry->syncpt_id = syncpt_id;
		__entry->threshold = threshold;
	),
	TP_printk("job_id=%u syncpt_id=%u threshold=%u",
		__entry->job_id, __entry->syncpt_id, __entry->threshold
	)
);

DEFINE_EVENT(job_fence, job_prefence,
	TP_PROTO(u32 job_id, u32 syncpt_id, u32 threshold),
	TP_ARGS(job_id, syncpt_id, threshold));

DEFINE_EVENT(job_fence, job_postfence,
	TP_PROTO(u32 job_id, u32 syncpt_id, u32 threshold),
	TP_ARGS(job_id, syncpt_id, threshold));

DECLARE_EVENT_CLASS(job_fence_semaphore,
	TP_PROTO(u32 job_id, u64 semaphore_id,
		 u32 semaphore_offset, u32 semaphore_value),
	TP_ARGS(job_id, semaphore_id, semaphore_offset, semaphore_value),
	TP_STRUCT__entry(
		__field(u64, semaphore_id)
		__field(u32, job_id)
		__field(u32, semaphore_offset)
		__field(u32, semaphore_value)
	),
	TP_fast_assign(
		__entry->job_id = job_id;
		__entry->semaphore_id = semaphore_id;
		__entry->semaphore_offset = semaphore_offset;
		__entry->semaphore_value = semaphore_value;
	),
	TP_printk("job_id=%u semaphore_id=%llu semaphore_offset=%u semaphore_value=%u",
		__entry->job_id, __entry->semaphore_id,
		__entry->semaphore_offset, __entry->semaphore_value
	)
);

DEFINE_EVENT(job_fence_semaphore, job_prefence_semaphore,
	TP_PROTO(u32 job_id, u64 semaphore_id,
		 u32 semaphore_offset, u32 semaphore_value),
	TP_ARGS(job_id, semaphore_id, semaphore_offset, semaphore_value));

DEFINE_EVENT(job_fence_semaphore, job_postfence_semaphore,
	TP_PROTO(u32 job_id, u64 semaphore_id,
		 u32 semaphore_offset, u32 semaphore_value),
	TP_ARGS(job_id, semaphore_id, semaphore_offset, semaphore_value));


TRACE_EVENT(job_timestamps,
	TP_PROTO(u32 job_id, u64 begin, u64 end),
	TP_ARGS(job_id, begin, end),
	TP_STRUCT__entry(
		__field(u32, job_id)
		__field(u64, begin)
		__field(u64, end)
	),
	TP_fast_assign(
		__entry->job_id = job_id;
		__entry->begin = begin;
		__entry->end = end;
	),
	TP_printk("job_id=%u begin=%llu end=%llu",
		__entry->job_id, __entry->begin, __entry->end
	)
);

TRACE_EVENT(nvpva_task_stats,
	TP_PROTO(
		const char *name,
		u64 queued_time,
		u64 head_time,
		u64 input_actions_complete,
		u64 vpu_assigned_time,
		u64 vpu_start_time,
		u64 vpu_complete_time,
		u64 complete_time,
		u8 vpu_assigned,
		u64 r5_overhead
		),
	TP_ARGS(
		name,
		queued_time,
		head_time,
		input_actions_complete,
		vpu_assigned_time,
		vpu_start_time,
		vpu_complete_time,
		complete_time,
		vpu_assigned,
		r5_overhead
		),
	TP_STRUCT__entry(
		__field(const char *, name)
		__field(u64, queued_time)
		__field(u64, head_time)
		__field(u64, input_actions_complete)
		__field(u64, vpu_assigned_time)
		__field(u64, vpu_start_time)
		__field(u64, vpu_complete_time)
		__field(u64, complete_time)
		__field(u8, vpu_assigned)
		__field(u64, r5_overhead)
		),
	TP_fast_assign(
		__entry->name = name;
		__entry->queued_time = queued_time;
		__entry->head_time = head_time;
		__entry->input_actions_complete = input_actions_complete;
		__entry->vpu_assigned_time = vpu_assigned_time;
		__entry->vpu_start_time = vpu_start_time;
		__entry->vpu_complete_time = vpu_complete_time;
		__entry->complete_time = complete_time;
		__entry->vpu_assigned = vpu_assigned;
		__entry->r5_overhead = r5_overhead;
		),
	TP_printk("%s\tqueued_time: %llu\thead_time: %llu\t"
		"input_actions_complete: %llu\tvpu_assigned_time: %llu\t"
		"vpu_start_time: %llu\tvpu_complete_time: %llu\t"
		"complete_time: %llu\tvpu_assigned: %d\t"
		"r5_overhead: %llu us",
		__entry->name, __entry->queued_time, __entry->head_time,
		__entry->input_actions_complete, __entry->vpu_assigned_time,
		__entry->vpu_start_time, __entry->vpu_complete_time,
		__entry->complete_time, __entry->vpu_assigned,
		__entry->r5_overhead)
);

TRACE_EVENT(nvpva_task_timestamp,
	TP_PROTO(
		const char *name,
		u32 class,
		u32 syncpoint_id,
		u32 syncpoint_thresh,
		u64 start_time,
		u64 end_time
		),
	TP_ARGS(
		name,
		class,
		syncpoint_id,
		syncpoint_thresh,
		start_time,
		end_time
		),
	TP_STRUCT__entry(
		__field(const char *, name)
		__field(u32, class)
		__field(u32, syncpoint_id)
		__field(u32, syncpoint_thresh)
		__field(u64, start_time)
		__field(u64, end_time)
		),
	TP_fast_assign(
		__entry->name = name;
		__entry->class = class;
		__entry->syncpoint_id = syncpoint_id;
		__entry->syncpoint_thresh = syncpoint_thresh;
		__entry->start_time = start_time;
		__entry->end_time = end_time;
		),
	TP_printk("name=%s, class=0x%02x, syncpoint_id=%u, syncpoint_thresh=%u, start_time=%llu, end_time=%llu",
		__entry->name, __entry->class, __entry->syncpoint_id, __entry->syncpoint_thresh,
		__entry->start_time, __entry->end_time)
);

TRACE_EVENT(nvpva_write,
	TP_PROTO(
		u64 delta_time,
		const char *name,
		u8 major,
		u8 minor,
		u8 flags,
		u8 sequence,
		u32 arg1,
		u32 arg2
		),
	TP_ARGS(
		delta_time,
		name,
		major,
		minor,
		flags,
		sequence,
		arg1,
		arg2
		),
	TP_STRUCT__entry(
		__field(u64, delta_time)
		__field(const char *, name)
		__field(u8, major)
		__field(u8, minor)
		__field(u8, flags)
		__field(u8, sequence)
		__field(u32, arg1)
		__field(u32, arg2)
		),
	TP_fast_assign(
		__entry->delta_time = delta_time;
		__entry->name = name;
		__entry->major = major;
		__entry->minor = minor;
		__entry->flags = flags;
		__entry->sequence = sequence;
		__entry->arg1 = arg1;
		__entry->arg2 = arg2;
		),
	TP_printk("time: %llu\t %s\t major: 0x%x\tminor: 0x%x\tflags: 0x%x\t"
		"sequence: 0x%x\targ1: %u\targ2: %u",
		__entry->delta_time, __entry->name, __entry->major,
		__entry->minor, __entry->flags, __entry->sequence,
		__entry->arg1, __entry->arg2)
);
#endif /* End of _TRACE_NVPVA_FTRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE nvpva_ftrace
#include <trace/define_trace.h>
