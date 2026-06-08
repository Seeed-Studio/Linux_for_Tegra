// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <nvidia/conftest.h>

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h> /* printk() */
#include <linux/slab.h>   /* kmalloc() */
#include <linux/errno.h> /* error codes */
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/mtd/mtd.h>
#include <linux/completion.h>
#include <linux/mtd/partitions.h>
#include <soc/tegra/ivc-priv.h>
#include <soc/tegra/ivc_ext.h>
#include <soc/tegra/virt/hv-ivc.h>
#include <linux/version.h>
#include <soc/tegra/fuse.h>
#include <tegra_virt_storage_spec.h>
#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
#include <linux/tegra-hsierrrptinj.h>
#endif
#define ECC_REQUEST_FAILED		0xFFFFFFFF

#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
#define HSI_QSPI_REPORT_ID(inst)	(0x805B + (inst))
#define HSI_ERROR_MAGIC			0xDEADDEAD

static uint32_t total_instance_id;
#endif

#define DEFAULT_INIT_VCPU (0U)
#define IVC_TIMEOUT_MS (30000)

struct vmtd_dev {
	struct vs_config_info config;
	uint64_t size;                   /* Device size in bytes */
	uint32_t ivc_id;
	uint32_t ivm_id;
	struct tegra_hv_ivc_cookie *ivck;
	struct tegra_hv_ivm_cookie *ivmk;
	struct device *device;
	void *shared_buffer;
	struct mutex lock;
	struct completion msg_complete;
	void *cmd_frame;
	struct mtd_info mtd;
	bool is_setup;
	uint32_t schedulable_vcpu_number;
	struct work_struct init;
#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
	uint32_t epl_id;
	uint32_t epl_reporter_id;
	uint32_t instance_id;
#endif
	uint32_t ecc_failed_chunk_address;
};

#define IVC_RESET_RETRIES 30

static inline struct vmtd_dev *mtd_to_vmtd(struct mtd_info *mtd)
{
	return container_of(mtd, struct vmtd_dev, mtd);
}

/**
 * @defgroup vscd_mtd_irq_timer LinuxMtdVSCD::IRQ/Timer
 *
 * @ingroup vscd_mtd_irq_timer
 * @{
 */
/**
 * @brief Interrupt handler for IVC (Inter-VM Communication) channel
 *
 * This function serves as the interrupt service routine for the IVC channel.
 * When an IVC interrupt occurs, it:
 * 1. Signals completion of an IVC transaction by calling complete()
 * 2. Wakes up any threads waiting on IVC communication
 * 3. Enables further IVC communication to proceed
 *
 * The handler is essential for the asynchronous nature of IVC communication,
 * allowing the driver to efficiently handle command/response sequences without
 * busy waiting.
 *
 * @param[in] irq The interrupt number being handled
 * @param[in] data Pointer to the vmtd_dev structure (passed as void*)
 * @return IRQ_HANDLED indicating successful handling of the interrupt
 *
 * @pre
 * - IVC channel must be properly initialized
 * - Completion structure must be initialized
 * - IRQ must be properly registered with this handler
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: Yes
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Sync
 *   - Re-entrant: Yes
 * - API Group
 *   - Init: Yes
 *   - Runtime: Yes
 *   - De-Init: Yes
 */
static irqreturn_t ivc_irq_handler(int irq, void *data)
{
	struct vmtd_dev *vmtddev = (struct vmtd_dev *)data;

	complete(&vmtddev->msg_complete);
	return IRQ_HANDLED;
}
/**
 * @}
 */

static int vmtd_send_cmd(struct vmtd_dev *vmtddev, struct vs_request *vs_req, bool use_timeout)
{
	while (tegra_hv_ivc_channel_notified(vmtddev->ivck) != 0) {
		if (use_timeout) {
			if ((wait_for_completion_timeout(&vmtddev->msg_complete,
				msecs_to_jiffies(IVC_TIMEOUT_MS)) == 0)) {
				dev_err(vmtddev->device, "Request sending timeout - 1!\n");
				return -EIO;
			}
		} else {
			wait_for_completion(&vmtddev->msg_complete);
		}
	}

	while (!tegra_hv_ivc_can_write(vmtddev->ivck)) {
		if (use_timeout) {
			if ((wait_for_completion_timeout(&vmtddev->msg_complete,
			msecs_to_jiffies(IVC_TIMEOUT_MS)) == 0)) {
				dev_err(vmtddev->device, "Request sending timeout - 2!\n");
				return -EIO;
			}
		} else {
			wait_for_completion(&vmtddev->msg_complete);
		}
	}

	if (tegra_hv_ivc_write(vmtddev->ivck, vs_req,
		sizeof(struct vs_request)) != sizeof(struct vs_request)) {
		dev_err(vmtddev->device, "Request sending failed!\n");
		return -EIO;
	}

	return 0;
}

static int vmtd_get_resp(struct vmtd_dev *vmtddev, struct vs_request *vs_req, bool use_timeout)
{

	while (tegra_hv_ivc_channel_notified(vmtddev->ivck) != 0) {
		if (use_timeout) {
			if ((wait_for_completion_timeout(&vmtddev->msg_complete,
				msecs_to_jiffies(IVC_TIMEOUT_MS)) == 0)) {
				dev_err(vmtddev->device, "Response fetching timeout - 1!\n");
				return -EIO;
			}
		} else {
			wait_for_completion(&vmtddev->msg_complete);
		}
	}

	while (!tegra_hv_ivc_can_read(vmtddev->ivck)) {
		if (use_timeout) {
			if ((wait_for_completion_timeout(&vmtddev->msg_complete,
				msecs_to_jiffies(IVC_TIMEOUT_MS)) == 0)) {
				dev_err(vmtddev->device, "Response fetching timeout - 2!\n");
				return -EIO;
			}
		} else {
			wait_for_completion(&vmtddev->msg_complete);
		}
	}

	if (tegra_hv_ivc_read(vmtddev->ivck, vs_req,
		sizeof(struct vs_request)) != sizeof(struct vs_request)) {
		dev_err(vmtddev->device, "Response fetching failed!\n");
		return -EIO;
	}

	return 0;
}

static int vmtd_process_request(struct vmtd_dev *vmtddev,
	struct vs_request *vs_req, bool use_timeout)
{
#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
	uint32_t num_bytes;
	loff_t offset;
#else
	uint32_t num_bytes = vs_req->mtddev_req.mtd_req.size;
	loff_t offset = vs_req->mtddev_req.mtd_req.offset;
#endif
	int32_t ret = 0;

#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
	if (vs_req->req_id != HSI_ERROR_MAGIC) {
		num_bytes = vs_req->mtddev_req.mtd_req.size;
		offset = vs_req->mtddev_req.mtd_req.offset;
	}
#endif

	ret = vmtd_send_cmd(vmtddev, vs_req, use_timeout);
	if (ret != 0) {
		dev_err(vmtddev->device,
			"Sending %d failed!\n",
			vs_req->mtddev_req.req_op);
		goto fail;
	}

	vs_req = (struct vs_request *)vmtddev->cmd_frame;
	ret = vmtd_get_resp(vmtddev, vs_req, use_timeout);
	if (ret != 0) {
		dev_err(vmtddev->device,
			"fetching response failed!\n");
		goto fail;
	}

#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
	if (vs_req->req_id == HSI_ERROR_MAGIC) {
		if (vs_req->status != 0) {
			dev_err(vmtddev->device, "Response status for error injection failed!\n");
			ret = -EIO;
			goto fail;
		} else {
			return ret;
		}
	}
#endif

	if ((vs_req->status != 0) ||
		(vs_req->mtddev_resp.mtd_resp.status != 0)) {
		dev_err(vmtddev->device,
			"Response status for offset %llx size %x failed!\n",
			offset, num_bytes);
		ret = -EIO;
		goto fail;
	}

	if (vs_req->mtddev_resp.mtd_resp.size != num_bytes) {
		dev_err(vmtddev->device,
			"size mismatch for offset %llx size %x returned %x!\n",
			offset, num_bytes,
			vs_req->mtddev_resp.mtd_resp.size);
		ret = -EIO;
		goto fail;
	}

fail:
	return ret;
}

static int vmtd_get_configinfo(struct vmtd_dev *vmtddev,
	struct vs_config_info *config)
{
	struct vs_request *vs_req = (struct vs_request *)vmtddev->cmd_frame;

	/* This while loop exits as long as the remote endpoint cooperates. */
	while (!tegra_hv_ivc_can_read(vmtddev->ivck))
		wait_for_completion(&vmtddev->msg_complete);

	if (!tegra_hv_ivc_read(vmtddev->ivck, vs_req,
				sizeof(struct vs_request))) {
		dev_err(vmtddev->device, "config fetching failed!\n");
		return -EIO;
	}

	if (vs_req->status != 0) {
		dev_err(vmtddev->device, "Config fetch request failed!\n");
		return -EINVAL;
	}

	*config = vs_req->config_info;
	return 0;
}

/**
 * @defgroup vscd_mtd_request_handler LinuxMtdVSCD::Request Handler
 *
 * @ingroup vscd_mtd_request_handler
 * @{
 */
/**
 * @brief Reads data from the virtual MTD device
 *
 * This function reads data from the virtual MTD device by:
 * 1. Validating read boundaries against device size
 * 2. Breaking down large reads into smaller chunks based on max_read_bytes_per_io
 * 3. For each chunk:
 *    - Preparing VS_MTD_READ request
 *    - Sending request through IVC channel
 *    - Waiting for response from physical device
 *    - Copying data from shared buffer to user buffer
 * 4. Maintaining thread safety through mutex locking
 *
 * @param[in] mtd Pointer to MTD device information structure
 * @param[in] from Starting offset in the device to read from
 * @param[in] len Number of bytes to read
 * @param[out] retlen Pointer to store number of bytes actually read
 * @param[out] buf Buffer to store read data
 * @return 0 on success, negative errno on failure
 *
 * @pre
 * - Device must be initialized
 * - IVC channel must be operational
 * - Shared memory buffer must be mapped
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
static int vmtd_read(struct mtd_info *mtd, loff_t from, size_t len,
		size_t *retlen, u_char *buf)
{
	struct vmtd_dev *vmtddev = mtd_to_vmtd(mtd);
	struct vs_request *vs_req;
	size_t remaining_size = len;
	size_t read_size;
	u_char *buf_addr = buf;
	loff_t offset = from;
	int32_t ret = 0;

	dev_dbg(vmtddev->device, "%s from 0x%llx, len %zd\n",
		__func__, offset, remaining_size);

	if (((offset + remaining_size) < offset) ||
			((offset + remaining_size) > vmtddev->mtd.size)) {
		dev_err(vmtddev->device,
			"from %llx len %lx out of range!\n", offset,
			remaining_size);
		return -EPERM;
	}

	mutex_lock(&vmtddev->lock);
	while (remaining_size) {
		read_size = min_t(size_t, (size_t)vmtddev->config.mtd_config.max_read_bytes_per_io,
				remaining_size);
		vs_req = (struct vs_request *)vmtddev->cmd_frame;
		vs_req->type = VS_DATA_REQ;
		vs_req->mtddev_req.req_op = VS_MTD_READ;
		vs_req->mtddev_req.mtd_req.offset = offset;
		vs_req->mtddev_req.mtd_req.size = read_size;
		vs_req->mtddev_req.mtd_req.data_offset = 0;
		vs_req->req_id = 0;

		ret = vmtd_process_request(vmtddev, vs_req, true);
		if (ret != 0) {
			dev_err(vmtddev->device,
				"Read for offset %llx size %lx failed!\n",
				offset, read_size);
			goto fail;
		}

		memcpy(buf_addr, vmtddev->shared_buffer, read_size);
		buf_addr += read_size;
		offset += read_size;
		remaining_size -= read_size;
	}
	*retlen = len;

fail:
	mutex_unlock(&vmtddev->lock);
	return ret;
}

/**
 * @brief Writes data to the virtual MTD device
 *
 * This function writes data to the virtual MTD device by:
 * 1. Validating write boundaries against device size
 * 2. Breaking down large writes into smaller chunks based on max_write_bytes_per_io
 * 3. For each chunk:
 *    - Copying data from user buffer to shared memory
 *    - Preparing VS_MTD_WRITE request
 *    - Sending request through IVC channel
 *    - Waiting for write confirmation
 * 4. Maintaining thread safety through mutex locking
 * 5. Handling write failures and partial writes
 *
 * @param[in] mtd Pointer to MTD device information structure
 * @param[in] to Starting offset in the device to write to
 * @param[in] len Number of bytes to write
 * @param[out] retlen Pointer to store number of bytes actually written
 * @param[in] buf Buffer containing data to write
 * @return 0 on success, negative errno on failure
 *
 * @pre
 * - Device must be initialized
 * - IVC channel must be operational
 * - Shared memory buffer must be mapped
 * - Device must not be in read-only mode
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
static int vmtd_write(struct mtd_info *mtd, loff_t to, size_t len,
		size_t *retlen, const u_char *buf)
{
	struct vmtd_dev *vmtddev = mtd_to_vmtd(mtd);
	struct vs_request *vs_req;
	size_t remaining_size = len;
	size_t write_size;
	const u_char *buf_addr = buf;
	loff_t offset = to;
	int32_t ret = 0;

	dev_dbg(vmtddev->device, "%s from 0x%08x, len %zd\n",
		__func__, (u32)offset, remaining_size);

	if (((offset + remaining_size) < offset) ||
		((offset + remaining_size) > vmtddev->mtd.size)) {
		dev_err(vmtddev->device, "to %llx len %lx out of range!\n",
			offset, remaining_size);
		return -EPERM;
	}

	mutex_lock(&vmtddev->lock);
	while (remaining_size) {
		write_size = min_t(size_t,
			(size_t)vmtddev->config.mtd_config.max_write_bytes_per_io,
				remaining_size);
		vs_req = (struct vs_request *)vmtddev->cmd_frame;
		vs_req->type = VS_DATA_REQ;
		vs_req->mtddev_req.req_op = VS_MTD_WRITE;
		vs_req->mtddev_req.mtd_req.offset = offset;
		vs_req->mtddev_req.mtd_req.size = write_size;
		vs_req->mtddev_req.mtd_req.data_offset = 0;
		vs_req->req_id = 0;

		memcpy(vmtddev->shared_buffer, buf_addr, write_size);

		ret = vmtd_process_request(vmtddev, vs_req, true);
		if (ret != 0) {
			dev_err(vmtddev->device,
				"write for offset %llx size %lx failed!\n",
				offset, write_size);
			goto fail;
		}

		buf_addr += write_size;
		offset += write_size;
		remaining_size -= write_size;
	}
	*retlen = len;

fail:
	mutex_unlock(&vmtddev->lock);
	return ret;
}

/**
 * @brief Erases a region of the virtual MTD device
 *
 * This function erases a specified region of the virtual MTD device by:
 * 1. Validating erase boundaries against device size
 * 2. Preparing VS_MTD_ERASE request with:
 *    - Starting address (instr->addr)
 *    - Length of region to erase (instr->len)
 * 3. Sending single erase command through IVC channel
 * 4. Waiting for erase completion confirmation
 * 5. Maintaining thread safety through mutex locking
 * 6. Handling erase failures
 *
 * @param[in] mtd Pointer to MTD device information structure
 * @param[in] instr Erase instruction structure containing:
 *                  - Address to start erasing from
 *                  - Length of region to erase
 * @return 0 on success, negative errno on failure
 *
 * @pre
 * - Device must be initialized
 * - IVC channel must be operational
 * - Device must not be in read-only mode
 * - Erase region must be aligned to erase block size
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
static int vmtd_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct vmtd_dev *vmtddev = mtd_to_vmtd(mtd);
	struct vs_request *vs_req;
	int32_t ret = 0;

	dev_dbg(vmtddev->device, "%s from 0x%08x, len %llx\n",
		__func__, (u32)instr->addr, instr->len);

	if (((instr->addr + instr->len) < instr->addr) ||
			((instr->addr + instr->len) > vmtddev->mtd.size)) {
		dev_err(vmtddev->device, "addr %llx len %llx out of range!\n",
			instr->addr, instr->len);
		return -EPERM;
	}

	mutex_lock(&vmtddev->lock);
	vs_req = (struct vs_request *)vmtddev->cmd_frame;

	vs_req->type = VS_DATA_REQ;
	vs_req->mtddev_req.req_op = VS_MTD_ERASE;
	vs_req->mtddev_req.mtd_req.offset = instr->addr;
	vs_req->mtddev_req.mtd_req.size = instr->len;
	vs_req->mtddev_req.mtd_req.data_offset = 0;
	vs_req->req_id = 0;

	/* FIXME: Erase timeout is not needed as the User is expected to have timeout for operations
	 *  To have timeout for erase, there should be a max_erase_bytes attribute in vs_mtd_info
	 *  and the timeout should be tuned to match this attribute value.
	 */
	ret = vmtd_process_request(vmtddev, vs_req, false);
	if (ret != 0) {
		dev_err(vmtddev->device,
			"Erase for offset %llx size %llx failed!\n",
			instr->addr, instr->len);
		mutex_unlock(&vmtddev->lock);
		goto fail;
	}
	mutex_unlock(&vmtddev->lock);

fail:
	return ret;
}
/**
 * @}
 */


/**
 * @defgroup vscd_mtd_suspend_resume LinuxMtdVSCD::Suspend/Resume
 *
 * @ingroup vscd_mtd_suspend_resume
 * @{
 */

#ifdef CONFIG_PM_SLEEP

/**
 * @brief Suspends the virtual MTD device operations
 *
 * This function performs the following operations during system suspend:
 * 1. Checks if the device is properly set up
 * 2. Acquires the device mutex to prevent concurrent access
 * 3. Disables the IVC interrupt to prevent further communications
 * 4. Resets the IVC channel to ensure clean state during suspend
 *
 * The function ensures that all ongoing MTD operations are properly halted
 * and the communication channel with the hypervisor is safely suspended.
 *
 * @param[in] dev Pointer to the device structure
 * @return 0 on success
 *
 * @pre
 * - Device must be initialized
 * - System must be in suspend transition
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
static int tegra_virt_mtd_suspend(struct device *dev)
{
	struct vmtd_dev *vmtddev = dev_get_drvdata(dev);

	if (vmtddev->is_setup) {
		mutex_lock(&vmtddev->lock);
		disable_irq(vmtddev->ivck->irq);
	}
	return 0;
}

/**
 * @brief Resumes the virtual MTD device operations
 *
 * This function performs the following operations during system resume:
 * 1. Checks if the device was properly set up before suspend
 * 2. Re-enables the IVC interrupt to restore communication
 * 3. Releases the device mutex to allow MTD operations
 *
 * The function restores the device to operational state after system resume,
 * re-establishing the communication channel with the hypervisor and
 * allowing MTD operations to proceed.
 *
 * @param[in] dev Pointer to the device structure
 * @return 0 on success
 *
 * @pre
 * - Device must have been previously suspended
 * - System must be in resume transition
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
static int tegra_virt_mtd_resume(struct device *dev)
{
	struct vmtd_dev *vmtddev = dev_get_drvdata(dev);

	if (vmtddev->is_setup) {
		enable_irq(vmtddev->ivck->irq);
		mutex_unlock(&vmtddev->lock);
	}
	return 0;
}

static const struct dev_pm_ops tegra_hv_vmtd_pm_ops = {
	.suspend = tegra_virt_mtd_suspend,
	.resume = tegra_virt_mtd_resume,

};
#endif /* CONFIG_PM_SLEEP */

/**
 * @}
 */

static int vmtd_setup_device(struct vmtd_dev *vmtddev)
{
	mutex_init(&vmtddev->lock);

	vmtddev->mtd.name = "virt_mtd";
	vmtddev->mtd.type = MTD_NORFLASH;
	vmtddev->mtd.writesize = 1;

	/* Set device read-only if config response say so */
	if (!(vmtddev->config.mtd_config.req_ops_supported &
				VS_MTD_READ_ONLY_MASK)) {
		dev_info(vmtddev->device, "setting device read-only\n");
		vmtddev->mtd.flags = MTD_CAP_ROM;
	} else {
		vmtddev->mtd.flags = MTD_CAP_NORFLASH;
	}

	vmtddev->mtd.size = vmtddev->config.mtd_config.size;
	dev_info(vmtddev->device, "size %lld!\n",
		vmtddev->config.mtd_config.size);
	vmtddev->mtd._erase = vmtd_erase;
	vmtddev->mtd._read = vmtd_read;
	vmtddev->mtd._write = vmtd_write;
	vmtddev->mtd.erasesize = vmtddev->config.mtd_config.erase_size;

	if (vmtddev->ivmk->size <
		vmtddev->config.mtd_config.max_read_bytes_per_io) {
		dev_info(vmtddev->device,
			"Consider increasing mempool size to %d!\n",
			vmtddev->config.mtd_config.max_read_bytes_per_io);
		vmtddev->config.mtd_config.max_read_bytes_per_io =
			vmtddev->ivmk->size;
	}

	if (vmtddev->ivmk->size <
		vmtddev->config.mtd_config.max_write_bytes_per_io) {
		dev_info(vmtddev->device,
			"Consider increasing mempool size to %d!\n",
			vmtddev->config.mtd_config.max_write_bytes_per_io);
		vmtddev->config.mtd_config.max_write_bytes_per_io =
			vmtddev->ivmk->size;
	}

	vmtddev->mtd.dev.parent = vmtddev->device;
	vmtddev->mtd.writebufsize = 1;

	mtd_set_of_node(&vmtddev->mtd, vmtddev->device->of_node);

	return mtd_device_parse_register(&vmtddev->mtd, NULL, NULL,
			NULL, 0);
}

/**
 * @defgroup vscd_mtd_sysfs LinuxMtdVSCD::Sysfs
 *
 * @ingroup vscd_mtd_sysfs
 * @{
 */
/**
 * @brief Shows the physical device type of the virtual MTD device
 *
 * This function retrieves and displays the physical device type that underlies
 * the virtual MTD device. Currently, it checks if the device is a QSPI device
 * and returns the appropriate string representation. If the device type is not
 * recognized, it returns "unknown!".
 *
 * @param[in] dev Pointer to the device structure
 * @param[in] attr Pointer to device attribute structure
 * @param[out] buf Buffer to store the device type string
 * @return Number of characters written to the buffer
 *
 * @pre
 * - Device must be initialized
 * - Configuration must be properly set up
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Sync
 *   - Re-entrant: Yes
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
static ssize_t vmtd_phys_dev_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct vmtd_dev *vmtddev = dev_get_drvdata(dev);

	if (vmtddev->config.phys_dev == VSC_DEV_QSPI)
		return snprintf(buf, 16, "QSPI\n");

	return snprintf(buf, 16, "unknown!\n");
}
static DEVICE_ATTR(phys_dev, 0444, vmtd_phys_dev_show, NULL);

/**
 * @brief Shows the physical base address of the virtual MTD device
 *
 * This function retrieves and displays the physical base address of the underlying
 * MTD device in hexadecimal format. This address represents the starting memory
 * location of the physical flash device in the system's memory map.
 *
 * @param[in] dev Pointer to the device structure
 * @param[in] attr Pointer to device attribute structure
 * @param[out] buf Buffer to store the base address string
 * @return Number of characters written to the buffer
 *
 * @pre
 * - Device must be initialized
 * - Configuration must be properly set up
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Sync
 *   - Re-entrant: Yes
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
static ssize_t vmtd_phys_base_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct vmtd_dev *vmtddev = dev_get_drvdata(dev);

	return snprintf(buf, 16, "0x%llx\n", vmtddev->config.phys_base);
}
static DEVICE_ATTR(phys_base, 0444, vmtd_phys_base_show, NULL);

/**
 * @brief Shows the manufacturer ID of the MTD device
 *
 * This function retrieves and displays the manufacturer ID of the flash device
 * in hexadecimal format. The manufacturer ID is a unique identifier that
 * indicates the company that manufactured the flash memory chip.
 *
 * @param[in] dev Pointer to the device structure
 * @param[in] attr Pointer to device attribute structure
 * @param[out] buf Buffer to store the manufacturer ID string
 * @return Number of characters written to the buffer
 *
 * @pre
 * - Device must be initialized
 * - Configuration must be properly set up
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Sync
 *   - Re-entrant: Yes
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
static ssize_t manufacturer_id_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct vmtd_dev *vmtddev = dev_get_drvdata(dev);

	return sprintf(buf, "0x%x\n", vmtddev->config.mtd_config.manufacturer_id);
}
static DEVICE_ATTR_RO(manufacturer_id);

/**
 * @brief Shows the device ID of the MTD device
 *
 * This function retrieves and displays the device ID of the flash device
 * in hexadecimal format. The device ID is a unique identifier that
 * specifies the particular model or variant of the flash memory chip.
 *
 * @param[in] dev Pointer to the device structure
 * @param[in] attr Pointer to device attribute structure
 * @param[out] buf Buffer to store the device ID string
 * @return Number of characters written to the buffer
 *
 * @pre
 * - Device must be initialized
 * - Configuration must be properly set up
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Sync
 *   - Re-entrant: Yes
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
static ssize_t device_id_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct vmtd_dev *vmtddev = dev_get_drvdata(dev);

	return sprintf(buf, "0x%x\n", vmtddev->config.mtd_config.device_id);
}
static DEVICE_ATTR_RO(device_id);

/**
 * @brief Shows the QSPI device size in bytes
 *
 * This function retrieves and displays the total size of the QSPI flash device
 * in bytes. This represents the total storage capacity of the physical flash
 * memory device that is being virtualized.
 *
 * @param[in] dev Pointer to the device structure
 * @param[in] attr Pointer to device attribute structure
 * @param[out] buf Buffer to store the device size string
 * @return Number of characters written to the buffer
 *
 * @pre
 * - Device must be initialized
 * - Configuration must be properly set up
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Sync
 *   - Re-entrant: Yes
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
static ssize_t qspi_device_size_bytes_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct vmtd_dev *vmtddev = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", (unsigned int) vmtddev->config.mtd_config.qspi_device_size_bytes);
}
static DEVICE_ATTR_RO(qspi_device_size_bytes);

/**
 * @brief Shows the ECC (Error Correction Code) status of the MTD device
 *
 * This function performs an ECC status check and returns the current ECC state.
 * It sends a VS_MTD_ECC request to the physical device and interprets the response.
 * Possible status values are:
 * - ECC_NO_ERROR: No errors detected
 * - ECC_ONE_BIT_CORRECTED: Single-bit error detected and corrected
 * - ECC_TWO_BIT_ERROR: Double-bit error detected (uncorrectable)
 * - ECC_DISABLED: ECC functionality is disabled
 * - ECC_REQUEST_FAILED: Failed to get ECC status
 *
 * @param[in] dev Pointer to the device structure
 * @param[in] attr Pointer to device attribute structure
 * @param[out] buf Buffer to store the ECC status string
 * @return Number of characters written to the buffer
 *
 * @pre
 * - Device must be initialized
 * - IVC channel must be operational
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Sync
 *   - Re-entrant: Yes
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
static ssize_t ecc_status_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct vmtd_dev *vmtddev = dev_get_drvdata(dev);
	struct vs_request *vs_req;
	int32_t ret;
	struct vs_mtd_ecc_response ecc_response;

	vs_req = (struct vs_request *)vmtddev->cmd_frame;
	vs_req->type = VS_DATA_REQ;
	vs_req->mtddev_req.req_op = VS_MTD_ECC;
	vs_req->mtddev_req.mtd_req.offset = 0;
	vs_req->mtddev_req.mtd_req.size = 0;
	vs_req->mtddev_req.mtd_req.data_offset = 0;
	/* FIXME: Need to choose request id based on some logic instead of 0 */
	vs_req->req_id = 0;

	ret = vmtd_process_request(vmtddev, vs_req, true);
	if (ret != 0) {
		dev_err(vmtddev->device, "Read ECC Failed\n");
		return sprintf(buf, "0x%x\n", ECC_REQUEST_FAILED);
	}

	ecc_response = vs_req->mtddev_resp.ecc_resp;
	vmtddev->ecc_failed_chunk_address = ecc_response.failed_chunk_addr;

	switch (ecc_response.status) {
	case ECC_NO_ERROR:
		return sprintf(buf, "0x%x\n", ECC_NO_ERROR);
	case ECC_ONE_BIT_CORRECTED:
		return sprintf(buf, "0x%x\n", ECC_ONE_BIT_CORRECTED);
	case ECC_TWO_BIT_ERROR:
		return sprintf(buf, "0x%x\n", ECC_TWO_BIT_ERROR);
	case ECC_DISABLED:
		return sprintf(buf, "0x%x\n", ECC_DISABLED);
	default:
		return sprintf(buf, "0x%x\n", ECC_REQUEST_FAILED);
	}
}
static DEVICE_ATTR_RO(ecc_status);

/**
 * @brief Shows the address of the chunk where ECC failure occurred
 *
 * This function retrieves and displays the memory address of the chunk where
 * the last ECC error was detected. The address is displayed in hexadecimal format.
 * After reading the address, it resets the stored address to 0 to prepare for
 * the next ECC error detection.
 *
 * @param[in] dev Pointer to the device structure
 * @param[in] attr Pointer to device attribute structure
 * @param[out] buf Buffer to store the failure chunk address string
 * @return Number of characters written to the buffer
 *
 * @pre
 * - Device must be initialized
 * - ECC status should have been checked previously
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Sync
 *   - Re-entrant: Yes
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
static ssize_t failure_chunk_addr_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct vmtd_dev *vmtddev = dev_get_drvdata(dev);
	ssize_t ret;

	ret = snprintf(buf, PAGE_SIZE, "0x%x\n", vmtddev->ecc_failed_chunk_address);
	vmtddev->ecc_failed_chunk_address = 0;

	return ret;

}
static DEVICE_ATTR_RO(failure_chunk_addr);
/**
 * @}
 */

static const struct attribute *vmtd_storage_attrs[] = {
	&dev_attr_phys_dev.attr,
	&dev_attr_phys_base.attr,
	&dev_attr_manufacturer_id.attr,
	&dev_attr_device_id.attr,
	&dev_attr_qspi_device_size_bytes.attr,
	&dev_attr_ecc_status.attr,
	&dev_attr_failure_chunk_addr.attr,
	NULL
};

#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
/* Error report injection test support is included */
static int vmtd_inject_err_fsi(unsigned int inst_id, struct epl_error_report_frame err_rpt_frame,
				void *data)
{
	struct vmtd_dev *vmtddev = (struct vmtd_dev *)data;
	struct vs_request *vs_req;
	int ret = 0;

	/* Sanity check inst_id */
	if (inst_id != vmtddev->instance_id) {
		dev_err(vmtddev->device, "Invalid Input -> Instance ID = 0x%04x\n", inst_id);
		return -EINVAL;
	}

	/* Sanity check reporter_id */
	if (err_rpt_frame.reporter_id != vmtddev->epl_reporter_id) {
		dev_err(vmtddev->device, "Invalid Input -> Reporter ID = 0x%04x\n",
						err_rpt_frame.reporter_id);
		return -EINVAL;
	}

	mutex_lock(&vmtddev->lock);

	vs_req = (struct vs_request *)vmtddev->cmd_frame;
	vs_req->req_id = HSI_ERROR_MAGIC;
	vs_req->type = VS_ERR_INJECT;
	vs_req->error_inject_req.error_id = err_rpt_frame.error_code;

	ret = vmtd_process_request(vmtddev, vs_req, true);
	if (ret != 0)
		dev_err(vmtddev->device,
			"Error injection failed for mtd device\n");

	mutex_unlock(&vmtddev->lock);

	return ret;
}
#endif

static void  vmtd_init_device(struct work_struct *ws)
{
	struct vmtd_dev *vmtddev = container_of(ws, struct vmtd_dev, init);
	struct vs_request *vs_req = (struct vs_request *)vmtddev->cmd_frame;
	uint32_t i = 0;
	int32_t ret = 0;
#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
	int err;
#endif

	if (devm_request_irq(vmtddev->device, vmtddev->ivck->irq,
		ivc_irq_handler, 0, "vmtd", vmtddev)) {
		dev_err(vmtddev->device, "Failed to request irq %d\n", vmtddev->ivck->irq);
		return;
	}

	tegra_hv_ivc_channel_reset(vmtddev->ivck);

	/* This while loop exits as long as the remote endpoint cooperates. */
	pr_notice("vmtd: send_config wait for ivc channel notified\n");
	while (tegra_hv_ivc_channel_notified(vmtddev->ivck) != 0) {
		if (i++ > IVC_RESET_RETRIES) {
			dev_err(vmtddev->device, "ivc reset timeout\n");
			return;
		}
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(1));
	}

	vs_req->type = VS_CONFIGINFO_REQ;
	dev_info(vmtddev->device, "send config cmd to ivc #%d\n",
		vmtddev->ivc_id);

	ret = vmtd_send_cmd(vmtddev, vs_req, true);
	if (ret != 0) {
		dev_err(vmtddev->device, "Sending %d failed!\n",
				vs_req->type);
		return;
	}

	ret = vmtd_get_configinfo(vmtddev, &vmtddev->config);
	if (ret != 0) {
		dev_err(vmtddev->device, "fetching configinfo failed!\n");
		return;
	}

	if (vmtddev->config.type != VS_MTD_DEV) {
		dev_err(vmtddev->device,
			"Non mtd Config not supported - unexpected response!\n");
		return;
	}

	if (vmtddev->config.mtd_config.size == 0) {
		dev_err(vmtddev->device, "virtual storage device size 0!\n");
		return;
	}

	ret = vmtd_setup_device(vmtddev);
	if (ret != 0) {
		dev_err(vmtddev->device,
			"Setting up vmtd devices failed!\n");
		return;
	}

	if (sysfs_create_files(&vmtddev->device->kobj,
			vmtd_storage_attrs) != 0) {
		dev_warn(vmtddev->device,
			"Error Setting up sysfs files!\n");
	}

#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
	if (vmtddev->config.type == VS_MTD_DEV) {
		vmtddev->epl_id = IP_QSPI;
		vmtddev->epl_reporter_id = HSI_QSPI_REPORT_ID(total_instance_id);
		vmtddev->instance_id = total_instance_id++;
	}

	if (vmtddev->epl_id == IP_QSPI) {
		/* Register error reporting callback */
		err = hsierrrpt_reg_cb(vmtddev->epl_id, vmtddev->instance_id,
							vmtd_inject_err_fsi, vmtddev);
		if (err != 0)
			dev_info(vmtddev->device, "Err inj callback registration failed: %d", err);
	}
#endif

	vmtddev->is_setup = true;
}

/**
 * @defgroup vscd_mtd_probe_remove LinuxMtdVSCD::Probe/Remove
 *
 * @ingroup vscd_mtd_probe_remove
 * @{
 */

/**
 * @brief Probes and initializes the virtual MTD device driver
 *
 * This function performs the following initialization steps:
 * 1. Verifies hypervisor mode and device tree node
 * 2. Allocates and initializes vmtd device structure
 * 3. Reserves IVC channel for command/response communication
 * 4. Reserves IVM memory pool for data transfer
 * 5. Maps shared memory buffer for data transfer
 * 6. Sets up interrupt handling for IVC communication
 * 7. Initializes the virtual MTD device with configuration from physical device
 * 8. Creates sysfs entries for device attributes
 * 9. Registers error injection callbacks if enabled
 *
 * @param[in] pdev Platform device structure containing device information
 * @return 0 on success, negative errno on failure
 *
 * @pre
 * - System must be running in Tegra hypervisor mode
 * - Valid device tree node must be present
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
static int tegra_virt_mtd_probe(struct platform_device *pdev)
{
	struct device_node __maybe_unused *np;
	struct device *dev = &pdev->dev;
	struct vmtd_dev *vmtddev;
	struct tegra_hv_ivm_cookie *ivmk;
	int ret;
	struct device_node *schedulable_vcpu_number_node;
	bool is_cpu_bound = true;

	if (!is_tegra_hypervisor_mode()) {
		dev_err(dev, "Not running on Drive Hypervisor!\n");
		return -ENODEV;
	}

	np = dev->of_node;
	if (np == NULL) {
		dev_err(dev, "No of_node data\n");
		return -ENODEV;
	}

	vmtddev = devm_kzalloc(dev, sizeof(struct vmtd_dev), GFP_KERNEL);
	if (vmtddev == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, vmtddev);
	vmtddev->device = dev;

	if (of_property_read_u32_index(np, "ivc", 1,
		&(vmtddev->ivc_id))) {
		dev_err(dev, "Failed to read ivc property\n");
		return -ENODEV;
	}

	if (of_property_read_u32_index(np, "mempool", 0,
		&(vmtddev->ivm_id))) {
		dev_err(dev, "Failed to read mempool property\n");
		return -ENODEV;
	}

	vmtddev->ivck = tegra_hv_ivc_reserve(NULL, vmtddev->ivc_id, NULL);
	if (IS_ERR_OR_NULL(vmtddev->ivck)) {
		dev_err(dev, "Failed to reserve IVC channel %d\n",
			vmtddev->ivc_id);
		vmtddev->ivck = NULL;
		return -ENODEV;
	}

	ivmk = tegra_hv_mempool_reserve(vmtddev->ivm_id);
	if (IS_ERR_OR_NULL(ivmk)) {
		dev_err(dev, "Failed to reserve IVM channel %d\n",
			vmtddev->ivm_id);
		ivmk = NULL;
		ret = -ENODEV;
		goto free_ivc;
	}
	vmtddev->ivmk = ivmk;

	vmtddev->shared_buffer = devm_memremap(vmtddev->device,
			ivmk->ipa, ivmk->size, MEMREMAP_WB);
	if (IS_ERR_OR_NULL(vmtddev->shared_buffer)) {
		dev_err(dev, "Failed to map mempool area %d\n",
				vmtddev->ivm_id);
		ret = -ENOMEM;
		goto free_mempool;
	}
	memset(vmtddev->shared_buffer, 0, ivmk->size);

	if (vmtddev->ivck->frame_size < sizeof(struct vs_request)) {
		dev_err(dev, "Frame size %d less than ivc_req %ld!\n",
			vmtddev->ivck->frame_size,
			sizeof(struct vs_request));
		ret = -ENOMEM;
		goto free_mempool;
	}

	vmtddev->cmd_frame = devm_kzalloc(vmtddev->device,
			vmtddev->ivck->frame_size, GFP_KERNEL);
	if (vmtddev->cmd_frame == NULL) {
		ret = -ENOMEM;
		goto free_mempool;
	}

	schedulable_vcpu_number_node = of_find_node_by_name(NULL,
			"virt-storage-request-submit-cpu-mapping");
	/* read lcpu_affinity from dts */
	if (schedulable_vcpu_number_node == NULL) {
		dev_err(dev, "%s: virt-storage-request-submit-cpu-mapping DT not found\n",
				__func__);
		is_cpu_bound = false;
	} else if (of_property_read_u32(schedulable_vcpu_number_node, "lcpu2tovcpu",
				&(vmtddev->schedulable_vcpu_number)) != 0) {
		dev_err(dev, "%s: lcpu2tovcpu affinity is not found\n", __func__);
		is_cpu_bound = false;
	}
	if (vmtddev->schedulable_vcpu_number >= num_online_cpus()) {
		dev_err(dev, "%s: cpu affinity (%d) > online cpus (%d)\n", __func__,
				vmtddev->schedulable_vcpu_number, num_online_cpus());
		is_cpu_bound = false;
	}
	if (false == is_cpu_bound) {
		dev_err(dev, "%s: WARN: CPU is unbound\n", __func__);
		vmtddev->schedulable_vcpu_number = num_possible_cpus();
	}

	init_completion(&vmtddev->msg_complete);

	INIT_WORK(&vmtddev->init, vmtd_init_device);

	schedule_work_on(((is_cpu_bound == false) ? DEFAULT_INIT_VCPU :
				vmtddev->schedulable_vcpu_number), &vmtddev->init);


	return 0;

free_mempool:
	tegra_hv_mempool_unreserve(vmtddev->ivmk);

free_ivc:
	tegra_hv_ivc_unreserve(vmtddev->ivck);

	return ret;
}

static int tegra_virt_mtd_remove(struct platform_device *pdev)
{
	struct vmtd_dev *vmtddev = platform_get_drvdata(pdev);

	tegra_hv_ivc_unreserve(vmtddev->ivck);
	tegra_hv_mempool_unreserve(vmtddev->ivmk);
#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
	if (vmtddev->epl_id == IP_SDMMC)
		hsierrrpt_dereg_cb(vmtddev->epl_id, vmtddev->instance_id);
#endif

	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id tegra_virt_mtd_match[] = {
	{ .compatible = "nvidia,tegra-virt-mtd-storage", },
	{},
};
MODULE_DEVICE_TABLE(of, tegra_virt_mtd_match);
#endif /* CONFIG_OF */

/**
 * @brief Removes and cleans up the virtual MTD device driver
 *
 * This function performs the following cleanup steps:
 * 1. Unreserves the IVC channel used for command/response communication
 * 2. Unreserves the IVM memory pool used for data transfer
 * 3. Deregisters error injection callbacks if enabled
 * 4. Frees allocated resources
 *
 * The function exists in two variants based on kernel version:
 * - Returns void for Linux v6.11 and later
 * - Returns int for earlier versions
 *
 * @param[in] pdev Platform device structure containing device information
 * @return void or 0 depending on kernel version
 *
 * @pre
 * - Driver must be successfully probed and initialized
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Async/Sync: Sync
 *   - Re-entrant: No
 * - API Group
 *   - Init: No
 *   - Runtime: No
 *   - De-Init: Yes
 */
#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID)  /* Linux v6.11 */
static void tegra_virt_mtd_remove_wrapper(struct platform_device *pdev)
{
	tegra_virt_mtd_remove(pdev);
}
#else
static int tegra_virt_mtd_remove_wrapper(struct platform_device *pdev)
{
	return tegra_virt_mtd_remove(pdev);
}
#endif
/**
 * @}
 */

static struct platform_driver tegra_virt_mtd_driver = {
	.probe	= tegra_virt_mtd_probe,
	.remove	= tegra_virt_mtd_remove_wrapper,
	.driver	= {
		.name = "Virtual MTD device",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(tegra_virt_mtd_match),
#ifdef CONFIG_PM_SLEEP
		.pm = &tegra_hv_vmtd_pm_ops,
#endif
	},
};

module_platform_driver(tegra_virt_mtd_driver);

MODULE_AUTHOR("Vishal Annapurve <vannapurve@nvidia.com>");
MODULE_DESCRIPTION("VIRT MTD driver");
MODULE_LICENSE("GPL v2");
