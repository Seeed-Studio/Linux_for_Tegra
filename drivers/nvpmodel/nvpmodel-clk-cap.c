// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include <linux/clk.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <soc/tegra/bpmp.h>
#include <soc/tegra/bpmp-abi.h>

struct nvpmodel_clk {
	struct kobj_attribute attr;
	struct clk *clk;
	struct tegra_bpmp *bpmp;
};

struct nvpmodel_clk_cap {
	struct kobject *clk_cap_kobject;
	struct nvpmodel_clk *clks;
	struct tegra_bpmp *bpmp;
	int num_clocks;
};

static ssize_t ccf_set_max_rate(struct clk *clk, unsigned long rate)
{
	int ret = 0;
	long rounded_rate = 0;

	/* Remove previous freq cap to get correct rounded rate for new cap */
	ret = clk_set_max_rate(clk, S64_MAX);
	if (ret)
		return ret;

	/* Determine what the rounded rate should be */
	rounded_rate = clk_round_rate(clk, rate);
	if (rounded_rate < 0)
		return rounded_rate;

	/* Apply new freq cap */
	rate = (unsigned long)rounded_rate;
	ret = clk_set_max_rate(clk, rate);

	return ret;
}

static ssize_t bpmp_set_emc_cap_rate(struct tegra_bpmp *bpmp, unsigned long rate)
{
	struct mrq_bwmgr_int_request req = { 0 };
	struct mrq_bwmgr_int_response resp = { 0 };
	struct tegra_bpmp_message msg;
	int ret = 0;

	req.cmd = CMD_BWMGR_INT_CAP_SET;
	req.bwmgr_cap_set_req.rate = rate;
	memset(&msg, 0, sizeof(struct tegra_bpmp_message));
	msg.mrq = MRQ_BWMGR_INT;
	msg.tx.data = &req;
	msg.tx.size = sizeof(struct mrq_bwmgr_int_request);
	msg.rx.data = &resp;
	msg.rx.size = sizeof(struct mrq_bwmgr_int_response);
	ret = tegra_bpmp_transfer(bpmp, &msg);
	if (ret || msg.rx.ret != 0)
		ret = -EINVAL;

	return ret;
}

static ssize_t clk_cap_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	long rate;
	struct nvpmodel_clk *nvpm_clk = container_of(attr, struct nvpmodel_clk, attr);

	rate = clk_round_rate(nvpm_clk->clk, S64_MAX);
	if (rate < 0)
		return rate;

	return sprintf(buf, "%ld\n", rate);
}

static ssize_t clk_cap_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
			     size_t count)
{
	int ccf_ret, bpmp_ret;
	long prev_max_rate, rounded_max_rate;
	unsigned long rate;
	struct nvpmodel_clk *nvpm_clk = container_of(attr, struct nvpmodel_clk, attr);

	ccf_ret = kstrtoul(buf, 0, &rate);
	if (ccf_ret)
		return ccf_ret;

	/* Store previous max freq in case of later failure */
	prev_max_rate = clk_round_rate(nvpm_clk->clk, S64_MAX);
	if (prev_max_rate < 0)
		return prev_max_rate;

	/* Update max freq in CCF */
	ccf_ret = ccf_set_max_rate(nvpm_clk->clk, rate);
	if (ccf_ret)
		return ccf_ret;

	/* Early return for the clocks that do not require additional BPMP MRQ involvement */
	if (strncmp(nvpm_clk->attr.attr.name, "emc", strlen("emc")))
		return count;

	/*
	 * The max freq has been successfully updated in the CCF, so any later
	 * failure must restore the max frequency to the previous frequency.
	 */
	rounded_max_rate = clk_round_rate(nvpm_clk->clk, S64_MAX);
	if (rounded_max_rate < 0) {
		pr_debug("CCF failed to round emc clock rate, try to restore previous max rate\n");
		ccf_ret = ccf_set_max_rate(nvpm_clk->clk, prev_max_rate);
		if (ccf_ret) {
			pr_debug("CCF failed to restore previous max rate for emc\n");
			return ccf_ret;
		}

		return rounded_max_rate;
	}

	/* Send EMC max freq capping request to BPMP */
	bpmp_ret = bpmp_set_emc_cap_rate(nvpm_clk->bpmp, rounded_max_rate);
	if (bpmp_ret) {
		pr_debug("BPMP failed to update emc max rate, try to restore previous max rate\n");
		ccf_ret = ccf_set_max_rate(nvpm_clk->clk, prev_max_rate);
		if (ccf_ret) {
			pr_debug("CCF failed to restore previous max rate for emc\n");
			return ccf_ret;
		}

		return bpmp_ret;
	}

	return count;
}

static const struct of_device_id of_nvpmodel_clk_cap_match[] = {
	{ .compatible = "nvidia,nvpmodel", },
	{},
};
MODULE_DEVICE_TABLE(of, of_nvpmodel_clk_cap_match);

static int nvpmodel_clk_cap_probe(struct platform_device *pdev)
{
	int i;
	int ret = 0;
	const char *clk_name;
	struct device_node *dn = pdev->dev.of_node;
	struct nvpmodel_clk_cap *nvpm_clk_cap;
	struct tegra_bpmp *tb;

	tb = tegra_bpmp_get(&pdev->dev);
	if (IS_ERR(tb))
		return PTR_ERR(tb);

	nvpm_clk_cap = devm_kzalloc(&pdev->dev, sizeof(*nvpm_clk_cap), GFP_KERNEL);
	if (!nvpm_clk_cap) {
		dev_err(&pdev->dev, "Failed to allocate nvpm_clk_cap!\n");
		ret = -ENOMEM;
		goto put_bpmp;
	}

	platform_set_drvdata(pdev, nvpm_clk_cap);

	nvpm_clk_cap->bpmp = tb;
	nvpm_clk_cap->num_clocks = of_property_count_strings(dn, "clock-names");
	if (nvpm_clk_cap->num_clocks <= 0) {
		dev_err(&pdev->dev, "Please specify at least one clock in clocks-names!\n");
		ret = nvpm_clk_cap->num_clocks ? nvpm_clk_cap->num_clocks : -ENODEV;
		goto put_bpmp;
	}

	nvpm_clk_cap->clks = devm_kzalloc(
		&pdev->dev, sizeof(*nvpm_clk_cap->clks) * nvpm_clk_cap->num_clocks, GFP_KERNEL);
	if (!nvpm_clk_cap->clks) {
		dev_err(&pdev->dev, "Failed to allocate the clocks!\n");
		ret = -ENOMEM;
		goto put_bpmp;
	}

	nvpm_clk_cap->clk_cap_kobject = kobject_create_and_add("nvpmodel_clk_cap", kernel_kobj);
	if (!nvpm_clk_cap->clk_cap_kobject) {
		dev_err(&pdev->dev, "Failed to create nvpmodel_clk_cap sysfs!\n");
		ret = -ENOMEM;
		goto put_bpmp;
	}

	for (i = 0; i < nvpm_clk_cap->num_clocks; i++) {
		if (of_property_read_string_index(dn, "clock-names", i, &clk_name)) {
			dev_warn(&pdev->dev, "Couldn't read %d-th clock name\n", i);
			continue;
		}

		(nvpm_clk_cap->clks)[i].bpmp = tb;
		(nvpm_clk_cap->clks)[i].clk = devm_clk_get(&pdev->dev, clk_name);
		if (IS_ERR((nvpm_clk_cap->clks)[i].clk)) {
			(nvpm_clk_cap->clks)[i].clk = NULL;
			dev_warn(&pdev->dev, "Couldn't get %s clock\n", clk_name);
			continue;
		}

		if (clk_prepare_enable((nvpm_clk_cap->clks)[i].clk)) {
			dev_warn(&pdev->dev, "Couldn't prepare and enable %s clock\n", clk_name);
			continue;
		}

		(nvpm_clk_cap->clks)[i].attr.attr.name = kstrdup_const(clk_name, GFP_KERNEL);
		if (!(nvpm_clk_cap->clks)[i].attr.attr.name) {
			dev_warn(&pdev->dev, "Couldn't allocate memory for %s clock\n", clk_name);
			continue;
		}

		sysfs_attr_init(&((nvpm_clk_cap->clks)[i].attr.attr));
		(nvpm_clk_cap->clks)[i].attr.attr.mode = 0664;
		(nvpm_clk_cap->clks)[i].attr.show = clk_cap_show;
		(nvpm_clk_cap->clks)[i].attr.store = clk_cap_store;
		if (sysfs_create_file(nvpm_clk_cap->clk_cap_kobject,
				      &(nvpm_clk_cap->clks)[i].attr.attr)) {
			dev_warn(&pdev->dev, "Couldn't create %s clock cap sysfs\n", clk_name);
			continue;
		}
	}

	return ret;

put_bpmp:
	tegra_bpmp_put(tb);

	return ret;
}

static int nvpmodel_clk_cap_remove(struct platform_device *pdev)
{
	int i;
	struct nvpmodel_clk_cap *nvpm_clk_cap = platform_get_drvdata(pdev);

	if (!nvpm_clk_cap)
		return -EINVAL;

	tegra_bpmp_put(nvpm_clk_cap->bpmp);

	if (nvpm_clk_cap->clks) {
		for (i = 0; i < nvpm_clk_cap->num_clocks; i++) {
			if ((nvpm_clk_cap->clks)[i].attr.attr.name)
				kfree_const((nvpm_clk_cap->clks)[i].attr.attr.name);

			if ((nvpm_clk_cap->clks)[i].clk)
				clk_disable_unprepare((nvpm_clk_cap->clks)[i].clk);

			sysfs_remove_file(nvpm_clk_cap->clk_cap_kobject,
					  &(nvpm_clk_cap->clks)[i].attr.attr);
		}
	}

	kobject_put(nvpm_clk_cap->clk_cap_kobject);

	return 0;
}

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void nvpmodel_clk_cap_remove_wrapper(struct platform_device *pdev)
{
	nvpmodel_clk_cap_remove(pdev);
}
#else
static int nvpmodel_clk_cap_remove_wrapper(struct platform_device *pdev)
{
	return nvpmodel_clk_cap_remove(pdev);
}
#endif

static struct platform_driver nvpmodel_clk_cap_driver = {
	.probe = nvpmodel_clk_cap_probe,
	.remove = nvpmodel_clk_cap_remove_wrapper,
	.driver = {
		.name = "nvpmodel-clk-cap",
		.of_match_table = of_nvpmodel_clk_cap_match,
	},
};

module_platform_driver(nvpmodel_clk_cap_driver);

MODULE_AUTHOR("Yi-Wei Wang <yiweiw@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA nvpmodel clock cap driver");
MODULE_LICENSE("GPL v2");
