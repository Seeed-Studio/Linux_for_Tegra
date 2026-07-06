/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

/**
 * @file dce-psc-client-priv.h
 * @brief DCE PSC client private structure definitions
 */

#ifndef DCE_PSC_CLIENT_PRIV_H
#define DCE_PSC_CLIENT_PRIV_H

#include <linux/device.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox_controller.h>
#include <linux/completion.h>
#include <linux/mutex.h>

#define DCE_PSC_MBOX_MSG_LEN	64
#define DCE_PSC_TX_TIMEOUT	2000	/* 2 seconds */
#define DCE_PSC_WAIT_TIMEOUT	5000	/* 5 seconds */
#define DCE_PSC_MAX_DMA_SIZE	(256 * 1024 * 1024U)	/* 256MB */

/**
 * struct dce_psc_client - DCE PSC client context
 *
 * @dev: Device pointer
 * @cl: Mailbox client structure
 * @chan: Mailbox channel
 * @lock: Mutex for serializing access
 * @rx_complete: Completion for RX synchronization
 * @rx_msg: Buffer for received messages
 * @initialized: Client initialization status
 */
struct dce_psc_client {
	struct device *dev;
	struct mbox_client cl;
	struct mbox_chan *chan;
	struct mutex lock;
	struct completion rx_complete;
	u8 rx_msg[DCE_PSC_MBOX_MSG_LEN];
	bool initialized;
};

/**
 * union dce_psc_mbox_msg - PSC mailbox message format
 *
 * @opcode: Command opcode (2 x 32-bit)
 * @tx_size: Size of TX data buffer
 * @rx_size: Size of RX data buffer
 * @tx_iova: DMA address of TX buffer
 * @rx_iova: DMA address of RX buffer
 * @data: Raw data view (16 x 32-bit words)
 */
union dce_psc_mbox_msg {
	struct {
		u32 opcode[2];
		u32 tx_size;
		u32 rx_size;
		u64 tx_iova;
		u64 rx_iova;
	};
	u32 data[16];
};

#endif /* DCE_PSC_CLIENT_PRIV_H */
