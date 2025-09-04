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
#ifndef __ARGUSV4L2_NVQUEUE_H__
#define __ARGUSV4L2_NVQUEUE_H__

#include <stdlib.h>
#include "nvargusv4l2_os.h"

typedef struct NvQueueRec
{
    NvMutexHandle mutexLock; /* mutex lock for queue */
    u_int32_t maxEntries;    /* maximum number of allowed entries */
    u_int32_t entrySize;     /* size of individual entry */
    u_int32_t pushIndex;     /* index of where to push entry */
    u_int32_t popIndex;      /* index of where to grab entry */
    u_int8_t *pEntryList;    /* pointer to beginning entry */
} NvQueue;

typedef struct NvQueueRec *NvQueueHandle;

int32_t NvQueueCreate(NvQueueHandle *phQueue, u_int32_t maxEntries, u_int32_t entrySize);

int32_t NvQueueEnQ(NvQueueHandle hQueue, void *pElem);

int32_t NvQueueDeQ(NvQueueHandle hQueue, void *pElem);

int32_t NvQueuePeek(NvQueueHandle hQueue, void *pElem);

int32_t NvQueuePeekEntry(NvQueueHandle hQueue, void *pElem, u_int32_t entry);

void NvQueueDestroy(NvQueueHandle *phQueue);

u_int32_t NvQueueGetNumEntries(NvQueueHandle hQueue);

#endif /* __ARGUSV4L2_NVQUEUE_H__ */
