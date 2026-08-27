/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sensor_dt_test_nodes - sensor device tree test node definitions
 *
 * Copyright (c) 2018-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 */

#ifndef __SENSOR_DT_TEST_NODES_H__
#define __SENSOR_DT_TEST_NODES_H__

struct sv_dt_node;

int sv_dt_make_root_node_props(struct sv_dt_node *node);
int sv_dt_make_modeX_node_props(struct sv_dt_node *node);

#endif // __SENSOR_DT_TEST_NODES_H__
