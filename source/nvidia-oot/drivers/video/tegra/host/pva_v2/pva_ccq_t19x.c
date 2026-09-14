// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * PVA Command Queue Interface handling
 */

#include "pva-interface.h"
#include <linux/kernel.h>
#include <linux/nvhost.h>
#include <linux/delay.h>

#include <linux/version.h>
#if KERNEL_VERSION(4, 15, 0) > LINUX_VERSION_CODE
#include <soc/tegra/chip-id.h>
#else
#include <soc/tegra/fuse.h>
#endif

#include "pva.h"
#include "pva_ccq_t19x.h"

#include "pva_regs.h"
#include "pva-interface.h"

#define MAX_CCQ_ELEMENTS 6

static int pva_ccq_wait(struct pva *pva, int timeout)
{
	unsigned long end_jiffies = jiffies + msecs_to_jiffies(timeout);

	/*
	 * Wait until there is free room in the CCQ. Otherwise the writes
	 * could stall the CPU. Ignore the timeout in simulation.
	 */

	do {
		u32 val = host1x_readl(pva->pdev,
				   cfg_ccq_status_r(pva->version, 0,
				   PVA_CCQ_STATUS2_INDEX));

		if (val <= MAX_CCQ_ELEMENTS)
			return 0;

		if ((pva->timeout_enabled == true) && time_after(jiffies, end_jiffies)) {
			/* check once more if a slot is available */
			val = host1x_readl(pva->pdev,
					       cfg_ccq_status_r(pva->version, 0,
					       PVA_CCQ_STATUS2_INDEX));
			if (val <= MAX_CCQ_ELEMENTS)
				return 0;
			else
				return -ETIMEDOUT;
		} else {
			usleep_range(5, 10);
		}
	} while (true);

}

int pva_ccq_send_task_t19x(struct pva *pva, u32 queue_id, dma_addr_t task_addr,
			   u8 batchsize, u8 *task_status, u32 flags)
{
	int err = 0;
	struct pva_cmd_s cmd = {0};

	(void)pva_cmd_submit_batch(&cmd, queue_id, task_addr, batchsize, flags);

	mutex_lock(&pva->ccq_mutex[1]);
	err = pva_ccq_wait(pva, 100);
	if (err < 0)
		goto err_wait_ccq;

	/* Make the writes to CCQ */
	host1x_writel(pva->pdev, cfg_ccq_r(pva->version, 0), cmd.cmd_field[1]);
	host1x_writel(pva->pdev, cfg_ccq_r(pva->version, 0), cmd.cmd_field[0]);

	mutex_unlock(&pva->ccq_mutex[1]);

	return err;

err_wait_ccq:
	mutex_unlock(&pva->ccq_mutex[1]);
	pva_abort(pva);

	return err;
}
