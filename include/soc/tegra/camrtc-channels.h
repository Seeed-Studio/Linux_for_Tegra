/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
 */

/**
 * @file camrtc-channels.h
 *
 * @brief RCE channel setup tags & structures.
 */

#ifndef INCLUDE_CAMRTC_CHANNELS_H
#define INCLUDE_CAMRTC_CHANNELS_H

#include "camrtc-common.h"

/**
 * @brief Create 64-bit TLV tag value from characters.
 *
 * Combine 8 character values into a 64-bit tag value.
 *
 * @pre None.
 *
 * @param	s0	1st character (LSB) [0,UINT8_MAX]
 * @param	s1	2nd character [0,UINT8_MAX]
 * @param	s2	3rd character [0,UINT8_MAX]
 * @param	s3	4th character [0,UINT8_MAX]
 * @param	s4	5th character [0,UINT8_MAX]
 * @param	s5	6th character [0,UINT8_MAX]
 * @param	s6	7th character [0,UINT8_MAX]
 * @param	s7	8th character (MSB) [0,UINT8_MAX]
 *
 * @return		64-bit TLV tag value (uint64_t)
 */
#define CAMRTC_TAG64(s0, s1, s2, s3, s4, s5, s6, s7) ( \
	((uint64_t)(s0) << 0U) | ((uint64_t)(s1) << 8U) | \
	((uint64_t)(s2) << 16U) | ((uint64_t)(s3) << 24U) | \
	((uint64_t)(s4) << 32U) | ((uint64_t)(s5) << 40U) | \
	((uint64_t)(s6) << 48U) | ((uint64_t)(s7) << 56U))

/**
 * @defgroup RceTags Command tags for shared memory setup
 * @{
 */

/**
 * @ref RceTags "Command tag" for IVC channel setup.  In this case, the TLV
 * instance is a @ref camrtc_tlv_ivc_setup "IVC setup header" with IVC channel
 * parameters. Multiple such TLV entries may provided as a list to configure
 * multiple IVC channels. The TLV list must be terminated with the
 * @ref CAMRTC_TAG_TLV_NULL command tag.
 */
#define CAMRTC_TAG_IVC_SETUP	CAMRTC_TAG64('I', 'V', 'C', '-', 'S', 'E', 'T', 'U')

/**
 * @ref RceTags "Command tag" for trace buffer setup. In this case, the TLV
 * instance is a @ref camrtc_trace_memory_header "trace memory header" and will
 * not be followed by any additional TLV entries. This tag is used to set up
 * a trace buffer and to indicate that the trace receiver does not support
 * string format console prints via the trace system. In this case, RCE FW will
 * route any console prints to another console port, such as TCU, if available.
 * (non-safety)
 */
#define CAMRTC_TAG_NV_TRACE	CAMRTC_TAG64('N', 'V', ' ', 'T', 'R', 'A', 'C', 'E')

/**
 * @ref RceTags "Command tag" for trace buffer setup. In this case, the TLV
 * is a @ref camrtc_trace_memory_header "trace memory header" and will
 * not be followed by any additional TLV entries. This tag is used to set up
 * a trace buffer and to indicate that the trace receiver additionally support
 * console prints via the trace system, and will route any string format log
 * and debug prints to the console and/or store them in an appropriate system
 * log. (non-safety)
 */
#define CAMRTC_TAG_NV_TRCON	CAMRTC_TAG64('N', 'V', ' ', 'T', 'R', 'C', 'O', 'N')

/**
 * @ref RceTags "Command tag" for falcon coverage buffer setup. In this case, the
 * TLV isa @ref camrtc_coverage_memory_header "coverage memory header" and will
 * not be followed by any additional TLV entries. (non-safety)
 */
#define CAMRTC_TAG_NV_COVERAGE	CAMRTC_TAG64('N', 'V', ' ', 'C', 'O', 'V', 'E', 'R')

/**
 * @ref RceTags "Command tag" for terminating a TLV list.
 */
#define CAMRTC_TAG_NV_NULL	MK_U64(0)
/** @} */

/**
 * @brief RCE Tag, length, and value (TLV) descriptor.
 *
 * The structure is followed in memory by a variable length payload.
 * The generic TLV descriptors may be concatenated into a list, which
 * must be terminated with the @ref CAMRTC_TAG_NV_NULL "NULL TLV".
 */
struct camrtc_tlv {
	/** @ref RceTags "Command tag" for shared memory setup. */
	uint64_t tag;

	/**
	 * Total length of the TLV (sizeof(@erf camrtc_tlv) + sizeof(Value))
	 * [0,UINT64_MAX]. Must be a multiple of 8.
	 */
	uint64_t len;
};

/**
 * @brief Setup TLV for IVC
 *
 * Multiple setup structures can follow each other.
 */
struct camrtc_tlv_ivc_setup {
	/** @ref CAMRTC_TAG_IVC_SETUP "IVC setup command tag". */
	uint64_t tag;

	/**
	 * Length of the tag specific data. Must be >=
	 * sizeof(@ref camrtc_tlv_ivc_setup) and a multiple of 8.
	 */
	uint64_t len;

	/**
	 * Base IOVA address of IVC read header. Receive queue from CCPLEX
	 * point of view. Must be non-zero and multiple of 64.
	 */
	uint64_t rx_iova;

	/** Size of IVC RX frame. Must be non-zero and a multiple of 64. */
	uint32_t rx_frame_size;

	/** Size of the queue as number of RX frames [1,UINT32_MAX]. */
	uint32_t rx_nframes;

	/**
	 * Base IOVA address of write header. Transmit queue from CCPLEX
	 * point of view. Must be non-zero and multiple of 64.
	 */
	uint64_t tx_iova;

	/** Size of IVC TX frame. Must be non-zero and a multiple of 64. */
	uint32_t tx_frame_size;

	/** Maximum number of IVC TX frames in the queue [1,UINT32_MAX]. */
	uint32_t tx_nframes;

	/**
	 * IVC channel group mask [1, @ref CAMRTC_HSP_SS_IVC_MASK].
	 * A bit-mask indicating which IVC channel wakeup group(s)
	 * the IVC channel belongs to (see @ref CAMRTC_HSP_IRQ).
	 */
	uint32_t channel_group;

	/** Version number of the IVC service [0,UINT32_MAX]. */
	uint32_t ivc_version;

	/** IVC service name. Need not be NUL terminated. */
	char ivc_service[32];
};

/**
 * @defgroup CamRTCChannelErrors Channel setup error codes
 * @{
 */

/** Channel setup successful. */
#define	RTCPU_CH_SUCCESS		MK_U32(0)

/** Channel setup failed: no matching IVC service found. */
#define	RTCPU_CH_ERR_NO_SERVICE		MK_U32(128)

/** Channel setup failed: IVC service is already configured. */
#define	RTCPU_CH_ERR_ALREADY		MK_U32(129)

/** Channel setup failed: Unknown command tag. */
#define	RTCPU_CH_ERR_UNKNOWN_TAG	MK_U32(130)

/** Channel setup failed: Invalid IOVA address. */
#define	RTCPU_CH_ERR_INVALID_IOVA	MK_U32(131)

/** Channel setup failed: Invalid parameter. */
#define	RTCPU_CH_ERR_INVALID_PARAM	MK_U32(132)

/** Channel setup failed: RCE trace disabled */
#define	RTCPU_CH_ERR_TRACE_DISABLED	MK_U32(133)

/** Channel setup failed: Invalid buffer IOVA range*/
#define	RTCPU_CH_ERR_INVALID_IOVA_RANGE	MK_U32(134)
/* @} */

/**
 * @brief Code coverage memory header
 */
struct camrtc_coverage_memory_header {
	/** Code coverage tag. Should be CAMRTC_TAG_NV_COVERAGE */
	uint64_t signature;
	/** Size of camrtc_coverage_memory_header */
	uint64_t length;
	/** Header revision */
	uint32_t revision;
	/** Size of the coverage memory buffer */
	uint32_t coverage_buffer_size;
	/** Coverage data inside the memory buffer in bytes */
	uint32_t coverage_total_bytes;
	/** Reserved */
	uint32_t reserved;
};

#endif /* INCLUDE_CAMRTC_CHANNELS_H */
