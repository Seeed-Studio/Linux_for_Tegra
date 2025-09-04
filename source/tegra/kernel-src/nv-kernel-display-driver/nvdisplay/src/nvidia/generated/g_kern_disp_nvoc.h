#ifndef _G_KERN_DISP_NVOC_H_
#define _G_KERN_DISP_NVOC_H_
#include "nvoc/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SPDX-FileCopyrightText: Copyright (c) 2020-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "g_kern_disp_nvoc.h"

#ifndef KERN_DISP_H
#define KERN_DISP_H

/******************************************************************************
*
*       Kernel Display module header
*       This file contains functions managing display on CPU RM
*
******************************************************************************/

#include "gpu/eng_state.h"
#include "gpu/gpu_halspec.h"
#include "gpu/disp/kern_disp_type.h"
#include "gpu/disp/kern_disp_max.h"
#include "gpu/mem_mgr/context_dma.h"
#include "gpu/disp/vblank_callback/vblank.h"

#include "ctrl/ctrl2080/ctrl2080internal.h"

typedef NV2080_CTRL_INTERNAL_DISPLAY_GET_STATIC_INFO_PARAMS KernelDisplayStaticInfo;

struct DispChannel;

#ifndef __NVOC_CLASS_DispChannel_TYPEDEF__
#define __NVOC_CLASS_DispChannel_TYPEDEF__
typedef struct DispChannel DispChannel;
#endif /* __NVOC_CLASS_DispChannel_TYPEDEF__ */

#ifndef __nvoc_class_id_DispChannel
#define __nvoc_class_id_DispChannel 0xbd2ff3
#endif /* __nvoc_class_id_DispChannel */


struct RgLineCallback;

#ifndef __NVOC_CLASS_RgLineCallback_TYPEDEF__
#define __NVOC_CLASS_RgLineCallback_TYPEDEF__
typedef struct RgLineCallback RgLineCallback;
#endif /* __NVOC_CLASS_RgLineCallback_TYPEDEF__ */

#ifndef __nvoc_class_id_RgLineCallback
#define __nvoc_class_id_RgLineCallback 0xa3ff1c
#endif /* __nvoc_class_id_RgLineCallback */



#define KDISP_GET_HEAD(pKernelDisplay, headID)    (RMCFG_MODULE_KERNEL_HEAD ? kdispGetHead(pKernelDisplay, headID) : NULL)

/*!
 * KernelDisp is a logical abstraction of the GPU Display Engine. The
 * Public API of the Display Engine is exposed through this object, and any
 * interfaces which do not manage the underlying Display hardware can be
 * managed by this object.
 */
#ifdef NVOC_KERN_DISP_H_PRIVATE_ACCESS_ALLOWED
#define PRIVATE_FIELD(x) x
#else
#define PRIVATE_FIELD(x) NVOC_PRIVATE_FIELD(x)
#endif
struct KernelDisplay {
    const struct NVOC_RTTI *__nvoc_rtti;
    struct OBJENGSTATE __nvoc_base_OBJENGSTATE;
    struct Object *__nvoc_pbase_Object;
    struct OBJENGSTATE *__nvoc_pbase_OBJENGSTATE;
    struct KernelDisplay *__nvoc_pbase_KernelDisplay;
    NV_STATUS (*__kdispConstructEngine__)(struct OBJGPU *, struct KernelDisplay *, ENGDESCRIPTOR);
    NV_STATUS (*__kdispStatePreInitLocked__)(struct OBJGPU *, struct KernelDisplay *);
    NV_STATUS (*__kdispStateInitLocked__)(struct OBJGPU *, struct KernelDisplay *);
    void (*__kdispStateDestroy__)(struct OBJGPU *, struct KernelDisplay *);
    NV_STATUS (*__kdispStateLoad__)(struct OBJGPU *, struct KernelDisplay *, NvU32);
    NV_STATUS (*__kdispStateUnload__)(struct OBJGPU *, struct KernelDisplay *, NvU32);
    NV_STATUS (*__kdispReconcileTunableState__)(POBJGPU, struct KernelDisplay *, void *);
    NV_STATUS (*__kdispStatePreLoad__)(POBJGPU, struct KernelDisplay *, NvU32);
    NV_STATUS (*__kdispStatePostUnload__)(POBJGPU, struct KernelDisplay *, NvU32);
    NV_STATUS (*__kdispStatePreUnload__)(POBJGPU, struct KernelDisplay *, NvU32);
    NV_STATUS (*__kdispStateInitUnlocked__)(POBJGPU, struct KernelDisplay *);
    void (*__kdispInitMissing__)(POBJGPU, struct KernelDisplay *);
    NV_STATUS (*__kdispStatePreInitUnlocked__)(POBJGPU, struct KernelDisplay *);
    NV_STATUS (*__kdispGetTunableState__)(POBJGPU, struct KernelDisplay *, void *);
    NV_STATUS (*__kdispCompareTunableState__)(POBJGPU, struct KernelDisplay *, void *, void *);
    void (*__kdispFreeTunableState__)(POBJGPU, struct KernelDisplay *, void *);
    NV_STATUS (*__kdispStatePostLoad__)(POBJGPU, struct KernelDisplay *, NvU32);
    NV_STATUS (*__kdispAllocTunableState__)(POBJGPU, struct KernelDisplay *, void **);
    NV_STATUS (*__kdispSetTunableState__)(POBJGPU, struct KernelDisplay *, void *);
    NvBool (*__kdispIsPresent__)(POBJGPU, struct KernelDisplay *);
    NvBool PDB_PROP_KDISP_IMP_ENABLE;
    struct DisplayInstanceMemory *pInst;
    struct KernelHead *pKernelHead[4];
    const KernelDisplayStaticInfo *pStaticInfo;
    NvBool bWarPurgeSatellitesOnCoreFree;
    struct RgLineCallback *rgLineCallbackPerHead[4][2];
    NvU32 isrVblankHeads;
};

#ifndef __NVOC_CLASS_KernelDisplay_TYPEDEF__
#define __NVOC_CLASS_KernelDisplay_TYPEDEF__
typedef struct KernelDisplay KernelDisplay;
#endif /* __NVOC_CLASS_KernelDisplay_TYPEDEF__ */

#ifndef __nvoc_class_id_KernelDisplay
#define __nvoc_class_id_KernelDisplay 0x55952e
#endif /* __nvoc_class_id_KernelDisplay */

extern const struct NVOC_CLASS_DEF __nvoc_class_def_KernelDisplay;

#define __staticCast_KernelDisplay(pThis) \
    ((pThis)->__nvoc_pbase_KernelDisplay)

#ifdef __nvoc_kern_disp_h_disabled
#define __dynamicCast_KernelDisplay(pThis) ((KernelDisplay*)NULL)
#else //__nvoc_kern_disp_h_disabled
#define __dynamicCast_KernelDisplay(pThis) \
    ((KernelDisplay*)__nvoc_dynamicCast(staticCast((pThis), Dynamic), classInfo(KernelDisplay)))
#endif //__nvoc_kern_disp_h_disabled

#define PDB_PROP_KDISP_IS_MISSING_BASE_CAST __nvoc_base_OBJENGSTATE.
#define PDB_PROP_KDISP_IS_MISSING_BASE_NAME PDB_PROP_ENGSTATE_IS_MISSING
#define PDB_PROP_KDISP_IMP_ENABLE_BASE_CAST
#define PDB_PROP_KDISP_IMP_ENABLE_BASE_NAME PDB_PROP_KDISP_IMP_ENABLE

NV_STATUS __nvoc_objCreateDynamic_KernelDisplay(KernelDisplay**, Dynamic*, NvU32, va_list);

NV_STATUS __nvoc_objCreate_KernelDisplay(KernelDisplay**, Dynamic*, NvU32);
#define __objCreate_KernelDisplay(ppNewObj, pParent, createFlags) \
    __nvoc_objCreate_KernelDisplay((ppNewObj), staticCast((pParent), Dynamic), (createFlags))

#define kdispConstructEngine(pGpu, pKernelDisplay, engDesc) kdispConstructEngine_DISPATCH(pGpu, pKernelDisplay, engDesc)
#define kdispStatePreInitLocked(pGpu, pKernelDisplay) kdispStatePreInitLocked_DISPATCH(pGpu, pKernelDisplay)
#define kdispStateInitLocked(pGpu, pKernelDisplay) kdispStateInitLocked_DISPATCH(pGpu, pKernelDisplay)
#define kdispStateDestroy(pGpu, pKernelDisplay) kdispStateDestroy_DISPATCH(pGpu, pKernelDisplay)
#define kdispStateLoad(pGpu, pKernelDisplay, flags) kdispStateLoad_DISPATCH(pGpu, pKernelDisplay, flags)
#define kdispStateUnload(pGpu, pKernelDisplay, flags) kdispStateUnload_DISPATCH(pGpu, pKernelDisplay, flags)
#define kdispReconcileTunableState(pGpu, pEngstate, pTunableState) kdispReconcileTunableState_DISPATCH(pGpu, pEngstate, pTunableState)
#define kdispStatePreLoad(pGpu, pEngstate, arg0) kdispStatePreLoad_DISPATCH(pGpu, pEngstate, arg0)
#define kdispStatePostUnload(pGpu, pEngstate, arg0) kdispStatePostUnload_DISPATCH(pGpu, pEngstate, arg0)
#define kdispStatePreUnload(pGpu, pEngstate, arg0) kdispStatePreUnload_DISPATCH(pGpu, pEngstate, arg0)
#define kdispStateInitUnlocked(pGpu, pEngstate) kdispStateInitUnlocked_DISPATCH(pGpu, pEngstate)
#define kdispInitMissing(pGpu, pEngstate) kdispInitMissing_DISPATCH(pGpu, pEngstate)
#define kdispStatePreInitUnlocked(pGpu, pEngstate) kdispStatePreInitUnlocked_DISPATCH(pGpu, pEngstate)
#define kdispGetTunableState(pGpu, pEngstate, pTunableState) kdispGetTunableState_DISPATCH(pGpu, pEngstate, pTunableState)
#define kdispCompareTunableState(pGpu, pEngstate, pTunables1, pTunables2) kdispCompareTunableState_DISPATCH(pGpu, pEngstate, pTunables1, pTunables2)
#define kdispFreeTunableState(pGpu, pEngstate, pTunableState) kdispFreeTunableState_DISPATCH(pGpu, pEngstate, pTunableState)
#define kdispStatePostLoad(pGpu, pEngstate, arg0) kdispStatePostLoad_DISPATCH(pGpu, pEngstate, arg0)
#define kdispAllocTunableState(pGpu, pEngstate, ppTunableState) kdispAllocTunableState_DISPATCH(pGpu, pEngstate, ppTunableState)
#define kdispSetTunableState(pGpu, pEngstate, pTunableState) kdispSetTunableState_DISPATCH(pGpu, pEngstate, pTunableState)
#define kdispIsPresent(pGpu, pEngstate) kdispIsPresent_DISPATCH(pGpu, pEngstate)
NV_STATUS kdispConstructInstMem_IMPL(struct KernelDisplay *pKernelDisplay);

#ifdef __nvoc_kern_disp_h_disabled
static inline NV_STATUS kdispConstructInstMem(struct KernelDisplay *pKernelDisplay) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_kern_disp_h_disabled
#define kdispConstructInstMem(pKernelDisplay) kdispConstructInstMem_IMPL(pKernelDisplay)
#endif //__nvoc_kern_disp_h_disabled

#define kdispConstructInstMem_HAL(pKernelDisplay) kdispConstructInstMem(pKernelDisplay)

void kdispDestructInstMem_IMPL(struct KernelDisplay *pKernelDisplay);

#ifdef __nvoc_kern_disp_h_disabled
static inline void kdispDestructInstMem(struct KernelDisplay *pKernelDisplay) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
}
#else //__nvoc_kern_disp_h_disabled
#define kdispDestructInstMem(pKernelDisplay) kdispDestructInstMem_IMPL(pKernelDisplay)
#endif //__nvoc_kern_disp_h_disabled

#define kdispDestructInstMem_HAL(pKernelDisplay) kdispDestructInstMem(pKernelDisplay)

NV_STATUS kdispSelectClass_v03_00_KERNEL(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, NvU32 swClass);

#ifdef __nvoc_kern_disp_h_disabled
static inline NV_STATUS kdispSelectClass(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, NvU32 swClass) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_kern_disp_h_disabled
#define kdispSelectClass(pGpu, pKernelDisplay, swClass) kdispSelectClass_v03_00_KERNEL(pGpu, pKernelDisplay, swClass)
#endif //__nvoc_kern_disp_h_disabled

#define kdispSelectClass_HAL(pGpu, pKernelDisplay, swClass) kdispSelectClass(pGpu, pKernelDisplay, swClass)

NvS32 kdispGetBaseOffset_v04_02(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay);

#ifdef __nvoc_kern_disp_h_disabled
static inline NvS32 kdispGetBaseOffset(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
    return 0;
}
#else //__nvoc_kern_disp_h_disabled
#define kdispGetBaseOffset(pGpu, pKernelDisplay) kdispGetBaseOffset_v04_02(pGpu, pKernelDisplay)
#endif //__nvoc_kern_disp_h_disabled

#define kdispGetBaseOffset_HAL(pGpu, pKernelDisplay) kdispGetBaseOffset(pGpu, pKernelDisplay)

NV_STATUS kdispGetChannelNum_v03_00(struct KernelDisplay *pKernelDisplay, DISPCHNCLASS channelClass, NvU32 channelInstance, NvU32 *pChannelNum);

#ifdef __nvoc_kern_disp_h_disabled
static inline NV_STATUS kdispGetChannelNum(struct KernelDisplay *pKernelDisplay, DISPCHNCLASS channelClass, NvU32 channelInstance, NvU32 *pChannelNum) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_kern_disp_h_disabled
#define kdispGetChannelNum(pKernelDisplay, channelClass, channelInstance, pChannelNum) kdispGetChannelNum_v03_00(pKernelDisplay, channelClass, channelInstance, pChannelNum)
#endif //__nvoc_kern_disp_h_disabled

#define kdispGetChannelNum_HAL(pKernelDisplay, channelClass, channelInstance, pChannelNum) kdispGetChannelNum(pKernelDisplay, channelClass, channelInstance, pChannelNum)

void kdispGetDisplayCapsBaseAndSize_v03_00(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, NvU32 *pOffset, NvU32 *pSize);

#ifdef __nvoc_kern_disp_h_disabled
static inline void kdispGetDisplayCapsBaseAndSize(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, NvU32 *pOffset, NvU32 *pSize) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
}
#else //__nvoc_kern_disp_h_disabled
#define kdispGetDisplayCapsBaseAndSize(pGpu, pKernelDisplay, pOffset, pSize) kdispGetDisplayCapsBaseAndSize_v03_00(pGpu, pKernelDisplay, pOffset, pSize)
#endif //__nvoc_kern_disp_h_disabled

#define kdispGetDisplayCapsBaseAndSize_HAL(pGpu, pKernelDisplay, pOffset, pSize) kdispGetDisplayCapsBaseAndSize(pGpu, pKernelDisplay, pOffset, pSize)

void kdispGetDisplaySfUserBaseAndSize_v03_00(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, NvU32 *pOffset, NvU32 *pSize);

#ifdef __nvoc_kern_disp_h_disabled
static inline void kdispGetDisplaySfUserBaseAndSize(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, NvU32 *pOffset, NvU32 *pSize) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
}
#else //__nvoc_kern_disp_h_disabled
#define kdispGetDisplaySfUserBaseAndSize(pGpu, pKernelDisplay, pOffset, pSize) kdispGetDisplaySfUserBaseAndSize_v03_00(pGpu, pKernelDisplay, pOffset, pSize)
#endif //__nvoc_kern_disp_h_disabled

#define kdispGetDisplaySfUserBaseAndSize_HAL(pGpu, pKernelDisplay, pOffset, pSize) kdispGetDisplaySfUserBaseAndSize(pGpu, pKernelDisplay, pOffset, pSize)

NV_STATUS kdispGetDisplayChannelUserBaseAndSize_v03_00(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, DISPCHNCLASS channelClass, NvU32 channelInstance, NvU32 *pOffset, NvU32 *pSize);

#ifdef __nvoc_kern_disp_h_disabled
static inline NV_STATUS kdispGetDisplayChannelUserBaseAndSize(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, DISPCHNCLASS channelClass, NvU32 channelInstance, NvU32 *pOffset, NvU32 *pSize) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_kern_disp_h_disabled
#define kdispGetDisplayChannelUserBaseAndSize(pGpu, pKernelDisplay, channelClass, channelInstance, pOffset, pSize) kdispGetDisplayChannelUserBaseAndSize_v03_00(pGpu, pKernelDisplay, channelClass, channelInstance, pOffset, pSize)
#endif //__nvoc_kern_disp_h_disabled

#define kdispGetDisplayChannelUserBaseAndSize_HAL(pGpu, pKernelDisplay, channelClass, channelInstance, pOffset, pSize) kdispGetDisplayChannelUserBaseAndSize(pGpu, pKernelDisplay, channelClass, channelInstance, pOffset, pSize)

NV_STATUS kdispImportImpData_IMPL(struct KernelDisplay *pKernelDisplay);

#ifdef __nvoc_kern_disp_h_disabled
static inline NV_STATUS kdispImportImpData(struct KernelDisplay *pKernelDisplay) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_kern_disp_h_disabled
#define kdispImportImpData(pKernelDisplay) kdispImportImpData_IMPL(pKernelDisplay)
#endif //__nvoc_kern_disp_h_disabled

#define kdispImportImpData_HAL(pKernelDisplay) kdispImportImpData(pKernelDisplay)

NV_STATUS kdispArbAndAllocDisplayBandwidth_v04_02(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, enum DISPLAY_ICC_BW_CLIENT iccBwClient, NvU32 minRequiredIsoBandwidthKBPS, NvU32 minRequiredFloorBandwidthKBPS);

#ifdef __nvoc_kern_disp_h_disabled
static inline NV_STATUS kdispArbAndAllocDisplayBandwidth(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, enum DISPLAY_ICC_BW_CLIENT iccBwClient, NvU32 minRequiredIsoBandwidthKBPS, NvU32 minRequiredFloorBandwidthKBPS) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_kern_disp_h_disabled
#define kdispArbAndAllocDisplayBandwidth(pGpu, pKernelDisplay, iccBwClient, minRequiredIsoBandwidthKBPS, minRequiredFloorBandwidthKBPS) kdispArbAndAllocDisplayBandwidth_v04_02(pGpu, pKernelDisplay, iccBwClient, minRequiredIsoBandwidthKBPS, minRequiredFloorBandwidthKBPS)
#endif //__nvoc_kern_disp_h_disabled

#define kdispArbAndAllocDisplayBandwidth_HAL(pGpu, pKernelDisplay, iccBwClient, minRequiredIsoBandwidthKBPS, minRequiredFloorBandwidthKBPS) kdispArbAndAllocDisplayBandwidth(pGpu, pKernelDisplay, iccBwClient, minRequiredIsoBandwidthKBPS, minRequiredFloorBandwidthKBPS)

NV_STATUS kdispSetPushBufferParamsToPhysical_IMPL(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, struct DispChannel *pDispChannel, NvHandle hObjectBuffer, struct ContextDma *pBufferContextDma, NvU32 hClass, NvU32 channelInstance, DISPCHNCLASS internalDispChnClass);

#ifdef __nvoc_kern_disp_h_disabled
static inline NV_STATUS kdispSetPushBufferParamsToPhysical(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, struct DispChannel *pDispChannel, NvHandle hObjectBuffer, struct ContextDma *pBufferContextDma, NvU32 hClass, NvU32 channelInstance, DISPCHNCLASS internalDispChnClass) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_kern_disp_h_disabled
#define kdispSetPushBufferParamsToPhysical(pGpu, pKernelDisplay, pDispChannel, hObjectBuffer, pBufferContextDma, hClass, channelInstance, internalDispChnClass) kdispSetPushBufferParamsToPhysical_IMPL(pGpu, pKernelDisplay, pDispChannel, hObjectBuffer, pBufferContextDma, hClass, channelInstance, internalDispChnClass)
#endif //__nvoc_kern_disp_h_disabled

#define kdispSetPushBufferParamsToPhysical_HAL(pGpu, pKernelDisplay, pDispChannel, hObjectBuffer, pBufferContextDma, hClass, channelInstance, internalDispChnClass) kdispSetPushBufferParamsToPhysical(pGpu, pKernelDisplay, pDispChannel, hObjectBuffer, pBufferContextDma, hClass, channelInstance, internalDispChnClass)

static inline NV_STATUS kdispAcquireDispChannelHw_56cd7a(struct KernelDisplay *pKernelDisplay, struct DispChannel *pDispChannel, NvU32 channelInstance, NvHandle hObjectBuffer, NvU32 initialGetPutOffset, NvBool allowGrabWithinSameClient, NvBool connectPbAtGrab) {
    return NV_OK;
}

#ifdef __nvoc_kern_disp_h_disabled
static inline NV_STATUS kdispAcquireDispChannelHw(struct KernelDisplay *pKernelDisplay, struct DispChannel *pDispChannel, NvU32 channelInstance, NvHandle hObjectBuffer, NvU32 initialGetPutOffset, NvBool allowGrabWithinSameClient, NvBool connectPbAtGrab) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_kern_disp_h_disabled
#define kdispAcquireDispChannelHw(pKernelDisplay, pDispChannel, channelInstance, hObjectBuffer, initialGetPutOffset, allowGrabWithinSameClient, connectPbAtGrab) kdispAcquireDispChannelHw_56cd7a(pKernelDisplay, pDispChannel, channelInstance, hObjectBuffer, initialGetPutOffset, allowGrabWithinSameClient, connectPbAtGrab)
#endif //__nvoc_kern_disp_h_disabled

#define kdispAcquireDispChannelHw_HAL(pKernelDisplay, pDispChannel, channelInstance, hObjectBuffer, initialGetPutOffset, allowGrabWithinSameClient, connectPbAtGrab) kdispAcquireDispChannelHw(pKernelDisplay, pDispChannel, channelInstance, hObjectBuffer, initialGetPutOffset, allowGrabWithinSameClient, connectPbAtGrab)

static inline NV_STATUS kdispReleaseDispChannelHw_56cd7a(struct KernelDisplay *pKernelDisplay, struct DispChannel *pDispChannel) {
    return NV_OK;
}

#ifdef __nvoc_kern_disp_h_disabled
static inline NV_STATUS kdispReleaseDispChannelHw(struct KernelDisplay *pKernelDisplay, struct DispChannel *pDispChannel) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_kern_disp_h_disabled
#define kdispReleaseDispChannelHw(pKernelDisplay, pDispChannel) kdispReleaseDispChannelHw_56cd7a(pKernelDisplay, pDispChannel)
#endif //__nvoc_kern_disp_h_disabled

#define kdispReleaseDispChannelHw_HAL(pKernelDisplay, pDispChannel) kdispReleaseDispChannelHw(pKernelDisplay, pDispChannel)

NV_STATUS kdispMapDispChannel_IMPL(struct KernelDisplay *pKernelDisplay, struct DispChannel *pDispChannel);

#ifdef __nvoc_kern_disp_h_disabled
static inline NV_STATUS kdispMapDispChannel(struct KernelDisplay *pKernelDisplay, struct DispChannel *pDispChannel) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_kern_disp_h_disabled
#define kdispMapDispChannel(pKernelDisplay, pDispChannel) kdispMapDispChannel_IMPL(pKernelDisplay, pDispChannel)
#endif //__nvoc_kern_disp_h_disabled

#define kdispMapDispChannel_HAL(pKernelDisplay, pDispChannel) kdispMapDispChannel(pKernelDisplay, pDispChannel)

void kdispUnbindUnmapDispChannel_IMPL(struct KernelDisplay *pKernelDisplay, struct DispChannel *pDispChannel);

#ifdef __nvoc_kern_disp_h_disabled
static inline void kdispUnbindUnmapDispChannel(struct KernelDisplay *pKernelDisplay, struct DispChannel *pDispChannel) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
}
#else //__nvoc_kern_disp_h_disabled
#define kdispUnbindUnmapDispChannel(pKernelDisplay, pDispChannel) kdispUnbindUnmapDispChannel_IMPL(pKernelDisplay, pDispChannel)
#endif //__nvoc_kern_disp_h_disabled

#define kdispUnbindUnmapDispChannel_HAL(pKernelDisplay, pDispChannel) kdispUnbindUnmapDispChannel(pKernelDisplay, pDispChannel)

NV_STATUS kdispRegisterRgLineCallback_IMPL(struct KernelDisplay *pKernelDisplay, struct RgLineCallback *pRgLineCallback, NvU32 head, NvU32 rgIntrLine, NvBool bEnable);

#ifdef __nvoc_kern_disp_h_disabled
static inline NV_STATUS kdispRegisterRgLineCallback(struct KernelDisplay *pKernelDisplay, struct RgLineCallback *pRgLineCallback, NvU32 head, NvU32 rgIntrLine, NvBool bEnable) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_kern_disp_h_disabled
#define kdispRegisterRgLineCallback(pKernelDisplay, pRgLineCallback, head, rgIntrLine, bEnable) kdispRegisterRgLineCallback_IMPL(pKernelDisplay, pRgLineCallback, head, rgIntrLine, bEnable)
#endif //__nvoc_kern_disp_h_disabled

#define kdispRegisterRgLineCallback_HAL(pKernelDisplay, pRgLineCallback, head, rgIntrLine, bEnable) kdispRegisterRgLineCallback(pKernelDisplay, pRgLineCallback, head, rgIntrLine, bEnable)

void kdispInvokeRgLineCallback_KERNEL(struct KernelDisplay *pKernelDisplay, NvU32 head, NvU32 rgIntrLine, NvBool bIsIrqlIsr);

#ifdef __nvoc_kern_disp_h_disabled
static inline void kdispInvokeRgLineCallback(struct KernelDisplay *pKernelDisplay, NvU32 head, NvU32 rgIntrLine, NvBool bIsIrqlIsr) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
}
#else //__nvoc_kern_disp_h_disabled
#define kdispInvokeRgLineCallback(pKernelDisplay, head, rgIntrLine, bIsIrqlIsr) kdispInvokeRgLineCallback_KERNEL(pKernelDisplay, head, rgIntrLine, bIsIrqlIsr)
#endif //__nvoc_kern_disp_h_disabled

#define kdispInvokeRgLineCallback_HAL(pKernelDisplay, head, rgIntrLine, bIsIrqlIsr) kdispInvokeRgLineCallback(pKernelDisplay, head, rgIntrLine, bIsIrqlIsr)

void kdispServiceVblank_KERNEL(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, NvU32 arg0, NvU32 arg1, THREAD_STATE_NODE *arg2);

#ifdef __nvoc_kern_disp_h_disabled
static inline void kdispServiceVblank(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, NvU32 arg0, NvU32 arg1, THREAD_STATE_NODE *arg2) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
}
#else //__nvoc_kern_disp_h_disabled
#define kdispServiceVblank(pGpu, pKernelDisplay, arg0, arg1, arg2) kdispServiceVblank_KERNEL(pGpu, pKernelDisplay, arg0, arg1, arg2)
#endif //__nvoc_kern_disp_h_disabled

#define kdispServiceVblank_HAL(pGpu, pKernelDisplay, arg0, arg1, arg2) kdispServiceVblank(pGpu, pKernelDisplay, arg0, arg1, arg2)

NvU32 kdispReadPendingVblank_KERNEL(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, THREAD_STATE_NODE *arg0);

#ifdef __nvoc_kern_disp_h_disabled
static inline NvU32 kdispReadPendingVblank(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, THREAD_STATE_NODE *arg0) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
    return 0;
}
#else //__nvoc_kern_disp_h_disabled
#define kdispReadPendingVblank(pGpu, pKernelDisplay, arg0) kdispReadPendingVblank_KERNEL(pGpu, pKernelDisplay, arg0)
#endif //__nvoc_kern_disp_h_disabled

#define kdispReadPendingVblank_HAL(pGpu, pKernelDisplay, arg0) kdispReadPendingVblank(pGpu, pKernelDisplay, arg0)

static inline NvBool kdispGetVgaWorkspaceBase_ceaee8(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, NvU64 *pOffset) {
    NV_ASSERT_PRECOMP(0);
    return ((NvBool)(0 != 0));
}

#ifdef __nvoc_kern_disp_h_disabled
static inline NvBool kdispGetVgaWorkspaceBase(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, NvU64 *pOffset) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
    return NV_FALSE;
}
#else //__nvoc_kern_disp_h_disabled
#define kdispGetVgaWorkspaceBase(pGpu, pKernelDisplay, pOffset) kdispGetVgaWorkspaceBase_ceaee8(pGpu, pKernelDisplay, pOffset)
#endif //__nvoc_kern_disp_h_disabled

#define kdispGetVgaWorkspaceBase_HAL(pGpu, pKernelDisplay, pOffset) kdispGetVgaWorkspaceBase(pGpu, pKernelDisplay, pOffset)

void kdispInvokeDisplayModesetCallback_KERNEL(struct KernelDisplay *pKernelDisplay, NvBool bModesetStart, NvU32 minRequiredIsoBandwidthKBPS, NvU32 minRequiredFloorBandwidthKBPS);

#ifdef __nvoc_kern_disp_h_disabled
static inline void kdispInvokeDisplayModesetCallback(struct KernelDisplay *pKernelDisplay, NvBool bModesetStart, NvU32 minRequiredIsoBandwidthKBPS, NvU32 minRequiredFloorBandwidthKBPS) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
}
#else //__nvoc_kern_disp_h_disabled
#define kdispInvokeDisplayModesetCallback(pKernelDisplay, bModesetStart, minRequiredIsoBandwidthKBPS, minRequiredFloorBandwidthKBPS) kdispInvokeDisplayModesetCallback_KERNEL(pKernelDisplay, bModesetStart, minRequiredIsoBandwidthKBPS, minRequiredFloorBandwidthKBPS)
#endif //__nvoc_kern_disp_h_disabled

#define kdispInvokeDisplayModesetCallback_HAL(pKernelDisplay, bModesetStart, minRequiredIsoBandwidthKBPS, minRequiredFloorBandwidthKBPS) kdispInvokeDisplayModesetCallback(pKernelDisplay, bModesetStart, minRequiredIsoBandwidthKBPS, minRequiredFloorBandwidthKBPS)

NV_STATUS kdispConstructEngine_IMPL(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, ENGDESCRIPTOR engDesc);

static inline NV_STATUS kdispConstructEngine_DISPATCH(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, ENGDESCRIPTOR engDesc) {
    return pKernelDisplay->__kdispConstructEngine__(pGpu, pKernelDisplay, engDesc);
}

NV_STATUS kdispStatePreInitLocked_IMPL(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay);

static inline NV_STATUS kdispStatePreInitLocked_DISPATCH(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay) {
    return pKernelDisplay->__kdispStatePreInitLocked__(pGpu, pKernelDisplay);
}

NV_STATUS kdispStateInitLocked_IMPL(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay);

static inline NV_STATUS kdispStateInitLocked_DISPATCH(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay) {
    return pKernelDisplay->__kdispStateInitLocked__(pGpu, pKernelDisplay);
}

void kdispStateDestroy_IMPL(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay);

static inline void kdispStateDestroy_DISPATCH(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay) {
    pKernelDisplay->__kdispStateDestroy__(pGpu, pKernelDisplay);
}

NV_STATUS kdispStateLoad_IMPL(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, NvU32 flags);

static inline NV_STATUS kdispStateLoad_DISPATCH(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, NvU32 flags) {
    return pKernelDisplay->__kdispStateLoad__(pGpu, pKernelDisplay, flags);
}

NV_STATUS kdispStateUnload_IMPL(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, NvU32 flags);

static inline NV_STATUS kdispStateUnload_DISPATCH(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, NvU32 flags) {
    return pKernelDisplay->__kdispStateUnload__(pGpu, pKernelDisplay, flags);
}

static inline NV_STATUS kdispReconcileTunableState_DISPATCH(POBJGPU pGpu, struct KernelDisplay *pEngstate, void *pTunableState) {
    return pEngstate->__kdispReconcileTunableState__(pGpu, pEngstate, pTunableState);
}

static inline NV_STATUS kdispStatePreLoad_DISPATCH(POBJGPU pGpu, struct KernelDisplay *pEngstate, NvU32 arg0) {
    return pEngstate->__kdispStatePreLoad__(pGpu, pEngstate, arg0);
}

static inline NV_STATUS kdispStatePostUnload_DISPATCH(POBJGPU pGpu, struct KernelDisplay *pEngstate, NvU32 arg0) {
    return pEngstate->__kdispStatePostUnload__(pGpu, pEngstate, arg0);
}

static inline NV_STATUS kdispStatePreUnload_DISPATCH(POBJGPU pGpu, struct KernelDisplay *pEngstate, NvU32 arg0) {
    return pEngstate->__kdispStatePreUnload__(pGpu, pEngstate, arg0);
}

static inline NV_STATUS kdispStateInitUnlocked_DISPATCH(POBJGPU pGpu, struct KernelDisplay *pEngstate) {
    return pEngstate->__kdispStateInitUnlocked__(pGpu, pEngstate);
}

static inline void kdispInitMissing_DISPATCH(POBJGPU pGpu, struct KernelDisplay *pEngstate) {
    pEngstate->__kdispInitMissing__(pGpu, pEngstate);
}

static inline NV_STATUS kdispStatePreInitUnlocked_DISPATCH(POBJGPU pGpu, struct KernelDisplay *pEngstate) {
    return pEngstate->__kdispStatePreInitUnlocked__(pGpu, pEngstate);
}

static inline NV_STATUS kdispGetTunableState_DISPATCH(POBJGPU pGpu, struct KernelDisplay *pEngstate, void *pTunableState) {
    return pEngstate->__kdispGetTunableState__(pGpu, pEngstate, pTunableState);
}

static inline NV_STATUS kdispCompareTunableState_DISPATCH(POBJGPU pGpu, struct KernelDisplay *pEngstate, void *pTunables1, void *pTunables2) {
    return pEngstate->__kdispCompareTunableState__(pGpu, pEngstate, pTunables1, pTunables2);
}

static inline void kdispFreeTunableState_DISPATCH(POBJGPU pGpu, struct KernelDisplay *pEngstate, void *pTunableState) {
    pEngstate->__kdispFreeTunableState__(pGpu, pEngstate, pTunableState);
}

static inline NV_STATUS kdispStatePostLoad_DISPATCH(POBJGPU pGpu, struct KernelDisplay *pEngstate, NvU32 arg0) {
    return pEngstate->__kdispStatePostLoad__(pGpu, pEngstate, arg0);
}

static inline NV_STATUS kdispAllocTunableState_DISPATCH(POBJGPU pGpu, struct KernelDisplay *pEngstate, void **ppTunableState) {
    return pEngstate->__kdispAllocTunableState__(pGpu, pEngstate, ppTunableState);
}

static inline NV_STATUS kdispSetTunableState_DISPATCH(POBJGPU pGpu, struct KernelDisplay *pEngstate, void *pTunableState) {
    return pEngstate->__kdispSetTunableState__(pGpu, pEngstate, pTunableState);
}

static inline NvBool kdispIsPresent_DISPATCH(POBJGPU pGpu, struct KernelDisplay *pEngstate) {
    return pEngstate->__kdispIsPresent__(pGpu, pEngstate);
}

void kdispDestruct_IMPL(struct KernelDisplay *pKernelDisplay);
#define __nvoc_kdispDestruct(pKernelDisplay) kdispDestruct_IMPL(pKernelDisplay)
NV_STATUS kdispConstructKhead_IMPL(struct KernelDisplay *pKernelDisplay);
#ifdef __nvoc_kern_disp_h_disabled
static inline NV_STATUS kdispConstructKhead(struct KernelDisplay *pKernelDisplay) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_kern_disp_h_disabled
#define kdispConstructKhead(pKernelDisplay) kdispConstructKhead_IMPL(pKernelDisplay)
#endif //__nvoc_kern_disp_h_disabled

void kdispDestructKhead_IMPL(struct KernelDisplay *pKernelDisplay);
#ifdef __nvoc_kern_disp_h_disabled
static inline void kdispDestructKhead(struct KernelDisplay *pKernelDisplay) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
}
#else //__nvoc_kern_disp_h_disabled
#define kdispDestructKhead(pKernelDisplay) kdispDestructKhead_IMPL(pKernelDisplay)
#endif //__nvoc_kern_disp_h_disabled

NV_STATUS kdispGetIntChnClsForHwCls_IMPL(struct KernelDisplay *pKernelDisplay, NvU32 hwClass, DISPCHNCLASS *pDispChnClass);
#ifdef __nvoc_kern_disp_h_disabled
static inline NV_STATUS kdispGetIntChnClsForHwCls(struct KernelDisplay *pKernelDisplay, NvU32 hwClass, DISPCHNCLASS *pDispChnClass) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
    return NV_ERR_NOT_SUPPORTED;
}
#else //__nvoc_kern_disp_h_disabled
#define kdispGetIntChnClsForHwCls(pKernelDisplay, hwClass, pDispChnClass) kdispGetIntChnClsForHwCls_IMPL(pKernelDisplay, hwClass, pDispChnClass)
#endif //__nvoc_kern_disp_h_disabled

void kdispNotifyEvent_IMPL(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, NvU32 notifyIndex, void *pNotifyParams, NvU32 notifyParamsSize, NvV32 info32, NvV16 info16);
#ifdef __nvoc_kern_disp_h_disabled
static inline void kdispNotifyEvent(struct OBJGPU *pGpu, struct KernelDisplay *pKernelDisplay, NvU32 notifyIndex, void *pNotifyParams, NvU32 notifyParamsSize, NvV32 info32, NvV16 info16) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
}
#else //__nvoc_kern_disp_h_disabled
#define kdispNotifyEvent(pGpu, pKernelDisplay, notifyIndex, pNotifyParams, notifyParamsSize, info32, info16) kdispNotifyEvent_IMPL(pGpu, pKernelDisplay, notifyIndex, pNotifyParams, notifyParamsSize, info32, info16)
#endif //__nvoc_kern_disp_h_disabled

void kdispSetWarPurgeSatellitesOnCoreFree_IMPL(struct KernelDisplay *pKernelDisplay, NvBool value);
#ifdef __nvoc_kern_disp_h_disabled
static inline void kdispSetWarPurgeSatellitesOnCoreFree(struct KernelDisplay *pKernelDisplay, NvBool value) {
    NV_ASSERT_FAILED_PRECOMP("KernelDisplay was disabled!");
}
#else //__nvoc_kern_disp_h_disabled
#define kdispSetWarPurgeSatellitesOnCoreFree(pKernelDisplay, value) kdispSetWarPurgeSatellitesOnCoreFree_IMPL(pKernelDisplay, value)
#endif //__nvoc_kern_disp_h_disabled

#undef PRIVATE_FIELD


void 
dispdeviceFillVgaSavedDisplayState( struct OBJGPU *pGpu,
    NvU64   vgaAddr,
    NvU8    vgaMemType,
    NvBool  vgaValid,
    NvU64   workspaceAddr,
    NvU8    workspaceMemType,
    NvBool  workspaceValid,
    NvBool  baseValid,
    NvBool  workspaceBaseValid
);

static NV_INLINE struct KernelHead*
kdispGetHead
(
    struct KernelDisplay *pKernelDisplay,
    NvU32 head
)
{
    if (head >= OBJ_MAX_HEADS)
    {
        return NULL;
    }

    return pKernelDisplay->pKernelHead[head];
}

static NV_INLINE NvU32 
kdispGetNumHeads(struct KernelDisplay *pKernelDisplay)
{
    NV_ASSERT(pKernelDisplay->pStaticInfo != NULL);
    return pKernelDisplay->pStaticInfo->numHeads;
}

static NV_INLINE NvU32 
kdispGetIsPrimaryVga(struct KernelDisplay *pKernelDisplay)
{
    NV_ASSERT(pKernelDisplay->pStaticInfo != NULL);
    return pKernelDisplay->pStaticInfo->bPrimaryVga;
}
#endif // KERN_DISP_H

#ifdef __cplusplus
} // extern "C"
#endif
#endif // _G_KERN_DISP_NVOC_H_
