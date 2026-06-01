// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <linux/io.h>

#include <aon.h>

#define SHRD_MBOX_OFFSET 0x8000
#define SHRD_SEM_OFFSET	 0x10000
#define SHRD_SEM_SET	 0x4u
#define SHRD_SEM_CLR	 0x8u
#define AON_SS_MAX	 4
#define AON_SM_MAX	 8
#define MBOX_TAG         BIT(32)

static void __iomem *tegra_aon_hsp_sm_reg(const struct tegra_aon *aon, u32 sm)
{
	return aon_reg(aon, hsp_sm_base_r()) + (SHRD_MBOX_OFFSET * sm);
}

void tegra_aon_hsp_sm_write(const struct tegra_aon *aon, u32 sm, u32 value)
{
	void __iomem *reg;

	WARN_ON(sm >= AON_SM_MAX);
	reg = tegra_aon_hsp_sm_reg(aon, sm);

	writel(MBOX_TAG | value, reg);
}

static void __iomem *tegra_aon_hsp_ss_reg(const struct tegra_aon *aon, u32 ss)
{
	return aon_reg(aon, hsp_ss_base_r()) + (SHRD_SEM_OFFSET * ss);
}

u32 tegra_aon_hsp_ss_status(const struct tegra_aon *aon, u32 ss)
{
	void __iomem *reg;

	WARN_ON(ss >= AON_SS_MAX);
	reg = tegra_aon_hsp_ss_reg(aon, ss);

	return readl(reg);
}

void tegra_aon_hsp_ss_set(const struct tegra_aon *aon, u32 ss, u32 bits)
{
	void __iomem *reg;

	WARN_ON(ss >= AON_SS_MAX);
	reg = tegra_aon_hsp_ss_reg(aon, ss);

	writel(bits, reg + SHRD_SEM_SET);
}

void tegra_aon_hsp_ss_clr(const struct tegra_aon *aon, u32 ss, u32 bits)
{
	void __iomem *reg;

	WARN_ON(ss >= AON_SS_MAX);
	reg = tegra_aon_hsp_ss_reg(aon, ss);

	writel(bits, reg + SHRD_SEM_CLR);
}
