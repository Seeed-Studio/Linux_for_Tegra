// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2015-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "ufs-provision.h"
#include "ufs-tegra.h"
#ifdef CONFIG_DEBUG_FS

#include <ufs/ufshcd.h>

void debugfs_provision_init(struct ufs_hba *hba, struct dentry *device_root)
{
}

void debugfs_provision_exit(struct ufs_hba *hba)
{
}
#endif
