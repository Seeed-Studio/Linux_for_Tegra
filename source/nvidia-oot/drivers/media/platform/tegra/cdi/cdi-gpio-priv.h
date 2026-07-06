// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#ifndef __CDI_GPIO_PRIV_H__
#define __CDI_GPIO_PRIV_H__

struct cdi_gpio_plat_data {
	const char *gpio_prnt_chip;
	u32 max_gpio;
};

struct cdi_gpio_desc {
	u32 gpio;
	atomic_t ref_cnt;
};

struct cdi_gpio_priv {
	struct device *pdev;
	struct cdi_gpio_plat_data pdata;
	struct mutex mutex;
	struct gpio_chip gpio_chip;
	struct gpio_chip *tgc;
	struct cdi_gpio_desc *gpios;
	u32 num_gpio;
};

#endif /* __CDI_GPIO_PRIV_H__ */
