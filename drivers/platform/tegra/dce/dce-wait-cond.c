// SPDX-License-Identifier: MIT
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include <dce-os-cond.h>
#include <dce-wait-cond.h>
#include <dce-os-utils.h>
#include <dce-os-log.h>
#include <dce.h>

/*
 * dce_wait_cond_wait_interruptible : Wait for a given condition
 *
 * @d : Pointer to tegra_dce struct.
 * @wait : DCE OS wait condition to init.
 * @reset : Boolean input to indicate if to reset condition.
 *	If reset is set to true, clear wait->complete as soon as we exit from wait (consume wake).
 *	Client can use this, if they want the next dce_wait_cond_wait_interruptible call to
 *	not see the old wait->complete state.
 * @timeout_ms : Wait timeout in ms. 0 for no timeout.
 *
 * Return : 0 if successful, -ETIMEOUT on timeout, -ERESTARTSYS on interrupt.
 *	-EINVAL if invalid input args.
 *
 * Note: Since multiple clients wait on a broadcast event, the user is responsible
 *	to reset the condition only when all clients have consumed the call.
 */
int dce_wait_cond_wait_interruptible(struct tegra_dce *d, struct dce_wait_cond *wait,
			bool reset, u32 timeout_ms)
{
	int ret = 0;

	DCE_WARN_ON_NULL(d);

	if (timeout_ms == 0) {
		ret = DCE_OS_COND_WAIT_INTERRUPTIBLE(&wait->cond_wait,
				dce_os_atomic_read(&wait->complete) == 1);
	} else {
		ret = DCE_OS_COND_WAIT_INTERRUPTIBLE_TIMEOUT(&wait->cond_wait,
				dce_os_atomic_read(&wait->complete) == 1,
				timeout_ms);
		/**
		 * DCE_OS_COND_WAIT_INTERRUPTIBLE_TIMEOUT returns remaining jiffies
		 * if condition was evaluated to true before timeout.
		 * Set return value to SUCCESS in this case.
		 */
		if (ret > 0)
			ret = 0;
	}

	/* Skip reset if interrupted by a signal or any other error. */
	if ((ret == 0) && reset)
		dce_os_atomic_set(&wait->complete, 0);

	return ret;
}

/*
 * dce_wait_cond_signal_interruptible : Wakeup waiting task on given condition
 *
 * @d : Pointer to tegra_dce struct.
 * @wait : DCE OS wait condition to init.
 *
 * Return : void
 */
void dce_wait_cond_signal_interruptible(struct tegra_dce *d, struct dce_wait_cond *wait)
{
	DCE_WARN_ON_NULL(d);

	/*
	 * Set wait->complete to "1", so if the wait is called even after
	 * "dce_os_cond_signal_interruptible", it'll see the complete variable
	 * as "1" and exit the wait immediately.
	 */
	dce_os_atomic_set(&wait->complete, 1);
	dce_os_cond_signal_interruptible(&wait->cond_wait);
}

/*
 * dce_wait_cond_reset : reset condition wait variable to zero
 *
 * @d : Pointer to tegra_dce struct.
 * @wait : DCE OS wait condition to init.
 *
 * Return : void
 */
void dce_wait_cond_reset(struct tegra_dce *d, struct dce_wait_cond *wait)
{
	DCE_WARN_ON_NULL(d);

	dce_os_atomic_set(&wait->complete, 0);
}

/**
 * dce_wait_cond_init : Init DCE OS wait condition
 *
 * @d : Pointer to tegra_dce struct.
 * @wait : DCE OS wait condition to init.
 *
 * Return : 0 if successful else error code
 */
int dce_wait_cond_init(struct tegra_dce *d, struct dce_wait_cond *wait)
{
	int ret = 0;

	if (dce_os_cond_init(&wait->cond_wait)) {
		dce_os_err(d, "dce boot wait condition init failed");
		ret = -1;
		goto fail;
	}

	dce_os_atomic_set(&wait->complete, 0);

fail:
	return ret;
}

/**
 * dce_wait_cond_deinit : de-init dce workqueues related resources
 *
 * @d : Pointer to tegra_dce struct.
 * @wait : DCE OS wait condition to init.
 *
 * Return : void
 */
void dce_wait_cond_deinit(struct tegra_dce *d, struct dce_wait_cond *wait)
{
	DCE_WARN_ON_NULL(d);

	dce_os_atomic_set(&wait->complete, 0);
	dce_os_cond_destroy(&wait->cond_wait);
}

/**
 * dce_wait_cond_broadcast_interruptible - Signal all waiters of a condition
 * variable
 *
 * @d : Pointer to tegra_dce struct.
 * @wait : DCE OS wait condition to signal.
 *
 * Wake up all waiters for a condition variable.
 *
 * The waiters are using an interruptible wait.
 */
void dce_wait_cond_broadcast_interruptible(struct tegra_dce *d, struct dce_wait_cond *wait)
{
	DCE_WARN_ON_NULL(d);
	/*
	 * Set wait->complete to "1", so if the wait is called even after
	 * "dce_wait_cond_broadcast_interruptible", it'll see the complete variable
	 * as "1" and exit the wait immediately.
	 */
	dce_os_atomic_set(&wait->complete, 1);
	dce_os_cond_broadcast_interruptible(&wait->cond_wait);
}
