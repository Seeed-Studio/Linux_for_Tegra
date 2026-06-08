// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2015-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#ifndef __CDI_MGR_PRIV_H__
#define __CDI_MGR_PRIV_H__

#include <linux/cdev.h>
#include <linux/hrtimer.h>
#include <media/cdi-mgr.h>
#include "cdi-tca-priv.h"

#define CDI_MGR_STOP_GPIO_INTR_EVENT_WAIT	(~(0u))
#define CDI_MGR_TCA9539_REGISTER_COUNT      (8)
#define CDI_MGR_TCA9539_BASE_REG_ADDR       (0x00)

#define CDI_MGR_GPIO_EVENT_QUEUE_SIZE		(8)
#define CDI_MGR_GPIO_TIMER_QUEUE_SIZE		(4)

enum cam_gpio_direction {
	CAM_DEVBLK_GPIO_UNUSED = 0,
	CAM_DEVBLK_GPIO_INPUT_INTR,
	CAM_DEVBLK_GPIO_OUTPUT
};

struct cam_gpio_timer_queue {
	struct hrtimer timer;
	ktime_t expires[CDI_MGR_GPIO_TIMER_QUEUE_SIZE];
	u32 head;
	u32 count;
};

struct cam_gpio_config {
	struct cdi_mgr_priv *mgr;
	u32 idx;
	enum cam_gpio_direction gpio_dir;
	struct gpio_desc *desc;
	int intr_irq;
	struct cam_gpio_timer_queue timers;
	ktime_t timeout;
};

struct cam_gpio_event_queue {
	wait_queue_head_t wait;
	struct cdi_mgr_gpio_intr events[CDI_MGR_GPIO_EVENT_QUEUE_SIZE];
	u32 head;
	u32 count;
};

struct cdi_mgr_priv {
	struct device *pdev; /* parent device */
	struct device *dev; /* this device */
	dev_t devt;
	struct cdev cdev;
	struct class *cdi_class;
	struct i2c_adapter *adap;
	struct cdi_mgr_platform_data *pdata;
	struct list_head dev_list;
	struct mutex mutex;
	struct dentry *d_entry;
	struct work_struct ins_work;
	struct task_struct *t;
	struct kernel_siginfo sinfo;
	int sig_no; /* store signal number from user space */
	spinlock_t spinlock;
	atomic_t in_use;
	char devname[32];
	u32 pwr_state;
	atomic_t irq_in_use;
	struct pwm_device *pwm;
	u8 des_pwr_method;
	u8 des_pwr_i2c_addr;
	struct tca9539_priv tca9539;
	struct cam_gpio_config gpios[MAX_CDI_GPIOS];
	uint32_t num_gpios;
	bool intrs_enable;
	struct cam_gpio_event_queue gpio_events;
	bool stop_err_irq_wait;
	u8 cim_ver; /* 1 - P3714 A01, 2 - P3714 A02/A03 */
	u32 cim_frsync[3]; /* FRSYNC source selection for each muxer */
	u8 pre_suspend_tca9539_regvals[CDI_MGR_TCA9539_REGISTER_COUNT];
	bool isP3898;
};

int cdi_mgr_power_up(struct cdi_mgr_priv *cdi_mgr, unsigned long arg);
int cdi_mgr_power_down(struct cdi_mgr_priv *cdi_mgr, unsigned long arg);

int cdi_mgr_debugfs_init(struct cdi_mgr_priv *cdi_mgr);
int cdi_mgr_debugfs_remove(struct cdi_mgr_priv *cdi_mgr);

#endif  /* __CDI_MGR_PRIV_H__ */
