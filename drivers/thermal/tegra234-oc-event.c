// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include <dt-bindings/thermal/tegra234-soctherm.h>
#include <linux/err.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>
#include <soc/tegra/bpmp-abi.h>
#include <soc/tegra/bpmp.h>

struct oc_soc_data {
	const struct attribute_group **attr_groups;
};

struct tegra234_oc_event {
	struct device *hwmon;
	struct tegra_bpmp *bpmp;
	const struct oc_soc_data *soc_data;
};

static ssize_t throt_en_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	int err = 0;
	struct tegra234_oc_event *tegra234_oc = dev_get_drvdata(dev);
	struct sensor_device_attribute *sensor_attr =
		container_of(attr, struct sensor_device_attribute, dev_attr);
	struct mrq_oc_status_response resp;
	struct tegra_bpmp_message msg = {
		.mrq = MRQ_OC_STATUS,
		.rx = {
			.data = &resp,
			.size = sizeof(resp),
		},
	};

	if (sensor_attr->index < 0) {
		dev_err(dev, "Negative index for OC events\n");
		return -EDOM;
	}

	err = tegra_bpmp_transfer(tegra234_oc->bpmp, &msg);
	if (err) {
		dev_err(dev, "Failed to transfer message: %d\n", err);
		return err;
	}

	if (msg.rx.ret < 0) {
		dev_err(dev, "Negative bpmp message return value: %d\n",
			msg.rx.ret);
		return -EINVAL;
	}

	return sprintf(buf, "%u\n", resp.throt_en[sensor_attr->index]);
}

static ssize_t event_cnt_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	int err = 0;
	struct tegra234_oc_event *tegra234_oc = dev_get_drvdata(dev);
	struct sensor_device_attribute *sensor_attr =
		container_of(attr, struct sensor_device_attribute, dev_attr);
	struct mrq_oc_status_response resp;
	struct tegra_bpmp_message msg = {
		.mrq = MRQ_OC_STATUS,
		.rx = {
			.data = &resp,
			.size = sizeof(resp),
		},
	};

	if (sensor_attr->index < 0) {
		dev_err(dev, "Negative index for OC events\n");
		return -EDOM;
	}

	err = tegra_bpmp_transfer(tegra234_oc->bpmp, &msg);
	if (err) {
		dev_err(dev, "Failed to transfer message: %d\n", err);
		return err;
	}

	if (msg.rx.ret < 0) {
		dev_err(dev, "Negative bpmp message return value: %d\n",
			msg.rx.ret);
		return -EINVAL;
	}

	return sprintf(buf, "%u\n", resp.event_cnt[sensor_attr->index]);
}

static SENSOR_DEVICE_ATTR_RO(oc1_throt_en, throt_en, TEGRA234_SOCTHERM_EDP_OC1);
static SENSOR_DEVICE_ATTR_RO(oc1_event_cnt, event_cnt,
			     TEGRA234_SOCTHERM_EDP_OC1);
static SENSOR_DEVICE_ATTR_RO(oc2_throt_en, throt_en, TEGRA234_SOCTHERM_EDP_OC2);
static SENSOR_DEVICE_ATTR_RO(oc2_event_cnt, event_cnt,
			     TEGRA234_SOCTHERM_EDP_OC2);
static SENSOR_DEVICE_ATTR_RO(oc3_throt_en, throt_en, TEGRA234_SOCTHERM_EDP_OC3);
static SENSOR_DEVICE_ATTR_RO(oc3_event_cnt, event_cnt,
			     TEGRA234_SOCTHERM_EDP_OC3);

static struct attribute *t234_oc1_attrs[] = {
	&sensor_dev_attr_oc1_throt_en.dev_attr.attr,
	&sensor_dev_attr_oc1_event_cnt.dev_attr.attr,
	NULL,
};

static struct attribute *t234_oc2_attrs[] = {
	&sensor_dev_attr_oc2_throt_en.dev_attr.attr,
	&sensor_dev_attr_oc2_event_cnt.dev_attr.attr,
	NULL,
};

static struct attribute *t234_oc3_attrs[] = {
	&sensor_dev_attr_oc3_throt_en.dev_attr.attr,
	&sensor_dev_attr_oc3_event_cnt.dev_attr.attr,
	NULL,
};

static const struct attribute_group oc1_data = {
	.attrs = t234_oc1_attrs,
};

static const struct attribute_group oc2_data = {
	.attrs = t234_oc2_attrs,
};

static const struct attribute_group oc3_data = {
	.attrs = t234_oc3_attrs,
};

static const struct attribute_group *t234_oc_groups[] = {
	&oc1_data,
	&oc2_data,
	&oc3_data,
	NULL,
};

static const struct oc_soc_data t234_oc_soc_data = {
	.attr_groups = t234_oc_groups,
};

static const struct of_device_id of_tegra234_oc_event_match[] = {
	{ .compatible = "nvidia,tegra234-oc-event", .data = &t234_oc_soc_data },
	{},
};
MODULE_DEVICE_TABLE(of, of_tegra234_oc_event_match);

static int tegra234_oc_event_probe(struct platform_device *pdev)
{
	int err = 0;
	const struct of_device_id *match;
	struct tegra_bpmp *tb;
	struct tegra234_oc_event *tegra234_oc;

	match = of_match_node(of_tegra234_oc_event_match, pdev->dev.of_node);
	if (!match)
		return -ENODEV;

	tb = tegra_bpmp_get(&pdev->dev);
	if (IS_ERR(tb))
		return PTR_ERR(tb);

	tegra234_oc =
		devm_kzalloc(&pdev->dev, sizeof(*tegra234_oc), GFP_KERNEL);
	if (!tegra234_oc) {
		err = -ENOMEM;
		goto put_bpmp;
	}

	platform_set_drvdata(pdev, tegra234_oc);
	tegra234_oc->soc_data = match->data;
	tegra234_oc->bpmp = tb;
	tegra234_oc->hwmon = devm_hwmon_device_register_with_groups(
		&pdev->dev, "soctherm_oc", tegra234_oc,
		tegra234_oc->soc_data->attr_groups);
	if (IS_ERR(tegra234_oc->hwmon)) {
		dev_err(&pdev->dev, "Failed to register hwmon device\n");
		err = -EINVAL;
		goto put_bpmp;
	}

	return err;

put_bpmp:
	tegra_bpmp_put(tb);

	return err;
}

static int tegra234_oc_event_remove(struct platform_device *pdev)
{
	struct tegra234_oc_event *tegra234_oc = platform_get_drvdata(pdev);

	if (!tegra234_oc)
		return -EINVAL;

	tegra_bpmp_put(tegra234_oc->bpmp);
	return 0;
}

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void tegra234_oc_event_remove_wrapper(struct platform_device *pdev)
{
	tegra234_oc_event_remove(pdev);
}
#else
static int tegra234_oc_event_remove_wrapper(struct platform_device *pdev)
{
	return tegra234_oc_event_remove(pdev);
}
#endif

static struct platform_driver tegra234_oc_event_driver = {
	.probe = tegra234_oc_event_probe,
	.remove = tegra234_oc_event_remove_wrapper,
	.driver = {
		.name = "tegra234-oc-event",
		.of_match_table = of_tegra234_oc_event_match,
	},
};

module_platform_driver(tegra234_oc_event_driver);

MODULE_AUTHOR("Yi-Wei Wang <yiweiw@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra234 Overcurrent Event Driver");
MODULE_LICENSE("GPL v2");
