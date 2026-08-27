// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.*/

/**
 * @file  tegra-hsierrrptinj.h
 * @brief <b> HSI Error Report Injection driver header file</b>
 *
 * This file will expose API prototypes for HSI Error Report Injection kernel
 * space APIs.
 */

#ifndef TEGRA_HSIERRRPTINJ_H
#define TEGRA_HSIERRRPTINJ_H

/* ==================[Includes]============================================= */

#include "tegra-epl.h"

/* ==================[Type Definitions]===================================== */
/* Number of registered IPs */
#define NUM_IPS 13U

/**
 * @brief IP IDs
 *
 * Note: Any update to this enum must be reflected in the macro NUM_IPS.
 */
typedef enum {
IP_OTHER = 0x0000,
IP_GPU   = 0x0001,
IP_EQOS  = 0x0002,
IP_MGBE  = 0x0003,
IP_PCIE  = 0x0004,
IP_PSC   = 0x0005,
IP_I2C   = 0x0006,
IP_QSPI  = 0x0007,
IP_SDMMC = 0x0008,
IP_TSEC  = 0x0009,
IP_THERM = 0x000A,
IP_SMMU  = 0x000B,
IP_DLA   = 0x000C,

IP_EC    = 0x00FC,
IP_SC7   = 0x00FD,
IP_FSI   = 0x00FE,
IP_HSM   = 0x00FF
} hsierrrpt_ipid_t;


/**
 * @brief Callback signature for initiating HSI error reports to FSI
 *
 * @param[in]   instance_id             Instance of supported IP.
 * @param[in]   err_rpt_frame           Error frame to be reported.
 * @param[in]   aux_data                Auxiliary data shared by IP drivers
 *
 * API signature for the common callback function that will be
 * implemented by the set of Tegra onchip IP drivers that report HSI
 * errors to the FSI.
 *
 * @returns
 *  0           (success)
 *  -EINVAL     (On invalid arguments)
 *  -EFAULT     (On IP driver failure to report error)
 *  -ETIME      (On timeout in IP driver)
 */
typedef int (*hsierrrpt_inj)(unsigned int instance_id, struct epl_error_report_frame err_rpt_frame,
			     void *aux_data);

#if IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ)

/**
 * @brief HSI error report injection callback registration
 *
 * @param[in]   ip_id                   Supported IP Id.
 * @param[in]   instance_id             Instance of supported IP.
 * @param[in]   cb_func                 Pointer to callback function.
 * @param[in]   aux_data                Auxiliary data shared by IP drivers
 *
 * API to register the HSI error report trigger callback function
 * with the utility. Tegra onchip IP drivers supporting HSI error
 * reporting to FSI shall call this API once, at launch time.
 *
 * @returns
 *  0           (success)
 *  -EINVAL     (On invalid arguments)
 *  -ENODEV     (On device driver not loaded)
 */
int hsierrrpt_reg_cb(hsierrrpt_ipid_t ip_id, unsigned int instance_id, hsierrrpt_inj cb_func,
		     void *aux_data);

/**
 * @brief HSI error report injection callback de-registration
 *
 * @param[in]   ip_id                   Supported IP Id.
 * @param[in]   instance_id             Instance of supported IP.
 *
 *
 * API to de-register the HSI error report trigger callback function
 * with the utility. Tegra onchip IP drivers supporting HSI error
 * reporting to FSI shall call this API during exit time.
 *
 * @returns
 *  0           (success)
 *  -EINVAL     (On invalid arguments)
 *  -ENODEV     (On device driver not loaded)
 */
int hsierrrpt_dereg_cb(hsierrrpt_ipid_t ip_id, unsigned int instance_id);

#else

static inline int hsierrrpt_reg_cb(hsierrrpt_ipid_t ip_id, unsigned int instance_id,
				   hsierrrpt_inj cb_func, void *aux_data)
{
	pr_info("tegra-hsierrrptinj: CONFIG_TEGRA_HSIERRRPTINJ not enabled\n");
	return 0;
}

static inline int hsierrrpt_dereg_cb(hsierrrpt_ipid_t ip_id, unsigned int instance_id)
{
	pr_info("tegra-hsierrrptinj: CONFIG_TEGRA_HSIERRRPTINJ not enabled\n");
	return 0;
}

#endif /* CONFIG_TEGRA_HSIERRRPTINJ */

#endif /* TEGRA_HSIERRRPTINJ_H */
