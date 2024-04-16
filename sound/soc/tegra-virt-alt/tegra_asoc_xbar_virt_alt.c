// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <linux/module.h>

#include "tegra_virt_alt_ivc.h"
#include "tegra_asoc_util_virt_alt.h"
#include "tegra_asoc_xbar_virt_alt.h"

static const struct soc_enum *tegra_virt_enum_source;

#define DAI(sname)						\
	{							\
		.name = #sname " CIF",				\
		.playback = {					\
			.stream_name = #sname " CIF Receive",	\
			.channels_min = 1,			\
			.channels_max = 16,			\
			.rates = SNDRV_PCM_RATE_8000_192000,	\
			.formats = SNDRV_PCM_FMTBIT_S8 |	\
				SNDRV_PCM_FMTBIT_S16_LE |	\
				SNDRV_PCM_FMTBIT_S24_LE |	\
				SNDRV_PCM_FMTBIT_S32_LE,	\
		},						\
		.capture = {					\
			.stream_name = #sname " CIF Transmit",	\
			.channels_min = 1,			\
			.channels_max = 16,			\
			.rates = SNDRV_PCM_RATE_8000_192000,	\
			.formats = SNDRV_PCM_FMTBIT_S8 |	\
				SNDRV_PCM_FMTBIT_S16_LE |	\
				SNDRV_PCM_FMTBIT_S24_LE |	\
				SNDRV_PCM_FMTBIT_S32_LE,	\
		},						\
	}

static struct snd_soc_dai_driver tegra264_virt_xbar_dais[] = {
	DAI(ADMAIF1),
	DAI(ADMAIF2),
	DAI(ADMAIF3),
	DAI(ADMAIF4),
	DAI(ADMAIF5),
	DAI(ADMAIF6),
	DAI(ADMAIF7),
	DAI(ADMAIF8),
	DAI(ADMAIF9),
	DAI(ADMAIF10),
	DAI(ADMAIF11),
	DAI(ADMAIF12),
	DAI(ADMAIF13),
	DAI(ADMAIF14),
	DAI(ADMAIF15),
	DAI(ADMAIF16),
	DAI(ADMAIF17),
	DAI(ADMAIF18),
	DAI(ADMAIF19),
	DAI(ADMAIF20),
	DAI(ADMAIF21),
	DAI(ADMAIF22),
	DAI(ADMAIF23),
	DAI(ADMAIF24),
	DAI(ADMAIF25),
	DAI(ADMAIF26),
	DAI(ADMAIF27),
	DAI(ADMAIF28),
	DAI(ADMAIF29),
	DAI(ADMAIF30),
	DAI(ADMAIF31),
	DAI(ADMAIF32),
};

static struct snd_soc_dai_driver tegra234_virt_xbar_dais[] = {
	DAI(ADMAIF1),
	DAI(ADMAIF2),
	DAI(ADMAIF3),
	DAI(ADMAIF4),
	DAI(ADMAIF5),
	DAI(ADMAIF6),
	DAI(ADMAIF7),
	DAI(ADMAIF8),
	DAI(ADMAIF9),
	DAI(ADMAIF10),
	DAI(ADMAIF11),
	DAI(ADMAIF12),
	DAI(ADMAIF13),
	DAI(ADMAIF14),
	DAI(ADMAIF15),
	DAI(ADMAIF16),
	DAI(ADMAIF17),
	DAI(ADMAIF18),
	DAI(ADMAIF19),
	DAI(ADMAIF20),
};

static const int tegra_virt_t234ref_source_value[] = {
	0,
	MUX_VALUE(0, 0),
	MUX_VALUE(0, 1),
	MUX_VALUE(0, 2),
	MUX_VALUE(0, 3),
	MUX_VALUE(0, 4),
	MUX_VALUE(0, 5),
	MUX_VALUE(0, 6),
	MUX_VALUE(0, 7),
	MUX_VALUE(0, 8),
	MUX_VALUE(0, 9),
	MUX_VALUE(0, 10),
	MUX_VALUE(0, 11),
	MUX_VALUE(0, 12),
	MUX_VALUE(0, 13),
	MUX_VALUE(0, 14),
	MUX_VALUE(0, 15),
	MUX_VALUE(0, 16),
	MUX_VALUE(0, 17),
	MUX_VALUE(0, 18),
	MUX_VALUE(0, 19),
	MUX_VALUE(0, 20),
	MUX_VALUE(0, 21),
	MUX_VALUE(0, 24),
	MUX_VALUE(0, 25),
	MUX_VALUE(0, 26),
	MUX_VALUE(0, 27),
	MUX_VALUE(1, 0),
	MUX_VALUE(1, 1),
	MUX_VALUE(1, 2),
	MUX_VALUE(1, 3),
	MUX_VALUE(1, 4),
	MUX_VALUE(1, 8),
	MUX_VALUE(1, 9),
	MUX_VALUE(1, 10),
	MUX_VALUE(1, 11),
	MUX_VALUE(1, 16),
	MUX_VALUE(1, 20),
	MUX_VALUE(1, 21),
	MUX_VALUE(1, 24),
	MUX_VALUE(1, 25),
	MUX_VALUE(1, 26),
	MUX_VALUE(1, 27),
	MUX_VALUE(1, 28),
	MUX_VALUE(1, 29),
	MUX_VALUE(2, 0),
	MUX_VALUE(2, 4),
	MUX_VALUE(2, 8),
	MUX_VALUE(2, 9),
	MUX_VALUE(2, 12),
	MUX_VALUE(2, 13),
	MUX_VALUE(2, 14),
	MUX_VALUE(2, 15),
	MUX_VALUE(2, 18),
	MUX_VALUE(2, 19),
	MUX_VALUE(2, 20),
	MUX_VALUE(2, 21),
	MUX_VALUE(2, 24),
	MUX_VALUE(2, 25),
	MUX_VALUE(2, 26),
	MUX_VALUE(2, 27),
	MUX_VALUE(2, 28),
	MUX_VALUE(2, 29),
	MUX_VALUE(2, 30),
	MUX_VALUE(2, 31),
	MUX_VALUE(3, 0),
	MUX_VALUE(3, 1),
	MUX_VALUE(3, 2),
	MUX_VALUE(3, 3),
	MUX_VALUE(3, 4),
	MUX_VALUE(3, 5),
	MUX_VALUE(3, 6),
	MUX_VALUE(3, 7),
	MUX_VALUE(3, 16),
	MUX_VALUE(3, 17),
	MUX_VALUE(3, 18),
	MUX_VALUE(3, 19),
	MUX_VALUE(3, 24),
	MUX_VALUE(3, 25),
	MUX_VALUE(3, 26),
	MUX_VALUE(3, 27),
	MUX_VALUE(3, 28),
	MUX_VALUE(3, 29),
};

const char * const tegra_virt_t234ref_source_text[] = {
	"None",
	"ADMAIF1",
	"ADMAIF2",
	"ADMAIF3",
	"ADMAIF4",
	"ADMAIF5",
	"ADMAIF6",
	"ADMAIF7",
	"ADMAIF8",
	"ADMAIF9",
	"ADMAIF10",
	"ADMAIF11",
	"ADMAIF12",
	"ADMAIF13",
	"ADMAIF14",
	"ADMAIF15",
	"ADMAIF16",
	"I2S1",
	"I2S2",
	"I2S3",
	"I2S4",
	"I2S5",
	"I2S6",
	"SFC1",
	"SFC2",
	"SFC3",
	"SFC4",
	"MIXER1-1",
	"MIXER1-2",
	"MIXER1-3",
	"MIXER1-4",
	"MIXER1-5",
	"AMX1",
	"AMX2",
	"AMX3",
	"AMX4",
	"ARAD1",
	"SPDIF1-1",
	"SPDIF1-2",
	"AFC1",
	"AFC2",
	"AFC3",
	"AFC4",
	"AFC5",
	"AFC6",
	"OPE1",
	"SPKPROT1",
	"MVC1",
	"MVC2",
	"IQC1-1",
	"IQC1-2",
	"IQC2-1",
	"IQC2-2",
	"DMIC1",
	"DMIC2",
	"DMIC3",
	"DMIC4",
	"ADX1-1",
	"ADX1-2",
	"ADX1-3",
	"ADX1-4",
	"ADX2-1",
	"ADX2-2",
	"ADX2-3",
	"ADX2-4",
	"ADX3-1",
	"ADX3-2",
	"ADX3-3",
	"ADX3-4",
	"ADX4-1",
	"ADX4-2",
	"ADX4-3",
	"ADX4-4",
	"ADMAIF17",
	"ADMAIF18",
	"ADMAIF19",
	"ADMAIF20",
	"ASRC1-1",
	"ASRC1-2",
	"ASRC1-3",
	"ASRC1-4",
	"ASRC1-5",
	"ASRC1-6",
};

const int tegra_virt_t264ref_source_value[] = {
	0,
	MUX_VALUE(0, 0),
	MUX_VALUE(0, 1),
	MUX_VALUE(0, 2),
	MUX_VALUE(0, 3),
	MUX_VALUE(0, 4),
	MUX_VALUE(0, 5),
	MUX_VALUE(0, 6),
	MUX_VALUE(0, 7),
	MUX_VALUE(0, 8),
	MUX_VALUE(0, 9),
	MUX_VALUE(0, 10),
	MUX_VALUE(0, 11),
	MUX_VALUE(0, 12),
	MUX_VALUE(0, 13),
	MUX_VALUE(0, 14),
	MUX_VALUE(0, 15),
	MUX_VALUE(0, 16),
	MUX_VALUE(0, 17),
	MUX_VALUE(0, 18),
	MUX_VALUE(0, 19),
	MUX_VALUE(0, 20),
	MUX_VALUE(0, 21),
	MUX_VALUE(0, 22),
	MUX_VALUE(0, 23),
	MUX_VALUE(0, 24),
	MUX_VALUE(0, 25),
	MUX_VALUE(0, 26),
	MUX_VALUE(0, 27),
	MUX_VALUE(1, 0),
	MUX_VALUE(1, 1),
	MUX_VALUE(1, 2),
	MUX_VALUE(1, 3),
	MUX_VALUE(1, 4),
	MUX_VALUE(1, 8),
	MUX_VALUE(1, 9),
	MUX_VALUE(1, 10),
	MUX_VALUE(1, 11),
	MUX_VALUE(1, 12),
	MUX_VALUE(1, 13),
	MUX_VALUE(1, 16),
	MUX_VALUE(1, 24),
	MUX_VALUE(1, 25),
	MUX_VALUE(1, 26),
	MUX_VALUE(1, 27),
	MUX_VALUE(1, 28),
	MUX_VALUE(1, 29),
	MUX_VALUE(2, 0),
	MUX_VALUE(2, 8),
	MUX_VALUE(2, 9),
	MUX_VALUE(2, 18),
	MUX_VALUE(2, 19),
	MUX_VALUE(2, 20),
	MUX_VALUE(2, 21),
	MUX_VALUE(2, 24),
	MUX_VALUE(2, 25),
	MUX_VALUE(2, 26),
	MUX_VALUE(2, 27),
	MUX_VALUE(2, 28),
	MUX_VALUE(2, 29),
	MUX_VALUE(2, 30),
	MUX_VALUE(2, 31),
	MUX_VALUE(3, 0),
	MUX_VALUE(3, 1),
	MUX_VALUE(3, 2),
	MUX_VALUE(3, 3),
	MUX_VALUE(3, 4),
	MUX_VALUE(3, 5),
	MUX_VALUE(3, 6),
	MUX_VALUE(3, 7),
	MUX_VALUE(3, 8),
	MUX_VALUE(3, 9),
	MUX_VALUE(3, 10),
	MUX_VALUE(3, 11),
	MUX_VALUE(3, 12),
	MUX_VALUE(3, 13),
	MUX_VALUE(3, 14),
	MUX_VALUE(3, 15),
	MUX_VALUE(3, 24),
	MUX_VALUE(3, 25),
	MUX_VALUE(3, 26),
	MUX_VALUE(3, 27),
	MUX_VALUE(3, 28),
	MUX_VALUE(3, 29),
	MUX_VALUE(4, 7),
	MUX_VALUE(4, 8),
	MUX_VALUE(4, 9),
	MUX_VALUE(4, 10),
	MUX_VALUE(4, 11),
	MUX_VALUE(4, 12),
	MUX_VALUE(4, 13),
	MUX_VALUE(4, 14),
	MUX_VALUE(4, 15),
	MUX_VALUE(4, 16),
	MUX_VALUE(4, 17),
	MUX_VALUE(4, 18),
	MUX_VALUE(4, 19),
	MUX_VALUE(4, 20),
	MUX_VALUE(4, 21),
	MUX_VALUE(4, 22),
};

const char * const tegra_virt_t264ref_source_text[] = {
	"None",
	"ADMAIF1",
	"ADMAIF2",
	"ADMAIF3",
	"ADMAIF4",
	"ADMAIF5",
	"ADMAIF6",
	"ADMAIF7",
	"ADMAIF8",
	"ADMAIF9",
	"ADMAIF10",
	"ADMAIF11",
	"ADMAIF12",
	"ADMAIF13",
	"ADMAIF14",
	"ADMAIF15",
	"ADMAIF16",
	"I2S1",
	"I2S2",
	"I2S3",
	"I2S4",
	"I2S5",
	"I2S6",
	"I2S7",
	"I2S8",
	"SFC1",
	"SFC2",
	"SFC3",
	"SFC4",
	"MIXER1-1",
	"MIXER1-2",
	"MIXER1-3",
	"MIXER1-4",
	"MIXER1-5",
	"AMX1",
	"AMX2",
	"AMX3",
	"AMX4",
	"AMX5",
	"AMX6",
	"ARAD1",
	"AFC1",
	"AFC2",
	"AFC3",
	"AFC4",
	"AFC5",
	"AFC6",
	"OPE1",
	"MVC1",
	"MVC2",
	"DMIC1",
	"DMIC2",
	"ADX1-1",
	"ADX1-2",
	"ADX1-3",
	"ADX1-4",
	"ADX2-1",
	"ADX2-2",
	"ADX2-3",
	"ADX2-4",
	"ADX3-1",
	"ADX3-2",
	"ADX3-3",
	"ADX3-4",
	"ADX4-1",
	"ADX4-2",
	"ADX4-3",
	"ADX4-4",
	"ADX5-1",
	"ADX5-2",
	"ADX5-3",
	"ADX5-4",
	"ADX6-1",
	"ADX6-2",
	"ADX6-3",
	"ADX6-4",
	"ASRC1-1",
	"ASRC1-2",
	"ASRC1-3",
	"ASRC1-4",
	"ASRC1-5",
	"ASRC1-6",
	"ADMAIF17",
	"ADMAIF18",
	"ADMAIF19",
	"ADMAIF20",
	"ADMAIF21",
	"ADMAIF22",
	"ADMAIF23",
	"ADMAIF24",
	"ADMAIF25",
	"ADMAIF26",
	"ADMAIF27",
	"ADMAIF28",
	"ADMAIF29",
	"ADMAIF30",
	"ADMAIF31",
	"ADMAIF32",
};

MUX_ENUM_CTRL_DECL_234(t234_admaif1_tx, 0x00);
MUX_ENUM_CTRL_DECL_234(t234_admaif2_tx, 0x01);
MUX_ENUM_CTRL_DECL_234(t234_admaif3_tx, 0x02);
MUX_ENUM_CTRL_DECL_234(t234_admaif4_tx, 0x03);
MUX_ENUM_CTRL_DECL_234(t234_admaif5_tx, 0x04);
MUX_ENUM_CTRL_DECL_234(t234_admaif6_tx, 0x05);
MUX_ENUM_CTRL_DECL_234(t234_admaif7_tx, 0x06);
MUX_ENUM_CTRL_DECL_234(t234_admaif8_tx, 0x07);
MUX_ENUM_CTRL_DECL_234(t234_admaif9_tx, 0x08);
MUX_ENUM_CTRL_DECL_234(t234_admaif10_tx, 0x09);
MUX_ENUM_CTRL_DECL_234(t234_admaif11_tx, 0x0a);
MUX_ENUM_CTRL_DECL_234(t234_admaif12_tx, 0x0b);
MUX_ENUM_CTRL_DECL_234(t234_admaif13_tx, 0x0c);
MUX_ENUM_CTRL_DECL_234(t234_admaif14_tx, 0x0d);
MUX_ENUM_CTRL_DECL_234(t234_admaif15_tx, 0x0e);
MUX_ENUM_CTRL_DECL_234(t234_admaif16_tx, 0x0f);
MUX_ENUM_CTRL_DECL_234(t234_admaif17_tx, 0x60);
MUX_ENUM_CTRL_DECL_234(t234_admaif18_tx, 0x61);
MUX_ENUM_CTRL_DECL_234(t234_admaif19_tx, 0x62);
MUX_ENUM_CTRL_DECL_234(t234_admaif20_tx, 0x63);
MUX_ENUM_CTRL_DECL_234(t234_i2s1_tx, 0x10);
MUX_ENUM_CTRL_DECL_234(t234_i2s2_tx, 0x11);
MUX_ENUM_CTRL_DECL_234(t234_i2s3_tx, 0x12);
MUX_ENUM_CTRL_DECL_234(t234_i2s4_tx, 0x13);
MUX_ENUM_CTRL_DECL_234(t234_i2s5_tx, 0x14);
MUX_ENUM_CTRL_DECL_234(t234_i2s6_tx, 0x15);
MUX_ENUM_CTRL_DECL_234(t234_sfc1_tx, 0x18);
MUX_ENUM_CTRL_DECL_234(t234_sfc2_tx, 0x19);
MUX_ENUM_CTRL_DECL_234(t234_sfc3_tx, 0x1a);
MUX_ENUM_CTRL_DECL_234(t234_sfc4_tx, 0x1b);
MUX_ENUM_CTRL_DECL_234(t234_amx11_tx, 0x48);
MUX_ENUM_CTRL_DECL_234(t234_amx12_tx, 0x49);
MUX_ENUM_CTRL_DECL_234(t234_amx13_tx, 0x4a);
MUX_ENUM_CTRL_DECL_234(t234_amx14_tx, 0x4b);
MUX_ENUM_CTRL_DECL_234(t234_amx21_tx, 0x4c);
MUX_ENUM_CTRL_DECL_234(t234_amx22_tx, 0x4d);
MUX_ENUM_CTRL_DECL_234(t234_amx23_tx, 0x4e);
MUX_ENUM_CTRL_DECL_234(t234_amx24_tx, 0x4f);
MUX_ENUM_CTRL_DECL_234(t234_amx31_tx, 0x50);
MUX_ENUM_CTRL_DECL_234(t234_amx32_tx, 0x51);
MUX_ENUM_CTRL_DECL_234(t234_amx33_tx, 0x52);
MUX_ENUM_CTRL_DECL_234(t234_amx34_tx, 0x53);
MUX_ENUM_CTRL_DECL_234(t234_amx41_tx, 0x5c);
MUX_ENUM_CTRL_DECL_234(t234_amx42_tx, 0x5d);
MUX_ENUM_CTRL_DECL_234(t234_amx43_tx, 0x5e);
MUX_ENUM_CTRL_DECL_234(t234_amx44_tx, 0x5f);
MUX_ENUM_CTRL_DECL_234(t234_adx1_tx, 0x58);
MUX_ENUM_CTRL_DECL_234(t234_adx2_tx, 0x59);
MUX_ENUM_CTRL_DECL_234(t234_adx3_tx, 0x5a);
MUX_ENUM_CTRL_DECL_234(t234_adx4_tx, 0x5b);
MUX_ENUM_CTRL_DECL_234(t234_mixer11_tx, 0x20);
MUX_ENUM_CTRL_DECL_234(t234_mixer12_tx, 0x21);
MUX_ENUM_CTRL_DECL_234(t234_mixer13_tx, 0x22);
MUX_ENUM_CTRL_DECL_234(t234_mixer14_tx, 0x23);
MUX_ENUM_CTRL_DECL_234(t234_mixer15_tx, 0x24);
MUX_ENUM_CTRL_DECL_234(t234_mixer16_tx, 0x25);
MUX_ENUM_CTRL_DECL_234(t234_mixer17_tx, 0x26);
MUX_ENUM_CTRL_DECL_234(t234_mixer18_tx, 0x27);
MUX_ENUM_CTRL_DECL_234(t234_mixer19_tx, 0x28);
MUX_ENUM_CTRL_DECL_234(t234_mixer110_tx, 0x29);
MUX_ENUM_CTRL_DECL_234(t234_afc1_tx, 0x34);
MUX_ENUM_CTRL_DECL_234(t234_afc2_tx, 0x35);
MUX_ENUM_CTRL_DECL_234(t234_afc3_tx, 0x36);
MUX_ENUM_CTRL_DECL_234(t234_afc4_tx, 0x37);
MUX_ENUM_CTRL_DECL_234(t234_afc5_tx, 0x38);
MUX_ENUM_CTRL_DECL_234(t234_afc6_tx, 0x39);
MUX_ENUM_CTRL_DECL_234(t234_spkprot_tx, 0x41);
MUX_ENUM_CTRL_DECL_234(t234_mvc1_tx, 0x44);
MUX_ENUM_CTRL_DECL_234(t234_mvc2_tx, 0x45);
MUX_ENUM_CTRL_DECL_234(t234_asrc11_tx, 0x64);
MUX_ENUM_CTRL_DECL_234(t234_asrc12_tx, 0x65);
MUX_ENUM_CTRL_DECL_234(t234_asrc13_tx, 0x66);
MUX_ENUM_CTRL_DECL_234(t234_asrc14_tx, 0x67);
MUX_ENUM_CTRL_DECL_234(t234_asrc15_tx, 0x68);
MUX_ENUM_CTRL_DECL_234(t234_asrc16_tx, 0x69);
MUX_ENUM_CTRL_DECL_234(t234_ope1_tx, 0x40);

ADDER_CTRL_DECL(Adder1, 0x0);
ADDER_CTRL_DECL(Adder2, 0x1);
ADDER_CTRL_DECL(Adder3, 0x2);
ADDER_CTRL_DECL(Adder4, 0x3);
ADDER_CTRL_DECL(Adder5, 0x4);

MUX_ENUM_CTRL_DECL_264(t264_admaif1_tx, 0x00);
MUX_ENUM_CTRL_DECL_264(t264_admaif2_tx, 0x01);
MUX_ENUM_CTRL_DECL_264(t264_admaif3_tx, 0x02);
MUX_ENUM_CTRL_DECL_264(t264_admaif4_tx, 0x03);
MUX_ENUM_CTRL_DECL_264(t264_admaif5_tx, 0x04);
MUX_ENUM_CTRL_DECL_264(t264_admaif6_tx, 0x05);
MUX_ENUM_CTRL_DECL_264(t264_admaif7_tx, 0x06);
MUX_ENUM_CTRL_DECL_264(t264_admaif8_tx, 0x07);
MUX_ENUM_CTRL_DECL_264(t264_admaif9_tx, 0x08);
MUX_ENUM_CTRL_DECL_264(t264_admaif10_tx, 0x09);
MUX_ENUM_CTRL_DECL_264(t264_admaif11_tx, 0x0a);
MUX_ENUM_CTRL_DECL_264(t264_admaif12_tx, 0x0b);
MUX_ENUM_CTRL_DECL_264(t264_admaif13_tx, 0x0c);
MUX_ENUM_CTRL_DECL_264(t264_admaif14_tx, 0x0d);
MUX_ENUM_CTRL_DECL_264(t264_admaif15_tx, 0x0e);
MUX_ENUM_CTRL_DECL_264(t264_admaif16_tx, 0x0f);
MUX_ENUM_CTRL_DECL_264(t264_admaif17_tx, 0x60);
MUX_ENUM_CTRL_DECL_264(t264_admaif18_tx, 0x61);
MUX_ENUM_CTRL_DECL_264(t264_admaif19_tx, 0x62);
MUX_ENUM_CTRL_DECL_264(t264_admaif20_tx, 0x63);
MUX_ENUM_CTRL_DECL_264(t264_admaif21_tx, 0x74);
MUX_ENUM_CTRL_DECL_264(t264_admaif22_tx, 0x75);
MUX_ENUM_CTRL_DECL_264(t264_admaif23_tx, 0x76);
MUX_ENUM_CTRL_DECL_264(t264_admaif24_tx, 0x77);
MUX_ENUM_CTRL_DECL_264(t264_admaif25_tx, 0x78);
MUX_ENUM_CTRL_DECL_264(t264_admaif26_tx, 0x79);
MUX_ENUM_CTRL_DECL_264(t264_admaif27_tx, 0x7a);
MUX_ENUM_CTRL_DECL_264(t264_admaif28_tx, 0x7b);
MUX_ENUM_CTRL_DECL_264(t264_admaif29_tx, 0x7c);
MUX_ENUM_CTRL_DECL_264(t264_admaif30_tx, 0x7d);
MUX_ENUM_CTRL_DECL_264(t264_admaif31_tx, 0x7e);
MUX_ENUM_CTRL_DECL_264(t264_admaif32_tx, 0x7f);
MUX_ENUM_CTRL_DECL_264(t264_i2s1_tx, 0x10);
MUX_ENUM_CTRL_DECL_264(t264_i2s2_tx, 0x11);
MUX_ENUM_CTRL_DECL_264(t264_i2s3_tx, 0x12);
MUX_ENUM_CTRL_DECL_264(t264_i2s4_tx, 0x13);
MUX_ENUM_CTRL_DECL_264(t264_i2s5_tx, 0x14);
MUX_ENUM_CTRL_DECL_264(t264_i2s6_tx, 0x15);
MUX_ENUM_CTRL_DECL_264(t264_i2s7_tx, 0x16);
MUX_ENUM_CTRL_DECL_264(t264_i2s8_tx, 0x17);
MUX_ENUM_CTRL_DECL_264(t264_sfc1_tx, 0x18);
MUX_ENUM_CTRL_DECL_264(t264_sfc2_tx, 0x19);
MUX_ENUM_CTRL_DECL_264(t264_sfc3_tx, 0x1a);
MUX_ENUM_CTRL_DECL_264(t264_sfc4_tx, 0x1b);
MUX_ENUM_CTRL_DECL_264(t264_mixer11_tx, 0x20);
MUX_ENUM_CTRL_DECL_264(t264_mixer12_tx, 0x21);
MUX_ENUM_CTRL_DECL_264(t264_mixer13_tx, 0x22);
MUX_ENUM_CTRL_DECL_264(t264_mixer14_tx, 0x23);
MUX_ENUM_CTRL_DECL_264(t264_mixer15_tx, 0x24);
MUX_ENUM_CTRL_DECL_264(t264_mixer16_tx, 0x25);
MUX_ENUM_CTRL_DECL_264(t264_mixer17_tx, 0x26);
MUX_ENUM_CTRL_DECL_264(t264_mixer18_tx, 0x27);
MUX_ENUM_CTRL_DECL_264(t264_mixer19_tx, 0x28);
MUX_ENUM_CTRL_DECL_264(t264_mixer110_tx, 0x29);
MUX_ENUM_CTRL_DECL_264(t264_dspk1_tx, 0x30);
MUX_ENUM_CTRL_DECL_264(t264_afc1_tx, 0x34);
MUX_ENUM_CTRL_DECL_264(t264_afc2_tx, 0x35);
MUX_ENUM_CTRL_DECL_264(t264_afc3_tx, 0x36);
MUX_ENUM_CTRL_DECL_264(t264_afc4_tx, 0x37);
MUX_ENUM_CTRL_DECL_264(t264_afc5_tx, 0x38);
MUX_ENUM_CTRL_DECL_264(t264_afc6_tx, 0x39);
MUX_ENUM_CTRL_DECL_264(t264_ope1_tx, 0x40);
MUX_ENUM_CTRL_DECL_264(t264_mvc1_tx, 0x44);
MUX_ENUM_CTRL_DECL_264(t264_mvc2_tx, 0x45);
MUX_ENUM_CTRL_DECL_264(t264_amx11_tx, 0x48);
MUX_ENUM_CTRL_DECL_264(t264_amx12_tx, 0x49);
MUX_ENUM_CTRL_DECL_264(t264_amx13_tx, 0x4a);
MUX_ENUM_CTRL_DECL_264(t264_amx14_tx, 0x4b);
MUX_ENUM_CTRL_DECL_264(t264_amx21_tx, 0x4c);
MUX_ENUM_CTRL_DECL_264(t264_amx22_tx, 0x4d);
MUX_ENUM_CTRL_DECL_264(t264_amx23_tx, 0x4e);
MUX_ENUM_CTRL_DECL_264(t264_amx24_tx, 0x4f);
MUX_ENUM_CTRL_DECL_264(t264_amx31_tx, 0x50);
MUX_ENUM_CTRL_DECL_264(t264_amx32_tx, 0x51);
MUX_ENUM_CTRL_DECL_264(t264_amx33_tx, 0x52);
MUX_ENUM_CTRL_DECL_264(t264_amx34_tx, 0x53);
MUX_ENUM_CTRL_DECL_264(t264_amx41_tx, 0x5c);
MUX_ENUM_CTRL_DECL_264(t264_amx42_tx, 0x5d);
MUX_ENUM_CTRL_DECL_264(t264_amx43_tx, 0x5e);
MUX_ENUM_CTRL_DECL_264(t264_amx44_tx, 0x5f);
MUX_ENUM_CTRL_DECL_264(t264_amx51_tx, 0x80);
MUX_ENUM_CTRL_DECL_264(t264_amx52_tx, 0x81);
MUX_ENUM_CTRL_DECL_264(t264_amx53_tx, 0x82);
MUX_ENUM_CTRL_DECL_264(t264_amx54_tx, 0x83);
MUX_ENUM_CTRL_DECL_264(t264_amx61_tx, 0x84);
MUX_ENUM_CTRL_DECL_264(t264_amx62_tx, 0x85);
MUX_ENUM_CTRL_DECL_264(t264_amx63_tx, 0x86);
MUX_ENUM_CTRL_DECL_264(t264_amx64_tx, 0x87);
MUX_ENUM_CTRL_DECL_264(t264_adx1_tx, 0x58);
MUX_ENUM_CTRL_DECL_264(t264_adx2_tx, 0x59);
MUX_ENUM_CTRL_DECL_264(t264_adx3_tx, 0x5a);
MUX_ENUM_CTRL_DECL_264(t264_adx4_tx, 0x5b);
MUX_ENUM_CTRL_DECL_264(t264_adx5_tx, 0x88);
MUX_ENUM_CTRL_DECL_264(t264_adx6_tx, 0x89);
MUX_ENUM_CTRL_DECL_264(t264_asrc11_tx, 0x64);
MUX_ENUM_CTRL_DECL_264(t264_asrc12_tx, 0x65);
MUX_ENUM_CTRL_DECL_264(t264_asrc13_tx, 0x66);
MUX_ENUM_CTRL_DECL_264(t264_asrc14_tx, 0x67);
MUX_ENUM_CTRL_DECL_264(t264_asrc15_tx, 0x68);
MUX_ENUM_CTRL_DECL_264(t264_asrc16_tx, 0x69);

static struct snd_soc_dapm_widget tegra264_virt_xbar_widgets[] = {
	WIDGETS("ADMAIF1", t264_admaif1_tx),
	WIDGETS("ADMAIF2", t264_admaif2_tx),
	WIDGETS("ADMAIF3", t264_admaif3_tx),
	WIDGETS("ADMAIF4", t264_admaif4_tx),
	WIDGETS("ADMAIF5", t264_admaif5_tx),
	WIDGETS("ADMAIF6", t264_admaif6_tx),
	WIDGETS("ADMAIF7", t264_admaif7_tx),
	WIDGETS("ADMAIF8", t264_admaif8_tx),
	WIDGETS("ADMAIF9", t264_admaif9_tx),
	WIDGETS("ADMAIF10", t264_admaif10_tx),
	WIDGETS("ADMAIF11", t264_admaif11_tx),
	WIDGETS("ADMAIF12", t264_admaif12_tx),
	WIDGETS("ADMAIF13", t264_admaif13_tx),
	WIDGETS("ADMAIF14", t264_admaif14_tx),
	WIDGETS("ADMAIF15", t264_admaif15_tx),
	WIDGETS("ADMAIF16", t264_admaif16_tx),
	WIDGETS("I2S1", t264_i2s1_tx),
	WIDGETS("I2S2", t264_i2s2_tx),
	WIDGETS("I2S3", t264_i2s3_tx),
	WIDGETS("I2S4", t264_i2s4_tx),
	WIDGETS("I2S5", t264_i2s5_tx),
	WIDGETS("I2S6", t264_i2s6_tx),
	WIDGETS("I2S7", t264_i2s7_tx),
	WIDGETS("I2S8", t264_i2s8_tx),
	WIDGETS("SFC1", t264_sfc1_tx),
	WIDGETS("SFC2", t264_sfc2_tx),
	WIDGETS("SFC3", t264_sfc3_tx),
	WIDGETS("SFC4", t264_sfc4_tx),
	MIXER_IN_WIDGETS("MIXER1-1", t264_mixer11_tx),
	MIXER_IN_WIDGETS("MIXER1-2", t264_mixer12_tx),
	MIXER_IN_WIDGETS("MIXER1-3", t264_mixer13_tx),
	MIXER_IN_WIDGETS("MIXER1-4", t264_mixer14_tx),
	MIXER_IN_WIDGETS("MIXER1-5", t264_mixer15_tx),
	MIXER_IN_WIDGETS("MIXER1-6", t264_mixer16_tx),
	MIXER_IN_WIDGETS("MIXER1-7", t264_mixer17_tx),
	MIXER_IN_WIDGETS("MIXER1-8", t264_mixer18_tx),
	MIXER_IN_WIDGETS("MIXER1-9", t264_mixer19_tx),
	MIXER_IN_WIDGETS("MIXER1-10", t264_mixer110_tx),

	MIXER_OUT_WIDGETS("MIXER1-1"),
	MIXER_OUT_WIDGETS("MIXER1-2"),
	MIXER_OUT_WIDGETS("MIXER1-3"),
	MIXER_OUT_WIDGETS("MIXER1-4"),
	MIXER_OUT_WIDGETS("MIXER1-5"),
	SND_SOC_DAPM_MIXER("Adder1", SND_SOC_NOPM, 1, 0,
		Adder1, ARRAY_SIZE(Adder1)),
	SND_SOC_DAPM_MIXER("Adder2", SND_SOC_NOPM, 1, 0,
		Adder2, ARRAY_SIZE(Adder2)),
	SND_SOC_DAPM_MIXER("Adder3", SND_SOC_NOPM, 1, 0,
		Adder3, ARRAY_SIZE(Adder3)),
	SND_SOC_DAPM_MIXER("Adder4", SND_SOC_NOPM, 1, 0,
		Adder4, ARRAY_SIZE(Adder4)),
	SND_SOC_DAPM_MIXER("Adder5", SND_SOC_NOPM, 1, 0,
		Adder5, ARRAY_SIZE(Adder5)),
	WIDGETS("AFC1", t264_afc1_tx),
	WIDGETS("AFC2", t264_afc2_tx),
	WIDGETS("AFC3", t264_afc3_tx),
	WIDGETS("AFC4", t264_afc4_tx),
	WIDGETS("AFC5", t264_afc5_tx),
	WIDGETS("AFC6", t264_afc6_tx),
	WIDGETS("OPE1", t264_ope1_tx),
	WIDGETS("MVC1", t264_mvc1_tx),
	WIDGETS("MVC2", t264_mvc2_tx),
	WIDGETS("AMX1-1", t264_amx11_tx),
	WIDGETS("AMX1-2", t264_amx12_tx),
	WIDGETS("AMX1-3", t264_amx13_tx),
	WIDGETS("AMX1-4", t264_amx14_tx),
	WIDGETS("AMX2-1", t264_amx21_tx),
	WIDGETS("AMX2-2", t264_amx22_tx),
	WIDGETS("AMX2-3", t264_amx23_tx),
	WIDGETS("AMX2-4", t264_amx24_tx),
	WIDGETS("ADX1", t264_adx1_tx),
	WIDGETS("ADX2", t264_adx2_tx),

	TX_WIDGETS("DMIC1"),
	TX_WIDGETS("DMIC2"),

	TX_WIDGETS("ADX1-1"),
	TX_WIDGETS("ADX1-2"),
	TX_WIDGETS("ADX1-3"),
	TX_WIDGETS("ADX1-4"),

	TX_WIDGETS("ADX2-1"),
	TX_WIDGETS("ADX2-2"),
	TX_WIDGETS("ADX2-3"),
	TX_WIDGETS("ADX2-4"),
	TX_WIDGETS("AMX1"),
	TX_WIDGETS("AMX2"),
	WIDGETS("ADMAIF17", t264_admaif17_tx),
	WIDGETS("ADMAIF18", t264_admaif18_tx),
	WIDGETS("ADMAIF19", t264_admaif19_tx),
	WIDGETS("ADMAIF20", t264_admaif20_tx),
	WIDGETS("AMX3-1", t264_amx31_tx),
	WIDGETS("AMX3-2", t264_amx32_tx),
	WIDGETS("AMX3-3", t264_amx33_tx),
	WIDGETS("AMX3-4", t264_amx34_tx),
	WIDGETS("AMX4-1", t264_amx41_tx),
	WIDGETS("AMX4-2", t264_amx42_tx),
	WIDGETS("AMX4-3", t264_amx43_tx),
	WIDGETS("AMX4-4", t264_amx44_tx),
	WIDGETS("AMX5-1", t264_amx51_tx),
	WIDGETS("AMX5-2", t264_amx52_tx),
	WIDGETS("AMX5-3", t264_amx53_tx),
	WIDGETS("AMX5-4", t264_amx54_tx),
	WIDGETS("AMX6-1", t264_amx61_tx),
	WIDGETS("AMX6-2", t264_amx62_tx),
	WIDGETS("AMX6-3", t264_amx63_tx),
	WIDGETS("AMX6-4", t264_amx64_tx),
	WIDGETS("ADX3", t264_adx3_tx),
	WIDGETS("ADX4", t264_adx4_tx),
	WIDGETS("ADX5", t264_adx5_tx),
	WIDGETS("ADX6", t264_adx6_tx),
	WIDGETS("ASRC1-1", t264_asrc11_tx),
	WIDGETS("ASRC1-2", t264_asrc12_tx),
	WIDGETS("ASRC1-3", t264_asrc13_tx),
	WIDGETS("ASRC1-4", t264_asrc14_tx),
	WIDGETS("ASRC1-5", t264_asrc15_tx),
	WIDGETS("ASRC1-6", t264_asrc16_tx),

	TX_WIDGETS("ADX3-1"),
	TX_WIDGETS("ADX3-2"),
	TX_WIDGETS("ADX3-3"),
	TX_WIDGETS("ADX3-4"),

	TX_WIDGETS("ADX4-1"),
	TX_WIDGETS("ADX4-2"),
	TX_WIDGETS("ADX4-3"),
	TX_WIDGETS("ADX4-4"),
	TX_WIDGETS("ADX5-1"),
	TX_WIDGETS("ADX5-2"),
	TX_WIDGETS("ADX5-3"),
	TX_WIDGETS("ADX5-4"),

	TX_WIDGETS("ADX6-1"),
	TX_WIDGETS("ADX6-2"),
	TX_WIDGETS("ADX6-3"),
	TX_WIDGETS("ADX6-4"),
	TX_WIDGETS("AMX3"),
	TX_WIDGETS("AMX4"),
	TX_WIDGETS("AMX5"),
	TX_WIDGETS("AMX6"),
	TX_WIDGETS("ARAD1"),
	CODEC_WIDGET("I2S1"),
	CODEC_WIDGET("I2S2"),
	CODEC_WIDGET("I2S3"),
	CODEC_WIDGET("I2S4"),
	CODEC_WIDGET("I2S5"),
	CODEC_WIDGET("I2S6"),
	CODEC_WIDGET("I2S7"),
	CODEC_WIDGET("I2S8"),
	WIDGETS("ADMAIF21", t264_admaif21_tx),
	WIDGETS("ADMAIF22", t264_admaif22_tx),
	WIDGETS("ADMAIF23", t264_admaif23_tx),
	WIDGETS("ADMAIF24", t264_admaif24_tx),
	WIDGETS("ADMAIF25", t264_admaif25_tx),
	WIDGETS("ADMAIF26", t264_admaif26_tx),
	WIDGETS("ADMAIF27", t264_admaif27_tx),
	WIDGETS("ADMAIF28", t264_admaif28_tx),
	WIDGETS("ADMAIF29", t264_admaif29_tx),
	WIDGETS("ADMAIF30", t264_admaif30_tx),
	WIDGETS("ADMAIF31", t264_admaif31_tx),
	WIDGETS("ADMAIF32", t264_admaif32_tx),
};

static struct snd_soc_dapm_widget tegra234_virt_xbar_widgets[] = {
	WIDGETS("ADMAIF1", t234_admaif1_tx),
	WIDGETS("ADMAIF2", t234_admaif2_tx),
	WIDGETS("ADMAIF3", t234_admaif3_tx),
	WIDGETS("ADMAIF4", t234_admaif4_tx),
	WIDGETS("ADMAIF5", t234_admaif5_tx),
	WIDGETS("ADMAIF6", t234_admaif6_tx),
	WIDGETS("ADMAIF7", t234_admaif7_tx),
	WIDGETS("ADMAIF8", t234_admaif8_tx),
	WIDGETS("ADMAIF9", t234_admaif9_tx),
	WIDGETS("ADMAIF10", t234_admaif10_tx),
	WIDGETS("I2S1", t234_i2s1_tx),
	WIDGETS("I2S2", t234_i2s2_tx),
	WIDGETS("I2S3", t234_i2s3_tx),
	WIDGETS("I2S4", t234_i2s4_tx),
	WIDGETS("I2S5", t234_i2s5_tx),
	WIDGETS("SFC1", t234_sfc1_tx),
	WIDGETS("SFC2", t234_sfc2_tx),
	WIDGETS("SFC3", t234_sfc3_tx),
	WIDGETS("SFC4", t234_sfc4_tx),
	MIXER_IN_WIDGETS("MIXER1-1", t234_mixer11_tx),
	MIXER_IN_WIDGETS("MIXER1-2", t234_mixer12_tx),
	MIXER_IN_WIDGETS("MIXER1-3", t234_mixer13_tx),
	MIXER_IN_WIDGETS("MIXER1-4", t234_mixer14_tx),
	MIXER_IN_WIDGETS("MIXER1-5", t234_mixer15_tx),
	MIXER_IN_WIDGETS("MIXER1-6", t234_mixer16_tx),
	MIXER_IN_WIDGETS("MIXER1-7", t234_mixer17_tx),
	MIXER_IN_WIDGETS("MIXER1-8", t234_mixer18_tx),
	MIXER_IN_WIDGETS("MIXER1-9", t234_mixer19_tx),
	MIXER_IN_WIDGETS("MIXER1-10", t234_mixer110_tx),

	MIXER_OUT_WIDGETS("MIXER1-1"),
	MIXER_OUT_WIDGETS("MIXER1-2"),
	MIXER_OUT_WIDGETS("MIXER1-3"),
	MIXER_OUT_WIDGETS("MIXER1-4"),
	MIXER_OUT_WIDGETS("MIXER1-5"),
	SND_SOC_DAPM_MIXER("Adder1", SND_SOC_NOPM, 1, 0,
		Adder1, ARRAY_SIZE(Adder1)),
	SND_SOC_DAPM_MIXER("Adder2", SND_SOC_NOPM, 1, 0,
		Adder2, ARRAY_SIZE(Adder2)),
	SND_SOC_DAPM_MIXER("Adder3", SND_SOC_NOPM, 1, 0,
		Adder3, ARRAY_SIZE(Adder3)),
	SND_SOC_DAPM_MIXER("Adder4", SND_SOC_NOPM, 1, 0,
		Adder4, ARRAY_SIZE(Adder4)),
	SND_SOC_DAPM_MIXER("Adder5", SND_SOC_NOPM, 1, 0,
		Adder5, ARRAY_SIZE(Adder5)),
	WIDGETS("AFC1", t234_afc1_tx),
	WIDGETS("AFC2", t234_afc2_tx),
	WIDGETS("AFC3", t234_afc3_tx),
	WIDGETS("AFC4", t234_afc4_tx),
	WIDGETS("AFC5", t234_afc5_tx),
	WIDGETS("AFC6", t234_afc6_tx),
	WIDGETS("OPE1", t234_ope1_tx),
	WIDGETS("SPKPROT1", t234_spkprot_tx),
	WIDGETS("MVC1", t234_mvc1_tx),
	WIDGETS("MVC2", t234_mvc2_tx),
	WIDGETS("AMX1-1", t234_amx11_tx),
	WIDGETS("AMX1-2", t234_amx12_tx),
	WIDGETS("AMX1-3", t234_amx13_tx),
	WIDGETS("AMX1-4", t234_amx14_tx),
	WIDGETS("AMX2-1", t234_amx21_tx),
	WIDGETS("AMX2-2", t234_amx22_tx),
	WIDGETS("AMX2-3", t234_amx23_tx),
	WIDGETS("AMX2-4", t234_amx24_tx),
	WIDGETS("ADX1", t234_adx1_tx),
	WIDGETS("ADX2", t234_adx2_tx),
	TX_WIDGETS("DMIC1"),
	TX_WIDGETS("DMIC2"),
	TX_WIDGETS("DMIC3"),
	TX_WIDGETS("AMX1"),
	TX_WIDGETS("ADX1-1"),
	TX_WIDGETS("ADX1-2"),
	TX_WIDGETS("ADX1-3"),
	TX_WIDGETS("ADX1-4"),
	TX_WIDGETS("AMX2"),
	TX_WIDGETS("ADX2-1"),
	TX_WIDGETS("ADX2-2"),
	TX_WIDGETS("ADX2-3"),
	TX_WIDGETS("ADX2-4"),
	WIDGETS("ADMAIF11", t234_admaif11_tx),
	WIDGETS("ADMAIF12", t234_admaif12_tx),
	WIDGETS("ADMAIF13", t234_admaif13_tx),
	WIDGETS("ADMAIF14", t234_admaif14_tx),
	WIDGETS("ADMAIF15", t234_admaif15_tx),
	WIDGETS("ADMAIF16", t234_admaif16_tx),
	WIDGETS("ADMAIF17", t234_admaif17_tx),
	WIDGETS("ADMAIF18", t234_admaif18_tx),
	WIDGETS("ADMAIF19", t234_admaif19_tx),
	WIDGETS("ADMAIF20", t234_admaif20_tx),
	WIDGETS("I2S6", t234_i2s6_tx),
	WIDGETS("AMX3-1", t234_amx31_tx),
	WIDGETS("AMX3-2", t234_amx32_tx),
	WIDGETS("AMX3-3", t234_amx33_tx),
	WIDGETS("AMX3-4", t234_amx34_tx),
	WIDGETS("AMX4-1", t234_amx41_tx),
	WIDGETS("AMX4-2", t234_amx42_tx),
	WIDGETS("AMX4-3", t234_amx43_tx),
	WIDGETS("AMX4-4", t234_amx44_tx),
	WIDGETS("ADX3", t234_adx3_tx),
	WIDGETS("ADX4", t234_adx4_tx),
	WIDGETS("ASRC1-1", t234_asrc11_tx),
	WIDGETS("ASRC1-2", t234_asrc12_tx),
	WIDGETS("ASRC1-3", t234_asrc13_tx),
	WIDGETS("ASRC1-4", t234_asrc14_tx),
	WIDGETS("ASRC1-5", t234_asrc15_tx),
	WIDGETS("ASRC1-6", t234_asrc16_tx),
	TX_WIDGETS("AMX3"),
	TX_WIDGETS("ADX3-1"),
	TX_WIDGETS("ADX3-2"),
	TX_WIDGETS("ADX3-3"),
	TX_WIDGETS("ADX3-4"),
	TX_WIDGETS("AMX4"),
	TX_WIDGETS("ADX4-1"),
	TX_WIDGETS("ADX4-2"),
	TX_WIDGETS("ADX4-3"),
	TX_WIDGETS("ADX4-4"),
	TX_WIDGETS("DMIC4"),
	TX_WIDGETS("ARAD1"),
	CODEC_WIDGET("I2S1"),
	CODEC_WIDGET("I2S2"),
	CODEC_WIDGET("I2S3"),
	CODEC_WIDGET("I2S4"),
	CODEC_WIDGET("I2S5"),
	CODEC_WIDGET("I2S6"),
};

#define MUX_ROUTES_234(name)						\
	{ name " Mux",      "ADMAIF1",		"ADMAIF1 RX" },		\
	{ name " Mux",      "ADMAIF2",		"ADMAIF2 RX" },		\
	{ name " Mux",      "ADMAIF3",		"ADMAIF3 RX" },		\
	{ name " Mux",      "ADMAIF4",		"ADMAIF4 RX" },		\
	{ name " Mux",      "ADMAIF5",		"ADMAIF5 RX" },		\
	{ name " Mux",      "ADMAIF6",		"ADMAIF6 RX" },		\
	{ name " Mux",      "ADMAIF7",		"ADMAIF7 RX" },		\
	{ name " Mux",      "ADMAIF8",		"ADMAIF8 RX" },		\
	{ name " Mux",      "ADMAIF9",		"ADMAIF9 RX" },		\
	{ name " Mux",      "ADMAIF10",		"ADMAIF10 RX" },	\
	{ name " Mux",      "I2S1",		"I2S1 RX" },		\
	{ name " Mux",      "I2S2",		"I2S2 RX" },		\
	{ name " Mux",      "I2S3",		"I2S3 RX" },		\
	{ name " Mux",      "I2S4",		"I2S4 RX" },		\
	{ name " Mux",      "I2S5",		"I2S5 RX" },		\
	{ name " Mux",      "SFC1",		"SFC1 RX" },		\
	{ name " Mux",      "SFC2",		"SFC2 RX" },		\
	{ name " Mux",      "SFC3",		"SFC3 RX" },		\
	{ name " Mux",      "SFC4",		"SFC4 RX" },		\
	{ name " Mux",      "MIXER1-1",		"MIXER1-1 RX" },	\
	{ name " Mux",      "MIXER1-2",		"MIXER1-2 RX" },	\
	{ name " Mux",      "MIXER1-3",		"MIXER1-3 RX" },	\
	{ name " Mux",      "MIXER1-4",		"MIXER1-4 RX" },	\
	{ name " Mux",      "MIXER1-5",		"MIXER1-5 RX" },	\
	{ name " Mux",      "AFC1",		"AFC1 RX" },		\
	{ name " Mux",      "AFC2",		"AFC2 RX" },		\
	{ name " Mux",      "AFC3",		"AFC3 RX" },		\
	{ name " Mux",      "AFC4",		"AFC4 RX" },		\
	{ name " Mux",      "AFC5",		"AFC5 RX" },		\
	{ name " Mux",      "AFC6",		"AFC6 RX" },		\
	{ name " Mux",      "OPE1",		"OPE1 RX" },		\
	{ name " Mux",      "MVC1",		"MVC1 RX" },		\
	{ name " Mux",      "MVC2",		"MVC2 RX" },		\
	{ name " Mux",      "DMIC1",		"DMIC1 RX" },		\
	{ name " Mux",      "DMIC2",		"DMIC2 RX" },		\
	{ name " Mux",      "DMIC3",		"DMIC3 RX" },		\
	{ name " Mux",      "AMX1",		"AMX1 RX" },		\
	{ name " Mux",      "ADX1-1",		"ADX1-1 RX" },		\
	{ name " Mux",      "ADX1-2",		"ADX1-2 RX" },		\
	{ name " Mux",      "ADX1-3",		"ADX1-3 RX" },		\
	{ name " Mux",      "ADX1-4",		"ADX1-4 RX" },		\
	{ name " Mux",      "AMX2",		"AMX2 RX" },		\
	{ name " Mux",      "ADX2-1",		"ADX2-1 RX" },		\
	{ name " Mux",      "ADX2-2",		"ADX2-2 RX" },		\
	{ name " Mux",      "ADX2-3",		"ADX2-3 RX" },		\
	{ name " Mux",      "ADX2-4",		"ADX2-4 RX" },		\
	{ name " Mux",      "ADMAIF11",		"ADMAIF11 RX" },	\
	{ name " Mux",      "ADMAIF12",		"ADMAIF12 RX" },	\
	{ name " Mux",      "ADMAIF13",		"ADMAIF13 RX" },	\
	{ name " Mux",      "ADMAIF14",		"ADMAIF14 RX" },	\
	{ name " Mux",      "ADMAIF15",		"ADMAIF15 RX" },	\
	{ name " Mux",      "ADMAIF16",		"ADMAIF16 RX" },	\
	{ name " Mux",      "ADMAIF17",		"ADMAIF17 RX" },	\
	{ name " Mux",      "ADMAIF18",		"ADMAIF18 RX" },	\
	{ name " Mux",      "ADMAIF19",		"ADMAIF19 RX" },	\
	{ name " Mux",      "ADMAIF20",		"ADMAIF20 RX" },	\
	{ name " Mux",      "DMIC4",		"DMIC4 RX" },		\
	{ name " Mux",      "I2S6",		"I2S6 RX" },		\
	{ name " Mux",      "ASRC1-1",		"ASRC1-1 RX" },		\
	{ name " Mux",      "ASRC1-2",		"ASRC1-2 RX" },		\
	{ name " Mux",      "ASRC1-3",		"ASRC1-3 RX" },		\
	{ name " Mux",      "ASRC1-4",		"ASRC1-4 RX" },		\
	{ name " Mux",      "ASRC1-5",		"ASRC1-5 RX" },		\
	{ name " Mux",      "ASRC1-6",		"ASRC1-6 RX" },		\
	{ name " Mux",      "AMX3",		"AMX3 RX" },		\
	{ name " Mux",      "ADX3-1",		"ADX3-1 RX" },		\
	{ name " Mux",      "ADX3-2",		"ADX3-2 RX" },		\
	{ name " Mux",      "ADX3-3",		"ADX3-3 RX" },		\
	{ name " Mux",      "ADX3-4",		"ADX3-4 RX" },		\
	{ name " Mux",      "AMX4",		"AMX4 RX" },		\
	{ name " Mux",      "ADX4-1",		"ADX4-1 RX" },		\
	{ name " Mux",      "ADX4-2",		"ADX4-2 RX" },		\
	{ name " Mux",      "ADX4-3",		"ADX4-3 RX" },		\
	{ name " Mux",      "ADX4-4",		"ADX4-4 RX" },		\
	{ name " Mux",      "ARAD1",		"ARAD1 RX" },

#define AMX_OUT_ROUTES(name)						\
	{ name " RX",      NULL,		name "-1 Mux" },	\
	{ name " RX",      NULL,		name "-2 Mux" },	\
	{ name " RX",      NULL,		name "-3 Mux" },	\
	{ name " RX",      NULL,		name "-4 Mux" },

#define ADX_IN_ROUTES_234(name)						\
	{ name "-1 RX",      NULL,		name " Mux" },		\
	{ name "-2 RX",      NULL,		name " Mux" },		\
	{ name "-3 RX",      NULL,		name " Mux" },		\
	{ name "-4 RX",      NULL,		name " Mux" },		\
	TEGRA234_ROUTES(name)

#define IN_OUT_ROUTES_234(name)						\
	{ name " RX",       NULL,		name " CIF Receive"},	\
	{ name " CIF Transmit", NULL,		name " Mux"},		\
	MUX_ROUTES_234(name)

#define TEGRA234_ROUTES(name)						\
	{ name " RX", NULL,		name " Mux"},			\
	MUX_ROUTES_234(name)

#define MIXER_IN_ROUTES_234(name)						\
	MUX_ROUTES_234(name)

#define MIC_SPK_ROUTES_234(name)						\
	{ name " RX",       NULL,		name " MIC"},		\
	{ name " HEADPHONE", NULL,		name " Mux"},		\
	MUX_ROUTES_234(name)

#define MIXER_ROUTES(name, id)	\
	{name,	"RX1",	"MIXER1-1 Mux",},	\
	{name,	"RX2",	"MIXER1-2 Mux",},	\
	{name,	"RX3",	"MIXER1-3 Mux",},	\
	{name,	"RX4",	"MIXER1-4 Mux",},	\
	{name,	"RX5",	"MIXER1-5 Mux",},	\
	{name,	"RX6",	"MIXER1-6 Mux",},	\
	{name,	"RX7",	"MIXER1-7 Mux",},	\
	{name,	"RX8",	"MIXER1-8 Mux",},	\
	{name,	"RX9",	"MIXER1-9 Mux",},	\
	{name,	"RX10",	"MIXER1-10 Mux"},	\
	{"MIXER1-"#id " RX",	NULL,	name}

#define MUX_ROUTES_264(name)						\
	{ name " Mux",      "ADMAIF1",		"ADMAIF1 RX" },		\
	{ name " Mux",      "ADMAIF2",		"ADMAIF2 RX" },		\
	{ name " Mux",      "ADMAIF3",		"ADMAIF3 RX" },		\
	{ name " Mux",      "ADMAIF4",		"ADMAIF4 RX" },		\
	{ name " Mux",      "ADMAIF5",		"ADMAIF5 RX" },		\
	{ name " Mux",      "ADMAIF6",		"ADMAIF6 RX" },		\
	{ name " Mux",      "ADMAIF7",		"ADMAIF7 RX" },		\
	{ name " Mux",      "ADMAIF8",		"ADMAIF8 RX" },		\
	{ name " Mux",      "ADMAIF9",		"ADMAIF9 RX" },		\
	{ name " Mux",      "ADMAIF10",		"ADMAIF10 RX" },	\
	{ name " Mux",      "I2S1",		"I2S1 RX" },		\
	{ name " Mux",      "I2S2",		"I2S2 RX" },		\
	{ name " Mux",      "I2S3",		"I2S3 RX" },		\
	{ name " Mux",      "I2S4",		"I2S4 RX" },		\
	{ name " Mux",      "I2S5",		"I2S5 RX" },		\
	{ name " Mux",      "SFC1",		"SFC1 RX" },		\
	{ name " Mux",      "SFC2",		"SFC2 RX" },		\
	{ name " Mux",      "SFC3",		"SFC3 RX" },		\
	{ name " Mux",      "SFC4",		"SFC4 RX" },		\
	{ name " Mux",      "MIXER1-1",		"MIXER1-1 RX" },	\
	{ name " Mux",      "MIXER1-2",		"MIXER1-2 RX" },	\
	{ name " Mux",      "MIXER1-3",		"MIXER1-3 RX" },	\
	{ name " Mux",      "MIXER1-4",		"MIXER1-4 RX" },	\
	{ name " Mux",      "MIXER1-5",		"MIXER1-5 RX" },	\
	{ name " Mux",      "AFC1",		"AFC1 RX" },		\
	{ name " Mux",      "AFC2",		"AFC2 RX" },		\
	{ name " Mux",      "AFC3",		"AFC3 RX" },		\
	{ name " Mux",      "AFC4",		"AFC4 RX" },		\
	{ name " Mux",      "AFC5",		"AFC5 RX" },		\
	{ name " Mux",      "AFC6",		"AFC6 RX" },		\
	{ name " Mux",      "OPE1",		"OPE1 RX" },		\
	{ name " Mux",      "MVC1",		"MVC1 RX" },		\
	{ name " Mux",      "MVC2",		"MVC2 RX" },		\
	{ name " Mux",      "DMIC1",		"DMIC1 RX" },		\
	{ name " Mux",      "DMIC2",		"DMIC2 RX" },		\
	{ name " Mux",      "AMX1",		"AMX1 RX" },		\
	{ name " Mux",      "ADX1-1",		"ADX1-1 RX" },		\
	{ name " Mux",      "ADX1-2",		"ADX1-2 RX" },		\
	{ name " Mux",      "ADX1-3",		"ADX1-3 RX" },		\
	{ name " Mux",      "ADX1-4",		"ADX1-4 RX" },		\
	{ name " Mux",      "AMX2",		"AMX2 RX" },		\
	{ name " Mux",      "ADX2-1",		"ADX2-1 RX" },		\
	{ name " Mux",      "ADX2-2",		"ADX2-2 RX" },		\
	{ name " Mux",      "ADX2-3",		"ADX2-3 RX" },		\
	{ name " Mux",      "ADX2-4",		"ADX2-4 RX" },		\
	{ name " Mux",      "ADMAIF11",		"ADMAIF11 RX" },	\
	{ name " Mux",      "ADMAIF12",		"ADMAIF12 RX" },	\
	{ name " Mux",      "ADMAIF13",		"ADMAIF13 RX" },	\
	{ name " Mux",      "ADMAIF14",		"ADMAIF14 RX" },	\
	{ name " Mux",      "ADMAIF15",		"ADMAIF15 RX" },	\
	{ name " Mux",      "ADMAIF16",		"ADMAIF16 RX" },	\
	{ name " Mux",      "ADMAIF17",		"ADMAIF17 RX" },	\
	{ name " Mux",      "ADMAIF18",		"ADMAIF18 RX" },	\
	{ name " Mux",      "ADMAIF19",		"ADMAIF19 RX" },	\
	{ name " Mux",      "ADMAIF20",		"ADMAIF20 RX" },	\
	{ name " Mux",      "ADMAIF21",		"ADMAIF21 RX" },	\
	{ name " Mux",      "ADMAIF22",		"ADMAIF22 RX" },	\
	{ name " Mux",      "ADMAIF23",		"ADMAIF23 RX" },	\
	{ name " Mux",      "ADMAIF24",		"ADMAIF24 RX" },	\
	{ name " Mux",      "ADMAIF25",		"ADMAIF25 RX" },	\
	{ name " Mux",      "ADMAIF26",		"ADMAIF26 RX" },	\
	{ name " Mux",      "ADMAIF27",		"ADMAIF27 RX" },	\
	{ name " Mux",      "ADMAIF28",		"ADMAIF28 RX" },	\
	{ name " Mux",      "ADMAIF29",		"ADMAIF29 RX" },	\
	{ name " Mux",      "ADMAIF30",		"ADMAIF30 RX" },	\
	{ name " Mux",      "ADMAIF31",		"ADMAIF31 RX" },	\
	{ name " Mux",      "ADMAIF32",		"ADMAIF32 RX" },	\
	{ name " Mux",      "I2S6",		    "I2S6 RX" },		\
	{ name " Mux",      "I2S7",		    "I2S7 RX" },		\
	{ name " Mux",      "I2S8",		    "I2S8 RX" },		\
	{ name " Mux",      "ASRC1-1",		"ASRC1-1 RX" },		\
	{ name " Mux",      "ASRC1-2",		"ASRC1-2 RX" },		\
	{ name " Mux",      "ASRC1-3",		"ASRC1-3 RX" },		\
	{ name " Mux",      "ASRC1-4",		"ASRC1-4 RX" },		\
	{ name " Mux",      "ASRC1-5",		"ASRC1-5 RX" },		\
	{ name " Mux",      "ASRC1-6",		"ASRC1-6 RX" },		\
	{ name " Mux",      "AMX3",		"AMX3 RX" },		\
	{ name " Mux",      "ADX3-1",		"ADX3-1 RX" },		\
	{ name " Mux",      "ADX3-2",		"ADX3-2 RX" },		\
	{ name " Mux",      "ADX3-3",		"ADX3-3 RX" },		\
	{ name " Mux",      "ADX3-4",		"ADX3-4 RX" },		\
	{ name " Mux",      "AMX4",		"AMX4 RX" },		\
	{ name " Mux",      "ADX4-1",		"ADX4-1 RX" },		\
	{ name " Mux",      "ADX4-2",		"ADX4-2 RX" },		\
	{ name " Mux",      "ADX4-3",		"ADX4-3 RX" },		\
	{ name " Mux",      "ADX4-4",		"ADX4-4 RX" },		\
	{ name " Mux",      "AMX5",		"AMX5 RX" },		\
	{ name " Mux",      "ADX5-1",		"ADX5-1 RX" },		\
	{ name " Mux",      "ADX5-2",		"ADX5-2 RX" },		\
	{ name " Mux",      "ADX5-3",		"ADX5-3 RX" },		\
	{ name " Mux",      "ADX5-4",		"ADX5-4 RX" },		\
	{ name " Mux",      "AMX6",		"AMX6 RX" },		\
	{ name " Mux",      "ADX6-1",		"ADX6-1 RX" },		\
	{ name " Mux",      "ADX6-2",		"ADX6-2 RX" },		\
	{ name " Mux",      "ADX6-3",		"ADX6-3 RX" },		\
	{ name " Mux",      "ADX6-4",		"ADX6-4 RX" },		\
	{ name " Mux",      "ARAD1",		"ARAD1 RX" },

#define ADX_IN_ROUTES_264(name)						\
	{ name "-1 RX",      NULL,		name " Mux" },		\
	{ name "-2 RX",      NULL,		name " Mux" },		\
	{ name "-3 RX",      NULL,		name " Mux" },		\
	{ name "-4 RX",      NULL,		name " Mux" },		\
	TEGRA264_ROUTES(name)

#define IN_OUT_ROUTES_264(name)						\
	{ name " RX",       NULL,		name " CIF Receive"},	\
	{ name " CIF Transmit", NULL,		name " Mux"},		\
	MUX_ROUTES_264(name)

#define TEGRA264_ROUTES(name)						\
	{ name " RX", NULL,		name " Mux"},			\
	MUX_ROUTES_264(name)

#define MIXER_IN_ROUTES_264(name)						\
	MUX_ROUTES_264(name)

#define MIC_SPK_ROUTES_264(name)						\
	{ name " RX",       NULL,		name " MIC"},		\
	{ name " HEADPHONE", NULL,		name " Mux"},		\
	MUX_ROUTES_264(name)

static struct snd_soc_dapm_route tegra234_virt_xbar_routes[] = {
	IN_OUT_ROUTES_234("ADMAIF1")
	IN_OUT_ROUTES_234("ADMAIF2")
	IN_OUT_ROUTES_234("ADMAIF3")
	IN_OUT_ROUTES_234("ADMAIF4")
	IN_OUT_ROUTES_234("ADMAIF5")
	IN_OUT_ROUTES_234("ADMAIF6")
	IN_OUT_ROUTES_234("ADMAIF7")
	IN_OUT_ROUTES_234("ADMAIF8")
	IN_OUT_ROUTES_234("ADMAIF9")
	IN_OUT_ROUTES_234("ADMAIF10")
	MIC_SPK_ROUTES_234("I2S1")
	MIC_SPK_ROUTES_234("I2S2")
	MIC_SPK_ROUTES_234("I2S3")
	MIC_SPK_ROUTES_234("I2S4")
	MIC_SPK_ROUTES_234("I2S5")
	TEGRA234_ROUTES("SFC1")
	TEGRA234_ROUTES("SFC2")
	TEGRA234_ROUTES("SFC3")
	TEGRA234_ROUTES("SFC4")
	MIXER_IN_ROUTES_234("MIXER1-1")
	MIXER_IN_ROUTES_234("MIXER1-2")
	MIXER_IN_ROUTES_234("MIXER1-3")
	MIXER_IN_ROUTES_234("MIXER1-4")
	MIXER_IN_ROUTES_234("MIXER1-5")
	MIXER_IN_ROUTES_234("MIXER1-6")
	MIXER_IN_ROUTES_234("MIXER1-7")
	MIXER_IN_ROUTES_234("MIXER1-8")
	MIXER_IN_ROUTES_234("MIXER1-9")
	MIXER_IN_ROUTES_234("MIXER1-10")

	MIXER_ROUTES("Adder1", 1),
	MIXER_ROUTES("Adder2", 2),
	MIXER_ROUTES("Adder3", 3),
	MIXER_ROUTES("Adder4", 4),
	MIXER_ROUTES("Adder5", 5),

	TEGRA234_ROUTES("AFC1")
	TEGRA234_ROUTES("AFC2")
	TEGRA234_ROUTES("AFC3")
	TEGRA234_ROUTES("AFC4")
	TEGRA234_ROUTES("AFC5")
	TEGRA234_ROUTES("AFC6")
	TEGRA234_ROUTES("OPE1")
	TEGRA234_ROUTES("SPKPROT1")
	TEGRA234_ROUTES("MVC1")
	TEGRA234_ROUTES("MVC2")
	TEGRA234_ROUTES("AMX1-1")
	TEGRA234_ROUTES("AMX1-2")
	TEGRA234_ROUTES("AMX1-3")
	TEGRA234_ROUTES("AMX1-4")
	TEGRA234_ROUTES("AMX2-1")
	TEGRA234_ROUTES("AMX2-2")
	TEGRA234_ROUTES("AMX2-3")
	TEGRA234_ROUTES("AMX2-4")
	ADX_IN_ROUTES_234("ADX1")
	ADX_IN_ROUTES_234("ADX2")
	AMX_OUT_ROUTES("AMX1")
	AMX_OUT_ROUTES("AMX2")
	IN_OUT_ROUTES_234("ADMAIF11")
	IN_OUT_ROUTES_234("ADMAIF12")
	IN_OUT_ROUTES_234("ADMAIF13")
	IN_OUT_ROUTES_234("ADMAIF14")
	IN_OUT_ROUTES_234("ADMAIF15")
	IN_OUT_ROUTES_234("ADMAIF16")
	IN_OUT_ROUTES_234("ADMAIF17")
	IN_OUT_ROUTES_234("ADMAIF18")
	IN_OUT_ROUTES_234("ADMAIF19")
	IN_OUT_ROUTES_234("ADMAIF20")
	TEGRA234_ROUTES("AMX3-1")
	TEGRA234_ROUTES("AMX3-2")
	TEGRA234_ROUTES("AMX3-3")
	TEGRA234_ROUTES("AMX3-4")
	TEGRA234_ROUTES("AMX4-1")
	TEGRA234_ROUTES("AMX4-2")
	TEGRA234_ROUTES("AMX4-3")
	TEGRA234_ROUTES("AMX4-4")
	ADX_IN_ROUTES_234("ADX3")
	ADX_IN_ROUTES_234("ADX4")
	MIC_SPK_ROUTES_234("I2S6")
	TEGRA234_ROUTES("ASRC1-1")
	TEGRA234_ROUTES("ASRC1-2")
	TEGRA234_ROUTES("ASRC1-3")
	TEGRA234_ROUTES("ASRC1-4")
	TEGRA234_ROUTES("ASRC1-5")
	TEGRA234_ROUTES("ASRC1-6")
	AMX_OUT_ROUTES("AMX3")
	AMX_OUT_ROUTES("AMX4")
};

static struct snd_soc_dapm_route tegra264_virt_xbar_routes[] = {
	IN_OUT_ROUTES_264("ADMAIF1")
	IN_OUT_ROUTES_264("ADMAIF2")
	IN_OUT_ROUTES_264("ADMAIF3")
	IN_OUT_ROUTES_264("ADMAIF4")
	IN_OUT_ROUTES_264("ADMAIF5")
	IN_OUT_ROUTES_264("ADMAIF6")
	IN_OUT_ROUTES_264("ADMAIF7")
	IN_OUT_ROUTES_264("ADMAIF8")
	IN_OUT_ROUTES_264("ADMAIF9")
	IN_OUT_ROUTES_264("ADMAIF10")
	IN_OUT_ROUTES_264("ADMAIF11")
	IN_OUT_ROUTES_264("ADMAIF12")
	IN_OUT_ROUTES_264("ADMAIF13")
	IN_OUT_ROUTES_264("ADMAIF14")
	IN_OUT_ROUTES_264("ADMAIF15")
	IN_OUT_ROUTES_264("ADMAIF16")
	MIC_SPK_ROUTES_264("I2S1")
	MIC_SPK_ROUTES_264("I2S2")
	MIC_SPK_ROUTES_264("I2S3")
	MIC_SPK_ROUTES_264("I2S4")
	MIC_SPK_ROUTES_264("I2S5")
	MIC_SPK_ROUTES_264("I2S6")
	MIC_SPK_ROUTES_264("I2S7")
	MIC_SPK_ROUTES_264("I2S8")
	TEGRA264_ROUTES("SFC1")
	TEGRA264_ROUTES("SFC2")
	TEGRA264_ROUTES("SFC3")
	TEGRA264_ROUTES("SFC4")
	MIXER_IN_ROUTES_264("MIXER1-1")
	MIXER_IN_ROUTES_264("MIXER1-2")
	MIXER_IN_ROUTES_264("MIXER1-3")
	MIXER_IN_ROUTES_264("MIXER1-4")
	MIXER_IN_ROUTES_264("MIXER1-5")
	MIXER_IN_ROUTES_264("MIXER1-6")
	MIXER_IN_ROUTES_264("MIXER1-7")
	MIXER_IN_ROUTES_264("MIXER1-8")
	MIXER_IN_ROUTES_264("MIXER1-9")
	MIXER_IN_ROUTES_264("MIXER1-10")
	MIXER_ROUTES("Adder1", 1),
	MIXER_ROUTES("Adder2", 2),
	MIXER_ROUTES("Adder3", 3),
	MIXER_ROUTES("Adder4", 4),
	MIXER_ROUTES("Adder5", 5),
	AMX_OUT_ROUTES("AMX1")
	AMX_OUT_ROUTES("AMX2")
	AMX_OUT_ROUTES("AMX3")
	AMX_OUT_ROUTES("AMX4")
	AMX_OUT_ROUTES("AMX5")
	AMX_OUT_ROUTES("AMX6")
	TEGRA264_ROUTES("AMX1-1")
	TEGRA264_ROUTES("AMX1-2")
	TEGRA264_ROUTES("AMX1-3")
	TEGRA264_ROUTES("AMX1-4")
	TEGRA264_ROUTES("AMX2-1")
	TEGRA264_ROUTES("AMX2-2")
	TEGRA264_ROUTES("AMX2-3")
	TEGRA264_ROUTES("AMX2-4")
	TEGRA264_ROUTES("AMX3-1")
	TEGRA264_ROUTES("AMX3-2")
	TEGRA264_ROUTES("AMX3-3")
	TEGRA264_ROUTES("AMX3-4")
	TEGRA264_ROUTES("AMX4-1")
	TEGRA264_ROUTES("AMX4-2")
	TEGRA264_ROUTES("AMX4-3")
	TEGRA264_ROUTES("AMX4-4")
	TEGRA264_ROUTES("AMX5-1")
	TEGRA264_ROUTES("AMX5-2")
	TEGRA264_ROUTES("AMX5-3")
	TEGRA264_ROUTES("AMX5-4")
	TEGRA264_ROUTES("AMX6-1")
	TEGRA264_ROUTES("AMX6-2")
	TEGRA264_ROUTES("AMX6-3")
	TEGRA264_ROUTES("AMX6-4")
	TEGRA264_ROUTES("AFC1")
	TEGRA264_ROUTES("AFC2")
	TEGRA264_ROUTES("AFC3")
	TEGRA264_ROUTES("AFC4")
	TEGRA264_ROUTES("AFC5")
	TEGRA264_ROUTES("AFC6")
	TEGRA264_ROUTES("OPE1")
	TEGRA264_ROUTES("MVC1")
	TEGRA264_ROUTES("MVC2")
	ADX_IN_ROUTES_264("ADX1")
	ADX_IN_ROUTES_264("ADX2")
	ADX_IN_ROUTES_264("ADX3")
	ADX_IN_ROUTES_264("ADX4")
	ADX_IN_ROUTES_264("ADX5")
	ADX_IN_ROUTES_264("ADX6")
	TEGRA264_ROUTES("ASRC1-1")
	TEGRA264_ROUTES("ASRC1-2")
	TEGRA264_ROUTES("ASRC1-3")
	TEGRA264_ROUTES("ASRC1-4")
	TEGRA264_ROUTES("ASRC1-5")
	TEGRA264_ROUTES("ASRC1-6")
	IN_OUT_ROUTES_264("ADMAIF17")
	IN_OUT_ROUTES_264("ADMAIF18")
	IN_OUT_ROUTES_264("ADMAIF19")
	IN_OUT_ROUTES_264("ADMAIF20")
	IN_OUT_ROUTES_264("ADMAIF21")
	IN_OUT_ROUTES_264("ADMAIF22")
	IN_OUT_ROUTES_264("ADMAIF23")
	IN_OUT_ROUTES_264("ADMAIF24")
	IN_OUT_ROUTES_264("ADMAIF25")
	IN_OUT_ROUTES_264("ADMAIF26")
	IN_OUT_ROUTES_264("ADMAIF27")
	IN_OUT_ROUTES_264("ADMAIF28")
	IN_OUT_ROUTES_264("ADMAIF29")
	IN_OUT_ROUTES_264("ADMAIF30")
	IN_OUT_ROUTES_264("ADMAIF31")
	IN_OUT_ROUTES_264("ADMAIF32")
};

static unsigned int tegra_virt_xbar_read(struct snd_soc_component *component,
					unsigned int reg)
{
	return 0;
}

static int tegra_virt_xbar_write(struct snd_soc_component *component,
		unsigned int reg, unsigned int val)
{
	return 0;
}

static int tegra_virt_xbar_component_probe(struct snd_soc_component *component)
{
	return 0;
}

static const struct snd_soc_component_driver tegra234_virt_xbar_codec = {
	.probe = tegra_virt_xbar_component_probe,
	.read = tegra_virt_xbar_read,
	.write = tegra_virt_xbar_write,
	.dapm_widgets = tegra234_virt_xbar_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tegra234_virt_xbar_widgets),
	.dapm_routes = tegra234_virt_xbar_routes,
	.num_dapm_routes = ARRAY_SIZE(tegra234_virt_xbar_routes),
};

static const struct snd_soc_component_driver tegra264_virt_xbar_codec = {
	.probe = tegra_virt_xbar_component_probe,
	.read = tegra_virt_xbar_read,
	.write = tegra_virt_xbar_write,
	.dapm_widgets = tegra264_virt_xbar_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tegra264_virt_xbar_widgets),
	.dapm_routes = tegra264_virt_xbar_routes,
	.num_dapm_routes = ARRAY_SIZE(tegra264_virt_xbar_routes),
};

int tegra_virt_get_route(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{

	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	int32_t reg = e->reg;
	struct nvaudio_ivc_ctxt *hivc_client =
		nvaudio_ivc_alloc_ctxt(card->dev);
	int err, i = 0;
	struct nvaudio_ivc_msg msg;

	memset(&msg, 0, sizeof(struct nvaudio_ivc_msg));
	msg.cmd = NVAUDIO_XBAR_GET_ROUTE;
	msg.params.xbar_info.rx_reg = reg;

	err = nvaudio_ivc_send_receive(hivc_client,
			&msg,
			sizeof(struct nvaudio_ivc_msg));
	if (err < 0)
		pr_err("%s: error on ivc_send_receive\n", __func__);

	for (i = 0; i < e->items; i++) {
		if (msg.params.xbar_info.bit_pos ==
			e->values[i])
			break;
		}

	if (i == e->items)
		ucontrol->value.integer.value[0] = 0;

	ucontrol->value.integer.value[0] = i;

	if (err < 0)
		return err;

	return 0;
}
EXPORT_SYMBOL(tegra_virt_get_route);


int tegra_virt_put_route(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	int32_t reg = e->reg;
	struct nvaudio_ivc_ctxt *hivc_client =
		nvaudio_ivc_alloc_ctxt(card->dev);
	int err;
	struct nvaudio_ivc_msg msg;
	struct snd_soc_dapm_context *dapm =
				snd_soc_dapm_kcontrol_dapm(kcontrol);

	memset(&msg, 0, sizeof(struct nvaudio_ivc_msg));
	msg.cmd = NVAUDIO_XBAR_SET_ROUTE;
	msg.params.xbar_info.rx_reg = reg;
	msg.params.xbar_info.tx_value =
	e->values[ucontrol->value.integer.value[0]];

	msg.params.xbar_info.tx_idx =
		ucontrol->value.integer.value[0] - 1;
	msg.ack_required = true;

	err = nvaudio_ivc_send_receive(hivc_client,
			&msg,
			sizeof(struct nvaudio_ivc_msg));
	if (err < 0) {
		pr_err("%s: error on ivc_send_receive\n", __func__);

		return err;
	}

	snd_soc_dapm_mux_update_power(dapm, kcontrol,
				ucontrol->value.integer.value[0], e, NULL);

	return 0;
}
EXPORT_SYMBOL(tegra_virt_put_route);

void tegra_virt_set_enum_source(const struct soc_enum *enum_virt)
{
	tegra_virt_enum_source = enum_virt;
}
EXPORT_SYMBOL(tegra_virt_set_enum_source);

static inline const struct soc_enum *tegra_virt_get_enum_source(void)
{
	return tegra_virt_enum_source;
}

int tegra_virt_xbar_register_codec(struct platform_device *pdev)
{

	int ret;

	if (of_device_is_compatible(pdev->dev.of_node,
		"nvidia,tegra264-virt-pcm-oot")) {
		ret = tegra_register_component(&pdev->dev,
				&tegra264_virt_xbar_codec,
				tegra264_virt_xbar_dais,
				ARRAY_SIZE(tegra264_virt_xbar_dais), "xbar");
	} else if (of_device_is_compatible(pdev->dev.of_node,
		"nvidia,tegra234-virt-pcm-oot")) {
		ret = tegra_register_component(&pdev->dev,
				&tegra234_virt_xbar_codec,
				tegra234_virt_xbar_dais,
				ARRAY_SIZE(tegra234_virt_xbar_dais), "xbar");
	}

	if (ret != 0) {
		dev_err(&pdev->dev, "Could not register CODEC: %d\n", ret);
		return -EBUSY;
	}

	return 0;
}
EXPORT_SYMBOL(tegra_virt_xbar_register_codec);
MODULE_AUTHOR("Dipesh Gandhi <dipeshg@nvidia.com>");
MODULE_DESCRIPTION("Tegra Virt ASoC XBAR code");
MODULE_LICENSE("GPL");
