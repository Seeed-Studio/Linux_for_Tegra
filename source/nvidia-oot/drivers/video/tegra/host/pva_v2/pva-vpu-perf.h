/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018-2023, NVIDIA Corporation.  All rights reserved.
 */

#ifndef _PVA_VPU_PERF_H_
#define _PVA_VPU_PERF_H_

#define PVA_TASK_VPU_NUM_PERF_COUNTERS 8

struct pva_task_vpu_perf_counter {
	u32 count;
	u32 sum;
	u64 sum_squared;
	u32 min;
	u32 max;
};

#endif

