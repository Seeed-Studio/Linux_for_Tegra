/*
 * Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Author: Sheetal <sheetal@nvidia.com>
 *
 * This header provides macros for Tegra ASoC sound bindings.
 */

#ifndef __DT_TEGRA264_ASOC_DAIS_H
#define __DT_TEGRA264_ASOC_DAIS_H

/*
 * DAI links can have one of these value
 * PCM_LINK	: optional, if nothing is specified link is treated as PCM link
 * COMPR_LINK	: required, if link is used with compress device
 * C2C_LINK	: required, for any other back end codec-to-codec links
 */
#define PCM_LINK	0
#define COMPR_LINK	1
#define C2C_LINK	2

/*
 * Following DAI indices are derived from respective module drivers.
 * Thus below values have to be in sync with the DAI arrays defined
 * in the drivers.
 */

#define XBAR_ADMAIF1			0
#define XBAR_ADMAIF2			1
#define XBAR_ADMAIF3			2
#define XBAR_ADMAIF4			3
#define XBAR_ADMAIF5			4
#define XBAR_ADMAIF6			5
#define XBAR_ADMAIF7			6
#define XBAR_ADMAIF8			7
#define XBAR_ADMAIF9			8
#define XBAR_ADMAIF10			9
#define XBAR_ADMAIF11			10
#define XBAR_ADMAIF12			11
#define XBAR_ADMAIF13			12
#define XBAR_ADMAIF14			13
#define XBAR_ADMAIF15			14
#define XBAR_ADMAIF16			15
#define XBAR_ADMAIF17			16
#define XBAR_ADMAIF18			17
#define XBAR_ADMAIF19			18
#define XBAR_ADMAIF20			19
#define XBAR_ADMAIF21			20
#define XBAR_ADMAIF22			21
#define XBAR_ADMAIF23			22
#define XBAR_ADMAIF24			23
#define XBAR_ADMAIF25			24
#define XBAR_ADMAIF26			25
#define XBAR_ADMAIF27			26
#define XBAR_ADMAIF28			27
#define XBAR_ADMAIF29			28
#define XBAR_ADMAIF30			29
#define XBAR_ADMAIF31			30
#define XBAR_ADMAIF32			31
#define XBAR_I2S1			32
#define XBAR_I2S2			33
#define XBAR_I2S3			34
#define XBAR_I2S4			35
#define XBAR_I2S5			36
#define XBAR_I2S6			37
#define XBAR_I2S7			38
#define XBAR_I2S8			39
#define XBAR_DMIC1			40
#define XBAR_DMIC2			41
#define XBAR_DSPK1			42
#define XBAR_SFC1_RX			43

/*
 * There are 2 set of macros are added for SFC, MVC and OPE TX.
 * One is for upstream kernel and another is for downstream kernel.
 * 
 * Because as per downstream kernel code there will be routing issue
 * if DAI names are updated for SFC, MVC and OPE input and
 * output. Due to that using single DAI with same name as downstream
 * kernel for input and output and added output DAIs to keep
 * similar to upstream kernel.
 *
 * Once the routing changes backported to downstream kernel, #else
 * part can be removed.
 */
#ifdef UPSTREAM
#define XBAR_SFC1_TX			44
#define XBAR_SFC2_TX			46
#define XBAR_SFC3_TX			48
#define XBAR_SFC4_TX			50
#define XBAR_MVC1_TX			52
#define XBAR_MVC2_TX			54
#define XBAR_OPE1_TX			144
#else
#define XBAR_SFC1_TX			XBAR_SFC1_RX
#define XBAR_SFC2_TX			XBAR_SFC2_RX
#define XBAR_SFC3_TX			XBAR_SFC3_RX
#define XBAR_SFC4_TX			XBAR_SFC4_RX
#define XBAR_MVC1_TX			XBAR_MVC1_RX
#define XBAR_MVC2_TX			XBAR_MVC2_RX
#define XBAR_OPE1_TX			XBAR_OPE1_RX
#endif

#define XBAR_SFC2_RX			45
#define XBAR_SFC3_RX			47
#define XBAR_SFC4_RX			49
#define XBAR_MVC1_RX			51
#define XBAR_MVC2_RX			53
#define XBAR_AMX1_IN1			55
#define XBAR_AMX1_IN2			56
#define XBAR_AMX1_IN3			57
#define XBAR_AMX1_IN4			58
#define XBAR_AMX1_OUT			59
#define XBAR_AMX2_IN1			60
#define XBAR_AMX2_IN2			61
#define XBAR_AMX2_IN3			62
#define XBAR_AMX2_IN4			63
#define XBAR_AMX2_OUT			64
#define XBAR_AMX3_IN1			65
#define XBAR_AMX3_IN2			66
#define XBAR_AMX3_IN3			67
#define XBAR_AMX3_IN4			68
#define XBAR_AMX3_OUT			69
#define XBAR_AMX4_IN1			70
#define XBAR_AMX4_IN2			71
#define XBAR_AMX4_IN3			72
#define XBAR_AMX4_IN4			73
#define XBAR_AMX4_OUT			74
#define XBAR_AMX5_IN1			75
#define XBAR_AMX5_IN2			76
#define XBAR_AMX5_IN3			77
#define XBAR_AMX5_IN4			78
#define XBAR_AMX5_OUT			79
#define XBAR_AMX6_IN1			80
#define XBAR_AMX6_IN2			81
#define XBAR_AMX6_IN3			82
#define XBAR_AMX6_IN4			83
#define XBAR_AMX6_OUT			84
#define XBAR_ADX1_IN			85
#define XBAR_ADX1_OUT1			86
#define XBAR_ADX1_OUT2			87
#define XBAR_ADX1_OUT3			88
#define XBAR_ADX1_OUT4			89
#define XBAR_ADX2_IN			90
#define XBAR_ADX2_OUT1			91
#define XBAR_ADX2_OUT2			92
#define XBAR_ADX2_OUT3			93
#define XBAR_ADX2_OUT4			94
#define XBAR_ADX3_IN			95
#define XBAR_ADX3_OUT1			96
#define XBAR_ADX3_OUT2			97
#define XBAR_ADX3_OUT3			98
#define XBAR_ADX3_OUT4			99
#define XBAR_ADX4_IN			100
#define XBAR_ADX4_OUT1			101
#define XBAR_ADX4_OUT2			102
#define XBAR_ADX4_OUT3			103
#define XBAR_ADX4_OUT4			104
#define XBAR_ADX5_IN			105
#define XBAR_ADX5_OUT1			106
#define XBAR_ADX5_OUT2			107
#define XBAR_ADX5_OUT3			108
#define XBAR_ADX5_OUT4			109
#define XBAR_ADX6_IN			110
#define XBAR_ADX6_OUT1			111
#define XBAR_ADX6_OUT2			112
#define XBAR_ADX6_OUT3			113
#define XBAR_ADX6_OUT4			114
#define XBAR_MIXER_IN1			115
#define XBAR_MIXER_IN2			116
#define XBAR_MIXER_IN3			117
#define XBAR_MIXER_IN4			118
#define XBAR_MIXER_IN5			119
#define XBAR_MIXER_IN6			120
#define XBAR_MIXER_IN7			121
#define XBAR_MIXER_IN8			122
#define XBAR_MIXER_IN9			123
#define XBAR_MIXER_IN10			124
#define XBAR_MIXER_OUT1			125
#define XBAR_MIXER_OUT2			126
#define XBAR_MIXER_OUT3			127
#define XBAR_MIXER_OUT4			128
#define XBAR_MIXER_OUT5			129
#define XBAR_ASRC_IN1			130
#define XBAR_ASRC_OUT1			131
#define XBAR_ASRC_IN2			132
#define XBAR_ASRC_OUT2			133
#define XBAR_ASRC_IN3			134
#define XBAR_ASRC_OUT3			135
#define XBAR_ASRC_IN4			136
#define XBAR_ASRC_OUT4			137
#define XBAR_ASRC_IN5			138
#define XBAR_ASRC_OUT5			139
#define XBAR_ASRC_IN6			140
#define XBAR_ASRC_OUT6			141
#define XBAR_ASRC_IN7			142
#define XBAR_OPE1_RX			143
#define XBAR_AFC1			145
#define XBAR_AFC2			146
#define XBAR_AFC3			147
#define XBAR_AFC4			148
#define XBAR_AFC5			149
#define XBAR_AFC6			150
#define XBAR_ARAD			151

/* ADMAIF DAIs */
#define ADMAIF1				0
#define ADMAIF2				1
#define ADMAIF3				2
#define ADMAIF4				3
#define ADMAIF5				4
#define ADMAIF6				5
#define ADMAIF7				6
#define ADMAIF8				7
#define ADMAIF9				8
#define ADMAIF10			9
#define ADMAIF11			10
#define ADMAIF12			11
#define ADMAIF13			12
#define ADMAIF14			13
#define ADMAIF15			14
#define ADMAIF16			15
#define ADMAIF17			16
#define ADMAIF18			17
#define ADMAIF19			18
#define ADMAIF20			19
#define ADMAIF21			20
#define ADMAIF22			21
#define ADMAIF23			22
#define ADMAIF24			23
#define ADMAIF25			24
#define ADMAIF26			25
#define ADMAIF27			26
#define ADMAIF28			27
#define ADMAIF29			28
#define ADMAIF30			29
#define ADMAIF31			30
#define ADMAIF32			31

/*
 * ADMAIF_CIF: DAIs used for codec-to-codec links between ADMAIF and XBAR.
 * Offset depends on the number of ADMAIF channels for a chip.
 * The DAI indices for these are derived from below offsets.
 */
#define TEGRA264_ADMAIF_CIF_OFFSET	32

/* I2S */
#define I2S_CIF		0
#define I2S_DAP		1

/* DMIC */
#define DMIC_CIF	0
#define DMIC_DAP	1

/* DSPK */
#define DSPK_CIF	0
#define DSPK_DAP	1

/* SFC */
#define SFC_IN		0
#define SFC_OUT		1

/* MIXER */
#define MIXER_IN1	0
#define MIXER_IN2	1
#define MIXER_IN3	2
#define MIXER_IN4	3
#define MIXER_IN5	4
#define MIXER_IN6	5
#define MIXER_IN7	6
#define MIXER_IN8	7
#define MIXER_IN9	8
#define MIXER_IN10	9
#define MIXER_OUT1	10
#define MIXER_OUT2	11
#define MIXER_OUT3	12
#define MIXER_OUT4	13
#define MIXER_OUT5	14

/* AFC */
#define AFC_IN		0
#define AFC_OUT		1

/* OPE */
#define OPE_IN		0
#define OPE_OUT		1

/* MVC */
#define MVC_IN		0
#define MVC_OUT		1

/* AMX */
#define AMX_IN1		0
#define AMX_IN2		1
#define AMX_IN3		2
#define AMX_IN4		3
#define AMX_OUT		4

/* ADX */
#ifdef UPSTREAM
#define ADX_IN		0
#define ADX_OUT1	1
#define ADX_OUT2	2
#define ADX_OUT3	3
#define ADX_OUT4	4
#else
#define ADX_OUT1	0
#define ADX_OUT2	1
#define ADX_OUT3	2
#define ADX_OUT4	3
#define ADX_IN		4
#endif

/* ASRC */
#define ASRC_IN1	0
#define ASRC_IN2	1
#define ASRC_IN3	2
#define ASRC_IN4	3
#define ASRC_IN5	4
#define ASRC_IN6	5
#define ASRC_IN7	6
#define ASRC_OUT1	7
#define ASRC_OUT2	8
#define ASRC_OUT3	9
#define ASRC_OUT4	10
#define ASRC_OUT5	11
#define ASRC_OUT6	12

/* ARAD */
#define ARAD		0

#endif
