#define NVOC_DCE_CLIENT_H_PRIVATE_ACCESS_ALLOWED
#include "nvoc/runtime.h"
#include "nvoc/rtti.h"
#include "nvtypes.h"
#include "nvport/nvport.h"
#include "nvport/inline/util_valist.h"
#include "utils/nvassert.h"
#include "g_dce_client_nvoc.h"

#ifdef DEBUG
char __nvoc_class_id_uniqueness_check_0x61649c = 1;
#endif

extern const struct NVOC_CLASS_DEF __nvoc_class_def_OBJDCECLIENTRM;

extern const struct NVOC_CLASS_DEF __nvoc_class_def_Object;

extern const struct NVOC_CLASS_DEF __nvoc_class_def_OBJENGSTATE;

void __nvoc_init_OBJDCECLIENTRM(OBJDCECLIENTRM*);
void __nvoc_init_funcTable_OBJDCECLIENTRM(OBJDCECLIENTRM*);
NV_STATUS __nvoc_ctor_OBJDCECLIENTRM(OBJDCECLIENTRM*);
void __nvoc_init_dataField_OBJDCECLIENTRM(OBJDCECLIENTRM*);
void __nvoc_dtor_OBJDCECLIENTRM(OBJDCECLIENTRM*);
extern const struct NVOC_EXPORT_INFO __nvoc_export_info_OBJDCECLIENTRM;

static const struct NVOC_RTTI __nvoc_rtti_OBJDCECLIENTRM_OBJDCECLIENTRM = {
    /*pClassDef=*/          &__nvoc_class_def_OBJDCECLIENTRM,
    /*dtor=*/               (NVOC_DYNAMIC_DTOR) &__nvoc_dtor_OBJDCECLIENTRM,
    /*offset=*/             0,
};

static const struct NVOC_RTTI __nvoc_rtti_OBJDCECLIENTRM_Object = {
    /*pClassDef=*/          &__nvoc_class_def_Object,
    /*dtor=*/               &__nvoc_destructFromBase,
    /*offset=*/             NV_OFFSETOF(OBJDCECLIENTRM, __nvoc_base_OBJENGSTATE.__nvoc_base_Object),
};

static const struct NVOC_RTTI __nvoc_rtti_OBJDCECLIENTRM_OBJENGSTATE = {
    /*pClassDef=*/          &__nvoc_class_def_OBJENGSTATE,
    /*dtor=*/               &__nvoc_destructFromBase,
    /*offset=*/             NV_OFFSETOF(OBJDCECLIENTRM, __nvoc_base_OBJENGSTATE),
};

static const struct NVOC_CASTINFO __nvoc_castinfo_OBJDCECLIENTRM = {
    /*numRelatives=*/       3,
    /*relatives=*/ {
        &__nvoc_rtti_OBJDCECLIENTRM_OBJDCECLIENTRM,
        &__nvoc_rtti_OBJDCECLIENTRM_OBJENGSTATE,
        &__nvoc_rtti_OBJDCECLIENTRM_Object,
    },
};

const struct NVOC_CLASS_DEF __nvoc_class_def_OBJDCECLIENTRM = 
{
    /*classInfo=*/ {
        /*size=*/               sizeof(OBJDCECLIENTRM),
        /*classId=*/            classId(OBJDCECLIENTRM),
        /*providerId=*/         &__nvoc_rtti_provider,
#if NV_PRINTF_STRINGS_ALLOWED
        /*name=*/               "OBJDCECLIENTRM",
#endif
    },
    /*objCreatefn=*/        (NVOC_DYNAMIC_OBJ_CREATE) &__nvoc_objCreateDynamic_OBJDCECLIENTRM,
    /*pCastInfo=*/          &__nvoc_castinfo_OBJDCECLIENTRM,
    /*pExportInfo=*/        &__nvoc_export_info_OBJDCECLIENTRM
};

static NV_STATUS __nvoc_thunk_OBJDCECLIENTRM_engstateConstructEngine(struct OBJGPU *arg0, struct OBJENGSTATE *arg1, ENGDESCRIPTOR arg2) {
    return dceclientConstructEngine(arg0, (struct OBJDCECLIENTRM *)(((unsigned char *)arg1) - __nvoc_rtti_OBJDCECLIENTRM_OBJENGSTATE.offset), arg2);
}

static void __nvoc_thunk_OBJDCECLIENTRM_engstateStateDestroy(struct OBJGPU *arg0, struct OBJENGSTATE *arg1) {
    dceclientStateDestroy(arg0, (struct OBJDCECLIENTRM *)(((unsigned char *)arg1) - __nvoc_rtti_OBJDCECLIENTRM_OBJENGSTATE.offset));
}

static NV_STATUS __nvoc_thunk_OBJDCECLIENTRM_engstateStateLoad(struct OBJGPU *arg0, struct OBJENGSTATE *arg1, NvU32 arg2) {
    return dceclientStateLoad(arg0, (struct OBJDCECLIENTRM *)(((unsigned char *)arg1) - __nvoc_rtti_OBJDCECLIENTRM_OBJENGSTATE.offset), arg2);
}

static NV_STATUS __nvoc_thunk_OBJDCECLIENTRM_engstateStateUnload(struct OBJGPU *arg0, struct OBJENGSTATE *arg1, NvU32 arg2) {
    return dceclientStateUnload(arg0, (struct OBJDCECLIENTRM *)(((unsigned char *)arg1) - __nvoc_rtti_OBJDCECLIENTRM_OBJENGSTATE.offset), arg2);
}

static NV_STATUS __nvoc_thunk_OBJENGSTATE_dceclientReconcileTunableState(POBJGPU pGpu, struct OBJDCECLIENTRM *pEngstate, void *pTunableState) {
    return engstateReconcileTunableState(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJDCECLIENTRM_OBJENGSTATE.offset), pTunableState);
}

static NV_STATUS __nvoc_thunk_OBJENGSTATE_dceclientStateInitLocked(POBJGPU pGpu, struct OBJDCECLIENTRM *pEngstate) {
    return engstateStateInitLocked(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJDCECLIENTRM_OBJENGSTATE.offset));
}

static NV_STATUS __nvoc_thunk_OBJENGSTATE_dceclientStatePreLoad(POBJGPU pGpu, struct OBJDCECLIENTRM *pEngstate, NvU32 arg0) {
    return engstateStatePreLoad(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJDCECLIENTRM_OBJENGSTATE.offset), arg0);
}

static NV_STATUS __nvoc_thunk_OBJENGSTATE_dceclientStatePostUnload(POBJGPU pGpu, struct OBJDCECLIENTRM *pEngstate, NvU32 arg0) {
    return engstateStatePostUnload(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJDCECLIENTRM_OBJENGSTATE.offset), arg0);
}

static NV_STATUS __nvoc_thunk_OBJENGSTATE_dceclientStatePreUnload(POBJGPU pGpu, struct OBJDCECLIENTRM *pEngstate, NvU32 arg0) {
    return engstateStatePreUnload(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJDCECLIENTRM_OBJENGSTATE.offset), arg0);
}

static NV_STATUS __nvoc_thunk_OBJENGSTATE_dceclientStateInitUnlocked(POBJGPU pGpu, struct OBJDCECLIENTRM *pEngstate) {
    return engstateStateInitUnlocked(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJDCECLIENTRM_OBJENGSTATE.offset));
}

static void __nvoc_thunk_OBJENGSTATE_dceclientInitMissing(POBJGPU pGpu, struct OBJDCECLIENTRM *pEngstate) {
    engstateInitMissing(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJDCECLIENTRM_OBJENGSTATE.offset));
}

static NV_STATUS __nvoc_thunk_OBJENGSTATE_dceclientStatePreInitLocked(POBJGPU pGpu, struct OBJDCECLIENTRM *pEngstate) {
    return engstateStatePreInitLocked(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJDCECLIENTRM_OBJENGSTATE.offset));
}

static NV_STATUS __nvoc_thunk_OBJENGSTATE_dceclientStatePreInitUnlocked(POBJGPU pGpu, struct OBJDCECLIENTRM *pEngstate) {
    return engstateStatePreInitUnlocked(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJDCECLIENTRM_OBJENGSTATE.offset));
}

static NV_STATUS __nvoc_thunk_OBJENGSTATE_dceclientGetTunableState(POBJGPU pGpu, struct OBJDCECLIENTRM *pEngstate, void *pTunableState) {
    return engstateGetTunableState(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJDCECLIENTRM_OBJENGSTATE.offset), pTunableState);
}

static NV_STATUS __nvoc_thunk_OBJENGSTATE_dceclientCompareTunableState(POBJGPU pGpu, struct OBJDCECLIENTRM *pEngstate, void *pTunables1, void *pTunables2) {
    return engstateCompareTunableState(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJDCECLIENTRM_OBJENGSTATE.offset), pTunables1, pTunables2);
}

static void __nvoc_thunk_OBJENGSTATE_dceclientFreeTunableState(POBJGPU pGpu, struct OBJDCECLIENTRM *pEngstate, void *pTunableState) {
    engstateFreeTunableState(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJDCECLIENTRM_OBJENGSTATE.offset), pTunableState);
}

static NV_STATUS __nvoc_thunk_OBJENGSTATE_dceclientStatePostLoad(POBJGPU pGpu, struct OBJDCECLIENTRM *pEngstate, NvU32 arg0) {
    return engstateStatePostLoad(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJDCECLIENTRM_OBJENGSTATE.offset), arg0);
}

static NV_STATUS __nvoc_thunk_OBJENGSTATE_dceclientAllocTunableState(POBJGPU pGpu, struct OBJDCECLIENTRM *pEngstate, void **ppTunableState) {
    return engstateAllocTunableState(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJDCECLIENTRM_OBJENGSTATE.offset), ppTunableState);
}

static NV_STATUS __nvoc_thunk_OBJENGSTATE_dceclientSetTunableState(POBJGPU pGpu, struct OBJDCECLIENTRM *pEngstate, void *pTunableState) {
    return engstateSetTunableState(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJDCECLIENTRM_OBJENGSTATE.offset), pTunableState);
}

static NvBool __nvoc_thunk_OBJENGSTATE_dceclientIsPresent(POBJGPU pGpu, struct OBJDCECLIENTRM *pEngstate) {
    return engstateIsPresent(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJDCECLIENTRM_OBJENGSTATE.offset));
}

const struct NVOC_EXPORT_INFO __nvoc_export_info_OBJDCECLIENTRM = 
{
    /*numEntries=*/     0,
    /*pExportEntries=*/  0
};

void __nvoc_dtor_OBJENGSTATE(OBJENGSTATE*);
void __nvoc_dtor_OBJDCECLIENTRM(OBJDCECLIENTRM *pThis) {
    __nvoc_dtor_OBJENGSTATE(&pThis->__nvoc_base_OBJENGSTATE);
    PORT_UNREFERENCED_VARIABLE(pThis);
}

void __nvoc_init_dataField_OBJDCECLIENTRM(OBJDCECLIENTRM *pThis) {
    PORT_UNREFERENCED_VARIABLE(pThis);
}

NV_STATUS __nvoc_ctor_OBJENGSTATE(OBJENGSTATE* );
NV_STATUS __nvoc_ctor_OBJDCECLIENTRM(OBJDCECLIENTRM *pThis) {
    NV_STATUS status = NV_OK;
    status = __nvoc_ctor_OBJENGSTATE(&pThis->__nvoc_base_OBJENGSTATE);
    if (status != NV_OK) goto __nvoc_ctor_OBJDCECLIENTRM_fail_OBJENGSTATE;
    __nvoc_init_dataField_OBJDCECLIENTRM(pThis);
    goto __nvoc_ctor_OBJDCECLIENTRM_exit; // Success

__nvoc_ctor_OBJDCECLIENTRM_fail_OBJENGSTATE:
__nvoc_ctor_OBJDCECLIENTRM_exit:

    return status;
}

static void __nvoc_init_funcTable_OBJDCECLIENTRM_1(OBJDCECLIENTRM *pThis) {
    PORT_UNREFERENCED_VARIABLE(pThis);

    pThis->__dceclientConstructEngine__ = &dceclientConstructEngine_IMPL;

    pThis->__dceclientStateDestroy__ = &dceclientStateDestroy_IMPL;

    pThis->__dceclientStateLoad__ = &dceclientStateLoad_IMPL;

    pThis->__dceclientStateUnload__ = &dceclientStateUnload_IMPL;

    pThis->__nvoc_base_OBJENGSTATE.__engstateConstructEngine__ = &__nvoc_thunk_OBJDCECLIENTRM_engstateConstructEngine;

    pThis->__nvoc_base_OBJENGSTATE.__engstateStateDestroy__ = &__nvoc_thunk_OBJDCECLIENTRM_engstateStateDestroy;

    pThis->__nvoc_base_OBJENGSTATE.__engstateStateLoad__ = &__nvoc_thunk_OBJDCECLIENTRM_engstateStateLoad;

    pThis->__nvoc_base_OBJENGSTATE.__engstateStateUnload__ = &__nvoc_thunk_OBJDCECLIENTRM_engstateStateUnload;

    pThis->__dceclientReconcileTunableState__ = &__nvoc_thunk_OBJENGSTATE_dceclientReconcileTunableState;

    pThis->__dceclientStateInitLocked__ = &__nvoc_thunk_OBJENGSTATE_dceclientStateInitLocked;

    pThis->__dceclientStatePreLoad__ = &__nvoc_thunk_OBJENGSTATE_dceclientStatePreLoad;

    pThis->__dceclientStatePostUnload__ = &__nvoc_thunk_OBJENGSTATE_dceclientStatePostUnload;

    pThis->__dceclientStatePreUnload__ = &__nvoc_thunk_OBJENGSTATE_dceclientStatePreUnload;

    pThis->__dceclientStateInitUnlocked__ = &__nvoc_thunk_OBJENGSTATE_dceclientStateInitUnlocked;

    pThis->__dceclientInitMissing__ = &__nvoc_thunk_OBJENGSTATE_dceclientInitMissing;

    pThis->__dceclientStatePreInitLocked__ = &__nvoc_thunk_OBJENGSTATE_dceclientStatePreInitLocked;

    pThis->__dceclientStatePreInitUnlocked__ = &__nvoc_thunk_OBJENGSTATE_dceclientStatePreInitUnlocked;

    pThis->__dceclientGetTunableState__ = &__nvoc_thunk_OBJENGSTATE_dceclientGetTunableState;

    pThis->__dceclientCompareTunableState__ = &__nvoc_thunk_OBJENGSTATE_dceclientCompareTunableState;

    pThis->__dceclientFreeTunableState__ = &__nvoc_thunk_OBJENGSTATE_dceclientFreeTunableState;

    pThis->__dceclientStatePostLoad__ = &__nvoc_thunk_OBJENGSTATE_dceclientStatePostLoad;

    pThis->__dceclientAllocTunableState__ = &__nvoc_thunk_OBJENGSTATE_dceclientAllocTunableState;

    pThis->__dceclientSetTunableState__ = &__nvoc_thunk_OBJENGSTATE_dceclientSetTunableState;

    pThis->__dceclientIsPresent__ = &__nvoc_thunk_OBJENGSTATE_dceclientIsPresent;
}

void __nvoc_init_funcTable_OBJDCECLIENTRM(OBJDCECLIENTRM *pThis) {
    __nvoc_init_funcTable_OBJDCECLIENTRM_1(pThis);
}

void __nvoc_init_OBJENGSTATE(OBJENGSTATE*);
void __nvoc_init_OBJDCECLIENTRM(OBJDCECLIENTRM *pThis) {
    pThis->__nvoc_pbase_OBJDCECLIENTRM = pThis;
    pThis->__nvoc_pbase_Object = &pThis->__nvoc_base_OBJENGSTATE.__nvoc_base_Object;
    pThis->__nvoc_pbase_OBJENGSTATE = &pThis->__nvoc_base_OBJENGSTATE;
    __nvoc_init_OBJENGSTATE(&pThis->__nvoc_base_OBJENGSTATE);
    __nvoc_init_funcTable_OBJDCECLIENTRM(pThis);
}

NV_STATUS __nvoc_objCreate_OBJDCECLIENTRM(OBJDCECLIENTRM **ppThis, Dynamic *pParent, NvU32 createFlags) {
    NV_STATUS status;
    Object *pParentObj;
    OBJDCECLIENTRM *pThis;

    pThis = portMemAllocNonPaged(sizeof(OBJDCECLIENTRM));
    if (pThis == NULL) return NV_ERR_NO_MEMORY;

    portMemSet(pThis, 0, sizeof(OBJDCECLIENTRM));

    __nvoc_initRtti(staticCast(pThis, Dynamic), &__nvoc_class_def_OBJDCECLIENTRM);

    if (pParent != NULL && !(createFlags & NVOC_OBJ_CREATE_FLAGS_PARENT_HALSPEC_ONLY))
    {
        pParentObj = dynamicCast(pParent, Object);
        objAddChild(pParentObj, &pThis->__nvoc_base_OBJENGSTATE.__nvoc_base_Object);
    }
    else
    {
        pThis->__nvoc_base_OBJENGSTATE.__nvoc_base_Object.pParent = NULL;
    }

    __nvoc_init_OBJDCECLIENTRM(pThis);
    status = __nvoc_ctor_OBJDCECLIENTRM(pThis);
    if (status != NV_OK) goto __nvoc_objCreate_OBJDCECLIENTRM_cleanup;

    *ppThis = pThis;
    return NV_OK;

__nvoc_objCreate_OBJDCECLIENTRM_cleanup:
    // do not call destructors here since the constructor already called them
    portMemFree(pThis);
    return status;
}

NV_STATUS __nvoc_objCreateDynamic_OBJDCECLIENTRM(OBJDCECLIENTRM **ppThis, Dynamic *pParent, NvU32 createFlags, va_list args) {
    NV_STATUS status;

    status = __nvoc_objCreate_OBJDCECLIENTRM(ppThis, pParent, createFlags);

    return status;
}

