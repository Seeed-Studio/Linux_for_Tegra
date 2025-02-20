// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/devfreq.h>
#include <linux/devfreq/tegra_wmark.h>
#include <linux/host1x-next.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include <nvidia/conftest.h>

#include <soc/tegra/fuse-helper.h>
#include <soc/tegra/pmc.h>
#include <soc/tegra/tegra-cbb.h>

#include "drm.h"
#include "falcon.h"
#include "util.h"
#include "vic.h"
#include "hwpm.h"

#define OFA_TFBIF_TRANSCFG		0x1444
#define OFA_TFBIF_ACTMON_ACTIVE_MASK	0x144c
#define OFA_TFBIF_ACTMON_ACTIVE_BORPS	0x1450
#define OFA_TFBIF_ACTMON_ACTIVE_WEIGHT	0x1454
#define OFA_SAFETY_RAM_INIT_REQ		0x3320
#define OFA_SAFETY_RAM_INIT_DONE	0x3324
#define OFA_SEC_INTF_CRC_CTRL           0xe000

#define OFA_TFBIF_ACTMON_ACTIVE_MASK_STARVED	BIT(0)
#define OFA_TFBIF_ACTMON_ACTIVE_MASK_STALLED	BIT(1)
#define OFA_TFBIF_ACTMON_ACTIVE_MASK_DELAYED	BIT(2)
#define OFA_TFBIF_ACTMON_ACTIVE_BORPS_ACTIVE	BIT(7)

struct ofa_config {
	const char *firmware;
	unsigned int version;
	bool has_safety_ram;
};

struct ofa {
	struct falcon falcon;
	struct tegra_drm_hwpm hwpm;

	void __iomem *regs;
	struct tegra_drm_client client;
	struct host1x_channel *channel;
	struct device *dev;
	struct clk *clk;
	struct devfreq *devfreq;
	struct devfreq_dev_profile *devfreq_profile;

	bool can_enable_crc;

	/* Platform configuration */
	const struct ofa_config *config;
};

static bool blf_write_allowed(u32 offset)
{
	void __iomem *regs = ioremap(0x13a10000 + offset, 12);
	u32 val;

	val = readl(regs + 0x8);
	if (!(val & 0x20000)) {
		iounmap(regs);
		return true;
	}

	val = readl(regs + 0x4);
	iounmap(regs);
	if (val & BIT(1))
		return true;

	return false;
}

static inline struct ofa *to_ofa(struct tegra_drm_client *client)
{
	return container_of(client, struct ofa, client);
}

static inline void ofa_writel(struct ofa *ofa, u32 value, unsigned int offset)
{
	writel(value, ofa->regs + offset);
}

static int ofa_boot(struct ofa *ofa)
{
	int err;
	u32 val;

	if (ofa->config->has_safety_ram) {
		ofa_writel(ofa, 0x1, OFA_SAFETY_RAM_INIT_REQ);
		err = readl_poll_timeout(ofa->regs + OFA_SAFETY_RAM_INIT_DONE, val, (val == 1),
					 100000, 10);
		if (err < 0) {
			dev_err(ofa->dev, "timeout while initializing safety RAM\n");
			return err;
		}
	}

	tegra_drm_program_iommu_regs(ofa->dev, ofa->regs, OFA_TFBIF_TRANSCFG);

	err = falcon_boot(&ofa->falcon);
	if (err < 0)
		return err;

	err = falcon_wait_idle(&ofa->falcon);
	if (err < 0) {
		dev_err(ofa->dev, "falcon boot timed out\n");
		return err;
	}

	return 0;
}

static void ofa_devfreq_update_wmark_threshold(struct devfreq *devfreq,
						 struct devfreq_tegra_wmark_config *cfg)
{
	struct ofa *ofa = dev_get_drvdata(devfreq->dev.parent);
	struct host1x_client *client = &ofa->client.base;

	host1x_actmon_update_active_wmark(client,
					  cfg->avg_upper_wmark,
					  cfg->avg_lower_wmark,
					  cfg->consec_upper_wmark,
					  cfg->consec_lower_wmark,
					  cfg->upper_wmark_enabled,
					  cfg->lower_wmark_enabled);
}

static int ofa_devfreq_target(struct device *dev, unsigned long *freq, u32 flags)
{
	struct ofa *ofa = dev_get_drvdata(dev);
	int err;

	err = clk_set_rate(ofa->clk, *freq);
	if (err < 0) {
		dev_err(dev, "failed to set clock rate\n");
		return err;
	}

	*freq = clk_get_rate(ofa->clk);

	return 0;
}

static int ofa_devfreq_get_dev_status(struct device *dev, struct devfreq_dev_status *stat)
{
	struct ofa *ofa = dev_get_drvdata(dev);
	struct host1x_client *client = &ofa->client.base;
	unsigned long usage;

	/* Update load information */
	host1x_actmon_read_active_norm(client, &usage);
	stat->total_time = 1000;
	stat->busy_time = usage;

	/* Update device frequency */
	stat->current_frequency = clk_get_rate(ofa->clk);

	return 0;
}

static int ofa_devfreq_get_cur_freq(struct device *dev, unsigned long *freq)
{
	struct ofa *ofa = dev_get_drvdata(dev);

	*freq = clk_get_rate(ofa->clk);

	return 0;
}

static int ofa_devfreq_init(struct ofa *ofa)
{
	unsigned long max_rate = clk_round_rate(ofa->clk, ULONG_MAX);
	unsigned long min_rate = clk_round_rate(ofa->clk, 0);
	unsigned long margin = clk_round_rate(ofa->clk, min_rate + 1) - min_rate;
	unsigned long rate = min_rate;
	struct devfreq_tegra_wmark_data *data;
	struct devfreq_dev_profile *devfreq_profile;
	struct devfreq *devfreq;

	while (rate <= max_rate) {
		dev_pm_opp_add(ofa->dev, rate, 0);
		rate += margin;
	}

	data = devm_kzalloc(ofa->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->event = DEVFREQ_TEGRA_AVG_WMARK_BELOW;
	data->update_wmark_threshold = ofa_devfreq_update_wmark_threshold;

	devfreq_profile = devm_kzalloc(ofa->dev, sizeof(*devfreq_profile), GFP_KERNEL);
	if (!devfreq_profile)
		return -ENOMEM;

	devfreq_profile->target = ofa_devfreq_target;
	devfreq_profile->get_dev_status = ofa_devfreq_get_dev_status;
	devfreq_profile->get_cur_freq = ofa_devfreq_get_cur_freq;
	devfreq_profile->initial_freq = max_rate;
	devfreq_profile->polling_ms = 100;

	devfreq = devm_devfreq_add_device(ofa->dev,
					  devfreq_profile,
					  DEVFREQ_GOV_USERSPACE,
					  data);
	if (IS_ERR(devfreq))
		return PTR_ERR(devfreq);

	ofa->devfreq = devfreq;

	return 0;
}

static void ofa_devfreq_deinit(struct ofa *ofa)
{
	if (!ofa->devfreq)
		return;

	devm_devfreq_remove_device(ofa->dev, ofa->devfreq);
	ofa->devfreq = NULL;
}

static int ofa_init(struct host1x_client *client)
{
	struct tegra_drm_client *drm = host1x_to_drm_client(client);
	struct drm_device *dev = dev_get_drvdata(client->host);
	struct tegra_drm *tegra = dev->dev_private;
	struct ofa *ofa = to_ofa(drm);
	int err;

	ofa->channel = host1x_channel_request(client);
	if (!ofa->channel) {
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
	host1x_channel_put(ofa->channel);
detach:
	host1x_client_iommu_detach(client);

	return err;
}

static int ofa_exit(struct host1x_client *client)
{
	struct tegra_drm_client *drm = host1x_to_drm_client(client);
	struct drm_device *dev = dev_get_drvdata(client->host);
	struct tegra_drm *tegra = dev->dev_private;
	struct ofa *ofa = to_ofa(drm);
	int err;

	/* avoid a dangling pointer just in case this disappears */
	client->dev->dma_parms = NULL;

	err = tegra_drm_unregister_client(tegra, drm);
	if (err < 0)
		return err;

	pm_runtime_dont_use_autosuspend(client->dev);
	pm_runtime_force_suspend(client->dev);

	host1x_syncpt_put(client->syncpts[0]);
	host1x_channel_put(ofa->channel);

	ofa->channel = NULL;

	dma_free_coherent(ofa->dev, ofa->falcon.firmware.size,
			  ofa->falcon.firmware.virt,
			  ofa->falcon.firmware.iova);

	return 0;
}

static unsigned long ofa_get_rate(struct host1x_client *client)
{
	struct ofa *ofa = dev_get_drvdata(client->dev);

	return clk_get_rate(ofa->clk);
}

static void ofa_actmon_event(struct host1x_client *client,
			     enum host1x_actmon_wmark_event event)
{
	struct ofa *ofa = dev_get_drvdata(client->dev);
	struct devfreq *df = ofa->devfreq;
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

static const struct host1x_client_ops ofa_client_ops = {
	.init = ofa_init,
	.exit = ofa_exit,
	.get_rate = ofa_get_rate,
	.actmon_event = ofa_actmon_event,
};

static int ofa_load_firmware(struct ofa *ofa)
{
	dma_addr_t iova;
	size_t size;
	void *virt;
	int err;

	if (ofa->falcon.firmware.virt)
		return 0;

	err = falcon_read_firmware(&ofa->falcon, ofa->config->firmware);
	if (err < 0)
		return err;

	size = ofa->falcon.firmware.size;

	virt = dma_alloc_coherent(ofa->dev, size, &iova, GFP_KERNEL);

	err = dma_mapping_error(ofa->dev, iova);
	if (err < 0)
		return err;

	ofa->falcon.firmware.virt = virt;
	ofa->falcon.firmware.iova = iova;

	err = falcon_load_firmware(&ofa->falcon);
	if (err < 0)
		goto cleanup;

	return 0;

cleanup:
	dma_free_coherent(ofa->dev, size, virt, iova);

	return err;
}

static void ofa_actmon_reg_init(struct ofa *ofa)
{
	ofa_writel(ofa,
		   OFA_TFBIF_ACTMON_ACTIVE_MASK_STARVED |
		   OFA_TFBIF_ACTMON_ACTIVE_MASK_STALLED |
		   OFA_TFBIF_ACTMON_ACTIVE_MASK_DELAYED,
		   OFA_TFBIF_ACTMON_ACTIVE_MASK);

	ofa_writel(ofa,
		   OFA_TFBIF_ACTMON_ACTIVE_BORPS_ACTIVE,
		   OFA_TFBIF_ACTMON_ACTIVE_BORPS);
}

static void ofa_count_weight_init(struct ofa *ofa, unsigned long rate)
{
	struct host1x_client *client = &ofa->client.base;
	u32 weight = 0;

	host1x_actmon_update_client_rate(client, rate, &weight);

	if (weight)
		ofa_writel(ofa, weight, OFA_TFBIF_ACTMON_ACTIVE_WEIGHT);
}

static __maybe_unused int ofa_runtime_resume(struct device *dev)
{
	struct ofa *ofa = dev_get_drvdata(dev);
	int err;

	err = clk_prepare_enable(ofa->clk);
	if (err < 0)
		return err;

	usleep_range(10, 20);

	err = ofa_load_firmware(ofa);
	if (err < 0)
		goto disable;

	err = ofa_boot(ofa);
	if (err < 0)
		goto disable;

	ofa->devfreq->resume_freq = ofa->devfreq->scaling_max_freq;
	err = devfreq_resume_device(ofa->devfreq);
	if (err < 0)
		goto disable;

	ofa_actmon_reg_init(ofa);

	ofa_count_weight_init(ofa, ofa->devfreq->scaling_max_freq);

	host1x_actmon_enable(&ofa->client.base);

	if (ofa->can_enable_crc)
		ofa_writel(ofa, 0x1, OFA_SEC_INTF_CRC_CTRL);

	return 0;

disable:
	clk_disable_unprepare(ofa->clk);
	return err;
}

static __maybe_unused int ofa_runtime_suspend(struct device *dev)
{
	struct ofa *ofa = dev_get_drvdata(dev);
	int err;

	err = devfreq_suspend_device(ofa->devfreq);
	if (err < 0)
		return err;

	host1x_channel_stop(ofa->channel);

	clk_disable_unprepare(ofa->clk);

	host1x_actmon_disable(&ofa->client.base);

	return 0;
}

static int ofa_open_channel(struct tegra_drm_client *client,
			    struct tegra_drm_context *context)
{
	struct ofa *ofa = to_ofa(client);
	int err;

	err = pm_runtime_get_sync(ofa->dev);
	if (err < 0) {
		pm_runtime_put(ofa->dev);
		return err;
	}

	context->channel = host1x_channel_get(ofa->channel);
	if (!context->channel) {
		pm_runtime_put(ofa->dev);
		return -ENOMEM;
	}

	return 0;
}

static void ofa_close_channel(struct tegra_drm_context *context)
{
	struct ofa *ofa = to_ofa(context->client);

	host1x_channel_put(context->channel);
	pm_runtime_put(ofa->dev);
}

static int ofa_can_use_memory_ctx(struct tegra_drm_client *client, bool *supported)
{
	*supported = true;

	return 0;
}

static int ofa_has_job_timestamping(struct tegra_drm_client *client, bool *supported,
				    u32 *timestamp_shift)
{
	*supported = true;
	*timestamp_shift = 5;

	return 0;
}

static const struct tegra_drm_client_ops ofa_ops = {
	.open_channel = ofa_open_channel,
	.close_channel = ofa_close_channel,
	.submit = tegra_drm_submit,
	.get_streamid_offset = tegra_drm_get_streamid_offset_thi,
	.can_use_memory_ctx = ofa_can_use_memory_ctx,
	.has_job_timestamping = ofa_has_job_timestamping,
};

#define NVIDIA_TEGRA_234_OFA_FIRMWARE "nvidia/tegra234/ofa.bin"

static const struct ofa_config ofa_t234_config = {
	.firmware = NVIDIA_TEGRA_234_OFA_FIRMWARE,
	.version = 0x23,
	.has_safety_ram = true,
};

static const struct of_device_id tegra_ofa_of_match[] = {
	{ .compatible = "nvidia,tegra234-ofa", .data = &ofa_t234_config },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_ofa_of_match);

static int ofa_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct host1x_syncpt **syncpts;
	struct ofa *ofa;
	int err;

	/* inherit DMA mask from host1x parent */
	err = dma_coerce_mask_and_coherent(dev, *dev->parent->dma_mask);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to set DMA mask: %d\n", err);
		return err;
	}

	ofa = devm_kzalloc(dev, sizeof(*ofa), GFP_KERNEL);
	if (!ofa)
		return -ENOMEM;

	ofa->config = of_device_get_match_data(dev);

	syncpts = devm_kzalloc(dev, sizeof(*syncpts), GFP_KERNEL);
	if (!syncpts)
		return -ENOMEM;

	ofa->regs = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(ofa->regs))
		return PTR_ERR(ofa->regs);

	ofa->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(ofa->clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		return PTR_ERR(ofa->clk);
	}

	err = clk_set_rate(ofa->clk, ULONG_MAX);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to set clock rate\n");
		return err;
	}

	ofa->falcon.dev = dev;
	ofa->falcon.regs = ofa->regs;

	err = falcon_init(&ofa->falcon);
	if (err < 0)
		return err;

	platform_set_drvdata(pdev, ofa);

	INIT_LIST_HEAD(&ofa->client.base.list);
	ofa->client.base.ops = &ofa_client_ops;
	ofa->client.base.dev = dev;
	ofa->client.base.class = 0xf8;
	ofa->client.base.syncpts = syncpts;
	ofa->client.base.num_syncpts = 1;
	ofa->dev = dev;

	INIT_LIST_HEAD(&ofa->client.list);
	ofa->client.version = ofa->config->version;
	ofa->client.ops = &ofa_ops;

	err = host1x_client_register(&ofa->client.base);
	if (err < 0) {
		dev_err(dev, "failed to register host1x client: %d\n", err);
		goto exit_falcon;
	}

	err = host1x_actmon_register(&ofa->client.base);
	if (err < 0)
		dev_info(dev, "failed to register host1x actmon: %d\n", err);

	err = ofa_devfreq_init(ofa);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to init devfreq: %d\n", err);
		goto exit_actmon;
	}

	ofa->can_enable_crc =
		!tegra_platform_is_silicon() ||
		(ofa->config == &ofa_t234_config && blf_write_allowed(0x6960));

	ofa->hwpm.dev = dev;
	ofa->hwpm.regs = ofa->regs;
	tegra_drm_hwpm_register(&ofa->hwpm, pdev->resource[0].start,
		TEGRA_DRM_HWPM_IP_OFA);

	pm_runtime_enable(dev);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_autosuspend_delay(dev, 500);

	return 0;

exit_actmon:
	host1x_actmon_unregister(&ofa->client.base);
	host1x_client_unregister(&ofa->client.base);
exit_falcon:
	falcon_exit(&ofa->falcon);

	return err;
}

static int ofa_remove(struct platform_device *pdev)
{
	struct ofa *ofa = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	ofa_devfreq_deinit(ofa);

	tegra_drm_hwpm_unregister(&ofa->hwpm, pdev->resource[0].start,
		TEGRA_DRM_HWPM_IP_OFA);

	host1x_actmon_unregister(&ofa->client.base);

	host1x_client_unregister(&ofa->client.base);

	falcon_exit(&ofa->falcon);

	return 0;
}

static const struct dev_pm_ops ofa_pm_ops = {
	SET_RUNTIME_PM_OPS(ofa_runtime_suspend, ofa_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void ofa_remove_wrapper(struct platform_device *pdev)
{
	ofa_remove(pdev);
}
#else
static int ofa_remove_wrapper(struct platform_device *pdev)
{
	return ofa_remove(pdev);
}
#endif

struct platform_driver tegra_ofa_driver = {
	.driver = {
		.name = "tegra-ofa",
		.of_match_table = tegra_ofa_of_match,
		.pm = &ofa_pm_ops
	},
	.probe = ofa_probe,
	.remove = ofa_remove_wrapper,
};

#if IS_ENABLED(CONFIG_ARCH_TEGRA_234_SOC)
MODULE_FIRMWARE(NVIDIA_TEGRA_234_OFA_FIRMWARE);
#endif
