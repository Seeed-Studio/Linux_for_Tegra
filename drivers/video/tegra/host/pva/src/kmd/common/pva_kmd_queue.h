/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_QUEUE_H
#define PVA_KMD_QUEUE_H
#include "pva_fw.h"
#include "pva_kmd_device_memory.h"
#include "pva_kmd_mutex.h"

struct pva_kmd_queue {
	struct pva_kmd_device *pva;
	struct pva_kmd_device_memory *queue_memory;
	struct pva_fw_submit_queue_header *queue_header;
	uint8_t ccq_id;
	uint8_t queue_id;
	uint32_t max_num_submit;
};

void pva_kmd_queue_init(struct pva_kmd_queue *queue, struct pva_kmd_device *pva,
			uint8_t ccq_id, uint8_t queue_id,
			struct pva_kmd_device_memory *queue_memory,
			uint32_t max_num_submit);

enum pva_error pva_kmd_queue_create(struct pva_kmd_context *ctx,
				    const struct pva_ops_queue_create *in_args,
				    uint32_t *queue_id);

enum pva_error pva_kmd_queue_destroy(struct pva_kmd_context *ctx,
				     uint32_t queue_id);

enum pva_error
pva_kmd_queue_submit(struct pva_kmd_queue *queue,
		     struct pva_fw_cmdbuf_submit_info const *submit_info);
uint32_t pva_kmd_queue_space(struct pva_kmd_queue *queue);

const struct pva_syncpt_rw_info *
pva_kmd_queue_get_rw_syncpt_info(struct pva_kmd_device *pva, uint8_t ccq_id,
				 uint8_t queue_id);

#endif // PVA_KMD_QUEUE_H
