// SPDX-License-Identifier: GPL-2.0-only
/**
 * Copyright (c) 2015-2024, NVIDIA CORPORATION. All rights reserved.
 */

#include <linux/version.h>
#include <soc/tegra/fuse.h>
#include <soc/tegra/virt/hv-ivc.h>
#include <linux/platform_device.h>
#include <linux/tegra_nvadsp.h>
#include <linux/reset.h>
#include <linux/clk.h>

#include <linux/delay.h>
#include <linux/tegra_nvadsp.h>

#ifdef CONFIG_TEGRA_VIRT_AUDIO_IVC
#include "tegra_virt_alt_ivc_common.h"
#include "tegra_virt_alt_ivc.h"
#endif

#include "dev.h"
#include "dev-t18x.h"
#include "amisc.h"
#include "amc.h"
#include "adsp_shared_struct.h"
#include "hwmailbox.h"
#include "log_state.h"

/* macros used to find the current mode of ADSP */
#define MODE_MASK 0x1f
#define MODE_USR 0x10
#define MODE_FIQ 0x11
#define MODE_IRQ 0x12
#define MODE_SVC 0x13
#define MODE_MON 0x16
#define MODE_ABT 0x17
#define MODE_UND 0x1b
#define MODE_SYS 0x1f

#ifdef CONFIG_PM
static int nvadsp_t18x_clocks_disable(struct platform_device *pdev)
{
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	/* APE and APB2APE clocks which are required by NVADSP are controlled
	 * from parent ACONNECT bus driver
	 */
	if (drv_data->adsp_clk) {
		clk_disable_unprepare(drv_data->adsp_clk);
		dev_dbg(dev, "adsp clocks disabled\n");
		drv_data->adsp_clk = NULL;
	}

	if (drv_data->aclk_clk) {
		clk_disable_unprepare(drv_data->aclk_clk);
		dev_dbg(dev, "aclk clock disabled\n");
		drv_data->aclk_clk = NULL;
	}

	if (drv_data->adsp_neon_clk) {
		clk_disable_unprepare(drv_data->adsp_neon_clk);
		dev_dbg(dev, "adsp_neon clocks disabled\n");
		drv_data->adsp_neon_clk = NULL;
	}

	return 0;
}

static int nvadsp_t18x_clocks_enable(struct platform_device *pdev)
{
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	int ret = 0;
	/* APE and APB2APE clocks which are required by NVADSP are controlled
	 * from parent ACONNECT bus driver
	 */
	drv_data->adsp_clk = devm_clk_get(dev, "adsp");
	if (IS_ERR_OR_NULL(drv_data->adsp_clk)) {
		dev_err(dev, "unable to find adsp clock\n");
		ret = PTR_ERR(drv_data->adsp_clk);
		goto end;
	}
	ret = clk_prepare_enable(drv_data->adsp_clk);
	if (ret) {
		dev_err(dev, "unable to enable adsp clock\n");
		goto end;
	}

	drv_data->aclk_clk = devm_clk_get(dev, "aclk");
	if (IS_ERR_OR_NULL(drv_data->aclk_clk)) {
		dev_err(dev, "unable to find aclk clock\n");
		ret = PTR_ERR(drv_data->aclk_clk);
		goto end;
	}
	ret = clk_prepare_enable(drv_data->aclk_clk);
	if (ret) {
		dev_err(dev, "unable to enable aclk clock\n");
		goto end;
	}

	drv_data->adsp_neon_clk = devm_clk_get(dev, "adspneon");
	if (IS_ERR_OR_NULL(drv_data->adsp_neon_clk)) {
		dev_err(dev, "unable to find adsp neon clock\n");
		ret = PTR_ERR(drv_data->adsp_neon_clk);
		goto end;
	}
	ret = clk_prepare_enable(drv_data->adsp_neon_clk);
	if (ret) {
		dev_err(dev, "unable to enable adsp neon clock\n");
		goto end;
	}
	dev_dbg(dev, "adsp neon clock enabled\n");

	dev_dbg(dev, "all clocks enabled\n");
	return 0;
 end:
	nvadsp_t18x_clocks_disable(pdev);
	return ret;
}

static int __nvadsp_t18x_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	int ret;

	dev_dbg(dev, "at %s:%d\n", __func__, __LINE__);

	ret = nvadsp_t18x_clocks_enable(pdev);
	if (ret) {
		dev_dbg(dev, "failed in nvadsp_t18x_clocks_enable\n");
		return ret;
	}

	return ret;
}

static int __nvadsp_t18x_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	dev_dbg(dev, "at %s:%d\n", __func__, __LINE__);

	return nvadsp_t18x_clocks_disable(pdev);
}

static int __nvadsp_t18x_runtime_idle(struct device *dev)
{
	dev_dbg(dev, "at %s:%d\n", __func__, __LINE__);
	return 0;
}

static int nvadsp_pm_t18x_init(struct platform_device *pdev)
{
	struct nvadsp_drv_data *d = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	dev_dbg(dev, "at %s:%d\n", __func__, __LINE__);

	d->runtime_suspend = __nvadsp_t18x_runtime_suspend;
	d->runtime_resume = __nvadsp_t18x_runtime_resume;
	d->runtime_idle = __nvadsp_t18x_runtime_idle;

	return 0;
}
#endif /* CONFIG_PM */

static void print_arm_mode_regs(struct nvadsp_drv_data *drv_data)
{
	struct nvadsp_exception_context *excep_context;
	struct arm_fault_frame_shared *shared_frame;
	struct arm_mode_regs_shared *shared_regs;
	struct nvadsp_shared_mem *shared_mem;
	struct device *dev = &drv_data->pdev->dev;

	shared_mem = drv_data->shared_adsp_os_data;
	excep_context = &shared_mem->exception_context;
	shared_frame = &excep_context->frame;
	shared_regs = &excep_context->regs;

	dev_err(dev, "dumping arm mode register data...\n");
	dev_err(dev, "%c fiq r13 0x%08x r14 0x%08x\n",
		((shared_frame->spsr & MODE_MASK) == MODE_FIQ) ? '*' : ' ',
		shared_regs->fiq_r13, shared_regs->fiq_r14);
	dev_err(dev, "%c irq r13 0x%08x r14 0x%08x\n",
		((shared_frame->spsr & MODE_MASK) == MODE_IRQ) ? '*' : ' ',
		shared_regs->irq_r13, shared_regs->irq_r14);
	dev_err(dev, "%c svc r13 0x%08x r14 0x%08x\n",
		((shared_frame->spsr & MODE_MASK) == MODE_SVC) ? '*' : ' ',
		shared_regs->svc_r13, shared_regs->svc_r14);
	dev_err(dev, "%c und r13 0x%08x r14 0x%08x\n",
		((shared_frame->spsr & MODE_MASK) == MODE_UND) ? '*' : ' ',
		shared_regs->und_r13, shared_regs->und_r14);
	dev_err(dev, "%c sys r13 0x%08x r14 0x%08x\n",
		((shared_frame->spsr & MODE_MASK) == MODE_SYS) ? '*' : ' ',
		shared_regs->sys_r13, shared_regs->sys_r14);
	dev_err(dev, "%c abt r13 0x%08x r14 0x%08x\n",
		((shared_frame->spsr & MODE_MASK) == MODE_ABT) ? '*' : ' ',
		shared_regs->abt_r13, shared_regs->abt_r14);
}

static void print_arm_fault_frame(struct nvadsp_drv_data *drv_data)
{
	struct nvadsp_exception_context *excep_context;
	struct arm_fault_frame_shared *shared_frame;
	struct nvadsp_shared_mem *shared_mem;
	struct device *dev = &drv_data->pdev->dev;

	shared_mem = drv_data->shared_adsp_os_data;
	excep_context = &shared_mem->exception_context;
	shared_frame = &excep_context->frame;

	dev_err(dev, "dumping fault frame...\n");
	dev_err(dev, "r0  0x%08x r1  0x%08x r2  0x%08x r3  0x%08x\n",
		 shared_frame->r[0], shared_frame->r[1], shared_frame->r[2],
		 shared_frame->r[3]);
	dev_err(dev, "r4  0x%08x r5  0x%08x r6  0x%08x r7  0x%08x\n",
		 shared_frame->r[4], shared_frame->r[5], shared_frame->r[6],
		 shared_frame->r[7]);
	dev_err(dev, "r8  0x%08x r9  0x%08x r10 0x%08x r11 0x%08x\n",
		 shared_frame->r[8], shared_frame->r[9], shared_frame->r[10],
		 shared_frame->r[11]);
	dev_err(dev, "r12 0x%08x usp 0x%08x ulr 0x%08x pc  0x%08x\n",
		 shared_frame->r[12], shared_frame->usp, shared_frame->ulr,
		 shared_frame->pc);
	dev_err(dev, "spsr 0x%08x\n", shared_frame->spsr);

}

static void dump_thread_name(struct platform_device *pdev, u32 val)
{
	dev_err(&pdev->dev, "%s: adsp current thread: %c%c%c%c\n",
		 __func__,
		 (val >> 24) & 0xFF, (val >> 16) & 0xFF,
		 (val >> 8) & 0xFF, (val >> 0) & 0xFF);
}

static void dump_irq_num(struct platform_device *pdev, u32 val)
{
	dev_err(&pdev->dev, "%s: adsp current/last irq : %d\n",
		 __func__, val);
}

static void get_adsp_state(struct nvadsp_drv_data *drv_data)
{
	struct device *dev;
	uint32_t val;
	const char *msg;

	if (!drv_data->pdev) {
		pr_err("ADSP Driver is not initialized\n");
		return;
	}

	dev = &drv_data->pdev->dev;

	if (drv_data->chip_data->adsp_state_hwmbox == 0) {
		dev_info(dev, "%s: No state hwmbox available\n", __func__);
		return;
	}

	val = hwmbox_readl(drv_data, drv_data->chip_data->adsp_state_hwmbox);
	dev_err(dev, "%s: adsp state hwmbox value: 0x%X\n", __func__, val);

	switch (val) {

	case ADSP_LOADER_MAIN_ENTRY:
		msg = "loader_main: entry to loader_main";
		break;
	case ADSP_LOADER_MAIN_CACHE_DISABLE_COMPLETE:
		msg = "loader_main: Cache has been disabled";
		break;
	case ADSP_LOADER_MAIN_CONFIGURE_MMU_COMPLETE:
		msg = "loader_main: MMU configuration is complete";
		break;
	case ADSP_LOADER_MAIN_CACHE_ENABLE_COMPLETE:
		msg = "loader_main: Cache has been enabled";
		break;
	case ADSP_LOADER_MAIN_FPU_ENABLE_COMPLETE:
		msg = "loader_main: FPU has been enabled";
		break;
	case ADSP_LOADER_MAIN_DECOMPRESSION_COMPLETE:
		msg = "loader_main: ADSP FW decompression is complete";
		break;
	case ADSP_LOADER_MAIN_EXIT:
		msg = "loader_main: exiting loader_main function";
		break;

	case ADSP_START_ENTRY_AT_RESET:
		msg = "start: ADSP is at reset";
		break;
	case ADSP_START_CPU_EARLY_INIT:
		msg = "start: ADSP to do cpu_early_init";
		break;
	case ADSP_START_FIRST_BOOT:
		msg = "start: ADSP is booting for first time,"
				"initializing DATA and clearing BSS";
		break;
	case ADSP_START_LK_MAIN_ENTRY:
		msg = "start: ADSP about to enter lk_main";
		break;

	case ADSP_LK_MAIN_ENTRY:
		msg = "lk_main: entry to lk_main";
		break;
	case ADSP_LK_MAIN_EARLY_THREAD_INIT_COMPLETE:
		msg = "lk_main: early_thread_init has been completed";
		break;
	case ADSP_LK_MAIN_EARLY_ARCH_INIT_COMPLETE:
		msg = "lk_main: early_arch_init has been completed";
		break;
	case ADSP_LK_MAIN_EARLY_PLATFORM_INIT_COMPLETE:
		msg = "lk_main: early_platform_init has been completed";
		break;
	case ADSP_LK_MAIN_EARLY_TARGET_INIT_COMPLETE:
		msg = "lk_main: early_target_init has been completed";
		break;
	case ADSP_LK_MAIN_CONSTRUCTOR_INIT_COMPLETE:
		msg = "lk_main: constructors has been called";
		break;
	case ADSP_LK_MAIN_HEAP_INIT_COMPLETE:
		msg = "lk_main: heap has been initialized";
		break;
	case ADSP_LK_MAIN_KERNEL_INIT_COMPLETE:
		msg = "lk_main: ADSP kernel has been initialized";
		break;
	case ADSP_LK_MAIN_CPU_RESUME_ENTRY:
		msg = "lk_main: ADSP is about to resume from suspend";
		break;

	case ADSP_BOOTSTRAP2_ARCH_INIT_COMPLETE:
		msg = "bootstrap2: ADSP arch_init is complete";
		break;
	case ADSP_BOOTSTRAP2_PLATFORM_INIT_COMPLETE:
		msg = "bootstrap2: platform has been initialized";
		break;
	case ADSP_BOOTSTRAP2_TARGET_INIT_COMPLETE:
		msg = "bootstrap2: target has been initialized";
		break;
	case ADSP_BOOTSTRAP2_APP_MODULE_INIT_COMPLETE:
		msg = "bootstrap2: APP modules initialized";
		break;
	case ADSP_BOOTSTRAP2_APP_INIT_COMPLETE:
		msg = "bootstrap2: APP init is complete";
		break;
	case ADSP_BOOTSTRAP2_STATIC_APP_INIT_COMPLETE:
		msg = "bootstrap2: Static apps has been initialized";
		break;
	case ADSP_BOOTSTRAP2_OS_LOAD_COMPLETE:
		msg = "bootstrap2: ADSP OS successfully loaded";
		break;
	case ADSP_SUSPEND_BEGINS:
		msg = "suspend: begins";
		break;
	case ADSP_SUSPEND_MBX_SEND_COMPLETE:
		msg = "suspend: mbox send complete";
		break;
	case ADSP_SUSPEND_DISABLED_TIMERS:
		msg = "suspend: timers disabled";
		break;
	case ADSP_SUSPEND_DISABLED_INTS:
		msg = "suspend: interrupts disabled";
		break;
	case ADSP_SUSPEND_ARAM_SAVED:
		msg = "suspend: aram saved";
		break;
	case ADSP_SUSPEND_AMC_SAVED:
		msg = "suspend: amc saved";
		break;
	case ADSP_SUSPEND_AMISC_SAVED:
		msg = "suspend: amisc saved";
		break;
	case ADSP_SUSPEND_L1_CACHE_DISABLED:
		msg = "suspend: l1 cache disabled";
		break;
	case ADSP_SUSPEND_L2_CACHE_DISABLED:
		msg = "suspend: l2 cache disabled";
		break;
	case ADSP_RESUME_ADSP:
		msg = "resume: beings";
		break;
	case ADSP_RESUME_AMISC_RESTORED:
		msg = "resume: amisc restored";
		break;
	case ADSP_RESUME_AMC_RESTORED:
		msg = "resume: amc restored";
		break;
	case ADSP_RESUME_ARAM_RESTORED:
		msg = "resume: aram restored";
		break;
	case ADSP_RESUME_COMPLETE:
		msg = "resume: complete";
		break;
	case ADSP_WFI_ENTER:
		msg = "WFI: Entering WFI";
		break;
	case ADSP_WFI_EXIT:
		msg = "WFI: Exiting WFI, Failed to Enter";
		break;
	case ADSP_DFS_MBOX_RECV:
		msg = "DFS: mbox received";
		break;
	case ADSP_DFS_MBOX_SENT:
		msg = "DFS: mbox sent";
		break;
	default:
		msg = "Unrecognized ADSP state!!";
		break;
	}

	dev_err(dev, "%s: %s\n", __func__, msg);

	val = hwmbox_readl(drv_data, drv_data->chip_data->adsp_thread_hwmbox);
	dump_thread_name(drv_data->pdev, val);

	val = hwmbox_readl(drv_data, drv_data->chip_data->adsp_irq_hwmbox);
	dump_irq_num(drv_data->pdev, val);
}

static void __dump_core_state_t18x(struct nvadsp_drv_data *drv_data)
{
	print_arm_fault_frame(drv_data);
	print_arm_mode_regs(drv_data);
	get_adsp_state(drv_data);
}

static int __set_boot_vec_t18x(struct nvadsp_drv_data *drv_data)
{
	struct platform_device *pdev = drv_data->pdev;
	struct device *dev = &pdev->dev;
	void *to = drv_data->state.evp_ptr;
	const void *from = drv_data->state.evp;
	int i, sz = AMC_EVP_SIZE;

	dev_dbg(dev, "Copying EVP...\n");
	for (i = 0; i < sz; i += 4) {
		u32 val = *(u32 *)(from + i);
		writel(val, (void __iomem *)(to + i));
	}

	return 0;
}

static int __set_boot_freqs_t18x(struct nvadsp_drv_data *drv_data)
{
	struct nvadsp_shared_mem *shared_mem = drv_data->shared_adsp_os_data;
	struct nvadsp_os_args *os_args = &shared_mem->os_args;
	struct platform_device *pdev = drv_data->pdev;
	struct device *dev = &pdev->dev;
	unsigned long max_adsp_freq;
	unsigned long adsp_freq;
	int ret = 0;

	if (!drv_data->adsp_clk)
		return -EINVAL;

	adsp_freq = drv_data->adsp_freq_hz; /* in Hz*/

	/* round rate shall be used with adsp parent clk i.e. aclk */
	max_adsp_freq = clk_round_rate(drv_data->aclk_clk, ULONG_MAX);

	/* Set max adsp boot freq */
	if (!adsp_freq)
		adsp_freq = max_adsp_freq;

	/* set rate shall be used with adsp parent clk i.e. aclk */
	ret = clk_set_rate(drv_data->aclk_clk, adsp_freq);
	if (ret) {
		dev_err(dev, "setting adsp_freq:%luHz failed.\n", adsp_freq);
		dev_err(dev, "max_adsp_freq:%luHz\n", max_adsp_freq);
		goto end;
	}

	drv_data->adsp_freq = adsp_freq / 1000; /* adsp_freq in KHz*/
	drv_data->adsp_freq_hz = adsp_freq;

	/* adspos uses os_args->adsp_freq_hz for EDF */
	os_args->adsp_freq_hz = drv_data->adsp_freq_hz;
end:
	dev_dbg(dev, "adsp cpu freq %luKHz\n",
		clk_get_rate(drv_data->adsp_clk) / 1000);
	return ret;
}

static int __assert_t18x_adsp(struct nvadsp_drv_data *d)
{
	struct platform_device *pdev = d->pdev;
	struct device *dev = &pdev->dev;
	int ret = 0;

	/*
	 * The ADSP_ALL reset in BPMP-FW is overloaded to assert
	 * all 7 resets i.e. ADSP, ADSPINTF, ADSPDBG, ADSPNEON,
	 * ADSPPERIPH, ADSPSCU and ADSPWDT resets. So resetting
	 * only ADSP reset is sufficient to reset all ADSP sub-modules.
	 */
	ret = reset_control_assert(d->adspall_rst);
	if (ret) {
		dev_err(dev, "failed to assert adsp\n");
		goto end;
	}

	/* APE_TKE reset */
	if (d->ape_tke_rst) {
		ret = reset_control_assert(d->ape_tke_rst);
		if (ret)
			dev_err(dev, "failed to assert ape_tke\n");
	}

end:
	return ret;
}

static int __deassert_t18x_adsp(struct nvadsp_drv_data *d)
{
	struct platform_device *pdev = d->pdev;
	struct device *dev = &pdev->dev;
	int ret = 0;

	/* APE_TKE reset */
	if (d->ape_tke_rst) {
		ret = reset_control_deassert(d->ape_tke_rst);
		if (ret) {
			 dev_err(dev, "failed to deassert ape_tke\n");
			 goto end;
		}
	}

	/*
	 * The ADSP_ALL reset in BPMP-FW is overloaded to de-assert
	 * all 7 resets i.e. ADSP, ADSPINTF, ADSPDBG, ADSPNEON, ADSPPERIPH,
	 * ADSPSCU and ADSPWDT resets. The BPMP-FW also takes care
	 * of specific de-assert sequence and delays between them.
	 * So de-resetting only ADSP reset is sufficient to de-reset
	 * all ADSP sub-modules.
	 */
	ret = reset_control_deassert(d->adspall_rst);
	if (ret)
		dev_err(dev, "failed to deassert adsp\n");

end:
	return ret;
}

#ifdef CONFIG_TEGRA_VIRT_AUDIO_IVC
static int __virt_assert_t18x_adsp(struct nvadsp_drv_data *d)
{
	int err;
	struct nvaudio_ivc_msg	msg;
	struct nvaudio_ivc_ctxt *hivc_client = nvaudio_get_ivc_alloc_ctxt();

	if (!hivc_client) {
		pr_err("%s: Failed to allocate IVC context\n", __func__);
		return -ENODEV;
	}

	memset(&msg, 0, sizeof(struct nvaudio_ivc_msg));
	msg.cmd = NVAUDIO_ADSP_RESET;
	msg.params.adsp_reset_info.reset_req = ASSERT;
	msg.ack_required = true;

	err = nvaudio_ivc_send_receive(hivc_client,
			&msg,
			sizeof(struct nvaudio_ivc_msg));
	if (err < 0)
		pr_err("%s: error on ivc_send_receive\n", __func__);

	return 0;
}

static int __virt_deassert_t18x_adsp(struct nvadsp_drv_data *d)
{
	int err;
	struct nvaudio_ivc_msg	msg;
	struct nvaudio_ivc_ctxt *hivc_client = nvaudio_get_ivc_alloc_ctxt();

	if (!hivc_client) {
		pr_err("%s: Failed to allocate IVC context\n", __func__);
		return -ENODEV;
	}

	memset(&msg, 0, sizeof(struct nvaudio_ivc_msg));
	msg.cmd = NVAUDIO_ADSP_RESET;
	msg.params.adsp_reset_info.reset_req = DEASSERT;
	msg.ack_required = true;

	err = nvaudio_ivc_send_receive(hivc_client,
			&msg,
			sizeof(struct nvaudio_ivc_msg));
	if (err < 0)
		pr_err("%s: error on ivc_send_receive\n", __func__);

	return 0;
}
#endif

static bool __check_wfi_status_t18x(struct nvadsp_drv_data *d)
{
	int cnt = 0;
	bool wfi_status = true;
	u32 adsp_status;

	/*
	 * Check L2_IDLE and L2_CLKSTOPPED in ADSP_STATUS
	 * NOTE: Standby mode in ADSP L2CC Power Control
	 *       register should be enabled for this
	 */
	do {
		adsp_status = amisc_readl(d, AMISC_ADSP_STATUS);
		if ((adsp_status & AMISC_ADSP_L2_IDLE) &&
		    (adsp_status & AMISC_ADSP_L2_CLKSTOPPED))
			break;
		cnt++;
		mdelay(1);
	} while (cnt < 5);

	if (cnt >= 5) {
		pr_err("ADSP L2C clock not halted: 0x%x\n", adsp_status);
		wfi_status = false;
	}

	return wfi_status;
}

static int nvadsp_dev_t18x_init(struct platform_device *pdev)
{
	struct nvadsp_drv_data *d = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	int ret = 0;

	d->set_boot_vec     = __set_boot_vec_t18x;
	d->set_boot_freqs   = __set_boot_freqs_t18x;
	d->check_wfi_status = __check_wfi_status_t18x;
	d->dump_core_state  = __dump_core_state_t18x;

#ifdef CONFIG_TEGRA_VIRT_AUDIO_IVC

	if (is_tegra_hypervisor_mode()) {
		d->assert_adsp = __virt_assert_t18x_adsp;
		d->deassert_adsp = __virt_deassert_t18x_adsp;
		d->adspall_rst = NULL;
		return 0;
	}
#endif

	d->assert_adsp = __assert_t18x_adsp;
	d->deassert_adsp = __deassert_t18x_adsp;
	d->adspall_rst = devm_reset_control_get(dev, "adspall");
	if (IS_ERR(d->adspall_rst)) {
		dev_err(dev, "can not get adspall reset\n");
		ret = PTR_ERR(d->adspall_rst);
		goto end;
	}

	d->ape_tke_rst = devm_reset_control_get(dev, "ape_tke");
	if (IS_ERR(d->ape_tke_rst))
		d->ape_tke_rst = NULL;

end:
	return ret;
}

struct nvadsp_chipdata tegrat18x_adsp_chipdata = {
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
	.adsp_shared_mem_hwmbox = 0x18000,   /* HWMBOX3 */
	.adsp_thread_hwmbox     = 0x20000,   /* HWMBOX4 */
	.adsp_os_config_hwmbox  = 0X28000,   /* HWMBOX5 */
	.adsp_state_hwmbox      = 0x30000,   /* HWMBOX6 */
	.adsp_irq_hwmbox        = 0x38000,   /* HWMBOX7 */
	.acast_init = nvadsp_acast_t18x_init,
	.dev_init = nvadsp_dev_t18x_init,
	.os_init = nvadsp_os_t18x_init,
#ifdef CONFIG_PM
	.pm_init = nvadsp_pm_t18x_init,
#endif

	.amc_err_war = true,
	.num_irqs = NVADSP_VIRQ_MAX,
};

struct nvadsp_chipdata tegra239_adsp_chipdata = {
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
	.adsp_shared_mem_hwmbox = 0x18000,   /* HWMBOX3 */
	.adsp_thread_hwmbox     = 0x20000,   /* HWMBOX4 */
	.adsp_os_config_hwmbox  = 0X28000,   /* HWMBOX5 */
	.adsp_state_hwmbox      = 0x30000,   /* HWMBOX6 */
	.adsp_irq_hwmbox        = 0x38000,   /* HWMBOX7 */
	.acast_init = nvadsp_acast_t18x_init,
	.dev_init = nvadsp_dev_t18x_init,
	.os_init = nvadsp_os_t18x_init,
#ifdef CONFIG_PM
	.pm_init = nvadsp_pm_t18x_init,
#endif

	.amc_err_war = false,

	/* Populate Chip ID Major Revision as well */
	.chipid_ext  = 0x9,
	.num_irqs    = NVADSP_VIRQ_MAX,
};
