#define NVOC_DEVICE_H_PRIVATE_ACCESS_ALLOWED
#include "nvoc/runtime.h"
#include "nvoc/rtti.h"
#include "nvtypes.h"
#include "nvport/nvport.h"
#include "nvport/inline/util_valist.h"
#include "utils/nvassert.h"
#include "g_device_nvoc.h"

#ifdef DEBUG
char __nvoc_class_id_uniqueness_check_0xe0ac20 = 1;
#endif

extern const struct NVOC_CLASS_DEF __nvoc_class_def_Device;

extern const struct NVOC_CLASS_DEF __nvoc_class_def_Object;

extern const struct NVOC_CLASS_DEF __nvoc_class_def_RsResource;

extern const struct NVOC_CLASS_DEF __nvoc_class_def_RmResourceCommon;

extern const struct NVOC_CLASS_DEF __nvoc_class_def_RmResource;

extern const struct NVOC_CLASS_DEF __nvoc_class_def_GpuResource;

void __nvoc_init_Device(Device*);
void __nvoc_init_funcTable_Device(Device*);
NV_STATUS __nvoc_ctor_Device(Device*, struct CALL_CONTEXT * arg_pCallContext, struct RS_RES_ALLOC_PARAMS_INTERNAL * arg_pParams);
void __nvoc_init_dataField_Device(Device*);
void __nvoc_dtor_Device(Device*);
extern const struct NVOC_EXPORT_INFO __nvoc_export_info_Device;

static const struct NVOC_RTTI __nvoc_rtti_Device_Device = {
    /*pClassDef=*/          &__nvoc_class_def_Device,
    /*dtor=*/               (NVOC_DYNAMIC_DTOR) &__nvoc_dtor_Device,
    /*offset=*/             0,
};

static const struct NVOC_RTTI __nvoc_rtti_Device_Object = {
    /*pClassDef=*/          &__nvoc_class_def_Object,
    /*dtor=*/               &__nvoc_destructFromBase,
    /*offset=*/             NV_OFFSETOF(Device, __nvoc_base_GpuResource.__nvoc_base_RmResource.__nvoc_base_RsResource.__nvoc_base_Object),
};

static const struct NVOC_RTTI __nvoc_rtti_Device_RsResource = {
    /*pClassDef=*/          &__nvoc_class_def_RsResource,
    /*dtor=*/               &__nvoc_destructFromBase,
    /*offset=*/             NV_OFFSETOF(Device, __nvoc_base_GpuResource.__nvoc_base_RmResource.__nvoc_base_RsResource),
};

static const struct NVOC_RTTI __nvoc_rtti_Device_RmResourceCommon = {
    /*pClassDef=*/          &__nvoc_class_def_RmResourceCommon,
    /*dtor=*/               &__nvoc_destructFromBase,
    /*offset=*/             NV_OFFSETOF(Device, __nvoc_base_GpuResource.__nvoc_base_RmResource.__nvoc_base_RmResourceCommon),
};

static const struct NVOC_RTTI __nvoc_rtti_Device_RmResource = {
    /*pClassDef=*/          &__nvoc_class_def_RmResource,
    /*dtor=*/               &__nvoc_destructFromBase,
    /*offset=*/             NV_OFFSETOF(Device, __nvoc_base_GpuResource.__nvoc_base_RmResource),
};

static const struct NVOC_RTTI __nvoc_rtti_Device_GpuResource = {
    /*pClassDef=*/          &__nvoc_class_def_GpuResource,
    /*dtor=*/               &__nvoc_destructFromBase,
    /*offset=*/             NV_OFFSETOF(Device, __nvoc_base_GpuResource),
};

static const struct NVOC_CASTINFO __nvoc_castinfo_Device = {
    /*numRelatives=*/       6,
    /*relatives=*/ {
        &__nvoc_rtti_Device_Device,
        &__nvoc_rtti_Device_GpuResource,
        &__nvoc_rtti_Device_RmResource,
        &__nvoc_rtti_Device_RmResourceCommon,
        &__nvoc_rtti_Device_RsResource,
        &__nvoc_rtti_Device_Object,
    },
};

const struct NVOC_CLASS_DEF __nvoc_class_def_Device = 
{
    /*classInfo=*/ {
        /*size=*/               sizeof(Device),
        /*classId=*/            classId(Device),
        /*providerId=*/         &__nvoc_rtti_provider,
#if NV_PRINTF_STRINGS_ALLOWED
        /*name=*/               "Device",
#endif
    },
    /*objCreatefn=*/        (NVOC_DYNAMIC_OBJ_CREATE) &__nvoc_objCreateDynamic_Device,
    /*pCastInfo=*/          &__nvoc_castinfo_Device,
    /*pExportInfo=*/        &__nvoc_export_info_Device
};

static NV_STATUS __nvoc_thunk_Device_gpuresControl(struct GpuResource *pResource, struct CALL_CONTEXT *pCallContext, struct RS_RES_CONTROL_PARAMS_INTERNAL *pParams) {
    return deviceControl((struct Device *)(((unsigned char *)pResource) - __nvoc_rtti_Device_GpuResource.offset), pCallContext, pParams);
}

static NV_STATUS __nvoc_thunk_Device_gpuresInternalControlForward(struct GpuResource *pDevice, NvU32 command, void *pParams, NvU32 size) {
    return deviceInternalControlForward((struct Device *)(((unsigned char *)pDevice) - __nvoc_rtti_Device_GpuResource.offset), command, pParams, size);
}

static NvBool __nvoc_thunk_GpuResource_deviceShareCallback(struct Device *pGpuResource, struct RsClient *pInvokingClient, struct RsResourceRef *pParentRef, RS_SHARE_POLICY *pSharePolicy) {
    return gpuresShareCallback((struct GpuResource *)(((unsigned char *)pGpuResource) + __nvoc_rtti_Device_GpuResource.offset), pInvokingClient, pParentRef, pSharePolicy);
}

static NV_STATUS __nvoc_thunk_GpuResource_deviceUnmap(struct Device *pGpuResource, struct CALL_CONTEXT *pCallContext, struct RsCpuMapping *pCpuMapping) {
    return gpuresUnmap((struct GpuResource *)(((unsigned char *)pGpuResource) + __nvoc_rtti_Device_GpuResource.offset), pCallContext, pCpuMapping);
}

static NV_STATUS __nvoc_thunk_RmResource_deviceGetMemInterMapParams(struct Device *pRmResource, RMRES_MEM_INTER_MAP_PARAMS *pParams) {
    return rmresGetMemInterMapParams((struct RmResource *)(((unsigned char *)pRmResource) + __nvoc_rtti_Device_RmResource.offset), pParams);
}

static NV_STATUS __nvoc_thunk_RmResource_deviceGetMemoryMappingDescriptor(struct Device *pRmResource, struct MEMORY_DESCRIPTOR **ppMemDesc) {
    return rmresGetMemoryMappingDescriptor((struct RmResource *)(((unsigned char *)pRmResource) + __nvoc_rtti_Device_RmResource.offset), ppMemDesc);
}

static NV_STATUS __nvoc_thunk_GpuResource_deviceGetMapAddrSpace(struct Device *pGpuResource, struct CALL_CONTEXT *pCallContext, NvU32 mapFlags, NV_ADDRESS_SPACE *pAddrSpace) {
    return gpuresGetMapAddrSpace((struct GpuResource *)(((unsigned char *)pGpuResource) + __nvoc_rtti_Device_GpuResource.offset), pCallContext, mapFlags, pAddrSpace);
}

static NvHandle __nvoc_thunk_GpuResource_deviceGetInternalObjectHandle(struct Device *pGpuResource) {
    return gpuresGetInternalObjectHandle((struct GpuResource *)(((unsigned char *)pGpuResource) + __nvoc_rtti_Device_GpuResource.offset));
}

static NV_STATUS __nvoc_thunk_RsResource_deviceControlFilter(struct Device *pResource, struct CALL_CONTEXT *pCallContext, struct RS_RES_CONTROL_PARAMS_INTERNAL *pParams) {
    return resControlFilter((struct RsResource *)(((unsigned char *)pResource) + __nvoc_rtti_Device_RsResource.offset), pCallContext, pParams);
}

static void __nvoc_thunk_RsResource_deviceAddAdditionalDependants(struct RsClient *pClient, struct Device *pResource, RsResourceRef *pReference) {
    resAddAdditionalDependants(pClient, (struct RsResource *)(((unsigned char *)pResource) + __nvoc_rtti_Device_RsResource.offset), pReference);
}

static NvU32 __nvoc_thunk_RsResource_deviceGetRefCount(struct Device *pResource) {
    return resGetRefCount((struct RsResource *)(((unsigned char *)pResource) + __nvoc_rtti_Device_RsResource.offset));
}

static NV_STATUS __nvoc_thunk_RmResource_deviceCheckMemInterUnmap(struct Device *pRmResource, NvBool bSubdeviceHandleProvided) {
    return rmresCheckMemInterUnmap((struct RmResource *)(((unsigned char *)pRmResource) + __nvoc_rtti_Device_RmResource.offset), bSubdeviceHandleProvided);
}

static NV_STATUS __nvoc_thunk_RsResource_deviceMapTo(struct Device *pResource, RS_RES_MAP_TO_PARAMS *pParams) {
    return resMapTo((struct RsResource *)(((unsigned char *)pResource) + __nvoc_rtti_Device_RsResource.offset), pParams);
}

static NV_STATUS __nvoc_thunk_RmResource_deviceControl_Prologue(struct Device *pResource, struct CALL_CONTEXT *pCallContext, struct RS_RES_CONTROL_PARAMS_INTERNAL *pParams) {
    return rmresControl_Prologue((struct RmResource *)(((unsigned char *)pResource) + __nvoc_rtti_Device_RmResource.offset), pCallContext, pParams);
}

static NV_STATUS __nvoc_thunk_GpuResource_deviceGetRegBaseOffsetAndSize(struct Device *pGpuResource, struct OBJGPU *pGpu, NvU32 *pOffset, NvU32 *pSize) {
    return gpuresGetRegBaseOffsetAndSize((struct GpuResource *)(((unsigned char *)pGpuResource) + __nvoc_rtti_Device_GpuResource.offset), pGpu, pOffset, pSize);
}

static NvBool __nvoc_thunk_RsResource_deviceCanCopy(struct Device *pResource) {
    return resCanCopy((struct RsResource *)(((unsigned char *)pResource) + __nvoc_rtti_Device_RsResource.offset));
}

static void __nvoc_thunk_RsResource_devicePreDestruct(struct Device *pResource) {
    resPreDestruct((struct RsResource *)(((unsigned char *)pResource) + __nvoc_rtti_Device_RsResource.offset));
}

static NV_STATUS __nvoc_thunk_RsResource_deviceUnmapFrom(struct Device *pResource, RS_RES_UNMAP_FROM_PARAMS *pParams) {
    return resUnmapFrom((struct RsResource *)(((unsigned char *)pResource) + __nvoc_rtti_Device_RsResource.offset), pParams);
}

static void __nvoc_thunk_RmResource_deviceControl_Epilogue(struct Device *pResource, struct CALL_CONTEXT *pCallContext, struct RS_RES_CONTROL_PARAMS_INTERNAL *pParams) {
    rmresControl_Epilogue((struct RmResource *)(((unsigned char *)pResource) + __nvoc_rtti_Device_RmResource.offset), pCallContext, pParams);
}

static NV_STATUS __nvoc_thunk_RsResource_deviceControlLookup(struct Device *pResource, struct RS_RES_CONTROL_PARAMS_INTERNAL *pParams, const struct NVOC_EXPORTED_METHOD_DEF **ppEntry) {
    return resControlLookup((struct RsResource *)(((unsigned char *)pResource) + __nvoc_rtti_Device_RsResource.offset), pParams, ppEntry);
}

static NV_STATUS __nvoc_thunk_GpuResource_deviceMap(struct Device *pGpuResource, struct CALL_CONTEXT *pCallContext, struct RS_CPU_MAP_PARAMS *pParams, struct RsCpuMapping *pCpuMapping) {
    return gpuresMap((struct GpuResource *)(((unsigned char *)pGpuResource) + __nvoc_rtti_Device_GpuResource.offset), pCallContext, pParams, pCpuMapping);
}

static NvBool __nvoc_thunk_RmResource_deviceAccessCallback(struct Device *pResource, struct RsClient *pInvokingClient, void *pAllocParams, RsAccessRight accessRight) {
    return rmresAccessCallback((struct RmResource *)(((unsigned char *)pResource) + __nvoc_rtti_Device_RmResource.offset), pInvokingClient, pAllocParams, accessRight);
}

#if !defined(NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG)
#define NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(x)      (0)
#endif

static const struct NVOC_EXPORTED_METHOD_DEF __nvoc_exported_method_def_Device[] = 
{
    {               /*  [0] */
#if NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x813u)
        /*pFunc=*/      (void (*)(void)) NULL,
#else
        /*pFunc=*/      (void (*)(void)) deviceCtrlCmdGpuGetClasslist_IMPL,
#endif // NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x813u)
        /*flags=*/      0x813u,
        /*accessRight=*/0x0u,
        /*methodId=*/   0x800201u,
        /*paramSize=*/  sizeof(NV0080_CTRL_GPU_GET_CLASSLIST_PARAMS),
        /*pClassInfo=*/ &(__nvoc_class_def_Device.classInfo),
#if NV_PRINTF_STRINGS_ALLOWED
        /*func=*/       "deviceCtrlCmdGpuGetClasslist"
#endif
    },
    {               /*  [1] */
#if NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x811u)
        /*pFunc=*/      (void (*)(void)) NULL,
#else
        /*pFunc=*/      (void (*)(void)) deviceCtrlCmdGpuGetNumSubdevices_IMPL,
#endif // NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x811u)
        /*flags=*/      0x811u,
        /*accessRight=*/0x0u,
        /*methodId=*/   0x800280u,
        /*paramSize=*/  sizeof(NV0080_CTRL_GPU_GET_NUM_SUBDEVICES_PARAMS),
        /*pClassInfo=*/ &(__nvoc_class_def_Device.classInfo),
#if NV_PRINTF_STRINGS_ALLOWED
        /*func=*/       "deviceCtrlCmdGpuGetNumSubdevices"
#endif
    },
    {               /*  [2] */
#if NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x5u)
        /*pFunc=*/      (void (*)(void)) NULL,
#else
        /*pFunc=*/      (void (*)(void)) deviceCtrlCmdGpuModifyGpuSwStatePersistence_IMPL,
#endif // NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x5u)
        /*flags=*/      0x5u,
        /*accessRight=*/0x0u,
        /*methodId=*/   0x800287u,
        /*paramSize=*/  sizeof(NV0080_CTRL_GPU_MODIFY_SW_STATE_PERSISTENCE_PARAMS),
        /*pClassInfo=*/ &(__nvoc_class_def_Device.classInfo),
#if NV_PRINTF_STRINGS_ALLOWED
        /*func=*/       "deviceCtrlCmdGpuModifyGpuSwStatePersistence"
#endif
    },
    {               /*  [3] */
#if NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x11u)
        /*pFunc=*/      (void (*)(void)) NULL,
#else
        /*pFunc=*/      (void (*)(void)) deviceCtrlCmdGpuQueryGpuSwStatePersistence_IMPL,
#endif // NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x11u)
        /*flags=*/      0x11u,
        /*accessRight=*/0x0u,
        /*methodId=*/   0x800288u,
        /*paramSize=*/  sizeof(NV0080_CTRL_GPU_QUERY_SW_STATE_PERSISTENCE_PARAMS),
        /*pClassInfo=*/ &(__nvoc_class_def_Device.classInfo),
#if NV_PRINTF_STRINGS_ALLOWED
        /*func=*/       "deviceCtrlCmdGpuQueryGpuSwStatePersistence"
#endif
    },
    {               /*  [4] */
#if NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x810u)
        /*pFunc=*/      (void (*)(void)) NULL,
#else
        /*pFunc=*/      (void (*)(void)) deviceCtrlCmdGpuGetVirtualizationMode_IMPL,
#endif // NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x810u)
        /*flags=*/      0x810u,
        /*accessRight=*/0x0u,
        /*methodId=*/   0x800289u,
        /*paramSize=*/  sizeof(NV0080_CTRL_GPU_GET_VIRTUALIZATION_MODE_PARAMS),
        /*pClassInfo=*/ &(__nvoc_class_def_Device.classInfo),
#if NV_PRINTF_STRINGS_ALLOWED
        /*func=*/       "deviceCtrlCmdGpuGetVirtualizationMode"
#endif
    },
    {               /*  [5] */
#if NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x813u)
        /*pFunc=*/      (void (*)(void)) NULL,
#else
        /*pFunc=*/      (void (*)(void)) deviceCtrlCmdGpuGetClasslistV2_IMPL,
#endif // NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x813u)
        /*flags=*/      0x813u,
        /*accessRight=*/0x0u,
        /*methodId=*/   0x800292u,
        /*paramSize=*/  sizeof(NV0080_CTRL_GPU_GET_CLASSLIST_V2_PARAMS),
        /*pClassInfo=*/ &(__nvoc_class_def_Device.classInfo),
#if NV_PRINTF_STRINGS_ALLOWED
        /*func=*/       "deviceCtrlCmdGpuGetClasslistV2"
#endif
    },
    {               /*  [6] */
#if NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x13u)
        /*pFunc=*/      (void (*)(void)) NULL,
#else
        /*pFunc=*/      (void (*)(void)) deviceCtrlCmdGpuGetFindSubDeviceHandle_IMPL,
#endif // NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x13u)
        /*flags=*/      0x13u,
        /*accessRight=*/0x0u,
        /*methodId=*/   0x800293u,
        /*paramSize=*/  sizeof(NV0080_CTRL_GPU_FIND_SUBDEVICE_HANDLE_PARAM),
        /*pClassInfo=*/ &(__nvoc_class_def_Device.classInfo),
#if NV_PRINTF_STRINGS_ALLOWED
        /*func=*/       "deviceCtrlCmdGpuGetFindSubDeviceHandle"
#endif
    },
    {               /*  [7] */
#if NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x211u)
        /*pFunc=*/      (void (*)(void)) NULL,
#else
        /*pFunc=*/      (void (*)(void)) deviceCtrlCmdGpuGetBrandCaps_IMPL,
#endif // NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x211u)
        /*flags=*/      0x211u,
        /*accessRight=*/0x0u,
        /*methodId=*/   0x800294u,
        /*paramSize=*/  sizeof(NV0080_CTRL_GPU_GET_BRAND_CAPS_PARAMS),
        /*pClassInfo=*/ &(__nvoc_class_def_Device.classInfo),
#if NV_PRINTF_STRINGS_ALLOWED
        /*func=*/       "deviceCtrlCmdGpuGetBrandCaps"
#endif
    },
    {               /*  [8] */
#if NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x204u)
        /*pFunc=*/      (void (*)(void)) NULL,
#else
        /*pFunc=*/      (void (*)(void)) deviceCtrlCmdGpuSetVgpuVfBar1Size_IMPL,
#endif // NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x204u)
        /*flags=*/      0x204u,
        /*accessRight=*/0x0u,
        /*methodId=*/   0x800296u,
        /*paramSize=*/  sizeof(NV0080_CTRL_GPU_SET_VGPU_VF_BAR1_SIZE_PARAMS),
        /*pClassInfo=*/ &(__nvoc_class_def_Device.classInfo),
#if NV_PRINTF_STRINGS_ALLOWED
        /*func=*/       "deviceCtrlCmdGpuSetVgpuVfBar1Size"
#endif
    },
    {               /*  [9] */
#if NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x1u)
        /*pFunc=*/      (void (*)(void)) NULL,
#else
        /*pFunc=*/      (void (*)(void)) deviceCtrlCmdOsUnixVTSwitch_IMPL,
#endif // NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x1u)
        /*flags=*/      0x1u,
        /*accessRight=*/0x0u,
        /*methodId=*/   0x801e01u,
        /*paramSize=*/  sizeof(NV0080_CTRL_OS_UNIX_VT_SWITCH_PARAMS),
        /*pClassInfo=*/ &(__nvoc_class_def_Device.classInfo),
#if NV_PRINTF_STRINGS_ALLOWED
        /*func=*/       "deviceCtrlCmdOsUnixVTSwitch"
#endif
    },
    {               /*  [10] */
#if NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x1u)
        /*pFunc=*/      (void (*)(void)) NULL,
#else
        /*pFunc=*/      (void (*)(void)) deviceCtrlCmdOsUnixVTGetFBInfo_IMPL,
#endif // NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x1u)
        /*flags=*/      0x1u,
        /*accessRight=*/0x0u,
        /*methodId=*/   0x801e02u,
        /*paramSize=*/  sizeof(NV0080_CTRL_OS_UNIX_VT_GET_FB_INFO_PARAMS),
        /*pClassInfo=*/ &(__nvoc_class_def_Device.classInfo),
#if NV_PRINTF_STRINGS_ALLOWED
        /*func=*/       "deviceCtrlCmdOsUnixVTGetFBInfo"
#endif
    },

};

const struct NVOC_EXPORT_INFO __nvoc_export_info_Device = 
{
    /*numEntries=*/     11,
    /*pExportEntries=*/ __nvoc_exported_method_def_Device
};

void __nvoc_dtor_GpuResource(GpuResource*);
void __nvoc_dtor_Device(Device *pThis) {
    __nvoc_deviceDestruct(pThis);
    __nvoc_dtor_GpuResource(&pThis->__nvoc_base_GpuResource);
    PORT_UNREFERENCED_VARIABLE(pThis);
}

void __nvoc_init_dataField_Device(Device *pThis) {
    PORT_UNREFERENCED_VARIABLE(pThis);
}

NV_STATUS __nvoc_ctor_GpuResource(GpuResource* , struct CALL_CONTEXT *, struct RS_RES_ALLOC_PARAMS_INTERNAL *);
NV_STATUS __nvoc_ctor_Device(Device *pThis, struct CALL_CONTEXT * arg_pCallContext, struct RS_RES_ALLOC_PARAMS_INTERNAL * arg_pParams) {
    NV_STATUS status = NV_OK;
    status = __nvoc_ctor_GpuResource(&pThis->__nvoc_base_GpuResource, arg_pCallContext, arg_pParams);
    if (status != NV_OK) goto __nvoc_ctor_Device_fail_GpuResource;
    __nvoc_init_dataField_Device(pThis);

    status = __nvoc_deviceConstruct(pThis, arg_pCallContext, arg_pParams);
    if (status != NV_OK) goto __nvoc_ctor_Device_fail__init;
    goto __nvoc_ctor_Device_exit; // Success

__nvoc_ctor_Device_fail__init:
    __nvoc_dtor_GpuResource(&pThis->__nvoc_base_GpuResource);
__nvoc_ctor_Device_fail_GpuResource:
__nvoc_ctor_Device_exit:

    return status;
}

static void __nvoc_init_funcTable_Device_1(Device *pThis) {
    PORT_UNREFERENCED_VARIABLE(pThis);

    pThis->__deviceControl__ = &deviceControl_IMPL;

    pThis->__deviceInternalControlForward__ = &deviceInternalControlForward_IMPL;

#if !NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x813u)
    pThis->__deviceCtrlCmdGpuGetClasslist__ = &deviceCtrlCmdGpuGetClasslist_IMPL;
#endif

#if !NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x813u)
    pThis->__deviceCtrlCmdGpuGetClasslistV2__ = &deviceCtrlCmdGpuGetClasslistV2_IMPL;
#endif

#if !NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x811u)
    pThis->__deviceCtrlCmdGpuGetNumSubdevices__ = &deviceCtrlCmdGpuGetNumSubdevices_IMPL;
#endif

#if !NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x5u)
    pThis->__deviceCtrlCmdGpuModifyGpuSwStatePersistence__ = &deviceCtrlCmdGpuModifyGpuSwStatePersistence_IMPL;
#endif

#if !NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x11u)
    pThis->__deviceCtrlCmdGpuQueryGpuSwStatePersistence__ = &deviceCtrlCmdGpuQueryGpuSwStatePersistence_IMPL;
#endif

#if !NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x810u)
    pThis->__deviceCtrlCmdGpuGetVirtualizationMode__ = &deviceCtrlCmdGpuGetVirtualizationMode_IMPL;
#endif

#if !NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x204u)
    pThis->__deviceCtrlCmdGpuSetVgpuVfBar1Size__ = &deviceCtrlCmdGpuSetVgpuVfBar1Size_IMPL;
#endif

#if !NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x211u)
    pThis->__deviceCtrlCmdGpuGetBrandCaps__ = &deviceCtrlCmdGpuGetBrandCaps_IMPL;
#endif

#if !NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x13u)
    pThis->__deviceCtrlCmdGpuGetFindSubDeviceHandle__ = &deviceCtrlCmdGpuGetFindSubDeviceHandle_IMPL;
#endif

#if !NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x1u)
    pThis->__deviceCtrlCmdOsUnixVTSwitch__ = &deviceCtrlCmdOsUnixVTSwitch_IMPL;
#endif

#if !NVOC_EXPORTED_METHOD_DISABLED_BY_FLAG(0x1u)
    pThis->__deviceCtrlCmdOsUnixVTGetFBInfo__ = &deviceCtrlCmdOsUnixVTGetFBInfo_IMPL;
#endif

    pThis->__nvoc_base_GpuResource.__gpuresControl__ = &__nvoc_thunk_Device_gpuresControl;

    pThis->__nvoc_base_GpuResource.__gpuresInternalControlForward__ = &__nvoc_thunk_Device_gpuresInternalControlForward;

    pThis->__deviceShareCallback__ = &__nvoc_thunk_GpuResource_deviceShareCallback;

    pThis->__deviceUnmap__ = &__nvoc_thunk_GpuResource_deviceUnmap;

    pThis->__deviceGetMemInterMapParams__ = &__nvoc_thunk_RmResource_deviceGetMemInterMapParams;

    pThis->__deviceGetMemoryMappingDescriptor__ = &__nvoc_thunk_RmResource_deviceGetMemoryMappingDescriptor;

    pThis->__deviceGetMapAddrSpace__ = &__nvoc_thunk_GpuResource_deviceGetMapAddrSpace;

    pThis->__deviceGetInternalObjectHandle__ = &__nvoc_thunk_GpuResource_deviceGetInternalObjectHandle;

    pThis->__deviceControlFilter__ = &__nvoc_thunk_RsResource_deviceControlFilter;

    pThis->__deviceAddAdditionalDependants__ = &__nvoc_thunk_RsResource_deviceAddAdditionalDependants;

    pThis->__deviceGetRefCount__ = &__nvoc_thunk_RsResource_deviceGetRefCount;

    pThis->__deviceCheckMemInterUnmap__ = &__nvoc_thunk_RmResource_deviceCheckMemInterUnmap;

    pThis->__deviceMapTo__ = &__nvoc_thunk_RsResource_deviceMapTo;

    pThis->__deviceControl_Prologue__ = &__nvoc_thunk_RmResource_deviceControl_Prologue;

    pThis->__deviceGetRegBaseOffsetAndSize__ = &__nvoc_thunk_GpuResource_deviceGetRegBaseOffsetAndSize;

    pThis->__deviceCanCopy__ = &__nvoc_thunk_RsResource_deviceCanCopy;

    pThis->__devicePreDestruct__ = &__nvoc_thunk_RsResource_devicePreDestruct;

    pThis->__deviceUnmapFrom__ = &__nvoc_thunk_RsResource_deviceUnmapFrom;

    pThis->__deviceControl_Epilogue__ = &__nvoc_thunk_RmResource_deviceControl_Epilogue;

    pThis->__deviceControlLookup__ = &__nvoc_thunk_RsResource_deviceControlLookup;

    pThis->__deviceMap__ = &__nvoc_thunk_GpuResource_deviceMap;

    pThis->__deviceAccessCallback__ = &__nvoc_thunk_RmResource_deviceAccessCallback;
}

void __nvoc_init_funcTable_Device(Device *pThis) {
    __nvoc_init_funcTable_Device_1(pThis);
}

void __nvoc_init_GpuResource(GpuResource*);
void __nvoc_init_Device(Device *pThis) {
    pThis->__nvoc_pbase_Device = pThis;
    pThis->__nvoc_pbase_Object = &pThis->__nvoc_base_GpuResource.__nvoc_base_RmResource.__nvoc_base_RsResource.__nvoc_base_Object;
    pThis->__nvoc_pbase_RsResource = &pThis->__nvoc_base_GpuResource.__nvoc_base_RmResource.__nvoc_base_RsResource;
    pThis->__nvoc_pbase_RmResourceCommon = &pThis->__nvoc_base_GpuResource.__nvoc_base_RmResource.__nvoc_base_RmResourceCommon;
    pThis->__nvoc_pbase_RmResource = &pThis->__nvoc_base_GpuResource.__nvoc_base_RmResource;
    pThis->__nvoc_pbase_GpuResource = &pThis->__nvoc_base_GpuResource;
    __nvoc_init_GpuResource(&pThis->__nvoc_base_GpuResource);
    __nvoc_init_funcTable_Device(pThis);
}

NV_STATUS __nvoc_objCreate_Device(Device **ppThis, Dynamic *pParent, NvU32 createFlags, struct CALL_CONTEXT * arg_pCallContext, struct RS_RES_ALLOC_PARAMS_INTERNAL * arg_pParams) {
    NV_STATUS status;
    Object *pParentObj;
    Device *pThis;

    pThis = portMemAllocNonPaged(sizeof(Device));
    if (pThis == NULL) return NV_ERR_NO_MEMORY;

    portMemSet(pThis, 0, sizeof(Device));

    __nvoc_initRtti(staticCast(pThis, Dynamic), &__nvoc_class_def_Device);

    if (pParent != NULL && !(createFlags & NVOC_OBJ_CREATE_FLAGS_PARENT_HALSPEC_ONLY))
    {
        pParentObj = dynamicCast(pParent, Object);
        objAddChild(pParentObj, &pThis->__nvoc_base_GpuResource.__nvoc_base_RmResource.__nvoc_base_RsResource.__nvoc_base_Object);
    }
    else
    {
        pThis->__nvoc_base_GpuResource.__nvoc_base_RmResource.__nvoc_base_RsResource.__nvoc_base_Object.pParent = NULL;
    }

    __nvoc_init_Device(pThis);
    status = __nvoc_ctor_Device(pThis, arg_pCallContext, arg_pParams);
    if (status != NV_OK) goto __nvoc_objCreate_Device_cleanup;

    *ppThis = pThis;
    return NV_OK;

__nvoc_objCreate_Device_cleanup:
    // do not call destructors here since the constructor already called them
    portMemFree(pThis);
    return status;
}

NV_STATUS __nvoc_objCreateDynamic_Device(Device **ppThis, Dynamic *pParent, NvU32 createFlags, va_list args) {
    NV_STATUS status;
    struct CALL_CONTEXT * arg_pCallContext = va_arg(args, struct CALL_CONTEXT *);
    struct RS_RES_ALLOC_PARAMS_INTERNAL * arg_pParams = va_arg(args, struct RS_RES_ALLOC_PARAMS_INTERNAL *);

    status = __nvoc_objCreate_Device(ppThis, pParent, createFlags, arg_pCallContext, arg_pParams);

    return status;
}

