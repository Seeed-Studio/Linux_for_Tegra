/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#ifndef __NVDLA_FW_RISCV_REG_H__
#define __NVDLA_FW_RISCV_REG_H__

static inline uint32_t riscv_mthdwdat_r(void)
{
	/* NV_PNVDLA_FALCON_MTHDWDAT */
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	return 0x0000206cU;
#else
	return 0x0000006cU;
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
}

static inline uint32_t riscv_mthdid_r(void)
{
	/* NV_PNVDLA_FALCON_MTHDID */
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	return 0x00002068U;
#else
	return 0x00000068U;
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
}

static inline uint32_t riscv_mthdid_wpend_v(uint32_t r)
{
	/* NV_PNVDLA_FALCON_MTHDID_WPEND (16:16) */
	return ((r >> 0x10) & 0x1);
}

static inline uint32_t riscv_mthdid_wpend_done_v(void)
{
	/* NV_PNVDLA_FALCON_MTHDID_WPEND_DONE (16:16) */
	return 0x0U;
}

static inline uint32_t riscv_mailbox0_r(void)
{
	/* NV_PNVDLA_FALCON_MAILBOX0 */
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	return 0x00002040U;
#else
	return 0x00000040U;
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
}

static inline uint32_t riscv_mailbox1_r(void)
{
	/* NV_PNVDLA_FALCON_MAILBOX1 */
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	return 0x00002044U;
#else
	return 0x00000044U;
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
}

static inline uint32_t riscv_irqstat_r(void)
{
	/* NV_PNVDLA_FALCON_IRQSTAT */
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	return 0x00002008U;
#else
	return 0x00000008U;
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
}

static inline uint32_t riscv_irqmclr_r(void)
{
	/* NV_PNVDLA_RISCV_IRQMCLR */
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	return 0x00002324U;
#else
	return 0x00000d24U;
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
}

static inline uint32_t riscv_irqmclr_swgen0_set_f(void)
{
	/* NV_PNVDLA_RISCV_IRQMCLR 6:6 */
	return 0x40;
}

static inline uint32_t riscv_irqmclr_swgen1_set_f(void)
{
	/* NV_PNVDLA_RISCV_IRQMCLR 7:7 */
	return 0x80;
}

static inline uint32_t riscv_irqsclr_r(void)
{
	/* NV_PNVDLA_FALCON_IRQSCLR */
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	return 0x00002004U;
#else
	return 0x00000004U;
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
}

static inline uint32_t riscv_irqsclr_swgen0_set_f(void)
{
	/* NV_PNVDLA_FALCON_IRQSCLR 6:6 */
	return 0x40;
}

static inline uint32_t riscv_irqsclr_swgen1_set_f(void)
{
	/* NV_PNVDLA_FALCON_IRQSCLR 7:7 */
	return 0x80;
}

static inline uint32_t riscv_dmatrfcmd_r(void)
{
	/* NV_PNVDLA_FALCON_DMATRFCMD */
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	return 0x00002118U;
#else
	return 0x00000118U;
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
}

static inline uint32_t riscv_dmatrfcmd_idle_v(uint32_t r)
{
	/* NV_PNVDLA_FALCON_DMATRFCMD_IDLE (1:1) */
	return ((r >> 0x1) & 0x1);
}

static inline uint32_t riscv_dmatrfcmd_idle_true_v(void)
{
	/* NV_PNVDLA_FALCON_DMATRFCMD_IDLE_TRUE (1:1) */
	return 0x1U;
}

static inline uint32_t riscv_dmatrfcmd_imem_true_f(void)
{
	/* NV_PRGNLCL_FALCON_DMATRFCMD_IMEM_TRUE (4:4) */
	return (0x1U << 0x4);
}

static inline uint32_t riscv_dmatrfcmd_size_256b_f(void)
{
	/* NV_PRGNLCL_FALCON_DMATRFCMD_SIZE_256B (10:8) */
	return (0x6U << 0x8);
}

static inline uint32_t riscv_dmatrfcmd_ctxdma_f(uint32_t v)
{
	/* NV_PRGNLCL_FALCON_DMATRFCMD_CTXDMA (14:12) */
	return (v & 0x3) << 0xc;
}

static inline uint32_t riscv_transcfg_falc_swid_v(void)
{
	/* NV_PNVDLA_TFBIF_TRANSCFG_FALC_SWID */
	return 0x2U;
}

static inline uint32_t riscv_dmatrfbase_r(void)
{
	/* NV_PNVDLA_FALCON_DMATRFBASE */
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	return 0x00002110U;
#else
	return 0x00000110U;
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
}

static inline uint32_t riscv_dmatrfmoffs_r(void)
{
	/* NV_PNVDLA_FALCON_DMATRFMOFFS */
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	return 0x00002114U;
#else
	return 0x00000114U;
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
}
static inline uint32_t riscv_dmatrffboffs_r(void)
{
	/* NV_PNVDLA_FALCON_DMATRFFBOFFS */
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	return 0x0000211cU;
#else
	return 0x0000011cU;
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
}

static inline uint32_t riscv_dmactl_r(void)
{
	/* NV_PNVDLA_FALCON_DMACTL */
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	return 0x0000210cU;
#else
	return 0x0000010cU;
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
}

static inline uint32_t riscv_dmactl_dmem_scrubbing_m(void)
{
	/* NV_PNVDLA_FALCON_DMACTL_DMEM_SCRUBBING (1:1) */
	return (0x1U << 0x1);
}

static inline uint32_t riscv_dmactl_imem_scrubbing_m(void)
{
	/* NV_PNVDLA_FALCON_DMACTL_DMEM_SCRUBBING (2:2) */
	return (0x1U << 0x2);
}

static inline uint32_t riscv_itfen_r(void)
{
	/* NV_PNVDLA_FALCON_ITFEN */
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	return 0x00002048U;
#else
	return 0x00000048U;
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
}

static inline uint32_t riscv_itfen_ctxen_enable_f(void)
{
	/* NV_PNVDLA_FALCON_ITFEN_CTXEN_ENABLE (0:0) */
	return 0x1U;
}

static inline uint32_t riscv_itfen_mthden_enable_f(void)
{
	/* NV_PNVDLA_FALCON_ITFEN_MTHDEN_ENABLE (1:1) */
	return (0x1U << 0x1);
}

static inline uint32_t riscv_cpuctl_r(void)
{
	/* NV_PNVDLA_RISCV_CPUCTL */
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	return 0x00002288U;
#else
	return 0x00000b88U;
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
}

static inline uint32_t riscv_cpuctl_startcpu_true_f(void)
{
	/* NV_PNVDLA_RISCV_CPUCTL_STARTCPU_TRUE (0:0) */
	return 0x1U;
}

static inline uint32_t riscv_irqtype_r(void)
{
	/* NV_PNVDLA_RISCV_IRQTYPE */
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	return 0x00002330U;
#else
	return 0x00000d30U;
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
}

static inline uint32_t riscv_irqtype_swgen0_host_nonstall_f(void)
{
	/* NV_PNVDLA_RISCV_IRQTYPE_SWGEN0_HOST_NONSTALL (6:6) */
	return (0x1U << 0x6);
}

static inline uint32_t riscv_irqtype_swgen1_host_nonstall_f(void)
{
	/* NV_PNVDLA_RISCV_IRQTYPE_SWGEN1_HOST_NONSTALL (7:7) */
	return (0x1U << 0x7);
}

static inline uint32_t riscv_boot_vector_lo_r(void)
{
	/* NV_PNVDLA_RISCV_BOOT_VECTOR_LO */
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	return 0x00002280U;
#else
	return 0x00000b80U;
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
}

static inline uint32_t riscv_boot_vector_hi_r(void)
{
	/* NV_PNVDLA_RISCV_BOOT_VECTOR_HI */
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	return 0x00002284U;
#else
	return 0x00000b84U;
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
}

static inline uint32_t riscv_bcr_ctrl_r(void)
{
	/* NV_PNVDLA_RISCV_BCR_CTRL */
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	return 0x00002388U;
#else
	return 0x00000e68U;
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
}

static inline uint32_t riscv_bcr_ctrl_core_select_v(uint32_t r)
{
	/* NV_PNVDLA_RISCV_BCR_CTRL_CORE_SELECT (4:4) */
	return ((r >> 0x4) & 0x1);
}

static inline uint32_t riscv_bcr_ctrl_core_select_riscv_f(void)
{
	/* NV_PNVDLA_RISCV_BCR_CTRL_CORE_SELECT_RISCV (4:4) */
	return (0x1U << 0x4);
}

static inline uint32_t riscv_bcr_ctrl_core_select_riscv_v(void)
{
	/* NV_PNVDLA_RISCV_BCR_CTRL_CORE_SELECT_RISCV (4:4) */
	return 0x1U;
}

static inline uint32_t riscv_bcr_ctrl_valid_v(uint32_t r)
{
	/* NV_PNVDLA_RISCV_BCR_CTRL_VALID (0:0) */
	return (r & 0x1);
}

static inline uint32_t riscv_bcr_ctrl_valid_true_f(void)
{
	/* NV_PNVDLA_RISCV_BCR_CTRL_VALID_TRUE (0:0) */
	return 0x1U;
}

static inline uint32_t riscv_bcr_ctrl_valid_true_v(void)
{
	/* NV_PNVDLA_RISCV_BCR_CTRL_VALID_TRUE (0:0) */
	return 0x1U;
}

static inline uint32_t riscv_idlestate_r(void)
{
	/* NV_PNVDLA_FALCON_IDLESTATE */
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	return 0x0000204cU;
#else
	return 0x0000004cU;
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
}

static inline uint32_t riscv_os_version_r(void)
{
	/* NV_PNVDLA_FALCON_OS */
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	return 0x00002080U;
#else
	return 0x00000080U;
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
}

static inline uint32_t riscv_imemc_r(uint32_t index)
{
	/* NV_PNVDLA_FALCON_IMEM(i) */
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	return (0x00002180U + ((index) * 16));
#else
	return (0x00000180U + ((index) * 16));
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
}

static inline uint32_t riscv_imemc_aincw_true_f(void)
{
	/* NV_PNVDLA_FALCON_IMEMC_AINCW_TRUE (24:24) */
	return (0x1U << 24);
}

static inline uint32_t riscv_imemd_r(uint32_t index)
{
	/* NV_PNVDLA_FALCON_IMEMD(i) */
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	return (0x00002184U + ((index) * 16));
#else
	return (0x00000184U + ((index) * 16));
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
}

static inline uint32_t riscv_dmemc_r(uint32_t index)
{
	/* NV_PNVDLA_FALCON_DMEMC(i) */
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	return (0x000021c0U + ((index) * 8));
#else
	return (0x000001c0U + ((index) * 8));
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
}

static inline uint32_t riscv_dmemc_aincw_true_f(void)
{
	/* NV_PNVDLA_FALCON_DMEMC_AINCW_TRUE (24:24) */
	return (0x1U << 24);
}

static inline uint32_t riscv_dmemd_r(uint32_t index)
{
	/* NV_PNVDLA_FALCON_DMEMD(i) */
#if defined(NVDLA_HAVE_CONFIG_FIREWALL) && (NVDLA_HAVE_CONFIG_FIREWALL == 1)
	return (0x000021c4U + ((index) * 8));
#else
	return (0x000001c4U + ((index) * 8));
#endif /* NVDLA_HAVE_CONFIG_FIREWALL */
}
#endif /* End of __NVDLA_FW_RISCV_REG_H__ */
