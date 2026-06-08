// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

/**
 * @file camrtc-diag.h
 *
 * @brief Diagnostic channel definitions.
 */

#ifndef INCLUDE_CAMRTC_DIAG_H
#define INCLUDE_CAMRTC_DIAG_H

#include <soc/tegra/camrtc-common.h>

#pragma GCC diagnostic error "-Wpadded"

/** Diagnostic DMA alignment requirement */
#define CAMRTC_DIAG_DMA_ALIGN_BYTES	64

/** Diagnostic IVC message alignment */
#define CAMRTC_DIAG_IVC_ALIGNOF	MK_ALIGN(8)

/** Diagnostic DMA alignment */
#define CAMRTC_DIAG_DMA_ALIGNOF MK_ALIGN(CAMRTC_DIAG_DMA_ALIGN_BYTES)

/** Diagnostic IVC message alignment specifier */
#define CAMRTC_DIAG_IVC_ALIGN	CAMRTC_ALIGN(CAMRTC_DIAG_IVC_ALIGNOF)

/** Diagnostic DMA alignment specifier */
#define CAMRTC_DIAG_DMA_ALIGN	CAMRTC_ALIGN(CAMRTC_DIAG_DMA_ALIGNOF)

/** Parameter unspecified. */
#define ISP5_SDL_PARAM_UNSPECIFIED	MK_U32(0xFFFFFFFF)

/**
 * @defgroup IspPfsdVersions "ISP PFSD test binary version numbers.
 * @{
 */

/** Version number of the ISP5 PFSD test vector binary. */
#define CAMRTC_DIAG_IS5P_PFSD_VERSION		MK_U32(1553727808)

/** Version number of the ISP6 PFSD test vector binary. */
#define CAMRTC_DIAG_ISP6_PFSD_VERSION		MK_U32(1630655840)

/** @} */

/** Maximum number of diagnostic test vectors */
#define CAMRTC_DIAG_MAX_ISP_PFSDF_NUM_VECTORS	MK_U32(40)

/**
 * @brief Header of an ISP PFSD test binary in shared memory.
 *
 * The header structure describes the version and the contents of the
 * test binary. The header is immediately followed by one or more
 * @ref isp5_sdl_test_descriptor "test descriptors", input images, and
 * finally memory allocations for various buffers. The offsets for each
 * separate memory region are given in the header.
 */
struct isp5_sdl_header {
	/** @ref IspPfsdVersions "ISP PFSD test binary version number" */
	uint32_t version;

	/**
	 * Number of test descriptors following this header
	 * [1, @ref CAMRTC_DIAG_MAX_ISP_PFSDF_NUM_VECTORS].
	 */
	uint32_t num_vectors;

	/** CRC32 on binary payload [0, UINT32_MAX]. */
	uint32_t payload_crc32;

	/**
	 * Byte offset into the test payload after the header
	 * (includes header size) [sizeof(@ref isp5_sdl_header),
	 * @ref camrtc_diag_isp5_sdl_setup_req::size).
	 * Must be a multiple of @ref CAMRTC_DIAG_DMA_ALIGN_BYTES.
	 */
	uint32_t payload_offset;

	/**
	 * Byte offset from start of test payload to start of input images
	 * [sizeof(@ref isp5_sdl_test_descriptor) * @ref num_vectors,
	 * @ref camrtc_diag_isp5_sdl_setup_req::size - @ref payload_offset].
	 * Must be a multiple of @ref CAMRTC_DIAG_DMA_ALIGN_BYTES.
	 */
	uint32_t input_base_offset;

	/**
	 * Byte offset from start of test payload to start of pushbuffer2
	 * allocation [sizeof(@ref isp5_sdl_test_descriptor) * @ref num_vectors,
	 * @ref camrtc_diag_isp5_sdl_setup_req::size - @ref payload_offset].
	 * Must be a multiple of @ref CAMRTC_DIAG_DMA_ALIGN_BYTES.
	 */
	uint32_t push_buffer2_offset;

	/**
	 * Byte offset from start of test payload to start of memory buffers
	 * for the MW[0/1/2] output surfaces
	 * [sizeof(@ref isp5_sdl_test_descriptor) * @ref num_vectors,
	 * @ref camrtc_diag_isp5_sdl_setup_req::size - @ref payload_offset].
	 * Must be a multiple of @ref CAMRTC_DIAG_DMA_ALIGN_BYTES.
	 */
	uint32_t output_buffers_offset;

	/** Reserved. */
	uint32_t reserved__[9];
} CAMRTC_DIAG_DMA_ALIGN;

/**
 * @brief isp5_sdl_test_descriptor - ISP5 SDL binary test descriptor
 */
struct isp5_sdl_test_descriptor {
	/** Zero-index test number [0, @ref num_vectors-1]. */
	uint32_t test_index;

	/**
	 * Input image width in pixels (same for all inputs)
	 * [@ref ISP_MIN_STRIP_WIDTH, @ref ISP_MAX_STRIP_WIDTH].
	 */
	uint16_t input_width;

	/**
	 * Input image height in pixels (same for all inputs)
	 * [@ref ISP_MIN_SLICE_HEIGHT, @ref ISP_MAX_SLICE_HEIGHT].
	 */
	uint16_t input_height;

	/**
	 * Array of offsets to the test vector input images relative to
	 * @ref isp5_sdl_header::input_base_offset
	 * [0, @ref camrtc_diag_isp5_sdl_setup_req::size -
	 * @ref isp5_sdl_header::payload_offset -
	 * @ref isp5_sdl_header::input_base_offset].
	 * Must be a multiple of @ref CAMRTC_DIAG_DMA_ALIGN_BYTES.
	 * Offsets for surfaces 1 and 2 may also be set to
	 * @ref ISP5_SDL_PARAM_UNSPECIFIED.
	 */
	uint32_t input_offset[3];

	/** Golden CRC32 values for MW0, MW1 and MW2 output [0, UINT32_MAX]. */
	uint32_t output_crc32[3];

	/** Reserved. */
	uint32_t reserved__[7];

	/**
	 * Populated ISP push buffer 1 size in dwords [0, 4096]
	 * (see @ref push_buffer1).
	 */
	uint32_t push_buffer1_size;

	/** ISP push buffer 1 */
	uint32_t push_buffer1[4096] CAMRTC_DIAG_DMA_ALIGN;

	/** ISP config buffer. */
	uint8_t config_buffer[128] CAMRTC_DIAG_DMA_ALIGN;
} CAMRTC_DIAG_DMA_ALIGN;

#pragma GCC diagnostic ignored "-Wpadded"

#endif /* INCLUDE_CAMRTC_DIAG_H */
