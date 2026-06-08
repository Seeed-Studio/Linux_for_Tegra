// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <nvidia/conftest.h>

#include <linux/module.h>
#include <linux/of.h>
#include <linux/export.h>
#include <sound/soc.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/soc.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>


#include "tegra_asoc_machine_virt_alt.h"

#define CODEC_NAME		NULL

#define DAI_NAME(i)		"AUDIO" #i
#define STREAM_NAME		"playback"
#define LINK_CPU_NAME		DRV_NAME
#define CPU_DAI_NAME(i)		"ADMAIF" #i
#define CODEC_DAI_NAME		"dit-hifi"
#define PLATFORM_NAME		LINK_CPU_NAME
#define LINK_ADSP_NAME		"tegra210-adsp-virt"
#define ADSP_DAI_NAME(i)		"ADSP ADMAIF"  #i
#define ADSP_CPU_DAI_NAME(i)		"ADSP-ADMAIF"  #i
#define ADSP_PCM_DAI_NAME(i)		"ADSP PCM" #i
#define ADSP_FE_DAI_NAME(i)		"ADSP-FE" #i

static unsigned int num_dai_links;
static const struct snd_soc_pcm_stream default_params = {
	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
};

static const struct snd_soc_pcm_stream adsp_default_params = {
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
};

static struct snd_soc_pcm_stream adsp_admaif_params[MAX_ADMAIF_IDS];

#define TEGRA_SND_SOC_DAILINK_DEFS(id, name) \
	SND_SOC_DAILINK_DEFS(audio##id, \
	DAILINK_COMP_ARRAY(COMP_CODEC(LINK_CPU_NAME, CPU_DAI_NAME(id))), \
	DAILINK_COMP_ARRAY(COMP_CODEC(LINK_CPU_NAME, "ADMAIF" name " CIF")), \
	DAILINK_COMP_ARRAY(COMP_PLATFORM(PLATFORM_NAME)));

#define TEGRA_SND_SOC_ADSP_DAILINK_DEFS(id, name) \
	SND_SOC_DAILINK_DEFS(adsp_admaif##id, \
	DAILINK_COMP_ARRAY(COMP_CODEC(LINK_ADSP_NAME, ADSP_CPU_DAI_NAME(id))), \
	DAILINK_COMP_ARRAY(COMP_CODEC(LINK_CPU_NAME, "ADMAIF" name " CIF")));

#define TEGRA_SND_SOC_ADSP_PCM_DAILINK_DEFS(id, name) \
	SND_SOC_DAILINK_DEFS(adsp_pcm##id, \
	DAILINK_COMP_ARRAY(COMP_CODEC(LINK_ADSP_NAME, ADSP_PCM_DAI_NAME(id))), \
	DAILINK_COMP_ARRAY(COMP_CODEC(LINK_ADSP_NAME, ADSP_FE_DAI_NAME(id))), \
	DAILINK_COMP_ARRAY(COMP_PLATFORM(LINK_ADSP_NAME)));

TEGRA_SND_SOC_DAILINK_DEFS(1, "1")
TEGRA_SND_SOC_DAILINK_DEFS(2, "2")
TEGRA_SND_SOC_DAILINK_DEFS(3, "3")
TEGRA_SND_SOC_DAILINK_DEFS(4, "4")
TEGRA_SND_SOC_DAILINK_DEFS(5, "5")
TEGRA_SND_SOC_DAILINK_DEFS(6, "6")
TEGRA_SND_SOC_DAILINK_DEFS(7, "7")
TEGRA_SND_SOC_DAILINK_DEFS(8, "8")
TEGRA_SND_SOC_DAILINK_DEFS(9, "9")
TEGRA_SND_SOC_DAILINK_DEFS(10, "10")
TEGRA_SND_SOC_DAILINK_DEFS(11, "11")
TEGRA_SND_SOC_DAILINK_DEFS(12, "12")
TEGRA_SND_SOC_DAILINK_DEFS(13, "13")
TEGRA_SND_SOC_DAILINK_DEFS(14, "14")
TEGRA_SND_SOC_DAILINK_DEFS(15, "15")
TEGRA_SND_SOC_DAILINK_DEFS(16, "16")
TEGRA_SND_SOC_DAILINK_DEFS(17, "17")
TEGRA_SND_SOC_DAILINK_DEFS(18, "18")
TEGRA_SND_SOC_DAILINK_DEFS(19, "19")
TEGRA_SND_SOC_DAILINK_DEFS(20, "20")
TEGRA_SND_SOC_DAILINK_DEFS(21, "21")
TEGRA_SND_SOC_DAILINK_DEFS(22, "22")
TEGRA_SND_SOC_DAILINK_DEFS(23, "23")
TEGRA_SND_SOC_DAILINK_DEFS(24, "24")
TEGRA_SND_SOC_DAILINK_DEFS(25, "25")
TEGRA_SND_SOC_DAILINK_DEFS(26, "26")
TEGRA_SND_SOC_DAILINK_DEFS(27, "27")
TEGRA_SND_SOC_DAILINK_DEFS(28, "28")
TEGRA_SND_SOC_DAILINK_DEFS(29, "29")
TEGRA_SND_SOC_DAILINK_DEFS(30, "30")
TEGRA_SND_SOC_DAILINK_DEFS(31, "31")
TEGRA_SND_SOC_DAILINK_DEFS(32, "32")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(1, "1")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(2, "2")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(3, "3")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(4, "4")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(5, "5")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(6, "6")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(7, "7")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(8, "8")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(9, "9")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(10, "10")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(11, "11")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(12, "12")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(13, "13")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(14, "14")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(15, "15")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(16, "16")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(17, "17")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(18, "18")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(19, "19")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(20, "20")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(21, "21")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(22, "22")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(23, "23")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(24, "24")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(25, "25")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(26, "26")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(27, "27")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(28, "28")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(29, "29")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(30, "20")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(31, "31")
TEGRA_SND_SOC_ADSP_DAILINK_DEFS(32, "32")

TEGRA_SND_SOC_ADSP_PCM_DAILINK_DEFS(1, "1")
TEGRA_SND_SOC_ADSP_PCM_DAILINK_DEFS(2, "2")
TEGRA_SND_SOC_ADSP_PCM_DAILINK_DEFS(3, "3")
TEGRA_SND_SOC_ADSP_PCM_DAILINK_DEFS(4, "4")
TEGRA_SND_SOC_ADSP_PCM_DAILINK_DEFS(5, "5")
TEGRA_SND_SOC_ADSP_PCM_DAILINK_DEFS(6, "6")
TEGRA_SND_SOC_ADSP_PCM_DAILINK_DEFS(7, "7")
TEGRA_SND_SOC_ADSP_PCM_DAILINK_DEFS(8, "8")
TEGRA_SND_SOC_ADSP_PCM_DAILINK_DEFS(9, "9")
TEGRA_SND_SOC_ADSP_PCM_DAILINK_DEFS(10, "10")
TEGRA_SND_SOC_ADSP_PCM_DAILINK_DEFS(11, "11")
TEGRA_SND_SOC_ADSP_PCM_DAILINK_DEFS(12, "12")
TEGRA_SND_SOC_ADSP_PCM_DAILINK_DEFS(13, "13")
TEGRA_SND_SOC_ADSP_PCM_DAILINK_DEFS(14, "14")
TEGRA_SND_SOC_ADSP_PCM_DAILINK_DEFS(15, "15")


#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
#define TEGRA_DAI_LINK(id) \
	{ \
		.name = DAI_NAME(id), \
		.stream_name = STREAM_NAME, \
		.c2c_params = &default_params, \
		.ignore_pmdown_time = 1, \
		.ignore_suspend = 0, \
		SND_SOC_DAILINK_REG(audio##id), \
	},
#define TEGRA_ADSP_DAI_LINK(id) \
	{ \
		.name = ADSP_DAI_NAME(id), \
		.stream_name = STREAM_NAME, \
		.c2c_params = &adsp_default_params, \
		.ignore_pmdown_time = 1, \
		.ignore_suspend = 0, \
		SND_SOC_DAILINK_REG(adsp_admaif##id), \
	},
#define TEGRA_ADSP_PCM_DAI_LINK(id) \
	{ \
		.name = ADSP_PCM_DAI_NAME(id), \
		.stream_name = ADSP_PCM_DAI_NAME(id), \
		.ignore_pmdown_time = 1, \
		.ignore_suspend = 0, \
		SND_SOC_DAILINK_REG(adsp_pcm##id), \
	},
#else
#define TEGRA_DAI_LINK(id) \
	{ \
		.name = DAI_NAME(id), \
		.stream_name = STREAM_NAME, \
		.params = &default_params, \
		.ignore_pmdown_time = 1, \
		.ignore_suspend = 0, \
		SND_SOC_DAILINK_REG(audio##id), \
	},
#define TEGRA_ADSP_DAI_LINK(id) \
	{ \
		.name = ADSP_DAI_NAME(id), \
		.stream_name = STREAM_NAME, \
		.params = &adsp_default_params, \
		.ignore_pmdown_time = 1, \
		.ignore_suspend = 0, \
		SND_SOC_DAILINK_REG(adsp_admaif##id), \
	},

#define TEGRA_ADSP_PCM_DAI_LINK(id) \
	{ \
		.name = ADSP_PCM_DAI_NAME(id), \
		.stream_name = ADSP_PCM_DAI_NAME(id), \
		.ignore_pmdown_time = 1, \
		.ignore_suspend = 0, \
		SND_SOC_DAILINK_REG(adsp_pcm##id), \
	},
#endif

static struct snd_soc_dai_link tegra_virt_t264ref_pcm_links[] = {
	TEGRA_DAI_LINK(1)
	TEGRA_DAI_LINK(2)
	TEGRA_DAI_LINK(3)
	TEGRA_DAI_LINK(4)
	TEGRA_DAI_LINK(5)
	TEGRA_DAI_LINK(6)
	TEGRA_DAI_LINK(7)
	TEGRA_DAI_LINK(8)
	TEGRA_DAI_LINK(9)
	TEGRA_DAI_LINK(10)
	TEGRA_DAI_LINK(11)
	TEGRA_DAI_LINK(12)
	TEGRA_DAI_LINK(13)
	TEGRA_DAI_LINK(14)
	TEGRA_DAI_LINK(15)
	TEGRA_DAI_LINK(16)
	TEGRA_DAI_LINK(17)
	TEGRA_DAI_LINK(18)
	TEGRA_DAI_LINK(19)
	TEGRA_DAI_LINK(20)
	TEGRA_DAI_LINK(21)
	TEGRA_DAI_LINK(22)
	TEGRA_DAI_LINK(23)
	TEGRA_DAI_LINK(24)
	TEGRA_DAI_LINK(25)
	TEGRA_DAI_LINK(26)
	TEGRA_DAI_LINK(27)
	TEGRA_DAI_LINK(28)
	TEGRA_DAI_LINK(29)
	TEGRA_DAI_LINK(30)
	TEGRA_DAI_LINK(31)
	TEGRA_DAI_LINK(32)
	TEGRA_ADSP_DAI_LINK(1)
	TEGRA_ADSP_DAI_LINK(2)
	TEGRA_ADSP_DAI_LINK(3)
	TEGRA_ADSP_DAI_LINK(4)
	TEGRA_ADSP_DAI_LINK(5)
	TEGRA_ADSP_DAI_LINK(6)
	TEGRA_ADSP_DAI_LINK(7)
	TEGRA_ADSP_DAI_LINK(8)
	TEGRA_ADSP_DAI_LINK(9)
	TEGRA_ADSP_DAI_LINK(10)
	TEGRA_ADSP_DAI_LINK(11)
	TEGRA_ADSP_DAI_LINK(12)
	TEGRA_ADSP_DAI_LINK(13)
	TEGRA_ADSP_DAI_LINK(14)
	TEGRA_ADSP_DAI_LINK(15)
	TEGRA_ADSP_DAI_LINK(16)
	TEGRA_ADSP_DAI_LINK(17)
	TEGRA_ADSP_DAI_LINK(18)
	TEGRA_ADSP_DAI_LINK(19)
	TEGRA_ADSP_DAI_LINK(20)
	TEGRA_ADSP_DAI_LINK(21)
	TEGRA_ADSP_DAI_LINK(22)
	TEGRA_ADSP_DAI_LINK(23)
	TEGRA_ADSP_DAI_LINK(24)
	TEGRA_ADSP_DAI_LINK(25)
	TEGRA_ADSP_DAI_LINK(26)
	TEGRA_ADSP_DAI_LINK(27)
	TEGRA_ADSP_DAI_LINK(28)
	TEGRA_ADSP_DAI_LINK(29)
	TEGRA_ADSP_DAI_LINK(30)
	TEGRA_ADSP_DAI_LINK(31)
	TEGRA_ADSP_DAI_LINK(32)
	TEGRA_ADSP_PCM_DAI_LINK(1)
	TEGRA_ADSP_PCM_DAI_LINK(2)
	TEGRA_ADSP_PCM_DAI_LINK(3)
	TEGRA_ADSP_PCM_DAI_LINK(4)
	TEGRA_ADSP_PCM_DAI_LINK(5)
	TEGRA_ADSP_PCM_DAI_LINK(6)
	TEGRA_ADSP_PCM_DAI_LINK(7)
	TEGRA_ADSP_PCM_DAI_LINK(8)
	TEGRA_ADSP_PCM_DAI_LINK(9)
	TEGRA_ADSP_PCM_DAI_LINK(10)
	TEGRA_ADSP_PCM_DAI_LINK(11)
	TEGRA_ADSP_PCM_DAI_LINK(12)
	TEGRA_ADSP_PCM_DAI_LINK(13)
	TEGRA_ADSP_PCM_DAI_LINK(14)
	TEGRA_ADSP_PCM_DAI_LINK(15)
};

static struct snd_soc_dai_link tegra_virt_t186ref_pcm_links[] = {
	{
		/* 0 */
		.name = DAI_NAME(1),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(audio1),
	},
	{
		/* 1 */
		.name = DAI_NAME(2),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(audio2),
	},
	{
		/* 2 */
		.name = DAI_NAME(3),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(audio3),
	},
	{
		/* 3 */
		.name = DAI_NAME(4),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(audio4),
	},
	{
		/* 4 */
		.name = DAI_NAME(5),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(audio5),
	},
	{
		/* 5 */
		.name = DAI_NAME(6),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(audio6),
	},
	{
		/* 6 */
		.name = DAI_NAME(7),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(audio7),
	},
	{
		/* 7 */
		.name = DAI_NAME(8),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(audio8),
	},
	{
		/* 8 */
		.name = DAI_NAME(9),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(audio9),
	},
	{
		/* 9 */
		.name = DAI_NAME(10),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(audio10),
	},
	{
		/* 10 */
		.name = DAI_NAME(11),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(audio11),
	},
	{
		/* 11 */
		.name = DAI_NAME(12),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(audio12),
	},
	{
		/* 12 */
		.name = DAI_NAME(13),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(audio13),
	},
	{
		/* 13 */
		.name = DAI_NAME(14),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(audio14),
	},
	{
		/* 14 */
		.name = DAI_NAME(15),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(audio15),
	},
	{
		/* 15 */
		.name = DAI_NAME(16),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(audio16),
	},
	{
		/* 16 */
		.name = DAI_NAME(17),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(audio17),
	},
	{
		/* 17 */
		.name = DAI_NAME(18),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(audio18),
	},
	{
		/* 18 */
		.name = DAI_NAME(19),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(audio19),
	},
	{
		/* 19 */
		.name = DAI_NAME(20),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(audio20),
	},
	{
		/* 20 */
		.name = "ADSP ADMAIF1",
		.stream_name = "ADSP AFMAIF1",
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &adsp_default_params,
#else
		.params = &adsp_default_params,
#endif
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(adsp_admaif1),
	},
	{
		/* 21 */
		.name = "ADSP ADMAIF2",
		.stream_name = "ADSP AFMAIF2",
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &adsp_default_params,
#else
		.params = &adsp_default_params,
#endif
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(adsp_admaif2),
	},
	{
		/* 22 */
		.name = "ADSP ADMAIF3",
		.stream_name = "ADSP AFMAIF3",
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &adsp_default_params,
#else
		.params = &adsp_default_params,
#endif
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(adsp_admaif3),
	},
	{
		/* 23 */
		.name = "ADSP ADMAIF4",
		.stream_name = "ADSP AFMAIF4",
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &adsp_default_params,
#else
		.params = &adsp_default_params,
#endif
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(adsp_admaif4),
	},
	{
		/* 24 */
		.name = "ADSP ADMAIF5",
		.stream_name = "ADSP AFMAIF5",
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &adsp_default_params,
#else
		.params = &adsp_default_params,
#endif
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(adsp_admaif5),
	},
	{
		/* 25 */
		.name = "ADSP ADMAIF6",
		.stream_name = "ADSP AFMAIF6",
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &adsp_default_params,
#else
		.params = &adsp_default_params,
#endif
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(adsp_admaif6),
	},
	{
		/* 26 */
		.name = "ADSP ADMAIF7",
		.stream_name = "ADSP AFMAIF7",
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &adsp_default_params,
#else
		.params = &adsp_default_params,
#endif
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(adsp_admaif7),
	},
	{
		/* 27 */
		.name = "ADSP ADMAIF8",
		.stream_name = "ADSP AFMAIF8",
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &adsp_default_params,
#else
		.params = &adsp_default_params,
#endif
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(adsp_admaif8),
	},
	{
		/* 28 */
		.name = "ADSP ADMAIF9",
		.stream_name = "ADSP AFMAIF9",
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &adsp_default_params,
#else
		.params = &adsp_default_params,
#endif
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(adsp_admaif9),
	},
	{
		/* 29 */
		.name = "ADSP ADMAIF10",
		.stream_name = "ADSP AFMAIF10",
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &adsp_default_params,
#else
		.params = &adsp_default_params,
#endif
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(adsp_admaif10),
	},
	{
		/* 30 */
		.name = "ADSP ADMAIF11",
		.stream_name = "ADSP AFMAIF11",
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &adsp_default_params,
#else
		.params = &adsp_default_params,
#endif
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(adsp_admaif11),
	},
	{
		/* 31 */
		.name = "ADSP ADMAIF12",
		.stream_name = "ADSP AFMAIF12",
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &adsp_default_params,
#else
		.params = &adsp_default_params,
#endif
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(adsp_admaif12),
	},
	{
		/* 32 */
		.name = "ADSP ADMAIF13",
		.stream_name = "ADSP AFMAIF13",
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &adsp_default_params,
#else
		.params = &adsp_default_params,
#endif
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(adsp_admaif13),
	},
	{
		/* 33 */
		.name = "ADSP ADMAIF14",
		.stream_name = "ADSP AFMAIF14",
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &adsp_default_params,
#else
		.params = &adsp_default_params,
#endif
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(adsp_admaif14),
	},
	{
		/* 34 */
		.name = "ADSP ADMAIF15",
		.stream_name = "ADSP AFMAIF15",
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &adsp_default_params,
#else
		.params = &adsp_default_params,
#endif
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(adsp_admaif15),
	},
	{
		/* 35 */
		.name = "ADSP ADMAIF16",
		.stream_name = "ADSP AFMAIF16",
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &adsp_default_params,
#else
		.params = &adsp_default_params,
#endif
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(adsp_admaif16),
	},
	{
		/* 36 */
		.name = "ADSP ADMAIF17",
		.stream_name = "ADSP AFMAIF17",
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &adsp_default_params,
#else
		.params = &adsp_default_params,
#endif
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(adsp_admaif17),
	},
	{
		/* 37 */
		.name = "ADSP ADMAIF18",
		.stream_name = "ADSP AFMAIF18",
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &adsp_default_params,
#else
		.params = &adsp_default_params,
#endif
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(adsp_admaif18),
	},
	{
		/* 38 */
		.name = "ADSP ADMAIF19",
		.stream_name = "ADSP AFMAIF19",
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &adsp_default_params,
#else
		.params = &adsp_default_params,
#endif
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(adsp_admaif19),
	},
	{
		/* 39 */
		.name = "ADSP ADMAIF20",
		.stream_name = "ADSP AFMAIF20",
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &adsp_default_params,
#else
		.params = &adsp_default_params,
#endif
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(adsp_admaif20),
	},
	{
		/* 40 */
		.name = "ADSP PCM1",
		.stream_name = "ADSP PCM1",
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(adsp_pcm1),
	},
	{
		/* 41 */
		.name = "ADSP PCM2",
		.stream_name = "ADSP PCM2",
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(adsp_pcm2),
	},
	{
		.name = "ADSP PCM3",
		.stream_name = "ADSP PCM3",
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(adsp_pcm3),
	},
	{
		.name = "ADSP PCM4",
		.stream_name = "ADSP PCM4",
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(adsp_pcm4),
	},
	{
		.name = "ADSP PCM5",
		.stream_name = "ADSP PCM5",
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(adsp_pcm5),
	},
	{
		.name = "ADSP PCM6",
		.stream_name = "ADSP PCM6",
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(adsp_pcm6),
	},
	{
		.name = "ADSP PCM7",
		.stream_name = "ADSP PCM7",
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(adsp_pcm7),
	},
	{
		.name = "ADSP PCM8",
		.stream_name = "ADSP PCM8",
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(adsp_pcm8),
	},
	{
		.name = "ADSP PCM9",
		.stream_name = "ADSP PCM9",
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(adsp_pcm9),
	},
	{
		.name = "ADSP PCM10",
		.stream_name = "ADSP PCM10",
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(adsp_pcm10),
	},
	{
		.name = "ADSP PCM11",
		.stream_name = "ADSP PCM11",
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(adsp_pcm11),
	},
	{
		.name = "ADSP PCM12",
		.stream_name = "ADSP PCM12",
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(adsp_pcm12),
	},
	{
		.name = "ADSP PCM13",
		.stream_name = "ADSP PCM13",
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(adsp_pcm13),
	},
	{
		.name = "ADSP PCM14",
		.stream_name = "ADSP PCM14",
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(adsp_pcm14),
	},
	{
		.name = "ADSP PCM15",
		.stream_name = "ADSP PCM15",
		.ignore_pmdown_time = 1,
		.ignore_suspend = 0,
		SND_SOC_DAILINK_REG(adsp_pcm15),
	},
};

static struct snd_soc_dai_link tegra_virt_t210ref_pcm_links[] = {
	{
		/* 0 */
		.name = DAI_NAME(1),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(audio1),
	},
	{
		/* 1 */
		.name = DAI_NAME(2),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(audio2),
	},
	{
		/* 2 */
		.name = DAI_NAME(3),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(audio3),
	},
	{
		/* 3 */
		.name = DAI_NAME(4),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(audio4),
	},
	{
		/* 4 */
		.name = DAI_NAME(5),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(audio5),
	},
	{
		/* 5 */
		.name = DAI_NAME(6),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(audio6),
	},
	{
		/* 6 */
		.name = DAI_NAME(7),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(audio7),
	},
	{
		/* 7 */
		.name = DAI_NAME(8),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(audio8),
	},
	{
		/* 8 */
		.name = DAI_NAME(9),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(audio9),
	},
	{
		/* 9 */
		.name = DAI_NAME(10),
		.stream_name = STREAM_NAME,
#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
		.c2c_params = &default_params,
#else
		.params = &default_params,
#endif
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(audio10),
	},
};

void tegra_virt_machine_set_num_dai_links(unsigned int val)
{
	num_dai_links = val;
}
EXPORT_SYMBOL(tegra_virt_machine_set_num_dai_links);

unsigned int tegra_virt_machine_get_num_dai_links(void)
{
	return num_dai_links;
}
EXPORT_SYMBOL(tegra_virt_machine_get_num_dai_links);

struct snd_soc_dai_link *tegra_virt_machine_get_dai_link(struct device *dev)
{
	struct snd_soc_dai_link *link = tegra_virt_t186ref_pcm_links;
	unsigned int size = TEGRA186_XBAR_DAI_LINKS;

	if (of_machine_is_compatible("nvidia,tegra210")) {
		link = tegra_virt_t210ref_pcm_links;
		size = TEGRA210_XBAR_DAI_LINKS;
	} else if (of_device_is_compatible(dev->of_node,
			"nvidia,tegra264-virt-pcm-oot")) {
		link = tegra_virt_t264ref_pcm_links;
		size = ARRAY_SIZE(tegra_virt_t264ref_pcm_links);
	}

	tegra_virt_machine_set_num_dai_links(size);
	return link;
}
EXPORT_SYMBOL(tegra_virt_machine_get_dai_link);

void tegra_virt_machine_set_adsp_admaif_dai_params(
		struct device *dev, uint32_t id, struct snd_soc_pcm_stream *params)
{
	struct snd_soc_dai_link *link = tegra_virt_t186ref_pcm_links;

	if (of_machine_is_compatible("nvidia,tegra210")) {
		link = tegra_virt_t210ref_pcm_links;
	} else if (of_device_is_compatible(dev->of_node,
			"nvidia,tegra264-virt-pcm-oot")) {
		link = tegra_virt_t264ref_pcm_links;
	}

	/* Check for valid ADSP ADMAIF ID */
	if (id >= MAX_ADMAIF_IDS) {
		pr_err("Invalid ADSP ADMAIF ID: %d\n", id);
		return;
	}

	/* Find DAI link corresponding to ADSP ADMAIF */
	link += id + MAX_ADMAIF_IDS;

	memcpy(&adsp_admaif_params[id], params,
		sizeof(struct snd_soc_pcm_stream));

#if defined(NV_SND_SOC_DAI_LINK_STRUCT_HAS_C2C_PARAMS_ARG) /* Linux v6.4 */
	link->c2c_params = &adsp_admaif_params[id];
#else
	link->params = &adsp_admaif_params[id];
#endif
}
EXPORT_SYMBOL(tegra_virt_machine_set_adsp_admaif_dai_params);

MODULE_AUTHOR("Dipesh Gandhi <dipeshg@nvidia.com>");
MODULE_DESCRIPTION("Tegra Virt ASoC machine code");
MODULE_LICENSE("GPL");
