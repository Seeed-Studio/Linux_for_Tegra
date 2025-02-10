// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2023, NVIDIA CORPORATION. All rights reserved.
 */

#include <linux/export.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/version.h>
#if KERNEL_VERSION(4, 15, 0) > LINUX_VERSION_CODE
#include <soc/tegra/chip-id.h>
#else
#include <soc/tegra/fuse.h>
#endif
#include <linux/platform_device.h>
#include <linux/nvhost.h>

#include "pva.h"
#include "pva_mailbox.h"
#include "pva_interface_regs_t19x.h"

static struct pva_status_interface_registers t19x_status_regs[NUM_INTERFACES_T19X] = {
	{
		{
			PVA_CCQ_STATUS3_REG,
			PVA_CCQ_STATUS4_REG,
			PVA_CCQ_STATUS5_REG,
			PVA_CCQ_STATUS6_REG,
			PVA_CCQ_STATUS7_REG
		}
	},
};

void read_status_interface_t19x(struct pva *pva,
				uint32_t interface_id, u32 isr_status,
				struct pva_cmd_status_regs *status_output)
{
	int i;
	uint32_t *status_registers;

	status_registers = t19x_status_regs[interface_id].registers;

	for (i = 0; i < PVA_CMD_STATUS_REGS; i++) {
		if (isr_status & (PVA_VALID_STATUS3 << i)) {
			status_output->status[i] = host1x_readl(pva->pdev,
						    status_registers[i]);
			if ((i == 0) && (isr_status & PVA_CMD_ERROR)) {
				status_output->error =
					PVA_GET_ERROR_CODE(
						status_output->status[i]);
			}
		}
	}
}
