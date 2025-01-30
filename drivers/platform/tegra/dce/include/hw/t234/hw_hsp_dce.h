/* SPDX-License-Identifier: MIT */
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

/*
 * Function naming determines intended use:
 *
 *     <x>_r(void) : Returns the offset for register <x>.
 *
 *     <x>_o(void) : Returns the offset for element <x>.
 *
 *     <x>_w(void) : Returns the word offset for word (4 byte) element <x>.
 *
 *     <x>_<y>_s(void) : Returns size of field <y> of register <x> in bits.
 *
 *     <x>_<y>_f(u32 v) : Returns a value based on 'v' which has been shifted
 *         and masked to place it at field <y> of register <x>.  This value
 *         can be |'d with others to produce a full register value for
 *         register <x>.
 *
 *     <x>_<y>_m(void) : Returns a mask for field <y> of register <x>.  This
 *         value can be ~'d and then &'d to clear the value of field <y> for
 *         register <x>.
 *
 *     <x>_<y>_<z>_f(void) : Returns the constant value <z> after being shifted
 *         to place it at field <y> of register <x>.  This value can be |'d
 *         with others to produce a full register value for <x>.
 *
 *     <x>_<y>_v(u32 r) : Returns the value of field <y> from a full register
 *         <x> value 'r' after being shifted to place its LSB at bit 0.
 *         This value is suitable for direct comparison with other unshifted
 *         values appropriate for use in field <y> of register <x>.
 *
 *     <x>_<y>_<z>_v(void) : Returns the constant value for <z> defined for
 *         field <y> of register <x>.  This value is suitable for direct
 *         comparison with unshifted values appropriate for use in field <y>
 *         of register <x>.
 */
#ifndef HW_HSP_DCE_H
#define HW_HSP_DCE_H

static inline u32 hsp_int_ie0_r(void)
{
	return 0x150100U;
}
static inline u32 hsp_int_ie1_r(void)
{
	return 0x150104U;
}
static inline u32 hsp_int_ie2_r(void)
{
	return 0x150108U;
}
static inline u32 hsp_int_ie3_r(void)
{
	return 0x15010cU;
}
static inline u32 hsp_int_ie4_r(void)
{
	return 0x150110U;
}
static inline u32 hsp_int_ie5_r(void)
{
	return 0x150114U;
}
static inline u32 hsp_int_ie6_r(void)
{
	return 0x150118U;
}
static inline u32 hsp_int_ie7_r(void)
{
	return 0x15011cU;
}
static inline u32 hsp_int_ir_r(void)
{
	return 0x150304U;
}
static inline u32 hsp_sm0_r(void)
{
	return 0x160000U;
}
static inline u32 hsp_sm0_full_int_ie_r(void)
{
	return 0x160004U;
}
static inline u32 hsp_sm0_empty_int_ie_r(void)
{
	return 0x160008U;
}
static inline u32 hsp_sm1_r(void)
{
	return 0x168000U;
}
static inline u32 hsp_sm1_full_int_ie_r(void)
{
	return 0x168004U;
}
static inline u32 hsp_sm1_empty_int_ie_r(void)
{
	return 0x168008U;
}
static inline u32 hsp_sm2_r(void)
{
	return 0x170000U;
}
static inline u32 hsp_sm2_full_int_ie_r(void)
{
	return 0x170004U;
}
static inline u32 hsp_sm2_empty_int_ie_r(void)
{
	return 0x170008U;
}
static inline u32 hsp_sm3_r(void)
{
	return 0x178000U;
}
static inline u32 hsp_sm3_full_int_ie_r(void)
{
	return 0x178004U;
}
static inline u32 hsp_sm3_empty_int_ie_r(void)
{
	return 0x178008U;
}
static inline u32 hsp_sm4_r(void)
{
	return 0x180000U;
}
static inline u32 hsp_sm4_full_int_ie_r(void)
{
	return 0x180004U;
}
static inline u32 hsp_sm4_empty_int_ie_r(void)
{
	return 0x180008U;
}
static inline u32 hsp_sm5_r(void)
{
	return 0x188000U;
}
static inline u32 hsp_sm5_full_int_ie_r(void)
{
	return 0x188004U;
}
static inline u32 hsp_sm5_empty_int_ie_r(void)
{
	return 0x188008U;
}
static inline u32 hsp_sm6_r(void)
{
	return 0x190000U;
}
static inline u32 hsp_sm6_full_int_ie_r(void)
{
	return 0x190004U;
}
static inline u32 hsp_sm6_empty_int_ie_r(void)
{
	return 0x190008U;
}
static inline u32 hsp_sm7_r(void)
{
	return 0x198000U;
}
static inline u32 hsp_sm7_full_int_ie_r(void)
{
	return 0x198004U;
}
static inline u32 hsp_sm7_empty_int_ie_r(void)
{
	return 0x198008U;
}
static inline u32 hsp_ss0_state_r(void)
{
	return 0x1a0000U;
}
static inline u32 hsp_ss0_set_r(void)
{
	return 0x1a0004U;
}
static inline u32 hsp_ss0_clr_r(void)
{
	return 0x1a0008U;
}
static inline u32 hsp_ss1_state_r(void)
{
	return 0x1b0000U;
}
static inline u32 hsp_ss1_set_r(void)
{
	return 0x1b0004U;
}
static inline u32 hsp_ss1_clr_r(void)
{
	return 0x1b0008U;
}
static inline u32 hsp_ss2_state_r(void)
{
	return 0x1c0000U;
}
static inline u32 hsp_ss2_set_r(void)
{
	return 0x1c0004U;
}
static inline u32 hsp_ss2_clr_r(void)
{
	return 0x1c0008U;
}
static inline u32 hsp_ss3_state_r(void)
{
	return 0x1d0000U;
}
static inline u32 hsp_ss3_set_r(void)
{
	return 0x1d0004U;
}
static inline u32 hsp_ss3_clr_r(void)
{
	return 0x1d0008U;
}
#endif
