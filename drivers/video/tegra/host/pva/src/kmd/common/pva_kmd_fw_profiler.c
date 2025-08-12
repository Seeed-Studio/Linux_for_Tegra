// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#include "pva_api_cmdbuf.h"
#include "pva_api_types.h"
#include "pva_bit.h"
#include "pva_fw.h"
#include "pva_kmd_cmdbuf.h"
#include "pva_kmd_device.h"
#include "pva_kmd_constants.h"
#include "pva_utils.h"
#include "pva_kmd_fw_profiler.h"
#include "pva_kmd_shared_buffer.h"
#include "pva_api_private.h"

// TODO: This is here temporarily just for testing. Should be moved to a common header
#define CMD_ID(x) PVA_EXTRACT(x, 6, 0, uint8_t)
#define CMD(name) [CMD_ID(PVA_CMD_OPCODE_##name)] = #name

static const char *cmd_names[PVA_CMD_OPCODE_COUNT] = {
	CMD(LINK_CHUNK),
	CMD(BARRIER),
	CMD(ACQUIRE_ENGINE),
	CMD(RELEASE_ENGINE),
	CMD(SET_CURRENT_ENGINE),
	CMD(CLEAR_VMEM),
	CMD(BIND_L2SRAM),
	CMD(RELEASE_L2SRAM),
	CMD(INVALIDATE_L2SRAM),
	CMD(FLUSH_L2SRAM),
	CMD(PATCH_L2SRAM_OFFSET),
	CMD(SET_VPU_EXECUTABLE),
	CMD(INIT_VPU_EXECUTABLE),
	CMD(PREFETCH_VPU_CODE),
	CMD(SET_VPU_PARAMETER),
	CMD(SET_VPU_PARAMETER_WITH_ADDRESS),
	CMD(SET_VPU_INSTANCE_PARAMETER),
	CMD(SET_VPU_PARAMETER_WITH_BUFFER),
	CMD(RUN_VPU),
	CMD(SET_PPE_EXECUTABLE),
	CMD(INIT_PPE_EXECUTABLE),
	CMD(PREFETCH_PPE_CODE),
	CMD(RUN_PPE),
	CMD(FETCH_DMA_CONFIGURATION),
	CMD(SETUP_DMA),
	CMD(RUN_DMA),
	CMD(BIND_DRAM_SLOT),
	CMD(BIND_VMEM_SLOT),
	CMD(UNREGISTER_RESOURCE),
	CMD(WRITE_DRAM),
	CMD(CAPTURE_TIMESTAMP),
	CMD(RUN_UNIT_TESTS)
};

static const char *priv_cmd_names[PVA_CMD_PRIV_OPCODE_COUNT] = {
	CMD(INIT_RESOURCE_TABLE),
	CMD(DEINIT_RESOURCE_TABLE),
	CMD(UPDATE_RESOURCE_TABLE),
	CMD(INIT_QUEUE),
	CMD(DEINIT_QUEUE),
	CMD(ENABLE_FW_PROFILING),
	CMD(DISABLE_FW_PROFILING),
	CMD(SUSPEND_FW),
	CMD(RESUME_FW)
};

static inline const char *pva_fw_get_cmd_name(uint32_t opcode)
{
	uint32_t cmd_id;
	const char *name;

	cmd_id = CMD_ID(opcode);

	if (opcode & PVA_CMD_PRIV_OPCODE_FLAG) {
		if (cmd_id >= PVA_CMD_PRIV_OPCODE_COUNT) {
			return "INVALID";
		}
		name = priv_cmd_names[cmd_id];
	} else {
		if (cmd_id >= PVA_CMD_OPCODE_COUNT) {
			return "INVALID";
		}
		name = cmd_names[cmd_id];
	}

	if (name == NULL) {
		return "UNKNOWN";
	} else {
		return name;
	}
}

void pva_kmd_device_init_profiler(struct pva_kmd_device *pva)
{
	pva->debugfs_context.g_fw_profiling_config.enabled = false;
	pva->debugfs_context.g_fw_profiling_config.filter = 0x0;
}

void pva_kmd_device_deinit_profiler(struct pva_kmd_device *pva)
{
	pva->debugfs_context.g_fw_profiling_config.enabled = false;
}

enum pva_error pva_kmd_notify_fw_enable_profiling(struct pva_kmd_device *pva)
{
	struct pva_kmd_submitter *dev_submitter = &pva->submitter;
	struct pva_cmd_enable_fw_profiling cmd = { 0 };
	uint32_t filter = 0U;
	uint8_t timestamp_type = TIMESTAMP_TYPE_CYCLE_COUNT;
	enum pva_error err = PVA_SUCCESS;

	struct pva_kmd_shared_buffer *profiling_buffer =
		&pva->kmd_fw_buffers[PVA_PRIV_CCQ_ID];

	// Ensure that the DRAM buffer that backs FW profiling was allocated
	if (profiling_buffer->resource_memory == NULL) {
		return PVA_INVALID_RESOURCE;
	}
	// filter |= PVA_FW_EVENT_DO_CMD;
	filter |= PVA_FW_EVENT_RUN_VPU;

	if (pva->debugfs_context.g_fw_profiling_config.enabled) {
		return PVA_SUCCESS;
	}

	pva_kmd_set_cmd_enable_fw_profiling(&cmd, filter, timestamp_type);

	err = pva_kmd_submit_cmd_sync(dev_submitter, &cmd, sizeof(cmd),
				      PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
				      PVA_KMD_WAIT_FW_TIMEOUT_US);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Failed to submit command");
		goto out;
	}

	pva->debugfs_context.g_fw_profiling_config.enabled = true;
	pva->debugfs_context.g_fw_profiling_config.filter = filter;
	pva->debugfs_context.g_fw_profiling_config.timestamp_type =
		timestamp_type;
	pva->debugfs_context.g_fw_profiling_config.timestamp_size =
		(pva->debugfs_context.g_fw_profiling_config.timestamp_type ==
		 TIMESTAMP_TYPE_TSE) ?
			      8 :
			      4;

out:
	return err;
}

enum pva_error pva_kmd_notify_fw_disable_profiling(struct pva_kmd_device *pva)
{
	struct pva_cmd_disable_fw_profiling cmd = { 0 };
	enum pva_error err;

	pva_kmd_set_cmd_disable_fw_profiling(&cmd);

	err = pva_kmd_submit_cmd_sync(&pva->submitter, &cmd, sizeof(cmd),
				      PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
				      PVA_KMD_WAIT_FW_TIMEOUT_US);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Failed to submit command");
		goto err_out;
	}

	pva->debugfs_context.g_fw_profiling_config.enabled = false;
	pva->debugfs_context.g_fw_profiling_config.filter = 0x0;

	return PVA_SUCCESS;

err_out:
	return err;
}

static void decode_and_print_event(unsigned long walltime,
				   unsigned long relative_time,
				   struct pva_fw_event_message message,
				   char *msg_string)
{
	switch (PVA_BIT(message.event)) {
	case PVA_FW_EVENT_DO_CMD: {
		sprintf(msg_string,
			"pva_fw@%lu: [%8lu] event=%-12s type=%-7s slot=%u  idx=%-5u    opcode=%s",
			walltime, relative_time, "DO_CMD",
			event_type_to_string(message.type), message.arg2,
			message.arg3, pva_fw_get_cmd_name(message.arg1));
	} break;
	case PVA_FW_EVENT_SCAN_QUEUES: {
		sprintf(msg_string,
			"pva_fw@%lu: [%8lu] event=%-12s type=%-7s found=%u ccq_id=%-5u queue_id=%u",
			walltime, relative_time, "SCAN_QUEUES",
			event_type_to_string(message.type), message.arg1,
			message.arg2, message.arg3);
	} break;
	case PVA_FW_EVENT_SCAN_SLOTS: {
		sprintf(msg_string,
			"pva_fw@%lu: [%8lu] event=%-12s type=%-7s state=%u slot=%u",
			walltime, relative_time, "SCAN_SLOTS",
			event_type_to_string(message.type), message.arg1,
			message.arg2);
	} break;
	case PVA_FW_EVENT_RUN_VPU: {
		sprintf(msg_string,
			"pva_fw@%lu: [%8lu] event=%-12s type=%-7s slot=%u  idx=%-5u    opcode=%s",
			walltime, relative_time, "RUN_VPU",
			event_type_to_string(message.type), message.arg2,
			message.arg3, pva_fw_get_cmd_name(message.arg1));
	} break;
	default:
		pva_dbg_printf("Unknown event type\n");
		break;
	}
}

enum pva_error pva_kmd_process_fw_event(struct pva_kmd_device *pva,
					uint8_t *data, uint32_t data_size)
{
	uint64_t timestamp = 0;
	char msg_string[200] = { '\0' };
	struct pva_fw_event_message event_header;
	static uint64_t prev_walltime = 0U;
	uint64_t relative_time = 0U;

	// TODO: R5 frequency is hard-coded for now. Get this at runtime.
	static const uint32_t r5_freq = 716800000U;
	static const uint64_t r5_cycle_duration = 1000000000000 / r5_freq;
	uint64_t walltime = 0U; // in nanoseconds

	if (data_size <
	    (sizeof(event_header) +
	     pva->debugfs_context.g_fw_profiling_config.timestamp_size)) {
		return PVA_INVAL;
	}

	memcpy(&event_header, data, sizeof(event_header));
	memcpy(&timestamp, &data[sizeof(event_header)],
	       pva->debugfs_context.g_fw_profiling_config.timestamp_size);

	if (pva->debugfs_context.g_fw_profiling_config.timestamp_type ==
	    TIMESTAMP_TYPE_TSE) {
		walltime = (timestamp << 5);
	} else if (pva->debugfs_context.g_fw_profiling_config.timestamp_type ==
		   TIMESTAMP_TYPE_CYCLE_COUNT) {
		timestamp = PVA_LOW32(timestamp);
		walltime = safe_mulu64(r5_cycle_duration, timestamp);
		walltime = walltime / 1000U;
	}
	relative_time = (prev_walltime > walltime) ?
				      0U :
				      safe_subu64(walltime, prev_walltime);
	decode_and_print_event(walltime, relative_time, event_header,
			       &msg_string[0]);
	pva_kmd_print_str(msg_string);
	prev_walltime = walltime;

	return PVA_SUCCESS;
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
