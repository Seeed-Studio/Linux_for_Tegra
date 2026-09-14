// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2018-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * place to add wrapper function to drop kasan scan
 */

#include <linux/export.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/device.h>
#include "nvmap_dev.h"

void kasan_memcpy_toio(void __iomem *to,
			const void *from, size_t count)
{
	while (count && (!IS_ALIGNED((unsigned long)to, 8) ||
			 !IS_ALIGNED((unsigned long)from, 8))) {
		__raw_writeb(*(u8 *)from, to);
		from++;
		to++;
		count--;
	}

	while (count >= 8) {
		__raw_writeq(*(u64 *)from, to);
		from += 8;
		to += 8;
		count -= 8;
	}

	while (count) {
		__raw_writeb(*(u8 *)from, to);
		from++;
		to++;
		count--;
	}
}
