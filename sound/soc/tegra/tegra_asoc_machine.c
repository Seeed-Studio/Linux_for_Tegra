// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2014-2025 NVIDIA CORPORATION. All rights reserved.
//
// tegra_asoc_machine.c - Tegra DAI links parser

#include <nvidia/conftest.h>

#include <linux/module.h>
#include <linux/of.h>
#include <linux/version.h>
#include <sound/simple_card_utils.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include "tegra_asoc_machine.h"

struct tegra_machine_control_data {
	struct snd_soc_pcm_runtime *rtd;
	unsigned int frame_mode;
	unsigned int master_mode;
};

static struct snd_soc_pcm_runtime *tegra_get_be(struct snd_soc_card *card,
						struct snd_soc_dapm_widget *widget,
						int stream)
{
	struct snd_soc_pcm_runtime *be;
	struct snd_soc_dapm_widget *w;
	struct snd_soc_dai *dai;
	int i;

	for_each_card_rtds(card, be) {
		for_each_rtd_dais(be, i, dai) {
			w = snd_soc_dai_get_widget(dai, stream);
			if (w == widget)
				return be;
		}
	}

	return NULL;
}

/*
 * In case of DPCM DAI link for i2s<->codec, note that the DAIs are
 * not directly mapped. The 'rtd' for codec is different in case of
 * a DPCM DAI link.
 */
static int dpcm_runtime_set_dai_fmt(struct snd_soc_pcm_runtime *rtd,
				    unsigned int fmt)
{
#if defined(NV_SND_SOC_RTD_TO_CODEC_PRESENT) /* Linux 6.7*/
	struct snd_soc_dai *dai = snd_soc_rtd_to_cpu(rtd, 0);
#else
	struct snd_soc_dai *dai = asoc_rtd_to_cpu(rtd, 0);
#endif
	struct snd_soc_dapm_widget_list *list;
	int stream = SNDRV_PCM_STREAM_PLAYBACK;
	struct snd_soc_pcm_runtime *be;
	struct snd_soc_dapm_widget *widget;
	int i, ret;

	/* nothing to be done if it is a normal sound card */
	if (!rtd->card->component_chaining)
		return 0;

	snd_soc_dapm_dai_get_connected_widgets(dai, stream, &list, NULL);

	for_each_dapm_widgets(list, i, widget) {
		if (widget->id != snd_soc_dapm_dai_in &&
		    widget->id != snd_soc_dapm_dai_out)
			continue;

		be = tegra_get_be(rtd->card, widget, stream);
		if (!be)
			continue;

		ret = snd_soc_runtime_set_dai_fmt(be, fmt);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int tegra_machine_codec_set_dai_fmt(struct snd_soc_pcm_runtime *rtd,
					   unsigned int frame_mode,
					   unsigned int master_mode)
{
	unsigned int fmt = rtd->dai_link->dai_fmt;
	int ret;

	if (frame_mode) {
		fmt &= ~SND_SOC_DAIFMT_FORMAT_MASK;
		fmt |= frame_mode;
	}

	if (master_mode) {
		fmt &= ~SND_SOC_DAIFMT_MASTER_MASK;
		master_mode <<= ffs(SND_SOC_DAIFMT_MASTER_MASK) - 1;

		if (master_mode == SND_SOC_DAIFMT_CBP_CFP)
			fmt |= SND_SOC_DAIFMT_CBP_CFP;
		else
			fmt |= SND_SOC_DAIFMT_CBC_CFC;
	}

	ret = snd_soc_runtime_set_dai_fmt(rtd, fmt);
	if (ret < 0)
		return ret;

	return dpcm_runtime_set_dai_fmt(rtd, fmt);
}

/*
 * The order of the below must not be changed as this
 * aligns with the SND_SOC_DAIFMT_XXX definitions in
 * include/sound/soc-dai.h.
 */
static const char * const tegra_machine_frame_mode_text[] = {
	"None",
	"i2s",
	"right-j",
	"left-j",
	"dsp-a",
	"dsp-b",
};

static int tegra_machine_codec_get_frame_mode(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tegra_machine_control_data *data = kcontrol->private_data;

	ucontrol->value.integer.value[0] = data->frame_mode;

	return 0;
}

static int tegra_machine_codec_put_frame_mode(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tegra_machine_control_data *data = kcontrol->private_data;
	int err;

	err = tegra_machine_codec_set_dai_fmt(data->rtd,
					      ucontrol->value.integer.value[0],
					      data->master_mode);
	if (err)
		return err;

	data->frame_mode = ucontrol->value.integer.value[0];

	return 0;
}

static const char * const tegra_machine_master_mode_text[] = {
	"None",
	"cbm-cfm",
	"cbs-cfs",
};

static int tegra_machine_codec_get_master_mode(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tegra_machine_control_data *data = kcontrol->private_data;

	ucontrol->value.integer.value[0] = data->master_mode;

	return 0;
}

static int tegra_machine_codec_put_master_mode(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct tegra_machine_control_data *data = kcontrol->private_data;
	int err;

	err = tegra_machine_codec_set_dai_fmt(data->rtd,
					      data->frame_mode,
					      ucontrol->value.integer.value[0]);
	if (err)
		return err;

	data->master_mode = ucontrol->value.integer.value[0];

	return 0;
}

static const struct soc_enum tegra_machine_codec_frame_mode =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(tegra_machine_frame_mode_text),
		tegra_machine_frame_mode_text);

static const struct soc_enum tegra_machine_codec_master_mode =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(tegra_machine_master_mode_text),
		tegra_machine_master_mode_text);

static int tegra_machine_add_ctl(struct snd_soc_card *card,
				 struct snd_kcontrol_new *knew,
				 void *private_data,
				 const unsigned char *name)
{
	struct snd_kcontrol *kctl;
	int ret;

	kctl = snd_ctl_new1(knew, private_data);
	if (!kctl)
		return -ENOMEM;

	ret = snd_ctl_add(card->snd_card, kctl);
	if (ret < 0)
		return ret;

	return 0;
}

static int tegra_machine_add_frame_mode_ctl(struct snd_soc_card *card,
	struct snd_soc_pcm_runtime *rtd, const unsigned char *name,
	struct tegra_machine_control_data *data)
{
	struct snd_kcontrol_new knew = {
		.iface		= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name		= name,
		.info		= snd_soc_info_enum_double,
		.index		= 0,
		.get		= tegra_machine_codec_get_frame_mode,
		.put		= tegra_machine_codec_put_frame_mode,
		.private_value	=
				(unsigned long)&tegra_machine_codec_frame_mode,
	};

	return tegra_machine_add_ctl(card, &knew, data, name);
}

static int tegra_machine_add_master_mode_ctl(struct snd_soc_card *card,
	struct snd_soc_pcm_runtime *rtd, const unsigned char *name,
	struct tegra_machine_control_data *data)
{
	struct snd_kcontrol_new knew = {
		.iface		= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name		= name,
		.info		= snd_soc_info_enum_double,
		.index		= 0,
		.get		= tegra_machine_codec_get_master_mode,
		.put		= tegra_machine_codec_put_master_mode,
		.private_value	=
				(unsigned long)&tegra_machine_codec_master_mode,
	};

	return tegra_machine_add_ctl(card, &knew, data, name);
}

int tegra_machine_add_i2s_codec_controls(struct snd_soc_card *card)
{
	struct tegra_machine_control_data *data;
	struct snd_soc_pcm_runtime *rtd;
	struct device_node *np;
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	unsigned int id;
	int ret;

	list_for_each_entry(rtd, &card->rtd_list, list) {
		np = rtd->dai_link->cpus->of_node;

		if (!np)
			continue;

		data = devm_kzalloc(card->dev, sizeof(*data), GFP_KERNEL);
		if (!data)
			return -ENOMEM;

		data->rtd = rtd;
		data->frame_mode = 0;
		data->master_mode = 0;

		if (of_property_read_u32(np, "nvidia,ahub-i2s-id", &id) < 0)
			continue;

		snprintf(name, sizeof(name), "I2S%d codec frame mode", id+1);

		ret = tegra_machine_add_frame_mode_ctl(card, rtd, name, data);
		if (ret)
			dev_warn(card->dev, "Failed to add control: %s!\n",
				 name);

		snprintf(name, sizeof(name), "I2S%d codec master mode", id+1);

		ret = tegra_machine_add_master_mode_ctl(card, rtd, name, data);
		if (ret) {
			dev_warn(card->dev, "Failed to add control: %s!\n",
				 name);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(tegra_machine_add_i2s_codec_controls);

/*
 * The order of the following definitions should align with
 * the 'snd_jack_types' enum as defined in include/sound/jack.h.
 */
static const char * const tegra_machine_jack_state_text[] = {
	"None",
	"HP",
	"MIC",
	"HS",
};

static const struct soc_enum tegra_machine_jack_state =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(tegra_machine_jack_state_text),
		tegra_machine_jack_state_text);

static int tegra_machine_codec_get_jack_state(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_jack *jack = kcontrol->private_data;

	ucontrol->value.integer.value[0] = jack->status;

	return 0;
}

static int tegra_machine_codec_put_jack_state(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_jack *jack = kcontrol->private_data;

	snd_soc_jack_report(jack, ucontrol->value.integer.value[0],
			    SND_JACK_HEADSET);

	return 0;
}

int tegra_machine_add_codec_jack_control(struct snd_soc_card *card,
					 struct snd_soc_pcm_runtime *rtd,
					 struct snd_soc_jack *jack)
{
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	struct snd_kcontrol_new knew = {
		.iface		= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name		= name,
		.info		= snd_soc_info_enum_double,
		.index		= 0,
		.get		= tegra_machine_codec_get_jack_state,
		.put		= tegra_machine_codec_put_jack_state,
		.private_value	= (unsigned long)&tegra_machine_jack_state,
	};

	if (rtd->dais[rtd->dai_link->num_cpus]->component->name_prefix)
		snprintf(name, sizeof(name), "%s Jack-state",
			 rtd->dais[rtd->dai_link->num_cpus]->component->name_prefix);
	else
		snprintf(name, sizeof(name), "Jack-state");

	return tegra_machine_add_ctl(card, &knew, jack, name);
}
EXPORT_SYMBOL_GPL(tegra_machine_add_codec_jack_control);

MODULE_DESCRIPTION("Tegra ASoC Machine Utility Code");
MODULE_LICENSE("GPL v2");
