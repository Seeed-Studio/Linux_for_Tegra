// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include <linux/bitmap.h>
#include <linux/cdev.h>
#include <linux/dcache.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <soc/tegra/ivc_ext.h>
#include <linux/tegra-ivc-bus.h>
#include <soc/tegra/ivc-priv.h>
#include <linux/wait.h>
#include <asm/ioctls.h>
#include <asm/uaccess.h>

#define CCIOGNFRAMES _IOR('c', 1, int)
#define CCIOGNBYTES _IOR('c', 2, int)

struct tegra_camchar_data {
	struct cdev cdev;
	struct tegra_ivc_channel *ch;
	struct mutex io_lock;
	wait_queue_head_t waitq;
	bool is_open;
	bool is_established;
};

#define DEVICE_COUNT (128)

static DECLARE_BITMAP(tegra_camchar_minor_map, DEVICE_COUNT);
static DEFINE_SPINLOCK(tegra_camchar_lock);
static dev_t tegra_camchar_major_number;
static struct class *tegra_camchar_class;

/**
 * @brief Opens the Tegra camera character device.
 *
 * This function performs the following operations:
 * - Retrieves the device-specific data from the provided @ref inode structure
 *   using @ref container_of().
 * - Checks if the device is already open.
 * - Calls @ref tegra_ivc_channel_runtime_get() to obtain a runtime reference
 *   to the channel.
 * - Marks the device as open and the connection as not established.
 * - Assigns the channel to the file's private data.
 * - Calls @ref nonseekable_open() to complete the open operation.
 *
 * @param[in]  in      Pointer to the @ref inode structure representing the device.
 *                     Valid value: non-null.
 * @param[in, out]  f  Pointer to the @ref file structure for the device file.
 *                     Valid value: non-null.
 *
 * @retval EOK      Successfully opened the device.
 * @retval -EBUSY   The device is already open.
 * @retval (int)    Returned errors from @ref tegra_ivc_channel_runtime_get() or
 *                  @ref nonseekable_open().
 */
static int tegra_camchar_open(struct inode *in, struct file *f)
{
	struct tegra_camchar_data *data;
	int ret;

	data = container_of(in->i_cdev, struct tegra_camchar_data, cdev);
	if (data->is_open)
		return -EBUSY;

	ret = tegra_ivc_channel_runtime_get(data->ch);
	if (ret < 0)
		return ret;

	data->is_open = true;
	data->is_established = false;
	f->private_data = data->ch;

	return nonseekable_open(in, f);
}

/**
 * @brief Releases the Tegra camera character device.
 *
 * This function performs the following operations:
 * - Retrieves the channel from the file's private data.
 * - Obtains the device-specific data by calling @ref tegra_ivc_channel_get_drvdata().
 * - Calls @ref tegra_ivc_channel_runtime_put() to release the runtime reference
 *   to the channel.
 * - Marks the device as closed.
 *
 * @param[in] in  Pointer to the @ref inode structure representing the device.
 *                Valid value: non-null.
 * @param[in] fp  Pointer to the @ref file structure for the device file.
 *                Valid value: non-null.
 *
 * @retval EOK    Successfully released the device.
 */
static int tegra_camchar_release(struct inode *in, struct file *fp)
{
	struct tegra_ivc_channel *ch = fp->private_data;
	struct tegra_camchar_data *data;

	data = tegra_ivc_channel_get_drvdata(ch);
	tegra_ivc_channel_runtime_put(ch);
	data->is_open = false;

	return 0;
}

/**
 * @brief Polls the Tegra CamChar device for I/O readiness.
 *
 * This function performs the following operations:
 * - Retrieves the driver data associated with the file pointer using
 *   @ref tegra_ivc_channel_get_drvdata().
 * - Waits for events on the device's wait queue by calling @ref poll_wait().
 * - Locks the I/O lock using @ref mutex_lock() to ensure exclusive access.
 * - Checks if the device can be read by invoking @ref tegra_ivc_can_read().
 * - Checks if the device can be written by invoking @ref tegra_ivc_can_write().
 * - Unlocks the I/O lock using @ref mutex_unlock() after the checks.
 * - Sets the appropriate event flags in the return value based on readability and writability.
 *
 * @param[in] fp  Pointer to the @ref file structure.
 *                Valid value: non-null.
 * @param[in] pt  Pointer to the @ref poll_table_struct.
 *                Valid value: non-null.
 *
 * @retval 0                                                 No events are available for
 *                                                           reading or writing.
 * @retval (EPOLLIN | EPOLLRDNORM)                           Data is available for reading.
 * @retval (EPOLLOUT | EPOLLWRNORM)                          Space is available for writing.
 * @retval (EPOLLIN | EPOLLRDNORM | EPOLLOUT | EPOLLWRNORM)  Data is available for both
 *                                                           reading and writing.
 */
static __poll_t tegra_camchar_poll(struct file *fp, struct poll_table_struct *pt)
{
	__poll_t ret = 0;
	struct tegra_ivc_channel *ch = fp->private_data;
	struct tegra_camchar_data *dev_data = tegra_ivc_channel_get_drvdata(ch);

	poll_wait(fp, &dev_data->waitq, pt);

	mutex_lock(&dev_data->io_lock);
	if (tegra_ivc_can_read(&ch->ivc))
		ret |= (EPOLLIN | EPOLLRDNORM);
	if (tegra_ivc_can_write(&ch->ivc))
		ret |= (EPOLLOUT | EPOLLWRNORM);
	mutex_unlock(&dev_data->io_lock);

	return ret;
}

/**
 * @brief Reads data from the Tegra camera character device.
 *
 * This function performs the following operations:
 * - Retrieves the channel from the file's private data.
 * - Obtains the device-specific data by calling @ref tegra_ivc_channel_get_drvdata().
 * - Checks if the channel is ready by verifying the @ref tegra_ivc_channel.is_ready flag.
 * - Limits the read length to the minimum of the requested length and the channel's
 *   frame size using @ref min_t().
 * - If the adjusted length is zero, returns immediately with no data to read.
 * - Attempts to acquire the I/O lock interruptibly by calling @ref mutex_lock_interruptible().
 * - Prepares to wait on the device's wait queue by calling @ref prepare_to_wait().
 * - Calls @ref tegra_ivc_read() to perform the actual read operation.
 * - Releases the I/O lock using @ref mutex_unlock().
 * - Exit read operation if @ref tegra_ivc_read() returns @ref -ENOMEM and:
 *   - A signal is pending based on @ref signal_pending().
 *   - If the file is opened with @ref O_NONBLOCK flag enabled.
 * - Otherwise, retries the read operation.
 * - Cleans up the wait by calling @ref finish_wait().
 *
 * @param[in]        fp      Pointer to the @ref file structure.
 *                           Valid value: non-null.
 * @param[out]       buffer  User-space buffer to store the read data.
 *                           Valid value: non-null.
 * @param[in]        len     Number of bytes to read.
 *                           Valid range: [0 .. channel's frame size].
 * @param[in, out]   offset  File position offset.
 *                           Valid value: non-null.
 *
 * @retval -EIO        If the channel is not ready.
 * @retval 0           If the adjusted read length is zero.
 * @retval -EINTR      If the read operation was interrupted by a signal,
 *                     as determined by @ref signal_pending().
 * @retval -EAGAIN     If the file is opened with @ref O_NONBLOCK and no data is available,
 *                     as determined by @ref tegra_ivc_read().
 * @retval (ssize_t)   If postive, number of bytes successfully read. If negative, error
 *                     codes returned from @ref tegra_ivc_read() or
 *                     @ref mutex_lock_interruptible().
 */
static ssize_t tegra_camchar_read(struct file *fp, char __user *buffer, size_t len,
					loff_t *offset)
{
	struct tegra_ivc_channel *ch = fp->private_data;
	struct tegra_camchar_data *dev_data = tegra_ivc_channel_get_drvdata(ch);
	DEFINE_WAIT(wait);
	ssize_t ret;

	if (!ch->is_ready) {
		dev_warn(&ch->dev, "%s: dev is not ready!\n", __func__);
		return -EIO;
	}

	len = min_t(size_t, len, ch->ivc.frame_size);
	if (len == 0)
		return 0;

	do {
		ret = mutex_lock_interruptible(&dev_data->io_lock);
		if (ret)
			break;
		prepare_to_wait(&dev_data->waitq, &wait, TASK_INTERRUPTIBLE);

		ret = tegra_ivc_read(&ch->ivc, buffer, NULL, len);
		mutex_unlock(&dev_data->io_lock);

		if (ret != -ENOMEM)
			;
		else if (signal_pending(current))
			ret = -EINTR;
		else if (fp->f_flags & O_NONBLOCK)
			ret = -EAGAIN;
		else
			schedule();

		finish_wait(&dev_data->waitq, &wait);

	} while (ret == -ENOMEM);

	return ret;
}

/**
 * @brief Writes data to the Tegra CamChar device.
 *
 * This function performs the following operations:
 * - Retrieves the channel from the file's private data.
 * - Obtains the device-specific data by calling
 *   @ref tegra_ivc_channel_get_drvdata().
 * - Defines a wait queue using @ref DEFINE_WAIT().
 * - Checks if the channel is ready.
 * - Limits the write length to the minimum of the requested length and the
 *   channel's frame size using @ref min_t().
 * - If the adjusted length is zero, returns 0 indicating no data to write.
 * - Enters a loop to attempt the write operation:
 *   - Acquires the I/O lock interruptibly by calling
 *     @ref mutex_lock_interruptible().
 *   - If the lock acquisition is interrupted, breaks the loop.
 *   - Prepares to wait on the device's wait queue by calling
 *     @ref prepare_to_wait().
 *   - Calls @ref tegra_ivc_write() to perform the actual write operation.
 *   - Releases the I/O lock using @ref mutex_unlock().
 *   - If the write operation is successful, marks the connection as established.
 *   - Handles specific error conditions:
 *     - If @ref tegra_ivc_write() returns @ref -ENOMEM or @ref ECONNRESET,
 *       and a signal is pending as determined by @ref signal_pending().
 *     - If the file is opened with @c O_NONBLOCK and no data can be written.
 *     - Otherwise, calls @ref schedule() to retry the write operation.
 * - Cleans up the wait by calling @ref finish_wait().
 * - If a connection reset occurs and the connection was established, breaks
 *   the loop.
 *
 * @param[in]        fp      Pointer to the @ref file structure.
 *                           Valid Value: non-NULL.
 * @param[in]        buffer  User-space buffer containing the data to write.
 *                           Valid Value: non-NULL.
 * @param[in]        len     Number of bytes to write.
 *                           Valid Range: [0 .. channel's frame size].
 * @param[in, out]   offset  File position offset.
 *                           Valid Value: non-NULL.
 *
 * @retval -EIO        If the channel is not ready.
 * @retval 0           If the adjusted write length is zero.
 * @retval -EINTR      If the write operation was interrupted by a signal,
 *                     as determined by @ref mutex_lock_interruptible()
 *                     or @ref signal_pending().
 * @retval -EAGAIN     If the file is opened with @ref O_NONBLOCK and no data
 *                     can be written, as determined by @ref tegra_ivc_write().
 * @retval (ssize_t)   If positive, number of bytes successfully written. If negative,
 *                     error codes returned from @ref tegra_ivc_write(),
 *                     @ref mutex_lock_interruptible(), or other internal
 *                     mechanisms such as @ref schedule() or @ref finish_wait().
 */
static ssize_t tegra_camchar_write(struct file *fp, const char __user *buffer,
					size_t len, loff_t *offset)
{
	struct tegra_ivc_channel *ch = fp->private_data;
	struct tegra_camchar_data *dev_data = tegra_ivc_channel_get_drvdata(ch);
	DEFINE_WAIT(wait);
	ssize_t ret;

	if (!ch->is_ready) {
		dev_warn(&ch->dev, "%s: dev is not ready!\n", __func__);
		return -EIO;
	}

	len = min_t(size_t, len, ch->ivc.frame_size);
	if (len == 0)
		return 0;

	do {
		ret = mutex_lock_interruptible(&dev_data->io_lock);
		if (ret)
			break;

		prepare_to_wait(&dev_data->waitq, &wait, TASK_INTERRUPTIBLE);
		ret = tegra_ivc_write(&ch->ivc, buffer, NULL, len);
		mutex_unlock(&dev_data->io_lock);

		if (ret > 0)
			dev_data->is_established = true;

		if (ret != -ENOMEM && ret != ECONNRESET)
			;
		else if (ret == ECONNRESET && dev_data->is_established)
			;
		else if (signal_pending(current))
			ret = -EINTR;
		else if (fp->f_flags & O_NONBLOCK)
			ret = -EAGAIN;
		else
			schedule();

		finish_wait(&dev_data->waitq, &wait);

		if (ret == ECONNRESET && dev_data->is_established)
			break;

	} while (ret == -ENOMEM || ret == -ECONNRESET);

	return ret;
}

/**
 * @brief Handles ioctl commands for the Tegra camera character device.
 *
 * This function performs the following operations:
 * - Retrieves the channel from the file's private data.
 * - Obtains the device-specific data by calling @ref tegra_ivc_channel_get_drvdata().
 * - Acquires the I/O lock by calling @ref mutex_lock().
 * - Processes the ioctl command @a cmd using a switch statement:
 *   - For the @ref FIONREAD command:
 *     - Checks if the device can be read by invoking @ref tegra_ivc_can_read().
 *     - Copies channel frame size to user space by calling @ref put_user().
 *   - For the @ref CCIOGNFRAMES command:
 *     - Copies the number of frames in the channel to user space by calling @ref put_user().
 *   - For the @ref CCIOGNBYTES command:
 *     - Copies the channel's frame size to user space by calling @ref put_user().
 * - Releases the I/O lock by calling @ref mutex_unlock().
 *
 * @param[in]        fp      Pointer to the @ref file structure.
 *                           Valid value: non-null.
 * @param[in]        cmd     Ioctl command number.
 *                           Valid value: Supported ioctl commands such as
 *                           @ref FIONREAD, @ref CCIOGNFRAMES, and @ref CCIOGNBYTES.
 * @param[in]        arg     User-space pointer where the ioctl result will be stored.
 *                           Valid value: Valid user-space address.
 *
 * @retval 0          Successfully handled the @c FIONREAD command and data is copied
 *                    to user space.
 * @retval -ENOTTY    The ioctl command @a cmd is not supported.
 * @retval (long)     Error codes returned from @ref mutex_lock() or @ref put_user().
 */
static long tegra_camchar_ioctl(struct file *fp, unsigned int cmd,
				unsigned long arg)
{
	struct tegra_ivc_channel *ch = fp->private_data;
	struct tegra_camchar_data *dev_data = tegra_ivc_channel_get_drvdata(ch);
	long ret;
	int val = 0;

	mutex_lock(&dev_data->io_lock);

	switch (cmd) {
	/* generic serial port ioctls */
	case FIONREAD:
		ret = 0;
		if (tegra_ivc_can_read(&ch->ivc))
			val = ch->ivc.frame_size;
		ret = put_user(val, (int __user *)arg);
		break;
	/* ioctls specific to this device */
	case CCIOGNFRAMES:
		val = ch->ivc.num_frames;
		ret = put_user(val, (int __user *)arg);
		break;
	case CCIOGNBYTES:
		val = ch->ivc.frame_size;
		ret = put_user(val, (int __user *)arg);
		break;

	default:
		ret = -ENOTTY;
	}

	mutex_unlock(&dev_data->io_lock);
	return ret;
}

static const struct file_operations tegra_camchar_fops = {
	.open = tegra_camchar_open,
	.poll = tegra_camchar_poll,
	.read = tegra_camchar_read,
	.write = tegra_camchar_write,
	.release = tegra_camchar_release,
	.unlocked_ioctl = tegra_camchar_ioctl,
	.compat_ioctl = tegra_camchar_ioctl,
#if defined(NV_NO_LLSEEK_PRESENT)
	.llseek = no_llseek,
#endif
};

/**
 * @brief Initializes the Tegra camera character device driver.
 *
 * This function performs the following operations:
 * - Allocates a range of character device numbers by calling
 *   @ref alloc_chrdev_region().
 * - Extracts the major number from the allocated device number using @ref MAJOR().
 * - Creates a device class for the character device by calling
 *   @ref class_create() or @ref class_create().
 * - Checks if class creation was successful. If it fails:
 *   - Unregisters the allocated device numbers by calling
 *     @ref unregister_chrdev_region().
 * - Registers the IVC driver by calling @ref tegra_ivc_driver_register().
 * - Checks if driver registration was successful. If it fails:
 *   - Destroys the created class using @ref class_destroy().
 *   - Unregisters the allocated device numbers by calling
 *     @ref unregister_chrdev_region().
 * - Logs an informational message indicating successful loading of the driver.
 *
 * @param[in]  drv  Pointer to the @ref tegra_ivc_driver structure.
 *                  Valid value: non-null.
 *
 * @retval 0          Successfully initialized the device driver.
 * @retval (int)      Error returned from @ref alloc_chrdev_region(), @ref class_create(),
 *                    @ref PTR_ERR(), or @ref tegra_ivc_driver_register().
 */
static int __init tegra_camchar_init(struct tegra_ivc_driver *drv)
{
	int ret;
	dev_t start;

	ret = alloc_chrdev_region(&start, 0, DEVICE_COUNT, "camchar");
	if (ret) {
		pr_alert("camchar: failed to allocate device numbers\n");
		return ret;
	}
	tegra_camchar_major_number = MAJOR(start);

#if defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG) /* Linux v6.4 */
	tegra_camchar_class = class_create("camchar_class");
#else
	tegra_camchar_class = class_create(THIS_MODULE, "camchar_class");
#endif
	if (IS_ERR(tegra_camchar_class)) {
		pr_alert("camchar: failed to create class\n");
		ret = PTR_ERR(tegra_camchar_class);
		goto init_err_class;
	}

	ret = tegra_ivc_driver_register(drv);
	if (ret) {
		pr_alert("camchar: ivc driver registration failed\n");
		goto init_err_ivc;
	}

	pr_info("camchar: rtcpu character device driver loaded\n");
	return 0;

init_err_ivc:
	class_destroy(tegra_camchar_class);
init_err_class:
	unregister_chrdev_region(start, DEVICE_COUNT);
	return ret;
}

/**
 * @brief Exits and cleans up the Tegra camera character device driver.
 *
 * This function performs the following operations:
 * - Creates a device number using @ref MKDEV() with the major number and minor number 0.
 * - Unregisters the IVC driver by calling @ref tegra_ivc_driver_unregister().
 * - Destroys the device class by calling @ref class_destroy().
 * - Unregisters the allocated character device numbers by calling
 *   @ref unregister_chrdev_region().
 * - Resets the major number to 0.
 * - Logs an informational message indicating successful unloading of the driver.
 *
 * @param[in]  drv  Pointer to the @ref tegra_ivc_driver structure.
 *                 Valid value: non-null.
 */
static void __exit tegra_camchar_exit(struct tegra_ivc_driver *drv)
{
	dev_t num = MKDEV(tegra_camchar_major_number, 0);

	tegra_ivc_driver_unregister(drv);
	class_destroy(tegra_camchar_class);
	unregister_chrdev_region(num, DEVICE_COUNT);
	tegra_camchar_major_number = 0;

	pr_info("camchar: unloaded rtcpu character device driver\n");
}

/**
 * @brief Notifies the Tegra camera character device of an event.
 *
 * This function performs the following operations:
 * - Obtains the device-specific data by calling @ref tegra_ivc_channel_get_drvdata().
 * - Wakes up any processes waiting on the device's wait queue by calling
 *   @ref wake_up_interruptible().
 *
 * @param[in]  ch  Pointer to the @ref tegra_ivc_channel structure.
 *                 Valid value: non-null.
 */
static void tegra_camchar_notify(struct tegra_ivc_channel *ch)
{
	struct tegra_camchar_data *dev_data = tegra_ivc_channel_get_drvdata(ch);

	wake_up_interruptible(&dev_data->waitq);
}

/**
 * @brief Allocates and returns the first available minor number for the CamChar device.
 *
 * This function performs the following operations:
 * - Acquires the spinlock by calling @ref spin_lock().
 * - Finds the first available minor number by calling @ref find_first_zero_bit().
 * - If an available minor number is found:
 *   - Marks it as used by calling @ref set_bit().
 * - Releases the spinlock by calling @ref spin_unlock().
 *
 * @retval (int)     An available minor number, determined by @ref find_first_zero_bit().
 * @retval -ENODEV   If no available minor number was found.
 */
static int tegra_camchar_get_minor(void)
{
	int minor;

	spin_lock(&tegra_camchar_lock);

	minor = find_first_zero_bit(tegra_camchar_minor_map, DEVICE_COUNT);
	if (minor < DEVICE_COUNT)
		set_bit(minor, tegra_camchar_minor_map);
	else
		minor = -ENODEV;

	spin_unlock(&tegra_camchar_lock);

	return minor;
}

/**
 * @brief Releases a minor number for the Tegra camera character device.
 *
 * This function performs the following operations:
 * - Acquires the spinlock by calling @ref spin_lock().
 * - Checks if the provided minor number is within the valid range.
 * - If valid, clears the corresponding bit in @ref tegra_camchar_minor_map
 *   using @ref clear_bit().
 * - Releases the spinlock by calling @ref spin_unlock().
 *
 * @param[in]  minor   Minor number to release.
 *                     Valid range: [0 .. @ref DEVICE_COUNT - 1].
 */
static void tegra_camchar_put_minor(unsigned minor)
{
	spin_lock(&tegra_camchar_lock);

	if (minor < DEVICE_COUNT)
		clear_bit(minor, tegra_camchar_minor_map);

	spin_unlock(&tegra_camchar_lock);
}

/**
 * @brief Probes and initializes the Tegra CamChar device.
 *
 * This function performs the following operations:
 * - Retrieves the device name from the device tree by invoking @ref of_device_get_match_data()
 *   with the provided @ref ch.dev.
 *   - If device name is NULL, read string from device tree property "nvidia,devname" using
 *     @ref of_property_read_string().
 * - Allocates memory for device data using @ref devm_kzalloc().
 * - Initializes the character device structure using @ref cdev_init().
 * - Sets the owner of the character device to @ref THIS_MODULE.
 * - Initializes the wait queue head using @ref init_waitqueue_head().
 * - Initializes the I/O mutex using @ref mutex_init().
 * - Associates the device data with the provided @ref tegra_ivc_channel using
 *   @ref tegra_ivc_channel_set_drvdata().
 * - Allocates a minor number retrieved using @ref tegra_camchar_get_minor().
 * - Creates a device number using @ref MKDEV().
 * - Adds the character device to the system using @ref cdev_add().
 *   - In case of failure, release minor number using @ref tegra_camchar_put_minor().
 * - Creates the device in sysfs using @ref device_create().
 *   - In case of failure, release minor number using @ref tegra_camchar_put_minor().
 *
 * @param[in]  ch  Pointer to the @ref tegra_ivc_channel structure.
 *                 Valid value: non-null.
 *
 * @retval EOK        Successfully probed and initialized the device.
 * @retval -ENOMEM    @ref devm_kzalloc() failed.
 * @retval -ENODEV    No available minor number was found using @ref tegra_camchar_get_minor().
 * @retval (int)      Other error codes indicating failure during device initialization originating
 *                    from @ref cdev_add(), @ref device_create, or @ref of_property_read_string().
 */
static int tegra_camchar_probe(struct tegra_ivc_channel *ch)
{
	const char *devname;
	struct tegra_camchar_data *data;
	int ret, minor;
	dev_t num;
	struct device *dummy;

	devname = of_device_get_match_data(&ch->dev);
	if (devname == NULL) {
		ret = of_property_read_string(ch->dev.of_node,
					"nvidia,devname", &devname);
		if (ret != 0)
			return ret;
	}

	dev_dbg(&ch->dev, "probing /dev/%s", devname);

	data = devm_kzalloc(&ch->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->ch = ch;
	cdev_init(&data->cdev, &tegra_camchar_fops);
	data->cdev.owner = THIS_MODULE;
	init_waitqueue_head(&data->waitq);
	mutex_init(&data->io_lock);

	tegra_ivc_channel_set_drvdata(ch, data);

	minor = tegra_camchar_get_minor();
	if (minor < 0)
		return minor;

	num = MKDEV(tegra_camchar_major_number, minor);
	ret = cdev_add(&data->cdev, num, 1);
	if (ret) {
		dev_warn(&ch->dev, "cannot add /dev/%s\n", devname);
		tegra_camchar_put_minor(minor);
		return ret;
	}

	dummy = device_create(tegra_camchar_class, &ch->dev, num, NULL,
			"%s", devname);
	if (IS_ERR(dummy)) {
		dev_err(&ch->dev, "cannot create /dev/%s\n", devname);
		tegra_camchar_put_minor(minor);
		return PTR_ERR(dummy);
	}

	return ret;
}

/**
 * @brief Removes the Tegra CamChar device.
 *
 * This function performs the following operations:
 * - Retrieves the device-specific data by calling
 *   @ref tegra_ivc_channel_get_drvdata().
 * - Obtains the device number from the device data.
 * - Destroys the device by calling @ref device_destroy().
 * - Deletes the character device by calling @ref cdev_del().
 * - Releases the allocated minor number by calling
 *   @ref tegra_camchar_put_minor().
 *
 * @param[in]  ch  Pointer to the @ref tegra_ivc_channel structure.
 *                 Valid Value: non-NULL.
 */
static void tegra_camchar_remove(struct tegra_ivc_channel *ch)
{
	struct tegra_camchar_data *data = tegra_ivc_channel_get_drvdata(ch);
	dev_t num = data->cdev.dev;

	device_destroy(tegra_camchar_class, num);
	cdev_del(&data->cdev);
	tegra_camchar_put_minor(MINOR(num));
}

static const struct tegra_ivc_channel_ops tegra_ivc_channel_chardev_ops = {
	.probe	= tegra_camchar_probe,
	.remove	= tegra_camchar_remove,
	.notify	= tegra_camchar_notify,
};

static const struct of_device_id camchar_of_match[] = {
	{ .compatible = "nvidia,tegra-ivc-cdev" },
	{ .compatible = "nvidia,tegra186-camera-ivc-protocol-echo",
		.data = (void *)"camchar-echo", },
	{ .compatible = "nvidia,tegra186-camera-ivc-protocol-dbg",
		.data = (void *)"camchar-dbg", },
	{ },
};
MODULE_DEVICE_TABLE(of, camchar_of_match);

static struct tegra_ivc_driver camchar_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.bus	= &tegra_ivc_bus_type,
		.name	= "tegra-ivc-cdev",
		.of_match_table = camchar_of_match,
	},
	.dev_type	= &tegra_ivc_channel_type,
	.ops.channel	= &tegra_ivc_channel_chardev_ops,
};

module_driver(camchar_driver, tegra_camchar_init, tegra_camchar_exit);
MODULE_AUTHOR("Jan Solanti <jsolanti@nvidia.com>");
MODULE_DESCRIPTION("The character device for ivc-bus");
MODULE_LICENSE("GPL v2");
