/* SPDX-License-Identifier: GPL-2.0-only
 * SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
#include <linux/version.h>
#include <asm/assembler.h>
#include <asm/cpufeature.h>
#include <asm/alternative.h>
#include <asm/asm-uaccess.h>

/*
 *	invalidate_icache_range(start,end)
 *
 *	Ensure that the I cache is invalid within specified region.
 *
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
SYM_FUNC_START(invalidate_icache_range)
alternative_if ARM64_HAS_CACHE_DIC
	mov	x0, xzr
	isb
	ret
alternative_else_nop_endif

	uaccess_ttbr0_enable x2, x3, x4

	invalidate_icache_by_line x0, x1, x2, x3, 2f
	mov	x0, xzr
1:
	uaccess_ttbr0_disable x1, x2
	ret
2:
	mov	x0, #-EFAULT
	b	1b
SYM_FUNC_END(invalidate_icache_range)

/*
 *	__flush_dcache_area(kaddr, size)
 *
 *	Ensure that any D-cache lines for the interval [kaddr, kaddr+size)
 *	are cleaned and invalidated to the PoC.
 *
 *	- kaddr   - kernel address
 *	- size    - size in question
 */
SYM_FUNC_START(__flush_dcache_area)
	add	x1, x0, x1
	dcache_by_line_op civac, sy, x0, x1, x2, x3
	ret
SYM_FUNC_END(__flush_dcache_area)

/*
 *	__clean_dcache_area_pou(kaddr, size)
 *
 * 	Ensure that any D-cache lines for the interval [kaddr, kaddr+size)
 * 	are cleaned to the PoU.
 *
 *	- kaddr   - kernel address
 *	- size    - size in question
 */
SYM_FUNC_START(__clean_dcache_area_pou)
alternative_if ARM64_HAS_CACHE_IDC
	dsb	ishst
	ret
alternative_else_nop_endif
	add	x1, x0, x1
	dcache_by_line_op cvau, ish, x0, x1, x2, x3
	ret
SYM_FUNC_END(__clean_dcache_area_pou)

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
SYM_FUNC_END(__clean_dcache_area_poc)
SYM_FUNC_END(__dma_clean_area)

/*
 *	__clean_dcache_area_pop(kaddr, size)
 *
 * 	Ensure that any D-cache lines for the interval [kaddr, kaddr+size)
 * 	are cleaned to the PoP.
 *
 *	- kaddr   - kernel address
 *	- size    - size in question
 */
SYM_FUNC_START(__clean_dcache_area_pop)
	alternative_if_not ARM64_HAS_DCPOP
	b	__clean_dcache_area_poc
	alternative_else_nop_endif
	add	x1, x0, x1
	dcache_by_line_op cvap, sy, x0, x1, x2, x3
	ret
SYM_FUNC_END(__clean_dcache_area_pop)

/*
 *	__dma_flush_area(start, size)
 *
 *	clean & invalidate D / U line
 *
 *	- start   - virtual start address of region
 *	- size    - size in question
 */
SYM_FUNC_START(__dma_flush_area)
	add	x1, x0, x1
	dcache_by_line_op civac, sy, x0, x1, x2, x3
	ret
SYM_FUNC_END(__dma_flush_area)

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
