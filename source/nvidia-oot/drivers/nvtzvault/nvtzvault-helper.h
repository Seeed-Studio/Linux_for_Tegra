// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#ifndef NVTZVAULT_HELPER_H
#define NVTZVAULT_HELPER_H

#include <linux/types.h>
#include <uapi/misc/nvtzvault-ioctl.h>

#define UINT8_MAX (0xFFU)
#define UINT32_MAX (0xFFFFFFFFU)

#define NVTZVAULT_TEE_PARAM_MAX_COUNT	(8U)

/**
 * @brief Error codes returned by SA operations
 *
 * Defines all possible error codes that can be returned
 * by SA operations, which are then translated to system error codes.
 */
/** @brief Operation completed successfully */
#define TZVAULT_SUCCESS                  0x0U
/** @brief Operation is in pending state */
#define TZVAULT_PENDING                  0x1000U
/** @brief Generic error occurred */
#define TZVAULT_ERROR_GENERIC             0x2000U
/** @brief Access conflict detected */
#define TZVAULT_ERROR_ACCESS_CONFLICT      0x2001U
/** @brief Access denied to requested resource */
#define TZVAULT_ERROR_ACCESS_DENIED        0x2002U
/** @brief Operation needs to be retried */
#define TZVAULT_ERROR_AGAIN               0x2003U
/** @brief Data format is invalid */
#define TZVAULT_ERROR_BAD_FORMAT           0x2004U
/** @brief Invalid parameters provided */
#define TZVAULT_ERROR_BAD_PARAMETERS       0x2005U
/** @brief System is in invalid state for operation */
#define TZVAULT_ERROR_BAD_STATE            0x2006U
/** @brief Resource is busy */
#define TZVAULT_ERROR_BUSY                0x2007U
/** @brief Operation was cancelled */
#define TZVAULT_ERROR_CANCEL              0x2008U
/** @brief Communication error occurred */
#define TZVAULT_ERROR_COMMUNICATION       0x2009U
/** @brief Too much data provided */
#define TZVAULT_ERROR_EXCESS_DATA          0x200AU
/** @brief Requested item not found */
#define TZVAULT_ERROR_ITEM_NOT_FOUND        0x200BU
/** @brief MAC verification failed */
#define TZVAULT_ERROR_MAC_INVALID          0x200CU
/** @brief No data available */
#define TZVAULT_ERROR_NO_DATA              0x200DU
/** @brief No message available */
#define TZVAULT_ERROR_NO_MESSAGE           0x200EU
/** @brief Resource not available */
#define TZVAULT_ERROR_NO_RESOURCE          0x200FU
/** @brief Feature not implemented */
#define TZVAULT_ERROR_NOT_IMPLEMENTED      0x2010U
/** @brief Operation not supported */
#define TZVAULT_ERROR_NOT_SUPPORTED        0x2011U
/** @brief Memory allocation failed */
#define TZVAULT_ERROR_OUT_OF_MEMORY         0x2012U
/** @brief Buffer overflow occurred */
#define TZVAULT_ERROR_OVERFLOW            0x2013U
/** @brief Security violation detected */
#define TZVAULT_ERROR_SECURITY            0x2014U
/** @brief Provided buffer too small */
#define TZVAULT_ERROR_SHORT_BUFFER         0x2015U
/** @brief Signature verification failed */
#define TZVAULT_ERROR_SIGNATURE_INVALID    0x2016U
/** @brief No storage space available */
#define TZVAULT_ERROR_STORAGE_NO_SPACE      0x2017U
/** @brief Target system is dead */
#define TZVAULT_ERROR_TARGET_DEAD          0x2018U
/** @brief System time needs to be reset */
#define TZVAULT_ERROR_TIME_NEEDS_RESET      0x2019U
/** @brief System time not set */
#define TZVAULT_ERROR_TIME_NOT_SET          0x201AU
/** @brief Operation timed out */
#define TZVAULT_ERROR_TIMEOUT             0x201BU

/**
 * @brief Context structure for TEE buffer operations
 *
 * Contains information about the buffer used for communication between
 * kernel driver and SA, including buffer pointer, length and current offset.
 */
struct nvtzvault_tee_buf_context {
	/** @brief Pointer to the communication buffer */
	uint8_t *buf_ptr;
	/** @brief Total length of the buffer in bytes */
	uint32_t buf_len;
	/** @brief Current offset within the buffer for read/write operations */
	uint32_t current_offset;
};

/**
 * @brief Translates SA error codes to system error codes
 *
 * @param[in] tzv_error The SA error code to translate
 *
 * @return The corresponding system error code (negative errno value)
 *         0 on success, negative error code on failure
 */
int nvtzvault_tee_translate_saerror_to_syserror(const uint32_t tzv_error);

/**
 * @brief Writes data to the buffer context with overflow checking
 *
 * @param[in,out] ctx The buffer context to write to
 * @param[in] data Pointer to the data to write
 * @param[in] size Size of the data to write in bytes
 * @param[in] is_user_space True if data pointer is from userspace, false if kernel space
 *
 * @return 0 on success, negative error code on failure:
 *         -EOVERFLOW if write would overflow buffer
 *         -ENOMEM if temporary buffer allocation fails
 *         -EFAULT if userspace copy fails
 */
int nvtzvault_tee_check_overflow_and_write(struct nvtzvault_tee_buf_context *ctx, void *data,
	const uint32_t size, bool is_user_space);

/**
 * @brief Reads data from the buffer context with overflow checking
 *
 * @param[in,out] ctx The buffer context to read from
 * @param[out] data Pointer where read data should be stored
 * @param[in] size Size of the data to read in bytes
 * @param[in] is_user_space True if data pointer is from userspace, false if kernel space
 *
 * @return 0 on success, negative error code on failure:
 *         -EOVERFLOW if read would overflow buffer
 *         -EFAULT if userspace copy fails
 */
int nvtzvault_tee_check_overflow_and_read(struct nvtzvault_tee_buf_context *ctx, void *data,
	const uint32_t size, bool is_user_space);

/**
 * @brief Writes command parameters to the buffer context
 *
 * @param[in,out] ctx The buffer context to write to
 * @param[in] cmd_id Command ID to write
 * @param[in] param_types Parameter types bitmap
 * @param[in] params Array of parameters to write
 *
 * @return 0 on success, negative error code on failure:
 *         -EINVAL if parameter types are invalid
 *         -EFAULT if parameter writing fails
 *         -EOVERFLOW if write would overflow buffer
 */
int nvtzvault_tee_write_all_params(struct nvtzvault_tee_buf_context *ctx, uint32_t cmd_id,
		uint32_t param_types,
		struct nvtzvault_teec_parameter params[NVTZVAULT_TEE_PARAM_MAX_COUNT]);

/**
 * @brief Reads command parameters from the buffer context
 *
 * @param[in,out] ctx The buffer context to read from
 * @param[out] p_param_types Pointer to store parameter types bitmap
 * @param[out] p_cmd_id Pointer to store command ID
 * @param[out] params Array to store read parameters
 *
 * @return 0 on success, negative error code on failure:
 *         -EINVAL if parameter types are invalid
 *         -EFAULT if parameter reading fails
 *         -EOVERFLOW if read would overflow buffer
 */
int nvtzvault_tee_read_all_params(struct nvtzvault_tee_buf_context *ctx, uint32_t *p_param_types,
	uint32_t *p_cmd_id, struct nvtzvault_teec_parameter params[NVTZVAULT_TEE_PARAM_MAX_COUNT]);

/**
 * @brief Initializes the buffer context for TEE operations
 *
 * @param[in,out] ctx The buffer context to initialize
 * @param[in] buf_ptr Pointer to the buffer to use for communication
 * @param[in] buf_len Length of the buffer in bytes
 *
 * @return 0 on success, negative error code on failure
 */
int32_t nvtzvault_tee_buf_context_init(struct nvtzvault_tee_buf_context *ctx,
	void *buf_ptr, uint32_t buf_len);

/**
 * @brief Resets the buffer context for TEE operations
 *
 * @param[in,out] ctx The buffer context to reset
 */
void nvtzvault_tee_buf_context_reset(struct nvtzvault_tee_buf_context *ctx);
#endif
