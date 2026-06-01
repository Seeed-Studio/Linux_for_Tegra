/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
 */

#ifndef PVA_NVHOST_H
#define PVA_NVHOST_H

#include <linux/platform_device.h>
#include <linux/fs.h>

extern const struct file_operations tegra_pva_ctrl_ops;

/**
 * @brief	Finalize the PVA Power-on-Sequence.
 *
 * This function called from host subsystem driver after the PVA
 * partition has been brought up, clocks enabled and reset deasserted.
 * In production mode, the function needs to wait until the ready  bit
 * within the PVA aperture has been set. After that enable the PVA IRQ.
 * Register the queue priorities on the PVA.
 *
 * @param pdev	Pointer to PVA device
 * @return:	0 on Success or negative error code
 *
 */
int pva_finalize_poweron(struct platform_device *pdev);

/**
 * @brief	Prepare PVA poweroff.
 *
 * This function called from host subsystem driver before turning off
 * the PVA. The function should turn off the PVA IRQ.
 *
 * @param pdev	Pointer to PVA device
 * @return	0 on Success or negative error code
 *
 */
int pva_prepare_poweroff(struct platform_device *pdev);

enum tegra_soc_hwpm_ip_reg_op;

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
int pva_hwpm_ip_pm(void *ip_dev, bool disable);

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
int pva_hwpm_ip_reg_op(void *ip_dev,
		       enum tegra_soc_hwpm_ip_reg_op reg_op,
		       u32 inst_element_index, u64 reg_offset,
		       u32 *reg_data);
#endif
