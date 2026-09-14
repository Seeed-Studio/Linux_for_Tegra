// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/bits.h>
#include <linux/reset.h>
#include "dev.h"

/* Defining offsets */
#define AO_MISC_CPU_RESET_VECTOR_0 0x0
#define AO_MISC_CPU_RUNSTALL_0 0x4
#define AO_MISC_CPU_WFI_STATUS_0 0x8

/* Defining fields */
#define AO_MISC_CPU_SET_RUNSTALL_0 0x1
#define AO_MISC_CPU_CLEAR_RUNSTALL_0 0x0

static int nvaon_os_t264_init(struct platform_device *pdev)
{
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	void __iomem *cpu_config_base = drv_data->base_regs[AO_MISC];
	u32 cpu_config;

	/**
	 * If AON CPU is already running at driver probe,
	 * then assume that it is expected to be always ON
	 */
	cpu_config = readl(cpu_config_base + AO_MISC_CPU_RUNSTALL_0);
	if (cpu_config == AO_MISC_CPU_CLEAR_RUNSTALL_0) {
		dev_info(dev, "AON CPU running as always ON\n");
		drv_data->is_always_on = true;
	}

	return 0;
}

#ifdef CONFIG_PM
static int nvaon_t264_clocks_disable(struct platform_device *pdev)
{
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	if (drv_data->adsp_clk) {
		clk_disable_unprepare(drv_data->adsp_clk);
		dev_dbg(dev, "cpu_clock disabled\n");
	}

	return 0;
}

static int nvaon_t264_clocks_enable(struct platform_device *pdev)
{
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	int ret = 0;

	if (drv_data->adsp_clk) {
		ret = clk_prepare_enable(drv_data->adsp_clk);
		if (ret)
			dev_err(dev, "unable to enable cpu_clock\n");
		else
			dev_dbg(dev, "cpu_clock enabled\n");
	}

	return ret;
}

static int __nvaon_t264_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(pdev);
	int ret;

	dev_dbg(dev, "at %s:%d\n", __func__, __LINE__);

	if (drv_data->is_always_on)
		return 0;

	ret = nvaon_t264_clocks_enable(pdev);
	if (ret) {
		dev_warn(dev, "failed in nvadsp_t264_clocks_enable\n");
		return ret;
	}

	return ret;
}

static int __nvaon_t264_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(pdev);

	dev_dbg(dev, "at %s:%d\n", __func__, __LINE__);

	if (drv_data->is_always_on)
		return 0;

	return nvaon_t264_clocks_disable(pdev);
}

static int __nvaon_t264_runtime_idle(struct device *dev)
{
	dev_dbg(dev, "at %s:%d\n", __func__, __LINE__);
	return 0;
}

static int nvaon_pm_t264_init(struct platform_device *pdev)
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

static void __dump_core_state_t264_aon(struct nvadsp_drv_data *d)
{
	/* TBD */
}

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
	struct platform_device *pdev = d->pdev;
	struct device *dev = &pdev->dev;
	void __iomem *cpu_config_base;
	u32 cpu_config;
	int ret = 0;

	/* Assert RUNSTALL */
	cpu_config_base = d->base_regs[AO_MISC];
	cpu_config = AO_MISC_CPU_SET_RUNSTALL_0;
	writel(cpu_config, cpu_config_base + AO_MISC_CPU_RUNSTALL_0);

	/* CAR assert */
	ret = reset_control_assert(d->adspall_rst);
	if (ret)
		dev_err(dev, "failed to assert aon_cpu: %d\n", ret);

	return 0;
}

static int __deassert_t264_aon(struct nvadsp_drv_data *d)
{
	struct platform_device *pdev = d->pdev;
	struct device *dev = &pdev->dev;
	void __iomem *cpu_config_base;
	u32 cpu_config;
	int ret = 0;

	/* CAR deassert */
	ret = reset_control_deassert(d->adspall_rst);
	if (ret) {
		dev_err(dev, "failed to deassert aon_cpu: %d\n", ret);
		goto end;
	}

	/* Lift RUNSTALL */
	cpu_config_base = d->base_regs[AO_MISC];
	cpu_config = AO_MISC_CPU_CLEAR_RUNSTALL_0;
	writel(cpu_config, cpu_config_base + AO_MISC_CPU_RUNSTALL_0);

end:
	return 0;
}

static int __map_hwmbox_interrupts(struct nvadsp_drv_data *d)
{

	/* WAR: Map hwmbox interrupts to different shared interrupt lines
	 * TBD: Actual implementation shall use only one interrupt line i.e SI-1
	 * for both full and empty interrupts
	 */
	void __iomem *hsp_config_base;

	/* Map the AON HSP COMMON physical address to virtual address */
	hsp_config_base = ioremap(0xc400000, 0x10000);
	if (!hsp_config_base) {
		return -ENOMEM;
	}

	/* Map mbx1 empty int to SI-2 */
	writel(0x2, (hsp_config_base + 0x108));

	/* Map mbx0 full int to SI-3 */
	writel(0x100, (hsp_config_base + 0x10c));

	return 0;
}

static bool __check_wfi_status_t264_aon(struct nvadsp_drv_data *d)
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

static int nvaon_dev_t264_init(struct platform_device *pdev)
{
	struct nvadsp_drv_data *d = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	int ret = 0;

	d->assert_adsp   = __assert_t264_aon;
	d->deassert_adsp = __deassert_t264_aon;

	d->set_boot_vec    = __set_boot_vec_t264;
	d->set_boot_freqs  = __set_boot_freqs_t264;

	d->map_hwmbox_interrupts = __map_hwmbox_interrupts;
	d->check_wfi_status = __check_wfi_status_t264_aon;
	d->dump_core_state  = __dump_core_state_t264_aon;

	d->adspall_rst = devm_reset_control_get(dev, "aon_cpu");
	if (IS_ERR(d->adspall_rst)) {
		dev_err(dev, "cannot get aon_cpu reset\n");
		ret = PTR_ERR(d->adspall_rst);
	}

	return ret;
}

struct nvadsp_chipdata tegra264_aon_chipdata = {
	.hwmb = {
		.reg_idx = AON_HSP,
		.hwmbox0_reg = 0x00000,
		.hwmbox1_reg = 0X08000,
		.hwmbox2_reg = 0X10000,
		.hwmbox3_reg = 0X18000,
		.hwmbox4_reg = 0X20000,
		.hwmbox5_reg = 0X28000,
		.hwmbox6_reg = 0X30000,
		.hwmbox7_reg = 0X38000,
		.empty_int_ie = 0x8,
	},
	.adsp_shared_mem_hwmbox    = 0x08048, /* HWMBOX1 TYPE1_DATA0 */
	.adsp_boot_config_hwmbox   = 0x0804C, /* HWMBOX1 TYPE1_DATA1 */
	.adsp_cpu_freq_hwmbox      = 0x08050, /* HWMBOX1 TYPE1_DATA2 */
	.dev_init = nvaon_dev_t264_init,
	.os_init = nvaon_os_t264_init,
#ifdef CONFIG_PM
	.pm_init = nvaon_pm_t264_init,
#endif
	.adsp_elf = "aon_t264.elf",
	.num_irqs = NVAON_VIRQ_MAX,
	.amc_not_avlbl = true,
	.no_wfi_irq = true,
};
