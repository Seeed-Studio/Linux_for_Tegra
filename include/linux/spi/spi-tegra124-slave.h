/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (C) 2023 NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef __LINUX_SPI_TEGRA124_SLAVE_H
#define __LINUX_SPI_TEGRA124_SLAVE_H

#include <linux/spi/spi.h>

typedef int (*spi_callback)(void *client_data);

int tegra124_spi_slave_register_callback(struct spi_device *spi,
				      spi_callback func_ready,
				      spi_callback func_isr,
				      void *client_data);
#endif
