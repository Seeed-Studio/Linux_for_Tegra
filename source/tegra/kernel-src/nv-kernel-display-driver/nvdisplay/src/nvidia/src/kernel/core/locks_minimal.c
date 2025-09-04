/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2021 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "core/core.h"
#include "core/locks.h"
#include "os/os.h"
#include "gpu_mgr/gpu_mgr.h"

typedef struct
{
    PORT_SEMAPHORE     *pLock;     //<! Lock that threads actually block on
    volatile NvU32      waiting;   //<! Number of waiting threads, debug use only
    OS_THREAD_HANDLE    threadId;  //<! ID of thread owning the lock, ~0 if none
    NvU64               timestamp; //<! Timestamp of last lock acquire
    LOCK_TRACE_INFO     traceInfo; //<! Lock acquire/release trace info
    NvBool              bValid;    //<! If ready to acquire/release the lock 
} GPULOCK;

static GPULOCK rmGpuLock;

#define INVALID_THREAD_ID ((OS_THREAD_HANDLE)~0ull)

NV_STATUS rmGpuLockInfoInit(void)
{
    portMemSet(&rmGpuLock, 0, sizeof(rmGpuLock));
    rmGpuLock.threadId = INVALID_THREAD_ID;

    rmGpuLock.pLock = portSyncSemaphoreCreate(portMemAllocatorGetGlobalNonPaged(), 1);
    if (rmGpuLock.pLock == NULL)
        return NV_ERR_INSUFFICIENT_RESOURCES;

    NvU64 timestamp;
    OS_THREAD_HANDLE threadId;
    osGetCurrentThread(&threadId);
    osGetCurrentTick(&timestamp);
    rmGpuLock.bValid = NV_FALSE;

    INSERT_LOCK_TRACE(&rmGpuLock.traceInfo, NV_RETURN_ADDRESS(),
                      lockTraceAlloc, 0, 0, threadId,
                      0, 0, timestamp);

    return NV_OK;
}

void rmGpuLockInfoDestroy(void)
{
    NV_ASSERT(rmGpuLock.waiting == 0);
    NV_ASSERT(rmGpuLock.threadId == INVALID_THREAD_ID);

    if (rmGpuLock.pLock != NULL)
        portSyncSemaphoreDestroy(rmGpuLock.pLock);

    portMemSet(&rmGpuLock, 0, sizeof(rmGpuLock));
    rmGpuLock.threadId = INVALID_THREAD_ID;
}

NV_STATUS rmGpuLockAlloc(NvU32 gpuInst)
{
    NV_ASSERT_OR_RETURN(gpuInst == 0, NV_ERR_INVALID_ARGUMENT);
    NV_ASSERT_OR_RETURN(rmGpuLock.pLock != NULL, NV_ERR_INVALID_STATE);

    rmGpuLock.bValid = NV_TRUE;

    return NV_OK;
}

void rmGpuLockFree(NvU32 gpuInst)
{
    NV_ASSERT_OR_RETURN_VOID(gpuInst == 0);
    rmGpuLock.bValid = NV_FALSE;
}

static NV_STATUS _rmGpuLockAcquire(NvU32 flags, void *ra)
{
    NvBool bCondAcquire = !!(flags & GPUS_LOCK_FLAGS_COND_ACQUIRE);
    NvBool bHighIrql = (portSyncExSafeToSleep() == NV_FALSE);

    //
    // We may get a bValid as NV_FALSE before GPU is attached.
    //
    if (rmGpuLock.bValid == NV_FALSE)
    {
        return NV_OK;
    }

    NV_ASSERT_OR_RETURN(rmGpuLock.pLock != NULL, NV_ERR_INVALID_STATE);
    NV_ASSERT_OR_RETURN(!rmGpuLockIsOwner(), NV_ERR_CYCLE_DETECTED);
    if (bCondAcquire || bHighIrql)
    {
        NvBool success = portSyncSemaphoreAcquireConditional(rmGpuLock.pLock);
        if (!success)
            return NV_ERR_STATE_IN_USE;
    }
    else
    {
        portAtomicIncrementU32(&rmGpuLock.waiting);
        portSyncSemaphoreAcquire(rmGpuLock.pLock);
        portAtomicDecrementU32(&rmGpuLock.waiting);
    }

    osGetCurrentThread(&rmGpuLock.threadId);
    osGetCurrentTick(&rmGpuLock.timestamp);

    OBJGPU *pGpu = gpumgrGetSomeGpu();
    if (pGpu && osLockShouldToggleInterrupts(pGpu))
        osDisableInterrupts(pGpu, bCondAcquire || bHighIrql);

    INSERT_LOCK_TRACE(&rmGpuLock.traceInfo, ra,
                      lockTraceAcquire, 0, 0, rmGpuLock.threadId,
                      bHighIrql, 0, rmGpuLock.timestamp);

    return NV_OK;
}

static NV_STATUS _rmGpuLockRelease(void *ra)
{
    OS_THREAD_HANDLE threadId;
    NvU64 timestamp;

    //
    // We may get a bValid as NV_FALSE before GPU is attached.
    //
    if (rmGpuLock.bValid == NV_FALSE)
    {
        return NV_OK;
    }

    osGetCurrentThread(&threadId);
    osGetCurrentTick(&timestamp);

    NV_ASSERT_OR_RETURN(threadId == rmGpuLock.threadId, NV_ERR_INVALID_STATE);

    INSERT_LOCK_TRACE(&rmGpuLock.traceInfo, ra,
                      lockTraceRelease, 0, 0, rmGpuLock.threadId,
                      0, 0, timestamp);

    rmGpuLock.threadId = INVALID_THREAD_ID;

    OBJGPU *pGpu = gpumgrGetSomeGpu();
    if (pGpu && osLockShouldToggleInterrupts(pGpu))
        osEnableInterrupts(pGpu);

    portSyncSemaphoreRelease(rmGpuLock.pLock);

    return NV_OK;
}

NV_STATUS rmGpuLocksAcquire(NvU32 flags, NvU32 module)
{
    return _rmGpuLockAcquire(flags, NV_RETURN_ADDRESS());
}

NvU32 rmGpuLocksRelease(NvU32 flags, OBJGPU* pGpu)
{
    return _rmGpuLockRelease(NV_RETURN_ADDRESS());
}

void rmGpuLocksFreeze(GPU_MASK gpuMask)
{
    NV_ASSERT_OR_RETURN_VOID(!"Function not implemented");
}
void rmGpuLocksUnfreeze(GPU_MASK gpuMask)
{
    NV_ASSERT_OR_RETURN_VOID(!"Function not implemented");
}
NV_STATUS rmGpuLockHide(NvU32 gpuMask)
{
    NV_ASSERT_OR_RETURN(!"Function not implemented", NV_ERR_NOT_SUPPORTED);
}
void rmGpuLockShow(NvU32 gpuMask)
{
    NV_ASSERT_OR_RETURN_VOID(!"Function not implemented");
}

NvBool rmGpuLockIsOwner(void)
{
    OS_THREAD_HANDLE threadId;
    osGetCurrentThread(&threadId);
    return threadId == rmGpuLock.threadId;
}

NvU32 rmGpuLocksGetOwnedMask(void)
{
    return rmGpuLockIsOwner() ? 0x1 : 0x0;
}

NvBool rmGpuLockIsHidden(OBJGPU* pGpu)
{
    return NV_FALSE;
}

NV_STATUS rmGpuLockSetOwner(OS_THREAD_HANDLE threadId)
{
    NvBool toDpcRefresh   = (threadId == GPUS_LOCK_OWNER_PENDING_DPC_REFRESH);
    NvBool fromDpcRefresh = (rmGpuLock.threadId == GPUS_LOCK_OWNER_PENDING_DPC_REFRESH);
    NV_ASSERT_OR_RETURN(toDpcRefresh || fromDpcRefresh, NV_ERR_INVALID_STATE);

    rmGpuLock.threadId = threadId;
    return NV_OK;
}

NV_STATUS rmGpuGroupLockAcquire(NvU32 gpuInst, GPU_LOCK_GRP_ID grp, NvU32 flags, NvU32 mod, GPU_MASK* pGpuMask)
{
    NV_ASSERT_OR_RETURN(pGpuMask != NULL, NV_ERR_INVALID_ARGUMENT);
    *pGpuMask = ~0;
    return _rmGpuLockAcquire(flags, NV_RETURN_ADDRESS());
}

NV_STATUS rmGpuGroupLockRelease(GPU_MASK gpuMask, NvU32 flags)
{
    if (gpuMask == 0)
        return NV_OK;
    return _rmGpuLockRelease(NV_RETURN_ADDRESS());
}

NvBool rmGpuGroupLockIsOwner(NvU32 gpuInst, GPU_LOCK_GRP_ID grp, GPU_MASK* pMask)
{
    if (*pMask == 0 && grp == GPU_LOCK_GRP_MASK)
        return NV_TRUE;
    return rmGpuLockIsOwner();
}

NvBool rmDeviceGpuLockIsOwner(NvU32 gpuInst)
{
    return rmGpuLockIsOwner();
}

NV_STATUS rmDeviceGpuLockSetOwner(OBJGPU *pGpu, OS_THREAD_HANDLE threadId)
{
    return rmGpuLockSetOwner(threadId);
}

NV_STATUS rmDeviceGpuLocksAcquire(OBJGPU *pGpu, NvU32 flags, NvU32 module)
{
    return _rmGpuLockAcquire(flags, NV_RETURN_ADDRESS());
}

NvU32 rmDeviceGpuLocksRelease(OBJGPU *pGpu, NvU32 flags, OBJGPU *pDpcGpu)
{
    return _rmGpuLockRelease(NV_RETURN_ADDRESS());
}

NvU64 rmIntrMaskLockAcquire(OBJGPU* pGpu)
{
    NV_ASSERT_OR_RETURN(!"Function not implemented", 0);
}
void rmIntrMaskLockRelease(OBJGPU* pGpu, NvU64 oldIrql)
{
    NV_ASSERT_OR_RETURN_VOID(!"Function not implemented");
}

