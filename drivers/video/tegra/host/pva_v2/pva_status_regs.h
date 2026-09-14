/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2023, NVIDIA Corporation.  All rights reserved.
 *
 * PVA Command header
 */


#ifndef __PVA_STATUS_REGS_H__
#define __PVA_STATUS_REGS_H__

#define PVA_CMD_STATUS_REGS 5

#define PVA_CMD_STATUS3_INDEX 0u
#define PVA_CMD_STATUS4_INDEX 1u
#define PVA_CMD_STATUS5_INDEX 2u
#define PVA_CMD_STATUS6_INDEX 3u
#define PVA_CMD_STATUS7_INDEX 4u

enum pva_cmd_status {
	PVA_CMD_STATUS_INVALID = 0,
	PVA_CMD_STATUS_WFI     = 1,
	PVA_CMD_STATUS_DONE    = 2,
	PVA_CMD_STATUS_ABORTED = 3,
};

struct pva_cmd_status_regs {
	uint32_t status[PVA_CMD_STATUS_REGS];
	uint32_t error;
	uint32_t cmd;
};

#endif
