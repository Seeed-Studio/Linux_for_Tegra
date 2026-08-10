/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 */

#ifndef _SOC_PLUGIN_PRIV_H_
#define _SOC_PLUGIN_PRIV_H_

#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include "soc_plugin_os_hal.h"
#include "soc_plugin_hal.h"

Soc_Plugin_Hal_Status soc_plugin_hal_init_soc(const void *nvdt_node);

int nvvse_soc_plugin_probe(struct platform_device *pdev);
int nvvse_soc_plugin_remove(struct platform_device *pdev);
void nvvse_soc_plugin_shutdown(struct platform_device *pdev);

#endif /* _SOC_PLUGIN_PRIV_H_ */
