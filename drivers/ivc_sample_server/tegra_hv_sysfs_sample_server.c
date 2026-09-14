/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

/* Driver logging string*/
#define pr_fmt(fmt)					"[TEGRA_HV_SAMPLE_SERVER] " fmt

#include <nvidia/conftest.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/interrupt.h>
#include <linux/atomic.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/string.h>
#include <linux/err.h>
#include <soc/tegra/virt/hv-ivc.h>

#define BUF_SIZE					200UL
#define DRIVER_NAME		 			"tegra_hv_sample_server_drv"
/*Replace the channel Id no with your channel ID*/
#define DEFAULT_CHANNEL_ID		 	31U

static unsigned int ivc_channel_id = DEFAULT_CHANNEL_ID;
module_param(ivc_channel_id, uint, 0444);
MODULE_PARM_DESC(ivc_channel_id, "IVC Channel ID, default channel id is 31");

struct sample_msg {
	uint64_t req_resp_counter;
	uint32_t cpuId; //core id on which the KMD Runs
	uint8_t buf[BUF_SIZE];
} __attribute__((aligned(64)));

struct ivc_sample_dev {
	struct kobject *kobj;
	struct tegra_hv_ivc_cookie *ivck;
	struct work_struct tx_work;
	struct work_struct rx_work;
	bool is_initialized;
	atomic_t is_ivc_estd;
	struct sample_msg usr_txmsg;	// Buffer to store user message
	struct sample_msg usr_rxmsg;	// Buffer to hold response from sample server
	struct mutex sysfs_txlock;		// Protect sysfs write operations
	struct mutex sysfs_rxlock;	   	// Protect sysfs read operations
	wait_queue_head_t rx_wait;		// Wait queue for blocking read
	atomic_t is_msg_avbl;
	uint64_t counter_arr[NR_CPUS];
};

static struct ivc_sample_dev *ivc_data;

// Function to write data to IVC channel
static int ivc_write_data(struct ivc_sample_dev *data, const void *buffer)
{
	void *frame;
	int ret;
	size_t frame_size = 0;

	// Check if we can write
	if (!tegra_hv_ivc_can_write(data->ivck)) {
		pr_err("channel is in bad state, can not write\n");
		return -EBUSY;
	}

	frame_size = data->ivck->frame_size;
	if (frame_size < sizeof(data->usr_txmsg)) {
		pr_err("Frame size: %zu is small, can not accomodate buffer of size %zu\n",
			frame_size, sizeof(data->usr_txmsg));
		return -ENOMEM;
	}

	// Get next frame to write
	frame = tegra_hv_ivc_write_get_next_frame(data->ivck);
	if (!frame) {
		pr_err("Failed to get next frame\n");
		return -EIO;
	}

	// Copy data to frame
	memcpy(frame, buffer, sizeof(data->usr_txmsg));

	// Advance write pointer
	ret = tegra_hv_ivc_write_advance(data->ivck);
	if (ret) {
		pr_err("Failed to advance write pointer\n");
		return ret;
	}

	pr_info("Msg sent from CpuId:[%u] Msg_count:[%llu] to sample server, Msg:[%s]\n",
		ivc_data->usr_txmsg.cpuId, ivc_data->usr_txmsg.req_resp_counter, ivc_data->usr_txmsg.buf);

	return 0;
}

static void ivc_work_tx_handler(struct work_struct *work)
{
	int ret;

	(void)work;
	mutex_lock(&ivc_data->sysfs_txlock);
	if (atomic_read(&ivc_data->is_ivc_estd)) {
		ret = ivc_write_data(ivc_data, &ivc_data->usr_txmsg);

		if (ret)
			pr_err("Failed to write data: %d\n", ret);
	} else {
		pr_err("Failed to schedule tx work; Channel not established\n");
	}
	mutex_unlock(&ivc_data->sysfs_txlock);
}

// Work function to handle received data
static void ivc_work_rx_handler(struct work_struct *work)
{
	void *frame;
	int ret;
	size_t len = 0;
	size_t frame_size = ivc_data->ivck->frame_size;

	(void)work;
	// Check if we can read
	if (!tegra_hv_ivc_can_read(ivc_data->ivck)) {
		pr_err("Failed to read, ivc channel is in bad state\n");
		return;
	}

	// Get next frame to read
	frame = tegra_hv_ivc_read_get_next_frame(ivc_data->ivck);
	if (!frame) {
		pr_err("Failed to get next frame\n");
		return;
	}

	len = min_t(size_t, frame_size, sizeof(ivc_data->usr_rxmsg));

	// Copy data from frame to buffer
	memcpy(&ivc_data->usr_rxmsg, frame, len);

	// Set message available flag and wake up readers
	atomic_set(&ivc_data->is_msg_avbl, 1);
	wake_up_interruptible(&ivc_data->rx_wait);

	// Process the received data
	pr_info("Msg:[%s] \n msg_counter:[%llu] Received from sample server CpuID[%u]:",
		ivc_data->usr_rxmsg.buf, ivc_data->usr_rxmsg.req_resp_counter,
		ivc_data->usr_rxmsg.cpuId);

	// Advance read pointer
	ret = tegra_hv_ivc_read_advance(ivc_data->ivck);
	if (ret) {
		pr_err("Failed to advance read pointer\n");
		return;
	}
}

static irqreturn_t tegra_hv_sample_server_irq_handler(int irq, void *data)
{
	struct ivc_sample_dev *ivc_data = data;

	(void)irq;
	/* until this function returns 0, the channel is unusable */
	if (tegra_hv_ivc_channel_notified(ivc_data->ivck) != 0)
		return IRQ_HANDLED;

	// Initialize atomic flag if not set
	if (!atomic_read(&ivc_data->is_ivc_estd))
		atomic_set(&ivc_data->is_ivc_estd, 1);

	if (tegra_hv_ivc_can_read(ivc_data->ivck) && !work_pending(&ivc_data->rx_work))
		schedule_work(&ivc_data->rx_work);

	return IRQ_HANDLED;
}

/* Show function - read value from kernel to userspace */
static ssize_t tegra_hv_usr_rxmsg_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	size_t len = sizeof(ivc_data->usr_rxmsg) > PAGE_SIZE ? PAGE_SIZE : sizeof(ivc_data->usr_rxmsg);
	ssize_t ret;
	int err;

	(void)kobj;
	(void)attr;
	// Wait for message to become available
	err = wait_event_interruptible(ivc_data->rx_wait,
			atomic_read(&ivc_data->is_msg_avbl));
	if (err)
		return -ERESTARTSYS;  // Signal received

	mutex_lock(&ivc_data->sysfs_rxlock);
	ret = scnprintf(buf, len, "Msg:[%s] \n Msg_count:[%llu] from server Thread ID:[%u]\n",
			ivc_data->usr_rxmsg.buf, ivc_data->usr_rxmsg.req_resp_counter,
			ivc_data->usr_rxmsg.cpuId);
	mutex_unlock(&ivc_data->sysfs_rxlock);
	//clear the state
	atomic_set(&ivc_data->is_msg_avbl, 0);

	return ret;
}

/* Store function - write value from userspace to kernel */
static ssize_t tegra_hv_usr_txmsg_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	size_t len;
	size_t buff_len = sizeof(ivc_data->usr_txmsg.buf);
	uint32_t cpuId = 0;

	(void)kobj;
	(void)attr;
	mutex_lock(&ivc_data->sysfs_txlock);
	memset(ivc_data->usr_txmsg.buf, 0, buff_len);
	/* Copy the message, leaving space for null terminator */
	len = min_t(size_t, count, buff_len - 1);
	memcpy(ivc_data->usr_txmsg.buf, buf, len);
	ivc_data->usr_txmsg.buf[len] = '\0';
	//get the CpuId and increment the counter
	cpuId = (uint32_t)smp_processor_id();

	if (cpuId >= nr_cpu_ids) {
	/*If the cpuId is out of range then, Set the value of cpuId to core zero*/
		pr_err("cpuId %u is greater than or equal to nr_cpu_ids:%u, set cpuID to Zero \n",
			cpuId, nr_cpu_ids);
		cpuId = 0;
	}
	ivc_data->usr_txmsg.cpuId = cpuId;
	ivc_data->counter_arr[cpuId] = ivc_data->counter_arr[cpuId] + 1;
	ivc_data->usr_txmsg.req_resp_counter = ivc_data->counter_arr[cpuId];
	/* Schedule work to send the message */
	if (ivc_data->is_initialized && !work_pending(&ivc_data->tx_work))
		schedule_work(&ivc_data->tx_work);

	mutex_unlock(&ivc_data->sysfs_txlock);

	return count;
}

/* Define kobject attributes */
static struct kobj_attribute tegra_hv_usr_txmsg_attr =
	__ATTR(tegra_hv_usr_txmsg, (S_IWUSR | S_IRUGO | S_IWGRP), NULL, tegra_hv_usr_txmsg_store);

static struct kobj_attribute tegra_hv_usr_rxmsg_attr =
	__ATTR(tegra_hv_usr_rxmsg, S_IRUGO, tegra_hv_usr_rxmsg_show, NULL);

/* Create attribute array*/
static struct attribute *ivc_usr_msg_attrs[] = {
	&tegra_hv_usr_txmsg_attr.attr,		// Write interface
	&tegra_hv_usr_rxmsg_attr.attr,		// Read interface
	NULL,
};

/* Define the attribute group */
static const struct attribute_group ivc_usr_msg_attr_group = {
	.attrs = ivc_usr_msg_attrs,
};

static int __init ivc_sample_server_init(void)
{
	int ret;

	// Add at start of init
	if (!is_tegra_hypervisor_mode()) {
		pr_err("ivc_sample_server_init Hypervisor not present\n");
		return -ENODEV;
	}

	// Allocate driver data
	ivc_data = kzalloc(sizeof(*ivc_data), GFP_KERNEL);
	if (!ivc_data) {
		return -ENOMEM;
	}

	// Initialize mutex
	mutex_init(&ivc_data->sysfs_txlock);
	mutex_init(&ivc_data->sysfs_rxlock);

	// IVC channel is not established
	atomic_set(&ivc_data->is_ivc_estd, 0);

	// Initialize work queue
	INIT_WORK(&ivc_data->tx_work, ivc_work_tx_handler);
	INIT_WORK(&ivc_data->rx_work, ivc_work_rx_handler);

	init_waitqueue_head(&ivc_data->rx_wait);
	atomic_set(&ivc_data->is_msg_avbl, 0);

	/* Create kobject for sysfs */
	ivc_data->kobj = kobject_create_and_add("tegra_hv_sample_server", kernel_kobj);
	if (!ivc_data->kobj) {
		pr_err("Failed to create kobject\n");
		ret = -ENOMEM;
		goto err_free;
	}

	/* Create sysfs group*/
	ret = sysfs_create_group(ivc_data->kobj, &ivc_usr_msg_attr_group);
	if (ret) {
		pr_err("Failed to create sysfs group: %d\n", ret);
		goto err_kobj;
	}

	// Reserve IVC channel
	ivc_data->ivck = tegra_hv_ivc_reserve(NULL, ivc_channel_id, NULL);
	if (IS_ERR(ivc_data->ivck)) {
		ret = PTR_ERR(ivc_data->ivck);
		pr_err("Failed to reserve IVC channel: %d\n", ret);
		goto err_sysfs;
	}
	/*
	 * start the channel reset process asynchronously. until the reset
	 * process completes, any attempt to use the ivc channel will return
	 * an error (e.g., all transmits will fail).
	 */
	tegra_hv_ivc_channel_reset(ivc_data->ivck);

	/* the interrupt request must be the last action */
	ret = request_irq(ivc_data->ivck->irq, tegra_hv_sample_server_irq_handler,
						0, DRIVER_NAME, ivc_data);
	if (ret != 0) {
		pr_err("Could not request irq #%d\n", ivc_data->ivck->irq);
		goto err_unreserve_device;
	}
	ivc_data->is_initialized = true;

	pr_info("IVC sample server driver initialized\n");
	pr_info("Sysfs files created at: /sys/kernel/tegra_hv_sample_server/\n");
	pr_info("Files: tegra_hv_usr_txmsg, tegra_hv_usr_rxmsg\n");
	return 0;

err_unreserve_device:
    tegra_hv_ivc_unreserve(ivc_data->ivck);

err_sysfs:
	sysfs_remove_group(ivc_data->kobj, &ivc_usr_msg_attr_group);

err_kobj:
	kobject_put(ivc_data->kobj);

err_free:
	kfree(ivc_data);

	pr_err("Error: HV_Ivc_sample_server_driver failed to initialize\n");
	return ret;
}

static void __exit ivc_sample_server_exit(void)
{
	if (ivc_data) {
		// Wake up any waiting readers
		atomic_set(&ivc_data->is_msg_avbl, 1);
		wake_up_interruptible(&ivc_data->rx_wait);
		// Cancel any pending work
		cancel_work_sync(&ivc_data->tx_work);
		cancel_work_sync(&ivc_data->rx_work);

		if (ivc_data->is_initialized) {
			// Free the irq handler
			free_irq(ivc_data->ivck->irq, ivc_data);
			// Unreserve IVC channel
			ivc_data->is_initialized = false;
			atomic_set(&ivc_data->is_ivc_estd, 0);
			tegra_hv_ivc_unreserve(ivc_data->ivck);
		}
		// Remove sysfs group
		sysfs_remove_group(ivc_data->kobj, &ivc_usr_msg_attr_group);

		// Remove kobject
		kobject_put(ivc_data->kobj);

		mutex_destroy(&ivc_data->sysfs_txlock);
		mutex_destroy(&ivc_data->sysfs_rxlock);
		kfree(ivc_data);
	}
}

module_init(ivc_sample_server_init);
module_exit(ivc_sample_server_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("sourab bera <sourabb@nvidia.com>");
MODULE_DESCRIPTION("Sample IVC driver to communicate with sample server");

