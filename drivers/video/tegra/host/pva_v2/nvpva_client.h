/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
 */

#ifndef NVPVA_CLIENT_H
#define NVPVA_CLIENT_H

#include <linux/kref.h>
#include <linux/mutex.h>
#include "pva_vpu_exe.h"

struct pva;

struct nvpva_client_context {
	/* Reference to the device*/
	struct pva *pva;

	/* context device */
	struct platform_device *cntxt_dev;

	/* PID of client process which uses this context */
	pid_t pid;

	/* This tracks active users */
	u32 ref_count;

	u32 sid_index;

	/* Data structure to track pinned buffers for this client */
	struct nvpva_buffers *buffers;

	u32 curr_sema_value;
	struct mutex sema_val_lock;

	/* Data structure to track elf context for vpu parsing */
	struct nvpva_elf_context elf_ctx;
};

struct pva;
int nvpva_client_context_init(struct pva *pva);
void nvpva_client_context_deinit(struct pva *pva);
void nvpva_client_context_get(struct nvpva_client_context *client);
struct nvpva_client_context
*nvpva_client_context_alloc(struct platform_device *pdev,
			    struct pva *dev,
			    pid_t pid);
void nvpva_client_context_put(struct nvpva_client_context *client);

#endif /* NVPVA_CLIENT_H */
