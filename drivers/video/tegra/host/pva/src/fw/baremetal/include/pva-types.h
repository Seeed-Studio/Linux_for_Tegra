/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2016-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_TYPES_H
#define PVA_TYPES_H
#include <stdint.h>

/**
 * @brief Used to represent address (IOVA) in PVA system.
 */
typedef uint64_t pva_iova;

/**
 * @brief Used to store Queue IDs, that represent the
 *        actual hardware queue id between FW and KMD.
 */
typedef uint8_t pva_queue_id_t;

/**
 * @brief Used to store PVE ID, that represents which
 *        PVE is being referred to .
 */
typedef uint8_t pva_pve_id_t;

/**
 * @brief Used to store Status interface ID, that is used
 *        to know through which status needs to be written.
 */
typedef uint8_t pva_status_interface_id_t;

#endif
