// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION.  All rights reserved.
 */

#include <nvidia/conftest.h>

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/devfreq.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#if defined(NV_TEGRA264_BWMGR_DEBUG_MACRO_PRESENT)
#include <linux/tegra264-bwmgr.h>
#endif
#include <soc/tegra/bpmp.h>

#include <devfreq/governor.h>

#define DEVFREQ_GOV_BWMGR "bpmp-bwmgr"

struct tegra_bpmp_bwmgr {
	struct device *dev;
	struct clk *clk;
	struct devfreq *devfreq;
	struct devfreq_dev_profile *devfreq_profile;
	struct notifier_block max_freq_nb;
	struct tegra_bpmp *bpmp;
	bool mrq_valid;
	bool enabled;
};

#if defined(NV_TEGRA264_BWMGR_DEBUG_MACRO_PRESENT)
static int tegra_bpmp_bwmgr_max_freq_notifier(struct notifier_block *nb, unsigned long action, void *ptr)
{
	struct tegra_bpmp_bwmgr *bwmgr = container_of(nb, struct tegra_bpmp_bwmgr, max_freq_nb);
	struct mrq_bwmgr_int_request req = { 0 };
	struct mrq_bwmgr_int_response resp = { 0 };
	struct tegra_bpmp_message msg = { 0 };
	struct dev_pm_qos *qos = bwmgr->dev->power.qos;
	struct pm_qos_constraints *constraint = &qos->freq.max_freq;
	unsigned long max_rate = (unsigned long)constraint->target_value * 1000;
	unsigned long rate = clk_round_rate(bwmgr->clk, max_rate);
	int err;

	req.cmd = CMD_BWMGR_INT_CAP_SET;
	req.bwmgr_cap_set_req.rate = rate;

	memset(&msg, 0, sizeof(struct tegra_bpmp_message));
	msg.mrq = MRQ_BWMGR_INT;
	msg.tx.data = &req;
	msg.tx.size = sizeof(struct mrq_bwmgr_int_request);
	msg.rx.data = &resp;
	msg.rx.size = sizeof(struct mrq_bwmgr_int_response);

	err = tegra_bpmp_transfer(bwmgr->bpmp, &msg);
	if (err < 0) {
		dev_err(bwmgr->dev, "BPMP transfer failed: %d\n", err);
		return err;
	}

	if (msg.rx.ret == 0) {
		dev_info(bwmgr->dev, "cap EMC frequency to %luHz\n", rate);
	} else {
		dev_err(bwmgr->dev, "failed to cap EMC frequency with %luHz: %d\n", rate, msg.rx.ret);
		return -EINVAL;
	}

	return err;
}
#endif

static int devfreq_gov_bwmgr_target_freq(struct devfreq *df, unsigned long *freq)
{
	*freq = DEVFREQ_MIN_FREQ;
	return 0;
}

static int devfreq_gov_bwmgr_event_handler(struct devfreq *df, unsigned int event, void *data)
{
	int ret = 0;

	if (event == DEVFREQ_GOV_START) {
		mutex_lock(&df->lock);
		ret = update_devfreq(df);
		mutex_unlock(&df->lock);
	}

	return ret;
}

static struct devfreq_governor devfreq_gov_bwmgr = {
	.name = DEVFREQ_GOV_BWMGR,
	.flags = DEVFREQ_GOV_FLAG_IMMUTABLE,
	.get_target_freq = devfreq_gov_bwmgr_target_freq,
	.event_handler = devfreq_gov_bwmgr_event_handler,
};

#if defined(NV_TEGRA264_BWMGR_DEBUG_MACRO_PRESENT)
static int tegra_bpmp_bwmgr_devfreq_target(struct device *dev, unsigned long *freq, u32 flags)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct tegra_bpmp_bwmgr *bwmgr = platform_get_drvdata(pdev);
	struct mrq_bwmgr_int_request bwmgr_req = { 0 };
	struct mrq_bwmgr_int_request bwmgr_resp = { 0 };
	struct tegra_bpmp_message msg = { 0 };
	u32 khz;
	int err;

	// This will be called once when calling devfreq_add_device. Need to
	// check early return if bwmgr is not supported.
	if (!bwmgr->mrq_valid || !bwmgr->enabled)
		return 0;

	khz = *freq / 1000;
	bwmgr_req.cmd = CMD_BWMGR_INT_CALC_AND_SET;
	bwmgr_req.bwmgr_calc_set_req.mc_floor = khz;
	bwmgr_req.bwmgr_calc_set_req.floor_unit = BWMGR_INT_UNIT_KHZ;
	bwmgr_req.bwmgr_calc_set_req.client_id = TEGRA264_BWMGR_DEBUG;

	msg.mrq = MRQ_BWMGR_INT;
	msg.tx.data = &bwmgr_req;
	msg.tx.size = sizeof(bwmgr_req);
	msg.rx.data = &bwmgr_resp;
	msg.rx.size = sizeof(bwmgr_resp);

	err = tegra_bpmp_transfer(bwmgr->bpmp, &msg);
	if (err < 0) {
		dev_err(dev, "BPMP transfer failed: %d\n", err);
		return err;
	}

	if (msg.rx.ret == 0) {
		dev_info(dev, "set EMC floor frequency to %uKHz\n", khz);
	} else {
		dev_err(dev, "failed to set floor frequency with %uKHz: %d\n", khz, msg.rx.ret);
		return -EINVAL;
	}

	return 0;
}

static int tegra_bpmp_bwmgr_devfreq_cur_freq(struct device *dev, unsigned long *freq)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct tegra_bpmp_bwmgr *bwmgr = platform_get_drvdata(pdev);

	*freq = clk_get_rate(bwmgr->clk);

	return 0;
}
#endif

static int tegra_bpmp_bwmgr_devfreq_register(struct tegra_bpmp_bwmgr *bwmgr)
{
#if defined(NV_TEGRA264_BWMGR_DEBUG_MACRO_PRESENT)
	static const struct attribute min_attr = { .name = "min_freq" };
	static const struct attribute max_attr = { .name = "max_freq" };
	unsigned long max_rate = clk_round_rate(bwmgr->clk, ULONG_MAX);
	unsigned long min_rate = clk_round_rate(bwmgr->clk, 0);
	unsigned long rate = min_rate;
	struct kobject *kobj = NULL;
	int ret = 0;

	bwmgr->devfreq_profile = devm_kzalloc(bwmgr->dev, sizeof(*bwmgr->devfreq_profile), GFP_KERNEL);
	if (!bwmgr->devfreq_profile)
		return -ENOMEM;

	do {
		dev_pm_opp_add(bwmgr->dev, rate, 0);
		if (rate == max_rate)
			break;

		rate = clk_round_rate(bwmgr->clk, rate + 1);
	} while (rate <= max_rate);

	bwmgr->devfreq_profile->target = tegra_bpmp_bwmgr_devfreq_target;
	bwmgr->devfreq_profile->initial_freq = clk_get_rate(bwmgr->clk);
	bwmgr->devfreq_profile->get_cur_freq = tegra_bpmp_bwmgr_devfreq_cur_freq;
	bwmgr->devfreq = devm_devfreq_add_device(bwmgr->dev,
						 bwmgr->devfreq_profile,
						 DEVFREQ_GOV_BWMGR, NULL);
	if (IS_ERR(bwmgr->devfreq)) {
		ret = PTR_ERR(bwmgr->devfreq);
		goto devfreq_add_dev_err;
	}

	if (!bwmgr->mrq_valid || !bwmgr->enabled) {
		kobj = &bwmgr->devfreq->dev.kobj;

		ret = sysfs_chmod_file(kobj, &min_attr, 0444);
		if (ret)
			dev_warn(bwmgr->dev, "fail to update min_freq as read-only\n");

		ret = sysfs_chmod_file(kobj, &max_attr, 0444);
		if (ret)
			dev_warn(bwmgr->dev, "fail to update max_freq as read-only\n");
	}

	bwmgr->max_freq_nb.notifier_call = tegra_bpmp_bwmgr_max_freq_notifier;
	ret = dev_pm_qos_add_notifier(bwmgr->dev,
				      &bwmgr->max_freq_nb,
				      DEV_PM_QOS_MAX_FREQUENCY);
	if (ret < 0)
		goto devfreq_add_notifier_err;

	return 0;

devfreq_add_notifier_err:
	devm_devfreq_remove_device(bwmgr->dev, bwmgr->devfreq);
devfreq_add_dev_err:
	dev_pm_opp_remove_all_dynamic(bwmgr->dev);
	kfree(bwmgr->devfreq_profile);
	return ret;
#else
	return -EOPNOTSUPP;
#endif
}

static void tegra_bpmp_bwmgr_devfreq_unregister(struct tegra_bpmp_bwmgr *bwmgr)
{
#if defined(NV_TEGRA264_BWMGR_DEBUG_MACRO_PRESENT)
	dev_pm_qos_remove_notifier(bwmgr->dev,
				   &bwmgr->max_freq_nb,
				   DEV_PM_QOS_MAX_FREQUENCY);
	devm_devfreq_remove_device(bwmgr->dev, bwmgr->devfreq);
	dev_pm_opp_remove_all_dynamic(bwmgr->dev);
	kfree(bwmgr->devfreq_profile);
#endif
}

static int tegra_bpmp_bwmgr_probe(struct platform_device *pdev)
{
	struct mrq_bwmgr_int_request bwmgr_req = { 0 };
	struct mrq_bwmgr_int_request bwmgr_resp = { 0 };
	struct tegra_bpmp_message msg = { 0 };
	struct tegra_bpmp_bwmgr *bwmgr;
	int err;

	bwmgr = devm_kzalloc(&pdev->dev, sizeof(*bwmgr), GFP_KERNEL);
	if (!bwmgr)
		return -ENOMEM;

	bwmgr->clk = devm_clk_get(&pdev->dev, "emc");
	if (IS_ERR(bwmgr->clk)) {
		err = PTR_ERR(bwmgr->clk);
		goto devm_clk_get_err;
	}

	bwmgr->bpmp = tegra_bpmp_get(&pdev->dev);
	if (IS_ERR(bwmgr->bpmp)) {
		err = PTR_ERR(bwmgr->bpmp);
		goto bpmp_get_err;
	}

	bwmgr->mrq_valid = tegra_bpmp_mrq_is_supported(bwmgr->bpmp, MRQ_BWMGR_INT);
	if (bwmgr->mrq_valid) {
		bwmgr_req.cmd = CMD_BWMGR_INT_CALC_AND_SET;
		bwmgr_req.bwmgr_calc_set_req.mc_floor = 0;
		bwmgr_req.bwmgr_calc_set_req.floor_unit = BWMGR_INT_UNIT_KHZ;
#if defined(NV_TEGRA264_BWMGR_DEBUG_MACRO_PRESENT)
		bwmgr_req.bwmgr_calc_set_req.client_id = TEGRA264_BWMGR_DEBUG;
#endif

		msg.mrq = MRQ_BWMGR_INT;
		msg.tx.data = &bwmgr_req;
		msg.tx.size = sizeof(bwmgr_req);
		msg.rx.data = &bwmgr_resp;
		msg.rx.size = sizeof(bwmgr_resp);

		err = tegra_bpmp_transfer(bwmgr->bpmp, &msg);
		if (err < 0) {
			dev_err(&pdev->dev, "BPMP transfer failed: %d\n", err);
			return err;
		}

		if (msg.rx.ret == 0)
			bwmgr->enabled = true;
	}

	bwmgr->dev = &pdev->dev;
	platform_set_drvdata(pdev, bwmgr);

	err = devfreq_add_governor(&devfreq_gov_bwmgr);
	if (err) {
		dev_err(&pdev->dev,
			"fail to register devfreq governor %s: %d\n",
			DEVFREQ_GOV_BWMGR, err);
		goto add_gov_err;
	}

	err = tegra_bpmp_bwmgr_devfreq_register(bwmgr);
	if (err) {
		dev_err(&pdev->dev, "fail to register devfreq device: %d\n", err);
		goto devfreq_register_err;
	}

	return 0;

devfreq_register_err:
	devfreq_remove_governor(&devfreq_gov_bwmgr);
add_gov_err:
	tegra_bpmp_put(bwmgr->bpmp);
bpmp_get_err:
	devm_clk_put(&pdev->dev, bwmgr->clk);
devm_clk_get_err:
	kfree(bwmgr);
	return err;
}

static int tegra_bpmp_bwmgr_remove(struct platform_device *pdev)
{
	struct tegra_bpmp_bwmgr *bwmgr = platform_get_drvdata(pdev);

	tegra_bpmp_bwmgr_devfreq_unregister(bwmgr);
	devfreq_remove_governor(&devfreq_gov_bwmgr);
	tegra_bpmp_put(bwmgr->bpmp);
	devm_clk_put(&pdev->dev, bwmgr->clk);
	kfree(bwmgr);

	return 0;
}

static const struct of_device_id tegra_bpmp_bwmgr_of_match[] = {
	{ .compatible = "nvidia,tegra264-bpmp-bwmgr", .data = NULL, },
	{},
};
MODULE_DEVICE_TABLE(of, tegra_bpmp_bwmgr_of_match);

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void tegra_bpmp_bwmgr_remove_wrapper(struct platform_device *pdev)
{
	tegra_bpmp_bwmgr_remove(pdev);
}
#else
static int tegra_bpmp_bwmgr_remove_wrapper(struct platform_device *pdev)
{
	return tegra_bpmp_bwmgr_remove(pdev);
}
#endif

static struct platform_driver tegra_bpmp_bwmgr_driver = {
	.probe		= tegra_bpmp_bwmgr_probe,
	.remove		= tegra_bpmp_bwmgr_remove_wrapper,
	.driver	= {
		.name	= "tegra-bpmp-bwmgr",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(tegra_bpmp_bwmgr_of_match),
	},
};
module_platform_driver(tegra_bpmp_bwmgr_driver);

MODULE_DESCRIPTION("Tegra BPMP BWMGR client");
MODULE_AUTHOR("Johnny Liu <johnliu@nvidia.com>");
MODULE_LICENSE("GPL v2");
