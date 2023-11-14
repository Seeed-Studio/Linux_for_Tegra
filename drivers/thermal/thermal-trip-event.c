// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/wait.h>

enum cdev_states {
	CDEV_INACTIVE,
	CDEV_ACTIVE,
	CDEV_DESTROY,
};

struct therm_trip_event {
	unsigned int cur_state;
	unsigned int max_state;
	unsigned int event_timeout_ms;
	struct mutex cur_state_lock;
	struct mutex event_timeout_lock;
	struct thermal_cooling_device *cdev;
	wait_queue_head_t waitq_head;
};

static int tte_cdev_get_max_state(struct thermal_cooling_device *tcd,
				  unsigned long *state)
{
	struct therm_trip_event *tte = tcd->devdata;

	*state = tte->max_state;

	return 0;
}

static int tte_cdev_get_cur_state(struct thermal_cooling_device *tcd,
				  unsigned long *state)
{
	struct therm_trip_event *tte = tcd->devdata;

	*state = tte->cur_state;

	return 0;
}

static int tte_cdev_set_cur_state(struct thermal_cooling_device *tcd,
				  unsigned long state)
{
	struct therm_trip_event *tte = tcd->devdata;
	struct device *dev = &tcd->device;

	if (state > tte->max_state)
		return -EINVAL;

	if (state == tte->cur_state)
		return 0;

	dev_notice(dev, "%s cooling state: %u -> %lu\n", tcd->type,
		   tte->cur_state, state);

	/*
	 * Although tcd->lock is already held in the thermal framework, a lock
	 * is added here to avoid a race condition between set_cur_state() and
	 * driver removal.
	 */
	mutex_lock(&tte->cur_state_lock);
	tte->cur_state = state;
	mutex_unlock(&tte->cur_state_lock);

	if (tte->cur_state != CDEV_INACTIVE) {
		if (wq_has_sleeper(&tte->waitq_head)) {
			wake_up_interruptible_all(&tte->waitq_head);
			dev_dbg(dev, "THERMAL_EVENT_TRIPPED!\n");
		}
	}

	return 0;
}

static ssize_t thermal_trip_event_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct therm_trip_event *tte = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", tte->cur_state);
}

static ssize_t thermal_trip_event_block_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct therm_trip_event *tte = dev_get_drvdata(dev);
	unsigned int event_timeout_ms = tte->event_timeout_ms;
	int ret;

	if (event_timeout_ms > 0)
		ret = wait_event_interruptible_timeout(
			tte->waitq_head, tte->cur_state != CDEV_INACTIVE,
			msecs_to_jiffies(event_timeout_ms));
	else
		ret = wait_event_interruptible(tte->waitq_head,
					       tte->cur_state != CDEV_INACTIVE);

	/*
	 * -ERESTARTSYS is returned to avoid false alarm and to resume the call.
	 */
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", tte->cur_state);
}

static ssize_t thermal_trip_event_block_store(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t count)
{
	unsigned int val;
	struct therm_trip_event *tte = dev_get_drvdata(dev);

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	mutex_lock(&tte->event_timeout_lock);
	tte->event_timeout_ms = val;
	mutex_unlock(&tte->event_timeout_lock);

	return count;
}

static struct thermal_cooling_device_ops tte_cdev_ops = {
	.get_max_state = tte_cdev_get_max_state,
	.get_cur_state = tte_cdev_get_cur_state,
	.set_cur_state = tte_cdev_set_cur_state,
};

static DEVICE_ATTR_RO(thermal_trip_event);
static DEVICE_ATTR_RW(thermal_trip_event_block);

static const struct attribute *tte_cdev_attr[] = {
	&dev_attr_thermal_trip_event.attr,
	&dev_attr_thermal_trip_event_block.attr,
	NULL,
};

static int thermal_trip_event_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct therm_trip_event *tte;
	const char *cdev_type;
	int ret;

	tte = devm_kzalloc(dev, sizeof(struct therm_trip_event), GFP_KERNEL);
	if (!tte)
		return -ENOMEM;

	if (of_property_read_string(np, "cdev-type", &cdev_type) != 0) {
		dev_err(dev, "invalid cdev-type property of %pOFn\n", np);
		return -EINVAL;
	}

	tte->cur_state = CDEV_INACTIVE;
	tte->max_state = CDEV_DESTROY;
	mutex_init(&tte->cur_state_lock);
	mutex_init(&tte->event_timeout_lock);
	init_waitqueue_head(&tte->waitq_head);
	dev_set_drvdata(dev, tte);

	tte->cdev = thermal_of_cooling_device_register(np, cdev_type, tte,
						       &tte_cdev_ops);
	if (IS_ERR(tte->cdev)) {
		ret = PTR_ERR(tte->cdev);
		goto destroy_lock;
	}

	ret = sysfs_create_files(&dev->kobj, tte_cdev_attr);
	if (ret) {
		dev_err(dev, "failed to create sysfs files\n");
		goto free_cdev;
	}

	ret = sysfs_create_link(&tte->cdev->device.kobj, &dev->kobj,
				"thermal_trip_event");
	if (ret) {
		dev_err(dev, "failed to create sysfs symlink\n");
		goto free_sysfs_files;
	}

	dev_info(dev, "cooling device registered.\n");
	return 0;

free_sysfs_files:
	sysfs_remove_files(&dev->kobj, tte_cdev_attr);
free_cdev:
	thermal_cooling_device_unregister(tte->cdev);
destroy_lock:
	mutex_destroy(&tte->event_timeout_lock);
	mutex_destroy(&tte->cur_state_lock);

	return ret;
}

static int thermal_trip_event_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct therm_trip_event *tte = dev_get_drvdata(dev);
	struct thermal_cooling_device *cdev = tte->cdev;

	/*
	 * There could be processes waiting for the event to happen while the
	 * driver is being removed. To have smooth removal, wake them up by
	 * updating the cur_state. The cooling state change here is not
	 * perceived by the thermal framework, but it's not a big deal as the
	 * cooling device is going to be destroyed soon.
	 */
	mutex_lock(&tte->cur_state_lock);
	tte->cur_state = CDEV_DESTROY;
	mutex_unlock(&tte->cur_state_lock);

	if (wq_has_sleeper(&tte->waitq_head)) {
		wake_up_interruptible_all(&tte->waitq_head);
		dev_dbg(dev, "THERMAL_EVENT_DESTROYED!\n");
	}

	sysfs_remove_link(&cdev->device.kobj, "thermal_trip_event");
	sysfs_remove_files(&dev->kobj, tte_cdev_attr);
	thermal_cooling_device_unregister(cdev);
	mutex_destroy(&tte->event_timeout_lock);
	mutex_destroy(&tte->cur_state_lock);

	return 0;
}

static const struct of_device_id thermal_trip_event_of_match[] = {
	{
		.compatible = "thermal-trip-event",
	},
	{},
};
MODULE_DEVICE_TABLE(of, thermal_trip_event_of_match);

static struct platform_driver thermal_trip_event_driver = {
	.driver = {
		.name = "thermal-trip-event",
		.owner = THIS_MODULE,
		.of_match_table = thermal_trip_event_of_match,
	},
	.probe = thermal_trip_event_probe,
	.remove = thermal_trip_event_remove,
};

module_platform_driver(thermal_trip_event_driver);

MODULE_AUTHOR("Sreenivasulu Velpula <svelpula@nvidia.com>");
MODULE_AUTHOR("Yi-Wei Wang <yiweiw@nvidia.com>");
MODULE_DESCRIPTION("Thermal Trip Event Driver");
MODULE_LICENSE("GPL v2");
