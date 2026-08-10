/*
 * Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/arm-smccc.h>
#include <linux/cpumask.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <uapi/linux/psci.h>

#include <misc/nv_ist.h>

/* RIST START SMC */
/* https://confluence.nvidia.com/display/TFA/Central+Registry+for+SMCs */
#define NV_IST_FN_RIST_START		0xC200FF0A

struct ist_smc_vars {
	bool enable;
	u32 tid;
	u32 smc_id;
};

static struct ist_smc_vars *cpu_settings;
static struct dentry *dbgfs_root;

bool is_ist_enabled(unsigned int cpu)
{
	return cpu_settings != NULL && cpu_settings[cpu].enable;
}

void cpu_enter_ist(unsigned int cpu)
{
	struct arm_smccc_res res;

	if ((cpu_settings != NULL && cpu_settings[cpu].enable)) {
		pr_debug("CPU %x is entering SMC %x\n", cpu, cpu_settings[cpu].smc_id);
		arm_smccc_smc(cpu_settings[cpu].smc_id,
			cpu_settings[cpu].tid,
			0, 0, 0, 0, 0, 0, &res);
	}
}

static int create_ist_debugfs_entries(void)
{
	char subdir_name[6]; /* cpu + XX + \n */
	struct dentry *subdir;
	int cpu;

	dbgfs_root = debugfs_create_dir("nv_ist", NULL);
	if (!dbgfs_root) {
		return -ENOENT;
	}

	for (cpu = cpumask_first(cpu_possible_mask);
		cpu < nr_cpu_ids;
		cpu = cpumask_next(cpu, cpu_possible_mask)) {
		snprintf(subdir_name, 6, "cpu%d", cpu);
		subdir = debugfs_create_dir(subdir_name, dbgfs_root);

		if (subdir == NULL)
			goto err;

		debugfs_create_bool("enable", 0644,
			subdir,	&(cpu_settings[cpu].enable));

		debugfs_create_u32("test_id", 0644,
			subdir,	&(cpu_settings[cpu].tid));
	}

	return 0;
err:
	debugfs_remove_recursive(dbgfs_root);
	return -ENOENT;
}

static int tegra_ist_probe(struct platform_device *pdev)
{
	size_t i;
	int err = 0;

	/* Zero-initialized */
	cpu_settings = devm_kcalloc(&pdev->dev, nr_cpu_ids,
		sizeof(struct ist_smc_vars), GFP_KERNEL);

	/* Setting the PSCI-ID to RIST_START SMC */
	for (i = 0; i < nr_cpu_ids; i++) {
		cpu_settings[i].enable = false;
		cpu_settings[i].tid = 0U;
		cpu_settings[i].smc_id = NV_IST_FN_RIST_START;
	}
	/* Create debugfs */
	err = create_ist_debugfs_entries();

	if (err)
		devm_kfree(&pdev->dev, cpu_settings);

	return err;
}

static int tegra_ist_remove(struct platform_device *pdev)
{
	debugfs_remove_recursive(dbgfs_root);
	return 0;
}

static const struct of_device_id tegra_ist_of_match[] = {
	{ .compatible = "nvidia,tegra26x-ist" },
	{ }, /* Terminal */
};

static struct platform_driver nv_ist_driver = {
	.probe = tegra_ist_probe,
	.remove = tegra_ist_remove,
	.driver = {
		.name = "nv_ist",
		.of_match_table = tegra_ist_of_match,
	},
};
module_platform_driver(nv_ist_driver);

MODULE_AUTHOR("Ishan Shah <ishah@nvidia.com>");
MODULE_DESCRIPTION("NV IST Driver");
MODULE_LICENSE("GPL v2");
