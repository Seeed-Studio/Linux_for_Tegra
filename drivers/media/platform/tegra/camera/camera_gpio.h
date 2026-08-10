/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014-2022, NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef __CAMERA_GPIO_H__
#define __CAMERA_GPIO_H__

int cam_gpio_register(struct device *dev,
			unsigned pin_num);

void cam_gpio_deregister(struct device *dev,
			unsigned pin_num);

int cam_gpio_ctrl(struct device *dev,
			unsigned pin_num, int ref_inc, bool active_high);

#endif
/* __CAMERA_GPIO_H__ */
