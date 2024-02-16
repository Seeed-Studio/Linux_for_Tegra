/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/version.h>
#include <linux/sys_soc.h>
#include <soc/tegra/fuse.h>

#define FUSE_SKU_INFO	0x10

static inline u32 tegra_get_sku_id(void)
{
	if (!tegra_sku_info.sku_id)
		tegra_fuse_readl(FUSE_SKU_INFO, &tegra_sku_info.sku_id);

	return tegra_sku_info.sku_id;
}

static inline bool tegra_platform_is_silicon(void)
{
	const struct soc_device_attribute tegra_soc_attrs[] = {
		{ .revision = "Silicon*" },
		{/* sentinel */}
	};

	if (soc_device_match(tegra_soc_attrs))
		return true;

	return false;
}

static inline bool tegra_platform_is_sim(void)
{
	return !tegra_platform_is_silicon();
}

static inline bool tegra_platform_is_fpga(void)
{
	const struct soc_device_attribute tegra_soc_attrs[] = {
		{ .revision = "*FPGA" },
		{/* sentinel */}
	};

	if (soc_device_match(tegra_soc_attrs))
		return true;

	return false;
}

static inline bool tegra_platform_is_vdk(void)
{
	const struct soc_device_attribute tegra_soc_attrs[] = {
		{ .revision = "VDK" },
		{/* sentinel */}
	};

	if (soc_device_match(tegra_soc_attrs))
		return true;

	return false;
}

/* OOT implementation of upstream API tegra_get_chip_id() */
static inline u8 __tegra_get_chip_id(void)
{
	const struct soc_device_attribute tegra194_soc_attrs[] = {
		{ .soc_id = "25" }, /* 0x19 */
		{/* sentinel */}
	};
	const struct soc_device_attribute tegra234_soc_attrs[] = {
		{ .soc_id = "35" }, /* 0x23 */
		{/* sentinel */}
	};
	const struct soc_device_attribute tegra264_soc_attrs[] = {
		{ .soc_id = "38" }, /* 0x26 */
		{/* sentinel */}
	};

	if (soc_device_match(tegra194_soc_attrs))
		return TEGRA194;

	if (soc_device_match(tegra234_soc_attrs))
		return TEGRA234;

	if (soc_device_match(tegra264_soc_attrs))
		return TEGRA264;

	return 0;
}
