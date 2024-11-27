// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <dce.h>

int dce_waiters_init(struct tegra_dce *d)
{
	int ret = 0;
	int i;

	if (dce_os_cond_init(&d->dce_bootstrap_done)) {
		dce_os_err(d, "dce boot wait condition init failed");
		ret = -1;
		goto exit;
	}

	for (i = 0; i < DCE_MAX_WAIT; i++) {
		struct dce_wait_cond *wait = &d->ipc_waits[i];

		if (dce_wait_cond_init(d, wait)) {
			dce_os_err(d, "dce wait condition %d init failed", i);
			ret = -1;
			goto init_error;
		}
	}

	return 0;
init_error:
	while (i >= 0) {
		struct dce_wait_cond *wait = &d->ipc_waits[i];

		dce_wait_cond_deinit(d, wait);
		i--;
	}
	dce_os_cond_destroy(&d->dce_bootstrap_done);
exit:
	return ret;
}

void dce_waiters_deinit(struct tegra_dce *d)
{
	int i;

	for (i = 0; i < DCE_MAX_WAIT; i++) {
		struct dce_wait_cond *wait = &d->ipc_waits[i];

		dce_wait_cond_deinit(d, wait);
	}

	dce_os_cond_destroy(&d->dce_bootstrap_done);
}
