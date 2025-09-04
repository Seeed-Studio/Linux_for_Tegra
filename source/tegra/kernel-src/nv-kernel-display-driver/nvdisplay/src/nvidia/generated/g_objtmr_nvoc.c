#define NVOC_OBJTMR_H_PRIVATE_ACCESS_ALLOWED
#include "nvoc/runtime.h"
#include "nvoc/rtti.h"
#include "nvtypes.h"
#include "nvport/nvport.h"
#include "nvport/inline/util_valist.h"
#include "utils/nvassert.h"
#include "g_objtmr_nvoc.h"

#ifdef DEBUG
char __nvoc_class_id_uniqueness_check_0x9ddede = 1;
#endif

extern const struct NVOC_CLASS_DEF __nvoc_class_def_OBJTMR;

extern const struct NVOC_CLASS_DEF __nvoc_class_def_Object;

extern const struct NVOC_CLASS_DEF __nvoc_class_def_OBJENGSTATE;

void __nvoc_init_OBJTMR(OBJTMR*, RmHalspecOwner* );
void __nvoc_init_funcTable_OBJTMR(OBJTMR*, RmHalspecOwner* );
NV_STATUS __nvoc_ctor_OBJTMR(OBJTMR*, RmHalspecOwner* );
void __nvoc_init_dataField_OBJTMR(OBJTMR*, RmHalspecOwner* );
void __nvoc_dtor_OBJTMR(OBJTMR*);
extern const struct NVOC_EXPORT_INFO __nvoc_export_info_OBJTMR;

static const struct NVOC_RTTI __nvoc_rtti_OBJTMR_OBJTMR = {
    /*pClassDef=*/          &__nvoc_class_def_OBJTMR,
    /*dtor=*/               (NVOC_DYNAMIC_DTOR) &__nvoc_dtor_OBJTMR,
    /*offset=*/             0,
};

static const struct NVOC_RTTI __nvoc_rtti_OBJTMR_Object = {
    /*pClassDef=*/          &__nvoc_class_def_Object,
    /*dtor=*/               &__nvoc_destructFromBase,
    /*offset=*/             NV_OFFSETOF(OBJTMR, __nvoc_base_OBJENGSTATE.__nvoc_base_Object),
};

static const struct NVOC_RTTI __nvoc_rtti_OBJTMR_OBJENGSTATE = {
    /*pClassDef=*/          &__nvoc_class_def_OBJENGSTATE,
    /*dtor=*/               &__nvoc_destructFromBase,
    /*offset=*/             NV_OFFSETOF(OBJTMR, __nvoc_base_OBJENGSTATE),
};

static const struct NVOC_CASTINFO __nvoc_castinfo_OBJTMR = {
    /*numRelatives=*/       3,
    /*relatives=*/ {
        &__nvoc_rtti_OBJTMR_OBJTMR,
        &__nvoc_rtti_OBJTMR_OBJENGSTATE,
        &__nvoc_rtti_OBJTMR_Object,
    },
};

const struct NVOC_CLASS_DEF __nvoc_class_def_OBJTMR = 
{
    /*classInfo=*/ {
        /*size=*/               sizeof(OBJTMR),
        /*classId=*/            classId(OBJTMR),
        /*providerId=*/         &__nvoc_rtti_provider,
#if NV_PRINTF_STRINGS_ALLOWED
        /*name=*/               "OBJTMR",
#endif
    },
    /*objCreatefn=*/        (NVOC_DYNAMIC_OBJ_CREATE) &__nvoc_objCreateDynamic_OBJTMR,
    /*pCastInfo=*/          &__nvoc_castinfo_OBJTMR,
    /*pExportInfo=*/        &__nvoc_export_info_OBJTMR
};

static NV_STATUS __nvoc_thunk_OBJTMR_engstateConstructEngine(struct OBJGPU *pGpu, struct OBJENGSTATE *pTmr, ENGDESCRIPTOR arg0) {
    return tmrConstructEngine(pGpu, (struct OBJTMR *)(((unsigned char *)pTmr) - __nvoc_rtti_OBJTMR_OBJENGSTATE.offset), arg0);
}

static NV_STATUS __nvoc_thunk_OBJTMR_engstateStateInitLocked(struct OBJGPU *pGpu, struct OBJENGSTATE *pTmr) {
    return tmrStateInitLocked(pGpu, (struct OBJTMR *)(((unsigned char *)pTmr) - __nvoc_rtti_OBJTMR_OBJENGSTATE.offset));
}

static NV_STATUS __nvoc_thunk_OBJTMR_engstateStateLoad(struct OBJGPU *pGpu, struct OBJENGSTATE *pTmr, NvU32 arg0) {
    return tmrStateLoad(pGpu, (struct OBJTMR *)(((unsigned char *)pTmr) - __nvoc_rtti_OBJTMR_OBJENGSTATE.offset), arg0);
}

static NV_STATUS __nvoc_thunk_OBJTMR_engstateStateUnload(struct OBJGPU *pGpu, struct OBJENGSTATE *pTmr, NvU32 arg0) {
    return tmrStateUnload(pGpu, (struct OBJTMR *)(((unsigned char *)pTmr) - __nvoc_rtti_OBJTMR_OBJENGSTATE.offset), arg0);
}

static void __nvoc_thunk_OBJTMR_engstateStateDestroy(struct OBJGPU *pGpu, struct OBJENGSTATE *pTmr) {
    tmrStateDestroy(pGpu, (struct OBJTMR *)(((unsigned char *)pTmr) - __nvoc_rtti_OBJTMR_OBJENGSTATE.offset));
}

static NV_STATUS __nvoc_thunk_OBJENGSTATE_tmrReconcileTunableState(POBJGPU pGpu, struct OBJTMR *pEngstate, void *pTunableState) {
    return engstateReconcileTunableState(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJTMR_OBJENGSTATE.offset), pTunableState);
}

static NV_STATUS __nvoc_thunk_OBJENGSTATE_tmrStatePreLoad(POBJGPU pGpu, struct OBJTMR *pEngstate, NvU32 arg0) {
    return engstateStatePreLoad(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJTMR_OBJENGSTATE.offset), arg0);
}

static NV_STATUS __nvoc_thunk_OBJENGSTATE_tmrStatePostUnload(POBJGPU pGpu, struct OBJTMR *pEngstate, NvU32 arg0) {
    return engstateStatePostUnload(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJTMR_OBJENGSTATE.offset), arg0);
}

static NV_STATUS __nvoc_thunk_OBJENGSTATE_tmrStatePreUnload(POBJGPU pGpu, struct OBJTMR *pEngstate, NvU32 arg0) {
    return engstateStatePreUnload(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJTMR_OBJENGSTATE.offset), arg0);
}

static NV_STATUS __nvoc_thunk_OBJENGSTATE_tmrStateInitUnlocked(POBJGPU pGpu, struct OBJTMR *pEngstate) {
    return engstateStateInitUnlocked(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJTMR_OBJENGSTATE.offset));
}

static void __nvoc_thunk_OBJENGSTATE_tmrInitMissing(POBJGPU pGpu, struct OBJTMR *pEngstate) {
    engstateInitMissing(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJTMR_OBJENGSTATE.offset));
}

static NV_STATUS __nvoc_thunk_OBJENGSTATE_tmrStatePreInitLocked(POBJGPU pGpu, struct OBJTMR *pEngstate) {
    return engstateStatePreInitLocked(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJTMR_OBJENGSTATE.offset));
}

static NV_STATUS __nvoc_thunk_OBJENGSTATE_tmrStatePreInitUnlocked(POBJGPU pGpu, struct OBJTMR *pEngstate) {
    return engstateStatePreInitUnlocked(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJTMR_OBJENGSTATE.offset));
}

static NV_STATUS __nvoc_thunk_OBJENGSTATE_tmrGetTunableState(POBJGPU pGpu, struct OBJTMR *pEngstate, void *pTunableState) {
    return engstateGetTunableState(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJTMR_OBJENGSTATE.offset), pTunableState);
}

static NV_STATUS __nvoc_thunk_OBJENGSTATE_tmrCompareTunableState(POBJGPU pGpu, struct OBJTMR *pEngstate, void *pTunables1, void *pTunables2) {
    return engstateCompareTunableState(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJTMR_OBJENGSTATE.offset), pTunables1, pTunables2);
}

static void __nvoc_thunk_OBJENGSTATE_tmrFreeTunableState(POBJGPU pGpu, struct OBJTMR *pEngstate, void *pTunableState) {
    engstateFreeTunableState(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJTMR_OBJENGSTATE.offset), pTunableState);
}

static NV_STATUS __nvoc_thunk_OBJENGSTATE_tmrStatePostLoad(POBJGPU pGpu, struct OBJTMR *pEngstate, NvU32 arg0) {
    return engstateStatePostLoad(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJTMR_OBJENGSTATE.offset), arg0);
}

static NV_STATUS __nvoc_thunk_OBJENGSTATE_tmrAllocTunableState(POBJGPU pGpu, struct OBJTMR *pEngstate, void **ppTunableState) {
    return engstateAllocTunableState(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJTMR_OBJENGSTATE.offset), ppTunableState);
}

static NV_STATUS __nvoc_thunk_OBJENGSTATE_tmrSetTunableState(POBJGPU pGpu, struct OBJTMR *pEngstate, void *pTunableState) {
    return engstateSetTunableState(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJTMR_OBJENGSTATE.offset), pTunableState);
}

static NvBool __nvoc_thunk_OBJENGSTATE_tmrIsPresent(POBJGPU pGpu, struct OBJTMR *pEngstate) {
    return engstateIsPresent(pGpu, (struct OBJENGSTATE *)(((unsigned char *)pEngstate) + __nvoc_rtti_OBJTMR_OBJENGSTATE.offset));
}

const struct NVOC_EXPORT_INFO __nvoc_export_info_OBJTMR = 
{
    /*numEntries=*/     0,
    /*pExportEntries=*/  0
};

void __nvoc_dtor_OBJENGSTATE(OBJENGSTATE*);
void __nvoc_dtor_OBJTMR(OBJTMR *pThis) {
    __nvoc_tmrDestruct(pThis);
    __nvoc_dtor_OBJENGSTATE(&pThis->__nvoc_base_OBJENGSTATE);
    PORT_UNREFERENCED_VARIABLE(pThis);
}

void __nvoc_init_dataField_OBJTMR(OBJTMR *pThis, RmHalspecOwner *pRmhalspecowner) {
    ChipHal *chipHal = &pRmhalspecowner->chipHal;
    const unsigned long chipHal_HalVarIdx = (unsigned long)chipHal->__nvoc_HalVarIdx;
    RmVariantHal *rmVariantHal = &pRmhalspecowner->rmVariantHal;
    const unsigned long rmVariantHal_HalVarIdx = (unsigned long)rmVariantHal->__nvoc_HalVarIdx;
    PORT_UNREFERENCED_VARIABLE(pThis);
    PORT_UNREFERENCED_VARIABLE(pRmhalspecowner);
    PORT_UNREFERENCED_VARIABLE(chipHal);
    PORT_UNREFERENCED_VARIABLE(chipHal_HalVarIdx);
    PORT_UNREFERENCED_VARIABLE(rmVariantHal);
    PORT_UNREFERENCED_VARIABLE(rmVariantHal_HalVarIdx);

    // NVOC Property Hal field -- PDB_PROP_TMR_USE_COUNTDOWN_TIMER_FOR_RM_CALLBACKS
    if (( ((chipHal_HalVarIdx >> 5) == 2UL) && ((1UL << (chipHal_HalVarIdx & 0x1f)) & 0x00010000UL) )) /* ChipHal: T234D */ 
    {
        pThis->setProperty(pThis, PDB_PROP_TMR_USE_COUNTDOWN_TIMER_FOR_RM_CALLBACKS, ((NvBool)(0 == 0)));
    }
    // default
    else
    {
        pThis->setProperty(pThis, PDB_PROP_TMR_USE_COUNTDOWN_TIMER_FOR_RM_CALLBACKS, ((NvBool)(0 != 0)));
    }

    // NVOC Property Hal field -- PDB_PROP_TMR_ALARM_INTR_REMOVED_FROM_PMC_TREE
    if (( ((chipHal_HalVarIdx >> 5) == 2UL) && ((1UL << (chipHal_HalVarIdx & 0x1f)) & 0x00010000UL) )) /* ChipHal: T234D */ 
    {
        pThis->setProperty(pThis, PDB_PROP_TMR_ALARM_INTR_REMOVED_FROM_PMC_TREE, ((NvBool)(0 == 0)));
    }
    // default
    else
    {
        pThis->setProperty(pThis, PDB_PROP_TMR_ALARM_INTR_REMOVED_FROM_PMC_TREE, ((NvBool)(0 != 0)));
    }

    // NVOC Property Hal field -- PDB_PROP_TMR_USE_OS_TIMER_FOR_CALLBACKS
    if (( ((chipHal_HalVarIdx >> 5) == 2UL) && ((1UL << (chipHal_HalVarIdx & 0x1f)) & 0x00010000UL) )) /* ChipHal: T234D */ 
    {
        pThis->setProperty(pThis, PDB_PROP_TMR_USE_OS_TIMER_FOR_CALLBACKS, ((NvBool)(0 == 0)));
    }
    // default
    else
    {
        pThis->setProperty(pThis, PDB_PROP_TMR_USE_OS_TIMER_FOR_CALLBACKS, ((NvBool)(0 != 0)));
    }
    pThis->setProperty(pThis, PDB_PROP_TMR_USE_PTIMER_FOR_OSTIMER_CALLBACKS, (0));
    pThis->setProperty(pThis, PDB_PROP_TMR_USE_POLLING_FOR_CALLBACKS, (0));

    // NVOC Property Hal field -- PDB_PROP_TMR_USE_SECOND_COUNTDOWN_TIMER_FOR_SWRL
    if (0)
    {
    }
    // default
    else
    {
        pThis->setProperty(pThis, PDB_PROP_TMR_USE_SECOND_COUNTDOWN_TIMER_FOR_SWRL, ((NvBool)(0 != 0)));
    }
}

NV_STATUS __nvoc_ctor_OBJENGSTATE(OBJENGSTATE* );
NV_STATUS __nvoc_ctor_OBJTMR(OBJTMR *pThis, RmHalspecOwner *pRmhalspecowner) {
    NV_STATUS status = NV_OK;
    status = __nvoc_ctor_OBJENGSTATE(&pThis->__nvoc_base_OBJENGSTATE);
    if (status != NV_OK) goto __nvoc_ctor_OBJTMR_fail_OBJENGSTATE;
    __nvoc_init_dataField_OBJTMR(pThis, pRmhalspecowner);
    goto __nvoc_ctor_OBJTMR_exit; // Success

__nvoc_ctor_OBJTMR_fail_OBJENGSTATE:
__nvoc_ctor_OBJTMR_exit:

    return status;
}

static void __nvoc_init_funcTable_OBJTMR_1(OBJTMR *pThis, RmHalspecOwner *pRmhalspecowner) {
    ChipHal *chipHal = &pRmhalspecowner->chipHal;
    const unsigned long chipHal_HalVarIdx = (unsigned long)chipHal->__nvoc_HalVarIdx;
    RmVariantHal *rmVariantHal = &pRmhalspecowner->rmVariantHal;
    const unsigned long rmVariantHal_HalVarIdx = (unsigned long)rmVariantHal->__nvoc_HalVarIdx;
    PORT_UNREFERENCED_VARIABLE(pThis);
    PORT_UNREFERENCED_VARIABLE(pRmhalspecowner);
    PORT_UNREFERENCED_VARIABLE(chipHal);
    PORT_UNREFERENCED_VARIABLE(chipHal_HalVarIdx);
    PORT_UNREFERENCED_VARIABLE(rmVariantHal);
    PORT_UNREFERENCED_VARIABLE(rmVariantHal_HalVarIdx);

    pThis->__tmrConstructEngine__ = &tmrConstructEngine_IMPL;

    pThis->__tmrStateInitLocked__ = &tmrStateInitLocked_IMPL;

    pThis->__tmrStateLoad__ = &tmrStateLoad_IMPL;

    pThis->__tmrStateUnload__ = &tmrStateUnload_IMPL;

    pThis->__tmrStateDestroy__ = &tmrStateDestroy_IMPL;

    pThis->__nvoc_base_OBJENGSTATE.__engstateConstructEngine__ = &__nvoc_thunk_OBJTMR_engstateConstructEngine;

    pThis->__nvoc_base_OBJENGSTATE.__engstateStateInitLocked__ = &__nvoc_thunk_OBJTMR_engstateStateInitLocked;

    pThis->__nvoc_base_OBJENGSTATE.__engstateStateLoad__ = &__nvoc_thunk_OBJTMR_engstateStateLoad;

    pThis->__nvoc_base_OBJENGSTATE.__engstateStateUnload__ = &__nvoc_thunk_OBJTMR_engstateStateUnload;

    pThis->__nvoc_base_OBJENGSTATE.__engstateStateDestroy__ = &__nvoc_thunk_OBJTMR_engstateStateDestroy;

    pThis->__tmrReconcileTunableState__ = &__nvoc_thunk_OBJENGSTATE_tmrReconcileTunableState;

    pThis->__tmrStatePreLoad__ = &__nvoc_thunk_OBJENGSTATE_tmrStatePreLoad;

    pThis->__tmrStatePostUnload__ = &__nvoc_thunk_OBJENGSTATE_tmrStatePostUnload;

    pThis->__tmrStatePreUnload__ = &__nvoc_thunk_OBJENGSTATE_tmrStatePreUnload;

    pThis->__tmrStateInitUnlocked__ = &__nvoc_thunk_OBJENGSTATE_tmrStateInitUnlocked;

    pThis->__tmrInitMissing__ = &__nvoc_thunk_OBJENGSTATE_tmrInitMissing;

    pThis->__tmrStatePreInitLocked__ = &__nvoc_thunk_OBJENGSTATE_tmrStatePreInitLocked;

    pThis->__tmrStatePreInitUnlocked__ = &__nvoc_thunk_OBJENGSTATE_tmrStatePreInitUnlocked;

    pThis->__tmrGetTunableState__ = &__nvoc_thunk_OBJENGSTATE_tmrGetTunableState;

    pThis->__tmrCompareTunableState__ = &__nvoc_thunk_OBJENGSTATE_tmrCompareTunableState;

    pThis->__tmrFreeTunableState__ = &__nvoc_thunk_OBJENGSTATE_tmrFreeTunableState;

    pThis->__tmrStatePostLoad__ = &__nvoc_thunk_OBJENGSTATE_tmrStatePostLoad;

    pThis->__tmrAllocTunableState__ = &__nvoc_thunk_OBJENGSTATE_tmrAllocTunableState;

    pThis->__tmrSetTunableState__ = &__nvoc_thunk_OBJENGSTATE_tmrSetTunableState;

    pThis->__tmrIsPresent__ = &__nvoc_thunk_OBJENGSTATE_tmrIsPresent;
}

void __nvoc_init_funcTable_OBJTMR(OBJTMR *pThis, RmHalspecOwner *pRmhalspecowner) {
    __nvoc_init_funcTable_OBJTMR_1(pThis, pRmhalspecowner);
}

void __nvoc_init_OBJENGSTATE(OBJENGSTATE*);
void __nvoc_init_OBJTMR(OBJTMR *pThis, RmHalspecOwner *pRmhalspecowner) {
    pThis->__nvoc_pbase_OBJTMR = pThis;
    pThis->__nvoc_pbase_Object = &pThis->__nvoc_base_OBJENGSTATE.__nvoc_base_Object;
    pThis->__nvoc_pbase_OBJENGSTATE = &pThis->__nvoc_base_OBJENGSTATE;
    __nvoc_init_OBJENGSTATE(&pThis->__nvoc_base_OBJENGSTATE);
    __nvoc_init_funcTable_OBJTMR(pThis, pRmhalspecowner);
}

NV_STATUS __nvoc_objCreate_OBJTMR(OBJTMR **ppThis, Dynamic *pParent, NvU32 createFlags) {
    NV_STATUS status;
    Object *pParentObj;
    OBJTMR *pThis;
    RmHalspecOwner *pRmhalspecowner;

    pThis = portMemAllocNonPaged(sizeof(OBJTMR));
    if (pThis == NULL) return NV_ERR_NO_MEMORY;

    portMemSet(pThis, 0, sizeof(OBJTMR));

    __nvoc_initRtti(staticCast(pThis, Dynamic), &__nvoc_class_def_OBJTMR);

    if (pParent != NULL && !(createFlags & NVOC_OBJ_CREATE_FLAGS_PARENT_HALSPEC_ONLY))
    {
        pParentObj = dynamicCast(pParent, Object);
        objAddChild(pParentObj, &pThis->__nvoc_base_OBJENGSTATE.__nvoc_base_Object);
    }
    else
    {
        pThis->__nvoc_base_OBJENGSTATE.__nvoc_base_Object.pParent = NULL;
    }

    if ((pRmhalspecowner = dynamicCast(pParent, RmHalspecOwner)) == NULL)
        pRmhalspecowner = objFindAncestorOfType(RmHalspecOwner, pParent);
    NV_ASSERT_OR_RETURN(pRmhalspecowner != NULL, NV_ERR_INVALID_ARGUMENT);

    __nvoc_init_OBJTMR(pThis, pRmhalspecowner);
    status = __nvoc_ctor_OBJTMR(pThis, pRmhalspecowner);
    if (status != NV_OK) goto __nvoc_objCreate_OBJTMR_cleanup;

    *ppThis = pThis;
    return NV_OK;

__nvoc_objCreate_OBJTMR_cleanup:
    // do not call destructors here since the constructor already called them
    portMemFree(pThis);
    return status;
}

NV_STATUS __nvoc_objCreateDynamic_OBJTMR(OBJTMR **ppThis, Dynamic *pParent, NvU32 createFlags, va_list args) {
    NV_STATUS status;

    status = __nvoc_objCreate_OBJTMR(ppThis, pParent, createFlags);

    return status;
}

