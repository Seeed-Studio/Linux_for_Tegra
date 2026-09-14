/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef DCE_DEBUG_PERF_H
#define DCE_DEBUG_PERF_H

#include <linux/debugfs.h>
#include <linux/uaccess.h>

int dbg_dce_perf_stats_stats_fops_open(struct inode *inode,
				       struct file *file);
ssize_t dbg_dce_perf_stats_stats_fops_write(struct file *file,
					    const char __user *user_buf,
					    size_t count, loff_t *ppos);

int dbg_dce_perf_stats_help_fops_open(struct inode *inode,
				      struct file *file);

ssize_t dbg_dce_perf_format_fops_write(struct file *file,
				       const char __user *user_buf,
				       size_t count, loff_t *ppos);
int dbg_dce_perf_format_fops_open(struct inode *inode, struct file *file);

int dbg_dce_perf_events_events_fops_open(struct inode *inode,
				 struct file *file);
ssize_t dbg_dce_perf_events_events_fops_write(struct file *file,
					      const char __user *user_buf,
					      size_t count, loff_t *ppos);
int dbg_dce_perf_events_help_fops_open(struct inode *inode,
				      struct file *file);
#endif
