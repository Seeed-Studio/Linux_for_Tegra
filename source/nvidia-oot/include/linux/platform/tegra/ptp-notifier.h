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

/**
 * @brief ptp_tsc_data - Struture used to store TSC and PTP time
 * information.
 */
struct ptp_tsc_data {
	/** PTP TimeStamp in nSec*/
	u64 ptp_ts;
	/** TSC TimeStamp in nSec*/
	u64 tsc_ts;
};

/* register / unregister HW time source */
void tegra_register_hwtime_source(int (*func)(struct net_device *, void *, int),
				  struct net_device *dev);
void tegra_unregister_hwtime_source(struct net_device *dev);

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
