/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_MUTEX_H
#define PVA_KMD_MUTEX_H

#include "pva_api.h"

#if defined(__KERNEL__) /* For Linux */

#include <linux/mutex.h>
typedef struct mutex pva_kmd_mutex_t;

#else /* For user space code, including QNX KMD */

#include <pthread.h>
/* Mutex */
typedef pthread_mutex_t pva_kmd_mutex_t;

#endif

enum pva_error pva_kmd_mutex_init(pva_kmd_mutex_t *m);
void pva_kmd_mutex_lock(pva_kmd_mutex_t *m);
void pva_kmd_mutex_unlock(pva_kmd_mutex_t *m);
void pva_kmd_mutex_deinit(pva_kmd_mutex_t *m);

#endif // PVA_KMD_MUTEX_H
