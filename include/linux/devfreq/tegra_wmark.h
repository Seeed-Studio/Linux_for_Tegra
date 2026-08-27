/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023, NVIDIA Corporation. All rights reserved.
 */

#ifndef DEVFREQ_TEGRA_WMARK_H
#define DEVFREQ_TEGRA_WMARK_H

#include <linux/devfreq.h>
#include <linux/notifier.h>
#include <linux/types.h>

#define DEVFREQ_GOV_TEGRA_WMARK		"tegra_wmark"

enum devfreq_tegra_wmark_event {
	DEVFREQ_TEGRA_AVG_WMARK_BELOW,
	DEVFREQ_TEGRA_AVG_WMARK_ABOVE,
	DEVFREQ_TEGRA_CONSEC_WMARK_BELOW,
	DEVFREQ_TEGRA_CONSEC_WMARK_ABOVE,
};

/**
 * struct devfreq_tegra_wmark_config - watermark thresholds configuration
 * @avg_upper_wmark		Moving average upper watermark threshold value
 * @avg_lower_wmark		Moving average lower watermark threshold value
 * @consec_upper_wmark		Consecutive upper watermark threshold value
 * @consec_lower_wmark		Consecutive lower watermark threshold value
 * @upper_wmark_enabled		Interrupt enable flag for upper watermarks
 * @lower_wmark_enabled		Interrupt enable flag for lower watermarks
 */
struct devfreq_tegra_wmark_config {
	u32 avg_upper_wmark;
	u32 avg_lower_wmark;
	u32 consec_upper_wmark;
	u32 consec_lower_wmark;
	bool upper_wmark_enabled;
	bool lower_wmark_enabled;
};

/**
 * struct devfreq_tegra_wmark_drv_data - private data stored in struct devfreq
 * @event:			Given different event types, the governor will
 *				estimate the next device frequency with different
 *				policies.
 * @update_wmark_threshold:	Callback function provided by the devfreq driver
 *				to update the watermark thresholds of the actmon
 *				monitoring the device active time.
 */
struct devfreq_tegra_wmark_data {
	enum devfreq_tegra_wmark_event event;
	void (*update_wmark_threshold)(struct devfreq *this,
				       struct devfreq_tegra_wmark_config *cfg);
};

#endif /* DEVFREQ_TEGRA_WMARK_H */
