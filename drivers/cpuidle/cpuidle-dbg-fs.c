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

#include "cpuidle-priv.h"

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
static struct {
	u64 src_cpu;
	struct cpumask mask_A;
	struct cpumask mask_B;
	u64 residency_ns_A;
	u64 residency_ns_B;
	u64 exit_latency_ns;
} sleep_data;

#define US_TO_NS(x) (1000 * x)

static int cpuidle_fs_coordinated_forced_idle_write(void *data, u64 val)
{
	if (cpumask_empty(&sleep_data.mask_A) && cpumask_empty(&sleep_data.mask_B)) {
		pr_err("Coordinated Wake Test: both cpumasks are empty\n");
		return -EINVAL;
	}
	cpuidle_dbg_sleep_many(&sleep_data.mask_A, &sleep_data.mask_B,
		sleep_data.residency_ns_A, sleep_data.residency_ns_B,
		sleep_data.exit_latency_ns, sleep_data.src_cpu, false);
	return 0;
}

static int cpuidle_fs_ipi_wake_coordinated_forced_idle_write(void *data, u64 val)
{
	if (cpumask_empty(&sleep_data.mask_A) && cpumask_empty(&sleep_data.mask_B)) {
		pr_err("Coordinated Wake Test: both cpumasks are empty\n");
		return -EINVAL;
	}
	cpuidle_dbg_sleep_many(&sleep_data.mask_A, &sleep_data.mask_B,
		sleep_data.residency_ns_A, sleep_data.residency_ns_B,
		sleep_data.exit_latency_ns, sleep_data.src_cpu, true);
	return 0;
}

/* Takes in userspace data & sets/unsets cpumask accordingly */
static ssize_t cpuidle_fs_parse_and_set_user_cpumask(struct file *file, const char __user *buf,
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

static ssize_t cpuidle_fs_set_ipi_dest_cpumask(struct file *file, const char __user *buf,
	size_t count, loff_t *pos)
{
	return cpuidle_fs_parse_and_set_user_cpumask(file, buf, count, pos, true);
}

static ssize_t cpuidle_fs_clear_ipi_dest_cpu_mask(struct file *file, const char __user *buf,
	size_t count, loff_t *pos)
{
	return cpuidle_fs_parse_and_set_user_cpumask(file, buf, count, pos, false);
}

static ssize_t cpuidle_fs_dest_cpumask_read(struct file *file, char __user *buf,
	size_t count, loff_t *ppos)
{
	int len;
	char pbuf[1024] = { 0 };
	struct cpumask *mask = (struct cpumask *) file->private_data;

	len = snprintf(pbuf, 1024, "%*pbl\n", cpumask_pr_args(mask));

	return simple_read_from_buffer(buf, count, ppos, pbuf, len);
}

static struct cpuidle_state *cpuidle_fs_get_state(u32 cpu, const char *name)
{
	/**
	 * We should be using the per-cpu struct cpuidle_devices.
	 * But there are linking errors with it.
	 * So we are using a fake struct cpuidle_device,
	 * with only CPU set since no sanity checks are done in cpuidle_get_cpu_driver.
	 */
	struct cpuidle_device dev = { .cpu = cpu };
	struct cpuidle_driver *drv = cpuidle_get_cpu_driver(&dev);
	if (!drv) {
		pr_err("Failed to get cpuidle driver for cpu%u\n", cpu);
		return NULL;
	}

	/* Assumes the name is unique to the state, shared across all CPUs */
	for (int i = 0; i < drv->state_count; i++) {
		if (strcmp(drv->states[i].name, name) == 0)
			return &drv->states[i];
	}
	return NULL;
}

static int cpuidle_fs_min_residency_read(void *data, u64 *val)
{
	struct cpuidle_state *idle_state = (struct cpuidle_state *) data;

	*val = idle_state->target_residency;
	return 0;
}

static int cpuidle_fs_min_residency_write(void *data, u64 val)
{
	struct cpuidle_state *param_state = (struct cpuidle_state *) data;
	struct cpuidle_state *state;
	int cpu;

	/**
	 * We need to update this on ALL drivers. In PSCI-DT land, each
	 * CPU has its own driver-instance.
	 */
	for_each_possible_cpu(cpu) {
		state = cpuidle_fs_get_state(cpu, param_state->name);
		if (state) {
			state->target_residency = min_t(u64, val, S64_MAX/1000);
			state->target_residency_ns = min_t(u64, val*1000, S64_MAX);
		}
	}
	return 0;
}

static int cpuidle_fs_exit_latency_read(void *data, u64 *val)
{
	struct cpuidle_state *idle_state = (struct cpuidle_state *) data;

	*val = idle_state->exit_latency;
	return 0;
}

static int cpuidle_fs_exit_latency_write(void *data, u64 val)
{
	struct cpuidle_state *param_state = (struct cpuidle_state *) data;
	struct cpuidle_state *state;
	int cpu;

	/**
	 * We need to update this on ALL drivers. In PSCI-DT land, each
	 * CPU has its own driver-instance.
	 */
	for_each_possible_cpu(cpu) {
		state = cpuidle_fs_get_state(cpu, param_state->name);
		if (state) {
			state->exit_latency = min_t(u64, val, S64_MAX/1000);
			state->exit_latency_ns = min_t(u64, val*1000, S64_MAX);
		}
	}
	return 0;
}

static int cpuidle_fs_forced_idle_write(void *data, u64 val)
{
	struct cpuidle_state *idle_state = (struct cpuidle_state *) data;
	int ret = 0;
	u64 duration_ns = US_TO_NS(val);

	cpuidle_dbg_sleep(duration_ns, (u64) (idle_state->exit_latency_ns));

	return ret;
}

DEFINE_SIMPLE_ATTRIBUTE(idle_state_fops, NULL, cpuidle_fs_forced_idle_write, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(coordinated_idle_state_fops, NULL,
	cpuidle_fs_coordinated_forced_idle_write, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(ipi_wake_coordinated_idle_state_fops, NULL,
	cpuidle_fs_ipi_wake_coordinated_forced_idle_write, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(min_residency_fops, cpuidle_fs_min_residency_read,
	cpuidle_fs_min_residency_write, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(exit_latency_fops, cpuidle_fs_exit_latency_read,
	cpuidle_fs_exit_latency_write, "%llu\n");
static const struct file_operations set_ipi_dest_cpumask_fops = {
	.owner		= THIS_MODULE,
	.open		= simple_open,
	.read		= cpuidle_fs_dest_cpumask_read,
	.write		= cpuidle_fs_set_ipi_dest_cpumask,
	.llseek		= noop_llseek,
};

static const struct file_operations clear_ipi_dest_cpumask_fops = {
	.owner		= THIS_MODULE,
	.open		= simple_open,
	.read		= cpuidle_fs_dest_cpumask_read,
	.write		= cpuidle_fs_clear_ipi_dest_cpu_mask,
	.llseek		= noop_llseek,
};

static struct dentry *cpuidle_debugfs_node;

int cpuidle_fs_init(void)
{
	int i;
	struct cpuidle_driver *drv = cpuidle_get_driver();
	static struct dentry *coordinated_debugfs_node;
	/* coordinated_ (12) + state-name (up to 16) + \0 (1) */
	char coordinated_wake_file[29];
	/* ipi_wake_coordinated_ (27) + state-name (up to 16) + \0 (1) */
	char ipi_wake_file[44];
	/* min_residency_ (14) + state-name (up to 16) + \0 (1) */
	char min_residency_file[31];
	/* exit_latency_ (13) + state-name (up to 16) + \0 (1) */
	char exit_latency_file[30];

	cpuidle_debugfs_node = debugfs_create_dir("cpuidle_debug", NULL);
	if (!cpuidle_debugfs_node)
		goto err_out;
	coordinated_debugfs_node = debugfs_create_dir("coordinated_cpuidle", cpuidle_debugfs_node);
	if (!coordinated_debugfs_node)
		goto err_out;

	debugfs_create_u64("coordinating_cpu", 0600, coordinated_debugfs_node, &sleep_data.src_cpu);
	debugfs_create_file("set_cpuidle_dest_cpumask_a", 0600, coordinated_debugfs_node,
		&sleep_data.mask_A, &set_ipi_dest_cpumask_fops);
	debugfs_create_file("clear_cpuidle_dest_cpumask_a", 0600, coordinated_debugfs_node,
		&sleep_data.mask_A, &clear_ipi_dest_cpumask_fops);
	debugfs_create_u64("cpuidle_residency_ns_a", 0600, coordinated_debugfs_node,
		&sleep_data.residency_ns_A);
	debugfs_create_file("set_cpuidle_dest_cpumask_b", 0600, coordinated_debugfs_node,
		&sleep_data.mask_B, &set_ipi_dest_cpumask_fops);
	debugfs_create_file("clear_cpuidle_dest_cpumask_b", 0600, coordinated_debugfs_node,
		&sleep_data.mask_B, &clear_ipi_dest_cpumask_fops);
	debugfs_create_u64("cpuidle_residency_ns_b", 0600, coordinated_debugfs_node,
		&sleep_data.residency_ns_B);

	/* Initialize per-state knobs */
	for (i = 0; i < drv->state_count; i++) {
		snprintf(coordinated_wake_file, 29, "coordinated_%s", drv->states[i].name);
		snprintf(ipi_wake_file, 44, "ipi_wake_coordinated_%s", drv->states[i].name);
		snprintf(min_residency_file, 31, "min_residency_%s", drv->states[i].name);
		snprintf(exit_latency_file, 30, "exit_latency_%s", drv->states[i].name);
		debugfs_create_file(drv->states[i].name, 0200,
			cpuidle_debugfs_node, &(drv->states[i]), &idle_state_fops);
		debugfs_create_file(coordinated_wake_file, 0200,
			coordinated_debugfs_node, &(drv->states[i]), &coordinated_idle_state_fops);
		debugfs_create_file(ipi_wake_file, 0200,
			coordinated_debugfs_node, &(drv->states[i]),
			&ipi_wake_coordinated_idle_state_fops);
		debugfs_create_file(min_residency_file, 0600,
			cpuidle_debugfs_node, &(drv->states[i]), &min_residency_fops);
		debugfs_create_file(exit_latency_file, 0600,
			cpuidle_debugfs_node, &(drv->states[i]), &exit_latency_fops);
	}
	return 0;

err_out:
	pr_err("%s: Couldn't create debugfs node for cpuidle\n", __func__);
	debugfs_remove_recursive(cpuidle_debugfs_node);
	return -ENOMEM;
}

void cpuidle_fs_teardown(void)
{
	debugfs_remove_recursive(cpuidle_debugfs_node);
}
