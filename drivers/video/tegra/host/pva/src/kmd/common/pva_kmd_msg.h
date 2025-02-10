/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, NVIDIA Corporation.  All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation.  Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */
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
