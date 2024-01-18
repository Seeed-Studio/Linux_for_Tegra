/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
/*
 * tegra_codec.h
 *
 */

#ifndef __TEGRA_CODECS_H__
#define __TEGRA_CODECS_H__

#include <sound/soc.h>

int tegra_codecs_init(struct snd_soc_card *card);

int tegra_codecs_runtime_setup(struct snd_soc_pcm_runtime *rtd,
			       struct snd_pcm_hw_params *params);

#endif
