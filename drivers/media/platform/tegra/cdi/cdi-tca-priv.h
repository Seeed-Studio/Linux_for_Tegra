// SPDX-License-Identifier: GPL-2.0
/* SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __CDI_TCA9539_PRIV_H__
#define __CDI_TCA9539_PRIV_H__

/* i2c payload size is only 12 bit */
#define MAX_MSG_SIZE	(0xFFF - 1)

struct tca9539_priv {
	struct i2c_adapter *adap;
	int bus;
	u32 addr;
	u32 reg_len;
	u32 dat_len;
	u8 init_val[12];
	u32 power_port;
	bool enable;
};

static int tca9539_raw_wr(
	struct device *dev, struct tca9539_priv *tca9539, unsigned int offset, u8 val)
{
	int ret = -ENODEV;
	u8 *buf_start = NULL;
	struct i2c_msg *i2cmsg;
	unsigned int num_msgs = 0, total_size, i;
	u8 data[3];
	size_t size = 1;

	dev_dbg(dev, "%s\n", __func__);

	if (tca9539->dat_len != 1)
		return -EINVAL;

	if (tca9539->reg_len != 1)
		return -EINVAL;

	data[0] = (u8)(offset & 0xff);
	data[1] = val;
	size += 1;

	num_msgs = size / MAX_MSG_SIZE;
	num_msgs += (size % MAX_MSG_SIZE) ? 1 : 0;

	i2cmsg = kzalloc((sizeof(struct i2c_msg)*num_msgs), GFP_KERNEL);
	if (!i2cmsg) {
		return -ENOMEM;
	}

	buf_start = data;
	total_size = size;

	dev_dbg(dev, "%s: num_msgs: %d\n", __func__, num_msgs);
	for (i = 0; i < num_msgs; i++) {
		i2cmsg[i].addr = tca9539->addr;
		i2cmsg[i].buf = (__u8 *)buf_start;

		if (i > 0)
			i2cmsg[i].flags = I2C_M_NOSTART;
		else
			i2cmsg[i].flags = 0;

		if (total_size > MAX_MSG_SIZE) {
			i2cmsg[i].len = MAX_MSG_SIZE;
			buf_start += MAX_MSG_SIZE;
			total_size -= MAX_MSG_SIZE;
		} else {
			i2cmsg[i].len = total_size;
		}
		dev_dbg(dev, "%s: addr:%x buf:%p, flags:%u len:%u\n",
			__func__, i2cmsg[i].addr, (void *)i2cmsg[i].buf,
			i2cmsg[i].flags, i2cmsg[i].len);
	}

	ret = i2c_transfer(tca9539->adap, i2cmsg, num_msgs);
	if (ret > 0)
		ret = 0;

	kfree(i2cmsg);
	return ret;
}

static int tca9539_raw_rd(
	struct device *dev, struct tca9539_priv *tca9539, unsigned int offset, u8 *val)
{
	int ret = -ENODEV;
	u8 data[2];
	size_t size = 1;
	struct i2c_msg i2cmsg[2];

	dev_dbg(dev, "%s\n", __func__);

	if (tca9539->dat_len != 1)
		return -EINVAL;

	if (tca9539->reg_len != 1)
		return -EINVAL;

	data[0] = (u8)(offset & 0xff);

	i2cmsg[0].addr = tca9539->addr;
	i2cmsg[0].len = tca9539->reg_len;
	i2cmsg[0].buf = (__u8 *)data;
	i2cmsg[0].flags = I2C_M_NOSTART;

	i2cmsg[1].addr = tca9539->addr;
	i2cmsg[1].flags = I2C_M_RD;
	i2cmsg[1].len = size;
	i2cmsg[1].buf = (__u8 *)val;

	ret = i2c_transfer(tca9539->adap, i2cmsg, 2);
	if (ret > 0)
		ret = 0;

	return ret;
}

#endif  /* __CDI_TCA9539_PRIV_H__ */

