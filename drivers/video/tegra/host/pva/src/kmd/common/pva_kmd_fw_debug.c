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

#include "pva_kmd_fw_debug.h"
#include "pva_kmd_utils.h"
#include "pva_api.h"

void pva_kmd_drain_fw_print(struct pva_kmd_fw_print_buffer *print_buffer)
{
	uint32_t tail = print_buffer->buffer_info->tail;

	if (tail > print_buffer->size) {
		pva_kmd_log_err(
			"Firmware print tail is out of bounds! Refusing to print\n");
		pva_dbg_printf("Tail %u vs size %u\n", tail,
			       print_buffer->size);
		return;
	}

	while (print_buffer->head < tail) {
		uint32_t max_len = tail - print_buffer->head;
		const char *str = print_buffer->content + print_buffer->head;
		uint32_t print_size;

		/* It must be null terminted */
		if (print_buffer->content[tail - 1] != '\0') {
			pva_kmd_log_err(
				"Firmware print is not null terminated! Refusing to print");
		}
		print_size = strnlen(str, max_len);
		pva_kmd_print_str(str);

		/* +1 for null terminator */
		print_buffer->head += print_size + 1;
	}

	if (print_buffer->buffer_info->flags & PVA_FW_PRINT_BUFFER_OVERFLOWED) {
		pva_kmd_log_err("Firmware print buffer overflowed!");
	}

	if (print_buffer->buffer_info->flags & PVA_FW_PRINT_FAILURE) {
		pva_kmd_log_err("Firmware print failed!");
	}
}
