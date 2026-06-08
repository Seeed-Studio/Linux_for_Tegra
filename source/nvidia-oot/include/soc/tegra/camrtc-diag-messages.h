// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

/**
 * @file camrtc-diag-messages.h
 *
 * @brief Diagnostic IVC messages
 */

#ifndef INCLUDE_CAMRTC_DIAG_MESSAGES_H
#define INCLUDE_CAMRTC_DIAG_MESSAGES_H

#include "soc/tegra/camrtc-common.h"
#include "soc/tegra/camrtc-capture.h"
#include "soc/tegra/camrtc-diag.h"

#pragma GCC diagnostic error "-Wpadded"

/**
 * @defgroup DiagMsgType Message types for RCE diagnostics channel
 * @{
 */

/**
 * @brief ISP PFSD diagnostics setup request.
 *
 * This is a @ref DiagMsgType "diagnostic message" to
 * set up ISP PFSD diagnostics and associated resources.
 *
 * @pre The @em diag IVC channel has been set up during
 *      boot using the @ref CAMRTC_HSP_CH_SETUP command.
 *
 * @par Header
 * - @ref camrtc_diag_msg::msg_type "msg_type" = @ref CAMRTC_DIAG_ISP5_SDL_SETUP_REQ
 * - @ref camrtc_diag_msg::transaction_id "transaction_id" = <em>unique ID</em>
 *
 * @par Payload
 * - @ref camrtc_diag_isp5_sdl_setup_req
 *
 * @par Response
 * - @ref CAMRTC_DIAG_ISP5_SDL_SETUP_RESP
 */
#define CAMRTC_DIAG_ISP5_SDL_SETUP_REQ		MK_U32(0x01)

/**
 * @brief ISP PFSD diagnostics setup response.
 *
 * This is a @ref DiaglMsgType "diagnostic message" received in
 * response to a @ref CAMRTC_DIAG_ISP5_SDL_SETUP_REQ message.
 *
 * @pre A @ref CAMRTC_DIAG_ISP5_SDL_SETUP_REQ message has been sent.
 *
 * @par Header
 * - @ref camrtc_diag_msg::msg_type "msg_type" = @ref CAMRTC_DIAG_ISP5_SDL_SETUP_RESP
 * - @ref camrtc_diag_msg::transaction_id "transaction_id" =
 *   @ref CAMRTC_DIAG_ISP5_SDL_SETUP_REQ@b::@ref camrtc_diag_msg " camrtc_diag_msg"@b::@ref camrtc_diag_msg::transaction_id "transaction_id"
 *
 * @par Payload
 *  - @ref camrtc_diag_isp5_sdl_setup_resp
 */
#define CAMRTC_DIAG_ISP5_SDL_SETUP_RESP		MK_U32(0x02)

/**
 * @brief ISP PFSD diagnostics release request.
 *
 * This is a @ref DiagMsgType "diagnostic message" to stop the
 * ISP PFSD diagnostics and release the associated resources.
 *
 * @pre The ISP PFSD diagnostics has been set up with
 *      @ref CAMRTC_DIAG_ISP5_SDL_SETUP_REQ.
 *
 * @par Header
 * - @ref camrtc_diag_msg::msg_type "msg_type" = @ref CAMRTC_DIAG_ISP5_SDL_RELEASE_REQ
 * - @ref camrtc_diag_msg::transaction_id "transaction_id" = <em>unique ID</em>
 *
 * @par Payload
 * - None
 *
 * @par Response
 * - @ref CAMRTC_DIAG_ISP5_SDL_RELEASE_RESP
 */
#define CAMRTC_DIAG_ISP5_SDL_RELEASE_REQ	MK_U32(0x03)

/**
 * @brief ISP PFSD diagnostics release response.
 *
 * This is a @ref DiaglMsgType "diagnostic message" received in
 * response to a @ref CAMRTC_DIAG_ISP5_SDL_RELEASE_REQ message.
 *
 * @pre A @ref CAMRTC_DIAG_ISP5_SDL_RELEASE_REQ message has been sent.
 *
 * @par Header
 * - @ref camrtc_diag_msg::msg_type "msg_type" = @ref CAMRTC_DIAG_ISP5_SDL_RELEASE_RESP
 * - @ref camrtc_diag_msg::transaction_id "transaction_id" =
 *   @ref CAMRTC_DIAG_ISP5_SDL_RELEASE_REQ@b::@ref camrtc_diag_msg " camrtc_diag_msg"@b::@ref camrtc_diag_msg::transaction_id "transaction_id"
 *
 * @par Payload
 *  - @ref camrtc_diag_isp5_sdl_release_resp
 */
#define CAMRTC_DIAG_ISP5_SDL_RELEASE_RESP	MK_U32(0x04)

/**
 * @brief ISP PFSD diagnostics status request.
 *
 * This is a @ref DiagMsgType "diagnostic message" to
 * query the diagnostic status.
 *
 * @pre The ISP PFSD diagnostics has been set up with
 *      @ref CAMRTC_DIAG_ISP5_SDL_SETUP_REQ.
 *
 * @par Header
 * - @ref camrtc_diag_msg::msg_type "msg_type" = @ref CAMRTC_DIAG_ISP5_SDL_STATUS_REQ
 * - @ref camrtc_diag_msg::transaction_id "transaction_id" = <em>unique ID</em>
 *
 * @par Payload
 * - None
 *
 * @par Response
 * - @ref CAMRTC_DIAG_ISP5_SDL_STATUS_RESP
 */
#define CAMRTC_DIAG_ISP5_SDL_STATUS_REQ		MK_U32(0x05)

/**
 * @brief ISP PFSD diagnostics status response.
 *
 * This is a @ref DiaglMsgType "diagnostic message" received in
 * response to a @ref CAMRTC_DIAG_ISP5_SDL_STATUS_REQ message.
 *
 * @pre A @ref CAMRTC_DIAG_ISP5_SDL_STATUS_REQ message has been sent.
 *
 * @par Header
 * - @ref camrtc_diag_msg::msg_type "msg_type" = @ref CAMRTC_DIAG_ISP5_SDL_STATUS_RESP
 * - @ref camrtc_diag_msg::transaction_id "transaction_id" =
 *   @ref CAMRTC_DIAG_ISP5_SDL_STATUS_REQ@b::@ref camrtc_diag_msg " camrtc_diag_msg"@b::@ref camrtc_diag_msg::transaction_id "transaction_id"
 *
 * @par Payload
 *  - @ref camrtc_diag_isp5_sdl_status_resp
 */
#define CAMRTC_DIAG_ISP5_SDL_STATUS_RESP	MK_U32(0x06)
/**@}*/

/**
 * @defgroup DiagResultCodes Diagnostics channel Result codes
 * @{
 */

/** No errors detected. */
#define CAMRTC_DIAG_SUCCESS			MK_U32(0x00)

/** Invalid argument. */
#define CAMRTC_DIAG_ERROR_INVAL			MK_U32(0x01)

/** Action not supported. */
#define CAMRTC_DIAG_ERROR_NOTSUP		MK_U32(0x02)

/** Device or resource busy. */
#define CAMRTC_DIAG_ERROR_BUSY			MK_U32(0x03)

/** Timeout error. */
#define CAMRTC_DIAG_ERROR_TIMEOUT		MK_U32(0x04)

/** Unknown error. */
#define CAMRTC_DIAG_ERROR_UNKNOWN		MK_U32(0xFF)
/**@}*/

/** @brief Message data for @ref CAMRTC_DIAG_ISP5_SDL_SETUP_REQ message */
struct camrtc_diag_isp5_sdl_setup_req {
	/**
	 * Diagnostic test binary IOVA for RCE. See @ref isp5_sdl_header
	 * "Diagnostic test binary header". Must be non-zero and
	 * a multiple of @ref CAMRTC_DIAG_DMA_ALIGN_BYTES.
	 */
	iova_t rce_iova;

	/**
	 * Diagnostic test binary IOVA for ISP. See @ref isp5_sdl_header
	 * "Diagnostic test binary header". Must be non-zero and
	 * a multiple of @ref CAMRTC_DIAG_DMA_ALIGN_BYTES.
	 */
	iova_t isp_iova;

	/**
	 * Diagnostic test binary size.
	 * Must be a multiple of @ref CAMRTC_DIAG_DMA_ALIGN_BYTES.
	 */
	uint32_t size;

	/**
	 * Diagnostic Test Interval (ms) [1, UINT32_MAX].
	 * Should be set to FDTI/2.
	 */
	uint32_t period;
} CAMRTC_DIAG_IVC_ALIGN;

/** @brief Message data for @ref CAMRTC_DIAG_ISP5_SDL_SETUP_RESP message */
struct camrtc_diag_isp5_sdl_setup_resp {
	/** @ref DiagResultCodes "Diagnostic request result code". */
	uint32_t result;

	/** Reserved. */
	uint32_t pad32_[1];
} CAMRTC_DIAG_IVC_ALIGN;

/** @brief Message data for @ref CAMRTC_DIAG_ISP5_SDL_RELEASE_RESP message */
struct camrtc_diag_isp5_sdl_release_resp {
	/** @ref DiagResultCodes "Diagnostic request result code". */
	uint32_t result;

	/** Reserved. */
	uint32_t pad32_[1];
} CAMRTC_DIAG_IVC_ALIGN;

/** @brief Message data for @ref CAMRTC_DIAG_ISP5_SDL_STATUS_RESP message */
struct camrtc_diag_isp5_sdl_status_resp {
	/** @ref DiagResultCodes "Diagnostic request result code". */
	uint32_t result;

	/** Nonzero if PFSD tests are being scheduled. */
	uint32_t running;

	/** Number of tests that have been scheduled. */
	uint64_t scheduled;

	/** Number of tests that have been executed. */
	uint64_t executed;

	/** Number of tests that have been passed. */
	uint64_t passed;

	/** Number of CRC failures in tests. (Counter stops at UINT32_MAX.) */
	uint32_t crc_failed;

	/** Explicit padding */
	uint32_t pad32_[1];
} CAMRTC_DIAG_IVC_ALIGN;

/**
 * @brief Common message format for all diagnostic IVC messages.
 */
struct camrtc_diag_msg {
	/** @ref MessageType "Message type". */
	uint32_t msg_type;

	/**
	 * Transaction ID associated with a request [0, UINT32_MAX]. The
	 * transaction in a response message must match the transaction ID
	 * of the associated request.
	 */
	uint32_t transaction_id;

	union {
		/** @anon_union_member */
		/** Message data for @ref CAMRTC_DIAG_ISP5_SDL_SETUP_REQ message */
		struct camrtc_diag_isp5_sdl_setup_req isp5_sdl_setup_req;

		/** @anon_union_member */
		/** Message data for @ref CAMRTC_DIAG_ISP5_SDL_SETUP_RESP message */
		struct camrtc_diag_isp5_sdl_setup_resp isp5_sdl_setup_resp;

		/** @anon_union_member */
		/** Message data for @ref CAMRTC_DIAG_ISP5_SDL_RELEASE_RESP message */
		struct camrtc_diag_isp5_sdl_release_resp isp5_sdl_release_resp;

		/** @anon_union_member */
		/** Message data for @ref CAMRTC_DIAG_ISP5_SDL_STATUS_RESP message */
		struct camrtc_diag_isp5_sdl_status_resp isp5_sdl_status_resp;
	};
} CAMRTC_DIAG_IVC_ALIGN;

#pragma GCC diagnostic ignored "-Wpadded"

#endif /* INCLUDE_CAMRTC_DIAG_MESSAGES_H */
