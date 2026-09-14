/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef DCE_OS_DEVICE_H
#define DCE_OS_DEVICE_H

#include <linux/cdev.h>
#include <linux/types.h>
#include <dce.h>

/* Forward declaration */
struct reset_control;
struct dce_psc_client;

/**
 * struct dce_linux_device - DCE data structure for storing
 * OS device specific info.
 */
struct dce_linux_device {
	/**
	 * @d : OS agnostic dce struct. Stores all runitme info for dce cluster
	 * elements.
	 */
	struct tegra_dce d;
	/**
	 * @dev : Pointer to DCE Cluster's Linux device struct.
	 */
	struct device *dev;
	/**
	 * @pdata : Pointer to dce platform data struct.
	 */
	struct dce_platform_data *pdata;
	/**
	 * @max_cpu_irqs : stores maximum no. os irqs from DCE cluster to CPU
	 * for this platform.
	 */
	u8 max_cpu_irqs;
	/**
	 * @regs : Stores the cpu-mapped base address of DCE Cluster. Will be
	 * used for MMIO transactions to DCE elements.
	 */
	void __iomem *regs;
	/**
	 * @dce_fw_load_by_psc : decides whether dce fw is loaded by psc fw if
	 * device tree set the property.
	 */
	bool dce_fw_load_by_psc;
	/**
	 * @rst : Pointer to DCE reset control for managing DCE reset.
	 */
	struct reset_control *rst;
	/**
	 * @psc : Pointer to DCE PSC client instance for communication with
	 * Power Sequencing Controller.
	 */
	struct dce_psc_client *psc;
	/**
	 * @dev : Pointer to PSC Linux device struct.
	 */
	struct device *psc_dev;
#ifdef CONFIG_DEBUG_FS
	/**
	 * @debugfs : Debugfs node for DCE Linux device.
	 */
	struct dentry *debugfs;
	/**
	 * @ext_test_status : Return code for external client tests run via
	 * debugfs
	 */
	s32 ext_test_status;
#endif
};

/**
 * dce_linux_device_from_dce - inline function to get linux os data from the
 *		os agnostic struct tegra_dc
 * @d : Pointer to the os agnostic tegra_dce data structure.
 *
 * Return : pointer to struct dce_linux_device
 */
static inline struct dce_linux_device *dce_linux_device_from_dce(struct tegra_dce *d)
{
	return container_of(d, struct dce_linux_device, d);
}

/**
 * dev_from_dce - inline function to get linux device from the
 *		os agnostic struct tegra_dce
 * @d : Pointer to the os agnostic tegra_dce data structure.
 *
 * Return : pointer to struct device
 */
static inline struct device *dev_from_dce_linux_device(struct tegra_dce *d)
{
	return dce_linux_device_from_dce(d)->dev;
}

/**
 * pdata_from_dce_linux_device - inline function to get dce platform data from
 *		the os agnostic struct tegra_dc.
 *
 * @d : Pointer to the os agnostic tegra_dce data structure.
 *
 * Return : pointer to struct device
 */
static inline struct dce_platform_data *pdata_from_dce_linux_device(struct tegra_dce *d)
{
	return ((struct dce_linux_device *)dev_get_drvdata(dev_from_dce_linux_device(d)))->pdata;
}

#endif /* DCE_OS_DEVICE_H */
