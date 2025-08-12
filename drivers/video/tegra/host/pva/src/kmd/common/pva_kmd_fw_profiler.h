/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_FW_PROFILER_H
#define PVA_KMD_FW_PROFILER_H
#include "pva_kmd_device.h"
#include "pva_kmd_shared_buffer.h"

#define PVA_KMD_FW_PROFILING_BUF_NUM_ELEMENTS (4096 * 100)

struct pva_kmd_fw_profiling_config {
	uint32_t filter;
	enum pva_fw_timestamp_t timestamp_type;
	uint8_t timestamp_size;
	uint8_t enabled;
};

void pva_kmd_device_init_profiler(struct pva_kmd_device *pva);

void pva_kmd_device_deinit_profiler(struct pva_kmd_device *pva);

enum pva_error pva_kmd_process_fw_event(struct pva_kmd_device *pva,
					uint8_t *data, uint32_t data_size);

void pva_kmd_process_fw_tracepoint(struct pva_kmd_device *pva,
				   struct pva_fw_tracepoint *tp);

enum pva_error pva_kmd_notify_fw_enable_profiling(struct pva_kmd_device *pva);

enum pva_error pva_kmd_notify_fw_disable_profiling(struct pva_kmd_device *pva);
#endif
