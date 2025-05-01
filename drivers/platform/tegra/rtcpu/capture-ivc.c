// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include <linux/tegra-capture-ivc.h>

#include <linux/completion.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <soc/tegra/ivc_ext.h>
#include <linux/tegra-ivc-bus.h>
#include <linux/nospec.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/version.h>
#include <asm/barrier.h>

#include <trace/events/tegra_capture.h>

#include "capture-ivc-priv.h"

/**
 * @brief Transmit a message over IVC channel with mutex protection
 *
 * This function transmits a message over the specified IVC channel with mutex
 * protection.
 * - Validates that the channel is valid and ready
 * - Acquires the write lock to ensure exclusive access to the channel using
 *   @ref mutex_lock_interruptible()
 * - Waits for the channel to be available for writing using @ref wait_event_interruptible()
 * - Calls @ref tegra_ivc_write() to write the message to the channel
 * - Releases the write lock using @ref mutex_unlock()
 *
 * @param[in] civc  Pointer to the capture IVC context
 *                  Valid value: non-NULL
 * @param[in] req   Pointer to the message to be transmitted
 *                  Valid value: non-NULL
 * @param[in] len   Length of the message in bytes
 *                  Valid range: > 0
 *
 * @retval -EIO     If channel is NULL or not ready
 * @retval -ERESTARTSYS  If signal received while waiting for the write queue
 * @retval (int)     Return code propagated from @ref mutex_lock_interruptible() or
 *                   @ref tegra_ivc_write() or @ref wait_event_interruptible()
 */
static int tegra_capture_ivc_tx_(struct tegra_capture_ivc *civc,
				const void *req, size_t len)
{
	struct tegra_ivc_channel *chan;
	int ret;

	chan = civc->chan;
	if (chan == NULL || !chan->is_ready) {
		if (chan != NULL)
			dev_warn(&chan->dev, "%s: dev is not ready!\n", __func__);
		return -EIO;
	}

	ret = mutex_lock_interruptible(&civc->ivc_wr_lock);
	if (unlikely(ret == -EINTR))
		return -ERESTARTSYS;
	if (unlikely(ret))
		return ret;

	ret = wait_event_interruptible(civc->write_q,
				tegra_ivc_can_write(&chan->ivc));
	if (likely(ret == 0))
		ret = tegra_ivc_write(&chan->ivc, NULL, req, len);

	mutex_unlock(&civc->ivc_wr_lock);

	if (unlikely(ret < 0))
		dev_err(&chan->dev, "tegra_ivc_write: error %d\n", ret);

	return ret;
}

/**
 * @brief Transmit a message over IVC channel with tracing
 *
 * This function wraps @ref tegra_capture_ivc_tx_() to provide tracing
 * functionality for IVC transmissions.
 * - Gets the channel name using @ref dev_name()
 * - If len is less than the header length, sets the header to zero and
 *   copies the message header using @ref memcpy()
 * - Otherwise, copies the header from the message using @ref memcpy()
 * - Calls @ref tegra_capture_ivc_tx_() to transmit the message
 * - Records the transmission via trace events for debugging and monitoring
 *   using @ref trace_capture_ivc_send() or @ref trace_capture_ivc_send_error()
 *   based on the return value of @ref tegra_capture_ivc_tx_()
 * @param[in] civc  Pointer to the capture IVC context
 *                  Valid value: non-NULL
 * @param[in] req   Pointer to the message to be transmitted
 *                  Valid value: non-NULL
 * @param[in] len   Length of the message in bytes
 *                  Valid range: > 0
 *
 * @retval (int)    Value returned by @ref tegra_capture_ivc_tx_()
 */
static int tegra_capture_ivc_tx(struct tegra_capture_ivc *civc,
				const void *req, size_t len)
{
	int ret;
	struct tegra_capture_ivc_msg_header hdr;
	size_t hdrlen = sizeof(hdr);
	char const *ch_name = "NULL";

	if (civc->chan)
		ch_name = dev_name(&civc->chan->dev);

	if (len < hdrlen) {
		memset(&hdr, 0, hdrlen);
		memcpy(&hdr, req, len);
	} else {
		memcpy(&hdr, req, hdrlen);
	}

	ret = tegra_capture_ivc_tx_(civc, req, len);

	if (ret < 0)
		trace_capture_ivc_send_error(ch_name, hdr.msg_id, hdr.channel_id, ret);
	else
		trace_capture_ivc_send(ch_name, hdr.msg_id, hdr.channel_id);

	return ret;
}

/**
 * @brief Submit a control message over the capture-control IVC channel
 *
 * This function submits a control message to the capture-control IVC channel.
 * - Verifies that the capture-control IVC context is initialized
 * - Calls @ref tegra_capture_ivc_tx() to transmit the control message
 *
 * @param[in] control_desc  Pointer to the control message descriptor
 *                          Valid value: non-NULL
 * @param[in] len           Length of the control message in bytes
 *                          Valid range: > 0
 *
 * @retval (int)    Value returned by @ref tegra_capture_ivc_tx()
 * @retval -ENODEV  If the control IVC context is not initialized
 */
int tegra_capture_ivc_control_submit(const void *control_desc, size_t len)
{
	if (__scivc_control == NULL) {
		pr_warn("%s: __scivc_control is NULL!\n", __func__);
		return -ENODEV;
	}

	return tegra_capture_ivc_tx(__scivc_control, control_desc, len);
}
EXPORT_SYMBOL(tegra_capture_ivc_control_submit);

/**
 * @brief Submit a capture message over the capture IVC channel
 *
 * This function submits a capture message to the capture IVC channel.
 * - Verifies that the capture IVC context is initialized
 * - Calls @ref tegra_capture_ivc_tx() to transmit the capture message
 *
 * @param[in] capture_desc  Pointer to the capture message descriptor
 *                          Valid value: non-NULL
 * @param[in] len           Length of the capture message in bytes
 *                          Valid range: > 0
 *
 * @retval (int)    Value returned by @ref tegra_capture_ivc_tx()
 * @retval -ENODEV  If the capture IVC context is not initialized
 */
int tegra_capture_ivc_capture_submit(const void *capture_desc, size_t len)
{
	if (__scivc_capture == NULL) {
		pr_warn("%s: __scivc_capture is NULL!\n", __func__);
		return -ENODEV;
	}

	return tegra_capture_ivc_tx(__scivc_capture, capture_desc, len);
}
EXPORT_SYMBOL(tegra_capture_ivc_capture_submit);

/**
 * @brief Register a callback function for capture-control IVC responses
 *
 * This function registers a callback function that will be invoked when a
 * response is received on the capture-control IVC channel.
 * - Validates input parameters
 * - Gets a runtime reference to the IVC channel using @ref tegra_ivc_channel_runtime_get()
 * - Acquires the callback context list lock using @ref spin_lock()
 * - Checks if the list is empty using @ref list_empty()
 * - If the list is empty, releases the lock using @ref spin_unlock().
 * - Otherwise, acquires an available callback context from the list using @ref list_first_entry()
 * - Removes the callback context from the list using @ref list_del()
 * - Releases the callback context list lock using @ref spin_unlock()
 * - Acquires the callback context lock using @ref mutex_lock()
 * - Checks if the callback function is already registered using @ref cb_ctx->cb_func
 * - If the callback function is already registered, releases the callback context lock using
 *   @ref mutex_unlock()
 * - Otherwise, associates the callback function with the context using @ref cb_ctx->cb_func
 * - Releases the callback context lock using @ref mutex_unlock()
 * - Adds back the channel using @ref tegra_ivc_channel_runtime_put()
 *
 * @param[in] control_resp_cb  Callback function to be invoked for responses
 *                             Valid value: non-NULL
 * @param[out] trans_id        Pointer to store the transaction ID
 *                             Valid value: non-NULL
 * @param[in] priv_context     Private context to be passed to the callback
 *                             Valid value: any value
 *
 * @retval 0         On successful registration
 * @retval -EINVAL   If input validation fails
 * @retval -ENODEV   If control IVC context is not initialized
 * @retval -EAGAIN   If no callback contexts are available
 * @retval -EIO      If an internal error occurs during registration
 */
int tegra_capture_ivc_register_control_cb(
		tegra_capture_ivc_cb_func control_resp_cb,
		uint32_t *trans_id, const void *priv_context)
{
	struct tegra_capture_ivc *civc;
	struct tegra_capture_ivc_cb_ctx *cb_ctx;
	size_t ctx_id;
	int ret;

	/* Check if inputs are valid */
	if (control_resp_cb == NULL) {
		pr_warn("%s: callback function is NULL!\n", __func__);
		return -EINVAL;
	}

	if (trans_id == NULL) {
		pr_warn("%s: return value trans_id is NULL!\n", __func__);
		return -EINVAL;
	}

	if (!__scivc_control) {
		pr_warn("%s: invalid __scivc_control\n", __func__);
		return -ENODEV;
	}

	civc = __scivc_control;

	ret = tegra_ivc_channel_runtime_get(civc->chan);
	if (unlikely(ret < 0))
		return ret;

	spin_lock(&civc->avl_ctx_list_lock);
	if (unlikely(list_empty(&civc->avl_ctx_list))) {
		spin_unlock(&civc->avl_ctx_list_lock);
		ret = -EAGAIN;
		goto fail;
	}


	cb_ctx = list_first_entry(&civc->avl_ctx_list,
			struct tegra_capture_ivc_cb_ctx, node);

	list_del(&cb_ctx->node);
	spin_unlock(&civc->avl_ctx_list_lock);

	ctx_id = cb_ctx - &civc->cb_ctx[0];

	if (ctx_id < TRANS_ID_START_IDX ||
			ctx_id >= ARRAY_SIZE(civc->cb_ctx)) {
		ret = -EIO;
		goto fail;
	}

	mutex_lock(&civc->cb_ctx_lock);

	if (cb_ctx->cb_func != NULL) {
		pr_warn("%s: cb_ctx is not NULL\n", __func__);
		ret = -EIO;
		goto locked_fail;
	}

	*trans_id = (uint32_t)ctx_id;
	cb_ctx->cb_func = control_resp_cb;
	cb_ctx->priv_context = priv_context;

	mutex_unlock(&civc->cb_ctx_lock);

	return 0;

locked_fail:
	mutex_unlock(&civc->cb_ctx_lock);
fail:
	tegra_ivc_channel_runtime_put(civc->chan);
	return ret;
}
EXPORT_SYMBOL(tegra_capture_ivc_register_control_cb);

/**
 * @brief Update callback context from transaction ID to channel ID
 *
 * This function updates the callback context from a temporary transaction ID to
 * a permanent channel ID. This is typically called after the RTCPU has assigned
 * a channel ID in response to a channel setup request.
 * - Validates input parameters
 * - Gets the channel ID and transaction ID using @ref array_index_nospec()
 * - Gets the capture IVC context using @ref __scivc_control
 * - Locks the callback context lock using @ref mutex_lock()
 * - Validates the transaction context using @ref civc->cb_ctx[trans_id].cb_func
 * - Validates the channel context using @ref civc->cb_ctx[chan_id].cb_func
 * - Moves the callback function from the transaction context to the channel context
 * - Adds the transaction context back to the available list
 * - Releases the callback context lock using @ref mutex_unlock()
 * - Locks the callback context list lock using @ref spin_lock()
 * - Adds the transaction context back to the available list using @ref list_add_tail()
 * - Releases the callback context list lock using @ref spin_unlock()
 *
 * @param[in] chan_id   Channel ID assigned by RTCPU
 *                      Valid range: [0, @ref NUM_CAPTURE_CHANNELS-1]
 * @param[in] trans_id  Transaction ID previously returned by
 *                      @ref tegra_capture_ivc_register_control_cb()
 *                      Valid range: [@ref TRANS_ID_START_IDX, @ref TOTAL_CHANNELS-1]
 *
 * @retval 0         On successful notification
 * @retval -EINVAL   If input validation fails
 * @retval -ENODEV   If control IVC context is not initialized
 * @retval -EBADF    If transaction context is idle
 * @retval -EBUSY    If channel context is busy
 */
int tegra_capture_ivc_notify_chan_id(uint32_t chan_id, uint32_t trans_id)
{
	struct tegra_capture_ivc *civc;

	if (chan_id >= NUM_CAPTURE_CHANNELS) {
		pr_warn("%s: invalid chan_id\n", __func__);
		return -EINVAL;
	}

	if (trans_id < TRANS_ID_START_IDX ||
			trans_id >= TOTAL_CHANNELS) {
		pr_warn("%s: invalid trans_id\n", __func__);
		return -EINVAL;
	}

	if (!__scivc_control) {
		pr_warn("%s: invalid __scivc_control\n", __func__);
		return -ENODEV;
	}

	chan_id  = array_index_nospec(chan_id,  NUM_CAPTURE_CHANNELS);
	trans_id = array_index_nospec(trans_id, TOTAL_CHANNELS);

	civc = __scivc_control;

	mutex_lock(&civc->cb_ctx_lock);

	if (civc->cb_ctx[trans_id].cb_func == NULL) {
		pr_warn("%s: transaction context at %u is idle\n", __func__, trans_id);
		mutex_unlock(&civc->cb_ctx_lock);
		return -EBADF;
	}

	if (civc->cb_ctx[chan_id].cb_func != NULL) {
		pr_warn("%s: channel context at %u is busy\n", __func__, chan_id);
		mutex_unlock(&civc->cb_ctx_lock);
		return -EBUSY;
	}

	/* Update cb_ctx index */
	civc->cb_ctx[chan_id].cb_func = civc->cb_ctx[trans_id].cb_func;
	civc->cb_ctx[chan_id].priv_context =
			civc->cb_ctx[trans_id].priv_context;

	/* Reset trans_id cb_ctx fields */
	civc->cb_ctx[trans_id].cb_func = NULL;
	civc->cb_ctx[trans_id].priv_context = NULL;

	mutex_unlock(&civc->cb_ctx_lock);

	spin_lock(&civc->avl_ctx_list_lock);
	list_add_tail(&civc->cb_ctx[trans_id].node, &civc->avl_ctx_list);
	spin_unlock(&civc->avl_ctx_list_lock);

	return 0;
}
EXPORT_SYMBOL(tegra_capture_ivc_notify_chan_id);

/**
 * @brief Register a callback function for capture IVC status indications
 *
 * This function registers a callback function that will be invoked when a
 * status indication is received on the capture IVC channel for a specific
 * channel ID.
 * - Validates input parameters
 * - Gets a runtime reference to the IVC channel using @ref tegra_ivc_channel_runtime_get()
 * - Gets lock to the callback context lock using @ref mutex_lock()
 * - Checks if the channel ID already has a registered callback using @ref cb_ctx->cb_func
 * - If the channel ID already has a registered callback, releases the callback context lock using
 *   @ref mutex_unlock()
 * - Otherwise, associates the callback function with the channel ID using @ref cb_ctx->cb_func
 * - Releases the callback context lock using @ref mutex_unlock()
 * - Releases the runtime reference to the IVC channel using @ref tegra_ivc_channel_runtime_put()
 *
 * @param[in] capture_status_ind_cb  Callback function to be invoked for status indications
 *                                  Valid value: non-NULL
 * @param[in] chan_id               Channel ID for which to register the callback
 *                                  Valid range: [0, @ref NUM_CAPTURE_CHANNELS-1]
 * @param[in] priv_context          Private context to be passed to the callback
 *                                  Valid value: any value
 *
 * @retval 0         On successful registration
 * @retval -EINVAL   If input validation fails
 * @retval -ENODEV   If capture IVC context is not initialized
 * @retval -EBUSY    If channel ID already has a registered callback
 */
int tegra_capture_ivc_register_capture_cb(
		tegra_capture_ivc_cb_func capture_status_ind_cb,
		uint32_t chan_id, const void *priv_context)
{
	struct tegra_capture_ivc *civc;
	int ret;

	if (capture_status_ind_cb == NULL) {
		pr_warn("%s: callback function is NULL\n", __func__);
		return -EINVAL;
	}

	if (chan_id >= NUM_CAPTURE_CHANNELS) {
		pr_warn("%s: invalid channel id %u\n", __func__, chan_id);
		return -EINVAL;
	}
	chan_id = array_index_nospec(chan_id, NUM_CAPTURE_CHANNELS);

	if (!__scivc_capture)
		return -ENODEV;

	civc = __scivc_capture;

	ret = tegra_ivc_channel_runtime_get(civc->chan);
	if (ret < 0)
		return ret;

	mutex_lock(&civc->cb_ctx_lock);

	if (civc->cb_ctx[chan_id].cb_func != NULL) {
		pr_warn("%s: capture channel %u is busy\n", __func__, chan_id);
		ret = -EBUSY;
		goto fail;
	}

	civc->cb_ctx[chan_id].cb_func = capture_status_ind_cb;
	civc->cb_ctx[chan_id].priv_context = priv_context;
	mutex_unlock(&civc->cb_ctx_lock);

	return 0;
fail:
	mutex_unlock(&civc->cb_ctx_lock);
	tegra_ivc_channel_runtime_put(civc->chan);

	return ret;
}
EXPORT_SYMBOL(tegra_capture_ivc_register_capture_cb);

/**
 * @brief Unregister a control callback function for a specified ID
 *
 * This function unregisters a previously registered callback function from
 * either a transaction ID or a channel ID.
 * - Validates the input ID
 * - Gets the capture IVC context using @ref __scivc_control
 * - Gets the id using @ref array_index_nospec()
 * - Locks the callback context lock using @ref mutex_lock()
 * - Clears the callback function and context from the specified ID using @ref cb_ctx->cb_func
 * - Acquires the callback context list lock using @ref spin_lock()
 * - If the ID is a transaction ID, adds it back to the available list using @ref list_add_tail()
 * - Releases the callback context list lock using @ref spin_unlock()
 * - Releases the callback context lock using @ref mutex_unlock()
 * - Releases the runtime reference to the IVC channel using @ref tegra_ivc_channel_runtime_put()
 *
 * @param[in] id  Transaction ID or channel ID to unregister
 *                Valid range: [0, @ref TOTAL_CHANNELS-1]
 *
 * @retval 0         On successful unregistration
 * @retval -EINVAL   If ID validation fails
 * @retval -ENODEV   If control IVC context is not initialized
 * @retval -EBADF    If the specified ID has no registered callback
 */
int tegra_capture_ivc_unregister_control_cb(uint32_t id)
{
	struct tegra_capture_ivc *civc;

	/* id could be temporary trans_id or rtcpu-allocated chan_id */
	if (id >= TOTAL_CHANNELS) {
		pr_warn("%s: invalid id %u\n", __func__, id);
		return -EINVAL;
	}
	if (!__scivc_control) {
		pr_warn("%s: __scivc_control is not exist!\n", __func__);
		return -ENODEV;
	}

	id = array_index_nospec(id, TOTAL_CHANNELS);

	civc = __scivc_control;

	mutex_lock(&civc->cb_ctx_lock);

	if (civc->cb_ctx[id].cb_func == NULL) {
		pr_warn("%s: control channel %u is idle\n", __func__, id);
		mutex_unlock(&civc->cb_ctx_lock);
		return -EBADF;
	}

	civc->cb_ctx[id].cb_func = NULL;
	civc->cb_ctx[id].priv_context = NULL;

	mutex_unlock(&civc->cb_ctx_lock);

	/*
	 * If it's trans_id, client encountered an error before or during
	 * chan_id update, in that case the corresponding cb_ctx
	 * needs to be added back in the avilable cb_ctx list.
	 */
	if (id >= TRANS_ID_START_IDX) {
		spin_lock(&civc->avl_ctx_list_lock);
		list_add_tail(&civc->cb_ctx[id].node, &civc->avl_ctx_list);
		spin_unlock(&civc->avl_ctx_list_lock);
	}

	tegra_ivc_channel_runtime_put(civc->chan);

	return 0;
}
EXPORT_SYMBOL(tegra_capture_ivc_unregister_control_cb);

/**
 * @brief Unregister a capture callback function for a specified channel ID
 *
 * This function unregisters a previously registered capture callback function.
 * - Validates the channel ID
 * - Gets the capture IVC context using @ref __scivc_capture
 * - Locks the callback context lock using @ref mutex_lock()
 * - Clears the callback function and context from the specified channel ID using
 *   @ref cb_ctx->cb_func
 * - Releases the callback context lock using @ref mutex_unlock()
 * - Releases the runtime reference to the IVC channel using @ref tegra_ivc_channel_runtime_put()
 *
 * @param[in] chan_id  Channel ID to unregister
 *                     Valid range: [0, @ref NUM_CAPTURE_CHANNELS-1]
 *
 * @retval 0         On successful unregistration
 * @retval -EINVAL   If channel ID validation fails
 * @retval -ENODEV   If capture IVC context is not initialized
 * @retval -EBADF    If the specified channel ID has no registered callback
 */
int tegra_capture_ivc_unregister_capture_cb(uint32_t chan_id)
{
	struct tegra_capture_ivc *civc;

	if (chan_id >= NUM_CAPTURE_CHANNELS)
		return -EINVAL;

	if (!__scivc_capture)
		return -ENODEV;

	chan_id = array_index_nospec(chan_id, NUM_CAPTURE_CHANNELS);

	civc = __scivc_capture;

	mutex_lock(&civc->cb_ctx_lock);

	if (civc->cb_ctx[chan_id].cb_func == NULL) {
		pr_warn("%s: capture channel %u is idle\n", __func__, chan_id);
		mutex_unlock(&civc->cb_ctx_lock);
		return -EBADF;
	}

	civc->cb_ctx[chan_id].cb_func = NULL;
	civc->cb_ctx[chan_id].priv_context = NULL;

	mutex_unlock(&civc->cb_ctx_lock);

	tegra_ivc_channel_runtime_put(civc->chan);

	return 0;
}
EXPORT_SYMBOL(tegra_capture_ivc_unregister_capture_cb);

/**
 * @brief Process an IVC message by invoking the appropriate callback
 *
 * This inline function processes an incoming IVC message by invoking the registered
 * callback function for the specified channel ID.
 * - Checks if a callback function is registered for the channel ID using
 *   @ref civc->cb_ctx[id].cb_func
 * - If registered, invokes the callback with the message and private context using
 *   @ref civc->cb_ctx[id].cb_func()
 *
 * @param[in] civc  Pointer to the capture IVC context
 *                  Valid value: non-NULL
 * @param[in] id    Channel ID associated with the message
 *                  Valid range: [0, @ref TOTAL_CHANNELS-1]
 * @param[in] msg   Pointer to the received message
 *                  Valid value: non-NULL
 */
static inline void tegra_capture_ivc_recv_msg(
	struct tegra_capture_ivc *civc,
	uint32_t id,
	const void *msg)
{
	struct device *dev = &civc->chan->dev;

	/* Check if callback function available */
	if (unlikely(!civc->cb_ctx[id].cb_func)) {
		dev_dbg(dev, "No callback for id %u\n", id);
	} else {
		/* Invoke client callback. */
		civc->cb_ctx[id].cb_func(msg, civc->cb_ctx[id].priv_context);
	}
}

/**
 * @brief Process all pending received IVC messages
 *
 * This inline function processes all pending messages in the IVC receive queue.
 * - Loops while messages are available for reading using @ref tegra_ivc_can_read()
 * - Retrieves the next message from the queue using @ref tegra_ivc_read_get_next_frame()
 * - Extracts the channel ID from the message header using @ref hdr->channel_id
 * - Records the message reception via trace events using @ref trace_capture_ivc_recv()
 * - Gets the id using @ref array_index_nospec()
 * - Validates the channel ID and dispatches the message to the appropriate callback using
 *   @ref tegra_capture_ivc_recv_msg()
 * - Advances the IVC read queue to the next message using @ref tegra_ivc_read_advance()
 *
 * @param[in] civc  Pointer to the capture IVC context
 *                  Valid value: non-NULL
 */
static inline void tegra_capture_ivc_recv(struct tegra_capture_ivc *civc)
{
	struct tegra_ivc *ivc = &civc->chan->ivc;
	struct device *dev = &civc->chan->dev;
	const void *msg;
	const struct tegra_capture_ivc_msg_header *hdr;
	uint32_t id;

	while (tegra_ivc_can_read(ivc)) {
#if defined(NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP) /* Linux 6.2 */
		struct iosys_map map;
		int err;
		err = tegra_ivc_read_get_next_frame(ivc, &map);
		if (err) {
			dev_err(dev, "Failed to get next frame for read\n");
			return;
		}
		msg = map.vaddr;
#else
		msg = tegra_ivc_read_get_next_frame(ivc);
#endif
		hdr = msg;
		id = hdr->channel_id;

		trace_capture_ivc_recv(dev_name(dev), hdr->msg_id, id);

		/* Check if message is valid */
		if (id < TOTAL_CHANNELS) {
			id = array_index_nospec(id, TOTAL_CHANNELS);
			tegra_capture_ivc_recv_msg(civc, id, msg);
		} else {
			dev_warn(dev, "Invalid rtcpu channel id %u", id);
		}

		tegra_ivc_read_advance(ivc);
	}
}

/**
 * @brief Worker function to process IVC notifications
 *
 * This function is invoked when an IVC notification is received from the RTCPU.
 * - Retrieves the capture IVC context from the work structure using @ref container_of()
 * - Acquires a runtime PM reference to prevent suspended operation using
 *   @ref pm_runtime_get_if_in_use()
 * - If channel is not ready, logs a warning.
 * - Verifies that the channel is ready using @ref chan->is_ready
 * - Calls @ref tegra_capture_ivc_recv() to process all pending messages
 * - Releases the runtime PM reference using @ref pm_runtime_put()
 *
 * @param[in] work  Pointer to the kthread work structure
 *                  Valid value: non-NULL
 */
static void tegra_capture_ivc_worker(struct kthread_work *work)
{
	struct tegra_capture_ivc *civc;
	struct tegra_ivc_channel *chan;

	civc = container_of(work, struct tegra_capture_ivc, work);
	chan = civc->chan;

	/*
	 * Do not process IVC events if worker gets woken up while
	 * this channel is suspended.  There is a Christmas tree
	 * notify when RCE resumes and IVC bus gets set up.
	 */
	if (pm_runtime_get_if_in_use(&chan->dev) > 0) {
		if (!chan->is_ready) {
			dev_warn(&chan->dev, "channel not ready\n");
		}

		tegra_capture_ivc_recv(civc);

		pm_runtime_put(&chan->dev);
	} else {
		dev_dbg(&chan->dev, "extra wakeup");
	}
}

/**
 * @brief IVC notification callback from the IVC subsystem
 *
 * This function is called by the IVC subsystem when a notification is received
 * from the remote processor (RTCPU).
 * - Retrieves the capture IVC context from the channel driver data using
 *   @ref tegra_ivc_channel_get_drvdata()
 * - Records the notification via trace events using @ref trace_capture_ivc_notify()
 * - Wakes up any threads waiting to write to the IVC channel using @ref wake_up()
 * - Queues the worker to process any received messages using @ref kthread_queue_work()
 *
 * @param[in] chan  Pointer to the IVC channel that received the notification
 *                  Valid value: non-NULL
 */
static void tegra_capture_ivc_notify(struct tegra_ivc_channel *chan)
{
	struct tegra_capture_ivc *civc = tegra_ivc_channel_get_drvdata(chan);

	trace_capture_ivc_notify(dev_name(&chan->dev));

	/* Only 1 thread can wait on write_q, rest wait for write_lock */
	wake_up(&civc->write_q);
	kthread_queue_work(&civc->ivc_worker, &civc->work);
}

#define NV(x) "nvidia," #x

/**
 * @brief Probe function for the tegra capture IVC driver
 *
 * This function is called when a matching IVC channel is found.
 * - Allocates and initializes the capture IVC context using @ref devm_kzalloc()
 * - Reads the service type from device tree using @ref of_property_read_string()
 * - Stores the channel reference in the IVC context (civc->chan = chan)
 * - Initializes synchronization primitives using @ref mutex_init()
 * - Initializes the work structure for processing IVC notifications using @ref kthread_init_work()
 * - Initializes the worker thread using @ref kthread_init_worker()
 * - Creates a kernel worker thread using @ref kthread_create()
 * - Sets the worker thread to FIFO scheduling using @ref sched_set_fifo_low()
 * - Wakes up the worker thread using @ref wake_up_process()
 * - Initializes the IVC write queue using @ref init_waitqueue_head()
 * - Initializes the spinlock for available context list using @ref spin_lock_init()
 * - Initializes the transaction context list using @ref INIT_LIST_HEAD()
 * - Locks the callback context lock using @ref mutex_lock()
 * - Adds the transaction contexts to the available list using @ref list_add_tail()
 * - Unlocks the callback context lock using @ref mutex_unlock()
 * - Associates the context with the IVC channel using @ref tegra_ivc_channel_set_drvdata()
 * - Checks if the service type is "capture-control" using @ref strcmp()
 * - Verifies that no control channel already exists
 * - Registers the context as a control service by setting @ref __scivc_control
 * - If not control, checks if service type is "capture" using @ref strcmp()
 * - Verifies that no capture channel already exists
 * - Registers the context as a capture service by setting @ref __scivc_capture
 * - Returns error if service type is neither control nor capture
 * - Stops the worker thread using @ref kthread_stop() if an error occurs
 *
 * @param[in] chan  Pointer to the IVC channel to be probed
 *                  Valid value: non-NULL
 *
 * @retval 0         On successful probe
 * @retval -ENOMEM   If memory allocation fails
 * @retval -EEXIST   If a channel for the same service already exists
 * @retval -EINVAL   If the service type is invalid or missing
 * @retval (int)     Value returned by @ref of_property_read_string() or @ref kthread_create()
 */
static int tegra_capture_ivc_probe(struct tegra_ivc_channel *chan)
{
	struct device *dev = &chan->dev;
	struct tegra_capture_ivc *civc;
	const char *service;
	int ret;
	uint32_t i;

	civc = devm_kzalloc(dev, (sizeof(*civc)), GFP_KERNEL);
	if (unlikely(civc == NULL))
		return -ENOMEM;

	ret = of_property_read_string(dev->of_node, NV(service),
			&service);
	if (unlikely(ret)) {
		dev_err(dev, "missing <%s> property\n", NV(service));
		return ret;
	}

	civc->chan = chan;

	mutex_init(&civc->cb_ctx_lock);
	mutex_init(&civc->ivc_wr_lock);

	/* Initialize kworker */
	kthread_init_work(&civc->work, tegra_capture_ivc_worker);

	kthread_init_worker(&civc->ivc_worker);

	civc->ivc_kthread = kthread_create(&kthread_worker_fn,
			&civc->ivc_worker, service);
	if (IS_ERR(civc->ivc_kthread)) {
		dev_err(dev, "Cannot allocate ivc worker thread\n");
		ret = PTR_ERR(civc->ivc_kthread);
		goto err;
	}
	sched_set_fifo_low(civc->ivc_kthread);
	wake_up_process(civc->ivc_kthread);

	/* Initialize wait queue */
	init_waitqueue_head(&civc->write_q);

	/* transaction-id list of available callback contexts */
	spin_lock_init(&civc->avl_ctx_list_lock);
	INIT_LIST_HEAD(&civc->avl_ctx_list);

	/* Add the transaction cb-contexts to the available list */
	mutex_lock(&civc->cb_ctx_lock);
	for (i = TRANS_ID_START_IDX; i < ARRAY_SIZE(civc->cb_ctx); i++)
		list_add_tail(&civc->cb_ctx[i].node, &civc->avl_ctx_list);
	mutex_unlock(&civc->cb_ctx_lock);

	tegra_ivc_channel_set_drvdata(chan, civc);

	if (!strcmp("capture-control", service)) {
		if (__scivc_control != NULL) {
			dev_warn(&chan->dev, "%s: __scivc_control already exists!\n", __func__);
			ret = -EEXIST;
			goto err_service;
		}
		__scivc_control = civc;
	} else if (!strcmp("capture", service)) {
		if (__scivc_capture != NULL) {
			dev_warn(&chan->dev, "%s: __scivc_capture already exists!\n", __func__);
			ret = -EEXIST;
			goto err_service;
		}
		__scivc_capture = civc;
	} else {
		dev_err(dev, "Unknown ivc channel %s\n", service);
		ret = -EINVAL;
		goto err_service;
	}

	return 0;

err_service:
	kthread_stop(civc->ivc_kthread);
err:
	return ret;
}

/**
 * @brief Remove function for the tegra capture IVC driver
 *
 * This function is called when an IVC channel is being removed.
 * - Gets the capture IVC context from the channel driver data using
 *   @ref tegra_ivc_channel_get_drvdata()
 * - Ensures any pending work on the worker thread is complete using
 *   @ref kthread_flush_worker()
 * - Stops the worker thread using @ref kthread_stop()
 * - Unregisters the IVC context from the global context using
 *   @ref __scivc_control or @ref __scivc_capture
 *
 * @param[in] chan  Pointer to the IVC channel to be removed
 *                  Valid value: non-NULL
 */
static void tegra_capture_ivc_remove(struct tegra_ivc_channel *chan)
{
	struct tegra_capture_ivc *civc = tegra_ivc_channel_get_drvdata(chan);

	kthread_flush_worker(&civc->ivc_worker);
	kthread_stop(civc->ivc_kthread);

	if (__scivc_control == civc)
		__scivc_control = NULL;
	else if (__scivc_capture == civc)
		__scivc_capture = NULL;
	else
		dev_warn(&chan->dev, "Unknown ivc channel\n");
}

static struct of_device_id tegra_capture_ivc_channel_of_match[] = {
	{ .compatible = "nvidia,tegra186-camera-ivc-protocol-capture-control" },
	{ .compatible = "nvidia,tegra186-camera-ivc-protocol-capture" },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_capture_ivc_channel_of_match);

static const struct tegra_ivc_channel_ops tegra_capture_ivc_ops = {
	.probe	= tegra_capture_ivc_probe,
	.remove	= tegra_capture_ivc_remove,
	.notify	= tegra_capture_ivc_notify,
};

static struct tegra_ivc_driver tegra_capture_ivc_driver = {
	.driver = {
		.name	= "tegra-capture-ivc",
		.bus	= &tegra_ivc_bus_type,
		.owner	= THIS_MODULE,
		.of_match_table = tegra_capture_ivc_channel_of_match,
	},
	.dev_type	= &tegra_ivc_channel_type,
	.ops.channel	= &tegra_capture_ivc_ops,
};

tegra_ivc_subsys_driver_default(tegra_capture_ivc_driver);
MODULE_AUTHOR("Sudhir Vyas <svyas@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra Capture IVC driver");
MODULE_LICENSE("GPL v2");
