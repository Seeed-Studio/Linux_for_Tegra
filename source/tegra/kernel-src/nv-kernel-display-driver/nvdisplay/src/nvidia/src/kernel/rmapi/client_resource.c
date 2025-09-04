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

#include "core/core.h"
#include "core/locks.h"
#include "core/system.h"
#include "os/os.h"
#include "rmapi/client_resource.h"
#include "rmapi/param_copy.h"
#include "rmapi/rs_utils.h"
#include "gpu/gpu.h"
#include "gpu/device/device.h"
#include "gpu_mgr/gpu_mgr.h"
#include "resserv/rs_client.h"
#include "resserv/rs_server.h"
#include "resserv/rs_access_map.h"
#include "nvBldVer.h"
#include "nvVer.h"
#include "mem_mgr/mem.h"
#include "nvsecurityinfo.h"
#include "resource_desc.h"

#include "mem_mgr/virt_mem_mgr.h"

NV_STATUS
cliresConstruct_IMPL
(
    RmClientResource *pRmCliRes,
    CALL_CONTEXT *pCallContext,
    RS_RES_ALLOC_PARAMS_INTERNAL* pParams
)
{
    return NV_OK;
}

void
cliresDestruct_IMPL
(
    RmClientResource *pRmCliRes
)
{
}

NvBool
cliresAccessCallback_IMPL
(
    RmClientResource *pRmCliRes,
    RsClient *pInvokingClient,
    void *pAllocParams,
    RsAccessRight accessRight
)
{
    // Client resource's access callback will grant any rights here to any resource it owns
    switch (accessRight)
    {
        case RS_ACCESS_NICE:
        {
            // Grant if the caller satisfies osAllowPriorityOverride
            return osAllowPriorityOverride();
        }
    }

    // Delegate to superclass
    return resAccessCallback_IMPL(staticCast(pRmCliRes, RsResource), pInvokingClient, pAllocParams, accessRight);
}

NvBool
cliresShareCallback_IMPL
(
    RmClientResource *pRmCliRes,
    RsClient *pInvokingClient,
    RsResourceRef *pParentRef,
    RS_SHARE_POLICY *pSharePolicy
)
{
    RmClient *pSrcClient = dynamicCast(RES_GET_CLIENT(pRmCliRes), RmClient);
    RmClient *pDstClient = dynamicCast(pInvokingClient, RmClient);
    NvBool bDstKernel = (pDstClient != NULL) &&
                        (rmclientGetCachedPrivilege(pDstClient) >= RS_PRIV_LEVEL_KERNEL);

    // Client resource's share callback will also share rights it shares here with any resource it owns
    //
    // If a kernel client is validating share policies, that means it's most likely duping on behalf of
    // a user space client. For this case, we check against the current process instead of the kernel
    // client object's process.
    //
    switch (pSharePolicy->type)
    {
        case RS_SHARE_TYPE_OS_SECURITY_TOKEN:
            if ((pSrcClient != NULL) && (pDstClient != NULL) &&
                (pSrcClient->pSecurityToken != NULL))
            {
                if (bDstKernel)
                {
                    NV_STATUS status;
                    PSECURITY_TOKEN *pCurrentToken;

                    pCurrentToken = osGetSecurityToken();
                    if (pCurrentToken == NULL)
                    {
                        NV_ASSERT_FAILED("Cannot get the security token for the current user");
                        return NV_FALSE;
                    }

                    status = osValidateClientTokens(pSrcClient->pSecurityToken, pCurrentToken);
                    portMemFree(pCurrentToken);
                    if (status == NV_OK)
                    {
                        return NV_TRUE;
                    }
                }
                else if (pDstClient->pSecurityToken != NULL)
                {
                    if (osValidateClientTokens(pSrcClient->pSecurityToken, pDstClient->pSecurityToken) == NV_OK)
                        return NV_TRUE;
                }
            }
            break;
        case RS_SHARE_TYPE_PID:
            if ((pSrcClient != NULL) && (pDstClient != NULL))
            {
                if ((pParentRef != NULL) && bDstKernel)
                {
                    if (pSrcClient->ProcID == osGetCurrentProcess())
                        return NV_TRUE;
                }
                else
                {
                    if (pSrcClient->ProcID == pDstClient->ProcID)
                        return NV_TRUE;
                }
            }
            break;
        case RS_SHARE_TYPE_SMC_PARTITION:
        case RS_SHARE_TYPE_GPU:
            // Require exceptions, since RmClientResource is not an RmResource
            if (pSharePolicy->action & RS_SHARE_ACTION_FLAG_REQUIRE)
                return NV_TRUE;
            break;
    }

    // Delegate to superclass
    return resShareCallback_IMPL(staticCast(pRmCliRes, RsResource), pInvokingClient, pParentRef, pSharePolicy);
}

// ****************************************************************************
//                              Helper functions
// ****************************************************************************


static NV_STATUS
CliControlSystemEvent
(
    NvHandle hClient,
    NvU32    event,
    NvU32    action
)
{
    NV_STATUS status = NV_OK;
    RmClient *pClient;
    PEVENTNOTIFICATION *pEventNotification = NULL;

    if (event >= NV0000_NOTIFIERS_MAXCOUNT)
    {
        return NV_ERR_INVALID_ARGUMENT;
    }

    if (NV_OK != serverutilGetClientUnderLock(hClient, &pClient))
        return NV_ERR_INVALID_CLIENT;

    CliGetEventNotificationList(hClient, hClient, NULL, &pEventNotification);
    if (pEventNotification != NULL)
    {
        switch (action)
        {
            case NV0000_CTRL_EVENT_SET_NOTIFICATION_ACTION_SINGLE:
            case NV0000_CTRL_EVENT_SET_NOTIFICATION_ACTION_REPEAT:
            {
                if (pClient->CliSysEventInfo.notifyActions[event] != NV0000_CTRL_EVENT_SET_NOTIFICATION_ACTION_DISABLE)
                {
                    status = NV_ERR_INVALID_STATE;
                    break;
                }

            //fall through
            }
            case NV0000_CTRL_EVENT_SET_NOTIFICATION_ACTION_DISABLE:
            {
                pClient->CliSysEventInfo.notifyActions[event] = action;
                break;
            }

            default:
            {
                status = NV_ERR_INVALID_ARGUMENT;
                break;
            }
        }
    }
    else
    {
        status = NV_ERR_INVALID_STATE;
    }

    return status;
}



static NV_STATUS
CliGetSystemEventStatus
(
    NvHandle  hClient,
    NvU32    *pEvent,
    NvU32    *pStatus
)
{
    NvU32 Head, Tail;
    RmClient *pClient;

    if (NV_OK != serverutilGetClientUnderLock(hClient, &pClient))
        return NV_ERR_INVALID_CLIENT;

    Head = pClient->CliSysEventInfo.systemEventsQueue.Head;
    Tail = pClient->CliSysEventInfo.systemEventsQueue.Tail;

    if (Head == Tail)
    {
        *pEvent = NV0000_NOTIFIERS_EVENT_NONE_PENDING;
        *pStatus = 0;
    }
    else
    {
        *pEvent  = pClient->CliSysEventInfo.systemEventsQueue.EventQueue[Tail].event;
        *pStatus = pClient->CliSysEventInfo.systemEventsQueue.EventQueue[Tail].status;
        pClient->CliSysEventInfo.systemEventsQueue.Tail = (Tail + 1) % NV_SYSTEM_EVENT_QUEUE_SIZE;
    }

    return NV_OK;
}



// ****************************************************************************
//                              Other functions
// ****************************************************************************

//
// cliresCtrlCmdSystemGetFeatures
//
// Lock Requirements:
//      Assert that API lock held on entry
//      No GPUs lock
//
NV_STATUS
cliresCtrlCmdSystemGetFeatures_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_SYSTEM_GET_FEATURES_PARAMS *pFeaturesParams
)
{
    OBJSYS    *pSys         = SYS_GET_INSTANCE();
    NvU32      featuresMask = 0;

    NV_ASSERT_OR_RETURN(pSys != NULL, NV_ERR_INVALID_STATE);

    LOCK_ASSERT_AND_RETURN(rmApiLockIsOwner());

    if (pSys->getProperty(pSys, PDB_PROP_SYS_IS_UEFI))
    {
         featuresMask = FLD_SET_DRF(0000, _CTRL_SYSTEM_GET_FEATURES,
            _UEFI, _TRUE, featuresMask);
    }

    // Don't update EFI init on non Display system
    if (pSys->getProperty(pSys, PDB_PROP_SYS_IS_EFI_INIT))
    {
        featuresMask = FLD_SET_DRF(0000, _CTRL_SYSTEM_GET_FEATURES,
            _IS_EFI_INIT, _TRUE, featuresMask);
    }

    pFeaturesParams->featuresMask = featuresMask;

    return NV_OK;
}

//
// cliresCtrlCmdSystemGetBuildVersionV2
//
// Lock Requirements:
//      Assert that API lock held on entry
//      No GPUs lock
//
NV_STATUS
cliresCtrlCmdSystemGetBuildVersionV2_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_SYSTEM_GET_BUILD_VERSION_V2_PARAMS *pParams
)
{
    LOCK_ASSERT_AND_RETURN(rmApiLockIsOwner());

    ct_assert(sizeof(NV_VERSION_STRING) <= sizeof(pParams->driverVersionBuffer));
    ct_assert(sizeof(NV_BUILD_BRANCH_VERSION) <= sizeof(pParams->versionBuffer));
    ct_assert(sizeof(NV_DISPLAY_DRIVER_TITLE) <= sizeof(pParams->titleBuffer));

    portMemCopy(pParams->driverVersionBuffer, sizeof(pParams->driverVersionBuffer),
                NV_VERSION_STRING, sizeof(NV_VERSION_STRING));
    portMemCopy(pParams->versionBuffer, sizeof(pParams->versionBuffer),
                NV_BUILD_BRANCH_VERSION, sizeof(NV_BUILD_BRANCH_VERSION));
    portMemCopy(pParams->titleBuffer, sizeof(pParams->titleBuffer),
                NV_DISPLAY_DRIVER_TITLE, sizeof(NV_DISPLAY_DRIVER_TITLE));

    pParams->changelistNumber = NV_BUILD_CHANGELIST_NUM;
    pParams->officialChangelistNumber = NV_LAST_OFFICIAL_CHANGELIST_NUM;

    return NV_OK;
}

//
// cliresCtrlCmdSystemGetCpuInfo
//
// Lock Requirements:
//      Assert that API lock held on entry
//      No GPUs lock
//
NV_STATUS
cliresCtrlCmdSystemGetCpuInfo_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_SYSTEM_GET_CPU_INFO_PARAMS *pCpuInfoParams
)
{
    OBJSYS    *pSys = SYS_GET_INSTANCE();

    LOCK_ASSERT_AND_RETURN(rmApiLockIsOwner());

    pCpuInfoParams->type = pSys->cpuInfo.type;
    pCpuInfoParams->capabilities = pSys->cpuInfo.caps;
    pCpuInfoParams->clock = pSys->cpuInfo.clock;
    pCpuInfoParams->L1DataCacheSize = pSys->cpuInfo.l1DataCacheSize;
    pCpuInfoParams->L2DataCacheSize = pSys->cpuInfo.l2DataCacheSize;
    pCpuInfoParams->dataCacheLineSize = pSys->cpuInfo.dataCacheLineSize;
    pCpuInfoParams->numLogicalCpus = pSys->cpuInfo.numLogicalCpus;
    pCpuInfoParams->numPhysicalCpus = pSys->cpuInfo.numPhysicalCpus;
    pCpuInfoParams->coresOnDie = pSys->cpuInfo.coresOnDie;
    pCpuInfoParams->family = pSys->cpuInfo.family;
    pCpuInfoParams->model = pSys->cpuInfo.model;
    pCpuInfoParams->stepping = pSys->cpuInfo.stepping;
    portMemCopy(pCpuInfoParams->name,
                sizeof (pCpuInfoParams->name), pSys->cpuInfo.name,
                sizeof (pCpuInfoParams->name));

    return NV_OK;
}

//
// cliresCtrlCmdSystemSetMemorySize
//
// Set system memory size in pages.
//
// Lock Requirements:
//      Assert that API and GPUs locks held on entry
//
NV_STATUS
cliresCtrlCmdSystemSetMemorySize_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_SYSTEM_SET_MEMORY_SIZE_PARAMS *pParams
)
{
    OBJSYS *pSys = SYS_GET_INSTANCE();
    OBJOS *pOS = SYS_GET_OS(pSys);

    LOCK_ASSERT_AND_RETURN(rmApiLockIsOwner() && rmGpuLockIsOwner());

    pOS->SystemMemorySize = pParams->memorySize;

    return NV_OK;
}

static NV_STATUS
classGetSystemClasses(NV0000_CTRL_SYSTEM_GET_CLASSLIST_PARAMS *pParams)
{
    NvU32 i;
    NvU32 numResources;
    const RS_RESOURCE_DESC *resources;
    NV0000_CTRL_SYSTEM_GET_CLASSLIST_PARAMS params;

    NV_ASSERT_OR_RETURN(pParams, NV_ERR_INVALID_ARGUMENT);

    RsResInfoGetResourceList(&resources, &numResources);

    portMemSet(&params, 0x0, sizeof(params));

    for (i = 0; i < numResources; i++)
    {
        if ((resources[i].pParentList[0] == classId(RmClientResource)) &&
            (resources[i].pParentList[1] == 0x0))
        {
            NV_ASSERT_OR_RETURN(params.numClasses < NV0000_CTRL_SYSTEM_MAX_CLASSLIST_SIZE,
                                NV_ERR_INVALID_STATE);

            params.classes[params.numClasses] = resources[i].externalClassId;
            params.numClasses++;
        }
    }

    portMemCopy(pParams, sizeof(*pParams), &params, sizeof(params));

    return NV_OK;
}

//
// cliresCtrlCmdSystemGetClassList
//
// Get list of supported system classes.
//
// Lock Requirements:
//      Assert that API and GPUs locks held on entry
//
NV_STATUS
cliresCtrlCmdSystemGetClassList_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_SYSTEM_GET_CLASSLIST_PARAMS *pParams
)
{
    NV_STATUS status;

    LOCK_ASSERT_AND_RETURN(rmApiLockIsOwner() && rmGpuLockIsOwner());

    status = classGetSystemClasses(pParams);

    return status;
}

//
// cliresCtrlCmdSystemNotifyEvent
//
// This function exists to allow the RM Client to notify us when they receive
// a system event message.  We generally will store off the data, but in some
// cases, we'll trigger our own handling of that code.  Prior to Vista, we
// would just poll a scratch bit for these events.  But for Vista, we get them
// directly from the OS.
//
// Added Support for notifying power change event to perfhandler
//
NV_STATUS
cliresCtrlCmdSystemNotifyEvent_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_SYSTEM_NOTIFY_EVENT_PARAMS *pParams
)
{
    NV_STATUS   status    = NV_OK;

    switch(pParams->eventType)
    {
        case NV0000_CTRL_SYSTEM_EVENT_TYPE_LID_STATE:
        case NV0000_CTRL_SYSTEM_EVENT_TYPE_DOCK_STATE:
        case NV0000_CTRL_SYSTEM_EVENT_TYPE_TRUST_LID:
        case NV0000_CTRL_SYSTEM_EVENT_TYPE_TRUST_DOCK:
        {
            status = NV_ERR_NOT_SUPPORTED;
            break;
        }

        case NV0000_CTRL_SYSTEM_EVENT_TYPE_POWER_SOURCE:
            status = NV_ERR_NOT_SUPPORTED;
            break;

        default:
            status = NV_ERR_INVALID_ARGUMENT;
            break;
    }

    return status;
}

NV_STATUS
cliresCtrlCmdSystemDebugCtrlRmMsg_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_SYSTEM_DEBUG_RMMSG_CTRL_PARAMS *pParams
)
{
// NOTE: RmMsg is only available when NV_PRINTF_STRINGS_ALLOWED is true.
#if NV_PRINTF_STRINGS_ALLOWED
    NvU32 len = 0;

    extern char RmMsg[NV0000_CTRL_SYSTEM_DEBUG_RMMSG_SIZE];

    switch (pParams->cmd)
    {
        case NV0000_CTRL_SYSTEM_DEBUG_RMMSG_CTRL_CMD_GET:
        {
            len = (NvU32)portStringLength(RmMsg);
            portMemCopy(pParams->data, len, RmMsg, len);
            pParams->count = len;
            break;
        }
        case NV0000_CTRL_SYSTEM_DEBUG_RMMSG_CTRL_CMD_SET:
        {
#if !(defined(DEBUG) || defined(DEVELOP))
            RmClient *pRmClient = dynamicCast(RES_GET_CLIENT(pRmCliRes), RmClient);
            CALL_CONTEXT *pCallContext = resservGetTlsCallContext();

            NV_ASSERT_OR_RETURN(pCallContext != NULL, NV_ERR_INVALID_STATE);
            NV_ASSERT_OR_RETURN(pRmClient != NULL, NV_ERR_INVALID_CLIENT);

            if (!rmclientIsAdmin(pRmClient, pCallContext->secInfo.privLevel))
            {
                NV_PRINTF(LEVEL_WARNING, "Non-privileged context issued privileged cmd\n");
                return NV_ERR_INSUFFICIENT_PERMISSIONS;
            }
#endif
            portMemCopy(RmMsg, NV0000_CTRL_SYSTEM_DEBUG_RMMSG_SIZE, pParams->data, NV0000_CTRL_SYSTEM_DEBUG_RMMSG_SIZE);
            break;
        }
        default:
            return NV_ERR_INVALID_ARGUMENT;
            break;
    }

    return NV_OK;
#else
    return NV_ERR_NOT_SUPPORTED;
#endif
}

NV_STATUS
cliresCtrlCmdSystemGetRmInstanceId_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_SYSTEM_GET_RM_INSTANCE_ID_PARAMS *pRmInstanceIdParams
)
{
    OBJSYS *pSys = SYS_GET_INSTANCE();
    pRmInstanceIdParams->rm_instance_id = pSys->rmInstanceId;

    return NV_OK;
}

//
// cliresCtrlCmdGpuGetAttachedIds
//
// Lock Requirements:
//      Assert that API lock held on entry
//      No GPUs lock
//
NV_STATUS
cliresCtrlCmdGpuGetAttachedIds_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_GPU_GET_ATTACHED_IDS_PARAMS *pGpuAttachedIds
)
{
    LOCK_ASSERT_AND_RETURN(rmApiLockIsOwner());

    return gpumgrGetAttachedGpuIds(pGpuAttachedIds);
}

//
// cliresCtrlCmdGpuGetIdInfo
//
// Lock Requirements:
//      Assert that API lock and Gpus lock held on entry
//
NV_STATUS
cliresCtrlCmdGpuGetIdInfo_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_GPU_GET_ID_INFO_PARAMS *pGpuIdInfoParams
)
{
    LOCK_ASSERT_AND_RETURN(rmApiLockIsOwner());

    return gpumgrGetGpuIdInfo(pGpuIdInfoParams);
}

NV_STATUS
cliresCtrlCmdGpuGetIdInfoV2_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_GPU_GET_ID_INFO_V2_PARAMS *pGpuIdInfoParams
)
{
    LOCK_ASSERT_AND_RETURN(rmApiLockIsOwner());

    return gpumgrGetGpuIdInfoV2(pGpuIdInfoParams);
}

//
// cliresCtrlCmdGpuGetInitStatus
//
// Lock Requirements:
//      Assert that API lock held on entry
//      No GPUs lock
//
NV_STATUS
cliresCtrlCmdGpuGetInitStatus_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_GPU_GET_INIT_STATUS_PARAMS *pGpuInitStatusParams
)
{
    LOCK_ASSERT_AND_RETURN(rmApiLockIsOwner());

    return gpumgrGetGpuInitStatus(pGpuInitStatusParams);
}

//
// cliresCtrlCmdGpuGetDeviceIds
//
// Lock Requirements:
//      Assert that API lock held on entry
//      No GPUs lock
//
NV_STATUS
cliresCtrlCmdGpuGetDeviceIds_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_GPU_GET_DEVICE_IDS_PARAMS *pDeviceIdsParams
)
{
    LOCK_ASSERT_AND_RETURN(rmApiLockIsOwner());

    pDeviceIdsParams->deviceIds = gpumgrGetDeviceInstanceMask();

    return NV_OK;
}

//
// cliresCtrlCmdGpuGetPciInfo
//
// Lock Requirements:
//      Assert that API lock held on entry
//      No GPUs lock
//
NV_STATUS
cliresCtrlCmdGpuGetPciInfo_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_GPU_GET_PCI_INFO_PARAMS *pPciInfoParams
)
{
    NV_STATUS status;
    NvU64 gpuDomainBusDevice;

    NV_ASSERT(rmApiLockIsOwner());

    status = gpumgrGetProbedGpuDomainBusDevice(pPciInfoParams->gpuId, &gpuDomainBusDevice);
    if (status != NV_OK)
        return status;

    pPciInfoParams->domain = gpuDecodeDomain(gpuDomainBusDevice);
    pPciInfoParams->bus = gpuDecodeBus(gpuDomainBusDevice);
    pPciInfoParams->slot = gpuDecodeDevice(gpuDomainBusDevice);

    return NV_OK;
}

//
// cliresCtrlCmdGpuGetProbedIds
//
// Lock Requirements:
//      Assert that API lock held on entry
//      No GPUs lock
//
NV_STATUS
cliresCtrlCmdGpuGetProbedIds_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_GPU_GET_PROBED_IDS_PARAMS *pGpuProbedIds
)
{
    LOCK_ASSERT_AND_RETURN(rmApiLockIsOwner());

    return gpumgrGetProbedGpuIds(pGpuProbedIds);
}

//
// cliresCtrlCmdGpuAttachIds
//
// Lock Requirements:
//      Assert that API lock held on entry
//      No GPUs lock
//
NV_STATUS
cliresCtrlCmdGpuAttachIds_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_GPU_ATTACH_IDS_PARAMS *pGpuAttachIds
)
{
    NV0000_CTRL_GPU_GET_PROBED_IDS_PARAMS *pGpuProbedIds = NULL;
    NvU32 i, j;
    NV_STATUS status = NV_OK;

    LOCK_ASSERT_AND_RETURN(rmApiLockIsOwner());

    if (pGpuAttachIds->gpuIds[0] == NV0000_CTRL_GPU_ATTACH_ALL_PROBED_IDS)
    {
        // XXX add callback to attach logic on Windows
        status = NV_OK;
        goto done;
    }

    pGpuProbedIds = portMemAllocNonPaged(sizeof(*pGpuProbedIds));
    if (pGpuProbedIds == NULL)
    {
        status = NV_ERR_NO_MEMORY;
        goto done;
    }

    status = gpumgrGetProbedGpuIds(pGpuProbedIds);
    if (status != NV_OK)
    {
        goto done;
    }

    for (i = 0; (i < NV0000_CTRL_GPU_MAX_PROBED_GPUS) &&
                (pGpuAttachIds->gpuIds[i] != NV0000_CTRL_GPU_INVALID_ID); i++)
    {
        for (j = 0; (j < NV0000_CTRL_GPU_MAX_PROBED_GPUS) &&
                    (pGpuProbedIds->gpuIds[j] != NV0000_CTRL_GPU_INVALID_ID); j++)
        {
            if (pGpuAttachIds->gpuIds[i] == pGpuProbedIds->gpuIds[j])
                break;
        }

        if ((j == NV0000_CTRL_GPU_MAX_PROBED_GPUS) ||
            (pGpuProbedIds->gpuIds[j] == NV0000_CTRL_GPU_INVALID_ID))
        {
            status = NV_ERR_INVALID_ARGUMENT;
            break;
        }
    }

    // XXX add callback to attach logic on Windows
done:
    portMemFree(pGpuProbedIds);
    return status;
}

//
// cliresCtrlCmdGpuDetachIds
//
// Lock Requirements:
//      Assert that API lock held on entry
//      No GPUs lock
//
NV_STATUS
cliresCtrlCmdGpuDetachIds_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_GPU_DETACH_IDS_PARAMS *pGpuDetachIds
)
{
    NV0000_CTRL_GPU_GET_ATTACHED_IDS_PARAMS *pGpuAttachedIds = NULL;
    NvU32 i, j;
    NV_STATUS status = NV_OK;

    LOCK_ASSERT_AND_RETURN(rmApiLockIsOwner());

    if (pGpuDetachIds->gpuIds[0] == NV0000_CTRL_GPU_DETACH_ALL_ATTACHED_IDS)
    {
        // XXX add callback to detach logic on Windows
        status = NV_OK;
        goto done;
    }
    else
    {
        pGpuAttachedIds = portMemAllocNonPaged(sizeof(*pGpuAttachedIds));
        if (pGpuAttachedIds == NULL)
        {
            status = NV_ERR_NO_MEMORY;
            goto done;
        }

        status = gpumgrGetAttachedGpuIds(pGpuAttachedIds);
        if (status != NV_OK)
        {
            goto done;
        }

        for (i = 0; (i < NV0000_CTRL_GPU_MAX_ATTACHED_GPUS) &&
                    (pGpuDetachIds->gpuIds[i] != NV0000_CTRL_GPU_INVALID_ID); i++)
        {
            for (j = 0; (j < NV0000_CTRL_GPU_MAX_ATTACHED_GPUS) &&
                        (pGpuAttachedIds->gpuIds[j] != NV0000_CTRL_GPU_INVALID_ID); j++)
            {
                if (pGpuDetachIds->gpuIds[i] == pGpuAttachedIds->gpuIds[j])
                    break;
            }

            if ((j == NV0000_CTRL_GPU_MAX_ATTACHED_GPUS) ||
                (pGpuAttachedIds->gpuIds[j] == NV0000_CTRL_GPU_INVALID_ID))
            {
                status = NV_ERR_INVALID_ARGUMENT;
                break;
            }
            else
            {
                // XXX add callback to detach logic on Windows
                break;
            }
        }
    }

done:
    portMemFree(pGpuAttachedIds);
    return status;
}

NV_STATUS
cliresCtrlCmdGpuGetSvmSize_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_GPU_GET_SVM_SIZE_PARAMS *pSvmSizeGetParams
)
{
    OBJGPU *pGpu = NULL;

    // error check incoming gpu id
    pGpu = gpumgrGetGpuFromId(pSvmSizeGetParams->gpuId);
    if (pGpu == NULL)
    {
        NV_PRINTF(LEVEL_WARNING, "GET_SVM_SIZE: bad gpuid: 0x%x\n",
                  pSvmSizeGetParams->gpuId);
        return NV_ERR_INVALID_ARGUMENT;
    }

    // Get the SVM size in MB.
    pSvmSizeGetParams->svmSize = 0;
    return NV_OK;
}

NV_STATUS
cliresCtrlCmdGsyncGetAttachedIds_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_GSYNC_GET_ATTACHED_IDS_PARAMS *pGsyncAttachedIds
)
{
    NvU32 i;

    for (i = 0; i < NV_ARRAY_ELEMENTS32(pGsyncAttachedIds->gsyncIds); i++)
    {
        pGsyncAttachedIds->gsyncIds[i] = NV0000_CTRL_GSYNC_INVALID_ID;
    }

    return NV_OK;
}

NV_STATUS
cliresCtrlCmdGsyncGetIdInfo_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_GSYNC_GET_ID_INFO_PARAMS *pGsyncIdInfoParams
)
{
    return NV_ERR_NOT_SUPPORTED;
}

NV_STATUS
cliresCtrlCmdEventSetNotification_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_EVENT_SET_NOTIFICATION_PARAMS *pEventSetNotificationParams
)
{
    NvHandle hClient = RES_GET_CLIENT_HANDLE(pRmCliRes);

    return CliControlSystemEvent(hClient, pEventSetNotificationParams->event, pEventSetNotificationParams->action);
}

NV_STATUS
cliresCtrlCmdEventGetSystemEventStatus_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_GET_SYSTEM_EVENT_STATUS_PARAMS *pSystemEventStatusParams
)
{
    NvHandle hClient = RES_GET_CLIENT_HANDLE(pRmCliRes);

    return CliGetSystemEventStatus(hClient, &pSystemEventStatusParams->event, &pSystemEventStatusParams->status);
}


NV_STATUS
cliresCtrlCmdSystemGetPrivilegedStatus_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_SYSTEM_GET_PRIVILEGED_STATUS_PARAMS *pParams
)
{
    RmClient *pClient = dynamicCast(RES_GET_CLIENT(pRmCliRes), RmClient);
    NvU8     privStatus = 0;
    CALL_CONTEXT *pCallContext = resservGetTlsCallContext();

    NV_ASSERT_OR_RETURN(RMCFG_FEATURE_KERNEL_RM, NV_ERR_NOT_SUPPORTED);
    NV_ASSERT_OR_RETURN (pClient != NULL, NV_ERR_INVALID_CLIENT);

    if (pCallContext->secInfo.privLevel >= RS_PRIV_LEVEL_KERNEL)
    {
        privStatus |= NV0000_CTRL_SYSTEM_GET_PRIVILEGED_STATUS_KERNEL_HANDLE_FLAG;
    }

    if (pCallContext->secInfo.privLevel >= RS_PRIV_LEVEL_USER_ROOT)
    {
        privStatus |= NV0000_CTRL_SYSTEM_GET_PRIVILEGED_STATUS_PRIV_USER_FLAG;
    }

    if (rmclientIsAdmin(pClient, pCallContext->secInfo.privLevel))
    {
        privStatus |= NV0000_CTRL_SYSTEM_GET_PRIVILEGED_STATUS_PRIV_HANDLE_FLAG;
    }

    pParams->privStatusFlags = privStatus;

    return NV_OK;
}

NV_STATUS
cliresCtrlCmdSystemGetFabricStatus_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_SYSTEM_GET_FABRIC_STATUS_PARAMS *pParams
)
{
    OBJSYS *pSys = SYS_GET_INSTANCE();
    NvU32 fabricStatus = NV0000_CTRL_GET_SYSTEM_FABRIC_STATUS_SKIP;

    if (pSys->getProperty(pSys, PDB_PROP_SYS_FABRIC_IS_EXTERNALLY_MANAGED))
    {
        fabricStatus = NV0000_CTRL_GET_SYSTEM_FABRIC_STATUS_UNINITIALIZED;

        if (pSys->getProperty(pSys, PDB_PROP_SYS_FABRIC_MANAGER_IS_REGISTERED))
        {
            fabricStatus = NV0000_CTRL_GET_SYSTEM_FABRIC_STATUS_IN_PROGRESS;
        }

        if (pSys->getProperty(pSys, PDB_PROP_SYS_FABRIC_MANAGER_IS_INITIALIZED))
        {
            fabricStatus = NV0000_CTRL_GET_SYSTEM_FABRIC_STATUS_INITIALIZED;
        }
    }

    pParams->fabricStatus = fabricStatus;

    return NV_OK;
}

NV_STATUS
cliresCtrlCmdGpuGetUuidInfo_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_GPU_GET_UUID_INFO_PARAMS *pParams
)
{
    OBJGPU *pGpu = NULL;

    pGpu = gpumgrGetGpuFromUuid(pParams->gpuUuid, pParams->flags);

    if (NULL == pGpu)
        return NV_ERR_OBJECT_NOT_FOUND;

    pParams->gpuId = pGpu->gpuId;
    pParams->deviceInstance = gpuGetDeviceInstance(pGpu);
    pParams->subdeviceInstance = gpumgrGetSubDeviceInstanceFromGpu(pGpu);

    return NV_OK;
}

NV_STATUS
cliresCtrlCmdGpuGetUuidFromGpuId_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_GPU_GET_UUID_FROM_GPU_ID_PARAMS *pParams
)
{
    OBJGPU   *pGpu = NULL;
    NvU8     *pGidString = NULL;
    NvU32     gidStrLen = 0;
    NV_STATUS rmStatus;

    // First check for UUID cached by gpumgr
    rmStatus = gpumgrGetGpuUuidInfo(pParams->gpuId, &pGidString, &gidStrLen, pParams->flags);

    if (rmStatus != NV_OK)
    {
        // If UUID not cached by gpumgr then try to query device
        pGpu = gpumgrGetGpuFromId(pParams->gpuId);

        if (NULL == pGpu)
            return NV_ERR_OBJECT_NOT_FOUND;

        // get the UUID of this GPU
        rmStatus = gpuGetGidInfo(pGpu, &pGidString, &gidStrLen, pParams->flags);
        if (rmStatus != NV_OK)
        {
            NV_PRINTF(LEVEL_WARNING,
                      "gpumgrGetGpuInfo: getting gpu GUID failed\n");
            return rmStatus;
        }
    }

    if (gidStrLen <= NV0000_GPU_MAX_GID_LENGTH)
    {
        portMemCopy(pParams->gpuUuid, gidStrLen, pGidString, gidStrLen);
        pParams->uuidStrLen = gidStrLen;
    }

    // cleanup the allocated gidstring
    portMemFree(pGidString);

    return NV_OK;
}

NV_STATUS
cliresCtrlCmdGpuModifyGpuDrainState_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_GPU_MODIFY_DRAIN_STATE_PARAMS *pParams
)
{
    NV_STATUS status;
    NvBool    bEnable;
    NvBool    bRemove = NV_FALSE;
    NvBool    bLinkDisable = NV_FALSE;
    OBJGPU   *pGpu = gpumgrGetGpuFromId(pParams->gpuId);

    if (NV0000_CTRL_GPU_DRAIN_STATE_ENABLED == pParams->newState)
    {
        if ((pGpu != NULL) && IsSLIEnabled(pGpu))
        {
            // "drain" state not supported in SLI configurations
            return NV_ERR_NOT_SUPPORTED;
        }

        bEnable = NV_TRUE;
        bRemove =
            ((pParams->flags & NV0000_CTRL_GPU_DRAIN_STATE_FLAG_REMOVE_DEVICE) != 0);
        bLinkDisable =
            ((pParams->flags & NV0000_CTRL_GPU_DRAIN_STATE_FLAG_LINK_DISABLE) != 0);

        if (bLinkDisable && !bRemove)
        {
            return NV_ERR_INVALID_ARGUMENT;
        }
    }
    else if (NV0000_CTRL_GPU_DRAIN_STATE_DISABLED ==
            pParams->newState)
    {
        bEnable = NV_FALSE;
    }
    else
    {
        return NV_ERR_INVALID_ARGUMENT;
    }

    // Set/Clear GPU manager drain state
    status = gpumgrModifyGpuDrainState(pParams->gpuId, bEnable, bRemove, bLinkDisable);

    return status;
}

NV_STATUS
cliresCtrlCmdGpuQueryGpuDrainState_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_GPU_QUERY_DRAIN_STATE_PARAMS *pParams
)
{
    NvBool    bDrainState;
    NvBool    bRemove;
    NV_STATUS status;

    status = gpumgrQueryGpuDrainState(pParams->gpuId, &bDrainState, &bRemove);

    if (status != NV_OK)
    {
        return status;
    }

    pParams->drainState = bDrainState ? NV0000_CTRL_GPU_DRAIN_STATE_ENABLED
                                      : NV0000_CTRL_GPU_DRAIN_STATE_DISABLED;

    pParams->flags = bRemove ? NV0000_CTRL_GPU_DRAIN_STATE_FLAG_REMOVE_DEVICE : 0;

    return NV_OK;
}

/*
 * Associate sub process ID with client handle
 *
 * @return 'NV_OK' on success. Otherwise return NV_ERR_INVALID_CLIENT
 */
NV_STATUS
cliresCtrlCmdSetSubProcessID_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_SET_SUB_PROCESS_ID_PARAMS *pParams
)
{
    NvHandle  hClient = RES_GET_CLIENT_HANDLE(pRmCliRes);
    RmClient *pClient;

    if (NV_OK != serverutilGetClientUnderLock(hClient, &pClient))
        return NV_ERR_INVALID_CLIENT;

    pClient->SubProcessID = pParams->subProcessID;
    portStringCopy(pClient->SubProcessName, sizeof(pClient->SubProcessName), pParams->subProcessName, sizeof(pParams->subProcessName));

    return NV_OK;
}

/*
 * Disable USERD isolation among all the sub processes within a user process
 *
 * @return 'NV_OK' on success. Otherwise return NV_ERR_INVALID_CLIENT
 */
NV_STATUS
cliresCtrlCmdDisableSubProcessUserdIsolation_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_DISABLE_SUB_PROCESS_USERD_ISOLATION_PARAMS *pParams
)
{
    NvHandle  hClient = RES_GET_CLIENT_HANDLE(pRmCliRes);
    RmClient *pClient;

    if (NV_OK != serverutilGetClientUnderLock(hClient, &pClient))
        return NV_ERR_INVALID_CLIENT;

    pClient->bIsSubProcessDisabled = pParams->bIsSubProcessDisabled;

    return NV_OK;
}

NV_STATUS
cliresCtrlCmdClientGetAddrSpaceType_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_CLIENT_GET_ADDR_SPACE_TYPE_PARAMS *pParams
)
{
    NvHandle         hClient = RES_GET_CLIENT_HANDLE(pRmCliRes);
    CALL_CONTEXT     callContext;
    RsClient        *pRsClient;
    RsResourceRef   *pResourceRef;
    Memory          *pMemory = NULL;
    GpuResource     *pGpuResource = NULL;
    NV_ADDRESS_SPACE memType;

    NV_ASSERT_OK_OR_RETURN(serverGetClientUnderLock(&g_resServ, hClient, &pRsClient));
    NV_ASSERT_OK_OR_RETURN(clientGetResourceRef(pRsClient, pParams->hObject, &pResourceRef));

    portMemSet(&callContext, 0, sizeof(callContext));
    callContext.pClient = pRsClient;
    callContext.pResourceRef = pResourceRef;

    pMemory = dynamicCast(pResourceRef->pResource, Memory);
    if (pMemory != NULL)
    {
        NV_ASSERT_OK_OR_RETURN(memGetMapAddrSpace(pMemory, &callContext, pParams->mapFlags, &memType));

    }
    else
    {
        pGpuResource = dynamicCast(pResourceRef->pResource, GpuResource);
        if (pGpuResource != NULL)
        {
            NV_ASSERT_OK_OR_RETURN(gpuresGetMapAddrSpace(pGpuResource, &callContext, pParams->mapFlags, &memType));
        }
        else
        {
            return NV_ERR_INVALID_OBJECT;
        }
    }

    switch (memType)
    {
        case ADDR_SYSMEM:
            pParams->addrSpaceType = NV0000_CTRL_CMD_CLIENT_GET_ADDR_SPACE_TYPE_SYSMEM;
            break;
        case ADDR_FBMEM:
            pParams->addrSpaceType = NV0000_CTRL_CMD_CLIENT_GET_ADDR_SPACE_TYPE_VIDMEM;
            break;
        case ADDR_REGMEM:
            pParams->addrSpaceType = NV0000_CTRL_CMD_CLIENT_GET_ADDR_SPACE_TYPE_REGMEM;
            break;
        case ADDR_FABRIC_V2:
            pParams->addrSpaceType = NV0000_CTRL_CMD_CLIENT_GET_ADDR_SPACE_TYPE_FABRIC;
            break;
        case ADDR_FABRIC_MC:
#ifdef NV0000_CTRL_CMD_CLIENT_GET_ADDR_SPACE_TYPE_FABRIC_MC
            pParams->addrSpaceType = NV0000_CTRL_CMD_CLIENT_GET_ADDR_SPACE_TYPE_FABRIC_MC;
            break;
#else
            NV_ASSERT(0);
            return NV_ERR_INVALID_ARGUMENT;
#endif
        case ADDR_VIRTUAL:
            NV_PRINTF(LEVEL_ERROR,
                      "VIRTUAL (0x%x) is not a valid NV0000_CTRL_CMD_CLIENT_GET_ADDR_SPACE_TYPE\n",
                      memType);
            pParams->addrSpaceType = NV0000_CTRL_CMD_CLIENT_GET_ADDR_SPACE_TYPE_INVALID;
            DBG_BREAKPOINT();
            return NV_ERR_INVALID_ARGUMENT;
        default:
            NV_PRINTF(LEVEL_ERROR, "Cannot determine address space 0x%x\n",
                      memType);
            pParams->addrSpaceType = NV0000_CTRL_CMD_CLIENT_GET_ADDR_SPACE_TYPE_INVALID;
            DBG_BREAKPOINT();
            return NV_ERR_INVALID_ARGUMENT;
    }

    return NV_OK;
}

NV_STATUS
cliresCtrlCmdClientGetHandleInfo_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_CLIENT_GET_HANDLE_INFO_PARAMS *pParams
)
{
    NvHandle       hClient = RES_GET_CLIENT_HANDLE(pRmCliRes);
    NV_STATUS      status;
    RsResourceRef *pRsResourceRef;

    status = serverutilGetResourceRef(hClient, pParams->hObject, &pRsResourceRef);
    if (status != NV_OK)
    {
        return status;
    }

    switch (pParams->index)
    {
        case NV0000_CTRL_CMD_CLIENT_GET_HANDLE_INFO_INDEX_PARENT:
            pParams->data.hResult = pRsResourceRef->pParentRef ? pRsResourceRef->pParentRef->hResource : 0;
            break;
        case NV0000_CTRL_CMD_CLIENT_GET_HANDLE_INFO_INDEX_CLASSID:
            pParams->data.iResult = pRsResourceRef->externalClassId;
            break;
        default:
            return NV_ERR_INVALID_ARGUMENT;
    }

    return NV_OK;
}

NV_STATUS
cliresCtrlCmdClientGetAccessRights_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_CLIENT_GET_ACCESS_RIGHTS_PARAMS *pParams
)
{
    NV_STATUS      status;
    RsResourceRef *pRsResourceRef;
    RsResourceRef *pClientRef = RES_GET_REF(pRmCliRes);
    RsClient      *pClient = pClientRef->pClient;

    status = serverutilGetResourceRef(pParams->hClient, pParams->hObject, &pRsResourceRef);
    if (status != NV_OK)
    {
        return status;
    }

    rsAccessUpdateRights(pRsResourceRef, pClient, NULL);

    rsAccessGetAvailableRights(pRsResourceRef, pClient, &pParams->maskResult);

    return NV_OK;
}

NV_STATUS
cliresCtrlCmdClientSetInheritedSharePolicy_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_CLIENT_SET_INHERITED_SHARE_POLICY_PARAMS *pParams
)
{
    NV0000_CTRL_CLIENT_SHARE_OBJECT_PARAMS params;

    portMemSet(&params, 0, sizeof(params));
    params.sharePolicy = pParams->sharePolicy;
    params.hObject = RES_GET_REF(pRmCliRes)->hResource;

    return cliresCtrlCmdClientShareObject(pRmCliRes, &params);
}

NV_STATUS
cliresCtrlCmdClientShareObject_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_CLIENT_SHARE_OBJECT_PARAMS *pParams
)
{
    RS_SHARE_POLICY *pSharePolicy = &pParams->sharePolicy;
    RsClient        *pClient = RES_GET_CLIENT(pRmCliRes);
    RsResourceRef   *pObjectRef;

    CALL_CONTEXT  callContext;
    CALL_CONTEXT *pOldCallContext;

    NV_STATUS status;

    if (pSharePolicy->type >= RS_SHARE_TYPE_MAX)
        return NV_ERR_INVALID_ARGUMENT;

    status = clientGetResourceRef(pClient, pParams->hObject, &pObjectRef);
    if (status != NV_OK)
        return status;

    CALL_CONTEXT *pCallContext = resservGetTlsCallContext();

    portMemSet(&callContext, 0, sizeof(callContext));
    callContext.pServer = &g_resServ;
    callContext.pClient = pClient;
    callContext.pResourceRef = pObjectRef;
    callContext.secInfo = pCallContext->secInfo;

    resservSwapTlsCallContext(&pOldCallContext, &callContext);
    status = clientShareResource(pClient, pObjectRef, pSharePolicy, &callContext);
    resservRestoreTlsCallContext(pOldCallContext);
    if (status != NV_OK)
        return status;

    //
    // Above clientShareResource does everything needed for normal sharing,
    // but we may still need to add a backref if we're sharing with a client,
    // to prevent stale access.
    //
    if (!(pSharePolicy->action & RS_SHARE_ACTION_FLAG_REVOKE) &&
        (pSharePolicy->type == RS_SHARE_TYPE_CLIENT))
    {
        RsClient *pClientTarget;

        // Trying to share with self, nothing to do.
        if (pSharePolicy->target == pClient->hClient)
            return NV_OK;

        status = serverGetClientUnderLock(&g_resServ, pSharePolicy->target, &pClientTarget);
        if (status != NV_OK)
            return status;

        status = clientAddAccessBackRef(pClientTarget, pObjectRef);
        if (status != NV_OK)
            return status;
    }

    return status;
}

NV_STATUS
cliresCtrlCmdClientGetChildHandle_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_CMD_CLIENT_GET_CHILD_HANDLE_PARAMS *pParams
)
{
    NvHandle       hClient = RES_GET_CLIENT_HANDLE(pRmCliRes);
    NV_STATUS      status;
    RsResourceRef *pParentRef;
    RsResourceRef *pResourceRef;

    status = serverutilGetResourceRef(hClient, pParams->hParent, &pParentRef);
    if (status != NV_OK)
    {
        return status;
    }

    status = refFindChildOfType(pParentRef, pParams->classId, NV_TRUE, &pResourceRef);
    if (status == NV_OK)
    {
        pParams->hObject = pResourceRef ? pResourceRef->hResource : 0;
    }
    return status;
}

NV_STATUS
cliresCtrlCmdGpuGetMemOpEnable_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_GPU_GET_MEMOP_ENABLE_PARAMS *pMemOpEnableParams
)
{
    OBJSYS   *pSys = SYS_GET_INSTANCE();
    NV_STATUS status = NV_OK;

    pMemOpEnableParams->enableMask = 0;

    if (pSys->getProperty(pSys, PDB_PROP_SYS_ENABLE_STREAM_MEMOPS))
    {
        NV_PRINTF(LEVEL_INFO, "MemOpOverride enabled\n");
        pMemOpEnableParams->enableMask = NV0000_CTRL_GPU_FLAGS_MEMOP_ENABLE;
    }

    return status;
}

NV_STATUS
cliresCtrlCmdGpuDisableNvlinkInit_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_GPU_DISABLE_NVLINK_INIT_PARAMS *pParams
)
{
    NvHandle hClient = RES_GET_CLIENT_HANDLE(pRmCliRes);
    CALL_CONTEXT *pCallContext = resservGetTlsCallContext();

    NV_ASSERT_OR_RETURN(RMCFG_FEATURE_KERNEL_RM, NV_ERR_NOT_SUPPORTED);
    NV_ASSERT_OR_RETURN(pCallContext != NULL, NV_ERR_INVALID_STATE);

    if (!rmclientIsCapableOrAdminByHandle(hClient,
                                          NV_RM_CAP_EXT_FABRIC_MGMT,
                                          pCallContext->secInfo.privLevel))
    {
        NV_PRINTF(LEVEL_WARNING, "Non-privileged context issued privileged cmd\n");
        return NV_ERR_INSUFFICIENT_PERMISSIONS;
    }

    if (pParams->gpuId == NV0000_CTRL_GPU_INVALID_ID)
    {
        return NV_ERR_INVALID_ARGUMENT;
    }

    return gpumgrSetGpuInitDisabledNvlinks(pParams->gpuId, pParams->mask, pParams->bSkipHwNvlinkDisable);
}

NV_STATUS
cliresCtrlCmdLegacyConfig_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_GPU_LEGACY_CONFIG_PARAMS *pParams
)
{
    NvHandle      hClient            = RES_GET_CLIENT_HANDLE(pRmCliRes);
    RsClient     *pClient            = RES_GET_CLIENT(pRmCliRes);
    RmClient     *pRmClient          = dynamicCast(pClient, RmClient);
    NvHandle      hDeviceOrSubdevice = pParams->hContext;
    NvHandle      hDevice;
    OBJGPU       *pGpu;
    GpuResource  *pGpuResource;
    NV_STATUS     rmStatus           = NV_OK;
    CALL_CONTEXT *pCallContext       = resservGetTlsCallContext();

    NV_ASSERT_OR_RETURN(RMCFG_FEATURE_KERNEL_RM, NV_ERR_NOT_SUPPORTED);
    NV_ASSERT_OR_RETURN(pCallContext != NULL, NV_ERR_INVALID_STATE);
    NV_ASSERT_OR_RETURN(pRmClient != NULL, NV_ERR_INVALID_CLIENT);

    //
    // Clients pass in device or subdevice as context for NvRmConfigXyz.
    //
    rmStatus = gpuresGetByDeviceOrSubdeviceHandle(pClient,
                                                  hDeviceOrSubdevice,
                                                  &pGpuResource);
    if (rmStatus != NV_OK)
        return rmStatus;

    hDevice = RES_GET_HANDLE(GPU_RES_GET_DEVICE(pGpuResource));
    pGpu    = GPU_RES_GET_GPU(pGpuResource);

    //
    // GSP client builds should have these legacy APIs disabled,
    // but a monolithic build running in offload mode can still reach here,
    // so log those cases and bail early to keep the same behavior.
    //
    NV_ASSERT_OR_RETURN(!IS_GSP_CLIENT(pGpu), NV_ERR_NOT_SUPPORTED);

    GPU_RES_SET_THREAD_BC_STATE(pGpuResource);

    pParams->dataType = pParams->opType;

    switch (pParams->opType)
    {
        default:
            PORT_UNREFERENCED_VARIABLE(pGpu);
            PORT_UNREFERENCED_VARIABLE(hDevice);
            PORT_UNREFERENCED_VARIABLE(hClient);
            rmStatus = NV_ERR_NOT_SUPPORTED;
            break;
    }

    return rmStatus;
}

NV_STATUS
cliresCtrlCmdSystemSyncExternalFabricMgmt_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_CMD_SYSTEM_SYNC_EXTERNAL_FABRIC_MGMT_PARAMS *pExtFabricMgmtParams
)
{
    OBJSYS *pSys = SYS_GET_INSTANCE();

    pSys->setProperty(pSys, PDB_PROP_SYS_FABRIC_IS_EXTERNALLY_MANAGED,
                      pExtFabricMgmtParams->bExternalFabricMgmt);
    return NV_OK;
}

NV_STATUS cliresCtrlCmdSystemGetClientDatabaseInfo_IMPL
(
    RmClientResource *pRmCliRes,
    NV0000_CTRL_SYSTEM_GET_CLIENT_DATABASE_INFO_PARAMS *pParams
)
{
    pParams->clientCount = g_resServ.activeClientCount;
    pParams->resourceCount = g_resServ.activeResourceCount;
    return NV_OK;
}

