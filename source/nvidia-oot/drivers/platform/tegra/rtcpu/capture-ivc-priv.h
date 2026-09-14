/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef __CAPTURE_IVC_PRIV_H__
#define __CAPTURE_IVC_PRIV_H__

/** Total number of capture channels (2 * VI + ISP). T234 has two VI units */
#define NUM_CAPTURE_CHANNELS 88

/** Temporary ids for the clients whose channel-id is not yet allocated */
#define NUM_CAPTURE_TRANSACTION_IDS NUM_CAPTURE_CHANNELS

/** Total number of channels including Temporary IDs */
#define TOTAL_CHANNELS (NUM_CAPTURE_CHANNELS + NUM_CAPTURE_TRANSACTION_IDS)
#define TRANS_ID_START_IDX NUM_CAPTURE_CHANNELS

/**
 * @brief Callback context of an IVC channel.
 */
struct tegra_capture_ivc_cb_ctx {
	/** Linked list of callback contexts */
	struct list_head node;
	/** Callback function registered by client */
	tegra_capture_ivc_cb_func cb_func;
	/** Private context of a VI/ISP capture context */
	const void *priv_context;
	struct semaphore sem_ch;
};

/**
 * @brief IVC channel context.
 */
struct tegra_capture_ivc {
	/** Pointer to IVC channel */
	struct tegra_ivc_channel *chan;
	/** Callback context lock */
	struct mutex cb_ctx_lock;
	/** Channel write lock */
	struct mutex ivc_wr_lock;
	/** Deferred work */
	struct kthread_work work;
	/** ivc worker thread **/
	struct kthread_worker ivc_worker;
	/** task struct **/
	struct task_struct *ivc_kthread;
	/** Channel work queue head */
	wait_queue_head_t write_q;
	/** Array holding callbacks registered by each channel */
	struct tegra_capture_ivc_cb_ctx cb_ctx[TOTAL_CHANNELS];
	/** spinlock protecting access to linked list */
	spinlock_t avl_ctx_list_lock;
	/** Linked list holding callback contexts */
	struct list_head avl_ctx_list;
};

/**
 * @brief Standard message header for all capture IVC messages.
 */
struct tegra_capture_ivc_msg_header {
	/** Message identifier. */
	uint32_t msg_id;
	union {
		/** Channel identifier. */
		uint32_t channel_id;
		/** Transaction id */
		uint32_t transaction;
	};
} __aligned(8);

/**
 * @brief Response of IVC msg
 */
struct tegra_capture_ivc_resp {
	/** IVC msg header. See @ref tegra_capture_ivc_msg_header */
	struct tegra_capture_ivc_msg_header header;
};

/** Pointer holding the Control IVC channel context, created during probe call*/
static struct tegra_capture_ivc *__scivc_control;

/** Pointer holding the Capture IVC channel context, created during probe call*/
static struct tegra_capture_ivc *__scivc_capture;

/**
 * @brief Worker thread to handle the asynchronous msgs on the IVC channel.
	This will further calls callbacks registered by Channel drivers.
 *
 * @param[in]	work	kthread_work pointer
 */
static void tegra_capture_ivc_worker(
	struct kthread_work *work);

/**
 * @brief Implementation of IVC notify operation which gets called when we any
 * 	new message on the bus for the channel. This signals the worker thread.
 *
 * @param[in]	chan	tegra_ivc_channel channel pointer
 */
static void tegra_capture_ivc_notify(
	struct tegra_ivc_channel *chan);

/**
 * @brief Implementation of probe operation which gets called during boot
 *
 * @param[in,out]	chan	tegra_ivc_channel channel pointer
 *
 * @returns	0 (success), neg. errno (failure)
 */
static int tegra_capture_ivc_probe(
	struct tegra_ivc_channel *chan);

/**
 * @brief Implementation of remove operation
 *
 * @param[in]	chan	tegra_ivc_channel channel pointer
 */
static void tegra_capture_ivc_remove(
	struct tegra_ivc_channel *chan);

/**
 * @brief Function to transmit the IVC msg after checking if it can write,
 *	using Tegra IVC core library APIs.
 *
 * @param[in]	civc	IVC channel on which the msg needs to be transmitted.
 * @param[in]	req	IVC msg blob.
 * @param[in]	len	IVC msg length.
 *
 * @returns	0 (success), neg. errno (failure)
 */
static int tegra_capture_ivc_tx(
	struct tegra_capture_ivc *civc,
	const void *req,
	size_t len);

#endif /* __CAPTURE_IVC_PRIV_H__ */
