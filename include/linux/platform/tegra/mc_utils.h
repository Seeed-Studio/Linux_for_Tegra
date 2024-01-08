// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2022-2024, NVIDIA CORPORATION. All rights reserved.

#ifndef __TEGRA_MC_UTILS_H
#define __TEGRA_MC_UTILS_H

struct mc_utils_ops {
	unsigned long (*emc_freq_to_bw)(unsigned long freq);
	unsigned long (*emc_bw_to_freq)(unsigned long bw);
	u8 (*get_dram_num_channels)(void);
};

/*
 * Utility API to convert the given frequency to Bandwidth.
 *
 * @freq Frequency to convert. It can be in any unit - the resulting Bandwidth
 *       will be in the same unit as passed. E.g KHz leads to KBps and Hz
 *       leads to Bps.
 *
 * Converts EMC clock frequency into theoretical BW. This
 * does not account for a realistic utilization of the EMC bus. That is the
 * various overheads (refresh, bank commands, etc) that a real system sees
 * are not computed.
 *
 * Return: Converted Bandwidth.
 */
unsigned long emc_freq_to_bw(unsigned long freq);

/*
 * Utility API to convert the given Bandwidth to frequency.
 *
 * @bw Bandwidth to convert. It can be in any unit - the resulting frequency
 *     will be in the same unit as passed. E.g KBps leads to KHz and Bps leads
 *     to Hz.
 *
 * Converts BW into theoretical EMC clock frequency.
 *
 * Return: Converted Frequency.
 */
unsigned long emc_bw_to_freq(unsigned long bw);

/*
 * Return Number of channels of dram.
 *
 * Return number of dram channels
 *
 * Return: dram channels.
 */
u8 get_dram_num_channels(void);
#endif /* __TEGRA_MC_UTILS_H */
