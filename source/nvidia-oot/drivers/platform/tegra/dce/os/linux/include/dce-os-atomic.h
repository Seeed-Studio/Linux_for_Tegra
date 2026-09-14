/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef DCE_OS_ATOMIC_H
#define DCE_OS_ATOMIC_H

#include <linux/atomic.h>

/** 32 bit atomic variable. */
typedef atomic_t dce_os_atomic_t;

static inline void dce_os_atomic_set(dce_os_atomic_t *v, int i)
{
	atomic_set(v, i);
}

static inline int dce_os_atomic_read(dce_os_atomic_t *v)
{
	return atomic_read(v);
}

static inline int dce_os_atomic_add_unless(dce_os_atomic_t *v, int a, int u)
{
	return atomic_add_unless(v, a, u);
}

#endif /* DCE_OS_ATOMIC_H */
