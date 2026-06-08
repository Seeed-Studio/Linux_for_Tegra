/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVDLA device interface
 */

#ifndef __NVDLA_DEVICE_H__
#define __NVDLA_DEVICE_H__

#include <linux/platform_device.h>

/*
 * Following Data are extracted from the platform device - nvhost_device_data.
 *  pdata->class
 *  pdata->clk_cap_attrs
 *  pdata->clk_cap_kobj
 *  pdata->clks
 *  pdata->clocks
 *  pdata->debugfs
 *  pdata->flcn_isr
 *  pdata->isolate_contexts
 *  pdata->lock
 *  pdata->num_clks
 *  pdata->pdev
 *  pdata->private_data
 *  pdata->version
 */

/**
 * @brief Reads a 32-bit register.
 *
 * @param[in] pdev Platform device.
 * @param[in] reg Register to be read.
 *
 * @return
 * - value of the register read.
 **/
uint32_t nvdla_device_register_read(struct platform_device *pdev,
	uint32_t reg);

/**
 * @brief Writes a 32-bit register.
 *
 * @param[in] pdev Platform device.
 * @param[in] reg Register to be written.
 * @param[in] value 32-bit value to be written.
 **/
void nvdla_device_register_write(struct platform_device *pdev,
	uint32_t reg,
	uint32_t value);

/**
 * @brief Initializes dla device
 *
 * - From the DT, iomap the resources and save in aperture.
 * - From the DT, set the clock rates, resets.
 * - pm_enable
 * - Prepare the device nodes.
 *
 * @param[in] pdev Platform device.
 *
 * @return
 * - zero upon successful operation.
 * - non-zero otherwise.
 **/
int32_t nvdla_module_init(struct platform_device *pdev);

/**
 * @brief Deinitializes dla device
 *
 * - release the device nodes.
 * - pm_disable and destroy the device nodes
 *
 * @param[in] pdev Platform device.
 **/
void nvdla_module_deinit(struct platform_device *pdev);

/**
 * @brief Registers new client.
 *
 * The functionality is triggered for every device open.
 *
 * @param[in] pdev Platform device
 * @param[in] context Context data corresponding to the client.
 *
 * @return
 * - zero upon successful operation.
 * - non-zero otherwise
 **/
int32_t nvdla_module_client_register(struct platform_device *pdev,
	void *context);

/**
 * @brief Unregisters new client.
 *
 * The functionality is triggered for every device open.
 *
 * @param[in] pdev Platform device
 * @param[in] context Context data corresponding to the client.
 **/
void nvdla_module_client_unregister(struct platform_device *pdev,
	void *context);

/**
 * @brief Increase the reference count from power on.
 *
 * @param[in] pdev Platform device.
 *
 * @return
 * - zero upon successful completion.
 * - non-zero otherwise.
 **/
int32_t nvdla_module_busy(struct platform_device *pdev);

/**
 * @brief Decrease the reference count towards power off.
 *
 * @param[in] pdev Platform device.
 **/
void nvdla_module_idle(struct platform_device *pdev);

/**
 * @brief Decreases the reference count towards power off.
 *
 * @param[in] pdev Platform device.
 * @param[in] refs number of ref counts to decrement towards power off.
 **/
void nvdla_module_idle_mult(struct platform_device *pdev, int32_t refs);

/**
 * @brief Resets the module.
 *
 * @param[in] pdev Platform device.
 * @param[in] reboot Flag if set, reboots on reset.
 **/
void nvdla_module_reset(struct platform_device *pdev, bool reboot);

/**
 * @brief PM runtime suspend operation callback
 *
 * @param[in] dev Device subjected to operation.
 *
 * @return
 * - zero upon successful completion.
 * - non-zero otherwise
 **/
int32_t nvdla_module_runtime_suspend(struct device *dev);

/**
 * @brief PM runtime resume operation callback
 *
 * @param[in] dev Device subjected to operation.
 *
 * @return
 * - zero upon successful completion.
 * - non-zero otherwise
 **/
int32_t nvdla_module_runtime_resume(struct device *dev);

/**
 * @brief PM suspend operation callback
 *
 * @param[in] dev Device subjected to operation.
 *
 * @return
 * - zero upon successful completion.
 * - non-zero otherwise
 **/
int32_t nvdla_module_suspend(struct device *dev);

/**
 * @brief PM resume operation callback
 *
 * @param[in] dev Device subjected to operation.
 *
 * @return
 * - zero upon successful completion.
 * - non-zero otherwise
 **/
int32_t nvdla_module_resume(struct device *dev);

/**
 * @brief PM prepare suspend operation callback
 *
 * @param[in] dev Device subjected to operation.
 *
 * @return
 * - zero upon successful completion.
 * - non-zero otherwise
 **/
int32_t nvdla_module_prepare_suspend(struct device *dev);

/**
 * @brief PM complete resume operation callback
 *
 * @param[in] dev Device subjected to operation.
 **/
void nvdla_module_complete_resume(struct device *dev);

/**
 * @brief Registers the platform driver.
 *
 * @param[in] pdriver Platform driver that is to be registered.
 *
 * @return
 * - zero upon successful completion.
 * - non-zero otherwise
 **/
int32_t nvdla_driver_register(struct platform_driver *pdriver);

/**
 * @brief Unregisters the platform driver.
 *
 * @param[in] pdriver Platform driver that is to be unregistered.
 **/
void nvdla_driver_unregister(struct platform_driver *pdriver);

#endif /*__NVDLA_DEVICE_H__ */
