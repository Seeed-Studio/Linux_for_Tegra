/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES.  All rights reserved. */
/*
 * This file is derived from the Linux kernel's drivers/ufs/core/ufshcd-priv.h
 * header file, but only the necessary prototypes used by the Tegra UFS driver
 * are copied. These functions were first introduced in Linux v4.17 and are
 * compatible with kernels upto Linux v6.15. Linux kernels newer than v6.15 need
 * to be verified.
 */

#ifndef _UFSHCD_PRIV_H_
#define _UFSHCD_PRIV_H_

#include <ufs/ufshcd.h>

int ufshcd_query_descriptor_retry(struct ufs_hba *hba,
				  enum query_opcode opcode,
				  enum desc_idn idn, u8 index,
				  u8 selector,
				  u8 *desc_buf, int *buf_len);
int ufshcd_query_attr(struct ufs_hba *hba, enum query_opcode opcode,
		      enum attr_idn idn, u8 index, u8 selector, u32 *attr_val);

#endif /* _UFSHCD_PRIV_H_ */
