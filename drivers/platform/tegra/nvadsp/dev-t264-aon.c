// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "dev.h"
#include "dev-t264-aon.h"

/* Defining offsets */
#define AO_MISC_CPU_RESET_VECTOR_0 0x0
#define AO_MISC_CPU_RUNSTALL_0 0x4
#define AO_MISC_CPU_WFI_STATUS_0 0x8

/* Defining fields */
#define AO_MISC_CPU_SET_RUNSTALL_0 0x1
#define AO_MISC_CPU_CLEAR_RUNSTALL_0 0x0

int nvaon_os_t264_init(struct platform_device *pdev)
{
	/* TBD */
	return 0;
}

static __attribute__((unused))
int nvaon_t264_clocks_disable(struct platform_device *pdev)
{
	/* TBD */
	return 0;
}

static __attribute__((unused))
int nvaon_t264_clocks_enable(struct platform_device *pdev)
{
	/* TBD */
	return 0;
}

#ifdef CONFIG_PM
static int __nvaon_t264_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	int ret;

	dev_dbg(dev, "at %s:%d\n", __func__, __LINE__);

	ret = nvaon_t264_clocks_enable(pdev);
	if (ret) {
		dev_dbg(dev, "failed in nvadsp_t264_clocks_enable\n");
		return ret;
	}

	return ret;
}

static int __nvaon_t264_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	dev_dbg(dev, "at %s:%d\n", __func__, __LINE__);

	return nvaon_t264_clocks_disable(pdev);
}

static int __nvaon_t264_runtime_idle(struct device *dev)
{
	dev_dbg(dev, "at %s:%d\n", __func__, __LINE__);
	return 0;
}

int nvaon_pm_t264_init(struct platform_device *pdev)
{
	struct nvadsp_drv_data *d = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	dev_dbg(dev, "at %s:%d\n", __func__, __LINE__);

	d->runtime_suspend = __nvaon_t264_runtime_suspend;
	d->runtime_resume = __nvaon_t264_runtime_resume;
	d->runtime_idle = __nvaon_t264_runtime_idle;

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

static int __assert_t264_aon(struct nvadsp_drv_data *d)
{
	void __iomem *cpu_config_base;
	u32 cpu_config;

	/* TBD: CPU assert */

	/* Assert RUNSTALL */
	cpu_config_base = d->base_regs[AO_MISC];
	cpu_config = AO_MISC_CPU_SET_RUNSTALL_0;
	writel(cpu_config, cpu_config_base + AO_MISC_CPU_RUNSTALL_0);

	return 0;
}

static int __deassert_t264_aon(struct nvadsp_drv_data *d)
{
	void __iomem *cpu_config_base;
	u32 cpu_config;

	/* TBD: CPU deassert */

	/* Lift RUNSTALL */
	cpu_config_base = d->base_regs[AO_MISC];
	cpu_config = AO_MISC_CPU_CLEAR_RUNSTALL_0;
	writel(cpu_config, cpu_config_base + AO_MISC_CPU_RUNSTALL_0);

	return 0;
}

static int __map_hwmbox_interrupts(struct nvadsp_drv_data *d)
{
	/* TBD: Map hwmbox interrupts to shared interrupt lines */

	return 0;
}

static bool __check_wfi_status(struct nvadsp_drv_data *d)
{
	void __iomem *cpu_config_base;
	bool wfi_status = false;
	u8 cnt = 5;

	cpu_config_base = d->base_regs[AO_MISC];

	while (cnt > 0) {
		wfi_status = readl(cpu_config_base + AO_MISC_CPU_WFI_STATUS_0);

		if (wfi_status)
			return wfi_status;
		cnt--;
	}

	return wfi_status;
}

int nvaon_reset_t264_init(struct platform_device *pdev)
{
	struct nvadsp_drv_data *d = platform_get_drvdata(pdev);
	int ret = 0;

	d->assert_adsp   = __assert_t264_aon;
	d->deassert_adsp = __deassert_t264_aon;
	d->adspall_rst   = NULL; //TBD

	d->set_boot_vec    = __set_boot_vec_t264;
	d->set_boot_freqs  = __set_boot_freqs_t264;

	d->map_hwmbox_interrupts = __map_hwmbox_interrupts;
	d->check_wfi_status = __check_wfi_status;
	return ret;
}
