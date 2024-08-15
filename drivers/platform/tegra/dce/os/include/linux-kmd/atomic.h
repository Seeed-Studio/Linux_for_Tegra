/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#ifndef NVDISPLAY_SERVER_ATOMIC_LINUX_H
#define NVDISPLAY_SERVER_ATOMIC_LINUX_H

#include <linux/atomic.h>

/** 32 bit atomic variable. */
typedef atomic_t os_atomic_t;

static inline void os_atomic_set(os_atomic_t *v, int i)
{
    atomic_set(v, i);
    return;
}

static inline int os_atomic_read(os_atomic_t *v)
{
    return atomic_read(v);
}

static inline int os_atomic_add_unless(os_atomic_t *v, int a, int u)
{
    return atomic_add_unless(v, a, u);
}

#endif /* NVDISPLAY_SERVER_ATOMIC_LINUX_H */