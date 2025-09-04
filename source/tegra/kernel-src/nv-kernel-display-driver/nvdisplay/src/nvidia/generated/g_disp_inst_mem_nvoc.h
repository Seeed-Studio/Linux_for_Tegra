#ifndef _G_DISP_INST_MEM_NVOC_H_
#define _G_DISP_INST_MEM_NVOC_H_
#include "nvoc/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SPDX-FileCopyrightText: Copyright (c) 1993-2021 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "g_disp_inst_mem_nvoc.h"

#ifndef DISPLAY_INSTANCE_MEMORY_H
#define DISPLAY_INSTANCE_MEMORY_H

/* ------------------------ Includes --------------------------------------- */
#include "nvtypes.h"
#include "nvoc/utility.h"
#include "gpu/disp/kern_disp.h"
#include "gpu/mem_mgr/virt_mem_allocator_common.h"
#include "gpu/mem_mgr/mem_desc.h"

/* ------------------------ Forward Declaration ---------------------------- */
typedef struct OBJEHEAP OBJEHEAP;
struct DispChannel;

#ifndef __NVOC_CLASS_DispChannel_TYPEDEF__
#define __NVOC_CLASS_DispChannel_TYPEDEF__
typedef struct DispChannel DispChannel;
#endif /* __NVOC_CLASS_DispChannel_TYPEDEF__ */

#ifndef __nvoc_class_id_DispChannel
#define __nvoc_class_id_DispChannel 0xbd2ff3
#endif /* __nvoc_class_id_DispChannel */


struct ContextDma;

#ifndef __NVOC_CLASS_ContextDma_TYPEDEF__
#define __NVOC_CLASS_ContextDma_TYPEDEF__
typedef struct ContextDma ContextDma;
#endif /* __NVOC_CLASS_ContextDma_TYPEDEF__ */

#ifndef __nvoc_class_id_ContextDma
#define __nvoc_class_id_ContextDma 0x88441b
#endif /* __nvoc_class_id_ContextDma */



/* ------------------------ Macros & Defines ------------------------------- */
#define KERNEL_DISPLAY_GET_INST_MEM(p)    ((p)->pInst)
#define DISP_INST_MEM_ALIGN               0x10000

/* ------------------------ Types definitions ------------------------------ */
/*!
 * A software hash table entry
 */
typedef struct
{
    struct ContextDma *pContextDma;
    struct DispChannel *pDispChannel;
} SW_HASH_TABLE_ENTRY;

#ifdef NVOC_DISP_INST_MEM_H_PRIVATE_ACCESS_ALLOWED
#define PRIVATE_FIELD(x) x
#else
#define PRIVATE_FIELD(x) NVOC_PRIVATE_FIELD(x)
#endif
struct DisplayInstanceMemory {
    const struct NVOC_RTTI *__nvoc_rtti;
    struct Object __nvoc_base_Object;
    struct Object *__nvoc_pbase_Object;
    struct DisplayInstanceMemory *__nvoc_pbase_DisplayInstanceMemory;
    NV_ADDRESS_SPACE instMemAddrSpace;
    NvU32 instMemAttr;
    NvU64 instMemBase;
    NvU32 instMemSize;
    MEMORY_DESCRIPTOR *pAllocedInstMemDesc;
    MEMORY_DESCRIPTOR *pInstMemDesc;
    void *pInstMem;
    NvU32 nHashTableEntries;
    NvU32 hashTableBaseAddr;
    SW_HASH_TABLE_ENTRY *pHashTable;
    OBJEHEAP *pInstHeap;
};

#ifndef __NVOC_CLASS_DisplayInstanceMemory_TYPEDEF__
#define __NVOC_CLASS_DisplayInstanceMemory_TYPEDEF__
typedef struct DisplayInstanceMemory DisplayInstanceMemory;
#endif /* __NVOC_CLASS_DisplayInstanceMemory_TYPEDEF__ */

#ifndef __nvoc_class_id_DisplayInstanceMemory
#define __nvoc_class_id_DisplayInstanceMemory 0x8223e2
#endif /* __nvoc_class_id_DisplayInstanceMemory */

extern const struct NVOC_CLASS_DEF __nvoc_class_def_DisplayInstanceMemory;

#define __staticCast_DisplayInstanceMemory(pThis) \
    ((pThis)->__nvoc_pbase_DisplayInstanceMemory)

#ifdef __nvoc_disp_inst_mem_h_disabled
#define __dynamicCast_DisplayInstanceMemory(pThis) ((DisplayInstanceMemory*)NULL)
#else //__nvoc_disp_inst_mem_h_disabled
#define __dynamicCast_DisplayInstanceMemory(pThis) \
    ((DisplayInstanceMemory*)__nvoc_dynamicCast(staticCast((pThis), Dynamic), classInfo(DisplayInstanceMemory)))
#endif //__nvoc_disp_inst_mem_h_disabled


NV_STATUS __nvoc_objCreateDynamic_DisplayInstanceMemory(DisplayInstanceMemory**, Dynamic*, NvU32, va_list);

NV_STATUS __nvoc_objCreate_DisplayInstanceMemory(DisplayInstanceMemory**, Dynamic*, NvU32);
#define __objCreate_DisplayInstanceMemory(ppNewObj, pParent, createFlags) \
    __nvoc_objCreate_DisplayInstanceMemory((ppNewObj), staticCast((pParent), Dynamic), (createFlags))

void instmemGetSize_v03_00(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, NvU32 *pTotalInstMemSize, NvU32 *pHashTableSize);

#ifdef __nvoc_disp_inst_mem_h_disabled
static inline void instmemGetSize(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, NvU32 *pTotalInstMemSize, NvU32 *pHashTableSize) {
    NV_ASSERT_FAILED_PRECOMP("DisplayInstanceMemory was disabled!");
}
#else //__nvoc_disp_inst_mem_h_disabled
#define instmemGetSize(pGpu, pInstMem, pTotalInstMemSize, pHashTableSize) instmemGetSize_v03_00(pGpu, pInstMem, pTotalInstMemSize, pHashTableSize)
#endif //__nvoc_disp_inst_mem_h_disabled

#define instmemGetSize_HAL(pGpu, pInstMem, pTotalInstMemSize, pHashTableSize) instmemGetSize(pGpu, pInstMem, pTotalInstMemSize, pHashTableSize)

NvU32 instmemGetHashTableBaseAddr_v03_00(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem);

#ifdef __nvoc_disp_inst_mem_h_disabled
static inline NvU32 instmemGetHashTableBaseAddr(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem) {
    NV_ASSERT_FAILED_PRECOMP("DisplayInstanceMemory was disabled!");
    return 0;
}
#else //__nvoc_disp_inst_mem_h_disabled
#define instmemGetHashTableBaseAddr(pGpu, pInstMem) instmemGetHashTableBaseAddr_v03_00(pGpu, pInstMem)
#endif //__nvoc_disp_inst_mem_h_disabled

#define instmemGetHashTableBaseAddr_HAL(pGpu, pInstMem) instmemGetHashTableBaseAddr(pGpu, pInstMem)

NvBool instmemIsValid_v03_00(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, NvU32 offset);

#ifdef __nvoc_disp_inst_mem_h_disabled
static inline NvBool instmemIsValid(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, NvU32 offset) {
    NV_ASSERT_FAILED_PRECOMP("DisplayInstanceMemory was disabled!");
    return NV_FALSE;
}
#else //__nvoc_disp_inst_mem_h_disabled
#define instmemIsValid(pGpu, pInstMem, offset) instmemIsValid_v03_00(pGpu, pInstMem, offset)
#endif //__nvoc_disp_inst_mem_h_disabled

#define instmemIsValid_HAL(pGpu, pInstMem, offset) instmemIsValid(pGpu, pInstMem, offset)

NvU32 instmemGenerateHashTableData_v03_00(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, NvU32 hClient, NvU32 offset, NvU32 dispChannelNum);

#ifdef __nvoc_disp_inst_mem_h_disabled
static inline NvU32 instmemGenerateHashTableData(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, NvU32 hClient, NvU32 offset, NvU32 dispChannelNum) {
    NV_ASSERT_FAILED_PRECOMP("DisplayInstanceMemory was disabled!");
    return 0;
}
#else //__nvoc_disp_inst_mem_h_disabled
#define instmemGenerateHashTableData(pGpu, pInstMem, hClient, offset, dispChannelNum) instmemGenerateHashTableData_v03_00(pGpu, pInstMem, hClient, offset, dispChannelNum)
#endif //__nvoc_disp_inst_mem_h_disabled

#define instmemGenerateHashTableData_HAL(pGpu, pInstMem, hClient, offset, dispChannelNum) instmemGenerateHashTableData(pGpu, pInstMem, hClient, offset, dispChannelNum)

NV_STATUS instmemHashFunc_v03_00(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, NvHandle hClient, NvHandle hContextDma, NvU32 dispChannelNum, NvU32 *result);

#ifdef __nvoc_disp_inst_mem_h_disabled
static inline NV_STATUS instmemHashFunc(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, NvHandle hClient, NvHandle hContextDma, NvU32 dispChannelNum, NvU32 *result) {
    NV_ASSERT_FAILED_PRECOMP("DisplayInstanceMemory was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_disp_inst_mem_h_disabled
#define instmemHashFunc(pGpu, pInstMem, hClient, hContextDma, dispChannelNum, result) instmemHashFunc_v03_00(pGpu, pInstMem, hClient, hContextDma, dispChannelNum, result)
#endif //__nvoc_disp_inst_mem_h_disabled

#define instmemHashFunc_HAL(pGpu, pInstMem, hClient, hContextDma, dispChannelNum, result) instmemHashFunc(pGpu, pInstMem, hClient, hContextDma, dispChannelNum, result)

NV_STATUS instmemCommitContextDma_v03_00(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, struct ContextDma *pContextDma);

#ifdef __nvoc_disp_inst_mem_h_disabled
static inline NV_STATUS instmemCommitContextDma(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, struct ContextDma *pContextDma) {
    NV_ASSERT_FAILED_PRECOMP("DisplayInstanceMemory was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_disp_inst_mem_h_disabled
#define instmemCommitContextDma(pGpu, pInstMem, pContextDma) instmemCommitContextDma_v03_00(pGpu, pInstMem, pContextDma)
#endif //__nvoc_disp_inst_mem_h_disabled

#define instmemCommitContextDma_HAL(pGpu, pInstMem, pContextDma) instmemCommitContextDma(pGpu, pInstMem, pContextDma)

static inline void instmemDecommitContextDma_b3696a(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, struct ContextDma *pContextDma) {
    return;
}

#ifdef __nvoc_disp_inst_mem_h_disabled
static inline void instmemDecommitContextDma(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, struct ContextDma *pContextDma) {
    NV_ASSERT_FAILED_PRECOMP("DisplayInstanceMemory was disabled!");
}
#else //__nvoc_disp_inst_mem_h_disabled
#define instmemDecommitContextDma(pGpu, pInstMem, pContextDma) instmemDecommitContextDma_b3696a(pGpu, pInstMem, pContextDma)
#endif //__nvoc_disp_inst_mem_h_disabled

#define instmemDecommitContextDma_HAL(pGpu, pInstMem, pContextDma) instmemDecommitContextDma(pGpu, pInstMem, pContextDma)

NV_STATUS instmemUpdateContextDma_v03_00(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, struct ContextDma *pContextDma, NvU64 *pNewAddress, NvU64 *pNewLimit, NvHandle hMemory, NvU32 comprInfo);

#ifdef __nvoc_disp_inst_mem_h_disabled
static inline NV_STATUS instmemUpdateContextDma(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, struct ContextDma *pContextDma, NvU64 *pNewAddress, NvU64 *pNewLimit, NvHandle hMemory, NvU32 comprInfo) {
    NV_ASSERT_FAILED_PRECOMP("DisplayInstanceMemory was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_disp_inst_mem_h_disabled
#define instmemUpdateContextDma(pGpu, pInstMem, pContextDma, pNewAddress, pNewLimit, hMemory, comprInfo) instmemUpdateContextDma_v03_00(pGpu, pInstMem, pContextDma, pNewAddress, pNewLimit, hMemory, comprInfo)
#endif //__nvoc_disp_inst_mem_h_disabled

#define instmemUpdateContextDma_HAL(pGpu, pInstMem, pContextDma, pNewAddress, pNewLimit, hMemory, comprInfo) instmemUpdateContextDma(pGpu, pInstMem, pContextDma, pNewAddress, pNewLimit, hMemory, comprInfo)

NV_STATUS instmemConstruct_IMPL(struct DisplayInstanceMemory *arg_pInstMem);
#define __nvoc_instmemConstruct(arg_pInstMem) instmemConstruct_IMPL(arg_pInstMem)
void instmemDestruct_IMPL(struct DisplayInstanceMemory *pInstMem);
#define __nvoc_instmemDestruct(pInstMem) instmemDestruct_IMPL(pInstMem)
NV_STATUS instmemStateInitLocked_IMPL(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem);
#ifdef __nvoc_disp_inst_mem_h_disabled
static inline NV_STATUS instmemStateInitLocked(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem) {
    NV_ASSERT_FAILED_PRECOMP("DisplayInstanceMemory was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_disp_inst_mem_h_disabled
#define instmemStateInitLocked(pGpu, pInstMem) instmemStateInitLocked_IMPL(pGpu, pInstMem)
#endif //__nvoc_disp_inst_mem_h_disabled

void instmemStateDestroy_IMPL(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem);
#ifdef __nvoc_disp_inst_mem_h_disabled
static inline void instmemStateDestroy(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem) {
    NV_ASSERT_FAILED_PRECOMP("DisplayInstanceMemory was disabled!");
}
#else //__nvoc_disp_inst_mem_h_disabled
#define instmemStateDestroy(pGpu, pInstMem) instmemStateDestroy_IMPL(pGpu, pInstMem)
#endif //__nvoc_disp_inst_mem_h_disabled

NV_STATUS instmemStateLoad_IMPL(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, NvU32 flags);
#ifdef __nvoc_disp_inst_mem_h_disabled
static inline NV_STATUS instmemStateLoad(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, NvU32 flags) {
    NV_ASSERT_FAILED_PRECOMP("DisplayInstanceMemory was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_disp_inst_mem_h_disabled
#define instmemStateLoad(pGpu, pInstMem, flags) instmemStateLoad_IMPL(pGpu, pInstMem, flags)
#endif //__nvoc_disp_inst_mem_h_disabled

NV_STATUS instmemStateUnload_IMPL(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, NvU32 flags);
#ifdef __nvoc_disp_inst_mem_h_disabled
static inline NV_STATUS instmemStateUnload(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, NvU32 flags) {
    NV_ASSERT_FAILED_PRECOMP("DisplayInstanceMemory was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_disp_inst_mem_h_disabled
#define instmemStateUnload(pGpu, pInstMem, flags) instmemStateUnload_IMPL(pGpu, pInstMem, flags)
#endif //__nvoc_disp_inst_mem_h_disabled

void instmemSetMemory_IMPL(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, NV_ADDRESS_SPACE dispInstMemAddrSpace, NvU32 dispInstMemAttr, NvU64 dispInstMemBase, NvU32 dispInstMemSize);
#ifdef __nvoc_disp_inst_mem_h_disabled
static inline void instmemSetMemory(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, NV_ADDRESS_SPACE dispInstMemAddrSpace, NvU32 dispInstMemAttr, NvU64 dispInstMemBase, NvU32 dispInstMemSize) {
    NV_ASSERT_FAILED_PRECOMP("DisplayInstanceMemory was disabled!");
}
#else //__nvoc_disp_inst_mem_h_disabled
#define instmemSetMemory(pGpu, pInstMem, dispInstMemAddrSpace, dispInstMemAttr, dispInstMemBase, dispInstMemSize) instmemSetMemory_IMPL(pGpu, pInstMem, dispInstMemAddrSpace, dispInstMemAttr, dispInstMemBase, dispInstMemSize)
#endif //__nvoc_disp_inst_mem_h_disabled

NV_STATUS instmemBindContextDma_IMPL(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, struct ContextDma *pContextDma, struct DispChannel *pDispChannel);
#ifdef __nvoc_disp_inst_mem_h_disabled
static inline NV_STATUS instmemBindContextDma(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, struct ContextDma *pContextDma, struct DispChannel *pDispChannel) {
    NV_ASSERT_FAILED_PRECOMP("DisplayInstanceMemory was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_disp_inst_mem_h_disabled
#define instmemBindContextDma(pGpu, pInstMem, pContextDma, pDispChannel) instmemBindContextDma_IMPL(pGpu, pInstMem, pContextDma, pDispChannel)
#endif //__nvoc_disp_inst_mem_h_disabled

NV_STATUS instmemUnbindContextDma_IMPL(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, struct ContextDma *pContextDma, struct DispChannel *pDispChannel);
#ifdef __nvoc_disp_inst_mem_h_disabled
static inline NV_STATUS instmemUnbindContextDma(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, struct ContextDma *pContextDma, struct DispChannel *pDispChannel) {
    NV_ASSERT_FAILED_PRECOMP("DisplayInstanceMemory was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_disp_inst_mem_h_disabled
#define instmemUnbindContextDma(pGpu, pInstMem, pContextDma, pDispChannel) instmemUnbindContextDma_IMPL(pGpu, pInstMem, pContextDma, pDispChannel)
#endif //__nvoc_disp_inst_mem_h_disabled

void instmemUnbindContextDmaFromAllChannels_IMPL(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, struct ContextDma *pContextDma);
#ifdef __nvoc_disp_inst_mem_h_disabled
static inline void instmemUnbindContextDmaFromAllChannels(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, struct ContextDma *pContextDma) {
    NV_ASSERT_FAILED_PRECOMP("DisplayInstanceMemory was disabled!");
}
#else //__nvoc_disp_inst_mem_h_disabled
#define instmemUnbindContextDmaFromAllChannels(pGpu, pInstMem, pContextDma) instmemUnbindContextDmaFromAllChannels_IMPL(pGpu, pInstMem, pContextDma)
#endif //__nvoc_disp_inst_mem_h_disabled

void instmemUnbindDispChannelContextDmas_IMPL(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, struct DispChannel *pDispChannel);
#ifdef __nvoc_disp_inst_mem_h_disabled
static inline void instmemUnbindDispChannelContextDmas(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, struct DispChannel *pDispChannel) {
    NV_ASSERT_FAILED_PRECOMP("DisplayInstanceMemory was disabled!");
}
#else //__nvoc_disp_inst_mem_h_disabled
#define instmemUnbindDispChannelContextDmas(pGpu, pInstMem, pDispChannel) instmemUnbindDispChannelContextDmas_IMPL(pGpu, pInstMem, pDispChannel)
#endif //__nvoc_disp_inst_mem_h_disabled

NV_STATUS instmemReserveContextDma_IMPL(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, NvU32 *offset);
#ifdef __nvoc_disp_inst_mem_h_disabled
static inline NV_STATUS instmemReserveContextDma(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, NvU32 *offset) {
    NV_ASSERT_FAILED_PRECOMP("DisplayInstanceMemory was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_disp_inst_mem_h_disabled
#define instmemReserveContextDma(pGpu, pInstMem, offset) instmemReserveContextDma_IMPL(pGpu, pInstMem, offset)
#endif //__nvoc_disp_inst_mem_h_disabled

NV_STATUS instmemFreeContextDma_IMPL(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, NvU32 offset);
#ifdef __nvoc_disp_inst_mem_h_disabled
static inline NV_STATUS instmemFreeContextDma(OBJGPU *pGpu, struct DisplayInstanceMemory *pInstMem, NvU32 offset) {
    NV_ASSERT_FAILED_PRECOMP("DisplayInstanceMemory was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_disp_inst_mem_h_disabled
#define instmemFreeContextDma(pGpu, pInstMem, offset) instmemFreeContextDma_IMPL(pGpu, pInstMem, offset)
#endif //__nvoc_disp_inst_mem_h_disabled

#undef PRIVATE_FIELD


#endif // DISPLAY_INSTANCE_MEMORY_H

#ifdef __cplusplus
} // extern "C"
#endif
#endif // _G_DISP_INST_MEM_NVOC_H_
