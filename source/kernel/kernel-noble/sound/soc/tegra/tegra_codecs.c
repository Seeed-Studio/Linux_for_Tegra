// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2021-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
//
// tegra_codecs.c - External audio codec setup

#include <linux/input.h>
#include <sound/jack.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/simple_card_utils.h>

#include "../../../sound/soc/codecs/rt5640.h"
#include "../../../sound/soc/codecs/rt5659.h"
#include "../../../sound/soc/codecs/sgtl5000.h"
#include "tegra_codecs.h"

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

static int tegra_machine_add_codec_jack_control(struct snd_soc_card *card,
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

static int tegra_machine_rt56xx_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *cmpnt;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_jack *jack;
	int err;

	cmpnt = rtd->dais[rtd->dai_link->num_cpus]->component;
	if (!cmpnt->driver->set_jack)
		goto dai_init;

	jack = devm_kzalloc(card->dev, sizeof(struct snd_soc_jack), GFP_KERNEL);
	if (!jack)
		return -ENOMEM;

	err = snd_soc_card_jack_new(card, "Headset Jack", SND_JACK_HEADSET,
				    jack);
	if (err) {
		dev_err(card->dev, "Headset Jack creation failed %d\n", err);
		return err;
	}

	err = tegra_machine_add_codec_jack_control(card, rtd, jack);
	if (err) {
		dev_err(card->dev, "Failed to add jack control: %d\n", err);
		return err;
	}

	err = cmpnt->driver->set_jack(cmpnt, jack, NULL);
	if (err) {
		dev_err(cmpnt->dev, "Failed to set jack: %d\n", err);
		return err;
	}

	/* single button supporting play/pause */
	snd_jack_set_key(jack->jack, SND_JACK_BTN_0, KEY_MEDIA);

	/* multiple buttons supporting play/pause and volume up/down */
	snd_jack_set_key(jack->jack, SND_JACK_BTN_1, KEY_MEDIA);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);

	snd_soc_dapm_sync(&card->dapm);


dai_init:
	return simple_util_dai_init(rtd);
}

static int tegra_machine_fepi_init(struct snd_soc_pcm_runtime *rtd)
{
	struct device *dev = rtd->card->dev;
	int err;

	err = snd_soc_dai_set_sysclk(rtd->dais[rtd->dai_link->num_cpus],
				     SGTL5000_SYSCLK, 12288000,
				     SND_SOC_CLOCK_IN);
	if (err) {
		dev_err(dev, "failed to set sgtl5000 sysclk!\n");
		return err;
	}

	return simple_util_dai_init(rtd);
}

static int tegra_machine_respeaker_init(struct snd_soc_pcm_runtime *rtd)
{
	struct device *dev = rtd->card->dev;
	int err;

	/* ac108 codec driver hardcodes the freq as 24000000
	 * and source as PLL irrespective of args passed through
	 * this callback
	 */
	err = snd_soc_dai_set_sysclk(rtd->dais[rtd->dai_link->num_cpus],
				     0, 24000000, SND_SOC_CLOCK_IN);
	if (err) {
		dev_err(dev, "failed to set ac108 sysclk!\n");
		return err;
	}

	return simple_util_dai_init(rtd);
}

static int set_pll_sysclk(struct snd_soc_pcm_runtime *rtd, int pll_src,
			  int clk_id, unsigned int srate,
			  unsigned int channels, unsigned int width,
			  unsigned int aud_mclk)
{
	unsigned int bclk_rate = srate * channels * width;
	int err;


	err = snd_soc_dai_set_pll(rtd->dais[rtd->dai_link->num_cpus], 0,
				  pll_src, bclk_rate, aud_mclk);
	if (err < 0) {
		dev_err(rtd->card->dev, "failed to set codec pll\n");
		return err;
	}

	err = snd_soc_dai_set_sysclk(rtd->dais[rtd->dai_link->num_cpus], clk_id,
				     aud_mclk, SND_SOC_CLOCK_IN);
	if (err < 0) {
		dev_err(rtd->card->dev, "dais[%d] clock not set\n",
			rtd->dai_link->num_cpus);
		return err;
	}

	return 0;
}

static struct snd_soc_pcm_runtime *get_pcm_runtime(struct snd_soc_card *card,
						   const char *link_name)
{
	struct snd_soc_pcm_runtime *rtd;

	for_each_card_rtds(card, rtd) {
		if (!strcmp(rtd->dai_link->name, link_name))
			return rtd;
	}

	return NULL;
}

static struct snd_soc_pcm_runtime *find_rtd(struct snd_soc_pcm_runtime *rtd,
					    char *link_name)
{
	struct snd_soc_pcm_runtime *vrtd;

	if (rtd->card->component_chaining) {
		if (!strcmp(rtd->dai_link->name, link_name))
			return rtd;
	} else {
		vrtd = get_pcm_runtime(rtd->card, link_name);
		if (vrtd)
			return vrtd;
	}

	return NULL;
}

int tegra_codecs_runtime_setup(struct snd_soc_pcm_runtime *rtd,
			       struct snd_pcm_hw_params *params)
{
	struct snd_soc_card *card = rtd->card;
	struct simple_util_priv *simple = snd_soc_card_get_drvdata(rtd->card);
	unsigned int channels = params_channels(params);
	unsigned int srate = params_rate(params);
	unsigned int width = params_width(params);
	struct snd_soc_pcm_runtime *vrtd;
	struct simple_dai_props *props;
	unsigned int aud_mclk;
	int err;


	vrtd = find_rtd(rtd, "rt565x-playback");
	if (vrtd) {
		props = simple_priv_to_props(simple, vrtd->num);
		aud_mclk = props->mclk_fs * srate;

		err = snd_soc_dai_set_sysclk(vrtd->dais[vrtd->dai_link->num_cpus],
					     RT5659_SCLK_S_MCLK,
					     aud_mclk, SND_SOC_CLOCK_IN);
		if (err < 0) {
			dev_err(card->dev, "dais[%d] clock not set\n",
				vrtd->dai_link->num_cpus);
			return err;
		}

	}

	vrtd = find_rtd(rtd, "rt5640-playback");
	if (vrtd) {
		props = simple_priv_to_props(simple, vrtd->num);
		aud_mclk = props->mclk_fs * srate;

		err = snd_soc_dai_set_sysclk(vrtd->dais[vrtd->dai_link->num_cpus],
					     RT5640_SCLK_S_MCLK,
					     aud_mclk, SND_SOC_CLOCK_IN);
		if (err < 0) {
			dev_err(card->dev, "dais[%d] clock not set\n",
				vrtd->dai_link->num_cpus);
			return err;
		}
	}

	vrtd = find_rtd(rtd, "rt565x-codec-sysclk-bclk1");
	if (vrtd) {
		props = simple_priv_to_props(simple, vrtd->num);
		aud_mclk = props->mclk_fs * srate;

		err = set_pll_sysclk(vrtd, RT5659_PLL1_S_BCLK1, RT5659_SCLK_S_PLL1,
				     srate, channels, width, aud_mclk);
		if (err < 0)
			return err;

	}

	vrtd = find_rtd(rtd, "rt5640-codec-sysclk-bclk1");
	if (vrtd) {
		props = simple_priv_to_props(simple, vrtd->num);
		aud_mclk = props->mclk_fs * srate;

		err = set_pll_sysclk(vrtd, RT5640_PLL1_S_BCLK1, RT5640_SCLK_S_PLL1,
				     srate, channels, width, aud_mclk);
		if (err < 0)
			return err;

	}

	return 0;
}

int tegra_codecs_init(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *dai_links = card->dai_link;
	int i;

	if (!dai_links || !card->num_links)
		return -EINVAL;

	for (i = 0; i < card->num_links; i++) {
		if (strstr(dai_links[i].name, "rt565x-playback") ||
		    strstr(dai_links[i].name, "rt5640-playback") ||
		    strstr(dai_links[i].name, "rt565x-codec-sysclk-bclk1") ||
		    strstr(dai_links[i].name, "rt5640-codec-sysclk-bclk1"))
			dai_links[i].init = tegra_machine_rt56xx_init;
		else if (strstr(dai_links[i].name, "fe-pi-audio-z-v2"))
			dai_links[i].init = tegra_machine_fepi_init;
		else if (strstr(dai_links[i].name, "respeaker-4-mic-array"))
			dai_links[i].init = tegra_machine_respeaker_init;
	}

	return 0;
}
