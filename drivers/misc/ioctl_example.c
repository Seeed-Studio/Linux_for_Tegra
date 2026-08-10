// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <nvidia/conftest.h>

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>

/* Define IOCTL commands */
#define IOCTL_SET_VALUE _IOW('k', 1, int)
#define IOCTL_GET_VALUE _IOR('k', 2, int)

static int value;
static dev_t dev_num;
static struct cdev cdev;
static struct class *dev_class;

static int device_open(struct inode *inode, struct file *file)
{
	pr_info("ioctl_example: Device opened\n");
	return 0;
}

static int device_release(struct inode *inode, struct file *file)
{
	pr_info("ioctl_example: Device closed\n");
	return 0;
}

static long device_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;

	switch (cmd) {
	case IOCTL_SET_VALUE:
		if (copy_from_user(&value, (int *)arg, sizeof(int)))
			ret = -EFAULT;
		break;

	case IOCTL_GET_VALUE:
		if (copy_to_user((int *)arg, &value, sizeof(int)))
			ret = -EFAULT;
		break;

	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

static struct file_operations fops = {
#if !defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG)
	.owner = THIS_MODULE,
#endif
	.open = device_open,
	.release = device_release,
	.unlocked_ioctl = device_ioctl,
};

static int __init ioctl_example_init(void)
{
	/* Allocate major and minor numbers */
	alloc_chrdev_region(&dev_num, 0, 1, "ioctl_example");

	/* Initialize the character device structure */
	cdev_init(&cdev, &fops);
	cdev_add(&cdev, dev_num, 1);

	/* Create a device class */

#if defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG)
	dev_class = class_create("ioctl_example");
#else
	dev_class = class_create(THIS_MODULE, "ioctl_example");
#endif
	device_create(dev_class, NULL, dev_num, NULL, "ioctl_example");

	pr_info("ioctl_example: Module loaded\n");
	return 0;
}

static void __exit ioctl_example_exit(void)
{
	device_destroy(dev_class, dev_num);
	class_destroy(dev_class);
	cdev_del(&cdev);
	unregister_chrdev_region(dev_num, 1);
	pr_info("ioctl_example: Module unloaded\n");
}

module_init(ioctl_example_init);
module_exit(ioctl_example_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Simple IOCTL Example");

