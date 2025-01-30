/* SPDX-License-Identifier: MIT */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
 * Function/Macro naming determines intended use:
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
#ifndef DCE_HW_HSP_DCE_H
#define DCE_HW_HSP_DCE_H

static inline u32 hsp_0_int_ie0_r(void)
{
	return 0x100100U;
}
static inline u32 hsp_0_int_ie1_r(void)
{
	return 0x100104U;
}
static inline u32 hsp_0_int_ie2_r(void)
{
	return 0x100108U;
}
static inline u32 hsp_0_int_ie3_r(void)
{
	return 0x10010cU;
}
static inline u32 hsp_0_int_ie4_r(void)
{
	return 0x100110U;
}
static inline u32 hsp_0_int_ie5_r(void)
{
	return 0x100114U;
}
static inline u32 hsp_0_int_ie6_r(void)
{
	return 0x100118U;
}
static inline u32 hsp_0_int_ie7_r(void)
{
	return 0x10011cU;
}
static inline u32 hsp_0_int_ir_r(void)
{
	return 0x100304U;
}
static inline u32 hsp_0_sm0_r(void)
{
	return 0x110000U;
}
static inline u32 hsp_0_sm0_full_int_ie_r(void)
{
	return 0x110004U;
}
static inline u32 hsp_0_sm0_empty_int_ie_r(void)
{
	return 0x110008U;
}
static inline u32 hsp_0_sm1_r(void)
{
	return 0x118000U;
}
static inline u32 hsp_0_sm1_full_int_ie_r(void)
{
	return 0x118004U;
}
static inline u32 hsp_0_sm1_empty_int_ie_r(void)
{
	return 0x118008U;
}
static inline u32 hsp_0_sm2_r(void)
{
	return 0x120000U;
}
static inline u32 hsp_0_sm2_full_int_ie_r(void)
{
	return 0x120004U;
}
static inline u32 hsp_0_sm2_empty_int_ie_r(void)
{
	return 0x120008U;
}
static inline u32 hsp_0_sm3_r(void)
{
	return 0x128000U;
}
static inline u32 hsp_0_sm3_full_int_ie_r(void)
{
	return 0x128004U;
}
static inline u32 hsp_0_sm3_empty_int_ie_r(void)
{
	return 0x128008U;
}
static inline u32 hsp_0_sm4_r(void)
{
	return 0x130000U;
}
static inline u32 hsp_0_sm4_full_int_ie_r(void)
{
	return 0x130004U;
}
static inline u32 hsp_0_sm4_empty_int_ie_r(void)
{
	return 0x130008U;
}
static inline u32 hsp_0_sm5_r(void)
{
	return 0x138000U;
}
static inline u32 hsp_0_sm5_full_int_ie_r(void)
{
	return 0x138004U;
}
static inline u32 hsp_0_sm5_empty_int_ie_r(void)
{
	return 0x138008U;
}
static inline u32 hsp_0_sm6_r(void)
{
	return 0x140000U;
}
static inline u32 hsp_0_sm6_full_int_ie_r(void)
{
	return 0x140004U;
}
static inline u32 hsp_0_sm6_empty_int_ie_r(void)
{
	return 0x140008U;
}
static inline u32 hsp_0_sm7_r(void)
{
	return 0x148000U;
}
static inline u32 hsp_0_sm7_full_int_ie_r(void)
{
	return 0x148004U;
}
static inline u32 hsp_0_sm7_empty_int_ie_r(void)
{
	return 0x148008U;
}
static inline u32 hsp_0_ss0_state_r(void)
{
	return 0x150000U;
}
static inline u32 hsp_0_ss0_set_r(void)
{
	return 0x150004U;
}
static inline u32 hsp_0_ss0_clr_r(void)
{
	return 0x150008U;
}
static inline u32 hsp_0_ss1_state_r(void)
{
	return 0x160000U;
}
static inline u32 hsp_0_ss1_set_r(void)
{
	return 0x160004U;
}
static inline u32 hsp_0_ss1_clr_r(void)
{
	return 0x160008U;
}
static inline u32 hsp_0_ss2_state_r(void)
{
	return 0x170000U;
}
static inline u32 hsp_0_ss2_set_r(void)
{
	return 0x170004U;
}
static inline u32 hsp_0_ss2_clr_r(void)
{
	return 0x170008U;
}
static inline u32 hsp_0_ss3_state_r(void)
{
	return 0x180000U;
}
static inline u32 hsp_0_ss3_set_r(void)
{
	return 0x180004U;
}
static inline u32 hsp_0_ss3_clr_r(void)
{
	return 0x180008U;
}
static inline u32 hsp_1_int_ie0_r(void)
{
	return 0x200100U;
}
static inline u32 hsp_1_int_ie1_r(void)
{
	return 0x200104U;
}
static inline u32 hsp_1_int_ie2_r(void)
{
	return 0x200108U;
}
static inline u32 hsp_1_int_ie3_r(void)
{
	return 0x20010cU;
}
static inline u32 hsp_1_int_ie4_r(void)
{
	return 0x200110U;
}
static inline u32 hsp_1_int_ie5_r(void)
{
	return 0x200114U;
}
static inline u32 hsp_1_int_ie6_r(void)
{
	return 0x200118U;
}
static inline u32 hsp_1_int_ie7_r(void)
{
	return 0x20011cU;
}
static inline u32 hsp_1_int_ir_r(void)
{
	return 0x200304U;
}
static inline u32 hsp_1_sm0_r(void)
{
	return 0x210000U;
}
static inline u32 hsp_1_sm0_full_int_ie_r(void)
{
	return 0x210004U;
}
static inline u32 hsp_1_sm0_empty_int_ie_r(void)
{
	return 0x210008U;
}
static inline u32 hsp_1_sm1_r(void)
{
	return 0x218000U;
}
static inline u32 hsp_1_sm1_full_int_ie_r(void)
{
	return 0x218004U;
}
static inline u32 hsp_1_sm1_empty_int_ie_r(void)
{
	return 0x218008U;
}
static inline u32 hsp_1_sm2_r(void)
{
	return 0x220000U;
}
static inline u32 hsp_1_sm2_full_int_ie_r(void)
{
	return 0x220004U;
}
static inline u32 hsp_1_sm2_empty_int_ie_r(void)
{
	return 0x220008U;
}
static inline u32 hsp_1_sm3_r(void)
{
	return 0x228000U;
}
static inline u32 hsp_1_sm3_full_int_ie_r(void)
{
	return 0x228004U;
}
static inline u32 hsp_1_sm3_empty_int_ie_r(void)
{
	return 0x228008U;
}
static inline u32 hsp_1_sm4_r(void)
{
	return 0x230000U;
}
static inline u32 hsp_1_sm4_full_int_ie_r(void)
{
	return 0x230004U;
}
static inline u32 hsp_1_sm4_empty_int_ie_r(void)
{
	return 0x230008U;
}
static inline u32 hsp_1_sm5_r(void)
{
	return 0x238000U;
}
static inline u32 hsp_1_sm5_full_int_ie_r(void)
{
	return 0x238004U;
}
static inline u32 hsp_1_sm5_empty_int_ie_r(void)
{
	return 0x238008U;
}
static inline u32 hsp_1_sm6_r(void)
{
	return 0x240000U;
}
static inline u32 hsp_1_sm6_full_int_ie_r(void)
{
	return 0x240004U;
}
static inline u32 hsp_1_sm6_empty_int_ie_r(void)
{
	return 0x240008U;
}
static inline u32 hsp_1_sm7_r(void)
{
	return 0x248000U;
}
static inline u32 hsp_1_sm7_full_int_ie_r(void)
{
	return 0x248004U;
}
static inline u32 hsp_1_sm7_empty_int_ie_r(void)
{
	return 0x248008U;
}
static inline u32 hsp_1_ss0_state_r(void)
{
	return 0x250000U;
}
static inline u32 hsp_1_ss0_set_r(void)
{
	return 0x250004U;
}
static inline u32 hsp_1_ss0_clr_r(void)
{
	return 0x250008U;
}
static inline u32 hsp_1_ss1_state_r(void)
{
	return 0x260000U;
}
static inline u32 hsp_1_ss1_set_r(void)
{
	return 0x260004U;
}
static inline u32 hsp_1_ss1_clr_r(void)
{
	return 0x260008U;
}
static inline u32 hsp_1_ss2_state_r(void)
{
	return 0x270000U;
}
static inline u32 hsp_1_ss2_set_r(void)
{
	return 0x270004U;
}
static inline u32 hsp_1_ss2_clr_r(void)
{
	return 0x270008U;
}
static inline u32 hsp_1_ss3_state_r(void)
{
	return 0x280000U;
}
static inline u32 hsp_1_ss3_set_r(void)
{
	return 0x280004U;
}
static inline u32 hsp_1_ss3_clr_r(void)
{
	return 0x280008U;
}
#endif
