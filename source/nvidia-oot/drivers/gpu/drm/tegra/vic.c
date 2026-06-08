// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (C) 2015-2025 NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/devfreq.h>
#include <linux/devfreq/tegra_wmark.h>
#include <linux/dma-mapping.h>
#include <linux/host1x-next.h>
#include <linux/interconnect.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include <nvidia/conftest.h>

#include <soc/tegra/fuse-helper.h>
#include <soc/tegra/pmc.h>
#include <soc/tegra/tegra-cbb.h>

#include "drm.h"
#include "falcon.h"
#include "riscv.h"
#include "util.h"
#include "vic.h"
#include "hwpm.h"

/* Falcon SEC registers */
#define VIC_SEC_INTF_CRC_CTRL               	0xe000

/* RISC-V SEC registers */
#define VIC_SEC_SFC_CRC_CFG			0xe000
#define VIC_SEC_INTF_CRC_CFG			0xe004

#define VIC_RISCV_BCR_CTRL			0x1a68
#define VIC_RISCV_BCR_CTRL_CORE_SELECT_RISCV	(1 << 4)
#define VIC_FALCON_DEBUGINFO			0x1094
#define VIC_DEBUGINFO_DUMMY			0xabcd1234
#define VIC_DEBUGINFO_CLEAR			0x0
#define VIC_RISCV_CPUCTL			0x1788
#define VIC_RISCV_CPUCTL_STARTCPU_TRUE		(1 << 0)
#define VIC_RISCV_CPUCTL_ACTIVE_STATE_V(x)	((x >> 7) & 0x1)
#define VIC_RISCV_CPUCTL_ACTIVE_STATE_ACTIVE_V	0x1

struct vic_config {
	const char *firmware;
	unsigned int version;
	bool supports_sid;
	bool supports_timestamping;
	bool has_crc_enable;
	bool has_riscv;
	u32 transcfg_addr;
	u32 actmon_active_mask;
	u32 actmon_active_borps;
	u32 actmon_active_weight;
	bool skip_bl_swizzling;
	u32 timestamp_shift;
};

struct vic {
	struct falcon falcon;
	struct tegra_drm_hwpm hwpm;

	void __iomem *regs;
	struct tegra_drm_client client;
	struct host1x_channel *channel;
	struct device *dev;
	struct clk *clk;
	struct reset_control *rst;
	struct devfreq *devfreq;
	struct devfreq_dev_profile *devfreq_profile;
	struct icc_path *icc_write;

	bool can_use_context;
	bool can_enable_crc;

	/* Platform configuration */
	const struct vic_config *config;

	/* RISC-V specific data */
	struct tegra_drm_riscv riscv;
};

static bool blf_write_allowed(u32 offset)
{
	void __iomem *regs;
	u32 val;

	if (of_machine_is_compatible("nvidia,tegra264")) {
		regs = ioremap(0x8180a98b60ULL, 20);

		val = readl(regs + 16);
		if (!(val & 0x20000)) {
			iounmap(regs);
			return true;
		}

		val = readl(regs + 8);
		iounmap(regs);
		if (val & BIT(1))
			return true;
	} else {
		regs = ioremap(0x13a10000 + offset, 12);

		val = readl(regs + 0x8);
		if (!(val & 0x20000)) {
			iounmap(regs);
			return true;
		}

		val = readl(regs + 0x4);
		iounmap(regs);
		if (val & BIT(1))
			return true;
	}

	return false;
}

static inline struct vic *to_vic(struct tegra_drm_client *client)
{
	return container_of(client, struct vic, client);
}

static void vic_writel(struct vic *vic, u32 value, unsigned int offset)
{
	writel(value, vic->regs + offset);
}

static int vic_falcon_boot(struct vic *vic)
{
	u32 fce_ucode_size, fce_bin_data_offset;
	void *hdr;
	int err = 0;

	/* setup clockgating registers */
	vic_writel(vic, CG_IDLE_CG_DLY_CNT(4) |
			CG_IDLE_CG_EN |
			CG_WAKEUP_DLY_CNT(4),
		   NV_PVIC_MISC_PRI_VIC_CG);

	err = falcon_boot(&vic->falcon);
	if (err < 0)
		return err;

	hdr = vic->falcon.firmware.virt;
	fce_bin_data_offset = *(u32 *)(hdr + VIC_UCODE_FCE_DATA_OFFSET);

	/* Old VIC firmware needs kernel help with setting up FCE microcode. */
	if (fce_bin_data_offset != 0x0 && fce_bin_data_offset != 0xa5a5a5a5) {
		hdr = vic->falcon.firmware.virt +
			*(u32 *)(hdr + VIC_UCODE_FCE_HEADER_OFFSET);
		fce_ucode_size = *(u32 *)(hdr + FCE_UCODE_SIZE_OFFSET);

		falcon_execute_method(&vic->falcon, VIC_SET_FCE_UCODE_SIZE,
				      fce_ucode_size);
		falcon_execute_method(
			&vic->falcon, VIC_SET_FCE_UCODE_OFFSET,
			(vic->falcon.firmware.iova + fce_bin_data_offset) >> 8);
	}

	err = falcon_wait_idle(&vic->falcon);
	if (err < 0) {
		dev_err(vic->dev,
			"failed to set application ID and FCE base\n");
		return err;
	}

	return 0;
}

static int vic_riscv_boot(struct vic *vic)
{
	int err;
	u32 val;

	vic_writel(vic, VIC_RISCV_BCR_CTRL_CORE_SELECT_RISCV, VIC_RISCV_BCR_CTRL);

	/* Write a known pattern into DEBUGINFO register */
	vic_writel(vic, VIC_DEBUGINFO_DUMMY, VIC_FALCON_DEBUGINFO);

	err = tegra_drm_riscv_boot_external(&vic->riscv);
	if (err < 0)
		return err;

	/* Kick start RISC-V */
	vic_writel(vic, VIC_RISCV_CPUCTL_STARTCPU_TRUE, VIC_RISCV_CPUCTL);

	err = readl_poll_timeout(
		vic->regs + VIC_RISCV_CPUCTL, val,
		VIC_RISCV_CPUCTL_ACTIVE_STATE_V(val) == VIC_RISCV_CPUCTL_ACTIVE_STATE_ACTIVE_V,
		10, 100000);
	if (err) {
		dev_err(vic->dev, "cpuctl active state timeout! val=0x%x", val);
		return err;
	}

	/* Check vic has reached a proper initialized state */
	err  = readl_poll_timeout(
		vic->regs + VIC_FALCON_DEBUGINFO, val,
		(val == VIC_DEBUGINFO_CLEAR),
		1000, 2000000);
	if (err) {
		dev_err(vic->dev, "not reached initialized state, timeout! val=0x%x", val);
		return err;
	}

	return 0;
}


static int vic_set_rate(struct vic *vic, unsigned long rate)
{
	unsigned long dev_rate;
	u32 emc_kbps;
	int err;

	err = clk_set_rate(vic->clk, rate);
	if (err < 0)
		return err;

	if (pm_runtime_suspended(vic->dev))
		return 0;

	dev_rate = clk_get_rate(vic->clk);

	if (vic->icc_write) {
		emc_kbps = dev_rate * VIC_AXI_RW_BANDWIDTH / 1024;
		err = icc_set_bw(vic->icc_write, 0, kbps_to_icc(emc_kbps));
		if (err)
			dev_warn(vic->dev, "failed to set icc bw: %d\n", err);
	}

	return 0;
}

static void vic_devfreq_update_wmark_threshold(struct devfreq *devfreq,
					       struct devfreq_tegra_wmark_config *cfg)
{
	struct vic *vic = dev_get_drvdata(devfreq->dev.parent);
	struct host1x_client *client = &vic->client.base;

	host1x_actmon_update_active_wmark(client,
					  cfg->avg_upper_wmark,
					  cfg->avg_lower_wmark,
					  cfg->consec_upper_wmark,
					  cfg->consec_lower_wmark,
					  cfg->upper_wmark_enabled,
					  cfg->lower_wmark_enabled);
}

static int vic_devfreq_target(struct device *dev, unsigned long *freq, u32 flags)
{
	struct vic *vic = dev_get_drvdata(dev);
	int err;

	err = vic_set_rate(vic, *freq);
	if (err < 0) {
		dev_err(dev, "failed to set clock rate\n");
		return err;
	}

	*freq = clk_get_rate(vic->clk);

	return 0;
}

static int vic_devfreq_get_dev_status(struct device *dev, struct devfreq_dev_status *stat)
{
	struct vic *vic = dev_get_drvdata(dev);
	struct host1x_client *client = &vic->client.base;
	unsigned long usage;

	/* Update load information */
	host1x_actmon_read_active_norm(client, &usage);
	stat->total_time = 1;
	stat->busy_time = usage;

	/* Update device frequency */
	stat->current_frequency = clk_get_rate(vic->clk);

	return 0;
}

static int vic_devfreq_get_cur_freq(struct device *dev, unsigned long *freq)
{
	struct vic *vic = dev_get_drvdata(dev);

	*freq = clk_get_rate(vic->clk);

	return 0;
}

static int vic_devfreq_init(struct vic *vic)
{
	unsigned long max_rate = clk_round_rate(vic->clk, ULONG_MAX);
	unsigned long min_rate = clk_round_rate(vic->clk, 0);
	unsigned long margin = clk_round_rate(vic->clk, min_rate + 1) - min_rate;
	unsigned long rate = min_rate;
	struct devfreq_tegra_wmark_data *data;
	struct devfreq_dev_profile *devfreq_profile;
	struct devfreq *devfreq;

	margin = max(margin, 9000000UL);
	do {
		dev_pm_opp_add(vic->dev, rate, 0);
		/* Overflow check */
		if (margin > ULONG_MAX - rate)
			break;
		rate += margin;
	} while (rate <= max_rate);

	data = devm_kzalloc(vic->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->event = DEVFREQ_TEGRA_AVG_WMARK_BELOW;
	data->update_wmark_threshold = vic_devfreq_update_wmark_threshold;

	devfreq_profile = devm_kzalloc(vic->dev, sizeof(*devfreq_profile), GFP_KERNEL);
	if (!devfreq_profile)
		return -ENOMEM;

	devfreq_profile->target = vic_devfreq_target;
	devfreq_profile->get_dev_status = vic_devfreq_get_dev_status;
	devfreq_profile->get_cur_freq = vic_devfreq_get_cur_freq;
	devfreq_profile->initial_freq = max_rate;
	devfreq_profile->polling_ms = 100;

	devfreq = devm_devfreq_add_device(vic->dev,
					  devfreq_profile,
					  DEVFREQ_GOV_USERSPACE,
					  data);
	if (IS_ERR(devfreq))
		return PTR_ERR(devfreq);

	vic->devfreq = devfreq;

	return 0;
}

static void vic_devfreq_deinit(struct vic *vic)
{
	if (!vic->devfreq)
		return;

	devm_devfreq_remove_device(vic->dev, vic->devfreq);
	vic->devfreq = NULL;
}

static int vic_init(struct host1x_client *client)
{
	struct tegra_drm_client *drm = host1x_to_drm_client(client);
	struct drm_device *dev = dev_get_drvdata(client->host);
	struct tegra_drm *tegra = dev->dev_private;
	struct vic *vic = to_vic(drm);
	int err;

	err = host1x_client_iommu_attach(client);
	if (err < 0 && err != -ENODEV) {
		dev_err(vic->dev, "failed to attach to domain: %d\n", err);
		return err;
	}

	vic->channel = host1x_channel_request(client);
	if (!vic->channel) {
		err = -ENOMEM;
		goto detach;
	}

	client->syncpts[0] = host1x_syncpt_request(client, 0);
	if (!client->syncpts[0]) {
		err = -ENOMEM;
		goto free_channel;
	}

	err = tegra_drm_register_client(tegra, drm);
	if (err < 0)
		goto free_syncpt;

	/*
	 * Inherit the DMA parameters (such as maximum segment size) from the
	 * parent host1x device.
	 */
	client->dev->dma_parms = client->host->dma_parms;

	return 0;

free_syncpt:
	host1x_syncpt_put(client->syncpts[0]);
free_channel:
	host1x_channel_put(vic->channel);
detach:
	host1x_client_iommu_detach(client);

	return err;
}

static int vic_exit(struct host1x_client *client)
{
	struct tegra_drm_client *drm = host1x_to_drm_client(client);
	struct drm_device *dev = dev_get_drvdata(client->host);
	struct tegra_drm *tegra = dev->dev_private;
	struct vic *vic = to_vic(drm);
	int err;

	/* avoid a dangling pointer just in case this disappears */
	client->dev->dma_parms = NULL;

	err = tegra_drm_unregister_client(tegra, drm);
	if (err < 0)
		return err;

	pm_runtime_dont_use_autosuspend(client->dev);
	pm_runtime_force_suspend(client->dev);

	host1x_syncpt_put(client->syncpts[0]);
	host1x_channel_put(vic->channel);
	host1x_client_iommu_detach(client);

	vic->channel = NULL;

	if (vic->config->has_riscv) {
		dma_free_coherent(vic->dev, vic->riscv.firmware.size,
				  vic->riscv.firmware.virt,
				  vic->riscv.firmware.iova);
	} else {
		if (client->group) {
			dma_unmap_single(vic->dev, vic->falcon.firmware.phys,
					 vic->falcon.firmware.size, DMA_TO_DEVICE);
			tegra_drm_free(tegra, vic->falcon.firmware.size,
				       vic->falcon.firmware.virt,
				       vic->falcon.firmware.iova);
		} else {
			dma_free_coherent(vic->dev, vic->falcon.firmware.size,
					  vic->falcon.firmware.virt,
					  vic->falcon.firmware.iova);
		}
	}

	return 0;
}

static unsigned long vic_get_rate(struct host1x_client *client)
{
	struct platform_device *pdev = to_platform_device(client->dev);
	struct vic *vic = platform_get_drvdata(pdev);

	return clk_get_rate(vic->clk);
}

static void vic_actmon_event(struct host1x_client *client,
			     enum host1x_actmon_wmark_event event)
{
	struct platform_device *pdev = to_platform_device(client->dev);
	struct vic *vic = platform_get_drvdata(pdev);
	struct devfreq *df = vic->devfreq;
	struct devfreq_tegra_wmark_data *data;

	if (!df)
		return;

	data = df->data;

	switch (event) {
	case HOST1X_ACTMON_AVG_WMARK_BELOW:
		data->event = DEVFREQ_TEGRA_AVG_WMARK_BELOW;
		break;
	case HOST1X_ACTMON_AVG_WMARK_ABOVE:
		data->event = DEVFREQ_TEGRA_AVG_WMARK_ABOVE;
		break;
	case HOST1X_ACTMON_CONSEC_WMARK_BELOW:
		data->event = DEVFREQ_TEGRA_CONSEC_WMARK_BELOW;
		break;
	case HOST1X_ACTMON_CONSEC_WMARK_ABOVE:
		data->event = DEVFREQ_TEGRA_CONSEC_WMARK_ABOVE;
		break;
	default:
		return;
	}

	mutex_lock(&df->lock);
	update_devfreq(df);
	mutex_unlock(&df->lock);
}

static const struct host1x_client_ops vic_client_ops = {
	.init = vic_init,
	.exit = vic_exit,
	.get_rate = vic_get_rate,
	.actmon_event = vic_actmon_event,
};

static int vic_load_falcon_firmware(struct vic *vic)
{
	struct host1x_client *client = &vic->client.base;
	struct tegra_drm *tegra = vic->client.drm;
	static DEFINE_MUTEX(lock);
	u32 fce_bin_data_offset;
	dma_addr_t iova;
	size_t size;
	void *virt;
	int err;

	mutex_lock(&lock);

	if (vic->falcon.firmware.virt) {
		err = 0;
		goto unlock;
	}

	err = falcon_read_firmware(&vic->falcon, vic->config->firmware);
	if (err < 0)
		goto unlock;

	size = vic->falcon.firmware.size;

	if (!client->group) {
		virt = dma_alloc_coherent(vic->dev, size, &iova, GFP_KERNEL);
		if (!virt) {
			err = -ENOMEM;
			goto unlock;
		}
	} else {
		virt = tegra_drm_alloc(tegra, size, &iova);
		if (IS_ERR(virt)) {
			err = PTR_ERR(virt);
			goto unlock;
		}
	}

	vic->falcon.firmware.virt = virt;
	vic->falcon.firmware.iova = iova;

	err = falcon_load_firmware(&vic->falcon);
	if (err < 0)
		goto cleanup;

	/*
	 * In this case we have received an IOVA from the shared domain, so we
	 * need to make sure to get the physical address so that the DMA API
	 * knows what memory pages to flush the cache for.
	 */
	if (client->group) {
		dma_addr_t phys;

		phys = dma_map_single(vic->dev, virt, size, DMA_TO_DEVICE);

		err = dma_mapping_error(vic->dev, phys);
		if (err < 0)
			goto cleanup;

		vic->falcon.firmware.phys = phys;
	}

	/*
	 * Check if firmware is new enough to not require mapping firmware
	 * to data buffer domains.
	 */
	fce_bin_data_offset = *(u32 *)(virt + VIC_UCODE_FCE_DATA_OFFSET);

	if (!vic->config->supports_sid) {
		vic->can_use_context = false;
	} else if (fce_bin_data_offset != 0x0 && fce_bin_data_offset != 0xa5a5a5a5) {
		/*
		 * Firmware will access FCE through STREAMID0, so context
		 * isolation cannot be used.
		 */
		vic->can_use_context = false;
		dev_warn_once(vic->dev, "context isolation disabled due to old firmware\n");
	} else {
		vic->can_use_context = true;
	}

unlock:
	mutex_unlock(&lock);
	return err;

cleanup:
	if (!client->group)
		dma_free_coherent(vic->dev, size, virt, iova);
	else
		tegra_drm_free(tegra, size, virt, iova);

	mutex_unlock(&lock);
	return err;
}

static int vic_load_riscv_firmware(struct vic *vic)
{
	dma_addr_t iova;
	size_t size;
	void *virt;
	int err;

	if (vic->riscv.firmware.virt)
		return 0;

	err = tegra_drm_riscv_read_firmware(&vic->riscv, vic->config->firmware);
	if (err < 0)
		return err;

	size = vic->riscv.firmware.size;

	virt = dma_alloc_coherent(vic->dev, size, &iova, GFP_KERNEL);

	err = dma_mapping_error(vic->dev, iova);
	if (err < 0)
		return err;

	vic->riscv.firmware.virt = virt;
	vic->riscv.firmware.iova = iova;

	err = tegra_drm_riscv_load_firmware(&vic->riscv);
	if (err < 0)
		goto cleanup;

	vic->can_use_context = true;

	return 0;

cleanup:
	dma_free_coherent(vic->dev, size, virt, iova);

	return err;
}

static void vic_actmon_reg_init(struct vic *vic)
{
	if (vic->config->actmon_active_mask)
		vic_writel(vic,
			VIC_TFBIF_ACTMON_ACTIVE_MASK_STARVED |
			VIC_TFBIF_ACTMON_ACTIVE_MASK_STALLED |
			VIC_TFBIF_ACTMON_ACTIVE_MASK_DELAYED,
			vic->config->actmon_active_mask);

	if (vic->config->actmon_active_borps)
		vic_writel(vic,
			VIC_TFBIF_ACTMON_ACTIVE_BORPS_ACTIVE,
			vic->config->actmon_active_borps);
}

static void vic_count_weight_init(struct vic *vic, unsigned long rate)
{
	const struct vic_config *config = vic->config;
	struct host1x_client *client = &vic->client.base;
	u32 weight = 0;

	host1x_actmon_update_client_rate(client, rate, &weight);

	if (config->actmon_active_weight && weight)
		vic_writel(vic, weight, config->actmon_active_weight);

}

static int __maybe_unused vic_runtime_resume(struct device *dev)
{
	struct vic *vic = dev_get_drvdata(dev);
	int err;

	err = clk_prepare_enable(vic->clk);
	if (err < 0)
		return err;

	usleep_range(10, 20);

	err = reset_control_deassert(vic->rst);
	if (err < 0)
		goto disable;

	usleep_range(10, 20);

	if (vic->config->supports_sid)
		tegra_drm_program_iommu_regs(vic->dev, vic->regs, vic->config->transcfg_addr);

	if (vic->config->has_riscv) {
		err = vic_load_riscv_firmware(vic);
		if (err < 0)
			goto assert;

		err = vic_riscv_boot(vic);
		if (err < 0)
			goto assert;
	} else {
		err = vic_load_falcon_firmware(vic);
		if (err < 0)
			goto assert;

		err = vic_falcon_boot(vic);
		if (err < 0)
			goto assert;
	}

	/* Forcely set frequency as Fmax when device is resumed back */
	vic->devfreq->resume_freq = vic->devfreq->scaling_max_freq;
	err = devfreq_resume_device(vic->devfreq);
	if (err < 0)
		goto assert;

	vic_actmon_reg_init(vic);

	vic_count_weight_init(vic, vic->devfreq->scaling_max_freq);

	host1x_actmon_enable(&vic->client.base);

	if (vic->can_enable_crc) {
		vic_writel(vic, 0x1, VIC_SEC_INTF_CRC_CTRL);
		if (of_machine_is_compatible("nvidia,tegra264"))
			vic_writel(vic, 0x1, VIC_SEC_INTF_CRC_CFG);
	}

	return 0;

assert:
	reset_control_assert(vic->rst);
disable:
	clk_disable_unprepare(vic->clk);
	return err;
}

static int __maybe_unused vic_runtime_suspend(struct device *dev)
{
	struct vic *vic = dev_get_drvdata(dev);
	int err;

	err = devfreq_suspend_device(vic->devfreq);
	if (err < 0)
		return err;

	if (vic->icc_write) {
		err = icc_set_bw(vic->icc_write, 0, 0);
		if (err) {
			dev_warn(vic->dev, "failed to set icc bw: %d\n", err);
			goto devfreq_resume;
		}
	}

	err = reset_control_assert(vic->rst);
	if (err < 0)
		goto devfreq_resume;

	usleep_range(2000, 4000);

	clk_disable_unprepare(vic->clk);

	host1x_channel_stop(vic->channel);

	host1x_actmon_disable(&vic->client.base);

	return 0;

devfreq_resume:
	devfreq_resume_device(vic->devfreq);
	return err;
}

static int vic_open_channel(struct tegra_drm_client *client,
			    struct tegra_drm_context *context)
{
	struct vic *vic = to_vic(client);

	context->channel = host1x_channel_get(vic->channel);
	if (!context->channel)
		return -ENOMEM;

	return 0;
}

static void vic_close_channel(struct tegra_drm_context *context)
{
	host1x_channel_put(context->channel);
}

static int vic_can_use_memory_ctx(struct tegra_drm_client *client, bool *supported)
{
	struct vic *vic = to_vic(client);
	int err;

	if (vic->config->has_riscv) {
		*supported = true;
	} else {
		/* This doesn't access HW so it's safe to call without powering up. */
		err = vic_load_falcon_firmware(vic);
		if (err < 0)
			return err;

		*supported = vic->can_use_context;
	}

	return 0;
}

static int vic_has_job_timestamping(struct tegra_drm_client *client, bool *supported,
			    u32 *timestamp_shift)
{
	struct vic *vic = to_vic(client);

	*supported = vic->config->supports_timestamping;
	*timestamp_shift = vic->config->timestamp_shift;

	return 0;
}

static int vic_skip_bl_swizzling(struct tegra_drm_client *client, bool *skip)
{
	struct vic *vic = to_vic(client);

	*skip = vic->config->skip_bl_swizzling;

	return 0;
}

static const struct tegra_drm_client_ops vic_ops = {
	.open_channel = vic_open_channel,
	.close_channel = vic_close_channel,
	.submit = tegra_drm_submit,
	.get_streamid_offset = tegra_drm_get_streamid_offset_thi,
	.can_use_memory_ctx = vic_can_use_memory_ctx,
	.has_job_timestamping = vic_has_job_timestamping,
	.skip_bl_swizzling = vic_skip_bl_swizzling,
};

#define NVIDIA_TEGRA_124_VIC_FIRMWARE "nvidia/tegra124/vic03_ucode.bin"

static const struct vic_config vic_t124_config = {
	.firmware = NVIDIA_TEGRA_124_VIC_FIRMWARE,
	.version = 0x40,
	.supports_sid = false,
};

#define NVIDIA_TEGRA_210_VIC_FIRMWARE "nvidia/tegra210/vic04_ucode.bin"

static const struct vic_config vic_t210_config = {
	.firmware = NVIDIA_TEGRA_210_VIC_FIRMWARE,
	.version = 0x21,
	.supports_sid = false,
};

#define NVIDIA_TEGRA_186_VIC_FIRMWARE "nvidia/tegra186/vic04_ucode.bin"

static const struct vic_config vic_t186_config = {
	.firmware = NVIDIA_TEGRA_186_VIC_FIRMWARE,
	.version = 0x18,
	.supports_sid = true,
	.transcfg_addr = 0x2044,
	.actmon_active_mask = 0x204c,
	.actmon_active_borps = 0x2050,
	.actmon_active_weight = 0x2054,
};

#define NVIDIA_TEGRA_194_VIC_FIRMWARE "nvidia/tegra194/vic.bin"

static const struct vic_config vic_t194_config = {
	.firmware = NVIDIA_TEGRA_194_VIC_FIRMWARE,
	.version = 0x19,
	.supports_sid = true,
	.supports_timestamping = true,
	.transcfg_addr = 0x2044,
	.actmon_active_mask = 0x204c,
	.actmon_active_borps = 0x2050,
	.actmon_active_weight = 0x2054,
	.timestamp_shift = 5,
};

#define NVIDIA_TEGRA_234_VIC_FIRMWARE "nvidia/tegra234/vic.bin"

static const struct vic_config vic_t234_config = {
	.firmware = NVIDIA_TEGRA_234_VIC_FIRMWARE,
	.version = 0x23,
	.supports_sid = true,
	.supports_timestamping = true,
	.has_crc_enable = true,
	.transcfg_addr = 0x2044,
	.actmon_active_mask = 0x204c,
	.actmon_active_borps = 0x2050,
	.actmon_active_weight = 0x2054,
	.timestamp_shift = 5,
};

#define NVIDIA_TEGRA_264_VIC_FIRMWARE "nvidia/tegra264/vic.bin"

static const struct vic_config vic_t264_config = {
	.firmware = NVIDIA_TEGRA_264_VIC_FIRMWARE,
	.version = 0x26,
	.supports_sid = true,
	.supports_timestamping = true,
	.has_crc_enable = true,
	.has_riscv = true,
	.transcfg_addr = 0x2244,
	.actmon_active_mask = 0x224c,
	.actmon_active_borps = 0x2250,
	.actmon_active_weight = 0x2254,
	.skip_bl_swizzling = true,
	.timestamp_shift = 0,
};

static const struct of_device_id tegra_vic_of_match[] = {
	{ .compatible = "nvidia,tegra124-vic", .data = &vic_t124_config },
	{ .compatible = "nvidia,tegra210-vic", .data = &vic_t210_config },
	{ .compatible = "nvidia,tegra186-vic", .data = &vic_t186_config },
	{ .compatible = "nvidia,tegra194-vic", .data = &vic_t194_config },
	{ .compatible = "nvidia,tegra234-vic", .data = &vic_t234_config },
	{ .compatible = "nvidia,tegra264-vic", .data = &vic_t264_config },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_vic_of_match);

static int vic_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct host1x_syncpt **syncpts;
	struct vic *vic;
	int err;

	/* inherit DMA mask from host1x parent */
	err = dma_coerce_mask_and_coherent(dev, *dev->parent->dma_mask);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to set DMA mask: %d\n", err);
		return err;
	}

	vic = devm_kzalloc(dev, sizeof(*vic), GFP_KERNEL);
	if (!vic)
		return -ENOMEM;

	vic->config = of_device_get_match_data(dev);

	syncpts = devm_kzalloc(dev, sizeof(*syncpts), GFP_KERNEL);
	if (!syncpts)
		return -ENOMEM;

	vic->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(vic->regs))
		return PTR_ERR(vic->regs);

	vic->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(vic->clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		return PTR_ERR(vic->clk);
	}

	vic->icc_write = devm_of_icc_get(dev, "write");
	if (IS_ERR(vic->icc_write))
		return dev_err_probe(&pdev->dev, PTR_ERR(vic->icc_write),
				     "failed to get icc write handle\n");

	if (!dev->pm_domain) {
		vic->rst = devm_reset_control_get(dev, "vic");
		if (IS_ERR(vic->rst)) {
			dev_err(&pdev->dev, "failed to get reset\n");
			return PTR_ERR(vic->rst);
		}
	}

	if (vic->config->has_riscv) {
		vic->riscv.dev = dev;
		vic->riscv.regs = vic->regs;

		vic->riscv.os_desc.code_offset = 0x0;
		vic->riscv.os_desc.code_size = 0x1300;
		vic->riscv.os_desc.data_offset = 0x1300;
		vic->riscv.os_desc.data_size = 0xa00;

		err = tegra_drm_riscv_init(&vic->riscv);
		if (err < 0)
			return err;
	} else {
		vic->falcon.dev = dev;
		vic->falcon.regs = vic->regs;

		err = falcon_init(&vic->falcon);
		if (err < 0)
			return err;
	}

	platform_set_drvdata(pdev, vic);

	INIT_LIST_HEAD(&vic->client.base.list);
	vic->client.base.ops = &vic_client_ops;
	vic->client.base.dev = dev;
	vic->client.base.class = HOST1X_CLASS_VIC;
	vic->client.base.syncpts = syncpts;
	vic->client.base.num_syncpts = 1;
	vic->dev = dev;

	INIT_LIST_HEAD(&vic->client.list);
	vic->client.version = vic->config->version;
	vic->client.ops = &vic_ops;

	err = host1x_client_register(&vic->client.base);
	if (err < 0) {
		dev_err(dev, "failed to register host1x client: %d\n", err);
		goto exit_probe;
	}

	err = host1x_actmon_register(&vic->client.base);
	if (err < 0)
		dev_info(dev, "failed to register host1x actmon: %d\n", err);

	/* Set default clock rate for vic */
	err = clk_set_rate(vic->clk, ULONG_MAX);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to set clock rate: %d\n", err);
		goto exit_probe;
	}

	err = vic_devfreq_init(vic);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to init devfreq: %d\n", err);
		goto exit_probe;
	}

	if (vic->config->has_crc_enable) {
		vic->can_enable_crc =
			!tegra_platform_is_silicon() ||
			blf_write_allowed(0x9500);
	}

	vic->hwpm.dev = dev;
	vic->hwpm.regs = vic->regs;
	tegra_drm_hwpm_register(&vic->hwpm, pdev->resource[0].start,
		TEGRA_DRM_HWPM_IP_VIC);

	pm_runtime_enable(dev);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_autosuspend_delay(dev, 500);

	return 0;

exit_probe:
	if (vic->config->has_riscv)
		tegra_drm_riscv_exit(&vic->riscv);
	else
		falcon_exit(&vic->falcon);

	return err;
}

static int vic_remove(struct platform_device *pdev)
{
	struct vic *vic = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	tegra_drm_hwpm_unregister(&vic->hwpm, pdev->resource[0].start,
		TEGRA_DRM_HWPM_IP_VIC);

	vic_devfreq_deinit(vic);

	host1x_actmon_unregister(&vic->client.base);

	host1x_client_unregister(&vic->client.base);

	if (vic->config->has_riscv)
		tegra_drm_riscv_exit(&vic->riscv);
	else
		falcon_exit(&vic->falcon);

	return 0;
}

static const struct dev_pm_ops vic_pm_ops = {
	RUNTIME_PM_OPS(vic_runtime_suspend, vic_runtime_resume, NULL)
	SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
};

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void vic_remove_wrapper(struct platform_device *pdev)
{
	vic_remove(pdev);
}
#else
static int vic_remove_wrapper(struct platform_device *pdev)
{
	return vic_remove(pdev);
}
#endif

struct platform_driver tegra_vic_driver = {
	.driver = {
		.name = "tegra-vic",
		.of_match_table = tegra_vic_of_match,
		.pm = &vic_pm_ops
	},
	.probe = vic_probe,
	.remove = vic_remove_wrapper,
};

#if IS_ENABLED(CONFIG_ARCH_TEGRA_124_SOC)
MODULE_FIRMWARE(NVIDIA_TEGRA_124_VIC_FIRMWARE);
#endif
#if IS_ENABLED(CONFIG_ARCH_TEGRA_210_SOC)
MODULE_FIRMWARE(NVIDIA_TEGRA_210_VIC_FIRMWARE);
#endif
#if IS_ENABLED(CONFIG_ARCH_TEGRA_186_SOC)
MODULE_FIRMWARE(NVIDIA_TEGRA_186_VIC_FIRMWARE);
#endif
#if IS_ENABLED(CONFIG_ARCH_TEGRA_194_SOC)
MODULE_FIRMWARE(NVIDIA_TEGRA_194_VIC_FIRMWARE);
#endif
#if IS_ENABLED(CONFIG_ARCH_TEGRA_234_SOC)
MODULE_FIRMWARE(NVIDIA_TEGRA_234_VIC_FIRMWARE);
#endif
#if IS_ENABLED(CONFIG_ARCH_TEGRA_264_SOC)
MODULE_FIRMWARE(NVIDIA_TEGRA_264_VIC_FIRMWARE);
#endif
