/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2023, NVIDIA Corporation.  All rights reserved.
 */

#ifndef _PVA_TRACE_H_
#define _PVA_TRACE_H_

#define NVPVA_DEFAULT_LG_MASK 0x00000000

/*
 * Individual Trace point
 *
 * The delta time recorded in each trace point is the time from the previous
 * trace point.  The first trace point in a block of trace points will have
 * a delta time of 0 (it is referencing the absolute time of the block).
 */
struct pva_trace_point {
	u32 delta_time;
	u8 major;
	u8 minor;
	u8 flags;
	u8 sequence;
	u32 arg1;
	u32 arg2;
};

/*
 * Trace block header that is written to DRAM, the indicated number of
 * trace points immediately follows the header.
 */
struct pva_trace_block_hdr {
	u64 start_time;
	u16 n_entries;
	u16 reserved_1;
	u32 reserved_2;
	u8 align[48];
};

struct pva_trace_header {
	u32 block_size;
	u32 head_offset;
	u32 tail_offset;
	u8 align[52];

};

#endif
