/*
 * Copyright (c) 2022, NVIDIA Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef DSU_H
#define DSU_H

#include <stdint.h>

#include <lib/mmio.h>

#include <platform_def.h>

/*******************************************************************************
 * DSU Power Policy Unit registers
 ******************************************************************************/
#define PPU_PWPR			U(0x000)
#define PPU_PMER			U(0x004)
#define PPU_PWSR			U(0x008)
#define PPU_DISR			U(0x010)
#define PPU_MISR			U(0x014)
#define PPU_STSR			U(0x018)
#define PPU_UNLK			U(0x01C)
#define PPU_PWCR			U(0x020)
#define PPU_PTCR			U(0x024)
#define PPU_IMR				U(0x030)
#define PPU_AIMR			U(0x034)
#define PPU_ISR				U(0x038)
#define PPU_AISR			U(0x03C)
#define PPU_IESR			U(0x040)
#define PPU_OPSR			U(0x044)
#define PPU_FUNRR			U(0x050)
#define PPU_FULRR			U(0x054)
#define PPU_MEMRR			U(0x058)
#define PPU_DCDR0			U(0x170)
#define PPU_DCDR1			U(0x174)
#define PPU_IDR0			U(0xFB0)
#define PPU_IDR1			U(0xFB4)
#define PPU_IIDR			U(0xFC8)
#define PPU_AIDR			U(0xFCC)
#define PPU_PIDR4			U(0xFD0)
#define PPU_PIDR5			U(0xFD4)
#define PPU_PIDR6			U(0xFD8)
#define PPU_PIDR7			U(0xFDC)
#define PPU_PIDR0			U(0xFE0)
#define PPU_PIDR1			U(0xFE4)
#define PPU_PIDR2			U(0xFE8)
#define PPU_PIDR3			U(0xFEC)
#define PPU_CIDR0			U(0xFF0)
#define PPU_CIDR1			U(0xFF4)
#define PPU_CIDR2			U(0xFF8)
#define PPU_CIDR3			U(0xFFC)

/*******************************************************************************
 * PPU_PWPR and PPU_PWSR constants
 ******************************************************************************/
#define OP_DYN_EN_BIT			BIT(24)
#define OP_POLICY_MODE_00		U(0x0)
#define OP_POLICY_MODE_01		U(0x1)
#define OP_POLICY_MODE_03		U(0x3)
#define OP_POLICY_MODE_04		U(0x4)
#define OP_POLICY_MODE_05		U(0x5)
#define OP_POLICY_MODE_07		U(0x7)
#define OP_POLICY_MODE_SHIFT		U(16)
#define OP_POLICY_MODE_MASK		U(0xF)
#define LOCK_EN_BIT			BIT(12)
#define PWR_DYN_EN_BIT			BIT(8)
#define PWR_POLICY_OFF			U(0x0)
#define PWR_POLICY_OFF_EMU		U(0x1)
#define PWR_POLICY_MEM_RET		U(0x2)
#define PWR_POLICY_MEM_RET__EMU		U(0x3)
#define PWR_POLICY_OFF_FUNC_RET		U(0x7)
#define PWR_POLICY_ON			U(0x8)
#define PWR_POLICY_WARM_RST		U(0x9)
#define PWR_POLICY_DBG_RECOV		U(0x10)
#define PWR_POLICY_SHIFT		U(0)
#define PWR_POLICY_MASK			U(0xF)

/*******************************************************************************
 * Helper macros
 ******************************************************************************/
#define PPU_REGION_SIZE			U(0x100000)
#define CLUSTER_PPU_BASE(n)		((PPU_REGION_SIZE * n) + U(0x30000))
#define CORE_PPU_BASE(n)		((PPU_REGION_SIZE * n) + U(0x80000))

/*******************************************************************************
 * Accessor functions
 ******************************************************************************/
static inline void dsu_ppu_cluster_write_32(uint32_t core_pos,
			uint64_t ub_base, uint32_t offset, uint32_t value)
{
	uint64_t base = ub_base + CLUSTER_PPU_BASE(core_pos);

	mmio_write_32(base + offset, value);
}

static inline uint32_t dsu_ppu_cluster_read_32(uint32_t core_pos,
			uint64_t ub_base, uint32_t offset)
{
	uint64_t base = ub_base + CLUSTER_PPU_BASE(core_pos);

	return mmio_read_32(base + offset);
}

static inline void dsu_ppu_core_write_32(uint32_t core_pos, uint64_t ub_base,
			uint32_t offset, uint32_t value)
{
	uint64_t base = ub_base + CORE_PPU_BASE(core_pos);

	mmio_write_32(base + offset, value);
}

static inline uint32_t dsu_ppu_core_read_32(uint32_t core_pos, uint64_t ub_base,
			uint32_t offset)
{
	uint64_t base = ub_base + CORE_PPU_BASE(core_pos);

	return mmio_read_32(base + offset);
}

/*******************************************************************************
 * Public functions
 ******************************************************************************/
int dsu_ppu_setup(uint64_t base, uint64_t per_socket_mmio_size,
		  unsigned int num_cpus_per_socket);
int dsu_ppu_core_power_on(uint32_t socket, uint32_t core,
	uint32_t cluster_ppu_pwpr, uint32_t core_ppu_pwpr);
int dsu_ppu_core_power_off(uint32_t socket, uint32_t core,
	uint32_t cluster_ppu_pwpr, uint32_t core_ppu_pwpr);

#endif
