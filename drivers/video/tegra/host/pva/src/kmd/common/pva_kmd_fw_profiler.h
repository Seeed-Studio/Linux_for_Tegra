/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, NVIDIA Corporation.  All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation.  Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */
#ifndef PVA_KMD_FW_PROFILER_H
#define PVA_KMD_FW_PROFILER_H
#include "pva_kmd_device.h"

struct pva_kmd_fw_profiling_buffer {
#define PVA_KMD_FW_PROFILING_BUFFER_SIZE (512 * 1024)
	struct pva_fw_profiling_buffer_header *buffer_info;
	char const *content;
	uint32_t size;
	uint32_t head;
};

struct pva_kmd_fw_profiling_config {
	uint32_t filter;
	enum pva_fw_timestamp_t timestamp_type;
	uint8_t timestamp_size;
	uint8_t enabled;
};

void pva_kmd_device_init_profiler(struct pva_kmd_device *pva);

void pva_kmd_device_deinit_profiler(struct pva_kmd_device *pva);

void pva_kmd_drain_fw_profiling_buffer(
	struct pva_kmd_device *pva,
	struct pva_kmd_fw_profiling_buffer *profiling_buffer);

enum pva_error pva_kmd_notify_fw_enable_profiling(struct pva_kmd_device *pva);

enum pva_error pva_kmd_notify_fw_disable_profiling(struct pva_kmd_device *pva);
#endif
