// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
//
// tegra_mixer_control.c - Override DAI PCM parameters

/* The driver is just for audio usecase testing purpose and has few limitations as
 * mentioned below, which is fine looking at the overall requirements.
 *
 * - Client overrides are not possible for AHUB internal modules. This is
 *   because DAI hw_param() call can carry one configuration and thus both
 *   XBAR and client setting overrides are not possible.
 *
 * - No overrides are provided for ADMAIF. The client configuration is
 *   passed by aplay/arecord applications and DAI hw_param() call carries
 *   the same.

 * - The DAI overrides need to be set every time before any use case and
 *   these are not persistent. This is because when an use case ends ASoC
 *   core clears DAI runtime settings. If necessary, it can be improved
 *   later by storing all DAI settings in the driver.
 */

#include <nvidia/conftest.h>

#include <linux/device.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/version.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>

#include "tegra_asoc_machine.h"

#define TEGRA234_MAX_CHANNEL_COUNT	16
#define TEGRA264_MAX_CHANNEL_COUNT	32

struct tegra_mixer_soc_data {
	const struct snd_kcontrol_new *i2s_ctls;
	const struct snd_kcontrol_new *amx_ctls;
	const struct snd_kcontrol_new *adx_ctls;
};

static int dai_get_rate(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	int reg = mc->reg, i = 0;
	struct snd_soc_dai *dai;

	for_each_component_dais(component, dai) {
		if (i++ != reg)
			continue;

#if defined(NV_SND_SOC_DAI_STRUCT_HAS_SYMMETRIC_PREFIX) /* Linux v6.13 */
		ucontrol->value.integer.value[0] = dai->symmetric_rate;
#else
		ucontrol->value.integer.value[0] = dai->rate;
#endif

		break;
	}

	return 0;
}

static int dai_put_rate(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	int value = ucontrol->value.integer.value[0];
	int reg = mc->reg, i = 0;
	struct snd_soc_dai *dai;
	bool change = false;

	for_each_component_dais(component, dai) {
		if (i++ != reg)
			continue;

#if defined(NV_SND_SOC_DAI_STRUCT_HAS_SYMMETRIC_PREFIX) /* Linux v6.13 */
		dai->symmetric_rate = value;
#else
		dai->rate = value;
#endif

		change = true;
		break;
	}

	return change;
}

static int dai_get_channel(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	int reg = mc->reg, i = 0;
	struct snd_soc_dai *dai;

	for_each_component_dais(component, dai) {
		if (i++ != reg)
			continue;

#if defined(NV_SND_SOC_DAI_STRUCT_HAS_SYMMETRIC_PREFIX) /* Linux v6.13 */
		ucontrol->value.integer.value[0] = dai->symmetric_channels;
#else
		ucontrol->value.integer.value[0] = dai->channels;
#endif
		break;
	}

	return 0;
}

static int dai_put_channel(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	int value = ucontrol->value.integer.value[0];
	int reg = mc->reg, i = 0;
	struct snd_soc_dai *dai;
	bool change = false;

	for_each_component_dais(component, dai) {
		if (i++ != reg)
			continue;

#if defined(NV_SND_SOC_DAI_STRUCT_HAS_SYMMETRIC_PREFIX) /* Linux v6.13 */
		dai->symmetric_channels = value;
#else
		dai->channels = value;
#endif

		change = true;
		break;
	}

	return change;
}

static int dai_get_bits(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	int reg = mc->reg, i = 0;
	struct snd_soc_dai *dai;

	for_each_component_dais(component, dai) {
		if (i++ != reg)
			continue;

#if defined(NV_SND_SOC_DAI_STRUCT_HAS_SYMMETRIC_PREFIX) /* Linux v6.13 */
		ucontrol->value.integer.value[0] = dai->symmetric_sample_bits;
#else
		ucontrol->value.integer.value[0] = dai->sample_bits;
#endif
		break;
	}

	return 0;
}

static int dai_put_bits(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	int value = ucontrol->value.integer.value[0];
	int reg = mc->reg, i = 0;
	struct snd_soc_dai *dai;
	bool change = false;

	for_each_component_dais(component, dai) {
		if (i++ != reg)
			continue;

#if defined(NV_SND_SOC_DAI_STRUCT_HAS_SYMMETRIC_PREFIX) /* Linux v6.13 */
		dai->symmetric_sample_bits = value;
#else
		dai->sample_bits = value;
#endif

		change = true;
		break;
	}

	return change;
}

#define PCM_PARAMS_CONTROLS(reg, name_rate, name_ch, name_bits,	\
			    max_rate, max_ch, max_bits)		\
								\
	SOC_SINGLE_EXT(name_rate, reg, 0, max_rate, 0,		\
		       dai_get_rate, dai_put_rate),		\
	SOC_SINGLE_EXT(name_ch, reg, 0, max_ch, 0,		\
		       dai_get_channel, dai_put_channel),	\
	SOC_SINGLE_EXT(name_bits, reg, 0, max_bits, 0,		\
		       dai_get_bits, dai_put_bits)

#define TEGRA_I2S_CTLS(chip, channels) \
static const struct snd_kcontrol_new chip ## _i2s_ctls[] = {		\
	PCM_PARAMS_CONTROLS(1, "Sample Rate", "Sample Channels",	\
			    "Sample Bits", 192000, channels, 32),	\
};

TEGRA_I2S_CTLS(tegra210, TEGRA234_MAX_CHANNEL_COUNT)
TEGRA_I2S_CTLS(tegra264, TEGRA264_MAX_CHANNEL_COUNT)

static const struct snd_kcontrol_new tegra210_dmic_ctls[] = {
	PCM_PARAMS_CONTROLS(1, "Sample Rate", "Sample Channels",
			    "Sample Bits", 48000, 2, 32),
};

static const struct snd_kcontrol_new tegra186_dspk_ctls[] = {
	PCM_PARAMS_CONTROLS(1, "Sample Rate", "Sample Channels",
			    "Sample Bits", 48000, 2, 32),
};

static struct snd_kcontrol_new tegra210_sfc_ctls[] = {
	PCM_PARAMS_CONTROLS(0, "Input Sample Rate", "Input Sample Channels",
			    "Input Sample Bits", 192000, 2, 32),
	PCM_PARAMS_CONTROLS(1, "Output Sample Rate", "Output Sample Channels",
			    "Output Sample Bits", 192000, 2, 32),
};

static struct snd_kcontrol_new tegra210_mvc_ctls[] = {
	SOC_SINGLE_EXT("Sample Channels", 1, 0, 8, 0,
		dai_get_channel, dai_put_channel),
	SOC_SINGLE_EXT("Sample Bits", 1, 0, 32, 0,
		dai_get_bits, dai_put_bits),
};

static struct snd_kcontrol_new tegra210_mixer_ctls[] = {
	SOC_SINGLE_EXT("RX1 Sample Channels", 0, 0, 8, 0,
		dai_get_channel, dai_put_channel),
	SOC_SINGLE_EXT("RX2 Sample Channels", 1, 0, 8, 0,
		dai_get_channel, dai_put_channel),
	SOC_SINGLE_EXT("RX3 Sample Channels", 2, 0, 8, 0,
		dai_get_channel, dai_put_channel),
	SOC_SINGLE_EXT("RX4 Sample Channels", 3, 0, 8, 0,
		dai_get_channel, dai_put_channel),
	SOC_SINGLE_EXT("RX5 Sample Channels", 4, 0, 8, 0,
		dai_get_channel, dai_put_channel),
	SOC_SINGLE_EXT("RX6 Sample Channels", 5, 0, 8, 0,
		dai_get_channel, dai_put_channel),
	SOC_SINGLE_EXT("RX7 Sample Channels", 6, 0, 8, 0,
		dai_get_channel, dai_put_channel),
	SOC_SINGLE_EXT("RX8 Sample Channels", 7, 0, 8, 0,
		dai_get_channel, dai_put_channel),
	SOC_SINGLE_EXT("RX9 Sample Channels", 8, 0, 8, 0,
		dai_get_channel, dai_put_channel),
	SOC_SINGLE_EXT("RX10 Sample Channels", 9, 0, 8, 0,
		dai_get_channel, dai_put_channel),
	SOC_SINGLE_EXT("TX1 Sample Channels", 10, 0, 8, 0,
		dai_get_channel, dai_put_channel),
	SOC_SINGLE_EXT("TX2 Sample Channels", 11, 0, 8, 0,
		dai_get_channel, dai_put_channel),
	SOC_SINGLE_EXT("TX3 Sample Channels", 12, 0, 8, 0,
		dai_get_channel, dai_put_channel),
	SOC_SINGLE_EXT("TX4 Sample Channels", 13, 0, 8, 0,
		dai_get_channel, dai_put_channel),
	SOC_SINGLE_EXT("TX5 Sample Channels", 14, 0, 8, 0,
		dai_get_channel, dai_put_channel),
};

#define TEGRA210_AMX_OUTPUT_CHANNELS_CTRL(reg, channel)				\
	SOC_SINGLE_EXT("Output Sample Channels", reg - 1, 0, channel, 0,	\
		       dai_get_channel, dai_put_channel)

#define TEGRA210_AMX_INPUT_CHANNELS_CTRL(reg)				\
	SOC_SINGLE_EXT("Input" #reg " Sample Channels", reg - 1, 0,	\
		       16, 0, dai_get_channel, dai_put_channel)


#define TEGRA210_ADX_OUTPUT_CHANNELS_CTRL(reg)				\
	SOC_SINGLE_EXT("Output" #reg " Sample Channels", reg, 0,	\
		       16, 0, dai_get_channel, dai_put_channel)

#define TEGRA210_ADX_INPUT_CHANNELS_CTRL(reg, channel)				\
	SOC_SINGLE_EXT("Input Sample Channels", reg - 1, 0, channel, 0,	\
		       dai_get_channel, dai_put_channel)

#define TEGRA_AMX_ADX_CTRLS(chip, channels)		\
static struct snd_kcontrol_new chip ## _amx_ctls[] = {	\
	TEGRA210_AMX_INPUT_CHANNELS_CTRL(1),		\
	TEGRA210_AMX_INPUT_CHANNELS_CTRL(2),		\
	TEGRA210_AMX_INPUT_CHANNELS_CTRL(3),		\
	TEGRA210_AMX_INPUT_CHANNELS_CTRL(4),		\
	TEGRA210_AMX_OUTPUT_CHANNELS_CTRL(5, channels),	\
};	\
\
static struct snd_kcontrol_new chip ## _adx_ctls[] = {	\
	TEGRA210_ADX_INPUT_CHANNELS_CTRL(1, channels),	\
	TEGRA210_ADX_OUTPUT_CHANNELS_CTRL(1),		\
	TEGRA210_ADX_OUTPUT_CHANNELS_CTRL(2),		\
	TEGRA210_ADX_OUTPUT_CHANNELS_CTRL(3),		\
	TEGRA210_ADX_OUTPUT_CHANNELS_CTRL(4),		\
};

TEGRA_AMX_ADX_CTRLS(tegra210, TEGRA234_MAX_CHANNEL_COUNT)
TEGRA_AMX_ADX_CTRLS(tegra264, TEGRA264_MAX_CHANNEL_COUNT)

static int dai_is_dummy(struct snd_soc_dai *dai)
{
	if (!strcmp(dai->name, "snd-soc-dummy-dai"))
		return 1;

	return 0;
}

static void tegra_dai_fixup(struct snd_soc_dai *dai,
			   struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_mask *mask = hw_param_mask(params,
						SNDRV_PCM_HW_PARAM_FORMAT);
	unsigned int d_rate, d_channels, d_sample_bits;

#if defined(NV_SND_SOC_DAI_STRUCT_HAS_SYMMETRIC_PREFIX) /* Linux v6.13 */
	d_rate = dai->symmetric_rate;
	d_channels = dai->symmetric_channels;
	d_sample_bits = dai->symmetric_sample_bits;
#else
	d_rate = dai->rate;
	d_channels = dai->channels;
	d_sample_bits = dai->sample_bits;
#endif

	if (d_rate)
		rate->min = rate->max = d_rate;

	if (d_channels)
		channels->min = channels->max = d_channels;

	if (d_sample_bits) {
		/*
		 * Skip format update when S24_LE is requested and DAI sample_bits is 32.
		 * This handles the case where dai->sample_bits represents the physical
		 * width for S24_LE format, which is 32 bits.
		 */
		if (d_sample_bits == 32 && snd_mask_test(mask, SNDRV_PCM_FORMAT_S24_LE))
			return;

		snd_mask_none(mask);

		switch (d_sample_bits) {
		case 8:
			snd_mask_set(mask, SNDRV_PCM_FORMAT_S8);
			break;
		case 16:
			snd_mask_set(mask, SNDRV_PCM_FORMAT_S16_LE);
			break;
		case 24:
			snd_mask_set(mask, SNDRV_PCM_FORMAT_S24_LE);
			break;
		case 32:
			snd_mask_set(mask, SNDRV_PCM_FORMAT_S32_LE);
			break;
		default:
			break;
		}
	}
}

static int tegra_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_dai *dai;

	/* Fixup CPU DAI */
#if defined(NV_SND_SOC_RTD_TO_CODEC_PRESENT) /* Linux 6.7*/
	dai = snd_soc_rtd_to_cpu(rtd, 0);
#else
	dai = asoc_rtd_to_cpu(rtd, 0);
#endif

	if (!dai_is_dummy(dai))
		tegra_dai_fixup(dai, params);

	/* Fixup Codec DAI */
#if defined(NV_SND_SOC_RTD_TO_CODEC_PRESENT) /* Linux 6.7*/
	dai = snd_soc_rtd_to_codec(rtd, 0);
#else
	dai = asoc_rtd_to_codec(rtd, 0);
#endif

	if (!dai_is_dummy(dai))
		tegra_dai_fixup(dai, params);

	return 0;
}

static int tegra_mixer_control_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct snd_soc_card *card;
	struct snd_soc_component *component;
	struct snd_soc_pcm_runtime *rtd;
	const struct tegra_mixer_soc_data *soc_data;

	dev_info(dev, "Begin probe of override control device\n");

	soc_data = of_device_get_match_data(&pdev->dev);

	card = dev_get_drvdata(dev->parent);
	if (!card) {
		dev_err(dev, "Failed to get APE card reference\n");
		return -EINVAL;
	}

	dev_set_drvdata(dev, card);

	for_each_card_components(card, component) {
		if (!component->name_prefix)
			continue;

		if (strstr(component->name_prefix, "I2S"))
			snd_soc_add_component_controls(component,
					soc_data->i2s_ctls,
					ARRAY_SIZE(tegra210_i2s_ctls));
		else if (strstr(component->name_prefix, "DMIC"))
			snd_soc_add_component_controls(component,
				tegra210_dmic_ctls,
				ARRAY_SIZE(tegra210_dmic_ctls));
		else if (strstr(component->name_prefix, "DSPK"))
			snd_soc_add_component_controls(component,
				tegra186_dspk_ctls,
				ARRAY_SIZE(tegra186_dspk_ctls));
		else if (strstr(component->name_prefix, "SFC"))
			snd_soc_add_component_controls(component,
				tegra210_sfc_ctls,
				ARRAY_SIZE(tegra210_sfc_ctls));
		else if (strstr(component->name_prefix, "MVC"))
			snd_soc_add_component_controls(component,
				tegra210_mvc_ctls,
				ARRAY_SIZE(tegra210_mvc_ctls));
		else if (strstr(component->name_prefix, "AMX"))
			snd_soc_add_component_controls(component,
				soc_data->amx_ctls,
				ARRAY_SIZE(tegra210_amx_ctls));
		else if (strstr(component->name_prefix, "ADX"))
			snd_soc_add_component_controls(component,
				soc_data->adx_ctls,
				ARRAY_SIZE(tegra210_adx_ctls));
		else if (strstr(component->name_prefix, "MIXER"))
			snd_soc_add_component_controls(component,
				tegra210_mixer_ctls,
				ARRAY_SIZE(tegra210_mixer_ctls));
	}

	tegra_machine_add_i2s_codec_controls(card);

	/* Fixup callback for BE codec2codec links */
	for_each_card_rtds(card, rtd) {
		/* Skip FE links for sound card with DPCM routes for AHUB */
		if (rtd->card->component_chaining && !rtd->dai_link->no_pcm)
			continue;

		/* Skip FE links for sound card with codec2codc routes for AHUB  */
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		if (!rtd->card->component_chaining && !rtd->dai_link->c2c_params)
#else
		if (!rtd->card->component_chaining && !rtd->dai_link->params)
#endif
			continue;

		rtd->dai_link->be_hw_params_fixup = tegra_hw_params_fixup;
	}

	dev_info(dev, "Registered override controls for APE sound card\n");

	return 0;
}

static void tegra_mixer_control_delete(struct device *dev, const char *prefix,
		const struct snd_kcontrol_new *controls, int count)
{
	struct snd_soc_card *card = dev_get_drvdata(dev);
	struct snd_ctl_elem_id id;
	int i, err = 0;

	for (i = 0; i < count; i++) {
		const struct snd_kcontrol_new *control = &controls[i];

		snprintf(id.name, sizeof(id.name), "%s %s", prefix,
				control->name);

		id.numid = 0;
		id.iface = control->iface;
		id.device = control->device;
		id.subdevice = control->subdevice;
		id.index = control->index;
		err = snd_ctl_remove_id(card->snd_card, &id);
		if (err != 0)
			dev_err(dev, "Failed to remove control %s: %d\n", id.name, err);
	}
}

static int tegra_mixer_control_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct snd_soc_component *comp;
	struct snd_soc_card *card = dev_get_drvdata(dev);
	const struct tegra_mixer_soc_data *soc_data;

	soc_data = of_device_get_match_data(&pdev->dev);

	for_each_card_components(card, comp) {
		if (!comp->name_prefix)
			continue;

		if (strstr(comp->name_prefix, "I2S"))
			tegra_mixer_control_delete(dev, comp->name_prefix,
					soc_data->i2s_ctls, ARRAY_SIZE(tegra210_i2s_ctls));
		else if (strstr(comp->name_prefix, "DMIC"))
			tegra_mixer_control_delete(dev, comp->name_prefix,
					tegra210_dmic_ctls, ARRAY_SIZE(tegra210_dmic_ctls));
		else if (strstr(comp->name_prefix, "DSPK"))
			tegra_mixer_control_delete(dev, comp->name_prefix,
					tegra186_dspk_ctls, ARRAY_SIZE(tegra186_dspk_ctls));
		else if (strstr(comp->name_prefix, "SFC"))
			tegra_mixer_control_delete(dev, comp->name_prefix,
					tegra210_sfc_ctls, ARRAY_SIZE(tegra210_sfc_ctls));
		else if (strstr(comp->name_prefix, "MVC"))
			tegra_mixer_control_delete(dev, comp->name_prefix,
					tegra210_mvc_ctls, ARRAY_SIZE(tegra210_mvc_ctls));
		else if (strstr(comp->name_prefix, "AMX"))
			tegra_mixer_control_delete(dev, comp->name_prefix,
					soc_data->amx_ctls, ARRAY_SIZE(tegra210_amx_ctls));
		else if (strstr(comp->name_prefix, "ADX"))
			tegra_mixer_control_delete(dev, comp->name_prefix,
					soc_data->adx_ctls, ARRAY_SIZE(tegra210_adx_ctls));
		else if (strstr(comp->name_prefix, "MIXER"))
			tegra_mixer_control_delete(dev, comp->name_prefix,
					tegra210_mixer_ctls, ARRAY_SIZE(tegra210_mixer_ctls));
	}

	return 0;
}

static const struct tegra_mixer_soc_data soc_data_tegra234 = {
	.i2s_ctls = tegra210_i2s_ctls,
	.amx_ctls = tegra210_amx_ctls,
	.adx_ctls = tegra210_adx_ctls,
};

static const struct tegra_mixer_soc_data soc_data_tegra264 = {
	.i2s_ctls = tegra264_i2s_ctls,
	.amx_ctls = tegra264_amx_ctls,
	.adx_ctls = tegra264_adx_ctls,
};

static const struct dev_pm_ops tegra_mixer_control_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static const struct of_device_id tegra_mixer_control_of_match[] = {
	{ .compatible = "nvidia,tegra234-mixer-control", .data = &soc_data_tegra234 },
	{ .compatible = "nvidia,tegra264-mixer-control", .data = &soc_data_tegra264 },
	{},
};
MODULE_DEVICE_TABLE(of, tegra_mixer_control_of_match);

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void tegra_mixer_control_remove_wrapper(struct platform_device *pdev)
{
	tegra_mixer_control_remove(pdev);
}
#else
static int tegra_mixer_control_remove_wrapper(struct platform_device *pdev)
{
	return tegra_mixer_control_remove(pdev);
}
#endif

static struct platform_driver tegra_mixer_control_driver = {
	.driver = {
		.name = "tegra-mixer-controls",
		.of_match_table = tegra_mixer_control_of_match,
		.pm = &tegra_mixer_control_pm_ops,
	},
	.probe = tegra_mixer_control_probe,
	.remove = tegra_mixer_control_remove_wrapper,
};
module_platform_driver(tegra_mixer_control_driver);

MODULE_AUTHOR("Sameer Pujar <spujar@nvidia.com>");
MODULE_DESCRIPTION("Tegra Mixer Controls");
MODULE_LICENSE("GPL");
