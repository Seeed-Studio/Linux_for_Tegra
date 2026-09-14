// SPDX-License-Identifier: MIT
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <dce.h>
#include <dce-os-log.h>
#include <dce-os-utils.h>
#include <dce-hsp-t234.h>
#include <hw/t234/hw_hsp_dce.h>

/**
 * ss_set_regs is a 2D array of read-only pointers to a function returning u32.
 *
 * Array of functions that retrun base addresses of shared semaphores set
 * registers in DCE cluster based on the semaphore id and HSP id.
 */
static u32 (*const ss_set_regs[DCE_MAX_HSP_T234][DCE_MAX_NO_SS_T234])(void) = {
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
static u32 (*const ss_clear_regs[DCE_MAX_HSP_T234][DCE_MAX_NO_SS_T234])(void) = {
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
static u32 (*const ss_state_regs[DCE_MAX_HSP_T234][DCE_MAX_NO_SS_T234])(void) = {
	{
		hsp_ss0_state_r,
		hsp_ss1_state_r,
		hsp_ss2_state_r,
		hsp_ss3_state_r,
	},
};

/**
 * dce_ss_get_state_t234 - Get the state of ss_#n in the DCE Cluster
 *
 * @d : Pointer to tegra_dce struct.
 * @hsp_id : ID of hsp instance used
 * @id : Shared Semaphore Id.
 *
 * Return : u32
 */
u32 dce_ss_get_state_t234(struct tegra_dce *d, u8 hsp_id, u8 id)
{
	return dce_os_readl(d, ss_state_regs[hsp_id][id]());
}

/**
 * dce_ss_set_t234 - Set an u32 value to ss_#n in the DCE Cluster
 *
 * @d : Pointer to tegra_dce struct.
 * @bpos : bit to be set.
 * @hsp_id : ID of hsp instance used
 * @id : Shared Semaphore Id.
 *
 * Return : Void
 */
void dce_ss_set_t234(struct tegra_dce *d, u8 bpos, u8 hsp_id, u8 id)
{
	unsigned long val = 0U;

	if (hsp_id >= DCE_MAX_HSP_T234 || id >= DCE_MAX_NO_SS_T234) {
		dce_os_err(d, "Invalid HSP ID:%u OR SS ID:%u", hsp_id, id);
		return;
	}

	val = dce_ss_get_state_t234(d, d->hsp_id, id);

	/**
	 * Debug info. please remove
	 */
	dce_os_info(d, "Current Value in SS#%d : %lx", id, val);

	/**
	 * TODO :Use DCE_INSERT here.
	 */
	dce_os_bitmap_set(&val, bpos, 1);

	/**
	 * Debug info. please remove
	 */
	dce_os_info(d, "Value after bitmap operation : %lx", val);

	dce_os_writel(d, ss_set_regs[hsp_id][id](), (u32)val);

	/**
	 * Debug info. please remove
	 */
	val = dce_ss_get_state_t234(d, d->hsp_id, id);
	dce_os_info(d, "Current Value in SS#%d : %lx", id, val);
}

/**
 * dce_ss_clear_t234 - Clear a bit in ss_#n in the DCE Cluster
 *
 * @d : Pointer to tegra_dce struct.
 * @bpos : bit to be cleared.
 * @hsp_id : ID of hsp instance used
 * @id : Shared Semaphore Id.
 *
 * Return : Void
 */
void dce_ss_clear_t234(struct tegra_dce *d, u8 bpos, u8 hsp_id, u8 id)
{
	unsigned long val;

	if (hsp_id >= DCE_MAX_HSP_T234 || id >= DCE_MAX_NO_SS_T234) {
		dce_os_err(d, "Invalid HSP ID:%u OR SS ID:%u", hsp_id, id);
		return;
	}

	val = dce_ss_get_state_t234(d, d->hsp_id, id);

	dce_os_bitmap_set(&val, bpos, 1);

	dce_os_writel(d, ss_clear_regs[hsp_id][id](), val);
}
