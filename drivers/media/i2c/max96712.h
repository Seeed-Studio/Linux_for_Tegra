/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2023-2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 */
/*
 * MAX96712 deserializer driver header
 */

#ifndef __MAX96712_H__
#define __MAX96712_H__

int max96712_write_reg_Dser(int slaveAddr, int channel, u16 addr, u8 val);
int max96712_read_reg_Dser(int slaveAddr, int channel, u16 addr, unsigned int *val);

#endif /* __MAX96712_H__ */
