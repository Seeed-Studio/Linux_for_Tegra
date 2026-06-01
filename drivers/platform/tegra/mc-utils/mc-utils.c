// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/export.h>
#include <linux/types.h>
#include <linux/debugfs.h>
#include <linux/clk.h>
#include <linux/platform/tegra/mc_utils.h>
#include <linux/version.h>
#include <soc/tegra/fuse.h>
#include <linux/io.h>
#include <linux/of.h>

#include <soc/tegra/virt/hv-ivc.h>

#define BYTES_PER_CLK_PER_CH	4
#define CH_32			32
#define CH_16			16
#define CH_8			8
#define CH_4			4
#define CH_32_BYTES_PER_CLK	(BYTES_PER_CLK_PER_CH * CH_32)
#define CH_16_BYTES_PER_CLK	(BYTES_PER_CLK_PER_CH * CH_16)
#define CH_8_BYTES_PER_CLK	(BYTES_PER_CLK_PER_CH * CH_8)
#define CH_4_BYTES_PER_CLK	(BYTES_PER_CLK_PER_CH * CH_4)

#define MC_EMEM_ADR_CFG_0 0x54
#define MC_ECC_CONTROL_0 0x1880

#define CH4 0xf
#define CH2 0x3

#define ECC_MASK 0x1 /* 1 = enabled, 0 = disabled */
#define RANK_MASK 0x1 /* 1 = 2-RANK, 0 = 1-RANK */
#define DRAM_MASK 0x3

/* EMC_FBIO_CFG5_0(1:0) : DRAM_TYPE */
#define DRAM_LPDDR4 0
#define DRAM_LPDDR5 1
#define DRAM_DDR3 2

/* BANDWIDTH LATENCY COMPONENTS */
#define SMMU_DISRUPTION_DRAM_CLK_LP4 6003
#define SMMU_DISRUPTION_DRAM_CLK_LP5 9005
#define RING0_DISRUPTION_MC_CLK_LP4 63
#define RING0_DISRUPTION_MC_CLK_LP5 63
#define HUM_DISRUPTION_DRAM_CLK_LP4 1247
#define HUM_DISRUPTION_DRAM_CLK_LP5 4768
#define HUM_DISRUPTION_NS_LP4 1406
#define HUM_DISRUPTION_NS_LP5 1707
#define EXPIRED_ISO_DRAM_CLK_LP4 424
#define EXPIRED_ISO_DRAM_CLK_LP5 792
#define EXPIRED_ISO_NS_LP4 279
#define EXPIRED_ISO_NS_LP5 279
#define REFRESH_RATE_LP4 176
#define REFRESH_RATE_LP5 226
#define PERIODIC_TRAINING_LP4 380
#define PERIODIC_TRAINING_LP5 380
#define CALIBRATION_LP4 30
#define CALIBRATION_LP5 30

struct emc_params {
	u32 rank;
	u32 ecc;
	u32 dram;
};

static struct emc_params emc_param;
static u32 ch_num;

static struct mc_utils_ops *ops;

static unsigned long freq_to_bw(unsigned long freq)
{
	if (ch_num == CH_32)
		return freq * CH_32_BYTES_PER_CLK;

	if (ch_num == CH_16)
		return freq * CH_16_BYTES_PER_CLK;

	if (ch_num == CH_8)
		return freq * CH_8_BYTES_PER_CLK;

	/*4CH and 4CH_ECC*/
	return freq * CH_4_BYTES_PER_CLK;
}

static unsigned long bw_to_freq(unsigned long bw)
{
	if (ch_num == CH_32)
		return (bw + CH_32_BYTES_PER_CLK - 1) / CH_32_BYTES_PER_CLK;

	if (ch_num == CH_16)
		return (bw + CH_16_BYTES_PER_CLK - 1) / CH_16_BYTES_PER_CLK;

	if (ch_num == CH_8)
		return (bw + CH_8_BYTES_PER_CLK - 1) / CH_8_BYTES_PER_CLK;

	/*4CH and 4CH_ECC*/
	return (bw + CH_4_BYTES_PER_CLK - 1) / CH_4_BYTES_PER_CLK;
}

static unsigned long emc_freq_to_bw_t23x(unsigned long freq)
{
	return freq_to_bw(freq);
}

unsigned long emc_freq_to_bw(unsigned long freq)
{
	if (ops && ops->emc_freq_to_bw)
		return ops->emc_freq_to_bw(freq);

	return -ENODEV;
}
EXPORT_SYMBOL(emc_freq_to_bw);

static unsigned long emc_bw_to_freq_t23x(unsigned long bw)
{
	return bw_to_freq(bw);
}

unsigned long emc_bw_to_freq(unsigned long bw)
{
	if (ops && ops->emc_bw_to_freq)
		return ops->emc_bw_to_freq(bw);

	return -ENODEV;
}
EXPORT_SYMBOL(emc_bw_to_freq);


static u8 get_dram_num_channels_t23x(void)
{
	return ch_num;
}

u8 get_dram_num_channels(void)
{
	if (ops && ops->get_dram_num_channels)
		return ops->get_dram_num_channels();

	return -ENODEV;
}
EXPORT_SYMBOL(get_dram_num_channels);

#if defined(CONFIG_DEBUG_FS)
static void tegra_mc_utils_debugfs_init(void)
{
	struct dentry *tegra_mc_debug_root = NULL;

	tegra_mc_debug_root = debugfs_create_dir("tegra_mc_utils", NULL);
	if (IS_ERR_OR_NULL(tegra_mc_debug_root)) {
		pr_err("tegra_mc: Unable to create debugfs dir\n");
		return;
	}

	debugfs_create_u32("num_channel", 0444, tegra_mc_debug_root,
			&ch_num);
}
#endif

static u32 get_dram_dt_prop(struct device_node *np, const char *prop)
{
	u32 val;
	int ret;

	ret = of_property_read_u32(np, prop, &val);
	if (ret) {
		pr_err("failed to read %s\n", prop);
		return ~0U;
	}

	return val;
}

static struct mc_utils_ops mc_utils_t23x_ops = {
	.emc_freq_to_bw = emc_freq_to_bw_t23x,
	.emc_bw_to_freq = emc_bw_to_freq_t23x,
	.get_dram_num_channels = get_dram_num_channels_t23x,
};

static struct mc_utils_ops mc_utils_t26x_ops = {
	.emc_freq_to_bw = emc_freq_to_bw_t23x,
	.emc_bw_to_freq = emc_bw_to_freq_t23x,
	.get_dram_num_channels = get_dram_num_channels_t23x,
};

static int __init tegra_mc_utils_init_t26x(void)
{
	u32 ch;
	void __iomem *mcb_base;
	u64 mcb_base_reg = 0x8108020000;
	u64 mcb_size_reg = 0x20000;
	u32 mc_emem_adr_cfg_channel_enable_0_reg = 0x8870;
	u32 channel_mask = 0xffffffff;

	if (!is_tegra_hypervisor_mode()) {
		mcb_base = ioremap(mcb_base_reg, mcb_size_reg);
		if (!mcb_base) {
			pr_err("Failed to ioremap\n");
			return -ENOMEM;
		}

		ch = readl(mcb_base + mc_emem_adr_cfg_channel_enable_0_reg);
		ch &= channel_mask;
		iounmap(mcb_base);

		while (ch) {
			if (ch & 1)
				ch_num++;
			ch >>= 1;
		}
	} else {
		struct device_node *np = of_find_compatible_node(NULL, NULL,
						"nvidia,tegra264-mc-utils");

		if (!np) {
			pr_err("mc-utils: nvidia,tegra264-mc-utils node not found\n");
			return -ENODEV;
		}

		ch_num = get_dram_dt_prop(np, "dram_channels");
	}

#if defined(CONFIG_DEBUG_FS)
	tegra_mc_utils_debugfs_init();
#endif
	return 0;
}

static int __init tegra_mc_utils_init_t23x(void)
{
	u32 dram, ch, ecc, rank;
	void __iomem *emc_base;
	void __iomem *mcb_base;
	u32 emc_base_reg = 0x02c60000;
	u32 emc_fbio_cfg5_0 = 0x100C;
	u32 mcb_base_reg = 0x02c10000;
	u32 mc_size_reg = 0x10000;
	u32 mc_emem_adr_cfg_channel_enable_0_reg = 0xdf8;
	u32 channel_mask = 0xffff;

	if (!is_tegra_hypervisor_mode()) {
		emc_base = ioremap(emc_base_reg, 0x00010000);
		dram = readl(emc_base + emc_fbio_cfg5_0) & DRAM_MASK;
		mcb_base = ioremap(mcb_base_reg, mc_size_reg);
		if (!mcb_base) {
			pr_err("Failed to ioremap\n");
			return -ENOMEM;
		}

		ch = readl(mcb_base + mc_emem_adr_cfg_channel_enable_0_reg);
		ch &= channel_mask;
		ecc = readl(mcb_base + MC_ECC_CONTROL_0) & ECC_MASK;

		rank = readl(mcb_base + MC_EMEM_ADR_CFG_0) & RANK_MASK;

		iounmap(emc_base);
		iounmap(mcb_base);

		while (ch) {
			if (ch & 1)
				ch_num++;
			ch >>= 1;
		}
	} else {
		struct device_node *np = of_find_compatible_node(NULL, NULL, "nvidia,tegra234-mc");

		if (!np) {
			pr_err("mc-utils: nvidia,tegra234-mc node not found \n");
			return -ENODEV;
		}

		ecc = get_dram_dt_prop(np, "dram_ecc");
		rank = get_dram_dt_prop(np, "dram_rank");
		dram = get_dram_dt_prop(np, "dram_lpddr");
		ch_num = get_dram_dt_prop(np, "dram_channels");
	}

	emc_param.ecc = ecc;
	emc_param.rank = rank;
	emc_param.dram = dram;

#if defined(CONFIG_DEBUG_FS)
	tegra_mc_utils_debugfs_init();
#endif
	return 0;
}

static int __init tegra_mc_utils_init(void)
{
	if (of_machine_is_compatible("nvidia,tegra234")) {
		ops = &mc_utils_t23x_ops;
		return tegra_mc_utils_init_t23x();
	}

	if (of_machine_is_compatible("nvidia,tegra264") ||
		of_machine_is_compatible("nvidia,t264sim")) {
		ops = &mc_utils_t26x_ops;
		return tegra_mc_utils_init_t26x();
	}
	pr_debug("%s: Not able to find SOC DT node\n", __func__);

	/* Do not fail driver loading for dependent drivers */
	return 0;
}
module_init(tegra_mc_utils_init);

static void __exit tegra_mc_utils_exit(void)
{
}
module_exit(tegra_mc_utils_exit);

MODULE_DESCRIPTION("MC utility provider module");
MODULE_AUTHOR("Puneet Saxena <puneets@nvidia.com>");
MODULE_AUTHOR("Ashish Mhetre <amhetre@nvidia.com>");
MODULE_LICENSE("GPL v2");
