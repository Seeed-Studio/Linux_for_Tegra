// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2024 NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#define	CENTRAL_ACTMON_CTRL_REG			0x0
#define CENTRAL_ACTMON_MC_ALL_AVG_COUNT_REG	0x124
#define CENTRAL_ACTMON_CTRL_SOURCE(v)		((v & (0x3 << 8)) >> 8)
#define CENTRAL_ACTMON_CTRL_SAMPLE_TICK(v)	((v & (0x1 << 10)) >> 10)
#define CENTRAL_ACTMON_CTRL_SAMPLE_PERIOD(v)	((v & (0xff << 0)) >> 0)

struct central_actmon {
	struct device *dev;
	struct clk *clk;
	unsigned long rate;
	void __iomem *regs;
	struct dentry *debugfs;
};

static u32 cactmon_readl(struct central_actmon *cactmon, u32 offset)
{
	return readl(cactmon->regs+offset);
}

static u32 __central_actmon_sample_period_get(struct central_actmon *cactmon)
{
	u32 val, source, sample_tick, sample_period;

	val = cactmon_readl(cactmon, CENTRAL_ACTMON_CTRL_REG);

	source = CENTRAL_ACTMON_CTRL_SOURCE(val);
	sample_period = CENTRAL_ACTMON_CTRL_SAMPLE_PERIOD(val) + 1;

	if (source == 1) {
		sample_period *= 1;
	} else if (source == 0) {
		sample_period *= 1000;
	} else {
		sample_tick = CENTRAL_ACTMON_CTRL_SAMPLE_TICK(val);
		sample_period *= sample_tick ? 65536 : 256;
		sample_period = (sample_period * 1000) / (cactmon->rate / 1000);
	}

	return sample_period;
}

static int central_actmon_sample_period_get(void *data, u64 *val)
{
	struct central_actmon *cactmon = (struct central_actmon *)data;

	*val = (u64) __central_actmon_sample_period_get(cactmon);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(central_actmon_sample_period_fops,
		central_actmon_sample_period_get, NULL,
		"%lld\n");

static int central_actmon_mc_all_get(void *data, u64 *val)
{
	struct central_actmon *cactmon = (struct central_actmon *)data;
	u32 sample_period_usec, mc_all_actives;

	sample_period_usec = __central_actmon_sample_period_get(cactmon);

	mc_all_actives = cactmon_readl(cactmon, CENTRAL_ACTMON_MC_ALL_AVG_COUNT_REG);

	*val = (u64)mc_all_actives * 1000 / sample_period_usec;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(central_actmon_mc_all_fops,
		central_actmon_mc_all_get, NULL,
		"%lld\n");

static void central_actmon_debugfs_init(struct central_actmon *cactmon)
{
	cactmon->debugfs = debugfs_create_dir("cactmon", NULL);

	debugfs_create_file("sample_period_usec", 0444, cactmon->debugfs, cactmon,
			    &central_actmon_sample_period_fops);
	debugfs_create_file("mc_all", 0444, cactmon->debugfs, cactmon,
			    &central_actmon_mc_all_fops);
}

static int central_actmon_probe(struct platform_device *pdev)
{
	struct central_actmon *cactmon;

	cactmon = devm_kzalloc(&pdev->dev, sizeof(*cactmon), GFP_KERNEL);
	if (!cactmon)
		return -ENOMEM;

	cactmon->regs = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(cactmon->regs))
		return PTR_ERR(cactmon->regs);

	cactmon->clk = devm_clk_get(&pdev->dev, "actmon");
	if (IS_ERR(cactmon->clk))
		return dev_err_probe(&pdev->dev,
				     PTR_ERR(cactmon->clk),
				     "failed to get actmon clock\n");
	cactmon->rate = clk_get_rate(cactmon->clk);

	cactmon->dev = &pdev->dev;
	platform_set_drvdata(pdev, cactmon);

	central_actmon_debugfs_init(cactmon);

	return 0;
}

static int central_actmon_remove(struct platform_device *pdev)
{
	struct central_actmon *cactmon = platform_get_drvdata(pdev);

	debugfs_remove_recursive(cactmon->debugfs);

	return 0;
}


static const struct of_device_id central_actmon_of_match[] = {
	{ .compatible = "nvidia,tegra234-cactmon-mc-all", .data = NULL, },
	{},
};
MODULE_DEVICE_TABLE(of, central_actmon_of_match);

static struct platform_driver central_actmon_driver = {
	.probe		= central_actmon_probe,
	.remove		= central_actmon_remove,
	.driver	= {
		.name	= "tegra-cactmon-mc-all",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(central_actmon_of_match),
	},
};
module_platform_driver(central_actmon_driver);

MODULE_DESCRIPTION("Tegra MC_ALL Central Activity Monitor");
MODULE_AUTHOR("Johnny Liu <johnliu@nvidia.com>");
MODULE_LICENSE("GPL v2");
