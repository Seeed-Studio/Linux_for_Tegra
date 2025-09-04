/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2020-2022, NVIDIA CORPORATION. All rights reserved.
 */

#include <console.h>
#include <tegra_common.h>

#ifdef CFG_TEGRA_TCU
#include <drivers/tegra_combined_uart.h>
#endif

#ifdef CFG_TEGRA_TCU
register_phys_mem_pgdir(MEM_AREA_IO_SEC, TEGRA_COMBUART_BASE, TEGRA_COMBUART_SIZE);
static struct tegra_combined_uart_data tcud;
#endif

#if defined(PLATFORM_FLAVOR_t194)
#define TEGRA194_QSPI0_BASE		0x3270000
#define TEGRA194_QSPI0_SIZE		0x10000
register_phys_mem_pgdir(MEM_AREA_IO_SEC, TEGRA194_QSPI0_BASE, TEGRA194_QSPI0_SIZE);
#endif

#if defined(PLATFORM_FLAVOR_t234)
#define TEGRA234_QSPI0_BASE		0x3270000
#define TEGRA234_QSPI0_SIZE		0x10000
register_phys_mem_pgdir(MEM_AREA_IO_SEC, TEGRA234_QSPI0_BASE, TEGRA234_QSPI0_SIZE);
#endif

#if defined(PLATFORM_FLAVOR_t234)
#define TEGRA234_SCRATCH_BASE		0xc390000
#define TEGRA234_SCRATCH_SIZE		0x1000
register_phys_mem_pgdir(MEM_AREA_IO_SEC, TEGRA234_SCRATCH_BASE, TEGRA234_SCRATCH_SIZE);
#endif

void console_init(void)
{
	/* Check if there is a platform-specific console.
	 * If so, skip the Tegra Combined UART initialization below.
	 */
	if(tegra_console_init() == TEE_SUCCESS)
		return;

#ifdef CFG_TEGRA_TCU
	tegra_combined_uart_init(&tcud, TEGRA_COMBUART_BASE, TEGRA_COMBUART_SIZE);
	register_serial_console(&tcud.chip);
#endif
}
