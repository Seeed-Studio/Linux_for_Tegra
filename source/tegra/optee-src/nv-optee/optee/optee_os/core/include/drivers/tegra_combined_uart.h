/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
 */

#ifndef TEGRA_COMBINED_UART_H
#define TEGRA_COMBINED_UART_H

#include <types_ext.h>
#include <drivers/serial.h>

#define TEGRA_COMBUART_BASE		0x0C198000
#define TEGRA_COMBUART_SIZE		0x1000

struct tegra_combined_uart_data {
	struct io_pa_va base;
	size_t base_size;
	struct serial_chip chip;
};

void tegra_combined_uart_init(struct tegra_combined_uart_data *tcud,
                            paddr_t base, size_t size);

#endif
