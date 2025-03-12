/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_MSG_H
#define PVA_KMD_MSG_H

#include "pva_api.h"

/**
 * @brief Handle messages from FW to hypervisor.
 *
 * This is just a provision for future hypervisor support. For now, this just
 * handles all messages from mailboxes.
 */
void pva_kmd_handle_hyp_msg(void *pva_dev, uint32_t const *data, uint8_t len);

/**
 * @brief Handle messages from FW to KMD.
 *
 * These messages come from CCQ0 statues registers.
 */
void pva_kmd_handle_msg(void *pva_dev, uint32_t const *data, uint8_t len);

// TODO: move to a better location OR consolidate handling of all message types here
enum pva_error pva_kmd_handle_msg_resource_unreg(void *context,
						 uint8_t interface,
						 uint8_t *element);
#endif // PVA_KMD_MSG_H
