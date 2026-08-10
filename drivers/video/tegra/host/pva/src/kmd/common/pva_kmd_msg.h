/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_MSG_H
#define PVA_KMD_MSG_H

#include "pva_api.h"

/**
 * @brief Handle messages from FW to hypervisor.
 *
 * @details This function performs the following operations:
 * - Receives message data from firmware intended for hypervisor processing
 * - Validates the message format and length for correctness
 * - Routes the message to appropriate hypervisor handling mechanisms
 * - Processes mailbox-based communication from firmware
 * - Handles future hypervisor support infrastructure
 * - Ensures proper message acknowledgment back to firmware
 *
 * This is just a provision for future hypervisor support. For now, this just
 * handles all messages from mailboxes. The function provides a foundation
 * for hypervisor communication that can be extended when full hypervisor
 * support is implemented in the system.
 *
 * @param[in] pva_dev  Pointer to PVA device structure
 *                     Valid value: non-null
 * @param[in] data     Pointer to message data array received from firmware
 *                     Valid value: non-null
 * @param[in] len      Length of the message data in 32-bit words
 *                     Valid range: [1 .. 255]
 */
void pva_kmd_handle_hyp_msg(void *pva_dev, uint32_t const *data, uint8_t len);

#endif // PVA_KMD_MSG_H
