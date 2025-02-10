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
#ifndef PVA_KMD_SHIM_INIT_H
#define PVA_KMD_SHIM_INIT_H
#include "pva_api.h"
struct pva_kmd_device;
struct pva_kmd_file_ops;

/* TODO: remove plat_init APIs. We should just pass in plat_data directly to
 * pva_kmd_device_create. */
void pva_kmd_device_plat_init(struct pva_kmd_device *pva);
void pva_kmd_device_plat_deinit(struct pva_kmd_device *pva);

void pva_kmd_read_syncpt_val(struct pva_kmd_device *pva, uint32_t syncpt_id,
			     uint32_t *syncpt_value);

void pva_kmd_get_syncpt_iova(struct pva_kmd_device *pva, uint32_t syncpt_id,
			     uint64_t *syncpt_iova);

void pva_kmd_allocate_syncpts(struct pva_kmd_device *pva);

/**
 * @brief Power on PVA cluster.
 */
enum pva_error pva_kmd_power_on(struct pva_kmd_device *pva);

/**
 * @brief Power off PVA cluster.
 */
void pva_kmd_power_off(struct pva_kmd_device *pva);

/**
 * @brief Initialize firmware.
 *
 * This function initializes firmware. On silicon, this includes
 * - power on R5,
 * - load firmware,
 * - bind interrupts,
 * - and wait for firmware boot to complete.
 *
 * @param pva pointer to the PVA device to initialize
 */
enum pva_error pva_kmd_init_fw(struct pva_kmd_device *pva);

/**
 * @brief De-init firmware.
 *
 * This function de-initializes firmware. On silicon, this includes
 * - free interrupts,
 * - power off R5,
 * - and free firmware memories.
 *
 * @param pva pointer to the PVA device to de-initialize
 */
void pva_kmd_deinit_fw(struct pva_kmd_device *pva);
#endif // PVA_KMD_SHIM_INIT_H
