// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_limits.h"
#include "pva_kmd_utils.h"
#include "pva_api.h"
#include "pva_api_cmdbuf.h"
#include "pva_api_types.h"
#include "pva_bit.h"
#include "pva_fw.h"
#include "pva_kmd_cmdbuf.h"
#include "pva_kmd_device.h"
#include "pva_kmd_fw_debug_printf.h"
#include "pva_kmd_constants.h"
#include "pva_utils.h"

void pva_kmd_init_fw_print_buffer(struct pva_kmd_device *pva,
				  void *debug_buffer_va)
{
	struct pva_kmd_fw_print_buffer *print_buffer = &pva->fw_print_buffer;
	print_buffer->buffer_info = pva_offset_pointer(
		debug_buffer_va,
		FW_TRACE_BUFFER_SIZE + FW_CODE_COVERAGE_BUFFER_SIZE);
	print_buffer->buffer_info->size =
		(uint32_t)((uint32_t)FW_DEBUG_LOG_BUFFER_SIZE -
			   (uint32_t)sizeof(*print_buffer->buffer_info));
	print_buffer->buffer_info->head = 0;
	print_buffer->buffer_info->tail = 0;
	print_buffer->buffer_info->flags = 0;
	print_buffer->content = pva_offset_pointer(
		print_buffer->buffer_info, sizeof(*print_buffer->buffer_info));
}

void pva_kmd_drain_fw_print(struct pva_kmd_device *pva)
{
	struct pva_kmd_fw_print_buffer *print_buffer = &pva->fw_print_buffer;
	struct pva_fw_print_buffer_header *buf_info = print_buffer->buffer_info;
	uint32_t tail = buf_info->tail;

	if (tail > buf_info->size) {
		pva_kmd_log_err(
			"Firmware print tail is out of bounds! Refusing to print\n");
		return;
	}

	if (buf_info->head > buf_info->size) {
		pva_kmd_log_err(
			"Firmware print head is out of bounds! Refusing to print\n");
		return;
	}

	while (buf_info->head != tail) {
		uint32_t max_len;
		uint32_t head = buf_info->head;
		const char *str = print_buffer->content + head;
		uint32_t print_size;
		size_t strnlen_result;

		if ((head + PVA_MAX_DEBUG_LOG_MSG_CHARACTERS) >
		    buf_info->size) {
			buf_info->head = 0;
			continue;
		}

		if (head < tail) {
			max_len = tail - head;
		} else {
			max_len = buf_info->size - head;
		}

		strnlen_result = strnlen(str, max_len);
		/* Validate strnlen result fits in uint32_t */
		if (strnlen_result > U32_MAX) {
			pva_kmd_log_err(
				"FW debug print string length exceeds U32_MAX");
			return;
		}
		/* CERT INT31-C: strnlen_result validated to fit in uint32_t, safe to cast */
		print_size = (uint32_t)strnlen_result;
		pva_kmd_log_err(str);

		/* +1 for null terminator - use safe addition to prevent overflow */
		head = safe_addu32(head, safe_addu32(print_size, 1U));
		if (head >= buf_info->size) {
			head = 0;
		}
		buf_info->head = head;
	}

	if ((print_buffer->buffer_info->flags &
	     PVA_FW_PRINT_BUFFER_OVERFLOWED) != 0U) {
		pva_kmd_log_err("Firmware print buffer overflowed!");
	}

	if ((print_buffer->buffer_info->flags & PVA_FW_PRINT_FAILURE) != 0U) {
		pva_kmd_log_err("Firmware print failed!");
	}
}