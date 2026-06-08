// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <nvidia/conftest.h>
#include <linux/tegra-camera-rtcpu.h>
#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/interconnect.h>
#include <dt-bindings/interconnect/tegra_icc_id.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/sched.h>
#include <linux/seq_buf.h>
#include <linux/slab.h>
#include <linux/tegra-ivc-bus.h>
#include <linux/pm_domain.h>
#include <soc/tegra/fuse.h>
#include <linux/tegra-rtcpu-monitor.h>
#include <linux/tegra-rtcpu-trace.h>
#include <linux/version.h>
#include <linux/wait.h>
#include <linux/string.h>
#include <linux/sysfs.h>

#include "clk-group.h"
#include "device-group.h"
#include "reset-group.h"
#include "linux/tegra-hsp-combo.h"

#include "soc/tegra/camrtc-commands.h"

#define CAMRTC_NUM_REGS		2
#define CAMRTC_NUM_RESETS	2

struct tegra_cam_rtcpu_pdata {
	const char *name;
	void (*assert_resets)(struct device *);
	int (*deassert_resets)(struct device *);
	int (*wait_for_idle)(struct device *);
	const char * const *reset_names;
	const char * const *reg_names;
	u32 (*pm_r5_ctrl)(void);
	u32 (*pm_pwr_status)(void);
};

/* Register specifics */
#define TEGRA_APS_FRSC_SC_CTL_0			0x0
#define TEGRA_APS_FRSC_SC_MODEIN_0		0x14

#define TEGRA_R5R_SC_DISABLE			0x5
#define TEGRA_FN_MODEIN				0x29527
#define TEGRA_PM_FWLOADDONE			0x2
#define TEGRA_PM_WFIPIPESTOPPED			0x200000

#define AMISC_ADSP_STATUS			0x14
#define AMISC_ADSP_L2_IDLE			BIT(31)
#define AMISC_ADSP_L2_CLKSTOPPED		BIT(30)

static int tegra_rce_cam_wait_for_idle(struct device *dev);
static void tegra_rce_cam_assert_resets(struct device *dev);
static int tegra_rce_cam_deassert_resets(struct device *dev);
static int tegra_camrtc_fw_set_operating_point(struct device *dev, uint32_t op);

static const char * const rce_reset_names[] = {
	"reset-names",			/* all named resets */
	NULL,
};

/* SCE and RCE share the PM regs */
static const char * const rce_reg_names[] = {
	"rce-pm",
	NULL,
};

static u32 rce_pm_r5_ctrl(void)
{
	return 0x40;
}

static u32 rce_pm_pwr_status(void)
{
	return 0x20;
}

static const struct tegra_cam_rtcpu_pdata rce_pdata = {
	.name = "rce",
	.wait_for_idle = tegra_rce_cam_wait_for_idle,
	.assert_resets = tegra_rce_cam_assert_resets,
	.deassert_resets = tegra_rce_cam_deassert_resets,
	.reset_names = rce_reset_names,
	.reg_names = rce_reg_names,
	.pm_r5_ctrl = rce_pm_r5_ctrl,
	.pm_pwr_status = rce_pm_pwr_status,
};

static u32 t264_rce_pm_r5_ctrl(void)
{
	return 0x3008;
}

static u32 t264_rce_pm_pwr_status(void)
{
	return 0x5004;
}

static const struct tegra_cam_rtcpu_pdata t264_rce_pdata = {
	.name = "t264_rce",
	.wait_for_idle = tegra_rce_cam_wait_for_idle,
	.assert_resets = tegra_rce_cam_assert_resets,
	.deassert_resets = tegra_rce_cam_deassert_resets,
	.reset_names = rce_reset_names,
	.reg_names = rce_reg_names,
	.pm_r5_ctrl = t264_rce_pm_r5_ctrl,
	.pm_pwr_status = t264_rce_pm_pwr_status,
};

#define NV(p) "nvidia," #p

struct tegra_cam_rtcpu {
	const char *name;
	struct tegra_ivc_bus *ivc;
	struct device_dma_parameters dma_parms;
	struct camrtc_hsp *hsp;
	struct tegra_rtcpu_trace *tracer;
	u32 cmd_timeout;
	u32 fw_version;
	u8 fw_hash[RTCPU_FW_HASH_SIZE];
	struct {
		u64 reset_complete;
		u64 boot_handshake;
	} stats;
	union {
		void __iomem *regs[CAMRTC_NUM_REGS];
		struct {
			void __iomem *pm_base;
			void __iomem *cfg_base;
		};
	};
	struct camrtc_clk_group *clocks;
	struct camrtc_reset_group *resets[CAMRTC_NUM_RESETS];
	const struct tegra_cam_rtcpu_pdata *pdata;
	struct camrtc_device_group *camera_devices;
	struct icc_path *icc_path;
	u32 mem_bw;
	struct tegra_camrtc_mon *monitor;
	u32 max_reboot_retry;
	atomic_t rebooting;
	bool powered;
	bool boot_sync_done;
	bool fw_active;
	bool online;
};

static struct device *s_dev;

static uint32_t operating_point;

static ssize_t show_operating_point(struct kobject *kobj, struct kobj_attribute *attr, char *buff)
{
	sprintf(buff, "%d", operating_point);

	return strlen(buff);
}

static ssize_t store_operating_point(struct  kobject *kobj, struct kobj_attribute *attr,
	const char *buff, size_t count)
{
	u32 temp;

	if (kstrtou32(buff, 10, &temp) == 0) {
		if ((temp == 0) || (temp == 6)) {
			operating_point = (uint32_t)temp;
			tegra_camrtc_fw_set_operating_point(s_dev, operating_point);
		}
	}

	return count;
}

static struct kobj_attribute operating_point_attribute =
	__ATTR(operating_point, 0644, show_operating_point, store_operating_point);

static struct attribute *attrs[] = {
	&operating_point_attribute.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

static struct kobject *kobj;

/**
 * @brief Initialize operating point sysfs entries
 *
 * This function creates sysfs entries for controlling the operating point.
 * - Initializes the operating_point variable to 0 using assignment
 * - Creates a kobject under kernel_kobj named "operating_point" using @ref kobject_create_and_add()
 * - Checks if the kobject creation was successful
 * - Returns -ENOMEM if kobject creation fails
 * - Creates a sysfs attribute group using @ref sysfs_create_group()
 * - Cleans up the kobject if attribute group creation fails using @ref kobject_put()
 * - Returns the result (success or error) of the operations
 *
 * @retval -ENOMEM   If kobject creation fails
 * @retval (int)     Value returned by @ref sysfs_create_group()
 */
static int init_operating_point_sysfs(void)
{
	int ret;

	operating_point = 0;

	kobj = kobject_create_and_add("operating_point", kernel_kobj);
	if (!kobj)
		return -ENOMEM;
	ret = sysfs_create_group(kobj, &attr_group);
	if (ret)
		kobject_put(kobj);
	return ret;
}

/**
 * @brief Clean up operating point sysfs entries
 *
 * This function removes the sysfs entries for controlling the operating point.
 * - Releases the kobject created by @ref init_operating_point_sysfs() using @ref kobject_put()
 */
static void deinit_operating_point_sysfs(void)
{
	kobject_put(kobj);
}

/**
 * @brief Map a device resource to an ioremap
 *
 * This function maps a device resource to an ioremap
 * - Retrieves the resource from the device tree using @ref of_address_to_resource()
 * - Returns an error pointer if the resource retrieval fails
 * - Maps the resource using @ref devm_ioremap_resource()
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 * @param[in] index Index of the resource to map
 *                  Valid value: [INT_MIN, INT_MAX]
 *
 * @retval (void *) Return value from @ref devm_ioremap_resource()
 * @retval IOMEM_ERR_PTR(int) If the resource retrieval fails
 */
static void __iomem *tegra_cam_ioremap(struct device *dev, int index)
{
	struct resource mem;
	int err = of_address_to_resource(dev->of_node, index, &mem);
	if (err)
		return IOMEM_ERR_PTR(err);

	/* NOTE: assumes size is large enough for caller */
	return devm_ioremap_resource(dev, &mem);
}

/**
 * @brief Map a device resource by name
 *
 * This function maps a device resource by name
 * - Retrieves the index of the resource from the device tree using @ref of_property_match_string()
 * - Returns an error pointer if the resource retrieval fails
 * - Maps the resource using @ref tegra_cam_ioremap()
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 * @param[in] name  Name of the resource to map
 *                  Valid value: non-NULL
 *
 * @retval (void *) Return value from @ref tegra_cam_ioremap()
 * @retval IOMEM_ERR_PTR(-ENOENT) If the resource retrieval fails
 */
static void __iomem *tegra_cam_ioremap_byname(struct device *dev,
					const char *name)
{
	int index = of_property_match_string(dev->of_node, "reg-names", name);
	if (index < 0)
		return IOMEM_ERR_PTR(-ENOENT);
	return tegra_cam_ioremap(dev, index);
}

/**
 * @brief Get resources for the camera RTCPU
 *
 * This function gets the resources for the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Retrieves the platform data for the device using @ref rtcpu->pdata
 * - Retrieves the clocks for the device using @ref camrtc_clk_group_get()
 * - Retrieves the device group for the device using @ref camrtc_device_group_get()
 * - Retrieves the reset group for the device using @ref camrtc_reset_group_get()
 * - Retrieves the registers for the device using @ref tegra_cam_ioremap_byname()
 * - Returns an error if the resource retrieval fails
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 *
 * @retval 0         On successful resource retrieval
 * @retval PTR_ERR(err) If error from @ref camrtc_reset_group_get() or
 * @ref camrtc_device_group_get()
 * @retval -EPROBE_DEFER If the resource retrieval fails
 */
static int tegra_camrtc_get_resources(struct device *dev)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);
	const struct tegra_cam_rtcpu_pdata *pdata = rtcpu->pdata;
	struct camrtc_device_group *devgrp;
	int i, err;

	rtcpu->clocks = camrtc_clk_group_get(dev);
	if (IS_ERR(rtcpu->clocks)) {
		err = PTR_ERR(rtcpu->clocks);
		if (err == -EPROBE_DEFER)
			dev_info(dev, "defer %s probe because of %s\n",
				rtcpu->name, "clocks");
		else
			dev_warn(dev, "clocks not available: %d\n", err);
		return err;
	}

	devgrp = camrtc_device_group_get(dev, "nvidia,camera-devices",
		"nvidia,camera-device-names");
	if (!IS_ERR(devgrp)) {
		rtcpu->camera_devices = devgrp;
	} else {
		err = PTR_ERR(devgrp);
		if (err == -EPROBE_DEFER)
			return err;
		if (err != -ENODATA && err != -ENOENT)
			dev_warn(dev, "get %s: failed: %d\n",
				"nvidia,camera-devices", err);
	}

#define GET_RESOURCES(_res_, _get_, _null_, _toerr)	\
	for (i = 0; i < ARRAY_SIZE(rtcpu->_res_##s); i++) { \
		if (!pdata->_res_##_names[i]) \
			break; \
		rtcpu->_res_##s[i] = _get_(dev, pdata->_res_##_names[i]); \
		err = _toerr(rtcpu->_res_##s[i]); \
		if (err == 0) \
			continue; \
		rtcpu->_res_##s[i] = _null_; \
		if (err == -EPROBE_DEFER) { \
			dev_info(dev, "defer %s probe because %s %s\n", \
				rtcpu->name, #_res_, pdata->_res_##_names[i]); \
			return err; \
		} \
		if (err != -ENODATA && err != -ENOENT) \
			dev_warn(dev, "%s %s not available: %d\n", #_res_, \
				pdata->_res_##_names[i], err); \
	}

#define _PTR2ERR(x) (IS_ERR(x) ? PTR_ERR(x) : 0)

	GET_RESOURCES(reset, camrtc_reset_group_get, NULL, _PTR2ERR);
	GET_RESOURCES(reg, tegra_cam_ioremap_byname, NULL, _PTR2ERR);

#undef _PTR2ERR

	if (rtcpu->resets[0] == NULL) {
		struct camrtc_reset_group *resets;

		resets = camrtc_reset_group_get(dev, NULL);

		if (!IS_ERR(resets))
			rtcpu->resets[0] = resets;
		else if (PTR_ERR(resets) == -EPROBE_DEFER) {
			dev_info(dev, "defer %s probe because of %s\n",
				rtcpu->name, "resets");
			return -EPROBE_DEFER;
		}
	}

	return 0;
}

/**
 * @brief Enable the clocks for the camera RTCPU
 *
 * This function enables the clocks for the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Retrieves the clocks for the device using @ref camrtc_clk_group_enable()
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 *
 * @retval (int) Return value from @ref camrtc_clk_group_enable()
 */
static int tegra_camrtc_enable_clks(struct device *dev)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);

	return camrtc_clk_group_enable(rtcpu->clocks);
}

/**
 * @brief Disable the clocks for the camera RTCPU
 *
 * This function disables the clocks for the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Retrieves the clocks for the device using @ref camrtc_clk_group_disable()
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 *
 * @retval (int) Return value from @ref camrtc_clk_group_disable()
 */
static void tegra_camrtc_disable_clks(struct device *dev)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);

	return camrtc_clk_group_disable(rtcpu->clocks);
}

/**
 * @brief Assert the resets for the camera RTCPU
 *
 * This function asserts the resets for the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Retrieves the platform data for the device using @ref rtcpu->pdata
 * - Calls the platform data's assert_resets function
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 */
static void tegra_camrtc_assert_resets(struct device *dev)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);

	if (rtcpu->pdata->assert_resets)
		rtcpu->pdata->assert_resets(dev);
}

/**
 * @brief Deassert the resets for the camera RTCPU
 *
 * This function deasserts the resets for the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Retrieves the platform data for the device using @ref rtcpu->pdata
 * - Calls the platform data's @ref deassert_resets function
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 *
 * @retval (int) Return value from @ref camrtc_reset_group_deassert()
 */
static int tegra_camrtc_deassert_resets(struct device *dev)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);
	int ret = 0;

	if (rtcpu->pdata->deassert_resets) {
		ret = rtcpu->pdata->deassert_resets(dev);
		rtcpu->stats.reset_complete = ktime_get_ns();
		rtcpu->stats.boot_handshake = 0;
	}

	return ret;
}

#define CAMRTC_MAX_BW (0xFFFFFFFFU)
#define RCE_MAX_BW_MBPS (160)

/**
 * @brief Initialize the ICC for the camera RTCPU
 *
 * This function initializes the ICC for the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - If the bandwidth is CAMRTC_MAX_BW, sets the memory bandwidth to RCE_MAX_BW_MBPS using
 *   @ref MBps_to_icc()
 * - Otherwise, sets the memory bandwidth to the provided bandwidth
 * - Retrieves the ICC path for the device using @ref devm_of_icc_get()
 * - Sets the icc path to NULL if the retrieval fails
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 * @param[in] bw    Memory bandwidth to set
 *                  Valid value: [0, CAMRTC_MAX_BW]
 */
static void tegra_camrtc_init_icc(struct device *dev, u32 bw)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);

	if (bw == CAMRTC_MAX_BW)
		rtcpu->mem_bw = MBps_to_icc(RCE_MAX_BW_MBPS);
	else
		rtcpu->mem_bw = bw;

	rtcpu->icc_path =  devm_of_icc_get(dev, "write");
	if (IS_ERR(rtcpu->icc_path)) {
		dev_warn(dev, "no interconnect control, err:%ld\n",
				PTR_ERR(rtcpu->icc_path));
		rtcpu->icc_path = NULL;
		return;
	}

	dev_dbg(dev, "using icc rate %u for power-on\n", rtcpu->mem_bw);
}

/**
 * @brief Initialize the memory bandwidth for the camera RTCPU
 *
 * This function initializes the memory bandwidth for the camera RTCPU
 * - Calls @ref tegra_camrtc_init_icc() with CAMRTC_MAX_BW
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 */
static void tegra_camrtc_init_membw(struct device *dev)
{
	tegra_camrtc_init_icc(dev, CAMRTC_MAX_BW);
}

/**
 * @brief Set the full memory bandwidth for the camera RTCPU
 *
 * This function sets the full memory bandwidth for the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Retrieves the ICC path for the device using @ref rtcpu->icc_path
 * - If the ICC path is not NULL, sets the memory bandwidth to the provided bandwidth using
 *   @ref icc_set_bw()
 * - Returns an error if the memory bandwidth setting fails
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 */
static void tegra_camrtc_full_mem_bw(struct device *dev)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);

	if (rtcpu->icc_path != NULL) {
		int ret = icc_set_bw(rtcpu->icc_path, 0, rtcpu->mem_bw);

		if (ret)
			dev_err(dev, "set icc bw [%u] failed: %d\n", rtcpu->mem_bw, ret);
		else
			dev_dbg(dev, "requested icc bw %u\n", rtcpu->mem_bw);
	}
}

/**
 * @brief Set the slow memory bandwidth for the camera RTCPU
 *
 * This function sets the slow memory bandwidth for the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Retrieves the ICC path for the device using @ref rtcpu->icc_path
 * - If the ICC path is not NULL, sets the memory bandwidth to 0 using @ref icc_set_bw()
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 */
static void tegra_camrtc_slow_mem_bw(struct device *dev)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);

	if (rtcpu->icc_path != NULL)
		(void)icc_set_bw(rtcpu->icc_path, 0, 0);
}

/**
 * @brief Set the fwloaddone flag for the camera RTCPU
 *
 * This function sets the fwloaddone flag for the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Retrieves the PM base for the device using @ref rtcpu->pm_base
 * - Reads the PM R5 control register using @ref readl()
 * - If the fwloaddone flag is true, sets the TEGRA_PM_FWLOADDONE bit using @ref writel()
 * - Otherwise, clears the TEGRA_PM_FWLOADDONE bit using @ref writel()
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 * @param[in] fwloaddone   FWLOADDONE flag to set
 *                  Valid value: true or false
 */
static void tegra_camrtc_set_fwloaddone(struct device *dev, bool fwloaddone)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);

	if (rtcpu->pm_base != NULL) {
		u32 val = readl(rtcpu->pm_base + rtcpu->pdata->pm_r5_ctrl());

		if (fwloaddone)
			val |= TEGRA_PM_FWLOADDONE;
		else
			val &= ~TEGRA_PM_FWLOADDONE;

		writel(val, rtcpu->pm_base + rtcpu->pdata->pm_r5_ctrl());
	}
}

/**
 * @brief Wait for the camera RTCPU to be idle
 *
 * This function waits for the camera RTCPU to be idle
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Retrieves the PM base for the device using @ref rtcpu->pm_base
 * - Reads the PM power status register using @ref readl()
 * - If the PM power status register is not NULL, polls for the WFI assert using @ref readl()
 * - If the timeout is less than 0, returns -EBUSY
 * - Calls @ref msleep() with a delay stride of HZ / 50
 * - Returns 0 on success
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 *
 * @retval 0  If @ref rtcpu->pm_base is NULL or successful
 * @retval -EBUSY  If timeout occurs
 */
static int tegra_rce_cam_wait_for_idle(struct device *dev)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);
	long timeout = rtcpu->cmd_timeout;
	long delay_stride = HZ / 50;

	if (rtcpu->pm_base == NULL)
		return 0;

	/* Poll for WFI assert.*/
	for (;;) {
		u32 val = readl(rtcpu->pm_base + rtcpu->pdata->pm_pwr_status());

		if ((val & TEGRA_PM_WFIPIPESTOPPED) == 0)
			break;

		if (timeout < 0) {
			dev_info(dev, "timeout waiting for WFI\n");
			return -EBUSY;
		}

		msleep(delay_stride);
		timeout -= delay_stride;
	}

	return 0;
}

/**
 * @brief Set the operating point for the camera RTCPU
 *
 * This function sets the operating point for the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Retrieves the HSP for the device using @ref rtcpu->hsp
 * - Returns 0 if the HSP is not NULL
 * - Returns the return value from @ref camrtc_hsp_set_operating_point()
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 * @param[in] op    Operating point to set
 *                  Valid value: [0, UINT32_MAX]
 *
 * @retval (int) Return value from @ref camrtc_hsp_set_operating_point()
 * @retval 0 if the HSP is NULL
 */
static int tegra_camrtc_fw_set_operating_point(struct device *dev, uint32_t op)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);

	if (!rtcpu->hsp)
		return 0;

	return camrtc_hsp_set_operating_point(rtcpu->hsp, op);
}

/**
 * @brief Deassert the resets for the camera RTCPU
 *
 * This function deasserts the resets for the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Retrieves the reset group for the device using @ref camrtc_reset_group_deassert()
 * - Returns an error if the reset group deassertion fails
 * - Sets the fwloaddone flag to true using @ref tegra_camrtc_set_fwloaddone()
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 *
 * @retval (int) Return value from @ref camrtc_reset_group_deassert()
 * @retval 0 On successful reset group deassertion
 */
static int tegra_rce_cam_deassert_resets(struct device *dev)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);
	int err;

	err = camrtc_reset_group_deassert(rtcpu->resets[0]);
	if (err)
		return err;

	/* nCPUHALT is a reset controlled by PM, not by CAR. */
	tegra_camrtc_set_fwloaddone(dev, true);

	return 0;
}

/**
 * @brief Assert the resets for the camera RTCPU
 *
 * This function asserts the resets for the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Retrieves the reset group for the device using @ref camrtc_reset_group_assert()
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 */
static void tegra_rce_cam_assert_resets(struct device *dev)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);

	camrtc_reset_group_assert(rtcpu->resets[0]);
}

/**
 * @brief Wait for the camera RTCPU to be idle
 *
 * This function waits for the camera RTCPU to be idle
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Retrieves the wait_for_idle function from the platform data using
 *   @ref rtcpu->pdata->wait_for_idle
 * - Returns the return value from @ref rtcpu->pdata->wait_for_idle
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 *
 * @retval (int) Return value from @ref rtcpu->pdata->wait_for_idle
 */
static int tegra_camrtc_wait_for_idle(struct device *dev)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);

	return rtcpu->pdata->wait_for_idle(dev);
}

/**
 * @brief Suspend the camera RTCPU
 *
 * This function suspends the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Checks if the fw_active flag is true and the HSP is not NULL
 * - Sets the fw_active flag to false
 * - Calls @ref camrtc_hsp_suspend() to suspend the RTCPU
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 *
 * @retval (int) Return value from @ref camrtc_hsp_suspend()
 * @retval 0  if the fw_active flag is false or the HSP is NULL
 */
static int tegra_camrtc_fw_suspend(struct device *dev)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);

	if (!rtcpu->fw_active || !rtcpu->hsp)
		return 0;

	rtcpu->fw_active = false;

	return camrtc_hsp_suspend(rtcpu->hsp);
}

/**
 * @brief Setup the shared memory for the camera RTCPU
 *
 * This function sets up the shared memory for the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Calls @ref tegra_rtcpu_trace_boot_sync() to set up the trace
 * - Calls @ref tegra_ivc_bus_boot_sync() to set up the IVC services
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 *
 * @retval (int) Return value from @ref tegra_ivc_bus_boot_sync()
 */
static int tegra_camrtc_setup_shared_memory(struct device *dev)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);
	int ret;

	/*
	 * Set-up trace
	 */
	ret = tegra_rtcpu_trace_boot_sync(rtcpu->tracer);
	if (ret < 0)
		dev_err(dev, "trace boot sync failed: %d\n", ret);

	/*
	 * Set-up and activate the IVC services in firmware
	 */
	ret = tegra_ivc_bus_boot_sync(rtcpu->ivc, &tegra_camrtc_iovm_setup);
	if (ret < 0)
		dev_err(dev, "ivc-bus boot sync failed: %d\n", ret);

	return ret;
}

/**
 * @brief Set the online status for the camera RTCPU
 *
 * This function sets the online status for the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Checks if the online status is already set
 * - If the online status is already set, returns
 * - If the online status is not set, calls @ref tegra_camrtc_setup_shared_memory()
 *   to set up the shared memory
 * - Sets the online status to the provided status
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 * @param[in] online   Online status to set
 *                  Valid value: true or false
 */
static void tegra_camrtc_set_online(struct device *dev, bool online)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);

	if (online == rtcpu->online)
		return;

	if (online) {
		if (tegra_camrtc_setup_shared_memory(dev) < 0)
			return;
	}

	/* Postpone the online transition if still probing */
	if (!IS_ERR_OR_NULL(rtcpu->ivc)) {
		rtcpu->online = online;
		tegra_ivc_bus_ready(rtcpu->ivc, online);
	}
}

/**
 * @brief Ping the camera RTCPU
 *
 * This function pings the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Calls @ref camrtc_hsp_ping() to ping the RTCPU
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 * @param[in] data   Data to ping the RTCPU with
 *                  Valid value: [0, UINT32_MAX]
 * @param[in] timeout   Timeout for the ping
 *                  Valid value: [0, UINT32_MAX]
 *
 * @retval (int) Return value from @ref camrtc_hsp_ping()
 */
int tegra_camrtc_ping(struct device *dev, u32 data, long timeout)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);

	return camrtc_hsp_ping(rtcpu->hsp, data, timeout);
}
EXPORT_SYMBOL(tegra_camrtc_ping);

/**
 * @brief Notify the camera RTCPU
 *
 * This function notifies the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Checks if the IVC bus is not NULL
 * - Calls @ref tegra_ivc_bus_notify() to notify the RTCPU
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 * @param[in] group   Group to notify the RTCPU with
 *                  Valid value: [0, UINT16_MAX]
 */
static void tegra_camrtc_ivc_notify(struct device *dev, u16 group)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);

	if (rtcpu->ivc)
		tegra_ivc_bus_notify(rtcpu->ivc, group);
}

/**
 * @brief Power on the camera RTCPU
 *
 * This function powers on the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Checks if the RTCPU is already powered
 * - If the RTCPU is already powered, returns 0
 * - If the RTCPU is not powered, calls @ref tegra_camrtc_enable_clks() to enable the clocks
 * - Calls @ref tegra_camrtc_deassert_resets() to deassert the resets
 * - Sets the powered flag to true
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 * @param[in] full_speed   Full speed flag to set
 *                  Valid value: true or false
 *
 * @retval (int) Return value from @ref tegra_camrtc_enable_clks() or
 * @ref tegra_camrtc_deassert_resets()
 * @retval 0 On successful power on
 */
static int tegra_camrtc_poweron(struct device *dev, bool full_speed)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);
	int ret;

	if (rtcpu->powered) {
		if (full_speed)
			camrtc_clk_group_adjust_fast(rtcpu->clocks);
		return 0;
	}

	/* Power on and let core run */
	ret = tegra_camrtc_enable_clks(dev);
	if (ret) {
		dev_err(dev, "failed to turn on %s clocks: %d\n",
			rtcpu->name, ret);
		return ret;
	}

	if (full_speed)
		camrtc_clk_group_adjust_fast(rtcpu->clocks);

	ret = tegra_camrtc_deassert_resets(dev);
	if (ret)
		return ret;

	rtcpu->powered = true;

	return 0;
}

/**
 * @brief Power off the camera RTCPU
 *
 * This function powers off the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Checks if the RTCPU is already powered
 * - If the RTCPU is not powered, returns
 * - Calls @ref tegra_camrtc_assert_resets() to assert the resets
 * - Calls @ref tegra_camrtc_disable_clks() to disable the clocks
 * - Sets the powered flag to false
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 */
static void tegra_camrtc_poweroff(struct device *dev)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);

	if (!rtcpu->powered)
		return;

	rtcpu->powered = false;
	rtcpu->boot_sync_done = false;
	rtcpu->fw_active = false;

	tegra_camrtc_assert_resets(dev);
	tegra_camrtc_disable_clks(dev);
}

/**
 * @brief Boot sync the camera RTCPU
 *
 * This function boots syncs the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Checks if the boot sync is already done
 * - If the boot sync is already done, returns
 * - Calls @ref camrtc_hsp_sync() to sync the RTCPU
 * - Sets the boot sync done flag to true
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 *
 * @retval (int) Return value from @ref camrtc_hsp_sync() or
 * @ref camrtc_hsp_resume()
 * @retval 0 On successful boot sync
 */
static int tegra_camrtc_boot_sync(struct device *dev)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);
	int ret;

	if (!rtcpu->boot_sync_done) {
		ret = camrtc_hsp_sync(rtcpu->hsp);
		if (ret < 0)
			return ret;

		rtcpu->fw_version = ret;
		rtcpu->boot_sync_done = true;
	}

	if (!rtcpu->fw_active) {
		ret = camrtc_hsp_resume(rtcpu->hsp);
		if (ret < 0)
			return ret;

		rtcpu->fw_active = true;
	}

	return 0;
}

/**
 * @brief Boot the camera RTCPU
 *
 * This function boots the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Calls @ref tegra_camrtc_poweron() to power on the RTCPU
 * - Calls @ref tegra_camrtc_full_mem_bw() to set the full memory bandwidth
 * - Loops until the RTCPU is online
 * - If the RTCPU is online, breaks the loop
 * - If the RTCPU is not online, retries the boot sequence
 * - If the RTCPU is not online after the max number of retries, breaks the loop
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 *
 * @retval (int) Return value from @ref tegra_camrtc_poweron()
 * @retval 0 On successful boot
 */
static int tegra_camrtc_boot(struct device *dev)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);
	int retry = 0, max_retries = rtcpu->max_reboot_retry;
	int ret;

	ret = tegra_camrtc_poweron(dev, true);
	if (ret)
		return ret;

	tegra_camrtc_full_mem_bw(dev);

	for (;;) {
		ret = tegra_camrtc_boot_sync(dev);

		tegra_camrtc_set_online(dev, ret == 0);

		if (ret == 0)
			break;
		if (retry++ == max_retries)
			break;
		if (retry > 1) {
			dev_warn(dev, "%s full reset, retry %u/%u\n",
				rtcpu->name, retry, max_retries);
			tegra_camrtc_assert_resets(dev);
			usleep_range(10, 30);
			tegra_camrtc_deassert_resets(dev);
		}
	}

	tegra_camrtc_slow_mem_bw(dev);

	return 0;
}

/**
 * @brief Setup the IOVM for the camera RTCPU
 *
 * This function sets up the IOVM for the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Calls @ref camrtc_hsp_ch_setup() to setup the IOVM
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 * @param[in] iova   IOVA to setup the IOVM with
 *                  Valid value: [0, UINT64_MAX]
 *
 * @retval (int) Return value from @ref camrtc_hsp_ch_setup()
 */
int tegra_camrtc_iovm_setup(struct device *dev, dma_addr_t iova)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);

	return camrtc_hsp_ch_setup(rtcpu->hsp, iova);
}
EXPORT_SYMBOL(tegra_camrtc_iovm_setup);

/**
 * @brief Print the version of the camera RTCPU
 *
 * This function prints the version of the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Calls @ref seq_buf_init() to initialize the sequence buffer
 * - Calls @ref seq_buf_printf() to print the version of the RTCPU
 * - Returns the number of bytes printed
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 * @param[in] buf   Buffer to print the version to
 *                  Valid value: non-NULL
 * @param[in] size   Size of the buffer
 *                  Valid value: [0, UINT32_MAX]
 *
 * @retval (ssize_t) Number of bytes printed using @ref seq_buf_used()
 */
ssize_t tegra_camrtc_print_version(struct device *dev,
					char *buf, size_t size)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);
	struct seq_buf s;
	int i;

	seq_buf_init(&s, buf, size);
	seq_buf_printf(&s, "version cpu=%s cmd=%u sha1=",
		rtcpu->name, rtcpu->fw_version);

	for (i = 0; i < RTCPU_FW_HASH_SIZE; i++)
		seq_buf_printf(&s, "%02x", rtcpu->fw_hash[i]);

	return seq_buf_used(&s);
}
EXPORT_SYMBOL(tegra_camrtc_print_version);

/**
 * @brief Log the firmware version of the camera RTCPU
 *
 * This function logs the firmware version of the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Calls @ref tegra_camrtc_print_version() to print the version of the RTCPU
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 */
static void tegra_camrtc_log_fw_version(struct device *dev)
{
	char version[TEGRA_CAMRTC_VERSION_LEN];

	tegra_camrtc_print_version(dev, version, sizeof(version));

	dev_info(dev, "firmware %s\n", version);
}

/**
 * @brief Start the PM of the camera RTCPU
 *
 * This function starts the PM of the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 * @param[in] op   Operation to log
 *                  Valid value: non-NULL
 */
static void tegra_camrtc_pm_start(struct device *dev, char const *op)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);

	dev_dbg(dev, "start %s [powered=%d synced=%d active=%d online=%d]\n",
		op, rtcpu->powered, rtcpu->boot_sync_done,
		rtcpu->fw_active, rtcpu->online);
}

/**
 * @brief Done the PM of the camera RTCPU
 *
 * This function done the PM of the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 * @param[in] op   Operation to log
 *                  Valid value: non-NULL
 * @param[in] err   Error code
 *                  Valid value: [0, INT32_MAX]
 */
static void tegra_camrtc_pm_done(struct device *dev, char const *op, int err)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);

	dev_dbg(dev, "done %s err=%d [powered=%d synced=%d active=%d online=%d]\n",
		op, err, rtcpu->powered, rtcpu->boot_sync_done,
		rtcpu->fw_active, rtcpu->online);
}

/**
 * @brief Runtime suspend the camera RTCPU
 *
 * This function runtime suspends the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Calls @ref tegra_camrtc_pm_start() to start the PM
 * - Calls @ref tegra_camrtc_fw_suspend() to suspend the RTCPU
 * - If the RTCPU suspend fails, resets the RTCPU
 * - Calls @ref tegra_camrtc_poweroff() to power off the RTCPU
 * - Sets the online flag to false
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 *
 * @retval 0 On successful runtime suspend
 */
static int tegra_cam_rtcpu_runtime_suspend(struct device *dev)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);
	int err;

	tegra_camrtc_pm_start(dev, "runtime_suspend");

	err = tegra_camrtc_fw_suspend(dev);
	/* Try full reset if an error occurred while suspending core. */
	if (err < 0) {

		dev_info(dev, "RTCPU suspend failed, resetting it");

		/* runtime_resume() powers RTCPU back on */
		tegra_camrtc_poweroff(dev);

		/* We want to boot sync IVC and trace when resuming */
		tegra_camrtc_set_online(dev, false);
	}

	camrtc_clk_group_adjust_slow(rtcpu->clocks);

	tegra_camrtc_pm_done(dev, "runtime_suspend", err);

	return 0;
}

/**
 * @brief Runtime resume the camera RTCPU
 *
 * This function runtime resumes the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Calls @ref tegra_camrtc_pm_start() to start the PM
 * - Calls @ref tegra_camrtc_boot() to boot the RTCPU
 * - Calls @ref tegra_camrtc_pm_done() to done the PM
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 *
 * @retval (int) Return value from @ref tegra_camrtc_boot()
 * @retval 0 On successful runtime resume
 */
static int tegra_cam_rtcpu_runtime_resume(struct device *dev)
{
	int err;

	tegra_camrtc_pm_start(dev, "runtime_resume");

	err = tegra_camrtc_boot(dev);

	tegra_camrtc_pm_done(dev, "runtime_resume", err);

	return err;
}

/**
 * @brief Runtime idle the camera RTCPU
 *
 * This function runtime idles the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Calls @ref pm_runtime_mark_last_busy() to mark the last busy time
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 *
 * @retval (int) 0
 */
static int tegra_cam_rtcpu_runtime_idle(struct device *dev)
{
	pm_runtime_mark_last_busy(dev);

	return 0;
}

/**
 * @brief Callback function triggered upon receiving CAMRTC_HSP_PANIC message.
 *
 * This function is registered with the HSP mailbox client. When an RCE panic
 * occurs, this callback retrieves the RTCPU tracer associated with the device
 * and flushes the snapshot portion of the trace buffer to capture RCE state
 * at the time of panic.
 *
 * Checks for NULL input parameters (`dev`, `rtcpu`, `tracer`) before proceeding.
 *
 * @param[in] dev Pointer to the parent device associated with the HSP client.
 *                Must not be NULL. Used to retrieve driver data.
 */
void rtcpu_trace_panic_callback(struct device *dev)
{
	struct tegra_cam_rtcpu *rtcpu = NULL;
	struct tegra_rtcpu_trace *tracer =  NULL;
	if (dev == NULL) {
		dev_err(dev, "%s: input dev handle is null\n", __func__);
		return;
	}

	rtcpu = dev_get_drvdata(dev);
	if (rtcpu == NULL) {
		dev_err(dev, "%s: input rtcpu handle is null\n", __func__);
		return;
	}

	tracer = rtcpu->tracer;
	if (tracer == NULL) {
		dev_err(dev, "%s: input tracer handle is null\n", __func__);
		return;
	}

	/* Call the accessor function to set panic flag */
	tegra_rtcpu_trace_set_panic_flag(tracer);
}
EXPORT_SYMBOL(rtcpu_trace_panic_callback);

/**
 * @brief Initialize the HSP for the camera RTCPU
 *
 * This function initializes the HSP for the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Checks if the HSP is already initialized
 * - If the HSP is already initialized, returns 0
 * - Calls @ref camrtc_hsp_create() to create the HSP
 * - If the HSP creation fails, returns the error code
 * - Sets the HSP to the driver data
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 *
 * @retval (int) Return value from @ref camrtc_hsp_create()
 * @retval 0 On successful HSP initialization or if the HSP is already initialized
 */
static int tegra_camrtc_hsp_init(struct device *dev)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);
	int err;

	if (!IS_ERR_OR_NULL(rtcpu->hsp))
		return 0;

	rtcpu->hsp = camrtc_hsp_create(dev, tegra_camrtc_ivc_notify,
			rtcpu->cmd_timeout);
	if (IS_ERR(rtcpu->hsp)) {
		err = PTR_ERR(rtcpu->hsp);
		rtcpu->hsp = NULL;
		dev_err(dev, "%s: failed to create hsp, err=%d\n", __func__, err);
		return err;
	}

	/* Register panic callback to capture trace on RCE panic */
	if (rtcpu->hsp && rtcpu->tracer) {
		err = camrtc_hsp_set_panic_callback(rtcpu->hsp, rtcpu_trace_panic_callback);
		if (err < 0)
			dev_err(dev, "%s: failed to set panic callback, err=%d\n", __func__, err);
	} else {
		dev_err(dev, "%s: cannot register RCE panic callback.\n", __func__);
	}

	return 0;
}

/**
 * @brief Remove the camera RTCPU
 *
 * This function removes the camera RTCPU
 * - Retrieves the driver data for the device using @ref platform_get_drvdata()
 * - Checks if the HSP is already initialized
 * - If the HSP is initialized, calls @ref camrtc_hsp_bye() to bye the HSP
 * - Calls @ref camrtc_hsp_free() to free the HSP
 * - Sets the HSP to NULL
 * - Destroys the tracer using @ref tegra_rtcpu_trace_destroy()
 * - Sets the tracer to NULL
 * - Powers off the device using @ref tegra_camrtc_poweroff()
 * - Sets the ICC path to NULL
 * - Removes the device from the PM genpd using @ref pm_genpd_remove_device()
 * - Destroys the monitor using @ref tegra_cam_rtcpu_mon_destroy()
 * - Destroys the IVC bus using @ref tegra_ivc_bus_destroy()
 * - Sets the DMA parameters to NULL
 * - Deinitializes the operating point sysfs using @ref deinit_operating_point_sysfs()
 *
 * @param[in] pdev   Pointer to the platform device
 *                  Valid value: non-NULL
 *
 * @retval 0 On successful removal
 */
static int tegra_cam_rtcpu_remove(struct platform_device *pdev)
{
	struct tegra_cam_rtcpu *rtcpu = platform_get_drvdata(pdev);
	bool online = rtcpu->online;
	bool pm_is_active = pm_runtime_active(&pdev->dev);

	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);

	tegra_camrtc_set_online(&pdev->dev, false);

	if (rtcpu->hsp) {
		if (pm_is_active)
			tegra_cam_rtcpu_runtime_suspend(&pdev->dev);
		if (online)
			camrtc_hsp_bye(rtcpu->hsp);
		camrtc_hsp_free(rtcpu->hsp);
		rtcpu->hsp = NULL;
	}

	tegra_rtcpu_trace_destroy(rtcpu->tracer);
	rtcpu->tracer = NULL;

	tegra_camrtc_poweroff(&pdev->dev);
	rtcpu->icc_path = NULL;
	pm_genpd_remove_device(&pdev->dev);
	tegra_cam_rtcpu_mon_destroy(rtcpu->monitor);
	tegra_ivc_bus_destroy(rtcpu->ivc);

	pdev->dev.dma_parms = NULL;

	deinit_operating_point_sysfs();

	return 0;
}

/**
 * @brief Probe the camera RTCPU
 *
 * This function probes the camera RTCPU
 * - Retrieves the driver data for the device using @ref of_device_get_match_data()
 * - Reads the device name from the device tree using @ref of_property_read_string()
 * - Reads the device properties using @ref of_property_read_u32()
 * - Allocates memory for the camera RTCPU using @ref devm_kzalloc()
 * - Sets the driver data for the device using @ref platform_set_drvdata()
 * - Sets the DMA parameters for the device using @ref dma_set_mask_and_coherent()
 * - Enables runtime power management for the device using @ref pm_runtime_enable()
 * - Retrieves the resources for the device using @ref tegra_camrtc_get_resources()
 * - Sets the reboot retry count using @ref of_property_read_u32()
 * - Reads the command timeout using @ref of_property_read_u32()
 * - Reads the autosuspend delay using @ref of_property_read_u32()
 * - Sets the autosuspend delay using @ref pm_runtime_set_autosuspend_delay()
 * - Initializes the memory bandwidth for the device using @ref tegra_camrtc_init_membw()
 * - Sets the DMA parameters for the device using @ref dev->dma_parms
 * - Sets the tracer for the device using @ref tegra_rtcpu_trace_create()
 * - Initializes the HSP for the device using @ref tegra_camrtc_hsp_init()
 * - Powers on the device using @ref pm_runtime_get_sync()
 * - Creates the IVC bus for the device using @ref tegra_ivc_bus_create()
 * - Creates the monitor for the device using @ref tegra_camrtc_mon_create()
 * - Reads the firmware hash using @ref camrtc_hsp_get_fw_hash()
 * - Logs the firmware version using @ref tegra_camrtc_log_fw_version()
 * - Sets the online flag to true using @ref tegra_camrtc_set_online()
 * - Puts the device using @ref pm_runtime_put()
 * - Sets the device to the global variable s_dev
 * - Initializes the operating point sysfs using @ref init_operating_point_sysfs()
 * - In case of failure, call @ref pm_runtime_dont_use_autosuspend() and
 *   @ref pm_runtime_put_sync_suspend()
 * - In case of failure, call @ref tegra_cam_rtcpu_remove()
 *
 * @param[in] pdev   Pointer to the platform device
 *                  Valid value: non-NULL
 *
 * @retval (int) Return value from @ref tegra_camrtc_get_resources()
 * or @ref tegra_camrtc_hsp_init() or @ref pm_runtime_get_sync() or @ref camrtc_hsp_get_fw_hash()
 * @retval -ENOMEM On memory allocation failure
 * @retval -ENODEV On device match failure
 * @retval 0 On successful probe
 */
static int tegra_cam_rtcpu_probe(struct platform_device *pdev)
{
	struct tegra_cam_rtcpu *rtcpu;
	const struct tegra_cam_rtcpu_pdata *pdata;
	struct device *dev = &pdev->dev;
	int ret;
	const char *name;
	uint32_t timeout;

	pdata = of_device_get_match_data(dev);
	if (pdata == NULL) {
		dev_err(dev, "no device match\n");
		return -ENODEV;
	}

	name = pdata->name;
	ret = of_property_read_string(dev->of_node, "nvidia,cpu-name", &name);
	if (ret)
		dev_dbg(dev, "no device property, cpu-name, setting to parent name\n");

	dev_dbg(dev, "probing RTCPU on %s\n", name);

	rtcpu = devm_kzalloc(dev, sizeof(*rtcpu), GFP_KERNEL);
	if (rtcpu == NULL)
		return -ENOMEM;

	rtcpu->pdata = pdata;
	rtcpu->name = name;
	platform_set_drvdata(pdev, rtcpu);

	(void) dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));

	/* Enable runtime power management */
	pm_runtime_enable(dev);

	ret = tegra_camrtc_get_resources(dev);
	if (ret)
		goto fail;

	atomic_set(&rtcpu->rebooting, 0);
	rtcpu->max_reboot_retry = 3;
	ret = of_property_read_u32(dev->of_node, NV(max-reboot),
			&rtcpu->max_reboot_retry);
	if (ret)
		dev_dbg(dev, "no device property, max-reboot, setting to default (3)\n");
	timeout = 2000;

	ret = of_property_read_u32(dev->of_node, "nvidia,cmd-timeout", &timeout);
	if (ret)
		dev_dbg(dev, "no device property, cmd-timeout, setting to default (2000)\n");

	rtcpu->cmd_timeout = msecs_to_jiffies(timeout);

	timeout = 60000;
	ret = of_property_read_u32(dev->of_node, NV(autosuspend-delay-ms), &timeout);
	if (ret == 0) {
		pm_runtime_use_autosuspend(dev);
		pm_runtime_set_autosuspend_delay(&pdev->dev, timeout);
	}

	tegra_camrtc_init_membw(dev);

	dev->dma_parms = &rtcpu->dma_parms;
	dma_set_max_seg_size(dev, UINT_MAX);

	rtcpu->tracer = tegra_rtcpu_trace_create(dev, rtcpu->camera_devices);

	ret = tegra_camrtc_hsp_init(dev);
	if (ret)
		goto fail;

	/* Power on device */
	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		goto fail;

	rtcpu->ivc = tegra_ivc_bus_create(dev, rtcpu->hsp);
	if (IS_ERR(rtcpu->ivc)) {
		ret = PTR_ERR(rtcpu->ivc);
		rtcpu->ivc = NULL;
		goto put_and_fail;
	}

	rtcpu->monitor = tegra_camrtc_mon_create(dev);
	if (IS_ERR(rtcpu->monitor)) {
		ret = PTR_ERR(rtcpu->monitor);
		goto put_and_fail;
	}

	if (of_property_read_bool(dev->of_node, "nvidia,disable-runtime-pm"))
		pm_runtime_get(dev);

	ret = camrtc_hsp_get_fw_hash(rtcpu->hsp,
			rtcpu->fw_hash, sizeof(rtcpu->fw_hash));
	if (ret)
		dev_err(dev, "failed to get firmware hash!\n");
	else
		tegra_camrtc_log_fw_version(dev);

	tegra_camrtc_set_online(dev, true);

	pm_runtime_put(dev);

	s_dev = dev;

	init_operating_point_sysfs();

	dev_dbg(dev, "successfully probed RTCPU on %s\n", name);

	return 0;

put_and_fail:
	pm_runtime_dont_use_autosuspend(dev);
	pm_runtime_put_sync_suspend(dev);
fail:
	tegra_cam_rtcpu_remove(pdev);
	return ret;
}

/**
 * @brief Reboot the camera RTCPU
 *
 * This function reboots the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Checks if the device is suspended using @ref pm_runtime_suspended()
 * - Checks if the RTCPU is powered using @ref rtcpu->powered
 * - Sets the rebooting flag to 1 using @ref atomic_cmpxchg()
 * - Calls @ref tegra_camrtc_pm_start() to start the PM operation
 * - Calls @ref tegra_camrtc_set_online() to set the online flag to false
 * - Calls @ref tegra_camrtc_wait_for_idle() to wait for the RTCPU to enter WFI
 * - Calls @ref tegra_camrtc_assert_resets() to assert the resets
 * - Sets the powered flag to false using @ref rtcpu->powered
 * - Calls @ref tegra_camrtc_boot() to boot the RTCPU
 * - Sets the rebooting flag to 0 using @ref atomic_set()
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 *
 * @retval (int) Return value from @ref tegra_camrtc_boot()
 * @retval -EIO On device suspended or RTCPU not powered
 * @retval -EBUSY On rebooting flag already set
 */
int tegra_camrtc_reboot(struct device *dev)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);
	int err = 0;
	int ret;

	if (pm_runtime_suspended(dev)) {
		dev_info(dev, "cannot reboot while suspended\n");
		return -EIO;
	}

	if (!rtcpu->powered)
		return -EIO;

	if (atomic_cmpxchg(&rtcpu->rebooting, 0, 1) == 1) {
		dev_info(dev, "reboot already in progress\n");
		return -EBUSY;
	}

	rtcpu->boot_sync_done = false;
	rtcpu->fw_active = false;

	pm_runtime_mark_last_busy(dev);

	tegra_camrtc_set_online(dev, false);

	/* Signal CAMRTC to suspend its operations.
	 * Incompleted memory operations by CAMRTC can cause a memory fabric
	 * error if the reset signal is asserted in middle of shared memory
	 * transaction.
	 */
	if (rtcpu->hsp)
		err = camrtc_hsp_bye(rtcpu->hsp);

	/* Wait for the core to enter WFI before asserting the reset.
	 * Don't bother if the core was unresponsive.
	 */
	if (err == 0)
		tegra_camrtc_wait_for_idle(dev);

	tegra_camrtc_assert_resets(dev);

	rtcpu->powered = false;

	ret = tegra_camrtc_boot(dev);

	atomic_set(&rtcpu->rebooting, 0);

	return ret;
}
EXPORT_SYMBOL(tegra_camrtc_reboot);

/**
 * @brief Restore the camera RTCPU
 *
 * This function restores the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Checks if the monitor is not NULL using @ref rtcpu->monitor
 * - Calls @ref tegra_camrtc_mon_restore_rtcpu() to restore the RTCPU
 * - Returns the return value from @ref tegra_camrtc_mon_restore_rtcpu()
 * - If the monitor is NULL, calls @ref tegra_camrtc_reboot() to reboot the RTCPU
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 *
 * @retval (int) Return value from @ref tegra_camrtc_mon_restore_rtcpu() or
 *   @ref tegra_camrtc_reboot()
 */
int tegra_camrtc_restore(struct device *dev)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);

	if (rtcpu->monitor)
		return tegra_camrtc_mon_restore_rtcpu(rtcpu->monitor);
	else
		return tegra_camrtc_reboot(dev);
}
EXPORT_SYMBOL(tegra_camrtc_restore);

/**
 * @brief Check if the camera RTCPU is alive
 *
 * This function checks if the camera RTCPU is alive
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Returns the online flag from @ref rtcpu->online
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 *
 * @retval (bool) Value of @ref rtcpu->online
 */
bool tegra_camrtc_is_rtcpu_alive(struct device *dev)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);

	return rtcpu->online;
}
EXPORT_SYMBOL(tegra_camrtc_is_rtcpu_alive);

/**
 * @brief Check if the camera RTCPU is powered
 *
 * This function checks if the camera RTCPU is powered
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Returns the powered flag from @ref rtcpu->powered
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 *
 * @retval (bool) Value of @ref rtcpu->powered
 * @retval false if the device is not found
 */
bool tegra_camrtc_is_rtcpu_powered(void)
{
	struct tegra_cam_rtcpu *rtcpu;

	if (s_dev) {
		rtcpu = dev_get_drvdata(s_dev);
		return rtcpu->powered;
	}

	return false;
}
EXPORT_SYMBOL(tegra_camrtc_is_rtcpu_powered);

/**
 * @brief Flush the trace for the camera RTCPU
 *
 * This function flushes the trace for the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Calls @ref tegra_rtcpu_trace_flush() to flush the trace
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 */
void tegra_camrtc_flush_trace(struct device *dev)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);

	tegra_rtcpu_trace_flush(rtcpu->tracer);
}
EXPORT_SYMBOL(tegra_camrtc_flush_trace);

/**
 * @brief Halt the camera RTCPU
 *
 * This function halts the camera RTCPU
 * - Retrieves the driver data for the device using @ref dev_get_drvdata()
 * - Checks if the online flag is true using @ref rtcpu->online
 * - Calls @ref tegra_camrtc_pm_start() to start the PM operation
 * - Calls @ref tegra_camrtc_set_online() to set the online flag to false
 * - Checks if the powered flag is false using @ref rtcpu->powered
 * - Calls @ref tegra_camrtc_pm_done() to finish the PM operation
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 * @param[in] op   Operation to perform
 *                  Valid value: non-NULL
 *
 * @retval 0 if the RTCPU is not powered or the operation is successful
 */
static int tegra_camrtc_halt(struct device *dev, char const *op)
{
	struct tegra_cam_rtcpu *rtcpu = dev_get_drvdata(dev);
	bool online = rtcpu->online;
	int err = 0;

	tegra_camrtc_pm_start(dev, op);

	tegra_camrtc_set_online(dev, false);

	if (!rtcpu->powered) {
		tegra_camrtc_pm_done(dev, op, 0);
		return 0;
	}

	if (!pm_runtime_suspended(dev))
		/* Tell CAMRTC that it should power down camera devices */
		err = tegra_camrtc_fw_suspend(dev);

	if (online && rtcpu->hsp && err == 0)
		/* Tell CAMRTC that shared memory is going away */
		err = camrtc_hsp_bye(rtcpu->hsp);

	if (err == 0)
		/* Don't bother to check for WFI if core is unresponsive */
		tegra_camrtc_wait_for_idle(dev);

	tegra_camrtc_poweroff(dev);

	tegra_camrtc_pm_done(dev, op, err); /* note this is not returned */

	return 0;
}

/**
 * @brief Suspend the camera RTCPU
 *
 * This function suspends the camera RTCPU
 * - Calls @ref tegra_camrtc_halt() to halt the RTCPU
 * - Returns the return value from @ref tegra_camrtc_halt()
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 *
 * @retval (int) Return value from @ref tegra_camrtc_halt()
 */
static int tegra_camrtc_suspend(struct device *dev)
{
	return tegra_camrtc_halt(dev, "suspend");
}

/**
 * @brief Resume the camera RTCPU
 *
 * This function resumes the camera RTCPU
 * - Calls @ref tegra_camrtc_pm_start() to start the PM operation
 * - Calls @ref pm_runtime_mark_last_busy() to mark the device as busy
 * - Calls @ref pm_runtime_resume() to resume the device
 * - Calls @ref tegra_camrtc_pm_done() to finish the PM operation
 *
 * @param[in] dev   Pointer to the device
 *                  Valid value: non-NULL
 *
 * @retval (int) Return value from @ref pm_runtime_resume() or
 * @ref tegra_camrtc_boot()
 */
static int tegra_camrtc_resume(struct device *dev)
{
	int err;

	tegra_camrtc_pm_start(dev, "resume");

	pm_runtime_mark_last_busy(dev);

	/* Call tegra_cam_rtcpu_runtime_resume() - unless PM thinks dev is ACTIVE */
	err = pm_runtime_resume(dev);
	if (err == 1)
		/* Already marked ACTIVE, boot explicitly */
		err = tegra_camrtc_boot(dev);

	tegra_camrtc_pm_done(dev, "resume", err);

	return err;
}

/**
 * @brief Shutdown the camera RTCPU
 *
 * This function shuts down the camera RTCPU
 * - Calls @ref tegra_camrtc_halt() to halt the RTCPU
 * - Returns the return value from @ref tegra_camrtc_halt()
 *
 * @param[in] pdev   Pointer to the platform device
 *                  Valid value: non-NULL
 */
static void tegra_cam_rtcpu_shutdown(struct platform_device *pdev)
{
	tegra_camrtc_halt(&pdev->dev, "shutdown");
}

static const struct of_device_id tegra_cam_rtcpu_of_match[] = {
	{
		.compatible = NV(tegra194-rce), .data = &rce_pdata
	},
	{
		.compatible = NV(tegra264-rce), .data = &t264_rce_pdata
	},
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_cam_rtcpu_of_match);

static const struct dev_pm_ops tegra_cam_rtcpu_pm_ops = {
	.suspend = tegra_camrtc_suspend,
	.resume = tegra_camrtc_resume,
	.runtime_suspend = tegra_cam_rtcpu_runtime_suspend,
	.runtime_resume = tegra_cam_rtcpu_runtime_resume,
	.runtime_idle = tegra_cam_rtcpu_runtime_idle,
};

/**
 * @brief Remove the camera RTCPU
 *
 * This function removes the camera RTCPU
 * - Calls @ref tegra_cam_rtcpu_remove() to remove the RTCPU
 *
 * @param[in] pdev   Pointer to the platform device
 *                  Valid value: non-NULL
 */
#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void tegra_cam_rtcpu_remove_wrapper(struct platform_device *pdev)
{
	tegra_cam_rtcpu_remove(pdev);
}
#else
static int tegra_cam_rtcpu_remove_wrapper(struct platform_device *pdev)
{
	return tegra_cam_rtcpu_remove(pdev);
}
#endif

static struct platform_driver tegra_cam_rtcpu_driver = {
	.driver = {
		.name	= "tegra186-cam-rtcpu",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(tegra_cam_rtcpu_of_match),
#ifdef CONFIG_PM
		.pm = &tegra_cam_rtcpu_pm_ops,
#endif
	},
	.probe = tegra_cam_rtcpu_probe,
	.remove = tegra_cam_rtcpu_remove_wrapper,
	.shutdown = tegra_cam_rtcpu_shutdown,
};
module_platform_driver(tegra_cam_rtcpu_driver);

MODULE_DESCRIPTION("CAMERA RTCPU driver");
MODULE_AUTHOR("NVIDIA");
MODULE_LICENSE("GPL v2");
