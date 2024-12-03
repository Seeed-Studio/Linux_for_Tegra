// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * Module to force cpuidle states through debugfs files.
 */
#include <nvidia/conftest.h>

#include <linux/module.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/interrupt.h>
#include <linux/debugfs.h>
#include <linux/cpu.h>
#include <linux/cpuidle.h>

#define US_TO_NS(x) (1000 * x)

static struct cpuidle_driver *drv;

static bool is_timer_irq(struct irq_desc *desc)
{
	return desc && desc->action && (desc->action->flags & IRQF_TIMER);
}

static void suspend_all_device_irqs(void)
{
	struct irq_data *data;
	struct irq_desc *desc;
	unsigned int nirqs;
	int irq;

#if defined(NV_IRQ_GET_NR_IRQS_PRESENT) /* Linux v6.13 */
	nirqs = irq_get_nr_irqs();
#else
	nirqs = nr_irqs;
#endif

	for (irq = 0, data = irq_get_irq_data(irq); irq < nirqs;
			irq++, data = irq_get_irq_data(irq)) {
		if (!data)
			continue;
		desc = irq_data_to_desc(data);
		if (!desc || is_timer_irq(desc))
			continue;
		irq_set_status_flags(irq, IRQ_DISABLE_UNLAZY);
		disable_irq_nosync(irq);
	}
}

static void resume_all_device_irqs(void)
{
	struct irq_data *data;
	struct irq_desc *desc;
	unsigned int nirqs;
	int irq;

#if defined(NV_IRQ_GET_NR_IRQS_PRESENT)
	nirqs = irq_get_nr_irqs();
#else
	nirqs = nr_irqs;
#endif

	for (irq = 0, data = irq_get_irq_data(irq); irq < nirqs;
			irq++, data = irq_get_irq_data(irq)) {
		if (!data)
			continue;
		desc = irq_data_to_desc(data);
		if (!desc || is_timer_irq(desc))
			continue;
		enable_irq(desc->irq_data.irq);
		irq_clear_status_flags(irq, IRQ_DISABLE_UNLAZY);
	}
}

static int forced_idle_write(void *data, u64 val)
{
	struct cpuidle_state *idle_state = (struct cpuidle_state *) data;
	int ret = 0;
	u64 duration_ns = US_TO_NS(val);

	suspend_all_device_irqs();
	/* duration_ns, latency_ns */
	play_idle_precise(duration_ns, (u64) (idle_state->exit_latency_ns));
	resume_all_device_irqs();

	return ret;
}

DEFINE_SIMPLE_ATTRIBUTE(idle_state_fops, NULL, forced_idle_write, "%llu\n");

static struct dentry *cpuidle_debugfs_node;

static int init_debugfs(void)
{
	int i;

	cpuidle_debugfs_node = debugfs_create_dir("cpuidle_debug", NULL);
	if (!cpuidle_debugfs_node)
		goto err_out;

	for (i = 0; i < drv->state_count; i++) {
		debugfs_create_file(drv->states[i].name, 0200,
			cpuidle_debugfs_node, &(drv->states[i]), &idle_state_fops);
	}
	return 0;

err_out:
	pr_err("%s: Couldn't create debugfs node for cpuidle\n", __func__);
	debugfs_remove_recursive(cpuidle_debugfs_node);
	return -ENOMEM;
}

static int __init cpuidle_debugfs_probe(void)
{
	drv = cpuidle_get_driver();
	init_debugfs();
	return 0;
}

static void __exit cpuidle_debugfs_remove(void)
{
	debugfs_remove_recursive(cpuidle_debugfs_node);
}

module_init(cpuidle_debugfs_probe);
module_exit(cpuidle_debugfs_remove);

MODULE_AUTHOR("Ishan Shah <ishah@nvidia.com>");
MODULE_DESCRIPTION("cpuidle debugfs driver");
MODULE_LICENSE("GPL");
