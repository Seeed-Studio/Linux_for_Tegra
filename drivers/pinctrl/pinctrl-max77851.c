// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * MAX77851 pin control driver.
 */

#include <linux/mfd/max77851.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "core.h"
#include "pinconf.h"
#include "pinctrl-utils.h"

#define MAX77851_PIN_NUM 12

enum max77851_pin_ppdrv {
	MAX77851_PIN_OD_DRV,
	MAX77851_PIN_PP_DRV,
};

enum max77851_pin_polarity {
	MAX77851_PIN_ACTIVE_HIGH,
	MAX77851_PIN_ACTIVE_LOW,
};

enum max77851_input_supply {
	MAX77851_INPUT_VDD = 0,
	MAX77851_INPUT_VIO = 1,
	MAX77851_INPUT_BAT = 1,
};

#define MAX77851_POLARITY                  (PIN_CONFIG_END + 1)
#define MAX77851_INPUT_DEB_FILTER          (PIN_CONFIG_END + 2)
#define MAX77851_INPUT_SUPPLY              (PIN_CONFIG_END + 3)

#define MAX77851_PU_SLPX_MASTER_SLOT       (PIN_CONFIG_END + 4)
#define MAX77851_PD_SLPY_MASTER_SLOT       (PIN_CONFIG_END + 5)
#define MAX77851_PU_SLOT                   (PIN_CONFIG_END + 6)
#define MAX77851_PD_SLOT                   (PIN_CONFIG_END + 7)
#define MAX77851_SLPX_SLOT                 (PIN_CONFIG_END + 8)
#define MAX77851_SLPY_SLOT                 (PIN_CONFIG_END + 9)

#define IS_GPIO_REG_SET_HIGH(pin)   ((pin >= MAX77851_GPIO4) && (pin <= MAX77851_GPIO7))
#define IS_FPSO_REG_SET_HIGH(pin)   ((pin >= MAX77851_FPSO2) && (pin <= MAX77851_FPSO3))

#define IS_GPIO(pin)   ((pin >= MAX77851_GPIO0) && (pin <= MAX77851_GPIO7))
#define IS_FPSO(pin)   ((pin >= MAX77851_FPSO0) && (pin <= MAX77851_FPSO3))
#define IS_NRSTIO(pin) (pin == MAX77851_NRSTIO)

struct max77851_pin_function {
	const char *name;
	const char * const *groups;
	unsigned int ngroups;
	int mux_option;
};

static const struct pinconf_generic_params max77851_cfg_params[] = {
	/* IO Configuration */
	{
		.property = "maxim,polarity",
		.param = MAX77851_POLARITY,
	},
	{
		.property = "maxim,input_debounce_filter",
		.param = MAX77851_INPUT_DEB_FILTER,
	},
	{
		.property = "maxim,input_suppy",
		.param = MAX77851_INPUT_SUPPLY,
	},
	/* FPS Configuration */
	{
		.property = "maxim,pu-slpx-master-slot",
		.param = MAX77851_PU_SLPX_MASTER_SLOT,
	},
	{
		.property = "maxim,pd-slpy-master-slot",
		.param = MAX77851_PD_SLPY_MASTER_SLOT,
	},
	{
		.property = "maxim,pu-slot",
		.param = MAX77851_PU_SLOT,
	},
	{
		.property = "maxim,pd-slot",
		.param = MAX77851_PD_SLOT,
	},
	{
		.property = "maxim,slpx-slot",
		.param = MAX77851_SLPX_SLOT,
	},
	{
		.property = "maxim,slpy-slot",
		.param = MAX77851_SLPY_SLOT,
	}
};

struct max77851_pingroup {
	const char *name;
	const unsigned int pins[1];
	unsigned int npins;
	unsigned int polarity;
	enum max77851_alternate_pinmux_option alt_option;

	/* For FPSO, use the same address */
	u8 pin_cfg0_addr;
	u8 pin_cfg1_addr;
};

struct max77851_pin_info {
	enum max77851_pin_ppdrv drv_type;
	int pull_config;
};

struct max77851_pctrl_info {
	struct device *dev;
	struct pinctrl_dev *pctl;
	struct regmap *rmap;

	int pins_current_opt[MAX77851_GPIO_NR];

	const struct max77851_pin_function *functions;
	unsigned int num_functions;

	const struct max77851_pingroup *pin_groups;
	int num_pin_groups;

	const struct pinctrl_pin_desc *pins;
	unsigned int num_pins;

	struct max77851_fps_data *fps_reg;
	unsigned int num_fps_regs;

	struct max77851_pin_info pin_info[MAX77851_PIN_NUM];
	struct max77851_fps_data fps_data[MAX77851_PIN_NUM];
};

static const struct pinctrl_pin_desc max77851_pins_desc[] = {
	/* GPIO 0-7 */
	PINCTRL_PIN(MAX77851_GPIO0, "gpio0"),
	PINCTRL_PIN(MAX77851_GPIO1, "gpio1"),
	PINCTRL_PIN(MAX77851_GPIO2, "gpio2"),
	PINCTRL_PIN(MAX77851_GPIO3, "gpio3"),
	PINCTRL_PIN(MAX77851_GPIO4, "gpio4"),
	PINCTRL_PIN(MAX77851_GPIO5, "gpio5"),
	PINCTRL_PIN(MAX77851_GPIO6, "gpio6"),
	PINCTRL_PIN(MAX77851_GPIO7, "gpio7"),
	/* FPSO 0-3 */
	PINCTRL_PIN(MAX77851_FPSO0, "fpso0"),
	PINCTRL_PIN(MAX77851_FPSO1, "fpso1"),
	PINCTRL_PIN(MAX77851_FPSO2, "fpso2"),
	PINCTRL_PIN(MAX77851_FPSO3, "fpso3"),
	PINCTRL_PIN(MAX77851_NRSTIO, "nrstio"),
};

static const char * const gpio_groups[] = {
	/* GPIO 0-7 */
	"gpio0",
	"gpio1",
	"gpio2",
	"gpio3",
	"gpio4",
	"gpio5",
	"gpio6",
	"gpio7",
	/* FPSO 0-3 */
	"fpso0",
	"fpso1",
	"fpso2",
	"fpso3",
	/* nrstio */
	"nrstio"
};

#define FUNCTION_GPIO_GROUP(fname, mux)	\
	{										\
		.name = fname,						\
		.groups = gpio_groups,				\
		.ngroups = ARRAY_SIZE(gpio_groups),	\
		.mux_option = GPIO_PINMUX_##mux,	\
	}

#define FUNCTION_FPSO_GROUP(fname, mux)	\
	{										\
		.name = fname,						\
		.groups = gpio_groups,				\
		.ngroups = ARRAY_SIZE(gpio_groups),	\
		.mux_option = FPSO_PINMUX_##mux,	\
	}

#define FUNCTION_NRSTIO_GROUP(fname, mux)	\
	{										\
		.name = fname,						\
		.groups = gpio_groups,				\
		.ngroups = ARRAY_SIZE(gpio_groups),	\
		.mux_option = NRSTIO_PINMUX_##mux,	\
	}

static const struct max77851_pin_function max77851_pin_function[] = {
		/* GPIO */
		FUNCTION_GPIO_GROUP("gpio-high-z", HIGH_Z),
		FUNCTION_GPIO_GROUP("gpio-input", GPIO_INPUT),
		FUNCTION_GPIO_GROUP("gpio-output", GPIO_OUTPUT),
		FUNCTION_GPIO_GROUP("gpio-fps-digital-input", FPS_DIGITAL_INPUT),
		FUNCTION_GPIO_GROUP("gpio-fps-digital-output", FPS_DIGITAL_OUTPUT),
		FUNCTION_GPIO_GROUP("src-enable-digital-input", SRC_ENABLE_DIGITAL_INPUT),
		FUNCTION_GPIO_GROUP("src-boot-dvs-digital-input", SRC_BOOT_DVS_DIGITAL_INPUT),
		FUNCTION_GPIO_GROUP("src-clock-digital-input", SRC_CLOCK_DIGITAL_INPUT),
		FUNCTION_GPIO_GROUP("src-fpwm-digital-input", SRC_FPWM_DIGITAL_INPUT),
		FUNCTION_GPIO_GROUP("src-pok-gpio-digital-output", SRC_POK_GPIO_DIGITAL_OUTPUT),
		FUNCTION_GPIO_GROUP("clk-32k-out", CLK_32K_OUT),
		FUNCTION_GPIO_GROUP("lb-alarm-output", LB_ALARM_OUTPUT),
		FUNCTION_GPIO_GROUP("o-type-reset", O_TYPE_RESET),
		FUNCTION_GPIO_GROUP("test-digital-input", TEST_DIGITAL_INPUT),
		FUNCTION_GPIO_GROUP("test-digital-output", TEST_DIGITAL_OUTPUT),
		FUNCTION_GPIO_GROUP("test-analog-in-out", TEST_ANALOG_IN_OUT),

		/* FPSO */
		FUNCTION_FPSO_GROUP("fpso-high-z", HIGH_Z),
		FUNCTION_FPSO_GROUP("fpso-digital-output", DIGITAL_OUTPUT),
		FUNCTION_FPSO_GROUP("fpso-fps-digital-output", FPS_DIGITAL_OUTPUT),
		FUNCTION_FPSO_GROUP("fpso-buck-sense", BUCK_SENSE),

		/* NRSTIO */
		FUNCTION_NRSTIO_GROUP("nrstio-high-z", HIGH_Z),
		FUNCTION_NRSTIO_GROUP("nrstio-digital-input", DIGITAL_INPUT),
		FUNCTION_NRSTIO_GROUP("nrstio-digital-output", DIGITAL_OUTPUT),
		FUNCTION_NRSTIO_GROUP("nrstio-fps-digital-output", FPS_DIGITAL_OUTPUT),
		FUNCTION_NRSTIO_GROUP("nrstio-lb-digital-output", LB_DIGITAL_OUTPUT),
};

#define MAX77851_GPIO_PINGROUP(_pg_name, _pin_id, _option, _polarity) \
{															\
	.name = #_pg_name,										\
	.pins = {MAX77851_##_pin_id},							\
	.npins = 1,												\
	.alt_option = GPIO_PINMUX_##_option,					\
	.polarity = MAX77851_PIN_##_polarity,					\
	.pin_cfg0_addr = _pin_id##_CFG0_REG,					\
	.pin_cfg1_addr = _pin_id##_CFG1_REG,					\
}

#define MAX77851_FPSO_PINGROUP(_pg_name, _pin_id, _option, _polarity) \
{														\
	.name = #_pg_name,									\
	.pins = {MAX77851_##_pin_id},						\
	.npins = 1,											\
	.alt_option = FPSO_PINMUX_##_option,				\
	.polarity = MAX77851_PIN_##_polarity,				\
	.pin_cfg0_addr = _pin_id##_CFG_REG,					\
	.pin_cfg1_addr = _pin_id##_CFG_REG,					\
}

#define MAX77851_NRSTIO_PINGROUP(_pg_name, _pin_id, _option, _polarity) \
{															\
	.name = #_pg_name,										\
	.pins = {MAX77851_##_pin_id},							\
	.npins = 1,												\
	.alt_option = NRSTIO_PINMUX_##_option,					\
	.polarity = MAX77851_PIN_##_polarity,					\
	.pin_cfg0_addr = _pin_id##_CFG0_REG,					\
	.pin_cfg1_addr = _pin_id##_CFG1_REG,					\
}

static const struct max77851_pingroup max77851_pingroups[] = {
	MAX77851_GPIO_PINGROUP(gpio0, GPIO0, CLK_32K_OUT, ACTIVE_HIGH),
	MAX77851_GPIO_PINGROUP(gpio1, GPIO1, FPS_DIGITAL_OUTPUT, ACTIVE_LOW),
	MAX77851_GPIO_PINGROUP(gpio2, GPIO2, LB_ALARM_OUTPUT, ACTIVE_HIGH),
	MAX77851_GPIO_PINGROUP(gpio3, GPIO3, FPS_DIGITAL_INPUT, ACTIVE_HIGH),
	MAX77851_GPIO_PINGROUP(gpio4, GPIO4, SRC_BOOT_DVS_DIGITAL_INPUT, ACTIVE_HIGH),
	MAX77851_GPIO_PINGROUP(gpio5, GPIO5, HIGH_Z, ACTIVE_HIGH),
	MAX77851_GPIO_PINGROUP(gpio6, GPIO6, HIGH_Z, ACTIVE_HIGH),
	MAX77851_GPIO_PINGROUP(gpio7, GPIO7, SRC_BOOT_DVS_DIGITAL_INPUT, ACTIVE_HIGH),

	MAX77851_FPSO_PINGROUP(fpso0, FPSO0, FPS_DIGITAL_OUTPUT, ACTIVE_HIGH),
	MAX77851_FPSO_PINGROUP(fpso1, FPSO1, FPS_DIGITAL_OUTPUT, ACTIVE_HIGH),
	MAX77851_FPSO_PINGROUP(fpso2, FPSO2, BUCK_SENSE, ACTIVE_HIGH),
	MAX77851_FPSO_PINGROUP(fpso3, FPSO3, FPS_DIGITAL_OUTPUT, ACTIVE_HIGH),

	MAX77851_NRSTIO_PINGROUP(nrstio, NRSTIO, FPS_DIGITAL_OUTPUT, ACTIVE_HIGH),
};

#define MAX77851_FPS_PINCTRL_REG_GROUP(_fps_name) \
{												\
	.fps_cfg0_addr = _fps_name##_CFG0_REG,		\
	.fps_cfg1_addr = _fps_name##_CFG1_REG,		\
	.fps_cfg2_addr = _fps_name##_CFG2_REG,		\
}

static struct max77851_fps_data max77851_fps_reg_groups[] = {
	MAX77851_FPS_PINCTRL_REG_GROUP(FPS_GPIO04),
	MAX77851_FPS_PINCTRL_REG_GROUP(FPS_GPIO15),
	MAX77851_FPS_PINCTRL_REG_GROUP(FPS_GPIO26),
	MAX77851_FPS_PINCTRL_REG_GROUP(FPS_GPIO37),
	MAX77851_FPS_PINCTRL_REG_GROUP(FPS_GPIO04),
	MAX77851_FPS_PINCTRL_REG_GROUP(FPS_GPIO15),
	MAX77851_FPS_PINCTRL_REG_GROUP(FPS_GPIO26),
	MAX77851_FPS_PINCTRL_REG_GROUP(FPS_GPIO37),

	MAX77851_FPS_PINCTRL_REG_GROUP(FPS_FPSO02),
	MAX77851_FPS_PINCTRL_REG_GROUP(FPS_FPSO13),
	MAX77851_FPS_PINCTRL_REG_GROUP(FPS_FPSO02),
	MAX77851_FPS_PINCTRL_REG_GROUP(FPS_FPSO13),

	MAX77851_FPS_PINCTRL_REG_GROUP(FPS_NRSTIO),
};


/*
 * 0 : GPIO 0 + GPIO 1 + GPIO 3 + GPIO 3
 * 1 : GPIO 4 + GPIO 5 + GPIO 6 + GPIO 7
 */
static int max77851_pinctrl_register_rw_set(struct regmap *rmap, unsigned int pin)
{
	int ret;
	unsigned int val = 0;
	unsigned int mask = (FPS_CFG_GPIOX_RW | FPS_CFG_FPSOX_RW);

	if (IS_GPIO(pin)) {
		if (IS_GPIO_REG_SET_HIGH(pin))
			val |= FPS_CFG_GPIOX_RW;
	}

	if (IS_FPSO(pin)) {
		if (IS_FPSO_REG_SET_HIGH(pin))
			val |= FPS_CFG_FPSOX_RW;
	}

	ret = regmap_update_bits(rmap, FPS_CFG_REG, mask, val);

	return ret;
}

static int max77851_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct max77851_pctrl_info *pcntl = pinctrl_dev_get_drvdata(pctldev);

	return pcntl->num_pin_groups;
}

static const char *max77851_pinctrl_get_group_name(
		struct pinctrl_dev *pctldev, unsigned int group)
{
	struct max77851_pctrl_info *pcntl = pinctrl_dev_get_drvdata(pctldev);

	return pcntl->pin_groups[group].name;
}

static int max77851_pinctrl_get_group_pins(
		struct pinctrl_dev *pctldev, unsigned int group,
		const unsigned int **pins, unsigned int *num_pins)
{
	struct max77851_pctrl_info *pcntl = pinctrl_dev_get_drvdata(pctldev);

	*pins = pcntl->pin_groups[group].pins;
	*num_pins = pcntl->pin_groups[group].npins;

	return 0;
}

static const struct pinctrl_ops max77851_pinctrl_ops = {
	.get_groups_count = max77851_pinctrl_get_groups_count,
	.get_group_name = max77851_pinctrl_get_group_name,
	.get_group_pins = max77851_pinctrl_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinctrl_utils_free_map,
};

static int max77851_pinctrl_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct max77851_pctrl_info *pcntl = pinctrl_dev_get_drvdata(pctldev);

	return pcntl->num_functions;
}

static const char *max77851_pinctrl_get_func_name(struct pinctrl_dev *pctldev,
						  unsigned int function)
{
	struct max77851_pctrl_info *pcntl = pinctrl_dev_get_drvdata(pctldev);

	return pcntl->functions[function].name;
}

static int max77851_pinctrl_get_func_groups(struct pinctrl_dev *pctldev,
					    unsigned int function,
					    const char * const **groups,
					    unsigned int * const num_groups)
{
	struct max77851_pctrl_info *pcntl = pinctrl_dev_get_drvdata(pctldev);

	*groups = pcntl->functions[function].groups;
	*num_groups = pcntl->functions[function].ngroups;

	return 0;
}

static int max77851_pinctrl_enable(struct pinctrl_dev *pctldev,
				   unsigned int function, unsigned int group)
{
	struct max77851_pctrl_info *pcntl = pinctrl_dev_get_drvdata(pctldev);
	u8 reg_addr;
	u8 val;
	int mask, shift;
	int ret;
	unsigned int pin;

	pin = pcntl->pin_groups[group].pins[0];
	reg_addr = pcntl->pin_groups[group].pin_cfg1_addr;

	if (IS_GPIO(pin)) {
		val = function;
		shift = FFS(GPIO_CFG1_MODE);
		mask = GPIO_CFG1_MODE;
	} else if (IS_FPSO(pin)) {
		val = function - FPSO_PINMUX_OFFSET;
		shift = FFS(FPSO_MODE_MASK);
		mask = FPSO_MODE_MASK;
	} else if (IS_NRSTIO(pin)) {
		val = function - NRSTIO_PINMUX_OFFSET;
		shift = FFS(NRSTIO_CFG1_MODE);
		mask = NRSTIO_CFG1_MODE;
	}

	if (!(IS_GPIO(pin) ||  IS_FPSO(pin) || IS_NRSTIO(pin))) {
		dev_err(pcntl->dev, "GPIO %u doesn't have function %u\n", group, function);
		return -EINVAL;
	}

	ret = regmap_update_bits(pcntl->rmap, reg_addr, mask, val << shift);
	if (ret < 0)
		dev_err(pcntl->dev, "Pin Control failed: %d\n", ret);

	return ret;
}

static const struct pinmux_ops max77851_pinmux_ops = {
	.get_functions_count	= max77851_pinctrl_get_funcs_count,
	.get_function_name	= max77851_pinctrl_get_func_name,
	.get_function_groups	= max77851_pinctrl_get_func_groups,
	.set_mux		= max77851_pinctrl_enable,
};

static int max77851_pinconf_get(struct pinctrl_dev *pctldev,
				unsigned int pin, unsigned long *config)
{
	struct max77851_pctrl_info *pcntl = pinctrl_dev_get_drvdata(pctldev);
	struct device *dev = pcntl->dev;
	enum pin_config_param param = pinconf_to_config_param(*config);
	unsigned int val;
	int arg = 0;
	int ret;

	switch (param) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		if (pcntl->pin_info[pin].drv_type == MAX77851_PIN_OD_DRV)
			arg = 1;
		break;

	case PIN_CONFIG_DRIVE_PUSH_PULL:
		if (pcntl->pin_info[pin].drv_type == MAX77851_PIN_PP_DRV)
			arg = 1;
		break;

	case PIN_CONFIG_BIAS_PULL_UP:
		ret = regmap_read(pcntl->rmap, pcntl->pin_groups[pin].pin_cfg0_addr, &val);
		if (ret < 0) {
			dev_err(dev, "Reg PUE_GPIO read failed: %d\n", ret);
			return ret;
		}
		if (val & GPIO_CFG0_PU)
			arg = 1;
		break;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		ret = regmap_read(pcntl->rmap, pcntl->pin_groups[pin].pin_cfg0_addr, &val);
		if (ret < 0) {
			dev_err(dev, "Reg PDE_GPIO read failed: %d\n", ret);
			return ret;
		}
		if (val & GPIO_CFG0_PD)
			arg = 1;
		break;

	default:
		dev_err(dev, "Properties not supported\n");
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, (u16)arg);

	return 0;
}

static int max77851_pinconf_set(struct pinctrl_dev *pctldev,
				unsigned int pin, unsigned long *configs,
				unsigned int num_configs)
{
	struct max77851_pctrl_info *pcntl = pinctrl_dev_get_drvdata(pctldev);
	struct device *dev = pcntl->dev;
	struct max77851_fps_data *fps_data = &pcntl->fps_data[pin];

	int param;
	u16 param_val;
	u8 reg_addr;
	u8 reg_addr0;
	u8 reg_addr1;
	unsigned int val;
	unsigned int pu_val;
	unsigned int pd_val;
	int ret;
	int i;

	int mask, shift;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		param_val = pinconf_to_config_argument(configs[i]);

		reg_addr0 = pcntl->pin_groups[pin].pin_cfg0_addr;
		reg_addr1 = pcntl->pin_groups[pin].pin_cfg1_addr;

		/* The register set should be set first before setting the FPS */
		switch (param) {
		case MAX77851_PU_SLPX_MASTER_SLOT:
		case MAX77851_PD_SLPY_MASTER_SLOT:
		case MAX77851_PU_SLOT:
		case MAX77851_PD_SLOT:
		case MAX77851_SLPX_SLOT:
		case MAX77851_SLPY_SLOT:
			ret = max77851_pinctrl_register_rw_set(pcntl->rmap, pin);
			if (ret < 0) {
				dev_err(dev, "Pin Control Register Set failed: %d\n", ret);
				return ret;
			}
			break;
		default:
			break;
		}

		switch (param) {
		case PIN_CONFIG_DRIVE_OPEN_DRAIN:
			mask = GPIO_CFG1_DRV;
			val = param_val ? 0 : 1;
			shift = FFS(GPIO_CFG1_DRV);
			reg_addr = pcntl->pin_groups[pin].pin_cfg1_addr;

			ret = regmap_update_bits(pcntl->rmap, reg_addr, mask, val << shift);
			if (ret < 0) {
				dev_err(dev, "Reg 0x%02x update failed %d\n", reg_addr1, ret);
				return ret;
			}
			pcntl->pin_info[pin].drv_type = val ? MAX77851_PIN_PP_DRV : MAX77851_PIN_OD_DRV;
			break;

		case PIN_CONFIG_DRIVE_PUSH_PULL:
			mask = GPIO_CFG1_DRV;
			val = param_val ? 1 : 0;
			shift = FFS(GPIO_CFG1_DRV);
			reg_addr = pcntl->pin_groups[pin].pin_cfg1_addr;

			ret = regmap_update_bits(pcntl->rmap, reg_addr, mask, val << shift);
			if (ret < 0) {
				dev_err(dev, "Reg 0x%02x update failed %d\n", reg_addr1, ret);
				return ret;
			}
			pcntl->pin_info[pin].drv_type = val ? MAX77851_PIN_PP_DRV : MAX77851_PIN_OD_DRV;
			break;

		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_BIAS_PULL_DOWN:
			pu_val = (param == PIN_CONFIG_BIAS_PULL_UP) ? 1 : 0;
			pd_val = (param == PIN_CONFIG_BIAS_PULL_DOWN) ? 1 : 0;
			mask = GPIO_CFG0_PU | GPIO_CFG0_PD;
			val = ((pu_val << FFS(GPIO_CFG0_PU)) | (pd_val << FFS(GPIO_CFG0_PD)));
			shift = 0;
			reg_addr = pcntl->pin_groups[pin].pin_cfg0_addr;

			ret = regmap_update_bits(pcntl->rmap, reg_addr, mask, val << shift);
			if (ret < 0) {
				dev_err(dev, "PULL Up/Down GPIO update failed: %d\n", ret);
				return ret;
			}
			break;

		case MAX77851_POLARITY:
			if (IS_FPSO(pin)) {
				mask = FPSO_CFG_POL;
				shift = FFS(FPSO_CFG_POL);
			} else {
				mask = GPIO_CFG0_POL;
				shift = FFS(GPIO_CFG0_POL);
			}

			val = param_val;
			reg_addr = pcntl->pin_groups[pin].pin_cfg0_addr;

			ret = regmap_update_bits(pcntl->rmap, reg_addr, mask, val << shift);
			if (ret < 0) {
				dev_err(dev, "PDE_GPIO update failed: %d\n", ret);
				return ret;
			}
			break;

		case MAX77851_INPUT_DEB_FILTER:
			if (IS_GPIO(pin) || IS_NRSTIO(pin)) {
				mask = GPIO_CFG0_IFILTER;
				shift = FFS(GPIO_CFG0_IFILTER);
				val = param_val;
				reg_addr = pcntl->pin_groups[pin].pin_cfg0_addr;

				ret = regmap_update_bits(pcntl->rmap, reg_addr, mask, val << shift);
				if (ret < 0) {
					dev_err(dev, "Input Deb Filter update failed: %d\n", ret);
					return ret;
				}
			}
			break;

		case MAX77851_INPUT_SUPPLY:
			if (IS_GPIO(pin) || IS_NRSTIO(pin)) {
				mask = GPIO_CFG0_SUP;
				shift = FFS(GPIO_CFG0_SUP);
				val = param_val;
				reg_addr = pcntl->pin_groups[pin].pin_cfg0_addr;

				ret = regmap_update_bits(pcntl->rmap, reg_addr, mask, val << shift);
				if (ret < 0) {
					dev_err(dev, "Input Supply GPIO update failed: %d\n", ret);
					return ret;
				}
			}
			break;

		case MAX77851_PU_SLPX_MASTER_SLOT:
			mask = MAX77851_FPS_PU_SLPX_SLOT_MASK;
			shift = FFS(MAX77851_FPS_PU_SLPX_SLOT_MASK);

			val = param_val;
			reg_addr = fps_data->fps_cfg0_addr;

			ret = regmap_update_bits(pcntl->rmap, reg_addr, mask, val << shift);
			if (ret < 0) {
				dev_err(dev, "SLPX MASTER SLOT update failed: %d\n", ret);
				return ret;
			}
			break;

		case MAX77851_PD_SLPY_MASTER_SLOT:
			mask = MAX77851_FPS_PD_SLPY_SLOT_MASK;
			shift = FFS(MAX77851_FPS_PD_SLPY_SLOT_MASK);

			val = param_val;
			reg_addr = fps_data->fps_cfg0_addr;

			ret = regmap_update_bits(pcntl->rmap, reg_addr, mask, val << shift);
			if (ret < 0) {
				dev_err(dev, "SLPY MASTER SLOT update failed: %d\n", ret);
				return ret;
			}
			break;

		case MAX77851_PU_SLOT:
			mask = MAX77851_FPS_PU_SLOT_MASK;
			shift = FFS(MAX77851_FPS_PU_SLOT_MASK);

			val = param_val;
			reg_addr = fps_data->fps_cfg1_addr;

			ret = regmap_update_bits(pcntl->rmap, reg_addr, mask, val << shift);
			if (ret < 0) {
				dev_err(dev, "PU SLOT update failed: %d\n", ret);
				return ret;
			}
			break;

		case MAX77851_PD_SLOT:
			mask = MAX77851_FPS_PD_SLOT_MASK;
			shift = FFS(MAX77851_FPS_PD_SLOT_MASK);

			val = param_val;
			reg_addr = fps_data->fps_cfg1_addr;

			ret = regmap_update_bits(pcntl->rmap, reg_addr, mask, val << shift);
			if (ret < 0) {
				dev_err(dev, "PD SLOT update failed: %d\n", ret);
				return ret;
			}
			break;

		case MAX77851_SLPX_SLOT:
			mask = MAX77851_FPS_SLPX_SLOT_MASK;
			shift = FFS(MAX77851_FPS_SLPX_SLOT_MASK);

			val = param_val;
			reg_addr = fps_data->fps_cfg2_addr;

			ret = regmap_update_bits(pcntl->rmap, reg_addr, mask, val << shift);
			if (ret < 0) {
				dev_err(dev, "SLPX SLOT update failed: %d\n", ret);
				return ret;
			}
			break;

		case MAX77851_SLPY_SLOT:
			mask = MAX77851_FPS_SLPY_SLOT_MASK;
			shift = FFS(MAX77851_FPS_SLPY_SLOT_MASK);

			val = param_val;
			reg_addr = fps_data->fps_cfg2_addr;

			ret = regmap_update_bits(pcntl->rmap, reg_addr, mask, val << shift);
			if (ret < 0) {
				dev_err(dev, "SLPY SLOT update failed: %d\n", ret);
				return ret;
			}
			break;
		default:
			dev_err(dev, "Properties not supported\n");
			return -ENOTSUPP;
		}
	}

	return 0;
}

static const struct pinconf_ops max77851_pinconf_ops = {
	.pin_config_get = max77851_pinconf_get,
	.pin_config_set = max77851_pinconf_set,
};

static struct pinctrl_desc max77851_pinctrl_desc = {
	.pctlops = &max77851_pinctrl_ops,
	.pmxops = &max77851_pinmux_ops,
	.confops = &max77851_pinconf_ops,
};

static int max77851_pinctrl_probe(struct platform_device *pdev)
{
	struct max77851_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct max77851_pctrl_info *pcntl;
	int i;

	pcntl = devm_kzalloc(&pdev->dev, sizeof(*pcntl), GFP_KERNEL);
	if (!pcntl)
		return -ENOMEM;

	pcntl->dev = &pdev->dev;
	pcntl->dev->of_node = pdev->dev.parent->of_node;
	pcntl->rmap = chip->rmap;

	pcntl->pins = max77851_pins_desc;
	pcntl->num_pins = ARRAY_SIZE(max77851_pins_desc);
	pcntl->functions = max77851_pin_function;
	pcntl->num_functions = ARRAY_SIZE(max77851_pin_function);
	pcntl->pin_groups = max77851_pingroups;
	pcntl->num_pin_groups = ARRAY_SIZE(max77851_pingroups);
	pcntl->fps_reg = max77851_fps_reg_groups;
	pcntl->num_fps_regs = ARRAY_SIZE(max77851_fps_reg_groups);
	platform_set_drvdata(pdev, pcntl);

	max77851_pinctrl_desc.name = dev_name(&pdev->dev);
	max77851_pinctrl_desc.pins = max77851_pins_desc;
	max77851_pinctrl_desc.npins = ARRAY_SIZE(max77851_pins_desc);
	max77851_pinctrl_desc.num_custom_params = ARRAY_SIZE(max77851_cfg_params);
	max77851_pinctrl_desc.custom_params = max77851_cfg_params;

	for (i = 0; i < MAX77851_PIN_NUM; ++i) {
		pcntl->fps_data[i].pu_slpx_master_slot = -1;
		pcntl->fps_data[i].pd_slpy_master_slot = -1;
		pcntl->fps_data[i].pd_slot = -1;
		pcntl->fps_data[i].pd_slot = -1;
		pcntl->fps_data[i].slpy_slot = -1;
		pcntl->fps_data[i].slpx_slot = -1;
	}

	devm_pinctrl_register_and_init(&pdev->dev, &max77851_pinctrl_desc, pcntl, &pcntl->pctl);
	if (IS_ERR(pcntl->pctl)) {
		dev_err(&pdev->dev, "Couldn't register pinctrl driver\n");
		return PTR_ERR(pcntl->pctl);
	}

	pinctrl_enable(pcntl->pctl);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int max77851_pinctrl_suspend(struct device *dev)
{
	return 0;
};

static int max77851_pinctrl_resume(struct device *dev)
{
	return 0;
}
#endif

static const struct dev_pm_ops max77851_pinctrl_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(
		max77851_pinctrl_suspend, max77851_pinctrl_resume)
};

static const struct platform_device_id max77851_pinctrl_devtype[] = {
	{ .name = "max77851-pinctrl", },
	{},
};
MODULE_DEVICE_TABLE(platform, max77851_pinctrl_devtype);

static struct platform_driver max77851_pinctrl_driver = {
	.driver = {
		.name = "max77851-pinctrl",
		.pm = &max77851_pinctrl_pm_ops,
	},
	.probe = max77851_pinctrl_probe,
	.id_table = max77851_pinctrl_devtype,
};

module_platform_driver(max77851_pinctrl_driver);

MODULE_DESCRIPTION("MAX77851 pin control driver");
MODULE_AUTHOR("Shubhi Garg<shgarg@nvidia.com>");
MODULE_AUTHOR("Joan Na<Joan.na@maximintegrated.com>");
MODULE_ALIAS("platform:max77851-pinctrl");
MODULE_LICENSE("GPL v2");
