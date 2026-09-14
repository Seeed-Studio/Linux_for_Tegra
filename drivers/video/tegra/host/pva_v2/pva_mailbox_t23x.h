/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2023, NVIDIA CORPORATION. All rights reserved.
 *
 * PVA mailbox header
 */

#ifndef __PVA_MAILBOX_T23X_H__
#define __PVA_MAILBOX_T23X_H__

#include <linux/platform_device.h>

#include "pva-interface.h"

/**
 * pva_read_mailbox() - read a mailbox register
 *
 * @pva:			Pointer to PVA structure
 * @mbox:		mailbox register to be written
 *
 * This function will read the indicated mailbox register and return its
 * contents.  it uses side channel B as host would.
 *
 * Return Value:
 *	contents of the indicated mailbox register
 */
u32 pva_read_mailbox_t23x(struct platform_device *pdev, u32 mbox_id);

/**
 * pva_write_mailbox() - write to a mailbox register
 *
 * @pva:			Pointer to PVA structure
 * @mbox:		mailbox register to be written
 * @value:		value to be written into the mailbox register
 *
 * This function will write a value into the indicated mailbox register.
 *
 * Return Value:
 *	none
 */
void pva_write_mailbox_t23x(struct platform_device *pdev, u32 mbox_id, u32 value);

#endif /*__PVA_MAINBOX_T23X_H__*/
