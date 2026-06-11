/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#ifndef _DT_BINDINGS_GPIO_TEGRA264_GPIO_H
#define _DT_BINDINGS_GPIO_TEGRA264_GPIO_H

/* GPIOs implemented by main GPIO controller */
#define TEGRA264_MAIN_GPIO_PORT_F 0
#define TEGRA264_MAIN_GPIO_PORT_G 1
#define TEGRA264_MAIN_GPIO_PORT_H 2
#define TEGRA264_MAIN_GPIO_PORT_J 3
#define TEGRA264_MAIN_GPIO_PORT_K 4
#define TEGRA264_MAIN_GPIO_PORT_L 5
#define TEGRA264_MAIN_GPIO_PORT_M 6
#define TEGRA264_MAIN_GPIO_PORT_P 7
#define TEGRA264_MAIN_GPIO_PORT_Q 8
#define TEGRA264_MAIN_GPIO_PORT_R 9
#define TEGRA264_MAIN_GPIO_PORT_S 10
#define TEGRA264_MAIN_GPIO_PORT_T 11
#define TEGRA264_MAIN_GPIO_PORT_U 12
#define TEGRA264_MAIN_GPIO_PORT_V 13
#define TEGRA264_MAIN_GPIO_PORT_W 14
#define TEGRA264_MAIN_GPIO_PORT_X 15
#define TEGRA264_MAIN_GPIO_PORT_Y 16
#define TEGRA264_MAIN_GPIO_PORT_Z 17
#define TEGRA264_MAIN_GPIO_PORT_AL 18

#define TEGRA264_MAIN_GPIO(port, offset) \
		((TEGRA264_MAIN_GPIO_PORT_##port * 8) + offset)

/* GPIOs implemented by UPHY GPIO controller */
#define TEGRA264_UPHY_GPIO_PORT_A 0
#define TEGRA264_UPHY_GPIO_PORT_B 1
#define TEGRA264_UPHY_GPIO_PORT_C 2
#define TEGRA264_UPHY_GPIO_PORT_D 3
#define TEGRA264_UPHY_GPIO_PORT_E 4

#define TEGRA264_UPHY_GPIO(port, offset) \
		((TEGRA264_UPHY_GPIO_PORT_##port * 8) + offset)


/* GPIOs implemented by AON GPIO controller */
#define TEGRA264_AON_GPIO_PORT_AA 0
#define TEGRA264_AON_GPIO_PORT_BB 1
#define TEGRA264_AON_GPIO_PORT_CC 2
#define TEGRA264_AON_GPIO_PORT_DD 3
#define TEGRA264_AON_GPIO_PORT_EE 4

#define TEGRA264_AON_GPIO(port, offset) \
		((TEGRA264_AON_GPIO_PORT_##port * 8) + offset)

/* GPIOs implemented by FSI GPIO controller */
#define TEGRA264_FSI_GPIO_PORT_AB 0
#define TEGRA264_FSI_GPIO_PORT_AC 1
#define TEGRA264_FSI_GPIO_PORT_AD 2
#define TEGRA264_FSI_GPIO_PORT_AE 3
#define TEGRA264_FSI_GPIO_PORT_AF 4
#define TEGRA264_FSI_GPIO_PORT_AG 5
#define TEGRA264_FSI_GPIO_PORT_AH 6
#define TEGRA264_FSI_GPIO_PORT_AJ 7

#define TEGRA264_FSI_GPIO(port, offset) \
		((TEGRA264_FSI_GPIO_PORT_##port * 8) + offset)

/* All pins */
#define TEGRA_PIN_BASE_ID_A 0
#define TEGRA_PIN_BASE_ID_B 1
#define TEGRA_PIN_BASE_ID_C 2
#define TEGRA_PIN_BASE_ID_D 3
#define TEGRA_PIN_BASE_ID_E 4
#define TEGRA_PIN_BASE_ID_F 5
#define TEGRA_PIN_BASE_ID_G 6
#define TEGRA_PIN_BASE_ID_H 7
#define TEGRA_PIN_BASE_ID_J 8
#define TEGRA_PIN_BASE_ID_K 9
#define TEGRA_PIN_BASE_ID_L 10
#define TEGRA_PIN_BASE_ID_M 11
#define TEGRA_PIN_BASE_ID_P 12
#define TEGRA_PIN_BASE_ID_Q 13
#define TEGRA_PIN_BASE_ID_R 14
#define TEGRA_PIN_BASE_ID_S 15
#define TEGRA_PIN_BASE_ID_T 16
#define TEGRA_PIN_BASE_ID_U 17
#define TEGRA_PIN_BASE_ID_V 18
#define TEGRA_PIN_BASE_ID_W 19
#define TEGRA_PIN_BASE_ID_X 20
#define TEGRA_PIN_BASE_ID_Y 21
#define TEGRA_PIN_BASE_ID_Z 22
#define TEGRA_PIN_BASE_ID_AL 23
#define TEGRA_PIN_BASE_ID_AA 24
#define TEGRA_PIN_BASE_ID_BB 25
#define TEGRA_PIN_BASE_ID_CC 26
#define TEGRA_PIN_BASE_ID_DD 27
#define TEGRA_PIN_BASE_ID_EE 28
#define TEGRA_PIN_BASE_ID_AB 29
#define TEGRA_PIN_BASE_ID_AC 30
#define TEGRA_PIN_BASE_ID_AD 31
#define TEGRA_PIN_BASE_ID_AE 32
#define TEGRA_PIN_BASE_ID_AF 33
#define TEGRA_PIN_BASE_ID_AG 34
#define TEGRA_PIN_BASE_ID_AH 35
#define TEGRA_PIN_BASE_ID_AJ 36

#define TEGRA_PIN_BASE(port) (TEGRA_PIN_BASE_ID_##port * 8)

#define TEGRA264_MAIN_GPIO_RANGE(st, end) \
		((TEGRA264_MAIN_GPIO_PORT_##end - TEGRA264_MAIN_GPIO_PORT_##st + 1) * 8)
#define TEGRA264_MAIN_GPIO_BASE(port) (TEGRA264_MAIN_GPIO_PORT_##port * 8)

#define TEGRA264_AON_GPIO_RANGE(st, end) \
		((TEGRA264_AON_GPIO_PORT_##end - TEGRA264_AON_GPIO_PORT_##st + 1) * 8)
#define TEGRA264_AON_GPIO_BASE(port) (TEGRA264_AON_GPIO_PORT_##port * 8)
#define TEGRA264_FSI_GPIO_RANGE(st, end) \
		((TEGRA264_FSI_GPIO_PORT_##end - TEGRA264_FSI_GPIO_PORT_##st + 1) * 8)
#define TEGRA264_FSI_GPIO_BASE(port) (TEGRA264_FSI_GPIO_PORT_##port * 8)
#define TEGRA264_UPHY_GPIO_RANGE(st, end) \
		((TEGRA264_UPHY_GPIO_PORT_##end - TEGRA264_UPHY_GPIO_PORT_##st + 1) * 8)
#define TEGRA264_UPHY_GPIO_BASE(port) (TEGRA264_UPHY_GPIO_PORT_##port * 8)
#endif
