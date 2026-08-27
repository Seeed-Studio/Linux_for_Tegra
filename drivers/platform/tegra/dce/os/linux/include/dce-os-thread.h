/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef DCE_OS_THREAD_H
#define DCE_OS_THREAD_H

#include <linux/sched.h>

struct task_struct;

struct dce_thread {
	struct task_struct *task;
	bool running;
	int (*fn)(void *data);
	void *data;
};

struct dce_thread;

int dce_os_thread_create(struct dce_thread *thread,
		void *data,
		int (*threadfn)(void *data), const char *name);

void dce_os_thread_stop(struct dce_thread *thread);

bool dce_os_thread_should_stop(struct dce_thread *thread);

bool dce_os_thread_is_running(struct dce_thread *thread);

void dce_os_thread_join(struct dce_thread *thread);

#endif /* DCE_OS_THREAD_H */
