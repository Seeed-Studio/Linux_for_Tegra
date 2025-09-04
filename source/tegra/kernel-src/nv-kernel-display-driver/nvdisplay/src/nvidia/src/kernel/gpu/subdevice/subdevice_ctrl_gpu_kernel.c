/*
 * SPDX-FileCopyrightText: Copyright (c) 2004-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

/*!
 * @file
 * @brief This module contains the gpu control interfaces for the
 *        subdevice (NV20_SUBDEVICE_0) class. Subdevice-level control calls
 *        are directed unicast to the associated GPU.
 *        File contains ctrls related to general GPU
 */

#include "core/core.h"
#include "core/locks.h"
#include "gpu/subdevice/subdevice.h"
#include "gpu/gpu.h"
#include "gpu_mgr/gpu_db.h"
#include "nvrm_registry.h"
#include "nvVer.h"
#include "objtmr.h"
#include "vgpu/rpc.h"
#include "gpu/mem_mgr/mem_mgr.h"

#include "rmapi/resource_fwd_decls.h"
#include "rmapi/client.h"

#include "class/cl900e.h"




static NV_STATUS
getGpuInfos(Subdevice *pSubdevice, NV2080_CTRL_GPU_GET_INFO_V2_PARAMS *pParams, NvBool bCanAccessHw)
{
    return NV_OK;
}

NV_STATUS
subdeviceCtrlCmdGpuGetInfoV2_IMPL
(
    Subdevice *pSubdevice,
    NV2080_CTRL_GPU_GET_INFO_V2_PARAMS *pGpuInfoParams
)
{
    return getGpuInfos(pSubdevice, pGpuInfoParams, NV_TRUE);
}

//
// subdeviceCtrlCmdGpuGetCachedInfo: As subdeviceCtrlCmdGpuGetInfoV2, except
// does not perform any HW access (NO_GPUS_ACCESS and NO_GPUS_LOCK flags)
//
NV_STATUS
subdeviceCtrlCmdGpuGetCachedInfo_IMPL
(
    Subdevice *pSubdevice,
    NV2080_CTRL_GPU_GET_INFO_V2_PARAMS *pGpuInfoParams
)
{
    return getGpuInfos(pSubdevice, pGpuInfoParams, NV_FALSE);
}

/*!
 * @brief This command can be used for Optimus enabled system.
 *
 * @return :
 *    NV_OK
 */
NV_STATUS
subdeviceCtrlCmdGpuSetOptimusInfo_IMPL
(
    Subdevice *pSubdevice,
    NV2080_CTRL_GPU_OPTIMUS_INFO_PARAMS *pGpuOptimusInfoParams
)
{
    NvU32   status  =   NV_OK;
    OBJGPU *pGpu    =   GPU_RES_GET_GPU(pSubdevice);

    if (pGpuOptimusInfoParams->isOptimusEnabled)
    {
        //
        // Setting pMemoryManager->bPersistentStandbyBuffer for Optimus system.
        // It is used for sys_mem allocation which is pinned across
        // S3 transitions.Sys_mem allocations are done at first S3 cycle
        // and release during driver unload, which reduces system
        // VM fragmentation, which was a problem in optimus system.
        // For more details refer bug 754122.
        //
        GPU_GET_MEMORY_MANAGER(pGpu)->bPersistentStandbyBuffer = NV_TRUE;
    }
    return status;
}

//
// subdeviceCtrlCmdGpuGetSdm
//
// Lock Requirements:
//      Assert that API lock held on entry
//
NV_STATUS
subdeviceCtrlCmdGpuGetSdm_IMPL
(
    Subdevice *pSubdevice,
    NV2080_CTRL_GPU_GET_SDM_PARAMS *pSdmParams
)
{
    OBJGPU *pGpu = GPU_RES_GET_GPU(pSubdevice);

    LOCK_ASSERT_AND_RETURN(rmApiLockIsOwner());

    pSdmParams->subdeviceMask = gpuGetSubdeviceMask(pGpu);

    return NV_OK;
}

//
// subdeviceCtrlCmdGpuSetSdm
//
// Lock Requirements:
//      Assert that API lock held on entry
//
NV_STATUS
subdeviceCtrlCmdGpuSetSdm_IMPL
(
    Subdevice* pSubdevice,
    NV2080_CTRL_GPU_SET_SDM_PARAMS* pSdmParams
)
{
    OBJGPU *pGpu = GPU_RES_GET_GPU(pSubdevice);
    NvU32   subdeviceInstance;

    LOCK_ASSERT_AND_RETURN(rmApiLockIsOwner());

    if (!ONEBITSET(pSdmParams->subdeviceMask))
    {
        NV_PRINTF(LEVEL_ERROR, "Subdevice mask has none or more than one bit set");
        return NV_ERR_INVALID_DATA;
    }

    if (gpuIsStateLoaded(pGpu))
    {
        NV_PRINTF(LEVEL_ERROR, "NV2080_CTRL_CMD_GPU_SET_SDM cannot be called after the GPU is loaded");
        return NV_ERR_INVALID_STATE;
    }
    subdeviceInstance = BIT_IDX_32(pSdmParams->subdeviceMask);

    if (subdeviceInstance >= NV_MAX_SUBDEVICES)
    {
        NV_PRINTF(LEVEL_ERROR, "Subdevice mask exceeds the max count of subdevices");
        return NV_ERR_INVALID_DATA;
    }
    pGpu->subdeviceInstance = subdeviceInstance;

    return NV_OK;
}

//
// subdeviceCtrlCmdGpuGetSimulationInfo
//
// Lock Requirements:
//      Assert that API lock held on entry
//
NV_STATUS
subdeviceCtrlCmdGpuGetSimulationInfo_IMPL
(
    Subdevice *pSubdevice,
    NV2080_CTRL_GPU_GET_SIMULATION_INFO_PARAMS *pGpuSimulationInfoParams
)
{
    OBJGPU *pGpu = GPU_RES_GET_GPU(pSubdevice);

    LOCK_ASSERT_AND_RETURN(rmApiLockIsOwner());

    if (IS_SILICON(pGpu))
    {
        pGpuSimulationInfoParams->type = NV2080_CTRL_GPU_GET_SIMULATION_INFO_TYPE_NONE;
    }
    else
    {
        pGpuSimulationInfoParams->type = NV2080_CTRL_GPU_GET_SIMULATION_INFO_TYPE_UNKNOWN;
    }

    return NV_OK;
}

//
// subdeviceCtrlCmdGpuGetEngines
//
// Lock Requirements:
//      Assert that API lock held on entry
//
NV_STATUS
subdeviceCtrlCmdGpuGetEngines_IMPL
(
    Subdevice *pSubdevice,
    NV2080_CTRL_GPU_GET_ENGINES_PARAMS *pParams
)
{
    NV2080_CTRL_GPU_GET_ENGINES_V2_PARAMS getEngineParamsV2;
    NvU32    *pKernelEngineList = NvP64_VALUE(pParams->engineList);
    NV_STATUS status = NV_OK;

    LOCK_ASSERT_AND_RETURN(rmApiLockIsOwner());

    portMemSet(&getEngineParamsV2, 0, sizeof(getEngineParamsV2));

    status = subdeviceCtrlCmdGpuGetEnginesV2(pSubdevice, &getEngineParamsV2);
    NV_CHECK_OR_RETURN(LEVEL_INFO, NV_OK == status, status);

    // NULL clients just want an engine count
    if (NULL != pKernelEngineList)
    {
        NV_CHECK_OR_RETURN(LEVEL_INFO, pParams->engineCount >= getEngineParamsV2.engineCount,
                         NV_ERR_BUFFER_TOO_SMALL);
        portMemCopy(pKernelEngineList,
                    getEngineParamsV2.engineCount * sizeof(*getEngineParamsV2.engineList), getEngineParamsV2.engineList,
                    getEngineParamsV2.engineCount * sizeof(*getEngineParamsV2.engineList));
    }

    pParams->engineCount = getEngineParamsV2.engineCount;

    return status;
}

//
// subdeviceCtrlCmdGpuGetEnginesV2
//
// Lock Requirements:
//      Assert that API lock held on entry
//
NV_STATUS
subdeviceCtrlCmdGpuGetEnginesV2_IMPL
(
    Subdevice *pSubdevice,
    NV2080_CTRL_GPU_GET_ENGINES_V2_PARAMS *pEngineParams
)
{
    OBJGPU   *pGpu = GPU_RES_GET_GPU(pSubdevice);
    NV_STATUS status = NV_OK;

    LOCK_ASSERT_AND_RETURN(rmApiLockIsOwner());

    // Update the engine Database
    NV_ASSERT_OK_OR_RETURN(gpuUpdateEngineTable(pGpu));

    // Validate engine count
    if (pGpu->engineDB.size > NV2080_GPU_MAX_ENGINES_LIST_SIZE)
    {
        NV_PRINTF(LEVEL_ERROR, "The engine database's size (0x%x) exceeds "
                  "NV2080_GPU_MAX_ENGINES_LIST_SIZE (0x%x)!\n",
                  pGpu->engineDB.size, NV2080_GPU_MAX_ENGINES_LIST_SIZE);
        DBG_BREAKPOINT();
        return NV_ERR_INVALID_STATE;
    }

    {
        NvU32 i;
        pEngineParams->engineCount = pGpu->engineDB.size;
        for (i = 0; i < pEngineParams->engineCount; i++)
        {
            pEngineParams->engineList[i] = pGpu->engineDB.pType[i];
        }
    }

    return status;
}

//
// subdeviceCtrlCmdGpuGetEngineClasslist
//
// Lock Requirements:
//      Assert that API lock held on entry
//
NV_STATUS
subdeviceCtrlCmdGpuGetEngineClasslist_IMPL
(
    Subdevice *pSubdevice,
    NV2080_CTRL_GPU_GET_ENGINE_CLASSLIST_PARAMS *pClassParams
)
{
    OBJGPU       *pGpu = GPU_RES_GET_GPU(pSubdevice);
    ENGDESCRIPTOR engDesc;
    NV_STATUS     status = NV_OK;

    LOCK_ASSERT_AND_RETURN(rmApiLockIsOwner());

    status = gpuXlateClientEngineIdToEngDesc(pGpu, pClassParams->engineType, &engDesc);
    NV_ASSERT(status == NV_OK);

    if (status != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR,
                  "NV2080_CTRL_CMD_GPU_GET_ENGINE_CLASSLIST Invalid engine ID 0x%x\n",
                  pClassParams->engineType);
        DBG_BREAKPOINT();
        return status;
    }

    status = gpuGetClassList(pGpu, &pClassParams->numClasses, NvP64_VALUE(pClassParams->classList), engDesc);

    if (status != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR,
                  "NV2080_CTRL_CMD_GPU_GET_ENGINE_CLASSLIST Class List query failed\n");
    }

    return status;
}

//
// subdeviceCtrlCmdGpuGetEnginePartnerList
//
// Lock Requirements:
//      Assert that API lock held on entry
//
NV_STATUS
subdeviceCtrlCmdGpuGetEnginePartnerList_IMPL
(
    Subdevice *pSubdevice,
    NV2080_CTRL_GPU_GET_ENGINE_PARTNERLIST_PARAMS *pPartnerListParams
)
{
    OBJGPU          *pGpu = GPU_RES_GET_GPU(pSubdevice);
    ENGDESCRIPTOR    engDesc;
    NvU32            localType;
    NvU32            i;
    PCLASSDESCRIPTOR pClass;
    NV_STATUS        status = NV_OK;

    pPartnerListParams->numPartners = 0;

    status = gpuXlateClientEngineIdToEngDesc(pGpu, pPartnerListParams->engineType, &engDesc);
    if (NV_OK != status)
    {
        NV_PRINTF(LEVEL_ERROR, "Invalid engine ID 0x%x\n",
                  pPartnerListParams->engineType);
        return status;
    }

    // find class in class db
    status = gpuGetClassByClassId(pGpu, pPartnerListParams->partnershipClassId, &pClass);
    if (NV_OK != status)
    {
        NV_PRINTF(LEVEL_ERROR, "Invalid class ID 0x%x\n",
                  pPartnerListParams->partnershipClassId);
        return status;
    }

    // Make sure that the engine related to this class is FIFO...
    if (pClass->engDesc != ENG_KERNEL_FIFO)
    {
        NV_PRINTF(LEVEL_ERROR,
                  "Class 0x%x is not considered a partnership class.\n",
                  pPartnerListParams->partnershipClassId);
        return NV_ERR_NOT_SUPPORTED;
    }

    // Translate the instance-local engine type to the global engine type in MIG mode
    localType = pPartnerListParams->engineType;

    // Restore the client's passed engineType
    pPartnerListParams->engineType = localType;

    //
    // For channels that the hal didn't handle, we should just return
    // all of the supported engines except for the target engine.
    //

    // Update the engine Database
    NV_ASSERT_OK_OR_RETURN(gpuUpdateEngineTable(pGpu));

    // Make sure it all will fit
    if (pGpu->engineDB.size > NV2080_CTRL_GPU_MAX_ENGINE_PARTNERS)
    {
        NV_PRINTF(LEVEL_ERROR,
                  "partnerList space is too small, time to increase. This is fatal\n");
        DBG_BREAKPOINT();
        return status;
    }

    // Copy over all of the engines except the target
    for (i = 0; i < pGpu->engineDB.size; i++)
    {
        // Skip the engine handed in
        if (pGpu->engineDB.pType[i] != pPartnerListParams->engineType )
        {
            pPartnerListParams->partnerList[pPartnerListParams->numPartners++] = pGpu->engineDB.pType[i];
        }
    }

    return status;
}

//
// subdeviceCtrlCmdGpuQueryMode_IMPL
//
// Lock Requirements:
//      Assert that API and GPUs lock held on entry
//
NV_STATUS
subdeviceCtrlCmdGpuQueryMode_IMPL
(
    Subdevice *pSubdevice,
    NV2080_CTRL_GPU_QUERY_MODE_PARAMS *pQueryMode
)
{
    OBJGPU *pGpu = GPU_RES_GET_GPU(pSubdevice);

    LOCK_ASSERT_AND_RETURN(rmApiLockIsOwner() && rmGpuLockIsOwner());

    switch (gpuGetMode(pGpu))
    {
        case NV_GPU_MODE_GRAPHICS_MODE:
        {
            pQueryMode->mode = NV2080_CTRL_GPU_QUERY_MODE_GRAPHICS_MODE;
            break;
        }
        case NV_GPU_MODE_COMPUTE_MODE:
        {
            pQueryMode->mode = NV2080_CTRL_GPU_QUERY_MODE_COMPUTE_MODE;
            break;
        }
        default:
        {
            pQueryMode->mode = NV2080_CTRL_GPU_QUERY_MODE_UNKNOWN_MODE;
            break;
        }
    }

    return NV_OK;
}

//
// subdeviceCtrlCmdGpuHandleGpuSR
//
// Lock Requirements:
//      Assert that API lock held on entry
//
NV_STATUS
subdeviceCtrlCmdGpuHandleGpuSR_IMPL
(
    Subdevice *pSubdevice
)
{
    return NV_OK;
}

//
// subdeviceCtrlCmdGpuGetId
//
// Lock Requirements:
//      Assert that API lock held on entry
//
NV_STATUS
subdeviceCtrlCmdGpuGetId_IMPL
(
    Subdevice *pSubdevice,
    NV2080_CTRL_GPU_GET_ID_PARAMS *pIdParams
)
{
    OBJGPU *pGpu = GPU_RES_GET_GPU(pSubdevice);

    LOCK_ASSERT_AND_RETURN(rmApiLockIsOwner());

    pIdParams->gpuId = pGpu->gpuId;

    return NV_OK;
}

//
// nv2080CtrlCmdGpuGetPids
//
// Lock Requirements:
//      Assert that API and GPUs lock held on entry
//
NV_STATUS
subdeviceCtrlCmdGpuGetPids_IMPL
(
    Subdevice *pSubdevice,
    NV2080_CTRL_GPU_GET_PIDS_PARAMS *pGetPidsParams
)
{
    OBJGPU *pGpu = GPU_RES_GET_GPU(pSubdevice);
    NvU32 internalClassId;
    NV_STATUS status;
    MIG_INSTANCE_REF *pRef = NULL;

    NV_ASSERT_OR_RETURN(RMCFG_FEATURE_KERNEL_RM, NV_ERR_NOT_SUPPORTED);

    LOCK_ASSERT_AND_RETURN(rmApiLockIsOwner() && rmGpuLockIsOwner());

    switch (pGetPidsParams->idType)
    {
        case (NV2080_CTRL_GPU_GET_PIDS_ID_TYPE_CLASS):
        {
            if (pGetPidsParams->id == NV20_SUBDEVICE_0)
            {
                internalClassId = classId(Subdevice);
            }
            else if (pGetPidsParams->id == MPS_COMPUTE)
            {
                internalClassId = classId(MpsApi);
            }
            else
            {
                internalClassId = classId(ChannelDescendant);
            }
            break;
        }
        case (NV2080_CTRL_GPU_GET_PIDS_ID_TYPE_VGPU_GUEST):
        {
            internalClassId = classId(HostVgpuDeviceApi);
            break;
        }
        default:
            return NV_ERR_INVALID_ARGUMENT;
    }

    //
    // Search over all clients to see if any contain objects of type = id.
    // If they do, then add their PID to the PIDArray param and also
    // return the amount of valid entries in the Array through pidTblCount.
    //
    status = gpuGetProcWithObject(pGpu, pGetPidsParams->id, internalClassId,
                                  pGetPidsParams->pidTbl, &pGetPidsParams->pidTblCount,
                                  pRef);
    return status;
}

//
// subdeviceCtrlCmdGpuGetPidInfo
//
// Lock Requirements:
//      Assert that API and GPUs lock held on entry
//
NV_STATUS
subdeviceCtrlCmdGpuGetPidInfo_IMPL
(
    Subdevice *pSubdevice,
    NV2080_CTRL_GPU_GET_PID_INFO_PARAMS *pGetPidInfoParams
)
{
    NV2080_CTRL_GPU_PID_INFO_DATA *pPidInfoData;
    NV2080_CTRL_SMC_SUBSCRIPTION_INFO *pSmcInfo;
    OBJGPU *pGpu = GPU_RES_GET_GPU(pSubdevice);
    NV2080_CTRL_GPU_PID_INFO *pPidInfo;
    NvU32 internalClassId;
    NvU32 i;
    MIG_INSTANCE_REF *pRef = NULL;
    NvBool bGlobalInfo = NV_TRUE;

    NV_ASSERT_OR_RETURN(RMCFG_FEATURE_KERNEL_RM, NV_ERR_NOT_SUPPORTED);

    LOCK_ASSERT_AND_RETURN(rmApiLockIsOwner() && rmGpuLockIsOwner());

    if ((pGetPidInfoParams->pidInfoListCount <= 0) ||
        (pGetPidInfoParams->pidInfoListCount >
         NV2080_CTRL_GPU_GET_PID_INFO_MAX_COUNT))
    {
        return NV_ERR_INVALID_ARGUMENT;
    }

    for (i = 0; i < pGetPidInfoParams->pidInfoListCount; ++i)
    {
        pPidInfo = &pGetPidInfoParams->pidInfoList[i];

        pSmcInfo = &pPidInfo->smcSubscription;
        pSmcInfo->computeInstanceId = PARTITIONID_INVALID;
        pSmcInfo->gpuInstanceId = PARTITIONID_INVALID;

        switch (pPidInfo->index)
        {
            case (NV2080_CTRL_GPU_PID_INFO_INDEX_VIDEO_MEMORY_USAGE):
            {
                internalClassId = classId(Memory);

                pPidInfoData = &pPidInfo->data;
                portMemSet(pPidInfoData, 0, sizeof(NV2080_CTRL_GPU_PID_INFO_DATA));
                pPidInfo->result = gpuFindClientInfoWithPidIterator(pGpu, pPidInfo->pid, 0,
                                                                    internalClassId,
                                                                    pPidInfoData,
                                                                    pSmcInfo,
                                                                    pRef,
                                                                    bGlobalInfo);
                break;
            }
            default:
            {
                pPidInfo->result = NV_ERR_INVALID_ARGUMENT;
                break;
            }
        }
    }

    return NV_OK;
}

NV_STATUS
subdeviceCtrlCmdGpuGetMaxSupportedPageSize_IMPL
(
    Subdevice *pSubdevice,
    NV2080_CTRL_GPU_GET_MAX_SUPPORTED_PAGE_SIZE_PARAMS *pParams
)
{
    OBJGPU   *pGpu = GPU_RES_GET_GPU(pSubdevice);
    NV_STATUS status  = NV_OK;

    LOCK_ASSERT_AND_RETURN(rmApiLockIsOwner() && rmDeviceGpuLockIsOwner(pGpu->gpuInstance));

    // Default to minimal page size (4k)
    pParams->maxSupportedPageSize = RM_PAGE_SIZE;

    if (gpuIsSriovEnabled(pGpu))
    {
        NvU64 vmmuSegmentSize = gpuGetVmmuSegmentSize(pGpu);
        if (vmmuSegmentSize > 0 &&
            vmmuSegmentSize < NV2080_CTRL_GPU_VMMU_SEGMENT_SIZE_512MB)
        {
            pParams->maxSupportedPageSize = RM_PAGE_SIZE_HUGE;
        }
    }

    return status;
}

/*!
 * @brief Check if address range is within the provided limits
 *
 * @param[in]  addrStart       Staring address of address range
 * @param[in]  addrLength      Size of address range
 * @param[in]  limitStart      Staring address of limit
 * @param[in]  limitLength     Size of limit
 *
 * @return
 *     NV_TRUE, if address range is within the provided limits
 *     NV_FALSE, if address range is outside the provided limits
 *
 */
static NvBool isAddressWithinLimits
(
    NvU64 addrStart,
    NvU64 addrLength,
    NvU64 limitStart,
    NvU64 limitLength
)
{
    NvU64 addrEnd  = 0;
    NvU64 limitEnd = 0;

    //
    // Calculate End address of address range and limit,
    // Return NV_FALSE in case of 64-bit addition overflow
    //
    if (!portSafeAddU64(addrStart, addrLength - 1, &addrEnd) ||
        !portSafeAddU64(limitStart, limitLength - 1, &limitEnd))
    {
        return NV_FALSE;
    }

    return ((addrStart >= limitStart) && (addrEnd <= limitEnd));
}

/*!
 * @brief Validate the address range for Memory Map request by comparing the
 *        user supplied address range with GPU BAR0/BAR1 range.
 *
 * Lock Requirements:
 *      Assert that API and GPUs lock held on entry
 *
 * @param[in] pSubdevice
 * @param[in] pParams    pointer to control parameters
 *
 * Possible status values returned are:
 *     NV_OK
 *     NV_ERR_PROTECTION_FAULT
 *
 */
NV_STATUS subdeviceCtrlCmdValidateMemMapRequest_IMPL
(
    Subdevice                                       *pSubdevice,
    NV2080_CTRL_GPU_VALIDATE_MEM_MAP_REQUEST_PARAMS *pParams
)
{
    OBJGPU       *pGpu     = GPU_RES_GET_GPU(pSubdevice);
    NvU64         start    = pParams->addressStart;
    NvU64         length   = pParams->addressLength;
    NV_STATUS     rmStatus;
    NvU32         bar0MapSize;
    NvU64         bar0MapOffset;

    LOCK_ASSERT_AND_RETURN(rmApiLockIsOwner() && rmGpuLockIsOwner());

    pParams->protection = NV_PROTECT_READ_WRITE;

    if (isAddressWithinLimits(start, length, pGpu->busInfo.gpuPhysAddr,
                        pGpu->deviceMappings[0].gpuNvLength))
    {
        start -= pGpu->busInfo.gpuPhysAddr;

        OBJTMR *pTmr = GPU_GET_TIMER(pGpu);
        rmStatus = tmrGetTimerBar0MapInfo_HAL(pGpu, pTmr,
                                              &bar0MapOffset, &bar0MapSize);
        if ((rmStatus == NV_OK) &&
            isAddressWithinLimits(start, length, bar0MapOffset, bar0MapSize))
        {
            pParams->protection = NV_PROTECT_READABLE;
            return NV_OK;
        }

        //
        // If the kernel side does not know about the object being mapped,
        // fall-through to GSP and see if it knows anything.
        //
        if (IS_GSP_CLIENT(pGpu))
        {
            RM_API *pRmApi = GPU_GET_PHYSICAL_RMAPI(pGpu);

            return pRmApi->Control(pRmApi,
                                   pGpu->hInternalClient,
                                   pGpu->hInternalSubdevice,
                                   NV2080_CTRL_CMD_GPU_VALIDATE_MEM_MAP_REQUEST,
                                   pParams, sizeof(*pParams));
        }

        return NV_ERR_PROTECTION_FAULT;
    }

    return NV_ERR_PROTECTION_FAULT;
}

/*!
 * @brief: This command returns the load time (latency) of each engine,
 *         implementing NV2080_CTRL_CMD_GPU_GET_ENGINE_LOAD_TIMES control call.
 *
 * @param[in]   pSubdevice
 * @param[in]   pParams
 *
 * @return
 *  NV_OK       Success
 */
NV_STATUS
subdeviceCtrlCmdGpuGetEngineLoadTimes_IMPL
(
    Subdevice *pSubdevice,
    NV2080_CTRL_GPU_GET_ENGINE_LOAD_TIMES_PARAMS *pParams
)
{
    OBJGPU        *pGpu              = GPU_RES_GET_GPU(pSubdevice);
    PENGDESCRIPTOR engDescriptorList = gpuGetInitEngineDescriptors(pGpu);
    NvU32          numEngDescriptors = gpuGetNumEngDescriptors(pGpu);
    NvU32          curEngDescIdx;

    NV_ASSERT_OR_RETURN(numEngDescriptors < NV2080_CTRL_GPU_MAX_ENGINE_OBJECTS, NV_ERR_BUFFER_TOO_SMALL);

    pParams->engineCount = numEngDescriptors;
    for (curEngDescIdx = 0; curEngDescIdx < numEngDescriptors; curEngDescIdx++)
    {
        ENGDESCRIPTOR   curEngDescriptor = engDescriptorList[curEngDescIdx];
        OBJENGSTATE    *pEngstate        = gpuGetEngstate(pGpu, curEngDescriptor);

        if (pEngstate == NULL)
        {
            pParams->engineIsInit[curEngDescIdx] = NV_FALSE;
            continue;
        }

        pParams->engineList[curEngDescIdx]          = pEngstate->engDesc;
        pParams->engineStateLoadTime[curEngDescIdx] = pEngstate->stats[ENGSTATE_STATE_LOAD].transitionTimeUs * 1000;
        pParams->engineIsInit[curEngDescIdx]        = NV_TRUE;
    }

    return NV_OK;
}

