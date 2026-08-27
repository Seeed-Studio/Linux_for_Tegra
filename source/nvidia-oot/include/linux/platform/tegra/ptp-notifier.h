// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#ifndef __PTP_NOTIFIER_H
#define __PTP_NOTIFIER_H

#include <linux/types.h>
#include <linux/notifier.h>
#include <linux/etherdevice.h>

#define PTP_HWTIME		1
#define PTP_TSC_HWTIME		2
#define TSC_HIGH_SHIFT		32U

/** Max of 5 interfaces created per IP multiply by number of IP */
#define MAX_MAC_INSTANCES	25

/*
 * @brief ptp_tsc_data - Structure used to store TSC and PTP time
 * information.
 */
struct ptp_tsc_data {
	/** PTP TimeStamp in nSec*/
	u64 ptp_ts;
	/** TSC TimeStamp in nSec*/
	u64 tsc_ts;
};

/**
 * @defgroup timesync_internal_api_group Timesync DRIVEOS Internal APIs
 * @{
 */
/**
 * @brief API for network driver to register callback function to read PTP & PTP-TSC concurrent timestamps
 *
 * @param[in] func Callback function pointer to be registered
 *   The callback function must have signature:
 *   int (*func)(struct net_device *dev, void *ts, int ts_type)
 *   Where:
 *   - dev: Network device associated with the time source
 *   - ts: Output buffer for timestamp data (type depends on ts_type)
 *   - ts_type: Type of timestamp requested (PTP_HWTIME or PTP_TSC_HWTIME)
 *   The callback should return 0 on success, negative error code on failure.
 *
 * @param[in] dev Network device to associate with this callback function
 *
 * @return None
 *
 * Behavior and expectations:
 * - Registers a callback function passed by network driver as input to this API
 * - Supports up to MAX_MAC_INSTANCES (25) concurrent registrations
 * - If the same network device is already registered, the call is ignored
 *
 * @pre
 * - Network device must be valid and initialized
 * - PTP hardware should be available and configured
 *
 * @post None
 *
 * @usage
 * - Allowed context:
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: Yes
 *   - Async/Sync: Sync
 * - Required Privileges: None (kernel space only)
 * - API Group
 *   - Init: Yes
 *   - Runtime: Yes
 *   - De-Init: No
 */
void tegra_register_hwtime_source(int (*func)(struct net_device *, void *, int),
				  struct net_device *dev);
/**
 * @brief API for network driver to unregister callback function to read PTP & PTP-TSC concurrent timestamps
 *
 * @param[in] dev Network device to unregister
 *
 * @return None
 *
 * Behavior and expectations:
 * - Removes the callback function associated with the specified network device
 * - Clears both the function pointer and network device from the registry
 * - If the device is not registered, the call is ignored with debug message
 * - Thread-safe implementation using spinlock protection
 *
 * @pre
 * - Network device must be valid
 * - Device should have been previously registered using tegra_register_hwtime_source()
 *
 * @post None
 *
 * @usage
 * - Allowed context:
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: Yes
 *   - Async/Sync: Sync
 * - Required Privileges: None (kernel space only)
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: Yes
 */
void tegra_unregister_hwtime_source(struct net_device *dev);
/** @} */

/* clients registering / unregistering for time update events */
int tegra_register_hwtime_notifier(struct notifier_block *nb);
int tegra_unregister_hwtime_notifier(struct notifier_block *nb);

/* Notify time updates to registered clients */
int tegra_hwtime_notifier_call_chain(unsigned int val, void *v);

/*
 * Get HW time counter.
 * Clients may call the API every anytime PTP/TSC time is needed.
 * If HW time source is not registered, returns -EINVAL
 */
int tegra_get_hwtime(const struct device_node *emac_node, void *ts, int ts_type);


#endif /* __PTP_NOTIFIER_H */
