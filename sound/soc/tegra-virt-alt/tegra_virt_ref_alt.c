// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <nvidia/conftest.h>

#include <linux/module.h>
#include <linux/version.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>

#include "tegra210_virt_alt_admaif.h"
#include "tegra_asoc_machine_virt_alt.h"
#include "tegra_asoc_util_virt_alt.h"
#include "tegra_asoc_xbar_virt_alt.h"
#include "tegra_virt_alt_ivc.h"

static struct snd_soc_card tegra_virt_t210ref_card = {
	.name = "t210ref-virt-card",
	.owner = THIS_MODULE,
	.fully_routed = true,
};

static struct snd_soc_card tegra_virt_t186ref_card = {
	.name = "t186ref-virt-card",
	.owner = THIS_MODULE,
	.fully_routed = true,
};

static void tegra_virt_set_dai_params(
		struct snd_soc_dai_link *dai_link,
		struct snd_soc_pcm_stream *user_params,
		unsigned int dai_id)
{
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
	dai_link[dai_id].c2c_params = user_params;
#else
	dai_link[dai_id].params = user_params;
#endif
}

static struct tegra_virt_admaif_soc_data soc_data_tegra186 = {
	.num_ch = TEGRA186_ADMAIF_CHANNEL_COUNT,
};


static const struct of_device_id tegra_virt_machine_of_match[] = {
	{ .compatible = "nvidia,tegra186-virt-pcm",
		.data = &soc_data_tegra186},
	{ .compatible = "nvidia,tegra234-virt-pcm-oot",
		.data = &soc_data_tegra186},
	{},
};

static int tegra_virt_machine_driver_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = NULL;
	int i, ret = 0;
	int admaif_ch_num = 0;
	bool adsp_enabled = false;
	unsigned int admaif_ch_list[MAX_ADMAIF_IDS];
	const struct of_device_id *match;
	struct tegra_virt_admaif_soc_data *soc_data;
	char buffer[30];
	int32_t adsp_admaif_bits, adsp_admaif_format;
	int32_t adsp_admaif_channels;
	struct snd_soc_pcm_stream adsp_admaif_dt_params = { };
	struct snd_soc_pcm_runtime *rtd;

	match = tegra_virt_machine_of_match;
	if (of_device_is_compatible(pdev->dev.of_node,
		"nvidia,tegra210-virt-pcm")) {
		card = &tegra_virt_t210ref_card;
		match = of_match_device(tegra_virt_machine_of_match,
						&pdev->dev);
		if (!match)
			return -ENODEV;
		soc_data = (struct tegra_virt_admaif_soc_data *)match->data;
	} else {
		card = &tegra_virt_t186ref_card;
		match = of_match_device(tegra_virt_machine_of_match,
						&pdev->dev);
		if (!match)
			return -ENODEV;
		soc_data = (struct tegra_virt_admaif_soc_data *)match->data;
	}

	card->dev = &pdev->dev;
	card->dai_link = tegra_virt_machine_get_dai_link();
	card->num_links = tegra_virt_machine_get_num_dai_links();
	adsp_enabled = of_property_read_bool(pdev->dev.of_node,
		"adsp_enabled");


	if (adsp_enabled) {
		dev_info(&pdev->dev, "virt-alt-pcm: adsp config is set\n");

		/* Get ADSP ADMAIF default param info */
		for (i = 0; i < MAX_ADMAIF_IDS; i++) {
			if ((sprintf(buffer, "adsp-admaif%d-channels", i + 1)) < 0)
				return -EINVAL;
			if (of_property_read_u32(pdev->dev.of_node,
					buffer, &adsp_admaif_channels))
				adsp_admaif_channels = 2;

			if ((sprintf(buffer, "adsp-admaif%d-bits", i + 1) < 0))
				return -EINVAL;
			if (of_property_read_u32(pdev->dev.of_node,
					buffer, &adsp_admaif_bits))
				adsp_admaif_bits = 16;

			if (adsp_admaif_channels > 16 || adsp_admaif_channels < 1) {
				dev_err(&pdev->dev,
					"unsupported channels %d for adsp admaif %d, setting default 2 channel\n",
					adsp_admaif_channels, i + 1);
				adsp_admaif_channels = 2;
			}

			switch (adsp_admaif_bits) {

			case 8:
				adsp_admaif_format = SNDRV_PCM_FMTBIT_S8;
				break;
			case 16:
				adsp_admaif_format = SNDRV_PCM_FMTBIT_S16_LE;
				break;
			case 32:
				adsp_admaif_format = SNDRV_PCM_FMTBIT_S32_LE;
				break;
			default:
				adsp_admaif_format = SNDRV_PCM_FMTBIT_S16_LE;
				dev_err(&pdev->dev,
					"unsupported bits %d for adsp admaif %d, setting default 16 bit\n",
					adsp_admaif_bits, i + 1);
				adsp_admaif_bits = 16;
				break;
			}
			adsp_admaif_dt_params.formats = adsp_admaif_format;
			adsp_admaif_dt_params.rate_min = 48000;
			adsp_admaif_dt_params.rate_max = 48000;
			adsp_admaif_dt_params.channels_min = adsp_admaif_channels;
			adsp_admaif_dt_params.channels_max = adsp_admaif_channels;

			tegra_virt_machine_set_adsp_admaif_dai_params(
					i, &adsp_admaif_dt_params);
		}
	} else {
		dev_info(&pdev->dev, "virt-alt-pcm: adsp config is not set\n");
		card->num_links = soc_data->num_ch;
	}

	if (tegra210_virt_admaif_register_component(pdev, soc_data)) {
		dev_err(&pdev->dev, "Failed register admaif component\n");
		return -EINVAL;
	}

	if (tegra_virt_xbar_register_codec(pdev)) {
		dev_err(&pdev->dev, "Failed register xbar component\n");
		ret = -EINVAL;
		goto undo_register_component;
	}

	if (of_property_read_u32(pdev->dev.of_node,
				"admaif_ch_num", &admaif_ch_num)) {
		dev_err(&pdev->dev, "number of admaif channels is not set\n");
		ret = -EINVAL;
		goto undo_register_codec;
	}

	if (of_property_read_string(pdev->dev.of_node, "cardname", &card->name))
		dev_warn(&pdev->dev, "Using default card name %s\n",
			card->name);

	if (admaif_ch_num > 0) {

		if (of_property_read_u32_array(pdev->dev.of_node,
						"admaif_ch_list",
						admaif_ch_list,
						admaif_ch_num)) {
			dev_err(&pdev->dev, "admaif_ch_list os not populated\n");
			ret = -EINVAL;
			goto undo_register_codec;
		}
		for (i = 0; i < admaif_ch_num; i++) {
			tegra_virt_set_dai_params(
					tegra_virt_machine_get_dai_link(),
					NULL,
					(admaif_ch_list[i] - 1));
		}
	}

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n",
			ret);
		ret = -EPROBE_DEFER;
		goto undo_register_codec;
	}

	list_for_each_entry(rtd, &card->rtd_list, list) {
#if defined(NV_SND_SOC_RTD_TO_CODEC_PRESENT) /* Linux 6.7*/
		struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
		struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
#else
		struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
		struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
#endif
		struct snd_soc_dai_driver *codec_drv = codec_dai->driver;
		struct snd_soc_dai_driver *cpu_drv = cpu_dai->driver;

		cpu_drv->playback.rates = SNDRV_PCM_RATE_KNOT;
		cpu_drv->playback.rate_min = 8000;
		cpu_drv->playback.rate_max = 192000;
		cpu_drv->capture.rates = SNDRV_PCM_RATE_KNOT;
		cpu_drv->capture.rate_min = 8000;
		cpu_drv->capture.rate_max = 192000;

		codec_drv->playback.rates = SNDRV_PCM_RATE_KNOT;
		codec_drv->playback.rate_min = 8000;
		codec_drv->playback.rate_max = 192000;
		codec_drv->capture.rates = SNDRV_PCM_RATE_KNOT;
		codec_drv->capture.rate_min = 8000;
		codec_drv->capture.rate_max = 192000;
	}

	pm_runtime_forbid(&pdev->dev);

	return 0;

undo_register_codec:
	snd_soc_unregister_component(&pdev->dev);
undo_register_component:
	tegra210_virt_admaif_unregister_component(pdev);

	nvaudio_ivc_free_ctxt(&pdev->dev);

	return ret;
}

static int tegra_virt_machine_driver_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

static struct platform_driver tegra_virt_machine_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table =
			of_match_ptr(tegra_virt_machine_of_match),
	},
	.probe = tegra_virt_machine_driver_probe,
	.remove = tegra_virt_machine_driver_remove,
};
module_platform_driver(tegra_virt_machine_driver);

MODULE_AUTHOR("Paresh Anandathirtha <paresha@nvidia.com>");
MODULE_DESCRIPTION("Tegra virt machine driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, tegra_virt_machine_of_match);
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_SOFTDEP("pre: snd_soc_tegra210_virt_alt_adsp");
