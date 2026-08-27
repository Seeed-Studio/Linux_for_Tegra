/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

/**
 * @file dce-psc-client.h
 * @brief DCE to PSC mailbox communication client header
 */

#ifndef DCE_PSC_CLIENT_H
#define DCE_PSC_CLIENT_H

#include <linux/types.h>
#include <linux/platform_device.h>

/**
 * dce_psc_client_deinit - Deinitialize DCE PSC client
 *
 * @pdev: Platform device pointer
 *
 * Cleans up resources allocated by dce_psc_client_init.
 */
void dce_psc_client_deinit(struct platform_device *pdev);

/**
 * dce_psc_load_fw - Load the dce fw through psc
 *
 * @pdev: Platform device pointer
 * @d: tegra_dce pointer
 *
 * Check the dce_fw_load_by_psc variable to decide
 * to load the DCE FW through PSC
 * Initialize the dce as psc client to setup mailbox
 * It must be called before any communication with
 * PSC is attempted.
 * Allocate the carveout and copy the DCE FW
 * Send mailbox message to PSC to load the DCE FW
 *
 * Returns: 0 on success, negative error code on failure
 */
int dce_psc_load_fw(struct platform_device *pdev,
		struct tegra_dce *d);

/**
 * dce_psc_resume_fw - Resume dce fw after sc7
 *
 * @dev: device pointer
 * @d: tegra_dce pointer
 *
 * Resume the dc_fw after sc7 through
 * PSC mailbox registers
 *
 * Returns: 0 on success, negative error code on failure
 */
int dce_psc_resume_fw(struct device *dev,
		struct tegra_dce *d);

#endif /* DCE_PSC_CLIENT_H */

