// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "teec-soc-mailbox.h"
#include "teec-soc-plugin.h"
#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/memory.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

#define MBOX_FORMAT_FLAG	(0x1+(0x0<<8)+('P'<<16)+('S'<<24))

/* Mailbox Request register offsets */
#define REQ_OPCODE_OFFSET		0x800U
#define REQ_FORMAT_FLAG_OFFSET	0x804U
#define DRIVER_ID_OFFSET	0x808U
#define IOVA_LOW_OFFSET		0x80CU
#define IOVA_HIGH_OFFSET	0x810U
#define MSG_SIZE_OFFSET		0x814U

/* Mailbox Response register offsets */
#define RESP_OPCODE_OFFSET	0x1000U
#define RESP_FORMAT_FLAG_OFFSET	0x1004U
#define RESP_STATUS_OFFSET		0x1008U

/* Mailbox register offset */
#define EXT_CTRL_OFFSET		0x4U
#define PSC_CTRL_REG_OFFSET	0x8U

/* Mailbox register bitfields */
#define MBOX_IN_VALID		0x1U
#define LIC_INTR_EN		0x100U
#define MBOX_OUT_DONE		0x10U
#define MBOX_OUT_VALID		0x1U

#define MAX_MAILBOX		8U


static DECLARE_COMPLETION(mbox_completion);
static DEFINE_MUTEX(g_mbox_mutex);

struct mbox_req {
	u32 task_opcode;
	u32 format_flag;
	u32 tos_driver_id;
	u32 hpse_carveout_iova_lsb;
	u32 hpse_carveout_iova_msb;
	u32 msg_size;
};

struct mbox_resp {
	u32 task_opcode;
	u32 format_flag;
	u32 status;
};

/**
 * @brief Global mailbox hardware state - internal to oesp-mailbox.c
 * Contains shared hardware resources accessed by all TAs
 */
struct mbox_ctx {
	void *hpse_carveout_base_va;
	uint64_t hpse_carveout_base_iova;
	uint64_t hpse_carveout_size;
	void *oesp_mbox_reg_base_va;
	uint64_t oesp_mbox_reg_size;
	struct mbox_resp resp;
	/* Physical/IOVA address of mailbox registers from DT parsing */
	uint64_t oesp_mbox_reg_base_iova;
	int irq;                          /* IRQ number parsed from device tree */
	bool dt_parsed;                   /* True after DT parsing completed */
	bool hw_initialized;              /* True after hardware initialization completed */
	int active_ta_count;              /* Number of active TAs for global state management */
} g_mbox_ctx;


/**
 * @brief Per-device-node context returned as tee_comms_priv
 * Opaque to common driver code, contains everything needed per-TA
 * Hardware resources (carveout, registers) managed globally; mutex provides serialization
 */
struct MbTeeComm {
	/* Per-device-node context */
	uint32_t device_node_id;          /* Device node identifier */
	uint32_t task_opcode;             /* TA opcode from device tree */
	uint32_t tos_driver_id;           /* TA driver ID from device tree */
	bool initialized;                 /* Initialization status */
};

/* Standardized error logging for nvtzvault driver */
#define NVTZVAULT_ERR(...) pr_err("nvtzvault: mailbox: " __VA_ARGS__)

/* Local constants */
#define UINT32_MAX (0xFFFFFFFFU)

/**
 * @brief Check mailbox initialization flags
 * @param mb_tee_comm Pointer to the MbTeeComm structure
 * @return TeeClientStatus status code
 */
static inline TeeClientStatus check_mbox_ready(struct MbTeeComm *mb_tee_comm)
{
	if (unlikely(!mb_tee_comm)) {
		NVTZVAULT_ERR("Invalid context pointer\n");
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;
	}

	if (unlikely(!g_mbox_ctx.dt_parsed)) {
		NVTZVAULT_ERR("Device tree not parsed\n");
		return TEE_CLIENT_STATUS_BAD_STATE;
	}

	if (unlikely(!g_mbox_ctx.hw_initialized)) {
		NVTZVAULT_ERR("Hardware not initialized\n");
		return TEE_CLIENT_STATUS_BAD_STATE;
	}

	if (unlikely(!mb_tee_comm->initialized)) {
		NVTZVAULT_ERR("TA context not initialized\n");
		return TEE_CLIENT_STATUS_BAD_STATE;
	}

	return TEE_CLIENT_STATUS_OK;
}

/**
 * @brief Interrupt handler for HPSE mailbox
 *
 * Handles interrupts from the OESP mailbox by checking status registers
 * and clearing interrupt flags.
 *
 * @param[in] irq The interrupt number
 * @param[in] dev_id The device ID pointer passed during request_irq
 *
 * @return IRQ_HANDLED if interrupt was handled
 *         IRQ_NONE if interrupt was not for this device
 */
static irqreturn_t tegra_hpse_irq_handler(int irq, void *dev_id)
{
	u32 reg_val;
	u8 *oesp_reg_mem_ptr = g_mbox_ctx.oesp_mbox_reg_base_va;
	struct mbox_resp *resp = &g_mbox_ctx.resp;

	/* Read PSC control register to check interrupt status */
	reg_val = readl(oesp_reg_mem_ptr + PSC_CTRL_REG_OFFSET);

	/* Check if this is our interrupt */
	if ((reg_val & MBOX_OUT_VALID) == 0U)
		return IRQ_NONE;

	/* Read response registers */
	resp->task_opcode = readl(oesp_reg_mem_ptr + RESP_OPCODE_OFFSET);
	resp->format_flag = readl(oesp_reg_mem_ptr + RESP_FORMAT_FLAG_OFFSET);
	resp->status = readl(oesp_reg_mem_ptr + RESP_STATUS_OFFSET);

	/* Ensure register read is complete before acknowledging response */
	rmb();

	/* Set MBOX_OUT_DONE to acknowledge response */
	reg_val = readl(oesp_reg_mem_ptr + EXT_CTRL_OFFSET);
	reg_val |= MBOX_OUT_DONE;
	writel(reg_val, oesp_reg_mem_ptr + EXT_CTRL_OFFSET);

	/* Signal completion to waiting thread */
	complete(&mbox_completion);

	return IRQ_HANDLED;
}

static TeeClientStatus teec_comms_init_mb(void *tee_comms_priv, void *pconfig);
static TeeClientStatus teec_comms_reset_mb(void *tee_comms_priv);
static TeeClientStatus teec_comms_reset_memory_mb(void *tee_comms_priv);
static TeeClientStatus teec_comms_send_msg_mb(void *tee_comms_priv, void *data, size_t size);
static TeeClientStatus teec_comms_wait_event_mb(void *tee_comms_priv, int64_t timeout_val);
static TeeClientStatus teec_comms_read_msg_mb(void *tee_comms_priv, void *data, size_t size);
static TeeClientStatus teec_comms_acq_lock_mb(void *tee_comms_priv);
static TeeClientStatus teec_comms_rel_lock_mb(void *tee_comms_priv);
static TeeClientStatus teec_comms_deinit_mb(void *tee_comms_priv);

/**
 * @brief Initialize mailbox communication hardware resources
 * @param tee_comms_priv Pointer to MbTeeComm structure
 * @param pconfig Device pointer for devm_* resource management APIs
 * @return TeeClientStatus status code
 *
 * IMPLEMENTATION DETAILS:
 * - Initialize global mutex for hardware serialization
 * - Maps HPSE carveout data buffer and mailbox control registers
 * - Both mappings use devm_* APIs for automatic cleanup
 * - Stores virtual addresses in global hardware context (g_mbox_ctx)
 * - Device pointer passed for devm_* resource management APIs
 * - Registers IRQ handler for TEE response notifications
 *
 * ERROR CONDITIONS:
 * - Invalid input parameters -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 * - Initialization state errors -> TEE_CLIENT_STATUS_BAD_STATE
 * - Hardware mapping/IRQ failures -> TEE_CLIENT_STATUS_GENERIC_ERROR
 */
static TeeClientStatus teec_comms_init_mb(void *tee_comms_priv, void *pconfig)
{
	struct MbTeeComm *mb_tee_comm = (struct MbTeeComm *)tee_comms_priv;
	struct device *dev = (struct device *)pconfig;
	struct platform_device *pdev;
	TeeClientStatus status = TEE_CLIENT_STATUS_OK;
	u32 reg_val;
	int ret;

	if (!mb_tee_comm) {
		NVTZVAULT_ERR("Invalid context in init\n");
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;
	}

	if (!dev) {
		NVTZVAULT_ERR("Device pointer required for devm_* APIs\n");
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;
	}

	if (!mb_tee_comm->initialized) {
		NVTZVAULT_ERR(
			"MbTeeComm not initialized - teec_initialize_interface must be called first\n");
		return TEE_CLIENT_STATUS_BAD_STATE;
	}

	/* Global hardware initialization - only done once for all TAs */
	mutex_lock(&g_mbox_mutex);
	if (!g_mbox_ctx.dt_parsed) {
		NVTZVAULT_ERR(
			"Device tree not parsed - teec_initialize_interface must be called first\n");
		status = TEE_CLIENT_STATUS_BAD_STATE;
		goto cleanup_mutex;
	}

	if (!g_mbox_ctx.hw_initialized) {
		/* First TA to initialize - set up shared hardware resources */
		pdev = to_platform_device(dev);
		if (!pdev) {
			NVTZVAULT_ERR("Failed to convert device to platform device\n");
			status = TEE_CLIENT_STATUS_GENERIC_ERROR;
			goto cleanup_mutex;
		}

		/* Get IRQ number from platform device */
		g_mbox_ctx.irq = platform_get_irq(pdev, 0);
		if (g_mbox_ctx.irq < 0) {
			NVTZVAULT_ERR("Failed to get IRQ from platform device: %d\n", g_mbox_ctx.irq);
			status = TEE_CLIENT_STATUS_GENERIC_ERROR;
			goto cleanup_mutex;
		}

		/* Map HPSE carveout using devm_* APIs for automatic cleanup */
		g_mbox_ctx.hpse_carveout_base_va = devm_memremap(dev, g_mbox_ctx.hpse_carveout_base_iova,
				g_mbox_ctx.hpse_carveout_size, MEMREMAP_WB);
		if (IS_ERR_OR_NULL(g_mbox_ctx.hpse_carveout_base_va)) {
			NVTZVAULT_ERR("Failed to map HPSE carveout memory\n");
			status = TEE_CLIENT_STATUS_GENERIC_ERROR;
			goto cleanup_mutex;
		}

		/* Map OESP mailbox registers using devm_* APIs for automatic cleanup */
		g_mbox_ctx.oesp_mbox_reg_base_va = devm_ioremap(dev, g_mbox_ctx.oesp_mbox_reg_base_iova,
				g_mbox_ctx.oesp_mbox_reg_size);
		if (!g_mbox_ctx.oesp_mbox_reg_base_va) {
			NVTZVAULT_ERR("Failed to map mailbox registers\n");
			status = TEE_CLIENT_STATUS_GENERIC_ERROR;
			goto cleanup_mutex;
		}

		/* Check if hardware is in clean state - no pending operations */
		reg_val = readl(g_mbox_ctx.oesp_mbox_reg_base_va + PSC_CTRL_REG_OFFSET);
		if (reg_val & (MBOX_IN_VALID | MBOX_OUT_VALID)) {
			NVTZVAULT_ERR(
				"Hardware not in clean state during init (PSC_CTRL=0x%x)\n",
				reg_val);
			NVTZVAULT_ERR("Previous module may have crashed or been forcibly removed\n");
			NVTZVAULT_ERR("Please reboot or reset hardware to recover\n");
			status = TEE_CLIENT_STATUS_BAD_STATE;
			goto cleanup_mutex;
		}

		/* Hardware constraint: Clear carveout memory byte-by-byte (bulk memset prohibited) */
		for (int i = 0; i < g_mbox_ctx.hpse_carveout_size; i++)
			((uint8_t *)g_mbox_ctx.hpse_carveout_base_va)[i] = 0x00;

		/* Request IRQ using devm_* APIs for automatic cleanup */
		ret = devm_request_irq(dev, g_mbox_ctx.irq, tegra_hpse_irq_handler,
			      IRQF_ONESHOT, dev_name(dev), &g_mbox_ctx);
		if (ret) {
			NVTZVAULT_ERR("Failed to request IRQ %d: %d\n", g_mbox_ctx.irq, ret);
			status = TEE_CLIENT_STATUS_GENERIC_ERROR;
			goto cleanup_mutex;
		}

		g_mbox_ctx.hw_initialized = true;
	}

	/* Track active TA count for global state management */
	g_mbox_ctx.active_ta_count++;

cleanup_mutex:
	mutex_unlock(&g_mbox_mutex);
	return status;
}

/**
 * @brief Reset communication channel - not required for mailbox transport
 * @param tee_comms_priv Pointer to MbTeeComm structure
 * @return TeeClientStatus status code
 *
 * IMPLEMENTATION DETAILS:
 * - No reset required for mailbox transport
 *
 * ERROR CONDITIONS:
 * - Invalid input parameters -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 */
static TeeClientStatus teec_comms_reset_mb(void *tee_comms_priv)
{
	struct MbTeeComm *mb_tee_comm = (struct MbTeeComm *)tee_comms_priv;

	/* Validate input parameter */
	if (!mb_tee_comm || !mb_tee_comm->initialized)
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;

	/* Mailbox doesn't require reset - return success */
	return TEE_CLIENT_STATUS_OK;
}

/**
 * @brief Reset and clear mailbox memory regions
 * @param tee_comms_priv Pointer to MbTeeComm structure
 * @return TeeClientStatus status code
 *
 * IMPLEMENTATION DETAILS:
 * - Clears HPSE carveout memory using byte-by-byte loop (hardware constraint)
 * - Clears all mailbox registers to 0x00 using register write
 *
 * ERROR CONDITIONS:
 * - Invalid input parameters -> TEE_CLIENT_STATUS_BAD_STATE
 */
static TeeClientStatus teec_comms_reset_memory_mb(void *tee_comms_priv)
{
	struct MbTeeComm *mb_tee_comm = (struct MbTeeComm *)tee_comms_priv;

	if (!mb_tee_comm || !mb_tee_comm->initialized)
		return TEE_CLIENT_STATUS_BAD_STATE;

	/* Acquire mutex to protect global hardware resources */
	mutex_lock(&g_mbox_mutex);

	/* Clear carveout */
	if (g_mbox_ctx.hpse_carveout_base_va) {
		/* Clear carveout memory byte-by-byte (special carveout memory constraint) */
		for (int i = 0; i < g_mbox_ctx.hpse_carveout_size; i++)
			((uint8_t *)g_mbox_ctx.hpse_carveout_base_va)[i] = 0x00;
	}

	/* Clear only request registers that we write to (avoid read-only response registers) */
	if (g_mbox_ctx.oesp_mbox_reg_base_va) {
		writel(0x0, g_mbox_ctx.oesp_mbox_reg_base_va + REQ_OPCODE_OFFSET);
		writel(0x0, g_mbox_ctx.oesp_mbox_reg_base_va + REQ_FORMAT_FLAG_OFFSET);
		writel(0x0, g_mbox_ctx.oesp_mbox_reg_base_va + DRIVER_ID_OFFSET);
		writel(0x0, g_mbox_ctx.oesp_mbox_reg_base_va + IOVA_LOW_OFFSET);
		writel(0x0, g_mbox_ctx.oesp_mbox_reg_base_va + IOVA_HIGH_OFFSET);
		writel(0x0, g_mbox_ctx.oesp_mbox_reg_base_va + MSG_SIZE_OFFSET);
		/* Note: EXT_CTRL_OFFSET not cleared as it's read-modify-write with hardware state */
	}

	mutex_unlock(&g_mbox_mutex);

	return TEE_CLIENT_STATUS_OK;
}

/**
 * @brief Send message data to TEE via mailbox
 * @param tee_comms_priv Pointer to MbTeeComm structure
 * @param data Pointer to message data buffer
 * @param size Message data size in bytes
 * @return TeeClientStatus status code
 *
 * IMPLEMENTATION DETAILS:
 * - Validates parameters: null checks, size > 0
 * - Checks mailbox availability: reads the mailbox register, fails if busy.
 * - Copies data to carveout
 * - Trigger transmission
 *
 * ERROR CONDITIONS:
 * - Invalid input parameters -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 * - Not initialized properly -> TEE_CLIENT_STATUS_BAD_STATE
 * - Mailbox busy -> TEE_CLIENT_STATUS_BAD_STATE
 * - Size overflow -> TEE_CLIENT_STATUS_GENERIC_ERROR
 */
static TeeClientStatus teec_comms_send_msg_mb(void *tee_comms_priv, void *data, size_t size)
{
	struct MbTeeComm *mb_tee_comm = (struct MbTeeComm *)tee_comms_priv;
	struct mbox_req req;
	TeeClientStatus status;
	u8 *oesp_reg_mem_ptr;
	u32 reg_val;

	/* Check if everything is ready */
	status = check_mbox_ready(mb_tee_comm);
	if (status != TEE_CLIENT_STATUS_OK)
		return status;

	/* PROTOCOL REQUIREMENT: Validate parameters - null checks, size > 0 */
	if (!data || size == 0) {
		NVTZVAULT_ERR("Invalid data parameters in send_msg\n");
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;
	}

	/* PROTOCOL REQUIREMENT: Check mailbox availability - reads mailbox register, fails if busy */
	oesp_reg_mem_ptr = g_mbox_ctx.oesp_mbox_reg_base_va;
	reg_val = readl(oesp_reg_mem_ptr + PSC_CTRL_REG_OFFSET);
	if ((reg_val & MBOX_IN_VALID) != 0) {
		NVTZVAULT_ERR("Hardware busy - MBOX_IN_VALID set (0x%x)\n", reg_val);
		return TEE_CLIENT_STATUS_BAD_STATE;
	}

	/* PROTOCOL REQUIREMENT: Size overflow check */
	if (size > g_mbox_ctx.hpse_carveout_size) {
		NVTZVAULT_ERR("Message size %zu exceeds carveout size %llu\n",
				size, g_mbox_ctx.hpse_carveout_size);
		return TEE_CLIENT_STATUS_GENERIC_ERROR;
	}

	reinit_completion(&mbox_completion);

	/* Send request */
	req.task_opcode = mb_tee_comm->task_opcode;
	writel(req.task_opcode, oesp_reg_mem_ptr + REQ_OPCODE_OFFSET);

	req.format_flag = MBOX_FORMAT_FLAG;
	writel(req.format_flag, oesp_reg_mem_ptr + REQ_FORMAT_FLAG_OFFSET);

	req.tos_driver_id = mb_tee_comm->tos_driver_id;
	writel(req.tos_driver_id, oesp_reg_mem_ptr + DRIVER_ID_OFFSET);

	/* Hardware constraint: Copy data byte-by-byte to HPSE carveout (no bulk operations) */
	for (int i = 0; i < size; i++)
		((uint8_t *)g_mbox_ctx.hpse_carveout_base_va)[i] = ((uint8_t *)data)[i];

	req.hpse_carveout_iova_lsb = g_mbox_ctx.hpse_carveout_base_iova & UINT32_MAX;
	writel(req.hpse_carveout_iova_lsb, oesp_reg_mem_ptr + IOVA_LOW_OFFSET);

	req.hpse_carveout_iova_msb = g_mbox_ctx.hpse_carveout_base_iova >> 32U;
	writel(req.hpse_carveout_iova_msb, oesp_reg_mem_ptr + IOVA_HIGH_OFFSET);

	req.msg_size = size;
	writel(req.msg_size, oesp_reg_mem_ptr + MSG_SIZE_OFFSET);

	reg_val = readl(oesp_reg_mem_ptr + EXT_CTRL_OFFSET);

	writel((reg_val | MBOX_IN_VALID | LIC_INTR_EN), oesp_reg_mem_ptr + EXT_CTRL_OFFSET);

	/* Ensure write buffer is flushed before returning */
	wmb();

	return TEE_CLIENT_STATUS_OK;
}

/**
 * @brief Wait for TEE response completion via IRQ mechanism
 * @param tee_comms_priv Pointer to MbTeeComm structure
 * @param timeout_val Timeout value in milliseconds (0 = non-blocking, <0 = invalid)
 * @return TeeClientStatus status code
 *
 * IMPLEMENTATION DETAILS:
 * - Waits for TEE response completion using IRQ-based completion mechanism
 *
 * ERROR CONDITIONS:
 * - Invalid input parameters -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 * - Not initialized properly -> TEE_CLIENT_STATUS_BAD_STATE
 * - Timeout expired -> TEE_CLIENT_STATUS_TOS_TIMEOUT
 */
static TeeClientStatus teec_comms_wait_event_mb(void *tee_comms_priv, int64_t timeout_val)
{
	struct MbTeeComm *mb_tee_comm = (struct MbTeeComm *)tee_comms_priv;
	unsigned long result;
	TeeClientStatus status;

	status = check_mbox_ready(mb_tee_comm);
	if (status != TEE_CLIENT_STATUS_OK)
		return status;

	/* Validate timeout parameter */
	if (timeout_val < 0) {
		NVTZVAULT_ERR("Invalid negative timeout %lld\n", (long long)timeout_val);
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;
	}

	/* If timeout is 0, check if completion is already available */
	if (timeout_val == 0) {
		if (try_wait_for_completion(&mbox_completion))
			return TEE_CLIENT_STATUS_OK;
		return TEE_CLIENT_STATUS_TOS_TIMEOUT;
	}

	/* Wait for IRQ to signal completion */
	result = wait_for_completion_timeout(&mbox_completion,
				msecs_to_jiffies((unsigned int)timeout_val));

	if (result > 0)
		return TEE_CLIENT_STATUS_OK;

	/* Wait timed out */
	return TEE_CLIENT_STATUS_TOS_TIMEOUT;
}

/**
 * @brief Read TEE response message with validation
 * @param tee_comms_priv Pointer to MbTeeComm structure
 * @param data Pointer to buffer for response data
 * @param size Data size to read in bytes
 * @return TeeClientStatus status code
 *
 * IMPLEMENTATION DETAILS:
 * - Validates parameters: same checks as send_msg
 * - Copies data from carveout
 *
 * ERROR CONDITIONS:
 * - Invalid input parameters -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 * - Not initialized properly -> TEE_CLIENT_STATUS_BAD_STATE
 * - TEE operation errors -> TEE_CLIENT_STATUS_GENERIC_ERROR
 */
static TeeClientStatus teec_comms_read_msg_mb(void *tee_comms_priv, void *data, size_t size)
{
	struct MbTeeComm *mb_tee_comm = (struct MbTeeComm *)tee_comms_priv;
	TeeClientStatus status;

	/* Check if everything is ready */
	status = check_mbox_ready(mb_tee_comm);
	if (status != TEE_CLIENT_STATUS_OK)
		return status;

	if (!data || size == 0) {
		NVTZVAULT_ERR("Invalid data parameters in read_msg\n");
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;
	}

	/* Check buffer size against carveout (response is in shared memory) */
	if (size > g_mbox_ctx.hpse_carveout_size) {
		NVTZVAULT_ERR("Read size %zu exceeds carveout size %llu\n",
				size, (unsigned long long)g_mbox_ctx.hpse_carveout_size);
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;
	}

	/* Response readiness already validated by wait_event - proceed with data validation */

	/* Check response status from IRQ handler */
	if (g_mbox_ctx.resp.status != 0U) {
		NVTZVAULT_ERR("TEE returned error status: %u\n", g_mbox_ctx.resp.status);
		return TEE_CLIENT_STATUS_GENERIC_ERROR;
	}

	/* Validate response format */
	if (g_mbox_ctx.resp.format_flag != MBOX_FORMAT_FLAG) {
		NVTZVAULT_ERR("Invalid response format flag: 0x%x\n", g_mbox_ctx.resp.format_flag);
		return TEE_CLIENT_STATUS_GENERIC_ERROR;
	}

	/* Verify response matches our request */
	if (g_mbox_ctx.resp.task_opcode != mb_tee_comm->task_opcode) {
		NVTZVAULT_ERR("Response opcode mismatch: expected 0x%x, got 0x%x\n",
				mb_tee_comm->task_opcode, g_mbox_ctx.resp.task_opcode);
		return TEE_CLIENT_STATUS_GENERIC_ERROR;
	}

	/* Hardware constraint: Read response data byte-by-byte from HPSE carveout */
	for (size_t i = 0; i < size; i++)
		((uint8_t *)data)[i] = ((uint8_t *)g_mbox_ctx.hpse_carveout_base_va)[i];

	return TEE_CLIENT_STATUS_OK;
}

/**
 * @brief Acquire exclusive mailbox hardware lock
 * @param tee_comms_priv Pointer to MbTeeComm structure
 * @return TeeClientStatus status code
 *
 * IMPLEMENTATION DETAILS:
 * - Applies the lock, mutex_lock on global hardware mutex
 * - Blocks other threads on the lock
 *
 * ERROR CONDITIONS:
 * - Invalid input parameters -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 * - Not initialized properly -> TEE_CLIENT_STATUS_BAD_STATE
 */
static TeeClientStatus teec_comms_acq_lock_mb(void *tee_comms_priv)
{
	struct MbTeeComm *mb_tee_comm = (struct MbTeeComm *)tee_comms_priv;
	TeeClientStatus status;

	/* Check if everything is ready */
	status = check_mbox_ready(mb_tee_comm);
	if (status != TEE_CLIENT_STATUS_OK)
		return status;

	/* Acquire global hardware lock for single mailbox serialization */
	mutex_lock(&g_mbox_mutex);
	return TEE_CLIENT_STATUS_OK;
}

/**
 * @brief Release exclusive mailbox hardware lock
 * @param tee_comms_priv Pointer to MbTeeComm structure
 * @return TeeClientStatus status code
 *
 * IMPLEMENTATION DETAILS:
 * - Calls mutex_unlock on global hardware mutex
 *
 * ERROR CONDITIONS:
 * - Invalid input parameters -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 * - Not initialized properly -> TEE_CLIENT_STATUS_BAD_STATE
 */
static TeeClientStatus teec_comms_rel_lock_mb(void *tee_comms_priv)
{
	struct MbTeeComm *mb_tee_comm = (struct MbTeeComm *)tee_comms_priv;
	TeeClientStatus status;

	/* Check if everything is ready */
	status = check_mbox_ready(mb_tee_comm);
	if (status != TEE_CLIENT_STATUS_OK)
		return status;

	/* Release global hardware lock */
	mutex_unlock(&g_mbox_mutex);

	return TEE_CLIENT_STATUS_OK;
}

/**
 * @brief Deinitialize mailbox communication and cleanup resources
 * @param tee_comms_priv Pointer to MbTeeComm structure
 * @return TeeClientStatus status code
 *
 * IMPLEMENTATION DETAILS:
 * - Hardware cleanup: devm_* APIs handle automatic cleanup
 * - Memory mappings automatically unmapped by kernel
 *
 * 2. Lock mechanism cleanup:
 *    - Global mutex persists (no per-TA cleanup needed)
 *
 * 3. Memory deallocation:
 *    - kfree(tee_comms_priv), and any pointer structures inside tee_comms_priv.
 *
 * LIFECYCLE NOTE:
 * After calling teec_comms_deinit(), the interface is fully torn down.
 * To resume communication, teec_initialize_interface() must be called
 * again before teec_comms_init() - not just teec_comms_init() alone.
 *
 * ERROR CONDITIONS:
 * - Invalid input parameters -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 * - Reference count underflow -> TEE_CLIENT_STATUS_BAD_STATE
 */
static TeeClientStatus teec_comms_deinit_mb(void *tee_comms_priv)
{
	struct MbTeeComm *mb_tee_comm = (struct MbTeeComm *)tee_comms_priv;
	uint32_t device_node_id;

	if (!mb_tee_comm)
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;

	device_node_id = mb_tee_comm->device_node_id;

	/* Global mutex persists across all TAs (no per-TA cleanup needed) */
	/* Track TA lifecycle and manage global state under mutex protection */
	mutex_lock(&g_mbox_mutex);

	/* Cleanup per-TA context */
	mb_tee_comm->initialized = false;
	kfree(mb_tee_comm);

	/* Reference counting: Decrement active TA count */
	if (WARN_ON(g_mbox_ctx.active_ta_count <= 0)) {
		NVTZVAULT_ERR("BUG - attempting to decrement zero/negative TA count (%d)\n",
				g_mbox_ctx.active_ta_count);
		mutex_unlock(&g_mbox_mutex);
		return TEE_CLIENT_STATUS_BAD_STATE;
	}
	g_mbox_ctx.active_ta_count--;

	/* Last TA cleanup: Reset global hardware state */
	if (g_mbox_ctx.active_ta_count == 0) {

		/* devm_* APIs handle automatic cleanup, just clear pointers */
		g_mbox_ctx.oesp_mbox_reg_base_va = NULL;
		g_mbox_ctx.hpse_carveout_base_va = NULL;
		g_mbox_ctx.dt_parsed = false;
		g_mbox_ctx.hw_initialized = false;
	}

	mutex_unlock(&g_mbox_mutex);
	return TEE_CLIENT_STATUS_OK;
}

/**
 * @brief Setup mailbox-based communication interface
 * @param tee_priv Pointer to TeeClient structure to initialize
 * @param dt_node Device tree node containing configuration
 * @return TeeClientStatus status code
 *
 * IMPLEMENTATION DETAILS:
 *
 * 1. MEMORY ALLOCATION:
 *    - Allocates MbTeeComm structure via kzalloc() (zero-initialized)
 *    - Stores pointer in tee_priv->tee_comms_priv
 *
 * 2. FUNCTION POINTER ASSIGNMENT:
 *    - Maps all function pointers to mailbox-specific implementations (*_mb functions)
 *    - These functions implement complete mailbox based communication protocol
 *    - Some functions are stub implementations (reset)
 *
 * 3. DEVICE TREE PARSING:
 *    - Parses global hardware resources once, per-TA properties separately
 *    - Reads "/reserved-memory/hpse-carveout" node directly
 *      * Extracts physical address (hpse_carveout_base_iova) and size (hpse_carveout_size)
 *      * Read device tree for register parsing
 *    - Reads communication properties from dt_node
 *      * "op-code" property -> task_opcode
 *      * "driver-id" property -> tos_driver_id
 *      * These values identify the specific TEE service
 *
 * 4. VALIDATION:
 *    - Checks for successful device tree parsing
 *    - Validates all required properties are present
 *    - Returns appropriate error codes for missing configuration
 *
 * DEVICE TREE REQUIREMENTS:
 * - Must have "/reserved-memory/hpse-carveout" node with reg property
 * - Communication node must have "op-code" and "driver-id" properties
 * - Properties must be valid 32-bit values
 *
 * USAGE IN TOS Driver:
 * - Called during initialise to setup communication interface based on DT configuration
 * - Sets up interface for specific TEE service (identified by op-code/driver-id)
 *
 * ERROR CONDITIONS:
 * - Invalid input parameters -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 * - Memory allocation failure -> TEE_CLIENT_STATUS_OUT_OF_MEMORY
 * - Missing device tree nodes/properties -> TEE_CLIENT_STATUS_BAD_PARAMETERS
 */
TeeClientStatus teec_initialize_interface_mailbox(TeeClient *tee_priv, const void *dt_node)
{
	struct MbTeeComm *mb_tee_comm;
	const struct device_node *ta_node;
	struct device_node *nvtzvault_node;
	struct device_node *hpse_carveout_node = NULL;
	struct device_node *oesp_mbox_node = NULL;
	uint32_t task_opcode;
	uint32_t tos_driver_id;
	TeeClientStatus status = TEE_CLIENT_STATUS_OK;

	ta_node = (const struct device_node *)dt_node;
	if (!tee_priv || !ta_node) {
		NVTZVAULT_ERR("Invalid parameters in teec_initialize_interface\n");
		return TEE_CLIENT_STATUS_BAD_PARAMETERS;
	}


	/* Memory allocation */
	mb_tee_comm = kzalloc(sizeof(struct MbTeeComm), GFP_KERNEL);
	if (!mb_tee_comm) {
		NVTZVAULT_ERR("Failed to allocate MbTeeComm\n");
		return TEE_CLIENT_STATUS_OUT_OF_MEMORY;
	}
	tee_priv->tee_comms_priv = mb_tee_comm;


	/* Function pointer assignment */
	tee_priv->teec_comms_init = teec_comms_init_mb;
	tee_priv->teec_comms_reset = teec_comms_reset_mb;
	tee_priv->teec_comms_reset_memory = teec_comms_reset_memory_mb;
	tee_priv->teec_comms_send_msg = teec_comms_send_msg_mb;
	tee_priv->teec_comms_wait_event = teec_comms_wait_event_mb;
	tee_priv->teec_comms_read_msg = teec_comms_read_msg_mb;
	tee_priv->teec_comms_acq_lock = teec_comms_acq_lock_mb;
	tee_priv->teec_comms_rel_lock = teec_comms_rel_lock_mb;
	tee_priv->teec_comms_deinit = teec_comms_deinit_mb;


	/* Parse global hardware resources once, per-TA properties separately */
	mutex_lock(&g_mbox_mutex);
	if (!g_mbox_ctx.dt_parsed) {
		hpse_carveout_node = of_find_node_by_path("/reserved-memory/hpse-carveout");
		if (!hpse_carveout_node) {
			NVTZVAULT_ERR("hpse-carveout node missing in DT\n");
			status = TEE_CLIENT_STATUS_BAD_PARAMETERS;
			goto cleanup_mutex;
		}

		if (of_property_read_u64(hpse_carveout_node, "reg",
				&g_mbox_ctx.hpse_carveout_base_iova)) {
			NVTZVAULT_ERR("reg property missing in hpse-carveout\n");
			status = TEE_CLIENT_STATUS_BAD_PARAMETERS;
			goto cleanup_mutex;
		}

		if (of_property_read_u64_index(hpse_carveout_node, "reg", 1,
				&g_mbox_ctx.hpse_carveout_size)) {
			NVTZVAULT_ERR("reg size missing in hpse-carveout\n");
			status = TEE_CLIENT_STATUS_BAD_PARAMETERS;
			goto cleanup_mutex;
		}

		if (!g_mbox_ctx.hpse_carveout_base_iova || !g_mbox_ctx.hpse_carveout_size) {
			NVTZVAULT_ERR("Invalid hpse carveout parameters\n");
			status = TEE_CLIENT_STATUS_BAD_PARAMETERS;
			goto cleanup_mutex;
		}

		nvtzvault_node = ta_node->parent;
		oesp_mbox_node = of_find_node_by_name(nvtzvault_node, "oesp-mailbox");
		if (!oesp_mbox_node) {
			NVTZVAULT_ERR("oesp-mailbox node missing in DT\n");
			status = TEE_CLIENT_STATUS_BAD_PARAMETERS;
			goto cleanup_mutex;
		}

		if (of_property_read_u64(oesp_mbox_node, "reg", &g_mbox_ctx.oesp_mbox_reg_base_iova)) {
			NVTZVAULT_ERR("reg property missing for oesp mailbox\n");
			status = TEE_CLIENT_STATUS_BAD_PARAMETERS;
			goto cleanup_mutex;
		}

		if (of_property_read_u64_index(oesp_mbox_node, "reg", 1,
				&g_mbox_ctx.oesp_mbox_reg_size)) {
			NVTZVAULT_ERR("reg size missing for oesp mailbox\n");
			status = TEE_CLIENT_STATUS_BAD_PARAMETERS;
			goto cleanup_mutex;
		}

		if (!g_mbox_ctx.oesp_mbox_reg_base_iova || !g_mbox_ctx.oesp_mbox_reg_size) {
			NVTZVAULT_ERR("Invalid oesp mailbox parameters\n");
			status = TEE_CLIENT_STATUS_BAD_PARAMETERS;
			goto cleanup_mutex;
		}

		g_mbox_ctx.dt_parsed = true;

		of_node_put(hpse_carveout_node);
		of_node_put(oesp_mbox_node);
		hpse_carveout_node = NULL;
		oesp_mbox_node = NULL;
	}
	mutex_unlock(&g_mbox_mutex);

	/* Parse per-TA properties from individual TA device tree nodes */
	if (of_property_read_u32(ta_node, "op-code", &task_opcode)) {
		NVTZVAULT_ERR("Failed to read op-code from DT\n");
		status = TEE_CLIENT_STATUS_BAD_PARAMETERS;
		goto cleanup_mem;
	}

	if (of_property_read_u32(ta_node, "driver-id", &tos_driver_id)) {
		NVTZVAULT_ERR("Failed to read driver-id from DT\n");
		status = TEE_CLIENT_STATUS_BAD_PARAMETERS;
		goto cleanup_mem;
	}

	mb_tee_comm->device_node_id = tos_driver_id;
	mb_tee_comm->task_opcode = task_opcode;
	mb_tee_comm->tos_driver_id = tos_driver_id;
	mb_tee_comm->initialized = true;

	return status;

cleanup_mutex:
	/* Cleanup device tree node references on error paths */
	if (hpse_carveout_node)
		of_node_put(hpse_carveout_node);
	if (oesp_mbox_node)
		of_node_put(oesp_mbox_node);
	mutex_unlock(&g_mbox_mutex);
cleanup_mem:
	kfree(mb_tee_comm);
	tee_priv->tee_comms_priv = NULL;
	return status;
}
