/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sensor_kernel_tests_core - sensor kernel tests module core
 *
 * Copyright (c) 2018-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 */

#ifndef __SENSOR_KERNEL_TESTS_CORE_H__
#define __SENSOR_KERNEL_TESTS_CORE_H__

#include <linux/stdarg.h>

#define SKT_NL_MAX_STR_LEN (1024U)

/**
 * skt_core_(v)log_msg - Log a unicast message to the recipient at portid
 * @portid: destination portid
 * @fmt: format
 * @args: arguments
 *
 * To be used only under test context, that is, when the recipient is
 * an initiator of SKT_CMD_RUN_TESTS.
 *
 * Returns non-zero on failure.
 */
int skt_core_vlog_msg(const u32 portid, const char *fmt, va_list args);
int skt_core_log_msg(const u32 portid, const char *fmt, ...);

#endif // __SENSOR_KERNEL_TESTS_CORE_H__
