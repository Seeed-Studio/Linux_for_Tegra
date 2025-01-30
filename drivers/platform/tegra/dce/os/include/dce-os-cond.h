/* SPDX-License-Identifier: MIT */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef DCE_OS_COND_H
#define DCE_OS_COND_H

#include <dce-os-cond-internal.h>

/**
 * DCE_OS_COND_WAIT_INTERRUPTIBLE - Wait for a condition to be true
 *
 * @c - The condition variable to sleep on
 * @condition - The condition that needs to be true
 *
 * Wait for a condition to become true. Returns -ERESTARTSYS
 * on signal.
 */
#define DCE_OS_COND_WAIT_INTERRUPTIBLE(c, condition) \
	DCE_OS_COND_WAIT_INTERRUPTIBLE_INTERNAL(c, condition)

/**
 * DCE_OS_COND_WAIT_INTERRUPTIBLE_TIMEOUT - Wait for a condition to be true
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
#define DCE_OS_COND_WAIT_INTERRUPTIBLE_TIMEOUT(c, condition, timeout_ms) \
	DCE_OS_COND_WAIT_INTERRUPTIBLE_TIMEOUT_INTERNAL(c, condition, timeout_ms)

/**
 * dce_os_cond_init - Initialize a condition variable
 *
 * @cond - The condition variable to initialize
 *
 * Initialize a condition variable before using it.
 */
int dce_os_cond_init(struct dce_os_cond *cond);

/**
 * dce_os_cond_destroy - Destroy a condition variable
 *
 * @cond - The condition variable to destroy
 */
void dce_os_cond_signal_interruptible(struct dce_os_cond *cond);

/**
 * dce_os_cond_signal_interruptible - Signal a condition variable
 *
 * @cond - The condition variable to signal
 *
 * Wake up a waiter for a condition variable to check if its condition has been
 * satisfied.
 *
 * The waiter is using an interruptible wait.
 */
int dce_os_cond_broadcast_interruptible(struct dce_os_cond *cond);

/**
 * dce_os_cond_broadcast_interruptible - Signal all waiters of a condition
 * variable
 *
 * @cond - The condition variable to signal
 *
 * Wake up all waiters for a condition variable to check if their conditions
 * have been satisfied.
 *
 * The waiters are using an interruptible wait.
 */
void dce_os_cond_destroy(struct dce_os_cond *cond);

#endif /* DCE_OS_COND_H */
