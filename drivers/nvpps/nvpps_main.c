// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2018-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

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
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <asm/arch_timer.h>
#include <linux/platform/tegra/ptp-notifier.h>
#include <linux/time.h>
#include <linux/version.h>
#include <uapi/linux/nvpps_ioctl.h>
#include <linux/hte.h>
#include <linux/nvpps.h>
#include <linux/of_address.h>


/* the following control flags are for
 * debugging purpose only
 */
/* #define NVPPS_ARM_COUNTER_PROFILING */
/* #define NVPPS_EQOS_REG_PROFILING */


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
	struct timer_list	tsc_timer;

	volatile bool		timer_inited;

	wait_queue_head_t	pps_event_queue;
	struct fasync_struct	*pps_event_async_queue;

	struct device_node	*pri_emac_node;
	struct device_node	*sec_emac_node;
	resource_size_t		pri_emac_base_addr;
	void __iomem *mac_base_addr;
	u32			sts_offset;
	u32			stns_offset;
	void __iomem 		*tsc_reg_map_base;
	u32			tsc_ptp_src;
	bool		only_timer_mode;
	bool		pri_ptp_failed;
	bool 		sec_ptp_failed;
	bool		support_tsc;
	uint8_t		k_int_val;
	uint16_t	lock_threshold_val;
	struct hte_ts_desc	desc;
	struct gpio_desc	*gpio_in;
};


/* file instance data */
struct nvpps_file_data {
	struct nvpps_device_data	*pdev_data;
	unsigned int			pps_event_id_rd;
};

#define EQOS_STSR_OFFSET		0xb08
#define EQOS_STNSR_OFFSET		0xb0c
#define MGBE_STSR_OFFSET		0xd08
#define MGBE_STNSR_OFFSET		0xd0c

#define T234_MGBE0_BASE_ADDR		0x6810000
#define T234_MGBE1_BASE_ADDR		0x6910000
#define T234_MGBE2_BASE_ADDR		0x6a10000
#define T234_MGBE3_BASE_ADDR		0x6b10000
#define T234_EQOS_BASE_ADDR		0x2310000
#define T194_EQOS_BASE_ADDR		0x2490000

#define	TSC_PTP_SRC_EQOS		0
#define	TSC_PTP_SRC_MGBE0		1
#define TSC_PTP_SRC_MGBE1		2
#define TSC_PTP_SRC_MGBE2		3
#define TSC_PTP_SRC_MGBE3		4
#define TSC_PTP_SRC_INVALID		5

/* Below are the tsc register offset from ioremapped
 * virtual base region stored in tsc_reg_map_base.
 */
#define TSC_STSCRSR_OFFSET								      0x104
#define TSC_CAPTURE_CONFIGURATION_PTX_OFFSET				(TSC_STSCRSR_OFFSET + 0x58)
#define TSC_CAPTURE_CONTROL_PTX_OFFSET					(TSC_STSCRSR_OFFSET + 0x5c)
#define TSC_LOCKING_CONFIGURATION_OFFSET				(TSC_STSCRSR_OFFSET + 0xe4)
#define TSC_LOCKING_CONTROL_OFFSET					(TSC_STSCRSR_OFFSET + 0xe8)
#define TSC_LOCKING_STATUS_OFFSET					(TSC_STSCRSR_OFFSET + 0xec)
#define TSC_LOCKING_REF_FREQUENCY_CONFIGURATION_OFFSET			(TSC_STSCRSR_OFFSET + 0xf0)
#define TSC_LOCKING_DIFF_CONFIGURATION_OFFSET				(TSC_STSCRSR_OFFSET + 0xf4)
#define TSC_LOCKING_ADJUST_CONFIGURATION_OFFSET				(TSC_STSCRSR_OFFSET + 0x108)
#define TSC_LOCKING_FAST_ADJUST_CONFIGURATION_OFFSET			(TSC_STSCRSR_OFFSET + 0x10c)
#define TSC_LOCKING_ADJUST_NUM_CONTROL_OFFSET				(TSC_STSCRSR_OFFSET + 0x110)
#define TSC_LOCKING_ADJUST_DELTA_CONTROL_OFFSET				(TSC_STSCRSR_OFFSET + 0x114)

#define TSC_LOCKING_FAST_ADJUST_CONFIGURATION_OFFSET_K_INT_SHIFT	8

#define SRC_SELECT_BIT_OFFSET	8
#define SRC_SELECT_BITS	0xff

#define TSC_LOCKED_STATUS_BIT_OFFSET 1
#define TSC_ALIGNED_STATUS_BIT_OFFSET 0

#define TSC_LOCK_CTRL_ALIGN_BIT_OFFSET 0

#define TSC_POLL_TIMER	1000
#define BASE_ADDRESS pdev_data->mac_base_addr
#define MAC_STNSR_TSSS_LPOS 0
#define MAC_STNSR_TSSS_HPOS 30

static struct device_node *emac_node;

#define GET_VALUE(data, lbit, hbit) ((data >> lbit) & (~(~0<<(hbit-lbit+1))))
#define MAC_STNSR_OFFSET (BASE_ADDRESS + pdev_data->stns_offset)
#define MAC_STNSR_RD(data) do {\
	(data) = ioread32(MAC_STNSR_OFFSET);\
} while (0)
#define MAC_STSR_OFFSET (BASE_ADDRESS + pdev_data->sts_offset)
#define MAC_STSR_RD(data) do {\
	(data) = ioread32(MAC_STSR_OFFSET);\
} while (0)

/*
 * tegra_chip_data Tegra chip specific data
 * @support_tsc: Supported TSC sync by chip
 */
struct tegra_chip_data {
	bool support_tsc;
};

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 2, 0)
static inline u64 __arch_counter_get_cntvct(void)
{
	u64 cval;

	asm volatile("mrs %0, cntvct_el0" : "=r" (cval));

	return cval;
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(5, 2, 0) */

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
	//TODO : should we support this API with memmapped method
	return tegra_get_hwtime(emac_node, ts, PTP_HWTIME);
}
EXPORT_SYMBOL(nvpps_get_ptp_ts);

static inline u64 get_systime(struct nvpps_device_data *pdev_data, u64 *tsc)
{
	u64 ns1, ns2, ns;
	u32 varmac_stnsr1, varmac_stnsr2;
	u32 varmac_stsr;

	/* read the PHC */
	MAC_STNSR_RD(varmac_stnsr1);
	MAC_STSR_RD(varmac_stsr);
	/* read the TSC */
	*tsc = __arch_counter_get_cntvct();

	/* read the nsec part of the PHC one more time */
	MAC_STNSR_RD(varmac_stnsr2);

	ns1 = GET_VALUE(varmac_stnsr1, MAC_STNSR_TSSS_LPOS, MAC_STNSR_TSSS_HPOS);
	ns2 = GET_VALUE(varmac_stnsr2, MAC_STNSR_TSSS_LPOS, MAC_STNSR_TSSS_HPOS);

	/* if ns1 is greater than ns2, it means nsec counter rollover
	 * happened. In that case read the updated sec counter again
	 */
	if (ns1 > ns2) {
		/* let's read the TSC again */
		*tsc = __arch_counter_get_cntvct();
		/* read the second portion of the PHC */
		MAC_STSR_RD(varmac_stsr);
		/* convert sec/high time value to nanosecond */
		ns = ns2 + (varmac_stsr * 1000000000ull);
	} else {
		/* convert sec/high time value to nanosecond */
		ns = ns1 + (varmac_stsr * 1000000000ull);
	}

	return ns;
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

	/* get the PTP timestamp */
	if (pdev_data->mac_base_addr) {
		/* get both the phc(using memmap reg) and tsc */
		phc = get_systime(pdev_data, &tsc);
		/*TODO : support fetching ptp offset using memmap method */
	} else {
		/* get PTP_TSC concurrent timestamp(using ptp notifier) from MAC driver */
		if (tegra_get_hwtime(pdev_data->pri_emac_node, &ptp_tsc_ts, PTP_TSC_HWTIME)) {
			pdev_data->pri_ptp_failed = true;
		} else {
			pdev_data->pri_ptp_failed = false;
			print_pri_ptp_failed = true;
			phc = ptp_tsc_ts.ptp_ts;
			tsc = ptp_tsc_ts.tsc_ts / pdev_data->tsc_res_ns;
		}

		if ((pdev_data->support_tsc) &&
			/* primary & secondary ptp interface are not same */
			(pdev_data->pri_emac_node != pdev_data->sec_emac_node)) {

			/* get PTP_TSC concurrent timestamp(using ptp notifier) from MAC
			 * driver for secondary interface
			 */
			if (tegra_get_hwtime(pdev_data->sec_emac_node, &sec_ptp_tsc_ts,
										PTP_TSC_HWTIME)) {
				pdev_data->sec_ptp_failed = true;
			} else {
				pdev_data->sec_ptp_failed = false;
				print_sec_ptp_failed = true;

				/* Adjust secondary iface's PTP TS to primary iface's concurrent PTP_TSC TS */
				secondary_phc = sec_ptp_tsc_ts.ptp_ts - (sec_ptp_tsc_ts.tsc_ts - ptp_tsc_ts.tsc_ts);
			}
		}
	}

#ifdef NVPPS_ARM_COUNTER_PROFILING
	{
	u64	tmp;
	int	i;
	irq_tsc = __arch_counter_get_cntvct();
	for (i = 0; i < 98; i++) {
		tmp = __arch_counter_get_cntvct();
	}
		tsc = __arch_counter_get_cntvct();
	}
#endif /* NVPPS_ARM_COUNTER_PROFILING */

#ifdef NVPPS_EQOS_REG_PROFILING
	{
	u32	varmac_stnsr;
	u32	varmac_stsr;
	int	i;
	irq_tsc = __arch_counter_get_cntvct();
	for (i = 0; i < 100; i++) {
		MAC_STNSR_RD(varmac_stnsr);
		MAC_STSR_RD(varmac_stsr)
	}
	tsc = __arch_counter_get_cntvct();
	}
#endif /* NVPPS_EQOS_REG_PROFILING */

	/* get the interrupt latency */
	if (irq_tsc) {
		irq_latency = (tsc - irq_tsc) * pdev_data->tsc_res_ns;
	}

	raw_spin_lock_irqsave(&pdev_data->lock, flags);
	pdev_data->pps_event_id_valid = true;
	pdev_data->pps_event_id++;
	pdev_data->tsc = irq_tsc ? irq_tsc : tsc;
	/* adjust the ptp time for the interrupt latency */
#if defined (NVPPS_ARM_COUNTER_PROFILING) || defined (NVPPS_EQOS_REG_PROFILING)
	pdev_data->phc = phc;
#else /* !NVPPS_ARM_COUNTER_PROFILING && !NVPPS_EQOS_REG_PROFILING */
	pdev_data->phc = phc ? phc - irq_latency : phc;
#endif /* NVPPS_ARM_COUNTER_PROFILING || NVPPS_EQOS_REG_PROFILING */
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

	/* If the current mode is TIMER mode, ignore the interrupt.
	 * If HTE is not enabled, use TSC and process the interrupt.
	 * If HTE is enabled, ignore the interrupt and process it in HTE callback
	 */
	if (!pdev_data->timer_inited) {
		if (!(pdev_data->use_gpio_int_timestamp))
			nvpps_get_ts(pdev_data, __arch_counter_get_cntvct());
	}

	return IRQ_HANDLED;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
static void tsc_timer_callback(unsigned long data)
{
	struct nvpps_device_data        *pdev_data = (struct nvpps_device_data *)data;
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,15,0) */
static void tsc_timer_callback(struct timer_list *t)
{
	struct nvpps_device_data *pdev_data = (struct nvpps_device_data *)from_timer(pdev_data, t, tsc_timer);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4,15,0) */
	uint32_t tsc_lock_status;
	tsc_lock_status = readl(pdev_data->tsc_reg_map_base + TSC_LOCKING_STATUS_OFFSET);
	/* Incase TSC is not locked clear ALIGNED bit(RW1C) so that
	 * TSC starts to lock to the PTP again based on the PTP
	 * source selected in TSC registers.
	 */
	if (!(tsc_lock_status & BIT(TSC_LOCKED_STATUS_BIT_OFFSET))) {
		uint32_t lock_control;
		dev_dbg(pdev_data->dev, "tsc_lock_stat:0x%x\n", tsc_lock_status);
		/* Write 1 to TSC_LOCKING_STATUS_0.ALIGNED to clear it */
		writel(tsc_lock_status | BIT(TSC_ALIGNED_STATUS_BIT_OFFSET),
			pdev_data->tsc_reg_map_base + TSC_LOCKING_STATUS_OFFSET);

		lock_control = readl(pdev_data->tsc_reg_map_base +
							 TSC_LOCKING_CONTROL_OFFSET);
		/* Write 1 to TSC_LOCKING_CONTROL_0.ALIGN */
		writel(lock_control | BIT(TSC_LOCK_CTRL_ALIGN_BIT_OFFSET),
			pdev_data->tsc_reg_map_base + TSC_LOCKING_CONTROL_OFFSET);
	}

	/* set the next expire time */
	mod_timer(&pdev_data->tsc_timer, jiffies + msecs_to_jiffies(TSC_POLL_TIMER));
}


#if LINUX_VERSION_CODE < KERNEL_VERSION(4,15,0)
static void nvpps_timer_callback(unsigned long data)
{
	struct nvpps_device_data        *pdev_data = (struct nvpps_device_data *)data;
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,15,0) */
static void nvpps_timer_callback(struct timer_list *t)
{
        struct nvpps_device_data        *pdev_data = (struct nvpps_device_data *)from_timer(pdev_data, t, timer);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4,15,0) */
	/* get timestamps for this event */
	nvpps_get_ts(pdev_data, 0);

	/* set the next expire time */
	if (pdev_data->timer_inited) {
		mod_timer(&pdev_data->timer, jiffies + msecs_to_jiffies(1000));
	}
}

/* spawn timer to monitor TSC to PTP lock and re-activate
 locking process if its not locked in the handler */
static int set_mode_tsc(struct nvpps_device_data *pdev_data)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
	setup_timer(&pdev_data->tsc_timer,
			tsc_timer_callback,
			(unsigned long)pdev_data);
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0) */
	timer_setup(&pdev_data->tsc_timer,
			tsc_timer_callback,
			0);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0) */
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
						del_timer_sync(&pdev_data->timer);
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
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
					setup_timer(&pdev_data->timer,
							nvpps_timer_callback,
							(unsigned long)pdev_data);
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0) */
					timer_setup(&pdev_data->timer,
							nvpps_timer_callback,
							0);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0) */
					pdev_data->timer_inited = true;
					/* setup timer interval to 1000 msecs */
					mod_timer(&pdev_data->timer, jiffies + msecs_to_jiffies(1000));
				}
				break;

			default:
				return -EINVAL;
		}
	}
	return err;
}



/* Character device stuff */
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
static unsigned int  nvpps_poll(struct file *file, poll_table *wait)
{
#else
static __poll_t nvpps_poll(struct file *file, poll_table *wait)
{
#endif
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
					"failed to get PTP_TSC timestamp from emac instance\n");
				dev_warn_ratelimited(pdev_data->dev, "Make sure PTP is running\n");
				print_pri_ptp_failed = false;
			}

			/* check flag to print ptp failure msg */
			if ((pdev_data->sec_ptp_failed) && (print_sec_ptp_failed)) {
				dev_warn_ratelimited(pdev_data->dev,
					"failed to get PTP_TSC timestamp from emac instance\n");
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

			tsc1 = __arch_counter_get_cntvct();

			dev_dbg(pdev_data->dev, "NVPPS_GETTIMESTAMP\n");

			err = copy_from_user(&time_stamp, uarg,
				sizeof(struct nvpps_timestamp_struct));
			if (err)
				return -EFAULT;

			mutex_lock(&pdev_data->ts_lock);
			switch (time_stamp.clockid) {
			case CLOCK_REALTIME:
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
				ktime_get_real_ts(&time_stamp.kernel_ts);
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0) */
				ktime_get_real_ts64(&time_stamp.kernel_ts);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0) */
				break;

			case CLOCK_MONOTONIC:
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
				ktime_get_ts(&time_stamp.kernel_ts);
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0) */
				ktime_get_ts64(&time_stamp.kernel_ts);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0) */
				break;

			default:
				dev_dbg(pdev_data->dev,
					"ioctl: Unsupported clockid\n");
			}

			err = tegra_get_hwtime(pdev_data->pri_emac_node, &ns, PTP_HWTIME);
			mutex_unlock(&pdev_data->ts_lock);
			if (err) {
				dev_dbg(pdev_data->dev,
					"pdev_data->dev, HW PTP not running\n");
				return err;
			}
			time_stamp.hw_ptp_ts.tv_sec = div_u64_rem(ns,
							1000000000ULL,
							&reminder);
			time_stamp.hw_ptp_ts.tv_nsec = reminder;

			tsc2 = __arch_counter_get_cntvct();
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

static void nvpps_fill_default_mac_phc_info(struct platform_device *pdev,
						struct nvpps_device_data *pdev_data)
{
	struct device_node *np = pdev->dev.of_node;
	bool memmap_phc_regs;

	/* identify the tsc_ptp_src  and sts_offset */
	if (pdev_data->pri_emac_base_addr == T234_MGBE0_BASE_ADDR) {
		pdev_data->sts_offset = MGBE_STSR_OFFSET;
		pdev_data->stns_offset = MGBE_STNSR_OFFSET;
		pdev_data->tsc_ptp_src = TSC_PTP_SRC_MGBE0;
	} else if (pdev_data->pri_emac_base_addr == T234_MGBE1_BASE_ADDR) {
		pdev_data->sts_offset = MGBE_STSR_OFFSET;
		pdev_data->stns_offset = MGBE_STNSR_OFFSET;
		pdev_data->tsc_ptp_src = TSC_PTP_SRC_MGBE1;
	} else if (pdev_data->pri_emac_base_addr == T234_MGBE2_BASE_ADDR) {
		pdev_data->sts_offset = MGBE_STSR_OFFSET;
		pdev_data->stns_offset = MGBE_STNSR_OFFSET;
		pdev_data->tsc_ptp_src = TSC_PTP_SRC_MGBE2;
	} else if (pdev_data->pri_emac_base_addr == T234_MGBE3_BASE_ADDR) {
		pdev_data->sts_offset = MGBE_STSR_OFFSET;
		pdev_data->stns_offset = MGBE_STNSR_OFFSET;
		pdev_data->tsc_ptp_src = TSC_PTP_SRC_MGBE3;
	} else if (pdev_data->pri_emac_base_addr == T234_EQOS_BASE_ADDR) {
		pdev_data->sts_offset = EQOS_STSR_OFFSET;
		pdev_data->stns_offset = EQOS_STNSR_OFFSET;
		pdev_data->tsc_ptp_src = TSC_PTP_SRC_EQOS;
	} else if (pdev_data->pri_emac_base_addr == T194_EQOS_BASE_ADDR) {
		pdev_data->sts_offset = EQOS_STSR_OFFSET;
		pdev_data->stns_offset = EQOS_STNSR_OFFSET;
		pdev_data->tsc_ptp_src = TSC_PTP_SRC_EQOS;
	} else {
		pdev_data->tsc_ptp_src = TSC_PTP_SRC_INVALID;
		dev_err(&pdev->dev, "Invalid EMAC base address\n");
		return;
	}

	/* Get default params from dt */
	memmap_phc_regs = of_property_read_bool(np, "memmap_phc_regs");

	if (memmap_phc_regs) {
		/* TODO: Add support to map secondary interfaces PHC registers */
		pdev_data->mac_base_addr = devm_ioremap(&pdev->dev, pdev_data->pri_emac_base_addr,
											SZ_4K);
		if (pdev_data->mac_base_addr == NULL) {
			dev_err(&pdev->dev, "failed to ioremap emac base address 0x%llx\n",
								pdev_data->pri_emac_base_addr);
			return;
		}
		dev_info(&pdev->dev, "using mem mapped MAC PHC reg method with emac %s\n",
				pdev_data->pri_emac_node->full_name);
	} else {
		if (pdev_data->pri_emac_node != NULL)
			dev_info(&pdev->dev, "using ptp notifier method on emac %s\n",
					pdev_data->pri_emac_node->full_name);
	}

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

static void nvpps_ptp_tsc_sync_config(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	uint32_t tsc_config_ptx_0;
	struct nvpps_device_data *pdev_data = platform_get_drvdata(pdev);

#define DEFAULT_K_INT_VAL			0x70
#define DEFAULT_LOCK_THRESHOLD_20US	0x26c

	//Set default K_INT & LOCK Threshold value
	pdev_data->k_int_val = DEFAULT_K_INT_VAL;
	pdev_data->lock_threshold_val = DEFAULT_LOCK_THRESHOLD_20US;

	//Override default K_INT value
	if (of_property_read_u8(np, "ptp_tsc_k_int", &pdev_data->k_int_val) == 0) {
		dev_info(&pdev->dev, "Using K_INT value : 0x%x\n", pdev_data->k_int_val);
	}

	//Override default Lock Threshold value
	if (of_property_read_u16(np, "ptp_tsc_lock_threshold", &pdev_data->lock_threshold_val) == 0) {
		if (pdev_data->lock_threshold_val < 0x1F) {
			//Use default value
			dev_warn(&pdev->dev, "ptp_tsc_lock_threshold value should be minimum 1us(i.e 0x1F). Using default value 20us(i.e 0x26c)\n");
			pdev_data->lock_threshold_val = DEFAULT_LOCK_THRESHOLD_20US;
		}
		dev_info(&pdev->dev, "Using Lock threshold value : 0x%x\n", pdev_data->lock_threshold_val);
	}

	//onetime config to init PTP TSC Sync logic
	writel(0x119, pdev_data->tsc_reg_map_base + TSC_LOCKING_CONFIGURATION_OFFSET);
	writel(pdev_data->lock_threshold_val, pdev_data->tsc_reg_map_base + TSC_LOCKING_DIFF_CONFIGURATION_OFFSET);
	writel(0x1, pdev_data->tsc_reg_map_base + TSC_LOCKING_CONTROL_OFFSET);
	writel((0x50011 | (pdev_data->k_int_val << TSC_LOCKING_FAST_ADJUST_CONFIGURATION_OFFSET_K_INT_SHIFT)),
		   pdev_data->tsc_reg_map_base + TSC_LOCKING_FAST_ADJUST_CONFIGURATION_OFFSET);
	writel(0x67, pdev_data->tsc_reg_map_base + TSC_LOCKING_ADJUST_DELTA_CONTROL_OFFSET);
	writel(0x313, pdev_data->tsc_reg_map_base + TSC_CAPTURE_CONFIGURATION_PTX_OFFSET);
	writel(0x1, pdev_data->tsc_reg_map_base + TSC_STSCRSR_OFFSET);

	tsc_config_ptx_0 = readl(pdev_data->tsc_reg_map_base + TSC_CAPTURE_CONFIGURATION_PTX_OFFSET);
	/* clear and set the ptp src based on ethernet interface passed
	 * from dt for tsc to lock onto.
	 */
	tsc_config_ptx_0 = tsc_config_ptx_0 &
		~(SRC_SELECT_BITS << SRC_SELECT_BIT_OFFSET);
	if (pdev_data->tsc_ptp_src != TSC_PTP_SRC_INVALID)
		tsc_config_ptx_0 = tsc_config_ptx_0 |
						(pdev_data->tsc_ptp_src << SRC_SELECT_BIT_OFFSET);
	writel(tsc_config_ptx_0, pdev_data->tsc_reg_map_base + TSC_CAPTURE_CONFIGURATION_PTX_OFFSET);
	tsc_config_ptx_0 = readl(pdev_data->tsc_reg_map_base + TSC_CAPTURE_CONFIGURATION_PTX_OFFSET);
	dev_info(&pdev->dev, "TSC config ptx 0x%x\n", tsc_config_ptx_0);

	set_mode_tsc(pdev_data);

	return;
}

static int nvpps_probe(struct platform_device *pdev)
{
	struct nvpps_device_data	*pdev_data;
	struct device_node		*np = pdev->dev.of_node;
	dev_t				devt;
	int				err;
	const struct tegra_chip_data    *cdata = NULL;
	struct resource			res;
	int				index;

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

	pdev_data->pri_emac_node = of_parse_phandle(np, "primary-emac", 0);
	if (pdev_data->pri_emac_node == NULL) {
		dev_info(&pdev->dev, "primary-emac node not found\n");
	} else {
		dev_info(&pdev->dev, "primary-emac found %s", pdev_data->pri_emac_node->full_name);
		index = of_property_match_string(pdev_data->pri_emac_node, "reg-names", "mac");
		if (index >= 0) {
			if (of_address_to_resource(pdev_data->pri_emac_node, index, &res)) {
				dev_err(&pdev->dev, "failed to parse primary emac reg property\n");
			} else {
				pdev_data->pri_emac_base_addr = res.start;
				dev_info(&pdev->dev, "primary emac base address 0x%llx\n",
						pdev_data->pri_emac_base_addr);
			}
		} else {
			dev_info(&pdev->dev, "failed to find ethernet mac registers\n");
		}
	}

	emac_node = pdev_data->pri_emac_node;
	pdev_data->sec_emac_node = of_parse_phandle(np, "sec-emac", 0);
	if (pdev_data->sec_emac_node == NULL) {
		dev_info(&pdev->dev, "sec-emac node not found\n");
		pdev_data->sec_emac_node = pdev_data->pri_emac_node;
	}

	cdata = of_device_get_match_data(&pdev->dev);
	pdev_data->support_tsc = cdata->support_tsc;

	nvpps_fill_default_mac_phc_info(pdev, pdev_data);

	init_waitqueue_head(&pdev_data->pps_event_queue);
	raw_spin_lock_init(&pdev_data->lock);
	mutex_init(&pdev_data->ts_lock);
	pdev_data->pdev = pdev;
	pdev_data->evt_mode = 0; /* NVPPS_MODE_GPIO */
	pdev_data->tsc_mode = NVPPS_TSC_NSEC;
	#define _PICO_SECS (1000000000000ULL)
	pdev_data->tsc_res_ns = (_PICO_SECS / (u64)arch_timer_get_cntfrq()) / 1000;
	#undef _PICO_SECS
	dev_info(&pdev->dev, "tsc_res_ns(%llu)\n", pdev_data->tsc_res_ns);

	/* Set up GPIO and HTE */
	err = nvpps_gpio_hte_setup(pdev_data);
	if (err < 0)
		return err;

	/* character device setup */
#ifndef NVPPS_NO_DT
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
#endif /* !NVPPS_NO_DT */

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
	err = set_mode(pdev_data,
				   (pdev_data->only_timer_mode) ? NVPPS_MODE_TIMER : NVPPS_MODE_GPIO);
	if (err) {
		dev_err(&pdev->dev, "set_mode failed err = %d\n", err);
		device_destroy(s_nvpps_class, pdev_data->dev->devt);
		goto error_ret;
	}
	pdev_data->evt_mode = (pdev_data->only_timer_mode) ? NVPPS_MODE_TIMER : NVPPS_MODE_GPIO;

	if (pdev_data->support_tsc) {
		struct resource *tsc_mem;

		tsc_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (tsc_mem == NULL) {
			err = -ENODEV;
			dev_err(&pdev->dev, "TSC memory resource not defined\n");
			device_destroy(s_nvpps_class, pdev_data->dev->devt);
#ifndef NVPPS_NO_DT
			class_destroy(s_nvpps_class);
#endif
			goto error_ret;
		}

		pdev_data->tsc_reg_map_base = ioremap(tsc_mem->start, resource_size(tsc_mem));
		if (!pdev_data->tsc_reg_map_base) {
		    dev_err(&pdev->dev, "TSC register ioremap failed\n");
			    device_destroy(s_nvpps_class, pdev_data->dev->devt);
		    err = -ENOMEM;
		    goto error_ret;
		}

		/* skip PTP TSC sync configuration if `ptp_tsc_sync_dis` is set */
		if ((of_property_read_bool(np, "ptp_tsc_sync_dis")) == false)
			nvpps_ptp_tsc_sync_config(pdev);
	}

	return 0;

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
			del_timer_sync(&pdev_data->timer);
		}
		if (pdev_data->mac_base_addr) {
			devm_iounmap(&pdev->dev, pdev_data->mac_base_addr);
			dev_info(&pdev->dev, "unmap MAC reg space %p for nvpps\n",
				pdev_data->mac_base_addr);
		}
		if (pdev_data->support_tsc) {
			del_timer_sync(&pdev_data->tsc_timer);
			iounmap(pdev_data->tsc_reg_map_base);
		}
		device_destroy(s_nvpps_class, pdev_data->dev->devt);
	}

	of_node_put(pdev_data->pri_emac_node);
	of_node_put(pdev_data->sec_emac_node);

#ifndef NVPPS_NO_DT
	class_unregister(s_nvpps_class);
	class_destroy(s_nvpps_class);
	unregister_chrdev_region(s_nvpps_devt, MAX_NVPPS_SOURCES);
#endif /* !NVPPS_NO_DT */
	return 0;
}


#ifdef CONFIG_PM
static int nvpps_suspend(struct platform_device *pdev, pm_message_t state)
{
	/* struct nvpps_device_data	*pdev_data = platform_get_drvdata(pdev); */

	return 0;
}

static int nvpps_resume(struct platform_device *pdev)
{
	/* struct nvpps_device_data	*pdev_data = platform_get_drvdata(pdev); */

	return 0;
}
#endif /* CONFIG_PM */


#ifndef NVPPS_NO_DT
static const struct tegra_chip_data tegra234_chip_data = {
	.support_tsc = true,
};
static const struct tegra_chip_data tegra194_chip_data = {
	.support_tsc = false,
};
static const struct of_device_id nvpps_of_table[] = {
	{ .compatible = "nvidia,tegra194-nvpps", .data = &tegra194_chip_data },
	{ .compatible = "nvidia,tegra234-nvpps", .data = &tegra234_chip_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nvpps_of_table);
#endif /* !NVPPS_NO_DT */


static struct platform_driver nvpps_plat_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.owner = THIS_MODULE,
#ifndef NVPPS_NO_DT
		.of_match_table = of_match_ptr(nvpps_of_table),
#endif /* !NVPPS_NO_DT */
	},
	.probe = nvpps_probe,
	.remove = nvpps_remove,
#ifdef CONFIG_PM
	.suspend = nvpps_suspend,
	.resume = nvpps_resume,
#endif /* CONFIG_PM */
};


#ifdef NVPPS_NO_DT
/* module init
 */
static int __init nvpps_init(void)
{
	int err;

	printk("%s\n", __FUNCTION__);

#if defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG) /* Linux v6.4 */
	s_nvpps_class = class_create("nvpps");
#else
	s_nvpps_class = class_create(THIS_MODULE, "nvpps");
#endif
	if (IS_ERR(s_nvpps_class)) {
		printk("nvpps: failed to allocate class\n");
		return PTR_ERR(s_nvpps_class);
	}

	err = alloc_chrdev_region(&s_nvpps_devt, 0, MAX_NVPPS_SOURCES, "nvpps");
	if (err < 0) {
		printk("nvpps: failed to allocate char device region\n");
		class_destroy(s_nvpps_class);
		return err;
	}

	printk("nvpps registered\n");

	return platform_driver_register(&nvpps_plat_driver);
}


/* module fini
 */
static void __exit nvpps_exit(void)
{
	printk("%s\n", __FUNCTION__);
	platform_driver_unregister(&nvpps_plat_driver);

	class_unregister(s_nvpps_class);
	class_destroy(s_nvpps_class);
	unregister_chrdev_region(s_nvpps_devt, MAX_NVPPS_SOURCES);
}

#endif /* NVPPS_NO_DT */


#ifdef NVPPS_NO_DT
module_init(nvpps_init);
module_exit(nvpps_exit);
#else /* !NVPPS_NO_DT */
module_platform_driver(nvpps_plat_driver);
#endif /* NVPPS_NO_DT */

MODULE_DESCRIPTION("NVidia Tegra PPS Driver");
MODULE_AUTHOR("David Tao tehyut@nvidia.com");
MODULE_LICENSE("GPL v2");
