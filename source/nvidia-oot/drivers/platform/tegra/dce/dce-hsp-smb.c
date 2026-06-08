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
 * smb_regs is a 2D array of read-only pointers to a function returning u32.
 *
 * Array of functions that retrun base addresses of shared maiboxes registers
 * in DCE cluster based on the mailbox id and HSP id.
 */
static  u32 (*const smb_regs[DCE_MAX_HSP_T234][DCE_MAX_NO_SMB_T234])(void) = {
	{
		hsp_sm0_r,
		hsp_sm1_r,
		hsp_sm2_r,
		hsp_sm3_r,
		hsp_sm4_r,
		hsp_sm5_r,
		hsp_sm6_r,
		hsp_sm7_r,
	},
};

/**
 * smb_full_ie_regs is a 2D array of read-only pointers to a function
 * returning u32.
 *
 * Array of functions that retrun base addresses of full IE for shared
 * maiboxes registers in DCE cluster based on the mailbox id and HSP id.
 */
static  u32 (*const smb_full_ie_regs[DCE_MAX_HSP_T234][DCE_MAX_NO_SMB_T234])(void) = {
	{
		hsp_sm0_full_int_ie_r,
		hsp_sm1_full_int_ie_r,
		hsp_sm2_full_int_ie_r,
		hsp_sm3_full_int_ie_r,
		hsp_sm4_full_int_ie_r,
		hsp_sm5_full_int_ie_r,
		hsp_sm6_full_int_ie_r,
		hsp_sm7_full_int_ie_r,
	},
};

/**
 * smb_empty_ie_regs is a 2D array of read-only pointers to a function
 * returning u32.
 *
 * Array of functions that retrun base addresses of empty IE for shared
 * maiboxes registers in DCE cluster based on the mailbox id and HSP id.
 */
static  u32 (*const smb_empty_ie_regs[DCE_MAX_HSP_T234][DCE_MAX_NO_SMB_T234])(void) = {
	{
		hsp_sm0_empty_int_ie_r,
		hsp_sm1_empty_int_ie_r,
		hsp_sm2_empty_int_ie_r,
		hsp_sm3_empty_int_ie_r,
		hsp_sm4_empty_int_ie_r,
		hsp_sm5_empty_int_ie_r,
		hsp_sm6_empty_int_ie_r,
		hsp_sm7_empty_int_ie_r,
	},
};

/**
 * dce_smb_set_t234 - Set an u32 value to smb_#n in the DCE Cluster
 *
 * @d : Pointer to tegra_dce struct.
 * @val : val to set.
 * @hsp_id : ID of hsp instance used
 * @id : Shared Mailbox Id.
 *
 * Return : Void
 */
void dce_smb_set_t234(struct tegra_dce *d, u32 val, u8 hsp_id, u8 id)
{
	if (id >= DCE_MAX_NO_SMB_T234 || hsp_id >= DCE_MAX_HSP_T234) {
		dce_os_err(d, "Invalid Shared Mailbox ID:%u or hsp:%u", id, hsp_id);
		return;
	}

	dce_os_writel(d, smb_regs[hsp_id][id](), val);
}

/**
 * dce_smb_set_full_ie_t234 - Set an u32 value to smb_#n in the DCE Cluster
 *
 * @d : Pointer to tegra_dce struct.
 * @en : enable if true and disable if false
 * @hsp_id : ID of hsp instance used
 * @id : Shared Mailbox Id.
 *
 * Return : Void
 */
void dce_smb_set_full_ie_t234(struct tegra_dce *d, bool en, u8 hsp_id, u8 id)
{
	u32 val = en ? 1U : 0U;

	if (id >= DCE_MAX_NO_SMB_T234 || hsp_id >= DCE_MAX_HSP_T234) {
		dce_os_err(d, "Invalid Shared Mailbox ID:%u or hsp:%u", id, hsp_id);
		return;
	}

	dce_os_writel(d, smb_full_ie_regs[hsp_id][id](), val);
}

/**
 * dce_smb_read_full_ie_t234 - Set an u32 value to smb_#n in the DCE Cluster
 *
 * @d : Pointer to tegra_dce struct.
 * @hsp_id : ID of hsp instance used
 * @id : Shared Mailbox Id.
 *
 * Return : u32 register value
 */
u32 dce_smb_read_full_ie_t234(struct tegra_dce *d, u8 hsp_id, u8 id)
{
	if (id >= DCE_MAX_NO_SMB_T234 || hsp_id >= DCE_MAX_HSP_T234) {
		dce_os_err(d, "Invalid Shared Mailbox ID:%u or hsp:%u", id, hsp_id);
		return 0xffffffff; /* TODO : Add DCE Error Numbers */
	}

	return dce_os_readl(d, smb_full_ie_regs[hsp_id][id]());
}

/**
 * dce_smb_enable_empty_ie_t234 - Set an u32 value to smb_#n in the DCE Cluster
 *
 * @d : Pointer to tegra_dce struct.
 * @en : enable if true and disable if false
 * @hsp_id : ID of hsp instance used
 * @id : Shared Mailbox Id.
 *
 * Return : Void
 */
void dce_smb_set_empty_ie_t234(struct tegra_dce *d, bool en, u8 hsp_id, u8 id)
{
	u32 val = en ? 1U : 0U;

	if (id >= DCE_MAX_NO_SMB_T234 || hsp_id >= DCE_MAX_HSP_T234) {
		dce_os_err(d, "Invalid Shared Mailbox ID:%u or hsp:%u", id, hsp_id);
		return;
	}

	dce_os_writel(d, smb_empty_ie_regs[hsp_id][id](), val);
}

/**
 * dce_smb_read_t234 - Read the u32 value from smb_#n in the DCE Cluster
 *
 * @d : Pointer to tegra_dce struct.
 * @hsp_id : ID of hsp instance used
 * @id : Shared Mailbox Id.
 *
 * Return : actual value if successful, 0xffffffff for errors scenarios
 */
u32 dce_smb_read_t234(struct tegra_dce *d, u8 hsp_id, u8 id)
{
	if (id >= DCE_MAX_NO_SMB_T234 || hsp_id >= DCE_MAX_HSP_T234) {
		dce_os_err(d, "Invalid Shared Mailbox ID:%u or hsp:%u", id, hsp_id);
		return 0xffffffff; /* TODO : Add DCE Error Numbers */
	}

	return dce_os_readl(d, smb_regs[hsp_id][id]());
}

/**
 * hsp_int_ie_regs is a 2D array of read-only pointers to a
 * function returning u32.
 *
 * Array of functions that retrun base addresses of hsp IE
 * regs in DCE cluster based on the id.
 */
static  u32 (*const hsp_int_ie_regs[DCE_MAX_HSP_T234][DCE_MAX_HSP_IE_T234])(void) = {
	{
		hsp_int_ie0_r,
		hsp_int_ie1_r,
		hsp_int_ie2_r,
		hsp_int_ie3_r,
		hsp_int_ie4_r,
		hsp_int_ie5_r,
		hsp_int_ie6_r,
		hsp_int_ie7_r,
	},
};

/**
 * hsp_int_ie_regs is a 1D array of read-only pointers to a
 * function returning u32.
 *
 * Array of functions that retrun addresses of hsp IR
 * regs in DCE cluster based on the id.
 */
static  u32 (*const hsp_int_ir_regs[DCE_MAX_HSP_T234])(void) = {

	hsp_int_ir_r,
};

/**
 * dce_hsp_ie_read_t234 - Read the u32 value from hsp_int_ie#n
 *						in the DCE Cluster
 *
 * @d : Pointer to tegra_dce struct.
 * @hsp_id : ID of hsp instance used
 * @id : Shared IE Id.
 *
 * Return : actual value if successful, 0xffffffff for errors scenarios
 */
u32 dce_hsp_ie_read_t234(struct tegra_dce *d, u8 hsp_id, u8 id)
{
	if (id >= DCE_MAX_HSP_IE_T234 || hsp_id >= DCE_MAX_HSP_T234) {
		dce_os_err(d, "Invalid Shared HSP IE ID:%u or hsp:%u", id, hsp_id);
		return 0xffffffff; /* TODO : Add DCE Error Numbers */
	}

	return dce_os_readl(d, hsp_int_ie_regs[hsp_id][id]());
}

/**
 * dce_hsp_ie_write_t234 - Read the u32 value from hsp_int_ie#n
 *						in the DCE Cluster
 *
 * @d : Pointer to tegra_dce struct.
 * @val : Value to be written
 * @hsp_id : ID of hsp instance used
 * @id : Shared IE Id.
 *
 * Return : void
 */
void dce_hsp_ie_write_t234(struct tegra_dce *d, u32 val, u8 hsp_id, u8 id)
{
	if (id >= DCE_MAX_HSP_IE_T234 || hsp_id >= DCE_MAX_HSP_T234) {
		dce_os_err(d, "Invalid Shared HSP IE ID:%u or hsp:%u", id, hsp_id);
		return;
	}

	dce_os_writel(d, hsp_int_ie_regs[hsp_id][id](),
			val | dce_os_readl(d, hsp_int_ie_regs[hsp_id][id]()));
}

/**
 * dce_hsp_ir_read_t234 - Read the u32 value from hsp_int_ir
 *					in the DCE Cluster
 *
 * @d : Pointer to tegra_dce struct.
 * @hsp_id : ID of hsp instance used
 *
 * Return : actual value if successful, 0xffffffff for errors scenarios
 */
u32 dce_hsp_ir_read_t234(struct tegra_dce *d, u8 hsp_id)
{
	if (hsp_id >= DCE_MAX_HSP_T234) {
		dce_os_err(d, "Invalid HSP ID:%u", hsp_id);
		return 0xffffffff; /* TODO : Add DCE Error Numbers */
	}

	return dce_os_readl(d, hsp_int_ir_regs[hsp_id]());
}
