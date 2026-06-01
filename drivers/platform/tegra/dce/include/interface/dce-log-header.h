/* SPDX-License-Identifier: GPL-2.0-only OR MIT
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef DCE_LOG_HEADER_H
#define DCE_LOG_HEADER_H

/**
 * Log buffer header information structure
 *
 * This structure contains metadata about the circular log buffer including
 * version information, buffer size, and synchronization flags for DCE and
 * consumer communication.
 */
struct dce_log_buffer_header {
    uint32_t version;               /** Header version for compatibility checking */
    uint32_t header_size;           /** Size of this header structure */
    uint32_t flag;                  /** Synchronization flag for producer-consumer */
    uint32_t circ_buf_size;         /** Size of circular buffer region */
    uint32_t is_encoded_log;        /** Indicates if logs are encoded or not */
    uint32_t reserved_32;           /** Reserved 32-bit field */
    uint64_t reserved_64;           /** Reserved 64-bit field */
    uint64_t total_bytes_written;   /** Total bytes written by DCE */
    uint8_t  buff[];                /** Starting point of circular region of buffer */
};

/** Log buffer synchronization flag, indicates if DCE is writing actively */
#define DCE_LOG_BUFFER_FLAG_DCE_ACTIVE     0x00000001U

/** Current version of the log buffer header */
#define DCE_LOG_BUFFER_HEADER_VERSION           0x00000001U

/** Size of the log buffer header structure */
#define DCE_LOG_BUFFER_HEADER_SIZE              sizeof(dce_log_buffer_header_t)

/** Default DCE log level for printing to log buffer */
#define DCE_LOG_DEFAULT_LOG_LEVEL       ((uint32_t)(0xFFFFFFFFU))

#endif
