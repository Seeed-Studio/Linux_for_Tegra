/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_THREAD_SEMA_H
#define PVA_KMD_THREAD_SEMA_H

#include "pva_api.h"

#if defined(__KERNEL__) /* For Linux */

#include <linux/semaphore.h>
typedef struct semaphore pva_kmd_sema_t;

#else /* For user space code, including QNX KMD */

#include <semaphore.h>
/* Mutex */
typedef sem_t pva_kmd_sema_t;

#endif

/**
 * @brief Initialize a semaphore.
 *
 * @param sem Pointer to the semaphore.
 * @param val Initial value of the semaphore.
 */
void pva_kmd_sema_init(pva_kmd_sema_t *sem, uint32_t val);

/**
 * @brief Wait on a semaphore.
 *
 * Decrement the semaphore count. If the count is zero, the caller will block
 * until the semaphore is posted or the timeout expires.
 *
 * @param sem Pointer to the semaphore.
 * @param timeout_ms Timeout in milliseconds.
 *
 * @retval PVA_SUCCESS if the semaphore was successfully acquired.
 * @retval PVA_TIMEDOUT if the semaphore was not acquired within the timeout.
 */
enum pva_error pva_kmd_sema_wait_timeout(pva_kmd_sema_t *sem,
					 uint32_t timeout_ms);

/**
 * @brief Signal a semaphore.
 *
 * Increment the semaphore count.
 *
 * @param sem Pointer to the semaphore.
 */
void pva_kmd_sema_post(pva_kmd_sema_t *sem);

/**
 * @brief Deinitialize a semaphore.
 *
 * @param sem Pointer to the semaphore.
 */
void pva_kmd_sema_deinit(pva_kmd_sema_t *sem);

#endif // PVA_KMD_THREAD_SEMA_H
