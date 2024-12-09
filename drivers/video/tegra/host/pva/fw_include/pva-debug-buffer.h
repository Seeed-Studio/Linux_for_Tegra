/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2024, NVIDIA Corporation.  All rights reserved.
 */

#ifndef PVA_DEBUG_BUF_H
#define PVA_DEBUG_BUF_H

#include <pva-bit.h>
#include <pva-types.h>
#include <pva-packed.h>

/* Max length of the log message including null termination */
#define PVA_MAX_DEBUG_LOG_MSG_CHARACTERS	80

/* Encoding error */
#define PVA_FW_PRINT_FAILURE (1 << 0)
/* Log message length is more than PVA_MAX_DEBUG_LOG_MSG_CHARACTERS i.e. 80 characters */
#define PVA_FW_PRINT_BUFFER_LOG_MSG_OVERFLOWED (1 << 1)
/* No space in circular buffer to add more logs */
#define PVA_FW_PRINT_BUFFER_FULL_LOG_DROPPED (1 << 2)

struct pva_kmd_fw_print_buffer {
	uint32_t size;
	uint32_t head;
	uint32_t tail;
	uint32_t flags;
	uint8_t pad0[4];
	/* Followed by print content */
} __packed;
#endif
