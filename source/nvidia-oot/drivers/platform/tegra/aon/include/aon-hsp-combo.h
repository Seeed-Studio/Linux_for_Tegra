/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef INCLUDE_AON_HSP_COMBO_H
#define INCLUDE_AON_HSP_COMBO_H

#include <linux/types.h>

struct tegra_aon;

int tegra_aon_hsp_sm_tx_write(struct tegra_aon *aon, u32 value);
int tegra_aon_hsp_sm_pair_request(struct tegra_aon *aon,
				void (*full_notify)(void *data, u32 value),
				void *pdata);
void tegra_aon_hsp_sm_pair_free(struct tegra_aon *aon);
bool tegra_aon_hsp_sm_tx_is_empty(struct tegra_aon *aon);

#endif
