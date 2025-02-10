/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/*
 * Unit: Utility Unit
 * SWUD Document:
 * p4sw-swarm.nvidia.com/view/sw/embedded/docs/projects/active/DRIVE_6.0/QNX/PLC_Work_Products/Element_WPs/Autonomous_Middleware/PVA/04_Unit_Design/PVA_FW/SWE-PVAFW-006-SWUD.pdf
 */
#ifndef PVA_PACKED_H
#define PVA_PACKED_H
/**
 * @brief Packed attribute that avoids compiler to add any paddings.
 *        Compiler implicitly adds padding between the structure members
 *        to make it aligned. To avoid this packed attribute is used.
 *        Packed is for shared structures between KMD and FW.
 *        If packed is not used, then we depend on what padding the compiler adds.
 *        Since KMD and FW are compiled by two different compilers, we need to
 *        ensure that the offsets of each member of the structure is the same in
 *        both KMD and FW. To ensure this we pack the structure.
 */
#define PVA_PACKED __attribute__((packed))
#endif // PVA_PACKED_H
