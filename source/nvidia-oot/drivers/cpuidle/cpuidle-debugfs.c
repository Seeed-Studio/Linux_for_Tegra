// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2023-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#define CREATE_TRACE_POINTS
#include <trace/events/cpuidle_debugfs_ftrace.h>

#define US_TO_NS(x) (1000 * x)

static struct cpuidle_driver *drv;

/**
 * We have two cpumasks defined, groups a and b.
 * They are meant generally to support forced-idle entry for two different
 * time periods. E.g. you may want CPUs in a to reside for 10ms, but b to reside
 * for 100 ms. In this way, you can test the Coordination of various idle-states,
 * as the desired residency & latency can be passed to the OS (or implicitly to
 * the platform) to make decisions about deeper idle states.
 * This can also be used to test waking up cores at varying points.
 *
 * In the latency-test scenario, where you are using ipi-wake, only CPUs in mask
 * a are going to be woken up via IPI. This can allow for those CPUs in b to
 * stay asleep for longer periods of time, which may reveal the effects of e.g.
 * keeping one core in a clusterpair/one thread in a thread-pair asleep and have
 * the other woken up.
 */

/* Core-number for ipi-sourcing */
static u64 ipi_src_cpu;
/* CPU Mask struct for the coordinated-entry functions */
static struct cpumask sleep_dest_a;
static struct cpumask sleep_dest_b;
/* Desired cc7 residency for coordinated-entry functions */
static u64 sleep_residency_ns_a;
static u64 sleep_residency_ns_b;

/* Custom struct to encapsulate idle-state details & work struct */
struct coordinated_sleep_struct {
	bool do_coordinated_wakeup;
	uint64_t duration_ns;
	uint64_t exit_latency_ns;
	struct work_struct work;
};

/* Struct for coordinating idle-entry & exit */
struct coordinated_sleep_struct coordination_params;
/* Per-CPU struct for idle-state details & work struct */
static DEFINE_PER_CPU(struct coordinated_sleep_struct, enter_idle_work);

static bool is_timer_irq(struct irq_desc *desc)
{
	return desc && desc->action && (desc->action->flags & IRQF_TIMER);
}

/* Function to disable all non-Timer IRQs. We need Timers for CC7-Wake. */
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

/* play_idle_precise wrapper with IRQs disabled. */
/* Assumed to be running on the target-core */
static void forced_idle_entry(u64 duration_ns, u64 exit_latency_ns)
{
	suspend_all_device_irqs();
	/* duration_ns, latency_ns */
	play_idle_precise(duration_ns, exit_latency_ns);
	resume_all_device_irqs();
}

/* Function that runs on each CPU as part of the work struct */
/* This forces it into the appropriate low-power idle state */
static void forced_idle_work_func(struct work_struct *work)
{
	struct coordinated_sleep_struct *this_cpu_sleep = container_of(work,
		struct coordinated_sleep_struct, work);

	forced_idle_entry(this_cpu_sleep->duration_ns, this_cpu_sleep->exit_latency_ns);
}

/* Function that runs on each CPU after wakeup */
static void forced_wakeup_work_func(void *info)
{
	trace_cpuidle_debugfs_print("Scheduled task after CPU_SUSPEND\n");
}

/* Function that runs on each CPU as part of the SMP interrupt call */
/* This will call into the workqueue functionality and schedule the forced_idle_work_func */
static void enter_work_func(void *info)
{
	struct coordinated_sleep_struct *this_cpu_sleep = this_cpu_ptr(&enter_idle_work);

	queue_work_on(smp_processor_id(), system_highpri_wq, &(this_cpu_sleep->work));
}

/* Function that runs on each CPU as an SMP interrupt call */
/* This will update the per_cpu sleep_details */
static void update_this_cpu_sleep_target(void *info)
{
	struct coordinated_sleep_struct *this_cpu_sleep = this_cpu_ptr(&enter_idle_work);
	struct coordinated_sleep_struct *sleep_details = (struct coordinated_sleep_struct *) info;

	/* Params are passed to forced_idle_entry func */
	this_cpu_sleep->duration_ns = sleep_details->duration_ns;
	this_cpu_sleep->exit_latency_ns = sleep_details->exit_latency_ns;
}

/* Function that runs on ipi_src_cpu to coordinate entry into forced idle */
/* Optionally: then coordinate a synchronized exit out of idle */
static void coordinated_forced_idle_work_func(struct work_struct *work)
{
	struct coordinated_sleep_struct *sleep_details =
		container_of(work, struct coordinated_sleep_struct, work);
	struct cpumask combined_mask;

	cpumask_or(&combined_mask, &sleep_dest_a, &sleep_dest_b);

	/* Copy a/b parameters into a & b respectively */
	sleep_details->duration_ns = sleep_residency_ns_a;
	smp_call_function_many(&sleep_dest_a, update_this_cpu_sleep_target,
		sleep_details, true);
	sleep_details->duration_ns = sleep_residency_ns_b;
	smp_call_function_many(&sleep_dest_b, update_this_cpu_sleep_target,
		sleep_details, true);

	/* Call into sleep-entry */
	smp_call_function_many(&combined_mask, enter_work_func, NULL, true);

	if (sleep_details->do_coordinated_wakeup) {
		/* Assume that the tasks will be scheduled */
		/* Delay for roughly 1/2 of the target residency period */
		/* We will use ndelay to avoid yielding the CPU */
		ndelay(sleep_residency_ns_a / 2);

		trace_cpuidle_debugfs_print("Triggering wake IPI\n");
		smp_call_function_many(&sleep_dest_a, forced_wakeup_work_func, NULL, true);

		trace_cpuidle_debugfs_print("Yielding ipi_src_cpu\n");
	}
}

static int min_residency_read(void *data, u64 *val)
{
	struct cpuidle_state *idle_state = (struct cpuidle_state *) data;

	*val = idle_state->target_residency;
	return 0;
}

static int min_residency_write(void *data, u64 val)
{
	struct cpuidle_state *idle_state = (struct cpuidle_state *) data;

	idle_state->target_residency = min_t(u64, val, S64_MAX/1000);
	idle_state->target_residency_ns = min_t(u64, val*1000, S64_MAX);
	return 0;
}

static int forced_idle_write(void *data, u64 val)
{
	struct cpuidle_state *idle_state = (struct cpuidle_state *) data;
	int ret = 0;
	u64 duration_ns = US_TO_NS(val);

	forced_idle_entry(duration_ns, (u64) (idle_state->exit_latency_ns));

	return ret;
}

/* Shared function to sanity-check cpu-masks and queue up given work on src_cpu */
/* Importantly, this can run on ANY core. But it will coordinate work to be run */
/* by the ipi_src_cpu on the various ipi_dest_cpus. */
static int coordinated_sleep_setup_and_queue(void *data, u64 val,
	struct coordinated_sleep_struct *idle_params)
{
	struct cpuidle_state *idle_state = (struct cpuidle_state *) data;
	int ret = 0;

	idle_params->exit_latency_ns = (u64) (idle_state->exit_latency_ns);

	if (cpumask_empty(&sleep_dest_a) && cpumask_empty(&sleep_dest_b)) {
		pr_info("Coordinated Wake Test: both cpumasks are empty\n");
		ret = -EINVAL;
		goto out;
	}

	queue_work_on(ipi_src_cpu, system_highpri_wq, &(idle_params->work));

	if (!flush_work(&(idle_params->work))) {
		pr_info("Coordinated Wake Test: test did not finish\n");
		ret = -EINVAL;
		goto out;
	}
out:
	return ret;
}

static int coordinated_forced_idle_write(void *data, u64 val)
{
	coordination_params.do_coordinated_wakeup = false;
	return coordinated_sleep_setup_and_queue(data, val, &coordination_params);
}

static int ipi_wake_coordinated_forced_idle_write(void *data, u64 val)
{
	coordination_params.do_coordinated_wakeup = true;
	return coordinated_sleep_setup_and_queue(data, val, &coordination_params);
}

/* Takes in userspace data & sets/unsets cpumask accordingly */
static ssize_t parse_and_set_user_cpumask(struct file *file, const char __user *buf,
	size_t count, loff_t *pos, bool set)
{
	ssize_t err;
	struct cpumask new_value;
	struct cpumask *oldmask = (struct cpumask *) file->private_data;

	err = cpumask_parselist_user(buf, count, &new_value);
	if (err == 0) {
		if (set == true)
			cpumask_or(oldmask, oldmask, &new_value);
		else
			cpumask_andnot(oldmask, oldmask, &new_value);
		err = count;
	}

	return err;
}

static ssize_t set_ipi_dest_cpumask(struct file *file, const char __user *buf,
	size_t count, loff_t *pos)
{
	return parse_and_set_user_cpumask(file, buf, count, pos, true);
}

static ssize_t clear_ipi_dest_cpu_mask(struct file *file, const char __user *buf,
	size_t count, loff_t *pos)
{
	return parse_and_set_user_cpumask(file, buf, count, pos, false);
}

static ssize_t dest_cpumask_read(struct file *file, char __user *buf,
	size_t count, loff_t *ppos)
{
	int len;
	char pbuf[1024] = { 0 };
	struct cpumask *mask = (struct cpumask *) file->private_data;

	len = snprintf(pbuf, 1024, "%*pbl\n", cpumask_pr_args(mask));

	return simple_read_from_buffer(buf, count, ppos, pbuf, len);
}

DEFINE_SIMPLE_ATTRIBUTE(idle_state_fops, NULL, forced_idle_write, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(coordinated_idle_state_fops, NULL, coordinated_forced_idle_write, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(ipi_wake_coordinated_idle_state_fops, NULL,
	ipi_wake_coordinated_forced_idle_write, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(min_residency_fops, min_residency_read, min_residency_write, "%llu\n");

static const struct file_operations set_ipi_dest_cpumask_fops = {
	.owner		= THIS_MODULE,
	.open		= simple_open,
	.read		= dest_cpumask_read,
	.write		= set_ipi_dest_cpumask,
	.llseek		= noop_llseek,
};

static const struct file_operations clear_ipi_dest_cpumask_fops = {
	.owner		= THIS_MODULE,
	.open		= simple_open,
	.read		= dest_cpumask_read,
	.write		= clear_ipi_dest_cpu_mask,
	.llseek		= noop_llseek,
};

static struct dentry *cpuidle_debugfs_node;

static int init_debugfs(void)
{
	int i;
	static struct dentry *coordinated_debugfs_node;
	/* coordinated_ (12) + state-name (up to 7) + \0 (1) */
	char coordinated_wake_file[20];
	/* ipi_wake_coordinated_ (27) + state-name (up to 7) + \0 (1) */
	char ipi_wake_file[35];
	/* min_residency_ (14) + state-name (up to 7) + \0 (1) */
	char min_residency_file[22];

	cpuidle_debugfs_node = debugfs_create_dir("cpuidle_debug", NULL);
	if (!cpuidle_debugfs_node)
		goto err_out;
	coordinated_debugfs_node = debugfs_create_dir("coordinated_cpuidle", cpuidle_debugfs_node);
	if (!coordinated_debugfs_node)
		goto err_out;

	debugfs_create_u64("coordinating_cpu", 0600, coordinated_debugfs_node, &ipi_src_cpu);
	debugfs_create_file("set_cpuidle_dest_cpumask_a", 0600, coordinated_debugfs_node,
		&sleep_dest_a, &set_ipi_dest_cpumask_fops);
	debugfs_create_file("clear_cpuidle_dest_cpumask_a", 0600, coordinated_debugfs_node,
		&sleep_dest_a, &clear_ipi_dest_cpumask_fops);
	debugfs_create_u64("cpuidle_residency_ns_a", 0600, coordinated_debugfs_node,
		&sleep_residency_ns_a);
	debugfs_create_file("set_cpuidle_dest_cpumask_b", 0600, coordinated_debugfs_node,
		&sleep_dest_b, &set_ipi_dest_cpumask_fops);
	debugfs_create_file("clear_cpuidle_dest_cpumask_b", 0600, coordinated_debugfs_node,
		&sleep_dest_b, &clear_ipi_dest_cpumask_fops);
	debugfs_create_u64("cpuidle_residency_ns_b", 0600, coordinated_debugfs_node,
		&sleep_residency_ns_b);

	/* Initialize per-state knobs */
	for (i = 0; i < drv->state_count; i++) {
		snprintf(coordinated_wake_file, 20, "coordinated_%s", drv->states[i].name);
		snprintf(ipi_wake_file, 35, "ipi_wake_coordinated_%s", drv->states[i].name);
		snprintf(min_residency_file, 22, "min_residency_%s", drv->states[i].name);
		debugfs_create_file(drv->states[i].name, 0200,
			cpuidle_debugfs_node, &(drv->states[i]), &idle_state_fops);
		debugfs_create_file(coordinated_wake_file, 0200,
			coordinated_debugfs_node, &(drv->states[i]), &coordinated_idle_state_fops);
		debugfs_create_file(ipi_wake_file, 0200,
			coordinated_debugfs_node, &(drv->states[i]),
			&ipi_wake_coordinated_idle_state_fops);
		debugfs_create_file(min_residency_file, 0600,
			cpuidle_debugfs_node, &(drv->states[i]), &min_residency_fops);
	}
	return 0;

err_out:
	pr_err("%s: Couldn't create debugfs node for cpuidle\n", __func__);
	debugfs_remove_recursive(cpuidle_debugfs_node);
	return -ENOMEM;
}

static int __init cpuidle_debugfs_probe(void)
{
	int cpu;
	drv = cpuidle_get_driver();

	/* Init the workqueue functions */
	INIT_WORK(&(coordination_params.work), coordinated_forced_idle_work_func);
	for_each_possible_cpu(cpu) {
		struct coordinated_sleep_struct *sleep_work = &per_cpu(enter_idle_work, cpu);

		INIT_WORK(&(sleep_work->work), forced_idle_work_func);
	}

	return init_debugfs();
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
