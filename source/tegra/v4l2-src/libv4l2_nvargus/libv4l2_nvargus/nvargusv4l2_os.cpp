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

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>

#include "nvargusv4l2_os.h"

typedef struct NvMutex_
{
    pthread_mutex_t mutex;
    u_int32_t count;
} NvMutex;

typedef struct NvSemaphore_
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    u_int32_t count; /* Sema value*/
} NvSemaphore;

typedef struct NvThread_
{
    pthread_t thread;
} NvThread;

typedef struct
{
    NvThreadFunction function;
    NvThread *thread;
    pthread_mutex_t barrier;
    void *thread_args;
    NvSemaphoreHandle init;
} NvThreadArgs;

int32_t NvMutexCreate(NvMutexHandle *mHandle)
{
    NvMutex *m;
    pthread_mutexattr_t attr;

    m = (NvMutex *)malloc(sizeof(NvMutex));
    if (!m)
    {
        *mHandle = NULL;
        return ENOMEM;
    }

    /* Mutex initialization for local process */
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&m->mutex, &attr);
    pthread_mutexattr_destroy(&attr);

    m->count = 0;
    *mHandle = m;
    return 0;
}

int32_t NvMutexAcquire(NvMutexHandle mHandle)
{
    int32_t val = 0;

    if (!mHandle)
        return EINVAL;

    val = pthread_mutex_lock(&mHandle->mutex);
    mHandle->count++;
    if (val)
        return val;

    return 0;
}

int32_t NvMutexRelease(NvMutexHandle mHandle)
{
    int32_t val = 0;

    if (!mHandle)
        return EINVAL;

    mHandle->count--;
    val = pthread_mutex_unlock(&mHandle->mutex);
    if (val)
        return val;

    return 0;
}

int32_t NvMutexDestroy(NvMutexHandle mHandle)
{
    int32_t val = 0;

    if (!mHandle)
        return EINVAL;

    val = pthread_mutex_destroy(&mHandle->mutex);
    if (val)
        return val;

    free(mHandle);
    return 0;
}

int32_t NvSemaphoreCreate(NvSemaphoreHandle *semaHandle, u_int32_t cnt)
{
    int32_t val = 0;
    NvSemaphore *sema = (NvSemaphore *)malloc(sizeof(NvSemaphore));
    if (!sema)
        return ENOMEM;

    val = pthread_mutex_init(&sema->mutex, 0);
    if (val)
    {
        free(sema);
        return val;
    }
    val = pthread_cond_init(&sema->cond, 0);
    if (val)
    {
        pthread_mutex_destroy(&sema->mutex);
        free(sema);
        return val;
    }
    sema->count = cnt;
    *semaHandle = sema;

    return 0;
}

void NvSemaphoreWait(NvSemaphoreHandle semaHandle)
{
    if (!semaHandle)
        return;

    pthread_mutex_lock(&semaHandle->mutex);
    while (!semaHandle->count)
    {
        pthread_cond_wait(&semaHandle->cond, &semaHandle->mutex);
    }
    semaHandle->count--;
    pthread_mutex_unlock(&semaHandle->mutex);
}

void NvSemaphoreSignal(NvSemaphoreHandle semaHandle)
{
    if (!semaHandle)
        return;
    pthread_mutex_lock(&semaHandle->mutex);
    semaHandle->count++;
    pthread_cond_signal(&semaHandle->cond);
    pthread_mutex_unlock(&semaHandle->mutex);
}

int32_t NvSemaphoreDestroy(NvSemaphoreHandle semaHandle)
{
    int32_t val_m = 0, val_c = 0;
    if (!semaHandle)
        return EINVAL;

    val_m = pthread_mutex_destroy(&semaHandle->mutex);
    val_c = pthread_cond_destroy(&semaHandle->cond);

    free(semaHandle);

    return (!val_m && !val_c) ? 0 : EINVAL;
}

static void *thread_wrapper(void *pThread)
{
    NvThreadArgs *args = (NvThreadArgs *)pThread;

    if (!args)
        return NULL;
    NvSemaphoreSignal(args->init);
    pthread_mutex_lock(&args->barrier);
    pthread_mutex_unlock(&args->barrier);

    args->function(args->thread_args);
    pthread_mutex_destroy(&args->barrier);
    NvSemaphoreDestroy(args->init);
    free(args);
    return 0;
}

int32_t NvThreadCreate(
    NvThreadFunction function,
    void *tArgs,
    NvThreadHandle *tHandle)
{
    int32_t err;
    NvThread *thrd = 0;
    NvThreadArgs *args = 0;

    if (!function || !tHandle)
        return EINVAL;

    /* create the thread struct */
    thrd = (NvThread *)malloc(sizeof(NvThread));
    if (thrd == NULL)
        goto fail;
    memset(thrd, 0x0, sizeof(NvThread));

    /* setup the thread args */
    args = (NvThreadArgs *)malloc(sizeof(NvThreadArgs));
    if (args == NULL)
        goto fail;
    memset(args, 0x0, sizeof(NvThreadArgs));

    args->function = function;
    args->thread = thrd;
    args->thread_args = tArgs;
    (void)pthread_mutex_init(&args->barrier, 0);

    /* The thread is created with the mutex lock held, to prevent
     * race conditions between thread assignment and
     * thread function execution */
    NvSemaphoreCreate(&args->init, 0);
    pthread_mutex_lock(&args->barrier);

    err = pthread_create(&thrd->thread, 0, thread_wrapper, args);
    if (err)
        goto fail;

    NvSemaphoreWait(args->init);

    *tHandle = thrd;
    pthread_mutex_unlock(&args->barrier);

    return 0;

fail:
    if (args)
    {
        pthread_mutex_unlock(&args->barrier);
        pthread_mutex_destroy(&args->barrier);
    }
    free(args);
    free(thrd);
    *tHandle = 0;

    return ENOMEM;
}

int32_t NvThreadSetName(NvThreadHandle t, const char *name)
{
    int32_t val = pthread_setname_np(t->thread, name);
    return val;
}

void NvThreadJoin(NvThreadHandle tHandle)
{
    if (!tHandle)
        return;

    int32_t val = pthread_join(tHandle->thread, 0);
    if (val)
        return;

    free(tHandle);
}

u_int64_t NvGetCurrentThreadId(void)
{
    return pthread_self();
}
