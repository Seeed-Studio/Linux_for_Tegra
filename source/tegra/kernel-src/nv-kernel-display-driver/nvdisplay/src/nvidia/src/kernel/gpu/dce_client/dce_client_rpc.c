/*
 * SPDX-FileCopyrightText: Copyright (c) 2020-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "gpu/dce_client/dce_client.h"
#include "os/dce_rm_client_ipc.h"

#include "os/os.h"

#include "vgpu/rpc.h"
#include "gpu/mem_mgr/virt_mem_allocator_common.h"
#include "class/cl0073.h"
#include "class/clc670.h"
#include "class/clc67b.h"
#include "class/clc67d.h"
#include "class/clc67e.h"
#include "class/cl84a0.h"
#include "class/clc372sw.h"

#include <rmapi/alloc_size.h>

#include "gpu/disp/kern_disp.h"

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

#define DCE_MAX_RPC_MSG_SIZE 4096

extern ROOT roots[MAX_RM_CLIENTS];
extern DEVICE devices[MAX_RM_CLIENTS];
extern SUBDEVICE subdevices[MAX_RM_CLIENTS];
extern DISPLAY_COMMON display;
extern DISPLAY_SW displaySW;
extern DISPLAY_SW_EVENT displaySWEventHotplug;
extern DISPLAY_SW_EVENT displaySWEventDPIRQ;
extern DISPLAY_HPD_CTRL displayCtrlHotplug;
extern DISPLAY_HPD_CTRL displayCtrlDPIRQ;
extern DISPLAY_DP_SET_MANUAL displayCtrlDPSetManual;

NV_STATUS
dceclientInitRpcInfra_IMPL
(
    OBJGPU *pGpu,
    DceClient *pDceClient
)
{
    NV_STATUS nvStatus = NV_OK;

    NV_PRINTF(LEVEL_INFO, "Init RPC Infra Called\n");

    pDceClient->pRpc = initRpcObject(pGpu);
    if (pDceClient->pRpc == NULL)
    {
        NV_PRINTF(LEVEL_ERROR, "initRpcObject failed\n");
        return NV_ERR_INSUFFICIENT_RESOURCES;
    }

    pDceClient->pRpc->maxRpcSize = DCE_MAX_RPC_MSG_SIZE;

    // Register Synchronous IPC client for RPC to DCE RM
    pDceClient->clientId[DCE_CLIENT_RM_IPC_TYPE_SYNC] = 0;
    nvStatus = osTegraDceRegisterIpcClient(DCE_CLIENT_RM_IPC_TYPE_SYNC,
                                           NULL,
                                           &pDceClient->clientId[DCE_CLIENT_RM_IPC_TYPE_SYNC]);
    if (nvStatus != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR, "Register dce ipc client failed for DCE_CLIENT_RM_IPC_TYPE_SYNC error 0x%x\n",
                               nvStatus);
        goto ipc_register_fail;
    }

    NV_PRINTF(LEVEL_INFO, "Registered dce ipc client DCE_CLIENT_RM_IPC_TYPE_SYNC handle: 0x%x\n",
                           pDceClient->clientId[DCE_CLIENT_RM_IPC_TYPE_SYNC]);

    // Register Asynchronous IPC client for event notification from DCE RM
    pDceClient->clientId[DCE_CLIENT_RM_IPC_TYPE_EVENT] = 0;
    nvStatus = osTegraDceRegisterIpcClient(DCE_CLIENT_RM_IPC_TYPE_EVENT,
                                           pGpu,
                                           &pDceClient->clientId[DCE_CLIENT_RM_IPC_TYPE_EVENT]);
    if (nvStatus != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR, "Register dce ipc client failed for DCE_CLIENT_RM_IPC_TYPE_EVENT error 0x%x\n",
                               nvStatus);
        goto ipc_register_fail;
    }
    NV_PRINTF(LEVEL_INFO, "Register dce ipc client DCE_CLIENT_RM_IPC_TYPE_EVENT: 0x%x\n",
                           pDceClient->clientId[DCE_CLIENT_RM_IPC_TYPE_EVENT]);

ipc_register_fail:
    if (nvStatus != NV_OK)
    {
        dceclientDeinitRpcInfra(pDceClient);
    }

    return nvStatus;
}

NV_STATUS
dceclientDceRmInit_IMPL
(
    OBJGPU *pGpu,
    DceClient *pDceClient,
    NvBool bInit
)
{
    NV_STATUS nvStatus = NV_OK;

    NV_RM_RPC_DCE_RM_INIT(pGpu, bInit, nvStatus);

    return nvStatus;
}

void
dceclientDeinitRpcInfra_IMPL
(
    DceClient *pDceClient
)
{
    NvU32 i = 0;

    NV_PRINTF(LEVEL_INFO, "Free RPC Infra Called\n");

    for (i = 0; i < NV_ARRAY_ELEMENTS(pDceClient->clientId); i++)
    {
        osTegraDceUnregisterIpcClient(pDceClient->clientId[i]);
    }

    portMemFree(pDceClient->pRpc);
    pDceClient->pRpc = NULL;
}

NV_STATUS
dceclientSendRpc_IMPL
(
    DceClient *pDceClient,
    void *msgData,
    NvU32 msgLength
)
{
    NV_STATUS nvStatus = NV_OK;
    NvU32 clientId = pDceClient->clientId[DCE_CLIENT_RM_IPC_TYPE_SYNC];

    NV_PRINTF(LEVEL_INFO, "Send RPC Called, clientid used 0x%x\n", clientId);

    if (msgData)
    {
        nvStatus = osTegraDceClientIpcSendRecv(clientId, msgData, msgLength);
        if (nvStatus != NV_OK)
        {
            NV_PRINTF(LEVEL_ERROR, "Send RPC failed for clientId %u error %u\n", clientId, nvStatus);
            return nvStatus;
        }
    }

    return nvStatus;
}

NV_STATUS
dceclientReceiveMsg_IMPL
(
    DceClient *pDceClient
)
{
    NV_STATUS nvStatus = NV_OK;

    NV_PRINTF(LEVEL_INFO, "Receive Message Called\n");

    return nvStatus;
}

NV_STATUS
dceclientSendMsg_IMPL
(
    DceClient *pDceClient
)
{
    NV_STATUS nvStatus = NV_OK;

    NV_PRINTF(LEVEL_INFO, "Send Message Called\n");

    return nvStatus;
}

static inline rpc_message_header_v *_dceRpcGetMessageHeader(OBJRPC *pRpc)
{
    return ((rpc_message_header_v*)(pRpc->message_buffer));
}

static inline rpc_generic_union *_dceRpcGetMessageData(OBJRPC *pRpc)
{
    return _dceRpcGetMessageHeader(pRpc)->rpc_message_data;
}

static inline NV_STATUS _dceRpcGetRpcResult(OBJRPC *pRpc)
{
    return _dceRpcGetMessageHeader(pRpc)->rpc_result;
}

/**
 * Prints the header info when _INFO level is enabled.
 */
static void _dceclientrmPrintHdr
(
    OBJRPC *pRpc
)
{
    NV_PRINTF(LEVEL_INFO, "NVRM_RPC_DCE : [msg-buf:0x%p] header_version = 0x%x signature = 0x%x "
              "length = 0x%x function = 0x%x rpc_result = 0x%x\n", pRpc->message_buffer,
              _dceRpcGetMessageHeader(pRpc)->header_version, _dceRpcGetMessageHeader(pRpc)->signature,
              _dceRpcGetMessageHeader(pRpc)->length, _dceRpcGetMessageHeader(pRpc)->function,
              _dceRpcGetMessageHeader(pRpc)->rpc_result);
}

/**
 * Allocate memory for rpc message
 * TODO : Change static allocation of 4K to
 * a better dynamic allocation
 */
static NV_STATUS _dceRpcAllocateMemory
(
    OBJRPC *pRpc
)
{
    NvU32 *message_buffer;

    message_buffer = portMemAllocNonPaged(DCE_MAX_RPC_MSG_SIZE);
    if (message_buffer == NULL)
    {
        NV_PRINTF(LEVEL_ERROR, "Cannot allocate memory for message_buffer\n");
        return NV_ERR_INSUFFICIENT_RESOURCES;
    }

    pRpc->message_buffer = message_buffer;

    return NV_OK;
}

static void _dceRpcFreeMemory
(
    OBJRPC *pRpc
)
{
    portMemFree(pRpc->message_buffer);
    pRpc->message_buffer = NULL;
}

/**
 * Send RPC msg and check the result.
 */
static NV_STATUS
_dceRpcIssueAndWait
(
    RM_API *pRmApi
)
{
    OBJGPU *pGpu = (OBJGPU*)pRmApi->pPrivateContext;
    OBJRPC *pRpc = GPU_GET_RPC(pGpu);

    NV_STATUS status = NV_ERR_INVALID_ARGUMENT;
    rpc_message_header_v* message_header = NULL;
    DceClient *pDceclientrm = GPU_GET_DCECLIENTRM(pGpu);

    message_header = _dceRpcGetMessageHeader(pRpc);
    if (message_header)
    {
        _dceclientrmPrintHdr(pRpc);

        status = dceclientSendRpc(pDceclientrm, message_header, message_header->length);
        if (status != NV_OK)
        {
            NV_PRINTF(LEVEL_ERROR, "NVRM_RPC_DCE: Error while issuing RPC [0x%x]\n", status);
            goto done;
        }
    }

done:
    return status;
}

void dceclientHandleAsyncRpcCallback
(
    NvU32 handle,
    NvU32 interfaceType,
    NvU32 msgLength,
    void *data,
    void *usrCtx
)
{
    NV_PRINTF(LEVEL_INFO, "dceclientHandleAsyncRpcCallback called\n");

    rpc_message_header_v *msg_hdr = NULL;
    rpc_generic_union *rpc_msg_data = NULL;
    OBJGPU *pGpu = (OBJGPU *)usrCtx;

    NV_ASSERT_OR_RETURN_VOID(interfaceType == DCE_CLIENT_RM_IPC_TYPE_EVENT);
    NV_ASSERT_OR_RETURN_VOID(pGpu != NULL && data != NULL);

    msg_hdr = (rpc_message_header_v *)data;
    rpc_msg_data = msg_hdr->rpc_message_data;

    switch (msg_hdr->function)
    {
        case NV_VGPU_MSG_EVENT_POST_EVENT:
        {
            rpc_post_event_v *rpc_params = &rpc_msg_data->post_event_v;

            if (rpc_params->bNotifyList)
            {
                gpuNotifySubDeviceEvent(pGpu, rpc_params->notifyIndex,
                                        rpc_params->eventData,
                                        rpc_params->eventDataSize, 0, 0);
            }
            else
            {
                PEVENTNOTIFICATION pNotifyList  = NULL;
                PEVENTNOTIFICATION pNotifyEvent = NULL;
                Event             *pEvent       = NULL;
                NV_STATUS          nvStatus     = NV_OK;

                // Get the notification list that contains this event.
                NV_ASSERT(CliGetEventInfo(rpc_params->hClient,
                          rpc_params->hEvent, &pEvent));

                if (pEvent->pNotifierShare != NULL)
                    pNotifyList = pEvent->pNotifierShare->pEventList;

                NV_ASSERT(pNotifyList != NULL);

                // Send event to a specific hEvent.  Find hEvent in the notification list.
                for (pNotifyEvent = pNotifyList; pNotifyEvent != NULL; pNotifyEvent = pNotifyEvent->Next)
                {
                    if (pNotifyEvent->hEvent == rpc_params->hEvent)
                    {
                        nvStatus = osNotifyEvent(pGpu, pNotifyEvent, 0,
                                   rpc_params->data, rpc_params->status);
                        if (nvStatus != NV_OK)
                            NV_PRINTF(LEVEL_ERROR, "osNotifyEvent failed with status: %x\n",nvStatus);
                        break;
                    }
                }
                NV_ASSERT(pNotifyEvent != NULL);
            }
            return;
        }
        case NV_VGPU_MSG_EVENT_RG_LINE_INTR:
        {
            rpc_rg_line_intr_v *rpc_params = &rpc_msg_data->rg_line_intr_v;

            KernelDisplay *pKernelDisplay = GPU_GET_KERNEL_DISPLAY(pGpu);
            NV_CHECK_OR_RETURN_VOID(LEVEL_ERROR, pKernelDisplay != NULL);

            kdispInvokeRgLineCallback(pKernelDisplay, rpc_params->head, rpc_params->rgIntr, NV_FALSE);
            return;
        }
        case NV_VGPU_MSG_EVENT_DISPLAY_MODESET:
        {
            rpc_display_modeset_v *rpc_params = &rpc_msg_data->display_modeset_v;

            KernelDisplay *pKernelDisplay = GPU_GET_KERNEL_DISPLAY(pGpu);
            NV_CHECK_OR_RETURN_VOID(LEVEL_ERROR, pKernelDisplay != NULL);

            kdispInvokeDisplayModesetCallback(pKernelDisplay,
                                              rpc_params->bModesetStart,
                                              rpc_params->minRequiredIsoBandwidthKBPS,
                                              rpc_params->minRequiredFloorBandwidthKBPS);
            return;
        }
        default:
        {
            NV_PRINTF(LEVEL_ERROR, "Unexpected RPC function 0x%x\n", msg_hdr->function);
            NV_ASSERT_FAILED("Unexpected RPC function");
            return;
        }
    }
}

NV_STATUS rpcRmApiControl_dce
(
    RM_API *pRmApi,
    NvHandle hClient,
    NvHandle hObject,
    NvU32 cmd,
    void *pParamStructPtr,
    NvU32 paramsSize
)
{
    OBJGPU *pGpu = (OBJGPU*)pRmApi->pPrivateContext;
    OBJRPC *pRpc = GPU_GET_RPC(pGpu);

    rpc_generic_union *msg_data;
    rpc_gsp_rm_control_v *rpc_params = NULL;
    NV_STATUS status = NV_ERR_NOT_SUPPORTED;
    NV2080_CTRL_EVENT_SET_NOTIFICATION_PARAMS setEventParams = { };
    NV0073_CTRL_CMD_DP_SET_MANUAL_DISPLAYPORT_PARAMS setManualParams = {0};

    NV_PRINTF(LEVEL_INFO, "NVRM_RPC_DCE : Prepare and send RmApiControl RPC\n");

    status = _dceRpcAllocateMemory(pRpc);
    if (status != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR, "NVRM_RPC_DCE: Memory Allocation Failed\n");
        goto done;
    }

    msg_data = _dceRpcGetMessageData(pRpc);
    rpc_params = &msg_data->gsp_rm_control_v;

    status = rpcWriteCommonHeader(pGpu, pRpc,
                                  NV_VGPU_MSG_FUNCTION_GSP_RM_CONTROL,
                                  (sizeof(rpc_gsp_rm_control_v) +
                                   paramsSize));
    if (status != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR, "NVRM_RPC_DCE: Writing RPC Header Failed [0x%x]\n", status);
        goto done;
    }

    rpc_params->hClient    = hClient;
    rpc_params->hObject    = hObject;
    rpc_params->cmd        = cmd;
    rpc_params->paramsSize = paramsSize;
    portMemCopy(rpc_params->params, paramsSize,pParamStructPtr, paramsSize);

    if (!pGpu->getProperty(pGpu, PDB_PROP_GPU_IN_PM_RESUME_CODEPATH))
    {
        if (cmd == NV2080_CTRL_CMD_EVENT_SET_NOTIFICATION)
        {
            portMemCopy(&setEventParams, paramsSize,pParamStructPtr, paramsSize);
            if (setEventParams.event == NV2080_NOTIFIERS_HOTPLUG)
            {
                displayCtrlHotplug.hClient = rpc_params->hClient;
                displayCtrlHotplug.hObject = rpc_params->hObject;
                portMemCopy(&displayCtrlHotplug.setEventParams, rpc_params->paramsSize, rpc_params->params, rpc_params->paramsSize);
                displayCtrlHotplug.valid = NV_TRUE;
            }
            if (setEventParams.event == NV2080_NOTIFIERS_DP_IRQ)
            {
                displayCtrlDPIRQ.hClient = rpc_params->hClient;
                displayCtrlDPIRQ.hObject = rpc_params->hObject;
                portMemCopy(&displayCtrlDPIRQ.setEventParams, rpc_params->paramsSize, rpc_params->params, rpc_params->paramsSize);
                displayCtrlDPIRQ.valid = NV_TRUE;
            }
        }
        if (cmd == NV0073_CTRL_CMD_DP_SET_MANUAL_DISPLAYPORT)
        {
            portMemCopy(&setManualParams, paramsSize,pParamStructPtr, paramsSize);
            displayCtrlDPSetManual.hClient = rpc_params->hClient;
            displayCtrlDPSetManual.hObject = rpc_params->hObject;
            portMemCopy(&displayCtrlDPSetManual.setManualParams, rpc_params->paramsSize, rpc_params->params, rpc_params->paramsSize);
            displayCtrlDPSetManual.valid = NV_TRUE;
        }
    }

    status = _dceRpcIssueAndWait(pRmApi);
    if (status != NV_OK)
    {
        goto done;
    }

    status = _dceRpcGetRpcResult(pRpc);
    if (status == NV_ERR_NOT_SUPPORTED)
    {
        NV_PRINTF(LEVEL_INFO, "NVRM_RPC_DCE: RM ctrl call cmd:0x%x not supported\n", cmd);
    }
    else if (status != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR, "NVRM_RPC_DCE: Failed RM ctrl call cmd:0x%x result 0x%x:\n", cmd, status);
    }

    portMemCopy(pParamStructPtr, paramsSize, rpc_params->params, paramsSize);

    NV_PRINTF(LEVEL_INFO, "NVRM_RPC_DCE: RPC for GSP RM Control Successful\n");

done:
    if (pRpc->message_buffer)
        _dceRpcFreeMemory(pRpc);
    return status;
}

NV_STATUS rpcRmApiAlloc_dce
(
    RM_API *pRmApi,
    NvHandle hClient,
    NvHandle hParent,
    NvHandle hObject,
    NvU32 hClass,
    void *pAllocParams
)
{
    OBJGPU *pGpu = (OBJGPU*)pRmApi->pPrivateContext;
    OBJRPC *pRpc = GPU_GET_RPC(pGpu);

    rpc_generic_union *msg_data;
    rpc_gsp_rm_alloc_v *rpc_params;
    NV_STATUS status;
    NvU32 paramsSize;
    NvBool bNullAllowed;
    NV0005_ALLOC_PARAMETERS displaySWEventAllocParams;

    NV_PRINTF(LEVEL_INFO, "NVRM_RPC_DCE: Prepare and send RmApiAlloc RPC\n");

    NV_ASSERT_OK_OR_GOTO(status,
                         _dceRpcAllocateMemory(pRpc),
                         done);

    msg_data = _dceRpcGetMessageData(pRpc);
    rpc_params = &msg_data->gsp_rm_alloc_v;

    NV_ASSERT_OK_OR_GOTO(status,
                         rmapiGetClassAllocParamSize(&paramsSize, pAllocParams, &bNullAllowed, hClass),
                         done);

    if (pAllocParams == NULL && !bNullAllowed)
    {
        NV_PRINTF(LEVEL_ERROR, "NVRM_RPC_DCE: NULL allocation params not allowed for class 0x%x\n", hClass);
        status = NV_ERR_INVALID_ARGUMENT;
        goto done;
    }

    status = rpcWriteCommonHeader(pGpu, pRpc,
                                  NV_VGPU_MSG_FUNCTION_GSP_RM_ALLOC,
                                  (sizeof(rpc_gsp_rm_alloc_v) + paramsSize));

    if (status != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR, "NVRM_RPC_DCE: Writing RPC Header Failed [0x%x]\n", status);
        goto done;
    }

    rpc_params->hClient    = hClient;
    rpc_params->hParent    = hParent;
    rpc_params->hObject    = hObject;
    rpc_params->hClass     = hClass;
    rpc_params->paramsSize = pAllocParams ? paramsSize : 0;
    portMemCopy(rpc_params->params, rpc_params->paramsSize, pAllocParams, paramsSize);

    if (!pGpu->getProperty(pGpu, PDB_PROP_GPU_IN_PM_RESUME_CODEPATH))
    {
        if (hClass == NV01_ROOT)
        {
            NvU32 i = 0;

            for (i = 0; i < MAX_RM_CLIENTS; i++)
            {
                if (!roots[i].valid)
                {
                    roots[i].hClient = rpc_params->hClient;
                    roots[i].hParent = rpc_params->hParent;
                    roots[i].hObject = rpc_params->hObject;
                    roots[i].hClass  = rpc_params->hClass;
                    portMemCopy(&roots[i].rootAllocParams, rpc_params->paramsSize, rpc_params->params, rpc_params->paramsSize);
                    roots[i].valid   = NV_TRUE;
                    break;
                }
            }
        }

        if (hClass == NV01_DEVICE_0)
        {
            NvU32 i=0;

            for (i = 0; i < MAX_RM_CLIENTS; i++)
            {
                if (!devices[i].valid)
                {
                    devices[i].hClient = rpc_params->hClient;
                    devices[i].hParent = rpc_params->hParent;
                    devices[i].hObject = rpc_params->hObject;
                    devices[i].hClass  = rpc_params->hClass;
                    portMemCopy(&devices[i].deviceAllocParams, rpc_params->paramsSize, rpc_params->params, rpc_params->paramsSize);
                    devices[i].valid   = NV_TRUE;
                    break;
                }
            }
        }

        if (hClass == NV20_SUBDEVICE_0)
        {
            NvU32 i = 0;

            for (i = 0; i < MAX_RM_CLIENTS; i++)
            {
                if (!subdevices[i].valid)
                {
                    subdevices[i].hClient = rpc_params->hClient;
                    subdevices[i].hParent = rpc_params->hParent;
                    subdevices[i].hObject = rpc_params->hObject;
                    subdevices[i].hClass  = rpc_params->hClass;
                    portMemCopy(&subdevices[i].subdeviceAllocParams, rpc_params->paramsSize, rpc_params->params, rpc_params->paramsSize);
                    subdevices[i].valid   = NV_TRUE;
                    break;
                }
            }
        }

        if (hClass == NV04_DISPLAY_COMMON)
        {
            display.hClient = rpc_params->hClient;
            display.hParent = rpc_params->hParent;
            display.hObject = rpc_params->hObject;
            display.hClass  = rpc_params->hClass;
            portMemCopy(&display.displayCommonAllocParams, rpc_params->paramsSize, rpc_params->params, rpc_params->paramsSize);
            display.valid   = NV_TRUE;
        }

        if (hClass == NVC372_DISPLAY_SW)
        {
            displaySW.hClient = rpc_params->hClient;
            displaySW.hParent = rpc_params->hParent;
            displaySW.hObject = rpc_params->hObject;
            displaySW.hClass  = rpc_params->hClass;
            portMemCopy(&displaySW.displaySWAllocParams, rpc_params->paramsSize, rpc_params->params, rpc_params->paramsSize);
            displaySW.valid   = NV_TRUE;
        }

        if (hClass == NV01_EVENT_KERNEL_CALLBACK_EX)
        {
            portMemCopy(&displaySWEventAllocParams, rpc_params->paramsSize, rpc_params->params, rpc_params->paramsSize);
            if (0x4000001 == displaySWEventAllocParams.notifyIndex)
            {
                displaySWEventHotplug.hClient = rpc_params->hClient;
                displaySWEventHotplug.hParent = rpc_params->hParent;
                displaySWEventHotplug.hObject = rpc_params->hObject;
                displaySWEventHotplug.hClass  = rpc_params->hClass;
                portMemCopy(&displaySWEventHotplug.displaySWEventAllocParams, rpc_params->paramsSize, rpc_params->params, rpc_params->paramsSize);
                displaySWEventHotplug.valid   = NV_TRUE;
            }

            if (0x4000007 == displaySWEventAllocParams.notifyIndex)
            {
                displaySWEventDPIRQ.hClient = rpc_params->hClient;
                displaySWEventDPIRQ.hParent = rpc_params->hParent;
                displaySWEventDPIRQ.hObject = rpc_params->hObject;
                displaySWEventDPIRQ.hClass  = rpc_params->hClass;
                portMemCopy(&displaySWEventDPIRQ.displaySWEventAllocParams, rpc_params->paramsSize, rpc_params->params, rpc_params->paramsSize);
                displaySWEventDPIRQ.valid   = NV_TRUE;
            }
        }
    }

    status = _dceRpcIssueAndWait(pRmApi);
    if (status != NV_OK)
    {
        goto done;
    }

    status = _dceRpcGetRpcResult(pRpc);
    if (status != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR, "NVRM_RPC_DCE: Failed RM Alloc Object result 0x%x:\n", status);
    }

    // Deserialize the response
    // We do not deserialize the variable length data as we do not expect it to be modified
    if (paramsSize > 0)
    {
        portMemCopy(pAllocParams, paramsSize, rpc_params->params, rpc_params->paramsSize);
    }

    NV_PRINTF(LEVEL_INFO, "NVRM_RPC_DCE: RPC for GSP RM Alloc Successful\n");

done:
    if (pRpc->message_buffer)
        _dceRpcFreeMemory(pRpc);
    return status;
}

NV_STATUS rpcRmApiFree_dce(RM_API *pRmApi, NvHandle hClient, NvHandle hObject)
{
    OBJGPU *pGpu = (OBJGPU*)pRmApi->pPrivateContext;
    OBJRPC *pRpc = GPU_GET_RPC(pGpu);

    rpc_generic_union *msg_data;
    NVOS00_PARAMETERS_v *rpc_params;
    NV_STATUS status = NV_ERR_NOT_SUPPORTED;

    NV_PRINTF(LEVEL_INFO, "NVRM_RPC_DCE Free "
              "RPC Called for hClient: 0x%x\n", hClient);

    status = _dceRpcAllocateMemory(pRpc);
    NV_ASSERT_OK_OR_RETURN(status);

    msg_data = _dceRpcGetMessageData(pRpc);
    rpc_params = &msg_data->free_v.params;

    status = rpcWriteCommonHeader(pGpu, pRpc,
                                  NV_VGPU_MSG_FUNCTION_FREE,
                                  sizeof(rpc_free_v));
    if (status != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR, "NVRM_RPC_DCE: Writing RPC Header Failed [0x%x]\n", status);
        goto done;
    }

    rpc_params->hRoot = hClient;
    rpc_params->hObjectParent = NV01_NULL_OBJECT;
    rpc_params->hObjectOld = hObject;

    status = _dceRpcIssueAndWait(pRmApi);
    if (status != NV_OK)
    {
        goto done;
    }

    status = _dceRpcGetRpcResult(pRpc);
    if (status != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR, "NVRM_RPC_DCE: Failed RM Free Object result 0x%x:\n", status);
    }

    NV_PRINTF(LEVEL_INFO, "NVRM_RPC_DCE: RPC for Free Successful\n");

done:
    if (pRpc->message_buffer)
        _dceRpcFreeMemory(pRpc);

    return status;
}

NV_STATUS rpcRmApiDupObject_dce
(
    RM_API *pRmApi,
    NvHandle hClient,
    NvHandle hParent,
    NvHandle *phObject,
    NvHandle hClientSrc,
    NvHandle hObjectSrc,
    NvU32 flags
)
{
    OBJGPU *pGpu = (OBJGPU*)pRmApi->pPrivateContext;
    OBJRPC *pRpc = GPU_GET_RPC(pGpu);

    rpc_generic_union *msg_data;
    NVOS55_PARAMETERS_v03_00 *rpc_params = NULL;
    NV_STATUS status = NV_ERR_NOT_SUPPORTED;

    NV_PRINTF(LEVEL_INFO, "NVRM_RPC_DCE Dup Object "
              "RPC Called for hClient: 0x%x\n", hClient);


    status = _dceRpcAllocateMemory(pRpc);
    NV_ASSERT_OK_OR_RETURN(status);

    msg_data = _dceRpcGetMessageData(pRpc);
    rpc_params = &msg_data->dup_object_v.params;

    status = rpcWriteCommonHeader(pGpu, pRpc,
                                  NV_VGPU_MSG_FUNCTION_DUP_OBJECT,
                                  sizeof(rpc_dup_object_v));
    if (status != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR, "NVRM_RPC_DCE: Writing RPC Header Failed [0x%x]\n", status);
        goto done;
    }

    rpc_params->hClient     = hClient;
    rpc_params->hParent     = hParent;
    rpc_params->hObject     = *phObject;
    rpc_params->hClientSrc  = hClientSrc;
    rpc_params->hObjectSrc  = hObjectSrc;
    rpc_params->flags       = flags;

    status = _dceRpcIssueAndWait(pRmApi);
    if (status != NV_OK)
    {
        goto done;
    }

    status = _dceRpcGetRpcResult(pRpc);
    if (status != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR, "NVRM_RPC_DCE: Failed RM Dup Object result 0x%x:\n", status);
    }

    NV_PRINTF(LEVEL_INFO, "NVRM_RPC_DCE: RPC for DUP OBJECT Successful\n");

done:
    portMemFree(pRpc->message_buffer);
    return status;
}

NV_STATUS
rpcDceRmInit_dce
(
    RM_API *pRmApi,
    NvBool bInit
)
{
    OBJGPU *pGpu = (OBJGPU*)pRmApi->pPrivateContext;
    OBJRPC *pRpc = GPU_GET_RPC(pGpu);

    rpc_generic_union *msg_data;
    NV_STATUS status = NV_ERR_NOT_SUPPORTED;
    rpc_dce_rm_init_v *rpc_params = NULL;

    NV_PRINTF(LEVEL_INFO, "NVRM_RPC_DCE RPC to trigger %s called\n",
              bInit ? "RmInitAdapter" : "RmShutdownAdapter");

    status = _dceRpcAllocateMemory(pRpc);
    NV_ASSERT_OK_OR_RETURN(status);

    msg_data = _dceRpcGetMessageData(pRpc);
    rpc_params = &msg_data->dce_rm_init_v;

    status = rpcWriteCommonHeader(pGpu, pRpc,
                                  NV_VGPU_MSG_FUNCTION_DCE_RM_INIT,
                                  sizeof(rpc_dce_rm_init_v));
    if (status != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR, "NVRM_RPC_DCE: Writing RPC Header Failed [0x%x]\n", status);
        goto done;
    }

    rpc_params->bInit     = bInit;
    status = _dceRpcIssueAndWait(pRmApi);
    if (status != NV_OK)
    {
        goto done;
    }

    status = _dceRpcGetRpcResult(pRpc);
    if (status != NV_OK)
    {
        NV_PRINTF(LEVEL_ERROR, "NVRM_RPC_DCE: Failed RM init/deinit result 0x%x:\n", status);
    }

done:
    portMemFree(pRpc->message_buffer);
    return status;
}
