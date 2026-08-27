// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

/**
 * @file teec-soc-plugin.c
 * @brief NVIDIA TZVault SOC Plugin Kernel Module
 *
 * @b Description: Standalone kernel module that provides SOC-specific transport interface.
 */

#include "teec-soc-plugin.h"
#include "teec-soc-ivc.h"
#include "teec-soc-mailbox.h"
#include <linux/module.h>
#include <linux/of.h>
#include <linux/printk.h>

#define NVTZVAULT_ERR(...) pr_err("nvtzvault: wrapper: " __VA_ARGS__)


/**
 * @brief Provide API version of the plugin interface
 *
 * @param [out] major_version Major version
 * @param [out] minor_version Minor version
 */
void teec_soc_get_interface_version(uint32_t *major_version, uint32_t *minor_version)
{
	if (major_version)
		*major_version = TEEC_SOC_PLUGIN_INTERFACE_MAJOR_VERSION;
	if (minor_version)
		*minor_version = TEEC_SOC_PLUGIN_INTERFACE_MINOR_VERSION;
}
EXPORT_SYMBOL_GPL(teec_soc_get_interface_version);

/**
 * @brief SOC Plugin Interface - Runtime Transport Selection
 *
 * @param tee_priv   TeeClient structure to initialize with plugin functions
 * @param comms_os_params   CommsParams structure with OS specific configuration parameters
 * @return           TEE_CLIENT_STATUS_OK on success, error code on failure
 *
 * @note CROSS-MODULE USAGE:
 *       This function is exported by teec-soc-plugin.ko module and called by
 *       nvtzvault.ko driver. The SOC plugin module must be loaded first, before the
 *       main driver can successfully initialize.
 *
 *       Selects transport based on device tree nodes:
 *           - oesp-mailbox node present -> Mailbox transport
 *
 * @note The transport selection is only required when this kernel module is intended to be scalable
 *       to support multiple transports (e.g., mailbox and IVC). If only one transport is supported,
 *       the transport selection can be omitted and this indirection to teec_initialize_interface
 *       implementation can be removed.
 *
 */
TeeClientStatus teec_initialize_interface(TeeClient *tee_priv, CommsParamsOS comms_os_params)
{
	struct device_node *transport_node;
	struct device_node *nvtzvault_node;
	const struct device_node *ta_node;

	if (!tee_priv) {
		NVTZVAULT_ERR("Invalid parameters\n");
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;
	}

	ta_node = comms_os_params.ta_node;
	if (!ta_node) {
		NVTZVAULT_ERR("Invalid device tree node\n");
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;
	}

	nvtzvault_node = ta_node->parent;
	if (!nvtzvault_node) {
		NVTZVAULT_ERR("TA node has no parent\n");
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;
	}

	transport_node = of_find_node_by_name(nvtzvault_node, "oesp-mailbox");
	if (transport_node) {
		of_node_put(transport_node);
		return teec_initialize_interface_mailbox(tee_priv, ta_node);
	}

	transport_node = of_find_node_by_name(nvtzvault_node, "teec-ivc");
	if (transport_node) {
		of_node_put(transport_node);
		return teec_initialize_interface_ivc(tee_priv, ta_node);
	}

	/* No supported transport found */
	NVTZVAULT_ERR("No transport nodes found in device tree\n");
	return TEE_CLIENT_STATUS_GENERIC_ERROR;
}
EXPORT_SYMBOL_GPL(teec_initialize_interface);

/**
 * @brief SOC Plugin module initialization
 */
static int __init teec_soc_plugin_init(void)
{
	pr_info("nvtzvault: SOC Plugin module loaded\n");
	return 0;
}

/**
 * @brief SOC Plugin module cleanup
 */
static void __exit teec_soc_plugin_exit(void)
{
	pr_info("nvtzvault: SOC Plugin module unloaded\n");
}

module_init(teec_soc_plugin_init);
module_exit(teec_soc_plugin_exit);

MODULE_DESCRIPTION("NVIDIA TZVault SOC Communication Interface");
MODULE_AUTHOR("Nvidia Corporation");
MODULE_LICENSE("GPL v2");
