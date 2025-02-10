// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2023, NVIDIA CORPORATION. All rights reserved.
 *
 * PVA mailbox code for T23x
 */

#include <linux/export.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/platform_device.h>
#include <linux/nvhost.h>

#include "pva_mailbox.h"
#include "pva_mailbox_t23x.h"
#include "pva_regs.h"


static u32 pva_get_mb_reg_ex(u32 i)
{
	u32 mb_reg[VALID_MB_INPUT_REGS_EX] = {
		hsp_sm0_r(),
		hsp_sm1_r(),
		hsp_sm2_r(),
		hsp_sm3_r(),
		hsp_sm4_r(),
		hsp_sm5_r(),
		hsp_sm6_r(),
		hsp_sm7_r()
	};

	return mb_reg[i];
}

u32 pva_read_mailbox_t23x(struct platform_device *pdev, u32 mbox_id)
{
	return host1x_readl(pdev, pva_get_mb_reg_ex(mbox_id));
}

void pva_write_mailbox_t23x(struct platform_device *pdev, u32 mbox_id, u32 value)
{
	host1x_writel(pdev, pva_get_mb_reg_ex(mbox_id), value);
}

