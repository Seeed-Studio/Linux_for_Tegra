/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __ENDPOINT_H__
#define __ENDPOINT_H__

#include "common.h"

/* forward declaration. */
struct driver_ctx_t;

/*
 * Entry point for the endpoint(s) char device sub-module/abstraction.
 *
 * On successful return (0), devices would have been created and ready to
 * accept ioctls from user-space application.
 */
int
endpoints_setup(struct driver_ctx_t *drv_ctx, void **endpoints_h);

/* Exit point for nvscic2c-pcie endpoints: Wait for all endpoints to close.*/
int
endpoints_waitfor_close(void *endpoints_h);

/* exit point for nvscic2c-pcie endpoints char device sub-module/abstraction.*/
int
endpoints_release(void **endpoints_h);
#endif /*__ENDPOINT_H__ */
