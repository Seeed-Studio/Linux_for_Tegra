/*
 * SPDX-FileCopyrightText: Copyright (c) 1999-2021 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

/**************************************************************************************************************
*
*   Description:
*       Sanity test the system environment to verify our driver can run properly
*
**************************************************************************************************************/

#include <core/core.h>
#include <os/os.h>
#include <gpu/gpu.h>
#include <gpu_mgr/gpu_mgr.h>
#include <objtmr.h>

#include "g_os_private.h"

NV_STATUS osSanityTestIsr(
    OBJGPU *pGpu
)
{
    NvBool serviced = NV_FALSE;

    return (serviced) ? NV_OK : NV_ERR_GENERIC;
}

//
// add various system environment start-up tests here
// currently, just verify interrupt hookup, but could also verify other details
//
NV_STATUS osVerifySystemEnvironment(
    OBJGPU *pGpu
)
{
    NV_STATUS status = NV_OK;

    return status;
}

