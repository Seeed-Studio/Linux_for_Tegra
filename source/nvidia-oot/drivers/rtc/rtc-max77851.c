// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * RTC driver for Maxim MAX77851
 */

#include <nvidia/conftest.h>

#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/rtc.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mfd/max77851.h>
#include <linux/irqdomain.h>
#include <linux/regmap.h>

#define MAX77851_I2C_ADDR_RTC		0x68

#define MAX77851_INVALID_I2C_ADDR	(-1)

/* Define non existing register */
#define MAX77851_INVALID_REG		(-1)

/* RTC Control Register */
#define BCD_EN_SHIFT			RTC_CFG0_BCD
#define BCD_EN_MASK			RTC_CFG0_BCD
#define MODEL24_SHIFT			RTC_CFG0_HRMODE
#define MODEL24_MASK			RTC_CFG0_HRMODE
/* RTC Update Register1 */
#define RTC_UDR_SHIFT			RTC_UPDATE_UDR
#define RTC_UDR_MASK			RTC_UPDATE_UDR
#define RTC_RBUDR_SHIFT			RTC_UPDATE_RBUDR
#define RTC_RBUDR_MASK			RTC_UPDATE_RBUDR
/* RTC Hour register */
#define HOUR_PM_SHIFT			RTC_HOUR_AMPM
#define HOUR_PM_MASK			RTC_HOUR_AMPM
/* RTC Alarm Enable */
#define ALARM_ENABLE_SHIFT		7
#define ALARM_ENABLE_MASK		ALARM_ENABLE_SHIFT

#define REG_RTC_NONE			0xdeadbeef

#define MAX77851_ALARM_ENABLE_VALUE	0x7f

#define MAX77851_ALARM_WORK_INTERVAL	msecs_to_jiffies(2000)
#define MAX77851_ALARM_DELAY_SECOND	10

enum {
	RTC_SEC = 0,
	RTC_MIN,
	RTC_HOUR,
	RTC_WEEKDAY,
	RTC_MONTH,
	RTC_YEAR,
	RTC_DATE,
	RTC_NR_TIME
};

struct max77851_rtc_driver_data {
	/* Minimum usecs needed for a RTC update */
	unsigned long delay;
	/* Mask used to read RTC registers value */
	u8 mask;
	/* Registers offset to I2C addresses map */
	const unsigned int *map;
	/* Has a separate alarm enable register */
	bool alarm_enable_reg;
	/* I2C address for RTC block */
	int rtc_i2c_addr;
	/* RTC interrupt via platform resource */
	bool rtc_irq_from_platform;
	/* Pending alarm status register */
	int alarm_pending_status_reg;
	/* RTC IRQ CHIP for regmap */
	const struct regmap_irq_chip *rtc_irq_chip;
	bool avoid_rtc_bulk_write;
};

struct max77851_rtc_info {
	struct device		*dev;
	struct i2c_client	*rtc;
	struct rtc_device	*rtc_dev;
	struct mutex		lock;

	struct regmap		*regmap;
	struct regmap		*rtc_regmap;

	const struct max77851_rtc_driver_data *drv_data;
	struct regmap_irq_chip_data *rtc_irq_data;

	int rtc_irq;
	int rtc_alarm1_virq;
	bool rtc_24hr_mode;
	bool rtc_binary_mode;
	bool shutdown;
};

enum MAX77851_RTC_OP {
	MAX77851_RTC_WRITE,
	MAX77851_RTC_READ,
};

/* These are not registers but just offsets that are mapped to addresses */
enum max77851_rtc_reg_offset {
	REG_RTC_INT = 0,
	REG_RTC_INTM,
	REG_RTC_CONTROLM,
	REG_RTC_CONTROL,
	REG_RTC_CONFIG,
	REG_RTC_UPDATE0,
	REG_RTC_SEC,
	REG_RTC_MIN,
	REG_RTC_HOUR,
	REG_RTC_WEEKDAY,
	REG_RTC_MONTH,
	REG_RTC_YEAR,
	REG_RTC_DATE,
	REG_ALARM1_SEC,
	REG_ALARM1_MIN,
	REG_ALARM1_HOUR,
	REG_ALARM1_WEEKDAY,
	REG_ALARM1_MONTH,
	REG_ALARM1_YEAR,
	REG_ALARM1_DATE,
	REG_ALARM2_SEC,
	REG_ALARM2_MIN,
	REG_ALARM2_HOUR,
	REG_ALARM2_WEEKDAY,
	REG_ALARM2_MONTH,
	REG_ALARM2_YEAR,
	REG_ALARM2_DATE,
	REG_RTC_AE1,
	REG_RTC_AE2,
	REG_RTC_END,
};

static const struct regmap_irq max77851_rtc_irqs[] = {
	/* RTC interrupts */
	REGMAP_IRQ_REG(0, 0, RTC_MSK_RTC60S_M),
	REGMAP_IRQ_REG(1, 0, RTC_MSK_RTCA1_M),
	REGMAP_IRQ_REG(2, 0, RTC_MSK_RTCA2_M),
	REGMAP_IRQ_REG(3, 0, RTC_MSK_RTC1S_M),
};

/* Maps RTC registers offset to the MAX77851 register addresses */
static const unsigned int max77851_map[REG_RTC_END] = {
	[REG_RTC_INT]        = RTC_INT_REG,
	[REG_RTC_INTM]       = RTC_MSK_REG,
	[REG_RTC_CONTROLM]   = RTC_CFG0M_REG,
	[REG_RTC_CONTROL]    = RTC_CFG0_REG,
	[REG_RTC_CONFIG]     = RTC_CFG1_REG,
	[REG_RTC_UPDATE0]    = RTC_UPDATE_REG,
	[REG_RTC_SEC]	     = RTC_SEC_REG,
	[REG_RTC_MIN]	     = RTC_MIN_REG,
	[REG_RTC_HOUR]	     = RTC_HOUR_REG,
	[REG_RTC_WEEKDAY]    = RTC_DOW_REG,
	[REG_RTC_MONTH]      = RTC_MONTH_REG,
	[REG_RTC_YEAR]	     = RTC_YEAR_REG,
	[REG_RTC_DATE]	     = RTC_DOM_REG,
	[REG_ALARM1_SEC]     = RTC_SECA1_REG,
	[REG_ALARM1_MIN]     = RTC_MINA1_REG,
	[REG_ALARM1_HOUR]    = RTC_HOURA1_REG,
	[REG_ALARM1_WEEKDAY] = RTC_DOWA1_REG,
	[REG_ALARM1_MONTH]   = RTC_MONTHA1_REG,
	[REG_ALARM1_YEAR]    = RTC_YEARA1_REG,
	[REG_ALARM1_DATE]    = RTC_DOMA1_REG,
	[REG_ALARM2_SEC]     = RTC_SECA2_REG,
	[REG_ALARM2_MIN]     = RTC_MINA2_REG,
	[REG_ALARM2_HOUR]    = RTC_HOURA2_REG,
	[REG_ALARM2_WEEKDAY] = RTC_DOWA2_REG,
	[REG_ALARM2_MONTH]   = RTC_MONTHA2_REG,
	[REG_ALARM2_YEAR]    = RTC_YEARA2_REG,
	[REG_ALARM2_DATE]    = RTC_DOMA2_REG,
	[REG_RTC_AE1]	     = RTC_AE1_REG,
	[REG_RTC_AE2]	     = RTC_AE2_REG,
};

static const struct regmap_irq_chip max77851_rtc_irq_chip = {
	.name		= "max77851-rtc",
	.status_base	= RTC_INT_REG,
	.mask_base	= RTC_MSK_REG,
	.num_regs	= 1,
	.irqs		= max77851_rtc_irqs,
	.num_irqs	= ARRAY_SIZE(max77851_rtc_irqs),
};

static const struct max77851_rtc_driver_data max77851_drv_data = {
	.delay = 200,
	.mask  = 0xff,
	.map   = max77851_map,
	.alarm_enable_reg  = true,
	.rtc_irq_from_platform = false,
	.alarm_pending_status_reg = TOP_STAT0_REG,
	.rtc_i2c_addr = MAX77851_I2C_ADDR_RTC,
	.rtc_irq_chip = &max77851_rtc_irq_chip,
};

static inline int _regmap_bulk_write(struct max77851_rtc_info *info,
		unsigned int reg, void *val, int len)
{
	int ret = 0;

	if (!info->drv_data->avoid_rtc_bulk_write) {
		/* RTC registers support sequential writing */
		ret = regmap_bulk_write(info->rtc_regmap, reg, val, len);
	} else {
		/* Power registers support register-data pair writing */
		u8 *src = (u8 *)val;
		int i;

		for (i = 0; i < len; i++) {
			ret = regmap_write(info->rtc_regmap, reg, *src++);
			if (ret < 0)
				break;
			reg++;
		}
	}
	if (ret < 0)
		dev_err(info->dev, "%s() failed, e %d\n", __func__, ret);

	return ret;
}

static void max77851_rtc_data_to_tm(u8 *data, struct rtc_time *tm,
				    struct max77851_rtc_info *info)
{
	u8 mask = info->drv_data->mask;

	tm->tm_sec = data[RTC_SEC] & mask;
	tm->tm_min = data[RTC_MIN] & mask;
	if (info->rtc_24hr_mode) {
		tm->tm_hour = data[RTC_HOUR] & 0x1f;
	} else {
		tm->tm_hour = data[RTC_HOUR] & 0x0f;
		if (data[RTC_HOUR] & HOUR_PM_MASK)
			tm->tm_hour += 12;
	}

	tm->tm_wday = ffs(data[RTC_WEEKDAY] & mask) - 1;
	tm->tm_mday = data[RTC_DATE] & 0x1f;
	tm->tm_mon = (data[RTC_MONTH] & 0x0f) - 1;
	tm->tm_year = (data[RTC_YEAR] & mask);
	tm->tm_yday = 0;
	tm->tm_isdst = 0;

	if (!info->drv_data->alarm_enable_reg)
		tm->tm_year += 100;
}

static int max77851_rtc_tm_to_data(struct rtc_time *tm, u8 *data,
				   struct max77851_rtc_info *info)
{
	data[RTC_SEC] = tm->tm_sec;
	data[RTC_MIN] = tm->tm_min;
	data[RTC_HOUR] = tm->tm_hour;
	data[RTC_WEEKDAY] = 1 << tm->tm_wday;
	data[RTC_DATE] = tm->tm_mday;
	data[RTC_MONTH] = tm->tm_mon + 1;

	if (info->drv_data->alarm_enable_reg) {
		data[RTC_YEAR] = tm->tm_year;
		return 0;
	}

	data[RTC_YEAR] = tm->tm_year > 100 ? (tm->tm_year - 100) : 0;

	if (tm->tm_year < 100) {
		dev_err(info->dev, "RTC cannot handle the year %d.\n",
			1900 + tm->tm_year);
		return -EINVAL;
	}

	return 0;
}

static int max77851_rtc_update(struct max77851_rtc_info *info,
			       enum MAX77851_RTC_OP op)
{
	int ret;
	unsigned int data;
	unsigned long delay = info->drv_data->delay;

	if (op == MAX77851_RTC_WRITE)
		data = RTC_UPDATE_UDR;
	else
		data = RTC_UPDATE_RBUDR;

	ret = regmap_write(info->rtc_regmap, info->drv_data->map[REG_RTC_UPDATE0],
					   data);
	if (ret < 0)
		dev_err(info->dev, "Fail to write update reg(ret=%d, data=0x%x)\n",
			ret, data);
	else {
		/* Minimum delay required before RTC update. */
		usleep_range(delay, delay * 2);
	}

	return ret;
}

static int max77851_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct max77851_rtc_info *info = dev_get_drvdata(dev);
	u8 data[RTC_NR_TIME];
	int ret;

	mutex_lock(&info->lock);

	ret = max77851_rtc_update(info, MAX77851_RTC_READ);
	if (ret < 0)
		goto out;

	ret = regmap_bulk_read(info->rtc_regmap,
			       info->drv_data->map[REG_RTC_SEC],
			       data, ARRAY_SIZE(data));
	if (ret < 0) {
		dev_err(info->dev, "Fail to read time reg(%d)\n", ret);
		goto out;
	}

	max77851_rtc_data_to_tm(data, tm, info);

	ret = rtc_valid_tm(tm);

out:
	mutex_unlock(&info->lock);
	return ret;
}

static int max77851_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct max77851_rtc_info *info = dev_get_drvdata(dev);
	u8 data[RTC_NR_TIME];
	int ret;

	ret = max77851_rtc_tm_to_data(tm, data, info);
	if (ret < 0)
		return ret;

	mutex_lock(&info->lock);

	ret = _regmap_bulk_write(info,
				 info->drv_data->map[REG_RTC_SEC],
				 data, ARRAY_SIZE(data));
	if (ret < 0) {
		dev_err(info->dev, "Fail to write time reg(%d)\n", ret);
		goto out;
	}

	ret = max77851_rtc_update(info, MAX77851_RTC_WRITE);

	ret = max77851_rtc_update(info, MAX77851_RTC_READ);

out:
	mutex_unlock(&info->lock);
	return ret;
}

static int max77851_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct max77851_rtc_info *info = dev_get_drvdata(dev);
	u8 data[RTC_NR_TIME];
	unsigned int val;
	const unsigned int *map = info->drv_data->map;
	int ret;

	mutex_lock(&info->lock);

	ret = max77851_rtc_update(info, MAX77851_RTC_READ);
	if (ret < 0)
		goto out;

	ret = regmap_bulk_read(info->rtc_regmap, map[REG_ALARM1_SEC],
			       data, ARRAY_SIZE(data));
	if (ret < 0) {
		dev_err(info->dev, "Fail to read alarm reg(%d)\n", ret);
		goto out;
	}

	max77851_rtc_data_to_tm(data, &alrm->time, info);

	alrm->enabled = 0;

	if (info->drv_data->alarm_enable_reg) {
		if (map[REG_RTC_AE1] == REG_RTC_NONE) {
			ret = -EINVAL;
			dev_err(info->dev,
				"alarm enable register not set(%d)\n", ret);
			goto out;
		}

		ret = regmap_read(info->rtc_regmap, map[REG_RTC_AE1], &val);
		if (ret < 0) {
			dev_err(info->dev,
				"fail to read alarm enable(%d)\n", ret);
			goto out;
		}

		if (val)
			alrm->enabled = 1;
	} else {
		dev_err(info->dev, "Fail (alarm_enable_reg)\n");
		goto out;
	}

	alrm->pending = 0;

	if (info->drv_data->alarm_pending_status_reg == MAX77851_INVALID_REG)
		goto out;

	ret = regmap_read(info->regmap,
			  info->drv_data->alarm_pending_status_reg, &val);
	if (ret < 0) {
		dev_err(info->dev,
			"Fail to read alarm pending status reg(%d)\n", ret);
		goto out;
	}

	if (val & (1 << 7)) /* RTCA1 */
		alrm->pending = 1;

out:
	mutex_unlock(&info->lock);
	return ret;
}

static int max77851_rtc_stop_alarm(struct max77851_rtc_info *info)
{
	int ret;
	const unsigned int *map = info->drv_data->map;

	if (!mutex_is_locked(&info->lock))
		dev_warn(info->dev, "%s: should have mutex locked\n", __func__);

	ret = max77851_rtc_update(info, MAX77851_RTC_READ);
	if (ret < 0)
		goto out;

	if (info->drv_data->alarm_enable_reg) {
		if (map[REG_RTC_AE1] == REG_RTC_NONE) {
			ret = -EINVAL;
			dev_err(info->dev,
				"alarm enable register not set(%d)\n", ret);
			goto out;
		}

		ret = regmap_write(info->rtc_regmap, map[REG_RTC_AE1], 0);
	} else {
		dev_err(info->dev, "Fail (alarm_enable_reg)\n");
		goto out;
	}

	if (ret < 0) {
		dev_err(info->dev, "Fail to write alarm reg(%d)\n", ret);
		goto out;
	}

	/* RTC Interrupt Mask */
	ret = regmap_update_bits(info->rtc_regmap, RTC_INT_REG, RTC_INT_RTCA1_I, RTC_INT_RTCA1_I);
	if (ret < 0) {
		dev_err(info->dev, "RTC register set failed: %d\n", ret);
		return ret;
	}

	ret = max77851_rtc_update(info, MAX77851_RTC_WRITE);
out:
	return ret;
}

static int max77851_rtc_start_alarm(struct max77851_rtc_info *info)
{
	int ret;
	const unsigned int *map = info->drv_data->map;

	if (!mutex_is_locked(&info->lock))
		dev_warn(info->dev, "%s: should have mutex locked\n", __func__);

	ret = max77851_rtc_update(info, MAX77851_RTC_READ);
	if (ret < 0)
		goto out;

	if (info->drv_data->alarm_enable_reg) {
		ret = regmap_write(info->rtc_regmap, map[REG_RTC_AE1],
				   MAX77851_ALARM_ENABLE_VALUE);
	} else {
		dev_err(info->dev, "Fail (alarm_enable_reg)\n");
		goto out;
	}

	if (ret < 0) {
		dev_err(info->dev, "Fail to write alarm reg(%d)\n", ret);
		goto out;
	}

	/* RTC Interrupt Unmask */
	ret = regmap_update_bits(info->rtc_regmap, RTC_INT_REG, RTC_INT_RTCA1_I, BIT_IS_ZERO);
	if (ret < 0) {
		dev_err(info->dev, "RTC register set failed: %d\n", ret);
		return ret;
	}

	ret = max77851_rtc_update(info, MAX77851_RTC_WRITE);
out:
	return ret;
}

static int max77851_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct max77851_rtc_info *info = dev_get_drvdata(dev);
	u8 data[RTC_NR_TIME];
	int ret;

	ret = max77851_rtc_tm_to_data(&alrm->time, data, info);
	if (ret < 0)
		return ret;

	mutex_lock(&info->lock);

	ret = max77851_rtc_stop_alarm(info);
	if (ret < 0)
		goto out;

	ret = _regmap_bulk_write(info,
				 info->drv_data->map[REG_ALARM1_SEC],
				 data, ARRAY_SIZE(data));

	if (ret < 0) {
		dev_err(info->dev, "Fail to write alarm reg(%d)\n", ret);
		goto out;
	}

	ret = max77851_rtc_update(info, MAX77851_RTC_WRITE);
	if (ret < 0)
		goto out;

	if (alrm->enabled)
		ret = max77851_rtc_start_alarm(info);
out:
	mutex_unlock(&info->lock);
	return ret;
}

static int max77851_rtc_alarm_irq_enable(struct device *dev,
					 unsigned int enabled)
{
	struct max77851_rtc_info *info = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&info->lock);
	if (enabled)
		ret = max77851_rtc_start_alarm(info);
	else
		ret = max77851_rtc_stop_alarm(info);
	mutex_unlock(&info->lock);

	return ret;
}

#if defined(CONFIG_REGMAP_IRQ)
static irqreturn_t max77851_rtc_alarm1_irq(int irq, void *data)
{
	struct max77851_rtc_info *info = data;
	const unsigned int *map = info->drv_data->map;
	unsigned int val;

	regmap_read(info->rtc_regmap, map[REG_RTC_INT], &val);

	rtc_update_irq(info->rtc_dev, 1, RTC_IRQF | RTC_AF);

	return IRQ_HANDLED;
}
#endif

static const struct rtc_class_ops max77851_rtc_ops = {
	.read_time = max77851_rtc_read_time,
	.set_time = max77851_rtc_set_time,
	.read_alarm = max77851_rtc_read_alarm,
	.set_alarm = max77851_rtc_set_alarm,
	.alarm_irq_enable = max77851_rtc_alarm_irq_enable,
};

static int max77851_rtc_enable(struct max77851_chip *chip)
{
	int ret;

	/* RTC Enable */
	ret = regmap_update_bits(chip->rmap, RLOGIC_CFG_REG, RLOGIC_CFG_RTC_EN, RLOGIC_CFG_RTC_EN);
	if (ret < 0) {
		dev_err(chip->dev, "Register set failed: %d\n", ret);
		return ret;
	}

	/* RTC Global Interrupt Unmask */
	ret = regmap_update_bits(chip->rmap, TOP_MSK0_REG, TOP_MSK0_RTC_M, BIT_IS_ZERO);
	if (ret < 0) {
		dev_err(chip->dev, "Global Mask register set failed: %d\n", ret);
		return ret;
	}

	return ret;
}

static int max77851_rtc_init_reg(struct max77851_rtc_info *info)
{
	unsigned int access_cntl;
	unsigned int mode_cntl;
	int ret;
	unsigned int val1;
	unsigned int val2;

	/* Set RTC control register : Binary mode, 24hour mdoe */
	access_cntl = BCD_EN_SHIFT | MODEL24_SHIFT;
	mode_cntl = MODEL24_SHIFT;

	info->rtc_24hr_mode = true;
	info->rtc_binary_mode = true;

	ret = regmap_read(info->rtc_regmap, info->drv_data->map[REG_RTC_CONTROLM], &val1);
	ret = regmap_read(info->rtc_regmap, info->drv_data->map[REG_RTC_CONTROL], &val2);

	ret = regmap_write(info->rtc_regmap, info->drv_data->map[REG_RTC_CONTROLM], access_cntl);
	if (ret < 0) {
		dev_err(info->dev, "RTC register set failed: %d\n", ret);
		return ret;
	}

	ret = regmap_write(info->rtc_regmap, info->drv_data->map[REG_RTC_CONTROL], mode_cntl);
	if (ret < 0) {
		dev_err(info->dev, "RTC register set failed: %d\n", ret);
		return ret;
	}

	ret = max77851_rtc_update(info, MAX77851_RTC_WRITE);
	ret = max77851_rtc_update(info, MAX77851_RTC_READ);

	ret = regmap_read(info->rtc_regmap, info->drv_data->map[REG_RTC_CONTROLM], &val1);
	ret = regmap_read(info->rtc_regmap, info->drv_data->map[REG_RTC_CONTROL], &val2);

	return ret;
}

static const struct regmap_config max77851_rtc_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RTC_DOMA2_REG,
};

static int max77851_init_rtc_regmap(struct max77851_rtc_info *info)
{
	struct device *parent = info->dev->parent;
	struct i2c_client *parent_i2c = to_i2c_client(parent);
	int ret;

	if (info->drv_data->rtc_irq_from_platform) {
		struct platform_device *pdev = to_platform_device(info->dev);

		info->rtc_irq = platform_get_irq(pdev, 0);
		if (info->rtc_irq < 0) {
			dev_err(info->dev, "Failed to get rtc interrupts: %d\n", info->rtc_irq);
			return info->rtc_irq;
		}
	} else {
		info->rtc_irq =  parent_i2c->irq;
		dev_info(info->dev, "rtc irq =  %d\n", info->rtc_irq);
	}

	info->regmap = dev_get_regmap(parent, NULL);
	if (!info->regmap) {
		dev_err(info->dev, "Failed to get rtc regmap\n");
		return -ENODEV;
	}

	if (info->drv_data->rtc_i2c_addr == MAX77851_INVALID_I2C_ADDR) {
		info->rtc_regmap = info->regmap;
		goto init_rtc_exit;
	}

#if defined(CONFIG_I2C)
	info->rtc = i2c_new_dummy_device(parent_i2c->adapter,
				  info->drv_data->rtc_i2c_addr);
	if (!info->rtc) {
		dev_err(info->dev, "Failed to allocate I2C device for RTC\n");
		return -ENODEV;
	}

	info->rtc_regmap = devm_regmap_init_i2c(info->rtc,
						&max77851_rtc_regmap_config);
	if (IS_ERR(info->rtc_regmap)) {
		ret = PTR_ERR(info->rtc_regmap);
		dev_err(info->dev, "Failed to allocate RTC regmap: %d\n", ret);
		goto err_unregister_i2c;
	}
#endif

init_rtc_exit:
	return 0;

#if defined(CONFIG_I2C)
err_unregister_i2c:
	if (info->rtc)
		i2c_unregister_device(info->rtc);
#endif
	return ret;
}

static int max77851_rtc_probe(struct platform_device *pdev)
{
	struct max77851_chip *chip = dev_get_drvdata(pdev->dev.parent);

	struct max77851_rtc_info *info;
	const struct platform_device_id *id = platform_get_device_id(pdev);
	struct device_node *np;
	int ret;

	np = of_get_child_by_name(pdev->dev.parent->of_node, "rtc");
	if (np && !of_device_is_available(np))
		return -ENODEV;

	info = devm_kzalloc(&pdev->dev, sizeof(struct max77851_rtc_info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	mutex_init(&info->lock);
	info->dev = &pdev->dev;
	info->drv_data = (const struct max77851_rtc_driver_data *)
		id->driver_data;

	info->shutdown = false;

	ret = max77851_init_rtc_regmap(info);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, info);

	ret = max77851_rtc_enable(chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to enable RTC reg:%d\n", ret);
		goto err_rtc;
	}

	ret = max77851_rtc_update(info, MAX77851_RTC_READ);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to update RTC reg:%d\n", ret);
		goto err_rtc;
	}

	ret = max77851_rtc_init_reg(info);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to initialize RTC reg:%d\n", ret);
		goto err_rtc;
	}

	device_init_wakeup(&pdev->dev, 1);

	info->rtc_dev = devm_rtc_device_register(&pdev->dev, id->name,
					&max77851_rtc_ops, THIS_MODULE);

	if (IS_ERR(info->rtc_dev)) {
		ret = PTR_ERR(info->rtc_dev);
		dev_err(&pdev->dev, "Failed to register RTC device: %d\n", ret);
		if (ret == 0)
			ret = -EINVAL;
		goto err_rtc;
	}
#if defined(CONFIG_REGMAP_IRQ)
	info->rtc_alarm1_virq = regmap_irq_get_virq(chip->top_irq_data, MAX77851_IRQ_TOP_RTC);
	if (info->rtc_alarm1_virq <= 0) {
		ret = -ENXIO;
		goto err_rtc;
	}

	ret = request_threaded_irq(info->rtc_alarm1_virq, NULL, max77851_rtc_alarm1_irq, 0,
				   "rtc-alarm1", info);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request alarm IRQ: %d: %d\n",
			info->rtc_alarm1_virq, ret);
		goto err_rtc;
	}
#endif

	ret = max77851_rtc_update(info, MAX77851_RTC_WRITE);
	ret = max77851_rtc_update(info, MAX77851_RTC_READ);

	return 0;

err_rtc:
#if defined(CONFIG_REGMAP_IRQ)
	regmap_del_irq_chip(info->rtc_irq, info->rtc_irq_data);
#endif
#if defined(CONFIG_I2C)
	if (info->rtc)
		i2c_unregister_device(info->rtc);
#endif

	return ret;
}

static void max77851_rtc_shutdown(struct platform_device *pdev)
{
	struct max77851_rtc_info *info = platform_get_drvdata(pdev);
	int ret;

	if (!info)
		goto mutex_exit;

	mutex_lock(&info->lock);
	info->shutdown = true;
	mutex_unlock(&info->lock);

	ret = max77851_rtc_stop_alarm(info);

	if (ret < 0)
		dev_err(info->dev, "rtc alarm stop failed:%d\n", ret);

mutex_exit:
	mutex_destroy(&info->lock);
}

static int max77851_rtc_remove(struct platform_device *pdev)
{
#if defined(CONFIG_REGMAP_IRQ) || defined(CONFIG_I2C)
	struct max77851_rtc_info *info = platform_get_drvdata(pdev);

#if defined(CONFIG_REGMAP_IRQ)
	free_irq(info->rtc_alarm1_virq, info);
	regmap_del_irq_chip(info->rtc_irq, info->rtc_irq_data);
#endif
#if defined(CONFIG_I2C)
	if (info->rtc)
		i2c_unregister_device(info->rtc);
#endif
#endif

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int max77851_rtc_suspend(struct device *dev)
{
	struct max77851_rtc_info *info = dev_get_drvdata(dev);

#if defined(CONFIG_REGMAP_IRQ)
	if (device_may_wakeup(dev))
		return enable_irq_wake(info->rtc_alarm1_virq);
#endif

	regcache_sync(info->rtc_regmap);

	return 0;
}

static int max77851_rtc_resume(struct device *dev)
{
#if defined(CONFIG_REGMAP_IRQ)
	struct max77851_rtc_info *info = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		return disable_irq_wake(info->rtc_alarm1_virq);
#endif

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(max77851_rtc_pm_ops,
			 max77851_rtc_suspend, max77851_rtc_resume);

static const struct platform_device_id rtc_id[] = {
	{ "max77851-rtc", .driver_data = (kernel_ulong_t)&max77851_drv_data, },
	{},
};
MODULE_DEVICE_TABLE(platform, rtc_id);

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void max77851_rtc_remove_wrapper(struct platform_device *pdev)
{
	max77851_rtc_remove(pdev);
}
#else
static int max77851_rtc_remove_wrapper(struct platform_device *pdev)
{
	return max77851_rtc_remove(pdev);
}
#endif

static struct platform_driver max77851_rtc_driver = {
	.driver		= {
		.name	= "max77851-rtc",
		.pm	= &max77851_rtc_pm_ops,
	},
	.probe		= max77851_rtc_probe,
	.shutdown	= max77851_rtc_shutdown,
	.remove		= max77851_rtc_remove_wrapper,
	.id_table	= rtc_id,
};

module_platform_driver(max77851_rtc_driver);

MODULE_DESCRIPTION("Maxim MAX77851 RTC driver");
MODULE_AUTHOR("Shubhi Garg<shgarg@nvidia.com>");
MODULE_AUTHOR("Joan Na<Joan.na@maximintegrated.com>");
MODULE_ALIAS("platform:max77851-rtc");
MODULE_LICENSE("GPL");
