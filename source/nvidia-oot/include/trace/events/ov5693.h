/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2017-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
/*
 * ov5693.h
 */

#include <nvidia/conftest.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM ov5693

#if !defined(_TRACE_OV5693_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_OV5693_H

#include <linux/tracepoint.h>

TRACE_EVENT(ov5693_s_stream,
	TP_PROTO(const char *name, int enable, int mode),
	TP_ARGS(name, enable, mode),
	TP_STRUCT__entry(
		__string(name,	name)
		__field(int,	enable)
		__field(int,	mode)
	),
	TP_fast_assign(
#if defined(NV___ASSIGN_STR_HAS_NO_SRC_ARG)
		__assign_str(name);
#else
		__assign_str(name, name);
#endif
		__entry->enable = enable;
		__entry->mode = mode;
	),
	TP_printk("%s: on %d mode %d", __get_str(name),
		  __entry->enable, __entry->mode)
);


#endif

/* This part must be outside protection */
#include <trace/define_trace.h>
