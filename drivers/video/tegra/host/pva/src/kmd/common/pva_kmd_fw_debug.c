// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_utils.h"
#include "pva_api.h"
#include "pva_api_cmdbuf.h"
#include "pva_api_types.h"
#include "pva_bit.h"
#include "pva_fw.h"
#include "pva_kmd_cmdbuf.h"
#include "pva_kmd_device.h"
#include "pva_kmd_fw_debug.h"
#include "pva_kmd_constants.h"
#include "pva_utils.h"

enum pva_error pva_kmd_notify_fw_set_debug_log_level(struct pva_kmd_device *pva,
						     uint32_t log_level)
{
	struct pva_kmd_submitter *submitter = &pva->submitter;
	struct pva_kmd_cmdbuf_builder builder;
	struct pva_cmd_set_debug_log_level *cmd;
	uint32_t fence_val;
	enum pva_error err;

	err = pva_kmd_submitter_prepare(submitter, &builder);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	cmd = pva_kmd_reserve_cmd_space(&builder, sizeof(*cmd));
	ASSERT(cmd != NULL);

	pva_kmd_set_cmd_set_debug_log_level(cmd, log_level);
	pva_kmd_print_str_u64("set debug log level cmd:", cmd->log_level);

	err = pva_kmd_submitter_submit(submitter, &builder, &fence_val);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("set debug log level cmd submission failed");
		goto cancel_builder;
	}

	err = pva_kmd_submitter_wait(submitter, fence_val,
				     PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
				     PVA_KMD_WAIT_FW_TIMEOUT_US);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err(
			"Waiting for FW timed out when setting debug log level");
		goto err_out;
	}

cancel_builder:
	pva_kmd_cmdbuf_builder_cancel(&builder);

err_out:
	return err;
}

void pva_kmd_drain_fw_print(struct pva_kmd_fw_print_buffer *print_buffer)
{
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

		print_size = strnlen(str, max_len);
		pva_kmd_print_str(str);

		/* +1 for null terminator */
		head = (head + print_size + 1);
		if (head >= buf_info->size) {
			head = 0;
		}
		buf_info->head = head;
	}

	if (print_buffer->buffer_info->flags & PVA_FW_PRINT_BUFFER_OVERFLOWED) {
		pva_kmd_log_err("Firmware print buffer overflowed!");
	}

	if (print_buffer->buffer_info->flags & PVA_FW_PRINT_FAILURE) {
		pva_kmd_log_err("Firmware print failed!");
	}
}
