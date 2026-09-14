/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_LINUX_H
#define PVA_KMD_LINUX_H

#include "pva_kmd.h"

#define PVA_LINUX_DEV_PATH_PREFIX "/dev/nvhost-ctrl-pva"

#define NVPVA_IOCTL_MAGIC 'Q'

#define PVA_KMD_IOCTL_GENERIC                                                  \
	_IOWR(NVPVA_IOCTL_MAGIC, 1, struct pva_kmd_linux_ioctl_header)

#define NVPVA_IOCTL_MAX_SIZE 256 //Temp value which can be updated later

struct nvpva_ioctl_part {
	void *addr;
	uint64_t size;
};

/**
 * The header of request to KMD
 */
struct pva_kmd_linux_ioctl_header {
	enum pva_ops_submit_mode mode;
	struct nvpva_ioctl_part request;
	struct nvpva_ioctl_part response;
	struct pva_fw_postfence postfence;
};

#endif // PVA_KMD_LINUX_H
