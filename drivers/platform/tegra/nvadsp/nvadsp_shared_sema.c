// SPDX-License-Identifier: GPL-2.0-only
/**
 * Copyright (c) 2014-2023, NVIDIA CORPORATION. All rights reserved.
 */

#include <linux/tegra_nvadsp.h>

nvadsp_shared_sema_t *
nvadsp_shared_sema_init(uint8_t nvadsp_shared_sema_id)
{
	return NULL;
}

status_t nvadsp_shared_sema_destroy(nvadsp_shared_sema_t *sema)
{
	return -ENOENT;
}

status_t nvadsp_shared_sema_acquire(nvadsp_shared_sema_t *sema)
{
	return -ENOENT;
}

status_t nvadsp_shared_sema_release(nvadsp_shared_sema_t *sema)
{
	return -ENOENT;
}

