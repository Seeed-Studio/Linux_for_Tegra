/*
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SE_PRIVATE_H
#define SE_PRIVATE_H

#include <lib/utils_def.h>

/* Control register to trigger SC7 context save/restore operation */
#define SE0_SC7_CTRL				U(0xBC)
#define SE0_SC7_CTRL_OP_BIT			BIT_32(0)
#define SE0_SC7_CMD_START_CTX_SAVE		(U(0) << 0)
#define SE0_SC7_CMD_START_CTX_RESTORE		(U(1) << 0)

/* Status register to report SC7 context-save/restore status */
#define SE0_SC7_STATUS				U(0xC0)
#define SE0_SC7_STATUS_BUSY			BIT_32(31)
#define SE0_SC7_STATE_SAVE_CTX_VAL		(U(0x2) << 7)
#define SE0_SC7_STATE_RESTORE_CTX_VAL		(U(0x6) << 7)

/* Interrupt register to enable/disable for SE engine */
#define SE0_INT_ENABLE				U(0x88)
#define SE0_SC7_CTX_START_ERROR			BIT_32(6)
#define SE0_SC7_CTX_INTEGRITY_ERROR		BIT_32(7)

/* Interrupt status register */
#define SE0_INT_STATUS				U(0x8C)

/* Software reset register to reset SE engine */
#define SE0_SOFTRESET				U(0x60)
#define SE0_SOFTRESET_FALSE			U(0)
#define SE0_SOFTRESET_TRUE			U(1)

/*******************************************************************************
 * Inline functions definition
 ******************************************************************************/

static inline uint32_t tegra_se_read_32(uint32_t offset)
{
	return mmio_read_32((uint32_t)(TEGRA_SE0_BASE + offset));
}

static inline void tegra_se_write_32(uint32_t offset, uint32_t val)
{
	mmio_write_32((uint32_t)(TEGRA_SE0_BASE + offset), val);
}

static inline uint32_t tegra_rng1_read_32(uint32_t offset)
{
	return mmio_read_32((uint32_t)(TEGRA_RNG1_BASE + offset));
}

static inline void tegra_rng_write_32(uint32_t offset, uint32_t val)
{
	mmio_write_32((uint32_t)(TEGRA_RNG1_BASE + offset), val);
}

#endif /* SE_PRIVATE_H */
