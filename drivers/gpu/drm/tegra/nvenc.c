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
#include "hwpm.h"

#define NVENC_TFBIF_TRANSCFG			0x1844
#define NVENC_TFBIF_ACTMON_ACTIVE_MASK		0x184c
#define NVENC_TFBIF_ACTMON_ACTIVE_BORPS		0x1850
#define NVENC_TFBIF_ACTMON_ACTIVE_WEIGHT	0x1854
#define NVENC_AXI_RW_BANDWIDTH			512

#define NVENC_TFBIF_ACTMON_ACTIVE_MASK_STARVED	BIT(0)
#define NVENC_TFBIF_ACTMON_ACTIVE_MASK_STALLED	BIT(1)
#define NVENC_TFBIF_ACTMON_ACTIVE_MASK_DELAYED	BIT(2)
#define NVENC_TFBIF_ACTMON_ACTIVE_BORPS_ACTIVE	BIT(7)

struct nvenc_config {
	const char *firmware;
	unsigned int version;
	bool supports_sid;
	bool supports_timestamping;
	unsigned int num_instances;
};

struct nvenc {
	struct falcon falcon;
	struct tegra_drm_hwpm hwpm;

	void __iomem *regs;
	struct tegra_drm_client client;
	struct host1x_channel *channel;
	struct device *dev;
	struct clk *clk;
	struct devfreq *devfreq;
	struct devfreq_dev_profile *devfreq_profile;
	struct icc_path *icc_write;

	/* Platform configuration */
	const struct nvenc_config *config;
};

static inline struct nvenc *to_nvenc(struct tegra_drm_client *client)
{
	return container_of(client, struct nvenc, client);
}

static inline void nvenc_writel(struct nvenc *nvenc, u32 value, unsigned int offset)
{
	writel(value, nvenc->regs + offset);
}

static int nvenc_set_rate(struct nvenc *nvenc, unsigned long rate)
{
	unsigned long dev_rate;
	u32 emc_kbps;
	int err;

	err = clk_set_rate(nvenc->clk, rate);
	if (err < 0)
		return err;

	if (pm_runtime_suspended(nvenc->dev))
		return 0;

	dev_rate = clk_get_rate(nvenc->clk);

	if (nvenc->icc_write) {
		emc_kbps = dev_rate * NVENC_AXI_RW_BANDWIDTH / 1024;
		err = icc_set_bw(nvenc->icc_write, 0, kbps_to_icc(emc_kbps));
		if (err)
			dev_warn(nvenc->dev, "failed to set icc bw: %d\n", err);
	}

	return 0;
}

static int nvenc_boot(struct nvenc *nvenc)
{
	int err;

	if (nvenc->config->supports_sid)
		tegra_drm_program_iommu_regs(nvenc->dev, nvenc->regs, NVENC_TFBIF_TRANSCFG);

	err = falcon_boot(&nvenc->falcon);
	if (err < 0)
		return err;

	err = falcon_wait_idle(&nvenc->falcon);
	if (err < 0) {
		dev_err(nvenc->dev, "falcon boot timed out\n");
		return err;
	}

	return 0;
}

static void nvenc_devfreq_update_wmark_threshold(struct devfreq *devfreq,
						 struct devfreq_tegra_wmark_config *cfg)
{
	struct nvenc *nvenc = dev_get_drvdata(devfreq->dev.parent);
	struct host1x_client *client = &nvenc->client.base;

	host1x_actmon_update_active_wmark(client,
					  cfg->avg_upper_wmark,
					  cfg->avg_lower_wmark,
					  cfg->consec_upper_wmark,
					  cfg->consec_lower_wmark,
					  cfg->upper_wmark_enabled,
					  cfg->lower_wmark_enabled);
}

static int nvenc_devfreq_target(struct device *dev, unsigned long *freq, u32 flags)
{
	struct nvenc *nvenc = dev_get_drvdata(dev);
	int err;

	err = nvenc_set_rate(nvenc, *freq);
	if (err < 0) {
		dev_err(dev, "failed to set clock rate\n");
		return err;
	}

	*freq = clk_get_rate(nvenc->clk);

	return 0;
}

static int nvenc_devfreq_get_dev_status(struct device *dev, struct devfreq_dev_status *stat)
{
	struct nvenc *nvenc = dev_get_drvdata(dev);
	struct host1x_client *client = &nvenc->client.base;
	unsigned long usage;

	/* Update load information */
	host1x_actmon_read_active_norm(client, &usage);
	stat->total_time = 1;
	stat->busy_time = usage;

	/* Update device frequency */
	stat->current_frequency = clk_get_rate(nvenc->clk);

	return 0;
}

static int nvenc_devfreq_get_cur_freq(struct device *dev, unsigned long *freq)
{
	struct nvenc *nvenc = dev_get_drvdata(dev);

	*freq = clk_get_rate(nvenc->clk);

	return 0;
}

static int nvenc_devfreq_init(struct nvenc *nvenc)
{
	unsigned long max_rate = clk_round_rate(nvenc->clk, ULONG_MAX);
	unsigned long min_rate = clk_round_rate(nvenc->clk, 0);
	unsigned long margin = clk_round_rate(nvenc->clk, min_rate + 1) - min_rate;
	unsigned long rate = min_rate;
	struct devfreq_tegra_wmark_data *data;
	struct devfreq_dev_profile *devfreq_profile;
	struct devfreq *devfreq;

	while (rate <= max_rate) {
		dev_pm_opp_add(nvenc->dev, rate, 0);
		rate += margin;
	}

	data = devm_kzalloc(nvenc->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->event = DEVFREQ_TEGRA_AVG_WMARK_BELOW;
	data->update_wmark_threshold = nvenc_devfreq_update_wmark_threshold;

	devfreq_profile = devm_kzalloc(nvenc->dev, sizeof(*devfreq_profile), GFP_KERNEL);
	if (!devfreq_profile)
		return -ENOMEM;

	devfreq_profile->target = nvenc_devfreq_target;
	devfreq_profile->get_dev_status = nvenc_devfreq_get_dev_status;
	devfreq_profile->get_cur_freq = nvenc_devfreq_get_cur_freq;
	devfreq_profile->initial_freq = max_rate;
	devfreq_profile->polling_ms = 100;

	devfreq = devm_devfreq_add_device(nvenc->dev,
					  devfreq_profile,
					  DEVFREQ_GOV_USERSPACE,
					  data);
	if (IS_ERR(devfreq))
		return PTR_ERR(devfreq);

	nvenc->devfreq = devfreq;

	return 0;
}

static void nvenc_devfreq_deinit(struct nvenc *nvenc)
{
	if (!nvenc->devfreq)
		return;

	devm_devfreq_remove_device(nvenc->dev, nvenc->devfreq);
	nvenc->devfreq = NULL;
}

static int nvenc_init(struct host1x_client *client)
{
	struct tegra_drm_client *drm = host1x_to_drm_client(client);
	struct drm_device *dev = dev_get_drvdata(client->host);
	struct tegra_drm *tegra = dev->dev_private;
	struct nvenc *nvenc = to_nvenc(drm);
	int err;

	err = host1x_client_iommu_attach(client);
	if (err < 0 && err != -ENODEV) {
		dev_err(nvenc->dev, "failed to attach to domain: %d\n", err);
		return err;
	}

	nvenc->channel = host1x_channel_request(client);
	if (!nvenc->channel) {
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
	host1x_channel_put(nvenc->channel);
detach:
	host1x_client_iommu_detach(client);

	return err;
}

static int nvenc_exit(struct host1x_client *client)
{
	struct tegra_drm_client *drm = host1x_to_drm_client(client);
	struct drm_device *dev = dev_get_drvdata(client->host);
	struct tegra_drm *tegra = dev->dev_private;
	struct nvenc *nvenc = to_nvenc(drm);
	int err;

	/* avoid a dangling pointer just in case this disappears */
	client->dev->dma_parms = NULL;

	err = tegra_drm_unregister_client(tegra, drm);
	if (err < 0)
		return err;

	pm_runtime_dont_use_autosuspend(client->dev);
	pm_runtime_force_suspend(client->dev);

	host1x_syncpt_put(client->syncpts[0]);
	host1x_channel_put(nvenc->channel);
	host1x_client_iommu_detach(client);

	nvenc->channel = NULL;

	if (client->group) {
		dma_unmap_single(nvenc->dev, nvenc->falcon.firmware.phys,
				 nvenc->falcon.firmware.size, DMA_TO_DEVICE);
		tegra_drm_free(tegra, nvenc->falcon.firmware.size,
			       nvenc->falcon.firmware.virt,
			       nvenc->falcon.firmware.iova);
	} else {
		dma_free_coherent(nvenc->dev, nvenc->falcon.firmware.size,
				  nvenc->falcon.firmware.virt,
				  nvenc->falcon.firmware.iova);
	}

	return 0;
}

static unsigned long nvenc_get_rate(struct host1x_client *client)
{
	struct platform_device *pdev = to_platform_device(client->dev);
	struct nvenc *nvenc = platform_get_drvdata(pdev);

	return clk_get_rate(nvenc->clk);
}

static void nvenc_actmon_event(struct host1x_client *client,
			     enum host1x_actmon_wmark_event event)
{
	struct platform_device *pdev = to_platform_device(client->dev);
	struct nvenc *nvenc = platform_get_drvdata(pdev);
	struct devfreq *df = nvenc->devfreq;
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

static const struct host1x_client_ops nvenc_client_ops = {
	.init = nvenc_init,
	.exit = nvenc_exit,
	.get_rate = nvenc_get_rate,
	.actmon_event = nvenc_actmon_event,
};

static int nvenc_load_firmware(struct nvenc *nvenc)
{
	struct host1x_client *client = &nvenc->client.base;
	struct tegra_drm *tegra = nvenc->client.drm;
	dma_addr_t iova;
	size_t size;
	void *virt;
	int err;

	if (nvenc->falcon.firmware.virt)
		return 0;

	err = falcon_read_firmware(&nvenc->falcon, nvenc->config->firmware);
	if (err < 0)
		return err;

	size = nvenc->falcon.firmware.size;

	if (!client->group) {
		virt = dma_alloc_coherent(nvenc->dev, size, &iova, GFP_KERNEL);

		err = dma_mapping_error(nvenc->dev, iova);
		if (err < 0)
			return err;
	} else {
		virt = tegra_drm_alloc(tegra, size, &iova);
	}

	nvenc->falcon.firmware.virt = virt;
	nvenc->falcon.firmware.iova = iova;

	err = falcon_load_firmware(&nvenc->falcon);
	if (err < 0)
		goto cleanup;

	/*
	 * In this case we have received an IOVA from the shared domain, so we
	 * need to make sure to get the physical address so that the DMA API
	 * knows what memory pages to flush the cache for.
	 */
	if (client->group) {
		dma_addr_t phys;

		phys = dma_map_single(nvenc->dev, virt, size, DMA_TO_DEVICE);

		err = dma_mapping_error(nvenc->dev, phys);
		if (err < 0)
			goto cleanup;

		nvenc->falcon.firmware.phys = phys;
	}

	return 0;

cleanup:
	if (!client->group)
		dma_free_coherent(nvenc->dev, size, virt, iova);
	else
		tegra_drm_free(tegra, size, virt, iova);

	return err;
}

static void nvenc_actmon_reg_init(struct nvenc *nvenc)
{
	nvenc_writel(nvenc,
		     NVENC_TFBIF_ACTMON_ACTIVE_MASK_DELAYED |
		     NVENC_TFBIF_ACTMON_ACTIVE_MASK_STALLED |
		     NVENC_TFBIF_ACTMON_ACTIVE_MASK_STARVED,
		     NVENC_TFBIF_ACTMON_ACTIVE_MASK);

	nvenc_writel(nvenc,
		     NVENC_TFBIF_ACTMON_ACTIVE_BORPS_ACTIVE,
		     NVENC_TFBIF_ACTMON_ACTIVE_BORPS);
}

static void nvenc_count_weight_init(struct nvenc *nvenc, unsigned long rate)
{
	struct host1x_client *client = &nvenc->client.base;
	u32 weight = 0;

	host1x_actmon_update_client_rate(client, rate, &weight);

	if (weight)
		nvenc_writel(nvenc, weight, NVENC_TFBIF_ACTMON_ACTIVE_WEIGHT);
}

static __maybe_unused int nvenc_runtime_resume(struct device *dev)
{
	struct nvenc *nvenc = dev_get_drvdata(dev);
	int err;

	err = clk_prepare_enable(nvenc->clk);
	if (err < 0)
		return err;

	usleep_range(10, 20);

	err = nvenc_load_firmware(nvenc);
	if (err < 0)
		goto disable;

	err = nvenc_boot(nvenc);
	if (err < 0)
		goto disable;

	/* Forcely set frequency as Fmax when device is resumed back */
	nvenc->devfreq->resume_freq = nvenc->devfreq->scaling_max_freq;
	err = devfreq_resume_device(nvenc->devfreq);
	if (err < 0)
		goto disable;

	nvenc_actmon_reg_init(nvenc);

	nvenc_count_weight_init(nvenc, nvenc->devfreq->scaling_max_freq);

	host1x_actmon_enable(&nvenc->client.base);

	return 0;

disable:
	clk_disable_unprepare(nvenc->clk);
	return err;
}

static __maybe_unused int nvenc_runtime_suspend(struct device *dev)
{
	struct nvenc *nvenc = dev_get_drvdata(dev);
	int err;

	err = devfreq_suspend_device(nvenc->devfreq);
	if (err < 0)
		return err;

	if (nvenc->icc_write) {
		err = icc_set_bw(nvenc->icc_write, 0, 0);
		if (err) {
			dev_warn(nvenc->dev, "failed to set icc bw: %d\n", err);
			goto devfreq_resume;
		}
	}

	clk_disable_unprepare(nvenc->clk);

	host1x_channel_stop(nvenc->channel);

	host1x_actmon_disable(&nvenc->client.base);

	return 0;

devfreq_resume:
	devfreq_resume_device(nvenc->devfreq);
	return err;
}

static int nvenc_open_channel(struct tegra_drm_client *client,
			    struct tegra_drm_context *context)
{
	struct nvenc *nvenc = to_nvenc(client);
	int err;

	err = pm_runtime_get_sync(nvenc->dev);
	if (err < 0) {
		pm_runtime_put(nvenc->dev);
		return err;
	}

	context->channel = host1x_channel_get(nvenc->channel);
	if (!context->channel) {
		pm_runtime_put(nvenc->dev);
		return -ENOMEM;
	}

	return 0;
}

static void nvenc_close_channel(struct tegra_drm_context *context)
{
	struct nvenc *nvenc = to_nvenc(context->client);

	host1x_channel_put(context->channel);
	pm_runtime_put(nvenc->dev);
}

static int nvenc_can_use_memory_ctx(struct tegra_drm_client *client, bool *supported)
{
	*supported = true;

	return 0;
}

static int nvenc_has_job_timestamping(struct tegra_drm_client *client, bool *supported)
{
	struct nvenc *nvenc = to_nvenc(client);

	*supported = nvenc->config->supports_timestamping;

	return 0;
}

static const struct tegra_drm_client_ops nvenc_ops = {
	.open_channel = nvenc_open_channel,
	.close_channel = nvenc_close_channel,
	.submit = tegra_drm_submit,
	.get_streamid_offset = tegra_drm_get_streamid_offset_thi,
	.can_use_memory_ctx = nvenc_can_use_memory_ctx,
	.has_job_timestamping = nvenc_has_job_timestamping,
};

#define NVIDIA_TEGRA_210_NVENC_FIRMWARE "nvidia/tegra210/nvenc.bin"

static const struct nvenc_config nvenc_t210_config = {
	.firmware = NVIDIA_TEGRA_210_NVENC_FIRMWARE,
	.version = 0x21,
	.supports_sid = false,
	.num_instances = 1,
};

#define NVIDIA_TEGRA_186_NVENC_FIRMWARE "nvidia/tegra186/nvenc.bin"

static const struct nvenc_config nvenc_t186_config = {
	.firmware = NVIDIA_TEGRA_186_NVENC_FIRMWARE,
	.version = 0x18,
	.supports_sid = true,
	.num_instances = 1,
};

#define NVIDIA_TEGRA_194_NVENC_FIRMWARE "nvidia/tegra194/nvenc.bin"

static const struct nvenc_config nvenc_t194_config = {
	.firmware = NVIDIA_TEGRA_194_NVENC_FIRMWARE,
	.version = 0x19,
	.supports_sid = true,
	.supports_timestamping = true,
	.num_instances = 2,
};

#define NVIDIA_TEGRA_234_NVENC_FIRMWARE "nvidia/tegra234/nvenc.bin"

static const struct nvenc_config nvenc_t234_config = {
	.firmware = NVIDIA_TEGRA_234_NVENC_FIRMWARE,
	.version = 0x23,
	.supports_sid = true,
	.supports_timestamping = true,
	.num_instances = 1,
};

static const struct of_device_id tegra_nvenc_of_match[] = {
	{ .compatible = "nvidia,tegra210-nvenc", .data = &nvenc_t210_config },
	{ .compatible = "nvidia,tegra186-nvenc", .data = &nvenc_t186_config },
	{ .compatible = "nvidia,tegra194-nvenc", .data = &nvenc_t194_config },
	{ .compatible = "nvidia,tegra234-nvenc", .data = &nvenc_t234_config },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_nvenc_of_match);

static int nvenc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct host1x_syncpt **syncpts;
	struct nvenc *nvenc;
	u32 host_class;
	int err;

	/* inherit DMA mask from host1x parent */
	err = dma_coerce_mask_and_coherent(dev, *dev->parent->dma_mask);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to set DMA mask: %d\n", err);
		return err;
	}

	nvenc = devm_kzalloc(dev, sizeof(*nvenc), GFP_KERNEL);
	if (!nvenc)
		return -ENOMEM;

	nvenc->config = of_device_get_match_data(dev);

	syncpts = devm_kzalloc(dev, sizeof(*syncpts), GFP_KERNEL);
	if (!syncpts)
		return -ENOMEM;

	nvenc->regs = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(nvenc->regs))
		return PTR_ERR(nvenc->regs);

	nvenc->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(nvenc->clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		return PTR_ERR(nvenc->clk);
	}

	err = of_property_read_u32(dev->of_node, "nvidia,host1x-class",
				   &host_class);
	if (err < 0)
		host_class = HOST1X_CLASS_NVENC;

	nvenc->falcon.dev = dev;
	nvenc->falcon.regs = nvenc->regs;

	err = falcon_init(&nvenc->falcon);
	if (err < 0)
		return err;

	nvenc->icc_write = devm_of_icc_get(dev, "write");
	if (IS_ERR(nvenc->icc_write))
		return dev_err_probe(&pdev->dev, PTR_ERR(nvenc->icc_write),
				     "failed to get icc write handle\n");

	platform_set_drvdata(pdev, nvenc);

	INIT_LIST_HEAD(&nvenc->client.base.list);
	nvenc->client.base.ops = &nvenc_client_ops;
	nvenc->client.base.dev = dev;
	nvenc->client.base.class = host_class;
	nvenc->client.base.syncpts = syncpts;
	nvenc->client.base.num_syncpts = 1;
	nvenc->dev = dev;

	INIT_LIST_HEAD(&nvenc->client.list);
	nvenc->client.version = nvenc->config->version;
	nvenc->client.ops = &nvenc_ops;

	err = host1x_client_register(&nvenc->client.base);
	if (err < 0) {
		dev_err(dev, "failed to register host1x client: %d\n", err);
		goto exit_falcon;
	}

	err = host1x_actmon_register(&nvenc->client.base);
	if (err < 0)
		dev_info(&pdev->dev, "failed to register host1x actmon: %d\n", err);

	/* Set default clock rate for nvenc */
	err = clk_set_rate(nvenc->clk, ULONG_MAX);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to set clock rate\n");
		goto exit_actmon;
	}

	err = nvenc_devfreq_init(nvenc);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to init devfreq: %d\n", err);
		goto exit_actmon;
	}

	nvenc->hwpm.dev = dev;
	nvenc->hwpm.regs = nvenc->regs;
	tegra_drm_hwpm_register(&nvenc->hwpm, pdev->resource[0].start,
		TEGRA_DRM_HWPM_IP_NVENC);

	pm_runtime_enable(dev);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_autosuspend_delay(dev, 500);

	return 0;

exit_actmon:
	host1x_actmon_unregister(&nvenc->client.base);
	host1x_client_unregister(&nvenc->client.base);
exit_falcon:
	falcon_exit(&nvenc->falcon);

	return err;
}

static int nvenc_remove(struct platform_device *pdev)
{
	struct nvenc *nvenc = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	tegra_drm_hwpm_unregister(&nvenc->hwpm, pdev->resource[0].start,
		TEGRA_DRM_HWPM_IP_NVENC);

	nvenc_devfreq_deinit(nvenc);

	host1x_actmon_unregister(&nvenc->client.base);

	host1x_client_unregister(&nvenc->client.base);

	falcon_exit(&nvenc->falcon);

	return 0;
}

static const struct dev_pm_ops nvenc_pm_ops = {
	SET_RUNTIME_PM_OPS(nvenc_runtime_suspend, nvenc_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

struct platform_driver tegra_nvenc_driver = {
	.driver = {
		.name = "tegra-nvenc",
		.of_match_table = tegra_nvenc_of_match,
		.pm = &nvenc_pm_ops
	},
	.probe = nvenc_probe,
	.remove = nvenc_remove,
};

#if IS_ENABLED(CONFIG_ARCH_TEGRA_210_SOC)
MODULE_FIRMWARE(NVIDIA_TEGRA_210_NVENC_FIRMWARE);
#endif
#if IS_ENABLED(CONFIG_ARCH_TEGRA_186_SOC)
MODULE_FIRMWARE(NVIDIA_TEGRA_186_NVENC_FIRMWARE);
#endif
#if IS_ENABLED(CONFIG_ARCH_TEGRA_194_SOC)
MODULE_FIRMWARE(NVIDIA_TEGRA_194_NVENC_FIRMWARE);
#endif
#if IS_ENABLED(CONFIG_ARCH_TEGRA_234_SOC)
MODULE_FIRMWARE(NVIDIA_TEGRA_234_NVENC_FIRMWARE);
#endif
