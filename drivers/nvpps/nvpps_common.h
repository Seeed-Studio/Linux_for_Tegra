// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#ifndef __NVPPS_COMMON_H__
#define __NVPPS_COMMON_H__

#include <linux/device.h>
#include <asm/arch_timer.h>
#include <linux/platform/tegra/ptp-notifier.h>

struct soc_dev_data;

/*
 * chip specific ops
 * @ptp_tsc_sync_cfg_fn: function pointer for PTP-TSC sync related TSC HW configuration
 * @ptp_tsc_synchronize_fn: function pointer for triggering PTP-TSC synchronization
 * @ptp_tsc_get_is_locked_fn: function pointer to get PTP-TSC sync status, return boolean true if PTP & TSC are synced else return boolean false
 * @ptp_tsc_suspend_sync_fn: function pointer for suspending PTP-TSC synchronization
 * @ptp_tsc_resume_sync_fn: function pointer for resuming PTP-TSC synchronization
 * @get_monotonic_tsc_ts_fn: function pointer to get TSC monotonic timestamp in TSC Cycles, This API can be called in isr context
 * @get_tsc_res_ns_fn: function pointer to get TSC resolution in nanoseconds
 * @get_ptp_tsc_concurrent_ts_ns_fn: function pointer to get PTP-TSC concurrent timestamps in nanosecs, This API can be called in isr context
 * @get_ptp_ts_ns_fn: function pointer to get PTP timestamp in nanoseconds, This API can be called in isr context
 */
struct chip_ops {
	int32_t (*ptp_tsc_sync_cfg_fn)(struct soc_dev_data *soc_data);
	void (*ptp_tsc_synchronize_fn)(struct soc_dev_data *soc_data);
	bool (*ptp_tsc_get_is_locked_fn)(struct soc_dev_data *soc_data);
	int32_t (*ptp_tsc_suspend_sync_fn)(struct soc_dev_data *soc_data);
	int32_t (*ptp_tsc_resume_sync_fn)(struct soc_dev_data *soc_data);
	int32_t (*get_monotonic_tsc_ts_fn)(struct soc_dev_data *soc_data, uint64_t *tsc_ts);
	int32_t (*get_tsc_res_ns_fn)(struct soc_dev_data *soc_data, uint64_t *tsc_res_ns);
	int32_t (*get_ptp_ts_ns_fn)(struct device_node *mac_node, uint64_t *ptp_ts);
	int32_t (*get_ptp_tsc_concurrent_ts_ns_fn)(struct device_node *mac_node, struct ptp_tsc_data *data);
};

struct soc_dev_data {
	/* nvpps device */
	struct device		*dev;
	/* chip specific ops */
	const struct chip_ops *ops;
	/* Variable to hold mmapped addr of TSC registers */
	void __iomem 		*reg_map_base;
	/* Variable to hold base pa of primary MAC interface */
	uint64_t    pri_mac_base_pa;
	/* variable to hold configured lock threshold value
	 * which is to be programmed in the TSC register
	 */
	uint32_t	lock_threshold_val;
	/* variable which defines the number of the pulse per second of
	 * input signal, from MAC interface to TSC
	 */
	uint32_t	pps_freq;
};

#endif /* __NVPPS_COMMON_H__ */
