/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 */

#ifndef _CORE_OS_HAL_H_
#define _CORE_OS_HAL_H_

#include <linux/kernel.h>
#include <linux/types.h>
#include "nvvse-linux-common.h"

/**
 * Defines the true and false values for the boolean variable.
 * True is defined as 0xBABAFACE and false is defined as the
 * complement of true to maximize the hamming distance.
 */
#define NvBoolTrue 0xBABAFACEU
#define NvBoolFalse (~NvBoolTrue)

typedef uint32_t NvBoolVar;

/* Tegra NVVSE crypt context */
typedef struct {
	/* Node ID */
	uint32_t  node_id;
	/* Flag to indicate if the node is a zero copy node */
	NvBoolVar is_zero_copy_node;
} ocb_params_t;

/* This structure is not used in Linux and added to avoid compile errors */
typedef struct {
	uint32_t dummy;
} attr_params_t;

/* Driver context structure */
typedef struct {
	/* Pointer to the message header */
	void *msg_header;
	/* Size of the message header */
	size_t msg_header_size;
	/* Pointer to the source buffer */
	void *src_buffer;
	/* Size of the source buffer */
	size_t src_buffer_size;
	/* Pointer to the AAD buffer */
	void *aad_buffer;
	/* Size of the AAD buffer */
	size_t aad_buffer_size;
	/* Pointer to the digest buffer */
	void *digest_buffer;
	/* Size of the digest buffer */
	size_t digest_buffer_size;
	/* Pointer to the tag buffer */
	void *tag_buffer;
	/* Size of the tag buffer */
	size_t tag_buffer_size;
	/* Pointer to the destination buffer */
	void *dst_buffer;
	/* Size of the destination buffer */
	size_t dst_buffer_size;
	void *validation_result_buffer;
	size_t validation_result_buffer_size;
} driver_context_t;

/* Handle to the NVVSE device */
typedef void *nvvse_handle;

/* NVVSE context structure */
typedef struct {
	nvvse_handle hnvvse;
} nvvse_context;

#endif
