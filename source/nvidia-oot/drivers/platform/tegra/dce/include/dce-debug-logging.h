/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef DCE_DEBUG_LOGGING_H
#define DCE_DEBUG_LOGGING_H

#include <dce-os-types.h>

int dbg_dce_log_help_fops_open(struct inode *inode, struct file *file);

int dbg_dce_log_fops_open(struct inode *inode, struct file *file);

ssize_t dbg_dce_log_fops_write(struct file *file,
			       const char __user *user_buf,
			       size_t count, loff_t *ppos);

#endif /* DCE_DEBUG_LOGGING_H */
