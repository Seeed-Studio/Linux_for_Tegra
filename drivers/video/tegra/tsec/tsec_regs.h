/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
 *
 * Tegra TSEC Module Support
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

#ifndef TSEC_REGS_H
#define TSEC_REGS_H

#include <linux/types.h>

struct tsec_reg_offsets_t {
	u32 QUEUE_HEAD_0;
	u32 QUEUE_TAIL_0;
	u32 MSGQ_HEAD_0;
	u32 MSGQ_TAIL_0;
	u32 EMEMC_0;
	u32 EMEMD_0;
	u32 THI_INT_STATUS_0;
	u32 THI_INT_STATUS_CLR_0;
	u32 THI_STREAMID0_0;
	u32 THI_STREAMID1_0;
	u32 PRIV_BLOCKER_CTRL_CG1;
	u32 RISCV_CG;
	u32 RISCV_IRQSCLR_0;
	u32 RISCV_IRQSTAT_0;
	u32 RISCV_IRQMSET_0;
	u32 RISCV_IRQMCLR_0;
	u32 RISCV_IRQSCLR_SWGEN0_SET;
	u32 RISCV_IRQMCLR_SWGEN0_SET;
	u32 RISCV_IRQMCLR_SWGEN1_SET;
	u32 RISCV_IRQSTAT_SWGEN0;
	u32 RISCV_IRQSTAT_SWGEN1;
	u32 RISCV_IRQMSET_SWGEN0_SET;
	u32 THI_SEC_0;
	u32 THI_SEC_CHLOCK;
	u32 RISCV_BCR_CTRL;
	u32 RISCV_BCR_CTRL_CORE_SELECT_RISCV;
	u32 RISCV_BCR_DMAADDR_PKCPARAM_LO;
	u32 RISCV_BCR_DMAADDR_PKCPARAM_HI;
	u32 RISCV_BCR_DMAADDR_FMCCODE_LO;
	u32 RISCV_BCR_DMAADDR_FMCCODE_HI;
	u32 RISCV_BCR_DMAADDR_FMCDATA_LO;
	u32 RISCV_BCR_DMAADDR_FMCDATA_HI;
	u32 RISCV_BCR_DMACFG;
	u32 RISCV_BCR_DMACFG_TARGET_LOCAL_FB;
	u32 RISCV_BCR_DMACFG_LOCK_LOCKED;
	u32 RISCV_BCR_DMACFG_SEC;
	u32 RISCV_BCR_DMACFG_SEC_GSCID;
	u32 FALCON_MAILBOX0;
	u32 FALCON_MAILBOX1;
	u32 MAILBOX0;
	u32 MAILBOX1;
	u32 RISCV_CPUCTL;
	u32 RISCV_CPUCTL_STARTCPU_TRUE;
	u32 RISCV_CPUCTL_ACTIVE_STAT;
	u32 RISCV_CPUCTL_ACTIVE_STAT_ACTIVE;
	u32 RISCV_BR_RETCODE;
	u32 RISCV_BR_RETCODE_RESULT;
	u32 RISCV_BR_RETCODE_RESULT_PASS;
	u32 FALCON_DMEMC_0;
	u32 FALCON_DMEMD_0;
	u32 DMEM_LOGBUF_OFFSET;
};

static inline u32 tsec_riscv_bcr_dmacfg_sec_gscid_f(u32 v, u32 offset)
{
	return ((v & offset) << 16);
}


static inline u32 tsec_riscv_cpuctl_active_stat_v(u32 r, u32 offset)
{
	return ((r >> offset) & 0x1);
}

static inline u32 tsec_riscv_br_retcode_result_v(u32 r, u32 offset)
{
	return ((r >> offset) & 0x3);
}


static inline u32 tsec_falcon_dmemc_r(u32 r, u32 offset)
{
	return (offset + (r) * 8);
}

static inline u32 tsec_falcon_dmemd_r(u32 r, u32 offset)
{
	return (offset + (r) * 8);
}

#endif /* TSEC_REGS_H */
