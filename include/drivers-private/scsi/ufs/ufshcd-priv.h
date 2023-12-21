// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.

#include <nvidia/conftest.h>

#include <linux/version.h>

#if defined(NV_UFS_UFSHCD_H_PRESENT)
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 7, 0)
#include <drivers-private/scsi/ufs/k61/ufshcd-priv.h>
#else
#include <drivers-private/scsi/ufs/k67/ufshcd-priv.h>
#endif
#else /* NV_UFS_UFSHCD_H_PRESENT */
#error "This header should not be included"
#endif
