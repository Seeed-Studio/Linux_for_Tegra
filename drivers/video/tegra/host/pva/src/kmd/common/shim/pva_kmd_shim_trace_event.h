/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_SHIM_TRACE_EVENT_H
#define PVA_KMD_SHIM_TRACE_EVENT_H

#include "pva_kmd_device.h"

#if PVA_ENABLE_NSYS_PROFILING

void pva_kmd_nsys_cmdbuf_trace(
	struct pva_kmd_device *pva,
	struct pva_kmd_fw_msg_cmdbuf_trace const *trace_info);

void pva_kmd_nsys_vpu_exec_trace(
	struct pva_kmd_device *pva,
	struct pva_kmd_fw_msg_vpu_exec_trace const *trace_info);

void pva_kmd_nsys_engine_acquire_trace(
	struct pva_kmd_device *pva,
	struct pva_kmd_fw_msg_engine_acquire_trace const *trace_info);

void pva_kmd_nsys_fence_trace(
	struct pva_kmd_device *pva,
	struct pva_kmd_fw_msg_fence_trace const *trace_info);

#else /* PVA_ENABLE_NSYS_PROFILING */

/* Dummy inline functions when Nsight Systems profiling is disabled */
static inline void
pva_kmd_nsys_cmdbuf_trace(struct pva_kmd_device *pva,
			  struct pva_kmd_fw_msg_cmdbuf_trace const *trace_info)
{
	(void)pva;
	(void)trace_info;
}

static inline void pva_kmd_nsys_vpu_exec_trace(
	struct pva_kmd_device *pva,
	struct pva_kmd_fw_msg_vpu_exec_trace const *trace_info)
{
	(void)pva;
	(void)trace_info;
}

static inline void pva_kmd_nsys_engine_acquire_trace(
	struct pva_kmd_device *pva,
	struct pva_kmd_fw_msg_engine_acquire_trace const *trace_info)
{
	(void)pva;
	(void)trace_info;
}

static inline void
pva_kmd_nsys_fence_trace(struct pva_kmd_device *pva,
			 struct pva_kmd_fw_msg_fence_trace const *trace_info)
{
	(void)pva;
	(void)trace_info;
}

#endif /* PVA_ENABLE_NSYS_PROFILING */

#endif // PVA_KMD_SHIM_TRACE_EVENT_H
