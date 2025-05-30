// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2015-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#ifndef _UFS_TEGRA_H
#define _UFS_TEGRA_H

#include <linux/io.h>
#include <soc/tegra/fuse.h>
#if defined(CONFIG_TEGRA_PROD_LEGACY)
#include <linux/tegra_prod.h>
#endif

#define NV_ADDRESS_MAP_MPHY_L0_BASE		0x02470000
#define NV_ADDRESS_MAP_MPHY_L1_BASE		0x02480000
#define NV_ADDRESS_MAP_T23X_UFSHC_VIRT_BASE	0x02520000
#define MPHY_ADDR_RANGE_T234			0x2268
#define UFS_AUX_ADDR_VIRT_RANGE_23X		0x14f

#define NV_ADDRESS_MAP_MPHY_L0_BASE_T264       0xa80b910000
#define NV_ADDRESS_MAP_MPHY_L1_BASE_T264       0xa80b920000
#define MPHY_ADDR_RANGE_T264                   0x10000
/* UFS AUX Base address for T264 */
#define NV_ADDRESS_MAP_T264_UFSHC_AUX_BASE     0xa80b8e0000
/* UFS AUX address range in T264 */
#define UFS_AUX_ADDR_RANGE_264                 0x100

/* UFS VIRTUALIZATION address for T264 */
#define NV_ADDRESS_MAP_T264_UFSHC_VIRT_BASE    0xa80b8f0000
/* UFS VIRTUALIZATION range in T264 */
#define UFS_AUX_ADDR_VIRT_RANGE_264            0x200

/* UFS AUX Base address for T234 */
#define NV_ADDRESS_MAP_T23X_UFSHC_AUX_BASE	0x02510000
/* UFS AUX address range in T234 */
#define UFS_AUX_ADDR_RANGE_23X			0x20
#define UFS_AUX_ADDR_VIRT_CTRL_0		0x0
#define UFS_AUX_ADDR_VIRT_CTRL_EN		0x1
#define UFS_AUX_ADDR_VIRT_PA_VA_CTRL		0x2
#define UFS_AUX_ADDR_VIRT_REG_0			0x4
#define UFSHC_AUX_UFSHC_CARD_DET_LP_PWR_CTRL_0	0x1CU

/*
 * M-PHY Registers
 */
#define MPHY_RX_APB_CAPABILITY_9C_9F_0		0x9c
#define MPHY_RX_APB_VENDOR22_0			0x1d4
#define MPHY_RX_APB_VENDOR24_0			0x1dc
#define MPHY_RX_APB_VENDOR3_0_T234		0x2188
#define MPHY_RX_APB_VENDOR4_0_T234		0x218c
#define MPHY_RX_APB_VENDOR5_0_T234		0x2190
#define MPHY_RX_APB_VENDOR8_0_T234		0x219c
#define MPHY_RX_APB_VENDOR9_0_T234		0x21a0
#define MPHY_RX_APB_VENDOR14_0_T234		0x21b4
#define MPHY_RX_APB_VENDOR22_0_T234		0x21d4
#define MPHY_RX_APB_VENDOR24_0_T234		0x21dc
#define MPHY_RX_APB_VENDOR34_0_T234		0x2204
#define MPHY_RX_APB_VENDOR37_0_T234		0X2210
#define MPHY_RX_APB_VENDOR3B_0_T234             0X2220
#define MPHY_RX_APB_VENDOR49_0_T234		0x2254

#define MPHY_TX_APB_TX_ATTRIBUTE_2C_2F_0	0x2c
#define MPHY_TX_APB_TX_VENDOR0_0		0x100
#define MPHY_TX_APB_TX_CG_OVR0_0		0x170
#define MPHY_TX_APB_TX_VENDOR0_0_T234		0x1100
#define MPHY_TX_APB_TX_VENDOR3_0_T234		0x110c
#define MPHY_TX_APB_TX_VENDOR4_0_T234		0x1110
#define MPHY_TX_APB_TX_CG_OVR0_0_T234		0x1170
#define MPHY_TX_APB_PAD_TIMING14_0_T234		0x1194

#define MPHY_TX_APB_TX_CLK_CTRL0_0		0x160
#define MPHY_TX_APB_TX_CLK_CTRL2_0		0x168
#define MPHY_TX_APB_TX_CLK_CTRL0_0_T234		0x1160
#define MPHY_TX_APB_TX_CLK_CTRL2_0_T234		0x1168

#define MPHY_TX_CLK_EN_SYMB	BIT(1)
#define MPHY_TX_CLK_EN_SLOW	BIT(3)
#define MPHY_TX_CLK_EN_FIXED	BIT(4)
#define MPHY_TX_CLK_EN_3X	BIT(5)

#define MPHY_TX_APB_TX_ATTRIBUTE_34_37_0	0x34
#define TX_ADVANCED_GRANULARITY		(0x8 << 16)
#define TX_ADVANCED_GRANULARITY_SETTINGS	(0x1 << 8)

#define TX_HS_Equalizer_Setting_FIELD_START	24
#define TX_HS_Equalizer_Setting_FIELD_LEN	3

#define MPHY_PWR_CHANGE_CLK_BOOST		0x0017
#define MPHY_EQ_TIMEOUT				0x1AADB5
#define MPHY_EQ_TIMEOUT_T264			0xFFFFFFFF
#define MPHY_GO_BIT	1U

#define MPHY_RX_APB_CAPABILITY_88_8B_0		0x88
#define RX_HS_G1_SYNC_LENGTH_CAPABILITY(x)	(((x) & 0x3f) << 24)
#define RX_HS_SYNC_LENGTH			0xf

#define MPHY_RX_APB_CAPABILITY_94_97_0		0x94
#define RX_HS_G2_SYNC_LENGTH_CAPABILITY(x)	(((x) & 0x3f) << 0)
#define RX_HS_G3_SYNC_LENGTH_CAPABILITY(x)	(((x) & 0x3f) << 8)

#define MPHY_RX_APB_CAPABILITY_8C_8F_0		0x8c
#define RX_MIN_ACTIVATETIME_CAP_ARG(x)		(((x) & 0xf) << 24)
#define RX_MIN_ACTIVATETIME			0x5

#define	MPHY_RX_APB_CAPABILITY_98_9B_0		0x98
#define RX_ADVANCED_FINE_GRANULARITY(x)		(((x) & 0x1) << 0)
#define	RX_ADVANCED_GRANULARITY(x)		(((x) & 0x3) << 1)
#define	RX_ADVANCED_MIN_ACTIVATETIME(x)		(((x) & 0xf) << 16)
#define RX_ADVANCED_MIN_AT			0xa

#define MPHY_RX_APB_VENDOR2_0			0x184
#define MPHY_RX_APB_VENDOR2_0_T234		0x2184
#define MPHY_RX_APB_VENDOR3_0			0x188
#define MPHY_RX_APB_VENDOR3_0_T234		0x2188
#define MPHY_RX_APB_VENDOR2_0_RX_CAL_EN		BIT(15)
#define MPHY_RX_APB_VENDOR2_0_RX_CAL_DONE	BIT(19)
#define MPHY_ENABLE_RX_MPHY2UPHY_IF_OVR_CTRL	BIT(26)

#define MPHY_TX_APB_TX_VENDOR2_0_T264		0x1108
#define MPHY_TX_APB_VENDOR2_0_TX_CAL_EN		BIT(15)
#define MPHY_TX_APB_VENDOR2_0_TX_CAL_DONE	BIT(19)

#define MPHY_RX_CAPABILITY_88_8B_VAL_FPGA	0x4f00fa1a
#define MPHY_RX_CAPABILITY_8C_8F_VAL_FPGA	0x50e080e
#define MPHY_RX_CAPABILITY_94_97_VAL_FPGA	0xe0e4f4f
#define MPHY_RX_CAPABILITY_98_9B_VAL_FPGA	0x4e0a0203

/* T234 FPGA specific values for clock dividor */
#define MPHY_RX_PWM_CLOCK_DIV_VAL_FPGA		0x80f1e34
#define MPHY_RX_HS_CLOCK_DIV_VAL_FPGA		0x01020608
#define MPHY_TX_PWM_CLOCK_DIV_VAL_FPGA		0x08102040
#define MPHY_TX_HS_CLOCK_DIV_VAL_FPGA		0x00000220
#define MPHY_TX_HIBERN8_ENTER_TIME_FPGA		0x8

#define MPHY_RX_GO_REG_VAL_FPGA			0x4001

/* Unipro Vendor registers */

/*
 * Vendor Specific Attributes
 */

#define VS_DEBUGSAVECONFIGTIME		0xD0A0
#define VS_DEBUGSAVECONFIGTIME_TREF	0x6
#define SET_TREF(x)			(((x) & 0x7) << 2)
#define VS_DEBUGSAVECONFIGTIME_ST_SCT	0x3
#define SET_ST_SCT(x)			((x) & 0x3)
#define VS_BURSTMBLCONFIG		(0x5 << 13)
#define VS_BURSTMBLREGISTER		0xc0
#define VS_TXBURSTCLOSUREDELAY		0xD084

/*UFS Clock Defines*/
#define UFSHC_CLK_FREQ		204000000
#define UFSHC_CLK_FREQ_T264	208000000
#define UFSDEV_CLK_FREQ		19200000

/*Uphy pll clock defines*/
#define UFS_CLK_UPHY_PLL3_RATEA 4992000000
#define UFS_CLK_UPHY_PLL3_RATEB 5840000000
#define UFS_CLK_UPHY_PLL3_RATEB_T264  582400000

/* HS clock frequencies */
#define MPHY_TX_HS_BIT_DIV_CLK	600000000
#define MPHY_RX_HS_BIT_DIV_CLK	312500000

enum ufs_state {
	UFSHC_INIT,
	UFSHC_SUSPEND,
	UFSHC_RESUME,
};

/* vendor specific pre-defined parameters */

/*
 * HCLKFrequency in MHz.
 * HCLKDIV is used to generate 1usec tick signal used by Unipro.
 */
#define UFS_VNDR_HCLKDIV_1US_TICK	0xCC
#define UFS_VNDR_HCLKDIV_1US_TICK_T264	0xD0
#define UFS_VNDR_HCLKDIV_1US_TICK_FPGA	0x1A

/*UFS host controller vendor specific registers */
enum {
	REG_UFS_VNDR_HCLKDIV	= 0xFC,
};

/*
 * UFS AUX Registers
 */

#define UFSHC_AUX_UFSHC_STATUS_0	0x10
#define UFSHC_HIBERNATE_STATUS		BIT(0)
#define UFSHC_AUX_UFSHC_DEV_CTRL_0	0x14
#define UFSHC_DEV_CLK_EN		BIT(0)
#define UFSHC_DEV_RESET			BIT(1)
#define UFSHC_AUX_UFSHC_SW_EN_CLK_SLCG_0	0x08
#define UFSHC_CLK_OVR_ON	BIT(0)
#define UFSHC_HCLK_OVR_ON	BIT(1)
#define UFSHC_LP_CLK_T_CLK_OVR_ON	BIT(2)
#define UFSHC_CLK_T_CLK_OVR_ON		BIT(3)
#define UFSHC_CG_SYS_CLK_OVR_ON		BIT(4)
#define UFSHC_TX_SYMBOL_CLK_OVR_ON	BIT(5)
#define UFSHC_RX_SYMBOLCLKSELECTED_CLK_OVR_ON		BIT(6)
#define UFSHC_PCLK_OVR_ON		BIT(7)

#define PA_SCRAMBLING		0x1585
#define PA_PEERSCRAMBLING	0x155B
#define PA_TxHsG1SyncLength	0x1552
#define PA_TxHsG2SyncLength	0x1554
#define PA_TxHsG3SyncLength	0x1556
#define PA_Local_TX_LCC_Enable	0x155E

#define SCREN			0x1

/*
 * DME Attributes
 */
#define DME_FC0PROTECTIONTIMEOUTVAL	0xD041
#define DME_TC0REPLAYTIMEOUTVAL		0xD042
#define DME_AFC0REQTIMEOUTVAL		0xD043

struct ufs_tegra_soc_data {
	u8 chip_id;
};

struct ufs_tegra_host {
	struct ufs_hba *hba;
	bool is_lane_clks_enabled;
	bool x2config;
	bool enable_hs_mode;
	bool enable_38mhz_clk;
	bool enable_ufs_provisioning;
	bool enable_auto_hibern8;
	u32 max_hs_gear;
	bool mask_fast_auto_mode;
	bool mask_hs_mode_b;
	bool configure_uphy_pll3;
	u32 max_pwm_gear;
	enum ufs_state ufshc_state;
	void *mphy_context;
	void __iomem *mphy_l0_base;
	void __iomem *mphy_l1_base;
	void __iomem *ufs_aux_base;
	void __iomem *ufs_virtualization_base;
	struct reset_control *ufs_rst;
	struct reset_control *ufs_axi_m_rst;
	struct reset_control *ufshc_lp_rst;
	struct reset_control *mphy_l0_rx_rst;
	struct reset_control *mphy_l0_tx_rst;
	struct reset_control *mphy_l1_rx_rst;
	struct reset_control *mphy_l1_tx_rst;
	struct reset_control *mphy_clk_ctl_rst;
	struct clk *mphy_core_pll_fixed;
	struct clk *mphy_l0_tx_symb;
	struct clk *mphy_tx_1mhz_ref;
	struct clk *mphy_l0_rx_ana;
	struct clk *mphy_l0_rx_symb;
	struct clk *mphy_l0_tx_ls_3xbit;
	struct clk *mphy_l0_rx_ls_bit;
	struct clk *mphy_l1_rx_ana;
	struct clk *mphy_l0_tx_2x_symb;
	struct clk *mphy_tx_hs_symb_div;
	struct clk *mphy_tx_hs_mux_symb_div;
	struct clk *mphy_rx_hs_symb_div;
	struct clk *mphy_rx_hs_mux_symb_div;
	struct clk *mphy_force_ls_mode;
	struct clk *ufshc_parent;
	struct clk *ufsdev_parent;
	struct clk *ufshc_clk;
	struct clk *ufshc_clk_div;
	struct clk *ufsdev_ref_clk;
	struct clk *ufsdev_osc;
	struct clk *ufs_uphy_pll3;
	struct clk *pllrefe_clk;
	struct clk *mphy_l0_uphy_tx_fifo;
	struct clk *isc_cpu;
	struct clk *utmi_pll1;
	struct regulator *vddio_ufs;
	struct regulator *vddio_ufs_ap;
	struct pinctrl *ufs_pinctrl;
	struct pinctrl_state *dpd_enable;
	struct pinctrl_state *dpd_disable;
	u32 vs_burst;
	/* UFS tegra deviations from standard UFSHCI spec. */
	unsigned int nvquirks;
	bool wake_enable_failed;
	bool enable_scramble;
	u32 ref_clk_freq;
	struct ufs_tegra_soc_data *soc;
	u32 streamid;
#if defined(CONFIG_TEGRA_PROD_LEGACY)
	struct tegra_prod *prod_list;
#endif

#ifdef CONFIG_DEBUG_FS
	u32 refclk_value;
	long program_refclk;
	u32 bootlun_en_id;
	long program_bootlun_en_id;
	u32 boot_enable;
	u32 descr_access_en;
	u8 enable_shared_wb;
	u8 *lun_desc_buf;
	long program_lun;
#endif
};

extern struct ufs_hba_variant_ops ufs_hba_tegra_vops;

static inline u32 mphy_readl(void __iomem *mphy_base, u32 offset)
{
	u32 val;

	if (tegra_sku_info.platform == TEGRA_PLATFORM_VSP)
		return 0;

	val = readl(mphy_base + offset);
	return val;
}

static inline void mphy_writel(void __iomem *mphy_base, u32 val, u32 offset)
{
	if (tegra_sku_info.platform == TEGRA_PLATFORM_VSP)
		return;
	writel(val, mphy_base + offset);
}

static inline void mphy_update(void __iomem *mphy_base, u32 val, u32 offset)
{
	u32 update_val;

	update_val = mphy_readl(mphy_base, offset);
	update_val |= val;
	mphy_writel(mphy_base, update_val, offset);
}

static inline void mphy_clear_bits(void __iomem *mphy_base, u32 val, u32 offset)
{
	u32 update_val;

	update_val = mphy_readl(mphy_base, offset);
	update_val &= ~val;
	mphy_writel(mphy_base, update_val, offset);
}

static inline u32 ufs_aux_readl(void __iomem *ufs_aux_base, u32 offset)
{
	u32 val;

	val = readl(ufs_aux_base + offset);
	return val;
}

static inline void ufs_aux_writel(void __iomem *ufs_aux_base, u32 val,
				  u32 offset)
{
	writel(val, ufs_aux_base + offset);
}

static inline void ufs_aux_update(void __iomem *ufs_aux_base, u32 val,
				  u32 offset)
{
	u32 update_val;

	update_val = ufs_aux_readl(ufs_aux_base, offset);
	update_val |= val;
	ufs_aux_writel(ufs_aux_base, update_val, offset);
}

static inline void ufs_aux_clear_bits(void __iomem *ufs_aux_base, u32 val,
				      u32 offset)
{
	u32 update_val;

	update_val = ufs_aux_readl(ufs_aux_base, offset);
	update_val &= ~val;
	ufs_aux_writel(ufs_aux_base, update_val, offset);
}

static inline void ufs_save_regs(void __iomem *reg_base, u32 *save_addr,
				 u16 reg_array[], u32 no_of_regs)
{
	u32 regs;
	u32 *dest = save_addr;

	for (regs = 0; regs < no_of_regs; ++regs, ++dest)
		*dest = readl(reg_base + (u32)reg_array[regs]);
}

static inline void ufs_restore_regs(void __iomem *reg_base, u32 *save_addr,
				    u16 reg_array[], u32 no_of_regs)
{
	u32 regs;
	u32 *src = save_addr;

	for (regs = 0; regs < no_of_regs; ++regs, ++src)
		writel(*src, reg_base + (u32)reg_array[regs]);
}

#endif
