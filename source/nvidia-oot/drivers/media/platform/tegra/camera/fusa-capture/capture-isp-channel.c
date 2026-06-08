// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2017-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

/**
 * @file drivers/media/platform/tegra/camera/fusa-capture/capture-isp-channel.c
 *
 * @brief ISP channel character device driver for the T186/T194 Camera RTCPU
 * platform.
 */

#include <nvidia/conftest.h>

#include <asm/ioctls.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/of_platform.h>
#include <linux/overflow.h>
#include <linux/nvhost.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/uaccess.h>
#include <media/fusa-capture/capture-isp.h>
#include <media/fusa-capture/capture-isp-channel.h>

/**
 * @brief ISP channel character device driver context.
 */
struct isp_channel_drv {
	struct device *dev; /**< ISP kernel @em device */
	struct platform_device *isp_capture_pdev;
		/**< Capture ISP driver platform device */
	u8 num_channels; /**< No. of ISP channel character devices */
	struct mutex lock; /**< ISP channel driver context lock. */
	struct platform_device *ndev; /**< ISP kernel @em platform_device */
	const struct isp_channel_drv_ops *ops;
		/**< ISP fops for Host1x syncpt/gos allocations */
	struct tegra_isp_channel *channels[];
		/**< Allocated ISP channel contexts */
};

/**
 * @defgroup ISP_CHANNEL_IOCTLS
 *
 * @brief ISP channel character device IOCTL API
 *
 * Clients in the UMD may open sysfs character devices representing ISP
 * channels, and perform configuration, and enqueue buffers in capture and
 * program requests to the low-level RCE subsystem via these IOCTLs.
 *
 * @{
 */

/**
 * @brief Set up ISP channel resources and request FW channel allocation in RCE.
 *
 * Initialize the ISP channel context and synchronization primitives, pin memory
 * for the capture and program process descriptor queues, set up the buffer
 * management table, initialize the capture/capture-control IVC channels and
 * request ISP FW channel allocation in RCE.
 *
 * @param[in]	ptr	Pointer to a struct @ref isp_capture_setup
 *
 * @returns	0 (success), neg. errno (failure)
 */
#define ISP_CAPTURE_SETUP \
	_IOW('I', 1, struct isp_capture_setup)

/**
 * @brief Release the ISP FW channel allocation in RCE, and all resources and
 * contexts in the KMD.
 *
 * @param[in]	rel	uint32_t bitmask of @ref CAPTURE_CHANNEL_RESET_FLAGS
 * @returns	0 (success), neg. errno (failure)
 */
#define ISP_CAPTURE_RELEASE \
	_IOW('I', 2, __u32)

/**
 * @brief Reset the ISP channel in RCE synchronously w/ the KMD; all pending
 * capture/program descriptors in the queue are discarded and syncpoint values
 * fast-forwarded to unblock waiting clients.
 *
 * @param[in]	rst	uint32_t bitmask of @ref CAPTURE_CHANNEL_RESET_FLAGS
 *
 * @returns	0 (success), neg. errno (failure)
 */
#define ISP_CAPTURE_RESET \
	_IOW('I', 3, __u32)

/**
 * @brief Retrieve the ids and current values of the progress, stats progress
 * syncpoints, and ISP FW channel allocated by RCE.
 *
 * If successful, the queried values are written back to the input struct.
 *
 * @param[in,out]	ptr	Pointer to a struct @ref isp_capture_info
 *
 * @returns	0 (success), neg. errno (failure)
 */
#define ISP_CAPTURE_GET_INFO \
	_IOR('I', 4, struct isp_capture_info)

/**
 * @brief Enqueue a process capture request to RCE, input and prefences are
 * allocated, and the addresses to surface buffers in the descriptor (referenced
 * by the buffer_index) are pinned and patched.
 *
 * @param[in]	ptr	Pointer to a struct @ref isp_capture_req
 *
 * @returns	0 (success), neg. errno (failure)
 */
#define ISP_CAPTURE_REQUEST \
	_IOW('I', 5, struct isp_capture_req)

/**
 * @brief Wait on the next completion of an enqueued frame, signalled by RCE.
 * The status in the frame's capture descriptor is safe to read when this
 * completes w/o a -ETIMEDOUT or other error.
 *
 * @note This call completes for the frame at the head of the FIFO queue, and is
 * not necessarily for the most recently enqueued process capture request.
 *
 * @param[in]	status	uint32_t timeout [ms], 0 for indefinite
 *
 * @returns	0 (success), neg. errno (failure)
 */
#define ISP_CAPTURE_STATUS \
	_IOW('I', 6, __u32)

/**
 * @brief Enqueue a program request to RCE, the addresses to the push buffer in
 * the descriptor (referenced by the buffer_index) are pinned and patched.
 *
 * @param[in]	ptr	Pointer to a struct @ref isp_program_req
 *
 * @returns	0 (success), neg. errno (failure)
 */
#define ISP_CAPTURE_PROGRAM_REQUEST \
	_IOW('I', 7, struct isp_program_req)

/**
 * @brief Wait on the next completion of an enqueued program, signalled by RCE.
 * The program execution is finished and is safe to free when this call
 * completes.
 *
 * @note This call completes for the program at the head of the FIFO queue, and
 * is not necessarily for the most recently enqueued program request.

 * @returns	0 (success), neg. errno (failure)
 */
#define ISP_CAPTURE_PROGRAM_STATUS \
	_IOW('I', 8, __u32)

/**
 * @brief Enqueue a joint capture and program request to RCE; this is equivalent
 * to calling @ref ISP_CAPTURE_PROGRAM_REQUEST and @ref ISP_CAPTURE_REQUEST
 * sequentially, but the number of KMD-RCE IVC transmissions is reduced to one
 * in each direction for every frame.
 *
 * @param[in]	ptr	Pointer to a struct @ref isp_capture_req_ex
 *
 * @returns	0 (success), neg. errno (failure)
 */
#define ISP_CAPTURE_REQUEST_EX \
	_IOW('I', 9, struct isp_capture_req_ex)

/**
 * @brief Set up the combined capture and program process progress status
 * notifier array, which is a replacement for the blocking
 * @ref ISP_CAPTURE_STATUS and @ref ISP_CAPTURE_PROGRAM_STATUS calls; allowing
 * for out-of-order frame process completion notifications.
 *
 * The values written by the KMD are any of the
 * @ref CAPTURE_PROGRESS_NOTIFIER_STATES.
 *
 * @param[in]	ptr	Pointer to a struct @ref isp_capture_progress_status_req
 *
 * @returns	0 (success), neg. errno (failure)
 */
#define ISP_CAPTURE_SET_PROGRESS_STATUS_NOTIFIER \
	_IOW('I', 10, struct isp_capture_progress_status_req)

/**
 * @brief Perform an operation on the surface buffer by setting the bitwise
 * @a flag field with @ref CAPTURE_BUFFER_OPS flags.
 *
 * @param[in]	ptr	Pointer to a struct @ref isp_buffer_req.
 *
 * @returns	0 (success), neg. errno (failure)
 */
#define ISP_CAPTURE_BUFFER_REQUEST \
	_IOW('I', 11, struct isp_buffer_req)

/**
 * @brief Get the ISP hardware capabilities.
 *
 * @param[in]	ptr	Pointer to a struct @ref isp_capabilities_info
 *
 * @returns	0 (success), neg. errno (failure)
 */
#define ISP_CAPTURE_GET_CAPABILITIES \
	_IOW('I', 12, struct isp_capabilities_info)

/** @} */

static struct isp_channel_drv *chdrv_;
static DEFINE_MUTEX(chdrv_lock);

/**
 * @brief Opens an ISP channel for the given @a inode and @a file.
 *
 * This function handles the opening of an Image Signal Processor (ISP) channel by performing
 * the following steps:
 * - Acquires the channel driver lock using @ref mutex_lock_interruptible().
 * - Retrieves the channel number from the provided @ref inode.
 * - Validates the availability of the channel driver and the specified channel number.
 * - Releases the channel driver lock using @ref mutex_unlock().
 * - Allocates and initializes a new @ref tegra_isp_channel structure.
 * - Initializes the capture process by calling @ref isp_capture_init().
 * - Acquires the channel driver lock using @ref mutex_lock().
 * - Registers the channel within the channel driver's channel array.
 * - Releases the channel driver lock using @ref mutex_unlock().
 * - Associates the channel with the opened file by setting private data for provided @ref file.
 * - Opens the file in a non-seekable mode using @ref nonseekable_open().
 *
 * If any step fails, the function performs necessary cleanup, including shutting down the
 * capture process by calling @ref isp_capture_shutdown() and freeing allocated memory
 * by calling @ref kfree().
 *
 * @note The act of opening an ISP channel character device node does not entail the
 * reservation of an ISP channel, ISP_CAPTURE_SETUP must be called afterwards
 * to request an allocation by RCE.
 *
 * @param[in] inode  Pointer to the @ref inode structure representing the file inode.
 *                   Valid Value: non-NULL.
 * @param[in] file   Pointer to the @ref file structure representing the opened file.
 *                   Valid Value: non-NULL.
 *
 * @retval 0                        Successfully opened the ISP channel.
 * @retval -ENOMEM                  Memory allocation @ref kzalloc() for the channel failed.
 * @retval -ENODEV                  Channel driver is unavailable or the channel number is invalid.
 * @retval -EBUSY                   The requested channel is already in use.
 * @retval -ERESTARTSYS             The lock acquisition was interrupted.
 * @retval (int)                    Initialization or file opening failed.
 */
static int isp_channel_open(
	struct inode *inode,
	struct file *file)
{
	struct tegra_isp_channel *chan;
	unsigned int channel = iminor(inode);
	struct isp_channel_drv *chan_drv;
	int err;

	if (mutex_lock_interruptible(&chdrv_lock))
		return -ERESTARTSYS;

	chan_drv = chdrv_;

	if (chan_drv == NULL || channel >= chan_drv->num_channels) {
		mutex_unlock(&chdrv_lock);
		return -ENODEV;
	}

	if (chan_drv->isp_capture_pdev == NULL) {
		mutex_unlock(&chdrv_lock);
		return -ENODEV;
	}

	mutex_unlock(&chdrv_lock);

	chan = kzalloc(sizeof(*chan), GFP_KERNEL);
	if (unlikely(chan == NULL))
		return -ENOMEM;

	chan->drv = chan_drv;
	chan->isp_dev = chan_drv->dev;
	chan->ndev = chan_drv->ndev;
	chan->ops = chan_drv->ops;
	chan->isp_capture_pdev = chan_drv->isp_capture_pdev;
	chan->priv = file;

	err = isp_capture_init(chan);
	if (err < 0)
		goto init_err;

	mutex_lock(&chan_drv->lock);
	if (chan_drv->channels[channel] != NULL) {
		mutex_unlock(&chan_drv->lock);
		err = -EBUSY;
		goto chan_err;
	}

	chan_drv->channels[channel] = chan;
	mutex_unlock(&chan_drv->lock);

	file->private_data = chan;

	return nonseekable_open(inode, file);

chan_err:
	isp_capture_shutdown(chan);
init_err:
	kfree(chan);
	return err;
}

/**
 * @brief Releases an ISP channel and performs necessary cleanup.
 *
 * This function releases the ISP channel associated with the provided @a file and @a inode.
 * It performs the following operations:
 * - Calls @ref isp_capture_shutdown() to shut down the ISP capture process.
 * - Acquires the channel driver's lock by invoking @ref mutex_lock().
 * - Verifies that the channel being released matches the registered channel.
 * - Removes the channel from the driver's channel array by setting the corresponding entry to NULL.
 * - Releases the channel driver's lock by calling @ref mutex_unlock().
 * - Frees the memory allocated for the channel using @ref kfree().
 *
 * @param[in]  inode  Pointer to the @ref inode structure representing the file's inode.
 *                    Valid Value: non-NULL.
 * @param[in]  file   Pointer to the @ref file structure representing the opened file.
 *                    Valid Value: non-NULL.
 *
 * @retval 0    Successfully released the ISP channel and performed cleanup.
 */
static int isp_channel_release(
	struct inode *inode,
	struct file *file)
{
	struct tegra_isp_channel *chan = file->private_data;
	unsigned int channel = iminor(inode);
	struct isp_channel_drv *chan_drv = chan->drv;

	isp_capture_shutdown(chan);

	mutex_lock(&chan_drv->lock);

	if (chan_drv->channels[channel] != chan)
		dev_warn(chan_drv->dev, "%s: dev does not match!\n", __func__);

	chan_drv->channels[channel] = NULL;

	mutex_unlock(&chan_drv->lock);
	kfree(chan);

	return 0;
}

/**
 * @brief Process an IOCTL call on an ISP channel character device.
 *
 * Depending on the specific IOCTL, the argument (@a arg) may be a pointer to a
 * defined struct payload that is copied from or back to user-space. This memory
 * is allocated and mapped from user-space and must be kept available until
 * after the IOCTL call completes.
 *
 * This is the @a ioctl file operation handler for an ISP channel node.
 *
 * @param[in]		file	ISP channel character device file struct
 * @param[in]		cmd	ISP channel IOCTL command
 * @param[in,out]	arg	IOCTL argument; numerical value or pointer
 *
 * @returns		0 (success), neg. errno (failure)
 */
/**
 * @brief Handles ioctl commands for the ISP channel.
 *
 * This function performs the following operations:
 * - Retrieves the ISP channel associated with the opened file.
 * - Validates the existence of the channel.
 * - Extracts the ioctl command number using @ref _IOC_NR().
 * - Executes the corresponding operation based on the command number:
 *   - For ISP_CAPTURE_SETUP:
 *     - Copies the @ref isp_capture_setup structure from user space using
 *       @ref copy_from_user().
 *     - Calls @ref isp_get_nvhost_device() to obtain the NVHost device.
 *     - Ensures the ISP device is valid.
 *     - Invokes @ref isp_capture_setup() with the setup data.
 *   - For ISP_CAPTURE_RESET:
 *     - Copies the reset parameter from user space using @ref copy_from_user().
 *     - Calls @ref isp_capture_reset() with the reset value.
 *   - For ISP_CAPTURE_RELEASE:
 *     - Copies the release parameter from user space using @ref copy_from_user().
 *     - Invokes @ref isp_capture_release() to release the capture.
 *   - For ISP_CAPTURE_GET_INFO:
 *     - Initializes the @ref isp_capture_info structure.
 *     - Calls @ref isp_capture_get_info() to retrieve capture information.
 *     - Copies the information back to user space using @ref copy_to_user().
 *   - For ISP_CAPTURE_REQUEST:
 *     - Copies the capture request data from user space using
 *       @ref copy_from_user().
 *     - Calls @ref isp_capture_request() to process the capture request.
 *   - For ISP_CAPTURE_STATUS:
 *     - Copies the timeout value from user space using @ref copy_from_user().
 *     - Invokes @ref isp_capture_status() with the timeout.
 *   - For ISP_CAPTURE_PROGRAM_REQUEST:
 *     - Copies the program request data from user space using
 *       @ref copy_from_user().
 *     - Calls @ref isp_capture_program_request() to submit the program request.
 *   - For ISP_CAPTURE_PROGRAM_STATUS:
 *     - Invokes @ref isp_capture_program_status() to get program status.
 *   - For ISP_CAPTURE_REQUEST_EX:
 *     - Copies the extended request data from user space using
 *       @ref copy_from_user().
 *     - Calls @ref isp_capture_request_ex() to submit the extended request.
 *   - For ISP_CAPTURE_SET_PROGRESS_STATUS_NOTIFIER:
 *     - Copies the notifier data from user space using
 *       @ref copy_from_user().
 *     - Invokes @ref isp_capture_set_progress_status_notifier() to set the notifier.
 *   - For ISP_CAPTURE_BUFFER_REQUEST:
 *     - Copies the buffer request data from user space using
 *       @ref copy_from_user().
 *     - Calls @ref isp_capture_buffer_request() to request buffers.
 * - Handles errors by setting appropriate error codes.
 *
 * @param[in]  file  Pointer to the @ref file structure representing the opened file.
 *                   Valid Value: non-NULL.
 * @param[in]  cmd   Ioctl command number.
 *                   Valid Range: Defined in @ref ISP_CHANNEL_IOCTLS.
 * @param[in]  arg   Argument for the ioctl command, typically a pointer to user data.
 *                   Valid Value: Depends on @a cmd.
 *
 * @retval  0                           Successfully executed the ioctl command.
 * @retval -EINVAL                      If the channel is invalid or the channel
 *                                      device is NULL, potentially due to @ref
 *                                      _IOC_NR() or @ref isp_get_nvhost_device().
 * @retval -EFAULT                      If copying data from/to user space fails
 *                                      using @ref copy_from_user() or @ref copy_to_user().
 * @retval -ENOIOCTLCMD                 If an unknown ioctl command is received.
 * @retval (int)                        Errors returned by @ref isp_capture_setup(),
 *                                      @ref isp_capture_reset(), @ref isp_capture_release(),
 *                                      @ref isp_capture_get_info(), @ref isp_capture_request(),
 *                                      @ref isp_capture_status(), @ref isp_capture_program_request(),
 *                                      @ref isp_capture_program_status(), @ref isp_capture_request_ex(),
 *                                      @ref isp_capture_set_progress_status_notifier(),
 *                                      or @ref isp_capture_buffer_request().
 */
static long isp_channel_ioctl(
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct tegra_isp_channel *chan = file->private_data;
	void __user *ptr = (void __user *)arg;
	long err = -EFAULT;

	if (unlikely(chan == NULL)) {
		pr_err("%s: invalid channel\n", __func__);
		return -EINVAL;
	}

	if (unlikely(ptr == NULL)) {
		pr_err("%s: invalid argument user pointer\n", __func__);
		return -EINVAL;
	}

	switch (_IOC_NR(cmd)) {
	case _IOC_NR(ISP_CAPTURE_SETUP): {
		struct isp_capture_setup setup = {};

		if (copy_from_user(&setup, ptr, sizeof(setup)))
			break;

		isp_get_nvhost_device(chan, &setup);
		if (chan->isp_dev == NULL) {
			dev_err(&chan->isp_capture_pdev->dev,
				"%s: channel device is NULL",
				__func__);
			return -EINVAL;
		}
		err = isp_capture_setup(chan, &setup);
		if (err)
			dev_err(chan->isp_dev, "isp capture setup failed\n");
		break;
	}

	case _IOC_NR(ISP_CAPTURE_RESET): {
		uint32_t rst;

		if (copy_from_user(&rst, ptr, sizeof(rst)))
			break;
		err = isp_capture_reset(chan, rst);
		if (err)
			dev_err(chan->isp_dev, "isp capture reset failed\n");
		break;
	}

	case _IOC_NR(ISP_CAPTURE_RELEASE): {
		uint32_t rel;

		if (copy_from_user(&rel, ptr, sizeof(rel)))
			break;
		err = isp_capture_release(chan, rel);
		if (err)
			dev_err(chan->isp_dev, "isp capture release failed\n");
		break;
	}

	case _IOC_NR(ISP_CAPTURE_GET_INFO): {
		struct isp_capture_info info;
		(void)memset(&info, 0, sizeof(info));

		err = isp_capture_get_info(chan, &info);
		if (err) {
			dev_err(chan->isp_dev, "isp capture get info failed\n");
			break;
		}
		if (copy_to_user(ptr, &info, sizeof(info)))
			err = -EFAULT;
		break;
	}

	case _IOC_NR(ISP_CAPTURE_REQUEST): {
		struct isp_capture_req req = {};

		if (copy_from_user(&req, ptr, sizeof(req)))
			break;
		err = isp_capture_request(chan, &req);
		if (err)
			dev_err(chan->isp_dev,
				"isp process capture request submit failed\n");
		break;
	}

	case _IOC_NR(ISP_CAPTURE_STATUS): {
		uint32_t timeout;

		if (copy_from_user(&timeout, ptr, sizeof(timeout)))
			break;
		err = isp_capture_status(chan, timeout);
		if (err)
			dev_err(chan->isp_dev,
				"isp process get status failed\n");
		break;
	}

	case _IOC_NR(ISP_CAPTURE_PROGRAM_REQUEST): {
		struct isp_program_req program_req = {};

		if (copy_from_user(&program_req, ptr, sizeof(program_req)))
			break;
		err = isp_capture_program_request(chan, &program_req);
		if (err)
			dev_err(chan->isp_dev,
				"isp process program request submit failed\n");
		break;
	}

	case _IOC_NR(ISP_CAPTURE_PROGRAM_STATUS): {
		err = isp_capture_program_status(chan);

		if (err)
			dev_err(chan->isp_dev,
				"isp process program get status failed\n");
		break;
	}

	case _IOC_NR(ISP_CAPTURE_REQUEST_EX): {
		struct isp_capture_req_ex req = {};

		if (copy_from_user(&req, ptr, sizeof(req)))
			break;
		err = isp_capture_request_ex(chan, &req);
		if (err)
			dev_err(chan->isp_dev,
				"isp process request extended submit failed\n");
		break;
	}
	case _IOC_NR(ISP_CAPTURE_SET_PROGRESS_STATUS_NOTIFIER): {
		struct isp_capture_progress_status_req req;

		if (copy_from_user(&req, ptr, sizeof(req)))
			break;
		err = isp_capture_set_progress_status_notifier(chan, &req);
		if (err)
			dev_err(chan->isp_dev,
				"isp capture set progress status buffers failed\n");
		break;
	}
	case _IOC_NR(ISP_CAPTURE_BUFFER_REQUEST): {
		struct isp_buffer_req req;

		if (copy_from_user(&req, ptr, sizeof(req)) != 0U)
			break;

		err = isp_capture_buffer_request(chan, &req);
		if (err < 0)
			dev_err(chan->isp_dev, "isp buffer req failed\n");
		break;
	}
	case _IOC_NR(ISP_CAPTURE_GET_CAPABILITIES): {
		struct isp_capabilities_info caps = {0};

		err = isp_capture_get_capabilities(chan, &caps);
		if (err < 0) {
			dev_err(chan->isp_dev, "isp get capabilities failed\n");
			break;
		}

		if (copy_to_user(ptr, &caps, sizeof(caps)) != 0UL) {
			err = -EFAULT;
			dev_err(chan->isp_dev, "failed to copy capabilities to user\n");
		}
		break;
	}
	default: {
		dev_err(chan->isp_dev, "%s:Unknown ioctl\n", __func__);
		return -ENOIOCTLCMD;
	}
	}

	return err;
}

static const struct file_operations isp_channel_fops = {
	.owner = THIS_MODULE,
#if defined(NV_NO_LLSEEK_PRESENT)
	.llseek = no_llseek,
#endif
	.unlocked_ioctl = isp_channel_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = isp_channel_ioctl,
#endif
	.open = isp_channel_open,
	.release = isp_channel_release,
};

/* Character device */
static struct class *isp_channel_class;
static int isp_channel_major = -1;

/**
 * @brief Registers and initializes the ISP channel driver.
 *
 * This function performs the following operations:
 * - Allocates memory for the @ref isp_channel_drv structure with space for
 *   the specified maximum number of ISP channels using @ref kzalloc().
 * - Initializes the driver structure fields, including setting the platform
 *   device TO @a ndev and the maximum number of channels to @a max_isp_channels.
 * - Initializes the driver mutex lock using @ref mutex_init().
 * - Acquires the global channel driver lock by invoking @ref mutex_lock().
 * - Checks if a channel driver is already registered.
 *   If a driver is already registered, it releases the lock and frees the allocated
 *   memory with @ref kfree().
 * - Sets the global channel driver reference to the newly allocated driver and
 *   releases the global lock using @ref mutex_unlock.
 * - Validates the ISP channel major number is not negative.
 * - Iterates over the number of channels and creates device nodes for each
 *   channel using @ref device_create().
 *
 * @param[in]  ndev              Pointer to the @ref platform_device structure representing the
 *                               platform device.
 *                               Valid Value: non-NULL.
 * @param[in]  max_isp_channels  Maximum number of ISP channels to support.
 *                               Valid Range: [0 .. UINT32_MAX].
 *
 * @retval  0                       Successfully registered and initialized the ISP channel driver.
 * @retval -ENOMEM                  If memory allocation for the driver structure fails,
 *                                  as indicated by @ref kzalloc().
 * @retval -EBUSY                   If an ISP channel driver is already registered.
 * @retval -EINVAL                  If the ISP channel major number is invalid, as checked before
 *                                  device creation.
 */
int isp_channel_drv_register(
	struct platform_device *ndev,
	unsigned int max_isp_channels)
{
	struct isp_channel_drv *chan_drv;
	unsigned int i;
	int err = 0;
	struct device *dev;

	if (unlikely(ndev == NULL))
		return -ENOMEM;

	chan_drv = kzalloc(offsetof(struct isp_channel_drv,
			channels[max_isp_channels]), GFP_KERNEL);
	if (unlikely(chan_drv == NULL))
		return -ENOMEM;

	chan_drv->dev  = NULL;
	chan_drv->ndev = NULL;
	chan_drv->isp_capture_pdev = ndev;
	chan_drv->num_channels = max_isp_channels;
	mutex_init(&chan_drv->lock);

	mutex_lock(&chdrv_lock);
	if (chdrv_ != NULL) {
		dev_warn(chan_drv->dev, "%s: dev is busy\n", __func__);
		mutex_unlock(&chdrv_lock);
		err = -EBUSY;
		goto error;
	}
	chdrv_ = chan_drv;
	mutex_unlock(&chdrv_lock);

	if (isp_channel_major < 0) {
		pr_err("%s: Invalid major number for ISP channel\n", __func__);
		err = -EINVAL;
		goto error;
	}

	for (i = 0; i < chan_drv->num_channels; i++) {
		dev_t devt = MKDEV(isp_channel_major, i);
		dev = device_create(isp_channel_class, &chan_drv->isp_capture_pdev->dev, devt, NULL,
				"capture-isp-channel%u", i);
		if (IS_ERR(dev)) {
			pr_err("%s: Failed to create device\n", __func__);
			err = PTR_ERR(dev);
			goto error_destroy;
		}
	}

	return 0;

error_destroy:
	while (i--) {
		dev_t devt = MKDEV(isp_channel_major, i);

		device_destroy(isp_channel_class, devt);
	}

error:
	mutex_lock(&chdrv_lock);
	chdrv_ = NULL;
	mutex_unlock(&chdrv_lock);
	kfree(chan_drv);
	return err;
}
EXPORT_SYMBOL(isp_channel_drv_register);

/**
 * @brief Registers file operations for the ISP channel driver.
 *
 * This function performs the following operations:
 * - Retrieves the global ISP channel driver instance.
 * - Checks if the channel driver is initialized by verifying the global driver reference.
 * - Acquires the global channel driver lock using @ref mutex_lock().
 * - If the driver operations are not set, assigns the provided operations.
 * - If the operations are already registered, logs a debug message using
 *   @ref dev_dbg().
 * - Releases the global channel driver lock using @ref mutex_unlock().
 * - Returns a status code based on the operation outcome.
 *
 * @param[in]  ops  Pointer to the @ref isp_channel_drv_ops structure containing the
 *                  file operations to be registered.
 *                  Valid Value: non-NULL.
 *
 * @retval  0                      Successfully registered the file operations.
 * @retval -EPROBE_DEFER           The ISP channel driver is not yet initialized,
 *                                 as indicated by @ref chdrv_ being NULL.
 */
int isp_channel_drv_fops_register(
	const struct isp_channel_drv_ops *ops)
{
	int err = 0;
	struct isp_channel_drv *chan_drv;

	chan_drv = chdrv_;
	if (chan_drv == NULL) {
		err = -EPROBE_DEFER;
		goto error;
	}

	mutex_lock(&chdrv_lock);
	if (chan_drv->ops == NULL)
		chan_drv->ops = ops;
	else
		dev_dbg(chan_drv->dev, "isp fops function table already registered\n");
	mutex_unlock(&chdrv_lock);

	return 0;

error:
	return err;
}
EXPORT_SYMBOL(isp_channel_drv_fops_register);

/**
 * @brief Unregisters the ISP channel driver and performs cleanup.
 *
 * This function performs the following operations:
 * - Acquires the global channel driver lock using @ref mutex_lock().
 * - Retrieves the current ISP channel driver instance from the global reference.
 * - Clears the global channel driver reference to indicate that it is no longer registered.
 * - Validates that the provided device matches the driver's device.
 * - Releases the global channel driver lock using @ref mutex_unlock().
 * - Checks if the ISP channel major number is valid. If invalid, logs an error using
 *   @ref pr_err() and exits.
 * - Iterates over all registered channels and destroys each device node using
 *   @ref device_destroy().
 * - Frees the memory allocated for the ISP channel driver using @ref kfree().
 *
 * @param[in] dev  Pointer to the @ref device structure representing the device to unregister.
 *                 Valid Value: non-NULL.
 */
void isp_channel_drv_unregister(
	struct device *dev)
{
	struct isp_channel_drv *chan_drv;
	unsigned int i;

	mutex_lock(&chdrv_lock);
	chan_drv = chdrv_;
	chdrv_ = NULL;

	if (chan_drv->dev != dev)
		dev_warn(chan_drv->dev, "%s: dev does not match\n", __func__);

	mutex_unlock(&chdrv_lock);

	if (isp_channel_major < 0) {
		pr_err("%s: Invalid major number for ISP channel\n", __func__);
		return;
	}

	for (i = 0; i < chan_drv->num_channels; i++) {
		dev_t devt = MKDEV(isp_channel_major, i);

		device_destroy(isp_channel_class, devt);
	}

	kfree(chan_drv);
}
EXPORT_SYMBOL(isp_channel_drv_unregister);

/**
 * @brief Initializes and registers the ISP channel driver.
 *
 * This function performs the following operations:
 * - Creates the ISP channel device class using @ref class_create().
 * - Registers a character device for the ISP channel with @ref register_chrdev().
 * - If registration fails, destroys the created device class using
 *   @ref class_destroy().
 * - Returns status based on the operation outcomes.
 *
 * @retval 0      Successfully initialized and registered the ISP channel driver.
 * @retval (int)  Error returned by @ref PTR_ERR() if @ref class_create() fails or by
 *                @ref register_chrdev() if character device registration fails.
 */
int isp_channel_drv_init(void)
{
#if defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG) /* Linux v6.4 */
	isp_channel_class = class_create("capture-isp-channel");
#else
	isp_channel_class = class_create(THIS_MODULE, "capture-isp-channel");
#endif
	if (IS_ERR(isp_channel_class))
		return PTR_ERR(isp_channel_class);

	isp_channel_major = register_chrdev(0, "capture-isp-channel",
						&isp_channel_fops);
	if (isp_channel_major < 0) {
		class_destroy(isp_channel_class);
		return isp_channel_major;
	}

	return 0;
}
EXPORT_SYMBOL(isp_channel_drv_init);

/**
 * @brief Cleans up and unregisters the ISP channel driver.
 *
 * This function performs the following operations:
 * - Calls @ref unregister_chrdev() to unregister the character device associated
 *   with the ISP channel driver.
 * - Calls @ref class_destroy() to destroy the device class for ISP channels.
 */
void isp_channel_drv_exit(void)
{
	unregister_chrdev(isp_channel_major, "capture-isp-channel");
	class_destroy(isp_channel_class);
}
EXPORT_SYMBOL(isp_channel_drv_exit);
