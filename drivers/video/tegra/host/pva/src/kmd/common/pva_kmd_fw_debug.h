/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_FW_DEBUG_H
#define PVA_KMD_FW_DEBUG_H
#include "pva_api.h"
#include "pva_fw.h"
#include "pva_kmd_device.h"

struct pva_kmd_fw_print_buffer {
	struct pva_fw_print_buffer_header *buffer_info;
	char const *content;
};

enum pva_error pva_kmd_notify_fw_set_trace_level(struct pva_kmd_device *pva,
						 uint32_t trace_level);

enum pva_error pva_kmd_notify_fw_set_profiling_level(struct pva_kmd_device *pva,
						     uint32_t level);

void pva_kmd_drain_fw_print(struct pva_kmd_fw_print_buffer *print_buffer);

#endif // PVA_KMD_FW_DEBUG_H
