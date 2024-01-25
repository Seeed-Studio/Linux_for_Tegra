// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2023-2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

/* Device tree example:
 *
 * bmi088@69 {
 *   compatible = "bmi,bmi088";
 *   reg = <0x69>; // <-- Must be gyroscope I2C address
 *   accel_i2c_addr = <0x19>; // Must be specified
 *   accel_irq-gpios = <&tegra_gpio TEGRA_GPIO(BB, 0) GPIO_ACTIVE_HIGH>;
 *   gyro_irq-gpios = <&tegra_gpio TEGRA_GPIO(BB, 1) GPIO_ACTIVE_HIGH>;
 *   accel_matrix    = [01 00 00 00 01 00 00 00 01];
 *   gyro_matrix        = [01 00 00 00 01 00 00 00 01];
 * };
 */

#include <nvidia/conftest.h>

#include <linux/device.h>
#include <linux/version.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/bitops.h>
#include <linux/hte.h>
#include "bmi_iio.h"

#define BMI_NAME			"bmi088"

#define BMI_ACC_SOFTRESET_DELAY_MS	(50)
#define BMI_GYR_SOFTRESET_DELAY_MS	(50)
#define BMI_ACC_PM_DELAY_MS		(5)
#define BMI_GYR_PM_DELAY_MS		(30)
#define BMI_HW_DELAY_POR_MS		(10)
#define BMI_HW_DELAY_DEV_ON_US		(2)
#define BMI_HW_DELAY_DEV_OFF_US		(1000)
#define BMI_REG_ACC_CHIP_ID		(0x00)
#define BMI_REG_ACC_ERR_REG		(0x02)
#define BMI_REG_ACC_STATUS		(0x03)
#define BMI_REG_ACC_DATA		(0x12)
#define BMI_REG_SENSORTIME_2		(0x1A)
#define BMI_REG_ACC_INT_STAT_1		(0x1D)
#define BMI_REG_TEMP_MSB		(0x22)
#define BMI_REG_FIFO_DATA		(0x26)
#define BMI_REG_ACC_CONF		(0x40)
#define BMI_REG_ACC_CONF_BWP_POR	(0xA0)
#define BMI_REG_ACC_CONF_BWP_MSK	(0xF0)
#define BMI_REG_ACC_RANGE		(0x41)
#define BMI_REG_FIFO_DOWNS		(0x45)
#define BMI_REG_ACC_FIFO_CFG_0		(0x48)
#define BMI_REG_ACC_FIFO_CFG_1		(0x49)
#define BMI_REG_INT1_IO_CTRL		(0x53)
#define BMI_REG_INT2_IO_CTRL		(0x54)
#define BMI_REG_ACCEL_INIT_CTRL		(0x59)
#define BMI_REG_INTX_IO_CTRL_OUT_EN	(0x08)
#define BMI_REG_INTX_IO_CTRL_ACTV_HI	(0x02)
#define BMI_REG_INT_MAP_DATA		(0x58)
#define BMI_INT1_OUT_ACTIVE_HIGH	(0x0A)
#define BMI_INT1_DTRDY			(0x04)
#define BMI_REG_ACC_PWR_CONF		(0x7C)
#define BMI_REG_ACC_PWR_CONF_ACTV	(0x00)
#define BMI_REG_ACC_PWR_CONF_SUSP	(0x03)
#define BMI_REG_ACC_PWR_CTRL		(0x7D)
#define BMI_REG_ACC_PWR_CTRL_OFF	(0x00)
#define BMI_REG_ACC_PWR_CTRL_ON		(0x04)
#define BMI_REG_ACC_SOFTRESET		(0x7E)
#define BMI_REG_ACC_SOFTRESET_FIFO	(0xB0)
#define BMI_REG_ACC_SOFTRESET_EXE	(0xB6)
/* HW registers gyroscope */
#define BMI_REG_GYR_CHIP_ID		(0x00)
#define BMI_REG_GYR_Z_MSB		(0x07)
#define BMI_REG_GYR_DATA		(0x02)
#define BMI_REG_GYR_INT_STAT_1		(0x0A)
#define BMI_REG_FIFO_STATUS		(0x0E)
#define BMI_REG_GYR_RANGE		(0x0F)
#define BMI_REG_GYR_BW			(0x10)
#define BMI_REG_GYR_LPM1		(0x11)
#define BMI_REG_GYR_LPM1_NORM		(0x00)
#define BMI_REG_GYR_LPM1_DEEP		(0x20)
#define BMI_REG_GYR_LPM1_SUSP		(0x80)
#define BMI_REG_GYR_SOFTRESET		(0x14)
#define BMI_REG_GYR_SOFTRESET_EXE	(0xB6)
#define BMI_REG_GYR_INT_CTRL		(0x15)
#define BMI_REG_GYR_INT_CTRL_DIS	(0x00)
#define BMI_REG_GYR_INT_CTRL_DATA_EN	(0x80)
#define BMI_REG_INT_3_4_IO_CONF		(0x16)
#define BMI_REG_INT_3_4_IO_CONF_3_HI	(0x01)
#define BMI_REG_INT_3_4_IO_CONF_4_HI	(0x04)
#define BMI_REG_INT_3_4_IO_MAP		(0x18)
#define BMI_REG_INT_3_4_IO_MAP_INT3	(0x01)
#define BMI_REG_INT_3_ACTIVE_HIGH	(0x01)
#define BMI_REG_FIFO_EXT_INT_S		(0x34)
#define BMI_REG_GYR_SELF_TEST		(0x3C)
#define BMI_REG_GYR_FIFO_CFG_1		(0x3E)

#define BMI_AXIS_N			(3)
#define BMI_IMU_DATA			(6)

/* hardware devices */
#define BMI_HW_ACC			(0)
#define BMI_HW_GYR			(1)
#define BMI_HW_N			(2)

#define BMI_PART_BMI088			(0)

#define HTE_TIMEOUT			(msecs_to_jiffies(100))

static const struct i2c_device_id bmi_i2c_device_ids[] = {
	{ BMI_NAME, BMI_PART_BMI088 },
	{},
};

struct bmi_gpio_irq {
	struct gpio_desc *gpio_in;
	const char *dev_name;
	int irq;
};

struct bmi_reg_rd {
	u8 reg_lo;
	u8 reg_hi;
};

static struct bmi_reg_rd bmi_reg_rds_acc[] = {
	{
		.reg_lo			= BMI_REG_ACC_CHIP_ID,
		.reg_hi			= BMI_REG_ACC_STATUS,
	},
	{
		.reg_lo			= BMI_REG_ACC_DATA,
		.reg_hi			= BMI_REG_SENSORTIME_2,
	},
	{
		.reg_lo			= BMI_REG_ACC_INT_STAT_1,
		.reg_hi			= BMI_REG_ACC_INT_STAT_1,
	},
	{
		.reg_lo			= BMI_REG_TEMP_MSB,
		.reg_hi			= BMI_REG_FIFO_DATA,
	},
	{
		.reg_lo			= BMI_REG_ACC_CONF,
		.reg_hi			= BMI_REG_ACC_RANGE,
	},
	{
		.reg_lo			= BMI_REG_FIFO_DOWNS,
		.reg_hi			= BMI_REG_ACC_FIFO_CFG_1,
	},
	{
		.reg_lo			= BMI_REG_INT1_IO_CTRL,
		.reg_hi			= BMI_REG_INT2_IO_CTRL,
	},
	{
		.reg_lo			= BMI_REG_INT_MAP_DATA,
		.reg_hi			= BMI_REG_INT_MAP_DATA,
	},
	{
		.reg_lo			= BMI_REG_ACC_PWR_CONF,
		.reg_hi			= BMI_REG_ACC_SOFTRESET,
	},
};

static struct bmi_reg_rd bmi_reg_rds_gyr[] = {
	{
		.reg_lo			= BMI_REG_GYR_CHIP_ID,
		.reg_hi			= BMI_REG_GYR_Z_MSB,
	},
	{
		.reg_lo			= BMI_REG_GYR_INT_STAT_1,
		.reg_hi			= BMI_REG_GYR_INT_STAT_1,
	},
	{
		.reg_lo			= BMI_REG_FIFO_STATUS,
		.reg_hi			= BMI_REG_GYR_LPM1,
	},
	{
		.reg_lo			= BMI_REG_GYR_SOFTRESET,
		.reg_hi			= BMI_REG_INT_3_4_IO_CONF,
	},
	{
		.reg_lo			= BMI_REG_INT_3_4_IO_MAP,
		.reg_hi			= BMI_REG_INT_3_4_IO_MAP,
	},
	{
		.reg_lo			= BMI_REG_FIFO_EXT_INT_S,
		.reg_hi			= BMI_REG_FIFO_EXT_INT_S,
	},
	{
		.reg_lo			= BMI_REG_GYR_SELF_TEST,
		.reg_hi			= BMI_REG_GYR_FIFO_CFG_1,
	},
};

static struct sensor_cfg bmi_snsr_cfgs[] = {
	{
		.name			= "accelerometer",
		.snsr_id		= BMI_HW_ACC,
		.ch_n			= BMI_AXIS_N,
		.part			= BMI_NAME,
		.max_range		= {
			.ival		= 0, /* default = +/-3g */
		},
		.delay_us_max		= 80000,
		/* default matrix to get the attribute */
		.matrix[0]		= 1,
		.matrix[4]		= 1,
		.matrix[8]		= 1,
		.float_significance	= IIO_VAL_INT_PLUS_MICRO,
	},
	{
		.name			= "gyroscope",
		.snsr_id		= BMI_HW_GYR,
		.ch_n			= BMI_AXIS_N,
		.max_range		= {
			.ival		= 0, /* default = +/-2000dps */
		},
		.delay_us_max		= 10000,
		.matrix[0]		= 1,
		.matrix[4]		= 1,
		.matrix[8]		= 1,
		.float_significance	= IIO_VAL_INT_PLUS_MICRO,
	},
};

struct bmi_rr {
	struct bmi_float max_range;
	struct bmi_float resolution;
};

static struct bmi_rr bmi_rr_acc_bmi088[] = {
/* all accelerometer values are in g's (9.80665 m/s2) fval = NANO scale */
	{
		.max_range		= {
			.ival		= 29,
			.fval		= 419950000,
		},
		.resolution		= {
			.ival		= 0,
			.fval		= 897,
		},
	},
	{
		.max_range		= {
			.ival		= 58,
			.fval		= 839900000,
		},
		.resolution		= {
			.ival		= 0,
			.fval		= 1795,
		},
	},
	{
		.max_range		= {
			.ival		= 117,
			.fval		= 679800000,
		},
		.resolution		= {
			.ival		= 0,
			.fval		= 3591,
		},
	},
	{
		.max_range		= {
			.ival		= 235,
			.fval		= 359600000,
		},
		.resolution		= {
			.ival		= 0,
			.fval		= 7182,
		},
	},
};

static struct bmi_rr bmi_rr_gyr[] = {
/* rad / sec  fval = NANO scale */
	{
		.max_range		= {
			.ival		= 34,
			.fval		= 906585040,
		},
		.resolution		= {
			.ival		= 0,
			.fval		= 1065,
		},
	},
	{
		.max_range		= {
			.ival		= 17,
			.fval		= 453292520,
		},
		.resolution		= {
			.ival		= 0,
			.fval		= 532,
		},
	},
	{
		.max_range		= {
			.ival		= 8,
			.fval		= 726646260,
		},
		.resolution		= {
			.ival		= 0,
			.fval		= 266,
		},
	},
	{
		.max_range		= {
			.ival		= 4,
			.fval		= 363323130,
		},
		.resolution		= {
			.ival		= 0,
			.fval		= 133,
		},
	},
	{
		.max_range		= {
			.ival		= 2,
			.fval		= 181661565,
		},
		.resolution		= {
			.ival		= 0,
			.fval		= 66,
		},
	},
};

struct bmi_rrs {
	struct bmi_rr *rr;
	unsigned int rr_0n;
};

static struct bmi_rrs bmi_rrs_acc[] = {
	{
		.rr			= bmi_rr_acc_bmi088,
		.rr_0n			= ARRAY_SIZE(bmi_rr_acc_bmi088) - 1,
	},
};

static struct bmi_rrs bmi_rrs_gyr[] = {
	{
		.rr			= bmi_rr_gyr,
		.rr_0n			= ARRAY_SIZE(bmi_rr_gyr) - 1,
	},
};

struct bmi_state;
static int bmi_acc_able(struct bmi_state *st, int en, bool fast);
static int bmi_acc_batch(struct bmi_state *st, unsigned int period_us,
			 bool range);
static int bmi_acc_softreset(struct bmi_state *st, unsigned int hw);
static int bmi_acc_pm(struct bmi_state *st, unsigned int hw, int able);
static unsigned long bmi_acc_irqflags(struct bmi_state *st);
static int bmi_gyr_able(struct bmi_state *st, int en, bool fast);
static int bmi_gyr_batch(struct bmi_state *st, unsigned int period_us,
			 bool range);
static int bmi_gyr_softreset(struct bmi_state *st, unsigned int hw);
static int bmi_gyr_pm(struct bmi_state *st, unsigned int hw, int able);
static unsigned long bmi_gyr_irqflags(struct bmi_state *st);

struct bmi_hw {
	struct bmi_reg_rd *reg_rds;
	struct bmi_rrs *rrs;
	unsigned int reg_rds_n;
	unsigned int rrs_0n;
	int (*fn_able)(struct bmi_state *st, int en, bool fast);
	int (*fn_batch)(struct bmi_state *st, unsigned int period_us,
			bool range);
	int (*fn_softreset)(struct bmi_state *st, unsigned int hw);
	int (*fn_pm)(struct bmi_state *st, unsigned int hw, int able);
	unsigned long (*fn_irqflags)(struct bmi_state *st);
};

static struct bmi_hw bmi_hws[] = {
	{
		.reg_rds		= bmi_reg_rds_acc,
		.rrs			= bmi_rrs_acc,
		.reg_rds_n		= ARRAY_SIZE(bmi_reg_rds_acc),
		.rrs_0n			= ARRAY_SIZE(bmi_rrs_acc) - 1,
		.fn_able		= &bmi_acc_able,
		.fn_batch		= &bmi_acc_batch,
		.fn_softreset		= &bmi_acc_softreset,
		.fn_pm			= &bmi_acc_pm,
		.fn_irqflags		= &bmi_acc_irqflags,
	},
	{
		.reg_rds		= bmi_reg_rds_gyr,
		.rrs			= bmi_rrs_gyr,
		.reg_rds_n		= ARRAY_SIZE(bmi_reg_rds_gyr),
		.rrs_0n			= ARRAY_SIZE(bmi_rrs_gyr) - 1,
		.fn_able		= &bmi_gyr_able,
		.fn_batch		= &bmi_gyr_batch,
		.fn_softreset		= &bmi_gyr_softreset,
		.fn_pm			= &bmi_gyr_pm,
		.fn_irqflags		= &bmi_gyr_irqflags,
	},
};

struct bmi_snsr {
	struct iio_dev *bmi_iio;
	struct bmi_rrs *rrs;
	struct sensor_cfg cfg;
	unsigned int usr_cfg;
	unsigned int period_us;
	u64 irq_ts;
	u64 irq_ts_old;
	u64 seq;
	struct completion hte_ts_cmpl;
	struct bmi_gpio_irq gis;
	struct bmi_state *st;
};

struct bmi_state {
	struct i2c_client *i2c;
	struct bmi_snsr snsrs[BMI_HW_N];
	bool iio_init_done[BMI_HW_N];
	unsigned int part;
	unsigned int sts;
	unsigned int errs_bus[BMI_HW_N];
	unsigned int enabled;
	unsigned int suspend_en_st;
	unsigned int hw_n;
	unsigned int hw_en;
	s64 ts_hw[BMI_HW_N];
	u8 ra_0x53;
	u8 ra_0x54;
	u8 ra_0x58;
	u8 rg_0x16;
	u8 rg_0x18;
	u16 i2c_addrs[BMI_HW_N];
};

static inline s64 get_ktime_timestamp(void)
{
	struct timespec64 ts;

	ktime_get_ts64(&ts);

	return timespec64_to_ns(&ts);
}

static int bmi_i2c_rd(struct bmi_state *st, unsigned int hw,
		      u8 reg, u16 len, void *buf)
{
	struct i2c_msg msg[2];
	int ret = -ENODEV;
	s64 ts;

	if (st->i2c_addrs[hw]) {
		msg[0].addr = st->i2c_addrs[hw];
		msg[0].flags = 0;
		msg[0].len = 1;
		msg[0].buf = &reg;
		msg[1].addr = st->i2c_addrs[hw];
		msg[1].flags = I2C_M_RD;
		msg[1].len = len;
		msg[1].buf = buf;
		ts = st->ts_hw[hw];
		if (st->hw_en & (1 << hw))
			ts += (BMI_HW_DELAY_DEV_ON_US * 1000);
		else
			ts += (BMI_HW_DELAY_DEV_OFF_US * 1000);
		ts -= get_ktime_timestamp();
		if (ts > 0) {
			ts /= 1000;
			ts++;
			udelay(ts);
		}
		ret = i2c_transfer(st->i2c->adapter, msg, 2);
		st->ts_hw[hw] = get_ktime_timestamp();
		if (ret != 2) {
			st->errs_bus[hw]++;
			ret = -EIO;
		} else {
			ret = 0;
		}
	}

	return ret;
}

static int bmi_i2c_w(struct bmi_state *st, unsigned int hw,
		     u16 len, u8 *buf)
{
	struct i2c_msg msg;
	int ret;
	s64 ts;

	if (st->i2c_addrs[hw]) {
		msg.addr = st->i2c_addrs[hw];
		msg.flags = 0;
		msg.len = len;
		msg.buf = buf;
		ts = st->ts_hw[hw];
		if (st->hw_en & (1 << hw))
			ts += (BMI_HW_DELAY_DEV_ON_US * 1000);
		else
			ts += (BMI_HW_DELAY_DEV_OFF_US * 1000);
		ts -= get_ktime_timestamp();
		if (ts > 0) {
			ts /= 1000; /* ns => us */
			ts++;
			udelay(ts);
		}
		ret = i2c_transfer(st->i2c->adapter, &msg, 1);
		st->ts_hw[hw] = get_ktime_timestamp();
		if (ret != 1) {
			st->errs_bus[hw]++;
			ret = -EIO;
		} else {
			ret = 0;
		}
	} else {
		ret = -ENODEV;
	}

	return ret;
}

static int bmi_i2c_wr(struct bmi_state *st, unsigned int hw, u8 reg, u8 val)
{
	int ret;
	u8 buf[2];

	buf[0] = reg;
	buf[1] = val;
	ret = bmi_i2c_w(st, hw, sizeof(buf), buf);
	if (ret)
		dev_err(&st->i2c->dev, "ERR: 0x%02X=>0x%02X\n", val, reg);

	return ret;
}

static int bmi_setup_gpio(struct device *dev, struct bmi_state *st,
			  unsigned int n)
{
	unsigned int i;
	struct bmi_snsr *snsrs = st->snsrs;
	int ret;

	for (i = 0; i < n; i++) {
		snsrs[i].gis.irq = -1;
		if ((snsrs[i].gis.gpio_in)) {
			ret = gpiod_direction_input(snsrs[i].gis.gpio_in);

			if (ret < 0) {
				dev_err(dev, "%s gpio_dir_input ERR:%d\n",
						snsrs[i].gis.dev_name, ret);
				return ret;
			}

			ret = gpiod_to_irq(snsrs[i].gis.gpio_in);
			if (ret < 0) {
				dev_err(dev, "%s gpio_to_irq ERR:%d\n",
						snsrs[i].gis.dev_name, ret);
				return ret;
			}

			snsrs[i].gis.irq = ret;
		}
	}

	return 0;
}

static int bmi_pm(struct bmi_state *st, int snsr_id, bool en)
{
	unsigned int i;
	int ret = 0;

	if (en) {
		if (!st->hw_en)
			/* first time power on, wait for power on reset time */
			mdelay(BMI_HW_DELAY_POR_MS);

		if (snsr_id < 0) {
			for (i = 0; i < st->hw_n; i++)
				ret |= bmi_hws[i].fn_pm(st, i, 1);
		} else {
			if (snsr_id >= st->hw_n)
				return -ENODEV;

			ret = bmi_hws[snsr_id].fn_pm(st, snsr_id, 1);
		}
	} else {
		if (snsr_id < 0) {
			for (i = 0; i < st->hw_n; i++) {
				ret |= bmi_hws[i].fn_pm(st, i, 0);
				st->enabled &= ~(1 << i);
			}
		} else {
			if (snsr_id >= st->hw_n)
				return -ENODEV;

			dev_dbg(&st->i2c->dev, "turning off:%d\n", snsr_id);
			ret = bmi_hws[snsr_id].fn_pm(st, snsr_id, 0);
			st->enabled &= ~(1 << snsr_id);
		}
	}

	if (ret) {
		if (snsr_id < 0)
			dev_err(&st->i2c->dev, "ALL pm_en=%x  ERR=%d\n",
				en, ret);
		else
			dev_err(&st->i2c->dev, "%s pm_en=%x  ERR=%d\n",
				st->snsrs[snsr_id].cfg.name, en, ret);
	}

	return ret;
}

struct bmi_odr {
	unsigned int period_us;
	u8 hw;
	unsigned int odr_hz;
	unsigned int nodr_hz_mant;
};

static unsigned int bmi_odr_i(struct bmi_odr *odrs, unsigned int odrs_n,
			      unsigned int period_us)
{
	unsigned int i;

	for (i = 0; i < odrs_n; i++) {
		if (period_us >= odrs[i].period_us)
			break;
	}

	return i;
}

static struct bmi_odr bmi_odrs_acc[] = {
	{ 80000, 0x05, 12, 500000 },
	{ 40000, 0x06, 25, 0 },
	{ 20000, 0x07, 50, 0 },
	{ 10000, 0x08, 100, 0 },
	{ 5000,  0x09, 200, 0 },
	{ 2500,  0x0A, 400, 0 },
	{ 1250,  0x0B, 800, 0 },
	{ 625,   0x0C, 1600, 0 },
};

/* Configures ODR and range */
static int bmi_acc_batch(struct bmi_state *st, unsigned int period_us,
			 bool range)
{
	u8 val;
	unsigned int odr_i;
	int ret = 0;

	odr_i = bmi_odr_i(bmi_odrs_acc, ARRAY_SIZE(bmi_odrs_acc), period_us);

	val = bmi_odrs_acc[odr_i].hw;
	val |= BMI_REG_ACC_CONF_BWP_POR;
	ret = bmi_i2c_wr(st, BMI_HW_ACC, BMI_REG_ACC_CONF, val);
	if (!ret)
		st->snsrs[BMI_HW_ACC].period_us = bmi_odrs_acc[odr_i].period_us;

	if (range)
		ret |= bmi_i2c_wr(st, BMI_HW_ACC, BMI_REG_ACC_RANGE,
				  (u8)st->snsrs[BMI_HW_ACC].usr_cfg);

	return ret;
}

/* Maps and set/reset the data ready interrupt */
static int bmi_acc_able(struct bmi_state *st, int en, bool fast)
{
	int ret = 0;

	if (!en)
		return bmi_i2c_wr(st, BMI_HW_ACC, BMI_REG_INT_MAP_DATA, 0x0);

	if (!fast) {
		ret = bmi_i2c_wr(st, BMI_HW_ACC, BMI_REG_INT1_IO_CTRL,
				 st->ra_0x53);
		ret |= bmi_i2c_wr(st, BMI_HW_ACC, BMI_REG_INT2_IO_CTRL,
				  st->ra_0x54);
	}

	ret |= bmi_i2c_wr(st, BMI_HW_ACC, BMI_REG_INT_MAP_DATA, st->ra_0x58);

	return ret;
}

static int bmi_acc_softreset(struct bmi_state *st, unsigned int hw)
{
	int ret;

	ret = bmi_i2c_wr(st, BMI_HW_ACC, BMI_REG_ACC_SOFTRESET,
			 BMI_REG_ACC_SOFTRESET_EXE);
	mdelay(BMI_ACC_SOFTRESET_DELAY_MS);

	st->hw_en &= ~(1 << hw);
	st->enabled &= ~(1 << hw);

	return ret;
}

static int bmi_acc_pm(struct bmi_state *st, unsigned int hw, int able)
{
	int ret;
	u8 pwr_conf;
	u8 pwr_on_off;

	if (able) {
		pwr_conf = BMI_REG_ACC_PWR_CONF_ACTV;
		pwr_on_off = BMI_REG_ACC_PWR_CTRL_ON;
		st->hw_en |= (1 << hw);
	} else {
		pwr_conf = BMI_REG_ACC_PWR_CONF_SUSP;
		pwr_on_off = BMI_REG_ACC_PWR_CTRL_OFF;
		st->hw_en &= ~(1 << hw);
	}

	ret = bmi_i2c_wr(st, BMI_HW_ACC, BMI_REG_ACC_PWR_CONF,
			 pwr_conf);

	mdelay(BMI_ACC_PM_DELAY_MS);


	ret |= bmi_i2c_wr(st, BMI_HW_ACC, BMI_REG_ACC_PWR_CTRL,
			  pwr_on_off);

	mdelay(BMI_ACC_PM_DELAY_MS);

	if (ret)
		st->hw_en &= ~(1 << hw);

	return ret;
}

static unsigned long bmi_acc_irqflags(struct bmi_state *st)
{
	unsigned long irqflags = IRQF_ONESHOT;
	u8 int_io_conf;

	if (st->ra_0x53 & BMI_REG_INTX_IO_CTRL_OUT_EN)
		int_io_conf = st->ra_0x53;
	else
		int_io_conf = st->ra_0x54;

	if (int_io_conf & BMI_REG_INTX_IO_CTRL_ACTV_HI)
		irqflags |= IRQF_TRIGGER_RISING;
	else
		irqflags |= IRQF_TRIGGER_FALLING;

	return irqflags;
}

static struct bmi_odr bmi_odrs_gyr[] = {
	{ 10000, 0x05, 100, 0 },
	{ 5000,  0x04, 200, 0 },
	{ 2500,  0x03, 400, 0 },
	{ 1000,  0x02, 1000, 0 },
	{ 500,   0x01, 2000, 0 },
};

static int bmi_gyr_batch(struct bmi_state *st, unsigned int period_us,
			 bool range)
{
	u8 val;
	unsigned int odr_i;
	int ret = 0;

	odr_i = bmi_odr_i(bmi_odrs_gyr, ARRAY_SIZE(bmi_odrs_gyr), period_us);

	val = bmi_odrs_gyr[odr_i].hw;
	ret = bmi_i2c_wr(st, BMI_HW_GYR, BMI_REG_GYR_BW, val);
	if (!ret)
		st->snsrs[BMI_HW_GYR].period_us = bmi_odrs_gyr[odr_i].period_us;

	if (range) {
		val = st->snsrs[BMI_HW_GYR].usr_cfg;
		ret |= bmi_i2c_wr(st, BMI_HW_GYR, BMI_REG_GYR_RANGE, val);
	}

	return ret;
}

/* Map and set/reset data ready interrupt */
static int bmi_gyr_able(struct bmi_state *st, int en, bool fast)
{
	int ret = 0;

	if (!en)
		return bmi_i2c_wr(st, BMI_HW_GYR, BMI_REG_GYR_INT_CTRL,
				  BMI_REG_GYR_INT_CTRL_DIS);

	if (!fast) {
		ret = bmi_i2c_wr(st, BMI_HW_GYR, BMI_REG_INT_3_4_IO_CONF,
				 st->rg_0x16);
		ret |= bmi_i2c_wr(st, BMI_HW_GYR, BMI_REG_INT_3_4_IO_MAP,
				  st->rg_0x18);
	}

	ret |= bmi_i2c_wr(st, BMI_HW_GYR, BMI_REG_GYR_INT_CTRL,
			  BMI_REG_GYR_INT_CTRL_DATA_EN);

	return ret;
}

static int bmi_gyr_softreset(struct bmi_state *st, unsigned int hw)
{
	int ret;

	ret = bmi_i2c_wr(st, BMI_HW_GYR, BMI_REG_GYR_SOFTRESET,
			 BMI_REG_GYR_SOFTRESET_EXE);
	mdelay(BMI_GYR_SOFTRESET_DELAY_MS);

	st->hw_en &= ~(1 << hw);
	st->enabled &= ~(1 << hw);

	return ret;
}

static int bmi_gyr_pm(struct bmi_state *st, unsigned int hw, int able)
{
	int ret;
	u8 val;

	if (able) {
		val = BMI_REG_GYR_LPM1_NORM;
		st->hw_en |= (1 << hw);
	} else {
		val = BMI_REG_GYR_LPM1_SUSP;
		st->hw_en &= ~(1 << hw);
	}

	ret = bmi_i2c_wr(st, BMI_HW_GYR, BMI_REG_GYR_LPM1, val);
	if (ret)
		st->hw_en &= ~(1 << hw);

	mdelay(BMI_GYR_PM_DELAY_MS);

	return ret;
}

static unsigned long bmi_gyr_irqflags(struct bmi_state *st)
{
	unsigned long irqflags = IRQF_ONESHOT;

	if (st->rg_0x18 & BMI_REG_INT_3_4_IO_MAP_INT3) {
		if (st->rg_0x16 & BMI_REG_INT_3_4_IO_CONF_3_HI)
			irqflags |= IRQF_TRIGGER_RISING;
		else
			irqflags |= IRQF_TRIGGER_FALLING;
	} else { /* BMI_REG_INT_3_4_IO_MAP_INT4 */
		if (st->rg_0x16 & BMI_REG_INT_3_4_IO_CONF_4_HI)
			irqflags |= IRQF_TRIGGER_RISING;
		else
			irqflags |= IRQF_TRIGGER_FALLING;
	}

	return irqflags;
}

static enum hte_return process_hw_ts(struct hte_ts_data *ts, void *p)
{
	struct bmi_snsr *sensor = (struct bmi_snsr *)p;
	struct bmi_state *st = sensor->st;

	sensor->irq_ts = ts->tsc;
	sensor->seq = ts->seq;

	complete(&sensor->hte_ts_cmpl);

	dev_dbg_ratelimited(&st->i2c->dev, "%s: seq %llu, ts:%llu\n",
			    __func__, sensor->seq, sensor->irq_ts);

	return HTE_CB_HANDLED;
}

static irqreturn_t bmi_irq_thread(int irq, void *dev_id)
{
	struct bmi_snsr *sensor = (struct bmi_snsr *)dev_id;
	struct bmi_state *st = sensor->st;
	unsigned int hw;
	int ret;
	u8 reg;
	u8 sample[BMI_IMU_DATA];

	if (irq == st->snsrs[BMI_HW_GYR].gis.irq) {
		hw = BMI_HW_GYR;
		reg = BMI_REG_GYR_DATA;
	} else {
		hw = BMI_HW_ACC;
		reg = BMI_REG_ACC_DATA;
	}

	/* Disable data ready interrupt before we read out data */
	ret = bmi_hws[hw].fn_able(st, 0, true);
	if (unlikely(ret)) {
		dev_err_ratelimited(&st->i2c->dev,
				    "can't disable sensor: %d\n", hw);
		goto err;
	}

	/* Wait for HTE IRQ to fetch the latest timestamp */
	ret = wait_for_completion_interruptible_timeout(&sensor->hte_ts_cmpl, HTE_TIMEOUT);
	if (!ret) {
		dev_dbg_ratelimited(&st->i2c->dev,
				    "sample dropped due to timeout");
		goto err;
	}

	mutex_lock(BMI_MUTEX(st->snsrs[hw].bmi_iio));

	ret = bmi_i2c_rd(st, hw, reg, sizeof(sample), sample);

	if (!ret) {
		bmi_iio_push_buf(st->snsrs[hw].bmi_iio, sample,
				 sensor->irq_ts);
		dev_dbg(&st->i2c->dev, "%d, ts= %lld ts_old= %lld\n",
			hw, sensor->irq_ts, sensor->irq_ts_old);

		sensor->irq_ts_old = sensor->irq_ts;
	}

	mutex_unlock(BMI_MUTEX(st->snsrs[hw].bmi_iio));

	/* Enable data ready interrupt */
	bmi_hws[hw].fn_able(st, 1, true);
err:
	return IRQ_HANDLED;
}

static int bmi_period(struct bmi_state *st, int snsr_id, bool range)
{

	if (snsr_id >= st->hw_n)
		return -ENODEV;

	return bmi_hws[snsr_id].fn_batch(st, st->snsrs[snsr_id].period_us,
					 range);
}

static int bmi_enable(void *client, int snsr_id, int enable)
{
	struct bmi_state *st = (struct bmi_state *)client;
	int ret;

	if (snsr_id >= st->hw_n)
		return -ENODEV;

	if (enable < 0)
		return (st->enabled & (1 << snsr_id));

	if (enable) {
		enable = st->enabled | (1 << snsr_id);
		ret = bmi_pm(st, snsr_id, true);
		if (ret < 0)
			return ret;

		ret = bmi_period(st, snsr_id, true);
		ret |= bmi_hws[snsr_id].fn_able(st, 1, false);
		if (!ret) {
			st->enabled = enable;
			return ret;
		}
	}

	ret = bmi_hws[snsr_id].fn_able(st, 0, false);
	ret |= bmi_pm(st, snsr_id, false);

	return ret;
}

static inline struct bmi_odr *bmi_find_odrs(struct bmi_state *st, int snsr_id,
					    unsigned int *sz)
{
	struct bmi_odr *odr;

	if (!st || !sz || (snsr_id >= st->hw_n))
		return NULL;

	if (snsr_id == BMI_HW_GYR) {
		odr = bmi_odrs_gyr;
		*sz = ARRAY_SIZE(bmi_odrs_gyr);
	} else if (snsr_id == BMI_HW_ACC) {
		odr = bmi_odrs_acc;
		*sz = ARRAY_SIZE(bmi_odrs_acc);
	} else {
		return NULL;
	}

	return odr;
}

static int bmi_read_odrs(struct bmi_state *st, int snsr_id, int *val,
			 int *val2)
{
	struct bmi_odr *odr;
	unsigned int o_sz;
	unsigned int index;

	if (!st || (snsr_id >= st->hw_n))
		return -EINVAL;

	odr = bmi_find_odrs(st, snsr_id, &o_sz);
	if (!odr)
		return -EINVAL;

	index = bmi_odr_i(odr, o_sz, st->snsrs[snsr_id].period_us);
	if (index >= o_sz)
		return -EINVAL;

	*val = odr[index].odr_hz;
	*val2 = odr[index].nodr_hz_mant;

	return index;
}

static int bmi_find_freq(struct bmi_odr *odr, unsigned int o_sz,
			 int val, int val2)
{
	unsigned int i;

	for (i = 0; i < o_sz; i++) {
		if (val == odr[i].odr_hz && val2 == odr[i].nodr_hz_mant)
			break;
	}

	if (i >= o_sz)
		return -EINVAL;

	return i;
}

static int bmi_freq_write(void *client, int snsr_id, int val, int val2)
{
	struct bmi_state *st = (struct bmi_state *)client;
	int odr_i;
	struct bmi_odr *odr;
	unsigned int o_sz, old_period;
	int ret = 0;

	if (!st || (snsr_id >= st->hw_n))
		return -EINVAL;

	odr = bmi_find_odrs(st, snsr_id, &o_sz);
	if (!odr)
		return -EINVAL;

	odr_i = bmi_find_freq(odr, o_sz, val, val2);
	if (odr_i < 0)
		return -EINVAL;

	old_period = st->snsrs[snsr_id].period_us;

	st->snsrs[snsr_id].period_us = odr[odr_i].period_us;

	ret = bmi_period(st, snsr_id, false);
	if (ret)
		st->snsrs[snsr_id].period_us = old_period;

	return ret;
}

static int bmi_freq_read(void *client, int snsr_id, int *val, int *val2)
{
	struct bmi_state *st = (struct bmi_state *)client;
	int odr_i;
	int ret = 0;

	if (!val || !val2 || !st)
		return -EINVAL;

	odr_i = bmi_read_odrs(st, snsr_id, val, val2);

	if (unlikely(odr_i < 0))
		return -EINVAL;

	return ret;
}

static int bmi_max_range(void *client, int snsr_id, int max_range)
{
	struct bmi_state *st = (struct bmi_state *)client;
	unsigned int i = max_range;

	if (st->enabled & (1 << snsr_id))
		/* can't change settings on the fly (disable device first) */
		return -EBUSY;

	if (st->snsrs[snsr_id].rrs) {
		if (i > st->snsrs[snsr_id].rrs->rr_0n)
			/* clamp to highest setting */
			i = st->snsrs[snsr_id].rrs->rr_0n;
		st->snsrs[snsr_id].usr_cfg = i;
		st->snsrs[snsr_id].cfg.max_range.ival =
				  st->snsrs[snsr_id].rrs->rr[i].max_range.ival;
		st->snsrs[snsr_id].cfg.max_range.fval =
				  st->snsrs[snsr_id].rrs->rr[i].max_range.fval;
		st->snsrs[snsr_id].cfg.scale.ival =
				 st->snsrs[snsr_id].rrs->rr[i].resolution.ival;
		st->snsrs[snsr_id].cfg.scale.fval =
				 st->snsrs[snsr_id].rrs->rr[i].resolution.fval;

	}

	return 0;
}

static int bmi_scale_write(void *client, int snsr_id, int val, int val2)
{
	struct bmi_state *st = (struct bmi_state *)client;
	int ret = 0;
	int i = 0;

	if (!st || (snsr_id >= st->hw_n))
		return -ENODEV;

	for (; i <= st->snsrs[snsr_id].rrs->rr_0n; i++) {
		if (st->snsrs[snsr_id].rrs->rr[i].resolution.ival == val &&
		    st->snsrs[snsr_id].rrs->rr[i].resolution.fval == val2)
			break;
	}
	if (i > st->snsrs[snsr_id].rrs->rr_0n)
		return -EINVAL;

	if (snsr_id == BMI_HW_GYR)
		ret = bmi_i2c_wr(st, BMI_HW_GYR, BMI_REG_GYR_RANGE, i);
	else if (snsr_id == BMI_HW_ACC)
		ret = bmi_i2c_wr(st, BMI_HW_ACC, BMI_REG_ACC_RANGE, i);
	else
		return -ENODEV;

	if (!ret) {
		st->snsrs[snsr_id].usr_cfg = i;
		st->snsrs[snsr_id].cfg.max_range.ival =
				  st->snsrs[snsr_id].rrs->rr[i].max_range.ival;
		st->snsrs[snsr_id].cfg.max_range.fval =
				  st->snsrs[snsr_id].rrs->rr[i].max_range.fval;
		st->snsrs[snsr_id].cfg.scale.ival =
				 st->snsrs[snsr_id].rrs->rr[i].resolution.ival;
		st->snsrs[snsr_id].cfg.scale.fval =
				 st->snsrs[snsr_id].rrs->rr[i].resolution.fval;
	}

	return ret;
}

static int bmi_read_err(void *client, int snsr_id, char *buf)
{
	ssize_t t = 0;
	struct bmi_state *st = (struct bmi_state *)client;

	if (snsr_id >= st->hw_n)
		return -ENODEV;

	t += snprintf(buf, PAGE_SIZE, "%s:\n", st->snsrs[snsr_id].cfg.name);
	t += snprintf(buf + t, PAGE_SIZE - t,
		      "I2C Bus Errors:%u\n", st->errs_bus[snsr_id]);
	return t;
}

static int bmi_get_data(void *client, int snsr_id, int axis, int *val)
{
	struct bmi_state *st = (struct bmi_state *)client;
	u8 reg;
	__le16 sample;
	int ret = 0;

	if (snsr_id >= st->hw_n)
		return -ENODEV;

	if (snsr_id == BMI_HW_ACC)
		reg = BMI_REG_ACC_DATA;
	else
		reg = BMI_REG_GYR_DATA;

	reg += (axis - IIO_MOD_X) * sizeof(__le16);

	ret = bmi_i2c_rd(st, snsr_id, reg, sizeof(__le16), &sample);
	if (!ret)
		*val = sign_extend32(le16_to_cpu(sample), 15);

	return ret;
}

static int bmi_regs(void *client, int snsr_id, char *buf)
{
	struct bmi_state *st = (struct bmi_state *)client;
	struct bmi_reg_rd *reg_rd;
	ssize_t t;
	u8 reg;
	u8 val;
	unsigned int i;
	int ret;

	if (snsr_id >= st->hw_n)
		return -ENODEV;

	t = snprintf(buf, PAGE_SIZE, "register:value\n");
	reg_rd = bmi_hws[snsr_id].reg_rds;
	for (i = 0; i < bmi_hws[snsr_id].reg_rds_n; i++) {
		for (reg = reg_rd[i].reg_lo; reg <= reg_rd[i].reg_hi; reg++) {
			ret = bmi_i2c_rd(st, snsr_id, reg, 1, &val);
			if (ret)
				t += snprintf(buf + t, PAGE_SIZE - t,
					      "0x%02X=ERR\n", i);
			else
				t += snprintf(buf + t, PAGE_SIZE - t,
					      "0x%02X=0x%02X\n", reg, val);
		}
	}

	return t;
}

static struct iio_fn_dev bmi_fn_dev = {
	.enable = bmi_enable,
	.regs = bmi_regs,
	.freq_read = bmi_freq_read,
	.freq_write = bmi_freq_write,
	.scale_write = bmi_scale_write,
	.read_err = bmi_read_err,
	.get_data = bmi_get_data,
};

static int __maybe_unused bmi_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bmi_state *st = i2c_get_clientdata(client);
	unsigned int i;
	unsigned int old_en_st;
	int ret = 0;
	int temp_ret = 0;

	st->sts |= BMI_STS_SUSPEND;
	st->suspend_en_st = 0;

	for (i = 0; i < st->hw_n; i++) {
		mutex_lock(BMI_MUTEX(st->snsrs[i].bmi_iio));
		/* check if sensor is enabled to begin with */
		old_en_st = bmi_enable(st, st->snsrs[i].cfg.snsr_id, -1);
		if (old_en_st) {
			temp_ret = bmi_enable(st, st->snsrs[i].cfg.snsr_id, 0);
			if (!temp_ret)
				st->suspend_en_st |= old_en_st;

			ret |= temp_ret;
		}
		mutex_unlock(BMI_MUTEX(st->snsrs[i].bmi_iio));
	}

	return ret;
}

static int __maybe_unused bmi_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bmi_state *st = i2c_get_clientdata(client);
	unsigned int i;
	int ret = 0;

	for (i = 0; i < st->hw_n; i++) {
		mutex_lock(BMI_MUTEX(st->snsrs[i].bmi_iio));
		if (st->suspend_en_st & (1 << st->snsrs[i].cfg.snsr_id))
			ret |= bmi_enable(st, st->snsrs[i].cfg.snsr_id, 1);
		mutex_unlock(BMI_MUTEX(st->snsrs[i].bmi_iio));
	}

	st->sts &= ~BMI_STS_SUSPEND;

	return ret;
}

static SIMPLE_DEV_PM_OPS(bmi_pm_ops, bmi_suspend, bmi_resume);

static void bmi_shutdown(struct i2c_client *client)
{
	struct bmi_state *st = i2c_get_clientdata(client);
	unsigned int i;

	st->sts |= BMI_STS_SHUTDOWN;
	for (i = 0; i < st->hw_n; i++) {
		if (st->iio_init_done[i])
			mutex_lock(BMI_MUTEX(st->snsrs[i].bmi_iio));

		if (bmi_enable(st, st->snsrs[i].cfg.snsr_id, -1))
			bmi_enable(st, st->snsrs[i].cfg.snsr_id, 0);

		if (st->iio_init_done[i]) {
			mutex_unlock(BMI_MUTEX(st->snsrs[i].bmi_iio));
		}
	}
}

static void bmi_remove(void *data)
{
	struct i2c_client *client = data;
	struct bmi_state *st = i2c_get_clientdata(client);
	int i;

	if (st != NULL) {
		bmi_shutdown(client);
		for (i = 0; i < st->hw_n; i++) {
			if (st->iio_init_done[i])
				bmi_iio_remove(st->snsrs[i].bmi_iio);
		}
	}

	dev_info(&client->dev, "removed\n");
}

static int bmi_of_dt(struct bmi_state *st, struct device_node *dn)
{
	u32 val32 = 0;
	const char *charp;
	int lenp;

	if (!dn)
		return 0;

	if (!st->i2c_addrs[BMI_HW_ACC]) {
		if (!of_property_read_u32(dn, "accel_i2c_addr", &val32))
			st->i2c_addrs[BMI_HW_ACC] = val32;
		else
			return -ENODEV;
	}


	st->snsrs[BMI_HW_ACC].gis.gpio_in = devm_gpiod_get(&st->i2c->dev, "accel_irq", 0);
	if (IS_ERR(st->snsrs[BMI_HW_ACC].gis.gpio_in)) {
		dev_err(&st->i2c->dev, "accel_irq is not set in DT\n");
		return PTR_ERR(st->snsrs[BMI_HW_ACC].gis.gpio_in);
	}

	st->snsrs[BMI_HW_GYR].gis.gpio_in = devm_gpiod_get(&st->i2c->dev, "gyro_irq", 0);
	if (IS_ERR(st->snsrs[BMI_HW_GYR].gis.gpio_in)) {
		dev_err(&st->i2c->dev, "gyro_irq is not set in DT\n");
		return PTR_ERR(st->snsrs[BMI_HW_GYR].gis.gpio_in);
	}

	if (!of_property_read_u32(dn, "accel_reg_0x53", &val32))
		st->ra_0x53 = (u8)val32;
	if (!of_property_read_u32(dn, "accel_reg_0x54", &val32))
		st->ra_0x54 = (u8)val32;
	if (!of_property_read_u32(dn, "accel_reg_0x58", &val32))
		st->ra_0x58 = (u8)val32;

	if (!of_property_read_u32(dn, "gyro_reg_0x16", &val32))
		st->rg_0x16 = (u8)val32;
	if (!of_property_read_u32(dn, "gyro_reg_0x18", &val32))
		st->rg_0x18 = (u8)val32;

	charp = of_get_property(dn, "accel_matrix", &lenp);
	if (charp && lenp == sizeof(st->snsrs[BMI_HW_ACC].cfg.matrix))
		memcpy(&st->snsrs[BMI_HW_ACC].cfg.matrix, charp, lenp);

	charp = of_get_property(dn, "gyro_matrix", &lenp);
	if (charp && lenp == sizeof(st->snsrs[BMI_HW_GYR].cfg.matrix))
		memcpy(&st->snsrs[BMI_HW_GYR].cfg.matrix, charp, lenp);

	return 0;
}

static int bmi_reset_all(struct bmi_state *st)
{
	int ret = 0;

	ret = bmi_hws[BMI_HW_ACC].fn_softreset(st, BMI_HW_ACC);
	ret |= bmi_hws[BMI_HW_GYR].fn_softreset(st, BMI_HW_GYR);

	return ret;
}

static int bmi_init(struct bmi_state *st, const struct i2c_device_id *id)
{
	unsigned long irqflags;
	unsigned int i;
	int ret;
	struct hte_ts_desc *desc;

	if (id == NULL)
		return -EINVAL;

	st->ra_0x53 = BMI_INT1_OUT_ACTIVE_HIGH;
	st->ra_0x54 = 0x00;
	st->ra_0x58 = BMI_INT1_DTRDY;
	st->rg_0x16 = BMI_REG_INT_3_ACTIVE_HIGH;
	st->rg_0x18 = BMI_REG_INT_3_4_IO_MAP_INT3;
	st->hw_n = BMI_HW_N;
	st->i2c_addrs[BMI_HW_ACC] = 0;
	st->hw_en = 0;
	st->enabled = 0;

	ret = bmi_of_dt(st, st->i2c->dev.of_node);
	if (ret) {
		dev_err(&st->i2c->dev, "of_dt ERR\n");
		return ret;
	}

	st->part = id->driver_data;
	st->i2c_addrs[BMI_HW_GYR] = st->i2c->addr;
	ret = bmi_reset_all(st);
	if (ret) {
		dev_err(&st->i2c->dev, "softreset failed\n");
		return ret;
	}

	bmi_fn_dev.sts = &st->sts;

	/*
	 * 0 - Accl
	 * 1 - Gyro
	 */
	for (i = 0; i < BMI_HW_N; i++) {
		memcpy(&st->snsrs[i].cfg, &bmi_snsr_cfgs[i],
		       sizeof(st->snsrs[i].cfg));

		ret = bmi_08x_iio_init(&st->snsrs[i].bmi_iio, st,
				       &st->i2c->dev, &bmi_fn_dev,
				       &st->snsrs[i].cfg);
		if (ret)
			return -ENODEV;

		st->snsrs[i].cfg.snsr_id = i;
		st->snsrs[i].cfg.part = bmi_i2c_device_ids[st->part].name;
		st->snsrs[i].rrs = &bmi_hws[i].rrs[st->part];
		bmi_max_range(st, i, st->snsrs[i].cfg.max_range.ival);
		st->snsrs[i].gis.dev_name = st->snsrs[i].cfg.name;
		st->iio_init_done[i] = true;
		st->snsrs[i].st = st;
		init_completion(&st->snsrs[i].hte_ts_cmpl);
	}

	ret = bmi_setup_gpio(&st->i2c->dev, st, st->hw_n);
	if (ret < 0)
		return ret;


	desc = devm_kzalloc(&st->i2c->dev, sizeof(*desc)*BMI_HW_N, GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	for (i = 0; i < BMI_HW_N; i++) {
		if (bmi_hws[i].fn_irqflags) {
			irqflags = bmi_hws[i].fn_irqflags(st);
			ret = devm_request_threaded_irq(&st->i2c->dev,
							st->snsrs[i].gis.irq,
							NULL,
							bmi_irq_thread,
							irqflags,
							st->snsrs[i].gis.dev_name,
							&st->snsrs[i]);
			if (ret) {
				dev_err(&st->i2c->dev,
					"req_threaded_irq ERR %d\n", ret);
				return ret;
			}
		}

		ret = hte_init_line_attr(&desc[i], 0, 0, NULL,
				st->snsrs[i].gis.gpio_in);
		if (ret) {
			dev_err(&st->i2c->dev,
					"hte_init_line_attr ERR %d\n", ret);
			return ret;
		}

		ret = hte_ts_get(&st->i2c->dev, &desc[i], i);

		if (ret) {
			dev_err(&st->i2c->dev,
					"hte_ts_get ERR %d\n", ret);
			return ret;
		}

		ret = devm_hte_request_ts_ns(&st->i2c->dev, &desc[i],
						process_hw_ts, NULL,
						&st->snsrs[i]);

		if (ret) {
			dev_err(&st->i2c->dev,
					"devm_hte_request_ts_ns ERR %d\n", ret);
			return ret;
		}
	}

	/*
	 * default rate to slowest speed, this gets reflected in register
	 * during buffer enable time.
	 */
	for (i = 0; i < st->hw_n; i++)
		st->snsrs[i].period_us = st->snsrs[i].cfg.delay_us_max;

	return ret;
}


#if defined(NV_I2C_DRIVER_STRUCT_PROBE_WITHOUT_I2C_DEVICE_ID_ARG) /* Linux 6.3 */
static int bmi_probe(struct i2c_client *client)
#else
static int bmi_probe(struct i2c_client *client, const struct i2c_device_id *id)
#endif
{
	struct bmi_state *st;
	int ret;

	st = devm_kzalloc(&client->dev, sizeof(*st), GFP_KERNEL);
	if (st == NULL)
		return -ENOMEM;

	i2c_set_clientdata(client, st);
	st->i2c = client;
#if KERNEL_VERSION(6, 3, 0) <= LINUX_VERSION_CODE
	ret = bmi_init(st, NULL);
#else
	ret = bmi_init(st, id);
#endif
	if (ret) {
		bmi_remove(client);
		return ret;
	}

	ret = devm_add_action_or_reset(&client->dev, bmi_remove, client);
	if (ret)
		return ret;

	dev_dbg(&client->dev, "done\n");

	return ret;
}

MODULE_DEVICE_TABLE(i2c, bmi_i2c_device_ids);

static const struct of_device_id bmi_of_match[] = {
	{ .compatible = "bmi,bmi088", },
	{}
};

MODULE_DEVICE_TABLE(of, bmi_of_match);

static struct i2c_driver bmi_driver = {
	.class				= I2C_CLASS_HWMON,
	.probe				= bmi_probe,
	.driver				= {
		.name			= BMI_NAME,
		.owner			= THIS_MODULE,
		.of_match_table		= of_match_ptr(bmi_of_match),
		.pm			= &bmi_pm_ops,
	},
	.id_table			= bmi_i2c_device_ids,
};

module_i2c_driver(bmi_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("BMI088 I2C driver");
MODULE_AUTHOR("NVIDIA Corporation");

