/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2023, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

/*
 * Unit: Utility Unit
 * SWUD Document:
 * p4sw-swarm.nvidia.com/view/sw/embedded/docs/projects/active/DRIVE_6.0/QNX/PLC_Work_Products/Element_WPs/Autonomous_Middleware/PVA/04_Unit_Design/PVA_FW/SWE-PVAFW-006-SWUD.pdf
 */
#ifndef PVA_TYPES_H
#define PVA_TYPES_H
#include <stdint.h>

/**
 * @brief Used to represent address (IOVA) in PVA system.
 */
typedef uint64_t pva_iova;

/**
 * @brief Used to store Queue IDs, that represent the
 *        actual hardware queue id between FW and KMD.
 */
typedef uint8_t pva_queue_id_t;

/**
 * @brief Used to store PVE ID, that represents which
 *        PVE is being referred to .
 */
typedef uint8_t pva_pve_id_t;

/**
 * @brief Used to store Status interface ID, that is used
 *        to know through which status needs to be written.
 */
typedef uint8_t pva_status_interface_id_t;

#endif
