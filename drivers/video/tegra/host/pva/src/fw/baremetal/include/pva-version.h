/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2021 NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/*
 * Unit: Host Interface Unit
 * SWUD Document:
 * p4sw-swarm.nvidia.com/view/sw/embedded/docs/projects/active/DRIVE_6.0/QNX/PLC_Work_Products/Element_WPs/Autonomous_Middleware/PVA/04_Unit_Design/PVA_FW/SWE-PVAFW-006-SWUD.pdf
 */
#ifndef PVA_VERSION_H
#define PVA_VERSION_H

#include <stdint.h>
#include <pva-bit.h>
#include <pva-fw-version.h>

/**
 * @brief Calculate a 32-bit build version with @ref PVA_VERSION_SUBMINOR,
 * @ref PVA_VERSION_MINOR, @ref PVA_VERSION_MAJOR and @ref VERSION_TYPE macros.
 *
 * @param [in] \_type\_ an 8-bit bitfield containing flags indicating which compilation
 * features were enabled when the firmware was compiled.
 *
 * @param [in] \_major\_ an unsigned, 8-bit value containing the major version of the
 * compiled firmware.
 *
 * @param [in] \_minor\_ an unsigned, 8-bit value containing the minor version of the
 * compiled firmware.
 *
 * @param [in] \_subminor\_ an unsigned, 8-bit value containing the sub-minor version
 * of the compiled firmware.
 @verbatim
 | ------------- | ---------------------|
 |  Bit Ranges   |      Function        |
 | ------------- | ---------------------|
 |    7-0        |  subminor version	|
 |   15-8        |  minor version	|
 |   23-16       |  major version  	|
 |   31-24     	 |  version type 	|
 ----------------------------------------
 @endverbatim
 */
#define PVA_MAKE_VERSION(_type_, _major_, _minor_, _subminor_)                 \
	(PVA_INSERT(_type_, 31, 24) | PVA_INSERT(_major_, 23, 16) |            \
	 PVA_INSERT(_minor_, 15, 8) | PVA_INSERT(_subminor_, 7, 0))

/**
 * @brief Calculate PVA R5 FW binary version by calling @ref PVA_MAKE_VERSION macro.
 *
 * @param [in] \_type\_ an 8-bit bitfield containing flags indicating which compilation
 * features were enabled when the firmware was compiled.
 *
 * @see VERSION_TYPE For details on how to construct the @p \_type\_ field.
 *
 * @see PVA_VERSION_MAJOR, PVA_VERSION_MINOR, PVA_VERSION_SUBMINOR for details
 * on the values used at the time this documentation was produced.
 */
#define PVA_VERSION(_type_)                                                    \
	PVA_MAKE_VERSION(_type_, PVA_VERSION_MAJOR, PVA_VERSION_MINOR,         \
			 PVA_VERSION_SUBMINOR)

#endif
