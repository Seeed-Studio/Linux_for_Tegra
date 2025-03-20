/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_SHARED_BUFFER_H
#define PVA_KMD_SHARED_BUFFER_H

#include "pva_kmd_device.h"

typedef enum pva_error (*shared_buffer_process_element_cb)(void *context,
							   uint8_t interface,
							   uint8_t *element);

typedef void (*shared_buffer_lock_cb)(struct pva_kmd_device *pva,
				      uint8_t interface);

struct pva_kmd_shared_buffer {
	// Only 'header' is in shared DRAM memory
	// Other fields are local to KMD and should be used for internal bookkeeping
	struct pva_fw_shared_buffer_header *header;
	// 'body' tracks the begining of buffer contents in DRAM
	uint8_t *body;
	// 'process_cb' callback is used to process elements in the buffer
	shared_buffer_process_element_cb process_cb;
	// 'lock_cb' callback is used to lock the buffer
	shared_buffer_lock_cb lock_cb;
	// 'unlock_cb' callback is used to unlock the buffer
	shared_buffer_lock_cb unlock_cb;
	// 'resource_memory' is used to track the memory allocated for the buffer
	struct pva_kmd_device_memory *resource_memory;
	// 'resource_offset' is used to track offset of buffer in 'resource_id'
	uint32_t resource_offset;
};

enum pva_error pva_kmd_shared_buffer_init(struct pva_kmd_device *pva,
					  uint8_t interface,
					  uint32_t element_size,
					  uint32_t buffer_size,
					  shared_buffer_lock_cb lock_cb,
					  shared_buffer_lock_cb unlock_cb);

enum pva_error pva_kmd_shared_buffer_deinit(struct pva_kmd_device *pva,
					    uint8_t interface);

void pva_kmd_shared_buffer_process(void *pva_dev, uint8_t interface);

enum pva_error pva_kmd_bind_shared_buffer_handler(void *pva_dev,
						  uint8_t interface,
						  void *data);

void pva_kmd_release_shared_buffer_handler(void *pva_dev, uint8_t interface);
#endif
