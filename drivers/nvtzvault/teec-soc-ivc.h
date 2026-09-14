/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef TEEC_SOC_IVC_H
#define TEEC_SOC_IVC_H

#include "teec-soc-plugin.h"

/**
 * @file teec-soc-ivc.h
 * @brief Internal header for ivc transport implementation
 *
 * This header contains internal declarations for the ivc transport
 * implementation. It should NOT be included by external modules.
 */

/**
 * @brief Initialize ivc-based communication interface
 * @param tee_priv Pointer to TeeClient structure to initialize
 * @param dt_node Device tree node containing ivc configuration
 * @return TeeClientStatus status code
 *
 * INTERNAL FUNCTION - Used by teec-soc-plugin.c to initialize
 * ivc transport when teec-ivc node is found in device tree.
 */
TeeClientStatus teec_initialize_interface_ivc(TeeClient *tee_priv, const void *dt_node);

#endif /* TEEC_SOC_IVC_H */
