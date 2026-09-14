// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "teec-soc-ivc.h"
#include "teec-soc-plugin.h"
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/printk.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <soc/tegra/virt/hv-ivc.h>

#define NVTZVAULT_ERR(fmt, ...) pr_err("nvtzvault: ivc: " fmt, ##__VA_ARGS__)

#define IVC_TIMEOUT_MS 5000 /* 5 seconds */

struct IvcTeeComm {
	uint32_t ivc_channel_id;
	struct tegra_hv_ivc_cookie *ivc_cookie;
	struct mutex lock;
	struct completion ivc_complete;
	int irq;
	bool initialized;
};

/**
 * @brief IRQ handler for IVC channel - per channel interrupt
 *
 * This handler is called when data is available on the IVC channel.
 * It signals completion to wake up waiting threads.
 *
 * @param[in] irq The interrupt number
 * @param[in] dev_id Pointer to IvcTeeComm structure
 *
 * @return IRQ_HANDLED if interrupt was handled
 */
static irqreturn_t nvtzvault_ivc_irq_handler(int irq, void *dev_id)
{
	struct IvcTeeComm *ivc_tee_comm = (struct IvcTeeComm *)dev_id;

	if (!ivc_tee_comm || !ivc_tee_comm->ivc_cookie) {
		NVTZVAULT_ERR("IRQ handler: invalid parameters\n");
		return IRQ_NONE;
	}

	/* Check if data is available and signal completion */
	if (tegra_hv_ivc_can_read(ivc_tee_comm->ivc_cookie))
		complete(&ivc_tee_comm->ivc_complete);

	return IRQ_HANDLED;
}

static TeeClientStatus teec_comms_init_ivc(void *tee_comms_priv, void *pconfig);
static TeeClientStatus teec_comms_reset_ivc(void *tee_comms_priv);
static TeeClientStatus teec_comms_reset_memory_ivc(void *tee_comms_priv);
static TeeClientStatus teec_comms_send_msg_ivc(void *tee_comms_priv, void *data, size_t size);
static TeeClientStatus teec_comms_wait_event_ivc(void *tee_comms_priv, int64_t timeout_val);
static TeeClientStatus teec_comms_read_msg_ivc(void *tee_comms_priv, void *data, size_t size);
static TeeClientStatus teec_comms_acq_lock_ivc(void *tee_comms_priv);
static TeeClientStatus teec_comms_rel_lock_ivc(void *tee_comms_priv);
static TeeClientStatus teec_comms_deinit_ivc(void *tee_comms_priv);

/**
 * @brief Initialize IVC communication hardware resources
 * @param tee_comms_priv Pointer to IvcTeeComm structure
 * @param pconfig Not used for IVC
 * @return TeeClientStatus status code
 *
 * IMPLEMENTATION DETAILS:
 * - Reserves IVC channel using tegra_hv_ivc_reserve()
 * - Resets channel to clean state before IRQ setup
 * - Registers per-channel IRQ handler for response notifications
 * - Initializes per-channel mutex and completion primitives
 *
 * ERROR CONDITIONS:
 * - Invalid input parameters -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 * - IVC not initialized -> TEE_CLIENT_STATUS_BAD_STATE
 * - IVC channel reservation failure -> TEE_CLIENT_STATUS_GENERIC_ERROR
 * - IRQ registration failure -> TEE_CLIENT_STATUS_GENERIC_ERROR
 * - No valid IRQ available -> TEE_CLIENT_STATUS_GENERIC_ERROR
 */
static TeeClientStatus teec_comms_init_ivc(void *tee_comms_priv, void *pconfig)
{
	struct IvcTeeComm *ivc_tee_comm = (struct IvcTeeComm *)tee_comms_priv;
	long err_code = 0;

	if (!ivc_tee_comm) {
		NVTZVAULT_ERR("Invalid IVC parameters\n");
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;
	}

	/* Reserve IVC channel */
	ivc_tee_comm->ivc_cookie = tegra_hv_ivc_reserve(NULL, ivc_tee_comm->ivc_channel_id, NULL);
	if (IS_ERR_OR_NULL(ivc_tee_comm->ivc_cookie)) {
		err_code = IS_ERR(ivc_tee_comm->ivc_cookie) ?
			   PTR_ERR(ivc_tee_comm->ivc_cookie) : -EINVAL;

		NVTZVAULT_ERR("Failed to reserve IVC channel %u, error: %ld\n",
			      ivc_tee_comm->ivc_channel_id, err_code);
		return TEE_CLIENT_STATUS_GENERIC_ERROR;
	}

	/* Reset channel before setting up IRQ */
	tegra_hv_ivc_channel_reset(ivc_tee_comm->ivc_cookie);

	/* Set up IRQ for this channel */
	if (ivc_tee_comm->ivc_cookie->irq >= 0) {
		int ret = request_irq((unsigned int)ivc_tee_comm->ivc_cookie->irq,
				      nvtzvault_ivc_irq_handler, 0,
				      "nvtzvault_ivc", ivc_tee_comm);
		if (ret) {
			NVTZVAULT_ERR("Failed to request IRQ %d for IVC channel %u: %d\n",
				      ivc_tee_comm->ivc_cookie->irq, ivc_tee_comm->ivc_channel_id, ret);
			return TEE_CLIENT_STATUS_GENERIC_ERROR;
		}
		ivc_tee_comm->irq = ivc_tee_comm->ivc_cookie->irq;
	} else {
		NVTZVAULT_ERR("No valid IRQ for IVC channel %u\n", ivc_tee_comm->ivc_channel_id);
		return TEE_CLIENT_STATUS_GENERIC_ERROR;
	}

	/* Initialize per-channel mutex */
	mutex_init(&ivc_tee_comm->lock);

	/* Initialize completion for IRQ-based waiting */
	init_completion(&ivc_tee_comm->ivc_complete);

	ivc_tee_comm->initialized = true;
	return TEE_CLIENT_STATUS_OK;
}

/**
 * @brief Reset IVC communication channel to clean state
 * @param tee_comms_priv Pointer to IvcTeeComm structure
 * @return TeeClientStatus status code
 *
 * IMPLEMENTATION DETAILS:
 * - Calls tegra_hv_ivc_channel_reset() to reset channel state
 *
 * ERROR CONDITIONS:
 * - Invalid input parameters -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 * - IVC cookie not initialized -> TEE_CLIENT_STATUS_BAD_STATE
 */
static TeeClientStatus teec_comms_reset_ivc(void *tee_comms_priv)
{
	struct IvcTeeComm *ivc_tee_comm = (struct IvcTeeComm *)tee_comms_priv;

	if (!ivc_tee_comm) {
		NVTZVAULT_ERR("Invalid IVC parameters\n");
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;
	}

	if (!ivc_tee_comm->ivc_cookie) {
		NVTZVAULT_ERR("IVC cookie not initialized\n");
		return TEE_CLIENT_STATUS_BAD_STATE;
	}

	tegra_hv_ivc_channel_reset(ivc_tee_comm->ivc_cookie);
	return TEE_CLIENT_STATUS_OK;
}

/**
 * @brief Reset IVC memory regions - no action required for IVC transport
 * @param tee_comms_priv Pointer to IvcTeeComm structure
 * @return TeeClientStatus status code
 *
 * IMPLEMENTATION DETAILS:
 * - No actions for IVC transport
 *
 * ERROR CONDITIONS:
 * - Invalid input parameters -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 */
static TeeClientStatus teec_comms_reset_memory_ivc(void *tee_comms_priv)
{
	struct IvcTeeComm *ivc_tee_comm = (struct IvcTeeComm *)tee_comms_priv;

	if (!ivc_tee_comm) {
		NVTZVAULT_ERR("Invalid IVC parameters\n");
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;
	}

	/* Nothing to reset for IVC */
	return TEE_CLIENT_STATUS_OK;
}

/**
 * @brief Send message data to TEE via IVC channel
 * @param tee_comms_priv Pointer to IvcTeeComm structure
 * @param data Pointer to message data buffer
 * @param size Message data size in bytes
 * @return TeeClientStatus status code
 *
 * IMPLEMENTATION DETAILS:
 * - Validates parameters: null checks, size > 0, size <= ivc frame_size
 * - Waits for channel notification and write readiness (5 second timeout)
 * - Resets completion before sending to prepare for response
 * - Uses tegra_hv_ivc_write() for actual data transmission
 *
 * ERROR CONDITIONS:
 * - Invalid input parameters -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 * - Size exceeds ivc frame size -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 * - Channel notification timeout -> TEE_CLIENT_STATUS_GENERIC_ERROR
 * - Write readiness timeout -> TEE_CLIENT_STATUS_GENERIC_ERROR
 * - IVC write failure -> TEE_CLIENT_STATUS_GENERIC_ERROR
 */
static TeeClientStatus teec_comms_send_msg_ivc(void *tee_comms_priv, void *data, size_t size)
{
	struct IvcTeeComm *ivc_tee_comm = (struct IvcTeeComm *)tee_comms_priv;
	int ret;
	int timeout;

	if (!ivc_tee_comm || !ivc_tee_comm->ivc_cookie || !data || size == 0) {
		NVTZVAULT_ERR("IVC send: bad parameters\n");
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;
	}

	if (size > ivc_tee_comm->ivc_cookie->frame_size) {
		NVTZVAULT_ERR("IVC send: size %zu exceeds ivc frame size %d\n",
			      size, ivc_tee_comm->ivc_cookie->frame_size);
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;
	}

	/* Notify channel */
	timeout = IVC_TIMEOUT_MS * 1000; /* Convert ms to microseconds for udelay */
	while (tegra_hv_ivc_channel_notified(ivc_tee_comm->ivc_cookie) != 0) {
		if (!timeout) {
			NVTZVAULT_ERR("IVC channel %u notification timeout\n",
				      ivc_tee_comm->ivc_channel_id);
			return TEE_CLIENT_STATUS_GENERIC_ERROR;
		}
		udelay(1);
		timeout--;
	}

	/* Now wait for channel to be ready for writing */
	while (tegra_hv_ivc_can_write(ivc_tee_comm->ivc_cookie) == 0) {
		if (!timeout) {
			NVTZVAULT_ERR("IVC channel %u write ready timeout\n",
				      ivc_tee_comm->ivc_channel_id);
			return TEE_CLIENT_STATUS_GENERIC_ERROR;
		}
		udelay(1);
		timeout--;
	}

	/* Reset completion before sending msg */
	reinit_completion(&ivc_tee_comm->ivc_complete);

	/* Send message */
	ret = tegra_hv_ivc_write(ivc_tee_comm->ivc_cookie, data, (int)size);
	if (ret < 0) {
		NVTZVAULT_ERR("IVC write failed: %d\n", ret);
		return TEE_CLIENT_STATUS_GENERIC_ERROR;
	}

	return TEE_CLIENT_STATUS_OK;
}

/**
 * @brief Wait for TEE response completion via IRQ mechanism
 * @param tee_comms_priv Pointer to IvcTeeComm structure
 * @param timeout_val Timeout value in milliseconds (0 = non-blocking, <0 = invalid)
 * @return TeeClientStatus status code
 *
 * IMPLEMENTATION DETAILS:
 * - Waits for TEE response completion using IRQ-based completion mechanism
 * - Uses wait_for_completion_timeout() with millisecond conversion
 * - Supports non-blocking mode when timeout_val = 0
 * - IRQ handler signals completion when data becomes available
 *
 * ERROR CONDITIONS:
 * - Invalid input parameters -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 * - Negative timeout value -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 * - IVC cookie not initialized -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 * - Timeout expired -> TEE_CLIENT_STATUS_TOS_TIMEOUT
 */
static TeeClientStatus teec_comms_wait_event_ivc(void *tee_comms_priv, int64_t timeout_val)
{
	struct IvcTeeComm *ivc_tee_comm = (struct IvcTeeComm *)tee_comms_priv;
	unsigned long result;

	if (!ivc_tee_comm || !ivc_tee_comm->ivc_cookie) {
		NVTZVAULT_ERR("IVC wait: bad parameters\n");
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;
	}

	/* Validate timeout parameter */
	if (timeout_val < 0) {
		NVTZVAULT_ERR("IVC: Invalid negative timeout %lld\n", timeout_val);
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;
	}

	/* If timeout is 0, check if completion is already available */
	if (timeout_val == 0) {
		if (try_wait_for_completion(&ivc_tee_comm->ivc_complete))
			return TEE_CLIENT_STATUS_OK;
		return TEE_CLIENT_STATUS_TOS_TIMEOUT;
	}

	/* Wait for IRQ to signal completion */
	result = wait_for_completion_timeout(&ivc_tee_comm->ivc_complete,
				msecs_to_jiffies((unsigned int)timeout_val));

	if (result > 0)
		return TEE_CLIENT_STATUS_OK;

	/* Wait timed out */
	NVTZVAULT_ERR("IVC response timeout on channel %u\n", ivc_tee_comm->ivc_channel_id);
	return TEE_CLIENT_STATUS_TOS_TIMEOUT;
}

/**
 * @brief Read TEE response message with validation
 * @param tee_comms_priv Pointer to IvcTeeComm structure
 * @param data Pointer to buffer for response data
 * @param size Data size to read in bytes
 * @return TeeClientStatus status code
 *
 * IMPLEMENTATION DETAILS:
 * - Validates parameters: null checks, size > 0, size <= frame_size
 * - Checks data availability using tegra_hv_ivc_can_read()
 * - Uses tegra_hv_ivc_read() for actual data reception
 * - Response readiness already validated by wait_event
 *
 * ERROR CONDITIONS:
 * - Invalid input parameters -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 * - Size exceeds ivc frame size -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 * - IVC cookie not initialized -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 * - No data available -> TEE_CLIENT_STATUS_GENERIC_ERROR
 * - IVC read failure -> TEE_CLIENT_STATUS_GENERIC_ERROR
 */
static TeeClientStatus teec_comms_read_msg_ivc(void *tee_comms_priv, void *data, size_t size)
{
	struct IvcTeeComm *ivc_tee_comm = (struct IvcTeeComm *)tee_comms_priv;
	int ret;

	if (!ivc_tee_comm || !ivc_tee_comm->ivc_cookie || !data || size == 0) {
		NVTZVAULT_ERR("IVC read: bad parameters\n");
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;
	}

	if (size > ivc_tee_comm->ivc_cookie->frame_size) {
		NVTZVAULT_ERR("IVC read: size %zu exceeds ivc frame size %d\n",
			      size, ivc_tee_comm->ivc_cookie->frame_size);
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;
	}

	/* Check if data is available before reading */
	if (!tegra_hv_ivc_can_read(ivc_tee_comm->ivc_cookie)) {
		NVTZVAULT_ERR("IVC read: no data available on channel %u\n",
			      ivc_tee_comm->ivc_channel_id);
		return TEE_CLIENT_STATUS_GENERIC_ERROR;
	}

	/* Read response */
	ret = tegra_hv_ivc_read(ivc_tee_comm->ivc_cookie, data, (int)size);
	if (ret != (int)size) {
		NVTZVAULT_ERR("IVC read failed: %d\n", ret);
		return TEE_CLIENT_STATUS_GENERIC_ERROR;
	}

	return TEE_CLIENT_STATUS_OK;
}

/**
 * @brief Acquire exclusive IVC channel lock
 * @param tee_comms_priv Pointer to IvcTeeComm structure
 * @return TeeClientStatus status code
 *
 * IMPLEMENTATION DETAILS:
 * - Acquires per-channel mutex for serialized access
 * - Blocks other threads attempting to use same IVC channel
 *
 * ERROR CONDITIONS:
 * - Invalid input parameters -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 * - IVC context not initialized -> TEE_CLIENT_STATUS_BAD_STATE
 */
static TeeClientStatus teec_comms_acq_lock_ivc(void *tee_comms_priv)
{
	struct IvcTeeComm *ivc_tee_comm = (struct IvcTeeComm *)tee_comms_priv;

	if (!ivc_tee_comm) {
		NVTZVAULT_ERR("Invalid IVC parameters\n");
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;
	}

	if (!ivc_tee_comm->initialized) {
		NVTZVAULT_ERR("IVC not initialized\n");
		return TEE_CLIENT_STATUS_BAD_STATE;
	}

	mutex_lock(&ivc_tee_comm->lock);
	return TEE_CLIENT_STATUS_OK;
}

/**
 * @brief Release exclusive IVC channel lock
 * @param tee_comms_priv Pointer to IvcTeeComm structure
 * @return TeeClientStatus status code
 *
 * IMPLEMENTATION DETAILS:
 * - Releases per-channel mutex to allow other threads access
 * - Must be called after successful teec_comms_acq_lock_ivc()
 *
 * ERROR CONDITIONS:
 * - Invalid input parameters -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 * - IVC context not initialized -> TEE_CLIENT_STATUS_BAD_STATE
 */
static TeeClientStatus teec_comms_rel_lock_ivc(void *tee_comms_priv)
{
	struct IvcTeeComm *ivc_tee_comm = (struct IvcTeeComm *)tee_comms_priv;

	if (!ivc_tee_comm) {
		NVTZVAULT_ERR("Invalid IVC parameters\n");
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;
	}

	if (!ivc_tee_comm->initialized) {
		NVTZVAULT_ERR("IVC not initialized\n");
		return TEE_CLIENT_STATUS_BAD_STATE;
	}

	mutex_unlock(&ivc_tee_comm->lock);
	return TEE_CLIENT_STATUS_OK;
}

/**
 * @brief Deinitialize IVC communication and cleanup resources
 * @param tee_comms_priv Pointer to IvcTeeComm structure
 * @return TeeClientStatus status code
 *
 * IMPLEMENTATION DETAILS:
 * - Frees registered IRQ handler using free_irq()
 * - Unreserves IVC channel using tegra_hv_ivc_unreserve()
 * - Destroys per channel mutex
 * - Completion does not need to be destroyed
 * - Deallocates IvcTeeComm structure memory
 * - Handles partial initialization cleanup via initialized flag
 *
 * LIFECYCLE NOTE:
 * After calling teec_comms_deinit_ivc(), the interface is fully torn down.
 * To resume communication, teec_initialize_interface_ivc() must be called
 * again before teec_comms_init_ivc() - not just teec_comms_init_ivc() alone.
 *
 * ERROR CONDITIONS:
 * - Invalid input parameters -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 */
static TeeClientStatus teec_comms_deinit_ivc(void *tee_comms_priv)
{
	struct IvcTeeComm *ivc_tee_comm = (struct IvcTeeComm *)tee_comms_priv;

	if (!ivc_tee_comm)
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;

	/* Free IRQ if registered */
	if (ivc_tee_comm->irq >= 0) {
		free_irq(ivc_tee_comm->irq, ivc_tee_comm);
		ivc_tee_comm->irq = -1;
	}

	if (ivc_tee_comm->ivc_cookie) {
		tegra_hv_ivc_unreserve(ivc_tee_comm->ivc_cookie);
		ivc_tee_comm->ivc_cookie = NULL;
	}

	/* Cleanup mutex and completion initialized in teec_comms_init_ivc() */
	if (ivc_tee_comm->initialized) {
		mutex_destroy(&ivc_tee_comm->lock);
		ivc_tee_comm->initialized = false;
	}

	kfree(ivc_tee_comm);
	return TEE_CLIENT_STATUS_OK;
}

/**
 * @brief Setup IVC-based communication interface
 * @param tee_priv Pointer to TeeClient structure to initialize
 * @param dt_node Device tree node containing configuration
 * @return TeeClientStatus status code
 *
 * IMPLEMENTATION DETAILS:
 *
 * 1. MEMORY ALLOCATION:
 *    - Allocates IvcTeeComm structure via kzalloc() (zero-initialized)
 *    - Stores pointer in tee_priv->tee_comms_priv
 *
 * 2. FUNCTION POINTER ASSIGNMENT:
 *    - Maps all function pointers to IVC-specific implementations (*_ivc functions)
 *    - These functions implement complete IVC-based communication protocol
 *
 * 3. DEVICE TREE PARSING:
 *    - Reads "driver-id" property from TA device tree node
 *    - Uses driver-id as IVC channel identifier
 *
 * 4. INITIALIZATION:
 *    - Sets IRQ to invalid state (-1)
 *    - Actual hardware initialization deferred to teec_comms_init_ivc()
 *
 * DEVICE TREE REQUIREMENTS:
 * - TA node must have "driver-id" property with valid u32 value
 * - IVC channels managed by hypervisor (no explicit memory regions needed)
 *
 * USAGE IN NVTZVAULT Driver:
 * - Called during probe to setup communication interface based on DT configuration
 * - Sets up interface for specific TEE service (identified by driver-id as channel)
 *
 * ERROR CONDITIONS:
 * - Invalid input parameters -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 * - Memory allocation failure -> TEE_CLIENT_STATUS_OUT_OF_MEMORY
 * - Missing device tree properties -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 */
TeeClientStatus teec_initialize_interface_ivc(TeeClient *tee_priv, const void *dt_node)
{
	struct IvcTeeComm *ivc_tee_comm;
	const struct device_node *ta_node;
	u32 driver_id;

	ta_node = (const struct device_node *)dt_node;
	if (!tee_priv || !ta_node) {
		NVTZVAULT_ERR("Invalid parameters\n");
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;
	}

	/* Allocate context */
	ivc_tee_comm = kzalloc(sizeof(struct IvcTeeComm), GFP_KERNEL);
	if (!ivc_tee_comm) {
		NVTZVAULT_ERR("Failed to allocate memory\n");
		return TEE_CLIENT_STATUS_OUT_OF_MEMORY;
	}
	tee_priv->tee_comms_priv = ivc_tee_comm;

	/* Initialize IRQ as invalid */
	ivc_tee_comm->irq = -1;

	/* Set function pointers */
	tee_priv->teec_comms_init = teec_comms_init_ivc;
	tee_priv->teec_comms_reset = teec_comms_reset_ivc;
	tee_priv->teec_comms_reset_memory = teec_comms_reset_memory_ivc;
	tee_priv->teec_comms_send_msg = teec_comms_send_msg_ivc;
	tee_priv->teec_comms_wait_event = teec_comms_wait_event_ivc;
	tee_priv->teec_comms_read_msg = teec_comms_read_msg_ivc;
	tee_priv->teec_comms_acq_lock = teec_comms_acq_lock_ivc;
	tee_priv->teec_comms_rel_lock = teec_comms_rel_lock_ivc;
	tee_priv->teec_comms_deinit = teec_comms_deinit_ivc;

	/* Read IVC channel ID from driver-id */
	if (of_property_read_u32(ta_node, "driver-id", &driver_id)) {
		NVTZVAULT_ERR("Failed to read driver-id from DT\n");
		tee_priv->tee_comms_priv = NULL;
		kfree(ivc_tee_comm);
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;
	}

	ivc_tee_comm->ivc_channel_id = driver_id;
	return TEE_CLIENT_STATUS_OK;
}
