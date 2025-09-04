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

#ifndef _RPC_HAL_STUBS_H_
#define _RPC_HAL_STUBS_H_

// The file replaces g_rpc_hal.h to provide stubs for rpc HAL functions when 
// Rmconfig Module RPC is disabled, while the BASE_DEFINITION for RPC object 
// is not needed. Thus making it a noop.
#define __RPC_OBJECT_BASE_DEFINITION

static inline NV_STATUS rpcAllocShareDevice_HAL(OBJGPU *pGpu, ...)                        { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcAllocMemory_HAL(OBJGPU *pGpu, ...)                             { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcAllocCtxDma_HAL(OBJGPU *pGpu, ...)                             { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcAllocChannelDma_HAL(OBJGPU *pGpu, ...)                         { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcAllocObject_HAL(OBJGPU *pGpu, ...)                             { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcMapMemoryDma_HAL(OBJGPU *pGpu, ...)                            { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcUnmapMemoryDma_HAL(OBJGPU *pGpu, ...)                          { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcAllocSubdevice_HAL(OBJGPU *pGpu, ...)                          { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcDupObject_HAL(OBJGPU *pGpu, ...)                               { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcIdleChannels_HAL(OBJGPU *pGpu, ...)                            { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcAllocEvent_HAL(OBJGPU *pGpu, ...)                              { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcDmaControl_HAL(OBJGPU *pGpu, ...)                              { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcFree_HAL(OBJGPU *pGpu, ...)                                    { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcPerfGetLevelInfo_HAL(OBJGPU *pGpu, ...)                        { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcUnloadingGuestDriver_HAL(OBJGPU *pGpu, ...)                    { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcGpuExecRegOps_HAL(OBJGPU *pGpu, ...)                           { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcGetStaticInfo_HAL(OBJGPU *pGpu, ...)                           { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcGetStaticInfo2_HAL(OBJGPU *pGpu, ...)                          { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcUpdateBarPde_HAL(OBJGPU *pGpu, ...)                            { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcSetPageDirectory_HAL(OBJGPU *pGpu, ...)                        { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcUnsetPageDirectory_HAL(OBJGPU *pGpu, ...)                      { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcUpdateGpuPdes_HAL(OBJGPU *pGpu, ...)                           { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcVgpuPfRegRead32_HAL(OBJGPU *pGpu, ...)                         { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcGetGspStaticInfo_HAL(OBJGPU *pGpu, ...)                        { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcSetMemoryInfo_HAL(OBJGPU *pGpu, ...)                           { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcSetRegistry_HAL(OBJGPU *pGpu, ...)                             { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcGspInitPostObjgpu_HAL(OBJGPU *pGpu, ...)                       { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcDumpProtobufComponent_HAL(OBJGPU *pGpu, ...)                   { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcRmfsInit_HAL(OBJGPU *pGpu, ...)                                { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcRmfsCloseQueue_HAL(OBJGPU *pGpu, ...)                          { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcRmfsCleanup_HAL(OBJGPU *pGpu, ...)                             { return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS rpcRmfsTest_HAL(OBJGPU *pGpu, ...)                                { return NV_ERR_NOT_SUPPORTED; }

#endif // _RPC_HAL_STUBS_H_

