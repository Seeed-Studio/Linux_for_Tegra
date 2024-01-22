/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM tegra_capture

#if !defined(_TRACE_TEGRA_CAPTURE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_TEGRA_CAPTURE_H

#include <linux/tracepoint.h>

/*
 * Classes
 */
#ifndef IVC_NAME_LEN
#define IVC_NAME_LEN	16
#endif

DECLARE_EVENT_CLASS(capture__msg,
	TP_PROTO(const char *ivc_name, u32 msg_id, u32 ch_id),
	TP_ARGS(ivc_name, msg_id, ch_id),
	TP_STRUCT__entry(
		__array(char, ivc_name, IVC_NAME_LEN)
		__field(u32, msg_id)
		__field(u32, ch_id)
	),
	TP_fast_assign(
		strscpy(__entry->ivc_name, ivc_name, sizeof(__entry->ivc_name));
		__entry->msg_id = msg_id;
		__entry->ch_id = ch_id;
	),
	TP_printk("ivc:\"%s\" msg:0x%02x ch:0x%02x",
		__entry->ivc_name,
		__entry->msg_id,
		__entry->ch_id)
);

/*
 * Events for capture and capture control protocol
 */
TRACE_EVENT(capture_ivc_notify,
	TP_PROTO(const char *ivc_name),
	TP_ARGS(ivc_name),
	TP_STRUCT__entry(
		__array(char, ivc_name, IVC_NAME_LEN)
	),
	TP_fast_assign(
		strscpy(__entry->ivc_name, ivc_name, sizeof(__entry->ivc_name));
	),
	TP_printk("ivc:\"%s\"", __entry->ivc_name)
);

DEFINE_EVENT(capture__msg, capture_ivc_recv,
	TP_PROTO(const char *ivc_name, u32 msg_id, u32 ch_id),
	TP_ARGS(ivc_name, msg_id, ch_id)
);

DEFINE_EVENT(capture__msg, capture_ivc_send,
	TP_PROTO(const char *ivc_name, u32 msg_id, u32 ch_id),
	TP_ARGS(ivc_name, msg_id, ch_id)
);

TRACE_EVENT(capture_ivc_send_error,
	TP_PROTO(const char *ivc_name, u32 msg_id, u32 ch_id, int err),
	TP_ARGS(ivc_name, msg_id, ch_id, err),
	TP_STRUCT__entry(
		__array(char, ivc_name, IVC_NAME_LEN)
		__field(u32, msg_id)
		__field(u32, ch_id)
		__field(int, err)
	),
	TP_fast_assign(
		strscpy(__entry->ivc_name, ivc_name, sizeof(__entry->ivc_name));
		__entry->msg_id = msg_id;
		__entry->ch_id = ch_id;
		__entry->err = err;
	),
	TP_printk("ivc:\"%s\" msg:0x%02x ch:0x%02x: err:%d",
		__entry->ivc_name,
		__entry->msg_id,
		__entry->ch_id,
		__entry->err)
);

/*
 * Capture scheduler events from RCE
 */
DECLARE_EVENT_CLASS(capture__progress_event,
	TP_PROTO(u64 ts, u32 channel_id, u32 sequence),
	TP_ARGS(ts, channel_id, sequence),
	TP_STRUCT__entry(
		__field(u64, ts)
		__field(u32, channel_id)
		__field(u32, sequence)
	),
	TP_fast_assign(
		__entry->ts = ts;
		__entry->channel_id = channel_id;
		__entry->sequence = sequence;
	),
	TP_printk("ts:%llu ch:0x%02x seq:%u",
		__entry->ts,
		__entry->channel_id,
		__entry->sequence)
);

DECLARE_EVENT_CLASS(capture__isp_event,
	TP_PROTO(u64 ts, u32 channel_id, u32 cap_sequence, u32 prog_sequence,
		u8 isp_settings_id, u8 vi_channel_id),
	TP_ARGS(ts, channel_id, cap_sequence, prog_sequence, isp_settings_id, vi_channel_id),
	TP_STRUCT__entry(
		__field(u64, ts)
		__field(u32, channel_id)
		__field(u32, prog_sequence)
		__field(u32, cap_sequence)
		__field(u8, isp_settings_id)
		__field(u8, vi_channel_id)
	),
	TP_fast_assign(
		__entry->ts = ts;
		__entry->channel_id = channel_id;
		__entry->prog_sequence = prog_sequence;
	),
	TP_printk("ts:%llu ch:0x%02x seq:%u prog:%u set:%u vi:%u",
		__entry->ts,
		__entry->channel_id,
		__entry->cap_sequence,
		__entry->prog_sequence,
		__entry->isp_settings_id,
		__entry->vi_channel_id)
);

DECLARE_EVENT_CLASS(capture__suspend_event,
	TP_PROTO(u64 ts, bool suspend),
	TP_ARGS(ts, suspend),
	TP_STRUCT__entry(
		__field(u64, ts)
		__field(bool, suspend)
	),
	TP_fast_assign(
		__entry->ts = ts;
		__entry->suspend = suspend;
	),
	TP_printk("ts:%llu suspend:%s",
		__entry->ts,
		__entry->suspend ? "true" : "false")
);

DEFINE_EVENT(capture__progress_event, capture_event_sof,
	TP_PROTO(u64 ts, u32 channel_id, u32 sequence),
	TP_ARGS(ts, channel_id, sequence)
);

DEFINE_EVENT(capture__progress_event, capture_event_eof,
	TP_PROTO(u64 ts, u32 channel_id, u32 sequence),
	TP_ARGS(ts, channel_id, sequence)
);

DEFINE_EVENT(capture__progress_event, capture_event_error,
	TP_PROTO(u64 ts, u32 channel_id, u32 sequence),
	TP_ARGS(ts, channel_id, sequence)
);

DEFINE_EVENT(capture__progress_event, capture_event_reschedule,
	TP_PROTO(u64 ts, u32 channel_id, u32 sequence),
	TP_ARGS(ts, channel_id, sequence)
);

DEFINE_EVENT(capture__isp_event, capture_event_reschedule_isp,
	TP_PROTO(u64 ts, u32 channel_id, u32 cap_sequence, u32 prog_sequence,
		u8 isp_settings_id, u8 vi_channel_id),
	TP_ARGS(ts, channel_id, cap_sequence, prog_sequence, isp_settings_id, vi_channel_id)
);

DEFINE_EVENT(capture__isp_event, capture_event_isp_done,
	TP_PROTO(u64 ts, u32 channel_id, u32 cap_sequence, u32 prog_sequence,
		u8 isp_settings_id, u8 vi_channel_id),
	TP_ARGS(ts, channel_id, cap_sequence, prog_sequence, isp_settings_id, vi_channel_id)
);

DEFINE_EVENT(capture__isp_event, capture_event_isp_error,
	TP_PROTO(u64 ts, u32 channel_id, u32 cap_sequence, u32 prog_sequence,
		u8 isp_settings_id, u8 vi_channel_id),
	TP_ARGS(ts, channel_id, cap_sequence, prog_sequence, isp_settings_id, vi_channel_id)
);

DEFINE_EVENT(capture__progress_event, capture_event_report_program,
	TP_PROTO(u64 ts, u32 channel_id, u32 sequence),
	TP_ARGS(ts, channel_id, sequence)
);

TRACE_EVENT(capture_event_wdt,
	TP_PROTO(u64 ts),
	TP_ARGS(ts),
	TP_STRUCT__entry(
		__field(u64, ts)
	),
	TP_fast_assign(
		__entry->ts = ts;
	),
	TP_printk("ts:%llu",
		__entry->ts)
);

DEFINE_EVENT(capture__suspend_event, capture_event_suspend,
	TP_PROTO(u64 ts, bool suspend),
	TP_ARGS(ts, suspend)
);

DEFINE_EVENT(capture__suspend_event, capture_event_suspend_isp,
	TP_PROTO(u64 ts, bool suspend),
	TP_ARGS(ts, suspend)
);

#endif /* _TRACE_TEGRA_CAPTURE_H */

#include <trace/define_trace.h>
