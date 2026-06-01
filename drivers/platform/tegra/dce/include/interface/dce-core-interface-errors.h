/* SPDX-License-Identifier: MIT */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#ifndef DCE_CORE_INTERFACE_ERRORS_H
#define DCE_CORE_INTERFACE_ERRORS_H

#define DCE_ERR_CORE_SUCCESS                    (0x00000000)

#define DCE_ERR_CORE_NOT_IMPLEMENTED            (0x00000001)
#define DCE_ERR_CORE_SC7_SEQUENCE               (0x00000002)
#define DCE_ERR_CORE_RD_MEM_MAP                 (0x00000003)
#define DCE_ERR_CORE_WR_MEM_MAP                 (0x00000004)
#define DCE_ERR_CORE_IVC_INIT                   (0x00000005)
#define DCE_ERR_CORE_BAD_ADMIN_CMD              (0x00000006)
#define DCE_ERR_CORE_HSP_DB_INUSE               (0x00000007)

#define DCE_ERR_CORE_NULL_PTR                   (0x00000010)
#define DCE_ERR_CORE_MEM_SIZE                   (0x00000011)
#define DCE_ERR_CORE_MEM_NOT_FOUND              (0x00000012)
#define DCE_ERR_CORE_MEM_NOT_MAPPED             (0x00000013)
#define DCE_ERR_CORE_MEM_ALREADY_MAPPED         (0x00000014)
#define DCE_ERR_CORE_MEM_BAD_REGION             (0x00000015)

#define DCE_ERR_CORE_VMINDEX_INVALID            (0x00000020)
#define DCE_ERR_CORE_VMINDEX_NO_AST_BASE        (0x00000021)

#define DCE_ERR_CORE_INTERFACE_LOCKED           (0x00000030)
#define DCE_ERR_CORE_INTERFACE_INCOMPATIBLE     (0x00000031)

#define DCE_ERR_CORE_TIMER_INVALID              (0x00000040)
#define DCE_ERR_CORE_TIMER_EXPIRED              (0x00000041)
#define DCE_ERR_CORE_TIMER_NOT_CREATED          (0x00000042)
#define DCE_ERR_CORE_TIMER_NOT_STARTED          (0x00000043)

#define DCE_ERR_CORE_IPC_BAD_TYPE               (0x00000050)
#define DCE_ERR_CORE_IPC_NO_HANDLES             (0x00000051)
#define DCE_ERR_CORE_IPC_BAD_CHANNEL            (0x00000052)
#define DCE_ERR_CORE_IPC_CHAN_REGISTERED        (0x00000053)
#define DCE_ERR_CORE_IPC_BAD_HANDLE             (0x00000054)
#define DCE_ERR_CORE_IPC_MSG_TOO_LARGE          (0x00000055)
#define DCE_ERR_CORE_IPC_NO_BUFFERS             (0x00000056)
#define DCE_ERR_CORE_IPC_BAD_HEADER             (0x00000057)
#define DCE_ERR_CORE_IPC_IVC_INIT               (0x00000058)
#define DCE_ERR_CORE_IPC_NO_DATA                (0x00000059)
#define DCE_ERR_CORE_IPC_INVALID_SIGNAL         (0x0000005A)
#define DCE_ERR_CORE_IPC_IVC_ERR                (0x0000005B)
#define DCE_ERR_CORE_IPC_SIGNAL_REGISTERED      (0x0000005C)

#define DCE_ERR_CORE_GPIO_INVALID_ID            (0x00000060)
#define DCE_ERR_CORE_GPIO_NO_SPACE              (0x00000061)

#define DCE_ERR_CORE_RM_BOOTSTRAP               (0x00000070)

#define DCE_ERR_CORE_NOT_FOUND                  (0x00000080)
#define DCE_ERR_CORE_NOT_INITIALIZED            (0x00000081)

#define DCE_ERR_CORE_DT_INVALID                 (0x00000090)
#define DCE_ERR_CORE_DT_NOT_INITIALIZED         (0x00000091)
#define DCE_ERR_CORE_DT_NOT_FOUND               (0x00000092)

#define DCE_ERR_CORE_IRQ_ENABLE_FAILED          (0x000000A0)
#define DCE_ERR_CORE_IRQ_DISABLE_FAILED         (0x000000A1)
#define DCE_ERR_CORE_IRQ_INVALID_PARAM          (0x000000A2)
#define DCE_ERR_CORE_IRQ_INVALID_HANDLE         (0x000000A3)
#define DCE_ERR_CORE_IRQ_REGISTER_FAILED        (0x000000A4)
#define DCE_ERR_CORE_IRQ_INVALID_CL_TABLE       (0x000000A5)
#define DCE_ERR_CORE_IRQ_NAME_NOT_FOUND         (0x000000A6)
#define DCE_ERR_CORE_IRQ_IN_USE                 (0x000000A7)

#define DCE_ERR_CORE_BPMP_MRQ_RESPONSE          (0x000000B0)
#define DCE_ERR_CORE_BPMP_MRQ_INVALID_HANDLE    (0x000000B1)
#define DCE_ERR_CORE_BPMP_MRQ_INVALID_PARAM     (0x000000B2)
#define DCE_ERR_CORE_BPMP_MRQ_GENERIC           (0x000000B3)

#define DCE_ERR_CORE_MMIO_INVALID_PARAM         (0x000000C0)
#define DCE_ERR_CORE_MMIO_INVALID_RANGE         (0x000000C1)
#define DCE_ERR_CORE_MMIO_MAP_NOT_FOUND         (0x000000C2)
#define DCE_ERR_CORE_MMIO_INVALID_HANDLE        (0x000000C3)
#define DCE_ERR_CORE_MMIO_NOT_MAPPED            (0x000000C4)
#define DCE_ERR_CORE_MMIO_NO_SPACE              (0x000000C5)

#define DCE_ERR_CORE_I2C_INIT_FAILED            (0x000000D0)
#define DCE_ERR_CORE_I2C_CLK_INIT_FAILED        (0x000000D1)
#define DCE_ERR_CORE_I2C_RST_INIT_FAILED        (0x000000D2)
#define DCE_ERR_CORE_I2C_IRQ_INIT_FAILED        (0x000000D3)
#define DCE_ERR_CORE_I2C_INVALID_PARAM          (0x000000D4)
#define DCE_ERR_CORE_I2C_NOT_REGISTERED         (0x000000D5)
#define DCE_ERR_CORE_I2C_XFER_TIMED_OUT         (0x000000D6)
#define DCE_ERR_CORE_I2C_PORT_NOT_FOUND         (0x000000D7)

#define DCE_ERR_CORE_OTHER                      (0x0000FFFF)

#endif
