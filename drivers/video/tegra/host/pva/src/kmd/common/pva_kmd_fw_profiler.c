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
#include "pva_api_cmdbuf.h"
#include "pva_api_types.h"
#include "pva_bit.h"
#include "pva_fw.h"
#include "pva_kmd_cmdbuf.h"
#include "pva_kmd_device.h"
#include "pva_kmd_constants.h"
#include "pva_utils.h"
#include "pva_kmd_fw_profiler.h"

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
	enum pva_error err = PVA_SUCCESS;
	const uint32_t profiling_buffer_size = PVA_KMD_FW_PROFILING_BUFFER_SIZE;

	struct pva_kmd_fw_profiling_buffer *fw_profiling_buffer =
		&pva->fw_profiling_buffer;

	// Event message should be 32-bit to keep logging latency low
	ASSERT(sizeof(struct pva_fw_event_message) == sizeof(uint32_t));

	pva->fw_profiling_buffer_memory =
		pva_kmd_device_memory_alloc_map(profiling_buffer_size, pva,
						PVA_ACCESS_RW,
						PVA_R5_SMMU_CONTEXT_ID);
	ASSERT(pva->fw_profiling_buffer_memory != NULL);

	/* Add profiling memory to resource table */
	err = pva_kmd_add_dram_buffer_resource(
		&pva->dev_resource_table, pva->fw_profiling_buffer_memory,
		&pva->fw_profiling_buffer_resource_id);
	ASSERT(err == PVA_SUCCESS);
	pva_kmd_update_fw_resource_table(&pva->dev_resource_table);

	fw_profiling_buffer->buffer_info =
		(struct pva_fw_profiling_buffer_header *)
			pva->fw_profiling_buffer_memory->va;
	fw_profiling_buffer->content =
		pva_offset_pointer(pva->fw_profiling_buffer_memory->va,
				   sizeof(*fw_profiling_buffer->buffer_info));
	fw_profiling_buffer->size = pva->fw_profiling_buffer_memory->size;
	fw_profiling_buffer->head = 0U;
	fw_profiling_buffer->buffer_info->flags = 0U;
	fw_profiling_buffer->buffer_info->tail = 0U;

	pva->debugfs_context.g_fw_profiling_config.enabled = false;
	pva->debugfs_context.g_fw_profiling_config.filter = 0x0;
}

void pva_kmd_device_deinit_profiler(struct pva_kmd_device *pva)
{
	pva_kmd_drop_resource(&pva->dev_resource_table,
			      pva->fw_profiling_buffer_resource_id);
	pva->debugfs_context.g_fw_profiling_config.enabled = false;
}

enum pva_error pva_kmd_notify_fw_enable_profiling(struct pva_kmd_device *pva)
{
	struct pva_kmd_cmdbuf_builder builder;
	struct pva_kmd_submitter *dev_submitter = &pva->submitter;
	struct pva_cmd_enable_fw_profiling *cmd;
	uint64_t buffer_offset = 0U;
	uint32_t filter = 0U;
	uint8_t timestamp_type = TIMESTAMP_TYPE_CYCLE_COUNT;
	uint32_t fence_val;
	enum pva_error err;

	// filter |= PVA_FW_EVENT_DO_CMD;
	filter |= PVA_FW_EVENT_RUN_VPU;

	if (pva->debugfs_context.g_fw_profiling_config.enabled) {
		return PVA_SUCCESS;
	}

	pva->fw_profiling_buffer.head = 0U;
	pva->fw_profiling_buffer.buffer_info->flags = 0U;
	pva->fw_profiling_buffer.buffer_info->tail = 0U;

	err = pva_kmd_submitter_prepare(dev_submitter, &builder);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}
	cmd = pva_kmd_reserve_cmd_space(&builder, sizeof(*cmd));
	ASSERT(cmd != NULL);
	pva_kmd_set_cmd_enable_fw_profiling(
		cmd, pva->fw_profiling_buffer_resource_id,
		pva->fw_profiling_buffer.size, buffer_offset, filter,
		timestamp_type);

	err = pva_kmd_submitter_submit(dev_submitter, &builder, &fence_val);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	err = pva_kmd_submitter_wait(dev_submitter, fence_val,
				     PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
				     PVA_KMD_WAIT_FW_TIMEOUT_US);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err(
			"Waiting for FW timed out when initializing context");
		goto err_out;
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

	return PVA_SUCCESS;
err_out:
	return err;
}

enum pva_error pva_kmd_notify_fw_disable_profiling(struct pva_kmd_device *pva)
{
	struct pva_kmd_cmdbuf_builder builder;
	struct pva_kmd_submitter *dev_submitter = &pva->submitter;
	struct pva_cmd_disable_fw_profiling *cmd;
	uint32_t fence_val;
	enum pva_error err;

	err = pva_kmd_submitter_prepare(dev_submitter, &builder);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}
	cmd = pva_kmd_reserve_cmd_space(&builder, sizeof(*cmd));
	ASSERT(cmd != NULL);
	pva_kmd_set_cmd_disable_fw_profiling(cmd);

	err = pva_kmd_submitter_submit(dev_submitter, &builder, &fence_val);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	err = pva_kmd_submitter_wait(dev_submitter, fence_val,
				     PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
				     PVA_KMD_WAIT_FW_TIMEOUT_US);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err(
			"Waiting for FW timed out when initializing context");
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

void pva_kmd_drain_fw_profiling_buffer(
	struct pva_kmd_device *pva,
	struct pva_kmd_fw_profiling_buffer *profiling_buffer)
{
	char msg_string[200] = { '\0' };
	struct pva_fw_event_message message;
	uint64_t prev_walltime = 0U;
	uint64_t timestamp = 0U;
	uint64_t relative_time = 0U;
	uint32_t buffer_space;

	// TODO: R5 frequency is hard-coded for now. Get this at runtime.
	static const uint32_t r5_freq = 716800000U;
	static const unsigned long r5_cycle_duration = 1000000000000 / r5_freq;
	unsigned long walltime = 0U; // in nanoseconds
	uint64_t walltime_diff;

	const uint32_t message_size =
		sizeof(message) +
		pva->debugfs_context.g_fw_profiling_config.timestamp_size;
	uint32_t *profiling_buffer_head = &profiling_buffer->head;
	uint32_t profiling_buffer_tail = profiling_buffer->buffer_info->tail;
	while (*profiling_buffer_head < profiling_buffer_tail) {
		buffer_space = safe_addu32(*profiling_buffer_head,
					   safe_subu32(message_size, 1U));
		ASSERT(buffer_space <= profiling_buffer_tail);
		memcpy(&message,
		       &profiling_buffer->content[*profiling_buffer_head],
		       sizeof(message));
		memcpy(&timestamp,
		       &profiling_buffer->content[*profiling_buffer_head +
						  sizeof(message)],
		       pva->debugfs_context.g_fw_profiling_config
			       .timestamp_size);

		if (pva->debugfs_context.g_fw_profiling_config.timestamp_type ==
		    TIMESTAMP_TYPE_TSE) {
			walltime = (timestamp << 5);
		} else if (pva->debugfs_context.g_fw_profiling_config
				   .timestamp_type ==
			   TIMESTAMP_TYPE_CYCLE_COUNT) {
			timestamp = PVA_LOW32(timestamp);
			walltime = (r5_cycle_duration * timestamp) / 1000U;
		}
		walltime_diff = safe_subu64((uint64_t)walltime, prev_walltime);
		relative_time = (prev_walltime == 0U) ? 0U : walltime_diff;
		decode_and_print_event(walltime, relative_time, message,
				       &msg_string[0]);
		pva_kmd_print_str(msg_string);
		*profiling_buffer_head = *profiling_buffer_head + message_size;
		prev_walltime = walltime;
	}

	return;
}
