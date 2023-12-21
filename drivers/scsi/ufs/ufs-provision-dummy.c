// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2015-2023 NVIDIA CORPORATION & AFFILIATES.  All rights reserved.

#include <nvidia/conftest.h>

#include "ufs-provision.h"
#include "ufs-tegra.h"
#ifdef CONFIG_DEBUG_FS

#if defined(NV_UFS_UFSHCD_H_PRESENT)
#include <ufs/ufshcd.h>
#else
#include <drivers-private/scsi/ufs/ufshcd.h>
#endif

void debugfs_provision_init(struct ufs_hba *hba, struct dentry *device_root)
{
}

void debugfs_provision_exit(struct ufs_hba *hba)
{
}
#endif
