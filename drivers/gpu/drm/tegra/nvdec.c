// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2015-2023, NVIDIA CORPORATION & AFFILIATES. All Rights Reserved.
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
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/version.h>

#include <soc/tegra/mc.h>

#include "drm.h"
#include "falcon.h"
#include "riscv.h"
#include "util.h"
#include "hwpm.h"

#define NVDEC_FW_MTHD_ADDR_ACTMON_ACTIVE_MASK	0xCAU
#define NVDEC_FW_MTHD_ADDR_ACTMON_ACTIVE_BORPS	0xCBU
#define NVDEC_FW_MTHD_ADDR_ACTMON_ACTIVE_WEIGHT	0xC9U
#define NVDEC_FALCON_UCLASS_METHOD_OFFSET	0x40
#define NVDEC_FALCON_UCLASS_METHOD_DATA		0x44
#define NVDEC_FALCON_DEBUGINFO			0x1094
#define NVDEC_TFBIF_TRANSCFG			0x2c44
#define NVDEC_TFBIF_ACTMON_ACTIVE_MASK		0x2c4c
#define NVDEC_TFBIF_ACTMON_ACTIVE_BORPS		0x2c50
#define NVDEC_TFBIF_ACTMON_ACTIVE_WEIGHT	0x2c54
#define NVDEC_AXI_RW_BANDWIDTH			512

#define NVDEC_TFBIF_ACTMON_ACTIVE_MASK_STARVED	BIT(0)
#define NVDEC_TFBIF_ACTMON_ACTIVE_MASK_STALLED	BIT(1)
#define NVDEC_TFBIF_ACTMON_ACTIVE_MASK_DELAYED	BIT(2)
#define NVDEC_TFBIF_ACTMON_ACTIVE_BORPS_ACTIVE	BIT(7)

struct nvdec_config {
	const char *firmware;
	unsigned int version;
	bool supports_sid;
	bool supports_timestamping;
	bool has_riscv;
	bool has_extra_clocks;
};

struct nvdec {
	struct falcon falcon;
	struct tegra_drm_hwpm hwpm;

	void __iomem *regs;
	struct tegra_drm_client client;
	struct host1x_channel *channel;
	struct device *dev;
	struct clk_bulk_data clks[3];
	unsigned int num_clks;
	struct reset_control *reset;
	struct devfreq *devfreq;
	struct devfreq_dev_profile *devfreq_profile;
	struct icc_path *icc_write;

	/* Platform configuration */
	const struct nvdec_config *config;

	/* RISC-V specific data */
	struct tegra_drm_riscv riscv;
	phys_addr_t carveout_base;
};

static inline struct nvdec *to_nvdec(struct tegra_drm_client *client)
{
	return container_of(client, struct nvdec, client);
}

static inline void nvdec_writel(struct nvdec *nvdec, u32 value,
				unsigned int offset)
{
	writel(value, nvdec->regs + offset);
}

static int nvdec_set_rate(struct nvdec *nvdec, unsigned long rate)
{
	unsigned long dev_rate;
	u32 emc_kbps;
	int err;

	err = clk_set_rate(nvdec->clks[0].clk, rate);
	if (err < 0)
		return err;

	if (pm_runtime_suspended(nvdec->dev))
		return 0;

	dev_rate = clk_get_rate(nvdec->clks[0].clk);

	if (nvdec->icc_write) {
		emc_kbps = dev_rate * NVDEC_AXI_RW_BANDWIDTH / 1024;
		err = icc_set_bw(nvdec->icc_write, 0, kbps_to_icc(emc_kbps));
		if (err)
			dev_warn(nvdec->dev, "failed to set icc bw: %d\n", err);
	}

	return 0;
}

static int nvdec_boot_falcon(struct nvdec *nvdec)
{
	int err;

	if (nvdec->config->supports_sid)
		tegra_drm_program_iommu_regs(nvdec->dev, nvdec->regs, NVDEC_TFBIF_TRANSCFG);

	err = falcon_boot(&nvdec->falcon);
	if (err < 0)
		return err;

	err = falcon_wait_idle(&nvdec->falcon);
	if (err < 0) {
		dev_err(nvdec->dev, "falcon boot timed out\n");
		return err;
	}

	return 0;
}

static int nvdec_wait_debuginfo(struct nvdec *nvdec, const char *phase)
{
	int err;
	u32 val;

	err = readl_poll_timeout(nvdec->regs + NVDEC_FALCON_DEBUGINFO, val, val == 0x0, 10, 100000);
	if (err) {
		dev_err(nvdec->dev, "failed to boot %s, debuginfo=0x%x\n", phase, val);
		return err;
	}

	return 0;
}

static int nvdec_boot_riscv(struct nvdec *nvdec)
{
	int err;

	err = reset_control_acquire(nvdec->reset);
	if (err)
		return err;

	nvdec_writel(nvdec, 0xabcd1234, NVDEC_FALCON_DEBUGINFO);

	err = tegra_drm_riscv_boot_bootrom(&nvdec->riscv, nvdec->carveout_base, 1,
					   &nvdec->riscv.bl_desc);
	if (err) {
		dev_err(nvdec->dev, "failed to execute bootloader\n");
		goto release_reset;
	}

	err = nvdec_wait_debuginfo(nvdec, "bootloader");
	if (err)
		goto release_reset;

	err = reset_control_reset(nvdec->reset);
	if (err)
		goto release_reset;

	nvdec_writel(nvdec, 0xabcd1234, NVDEC_FALCON_DEBUGINFO);

	err = tegra_drm_riscv_boot_bootrom(&nvdec->riscv, nvdec->carveout_base, 1,
					   &nvdec->riscv.os_desc);
	if (err) {
		dev_err(nvdec->dev, "failed to execute firmware\n");
		goto release_reset;
	}

	err = nvdec_wait_debuginfo(nvdec, "firmware");
	if (err)
		goto release_reset;

release_reset:
	reset_control_release(nvdec->reset);

	return err;
}

static void nvdec_devfreq_update_wmark_threshold(struct devfreq *devfreq,
						 struct devfreq_tegra_wmark_config *cfg)
{
	struct nvdec *nvdec = dev_get_drvdata(devfreq->dev.parent);
	struct host1x_client *client = &nvdec->client.base;

	host1x_actmon_update_active_wmark(client,
					  cfg->avg_upper_wmark,
					  cfg->avg_lower_wmark,
					  cfg->consec_upper_wmark,
					  cfg->consec_lower_wmark,
					  cfg->upper_wmark_enabled,
					  cfg->lower_wmark_enabled);
}

static int nvdec_devfreq_target(struct device *dev, unsigned long *freq, u32 flags)
{
	struct nvdec *nvdec = dev_get_drvdata(dev);
	int err;

	err = nvdec_set_rate(nvdec, *freq);
	if (err < 0) {
		dev_err(dev, "failed to set clock rate\n");
		return err;
	}

	*freq = clk_get_rate(nvdec->clks[0].clk);

	return 0;
}

static int nvdec_devfreq_get_dev_status(struct device *dev, struct devfreq_dev_status *stat)
{
	struct nvdec *nvdec = dev_get_drvdata(dev);
	struct host1x_client *client = &nvdec->client.base;
	unsigned long usage;

	/* Update load information */
	host1x_actmon_read_active_norm(client, &usage);
	stat->total_time = 1;
	stat->busy_time = usage;

	/* Update device frequency */
	stat->current_frequency = clk_get_rate(nvdec->clks[0].clk);

	return 0;
}

static int nvdec_devfreq_get_cur_freq(struct device *dev, unsigned long *freq)
{
	struct nvdec *nvdec = dev_get_drvdata(dev);

	*freq = clk_get_rate(nvdec->clks[0].clk);

	return 0;
}

static int nvdec_devfreq_init(struct nvdec *nvdec)
{
	struct clk *clk = nvdec->clks[0].clk;
	unsigned long max_rate = clk_round_rate(clk, ULONG_MAX);
	unsigned long min_rate = clk_round_rate(clk, 0);
	unsigned long margin = clk_round_rate(clk, min_rate + 1) - min_rate;
	unsigned long rate = min_rate;
	struct devfreq_tegra_wmark_data *data;
	struct devfreq_dev_profile *devfreq_profile;
	struct devfreq *devfreq;

	while (rate <= max_rate) {
		dev_pm_opp_add(nvdec->dev, rate, 0);
		rate += margin;
	}

	data = devm_kzalloc(nvdec->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->event = DEVFREQ_TEGRA_AVG_WMARK_BELOW;
	data->update_wmark_threshold = nvdec_devfreq_update_wmark_threshold;

	devfreq_profile = devm_kzalloc(nvdec->dev, sizeof(*devfreq_profile), GFP_KERNEL);
	if (!devfreq_profile)
		return -ENOMEM;

	devfreq_profile->target = nvdec_devfreq_target;
	devfreq_profile->get_dev_status = nvdec_devfreq_get_dev_status;
	devfreq_profile->get_cur_freq = nvdec_devfreq_get_cur_freq;
	devfreq_profile->initial_freq = max_rate;
	devfreq_profile->polling_ms = 100;

	devfreq = devm_devfreq_add_device(nvdec->dev,
					  devfreq_profile,
					  DEVFREQ_GOV_USERSPACE,
					  data);
	if (IS_ERR(devfreq))
		return PTR_ERR(devfreq);

	nvdec->devfreq = devfreq;

	return 0;
}

static void nvdec_devfreq_deinit(struct nvdec *nvdec)
{
	if (!nvdec->devfreq)
		return;

	devm_devfreq_remove_device(nvdec->dev, nvdec->devfreq);
	nvdec->devfreq = NULL;
}

static int nvdec_init(struct host1x_client *client)
{
	struct tegra_drm_client *drm = host1x_to_drm_client(client);
	struct drm_device *dev = dev_get_drvdata(client->host);
	struct tegra_drm *tegra = dev->dev_private;
	struct nvdec *nvdec = to_nvdec(drm);
	int err;

	err = host1x_client_iommu_attach(client);
	if (err < 0 && err != -ENODEV) {
		dev_err(nvdec->dev, "failed to attach to domain: %d\n", err);
		return err;
	}

	nvdec->channel = host1x_channel_request(client);
	if (!nvdec->channel) {
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
	host1x_channel_put(nvdec->channel);
detach:
	host1x_client_iommu_detach(client);

	return err;
}

static int nvdec_exit(struct host1x_client *client)
{
	struct tegra_drm_client *drm = host1x_to_drm_client(client);
	struct drm_device *dev = dev_get_drvdata(client->host);
	struct tegra_drm *tegra = dev->dev_private;
	struct nvdec *nvdec = to_nvdec(drm);
	int err;

	/* avoid a dangling pointer just in case this disappears */
	client->dev->dma_parms = NULL;

	err = tegra_drm_unregister_client(tegra, drm);
	if (err < 0)
		return err;

	pm_runtime_dont_use_autosuspend(client->dev);
	pm_runtime_force_suspend(client->dev);

	host1x_syncpt_put(client->syncpts[0]);
	host1x_channel_put(nvdec->channel);
	host1x_client_iommu_detach(client);

	nvdec->channel = NULL;

	if (client->group) {
		dma_unmap_single(nvdec->dev, nvdec->falcon.firmware.phys,
				 nvdec->falcon.firmware.size, DMA_TO_DEVICE);
		tegra_drm_free(tegra, nvdec->falcon.firmware.size,
			       nvdec->falcon.firmware.virt,
			       nvdec->falcon.firmware.iova);
	} else {
		dma_free_coherent(nvdec->dev, nvdec->falcon.firmware.size,
				  nvdec->falcon.firmware.virt,
				  nvdec->falcon.firmware.iova);
	}

	return 0;
}

static unsigned long nvdec_get_rate(struct host1x_client *client)
{
	struct platform_device *pdev = to_platform_device(client->dev);
	struct nvdec *nvdec = platform_get_drvdata(pdev);

	return clk_get_rate(nvdec->clks[0].clk);
}

static void nvdec_actmon_event(struct host1x_client *client,
			     enum host1x_actmon_wmark_event event)
{
	struct platform_device *pdev = to_platform_device(client->dev);
	struct nvdec *nvdec = platform_get_drvdata(pdev);
	struct devfreq *df = nvdec->devfreq;
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

static const struct host1x_client_ops nvdec_client_ops = {
	.init = nvdec_init,
	.exit = nvdec_exit,
	.get_rate = nvdec_get_rate,
	.actmon_event = nvdec_actmon_event,
};

static int nvdec_load_falcon_firmware(struct nvdec *nvdec)
{
	struct host1x_client *client = &nvdec->client.base;
	struct tegra_drm *tegra = nvdec->client.drm;
	dma_addr_t iova;
	size_t size;
	void *virt;
	int err;

	if (nvdec->falcon.firmware.virt)
		return 0;

	err = falcon_read_firmware(&nvdec->falcon, nvdec->config->firmware);
	if (err < 0)
		return err;

	size = nvdec->falcon.firmware.size;

	if (!client->group) {
		virt = dma_alloc_coherent(nvdec->dev, size, &iova, GFP_KERNEL);

		err = dma_mapping_error(nvdec->dev, iova);
		if (err < 0)
			return err;
	} else {
		virt = tegra_drm_alloc(tegra, size, &iova);
	}

	nvdec->falcon.firmware.virt = virt;
	nvdec->falcon.firmware.iova = iova;

	err = falcon_load_firmware(&nvdec->falcon);
	if (err < 0)
		goto cleanup;

	/*
	 * In this case we have received an IOVA from the shared domain, so we
	 * need to make sure to get the physical address so that the DMA API
	 * knows what memory pages to flush the cache for.
	 */
	if (client->group) {
		dma_addr_t phys;

		phys = dma_map_single(nvdec->dev, virt, size, DMA_TO_DEVICE);

		err = dma_mapping_error(nvdec->dev, phys);
		if (err < 0)
			goto cleanup;

		nvdec->falcon.firmware.phys = phys;
	}

	return 0;

cleanup:
	if (!client->group)
		dma_free_coherent(nvdec->dev, size, virt, iova);
	else
		tegra_drm_free(tegra, size, virt, iova);

	return err;
}

static void nvdec_actmon_reg_init(struct nvdec *nvdec)
{
	if (nvdec->config->has_riscv) {
		nvdec_writel(nvdec,
			     NVDEC_FW_MTHD_ADDR_ACTMON_ACTIVE_MASK,
			     NVDEC_FALCON_UCLASS_METHOD_OFFSET);
		nvdec_writel(nvdec,
			     NVDEC_TFBIF_ACTMON_ACTIVE_MASK_DELAYED |
			     NVDEC_TFBIF_ACTMON_ACTIVE_MASK_STALLED |
			     NVDEC_TFBIF_ACTMON_ACTIVE_MASK_STARVED,
			     NVDEC_FALCON_UCLASS_METHOD_DATA);

		nvdec_writel(nvdec,
			     NVDEC_FW_MTHD_ADDR_ACTMON_ACTIVE_BORPS,
			     NVDEC_FALCON_UCLASS_METHOD_OFFSET);
		nvdec_writel(nvdec,
			     NVDEC_TFBIF_ACTMON_ACTIVE_BORPS_ACTIVE,
			     NVDEC_FALCON_UCLASS_METHOD_DATA);
	} else {
		nvdec_writel(nvdec,
			     NVDEC_TFBIF_ACTMON_ACTIVE_MASK_DELAYED |
			     NVDEC_TFBIF_ACTMON_ACTIVE_MASK_STALLED |
			     NVDEC_TFBIF_ACTMON_ACTIVE_MASK_STARVED,
			     NVDEC_TFBIF_ACTMON_ACTIVE_MASK);

		nvdec_writel(nvdec,
			     NVDEC_TFBIF_ACTMON_ACTIVE_BORPS_ACTIVE,
			     NVDEC_TFBIF_ACTMON_ACTIVE_BORPS);
	}
}

static void nvdec_count_weight_init(struct nvdec *nvdec, unsigned long rate)
{
	const struct nvdec_config *config = nvdec->config;
	struct host1x_client *client = &nvdec->client.base;
	u32 weight;

	host1x_actmon_update_client_rate(client, rate, &weight);

	if (!weight)
		return;

	if (!config->has_riscv) {
		nvdec_writel(nvdec, weight, NVDEC_TFBIF_ACTMON_ACTIVE_WEIGHT);
	} else {
		nvdec_writel(nvdec,
			     NVDEC_FW_MTHD_ADDR_ACTMON_ACTIVE_WEIGHT,
			     NVDEC_FALCON_UCLASS_METHOD_OFFSET);
		nvdec_writel(nvdec, weight, NVDEC_FALCON_UCLASS_METHOD_DATA);
	}
}

static __maybe_unused int nvdec_runtime_resume(struct device *dev)
{
	struct nvdec *nvdec = dev_get_drvdata(dev);
	int err;

	err = clk_bulk_prepare_enable(nvdec->num_clks, nvdec->clks);
	if (err < 0)
		return err;

	usleep_range(10, 20);

	if (nvdec->config->has_riscv) {
		err = nvdec_boot_riscv(nvdec);
		if (err < 0)
			goto disable;
	} else {
		err = nvdec_load_falcon_firmware(nvdec);
		if (err < 0)
			goto disable;

		err = nvdec_boot_falcon(nvdec);
		if (err < 0)
			goto disable;
	}

	/* Forcely set frequency as Fmax when device is resumed back */
	nvdec->devfreq->resume_freq = nvdec->devfreq->scaling_max_freq;
	err = devfreq_resume_device(nvdec->devfreq);
	if (err < 0)
		goto disable;

	nvdec_actmon_reg_init(nvdec);

	nvdec_count_weight_init(nvdec, nvdec->devfreq->scaling_max_freq);

	host1x_actmon_enable(&nvdec->client.base);

	return 0;

disable:
	clk_bulk_disable_unprepare(nvdec->num_clks, nvdec->clks);
	return err;
}

static __maybe_unused int nvdec_runtime_suspend(struct device *dev)
{
	struct nvdec *nvdec = dev_get_drvdata(dev);
	int err;

	err = devfreq_suspend_device(nvdec->devfreq);
	if (err < 0)
		return err;

	if (nvdec->icc_write) {
		err = icc_set_bw(nvdec->icc_write, 0, 0);
		if (err) {
			dev_warn(nvdec->dev, "failed to set icc bw: %d\n", err);
			goto devfreq_resume;
		}
	}

	clk_bulk_disable_unprepare(nvdec->num_clks, nvdec->clks);

	host1x_channel_stop(nvdec->channel);

	host1x_actmon_disable(&nvdec->client.base);

	return 0;

devfreq_resume:
	devfreq_resume_device(nvdec->devfreq);
	return err;
}

static int nvdec_open_channel(struct tegra_drm_client *client,
			    struct tegra_drm_context *context)
{
	struct nvdec *nvdec = to_nvdec(client);

	context->channel = host1x_channel_get(nvdec->channel);
	if (!context->channel)
		return -ENOMEM;

	return 0;
}

static void nvdec_close_channel(struct tegra_drm_context *context)
{
	host1x_channel_put(context->channel);
}

static int nvdec_can_use_memory_ctx(struct tegra_drm_client *client, bool *supported)
{
	*supported = true;

	return 0;
}

static int nvdec_has_job_timestamping(struct tegra_drm_client *client, bool *supported)
{
	struct nvdec *nvdec = to_nvdec(client);

	*supported = nvdec->config->supports_timestamping;

	return 0;
}

static const struct tegra_drm_client_ops nvdec_ops = {
	.open_channel = nvdec_open_channel,
	.close_channel = nvdec_close_channel,
	.submit = tegra_drm_submit,
	.get_streamid_offset = tegra_drm_get_streamid_offset_thi,
	.can_use_memory_ctx = nvdec_can_use_memory_ctx,
	.has_job_timestamping = nvdec_has_job_timestamping,
};

#define NVIDIA_TEGRA_210_NVDEC_FIRMWARE "nvidia/tegra210/nvdec.bin"

static const struct nvdec_config nvdec_t210_config = {
	.firmware = NVIDIA_TEGRA_210_NVDEC_FIRMWARE,
	.version = 0x21,
	.supports_sid = false,
};

#define NVIDIA_TEGRA_186_NVDEC_FIRMWARE "nvidia/tegra186/nvdec.bin"

static const struct nvdec_config nvdec_t186_config = {
	.firmware = NVIDIA_TEGRA_186_NVDEC_FIRMWARE,
	.version = 0x18,
	.supports_sid = true,
};

#define NVIDIA_TEGRA_194_NVDEC_FIRMWARE "nvidia/tegra194/nvdec.bin"

static const struct nvdec_config nvdec_t194_config = {
	.firmware = NVIDIA_TEGRA_194_NVDEC_FIRMWARE,
	.version = 0x19,
	.supports_sid = true,
	.supports_timestamping = true,
};

static const struct nvdec_config nvdec_t234_config = {
	.version = 0x23,
	.supports_sid = true,
	.supports_timestamping = true,
	.has_riscv = true,
	.has_extra_clocks = true,
};

static const struct of_device_id tegra_nvdec_of_match[] = {
	{ .compatible = "nvidia,tegra210-nvdec", .data = &nvdec_t210_config },
	{ .compatible = "nvidia,tegra186-nvdec", .data = &nvdec_t186_config },
	{ .compatible = "nvidia,tegra194-nvdec", .data = &nvdec_t194_config },
	{ .compatible = "nvidia,tegra234-nvdec", .data = &nvdec_t234_config },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_nvdec_of_match);

static int nvdec_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct host1x_syncpt **syncpts;
	struct nvdec *nvdec;
	u32 host_class;
	int err;

	/* inherit DMA mask from host1x parent */
	err = dma_coerce_mask_and_coherent(dev, *dev->parent->dma_mask);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to set DMA mask: %d\n", err);
		return err;
	}

	nvdec = devm_kzalloc(dev, sizeof(*nvdec), GFP_KERNEL);
	if (!nvdec)
		return -ENOMEM;

	nvdec->config = of_device_get_match_data(dev);

	syncpts = devm_kzalloc(dev, sizeof(*syncpts), GFP_KERNEL);
	if (!syncpts)
		return -ENOMEM;

	nvdec->regs = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(nvdec->regs))
		return PTR_ERR(nvdec->regs);

	nvdec->clks[0].id = "nvdec";
	nvdec->num_clks = 1;

	if (nvdec->config->has_extra_clocks) {
		nvdec->num_clks = 3;
		nvdec->clks[1].id = "fuse";
		nvdec->clks[2].id = "tsec_pka";
	}

	err = devm_clk_bulk_get(dev, nvdec->num_clks, nvdec->clks);
	if (err) {
		dev_err(&pdev->dev, "failed to get clock(s)\n");
		return err;
	}

	err = of_property_read_u32(dev->of_node, "nvidia,host1x-class", &host_class);
	if (err < 0)
		host_class = HOST1X_CLASS_NVDEC;

	if (nvdec->config->has_riscv) {
		struct tegra_mc *mc;

		mc = devm_tegra_memory_controller_get(dev);
		if (IS_ERR(mc)) {
			dev_err_probe(dev, PTR_ERR(mc),
				"failed to get memory controller handle\n");
			return PTR_ERR(mc);
		}

		{
			void __iomem *mc_addr = ioremap(0x02c10c0c, 0x8);
			nvdec->carveout_base |= (phys_addr_t)readl(mc_addr + 0);
			nvdec->carveout_base |= (phys_addr_t)readl(mc_addr + 4) << 32;
		}

		nvdec->reset = devm_reset_control_get_exclusive_released(dev, "nvdec");
		if (IS_ERR(nvdec->reset)) {
			dev_err_probe(dev, PTR_ERR(nvdec->reset), "failed to get reset\n");
			return PTR_ERR(nvdec->reset);
		}

		nvdec->riscv.dev = dev;
		nvdec->riscv.regs = nvdec->regs;

		nvdec->riscv.bl_desc.manifest_offset = 0x100;
		nvdec->riscv.bl_desc.data_offset = 0x900;
		nvdec->riscv.bl_desc.code_offset = 0x5500;
		nvdec->riscv.os_desc.manifest_offset = 0xc000;
		nvdec->riscv.os_desc.data_offset = 0xc800;
		nvdec->riscv.os_desc.code_offset = 0xe800;
	} else {
		nvdec->falcon.dev = dev;
		nvdec->falcon.regs = nvdec->regs;

		err = falcon_init(&nvdec->falcon);
		if (err < 0)
			return err;
	}

	nvdec->icc_write = devm_of_icc_get(dev, "write");
	if (IS_ERR(nvdec->icc_write)) {
		dev_err(&pdev->dev, "failed to get icc write handle\n");
		return PTR_ERR(nvdec->icc_write);
	}

	platform_set_drvdata(pdev, nvdec);

	INIT_LIST_HEAD(&nvdec->client.base.list);
	nvdec->client.base.ops = &nvdec_client_ops;
	nvdec->client.base.dev = dev;
	nvdec->client.base.class = host_class;
	nvdec->client.base.syncpts = syncpts;
	nvdec->client.base.num_syncpts = 1;
	nvdec->dev = dev;

	INIT_LIST_HEAD(&nvdec->client.list);
	nvdec->client.version = nvdec->config->version;
	nvdec->client.ops = &nvdec_ops;

	err = host1x_client_register(&nvdec->client.base);
	if (err < 0) {
		dev_err(dev, "failed to register host1x client: %d\n", err);
		goto exit_falcon;
	}

	err = host1x_actmon_register(&nvdec->client.base);
	if (err < 0)
		dev_info(&pdev->dev, "failed to register host1x actmon: %d\n", err);

	/* Set default clock rate for nvdec */
	err = clk_set_rate(nvdec->clks[0].clk, ULONG_MAX);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to set clock rate: %d\n", err);
		goto exit_actmon;
	}

	err = nvdec_devfreq_init(nvdec);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to init devfreq: %d\n", err);
		goto exit_actmon;
	}

	nvdec->hwpm.dev = dev;
	nvdec->hwpm.regs = nvdec->regs;
	tegra_drm_hwpm_register(&nvdec->hwpm, pdev->resource[0].start,
		TEGRA_DRM_HWPM_IP_NVDEC);

	pm_runtime_enable(dev);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_autosuspend_delay(dev, 500);

	return 0;

exit_actmon:
	host1x_actmon_unregister(&nvdec->client.base);
	host1x_client_unregister(&nvdec->client.base);
exit_falcon:
	falcon_exit(&nvdec->falcon);

	return err;
}

static int nvdec_remove(struct platform_device *pdev)
{
	struct nvdec *nvdec = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	tegra_drm_hwpm_unregister(&nvdec->hwpm, pdev->resource[0].start,
		TEGRA_DRM_HWPM_IP_NVDEC);

	nvdec_devfreq_deinit(nvdec);

	host1x_actmon_unregister(&nvdec->client.base);

	host1x_client_unregister(&nvdec->client.base);

	falcon_exit(&nvdec->falcon);

	return 0;
}

static const struct dev_pm_ops nvdec_pm_ops = {
	SET_RUNTIME_PM_OPS(nvdec_runtime_suspend, nvdec_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

struct platform_driver tegra_nvdec_driver = {
	.driver = {
		.name = "tegra-nvdec",
		.of_match_table = tegra_nvdec_of_match,
		.pm = &nvdec_pm_ops
	},
	.probe = nvdec_probe,
	.remove = nvdec_remove,
};

#if IS_ENABLED(CONFIG_ARCH_TEGRA_210_SOC)
MODULE_FIRMWARE(NVIDIA_TEGRA_210_NVDEC_FIRMWARE);
#endif
#if IS_ENABLED(CONFIG_ARCH_TEGRA_186_SOC)
MODULE_FIRMWARE(NVIDIA_TEGRA_186_NVDEC_FIRMWARE);
#endif
#if IS_ENABLED(CONFIG_ARCH_TEGRA_194_SOC)
MODULE_FIRMWARE(NVIDIA_TEGRA_194_NVDEC_FIRMWARE);
#endif
