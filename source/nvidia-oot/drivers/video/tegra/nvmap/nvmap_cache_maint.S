/* SPDX-License-Identifier: GPL-2.0-only
 * SPDX-FileCopyrightText: Copyright (c) 2021-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Derived from Linux kernel source file arch/arm64/mm/cache.S
 * Copyright (C) 2001 Deep Blue Solutions Ltd.
 * Copyright (C) 2012 ARM Ltd.
 *
 * Cache maintenance
 */

#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/cpufeature.h>
#include <asm/alternative.h>
#include <asm/asm-uaccess.h>

/*
 *	__inval_dcache_area(kaddr, size)
 *
 * 	Ensure that any D-cache lines for the interval [kaddr, kaddr+size)
 * 	are invalidated. Any partial lines at the ends of the interval are
 *	also cleaned to PoC to prevent data loss.
 *
 *	- kaddr   - kernel address
 *	- size    - size in question
 */
SYM_FUNC_START_LOCAL(__dma_inv_area)
SYM_FUNC_START(__inval_dcache_area)
	/* FALLTHROUGH */

/*
 *	__dma_inv_area(start, size)
 *	- start   - virtual start address of region
 *	- size    - size in question
 */
	add	x1, x1, x0
	dcache_line_size x2, x3
	sub	x3, x2, #1
	tst	x1, x3				// end cache line aligned?
	bic	x1, x1, x3
	b.eq	1f
	dc	civac, x1			// clean & invalidate D / U line
1:	tst	x0, x3				// start cache line aligned?
	bic	x0, x0, x3
	b.eq	2f
	dc	civac, x0			// clean & invalidate D / U line
	b	3f
2:	dc	ivac, x0			// invalidate D / U line
3:	add	x0, x0, x2
	cmp	x0, x1
	b.lo	2b
	dsb	sy
	ret
	sb
SYM_FUNC_END(__inval_dcache_area)
SYM_FUNC_END(__dma_inv_area)

/*
 *	__clean_dcache_area_poc(kaddr, size)
 *
 * 	Ensure that any D-cache lines for the interval [kaddr, kaddr+size)
 * 	are cleaned to the PoC.
 *
 *	- kaddr   - kernel address
 *	- size    - size in question
 */
SYM_FUNC_START_LOCAL(__dma_clean_area)
SYM_FUNC_START(__clean_dcache_area_poc)
	/* FALLTHROUGH */

/*
 *	__dma_clean_area(start, size)
 *	- start   - virtual start address of region
 *	- size    - size in question
 */
	add	x1, x0, x1
	dcache_by_line_op cvac, sy, x0, x1, x2, x3
	ret
	sb
SYM_FUNC_END(__clean_dcache_area_poc)
SYM_FUNC_END(__dma_clean_area)

/*
 *	__dma_map_area_from_device(start, size)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 */
SYM_FUNC_START(__dma_map_area_from_device)
	b	__dma_inv_area
	b	__dma_clean_area
SYM_FUNC_END(__dma_map_area_from_device)

/*
 *	__dma_map_area_to_device(start, size)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 */
SYM_FUNC_START(__dma_map_area_to_device)
	b	__dma_clean_area
SYM_FUNC_END(__dma_map_area_to_device)
