/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, NVIDIA Corporation.  All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation.  Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */

#ifndef PVA_KMD_CONSTANTS_H
#define PVA_KMD_CONSTANTS_H
#include "pva_constants.h"
/* Limits related to KMD's own submission*/
#define PVA_KMD_MAX_NUM_KMD_RESOURCES 32
#define PVA_KMD_MAX_NUM_KMD_DMA_CONFIGS 1
#define PVA_KMD_MAX_NUM_KMD_CHUNKS 32
#define PVA_KMD_MAX_NUM_KMD_SUBMITS 32

/* Limits related to User's privileged submission */
#define PVA_KMD_MAX_NUM_PRIV_CHUNKS 256
#define PVA_KMD_MAX_NUM_PRIV_SUBMITS 256

#define PVA_KMD_USER_CONTEXT_ID_BASE 1u
#define PVA_KMD_PVA0_T23x_REG_BASE 0x16000000
#define PVA_KMD_PVA0_T23x_REG_SIZE 0x800000

#define PVA_KMD_TIMEOUT_INF UINT64_MAX

// clang-format off
#if PVA_BUILD_MODE == PVA_BUILD_MODE_SIM
    #define PVA_KMD_TIMEOUT_FACTOR 100
#else
    #define PVA_KMD_TIMEOUT_FACTOR 1
#endif
// clang-format on

#define PVA_KMD_TIMEOUT(val) (val * PVA_KMD_TIMEOUT_FACTOR)

#define PVA_KMD_TIMEOUT_RESOURCE_SEMA_MS PVA_KMD_TIMEOUT(100) /*< 100 ms */
#define PVA_KMD_WAIT_FW_TIMEOUT_US PVA_KMD_TIMEOUT(1000000) /*< 1 second*/
#define PVA_KMD_WAIT_FW_POLL_INTERVAL_US PVA_KMD_TIMEOUT(100) /*< 100 us*/
#define PVA_KMD_FW_BOOT_TIMEOUT_MS PVA_KMD_TIMEOUT(1000) /*< 1 seconds */

#define PVA_NUM_RW_SYNCPTS 56

// clang-format off
#if PVA_DEV_MAIN_COMPATIBLE == 1
    #define PVA_KMD_LOAD_FROM_GSC_DEFAULT true
    #if PVA_SAFETY == 1
        #define PVA_KMD_APP_AUTH_DEFAULT true
    #else
        #define PVA_KMD_APP_AUTH_DEFAULT false
    #endif
#else
    #define PVA_KMD_LOAD_FROM_GSC_DEFAULT false
    #define PVA_KMD_APP_AUTH_DEFAULT false
#endif
// clang-format on

#endif // PVA_KMD_CONSTANTS_H
