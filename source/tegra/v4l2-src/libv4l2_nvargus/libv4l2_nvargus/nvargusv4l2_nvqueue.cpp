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

#include <string.h>
#include <errno.h>

#include "nvargusv4l2_nvqueue.h"

int32_t NvQueueCreate(NvQueueHandle *phQueue, u_int32_t maxEntries, u_int32_t entrySize)
{
    NvQueue *pQueue;
    int32_t retval = 0;

    if (maxEntries == 0)
        return EINVAL;

    pQueue = (NvQueue *)malloc(sizeof(NvQueue));
    if (!pQueue)
        return ENOMEM;

    memset(pQueue, 0x0, sizeof(NvQueue));
    pQueue->pEntryList = 0;
    pQueue->pushIndex = 0;
    pQueue->popIndex = 0;
    pQueue->maxEntries = maxEntries + 1;
    pQueue->entrySize = entrySize;

    retval = NvMutexCreate(&pQueue->mutexLock);
    if (retval != 0)
        goto nvqueue_exit;

    pQueue->pEntryList = (u_int8_t *)malloc(pQueue->maxEntries * entrySize);
    if (!pQueue->pEntryList)
    {
        retval = ENOMEM;
        goto nvqueue_exit;
    }

    *phQueue = pQueue;
    return 0;

nvqueue_exit:
    if (pQueue)
    {
        NvMutexDestroy(pQueue->mutexLock);
        free(pQueue);
    }

    *phQueue = 0;
    return retval;
}

int32_t NvQueueEnQ(NvQueueHandle hQueue, void *pElem)
{
    NvQueue *pQueue = hQueue;
    int32_t retval = 0;
    u_int32_t pushIdx, popIdx;

    NvMutexAcquire(pQueue->mutexLock);

    if (pElem == NULL)
    {
        retval = EINVAL;
        goto nvqueue_exit;
    }

    pushIdx = pQueue->pushIndex;
    popIdx = pQueue->popIndex;

    /* Check if space available */
    if (pushIdx + 1 == popIdx || pushIdx + 1 == popIdx + pQueue->maxEntries)
    {
        retval = ENOMEM;
        goto nvqueue_exit;
    }

    memcpy(&pQueue->pEntryList[pushIdx * pQueue->entrySize], pElem,
           pQueue->entrySize);

    if (++pushIdx >= pQueue->maxEntries)
        pushIdx = 0;
    pQueue->pushIndex = pushIdx;

nvqueue_exit:
    NvMutexRelease(pQueue->mutexLock);
    return retval;
}

int32_t NvQueueDeQ(NvQueueHandle hQueue, void *pElem)
{
    NvQueue *pQueue = hQueue;
    int32_t retval = 0;
    u_int32_t popIdx;

    NvMutexAcquire(pQueue->mutexLock);

    popIdx = pQueue->popIndex;
    if (pQueue->pushIndex == popIdx)
    {
        retval = EINVAL;
        goto nvqueue_exit;
    }

    memcpy(pElem, &pQueue->pEntryList[popIdx * pQueue->entrySize],
           pQueue->entrySize);

    if (++popIdx >= pQueue->maxEntries)
        popIdx = 0;
    pQueue->popIndex = popIdx;

nvqueue_exit:
    NvMutexRelease(pQueue->mutexLock);
    return retval;
}

int32_t NvQueuePeek(NvQueueHandle hQueue, void *pElem)
{
    NvQueue *pQueue = hQueue;
    int32_t retval = 0;
    u_int32_t popIdx;

    NvMutexAcquire(pQueue->mutexLock);

    popIdx = pQueue->popIndex;
    if (pQueue->pushIndex == popIdx)
    {
        retval = EINVAL;
        goto nvqueue_exit;
    }

    memcpy(pElem, &pQueue->pEntryList[popIdx * pQueue->entrySize],
           pQueue->entrySize);

nvqueue_exit:
    NvMutexRelease(pQueue->mutexLock);
    return retval;
}

int32_t NvQueuePeekEntry(NvQueueHandle hQueue, void *pElem, u_int32_t nEntry)
{
    NvQueue *pQueue = hQueue;
    int32_t err = 0;
    u_int32_t entry, pushIdx, popIdx, numEntries;

    NvMutexAcquire(pQueue->mutexLock);

    pushIdx = pQueue->pushIndex;
    popIdx = pQueue->popIndex;

    numEntries = (pushIdx >= popIdx) ? pushIdx - popIdx : pQueue->maxEntries - popIdx + pushIdx;

    if ((numEntries == 0) || (numEntries <= nEntry))
    {
        err = EINVAL;
        goto nvqueue_exit;
    }

    entry = popIdx + nEntry;
    if (entry >= pQueue->maxEntries)
        entry -= pQueue->maxEntries;

    memcpy(pElem, &pQueue->pEntryList[entry * pQueue->entrySize],
           pQueue->entrySize);

nvqueue_exit:
    NvMutexRelease(pQueue->mutexLock);
    return err;
}

void NvQueueDestroy(NvQueueHandle *phQueue)
{
    NvQueue *pQueue = *phQueue;
    if (!pQueue)
        return;
    NvMutexDestroy(pQueue->mutexLock);
    free(pQueue->pEntryList);
    free(pQueue);
    *phQueue = NULL;
}

u_int32_t NvQueueGetNumEntries(NvQueueHandle hQueue)
{
    NvQueue *pQueue = hQueue;
    u_int32_t pushIdx = pQueue->pushIndex;
    u_int32_t popIdx = pQueue->popIndex;
    u_int32_t numEntries = (pushIdx >= popIdx) ? pushIdx - popIdx : pQueue->maxEntries - popIdx + pushIdx;

    return numEntries;
}
