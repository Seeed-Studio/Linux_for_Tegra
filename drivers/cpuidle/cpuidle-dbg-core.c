// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/smp.h>
#include <linux/limits.h>
#include <linux/minmax.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/platform_device.h>

#include "cpuidle-priv.h"

#define CREATE_TRACE_POINTS
#include <trace/events/cpuidle_debugfs_ftrace.h>

static char *gpio_chip;
module_param(gpio_chip, charp, 0644);
MODULE_PARM_DESC(gpio_chip, "GPIO controller chip label");

static int gpio_pin = -1;
module_param(gpio_pin, int, 0644);
MODULE_PARM_DESC(gpio_pin, "GPIO pin offset within controller");

static struct gpio_desc *gpio_desc;
static struct platform_device *gpio_pdev;
static struct gpiod_lookup_table gpio_lookup = {
	.dev_id = "cpuidle-gpio",
	.table = {
		{ /* Created based on module parameters */ },
		{ },
	},
};

struct cpuidle_work {
	u64 duration_ns;
	u64 exit_latency_ns;
	struct work_struct work;
};

/* Per-CPU struct for idle-state details & work struct */
static DEFINE_PER_CPU(struct cpuidle_work, cpuidle_dbg_work);

static unsigned int cpuidle_dbg_get_nirqs(void)
{
#if defined(NV_IRQ_GET_NR_IRQS_PRESENT) /* Linux v6.13 */
	return irq_get_nr_irqs();
#else
	return nr_irqs;
#endif
}

static bool cpuidle_dbg_is_timer_irq(int irq)
{
	bool timer = false;
	struct irq_data *data = irq_get_irq_data(irq);

	if (data) {
		struct irq_desc *desc = irq_data_to_desc(data);

		if (desc && desc->action)
			timer = (desc->action->flags & IRQF_TIMER);
	}
	return timer;
}

/* Function to disable all non-Timer and IPI IRQs. We need timers for CC7-wake. */
static void cpuidle_dbg_suspend_irqs(void)
{
	const unsigned int nirqs = cpuidle_dbg_get_nirqs();

	for (int irq = 0; irq < nirqs; irq++) {
		if (!cpuidle_dbg_is_timer_irq(irq)) {
			irq_set_status_flags(irq, IRQ_DISABLE_UNLAZY);
			disable_irq_nosync(irq);
		}
	}
}

static void cpuidle_dbg_resume_irqs(void)
{
	const unsigned int nirqs = cpuidle_dbg_get_nirqs();

	for (int irq = 0; irq < nirqs; irq++) {
		if (!cpuidle_dbg_is_timer_irq(irq)) {
			enable_irq(irq);
			irq_clear_status_flags(irq, IRQ_DISABLE_UNLAZY);
		}
	}
}

/* Assumed to be running on the target-core */
void cpuidle_dbg_sleep(u64 duration_ns, u64 exit_latency_ns)
{
	cpuidle_dbg_suspend_irqs();
	play_idle_precise(duration_ns, exit_latency_ns);
	cpuidle_dbg_resume_irqs();
}

static void cpuidle_dbg_sleep_func(struct work_struct *work)
{
	struct cpuidle_work *cpuidle_work = container_of(work, struct cpuidle_work, work);

	cpuidle_dbg_sleep(cpuidle_work->duration_ns, cpuidle_work->exit_latency_ns);
}

static void cpuidle_dbg_sleep_enqueue(void *info)
{
	struct cpuidle_work *cpuidle_work = this_cpu_ptr(&cpuidle_dbg_work);

	queue_work_on(smp_processor_id(), system_highpri_wq, &(cpuidle_work->work));
}

static void cpuidle_dbg_update_work(void *info)
{
	struct cpuidle_work *work_cpu = this_cpu_ptr(&cpuidle_dbg_work);
	struct cpuidle_work *work_new = (struct cpuidle_work *) info;

	work_cpu->duration_ns = work_new->duration_ns;
	work_cpu->exit_latency_ns = work_new->exit_latency_ns;
}

static void cpuidle_dbg_update_work_many(const struct cpumask *mask,
					 u64 residency_ns,
					 u64 exit_latency_ns)
{
	struct cpuidle_work work = {
		.duration_ns = residency_ns,
		.exit_latency_ns = exit_latency_ns,
	};

	smp_call_function_many(mask, cpuidle_dbg_update_work, &work, true);
}

static void cpuidle_dbg_wake_trace(void *info)
{
	trace_cpuidle_debugfs_print("Scheduled task after CPU_SUSPEND\n");
}

static void cpuidle_dbg_wake_many(void *info)
{
	struct cpumask *mask = (struct cpumask *) info;

	smp_call_function_many(mask, cpuidle_dbg_wake_trace, NULL, true);
	if (gpio_desc)
		gpiod_set_value(gpio_desc, 0);
}

static void cpuidle_dbg_wake(struct cpumask *mask, u64 src_cpu, u64 residency_ns)
{
	ndelay(residency_ns / 2);

	trace_cpuidle_debugfs_print("Triggering wake IPI\n");
	smp_call_function_single(src_cpu, cpuidle_dbg_wake_many, mask, true);
	trace_cpuidle_debugfs_print("Yielding ipi_src_cpu\n");
}

void cpuidle_dbg_sleep_many(struct cpumask *mask_A,
			    struct cpumask *mask_B,
			    u64 residency_ns_A,
			    u64 residency_ns_B,
			    u64 exit_latency_ns,
			    u64 src_cpu,
			    bool do_coordinated_wakeup)
{
	struct cpumask combined_mask;

	cpumask_or(&combined_mask, mask_A, mask_B);

	cpuidle_dbg_update_work_many(mask_A, residency_ns_A, exit_latency_ns);
	cpuidle_dbg_update_work_many(mask_B, residency_ns_B, exit_latency_ns);

	if (do_coordinated_wakeup && gpio_desc)
		gpiod_set_value(gpio_desc, 1);

	smp_call_function_many(&combined_mask, cpuidle_dbg_sleep_enqueue, NULL, true);

	if (do_coordinated_wakeup)
		cpuidle_dbg_wake(mask_A, src_cpu, residency_ns_A);
}

static int cpuidle_gpio_init(void)
{
	int err = 0;

	if (!gpio_chip || gpio_pin < 0) {
		pr_debug("cpuidle-gpio: No GPIO configured (gpio_chip=%s, gpio_pin=%d)\n",
			gpio_chip ? gpio_chip : "NULL", gpio_pin);
		goto out;
	}

	gpio_pdev = platform_device_register_simple("cpuidle-gpio", -1, NULL, 0);
	if (IS_ERR(gpio_pdev)) {
		err = PTR_ERR(gpio_pdev);
		pr_err("cpuidle-gpio: Failed to register platform device: %d\n", err);
		gpio_pdev = NULL;
		goto out;
	}

	gpio_lookup.table[0] = (struct gpiod_lookup)
		GPIO_LOOKUP_IDX(gpio_chip, gpio_pin, "cpuidle", 0, GPIO_ACTIVE_HIGH);
	gpiod_add_lookup_table(&gpio_lookup);

	gpio_desc = devm_gpiod_get(&gpio_pdev->dev, "cpuidle", GPIOD_OUT_LOW);
	if (IS_ERR(gpio_desc)) {
		err = PTR_ERR(gpio_desc);
		pr_err("cpuidle-gpio: Failed to get GPIO descriptor: %d\n", err);
		platform_device_unregister(gpio_pdev);
		gpiod_remove_lookup_table(&gpio_lookup);
		gpio_pdev = NULL;
		gpio_desc = NULL;
		goto out;
	}
	pr_debug("cpuidle-gpio: Initialized GPIO %s pin %d\n", gpio_chip, gpio_pin);

out:
	return err;
}

static int __init cpuidle_debugfs_init(void)
{
	int cpu, err;

	for_each_possible_cpu(cpu) {
		struct cpuidle_work *work_cpu = &per_cpu(cpuidle_dbg_work, cpu);

		INIT_WORK(&(work_cpu->work), cpuidle_dbg_sleep_func);
	}

	err = cpuidle_fs_init();
	if (err == 0)
		err = cpuidle_gpio_init();

	return err;
}

static void __exit cpuidle_debugfs_exit(void)
{
	if (gpio_pdev) {
		platform_device_unregister(gpio_pdev);
		gpiod_remove_lookup_table(&gpio_lookup);
		gpio_pdev = NULL;
		gpio_desc = NULL;
	}
	cpuidle_fs_teardown();
}

module_init(cpuidle_debugfs_init);
module_exit(cpuidle_debugfs_exit);

MODULE_AUTHOR("Ishan Shah <ishah@nvidia.com>");
MODULE_DESCRIPTION("cpuidle debugfs driver");
MODULE_LICENSE("GPL");
