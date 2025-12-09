/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_THREAD_SEMA_H
#define PVA_KMD_THREAD_SEMA_H

#include "pva_api.h"

#if defined(__KERNEL__) /* For Linux */

#include <linux/semaphore.h>
/**
 * @brief Semaphore type alias for kernel space
 *
 * @details Platform-specific semaphore implementation that provides
 * cross-platform abstraction for semaphore operations in the PVA KMD.
 * The underlying implementation varies by platform but provides
 * consistent interface for synchronization operations.
 */
typedef struct semaphore pva_kmd_sema_t;

#include <linux/atomic.h>
/**
 * @brief Atomic integer type alias for kernel space
 *
 * @details Platform-specific atomic integer implementation that provides
 * cross-platform abstraction for atomic operations in the PVA KMD.
 * The underlying implementation varies by platform but provides
 * consistent interface for thread-safe integer operations.
 */
typedef atomic_t pva_kmd_atomic_t;

#else /* For user space code, including QNX KMD */

#include <semaphore.h>
/**
 * @brief Semaphore type alias for user space
 *
 * @details Platform-specific semaphore implementation that provides
 * cross-platform abstraction for semaphore operations in the PVA KMD.
 * The underlying implementation varies by platform but provides
 * consistent interface for synchronization operations.
 */
typedef sem_t pva_kmd_sema_t;

// clang-format off
#ifdef __cplusplus
#include <atomic>
/**
 * @brief Atomic integer type alias for C++ user space
 *
 * @details Platform-specific atomic integer implementation that provides
 * cross-platform abstraction for atomic operations in the PVA KMD.
 * The underlying implementation varies by platform but provides
 * consistent interface for thread-safe integer operations.
 */
// The strange format is to make kernel patch check script happy
typedef std::atomic < int > pva_kmd_atomic_t;
#else
#include <stdatomic.h>
/**
 * @brief Atomic integer type alias for C user space
 *
 * @details Platform-specific atomic integer implementation that provides
 * cross-platform abstraction for atomic operations in the PVA KMD.
 * The underlying implementation varies by platform but provides
 * consistent interface for thread-safe integer operations.
 */
typedef atomic_int pva_kmd_atomic_t;
#endif
// clang-format on

#endif

/**
 * @brief Initialize a semaphore
 *
 * @details This function initializes a semaphore with the specified
 * initial value and prepares it for use with other semaphore operations.
 * The initialization process sets up platform-specific semaphore resources
 * and configures the semaphore for thread synchronization operations.
 *
 * The semaphore must be deinitialized using @ref pva_kmd_sema_deinit()
 * when no longer needed to prevent resource leaks.
 *
 * @param[in, out] sem  Pointer to the semaphore structure to initialize
 *                      Valid value: non-null
 * @param[in] val       Initial value of the semaphore
 *                      Valid range: [0 .. UINT32_MAX]
 */
void pva_kmd_sema_init(pva_kmd_sema_t *sem, uint32_t val);

/**
 * @brief Wait on a semaphore with timeout
 *
 * @details This function attempts to acquire the semaphore by decrementing
 * its count atomically. If the count is greater than zero, the function
 * decrements the count and returns immediately. If the count is zero,
 * the function blocks the calling thread until either the semaphore becomes
 * available or the specified timeout expires.
 *
 * The function provides timeout-based semaphore acquisition to prevent
 * indefinite blocking in case of synchronization issues.
 *
 * @param[in, out] sem         Pointer to the semaphore to wait on
 *                             Valid value: non-null, previously initialized
 * @param[in] timeout_ms       Timeout in milliseconds
 *                             Valid range: [0 .. UINT32_MAX]
 *
 * @retval PVA_SUCCESS         Semaphore was successfully acquired within
 *                             the timeout period
 * @retval PVA_TIMEDOUT        Semaphore was not acquired within the
 *                             specified timeout period
 */
enum pva_error pva_kmd_sema_wait_timeout(pva_kmd_sema_t *sem,
					 uint32_t timeout_ms);

/**
 * @brief Signal a semaphore
 *
 * @details This function atomically increments the semaphore count and
 * wakes up one waiting thread if any threads are blocked waiting for
 * the semaphore. The operation is thread-safe and can be called from
 * multiple threads concurrently.
 *
 * This function enables synchronization between threads by releasing
 * semaphore resources that waiting threads can acquire.
 *
 * @param[in, out] sem  Pointer to the semaphore to signal
 *                      Valid value: non-null, previously initialized
 */
void pva_kmd_sema_post(pva_kmd_sema_t *sem);

/**
 * @brief Deinitialize a semaphore
 *
 * @details This function cleans up platform-specific semaphore resources
 * and releases any system resources associated with the semaphore.
 * After calling this function, the semaphore becomes invalid for
 * further use until re-initialized.
 *
 * This function must be called for every semaphore initialized with
 * @ref pva_kmd_sema_init() to prevent resource leaks.
 *
 * @param[in, out] sem  Pointer to the semaphore to deinitialize
 *                      Valid value: non-null, previously initialized
 */
void pva_kmd_sema_deinit(pva_kmd_sema_t *sem);

/**
 * @brief Store a value atomically
 *
 * @details This function atomically stores the specified value in the
 * atomic variable, ensuring memory ordering guarantees across different
 * CPU architectures. The operation provides thread-safe write access
 * to shared integer variables without race conditions.
 *
 * @param[in, out] a_var  Pointer to the atomic variable to modify
 *                        Valid value: non-null
 * @param[in] val         Value to store in the atomic variable
 *                        Valid range: [INT_MIN .. INT_MAX]
 */
void pva_kmd_atomic_store(pva_kmd_atomic_t *a_var, int val);

/**
 * @brief Atomically add a value and return the previous value
 *
 * @details This function atomically adds the specified value to the
 * atomic variable and returns the value that was stored before the
 * addition. The operation ensures memory ordering guarantees and
 * provides thread-safe read-modify-write access to shared variables.
 *
 * @param[in, out] a_var  Pointer to the atomic variable to modify
 *                        Valid value: non-null
 * @param[in] val         Value to add to the atomic variable
 *                        Valid range: [INT_MIN .. INT_MAX]
 *
 * @retval previous_value The value stored in the atomic variable
 *                        before the addition operation
 */
int pva_kmd_atomic_fetch_add(pva_kmd_atomic_t *a_var, int val);

/**
 * @brief Atomically subtract a value and return the previous value
 *
 * @details This function atomically subtracts the specified value from
 * the atomic variable and returns the value that was stored before the
 * subtraction. The operation ensures memory ordering guarantees and
 * provides thread-safe read-modify-write access to shared variables.
 *
 * @param[in, out] a_var  Pointer to the atomic variable to modify
 *                        Valid value: non-null
 * @param[in] val         Value to subtract from the atomic variable
 *                        Valid range: [INT_MIN .. INT_MAX]
 *
 * @retval previous_value The value stored in the atomic variable
 *                        before the subtraction operation
 */
int pva_kmd_atomic_fetch_sub(pva_kmd_atomic_t *a_var, int val);

/**
 * @brief Load a value atomically
 *
 * @details This function atomically reads the current value from the
 * atomic variable, ensuring memory ordering guarantees across different
 * CPU architectures. The operation provides thread-safe read access
 * to shared integer variables without race conditions or partial reads.
 *
 * @param[in] a_var       Pointer to the atomic variable to read
 *                        Valid value: non-null
 *
 * @retval current_value  The current value stored in the atomic variable
 */
int pva_kmd_atomic_load(pva_kmd_atomic_t *a_var);

#endif // PVA_KMD_THREAD_SEMA_H
