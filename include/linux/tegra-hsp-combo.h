/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef INCLUDE_RTCPU_HSP_COMBO_H
#define INCLUDE_RTCPU_HSP_COMBO_H

#include <linux/types.h>

struct camrtc_hsp;
struct device;

struct camrtc_hsp *camrtc_hsp_create(
	struct device *dev,
	void (*group_notify)(struct device *dev, u16 group),
	long cmd_timeout);

void camrtc_hsp_free(struct camrtc_hsp *camhsp);

void camrtc_hsp_group_ring(struct camrtc_hsp *camhsp,
		u16 group);

int camrtc_hsp_sync(struct camrtc_hsp *camhsp);
int camrtc_hsp_resume(struct camrtc_hsp *camhsp);
int camrtc_hsp_suspend(struct camrtc_hsp *camhsp);
int camrtc_hsp_bye(struct camrtc_hsp *camhsp);
int camrtc_hsp_ch_setup(struct camrtc_hsp *camhsp, dma_addr_t iova);
int camrtc_hsp_ping(struct camrtc_hsp *camhsp, u32 data, long timeout);
int camrtc_hsp_get_fw_hash(struct camrtc_hsp *camhsp,
		u8 hash[], size_t hash_size);
int camrtc_hsp_set_operating_point(struct camrtc_hsp *camhsp,
		uint32_t operating_point);

int camrtc_hsp_set_panic_callback(struct camrtc_hsp *camhsp,
		void (*panic_callback)(struct device *dev));


#endif	/* INCLUDE_RTCPU_HSP_COMBO_H */
