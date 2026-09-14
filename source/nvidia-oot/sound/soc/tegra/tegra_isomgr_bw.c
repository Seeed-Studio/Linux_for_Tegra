// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2023 NVIDIA CORPORATION. All rights reserved.
 *
 * ADMA bandwidth calculation
 */

#include <linux/module.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "tegra_isomgr_bw.h"
#include <linux/interconnect.h>

#define MAX_BW	393216 /*Maximum KiloByte*/
#define MAX_DEV_NUM 256

static struct adma_isomgr {
	int current_bandwidth;
	bool device_number[MAX_DEV_NUM];
	int bw_per_device[MAX_DEV_NUM];
	struct mutex mutex;
	/* icc_path handle handle */
	struct icc_path *icc_path_handle;
} *adma;

void tegra_isomgr_adma_setbw(struct snd_pcm_substream *substream,
				bool is_running)
{
	int bandwidth, sample_bytes;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_pcm *pcm = substream->pcm;

	if (!adma || !runtime || !pcm)
		return;

	if (pcm->device >= MAX_DEV_NUM) {
		pr_err("%s: PCM device number is greater than %d\n", __func__,
			MAX_DEV_NUM);
		return;
	}

	if (((adma->device_number[pcm->device] == true) && is_running) ||
		((adma->device_number[pcm->device] == false) && !is_running)
		)
		return;

	mutex_lock(&adma->mutex);

	if (is_running) {
		sample_bytes = snd_pcm_format_width(runtime->format)/8;
		if (sample_bytes < 0)
			sample_bytes = 0;

		/* KB/s kilo bytes per sec */
		bandwidth = runtime->channels * (runtime->rate/1000) *
				sample_bytes;

		adma->device_number[pcm->device] = true;
		adma->current_bandwidth += bandwidth;
		adma->bw_per_device[pcm->device] = bandwidth;
	} else {
		adma->device_number[pcm->device] = false;
		adma->current_bandwidth -= adma->bw_per_device[pcm->device];
		adma->bw_per_device[pcm->device] = 0;
	}

	mutex_unlock(&adma->mutex);

	if (adma->current_bandwidth < 0) {
		pr_err("%s: ADMA ISO BW can't be less than zero\n", __func__);
		adma->current_bandwidth = 0;
	} else if (adma->current_bandwidth > MAX_BW) {
		pr_err("%s: ADMA ISO BW can't be more than %d\n", __func__,
			MAX_BW);
		adma->current_bandwidth = MAX_BW;
	}

	if (adma->icc_path_handle)
		icc_set_bw(adma->icc_path_handle, adma->current_bandwidth, MAX_BW);
}
EXPORT_SYMBOL(tegra_isomgr_adma_setbw);

void tegra_isomgr_adma_register(struct device *dev)
{
	adma = kzalloc(sizeof(struct adma_isomgr), GFP_KERNEL);
	if (!adma) {
		pr_err("%s: Failed to allocate adma isomgr struct\n", __func__);
		return;
	}

	adma->current_bandwidth = 0;
	adma->icc_path_handle = NULL;
	memset(&adma->device_number, 0, sizeof(bool) * MAX_DEV_NUM);
	memset(&adma->bw_per_device, 0, sizeof(int) * MAX_DEV_NUM);

	mutex_init(&adma->mutex);

	adma->icc_path_handle = devm_of_icc_get(dev, "write");
	if (IS_ERR(adma->icc_path_handle)) {
		pr_err("%s: Failed to register Interconnect. err=%ld\n",
		__func__, PTR_ERR(adma->icc_path_handle));
		adma->icc_path_handle = NULL;
		tegra_isomgr_adma_unregister(dev);
	}
}
EXPORT_SYMBOL(tegra_isomgr_adma_register);

void tegra_isomgr_adma_unregister(struct device *dev)
{
	if (!adma)
		return;

	mutex_destroy(&adma->mutex);

	if (adma->icc_path_handle) {
		icc_put(adma->icc_path_handle);
		adma->icc_path_handle = NULL;
	}

	kfree(adma);
	adma = NULL;
}
EXPORT_SYMBOL(tegra_isomgr_adma_unregister);
MODULE_AUTHOR("Mohan Kumar <mkumard@nvidia.com>");
MODULE_DESCRIPTION("Tegra ADMA Bandwidth Request driver");
MODULE_LICENSE("GPL");
