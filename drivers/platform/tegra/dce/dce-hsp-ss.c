// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2024, NVIDIA CORPORATION.  All rights reserved.
 */

#include <dce.h>
#include <dce-log.h>
#include <dce-util-common.h>

#define DCE_MAX_NO_SS 4

/**
 * ss_set_regs is a 2D array of read-only pointers to a function returning u32.
 *
 * Array of functions that retrun base addresses of shared semaphores set
 * registers in DCE cluster based on the semaphore id and HSP id.
 */
__weak u32 (*const ss_set_regs[DCE_MAX_HSP][DCE_MAX_NO_SS])(void) = {
	{
		hsp_ss0_set_r,
		hsp_ss1_set_r,
		hsp_ss2_set_r,
		hsp_ss3_set_r,
	},
};

/**
 * ss_clear_regs is a 2D array of read-only pointers to a function
 * returning u32.
 *
 * Array of functions that retrun base addresses of shared semaphores clear
 * registers in DCE cluster based on the semaphore id and HSP id.
 */
__weak u32 (*const ss_clear_regs[DCE_MAX_HSP][DCE_MAX_NO_SS])(void) = {
	{
		hsp_ss0_clr_r,
		hsp_ss1_clr_r,
		hsp_ss2_clr_r,
		hsp_ss3_clr_r,
	},
};

/**
 * ss_state_regs is a 2D array of read-only pointers to a function
 * returning u32.
 *
 * Array of functions that retrun base addresses of shared semaphores state
 * registers in DCE cluster based on the semaphore id and HSP id.
 */
__weak u32 (*const ss_state_regs[DCE_MAX_HSP][DCE_MAX_NO_SS])(void) = {
	{
		hsp_ss0_state_r,
		hsp_ss1_state_r,
		hsp_ss2_state_r,
		hsp_ss3_state_r,
	},
};

/**
 * dce_ss_get_state - Get the state of ss_#n in the DCE Cluster
 *
 * @d : Pointer to tegra_dce struct.
 * @hsp_id : ID of hsp instance used
 * @id : Shared Semaphore Id.
 *
 * Return : u32
 */
u32 dce_ss_get_state(struct tegra_dce *d, u8 hsp_id, u8 id)
{
	return dce_readl(d, ss_state_regs[hsp_id][id]());
}

/**
 * dce_ss_set - Set an u32 value to ss_#n in the DCE Cluster
 *
 * @d : Pointer to tegra_dce struct.
 * @bpos : bit to be set.
 * @hsp_id : ID of hsp instance used
 * @id : Shared Semaphore Id.
 *
 * Return : Void
 */
void dce_ss_set(struct tegra_dce *d, u8 bpos, u8 hsp_id, u8 id)
{
	unsigned long val = 0U;

	if (hsp_id >= DCE_MAX_HSP || id >= DCE_MAX_NO_SS) {
		dce_err(d, "Invalid HSP ID:%u OR SS ID:%u", hsp_id, id);
		return;
	}

	val = dce_ss_get_state(d, d->hsp_id, id);

	/**
	 * Debug info. please remove
	 */
	dce_info(d, "Current Value in SS#%d : %lx", id, val);

	/**
	 * TODO :Use DCE_INSERT here.
	 */
	dce_bitmap_set(&val, bpos, 1);

	/**
	 * Debug info. please remove
	 */
	dce_info(d, "Value after bitmap operation : %lx", val);

	dce_writel(d, ss_set_regs[hsp_id][id](), (u32)val);

	/**
	 * Debug info. please remove
	 */
	val = dce_ss_get_state(d, d->hsp_id, id);
	dce_info(d, "Current Value in SS#%d : %lx", id, val);
}

/**
 * dce_ss_clear - Clear a bit in ss_#n in the DCE Cluster
 *
 * @d : Pointer to tegra_dce struct.
 * @bpos : bit to be cleared.
 * @hsp_id : ID of hsp instance used
 * @id : Shared Semaphore Id.
 *
 * Return : Void
 */
void dce_ss_clear(struct tegra_dce *d, u8 bpos, u8 hsp_id, u8 id)
{
	unsigned long val;

	if (hsp_id >= DCE_MAX_HSP || id >= DCE_MAX_NO_SS) {
		dce_err(d, "Invalid HSP ID:%u OR SS ID:%u", hsp_id, id);
		return;
	}

	val = dce_ss_get_state(d, d->hsp_id, id);

	dce_bitmap_set(&val, bpos, 1);

	dce_writel(d, ss_clear_regs[hsp_id][id](), val);
}
