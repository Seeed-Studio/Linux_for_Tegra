/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

/**
 * @file
 * <b>max96712 API: For Maxim Integrated max96712 deserializer</b>
 *
 * @b Description: Defines elements used to set up and use a
 *  Maxim Integrated max96712 deserializer.
 */

#ifndef __OBC_MAX96712_H__
#define __OBC_MAX96712_H__

#include <linux/types.h>
#include <media/gmsl-link.h>
#include <media/obc_g300_priv.h>

int max96712_write_reg_with_addr(struct device *dev, u8 i2c_addr, u16 addr, u8 val);
int max96712_write_tab_with_addr(struct device *dev, u8 i2c_addr, struct max_reg_pair tab[], u16 size);
int max96712_read_reg_with_addr(struct device *dev, u8 i2c_addr, u16 addr, u8 *val);

int max96712_write_seri_reg(struct device *dev, u8 link_id, u16 addr, u8 val);
int max96712_write_seri_tab(struct device *dev, u8 link_id, struct max_reg_pair buf[], u16 size);

int max96712_write_sensor(struct device *dev, u8 link_id, void *data, u16 length);
int max96712_read_sensor(struct device *dev, u8 link_id, void *data, u16 length);


int max96712_init_settings(struct device *dev);
int max96712_reset_dev(struct device *dev);
int max96712_set_pipe(struct device *dev, u8 link_id, u8 pipe_id, u8 data_type1, u8 data_type2, u8 srcvc_id, u8 dstvc_id);

int max96712_link_pipe_bind(struct device *dev, u8 link_id, u8 pipe_id, u8 seri_pipe_id);

u8 max96712_get_link_map(struct device *dev);
u8 max96712_check_pipe_lock(struct device *dev);

int max96712_power_on(struct device *dev);
void max96712_power_off(struct device *dev);

int max96712_get_link_state(struct device *dev, u8 link_id, int *value);
int max96712_get_available_pipe_id(struct device *dev, int dst_vc_id);
int max96712_release_pipe(struct device *dev, u8 link_id, u8 pipe_id);
void max96712_set_link_init_flag(struct device *dev, u8 link);
u8 max96712_get_link_init_flag(struct device *dev);
int max96712_init_tx_gpio(struct device *dev);

int max96712_reset_oneshot(struct device *dev);

/** @} */

#endif  /* __max96712_H__ */
