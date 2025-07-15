// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2012-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <nvidia/conftest.h>

#include <linux/clk.h>
#include <linux/devfreq.h>
#include <linux/devfreq/nvhost_podgov.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/version.h>

#include <devfreq/governor.h>


#define DEFINE_ATTR_LOAD_STORE(name, limit)				\
	static ssize_t name##_store(struct device *dev,			\
				   struct device_attribute *attr,	\
				   const char *buf,			\
				   size_t count)			\
	{								\
		struct devfreq *df = to_devfreq(dev);			\
		struct podgov_data *podgov;				\
		unsigned int name;					\
		int ret;						\
									\
		ret = kstrtouint(buf, 0, &name);			\
		if (ret)						\
			return ret;					\
									\
		name = min_t(unsigned int, name, limit);		\
									\
		mutex_lock(&df->lock);					\
		podgov = df->governor_data;				\
		podgov->name = name;					\
		mutex_unlock(&df->lock);				\
									\
		return count;						\
	}								\
	static ssize_t name##_show(struct device *dev,			\
				  struct device_attribute *attr,	\
				  char *buf)				\
	{								\
		struct devfreq *df = to_devfreq(dev);			\
		struct podgov_data *podgov;				\
		int err;						\
									\
		mutex_lock(&df->lock);					\
		podgov = df->governor_data;				\
		err = sprintf(buf, "%u\n", podgov->name);		\
		mutex_unlock(&df->lock);				\
									\
		return err;						\
	}								\
	static DEVICE_ATTR_RW(name)


/**
 * struct podgov_data - governor private data stored in struct devfreq
 * @load_target:	Frequency scaling logic will try to keep the device
 *			running at the specified load with specific frequency.
 *			The valid value of 'load_target' ranges from 0 to 1000.
 * @load_margin:	Margin value associated with the 'load_target' to determine
 *			the load threshold to scale down the device frequency. If
 *			load_target equals to 700 and load_margin equals to 100,
 *			then the governor will scale down the frequency when load
 *			of device is below 600 (700 - 100). The valid value of
 *			'load_margin' ranges from 0 to 1000.
 * @load_max:		Whenever the instantaneous load value exceeds this value
 *			, the governor will try to scale the device to whatever
 *			maximum frequency it can achieve.  The valid value of
 *			'load_max' ranges from 0 to 1000.
 * @down_freq_margin:	Number of frequency steps for scaling down the frequency
 *			when the moving-average load value below the load_target.
 * @up_freq_margin:	Number of frequency steps for scaling up the frequency
 *			when the moving-average load value beyond the load_target.
 * @k:			The moving-average weight factor for the average load.
 *			The average load of the device is calcualted as following:
 *			$$
 *			    avg_load = (avg_load * (2**k - 1) + load) / 2**k
 *			$$
 * @avg_load:		Moving-average load value tracked by the governor.
 * @freq_index:		Index value of current frequency in the frequency table.
 * @df:			The devfreq device associated with the governor.
 * @nb:			Notifier block for DEVFREQ_TRANSITION_NOTIFIER list.
 */
struct podgov_data {
	/* Tunable parameters */
	unsigned int load_target;
	unsigned int load_margin;
	unsigned int load_max;
	unsigned int down_freq_margin;
	unsigned int up_freq_margin;
	unsigned int k;

	/* Private fields */
	unsigned int avg_load;
	int freq_index;
	struct devfreq *df;
	struct notifier_block nb;
};

DEFINE_ATTR_LOAD_STORE(load_target, 1000);
DEFINE_ATTR_LOAD_STORE(load_margin, 1000);
DEFINE_ATTR_LOAD_STORE(load_max, 1000);
DEFINE_ATTR_LOAD_STORE(down_freq_margin, 10);
DEFINE_ATTR_LOAD_STORE(up_freq_margin, 10);
DEFINE_ATTR_LOAD_STORE(k, 10);

static struct attribute *dev_entries[] = {
	&dev_attr_load_target.attr,
	&dev_attr_load_margin.attr,
	&dev_attr_load_max.attr,
	&dev_attr_down_freq_margin.attr,
	&dev_attr_up_freq_margin.attr,
	&dev_attr_k.attr,
	NULL,
};

static struct attribute_group dev_attr_group = {
	.name = DEVFREQ_GOV_NVHOST_PODGOV,
	.attrs = dev_entries,
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

static int devfreq_notifier_call(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct podgov_data *podgov = container_of(nb, struct podgov_data, nb);
	struct devfreq_freqs *freqs = ptr;

	switch (event) {
	case DEVFREQ_POSTCHANGE:
		podgov->freq_index = devfreq_get_freq_index(podgov->df, freqs->new);
		break;
	default:
		break;
	};

	return NOTIFY_DONE;
}

static int nvhost_pod_target_freq(struct devfreq *df, unsigned long *freq)
{
	struct podgov_data *podgov = df->governor_data;
	struct devfreq_dev_status *status;
	unsigned long load, weight;
	unsigned int down_threshold;
	int index, err;

	/*
	 * NOTE:
	 * Since "cancel_delayed_work_sync(&devfreq->work)" is not synchronized
	 * by the devfreq->lock mutex lock in the devfreq_monitor_stop function
	 * in the devfreq core, the condition check here is necessary due to
	 * out-of-order execution even though devfreq timer has stopped and the
	 * df->governor_data is already freed in the nvhost_pod_exit handler.
	 */
	if (!podgov)
		return -ENOENT;

	err = devfreq_update_stats(df);
	if (err)
		return err;

	status = &df->last_status;

	/* Update two types of loads */
	load = status->busy_time * 1000 / status->total_time;
	weight = 1 << podgov->k;
	podgov->avg_load = (podgov->avg_load * (weight - 1) + load) / weight;

	/* Scale up to maximum frequency to respond transient peak workload */
	if (load >= podgov->load_max) {
		*freq = ULONG_MAX;
		return 0;
	}

	/* Scale up current frequency by number of steps */
	if (podgov->avg_load > podgov->load_target) {
		index = podgov->freq_index + podgov->up_freq_margin;

#if defined(NV_DEVFREQ_HAS_FREQ_TABLE)
		if (index < 0)
			index = df->max_state - 1;
		else
			index = min(index, (int)df->max_state - 1);

		*freq = df->freq_table[index];
#else
		if (index < 0)
			index = df->profile->max_state - 1;
		else
			index = min(index, (int)df->profile->max_state - 1);

		*freq = df->profile->freq_table[index];
#endif
		return 0;
	}

	/* Scale down current frequency by number of steps */
	if (podgov->load_margin < podgov->load_target) {
		down_threshold = podgov->load_target - podgov->load_margin;
	} else {
		down_threshold = 0;
	}

	if (podgov->avg_load < down_threshold) {
		index = podgov->freq_index - podgov->down_freq_margin;
		index = max(index, 0);
#if defined(NV_DEVFREQ_HAS_FREQ_TABLE)
		*freq = df->freq_table[index];
#else
		*freq = df->profile->freq_table[index];
#endif
		return 0;
	}

	/* Stay with the same frequency */
	*freq = status->current_frequency;
	return 0;
}

static int nvhost_pod_init(struct devfreq *df)
{
	struct podgov_data *podgov;
	struct devfreq_dev_status *status;
	int err;

	err = devfreq_update_stats(df);
	if (err)
		return err;

	status = &df->last_status;

	podgov = kzalloc(sizeof(*podgov), GFP_KERNEL);
	if (!podgov)
		return -ENOMEM;

	df->governor_data = (void *)podgov;

	/* Set default scaling parameters */
	podgov->load_target = 700;
	podgov->load_margin = 100;
	podgov->load_max = 900;
	podgov->down_freq_margin = 1;
	podgov->up_freq_margin = 4;
	podgov->k = 3;

	/* Reset private data */
	podgov->avg_load = 0;
	podgov->freq_index = devfreq_get_freq_index(df, status->current_frequency);
	podgov->df = df;

	podgov->nb.notifier_call = devfreq_notifier_call;
	err = devfreq_register_notifier(df, &podgov->nb, DEVFREQ_TRANSITION_NOTIFIER);
	if (err)
		goto free_data;

	/* Expose tunable params under devfreq sysfs */
	err = sysfs_create_group(&df->dev.kobj, &dev_attr_group);
	if (err)
		goto unregister_notifier;

	return 0;

unregister_notifier:
	devfreq_unregister_notifier(df, &podgov->nb, DEVFREQ_TRANSITION_NOTIFIER);
free_data:
	kfree(df->governor_data);
	df->governor_data = NULL;
	return err;
}

static void nvhost_pod_exit(struct devfreq *df)
{
	struct podgov_data *podgov = df->governor_data;

	sysfs_remove_group(&df->dev.kobj, &dev_attr_group);
	devfreq_unregister_notifier(df, &podgov->nb, DEVFREQ_TRANSITION_NOTIFIER);

	kfree(df->governor_data);
	df->governor_data = NULL;
}

static void nvhost_pod_suspend(struct devfreq *df)
{
	struct podgov_data *podgov = df->governor_data;

	podgov->avg_load = 0;

	devfreq_monitor_suspend(df);
}

static void nvhost_pod_resume(struct devfreq *df)
{
	devfreq_monitor_resume(df);
}

static int nvhost_pod_event_handler(struct devfreq *df,
			unsigned int event, void *data)
{
	int ret = 0;

	switch (event) {
	case DEVFREQ_GOV_START:
		if (!try_module_get(THIS_MODULE))
			return -ENODEV;

		mutex_lock(&df->lock);
		ret = nvhost_pod_init(df);
		mutex_unlock(&df->lock);
		devfreq_monitor_start(df);
		break;
	case DEVFREQ_GOV_STOP:
		devfreq_monitor_stop(df);
		mutex_lock(&df->lock);
		nvhost_pod_exit(df);
		mutex_unlock(&df->lock);
		module_put(THIS_MODULE);
		break;
	case DEVFREQ_GOV_UPDATE_INTERVAL:
		devfreq_update_interval(df, (unsigned int *)data);
		break;
	case DEVFREQ_GOV_SUSPEND:
		nvhost_pod_suspend(df);
		break;
	case DEVFREQ_GOV_RESUME:
		nvhost_pod_resume(df);
		break;
	default:
		break;
	}

	return ret;
}

static struct devfreq_governor nvhost_podgov = {
	.name = DEVFREQ_GOV_NVHOST_PODGOV,
	.attrs = DEVFREQ_GOV_ATTR_POLLING_INTERVAL | DEVFREQ_GOV_ATTR_TIMER,
	.get_target_freq = nvhost_pod_target_freq,
	.event_handler = nvhost_pod_event_handler,
};


static int __init podgov_init(void)
{
	return devfreq_add_governor(&nvhost_podgov);
}
subsys_initcall(podgov_init);

static void __exit podgov_exit(void)
{
	devfreq_remove_governor(&nvhost_podgov);
	return;
}
module_exit(podgov_exit);
MODULE_LICENSE("GPL");
