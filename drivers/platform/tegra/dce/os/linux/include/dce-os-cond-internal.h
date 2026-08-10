/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef DCE_OS_COND_INTERNAL_H
#define DCE_OS_COND_INTERNAL_H

#include <linux/wait.h>
#include <linux/sched.h>

struct dce_os_cond {
	bool initialized;
	wait_queue_head_t wq;
};

#define DCE_OS_COND_WAIT_INTERRUPTIBLE_INTERNAL(c, condition) \
({ \
	int ret = 0; \
	ret = wait_event_interruptible((c)->wq, condition); \
	ret; \
})

#define DCE_OS_COND_WAIT_INTERRUPTIBLE_TIMEOUT_INTERNAL(c, condition, timeout_ms) \
({ \
	int ret = 0; \
	/* This is the assignment to enforce a u32 for timeout_ms */ \
	u32 *tmp = (typeof(timeout_ms) *)NULL; \
	(void)tmp; \
	if (timeout_ms > 0U) { \
		long _ret = wait_event_interruptible_timeout((c)->wq, \
				condition, msecs_to_jiffies(timeout_ms)); \
		if (_ret == 0) \
			ret = -ETIMEDOUT; \
		else if (_ret < 0) \
			ret = _ret; \
	} else { \
		ret = wait_event_interruptible((c)->wq, condition); \
	} \
	ret; \
})

#endif /* DCE_OS_COND_INTERNAL_H */
