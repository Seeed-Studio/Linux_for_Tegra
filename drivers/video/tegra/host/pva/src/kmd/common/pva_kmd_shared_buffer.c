// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_abort.h"
#include "pva_kmd_device.h"
#include "pva_kmd_context.h"
#include "pva_kmd_shim_trace_event.h"
#include "pva_kmd_shared_buffer.h"
#include "pva_kmd_fw_tracepoints.h"
#include "pva_kmd_limits.h"

enum pva_error pva_kmd_shared_buffer_init(struct pva_kmd_device *pva,
					  uint8_t interface,
					  uint32_t element_size,
					  uint32_t num_entries,
					  shared_buffer_lock_cb lock_cb,
					  shared_buffer_lock_cb unlock_cb)
{
	enum pva_error err = PVA_SUCCESS;

	struct pva_kmd_device_memory *device_memory;
	struct pva_kmd_shared_buffer *buffer;
	uint64_t buffer_size;
	struct pva_cmd_init_shared_dram_buffer init_cmd = { 0 };

	ASSERT(interface < (uint8_t)PVA_MAX_NUM_CCQ);
	buffer = &pva->kmd_fw_buffers[interface];

	// If the buffer is already initialized, skip buffer allocation and just notify FW.
	// This is needed to support suspend/resume.
	if (buffer->header == NULL) {
		// Ensure that the buffer body is a multiple of 'element size'
		buffer_size = safe_mulu64(num_entries, element_size);
		buffer_size =
			safe_addu64(buffer_size,
				    sizeof(struct pva_fw_shared_buffer_header));

		device_memory =
			pva_kmd_device_memory_alloc_map(buffer_size, pva,
							PVA_ACCESS_RW,
							PVA_R5_SMMU_CONTEXT_ID);
		if (device_memory == NULL) {
			return PVA_NOMEM;
		}

		buffer->header =
			(struct pva_fw_shared_buffer_header *)device_memory->va;
		buffer->header->flags = 0U;
		buffer->header->element_size = element_size;
		buffer->header->head = 0U;
		buffer->header->tail = 0U;
		buffer->body = (pva_offset_pointer(buffer->header,
						   sizeof(*buffer->header)));
		buffer->lock_cb = lock_cb;
		buffer->unlock_cb = unlock_cb;
		buffer->resource_offset = 0U;
		buffer->resource_memory = device_memory;

		err = pva_kmd_bind_shared_buffer_handler(pva, interface, pva);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err_u64(
				"Failed to bind shared buffer handler for interface",
				interface);
			goto free_buffer_memory;
		}
	} else {
		device_memory = buffer->resource_memory;
	}

	/* Validate device memory size and iova fit in uint32_t */
	if ((device_memory->size > U32_MAX) ||
	    (device_memory->iova > U32_MAX)) {
		pva_kmd_log_err("Device memory size or iova exceeds U32_MAX");
		err = PVA_INVAL;
		goto release_handler;
	}

	/* CERT INT31-C: iova and size validated to fit in uint32_t, safe to cast */
	pva_kmd_set_cmd_init_shared_dram_buffer(&init_cmd, (uint8_t)interface,
						device_memory->iova,
						(uint32_t)device_memory->size);

	err = pva_kmd_submit_cmd_sync(&pva->submitter, &init_cmd,
				      (uint32_t)sizeof(init_cmd),
				      PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
				      PVA_KMD_WAIT_FW_TIMEOUT_US);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Failed to submit command");
		goto release_handler;
	}

	return err;

release_handler:
	pva_kmd_release_shared_buffer_handler(pva, interface);
free_buffer_memory:
	pva_kmd_device_memory_free(device_memory);
	buffer->header = NULL;
	buffer->resource_memory = NULL;
	return err;
}

enum pva_error pva_kmd_shared_buffer_deinit(struct pva_kmd_device *pva,
					    uint8_t interface)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_kmd_shared_buffer *buffer;
	struct pva_cmd_deinit_shared_dram_buffer deinit_cmd = { 0 };

	ASSERT(interface < (uint8_t)PVA_MAX_NUM_CCQ);
	buffer = &pva->kmd_fw_buffers[interface];

	pva_kmd_set_cmd_deinit_shared_dram_buffer(&deinit_cmd, interface);

	err = pva_kmd_submit_cmd_sync(&pva->submitter, &deinit_cmd,
				      (uint32_t)sizeof(deinit_cmd),
				      PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
				      PVA_KMD_WAIT_FW_TIMEOUT_US);
	if (err != PVA_SUCCESS) {
		// This might happen if FW is aborted. It's safe to keep going.
		pva_kmd_log_err("Failed to notify FW of buffer deinit");
	}
	pva_kmd_release_shared_buffer_handler(pva, interface);

	pva_kmd_shared_buffer_process(pva, interface);

	buffer->header = NULL;
	pva_kmd_device_memory_free(buffer->resource_memory);
	buffer->resource_memory = NULL;

	return err;
}

static void process_fw_event_msg(struct pva_kmd_device *pva, void *msg_body,
				 uint32_t msg_size)
{
	// TODO: This must be updated once profiler config is exposed through debugfs.
	//	 KMD must use the same timestamp size as the FW. It is possible that the user
	//	 changes the timestamp size through debugfs after FW logged the event.
	//	 FW must log the type of timestamp it used to capture the event.
	ASSERT(msg_size == sizeof(struct pva_fw_event_message) +
				   pva->debugfs_context.g_fw_profiling_config
					   .timestamp_size);

	pva_kmd_process_fw_event(pva, (uint8_t *)msg_body, msg_size);
}

static void process_trace_msg(struct pva_kmd_device *pva, uint32_t msg_type,
			      void *msg_body, uint32_t msg_size)
{
	switch (msg_type) {
#if PVA_ENABLE_FW_TRACEPOINTS == 1
	case PVA_KMD_FW_BUF_MSG_TYPE_FW_TRACEPOINT: {
		struct pva_fw_tracepoint tracepoint;
		ASSERT(msg_size == sizeof(struct pva_fw_tracepoint));
		memcpy(&tracepoint, msg_body, sizeof(tracepoint));
		pva_kmd_process_fw_tracepoint(pva, &tracepoint);
		break;
	}
#endif
#if PVA_ENABLE_NSYS_PROFILING == 1
	case PVA_KMD_FW_BUF_MSG_TYPE_CMD_BUF_TRACE: {
		struct pva_kmd_fw_msg_cmdbuf_trace cmdbuf_trace;
		ASSERT(msg_size == sizeof(struct pva_kmd_fw_msg_cmdbuf_trace));
		memcpy(&cmdbuf_trace, msg_body, sizeof(cmdbuf_trace));
		pva_kmd_nsys_cmdbuf_trace(pva, &cmdbuf_trace);
		break;
	}
	case PVA_KMD_FW_BUF_MSG_TYPE_VPU_TRACE: {
		struct pva_kmd_fw_msg_vpu_exec_trace vpu_trace;
		ASSERT(msg_size ==
		       sizeof(struct pva_kmd_fw_msg_vpu_exec_trace));
		memcpy(&vpu_trace, msg_body, sizeof(vpu_trace));
		// We do not check the profiling level here. FW checks profiling level while logging
		// the trace event. If the profiling level was high enough for FW to log the event,
		// KMD should trace it. The profiling level might have changed since FW logged the event.
		pva_kmd_nsys_vpu_exec_trace(pva, &vpu_trace);
		break;
	}
	case PVA_KMD_FW_BUF_MSG_TYPE_FENCE_TRACE: {
		struct pva_kmd_fw_msg_fence_trace fence_trace;
		ASSERT(msg_size == sizeof(struct pva_kmd_fw_msg_fence_trace));
		(void)memcpy(&fence_trace, (const uint8_t *)msg_body,
			     sizeof(fence_trace));
		pva_kmd_nsys_fence_trace(pva, &fence_trace);
		break;
	}
	case PVA_KMD_FW_BUF_MSG_TYPE_ENGINE_ACQUIRE_TRACE: {
		struct pva_kmd_fw_msg_engine_acquire_trace engine_acquire_trace;
		ASSERT(msg_size ==
		       sizeof(struct pva_kmd_fw_msg_engine_acquire_trace));
		(void)memcpy(&engine_acquire_trace, (const uint8_t *)msg_body,
			     sizeof(engine_acquire_trace));
		pva_kmd_nsys_engine_acquire_trace(pva, &engine_acquire_trace);
		break;
	}
#endif
	default: {
		// This should not happen as we only call this function for known message types.
		// Added for fixing the compiler warning.
		FAULT("Unexpected trace message type in process_trace_msg");
		break;
	}
	}
}

static void process_res_unreg_msg(struct pva_kmd_device *pva, uint8_t interface,
				  void *msg_body, uint32_t msg_size)
{
	struct pva_kmd_fw_msg_res_unreg unreg_data;
	struct pva_kmd_context *ctx = NULL;

	ASSERT(msg_size == sizeof(struct pva_kmd_fw_msg_res_unreg));
	(void)memcpy((void *)&unreg_data, (const void *)msg_body,
		     sizeof(unreg_data));
	ctx = pva_kmd_get_context(pva, interface);

	ASSERT(ctx != NULL);

	// We do not lock the resource table here because this function is intended
	// to be called from the shared buffer processing function which should acquire
	// the required lock.
	pva_kmd_drop_resource_unsafe(&ctx->ctx_resource_table,
				     unreg_data.resource_id);
}

static void shared_buffer_process_msg(struct pva_kmd_device *pva,
				      uint8_t interface, void *msg)
{
	struct pva_kmd_fw_buffer_msg_header header;
	void *msg_body;
	uint32_t msg_size;

	ASSERT(msg != NULL);

	// Copy the header
	(void)memcpy((void *)&header, (const void *)msg, sizeof(header));
	msg_size = safe_subu32(header.size, (uint32_t)sizeof(header));
	msg_body = (uint8_t *)msg + sizeof(header);

	switch (header.type) {
	case PVA_KMD_FW_BUF_MSG_TYPE_FW_EVENT:
		process_fw_event_msg(pva, msg_body, msg_size);
		break;
	case PVA_KMD_FW_BUF_MSG_TYPE_FW_TRACEPOINT:
	case PVA_KMD_FW_BUF_MSG_TYPE_CMD_BUF_TRACE:
	case PVA_KMD_FW_BUF_MSG_TYPE_VPU_TRACE:
	case PVA_KMD_FW_BUF_MSG_TYPE_FENCE_TRACE:
	case PVA_KMD_FW_BUF_MSG_TYPE_ENGINE_ACQUIRE_TRACE:
		process_trace_msg(pva, header.type, msg_body, msg_size);
		break;
	case PVA_KMD_FW_BUF_MSG_TYPE_RES_UNREG:
		process_res_unreg_msg(pva, interface, msg_body, msg_size);
		break;
	default:
		FAULT("Unexpected message type while processing shared buffer");
		break;
	}
}

void pva_kmd_shared_buffer_process(void *pva_dev, uint8_t interface)
{
	struct pva_kmd_device *pva;
	struct pva_kmd_shared_buffer *fw_buffer;
	uint32_t *buffer_head;
	uint32_t buffer_tail;
	uint32_t buffer_size;
	uint8_t *buffer_body;
	uint32_t element_size;
	uint8_t *current_element;
	uint64_t buffer_size_u64;

	pva = (struct pva_kmd_device *)pva_dev;
	fw_buffer = &pva->kmd_fw_buffers[interface];
	current_element = NULL;

	if (fw_buffer->lock_cb != NULL) {
		fw_buffer->lock_cb(pva, interface);
	}

	ASSERT(fw_buffer->resource_memory->size > sizeof(*fw_buffer->header));

	buffer_head = &fw_buffer->header->head;
	buffer_tail = fw_buffer->header->tail;
	/* buffer size bounded by resource memory allocation */
	buffer_size_u64 =
		fw_buffer->resource_memory->size - sizeof(*fw_buffer->header);
	ASSERT(buffer_size_u64 <= U32_MAX);
	buffer_size = (uint32_t)buffer_size_u64;
	buffer_body = fw_buffer->body;
	element_size = fw_buffer->header->element_size;

	ASSERT(buffer_body != NULL);

	// Ensure element size fits within the buffer
	ASSERT(buffer_size % element_size == 0U);

	// check buffer header to see if there was an overflow
	if ((fw_buffer->header->flags & PVA_KMD_FW_BUF_FLAG_OVERFLOW) != 0U) {
		// Clear the overflow flag
		// Note: this might be error prone. We are writing the flag here and at
		//       the same time, the FW might be updating the flag too. Since the
		//       flag is only being used to detect overflow today, we will ignore
		//	 this issue for now.
		fw_buffer->header->flags &= ~PVA_KMD_FW_BUF_FLAG_OVERFLOW;

		// Log the overflow
		pva_kmd_log_err_u64("Buffer overflow detected on interface",
				    interface);

		if (interface >= PVA_USER_CCQ_BASE) {
			// Buffers corresponding to user CCQs are used only for sending resource
			// unregistration requests to KMD.
			// If there is an overflow on this interface, we should abort the associated user
			// context in order to prevent further memory leak.
			// Note that ideally this should never happen as the buffer is expected to be
			// the same size as the resource table.
			// TODO: abort only the user context, not the device.
			pva_kmd_abort_fw(pva,
					 (enum pva_error)PVA_BUF_OUT_OF_RANGE);
		}

		// Buffer corresponding to CCQ 0 is used for sending messages common to a VM.
		// Today, these messages are only FW profiling and NSIGHT profiling messages.
		// Even if there is an overflow, we can continue processing the buffer.
		// We will drop the overflowed messages.
	}

	// Loop while `head` has not yet caught up to `tail`
	while (*buffer_head != buffer_tail) {
		// Ensure current position is valid
		ASSERT(*buffer_head < buffer_size);

		// Retrieve the current element in the buffer
		current_element = (void *)&buffer_body[*buffer_head];

		// Call the user-provided callback with the current element and context
		shared_buffer_process_msg(pva, interface, current_element);

		// Advance the head pointer in a circular buffer fashion
		*buffer_head = (*buffer_head + element_size) % buffer_size;
	}

	if (fw_buffer->unlock_cb != NULL) {
		fw_buffer->unlock_cb(pva, interface);
	}
}
