/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 */

#ifndef _UAPI_EARC_AUDIO_IOCTL_H
#define _UAPI_EARC_AUDIO_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

/* Enum for input audio formats */
enum AudioFormat {
	PCM_2CH,
	PCM_MULTI_CH,
	DOLBY_DIGITAL,
	DTS,
	DOLBY_DIGITAL_PLUS,
	DOLBY_TRUE_HD,
	DTS_MA,
	DOLBY_ATMOS,
	DTS_X
};

/* Enum for input audio types */
enum AudioInputType {
	SPDIF = 0,
	I2S = 1,
	DSD = 2,
	HBR = 3
};

/* Enum for PCM layout */
enum PCMLayout {
	LAYOUT_0 = 0,
	LAYOUT_1 = 1
};

/* Enum for compression layout */
enum CompressionLayout {
	COMPRESS_LAYOUT_A = 2,
	COMPRESS_LAYOUT_B = 3
};

/* eARC Tx params */
struct earc_tx_params {
	enum AudioInputType inp_type;
	enum AudioFormat fmt;
	enum CompressionLayout cmp_layout;
	uint32_t sample_rate;
	uint8_t sample_size;
	uint8_t num_chs;
} __packed;

/* eARC Rx params */
struct earc_rx_params {
	enum AudioFormat fmt;
	uint32_t sample_rate;
	uint8_t sample_size;
	uint8_t num_chs;
	uint8_t ch_alloc;
} __packed;

/* eARC sink capabilities */
struct earc_sink_cap {
	size_t audio_cap_size;
	uint8_t *audio_cap_data;
};

#define EARC_IOCTL_DEVICE_NAME "earc_audio_driver"
#define EARC_AUDIO_IOCTL_MAGIC 'E'
#define EARC_AUDIO_SET_TX_PARAMS _IOW(EARC_AUDIO_IOCTL_MAGIC, 1, struct earc_tx_params)
#define EARC_AUDIO_SET_SPDIF_ENABLE _IOW(EARC_AUDIO_IOCTL_MAGIC, 2, uint8_t)
#define EARC_AUDIO_SET_TX_TDM_ENABLE _IOW(EARC_AUDIO_IOCTL_MAGIC, 3, uint8_t)
#define EARC_AUDIO_SET_RX_TDM_ENABLE _IOW(EARC_AUDIO_IOCTL_MAGIC, 4, uint8_t)
#define EARC_AUDIO_STOP_TX _IO(EARC_AUDIO_IOCTL_MAGIC, 5)
#define EARC_AUDIO_STOP_RX _IO(EARC_AUDIO_IOCTL_MAGIC, 6)
#define EARC_AUDIO_GET_RX_PARAMS _IOR(EARC_AUDIO_IOCTL_MAGIC, 7, struct earc_rx_params)
#define EARC_AUDIO_GET_SINK_CAP _IOR(EARC_AUDIO_IOCTL_MAGIC, 8, struct earc_sink_cap)

#endif /* _UAPI_EARC_AUDIO_IOCTL_H */
