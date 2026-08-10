// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * Crypto driver for NVIDIA Security Engine for block cipher operations.
 */

#include <linux/bitfield.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

#include "tegra-se.h"

#define KDS_ALLOC_MUTEX			0
#define KDS_ALLOC_RGN_ATTR0		0x4
#define KDS_ALLOC_RGN_ATTR1		0x8
#define KDS_ALLOC_OP_TRIG		0x18
#define KDS_ALLOC_OP_STATUS		0x1c

#define KDS_MUTEX_MST_ID(x)		FIELD_PREP(GENMASK(13, 8), x)
#define KDS_MUTEX_BUSY			BIT(4)
#define KDS_MUTEX_OP(x)			FIELD_PREP(BIT(0), x)
#define KDS_MUTEX_REQ			KDS_MUTEX_OP(1)
#define KDS_MUTEX_RELEASE		KDS_MUTEX_OP(0)

#define KDS_RGN_ATTR_OWNER(x)		FIELD_PREP(GENMASK(31, 24), x)
#define KDS_RGN_ATTR_TYPE(x)		FIELD_PREP(GENMASK(21, 20), x)
#define KDS_RGN_ATTR_TYPE_NORMAL	KDS_RGN_ATTR_TYPE(0)

#define KDS_RGN_ATTR_MAX_KSIZE_256	FIELD_PREP(GENMASK(17, 16), 1)
#define KDS_RGN_ATTR_NUM_KEYS(x)	FIELD_PREP(GENMASK(15, 0), x)

#define KDS_ALLOC_OP_STATUS_FIELD(x)	FIELD_PREP(GENMASK(1, 0), x)
#define KDS_ALLOC_OP_IDLE		KDS_ALLOC_OP_STATUS_FIELD(0)
#define KDS_ALLOC_OP_BUSY		KDS_ALLOC_OP_STATUS_FIELD(1)
#define KDS_ALLOC_OP_PASS		KDS_ALLOC_OP_STATUS_FIELD(2)
#define KDS_ALLOC_OP_FAIL		KDS_ALLOC_OP_STATUS_FIELD(3)
#define KDS_ALLOC_RGN_ID_MASK		GENMASK(14, 4)

#define SE_KSLT_KEY_ID_MASK		GENMASK(15, 0)
#define SE_KSLT_REGION_ID_MASK		GENMASK(25, 16)

#define SE_KSLT_TABLE_ID_MASK		GENMASK(31, 26)
#define SE_KSLT_TABLE_ID(x)		FIELD_PREP(SE_KSLT_TABLE_ID_MASK, x)
#define SE_KSLT_TABLE_ID_GLOBAL		SE_KSLT_TABLE_ID(48)

#define KDS_TIMEOUT			100000 /* 100 msec */
#define KDS_MAX_KEYID			63
#define KDS_ID_VALID_MASK		GENMASK(KDS_MAX_KEYID, 0)
#define TEGRA_GPSE			3

static u32 kds_region_id;
static u64 kds_keyid = BIT(0);

struct tegra_kds {
	struct device *dev;
	void __iomem *base;
	u32 owner;
	u32 id;
};

static u16 tegra_kds_keyid_alloc(void)
{
	u16 keyid;

	/* Check if all key slots are full */
	if (kds_keyid == GENMASK(KDS_MAX_KEYID, 0))
		return 0;

	keyid = ffz(kds_keyid);
	kds_keyid |= BIT(keyid);

	return keyid;
}

static void tegra_kds_keyid_free(u32 id)
{
	kds_keyid &= ~(BIT(id));
}

static inline void kds_writel(struct tegra_kds *kds, unsigned int offset,
			     unsigned int val)
{
	writel_relaxed(val, kds->base + offset);
}

static inline u32 kds_readl(struct tegra_kds *kds, unsigned int offset)
{
	return readl_relaxed(kds->base + offset);
}

static int kds_mutex_lock(struct tegra_kds *kds)
{
	u32 val;
	int ret;

	ret = readl_relaxed_poll_timeout(kds->base + KDS_ALLOC_MUTEX,
			val, !(val & KDS_MUTEX_BUSY),
			10, KDS_TIMEOUT);

	if (ret)
		return ret;

	val = KDS_MUTEX_MST_ID(TEGRA_GPSE) |
		 KDS_MUTEX_REQ;

	kds_writel(kds, KDS_ALLOC_MUTEX, val);

	return 0;
}

static void kds_mutex_unlock(struct tegra_kds *kds)
{
	u32 val;

	val = KDS_MUTEX_MST_ID(TEGRA_GPSE) |
		 KDS_MUTEX_RELEASE;

	kds_writel(kds, KDS_ALLOC_MUTEX, val);
}

static int tegra_kds_region_setup(struct tegra_kds *kds)
{
	u32 val, region_attr;
	int ret;

	region_attr = KDS_RGN_ATTR_OWNER(TEGRA_GPSE) |
		      KDS_RGN_ATTR_TYPE_NORMAL |
		      KDS_RGN_ATTR_MAX_KSIZE_256 |
		      KDS_RGN_ATTR_NUM_KEYS(64);

	ret = kds_mutex_lock(kds);
	if (ret)
		return ret;

	kds_writel(kds, KDS_ALLOC_RGN_ATTR0, region_attr);
	kds_writel(kds, KDS_ALLOC_RGN_ATTR1, BIT(TEGRA_GPSE));
	kds_writel(kds, KDS_ALLOC_OP_TRIG, 1);

	ret = readl_relaxed_poll_timeout(kds->base + KDS_ALLOC_OP_STATUS,
			val, !(val & KDS_ALLOC_OP_BUSY), 10, KDS_TIMEOUT);

	if (ret) {
		dev_err(kds->dev, "Region allocation timed out val\n");
		goto out;
	}

	if (KDS_ALLOC_OP_STATUS_FIELD(val) == KDS_ALLOC_OP_FAIL) {
		dev_err(kds->dev, "Region allocation failed\n");
		ret = -EINVAL;
		goto out;
	}

	kds->id = FIELD_GET(KDS_ALLOC_RGN_ID_MASK, val);
	kds_region_id = kds->id;
	dev_info(kds->dev, "Allocated Global Key ID table with ID %#x\n", kds->id);

out:
	kds_mutex_unlock(kds);

	return ret;
}

bool tegra_key_in_kds(u32 keyid)
{
	if (!((keyid & SE_KSLT_TABLE_ID_MASK) == SE_KSLT_TABLE_ID_GLOBAL))
		return false;

	return ((BIT(keyid & SE_KSLT_KEY_ID_MASK) & KDS_ID_VALID_MASK) &&
		(BIT(keyid & SE_KSLT_KEY_ID_MASK) & kds_keyid));
}
EXPORT_SYMBOL(tegra_key_in_kds);

u32 tegra_kds_get_id(void)
{
	u32 kds_id, keyid;

	keyid = tegra_kds_keyid_alloc();
	if (!keyid)
		return -ENOMEM;

	kds_id = SE_KSLT_TABLE_ID_GLOBAL |
		 FIELD_PREP(SE_KSLT_REGION_ID_MASK, kds_region_id) |
		 FIELD_PREP(SE_KSLT_KEY_ID_MASK, keyid);

	return kds_id;
}
EXPORT_SYMBOL(tegra_kds_get_id);

void tegra_kds_free_id(u32 keyid)
{
	tegra_kds_keyid_free(keyid & 0xff);
}
EXPORT_SYMBOL(tegra_kds_free_id);

static int tegra_kds_probe(struct platform_device *pdev)
{
	struct tegra_kds *kds;

	kds = devm_kzalloc(&pdev->dev, sizeof(struct tegra_kds), GFP_KERNEL);
	if (!kds)
		return -ENOMEM;

	kds->dev = &pdev->dev;
	kds->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(kds->base))
		return PTR_ERR(kds->base);

	return tegra_kds_region_setup(kds);
}

static const struct of_device_id tegra_kds_of_match[] = {
	{
		.compatible = "nvidia,tegra264-kds",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_kds_of_match);

static struct platform_driver tegra_kds_driver = {
	.driver = {
		.name	= "tegra-kds",
		.of_match_table = tegra_kds_of_match,
	},
	.probe		= tegra_kds_probe,
};

module_platform_driver(tegra_kds_driver);

MODULE_DESCRIPTION("NVIDIA Tegra Key Distribution System Driver");
MODULE_AUTHOR("Akhil R <akhilrajeev@nvidia.com>");
MODULE_LICENSE("GPL");
