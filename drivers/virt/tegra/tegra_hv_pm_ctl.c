/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <nvidia/conftest.h>

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/atomic.h>

#ifdef CONFIG_PM_SLEEP
#include <linux/suspend.h>
#include <net/sock.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#endif
#include <soc/tegra/virt/hv-ivc.h>
#include <soc/tegra/fuse.h>
#include <soc/tegra/virt/tegra_hv_pm_ctl.h>
#include <soc/tegra/virt/syscalls.h>
#include <soc/tegra/virt/tegra_hv_sysmgr.h>


#define DRV_NAME	"tegra_hv_pm_ctl"
#define CHAR_DEV_COUNT	1
#define MAX_GUESTS_NUM	8

#ifdef CONFIG_PM_SLEEP
#define NETLINK_USERSPACE_PM	30
#define MAX_USER_CLIENT		64
#define USERSPACE_RESPONSE_TIMEOUT msecs_to_jiffies(5000)

static struct sock *nl_sk;
static struct completion netlink_complete;
static spinlock_t netlink_lock;

static struct {
	uint32_t client_pid;
	bool suspend_response;
} user_client[MAX_USER_CLIENT];
static atomic_t user_client_count;
#endif

struct tegra_hv_pm_ctl {
	struct device *dev;

	u32 ivc;
	struct tegra_hv_ivc_cookie *ivck;

	struct class *class;
	struct cdev cdev;
	dev_t char_devt;
	bool char_is_open;
	u32 wait_for_guests[MAX_GUESTS_NUM];
	u32 wait_for_guests_size;

	struct mutex mutex_lock;
	wait_queue_head_t wq;
};

int (*tegra_hv_pm_ctl_prepare_shutdown)(void);

/* Global driver data */
static struct tegra_hv_pm_ctl *tegra_hv_pm_ctl_data;

/* Guest ID for state */
static u32 guest_id;

static int tegra_hv_pm_ctl_get_guest_state(u32 vmid, u32 *state);

static int tegra_hv_pm_ctl_trigger_guest_suspend(u32 vmid)
{
	int ret;

	if (!tegra_hv_pm_ctl_data) {
		pr_err("%s: tegra_hv_pm_ctl driver is not probed, %d\n",
			__func__, -ENXIO);
		return -ENXIO;
	}

	ret = hyp_guest_reset(GUEST_SUSPEND_REQ_CMD(vmid), NULL);
	if (ret < 0) {
		pr_err("%s: Failed to trigger guest%u suspend, %d\n",
			__func__, vmid, ret);
		return ret;
	}

	return 0;
}

/*
 * For dependency management on System suspend, if there are guests required
 * to wait and the guests are active, the privileged guest sends
 * a guest suspend command to the guests and waits for the guests to be
 * suspended or shutsdown. Shutdown is acceptable as the key purpose
 * of this function is to ensure this VM stays up while VMs dependent on
 * this VM are up
 */
static int do_wait_for_guests_inactive(void)
{
	bool sent_guest_suspend = false;
	uint32_t i = 0;
	int ret = 0;

	while (i < tegra_hv_pm_ctl_data->wait_for_guests_size) {
		u32 vmid = tegra_hv_pm_ctl_data->wait_for_guests[i];
		u32 state;

		ret = tegra_hv_pm_ctl_get_guest_state(vmid, &state);
		if (ret < 0)
			return ret;

		if (state == VM_STATE_SUSPEND || state == VM_STATE_SHUTDOWN) {
			sent_guest_suspend = false;
			i++;
			continue;
		}

		if (sent_guest_suspend == false) {
			pr_debug("%s: Send a guest suspend command to guest%u\n",
				__func__, vmid);
			ret = tegra_hv_pm_ctl_trigger_guest_suspend(vmid);
			if (ret < 0)
				return ret;

			sent_guest_suspend = true;
		}
		msleep(10);
	}

	return 0;
}

int tegra_hv_pm_ctl_trigger_sys_suspend(void)
{
	int ret;

	if (!tegra_hv_pm_ctl_data) {
		pr_err("%s: tegra_hv_pm_ctl driver is not probed, %d\n",
			__func__, -ENXIO);
		return -ENXIO;
	}

	ret = do_wait_for_guests_inactive();
	if (ret < 0) {
		pr_err("%s: Failed to wait for guests suspended, %d\n",
			__func__, ret);
		return ret;
	}

	ret = hyp_guest_reset(SYS_SUSPEND_INIT_CMD, NULL);
	if (ret < 0) {
		pr_err("%s: Failed to trigger system suspend, %d\n",
			__func__, ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(tegra_hv_pm_ctl_trigger_sys_suspend);

int tegra_hv_pm_ctl_trigger_sys_shutdown(void)
{
	int ret;

	if (!tegra_hv_pm_ctl_data) {
		pr_err("%s: tegra_hv_pm_ctl driver is not probed, %d\n",
			__func__, -ENXIO);
		return -ENXIO;
	}

	if (tegra_hv_pm_ctl_prepare_shutdown) {
		ret = tegra_hv_pm_ctl_prepare_shutdown();
		if (ret < 0) {
			pr_err("%s: Failed to prepare shutdown, %d\n",
				__func__, ret);
			return ret;
		}
	}

	ret = hyp_guest_reset(SYS_SHUTDOWN_INIT_CMD, NULL);
	if (ret < 0) {
		pr_err("%s: Failed to trigger system shutdown, %d\n",
			__func__, ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(tegra_hv_pm_ctl_trigger_sys_shutdown);

int tegra_hv_pm_ctl_trigger_sys_reboot(void)
{
	int ret;

	if (!tegra_hv_pm_ctl_data) {
		pr_err("%s: tegra_hv_pm_ctl driver is not probed, %d\n",
			__func__, -ENXIO);
		return -ENXIO;
	}

	ret = hyp_guest_reset(SYS_REBOOT_INIT_CMD, NULL);
	if (ret < 0) {
		pr_err("%s: Failed to trigger system reboot, %d\n",
			__func__, ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(tegra_hv_pm_ctl_trigger_sys_reboot);

static int tegra_hv_pm_ctl_trigger_guest_resume(u32 vmid)
{
	int ret;

	if (!tegra_hv_pm_ctl_data) {
		pr_err("%s: tegra_hv_pm_ctl driver is not probed, %d\n",
			__func__, -ENXIO);
		return -ENXIO;
	}

	ret = hyp_guest_reset(GUEST_RESUME_INIT_CMD(vmid), NULL);
	if (ret < 0) {
		pr_err("%s: Failed to trigger guest%u resume, %d\n",
			__func__, vmid, ret);
		return ret;
	}

	return 0;
}

static int tegra_hv_pm_ctl_get_guest_state(u32 vmid, u32 *state)
{
	int ret;

	if (!tegra_hv_pm_ctl_data) {
		pr_err("%s: tegra_hv_pm_ctl driver is not probed, %d\n",
			__func__, -ENXIO);
		return -ENXIO;
	}

	/* guest state which can be returned:
	 * VM_STATE_BOOT, VM_STATE_HALT, VM_STATE_SUSPEND, VM_STATE_SHUTDOWN */
	ret = hyp_read_guest_state(vmid, state);
	if (ret < 0) {
		pr_err("%s: Failed to get guest%u state, %d\n",
			__func__, vmid, ret);
		return ret;
	}

	return 0;
}

static irqreturn_t tegra_hv_pm_ctl_irq(int irq, void *dev_id)
{
	struct tegra_hv_pm_ctl *data = dev_id;

	/* handle IVC state changes */
	if (tegra_hv_ivc_channel_notified(data->ivck) != 0)
		goto out;

	if (tegra_hv_ivc_can_read(data->ivck) || tegra_hv_ivc_can_write(data->ivck))
		wake_up_interruptible_all(&data->wq);

out:
	return IRQ_HANDLED;
}

static ssize_t tegra_hv_pm_ctl_read(struct file *filp, char __user *buf,
				    size_t count, loff_t *ppos)
{
	struct tegra_hv_pm_ctl *data = filp->private_data;
	size_t chunk;
	int ret = 0;

	if (!tegra_hv_ivc_can_read(data->ivck)) {
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		ret = wait_event_interruptible(data->wq,
					       tegra_hv_ivc_can_read(data->ivck));
		if (ret < 0)
			return ret;
	}

	chunk = min_t(size_t, count, data->ivck->frame_size);
	mutex_lock(&data->mutex_lock);
	ret = tegra_hv_ivc_read_user(data->ivck, buf, chunk);
	mutex_unlock(&data->mutex_lock);
	if (ret < 0)
		dev_err(data->dev, "%s: Failed to read data from IVC %d, %d\n",
			__func__, data->ivc, ret);

	return ret;
}

static ssize_t tegra_hv_pm_ctl_write(struct file *filp, const char __user *buf,
				     size_t count, loff_t *ppos)
{
	struct tegra_hv_pm_ctl *data = filp->private_data;
	size_t chunk;
	int ret = 0;

	if (!tegra_hv_ivc_can_write(data->ivck)) {
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		ret = wait_event_interruptible(data->wq,
					       tegra_hv_ivc_can_write(data->ivck));
		if (ret < 0)
			return ret;
	}

	chunk = min_t(size_t, count, data->ivck->frame_size);
	mutex_lock(&data->mutex_lock);
	ret = tegra_hv_ivc_write_user(data->ivck, buf, chunk);
	mutex_unlock(&data->mutex_lock);
	if (ret < 0)
		dev_err(data->dev, "%s: Failed to write data from IVC %d, %d\n",
			__func__, data->ivc, ret);

	return ret;
}

static __poll_t tegra_hv_pm_ctl_poll(struct file *filp,
					 struct poll_table_struct *table)
{
	struct tegra_hv_pm_ctl *data = filp->private_data;
	__poll_t req_events = poll_requested_events(table);
	__poll_t read_mask = ((__force __poll_t)POLLIN) | ((__force __poll_t)POLLRDNORM);
	__poll_t write_mask = ((__force __poll_t)POLLOUT) | ((__force __poll_t)POLLWRNORM);
	__poll_t mask = 0;

	mutex_lock(&data->mutex_lock);
	if (!tegra_hv_ivc_can_read(data->ivck) && (req_events & read_mask)) {
		mutex_unlock(&data->mutex_lock);
		poll_wait(filp, &data->wq, table);
		mutex_lock(&data->mutex_lock);
	}

	if (tegra_hv_ivc_can_read(data->ivck))
		mask |= read_mask;
	if (tegra_hv_ivc_can_write(data->ivck))
		mask |= write_mask;
	mutex_unlock(&data->mutex_lock);

	return mask;
}

static int tegra_hv_pm_ctl_open(struct inode *inode, struct file *filp)
{
	struct cdev *cdev = inode->i_cdev;
	struct tegra_hv_pm_ctl *data =
			container_of(cdev, struct tegra_hv_pm_ctl, cdev);
	int ret = 0;

	mutex_lock(&data->mutex_lock);
	if (data->char_is_open) {
		ret = -EBUSY;
		goto out;
	}

	data->char_is_open = true;
	filp->private_data = data;
out:
	mutex_unlock(&data->mutex_lock);

	return ret;
}

static int tegra_hv_pm_ctl_release(struct inode *inode, struct file *filp)
{
	struct tegra_hv_pm_ctl *data = filp->private_data;

	mutex_lock(&data->mutex_lock);
	data->char_is_open = false;
	filp->private_data = NULL;
	mutex_unlock(&data->mutex_lock);

	return 0;
}

static const struct file_operations tegra_hv_pm_ctl_fops = {
	.owner		= THIS_MODULE,
#if defined(NV_NO_LLSEEK_PRESENT)
	.llseek		= no_llseek,
#endif
	.read		= tegra_hv_pm_ctl_read,
	.write		= tegra_hv_pm_ctl_write,
	.poll		= tegra_hv_pm_ctl_poll,
	.open		= tegra_hv_pm_ctl_open,
	.release	= tegra_hv_pm_ctl_release,
};

static ssize_t ivc_id_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct tegra_hv_pm_ctl *data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n", data->ivc);
}

static ssize_t ivc_frame_size_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct tegra_hv_pm_ctl *data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", data->ivck->frame_size);
}

static ssize_t ivc_nframes_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct tegra_hv_pm_ctl *data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", data->ivck->nframes);
}

static ssize_t ivc_peer_vmid_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct tegra_hv_pm_ctl *data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", data->ivck->peer_vmid);
}

static ssize_t trigger_sys_suspend_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct tegra_hv_pm_ctl *data = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret) {
		dev_err(data->dev, "%s: Failed to convert string to uint\n",
			__func__);
		return ret;
	}

	if (val != 1) {
		dev_err(data->dev, "%s: Unsupported value, %u\n",
			__func__, val);
		return -EINVAL;
	}

	ret = tegra_hv_pm_ctl_trigger_sys_suspend();
	if (ret < 0)
		return ret;

	return count;
}


static ssize_t trigger_sys_shutdown_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct tegra_hv_pm_ctl *data = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret) {
		dev_err(data->dev, "%s: Failed to convert string to uint\n",
			__func__);
		return ret;
	}

	if (val != 1) {
		dev_err(data->dev, "%s: Unsupported value, %u\n",
			__func__, val);
		return -EINVAL;
	}

	ret = tegra_hv_pm_ctl_trigger_sys_shutdown();
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t trigger_sys_reboot_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct tegra_hv_pm_ctl *data = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret) {
		dev_err(data->dev, "%s: Failed to convert string to uint\n",
			__func__);
		return ret;
	}

	if (val != 1) {
		dev_err(data->dev, "%s: Unsupported value, %u\n",
			__func__, val);
		return -EINVAL;
	}

	ret = tegra_hv_pm_ctl_trigger_sys_reboot();
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t trigger_guest_suspend_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct tegra_hv_pm_ctl *data = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret) {
		dev_err(data->dev, "%s: Failed to convert string to uint\n",
			__func__);
		return ret;
	}

	ret = tegra_hv_pm_ctl_trigger_guest_suspend(val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t trigger_guest_resume_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct tegra_hv_pm_ctl *data = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret) {
		dev_err(data->dev, "%s: Failed to convert string to uint\n",
			__func__);
		return ret;
	}

	ret = tegra_hv_pm_ctl_trigger_guest_resume(val);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t guest_state_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct tegra_hv_pm_ctl *data = dev_get_drvdata(dev);
	u32 state = VM_STATE_BOOT;
	u32 vmid;
	int ret;

	mutex_lock(&data->mutex_lock);
	vmid = guest_id;
	mutex_unlock(&data->mutex_lock);
	ret = tegra_hv_pm_ctl_get_guest_state(vmid, &state);
	if (ret < 0)
		return ret;

	return snprintf(buf, PAGE_SIZE, "guest%u: %u\n", vmid, state);
}

static ssize_t guest_state_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct tegra_hv_pm_ctl *data = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret) {
		dev_err(data->dev, "%s: Failed to convert string to uint\n",
			__func__);
		return ret;
	}

	mutex_lock(&data->mutex_lock);
	guest_id = val;
	mutex_unlock(&data->mutex_lock);

	return count;
}

static ssize_t wait_for_guests_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct tegra_hv_pm_ctl *data = dev_get_drvdata(dev);
	ssize_t count = 0, result;
	ssize_t page_size = PAGE_SIZE;
	int i, ret;

	for (i = 0; i < data->wait_for_guests_size; i++) {
		if (check_sub_overflow(page_size, count, &result)) {
			pr_err("%s: operation got overflown.\n", __func__);
			return -EINVAL;
		}

		ret = snprintf(buf + count, result, "%u ",
				data->wait_for_guests[i]);
		if (ret < 0) {
			pr_err("%s: snprintf API got failed.\n", __func__);
			return -EINVAL;
		}

		if (check_add_overflow((ssize_t)ret, count, &count)) {
			pr_err("%s: operation got overflown.\n", __func__);
			return -EINVAL;
		}
	}

	if (check_sub_overflow(page_size, count, &result)) {
		pr_err("%s: operation got overflown.\n", __func__);
		return -EINVAL;
	}

	ret = snprintf(buf + count, result, "\n");
	if (ret < 0) {
		pr_err("%s: snprintf API got failed.\n", __func__);
		return -EINVAL;
	}

	if (check_add_overflow((ssize_t)ret, count, &count)) {
		pr_err("%s: operation got overflown.\n", __func__);
		return -EINVAL;
	}

	return count;
}

static DEVICE_ATTR_RO(ivc_id);
static DEVICE_ATTR_RO(ivc_frame_size);
static DEVICE_ATTR_RO(ivc_nframes);
static DEVICE_ATTR_RO(ivc_peer_vmid);
static DEVICE_ATTR_WO(trigger_sys_suspend);
static DEVICE_ATTR_WO(trigger_sys_shutdown);
static DEVICE_ATTR_WO(trigger_sys_reboot);
static DEVICE_ATTR_WO(trigger_guest_suspend);
static DEVICE_ATTR_WO(trigger_guest_resume);
static DEVICE_ATTR_RW(guest_state);
static DEVICE_ATTR_RO(wait_for_guests);

static struct attribute *tegra_hv_pm_ctl_attributes[] = {
	&dev_attr_ivc_id.attr,
	&dev_attr_ivc_frame_size.attr,
	&dev_attr_ivc_nframes.attr,
	&dev_attr_ivc_peer_vmid.attr,
	&dev_attr_trigger_sys_suspend.attr,
	&dev_attr_trigger_sys_shutdown.attr,
	&dev_attr_trigger_sys_reboot.attr,
	&dev_attr_trigger_guest_suspend.attr,
	&dev_attr_trigger_guest_resume.attr,
	&dev_attr_guest_state.attr,
	&dev_attr_wait_for_guests.attr,
	NULL
};

static const struct attribute_group tegra_hv_pm_ctl_attr_group = {
	.attrs = tegra_hv_pm_ctl_attributes,
};

static int tegra_hv_pm_ctl_setup(struct tegra_hv_pm_ctl *data)
{
	struct tegra_hv_ivc_cookie *ivck;
	struct device *chr_dev;
	int ret;

	ivck = tegra_hv_ivc_reserve(NULL, data->ivc, NULL);
	if (IS_ERR_OR_NULL(ivck)) {
		dev_err(data->dev, "%s: Failed to reserve IVC %d\n",
			__func__, data->ivc);
		data->ivck = NULL;
		return -ENODEV;
	}
	data->ivck = ivck;
	dev_dbg(data->dev,
		"%s: IVC %d: irq=%d, peer_vmid=%d, nframes=%d, frame_size=%d\n",
		__func__, data->ivc, ivck->irq, ivck->peer_vmid, ivck->nframes,
		ivck->frame_size);

	ret = devm_request_irq(data->dev, ivck->irq, tegra_hv_pm_ctl_irq,
			       0, dev_name(data->dev), data);
	if (ret < 0) {
		dev_err(data->dev, "%s: Failed to request irq %d, %d\n",
			__func__, ivck->irq, ret);
		goto error;
	}

	tegra_hv_ivc_channel_reset(ivck);

	ret = alloc_chrdev_region(&data->char_devt, 0, CHAR_DEV_COUNT,
				  DRV_NAME);
	if (ret < 0) {
		dev_err(data->dev, "%s: Failed to alloc chrdev region, %d\n",
			__func__, ret);
		goto error;
	}

	cdev_init(&data->cdev, &tegra_hv_pm_ctl_fops);
	data->cdev.owner = THIS_MODULE;
	ret = cdev_add(&data->cdev, data->char_devt, 1);
	if (ret) {
		dev_err(data->dev, "%s: Failed to add cdev, %d\n",
			__func__, ret);
		goto error_unregister_chrdev_region;
	}

	chr_dev = device_create(data->class, data->dev, data->char_devt,
				data, DRV_NAME);
	if (IS_ERR(chr_dev)) {
		dev_err(data->dev, "%s: Failed to create device, %ld\n",
			__func__, PTR_ERR(chr_dev));
		ret = PTR_ERR(chr_dev);
		goto error_cdev_del;
	}

	return 0;

error_cdev_del:
	cdev_del(&data->cdev);
error_unregister_chrdev_region:
	unregister_chrdev_region(MAJOR(data->char_devt), 1);
error:
	if (data->ivck)
		tegra_hv_ivc_unreserve(data->ivck);
	return ret;
}

static void tegra_hv_pm_ctl_cleanup(struct tegra_hv_pm_ctl *data)
{
	device_destroy(data->class, data->char_devt);
	cdev_del(&data->cdev);
	unregister_chrdev_region(MAJOR(data->char_devt), 1);
	if (data->ivck)
		tegra_hv_ivc_unreserve(data->ivck);
}

static int tegra_hv_pm_ctl_parse_dt(struct tegra_hv_pm_ctl *data)
{
	struct device_node *np = data->dev->of_node;
	int ret;

	if (!np) {
		dev_err(data->dev, "%s: Failed to find device node\n",
			__func__);
		return -EINVAL;
	}

	if (of_property_read_u32_index(np, "ivc", 1, &data->ivc)) {
		dev_err(data->dev, "%s: Failed to find ivc property in %s\n",
			__func__, np->full_name);
		return -EINVAL;
	}

	/* List of guests to wait before sending a System suspend command for
	 * dependency management. */
	ret = of_property_read_variable_u32_array(np, "wait-for-guests",
				data->wait_for_guests, 1, MAX_GUESTS_NUM);
	if (ret > 0)
		data->wait_for_guests_size = ret;

	return 0;
}

#ifdef CONFIG_PM_SLEEP

static void pm_recv_msg(struct sk_buff *skb)
{
	struct tegra_hv_pm_ctl *data = tegra_hv_pm_ctl_data;
	struct nlmsghdr *nlh;
	static uint32_t i;
	void *ptr = skb->data;

	nlh = (struct nlmsghdr *)ptr;

	/*process messages coming from User Space only*/
	if (nlh->nlmsg_pid != 0) {
		if (strcmp((char *)nlmsg_data(nlh), "PM Register") == 0) {

			bool hole = false;
			uint32_t loc = 0;

			/*regiter userspace Client with kernel*/
			spin_lock(&netlink_lock);
			for (i = 0; i < atomic_read(&user_client_count); i++) {
				if (user_client[i].client_pid == nlh->nlmsg_pid) {
					dev_warn(data->dev, "Client already registered \
								with pid:%d\n", nlh->nlmsg_pid);
					spin_unlock(&netlink_lock);
					return;
				}
				if (user_client[i].client_pid == 0 && hole == false) {
					hole = true;
					loc = i;
					break;
				}
			}

			dev_dbg(data->dev, "Registering UserSpace Client \
							with pid:%d\n", nlh->nlmsg_pid);
			if (hole == true) {
				user_client[loc].client_pid = nlh->nlmsg_pid;
			} else {
				uint32_t index = atomic_read(&user_client_count);
				if (index < MAX_USER_CLIENT - 1) {
					user_client[index].client_pid = nlh->nlmsg_pid;
					atomic_inc(&user_client_count);
				} else
					dev_err(data->dev, "Client Registration failed for pid:%d \
						due to resource exhaustion\n", nlh->nlmsg_pid);
			}
			spin_unlock(&netlink_lock);

		} else if (strcmp((char *)nlmsg_data(nlh), "PM Deregister") == 0) {
			/*regiter userspace Client with kernel*/
			bool deregistered = false;

			spin_lock(&netlink_lock);
			for (i = 0; i < atomic_read(&user_client_count); i++) {
				if (user_client[i].client_pid == nlh->nlmsg_pid) {
					dev_dbg(data->dev, "Deregistering UserSpace Client \
								with pid:%d\n", nlh->nlmsg_pid);
					user_client[i].client_pid = 0;
					deregistered = true;
					break;
				}
			}
			spin_unlock(&netlink_lock);

			if (deregistered == false) {
				dev_warn(data->dev, "Client already deregistered \
								with pid:%d\n", nlh->nlmsg_pid);
			}

		} else if (strcmp((char *)nlmsg_data(nlh), "Suspend Response") == 0) {
			/*update suspend response state for Client*/
			bool active = false;

			spin_lock(&netlink_lock);
			for (i = 0; i < atomic_read(&user_client_count); i++) {
				if (user_client[i].client_pid == nlh->nlmsg_pid
						&& user_client[i].suspend_response == false) {
					dev_dbg(data->dev, "Received Suspend Response \
								from pid:%d\n", nlh->nlmsg_pid);
					user_client[i].suspend_response = true;
					active = true;
					break;
				} else if (user_client[i].client_pid == nlh->nlmsg_pid
						&& user_client[i].suspend_response == true) {
					dev_warn(data->dev, "Already Received Suspend Response \
								from pid:%d\n", nlh->nlmsg_pid);
					active = true;
					break;
				}
			}
			spin_unlock(&netlink_lock);

			if (active == false) {
				dev_err(data->dev, "Client is not active \
								with pid:%d\n", nlh->nlmsg_pid);
			}

		} else if (strcmp((char *)nlmsg_data(nlh), "Resume Response") == 0) {
			/*update resume response state for Client*/
			bool active = false;

			spin_lock(&netlink_lock);
			for (i = 0; i < atomic_read(&user_client_count); i++) {
				if (user_client[i].client_pid == nlh->nlmsg_pid
						&& user_client[i].suspend_response == false) {
					dev_warn(data->dev, "Already Received Resume Response \
								from pid:%d\n", nlh->nlmsg_pid);
					active = true;
					break;
				} else if (user_client[i].client_pid == nlh->nlmsg_pid
						&& user_client[i].suspend_response == true) {
					dev_dbg(data->dev, "Received Resume Response \
								from pid:%d\n", nlh->nlmsg_pid);
					user_client[i].suspend_response = false;
					active = true;
					break;
				}
			}
			spin_unlock(&netlink_lock);

			if (active == false) {
				dev_err(data->dev, "Client is not active with pid:%d\n",
						nlh->nlmsg_pid);
			}

		} else {
			dev_err(data->dev, "Error: Wrong request coming from User Space\n");
		}
	} else {
		dev_warn(data->dev, "Data being sent from KernelSpace to KernelSpace\n");
	}

	/*invoke blocked task if got suspend response from all clients*/
	spin_lock(&netlink_lock);
	if (strcmp((char *)nlmsg_data(nlh), "Suspend Response") == 0) {
		for (i = 0; i < atomic_read(&user_client_count) && atomic_read(&user_client_count) > 0; i++) {
			if (user_client[i].client_pid > 0
					&& user_client[i].suspend_response == false) {
				spin_unlock(&netlink_lock);
				return;
			}
		}
	}

	/*invoke blocked task if got resume response from all clients*/
	if (strcmp((char *)nlmsg_data(nlh), "Resume Response") == 0) {
		for (i = 0; i < atomic_read(&user_client_count) && atomic_read(&user_client_count) > 0; i++) {
			if (user_client[i].client_pid > 0
					&& user_client[i].suspend_response == true) {
				spin_unlock(&netlink_lock);
				return;
			}
		}
	}
	spin_unlock(&netlink_lock);

	if (strcmp((char *)nlmsg_data(nlh), "Suspend Response") == 0
			|| strcmp((char *)nlmsg_data(nlh), "Resume Response") == 0) {
		dev_dbg(data->dev, "invoke sleeping thread\n");
		complete(&netlink_complete);
	}
}

static int notify_client(const char *msg, size_t msg_size)
{
	struct tegra_hv_pm_ctl *data = tegra_hv_pm_ctl_data;
	struct sk_buff *skb_out;
	struct nlmsghdr *nlh;
	struct pid *pid;
	struct task_struct *task;
	uint32_t i;
	int ret = 0;

	spin_lock(&netlink_lock);
	for (i = 0; i < atomic_read(&user_client_count); i++) {

		if (user_client[i].client_pid == 0)
			continue;

		/* Check whether process is still alive or not
		 * or the process has been killed without
		 * deregistering itself.
		 */
		pid = find_vpid(user_client[i].client_pid);
		if (pid == NULL) {
			user_client[i].client_pid = 0;
			continue;
		}

		task = pid_task(pid, PIDTYPE_PID);
		if (task == NULL) {
			user_client[i].client_pid = 0;
			continue;
		}

		skb_out = nlmsg_new(msg_size, 0);
		if (!skb_out) {
			dev_err(data->dev, "Failed to allocate new skb\n");
			ret = -ENOMEM;
			goto fail;
		}

		nlh = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, msg_size, 0);
		if (nlh != NULL)
			strscpy(nlmsg_data(nlh), msg, msg_size);
		else {
			dev_err(data->dev, "Failed to allocate netlink msg header\n");
			ret = -ENOMEM;
			goto fail;
		}

		if (user_client[i].client_pid > 0) {
			dev_dbg(data->dev, "Sending to client id: %d\n", user_client[i].client_pid);
			ret = nlmsg_unicast(nl_sk, skb_out, user_client[i].client_pid);
			if (ret < 0)
				goto fail;
		}
	}

fail:
	spin_unlock(&netlink_lock);
	return ret;
}

static int netlink_pm_notify(struct notifier_block *nb,
			     unsigned long mode, void *_unused)
{
	struct tegra_hv_pm_ctl *data = tegra_hv_pm_ctl_data;
	const char *suspend_req = "Suspend Request";
	const char *resume_req = "Resume Request";
	size_t msg_size;
	int ret;

	switch (mode) {
	case PM_HIBERNATION_PREPARE:
	case PM_RESTORE_PREPARE:
	case PM_SUSPEND_PREPARE:

		/*Send the message to userspace*/
		msg_size = strlen(suspend_req);

		ret = notify_client(suspend_req, msg_size);
		if (ret != 0)
			dev_err(data->dev, "Error while notifying clients %d\n", ret);
		else
			dev_dbg(data->dev, "all client notified successful\n");

		/*Receive the message from userspace*/
		if (atomic_read(&user_client_count))
			if (wait_for_completion_timeout(&netlink_complete,
							USERSPACE_RESPONSE_TIMEOUT) == 0) {
				dev_err(data->dev, "%s target suspend failed\n", __func__);
				return NOTIFY_BAD;
		}
		break;

	case PM_POST_HIBERNATION:
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:

		/*Send the message to userspace*/
		msg_size = strlen(resume_req);

		ret = notify_client(resume_req, msg_size);
		if (ret != 0)
			dev_err(data->dev, "Error while notifying clients %d\n", ret);
		else
			dev_dbg(data->dev, "all client notified successful\n");

		/*Receive the message from userspace*/
		if (atomic_read(&user_client_count))
			if (wait_for_completion_timeout(&netlink_complete,
						USERSPACE_RESPONSE_TIMEOUT) == 0) {
				dev_err(data->dev, "%s target resume failed\n", __func__);
				return NOTIFY_BAD;
		}
		break;

	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block netlink_pm_nb = {
	.notifier_call = netlink_pm_notify,
};
#endif

static int tegra_hv_pm_ctl_probe(struct platform_device *pdev)
{
	struct tegra_hv_pm_ctl *data;
	int ret;

#ifdef CONFIG_PM_SLEEP
	struct netlink_kernel_cfg cfg = {
		.input = pm_recv_msg,
	};
#endif

	if (!is_tegra_hypervisor_mode()) {
		dev_err(&pdev->dev, "%s: Hypervisor is not present\n",
			__func__);
		return -ENODEV;
	}

	data = devm_kzalloc(&pdev->dev, sizeof(struct tegra_hv_pm_ctl),
			    GFP_KERNEL);
	if (!data) {
		dev_err(&pdev->dev,
			"%s: Failed to alloc memory for driver data\n",
			__func__);
		return -ENOMEM;
	}

	data->dev = &pdev->dev;
	platform_set_drvdata(pdev, data);
	mutex_init(&data->mutex_lock);
	init_waitqueue_head(&data->wq);

	ret = tegra_hv_pm_ctl_parse_dt(data);
	if (ret < 0) {
		dev_err(&pdev->dev, "%s: Failed to parse device tree, %d\n",
			__func__, ret);
		return ret;
	}

#if defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG) /* Linux v6.4 */
	data->class = class_create(DRV_NAME);
#else
	data->class = class_create(THIS_MODULE, DRV_NAME);
#endif
	if (IS_ERR(data->class)) {
		dev_err(data->dev, "%s: Failed to create class, %ld\n",
			__func__, PTR_ERR(data->class));
		return PTR_ERR(data->class);
	}

	ret = sysfs_create_group(&pdev->dev.kobj, &tegra_hv_pm_ctl_attr_group);
	if (ret < 0) {
		dev_err(&pdev->dev, "%s: Failed to create sysfs group, %d\n",
			__func__, ret);
		goto error_class_destroy;
	}

	ret = tegra_hv_pm_ctl_setup(data);
	if (ret < 0) {
		dev_err(&pdev->dev, "%s: Failed to setup device, %d\n",
			__func__, ret);
		goto error_sysfs_remove_group;
	}

	tegra_hv_pm_ctl_data = data;

#ifdef CONFIG_PM_SLEEP

	init_completion(&netlink_complete);
	spin_lock_init(&netlink_lock);

	/*Creating netlink socket to communicate to Userspace*/
	nl_sk = netlink_kernel_create(&init_net, NETLINK_USERSPACE_PM, &cfg);
	if (!nl_sk) {
		dev_err(data->dev, "Error while creating socket.\n");
		ret = -EAGAIN;
		goto error_sysfs_remove_group;
	}

	/*register pm notifier for suspend/resume */
	ret = register_pm_notifier(&netlink_pm_nb);
	if (ret)
		dev_dbg(data->dev, "Couldn't register suspend notifier, return %d\n", ret);

#endif
	dev_info(&pdev->dev, "%s: Probed\n", __func__);

	return 0;

error_sysfs_remove_group:
	sysfs_remove_group(&pdev->dev.kobj, &tegra_hv_pm_ctl_attr_group);
error_class_destroy:
	class_destroy(data->class);

	return ret;
}

static int tegra_hv_pm_ctl_remove(struct platform_device *pdev)
{
	struct tegra_hv_pm_ctl *data = platform_get_drvdata(pdev);

	tegra_hv_pm_ctl_data = NULL;
	tegra_hv_pm_ctl_cleanup(data);
	sysfs_remove_group(&pdev->dev.kobj, &tegra_hv_pm_ctl_attr_group);
	class_destroy(data->class);

	return 0;
}

static const struct of_device_id tegra_hv_pm_ctl_match[] = {
	{ .compatible = "nvidia,tegra-hv-pm-ctl", },
	{},
};
MODULE_DEVICE_TABLE(of, tegra_hv_pm_ctl_match);

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void tegra_hv_pm_ctl_remove_wrapper(struct platform_device *pdev)
{
	tegra_hv_pm_ctl_remove(pdev);
}
#else
static int tegra_hv_pm_ctl_remove_wrapper(struct platform_device *pdev)
{
	return tegra_hv_pm_ctl_remove(pdev);
}
#endif

static struct platform_driver tegra_hv_pm_ctl_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(tegra_hv_pm_ctl_match),
	},
	.probe = tegra_hv_pm_ctl_probe,
	.remove = tegra_hv_pm_ctl_remove_wrapper,
};
module_platform_driver(tegra_hv_pm_ctl_driver);

MODULE_DESCRIPTION("Nvidia hypervisor PM control driver");
MODULE_AUTHOR("Jinyoung Park <jinyoungp@nvidia.com>");
MODULE_LICENSE("GPL");
