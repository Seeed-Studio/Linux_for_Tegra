// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2018-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/gpio.h>
#include <linux/list.h>
#include <linux/of_gpio.h>
#include <linux/time.h>
#include <uapi/linux/nvpps_ioctl.h>
#include <linux/hte.h>
#include <linux/nvpps.h>
#include <linux/overflow.h>
#include <linux/of_address.h>

#include "nvpps_common.h"
#include "nvpps_platforms.h"

#define MAX_NVPPS_SOURCES	1
#define NVPPS_DEF_MODE		NVPPS_MODE_GPIO

/* statics */
static struct class	*s_nvpps_class;
static dev_t		s_nvpps_devt;
static DEFINE_MUTEX(s_nvpps_lock);
static DEFINE_IDR(s_nvpps_idr);

bool print_pri_ptp_failed = true;
bool print_sec_ptp_failed = true;

/* platform device instance data */
struct nvpps_device_data {
	struct platform_device	*pdev;
	struct cdev		cdev;
	struct device		*dev;
	unsigned int		id;
	unsigned int		gpio_pin;
	int			irq;
	bool			irq_registered;
	bool			use_gpio_int_timestamp;
	bool			pps_event_id_valid;
	unsigned int		pps_event_id;
	u32			actual_evt_mode;
	u64			tsc;
	u64			phc;
	u64			secondary_phc;
	u64			irq_latency;
	u64			tsc_res_ns;
	raw_spinlock_t		lock;
	struct mutex		ts_lock;

	u32			evt_mode;
	u32			tsc_mode;

	struct timer_list	timer;
	u32			ts_capture_interval_ms;
	struct timer_list	tsc_timer;

	volatile bool		timer_inited;

	wait_queue_head_t	pps_event_queue;
	struct fasync_struct	*pps_event_async_queue;

	struct device_node	*pri_emac_node;
	struct device_node	*sec_emac_node;
	bool		only_timer_mode;
	bool		pri_ptp_failed;
	bool 		sec_ptp_failed;
	bool		support_tsc;
	uint32_t	lck_trig_interval;
	struct hte_ts_desc	desc;
	struct gpio_desc	*gpio_in;
	struct soc_dev_data soc_data;
};


/* file instance data */
struct nvpps_file_data {
	struct nvpps_device_data	*pdev_data;
	unsigned int			pps_event_id_rd;
};


#define TSC_POLL_TIMER	1000

static struct device_node *emac_node;
int32_t (*nvpps_get_ptp_ts_ns_fn)(struct device_node *mac_node, uint64_t *ptp_ts);

#define _NANO_SECS (1000000000ULL)

/* Macro defines the Min interval(in PPS edge cnt) at which PTP-TSC lock is triggered */
#define MIN_TSC_LOCK_TRIGGER_INTERVAL		1U
/* Macro defines the Max interval(in PPS edge cnt) at which PTP-TSC lock is triggered */
#define MAX_TSC_LOCK_TRIGGER_INTERVAL		8U

/* Define bounds for ts-capture-interval property in device tree */
#define TS_CAPTURE_INTERVAL_MIN_MS	100
#define TS_CAPTURE_INTERVAL_MAX_MS	1000

/*
 * Get PTP time
 * Clients may call this API whenever PTP time is needed.
 * If PTP time source is not registered, returns -EINVAL
 *
 * This API is available irrespective of nvpps dt availablity
 * When nvpps dt node is not present, interface name will
 * default to "eth0"
 */
int nvpps_get_ptp_ts(void *ts)
{
	int err = -EINVAL;

	if (nvpps_get_ptp_ts_ns_fn != NULL) {
		err = (nvpps_get_ptp_ts_ns_fn)(emac_node, ts);
			if (err != 0) {
				goto fail;
			}
	}

	err = 0;

fail:
	return err;
}
EXPORT_SYMBOL(nvpps_get_ptp_ts);

static int32_t nvpps_get_ptp_tsc_concurrent_ts(struct nvpps_device_data *pdev_data,
											   struct device_node *mac_node,
											   struct ptp_tsc_data *data)
{
	u64 tsc1, tsc2;
	u64 ptp_ts;
	int32_t ret = -EINVAL;

	/* HW Based concurrent TS */
	if (pdev_data->soc_data.ops->get_ptp_tsc_concurrent_ts_ns_fn != NULL) {
		ret = pdev_data->soc_data.ops->get_ptp_tsc_concurrent_ts_ns_fn(mac_node, data);
		if (ret != 0) {
			goto fail;
		}
	} else {
		/* SW Based concurrent TS */
		/* get the TSC time before the function call */
		ret = pdev_data->soc_data.ops->get_monotonic_tsc_ts_fn(&pdev_data->soc_data, &tsc1);
		if (ret != 0) {
			goto fail;
		}

		/* get the PTP time from requested MAC */
		ret = pdev_data->soc_data.ops->get_ptp_ts_ns_fn(mac_node, &ptp_ts);
		if (ret != 0) {
			goto fail;
		}

		/* get the TSC time after the function call */
		ret = pdev_data->soc_data.ops->get_monotonic_tsc_ts_fn(&pdev_data->soc_data, &tsc2);
		if (ret != 0) {
			goto fail;
		}

		/* we do not know the latency of the get_ptp_ts_ns_fn() function
		 * so we are measuring the before and after and use the two
		 * samples average to approximate the time when the PTP clock
		 * is sampled
		 */
		if (unlikely(check_add_overflow(tsc1, tsc2, &tsc1))) {
			ret = -EOVERFLOW;
			goto fail;
		}

		/* average the values stored in tsc1 */
		tsc1 = tsc1 / 2 ;

		data->ptp_ts = ptp_ts;

		/* check for overflow and set tsc_ts */
		if (unlikely(check_mul_overflow(tsc1, pdev_data->tsc_res_ns, &data->tsc_ts))) {
			ret = -EOVERFLOW;
			goto fail;
		}
	}

	ret = 0;

fail:
	return ret;
}

/*
 * Report the PPS event
 */
static void nvpps_get_ts(struct nvpps_device_data *pdev_data, u64 irq_tsc)
{
	u64		tsc = 0;
	u64		phc = 0;
	u64		secondary_phc = 0;
	u64		irq_latency = 0;
	unsigned long	flags;
	struct ptp_tsc_data ptp_tsc_ts = {0}, sec_ptp_tsc_ts = {0};
	int32_t err;

	/* get PTP_TSC concurrent timestamp from MAC driver */
	err = nvpps_get_ptp_tsc_concurrent_ts(pdev_data, pdev_data->pri_emac_node, &ptp_tsc_ts);
	if (err != 0) {
		pdev_data->pri_ptp_failed = true;
	} else {
		pdev_data->pri_ptp_failed = false;
		print_pri_ptp_failed = true;
		phc = ptp_tsc_ts.ptp_ts;
		tsc = ptp_tsc_ts.tsc_ts / pdev_data->tsc_res_ns;
	}

	if (pdev_data->sec_emac_node != NULL) {
		/* get PTP_TSC concurrent timestamp from MAC
		 * driver for secondary interface
		 */
		err = nvpps_get_ptp_tsc_concurrent_ts(pdev_data, pdev_data->sec_emac_node, &sec_ptp_tsc_ts);
		if (err != 0) {
			pdev_data->sec_ptp_failed = true;
		} else {
			/* Check for underflow condition */
			if (sec_ptp_tsc_ts.ptp_ts < (sec_ptp_tsc_ts.tsc_ts - ptp_tsc_ts.tsc_ts)) {
				pdev_data->sec_ptp_failed = false;
				dev_err(pdev_data->dev, "secondary intf concurrent TS underflow\n");
			} else {
				pdev_data->sec_ptp_failed = false;
				print_sec_ptp_failed = true;

				/* Adjust secondary iface's PTP TS to primary iface's concurrent PTP_TSC TS */
				secondary_phc = sec_ptp_tsc_ts.ptp_ts - (sec_ptp_tsc_ts.tsc_ts - ptp_tsc_ts.tsc_ts);
			}
		}
	}

	/* get the interrupt latency */
	if (irq_tsc) {
		irq_latency = (tsc - irq_tsc) * pdev_data->tsc_res_ns;
	}

	raw_spin_lock_irqsave(&pdev_data->lock, flags);
	pdev_data->pps_event_id_valid = true;
	pdev_data->pps_event_id++;
	pdev_data->tsc = irq_tsc ? irq_tsc : tsc;
	/* adjust the ptp time for the interrupt latency */
	pdev_data->phc = phc ? phc - irq_latency : phc;
	pdev_data->irq_latency = irq_latency;
	pdev_data->actual_evt_mode = irq_tsc ? NVPPS_MODE_GPIO : NVPPS_MODE_TIMER;
	/* Re-adjust secondary iface's PTP TS to irq_tsc TS,
	 * irq_latency will be 0 if TIMER mode,  >0 if GPIO mode
	 */
	pdev_data->secondary_phc = secondary_phc ? secondary_phc - irq_latency : secondary_phc;
	raw_spin_unlock_irqrestore(&pdev_data->lock, flags);

	/* event notification */
	wake_up_interruptible(&pdev_data->pps_event_queue);
	kill_fasync(&pdev_data->pps_event_async_queue, SIGIO, POLL_IN);
}

static irqreturn_t nvpps_gpio_isr(int irq, void *data)
{
	struct nvpps_device_data        *pdev_data = (struct nvpps_device_data *)data;
	uint64_t tsc_ts = 0;
	int32_t ret;

	/* If the current mode is TIMER mode, ignore the interrupt.
	 * If HTE is not enabled, use TSC and process the interrupt.
	 * If HTE is enabled, ignore the interrupt and process it in HTE callback
	 */
	if (!pdev_data->timer_inited) {
		if (!(pdev_data->use_gpio_int_timestamp)) {
			ret = pdev_data->soc_data.ops->get_monotonic_tsc_ts_fn(&pdev_data->soc_data, &tsc_ts);
			if (ret == 0)
				nvpps_get_ts(pdev_data, tsc_ts);
		}
	}

	return IRQ_HANDLED;
}

static void tsc_timer_callback(struct timer_list *t)
{
	struct nvpps_device_data *pdev_data =
#if defined(timer_container_of) /* Linux v6.16 */
		timer_container_of(pdev_data, t, tsc_timer);
#else
		from_timer(pdev_data, t, tsc_timer);
#endif

	if (pdev_data->soc_data.ops->ptp_tsc_get_is_locked_fn) {
		/* check and trigger sync if PTP-TSC is unlocked */
		if (pdev_data->soc_data.ops->ptp_tsc_get_is_locked_fn(&(pdev_data->soc_data)) == false) {
			if (pdev_data->soc_data.ops->ptp_tsc_synchronize_fn) {
				pdev_data->soc_data.ops->ptp_tsc_synchronize_fn(&(pdev_data->soc_data));
			}
		}
	}

	/* set the next expire time */
	mod_timer(&pdev_data->tsc_timer, jiffies + msecs_to_jiffies((TSC_POLL_TIMER / pdev_data->soc_data.pps_freq) * pdev_data->lck_trig_interval));
}


static void nvpps_timer_callback(struct timer_list *t)
{
	struct nvpps_device_data *pdev_data =
#if defined(timer_container_of) /* Linux v6.16 */
		timer_container_of(pdev_data, t, timer);
#else
		from_timer(pdev_data, t, timer);
#endif

	/* get timestamps for this event */
	nvpps_get_ts(pdev_data, 0);

	/* set the next expire time */
	if (pdev_data->timer_inited) {
		mod_timer(&pdev_data->timer, jiffies + msecs_to_jiffies(pdev_data->ts_capture_interval_ms));
	}
}

/* spawn timer to monitor TSC to PTP lock and re-activate
 locking process if its not locked in the handler */
static int set_mode_tsc(struct nvpps_device_data *pdev_data)
{
	timer_setup(&pdev_data->tsc_timer,
			tsc_timer_callback,
			0);

	mod_timer(&pdev_data->tsc_timer, jiffies + msecs_to_jiffies(1000));

	return 0;
}

/*
 * Store hardware timestamp
 */
static enum hte_return process_hw_ts(struct hte_ts_data *ts, void *p)
{
	struct nvpps_device_data *pdev_data = (struct nvpps_device_data *)p;

	/* If an callback is generated then check
	 * current mode in use. Ignore the callback
	 * if current mode is TIMER mode
	 */
	if (!pdev_data->timer_inited)
		nvpps_get_ts(pdev_data, ts->tsc);

	return HTE_CB_HANDLED;
}

static int set_mode(struct nvpps_device_data *pdev_data, u32 mode)
{
	int	err = 0;
	if (mode != pdev_data->evt_mode) {
		switch (mode) {
			case NVPPS_MODE_GPIO:
				if (!pdev_data->only_timer_mode) {
					if (pdev_data->timer_inited) {
						pdev_data->timer_inited = false;
#if defined(NV_TIMER_DELETE_PRESENT) /* Linux v6.15 */
						timer_delete_sync(&pdev_data->timer);
#else
						del_timer_sync(&pdev_data->timer);
#endif
					}
					if (!pdev_data->irq_registered) {
						/* register IRQ handler */
						err = devm_request_irq(pdev_data->dev, pdev_data->irq, nvpps_gpio_isr,
								IRQF_TRIGGER_RISING, "nvpps_isr", pdev_data);
						if (err) {
							dev_err(pdev_data->dev, "failed to acquire IRQ %d\n", pdev_data->irq);
						} else {
							pdev_data->irq_registered = true;
							dev_info(pdev_data->dev, "Registered IRQ %d for nvpps\n", pdev_data->irq);
						}
					} else {
						dev_dbg(pdev_data->dev, "IRQ %d for nvpps is already registered\n", pdev_data->irq);
					}
				} else {
					dev_err(pdev_data->dev, "unable to switch mode. Only timer mode is supported\n");
					err = -EINVAL;
				}
				break;

			case NVPPS_MODE_TIMER:
				/* If GPIO mode is run and IRQ is registered previously,
				 * then don't free the already requested IRQ. This is to
				 * avoid free'ing and re-registering of the IRQ when
				 * switching b/w the operating modes.
				 */
				/* If TIMER mode is requested and not initialized
				 * already then initialize it
				 */
				if (!pdev_data->timer_inited) {
					timer_setup(&pdev_data->timer,
							nvpps_timer_callback,
							0);
					pdev_data->timer_inited = true;
					/* setup timer interval to 1000 msecs */
					mod_timer(&pdev_data->timer, jiffies + msecs_to_jiffies(pdev_data->ts_capture_interval_ms));
				}
				break;

			default:
				return -EINVAL;
		}
	}
	return err;
}



/* Character device stuff */
static __poll_t nvpps_poll(struct file *file, poll_table *wait)
{
	struct nvpps_file_data		*pfile_data = (struct nvpps_file_data *)file->private_data;
	struct nvpps_device_data	*pdev_data = pfile_data->pdev_data;

	poll_wait(file, &pdev_data->pps_event_queue, wait);
	if (pdev_data->pps_event_id_valid &&
		(pfile_data->pps_event_id_rd != pdev_data->pps_event_id)) {
		return POLLIN | POLLRDNORM;
	} else {
		return 0;
	}
}


static int nvpps_fasync(int fd, struct file *file, int on)
{
	struct nvpps_file_data		*pfile_data = (struct nvpps_file_data *)file->private_data;
	struct nvpps_device_data	*pdev_data = pfile_data->pdev_data;

	return fasync_helper(fd, file, on, &pdev_data->pps_event_async_queue);
}


static long nvpps_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct nvpps_file_data		*pfile_data = (struct nvpps_file_data *)file->private_data;
	struct nvpps_device_data	*pdev_data = pfile_data->pdev_data;
	struct nvpps_params		params;
	void __user			*uarg = (void __user *)arg;
	int				err;

	switch (cmd) {
		case NVPPS_GETVERSION: {
			struct nvpps_version	version;

			dev_dbg(pdev_data->dev, "NVPPS_GETVERSION\n");

			/* Get the current parameters */
			version.version.major = NVPPS_VERSION_MAJOR;
			version.version.minor = NVPPS_VERSION_MINOR;
			version.api.major = NVPPS_API_MAJOR;
			version.api.minor = NVPPS_API_MINOR;

			err = copy_to_user(uarg, &version, sizeof(struct nvpps_version));
			if (err) {
				return -EFAULT;
			}
			break;
		}

		case NVPPS_GETPARAMS:
			dev_dbg(pdev_data->dev, "NVPPS_GETPARAMS\n");

			/* Get the current parameters */
			params.evt_mode = pdev_data->evt_mode;
			params.tsc_mode = pdev_data->tsc_mode;

			err = copy_to_user(uarg, &params, sizeof(struct nvpps_params));
			if (err) {
				return -EFAULT;
			}
			break;

		case NVPPS_SETPARAMS:
			dev_dbg(pdev_data->dev, "NVPPS_SETPARAMS\n");

			err = copy_from_user(&params, uarg, sizeof(struct nvpps_params));
			if (err) {
				return -EFAULT;
			}
			err = set_mode(pdev_data, params.evt_mode);
			if (err) {
				dev_dbg(pdev_data->dev, "switch_mode to %d failed err(%d)\n", params.evt_mode, err);
				return err;
			}
			pdev_data->evt_mode = params.evt_mode;
			pdev_data->tsc_mode = params.tsc_mode;
			break;

		case NVPPS_GETEVENT: {
			struct nvpps_timeevent	time_event;
			unsigned long		flags;

			dev_dbg(pdev_data->dev, "NVPPS_GETEVENT\n");

			/* check flag to print ptp failure msg */
			if ((pdev_data->pri_ptp_failed) && (print_pri_ptp_failed)) {
				dev_warn_ratelimited(pdev_data->dev,
					"failed to get PTP_TSC timestamp from primary emac instance\n");
				dev_warn_ratelimited(pdev_data->dev, "Make sure PTP is running\n");
				print_pri_ptp_failed = false;
			}

			/* check flag to print ptp failure msg */
			if ((pdev_data->sec_ptp_failed) && (print_sec_ptp_failed)) {
				dev_warn_ratelimited(pdev_data->dev,
					"failed to get PTP_TSC timestamp from secondary emac instance\n");
				dev_warn_ratelimited(pdev_data->dev, "Make sure PTP is running\n");
				print_sec_ptp_failed = false;
			}

			/* Return the fetched timestamp */
			raw_spin_lock_irqsave(&pdev_data->lock, flags);
			pfile_data->pps_event_id_rd = pdev_data->pps_event_id;
			time_event.evt_nb = pdev_data->pps_event_id;
			time_event.tsc = pdev_data->tsc;
			time_event.ptp = pdev_data->phc;
			time_event.secondary_ptp = pdev_data->secondary_phc;
			time_event.irq_latency = pdev_data->irq_latency;
			raw_spin_unlock_irqrestore(&pdev_data->lock, flags);
			if (pdev_data->tsc_mode == NVPPS_TSC_NSEC &&
				       !pdev_data->use_gpio_int_timestamp) {
				time_event.tsc *= pdev_data->tsc_res_ns;
			}
			time_event.tsc_res_ns = pdev_data->tsc_res_ns;
			/* return the mode when the time event actually occured */
			time_event.evt_mode = pdev_data->actual_evt_mode;
			time_event.tsc_mode = pdev_data->tsc_mode;

			err = copy_to_user(uarg, &time_event, sizeof(struct nvpps_timeevent));
			if (err) {
				return -EFAULT;
			}

			break;
		}

		case NVPPS_GETTIMESTAMP: {
			struct nvpps_timestamp_struct	time_stamp;
			u64	ns;
			u32	reminder;
			u64	tsc1, tsc2;
			u64	tsc_ts;

			dev_dbg(pdev_data->dev, "NVPPS_GETTIMESTAMP\n");

			err = pdev_data->soc_data.ops->get_monotonic_tsc_ts_fn(&pdev_data->soc_data, &tsc1);
			if (err != 0) {
				dev_err(pdev_data->dev, "failed to get monotonic tsc ts, err: %d\n", err);
				return err;
			}

			err = copy_from_user(&time_stamp, uarg,
				sizeof(struct nvpps_timestamp_struct));
			if (err)
				return -EFAULT;

			mutex_lock(&pdev_data->ts_lock);
			switch (time_stamp.clockid) {
			case CLOCK_REALTIME:
				ktime_get_real_ts64(&time_stamp.kernel_ts);
				break;

			case CLOCK_MONOTONIC:
				/* read TSC counter value */
				err = pdev_data->soc_data.ops->get_monotonic_tsc_ts_fn(&pdev_data->soc_data, &tsc_ts);
				if (err != 0) {
					dev_err(pdev_data->dev, "Failed to read CLOCK_MONOTONIC ts, err: %d\n", err);
					return err;
				}

				/* convert TSC counter value to nsec value */
				tsc_ts = tsc_ts * pdev_data->tsc_res_ns;

				/* split TSC TS in seconds & nanoseconds */
				time_stamp.kernel_ts.tv_sec = tsc_ts / _NANO_SECS;
				time_stamp.kernel_ts.tv_nsec = (tsc_ts - (time_stamp.kernel_ts.tv_sec * _NANO_SECS));
				break;

			default:
				dev_dbg(pdev_data->dev,
					"ioctl: Unsupported clockid\n");
			}

			err = pdev_data->soc_data.ops->get_ptp_ts_ns_fn(pdev_data->pri_emac_node, &ns);
			mutex_unlock(&pdev_data->ts_lock);
			if (err) {
				dev_err(pdev_data->dev, "HW PTP not running, err(%d)\n", err);
				return err;
			}
			time_stamp.hw_ptp_ts.tv_sec = div_u64_rem(ns,
							_NANO_SECS,
							&reminder);
			time_stamp.hw_ptp_ts.tv_nsec = reminder;

			err = pdev_data->soc_data.ops->get_monotonic_tsc_ts_fn(&pdev_data->soc_data, &tsc2);
			if (err != 0) {
				dev_err(pdev_data->dev, "Failed to get monotonic tsc ts, err(%d)\n", err);
				return err;
			}

			time_stamp.extra[0] =
				(tsc2 - tsc1) * pdev_data->tsc_res_ns;

			err = copy_to_user(uarg, &time_stamp,
				sizeof(struct nvpps_timestamp_struct));
			if (err)
				return -EFAULT;
			break;
		}

		default:
			return -ENOTTY;
	}

	return 0;
}



static int nvpps_open(struct inode *inode, struct file *file)
{
	struct nvpps_device_data	*pdev_data = container_of(inode->i_cdev, struct nvpps_device_data, cdev);
	struct nvpps_file_data		*pfile_data;

	pfile_data = kzalloc(sizeof(struct nvpps_file_data), GFP_KERNEL);
	if (!pfile_data) {
		dev_err(&pdev_data->pdev->dev, "nvpps_open kzalloc() failed\n");
		return -ENOMEM;
	}

	pfile_data->pdev_data = pdev_data;
	pfile_data->pps_event_id_rd = (unsigned int)-1;

	file->private_data = pfile_data;
	kobject_get(&pdev_data->dev->kobj);
	return 0;
}



static int nvpps_close(struct inode *inode, struct file *file)
{
	struct nvpps_device_data	*pdev_data = container_of(inode->i_cdev, struct nvpps_device_data, cdev);

	if (file->private_data) {
		kfree(file->private_data);
	}
	kobject_put(&pdev_data->dev->kobj);
	return 0;
}



static const struct file_operations nvpps_fops = {
	.owner		= THIS_MODULE,
	.poll		= nvpps_poll,
	.fasync		= nvpps_fasync,
	.unlocked_ioctl	= nvpps_ioctl,
	.open		= nvpps_open,
	.release	= nvpps_close,
};



static void nvpps_dev_release(struct device *dev)
{
	struct nvpps_device_data	*pdev_data = dev_get_drvdata(dev);

	cdev_del(&pdev_data->cdev);

	mutex_lock(&s_nvpps_lock);
	idr_remove(&s_nvpps_idr, pdev_data->id);
	mutex_unlock(&s_nvpps_lock);

	kfree(dev);
}

static int nvpps_gpio_hte_setup(struct nvpps_device_data *pdev_data)
{
	int err;
	struct platform_device  *pdev = pdev_data->pdev;

	pdev_data->use_gpio_int_timestamp = false;
	pdev_data->gpio_in = devm_gpiod_get_optional(&pdev->dev, "nvpps", 0);
	if (!pdev_data->gpio_in) {
		dev_info(&pdev->dev, "PPS GPIO not provided in DT, only Timer mode available\n");
		pdev_data->only_timer_mode = true;
		return 0;
	}

	err = gpiod_direction_input(pdev_data->gpio_in);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to set pin direction\n");
		return err;
	}

	/* IRQ setup */
	err = gpiod_to_irq(pdev_data->gpio_in);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to map GPIO to IRQ: %d\n", err);
		return err;
	}

	pdev_data->irq = err;
	dev_info(&pdev->dev, "gpio_to_irq(%d)\n", pdev_data->irq);

	/*
	 * Setup HTE. Note that HTE support is optional and so if it fails,
	 * still allow the driver to operate without it.
	 */
	err = hte_init_line_attr(&pdev_data->desc, 0, 0, NULL, pdev_data->gpio_in);
	if (err < 0) {
		dev_warn(&pdev->dev, "hte_init_line_attr failed\n");
		return 0;
	}

	err = hte_ts_get(&pdev->dev, &pdev_data->desc, 0);
	if (err < 0) {
		dev_warn(&pdev->dev, "hte_ts_get failed\n");
		return 0;
	}

	err = devm_hte_request_ts_ns(&pdev->dev, &pdev_data->desc,
			process_hw_ts, NULL, pdev_data);
	if (err < 0) {
		dev_warn(&pdev->dev, "devm_hte_request_ts_ns failed\n");
		return 0;
	}

	pdev_data->use_gpio_int_timestamp = true;
	dev_info(&pdev->dev, "HTE request timestamp succeed\n");

	return 0;
}

static int32_t nvpps_parse_tsc_dt_params(struct nvpps_device_data *pdev_data)
{
	struct platform_device *pdev = pdev_data->pdev;
	struct device_node *np = pdev->dev.of_node;
	struct resource *tsc_mem;
	int32_t err = 0;

	/* skip PTP TSC sync configuration if `ptp_tsc_sync_dis` is set */
	if ((of_property_read_bool(np, "ptp_tsc_sync_dis")) == false) {
		pdev_data->support_tsc = true;

		tsc_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (tsc_mem == NULL) {
			err = -ENODEV;
			dev_err(&pdev->dev, "TSC memory resource not defined\n");
			goto fail;
		}

		pdev_data->soc_data.reg_map_base = ioremap(tsc_mem->start, resource_size(tsc_mem));
		if (!pdev_data->soc_data.reg_map_base) {
			dev_err(&pdev->dev, "TSC register ioremap failed\n");
			err = -ENOMEM;
			goto fail;
		}

		/* read MAC pps_freq property & validate */
		err = of_property_read_u32(pdev_data->pri_emac_node, "nvidia,pps_op_ctrl", &pdev_data->soc_data.pps_freq);
		if (err < 0) {
			dev_err(&pdev->dev, "unable to read PPS freq property(nvidia,pps_op_ctrl) from MAC device node, err: %d\n", err);
			goto fail;
		}

		/* read tsc lock trigger interval from dt */
		err = of_property_read_u32(np, "ptp_tsc_sync_trig_interval", &pdev_data->lck_trig_interval);
		if (err < 0) {
			dev_err(&pdev->dev, "unable to read `ptp_tsc_sync_trig_interval` from dt node, err: %d\n", err);
			goto fail;
		}

		err = of_property_read_u32(np, "ptp_tsc_lock_threshold", &pdev_data->soc_data.lock_threshold_val);
		if (err < 0) {
			dev_err(&pdev->dev, "unable to read `ptp_tsc_lock_threshold` from dt node. err: %d\n", err);
			goto fail;
		}
	} else {
		pdev_data->support_tsc = false;
	}

fail:
	return err;
}

static int nvpps_probe(struct platform_device *pdev)
{
	struct nvpps_device_data	*pdev_data;
	struct device_node		*np = pdev->dev.of_node;
	dev_t				devt;
	int				err;
	const struct chip_ops    *cdata = NULL;
	int				index;
	uint32_t			initial_mode;
	struct resource res;

	dev_info(&pdev->dev, "%s\n", __FUNCTION__);

	if (!np) {
		dev_err(&pdev->dev, "no valid device node, probe failed\n");
		return -EINVAL;
	}

	pdev_data = devm_kzalloc(&pdev->dev, sizeof(struct nvpps_device_data), GFP_KERNEL);
	if (!pdev_data) {
		return -ENOMEM;
	}

	emac_node = NULL;

	cdata = of_device_get_match_data(&pdev->dev);
	pdev_data->soc_data.ops = cdata;

	pdev_data->pri_emac_node = of_parse_phandle(np, "primary-emac", 0);
	if (pdev_data->pri_emac_node == NULL) {
		dev_err(&pdev->dev, "primary-emac node not found\n");
		return -EINVAL;
	}

	index = of_property_match_string(pdev_data->pri_emac_node, "reg-names", "mac");
	if (index >= 0) {
		if (of_address_to_resource(pdev_data->pri_emac_node, index, &res)) {
			dev_err(&pdev->dev, "failed to parse primary emac reg property\n");
			return -EINVAL;
		} else {
			pdev_data->soc_data.pri_mac_base_pa = res.start;
		}
	} else {
		dev_err(&pdev->dev, "failed to find ethernet mac registers\n");
		return -EINVAL;
	}

	dev_info(&pdev->dev, "primary-emac : %s", pdev_data->pri_emac_node->full_name);

	emac_node = pdev_data->pri_emac_node;
	nvpps_get_ptp_ts_ns_fn = pdev_data->soc_data.ops->get_ptp_ts_ns_fn;

	pdev_data->sec_emac_node = of_parse_phandle(np, "sec-emac", 0);
	if (pdev_data->sec_emac_node == NULL) {
		dev_info(&pdev->dev, "sec-emac node not found\n");
	} else {
		if (pdev_data->pri_emac_node == pdev_data->sec_emac_node) {
			dev_err(&pdev->dev, "sec-emac is same as primary-emac node. Undefine sec-emac if not needed\n");
			return -EINVAL;
		}
	}

	init_waitqueue_head(&pdev_data->pps_event_queue);
	raw_spin_lock_init(&pdev_data->lock);
	mutex_init(&pdev_data->ts_lock);
	pdev_data->pdev = pdev;
	pdev_data->evt_mode = 0; /* NVPPS_MODE_GPIO */
	pdev_data->tsc_mode = NVPPS_TSC_NSEC;

	/* Get ts-capture-interval from device tree, default to 1000ms if not specified */
	err = of_property_read_u32(np, "ts-capture-interval", &pdev_data->ts_capture_interval_ms);
	if (err < 0) {
		pdev_data->ts_capture_interval_ms = TS_CAPTURE_INTERVAL_MAX_MS;
		dev_info(&pdev->dev, "ts-capture-interval not specified, using default %ums\n",
				 pdev_data->ts_capture_interval_ms);
	} else {
		dev_info(&pdev->dev, "ts-capture-interval set to %ums\n",
				 pdev_data->ts_capture_interval_ms);
	}

	/* Validate ts_capture_interval_ms is within valid range */
	if ((pdev_data->ts_capture_interval_ms < TS_CAPTURE_INTERVAL_MIN_MS) ||
	    (pdev_data->ts_capture_interval_ms > TS_CAPTURE_INTERVAL_MAX_MS)) {
		dev_err(&pdev->dev, "timestamp capture interval %ums is invalid. Please refer to binding doc for valid range\n",
				pdev_data->ts_capture_interval_ms);
		return -ERANGE;
	}

	/* Set up GPIO and HTE */
	err = nvpps_gpio_hte_setup(pdev_data);
	if (err < 0)
		return err;

	/* Get initial operating mode from device tree, default to timer mode if not specified */
	err = of_property_read_u32(np, "nvpps-event-mode-init", &initial_mode);
	if (err < 0) {
		if (pdev_data->only_timer_mode == true) {
			dev_warn(&pdev->dev, "nvpps-event-mode-init property not specified err(%d), using default value 2 (TIMER mode)\n", err);
			initial_mode = NVPPS_MODE_TIMER;
		} else {
			dev_warn(&pdev->dev, "nvpps-event-mode-init property not specified err(%d), using default value 1 (GPIO mode)\n", err);
			initial_mode = NVPPS_MODE_GPIO;
		}
	}

	/* Validate initial operation mode selected is valid */
	if (initial_mode == NVPPS_MODE_GPIO) {
		/* Validate if initial operation mode selected(GPIO) can be supported.
		 * If nvpps-gpios property is not specified, only_timer_mode is true and
		 * GPIO mode cannot be selected.
		 */
		if (pdev_data->only_timer_mode == true) {
			dev_err(&pdev->dev, "GPIO mode as initial operating mode cannot be selected without defining nvpps-gpios property\n");
			return -EINVAL;
		}
		dev_info(&pdev->dev, "Initial operating mode selected as GPIO\n");
	} else if (initial_mode == NVPPS_MODE_TIMER) {
		dev_info(&pdev->dev, "Initial operating mode selected as TIMER\n");
	} else {
		dev_err(&pdev->dev, "Invalid initial operating mode %u specified. Valid values are 1 (GPIO) and 2 (TIMER)\n",
				initial_mode);
		return -EINVAL;
	}

	/* character device setup */
#if defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG) /* Linux v6.4 */
	s_nvpps_class = class_create("nvpps");
#else
	s_nvpps_class = class_create(THIS_MODULE, "nvpps");
#endif
	if (IS_ERR(s_nvpps_class)) {
		dev_err(&pdev->dev, "failed to allocate class\n");
		return PTR_ERR(s_nvpps_class);
	}

	err = alloc_chrdev_region(&s_nvpps_devt, 0, MAX_NVPPS_SOURCES, "nvpps");
	if (err < 0) {
		dev_err(&pdev->dev, "failed to allocate char device region\n");
		class_destroy(s_nvpps_class);
		return err;
	}

	/* get an idr for the device */
	mutex_lock(&s_nvpps_lock);
	err = idr_alloc(&s_nvpps_idr, pdev_data, 0, MAX_NVPPS_SOURCES, GFP_KERNEL);
	if (err < 0) {
		if (err == -ENOSPC) {
			dev_err(&pdev->dev, "nvpps: out of idr \n");
			err = -EBUSY;
		}
		mutex_unlock(&s_nvpps_lock);
		return err;
	}
	pdev_data->id = err;
	mutex_unlock(&s_nvpps_lock);

	/* associate the cdev with the file operations */
	cdev_init(&pdev_data->cdev, &nvpps_fops);

	/* build up the device number */
	devt = MKDEV(MAJOR(s_nvpps_devt), pdev_data->id);
	pdev_data->cdev.owner = THIS_MODULE;

	/* create the device node */
	pdev_data->dev = device_create(s_nvpps_class, NULL, devt, pdev_data, "nvpps%d", pdev_data->id);
	if (IS_ERR(pdev_data->dev)) {
		err = PTR_ERR(pdev_data->dev);
		goto error_ret;
	}

	pdev_data->dev->release = nvpps_dev_release;

	err = cdev_add(&pdev_data->cdev, devt, 1);
	if (err) {
		dev_err(&pdev->dev, "nvpps: failed to add char device %d:%d\n",
			MAJOR(s_nvpps_devt), pdev_data->id);
		device_destroy(s_nvpps_class, pdev_data->dev->devt);
		goto error_ret;
	}

	dev_info(&pdev->dev, "nvpps cdev(%d:%d)\n", MAJOR(s_nvpps_devt), pdev_data->id);
	platform_set_drvdata(pdev, pdev_data);

	/* setup PPS event hndler */
	err = set_mode(pdev_data, initial_mode);
	if (err) {
		dev_err(&pdev->dev, "set_mode failed err = %d\n", err);
		device_destroy(s_nvpps_class, pdev_data->dev->devt);
		goto error_ret;
	}
	pdev_data->evt_mode = initial_mode;

	pdev_data->soc_data.dev = pdev_data->dev;

	err = nvpps_parse_tsc_dt_params(pdev_data);
	if (err) {
		goto error_ret2;
	}

	if (pdev_data->support_tsc) {
		/* validate if lck_trig_interval is in allowed range */
		if ((pdev_data->lck_trig_interval < MIN_TSC_LOCK_TRIGGER_INTERVAL) ||
			(pdev_data->lck_trig_interval > MAX_TSC_LOCK_TRIGGER_INTERVAL)) {
			dev_err(&pdev->dev,
				"Invalid input provided for ptp_tsc_sync_trig_interval. Allowed range is %u - %u\n",
				MIN_TSC_LOCK_TRIGGER_INTERVAL, MAX_TSC_LOCK_TRIGGER_INTERVAL);
			err = -EINVAL;
			goto error_ret2;
		}

		/* validate if required function ptrs are initialized */
		if ((pdev_data->soc_data.ops->ptp_tsc_sync_cfg_fn == NULL) ||
			(pdev_data->soc_data.ops->ptp_tsc_synchronize_fn == NULL) ||
			(pdev_data->soc_data.ops->ptp_tsc_get_is_locked_fn == NULL) ||
			(pdev_data->soc_data.ops->get_tsc_res_ns_fn == NULL) ||
			(pdev_data->soc_data.ops->get_monotonic_tsc_ts_fn == NULL) ||
			(pdev_data->soc_data.ops->get_ptp_ts_ns_fn == NULL)) {
			dev_err(&pdev->dev, "Reqd functions not initialized\n");
			err = -EINVAL;
			goto error_ret2;
		}

		if (pdev_data->soc_data.ops->get_ptp_tsc_concurrent_ts_ns_fn == NULL) {
			dev_info(&pdev->dev, "HW based PTP_TSC concurrent timestamping feature not registered. Using SW approach\n");
		}

		/* initialize TSC to synchronize with PTP src */
		if (pdev_data->soc_data.ops->ptp_tsc_sync_cfg_fn(&(pdev_data->soc_data)) != 0) {
			dev_err(&pdev->dev, "failed to initialize HW TSC");
			err = -EINVAL;
			goto error_ret2;
		} else {
			if (pdev_data->soc_data.ops->get_tsc_res_ns_fn(&pdev_data->soc_data, &pdev_data->tsc_res_ns) != 0) {
				dev_err(&pdev->dev, "failed to get tsc resolution");
				err = -EINVAL;
				goto error_ret2;
			}
			dev_info(&pdev->dev, "tsc_res_ns(%llu)\n", pdev_data->tsc_res_ns);

			/* setup timer to monitor & trigger PTP-TSC sync */
			set_mode_tsc(pdev_data);
		}
	} else {
		dev_info(&pdev->dev, "ptp tsc sync is disabled\n");
	}

	return 0;

error_ret2:
	device_destroy(s_nvpps_class, pdev_data->dev->devt);
	class_destroy(s_nvpps_class);

error_ret:
	of_node_put(pdev_data->pri_emac_node);
	of_node_put(pdev_data->sec_emac_node);
	cdev_del(&pdev_data->cdev);
	mutex_lock(&s_nvpps_lock);
	idr_remove(&s_nvpps_idr, pdev_data->id);
	mutex_unlock(&s_nvpps_lock);
	return err;
}

static int nvpps_remove(struct platform_device *pdev)
{
	struct nvpps_device_data	*pdev_data = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "%s\n", __FUNCTION__);

	if (pdev_data) {
		if (pdev_data->timer_inited) {
			pdev_data->timer_inited = false;
#if defined(NV_TIMER_DELETE_PRESENT) /* Linux v6.15 */
			timer_delete_sync(&pdev_data->timer);
#else
			del_timer_sync(&pdev_data->timer);
#endif
		}
		if (pdev_data->support_tsc) {
#if defined(NV_TIMER_DELETE_PRESENT) /* Linux v6.15 */
			timer_delete_sync(&pdev_data->tsc_timer);
#else
			del_timer_sync(&pdev_data->tsc_timer);
#endif
			iounmap(pdev_data->soc_data.reg_map_base);
		}
		device_destroy(s_nvpps_class, pdev_data->dev->devt);
	}

	of_node_put(pdev_data->pri_emac_node);
	of_node_put(pdev_data->sec_emac_node);

	class_unregister(s_nvpps_class);
	class_destroy(s_nvpps_class);
	unregister_chrdev_region(s_nvpps_devt, MAX_NVPPS_SOURCES);
	return 0;
}


#ifdef CONFIG_PM
static int nvpps_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct nvpps_device_data *pdev_data = platform_get_drvdata(pdev);

	if (pdev_data->soc_data.ops->ptp_tsc_suspend_sync_fn) {
		if (pdev_data->soc_data.ops->ptp_tsc_suspend_sync_fn(&(pdev_data->soc_data)) != 0) {
			dev_err(pdev_data->dev, "Failed to suspend\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int nvpps_resume(struct platform_device *pdev)
{
	struct nvpps_device_data *pdev_data = platform_get_drvdata(pdev);

	if (pdev_data->soc_data.ops->ptp_tsc_resume_sync_fn) {
		if (pdev_data->soc_data.ops->ptp_tsc_resume_sync_fn(&(pdev_data->soc_data)) != 0) {
			dev_err(pdev_data->dev, "Failed to resume\n");
			return -EINVAL;
		}
	}

	return 0;
}
#endif /* CONFIG_PM */



#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void nvpps_remove_wrapper(struct platform_device *pdev)
{
	nvpps_remove(pdev);
}
#else
static int nvpps_remove_wrapper(struct platform_device *pdev)
{
	return nvpps_remove(pdev);
}
#endif

static struct platform_driver nvpps_plat_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(nvpps_of_table),
	},
	.probe = nvpps_probe,
	.remove = nvpps_remove_wrapper,
#ifdef CONFIG_PM
	.suspend = nvpps_suspend,
	.resume = nvpps_resume,
#endif /* CONFIG_PM */
};

module_platform_driver(nvpps_plat_driver);

MODULE_DESCRIPTION("NVidia Tegra PPS Driver");
MODULE_AUTHOR("David Tao tehyut@nvidia.com");
MODULE_LICENSE("GPL v2");
