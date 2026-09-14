/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2023 NVIDIA CORPORATION.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef DCE_ADMIN_PERF_STATS_H
#define DCE_ADMIN_PERF_STATS_H

/*
 * Values for stats field
 */
#define DCE_ADMIN_PERF_STATS_SCHED	0x0001
#define DCE_ADMIN_PERF_STATS_CACHE	0x0002
#define DCE_ADMIN_PERF_STATS_IEXEC	0x0003
#define DCE_ADMIN_PERF_STATS_MMIO	0x0004
#define DCE_ADMIN_PERF_STATS_ALL	0xFFFF

/*
 * Values for clear field
 */
#define DCE_ADMIN_PERF_CLEAR_RETAIN	0U
#define DCE_ADMIN_PERF_CLEAR_CLEAR	1U

#define DCE_ADMIN_EVENT_CLEAR_RETAIN	0U
#define DCE_ADMIN_EVENT_CLEAR_CLEAR	1U

struct dce_admin_perf_cmd_args {
	uint32_t	enable;   // Enable stats capture through Bit-Mask
	uint32_t	clear;    // Clear all event stats
};

struct dce_admin_perf_args {
	struct dce_admin_perf_cmd_args		perf_cmd;
	struct dce_admin_perf_cmd_args		event_cmd;
};

/*
 * Task related defines for perf stats
 */
#define DCE_ADMIN_PERF_ADMIN_TASK		0U
#define DCE_ADMIN_PERF_RM_TASK			1U
#define DCE_ADMIN_PERF_PRINT_TASK		2U
#define DCE_ADMIN_PERF_TCU_TASK			3U
#define DCE_ADMIN_PERF_IDLE_TASK		4U
// Change the active task number on adding more tasks
#define DCE_ADMIN_PERF_ACTIVE_TASKS_NUM		5U
// Max number of tasks in perf stats capture
#define DCE_ADMIN_PERF_NUM_TASKS		10U
// Task name length
#define DCE_ADMIN_TASK_NAME_LEN                 16U

struct dce_admin_stat_avg {
	uint64_t		accumulate;       // accumulated delta times
	uint64_t		iterations;
	uint64_t		min;
	uint64_t		max;
};

struct dce_admin_task_stats {
	const char		name[DCE_ADMIN_TASK_NAME_LEN];    // task name
	uint64_t		init;           // how long to run init code
	uint64_t		ready;          // how long for task to be ready
	uint64_t		ready_time;     // time when task became ready
	uint64_t		dcache_misses;  // dcaches missed count accumulated within this task
	uint64_t		inst_exec;      // instruction execution accumulated
	uint64_t		mmio_req;       // memory request accumulated
	struct dce_admin_stat_avg	sleep;
	struct dce_admin_stat_avg	run;
	struct dce_admin_stat_avg	run_context; // running context
};

struct dce_admin_sched_stats {
	uint64_t		start;            // time collection started
	uint64_t		end;              // time collection stopped
	uint64_t		context_switches; // overall context switches
	struct dce_admin_task_stats	tasks[DCE_ADMIN_PERF_NUM_TASKS];
};

/*
 * Performance monitor events and  counters
 */
#define DCE_ADMIN_PM_EVT_DCACHE_MISSES	0U
#define DCE_ADMIN_PM_EVT_INSTR_EXEC	1U
#define DCE_ADMIN_PM_EVT_MEM_REQ	2U
#define DCE_ADMIN_PM_EVT_COUNTERS	3U

struct dce_admin_pm_event_stats {
	uint64_t		start;   // time collection started
	uint64_t		end;     // time collection stopped
	uint64_t		count;   // Event counter accumulated
};

struct dce_admin_perf_info {
	uint64_t			tcm_ready;      // tcm is ready/copied
	uint64_t			sched_start;    // scheduler started
	uint64_t			ready_time;     // signaled ready
	struct dce_admin_pm_event_stats	pm_events[DCE_ADMIN_PM_EVT_COUNTERS];
	struct dce_admin_sched_stats	sched;
};

#define DCE_ADMIN_EVENT_NAME_LEN	32U      // max length of an event name
#define DCE_ADMIN_NUM_EVENTS		20U      // s.b. same as DCE_NUM_EVENTS

struct dce_admin_event {
	char				name[DCE_ADMIN_EVENT_NAME_LEN];
	struct dce_admin_stat_avg	event;
};

struct dce_admin_event_info {
	struct dce_admin_event		events[DCE_ADMIN_NUM_EVENTS];
};


struct dce_admin_perf_stats_info {
	union {
		struct dce_admin_perf_info	sched_stats;
		struct dce_admin_event_info	events_stats;
	} info;
};
#endif
