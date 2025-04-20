// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#ifndef OESP_MAILBOX_H
#define OESP_MAILBOX_H

#include <linux/types.h>
#include <linux/device.h>
#include <linux/platform_device.h>

#define UINT8_MAX (0xFFU)
#define UINT32_MAX (0xFFFFFFFFU)

/**
 * @brief Context structure for OESP mailbox operations
 *
 * Contains the virtual address of mailbox registers and
 * IOVA of the HPSE carveout memory used for communication.
 */
struct oesp_mailbox_context {
	/** @brief Virtual address of the OESP mailbox registers */
	uint8_t *reg_base_va;
	/** @brief IOVA (I/O Virtual Address) of HPSE carveout memory */
	uint64_t hpse_carveout_iova;
};

/**
 * @brief Register HPSE mailbox interrupt handler
 *
 * @param[in] dev Platform device pointer
 * @param[in] irq IRQ number to register
 *
 * @return 0 on success, negative error code on failure
 */
int32_t oesp_mailbox_register_irq(struct device *dev, int irq);

/**
 * @brief Sends a request through the OESP mailbox and waits for response
 *
 * Triggers communication with the SA by writing the request to the mailbox
 * registers and waits for the response. This is a blocking call that waits
 * until response is received or timeout occurs.
 *
 * @param[in] buf_ptr Pointer to the buffer containing request data
 * @param[in] buf_len Length of the request data
 * @param[in] task_opcode Task opcode to be sent to the SA
 * @param[in] driver_id Driver ID to be sent to the SA
 * @return 0 on successful response
 *         Negative error code on failure:
 *         -ETIMEDOUT if response not received within timeout period
 */
int32_t oesp_mailbox_send_and_read(void *buf_ptr, uint32_t buf_len, uint32_t task_opcode,
		uint32_t driver_id);

/**
 * @brief Initialize HPSE mailbox context and register interrupt handler
 *
 * @param[in] pdev Platform device pointer
 *
 * @return 0 on success, negative error code on failure
 *         -ENODEV if HPSE node is not found
 *         -EINVAL if any required properties are missing
 *         -ENOMEM if memory allocation fails
 */
int32_t oesp_mailbox_init(struct platform_device *pdev);
#endif
