/* SPDX-License-Identifier: MIT */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef DCE_IPC_H
#define DCE_IPC_H

#include <dce-os-lock.h>
#include <dce-os-types.h>
#include <dce-os-ivc.h>

#include <interface/dce-admin-cmds.h>
#include <interface/dce-core-interface-ipc-types.h>
#include <interface/dce-ipc-state.h>
#include <linux/platform/tegra/dce/dce-client-ipc.h>

struct tegra_dce;

#define DCE_IPC_CHANNEL_TYPE_ADMIN 0U
#define DCE_IPC_CHANNEL_TYPE_CPU_CLIENTS 1U

#define DCE_IPC_MAX_IVC_CHANNELS 4U

/**
 * TODO : Move the DispRM max to a config file
 */
#define DCE_DISPRM_CMD_MAX_NFRAMES	        1U
#define DCE_DISPRM_CMD_MAX_FSIZE	        4096U
#define DCE_DISPRM_EVENT_NOTIFY_CMD_MAX_NFRAMES	4U
#define DCE_DISPRM_EVENT_NOTIFY_CMD_MAX_FSIZE	4096U
#define DCE_ADMIN_CMD_MAX_FSIZE		        2048U

#define DCE_IPC_WAIT_TYPE_INVALID	0U
#define DCE_IPC_WAIT_TYPE_RPC		1U

#define DCE_IPC_CHANNEL_VALID		BIT(0)
#define DCE_IPC_CHANNEL_INITIALIZED	BIT(1)
#define DCE_IPC_CHANNEL_SYNCED		BIT(2)
#define DCE_IPC_CHANNEL_MSG_HEADER	BIT(15)

#define DCE_IPC_CH_KMD_TYPE_ADMIN	0U
#define DCE_IPC_CH_KMD_TYPE_RM		1U
#define DCE_IPC_CH_KMD_TYPE_HDCP	2U
#define DCE_IPC_CH_KMD_TYPE_RM_NOTIFY	3U
#define DCE_IPC_CH_KMD_TYPE_MAX		4U
/**
 * struct dce_ipc_signal - Stores ivc channel details
 *
 * @d : Pointer to struct tegra_dce.
 * @ibuff : Pointer to the input data buffer.
 * @obuff : Pointer to the output data buffer.
 * @d_ivc : Pointer to the ivc data structure.
 */
struct dce_ipc_mailbox {
	u8 mb_type;
	u32 mb_num;
};

/**
 * TODO : Use linux doorbell driver
 */
struct dce_ipc_doorbell {
	u32 db_num;
	u32 db_bit;
};

struct dce_ipc_signal_instance {
	u32 type;
	u32 sema_num;
	u32 sema_bit;
	union {
		struct dce_ipc_mailbox mbox;
		struct dce_ipc_doorbell db;
	} form;
	struct dce_ipc_signal *signal;
	struct dce_ipc_signal_instance *next;
};

typedef void (*dce_ipc_signal_notify)(struct tegra_dce *d,
		struct dce_ipc_signal_instance *signal);

struct dce_ipc_signal {
	struct dce_ipc_channel *ch;
	dce_ipc_signal_notify notify;
	struct dce_ipc_signal_instance to_d;
	struct dce_ipc_signal_instance from_d;
};

int dce_ipc_signal_init(struct dce_ipc_channel *chan);


/**
 * struct dce_ipc_region - Contains ivc region specific memory info.
 *
 * @size : total IVC region size.
 * @tx : transmit region info.
 * @rx : receive region info.
 */
struct dce_ipc_region {
	u32 s_offset;
	dma_addr_t iova;
	unsigned long size;
	void *base;
};

struct dce_ipc_queue_info {
	u8 nframes;
	u32 frame_sz;
	dma_addr_t rx_iova;
	dma_addr_t tx_iova;
};

/**
 * struct dce_ipc_channel - Stores ivc channel details
 *
 * @d : Pointer to struct tegra_dce.
 * @ibuff : Pointer to the input data buffer.
 * @obuff : Pointer to the output data buffer.
 * @d_ivc : Pointer to the ivc data structure.
 */
struct dce_ipc_channel {
	u32 flags;
	u32 w_type;
	u32	ch_type;
	u32	ipc_type;
#if defined(NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP)
	struct iosys_map ibuff;
	struct iosys_map obuff;
#else
	void *ibuff;
	void *obuff;
#endif
	dce_os_ivc_t d_ivc;
	struct tegra_dce *d;
	struct dce_os_mutex lock;
	struct dce_ipc_signal signal;
	struct dce_ipc_queue_info q_info;
};

/**
 * struct dce_ipc - Stores ipc data
 *
 * @region - Store data about ivc region in DRAM
 * @ch - Array of pointers to store dce ivc channel info
 */
struct dce_ipc {
	struct dce_ipc_region region;
	struct dce_ipc_channel *ch[DCE_IPC_MAX_IVC_CHANNELS];
};

int dce_ipc_send_message(struct tegra_dce *d,
		u32 ch_type, const void *data, size_t size);

int dce_ipc_read_message(struct tegra_dce *d,
		u32 ch_type, void *data, size_t size);

int dce_ipc_send_message_sync(struct tegra_dce *d,
		u32 ch_type, struct dce_ipc_message *msg);

int dce_ipc_get_channel_info(struct tegra_dce *d,
		struct dce_ipc_queue_info *q_info, u32 ch_index);

void dce_os_ipc_deinit_region_info(struct tegra_dce *d);

int dce_os_ipc_init_region_info(struct tegra_dce *d);

struct tegra_dce *dce_ipc_get_dce_from_ch_unlocked(u32 ch_type);

int dce_ipc_channel_init_unlocked(struct tegra_dce *d, u32 ch_type);

void dce_ipc_channel_deinit_unlocked(struct tegra_dce *d, u32 ch_type);

void dce_ipc_channel_reset(struct tegra_dce *d, u32 ch_type);

uint32_t dce_ipc_get_ipc_type(struct tegra_dce *d, u32 ch_type);

bool dce_ipc_channel_is_ready(struct tegra_dce *d, u32 ch_type);

bool dce_ipc_channel_is_synced(struct tegra_dce *d, u32 ch_type);

u32 dce_ipc_get_cur_wait_type(struct tegra_dce *d, u32 ch_type);

bool dce_ipc_is_data_available(struct tegra_dce *d, u32 ch_type);

int dce_ipc_get_region_iova_info(struct tegra_dce *d, u64 *iova, u32 *size);

int dce_ipc_init_signaling(struct tegra_dce *d, struct dce_ipc_channel *ch);

void dce_ipc_deinit_signaling(struct tegra_dce *d, struct dce_ipc_channel *ch);

#endif
