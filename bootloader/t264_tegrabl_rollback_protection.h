/*
 * SPDX-FileCopyrightText: Copyright (c) 2018-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation.  Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */

#ifndef INCLUDED_TEGRABL_ROLLBACK_PROTECTION_H
#define INCLUDED_TEGRABL_ROLLBACK_PROTECTION_H

/* OEM FW ratchet indices loaded by MB1 */
#define OEM_FW_RATCHET_IDX_INVALID              0x00U
#define OEM_FW_RATCHET_IDX_MB1BCT               0x01U
#define OEM_FW_RATCHET_IDX_MEMBCT               0x02U
#define OEM_FW_RATCHET_IDX_BPMP_DTB             0x03U
#define OEM_FW_RATCHET_IDX_MB2_RF               0x04U
#define OEM_FW_RATCHET_IDX_MB2                  0x05U
#define OEM_FW_RATCHET_IDX_BPMP_IST_CFG         0x06U
#define OEM_FW_RATCHET_IDX_MB2_APPLET           0x07U
#define OEM_FW_RATCHET_IDX_FSKP_FW              0x08U

#define OEM_FW_RATCHET_IDX_MB1_MAX              0x0eU /* 14, Reserved 6 */
#define MAX_MB1_BINS       OEM_FW_RATCHET_IDX_MB1_MAX

/* OEM FW ratchet indices loaded by MB2 */
#define OEM_FW_RATCHET_IDX_PVIT                 0x0fU /* 15 */
#define OEM_FW_RATCHET_IDX_MEMDTB               0x10U /* 16 */
#define OEM_FW_RATCHET_IDX_FSI                  0x11U /* 17 */
#define OEM_FW_RATCHET_IDX_RST_TID              0x12U /* 18 */
#define OEM_FW_RATCHET_IDX_RCE                  0x13U /* 19 */
#define OEM_FW_RATCHET_IDX_APE                  0x14U /* 20 */
#define OEM_FW_RATCHET_IDX_APE1                 0x15U /* 21 */
#define OEM_FW_RATCHET_IDX_AON                  0x16U /* 22 */
#define OEM_FW_RATCHET_IDX_DCE                  0x17U /* 23 */
#define OEM_FW_RATCHET_IDX_PVA                  0x18U /* 24 */
#define OEM_FW_RATCHET_IDX_ATF                  0x19U /* 25 */
#define OEM_FW_RATCHET_IDX_SECURE_HV            0x1aU /* 26 */
#define OEM_FW_RATCHET_IDX_EKS                  0x1bU /* 27 */
#define OEM_FW_RATCHET_IDX_SECURE_FW_FIP        0x1cU /* 28 */
#define OEM_FW_RATCHET_IDX_HV                   0x1dU /* 29 */
#define OEM_FW_RATCHET_IDX_CPU_BL               0x1eU /* 30 */
#define OEM_FW_RATCHET_IDX_CPU_BL_DTB           0x1fU /* 31 */
#define OEM_FW_RATCHET_IDX_EFI_OS_LOAD          0x20U /* 32 */
#define OEM_FW_RATCHET_IDX_KERNEL               0x21U /* 33 */
#define OEM_FW_RATCHET_IDX_KERNEL_DTB           0x22U /* 34 */
#define OEM_FW_RATCHET_IDX_RAMDISK              0x23U /* 35 */
#define OEM_FW_RATCHET_IDX_MISC_CONFIG          0x24U /* 36 */

#define OEM_FW_RATCHET_IDX_MB2_MAX              0x27U /* 39, Reserved 3 */
#define OEM_FW_RATCHET_IDX_IST_TESTIMAGE        0x28U /* 40 */
#define OEM_FW_RATCHET_IDX_IST_RUNTIME_INFO     0x29U /* 41 */
#define OEM_FW_RATCHET_IDX_BL_MAX               0x2fU /* 47, Reserved for future use */

/* Total number of OEM FW ratchet indices */
#define MAX_OEM_FW_RATCHET_INDEX               0x130U /* 304 */

#endif /* INCLUDED_TEGRABL_ROLLBACK_PROTECTION_H */
