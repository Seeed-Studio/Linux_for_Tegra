// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2023-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include <linux/devfreq.h>
#include <linux/devfreq/tegra_wmark.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/pm_opp.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>
#include <linux/version.h>

#include <devfreq/governor.h>

/**
 * struct tegra_wmark_data - governor private data stored in struct devfreq
 * @load_target:	Frequency scaling logic will try to keep the device
 *			running at the specified load under specific frequency.
 *			The average watermark thresholds of the actmon should be
 *			calculated with this value:
 *			$$
 *			    avg_x_wmark = freq_x * load_target / 1000
 *			$$
 *			where 'x' can be 'upper' or 'lower' in the formula, and
 *			the valid value of 'load_target' ranges from 0 to 1000.
 * @up_wmark_margin:	Offset value that will be applied to load_target for the
 *			consecutive upper watermark threshold, which means the
 *			formula becomes:
 *			$$
 *			    consec_upper_wmark =
 *			      freq_upper * (load_target + up_wmark_margin) / 1000
 *			$$
 * @down_wmark_margin:  Offset value that will be applied to load_target for the
 *                      consecutive lower watermark threshold, which means the
 *                      formula becomes:
 *                      $$
 *                          consec_lower_wmark =
 *			      freq_lower * (load_target - down_wmark_margin) / 1000
 *                      $$
 * @up_freq_margin:	Number of frequency steps for scaling up the frequency
 *			when consecutive upper watermark interrupt get triggered.
 * @down_freq_margin:	Number of frequency steps for scaling down the frequency
 *			when consecutive lower watermark interrupt get triggered.
 * @curr_freq_index:		Index value of current frequency in the frequency table.
 * @df:			The devfreq instance of own device.
 * @nb:			Notifier block for DEVFREQ_TRANSITION_NOTIFIER list.
 */
struct tegra_wmark_data {
	unsigned int load_target;
	unsigned int up_wmark_margin;
	unsigned int down_wmark_margin;
	unsigned int up_freq_margin;
	unsigned int down_freq_margin;
	int curr_freq_index;
	struct devfreq *df;
	struct notifier_block nb;
};

static int devfreq_get_freq_index(struct devfreq *df, unsigned long freq)
{
#if defined(NV_DEVFREQ_HAS_FREQ_TABLE)
	unsigned long *freq_table = df->freq_table;
	unsigned int max_state = df->max_state;
#else
	unsigned long *freq_table = df->profile->freq_table;
	unsigned int max_state = df->profile->max_state;
#endif
	int i;

	for (i = 0; i < max_state; i++) {
		if (freq_table[i] >= freq)
			break;
	}

	return i;
}

static int devfreq_tegra_wmark_target_freq(struct devfreq *df, unsigned long *freq)
{
	struct tegra_wmark_data *govdata = df->governor_data;
	struct devfreq_tegra_wmark_data *drvdata = df->data;
#if defined(NV_DEVFREQ_HAS_FREQ_TABLE)
	unsigned long *freq_table = df->freq_table;
	unsigned int max_state = df->max_state;
#else
	unsigned long *freq_table = df->profile->freq_table;
	unsigned int max_state = df->profile->max_state;
#endif
	int target_index = 0;

	switch (drvdata->event) {
	case DEVFREQ_TEGRA_AVG_WMARK_BELOW:
		target_index = max_t(int, 0, govdata->curr_freq_index-1);
		break;
	case DEVFREQ_TEGRA_AVG_WMARK_ABOVE:
		target_index = min_t(int, max_state-1, govdata->curr_freq_index+1);
		break;
	case DEVFREQ_TEGRA_CONSEC_WMARK_BELOW:
		target_index = max_t(int, 0, govdata->curr_freq_index-govdata->down_freq_margin);
		break;
	case DEVFREQ_TEGRA_CONSEC_WMARK_ABOVE:
		target_index = min_t(int,
				     max_state-1,
				     govdata->curr_freq_index + govdata->up_freq_margin);
		break;
	default:
		break;
	}

	*freq = freq_table[target_index];

	return 0;
}

static s32 devfreq_pm_qos_read_value(struct devfreq *df, enum dev_pm_qos_req_type type)
{
	struct device *dev = df->dev.parent;
	struct dev_pm_qos *pm_qos = dev->power.qos;
	s32 ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&dev->power.lock, flags);

	switch (type) {
	case DEV_PM_QOS_MIN_FREQUENCY:
		ret = IS_ERR_OR_NULL(pm_qos) ? PM_QOS_MIN_FREQUENCY_DEFAULT_VALUE
			: READ_ONCE(pm_qos->freq.min_freq.target_value);
		break;
	case DEV_PM_QOS_MAX_FREQUENCY:
		ret = IS_ERR_OR_NULL(pm_qos) ? PM_QOS_MAX_FREQUENCY_DEFAULT_VALUE
			: READ_ONCE(pm_qos->freq.max_freq.target_value);
		break;
	default:
		break;
	}

	spin_unlock_irqrestore(&dev->power.lock, flags);

	return ret;
}

static void devfreq_get_freq_range(struct devfreq *df,
				   unsigned long *min_freq,
				   unsigned long *max_freq)
{
	s32 qos_min_freq, qos_max_freq;

#if defined(NV_DEVFREQ_HAS_FREQ_TABLE)
	*min_freq = df->freq_table[0];
	*max_freq = df->freq_table[df->max_state - 1];
#else
	*min_freq = df->profile->freq_table[0];
	*max_freq = df->profile->freq_table[df->profile->max_state - 1];
#endif

	qos_min_freq = devfreq_pm_qos_read_value(df, DEV_PM_QOS_MIN_FREQUENCY);
	qos_max_freq = devfreq_pm_qos_read_value(df, DEV_PM_QOS_MAX_FREQUENCY);

	/* Apply constraints from PM QoS */
	*min_freq = max(*min_freq, (unsigned long)qos_min_freq * 1000);
	if (qos_max_freq != PM_QOS_MAX_FREQUENCY_DEFAULT_VALUE)
		*max_freq = min(*max_freq, (unsigned long)qos_max_freq * 1000);

	/* Apply constraints from OPP framework */
	*min_freq = max(*min_freq, df->scaling_min_freq);
	*max_freq = min(*max_freq, df->scaling_max_freq);
}

static void devfreq_update_wmark_threshold(struct devfreq *df)
{
	struct tegra_wmark_data *govdata = df->governor_data;
	struct devfreq_tegra_wmark_data *drvdata = df->data;
	struct devfreq_tegra_wmark_config wmark_config;
	unsigned long curr_freq, prev_freq, min_freq, max_freq;
#if defined(NV_DEVFREQ_HAS_FREQ_TABLE)
	unsigned long *freq_table = df->freq_table;
#else
	unsigned long *freq_table = df->profile->freq_table;
#endif
	int err;

	devfreq_get_freq_range(df, &min_freq, &max_freq);

	err = devfreq_update_stats(df);
	if (err)
		return;

	govdata->curr_freq_index = devfreq_get_freq_index(df, df->last_status.current_frequency);

	curr_freq = freq_table[govdata->curr_freq_index];

	if (curr_freq < max_freq) {
		wmark_config.avg_upper_wmark = curr_freq / 1000 * govdata->load_target;
		wmark_config.consec_upper_wmark = wmark_config.avg_upper_wmark
						  + (curr_freq / 1000 * govdata->up_wmark_margin);
		wmark_config.upper_wmark_enabled = 1;
	} else
		wmark_config.upper_wmark_enabled = 0;

	if (curr_freq > min_freq) {
		prev_freq = max_t(unsigned long,
				  freq_table[max_t(int,
						   govdata->curr_freq_index-1,
						   0)],
				  min_freq);
		wmark_config.avg_lower_wmark = prev_freq / 1000 * govdata->load_target;
		wmark_config.consec_lower_wmark = wmark_config.avg_lower_wmark
						  - (prev_freq / 1000 * govdata->down_wmark_margin);
		wmark_config.lower_wmark_enabled = 1;
	} else
		wmark_config.lower_wmark_enabled = 0;

	drvdata->update_wmark_threshold(df, &wmark_config);
}

static ssize_t up_freq_margin_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,
				 size_t count)
{
	struct devfreq *df = to_devfreq(dev);
	struct tegra_wmark_data *govdata;
	unsigned int freq_margin;
#if defined(NV_DEVFREQ_HAS_FREQ_TABLE)
	unsigned int max_state = df->max_state;
#else
	unsigned int max_state = df->profile->max_state;
#endif
	int ret;

	ret = kstrtouint(buf, 0, &freq_margin);
	if (ret)
		return ret;

	freq_margin = min_t(unsigned int, freq_margin, max_state);

	mutex_lock(&df->lock);
	govdata = df->governor_data;
	govdata->up_freq_margin = freq_margin;
	mutex_unlock(&df->lock);

	return count;
}

static ssize_t up_freq_margin_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct devfreq *df = to_devfreq(dev);
	struct tegra_wmark_data *govdata;
	int err;

	mutex_lock(&df->lock);
	govdata = df->governor_data;
	err = sprintf(buf, "%u\n", govdata->up_freq_margin);
	mutex_unlock(&df->lock);

	return err;
}
static DEVICE_ATTR_RW(up_freq_margin);

static ssize_t down_freq_margin_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,
				 size_t count)
{
	struct devfreq *df = to_devfreq(dev);
	struct tegra_wmark_data *govdata;
	unsigned int freq_margin;
#if defined(NV_DEVFREQ_HAS_FREQ_TABLE)
	unsigned int max_state = df->max_state;
#else
	unsigned int max_state = df->profile->max_state;
#endif
	int ret;

	ret = kstrtouint(buf, 0, &freq_margin);
	if (ret)
		return ret;

	freq_margin = min_t(unsigned int, freq_margin, max_state);

	mutex_lock(&df->lock);
	govdata = df->governor_data;
	govdata->down_freq_margin = freq_margin;
	mutex_unlock(&df->lock);

	return count;
}

static ssize_t down_freq_margin_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct devfreq *df = to_devfreq(dev);
	struct tegra_wmark_data *govdata;
	int err;

	mutex_lock(&df->lock);
	govdata = df->governor_data;
	err = sprintf(buf, "%u\n", govdata->down_freq_margin);
	mutex_unlock(&df->lock);

	return err;
}
static DEVICE_ATTR_RW(down_freq_margin);

static ssize_t up_wmark_margin_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,
				 size_t count)
{
	struct devfreq *df = to_devfreq(dev);
	struct tegra_wmark_data *govdata;
	unsigned int wmark_margin;
	int ret;

	ret = kstrtouint(buf, 0, &wmark_margin);
	if (ret)
		return ret;

	wmark_margin = min_t(unsigned int, wmark_margin, 1000);

	mutex_lock(&df->lock);
	govdata = df->governor_data;
	govdata->up_wmark_margin = wmark_margin;
	mutex_unlock(&df->lock);

	devfreq_update_wmark_threshold(df);

	return count;
}

static ssize_t up_wmark_margin_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct devfreq *df = to_devfreq(dev);
	struct tegra_wmark_data *govdata;
	int err;

	mutex_lock(&df->lock);
	govdata = df->governor_data;
	err = sprintf(buf, "%u\n", govdata->up_wmark_margin);
	mutex_unlock(&df->lock);

	return err;
}
static DEVICE_ATTR_RW(up_wmark_margin);

static ssize_t down_wmark_margin_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,
				 size_t count)
{
	struct devfreq *df = to_devfreq(dev);
	struct tegra_wmark_data *govdata;
	unsigned int wmark_margin;
	int ret;

	ret = kstrtouint(buf, 0, &wmark_margin);
	if (ret)
		return ret;

	wmark_margin = min_t(unsigned int, wmark_margin, 1000);

	mutex_lock(&df->lock);
	govdata = df->governor_data;
	govdata->down_wmark_margin = wmark_margin;
	mutex_unlock(&df->lock);

	devfreq_update_wmark_threshold(df);

	return count;
}

static ssize_t down_wmark_margin_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct devfreq *df = to_devfreq(dev);
	struct tegra_wmark_data *govdata;
	int err;

	mutex_lock(&df->lock);
	govdata = df->governor_data;
	err = sprintf(buf, "%u\n", govdata->down_wmark_margin);
	mutex_unlock(&df->lock);

	return err;
}
static DEVICE_ATTR_RW(down_wmark_margin);

static ssize_t load_target_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,
				 size_t count)
{
	struct devfreq *df = to_devfreq(dev);
	struct tegra_wmark_data *govdata;
	unsigned int load_target;
	int ret;

	ret = kstrtouint(buf, 0, &load_target);
	if (ret)
		return ret;

	load_target = min_t(unsigned int, load_target, 1000);

	mutex_lock(&df->lock);
	govdata = df->governor_data;
	govdata->load_target = load_target;
	mutex_unlock(&df->lock);

	devfreq_update_wmark_threshold(df);

	return count;
}

static ssize_t load_target_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct devfreq *df = to_devfreq(dev);
	struct tegra_wmark_data *govdata;
	int err;

	mutex_lock(&df->lock);
	govdata = df->governor_data;
	err = sprintf(buf, "%u\n", govdata->load_target);
	mutex_unlock(&df->lock);

	return err;
}
static DEVICE_ATTR_RW(load_target);

static struct attribute *dev_entries[] = {
	&dev_attr_load_target.attr,
	&dev_attr_up_wmark_margin.attr,
	&dev_attr_down_wmark_margin.attr,
	&dev_attr_up_freq_margin.attr,
	&dev_attr_down_freq_margin.attr,
	NULL,
};

static struct attribute_group dev_attr_group = {
	.name = DEVFREQ_GOV_TEGRA_WMARK,
	.attrs = dev_entries,
};

static int devfreq_tegra_wmark_notifier_call(struct notifier_block *nb,
					      unsigned long event, void *ptr)
{
	struct tegra_wmark_data *govdata
			= container_of(nb, struct tegra_wmark_data, nb);

	switch (event) {
	case DEVFREQ_POSTCHANGE:
		devfreq_update_wmark_threshold(govdata->df);
		break;
	default:
		break;
	};

	return NOTIFY_DONE;
}

static int tegra_wmark_init(struct devfreq *df)
{
	struct tegra_wmark_data *govdata;
	struct devfreq_tegra_wmark_data *drvdata;
	int err;

	if (!df->data)
		return -EINVAL;

	drvdata = df->data;
	if (!drvdata->update_wmark_threshold)
		return -EINVAL;

	govdata = kzalloc(sizeof(*govdata), GFP_KERNEL);
	if (!govdata)
		return -ENOMEM;

	govdata->load_target = 800;
	govdata->up_wmark_margin = 100;
	govdata->down_wmark_margin = 100;
	govdata->up_freq_margin = 4;
	govdata->down_freq_margin = 1;
	govdata->curr_freq_index = 0;

	govdata->df = df;
	df->governor_data = govdata;

	govdata->nb.notifier_call = devfreq_tegra_wmark_notifier_call;
	err = devfreq_register_notifier(df, &govdata->nb, DEVFREQ_TRANSITION_NOTIFIER);
	if (err)
		goto out_register_notifier;

	err = sysfs_create_group(&df->dev.kobj, &dev_attr_group);
	if (err)
		goto out_create_sysfs;

	return err;

out_create_sysfs:
	devfreq_unregister_notifier(df, &govdata->nb, DEVFREQ_TRANSITION_NOTIFIER);

out_register_notifier:
	kfree(df->governor_data);
	df->governor_data = NULL;

	return err;
}

static void tegra_wmark_exit(struct devfreq *df)
{
	struct tegra_wmark_data *govdata = df->governor_data;

	devfreq_unregister_notifier(df, &govdata->nb, DEVFREQ_TRANSITION_NOTIFIER);
	sysfs_remove_group(&df->dev.kobj, &dev_attr_group);
	kfree(df->governor_data);
	df->governor_data = NULL;
}

static int devfreq_tegra_wmark_event_handler(struct devfreq *df,
					      unsigned int event,
					      void *data)
{
	struct devfreq_tegra_wmark_data *drvdata = df->data;
	struct devfreq_tegra_wmark_config wmark_config;
	int err;

	switch (event) {
	case DEVFREQ_GOV_START:
		if (!try_module_get(THIS_MODULE))
			return -ENODEV;

		err = tegra_wmark_init(df);
		if (err)
			return err;

		devfreq_update_wmark_threshold(df);
		break;
	case DEVFREQ_GOV_STOP:
		wmark_config.upper_wmark_enabled = 0;
		wmark_config.lower_wmark_enabled = 0;
		drvdata->update_wmark_threshold(df, &wmark_config);
		tegra_wmark_exit(df);
		module_put(THIS_MODULE);
		break;
	case DEVFREQ_GOV_SUSPEND:
		wmark_config.upper_wmark_enabled = 0;
		wmark_config.lower_wmark_enabled = 0;
		drvdata->update_wmark_threshold(df, &wmark_config);
		break;
	case DEVFREQ_GOV_RESUME:
		devfreq_update_wmark_threshold(df);
		break;
	default:
		break;
	}

	return 0;
}

static struct devfreq_governor devfreq_tegra_wmark = {
	.name = DEVFREQ_GOV_TEGRA_WMARK,
	.flags = DEVFREQ_GOV_FLAG_IRQ_DRIVEN,
	.get_target_freq = devfreq_tegra_wmark_target_freq,
	.event_handler = devfreq_tegra_wmark_event_handler,
};

static int __init devfreq_tegra_wmark_init(void)
{
	return devfreq_add_governor(&devfreq_tegra_wmark);
}
subsys_initcall(devfreq_tegra_wmark_init);

static void __exit devfreq_tegra_wmark_exit(void)
{
	devfreq_remove_governor(&devfreq_tegra_wmark);
}
module_exit(devfreq_tegra_wmark_exit);

MODULE_AUTHOR("Johnny Liu <johnliu@nvidia.com>");
MODULE_LICENSE("GPL v2");
