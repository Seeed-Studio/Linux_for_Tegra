/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_TEGRA_STATS_H
#define PVA_KMD_TEGRA_STATS_H
#include "pva_kmd_device.h"

/**
 * @brief Structure which holds vpu stats information
 */
struct pva_kmd_tegrastats {
	/** Holds vpu utilization as a percentage for each VPU in the PVA */
	uint64_t average_vpu_utilization[PVA_NUM_PVE];
	/** Current state of pva_kmd_tegrastats */
	uint64_t window_start_time;
	uint64_t window_end_time;
};

void pva_kmd_device_init_tegra_stats(struct pva_kmd_device *pva);

void pva_kmd_device_deinit_tegra_stats(struct pva_kmd_device *pva);

enum pva_error
pva_kmd_notify_fw_get_tegra_stats(struct pva_kmd_device *pva,
				  struct pva_kmd_tegrastats *kmd_tegra_stats);

#endif
