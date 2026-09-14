/*
 * cpuidle event logging to ftrace.
 *
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM cpuidle_debugfs_ftrace

#if !defined(_TRACE_CPUIDLE_DEBUGFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_CPUIDLE_DEBUGFS_H

#include <linux/ktime.h>
#include <linux/tracepoint.h>

TRACE_EVENT(cpuidle_debugfs_print,
	TP_PROTO(
		const char *str
	),

	TP_ARGS(str),

	TP_STRUCT__entry(
		__field(const char *, str)
	),

	TP_fast_assign(
		__entry->str = str;
	),

	TP_printk("%s",
		__entry->str
	)
);

#endif /* _TRACE_CPUIDLE_DEBUGFS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
