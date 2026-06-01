/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2023, NVIDIA Corporation.  All rights reserved.
 */

#ifndef PVA_UCODE_HEADER_TYPES_H
#define PVA_UCODE_HEADER_TYPES_H

/*
 * This file is distinct from the other uCode header file because it
 * defines constants/values that are used by the linker scripts and therefor
 * cannot have C structures (only pre-processor directives).
 */

/*
 * Define the length of a section header to be defined independently than
 * the C structure (it will be larger).  Picking a value that is easy to
 * compute.
 */
#define PVA_UCODE_SEG_HDR_LENGTH 128

#define PVA_UCODE_SEG_NONE 0 /* not a segment */
#define PVA_UCODE_SEG_EVP 1 /* EVP information */
#define PVA_UCODE_SEG_R5 2 /* R5 code/data */
#define PVA_UCODE_SEG_CRASHDUMP 3 /* space for crash dump */
#define PVA_UCODE_SEG_TRACE_LOG 4 /* space for PVA trace logs */
#define PVA_UCODE_SEG_DRAM_CACHED 5 /* cachable DRAM area */
#define PVA_UCODE_SEG_CODE_COVERAGE 6 /* space for PVA FW code coverage */
#define PVA_UCODE_SEG_DEBUG_LOG 7 /* space for PVA debug logs */
#define PVA_UCODE_SEG_NEXT 8 /* must be last */

/* PVA FW binary max segment size used for section alignment */
#define PVA_BIN_MAX_HEADER_SIZE 0x1000
#define PVA_BIN_MAX_EVP_SIZE 0x1000

#define PVA_HDR_MAGIC 0x31415650 /* PVA1 in little endian */
#define PVA_HDR_VERSION 0x00010000 /* version 1.0 of the header */
#endif
