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

#ifndef NVDISPLAY_SERVER_OS_DCE_DEVICE_LINUX_H
#define NVDISPLAY_SERVER_OS_DCE_DEVICE_LINUX_H

#include <linux/cdev.h>
#include <linux/types.h>
#include <dce.h>

/**
 * struct dce_device - DCE data structure for storing
 * linux device specific info.
 */
struct dce_device {
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
 * dce_device_from_dce - inline function to get linux os data from the
 *                       os agnostic struct tegra_dc
 * @d : Pointer to the os agnostic tegra_dce data structure.
 *
 * Return : pointer to struct dce_device
 */
static inline struct dce_device *dce_device_from_dce(struct tegra_dce *d)
{
    return container_of(d, struct dce_device, d);
}

/**
 * dev_from_dce - inline function to get linux device from the
 *                os agnostic struct tegra_dce
 * @d : Pointer to the os agnostic tegra_dce data structure.
 *
 * Return : pointer to struct device
 */
static inline struct device *dev_from_dce(struct tegra_dce *d)
{
    return dce_device_from_dce(d)->dev;
}

/**
 * pdata_from_dce - inline function to get dce platform data from
 *                  the os agnostic struct tegra_dc.
 *
 * @d : Pointer to the os agnostic tegra_dce data structure.
 *
 * Return : pointer to struct device
 */
static inline struct dce_platform_data *pdata_from_dce(struct tegra_dce *d)
{
    return ((struct dce_device *)dev_get_drvdata(dev_from_dce(d)))->pdata;
}

#endif /* NVDISPLAY_SERVER_OS_DCE_DEVICE_LINUX_H */