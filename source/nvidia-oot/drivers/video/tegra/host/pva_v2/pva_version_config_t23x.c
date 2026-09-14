// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2023, NVIDIA Corporation.  All rights reserved.
 */

#include "pva_mailbox_t23x.h"
#include "pva_interface_regs_t23x.h"
#include "pva_ccq_t23x.h"

struct pva_version_config pva_t23x_config = {
	.read_mailbox = pva_read_mailbox_t23x,
	.write_mailbox = pva_write_mailbox_t23x,
	.read_status_interface = read_status_interface_t23x,
	.ccq_send_task = pva_ccq_send_task_t23x,
	.submit_cmd_sync_locked = pva_send_cmd_sync_locked,
	.submit_cmd_sync = pva_send_cmd_sync,
	.irq_count = 9,
};
