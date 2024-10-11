// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <dce.h>
#include <os-cond.h>
#include <os-lock.h>
#include <dce-worker.h>
#include <dce-os-utils.h>
#include <interface/dce-admin-cmds.h>

/*
 * dce_wait_interruptible : Wait for a given condition
 *
 * @d : Pointer to tegra_dce struct.
 * @msg_id : index of wait condition
 *
 * Return : 0 if successful else error code
 */
int dce_wait_interruptible(struct tegra_dce *d, u32 msg_id)
{
	struct dce_wait_cond *wait;

	if (msg_id >= DCE_MAX_WAIT) {
		dce_os_err(d, "Invalid wait requested %u", msg_id);
		return -EINVAL;
	}

	wait = &d->ipc_waits[msg_id];

	/*
	 * It is possible that we received the ACK from DCE even before we
	 * start waiting. But that should not be an issue as wait->complete
	 * Will be "1" and we immediately exit from the wait.
	 */
	DCE_COND_WAIT_INTERRUPTIBLE(&wait->cond_wait,
			dce_os_atomic_read(&wait->complete) == 1);

	if (dce_os_atomic_read(&wait->complete) != 1)
		return -EINTR;

	/*
	 * Clear wait->complete as soon as we exit from wait (consume the wake call)
	 * So that when the next dce_wait_interruptible is called, it doesn't see old
	 * wait->complete state.
	 */
	dce_os_atomic_set(&wait->complete, 0);
	return 0;
}

/*
 * dce_wakeup_interruptible : Wakeup waiting task on given condition
 *
 * @d : Pointer to tegra_dce struct.
 * @msg_id : index of wait condition
 *
 * Return : void
 */
void dce_wakeup_interruptible(struct tegra_dce *d, u32 msg_id)
{
	struct dce_wait_cond *wait;

	if (msg_id >= DCE_MAX_WAIT) {
		dce_os_err(d, "Invalid wait requested %u", msg_id);
		return;
	}

	wait = &d->ipc_waits[msg_id];

	/*
	 * Set wait->complete to "1", so if the wait is called even after
	 * "dce_cond_signal_interruptible", it'll see the complete variable
	 * as "1" and exit the wait immediately.
	 */
	dce_os_atomic_set(&wait->complete, 1);
	dce_cond_signal_interruptible(&wait->cond_wait);
}

/*
 * dce_cond_wait_reset : reset condition wait variable to zero
 *
 * @d : Pointer to tegra_dce struct.
 * @msg_id : index of wait condition
 *
 * Return : void
 */
void dce_cond_wait_reset(struct tegra_dce *d, u32 msg_id)
{
	struct dce_wait_cond *wait;

	if (msg_id >= DCE_MAX_WAIT) {
		dce_os_err(d, "Invalid wait requested %u", msg_id);
		return;
	}

	wait = &d->ipc_waits[msg_id];
	dce_os_atomic_set(&wait->complete, 0);
}

/**
 * dce_work_cond_sw_resource_init : Init dce workqueues related resources
 *
 * @d : Pointer to tegra_dce struct.
 *
 * Return : 0 if successful else error code
 */
int dce_work_cond_sw_resource_init(struct tegra_dce *d)
{
	int ret = 0;
	int i;

	if (dce_cond_init(&d->dce_bootstrap_done)) {
		dce_os_err(d, "dce boot wait condition init failed");
		ret = -1;
		goto exit;
	}

	for (i = 0; i < DCE_MAX_WAIT; i++) {
		struct dce_wait_cond *wait = &d->ipc_waits[i];

		if (dce_cond_init(&wait->cond_wait)) {
			dce_os_err(d, "dce wait condition %d init failed", i);
			ret = -1;
			goto init_error;
		}

		dce_os_atomic_set(&wait->complete, 0);
	}

	return 0;
init_error:
	while (i >= 0) {
		struct dce_wait_cond *wait = &d->ipc_waits[i];

		dce_cond_destroy(&wait->cond_wait);
		i--;
	}
	dce_cond_destroy(&d->dce_bootstrap_done);
exit:
	return ret;
}

/**
 * dce_work_cond_sw_resource_deinit : de-init dce workqueues related resources
 *
 * @d : Pointer to tegra_dce struct.
 *
 * Return : void
 */
void dce_work_cond_sw_resource_deinit(struct tegra_dce *d)
{
	int i;

	for (i = 0; i < DCE_MAX_WAIT; i++) {
		struct dce_wait_cond *wait = &d->ipc_waits[i];

		dce_cond_destroy(&wait->cond_wait);
		dce_os_atomic_set(&wait->complete, 0);
	}

	dce_cond_destroy(&d->dce_bootstrap_done);
}
