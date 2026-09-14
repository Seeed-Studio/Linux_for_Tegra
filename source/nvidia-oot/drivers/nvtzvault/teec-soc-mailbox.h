/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef TEEC_SOC_MAILBOX_H
#define TEEC_SOC_MAILBOX_H

#include "teec-soc-plugin.h"

/**
 * @file teec-soc-mailbox.h
 * @brief Internal header for mailbox transport implementation
 *
 * This header contains internal declarations for the mailbox transport
 * implementation. It should NOT be included by external modules.
 */

/**
 * @brief Initialize mailbox-based communication interface
 * @param tee_priv Pointer to TeeClient structure to initialize
 * @param dt_node Device tree node containing mailbox configuration
 * @return TeeClientStatus status code
 *
 * INTERNAL FUNCTION - Used by teec-soc-plugin.c to initialize
 * mailbox transport when oesp-mailbox node is found in device tree.
 */
TeeClientStatus teec_initialize_interface_mailbox(TeeClient *tee_priv, const void *dt_node);

#endif /* TEEC_SOC_MAILBOX_H */
