// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2024, NVIDIA CORPORATION. All rights reserved.
 *
 * Some MM related functionality specific to nvmap.
 */

#include <trace/events/nvmap.h>
#include <linux/version.h>

#include <asm/pgtable.h>

#include "nvmap_priv.h"

void nvmap_zap_handle(struct nvmap_handle *handle, u64 offset, u64 size)
{
	pr_debug("%s is not supported!\n", __func__);
}
