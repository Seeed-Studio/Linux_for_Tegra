/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_SILICON_HWPM_H
#define PVA_KMD_SILICON_HWPM_H
#include "pva_kmd.h"
#include "pva_kmd_shim_debugfs.h"

#if PVA_ENABLE_HWPM == 1

/**
 * @brief PVA hardware performance monitoring power management control
 *
 * @details This function is called from the Tegra HWPM (Hardware Performance
 * Monitoring) driver to control the power state of the PVA device for
 * performance monitoring operations. It manages the power-on/power-off
 * sequence of the PVA device to enable or disable hardware performance
 * monitoring capabilities. When disable is false, the PVA device is powered
 * on to allow HWPM register access and monitoring operations.
 *
 * @param[in] ip_dev  Pointer to PVA device instance
 *                    Valid value: typically @ref pva_kmd_device pointer
 * @param[in] disable Power management control flag
 *                    Valid values:
 *                    - true: Disable power management (power off PVA)
 *                    - false: Enable power management (power on PVA)
 *
 * @retval 0           Power management operation completed successfully
 * @retval -EINVAL     Invalid device pointer or parameter
 * @retval -ENODEV     Device not available or not initialized
 * @retval -ETIMEDOUT  Timeout during power state transition
 * @retval -EIO        Hardware error during power management operation
 */
int pva_kmd_hwpm_ip_pm(void *ip_dev, bool disable);

/**
 * @brief PVA hardware performance monitoring register access operation
 *
 * @details This function is called from the Tegra HWPM (Hardware Performance
 * Monitoring) driver to perform read or write operations on PVA HWPM registers.
 * It provides controlled access to performance monitoring registers within the
 * PVA device, supporting both read and write operations. The function validates
 * the register offset, performs the requested operation, and manages data
 * transfer to/from the specified register location.
 *
 * @param[in]     ip_dev              Pointer to PVA device instance
 *                                    Valid value: typically @ref pva_kmd_device pointer
 * @param[in]     reg_op              Register operation type
 *                                    Valid values:
 *                                    - TEGRA_SOC_HWPM_IP_REG_OP_READ: Read operation
 *                                    - TEGRA_SOC_HWPM_IP_REG_OP_WRITE: Write operation
 * @param[in]     inst_element_index  Element index within PVA instance
 *                                    Valid range: [0 .. PVA_MAX_ELEMENTS-1]
 * @param[in]     reg_offset          Register offset relative to PVA HWPM base address
 *                                    Valid range: [0 .. PVA_HWPM_REGISTER_SPACE_SIZE-4]
 *                                    Must be 4-byte aligned
 * @param[in,out] reg_data            Pointer to register data
 *                                    For read: buffer to store read data (output)
 *                                    For write: buffer containing data to write (input)
 *                                    Valid value: non-null, must point to valid 32-bit storage
 *
 * @retval 0           Register operation completed successfully
 * @retval -EINVAL     Invalid device pointer, operation type, or register offset
 * @retval -ENODEV     Device not available or not initialized
 * @retval -EACCES     Access denied to specified register
 * @retval -ETIMEDOUT  Timeout during register access
 * @retval -EIO        Hardware error during register operation
 */
int pva_kmd_hwpm_ip_reg_op(void *ip_dev, uint32_t reg_op,
			   uint32_t inst_element_index, uint64_t reg_offset,
			   uint32_t *reg_data);

#else /* PVA_ENABLE_HWPM */

/* Dummy inline functions when HWPM is disabled */
static inline int pva_kmd_hwpm_ip_pm(void *ip_dev, bool disable)
{
	(void)ip_dev;
	(void)disable;
	return 0;
}

static inline int pva_kmd_hwpm_ip_reg_op(void *ip_dev, uint32_t reg_op,
					 uint32_t inst_element_index,
					 uint64_t reg_offset,
					 uint32_t *reg_data)
{
	(void)ip_dev;
	(void)reg_op;
	(void)inst_element_index;
	(void)reg_offset;
	(void)reg_data;
	return -1; /* Return error to indicate HWPM not supported */
}

#endif /* PVA_ENABLE_HWPM */
#endif //PVA_KMD_SILICON_HWPM_H