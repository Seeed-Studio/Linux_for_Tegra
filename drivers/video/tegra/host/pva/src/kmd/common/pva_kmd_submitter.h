/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_SUBMITTER_H
#define PVA_KMD_SUBMITTER_H
#include "pva_kmd_cmdbuf.h"
#include "pva_kmd_mutex.h"
#include "pva_kmd_queue.h"

/** A thread-safe submitter utility */
struct pva_kmd_submitter {
	/** The lock protects the submission to the queue, including
	 * incrementing the post fence */
	pva_kmd_mutex_t *submit_lock;
	struct pva_kmd_queue *queue;
	uint32_t *post_fence_va;
	struct pva_fw_postfence post_fence;
	uint32_t fence_future_value;

	/** This lock protects the use of the chunk_pool*/
	pva_kmd_mutex_t *chunk_pool_lock;
	struct pva_kmd_cmdbuf_chunk_pool *chunk_pool;
};

void pva_kmd_submitter_init(struct pva_kmd_submitter *submitter,
			    struct pva_kmd_queue *queue,
			    pva_kmd_mutex_t *submit_lock,
			    struct pva_kmd_cmdbuf_chunk_pool *chunk_pool,
			    pva_kmd_mutex_t *chunk_pool_lock,
			    uint32_t *post_fence_va,
			    struct pva_fw_postfence const *post_fence);

enum pva_error
pva_kmd_submitter_prepare(struct pva_kmd_submitter *submitter,
			  struct pva_kmd_cmdbuf_builder *builder);

enum pva_error pva_kmd_submitter_submit(struct pva_kmd_submitter *submitter,
					struct pva_kmd_cmdbuf_builder *builder,
					uint32_t *out_fence_val);
enum pva_error pva_kmd_submitter_wait(struct pva_kmd_submitter *submitter,
				      uint32_t fence_val,
				      uint32_t poll_interval_ms,
				      uint32_t timeout_ms);
enum pva_error
pva_kmd_submitter_submit_with_fence(struct pva_kmd_submitter *submitter,
				    struct pva_kmd_cmdbuf_builder *builder,
				    struct pva_fw_postfence *fence);

/* prepare submission */
/* add cmd */
/* add cmd */
/* do submit -> fence value */
/* wait for fence */

/* prepare submission */
/* add cmd */
/* add cmd */
/* do submit with fence (provide a fence) */

/* Helper function to submit several commands and wait for them to complete.
Total size must be smaller than a chunk. */
enum pva_error pva_kmd_submit_cmd_sync(struct pva_kmd_submitter *submitter,
				       void *cmds, uint32_t size,
				       uint32_t poll_interval_us,
				       uint32_t timeout_us);

#endif // PVA_KMD_SUBMITTER_H
