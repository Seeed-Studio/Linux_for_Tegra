/*
 * SPDX-FileCopyrightText: Copyright (c) 2016-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

//
// No include guards - this file is included multiple times, each time with a
// different definition for RS_ENTRY
//

//
// Table describing all RsResource subclasses.
//
// Internal Class - there is a RM internal class representing all classes
// exported to RM clients. The internal name of the class should be similar to
// the symbolic name used by clients. If there is ambiguity between RM internal
// classes, e.g.: between the PMU engine (OBJPMU) and the exported class, it's
// recommended to use Api as the suffix to disambiguate; for example, OBJPMU
// (the engine) vs PmuApi (the per-client api object). It's also recommended to
// avoid using Object, Resource, etc as those terms don't improve clarity.
// If there is no ambiguity, there is no need to add the Api suffix; for example,
// Channel is preferred over ChannelApi (there is no other Channel object in
// RM).
//
// Multi-Instance - NV_TRUE if there can be multiple instances of this object's
// *internal* class id under a parent.
//
// This list should eventually replace the similar lists in nvapi.c and
// rmctrl.c. The number of fields in the table should be kept minimal, just
// enough to create the object, with as much of the detail being specified
// within the class itself.
//
// In the future we should consider switching to a registration approach or
// generating with NVOC and/or annotating the class definition.
//
// RS-TODO: Rename classes that have 'Object' in their names
//



RS_ENTRY(
    /* External Class         */ NV01_ROOT,
    /* Internal Class         */ RmClientResource,
    /* Multi-Instance         */ NV_FALSE,
    /* Parents                */ RS_ROOT_OBJECT,
    /* Alloc Param Info       */ RS_OPTIONAL(NvHandle),
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPUS_LOCK_ON_ALLOC,
    /* Required Access Rights */ RS_ACCESS_NONE
)
RS_ENTRY(
    /* External Class         */ NV01_ROOT_NON_PRIV,
    /* Internal Class         */ RmClientResource,
    /* Multi-Instance         */ NV_FALSE,
    /* Parents                */ RS_ROOT_OBJECT,
    /* Alloc Param Info       */ RS_OPTIONAL(NvHandle),
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPUS_LOCK_ON_ALLOC,
    /* Required Access Rights */ RS_ACCESS_NONE
)
RS_ENTRY(
    /* External Class         */ NV01_ROOT_CLIENT,
    /* Internal Class         */ RmClientResource,
    /* Multi-Instance         */ NV_FALSE,
    /* Parents                */ RS_ROOT_OBJECT,
    /* Alloc Param Info       */ RS_OPTIONAL(NvHandle),
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPUS_LOCK_ON_ALLOC,
    /* Required Access Rights */ RS_ACCESS_NONE
)
RS_ENTRY(
    /* External Class         */ NV0020_GPU_MANAGEMENT,
    /* Internal Class         */ GpuManagementApi,
    /* Multi-Instance         */ NV_FALSE,
    /* Parents                */ RS_LIST(classId(RmClientResource)),
    /* Alloc Param Info       */ RS_OPTIONAL(NvHandle),
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPUS_LOCK,
    /* Required Access Rights */ RS_ACCESS_NONE
)
RS_ENTRY(
    /* External Class         */ NV01_DEVICE_0,
    /* Internal Class         */ Device,
    /* Multi-Instance         */ NV_TRUE,
    /* Parents                */ RS_LIST(classId(RmClientResource)),
    /* Alloc Param Info       */ RS_OPTIONAL(NV0080_ALLOC_PARAMETERS),
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPUS_LOCK | RS_FLAGS_ACQUIRE_RO_API_LOCK_ON_ALLOC,
    /* Required Access Rights */ RS_ACCESS_NONE
)
RS_ENTRY(
    /* External Class         */ GF100_HDACODEC,
    /* Internal Class         */ Hdacodec,
    /* Multi-Instance         */ NV_FALSE,
    /* Parents                */ RS_LIST(classId(Device)),
    /* Alloc Param Info       */ RS_NONE,
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPUS_LOCK_ON_FREE | RS_FLAGS_ALLOC_RPC_TO_ALL,
    /* Required Access Rights */ RS_ACCESS_NONE
)
    /* Channels can have a CHANNEL_GROUP, a DEVICE, or a CONTEXT_SHARE (starting in Volta) as parents */
    /* RS-TODO: Update channel parent list when CONTEXT_SHARE is added */
RS_ENTRY(
    /* External Class         */ NV20_SUBDEVICE_0,
    /* Internal Class         */ Subdevice,
    /* Multi-Instance         */ NV_TRUE,
    /* Parents                */ RS_LIST(classId(Device)),
    /* Alloc Param Info       */ RS_OPTIONAL(NV2080_ALLOC_PARAMETERS),
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPUS_LOCK | RS_FLAGS_ACQUIRE_RO_API_LOCK_ON_ALLOC,
    /* Required Access Rights */ RS_ACCESS_NONE
)
RS_ENTRY(
    /* External Class         */ NV2081_BINAPI,
    /* Internal Class         */ BinaryApi,
    /* Multi-Instance         */ NV_TRUE,
    /* Parents                */ RS_LIST(classId(Subdevice)),
    /* Alloc Param Info       */ RS_OPTIONAL(NV2081_ALLOC_PARAMETERS),
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPUS_LOCK | RS_FLAGS_ACQUIRE_RO_API_LOCK_ON_ALLOC | RS_FLAGS_ALLOC_RPC_TO_PHYS_RM,
    /* Required Access Rights */ RS_ACCESS_NONE
)
RS_ENTRY(
    /* External Class         */ NV2082_BINAPI_PRIVILEGED,
    /* Internal Class         */ BinaryApiPrivileged,
    /* Multi-Instance         */ NV_TRUE,
    /* Parents                */ RS_LIST(classId(Subdevice)),
    /* Alloc Param Info       */ RS_OPTIONAL(NV2082_ALLOC_PARAMETERS),
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPUS_LOCK | RS_FLAGS_ACQUIRE_RO_API_LOCK_ON_ALLOC | RS_FLAGS_ALLOC_RPC_TO_PHYS_RM,
    /* Required Access Rights */ RS_ACCESS_NONE
)
RS_ENTRY(
    /* External Class         */ NV01_MEMORY_SYSTEM,
    /* Internal Class         */ SystemMemory,
    /* Multi-Instance         */ NV_TRUE,
    /* Parents                */ RS_LIST(classId(Device)),
    /* Alloc Param Info       */ RS_REQUIRED(NV_MEMORY_ALLOCATION_PARAMS),
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPUS_LOCK | RS_FLAGS_ACQUIRE_RO_API_LOCK_ON_ALLOC,
    /* Required Access Rights */ RS_ACCESS_NONE
)
RS_ENTRY(
    /* External Class         */ NV01_MEMORY_SYSTEM_OS_DESCRIPTOR,
    /* Internal Class         */ OsDescMemory,
    /* Multi-Instance         */ NV_TRUE,
    /* Parents                */ RS_LIST(classId(Device)),
    /* Alloc Param Info       */ RS_REQUIRED(NV_OS_DESC_MEMORY_ALLOCATION_PARAMS),
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPU_GROUP_LOCK | RS_FLAGS_ACQUIRE_RO_API_LOCK_ON_ALLOC,
    /* Required Access Rights */ RS_ACCESS_NONE
)
RS_ENTRY(
    /* External Class         */ NV01_MEMORY_SYNCPOINT,
    /* Internal Class         */ SyncpointMemory,
    /* Multi-Instance         */ NV_TRUE,
    /* Parents                */ RS_LIST(classId(Device)),
    /* Alloc Param Info       */ RS_REQUIRED(NV_MEMORY_SYNCPOINT_ALLOCATION_PARAMS),
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPU_GROUP_LOCK,
    /* Required Access Rights */ RS_ACCESS_NONE
)
    /* Subdevice Children: */
RS_ENTRY(
    /* External Class         */ NVC671_DISP_SF_USER,
    /* Internal Class         */ DispSfUser,
    /* Multi-Instance         */ NV_FALSE,
    /* Parents                */ RS_LIST(classId(Subdevice)),
    /* Alloc Param Info       */ RS_NONE,
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPUS_LOCK,
    /* Required Access Rights */ RS_ACCESS_NONE
)
    /* Display classes: */
RS_ENTRY(
    /* External Class         */ NVC670_DISPLAY,
    /* Internal Class         */ NvDispApi,
    /* Multi-Instance         */ NV_FALSE,
    /* Parents                */ RS_LIST(classId(Device)),
    /* Alloc Param Info       */ RS_NONE,
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPUS_LOCK | RS_FLAGS_ALLOC_RPC_TO_ALL,
    /* Required Access Rights */ RS_ACCESS_NONE
)
RS_ENTRY(
    /* External Class         */ NVC372_DISPLAY_SW,
    /* Internal Class         */ DispSwObj,
    /* Multi-Instance         */ NV_TRUE,
    /* Parents                */ RS_LIST(classId(Device)),
    /* Alloc Param Info       */ RS_NONE,
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ALLOC_RPC_TO_ALL,
    /* Required Access Rights */ RS_ACCESS_NONE
)
RS_ENTRY(
    /* External Class         */ NV04_DISPLAY_COMMON,
    /* Internal Class         */ DispCommon,
    /* Multi-Instance         */ NV_FALSE,
    /* Parents                */ RS_LIST(classId(Device)),
    /* Alloc Param Info       */ RS_NONE,
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPUS_LOCK_ON_FREE | RS_FLAGS_ALLOC_RPC_TO_ALL,
    /* Required Access Rights */ RS_ACCESS_NONE
)
RS_ENTRY(
    /* External Class         */ NVC67A_CURSOR_IMM_CHANNEL_PIO,
    /* Internal Class         */ DispChannelPio,
    /* Multi-Instance         */ NV_TRUE,
    /* Parents                */ RS_LIST(classId(NvDispApi)),
    /* Alloc Param Info       */ RS_REQUIRED(NV50VAIO_CHANNELPIO_ALLOCATION_PARAMETERS),
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPUS_LOCK | RS_FLAGS_ALLOC_RPC_TO_ALL,
    /* Required Access Rights */ RS_ACCESS_NONE
)
RS_ENTRY(
    /* External Class         */ NVC67B_WINDOW_IMM_CHANNEL_DMA,
    /* Internal Class         */ DispChannelDma,
    /* Multi-Instance         */ NV_TRUE,
    /* Parents                */ RS_LIST(classId(NvDispApi)),
    /* Alloc Param Info       */ RS_REQUIRED(NV50VAIO_CHANNELDMA_ALLOCATION_PARAMETERS),
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPUS_LOCK | RS_FLAGS_ALLOC_RPC_TO_ALL,
    /* Required Access Rights */ RS_ACCESS_NONE
)
RS_ENTRY(
    /* External Class         */ NVC67D_CORE_CHANNEL_DMA,
    /* Internal Class         */ DispChannelDma,
    /* Multi-Instance         */ NV_TRUE,
    /* Parents                */ RS_LIST(classId(NvDispApi)),
    /* Alloc Param Info       */ RS_REQUIRED(NV50VAIO_CHANNELDMA_ALLOCATION_PARAMETERS),
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPUS_LOCK | RS_FLAGS_ALLOC_RPC_TO_ALL,
    /* Required Access Rights */ RS_ACCESS_NONE
)
RS_ENTRY(
    /* External Class         */ NVC77F_ANY_CHANNEL_DMA,
    /* Internal Class         */ DispChannelDma,
    /* Multi-Instance         */ NV_TRUE,
    /* Parents                */ RS_LIST(classId(NvDispApi)),
    /* Alloc Param Info       */ RS_REQUIRED(NV50VAIO_CHANNELDMA_ALLOCATION_PARAMETERS),
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPUS_LOCK,
    /* Required Access Rights */ RS_ACCESS_NONE
)
RS_ENTRY(
    /* External Class         */ NVC67E_WINDOW_CHANNEL_DMA,
    /* Internal Class         */ DispChannelDma,
    /* Multi-Instance         */ NV_TRUE,
    /* Parents                */ RS_LIST(classId(NvDispApi)),
    /* Alloc Param Info       */ RS_REQUIRED(NV50VAIO_CHANNELDMA_ALLOCATION_PARAMETERS),
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPUS_LOCK | RS_FLAGS_ALLOC_RPC_TO_ALL,
    /* Required Access Rights */ RS_ACCESS_NONE
)
RS_ENTRY(
    /* External Class         */ NVC673_DISP_CAPABILITIES,
    /* Internal Class         */ DispCapabilities,
    /* Multi-Instance         */ NV_FALSE,
    /* Parents                */ RS_LIST(classId(NvDispApi)),
    /* Alloc Param Info       */ RS_NONE,
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPUS_LOCK,
    /* Required Access Rights */ RS_ACCESS_NONE
)
    /* Classes allocated under channel: */
RS_ENTRY(
    /* External Class         */ NV01_CONTEXT_DMA,
    /* Internal Class         */ ContextDma,
    /* Multi-Instance         */ NV_TRUE,
    /* Parents                */ RS_ANY_PARENT,
    /* Alloc Param Info       */ RS_REQUIRED(NV_CONTEXT_DMA_ALLOCATION_PARAMS),
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPUS_LOCK,
    /* Required Access Rights */ RS_ACCESS_NONE
)
RS_ENTRY(
    /* External Class         */ NV01_EVENT,
    /* Internal Class         */ Event,
    /* Multi-Instance         */ NV_TRUE,
    /* Parents                */ RS_ANY_PARENT,
    /* Alloc Param Info       */ RS_REQUIRED(NV0005_ALLOC_PARAMETERS),
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPUS_LOCK,
    /* Required Access Rights */ RS_ACCESS_NONE
)
RS_ENTRY(
    /* External Class         */ NV01_EVENT_OS_EVENT,
    /* Internal Class         */ Event,
    /* Multi-Instance         */ NV_TRUE,
    /* Parents                */ RS_ANY_PARENT,
    /* Alloc Param Info       */ RS_REQUIRED(NV0005_ALLOC_PARAMETERS),
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPUS_LOCK,
    /* Required Access Rights */ RS_ACCESS_NONE
)
RS_ENTRY(
    /* External Class         */ NV01_EVENT_KERNEL_CALLBACK,
    /* Internal Class         */ Event,
    /* Multi-Instance         */ NV_TRUE,
    /* Parents                */ RS_ANY_PARENT,
    /* Alloc Param Info       */ RS_REQUIRED(NV0005_ALLOC_PARAMETERS),
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPUS_LOCK,
    /* Required Access Rights */ RS_ACCESS_NONE
)
RS_ENTRY(
    /* External Class         */ NV01_EVENT_KERNEL_CALLBACK_EX,
    /* Internal Class         */ Event,
    /* Multi-Instance         */ NV_TRUE,
    /* Parents                */ RS_ANY_PARENT,
    /* Alloc Param Info       */ RS_REQUIRED(NV0005_ALLOC_PARAMETERS),
    /* Resource Free Priority */ RS_FREE_PRIORITY_DEFAULT,
    /* Flags                  */ RS_FLAGS_ACQUIRE_GPUS_LOCK,
    /* Required Access Rights */ RS_ACCESS_NONE
)

// Undefine the entry macro to simplify call sites
#undef RS_ENTRY
