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

//******************************************************************************
//
//   Description:
//       This file implements RPC code common to all builds.
//
//******************************************************************************

#include "gpu/gpu.h"
#include "vgpu/rpc.h"
#include "os/os.h"

#define RPC_STRUCTURES
#define RPC_GENERIC_UNION
#include "g_rpc-structures.h"
#undef RPC_STRUCTURES
#undef RPC_GENERIC_UNION

#define RPC_MESSAGE_STRUCTURES
#define RPC_MESSAGE_GENERIC_UNION
#include "g_rpc-message-header.h"
#undef RPC_MESSAGE_STRUCTURES
#undef RPC_MESSAGE_GENERIC_UNION

void rpcRmApiSetup(OBJGPU *pGpu)
{
    //
    // Physical RMAPI is already initialized for monolithic, and this function
    // just needs to overwrite individual methods as needed
    //
    RM_API *pRmApi = GPU_GET_PHYSICAL_RMAPI(pGpu);
    PORT_UNREFERENCED_VARIABLE(pRmApi);

    if (IS_VIRTUAL(pGpu))
    {
        // none for now
    }
    else if (IS_GSP_CLIENT(pGpu) && IsT234D(pGpu))
    {
        pRmApi->Control         = rpcRmApiControl_dce;
        pRmApi->AllocWithHandle = rpcRmApiAlloc_dce;
        pRmApi->Free            = rpcRmApiFree_dce;
        pRmApi->DupObject       = rpcRmApiDupObject_dce;
    }
}

OBJRPC *initRpcObject(OBJGPU *pGpu)
{
    OBJRPC   *pRpc     = NULL;

    pRpc = portMemAllocNonPaged(sizeof(OBJRPC));
    if (pRpc == NULL)
    {
        NV_PRINTF(LEVEL_ERROR,
                  "cannot allocate memory for OBJRPC (instance %d)\n",
                  gpuGetInstance(pGpu));
        return NULL;
    }

    rpcRmApiSetup(pGpu);

    return pRpc;
}

NV_STATUS rpcWriteCommonHeader(OBJGPU *pGpu, OBJRPC *pRpc, NvU32 func, NvU32 paramLength)
{
    NV_STATUS status = NV_OK;

    if (!pRpc)
    {
        NV_PRINTF(LEVEL_ERROR,
                  "NVRM_RPC: called with NULL pRpc.  Function %d.\n", func);
        NV_ASSERT(0);
        return NV_ERR_INVALID_STATE;
    }

    portMemSet(pRpc->message_buffer, 0, pRpc->maxRpcSize);

    vgpu_rpc_message_header_v->header_version     = DRF_DEF(_VGPU, _MSG_HEADER_VERSION, _MAJOR, _TOT) |
                                                    DRF_DEF(_VGPU, _MSG_HEADER_VERSION, _MINOR, _TOT);
    vgpu_rpc_message_header_v->signature          = NV_VGPU_MSG_SIGNATURE_VALID;
    vgpu_rpc_message_header_v->rpc_result         = NV_VGPU_MSG_RESULT_RPC_PENDING;
    vgpu_rpc_message_header_v->rpc_result_private = NV_VGPU_MSG_RESULT_RPC_PENDING;
    {
        vgpu_rpc_message_header_v->u.spare        = NV_VGPU_MSG_UNION_INIT;
    }
    vgpu_rpc_message_header_v->function           = func;
    vgpu_rpc_message_header_v->length             = sizeof(rpc_message_header_v) + paramLength;

    return status;
}
