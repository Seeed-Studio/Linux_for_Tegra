/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_SILICON_HWPM_H
#define PVA_KMD_SILICON_HWPM_H
#include "pva_kmd.h"
#include "pva_kmd_shim_debugfs.h"

/**
* @brief	pva_hwpm_ip_pm
*
* This function called from Tegra HWPM driver to
* poweron/off pva device.
*
* @param ip_dev	Pointer to PVA device
* @param disable	disable/enable power management.  PVA is
*			powered on when false.
* @param reg_offset	offset of register relative to PVA HWP base
* @return		0 on Success or negative error code
*
*/
int pva_kmd_hwpm_ip_pm(void *ip_dev, bool disable);

/**
* @brief	pva_hwpm_ip_reg_op
*
* This function called from Tegra HWPM driver to
* access PVA HWPM registers.
*
* @param ip_dev		Pointer to PVA device
* @param reg_op		access operation and can be one of
*				TEGRA_SOC_HWPM_IP_REG_OP_READ
*				TEGRA_SOC_HWPM_IP_REG_OP_WRITE
* @param inst_element_index	element index within PVA instance
* @param reg_offset		offset of register relative to PVA HWP base
* @param reg_data		pointer to where data is to be placed or read.
* @return			0 on Success or negative error code
*
*/
int pva_kmd_hwpm_ip_reg_op(void *ip_dev, uint32_t reg_op,
			   uint32_t inst_element_index, uint64_t reg_offset,
			   uint32_t *reg_data);
#endif //PVA_KMD_SILICON_HWPM_H