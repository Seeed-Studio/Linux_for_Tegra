/*
 * Copyright (c) 2023, NVIDIA Corporation & AFFILIATES. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef __USER_TA_HEADER_DEFINES_H__
#define __USER_TA_HEADER_DEFINES_H__

#include <ftpm_helper_ta.h>

#define TA_UUID		FTPM_HELPER_TA_UUID
#define TA_FLAGS	(TA_FLAG_SINGLE_INSTANCE | \
			 TA_FLAG_INSTANCE_KEEP_ALIVE)
#define TA_STACK_SIZE	(2 * 1024)
#define TA_DATA_SIZE	(4 * 1024)

#endif /* __USER_TA_HEADER_DEFINES_H__ */
