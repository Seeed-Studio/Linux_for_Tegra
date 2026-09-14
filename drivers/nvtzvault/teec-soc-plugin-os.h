/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef TEEC_SOC_PLUGIN_OS_H
#define TEEC_SOC_PLUGIN_OS_H

/**
 * @file teec-soc-plugin-os.h
 * @brief OS-specific communication interface for TEE (Trusted Execution Environment) client operations
 *
 * @b Description: Linux kernel-specific interface extensions for TEE transport abstraction.
 */

/**
 * @defgroup teec_soc_plugin_os_linux TEEC_SOC_PLUGIN_OS::Linux Kernel Interface
 *
 * OS-specific extensions to the TEE Client SOC Plugin interface for Linux kernel environment.
 * This interface provides Linux kernel-specific configuration and initialization parameters
 * that complement the transport-agnostic interface defined in teec-soc-plugin.h.
 *
 * The OS-specific interface enables OS-specific configuration parameter passing.
 *
 * @{
 */

/**
 * @brief OS-specific communication parameters for Linux kernel
 *
 * This structure contains Linux kernel-specific parameters required for
 * transport initialization and configuration. It provides the necessary
 * OS-specific context while maintaining transport independence.
 *
 * @note kernel_checkpatch: typedef required to conform to plugin interface
 */
typedef struct {
	/* Device tree node containing TA-specific configuration properties */
	const struct device_node *ta_node;
} CommsParamsOS;

/** @} */

#endif /* TEEC_SOC_PLUGIN_OS_H */
