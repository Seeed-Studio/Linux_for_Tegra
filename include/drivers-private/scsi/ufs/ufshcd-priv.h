// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2023-2025, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.

#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 7, 0)
#include <drivers-private/scsi/ufs/k61/ufshcd-priv.h>
#else
#include <drivers-private/scsi/ufs/k67/ufshcd-priv.h>
#endif
