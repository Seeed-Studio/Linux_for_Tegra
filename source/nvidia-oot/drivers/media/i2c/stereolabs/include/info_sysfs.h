/**
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/types.h>
#include <linux/kobject.h>


#ifndef __INFO_SYSFS_H__
#define __INFO_SYSFS_H__

extern int dser_read_video_lock(int channel, int zedx_id);
extern int dser_read_link_lock(int channel, int zedx_id);
extern int ser_read_video_conf(int zedx_id);

/**
 * struct info_sysfs - Structure that contains information to print via ioctl
 * @parent_kobj: struct kobject - kobject associated with the parent folder
 * @info_kobj: struct kobject - kobject of the zed_info folder
 * @sync_sensor_index: u32 - sensor index found in the dts
 * @eeprom_id_addr: unsigned long - eeprom address of the camera
 * @serial_number: unsigned long - serial number of the camera
 * @video_id: int - id of the /dev/videoX device
 * @name: char* - name of the v4l2 subdev, looks like "subdev zedx 30-0010"
 * @tc_dev: struct tegracam_device* - Reference to the tegra device associated
 * 
 * Sensor information structure for the sysfs entry
 */
struct info_sysfs {
	/* to declare the sensor in the /sys filesystem */
    struct kobject parent_kobj;
	struct kobject info_kobj;

	/* info to print for all sensors */
	u32 sync_sensor_index; /* right from the dts */
	unsigned int eeprom_id_addr; /* eeprom address */
	unsigned long serial_number; /* ZED X HDR serial number */
	int video_id; /* /dev/videoX id */
	char* name; /* v4l2 subdev name */
	struct tegracam_device *tc_dev;
	u8 model_id;
	int gmsl_port;
	unsigned int acc_addr; /* Accelerometer i2c address */
	unsigned int gyro_addr; /* Gyroscope i2c address */
	u8 awb;
	u8 video_lock;
	u8 channel;
	int link_lock;
	int zedx_id;
	int status; // Flag indicating if the serializer is properly configured for video output
};

/**
 * Function that prints the content of the NOR Flash of the ISX031
 */
static ssize_t sysfs_info_get(struct kobject *kobj,
		struct attribute *attr, char *buf)
{
	struct info_sysfs *priv = container_of(kobj, struct info_sysfs, info_kobj);

	if (!strcmp(attr->name, "name"))
		return sprintf(buf, "%s\n", (priv->name));

	else if (!strcmp(attr->name, "sync_sensor"))
		return sprintf(buf, "%u\n", priv->sync_sensor_index);

	else if (!strcmp(attr->name, "video_id"))
		return sprintf(buf, "/dev/video%u\n", priv->video_id);

	else if (!strcmp(attr->name, "serial"))
		return sprintf(buf, "%lu\n", priv->serial_number);

	else if (!strcmp(attr->name, "i2c_bus"))
		return sprintf(buf, "%d\n", priv->tc_dev->client->adapter->nr);

	else if (!strcmp(attr->name, "hex_i2c_addr"))
		return sprintf(buf, "%x\n", priv->tc_dev->client->addr);

	else if (!strcmp(attr->name, "model_id"))
		return sprintf(buf, "%d\n", priv->model_id);
	
	else if (!strcmp(attr->name, "awb"))
		return sprintf(buf, "%d\n", priv->awb);

	else if (!strcmp(attr->name, "acc_addr"))
		return sprintf(buf, "%d\n", priv->acc_addr);

	else if (!strcmp(attr->name, "gyro_addr"))
		return sprintf(buf, "%d\n", priv->gyro_addr);

	else if (!strcmp(attr->name, "gmsl_port"))
		return sprintf(buf, "%d\n", priv->gmsl_port);

	else if (!strcmp(attr->name, "link_lock")){
		int ret = dser_read_link_lock(priv->channel, priv->zedx_id);
		
		if(ret >= 0) // if ret < 0 (read has failed) do not overwrite existing value
			priv->link_lock = ret;
		if (ret == -255) // link not supported
			priv->link_lock = -1;

		return sprintf(buf, "%d\n", priv->link_lock);
	}

	else if (!strcmp(attr->name, "video_lock")){
		int ret = dser_read_video_lock(priv->channel, priv->zedx_id);
		// if ret < 0 (read has failed) do not overwrite existing value
		if(ret >= 0)
			priv->video_lock = ret;
		return sprintf(buf, "%d\n", priv->video_lock);
	}

	else if (!strcmp(attr->name, "status")){
		priv->status = ser_read_video_conf(priv->zedx_id);
		return sprintf(buf, "%d\n", priv->status);
	}

	return 0;
}

/**
 * Writes are not supported
 */
static ssize_t sysfs_info_set(struct kobject *kobj,
		struct attribute *attr,const char *buf, size_t count)
{
	/* Fake write */
	return count;
}

/**
 * sysfs functions that acts on the attributes
 */
static struct sysfs_ops info_sysfs_ops = {
    .show = sysfs_info_get,
	.store = sysfs_info_set,
};

/**
 * Sysfs attributes that uses the get and set functions.
 * These will appear as folders in
 * /sys/class/video4linux/videoX/zed_info/{serial, name, ...}
 */
static struct kobj_attribute serial_attribute =
	__ATTR(serial, 0444, NULL, NULL);

static struct kobj_attribute name_attribute =
	__ATTR(name, 0444, NULL, NULL);

static struct kobj_attribute sensor_attribute =
	__ATTR(sync_sensor, 0444, NULL, NULL);

static struct kobj_attribute video_attribute =
	__ATTR(video_id, 0444, NULL, NULL);

static struct kobj_attribute i2c_bus_attribute =
	__ATTR(i2c_bus, 0444, NULL, NULL);

static struct kobj_attribute i2c_addr_attribute =
	__ATTR(hex_i2c_addr, 0444, NULL, NULL);

static struct kobj_attribute model_attribute =
	__ATTR(model_id, 0444, NULL, NULL);

static struct kobj_attribute acc_addr_attribute =
	__ATTR(acc_addr, 0444, NULL, NULL);

static struct kobj_attribute gyro_addr_attribute =
	__ATTR(gyro_addr, 0444, NULL, NULL);

static struct kobj_attribute gmsl_port_attribute =
	__ATTR(gmsl_port, 0444, NULL, NULL);

static struct kobj_attribute awb_attribute =
	__ATTR(awb, 0444, NULL, NULL);

static struct kobj_attribute link_lock_attribute =
	__ATTR(link_lock, 0444, NULL, NULL);

static struct kobj_attribute video_lock_attribute =
	__ATTR(video_lock, 0444, NULL, NULL);

static struct kobj_attribute status_attribute =
	__ATTR(status, 0444, NULL, NULL);

/** 
 * Attribute array that contains all the files we want to display.
 * It is then reference in the struct attribute group that is itself
 * embedded in an attribute group array.
 * Finally, it is declared in the struct kobj_type that initializes
 * the sysfs kobject in the sensor driver probe.
 */
static struct attribute *attribute_array[] = {
	&serial_attribute.attr,
	&name_attribute.attr,
	&sensor_attribute.attr,
	&video_attribute.attr,
	&i2c_bus_attribute.attr,
	&i2c_addr_attribute.attr,
	&model_attribute.attr,
	&acc_addr_attribute.attr,
	&gyro_addr_attribute.attr,
	&gmsl_port_attribute.attr,
	&awb_attribute.attr,
	&link_lock_attribute.attr,
	&video_lock_attribute.attr,
	&status_attribute.attr,
	NULL,
};

/**
 * We need to treat the case with L4T 32.7: kernel version is 4.9,
 * default_groups were not a thing back then.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)

static struct kobj_type zed_info_kobj_type = {
	.sysfs_ops = &info_sysfs_ops,
	.default_attrs = attribute_array,
};

#else

static struct attribute_group my_little_group = {
	.attrs = attribute_array,
};
static const struct attribute_group *info_group_array[] = {
	&my_little_group,
};

static struct kobj_type zed_info_kobj_type = {
	.sysfs_ops = &info_sysfs_ops,
	.default_groups = info_group_array,
};

#endif

#endif
