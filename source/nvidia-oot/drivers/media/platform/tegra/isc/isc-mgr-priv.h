/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef __ISC_MGR_PRIV_H__
#define __ISC_MGR_PRIV_H__

#include <linux/cdev.h>
#include <linux/version.h>

struct isc_mgr_priv {
	struct device *pdev; /* parent device */
	struct device *dev; /* this device */
	dev_t devt;
	struct cdev cdev;
	struct class *isc_class;
	struct i2c_adapter *adap;
	struct isc_mgr_platform_data *pdata;
	struct list_head dev_list;
	struct mutex mutex;
	struct dentry *d_entry;
	struct work_struct ins_work;
	struct task_struct *t;
	struct kernel_siginfo sinfo;
	int sig_no; /* store signal number from user space */
	spinlock_t spinlock;
	atomic_t in_use;
	int err_irq;
	char devname[32];
	u32 pwr_state;
	atomic_t irq_in_use;
	struct pwm_device *pwm;
	wait_queue_head_t err_queue;
	bool err_irq_recvd;
};

int isc_mgr_power_up(struct isc_mgr_priv *isc_mgr, unsigned long arg);
int isc_mgr_power_down(struct isc_mgr_priv *isc_mgr, unsigned long arg);

int isc_mgr_debugfs_init(struct isc_mgr_priv *isc_mgr);
int isc_mgr_debugfs_remove(struct isc_mgr_priv *isc_mgr);

#endif  /* __ISC_MGR_PRIV_H__ */
