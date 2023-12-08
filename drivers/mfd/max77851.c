// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * Maxim MAX77851 MFD Driver
 */

#include <nvidia/conftest.h>

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/mfd/max77851.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define IS_MX_MASTER23(fps) ((fps == MX_FPS_MASTER2) || (fps == MX_FPS_MASTER3))
#define IS_MX_MASTER01(fps) ((fps == MX_FPS_MASTER0) || (fps == MX_FPS_MASTER1))

#define MAX77851_FPS_CNFG_LENGTH    5

struct max77851_chip *max77851_chip;

static const struct resource gpio_resources[] = {
	DEFINE_RES_IRQ(MAX77851_IRQ_TOP_GPIO),
};

static const struct resource power_resources[] = {
	DEFINE_RES_IRQ(MAX77851_IRQ_TOP_LB),
};

static const struct resource rtc_resources[] = {
	DEFINE_RES_IRQ(MAX77851_IRQ_TOP_RTC),
};

static const struct resource thermal_resources[] = {
	DEFINE_RES_IRQ(MAX77851_IRQ_TOP_TJ_SHDN),
	DEFINE_RES_IRQ(MAX77851_IRQ_TOP_TJ_ALM1),
	DEFINE_RES_IRQ(MAX77851_IRQ_TOP_TJ_ALM2),
};

static const struct regmap_irq max77851_top_irqs[] = {
	REGMAP_IRQ_REG(MAX77851_IRQ_TOP_BUCK, 0, TOP_INT0_BUCK_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_TOP_EN, 0, TOP_INT0_EN_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_TOP_FPS, 0, TOP_INT0_FPS_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_TOP_GPIO, 0, TOP_INT0_GPIO_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_TOP_IO, 0, TOP_INT0_IO_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_TOP_LDO, 0, TOP_INT0_LDO_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_TOP_RLOGIC, 0, TOP_INT0_RLOGIC_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_TOP_RTC, 0, TOP_INT0_RTC_I),

	REGMAP_IRQ_REG(MAX77851_IRQ_TOP_UVLO, 1, TOP_INT1_UVLO_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_TOP_LB, 1, TOP_INT1_LB_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_TOP_LDO, 1, TOP_INT1_LB_ALM_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_TOP_OVLO, 1, TOP_INT1_OVLO_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_TOP_TJ_SHDN, 1, TOP_INT1_TJ_SHDN_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_TOP_TJ_ALM1, 1, TOP_INT1_TJ_ALM1_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_TOP_TJ_ALM2, 1, TOP_INT1_TJ_ALM2_I),
	REGMAP_IRQ_REG(MAX77851_IRQ_TOP_TJ_SMPL, 1, TOP_INT1_SMPL_I)
};

static const struct mfd_cell max77851_children[] = {
	{ .name = "max77851-pinctrl", },
	{ .name = "max77851-clock", },
	{ .name = "max77851-regulator", },
	{ .name = "max77851-watchdog", },
	{
		.name = "max77851-gpio",
		.resources = gpio_resources,
		.num_resources = ARRAY_SIZE(gpio_resources),
	}, {
		.name = "max77851-rtc",
		.resources = rtc_resources,
		.num_resources = ARRAY_SIZE(rtc_resources),
	}, {
		.name = "max77851-power",
		.resources = power_resources,
		.num_resources = ARRAY_SIZE(power_resources),
	}, {
		.name = "max77851-thermal",
		.resources = thermal_resources,
		.num_resources = ARRAY_SIZE(thermal_resources),
	},
};

static const struct regmap_range max77851_readable_ranges[] = {
	regmap_reg_range(TOP_ID_REG, BUCK4_CFG7_REG),
};

static const struct regmap_access_table max77851_readable_table = {
	.yes_ranges = max77851_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(max77851_readable_ranges),
};

static const struct regmap_range max77851_writable_ranges[] = {
	regmap_reg_range(TOP_ID_REG, BUCK4_CFG7_REG),
};

static const struct regmap_access_table max77851_writable_table = {
	.yes_ranges = max77851_writable_ranges,
	.n_yes_ranges = ARRAY_SIZE(max77851_writable_ranges),
};

static const struct regmap_config max77851_regmap_config = {
	.name = "max77851-pmic",
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = BUCK4_CFG7_REG + 1,
	.rd_table = &max77851_readable_table,
	.wr_table = &max77851_writable_table,
};

static struct regmap_irq_chip max77851_top_irq_chip = {
	.name = "max77851-top",
	.irqs = max77851_top_irqs,
	.num_irqs = ARRAY_SIZE(max77851_top_irqs),
	.num_regs = 2,
	.status_base = TOP_INT0_REG,
	.mask_base = TOP_MSK0_REG,
};

/*
 * 0 : Mater 0 + Mater 1
 * 1 : Mater 2 + Mater 3
 */
static int max77851_master_register_rw_set(struct regmap *rmap, u8 select)
{
	int ret;
	unsigned int val;

	if (IS_MX_MASTER01(select))
		val = BIT_IS_ZERO;
	else
		val = FPS_CFG_MX_RW;

	ret = regmap_update_bits(rmap, FPS_CFG_REG, FPS_CFG_MX_RW, val);

	return ret;
}

static u8 max77851_get_fps_register_addr(unsigned int fps)
{
	u8 ret;

	switch (fps) {
	case MX_FPS_MASTER0:
		ret = FPS_M02_CFG0_REG;
		break;
	case MX_FPS_MASTER1:
		ret = FPS_M13_CFG0_REG;
		break;
	case MX_FPS_MASTER2:
		ret = FPS_M02_CFG0_REG;
		break;
	case MX_FPS_MASTER3:
		ret = FPS_M13_CFG0_REG;
		break;
	default:
		ret = FPS_M02_CFG0_REG;
	}

	return ret;
}

static int max77851_config_master_fps(struct max77851_chip *chip)
{
	struct device *dev = chip->dev;
	struct device_node *np;
	u32 pval;
	u32 cnfg0, cnfg1;
	int ret = 0;

	np = of_get_child_by_name(dev->of_node, "fps");
	if (!np) {
		dev_err(dev, "FPS master node is not valid: %d\n", ret);
		return ret;
	}

	ret = of_property_read_u32(np, "maxim,power-down-slot-period-us", &pval);
	chip->fps_master_pd_slot_period = (!ret) ? pval : FPS_PERIOD_4KHZ_050US;

	ret = of_property_read_u32(np, "maxim,power-up-slot-period-us", &pval);
	chip->fps_master_pu_slot_period = (!ret) ? pval : FPS_PERIOD_32KHZ_122US;

	cnfg0 = BITS_REAL_VALUE(chip->fps_master_pd_slot_period, MAX77851_FPS_PD_SLOT_MASK) |
			BITS_REAL_VALUE(chip->fps_master_pu_slot_period, MAX77851_FPS_PU_SLOT_MASK);

	ret = regmap_write(chip->rmap, FPS_IM_CFG0_REG, (u8)cnfg0);
	if (ret < 0) {
		dev_err(dev, "Reg 0x%02x write failed, %d\n", FPS_IM_CFG0_REG, ret);
		return ret;
	}

	chip->sleep_on_suspend = of_property_read_bool(np, "maxim,sleep-on-suspend");

	ret = of_property_read_u32(np, "maxim,sleep-exit-slot-period-us", &pval);
	chip->fps_master_slpx_slot_period = (!ret) ? pval : FPS_PERIOD_4KHZ_050US;

	ret = of_property_read_u32(np, "maxim,sleep-entry-slot-period-us", &pval);
	chip->fps_master_slpy_slot_period = (!ret) ? pval : FPS_PERIOD_32KHZ_122US;

	cnfg1 = BITS_REAL_VALUE(chip->fps_master_slpy_slot_period, MAX77851_FPS_SLPY_SLOT_MASK) |
			BITS_REAL_VALUE(chip->fps_master_slpx_slot_period, MAX77851_FPS_SLPX_SLOT_MASK);

	ret = regmap_write(chip->rmap, FPS_IM_CFG1_REG, (u8)cnfg1);
	if (ret < 0) {
		dev_err(dev, "Reg 0x%02x write failed, %d\n", FPS_IM_CFG1_REG, ret);
		return ret;
	}

	return 0;
}

static int max77851_config_fps(struct max77851_chip *chip,
			       struct device_node *fps_np)
{
	struct device *dev = chip->dev;
	//unsigned int mask = 0, config = 0;
	u32 fps_max_period;
	u32 pval;
	unsigned int rval;
	int fps_id;
	int ret;
	char fps_name[10];
	u8 reg_addr;

	fps_max_period = MAX77851_FPS_PERIOD_MAX_US;

	for (fps_id = 0; fps_id < MX_FPS_MASTER_NUM; fps_id++) {
		ret = sprintf(fps_name, "fps%d", fps_id);
		if (ret < 0) {
			dev_err(dev, "sprintf failed with error status: 0x%x\n", ret);
			return -EINVAL;
		}
		if (!strcmp(fps_np->name, fps_name))
			break;
	}

	if (fps_id == MX_FPS_MASTER_NUM) {
		dev_err(dev, "FPS node name fps[%s] is not valid\n", fps_np->name);
		return -EINVAL;
	}

	max77851_master_register_rw_set(chip->rmap, fps_id);

	reg_addr = max77851_get_fps_register_addr(fps_id);

	ret = of_property_read_u32(fps_np, "maxim,fps-enable", &pval);
	chip->fps_master_data[fps_id].enable = (!ret) ? pval : MAX77851_FPS_DEFAULT;

	if (chip->fps_master_data[fps_id].enable == MAX77851_FPS_DEFAULT) {
		dev_info(dev, "fps master[%d]: using default setting\n", fps_id);
		return 0;
	}

	ret = of_property_read_u32(fps_np, "maxim,pd-fps-master-slot", &pval);
	chip->fps_master_data[fps_id].pd_slot = (!ret) ? pval : FPS_MX_MASTER_SLOT_0;

	ret = of_property_read_u32(fps_np, "maxim,pu-fps-master-slot", &pval);
	chip->fps_master_data[fps_id].pu_slot = (!ret) ? pval : FPS_MX_MASTER_SLOT_0;

	ret = of_property_read_u32(fps_np, "maxim,slpy-fps-master-slot", &pval);
	chip->fps_master_data[fps_id].slpy_slot = (!ret) ? pval : FPS_MX_MASTER_SLOT_0;

	ret = of_property_read_u32(fps_np, "maxim,slpx-fps-master-slot", &pval);
	chip->fps_master_data[fps_id].slpx_slot = (!ret) ? pval : FPS_MX_MASTER_SLOT_0;

	ret = of_property_read_u32(fps_np, "maxim,power-down-time-period-us", &pval);
	chip->fps_master_data[fps_id].pd_period = (!ret) ? pval : FPS_PERIOD_4KHZ_100US;

	ret = of_property_read_u32(fps_np, "maxim,power-up-time-period-us", &pval);
	chip->fps_master_data[fps_id].pu_period = (!ret) ? pval : FPS_PERIOD_32KHZ_244US;

	ret = of_property_read_u32(fps_np, "maxim,sleep-entry-time-period-us", &pval);
	chip->fps_master_data[fps_id].slpy_period = (!ret) ? pval : FPS_PERIOD_32KHZ_244US;

	ret = of_property_read_u32(fps_np, "maxim,sleep-exit-time-period-us", &pval);
	chip->fps_master_data[fps_id].slpx_period = (!ret) ? pval : FPS_PERIOD_4KHZ_100US;

	ret = of_property_read_u32(fps_np, "maxim,abort-enable", &pval);
	chip->fps_master_data[fps_id].abort_enable = (!ret) ? pval : MAX77851_FPS_ABORT_DISABLE;

	ret = of_property_read_u32(fps_np, "maxim,sleep-enable", &pval);
	chip->fps_master_data[fps_id].sleep_mode = (!ret) ? pval : MAX77851_FPS_SLEEP_DISABLE;

	ret = of_property_read_u32(fps_np, "maxim,abort-mode", &pval);
	chip->fps_master_data[fps_id].abort_mode = (!ret) ? pval : MAX77851_FPS_ABORT_NEXT_SLOT;

	ret = of_property_read_u32(fps_np, "maxim,pd-max-slot", &pval);
	chip->fps_master_data[fps_id].pd_max_slot = (!ret) ? pval : MAX77851_FPS_16_SLOTS;

	ret = of_property_read_u32(fps_np, "maxim,pu-max-slot", &pval);
	chip->fps_master_data[fps_id].pu_max_slot = (!ret) ? pval : MAX77851_FPS_16_SLOTS;

	ret = of_property_read_u32(fps_np, "maxim,slpy-max-slot", &pval);
	chip->fps_master_data[fps_id].slpy_max_slot = (!ret) ? pval : MAX77851_FPS_16_SLOTS;

	ret = of_property_read_u32(fps_np, "maxim,slpx-max-slot", &pval);
	chip->fps_master_data[fps_id].slpx_max_slot = (!ret) ? pval : MAX77851_FPS_16_SLOTS;

	rval = BITS_REAL_VALUE(chip->fps_master_data[fps_id].pd_slot, FPS_CFG0_PD) |
			BITS_REAL_VALUE(chip->fps_master_data[fps_id].pu_slot, FPS_CFG0_PU) |
			BITS_REAL_VALUE(chip->fps_master_data[fps_id].enable, FPS_CFG0_EN) |
			BITS_REAL_VALUE(chip->fps_master_data[fps_id].abort_enable, FPS_CFG0_ABT_EN);

	ret = regmap_write(chip->rmap, reg_addr, rval);
	if (ret < 0) {
		dev_err(dev, "Reg 0x%02x write failed, %d\n", FPS_M02_CFG0_REG, ret);
		return ret;
	}

	rval = BITS_REAL_VALUE(chip->fps_master_data[fps_id].slpy_slot, FPS_CFG1_SLPY) |
			BITS_REAL_VALUE(chip->fps_master_data[fps_id].slpx_slot, FPS_CFG1_SLPX) |
			BITS_REAL_VALUE(chip->fps_master_data[fps_id].sleep_mode, FPS_CFG1_SLP_EN) |
			BITS_REAL_VALUE(chip->fps_master_data[fps_id].abort_mode, FPS_CFG1_ABT);

	ret = regmap_write(chip->rmap, (reg_addr + 1), rval);
	if (ret < 0) {
		dev_err(dev, "Reg 0x%02x write failed, %d\n", FPS_M02_CFG1_REG, ret);
		return ret;
	}

	rval = BITS_REAL_VALUE(chip->fps_master_data[fps_id].pd_period, FPS_CFG2_PD_T) |
			BITS_REAL_VALUE(chip->fps_master_data[fps_id].pu_period, FPS_CFG2_PU_T);

	ret = regmap_write(chip->rmap, (reg_addr + 2), rval);
	if (ret < 0) {
		dev_err(dev, "Reg 0x%02x write failed, %d\n", FPS_M02_CFG2_REG, ret);
		return ret;
	}

	rval = BITS_REAL_VALUE(chip->fps_master_data[fps_id].slpy_period, FPS_CFG3_SLPY_T) |
			BITS_REAL_VALUE(chip->fps_master_data[fps_id].slpx_period, FPS_CFG3_SLPX_T);

	ret = regmap_write(chip->rmap, (reg_addr + 3), rval);
	if (ret < 0) {
		dev_err(dev, "Reg 0x%02x write failed, %d\n", FPS_M02_CFG3_REG, ret);
		return ret;
	}

	rval = BITS_REAL_VALUE(chip->fps_master_data[fps_id].pd_max_slot, FPS_CFG4_PD_MAX) |
			BITS_REAL_VALUE(chip->fps_master_data[fps_id].slpy_max_slot, FPS_CFG4_SLPY_MAX) |
			BITS_REAL_VALUE(chip->fps_master_data[fps_id].pu_max_slot, FPS_CFG4_PU_MAX) |
			BITS_REAL_VALUE(chip->fps_master_data[fps_id].slpx_max_slot, FPS_CFG4_SLPX_MAX);

	ret = regmap_write(chip->rmap, (reg_addr + 4), rval);
	if (ret < 0) {
		dev_err(dev, "Reg 0x%02x write failed, %d\n", FPS_M02_CFG4_REG, ret);
		return ret;
	}

	return 0;
}

static int max77851_initialise_fps(struct max77851_chip *chip)
{
	struct device *dev = chip->dev;
	struct device_node *fps_np, *fps_child;
	unsigned int val_buf[5];
	unsigned int val;
	unsigned int *pval;
	int fps_num;
	int ret;
	u8 reg_addr;

	ret = regmap_read(chip->rmap, FPS_IM_CFG0_REG, &val);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read %d\n", ret);
		return ret;
	}
	chip->fps_master_pd_slot_period = BITS_VALUE(val, FPS_IM_CFG0_PD_T);
	chip->fps_master_pu_slot_period = BITS_VALUE(val, FPS_IM_CFG0_PU_T);

	ret = regmap_read(chip->rmap, FPS_IM_CFG1_REG, &val);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read %d\n", ret);
		return ret;
	}
	chip->fps_master_slpy_slot_period = BITS_VALUE(val, FPS_IM_CFG1_SLPY_T);
	chip->fps_master_slpx_slot_period = BITS_VALUE(val, FPS_IM_CFG1_SLPX_T);

	/* Master 0/1/2/3 Configiuration */

	for (fps_num = 0 ; fps_num < MX_FPS_MASTER_NUM; fps_num++) {
		max77851_master_register_rw_set(chip->rmap, fps_num);

		reg_addr = max77851_get_fps_register_addr(fps_num);

		ret = regmap_raw_read(chip->rmap, reg_addr, val_buf, MAX77851_FPS_CNFG_LENGTH);
		if (ret < 0) {
			dev_err(chip->dev, "Failed to read %d\n", ret);
			return ret;
		}

		pval = val_buf;

		chip->fps_master_data[fps_num].pd_slot = BITS_VALUE(val_buf[0], FPS_CFG0_PD_MASK);
		chip->fps_master_data[fps_num].enable = BITS_VALUE(val_buf[0], FPS_CFG0_EN_MASK);
		chip->fps_master_data[fps_num].pu_slot = BITS_VALUE(val_buf[0], FPS_CFG0_PU_MASK);
		chip->fps_master_data[fps_num].abort_enable = BITS_VALUE(val_buf[0], FPS_CFG0_ABT_EN_MASK);

		chip->fps_master_data[fps_num].slpx_slot = BITS_VALUE(val_buf[1], FPS_CFG1_SLPX_MASK);
		chip->fps_master_data[fps_num].slpy_slot = BITS_VALUE(val_buf[1], FPS_CFG1_SLPY_MASK);
		chip->fps_master_data[fps_num].sleep_mode = BITS_VALUE(val_buf[1], FPS_CFG1_SLP_EN_MASK);
		chip->fps_master_data[fps_num].abort_mode = BITS_VALUE(val_buf[1], FPS_CFG1_ABT_MASK);

		chip->fps_master_data[fps_num].pd_period = BITS_VALUE(val_buf[2], FPS_CFG2_PD_T_MASK);
		chip->fps_master_data[fps_num].pu_period = BITS_VALUE(val_buf[2], FPS_CFG2_PU_T_MASK);

		chip->fps_master_data[fps_num].slpx_period = BITS_VALUE(val_buf[3], FPS_CFG3_SLPX_T_MASK);
		chip->fps_master_data[fps_num].slpy_period = BITS_VALUE(val_buf[3], FPS_CFG3_SLPY_T_MASK);

		chip->fps_master_data[fps_num].pd_max_slot = BITS_VALUE(val_buf[4], FPS_CFG4_PD_MAX_MASK);
		chip->fps_master_data[fps_num].pu_max_slot = BITS_VALUE(val_buf[4], FPS_CFG4_PU_MAX_MASK);
		chip->fps_master_data[fps_num].slpy_max_slot = BITS_VALUE(val_buf[4], FPS_CFG4_SLPY_MAX_MASK);
		chip->fps_master_data[fps_num].slpx_max_slot = BITS_VALUE(val_buf[4], FPS_CFG4_SLPX_MAX_MASK);
	}

	/* FPS Master */
	max77851_config_master_fps(chip);

	fps_np = of_get_child_by_name(dev->of_node, "fps");
	if (!fps_np)
		return 0;

	for_each_child_of_node(fps_np, fps_child) {
		ret = max77851_config_fps(chip, fps_child);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int max77851_init_low_battery_monitor(struct max77851_chip *chip)
{
	struct device *dev = chip->dev;
	struct device_node *np;
	u32 pval;
	u8 mask = 0;
	u8 val = 0;

	int low_bat_en = 0;
	int low_bat_alm_en = 0;

	int ret;

	np = of_get_child_by_name(dev->of_node, "low-battery-monitor");
	if (!np)
		return 0;

	ret = of_property_read_u32(np,
			"maxim,low-battery-enable", &pval);
	low_bat_en = (!ret) ? pval : MAX77851_LOW_BAT_ENABLE;

	ret = of_property_read_u32(np,
			"maxim,low-battery-alarm-enable", &pval);
	low_bat_alm_en = (!ret) ? pval : MAX77851_LOW_BAT_ALARM_ENABLE;

	val = BITS_REAL_VALUE(low_bat_en, TOP_CFG0_LB_EN) |
			BITS_REAL_VALUE(low_bat_alm_en, TOP_CFG0_LB_ALM_EN);

	mask = TOP_CFG0_LB_EN | TOP_CFG0_LB_ALM_EN;

	ret = regmap_update_bits(chip->rmap, TOP_CFG0_REG, mask, val);
	if (ret < 0) {
		dev_err(dev, "Reg 0x%02x update failed, %d\n", TOP_CFG0_REG, ret);
		return ret;
	}

	return 0;
}

static int max77851_read_version(struct max77851_chip *chip)
{
	unsigned int val;
	u8 cid_val[3];
	int i;
	int ret;

	for (i = TOP_ID_REG; i <= TOP_OTP_REV_REG; i++) {
		ret = regmap_read(chip->rmap, i, &val);
		if (ret < 0) {
			dev_err(chip->dev, "Failed to read CID: %d\n", ret);
			return ret;
		}
		cid_val[i] = val;
	}

	dev_info(chip->dev, "PMIC(0x%X) OTP Revision:0x%X, Device Revision:0x%X\n",
		 cid_val[0], cid_val[2], cid_val[1]);

	return ret;
}

static void max77851_pm_power_off(void)
{
	struct max77851_chip *chip = max77851_chip;

	regmap_update_bits(chip->rmap, FPS_SW_REG,
			   FPS_SW_COLD_RST, FPS_SW_COLD_RST);
}

#if defined(NV_I2C_DRIVER_STRUCT_PROBE_WITHOUT_I2C_DEVICE_ID_ARG) /* Linux 6.3 */
static int max77851_probe(struct i2c_client *client)
#else
static int max77851_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
#endif
{
	const struct regmap_config *rmap_config;
	struct max77851_chip *chip;
	const struct mfd_cell *mfd_cells;
	int n_mfd_cells;
	bool pm_off;
	int ret;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	i2c_set_clientdata(client, chip);
	chip->dev = &client->dev;
	chip->irq_base = -1;
	chip->chip_irq = client->irq;

	mfd_cells = max77851_children;
	n_mfd_cells = ARRAY_SIZE(max77851_children);
	rmap_config = &max77851_regmap_config;


	chip->rmap = devm_regmap_init_i2c(client, rmap_config);
	if (IS_ERR(chip->rmap)) {
		ret = PTR_ERR(chip->rmap);
		dev_err(chip->dev, "Failed to initialise regmap: %d\n", ret);
		return ret;
	}

	ret = max77851_read_version(chip);
	if (ret < 0)
		return ret;

	max77851_top_irq_chip.irq_drv_data = chip;
	ret = devm_regmap_add_irq_chip(chip->dev, chip->rmap, client->irq,
				       IRQF_ONESHOT | IRQF_SHARED,
				       chip->irq_base, &max77851_top_irq_chip,
				       &chip->top_irq_data);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to add regmap irq: %d\n", ret);
		return ret;
	}

	ret = max77851_initialise_fps(chip);
	if (ret < 0)
		return ret;

	ret = max77851_init_low_battery_monitor(chip);
	if (ret < 0)
		return ret;

	ret =  devm_mfd_add_devices(chip->dev, PLATFORM_DEVID_NONE,
				    mfd_cells, n_mfd_cells, NULL, 0,
				    regmap_irq_get_domain(chip->top_irq_data));
	if (ret < 0) {
		dev_err(chip->dev, "Failed to add MFD children: %d\n", ret);
		return ret;
	}

	pm_off = of_device_is_system_power_controller(client->dev.of_node);

	//if (pm_off && !pm_power_off) {
	if (pm_off) {
		max77851_chip = chip;
		pm_power_off = max77851_pm_power_off;
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP

static int max77851_i2c_suspend(struct device *dev)
{
	struct max77851_chip *chip = dev_get_drvdata(dev);
	struct i2c_client *client = to_i2c_client(dev);
	int ret;

	if (chip->sleep_on_suspend) {
		/* FPS on -> sleep */
		ret = regmap_write(chip->rmap, FPS_SW_REG, FPS_SW_SLP);
		if (ret < 0) {
			dev_err(dev, "Reg 0x%02x write failed, %d\n", FPS_SW_REG, ret);
			return ret;
		}
	}

	disable_irq(client->irq);

	return 0;
}

static int max77851_i2c_resume(struct device *dev)
{
	struct max77851_chip *chip = dev_get_drvdata(dev);
	struct i2c_client *client = to_i2c_client(dev);
	int ret;

	if (chip->sleep_on_suspend) {
		/* FPS sleep -> on */
		ret = regmap_write(chip->rmap, FPS_SW_REG, FPS_SW_ON);
		if (ret < 0) {
			dev_err(dev, "Reg 0x%02x write failed, %d\n", FPS_SW_REG, ret);
			return ret;
		}
	}

	enable_irq(client->irq);

	return 0;
}
#endif

static const struct i2c_device_id max77851_id[] = {
	{"maxim,max77851-pmic", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, max77851_id);

#ifdef CONFIG_OF
static const struct of_device_id max77851_of_match[] = {
	{.compatible = "maxim,max77851-pmic",},
	{},
};
MODULE_DEVICE_TABLE(of, max77851_of_match);
#endif

static const struct dev_pm_ops max77851_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(max77851_i2c_suspend, max77851_i2c_resume)
};

static struct i2c_driver max77851_driver = {
	.driver = {
		.name = "maxim,max77851-pmic",
		.of_match_table = of_match_ptr(max77851_of_match),
		.pm = &max77851_pm_ops,
	},
	.probe = max77851_probe,
	.id_table = max77851_id,
};

#if 0
static int __init max77851_init(void)
{
	return i2c_add_driver(&max77851_driver);
}
module_init(max77851_init);

static void __exit max77851_exit(void)
{
	i2c_del_driver(&max77851_driver);
}
module_exit(max77851_exit);
#endif

module_i2c_driver(max77851_driver);

MODULE_DESCRIPTION("MAX77851 Multi Function Device Core Driver");
MODULE_AUTHOR("Shubhi Garg<shgarg@nvidia.com>");
MODULE_AUTHOR("Joan Na<Joan.na@maximintegrated.com>");
MODULE_ALIAS("i2c:max77851");
MODULE_LICENSE("GPL v2");
