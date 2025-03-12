/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2019-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_PACKED_H
#define PVA_PACKED_H
/**
 * @brief Packed attribute that avoids compiler to add any paddings.
 *        Compiler implicitly adds padding between the structure members
 *        to make it aligned. To avoid this packed attribute is used.
 *        Packed is for shared structures between KMD and FW.
 *        If packed is not used, then we depend on what padding the compiler adds.
 *        Since KMD and FW are compiled by two different compilers, we need to
 *        ensure that the offsets of each member of the structure is the same in
 *        both KMD and FW. To ensure this we pack the structure.
 */
#define PVA_PACKED __attribute__((packed))
#endif // PVA_PACKED_H
