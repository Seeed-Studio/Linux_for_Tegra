/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2017-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <nvidia/conftest.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM camera_common

#if !defined(_TRACE_CAMERA_COMMON_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_CAMERA_COMMON_H

#include <linux/version.h>
#include <linux/tracepoint.h>

struct tegra_channel;
struct timespec64;

DECLARE_EVENT_CLASS(channel_simple,
	TP_PROTO(const char *name),
	TP_ARGS(name),
	TP_STRUCT__entry(
		__string(name,	name)
	),
	TP_fast_assign(
#if defined(NV___ASSIGN_STR_HAS_NO_SRC_ARG)
		__assign_str(name);
#else
		__assign_str(name, name);
#endif
	),
	TP_printk("%s", __get_str(name))
);

DEFINE_EVENT(channel_simple, tegra_channel_open,
	TP_PROTO(const char *name),
	TP_ARGS(name)
);

DEFINE_EVENT(channel_simple, tegra_channel_close,
	TP_PROTO(const char *name),
	TP_ARGS(name)
);

DEFINE_EVENT(channel_simple, tegra_channel_notify_status_callback,
	TP_PROTO(const char *name),
	TP_ARGS(name)
);

DECLARE_EVENT_CLASS(channel,
	TP_PROTO(const char *name, int num),
	TP_ARGS(name, num),
	TP_STRUCT__entry(
		__string(name,	name)
		__field(int,	num)
	),
	TP_fast_assign(
#if defined(NV___ASSIGN_STR_HAS_NO_SRC_ARG)
		__assign_str(name);
#else
		__assign_str(name, name);
#endif
		__entry->num = num;
	),
	TP_printk("%s : 0x%x", __get_str(name), (int)__entry->num)
);

DEFINE_EVENT(channel, tegra_channel_set_stream,
	TP_PROTO(const char *name, int num),
	TP_ARGS(name, num)
);

DEFINE_EVENT(channel, csi_s_stream,
	TP_PROTO(const char *name, int num),
	TP_ARGS(name, num)
);

DEFINE_EVENT(channel, tegra_channel_set_power,
	TP_PROTO(const char *name, int num),
	TP_ARGS(name, num)
);

DEFINE_EVENT(channel, camera_common_s_power,
	TP_PROTO(const char *name, int num),
	TP_ARGS(name, num)
);

DEFINE_EVENT(channel, csi_s_power,
	TP_PROTO(const char *name, int num),
	TP_ARGS(name, num)
);

TRACE_EVENT(tegra_channel_capture_setup,
	TP_PROTO(struct tegra_channel *chan, unsigned int index),
	TP_ARGS(chan, index),
	TP_STRUCT__entry(
		__field(unsigned int,	vnc_id)
		__field(unsigned int,	width)
		__field(unsigned int,	height)
		__field(unsigned int,	format)
	),
	TP_fast_assign(
		__entry->vnc_id = chan->vnc_id[index];
		__entry->width = chan->format.width;
		__entry->height = chan->format.height;
		__entry->format = chan->fmtinfo->img_fmt;
	),
	TP_printk("vnc_id %u W %u H %u fmt %x",
		  __entry->vnc_id, __entry->width, __entry->height,
		  __entry->format)
);

DECLARE_EVENT_CLASS(frame,
	TP_PROTO(const char *str, struct timespec64 *ts),
	TP_ARGS(str, ts),
	TP_STRUCT__entry(
		__string(str,	str)
		__field(long,	tv_sec)
		__field(long,	tv_nsec)
	),
	TP_fast_assign(
#if defined(NV___ASSIGN_STR_HAS_NO_SRC_ARG)
		__assign_str(str);
#else
		__assign_str(str, str);
#endif
		__entry->tv_sec = ts->tv_sec;
		__entry->tv_nsec = ts->tv_nsec;
	),
	TP_printk("%s:%ld.%ld", __get_str(str), __entry->tv_sec,
		  __entry->tv_nsec)
);

DEFINE_EVENT(frame, tegra_channel_capture_frame,
	TP_PROTO(const char *str, struct timespec64 *ts),
	TP_ARGS(str, ts)
);

DEFINE_EVENT(frame, tegra_channel_capture_done,
	TP_PROTO(const char *str, struct timespec64 *ts),
	TP_ARGS(str, ts)
);

TRACE_EVENT(vi_task_submit,
	TP_PROTO(u32 class_id, u32 channel_id, u32 syncpt_id,
		u32 syncpt_thresh, u32 pid, u32 tid),
	TP_ARGS(class_id, channel_id, syncpt_id, syncpt_thresh, pid, tid),
	TP_STRUCT__entry(
		__field(u32, class_id)
		__field(u32, channel_id)
		__field(u32, syncpt_id)
		__field(u32, syncpt_thresh)
		__field(u32, pid)
		__field(u32, tid)
	),
	TP_fast_assign(
		__entry->class_id = class_id;
		__entry->channel_id = channel_id;
		__entry->syncpt_id = syncpt_id;
		__entry->syncpt_thresh = syncpt_thresh;
		__entry->pid = pid;
		__entry->tid = tid;
	),
	TP_printk(
		"class_id:%u ch:%u syncpt_id:%u syncpt_thresh:%u pid:%u tid:%u",
		__entry->class_id,
		__entry->channel_id,
		__entry->syncpt_id,
		__entry->syncpt_thresh,
		__entry->pid,
		__entry->tid)
);

TRACE_EVENT(isp_task_submit,
	TP_PROTO(u32 class_id, u32 channel_id, u32 syncpt_id,
		u32 syncpt_thresh, u32 pid, u32 tid),
	TP_ARGS(class_id, channel_id, syncpt_id, syncpt_thresh, pid, tid),
	TP_STRUCT__entry(
		__field(u32, class_id)
		__field(u32, channel_id)
		__field(u32, syncpt_id)
		__field(u32, syncpt_thresh)
		__field(u32, pid)
		__field(u32, tid)
	),
	TP_fast_assign(
		__entry->class_id = class_id;
		__entry->channel_id = channel_id;
		__entry->syncpt_id = syncpt_id;
		__entry->syncpt_thresh = syncpt_thresh;
		__entry->pid = pid;
		__entry->tid = tid;
	),
	TP_printk(
		"class_id:%u ch:%u syncpt_id:%u syncpt_thresh:%u pid:%u tid:%u",
		__entry->class_id,
		__entry->channel_id,
		__entry->syncpt_id,
		__entry->syncpt_thresh,
		__entry->pid,
		__entry->tid)
);

TRACE_EVENT(camera_task_log,
	TP_PROTO(u32 class_id, u32 type,
		u64 timestamp, u32 pid, u32 tid),
	TP_ARGS(class_id, type, timestamp, pid, tid),
	TP_STRUCT__entry(
		__field(u32, class_id)
		__field(u32, type)
		__field(u64, timestamp)
		__field(u32, pid)
		__field(u32, tid)
	),
	TP_fast_assign(
		__entry->class_id = class_id;
		__entry->type = type;
		__entry->timestamp = timestamp;
		__entry->pid = pid;
		__entry->tid = tid;
	),
	TP_printk(
		"class_id:%u type:%u ts:%llu pid:%u tid:%u",
		__entry->class_id,
		__entry->type,
		__entry->timestamp,
		__entry->pid,
		__entry->tid)
);

#endif

/* This part must be outside protection */
#include <trace/define_trace.h>
