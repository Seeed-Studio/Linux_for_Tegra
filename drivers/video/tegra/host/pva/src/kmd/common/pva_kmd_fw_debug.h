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

#ifndef PVA_KMD_FW_DEBUG_H
#define PVA_KMD_FW_DEBUG_H
#include "pva_api.h"
#include "pva_fw.h"

struct pva_kmd_fw_print_buffer {
	struct pva_fw_print_buffer_header *buffer_info;
	char const *content;
	uint32_t size;
	uint32_t head;
};

void pva_kmd_drain_fw_print(struct pva_kmd_fw_print_buffer *print_buffer);

#endif // PVA_KMD_FW_DEBUG_H
