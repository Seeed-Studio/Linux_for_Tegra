/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
 *
 * Tegra TSEC T264 Module Support
 */

#ifndef TSEC_T264_H
#define TSEC_T264_H

#include "tsec_linux.h"

/*
 * Initialize TSEC T264 initialization sequence
 */
int tsec_t264_init(struct platform_device *dev);

#endif /* TSEC_T264_H */