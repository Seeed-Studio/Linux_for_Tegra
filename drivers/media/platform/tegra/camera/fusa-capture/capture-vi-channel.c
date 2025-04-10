// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2017-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

/**
 * @file drivers/media/platform/tegra/camera/fusa-capture/capture-vi-channel.c
 *
 * @brief VI channel character device driver for the T234 Camera RTCPU
 * platform.
 */

#include <nvidia/conftest.h>

#include <asm/ioctls.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/of_platform.h>
#include <linux/nvhost.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <media/fusa-capture/capture-vi.h>
#include <media/fusa-capture/capture-vi-channel.h>
#include <linux/arm64-barrier.h>

/**
 * @defgroup VI_CHANNEL_IOCTLS
 *
 * @brief VI channel character device IOCTL API
 *
 * Clients in the UMD may open sysfs character devices representing VI channels,
 * and perform configuration and enqueue buffers in capture requests to the
 * low-level RCE subsystem via these IOCTLs.
 *
 * @{
 */

/**
 * @brief Set up ISP channel resources and request FW channel allocation in RCE.
 *
 * Initialize the VI channel context and synchronization primitives, pin memory
 * for the capture descriptor queue, set up the buffer management table,
 * initialize the capture/capture-control IVC channels and request VI FW channel
 * allocation in RCE.
 *
 * @param[in]	ptr	Pointer to a struct @ref vi_capture_setup
 *
 * @returns	0 (success), neg. errno (failure)
 */
#define VI_CAPTURE_SETUP \
	_IOW('I', 1, struct vi_capture_setup)

/**
 * @brief Release the VI FW channel allocation in RCE, and all resources and
 * contexts in the KMD.
 *
 * @param[in]	reset_flags	uint32_t bitmask of
 *				@ref CAPTURE_CHANNEL_RESET_FLAGS

 * @returns	0 (success), neg. errno (failure)
 */
#define VI_CAPTURE_RELEASE \
	_IOW('I', 2, __u32)

/**
 * @brief Execute a blocking capture-control IVC request to RCE.
 *
 * @param[in]	ptr	Pointer to a struct  @ref vi_capture_control_msg
 *
 * @returns	0 (success), neg. errno (failure)
 */
#define VI_CAPTURE_SET_CONFIG \
	_IOW('I', 3, struct vi_capture_control_msg)

/**
 * @brief Reset the VI channel in RCE synchronously w/ the KMD; all pending
 * capture descriptors in the queue are discarded and syncpoint values
 * fast-forwarded to unblock waiting clients.
 *
 * @param[in]	reset_flags	uint32_t bitmask of
 *				@ref CAPTURE_CHANNEL_RESET_FLAGS

 * @returns	0 (success), neg. errno (failure)
 */
#define VI_CAPTURE_RESET \
	_IOW('I', 4, __u32)

/**
 * @brief Retrieve the ids and current values of the progress, embedded data and
 * line timer syncpoints, and VI HW channel(s) allocated by RCE.
 *
 * If successful, the queried values are written back to the input struct.
 *
 * @param[in,out]	ptr	Pointer to a struct @ref vi_capture_info
 *
 * @returns	0 (success), neg. errno (failure)
 */
#define VI_CAPTURE_GET_INFO \
	_IOR('I', 5, struct vi_capture_info)

/**
 * @brief Enqueue a capture request to RCE, the addresses to surface buffers in
 * the descriptor (referenced by the buffer_index) are pinned and patched.
 *
 * The payload shall be a pointer to a struct @ref vi_capture_req.
 *
 * @param[in]	ptr	Pointer to a struct @ref vi_capture_compand
 *
 * @returns	0 (success), neg. errno (failure)
 */
#define VI_CAPTURE_REQUEST \
	_IOW('I', 6, struct vi_capture_req)

/**
 * Wait on the next completion of an enqueued frame, signalled by RCE. The
 * status in the frame's capture descriptor is safe to read when this completes
 * w/o a -ETIMEDOUT or other error.
 *
 * @note This call completes for the frame at the head of the FIFO queue, and is
 * not necessarily for the most recently enqueued capture request.
 *
 * @param[in]	timeout_ms	uint32_t timeout [ms], 0 for indefinite
 *
 * @returns	0 (success), neg. errno (failure)
 */
#define VI_CAPTURE_STATUS \
	_IOW('I', 7, __u32)

/**
 * @brief Set up the capture progress status notifier array, which is a
 * replacement for the blocking @ref VI_CAPTURE_STATUS call; allowing for
 * out-of-order frame completion notifications.
 *
 * The values written by the KMD are any of the
 * @ref CAPTURE_PROGRESS_NOTIFIER_STATES.
 *
 * @param[in]	ptr	Pointer to a struct @ref vi_capture_progress_status_req
 *
 * @returns	0 (success), neg. errno (failure)
 */
#define VI_CAPTURE_SET_PROGRESS_STATUS_NOTIFIER \
	_IOW('I', 9, struct vi_capture_progress_status_req)

/**
 * @brief Perform an operation on the surface buffer by setting the bitwise
 * @a flag field with @ref CAPTURE_BUFFER_OPS flags.
 *
 * @param[in]	ptr	Pointer to a struct @ref vi_buffer_req
 * @returns	0 (success), neg. errno (failure)
 */
#define VI_CAPTURE_BUFFER_REQUEST \
	_IOW('I', 10, struct vi_buffer_req)

/** @} */

/**
 * @brief Unpins previously pinned buffers for a VI capture channel.
 *
 * This function unpins buffers associated with a specified buffer index in a VI capture channel.
 * It performs the following operations:
 * - Validates the input capture channel pointer.
 * - Retrieves and validates the capture data from the channel.
 * - Locks the unpins list mutex using @ref mutex_lock().
 * - Accesses the unpins list for the specified buffer index.
 * - If there are pending unpins, iterates through each and releases the mappings
 *   by calling @ref put_mapping().
 * - Resets the unpins data using @ref memset().
 * - Unlocks the unpins list mutex using @ref mutex_unlock().
 *
 * @param[in] chan          Pointer to the @ref tegra_vi_channel structure.
 *                          Valid value: non-NULL.
 * @param[in] buffer_index  Index of the buffer to unpin.
 *                          Valid range: corresponds to entries in the unpins list.
 */
void vi_capture_request_unpin(
	struct tegra_vi_channel *chan,
	uint32_t buffer_index)
{
	struct vi_capture *capture;
	struct capture_common_unpins *unpins;
	int i = 0;

	if (unlikely(chan == NULL)) {
		pr_err("%s: vi channel pointer is NULL\n", __func__);
		return;
	}

	capture = chan->capture_data;
	if (unlikely(capture == NULL)) {
		dev_err(chan->dev, "%s: vi capture uninitialized\n", __func__);
		return;
	}

	mutex_lock(&capture->unpins_list_lock);
	unpins = &capture->unpins_list[buffer_index];

	if (unpins->num_unpins != 0) {
		for (i = 0; i < unpins->num_unpins; i++) {
			if (capture->buf_ctx != NULL && unpins->data[i] != NULL)
				put_mapping(capture->buf_ctx, unpins->data[i]);
		}
		(void)memset(unpins, 0U,sizeof(*unpins));
	}
	mutex_unlock(&capture->unpins_list_lock);
}
EXPORT_SYMBOL(vi_capture_request_unpin);

static struct vi_channel_drv *chdrv_;
static DEFINE_MUTEX(chdrv_lock);

/**
 * @brief Opens a VI channel with the specified configuration.
 *
 * This function performs the following operations:
 * - Attempts to acquire the channel driver lock using @ref mutex_lock_interruptible().
 * - Retrieves the channel driver instance.
 * - Validates that the channel driver is initialized and the channel index is within
 *   the available range.
 * - Releases the channel driver lock using @ref mutex_unlock().
 * - Allocates memory for a new @ref tegra_vi_channel structure using @ref kzalloc().
 * - Initializes the channel structure with driver data and operations.
 * - Initializes capture settings by calling @ref vi_capture_init().
 * - Acquires the channel driver's lock using @ref mutex_lock().
 * - Checks if the specified channel is already in use via @ref rcu_access_pointer().
 * - Assigns the new channel to the driver's channel array using @ref rcu_assign_pointer().
 * - Releases the channel driver's lock.
 * - On failure, cleans up resources by calling @ref vi_capture_shutdown() and
 *   @ref kfree().
 *
 * @param[in]  channel        The index of the channel to open.
 *                            Valid range: [0 .. @ref vi_channel_drv.num_channels - 1]
 * @param[in]  is_mem_pinned  Flag indicating whether memory should be pinned.
 *                            Valid value: true or false.
 *
 * @retval (tegra_vi_channel *)       Pointer to @ref tegra_vi_channel On successful completion.
 * @retval ERR_PTR(-ERESTARTSYS)      If @ref mutex_lock_interruptible() is interrupted.
 * @retval ERR_PTR(-ENODEV)           If the channel driver is unavailable or
 *                                    the channel index is out of range.
 * @retval ERR_PTR(-ENOMEM)           If memory allocation via @ref kzalloc() fails.
 * @retval ERR_PTR(-EBUSY)            If the specified channel is already open via
 *                                    @ref rcu_access_pointer().
 * @retval ERR_PTR(err)               Errors propagated from @ref vi_capture_init().
 */
struct tegra_vi_channel *vi_channel_open_ex(
	unsigned int channel,
	bool is_mem_pinned)
{
	struct tegra_vi_channel *chan;
	struct vi_channel_drv *chan_drv;
	int err;

	if (mutex_lock_interruptible(&chdrv_lock))
		return ERR_PTR(-ERESTARTSYS);

	chan_drv = chdrv_;

	if (chan_drv == NULL || channel >= chan_drv->num_channels) {
		mutex_unlock(&chdrv_lock);
		return ERR_PTR(-ENODEV);
	}
	mutex_unlock(&chdrv_lock);

	chan = kzalloc(sizeof(*chan), GFP_KERNEL | GFP_NOWAIT);
	if (unlikely(chan == NULL))
		return ERR_PTR(-ENOMEM);

	chan->drv = chan_drv;
	chan->vi_capture_pdev = chan_drv->vi_capture_pdev;

	chan->ops = chan_drv->ops;

	err = vi_capture_init(chan, is_mem_pinned);
	if (err < 0)
		goto error;

	mutex_lock(&chan_drv->lock);
	if (rcu_access_pointer(chan_drv->channels[channel]) != NULL) {
		mutex_unlock(&chan_drv->lock);
		err = -EBUSY;
		goto rcu_err;
	}

	rcu_assign_pointer(chan_drv->channels[channel], (void *)chan);
	mutex_unlock(&chan_drv->lock);

	return chan;

rcu_err:
	vi_capture_shutdown(chan);
error:
	kfree(chan);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(vi_channel_open_ex);

/**
 * @brief Closes a VI channel and releases associated resources.
 *
 * This function performs the following operations:
 * - Shuts down the capture on the specified channel using @ref vi_capture_shutdown().
 * - Acquires the channel driver's lock using @ref mutex_lock().
 * - Verifies that the channel being closed is the one stored in the driver's channel array
 *   using @ref rcu_access_pointer().
 * - Sets the corresponding entry in the driver's channel array to NULL using @ref RCU_INIT_POINTER().
 * - Releases the channel driver's lock using @ref mutex_unlock().
 * - Frees the memory allocated for the channel structure using @ref kfree_rcu().
 *
 * @param[in]  channel  The index of the channel to close.
 *                      Valid range: [0 .. @ref vi_channel_drv.num_channels - 1]
 * @param[in]  chan     Pointer to the @ref tegra_vi_channel structure to close.
 *                      Valid value: non-NULL.
 *
 * @retval 0    On successful completion.
 */
int vi_channel_close_ex(
	unsigned int channel,
	struct tegra_vi_channel *chan)
{
	struct vi_channel_drv *chan_drv = chan->drv;

	vi_capture_shutdown(chan);

	mutex_lock(&chan_drv->lock);

	if (rcu_access_pointer(chan_drv->channels[channel]) != chan)
		dev_warn(chan_drv->dev, "%s: dev does not match\n", __func__);

	RCU_INIT_POINTER(chan_drv->channels[channel], NULL);

	mutex_unlock(&chan_drv->lock);
	kfree_rcu(chan, rcu);

	return 0;
}
EXPORT_SYMBOL(vi_channel_close_ex);

/**
 * @brief Opens a VI channel and associates it with a file instance.
 *
 * This function performs the following operations:
 * - Retrieves the channel index from the provided @ref inode using @ref iminor().
 * - Calls @ref vi_channel_open_ex() with the channel index and a memory pinned flag.
 * - Checks if the returned channel pointer is an error using @ref IS_ERR().
 * - Assigns the opened channel to the @ref file's private data.
 * - Calls @ref nonseekable_open() to perform standard file opening operations.
 * - Returns the result of @ref nonseekable_open().
 *
 * @param[in]  inode  Pointer to the @ref inode structure representing the file.
 *                    Valid value: non-NULL.
 * @param[in]  file   Pointer to the @ref file structure representing the opened file.
 *                    Valid value: non-NULL.
 *
 * @retval 0                 On successful opening of the VI channel.
 * @retval -ERESTARTSYS      If @ref vi_channel_open_ex() fails due to
 *                           @ref mutex_lock_interruptible() being interrupted.
 * @retval -ENODEV           If @ref vi_channel_open_ex() fails because the channel
 *                           driver is unavailable or the channel index is out of range.
 * @retval -ENOMEM           If @ref vi_channel_open_ex() fails due to memory
 *                           allocation failure via @ref kzalloc().
 * @retval -EBUSY            If @ref vi_channel_open_ex() fails because the specified
 *                           channel is already open via @ref rcu_access_pointer().
 * @retval (int)             If @ref vi_channel_open_ex() fails via @ref vi_capture_init()
 *                           or if @ref nonseekable_open() fails, returns the corresponding
 *                           error code via @ref nonseekable_open().
 */
static int vi_channel_open(
	struct inode *inode,
	struct file *file)
{
	unsigned int channel = iminor(inode);
	struct tegra_vi_channel *chan;

	chan = vi_channel_open_ex(channel, true);
	if (IS_ERR(chan))
		return PTR_ERR(chan);

	file->private_data = chan;

	return nonseekable_open(inode, file);
}

/**
 * @brief Releases a VI channel and cleans up associated resources.
 *
 * This function performs the following operations:
 * - Retrieves the channel index from the provided @ref inode using @ref iminor().
 * - Obtains the channel data from the @a file's private data.
 * - Calls @ref vi_channel_close_ex() with the channel index and channel data to close the VI channel.
 * - Returns 0 to indicate successful release.
 *
 * @param[in]  inode  Pointer to the @ref inode structure representing the file.
 *                    Valid value: non-NULL.
 * @param[in]  file   Pointer to the @ref file structure representing the opened file.
 *                    Valid value: non-NULL.
 *
 * @retval 0    On successful release of the VI channel.
 */
static int vi_channel_release(
	struct inode *inode,
	struct file *file)
{
	struct tegra_vi_channel *chan = file->private_data;
	unsigned int channel = iminor(inode);

	vi_channel_close_ex(channel, chan);

	return 0;
}

/**
 * @brief Pins buffers for a VI capture request while holding the necessary lock.
 *
 * This function performs the following operations:
 * - Retrieves the capture data from the provided channel.
 * - Calculates the buffer amount and checks for multiplication overflow by calling
 *   @ref check_mul_overflow().
 * - Computes the pointer to the capture descriptor based on the buffer amount.
 * - Validates that the number of surfaces does not exceed the maximum allowed by using
 *   @ref BUG_ON().
 * - Iterates over the ATOMP surfaces and calls @ref capture_common_pin_and_get_iova()
 *   for each surface to pin and obtain IOVA mappings.
 * - Attempts to pin and obtain an IOVA mapping for the engine status surface by calling
 *   @ref capture_common_pin_and_get_iova().
 * @note Cleanup of unpinned buffers is handled by @ref vi_capture_request_unpin().
 *
 * @param[in]      chan             Pointer to the @ref tegra_vi_channel structure.
 *                                  Valid value: non-NULL.
 * @param[in]      req              Pointer to the @ref vi_capture_req structure representing
 *                                  the capture request.
 *                                  Valid value: non-NULL.
 * @param[in, out] request_unpins   Pointer to the @ref capture_common_unpins structure
 *                                  used for managing unpin operations.
 *                                  Valid value: non-NULL.
 *
 * @retval 0         On successful pinning of all buffers.
 * @retval -EFAULT   If @ref check_mul_overflow() detects an overflow.
 * @retval (int)     If @ref capture_common_pin_and_get_iova() fails while pinning ATOMP
 *                   surfaces or the engine status surface.
 */
static int pin_vi_capture_request_buffers_locked(struct tegra_vi_channel *chan,
		struct vi_capture_req *req,
		struct capture_common_unpins *request_unpins)
{
	struct vi_capture *capture = chan->capture_data;
	struct capture_descriptor *desc = NULL;
	struct capture_descriptor_memoryinfo* desc_mem =
			&capture->requests_memoryinfo[req->buffer_index];
	int i;
	int err = 0;
	uint32_t buffer_amount = 0;

	if (check_mul_overflow(req->buffer_index, capture->request_size, &buffer_amount)) {
		dev_err(chan->dev, "%s: Requests memoryinfo overflow\n", __func__);
		return -EFAULT;
	}

	desc = (struct capture_descriptor *)
					(capture->requests.va + buffer_amount);

	/* Buffer count: ATOMP surfaces + engine_surface */
	BUG_ON(VI_NUM_ATOMP_SURFACES + 1U >= MAX_PIN_BUFFER_PER_REQUEST);

	for (i = 0; i < VI_NUM_ATOMP_SURFACES; i++) {
		err = capture_common_pin_and_get_iova(capture->buf_ctx,
			desc->ch_cfg.atomp.surface[i].offset_hi,
			desc->ch_cfg.atomp.surface[i].offset,
			&desc_mem->surface[i].base_address, &desc_mem->surface[i].size,
			request_unpins);

		if (err) {
			dev_err(chan->dev, "%s: get atomp iova failed\n", __func__);
			goto fail;
		}
	}

	err = capture_common_pin_and_get_iova(capture->buf_ctx,
		desc->engine_status.offset_hi,
		desc->engine_status.offset,
		&desc_mem->engine_status_surface_base_address,
		&desc_mem->engine_status_surface_size,
		request_unpins);

	if (err) {
		dev_err(chan->dev, "%s: get engine surf iova failed\n", __func__);
		goto fail;
	}

fail:
	/* Unpin cleanup is done in vi_capture_request_unpin() */
	return err;
}

/**
 * @brief Handles IOCTL commands for a VI channel.
 *
 * This function performs the following operations:
 * - Retrieves the channel data from the @ref file's private data.
 * - Validates that the channel and capture context are initialized.
 * - Determines the command number using @ref _IOC_NR().
 * - Processes the command based on its number:
 *   - **VI_CAPTURE_SETUP:**
 *     - Copies setup data from user space using @ref copy_from_user().
 *     - Calls @ref vi_get_nvhost_device() to obtain the NVHost device.
 *     - Validates the request size.
 *     - Checks if buffer setup is already done.
 *     - Creates a buffer table using @ref create_buffer_table().
 *     - Pins memory using @ref capture_common_pin_memory() and validates buffer size.
 *     - Sets up capture using @ref vi_capture_setup().
 *     - Handles errors by cleaning up resources using @ref capture_common_unpin_memory()
 *       and @ref destroy_buffer_table().
 *   - **VI_CAPTURE_RESET:**
 *     - Copies reset flags from user space using @ref copy_from_user().
 *     - Calls @ref vi_capture_reset() to reset the capture.
 *     - If successful, unpins all requests using @ref vi_capture_request_unpin().
 *   - **VI_CAPTURE_RELEASE:**
 *     - Copies release flags from user space using @ref copy_from_user().
 *     - Calls @ref vi_capture_release() to release the capture.
 *     - If successful, unpins all requests using @ref vi_capture_request_unpin(),
 *       unpins memory using @ref capture_common_unpin_memory(), destroys the buffer
 *       table using @ref destroy_buffer_table(), and frees the unpins list using vfree().
 *   - **VI_CAPTURE_GET_INFO:**
 *     - Initializes an info structure.
 *     - Calls @ref vi_capture_get_info() to retrieve capture information.
 *     - Copies info back to user space using @ref copy_to_user().
 *   - **VI_CAPTURE_SET_CONFIG:**
 *     - Copies control message from user space using @ref copy_from_user().
 *     - Calls @ref vi_capture_control_message_from_user() to set the configuration.
 *   - **VI_CAPTURE_REQUEST:**
 *     - Copies capture request from user space using @ref copy_from_user().
 *     - Validates the number of relocs, buffer index, and non-null unpins list.
 *     - Calls @ref mutex_lock() to lock the unpins list mutex.
 *     - Pins request buffers using @ref pin_vi_capture_request_buffers_locked().
 *     - Calls @ref mutex_unlock() to unlock the unpins list mutex.
 *     - Submits the capture request using @ref vi_capture_request().
 *     - Unpins on failure using @ref vi_capture_request_unpin().
 *   - **VI_CAPTURE_STATUS:**
 *     - Copies timeout value from user space using @ref copy_from_user().
 *     - Calls @ref vi_capture_status() to get the capture status.
 *   - **VI_CAPTURE_SET_PROGRESS_STATUS_NOTIFIER:**
 *     - Copies progress status request from user space using @ref copy_from_user().
 *     - Calls @ref vi_capture_set_progress_status_notifier() to set the notifier.
 *   - **VI_CAPTURE_BUFFER_REQUEST:**
 *     - Copies buffer request from user space using @ref copy_from_user().
 *     - Calls @ref capture_buffer_request() to handle the buffer request.
 * - Returns the appropriate status code based on the operation outcome.
 *
 * In case of any errors during the processing of commands, the function ensures that
 * resources are properly cleaned up to maintain system stability.
 *
 * @param[in]  file  Pointer to the @ref file structure representing the opened file.
 *                   Valid value: non-NULL.
 * @param[in]  cmd   IOCTL command number to be processed.
 *                   Valid range: Defined by @ref VI_CHANNEL_IOCTLS.
 * @param[in]  arg   Argument for the IOCTL command, typically a user space pointer.
 *                   Valid value: depends on the command.
 *
 * @retval 0                 On successful processing of the IOCTL command.
 * @retval -EINVAL           If the channel is invalid, context is uninitialized, or
 *                           specific validation checks fail via @ref vi_get_nvhost_device().
 * @retval -EFAULT           If copying data from or to user space fails using @ref copy_from_user()
 *                           or @ref copy_to_user(), or if memory setup fails by invoking
 *                           @ref capture_common_pin_memory() or @ref create_buffer_table().
 * @retval -ENOMEM           If buffer size does not match queue depth during setup.
 * @retval -EBUSY            If a descriptor is still in use by RTCU via @ref vi_capture_request_unpin().
 * @retval -ENOIOCTLCMD      If an unknown IOCTL command is received.
 * @retval (int)             Specific errors returned by external functions such as
 *                           @ref vi_capture_setup(), @ref vi_capture_reset(),
 *                           @ref vi_capture_release(), @ref vi_capture_get_info(),
 *                           @ref vi_capture_control_message_from_user(),
 *                           @ref pin_vi_capture_request_buffers_locked(),
 *                           @ref vi_capture_request(), @ref vi_capture_status(),
 *                           @ref vi_capture_set_progress_status_notifier(),
 *                           or @ref capture_buffer_request().
 */
static long vi_channel_ioctl(
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct tegra_vi_channel *chan = file->private_data;
	struct vi_capture *capture;
	void __user *ptr = (void __user *)arg;
	int err = -EFAULT;

	if (unlikely(chan == NULL)) {
		pr_err("%s: invalid channel\n", __func__);
		return -EINVAL;
	}

	capture = chan->capture_data;
	if (unlikely(capture == NULL)) {
		dev_err(chan->dev, "%s: invalid context", __func__);
		return -EINVAL;
	}

	switch (_IOC_NR(cmd)) {
	case _IOC_NR(VI_CAPTURE_SETUP): {
		struct vi_capture_setup setup = {};
		struct capture_buffer_table *buffer_ctx;

		if (copy_from_user(&setup, ptr, sizeof(setup)))
			break;

		vi_get_nvhost_device(chan, &setup);
		if (chan->dev == NULL) {
			dev_err(&chan->vi_capture_pdev->dev,
				"%s: channel device is NULL",
				__func__);
			return -EINVAL;
		}

		if (setup.request_size < sizeof(struct capture_descriptor)) {
			dev_err(chan->dev,
				"request size is too small to fit capture descriptor\n");
			return -EINVAL;
		}

		if (capture->buf_ctx) {
			dev_err(chan->dev, "vi buffer setup already done");
			return -EFAULT;
		}

		buffer_ctx = create_buffer_table(chan->dev);
		if (buffer_ctx == NULL) {
			dev_err(chan->dev, "vi buffer setup failed");
			return -ENOMEM;
		}

		/* pin the capture descriptor ring buffer */
		err = capture_common_pin_memory(capture->rtcpu_dev,
				setup.mem, &capture->requests);
		if (err < 0) {
			dev_err(chan->dev,
				"%s: memory setup failed\n", __func__);
			destroy_buffer_table(buffer_ctx);
			buffer_ctx = NULL;
			return -EFAULT;
		}

		/* Check that buffer size matches queue depth */
		if ((capture->requests.buf->size / setup.request_size) <
				setup.queue_depth) {
			dev_err(chan->dev,
				"%s: descriptor buffer is too small for given queue depth\n",
				__func__);
			capture_common_unpin_memory(&capture->requests);
			destroy_buffer_table(buffer_ctx);
			buffer_ctx = NULL;
			return -ENOMEM;
		}

		setup.iova = capture->requests.iova;
		err = vi_capture_setup(chan, &setup);
		if (err < 0) {
			dev_err(chan->dev, "vi capture setup failed\n");
			capture_common_unpin_memory(&capture->requests);
			destroy_buffer_table(buffer_ctx);
			buffer_ctx = NULL;
			return err;
		}

		capture->buf_ctx = buffer_ctx;
		break;
	}

	case _IOC_NR(VI_CAPTURE_RESET): {
		uint32_t reset_flags;
		int i;

		if (copy_from_user(&reset_flags, ptr, sizeof(reset_flags)))
			break;

		err = vi_capture_reset(chan, reset_flags);
		if (err < 0)
			dev_err(chan->dev, "vi capture reset failed\n");
		else {
			for (i = 0; i < capture->queue_depth; i++)
				vi_capture_request_unpin(chan, i);
		}

		break;
	}

	case _IOC_NR(VI_CAPTURE_RELEASE): {
		uint32_t reset_flags;
		int i;

		if (copy_from_user(&reset_flags, ptr, sizeof(reset_flags)))
			break;

		err = vi_capture_release(chan, reset_flags);
		if (err < 0)
			dev_err(chan->dev, "vi capture release failed\n");
		else {
			for (i = 0; i < capture->queue_depth; i++)
				vi_capture_request_unpin(chan, i);
			capture_common_unpin_memory(&capture->requests);

			if (capture->buf_ctx != NULL) {
				destroy_buffer_table(capture->buf_ctx);
				capture->buf_ctx = NULL;
			}

			vfree(capture->unpins_list);
			capture->unpins_list = NULL;
		}

		break;
	}

	case _IOC_NR(VI_CAPTURE_GET_INFO): {
		struct vi_capture_info info;
		(void)memset(&info, 0, sizeof(info));

		err = vi_capture_get_info(chan, &info);
		if (err < 0) {
			dev_err(chan->dev, "vi capture get info failed\n");
			break;
		}
		if (copy_to_user(ptr, &info, sizeof(info)))
			err = -EFAULT;
		break;
	}

	case _IOC_NR(VI_CAPTURE_SET_CONFIG): {
		struct vi_capture_control_msg msg = {};

		if (copy_from_user(&msg, ptr, sizeof(msg)))
			break;
		err = vi_capture_control_message_from_user(chan, &msg);
		if (err < 0)
			dev_err(chan->dev, "vi capture set config failed\n");
		break;
	}

	case _IOC_NR(VI_CAPTURE_REQUEST): {
		struct vi_capture_req req;
		struct capture_common_unpins *request_unpins;

		if (copy_from_user(&req, ptr, sizeof(req)))
			break;

		if (req.num_relocs == 0) {
			dev_err(chan->dev, "request must have non-zero relocs\n");
			return -EINVAL;
		}

		if (req.buffer_index >= capture->queue_depth) {
			dev_err(chan->dev, "buffer index is out of bound\n");
			return -EINVAL;
		}

		/* Don't let to speculate with invalid buffer_index value */
		spec_bar();

		if (capture->unpins_list == NULL) {
			dev_err(chan->dev, "Channel setup incomplete\n");
			return -EINVAL;
		}

		mutex_lock(&capture->unpins_list_lock);

		request_unpins = &capture->unpins_list[req.buffer_index];

		if (request_unpins->num_unpins != 0U) {
			dev_err(chan->dev, "Descriptor is still in use by rtcpu\n");
			mutex_unlock(&capture->unpins_list_lock);
			return -EBUSY;
		}
		err = pin_vi_capture_request_buffers_locked(chan, &req,
				request_unpins);

		mutex_unlock(&capture->unpins_list_lock);

		if (err < 0) {
			dev_err(chan->dev,
				"pin request failed\n");
			vi_capture_request_unpin(chan, req.buffer_index);
			break;
		}

		err = vi_capture_request(chan, &req);
		if (err < 0) {
			dev_err(chan->dev,
				"vi capture request submit failed\n");
			vi_capture_request_unpin(chan, req.buffer_index);
		}

		break;
	}

	case _IOC_NR(VI_CAPTURE_STATUS): {
		uint32_t timeout_ms;

		if (copy_from_user(&timeout_ms, ptr, sizeof(timeout_ms)))
			break;
		err = vi_capture_status(chan, timeout_ms);
		if (err < 0)
			dev_err(chan->dev,
				"vi capture get status failed\n");
		break;
	}

	case _IOC_NR(VI_CAPTURE_SET_PROGRESS_STATUS_NOTIFIER): {
		struct vi_capture_progress_status_req req;

		if (copy_from_user(&req, ptr, sizeof(req)))
			break;
		err = vi_capture_set_progress_status_notifier(chan, &req);
		if (err < 0)
			dev_err(chan->dev,
					"setting progress status buffer failed\n");
		break;
	}

	case _IOC_NR(VI_CAPTURE_BUFFER_REQUEST): {
		struct vi_buffer_req req;

		if (copy_from_user(&req, ptr, sizeof(req)) != 0U)
			break;

		if (capture->buf_ctx == NULL) {
			dev_err(chan->dev, "vi buffer setup not done\n");
			break;
		}

		err = capture_buffer_request(
			capture->buf_ctx, req.mem, req.flag);
		if (err < 0)
			dev_err(chan->dev, "vi buffer request failed\n");
		break;
	}

	default: {
		dev_err(chan->dev, "%s:Unknown ioctl\n", __func__);
		return -ENOIOCTLCMD;
	}
	}

	return err;
}

static const struct file_operations vi_channel_fops = {
	.owner = THIS_MODULE,
#if defined(NV_NO_LLSEEK_PRESENT)
	.llseek = no_llseek,
#endif
	.unlocked_ioctl = vi_channel_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = vi_channel_ioctl,
#endif
	.open = vi_channel_open,
	.release = vi_channel_release,
};

/* Character device */
static struct class *vi_channel_class;
static int vi_channel_major;

/**
 * @brief Registers a VI channel driver with a specified number of channels.
 *
 * This function performs the following operations:
 * - Allocates memory for the channel driver structure and channel pointers using
 *   @ref devm_kzalloc().
 * - Initializes the channel driver structure with the platform device and the number
 *   of channels.
 * - Initializes the driver's mutex lock using @ref mutex_init().
 * - Acquires the global driver lock using @ref mutex_lock().
 * - Assigns the newly allocated driver structure to the global driver reference.
 * - Releases the global driver lock using @ref mutex_unlock().
 * - Iterates over the number of channels and creates device entries for each channel
 *   using @ref device_create().
 *
 * @param[in]  ndev             Pointer to the @ref platform_device structure representing the
 *                              platform device.
 *                              Valid value: non-NULL.
 * @param[in]  max_vi_channels  Maximum number of VI channels to register.
 *                              Valid range: [0 .. UINT32_MAX].
 *
 * @retval 0          On successful registration of the VI channel driver.
 * @retval -ENOMEM    If memory allocation via @ref devm_kzalloc() fails.
 * @retval -EBUSY     If a VI channel driver is already registered.
 */
int vi_channel_drv_register(
	struct platform_device *ndev,
	unsigned int max_vi_channels)
{
	struct vi_channel_drv *chan_drv;
	int err = 0;
	unsigned int i;

	chan_drv = devm_kzalloc(&ndev->dev, sizeof(*chan_drv) +
			max_vi_channels * sizeof(struct tegra_vi_channel *),
			GFP_KERNEL);
	if (unlikely(chan_drv == NULL))
		return -ENOMEM;

	chan_drv->dev = NULL;
	chan_drv->ndev = NULL;
	chan_drv->vi_capture_pdev = ndev;
	chan_drv->num_channels = max_vi_channels;

	mutex_init(&chan_drv->lock);

	mutex_lock(&chdrv_lock);
	if (chdrv_ != NULL) {
		mutex_unlock(&chdrv_lock);
		dev_warn(chdrv_->dev, "%s: chdrv is busy\n", __func__);
		err = -EBUSY;
		goto error;
	}
	chdrv_ = chan_drv;
	mutex_unlock(&chdrv_lock);

	for (i = 0; i < chan_drv->num_channels; i++) {
		dev_t devt = MKDEV((unsigned long)vi_channel_major, i);

		struct device *dev = &chan_drv->vi_capture_pdev->dev;
		device_create(vi_channel_class, dev, devt, NULL,
				"capture-vi-channel%u", i);
	}

	return 0;

error:
	return err;
}
EXPORT_SYMBOL(vi_channel_drv_register);

/**
 * @brief Registers the operations for the VI channel driver.
 *
 * This function performs the following operations:
 * - Retrieves the channel driver instance from the global variable.
 * - Checks if the channel driver instance is initialized.
 * - Acquires the channel driver lock using @ref mutex_lock().
 * - If the operations table in the channel driver is not already set, assigns the provided
 *   operations to it.
 * - If the operations table is already registered, logs a debug message using @ref dev_dbg().
 * - Releases the channel driver lock using @ref mutex_unlock().
 *
 * @param[in]  ops        Pointer to the @ref vi_channel_drv_ops structure containing
 *                        the operations to register.
 *                        Valid value: non-NULL.
 *
 * @retval 0                On successful registration of the operations.
 * @retval -EPROBE_DEFER    If the channel driver is NULL.
 */
int vi_channel_drv_fops_register(
	const struct vi_channel_drv_ops *ops)
{
	int err = 0;
	struct vi_channel_drv *chan_drv;

	chan_drv = chdrv_;
	if (chan_drv == NULL) {
		err = -EPROBE_DEFER;
		goto error;
	}

	mutex_lock(&chdrv_lock);
	if (chan_drv->ops == NULL)
		chan_drv->ops = ops;
	else
		dev_dbg(chan_drv->dev, "vi fops function table already registered\n");
	mutex_unlock(&chdrv_lock);

	return 0;

error:
	return err;
}
EXPORT_SYMBOL(vi_channel_drv_fops_register);

/**
 * @brief Unregisters a VI channel driver and releases associated resources.
 *
 * This function performs the following operations:
 * - Acquires the global driver lock using @ref mutex_lock().
 * - Retrieves the current channel driver instance.
 * - Clears the global driver reference.
 * - Releases the global driver lock using @ref mutex_unlock().
 * - Validates the major number.
 * - Iterates over the number of channels and destroys each device using @ref device_destroy().
 * - Frees the memory allocated for the channel driver structure using @ref devm_kfree().
 *
 * @param[in]  dev  Pointer to the @ref device structure representing the device.
 *                  Valid value: non-NULL.
 */
void vi_channel_drv_unregister(
	struct device *dev)
{
	struct vi_channel_drv *chan_drv;
	unsigned int i;

	mutex_lock(&chdrv_lock);
	chan_drv = chdrv_;
	chdrv_ = NULL;
	if (chan_drv->dev != dev)
		dev_warn(chan_drv->dev, "%s: dev does not match\n", __func__);
	mutex_unlock(&chdrv_lock);

	if (vi_channel_major < 0) {
		pr_err("%s: Invalid major number for VI channel\n", __func__);
		return;
	}

	for (i = 0; i < chan_drv->num_channels; i++) {
		dev_t devt = MKDEV(vi_channel_major, i);

		device_destroy(vi_channel_class, devt);
	}

	devm_kfree(&chan_drv->vi_capture_pdev->dev, chan_drv);
}
EXPORT_SYMBOL(vi_channel_drv_unregister);

/**
 * @brief Initializes the VI channel driver by creating the device class and registering
 *        character devices.
 *
 * This function performs the following operations:
 * - Creates a device class for VI channels using @ref class_create().
 * - Registers a character device with dynamic major number allocation using
 *   @ref register_chrdev().
 * - If character device registration fails, destroys the created device class
 *   using @ref class_destroy().
 *
 * @retval 0          On successful initialization and registration.
 * @retval -ENOMEM    If device class creation fails due to insufficient memory.
 * @retval -EINVAL    If character device registration fails.
 */
int vi_channel_drv_init(void)
{
#if defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG) /* Linux v6.4 */
	vi_channel_class = class_create("capture-vi-channel");
#else
	vi_channel_class = class_create(THIS_MODULE, "capture-vi-channel");
#endif
	if (IS_ERR(vi_channel_class))
		return PTR_ERR(vi_channel_class);

	vi_channel_major = register_chrdev(0, "capture-vi-channel",
						&vi_channel_fops);
	if (vi_channel_major < 0) {
		class_destroy(vi_channel_class);
		return vi_channel_major;
	}

	return 0;
}

/**
 * @brief Unregisters the VI channel driver (major) and releases associated resources.
 *
 * This function performs the following operations:
 * - Unregisters the character device for the VI channel using @ref unregister_chrdev().
 * - Destroys the VI channel device class using @ref class_destroy().
 */
void vi_channel_drv_exit(void)
{
	unregister_chrdev(vi_channel_major, "capture-vi-channel");
	class_destroy(vi_channel_class);
}
