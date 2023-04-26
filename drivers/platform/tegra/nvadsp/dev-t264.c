// SPDX-License-Identifier: GPL-2.0-only
/**
 * Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
 */

#include <dev.h>
#include "dev-t264.h"

#define AMISC_ADSP_CPU_CONFIG_STRIDE    (0x1000)
#define AMISC_ADSP_CPU_CONFIG           (0x0)
#define   AMISC_ADSP_STATVECTORSEL      (1 << 4)
#define   AMISC_ADSP_RUNSTALL           (1 << 0)
#define AMISC_ADSP_CPU_RESETVEC         0x4

int nvadsp_os_t264_init(struct platform_device *pdev)
{
	/* TBD */
	return 0;
}

#ifdef CONFIG_PM
static int nvadsp_t264_clocks_disable(struct platform_device *pdev)
{
	/* TBD */
	return 0;
}

static int nvadsp_t264_clocks_enable(struct platform_device *pdev)
{
	/* TBD */
	return 0;
}

static int __nvadsp_t264_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	int ret;

	dev_dbg(dev, "at %s:%d\n", __func__, __LINE__);

	ret = nvadsp_t264_clocks_enable(pdev);
	if (ret) {
		dev_dbg(dev, "failed in nvadsp_t264_clocks_enable\n");
		return ret;
	}

	return ret;
}

static int __nvadsp_t264_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	dev_dbg(dev, "at %s:%d\n", __func__, __LINE__);

	return nvadsp_t264_clocks_disable(pdev);
}

static int __nvadsp_t264_runtime_idle(struct device *dev)
{
	dev_dbg(dev, "at %s:%d\n", __func__, __LINE__);
	return 0;
}

int nvadsp_pm_t264_init(struct platform_device *pdev)
{
	struct nvadsp_drv_data *d = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	dev_dbg(dev, "at %s:%d\n", __func__, __LINE__);

	d->runtime_suspend = __nvadsp_t264_runtime_suspend;
	d->runtime_resume = __nvadsp_t264_runtime_resume;
	d->runtime_idle = __nvadsp_t264_runtime_idle;

	return 0;
}
#endif /* CONFIG_PM */

static int __set_boot_vec_t264(struct nvadsp_drv_data *d)
{
        /* TBD */
        return 0;
}

static int __set_boot_freqs_t264(struct nvadsp_drv_data *d)
{
	/* TBD */
	return 0;
}

static int __assert_t264_adsp(struct nvadsp_drv_data *d)
{
	void __iomem *cpu_config_base;
	u32 cpu_config;

	/* TBD: CAR assert */

	/* Assert RUNSTALL */
	cpu_config_base = d->base_regs[AMISC] +
		(d->chip_data->adsp_prid * AMISC_ADSP_CPU_CONFIG_STRIDE);
	cpu_config = readl(cpu_config_base + AMISC_ADSP_CPU_CONFIG);
	cpu_config |= AMISC_ADSP_RUNSTALL;
	writel(cpu_config, cpu_config_base + AMISC_ADSP_CPU_CONFIG);

	return 0;
}

static int __deassert_t264_adsp(struct nvadsp_drv_data *d)
{
	void __iomem *cpu_config_base;
	u32 cpu_config;

	/* TBD: CAR deassert */

	/* Deassert RUNSTALL */
	cpu_config_base = d->base_regs[AMISC] +
		(d->chip_data->adsp_prid * AMISC_ADSP_CPU_CONFIG_STRIDE);
	cpu_config = readl(cpu_config_base + AMISC_ADSP_CPU_CONFIG);
	cpu_config &= (~AMISC_ADSP_RUNSTALL);
	writel(cpu_config, cpu_config_base + AMISC_ADSP_CPU_CONFIG);

	return 0;
}

int nvadsp_reset_t264_init(struct platform_device *pdev)
{
	struct nvadsp_drv_data *d = platform_get_drvdata(pdev);
	int ret = 0;

	d->assert_adsp   = __assert_t264_adsp;
	d->deassert_adsp = __deassert_t264_adsp;
	d->adspall_rst   = NULL; //TBD

	d->set_boot_vec    = __set_boot_vec_t264;
	d->set_boot_freqs  = __set_boot_freqs_t264;

	return ret;
}
