/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* include hw specification */
#include "host1xEMU.h"

#include "../dev.h"

#include "syncpt_hw.c"

int host1xEMU_init(struct host1x *host)
{
    host->syncpt_op = &host1x_syncpt_ops;
    return 0;
}
