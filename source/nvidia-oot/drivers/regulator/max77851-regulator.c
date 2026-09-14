// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * Maxim MAX77851 Regulator driver
 */

#include <linux/init.h>
#include <linux/mfd/max77851.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

#define max77851_rails(_name)	"max77851-"#_name

#define IS_BUCK(type) (type == MAX77851_REGULATOR_TYPE_BUCK)
#define IS_NLDO(type) (type == MAX77851_REGULATOR_TYPE_LDO_N)
#define IS_PLDO(type) (type == MAX77851_REGULATOR_TYPE_LDO_P)

/* Power Mode */
#define MAX77851_POWER_MODE_NORMAL		BIT_IS_ZERO
#define MAX77851_POWER_MODE_LPM			BIT(1)

#define MAX77851_POWER_MODE_MASK		BIT(1)

/* SD Slew Rate */
#define MAX77851_SD_SR_13_75			0
#define MAX77851_SD_SR_27_5			1
#define MAX77851_SD_SR_55			2
#define MAX77851_SD_SR_100			3

/* FPS ENABLE / DISABLE */
#define MAX77851_FPS_MODE_DISABLE		0
#define MAX77851_FPS_MODE_ENABLE		1

#define MAX77851_DVS_VOLTAGE_NUM		2

#define MAX77851_BUCK_RAMP_DELAY		10000	/* uV/us */
#define MAX77851_LDO_ENABLE_TIME		100	/* us */
#define MAX77851_BUCK_ENABLE_TIME		100	/* us */

enum max77851_ramp_rate {
	RAMP_RATE_0P15MV,
	RAMP_RATE_0P625MV,
	RAMP_RATE_1P25MV,
	RAMP_RATE_2P5MV,
	RAMP_RATE_5MV,
	RAMP_RATE_10MV,
	RAMP_RATE_20MV,
	RAMP_RATE_40MV,
	RAMP_RATE_NO_CTRL,	/* 100mV/us */
};

enum max77851_regulators_ldo {
	MAX77851_REGULATOR_ID_LDO0,
	MAX77851_REGULATOR_ID_LDO1,
	MAX77851_REGULATOR_ID_LDO2,
	MAX77851_REGULATOR_ID_LDO3,
	MAX77851_REGULATOR_ID_LDO4,
	MAX77851_REGULATOR_ID_LDO5,
	MAX77851_REGULATOR_ID_LDO6,

	MAX77851_REGULATOR_ID_BUCK0,
	MAX77851_REGULATOR_ID_BUCK1,
	MAX77851_REGULATOR_ID_BUCK2,
	MAX77851_REGULATOR_ID_BUCK3,
	MAX77851_REGULATOR_ID_BUCK4,
	MAX77851_REGULATOR_ID_NUM,
};

/* Regulator types */
enum max77851_regulator_type {
	/* LDO 0-3 */
	MAX77851_REGULATOR_TYPE_LDO_N,
	/* LDO 4-6 */
	MAX77851_REGULATOR_TYPE_LDO_P,
	/* BUCK */
	MAX77851_REGULATOR_TYPE_BUCK,
};

struct max77851_regulator_data {
	u8 type;

	u8 cfg_addr;
	u8 vout0_addr;
	u8 vout1_addr;
	u8 ramp_delay_addr;

	/* Common */
	unsigned int en;
	unsigned int lp_mode_en;
	unsigned int power_mode;

	unsigned int opmode;
	unsigned int fps_mode_en;

	unsigned int voltage[MAX77851_DVS_VOLTAGE_NUM];

	struct regulator_desc desc;
};

/* Main Regulator Resource */
struct max77851_regulator {
	struct device *dev;
	struct regmap *rmap;

	unsigned int num_reulator;

	/* FPS */
	struct max77851_fps_data *fps_data;

	/* Regulator */
	struct max77851_regulator_data *reg_data;
};

static int max77851_regulator_set_ramp_delay(struct regulator_dev *rdev, int ramp_delay)
{
	struct max77851_regulator *pmic = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	struct max77851_regulator_data *rdata = &pmic->reg_data[id];
	unsigned int ramp_value;
	int ret;

	switch (ramp_delay) {
	case 1 ... 150:
		ramp_value = RAMP_RATE_0P15MV;
		break;
	case 151 ... 625:
		ramp_value = RAMP_RATE_0P625MV;
		break;
	case 626 ... 1250:
		ramp_value = RAMP_RATE_1P25MV;
		break;
	case 1251 ... 2500:
		ramp_value = RAMP_RATE_1P25MV;
		break;
	case 2501 ... 5000:
		ramp_value = RAMP_RATE_5MV;
		break;
	case 5001 ... 10000:
		ramp_value = RAMP_RATE_10MV;
		break;
	case 10001 ... 20000:
		ramp_value = RAMP_RATE_20MV;
		break;
	case 20001 ... 40000:
		ramp_value = RAMP_RATE_40MV;
		break;
	default:
		pr_warn("%s: ramp_delay: %d not supported\n", rdev->desc->name, ramp_delay);
		return -EINVAL;
	}

	ret = regmap_update_bits(rdev->regmap, rdata->ramp_delay_addr,
				  BUCK_CFG5_RD_SR, ramp_value << FFS(BUCK_CFG5_RD_SR));
	if (ret < 0) {
		dev_err(pmic->dev, "Reg 0x%02x read failed %d\n", rdata->ramp_delay_addr, ret);
		return ret;
	}

	ret = regmap_update_bits(rdev->regmap, rdata->ramp_delay_addr,
				  BUCK_CFG5_RU_SR, ramp_value << FFS(BUCK_CFG5_RU_SR));
	if (ret < 0) {
		dev_err(pmic->dev, "Reg 0x%02x read failed %d\n", rdata->ramp_delay_addr, ret);
		return ret;
	}

	return ret;
}

static int max77851_regulator_get_enable(struct max77851_regulator *pmic,
					     int id)
{
	struct max77851_regulator_data *reg_data = &pmic->reg_data[id];
	unsigned int val;
	int ret;

	ret = regmap_read(pmic->rmap, reg_data->cfg_addr, &val);
	if (ret < 0) {
		dev_err(pmic->dev, "Regulator %d: Reg 0x%02x read failed: %d\n",
			id, reg_data->cfg_addr, ret);
		return ret;
	}

	return BITS_VALUE(val, REGULATOR_ENABLE);
}

static int max77851_regulator_set_enable(struct max77851_regulator *pmic,
					     int id)
{
	struct max77851_regulator_data *reg_data = &pmic->reg_data[id];
	unsigned int val;
	int ret;

	val = REGULATOR_ENABLE;
	ret = regmap_update_bits(pmic->rmap, reg_data->cfg_addr,
					 REGULATOR_ENABLE_MASK, val);

	return ret;
}

static int max77851_regulator_set_disable(struct max77851_regulator *pmic,
					     int id)
{
	struct max77851_regulator_data *reg_data = &pmic->reg_data[id];
	unsigned int val;
	int ret;

	val = REGULATOR_DISABLE;
	ret = regmap_update_bits(pmic->rmap, reg_data->cfg_addr,
					 REGULATOR_ENABLE_MASK, val);

	return ret;
}

static int max77851_regulator_enable(struct regulator_dev *rdev)
{
	struct max77851_regulator *pmic = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	struct max77851_regulator_data *reg_data = &pmic->reg_data[id];

	if (reg_data->fps_mode_en != MAX77851_FPS_MODE_DISABLE)
		return 0;

	return max77851_regulator_set_enable(pmic, id);
}

static int max77851_regulator_disable(struct regulator_dev *rdev)
{
	struct max77851_regulator *pmic = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	struct max77851_regulator_data *reg_data = &pmic->reg_data[id];

	if (reg_data->fps_mode_en != MAX77851_FPS_MODE_DISABLE)
		return 0;

	return max77851_regulator_set_disable(pmic, id);
}

static int max77851_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct max77851_regulator *pmic = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	struct max77851_regulator_data *reg_data = &pmic->reg_data[id];
	int ret;

	if (reg_data->fps_mode_en != MAX77851_FPS_MODE_DISABLE)
		return 1;

	ret = max77851_regulator_get_enable(pmic, id);
	if (ret < 0)
		return ret;

	if (ret & REGULATOR_ENABLE)
		return 1;

	return 0;
}

static int max77851_regulator_set_power_mode(struct max77851_regulator *pmic,
					     int power_mode, int id)
{
	struct max77851_regulator_data *reg_data = &pmic->reg_data[id];
	u8 mask = MAX77851_POWER_MODE_MASK;
	int ret;

	ret = regmap_update_bits(pmic->rmap, reg_data->cfg_addr, mask, power_mode);
	if (ret < 0) {
		dev_err(pmic->dev, "Regulator %d mode set failed: %d\n",
			id, ret);
		return ret;
	}

	reg_data->power_mode = power_mode;

	return ret;
}

static int max77851_regulator_get_power_mode(struct max77851_regulator *pmic,
						 int id)
{
	struct max77851_regulator_data *reg_data = &pmic->reg_data[id];
	unsigned int val;
	int ret;

	ret = regmap_read(pmic->rmap, reg_data->cfg_addr, &val);
	if (ret < 0) {
		dev_err(pmic->dev, "Regulator %d: Reg 0x%02x read failed: %d\n",
			id, reg_data->cfg_addr, ret);
		return ret;
	}

	return (val & MAX77851_POWER_MODE_MASK);
}

static unsigned int max77851_regulator_get_mode(struct regulator_dev *rdev)
{
	struct max77851_regulator *pmic = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	struct max77851_regulator_data *reg_data = &pmic->reg_data[id];
	int ret;
	int power_mode, opmode;

	ret = max77851_regulator_get_power_mode(pmic, id);
	if (ret < 0)
		return 0;

	power_mode = ret;

	switch (power_mode) {
	case MAX77851_POWER_MODE_NORMAL:
		opmode = REGULATOR_MODE_NORMAL;
		break;
	case MAX77851_POWER_MODE_LPM:
		opmode = REGULATOR_MODE_IDLE;
		break;
	default:
		return 0;
	}

	reg_data->opmode = opmode;

	return opmode;
}

static int max77851_regulator_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct max77851_regulator *pmic = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	unsigned int power_mode = MAX77851_POWER_MODE_NORMAL;
	struct max77851_regulator_data *reg_data = &pmic->reg_data[id];

	switch (mode) {
	case REGULATOR_MODE_IDLE:
		power_mode = MAX77851_POWER_MODE_LPM; /* ON in Low Power Mode */
		break;
	case REGULATOR_MODE_NORMAL:
		power_mode = MAX77851_POWER_MODE_NORMAL; /* ON in Normal Mode */
		break;
	default:
		dev_warn(&rdev->dev, "%s: regulator mode: 0x%x not supported\n",
			 rdev->desc->name, mode);
		return -EINVAL;
	}
	reg_data->power_mode = power_mode;
	reg_data->opmode = mode;

	return max77851_regulator_set_power_mode(pmic, power_mode, id);
}

static int max77851_regulator_intial(struct max77851_regulator *pmic, int id)
{
	struct max77851_regulator_data *reg_data = &pmic->reg_data[id];
	unsigned int val;
	int ret;

	ret = regmap_read(pmic->rmap, reg_data->cfg_addr, &val);
	if (ret < 0) {
		dev_err(pmic->dev, "Reg 0x%02x read failed %d\n", reg_data->cfg_addr, ret);
		return ret;
	}

	reg_data->en = ret & BIT(0) ? 1 : 0;
	reg_data->lp_mode_en = ret & BIT(2) ? 1 : 0;

	return 0;
}

static int max77851_regulator_set_vout_range(struct max77851_regulator *pmic,
						u32 val, int id)
{
	struct max77851_regulator_data *reg_data = &pmic->reg_data[id];
	u8 reg_addr;
	int mask, shift;
	int ret;
	unsigned int read_val;

	ret = regmap_read(pmic->rmap, reg_data->cfg_addr, &read_val);

	if (IS_BUCK(reg_data->type)) {
		reg_addr = reg_data->cfg_addr;
		mask = BUCK_CFG0_VOUT_RNG;
		shift = FFS(BUCK_CFG0_VOUT_RNG);
	} else {
		if (IS_NLDO(reg_data->type)) {
			reg_addr = reg_data->cfg_addr;
			mask = LDO_CFG0_VOUT_RNG;
			shift = FFS(LDO_CFG0_VOUT_RNG);
		} else {
			dev_info(pmic->dev, "PMOS is not supported : %d\n", ret);
			return 0;
		}
	}

	ret = regmap_update_bits(pmic->rmap, reg_addr, mask, BITS_REAL_VALUE(val, mask));
	if (ret < 0) {
		dev_err(pmic->dev, "Regulator %d range set failed: %d\n",
			id, ret);
		return ret;
	}

	ret = regmap_read(pmic->rmap, reg_data->cfg_addr, &read_val);

	return ret;
}

static int max77851_regulator_set_vout_voltage(struct max77851_regulator *pmic,
						unsigned int *vout, int id)
{
	struct max77851_regulator_data *reg_data = &pmic->reg_data[id];
	int ret;

	if (IS_BUCK(reg_data->type)) {
		ret = regmap_write(pmic->rmap, reg_data->vout0_addr, vout[0]);
		if (ret < 0) {
			dev_err(pmic->dev, "Regulator %d vout set failed: %d\n", id, ret);
			return ret;
		}
		ret = regmap_write(pmic->rmap, reg_data->vout1_addr, vout[1]);
		if (ret < 0) {
			dev_err(pmic->dev, "Regulator %d vout set failed: %d\n", id, ret);
			return ret;
		}
	} else {
		/* Register set vout0 */
		ret = regmap_update_bits(pmic->rmap, reg_data->cfg_addr, LDO_CFG0_VOUT_RW, BIT_IS_ZERO);
		if (ret < 0) {
			dev_err(pmic->dev, "Regulator %d register set failed: %d\n",
				id, ret);
			return ret;
		}
		ret = regmap_write(pmic->rmap, reg_data->vout0_addr, vout[0]);
		if (ret < 0) {
			dev_err(pmic->dev, "Regulator %d vout set failed: %d\n", id, ret);
			return ret;
		}

		/* Register set vout1 */
		ret = regmap_update_bits(pmic->rmap, reg_data->cfg_addr, LDO_CFG0_VOUT_RW, LDO_CFG0_VOUT_RW);
		if (ret < 0) {
			dev_err(pmic->dev, "Regulator %d register set failed: %d\n",
				id, ret);
			return ret;
		}
		ret = regmap_write(pmic->rmap, reg_data->vout1_addr, vout[1]);
		if (ret < 0) {
			dev_err(pmic->dev, "Regulator %d vout set failed: %d\n", id, ret);
			return ret;
		}
	}

	return ret;
}

static int max77851_regulator_set_fps(struct max77851_regulator *pmic, int id)
{
	struct max77851_fps_data *fps_data = &pmic->fps_data[id];
	unsigned int val;
	int mask, shift;
	u8 reg_addr;
	int ret;

	mask = MAX77851_FPS_PU_SLPX_SLOT_MASK;
	shift = FFS(MAX77851_FPS_PU_SLPX_SLOT_MASK);
	val = fps_data->pu_slpx_master_slot;
	reg_addr = fps_data->fps_cfg0_addr;

	ret = regmap_update_bits(pmic->rmap, reg_addr, mask, val << shift);
	if (ret < 0) {
		dev_err(pmic->dev, "PY SLPX MASTER SLOT update failed: %d\n", ret);
		return ret;
	}

	mask = MAX77851_FPS_PD_SLPY_SLOT_MASK;
	shift = FFS(MAX77851_FPS_PD_SLPY_SLOT_MASK);

	val = fps_data->pd_slpy_master_slot;
	reg_addr = fps_data->fps_cfg0_addr;

	ret = regmap_update_bits(pmic->rmap, reg_addr, mask, val << shift);
	if (ret < 0) {
		dev_err(pmic->dev, "PD SLPY MASTER SLOT update failed: %d\n", ret);
		return ret;
	}

	mask = MAX77851_FPS_PU_SLOT_MASK;
	shift = FFS(MAX77851_FPS_PU_SLOT_MASK);

	val = fps_data->pu_slot;
	reg_addr = fps_data->fps_cfg1_addr;

	ret = regmap_update_bits(pmic->rmap, reg_addr, mask, val << shift);
	if (ret < 0) {
		dev_err(pmic->dev, "PU SLOT update failed: %d\n", ret);
		return ret;
	}

	mask = MAX77851_FPS_PD_SLOT_MASK;
	shift = FFS(MAX77851_FPS_PD_SLOT_MASK);

	val = fps_data->pd_slot;
	reg_addr = fps_data->fps_cfg1_addr;

	ret = regmap_update_bits(pmic->rmap, reg_addr, mask, val << shift);
	if (ret < 0) {
		dev_err(pmic->dev, "PD SLOT update failed: %d\n", ret);
		return ret;
	}

	mask = MAX77851_FPS_SLPX_SLOT_MASK;
	shift = FFS(MAX77851_FPS_SLPX_SLOT_MASK);

	val = fps_data->slpx_slot;
	reg_addr = fps_data->fps_cfg2_addr;

	ret = regmap_update_bits(pmic->rmap, reg_addr, mask, val << shift);
	if (ret < 0) {
		dev_err(pmic->dev, "SLPX SLOT update failed: %d\n", ret);
		return ret;
	}

	mask = MAX77851_FPS_SLPY_SLOT_MASK;
	shift = FFS(MAX77851_FPS_SLPY_SLOT_MASK);

	val = fps_data->slpy_slot;
	reg_addr = fps_data->fps_cfg2_addr;
	ret = regmap_update_bits(pmic->rmap, reg_addr, mask, val << shift);
	if (ret < 0) {
		dev_err(pmic->dev, "SLPY SLOT update failed: %d\n", ret);
		return ret;
	}

	return ret;
}

static int max77851_of_parse_cb(struct device_node *np,
				const struct regulator_desc *desc,
				struct regulator_config *config)
{
	struct max77851_regulator *pmic = config->driver_data;
	struct max77851_fps_data *fps_data = &pmic->fps_data[desc->id];
	struct max77851_regulator_data *reg_data = &pmic->reg_data[desc->id];
	unsigned int voltage[MAX77851_VOUT_NUM];
	u32 pval;
	int ret;

	/* Use FPS Default Data */
	if (of_property_read_bool(np, "maxim,fps-default-enable")) {
		reg_data->fps_mode_en = MAX77851_FPS_MODE_ENABLE;
		return 0;
	}

	reg_data->fps_mode_en = MAX77851_FPS_MODE_DISABLE;

	/* VOUT Range */
	ret = of_property_read_u32(np, "maxim,out-voltage-range", &pval);
	if (ret < 0)
		pval = MAX77851_VOUT_RNG_LOW;

	max77851_regulator_set_vout_range(pmic, pval, desc->id);

	if (of_property_read_bool(np, "maxim,fps-user-setting-enable")) {
		ret = of_property_read_u32(np, "maxim,pu-slpx-master-slot", &pval);
		fps_data->pu_slpx_master_slot = (!ret) ? pval : MAX77851_FPS_MASTER_SLOT_0;

		ret = of_property_read_u32(np, "maxim,pd-slpy-master-slot", &pval);
		fps_data->pd_slpy_master_slot = (!ret) ? pval : MAX77851_FPS_MASTER_SLOT_0;

		ret = of_property_read_u32(np, "maxim,pu-slot", &pval);
		fps_data->pu_slot = (!ret) ? pval : MAX77851_FPS_SLOT_0;

		ret = of_property_read_u32(np, "maxim,pd-slot", &pval);
		fps_data->pd_slot = (!ret) ? pval : MAX77851_FPS_SLOT_0;

		ret = of_property_read_u32(np, "maxim,slpy-slot", &pval);
		fps_data->slpy_slot = (!ret) ? pval : MAX77851_FPS_SLOT_0;

		ret = of_property_read_u32(np, "maxim,slpx-slot", &pval);
		fps_data->slpx_slot = (!ret) ? pval : MAX77851_FPS_SLOT_0;

		reg_data->fps_mode_en = MAX77851_FPS_MODE_ENABLE;

		max77851_regulator_set_fps(pmic, desc->id);
	}

	if (of_property_read_bool(np, "maxim,regulator-dvs-mode-enable")) {
		if (of_property_read_u32_array(np, "maxim,regulator-dvs-voltage",
					voltage, MAX77851_DVS_VOLTAGE_NUM)) {
			dev_err(pmic->dev, "dvs voltages not specified\n");
			return -EINVAL;
		}
		max77851_regulator_set_vout_voltage(pmic, voltage, desc->id);
	}

	return max77851_regulator_intial(pmic, desc->id);
}

static const struct regulator_ops max77851_regulator_ldo_ops = {
	.is_enabled = max77851_regulator_is_enabled,
	.enable = max77851_regulator_enable,
	.disable = max77851_regulator_disable,
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.set_mode = max77851_regulator_set_mode,
	.get_mode = max77851_regulator_get_mode,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.set_active_discharge = regulator_set_active_discharge_regmap,
};

static const struct regulator_ops max77851_regulator_buck_ops = {
	.is_enabled = max77851_regulator_is_enabled,
	.enable = max77851_regulator_enable,
	.disable = max77851_regulator_disable,
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.set_mode = max77851_regulator_set_mode,
	.get_mode = max77851_regulator_get_mode,
	.set_ramp_delay = max77851_regulator_set_ramp_delay,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.set_active_discharge = regulator_set_active_discharge_regmap,
};

#define REGULATOR_LDO_DESC(_id, _name, _sname, _type, _min_uV, _max_uV, _step_uV)	\
	[MAX77851_REGULATOR_ID_##_id] = {							\
		.type  = MAX77851_REGULATOR_TYPE_LDO_##_type,			\
		.cfg_addr = _id##_CFG0_REG,								\
		.vout0_addr = _id##_CFG1_REG,							\
		.vout1_addr = _id##_CFG1_REG,							\
		.desc = {												\
			.owner				= THIS_MODULE,					\
			.name		        = max77851_rails(_name),		\
			.supply_name		= _sname,						\
			.of_match			= of_match_ptr(#_name),			\
			.regulators_node	= of_match_ptr("regulators"),	\
			.of_parse_cb		= max77851_of_parse_cb,			\
			.id					= MAX77851_REGULATOR_ID_##_id,	\
			.ops				= &max77851_regulator_ldo_ops,	\
			.type				= REGULATOR_VOLTAGE,			\
			.min_uV				= _min_uV,						\
			.uV_step			= _step_uV,						\
			.enable_time		= MAX77851_LDO_ENABLE_TIME,		\
			.n_voltages			= ((_max_uV - _min_uV) / _step_uV) + 1,		\
			.vsel_reg			= _id##_CFG1_REG,				\
			.vsel_mask			= LDO_CFG1_VOUT,				\
			.enable_reg			= _id##_CFG0_REG,				\
			.enable_mask		= LDO_CFG0_EN,					\
			.enable_val			= LDO_CFG0_EN,					\
			.disable_val		= BIT_IS_ZERO,					\
			.active_discharge_reg = _id##_CFG0_REG,				\
			.active_discharge_mask = LDO_CFG0_ADE,				\
			.active_discharge_on   = LDO_CFG0_ADE,				\
			.active_discharge_off  = BIT_IS_ZERO,				\
		},		\
}

#define REGULATOR_BUCK_DESC(_id, _name, _sname, _min_uV, _max_uV, _step_uV)	\
	[MAX77851_REGULATOR_ID_##_id] = {						\
		.type  = MAX77851_REGULATOR_TYPE_BUCK,			\
		.cfg_addr = _id##_CFG0_REG,								\
		.vout0_addr = _id##_CFG1_REG,							\
		.vout1_addr = _id##_CFG2_REG,							\
		.ramp_delay_addr = _id##_CFG5_REG,						\
		.desc = {												\
			.owner				= THIS_MODULE,					\
			.name				= max77851_rails(_name),		\
			.supply_name		= _sname,						\
			.of_match			= of_match_ptr(#_name),			\
			.regulators_node	= of_match_ptr("regulators"),	\
			.of_parse_cb		= max77851_of_parse_cb,			\
			.id					= MAX77851_REGULATOR_ID_##_id,	\
			.ops				= &max77851_regulator_buck_ops,	\
			.type				= REGULATOR_VOLTAGE,			\
			.min_uV				= _min_uV,						\
			.uV_step			= _step_uV,						\
			.ramp_delay			= MAX77851_BUCK_RAMP_DELAY,		\
			.enable_time		= MAX77851_BUCK_ENABLE_TIME,	\
			.n_voltages			= ((_max_uV - _min_uV) / _step_uV) + 1,	\
			.vsel_reg			= _id##_CFG1_REG,				\
			.vsel_mask			= BUCK_CFG1_VOUT0,				\
			.enable_reg			= _id##_CFG0_REG,				\
			.enable_mask		= BUCK_CFG0_EN,					\
			.enable_val			= BUCK_CFG0_EN,					\
			.disable_val		= BIT_IS_ZERO,					\
			.active_discharge_reg = _id##_CFG6_REG,				\
			.active_discharge_mask = BUCK_CFG6_ADE,				\
			.active_discharge_on   = BUCK_CFG6_ADE,				\
			.active_discharge_off  = BIT_IS_ZERO,				\
		},		\
}

static struct max77851_regulator_data max77851_regs_data[MAX77851_REGULATOR_ID_NUM] = {
	REGULATOR_LDO_DESC(LDO0, ldo0, "in-ldo0", N, 400000, 1993750,  6250),
	REGULATOR_LDO_DESC(LDO1, ldo1, "in-ldo1", N, 400000, 1993750,  6250),
	REGULATOR_LDO_DESC(LDO2, ldo2, "in-ldo2", N, 400000, 1993750,  6250),
	REGULATOR_LDO_DESC(LDO3, ldo3, "in-ldo3", N, 400000, 3587500, 12500),
	REGULATOR_LDO_DESC(LDO4, ldo4, "in-ldo4", P, 400000, 3975000, 25000),
	REGULATOR_LDO_DESC(LDO5, ldo5, "in-ldo5", P, 400000, 3975000, 25000),
	REGULATOR_LDO_DESC(LDO6, ldo6, "in-ldo6", P, 400000, 3975000, 25000),

	REGULATOR_BUCK_DESC(BUCK0, buck0, "in-buck0", 300000, 1200000, 2500),
	REGULATOR_BUCK_DESC(BUCK1, buck1, "in-buck1", 300000, 1200000, 2500),
	REGULATOR_BUCK_DESC(BUCK2, buck2, "in-buck2", 300000, 1200000, 2500),
	REGULATOR_BUCK_DESC(BUCK3, buck3, "in-buck3", 1000000, 2400000, 2500),
	REGULATOR_BUCK_DESC(BUCK4, buck4, "in-buck4", 1000000, 2400000, 2500),
};

#define MAX77851_FPS_REGULATOR_REG(_fps_name) {		\
		.fps_cfg0_addr = _fps_name##_CFG0_REG,		\
		.fps_cfg1_addr = _fps_name##_CFG1_REG,		\
		.fps_cfg2_addr = _fps_name##_CFG2_REG,		\
}

static struct max77851_fps_data max77851_fps_data_data[MAX77851_REGULATOR_ID_NUM] = {
	MAX77851_FPS_REGULATOR_REG(FPS_LDO0),
	MAX77851_FPS_REGULATOR_REG(FPS_LDO1),
	MAX77851_FPS_REGULATOR_REG(FPS_LDO2),
	MAX77851_FPS_REGULATOR_REG(FPS_LDO3),
	MAX77851_FPS_REGULATOR_REG(FPS_LDO4),
	MAX77851_FPS_REGULATOR_REG(FPS_LDO5),
	MAX77851_FPS_REGULATOR_REG(FPS_LDO6),

	MAX77851_FPS_REGULATOR_REG(FPS_BUCK0),
	MAX77851_FPS_REGULATOR_REG(FPS_BUCK1),
	MAX77851_FPS_REGULATOR_REG(FPS_BUCK2),
	MAX77851_FPS_REGULATOR_REG(FPS_BUCK3),
	MAX77851_FPS_REGULATOR_REG(FPS_BUCK4),
};

static int max77851_regulator_probe(struct platform_device *pdev)
{
	struct max77851_chip *max77851_chip = dev_get_drvdata(pdev->dev.parent);
	struct max77851_regulator *pmic;
	struct max77851_fps_data *fps_data;
	struct max77851_regulator_data *reg_data;
	struct device *dev = &pdev->dev;
	struct regulator_config config = { };
	int ret = 0;
	int id = 0;

	pmic = devm_kzalloc(dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic)
		return -ENOMEM;

	platform_set_drvdata(pdev, pmic);

	pmic->dev = dev;
	pmic->rmap = max77851_chip->rmap;
	pmic->dev->of_node = pdev->dev.parent->of_node;

	fps_data = max77851_fps_data_data;
	reg_data = max77851_regs_data;

	pmic->reg_data = max77851_regs_data;
	pmic->fps_data = max77851_fps_data_data;

	config.regmap = pmic->rmap;
	config.dev = dev;
	config.driver_data = pmic;

	for (id = 0; id < MAX77851_REGULATOR_ID_NUM; id++) {
		struct regulator_dev *rdev;
		struct regulator_desc *rdesc;

		rdesc = &reg_data[id].desc;

		pmic->reg_data[id].power_mode = MAX77851_POWER_MODE_NORMAL;

		pmic->fps_data[id].pd_slot = -1;
		pmic->fps_data[id].pu_slot = -1;
		pmic->fps_data[id].slpx_slot = -1;
		pmic->fps_data[id].slpy_slot = -1;
		pmic->fps_data[id].pu_slpx_master_slot = -1;
		pmic->fps_data[id].pd_slpy_master_slot = -1;

		rdev = devm_regulator_register(dev, rdesc, &config);
		if (IS_ERR(rdev)) {
			ret = PTR_ERR(rdev);
			dev_err(dev, "Regulator registration %s failed: %d\n",
				rdesc->name, ret);
			return ret;
		}
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int max77851_regulator_suspend(struct device *dev)
{
	return 0;
}

static int max77851_regulator_resume(struct device *dev)
{
	return 0;
}
#endif

static const struct dev_pm_ops max77851_regulator_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(max77851_regulator_suspend,
				max77851_regulator_resume)
};

static const struct platform_device_id max77851_regulator_devtype[] = {
	{ .name = "max77851-regulator", },
	{},
};
MODULE_DEVICE_TABLE(platform, max77851_regulator_devtype);

static struct platform_driver max77851_regulator_driver = {
	.probe = max77851_regulator_probe,
	.id_table = max77851_regulator_devtype,
	.driver = {
		.name = "max77851-regulator",
		.pm = &max77851_regulator_pm_ops,
	},
};

static int __init max77851_regulator_init(void)
{
	return platform_driver_register(&max77851_regulator_driver);
}
subsys_initcall(max77851_regulator_init);

static void __exit max77851_reg_exit(void)
{
	platform_driver_unregister(&max77851_regulator_driver);
}
module_exit(max77851_reg_exit);

MODULE_DESCRIPTION("MAX77851 regulator driver");
MODULE_AUTHOR("Shubhi Garg<shgarg@nvidia.com>");
MODULE_AUTHOR("Joan Na<Joan.na@maximintegrated.com>");
MODULE_ALIAS("platform:max77851-regulator");
MODULE_LICENSE("GPL v2");
