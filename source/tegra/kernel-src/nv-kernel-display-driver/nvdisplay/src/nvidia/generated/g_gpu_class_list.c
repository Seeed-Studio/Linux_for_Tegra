/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include <core/core.h>
#include <gpu/gpu.h>
#include <gpu/eng_desc.h>
#include <g_allclasses.h>



const CLASSDESCRIPTOR *
gpuGetClassDescriptorList_T234D(POBJGPU pGpu, NvU32 *pNumClassDescriptors)
{
    static const CLASSDESCRIPTOR halT234DClassDescriptorList[] = {
        { GF100_HDACODEC, ENG_HDACODEC },
        { NV01_MEMORY_SYNCPOINT, ENG_DMA },
        { NV04_DISPLAY_COMMON, ENG_KERNEL_DISPLAY },
        { NVC372_DISPLAY_SW, ENG_KERNEL_DISPLAY },
        { NVC670_DISPLAY, ENG_KERNEL_DISPLAY },
        { NVC671_DISP_SF_USER, ENG_KERNEL_DISPLAY },
        { NVC673_DISP_CAPABILITIES, ENG_KERNEL_DISPLAY },
        { NVC67A_CURSOR_IMM_CHANNEL_PIO, ENG_KERNEL_DISPLAY },
        { NVC67B_WINDOW_IMM_CHANNEL_DMA, ENG_KERNEL_DISPLAY },
        { NVC67D_CORE_CHANNEL_DMA, ENG_KERNEL_DISPLAY },
        { NVC67E_WINDOW_CHANNEL_DMA, ENG_KERNEL_DISPLAY },
        { NVC77F_ANY_CHANNEL_DMA, ENG_KERNEL_DISPLAY },
    };

    #define HALT234D_NUM_CLASS_DESCS (sizeof(halT234DClassDescriptorList) / sizeof(CLASSDESCRIPTOR))

    #define HALT234D_NUM_CLASSES 16

    ct_assert(NV0080_CTRL_GPU_CLASSLIST_MAX_SIZE >= HALT234D_NUM_CLASSES);

    *pNumClassDescriptors = HALT234D_NUM_CLASS_DESCS;
    return halT234DClassDescriptorList;
}


