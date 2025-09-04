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

/*!
 * @file
 * @brief   GSP Client (CPU RM) specific GPU routines reside in this file.
 */

#include "core/core.h"
#include "gpu/gpu.h"
#include "ctrl/ctrl2080.h"

/*!
 * @brief These functions are used on CPU RM when pGpu is a GSP client.
 * Data is fetched from GSP using subdeviceCtrlCmdInternalGetChipInfo and cached,
 * then retrieved through the internal gpuGetChipInfo.
 *
 * Functions either return value directly, or through a second [out] param, depending
 * on the underlying function.
 *
 * @param[in]  pGpu
 */
NvU8
gpuGetChipSubRev_FWCLIENT
(
    OBJGPU *pGpu
)
{
    const NV2080_CTRL_INTERNAL_GPU_GET_CHIP_INFO_PARAMS *pChipInfo = gpuGetChipInfo(pGpu);
    NV_ASSERT_OR_RETURN(pChipInfo != NULL, 0);

    return pChipInfo->chipSubRev;
}

NvU32
gpuGetEmulationRev1_FWCLIENT
(
    OBJGPU *pGpu
)
{
    const NV2080_CTRL_INTERNAL_GPU_GET_CHIP_INFO_PARAMS *pChipInfo = gpuGetChipInfo(pGpu);
    NV_ASSERT_OR_RETURN(pChipInfo != NULL, 0);

    return pChipInfo->emulationRev1;
}

NV_STATUS
gpuConstructDeviceInfoTable_FWCLIENT
(
    OBJGPU *pGpu
)
{
    NV_STATUS status;
    RM_API *pRmApi = GPU_GET_PHYSICAL_RMAPI(pGpu);

    NV2080_CTRL_INTERNAL_GET_DEVICE_INFO_TABLE_PARAMS *pParams;
    const NvU32 cmd = NV2080_CTRL_CMD_INTERNAL_GET_DEVICE_INFO_TABLE;

    if (pGpu->pDeviceInfoTable) // already initialized
        return NV_OK;

    pParams = portMemAllocNonPaged(sizeof(*pParams));
    NV_ASSERT_OR_RETURN(pParams != NULL, NV_ERR_NO_MEMORY);

    status = pRmApi->Control(pRmApi, pGpu->hInternalClient, pGpu->hInternalSubdevice,
                             cmd, pParams, sizeof(*pParams));
    if (status != NV_OK)
        goto done;

    if (pParams->numEntries == 0)
        goto done;

    pGpu->pDeviceInfoTable = portMemAllocNonPaged(pParams->numEntries * sizeof(DEVICE_INFO2_TABLE));
    NV_ASSERT_TRUE_OR_GOTO(status, pGpu->pDeviceInfoTable != NULL, NV_ERR_NO_MEMORY, done);

    pGpu->numDeviceInfoEntries = pParams->numEntries;
    portMemCopy(pGpu->pDeviceInfoTable,   pGpu->numDeviceInfoEntries * sizeof(DEVICE_INFO2_TABLE),
                pParams->deviceInfoTable, pParams->numEntries        * sizeof(DEVICE_INFO2_TABLE));

done:
    portMemFree(pParams);
    return status;
}

NvU32
gpuGetLitterValues_FWCLIENT
(
    OBJGPU *pGpu,
    NvU32 index
)
{
    return 0;
}

NV_STATUS
gpuGetRegBaseOffset_FWCLIENT
(
    OBJGPU *pGpu,
    NvU32 regBase,
    NvU32 *pOffset
)
{
    const NV2080_CTRL_INTERNAL_GPU_GET_CHIP_INFO_PARAMS *pChipInfo = gpuGetChipInfo(pGpu);
    NV_ASSERT_OR_RETURN(pChipInfo != NULL, NV_ERR_INVALID_STATE);
    NV_ASSERT_OR_RETURN(regBase < NV_ARRAY_ELEMENTS(pChipInfo->regBases), NV_ERR_NOT_SUPPORTED);

    if (pChipInfo->regBases[regBase] != 0xFFFFFFFF)
    {
        *pOffset = pChipInfo->regBases[regBase];
        return NV_OK;
    }

    return NV_ERR_NOT_SUPPORTED;
}

NvU32
gpuReadBAR1Size_FWCLIENT
(
    OBJGPU *pGpu
)
{
    const NV2080_CTRL_INTERNAL_GPU_GET_CHIP_INFO_PARAMS *pChipInfo = gpuGetChipInfo(pGpu);
    NV_ASSERT_OR_RETURN(pChipInfo != NULL, 0);

    return pChipInfo->bar1Size;
}
