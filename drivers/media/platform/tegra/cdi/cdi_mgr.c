// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2015-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/of_irq.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <asm/siginfo.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/i2c.h>
#include <linux/pwm.h>
#include <linux/debugfs.h>
#include <linux/nospec.h>
#include <linux/seq_file.h>
#include <media/cdi-dev.h>
#include <media/cdi-mgr.h>
#include <linux/gpio/consumer.h>
#include <linux/semaphore.h>

#include <asm/barrier.h>

#include "cdi-mgr-priv.h"

#define PW_ON(flag)	((flag) ? 0 : 1)
#define PW_OFF(flag)	((flag) ? 1 : 0)

/* i2c payload size is only 12 bit */
#define MAX_MSG_SIZE	(0xFFF - 1)

/* minor number range would be 0 to 127 */
#define CDI_DEV_MAX	128

#define TIMEOUT_US 2000000 /* 2 seconds */

static struct semaphore tca9539_sem;

/* CDI Dev Debugfs functions
 *
 *    - cdi_mgr_debugfs_init
 *    - cdi_mgr_debugfs_remove
 *    - cdi_mgr_status_show
 *    - cdi_mgr_attr_set
 *    - pwr_on_get
 *    - pwr_on_set
 *    - pwr_off_get
 *    - pwr_off_set
 */
static int cdi_mgr_status_show(struct seq_file *s, void *data)
{
	struct cdi_mgr_priv *cdi_mgr = s->private;
	struct cdi_mgr_client *cdi_dev;

	if (cdi_mgr == NULL)
		return 0;
	pr_info("%s - %s\n", __func__, cdi_mgr->devname);

	if (list_empty(&cdi_mgr->dev_list)) {
		seq_printf(s, "%s: No devices supported.\n", cdi_mgr->devname);
		return 0;
	}

	mutex_lock(&cdi_mgr->mutex);
	list_for_each_entry_reverse(cdi_dev, &cdi_mgr->dev_list, list) {
		seq_printf(s, "    %02d  --  @0x%02x, %02d, %d, %s\n",
			cdi_dev->id,
			cdi_dev->cfg.addr,
			cdi_dev->cfg.reg_bits,
			cdi_dev->cfg.val_bits,
			cdi_dev->cfg.drv_name
			);
	}
	mutex_unlock(&cdi_mgr->mutex);

	return 0;
}

static ssize_t cdi_mgr_attr_set(struct file *s,
		const char __user *user_buf, size_t count, loff_t *ppos)
{
	return count;
}

static int cdi_mgr_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, cdi_mgr_status_show, inode->i_private);
}

static const struct file_operations cdi_mgr_debugfs_fops = {
	.open = cdi_mgr_debugfs_open,
	.read = seq_read,
	.write = cdi_mgr_attr_set,
	.llseek = seq_lseek,
	.release = single_release,
};

static int pwr_on_get(void *data, u64 *val)
{
	struct cdi_mgr_priv *cdi_mgr = data;

	if (cdi_mgr->pdata == NULL || !cdi_mgr->pdata->num_pwr_gpios) {
		*val = 0ULL;
		return 0;
	}

	*val = (cdi_mgr->pwr_state & (BIT(28) - 1)) |
		((cdi_mgr->pdata->num_pwr_gpios & 0x0f) << 28);
	return 0;
}

static int pwr_on_set(void *data, u64 val)
{
	return cdi_mgr_power_up((struct cdi_mgr_priv *)data, val);
}

DEFINE_SIMPLE_ATTRIBUTE(pwr_on_fops, pwr_on_get, pwr_on_set, "0x%02llx\n");

static int pwr_off_get(void *data, u64 *val)
{
	struct cdi_mgr_priv *cdi_mgr = data;

	if (cdi_mgr->pdata == NULL || !cdi_mgr->pdata->num_pwr_gpios) {
		*val = 0ULL;
		return 0;
	}

	*val = (~cdi_mgr->pwr_state) & (BIT(cdi_mgr->pdata->num_pwr_gpios) - 1);
	*val = (*val & (BIT(28) - 1)) |
		((cdi_mgr->pdata->num_pwr_gpios & 0x0f) << 28);
	return 0;
}

static int pwr_off_set(void *data, u64 val)
{
	return cdi_mgr_power_down((struct cdi_mgr_priv *)data, val);
}

DEFINE_SIMPLE_ATTRIBUTE(pwr_off_fops, pwr_off_get, pwr_off_set, "0x%02llx\n");

static int tca9539_wr(
	struct cdi_mgr_priv *info, unsigned int offset, u8 val)
{
	int ret = -ENODEV;

	dev_dbg(info->dev, "%s\n", __func__);
	mutex_lock(&info->mutex);

	ret = tca9539_raw_wr(info->dev, &info->tca9539, offset, val);

	mutex_unlock(&info->mutex);
	return ret;
}

static int tca9539_rd(
	struct cdi_mgr_priv *info, unsigned int offset, u8 *val)
{
	int ret = -ENODEV;

	dev_dbg(info->dev, "%s\n", __func__);
	mutex_lock(&info->mutex);

	ret = tca9539_raw_rd(info->dev, &info->tca9539, offset, val);

	mutex_unlock(&info->mutex);

	return ret;
}

int cdi_mgr_debugfs_init(struct cdi_mgr_priv *cdi_mgr)
{
	struct dentry *d;

	dev_dbg(cdi_mgr->dev, "%s %s\n", __func__, cdi_mgr->devname);
	cdi_mgr->d_entry = debugfs_create_dir(
		cdi_mgr->devname, NULL);
	if (cdi_mgr->d_entry == NULL) {
		dev_err(cdi_mgr->dev, "%s: create dir failed\n", __func__);
		return -ENOMEM;
	}

	d = debugfs_create_file("map", 0644, cdi_mgr->d_entry,
		(void *)cdi_mgr, &cdi_mgr_debugfs_fops);
	if (!d)
		goto debugfs_init_err;

	d = debugfs_create_file("pwr-on", 0644, cdi_mgr->d_entry,
		(void *)cdi_mgr, &pwr_on_fops);
	if (!d)
		goto debugfs_init_err;

	d = debugfs_create_file("pwr-off", 0644, cdi_mgr->d_entry,
		(void *)cdi_mgr, &pwr_off_fops);
	if (!d)
		goto debugfs_init_err;

	return 0;

debugfs_init_err:
	dev_err(cdi_mgr->dev, "%s: create file failed\n", __func__);
	debugfs_remove_recursive(cdi_mgr->d_entry);
	cdi_mgr->d_entry = NULL;
	return -ENOMEM;
}

int cdi_mgr_debugfs_remove(struct cdi_mgr_priv *cdi_mgr)
{
	if (cdi_mgr->d_entry == NULL)
		return 0;
	debugfs_remove_recursive(cdi_mgr->d_entry);
	cdi_mgr->d_entry = NULL;
	return 0;
}

static int cdi_mgr_gpio_eq_event(
	struct cdi_mgr_priv *cdi_mgr, struct cdi_mgr_gpio_intr evt)
{
	struct cam_gpio_event_queue *queue;
	u32 queue_cap;

	if (!cdi_mgr)
		return -EINVAL;

	queue = &cdi_mgr->gpio_events;
	queue_cap = ARRAY_SIZE(queue->events);

	if (queue->count >= queue_cap) {
		dev_err(cdi_mgr->dev,
			"%s: failed to enqueue interrupt event, overflow\n",
			__func__);
		return -ENOMEM;
	}

	queue->events[(queue->head+queue->count)%queue_cap] = evt;
	queue->count++;

	return 0;
}

static int cdi_mgr_gpio_dq_event(
	struct cdi_mgr_priv *cdi_mgr, struct cdi_mgr_gpio_intr *evt)
{
	struct cam_gpio_event_queue *queue;
	u32 queue_cap;

	if (!cdi_mgr || !evt)
		return -EINVAL;

	queue = &cdi_mgr->gpio_events;
	queue_cap = ARRAY_SIZE(queue->events);

	if (queue->count == 0)
		return -EIO;

	*evt = queue->events[queue->head];
	queue->head = (queue->head+1) % queue_cap;
	queue->count--;

	return 0;
}

static int cdi_mgr_gpio_eq_intr_timer(
	struct cam_gpio_config *pin)
{
	struct cdi_mgr_priv *cdi_mgr;
	struct cam_gpio_timer_queue *queue;
	u32 queue_cap;
	ktime_t timeout_abs;
	struct cdi_mgr_gpio_intr event;

	if (!pin || !pin->mgr)
		return -EINVAL;

	/* Skip enqueue if timeout is disabled */
	if (ktime_to_ms(pin->timeout) <= 0)
		return 0;

	cdi_mgr = pin->mgr;
	queue = &pin->timers;
	queue_cap = ARRAY_SIZE(queue->expires);

	if (queue->count >= queue_cap) {
		dev_err(cdi_mgr->dev,
			"%s: failed to enqueue interrupt timer, overflow\n",
			__func__);

		/* Report cdi-mgr fault */
		event.idx = pin->idx;
		event.code = CDI_MGR_GPIO_INTR_FAULT;
		if (cdi_mgr_gpio_eq_event(cdi_mgr, event) != 0)
			dev_err(cdi_mgr->dev,
				"%s: failed to enqueue cdi-mgr fault event, "
				"idx %d\n", __func__, pin->idx);

		return -ENOMEM;
	}

	timeout_abs = ktime_add(ktime_get(), pin->timeout);

	if (!hrtimer_active(&queue->timer)) {
		/* Start the interrupt timer immediately */
		hrtimer_start(&queue->timer, timeout_abs, HRTIMER_MODE_ABS);
	} else {
		/* Enqueue interrupt timer */
		queue->expires[(queue->head+queue->count)%queue_cap] = timeout_abs;
		queue->count++;
	}

	return 0;
}

static int cdi_mgr_gpio_dq_intr_timer(
	struct cam_gpio_config *pin)
{
	unsigned long flags;
	struct cdi_mgr_priv *cdi_mgr;
	struct cam_gpio_timer_queue *queue;
	u32 queue_cap;
	ktime_t *expire;
	struct cdi_mgr_gpio_intr event;

	if (!pin || !pin->mgr)
		return -EINVAL;

	cdi_mgr = pin->mgr;
	queue = &pin->timers;
	queue_cap = ARRAY_SIZE(queue->expires);

	spin_lock_irqsave(&cdi_mgr->spinlock, flags);

	/* Report timer expiry */
	event.idx = pin->idx;
	event.code = CDI_MGR_GPIO_INTR_TIMEOUT;
	if (cdi_mgr_gpio_eq_event(cdi_mgr, event) != 0)
		dev_err(cdi_mgr->dev,
			"%s: failed to enqueue interrupt timeout event, "
			"idx %d\n", __func__, pin->idx);

	/* Dequeue next interrupt timer */
	if (queue->count == 0)
		goto unlock;

	expire = &queue->expires[queue->head];
	hrtimer_start(&queue->timer, *expire, HRTIMER_MODE_ABS);
	queue->head = (queue->head+1) % queue_cap;
	queue->count--;

unlock:
	spin_unlock_irqrestore(&cdi_mgr->spinlock, flags);

	wake_up_interruptible(&cdi_mgr->gpio_events.wait);

	return 0;
}

static irqreturn_t cdi_mgr_isr(int irq, void *data)
{
	struct cdi_mgr_priv *cdi_mgr;
	int ret;
	unsigned long flags;
	int i = 0;
	struct cam_gpio_config *pin;
	struct cdi_mgr_gpio_intr event;

	if (data) {
		cdi_mgr = (struct cdi_mgr_priv *)data;
		if (!cdi_mgr->intrs_enable)
			return IRQ_HANDLED;

		spin_lock_irqsave(&cdi_mgr->spinlock, flags);
		for (i = 0; i < cdi_mgr->num_gpios; i++) {
			pin = &cdi_mgr->gpios[i];
			if (irq != pin->intr_irq)
				continue;
			event.idx = pin->idx;
			event.code = CDI_MGR_GPIO_INTR;
			if (cdi_mgr_gpio_eq_event(cdi_mgr, event) != 0)
				pr_err("Failed to enqueue interrupt event, "
					"idx %d\n", pin->idx);
			if (cdi_mgr_gpio_eq_intr_timer(pin) != 0)
				pr_err("Failed to enqueue interrupt timer, "
					"idx %d\n", pin->idx);
		}
		spin_unlock_irqrestore(&cdi_mgr->spinlock, flags);

		wake_up_interruptible(&cdi_mgr->gpio_events.wait);

		spin_lock_irqsave(&cdi_mgr->spinlock, flags);
		if (cdi_mgr->sinfo.si_signo && cdi_mgr->t) {
			/* send the signal to user space */
			ret = send_sig_info(cdi_mgr->sinfo.si_signo,
					&cdi_mgr->sinfo,
					cdi_mgr->t);
			if (ret < 0) {
				pr_err("error sending signal\n");
			}
		}
		spin_unlock_irqrestore(&cdi_mgr->spinlock, flags);
	}

	return IRQ_HANDLED;
}

static enum hrtimer_restart cdi_mgr_intr_timer(struct hrtimer *timer)
{
	struct cam_gpio_timer_queue *queue;
	struct cam_gpio_config *pin;

	if (timer == NULL)
		goto error;

	queue = container_of(timer, struct cam_gpio_timer_queue, timer);
	pin = container_of(queue, struct cam_gpio_config, timers);

	if (cdi_mgr_gpio_dq_intr_timer(pin) != 0)
		goto error;

	goto done;

error:
	pr_err("Failed to handle GPIO interrupt timer expiry\n");

done:
	return HRTIMER_NORESTART;
}

int cdi_delete_lst(struct device *dev, struct i2c_client *client)
{
	struct cdi_mgr_priv *cdi_mgr;
	struct cdi_mgr_client *cdi_dev;

	if (dev == NULL)
		return -EFAULT;

	cdi_mgr = (struct cdi_mgr_priv *)dev_get_drvdata(dev);

	mutex_lock(&cdi_mgr->mutex);
	list_for_each_entry(cdi_dev, &cdi_mgr->dev_list, list) {
		if (cdi_dev->client == client) {
			list_del(&cdi_dev->list);
			break;
		}
	}
	mutex_unlock(&cdi_mgr->mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(cdi_delete_lst);

static int cdi_remove_dev(struct cdi_mgr_priv *cdi_mgr, unsigned long arg)
{
	struct cdi_mgr_client *cdi_dev;

	dev_dbg(cdi_mgr->dev, "%s %ld\n", __func__, arg);
	mutex_lock(&cdi_mgr->mutex);
	list_for_each_entry(cdi_dev, &cdi_mgr->dev_list, list) {
		if (cdi_dev->id == arg) {
			list_del(&cdi_dev->list);
			break;
		}
	}
	mutex_unlock(&cdi_mgr->mutex);

	if (&cdi_dev->list != &cdi_mgr->dev_list)
		i2c_unregister_device(cdi_dev->client);
	else
		dev_err(cdi_mgr->dev, "%s: list %lx un-exist\n", __func__, arg);

	return 0;
}

static int __cdi_create_dev(
	struct cdi_mgr_priv *cdi_mgr, struct cdi_mgr_new_dev *new_dev)
{
	struct cdi_mgr_client *cdi_dev;
	struct i2c_board_info brd;
	int err = 0;

	if (new_dev->addr >= 0x80 || new_dev->drv_name[0] == '\0' ||
		(new_dev->val_bits != 8 && new_dev->val_bits != 16) ||
		(new_dev->reg_bits != 0 && new_dev->reg_bits != 8 &&
		new_dev->reg_bits != 16)) {
		dev_err(cdi_mgr->dev,
			"%s: invalid cdi dev params: %s %x %d %d\n",
			__func__, new_dev->drv_name, new_dev->addr,
			new_dev->reg_bits, new_dev->val_bits);
		return -EINVAL;
	}

	cdi_dev = devm_kzalloc(cdi_mgr->dev, sizeof(*cdi_dev), GFP_KERNEL);
	if (!cdi_dev) {
		dev_err(cdi_mgr->dev, "Unable to allocate memory!\n");
		return -ENOMEM;
	}

	memcpy(&cdi_dev->cfg, new_dev, sizeof(cdi_dev->cfg));
	dev_dbg(cdi_mgr->pdev, "%s - %s @ %x, %d %d\n", __func__,
		cdi_dev->cfg.drv_name, cdi_dev->cfg.addr,
		cdi_dev->cfg.reg_bits, cdi_dev->cfg.val_bits);

	cdi_dev->pdata.drv_name[sizeof(cdi_dev->pdata.drv_name) - 1] = '\0';
	cdi_dev->cfg.drv_name[sizeof(cdi_dev->cfg.drv_name) - 1] = '\0';
	err = snprintf(cdi_dev->pdata.drv_name, sizeof(cdi_dev->pdata.drv_name) - 1,
		       "%s.%u.%02x", cdi_dev->cfg.drv_name,
		       cdi_mgr->adap->nr, cdi_dev->cfg.addr);

	if (err < 0) {
		dev_err(cdi_mgr->dev, "output error: %d", err);
		goto dev_create_err;
	}

	cdi_dev->pdata.reg_bits = cdi_dev->cfg.reg_bits;
	cdi_dev->pdata.val_bits = cdi_dev->cfg.val_bits;
	cdi_dev->pdata.pdev = cdi_mgr->dev;
	cdi_dev->pdata.np = cdi_mgr->pdev->of_node;

	mutex_init(&cdi_dev->mutex);
	INIT_LIST_HEAD(&cdi_dev->list);

	memset(&brd, 0, sizeof(brd));
	strncpy(brd.type, "cdi-dev", sizeof(brd.type));
	brd.addr = cdi_dev->cfg.addr;
	brd.platform_data = &cdi_dev->pdata;
	cdi_dev->client = i2c_new_client_device(cdi_mgr->adap, &brd);
	if (!cdi_dev->client) {
		dev_err(cdi_mgr->dev,
			"%s cannot allocate client: %s bus %d, %x\n", __func__,
			cdi_dev->pdata.drv_name, cdi_mgr->adap->nr, brd.addr);
		err = -EINVAL;
		goto dev_create_err;
	}

	mutex_lock(&cdi_mgr->mutex);
	if (!list_empty(&cdi_mgr->dev_list))
		cdi_dev->id = list_entry(cdi_mgr->dev_list.next,
			struct cdi_mgr_client, list)->id + 1;
	list_add(&cdi_dev->list, &cdi_mgr->dev_list);
	mutex_unlock(&cdi_mgr->mutex);

	return cdi_dev->id;

dev_create_err:
	devm_kfree(cdi_mgr->dev, cdi_dev);
	return err;
}

static int cdi_create_dev(struct cdi_mgr_priv *cdi_mgr, const void __user *arg)
{
	struct cdi_mgr_new_dev d_cfg;

	if (copy_from_user(&d_cfg, arg, sizeof(d_cfg))) {
		dev_err(cdi_mgr->pdev,
			"%s: failed to copy from user\n", __func__);
		return -EFAULT;
	}

	return __cdi_create_dev(cdi_mgr, &d_cfg);
}

static int cdi_mgr_write_pid(struct file *file, const void __user *arg)
{
	struct cdi_mgr_priv *cdi_mgr = file->private_data;
	struct cdi_mgr_sinfo sinfo;
	unsigned long flags;

	if (copy_from_user(&sinfo, arg, sizeof(sinfo))) {
		dev_err(cdi_mgr->pdev,
			"%s: failed to copy from user\n", __func__);
		return -EFAULT;
	}

	if (cdi_mgr->sinfo.si_int) {
		dev_err(cdi_mgr->pdev, "exist signal info\n");
		return -EINVAL;
	}

	if ((sinfo.sig_no < SIGRTMIN) || (sinfo.sig_no > SIGRTMAX)) {
		dev_err(cdi_mgr->pdev, "Invalid signal number\n");
		return -EINVAL;
	}

	if (!sinfo.pid) {
		dev_err(cdi_mgr->pdev, "Invalid PID\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&cdi_mgr->spinlock, flags);
	cdi_mgr->sinfo.si_signo = cdi_mgr->sig_no = sinfo.sig_no;
	cdi_mgr->sinfo.si_code = SI_QUEUE;
	cdi_mgr->sinfo.si_ptr = (void __user *)((unsigned long)sinfo.context);
	spin_unlock_irqrestore(&cdi_mgr->spinlock, flags);

	rcu_read_lock();
	cdi_mgr->t = pid_task(find_pid_ns(sinfo.pid, &init_pid_ns),
				PIDTYPE_PID);
	if (cdi_mgr->t == NULL) {
		dev_err(cdi_mgr->pdev, "no such pid\n");
		rcu_read_unlock();
		return -ENODEV;
	}
	rcu_read_unlock();

	return 0;
}

static int cdi_mgr_get_pwr_info(struct cdi_mgr_priv *cdi_mgr,
				void __user *arg)
{
	struct cdi_mgr_platform_data *pd = cdi_mgr->pdata;
	struct cdi_mgr_pwr_info pinfo;
	int err;

	if (copy_from_user(&pinfo, arg, sizeof(pinfo))) {
		dev_err(cdi_mgr->pdev,
			"%s: failed to copy from user\n", __func__);
		return -EFAULT;
	}

	if (!pd->num_pwr_gpios) {
		dev_err(cdi_mgr->pdev,
			"%s: no power gpios\n", __func__);
		pinfo.pwr_status = -1;
		err = -ENODEV;
		goto pwr_info_end;
	}

	if (pinfo.pwr_gpio >= pd->num_pwr_gpios || pinfo.pwr_gpio < 0) {
		dev_err(cdi_mgr->pdev,
			"%s: invalid power gpio provided\n", __func__);
		pinfo.pwr_status = -1;
		err = -EINVAL;
		goto pwr_info_end;
	}

	pinfo.pwr_gpio = array_index_nospec(pinfo.pwr_gpio, pd->num_pwr_gpios);

	pinfo.pwr_status  = gpio_get_value(pd->pwr_gpios[pinfo.pwr_gpio]);
	err = 0;

pwr_info_end:
	if (copy_to_user(arg, &pinfo, sizeof(pinfo))) {
		dev_err(cdi_mgr->pdev,
			"%s: failed to copy to user\n", __func__);
		return -EFAULT;
	}
	return err;
}

static int cdi_mgr_get_pwr_ctrl_info(struct cdi_mgr_priv *cdi_mgr,
				void __user *arg)
{
	struct cdi_mgr_pwr_ctrl_info pinfo;
	int err = 0;

	if (copy_from_user(&pinfo, arg, sizeof(pinfo))) {
		dev_err(cdi_mgr->pdev,
			"%s: failed to copy from user\n", __func__);
		return -EFAULT;
	}

	pinfo.des_pwr_method = cdi_mgr->des_pwr_method;
	pinfo.des_pwr_i2c_addr = cdi_mgr->des_pwr_i2c_addr;

	if (copy_to_user(arg, &pinfo, sizeof(pinfo))) {
		dev_err(cdi_mgr->pdev,
			"%s: failed to copy to user\n", __func__);
		return -EFAULT;
	}
	return err;
}

int cdi_mgr_power_up(struct cdi_mgr_priv *cdi_mgr, unsigned long arg)
{
	struct cdi_mgr_platform_data *pd = cdi_mgr->pdata;
	int i;
	u32 pwr_gpio;

	dev_dbg(cdi_mgr->pdev, "%s - %lu\n", __func__, arg);

	if (!pd->num_pwr_gpios)
		goto pwr_up_end;

	if (arg >= MAX_CDI_GPIOS)
		arg = MAX_CDI_GPIOS - 1;

	arg = array_index_nospec(arg, MAX_CDI_GPIOS);
	pwr_gpio = pd->pwr_mapping[arg];

	if (pwr_gpio < pd->num_pwr_gpios) {
		pwr_gpio = array_index_nospec(pwr_gpio, pd->num_pwr_gpios);
		gpio_set_value(pd->pwr_gpios[pwr_gpio],
			PW_ON(pd->pwr_flags[pwr_gpio]));
		cdi_mgr->pwr_state |= BIT(pwr_gpio);
		return 0;
	}

	for (i = 0; i < pd->num_pwr_gpios; i++) {
		dev_dbg(cdi_mgr->pdev, "  - %d, %d\n",
			pd->pwr_gpios[i], PW_ON(pd->pwr_flags[i]));
		gpio_set_value(pd->pwr_gpios[i], PW_ON(pd->pwr_flags[i]));
		/* SW WAR for platform issue */
		/* Add 5ms delay between two gpio toggles */
		/* Bug 4125801*/
		if (cdi_mgr->isP3898)
			mdelay(5);
		cdi_mgr->pwr_state |= BIT(i);
	}

pwr_up_end:
	return 0;
}

int cdi_mgr_power_down(struct cdi_mgr_priv *cdi_mgr, unsigned long arg)
{
	struct cdi_mgr_platform_data *pd = cdi_mgr->pdata;
	int i;
	u32 pwr_gpio;

	dev_dbg(cdi_mgr->pdev, "%s - %lx\n", __func__, arg);

	if (!pd->num_pwr_gpios)
		goto pwr_dn_end;

	if (arg >= MAX_CDI_GPIOS)
		arg = MAX_CDI_GPIOS - 1;

	arg = array_index_nospec(arg, MAX_CDI_GPIOS);

	pwr_gpio = pd->pwr_mapping[arg];

	if (pwr_gpio < pd->num_pwr_gpios) {
		pwr_gpio = array_index_nospec(pwr_gpio, pd->num_pwr_gpios);
		gpio_set_value(pd->pwr_gpios[pwr_gpio],
				PW_OFF(pd->pwr_flags[pwr_gpio]));
		cdi_mgr->pwr_state &= ~BIT(pwr_gpio);
		return 0;
	}

	for (i = 0; i < pd->num_pwr_gpios; i++) {
		dev_dbg(cdi_mgr->pdev, "  - %d, %d\n",
			pd->pwr_gpios[i], PW_OFF(pd->pwr_flags[i]));
		gpio_set_value(pd->pwr_gpios[i], PW_OFF(pd->pwr_flags[i]));
		cdi_mgr->pwr_state &= ~BIT(i);
	}
	mdelay(7);

pwr_dn_end:
	return 0;
}

static int cdi_mgr_mcdi_ctrl(struct cdi_mgr_priv *cdi_mgr, bool mcdi_on)
{
	struct cdi_mgr_platform_data *pd = cdi_mgr->pdata;
	int err, i;

	dev_dbg(cdi_mgr->pdev, "%s - %s\n", __func__, mcdi_on ? "ON" : "OFF");

	if (!pd->num_mcdi_gpios)
		return 0;

	for (i = 0; i < pd->num_mcdi_gpios; i++) {
		if (mcdi_on) {
			if (gpio_request(pd->mcdi_gpios[i],
							"mcdi-gpio")) {
				dev_err(cdi_mgr->pdev, "failed req GPIO: %d\n",
					pd->mcdi_gpios[i]);
				goto mcdi_ctrl_err;
			}

			err = gpio_direction_output(
				pd->mcdi_gpios[i], PW_ON(pd->mcdi_flags[i]));
		} else {
			err = gpio_direction_output(
				pd->mcdi_gpios[i], PW_OFF(pd->mcdi_flags[i]));
			gpio_free(pd->mcdi_gpios[i]);
		}
	}
	return 0;

mcdi_ctrl_err:
	for (; i >= 0; i--)
		gpio_free(pd->mcdi_gpios[i]);
	return -EBUSY;
}

static int cdi_mgr_pwm_enable(
	struct cdi_mgr_priv *cdi_mgr, unsigned long arg)
{
	int err = 0;

	if (!cdi_mgr || !cdi_mgr->pwm)
		return -EINVAL;

	switch (arg) {
	case CDI_MGR_PWM_ENABLE:
		err = pwm_enable(cdi_mgr->pwm);
		break;
	case CDI_MGR_PWM_DISABLE:
		pwm_disable(cdi_mgr->pwm);
		break;
	default:
		dev_err(cdi_mgr->pdev, "%s unrecognized command: %lx\n",
			__func__, arg);
	}

	return err;
}

static int cdi_mgr_pwm_config(
	struct cdi_mgr_priv *cdi_mgr, const void __user *arg)
{
	struct cdi_mgr_pwm_info pwm_cfg;
	int err = 0;

	if (!cdi_mgr || !cdi_mgr->pwm)
		return -EINVAL;

	if (copy_from_user(&pwm_cfg, arg, sizeof(pwm_cfg))) {
		dev_err(cdi_mgr->pdev,
			"%s: failed to copy from user\n", __func__);
		return -EFAULT;
	}

	err = pwm_config(cdi_mgr->pwm, pwm_cfg.duty_ns, pwm_cfg.period_ns);

	return err;
}

static u32 gpio_to_idx(
	struct cdi_mgr_priv *cdi_mgr, u32 idx)
{
	int i;

	for (i = 0; i < cdi_mgr->num_gpios; i++)
		if (cdi_mgr->gpios[i].idx == idx)
			return i;

	return MAX_CDI_GPIOS;
}

static int cdi_mgr_gpio_config(
	struct cdi_mgr_priv *cdi_mgr, const void __user *arg)
{
	int err = 0;

	struct cdi_mgr_gpio_info config;
	u32 idx;
	struct cam_gpio_config *pin;

	if (!cdi_mgr)
		return -EINVAL;

	if (copy_from_user(&config, arg, sizeof(config))) {
		dev_err(cdi_mgr->pdev,
			"%s: failed to copy from user\n", __func__);
		return -EFAULT;
	}

	idx = gpio_to_idx(cdi_mgr, config.idx);
	if (idx >= MAX_CDI_GPIOS) {
		dev_err(cdi_mgr->pdev, "%s: unknown gpio idx %u\n",
			__func__, config.idx);
		/* WAR: nvbugs/4087689 */
		/* return -EFAULT; */
		return 0;
	}

	pin = &cdi_mgr->gpios[idx];

	/* Interrupt timeout */
	if ((config.timeout_ms > 0) &&
			(pin->gpio_dir != CAM_DEVBLK_GPIO_INPUT_INTR))
		dev_warn(cdi_mgr->pdev,
			"%s: gpio idx %u setting timeout for non-interrupt "
			"type pin has no effect\n", __func__, idx);

	pin->timeout = ms_to_ktime(config.timeout_ms);

	return err;
}

static int cdi_mgr_gpio_intr_arm(
	struct cdi_mgr_priv *cdi_mgr)
{
	int i;
	struct cam_gpio_config *pin;

	if (!cdi_mgr)
		return -EINVAL;

	if (!atomic_xchg(&cdi_mgr->irq_in_use, 1))
		for (i = 0; i < cdi_mgr->num_gpios; i++) {
			pin = &cdi_mgr->gpios[i];
			if ((pin->gpio_dir == CAM_DEVBLK_GPIO_INPUT_INTR) &&
					(pin->intr_irq >= 0))
				enable_irq(pin->intr_irq);
		}

	return 0;
}

static int cdi_mgr_gpio_intr_reset(struct cdi_mgr_priv *cdi_mgr)
{
	unsigned long flags;
	int i;
	struct cam_gpio_config *pin;
	struct cam_gpio_event_queue *evts_queue;
	struct cam_gpio_timer_queue *tmrs_queue;

	if (!cdi_mgr)
		return -EINVAL;

	evts_queue = &cdi_mgr->gpio_events;

	spin_lock_irqsave(&cdi_mgr->spinlock, flags);

	// Reset GPIO pin timer queues
	for (i = 0; i < cdi_mgr->num_gpios; i++) {
		pin = &cdi_mgr->gpios[i];
		tmrs_queue = &pin->timers;
		(void)hrtimer_try_to_cancel(&tmrs_queue->timer);
		tmrs_queue->head = 0;
		tmrs_queue->count = 0;
	}

	// Reset GPIO events queue
	evts_queue->head = 0;
	evts_queue->count = 0;

	cdi_mgr->intrs_enable = false;
	cdi_mgr->stop_err_irq_wait = false;

	spin_unlock_irqrestore(&cdi_mgr->spinlock, flags);

	return 0;
}

static int cdi_mgr_gpio_intr_wait(
	struct cdi_mgr_priv *cdi_mgr, void __user *arg)
{
	int err = 0;
	int ret;
	unsigned long flags;
	struct cdi_mgr_gpio_intr event;

	if (!cdi_mgr) {
		err = -EINVAL;
		goto done;
	}

	err = cdi_mgr_gpio_intr_arm(cdi_mgr);
	if (err != 0) {
		dev_err(cdi_mgr->pdev,
			"%s: cdi_mgr_gpio_intr_arm failed %d\n",
			__func__, err);
		goto done;
	}

	ret = wait_event_interruptible(cdi_mgr->gpio_events.wait,
		(cdi_mgr->stop_err_irq_wait
			|| (cdi_mgr->gpio_events.count > 0)));
	if (ret < 0) {
		err = ret;
		dev_err(cdi_mgr->pdev,
			"%s: wait_event_interruptible failed %d\n",
			__func__, err);
		goto done;
	}

	if (cdi_mgr->stop_err_irq_wait) {
		event.idx = 0;
		event.code = CDI_MGR_GPIO_INTR_UNBLOCK;
		if (copy_to_user(arg, &event, sizeof(event))) {
			err = -EFAULT;
			dev_err(cdi_mgr->pdev,
				"%s: failed to copy to user\n", __func__);
		}
		cdi_mgr->stop_err_irq_wait = false;
		goto done;
	}

	spin_lock_irqsave(&cdi_mgr->spinlock, flags);
	ret = cdi_mgr_gpio_dq_event(cdi_mgr, &event);
	spin_unlock_irqrestore(&cdi_mgr->spinlock, flags);
	if (ret != 0) {
		err = ret;
		dev_err(cdi_mgr->pdev,
			"%s: failed to dequeue interrupt event\n", __func__);
		goto done;
	}

	if (copy_to_user(arg, &event, sizeof(event))) {
		err = -EFAULT;
		dev_err(cdi_mgr->pdev,
			"%s: failed to copy to user\n", __func__);
		goto done;
	}

done:
	return err;
}

static int cdi_mgr_gpio_intr_wait_abort(struct cdi_mgr_priv *cdi_mgr)
{
	if (!cdi_mgr)
		return -EINVAL;

	cdi_mgr->stop_err_irq_wait = true;
	wake_up_interruptible(&cdi_mgr->gpio_events.wait);

	return 0;
}

static int cdi_mgr_des_power(
	struct cdi_mgr_priv *cdi_mgr, bool enable)
{
	u8 val;

	/* if runtime_pwrctrl_off is not true, power on all here */
	if (!cdi_mgr->pdata->runtime_pwrctrl_off) {
		if (enable)
			cdi_mgr_power_up(cdi_mgr, 0xffffffff);
		else
			cdi_mgr_power_down(cdi_mgr, 0xffffffff);
	}

	cdi_mgr_mcdi_ctrl(cdi_mgr, enable);

	if (cdi_mgr->tca9539.enable) {
		if (down_timeout(&tca9539_sem,
			usecs_to_jiffies(TIMEOUT_US)) != 0)
			dev_err(cdi_mgr->dev,
				"%s: failed to wait for the semaphore\n",
				__func__);
		if (cdi_mgr->cim_ver == 1U) { /* P3714 A01 */
			if (tca9539_rd(cdi_mgr, 0x02, &val) != 0)
				return -EFAULT;
			if (enable)
				val |= (0x10 << cdi_mgr->tca9539.power_port);
			else
				val &= ~(0x10 << cdi_mgr->tca9539.power_port);
			if (tca9539_wr(cdi_mgr, 0x02, val) != 0)
				return -EFAULT;
		} else if (cdi_mgr->cim_ver == 2U) { /* P3714 A02 */
			if (tca9539_rd(cdi_mgr, 0x03, &val) != 0)
				return -EFAULT;
			if (enable)
				val |= (0x1 << cdi_mgr->tca9539.power_port);
			else
				val &= ~(0x1 << cdi_mgr->tca9539.power_port);
			if (tca9539_wr(cdi_mgr, 0x03, val) != 0)
				return -EFAULT;
		}
		up(&tca9539_sem);
	}

	return 0;
}

static long cdi_mgr_ioctl(
	struct file *file, unsigned int cmd, unsigned long arg)
{
	struct cdi_mgr_priv *cdi_mgr = file->private_data;
	int err = 0, i = 0;
	unsigned long flags;

	/* command distributor */
	switch (cmd) {
	case CDI_MGR_IOCTL_DEV_ADD:
		err = cdi_create_dev(cdi_mgr, (const void __user *)arg);
		break;
	case CDI_MGR_IOCTL_DEV_DEL:
		cdi_remove_dev(cdi_mgr, arg);
		break;
	case CDI_MGR_IOCTL_PWR_DN:
		err = cdi_mgr_power_down(cdi_mgr, arg);
		break;
	case CDI_MGR_IOCTL_PWR_UP:
		err = cdi_mgr_power_up(cdi_mgr, arg);
		break;
	case CDI_MGR_IOCTL_SET_PID:
		/* first enable irq to clear pending interrupt
		 * and then register PID
		 */
		if (!atomic_xchg(&cdi_mgr->irq_in_use, 1)) {
			for (i = 0; i < cdi_mgr->num_gpios; i++) {
				if ((cdi_mgr->gpios[i].gpio_dir ==
					CAM_DEVBLK_GPIO_INPUT_INTR) &&
					cdi_mgr->gpios[i].intr_irq >= 0)
					enable_irq(cdi_mgr->gpios[i].intr_irq);
			}
		}

		err = cdi_mgr_write_pid(file, (const void __user *)arg);
		break;
	case CDI_MGR_IOCTL_SIGNAL:
		switch (arg) {
		case CDI_MGR_SIGNAL_RESUME:
			if (!cdi_mgr->sig_no) {
				dev_err(cdi_mgr->pdev,
					"invalid sig_no, setup pid first\n");
				return -EINVAL;
			}
			spin_lock_irqsave(&cdi_mgr->spinlock, flags);
			cdi_mgr->sinfo.si_signo = cdi_mgr->sig_no;
			spin_unlock_irqrestore(&cdi_mgr->spinlock, flags);
			break;
		case CDI_MGR_SIGNAL_SUSPEND:
			spin_lock_irqsave(&cdi_mgr->spinlock, flags);
			cdi_mgr->sinfo.si_signo = 0;
			spin_unlock_irqrestore(&cdi_mgr->spinlock, flags);
			break;
		default:
			dev_err(cdi_mgr->pdev, "%s unrecognized signal: %lx\n",
				__func__, arg);
		}
		break;
	case CDI_MGR_IOCTL_PWR_INFO:
		err = cdi_mgr_get_pwr_info(cdi_mgr, (void __user *)arg);
		break;
	case CDI_MGR_IOCTL_PWM_ENABLE:
		err = cdi_mgr_pwm_enable(cdi_mgr, arg);
		break;
	case CDI_MGR_IOCTL_PWM_CONFIG:
		err = cdi_mgr_pwm_config(cdi_mgr, (const void __user *)arg);
		break;
	case CDI_MGR_IOCTL_INTR_CONFIG:
		err = cdi_mgr_gpio_config(cdi_mgr, (const void __user *)arg);
		break;
	case CDI_MGR_IOCTL_INTR_ENABLE:
		err = cdi_mgr_gpio_intr_reset(cdi_mgr);
		cdi_mgr->intrs_enable = true;
		break;
	case CDI_MGR_IOCTL_INTR_WAIT:
		err = cdi_mgr_gpio_intr_wait(cdi_mgr, (void __user *)arg);
		break;
	case CDI_MGR_IOCTL_INTR_WAIT_ABORT:
		err = cdi_mgr_gpio_intr_wait_abort(cdi_mgr);
		break;
	case CDI_MGR_IOCTL_GET_PWR_INFO:
		err = cdi_mgr_get_pwr_ctrl_info(cdi_mgr, (void __user *)arg);
		break;
	case CDI_MGR_IOCTL_ENABLE_DES_POWER:
		err = cdi_mgr_des_power(cdi_mgr, true);
		break;
	case CDI_MGR_IOCTL_DISABLE_DES_POWER:
		err = cdi_mgr_des_power(cdi_mgr, false);
		break;
	default:
		dev_err(cdi_mgr->pdev, "%s unsupported ioctl: %x\n",
			__func__, cmd);
		err = -EINVAL;
	}

	if (err)
		dev_dbg(cdi_mgr->pdev, "err = %d\n", err);

	return err;
}

static int cdi_mgr_open(struct inode *inode, struct file *file)
{
	struct cdi_mgr_priv *cdi_mgr = container_of(inode->i_cdev,
					struct cdi_mgr_priv, cdev);

	/* only one application can open one cdi_mgr device */
	if (atomic_xchg(&cdi_mgr->in_use, 1))
		return -EBUSY;

	dev_dbg(cdi_mgr->pdev, "%s\n", __func__);
	file->private_data = cdi_mgr;

	cdi_mgr->stop_err_irq_wait = false;

	return 0;
}

static int cdi_mgr_release(struct inode *inode, struct file *file)
{
	u8 val;
	int i = 0;
	struct cdi_mgr_priv *cdi_mgr = file->private_data;

	if (cdi_mgr->tca9539.enable) {
		if (down_timeout(&tca9539_sem,
			usecs_to_jiffies(TIMEOUT_US)) != 0)
			dev_err(cdi_mgr->dev,
				"%s: failed to wait for the semaphore\n",
				__func__);
		if (cdi_mgr->cim_ver == 1U) { /* P3714 A01 */
			if (tca9539_rd(cdi_mgr, 0x02, &val) != 0)
				return -EFAULT;
			val &= ~(0x10 << cdi_mgr->tca9539.power_port);
			if (tca9539_wr(cdi_mgr, 0x02, val) != 0)
				return -EFAULT;
		} else if (cdi_mgr->cim_ver == 2U) { /* P3714 A02 */
			if (tca9539_rd(cdi_mgr, 0x03, &val) != 0)
				return -EFAULT;
			val &= ~(0x1 << cdi_mgr->tca9539.power_port);
			if (tca9539_wr(cdi_mgr, 0x03, val) != 0)
				return -EFAULT;
		}
		up(&tca9539_sem);
	}

	if (cdi_mgr->pwm)
		if (pwm_is_enabled(cdi_mgr->pwm))
			pwm_disable(cdi_mgr->pwm);

	cdi_mgr_mcdi_ctrl(cdi_mgr, false);

	/* disable irq if irq is in use, when device is closed */
	(void)cdi_mgr_gpio_intr_reset(cdi_mgr);
	if (atomic_xchg(&cdi_mgr->irq_in_use, 0)) {
		for (i = 0; i < cdi_mgr->num_gpios; i++) {
			if ((cdi_mgr->gpios[i].gpio_dir == CAM_DEVBLK_GPIO_INPUT_INTR) &&
				cdi_mgr->gpios[i].intr_irq >= 0)
				disable_irq(cdi_mgr->gpios[i].intr_irq);
		}
		cdi_mgr->stop_err_irq_wait = true;
		wake_up_interruptible(&cdi_mgr->gpio_events.wait);
	}

	/* if runtime_pwrctrl_off is not true, power off all here */
	if (!cdi_mgr->pdata->runtime_pwrctrl_off)
		cdi_mgr_power_down(cdi_mgr, 0xffffffff);

	/* clear sinfo to prevent report error after handler is closed */
	memset(&cdi_mgr->sinfo, 0, sizeof(struct kernel_siginfo));
	cdi_mgr->t = NULL;
	WARN_ON(!atomic_xchg(&cdi_mgr->in_use, 0));

	return 0;
}

static const struct file_operations cdi_mgr_fileops = {
	.owner = THIS_MODULE,
	.open = cdi_mgr_open,
	.unlocked_ioctl = cdi_mgr_ioctl,
	.release = cdi_mgr_release,
};

static void cdi_mgr_del(struct cdi_mgr_priv *cdi_mgr)
{
	struct cdi_mgr_platform_data *pd = cdi_mgr->pdata;
	struct cdi_mgr_client *cdi_dev = NULL;
	int i;

	mutex_lock(&cdi_mgr->mutex);
	list_for_each_entry(cdi_dev, &cdi_mgr->dev_list, list) {
		/* remove i2c_clients that cdi-mgr created */
		if (cdi_dev->client != NULL) {
			i2c_unregister_device(cdi_dev->client);
			cdi_dev->client = NULL;
		}
	}
	mutex_unlock(&cdi_mgr->mutex);

	for (i = 0; i < pd->num_pwr_gpios; i++)
		if (pd->pwr_gpios[i])
			gpio_direction_output(
				pd->pwr_gpios[i], PW_OFF(pd->pwr_flags[i]));

	if (cdi_mgr->tca9539.enable)
		i2c_put_adapter(cdi_mgr->tca9539.adap);
	i2c_put_adapter(cdi_mgr->adap);

	for (i = 0; i < MAX_CDI_GPIOS; i++) {
		if (cdi_mgr->gpios[i].desc)
			devm_gpiod_put(cdi_mgr->dev, cdi_mgr->gpios[i].desc);
	}
}

static void cdi_mgr_dev_ins(struct work_struct *work)
{
	struct cdi_mgr_priv *cdi_mgr =
		container_of(work, struct cdi_mgr_priv, ins_work);
	struct device_node *np = cdi_mgr->pdev->of_node;
	struct device_node *subdev;
	struct cdi_mgr_new_dev d_cfg = {.drv_name = "cdi-dev"};
	const char *sname;
	u32 val;
	int err = 0;

	if (np == NULL)
		return;

	dev_dbg(cdi_mgr->dev, "%s - %s\n", __func__, np->full_name);
	sname = of_get_property(np, "cdi-dev", NULL);
	if (sname)
		strncpy(d_cfg.drv_name, sname, sizeof(d_cfg.drv_name) - 8);

	for_each_child_of_node(np, subdev) {
		err = of_property_read_u32(subdev, "addr", &val);
		if (err || !val)
			continue;

		d_cfg.addr = val;
		err = of_property_read_u32(subdev, "reg_len", &val);
		if (err || !val)
			continue;

		d_cfg.reg_bits = val;
		err = of_property_read_u32(subdev, "dat_len", &val);
		if (err || !val)
			continue;

		d_cfg.val_bits = val;

		__cdi_create_dev(cdi_mgr, &d_cfg);
	}
}

static int cdi_mgr_of_get_grp_gpio(
	struct device *dev, struct device_node *np,
	const char *name, int size, u32 *grp, u32 *flags)
{
	int num, i;

#if defined(NV_OF_GPIO_NAMED_COUNT_PRESENT) /* Linux 6.2 */
	num = of_gpio_named_count(np, name);
#else
	num = of_count_phandle_with_args(np, name, "#gpio-cells");
#endif
	dev_dbg(dev, "    num gpios of %s: %d\n", name, num);
	if (num < 0)
		return 0;

	for (i = 0; (i < num) && (i < size); i++) {
#if defined(NV_OF_GET_NAMED_GPIO_FLAGS_PRESENT) /* Linux 6.2 */
		grp[i] = of_get_named_gpio_flags(np, name, i, &flags[i]);
#else
		grp[i] = of_get_named_gpio(np, name, i);
		flags[i] = 0;
#endif
		if ((int)grp[i] < 0) {
			dev_err(dev, "%s: gpio[%d] invalid\n", __func__, i);
			return -EINVAL;
		}
		dev_dbg(dev, "        [%d] - %d %x\n", i, grp[i], flags[i]);
	}

	return num;
}

static int cdi_mgr_get_pwr_map(
	struct device *dev, struct device_node *np,
	struct cdi_mgr_platform_data *pd)
{
	int num_map_items = 0;
	u32 pwr_map_val;
	unsigned int i;

	for (i = 0; i < MAX_CDI_GPIOS; i++)
		pd->pwr_mapping[i] = i;

	if (!of_get_property(np, "pwr-items", NULL))
		return 0;

	num_map_items = of_property_count_elems_of_size(np,
				"pwr-items", sizeof(u32));
	if (num_map_items < 0) {
		dev_err(dev, "%s: error processing pwr items\n",
			__func__);
		return -1;
	}

	if (num_map_items < pd->num_pwr_gpios) {
		dev_err(dev, "%s: invalid number of pwr items\n",
			__func__);
		return -1;
	}

	for (i = 0; i < num_map_items; i++) {
		if (of_property_read_u32_index(np, "pwr-items",
			i, &pwr_map_val)) {
			dev_err(dev, "%s: failed to get pwr item\n",
				__func__);
			goto pwr_map_err;
		}

		if (pwr_map_val >= pd->num_pwr_gpios) {
			dev_err(dev, "%s: invalid power item index provided\n",
				__func__);
			goto pwr_map_err;
		}
		pd->pwr_mapping[i] = pwr_map_val;
	}

	pd->num_pwr_map = num_map_items;
	return 0;

pwr_map_err:
	for (i = 0; i < MAX_CDI_GPIOS; i++)
		pd->pwr_mapping[i] = i;

	pd->num_pwr_map = pd->num_pwr_gpios;

	return -1;
}

static struct cdi_mgr_platform_data *of_cdi_mgr_pdata(struct platform_device
	*pdev)
{
	struct device_node *np = pdev->dev.of_node, *child_np = NULL;
	struct cdi_mgr_platform_data *pd = NULL;
	int err;
	bool ext_pwr_ctrl_des = false, ext_pwr_ctrl_sensor = false;

	dev_dbg(&pdev->dev, "%s\n", __func__);
	pd = devm_kzalloc(&pdev->dev, sizeof(*pd), GFP_KERNEL);
	if (!pd) {
		dev_err(&pdev->dev, "%s: allocate memory error\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	child_np = of_get_child_by_name(np, "tegra");
	if (child_np == NULL) {
		dev_err(&pdev->dev, "%s: missing tegra node # DT %s\n",
			__func__, np->full_name);
		return ERR_PTR(-EEXIST);
	}

	pd->drv_name = (void *)of_get_property(np, "drv_name", NULL);
	if (pd->drv_name)
		dev_dbg(&pdev->dev, "    drvname: %s\n", pd->drv_name);

	err = of_property_read_u32(child_np, "i2c-bus", &pd->bus);
	if (err) {
		dev_err(&pdev->dev, "%s: missing i2c bus # DT %s\n",
			__func__, child_np->full_name);
		return ERR_PTR(-EEXIST);
	}
	dev_dbg(&pdev->dev, "    i2c-bus: %d\n", pd->bus);

	err = of_property_read_u32(child_np, "csi-port", &pd->csi_port);
	if (err) {
		dev_err(&pdev->dev, "%s: missing csi port # DT %s\n",
			__func__, child_np->full_name);
		return ERR_PTR(-EEXIST);
	}
	dev_dbg(&pdev->dev, "    csiport: %d\n", pd->csi_port);

	pd->num_pwr_gpios = cdi_mgr_of_get_grp_gpio(
		&pdev->dev, child_np, "pwdn-gpios",
		ARRAY_SIZE(pd->pwr_gpios), pd->pwr_gpios, pd->pwr_flags);
	if (pd->num_pwr_gpios < 0)
		return ERR_PTR(pd->num_pwr_gpios);

	pd->num_mcdi_gpios = cdi_mgr_of_get_grp_gpio(
		&pdev->dev, child_np, "mcdi-gpios",
		ARRAY_SIZE(pd->mcdi_gpios), pd->mcdi_gpios, pd->mcdi_flags);
	if (pd->num_mcdi_gpios < 0)
		return ERR_PTR(pd->num_mcdi_gpios);

	child_np = of_get_child_by_name(np, "pwr_ctrl");
	if (child_np == NULL) {
		dev_err(&pdev->dev, "%s: missing pwr_ctrl node # DT %s\n",
			__func__, np->full_name);
		return ERR_PTR(-EEXIST);
	}

	pd->default_pwr_on = of_property_read_bool(child_np,
		"default-power-on");
	pd->runtime_pwrctrl_off =
		of_property_read_bool(child_np, "runtime-pwrctrl-off");

	pd->ext_pwr_ctrl = 0;
	ext_pwr_ctrl_des =
		of_property_read_bool(child_np, "ext-pwr-ctrl-deserializer");
	if (ext_pwr_ctrl_des == true)
		pd->ext_pwr_ctrl |= 1 << 0;
	ext_pwr_ctrl_sensor = of_property_read_bool(child_np,
		"ext-pwr-ctrl-sensor");
	if (ext_pwr_ctrl_sensor == true)
		pd->ext_pwr_ctrl |= 1 << 1;

	err = cdi_mgr_get_pwr_map(&pdev->dev, child_np, pd);
	if (err)
		dev_err(&pdev->dev,
			"%s: failed to map pwr items. Using default values\n",
			__func__);

	return pd;
}

#if defined(NV_CLASS_STRUCT_DEVNODE_HAS_CONST_DEV_ARG) /* Linux v6.2 */
static char *cdi_mgr_devnode(const struct device *dev, umode_t *mode)
#else
static char *cdi_mgr_devnode(struct device *dev, umode_t *mode)
#endif
{
	if (!mode)
		return NULL;

	/* set alway user to access this device */
	*mode = 0666;

	return NULL;
}

static int cdi_mgr_suspend(struct device *dev)
{
	unsigned int reg_addr;
	int rc;
	struct cdi_mgr_priv *cdi_mgr;

	cdi_mgr = (struct cdi_mgr_priv *)dev_get_drvdata(dev);

	if (cdi_mgr->tca9539.enable) {
		reg_addr = CDI_MGR_TCA9539_BASE_REG_ADDR;
		while (reg_addr < CDI_MGR_TCA9539_REGISTER_COUNT) {
			rc = tca9539_rd(cdi_mgr, reg_addr,
				&cdi_mgr->pre_suspend_tca9539_regvals[reg_addr]);
			if (rc != 0) {
				dev_err(dev, "%s: tca9539_raw_rd failed reading reg[0x%x]\n",
					__func__, reg_addr);
				return -EIO;
			}

			++reg_addr;
		}
	}

	return 0;
}

static int cdi_mgr_resume(struct device *dev)
{
	struct pwm_device *pwm;
	struct cdi_mgr_priv *cdi_mgr;
	unsigned int reg_addr;
	int rc;

	/* Reconfigure PWM as done during boot time */
	if (of_property_read_bool(dev->of_node, "pwms")) {
		pwm = devm_pwm_get(dev, NULL);
		if (!IS_ERR(pwm))
			dev_info(dev, "%s Resume successful\n", __func__);
	}

	cdi_mgr = (struct cdi_mgr_priv *)dev_get_drvdata(dev);

	if (cdi_mgr->tca9539.enable) {
		reg_addr = CDI_MGR_TCA9539_BASE_REG_ADDR;
		while (reg_addr < CDI_MGR_TCA9539_REGISTER_COUNT) {
			rc = tca9539_wr(cdi_mgr, reg_addr,
				cdi_mgr->pre_suspend_tca9539_regvals[reg_addr]);
			if (rc != 0) {
				dev_err(dev, "%s: tca9539_raw_wr failed setting reg[0x%x] = 0x%x\n",
					__func__, reg_addr,
					cdi_mgr->pre_suspend_tca9539_regvals[reg_addr]);
				return -EIO;
			}
			++reg_addr;
		}
	}

	return 0;
}

static const struct dev_pm_ops cdi_mgr_pm_ops = {
	.suspend = cdi_mgr_suspend,
	.resume = cdi_mgr_resume,
	.runtime_suspend = cdi_mgr_suspend,
	.runtime_resume = cdi_mgr_resume,
};

static int cdi_mgr_setup_gpio_interrupt(struct device *dev, struct cdi_mgr_priv *cdi_mgr,
					uint32_t idx, uint32_t gpio_idx, uint32_t intr_edge)
{
	int ret = 0;
	int gpio_irq = 0;

	ret = gpiod_direction_input(cdi_mgr->gpios[idx].desc);
	if (ret) {
		dev_err(dev, "%s Failed to gpio direction : input 0\n",
			__func__);
		return ret;
	}

	gpio_irq = gpiod_to_irq(cdi_mgr->gpios[idx].desc);
	if (gpio_irq < 0) {
		dev_err(dev, "gpiod_to_irq() failed: %d\n", gpio_irq);
		return gpio_irq;
	}

	cdi_mgr->gpios[idx].intr_irq = gpio_irq;
	ret = devm_request_irq(dev,
			cdi_mgr->gpios[idx].intr_irq,
			cdi_mgr_isr, intr_edge, dev_name(dev), cdi_mgr);
	if (ret) {
		dev_err(dev, "devm_request_irq failed with err %d\n", ret);
		return ret;
	}
	disable_irq(cdi_mgr->gpios[idx].intr_irq);
	atomic_set(&cdi_mgr->irq_in_use, 0);

	cdi_mgr->gpios[idx].gpio_dir = CAM_DEVBLK_GPIO_INPUT_INTR;
	cdi_mgr->gpios[idx].idx = gpio_idx;

	return 0;
}

static int cdi_mgr_configure_gpios(struct device *dev, struct cdi_mgr_priv *cdi_mgr)
{
	struct device_node *node = NULL;
	int num_gpios = 0;
	uint32_t i = 0, j = 0;
	int ret = 0;

	node = of_get_child_by_name(dev->of_node, "tegra");

	if (node != NULL) {
		node = of_get_child_by_name(node, "gpios");

		if (node != NULL) {
			struct device_node *child = NULL;

			num_gpios = of_get_child_count(node);
			if (!num_gpios || num_gpios > MAX_CDI_GPIOS) {
				dev_err(dev, "%s Invalid number of gpios : %d\n",
					__func__, num_gpios);
				return -EINVAL;
			}
			dev_dbg(dev, "%s gpio node count : %d\n", __func__, num_gpios);

			for_each_child_of_node(node, child) {
				uint32_t gpio_index = 0;

				if (of_property_read_u32(child, "index", &gpio_index)) {
					dev_err(dev, "%s \"index\" dt property not found\n",
						__func__);
					return -ENOENT;
				}

				if (gpio_index >= MAX_CDI_GPIOS) {
					dev_err(dev, "%s Invalid gpios index: %d,"
						" valid value is below %d\n",
						__func__, gpio_index, MAX_CDI_GPIOS);
					return -EINVAL;
				}

				for (j = 0; j < cdi_mgr->num_gpios; j++) {
					if (cdi_mgr->gpios[j].idx == gpio_index) {
						dev_err(dev, "%s GPIO already in use\n", __func__);
						return -EPERM;
					}
				}

				cdi_mgr->gpios[i].desc = devm_fwnode_gpiod_get_index(dev,
					&child->fwnode, "devblk", 0, GPIOD_ASIS, NULL);
				if (IS_ERR(cdi_mgr->gpios[i].desc)) {
					ret = PTR_ERR(cdi_mgr->gpios[i].desc);
					if (ret < 0)
						dev_err(dev, "%s Failed to allocate gpio desc\n",
							 __func__);
					return ret;
				}

				if (of_property_read_bool(child, "intr-edge-falling")) {
					ret = cdi_mgr_setup_gpio_interrupt(dev, cdi_mgr, i,
							gpio_index, IRQF_TRIGGER_FALLING);
					if (ret < 0) {
						dev_err(dev, "%s():%d Failed to setup input"
							"interrupt gpio\n",
							__func__, __LINE__);
						return ret;
					}
				} else if (of_property_read_bool(child, "intr-edge-rising")) {
					ret = cdi_mgr_setup_gpio_interrupt(dev, cdi_mgr, i,
							gpio_index, IRQF_TRIGGER_RISING);
					if (ret < 0) {
						dev_err(dev, "%s():%d Failed to setup input"
							" interrupt gpio\n",
							__func__, __LINE__);
						return ret;
					}
				} else {
					dev_err(dev, "%s(): Invalid DT property\n", __func__);
					return -EINVAL;
				}
				i++;
				cdi_mgr->num_gpios++;
			}
		}
	} else {
		dev_err(dev, "%s \"tegra\" dt node not found\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static void cdi_mgr_get_cim_ver(struct device *dev, struct cdi_mgr_priv *cdi_mgr)
{
	int err = 0;
	struct device_node *child = NULL;
	struct device_node *cim = NULL;
	const char *cim_ver;

	child = of_get_parent(dev->of_node);
	if (child != NULL) {
		cim = of_get_compatible_child(child,
					"nvidia,cim_ver");
		if (cim != NULL) {
			err = of_property_read_string(cim,
					"cim_ver",
					&cim_ver);
			if (!err) {
				if (!strncmp(cim_ver,
					"cim_ver_a01",
					sizeof("cim_ver_a01"))) {
					dev_info(dev,
						"CIM A01\n");
					cdi_mgr->cim_ver = 1U;
				} else {
					dev_info(dev,
						"CIM A02\n");
					cdi_mgr->cim_ver = 2U;
					if (of_property_read_u32_array(cim,
						"cim_frsync_src",
						cdi_mgr->cim_frsync,
						sizeof(cdi_mgr->cim_frsync)/sizeof(u32))) {
						memset((void *)cdi_mgr->cim_frsync,
							0U,
							sizeof(cdi_mgr->cim_frsync));
					}
				}
			}
		}
	}
}

static int cdi_mgr_probe(struct platform_device *pdev)
{
	int err = 0;
	struct cdi_mgr_priv *cdi_mgr;
	struct cdi_mgr_platform_data *pd;
	unsigned int i;
	struct cam_gpio_config *pin = NULL;
	struct device_node *child = NULL;
	struct device_node *child_tca9539 = NULL;
	struct device_node *root_node = NULL;
	const char *model;

	dev_info(&pdev->dev, "%sing...\n", __func__);

	cdi_mgr = devm_kzalloc(&pdev->dev,
			sizeof(struct cdi_mgr_priv),
			GFP_KERNEL);
	if (!cdi_mgr) {
		dev_err(&pdev->dev, "Unable to allocate memory!\n");
		return -ENOMEM;
	}

	spin_lock_init(&cdi_mgr->spinlock);
	atomic_set(&cdi_mgr->in_use, 0);
	INIT_LIST_HEAD(&cdi_mgr->dev_list);
	mutex_init(&cdi_mgr->mutex);
	init_waitqueue_head(&cdi_mgr->gpio_events.wait);
	cdi_mgr->pwm = NULL;

	for (i = 0; i < ARRAY_SIZE(cdi_mgr->gpios); i++) {
		pin = &cdi_mgr->gpios[i];
		pin->mgr = cdi_mgr;
		hrtimer_init(&pin->timers.timer, CLOCK_MONOTONIC,
			HRTIMER_MODE_ABS);
		pin->timers.timer.function = &cdi_mgr_intr_timer;
	}

	if (pdev->dev.of_node) {
		pd = of_cdi_mgr_pdata(pdev);
		if (IS_ERR(pd))
			return PTR_ERR(pd);
		cdi_mgr->pdata = pd;
	} else if (pdev->dev.platform_data) {
		cdi_mgr->pdata = pdev->dev.platform_data;
		pd = cdi_mgr->pdata;
	} else {
		dev_err(&pdev->dev, "%s No platform data.\n", __func__);
		return -EFAULT;
	}

	cdi_mgr->isP3898 = false;
	root_node = of_root;
	if (root_node) {
		model = of_get_property(root_node, "model", NULL);
		dev_info(&pdev->dev, "platform name: %s\n", model);
		if (!strcmp(model, "p3898-0010"))
			cdi_mgr->isP3898 = true;
	}

	if (of_property_read_bool(pdev->dev.of_node, "pwms")) {
		cdi_mgr->pwm = devm_pwm_get(&pdev->dev, NULL);
		if (!IS_ERR(cdi_mgr->pwm)) {
			dev_info(&pdev->dev,
				"%s: success to get PWM\n", __func__);
			pwm_disable(cdi_mgr->pwm);
		} else {
			err = PTR_ERR(cdi_mgr->pwm);
			if (err != -EPROBE_DEFER)
				dev_err(&pdev->dev,
					"%s: fail to get PWM\n", __func__);
			return err;
		}
	}

	cdi_mgr->adap = i2c_get_adapter(pd->bus);
	if (!cdi_mgr->adap) {
		dev_err(&pdev->dev, "%s no such i2c bus %d\n",
			__func__, pd->bus);
		return -ENODEV;
	}

	if (pd->num_pwr_gpios > 0) {
		for (i = 0; i < pd->num_pwr_gpios; i++) {
			if (!gpio_is_valid(pd->pwr_gpios[i]))
				goto err_probe;

			if (gpio_request(pd->pwr_gpios[i], "pwdn-gpios")) {
				dev_err(&pdev->dev, "failed to req GPIO: %d\n",
					pd->pwr_gpios[i]);
				goto err_probe;
			}

			err = gpio_direction_output(pd->pwr_gpios[i],
				pd->default_pwr_on ?
				PW_ON(pd->pwr_flags[i]) :
				PW_OFF(pd->pwr_flags[i]));
			if (err < 0) {
				dev_err(&pdev->dev, "failed to setup GPIO: %d\n",
					pd->pwr_gpios[i]);
				i++;
				goto err_probe;
			}
			if (pd->default_pwr_on)
				cdi_mgr->pwr_state |= BIT(i);
		}
	}

	if (cdi_mgr_configure_gpios(&pdev->dev, cdi_mgr) < 0) {
		dev_err(&pdev->dev, "%s(): GPIO setup failed\n", __func__);
		goto err_probe;
	}
	cdi_mgr->pdev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, cdi_mgr);

	if (pd->drv_name)
		err = snprintf(cdi_mgr->devname, sizeof(cdi_mgr->devname),
			       "%s.%x.%c", pd->drv_name, pd->bus,
			       'a' + pd->csi_port);
	else
		err = snprintf(cdi_mgr->devname, sizeof(cdi_mgr->devname),
			       "cdi-mgr.%x.%c", pd->bus, 'a' + pd->csi_port);

	if (err < 0) {
		dev_err(&pdev->dev, "encoding error: %d\n", err);
		goto err_probe;
	}

	/* Request dynamic allocation of a device major number */
	err = alloc_chrdev_region(&cdi_mgr->devt,
				0, CDI_DEV_MAX, cdi_mgr->devname);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to allocate char dev region %d\n",
			err);
		goto err_probe;
	}

	/* poluate sysfs entries */
#if defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG) /* Linux v6.4 */
	cdi_mgr->cdi_class = class_create(cdi_mgr->devname);
#else
	cdi_mgr->cdi_class = class_create(THIS_MODULE, cdi_mgr->devname);
#endif
	if (IS_ERR(cdi_mgr->cdi_class)) {
		err = PTR_ERR(cdi_mgr->cdi_class);
		cdi_mgr->cdi_class = NULL;
		dev_err(&pdev->dev, "failed to create class %d\n",
			err);
		goto err_probe;
	}

	cdi_mgr->cdi_class->devnode = cdi_mgr_devnode;

	/* connect the file operations with the cdev */
	cdev_init(&cdi_mgr->cdev, &cdi_mgr_fileops);
	cdi_mgr->cdev.owner = THIS_MODULE;

	/* connect the major/minor number to this dev */
	err = cdev_add(&cdi_mgr->cdev, MKDEV(MAJOR(cdi_mgr->devt), 0), 1);
	if (err) {
		dev_err(&pdev->dev, "Unable to add cdev %d\n", err);
		goto err_probe;
	}
	/* send uevents to udev, it will create /dev node for cdi-mgr */
	cdi_mgr->dev = device_create(cdi_mgr->cdi_class, &pdev->dev,
				     cdi_mgr->cdev.dev,
				     cdi_mgr,
				     cdi_mgr->devname);
	if (IS_ERR(cdi_mgr->dev)) {
		err = PTR_ERR(cdi_mgr->dev);
		cdi_mgr->dev = NULL;
		dev_err(&pdev->dev, "failed to create device %d\n", err);
		goto err_probe;
	}

	/* Find CIM version */
	cdi_mgr_get_cim_ver(&pdev->dev, cdi_mgr);

	child = of_get_child_by_name(pdev->dev.of_node, "pwr_ctrl");
	if (child != NULL) {
		if (of_property_read_bool(child,
					"deserializer-pwr-gpio"))
			cdi_mgr->des_pwr_method = DES_PWR_GPIO;
		else if (of_property_read_bool(child,
					"deserializer-pwr-nvccp"))
			cdi_mgr->des_pwr_method = DES_PWR_NVCCP;
		else
			cdi_mgr->des_pwr_method = DES_PWR_NO_PWR;
		cdi_mgr->des_pwr_i2c_addr = 0;

		/* get the I/O expander information */
		child_tca9539 = of_get_child_by_name(child, "tca9539");
		if (child_tca9539 != NULL) {
			err = of_property_read_u32(child_tca9539, "i2c-bus",
					&cdi_mgr->tca9539.bus);
			if (err)
				cdi_mgr->tca9539.bus = pd->bus;
			err = of_property_read_u32(child_tca9539, "addr",
				&cdi_mgr->tca9539.addr);
			if (err || !cdi_mgr->tca9539.addr) {
				dev_err(&pdev->dev, "%s: ERROR %d addr = %d\n",
					__func__, err,
					cdi_mgr->tca9539.addr);
				goto err_probe;
			}
			err = of_property_read_u32(child_tca9539, "reg_len",
				&cdi_mgr->tca9539.reg_len);
			if (err || !cdi_mgr->tca9539.reg_len) {
				dev_err(&pdev->dev, "%s: ERROR %d reg_len = %d\n",
					__func__, err,
					cdi_mgr->tca9539.reg_len);
				goto err_probe;
			}
			err = of_property_read_u32(child_tca9539, "dat_len",
				&cdi_mgr->tca9539.dat_len);
			if (err || !cdi_mgr->tca9539.dat_len) {
				dev_err(&pdev->dev, "%s: ERROR %d dat_len = %d\n",
					__func__, err,
					cdi_mgr->tca9539.dat_len);
				goto err_probe;
			}
			err = of_property_read_u32(child_tca9539->parent,
					"power_port",
					&cdi_mgr->tca9539.power_port);
			if (err) {
				dev_err(&pdev->dev, "%s: ERROR %d power_port = %d\n",
					__func__, err,
					cdi_mgr->tca9539.power_port);
				goto err_probe;
			}

			cdi_mgr->tca9539.reg_len /= 8;
			cdi_mgr->tca9539.dat_len /= 8;
			cdi_mgr->tca9539.enable = 1;
			cdi_mgr->tca9539.adap = i2c_get_adapter(cdi_mgr->tca9539.bus);
			if (!cdi_mgr->tca9539.adap) {
				dev_err(&pdev->dev, "%s no such i2c bus %d\n",
					__func__, cdi_mgr->tca9539.bus);
				goto err_probe;
			}

			sema_init(&tca9539_sem, 1);

			/* Set the init values */
			/* TODO : read the array to initialize */
			/* the registers in TCA9539 */
			/* Use the IO expander to control PWDN signals */
			if (cdi_mgr->cim_ver == 1U) { /* P3714 A01 */
				if (tca9539_wr(cdi_mgr, 0x6, 0x0E) != 0) {
					dev_err(&pdev->dev,
							"%s: ERR %d: TCA9539: Failed to select PWDN signal source\n",
							__func__, err);
					goto err_probe;
				}
				/* Output low for AGGA/B/C/D_PWRDN */
				if (tca9539_wr(cdi_mgr, 0x2, 0x0E) != 0) {
					dev_err(&pdev->dev,
							"%s: ERR %d: TCA9539: Failed to set the output level\n",
							__func__, err);
					goto err_probe;
				}
			} else if (cdi_mgr->cim_ver == 2U) { /* P3714 A02 */
				if (tca9539_wr(cdi_mgr, 0x6, 0xC0) != 0) {
					dev_err(&pdev->dev,
							"%s: ERR %d: TCA9539: Failed to select FS selection signal source\n",
							__func__, err);
					goto err_probe;
				}
				if (tca9539_wr(cdi_mgr, 0x7, 0x70) != 0) {
					dev_err(&pdev->dev,
							"%s: ERR %d: TCA9539: Failed to select PWDN signal source\n",
							__func__, err);
					goto err_probe;
				}

				/* Configure FRSYNC logic */
				dev_info(&pdev->dev,
						"FRSYNC source: %d %d %d\n",
						cdi_mgr->cim_frsync[0],
						cdi_mgr->cim_frsync[1],
						cdi_mgr->cim_frsync[2]);
				if (tca9539_wr(cdi_mgr, 0x2,
					(cdi_mgr->cim_frsync[2] << 4) |
					(cdi_mgr->cim_frsync[1] << 2) |
					(cdi_mgr->cim_frsync[0])) < 0) {
					dev_err(&pdev->dev,
							"%s: ERR %d: TCA9539: Failed to set FRSYNC control logic\n",
							__func__, err);
					goto err_probe;
				}
				/* Output low for AGGA/B/C/D_PWRDN */
				if (tca9539_wr(cdi_mgr, 0x3, 0x00) != 0) {
					dev_err(&pdev->dev,
							"%s: ERR %d: TCA9539: Failed to set the output level\n",
							__func__, err);
					goto err_probe;
				}
			}
		}
	}

	cdi_mgr_debugfs_init(cdi_mgr);
	INIT_WORK(&cdi_mgr->ins_work, cdi_mgr_dev_ins);
	schedule_work(&cdi_mgr->ins_work);
	return 0;

err_probe:
	cdi_mgr_del(cdi_mgr);
	return err;
}

static int cdi_mgr_remove(struct platform_device *pdev)
{
	struct cdi_mgr_priv *cdi_mgr = dev_get_drvdata(&pdev->dev);

	if (cdi_mgr) {
		cdi_mgr_debugfs_remove(cdi_mgr);
		cdi_mgr_del(cdi_mgr);

		if (cdi_mgr->dev)
			device_destroy(cdi_mgr->cdi_class,
				       cdi_mgr->cdev.dev);
		if (cdi_mgr->cdev.dev)
			cdev_del(&cdi_mgr->cdev);

		if (cdi_mgr->cdi_class)
			class_destroy(cdi_mgr->cdi_class);

		if (cdi_mgr->devt)
			unregister_chrdev_region(cdi_mgr->devt, CDI_DEV_MAX);
	}

	return 0;
}

static const struct of_device_id cdi_mgr_of_match[] = {
	{ .compatible = "nvidia,cdi-mgr", },
	{ }
};
MODULE_DEVICE_TABLE(of, cdi_mgr_of_match);

static struct platform_driver cdi_mgr_driver = {
	.driver = {
		.name = "cdi-mgr",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(cdi_mgr_of_match),
		.pm = &cdi_mgr_pm_ops,
	},
	.probe = cdi_mgr_probe,
	.remove = cdi_mgr_remove,
};

module_platform_driver(cdi_mgr_driver);

MODULE_DESCRIPTION("tegra auto cdi manager driver");
MODULE_AUTHOR("Songhee Baek <sbeak@nvidia.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:cdi_mgr");
MODULE_SOFTDEP("pre: cdi_pwm");
