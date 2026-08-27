/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2023, NVIDIA Corporation.  All rights reserved.
 */

#ifndef PVA_FW_VERSION_H
#define PVA_FW_VERSION_H

#define VERSION_TYPE                                                           \
	(PVA_DEBUG | (SAFETY << 1) | (PVA_TEST_SUPPORT << 2) |                 \
	 (STANDALONE_TESTS << 3))

#define PVA_VERSION_MAJOR 0x08
#define PVA_VERSION_MINOR 0x02
#define PVA_VERSION_SUBMINOR 0x03

#ifndef PVA_VERSION_GCID_REVISION
#define PVA_VERSION_GCID_REVISION 0x00000000
#endif

#ifndef PVA_VERSION_BUILT_ON
#define PVA_VERSION_BUILT_ON 0x00000000
#endif

#endif
