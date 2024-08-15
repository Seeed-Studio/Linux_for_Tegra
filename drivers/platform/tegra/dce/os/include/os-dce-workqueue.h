/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#ifndef NVDISPLAY_SERVER_OS_DCE_WQ_H
#define NVDISPLAY_SERVER_OS_DCE_WQ_H

#ifdef __KERNEL__
#include <dce-workqueue.h>
#elif defined(NVDISPLAY_SERVER_HVRTOS)
#include <hvrtos/os-dce-workqueue.h>
#else
#error "OS Not Supported"
#endif

int dce_init_work(struct tegra_dce *d,
           struct dce_work_struct *work,
           void (*work_fn)(struct tegra_dce *d));

void dce_schedule_work(struct dce_work_struct *work);

#endif /* NVDISPLAY_SERVER_OS_DCE_WQ_H */