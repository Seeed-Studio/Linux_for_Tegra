#ifndef _G_SYNCPOINT_MEM_NVOC_H_
#define _G_SYNCPOINT_MEM_NVOC_H_
#include "nvoc/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SPDX-FileCopyrightText: Copyright (c) 2020-2021 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "g_syncpoint_mem_nvoc.h"

#ifndef _SYNCPOINT_MEMORY_H_
#define _SYNCPOINT_MEMORY_H_

#include "mem_mgr/mem.h"

/*!
 * Bind memory allocated through os descriptor
 */
#ifdef NVOC_SYNCPOINT_MEM_H_PRIVATE_ACCESS_ALLOWED
#define PRIVATE_FIELD(x) x
#else
#define PRIVATE_FIELD(x) NVOC_PRIVATE_FIELD(x)
#endif
struct SyncpointMemory {
    const struct NVOC_RTTI *__nvoc_rtti;
    struct Memory __nvoc_base_Memory;
    struct Object *__nvoc_pbase_Object;
    struct RsResource *__nvoc_pbase_RsResource;
    struct RmResourceCommon *__nvoc_pbase_RmResourceCommon;
    struct RmResource *__nvoc_pbase_RmResource;
    struct Memory *__nvoc_pbase_Memory;
    struct SyncpointMemory *__nvoc_pbase_SyncpointMemory;
    NvBool (*__syncpointCanCopy__)(struct SyncpointMemory *);
    NV_STATUS (*__syncpointCheckMemInterUnmap__)(struct SyncpointMemory *, NvBool);
    NV_STATUS (*__syncpointControl__)(struct SyncpointMemory *, CALL_CONTEXT *, struct RS_RES_CONTROL_PARAMS_INTERNAL *);
    NV_STATUS (*__syncpointUnmap__)(struct SyncpointMemory *, CALL_CONTEXT *, RsCpuMapping *);
    NV_STATUS (*__syncpointGetMemInterMapParams__)(struct SyncpointMemory *, RMRES_MEM_INTER_MAP_PARAMS *);
    NV_STATUS (*__syncpointGetMemoryMappingDescriptor__)(struct SyncpointMemory *, MEMORY_DESCRIPTOR **);
    NV_STATUS (*__syncpointGetMapAddrSpace__)(struct SyncpointMemory *, CALL_CONTEXT *, NvU32, NV_ADDRESS_SPACE *);
    NvBool (*__syncpointShareCallback__)(struct SyncpointMemory *, struct RsClient *, struct RsResourceRef *, RS_SHARE_POLICY *);
    NV_STATUS (*__syncpointControlFilter__)(struct SyncpointMemory *, struct CALL_CONTEXT *, struct RS_RES_CONTROL_PARAMS_INTERNAL *);
    void (*__syncpointAddAdditionalDependants__)(struct RsClient *, struct SyncpointMemory *, RsResourceRef *);
    NvU32 (*__syncpointGetRefCount__)(struct SyncpointMemory *);
    NV_STATUS (*__syncpointMapTo__)(struct SyncpointMemory *, RS_RES_MAP_TO_PARAMS *);
    NV_STATUS (*__syncpointControl_Prologue__)(struct SyncpointMemory *, CALL_CONTEXT *, struct RS_RES_CONTROL_PARAMS_INTERNAL *);
    NV_STATUS (*__syncpointIsReady__)(struct SyncpointMemory *);
    NV_STATUS (*__syncpointCheckCopyPermissions__)(struct SyncpointMemory *, struct OBJGPU *, NvHandle);
    void (*__syncpointPreDestruct__)(struct SyncpointMemory *);
    NV_STATUS (*__syncpointUnmapFrom__)(struct SyncpointMemory *, RS_RES_UNMAP_FROM_PARAMS *);
    void (*__syncpointControl_Epilogue__)(struct SyncpointMemory *, CALL_CONTEXT *, struct RS_RES_CONTROL_PARAMS_INTERNAL *);
    NV_STATUS (*__syncpointControlLookup__)(struct SyncpointMemory *, struct RS_RES_CONTROL_PARAMS_INTERNAL *, const struct NVOC_EXPORTED_METHOD_DEF **);
    NV_STATUS (*__syncpointMap__)(struct SyncpointMemory *, CALL_CONTEXT *, struct RS_CPU_MAP_PARAMS *, RsCpuMapping *);
    NvBool (*__syncpointAccessCallback__)(struct SyncpointMemory *, struct RsClient *, void *, RsAccessRight);
};

#ifndef __NVOC_CLASS_SyncpointMemory_TYPEDEF__
#define __NVOC_CLASS_SyncpointMemory_TYPEDEF__
typedef struct SyncpointMemory SyncpointMemory;
#endif /* __NVOC_CLASS_SyncpointMemory_TYPEDEF__ */

#ifndef __nvoc_class_id_SyncpointMemory
#define __nvoc_class_id_SyncpointMemory 0x529def
#endif /* __nvoc_class_id_SyncpointMemory */

extern const struct NVOC_CLASS_DEF __nvoc_class_def_SyncpointMemory;

#define __staticCast_SyncpointMemory(pThis) \
    ((pThis)->__nvoc_pbase_SyncpointMemory)

#ifdef __nvoc_syncpoint_mem_h_disabled
#define __dynamicCast_SyncpointMemory(pThis) ((SyncpointMemory*)NULL)
#else //__nvoc_syncpoint_mem_h_disabled
#define __dynamicCast_SyncpointMemory(pThis) \
    ((SyncpointMemory*)__nvoc_dynamicCast(staticCast((pThis), Dynamic), classInfo(SyncpointMemory)))
#endif //__nvoc_syncpoint_mem_h_disabled


NV_STATUS __nvoc_objCreateDynamic_SyncpointMemory(SyncpointMemory**, Dynamic*, NvU32, va_list);

NV_STATUS __nvoc_objCreate_SyncpointMemory(SyncpointMemory**, Dynamic*, NvU32, CALL_CONTEXT * arg_pCallContext, struct RS_RES_ALLOC_PARAMS_INTERNAL * arg_pParams);
#define __objCreate_SyncpointMemory(ppNewObj, pParent, createFlags, arg_pCallContext, arg_pParams) \
    __nvoc_objCreate_SyncpointMemory((ppNewObj), staticCast((pParent), Dynamic), (createFlags), arg_pCallContext, arg_pParams)

#define syncpointCanCopy(pSyncpointMemory) syncpointCanCopy_DISPATCH(pSyncpointMemory)
#define syncpointCheckMemInterUnmap(pMemory, bSubdeviceHandleProvided) syncpointCheckMemInterUnmap_DISPATCH(pMemory, bSubdeviceHandleProvided)
#define syncpointControl(pMemory, pCallContext, pParams) syncpointControl_DISPATCH(pMemory, pCallContext, pParams)
#define syncpointUnmap(pMemory, pCallContext, pCpuMapping) syncpointUnmap_DISPATCH(pMemory, pCallContext, pCpuMapping)
#define syncpointGetMemInterMapParams(pMemory, pParams) syncpointGetMemInterMapParams_DISPATCH(pMemory, pParams)
#define syncpointGetMemoryMappingDescriptor(pMemory, ppMemDesc) syncpointGetMemoryMappingDescriptor_DISPATCH(pMemory, ppMemDesc)
#define syncpointGetMapAddrSpace(pMemory, pCallContext, mapFlags, pAddrSpace) syncpointGetMapAddrSpace_DISPATCH(pMemory, pCallContext, mapFlags, pAddrSpace)
#define syncpointShareCallback(pResource, pInvokingClient, pParentRef, pSharePolicy) syncpointShareCallback_DISPATCH(pResource, pInvokingClient, pParentRef, pSharePolicy)
#define syncpointControlFilter(pResource, pCallContext, pParams) syncpointControlFilter_DISPATCH(pResource, pCallContext, pParams)
#define syncpointAddAdditionalDependants(pClient, pResource, pReference) syncpointAddAdditionalDependants_DISPATCH(pClient, pResource, pReference)
#define syncpointGetRefCount(pResource) syncpointGetRefCount_DISPATCH(pResource)
#define syncpointMapTo(pResource, pParams) syncpointMapTo_DISPATCH(pResource, pParams)
#define syncpointControl_Prologue(pResource, pCallContext, pParams) syncpointControl_Prologue_DISPATCH(pResource, pCallContext, pParams)
#define syncpointIsReady(pMemory) syncpointIsReady_DISPATCH(pMemory)
#define syncpointCheckCopyPermissions(pMemory, pDstGpu, hDstClientNvBool) syncpointCheckCopyPermissions_DISPATCH(pMemory, pDstGpu, hDstClientNvBool)
#define syncpointPreDestruct(pResource) syncpointPreDestruct_DISPATCH(pResource)
#define syncpointUnmapFrom(pResource, pParams) syncpointUnmapFrom_DISPATCH(pResource, pParams)
#define syncpointControl_Epilogue(pResource, pCallContext, pParams) syncpointControl_Epilogue_DISPATCH(pResource, pCallContext, pParams)
#define syncpointControlLookup(pResource, pParams, ppEntry) syncpointControlLookup_DISPATCH(pResource, pParams, ppEntry)
#define syncpointMap(pMemory, pCallContext, pParams, pCpuMapping) syncpointMap_DISPATCH(pMemory, pCallContext, pParams, pCpuMapping)
#define syncpointAccessCallback(pResource, pInvokingClient, pAllocParams, accessRight) syncpointAccessCallback_DISPATCH(pResource, pInvokingClient, pAllocParams, accessRight)
NvBool syncpointCanCopy_IMPL(struct SyncpointMemory *pSyncpointMemory);

static inline NvBool syncpointCanCopy_DISPATCH(struct SyncpointMemory *pSyncpointMemory) {
    return pSyncpointMemory->__syncpointCanCopy__(pSyncpointMemory);
}

static inline NV_STATUS syncpointCheckMemInterUnmap_DISPATCH(struct SyncpointMemory *pMemory, NvBool bSubdeviceHandleProvided) {
    return pMemory->__syncpointCheckMemInterUnmap__(pMemory, bSubdeviceHandleProvided);
}

static inline NV_STATUS syncpointControl_DISPATCH(struct SyncpointMemory *pMemory, CALL_CONTEXT *pCallContext, struct RS_RES_CONTROL_PARAMS_INTERNAL *pParams) {
    return pMemory->__syncpointControl__(pMemory, pCallContext, pParams);
}

static inline NV_STATUS syncpointUnmap_DISPATCH(struct SyncpointMemory *pMemory, CALL_CONTEXT *pCallContext, RsCpuMapping *pCpuMapping) {
    return pMemory->__syncpointUnmap__(pMemory, pCallContext, pCpuMapping);
}

static inline NV_STATUS syncpointGetMemInterMapParams_DISPATCH(struct SyncpointMemory *pMemory, RMRES_MEM_INTER_MAP_PARAMS *pParams) {
    return pMemory->__syncpointGetMemInterMapParams__(pMemory, pParams);
}

static inline NV_STATUS syncpointGetMemoryMappingDescriptor_DISPATCH(struct SyncpointMemory *pMemory, MEMORY_DESCRIPTOR **ppMemDesc) {
    return pMemory->__syncpointGetMemoryMappingDescriptor__(pMemory, ppMemDesc);
}

static inline NV_STATUS syncpointGetMapAddrSpace_DISPATCH(struct SyncpointMemory *pMemory, CALL_CONTEXT *pCallContext, NvU32 mapFlags, NV_ADDRESS_SPACE *pAddrSpace) {
    return pMemory->__syncpointGetMapAddrSpace__(pMemory, pCallContext, mapFlags, pAddrSpace);
}

static inline NvBool syncpointShareCallback_DISPATCH(struct SyncpointMemory *pResource, struct RsClient *pInvokingClient, struct RsResourceRef *pParentRef, RS_SHARE_POLICY *pSharePolicy) {
    return pResource->__syncpointShareCallback__(pResource, pInvokingClient, pParentRef, pSharePolicy);
}

static inline NV_STATUS syncpointControlFilter_DISPATCH(struct SyncpointMemory *pResource, struct CALL_CONTEXT *pCallContext, struct RS_RES_CONTROL_PARAMS_INTERNAL *pParams) {
    return pResource->__syncpointControlFilter__(pResource, pCallContext, pParams);
}

static inline void syncpointAddAdditionalDependants_DISPATCH(struct RsClient *pClient, struct SyncpointMemory *pResource, RsResourceRef *pReference) {
    pResource->__syncpointAddAdditionalDependants__(pClient, pResource, pReference);
}

static inline NvU32 syncpointGetRefCount_DISPATCH(struct SyncpointMemory *pResource) {
    return pResource->__syncpointGetRefCount__(pResource);
}

static inline NV_STATUS syncpointMapTo_DISPATCH(struct SyncpointMemory *pResource, RS_RES_MAP_TO_PARAMS *pParams) {
    return pResource->__syncpointMapTo__(pResource, pParams);
}

static inline NV_STATUS syncpointControl_Prologue_DISPATCH(struct SyncpointMemory *pResource, CALL_CONTEXT *pCallContext, struct RS_RES_CONTROL_PARAMS_INTERNAL *pParams) {
    return pResource->__syncpointControl_Prologue__(pResource, pCallContext, pParams);
}

static inline NV_STATUS syncpointIsReady_DISPATCH(struct SyncpointMemory *pMemory) {
    return pMemory->__syncpointIsReady__(pMemory);
}

static inline NV_STATUS syncpointCheckCopyPermissions_DISPATCH(struct SyncpointMemory *pMemory, struct OBJGPU *pDstGpu, NvHandle hDstClientNvBool) {
    return pMemory->__syncpointCheckCopyPermissions__(pMemory, pDstGpu, hDstClientNvBool);
}

static inline void syncpointPreDestruct_DISPATCH(struct SyncpointMemory *pResource) {
    pResource->__syncpointPreDestruct__(pResource);
}

static inline NV_STATUS syncpointUnmapFrom_DISPATCH(struct SyncpointMemory *pResource, RS_RES_UNMAP_FROM_PARAMS *pParams) {
    return pResource->__syncpointUnmapFrom__(pResource, pParams);
}

static inline void syncpointControl_Epilogue_DISPATCH(struct SyncpointMemory *pResource, CALL_CONTEXT *pCallContext, struct RS_RES_CONTROL_PARAMS_INTERNAL *pParams) {
    pResource->__syncpointControl_Epilogue__(pResource, pCallContext, pParams);
}

static inline NV_STATUS syncpointControlLookup_DISPATCH(struct SyncpointMemory *pResource, struct RS_RES_CONTROL_PARAMS_INTERNAL *pParams, const struct NVOC_EXPORTED_METHOD_DEF **ppEntry) {
    return pResource->__syncpointControlLookup__(pResource, pParams, ppEntry);
}

static inline NV_STATUS syncpointMap_DISPATCH(struct SyncpointMemory *pMemory, CALL_CONTEXT *pCallContext, struct RS_CPU_MAP_PARAMS *pParams, RsCpuMapping *pCpuMapping) {
    return pMemory->__syncpointMap__(pMemory, pCallContext, pParams, pCpuMapping);
}

static inline NvBool syncpointAccessCallback_DISPATCH(struct SyncpointMemory *pResource, struct RsClient *pInvokingClient, void *pAllocParams, RsAccessRight accessRight) {
    return pResource->__syncpointAccessCallback__(pResource, pInvokingClient, pAllocParams, accessRight);
}

NV_STATUS syncpointConstruct_IMPL(struct SyncpointMemory *arg_pSyncpointMemory, CALL_CONTEXT *arg_pCallContext, struct RS_RES_ALLOC_PARAMS_INTERNAL *arg_pParams);
#define __nvoc_syncpointConstruct(arg_pSyncpointMemory, arg_pCallContext, arg_pParams) syncpointConstruct_IMPL(arg_pSyncpointMemory, arg_pCallContext, arg_pParams)
#undef PRIVATE_FIELD


#endif

#ifdef __cplusplus
} // extern "C"
#endif
#endif // _G_SYNCPOINT_MEM_NVOC_H_
