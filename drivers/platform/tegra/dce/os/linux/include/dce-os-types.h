/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef DCE_OS_TYPES_H
#define DCE_OS_TYPES_H

#include <linux/types.h>

/**
 * TODO: TDS-16678:
 * 1. Certain core files using EXPORT_SYMBOL() are re-used between
 *    DCE-KMD and HVRTOS. So we need to create a OS agnostic
 *    wrapper around this.
 * 2. We could move EXPORT_SYMBOL() from the file where symbols are
 *    defined to a different linux specific file, but that results
 *    in svcacv warning.
 * 3. Defining this wrapper macro here as we don't have any other
 *    appropriate OS specific header to put this to.
 */
#define DCE_EXPORT_SYMBOL(x) EXPORT_SYMBOL(x)

#endif /* DCE_OS_TYPES_H */
