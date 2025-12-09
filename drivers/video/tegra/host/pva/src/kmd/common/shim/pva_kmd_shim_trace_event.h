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

/**
 * @brief Add fence trace event to the trace system.
 *
 * @details This function performs the following operations:
 * - Records fence synchronization timing information
 * - Adds trace event to the platform's trace collection system
 * - Captures synchronization metrics for fence operations
 * - Enables profiling and analysis of synchronization behavior
 * - Uses platform-appropriate trace event mechanisms
 *
 * This function is called when fence synchronization events occur
 * to record timing and synchronization information. The trace data
 * helps analyze synchronization overhead and identify potential
 * performance bottlenecks in fence operations.
 *
 * @param[in, out] pva        Pointer to @ref pva_kmd_device structure
 *                            Valid value: non-null
 * @param[in] trace_info      Pointer to fence trace information structure
 *                            Valid value: non-null pointer to
 *                            @ref pva_kmd_fw_msg_fence_trace
 */
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
