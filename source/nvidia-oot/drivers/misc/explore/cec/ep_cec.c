// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pm.h>
#include <linux/uaccess.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/poll.h>

#include "ep_cec.h"
#include "../audio/ep92hx.h"

/* Define flags based on kernel version */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0) && \
	LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)
#define NV_I2C_DRIVER_STRUCT_HAS_PROBE_NEW
#endif

static struct device *earc_dev;

static const struct reg_default explore_cec_defaults[] = {
	/* System Control Registers (0x0100 - 0x0102) */
	{ CEC_REG_SYSTEM_CTRL, 0x00 },		/* System Control */
	{ CEC_REG_RETRY_TIME, 0x30 },		/* RETRY_TIME[3:0] */
	{ CEC_REG_FEATURE_CTRL, 0x01 },		/* Feature Control */

	/* Feature Enable Registers (0x0103 - 0x0104) */
	{ CEC_REG_FEATURE_EN_0_7, 0x00 },	/* Feature Enable 0-7 */
	{ CEC_REG_FEATURE_EN_8_15, 0x00 },	/* Feature Enable 8-15 */

	/* Interrupt and Command Registers (0x0105 - 0x0106) */
	{ CEC_REG_INTR_FLAGS, 0x00 },		/* Interrupt Flags */
	{ CEC_REG_CMD_CODE, 0x00 },		/* Command Code */

	/* High Level Mode: Device Topology Registers (0x0107 - 0x0108) */
	{ CEC_REG_PHYS_ADDR_HIGH, 0x00 },	/* Physical Address High Byte */
	{ CEC_REG_PHYS_ADDR_LOW, 0x00 },	/* Physical Address Low Byte */

	/* Firmware Revision (0x0400 - 0x0401) */
	{ CEC_REG_FW_REV_HIGH, 0x03 },		/* FW_REV[15:8] */
	{ CEC_REG_FW_REV_LOW, 0x26 },		/* FW_REV[7:0] */

	/* Command Parameter Buffer (0x0500 - 0x0510) */
	{ CEC_REG_CMD_START, 0x00 },		/* START */
	{ CEC_REG_CMD_SIZE, 0x00 },		/* SIZE */
	{ CEC_REG_CMD_OPCODE, 0x00 },		/* Opcode */
	{ CEC_REG_CMD_PARAM_0, 0x00 },		/* Param_0 */
	{ CEC_REG_CMD_PARAM_1, 0x00 },		/* Param_1 */
	{ CEC_REG_CMD_PARAM_2, 0x00 },		/* Param_2 */
	{ CEC_REG_CMD_PARAM_3, 0x00 },		/* Param_3 */
	{ CEC_REG_CMD_PARAM_4, 0x00 },		/* Param_4 */
	{ CEC_REG_CMD_PARAM_5, 0x00 },		/* Param_5 */
	{ CEC_REG_CMD_PARAM_6, 0x00 },		/* Param_6 */
	{ CEC_REG_CMD_PARAM_7, 0x00 },		/* Param_7 */
	{ CEC_REG_CMD_PARAM_8, 0x00 },		/* Param_8 */
	{ CEC_REG_CMD_PARAM_9, 0x00 },		/* Param_9 */
	{ CEC_REG_CMD_PARAM_10, 0x00 },		/* Param_10 */
	{ CEC_REG_CMD_PARAM_11, 0x00 },		/* Param_11 */
	{ CEC_REG_CMD_PARAM_12, 0x00 },		/* Param_12 */
	{ CEC_REG_CMD_PARAM_13, 0x00 },		/* Param_13 */

	/* Command Status (0x0600) */
	{ CEC_REG_CMD_STATUS, 0x00 },		/* STATUS */

	/* Event Status Buffer (0x0700 - 0x0711) */
	{ CEC_REG_EVT_START, 0x00 },		/* START */
	{ CEC_REG_EVT_SIZE, 0x00 },		/* SIZE */
	{ CEC_REG_EVT_OPCODE, 0x00 },		/* Opcode */
	{ CEC_REG_EVT_PARAM_0, 0x00 },		/* Param_0 */
	{ CEC_REG_EVT_PARAM_1, 0x00 },		/* Param_1 */
	{ CEC_REG_EVT_PARAM_2, 0x00 },		/* Param_2 */
	{ CEC_REG_EVT_PARAM_3, 0x00 },		/* Param_3 */
	{ CEC_REG_EVT_PARAM_4, 0x00 },		/* Param_4 */
	{ CEC_REG_EVT_PARAM_5, 0x00 },		/* Param_5 */
	{ CEC_REG_EVT_PARAM_6, 0x00 },		/* Param_6 */
	{ CEC_REG_EVT_PARAM_7, 0x00 },		/* Param_7 */
	{ CEC_REG_EVT_PARAM_8, 0x00 },		/* Param_8 */
	{ CEC_REG_EVT_PARAM_9, 0x00 },		/* Param_9 */
	{ CEC_REG_EVT_PARAM_10, 0x00 },		/* Param_10 */
	{ CEC_REG_EVT_PARAM_11, 0x00 },		/* Param_11 */
	{ CEC_REG_EVT_PARAM_12, 0x00 },		/* Param_12 */
	{ CEC_REG_EVT_PARAM_13, 0x00 },		/* Param_13 */
	{ CEC_REG_EVT_END, 0x00 },		/* END */
};

static bool explore_cec_writeable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CEC_REG_SYSTEM_CTRL ... CEC_REG_PHYS_ADDR_LOW:
	case CEC_REG_CMD_START ... CEC_REG_CMD_PARAM_13:
		return true;
	default:
		return false;
	}
};

static int explore_cec_file_open(struct inode *inode, struct file *file)
{
	struct miscdevice *miscdev = file->private_data;
	struct explore_cec *cec_data = container_of(miscdev, struct explore_cec, misc_dev);

	pr_debug("%s: Explore cec file opened\n", __func__);
	file->private_data = cec_data;
	return 0;
}

static int explore_cec_file_release(struct inode *inode, struct file *file)
{
	pr_debug("%s: Explore cec file closed\n", __func__);
	return 0;
}

static ssize_t ep_cec_read_message(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct explore_cec *cec_data = file->private_data;
	struct ep_cec_message cec_msg = {0};
	int ret;

	if (!cec_data) {
		pr_err("Invalid cec_data pointer\n");
		return -EINVAL;
	}

	/* Check if user buffer is large enough */
	if (count < sizeof(struct ep_cec_message)) {
		dev_err(&cec_data->client->dev, "Buffer too small, need %zu bytes\n",
			sizeof(struct ep_cec_message));
		return -EINVAL;
	}

	mutex_lock(&cec_data->cec_lock);
	ret = kfifo_get(&cec_data->cec_msg_fifo, &cec_msg);
	mutex_unlock(&cec_data->cec_lock);
	if (ret > 0) {
		if (copy_to_user(buf, &cec_msg, sizeof(cec_msg))) {
			dev_err(&cec_data->client->dev, "Failed to copy to user\n");
			ret = -EFAULT;
		} else {
			ret = sizeof(cec_msg);
		}
	} else if (ret == 0) {
		dev_info(&cec_data->client->dev, "CEC message ring buffer is empty\n");
		ret = 0;
	} else {
		dev_err(&cec_data->client->dev, "Failed to read CEC message from ring buffer\n");
		ret = -EFAULT;
	}

	return ret;
}

static __poll_t explore_cec_poll(struct file *file, poll_table *wait)
{
	struct explore_cec *cec_data = file->private_data;
	__poll_t mask = 0;

	if (!cec_data) {
		pr_err("Invalid cec_data pointer\n");
		return POLLERR;
	}

	/* Add wait queue to poll table */
	poll_wait(file, &cec_data->cec_wait_queue, wait);

	mutex_lock(&cec_data->cec_lock);
	/* Check if there are messages in the fifo */
	if (kfifo_len(&cec_data->cec_msg_fifo) > 0)
		mask |= POLLIN | POLLRDNORM;
	mutex_unlock(&cec_data->cec_lock);

	return mask;
}

static ssize_t ep_cec_dump_registers(struct file *file, char __user *buf)
{
	struct explore_cec *cec_data = file->private_data;
	char *tmp_buf = NULL;
	unsigned int val;
	int len = 0;
	int ret = 0;

	if (cec_data == NULL || cec_data->client == NULL) {
		pr_err("Invalid cec_data or client pointer\n");
		return -EINVAL;
	}

	tmp_buf = kmalloc(CEC_REG_DUMP_SIZE, GFP_KERNEL);
	if (tmp_buf == NULL) {
		dev_err(&cec_data->client->dev, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	for (int i = 0; i < ARRAY_SIZE(explore_cec_defaults); i++) {
		ret = regmap_read(cec_data->regmap,
				  explore_cec_defaults[i].reg, &val);
		if (ret) {
			dev_err(&cec_data->client->dev, "Failed to read reg 0x%x: %d\n",
				explore_cec_defaults[i].reg, ret);
			len += snprintf(tmp_buf + len, CEC_REG_DUMP_SIZE - len,
					"Reg 0x%04x: failed to read\n",
					explore_cec_defaults[i].reg);
		} else {
			len += snprintf(tmp_buf + len, CEC_REG_DUMP_SIZE - len,
					"Reg 0x%04x: 0x%02x\n",
					explore_cec_defaults[i].reg, val);
		}
		if (len >= CEC_REG_DUMP_SIZE) {
			dev_err(&cec_data->client->dev, "Buffer too small, stop dumping\n");
			break;
		}
	}

	ret = copy_to_user(buf, tmp_buf, len);
	if (ret != 0) {
		dev_err(&cec_data->client->dev, "Failed to copy to user\n");
		ret = -EFAULT;
	} else {
		ret = len;
	}

	kfree(tmp_buf);
	return ret;
}

static long explore_cec_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret;

	switch (cmd) {
	case EP_CEC_IOCTL_DUMP_REG:
		ret = ep_cec_dump_registers(file, (char __user *)arg);
		break;
	default:
		pr_warn("Invalid ioctl command\n");
		return -EINVAL;
	}

	return ret;
}

static struct file_operations explore_cec_fops = {
	.owner		= THIS_MODULE,
	.open		= explore_cec_file_open,
	.release	= explore_cec_file_release,
	.unlocked_ioctl	= explore_cec_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	=  explore_cec_ioctl,
#endif
	.read		= ep_cec_read_message,
	.poll		= explore_cec_poll,
};

static const struct regmap_config explore_cec_regmap_config = {
	.reg_bits		= 16,
	.val_bits		= 8,
	.max_register		= CEC_REG_MAX,
	.cache_type		= REGCACHE_NONE,
	.reg_defaults		= explore_cec_defaults,
	.num_reg_defaults	= ARRAY_SIZE(explore_cec_defaults),
	.writeable_reg		= explore_cec_writeable,
};

static bool ep_cec_wait_value(struct regmap *regmap, unsigned int reg,
			      unsigned int value, unsigned int timeout_ms)
{
	unsigned long deadline = jiffies + msecs_to_jiffies(timeout_ms);
	unsigned int val;

	do {
		if (regmap_read(regmap, reg, &val)) {
			pr_err("Failed to read register: 0x%x\n", reg);
			return false;
		}
		if (val == value)
			return true;
		cpu_relax();
		udelay(10);
	} while (!time_after_eq(jiffies, deadline));

	pr_err("Timeout waiting for register: 0x%x, value: 0x%x\n", reg, value);
	return false;
}

static ssize_t ep_cec_disable_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	bool is_disabled;
	int ret;

	if (earc_dev == NULL) {
		dev_err(dev, "eARC device not found\n");
		return -ENODEV;
	}

	ret = get_cec_disable(earc_dev, &is_disabled);
	if (ret < 0) {
		dev_err(dev, "Failed to get CEC disable status: %d\n", ret);
		return ret;
	}

	return sysfs_emit(buf, "%d\n", is_disabled ? 1 : 0);
}

#ifdef EP_CEC_DEBUG
static ssize_t ep_cec_disable_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct explore_cec *cec_data = dev_get_drvdata(dev);
	int ret;
	bool disable;

	ret = kstrtobool(buf, &disable);
	if (ret)
		return ret;

	if (earc_dev == NULL) {
		dev_err(dev, "eARC device not found\n");
		return -ENODEV;
	}

	mutex_lock(&cec_data->cec_lock);
	ret = set_cec_disable(earc_dev, disable);
	if (ret != 0) {
		dev_err(dev, "Failed to set cec disable\n");
		goto out_unlock;
	}
	dev_info(dev, "Explore CEC %s\n", disable ? "disabled" : "enabled");
	ret = count;

out_unlock:
	mutex_unlock(&cec_data->cec_lock);
	return ret;
}
#endif /* EP_CEC_DEBUG */

static ssize_t ep_cec_physical_addr_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	struct explore_cec *cec_data = dev_get_drvdata(dev);

	return sysfs_emit(buf, "0x%x\n", cec_data->physical_addr);
}

static ssize_t ep_cec_physical_addr_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct explore_cec *cec_data = dev_get_drvdata(dev);
	int ret;
	u16 phys_addr;

	ret = kstrtou16(buf, 0, &phys_addr);
	if (ret != 0)
		return ret;

	if (phys_addr > CEC_MAX_PHYSICAL_ADDR) {
		dev_warn(dev, "Invalid physical address: 0x%x\n", phys_addr);
		return -EINVAL;
	}

	mutex_lock(&cec_data->cec_lock);
	/* Write high byte */
	ret = regmap_write(cec_data->regmap, CEC_REG_PHYS_ADDR_HIGH, (phys_addr >> 8) & 0xFF);
	if (ret != 0) {
		dev_err(dev, "Failed to write physical address high byte\n");
		goto out_unlock;
	}
	/* Write low byte */
	ret = regmap_write(cec_data->regmap, CEC_REG_PHYS_ADDR_LOW, phys_addr & 0xFF);
	if (ret != 0) {
		dev_err(dev, "Failed to write physical address low byte\n");
		goto out_unlock;
	}
	cec_data->physical_addr = phys_addr;
	dev_info(dev, "Set EP CEC physical address to 0x%x\n", phys_addr);
	ret = count;

out_unlock:
	mutex_unlock(&cec_data->cec_lock);
	return ret;
}

static ssize_t ep_cec_logical_addr_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct explore_cec *cec_data = dev_get_drvdata(dev);

	return sysfs_emit(buf, "0x%x\n", cec_data->logical_addr);
}

static ssize_t ep_cec_logical_addr_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct explore_cec *cec_data = dev_get_drvdata(dev);
	int ret;
	u8 logical_addr;

	ret = kstrtou8(buf, 0, &logical_addr);
	if (ret != 0)
		return ret;

	if (logical_addr > CEC_MAX_LOGICAL_ADDR) {
		dev_warn(dev, "Invalid logical address: 0x%x\n", logical_addr);
		return -EINVAL;
	}

	mutex_lock(&cec_data->cec_lock);
	ret = regmap_write(cec_data->regmap, CEC_REG_CMD_START, logical_addr);
	if (ret != 0) {
		dev_err(dev, "Failed to set logical address\n");
		goto out_unlock;
	}
	/* Execute Set Logical Address command */
	ret = regmap_write(cec_data->regmap, CEC_REG_CMD_CODE, CEC_CMD_SET_LOGICAL_ADDR);
	if (ret != 0) {
		dev_err(dev, "Failed to execute Set Logical Address command\n");
		goto out_unlock;
	}
	/* Wait for any previous command to complete */
	if (!ep_cec_wait_value(cec_data->regmap, CEC_REG_CMD_CODE, 0x00, CEC_CMD_DELAY_MS)) {
		ret = -ETIMEDOUT;
		goto out_unlock;
	}

	cec_data->logical_addr = logical_addr;
	dev_info(dev, "Set EP CEC logical address to 0x%x\n", logical_addr);
	ret = count;

out_unlock:
	mutex_unlock(&cec_data->cec_lock);
	return ret;
}

static struct device *get_earc_device(void)
{
	struct device_node *np;
	struct i2c_client *client;

	np = of_find_compatible_node(NULL, NULL, "nv,ep-earc");
	if (np == NULL) {
		pr_err("Failed to find eARC device tree node\n");
		return NULL;
	}

	client = of_find_i2c_device_by_node(np);
	of_node_put(np);

	if (client == NULL) {
		pr_err("Failed to find I2C device for eARC\n");
		return NULL;
	}

	return &client->dev;
}

static int explore_cec_hw_init(struct explore_cec *cec_data)
{
	int ret;

	ret = regmap_write(cec_data->regmap, CEC_REG_FEATURE_CTRL, CEC_FEATURE_INIT_VALUE);
	if (ret != 0) {
		dev_err(&cec_data->client->dev, "Failed to set feature control\n");
		return ret;
	}

	if (!ep_cec_wait_value(cec_data->regmap, CEC_REG_CMD_CODE, 0x00, CEC_CMD_DELAY_MS))
		return -ETIMEDOUT;

	earc_dev = get_earc_device();
	if (earc_dev == NULL) {
		dev_err(&cec_data->client->dev, "Failed to get eARC device\n");
		return -ENODEV;
	}
	ret = set_cec_disable(earc_dev, true);
	if (ret != 0) {
		dev_err(&cec_data->client->dev, "Failed to set cec disable\n");
		return ret;
	}

	return 0;
}

static void ep_cec_clear_ecf_flag(struct explore_cec *cec_data)
{
	if (regmap_write(cec_data->regmap, CEC_REG_INTR_FLAGS, 0x00) != 0)
		dev_err(&cec_data->client->dev, "Failed to clear ECF flag\n");
}

static irqreturn_t ep_cec_thread_handler(int irq, void *data)
{
	struct explore_cec *cec_data = data;
	struct ep_cec_message cec_msg = {0};
	unsigned int intr_flags;
	int ret;

	mutex_lock(&cec_data->cec_lock);
	/* Read interrupt flags to check ECF */
	ret = regmap_read(cec_data->regmap, CEC_REG_INTR_FLAGS, &intr_flags);
	if (ret != 0) {
		dev_err(&cec_data->client->dev, "Failed to read interrupt flags\n");
		/* If read failed, clear ECF flag and drop this message */
		ep_cec_clear_ecf_flag(cec_data);
		goto out_unlock;
	}

	if ((intr_flags & CEC_INT_ECF_MASK) != 0) {
		/* Read CEC message (18 bytes) */
		ret = regmap_bulk_read(cec_data->regmap, CEC_REG_EVT_START,
				       cec_msg.data,
				       sizeof(cec_msg.data));
		if (ret) {
			dev_err(&cec_data->client->dev, "Failed to read CEC message\n");
			/* If read failed, clear ECF flag and drop this message */
			ep_cec_clear_ecf_flag(cec_data);
			goto out_unlock;
		}

		ep_cec_clear_ecf_flag(cec_data);
		if (kfifo_put(&cec_data->cec_msg_fifo, cec_msg) == 0)
			dev_err(&cec_data->client->dev, "CEC message ring buffer is full\n");
		else
			wake_up_interruptible(&cec_data->cec_wait_queue);
	} else {
		dev_err(&cec_data->client->dev, "EP CEC GPIO interrupt triggered, but ECF flag is not set\n");
	}

out_unlock:
	mutex_unlock(&cec_data->cec_lock);
	return IRQ_HANDLED;
}

static int ep_cec_setup_gpio_irq(struct explore_cec *cec_data)
{
	struct device *dev = &cec_data->client->dev;
	int irq;
	int ret;

	/* Get GPIO from device tree */
	cec_data->gpiod = devm_gpiod_get(dev, NULL, GPIOD_IN);
	if (IS_ERR(cec_data->gpiod)) {
		ret = PTR_ERR(cec_data->gpiod);
		if (ret == -ENOENT) {
			dev_err(dev, "failed to get gpio: %d\n", ret);
			return ret;
		}
	}

	irq = gpiod_to_irq(cec_data->gpiod);
	if (irq < 0) {
		ret = irq;
		dev_err(dev, "Unable to get irq number for GPIO, error %d\n", ret);
		return ret;
	}
	cec_data->irq = irq;

	ret = devm_request_threaded_irq(dev, cec_data->irq, NULL,
					ep_cec_thread_handler,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"ep_cec", cec_data);
	if (ret < 0) {
		dev_err(dev, "Unable to claim irq %d; error %d\n",
			cec_data->irq, ret);
		return ret;
	}

	dev_info(dev, "EP CEC GPIO interrupt setup successfully (IRQ: %d)\n", irq);
	return 0;
}

#ifdef EP_CEC_DEBUG
static DEVICE_ATTR_RW(ep_cec_disable);
#else /* EP_CEC_DEBUG */
static DEVICE_ATTR_RO(ep_cec_disable);
#endif /* EP_CEC_DEBUG */

static DEVICE_ATTR_RW(ep_cec_physical_addr);
static DEVICE_ATTR_RW(ep_cec_logical_addr);

/* Sysfs attribute group */
static struct attribute *ep_cec_attrs[] = {
	&dev_attr_ep_cec_disable.attr,
	&dev_attr_ep_cec_physical_addr.attr,
	&dev_attr_ep_cec_logical_addr.attr,
	NULL,
};

static const struct attribute_group ep_cec_attr_group = {
	.attrs = ep_cec_attrs,
};

static int explore_cec_probe(struct i2c_client *client)
{
	struct explore_cec *cec_data = NULL;
	int ret;

	cec_data = devm_kzalloc(&client->dev, sizeof(struct explore_cec), GFP_KERNEL);
	if (!cec_data)
		return -ENOMEM;

	cec_data->client = client;
	i2c_set_clientdata(client, cec_data);

	cec_data->regmap = devm_regmap_init_i2c(client, &explore_cec_regmap_config);
	if (IS_ERR(cec_data->regmap)) {
		dev_err(&client->dev, "Failed to initialize regmap\n");
		return PTR_ERR(cec_data->regmap);
	}

	cec_data->misc_dev.minor = MISC_DYNAMIC_MINOR;
	cec_data->misc_dev.name = CEC_DEVICE_NAME;
	cec_data->misc_dev.fops = &explore_cec_fops;
	cec_data->misc_dev.parent = &client->dev;
	ret = misc_register(&cec_data->misc_dev);
	if (ret) {
		dev_err(&client->dev, "Failed to register misc device\n");
		return ret;
	}

	ret = explore_cec_hw_init(cec_data);
	if (ret) {
		dev_err(&client->dev, "Failed to initialize hardware\n");
		goto out_misc;
	}

	/* Create sysfs group */
	ret = sysfs_create_group(&client->dev.kobj, &ep_cec_attr_group);
	if (ret) {
		dev_err(&client->dev, "Failed to create ep_cec sysfs group\n");
		goto out_misc;
	}

	/* Set up GPIO interrupt */
	ret = ep_cec_setup_gpio_irq(cec_data);
	if (ret) {
		dev_err(&client->dev, "Failed to setup GPIO interrupt\n");
		goto out_sysfs;
	}

	INIT_KFIFO(cec_data->cec_msg_fifo);
	mutex_init(&cec_data->cec_lock);
	init_waitqueue_head(&cec_data->cec_wait_queue);

	dev_info(&client->dev, "Explore CEC driver initialized successfully\n");
	return 0;

out_sysfs:
	sysfs_remove_group(&client->dev.kobj, &ep_cec_attr_group);

out_misc:
	misc_deregister(&cec_data->misc_dev);
	return ret;
}

static int explore_cec_suspend(struct device *dev)
{
	struct explore_cec *cec_data = dev_get_drvdata(dev);

	if (enable_irq_wake(cec_data->irq) != 0)
		dev_err(dev, "Failed to enable ep cec irq wake\n");

	dev_info(dev, "Explore cec suspend\n");
	return 0;
}

static int explore_cec_resume(struct device *dev)
{
	struct explore_cec *cec_data = dev_get_drvdata(dev);

	if (disable_irq_wake(cec_data->irq) != 0)
		dev_err(dev, "Failed to disable ep cec irq wake\n");

	dev_info(dev, "Explore cec resume\n");
	return 0;
}

static SIMPLE_DEV_PM_OPS(explore_cec_pm_ops, explore_cec_suspend, explore_cec_resume);

static struct of_device_id explore_cec_of_match[] = {
	{ .compatible = "nvidia,ep-cec"},
	{},
};
MODULE_DEVICE_TABLE(of, explore_cec_of_match);

static const struct i2c_device_id explore_cec_i2c_id[] = {
	{ "ep92hx", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, explore_cec_i2c_id);

/*
 * Device removal function. Note that we don't need to free cec_data memory here
 * because it was allocated using devm_kzalloc() in the probe function. The kernel's
 * device resource management system will automatically free this memory when the
 * device is removed.
 */
static void explore_cec_remove(struct i2c_client *client)
{
	struct explore_cec *cec_data = i2c_get_clientdata(client);

	mutex_destroy(&cec_data->cec_lock);
	sysfs_remove_group(&client->dev.kobj, &ep_cec_attr_group);
	misc_deregister(&cec_data->misc_dev);
	earc_dev = NULL;
	dev_info(&client->dev, "Explore CEC driver removed\n");
}

static struct i2c_driver explore_cec_driver = {
	.driver = {
		.owner		= THIS_MODULE,
		.name		= CEC_DEVICE_NAME,
		.of_match_table	= explore_cec_of_match,
#ifdef CONFIG_PM
		.pm 		= &explore_cec_pm_ops,
#endif /* CONFIG_PM */
	},
#ifdef NV_I2C_DRIVER_STRUCT_HAS_PROBE_NEW
	.probe_new		= explore_cec_probe,
#else
	.probe			= explore_cec_probe,
#endif /* NV_I2C_DRIVER_STRUCT_HAS_PROBE_NEW */
	.remove			= explore_cec_remove,
	.id_table		= explore_cec_i2c_id,
};

module_i2c_driver(explore_cec_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Explore CEC Driver");
MODULE_AUTHOR("Devin Dai <wedai@nvidia.com>");
MODULE_SOFTDEP("pre: ep92hx");
