/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2023, NVIDIA Corporation.  All rights reserved.
 */

#ifndef PVA_PACKED_H
#define PVA_PACKED_H
#ifdef __chess__
#define PVA_PACKED /* TODO: find chess compiler pragma if there is one. */
#else
#define PVA_PACKED __packed
#endif
#endif
