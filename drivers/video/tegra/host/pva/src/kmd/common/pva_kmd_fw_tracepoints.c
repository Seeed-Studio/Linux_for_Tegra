// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_api_types.h"
#include "pva_fw.h"
#include "pva_kmd_cmdbuf.h"
#include "pva_kmd_device.h"
#include "pva_kmd_constants.h"
#include "pva_kmd_fw_tracepoints.h"

static int64_t update_fw_trace_level(struct pva_kmd_device *pva,
				     void *file_data, const uint8_t *in_buffer,
				     uint64_t offset, uint64_t size)
{
	uint32_t trace_level;
	unsigned long retval;
	size_t copy_size;
	uint32_t base = 10;
	char strbuf[11]; // 10 bytes for the highest 32bit value and another 1 byte for the Null character
	strbuf[10] = '\0';

	if (size == 0U) {
		pva_kmd_log_err("Write failed, no data provided");
		return -1;
	}

	/* Copy minimum of buffer size and input size */
	copy_size =
		(size < (sizeof(strbuf) - 1U)) ? size : (sizeof(strbuf) - 1U);

	retval = pva_kmd_copy_data_from_user(strbuf, in_buffer + offset,
					     copy_size);
	if (retval != 0UL) {
		pva_kmd_log_err("Failed to copy write buffer from user");
		return -1;
	}

	trace_level = (uint32_t)pva_kmd_strtol(strbuf, base);

	pva->fw_trace_level = trace_level;

	/* If device is on, busy the device and set the debug log level */
	if (pva_kmd_device_maybe_on(pva) == true) {
		enum pva_error err;
		err = pva_kmd_device_busy(pva);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err(
				"pva_kmd_device_busy failed when submitting set debug log level cmd");
			goto err_end;
		}

		err = pva_kmd_notify_fw_set_trace_level(pva, trace_level);

		pva_kmd_device_idle(pva);

		if (err != PVA_SUCCESS) {
			pva_kmd_log_err(
				"Failed to notify FW about debug log level change");
		}
	}
err_end:
	return (int64_t)copy_size;
}

static int64_t get_fw_trace_level(struct pva_kmd_device *dev, void *file_data,
				  uint8_t *out_buffer, uint64_t offset,
				  uint64_t size)
{
	char print_buffer[64];
	int formatted_len;

	formatted_len = snprintf(print_buffer, sizeof(print_buffer), "%u\n",
				 dev->fw_trace_level);

	if (formatted_len <= 0) {
		return -1;
	}

	return pva_kmd_read_from_buffer_to_user(out_buffer, size, offset,
						print_buffer,
						(uint64_t)formatted_len);
}

enum pva_error pva_kmd_fw_tracepoints_init_debugfs(struct pva_kmd_device *pva)
{
	enum pva_error err;

	pva->debugfs_context.fw_trace_level_fops.write = &update_fw_trace_level;
	pva->debugfs_context.fw_trace_level_fops.read = &get_fw_trace_level;
	pva->debugfs_context.fw_trace_level_fops.pdev = pva;
	err = pva_kmd_debugfs_create_file(
		pva, "fw_trace_level",
		&pva->debugfs_context.fw_trace_level_fops);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Failed to create fw_trace_level debugfs file");
		return err;
	}

	return PVA_SUCCESS;
}

enum pva_error pva_kmd_notify_fw_set_trace_level(struct pva_kmd_device *pva,
						 uint32_t trace_level)
{
	struct pva_cmd_set_trace_level cmd = { 0 };
	pva_kmd_set_cmd_set_trace_level(&cmd, trace_level);

	return pva_kmd_submit_cmd_sync(&pva->submitter, &cmd,
				       (uint32_t)sizeof(cmd),
				       PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
				       PVA_KMD_WAIT_FW_TIMEOUT_US);
}

void pva_kmd_process_fw_tracepoint(struct pva_kmd_device *pva,
				   struct pva_fw_tracepoint *tp)
{
	char msg_string[200] = { '\0' };

	snprintf(
		msg_string, sizeof(msg_string),
		"pva fw tracepoint: type=%s flags=%s slot=%s ccq=%u queue=%u engine=%u arg1=0x%x arg2=0x%x",
		pva_fw_tracepoint_type_to_string(PVA_BIT(tp->type)),
		pva_fw_tracepoint_flags_to_string(tp->flags),
		pva_fw_tracepoint_slot_id_to_string(tp->slot_id),
		(uint32_t)tp->ccq_id, (uint32_t)tp->queue_id,
		(uint32_t)tp->engine_id, (uint32_t)tp->arg1,
		(uint32_t)tp->arg2);

	pva_kmd_print_str(msg_string);
}
