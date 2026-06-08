// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/bits.h>
#include <linux/reset.h>
#include <soc/tegra/virt/hv-ivc.h>
#include "dev.h"
#include "hwmailbox.h"
#include "os.h"

#define AMISC_ADSP_CPU_CONFIG           (0x0)
#define   AMISC_ADSP_STATVECTORSEL      (1 << 4)
#define   AMISC_ADSP_RUNSTALL           (1 << 0)
#define AMISC_ADSP_CPU_RESETVEC         (0x4)
#define AMISC_ADSP_CPU_IDLE_STATUS      (0x2c)

static int nvadsp_os_t264_init(struct platform_device *pdev)
{
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(pdev);
	int ret = 0, val = 0;

	if (drv_data->chip_data->adsp_os_config_hwmbox != 0) {
		if (is_tegra_hypervisor_mode()) {
			/* Set ADSP to know its virtualized configuration */
			val = ADSP_CONFIG_VIRT_EN << ADSP_CONFIG_VIRT_SHIFT;

			/* Write to HWMBOX */
			hwmbox_writel(drv_data, val,
				drv_data->chip_data->adsp_os_config_hwmbox);
		}
	}

	return ret;
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

static int nvadsp_pm_t264_init(struct platform_device *pdev)
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

static void __dump_core_state_t264(struct nvadsp_drv_data *d)
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

static int __assert_t264_adsp(struct nvadsp_drv_data *d)
{
	struct platform_device *pdev = d->pdev;
	struct device *dev = &pdev->dev;
	void __iomem *cpu_config_base;
	u32 cpu_config;
	int ret = 0;

	/* Assert RUNSTALL */
	cpu_config_base = d->base_regs[AMISC];
	cpu_config = readl(cpu_config_base + AMISC_ADSP_CPU_CONFIG);
	cpu_config |= AMISC_ADSP_RUNSTALL;
	writel(cpu_config, cpu_config_base + AMISC_ADSP_CPU_CONFIG);

	/* CAR assert */
	ret = reset_control_assert(d->adspall_rst);
	if (ret)
		dev_err(dev, "failed to assert adsp: %d\n", ret);

	return ret;
}

static int __deassert_t264_adsp(struct nvadsp_drv_data *d)
{
	struct platform_device *pdev = d->pdev;
	struct device *dev = &pdev->dev;
	void __iomem *cpu_config_base;
	u32 cpu_config;
	int ret = 0;

	/* CAR deassert */
	ret = reset_control_deassert(d->adspall_rst);
	if (ret) {
		dev_err(dev, "failed to deassert adsp: %d\n", ret);
		goto end;
	}

	/* Deassert RUNSTALL */
	cpu_config_base = d->base_regs[AMISC];
	cpu_config = readl(cpu_config_base + AMISC_ADSP_CPU_CONFIG);
	cpu_config &= (~AMISC_ADSP_RUNSTALL);
	writel(cpu_config, cpu_config_base + AMISC_ADSP_CPU_CONFIG);

end:
	return ret;
}

static bool __check_wfi_status_t264(struct nvadsp_drv_data *d)
{
	void __iomem *cpu_config_base;
	bool wfi_status = false;
	u8 cnt = 5;

	cpu_config_base = d->base_regs[AMISC];

	while (cnt > 0) {
		wfi_status = readl(cpu_config_base + AMISC_ADSP_CPU_IDLE_STATUS);

		if (wfi_status)
			return wfi_status;
		cnt--;
	}

	return wfi_status;
}

static int nvadsp_dev_t264_init(struct platform_device *pdev)
{
	struct nvadsp_drv_data *d = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	int ret = 0;

	d->assert_adsp   = __assert_t264_adsp;
	d->deassert_adsp = __deassert_t264_adsp;

	d->set_boot_vec    = __set_boot_vec_t264;
	d->set_boot_freqs  = __set_boot_freqs_t264;

	d->check_wfi_status = __check_wfi_status_t264;
	d->dump_core_state  = __dump_core_state_t264;

	d->adspall_rst = devm_reset_control_get(dev, "adsp");
	if (IS_ERR(d->adspall_rst)) {
		dev_err(dev, "cannot get adsp reset\n");
		ret = PTR_ERR(d->adspall_rst);
	}

	return ret;
}

struct nvadsp_chipdata tegra264_adsp0_chipdata = {
	.hwmb = {
		.reg_idx = AHSP,
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
	.adsp_os_config_hwmbox     = 0x08054, /* HWMBOX1 TYPE1_DATA3 */
	.dev_init = nvadsp_dev_t264_init,
	.os_init = nvadsp_os_t264_init,
#ifdef CONFIG_PM
	.pm_init = nvadsp_pm_t264_init,
#endif
	.adsp_elf = "adsp0_t264.elf",
	.num_irqs = NVADSP_VIRQ_MAX,
};

struct nvadsp_chipdata tegra264_adsp1_chipdata = {
	.hwmb = {
		.reg_idx = AHSP,
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
	.adsp_os_config_hwmbox     = 0x08054, /* HWMBOX1 TYPE1_DATA3 */
	.dev_init = nvadsp_dev_t264_init,
	.os_init = nvadsp_os_t264_init,
#ifdef CONFIG_PM
	.pm_init = nvadsp_pm_t264_init,
#endif
	.adsp_elf = "adsp1_t264.elf",
	.num_irqs = NVADSP_VIRQ_MAX,
	.amc_not_avlbl = true,
};
