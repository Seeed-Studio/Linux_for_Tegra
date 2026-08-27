// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "core_common_hal.h"

#include "nvvse-linux-common.h"

#define NVVSE_DEBUG_PRINT 0u
#define NVVSE_ERROR_PRINT 2u

void nvvse_log_str_hex(const char *log_str, uint32_t data, uint32_t level)
{
	if (log_str == NULL) {
		NVVSE_ERR("%s: input string is NULL\n", __func__);
		return;
	}

	if (level == NVVSE_ERROR_PRINT)
		NVVSE_ERR("%s: %x\n", log_str, data);

#if NVVSE_DEBUG_PRINT == 1
	else
		NVVSE_INFO("%s: %x\n", log_str, data);
#endif
}

/**
 * @brief Logs a string message along with a signed integer value to the system log.
 *        This function is used for logging string messages with associated signed integer data,
 *        typically for debugging numerical values or status codes.
 *
 * @param log_str [in] The null-terminated string to be logged. Must not be NULL.
 * @param data [in] The signed integer value to be logged.
 *
 * @return void This function does not return a value.
 */
void nvvse_log_str_int(const char *log_str, int32_t data, uint32_t level)
{
	if (log_str == NULL) {
		NVVSE_ERR("%s: input string is NULL\n", __func__);
		return;
	}

	if (level == NVVSE_ERROR_PRINT)
		NVVSE_ERR("%s: %d\n", log_str, data);

#if NVVSE_DEBUG_PRINT == 1
	else
		NVVSE_INFO("%s: %d\n", log_str, data);
#endif
}

/**
 * @brief Logs a string message along with an unsigned integer value to the system log.
 *        This function is used for logging string messages with associated unsigned integer data,
 *        typically for debugging numerical values, sizes, or counts.
 *
 * @param log_str [in] The null-terminated string to be logged. Must not be NULL.
 * @param data [in] The unsigned integer value to be logged.
 *
 * @return void This function does not return a value.
 */
void nvvse_log_str_uint(const char *log_str, uint32_t data, uint32_t level)
{
	if (log_str == NULL) {
		NVVSE_ERR("%s: input string is NULL\n", __func__);
		return;
	}

	if (level == NVVSE_ERROR_PRINT)
		NVVSE_ERR("%s: %u\n", log_str, data);

#if NVVSE_DEBUG_PRINT == 1
	else
		NVVSE_INFO("%s: %u\n", log_str, data);
#endif
}

/**
 * @brief Logs a string message along with a long unsigned integer value to the system log.
 *        This function is used for logging string messages with associated long unsigned integer
 *        data, typically for debugging large numerical values, addresses, or 64-bit data.
 *
 * @param log_str [in] The null-terminated string to be logged. Must not be NULL.
 * @param data [in] The long unsigned integer value to be logged.
 *
 * @return void This function does not return a value.
 */
void nvvse_log_str_long_uint(const char *log_str, uint64_t data, uint32_t level)
{
	if (log_str == NULL) {
		NVVSE_ERR("%s: input string is NULL\n", __func__);
		return;
	}

	if (level == NVVSE_ERROR_PRINT)
		NVVSE_ERR("%s: %llu\n", log_str, data);

#if NVVSE_DEBUG_PRINT == 1
	else
		NVVSE_INFO("%s: %llu\n", log_str, data);
#endif
}

/**
 * @brief Logs a string message along with two signed integer values to the system log.
 *        This function is used for logging string messages with two associated signed integer
 *        values, typically for debugging pairs of numerical values or comparative data.
 *
 * @param log_str [in] The null-terminated string to be logged. Must not be NULL.
 * @param data1 [in] The first signed integer value to be logged.
 * @param data2 [in] The second signed integer value to be logged.
 *
 * @return void This function does not return a value.
 */
void nvvse_log_str_two_int(const char *log_str, int32_t data1, int32_t data2, uint32_t level)
{
	if (log_str == NULL) {
		NVVSE_ERR("%s: input string is NULL\n", __func__);
		return;
	}

	if (level == NVVSE_ERROR_PRINT)
		NVVSE_ERR("%s: %d, %d\n", log_str, data1, data2);

#if NVVSE_DEBUG_PRINT == 1
	else
		NVVSE_INFO("%s: %d, %d\n", log_str, data1, data2);
#endif
}

/**
 * @brief Logs a string message along with two unsigned integer values to the system log.
 *        This function is used for logging string messages with two associated unsigned integer
 *        values, typically for debugging pairs of numerical values, sizes, or counts.
 *
 * @param log_str [in] The null-terminated string to be logged. Must not be NULL.
 * @param data1 [in] The first unsigned integer value to be logged.
 * @param data2 [in] The second unsigned integer value to be logged.
 *
 * @return void This function does not return a value.
 */
void nvvse_log_str_two_uint(const char *log_str, uint32_t data1, uint32_t data2, uint32_t level)
{
	if (log_str == NULL) {
		NVVSE_ERR("%s: input string is NULL\n", __func__);
		return;
	}

	if (level == NVVSE_ERROR_PRINT)
		NVVSE_ERR("%s: %u, %u\n", log_str, data1, data2);

#if NVVSE_DEBUG_PRINT == 1
	else
		NVVSE_INFO("%s: %u, %u\n", log_str, data1, data2);
#endif
}

/**
 * @brief Logs a string message along with two long unsigned integer values to the system log.
 *        This function is used for logging string messages with two associated long unsigned
 *        integer values, typically for debugging pairs of large numerical values, addresses,
 *        or 64-bit data.
 *
 * @param log_str [in] The null-terminated string to be logged. Must not be NULL.
 * @param data1 [in] The first long unsigned integer value to be logged.
 * @param data2 [in] The second long unsigned integer value to be logged.
 *
 * @return void This function does not return a value.
 */
void nvvse_log_str_two_long_uint(const char *log_str, uint64_t data1, uint64_t data2,
			uint32_t level)
{
	if (log_str == NULL) {
		NVVSE_ERR("%s: input string is NULL\n", __func__);
		return;
	}

	if (level == NVVSE_ERROR_PRINT)
		NVVSE_ERR("%s: %llu, %llu\n", log_str, data1, data2);

#if NVVSE_DEBUG_PRINT == 1
	else
		NVVSE_INFO("%s: %llu, %llu\n", log_str, data1, data2);
#endif
}

void nvvse_log_str_hex_ulong(const char *log_str, uint64_t data, uint32_t level)
{
	if (log_str == NULL) {
		NVVSE_ERR("%s: input string is NULL\n", __func__);
		return;
	}

	if (level == NVVSE_ERROR_PRINT)
		NVVSE_ERR("%s: %llx\n", log_str, data);

#if NVVSE_DEBUG_PRINT == 1
	else
		NVVSE_INFO("%s: %llx\n", log_str, data);
#endif
}

void nvvse_log_str(const char *log_str, uint32_t level)
{
	if (log_str == NULL) {
		NVVSE_ERR("%s: input string is NULL\n", __func__);
		return;
	}

	if (level == NVVSE_ERROR_PRINT)
		NVVSE_ERR("%s\n", log_str);

#if NVVSE_DEBUG_PRINT == 1
	else
		NVVSE_INFO("%s\n", log_str);
#endif
}

void *memory_alloc(size_t size)
{
	void *ptr;

	ptr = kzalloc(size, GFP_KERNEL);

	if (!ptr)
		NVVSE_ERR("%s Failed to allocate %zu bytes\n", __func__, size);

	return ptr;
}

void memory_free(void *ptr)
{
	/* kfree performs NULL check internally, so no null check needed here */
	kfree(ptr);
}

void memory_set(void *ptr, int value, size_t size)
{
	if (ptr)
		memset(ptr, value, size);
}
