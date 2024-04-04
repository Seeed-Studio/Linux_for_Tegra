/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2014-2024 NVIDIA CORPORATION. All rights reserved. */
/*
 * tegra_asoc_machine.h
 *
 */

#ifndef __TEGRA_ASOC_MACHINE_H__
#define __TEGRA_ASOC_MACHINE_H__

int tegra_machine_add_i2s_codec_controls(struct snd_soc_card *card);
int tegra_machine_add_codec_jack_control(struct snd_soc_card *card,
					 struct snd_soc_pcm_runtime *rtd,
					 struct snd_soc_jack *jack);

#endif
