#ifndef _G_SUBDEVICE_NVOC_H_
#define _G_SUBDEVICE_NVOC_H_
#include "nvoc/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SPDX-FileCopyrightText: Copyright (c) 1993-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
#include "g_subdevice_nvoc.h"

#ifndef _SUBDEVICE_H_
#define _SUBDEVICE_H_

#include "resserv/resserv.h"
#include "nvoc/prelude.h"
#include "resserv/rs_resource.h"
#include "gpu/gpu_resource.h"
#include "rmapi/event.h"
#include "containers/btree.h"
#include "nvoc/utility.h"
#include "gpu/gpu_halspec.h"

#include "class/cl2080.h"
#include "ctrl/ctrl0000/ctrl0000system.h"
#include "ctrl/ctrl2080.h" // rmcontrol parameters

#ifndef NV2080_NOTIFIERS_CE_IDX
// TODO these need to be moved to cl2080.h
#define NV2080_NOTIFIERS_CE_IDX(i) ((i) - NV2080_NOTIFIERS_CE0)
#define NV2080_NOTIFIERS_NVENC_IDX(i) ((i) - NV2080_NOTIFIERS_NVENC0)
#define NV2080_NOTIFIERS_NVDEC_IDX(i) ((i) - NV2080_NOTIFIERS_NVDEC0)
#define NV2080_NOTIFIERS_GR_IDX(i) ((i) - NV2080_NOTIFIERS_GR0)
#endif

#define NV2080_ENGINE_RANGE_GR()    rangeMake(NV2080_ENGINE_TYPE_GR(0), NV2080_ENGINE_TYPE_GR(NV2080_ENGINE_TYPE_GR_SIZE - 1))
#define NV2080_ENGINE_RANGE_COPY()  rangeMake(NV2080_ENGINE_TYPE_COPY(0), NV2080_ENGINE_TYPE_COPY(NV2080_ENGINE_TYPE_COPY_SIZE - 1))
#define NV2080_ENGINE_RANGE_NVDEC() rangeMake(NV2080_ENGINE_TYPE_NVDEC(0), NV2080_ENGINE_TYPE_NVDEC(NV2080_ENGINE_TYPE_NVDEC_SIZE - 1))
#define NV2080_ENGINE_RANGE_NVENC() rangeMake(NV2080_ENGINE_TYPE_NVENC(0), NV2080_ENGINE_TYPE_NVENC(NV2080_ENGINE_TYPE_NVENC_SIZE - 1))
#define NV2080_ENGINE_RANGE_NVJPEG() rangeMake(NV2080_ENGINE_TYPE_NVJPEG(0), NV2080_ENGINE_TYPE_NVJPEG(NV2080_ENGINE_TYPE_NVJPEG_SIZE - 1))

struct Device;

#ifndef __NVOC_CLASS_Device_TYPEDEF__
#define __NVOC_CLASS_Device_TYPEDEF__
typedef struct Device Device;
#endif /* __NVOC_CLASS_Device_TYPEDEF__ */

#ifndef __nvoc_class_id_Device
#define __nvoc_class_id_Device 0xe0ac20
#endif /* __nvoc_class_id_Device */


struct OBJGPU;

#ifndef __NVOC_CLASS_OBJGPU_TYPEDEF__
#define __NVOC_CLASS_OBJGPU_TYPEDEF__
typedef struct OBJGPU OBJGPU;
#endif /* __NVOC_CLASS_OBJGPU_TYPEDEF__ */

#ifndef __nvoc_class_id_OBJGPU
#define __nvoc_class_id_OBJGPU 0x7ef3cb
#endif /* __nvoc_class_id_OBJGPU */


struct Memory;

#ifndef __NVOC_CLASS_Memory_TYPEDEF__
#define __NVOC_CLASS_Memory_TYPEDEF__
typedef struct Memory Memory;
#endif /* __NVOC_CLASS_Memory_TYPEDEF__ */

#ifndef __nvoc_class_id_Memory
#define __nvoc_class_id_Memory 0x4789f2
#endif /* __nvoc_class_id_Memory */


struct P2PApi;

#ifndef __NVOC_CLASS_P2PApi_TYPEDEF__
#define __NVOC_CLASS_P2PApi_TYPEDEF__
typedef struct P2PApi P2PApi;
#endif /* __NVOC_CLASS_P2PApi_TYPEDEF__ */

#ifndef __nvoc_class_id_P2PApi
#define __nvoc_class_id_P2PApi 0x3982b7
#endif /* __nvoc_class_id_P2PApi */



/**
 * A subdevice represents a single GPU within a device. Subdevice provide
 * unicast semantics; that is, operations involving a subdevice are applied to
 * the associated GPU only.
 */
#ifdef NVOC_SUBDEVICE_H_PRIVATE_ACCESS_ALLOWED
#define PRIVATE_FIELD(x) x
#else
#define PRIVATE_FIELD(x) NVOC_PRIVATE_FIELD(x)
#endif
struct Subdevice {
    const struct NVOC_RTTI *__nvoc_rtti;
    struct GpuResource __nvoc_base_GpuResource;
    struct Notifier __nvoc_base_Notifier;
    struct Object *__nvoc_pbase_Object;
    struct RsResource *__nvoc_pbase_RsResource;
    struct RmResourceCommon *__nvoc_pbase_RmResourceCommon;
    struct RmResource *__nvoc_pbase_RmResource;
    struct GpuResource *__nvoc_pbase_GpuResource;
    struct INotifier *__nvoc_pbase_INotifier;
    struct Notifier *__nvoc_pbase_Notifier;
    struct Subdevice *__nvoc_pbase_Subdevice;
    void (*__subdevicePreDestruct__)(struct Subdevice *);
    NV_STATUS (*__subdeviceInternalControlForward__)(struct Subdevice *, NvU32, void *, NvU32);
    NV_STATUS (*__subdeviceControlFilter__)(struct Subdevice *, struct CALL_CONTEXT *, struct RS_RES_CONTROL_PARAMS_INTERNAL *);
    NV_STATUS (*__subdeviceCtrlCmdGpuGetCachedInfo__)(struct Subdevice *, NV2080_CTRL_GPU_GET_INFO_V2_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGpuGetInfoV2__)(struct Subdevice *, NV2080_CTRL_GPU_GET_INFO_V2_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGpuGetIpVersion__)(struct Subdevice *, NV2080_CTRL_GPU_GET_IP_VERSION_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGpuSetOptimusInfo__)(struct Subdevice *, NV2080_CTRL_GPU_OPTIMUS_INFO_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGpuGetNameString__)(struct Subdevice *, NV2080_CTRL_GPU_GET_NAME_STRING_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGpuGetShortNameString__)(struct Subdevice *, NV2080_CTRL_GPU_GET_SHORT_NAME_STRING_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGpuGetSdm__)(struct Subdevice *, NV2080_CTRL_GPU_GET_SDM_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGpuSetSdm__)(struct Subdevice *, NV2080_CTRL_GPU_SET_SDM_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGpuGetSimulationInfo__)(struct Subdevice *, NV2080_CTRL_GPU_GET_SIMULATION_INFO_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGpuGetEngines__)(struct Subdevice *, NV2080_CTRL_GPU_GET_ENGINES_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGpuGetEnginesV2__)(struct Subdevice *, NV2080_CTRL_GPU_GET_ENGINES_V2_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGpuGetEngineClasslist__)(struct Subdevice *, NV2080_CTRL_GPU_GET_ENGINE_CLASSLIST_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGpuGetEnginePartnerList__)(struct Subdevice *, NV2080_CTRL_GPU_GET_ENGINE_PARTNERLIST_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGpuQueryMode__)(struct Subdevice *, NV2080_CTRL_GPU_QUERY_MODE_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGpuGetOEMBoardInfo__)(struct Subdevice *, NV2080_CTRL_GPU_GET_OEM_BOARD_INFO_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGpuGetOEMInfo__)(struct Subdevice *, NV2080_CTRL_GPU_GET_OEM_INFO_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGpuHandleGpuSR__)(struct Subdevice *);
    NV_STATUS (*__subdeviceCtrlCmdGpuInitializeCtx__)(struct Subdevice *, NV2080_CTRL_GPU_INITIALIZE_CTX_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGpuPromoteCtx__)(struct Subdevice *, NV2080_CTRL_GPU_PROMOTE_CTX_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGpuEvictCtx__)(struct Subdevice *, NV2080_CTRL_GPU_EVICT_CTX_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGpuGetId__)(struct Subdevice *, NV2080_CTRL_GPU_GET_ID_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGpuGetGidInfo__)(struct Subdevice *, NV2080_CTRL_GPU_GET_GID_INFO_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGpuGetPids__)(struct Subdevice *, NV2080_CTRL_GPU_GET_PIDS_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGpuGetPidInfo__)(struct Subdevice *, NV2080_CTRL_GPU_GET_PID_INFO_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGpuQueryFunctionStatus__)(struct Subdevice *, NV2080_CTRL_CMD_GPU_QUERY_FUNCTION_STATUS_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGpuGetMaxSupportedPageSize__)(struct Subdevice *, NV2080_CTRL_GPU_GET_MAX_SUPPORTED_PAGE_SIZE_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdValidateMemMapRequest__)(struct Subdevice *, NV2080_CTRL_GPU_VALIDATE_MEM_MAP_REQUEST_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGpuGetEngineLoadTimes__)(struct Subdevice *, NV2080_CTRL_GPU_GET_ENGINE_LOAD_TIMES_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdEventSetTrigger__)(struct Subdevice *);
    NV_STATUS (*__subdeviceCtrlCmdEventSetTriggerFifo__)(struct Subdevice *, NV2080_CTRL_EVENT_SET_TRIGGER_FIFO_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdEventSetNotification__)(struct Subdevice *, NV2080_CTRL_EVENT_SET_NOTIFICATION_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdEventSetMemoryNotifies__)(struct Subdevice *, NV2080_CTRL_EVENT_SET_MEMORY_NOTIFIES_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdEventSetSemaphoreMemory__)(struct Subdevice *, NV2080_CTRL_EVENT_SET_SEMAPHORE_MEMORY_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdEventSetSemaMemValidation__)(struct Subdevice *, NV2080_CTRL_EVENT_SET_SEMA_MEM_VALIDATION_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdTimerCancel__)(struct Subdevice *);
    NV_STATUS (*__subdeviceCtrlCmdTimerSchedule__)(struct Subdevice *, NV2080_CTRL_CMD_TIMER_SCHEDULE_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdTimerGetTime__)(struct Subdevice *, NV2080_CTRL_TIMER_GET_TIME_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdTimerGetRegisterOffset__)(struct Subdevice *, NV2080_CTRL_TIMER_GET_REGISTER_OFFSET_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdTimerGetGpuCpuTimeCorrelationInfo__)(struct Subdevice *, NV2080_CTRL_TIMER_GET_GPU_CPU_TIME_CORRELATION_INFO_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdEccGetClientExposedCounters__)(struct Subdevice *, NV2080_CTRL_ECC_GET_CLIENT_EXPOSED_COUNTERS_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdGspGetFeatures__)(struct Subdevice *, NV2080_CTRL_GSP_GET_FEATURES_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdOsUnixGc6BlockerRefCnt__)(struct Subdevice *, NV2080_CTRL_OS_UNIX_GC6_BLOCKER_REFCNT_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdOsUnixAllowDisallowGcoff__)(struct Subdevice *, NV2080_CTRL_OS_UNIX_ALLOW_DISALLOW_GCOFF_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdOsUnixAudioDynamicPower__)(struct Subdevice *, NV2080_CTRL_OS_UNIX_AUDIO_DYNAMIC_POWER_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdDisplayGetIpVersion__)(struct Subdevice *, NV2080_CTRL_INTERNAL_DISPLAY_GET_IP_VERSION_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdDisplayGetStaticInfo__)(struct Subdevice *, NV2080_CTRL_INTERNAL_DISPLAY_GET_STATIC_INFO_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdDisplaySetChannelPushbuffer__)(struct Subdevice *, NV2080_CTRL_INTERNAL_DISPLAY_CHANNEL_PUSHBUFFER_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdDisplayWriteInstMem__)(struct Subdevice *, NV2080_CTRL_INTERNAL_DISPLAY_WRITE_INST_MEM_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdDisplaySetupRgLineIntr__)(struct Subdevice *, NV2080_CTRL_INTERNAL_DISPLAY_SETUP_RG_LINE_INTR_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdDisplaySetImportedImpData__)(struct Subdevice *, NV2080_CTRL_INTERNAL_DISPLAY_SET_IMP_INIT_INFO_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdDisplayGetDisplayMask__)(struct Subdevice *, NV2080_CTRL_INTERNAL_DISPLAY_GET_ACTIVE_DISPLAY_DEVICES_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdInternalGetChipInfo__)(struct Subdevice *, NV2080_CTRL_INTERNAL_GPU_GET_CHIP_INFO_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdInternalGetUserRegisterAccessMap__)(struct Subdevice *, NV2080_CTRL_INTERNAL_GPU_GET_USER_REGISTER_ACCESS_MAP_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdInternalGetDeviceInfoTable__)(struct Subdevice *, NV2080_CTRL_INTERNAL_GET_DEVICE_INFO_TABLE_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdInternalGetConstructedFalconInfo__)(struct Subdevice *, NV2080_CTRL_INTERNAL_GET_CONSTRUCTED_FALCON_INFO_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdInternalRecoverAllComputeContexts__)(struct Subdevice *);
    NV_STATUS (*__subdeviceCtrlCmdInternalGetSmcMode__)(struct Subdevice *, NV2080_CTRL_INTERNAL_GPU_GET_SMC_MODE_PARAMS *);
    NV_STATUS (*__subdeviceCtrlCmdInternalGetPcieP2pCaps__)(struct Subdevice *, NV2080_CTRL_INTERNAL_GET_PCIE_P2P_CAPS_PARAMS *);
    NvBool (*__subdeviceShareCallback__)(struct Subdevice *, struct RsClient *, struct RsResourceRef *, RS_SHARE_POLICY *);
    NV_STATUS (*__subdeviceMapTo__)(struct Subdevice *, RS_RES_MAP_TO_PARAMS *);
    NV_STATUS (*__subdeviceGetOrAllocNotifShare__)(struct Subdevice *, NvHandle, NvHandle, struct NotifShare **);
    NV_STATUS (*__subdeviceCheckMemInterUnmap__)(struct Subdevice *, NvBool);
    NV_STATUS (*__subdeviceGetMapAddrSpace__)(struct Subdevice *, struct CALL_CONTEXT *, NvU32, NV_ADDRESS_SPACE *);
    void (*__subdeviceSetNotificationShare__)(struct Subdevice *, struct NotifShare *);
    NvU32 (*__subdeviceGetRefCount__)(struct Subdevice *);
    void (*__subdeviceAddAdditionalDependants__)(struct RsClient *, struct Subdevice *, RsResourceRef *);
    NV_STATUS (*__subdeviceControl_Prologue__)(struct Subdevice *, struct CALL_CONTEXT *, struct RS_RES_CONTROL_PARAMS_INTERNAL *);
    NV_STATUS (*__subdeviceGetRegBaseOffsetAndSize__)(struct Subdevice *, struct OBJGPU *, NvU32 *, NvU32 *);
    NV_STATUS (*__subdeviceUnmapFrom__)(struct Subdevice *, RS_RES_UNMAP_FROM_PARAMS *);
    void (*__subdeviceControl_Epilogue__)(struct Subdevice *, struct CALL_CONTEXT *, struct RS_RES_CONTROL_PARAMS_INTERNAL *);
    NV_STATUS (*__subdeviceControlLookup__)(struct Subdevice *, struct RS_RES_CONTROL_PARAMS_INTERNAL *, const struct NVOC_EXPORTED_METHOD_DEF **);
    NvHandle (*__subdeviceGetInternalObjectHandle__)(struct Subdevice *);
    NV_STATUS (*__subdeviceControl__)(struct Subdevice *, struct CALL_CONTEXT *, struct RS_RES_CONTROL_PARAMS_INTERNAL *);
    NV_STATUS (*__subdeviceUnmap__)(struct Subdevice *, struct CALL_CONTEXT *, struct RsCpuMapping *);
    NV_STATUS (*__subdeviceGetMemInterMapParams__)(struct Subdevice *, RMRES_MEM_INTER_MAP_PARAMS *);
    NV_STATUS (*__subdeviceGetMemoryMappingDescriptor__)(struct Subdevice *, struct MEMORY_DESCRIPTOR **);
    NV_STATUS (*__subdeviceUnregisterEvent__)(struct Subdevice *, NvHandle, NvHandle, NvHandle, NvHandle);
    NvBool (*__subdeviceCanCopy__)(struct Subdevice *);
    PEVENTNOTIFICATION *(*__subdeviceGetNotificationListPtr__)(struct Subdevice *);
    struct NotifShare *(*__subdeviceGetNotificationShare__)(struct Subdevice *);
    NV_STATUS (*__subdeviceMap__)(struct Subdevice *, struct CALL_CONTEXT *, struct RS_CPU_MAP_PARAMS *, struct RsCpuMapping *);
    NvBool (*__subdeviceAccessCallback__)(struct Subdevice *, struct RsClient *, void *, RsAccessRight);
    NvU32 deviceInst;
    NvU32 subDeviceInst;
    struct Device *pDevice;
    NvBool bMaxGrTickFreqRequested;
    NvU64 P2PfbMappedBytes;
    NvU32 notifyActions[165];
    NvHandle hNotifierMemory;
    struct Memory *pNotifierMemory;
    NvHandle hSemMemory;
    NvU32 videoStream4KCount;
    NvU32 videoStreamHDCount;
    NvU32 videoStreamSDCount;
    NvU32 videoStreamLinearCount;
    NvU32 ofaCount;
    NvBool bGpuDebugModeEnabled;
    NvBool bRcWatchdogEnableRequested;
    NvBool bRcWatchdogDisableRequested;
    NvBool bRcWatchdogSoftDisableRequested;
    NvBool bReservePerfMon;
    NvU32 perfBoostIndex;
    NvU32 perfBoostRefCount;
    NvBool perfBoostEntryExists;
    NvBool bLockedClockModeRequested;
    NvU32 bNvlinkErrorInjectionModeRequested;
    NvBool bSchedPolicySet;
    NvBool bGcoffDisallowed;
    NvBool bUpdateTGP;
};

#ifndef __NVOC_CLASS_Subdevice_TYPEDEF__
#define __NVOC_CLASS_Subdevice_TYPEDEF__
typedef struct Subdevice Subdevice;
#endif /* __NVOC_CLASS_Subdevice_TYPEDEF__ */

#ifndef __nvoc_class_id_Subdevice
#define __nvoc_class_id_Subdevice 0x4b01b3
#endif /* __nvoc_class_id_Subdevice */

extern const struct NVOC_CLASS_DEF __nvoc_class_def_Subdevice;

#define __staticCast_Subdevice(pThis) \
    ((pThis)->__nvoc_pbase_Subdevice)

#ifdef __nvoc_subdevice_h_disabled
#define __dynamicCast_Subdevice(pThis) ((Subdevice*)NULL)
#else //__nvoc_subdevice_h_disabled
#define __dynamicCast_Subdevice(pThis) \
    ((Subdevice*)__nvoc_dynamicCast(staticCast((pThis), Dynamic), classInfo(Subdevice)))
#endif //__nvoc_subdevice_h_disabled


NV_STATUS __nvoc_objCreateDynamic_Subdevice(Subdevice**, Dynamic*, NvU32, va_list);

NV_STATUS __nvoc_objCreate_Subdevice(Subdevice**, Dynamic*, NvU32, struct CALL_CONTEXT * arg_pCallContext, struct RS_RES_ALLOC_PARAMS_INTERNAL * arg_pParams);
#define __objCreate_Subdevice(ppNewObj, pParent, createFlags, arg_pCallContext, arg_pParams) \
    __nvoc_objCreate_Subdevice((ppNewObj), staticCast((pParent), Dynamic), (createFlags), arg_pCallContext, arg_pParams)

#define subdevicePreDestruct(pResource) subdevicePreDestruct_DISPATCH(pResource)
#define subdeviceInternalControlForward(pSubdevice, command, pParams, size) subdeviceInternalControlForward_DISPATCH(pSubdevice, command, pParams, size)
#define subdeviceControlFilter(pSubdevice, pCallContext, pParams) subdeviceControlFilter_DISPATCH(pSubdevice, pCallContext, pParams)
#define subdeviceCtrlCmdGpuGetCachedInfo(pSubdevice, pGpuInfoParams) subdeviceCtrlCmdGpuGetCachedInfo_DISPATCH(pSubdevice, pGpuInfoParams)
#define subdeviceCtrlCmdGpuGetInfoV2(pSubdevice, pGpuInfoParams) subdeviceCtrlCmdGpuGetInfoV2_DISPATCH(pSubdevice, pGpuInfoParams)
#define subdeviceCtrlCmdGpuGetIpVersion(pSubdevice, pGpuIpVersionParams) subdeviceCtrlCmdGpuGetIpVersion_DISPATCH(pSubdevice, pGpuIpVersionParams)
#define subdeviceCtrlCmdGpuSetOptimusInfo(pSubdevice, pGpuOptimusInfoParams) subdeviceCtrlCmdGpuSetOptimusInfo_DISPATCH(pSubdevice, pGpuOptimusInfoParams)
#define subdeviceCtrlCmdGpuGetNameString(pSubdevice, pNameStringParams) subdeviceCtrlCmdGpuGetNameString_DISPATCH(pSubdevice, pNameStringParams)
#define subdeviceCtrlCmdGpuGetShortNameString(pSubdevice, pShortNameStringParams) subdeviceCtrlCmdGpuGetShortNameString_DISPATCH(pSubdevice, pShortNameStringParams)
#define subdeviceCtrlCmdGpuGetSdm(pSubdevice, pSdmParams) subdeviceCtrlCmdGpuGetSdm_DISPATCH(pSubdevice, pSdmParams)
#define subdeviceCtrlCmdGpuSetSdm(pSubdevice, pSdmParams) subdeviceCtrlCmdGpuSetSdm_DISPATCH(pSubdevice, pSdmParams)
#define subdeviceCtrlCmdGpuGetSimulationInfo(pSubdevice, pGpuSimulationInfoParams) subdeviceCtrlCmdGpuGetSimulationInfo_DISPATCH(pSubdevice, pGpuSimulationInfoParams)
#define subdeviceCtrlCmdGpuGetEngines(pSubdevice, pParams) subdeviceCtrlCmdGpuGetEngines_DISPATCH(pSubdevice, pParams)
#define subdeviceCtrlCmdGpuGetEnginesV2(pSubdevice, pEngineParams) subdeviceCtrlCmdGpuGetEnginesV2_DISPATCH(pSubdevice, pEngineParams)
#define subdeviceCtrlCmdGpuGetEngineClasslist(pSubdevice, pClassParams) subdeviceCtrlCmdGpuGetEngineClasslist_DISPATCH(pSubdevice, pClassParams)
#define subdeviceCtrlCmdGpuGetEnginePartnerList(pSubdevice, pPartnerListParams) subdeviceCtrlCmdGpuGetEnginePartnerList_DISPATCH(pSubdevice, pPartnerListParams)
#define subdeviceCtrlCmdGpuQueryMode(pSubdevice, pQueryMode) subdeviceCtrlCmdGpuQueryMode_DISPATCH(pSubdevice, pQueryMode)
#define subdeviceCtrlCmdGpuGetOEMBoardInfo(pSubdevice, pBoardInfo) subdeviceCtrlCmdGpuGetOEMBoardInfo_DISPATCH(pSubdevice, pBoardInfo)
#define subdeviceCtrlCmdGpuGetOEMInfo(pSubdevice, pOemInfo) subdeviceCtrlCmdGpuGetOEMInfo_DISPATCH(pSubdevice, pOemInfo)
#define subdeviceCtrlCmdGpuHandleGpuSR(pSubdevice) subdeviceCtrlCmdGpuHandleGpuSR_DISPATCH(pSubdevice)
#define subdeviceCtrlCmdGpuInitializeCtx(pSubdevice, pInitializeCtxParams) subdeviceCtrlCmdGpuInitializeCtx_DISPATCH(pSubdevice, pInitializeCtxParams)
#define subdeviceCtrlCmdGpuPromoteCtx(pSubdevice, pPromoteCtxParams) subdeviceCtrlCmdGpuPromoteCtx_DISPATCH(pSubdevice, pPromoteCtxParams)
#define subdeviceCtrlCmdGpuEvictCtx(pSubdevice, pEvictCtxParams) subdeviceCtrlCmdGpuEvictCtx_DISPATCH(pSubdevice, pEvictCtxParams)
#define subdeviceCtrlCmdGpuGetId(pSubdevice, pIdParams) subdeviceCtrlCmdGpuGetId_DISPATCH(pSubdevice, pIdParams)
#define subdeviceCtrlCmdGpuGetGidInfo(pSubdevice, pGidInfoParams) subdeviceCtrlCmdGpuGetGidInfo_DISPATCH(pSubdevice, pGidInfoParams)
#define subdeviceCtrlCmdGpuGetPids(pSubdevice, pGetPidsParams) subdeviceCtrlCmdGpuGetPids_DISPATCH(pSubdevice, pGetPidsParams)
#define subdeviceCtrlCmdGpuGetPidInfo(pSubdevice, pGetPidInfoParams) subdeviceCtrlCmdGpuGetPidInfo_DISPATCH(pSubdevice, pGetPidInfoParams)
#define subdeviceCtrlCmdGpuQueryFunctionStatus(pSubdevice, pParams) subdeviceCtrlCmdGpuQueryFunctionStatus_DISPATCH(pSubdevice, pParams)
#define subdeviceCtrlCmdGpuGetMaxSupportedPageSize(pSubdevice, pParams) subdeviceCtrlCmdGpuGetMaxSupportedPageSize_DISPATCH(pSubdevice, pParams)
#define subdeviceCtrlCmdValidateMemMapRequest(pSubdevice, pParams) subdeviceCtrlCmdValidateMemMapRequest_DISPATCH(pSubdevice, pParams)
#define subdeviceCtrlCmdGpuGetEngineLoadTimes(pSubdevice, pParams) subdeviceCtrlCmdGpuGetEngineLoadTimes_DISPATCH(pSubdevice, pParams)
#define subdeviceCtrlCmdEventSetTrigger(pSubdevice) subdeviceCtrlCmdEventSetTrigger_DISPATCH(pSubdevice)
#define subdeviceCtrlCmdEventSetTriggerFifo(pSubdevice, pTriggerFifoParams) subdeviceCtrlCmdEventSetTriggerFifo_DISPATCH(pSubdevice, pTriggerFifoParams)
#define subdeviceCtrlCmdEventSetNotification(pSubdevice, pSetEventParams) subdeviceCtrlCmdEventSetNotification_DISPATCH(pSubdevice, pSetEventParams)
#define subdeviceCtrlCmdEventSetMemoryNotifies(pSubdevice, pSetMemoryNotifiesParams) subdeviceCtrlCmdEventSetMemoryNotifies_DISPATCH(pSubdevice, pSetMemoryNotifiesParams)
#define subdeviceCtrlCmdEventSetSemaphoreMemory(pSubdevice, pSetSemMemoryParams) subdeviceCtrlCmdEventSetSemaphoreMemory_DISPATCH(pSubdevice, pSetSemMemoryParams)
#define subdeviceCtrlCmdEventSetSemaMemValidation(pSubdevice, pSetSemaMemValidationParams) subdeviceCtrlCmdEventSetSemaMemValidation_DISPATCH(pSubdevice, pSetSemaMemValidationParams)
#define subdeviceCtrlCmdTimerCancel(pSubdevice) subdeviceCtrlCmdTimerCancel_DISPATCH(pSubdevice)
#define subdeviceCtrlCmdTimerSchedule(pSubdevice, pParams) subdeviceCtrlCmdTimerSchedule_DISPATCH(pSubdevice, pParams)
#define subdeviceCtrlCmdTimerGetTime(pSubdevice, pParams) subdeviceCtrlCmdTimerGetTime_DISPATCH(pSubdevice, pParams)
#define subdeviceCtrlCmdTimerGetRegisterOffset(pSubdevice, pTimerRegOffsetParams) subdeviceCtrlCmdTimerGetRegisterOffset_DISPATCH(pSubdevice, pTimerRegOffsetParams)
#define subdeviceCtrlCmdTimerGetGpuCpuTimeCorrelationInfo(pSubdevice, pParams) subdeviceCtrlCmdTimerGetGpuCpuTimeCorrelationInfo_DISPATCH(pSubdevice, pParams)
#define subdeviceCtrlCmdEccGetClientExposedCounters(pSubdevice, pParams) subdeviceCtrlCmdEccGetClientExposedCounters_DISPATCH(pSubdevice, pParams)
#define subdeviceCtrlCmdGspGetFeatures(pSubdevice, pGspFeaturesParams) subdeviceCtrlCmdGspGetFeatures_DISPATCH(pSubdevice, pGspFeaturesParams)
#define subdeviceCtrlCmdOsUnixGc6BlockerRefCnt(pSubdevice, pParams) subdeviceCtrlCmdOsUnixGc6BlockerRefCnt_DISPATCH(pSubdevice, pParams)
#define subdeviceCtrlCmdOsUnixAllowDisallowGcoff(pSubdevice, pParams) subdeviceCtrlCmdOsUnixAllowDisallowGcoff_DISPATCH(pSubdevice, pParams)
#define subdeviceCtrlCmdOsUnixAudioDynamicPower(pSubdevice, pParams) subdeviceCtrlCmdOsUnixAudioDynamicPower_DISPATCH(pSubdevice, pParams)
#define subdeviceCtrlCmdDisplayGetIpVersion(pSubdevice, pParams) subdeviceCtrlCmdDisplayGetIpVersion_DISPATCH(pSubdevice, pParams)
#define subdeviceCtrlCmdDisplayGetStaticInfo(pSubdevice, pParams) subdeviceCtrlCmdDisplayGetStaticInfo_DISPATCH(pSubdevice, pParams)
#define subdeviceCtrlCmdDisplaySetChannelPushbuffer(pSubdevice, pParams) subdeviceCtrlCmdDisplaySetChannelPushbuffer_DISPATCH(pSubdevice, pParams)
#define subdeviceCtrlCmdDisplayWriteInstMem(pSubdevice, pParams) subdeviceCtrlCmdDisplayWriteInstMem_DISPATCH(pSubdevice, pParams)
#define subdeviceCtrlCmdDisplaySetupRgLineIntr(pSubdevice, pParams) subdeviceCtrlCmdDisplaySetupRgLineIntr_DISPATCH(pSubdevice, pParams)
#define subdeviceCtrlCmdDisplaySetImportedImpData(pSubdevice, pParams) subdeviceCtrlCmdDisplaySetImportedImpData_DISPATCH(pSubdevice, pParams)
#define subdeviceCtrlCmdDisplayGetDisplayMask(pSubdevice, pParams) subdeviceCtrlCmdDisplayGetDisplayMask_DISPATCH(pSubdevice, pParams)
#define subdeviceCtrlCmdInternalGetChipInfo(pSubdevice, pParams) subdeviceCtrlCmdInternalGetChipInfo_DISPATCH(pSubdevice, pParams)
#define subdeviceCtrlCmdInternalGetUserRegisterAccessMap(pSubdevice, pParams) subdeviceCtrlCmdInternalGetUserRegisterAccessMap_DISPATCH(pSubdevice, pParams)
#define subdeviceCtrlCmdInternalGetDeviceInfoTable(pSubdevice, pParams) subdeviceCtrlCmdInternalGetDeviceInfoTable_DISPATCH(pSubdevice, pParams)
#define subdeviceCtrlCmdInternalGetConstructedFalconInfo(pSubdevice, pParams) subdeviceCtrlCmdInternalGetConstructedFalconInfo_DISPATCH(pSubdevice, pParams)
#define subdeviceCtrlCmdInternalRecoverAllComputeContexts(pSubdevice) subdeviceCtrlCmdInternalRecoverAllComputeContexts_DISPATCH(pSubdevice)
#define subdeviceCtrlCmdInternalGetSmcMode(pSubdevice, pParams) subdeviceCtrlCmdInternalGetSmcMode_DISPATCH(pSubdevice, pParams)
#define subdeviceCtrlCmdInternalGetPcieP2pCaps(pSubdevice, pParams) subdeviceCtrlCmdInternalGetPcieP2pCaps_DISPATCH(pSubdevice, pParams)
#define subdeviceShareCallback(pGpuResource, pInvokingClient, pParentRef, pSharePolicy) subdeviceShareCallback_DISPATCH(pGpuResource, pInvokingClient, pParentRef, pSharePolicy)
#define subdeviceMapTo(pResource, pParams) subdeviceMapTo_DISPATCH(pResource, pParams)
#define subdeviceGetOrAllocNotifShare(pNotifier, hNotifierClient, hNotifierResource, ppNotifShare) subdeviceGetOrAllocNotifShare_DISPATCH(pNotifier, hNotifierClient, hNotifierResource, ppNotifShare)
#define subdeviceCheckMemInterUnmap(pRmResource, bSubdeviceHandleProvided) subdeviceCheckMemInterUnmap_DISPATCH(pRmResource, bSubdeviceHandleProvided)
#define subdeviceGetMapAddrSpace(pGpuResource, pCallContext, mapFlags, pAddrSpace) subdeviceGetMapAddrSpace_DISPATCH(pGpuResource, pCallContext, mapFlags, pAddrSpace)
#define subdeviceSetNotificationShare(pNotifier, pNotifShare) subdeviceSetNotificationShare_DISPATCH(pNotifier, pNotifShare)
#define subdeviceGetRefCount(pResource) subdeviceGetRefCount_DISPATCH(pResource)
#define subdeviceAddAdditionalDependants(pClient, pResource, pReference) subdeviceAddAdditionalDependants_DISPATCH(pClient, pResource, pReference)
#define subdeviceControl_Prologue(pResource, pCallContext, pParams) subdeviceControl_Prologue_DISPATCH(pResource, pCallContext, pParams)
#define subdeviceGetRegBaseOffsetAndSize(pGpuResource, pGpu, pOffset, pSize) subdeviceGetRegBaseOffsetAndSize_DISPATCH(pGpuResource, pGpu, pOffset, pSize)
#define subdeviceUnmapFrom(pResource, pParams) subdeviceUnmapFrom_DISPATCH(pResource, pParams)
#define subdeviceControl_Epilogue(pResource, pCallContext, pParams) subdeviceControl_Epilogue_DISPATCH(pResource, pCallContext, pParams)
#define subdeviceControlLookup(pResource, pParams, ppEntry) subdeviceControlLookup_DISPATCH(pResource, pParams, ppEntry)
#define subdeviceGetInternalObjectHandle(pGpuResource) subdeviceGetInternalObjectHandle_DISPATCH(pGpuResource)
#define subdeviceControl(pGpuResource, pCallContext, pParams) subdeviceControl_DISPATCH(pGpuResource, pCallContext, pParams)
#define subdeviceUnmap(pGpuResource, pCallContext, pCpuMapping) subdeviceUnmap_DISPATCH(pGpuResource, pCallContext, pCpuMapping)
#define subdeviceGetMemInterMapParams(pRmResource, pParams) subdeviceGetMemInterMapParams_DISPATCH(pRmResource, pParams)
#define subdeviceGetMemoryMappingDescriptor(pRmResource, ppMemDesc) subdeviceGetMemoryMappingDescriptor_DISPATCH(pRmResource, ppMemDesc)
#define subdeviceUnregisterEvent(pNotifier, hNotifierClient, hNotifierResource, hEventClient, hEvent) subdeviceUnregisterEvent_DISPATCH(pNotifier, hNotifierClient, hNotifierResource, hEventClient, hEvent)
#define subdeviceCanCopy(pResource) subdeviceCanCopy_DISPATCH(pResource)
#define subdeviceGetNotificationListPtr(pNotifier) subdeviceGetNotificationListPtr_DISPATCH(pNotifier)
#define subdeviceGetNotificationShare(pNotifier) subdeviceGetNotificationShare_DISPATCH(pNotifier)
#define subdeviceMap(pGpuResource, pCallContext, pParams, pCpuMapping) subdeviceMap_DISPATCH(pGpuResource, pCallContext, pParams, pCpuMapping)
#define subdeviceAccessCallback(pResource, pInvokingClient, pAllocParams, accessRight) subdeviceAccessCallback_DISPATCH(pResource, pInvokingClient, pAllocParams, accessRight)
void subdevicePreDestruct_IMPL(struct Subdevice *pResource);

static inline void subdevicePreDestruct_DISPATCH(struct Subdevice *pResource) {
    pResource->__subdevicePreDestruct__(pResource);
}

NV_STATUS subdeviceInternalControlForward_IMPL(struct Subdevice *pSubdevice, NvU32 command, void *pParams, NvU32 size);

static inline NV_STATUS subdeviceInternalControlForward_DISPATCH(struct Subdevice *pSubdevice, NvU32 command, void *pParams, NvU32 size) {
    return pSubdevice->__subdeviceInternalControlForward__(pSubdevice, command, pParams, size);
}

NV_STATUS subdeviceControlFilter_IMPL(struct Subdevice *pSubdevice, struct CALL_CONTEXT *pCallContext, struct RS_RES_CONTROL_PARAMS_INTERNAL *pParams);

static inline NV_STATUS subdeviceControlFilter_DISPATCH(struct Subdevice *pSubdevice, struct CALL_CONTEXT *pCallContext, struct RS_RES_CONTROL_PARAMS_INTERNAL *pParams) {
    return pSubdevice->__subdeviceControlFilter__(pSubdevice, pCallContext, pParams);
}

NV_STATUS subdeviceCtrlCmdGpuGetCachedInfo_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_INFO_V2_PARAMS *pGpuInfoParams);

static inline NV_STATUS subdeviceCtrlCmdGpuGetCachedInfo_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_INFO_V2_PARAMS *pGpuInfoParams) {
    return pSubdevice->__subdeviceCtrlCmdGpuGetCachedInfo__(pSubdevice, pGpuInfoParams);
}

NV_STATUS subdeviceCtrlCmdGpuGetInfoV2_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_INFO_V2_PARAMS *pGpuInfoParams);

static inline NV_STATUS subdeviceCtrlCmdGpuGetInfoV2_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_INFO_V2_PARAMS *pGpuInfoParams) {
    return pSubdevice->__subdeviceCtrlCmdGpuGetInfoV2__(pSubdevice, pGpuInfoParams);
}

NV_STATUS subdeviceCtrlCmdGpuGetIpVersion_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_IP_VERSION_PARAMS *pGpuIpVersionParams);

static inline NV_STATUS subdeviceCtrlCmdGpuGetIpVersion_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_IP_VERSION_PARAMS *pGpuIpVersionParams) {
    return pSubdevice->__subdeviceCtrlCmdGpuGetIpVersion__(pSubdevice, pGpuIpVersionParams);
}

NV_STATUS subdeviceCtrlCmdGpuSetOptimusInfo_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_OPTIMUS_INFO_PARAMS *pGpuOptimusInfoParams);

static inline NV_STATUS subdeviceCtrlCmdGpuSetOptimusInfo_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_OPTIMUS_INFO_PARAMS *pGpuOptimusInfoParams) {
    return pSubdevice->__subdeviceCtrlCmdGpuSetOptimusInfo__(pSubdevice, pGpuOptimusInfoParams);
}

NV_STATUS subdeviceCtrlCmdGpuGetNameString_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_NAME_STRING_PARAMS *pNameStringParams);

static inline NV_STATUS subdeviceCtrlCmdGpuGetNameString_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_NAME_STRING_PARAMS *pNameStringParams) {
    return pSubdevice->__subdeviceCtrlCmdGpuGetNameString__(pSubdevice, pNameStringParams);
}

NV_STATUS subdeviceCtrlCmdGpuGetShortNameString_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_SHORT_NAME_STRING_PARAMS *pShortNameStringParams);

static inline NV_STATUS subdeviceCtrlCmdGpuGetShortNameString_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_SHORT_NAME_STRING_PARAMS *pShortNameStringParams) {
    return pSubdevice->__subdeviceCtrlCmdGpuGetShortNameString__(pSubdevice, pShortNameStringParams);
}

NV_STATUS subdeviceCtrlCmdGpuGetSdm_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_SDM_PARAMS *pSdmParams);

static inline NV_STATUS subdeviceCtrlCmdGpuGetSdm_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_SDM_PARAMS *pSdmParams) {
    return pSubdevice->__subdeviceCtrlCmdGpuGetSdm__(pSubdevice, pSdmParams);
}

NV_STATUS subdeviceCtrlCmdGpuSetSdm_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_SET_SDM_PARAMS *pSdmParams);

static inline NV_STATUS subdeviceCtrlCmdGpuSetSdm_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_SET_SDM_PARAMS *pSdmParams) {
    return pSubdevice->__subdeviceCtrlCmdGpuSetSdm__(pSubdevice, pSdmParams);
}

NV_STATUS subdeviceCtrlCmdGpuGetSimulationInfo_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_SIMULATION_INFO_PARAMS *pGpuSimulationInfoParams);

static inline NV_STATUS subdeviceCtrlCmdGpuGetSimulationInfo_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_SIMULATION_INFO_PARAMS *pGpuSimulationInfoParams) {
    return pSubdevice->__subdeviceCtrlCmdGpuGetSimulationInfo__(pSubdevice, pGpuSimulationInfoParams);
}

NV_STATUS subdeviceCtrlCmdGpuGetEngines_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_ENGINES_PARAMS *pParams);

static inline NV_STATUS subdeviceCtrlCmdGpuGetEngines_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_ENGINES_PARAMS *pParams) {
    return pSubdevice->__subdeviceCtrlCmdGpuGetEngines__(pSubdevice, pParams);
}

NV_STATUS subdeviceCtrlCmdGpuGetEnginesV2_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_ENGINES_V2_PARAMS *pEngineParams);

static inline NV_STATUS subdeviceCtrlCmdGpuGetEnginesV2_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_ENGINES_V2_PARAMS *pEngineParams) {
    return pSubdevice->__subdeviceCtrlCmdGpuGetEnginesV2__(pSubdevice, pEngineParams);
}

NV_STATUS subdeviceCtrlCmdGpuGetEngineClasslist_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_ENGINE_CLASSLIST_PARAMS *pClassParams);

static inline NV_STATUS subdeviceCtrlCmdGpuGetEngineClasslist_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_ENGINE_CLASSLIST_PARAMS *pClassParams) {
    return pSubdevice->__subdeviceCtrlCmdGpuGetEngineClasslist__(pSubdevice, pClassParams);
}

NV_STATUS subdeviceCtrlCmdGpuGetEnginePartnerList_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_ENGINE_PARTNERLIST_PARAMS *pPartnerListParams);

static inline NV_STATUS subdeviceCtrlCmdGpuGetEnginePartnerList_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_ENGINE_PARTNERLIST_PARAMS *pPartnerListParams) {
    return pSubdevice->__subdeviceCtrlCmdGpuGetEnginePartnerList__(pSubdevice, pPartnerListParams);
}

NV_STATUS subdeviceCtrlCmdGpuQueryMode_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_QUERY_MODE_PARAMS *pQueryMode);

static inline NV_STATUS subdeviceCtrlCmdGpuQueryMode_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_QUERY_MODE_PARAMS *pQueryMode) {
    return pSubdevice->__subdeviceCtrlCmdGpuQueryMode__(pSubdevice, pQueryMode);
}

NV_STATUS subdeviceCtrlCmdGpuGetOEMBoardInfo_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_OEM_BOARD_INFO_PARAMS *pBoardInfo);

static inline NV_STATUS subdeviceCtrlCmdGpuGetOEMBoardInfo_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_OEM_BOARD_INFO_PARAMS *pBoardInfo) {
    return pSubdevice->__subdeviceCtrlCmdGpuGetOEMBoardInfo__(pSubdevice, pBoardInfo);
}

NV_STATUS subdeviceCtrlCmdGpuGetOEMInfo_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_OEM_INFO_PARAMS *pOemInfo);

static inline NV_STATUS subdeviceCtrlCmdGpuGetOEMInfo_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_OEM_INFO_PARAMS *pOemInfo) {
    return pSubdevice->__subdeviceCtrlCmdGpuGetOEMInfo__(pSubdevice, pOemInfo);
}

NV_STATUS subdeviceCtrlCmdGpuHandleGpuSR_IMPL(struct Subdevice *pSubdevice);

static inline NV_STATUS subdeviceCtrlCmdGpuHandleGpuSR_DISPATCH(struct Subdevice *pSubdevice) {
    return pSubdevice->__subdeviceCtrlCmdGpuHandleGpuSR__(pSubdevice);
}

NV_STATUS subdeviceCtrlCmdGpuInitializeCtx_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_INITIALIZE_CTX_PARAMS *pInitializeCtxParams);

static inline NV_STATUS subdeviceCtrlCmdGpuInitializeCtx_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_INITIALIZE_CTX_PARAMS *pInitializeCtxParams) {
    return pSubdevice->__subdeviceCtrlCmdGpuInitializeCtx__(pSubdevice, pInitializeCtxParams);
}

NV_STATUS subdeviceCtrlCmdGpuPromoteCtx_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_PROMOTE_CTX_PARAMS *pPromoteCtxParams);

static inline NV_STATUS subdeviceCtrlCmdGpuPromoteCtx_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_PROMOTE_CTX_PARAMS *pPromoteCtxParams) {
    return pSubdevice->__subdeviceCtrlCmdGpuPromoteCtx__(pSubdevice, pPromoteCtxParams);
}

NV_STATUS subdeviceCtrlCmdGpuEvictCtx_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_EVICT_CTX_PARAMS *pEvictCtxParams);

static inline NV_STATUS subdeviceCtrlCmdGpuEvictCtx_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_EVICT_CTX_PARAMS *pEvictCtxParams) {
    return pSubdevice->__subdeviceCtrlCmdGpuEvictCtx__(pSubdevice, pEvictCtxParams);
}

NV_STATUS subdeviceCtrlCmdGpuGetId_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_ID_PARAMS *pIdParams);

static inline NV_STATUS subdeviceCtrlCmdGpuGetId_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_ID_PARAMS *pIdParams) {
    return pSubdevice->__subdeviceCtrlCmdGpuGetId__(pSubdevice, pIdParams);
}

NV_STATUS subdeviceCtrlCmdGpuGetGidInfo_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_GID_INFO_PARAMS *pGidInfoParams);

static inline NV_STATUS subdeviceCtrlCmdGpuGetGidInfo_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_GID_INFO_PARAMS *pGidInfoParams) {
    return pSubdevice->__subdeviceCtrlCmdGpuGetGidInfo__(pSubdevice, pGidInfoParams);
}

NV_STATUS subdeviceCtrlCmdGpuGetPids_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_PIDS_PARAMS *pGetPidsParams);

static inline NV_STATUS subdeviceCtrlCmdGpuGetPids_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_PIDS_PARAMS *pGetPidsParams) {
    return pSubdevice->__subdeviceCtrlCmdGpuGetPids__(pSubdevice, pGetPidsParams);
}

NV_STATUS subdeviceCtrlCmdGpuGetPidInfo_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_PID_INFO_PARAMS *pGetPidInfoParams);

static inline NV_STATUS subdeviceCtrlCmdGpuGetPidInfo_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_PID_INFO_PARAMS *pGetPidInfoParams) {
    return pSubdevice->__subdeviceCtrlCmdGpuGetPidInfo__(pSubdevice, pGetPidInfoParams);
}

NV_STATUS subdeviceCtrlCmdGpuQueryFunctionStatus_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_CMD_GPU_QUERY_FUNCTION_STATUS_PARAMS *pParams);

static inline NV_STATUS subdeviceCtrlCmdGpuQueryFunctionStatus_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_CMD_GPU_QUERY_FUNCTION_STATUS_PARAMS *pParams) {
    return pSubdevice->__subdeviceCtrlCmdGpuQueryFunctionStatus__(pSubdevice, pParams);
}

NV_STATUS subdeviceCtrlCmdGpuGetMaxSupportedPageSize_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_MAX_SUPPORTED_PAGE_SIZE_PARAMS *pParams);

static inline NV_STATUS subdeviceCtrlCmdGpuGetMaxSupportedPageSize_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_MAX_SUPPORTED_PAGE_SIZE_PARAMS *pParams) {
    return pSubdevice->__subdeviceCtrlCmdGpuGetMaxSupportedPageSize__(pSubdevice, pParams);
}

NV_STATUS subdeviceCtrlCmdValidateMemMapRequest_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_VALIDATE_MEM_MAP_REQUEST_PARAMS *pParams);

static inline NV_STATUS subdeviceCtrlCmdValidateMemMapRequest_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_VALIDATE_MEM_MAP_REQUEST_PARAMS *pParams) {
    return pSubdevice->__subdeviceCtrlCmdValidateMemMapRequest__(pSubdevice, pParams);
}

NV_STATUS subdeviceCtrlCmdGpuGetEngineLoadTimes_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_ENGINE_LOAD_TIMES_PARAMS *pParams);

static inline NV_STATUS subdeviceCtrlCmdGpuGetEngineLoadTimes_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_ENGINE_LOAD_TIMES_PARAMS *pParams) {
    return pSubdevice->__subdeviceCtrlCmdGpuGetEngineLoadTimes__(pSubdevice, pParams);
}

NV_STATUS subdeviceCtrlCmdEventSetTrigger_IMPL(struct Subdevice *pSubdevice);

static inline NV_STATUS subdeviceCtrlCmdEventSetTrigger_DISPATCH(struct Subdevice *pSubdevice) {
    return pSubdevice->__subdeviceCtrlCmdEventSetTrigger__(pSubdevice);
}

NV_STATUS subdeviceCtrlCmdEventSetTriggerFifo_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_EVENT_SET_TRIGGER_FIFO_PARAMS *pTriggerFifoParams);

static inline NV_STATUS subdeviceCtrlCmdEventSetTriggerFifo_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_EVENT_SET_TRIGGER_FIFO_PARAMS *pTriggerFifoParams) {
    return pSubdevice->__subdeviceCtrlCmdEventSetTriggerFifo__(pSubdevice, pTriggerFifoParams);
}

NV_STATUS subdeviceCtrlCmdEventSetNotification_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_EVENT_SET_NOTIFICATION_PARAMS *pSetEventParams);

static inline NV_STATUS subdeviceCtrlCmdEventSetNotification_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_EVENT_SET_NOTIFICATION_PARAMS *pSetEventParams) {
    return pSubdevice->__subdeviceCtrlCmdEventSetNotification__(pSubdevice, pSetEventParams);
}

NV_STATUS subdeviceCtrlCmdEventSetMemoryNotifies_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_EVENT_SET_MEMORY_NOTIFIES_PARAMS *pSetMemoryNotifiesParams);

static inline NV_STATUS subdeviceCtrlCmdEventSetMemoryNotifies_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_EVENT_SET_MEMORY_NOTIFIES_PARAMS *pSetMemoryNotifiesParams) {
    return pSubdevice->__subdeviceCtrlCmdEventSetMemoryNotifies__(pSubdevice, pSetMemoryNotifiesParams);
}

NV_STATUS subdeviceCtrlCmdEventSetSemaphoreMemory_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_EVENT_SET_SEMAPHORE_MEMORY_PARAMS *pSetSemMemoryParams);

static inline NV_STATUS subdeviceCtrlCmdEventSetSemaphoreMemory_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_EVENT_SET_SEMAPHORE_MEMORY_PARAMS *pSetSemMemoryParams) {
    return pSubdevice->__subdeviceCtrlCmdEventSetSemaphoreMemory__(pSubdevice, pSetSemMemoryParams);
}

NV_STATUS subdeviceCtrlCmdEventSetSemaMemValidation_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_EVENT_SET_SEMA_MEM_VALIDATION_PARAMS *pSetSemaMemValidationParams);

static inline NV_STATUS subdeviceCtrlCmdEventSetSemaMemValidation_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_EVENT_SET_SEMA_MEM_VALIDATION_PARAMS *pSetSemaMemValidationParams) {
    return pSubdevice->__subdeviceCtrlCmdEventSetSemaMemValidation__(pSubdevice, pSetSemaMemValidationParams);
}

NV_STATUS subdeviceCtrlCmdTimerCancel_IMPL(struct Subdevice *pSubdevice);

static inline NV_STATUS subdeviceCtrlCmdTimerCancel_DISPATCH(struct Subdevice *pSubdevice) {
    return pSubdevice->__subdeviceCtrlCmdTimerCancel__(pSubdevice);
}

NV_STATUS subdeviceCtrlCmdTimerSchedule_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_CMD_TIMER_SCHEDULE_PARAMS *pParams);

static inline NV_STATUS subdeviceCtrlCmdTimerSchedule_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_CMD_TIMER_SCHEDULE_PARAMS *pParams) {
    return pSubdevice->__subdeviceCtrlCmdTimerSchedule__(pSubdevice, pParams);
}

NV_STATUS subdeviceCtrlCmdTimerGetTime_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_TIMER_GET_TIME_PARAMS *pParams);

static inline NV_STATUS subdeviceCtrlCmdTimerGetTime_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_TIMER_GET_TIME_PARAMS *pParams) {
    return pSubdevice->__subdeviceCtrlCmdTimerGetTime__(pSubdevice, pParams);
}

NV_STATUS subdeviceCtrlCmdTimerGetRegisterOffset_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_TIMER_GET_REGISTER_OFFSET_PARAMS *pTimerRegOffsetParams);

static inline NV_STATUS subdeviceCtrlCmdTimerGetRegisterOffset_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_TIMER_GET_REGISTER_OFFSET_PARAMS *pTimerRegOffsetParams) {
    return pSubdevice->__subdeviceCtrlCmdTimerGetRegisterOffset__(pSubdevice, pTimerRegOffsetParams);
}

NV_STATUS subdeviceCtrlCmdTimerGetGpuCpuTimeCorrelationInfo_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_TIMER_GET_GPU_CPU_TIME_CORRELATION_INFO_PARAMS *pParams);

static inline NV_STATUS subdeviceCtrlCmdTimerGetGpuCpuTimeCorrelationInfo_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_TIMER_GET_GPU_CPU_TIME_CORRELATION_INFO_PARAMS *pParams) {
    return pSubdevice->__subdeviceCtrlCmdTimerGetGpuCpuTimeCorrelationInfo__(pSubdevice, pParams);
}

NV_STATUS subdeviceCtrlCmdEccGetClientExposedCounters_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_ECC_GET_CLIENT_EXPOSED_COUNTERS_PARAMS *pParams);

static inline NV_STATUS subdeviceCtrlCmdEccGetClientExposedCounters_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_ECC_GET_CLIENT_EXPOSED_COUNTERS_PARAMS *pParams) {
    return pSubdevice->__subdeviceCtrlCmdEccGetClientExposedCounters__(pSubdevice, pParams);
}

NV_STATUS subdeviceCtrlCmdGspGetFeatures_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_GSP_GET_FEATURES_PARAMS *pGspFeaturesParams);

static inline NV_STATUS subdeviceCtrlCmdGspGetFeatures_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_GSP_GET_FEATURES_PARAMS *pGspFeaturesParams) {
    return pSubdevice->__subdeviceCtrlCmdGspGetFeatures__(pSubdevice, pGspFeaturesParams);
}

NV_STATUS subdeviceCtrlCmdOsUnixGc6BlockerRefCnt_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_OS_UNIX_GC6_BLOCKER_REFCNT_PARAMS *pParams);

static inline NV_STATUS subdeviceCtrlCmdOsUnixGc6BlockerRefCnt_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_OS_UNIX_GC6_BLOCKER_REFCNT_PARAMS *pParams) {
    return pSubdevice->__subdeviceCtrlCmdOsUnixGc6BlockerRefCnt__(pSubdevice, pParams);
}

NV_STATUS subdeviceCtrlCmdOsUnixAllowDisallowGcoff_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_OS_UNIX_ALLOW_DISALLOW_GCOFF_PARAMS *pParams);

static inline NV_STATUS subdeviceCtrlCmdOsUnixAllowDisallowGcoff_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_OS_UNIX_ALLOW_DISALLOW_GCOFF_PARAMS *pParams) {
    return pSubdevice->__subdeviceCtrlCmdOsUnixAllowDisallowGcoff__(pSubdevice, pParams);
}

NV_STATUS subdeviceCtrlCmdOsUnixAudioDynamicPower_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_OS_UNIX_AUDIO_DYNAMIC_POWER_PARAMS *pParams);

static inline NV_STATUS subdeviceCtrlCmdOsUnixAudioDynamicPower_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_OS_UNIX_AUDIO_DYNAMIC_POWER_PARAMS *pParams) {
    return pSubdevice->__subdeviceCtrlCmdOsUnixAudioDynamicPower__(pSubdevice, pParams);
}

NV_STATUS subdeviceCtrlCmdDisplayGetIpVersion_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_DISPLAY_GET_IP_VERSION_PARAMS *pParams);

static inline NV_STATUS subdeviceCtrlCmdDisplayGetIpVersion_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_DISPLAY_GET_IP_VERSION_PARAMS *pParams) {
    return pSubdevice->__subdeviceCtrlCmdDisplayGetIpVersion__(pSubdevice, pParams);
}

NV_STATUS subdeviceCtrlCmdDisplayGetStaticInfo_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_DISPLAY_GET_STATIC_INFO_PARAMS *pParams);

static inline NV_STATUS subdeviceCtrlCmdDisplayGetStaticInfo_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_DISPLAY_GET_STATIC_INFO_PARAMS *pParams) {
    return pSubdevice->__subdeviceCtrlCmdDisplayGetStaticInfo__(pSubdevice, pParams);
}

NV_STATUS subdeviceCtrlCmdDisplaySetChannelPushbuffer_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_DISPLAY_CHANNEL_PUSHBUFFER_PARAMS *pParams);

static inline NV_STATUS subdeviceCtrlCmdDisplaySetChannelPushbuffer_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_DISPLAY_CHANNEL_PUSHBUFFER_PARAMS *pParams) {
    return pSubdevice->__subdeviceCtrlCmdDisplaySetChannelPushbuffer__(pSubdevice, pParams);
}

NV_STATUS subdeviceCtrlCmdDisplayWriteInstMem_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_DISPLAY_WRITE_INST_MEM_PARAMS *pParams);

static inline NV_STATUS subdeviceCtrlCmdDisplayWriteInstMem_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_DISPLAY_WRITE_INST_MEM_PARAMS *pParams) {
    return pSubdevice->__subdeviceCtrlCmdDisplayWriteInstMem__(pSubdevice, pParams);
}

NV_STATUS subdeviceCtrlCmdDisplaySetupRgLineIntr_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_DISPLAY_SETUP_RG_LINE_INTR_PARAMS *pParams);

static inline NV_STATUS subdeviceCtrlCmdDisplaySetupRgLineIntr_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_DISPLAY_SETUP_RG_LINE_INTR_PARAMS *pParams) {
    return pSubdevice->__subdeviceCtrlCmdDisplaySetupRgLineIntr__(pSubdevice, pParams);
}

NV_STATUS subdeviceCtrlCmdDisplaySetImportedImpData_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_DISPLAY_SET_IMP_INIT_INFO_PARAMS *pParams);

static inline NV_STATUS subdeviceCtrlCmdDisplaySetImportedImpData_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_DISPLAY_SET_IMP_INIT_INFO_PARAMS *pParams) {
    return pSubdevice->__subdeviceCtrlCmdDisplaySetImportedImpData__(pSubdevice, pParams);
}

NV_STATUS subdeviceCtrlCmdDisplayGetDisplayMask_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_DISPLAY_GET_ACTIVE_DISPLAY_DEVICES_PARAMS *pParams);

static inline NV_STATUS subdeviceCtrlCmdDisplayGetDisplayMask_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_DISPLAY_GET_ACTIVE_DISPLAY_DEVICES_PARAMS *pParams) {
    return pSubdevice->__subdeviceCtrlCmdDisplayGetDisplayMask__(pSubdevice, pParams);
}

NV_STATUS subdeviceCtrlCmdInternalGetChipInfo_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_GPU_GET_CHIP_INFO_PARAMS *pParams);

static inline NV_STATUS subdeviceCtrlCmdInternalGetChipInfo_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_GPU_GET_CHIP_INFO_PARAMS *pParams) {
    return pSubdevice->__subdeviceCtrlCmdInternalGetChipInfo__(pSubdevice, pParams);
}

NV_STATUS subdeviceCtrlCmdInternalGetUserRegisterAccessMap_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_GPU_GET_USER_REGISTER_ACCESS_MAP_PARAMS *pParams);

static inline NV_STATUS subdeviceCtrlCmdInternalGetUserRegisterAccessMap_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_GPU_GET_USER_REGISTER_ACCESS_MAP_PARAMS *pParams) {
    return pSubdevice->__subdeviceCtrlCmdInternalGetUserRegisterAccessMap__(pSubdevice, pParams);
}

NV_STATUS subdeviceCtrlCmdInternalGetDeviceInfoTable_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_GET_DEVICE_INFO_TABLE_PARAMS *pParams);

static inline NV_STATUS subdeviceCtrlCmdInternalGetDeviceInfoTable_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_GET_DEVICE_INFO_TABLE_PARAMS *pParams) {
    return pSubdevice->__subdeviceCtrlCmdInternalGetDeviceInfoTable__(pSubdevice, pParams);
}

NV_STATUS subdeviceCtrlCmdInternalGetConstructedFalconInfo_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_GET_CONSTRUCTED_FALCON_INFO_PARAMS *pParams);

static inline NV_STATUS subdeviceCtrlCmdInternalGetConstructedFalconInfo_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_GET_CONSTRUCTED_FALCON_INFO_PARAMS *pParams) {
    return pSubdevice->__subdeviceCtrlCmdInternalGetConstructedFalconInfo__(pSubdevice, pParams);
}

NV_STATUS subdeviceCtrlCmdInternalRecoverAllComputeContexts_IMPL(struct Subdevice *pSubdevice);

static inline NV_STATUS subdeviceCtrlCmdInternalRecoverAllComputeContexts_DISPATCH(struct Subdevice *pSubdevice) {
    return pSubdevice->__subdeviceCtrlCmdInternalRecoverAllComputeContexts__(pSubdevice);
}

NV_STATUS subdeviceCtrlCmdInternalGetSmcMode_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_GPU_GET_SMC_MODE_PARAMS *pParams);

static inline NV_STATUS subdeviceCtrlCmdInternalGetSmcMode_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_GPU_GET_SMC_MODE_PARAMS *pParams) {
    return pSubdevice->__subdeviceCtrlCmdInternalGetSmcMode__(pSubdevice, pParams);
}

NV_STATUS subdeviceCtrlCmdInternalGetPcieP2pCaps_IMPL(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_GET_PCIE_P2P_CAPS_PARAMS *pParams);

static inline NV_STATUS subdeviceCtrlCmdInternalGetPcieP2pCaps_DISPATCH(struct Subdevice *pSubdevice, NV2080_CTRL_INTERNAL_GET_PCIE_P2P_CAPS_PARAMS *pParams) {
    return pSubdevice->__subdeviceCtrlCmdInternalGetPcieP2pCaps__(pSubdevice, pParams);
}

static inline NvBool subdeviceShareCallback_DISPATCH(struct Subdevice *pGpuResource, struct RsClient *pInvokingClient, struct RsResourceRef *pParentRef, RS_SHARE_POLICY *pSharePolicy) {
    return pGpuResource->__subdeviceShareCallback__(pGpuResource, pInvokingClient, pParentRef, pSharePolicy);
}

static inline NV_STATUS subdeviceMapTo_DISPATCH(struct Subdevice *pResource, RS_RES_MAP_TO_PARAMS *pParams) {
    return pResource->__subdeviceMapTo__(pResource, pParams);
}

static inline NV_STATUS subdeviceGetOrAllocNotifShare_DISPATCH(struct Subdevice *pNotifier, NvHandle hNotifierClient, NvHandle hNotifierResource, struct NotifShare **ppNotifShare) {
    return pNotifier->__subdeviceGetOrAllocNotifShare__(pNotifier, hNotifierClient, hNotifierResource, ppNotifShare);
}

static inline NV_STATUS subdeviceCheckMemInterUnmap_DISPATCH(struct Subdevice *pRmResource, NvBool bSubdeviceHandleProvided) {
    return pRmResource->__subdeviceCheckMemInterUnmap__(pRmResource, bSubdeviceHandleProvided);
}

static inline NV_STATUS subdeviceGetMapAddrSpace_DISPATCH(struct Subdevice *pGpuResource, struct CALL_CONTEXT *pCallContext, NvU32 mapFlags, NV_ADDRESS_SPACE *pAddrSpace) {
    return pGpuResource->__subdeviceGetMapAddrSpace__(pGpuResource, pCallContext, mapFlags, pAddrSpace);
}

static inline void subdeviceSetNotificationShare_DISPATCH(struct Subdevice *pNotifier, struct NotifShare *pNotifShare) {
    pNotifier->__subdeviceSetNotificationShare__(pNotifier, pNotifShare);
}

static inline NvU32 subdeviceGetRefCount_DISPATCH(struct Subdevice *pResource) {
    return pResource->__subdeviceGetRefCount__(pResource);
}

static inline void subdeviceAddAdditionalDependants_DISPATCH(struct RsClient *pClient, struct Subdevice *pResource, RsResourceRef *pReference) {
    pResource->__subdeviceAddAdditionalDependants__(pClient, pResource, pReference);
}

static inline NV_STATUS subdeviceControl_Prologue_DISPATCH(struct Subdevice *pResource, struct CALL_CONTEXT *pCallContext, struct RS_RES_CONTROL_PARAMS_INTERNAL *pParams) {
    return pResource->__subdeviceControl_Prologue__(pResource, pCallContext, pParams);
}

static inline NV_STATUS subdeviceGetRegBaseOffsetAndSize_DISPATCH(struct Subdevice *pGpuResource, struct OBJGPU *pGpu, NvU32 *pOffset, NvU32 *pSize) {
    return pGpuResource->__subdeviceGetRegBaseOffsetAndSize__(pGpuResource, pGpu, pOffset, pSize);
}

static inline NV_STATUS subdeviceUnmapFrom_DISPATCH(struct Subdevice *pResource, RS_RES_UNMAP_FROM_PARAMS *pParams) {
    return pResource->__subdeviceUnmapFrom__(pResource, pParams);
}

static inline void subdeviceControl_Epilogue_DISPATCH(struct Subdevice *pResource, struct CALL_CONTEXT *pCallContext, struct RS_RES_CONTROL_PARAMS_INTERNAL *pParams) {
    pResource->__subdeviceControl_Epilogue__(pResource, pCallContext, pParams);
}

static inline NV_STATUS subdeviceControlLookup_DISPATCH(struct Subdevice *pResource, struct RS_RES_CONTROL_PARAMS_INTERNAL *pParams, const struct NVOC_EXPORTED_METHOD_DEF **ppEntry) {
    return pResource->__subdeviceControlLookup__(pResource, pParams, ppEntry);
}

static inline NvHandle subdeviceGetInternalObjectHandle_DISPATCH(struct Subdevice *pGpuResource) {
    return pGpuResource->__subdeviceGetInternalObjectHandle__(pGpuResource);
}

static inline NV_STATUS subdeviceControl_DISPATCH(struct Subdevice *pGpuResource, struct CALL_CONTEXT *pCallContext, struct RS_RES_CONTROL_PARAMS_INTERNAL *pParams) {
    return pGpuResource->__subdeviceControl__(pGpuResource, pCallContext, pParams);
}

static inline NV_STATUS subdeviceUnmap_DISPATCH(struct Subdevice *pGpuResource, struct CALL_CONTEXT *pCallContext, struct RsCpuMapping *pCpuMapping) {
    return pGpuResource->__subdeviceUnmap__(pGpuResource, pCallContext, pCpuMapping);
}

static inline NV_STATUS subdeviceGetMemInterMapParams_DISPATCH(struct Subdevice *pRmResource, RMRES_MEM_INTER_MAP_PARAMS *pParams) {
    return pRmResource->__subdeviceGetMemInterMapParams__(pRmResource, pParams);
}

static inline NV_STATUS subdeviceGetMemoryMappingDescriptor_DISPATCH(struct Subdevice *pRmResource, struct MEMORY_DESCRIPTOR **ppMemDesc) {
    return pRmResource->__subdeviceGetMemoryMappingDescriptor__(pRmResource, ppMemDesc);
}

static inline NV_STATUS subdeviceUnregisterEvent_DISPATCH(struct Subdevice *pNotifier, NvHandle hNotifierClient, NvHandle hNotifierResource, NvHandle hEventClient, NvHandle hEvent) {
    return pNotifier->__subdeviceUnregisterEvent__(pNotifier, hNotifierClient, hNotifierResource, hEventClient, hEvent);
}

static inline NvBool subdeviceCanCopy_DISPATCH(struct Subdevice *pResource) {
    return pResource->__subdeviceCanCopy__(pResource);
}

static inline PEVENTNOTIFICATION *subdeviceGetNotificationListPtr_DISPATCH(struct Subdevice *pNotifier) {
    return pNotifier->__subdeviceGetNotificationListPtr__(pNotifier);
}

static inline struct NotifShare *subdeviceGetNotificationShare_DISPATCH(struct Subdevice *pNotifier) {
    return pNotifier->__subdeviceGetNotificationShare__(pNotifier);
}

static inline NV_STATUS subdeviceMap_DISPATCH(struct Subdevice *pGpuResource, struct CALL_CONTEXT *pCallContext, struct RS_CPU_MAP_PARAMS *pParams, struct RsCpuMapping *pCpuMapping) {
    return pGpuResource->__subdeviceMap__(pGpuResource, pCallContext, pParams, pCpuMapping);
}

static inline NvBool subdeviceAccessCallback_DISPATCH(struct Subdevice *pResource, struct RsClient *pInvokingClient, void *pAllocParams, RsAccessRight accessRight) {
    return pResource->__subdeviceAccessCallback__(pResource, pInvokingClient, pAllocParams, accessRight);
}

static inline NV_STATUS subdeviceSetPerfmonReservation(struct Subdevice *pSubdevice, NvBool bReservation, NvBool bClientHandlesGrGating, NvBool bRmHandlesIdleSlow) {
    return NV_OK;
}

static inline NV_STATUS subdeviceResetTGP(struct Subdevice *pSubdevice) {
    return NV_OK;
}

static inline NV_STATUS subdeviceReleaseVideoStreams(struct Subdevice *pSubdevice) {
    return NV_OK;
}

static inline void subdeviceRestoreLockedClock(struct Subdevice *pSubdevice, struct CALL_CONTEXT *pCallContext) {
    return;
}

static inline void subdeviceReleaseNvlinkErrorInjectionMode(struct Subdevice *pSubdevice, struct CALL_CONTEXT *pCallContext) {
    return;
}

static inline void subdeviceRestoreGrTickFreq(struct Subdevice *pSubdevice, struct CALL_CONTEXT *pCallContext) {
    return;
}

static inline void subdeviceRestoreWatchdog(struct Subdevice *pSubdevice) {
    return;
}

NV_STATUS subdeviceConstruct_IMPL(struct Subdevice *arg_pResource, struct CALL_CONTEXT *arg_pCallContext, struct RS_RES_ALLOC_PARAMS_INTERNAL *arg_pParams);
#define __nvoc_subdeviceConstruct(arg_pResource, arg_pCallContext, arg_pParams) subdeviceConstruct_IMPL(arg_pResource, arg_pCallContext, arg_pParams)
void subdeviceDestruct_IMPL(struct Subdevice *pResource);
#define __nvoc_subdeviceDestruct(pResource) subdeviceDestruct_IMPL(pResource)
void subdeviceUnsetGpuDebugMode_IMPL(struct Subdevice *pSubdevice);
#ifdef __nvoc_subdevice_h_disabled
static inline void subdeviceUnsetGpuDebugMode(struct Subdevice *pSubdevice) {
    NV_ASSERT_FAILED_PRECOMP("Subdevice was disabled!");
}
#else //__nvoc_subdevice_h_disabled
#define subdeviceUnsetGpuDebugMode(pSubdevice) subdeviceUnsetGpuDebugMode_IMPL(pSubdevice)
#endif //__nvoc_subdevice_h_disabled

void subdeviceReleaseComputeModeReservation_IMPL(struct Subdevice *pSubdevice, struct CALL_CONTEXT *pCallContext);
#ifdef __nvoc_subdevice_h_disabled
static inline void subdeviceReleaseComputeModeReservation(struct Subdevice *pSubdevice, struct CALL_CONTEXT *pCallContext) {
    NV_ASSERT_FAILED_PRECOMP("Subdevice was disabled!");
}
#else //__nvoc_subdevice_h_disabled
#define subdeviceReleaseComputeModeReservation(pSubdevice, pCallContext) subdeviceReleaseComputeModeReservation_IMPL(pSubdevice, pCallContext)
#endif //__nvoc_subdevice_h_disabled

NV_STATUS subdeviceGetByHandle_IMPL(struct RsClient *pClient, NvHandle hSubdevice, struct Subdevice **ppSubdevice);
#define subdeviceGetByHandle(pClient, hSubdevice, ppSubdevice) subdeviceGetByHandle_IMPL(pClient, hSubdevice, ppSubdevice)
NV_STATUS subdeviceGetByGpu_IMPL(struct RsClient *pClient, struct OBJGPU *pGpu, struct Subdevice **ppSubdevice);
#define subdeviceGetByGpu(pClient, pGpu, ppSubdevice) subdeviceGetByGpu_IMPL(pClient, pGpu, ppSubdevice)
NV_STATUS subdeviceGetByInstance_IMPL(struct RsClient *pClient, NvHandle hDevice, NvU32 subDeviceInst, struct Subdevice **ppSubdevice);
#define subdeviceGetByInstance(pClient, hDevice, subDeviceInst, ppSubdevice) subdeviceGetByInstance_IMPL(pClient, hDevice, subDeviceInst, ppSubdevice)
#undef PRIVATE_FIELD


// ****************************************************************************
//                            Deprecated Definitions
// ****************************************************************************

/**
 * WARNING: This function is deprecated! Please use subdeviceGetByGpu and
 * GPU_RES_SET_THREAD_BC_STATE (if needed to set thread UC state for SLI)
 */
struct Subdevice *CliGetSubDeviceInfoFromGpu(NvHandle, struct OBJGPU*);

/**
 * WARNING: This function is deprecated! Please use subdeviceGetByGpu and
 * RES_GET_HANDLE
 */
NV_STATUS CliGetSubDeviceHandleFromGpu(NvHandle, struct OBJGPU*, NvHandle *);

/**
 * WARNING: This function is deprecated and use is *strongly* discouraged
 * (especially for new code!)
 *
 * From the function name (CliSetSubDeviceContext) it appears as a simple
 * accessor but violates expectations by modifying the SLI BC threadstate (calls
 * to GPU_RES_SET_THREAD_BC_STATE). This can be dangerous if not carefully
 * managed by the caller.
 *
 * Instead of using this routine, please use subdeviceGetByHandle then call
 * GPU_RES_GET_GPU, RES_GET_HANDLE, GPU_RES_SET_THREAD_BC_STATE as needed.
 *
 * Note that GPU_RES_GET_GPU supports returning a pGpu for both pDevice,
 * pSubdevice, the base pResource type, and any resource that inherits from
 * GpuResource. That is, instead of using CliSetGpuContext or
 * CliSetSubDeviceContext, please use following pattern to look up the pGpu:
 *
 * OBJGPU *pGpu = GPU_RES_GET_GPU(pResource or pResourceRef->pResource)
 *
 * To set the threadstate, please use:
 *
 * GPU_RES_SET_THREAD_BC_STATE(pResource or pResourceRef->pResource);
 */
NV_STATUS CliSetSubDeviceContext(NvHandle hClient, NvHandle hSubdevice, NvHandle *phDevice,
                                 struct OBJGPU **ppGpu);

#endif

#ifdef __cplusplus
} // extern "C"
#endif
#endif // _G_SUBDEVICE_NVOC_H_
