/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_MUTEX_H
#define PVA_KMD_MUTEX_H

#include "pva_api.h"

#if defined(__KERNEL__) /* For Linux */

#include <linux/mutex.h>
/**
 * @brief Mutex type alias for Linux kernel space
 *
 * @details Platform-specific mutex implementation that maps to the Linux
 * kernel mutex structure. This provides cross-platform abstraction for
 * mutex operations in the PVA KMD, allowing the same code to work across
 * different operating system environments.
 */
typedef struct mutex pva_kmd_mutex_t;

#else /* For user space code, including QNX KMD */

#include <pthread.h>
/**
 * @brief Mutex type alias for user space (QNX and other platforms)
 *
 * @details Platform-specific mutex implementation that maps to POSIX
 * pthread mutex for user space environments including QNX KMD. This
 * provides cross-platform abstraction for mutex operations, enabling
 * consistent synchronization primitives across different platforms.
 */
typedef pthread_mutex_t pva_kmd_mutex_t;

#endif

/**
 * @brief Initialize a PVA KMD mutex
 *
 * @details This function performs the following operations:
 * - Initializes the platform-specific mutex structure
 * - Sets up appropriate mutex attributes for the target platform
 * - Configures the mutex for proper operation in kernel or user space
 * - Ensures the mutex is ready for lock/unlock operations
 * - Handles platform-specific initialization requirements
 *
 * The mutex must be initialized before it can be used for synchronization.
 * After successful initialization, the mutex can be used with
 * @ref pva_kmd_mutex_lock() and @ref pva_kmd_mutex_unlock(). The mutex
 * should be deinitialized using @ref pva_kmd_mutex_deinit() when no
 * longer needed.
 *
 * @param[in, out] m  Pointer to @ref pva_kmd_mutex_t structure to initialize
 *                    Valid value: non-null
 *
 * @retval PVA_SUCCESS           Mutex initialized successfully
 * @retval PVA_INTERNAL         Platform-specific mutex initialization failed
 */
enum pva_error pva_kmd_mutex_init(pva_kmd_mutex_t *m);

/**
 * @brief Acquire a lock on the PVA KMD mutex
 *
 * @details This function performs the following operations:
 * - Attempts to acquire an exclusive lock on the specified mutex
 * - Blocks the calling thread if the mutex is already locked by another thread
 * - Provides mutual exclusion for critical sections of code
 * - Uses platform-appropriate locking mechanisms (kernel or user space)
 * - Ensures atomic access to shared resources protected by the mutex
 *
 * The calling thread will block until the mutex becomes available. Once
 * acquired, the thread has exclusive access to resources protected by this
 * mutex until @ref pva_kmd_mutex_unlock() is called. The mutex must be
 * initialized using @ref pva_kmd_mutex_init() before calling this function.
 *
 * @param[in, out] m  Pointer to @ref pva_kmd_mutex_t structure to lock
 *                    Valid value: non-null, must be initialized
 */
void pva_kmd_mutex_lock(pva_kmd_mutex_t *m);

/**
 * @brief Release a lock on the PVA KMD mutex
 *
 * @details This function performs the following operations:
 * - Releases the exclusive lock on the specified mutex
 * - Allows other waiting threads to acquire the mutex
 * - Signals completion of the critical section protected by the mutex
 * - Uses platform-appropriate unlocking mechanisms
 * - Maintains proper synchronization state for subsequent operations
 *
 * This function must only be called by the thread that currently holds
 * the mutex lock. After unlocking, other threads waiting on the mutex
 * may acquire the lock. The mutex must have been previously locked using
 * @ref pva_kmd_mutex_lock() before calling this function.
 *
 * @param[in, out] m  Pointer to @ref pva_kmd_mutex_t structure to unlock
 *                    Valid value: non-null, must be currently locked by calling thread
 */
void pva_kmd_mutex_unlock(pva_kmd_mutex_t *m);

/**
 * @brief Deinitialize a PVA KMD mutex and free resources
 *
 * @details This function performs the following operations:
 * - Deinitializes the platform-specific mutex structure
 * - Frees any resources allocated during mutex initialization
 * - Ensures proper cleanup of platform-specific mutex state
 * - Marks the mutex as invalid for future use
 * - Handles platform-specific deinitialization requirements
 *
 * The mutex must not be locked when this function is called, and no
 * threads should be waiting on the mutex. After deinitialization, the
 * mutex cannot be used for synchronization until it is reinitialized
 * using @ref pva_kmd_mutex_init().
 *
 * @param[in, out] m  Pointer to @ref pva_kmd_mutex_t structure to deinitialize
 *                    Valid value: non-null, must be initialized and unlocked
 */
void pva_kmd_mutex_deinit(pva_kmd_mutex_t *m);

#endif // PVA_KMD_MUTEX_H
