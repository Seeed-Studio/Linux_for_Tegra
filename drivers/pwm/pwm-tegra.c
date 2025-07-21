// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * drivers/pwm/pwm-tegra.c
 *
 * Tegra pulse-width-modulation controller driver
 *
 * Copyright (c) 2010-2020, NVIDIA Corporation.
 * Based on arch/arm/plat-mxc/pwm.c by Sascha Hauer <s.hauer@pengutronix.de>
 *
 * Overview of Tegra Pulse Width Modulator Register
 * CSR_0 of Tegra20, Tegra186, and Tegra194:
 * +-------+-------+-----------------------------------------------------------+
 * | Bit   | Field | Description                                               |
 * +-------+-------+-----------------------------------------------------------+
 * | 31    | ENB   | Enable Pulse width modulator.                             |
 * |       |       | 0 = DISABLE, 1 = ENABLE.                                  |
 * +-------+-------+-----------------------------------------------------------+
 * | 30:16 | PWM_0 | Pulse width that needs to be programmed.                  |
 * |       |       | 0 = Always low.                                           |
 * |       |       | 1 = 1 / (1 + PWM_DEPTH) pulse high.                       |
 * |       |       | 2 = 2 / (1 + PWM_DEPTH) pulse high.                       |
 * |       |       | N = N / (1 + PWM_DEPTH) pulse high.                       |
 * |       |       | Only 8 bits are usable [23:16].                           |
 * |       |       | Bit[24] can be programmed to 1 to achieve 100% duty       |
 * |       |       | cycle. In this case the other bits [23:16] are set to     |
 * |       |       | don't care.                                               |
 * +-------+-------+-----------------------------------------------------------+
 * | 12:0  | PFM_0 | Frequency divider that needs to be programmed, also known |
 * |       |       | as SCALE. Division by (1 + PFM_0).                        |
 * +-------+-------+-----------------------------------------------------------+
 *
 * CSR_0 of Tegra264:
 * +-------+-------+-----------------------------------------------------------+
 * | Bit   | Field | Description                                               |
 * +-------+-------+-----------------------------------------------------------+
 * | 31:16 | PWM_0 | Pulse width that needs to be programmed.                  |
 * |       |       | 0 = Always low.                                           |
 * |       |       | 1 = 1 / (1 + PWM_DEPTH) pulse high.                       |
 * |       |       | 2 = 2 / (1 + PWM_DEPTH) pulse high.                       |
 * |       |       | N = N / (1 + PWM_DEPTH) pulse high.                       |
 * +-------+-------+-----------------------------------------------------------+
 * | 15:0  | PFM_0 | Frequency divider that needs to be programmed, also known |
 * |       |       | as SCALE. Division by (1 + PFM_0).                        |
 * +-------+-------+-----------------------------------------------------------+
 *
 * CSR_1 of Tegra264:
 * +-------+-------+-----------------------------------------------------------+
 * | Bit   | Field | Description                                               |
 * +-------+-------+-----------------------------------------------------------+
 * | 31    | ENB   | Enable Pulse width modulator.                             |
 * |       |       | 0 = DISABLE, 1 = ENABLE.                                  |
 * +-------+-------+-----------------------------------------------------------+
 * | 30:15 | DEPTH | Depth for pulse width modulator. This controls the pulse  |
 * |       |       | time generated. Division by (1 + PWM_DEPTH).              |
 * +-------+-------+-----------------------------------------------------------+
 *
 * The PWM clock frequency is divided by (1 + PWM_DEPTH) before subdividing it
 * based on the programmable frequency division value to generate the required
 * frequency for PWM output. The maximum output frequency that can be
 * achieved is (max rate of source clock) / (1 + PWM_DEPTH).
 * e.g. if source clock rate is 408 MHz and PWM_DEPTH is 255, maximum output
 * frequency can be: 408 MHz / (1 + 255) ~= 1.6 MHz.
 * This 1.6 MHz frequency can further be divided using SCALE value in PWM.
 *
 * Limitations:
 * -	When PWM is disabled, the output is driven to inactive.
 * -	It does not allow the current PWM period to complete and
 *	stops abruptly.
 *
 * -	If the register is reconfigured while PWM is running,
 *	it does not complete the currently running period.
 *
 * -	If the user input duty is beyond acceptible limits,
 *	-EINVAL is returned.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_opp.h>
#include <linux/pwm.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/reset.h>

#include <soc/tegra/common.h>

#define DEFAULT_PWM_DEPTH 255
#define PWM_ENABLE	(1 << 31)
#define REG_WIDTH 32

struct tegra_pwm_soc {
	unsigned int channel_offset;
	unsigned int depth_offset;
	unsigned int depth_shift;
	unsigned int depth_width;
	unsigned int duty_shift;
	unsigned int duty_width;
	unsigned int enb_offset;
	unsigned int num_channels;
	unsigned int scale_shift;
	unsigned int scale_width;
	bool has_depth_csr;
};

struct tegra_pwm_chip {
	struct pwm_chip chip;
	struct device *dev;

	struct clk *clk;
	struct reset_control*rst;

	u32 pwm_depth;
	unsigned long clk_rate;
	unsigned long min_period_ns;

	void __iomem *regs;

	const struct tegra_pwm_soc *soc;
};

static inline struct tegra_pwm_chip *to_tegra_pwm_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct tegra_pwm_chip, chip);
}

static inline u32 pwm_readl(struct tegra_pwm_chip *pc, unsigned int offset)
{
	return readl(pc->regs + offset);
}

static inline void pwm_writel(struct tegra_pwm_chip *pc, unsigned int offset, u32 value)
{
	writel(value, pc->regs + offset);
}

static inline void pwm_writel_mask32(struct tegra_pwm_chip *pc,
				     unsigned int offset, u32 mask, u32 value)
{
	u32 ret = pwm_readl(pc, offset);
	ret = (ret & ~mask) | (value & mask);
	pwm_writel(pc, offset, ret);
}

static int tegra_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			    u64 duty_ns, u64 period_ns)
{
	struct tegra_pwm_chip *pc = to_tegra_pwm_chip(chip);
	unsigned long channel_o = pc->soc->channel_offset;
	unsigned long duty_w = pc->soc->duty_width;
	unsigned long duty_s = pc->soc->duty_shift;
	unsigned long scale_w = pc->soc->scale_width;
	unsigned long scale_s = pc->soc->scale_shift;
	unsigned long required_clk_rate;
	u32 pwm_f, pfm_f;
	u64 val;
	int err;

	/*
	 *  min period = max clock limit / (1 + pc->pwm_depth)
	 */
	if (period_ns < pc->min_period_ns)
		return -EINVAL;

	/*
	 * Convert from duty_ns / period_ns to a fixed number of duty ticks
	 * per (1 + pc->pwm_depth) cycles and make sure to round to the
	 * nearest integer during division.
	 */
	val = mul_u64_u64_div_u64(duty_ns, 1 + pc->pwm_depth, period_ns);
	if (val > U32_MAX)
		return -EINVAL;

	pwm_f = (u32)val;

	/* Avoid overflow on 100% duty cycle */
	if (pwm_f == 1 + pc->pwm_depth)
		if (duty_s + duty_w == REG_WIDTH)
			--pwm_f;

	/*
	 * required_clk_rate is a reference rate for source clock and
	 * it is derived based on user requested period.
	 */
	val = mul_u64_u64_div_u64(NSEC_PER_SEC, 1 + pc->pwm_depth, period_ns);
	if (val > U32_MAX)
		return -EINVAL;

	required_clk_rate = (u32)val;
	pc->clk_rate = clk_get_rate(pc->clk);
	if (pc->clk_rate < required_clk_rate)
		return -EINVAL;

	/*
	 * Since the actual PWM divider is the register's frequency divider
	 * field plus 1, we need to decrement to get the correct value to
	 * write to the register.
	 */
	pfm_f = DIV_ROUND_CLOSEST_ULL(pc->clk_rate, required_clk_rate) - 1;

	/*
	 * Make sure that the pfm_f will fit in the register's frequency
	 * divider field.
	 */
	if (pfm_f >> scale_w)
		return -EINVAL;

	/*
	 * If the PWM channel is disabled, make sure to turn on the clock
	 * before writing the register. Otherwise, keep it enabled.
	 */
	if (!pwm_is_enabled(pwm)) {
		err = pm_runtime_resume_and_get(pc->dev);
		if (err)
			return err;
	}

	pwm_writel_mask32(pc, pwm->hwpwm * channel_o,
			  GENMASK(duty_s + duty_w - 1, duty_s),
			  pwm_f << duty_s);
	pwm_writel_mask32(pc, pwm->hwpwm * channel_o,
			  GENMASK(scale_s + scale_w - 1, scale_s),
			  pfm_f << scale_s);

	/*
	 * If the PWM was not enabled, turn the clock off again to save power.
	 */
	if (!pwm_is_enabled(pwm))
		pm_runtime_put(pc->dev);

	return 0;
}

static int tegra_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct tegra_pwm_chip *pc = to_tegra_pwm_chip(chip);
	unsigned int enb_offset;
	int rc = 0;
	u32 val;

	rc = pm_runtime_resume_and_get(pc->dev);
	if (rc)
		return rc;

	enb_offset = pwm->hwpwm * pc->soc->channel_offset + pc->soc->enb_offset;
	val = pwm_readl(pc, enb_offset);
	val |= PWM_ENABLE;
	pwm_writel(pc, enb_offset, val);

	return 0;
}

static void tegra_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct tegra_pwm_chip *pc = to_tegra_pwm_chip(chip);
	unsigned int enb_offset;
	u32 val;

	enb_offset = pwm->hwpwm * pc->soc->channel_offset + pc->soc->enb_offset;
	val = pwm_readl(pc, enb_offset);
	val &= ~PWM_ENABLE;
	pwm_writel(pc, enb_offset, val);

	pm_runtime_put_sync(pc->dev);
}

static int tegra_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			   const struct pwm_state *state)
{
	int err;
	bool enabled = pwm->state.enabled;

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	if (!state->enabled) {
		if (enabled)
			tegra_pwm_disable(chip, pwm);

		return 0;
	}

	err = tegra_pwm_config(chip, pwm, state->duty_cycle, state->period);
	if (err)
		return err;

	if (!enabled)
		err = tegra_pwm_enable(chip, pwm);

	return err;
}

/**
 * tegra_pwm_depth_update - Update PWM depth CSR register if any
 * @chip: pwm chip whose pwm_depth is about to be updated
 *
 * Callers must assume that the PM usage counter is non-zero.
 */
static void tegra_pwm_depth_update(struct pwm_chip *chip)
{
	struct tegra_pwm_chip *pc = to_tegra_pwm_chip(chip);
	unsigned long depth_o = pc->soc->depth_offset;
	unsigned long depth_w = pc->soc->depth_width;
	unsigned long depth_s = pc->soc->depth_shift;

	pwm_writel_mask32(pc, depth_o, GENMASK(depth_s + depth_w - 1, depth_s),
			  pc->pwm_depth << depth_s);
}

static int tegra_pwm_probe_from_dt(struct device *dev,
				   struct tegra_pwm_chip *pc)
{
	struct device_node *np = dev->of_node;
	int ret;

	ret = of_property_read_u32(np, "nvidia,pwm-depth", &pc->pwm_depth);

	/* Ignore -EINVAL for optional property */
	if (ret == -EINVAL)
		return 0;

	if (ret)
		return ret;

	if (pc->pwm_depth >= (u32)(1 << pc->soc->depth_width)) {
		dev_err(dev, "invalid nvidia,pwm-depth property\n");
		return -EINVAL;
	}

	return ret;
}

static const struct pwm_ops tegra_pwm_ops = {
	.apply = tegra_pwm_apply,
};

static int tegra_pwm_probe(struct platform_device *pdev)
{
	struct tegra_pwm_chip *pc;
	int ret;

	pc = devm_kzalloc(&pdev->dev, sizeof(*pc), GFP_KERNEL);
	if (!pc)
		return -ENOMEM;

	pc->soc = of_device_get_match_data(&pdev->dev);
	pc->dev = &pdev->dev;

	pc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pc->regs))
		return PTR_ERR(pc->regs);

	platform_set_drvdata(pdev, pc);

	pc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pc->clk))
		return PTR_ERR(pc->clk);

	pc->pwm_depth = DEFAULT_PWM_DEPTH;
	ret = tegra_pwm_probe_from_dt(&pdev->dev, pc);
	if (ret)
		return ret;

	ret = devm_tegra_core_dev_init_opp_table_common(&pdev->dev);
	if (ret)
		return ret;

	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret)
		return ret;

	/* Set maximum frequency of the IP */
	ret = dev_pm_opp_set_rate(pc->dev, S64_MAX);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to set max frequency: %d\n", ret);
		goto put_pm;
	}

	/*
	 * The requested and configured frequency may differ due to
	 * clock register resolutions. Get the configured frequency
	 * so that PWM period can be calculated more accurately.
	 */
	pc->clk_rate = clk_get_rate(pc->clk);
	if (pc->clk_rate < (1 + pc->pwm_depth)) {
		dev_err(&pdev->dev, "Invalid clock frequency\n");
		ret = -EINVAL;
		goto put_pm;
	}

	/* Set minimum limit of PWM period for the IP */
	pc->min_period_ns =
		(NSEC_PER_SEC / (pc->clk_rate / (1 + pc->pwm_depth))) + 1;

	pc->rst = devm_reset_control_get_exclusive(&pdev->dev, "pwm");
	if (IS_ERR(pc->rst)) {
		ret = PTR_ERR(pc->rst);
		dev_err(&pdev->dev, "Reset control is not found: %d\n", ret);
		goto put_pm;
	}

	reset_control_deassert(pc->rst);

	pc->chip.dev = &pdev->dev;
	pc->chip.ops = &tegra_pwm_ops;
	pc->chip.npwm = pc->soc->num_channels;

	ret = pwmchip_add(&pc->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "pwmchip_add() failed: %d\n", ret);
		reset_control_assert(pc->rst);
		goto put_pm;
	}

	if (pc->soc->has_depth_csr)
		tegra_pwm_depth_update(&pc->chip);

	pm_runtime_put(&pdev->dev);

	return 0;
put_pm:
	pm_runtime_put_sync_suspend(&pdev->dev);
	pm_runtime_force_suspend(&pdev->dev);
	return ret;
}

static void tegra_pwm_remove(struct platform_device *pdev)
{
	struct tegra_pwm_chip *pc = platform_get_drvdata(pdev);

	pwmchip_remove(&pc->chip);

	reset_control_assert(pc->rst);

	pm_runtime_force_suspend(&pdev->dev);
}

static int __maybe_unused tegra_pwm_runtime_suspend(struct device *dev)
{
	struct tegra_pwm_chip *pc = dev_get_drvdata(dev);
	int err;

	clk_disable_unprepare(pc->clk);

	err = pinctrl_pm_select_sleep_state(dev);
	if (err) {
		clk_prepare_enable(pc->clk);
		return err;
	}

	return 0;
}

static int __maybe_unused tegra_pwm_runtime_resume(struct device *dev)
{
	struct tegra_pwm_chip *pc = dev_get_drvdata(dev);
	int err;

	err = pinctrl_pm_select_default_state(dev);
	if (err)
		return err;

	err = clk_prepare_enable(pc->clk);
	if (err) {
		pinctrl_pm_select_sleep_state(dev);
		return err;
	}

	return 0;
}

static const struct tegra_pwm_soc tegra20_pwm_soc = {
	.channel_offset = 16,
	.depth_width = 8,
	.duty_shift = 16,
	.duty_width = 9,
	.enb_offset = 0,
	.num_channels = 4,
	.scale_shift = 0,
	.scale_width = 13,
	.has_depth_csr = false,
};

static const struct tegra_pwm_soc tegra186_pwm_soc = {
	.channel_offset = 0,
	.depth_width = 8,
	.duty_shift = 16,
	.duty_width = 9,
	.enb_offset = 0,
	.num_channels = 1,
	.scale_shift = 0,
	.scale_width = 13,
	.has_depth_csr = false,
};

static const struct tegra_pwm_soc tegra194_pwm_soc = {
	.channel_offset = 0,
	.depth_width = 8,
	.duty_shift = 16,
	.duty_width = 9,
	.enb_offset = 0,
	.num_channels = 1,
	.scale_shift = 0,
	.scale_width = 13,
	.has_depth_csr = false,
};

static const struct tegra_pwm_soc tegra264_pwm_soc = {
	.channel_offset = 0,
	.depth_offset = 4,
	.depth_shift = 15,
	.depth_width = 16,
	.duty_shift = 16,
	.duty_width = 16,
	.enb_offset = 4,
	.num_channels = 1,
	.scale_shift = 0,
	.scale_width = 16,
	.has_depth_csr = true,
};

static const struct of_device_id tegra_pwm_of_match[] = {
	{ .compatible = "nvidia,tegra20-pwm", .data = &tegra20_pwm_soc },
	{ .compatible = "nvidia,tegra186-pwm", .data = &tegra186_pwm_soc },
	{ .compatible = "nvidia,tegra194-pwm", .data = &tegra194_pwm_soc },
	{ .compatible = "nvidia,tegra264-pwm", .data = &tegra264_pwm_soc },
	{ }
};
MODULE_DEVICE_TABLE(of, tegra_pwm_of_match);

static const struct dev_pm_ops tegra_pwm_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra_pwm_runtime_suspend, tegra_pwm_runtime_resume,
			   NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static struct platform_driver tegra_pwm_driver = {
	.driver = {
		.name = "tegra-pwm",
		.of_match_table = tegra_pwm_of_match,
		.pm = &tegra_pwm_pm_ops,
	},
	.probe = tegra_pwm_probe,
	.remove_new = tegra_pwm_remove,
};

module_platform_driver(tegra_pwm_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sandipan Patra <spatra@nvidia.com>");
MODULE_DESCRIPTION("Tegra PWM controller driver");
MODULE_ALIAS("platform:tegra-pwm");
