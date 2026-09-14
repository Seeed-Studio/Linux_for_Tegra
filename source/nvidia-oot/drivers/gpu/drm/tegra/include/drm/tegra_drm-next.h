/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2022, NVIDIA Corporation. All rights reserved.
 */

#ifndef __DRM_TEGRA_DRM_H
#define __DRM_TEGRA_DRM_H

#include <linux/types.h>

struct host1x_syncpt *tegra_drm_get_syncpt(int fd, u32 syncpt_id);

#endif
