// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, NVIDIA Corporation.  All rights reserved.
 *
 * NVDLA event logging to ftrace
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM nvdla_ftrace

#if !defined(_TRACE_NVDLA_FTRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NVDLA_FTRACE_H

#include <linux/tracepoint.h>
#include <linux/device.h>

TRACE_EVENT(job_submit,
	TP_PROTO(struct device *dev, u32 class_id, u32 job_id, u32 num_fences, u64 hw_timestamp),
	TP_ARGS(dev, class_id, job_id, num_fences, hw_timestamp),
	TP_STRUCT__entry(
		__field(struct device *, dev)
		__field(u32, class_id)
		__field(u32, job_id)
		__field(u32, num_fences)
		__field(u64, hw_timestamp)
	),
	TP_fast_assign(
		__entry->dev = dev;
		__entry->class_id = class_id;
		__entry->job_id = job_id;
		__entry->num_fences = num_fences;
		__entry->hw_timestamp = hw_timestamp;
	),
	TP_printk("%s class=%02x id=%u fences=%u ts=%llu",
		dev_name(__entry->dev), __entry->class_id, __entry->job_id,
		__entry->num_fences, __entry->hw_timestamp
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
#endif /* End of _TRACE_NVDLA_FTRACE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
