/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __EP92HX_H__
#define __EP92HX_H__

#include <uapi/misc/ep_earc_audio_ioctl.h>

/* SA_IF Registers */
#define REG_VENDOR_ID_0			0x00
#define REG_VENDOR_ID_1			0x01
#define REG_DEVICE_ID_0			0x02
#define REG_DEVICE_ID_1			0x03
#define REG_VERSION				0x04
#define REG_YEAR				0x05
#define REG_MONTH				0x06
#define REG_DATE				0x07
#define REG_GEN_INFO_0			0x08
#define REG_GEN_INFO_1			0x09
#define REG_GEN_INFO_2			0x0A
#define REG_GEN_INFO_3			0x0B
#define REG_GEN_INFO_4			0x0C
#define REG_GEN_INFO_5			0x0D
#define REG_GEN_INFO_6			0x0E
#define REG_ISP_MODE			0x0F

#define REG_CTRL_0				0x10
#define REG_CTRL_1				0x11
#define REG_CTRL_2				0x12
#define REG_CTRL_3				0x13
#define REG_CTRL_4				0x14
#define REG_CEC_EVT_CODE		0x15
#define REG_CEC_EVT_PARAM1		0x16
#define REG_CEC_EVT_PARAM2		0x17
#define REG_CEC_EVT_PARAM3		0x18
#define REG_CEC_EVT_PARAM4		0x19

#define REG_SYSTEM_STATUS_0		0x20
#define REG_SYSTEM_STATUS_1		0x21
#define REG_AUDIO_STATUS		0x22
#define REG_CHANNEL_STATUS_0	0x23
#define REG_CHANNEL_STATUS_1	0x24
#define REG_CHANNEL_STATUS_2	0x25
#define REG_CHANNEL_STATUS_3	0x26
#define REG_CHANNEL_STATUS_4	0x27
#define REG_AUD_INFO_FRAME_0	0x28
#define REG_AUD_INFO_FRAME_1	0x29
#define REG_AUD_INFO_FRAME_2	0x2A
#define REG_AUD_INFO_FRAME_3	0x2B
#define REG_AUD_INFO_FRAME_4	0x2C
#define REG_AUD_INFO_FRAME_5	0x2D

#define REG_TX_PKT_CTRL_0		0x31
#define REG_TX_PKT_CTRL_1		0x32
#define REG_TX_PKT_CTRL_2		0x33
#define REG_TX_PKT_CTRL_3		0x34
#define REG_TX_PKT_CTRL_4		0x35
#define REG_TX_PKT_CTRL_5		0x36
#define REG_TX_PKT_CTRL_6		0x37

#define REG_TX_PKT_CTRL_7		0x38
#define REG_TX_PKT_CTRL_8		0x39
#define REG_TX_PKT_CTRL_9		0x3A
#define REG_TX_PKT_CTRL_10		0x3B
#define REG_TX_PKT_CTRL_11		0x3C
#define REG_TX_PKT_CTRL_12		0x3D
#define REG_TX_PKT_CTRL_13		0x3E
#define REG_TX_PKT_CTRL_14		0x3F

#define REG_SPDIF_CS_0			0x40
#define REG_SPDIF_CS_1			0x41
#define REG_SPDIF_CS_2			0x42
#define REG_SPDIF_CS_3			0x43
#define REG_SPDIF_CS_4			0x44

#define REG_EARC_LATENCY		0x45
#define REG_EARC_LATENCY_REQ	0x46
#define REG_AUDIO_CAP			0x47

#define REG_MAX					0xFF

/* SA_IF Bit Masks */
/* Gen Info 0 */
#define BIT_EARC_RX_ON			BIT(1)
#define BIT_RX_HOTPLUG			BIT(2)
#define BIT_EARC_TX_ON			BIT(5)
#define BIT_TX_HOTPLUG			BIT(6)

/* Gen Info 1 */
#define BIT_ADO_CHF				BIT(0)
#define BIT_EARC_CAP_CHF		BIT(2)

/* Ctrl 0 */
#define BIT_CEC_DISABLE			BIT(2)
#define BIT_POWER_DOWN			BIT(7)

/* Ctrl 1 */
#define BIT_EARC_TX_MUTE		BIT(0)
#define BIT_AIN_MUTE			BIT(2)

/* Ctrl 2 */
#define A_IN_MASK				(BIT(0) | BIT(1))
#define BIT_TX_LAYOUT			BIT(2)
#define BIT_DF_EN				BIT(3)
#define A_FORMAT_MASK			(BIT(4) | BIT(5))
#define BIT_ARC_EARC_SEL		BIT(7)

/* Ctrl 3 */
#define BIT_SPDIF_OUT_OFF		BIT(0)

/* Ctrl 4 */
#define BIT_TDM_LAYOUT_16		BIT(0)
#define BIT_TDM_IN_SS_SEL		BIT(1)
#define BIT_TDM_IN_EN			BIT(4)
#define BIT_TDM_OUT_EN			BIT(5)
#define BIT_I2S_SPDIF_EN		BIT(7)

/* System status 0 */
#define BIT_MCLK_OK				BIT(6)
#define BIT_RX_LAYOUT			BIT(0)

/* Audio status */
#define BIT_AS_SAMPLE_RATE_SEL	0x07
#define BIT_STD_ADO				BIT(3)
#define BIT_DSD_ADO				BIT(4)
#define BIT_HBR_ADO				BIT(5)
#define BIT_DST_ADO				BIT(6)

/* Channel status 1*/
#define BIT_COMPRESSED_AUDIO	BIT(1)

/* Channel status 3 */
#define BIT_CS_SAMPLE_RATE_SEL	0x0F

/* Channel status 5 */
#define BIT_CS_MULTI_CH_SEL	    0x70

/* Audio Info Frame 1 */
#define BIT_NUM_CHS_SEL			0x07

/* Audio Info Frame 4 and eARC Tx packet control 0 */
#define BIT_CH_ALLOC_SEL		0xFF

/* eARC Tx packet control 7 */
#define BIT_AUDIO_TYPE_0		BIT(0)
#define BIT_AUDIO_TYPE_1		BIT(1)
#define BIT_AUDIO_TYPE_3		BIT(3)
#define BIT_AUDIO_TYPE_4		BIT(4)
#define BIT_AUDIO_TYPE_5		BIT(5)

/* eARC Tx packet control 11 */
#define BIT_MAX_LEN				BIT(0)
#define BIT_SAMP_LEN_SEL		(BIT(1) | BIT(2) | BIT(3))

/* EP92HX Register Default Values */
#define EP92HX_VENDOR_ID_0		0x17
#define EP92HX_VENDOR_ID_1		0x7A
#define EP92HX_DEVICE_ID_0		0x92
#define EP92HX_DEVICE_ID_1		0x12
#define EP92HX_VERSION			0x10
#define EP92HX_YEAR				0x09
#define EP92HX_MONTH			0x07
#define EP92HX_DATE				0x06

/* ISP Mode */
#define EP92HX_ISP_MODE_ENABLE		0x40

/* Audio Status sample rate selector values */
#define AS_SAMPLE_RATE_SEL_32000	0x0
#define AS_SAMPLE_RATE_SEL_44100	0x1
#define AS_SAMPLE_RATE_SEL_48000	0x2
#define AS_SAMPLE_RATE_SEL_88200	0x3
#define AS_SAMPLE_RATE_SEL_96000	0x4
#define AS_SAMPLE_RATE_SEL_176400	0x5
#define AS_SAMPLE_RATE_SEL_192000	0x6
#define AS_SAMPLE_RATE_SEL_768000	0x7

/* Channel status sample rate selector values */
#define CS_SAMPLE_RATE_SEL_44100	0x0
#define CS_SAMPLE_RATE_SEL_48000	0x2
#define CS_SAMPLE_RATE_SEL_32000	0x3
#define CS_SAMPLE_RATE_SEL_22050	0x4
#define CS_SAMPLE_RATE_SEL_24000	0x6
#define CS_SAMPLE_RATE_SEL_88200	0x8
#define CS_SAMPLE_RATE_SEL_96000	0xA
#define CS_SAMPLE_RATE_SEL_176400	0xC
#define CS_SAMPLE_RATE_SEL_192000	0xE

/* Sample rate values */
#define SAMPLE_RATE_22050		22050
#define SAMPLE_RATE_24000		24000
#define SAMPLE_RATE_32000		32000
#define SAMPLE_RATE_44100		44100
#define SAMPLE_RATE_48000		48000
#define SAMPLE_RATE_88200		88200
#define SAMPLE_RATE_96000		96000
#define SAMPLE_RATE_176400		176400
#define SAMPLE_RATE_192000		192000
#define SAMPLE_RATE_768000		768000

/* Number of channels */
#define NUM_CHANNELS_2			2
#define NUM_CHANNELS_3			3
#define NUM_CHANNELS_4			4
#define NUM_CHANNELS_5			5
#define NUM_CHANNELS_6			6
#define NUM_CHANNELS_7			7
#define NUM_CHANNELS_8			8

/* Sample sizes */
#define SAMPLE_SIZE_16			16
#define SAMPLE_SIZE_24			24
#define SAMPLE_SIZE_32			32

/* Polling interval and delay values */
#define POLLING_INTERVAL_MS		20
#define DIFF_OUTPUT_DELAY_MS	100
#define HPD_DELAY_MS			600 /* HW mentions 500ms, add a 100ms buffer */

int set_cec_disable(struct device *dev, bool disable);
int get_cec_disable(struct device *dev, bool *is_disable);

#endif /* __EP92HX_H__ */
