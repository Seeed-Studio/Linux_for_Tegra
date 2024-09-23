// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All Rights Reserved.
 *
 * Module to disable Clock-Gating on WFI for T264.
 *
 */
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/interrupt.h>
#include <linux/debugfs.h>
#include <linux/cpu.h>
#include <linux/cpuidle.h>

#define CCPLEX_SOCKET0_MMI0_BASE 0x8130000000
#define CCPLEX_SOCKET0_MMIO_SIZE 0x3fffff

/* Two cores to a cluster */
#define T264_CORE_OFFSET(cl) ((cl % 2) * 0x8)
/* Two clusters to a switch */
#define T264_CLUSTER_OFFSET(cl) (((cl % 4) / 2) * 0x1000)
/* Four switches in the system */
#define T264_SWITCH_OFFSET(cl) ((cl / 4) * 0x100000)
/* Determine the per-core CG Disable Register */
/* coreN_archcg_clken */
#define T264_CG_DISABLE_REG(base, cl) \
	(base + T264_SWITCH_OFFSET(cl) + T264_CLUSTER_OFFSET(cl) \
	+ T264_CORE_OFFSET(cl) + 0x218)

static void __iomem *ccplex_mmio_base;

static void __iomem *get_t264_cg_disable_reg(void __iomem *base, int core)
{
	return T264_CG_DISABLE_REG(base, core);
}

static int cg_disable_write(void *data, u64 val)
{
	void __iomem *reg = (void __iomem *) data;

	if (val == 0) {
		writel(0x00, reg);
	} else {
		writel(0x3, reg);
	}

	return 0;
}

static int cg_disable_read(void *data, u64 *val)
{
	void __iomem *reg = (void __iomem *) data;
	u64 regval;

	regval = readl(reg);

	if (regval == 0) {
		*val = 0;
	} else {
		*val = 1;
	}

	return 0;
}


DEFINE_SIMPLE_ATTRIBUTE(idle_state_fops, cg_disable_read, cg_disable_write, "%llu\n");

static struct dentry *cpuidle_cg_disable_node;

static int init_debugfs(void)
{
	int cpu;

	cpuidle_cg_disable_node = debugfs_create_dir("cpuidle_cg_disable", NULL);
	if (!cpuidle_cg_disable_node)
		goto err_out;

	for_each_possible_cpu(cpu) {
		/* Size = 7 = cpu (3) + %d (3 from max 1000 cores) + \n (1) */
		char str_buf[7];
		struct dentry *debugfs_dir;

		snprintf(str_buf, 7, "cpu%d", cpu);
		debugfs_dir = debugfs_create_dir(str_buf, cpuidle_cg_disable_node);
		if (!debugfs_dir) {
			goto err_out;
		}
		debugfs_create_file("disable_cg", 0600,
			debugfs_dir, (void *) get_t264_cg_disable_reg(ccplex_mmio_base, cpu),
			&idle_state_fops);
	}

	return 0;

err_out:
	pr_err("%s: Couldn't create debugfs node for cpuidle\n", __func__);
	debugfs_remove_recursive(cpuidle_cg_disable_node);
	return -ENOMEM;
}

static int __init cpuidle_cg_disable_probe(void)
{
	int error;

	/* Set up the IO Mapping */
	ccplex_mmio_base = ioremap(CCPLEX_SOCKET0_MMI0_BASE, CCPLEX_SOCKET0_MMIO_SIZE);
	if (IS_ERR(ccplex_mmio_base)) {
		error = PTR_ERR(ccplex_mmio_base);
		goto mapping_err_out;
	}
	error = init_debugfs();
	if (error) {
		goto debugfs_err_out;
	}
	return 0;

mapping_err_out:
	pr_err("%s: Couldn't map the CPU's mmcrab registers\n", __func__);

debugfs_err_out:
	return error;
}

static void __exit cpuidle_cg_disable_remove(void)
{
	debugfs_remove_recursive(cpuidle_cg_disable_node);
	iounmap(ccplex_mmio_base);
}

module_init(cpuidle_cg_disable_probe);
module_exit(cpuidle_cg_disable_remove);

MODULE_AUTHOR("Ishan Shah <ishah@nvidia.com>");
MODULE_DESCRIPTION("clock gating disable driver");
MODULE_LICENSE("GPL");
