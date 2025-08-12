/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
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

/**
 * @brief Reset assert FW so it can be in recovery and
 * user submission halted. This is requied for host1x
 * watchdog, or kmd submission timeout failures.
 */
void pva_kmd_freeze_fw(struct pva_kmd_device *pva);

/**
 * @brief Increase reference count on the PVA device.
 *
 * Power on PVA if necessary.
 */
enum pva_error pva_kmd_device_busy(struct pva_kmd_device *pva);

/**
 * @brief Decrease reference count on the PVA device.
 *
 * Power off PVA if necessary.
 */
void pva_kmd_device_idle(struct pva_kmd_device *pva);

/**
 * @brief Check if PVA is already powered on.
 *
 * This function provides a hint about the current power state of the PVA device.
 * A return value of true indicates the device is likely powered on, while false
 * indicates it is likely powered off.
 *
 * @note This is only a hint and the power state may change at any time. The caller
 * must still take a reference using pva_kmd_device_busy() before attempting any
 * communication with the device.
 */
bool pva_kmd_device_maybe_on(struct pva_kmd_device *pva);

/**
 * @brief Load firmware.
 *
 * This function loads firmware. On silicon, this includes
 * - power on R5,
 * - load firmware,
 * - bind interrupts,
 * - and wait for firmware boot to complete.
 *
 * @param pva pointer to the PVA device
 */
enum pva_error pva_kmd_load_fw(struct pva_kmd_device *pva);

/**
 * @brief Unload firmware.
 *
 * This function unloads firmware. On silicon, this includes
 * - free interrupts,
 * - power off R5,
 * - and free firmware memories.
 *
 * @param pva pointer to the PVA device
 */
void pva_kmd_unload_fw(struct pva_kmd_device *pva);

/**
 * @brief Disable all interrupts without waiting for running interrupt handlers
 * to complete.
 *
 * We don't wait for running interrupt handlers to complete because we want to
 * be able to call this function from interrupt handles themselves.
 *
 * This function is to be called when PVA enters bad state and we want to
 * protect KMD from potential interrupt floods from PVA (particularly watchdog
 * interrupt that will trigger repeatedly by HW).
 */
void pva_kmd_disable_all_interrupts_nosync(struct pva_kmd_device *pva);

/**
 * @brief Report error to FSI.
 *
 * This function reports an error to FSI.
 *
 * @param pva pointer to the PVA device
 */
void pva_kmd_report_error_fsi(struct pva_kmd_device *pva, uint32_t error_code);

#endif // PVA_KMD_SHIM_INIT_H
