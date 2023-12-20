// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.

#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 16, 0)
#include <drivers-private/scsi/ufs/k515/ufshcd.h>
#elif LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
#include <drivers-private/scsi/ufs/k516/ufshcd.h>
#elif LINUX_VERSION_CODE < KERNEL_VERSION(6, 7, 0)
#include <drivers-private/scsi/ufs/k61/ufshcd.h>
#else
#error "Use headers from core kernel"
#endif
