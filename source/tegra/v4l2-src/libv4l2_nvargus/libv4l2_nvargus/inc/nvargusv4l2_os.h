/*
 * Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __NVARGUSV4L2_OS_H__
#define __NVARGUSV4L2_OS_H__

#include <stdlib.h>

typedef struct NvMutex_ *NvMutexHandle;
typedef struct NvSemaphore_ *NvSemaphoreHandle;
typedef struct NvThread_ *NvThreadHandle;


/** Entry point for a thread.
 */
typedef void (*NvThreadFunction)(void *args);

/**
 * Allocates a new process-local mutex.
 *
 * @note Mutexes can be locked recursively; if a thread owns the lock,
 * it can lock it again as long as it unlocks it an equal number of times.
 *
 * @param mutex The mutex to initialize.
 *
 * @return \a ENOMEM, or one of common error codes on
 * failure.
 */
int32_t NvMutexCreate(NvMutexHandle *mutex);

/** Locks the given unlocked mutex.
 *
 * @param mutex The mutex to lock; note it is a recursive lock.
 *
 * @return \a EINVAL, or one of common error codes on
 * failure.
 */
int32_t NvMutexAcquire(NvMutexHandle mutex);

/** Unlocks a locked mutex.
 *
 * @param mutex The mutex to unlock.
 *
 * @return \a EINVAL, or one of common error codes on
 * failure.
 */
int32_t NvMutexRelease(NvMutexHandle mutex);

/** Frees the resources held by a mutex.
 *
 * Mutexes will be destroyed only when last reference has gone away.
 *
 * @param mutex The mutex to destroy.
 *
 * @return \a EINVAL, or one of common error codes on
 * failure.
 */
int32_t NvMutexDestroy(NvMutexHandle mutex);

/** Creates a counting semaphore.
 *
 * @param semaphore A pointer to the semaphore to initialize.
 * @param value The initial semaphore value.
 *
 * @retval 0 on success, or the appropriate error code.
 */
int32_t NvSemaphoreCreate(NvSemaphoreHandle *semaphore, u_int32_t value);

/** Waits until the semaphore value becomes non-zero, then
 *  decrements the value and returns.
 *
 * @param semaphore The semaphore to wait for.
 */
void NvSemaphoreWait(NvSemaphoreHandle semaphore);

/** Increments the semaphore value.
 *
 * @param semaphore The semaphore to signal.
 */
void NvSemaphoreSignal(NvSemaphoreHandle semaphore);

/** Frees resources held by the semaphore.
 *
 *  Semaphores are reference counted across the multiprocesses,
 *  and will be destroyed when the last reference has
 *  gone away.
 *
 * @param semaphore The semaphore to destroy.
 *
 * @retval 0 on success, or the appropriate error code.
 */
int32_t NvSemaphoreDestroy(NvSemaphoreHandle semaphore);

/** Creates a thread.
 *
 *  @param function The thread entry point.
 *  @param args A pointer to the thread arguments.
 *  @param [out] thread A pointer to the result thread ID structure.
 *
 * @retval 0 on success, or the appropriate error code.
 */
int32_t NvThreadCreate(NvThreadFunction function, void *args,
                       NvThreadHandle *thread);

/** Assigns the given name to the given thread.
 *
 *  @param thread The thread to assign the name to.
 *  @param name thread name string.
 */
int32_t NvThreadSetName(NvThreadHandle thread, const char *name);

/** Waits for the given thread to exit.
 *
 *  The joined thread will be destroyed automatically. All OS resources
 *  will be reclaimed.
 *
 *  @param thread The thread to wait for.
 */
void NvThreadJoin(NvThreadHandle thread);

/** Returns current thread ID.
 *
 *  @retval ThreadId
 */
u_int64_t NvGetCurrentThreadId(void);

#endif /* __NVARGUSV4L2_OS_H__ */
