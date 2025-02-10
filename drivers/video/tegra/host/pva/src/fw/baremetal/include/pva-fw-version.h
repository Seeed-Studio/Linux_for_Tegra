/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2022 NVIDIA CORPORATION.  All rights reserved.
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
#ifndef PVA_FW_VERSION_H
#define PVA_FW_VERSION_H

#include <pva-bit.h>

/*
 * Note: Below are doxygen comments with the @def command.
 * This allows the comment to be physically distant from the define
 * being documented.  And allows for a single general comment that is
 * regardless of the being assigned to the macro.
 */

/**
 * @defgroup PVA_VERSION_TYPE_FLAGS VERSION_TYPE Bit Flags
 *
 * @brief The bit flags that indicate the qualities of the Built Firmware.
 * e.g: Debug, Safety, Test Features, etc.
 *
 * @see VERSION_TYPE
 * @{
 */

/**
 * @def VERSION_CODE_DEBUG
 * @brief Set or Clear the 'debug' bit for the FW version type value. For a safety
 * build the value of this define will be zero.
 *
 * @details This bit is set if the macro @r PVA_DEBUG is defined.
 * @see PVA_DEBUG
 */
#if PVA_DEBUG == 1
#define VERSION_CODE_DEBUG PVA_BIT(0)
#else
#define VERSION_CODE_DEBUG (0U)
#endif

/**
 * @def VERSION_CODE_SAFETY
 * @brief Set or Clear the 'safety' bit for the FW version type value.  For a safety
 * build the value of this define will be non-zero.
 *
 * @details This bit is set if the macro @r PVA_SAFETY is defined.
 * Building for Safety disables certain functions that are used for debug, testing,
 * or would otherwise pose a risk to system conforming to safety protocols such as ISO-26262 or
 * ASPICE.
 *
 * @see PVA_SAFETY
 */
#if PVA_SAFETY == 1
#define VERSION_CODE_SAFETY PVA_BIT(1)
#else
#define VERSION_CODE_SAFETY (0U)
#endif

/**
 * @def VERSION_CODE_PVA_TEST_SUPPORT
 * @brief Set or Clear the 'test support' bit for the FW version type value.
 *
 * @details This bit is set if the macro @r TEST_TASK is defined.
 * This bit is expected to be unset during a safety build.
 *
 * Building with tests support enabled may add additional commands to that
 * can be processed by the FW to aid in testing of the system code. Often code of this
 * nature can change the processing, memory, or timing characteristics of the system, and
 * and should only enabled when explicitly needed.
 *
 *
 * @see TEST_TASK
 */
#if TEST_TASK == 1
#define VERSION_CODE_PVA_TEST_SUPPORT PVA_BIT(2)
#else
#define VERSION_CODE_PVA_TEST_SUPPORT (0U)
#endif

/**
 * @def VERSION_CODE_STANDALONE_TESTS
 * @brief Set or Clear the 'standalone tests' bit for the FW version type value.
 *
 * @details This bit is set if the macro @r TEST_TASK is defined.
 * This bit is expected to be unset during a safety build.
 *
 * @see TEST_TASK
 *
 */
#if TEST_TASK == 1
#define VERSION_CODE_STANDALONE_TESTS PVA_BIT(3)
#else
#define VERSION_CODE_STANDALONE_TESTS (0U)
#endif
/** @} */

/**
 * @defgroup PVA_VERSION_MACROS PVA version macros used to calculate the PVA
 * FW binary version.
 * @{
 */

/**
  * @brief An 8-bit bit field that describes which conditionally compiled facets of the Firmware
  * have been enabled.
  *
  * @details The value of this macro is used when constructing a 32-bit Firmware Version identifier.
  *
  @verbatim
  |  Bit  |  Structure Field Name  |  Condition for Enabling  |
  |:-----:|:----------------------:|:------------------------:|
  |  0  |  VERSION_CODE_DEBUG  |  This bit is set when the Firmware is built with @ref PVA_DEBUG defined as equalling 1.
  |  1  |  VERSION_CODE_SAFETY  |  This bit is set when the Firmware is built with @ref PVA_SAFETY defined equalling 1.  |
  |  2  |  VERSION_CODE_PVA_TEST_SUPPORT  |  This bit is set when the Firmware is built with @ref TEST_TASK defined as equalling 1.  |
  |  3  |  VERSION_CODE_STANDALONE_TESTS  |  This bit is set when the Firmware is built with @ref TEST_TASK defined equalling 1. |
  | 4-7 |  Reserved  |  The remaining bits of the bitfield are undefined.  |
  @endverbatim
  * @see PVA_VERSION_TYPE_FLAGS
  */
#define VERSION_TYPE                                                           \
	(uint32_t) VERSION_CODE_DEBUG | (uint32_t)VERSION_CODE_SAFETY |        \
		(uint32_t)VERSION_CODE_PVA_TEST_SUPPORT |                      \
		(uint32_t)VERSION_CODE_STANDALONE_TESTS
/** @} */

/**
 * @defgroup PVA_VERSION_VALUES PVA Major, Minor, and Subminor Version Values
 *
 * @brief The values listed below are applied to the corresponding fields when
 * the PVA_VERSION macro is used.
 *
 * @see PVA_VERSION, PVA_MAKE_VERSION
 * @{
 */

/**
 * @brief The Major version of the Firmware
 */
#define PVA_VERSION_MAJOR 0x08

/**
 * @brief The Minor version of the Firmware
 */
#define PVA_VERSION_MINOR 0x02

/**
 * @brief The sub-minor version of the Firmware.
 */
#define PVA_VERSION_SUBMINOR 0x03
/** @} */

/**
 * @def PVA_VERSION_GCID_REVISION
 * @brief The GCID Revision of the Firmware.
 *
 * @details If this version is not otherwise defined during build time, this fallback value is used.
 */
#ifndef PVA_VERSION_GCID_REVISION
/**
 * @brief GCID revision of PVA FW binary.
 */
#define PVA_VERSION_GCID_REVISION 0x00000000
#endif

/**
 * @def PVA_VERSION_BUILT_ON
 * @brief The date and time the version of software was built, expressed as the number
 * of seconds since the Epoch (00:00:00 UTC, January 1, 1970).
 *
 * @details If this version is not otherwise defined during build time, this fallback value is used.
 */
#ifndef PVA_VERSION_BUILT_ON
#define PVA_VERSION_BUILT_ON 0x00000000
#endif
/** @} */

#endif
