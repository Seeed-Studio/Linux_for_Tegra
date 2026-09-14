// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2018-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

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
#include <linux/gpio/consumer.h>
#include <linux/list.h>
#include <linux/time.h>
#include <uapi/linux/nvpps_ioctl.h>
#include <linux/hte.h>
#include <linux/nvpps.h>
#include <linux/of_address.h>
#include <linux/overflow.h>
#include <linux/workqueue.h>

#include "nvpps_common.h"
#include "nvpps_platforms.h"

#define MAX_NVPPS_SOURCES	1
#define NVPPS_DEF_MODE		NVPPS_MODE_GPIO

/* statics */
static struct class	*s_nvpps_class;
static dev_t		s_nvpps_devt;
static DEFINE_MUTEX(s_nvpps_lock);
static DEFINE_IDR(s_nvpps_idr);

/*nvpps error codes*/
enum nvpps_error_codes {
	NVPPS_SUCCESS = 0,                         /* No error */
	NVPPS_ERR_TSC_BACKWARDS,                   /* TSC counter went backwards (current < interrupt TSC) */
	NVPPS_ERR_PHC_LATENCY_ADJUST_UNDERFLOW,    /* Underflow when adjusting PHC timestamp for interrupt latency */
	NVPPS_ERR_TSC_CONVERSION_OVERFLOW,         /* Overflow in TSC timestamp conversion to nsec */
	NVPPS_ERR_GET_MONOTONIC,                   /* Failed to get monotonic tsc_ts in gpio isr */
	NVPPS_ERR_SEC_TSC_BACKWARDS,               /* Underflow error when calculating irq_latency wrt secondary tsc */
	NVPPS_ERR_SEC_PHC_LATENCY_ADJUST_UNDERFLOW,/* Underflow when adjusting secondary PHC timestamp for interrupt latency */
	NVPPS_ERR_SEC_INTF_TSC_UNDERFLOW,          /* Underflow detected in secondary interface TSC timestamp */
	NVPPS_ERR_SEC_INTF_PTP_UNDERFLOW,          /* Underflow detected in secondary interface PTP timestamp */
	NVPPS_ERR_PRI_PTP_FAILED,                  /* failed to get PTP_TSC timestamp from primary emac instance,Make sure PTP is running*/
	NVPPS_ERR_SEC_PTP_FAILED,                  /* failed to get PTP_TSC timestamp from secondary emac instance,Make sure PTP is running */
	NVPPS_ERR_BOTH_PRI_SEC_PTP_FAILED,         /* failed to get concurrent timestamp from both primary and secondary interface */
	NVPPS_ERR_BOTH_PRI_SEC_REBASE_FAILED,      /* Both primary and secondary PTP timestamp rebase failed */
	NVPPS_ERR_END,                             /* Sentinel */
};

/* Error messages corresponding to each error code */
static const char * const nvpps_error_messages[] = {
	[NVPPS_SUCCESS] = "Success",
	[NVPPS_ERR_TSC_BACKWARDS] = "Concurrent Primary TSC timestamp less than TSC timestamp from interrupt",
	[NVPPS_ERR_PHC_LATENCY_ADJUST_UNDERFLOW] = "Underflow detected when adjusting PHC timestamp",
	[NVPPS_ERR_TSC_CONVERSION_OVERFLOW] = "Overflow detected in TSC timestamp conversion to nsec",
	[NVPPS_ERR_GET_MONOTONIC] = "Failed to get monotonic tsc_ts in gpio isr",
	[NVPPS_ERR_SEC_TSC_BACKWARDS] = "Concurrent Secondary TSC timestamp less than TSC timestamp from interrupt",
	[NVPPS_ERR_SEC_PHC_LATENCY_ADJUST_UNDERFLOW] = "Underflow detected when adjusting secondary PHC timestamp",
	[NVPPS_ERR_SEC_INTF_TSC_UNDERFLOW] = "Underflow detected in secondary interface TSC timestamp",
	[NVPPS_ERR_SEC_INTF_PTP_UNDERFLOW] = "Underflow detected in secondary interface PTP timestamp",
	[NVPPS_ERR_PRI_PTP_FAILED] = "failed to get PTP_TSC timestamp from primary emac instance,Make sure PTP is running",
	[NVPPS_ERR_SEC_PTP_FAILED] = "failed to get PTP_TSC timestamp from secondary emac instance,Make sure PTP is running",
	[NVPPS_ERR_BOTH_PRI_SEC_PTP_FAILED] = "failed to get concurrent timestamp from both primary and secondary interface",
	[NVPPS_ERR_BOTH_PRI_SEC_REBASE_FAILED] = "Both primary and secondary PTP timestamp rebase failed",
};

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
	raw_spinlock_t		irq_lock;	/* For ISR and timer contexts - protects in_suspend */
	struct mutex		data_lock;	/* For work queue and user contexts - protects timestamp and error data */
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
	bool		print_pri_ptp_failed;	/* Rate-limit primary PTP error messages */
	bool		print_sec_ptp_failed;	/* Rate-limit secondary PTP error messages */
	bool		support_tsc;
	uint32_t	lck_trig_interval;
	struct hte_ts_desc	desc;
	struct gpio_desc	*gpio_in;
	struct soc_dev_data soc_data;
	enum nvpps_error_codes nvpps_err;
	bool		in_suspend;
	struct workqueue_struct *wq;
	struct work_struct work;
	u64		work_irq_tsc;
	bool		work_irq_tsc_error;	/* Error flag for work queue */
	u32		work_evt_mode;		/* Event mode that triggered work */
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

static int safe_sub(u64 a, u64 b, u64 *res)
{
	int err = -1;

	if (b >= a) {
		*res = b - a;
		err = 0;
	} else {
		*res = 0;
	}

	return err;
}

static int validate_and_rebase_ptp_ts(u64 base_tsc,
			       struct ptp_tsc_data *concur_data,
			       u64 *phc)
{
	u64 delta_ts = 0;
	if (safe_sub(base_tsc, concur_data->tsc_ts, &delta_ts)) {
		/* error base_tsc < ptp_tsc_ts.tsc_ts */
		*phc = 0;
		return NVPPS_ERR_TSC_BACKWARDS;
	}

	if (safe_sub(delta_ts, concur_data->ptp_ts, phc)) {
		/* error ptp_ts < (ptp_tsc_ts.tsc_ts - base_tsc) */
		/* *phc = 0; phc inited to 0 inside safe_sub() */
		return NVPPS_ERR_PHC_LATENCY_ADJUST_UNDERFLOW;
	}

	/* *phc >= 0; phc inited to >= 0 inside safe_sub() */
	return 0;
}

/*
 * Report the PPS event
 */
static void nvpps_get_ts(struct nvpps_device_data *pdev_data, u64 irq_tsc, u32 evt_mode)
{
	u64		tsc = 0;
	u64		phc = 0;
	u64		secondary_phc = 0;
	struct ptp_tsc_data ptp_tsc_ts = {0}, sec_ptp_tsc_ts = {0};
	int32_t err;
	enum nvpps_error_codes local_nvpps_err = NVPPS_SUCCESS;
	bool local_pri_ptp_failed = false;
	bool local_sec_ptp_failed = false;
	bool update_values = 1;

	/* If irq_tsc is != 0, convert value from tick to nsec */
	if (irq_tsc) {
		if (unlikely(check_mul_overflow(irq_tsc, pdev_data->tsc_res_ns, &irq_tsc))) {
			/* Invalid base to rebase or align ptp ts's to.
			 * Set error & return old known good values(Don't update shared data)
			 */
			update_values = 0;
			local_pri_ptp_failed = true;
			local_sec_ptp_failed = true;
			/* Since local_nvpps_err is initialized to NVPPS_SUCCESS
			 * we are not overwriting the error
			 */
			local_nvpps_err = NVPPS_ERR_TSC_CONVERSION_OVERFLOW;
			goto set_locked_values;
		}
	}

	/* get PTP_TSC concurrent timestamp from MAC driver */
	err = nvpps_get_ptp_tsc_concurrent_ts(pdev_data, pdev_data->pri_emac_node, &ptp_tsc_ts);
	/* if API returns failure to read concurrent TS OR
	 * if the returned values are zero, Mark Failed to read concurrent TSs
	 * check only tsc == 0 because if API is successsful then
	 * certainly tsc is != 0 but ptp can be zero(but most likely not)
	 */
	if ((err != 0) ||
		(ptp_tsc_ts.tsc_ts == 0)) {
		local_pri_ptp_failed = true;
		/* local_nvpps_err here is NVPPS_SUCCESS because,
		 * if error happens at previous stage we jump to the end.
		 * Not overwriting any error
		 */
		local_nvpps_err = NVPPS_ERR_PRI_PTP_FAILED;
		ptp_tsc_ts.ptp_ts = 0;
		ptp_tsc_ts.tsc_ts = 0;
	} else {
		local_pri_ptp_failed = false;
	}

	/* get PTP_TSC concurrent timestamp from secondary MAC driver */
	if (pdev_data->sec_emac_node != NULL) {
		err = nvpps_get_ptp_tsc_concurrent_ts(pdev_data, pdev_data->sec_emac_node, &sec_ptp_tsc_ts);
		/* if API returns failure to read concurrent ts OR
		 * if the returned values are zero, Mark Failed to read concurrent TSs
		 * check only tsc == 0 because if API is successsful then
		 * certainly tsc is != 0 but ptp can be zero(but most likely not)
		 */
		if ((err != 0) ||
			(sec_ptp_tsc_ts.tsc_ts == 0)) {
			local_sec_ptp_failed = true;
			if (local_nvpps_err == NVPPS_SUCCESS)
				local_nvpps_err = NVPPS_ERR_SEC_PTP_FAILED;
			else /* (local_nvpps_err == NVPPS_ERR_PRI_PTP_FAILED) */
				local_nvpps_err = NVPPS_ERR_BOTH_PRI_SEC_PTP_FAILED;
			sec_ptp_tsc_ts.ptp_ts = 0;
			sec_ptp_tsc_ts.tsc_ts = 0;
		} else {
			local_sec_ptp_failed = false;
		}
	}

	/* Rule 3: If both tsc_ts values are zero, set all to zero */
	/* if primary concurrent ptp_tsc read failed */
	if ((local_pri_ptp_failed == true) &&
		/* if optional secondary interface is not providied */
		((pdev_data->sec_emac_node == NULL) ||
		/* if optional secondary interface is provided and
		 * concurrent ptp_tsc read failed on secondary interface
		 */
		 (local_sec_ptp_failed == true))) {
			/* Return all tsc, phc, secondary_phc = 0*/
			tsc = 0;
			phc = 0;
			secondary_phc = 0;
			/* local_nvpps_err is already initialized hence no need to update */
			goto set_locked_values;
	}

	if (irq_tsc)
		tsc = irq_tsc;
	else if (local_pri_ptp_failed == false)
		tsc = ptp_tsc_ts.tsc_ts;
	else if ((pdev_data->sec_emac_node) && (local_sec_ptp_failed == false))
		tsc = sec_ptp_tsc_ts.tsc_ts;
	else
		{} /* Not possible because (pri failed && secondary_phc not given) case
		already has goto set_locked_values in previous section*/


	if (local_pri_ptp_failed == false) {
		err = validate_and_rebase_ptp_ts(tsc, &ptp_tsc_ts, &phc);
		/* primary ptp ts rebase failed */
		/* phc = 0; initied to 0 in case of error inside validate_and_rebase_ptp_ts() */
		if (err) {
			phc = 0;
			local_pri_ptp_failed = true;
			if (local_nvpps_err == NVPPS_SUCCESS) {
				/* Use error codes as-is for primary interface */
				local_nvpps_err = err;
			}
		}
	}

	if ((pdev_data->sec_emac_node) && (local_sec_ptp_failed == false)) {
		err = validate_and_rebase_ptp_ts(tsc, &sec_ptp_tsc_ts, &secondary_phc);
		/* Secondary ptp tsc rebase failed */
		/* secondary_phc = 0; initied to 0 in case of error inside validate_and_rebase_ptp_ts() */
		if (err) {
			secondary_phc = 0;
			local_sec_ptp_failed = true;
			if (local_nvpps_err == NVPPS_SUCCESS) {
				/* Map primary error codes to secondary for secondary interface */
				if (err == NVPPS_ERR_TSC_BACKWARDS)
					local_nvpps_err = NVPPS_ERR_SEC_TSC_BACKWARDS;
				else if (err == NVPPS_ERR_PHC_LATENCY_ADJUST_UNDERFLOW)
					local_nvpps_err = NVPPS_ERR_SEC_PHC_LATENCY_ADJUST_UNDERFLOW;
				else
					local_nvpps_err = err;
			} else if ((local_nvpps_err == NVPPS_ERR_TSC_BACKWARDS) ||
				   (local_nvpps_err == NVPPS_ERR_PHC_LATENCY_ADJUST_UNDERFLOW)) {
				local_nvpps_err = NVPPS_ERR_BOTH_PRI_SEC_REBASE_FAILED;
			}
		}
	}

	if ((phc == 0) && (secondary_phc == 0)) {
		update_values = 0;
	}

	/* At this point, We should have valid tsc, phc, secondary phc */
	/* take lock and Update shared data */
set_locked_values:

	/* Use mutex for protecting timestamp and error data in work queue context */
	mutex_lock(&pdev_data->data_lock);

	/* Set PTP failure status from local variables */
	pdev_data->pri_ptp_failed = local_pri_ptp_failed;
	pdev_data->sec_ptp_failed = local_sec_ptp_failed;

	/* Update nvpps_err under lock protection */
	if (update_values == 1) {
		/* update event id */
		pdev_data->pps_event_id_valid = true;
		pdev_data->pps_event_id++;

		pdev_data->phc = phc;
		pdev_data->secondary_phc = secondary_phc;
		pdev_data->tsc = tsc;
		pdev_data->actual_evt_mode = evt_mode;
	}
	/* Update print flags and nvpps_err only in case if it was a new error*/
	if (local_nvpps_err != pdev_data->nvpps_err) {
		pdev_data->nvpps_err = local_nvpps_err;
		if (pdev_data->pri_ptp_failed) {
			pdev_data->print_pri_ptp_failed = true;
		}
		if (pdev_data->sec_ptp_failed) {
			pdev_data->print_sec_ptp_failed = true;
		}
	}
	if ((local_pri_ptp_failed == false) && (local_sec_ptp_failed == false)) {
		pdev_data->nvpps_err = NVPPS_SUCCESS;
	}

	mutex_unlock(&pdev_data->data_lock);

	/* event notification */
	wake_up_interruptible(&pdev_data->pps_event_queue);
	kill_fasync(&pdev_data->pps_event_async_queue, SIGIO, POLL_IN);
}

/*
 * Work queue handler function
 */
static void nvpps_work_handler(struct work_struct *work)
{
	struct nvpps_device_data *pdev_data = container_of(work, struct nvpps_device_data, work);
	unsigned long	flags = 0;
	u64 irq_tsc;
	u32 evt_mode;
	bool tsc_error;

	/* Protect work_* fields with spinlock to synchronize access between interrupt callback and work handler */
	raw_spin_lock_irqsave(&pdev_data->irq_lock, flags);
	irq_tsc = pdev_data->work_irq_tsc;
	evt_mode = pdev_data->work_evt_mode;
	tsc_error = pdev_data->work_irq_tsc_error;
	raw_spin_unlock_irqrestore(&pdev_data->irq_lock, flags);

	/* Check if this work was queued for error handling */
	if (tsc_error) {
		/* Error path: Handle TSC read failure in process context with mutex */
		mutex_lock(&pdev_data->data_lock);

		if (pdev_data->nvpps_err != NVPPS_ERR_GET_MONOTONIC) {
			pdev_data->nvpps_err = NVPPS_ERR_GET_MONOTONIC;
			pdev_data->print_pri_ptp_failed = true;
			pdev_data->pri_ptp_failed = true;
			/* Use old TS values in error path */
		}

		mutex_unlock(&pdev_data->data_lock);
	} else {
		/* Normal path: Call nvpps_get_ts with the stored irq_tsc value and event mode */
		nvpps_get_ts(pdev_data, irq_tsc, evt_mode);
	}
}

static irqreturn_t nvpps_gpio_isr(int irq, void *data)
{
	struct nvpps_device_data        *pdev_data = (struct nvpps_device_data *)data;
	uint64_t tsc_ts = 0;
	int32_t ret;
	unsigned long flags;

	/* If the current mode is TIMER mode, ignore the interrupt.
	 * If HTE is not enabled, use TSC and process the interrupt.
	 * If HTE is enabled, ignore the interrupt and process it in HTE callback
	 */
	if (!pdev_data->timer_inited) {
		if (!(pdev_data->use_gpio_int_timestamp)) {
			ret = pdev_data->soc_data.ops->get_monotonic_tsc_ts_fn(&pdev_data->soc_data, &tsc_ts);

			raw_spin_lock_irqsave(&pdev_data->irq_lock, flags);
			if (ret == 0) {
				pdev_data->work_irq_tsc = tsc_ts;
				pdev_data->work_irq_tsc_error = false;
			} else {
				pdev_data->work_irq_tsc = 0;
				pdev_data->work_irq_tsc_error = true;
			}
			pdev_data->work_evt_mode = NVPPS_MODE_GPIO;
			raw_spin_unlock_irqrestore(&pdev_data->irq_lock, flags);

			/* Schedule work for bottom half processing */
			queue_work(pdev_data->wq, &pdev_data->work);
		}
	}

	return IRQ_HANDLED;
}

static void tsc_timer_callback(struct timer_list *t)
{

	unsigned long timeout = 0;
	unsigned long	flags;
	struct nvpps_device_data *pdev_data =
#if defined(timer_container_of) /* Linux v6.16 */
		timer_container_of(pdev_data, t, tsc_timer);
#else
		from_timer(pdev_data, t, tsc_timer);
#endif

	/* TODO: Since ptp_tsc_get_is_locked_fn() & ptp_tsc_synchronize_fn() are platform specific,
	 * Check if these can be moved out of interrupt context to process context.
	 */
	if (pdev_data->soc_data.ops->ptp_tsc_get_is_locked_fn) {
		/* check and trigger sync if PTP-TSC is unlocked */
		if (pdev_data->soc_data.ops->ptp_tsc_get_is_locked_fn(&(pdev_data->soc_data)) == false) {
			if (pdev_data->soc_data.ops->ptp_tsc_synchronize_fn) {
				pdev_data->soc_data.ops->ptp_tsc_synchronize_fn(&(pdev_data->soc_data));
			}
		}
	}

	/* Use irq_lock to protect in_suspend flag access from timer context */
	raw_spin_lock_irqsave(&pdev_data->irq_lock, flags);
	if (!pdev_data->in_suspend) {
		/* set the next expire time */
		if (unlikely(check_add_overflow(jiffies,
				msecs_to_jiffies((TSC_POLL_TIMER /
				pdev_data->soc_data.pps_freq) *
				pdev_data->lck_trig_interval), &timeout)))
			dev_err(pdev_data->dev, "%s: Overflow detected during addition\n", __func__);

		mod_timer(&pdev_data->tsc_timer, timeout);
	}
	raw_spin_unlock_irqrestore(&pdev_data->irq_lock, flags);
}


static void nvpps_timer_callback(struct timer_list *t)
{
	unsigned long	flags;
	uint64_t tsc_ts = 0;
	struct nvpps_device_data *pdev_data =
#if defined(timer_container_of) /* Linux v6.16 */
		timer_container_of(pdev_data, t, timer);
#else
		from_timer(pdev_data, t, timer);
#endif
	int32_t ret = pdev_data->soc_data.ops->get_monotonic_tsc_ts_fn(&pdev_data->soc_data, &tsc_ts);

	raw_spin_lock_irqsave(&pdev_data->irq_lock, flags);
	if (ret == 0) {
		pdev_data->work_irq_tsc = tsc_ts;
		pdev_data->work_irq_tsc_error = false;
	} else {
		pdev_data->work_irq_tsc = 0;
		pdev_data->work_irq_tsc_error = true;
	}
	pdev_data->work_evt_mode = NVPPS_MODE_TIMER;
	raw_spin_unlock_irqrestore(&pdev_data->irq_lock, flags);

	/* Queue work outside spinlock to minimize critical section */
	queue_work(pdev_data->wq, &pdev_data->work);

	/* Use irq_lock to protect in_suspend flags from timer context */
	raw_spin_lock_irqsave(&pdev_data->irq_lock, flags);
	if (!pdev_data->in_suspend) {
		/* set the next expire time */
		if (pdev_data->timer_inited) {
			unsigned long timeout = 0;

			if (unlikely(check_add_overflow(jiffies,
											msecs_to_jiffies(pdev_data->ts_capture_interval_ms),
											&timeout)))
				dev_err(pdev_data->dev,
						"%s: Overflow detected during addition\n", __func__);

			mod_timer(&pdev_data->timer, timeout);
		}
	}
	raw_spin_unlock_irqrestore(&pdev_data->irq_lock, flags);
}

/* spawn timer to monitor TSC to PTP lock and re-activate
 locking process if its not locked in the handler */
static int32_t set_mode_tsc(struct nvpps_device_data *pdev_data)
{
	int32_t ret = -1;
	unsigned long timeout = 0;

	timer_setup(&pdev_data->tsc_timer,
			tsc_timer_callback,
			0);

	if (unlikely(check_add_overflow(jiffies, msecs_to_jiffies(1000), &timeout))) {
		dev_err(pdev_data->dev, "Overflow detected during addition in set_mode_tsc\n");
		goto err;
	}
	mod_timer(&pdev_data->tsc_timer, timeout);

	ret = 0;
err:
	return ret;
}

/*
 * Store hardware timestamp
 */
static enum hte_return process_hw_ts(struct hte_ts_data *ts, void *p)
{
	struct nvpps_device_data *pdev_data = (struct nvpps_device_data *)p;
	unsigned long flags;

	/* If an callback is generated then check
	 * current mode in use. Ignore the callback
	 * if current mode is TIMER mode
	 */
	if (!pdev_data->timer_inited) {
		/* Protect work queue data setup with spinlock to prevent race with work handler */
		raw_spin_lock_irqsave(&pdev_data->irq_lock, flags);
		pdev_data->work_irq_tsc = ts->tsc;
		pdev_data->work_evt_mode = NVPPS_MODE_GPIO;
		pdev_data->work_irq_tsc_error = false;
		raw_spin_unlock_irqrestore(&pdev_data->irq_lock, flags);

		/* Schedule work queue for bottom half processing */
		queue_work(pdev_data->wq, &pdev_data->work);
	}

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
#if defined(NV_TIMER_DELETE_PRESENT) /* Linux v6.15 */
						timer_delete_sync(&pdev_data->timer);
#else
						del_timer_sync(&pdev_data->timer);
#endif
						/* Cancel any previously queued work */
						if (pdev_data->wq)
							cancel_work_sync(&pdev_data->work);

						pdev_data->timer_inited = false;
					}

					if (!pdev_data->irq_registered) {
						/* register & enable IRQ handler */
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

						/* enable GPIO */
						enable_irq(pdev_data->irq);
					}
				} else {
					dev_err(pdev_data->dev, "unable to switch mode. Only timer mode is supported\n");
					err = -EINVAL;
				}
				break;

			case NVPPS_MODE_TIMER:

				/* disable GPIO interrupt & cancel pervious work */
				if (pdev_data->irq_registered) {
					disable_irq(pdev_data->irq);

					/* Cancel any previously queued work */
					if (pdev_data->wq)
						cancel_work_sync(&pdev_data->work);
				}

				/* If TIMER mode is requested and not initialized
				 * already then initialize it
				 */
				if (!pdev_data->timer_inited) {
					unsigned long timeout = 0;
					timer_setup(&pdev_data->timer,
							nvpps_timer_callback,
							0);
					pdev_data->timer_inited = true;
					if (unlikely(check_add_overflow(jiffies, msecs_to_jiffies(pdev_data->ts_capture_interval_ms), &timeout))) {
						dev_err(pdev_data->dev, "Overflow detected during addition when setting timer mode\n");
						return -ERANGE;
					}
					/* setup timer interval to 1000 msecs */
					mod_timer(&pdev_data->timer, timeout);
				}
				break;

			default:
				return -EINVAL;
		}
	}
	return err;
}


/**
 * @defgroup timesync_user_api_group NvPPS user APIs/IOCTL
 * @{
 */

/* Character device stuff */

/**
 * @brief This function handles the poll system call on nvpps device file descriptor.
 *        This function registers the user process with a driver-specific wait queue and
 *        return bitmask indicating readiness of the device for I/O operations.
 *
 * @param[in]      file A pointer to the struct file object representing the open device file.
 * @param[in]      wait A structure used to register the current process onto one or more waiting queues
 *
 * @retval
 *                 - Kernel provided bitmask(i.e POLLIN | POLLRDNORM) indicating the device is readable on success
 *
 * @pre
 *                 - NvPPS driver should be initialized and active
 *                 - The device file should be opened
 *
 * @post           none
 *
 * @usage
 * - Allowed context for the IOCTL call
 *   - Signal handler: No
 *   - Thread-safe: No
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 *
 */
static __poll_t nvpps_poll(struct file *file, poll_table *wait)
{
	struct nvpps_file_data		*pfile_data = (struct nvpps_file_data *)file->private_data;
	struct nvpps_device_data	*pdev_data = pfile_data->pdev_data;
	__poll_t ret			= 0;
	poll_wait(file, &pdev_data->pps_event_queue, wait);

	/* Use mutex to protect timestamp data in user context */
	mutex_lock(&pdev_data->data_lock);
	if (pdev_data->pps_event_id_valid &&
		(pfile_data->pps_event_id_rd != pdev_data->pps_event_id)) {
		ret = POLLIN | POLLRDNORM;
	}
	mutex_unlock(&pdev_data->data_lock);

	return ret;
}


/**
 * @brief This function is used to enable or disable asynchronous notification for a device
 *        which is used for notifying the userspace process of device events without blocking
 *
 * @param[in]      fd File descriptor to the open nvpps device file
 * @param[in]      file The struct file pointer for the open file
 * @param[in]      on Indicates whether to enable or disable asynchronous notification
 *
 * @retval
 *                 - < 0 to indicate error
 *                 - = 0 if no change was made to list of processes
 *                 - > 0 on successfully adding process to notification list
 *
 * @pre
 *                 - NvPPS driver should be initialized and active
 *                 - The device file should be opened
 *
 * @post           none
 *
 * @usage
 * - Allowed context for the IOCTL call
 *   - Signal handler: No
 *   - Thread-safe: No
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 *
 */
static int nvpps_fasync(int fd, struct file *file, int on)
{
	struct nvpps_file_data		*pfile_data = (struct nvpps_file_data *)file->private_data;
	struct nvpps_device_data	*pdev_data = pfile_data->pdev_data;

	return fasync_helper(fd, file, on, &pdev_data->pps_event_async_queue);
}


/**
 * @brief Handles IOCTL cmds/requests from clients of NvPPS driver
 *        This function does following initialization:
 *        This function processes IOCTL commands for:
 *         - Getting version info
 *         - Getting current operating parameters
 *         - Setting operating parameters
 *         - Reading Kernel time & PTP time from primary interface
 *         - Reading concurrent PTP-TSC timestamp from Primary & Secondary interfaces
 *
 * @param[in]      file pointer to nvpps device node(/dev/nvpps) file
 * @param[in]      cmd unsigned int variable which holds the IOCTL cmd
 * @param[in]      arg unsigned long variable which holds memory addr for data exchange
 *
 * @return
 *          - 0 on success
 *          - An IOCTL specific error code otherwise, refer IOCTL cmd macro section
 *
 * @pre
 *          - NvPPS driver should be initialized and active
 *          - The Tegra network driver should be initialized & operational
 *          - PTP client & master daemon should be started on primary & secondary n/w interfaces
 *
 * @post    none
 *
 * @usage
 * - Allowed context for the IOCTL call
 *   - Signal handler: No
 *   - Thread-safe: No
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 *
 */
static long nvpps_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct nvpps_file_data		*pfile_data = (struct nvpps_file_data *)file->private_data;
	struct nvpps_device_data	*pdev_data = pfile_data->pdev_data;
	struct nvpps_params		params = {0};
	void __user			*uarg = (void __user *)arg;
	int				err = -1;

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

			/* Validate tsc_mode parameter */
			if ((params.tsc_mode != NVPPS_TSC_NSEC) && (params.tsc_mode != NVPPS_TSC_COUNTER)) {
				dev_err(pdev_data->dev, "Invalid tsc_mode %u. Valid values are %u (NVPPS_TSC_NSEC) and %u (NVPPS_TSC_COUNTER)\n",
					params.tsc_mode, NVPPS_TSC_NSEC, NVPPS_TSC_COUNTER);
				return -EINVAL;
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
			bool		local_pri_ptp_failed = false;
			bool		local_sec_ptp_failed = false;
			bool		local_print_pri = false;
			bool		local_print_sec = false;
			enum		nvpps_error_codes local_nvpps_err = NVPPS_SUCCESS;
			dev_dbg(pdev_data->dev, "NVPPS_GETEVENT\n");

			/* Return the fetched timestamp */
			mutex_lock(&pdev_data->data_lock);
			local_pri_ptp_failed = pdev_data->pri_ptp_failed;
			local_sec_ptp_failed = pdev_data->sec_ptp_failed;
			local_nvpps_err = pdev_data->nvpps_err;
			pfile_data->pps_event_id_rd = pdev_data->pps_event_id;
			time_event.evt_nb = pdev_data->pps_event_id;
			time_event.tsc = pdev_data->tsc;
			time_event.ptp = pdev_data->phc;
			time_event.secondary_ptp = pdev_data->secondary_phc;
			/* irq_latency is always 0 because we are rebasing both phc values to tsc TS from ISR ctx,
			 * Keeping this field for backward compatibility
			 */
			time_event.irq_latency = 0;
			time_event.evt_mode = pdev_data->actual_evt_mode;

			/* Check and clear print flags under lock */
			local_print_pri = pdev_data->print_pri_ptp_failed;
			local_print_sec = pdev_data->print_sec_ptp_failed;
			if (local_pri_ptp_failed && local_print_pri) {
				pdev_data->print_pri_ptp_failed = false;
			}
			if (local_sec_ptp_failed && local_print_sec) {
				pdev_data->print_sec_ptp_failed = false;
			}
			mutex_unlock(&pdev_data->data_lock);

			/* Print error messages outside lock (I/O can be slow) */
			if (local_pri_ptp_failed && local_print_pri) {
				if ((local_nvpps_err >= NVPPS_SUCCESS) && (local_nvpps_err < NVPPS_ERR_END))
					dev_warn_ratelimited(pdev_data->dev, "Error: %s\n", nvpps_error_messages[local_nvpps_err]);
			}
			if (local_sec_ptp_failed && local_print_sec) {
				if ((local_nvpps_err >= NVPPS_SUCCESS) && (local_nvpps_err < NVPPS_ERR_END))
					dev_warn_ratelimited(pdev_data->dev, "Error: %s\n", nvpps_error_messages[local_nvpps_err]);
			}

			if (pdev_data->tsc_mode == NVPPS_TSC_NSEC &&
				       !pdev_data->use_gpio_int_timestamp) {
				if (check_mul_overflow(time_event.tsc, pdev_data->tsc_res_ns, &(time_event.tsc))) {
					dev_err(pdev_data->dev, "Overflow detected during multiplication of time_event.tsc and pdev_data->tsc_res_ns\n");
					time_event.tsc = 0;
					time_event.ptp = 0;
					time_event.secondary_ptp = 0;
				}
			}
			time_event.tsc_res_ns = pdev_data->tsc_res_ns;
			/* return the mode when the time event actually occured */
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

			/* TODO : Check if the mutex ts_lock can be removed, since its only used in this IOCTL call */
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
					goto unlock_and_return;
				}

				/* convert TSC counter value to nsec value */
				if (check_mul_overflow(tsc_ts, pdev_data->tsc_res_ns, &tsc_ts)) {
					dev_err(pdev_data->dev, "Overflow detected during multiplication of tsc_ts and pdev_data->tsc_res_ns\n");
					err = -ERANGE;
					goto unlock_and_return;
				}

				/* split TSC TS in seconds & nanoseconds */
				time_stamp.kernel_ts.tv_sec = tsc_ts / _NANO_SECS;
				if ((long)tsc_ts < (long)(time_stamp.kernel_ts.tv_sec * _NANO_SECS)) {
					dev_err(pdev_data->dev, "Error: tsc_ts is less than (time_stamp.kernel_ts.tv_sec * _NANO_SECS)");
					err = -ERANGE;
					goto unlock_and_return;
				}
				time_stamp.kernel_ts.tv_nsec = (long)tsc_ts - (long)(time_stamp.kernel_ts.tv_sec * _NANO_SECS);

				break;

			default:
				dev_err(pdev_data->dev, "ioctl: Unsupported clockid\n");
				err = -EINVAL;
				goto unlock_and_return;
			}

			err = pdev_data->soc_data.ops->get_ptp_ts_ns_fn(pdev_data->pri_emac_node, &ns);
			if (err) {
				dev_err(pdev_data->dev, "HW PTP not running, err(%d)\n", err);
				goto unlock_and_return;
			}

unlock_and_return:
			mutex_unlock(&pdev_data->ts_lock);
			if (err) {
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



/**
 * @brief Handle user request to open NvPPS device node
 *        This function handles the open system call on NvPPS device node
 *         - init/alloc memory and initialize file data
 *         - update the ref count on the file
 *
 * @param[in]      inode A pointer to inode of the device file corresponding to nvpps device node file
 * @param[in]      file A pointer to a newly created kernel struct file object
 *
 * @retval
 *                 - 0 on success and
 *                 - -ENOMEM if memory alloc fails
 *
 * @pre
 *                 - NvPPS driver should be initialized and active
 *
 * @post
 *                 - ioctl cmds can be requested on open file
 *
 * @usage
 * - Allowed context for the IOCTL call
 *   - Signal handler: No
 *   - Thread-safe: No
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 *
 */
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



/**
 * @brief Handle user request to close NvPPS device node
 *        This function handles the close system call on NvPPS device node
 *        free's alloc'ed memory and decrement ref count on the file.
 *
 * @param[in]      inode A pointer to inode of the device file corresponding to nvpps device node file
 * @param[in]      file A pointer to a struct file object representing the open file instance
 *
 * @retval
 *                 - 0 on success
 *
 * @pre
 *                 - NvPPS dev node file should be opened
 *
 * @post           none
 *
 * @usage
 * - Allowed context for the IOCTL call
 *   - Signal handler: No
 *   - Thread-safe: No
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - API Group
 *   - Init: No
 *   - Runtime: No
 *   - De-Init: Yes
 *
 */
static int nvpps_close(struct inode *inode, struct file *file)
{
	struct nvpps_device_data	*pdev_data = container_of(inode->i_cdev, struct nvpps_device_data, cdev);

	if (file->private_data) {
		kfree(file->private_data);
	}
	kobject_put(&pdev_data->dev->kobj);
	return 0;
}

/** @} */


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
	int err = -1;
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
	struct resource *tsc_mem = NULL;
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


/**
 * @defgroup timesync_user_api_group NvPPS user APIs/IOCTL
 * @{
 */

/**
 * @brief Probe function to initialize NvPPS driver
 *        This function does following initialization:
 *         - Init platform specific data such as ops, register addr etc
 *         - Init primary & secondary PTP interfaces dev nodes
 *         - Init/Parse & validate Platform specific and independent dt properties
 *         - Setup character device
 *         - Setup driver operating mode as configured by user from DT node
 *         - Configure TSC IP registers for synchronization with PTP MAC interface
 *         - Setup PTP-TSC lock monitoring thread with fixed interval based on user config
 *
 * @param[in]      pdev platform device structure representing driver
 *
 * @retval
 *                 - 0 on success and
 *                 - Linux kernel defined error code otherwise
 *
 * @pre            none
 *
 * @post
 *                 - Tegra Network driver and PTP Client & Master daemons needs to be initialized
 *
 * @usage
 * - Allowed context for the API call
 *   - Signal handler: No
 *   - Thread-safe: No
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - API Group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 *
 */
static int nvpps_probe(struct platform_device *pdev)
{
	struct nvpps_device_data	*pdev_data;
	struct device_node		*np = pdev->dev.of_node;
	dev_t				devt = {0};
	int				err = -1;
	const struct chip_ops    *cdata = NULL;
	int				index = 0;
	uint32_t			initial_mode = 0;
	unsigned long	flags = 0;
	struct resource res = {0};

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
	if (NULL == cdata) {
		dev_err(&pdev->dev, "no match data found\n");
		return -EINVAL;
	}
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

	pdev_data->sec_emac_node = of_parse_phandle(np, "sec-emac", 0);
	if (pdev_data->sec_emac_node == NULL) {
		dev_info(&pdev->dev, "sec-emac node not found\n");
	} else {
		if (pdev_data->pri_emac_node == pdev_data->sec_emac_node) {
			dev_err(&pdev->dev, "sec-emac is same as primary-emac node. Undefine sec-emac if not needed\n");
			return -EINVAL;
		}
		dev_info(&pdev->dev, "secondary-emac : %s", pdev_data->sec_emac_node->full_name);
	}

	init_waitqueue_head(&pdev_data->pps_event_queue);
	raw_spin_lock_init(&pdev_data->irq_lock);
	mutex_init(&pdev_data->data_lock);
	mutex_init(&pdev_data->ts_lock);
	pdev_data->pdev = pdev;
	pdev_data->evt_mode = 0;
	pdev_data->tsc_mode = NVPPS_TSC_NSEC;

	/* Initialize print flags to enable initial error messages */
	pdev_data->print_pri_ptp_failed = true;
	pdev_data->print_sec_ptp_failed = true;

	/* Initialize work queue */
	pdev_data->wq = alloc_workqueue("nvpps_wq", WQ_MEM_RECLAIM | WQ_HIGHPRI | WQ_FREEZABLE, 1);
	if (!pdev_data->wq) {
		dev_err(&pdev->dev, "Failed to allocate workqueue\n");
		return -ENOMEM;
	}
	INIT_WORK(&pdev_data->work, nvpps_work_handler);
	raw_spin_lock_irqsave(&pdev_data->irq_lock, flags);
	pdev_data->in_suspend = false;
	pdev_data->work_irq_tsc = 0;
	pdev_data->work_irq_tsc_error = false;
	pdev_data->work_evt_mode = NVPPS_MODE_TIMER;
	raw_spin_unlock_irqrestore(&pdev_data->irq_lock, flags);


	/* Get ts-capture-interval from device tree, default to 1000ms if not specified */
	err = of_property_read_u32(np, "ts-capture-interval", &pdev_data->ts_capture_interval_ms);
	if (err < 0) {
		pdev_data->ts_capture_interval_ms = TS_CAPTURE_INTERVAL_MAX_MS;
		err = 0;
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

	/* Get initial operating mode from device tree */
	err = of_property_read_u32(np, "nvpps-event-mode-init", &initial_mode);
	if (err < 0) {
		dev_err(&pdev->dev, "nvpps-event-mode-init property not specified err(%d)\n", err);
		return err;
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

	mutex_lock(&pdev_data->data_lock);
	pdev_data->nvpps_err = NVPPS_SUCCESS;
	mutex_unlock(&pdev_data->data_lock);

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

	/* validate if required function ptrs are initialized irrespective of TSC support */
	if ((pdev_data->soc_data.ops->get_tsc_res_ns_fn == NULL) ||
	    (pdev_data->soc_data.ops->get_monotonic_tsc_ts_fn == NULL) ||
	    (pdev_data->soc_data.ops->get_ptp_ts_ns_fn == NULL)) {
		dev_err(&pdev->dev, "Timestamp & resolution get functions are not defined\n");
		err = -EINVAL;
		goto error_ret2;
	}

	nvpps_get_ptp_ts_ns_fn = pdev_data->soc_data.ops->get_ptp_ts_ns_fn;

	if (pdev_data->soc_data.ops->get_tsc_res_ns_fn(&pdev_data->soc_data, &pdev_data->tsc_res_ns) != 0) {
		dev_err(&pdev->dev, "failed to get tsc resolution");
		err = -EINVAL;
		goto error_ret2;
	}
	dev_info(&pdev->dev, "tsc_res_ns(%llu)\n", pdev_data->tsc_res_ns);

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

		/* validate if required function ptrs are initialized when ptp tsc sync is enabled */
		if ((pdev_data->soc_data.ops->ptp_tsc_sync_cfg_fn == NULL) ||
			(pdev_data->soc_data.ops->ptp_tsc_synchronize_fn == NULL) ||
			(pdev_data->soc_data.ops->ptp_tsc_get_is_locked_fn == NULL)) {
			dev_err(&pdev->dev, "HW TSC init & timestamping functions not defined\n");
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
			/* setup timer to monitor & trigger PTP-TSC sync */
			if (set_mode_tsc(pdev_data) != 0) {
				err = -EINVAL;
				goto error_ret2;
			}
		}
	} else {
		dev_info(&pdev->dev, "ptp tsc sync is disabled\n");
	}

	return 0;

error_ret2:
	if (pdev_data->timer_inited) {
#if defined(NV_TIMER_DELETE_PRESENT) /* Linux v6.15 */
		timer_delete_sync(&pdev_data->timer);
#else
		del_timer_sync(&pdev_data->timer);
#endif
	}

	device_destroy(s_nvpps_class, pdev_data->dev->devt);
	class_destroy(s_nvpps_class);

error_ret:
	of_node_put(pdev_data->pri_emac_node);
	of_node_put(pdev_data->sec_emac_node);
	cdev_del(&pdev_data->cdev);
	mutex_lock(&s_nvpps_lock);
	idr_remove(&s_nvpps_idr, pdev_data->id);
	mutex_unlock(&s_nvpps_lock);

	/* Cleanup workqueue */
	if (pdev_data->wq) {
		cancel_work_sync(&pdev_data->work);
		destroy_workqueue(pdev_data->wq);
	}

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

		/* Cleanup workqueue */
		if (pdev_data->wq) {
			cancel_work_sync(&pdev_data->work);
			destroy_workqueue(pdev_data->wq);
		}

		of_node_put(pdev_data->pri_emac_node);
		of_node_put(pdev_data->sec_emac_node);
	}


	class_unregister(s_nvpps_class);
	class_destroy(s_nvpps_class);
	unregister_chrdev_region(s_nvpps_devt, MAX_NVPPS_SOURCES);
	return 0;
}


#ifdef CONFIG_PM
/**
 * @brief Suspend function to support PM in NvPPS driver
 *        This function does following :
 *         - Set in_suspend boolean var to true to indicate initiation of request to suspend nvpps driver
 *         - Delete timer setup for monitoring PTP-TSC synchronization
 *         - Disable 1PPS GPIO pin irq if registered
 *         - Delete timer setup for capturing concurrent PTP-TSC timestamps
 *         - Call platform specific suspend API if registered to suspend the HW IP
 *
 * @param[in]      pdev platform device structure representing driver
 * @param[in]      state representing a power management message
 *
 * @retval
 *                 - 0 on success
 *
 * @pre
 *                 - NvPPS driver must be initialized and active
 *
 * @post           none
 *
 * @usage
 * - Allowed context for the API call
 *   - Signal handler: No
 *   - Thread-safe: No
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 *
 */
static int nvpps_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct nvpps_device_data *pdev_data = platform_get_drvdata(pdev);
	unsigned long	flags;
	int32_t ret = 0;

	/* Use irq_lock to set in_suspend flag visible to timer contexts */
	raw_spin_lock_irqsave(&pdev_data->irq_lock, flags);
	pdev_data->in_suspend = true;
	raw_spin_unlock_irqrestore(&pdev_data->irq_lock, flags);

	/* delete ptp_tsc monitoring timer */
	if (pdev_data->support_tsc) {
#if defined(NV_TIMER_DELETE_PRESENT) /* Linux v6.15 */
		timer_delete_sync(&pdev_data->tsc_timer);
#else
		del_timer_sync(&pdev_data->tsc_timer);
#endif
	}

	/* disable GPIO interrupt */
	if (pdev_data->irq_registered)
		disable_irq(pdev_data->irq);

	/* delete timestamp collection timer thread */
	if (pdev_data->timer_inited) {
#if defined(NV_TIMER_DELETE_PRESENT) /* Linux v6.15 */
		timer_delete_sync(&pdev_data->timer);
#else
		del_timer_sync(&pdev_data->timer);
#endif
	}

	/* Cancel any pending work (mode independent) items instead of waiting for completion */
	if (pdev_data->wq)
		cancel_work_sync(&pdev_data->work);

	/* Suspend TSC HW */
	if ((pdev_data->support_tsc) &&
		(pdev_data->soc_data.ops->ptp_tsc_suspend_sync_fn)) {
		ret = pdev_data->soc_data.ops->ptp_tsc_suspend_sync_fn(&(pdev_data->soc_data));
		if (ret != 0) {
			dev_err(pdev_data->dev, "%s: Failed to suspend HW PTP-TSC sync, ret %d\n", __func__, ret);
			return ret;
		}
	}

	return 0;
}

/**
 * @brief Resume function to support PM in NvPPS driver
 *        This function does following :
 *         - Reset all event data
 *         - Call platform specific resume API if registered to resume the HW IP
 *         - Re-enable 1PPS GPIO pin irq, if registered before going in suspend
 *         - Setup timer for capturing concurrent PTP-TSC timestamps
 *         - Setup timer for monitoring PTP-TSC synchronization
 *
 * @param[in]      pdev platform device structure representing driver
 *
 * @retval
 *                 - 0 on success and
 *                 - -EINVAL on failing to resume platform specific HW IP
 *
 * @pre
 *                 - NvPPS driver must be in suspended state
 *
 * @post
 *                 - Resume Tegra network driver and PTP client & master daemons
 *
 * @usage
 * - Allowed context for the API call
 *   - Signal handler: No
 *   - Thread-safe: No
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - API Group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 *
 */
static int nvpps_resume(struct platform_device *pdev)
{
	unsigned long flags;
	unsigned long timeout = 0;
	struct nvpps_device_data *pdev_data = platform_get_drvdata(pdev);

	/* resume HW PTP-TSC sync */
	if ((pdev_data->support_tsc) &&
		(pdev_data->soc_data.ops->ptp_tsc_resume_sync_fn)) {
		if (pdev_data->soc_data.ops->ptp_tsc_resume_sync_fn(&(pdev_data->soc_data)) != 0) {
			dev_err(pdev_data->dev, "%s: Failed to resume HW PTP-TSC sync\n", __func__);
			return -EINVAL;
		}
	}

	/* Reset timestamp and error data in resume using mutex (process context) */
	mutex_lock(&pdev_data->data_lock);
	pdev_data->pps_event_id_valid = false;
	pdev_data->pps_event_id = 0;
	pdev_data->tsc = 0;
	pdev_data->phc = 0;
	pdev_data->secondary_phc = 0;
	/* Reset error status variables */
	pdev_data->pri_ptp_failed = false;
	pdev_data->sec_ptp_failed = false;
	pdev_data->nvpps_err = NVPPS_SUCCESS;
	/* Reset print flags to enable error messages after resume */
	pdev_data->print_pri_ptp_failed = true;
	pdev_data->print_sec_ptp_failed = true;
	mutex_unlock(&pdev_data->data_lock);

	/* enable GPIO IRQ to support GPIO mode */
	if (pdev_data->irq_registered)
		enable_irq(pdev_data->irq);

	/* Setup timestamp collection timer for TIMER mode, if set before suspend */
	if (pdev_data->timer_inited) {
		timer_setup(&pdev_data->timer,
				nvpps_timer_callback,
				0);

		/* set timer expiry after `ts_capture_interval_ms` */
		if (unlikely(check_add_overflow(jiffies, msecs_to_jiffies(pdev_data->ts_capture_interval_ms), &timeout))) {
			dev_err(pdev_data->dev, "Overflow detected during addition when setting timer in nvtime_resume\n");
		}

		mod_timer(&pdev_data->timer, timeout);
	}

	if (pdev_data->support_tsc)
		/* setup TSC Lock monitoring timer */
		set_mode_tsc(pdev_data);

	/* Clear in_suspend flag using irq_lock to make visible to timer contexts */
	raw_spin_lock_irqsave(&pdev_data->irq_lock, flags);
	pdev_data->in_suspend = false;
	raw_spin_unlock_irqrestore(&pdev_data->irq_lock, flags);

	dev_dbg(pdev_data->dev, "NvPPS Resume successful\n");
	return 0;
}
#endif /* CONFIG_PM */


/**
 * @brief Remove function to deinitialize NvPPS driver
 *        This function does following deinitialization:
 *         - Deinit any timers that have been setup
 *         - Free/unmap memory resources
 *         - Destroy the device node created
 *         - Cleanup character device setup
 *
 * @param[in]      pdev platform device structure representing driver
 *
 * @retval
 *                 - 0 on success
 *
 * @pre
 *                 - All clients for NvPPS should be deinitialized
 *                 - The Tegra network driver should have been deinitialized.
 *
 * @post           none
 *
 * @usage
 * - Allowed context for the API call
 *   - Signal handler: No
 *   - Thread-safe: No
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - API Group
 *   - Init: No
 *   - Runtime: No
 *   - De-Init: Yes
 *
 */
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

/** @} */

MODULE_DESCRIPTION("NVidia Tegra PPS Driver");
MODULE_AUTHOR("David Tao tehyut@nvidia.com");
MODULE_LICENSE("GPL v2");
