/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef __TEGRA210_VIRT_ALT_ADMAIF_H__
#define __TEGRA210_VIRT_ALT_ADMAIF_H__

#define DRV_NAME					"virt-alt-pcm-oot"

#define TEGRA210_ADMAIF_BASE				0x702d0000
#define TEGRA210_ADMAIF_XBAR_RX_FIFO_READ		0x2c
#define TEGRA210_ADMAIF_XBAR_TX_FIFO_WRITE		0x32c
#define TEGRA210_ADMAIF_CHANNEL_REG_STRIDE		0x40

#define TEGRA186_ADMAIF_BASE				0x0290f000
#define TEGRA186_ADMAIF_XBAR_RX_FIFO_READ		0x2c
#define TEGRA186_ADMAIF_XBAR_TX_FIFO_WRITE		0x52c
#define TEGRA186_ADMAIF_CHANNEL_REG_STRIDE		0x40

#define TEGRA210_AUDIOCIF_BITS_8			1
#define TEGRA210_AUDIOCIF_BITS_12			2
#define TEGRA210_AUDIOCIF_BITS_16			3
#define TEGRA210_AUDIOCIF_BITS_20			4
#define TEGRA210_AUDIOCIF_BITS_24			5
#define TEGRA210_AUDIOCIF_BITS_28			6
#define TEGRA210_AUDIOCIF_BITS_32			7

#define TEGRA210_AUDIOCIF_CTRL_FIFO_THRESHOLD_SHIFT	24
#define TEGRA210_AUDIOCIF_CTRL_AUDIO_CHANNELS_SHIFT	20
#define TEGRA210_AUDIOCIF_CTRL_CLIENT_CHANNELS_SHIFT	16
#define TEGRA210_AUDIOCIF_CTRL_AUDIO_BITS_SHIFT		12
#define TEGRA210_AUDIOCIF_CTRL_CLIENT_BITS_SHIFT	8
#define TEGRA210_AUDIOCIF_CTRL_EXPAND_SHIFT		6
#define TEGRA210_AUDIOCIF_CTRL_STEREO_CONV_SHIFT	4
#define TEGRA210_AUDIOCIF_CTRL_REPLICATE_SHIFT		3
#define TEGRA210_AUDIOCIF_CTRL_TRUNCATE_SHIFT		1
#define TEGRA210_AUDIOCIF_CTRL_MONO_CONV_SHIFT		0

#define TEGRA264_ADMAIF_BASE				0x09610000
#define TEGRA264_ADMAIF_XBAR_RX_FIFO_READ		0x2c
#define TEGRA264_ADMAIF_XBAR_TX_FIFO_WRITE		0x102c
#define TEGRA264_ADMAIF_CHANNEL_REG_STRIDE		0x40
#define TEGRA_32CH_ACIF_CTRL_AUDIO_BITS_SHIFT		11
#define TEGRA_32CH_ACIF_CTRL_CLIENT_CH_SHIFT		14
#define TEGRA_32CH_ACIF_CTRL_AUDIO_CH_SHIFT		19

#define TEGRA264_ADMAIF_BASE				0x09610000
#define TEGRA264_ADMAIF_XBAR_RX_FIFO_READ		0x2c
#define TEGRA264_ADMAIF_XBAR_TX_FIFO_WRITE		0x102c
#define TEGRA264_ADMAIF_CHANNEL_REG_STRIDE		0x40
#define TEGRA264_MAX_CHANNELS				32
#define TEGRA_32CH_ACIF_CTRL_AUDIO_BITS_SHIFT		11
#define TEGRA_32CH_ACIF_CTRL_CLIENT_CH_SHIFT		14
#define TEGRA_32CH_ACIF_CTRL_AUDIO_CH_SHIFT		19

/* ADMAIF ids */
enum {
	ADMAIF_ID_0 = 0,
	ADMAIF_ID_1,
	ADMAIF_ID_2,
	ADMAIF_ID_3,
	ADMAIF_ID_4,
	ADMAIF_ID_5,
	ADMAIF_ID_6,
	ADMAIF_ID_7,
	ADMAIF_ID_8,
	ADMAIF_ID_9,
	ADMAIF_ID_10,
	TEGRA210_ADMAIF_CHANNEL_COUNT = ADMAIF_ID_10,
	ADMAIF_ID_11,
	ADMAIF_ID_12,
	ADMAIF_ID_13,
	ADMAIF_ID_14,
	ADMAIF_ID_15,
	ADMAIF_ID_16,
	ADMAIF_ID_17,
	ADMAIF_ID_18,
	ADMAIF_ID_19,
	MAX_ADMAIF_T186_IDS,
	TEGRA186_ADMAIF_CHANNEL_COUNT = MAX_ADMAIF_T186_IDS,
	ADMAIF_ID_20 = MAX_ADMAIF_T186_IDS,
	ADMAIF_ID_21,
	ADMAIF_ID_22,
	ADMAIF_ID_23,
	ADMAIF_ID_24,
	ADMAIF_ID_25,
	ADMAIF_ID_26,
	ADMAIF_ID_27,
	ADMAIF_ID_28,
	ADMAIF_ID_29,
	ADMAIF_ID_30,
	ADMAIF_ID_31,
	MAX_ADMAIF_T264_IDS,
	TEGRA264_ADMAIF_CHANNEL_COUNT = MAX_ADMAIF_T264_IDS,
	MAX_ADMAIF_IDS = MAX_ADMAIF_T264_IDS,
};

/* Audio cif definition */
struct tegra210_virt_audio_cif {
	unsigned int threshold;
	unsigned int audio_channels;
	unsigned int client_channels;
	unsigned int audio_bits;
	unsigned int client_bits;
	unsigned int expand;
	unsigned int stereo_conv;
	unsigned int replicate;
	int32_t direction;
	unsigned int truncate;
	unsigned int mono_conv;
};

/*  apbif data */
struct tegra210_virt_admaif_client_data {
	struct nvaudio_ivc_ctxt *hivc_client;
};

struct tegra210_admaif {
	struct tegra_alt_pcm_dma_params *capture_dma_data;
	struct tegra_alt_pcm_dma_params *playback_dma_data;
	struct tegra210_virt_admaif_client_data client_data;
	unsigned int num_ch;
};

struct tegra_virt_admaif_soc_data {
	unsigned int num_ch;
};

int tegra210_virt_admaif_register_component(struct platform_device *pdev,
				struct tegra_virt_admaif_soc_data *soc_data);
void tegra210_virt_admaif_unregister_component(struct platform_device *pdev);

struct nvaudio_ivc_ctxt *nvaudio_get_saved_ivc_ctxt(void);

#endif
