// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <nvidia/conftest.h>

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>

#include "tegra210_virt_alt_admaif.h"
#include "nvaudio_ivc/tegra_virt_alt_ivc.h"
#include "tegra_pcm_alt.h"
#include "tegra_asoc_xbar_virt_alt.h"
#include "tegra_asoc_util_virt_alt.h"

#define NUM_META_CONTROLS	3

static const unsigned int tegra210_rates[] = {
	8000, 11025, 12000, 16000, 22050,
	24000, 32000, 44100, 48000, 64000,
	88200, 96000, 176400, 192000
};

static const struct snd_pcm_hw_constraint_list tegra210_rate_constraints = {
	.count = ARRAY_SIZE(tegra210_rates),
	.list = tegra210_rates,
};

static struct nvaudio_ivc_ctxt *svd_cntxt;

static struct tegra210_admaif *admaif;
static int tegra210_admaif_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct device *dev = dai->dev;
	struct tegra210_virt_admaif_client_data *data =
				&admaif->client_data;
	struct tegra210_virt_audio_cif cif_conf;
	struct nvaudio_ivc_msg	msg;
	unsigned int value;
	unsigned int audio_bits_shift;
	unsigned int audio_ch_shift;
	unsigned int client_ch_shift;
	int err;

	memset(&cif_conf, 0, sizeof(struct tegra210_virt_audio_cif));
	cif_conf.audio_channels = params_channels(params);
	cif_conf.client_channels = params_channels(params);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		cif_conf.client_bits = TEGRA210_AUDIOCIF_BITS_8;
		cif_conf.audio_bits = TEGRA210_AUDIOCIF_BITS_8;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		cif_conf.client_bits = TEGRA210_AUDIOCIF_BITS_16;
		cif_conf.audio_bits = TEGRA210_AUDIOCIF_BITS_16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		cif_conf.client_bits = TEGRA210_AUDIOCIF_BITS_24;
		cif_conf.audio_bits = TEGRA210_AUDIOCIF_BITS_24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		cif_conf.client_bits = TEGRA210_AUDIOCIF_BITS_32;
		cif_conf.audio_bits = TEGRA210_AUDIOCIF_BITS_32;
		break;
	default:
		dev_err(dev, "Wrong format!\n");
		return -EINVAL;
	}
	cif_conf.direction = substream->stream;

	audio_bits_shift = (admaif->num_ch == TEGRA264_MAX_CHANNELS) ?
		TEGRA_32CH_ACIF_CTRL_AUDIO_BITS_SHIFT : TEGRA210_AUDIOCIF_CTRL_AUDIO_BITS_SHIFT;

	audio_ch_shift = (admaif->num_ch == TEGRA264_MAX_CHANNELS) ?
		TEGRA_32CH_ACIF_CTRL_AUDIO_CH_SHIFT : TEGRA210_AUDIOCIF_CTRL_AUDIO_CHANNELS_SHIFT;

	client_ch_shift = (admaif->num_ch == TEGRA264_MAX_CHANNELS) ?
		TEGRA_32CH_ACIF_CTRL_CLIENT_CH_SHIFT : TEGRA210_AUDIOCIF_CTRL_CLIENT_CHANNELS_SHIFT;

	value = (cif_conf.threshold <<
			TEGRA210_AUDIOCIF_CTRL_FIFO_THRESHOLD_SHIFT) |
		((cif_conf.audio_channels - 1) << audio_ch_shift) |
		((cif_conf.client_channels - 1) << client_ch_shift) |
		(cif_conf.audio_bits << audio_bits_shift) |
		(cif_conf.client_bits <<
			TEGRA210_AUDIOCIF_CTRL_CLIENT_BITS_SHIFT) |
		(cif_conf.expand <<
			TEGRA210_AUDIOCIF_CTRL_EXPAND_SHIFT) |
		(cif_conf.stereo_conv <<
			TEGRA210_AUDIOCIF_CTRL_STEREO_CONV_SHIFT) |
		(cif_conf.replicate <<
			TEGRA210_AUDIOCIF_CTRL_REPLICATE_SHIFT) |
		(cif_conf.truncate <<
			TEGRA210_AUDIOCIF_CTRL_TRUNCATE_SHIFT) |
		(cif_conf.mono_conv <<
			TEGRA210_AUDIOCIF_CTRL_MONO_CONV_SHIFT);

	memset(&msg, 0, sizeof(struct nvaudio_ivc_msg));
	msg.params.dmaif_info.id        = dai->id;
	msg.params.dmaif_info.value     = value;
	msg.ack_required = true;
	if (!cif_conf.direction)
		msg.cmd = NVAUDIO_DMAIF_SET_TXCIF;
	else
		msg.cmd = NVAUDIO_DMAIF_SET_RXCIF;

	err = nvaudio_ivc_send_receive(data->hivc_client,
				&msg,
				sizeof(struct nvaudio_ivc_msg));

	if (err < 0)
		pr_err("%s: error on ivc_send_receive\n", __func__);

	return 0;
}

static void tegra210_admaif_start_playback(struct snd_soc_dai *dai)
{
	struct tegra210_virt_admaif_client_data *data =
				&admaif->client_data;
	int err;
	struct nvaudio_ivc_msg msg;

	memset(&msg, 0, sizeof(struct nvaudio_ivc_msg));
	msg.cmd = NVAUDIO_START_PLAYBACK;
	msg.params.dmaif_info.id = dai->id;
	msg.ack_required = true;
	err = nvaudio_ivc_send_receive(data->hivc_client,
			&msg, sizeof(struct nvaudio_ivc_msg));

	if (err < 0)
		pr_err("%s: error on ivc_send\n", __func__);
}

static void tegra210_admaif_stop_playback(struct snd_soc_dai *dai)
{
	struct tegra210_virt_admaif_client_data *data =
				&admaif->client_data;
	int err;
	struct nvaudio_ivc_msg msg;

	memset(&msg, 0, sizeof(struct nvaudio_ivc_msg));
	msg.cmd = NVAUDIO_STOP_PLAYBACK;
	msg.params.dmaif_info.id = dai->id;

	msg.ack_required = true;
	err = nvaudio_ivc_send_receive(data->hivc_client,
			&msg, sizeof(struct nvaudio_ivc_msg));

	if (err < 0)
		pr_err("%s: error on ivc_send\n", __func__);
}

static void tegra210_admaif_start_capture(struct snd_soc_dai *dai)
{
	struct tegra210_virt_admaif_client_data *data =
				&admaif->client_data;
	int err;
	struct nvaudio_ivc_msg msg;

	memset(&msg, 0, sizeof(struct nvaudio_ivc_msg));
	msg.cmd = NVAUDIO_START_CAPTURE;
	msg.params.dmaif_info.id = dai->id;

	msg.ack_required = true;
	err = nvaudio_ivc_send_receive(data->hivc_client,
			&msg, sizeof(struct nvaudio_ivc_msg));

	if (err < 0)
		pr_err("%s: error on ivc_send\n", __func__);
}

static void tegra210_admaif_stop_capture(struct snd_soc_dai *dai)
{
	struct tegra210_virt_admaif_client_data *data =
				&admaif->client_data;
	int err;
	struct nvaudio_ivc_msg msg;

	memset(&msg, 0, sizeof(struct nvaudio_ivc_msg));
	msg.cmd = NVAUDIO_STOP_CAPTURE;
	msg.params.dmaif_info.id = dai->id;

	msg.ack_required = true;
	err = nvaudio_ivc_send_receive(data->hivc_client,
			&msg, sizeof(struct nvaudio_ivc_msg));
	if (err < 0)
		pr_err("%s: error on ivc_send\n", __func__);
}

static int tegra210_admaif_trigger(struct snd_pcm_substream *substream, int cmd,
				 struct snd_soc_dai *dai)
{
	int err;
	pr_info("Pcm trigger for admaif%d %s: cmd_id %d\n", dai->id+1,
		(substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		"playback":"capture", cmd);

	err = snd_dmaengine_pcm_trigger(substream, cmd);
	if (err)
		return err;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			tegra210_admaif_start_playback(dai);
		else
			tegra210_admaif_start_capture(dai);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			tegra210_admaif_stop_playback(dai);
		else
			tegra210_admaif_stop_capture(dai);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int tegra210_admaif_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *cpu_dai)
{
	return snd_pcm_hw_constraint_list(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_RATE, &tegra210_rate_constraints);
}

static int tegra210_admaif_dai_probe(struct snd_soc_dai *dai)
{
	snd_soc_dai_init_dma_data(dai, &admaif->playback_dma_data[dai->id],
				  &admaif->capture_dma_data[dai->id]);

	return 0;
}

static struct snd_soc_dai_ops tegra210_admaif_dai_ops = {
#if defined(NV_SND_SOC_DAI_OPS_STRUCT_HAS_PROBE_PRESENT) /* Linux 6.5 */
	.probe = tegra210_admaif_dai_probe,
#endif
	.hw_params	= tegra210_admaif_hw_params,
	.trigger	= tegra210_admaif_trigger,
	.startup	= tegra210_admaif_startup,
};

#if defined(NV_SND_SOC_DAI_OPS_STRUCT_HAS_PROBE_PRESENT) /* Linux 6.5 */
#define ADMAIF_DAI(id, channels)							\
	{							\
		.name = "ADMAIF" #id,				\
		.playback = {					\
			.stream_name = "Playback " #id,		\
			.channels_min = 1,			\
			.channels_max = channels,		\
			.rates = SNDRV_PCM_RATE_8000_192000,	\
			.formats = SNDRV_PCM_FMTBIT_S8 |	\
				SNDRV_PCM_FMTBIT_S16_LE |	\
				SNDRV_PCM_FMTBIT_S24_LE |	\
				SNDRV_PCM_FMTBIT_S32_LE,	\
		},						\
		.capture = {					\
			.stream_name = "Capture " #id,		\
			.channels_min = 1,			\
			.channels_max = channels,		\
			.rates = SNDRV_PCM_RATE_8000_192000,		\
			.formats = SNDRV_PCM_FMTBIT_S8 |		\
				SNDRV_PCM_FMTBIT_S16_LE |		\
				SNDRV_PCM_FMTBIT_S24_LE |		\
				SNDRV_PCM_FMTBIT_S32_LE,		\
		},						\
		.ops = &tegra210_admaif_dai_ops,			\
	}
#else
#define ADMAIF_DAI(id, channels)							\
	{							\
		.name = "ADMAIF" #id,				\
		.probe = tegra210_admaif_dai_probe,		\
		.playback = {					\
			.stream_name = "Playback " #id,		\
			.channels_min = 1,			\
			.channels_max = channels,		\
			.rates = SNDRV_PCM_RATE_8000_192000,	\
			.formats = SNDRV_PCM_FMTBIT_S8 |	\
				SNDRV_PCM_FMTBIT_S16_LE |	\
				SNDRV_PCM_FMTBIT_S24_LE |	\
				SNDRV_PCM_FMTBIT_S32_LE,	\
		},						\
		.capture = {					\
			.stream_name = "Capture " #id,		\
			.channels_min = 1,			\
			.channels_max = channels,			\
			.rates = SNDRV_PCM_RATE_8000_192000,		\
			.formats = SNDRV_PCM_FMTBIT_S8 |		\
				SNDRV_PCM_FMTBIT_S16_LE |		\
				SNDRV_PCM_FMTBIT_S24_LE |		\
				SNDRV_PCM_FMTBIT_S32_LE,		\
		},						\
		.ops = &tegra210_admaif_dai_ops,			\
	}
#endif


static struct snd_soc_dai_driver tegra210_admaif_dais[] = {
	ADMAIF_DAI(1, TEGRA186_MAX_CHANNELS),
	ADMAIF_DAI(2, TEGRA186_MAX_CHANNELS),
	ADMAIF_DAI(3, TEGRA186_MAX_CHANNELS),
	ADMAIF_DAI(4, TEGRA186_MAX_CHANNELS),
	ADMAIF_DAI(5, TEGRA186_MAX_CHANNELS),
	ADMAIF_DAI(6, TEGRA186_MAX_CHANNELS),
	ADMAIF_DAI(7, TEGRA186_MAX_CHANNELS),
	ADMAIF_DAI(8, TEGRA186_MAX_CHANNELS),
	ADMAIF_DAI(9, TEGRA186_MAX_CHANNELS),
	ADMAIF_DAI(10, TEGRA186_MAX_CHANNELS),
	ADMAIF_DAI(11, TEGRA186_MAX_CHANNELS),
	ADMAIF_DAI(12, TEGRA186_MAX_CHANNELS),
	ADMAIF_DAI(13, TEGRA186_MAX_CHANNELS),
	ADMAIF_DAI(14, TEGRA186_MAX_CHANNELS),
	ADMAIF_DAI(15, TEGRA186_MAX_CHANNELS),
	ADMAIF_DAI(16, TEGRA186_MAX_CHANNELS),
	ADMAIF_DAI(17, TEGRA186_MAX_CHANNELS),
	ADMAIF_DAI(18, TEGRA186_MAX_CHANNELS),
	ADMAIF_DAI(19, TEGRA186_MAX_CHANNELS),
	ADMAIF_DAI(20, TEGRA186_MAX_CHANNELS),
};

static struct snd_soc_dai_driver tegra264_admaif_dais[] = {
	ADMAIF_DAI(1, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(2, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(3, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(4, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(5, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(6, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(7, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(8, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(9, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(10, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(11, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(12, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(13, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(14, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(15, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(16, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(17, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(18, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(19, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(20, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(21, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(22, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(23, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(24, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(25, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(26, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(27, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(28, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(29, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(30, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(31, TEGRA264_MAX_CHANNELS),
	ADMAIF_DAI(32, TEGRA264_MAX_CHANNELS),
};

static const struct soc_enum tegra_virt_t186_asrc_source =
	SOC_ENUM_SINGLE_EXT(NUM_ASRC_MODE, tegra186_asrc_ratio_source_text);

static const struct soc_enum tegra_virt_t186_arad_source =
	SOC_VALUE_ENUM_SINGLE(0, 0, 0, NUM_ARAD_SOURCES,
					tegra186_arad_mux_text,
					tegra186_arad_mux_value);

static const struct soc_enum tegra_virt_t210_mvc_curvetype =
	SOC_ENUM_SINGLE_EXT(NUM_MVC_CURVETYPE, tegra210_mvc_curve_type_text);

static const struct snd_kcontrol_new tegra_virt_t186ref_controls[] = {
MIXER_GAIN_CTRL_DECL("MIXER1 RX1 Gain Volume", 0x00),
MIXER_GAIN_CTRL_DECL("MIXER1 RX2 Gain Volume", 0x01),
MIXER_GAIN_CTRL_DECL("MIXER1 RX3 Gain Volume", 0x02),
MIXER_GAIN_CTRL_DECL("MIXER1 RX4 Gain Volume", 0x03),
MIXER_GAIN_CTRL_DECL("MIXER1 RX5 Gain Volume", 0x04),
MIXER_GAIN_CTRL_DECL("MIXER1 RX6 Gain Volume", 0x05),
MIXER_GAIN_CTRL_DECL("MIXER1 RX7 Gain Volume", 0x06),
MIXER_GAIN_CTRL_DECL("MIXER1 RX8 Gain Volume", 0x07),
MIXER_GAIN_CTRL_DECL("MIXER1 RX9 Gain Volume", 0x08),
MIXER_GAIN_CTRL_DECL("MIXER1 RX10 Gain Volume", 0x09),

MIXER_GAIN_INSTANT_CTRL_DECL("MIXER1 RX1 Instant Gain Volume", 0x00),
MIXER_GAIN_INSTANT_CTRL_DECL("MIXER1 RX2 Instant Gain Volume", 0x01),
MIXER_GAIN_INSTANT_CTRL_DECL("MIXER1 RX3 Instant Gain Volume", 0x02),
MIXER_GAIN_INSTANT_CTRL_DECL("MIXER1 RX4 Instant Gain Volume", 0x03),
MIXER_GAIN_INSTANT_CTRL_DECL("MIXER1 RX5 Instant Gain Volume", 0x04),
MIXER_GAIN_INSTANT_CTRL_DECL("MIXER1 RX6 Instant Gain Volume", 0x05),
MIXER_GAIN_INSTANT_CTRL_DECL("MIXER1 RX7 Instant Gain Volume", 0x06),
MIXER_GAIN_INSTANT_CTRL_DECL("MIXER1 RX8 Instant Gain Volume", 0x07),
MIXER_GAIN_INSTANT_CTRL_DECL("MIXER1 RX9 Instant Gain Volume", 0x08),
MIXER_GAIN_INSTANT_CTRL_DECL("MIXER1 RX10 Instant Gain Volume", 0x09),

MIXER_DURATION_CTRL_DECL("MIXER1 RX1 Duration", 0x00),
MIXER_DURATION_CTRL_DECL("MIXER1 RX2 Duration", 0x01),
MIXER_DURATION_CTRL_DECL("MIXER1 RX3 Duration", 0x02),
MIXER_DURATION_CTRL_DECL("MIXER1 RX4 Duration", 0x03),
MIXER_DURATION_CTRL_DECL("MIXER1 RX5 Duration", 0x04),
MIXER_DURATION_CTRL_DECL("MIXER1 RX6 Duration", 0x05),
MIXER_DURATION_CTRL_DECL("MIXER1 RX7 Duration", 0x06),
MIXER_DURATION_CTRL_DECL("MIXER1 RX8 Duration", 0x07),
MIXER_DURATION_CTRL_DECL("MIXER1 RX9 Duration", 0x08),
MIXER_DURATION_CTRL_DECL("MIXER1 RX10 Duration", 0x09),

MIXER_SET_FADE("MIXER1 Fade", 0x00),
MIXER_GET_FADE_STATUS("MIXER1 Fade Status", 0x00),

SFC_IN_FREQ_CTRL_DECL("SFC1 Input Sample Rate", 0x00),
SFC_IN_FREQ_CTRL_DECL("SFC2 Input Sample Rate", 0x01),
SFC_IN_FREQ_CTRL_DECL("SFC3 Input Sample Rate", 0x02),
SFC_IN_FREQ_CTRL_DECL("SFC4 Input Sample Rate", 0x03),

SFC_OUT_FREQ_CTRL_DECL("SFC1 Output Sample Rate", 0x00),
SFC_OUT_FREQ_CTRL_DECL("SFC2 Output Sample Rate", 0x01),
SFC_OUT_FREQ_CTRL_DECL("SFC3 Output Sample Rate", 0x02),
SFC_OUT_FREQ_CTRL_DECL("SFC4 Output Sample Rate", 0x03),

MVC_CURVE_TYPE_CTRL_DECL("MVC1 Curve Type", 0x00,
			&tegra_virt_t210_mvc_curvetype),
MVC_CURVE_TYPE_CTRL_DECL("MVC2 Curve Type", 0x01,
			&tegra_virt_t210_mvc_curvetype),

MVC_TAR_VOL_CTRL_DECL("MVC1 Volume", 0x00),
MVC_TAR_VOL_CTRL_DECL("MVC2 Volume", 0x01),

MVC_MUTE_CTRL_DECL("MVC1 Mute", 0x00),
MVC_MUTE_CTRL_DECL("MVC2 Mute", 0x01),

ASRC_RATIO_CTRL_DECL("ASRC1 Ratio1", 0x01),
ASRC_RATIO_CTRL_DECL("ASRC1 Ratio2", 0x02),
ASRC_RATIO_CTRL_DECL("ASRC1 Ratio3", 0x03),
ASRC_RATIO_CTRL_DECL("ASRC1 Ratio4", 0x04),
ASRC_RATIO_CTRL_DECL("ASRC1 Ratio5", 0x05),
ASRC_RATIO_CTRL_DECL("ASRC1 Ratio6", 0x06),

ASRC_STREAM_RATIO_CTRL_DECL("ASRC1 Ratio1 Source", 0x01,
			&tegra_virt_t186_asrc_source),
ASRC_STREAM_RATIO_CTRL_DECL("ASRC1 Ratio2 Source", 0x02,
			&tegra_virt_t186_asrc_source),
ASRC_STREAM_RATIO_CTRL_DECL("ASRC1 Ratio3 Source", 0x03,
			&tegra_virt_t186_asrc_source),
ASRC_STREAM_RATIO_CTRL_DECL("ASRC1 Ratio4 Source", 0x04,
			&tegra_virt_t186_asrc_source),
ASRC_STREAM_RATIO_CTRL_DECL("ASRC1 Ratio5 Source", 0x05,
			&tegra_virt_t186_asrc_source),
ASRC_STREAM_RATIO_CTRL_DECL("ASRC1 Ratio6 Source", 0x06,
			&tegra_virt_t186_asrc_source),

ASRC_STREAM_HWCOMP_CTRL_DECL("ASRC1 Stream1 HW Component Disable", 0x01),
ASRC_STREAM_HWCOMP_CTRL_DECL("ASRC1 Stream2 HW Component Disable", 0x02),
ASRC_STREAM_HWCOMP_CTRL_DECL("ASRC1 Stream3 HW Component Disable", 0x03),
ASRC_STREAM_HWCOMP_CTRL_DECL("ASRC1 Stream4 HW Component Disable", 0x04),
ASRC_STREAM_HWCOMP_CTRL_DECL("ASRC1 Stream5 HW Component Disable", 0x05),
ASRC_STREAM_HWCOMP_CTRL_DECL("ASRC1 Stream6 HW Component Disable", 0x06),

ASRC_STREAM_INPUT_THRESHOLD_CTRL_DECL("ASRC1 Stream1 Input Threshold", 0x01),
ASRC_STREAM_INPUT_THRESHOLD_CTRL_DECL("ASRC1 Stream2 Input Threshold", 0x02),
ASRC_STREAM_INPUT_THRESHOLD_CTRL_DECL("ASRC1 Stream3 Input Threshold", 0x03),
ASRC_STREAM_INPUT_THRESHOLD_CTRL_DECL("ASRC1 Stream4 Input Threshold", 0x04),
ASRC_STREAM_INPUT_THRESHOLD_CTRL_DECL("ASRC1 Stream5 Input Threshold", 0x05),
ASRC_STREAM_INPUT_THRESHOLD_CTRL_DECL("ASRC1 Stream6 Input Threshold", 0x06),

ASRC_STREAM_OUTPUT_THRESHOLD_CTRL_DECL("ASRC1 Stream1 Output Threshold", 0x01),
ASRC_STREAM_OUTPUT_THRESHOLD_CTRL_DECL("ASRC1 Stream2 Output Threshold", 0x02),
ASRC_STREAM_OUTPUT_THRESHOLD_CTRL_DECL("ASRC1 Stream3 Output Threshold", 0x03),
ASRC_STREAM_OUTPUT_THRESHOLD_CTRL_DECL("ASRC1 Stream4 Output Threshold", 0x04),
ASRC_STREAM_OUTPUT_THRESHOLD_CTRL_DECL("ASRC1 Stream5 Output Threshold", 0x05),
ASRC_STREAM_OUTPUT_THRESHOLD_CTRL_DECL("ASRC1 Stream6 Output Threshold", 0x06),

#if TEGRA_ARAD
ARAD_LANE_SOURCE_CTRL_DECL("Numerator1 Mux", numerator1_enum,
				&tegra_virt_t186_arad_source),
ARAD_LANE_SOURCE_CTRL_DECL("Numerator2 Mux", numerator2_enum,
				&tegra_virt_t186_arad_source),
ARAD_LANE_SOURCE_CTRL_DECL("Numerator3 Mux", numerator3_enum,
				&tegra_virt_t186_arad_source),
ARAD_LANE_SOURCE_CTRL_DECL("Numerator4 Mux", numerator4_enum,
				&tegra_virt_t186_arad_source),
ARAD_LANE_SOURCE_CTRL_DECL("Numerator5 Mux", numerator5_enum,
				&tegra_virt_t186_arad_source),
ARAD_LANE_SOURCE_CTRL_DECL("Numerator6 Mux", numerator6_enum,
				&tegra_virt_t186_arad_source),

ARAD_LANE_SOURCE_CTRL_DECL("Denominator1 Mux", denominator1_enum,
				&tegra_virt_t186_arad_source),
ARAD_LANE_SOURCE_CTRL_DECL("Denominator2 Mux", denominator2_enum,
				&tegra_virt_t186_arad_source),
ARAD_LANE_SOURCE_CTRL_DECL("Denominator3 Mux", denominator3_enum,
				&tegra_virt_t186_arad_source),
ARAD_LANE_SOURCE_CTRL_DECL("Denominator4 Mux", denominator4_enum,
				&tegra_virt_t186_arad_source),
ARAD_LANE_SOURCE_CTRL_DECL("Denominator5 Mux", denominator5_enum,
				&tegra_virt_t186_arad_source),
ARAD_LANE_SOURCE_CTRL_DECL("Denominator6 Mux", denominator6_enum,
				&tegra_virt_t186_arad_source),

ARAD_LANE_PRESCALAR_CTRL_DECL("Numerator1 Prescalar", numerator1_enum),
ARAD_LANE_PRESCALAR_CTRL_DECL("Numerator2 Prescalar", numerator2_enum),
ARAD_LANE_PRESCALAR_CTRL_DECL("Numerator3 Prescalar", numerator3_enum),
ARAD_LANE_PRESCALAR_CTRL_DECL("Numerator4 Prescalar", numerator4_enum),
ARAD_LANE_PRESCALAR_CTRL_DECL("Numerator5 Prescalar", numerator5_enum),
ARAD_LANE_PRESCALAR_CTRL_DECL("Numerator6 Prescalar", numerator6_enum),

ARAD_LANE_PRESCALAR_CTRL_DECL("Denominator1 Prescalar", denominator1_enum),
ARAD_LANE_PRESCALAR_CTRL_DECL("Denominator2 Prescalar", denominator2_enum),
ARAD_LANE_PRESCALAR_CTRL_DECL("Denominator3 Prescalar", denominator3_enum),
ARAD_LANE_PRESCALAR_CTRL_DECL("Denominator4 Prescalar", denominator4_enum),
ARAD_LANE_PRESCALAR_CTRL_DECL("Denominator5 Prescalar", denominator5_enum),
ARAD_LANE_PRESCALAR_CTRL_DECL("Denominator6 Prescalar", denominator6_enum),

ARAD_LANE_ENABLE_CTRL_DECL("Lane1 enable", 0x00),
ARAD_LANE_ENABLE_CTRL_DECL("Lane2 enable", 0x01),
ARAD_LANE_ENABLE_CTRL_DECL("Lane3 enable", 0x02),
ARAD_LANE_ENABLE_CTRL_DECL("Lane4 enable", 0x03),
ARAD_LANE_ENABLE_CTRL_DECL("Lane5 enable", 0x04),
ARAD_LANE_ENABLE_CTRL_DECL("Lane6 enable", 0x05),

ARAD_LANE_RATIO_CTRL_DECL("Lane1 Ratio", 0x00),
ARAD_LANE_RATIO_CTRL_DECL("Lane2 Ratio", 0x01),
ARAD_LANE_RATIO_CTRL_DECL("Lane3 Ratio", 0x02),
ARAD_LANE_RATIO_CTRL_DECL("Lane4 Ratio", 0x03),
ARAD_LANE_RATIO_CTRL_DECL("Lane5 Ratio", 0x04),
ARAD_LANE_RATIO_CTRL_DECL("Lane6 Ratio", 0x05),
#endif

I2S_LOOPBACK_ENABLE_CTRL_DECL("I2S1 Loopback", 0x01),
I2S_LOOPBACK_ENABLE_CTRL_DECL("I2S2 Loopback", 0x02),
I2S_LOOPBACK_ENABLE_CTRL_DECL("I2S3 Loopback", 0x03),
I2S_LOOPBACK_ENABLE_CTRL_DECL("I2S4 Loopback", 0x04),
I2S_LOOPBACK_ENABLE_CTRL_DECL("I2S5 Loopback", 0x05),
I2S_LOOPBACK_ENABLE_CTRL_DECL("I2S6 Loopback", 0x06),

#if TEGRA_REGDUMP
REGDUMP_CTRL_DECL("ADMAIF1 regdump", ADMAIF1, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF2 regdump", ADMAIF2, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF3 regdump", ADMAIF3, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF4 regdump", ADMAIF4, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF5 regdump", ADMAIF5, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF6 regdump", ADMAIF6, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF7 regdump", ADMAIF7, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF8 regdump", ADMAIF8, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF9 regdump", ADMAIF9, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF10 regdump", ADMAIF10, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF11 regdump", ADMAIF11, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF12 regdump", ADMAIF12, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF13 regdump", ADMAIF13, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF14 regdump", ADMAIF14, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF15 regdump", ADMAIF15, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF16 regdump", ADMAIF16, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF17 regdump", ADMAIF17, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF18 regdump", ADMAIF18, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF19 regdump", ADMAIF19, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF20 regdump", ADMAIF20, 0, NVAUDIO_REGDUMP_RX_TX),

REGDUMP_CTRL_DECL("AMX1 regdump", AMX1, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("AMX2 regdump", AMX2, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("AMX3 regdump", AMX3, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("AMX4 regdump", AMX4, 0, NVAUDIO_REGDUMP_RX_TX),

REGDUMP_CTRL_DECL("ADX1 regdump", ADX1, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADX2 regdump", ADX2, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADX3 regdump", ADX3, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADX4 regdump", ADX4, 0, NVAUDIO_REGDUMP_RX_TX),

REGDUMP_CTRL_DECL("MIXER1-1 RX regdump", MIXER1, 0, NVAUDIO_REGDUMP_RX),
REGDUMP_CTRL_DECL("MIXER1-2 RX regdump", MIXER1, 1, NVAUDIO_REGDUMP_RX),
REGDUMP_CTRL_DECL("MIXER1-3 RX regdump", MIXER1, 2, NVAUDIO_REGDUMP_RX),
REGDUMP_CTRL_DECL("MIXER1-4 RX regdump", MIXER1, 3, NVAUDIO_REGDUMP_RX),
REGDUMP_CTRL_DECL("MIXER1-5 RX regdump", MIXER1, 4, NVAUDIO_REGDUMP_RX),
REGDUMP_CTRL_DECL("MIXER1-6 RX regdump", MIXER1, 5, NVAUDIO_REGDUMP_RX),
REGDUMP_CTRL_DECL("MIXER1-7 RX regdump", MIXER1, 6, NVAUDIO_REGDUMP_RX),
REGDUMP_CTRL_DECL("MIXER1-8 RX regdump", MIXER1, 7, NVAUDIO_REGDUMP_RX),
REGDUMP_CTRL_DECL("MIXER1-9 RX regdump", MIXER1, 8, NVAUDIO_REGDUMP_RX),
REGDUMP_CTRL_DECL("MIXER1-10 RX regdump", MIXER1, 9, NVAUDIO_REGDUMP_RX),

REGDUMP_CTRL_DECL("MIXER1-1 TX regdump", MIXER1, 0, NVAUDIO_REGDUMP_TX),
REGDUMP_CTRL_DECL("MIXER1-2 TX regdump", MIXER1, 1, NVAUDIO_REGDUMP_TX),
REGDUMP_CTRL_DECL("MIXER1-3 TX regdump", MIXER1, 2, NVAUDIO_REGDUMP_TX),
REGDUMP_CTRL_DECL("MIXER1-4 TX regdump", MIXER1, 3, NVAUDIO_REGDUMP_TX),
REGDUMP_CTRL_DECL("MIXER1-5 TX regdump", MIXER1, 4, NVAUDIO_REGDUMP_TX),

REGDUMP_CTRL_DECL("I2S1 regdump", I2S1, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("I2S2 regdump", I2S2, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("I2S3 regdump", I2S3, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("I2S4 regdump", I2S4, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("I2S5 regdump", I2S5, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("I2S6 regdump", I2S6, 0, NVAUDIO_REGDUMP_RX_TX),

REGDUMP_CTRL_DECL("ASRC1-1 regdump", ASRC1, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ASRC1-2 regdump", ASRC1, 1, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ASRC1-3 regdump", ASRC1, 2, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ASRC1-4 regdump", ASRC1, 3, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ASRC1-5 regdump", ASRC1, 4, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ASRC1-6 regdump", ASRC1, 5, NVAUDIO_REGDUMP_RX_TX),

REGDUMP_CTRL_DECL("SFC1 regdump", SFC1, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("SFC2 regdump", SFC2, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("SFC3 regdump", SFC3, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("SFC4 regdump", SFC4, 0, NVAUDIO_REGDUMP_RX_TX),

REGDUMP_CTRL_DECL("MVC1 regdump", MVC1, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("MVC2 regdump", MVC2, 0, NVAUDIO_REGDUMP_RX_TX),

REGDUMP_CTRL_DECL("ARAD1 Lane1 regdump", ARAD1, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ARAD1 Lane2 regdump", ARAD1, 1, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ARAD1 Lane3 regdump", ARAD1, 2, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ARAD1 Lane4 regdump", ARAD1, 3, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ARAD1 Lane5 regdump", ARAD1, 4, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ARAD1 Lane6 regdump", ARAD1, 5, NVAUDIO_REGDUMP_RX_TX),

ADMA_REGDUMP_CTRL_DECL("ADMA1 regdump", 1),
ADMA_REGDUMP_CTRL_DECL("ADMA2 regdump", 2),
ADMA_REGDUMP_CTRL_DECL("ADMA3 regdump", 3),
ADMA_REGDUMP_CTRL_DECL("ADMA4 regdump", 4),
ADMA_REGDUMP_CTRL_DECL("ADMA5 regdump", 5),
ADMA_REGDUMP_CTRL_DECL("ADMA6 regdump", 6),
ADMA_REGDUMP_CTRL_DECL("ADMA7 regdump", 7),
ADMA_REGDUMP_CTRL_DECL("ADMA8 regdump", 8),
ADMA_REGDUMP_CTRL_DECL("ADMA9 regdump", 9),
ADMA_REGDUMP_CTRL_DECL("ADMA10 regdump", 10),
ADMA_REGDUMP_CTRL_DECL("ADMA11 regdump", 11),
ADMA_REGDUMP_CTRL_DECL("ADMA12 regdump", 12),
ADMA_REGDUMP_CTRL_DECL("ADMA13 regdump", 13),
ADMA_REGDUMP_CTRL_DECL("ADMA14 regdump", 14),
ADMA_REGDUMP_CTRL_DECL("ADMA15 regdump", 15),
ADMA_REGDUMP_CTRL_DECL("ADMA16 regdump", 16),
ADMA_REGDUMP_CTRL_DECL("ADMA17 regdump", 17),
ADMA_REGDUMP_CTRL_DECL("ADMA18 regdump", 18),
ADMA_REGDUMP_CTRL_DECL("ADMA19 regdump", 19),
ADMA_REGDUMP_CTRL_DECL("ADMA20 regdump", 20),
ADMA_REGDUMP_CTRL_DECL("ADMA21 regdump", 21),
ADMA_REGDUMP_CTRL_DECL("ADMA22 regdump", 22),
ADMA_REGDUMP_CTRL_DECL("ADMA23 regdump", 23),
ADMA_REGDUMP_CTRL_DECL("ADMA24 regdump", 24),
ADMA_REGDUMP_CTRL_DECL("ADMA25 regdump", 25),
ADMA_REGDUMP_CTRL_DECL("ADMA26 regdump", 26),
ADMA_REGDUMP_CTRL_DECL("ADMA27 regdump", 27),
ADMA_REGDUMP_CTRL_DECL("ADMA28 regdump", 28),
ADMA_REGDUMP_CTRL_DECL("ADMA29 regdump", 29),
ADMA_REGDUMP_CTRL_DECL("ADMA30 regdump", 30),
ADMA_REGDUMP_CTRL_DECL("ADMA31 regdump", 31),
ADMA_REGDUMP_CTRL_DECL("ADMA32 regdump", 32),
#endif
};

static const struct snd_kcontrol_new tegra_virt_t264ref_controls[] = {
I2S_LOOPBACK_ENABLE_CTRL_DECL("I2S7 Loopback", 0x07),
I2S_LOOPBACK_ENABLE_CTRL_DECL("I2S8 Loopback", 0x08),

#if TEGRA_REGDUMP
REGDUMP_CTRL_DECL("ADMAIF21 regdump", ADMAIF21, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF22 regdump", ADMAIF22, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF23 regdump", ADMAIF23, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF24 regdump", ADMAIF24, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF25 regdump", ADMAIF25, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF26 regdump", ADMAIF26, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF27 regdump", ADMAIF27, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF28 regdump", ADMAIF28, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF29 regdump", ADMAIF29, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF30 regdump", ADMAIF30, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF31 regdump", ADMAIF31, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADMAIF32 regdump", ADMAIF32, 0, NVAUDIO_REGDUMP_RX_TX),

REGDUMP_CTRL_DECL("AMX5 regdump", AMX5, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("AMX6 regdump", AMX6, 0, NVAUDIO_REGDUMP_RX_TX),

REGDUMP_CTRL_DECL("ADX5 regdump", ADX5, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("ADX6 regdump", ADX6, 0, NVAUDIO_REGDUMP_RX_TX),

REGDUMP_CTRL_DECL("I2S7 regdump", I2S7, 0, NVAUDIO_REGDUMP_RX_TX),
REGDUMP_CTRL_DECL("I2S8 regdump", I2S8, 0, NVAUDIO_REGDUMP_RX_TX),

ADMA_REGDUMP_CTRL_DECL("ADMA33 regdump", 33),
ADMA_REGDUMP_CTRL_DECL("ADMA34 regdump", 34),
ADMA_REGDUMP_CTRL_DECL("ADMA35 regdump", 35),
ADMA_REGDUMP_CTRL_DECL("ADMA36 regdump", 36),
ADMA_REGDUMP_CTRL_DECL("ADMA37 regdump", 37),
ADMA_REGDUMP_CTRL_DECL("ADMA38 regdump", 38),
ADMA_REGDUMP_CTRL_DECL("ADMA39 regdump", 39),
ADMA_REGDUMP_CTRL_DECL("ADMA40 regdump", 40),
ADMA_REGDUMP_CTRL_DECL("ADMA41 regdump", 41),
ADMA_REGDUMP_CTRL_DECL("ADMA42 regdump", 42),
ADMA_REGDUMP_CTRL_DECL("ADMA43 regdump", 43),
ADMA_REGDUMP_CTRL_DECL("ADMA44 regdump", 44),
ADMA_REGDUMP_CTRL_DECL("ADMA45 regdump", 45),
ADMA_REGDUMP_CTRL_DECL("ADMA46 regdump", 46),
ADMA_REGDUMP_CTRL_DECL("ADMA47 regdump", 47),
ADMA_REGDUMP_CTRL_DECL("ADMA48 regdump", 48),
ADMA_REGDUMP_CTRL_DECL("ADMA49 regdump", 49),
ADMA_REGDUMP_CTRL_DECL("ADMA50 regdump", 50),
ADMA_REGDUMP_CTRL_DECL("ADMA51 regdump", 51),
ADMA_REGDUMP_CTRL_DECL("ADMA52 regdump", 52),
ADMA_REGDUMP_CTRL_DECL("ADMA53 regdump", 53),
ADMA_REGDUMP_CTRL_DECL("ADMA54 regdump", 54),
ADMA_REGDUMP_CTRL_DECL("ADMA55 regdump", 55),
ADMA_REGDUMP_CTRL_DECL("ADMA56 regdump", 56),
ADMA_REGDUMP_CTRL_DECL("ADMA57 regdump", 57),
ADMA_REGDUMP_CTRL_DECL("ADMA58 regdump", 58),
ADMA_REGDUMP_CTRL_DECL("ADMA59 regdump", 59),
ADMA_REGDUMP_CTRL_DECL("ADMA60 regdump", 60),
ADMA_REGDUMP_CTRL_DECL("ADMA61 regdump", 61),
ADMA_REGDUMP_CTRL_DECL("ADMA62 regdump", 62),
ADMA_REGDUMP_CTRL_DECL("ADMA63 regdump", 63),
ADMA_REGDUMP_CTRL_DECL("ADMA64 regdump", 64),
#endif
};

static int tegra210_virt_admaif_component_probe(struct snd_soc_component *component)
{
	int err;

	err = snd_soc_add_component_controls(component, tegra_virt_t264ref_controls,
			ARRAY_SIZE(tegra_virt_t264ref_controls));
	if (err)
		dev_err(component->dev, "can't add T264 specific controls, err: %d\n", err);

	return err;
}

static struct snd_soc_component_driver tegra210_admaif_dai_driver = {
	.name		= "tegra210-virt-pcm",
	.controls	= tegra_virt_t186ref_controls,
	.num_controls	= ARRAY_SIZE(tegra_virt_t186ref_controls),
};

static struct snd_soc_component_driver tegra264_admaif_dai_driver = {
	.probe		= tegra210_virt_admaif_component_probe,
	.name		= "tegra264-virt-pcm",
	.controls	= tegra_virt_t186ref_controls,
	.num_controls	= ARRAY_SIZE(tegra_virt_t186ref_controls),
};

int tegra210_virt_admaif_register_component(struct platform_device *pdev,
				struct tegra_virt_admaif_soc_data *data)
{
	int i = 0;
	int ret;
	int admaif_ch_num = 0;
	unsigned int admaif_ch_list[MAX_ADMAIF_IDS] = {0};
	struct tegra_virt_admaif_soc_data *soc_data = data;
	int adma_count = 0;
	unsigned int buffer_size;

	admaif = devm_kzalloc(&pdev->dev, sizeof(*admaif), GFP_KERNEL);
	if (admaif == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	admaif->num_ch = soc_data->num_ch;
	admaif->client_data.hivc_client =
			nvaudio_ivc_alloc_ctxt(&pdev->dev);
	if (!admaif->client_data.hivc_client) {
		dev_err(&pdev->dev, "Failed to allocate IVC context\n");
		ret = -ENODEV;
		goto err;
	}
	svd_cntxt = admaif->client_data.hivc_client;

	admaif->capture_dma_data = devm_kzalloc(&pdev->dev,
			sizeof(struct tegra_alt_pcm_dma_params) *
				soc_data->num_ch,
			GFP_KERNEL);
	if (admaif->capture_dma_data == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	admaif->playback_dma_data = devm_kzalloc(&pdev->dev,
			sizeof(struct tegra_alt_pcm_dma_params) *
				soc_data->num_ch,
			GFP_KERNEL);
	if (admaif->playback_dma_data == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	if (of_property_read_u32(pdev->dev.of_node,
				"admaif_ch_num", &admaif_ch_num)) {
		dev_err(&pdev->dev, "number of admaif channels is not set\n");
		return -EINVAL;
	}

	if (of_property_read_u32_array(pdev->dev.of_node,
						"admaif_ch_list",
						admaif_ch_list,
						admaif_ch_num)) {
		dev_err(&pdev->dev, "admaif_ch_list is not populated\n");
		return -EINVAL;
	}


	for (i = 0; i < soc_data->num_ch; i++) {
		if ((i + 1) != admaif_ch_list[adma_count])
			continue;
		if (of_device_is_compatible(pdev->dev.of_node,
			"nvidia,tegra186-virt-pcm")) {
			admaif->playback_dma_data[i].addr = TEGRA186_ADMAIF_BASE +
					TEGRA186_ADMAIF_XBAR_TX_FIFO_WRITE +
					(i * TEGRA186_ADMAIF_CHANNEL_REG_STRIDE);
			admaif->capture_dma_data[i].addr = TEGRA186_ADMAIF_BASE +
					TEGRA186_ADMAIF_XBAR_RX_FIFO_READ +
					(i * TEGRA186_ADMAIF_CHANNEL_REG_STRIDE);
		} else if (of_device_is_compatible(pdev->dev.of_node,
			"nvidia,tegra234-virt-pcm-oot")) {
			admaif->playback_dma_data[i].addr = TEGRA186_ADMAIF_BASE +
					TEGRA186_ADMAIF_XBAR_TX_FIFO_WRITE +
					(i * TEGRA186_ADMAIF_CHANNEL_REG_STRIDE);
			admaif->capture_dma_data[i].addr = TEGRA186_ADMAIF_BASE +
					TEGRA186_ADMAIF_XBAR_RX_FIFO_READ +
					(i * TEGRA186_ADMAIF_CHANNEL_REG_STRIDE);
		} else if (of_device_is_compatible(pdev->dev.of_node,
			"nvidia,tegra264-virt-pcm-oot")) {
			admaif->playback_dma_data[i].addr = TEGRA264_ADMAIF_BASE +
					TEGRA264_ADMAIF_XBAR_TX_FIFO_WRITE +
					(i * TEGRA264_ADMAIF_CHANNEL_REG_STRIDE);
			admaif->capture_dma_data[i].addr = TEGRA264_ADMAIF_BASE +
					TEGRA264_ADMAIF_XBAR_RX_FIFO_READ +
					(i * TEGRA264_ADMAIF_CHANNEL_REG_STRIDE);
		} else if (of_device_is_compatible(pdev->dev.of_node,
			"nvidia,tegra210-virt-pcm")) {
			admaif->playback_dma_data[i].addr = TEGRA210_ADMAIF_BASE +
					TEGRA210_ADMAIF_XBAR_TX_FIFO_WRITE +
					(i * TEGRA210_ADMAIF_CHANNEL_REG_STRIDE);
			admaif->capture_dma_data[i].addr = TEGRA210_ADMAIF_BASE +
					TEGRA210_ADMAIF_XBAR_RX_FIFO_READ +
					(i * TEGRA210_ADMAIF_CHANNEL_REG_STRIDE);
		} else if (of_device_is_compatible(pdev->dev.of_node,
			"nvidia,tegra264-virt-pcm-oot")) {
			admaif->playback_dma_data[i].addr = TEGRA264_ADMAIF_BASE +
					TEGRA264_ADMAIF_XBAR_TX_FIFO_WRITE +
					(i * TEGRA264_ADMAIF_CHANNEL_REG_STRIDE);
			admaif->capture_dma_data[i].addr = TEGRA264_ADMAIF_BASE +
					TEGRA264_ADMAIF_XBAR_RX_FIFO_READ +
					(i * TEGRA264_ADMAIF_CHANNEL_REG_STRIDE);
			/* TODO: Should get from soc_data during full Thor changes */
			admaif->num_ch = TEGRA264_MAX_CHANNELS;
		} else {
			dev_err(&pdev->dev,
				"Uncompatible device driver\n");
			ret = -ENODEV;
			goto err;
		}

		buffer_size = 0;
		if (of_property_read_u32_index(pdev->dev.of_node,
				"dma-buffer-size",
				(i * 2) + 1,
				&buffer_size) < 0)
			dev_dbg(&pdev->dev,
				"Missing property nvidia,dma-buffer-size\n");
		admaif->playback_dma_data[i].buffer_size = buffer_size;
		admaif->playback_dma_data[i].width = 32;
		admaif->playback_dma_data[i].req_sel = i + 1;

		if (of_property_read_string_index(pdev->dev.of_node,
				"dma-names",
				(adma_count * 2) + 1,
				&admaif->playback_dma_data[i].chan_name) < 0) {
			dev_err(&pdev->dev,
				"Missing property nvidia,dma-names\n");
			ret = -ENODEV;
			goto err;
		}

		buffer_size = 0;
		if (of_property_read_u32_index(pdev->dev.of_node,
				"dma-buffer-size",
				(i * 2),
				&buffer_size) < 0)
			dev_dbg(&pdev->dev,
				"Missing property nvidia,dma-buffer-size\n");
		admaif->capture_dma_data[i].buffer_size = buffer_size;
		admaif->capture_dma_data[i].width = 32;
		admaif->capture_dma_data[i].req_sel = i + 1;
		if (of_property_read_string_index(pdev->dev.of_node,
				"dma-names",
				(adma_count * 2),
				&admaif->capture_dma_data[i].chan_name) < 0) {
			dev_err(&pdev->dev,
				"Missing property nvidia,dma-names\n");
			ret = -ENODEV;
			goto err;
		}
		adma_count++;
	}

	if (of_device_is_compatible(pdev->dev.of_node,
				"nvidia,tegra264-virt-pcm-oot")) {
		ret = tegra_register_component(&pdev->dev,
					&tegra264_admaif_dai_driver,
					tegra264_admaif_dais,
					soc_data->num_ch, "admaif");
	} else {
		ret = tegra_register_component(&pdev->dev,
					&tegra210_admaif_dai_driver,
					tegra210_admaif_dais,
					soc_data->num_ch, "admaif");
	}
	if (ret) {
		dev_err(&pdev->dev, "Could not register DAIs %d: %d\n",
			i, ret);
		goto err;
	}

	ret = tegra_alt_pcm_platform_register(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Could not register PCM: %d\n", ret);
		goto err_unregister_dais;
	}

	return 0;
err_unregister_dais:
	snd_soc_unregister_component(&pdev->dev);
err:
	return ret;
}
EXPORT_SYMBOL_GPL(tegra210_virt_admaif_register_component);

void tegra210_virt_admaif_unregister_component(struct platform_device *pdev)
{
	tegra_alt_pcm_platform_unregister(&pdev->dev);
	snd_soc_unregister_component(&pdev->dev);
}
EXPORT_SYMBOL_GPL(tegra210_virt_admaif_unregister_component);

struct nvaudio_ivc_ctxt *nvaudio_get_saved_ivc_ctxt(void)
{
	if (svd_cntxt)
		return svd_cntxt;

	pr_err("%s: ivc ctxt not allocated\n", __func__);
	return NULL;
}
EXPORT_SYMBOL_GPL(nvaudio_get_saved_ivc_ctxt);

MODULE_LICENSE("GPL");
