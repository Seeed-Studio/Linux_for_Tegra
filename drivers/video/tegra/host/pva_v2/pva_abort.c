// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2025, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/nvhost.h>
#include <linux/wait.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>

#include "pva.h"
#include "pva_sec_ec.h"
#include "pva_queue.h"


static void nvpva_debug_dump_device(struct pva *pva)
{
	pva_dump_queues(pva);
}

static void pva_abort_handler(struct work_struct *work)
{
	struct pva *pva = container_of(work, struct pva,
				       pva_abort_handler_work);
	struct platform_device *pdev = pva->pdev;
	int i;
	u32 checkpoint;

	/* show checkpoint value here*/
	checkpoint = host1x_readl(pdev,
		cfg_ccq_status_r(pva->version, 0, PVA_CCQ_STATUS6_INDEX));
	nvpva_warn(&pdev->dev, "Checkpoint value: 0x%08x",
		   checkpoint);

	/* Dump nvhost state to show the pending jobs */
	nvpva_debug_dump_device(pva);

	/* Copy trace points to ftrace buffer */
	pva_trace_copy_to_ftrace(pva);
	pva_fw_log_dump(pva, true);

	/*wake up sync cmd waiters*/
	for (i = 0; i < pva->version_config->irq_count; i++) {
		if (pva->cmd_status[i] == PVA_CMD_STATUS_WFI) {
			pva->cmd_status[i] = PVA_CMD_STATUS_ABORTED;
			wake_up(&pva->cmd_waitqueue[i]);
			schedule();
		}
	}

	/* lock mailbox mutex to avoid synchronous communication. */
	do {
		schedule();
	} while (mutex_trylock(&pva->mailbox_mutex) == false);

	/* There is no ongoing activity anymore. Update mailbox status */
	for (i = 0; i < pva->version_config->irq_count; i++)
		pva->cmd_status[i] = PVA_CMD_STATUS_INVALID;


	/* Lock CCQ mutex to avoid asynchornous communication */
	for (i = 0; i < MAX_PVA_INTERFACE; i++)
		mutex_lock(&pva->ccq_mutex[i]);

	/*
	 * If boot was still on-going, skip over recovery and let boot-up
	 * routine handle the failure
	 */
	if (!pva->booted) {
		nvpva_warn(&pdev->dev, "Recovery skipped: PVA not booted");
		goto skip_recovery;
	}

	/* disable error reporting to hsm*/
	pva_disable_ec_err_reporting(pva);

	/* Reset the PVA and reload firmware */
	nvhost_module_reset(pdev, true);

	/* enable error reporting to hsm*/
	pva_enable_ec_err_reporting(pva);

skip_recovery:

	/* Remove pending tasks from the queue */
	nvpva_queue_abort_all(pva->pool);

	if (pva->pva_power_on_err == 0)
		pva->in_recovery = false;

	for (i = 0; i < MAX_PVA_INTERFACE; i++)
		mutex_unlock(&pva->ccq_mutex[i]);

	mutex_unlock(&pva->mailbox_mutex);
	mutex_unlock(&pva->pva_fw_log_mutex);
	nvpva_warn(&pdev->dev, "Recovery finished");
	pva_recovery_release(pva);
}

void pva_recovery_release(struct pva *pva)
{
	u32 recovery_cnt;

	mutex_lock(&pva->recovery_mutex);
	recovery_cnt = atomic_read(&pva->recovery_cnt);
	if (recovery_cnt > 0)
		atomic_set(&pva->recovery_cnt, 0);

	mutex_unlock(&pva->recovery_mutex);
}

bool pva_recovery_acquire(struct pva *pva, struct mutex *mutex)
{
	bool acquired = true;
	u32 recovery_cnt;

	mutex_lock(&pva->recovery_mutex);
	recovery_cnt = atomic_read(&pva->recovery_cnt);
	if ((recovery_cnt > 0) || work_pending(&pva->pva_abort_handler_work))
		acquired = false;
	else
		if(mutex == NULL)
			atomic_set(&pva->recovery_cnt, 1);
		else
			mutex_lock(mutex);

	mutex_unlock(&pva->recovery_mutex);

	return acquired;
}

void pva_abort(struct pva *pva)
{
	/* If recovery pending, ignore concurrent request */
	if (!pva_recovery_acquire(pva, NULL)) {
		WARN(true, "Recovery request while pending ignored.");
		return;
	}

	pva->in_recovery = true;
	WARN(true, "Attempting to recover the engine");
	schedule_work(&pva->pva_abort_handler_work);
}

void pva_abort_init(struct pva *pva)
{
	INIT_WORK(&pva->pva_abort_handler_work, pva_abort_handler);
}
