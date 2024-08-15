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

#ifndef NVDISPLAY_SERVER_TYPES_H
#define NVDISPLAY_SERVER_TYPES_H

#ifdef __KERNEL__
#include <linux/types.h>
#elif defined(NVDISPLAY_SERVER_HVRTOS)
#include "hvrtos/types.h"
#else
#error "OS Not Supported"
#endif

#endif /* NVDISPLAY_SERVER_TYPES_H */