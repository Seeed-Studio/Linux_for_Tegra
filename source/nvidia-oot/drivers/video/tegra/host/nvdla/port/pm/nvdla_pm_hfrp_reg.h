/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
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

#ifndef __NVDLA_PM_HFRP_REG_H_
#define __NVDLA_PM_HFRP_REG_H_

static inline uint32_t hfrp_mailbox0_mode_r(void)
{
	/* MAILBOX0_MODE */
	return 0x00000004U;
}

static inline uint32_t hfrp_mailbox0_mode_circular_v(void)
{
	/* MAILBOX0_MODE_CIRCULAR (31:0) */
	return 0x00000001U;
}

static inline uint32_t hfrp_irq_in_set_r(void)
{
	/* IRQ_IN_SET */
	return 0x00000100U;
}

static inline uint32_t hfrp_irq_in_set_doorbell_f(uint32_t v)
{
	/* IRQ_IN_SET_DOORBELL (1:1) */
	return ((v & 0x1) << 1);
}

static inline uint32_t hfrp_irq_out_set_r(void)
{
	/* IRQ_OUT_SET */
	return 0x00000104U;
}

static inline uint32_t hfrp_irq_out_set_reset_v(uint32_t r)
{
	/* IRQ_OUT_SET_RESET (0:0) */
	return r & 0x1;
}

static inline uint32_t hfrp_irq_out_set_doorbell_v(uint32_t r)
{
	/* IRQ_OUT_SET_DOORBELL (1:1) */
	return (r >> 1) & 0x1;
}

static inline uint32_t hfrp_irq_out_set_cgstart_v(uint32_t r)
{
	/* IRQ_OUT_SET_CGSTART (26:26) */
	return (r >> 26) & 0x1;
}

static inline uint32_t hfrp_irq_out_set_cgend_v(uint32_t r)
{
	/* IRQ_OUT_SET_CGEND (27:27) */
	return (r >> 27) & 0x1;
}

static inline uint32_t hfrp_irq_out_set_pgstart_v(uint32_t r)
{
	/* IRQ_OUT_SET_PGSTART (28:28) */
	return (r >> 28) & 0x1;
}

static inline uint32_t hfrp_irq_out_set_pgend_v(uint32_t r)
{
	/* IRQ_OUT_SET_PGEND (29:29) */
	return (r >> 29) & 0x1;
}

static inline uint32_t hfrp_irq_out_set_rgstart_v(uint32_t r)
{
	/* IRQ_OUT_SET_RGSTART (30:30) */
	return (r >> 30) & 0x1;
}

static inline uint32_t hfrp_irq_out_set_rgend_v(uint32_t r)
{
	/* IRQ_OUT_SET_RGEND (31:31) */
	return (r >> 31) & 0x1;
}

static inline uint32_t hfrp_irq_in_clr_r(void)
{
	/* IRQ_IN_CLR */
	return 0x00000108U;
}

static inline uint32_t hfrp_irq_out_clr_r(void)
{
	/* IRQ_OUT_CLR */
	return 0x0000010cU;
}

static inline uint32_t hfrp_irq_out_clr_reset_f(uint32_t v)
{
	/* IRQ_OUT_CLR_RESET (0:0) */
	return (v & 0x1);
}

static inline uint32_t hfrp_irq_out_clr_doorbell_f(uint32_t v)
{
	/* IRQ_OUT_CLR_DOORBELL (1:1) */
	return ((v & 0x1) << 1);
}

static inline uint32_t hfrp_irq_out_clr_cgstart_f(uint32_t v)
{
	/* IRQ_OUT_CLR_CGSTART (26:26) */
	return ((v & 0x1) << 26);
}

static inline uint32_t hfrp_irq_out_clr_cgend_f(uint32_t v)
{
	/* IRQ_OUT_CLR_CGEND (27:27) */
	return ((v & 0x1) << 27);
}

static inline uint32_t hfrp_irq_out_clr_pgstart_f(uint32_t v)
{
	/* IRQ_OUT_CLR_PGSTART (28:28) */
	return ((v & 0x1) << 28);
}

static inline uint32_t hfrp_irq_out_clr_pgend_f(uint32_t v)
{
	/* IRQ_OUT_CLR_PGEND (29:29) */
	return ((v & 0x1) << 29);
}

static inline uint32_t hfrp_irq_out_clr_rgstart_f(uint32_t v)
{
	/* IRQ_OUT_CLR_RGSTART (30:30) */
	return ((v & 0x1) << 30);
}

static inline uint32_t hfrp_irq_out_clr_rgend_f(uint32_t v)
{
	/* IRQ_OUT_CLR_RGEND (31:31) */
	return ((v & 0x1) << 31);
}

static inline uint32_t hfrp_buffer_clientoffs_r(void)
{
	/* BUFFER_CLIENTOFFS */
	return 0x00000110U;
}

static inline uint32_t hfrp_buffer_clientoffs_cmd_head_m(void)
{
	/* BUFFER_CLIENTOFFS_CMD_HEAD 7:0 */
	return 0xffU;
}

static inline uint32_t hfrp_buffer_clientoffs_resp_tail_m(void)
{
	/* BUFFER_CLIENTOFFS_RESPONSE_TAIL 15:8 */
	return (0xffU << 8);
}

static inline uint32_t hfrp_buffer_clientoffs_cmd_head_f(uint32_t v)
{
	/* BUFFER_CLIENTOFFS_CMD_HEAD 7:0 */
	return (v & 0xffU);
}

static inline uint32_t hfrp_buffer_clientoffs_cmd_head_v(uint32_t r)
{
	/* BUFFER_CLIENTOFFS_CMD_HEAD 7:0 */
	return (r & 0xffU);
}

static inline uint32_t hfrp_buffer_clientoffs_resp_tail_f(uint32_t v)
{
	/* BUFFER_CLIENTOFFS_RESPONSE_TAIL 15:8 */
	return ((v & 0xffU) << 8);
}

static inline uint32_t hfrp_buffer_clientoffs_resp_tail_v(uint32_t r)
{
	/* BUFFER_CLIENTOFFS_RESPONSE_TAIL 15:8 */
	return ((r >> 8) & 0xffU);
}

static inline uint32_t hfrp_buffer_serveroffs_r(void)
{
	/* BUFFER_SERVEROFFS */
	return 0x00000188U;
}

static inline uint32_t hfrp_buffer_serveroffs_resp_head_v(uint32_t r)
{
	/* BUFFER_SERVEROFFS_RESPONSE_HEAD 7:0 */
	return (r & 0xffU);
}

static inline uint32_t hfrp_buffer_serveroffs_cmd_tail_v(uint32_t r)
{
	/* BUFFER_SERVEROFFS_CMD_TAIL 15:8 */
	return ((r >> 8) & 0xffU);
}

static inline uint32_t hfrp_buffer_cmd_r(uint32_t index)
{
	/* BUFFER_CMD 116B */
	return (0x00000114U + index);
}

static inline uint32_t hfrp_buffer_resp_r(uint32_t index)
{
	/* BUFFER_RESPONSE 116B */
	return (0x0000018cU + index);
}

#endif /* __NVDLA_PM_HFRP_REG_H_ */
