// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/pm_runtime.h>
#include <linux/tegra-ivc-bus.h>
#include <linux/fcntl.h>
#include <soc/tegra/fuse-helper.h>
#include <linux/scatterlist.h>
#include <linux/iommu.h>
#include <linux/dma-mapping.h>
#include <linux/types.h>  // For u32, int32_t, etc.
#include <soc/tegra/camrtc-diag-messages.h>
#include <soc/tegra/ivc_ext.h>

/* Memory size for diagnostics */
#define DIAG_MEM_SIZE (6 * 1024 * 1024)

/* ISP SDL test vector period */
#define DIAG_ISP_PFSD_PERIOD (30)

/* Max wait time for response from RCE [ms] */
#define DIAG_MAX_TIMEOUT ((int64_t)5000)

/* Version numbers for ISP test vectors */
#define DIAG_ISP6_PFSD_VERSION    (U32_C(1630655840))  /* ISP6/T234 */
#define DIAG_ISP7_PFSD_VERSION    (U32_C(1725393746))  /* ISP7/T264 */

/* Special number used to create the CRC table for computing the unsigned
 * 32-bit CRC checksum of the PFSD binary's payload.
 */
#define CRC32_POLYNOMIAL   (U32_C(0xEDB88320))

/* Path to ISP6 SDL vector file */
#define DEFAULT_ISP6_SDL_VECTORS "/lib/firmware/tegra23x/isp6-sdl-vectors.bin"
/* Path to ISP7 SDL vector file */
#define DEFAULT_ISP7_SDL_VECTORS "/lib/firmware/tegra26x/isp7-sdl-vectors.bin"

/* Maximum number of ISP instances supported */
#define MAX_ISP_INSTANCES 2  /* Support up to 2 ISP instances */
/**
 * Enable second ISP instance
 * Currently second ISP is disabled, because support needs to be added to RCE to run PFSD vectors
 * on second ISP instance.
 */
#define ISP_SECOND_INSTANCE_ENABLED 0

/* Device index for ISP0 */
#define ISP_0_DEVICE_INDEX 0
/* Device index for ISP1 */
#define ISP_1_DEVICE_INDEX 4

/* Memory for diagnostics */
struct camera_diag_memory {
	/* Pointer to the memory */
	void *ptr;
	/* Device I/O address */
	dma_addr_t iova;
	/* Size of the memory */
	size_t size;
};

/* Device for diagnostics */
struct camera_diag_device {
	/* Pointer to the device structure
	 * This represents either an ISP or RCE device that is used for
	 * diagnostic operations. It's used for DMA allocations, memory
	 * mapping, and device communication. For ISP devices, it's obtained
	 * from the platform device. For RCE, it's derived from the IVC channel.
	 * Must be a valid device pointer when used.
	 */
	struct device *dev;
	/* Device I/O address */
	dma_addr_t dev_iova;
};

/* Channel for diagnostics */
struct camera_diag_channel {
	/* Device structure for the diagnostic channel */
	struct device dev;
	/* IVC channel pointer */
	struct tegra_ivc_channel *ivc;
	/* Mutex for synchronization */
	struct mutex mutex;
	/* Completion for response */
	struct completion resp_ready;
	/* Response message */
	struct camrtc_diag_msg *resp_msg;
	/* Memory for diagnostics */
	struct camera_diag_memory mem;
	/* Memory for ISP instances */
	struct camera_diag_memory mem_isp[MAX_ISP_INSTANCES];
	/* RCE device */
	struct camera_diag_device rce_dev;
	/* ISP devices */
	struct camera_diag_device isp_dev[MAX_ISP_INSTANCES];
	/* Flag to check if channel is initialized */
	bool is_initialized;
	/* Path to ISP SDL vector file */
	char isp_file[256];
	/* Number of ISP instances */
	int num_isp_instances;
	/* Flag to track whether diagnostics were running before suspend */
	bool diag_was_running;
};

/* Static CRC table to avoid recalculation */
static u32 crc_table[256];
static bool crc_table_initialized;

/**
 * @brief Helper function to create a CRC table.
 *
 * This function creates a lookup table for CRC32 calculation.
 * The table is used for computing the CRC32 checksum of the ISP PFSD binary payload.
 *
 * @retval 0        Success
 * @retval -EINVAL  Invalid parameters
 */
static void build_crc_table(void)
{
	u16 i;
	u16 j;
	u32 crc;

	if (crc_table_initialized)
		return;

	for (i = 0; i <= 255U; i++) {
		crc = i;
		for (j = 8; j > 0U; j--) {
			if ((crc & 1U) != 0U)
				crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
			else
				crc >>= 1;
		}
		crc_table[i] = crc;
	}

	crc_table_initialized = true;
}

/**
 * @brief Calculate the CRC32 of a buffer.
 *
 * This function calculates the CRC32 checksum of a specified buffer.
 * It uses a pre-built CRC table for efficient computation.
 *
 * @param[in]  count   Amount of data to read [byte].
 *                     Valid range: [1,INT_MAX]
 * @param[in,out] pCrc In: pointer to base CRC value
 *                     Out: pointer to calculated CRC value
 *                     Valid range: [0,UINT_MAX]
 * @param[in]  buffer  Pointer to the start of the buffer region to compute checksum.
 *                     Must be non-NULL.
 *
 * @retval 0        Success
 * @retval -EINVAL  Invalid parameter(s)
 */
static int32_t diag_calculate_crc(ssize_t count, u32 *pCrc, const char *buffer)
{
	int32_t err = 0;
	const u8 *p;
	u32 temp1;
	u32 temp2;
	u32 crc;

	if (pCrc == NULL) {
		pr_err("Invalid CRC pointer\n");
		return -EINVAL;
	}
	crc = *pCrc;

	if (count <= 0) {
		pr_err("count invalid: %zd\n", count);
		return -EINVAL;
	}

	if (buffer == NULL) {
		pr_err("buffer invalid\n");
		return -EINVAL;
	}

	build_crc_table();

	crc = crc ^ 0xFFFFFFFFU;
	p = (const u8 *) buffer;
	while (count > 0) {
		temp1 = (crc >> 8) & 0x00FFFFFFU;
		temp2 = crc_table[((u32) crc ^ *p++) & ((u32) 0xFF)];
		crc = temp1 ^ temp2;
		count--;
	}
	*pCrc = crc ^ 0xFFFFFFFFU;

	return err;
}

/**
 * @brief Translate diagnostic result code to Linux error code.
 *
 * This function maps camera diagnostic result codes to standard Linux error codes.
 * It ensures consistent error handling across the driver.
 *
 * @param[in] diag_code The diagnostic result code to be translated.
 *
 * @return The corresponding Linux error code.
 *         0 for success, negative value for errors.
 */
static int translate_diag_code(u32 diag_code)
{
	switch (diag_code) {
	case CAMRTC_DIAG_SUCCESS:
		return 0;
	case CAMRTC_DIAG_ERROR_INVAL:
		return -EINVAL;
	case CAMRTC_DIAG_ERROR_NOTSUP:
		return -ENOTSUPP;
	case CAMRTC_DIAG_ERROR_BUSY:
		return -EBUSY;
	case CAMRTC_DIAG_ERROR_TIMEOUT:
		return -ETIMEDOUT;
	default:
		return -EIO;
	}
}

/**
 * @brief Submit a message to the IVC channel and wait for response.
 *
 * This function sends a message to the IVC channel and waits for a response.
 * It handles the communication protocol with the camera diagnostic firmware.
 *
 * @param[in]     ch       Pointer to the camera diagnostic channel.
 * @param[in]     req_msg  Pointer to the request message to be sent.
 * @param[out]    resp_msg Pointer to store the response message.
 *
 * @retval 0        Success
 * @retval -EINVAL  Invalid parameters
 * @retval -ENODEV  Channel not initialized
 * @retval -EBUSY   IVC channel not writable
 * @retval -ETIMEDOUT Timeout waiting for response
 * @retval -EPROTO  Received response with unexpected transaction ID
 * @retval Other    Error codes from IVC operations
 */
static int camera_diag_submit_msg(struct camera_diag_channel *ch,
								 struct camrtc_diag_msg *req_msg,
								 struct camrtc_diag_msg *resp_msg)
{
	int ret;
	u64 timeout_jiffies;

	if (ch == NULL || req_msg == NULL || resp_msg == NULL) {
		pr_err("Invalid parameters\n");
		return -EINVAL;
	}

	if (!ch->is_initialized) {
		dev_err(&ch->dev, "Channel not initialized\n");
		return -ENODEV;
	}

	dev_dbg(&ch->dev, "Submitting message type=%u, txid=%u\n",
		req_msg->msg_type, req_msg->transaction_id);

	mutex_lock(&ch->mutex);
	reinit_completion(&ch->resp_ready);

	/* Ensure the IVC channel is in a good state */
	if (!tegra_ivc_can_write(&ch->ivc->ivc)) {
		dev_err(&ch->dev, "IVC channel not writable\n");
		mutex_unlock(&ch->mutex);
		return -EBUSY;
	}

	ret = tegra_ivc_write(&ch->ivc->ivc, NULL, req_msg, sizeof(*req_msg));
	if (ret < 0) {
		dev_err(&ch->dev, "IVC write failed: %d\n", ret);
		mutex_unlock(&ch->mutex);
		return ret;
	}

	/* Notify the receiver */
	dev_dbg(&ch->dev, "Waiting for response to message type=%u\n", req_msg->msg_type);
	timeout_jiffies = msecs_to_jiffies(DIAG_MAX_TIMEOUT);

	ret = wait_for_completion_timeout(&ch->resp_ready, timeout_jiffies);
	if (ret == 0) {
		if (tegra_ivc_can_read(&ch->ivc->ivc)) {
			dev_info(&ch->dev, "IVC channel is readable, but notify not called\n");
			/* We'll attempt to read anyway since data is available */
		} else {
			dev_err(&ch->dev, "Timeout waiting for response after %lld ms\n", DIAG_MAX_TIMEOUT);
			mutex_unlock(&ch->mutex);
			return -ETIMEDOUT;
		}
	}

	/* Read the response */
	ret = tegra_ivc_read(&ch->ivc->ivc, NULL, resp_msg, sizeof(*resp_msg));
	if (ret < 0) {
		dev_err(&ch->dev, "IVC read failed after wait: %d\n", ret);
		mutex_unlock(&ch->mutex);
		return ret;
	}

	if (resp_msg->transaction_id != req_msg->transaction_id) {
		dev_err(&ch->dev, "Received response with unexpected transaction ID: %u\n",
				resp_msg->transaction_id);
		mutex_unlock(&ch->mutex);
		return -EPROTO;
	}

	memcpy(resp_msg, ch->resp_msg, sizeof(*resp_msg));

	mutex_unlock(&ch->mutex);
	return 0;
}

/**
 * @brief Callback function for IVC channel notifications.
 *
 * This function is called when a notification is received from the IVC channel.
 * It processes incoming messages and completes the response wait operation when
 * a valid message is received.
 *
 * @param[in] chan Pointer to the IVC channel.
 */
static void camera_diag_notify(struct tegra_ivc_channel *chan)
{
	struct camera_diag_channel *ch = tegra_ivc_channel_get_drvdata(chan);
	struct camrtc_diag_msg *msg;
	int ret;

	if (ch == NULL) {
		dev_err(&chan->dev, "No channel context in notify callback\n");
		return;
	}

	/* Check if we can read from the channel */
	if (!tegra_ivc_can_read(&chan->ivc)) {
		dev_dbg(&ch->dev, "IVC channel not readable in notify\n");
		return;
	}

	/* Read the message */
	ret = tegra_ivc_read_peek(&chan->ivc, NULL, ch->resp_msg, 0, sizeof(*ch->resp_msg));
	if (ret < 0) {
		dev_err(&ch->dev, "Failed to read IVC message: %d\n", ret);
		return;
	}

	msg = ch->resp_msg;
	dev_dbg(&ch->dev, "Received message: type=%u, txn_id=%u\n",
		msg->msg_type, msg->transaction_id);

	switch (msg->msg_type) {
	case CAMRTC_DIAG_ISP5_SDL_SETUP_RESP:
	case CAMRTC_DIAG_ISP5_SDL_RELEASE_RESP:
	case CAMRTC_DIAG_ISP5_SDL_STATUS_RESP:
		complete(&ch->resp_ready);
		break;
	default:
		dev_err(&ch->dev, "Unknown message type: %u\n", msg->msg_type);
		break;
	}
}

/**
 * @brief Initialize camera diagnostics channel.
 *
 * This function initializes the camera diagnostics channel, allocates memory,
 * and sets up mappings for all available ISP instances. It handles different
 * chip configurations by adjusting the number of ISP instances accordingly.
 *
 * @param[in,out] ch Pointer to the camera diagnostic channel to initialize.
 *
 * @retval 0             Success
 * @retval -EINVAL       Invalid parameters
 * @retval -ENOMEM       Failed to allocate memory
 * @retval -ENODEV       Failed to find ISP device node
 * @retval -EPROBE_DEFER ISP or IOMMU not yet mapped
 */
static int camera_diag_channel_init(struct camera_diag_channel *ch)
{
	struct device *rce_dev;
	struct device_node *isp_node;
	struct platform_device *isp_pdev;
	struct device *isp_dev = NULL;
	int ret, i;
	struct sg_table sgt;
	int chip_id = __tegra_get_chip_id();
	int max_allowed_instances;

	if (ch == NULL) {
		pr_err("Invalid channel handle\n");
		return -EINVAL;
	}

	rce_dev = ch->ivc->dev.parent->parent; /* RCE device */
	if (!rce_dev) {
		dev_err(&ch->dev, "Failed to get RCE device\n");
		return -ENODEV;
	}

	/* Set maximum ISP instances based on chip ID */
	switch (chip_id) {
	case TEGRA234:
		max_allowed_instances = 1; /* T234 only supports 1 ISP */
		break;
	case TEGRA264:
		max_allowed_instances = 2; /* T264 supports 2 ISP instances */
		break;
	default:
		dev_info(&ch->dev, "Unknown chip ID %d, defaulting to 1 ISP instance", chip_id);
		max_allowed_instances = 1;
		break;
	}

	/* Limit search to maximum allowed for this chip */
	if (max_allowed_instances < MAX_ISP_INSTANCES)
		dev_info(&ch->dev, "Limiting to %d ISP instance(s) for this chip", max_allowed_instances);

	/* Initialize completion structure */
	init_completion(&ch->resp_ready);

	/* Initialize the number of ISP instances to 0 */
	ch->num_isp_instances = 0;

	/* Allocate memory for diagnostics first (shared by all ISP instances) */
	ch->mem.size = DIAG_MEM_SIZE;
	ch->mem.ptr = dma_alloc_coherent(rce_dev, ch->mem.size, &ch->mem.iova, GFP_KERNEL);
	if (ch->mem.ptr == NULL) {
		dev_err(&ch->dev, "Failed to allocate memory for diagnostics\n");
		return -ENOMEM;
	}

	dev_dbg(&ch->dev, "Allocated memory for diagnostics: VA=%p, IOVA=%pad, size=%zu\n",
			ch->mem.ptr, &ch->mem.iova, ch->mem.size);

	/* Find ISP devices - try to get up to max_allowed_instances */
	for (i = 0; i < max_allowed_instances; i++) {
		u32 isp_device_index;
		/* Skip second instance if not enabled */
		if (i > 0 && !ISP_SECOND_INSTANCE_ENABLED) {
			dev_info(&ch->dev, "Second ISP instance support is disabled\n");
			break;
		}

		if (i == 0)
			isp_device_index = ISP_0_DEVICE_INDEX;
		else if (i == 1)
			isp_device_index = ISP_1_DEVICE_INDEX;

		isp_node = of_parse_phandle(rce_dev->of_node, "nvidia,camera-devices", isp_device_index);
		if (!isp_node) {
			if (i == 0) {
				dev_err(&ch->dev, "Failed to find ISP device node\n");
				ret = -ENODEV;
				goto error_cleanup;
			} else {
				/* Not an error for second instance */
				dev_info(&ch->dev, "No additional ISP instances found\n");
				break;
			}
		}

		isp_pdev = of_find_device_by_node(isp_node);
		if (!isp_pdev) {
			dev_err(&ch->dev, "Failed to find ISP platform device %d\n", i);
			of_node_put(isp_node);
			if (i == 0) {
				ret = -EPROBE_DEFER;  /* Defer probe until ISP is available */
				goto error_cleanup;
			} else {
				continue;  /* Skip this instance */
			}
		}

		isp_dev = &isp_pdev->dev;

		/* Check if ISP device has an IOMMU mapped */
		if (!device_iommu_mapped(isp_dev)) {
			dev_dbg(&ch->dev, "ISP%d IOMMU not mapped yet, deferring probe\n", i);
			put_device(&isp_pdev->dev);
			of_node_put(isp_node);
			if (i == 0) {
				ret = -EPROBE_DEFER;  /* Defer probe until IOMMU is set up */
				goto error_cleanup;
			} else {
				continue;  /* Skip this instance */
			}
		}

		/* Setup ISP mapping for this instance */
		ch->mem_isp[i].ptr = ch->mem.ptr;
		ch->mem_isp[i].size = ch->mem.size;

		/* Map memory for ISP using scatter-gather table */
		ret = dma_get_sgtable(rce_dev, &sgt, ch->mem.ptr, ch->mem.iova, ch->mem.size);
		if (ret < 0) {
			dev_err(&ch->dev, "Failed to get scatter-gather table for ISP%d: %d\n", i, ret);
			put_device(&isp_pdev->dev);
			of_node_put(isp_node);
			if (i == 0)
				goto error_cleanup;
			else
				continue;  /* Skip this instance */
		}

		ret = dma_map_sg(isp_dev, sgt.sgl, sgt.nents, DMA_BIDIRECTIONAL);
		if (ret == 0) {
			dev_err(&ch->dev, "Failed to map memory to ISP%d\n", i);
			sg_free_table(&sgt);
			put_device(&isp_pdev->dev);
			of_node_put(isp_node);
			if (i == 0) {
				ret = -ENOMEM;
				goto error_cleanup;
			} else {
				continue;  /* Skip this instance */
			}
		}

		/* Get the ISP IOVA from the first entry in the sg table */
		ch->mem_isp[i].iova = sg_dma_address(sgt.sgl);
		sg_free_table(&sgt);

		/* Store the ISP device for later use */
		ch->isp_dev[i].dev = isp_dev;

		/* Sync memory for device access */
		dma_sync_single_for_device(isp_dev, ch->mem_isp[i].iova, ch->mem_isp[i].size, DMA_BIDIRECTIONAL);

		put_device(&isp_pdev->dev);
		of_node_put(isp_node);

		/* Increment the count of ISP instances */
		ch->num_isp_instances++;
	}

	/* Need at least one ISP instance */
	if (ch->num_isp_instances == 0) {
		dev_err(&ch->dev, "No valid ISP instances found\n");
		ret = -ENODEV;
		goto error_cleanup;
	}

	ch->is_initialized = true;
	dev_dbg(&ch->dev, "Initialized with %d ISP instance(s)\n", ch->num_isp_instances);
	return 0;

error_cleanup:
	/* Free allocated memory */
	if (ch->mem.ptr) {
		dma_free_coherent(rce_dev, ch->mem.size, ch->mem.ptr, ch->mem.iova);
		ch->mem.ptr = NULL;
	}

	return ret;
}

/**
 * @brief Setup ISP SDL diagnostics.
 *
 * This function sets up the ISP Safety Diagnostic Library (SDL) tests. It
 * loads the appropriate test vectors from a file, validates the CRC,
 * and sends setup commands to the firmware for each ISP instance.
 *
 * @param[in,out] ch Pointer to the camera diagnostic channel.
 *
 * @retval 0        Success (at least one ISP instance was set up successfully)
 * @retval -EINVAL  Invalid parameters, CRC validation failed, or version mismatch
 * @retval -ENODEV  No ISP instances available or all setup attempts failed
 * @retval -EFBIG   Test vector file too large for allocated memory
 * @retval -EIO     File read error
 * @retval Other    Error codes from file operations or IVC communication
 */
static int camera_diag_isp_sdl_setup(struct camera_diag_channel *ch)
{
	struct camrtc_diag_msg req, resp;
	int err, i;
	struct file *filp;
	ssize_t read_size;
	const struct isp5_sdl_header *header;
	loff_t pos = 0;
	char *default_vectors_path = NULL;
	u32 crc = 0;
	char *crc_ptr;
	bool setup_success = false;
	int chip_id = __tegra_get_chip_id();  /* Use fuse-helper function instead of hardcoded value */
	u32 expected_version;

	if (ch == NULL || !ch->is_initialized || ch->num_isp_instances == 0) {
		pr_err("Invalid channel handle or not initialized\n");
		return -EINVAL;
	}

	dev_info(&ch->dev, "Setting up ISP SDL diagnostics for %d instance(s)\n",
		 ch->num_isp_instances);

	/* Choose the appropriate vector file based on chip ID */
	switch (chip_id) {
	case TEGRA234:
		default_vectors_path = DEFAULT_ISP6_SDL_VECTORS;
		expected_version = DIAG_ISP6_PFSD_VERSION;
		break;
	case TEGRA264:
		default_vectors_path = DEFAULT_ISP7_SDL_VECTORS;
		expected_version = DIAG_ISP7_PFSD_VERSION;
		break;
	default:
		dev_err(&ch->dev, "Unsupported chip ID: %d, using default\n", chip_id);
		default_vectors_path = DEFAULT_ISP7_SDL_VECTORS; // Default to latest
		expected_version = DIAG_ISP7_PFSD_VERSION;
		break;
	}

	/* Use the default path if not specified */
	if (ch->isp_file[0] == '\0')
		strscpy(ch->isp_file, default_vectors_path, sizeof(ch->isp_file));

	dev_dbg(&ch->dev, "Using ISP SDL vectors from: %s\n", ch->isp_file);

	/* Open the firmware file */
	filp = filp_open(ch->isp_file, O_RDONLY, 0);
	if (IS_ERR(filp)) {
		err = PTR_ERR(filp);
		dev_err(&ch->dev, "Failed to open ISP SDL vector file %s: %d\n",
				ch->isp_file, err);
		return err;
	}

	/* Read the file into memory */
	read_size = kernel_read(filp, ch->mem.ptr, ch->mem.size, &pos);
	filp_close(filp, NULL);

	if (read_size <= 0) {
		dev_err(&ch->dev, "Failed to read ISP SDL vector file: %zd\n", read_size);
		return read_size < 0 ? read_size : -EIO;
	}

	if (read_size > ch->mem.size) {
		dev_err(&ch->dev, "File too large for allocated memory: %zd > %zu\n",
				read_size, ch->mem.size);
		return -EFBIG;
	}

	dev_dbg(&ch->dev, "Successfully read %zd bytes from ISP SDL vector file\n", read_size);

	/* Validate CRC */
	header = (struct isp5_sdl_header *)ch->mem.ptr;
	crc_ptr = (char *)ch->mem.ptr + sizeof(struct isp5_sdl_header);

	err = diag_calculate_crc(read_size - sizeof(struct isp5_sdl_header), &crc, crc_ptr);
	if (err != 0) {
		dev_err(&ch->dev, "CRC calculation failed: %d\n", err);
		return err;
	}

	if (crc != header->payload_crc32) {
		dev_err(&ch->dev, "CRC validation failed: expected 0x%08x, got 0x%08x\n",
				header->payload_crc32, crc);
		return -EINVAL;
	}

	if (header->version != expected_version) {
		dev_err(&ch->dev, "Version mismatch: expected 0x%08x, got 0x%08x\n",
				expected_version, header->version);
		return -EINVAL;
	}


	/* Set up SDL for each ISP instance */
	for (i = 0; i < ch->num_isp_instances; i++) {
		/* Skip second instance if not enabled */
		if (i > 0 && !ISP_SECOND_INSTANCE_ENABLED) {
			dev_info(&ch->dev, "Skipping second ISP instance setup (disabled)\n");
			break;
		}

		/* Send the setup request to the firmware */
		memset(&req, 0, sizeof(req));
		req.msg_type = CAMRTC_DIAG_ISP5_SDL_SETUP_REQ;
		req.transaction_id = CAMRTC_DIAG_ISP5_SDL_SETUP_REQ + i; /* Unique ID per instance */

		/* Use the different IOVAs for RCE and ISP */
		req.isp5_sdl_setup_req.rce_iova = ch->mem.iova;
		req.isp5_sdl_setup_req.isp_iova = ch->mem_isp[i].iova;
		req.isp5_sdl_setup_req.size = read_size;
		req.isp5_sdl_setup_req.period = DIAG_ISP_PFSD_PERIOD;

		dev_dbg(&ch->dev, "Sending ISP%d SDL setup request: rce_iova=0x%llx, isp_iova=0x%llx, size=%u\n",
				i, req.isp5_sdl_setup_req.rce_iova, req.isp5_sdl_setup_req.isp_iova,
				req.isp5_sdl_setup_req.size);

		err = camera_diag_submit_msg(ch, &req, &resp);
		if (err != 0) {
			dev_err(&ch->dev, "Failed to submit ISP%d SDL setup request: %d\n", i, err);
			/* Continue with next instance if this one fails */
			continue;
		}

		if (resp.isp5_sdl_setup_resp.result != CAMRTC_DIAG_SUCCESS) {
			dev_err(&ch->dev, "ISP%d SDL setup response returned error: %u\n",
					i, resp.isp5_sdl_setup_resp.result);
			/* Continue with next instance if this one fails */
			continue;
		}

		dev_info(&ch->dev, "ISP%d SDL diagnostics setup successful\n", i);
		setup_success = true;
	}

	if (!setup_success) {
		dev_err(&ch->dev, "Failed to set up SDL diagnostics on any ISP instance\n");
		return -ENODEV;
	}

	return 0;
}

/**
 * @brief Get ISP SDL diagnostics status.
 *
 * This function queries the status of the ISP Safety Diagnostic Library (SDL) tests
 * for a specific ISP instance. It retrieves information such as number of tests executed,
 * passed, and current running status.
 *
 * @param[in]     ch          Pointer to the camera diagnostic channel.
 * @param[out]    status      Pointer to store the SDL status information.
 * @param[in]     instance_id ISP instance ID (0 for primary, 1 for secondary if supported).
 *
 * @retval 0        Success
 * @retval -EINVAL  Invalid parameters or second ISP instance not enabled
 * @retval Other    Error codes from camera_diag_submit_msg or translated diagnostic codes
 */
static int camera_diag_isp_sdl_status(struct camera_diag_channel *ch,
					 struct camrtc_diag_isp5_sdl_status_resp *status,
					 int instance_id)
{
	int err;
	struct camrtc_diag_msg req, resp;

	if (ch == NULL || status == NULL) {
		pr_err("Invalid parameters\n");
		return -EINVAL;
	}

	/* Validate instance_id */
	if (instance_id >= ch->num_isp_instances) {
		dev_err(&ch->dev, "Invalid ISP instance ID: %d (max: %d)\n",
			instance_id, ch->num_isp_instances - 1);
		return -EINVAL;
	}

	/* Skip second instance if not enabled */
	if (instance_id > 0 && !ISP_SECOND_INSTANCE_ENABLED) {
		dev_info(&ch->dev, "Second ISP instance is disabled\n");
		return -EINVAL;
	}

	dev_dbg(&ch->dev, "Getting ISP%d SDL status\n", instance_id);

	/* Prepare and send the status request */
	memset(&req, 0, sizeof(req));
	req.msg_type = CAMRTC_DIAG_ISP5_SDL_STATUS_REQ;
	req.transaction_id = CAMRTC_DIAG_ISP5_SDL_STATUS_REQ + instance_id; /* Unique ID per instance */

	err = camera_diag_submit_msg(ch, &req, &resp);
	if (err != 0) {
		dev_err(&ch->dev, "Failed to submit ISP%d SDL status request: %d\n", instance_id, err);
		return err;
	}

	if (resp.isp5_sdl_status_resp.result != CAMRTC_DIAG_SUCCESS) {
		dev_err(&ch->dev, "ISP%d SDL status response returned error: %u\n",
			instance_id, resp.isp5_sdl_status_resp.result);
		return translate_diag_code(resp.isp5_sdl_status_resp.result);
	}

	*status = resp.isp5_sdl_status_resp;
	dev_dbg(&ch->dev, "ISP%d SDL status: running=%u, executed=%llu, passed=%llu\n",
		instance_id, status->running, status->executed, status->passed);
	return 0;
}

/**
 * @brief Check the status of the diagnostic channel.
 *
 * This function checks the status of the diagnostic channel by comparing
 * the number of executed and passed tests before and after a delay period.
 * It verifies that tests are being executed and passed correctly.
 *
 * @param[in] ch  Pointer to the camera diagnostic channel.
 *
 * @retval 0        Success - tests are running correctly
 * @retval -EINVAL  Invalid parameter(s) or tests not running correctly
 * @retval Other    Error codes from camera_diag_isp_sdl_status
 */
static int __maybe_unused camera_diag_check_status(struct camera_diag_channel *ch)
{
	struct camrtc_diag_isp5_sdl_status_resp status_before, status_after;
	int err;

	if (ch == NULL) {
		pr_err("Invalid channel handle\n");
		return -EINVAL;
	}

	/* Allow at least 1 test to run */
	msleep(2 * DIAG_ISP_PFSD_PERIOD * 1000);

	/* Use instance 0 (primary ISP) */
	err = camera_diag_isp_sdl_status(ch, &status_before, 0);
	if (err != 0) {
		pr_err("Diag channel get status failed\n");
		return err;
	}

	/* Allow another test scheduling */
	msleep(2 * DIAG_ISP_PFSD_PERIOD * 1000);

	/* Use instance 0 (primary ISP) */
	err = camera_diag_isp_sdl_status(ch, &status_after, 0);
	if (err != 0) {
		pr_err("Diag channel get status failed\n");
		return err;
	}

	if ((status_after.executed <= status_before.executed) ||
		(status_after.passed <= status_before.passed)) {
		pr_err("Error: Number of executed or passed tests is not increasing\n");
		return -EINVAL;
	}

	dev_info(&ch->dev, "Number of executed tests: %llu\n", status_after.executed);
	dev_info(&ch->dev, "Number of passed tests: %llu\n", status_after.passed);
	dev_info(&ch->dev, "Number of crc failed tests: %u\n", status_after.crc_failed);
	dev_info(&ch->dev, "Number of running tests: %u\n", status_after.running);
	dev_info(&ch->dev, "Number of scheduled tests: %llu\n", status_after.scheduled);

	return 0;
}

/**
 * @brief Sysfs show function for checking diagnostic status.
 *
 * This function is called when the user reads the 'status' sysfs attribute.
 * It displays the current status of all ISP SDL diagnostics tests, including
 * the number of tests running, scheduled, executed, passed, and failed.
 *
 * @param[in]  dev   Device pointer.
 * @param[in]  attr  Device attribute descriptor.
 * @param[out] buf   Buffer to write the status information to.
 *
 * @return Number of bytes written to buffer on success, negative error code on failure.
 */
static ssize_t status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct camera_diag_channel *ch = dev_get_drvdata(dev);
	struct camrtc_diag_isp5_sdl_status_resp status;
	int err, i;
	unsigned long start_time, end_time;
	bool all_tests_pass;
	ssize_t pos = 0;

	if (ch == NULL)
		return -EINVAL;

	/* Print status header */
	pos += sprintf(buf + pos, "Camera diagnostic status:\n");

	/* Get status for each active ISP instance */
	for (i = 0; i < ch->num_isp_instances; i++) {
		/* Skip second instance if not enabled */
		if (i > 0 && !ISP_SECOND_INSTANCE_ENABLED) {
			int len = sprintf(buf + pos, "\nISP%d: DISABLED\n", i);
			(void)__builtin_add_overflow(pos, len, &pos);
			continue;
		}

		start_time = jiffies;
		err = camera_diag_isp_sdl_status(ch, &status, i);

		end_time = 0U;
		(void)__builtin_add_overflow(start_time, msecs_to_jiffies(1000), &end_time);
		if (time_after(jiffies, end_time) || err != 0) {
			pos += sprintf(buf + pos, "\nISP%d: Error getting diagnostic status: %d\n", i, err);
			continue;
		}

		/* Calculate diagnostic status */
		all_tests_pass = (status.executed > 0 && status.passed == status.executed);

		/* Print detailed status information for this ISP */
		pos += sprintf(buf + pos,
			"\nISP%d:\n"
			"  Running: %u\n"
			"  Scheduled: %llu\n"
			"  Executed: %llu\n"
			"  Passed: %llu\n"
			"  CRC failed: %u\n"
			"  Status: %s\n",
			i,
			status.running,
			status.scheduled,
			status.executed,
			status.passed,
			status.crc_failed,
			all_tests_pass ? "All tests passing" :
			  (status.running ? "Tests running" : "Not active"));
	}

	return pos;
}

/**
 * @brief Release ISP SDL diagnostics.
 *
 * This function releases the ISP Safety Diagnostic Library (SDL) tests
 * that were previously set up. It sends release commands to the firmware
 * for each ISP instance.
 *
 * @param[in,out] ch Pointer to the camera diagnostic channel.
 *
 * @retval 0        Success (at least one ISP instance was released successfully)
 * @retval -EINVAL  Invalid parameters or channel not initialized
 * @retval -ENODEV  Failed to release SDL diagnostics on any ISP instance
 * @retval Other    Error codes from camera_diag_submit_msg
 */
static int camera_diag_isp_sdl_release(struct camera_diag_channel *ch)
{
	int err, i;
	struct camrtc_diag_msg req, resp;
	bool release_success = false;

	if (ch == NULL || !ch->is_initialized) {
		pr_err("Invalid channel handle or not initialized\n");
		return -EINVAL;
	}

	dev_dbg(&ch->dev, "Releasing ISP SDL diagnostics for all instances\n");

	/* Release SDL for each ISP instance */
	for (i = 0; i < ch->num_isp_instances; i++) {
		/* Skip second instance if not enabled */
		if (i > 0 && !ISP_SECOND_INSTANCE_ENABLED) {
			dev_info(&ch->dev, "Skipping second ISP instance release (disabled)\n");
			break;
		}

		dev_info(&ch->dev, "Releasing SDL for ISP instance %d\n", i);

		/* Prepare and send the release request */
		memset(&req, 0, sizeof(req));
		req.msg_type = CAMRTC_DIAG_ISP5_SDL_RELEASE_REQ;
		req.transaction_id = CAMRTC_DIAG_ISP5_SDL_RELEASE_REQ + i; /* Unique ID per instance */

		err = camera_diag_submit_msg(ch, &req, &resp);
		if (err != 0) {
			dev_err(&ch->dev, "Failed to submit ISP%d SDL release request: %d\n", i, err);
			/* Continue with next instance if this one fails */
			continue;
		}

		if (resp.isp5_sdl_release_resp.result != CAMRTC_DIAG_SUCCESS) {
			dev_err(&ch->dev, "ISP%d SDL release response returned error: %u\n",
					i, resp.isp5_sdl_release_resp.result);
			/* Continue with next instance if this one fails */
			continue;
		}

		dev_dbg(&ch->dev, "ISP%d SDL diagnostics released successfully\n", i);
		release_success = true;
	}

	if (!release_success && ch->num_isp_instances > 0) {
		dev_err(&ch->dev, "Failed to release SDL diagnostics on any ISP instance\n");
		return -ENODEV;
	}

	return 0;
}

/**
 * @brief Deinitialize the diagnostic channel.
 *
 * This function performs cleanup for the camera diagnostics channel.
 * It releases ISP SDL diagnostics, unmaps memory from ISP devices,
 * frees allocated memory, and releases runtime PM resources.
 *
 * @param[in,out] ch Pointer to the camera diagnostic channel to deinitialize.
 */
static void camera_diag_channel_deinit(struct camera_diag_channel *ch)
{
	struct device *rce_dev;
	int i;

	if (ch == NULL || !ch->is_initialized) {
		pr_err("Invalid channel handle or not initialized\n");
		return;
	}

	dev_dbg(&ch->dev, "Deinitializing camera diagnostics channel\n");

	/* Get RCE device */
	rce_dev = ch->ivc->dev.parent->parent;
	if (!rce_dev) {
		dev_err(&ch->dev, "Invalid RCE device\n");
		return;
	}

	/* Release ISP SDL for all instances */
	camera_diag_isp_sdl_release(ch);

	/* Clean up each ISP instance */
	for (i = 0; i < ch->num_isp_instances; i++) {
		if (ch->isp_dev[i].dev && ch->mem_isp[i].ptr) {
			dev_dbg(&ch->dev, "Unmapping memory from ISP%d: VA=%p, IOVA=%pad\n",
					i, ch->mem_isp[i].ptr, &ch->mem_isp[i].iova);
			dma_unmap_single(ch->isp_dev[i].dev, ch->mem_isp[i].iova,
							ch->mem_isp[i].size, DMA_BIDIRECTIONAL);
			ch->mem_isp[i].ptr = NULL;
		}
	}

	/* Free RCE memory */
	if (ch->mem.ptr) {
		dev_dbg(&ch->dev, "Freeing memory: VA=%p, IOVA=%pad\n",
				ch->mem.ptr, &ch->mem.iova);
		dma_free_coherent(rce_dev, ch->mem.size, ch->mem.ptr, ch->mem.iova);
		ch->mem.ptr = NULL;
	}

	/* Release RCE runtime PM */
	pm_runtime_put(rce_dev);

	ch->is_initialized = false;
	dev_dbg(&ch->dev, "Camera diagnostics channel deinitialization complete\n");
}

/**
 * @brief Sysfs store function for controlling diagnostic state.
 *
 * This function is called when the user writes to the 'diag_control' sysfs attribute.
 * It allows toggling the ISP SDL diagnostics on and off without deallocating memory.
 * Accepted values: "start" to start diagnostics, "stop" to stop diagnostics.
 *
 * @param[in]  dev   Device pointer.
 * @param[in]  attr  Device attribute descriptor.
 * @param[in]  buf   Buffer containing the user command.
 * @param[in]  count Size of the buffer.
 *
 * @return Number of bytes processed on success, negative error code on failure.
 */
static ssize_t diag_control_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct camera_diag_channel *ch = dev_get_drvdata(dev);
	struct camrtc_diag_isp5_sdl_status_resp status;
	int err = 0;
	bool is_running = false;

	if (ch == NULL || !ch->is_initialized)
		return -EINVAL;


	/* Get current status first */
	err = camera_diag_isp_sdl_status(ch, &status, 0);
	if (err == 0)
		is_running = (status.running != 0);

	/* Check for "stop" command */
	if (strncmp(buf, "stop", 4) == 0) {
		if (!is_running) {
			dev_info(&ch->dev, "ISP SDL diagnostics already stopped\n");
			return count;
		}

		dev_info(&ch->dev, "Stopping ISP SDL diagnostics\n");
		err = camera_diag_isp_sdl_release(ch);
		if (err != 0) {
			dev_err(&ch->dev, "Failed to stop ISP SDL diagnostics: %d\n", err);
			return err;
		}
		return count;
	}

	/* Check for "start" command */
	if (strncmp(buf, "start", 5) == 0) {
		if (is_running) {
			dev_info(&ch->dev, "ISP SDL diagnostics already running\n");
			return count;
		}

		dev_info(&ch->dev, "Starting ISP SDL diagnostics\n");
		err = camera_diag_isp_sdl_setup(ch);
		if (err != 0) {
			dev_err(&ch->dev, "Failed to start ISP SDL diagnostics: %d\n", err);
			return err;
		}
		return count;
	}

	/* If we get here, the command was not recognized */
	dev_err(&ch->dev, "Unknown command: %.*s\n", (int)count, buf);
	return -EINVAL;
}

/**
 * @brief Sysfs show function for diagnostic control state.
 *
 * This function is called when the user reads the 'diag_control' sysfs attribute.
 * It shows the current state of the ISP SDL diagnostics and available commands.
 *
 * @param[in]  dev   Device pointer.
 * @param[in]  attr  Device attribute descriptor.
 * @param[out] buf   Buffer to write the state information to.
 *
 * @return Number of bytes written to buffer on success, negative error code on failure.
 */
static ssize_t diag_control_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct camera_diag_channel *ch = dev_get_drvdata(dev);
	struct camrtc_diag_isp5_sdl_status_resp status;
	int err;
	bool is_running = false;

	if (ch == NULL || !ch->is_initialized)
		return -EINVAL;

	/* Try to get status for primary ISP instance */
	err = camera_diag_isp_sdl_status(ch, &status, 0);
	if (err == 0)
		is_running = (status.running != 0);

	return sprintf(buf,
		"Camera diagnostic control\n"
		"Current state: %s\n"
		"\n"
		"Available commands:\n"
		"  start - Start ISP SDL diagnostics\n"
		"  stop  - Stop ISP SDL diagnostics\n",
		is_running ? "RUNNING" : "STOPPED");
}

/* Create the device attributes */
static DEVICE_ATTR_RO(status);
static DEVICE_ATTR_RW(diag_control);

/* Define the attribute group */
static struct attribute *camera_diag_attrs[] = {
	&dev_attr_status.attr,
	&dev_attr_diag_control.attr,
	NULL,
};

static const struct attribute_group camera_diag_attr_group = {
	.attrs = camera_diag_attrs,
};


/**
 * @brief Probe function for camera-diagnostics driver.
 *
 * This function is called when the IVC channel for camera diagnostics is discovered.
 * It initializes the camera diagnostics channel, sets up ISP SDL diagnostics,
 * and creates sysfs interfaces for user interaction.
 *
 * @param[in] chan IVC channel to probe.
 *
 * @retval 0             Success
 * @retval -ENOMEM       Failed to allocate memory
 * @retval -EPROBE_DEFER Deferring probe until ISP is ready
 * @retval Other         Error codes from initialization functions
 */
static int camera_diag_probe(struct tegra_ivc_channel *chan)
{
	struct camera_diag_channel *ch;
	int err;

	dev_dbg(&chan->dev, "Probing camera diagnostics driver\n");

	ch = devm_kzalloc(&chan->dev, sizeof(*ch), GFP_KERNEL);
	if (ch == NULL)
		return -ENOMEM;

	dev_dbg(&chan->dev, "Allocated channel structure\n");

	dev_set_name(&ch->dev, "camera-diag");
	ch->dev.parent = &chan->dev;
	ch->dev.type = &tegra_ivc_channel_type;
	ch->ivc = chan;

	/* Set driver data */
	tegra_ivc_channel_set_drvdata(chan, ch);

	/* Setup mutex */
	mutex_init(&ch->mutex);
	dev_dbg(&chan->dev, "Initialized mutex\n");

	/* Allocate response message buffer */
	ch->resp_msg = devm_kzalloc(&chan->dev, sizeof(*ch->resp_msg), GFP_KERNEL);
	if (ch->resp_msg == NULL) {
		err = -ENOMEM;
		goto error;
	}
	dev_dbg(&chan->dev, "Allocated response message buffer\n");

	/* Initialize the diagnostic channel */
	dev_dbg(&chan->dev, "Initializing diagnostic channel\n");
	err = camera_diag_channel_init(ch);
	if (err == -EPROBE_DEFER) {
		/* Clean up and return EPROBE_DEFER to try again later */
		dev_dbg(&chan->dev, "Deferring probe until ISP is ready\n");
		mutex_destroy(&ch->mutex);
		devm_kfree(&chan->dev, ch->resp_msg);
		devm_kfree(&chan->dev, ch);
		return -EPROBE_DEFER;
	} else if (err != 0) {
		dev_err(&chan->dev, "Failed to initialize diagnostic channel: %d\n", err);
		goto error;
	}

	/* Set up ISP SDL diagnostics */
	err = camera_diag_isp_sdl_setup(ch);
	if (err != 0) {
		dev_err(&chan->dev, "Failed to set up ISP SDL diagnostics: %d\n", err);
		/* We can continue even if this fails */
		dev_warn(&chan->dev, "Continuing without ISP SDL diagnostics\n");
	}

	dev_dbg(&chan->dev, "Set driver data\n");

	/* Create the device */
	err = device_register(&ch->dev);
	if (err) {
		dev_err(&chan->dev, "Failed to register device: %d\n", err);
		goto error;
	}

	/* Set driver data */
	dev_set_drvdata(&ch->dev, ch);

	/* Create sysfs attribute group */
	err = sysfs_create_group(&ch->dev.kobj, &camera_diag_attr_group);
	if (err) {
		dev_err(&chan->dev, "Failed to create sysfs attributes: %d\n", err);
		device_unregister(&ch->dev);
		goto error;
	}

	dev_info(&chan->dev, "Camera diagnostics channel ready\n");
	return 0;

error:
	dev_err(&chan->dev, "Probe failed with error: %d\n", err);
	/* Destroy mutex */
	mutex_destroy(&ch->mutex);
	/* No need to manually free ch as it was allocated with devm_kzalloc */
	return err;
}

/**
 * @brief Remove function for camera-diagnostics driver.
 *
 * This function is called when the IVC channel for camera diagnostics is removed.
 * It cleans up all resources allocated during probe, releases ISP SDL diagnostics,
 * and removes sysfs interfaces.
 *
 * @param[in] chan IVC channel to remove.
 */
static void camera_diag_remove(struct tegra_ivc_channel *chan)
{
	struct camera_diag_channel *ch = tegra_ivc_channel_get_drvdata(chan);

	dev_dbg(&chan->dev, "Removing camera diagnostics driver\n");

	if (ch == NULL) {
		dev_err(&chan->dev, "Channel is NULL, nothing to remove\n");
		return;
	}

	/* Remove sysfs attribute group */
	sysfs_remove_group(&ch->dev.kobj, &camera_diag_attr_group);

	/* Unregister the device */
	device_unregister(&ch->dev);

	dev_dbg(&chan->dev, "Releasing diagnostics resources\n");
	camera_diag_channel_deinit(ch);

	dev_dbg(&chan->dev, "Destroying mutex\n");
	mutex_destroy(&ch->mutex);

	dev_dbg(&chan->dev, "Clearing driver data\n");
	tegra_ivc_channel_set_drvdata(chan, NULL);

	/* No need to free ch as it was allocated with devm_kzalloc */

	dev_info(&chan->dev, "Camera diagnostics driver removed\n");
}

/**
 * @brief Suspend function for camera-diagnostics driver.
 *
 * This function is called when the system is suspending. It stops
 * any active ISP SDL diagnostics and saves the current state to
 * restore it on resume.
 *
 * Uses the same stop API function (camera_diag_isp_sdl_release) that
 * is called when user writes "stop" to the sysfs control interface.
 *
 * @param[in] dev Device to suspend.
 *
 * @retval 0        Success
 * @retval Other    Error codes from camera_diag_isp_sdl_release
 */
static int camera_diag_pm_suspend(struct device *dev)
{
	struct tegra_ivc_channel *chan = to_tegra_ivc_channel(dev);
	struct camera_diag_channel *ch = tegra_ivc_channel_get_drvdata(chan);
	struct camrtc_diag_isp5_sdl_status_resp status;
	int err = 0;

	dev_dbg(dev, "Suspending camera diagnostics driver\n");

	if (ch == NULL || !ch->is_initialized) {
		dev_err(dev, "Invalid channel handle or not initialized\n");
		return 0; /* Continue with suspend process */
	}

	/* Check if diagnostics are running */
	ch->diag_was_running = false;
	if (ch->num_isp_instances > 0) {
		err = camera_diag_isp_sdl_status(ch, &status, 0);
		if (err == 0 && status.running != 0) {
			ch->diag_was_running = true;

			/* Stop diagnostics using the same API as sysfs control interface */
			dev_info(dev, "Stopping ISP SDL diagnostics for suspend\n");
			err = camera_diag_isp_sdl_release(ch);
			if (err != 0) {
				dev_err(dev, "Failed to stop ISP SDL diagnostics for suspend: %d\n", err);
				/* Continue with suspend process even if this fails */
			}
		}
	}

	dev_dbg(dev, "Camera diagnostics suspended (was_running=%d)\n",
		ch->diag_was_running);
	return 0;
}

/**
 * @brief Resume function for camera-diagnostics driver.
 *
 * This function is called when the system is resuming. It restarts
 * ISP SDL diagnostics if they were running before suspend.
 *
 * Uses the same start API function (camera_diag_isp_sdl_setup) that
 * is called when user writes "start" to the sysfs control interface.
 *
 * @param[in] dev Device to resume.
 *
 * @retval 0        Success
 * @retval Other    Error codes from camera_diag_isp_sdl_setup
 */
static int camera_diag_pm_resume(struct device *dev)
{
	struct tegra_ivc_channel *chan = to_tegra_ivc_channel(dev);
	struct camera_diag_channel *ch = tegra_ivc_channel_get_drvdata(chan);
	int err = 0;

	dev_dbg(dev, "Resuming camera diagnostics driver\n");

	if (ch == NULL || !ch->is_initialized) {
		dev_err(dev, "Invalid channel handle or not initialized\n");
		return 0; /* Continue with resume process */
	}

	/* Restart diagnostics if they were running before suspend */
	if (ch->diag_was_running) {
		/* Start diagnostics using the same API as sysfs control interface */
		dev_info(dev, "Restarting ISP SDL diagnostics after resume\n");
		err = camera_diag_isp_sdl_setup(ch);
		if (err != 0) {
			dev_err(dev, "Failed to restart ISP SDL diagnostics after resume: %d\n", err);
			/* Continue with resume process even if this fails */
		}
	}

	dev_dbg(dev, "Camera diagnostics resumed\n");
	return 0;
}

/* Define PM callbacks */
static const struct dev_pm_ops camera_diag_pm_ops = {
	.suspend = camera_diag_pm_suspend,
	.resume = camera_diag_pm_resume,
};

/* Operations for camera-diagnostics IVC channel */
static const struct tegra_ivc_channel_ops camera_diag_ops = {
	.probe = camera_diag_probe,
	.remove = camera_diag_remove,
	.notify = camera_diag_notify,
};

/* Device match table */
static const struct of_device_id camera_diag_of_match[] = {
	{ .compatible = "nvidia,tegra186-camera-diagnostics" },
	{ },
};
MODULE_DEVICE_TABLE(of, camera_diag_of_match);

/* Driver registration */
static struct tegra_ivc_driver camera_diag_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.bus = &tegra_ivc_bus_type,
		.name = "camera-diagnostics",
		.of_match_table = camera_diag_of_match,
		.pm = &camera_diag_pm_ops,
	},
	.dev_type = &tegra_ivc_channel_type,
	.ops.channel = &camera_diag_ops,
};

tegra_ivc_subsys_driver_default(camera_diag_driver);

MODULE_AUTHOR("NVIDIA Corporation");
MODULE_DESCRIPTION("NVIDIA Tegra Camera Diagnostics driver");
MODULE_LICENSE("GPL v2");
MODULE_SOFTDEP("pre: nvhost_isp5");
