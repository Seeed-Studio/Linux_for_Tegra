// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.

#include <nvidia/conftest.h>

#include <linux/version.h>

#if defined(NV_UFS_UFSHCD_H_PRESENT)
#error "Use headers from core kernel"
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 16, 0)
#include <drivers-private/scsi/ufs/k515/unipro.h>
#else
#include <drivers-private/scsi/ufs/k516/unipro.h>
#endif
#endif
