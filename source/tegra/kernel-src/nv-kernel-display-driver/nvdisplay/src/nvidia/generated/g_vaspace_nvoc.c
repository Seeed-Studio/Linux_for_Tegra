#define NVOC_VASPACE_H_PRIVATE_ACCESS_ALLOWED
#include "nvoc/runtime.h"
#include "nvoc/rtti.h"
#include "nvtypes.h"
#include "nvport/nvport.h"
#include "nvport/inline/util_valist.h"
#include "utils/nvassert.h"
#include "g_vaspace_nvoc.h"

#ifdef DEBUG
char __nvoc_class_id_uniqueness_check_0x6c347f = 1;
#endif

extern const struct NVOC_CLASS_DEF __nvoc_class_def_OBJVASPACE;

extern const struct NVOC_CLASS_DEF __nvoc_class_def_Object;

void __nvoc_init_OBJVASPACE(OBJVASPACE*);
void __nvoc_init_funcTable_OBJVASPACE(OBJVASPACE*);
NV_STATUS __nvoc_ctor_OBJVASPACE(OBJVASPACE*);
void __nvoc_init_dataField_OBJVASPACE(OBJVASPACE*);
void __nvoc_dtor_OBJVASPACE(OBJVASPACE*);
extern const struct NVOC_EXPORT_INFO __nvoc_export_info_OBJVASPACE;

static const struct NVOC_RTTI __nvoc_rtti_OBJVASPACE_OBJVASPACE = {
    /*pClassDef=*/          &__nvoc_class_def_OBJVASPACE,
    /*dtor=*/               (NVOC_DYNAMIC_DTOR) &__nvoc_dtor_OBJVASPACE,
    /*offset=*/             0,
};

static const struct NVOC_RTTI __nvoc_rtti_OBJVASPACE_Object = {
    /*pClassDef=*/          &__nvoc_class_def_Object,
    /*dtor=*/               &__nvoc_destructFromBase,
    /*offset=*/             NV_OFFSETOF(OBJVASPACE, __nvoc_base_Object),
};

static const struct NVOC_CASTINFO __nvoc_castinfo_OBJVASPACE = {
    /*numRelatives=*/       2,
    /*relatives=*/ {
        &__nvoc_rtti_OBJVASPACE_OBJVASPACE,
        &__nvoc_rtti_OBJVASPACE_Object,
    },
};

// Not instantiable because it's an abstract class with following pure virtual functions:
//  vaspaceConstruct_
//  vaspaceAlloc
//  vaspaceFree
//  vaspaceApplyDefaultAlignment
//  vaspaceGetVasInfo
const struct NVOC_CLASS_DEF __nvoc_class_def_OBJVASPACE = 
{
    /*classInfo=*/ {
        /*size=*/               sizeof(OBJVASPACE),
        /*classId=*/            classId(OBJVASPACE),
        /*providerId=*/         &__nvoc_rtti_provider,
#if NV_PRINTF_STRINGS_ALLOWED
        /*name=*/               "OBJVASPACE",
#endif
    },
    /*objCreatefn=*/        (NVOC_DYNAMIC_OBJ_CREATE) NULL,
    /*pCastInfo=*/          &__nvoc_castinfo_OBJVASPACE,
    /*pExportInfo=*/        &__nvoc_export_info_OBJVASPACE
};

const struct NVOC_EXPORT_INFO __nvoc_export_info_OBJVASPACE = 
{
    /*numEntries=*/     0,
    /*pExportEntries=*/  0
};

void __nvoc_dtor_Object(Object*);
void __nvoc_dtor_OBJVASPACE(OBJVASPACE *pThis) {
    __nvoc_dtor_Object(&pThis->__nvoc_base_Object);
    PORT_UNREFERENCED_VARIABLE(pThis);
}

void __nvoc_init_dataField_OBJVASPACE(OBJVASPACE *pThis) {
    PORT_UNREFERENCED_VARIABLE(pThis);
}

NV_STATUS __nvoc_ctor_Object(Object* );
NV_STATUS __nvoc_ctor_OBJVASPACE(OBJVASPACE *pThis) {
    NV_STATUS status = NV_OK;
    status = __nvoc_ctor_Object(&pThis->__nvoc_base_Object);
    if (status != NV_OK) goto __nvoc_ctor_OBJVASPACE_fail_Object;
    __nvoc_init_dataField_OBJVASPACE(pThis);
    goto __nvoc_ctor_OBJVASPACE_exit; // Success

__nvoc_ctor_OBJVASPACE_fail_Object:
__nvoc_ctor_OBJVASPACE_exit:

    return status;
}

static void __nvoc_init_funcTable_OBJVASPACE_1(OBJVASPACE *pThis) {
    PORT_UNREFERENCED_VARIABLE(pThis);

    pThis->__vaspaceConstruct___ = NULL;

    pThis->__vaspaceAlloc__ = NULL;

    pThis->__vaspaceFree__ = NULL;

    pThis->__vaspaceApplyDefaultAlignment__ = NULL;

    pThis->__vaspaceIncAllocRefCnt__ = &vaspaceIncAllocRefCnt_b7902c;

    pThis->__vaspaceGetVaStart__ = &vaspaceGetVaStart_IMPL;

    pThis->__vaspaceGetVaLimit__ = &vaspaceGetVaLimit_IMPL;

    pThis->__vaspaceGetVasInfo__ = NULL;

    pThis->__vaspaceGetFlags__ = &vaspaceGetFlags_edd98b;

    pThis->__vaspaceIsInternalVaRestricted__ = &vaspaceIsInternalVaRestricted_IMPL;
}

void __nvoc_init_funcTable_OBJVASPACE(OBJVASPACE *pThis) {
    __nvoc_init_funcTable_OBJVASPACE_1(pThis);
}

void __nvoc_init_Object(Object*);
void __nvoc_init_OBJVASPACE(OBJVASPACE *pThis) {
    pThis->__nvoc_pbase_OBJVASPACE = pThis;
    pThis->__nvoc_pbase_Object = &pThis->__nvoc_base_Object;
    __nvoc_init_Object(&pThis->__nvoc_base_Object);
    __nvoc_init_funcTable_OBJVASPACE(pThis);
}

