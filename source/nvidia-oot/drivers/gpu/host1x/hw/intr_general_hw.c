// SPDX-License-Identifier: GPL-2.0-only
/*
 * Tegra host1x General Interrupt Management
 *
 * Copyright (C) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <linux/interrupt.h>

#include "../dev.h"

static irqreturn_t host1x_general_isr(int irq, void *dev_id)
{
	struct host1x *host = dev_id;
	unsigned long status;

	status = host1x_common_readl(host, HOST1X_COMMON_THOST_INTRSTATUS);

#if HOST1X_HW != 9
	if (status & HOST1X_COMMON_THOST_INTRSTATUS_NVENC_ACTMON_INTR)
		host1x_actmon_handle_interrupt(host, HOST1X_CLASS_NVENC);
#endif

	if (status & HOST1X_COMMON_THOST_INTRSTATUS_VIC_ACTMON_INTR)
		host1x_actmon_handle_interrupt(host, HOST1X_CLASS_VIC);

#if HOST1X_HW != 9
	if (status & HOST1X_COMMON_THOST_INTRSTATUS_NVDEC_ACTMON_INTR)
		host1x_actmon_handle_interrupt(host, HOST1X_CLASS_NVDEC);

	if (status & HOST1X_COMMON_THOST_INTRSTATUS_NVJPG_ACTMON_INTR)
		host1x_actmon_handle_interrupt(host, HOST1X_CLASS_NVJPG);

	if (status & HOST1X_COMMON_THOST_INTRSTATUS_NVJPG1_ACTMON_INTR)
		host1x_actmon_handle_interrupt(host, HOST1X_CLASS_NVJPG1);

	if (status & HOST1X_COMMON_THOST_INTRSTATUS_OFA_ACTMON_INTR)
		host1x_actmon_handle_interrupt(host, HOST1X_CLASS_OFA);
#endif

	host1x_common_writel(host, status, HOST1X_COMMON_THOST_INTRSTATUS);

	return IRQ_HANDLED;
}

static int host1x_intr_init_host_general(struct host1x *host)
{
	int err;

	host1x_hw_intr_disable_all_general_intrs(host);

	err = devm_request_threaded_irq(host->dev,
					host->general_irq,
					NULL, host1x_general_isr,
					IRQF_ONESHOT, "host1x_general",
					host);
	if (err < 0) {
		devm_free_irq(host->dev, host->general_irq, host);
		return err;
	}

	return 0;
}

static void host1x_intr_enable_general_intrs(struct host1x *host)
{
	if (!host->common_regs)
		return;

	/* Assign CPU0 for host1x general interrupts */
	host1x_common_writel(host, 0x1, HOST1X_COMMON_INTR_CPU0_MASK);

	/* Allow host1x general interrupts go to CPU0 only */
	host1x_common_writel(host, 0x1, HOST1X_COMMON_THOST_GLOBAL_INTRMASK);

#if HOST1X_HW != 9
	/* Enable host1x general interrupts */
	host1x_common_writel(host,
		HOST1X_COMMON_THOST_INTRMASK_NVENC_ACTMON(1) |
		HOST1X_COMMON_THOST_INTRMASK_VIC_ACTMON(1)   |
		HOST1X_COMMON_THOST_INTRMASK_NVDEC_ACTMON(1) |
		HOST1X_COMMON_THOST_INTRMASK_NVJPG_ACTMON(1) |
		HOST1X_COMMON_THOST_INTRMASK_NVJPG1_ACTMON(1)|
		HOST1X_COMMON_THOST_INTRMASK_OFA_ACTMON(1),
		HOST1X_COMMON_THOST_INTRMASK);
#else
	/* Enable host1x general interrupts */
	host1x_common_writel(host,
		HOST1X_COMMON_THOST_INTRMASK_VIC_ACTMON(1),
		HOST1X_COMMON_THOST_INTRMASK);
#endif
}

static void host1x_intr_disable_all_general_intrs(struct host1x *host)
{
	if (!host->common_regs)
		return;

	host1x_common_writel(host, 0x0, HOST1X_COMMON_THOST_INTRMASK);
}

static const struct host1x_intr_general_ops host1x_intr_general_ops = {
	.init_host_general = host1x_intr_init_host_general,
	.enable_general_intrs = host1x_intr_enable_general_intrs,
	.disable_all_general_intrs = host1x_intr_disable_all_general_intrs,
};
