// SPDX-License-Identifier: GPL-2.0-only
/*
 * Tegra host1x activity monitor interfaces
 *
 * Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/host1x-next.h>

#include "dev.h"
#include "actmon.h"
#include "hw/actmon.h"

static void actmon_writel(struct host1x_actmon *actmon, u32 val, u32 offset)
{
	writel(val, actmon->regs+offset);
}

static u32 actmon_readl(struct host1x_actmon *actmon, u32 offset)
{
	return readl(actmon->regs+offset);
}

static void actmon_module_writel(struct host1x_actmon_module *module, u32 val, u32 offset)
{
	writel(val, module->regs+offset);
}

static u32 actmon_module_readl(struct host1x_actmon_module *module, u32 offset)
{
	return readl(module->regs+offset);
}

static void host1x_actmon_update_sample_period(struct host1x_actmon *actmon)
{
	unsigned long actmon_mhz;
	u32 actmon_clks_per_sample, sample_period, val = 0;

	actmon_mhz = actmon->rate / 1000000;
	actmon_clks_per_sample = actmon_mhz * actmon->usecs_per_sample;

	val |= HOST1X_ACTMON_CTRL_SOURCE(2);

	if (actmon_clks_per_sample > 65536) {
		val |= HOST1X_ACTMON_CTRL_SAMPLE_TICK(1);
		sample_period = actmon_clks_per_sample / 65536;
	} else {
		val &= ~HOST1X_ACTMON_CTRL_SAMPLE_TICK(1);
		sample_period = actmon_clks_per_sample / 256;
	}

	val &= ~HOST1X_ACTMON_CTRL_SAMPLE_PERIOD_MASK;
	val |= HOST1X_ACTMON_CTRL_SAMPLE_PERIOD(sample_period);
	actmon_writel(actmon, val, HOST1X_ACTMON_CTRL_REG);
}

static int host1x_actmon_sample_period_get(void *data, u64 *val)
{
	struct host1x_actmon *actmon = (struct host1x_actmon *)data;

	*val = (u64) actmon->usecs_per_sample;

	return 0;
}

static int host1x_actmon_sample_period_set(void *data, u64 val)
{
	struct host1x_actmon *actmon = (struct host1x_actmon *)data;

	actmon->usecs_per_sample = (u32)val;
	host1x_actmon_update_sample_period(actmon);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(host1x_actmon_sample_period_fops,
		host1x_actmon_sample_period_get,
		host1x_actmon_sample_period_set,
		"%lld\n");

static void host1x_actmon_debug_init(struct host1x_actmon *actmon, const char *name)
{
	struct host1x *host = dev_get_drvdata(actmon->client->host->parent);
	struct dentry *debugfs = host->actmon_debugfs;

	if (!debugfs)
		return;

	actmon->debugfs = debugfs_create_dir(name, debugfs);

	/* R/W files */
	debugfs_create_file("sample_period", 0644, actmon->debugfs, actmon,
			&host1x_actmon_sample_period_fops);
}

static int host1x_actmon_module_k_get(void *data, u64 *val)
{
	struct host1x_actmon_module *module = (struct host1x_actmon_module *)data;

	*val = (u64) module->k;

	return 0;
}

static int host1x_actmon_module_k_set(void *data, u64 val)
{
	struct host1x_actmon_module *module = (struct host1x_actmon_module *)data;
	u32 val32;

	module->k = (u32)val;

	val32 = actmon_module_readl(module, HOST1X_ACTMON_MODULE_CTRL_REG);
	val32 &= ~HOST1X_ACTMON_MODULE_CTRL_K_VAL_MASK;
	val32 |= HOST1X_ACTMON_MODULE_CTRL_K_VAL(module->k);
	actmon_module_writel(module, val32, HOST1X_ACTMON_MODULE_CTRL_REG);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(host1x_actmon_module_k_fops,
		host1x_actmon_module_k_get,
		host1x_actmon_module_k_set,
		"%lld\n");

static int host1x_actmon_module_consec_upper_num_get(void *data, u64 *val)
{
	struct host1x_actmon_module *module = (struct host1x_actmon_module *)data;

	*val = (u64) module->consec_upper_num;

	return 0;
}

static int host1x_actmon_module_consec_upper_num_set(void *data, u64 val)
{
	struct host1x_actmon_module *module = (struct host1x_actmon_module *)data;
	u32 val32;

	module->consec_upper_num = (u32)val;

	val32 = actmon_module_readl(module, HOST1X_ACTMON_MODULE_CTRL_REG);
	val32 &= ~HOST1X_ACTMON_MODULE_CTRL_CONSEC_UPPER_NUM_MASK;
	val32 |= HOST1X_ACTMON_MODULE_CTRL_CONSEC_UPPER_NUM(module->consec_upper_num);
	actmon_module_writel(module, val32, HOST1X_ACTMON_MODULE_CTRL_REG);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(host1x_actmon_module_consec_upper_num_fops,
		host1x_actmon_module_consec_upper_num_get,
		host1x_actmon_module_consec_upper_num_set,
		"%lld\n");

static int host1x_actmon_module_consec_lower_num_get(void *data, u64 *val)
{
	struct host1x_actmon_module *module = (struct host1x_actmon_module *)data;

	*val = (u64) module->consec_lower_num;

	return 0;
}

static int host1x_actmon_module_consec_lower_num_set(void *data, u64 val)
{
	struct host1x_actmon_module *module = (struct host1x_actmon_module *)data;
	u32 val32;

	module->consec_lower_num = (u32)val;

	val32 = actmon_module_readl(module, HOST1X_ACTMON_MODULE_CTRL_REG);
	val32 &= ~HOST1X_ACTMON_MODULE_CTRL_CONSEC_LOWER_NUM_MASK;
	val32 |= HOST1X_ACTMON_MODULE_CTRL_CONSEC_LOWER_NUM(module->consec_lower_num);
	actmon_module_writel(module, val32, HOST1X_ACTMON_MODULE_CTRL_REG);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(host1x_actmon_module_consec_lower_num_fops,
		host1x_actmon_module_consec_lower_num_get,
		host1x_actmon_module_consec_lower_num_set,
		"%lld\n");

static int host1x_actmon_module_avg_norm_get(void *data, u64 *val)
{
	struct host1x_actmon_module *module = (struct host1x_actmon_module *)data;
	struct host1x_actmon *actmon = module->actmon;
	struct host1x_client *client = actmon->client;
	unsigned long client_freq;
	u32 active_clks, client_clks;

	if (!client->ops->get_rate)
		return -ENOTSUPP;

	active_clks = actmon_module_readl(module, HOST1X_ACTMON_MODULE_AVG_COUNT_REG);

	client_freq = client->ops->get_rate(client);
	client_clks = ((client_freq / 1000) * actmon->usecs_per_sample) / 1000;

	*val = (u64) (active_clks * 1000) / client_clks;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(host1x_actmon_module_avg_norm_fops,
		host1x_actmon_module_avg_norm_get, NULL,
		"%lld\n");

static void host1x_actmon_module_debug_init(struct host1x_actmon_module *module)
{
	struct dentry *debugfs = module->actmon->debugfs;
	char dirname[8];

	if (!debugfs)
		return;

	snprintf(dirname, sizeof(dirname), "module%d", module->type);
	module->debugfs = debugfs_create_dir(dirname, debugfs);

	/* R/W files */
	debugfs_create_file("k", 0644, module->debugfs, module,
			&host1x_actmon_module_k_fops);
	debugfs_create_file("consec_upper_num", 0644, module->debugfs, module,
			&host1x_actmon_module_consec_upper_num_fops);
	debugfs_create_file("consec_lower_num", 0644, module->debugfs, module,
			&host1x_actmon_module_consec_lower_num_fops);

	/* R files */
	debugfs_create_file("usage", 0444, module->debugfs, module,
			&host1x_actmon_module_avg_norm_fops);
}

static void host1x_actmon_init(struct host1x_actmon *actmon)
{
	u32 val;

	/* Global control register */
	host1x_actmon_update_sample_period(actmon);

	/* Global interrupt enable register */
	val = (1 << actmon->num_modules) - 1;
	actmon_writel(actmon, val, HOST1X_ACTMON_INTR_ENB_REG);
}

static void host1x_actmon_deinit(struct host1x_actmon *actmon)
{
	actmon_writel(actmon, 0, HOST1X_ACTMON_CTRL_REG);
	actmon_writel(actmon, 0, HOST1X_ACTMON_INTR_ENB_REG);
}

static void host1x_actmon_module_init(struct host1x_actmon_module *module)
{
	/* Local control register */
	actmon_module_writel(module,
		HOST1X_ACTMON_MODULE_CTRL_ACTMON_ENB(0) |
		HOST1X_ACTMON_MODULE_CTRL_ENB_PERIODIC(1) |
		HOST1X_ACTMON_MODULE_CTRL_K_VAL(module->k) |
		HOST1X_ACTMON_MODULE_CTRL_CONSEC_UPPER_NUM(module->consec_upper_num) |
		HOST1X_ACTMON_MODULE_CTRL_CONSEC_LOWER_NUM(module->consec_lower_num),
		HOST1X_ACTMON_MODULE_CTRL_REG);

	/* Interrupt enable register (disable interrupts by default) */
	actmon_module_writel(module, 0, HOST1X_ACTMON_MODULE_INTR_ENB_REG);

	/* Interrupt status register */
	actmon_module_writel(module, ~0, HOST1X_ACTMON_MODULE_INTR_STATUS_REG);

	/* Consecutive watermark registers */
	actmon_module_writel(module, ~0, HOST1X_ACTMON_MODULE_UPPER_WMARK_REG);
	actmon_module_writel(module, 0, HOST1X_ACTMON_MODULE_LOWER_WMARK_REG);

	/* Moving-average watermark registers */
	actmon_module_writel(module, ~0, HOST1X_ACTMON_MODULE_AVG_UPPER_WMARK_REG);
	actmon_module_writel(module, 0, HOST1X_ACTMON_MODULE_AVG_LOWER_WMARK_REG);

	/* Init average value register */
	actmon_module_writel(module, 0, HOST1X_ACTMON_MODULE_INIT_AVG_REG);
}

static void host1x_actmon_module_deinit(struct host1x_actmon_module *module)
{
	actmon_module_writel(module, 0, HOST1X_ACTMON_MODULE_CTRL_REG);
	actmon_module_writel(module, 0, HOST1X_ACTMON_MODULE_INTR_ENB_REG);
	actmon_module_writel(module, ~0, HOST1X_ACTMON_MODULE_INTR_STATUS_REG);
	actmon_module_writel(module, 0, HOST1X_ACTMON_MODULE_UPPER_WMARK_REG);
	actmon_module_writel(module, 0, HOST1X_ACTMON_MODULE_LOWER_WMARK_REG);
	actmon_module_writel(module, 0, HOST1X_ACTMON_MODULE_AVG_UPPER_WMARK_REG);
	actmon_module_writel(module, 0, HOST1X_ACTMON_MODULE_AVG_LOWER_WMARK_REG);
	actmon_module_writel(module, 0, HOST1X_ACTMON_MODULE_INIT_AVG_REG);
	actmon_module_writel(module, 0, HOST1X_ACTMON_MODULE_COUNT_WEIGHT_REG);
}

void host1x_actmon_handle_interrupt(struct host1x *host, int classid)
{
	unsigned long actmon_status, module_status;
	struct host1x_actmon_module *module;
	struct host1x_actmon *actmon;
	struct host1x_client *client;

	list_for_each_entry(actmon, &host->actmons, list) {
		if (actmon->client->class == classid)
			break;
	}

	client = actmon->client;
	module = &actmon->modules[HOST1X_ACTMON_MODULE_ACTIVE];

	actmon_status = actmon_readl(actmon, HOST1X_ACTMON_INTR_STATUS_REG);
	module_status = actmon_module_readl(module, HOST1X_ACTMON_MODULE_INTR_STATUS_REG);

	/* Trigger DFS if client supports it */
	if (!client->ops->actmon_event)
		;
	else if (module_status & HOST1X_ACTMON_MODULE_INTR_CONSEC_WMARK_ABOVE)
		client->ops->actmon_event(client, HOST1X_ACTMON_CONSEC_WMARK_ABOVE);
	else if (module_status & HOST1X_ACTMON_MODULE_INTR_CONSEC_WMARK_BELOW)
		client->ops->actmon_event(client, HOST1X_ACTMON_CONSEC_WMARK_BELOW);
	else if (module_status & HOST1X_ACTMON_MODULE_INTR_AVG_WMARK_ABOVE)
		client->ops->actmon_event(client, HOST1X_ACTMON_AVG_WMARK_ABOVE);
	else if (module_status & HOST1X_ACTMON_MODULE_INTR_AVG_WMARK_BELOW)
		client->ops->actmon_event(client, HOST1X_ACTMON_AVG_WMARK_BELOW);

	actmon_module_writel(module, module_status, HOST1X_ACTMON_MODULE_INTR_STATUS_REG);
	actmon_writel(actmon, actmon_status, HOST1X_ACTMON_INTR_STATUS_REG);
}

int host1x_actmon_register(struct host1x_client *client)
{
	struct host1x *host = dev_get_drvdata(client->host->parent);
	const struct host1x_info *info = host->info;
	const struct host1x_actmon_entry *entry = NULL;
	struct host1x_actmon_module *module;
	struct host1x_actmon *actmon;
	unsigned long flags;
	int i;

	if (!host->actmon_regs || !host->actmon_clk)
		return -ENODEV;

	for (i = 0; i < info->num_actmon_entries; i++) {
		if (info->actmon_table[i].classid == client->class)
			entry = &info->actmon_table[i];
	}
	if (!entry)
		return -ENODEV;

	actmon = devm_kzalloc(client->dev, sizeof(*actmon), GFP_KERNEL);
	if (!actmon)
		return -ENOMEM;

	INIT_LIST_HEAD(&actmon->list);

	spin_lock_irqsave(&host->actmons_lock, flags);
	list_add_tail(&actmon->list, &host->actmons);
	spin_unlock_irqrestore(&host->actmons_lock, flags);

	actmon->client = client;
	actmon->rate = clk_get_rate(host->actmon_clk);
	actmon->regs = host->actmon_regs + entry->offset;
	actmon->irq = entry->irq;
	actmon->num_modules = entry->num_modules;
	actmon->usecs_per_sample = 1500;

	/* Configure actmon registers */
	host1x_actmon_init(actmon);

	/* Create debugfs for the actmon */
	host1x_actmon_debug_init(actmon, entry->name);

	/* Configure actmon module registers */
	for (i = 0; i < actmon->num_modules; i++) {
		module = &actmon->modules[i];
		module->actmon = actmon;
		module->type = i;
		module->regs = actmon->regs + (i * HOST1X_ACTMON_MODULE_OFFSET);

		module->k = 6;
		module->consec_upper_num = 7;
		module->consec_lower_num = 7;
		host1x_actmon_module_init(module);

		/* Create debugfs for the actmon module */
		host1x_actmon_module_debug_init(module);
	}

	client->actmon = actmon;

	return 0;
}
EXPORT_SYMBOL(host1x_actmon_register);

void host1x_actmon_unregister(struct host1x_client *client)
{
	struct host1x_actmon_module *module;
	struct host1x *host = dev_get_drvdata(client->host->parent);
	struct host1x_actmon *actmon = client->actmon;
	unsigned long flags;
	int i;

	if (!host->actmon_regs || !host->actmon_clk)
		return;

	if (!actmon)
		return;

	for (i = 0; i < actmon->num_modules; i++) {
		module = &actmon->modules[i];
		host1x_actmon_module_deinit(module);
		debugfs_remove_recursive(module->debugfs);
	}

	debugfs_remove_recursive(actmon->debugfs);

	host1x_actmon_deinit(actmon);

	spin_lock_irqsave(&host->actmons_lock, flags);
	list_del(&actmon->list);
	spin_unlock_irqrestore(&host->actmons_lock, flags);
}
EXPORT_SYMBOL(host1x_actmon_unregister);

void host1x_actmon_enable(struct host1x_client *client)
{
	struct host1x_actmon *actmon = client->actmon;
	struct host1x_actmon_module *module;
	int i;

	if (!actmon)
		return;

	for (i = 0; i < actmon->num_modules; i++) {
		module = &actmon->modules[i];
		actmon_module_writel(module,
			actmon_module_readl(module, HOST1X_ACTMON_MODULE_CTRL_REG) |
			HOST1X_ACTMON_MODULE_CTRL_ACTMON_ENB(1),
			HOST1X_ACTMON_MODULE_CTRL_REG);
	}
}
EXPORT_SYMBOL(host1x_actmon_enable);

void host1x_actmon_disable(struct host1x_client *client)
{
	struct host1x_actmon *actmon = client->actmon;
	struct host1x_actmon_module *module;
	int i;

	if (!actmon)
		return;

	for (i = 0; i < actmon->num_modules; i++) {
		module = &actmon->modules[i];
		actmon_module_writel(module,
			actmon_module_readl(module, HOST1X_ACTMON_MODULE_CTRL_REG) &
			~HOST1X_ACTMON_MODULE_CTRL_ACTMON_ENB(1),
			HOST1X_ACTMON_MODULE_CTRL_REG);
	}
}
EXPORT_SYMBOL(host1x_actmon_disable);

void host1x_actmon_update_client_rate(struct host1x_client *client,
				      unsigned long rate,
				      u32 *weight)
{
	struct host1x_actmon *actmon = client->actmon;
	struct host1x_actmon_module *module;
	u32 val;
	int i;

	if (!actmon) {
		*weight = 0;
		return;
	}

	val = (rate / actmon->rate) << 2;

	for (i = 0; i < actmon->num_modules; i++) {
		module = &actmon->modules[i];
		actmon_module_writel(module, val, HOST1X_ACTMON_MODULE_COUNT_WEIGHT_REG);
	}

	*weight = val;
}
EXPORT_SYMBOL(host1x_actmon_update_client_rate);

void host1x_actmon_read_active_norm(struct host1x_client *client, unsigned long *usage)
{
	struct host1x_actmon *actmon = client->actmon;
	struct host1x_actmon_module *module;
	u64 val;

	if (!actmon || !actmon->num_modules) {
		*usage = 0;
		return;
	}

	module = &actmon->modules[HOST1X_ACTMON_MODULE_ACTIVE];
	host1x_actmon_module_avg_norm_get(module, &val);

	*usage = (unsigned long)val;
}
EXPORT_SYMBOL(host1x_actmon_read_active_norm);

int host1x_actmon_read_avg_count(struct host1x_client *client)
{
	struct host1x *host = dev_get_drvdata(client->host->parent);
	unsigned int offset;

	if (!host->actmon_regs)
		return -ENODEV;

	/* FIXME: Only T234 supported */

	switch (client->class) {
	case HOST1X_CLASS_NVENC:
		offset = 0x0;
		break;
	case HOST1X_CLASS_VIC:
		offset = 0x10000;
		break;
	case HOST1X_CLASS_NVDEC:
		offset = 0x20000;
		break;
	default:
		return -EINVAL;
	}

	return readl(host->actmon_regs + offset + 0xa4);
}
EXPORT_SYMBOL(host1x_actmon_read_avg_count);

void host1x_actmon_update_active_wmark(struct host1x_client *client,
				       u32 avg_upper_wmark,
				       u32 avg_lower_wmark,
				       u32 consec_upper_wmark,
				       u32 consec_lower_wmark,
				       bool upper_wmark_enabled,
				       bool lower_wmark_enabled)
{
	struct host1x_actmon *actmon = client->actmon;
	struct host1x_actmon_module *module;
	u32 val = 0;

	if (!actmon || !actmon->num_modules)
		return;

	module = &actmon->modules[HOST1X_ACTMON_MODULE_ACTIVE];

	/* Update watermark thresholds */
	actmon_module_writel(module,
			     avg_upper_wmark / 1000 * actmon->usecs_per_sample / 1000,
			     HOST1X_ACTMON_MODULE_AVG_UPPER_WMARK_REG);
	actmon_module_writel(module,
			     avg_lower_wmark / 1000 * actmon->usecs_per_sample / 1000,
			     HOST1X_ACTMON_MODULE_AVG_LOWER_WMARK_REG);
	actmon_module_writel(module,
			     consec_upper_wmark / 1000 * actmon->usecs_per_sample / 1000,
			     HOST1X_ACTMON_MODULE_UPPER_WMARK_REG);
	actmon_module_writel(module,
			     consec_lower_wmark / 1000 * actmon->usecs_per_sample / 1000,
			     HOST1X_ACTMON_MODULE_LOWER_WMARK_REG);

	/* Update watermark interrupt enable bits */
	val |= HOST1X_ACTMON_MODULE_INTR_AVG_WMARK_ABOVE_ENB(upper_wmark_enabled);
	val |= HOST1X_ACTMON_MODULE_INTR_AVG_WMARK_BELOW_ENB(lower_wmark_enabled);
	val |= HOST1X_ACTMON_MODULE_INTR_CONSEC_WMARK_ABOVE_ENB(upper_wmark_enabled);
	val |= HOST1X_ACTMON_MODULE_INTR_CONSEC_WMARK_BELOW_ENB(lower_wmark_enabled);
	actmon_module_writel(module, val, HOST1X_ACTMON_MODULE_INTR_ENB_REG);
}
EXPORT_SYMBOL(host1x_actmon_update_active_wmark);
