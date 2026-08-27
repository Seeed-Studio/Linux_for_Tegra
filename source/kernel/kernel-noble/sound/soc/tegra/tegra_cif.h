/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tegra_cif.h - TEGRA Audio CIF Programming
 *
 * Copyright (c) 2020-2023 NVIDIA CORPORATION.  All rights reserved.
 *
 */

#ifndef __TEGRA_CIF_H__
#define __TEGRA_CIF_H__

#include <linux/regmap.h>

#define TEGRA_ACIF_CTRL_FIFO_TH_SHIFT		24
#define TEGRA_ACIF_CTRL_AUDIO_CH_SHIFT		20
#define TEGRA_ACIF_CTRL_CLIENT_CH_SHIFT		16
#define TEGRA_ACIF_CTRL_AUDIO_BITS_SHIFT	12
#define TEGRA_ACIF_CTRL_CLIENT_BITS_SHIFT	8
#define TEGRA_ACIF_CTRL_EXPAND_SHIFT		6
#define TEGRA_ACIF_CTRL_STEREO_CONV_SHIFT	4
#define TEGRA_ACIF_CTRL_REPLICATE_SHIFT		3
#define TEGRA_ACIF_CTRL_TRUNCATE_SHIFT		1
#define TEGRA_ACIF_CTRL_MONO_CONV_SHIFT		0

#define TEGRA_32CH_ACIF_CTRL_AUDIO_BITS_SHIFT	11
#define TEGRA_32CH_ACIF_CTRL_CLIENT_CH_SHIFT	14
#define TEGRA_32CH_ACIF_CTRL_AUDIO_CH_SHIFT	19

/* AUDIO/CLIENT_BITS values */
#define TEGRA_ACIF_BITS_8			1
#define TEGRA_ACIF_BITS_16			3
#define TEGRA_ACIF_BITS_24			5
#define TEGRA_ACIF_BITS_32			7

#define TEGRA_ACIF_UPDATE_MASK			0x3ffffffb

struct tegra_cif_conf {
	unsigned int threshold;
	unsigned int audio_ch;
	unsigned int client_ch;
	unsigned int audio_bits;
	unsigned int client_bits;
	unsigned int expand;
	unsigned int stereo_conv;
	unsigned int replicate;
	unsigned int truncate;
	unsigned int mono_conv;
	unsigned int has_max_32ch;
};

static inline void tegra_set_cif(struct regmap *regmap, unsigned int reg,
				 struct tegra_cif_conf *conf)
{
	unsigned int value;
	unsigned int audio_bits_shift;
	unsigned int audio_ch_shift;
	unsigned int client_ch_shift;

	audio_bits_shift = conf->has_max_32ch ? TEGRA_32CH_ACIF_CTRL_AUDIO_BITS_SHIFT : \
			   TEGRA_ACIF_CTRL_AUDIO_BITS_SHIFT;

	audio_ch_shift = conf->has_max_32ch ? TEGRA_32CH_ACIF_CTRL_AUDIO_CH_SHIFT : \
			   TEGRA_ACIF_CTRL_AUDIO_CH_SHIFT;

	client_ch_shift = conf->has_max_32ch ? TEGRA_32CH_ACIF_CTRL_CLIENT_CH_SHIFT : \
			   TEGRA_ACIF_CTRL_CLIENT_CH_SHIFT;

	value = (conf->threshold << TEGRA_ACIF_CTRL_FIFO_TH_SHIFT) |
		((conf->audio_ch - 1) << audio_ch_shift) |
		((conf->client_ch - 1) << client_ch_shift) |
		(conf->audio_bits << audio_bits_shift) |
		(conf->client_bits << TEGRA_ACIF_CTRL_CLIENT_BITS_SHIFT) |
		(conf->expand << TEGRA_ACIF_CTRL_EXPAND_SHIFT) |
		(conf->stereo_conv << TEGRA_ACIF_CTRL_STEREO_CONV_SHIFT) |
		(conf->replicate << TEGRA_ACIF_CTRL_REPLICATE_SHIFT) |
		(conf->truncate << TEGRA_ACIF_CTRL_TRUNCATE_SHIFT) |
		(conf->mono_conv << TEGRA_ACIF_CTRL_MONO_CONV_SHIFT);

	regmap_update_bits(regmap, reg, TEGRA_ACIF_UPDATE_MASK, value);
}

#endif
