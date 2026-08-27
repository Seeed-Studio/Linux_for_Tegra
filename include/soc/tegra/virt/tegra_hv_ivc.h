/*
 * SPDX-License-Identifier: GPL-2.0-only
 * SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef TEGRA_HV_IVC_H
#define TEGRA_HV_IVC_H

#include <linux/of.h>
#include <linux/version.h>

/**
 * @defgroup hypervisor_ivc_framework Hypervisor IVC Framework
 * @{
 */

/** @brief structure representing ivc queue cookie in hypervisor driver */
struct tegra_hv_ivc_cookie {
	/** @brief irq linked to the ivc queue */
	int irq;
	/** @brief vmid of the peer */
	int peer_vmid;
	/** @brief number of frames in an ivc queue */
	int nframes;
	/** @brief ivc frame size*/
	int frame_size;
	/** @brief address used to notify end-point */
	uint32_t *notify_va;
};

/** @brief represents the ivc queue operations */
struct tegra_hv_ivc_ops {
	/** @brief called when data are received */
	void (*rx_rdy)(struct tegra_hv_ivc_cookie *ivck);
	/** @brief called when space is available to write data */
	void (*tx_rdy)(struct tegra_hv_ivc_cookie *ivck);
};

/**
 * @brief          Reserve an IVC queue for use
 * @param[in]      dn Device node pointer to the queue in the DT
 *		   If NULL, then operate on first HV device
 * @param[in]      id Id number of the queue to use.
 * @param[in]      ops Ops structure or NULL (deprecated)
 *
 * @retval         ptr IVC queue cookie pointer or else errors.
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *
 * @post
 *                 - After reserving IVC queue client can perform I/O operation on the IVC queue.
 *
 * @usage
 *                 - Reserve ivc queue before performing I/O operations.
 *                 - Tegra HV driver should have been probed successfully.
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
struct tegra_hv_ivc_cookie *tegra_hv_ivc_reserve(
		struct device_node *dn, int id,
		const struct tegra_hv_ivc_ops *ops);

/**
 * @brief          Unreserve an IVC queue used
 * @param[in]      ivck IVC cookie of the queue
 *
 * @retval         0 On success and an error code otherwise
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *
 * @post
 *                 - After unreserving IVC queue client can not perform I/O operation on the IVC queue.
 *
 * @usage
 *                 - Unreserve ivc queue after performing I/O operations.
 *                 - Tegra HV driver should have been probed successfully.
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
int tegra_hv_ivc_unreserve(struct tegra_hv_ivc_cookie *ivck);

/**
 * @brief          Write a number of bytes (as a single frame) from the queue.
 * @param[in]      ivck IVC cookie of the queue
 * @param[in]      buf Pointer to the data to write
 * @param[in]      size Size of the data to write
 *
 * @retval         size On success and an error code otherwise
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *                 - IVC should have been reserved before performing any ivc operations.
 *
 * @post
 *                 - Data will be written on the ivc queue successfully.
 *
 * @usage
 *                 - Use this API to send data to peer end.
 *                 - IVC should have been reserved before using this API.
 *                 - Never use this API after unreserving ivc queue.
 *                 - Tegra HV driver should have been probed successfully.
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
int tegra_hv_ivc_write(struct tegra_hv_ivc_cookie *ivck, const void *buf,
		int size);

/**
 * @brief          Write a number of bytes (as a single frame) from the queue.
 * @param[in]      ivck IVC cookie of the queue
 * @param[in]      buf Pointer to the userspace data to write
 * @param[in]      size Size of the data to write
 *
 * @retval         size Returns size on success and an error code otherwise
 *
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *                 - IVC should have been reserved before performing any ivc operations.
 *
 * @post
 *                 - Data will be written on the ivc queue successfully from userspace buffer.
 *
 * @usage
 *                 - Use this API to send data to peer end.
 *                 - IVC should have been reserved before using this API.
 *                 - Never use this API after unreserving ivc queue.
 *                 - Tegra HV driver should have been probed successfully.
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
int tegra_hv_ivc_write_user(struct tegra_hv_ivc_cookie *ivck, const void __user *buf,
		int size);

/**
 * @brief          Reads a number of bytes (as a single frame) from the queue.
 * @param[in]      ivck IVC cookie of the queue
 * @param[in,out]  buf Pointer to the data to read
 * @param[in]      size Max size of the data to read
 *
 * @retval         size Returns size on success and an error code otherwise
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *                 - IVC should have been reserved before performing any ivc operations.
 *
 * @post
 *                 - Data will be read from the ivc queue successfully.
 *
 * @usage
 *                 - Use this API to receive data from peer end.
 *                 - IVC should have been reserved before using this API.
 *                 - Never use this API after unreserving ivc queue.
 *                 - Tegra HV driver should have been probed successfully.
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
int tegra_hv_ivc_read(struct tegra_hv_ivc_cookie *ivck, void *buf, int size);

/**
 * @brief          Reads a number of bytes (as a single frame) from the queue.
 * @param[in]      ivck IVC cookie of the queue
 * @param[in,out]  buf Pointer to the userspace data to read
 * @param[in]      size Max size of the data to read
 *
 * @retval         size Returns size on success and an error code otherwise
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *                 - IVC should have been reserved before performing any ivc operations.
 *
 * @post
 *                 - Data will be read from the ivc queue successfully into userspace buffer.
 *
 * @usage
 *                 - Use this API to receive data from peer end.
 *                 - IVC should have been reserved before using this API.
 *                 - Never use this API after unreserving ivc queue.
 *                 - Tegra HV driver should have been probed successfully.
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
int tegra_hv_ivc_read_user(struct tegra_hv_ivc_cookie *ivck, void __user *buf, int size);

/**
 * @brief          Check whether data is available to read.
 * @param[in]      ivck IVC cookie of the queue
 *
 * @retval         1 If data are available in the rx queue, 0 if not
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *                 - IVC should have been reserved before performing any ivc operations.
 *
 * @post
 *                 - Client take decision to read data if data is present
 *
 * @usage
 *                 - Use this API if client want to check if response has been received peer end.
 *                 - IVC should have been reserved before using this API.
 *                 - Never use this API after unreserving ivc queue.
 *                 - Tegra HV driver should have been probed successfully.
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
int tegra_hv_ivc_can_read(struct tegra_hv_ivc_cookie *ivck);

/**
 * @brief          Check whether data can be written
 * @param[in]      ivck IVC cookie of the queue
 *
 * @retval         1 If data are can be written to the tx queue, 0 if not
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *                 - IVC should have been reserved before performing any ivc operations.
 *
 * @post
 *                 - Client take decision to write data if write slot is free.
 *
 * @usage
 *                 - Use this API if client want to check if write slot is present or not.
 *                 - IVC should have been reserved before using this API.
 *                 - Never use this API after unreserving ivc queue.
 *                 - Tegra HV driver should have been probed successfully.
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
int tegra_hv_ivc_can_write(struct tegra_hv_ivc_cookie *ivck);

/**
 * @brief          Test whether the tx queue is empty
 * @param[in]      ivck IVC cookie of the queue
 *
 * @retval         1 If the queue is empty, zero otherwise
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *                 - IVC should have been reserved before performing any ivc operations.
 *
 * @post
 *                 - Client will know whether tx queue is empty or not.
 *
 * @usage
 *                 - Use this API if tx queue is empty or not.
 *                 - IVC should have been reserved before using this API.
 *                 - Never use this API after unreserving ivc queue.
 *                 - Tegra HV driver should have been probed successfully.
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
int tegra_hv_ivc_tx_empty(struct tegra_hv_ivc_cookie *ivck);

#ifndef CONFIG_KERNEL_BUILD_WITH_PROD_DEFCONFIG
/*
 * @brief          Gets number of free entries in tx queue
 *                 Returns the number of unused entries in the tx queue. Assuming the caller
 *                 does not write any additional frames, this number may increase from the
 *                 value returned as the receiver consumes frames.
 * @param[in]      ivck IVC cookie of the queue
 *
 * @retval         num Number ot frames available.
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *                 - IVC should have been reserved before performing any ivc operations.
 * @usage
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
uint32_t tegra_hv_ivc_tx_frames_available(struct tegra_hv_ivc_cookie *ivck);

/*
 * @brief          Dump ivc info in dmesg logs
 * @param[in]      ivck IVC cookie of the queue
 *
 * @retval         0 On success, a negative error code otherwise
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *                 - IVC should have been reserved before performing any ivc operations.
 * @usage
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
int tegra_hv_ivc_dump(struct tegra_hv_ivc_cookie *ivck);
#endif

/**
 * @brief          Peek at the next frame to receive.
 *                 Peek at the next frame to be received, without removing it from the queue.
 *
 * @param[in]      ivck IVC cookie of the queue
 *
 * @retval         ptr Returns a pointer to the frame, or an error encoded pointer.
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *                 - IVC should have been reserved before performing any ivc operations.
 *
 * @post
 *                 - After peeking next frame we can check the content of frames.
 *
 * @usage
 *                 - If client want to check next frame without removing it from queue.
 *                 - IVC should have been reserved before using this API.
 *                 - Never use this API after unreserving ivc queue.
 *                 - Tegra HV driver should have been probed successfully.
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
void *tegra_hv_ivc_read_get_next_frame(struct tegra_hv_ivc_cookie *ivck);

/**
 * @brief          Advance the read queue
 * @param[in]      ivck IVC cookie of the queue
 *
 * @retval         0, Or a negative error value if failed.
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *                 - IVC should have been reserved before performing any ivc operations.
 *
 * @post
 *                 - Internal ivc counters point to next read frame.
 *
 * @usage
 *                 - Use this API if client wants to advance to next read frame
 *                 - IVC should have been reserved before using this API.
 *                 - Never use this API after unreserving ivc queue.
 *                 - Tegra HV driver should have been probed successfully.
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
int tegra_hv_ivc_read_advance(struct tegra_hv_ivc_cookie *ivck);

/**
 * @brief          Poke at the next frame to transmit
 *                 Get access to the next frame.
 * @param[in]      ivck IVC cookie of the queue
 *
 * @retval         ptr Returns a pointer to the frame, or an error encoded pointer.
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *                 - IVC should have been reserved before performing any ivc operations.
 *
 * @post
 *                 - Client can write data to frame after getting it without advacing tx frame counter.
 *
 * @usage
 *                 - If client want to send data to peer end without removing it from queue.
 *                 - IVC should have been reserved before using this API.
 *                 - Never use this API after unreserving ivc queue.
 *                 - Tegra HV driver should have been probed successfully.
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
void *tegra_hv_ivc_write_get_next_frame(struct tegra_hv_ivc_cookie *ivck);

/**
 * @brief          Advance the write queue
 * @param[in]      ivck IVC cookie of the queue
 *
 * @retval         0 On success or a negative error value if failed.
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *                 - IVC should have been reserved before performing any ivc operations.
 *
 * @post
 *                 - Internal ivc counters point to next write frame.
 *
 * @usage
 *                 - Use this API if client wants to advance to next write frame
 *                 - IVC should have been reserved before using this API.
 *                 - Never use this API after unreserving ivc queue.
 *                 - Tegra HV driver should have been probed successfully.
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
int tegra_hv_ivc_write_advance(struct tegra_hv_ivc_cookie *ivck);

/**
 * @brief          Handle internal messages.
 *                 This function must be called following every notification (interrupt or
 *                 callback invocation) for the tegra_hv_- version).
 * @param[in]      ivck IVC cookie of the queue
 *
 * @retval         0 If the channel is ready for communication, or -EAGAIN if a channel
 *                 reset is in progress.
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *                 - IVC should have been reserved before performing any ivc operations.
 *
 * @post
 *                 - IVC channel will be establised with peer end and will be ready for communication
 *
 * @usage
 *                 - Use this APi to establish connection with peer end for communication
 *                 - IVC should have been reserved before using this API.
 *                 - Never use this API after unreserving ivc queue.
 *                 - Tegra HV driver should have been probed successfully.
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
int tegra_hv_ivc_channel_notified(struct tegra_hv_ivc_cookie *ivck);

/**
 * @brief          Initiates a reset of the shared memory state.
 *                 This function must be called after a channel is reserved before it is used
 *                 for communication. The channel will be ready for use when a subsequent call
 *                 to ivc_channel_notified() returns 0.
 * @param[in]      ivck IVC cookie of the queue
 *
 * @retval         None
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *                 - IVC should have been reserved before performing any ivc operations.
 *
 * @post
 *                 - Channel will be in reseted state.
 *
 * @usage
 *                 - Before establishing connection reset ivc channel first after reserving ivc queue.
 *                 - IVC should have been reserved before using this API.
 *                 - Never use this API after unreserving ivc queue.
 *                 - Tegra HV driver should have been probed successfully.
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
void tegra_hv_ivc_channel_reset(struct tegra_hv_ivc_cookie *ivck);

/**
 * @brief          Get info (IPA and size) of Guest shared area
 * @param[in]      ivck IVC cookie of the queue
 * @param[out]     pa IPA of shared area
 * @param[out]     size Size of the shared area
 *
 * @retval         0 On success & -EINVAL on failure.
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *                 - IVC should have been reserved before performing any ivc operations.
 *
 * @post
 *                 - IPA address and size of ivc queue will be available to client.
 *
 * @usage
 *                 - When client want to mmap ivc memory to userspace via mmap API.
 *                 - Tegra HV driver should have been probed successfully.
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
int tegra_hv_ivc_get_info(struct tegra_hv_ivc_cookie *ivck, uint64_t *pa,
			  uint64_t *size);

/**
 * @brief          Notify remote guest
 * @param[in]      ivck IVC cookie of the queue
 *
 * @retval         None
 *
 * @pre
 *                 - Tegra hypervisor driver should have been initialized.
 *                 - This API should be invoked on virtual/hypervisor environment only.
 *                 - IVC should have been reserved before performing any ivc operations.
 *
 * @post
 *                 - Remote guest will be notified
 *
 * @usage
 *                 - Use when want to notify the peer end after writing or reading data to ivc queue
 *                 - IVC should have been reserved before using this API.
 *                 - Never use this API after unreserving ivc queue.
 *                 - Tegra HV driver should have been probed successfully.
 *                 - Allowed context for the API call
 *                   - Interrupt handler: Yes
 *                   - Signal handler: N/A
 *                   - Thread-safe: No
 *                   - Async/Sync: Sync
 *                   - Re-entrant: No
 *                 - API Group
 *                   - Init: No
 *                   - Runtime: Yes
 *                   - De-Init: No
 */
void tegra_hv_ivc_notify(struct tegra_hv_ivc_cookie *ivck);

/** @} */

#endif /* TEGRA_HV_IVC_H */
