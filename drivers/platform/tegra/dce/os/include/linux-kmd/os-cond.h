/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#ifndef NVDISPLAY_SERVER_OS_COND_LINUX_H
#define NVDISPLAY_SERVER_OS_COND_LINUX_H

#include <linux/wait.h>
#include <linux/sched.h>

struct dce_cond {
    bool initialized;
    wait_queue_head_t wq;
};

/**
 * DCE_COND_WAIT - Wait for a condition to be true
 *
 * @c - The condition variable to sleep on
 * @condition - The condition that needs to be true
 *
 * Wait for a condition to become true.
 */
#define DCE_COND_WAIT(c, condition) \
({\
    int ret = 0; \
    wait_event((c)->wq, condition); \
    ret;\
})

/**
 * DCE_COND_WAIT_INTERRUPTIBLE - Wait for a condition to be true
 *
 * @c - The condition variable to sleep on
 * @condition - The condition that needs to be true
 *
 * Wait for a condition to become true. Returns -ERESTARTSYS
 * on signal.
 */
#define DCE_COND_WAIT_INTERRUPTIBLE(c, condition) \
({ \
    int ret = 0; \
    ret = wait_event_interruptible((c)->wq, condition); \
    ret; \
})

/**
 * DCE_COND_WAIT_TIMEOUT - Wait for a condition to be true
 *
 * @c - The condition variable to sleep on
 * @condition - The condition that needs to be true
 * @timeout_ms - Timeout in milliseconds, or 0 for infinite wait.
 *               This parameter must be a u32. Since this is a macro, this is
 *               enforced by assigning a typecast NULL pointer to a u32 tmp
 *               variable which will generate a compiler warning (or error if
 *               the warning is configured as an error).
 *
 * Wait for a condition to become true. Returns -ETIMEOUT if
 * the wait timed out with condition false.
 */
#define DCE_COND_WAIT_TIMEOUT(c, condition, timeout_ms) \
({\
    int ret = 0; \
    /* This is the assignment to enforce a u32 for timeout_ms */ \
    u32 *tmp = (typeof(timeout_ms) *)NULL; \
    (void)tmp; \
    if (timeout_ms > 0U) { \
        long _ret = wait_event_timeout((c)->wq, condition, \
                        msecs_to_jiffies(timeout_ms)); \
        if (_ret == 0) \
            ret = -ETIMEDOUT; \
    } else { \
        wait_event((c)->wq, condition); \
    } \
    ret;\
})

/**
 * DCE_COND_WAIT_INTERRUPTIBLE_TIMEOUT - Wait for a condition to be true
 *
 * @c - The condition variable to sleep on
 * @condition - The condition that needs to be true
 * @timeout_ms - Timeout in milliseconds, or 0 for infinite wait.
 *               This parameter must be a u32. Since this is a macro, this is
 *               enforced by assigning a typecast NULL pointer to a u32 tmp
 *               variable which will generate a compiler warning (or error if
 *               the warning is configured as an error).
 *
 * Wait for a condition to become true. Returns -ETIMEOUT if
 * the wait timed out with condition false or -ERESTARTSYS on
 * signal.
 */
#define DCE_COND_WAIT_INTERRUPTIBLE_TIMEOUT(c, condition, timeout_ms) \
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
        else if (_ret == -ERESTARTSYS) \
            ret = -ERESTARTSYS; \
    } else { \
        ret = wait_event_interruptible((c)->wq, condition); \
    } \
    ret; \
})

int dce_cond_init(struct dce_cond *cond);

void dce_cond_signal(struct dce_cond *cond);

void dce_cond_signal_interruptible(struct dce_cond *cond);

int dce_cond_broadcast(struct dce_cond *cond);

int dce_cond_broadcast_interruptible(struct dce_cond *cond);

void dce_cond_destroy(struct dce_cond *cond);

#endif /* NVDISPLAY_SERVER_OS_COND_LINUX_H */