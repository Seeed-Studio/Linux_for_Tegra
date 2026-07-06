// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2016-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/pwm.h>
#include <linux/atomic.h>

#include "cdi-pwm-priv.h"

/*
 * Below values are configured during suspend.
 * Invalid values are chosen so that PWM
 * configuration in resume works fine.
 * Period is chosen as least non-zero value
 * and duty-ratio zero.
 */
#define PWM_SUSPEND_PERIOD		1
#define PWM_SUSPEND_DUTY_RATIO		0

static const struct of_device_id cdi_pwm_of_match[] = {
	{ .compatible = "nvidia,cdi-pwm", .data = NULL },
	{},
};

static inline struct cdi_pwm_info *to_cdi_pwm_info(struct pwm_chip *chip)
{
#if defined(NV_PWM_CHIP_STRUCT_HAS_STRUCT_DEVICE)
	return pwmchip_get_drvdata(chip);
#else
	return container_of(chip, struct cdi_pwm_info, chip);
#endif
}

#if defined(NV_PWM_OPS_STRUCT_HAS_CONFIG) /* Linux 6.0 */
static int cdi_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct cdi_pwm_info *info = to_cdi_pwm_info(chip);
	int err = 0;

	if (!chip || !pwm)
		return -EINVAL;

	if (info->force_on)
		return err;

	mutex_lock(&info->mutex);

	if (atomic_inc_return(&info->in_use) == 1)
		err = pwm_enable(info->pwm);

	mutex_unlock(&info->mutex);

	return err;
}

static void cdi_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct cdi_pwm_info *info = to_cdi_pwm_info(chip);
	int atomic_val;

	mutex_lock(&info->mutex);

	atomic_val = atomic_read(&info->in_use);
	if (atomic_val > 0) {
		if (atomic_dec_and_test(&info->in_use))
			pwm_disable(info->pwm);
	}

	mutex_unlock(&info->mutex);
}

static int cdi_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			int duty_ns, int period_ns)
{
	struct cdi_pwm_info *info = to_cdi_pwm_info(chip);
	int err = 0;

	if (info->force_on)
		return err;

	mutex_lock(&info->mutex);

	err = pwm_config(info->pwm, duty_ns, period_ns);

	mutex_unlock(&info->mutex);

	return err;
}
#endif

static struct pwm_device *of_cdi_pwm_xlate(struct pwm_chip *pc,
			const struct of_phandle_args *args)
{
	struct pwm_device *pwm;
	struct cdi_pwm_info *info = to_cdi_pwm_info(pc);
#if defined(NV_PWM_CHIP_STRUCT_HAS_STRUCT_DEVICE)
	struct device *dev = &pc->dev;
#else
	struct device *dev = pc->dev;
#endif
	int err = 0;

	pwm = of_pwm_xlate_with_flags(pc, args);
	if (IS_ERR(pwm))
		return NULL;

	if (!pwm->args.period) {
		dev_err(dev, "Period should be larger than 0\n");
		return NULL;
	}

	if (info->force_on) {
		err = pwm_config(info->pwm, args->args[1]/4, args->args[1]);
		if (err) {
			dev_err(dev, "can't config PWM: %d\n", err);
			return NULL;
		}

		err = pwm_enable(info->pwm);
		if (err) {
			dev_err(dev, "can't enable PWM: %d\n", err);
			return NULL;
		}
	} else {
		err = pwm_config(pwm, args->args[1]/4, args->args[1]);
		if (err) {
			dev_err(dev, "can't config PWM: %d\n", err);
			return NULL;
		}
	}

	return pwm;
}

static const struct pwm_ops cdi_pwm_ops = {
#if defined(NV_PWM_OPS_STRUCT_HAS_CONFIG) /* Linux 6.0 */
	.config = cdi_pwm_config,
	.enable = cdi_pwm_enable,
	.disable = cdi_pwm_disable,
#endif
#if defined(NV_PWM_OPS_STRUCT_HAS_OWNER) /* Linux 6.7 */
	.owner = THIS_MODULE,
#endif
};

static int cdi_pwm_probe(struct platform_device *pdev)
{
	struct cdi_pwm_info *info = NULL;
	struct pwm_chip *chip;
	int err = 0, npwm;
	bool force_on = false;

	dev_info(&pdev->dev, "%sing...\n", __func__);

	err = of_property_read_u32(pdev->dev.of_node, "npwm", &npwm);
	if (err < 0) {
		dev_err(&pdev->dev, "npwm is not defined: %d\n", err);
		return err;
	}

#if defined(NV_PWM_CHIP_STRUCT_HAS_STRUCT_DEVICE)
	chip = devm_pwmchip_alloc(&pdev->dev, npwm, sizeof(*info));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	info = to_cdi_pwm_info(chip);
#else
	info = devm_kzalloc(
		&pdev->dev, sizeof(struct cdi_pwm_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	chip = &info->chip;
	chip->dev = &pdev->dev;
	chip->npwm = npwm;
#endif

	atomic_set(&info->in_use, 0);
	mutex_init(&info->mutex);

	force_on = of_property_read_bool(pdev->dev.of_node, "force_on");

	chip->ops = &cdi_pwm_ops;
#if defined(NV_PWM_CHIP_STRUCT_HAS_BASE_ARG)
	chip->base = -1;
#endif
	chip->of_xlate = of_cdi_pwm_xlate;
	info->force_on = force_on;

	err = pwmchip_add(chip);
	if (err < 0) {
		dev_err(&pdev->dev, "pwmchip_add() failed: %d\n", err);
		return err;
	}

	platform_set_drvdata(pdev, chip);

	info->pwm = devm_pwm_get(&pdev->dev, NULL);
	if (!IS_ERR(info->pwm)) {
		pwm_disable(info->pwm);
		dev_info(&pdev->dev, "%s success to get PWM\n", __func__);
	} else {
		pwmchip_remove(chip);
		err = PTR_ERR(info->pwm);
		if (err != -EPROBE_DEFER)
			dev_err(&pdev->dev,
				"%s: fail to get PWM\n", __func__);
	}

	return err;
}

static int cdi_pwm_remove(struct platform_device *pdev)
{
	struct pwm_chip *chip = platform_get_drvdata(pdev);

	pwmchip_remove(chip);

	return 0;
}

static int cdi_pwm_suspend(struct device *dev)
{
	struct pwm_chip *chip = dev_get_drvdata(dev);
	struct cdi_pwm_info *info = to_cdi_pwm_info(chip);

	if (info == NULL) {
		dev_err(dev, "%s: fail to get info\n", __func__);
	} else {
		if (!IS_ERR(info->pwm)) {
			pwm_disable(info->pwm);
			pwm_config(info->pwm, PWM_SUSPEND_DUTY_RATIO,
					PWM_SUSPEND_PERIOD);
		}
	}
	return 0;
}

static int cdi_pwm_resume(struct device *dev)
{
	/* Do nothing */
	return 0;
}

static const struct dev_pm_ops cdi_pwm_pm_ops = {
	.suspend = cdi_pwm_suspend,
	.resume = cdi_pwm_resume,
	.runtime_suspend = cdi_pwm_suspend,
	.runtime_resume = cdi_pwm_resume,
};

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void cdi_pwm_remove_wrapper(struct platform_device *pdev)
{
	cdi_pwm_remove(pdev);
}
#else
static int cdi_pwm_remove_wrapper(struct platform_device *pdev)
{
	return cdi_pwm_remove(pdev);
}
#endif

static struct platform_driver cdi_pwm_driver = {
	.driver = {
		.name = "cdi-pwm",
		.owner = THIS_MODULE,
		.of_match_table = cdi_pwm_of_match,
		.pm = &cdi_pwm_pm_ops,
	},
	.probe = cdi_pwm_probe,
	.remove = cdi_pwm_remove_wrapper,
};

module_platform_driver(cdi_pwm_driver);

MODULE_AUTHOR("Junghyun Kim <juskim@nvidia.com>");
MODULE_DESCRIPTION("CDI PWM driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, cdi_pwm_of_match);
MODULE_SOFTDEP("pre: cdi_dev");
