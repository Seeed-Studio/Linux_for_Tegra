/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_FW_TRACEPOINTS_H
#define PVA_KMD_FW_TRACEPOINTS_H

#include "pva_fw.h"
#include "pva_kmd_device.h"

#if PVA_ENABLE_FW_TRACEPOINTS == 1
enum pva_error pva_kmd_fw_tracepoints_init_debugfs(struct pva_kmd_device *pva);

enum pva_error pva_kmd_notify_fw_set_trace_level(struct pva_kmd_device *pva,
						 uint32_t trace_level);

void pva_kmd_process_fw_tracepoint(struct pva_kmd_device *pva,
				   struct pva_fw_tracepoint *tp);
#else

static inline enum pva_error
pva_kmd_fw_tracepoints_init_debugfs(struct pva_kmd_device *pva)
{
	(void)pva;
	return PVA_SUCCESS;
}

static inline enum pva_error
pva_kmd_notify_fw_set_trace_level(struct pva_kmd_device *pva,
				  uint32_t trace_level)
{
	(void)pva;
	(void)trace_level;
	return PVA_SUCCESS;
}

static inline void pva_kmd_process_fw_tracepoint(struct pva_kmd_device *pva,
						 struct pva_fw_tracepoint *tp)
{
	(void)pva;
	(void)tp;
}
#endif

#endif // PVA_KMD_FW_TRACEPOINTS_H
