/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2023, NVIDIA Corporation.  All rights reserved.
 */

#ifndef PVA_VERSION_H
#define PVA_VERSION_H

#include <pva-types.h>
#include <pva-bit.h>
#include <pva-fw-version.h>

#define PVA_MAKE_VERSION(_type_, _major_, _minor_, _subminor_)                 \
	(PVA_INSERT(_type_, 31, 24) | PVA_INSERT(_major_, 23, 16) |            \
	 PVA_INSERT(_minor_, 15, 8) | PVA_INSERT(_subminor_, 7, 0))

#define PVA_VERSION(_type_)                                                    \
	PVA_MAKE_VERSION(_type_, PVA_VERSION_MAJOR, PVA_VERSION_MINOR,         \
			 PVA_VERSION_SUBMINOR)

#endif
