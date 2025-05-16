// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION. All rights reserved.
/*
 * drivers/soc/tegra/tegra264-pmc.c
 */

#define pr_fmt(fmt) "tegra264-pmc: " fmt

#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <soc/tegra/common.h>
#include <soc/tegra/fuse.h>
#include <soc/tegra/pmc.h>

#include <dt-bindings/pinctrl/pinctrl-tegra-io-pad.h>
#include <dt-bindings/gpio/tegra264-gpio.h>


static int tegra_pmc_reboot_notify(struct notifier_block *this,
				   unsigned long action, void *data)
{
	struct tegra_pmc *pmc = container_of(this, struct tegra_pmc,
					reboot_notifier);

	if (action == SYS_RESTART)
		tegra_pmc_program_reboot_reason(pmc, data);

	return NOTIFY_DONE;
}

/**
 * devm_tegra_pmc_get() - get Tegra PMC pointer
 * @dev: device pointer of the Client device
 *
 * Return: ERR_PTR() on error or a valid pointer to Tegra PMC.
 */
static struct tegra_pmc *devm_tegra_pmc_get(struct device *dev)
{
	struct platform_device *pdev;
	struct tegra_pmc *pmc;
	struct device_node *np;

	np = of_parse_phandle(dev->of_node, "nvidia,pmc", 0);
	if (!np)
		return ERR_PTR(-ENOENT);

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev)
		return ERR_PTR(-ENODEV);

	pmc = platform_get_drvdata(pdev);
	if (!pmc)
		return ERR_PTR(-EPROBE_DEFER);

	return pmc;
}

/**
 * tegra264_io_pad_power_enable() - enable power to I/O pad
 * @dev: device pointer of the Client device
 * @id: Tegra I/O pad ID for which to enable power
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int tegra264_io_pad_power_enable(struct device *dev, enum tegra_io_pad id)
{
	struct tegra_pmc *pmc = devm_tegra_pmc_get(dev);
	if (IS_ERR(pmc))
		return PTR_ERR(pmc);

	return tegra186_io_pad_power_enable(pmc, id);
}
EXPORT_SYMBOL(tegra264_io_pad_power_enable);

/**
 * tegra264_io_pad_power_disable() - disable power to I/O pad
 * @dev: device pointer of the Client device
 * @id: Tegra I/O pad ID for which to enable power
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int tegra264_io_pad_power_disable(struct device *dev, enum tegra_io_pad id)
{
	struct tegra_pmc *pmc = devm_tegra_pmc_get(dev);
	if (IS_ERR(pmc))
		return PTR_ERR(pmc);

	return tegra186_io_pad_power_disable(pmc, id);
}
EXPORT_SYMBOL(tegra264_io_pad_power_disable);

static int tegra_pmc_probe(struct platform_device *pdev)
{
	struct tegra_pmc *pmc;
	struct resource *res;
	int err;

	pmc = devm_kzalloc(&pdev->dev, sizeof(*pmc), GFP_KERNEL);
	if (!pmc)
		return -ENOMEM;

	pmc->soc = of_device_get_match_data(&pdev->dev);
	pmc->dev = &pdev->dev;

	/* take over the memory region from the early initialization */
	pmc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pmc->base))
		return PTR_ERR(pmc->base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "wake");
	pmc->wake = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pmc->wake))
		return PTR_ERR(pmc->wake);

	/* "scratch" is an optional aperture */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "scratch");
	if (res) {
		pmc->scratch = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(pmc->scratch))
			return PTR_ERR(pmc->scratch);
	} else {
		pmc->scratch = NULL;
	}

	/*
	 * PMC should be last resort for restarting since it soft-resets
	 * CPU without resetting everything else.
	 */
	if (pmc->scratch) {
		pmc->reboot_notifier.notifier_call = tegra_pmc_reboot_notify;
		err = devm_register_reboot_notifier(&pdev->dev,
						    &pmc->reboot_notifier);
		if (err) {
			dev_err(&pdev->dev,
				"unable to register reboot notifier, %d\n",
				err);
			return err;
		}
	}

	err = tegra_pmc_init(pmc);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to initialize PMC: %d\n", err);
		return err;
	}

	tegra_pmc_reset_sysfs_init(pmc);

	err = tegra_pmc_pinctrl_init(pmc);
	if (err)
		goto cleanup_sysfs;

	err = tegra_pmc_irq_init(pmc);
	if (err < 0)
		goto cleanup_sysfs;

	/* Some wakes require specific filter configuration */
	if (pmc->soc->set_wake_filters)
		pmc->soc->set_wake_filters(pmc);

	platform_set_drvdata(pdev, pmc);

	return 0;

cleanup_sysfs:
	tegra_pmc_reset_sysfs_remove(pmc);

	return err;
}

static int __maybe_unused tegra_pmc_resume(struct device *dev)
{
	struct tegra_pmc *pmc = dev_get_drvdata(dev);

	tegra186_pmc_resume(pmc);

	return 0;
}

static int  __maybe_unused tegra_pmc_suspend(struct device *dev)
{
	struct tegra_pmc *pmc = dev_get_drvdata(dev);

	return tegra186_pmc_suspend(pmc);
}

static const struct dev_pm_ops tegra_pmc_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(tegra_pmc_suspend, tegra_pmc_resume)
};

static const char * const tegra264_reset_levels[] = {
	"L0", "L1", "L2", "WARM"
};

#define TEGRA264_IO_PAD(_id, _dpd, _request, _status, _has_int_reg, _e_reg06, _e_reg18, _voltage, _e_33v_ctl, _name)	\
	((struct tegra_io_pad_soc) {					\
		.id		= (_id),				\
		.dpd		= (_dpd),				\
		.request	= (_request),				\
		.status		= (_status),				\
		.has_int_reg	= (_has_int_reg),			\
		.e_reg06	= (_e_reg06),				\
		.e_reg18	= (_e_reg18),				\
		.voltage	= (_voltage),				\
		.e_33v_ctl	= (_e_33v_ctl),				\
		.name		= (_name),				\
	})

#define TEGRA_IO_PIN_DESC(_id, _name)	\
	((struct pinctrl_pin_desc) {	\
		.number	= (_id),	\
		.name	= (_name),	\
	})

static const struct tegra_io_pad_soc tegra264_io_pads[] = {
	TEGRA264_IO_PAD(TEGRA_IO_PAD_CSIA, 0, 0x41020, 0x41024, 0, UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX, "csia"),
	TEGRA264_IO_PAD(TEGRA_IO_PAD_CSIB, 1, 0x41020, 0x41024, 0, UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX, "csib"),
	TEGRA264_IO_PAD(TEGRA_IO_PAD_HDMI_DP0, 0, 0x41050, 0x41054, 0, UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX, "hdmi-dp0"),
	TEGRA264_IO_PAD(TEGRA_IO_PAD_CSIC, 2, 0x41020, 0x41024, 0, UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX, "csic"),
	TEGRA264_IO_PAD(TEGRA_IO_PAD_CSID, 3, 0x41020, 0x41024, 0, UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX, "csid"),
	TEGRA264_IO_PAD(TEGRA_IO_PAD_CSIE, 4, 0x41020, 0x41024, 0, UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX, "csie"),
	TEGRA264_IO_PAD(TEGRA_IO_PAD_CSIF, 5, 0x41020, 0x41024, 0, UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX, "csif"),
	TEGRA264_IO_PAD(TEGRA_IO_PAD_UFS, 4, 0x41040, 0x41044, 0, UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX, "ufs0"),
	TEGRA264_IO_PAD(TEGRA_IO_PAD_EDP, 0, 0x41028, 0x4102c, 0, UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX, "edp"),
	TEGRA264_IO_PAD(TEGRA_IO_PAD_SDMMC1, 0, 0x41090, 0x41094, 0, UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX, "sdmmc1"),
	TEGRA264_IO_PAD(TEGRA_IO_PAD_SDMMC1_HV, UINT_MAX, UINT_MAX, UINT_MAX, 1, 2, 1, 0, 0x41004, "sdmmc1-hv"),
	TEGRA264_IO_PAD(TEGRA_IO_PAD_CSIG, 6, 0x41020, 0x41024, 0, UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX, "csig"),
	TEGRA264_IO_PAD(TEGRA_IO_PAD_CSIH, 7, 0x41020, 0x41024, 0, UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX, "csih"),
};

static const struct pinctrl_pin_desc tegra264_pin_descs[] = {
	TEGRA_IO_PIN_DESC(TEGRA_IO_PAD_CSIA, "csia"),
	TEGRA_IO_PIN_DESC(TEGRA_IO_PAD_CSIB, "csib"),
	TEGRA_IO_PIN_DESC(TEGRA_IO_PAD_HDMI_DP0, "hdmi-dp0"),
	TEGRA_IO_PIN_DESC(TEGRA_IO_PAD_CSIC, "csic"),
	TEGRA_IO_PIN_DESC(TEGRA_IO_PAD_CSID, "csid"),
	TEGRA_IO_PIN_DESC(TEGRA_IO_PAD_CSIE, "csie"),
	TEGRA_IO_PIN_DESC(TEGRA_IO_PAD_CSIF, "csif"),
	TEGRA_IO_PIN_DESC(TEGRA_IO_PAD_UFS, "ufs0"),
	TEGRA_IO_PIN_DESC(TEGRA_IO_PAD_EDP, "edp"),
	TEGRA_IO_PIN_DESC(TEGRA_IO_PAD_SDMMC1, "sdmmc1"),
	TEGRA_IO_PIN_DESC(TEGRA_IO_PAD_SDMMC1_HV, "sdmmc1-hv"),
	TEGRA_IO_PIN_DESC(TEGRA_IO_PAD_CSIG, "csig"),
	TEGRA_IO_PIN_DESC(TEGRA_IO_PAD_CSIH, "csih"),
};

static const struct tegra_pmc_regs tegra264_pmc_regs = {
	.scratch0 = 0x684,
	.rst_status = 0x4,
	.rst_source_shift = 0x2,
	.rst_source_mask = 0x1fc,
	.rst_level_shift = 0x0,
	.rst_level_mask = 0x3,
	.aowake_cntrl = 0x0,
	.aowake_mask_w = 0x200,
	.aowake_status_w = 0x410,
	.aowake_status_r = 0x610,
	.aowake_tier2_routing = 0x660,
	.aowake_sw_status_w = 0x624,
	.aowake_sw_status = 0x628,
	.aowake_latch_sw = 0x620,
	.aowake_ctrl = 0x68c,
};

static const char * const tegra264_reset_sources[] = {
	"SYS_RESET_N",		/* 0 */
	"CSDC_RTC_XTAL",
	"VREFRO_POWER_BAD",
	"SCPM_SOC_XTAL",
	"SCPM_RTC_XTAL",
	"FMON_32K",
	"FMON_OSC",
	"POD_RTC",
	"POD_IO",
	"POD_PLUS_IO_SPLL",
	"POD_PLUS_SOC",		/* 10 */
	"VMON_PLUS_UV",
	"VMON_PLUS_OV",
	"FUSECRC_FAULT",
	"OSC_FAULT",
	"BPMP_BOOT_FAULT",
	"SCPM_BPMP_CORE_CLK",
	"SCPM_PSC_SE_CLK",
	"VMON_SOC_MIN",
	"VMON_SOC_MAX",
	"VMON_MSS_MIN",		/* 20 */
	"VMON_MSS_MAX",
	"POD_PLUS_IO_U4_TSENSE",
	"SOC_THERM_FAULT",
	"FSI_THERM_FAULT",
	"PSC_TURTLE_MODE",
	"SCPM_OESP_SE_CLK",
	"SCPM_SB_SE_CLK",
	"POD_CPU",
	"POD_GPU",
	"DCLS_GPU",		/* 30 */
	"POD_MSS",
	"FSI_FMON",
	"VMON_FSI_MIN",
	"VMON_FSI_MAX",
	"VMON_CPU_MIN",
	"VMON_CPU_MAX",
	"NVJTAG_SEL_MONITOR",
	"BPMP_FMON",
	"AO_WDT_POR",
	"BPMP_WDT_POR",		/* 40 */
	"AO_TKE_WDT_POR",
	"RCE0_WDT_POR",
	"RCE1_WDT_POR",
	"DCE_WDT_POR",
	"PVA_0_WDT_POR",
	"FSI_R5_WDT_POR",
	"FSI_R52_0_WDT_POR",
	"FSI_R52_1_WDT_POR",
	"FSI_R52_2_WDT_POR",
	"FSI_R52_3_WDT_POR",	/* 50 */
	"TOP_0_WDT_POR",
	"TOP_1_WDT_POR",
	"TOP_2_WDT_POR",
	"APE_C0_WDT_POR",
	"APE_C1_WDT_POR",
	"GPU_TKE_WDT_POR",
	"OESP_WDT_POR",
	"SB_WDT_POR",
	"PSC_WDT_POR",
	"SW_MAIN",		/* 60 */
	"L0L1_RST_OUT_N",
	"FSI_HSM",
	"CSITE_SW",
	"AO_WDT_DBG",
	"BPMP_WDT_DBG",
	"AO_TKE_WDT_DBG",
	"RCE0_WDT_DBG",
	"RCE1_WDT_DBG",
	"DCE_WDT_DBG",
	"PVA_0_WDT_DBG",	/* 70 */
	"FSI_R5_WDT_DBG",
	"FSI_R52_0_WDT_DBG",
	"FSI_R52_1_WDT_DBG",
	"FSI_R52_2_WDT_DBG",
	"FSI_R52_3_WDT_DBG",
	"TOP_0_WDT_DBG",
	"TOP_1_WDT_DBG",
	"TOP_2_WDT_DBG",
	"APE_C0_WDT_DBG",
	"APE_C1_WDT_DBG",	/* 80 */
	"SB_WDT_DBG",
	"OESP_WDT_DBG",
	"PSC_WDT_DBG",
	"TSC_0_WDT_DBG",
	"TSC_1_WDT_DBG",
	"L2_RST_OUT_N",
	"SC7",			/* 87 */
};

static const struct tegra_wake_event tegra264_wake_events[] = {
	TEGRA_WAKE_IRQ("pmu", 0, 727),
	TEGRA_WAKE_IRQ("rtc", 65, 548),
	TEGRA_WAKE_IRQ("usb3_port_0", 79, 965),
	TEGRA_WAKE_IRQ("usb3_port_1", 80, 965),
	TEGRA_WAKE_IRQ("usb3_port_3", 82, 965),
	TEGRA_WAKE_IRQ("usb2_port_0", 83, 965),
	TEGRA_WAKE_IRQ("usb2_port_1", 84, 965),
	TEGRA_WAKE_IRQ("usb2_port_2", 85, 965),
	TEGRA_WAKE_IRQ("usb2_port_3", 86, 965),
	TEGRA_WAKE_GPIO("power_btn", 5, 1, TEGRA264_AON_GPIO(AA, 5)),
};

static const struct tegra_pmc_soc tegra264_pmc_soc = {
	.supports_core_domain = false,
	.num_powergates = 0,
	.powergates = NULL,
	.num_cpu_powergates = 0,
	.cpu_powergates = NULL,
	.has_tsense_reset = false,
	.has_gpu_clamps = false,
	.needs_mbist_war = false,
	.has_impl_33v_pwr = true,
	.maybe_tz_only = false,
	.num_io_pads = ARRAY_SIZE(tegra264_io_pads),
	.io_pads = tegra264_io_pads,
	.num_pin_descs = ARRAY_SIZE(tegra264_pin_descs),
	.pin_descs = tegra264_pin_descs,
	.regs = &tegra264_pmc_regs,
	.init = NULL,
	.setup_irq_polarity = tegra186_pmc_setup_irq_polarity,
	.set_wake_filters = tegra186_pmc_set_wake_filters,
	.irq_set_wake = tegra186_pmc_irq_set_wake,
	.irq_set_type = tegra186_pmc_irq_set_type,
	.reset_sources = tegra264_reset_sources,
	.num_reset_sources = ARRAY_SIZE(tegra264_reset_sources),
	.reset_levels = tegra264_reset_levels,
	.num_reset_levels = ARRAY_SIZE(tegra264_reset_levels),
	.num_wake_events = ARRAY_SIZE(tegra264_wake_events),
	.wake_events = tegra264_wake_events,
	.max_wake_events = 128,
	.max_wake_vectors = 4,
	.pmc_clks_data = NULL,
	.num_pmc_clks = 0,
	.has_blink_output = false,
	.has_single_mmio_aperture = false,
};

static const struct of_device_id tegra_pmc_match[] = {
	{ .compatible = "nvidia,tegra264-pmc", .data = &tegra264_pmc_soc },
	{ }
};

static struct platform_driver tegra_pmc_driver = {
	.driver = {
		.name = "tegra264-pmc",
		.suppress_bind_attrs = true,
		.of_match_table = tegra_pmc_match,
		.pm = &tegra_pmc_pm_ops,
	},
	.probe = tegra_pmc_probe,
};
builtin_platform_driver(tegra_pmc_driver);

/*
 * Early initialization to allow access to registers in the very early boot
 * process.
 */
static int __init tegra_pmc_early_init(void)
{
	const struct of_device_id *match;
	struct device_node *np = NULL;
	struct resource regs;
	struct tegra_pmc *pmc;
	bool invert;
	int ret = 0;

	pmc = kzalloc(sizeof(*pmc), GFP_KERNEL);
	if (!pmc)
		return -ENOMEM;

	for_each_matching_node_and_match(np, tegra_pmc_match, &match) {
		if (!of_device_is_available(np))
			continue;

		pmc->soc = match->data;

		/*
		 * Extract information from the device tree if we've found a
		 * matching node.
		 */
		if (of_address_to_resource(np, 0, &regs) < 0) {
			pr_err("failed to get PMC registers\n");
			of_node_put(np);
			ret = -ENXIO;
			goto err;
		}

		pmc->base = ioremap(regs.start, resource_size(&regs));
		if (!pmc->base) {
			pr_err("failed to map PMC registers\n");
			of_node_put(np);
			ret = -ENXIO;
			goto err;
		}

		/*
		 * Invert the interrupt polarity if a PMC device tree node
		 * exists and contains the nvidia,invert-interrupt property.
		 */
		invert = of_property_read_bool(np, "nvidia,invert-interrupt");

		pmc->soc->setup_irq_polarity(pmc, np, invert);

		iounmap(pmc->base);
	}

err:
	kfree(pmc);

	return ret;
}
early_initcall(tegra_pmc_early_init);
