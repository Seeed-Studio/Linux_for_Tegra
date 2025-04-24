/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_SHIM_TRACE_EVENT_H
#define PVA_KMD_SHIM_TRACE_EVENT_H

#include "pva_kmd_device.h"

void pva_kmd_shim_add_trace_vpu_exec(
	struct pva_kmd_device *pva,
	struct pva_kmd_fw_msg_vpu_trace const *trace_info);

void pva_kmd_shim_add_trace_fence(
	struct pva_kmd_device *pva,
	struct pva_kmd_fw_msg_fence_trace const *trace_info);

#endif // PVA_KMD_SHIM_TRACE_EVENT_H
