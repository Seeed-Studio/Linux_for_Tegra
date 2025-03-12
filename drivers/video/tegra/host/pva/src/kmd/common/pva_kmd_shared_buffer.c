// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_abort.h"
#include "pva_kmd_device.h"
#include "pva_kmd_shared_buffer.h"

static void
setup_cmd_init_shared_dram_buffer(void *cmd, uint8_t interface,
				  struct pva_kmd_shared_buffer *fw_buffer)
{
	struct pva_cmd_init_shared_dram_buffer *init_cmd =
		(struct pva_cmd_init_shared_dram_buffer *)cmd;

	pva_kmd_set_cmd_init_shared_dram_buffer(
		init_cmd, interface, fw_buffer->resource_memory->iova,
		fw_buffer->resource_memory->size);
}

static void
setup_cmd_deinit_shared_dram_buffer(void *cmd, uint8_t interface,
				    struct pva_kmd_shared_buffer *fw_buffer)
{
	struct pva_cmd_deinit_shared_dram_buffer *deinit_cmd =
		(struct pva_cmd_deinit_shared_dram_buffer *)cmd;

	pva_kmd_set_cmd_deinit_shared_dram_buffer(deinit_cmd, interface);
}

static enum pva_error
notify_fw(struct pva_kmd_device *pva, uint8_t interface,
	  void (*setup_cmd_cb)(void *cmd, uint8_t interface,
			       struct pva_kmd_shared_buffer *fw_buffer),
	  size_t cmd_size)
{
	enum pva_error err;
	struct pva_kmd_cmdbuf_builder builder;
	struct pva_kmd_submitter *dev_submitter = &pva->submitter;
	struct pva_kmd_shared_buffer *fw_buffer;
	void *cmd_space;
	uint32_t fence_val;

	ASSERT(interface < PVA_MAX_NUM_CCQ);

	fw_buffer = &pva->kmd_fw_buffers[interface];

	err = pva_kmd_submitter_prepare(dev_submitter, &builder);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	// Make sure FW buffer was allocated
	ASSERT(fw_buffer->header != NULL);

	cmd_space = pva_kmd_reserve_cmd_space(&builder, cmd_size);
	ASSERT(cmd_space != NULL);

	// Let the setup callback configure the specific command
	setup_cmd_cb(cmd_space, interface, fw_buffer);

	err = pva_kmd_submitter_submit(dev_submitter, &builder, &fence_val);
	if (err != PVA_SUCCESS) {
		// Error is either QUEUE_FULL or TIMEDOUT
		goto cancel_builder;
	}

	err = pva_kmd_submitter_wait(dev_submitter, fence_val,
				     PVA_KMD_WAIT_FW_POLL_INTERVAL_US,
				     PVA_KMD_WAIT_FW_TIMEOUT_US);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err(
			"Waiting for FW timed out while processing buffer command");
		goto err_out;
	}

	return PVA_SUCCESS;

cancel_builder:
	pva_kmd_cmdbuf_builder_cancel(&builder);
err_out:
	return err;
}

enum pva_error pva_kmd_shared_buffer_init(
	struct pva_kmd_device *pva, uint8_t interface, uint32_t element_size,
	uint32_t num_entries, shared_buffer_process_element_cb process_cb,
	shared_buffer_lock_cb lock_cb, shared_buffer_lock_cb unlock_cb)
{
	enum pva_error err = PVA_SUCCESS;

	struct pva_kmd_device_memory *device_memory;
	struct pva_kmd_shared_buffer *buffer;
	uint64_t buffer_size;

	ASSERT(interface < PVA_MAX_NUM_CCQ);
	buffer = &pva->kmd_fw_buffers[interface];

	// Ensure that the buffer body is a multiple of 'element size'
	buffer_size = safe_mulu64(num_entries, element_size);
	buffer_size = safe_addu64(buffer_size,
				  sizeof(struct pva_fw_shared_buffer_header));

	device_memory = pva_kmd_device_memory_alloc_map(
		buffer_size, pva, PVA_ACCESS_RW, PVA_R5_SMMU_CONTEXT_ID);
	if (device_memory == NULL) {
		return PVA_NOMEM;
	}

	buffer->header =
		(struct pva_fw_shared_buffer_header *)device_memory->va;
	buffer->header->flags = 0U;
	buffer->header->element_size = element_size;
	buffer->header->head = 0U;
	buffer->header->tail = 0U;
	buffer->body =
		(pva_offset_pointer(buffer->header, sizeof(*buffer->header)));
	buffer->process_cb = process_cb;
	buffer->lock_cb = lock_cb;
	buffer->unlock_cb = unlock_cb;
	buffer->resource_offset = 0U;
	buffer->resource_memory = device_memory;

	err = pva_kmd_bind_shared_buffer_handler(pva, interface, pva);
	if (err != PVA_SUCCESS) {
		goto free_buffer_memory;
	}

	err = notify_fw(pva, interface, setup_cmd_init_shared_dram_buffer,
			sizeof(struct pva_cmd_init_shared_dram_buffer));
	if (err != PVA_SUCCESS) {
		goto release_handler;
	}

	return err;

release_handler:
	pva_kmd_release_shared_buffer_handler(pva, interface);
free_buffer_memory:
	pva_kmd_device_memory_free(device_memory);
	return err;
}

enum pva_error pva_kmd_shared_buffer_deinit(struct pva_kmd_device *pva,
					    uint8_t interface)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_kmd_shared_buffer *buffer;

	ASSERT(interface < PVA_MAX_NUM_CCQ);
	buffer = &pva->kmd_fw_buffers[interface];

	if (!pva->recovery) {
		err = notify_fw(
			pva, interface, setup_cmd_deinit_shared_dram_buffer,
			sizeof(struct pva_cmd_deinit_shared_dram_buffer));
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err("Failed to deinit FW buffer");
		}
	}
	pva_kmd_release_shared_buffer_handler(pva, interface);

	pva_kmd_shared_buffer_process(pva, interface);

	pva_kmd_device_memory_free(buffer->resource_memory);
	buffer->resource_memory = NULL;

	return err;
}

void pva_kmd_shared_buffer_process(void *pva_dev, uint8_t interface)
{
	struct pva_kmd_device *pva = (struct pva_kmd_device *)pva_dev;
	struct pva_kmd_shared_buffer *fw_buffer =
		&pva->kmd_fw_buffers[interface];
	uint32_t *buffer_head;
	uint32_t buffer_tail;
	uint32_t buffer_size;
	uint8_t *buffer_body;
	uint32_t element_size;
	uint8_t *current_element = NULL;

	ASSERT(fw_buffer->resource_memory->size > sizeof(*fw_buffer->header));

	buffer_head = &fw_buffer->header->head;
	buffer_tail = fw_buffer->header->tail;
	buffer_size =
		fw_buffer->resource_memory->size - sizeof(*fw_buffer->header);
	buffer_body = fw_buffer->body;
	element_size = fw_buffer->header->element_size;

	ASSERT(buffer_body != NULL);

	// Ensure element size fits within the buffer
	ASSERT(buffer_size % element_size == 0);

	// check buffer header to see if there was an overflow
	if (fw_buffer->header->flags & PVA_KMD_FW_BUF_FLAG_OVERFLOW) {
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
			pva_kmd_abort(pva);
		}

		// Buffer corresponding to CCQ 0 is used for sending messages common to a VM.
		// Today, these messages are only FW profiling and NSIGHT profiling messages.
		// Even if there is an overflow, we can continue processing the buffer.
		// We will drop the overflowed messages.
	}

	if (fw_buffer->lock_cb != NULL) {
		fw_buffer->lock_cb(pva, interface);
	}

	// Loop while `head` has not yet caught up to `tail`
	while (*buffer_head != buffer_tail) {
		// Ensure current position is valid
		ASSERT(*buffer_head < buffer_size);

		// Retrieve the current element in the buffer
		current_element = (void *)&buffer_body[*buffer_head];

		// Call the user-provided callback with the current element and context
		fw_buffer->process_cb(pva, interface, current_element);

		// Advance the head pointer in a circular buffer fashion
		*buffer_head = (*buffer_head + element_size) % buffer_size;
	}

	if (fw_buffer->unlock_cb != NULL) {
		fw_buffer->unlock_cb(pva, interface);
	}
}
