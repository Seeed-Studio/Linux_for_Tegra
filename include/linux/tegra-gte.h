/*
 * Copyright (c) 2020-2022, NVIDIA CORPORATION & AFFILIATES.All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef _TEGRA_GTE_H
#define _TEGRA_GTE_H

#include <linux/device.h>

struct tegra_gte_ev_desc {
	int id;
	u32 ev_bit;
	u32 slice;
};

/* GTE hardware timestamping event details */
struct tegra_gte_ev_detail {
	u64 ts_raw; /* raw counter value */
	u64 ts_ns; /* counter value converted into nano seconds */
	int dir; /* direction of the event */
};

/*
 * GTE event registration function
 *
 * Parameters:
 *
 * Input:
 * @np:		device node of the interested GTE device
 * @ev_id:	event id
 *
 * Returns:
 *		Returns ERR_PTR in case of failure or valid
 *		struct tegra_gte_ev_desc for success.
 *
 * Note:	API is not stable and subject to change.
 */
struct tegra_gte_ev_desc *tegra_gte_register_event(struct device_node *np,
						   u32 ev_id);

/*
 * GTE event un-registration function
 *
 * Parameters:
 *
 * Input:
 * @desc:	This parameter should be the same as returned from register
 *
 * Returns:
 *		Returns 0 for success and any other value for the failure
 *
  * Note:	API is not stable and subject to change.
 */
int tegra_gte_unregister_event(struct tegra_gte_ev_desc *desc);

/*
 * GTE event retrieval function
 *
 * Parameters:
 *
 * Input:
 * @desc:	This parameter should be the same as returned from register
 *
 * Output:
 * @hts:	hts event details
 *
 * Returns:
 *		Returns 0 for success and any other value for the failure
 *
 * Note:	API is not stable and subject to change.
 */
int tegra_gte_retrieve_event(const struct tegra_gte_ev_desc *desc,
			     struct tegra_gte_ev_detail *hts);

#endif
