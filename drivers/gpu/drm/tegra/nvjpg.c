// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2024, NVIDIA CORPORATION & AFFILIATES. All Rights Reserved.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/devfreq.h>
#include <linux/devfreq/tegra_wmark.h>
#include <linux/host1x-next.h>
#include <linux/interconnect.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/version.h>

#include <soc/tegra/pmc.h>

#include "drm.h"
#include "falcon.h"
#include "util.h"

#define NVJPG_TFBIF_TRANSCFG			0x1444
#define NVJPG_TFBIF_ACTMON_ACTIVE_MASK		0x144c
#define NVJPG_TFBIF_ACTMON_ACTIVE_BORPS		0x1450
#define NVJPG_TFBIF_ACTMON_ACTIVE_WEIGHT	0x1454
#define NVJPG_AXI_RW_BANDWIDTH			512

#define NVJPG_TFBIF_ACTMON_ACTIVE_MASK_STARVED	BIT(0)
#define NVJPG_TFBIF_ACTMON_ACTIVE_MASK_STALLED	BIT(1)
#define NVJPG_TFBIF_ACTMON_ACTIVE_MASK_DELAYED	BIT(2)
#define NVJPG_TFBIF_ACTMON_ACTIVE_BORPS_ACTIVE	BIT(7)

struct nvjpg_config {
	const char *firmware;
	unsigned int version;
	bool supports_sid;
	unsigned int num_instances;
};

struct nvjpg {
	struct falcon falcon;

	void __iomem *regs;
	struct tegra_drm_client client;
	struct host1x_channel *channel;
	struct device *dev;
	struct clk *clk;
	struct devfreq *devfreq;
	struct devfreq_dev_profile *devfreq_profile;
	struct icc_path *icc_write;

	/* Platform configuration */
	const struct nvjpg_config *config;
};

static inline struct nvjpg *to_nvjpg(struct tegra_drm_client *client)
{
	return container_of(client, struct nvjpg, client);
}

static inline void nvjpg_writel(struct nvjpg *nvjpg, u32 value, unsigned int offset)
{
	writel(value, nvjpg->regs + offset);
}

static int nvjpg_set_rate(struct nvjpg *nvjpg, unsigned long rate)
{
	unsigned long dev_rate;
	u32 emc_kbps;
	int err;

	err = clk_set_rate(nvjpg->clk, rate);
	if (err < 0)
		return err;

	if (pm_runtime_suspended(nvjpg->dev))
		return 0;

	dev_rate = clk_get_rate(nvjpg->clk);

	if (nvjpg->icc_write) {
		emc_kbps = dev_rate * NVJPG_AXI_RW_BANDWIDTH / 1024;
		err = icc_set_bw(nvjpg->icc_write, 0, kbps_to_icc(emc_kbps));
		if (err)
			dev_warn(nvjpg->dev, "failed to set icc bw: %d\n", err);
	}

	return 0;
}

static int nvjpg_boot(struct nvjpg *nvjpg)
{
	int err;

	if (nvjpg->config->supports_sid)
		tegra_drm_program_iommu_regs(nvjpg->dev, nvjpg->regs, NVJPG_TFBIF_TRANSCFG);

	err = falcon_boot(&nvjpg->falcon);
	if (err < 0)
		return err;

	err = falcon_wait_idle(&nvjpg->falcon);
	if (err < 0) {
		dev_err(nvjpg->dev, "falcon boot timed out\n");
		return err;
	}

	return 0;
}

static void nvjpg_devfreq_update_wmark_threshold(struct devfreq *devfreq,
						 struct devfreq_tegra_wmark_config *cfg)
{
	struct nvjpg *nvjpg = dev_get_drvdata(devfreq->dev.parent);
	struct host1x_client *client = &nvjpg->client.base;

	host1x_actmon_update_active_wmark(client,
					  cfg->avg_upper_wmark,
					  cfg->avg_lower_wmark,
					  cfg->consec_upper_wmark,
					  cfg->consec_lower_wmark,
					  cfg->upper_wmark_enabled,
					  cfg->lower_wmark_enabled);
}

static int nvjpg_devfreq_target(struct device *dev, unsigned long *freq, u32 flags)
{
	struct nvjpg *nvjpg = dev_get_drvdata(dev);
	int err;

	err = nvjpg_set_rate(nvjpg, *freq);
	if (err < 0) {
		dev_err(dev, "failed to set clock rate\n");
		return err;
	}

	*freq = clk_get_rate(nvjpg->clk);

	return 0;
}

static int nvjpg_devfreq_get_dev_status(struct device *dev, struct devfreq_dev_status *stat)
{
	struct nvjpg *nvjpg = dev_get_drvdata(dev);
	struct host1x_client *client = &nvjpg->client.base;
	unsigned long usage;

	/* Update load information */
	host1x_actmon_read_active_norm(client, &usage);
	stat->total_time = 1;
	stat->busy_time = usage;

	/* Update device frequency */
	stat->current_frequency = clk_get_rate(nvjpg->clk);

	return 0;
}

static int nvjpg_devfreq_get_cur_freq(struct device *dev, unsigned long *freq)
{
	struct nvjpg *nvjpg = dev_get_drvdata(dev);

	*freq = clk_get_rate(nvjpg->clk);

	return 0;
}

static int nvjpg_devfreq_init(struct nvjpg *nvjpg)
{
	unsigned long max_rate = clk_round_rate(nvjpg->clk, ULONG_MAX);
	unsigned long min_rate = clk_round_rate(nvjpg->clk, 0);
	unsigned long margin = clk_round_rate(nvjpg->clk, min_rate + 1) - min_rate;
	unsigned long rate = min_rate;
	struct devfreq_tegra_wmark_data *data;
	struct devfreq_dev_profile *devfreq_profile;
	struct devfreq *devfreq;

	while (rate <= max_rate) {
		dev_pm_opp_add(nvjpg->dev, rate, 0);
		rate += margin;
	}

	data = devm_kzalloc(nvjpg->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->event = DEVFREQ_TEGRA_AVG_WMARK_BELOW;
	data->update_wmark_threshold = nvjpg_devfreq_update_wmark_threshold;

	devfreq_profile = devm_kzalloc(nvjpg->dev, sizeof(*devfreq_profile), GFP_KERNEL);
	if (!devfreq_profile)
		return -ENOMEM;

	devfreq_profile->target = nvjpg_devfreq_target;
	devfreq_profile->get_dev_status = nvjpg_devfreq_get_dev_status;
	devfreq_profile->get_cur_freq = nvjpg_devfreq_get_cur_freq;
	devfreq_profile->initial_freq = max_rate;
	devfreq_profile->polling_ms = 100;

	devfreq = devm_devfreq_add_device(nvjpg->dev,
					  devfreq_profile,
					  DEVFREQ_GOV_USERSPACE,
					  data);
	if (IS_ERR(devfreq))
		return PTR_ERR(devfreq);

	nvjpg->devfreq = devfreq;

	return 0;
}

static void nvjpg_devfreq_deinit(struct nvjpg *nvjpg)
{
	if (!nvjpg->devfreq)
		return;

	devm_devfreq_remove_device(nvjpg->dev, nvjpg->devfreq);
	nvjpg->devfreq = NULL;
}

static int nvjpg_init(struct host1x_client *client)
{
	struct tegra_drm_client *drm = host1x_to_drm_client(client);
	struct drm_device *dev = dev_get_drvdata(client->host);
	struct tegra_drm *tegra = dev->dev_private;
	struct nvjpg *nvjpg = to_nvjpg(drm);
	int err;

	err = host1x_client_iommu_attach(client);
	if (err < 0 && err != -ENODEV) {
		dev_err(nvjpg->dev, "failed to attach to domain: %d\n", err);
		return err;
	}

	nvjpg->channel = host1x_channel_request(client);
	if (!nvjpg->channel) {
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
	host1x_channel_put(nvjpg->channel);
detach:
	host1x_client_iommu_detach(client);

	return err;
}

static int nvjpg_exit(struct host1x_client *client)
{
	struct tegra_drm_client *drm = host1x_to_drm_client(client);
	struct drm_device *dev = dev_get_drvdata(client->host);
	struct tegra_drm *tegra = dev->dev_private;
	struct nvjpg *nvjpg = to_nvjpg(drm);
	int err;

	/* avoid a dangling pointer just in case this disappears */
	client->dev->dma_parms = NULL;

	err = tegra_drm_unregister_client(tegra, drm);
	if (err < 0)
		return err;

	pm_runtime_dont_use_autosuspend(client->dev);
	pm_runtime_force_suspend(client->dev);

	host1x_syncpt_put(client->syncpts[0]);
	host1x_channel_put(nvjpg->channel);
	host1x_client_iommu_detach(client);

	nvjpg->channel = NULL;

	if (client->group) {
		dma_unmap_single(nvjpg->dev, nvjpg->falcon.firmware.phys,
				 nvjpg->falcon.firmware.size, DMA_TO_DEVICE);
		tegra_drm_free(tegra, nvjpg->falcon.firmware.size,
			       nvjpg->falcon.firmware.virt,
			       nvjpg->falcon.firmware.iova);
	} else {
		dma_free_coherent(nvjpg->dev, nvjpg->falcon.firmware.size,
				  nvjpg->falcon.firmware.virt,
				  nvjpg->falcon.firmware.iova);
	}

	return 0;
}

static unsigned long nvjpg_get_rate(struct host1x_client *client)
{
	struct platform_device *pdev = to_platform_device(client->dev);
	struct nvjpg *nvjpg = platform_get_drvdata(pdev);

	return clk_get_rate(nvjpg->clk);
}

static void nvjpg_actmon_event(struct host1x_client *client,
			     enum host1x_actmon_wmark_event event)
{
	struct platform_device *pdev = to_platform_device(client->dev);
	struct nvjpg *nvjpg = platform_get_drvdata(pdev);
	struct devfreq *df = nvjpg->devfreq;
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

static const struct host1x_client_ops nvjpg_client_ops = {
	.init = nvjpg_init,
	.exit = nvjpg_exit,
	.get_rate = nvjpg_get_rate,
	.actmon_event = nvjpg_actmon_event,
};

static int nvjpg_load_firmware(struct nvjpg *nvjpg)
{
	struct host1x_client *client = &nvjpg->client.base;
	struct tegra_drm *tegra = nvjpg->client.drm;
	dma_addr_t iova;
	size_t size;
	void *virt;
	int err;

	if (nvjpg->falcon.firmware.virt)
		return 0;

	err = falcon_read_firmware(&nvjpg->falcon, nvjpg->config->firmware);
	if (err < 0)
		return err;

	size = nvjpg->falcon.firmware.size;

	if (!client->group) {
		virt = dma_alloc_coherent(nvjpg->dev, size, &iova, GFP_KERNEL);

		err = dma_mapping_error(nvjpg->dev, iova);
		if (err < 0)
			return err;
	} else {
		virt = tegra_drm_alloc(tegra, size, &iova);
	}

	nvjpg->falcon.firmware.virt = virt;
	nvjpg->falcon.firmware.iova = iova;

	err = falcon_load_firmware(&nvjpg->falcon);
	if (err < 0)
		goto cleanup;

	/*
	 * In this case we have received an IOVA from the shared domain, so we
	 * need to make sure to get the physical address so that the DMA API
	 * knows what memory pages to flush the cache for.
	 */
	if (client->group) {
		dma_addr_t phys;

		phys = dma_map_single(nvjpg->dev, virt, size, DMA_TO_DEVICE);

		err = dma_mapping_error(nvjpg->dev, phys);
		if (err < 0)
			goto cleanup;

		nvjpg->falcon.firmware.phys = phys;
	}

	return 0;

cleanup:
	if (!client->group)
		dma_free_coherent(nvjpg->dev, size, virt, iova);
	else
		tegra_drm_free(tegra, size, virt, iova);

	return err;
}

static void nvjpg_actmon_reg_init(struct nvjpg *nvjpg)
{
	nvjpg_writel(nvjpg,
		     NVJPG_TFBIF_ACTMON_ACTIVE_MASK_STARVED |
		     NVJPG_TFBIF_ACTMON_ACTIVE_MASK_STALLED |
		     NVJPG_TFBIF_ACTMON_ACTIVE_MASK_DELAYED,
		     NVJPG_TFBIF_ACTMON_ACTIVE_MASK);

	nvjpg_writel(nvjpg,
		     NVJPG_TFBIF_ACTMON_ACTIVE_BORPS_ACTIVE,
		     NVJPG_TFBIF_ACTMON_ACTIVE_BORPS);
}

static void nvjpg_count_weight_init(struct nvjpg *nvjpg, unsigned long rate)
{
	struct host1x_client *client = &nvjpg->client.base;
	u32 weight = 0;

	host1x_actmon_update_client_rate(client, rate, &weight);

	if (weight)
		nvjpg_writel(nvjpg, weight, NVJPG_TFBIF_ACTMON_ACTIVE_WEIGHT);
}

static __maybe_unused int nvjpg_runtime_resume(struct device *dev)
{
	struct nvjpg *nvjpg = dev_get_drvdata(dev);
	int err;

	err = clk_prepare_enable(nvjpg->clk);
	if (err < 0)
		return err;

	usleep_range(10, 20);

	err = nvjpg_load_firmware(nvjpg);
	if (err < 0)
		goto disable;

	err = nvjpg_boot(nvjpg);
	if (err < 0)
		goto disable;

	/* Forcely set frequency as Fmax when device is resumed back */
	nvjpg->devfreq->resume_freq = nvjpg->devfreq->scaling_max_freq;
	err = devfreq_resume_device(nvjpg->devfreq);
	if (err < 0)
		goto disable;

	nvjpg_actmon_reg_init(nvjpg);

	nvjpg_count_weight_init(nvjpg, nvjpg->devfreq->scaling_max_freq);

	host1x_actmon_enable(&nvjpg->client.base);

	return 0;

disable:
	clk_disable_unprepare(nvjpg->clk);
	return err;
}

static __maybe_unused int nvjpg_runtime_suspend(struct device *dev)
{
	struct nvjpg *nvjpg = dev_get_drvdata(dev);
	int err;

	err = devfreq_suspend_device(nvjpg->devfreq);
	if (err < 0)
		return err;

	if (nvjpg->icc_write) {
		err = icc_set_bw(nvjpg->icc_write, 0, 0);
		if (err) {
			dev_warn(nvjpg->dev, "failed to set icc bw: %d\n", err);
			goto devfreq_resume;
		}
	}

	clk_disable_unprepare(nvjpg->clk);

	host1x_channel_stop(nvjpg->channel);

	host1x_actmon_disable(&nvjpg->client.base);

	return 0;

devfreq_resume:
	devfreq_resume_device(nvjpg->devfreq);
	return err;
}

static int nvjpg_open_channel(struct tegra_drm_client *client,
			    struct tegra_drm_context *context)
{
	struct nvjpg *nvjpg = to_nvjpg(client);
	int err;

	err = pm_runtime_get_sync(nvjpg->dev);
	if (err < 0) {
		pm_runtime_put(nvjpg->dev);
		return err;
	}

	context->channel = host1x_channel_get(nvjpg->channel);
	if (!context->channel) {
		pm_runtime_put(nvjpg->dev);
		return -ENOMEM;
	}

	return 0;
}

static void nvjpg_close_channel(struct tegra_drm_context *context)
{
	struct nvjpg *nvjpg = to_nvjpg(context->client);

	host1x_channel_put(context->channel);
	pm_runtime_put(nvjpg->dev);
}

static int nvjpg_can_use_memory_ctx(struct tegra_drm_client *client, bool *supported)
{
	*supported = true;

	return 0;
}

static const struct tegra_drm_client_ops nvjpg_ops = {
	.open_channel = nvjpg_open_channel,
	.close_channel = nvjpg_close_channel,
	.submit = tegra_drm_submit,
	.get_streamid_offset = tegra_drm_get_streamid_offset_thi,
	.can_use_memory_ctx = nvjpg_can_use_memory_ctx,
};

#define NVIDIA_TEGRA_210_NVJPG_FIRMWARE "nvidia/tegra210/nvjpg.bin"

static const struct nvjpg_config nvjpg_t210_config = {
	.firmware = NVIDIA_TEGRA_210_NVJPG_FIRMWARE,
	.version = 0x21,
	.supports_sid = false,
	.num_instances = 1,
};

#define NVIDIA_TEGRA_186_NVJPG_FIRMWARE "nvidia/tegra186/nvjpg.bin"

static const struct nvjpg_config nvjpg_t186_config = {
	.firmware = NVIDIA_TEGRA_186_NVJPG_FIRMWARE,
	.version = 0x18,
	.supports_sid = true,
	.num_instances = 1,
};

#define NVIDIA_TEGRA_194_NVJPG_FIRMWARE "nvidia/tegra194/nvjpg.bin"

static const struct nvjpg_config nvjpg_t194_config = {
	.firmware = NVIDIA_TEGRA_194_NVJPG_FIRMWARE,
	.version = 0x19,
	.supports_sid = true,
	.num_instances = 1,
};

#define NVIDIA_TEGRA_234_NVJPG_FIRMWARE "nvidia/tegra234/nvjpg.bin"

static const struct nvjpg_config nvjpg_t234_config = {
	.firmware = NVIDIA_TEGRA_234_NVJPG_FIRMWARE,
	.version = 0x23,
	.supports_sid = true,
	.num_instances = 2,
};

static const struct of_device_id tegra_nvjpg_of_match[] = {
	{ .compatible = "nvidia,tegra210-nvjpg", .data = &nvjpg_t210_config },
	{ .compatible = "nvidia,tegra186-nvjpg", .data = &nvjpg_t186_config },
	{ .compatible = "nvidia,tegra194-nvjpg", .data = &nvjpg_t194_config },
	{ .compatible = "nvidia,tegra234-nvjpg", .data = &nvjpg_t234_config },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_nvjpg_of_match);

static int nvjpg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct host1x_syncpt **syncpts;
	struct nvjpg *nvjpg;
	u32 host_class;
	int err;

	/* inherit DMA mask from host1x parent */
	err = dma_coerce_mask_and_coherent(dev, *dev->parent->dma_mask);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to set DMA mask: %d\n", err);
		return err;
	}

	nvjpg = devm_kzalloc(dev, sizeof(*nvjpg), GFP_KERNEL);
	if (!nvjpg)
		return -ENOMEM;

	nvjpg->config = of_device_get_match_data(dev);

	syncpts = devm_kzalloc(dev, sizeof(*syncpts), GFP_KERNEL);
	if (!syncpts)
		return -ENOMEM;

	nvjpg->regs = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(nvjpg->regs))
		return PTR_ERR(nvjpg->regs);

	nvjpg->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(nvjpg->clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		return PTR_ERR(nvjpg->clk);
	}

	err = of_property_read_u32(dev->of_node, "nvidia,host1x-class",
				   &host_class);
	if (err < 0)
		host_class = HOST1X_CLASS_NVJPG;

	nvjpg->falcon.dev = dev;
	nvjpg->falcon.regs = nvjpg->regs;

	err = falcon_init(&nvjpg->falcon);
	if (err < 0)
		return err;

	nvjpg->icc_write = devm_of_icc_get(dev, "write");
	if (IS_ERR(nvjpg->icc_write))
		return dev_err_probe(&pdev->dev, PTR_ERR(nvjpg->icc_write),
				     "failed to get icc write handle\n");

	platform_set_drvdata(pdev, nvjpg);

	INIT_LIST_HEAD(&nvjpg->client.base.list);
	nvjpg->client.base.ops = &nvjpg_client_ops;
	nvjpg->client.base.dev = dev;
	nvjpg->client.base.class = host_class;
	nvjpg->client.base.syncpts = syncpts;
	nvjpg->client.base.num_syncpts = 1;
	nvjpg->dev = dev;

	INIT_LIST_HEAD(&nvjpg->client.list);
	nvjpg->client.version = nvjpg->config->version;
	nvjpg->client.ops = &nvjpg_ops;

	err = host1x_client_register(&nvjpg->client.base);
	if (err < 0) {
		dev_err(dev, "failed to register host1x client: %d\n", err);
		goto exit_falcon;
	}

	err = host1x_actmon_register(&nvjpg->client.base);
	if (err < 0)
		dev_info(&pdev->dev, "failed to register host1x actmon: %d\n", err);

	/* Set default clock rate for nvjpg */
	err = clk_set_rate(nvjpg->clk, ULONG_MAX);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to set clock rate\n");
		goto exit_actmon;
	}

	err = nvjpg_devfreq_init(nvjpg);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to init devfreq: %d\n", err);
		goto exit_actmon;
	}

	pm_runtime_enable(dev);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_autosuspend_delay(dev, 500);

	return 0;

exit_actmon:
	host1x_actmon_unregister(&nvjpg->client.base);
	host1x_client_unregister(&nvjpg->client.base);
exit_falcon:
	falcon_exit(&nvjpg->falcon);

	return err;
}

static int nvjpg_remove(struct platform_device *pdev)
{
	struct nvjpg *nvjpg = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	nvjpg_devfreq_deinit(nvjpg);

	host1x_actmon_unregister(&nvjpg->client.base);

	host1x_client_unregister(&nvjpg->client.base);

	falcon_exit(&nvjpg->falcon);

	return 0;
}

static const struct dev_pm_ops nvjpg_pm_ops = {
	SET_RUNTIME_PM_OPS(nvjpg_runtime_suspend, nvjpg_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

struct platform_driver tegra_nvjpg_driver = {
	.driver = {
		.name = "tegra-nvjpg",
		.of_match_table = tegra_nvjpg_of_match,
		.pm = &nvjpg_pm_ops
	},
	.probe = nvjpg_probe,
	.remove = nvjpg_remove,
};

#if IS_ENABLED(CONFIG_ARCH_TEGRA_210_SOC)
MODULE_FIRMWARE(NVIDIA_TEGRA_210_NVJPG_FIRMWARE);
#endif
#if IS_ENABLED(CONFIG_ARCH_TEGRA_186_SOC)
MODULE_FIRMWARE(NVIDIA_TEGRA_186_NVJPG_FIRMWARE);
#endif
#if IS_ENABLED(CONFIG_ARCH_TEGRA_194_SOC)
MODULE_FIRMWARE(NVIDIA_TEGRA_194_NVJPG_FIRMWARE);
#endif
#if IS_ENABLED(CONFIG_ARCH_TEGRA_234_SOC)
MODULE_FIRMWARE(NVIDIA_TEGRA_234_NVJPG_FIRMWARE);
#endif
