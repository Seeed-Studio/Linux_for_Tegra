/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef __TEGRA_VIRT_ALT_XBAR_H__
#define __TEGRA_VIRT_ALT_XBAR_H__

#include <sound/soc.h>

#define TEGRA_XBAR_RX_STRIDE	0x4
#define TEGRA_T186_SRC_NUM_MUX	83
#define TEGRA_T210_SRC_NUM_MUX	55

#define TEGRA186_MAX_CHANNELS	16
#define TEGRA264_MAX_CHANNELS	32

#define MUX_REG(id) (TEGRA_XBAR_RX_STRIDE * (id))
#define SOC_ENUM_EXT_REG(xname, xcount, xenum, xhandler_get, xhandler_put) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_enum_double, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = (unsigned long)xenum,	\
	.tlv.p = (unsigned int *) xcount,	\
}

#define SOC_VALUE_ENUM_WIDE(xreg, shift, xmax, xtexts, xvalues) \
{	.reg = xreg, .shift_l = shift, .shift_r = shift, \
	.items = xmax, .texts = xtexts, .values = xvalues, \
	.mask = xmax ? roundup_pow_of_two(xmax) - 1 : 0}

#define SOC_VALUE_ENUM_WIDE_DECL(name, xreg, shift, \
		xtexts, xvalues) \
	static struct soc_enum name = SOC_VALUE_ENUM_WIDE(xreg, shift, \
					ARRAY_SIZE(xtexts), xtexts, xvalues)

#define MUX_ENUM_CTRL_DECL_234(ename, id) \
	SOC_VALUE_ENUM_WIDE_DECL(ename##_enum, MUX_REG(id), 0,	\
			tegra_virt_t234ref_source_text, \
			tegra_virt_t234ref_source_value); \
	static const struct snd_kcontrol_new ename##_control = \
		SOC_DAPM_ENUM_EXT("Route", ename##_enum,\
				tegra_virt_get_route,\
				tegra_virt_put_route)

#define MUX_ENUM_CTRL_DECL_264(ename, id) \
	SOC_VALUE_ENUM_WIDE_DECL(ename##_enum, MUX_REG(id), 0,	\
			tegra_virt_t264ref_source_text, \
			tegra_virt_t264ref_source_value); \
	static const struct snd_kcontrol_new ename##_control = \
		SOC_DAPM_ENUM_EXT("Route", ename##_enum,\
				tegra_virt_get_route,\
				tegra_virt_put_route)

#define MUX_VALUE(npart, nbit) (1 + nbit + npart * 32)

#define WIDGETS(sname, ename) \
	SND_SOC_DAPM_AIF_IN(sname " RX", NULL, 0, SND_SOC_NOPM, 0, 0), \
	SND_SOC_DAPM_MUX(sname " Mux", SND_SOC_NOPM, 0, 0, &ename##_control)

#define TX_WIDGETS(sname) \
	SND_SOC_DAPM_AIF_IN(sname " RX", NULL, 0, SND_SOC_NOPM, 0, 0)

#define MIXER_IN_WIDGETS(sname, ename) \
	SND_SOC_DAPM_MUX(sname " Mux", SND_SOC_NOPM, 0, 0, &ename##_control)

#define MIXER_OUT_WIDGETS(sname) \
	SND_SOC_DAPM_AIF_IN(sname " RX", NULL, 0, SND_SOC_NOPM, 0, 0)

#define SND_SOC_DAPM_OUT(wname) \
	{.id = snd_soc_dapm_spk, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0, .reg = SND_SOC_NOPM, .event = NULL,}

#define SND_SOC_DAPM_IN(wname) \
	{.id = snd_soc_dapm_mic, .name = wname, .kcontrol_news = NULL, \
	.num_kcontrols = 0, .reg = SND_SOC_NOPM, .event = NULL,}

#define CODEC_WIDGET(sname) \
	SND_SOC_DAPM_IN(sname " MIC"), \
	SND_SOC_DAPM_OUT(sname " HEADPHONE")


extern const int tegra_virt_t210ref_source_value[];
extern const char * const tegra_virt_t210ref_source_text[];
extern const int tegra_virt_t186ref_source_value[];
extern const char * const tegra_virt_t186ref_source_text[];

int tegra_virt_get_route(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
int tegra_virt_put_route(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol);
void tegra_virt_set_enum_source(const struct soc_enum *enum_virt);
int tegra_virt_xbar_register_codec(struct platform_device *pdev);
#endif
