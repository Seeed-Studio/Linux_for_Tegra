/*
 * Copyright (c) 2021-2022, NVIDIA Corporation & AFFILIATES. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef __USER_TA_HEADER_DEFINES_H__
#define __USER_TA_HEADER_DEFINES_H__

#include <luks_srv_ta.h>

#define TA_UUID		LUKS_SRV_TA_UUID
#define TA_FLAGS	TA_FLAG_SINGLE_INSTANCE
#define TA_STACK_SIZE	(2 * 1024)
#define TA_DATA_SIZE	(16 * 1024)

#endif
