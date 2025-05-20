// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2019-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "ether_linux.h"

/**
 * @brief Function used to get PTP time
 * @param[in] data: OSI core private data structure
 *
 * @retval "nano seconds" of MAC system time
 */
static inline int ether_get_hw_time(struct net_device *dev,
				    void *ts, int ts_type)
{
	struct ether_priv_data *pdata;
	struct osi_dma_priv_data *osi_dma;
	struct osi_core_priv_data *osi_core;
	struct osi_ioctl ioctl_data = {};
	unsigned long flags;
	unsigned int sec, nsec;
	struct osi_core_ptp_tsc_data local_ts;
	int ret = -1;

	pdata = netdev_priv(dev);
	osi_dma = pdata->osi_dma;
	osi_core = pdata->osi_core;

	switch (ts_type) {
	case PTP_HWTIME:
		raw_spin_lock_irqsave(&pdata->ptp_lock, flags);

		ret = osi_dma_get_systime_from_mac(osi_dma, &sec, &nsec);
		if (ret != 0) {
			dev_err(pdata->dev, "%s: Failed to read systime from MAC %d\n",
				__func__, ret);
			raw_spin_unlock_irqrestore(&pdata->ptp_lock, flags);
			return ret;
		}

		*((u64 *)ts) = nsec + (sec * OSI_NSEC_PER_SEC);

		raw_spin_unlock_irqrestore(&pdata->ptp_lock, flags);
		break;

	case PTP_TSC_HWTIME:
		raw_spin_lock_irqsave(&pdata->ptp_lock, flags);

		ioctl_data.cmd = OSI_CMD_CAP_TSC_PTP;
		ret = osi_handle_ioctl(osi_core, &ioctl_data);
		if (ret != 0) {
			dev_err(pdata->dev,
				"Failed to get TSC Struct info from registers\n");
			raw_spin_unlock_irqrestore(&pdata->ptp_lock, flags);
			return ret;
		}
		memcpy(&local_ts, &ioctl_data.data.ptp_tsc,
		       sizeof(struct osi_core_ptp_tsc_data));

		((struct ptp_tsc_data *)ts)->ptp_ts = local_ts.ptp_low_bits +
						      (local_ts.ptp_high_bits * OSI_NSEC_PER_SEC);
		((struct ptp_tsc_data *)ts)->tsc_ts = ((u64)local_ts.tsc_high_bits <<
						       TSC_HIGH_SHIFT) |
						       local_ts.tsc_low_bits;

		raw_spin_unlock_irqrestore(&pdata->ptp_lock, flags);
		break;

	default:
		dev_err(pdata->dev, "Invalid time stamp requested\n");
		return -EINVAL;
	}
	return 0;
}

int ether_adjust_time(struct ptp_clock_info *ptp, s64 nsec_delta)
{
	struct ether_priv_data *pdata = container_of(ptp,
						     struct ether_priv_data,
						     ptp_clock_ops);
	struct osi_core_priv_data *osi_core = pdata->osi_core;
	struct osi_ioctl ioctl_data = {};
	unsigned long flags;
	int ret = -1;

	raw_spin_lock_irqsave(&pdata->ptp_lock, flags);

	ioctl_data.cmd = OSI_CMD_ADJ_TIME;
	ioctl_data.arg8_64 = nsec_delta;
	ret = osi_handle_ioctl(osi_core, &ioctl_data);
	if (ret < 0) {
		dev_err(pdata->dev,
			"%s:failed to adjust time with reason %d\n",
			__func__, ret);
	}

	raw_spin_unlock_irqrestore(&pdata->ptp_lock, flags);

	return ret;
}

int ether_adjust_clock(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct ether_priv_data *pdata = container_of(ptp,
						     struct ether_priv_data,
						     ptp_clock_ops);
	struct osi_core_priv_data *osi_core = pdata->osi_core;
	struct osi_ioctl ioctl_data = {};
	unsigned long flags;
	int ret = -1;

	raw_spin_lock_irqsave(&pdata->ptp_lock, flags);

	ioctl_data.cmd = OSI_CMD_ADJ_FREQ;
	ioctl_data.arg6_32 = scaled_ppm_to_ppb(scaled_ppm);
	ret = osi_handle_ioctl(osi_core, &ioctl_data);
	if (ret < 0) {
		dev_err(pdata->dev,
			"%s:failed to adjust frequency with reason code %d\n",
			__func__, ret);
	}

	raw_spin_unlock_irqrestore(&pdata->ptp_lock, flags);

	return ret;
}

int ether_get_time(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct ether_priv_data *pdata = container_of(ptp,
						     struct ether_priv_data,
						     ptp_clock_ops);
	struct osi_dma_priv_data *osi_dma = pdata->osi_dma;
	unsigned int sec, nsec;
	unsigned long flags;
	int ret = 0;

	raw_spin_lock_irqsave(&pdata->ptp_lock, flags);

	ret = osi_dma_get_systime_from_mac(osi_dma, &sec, &nsec);
	if (ret < 0) {
		dev_err(pdata->dev, "%s: Failed to read systime from MAC %d\n",
                        __func__, ret);
		raw_spin_unlock_irqrestore(&pdata->ptp_lock, flags);
		return ret;
	}
	raw_spin_unlock_irqrestore(&pdata->ptp_lock, flags);

	ts->tv_sec = sec;
	ts->tv_nsec = nsec;

	return 0;
}

int ether_set_time(struct ptp_clock_info *ptp, const struct timespec64 *ts)
{
	struct ether_priv_data *pdata = container_of(ptp,
						     struct ether_priv_data,
						     ptp_clock_ops);
	struct osi_core_priv_data *osi_core = pdata->osi_core;
	struct osi_ioctl ioctl_data = {};
	unsigned long flags;
	int ret = -1;

	raw_spin_lock_irqsave(&pdata->ptp_lock, flags);

	ioctl_data.cmd = OSI_CMD_SET_SYSTOHW_TIME;
	ioctl_data.arg1_u32 = ts->tv_sec;
	ioctl_data.arg2_u32 = ts->tv_nsec;
	ret = osi_handle_ioctl(osi_core, &ioctl_data);
	if (ret < 0) {
		dev_err(pdata->dev,
			"%s:failed to set system time with reason %d\n",
			__func__, ret);
	}

	raw_spin_unlock_irqrestore(&pdata->ptp_lock, flags);

	return ret;
}

/**
 * @brief Describing Ethernet PTP hardware clock
 */
static struct ptp_clock_info ether_ptp_clock_ops = {
	.owner = THIS_MODULE,
	.name = "ether_ptp_clk",
	.max_adj = OSI_PTP_REQ_CLK_FREQ,
	.n_alarm = 0,
	.n_ext_ts = 0,
	.n_per_out = 0,
	.pps = 0,
	.adjfine = ether_adjust_clock,
	.adjtime = ether_adjust_time,
	.gettime64 = ether_get_time,
	.settime64 = ether_set_time,
};

static int ether_early_ptp_init(struct ether_priv_data *pdata)
{
	struct osi_core_priv_data *osi_core = pdata->osi_core;
	struct osi_ioctl ioctl_data = {};
	int ret = 0;
	struct timespec64 now;

	osi_core->ptp_config.ptp_filter =
		OSI_MAC_TCR_TSENA | OSI_MAC_TCR_TSCFUPDT |
		OSI_MAC_TCR_TSCTRLSSR | OSI_MAC_TCR_TSVER2ENA |
		OSI_MAC_TCR_TSIPENA | OSI_MAC_TCR_TSIPV6ENA |
		OSI_MAC_TCR_TSIPV4ENA | OSI_MAC_TCR_SNAPTYPSEL_1;

	/* Store default PTP clock frequency, so that we
	 * can make use of it for coarse correction */
	osi_core->ptp_config.ptp_clock = pdata->ptp_ref_clock_speed;
	/* initialize system time */
	ktime_get_real_ts64(&now);
	/* Store sec and nsec */
	osi_core->ptp_config.sec = now.tv_sec;
	osi_core->ptp_config.nsec = now.tv_nsec;
	/* one nsec accuracy */
	osi_core->ptp_config.one_nsec_accuracy = OSI_ENABLE;

	/* enable the PTP configuration */
	ioctl_data.arg1_u32 = OSI_ENABLE;
	ioctl_data.cmd = OSI_CMD_CONFIG_PTP;
	ret = osi_handle_ioctl(osi_core, &ioctl_data);
	if (ret < 0) {
		dev_err(pdata->dev, "Failure to enable CONFIG_PTP\n");
		return -EFAULT;
	}

	return ret;
}

int ether_ptp_init(struct ether_priv_data *pdata)
{
	if (pdata->hw_feat.tsstssel == OSI_DISABLE) {
		pdata->ptp_clock = NULL;
		dev_err(pdata->dev, "No PTP supports in HW\n"
			"Aborting PTP clock driver registration\n");
		return -1;
	}

	raw_spin_lock_init(&pdata->ptp_lock);

	pdata->ptp_clock_ops = ether_ptp_clock_ops;
	pdata->ptp_clock = ptp_clock_register(&pdata->ptp_clock_ops,
					      pdata->dev);
	if (IS_ERR(pdata->ptp_clock)) {
		pdata->ptp_clock = NULL;
		dev_err(pdata->dev, "Fail to register PTP clock\n");
		return -1;
	}

	/* By default enable nano second accuracy */
	pdata->osi_core->ptp_config.one_nsec_accuracy = OSI_ENABLE;
	if ((pdata->osi_core->m2m_role == OSI_PTP_M2M_PRIMARY) ||
	    (pdata->osi_core->m2m_role == OSI_PTP_M2M_SECONDARY)) {
		return ether_early_ptp_init(pdata);
	}

	return 0;
}

void ether_ptp_remove(struct ether_priv_data *pdata)
{
	if (pdata->ptp_clock) {
		ptp_clock_unregister(pdata->ptp_clock);
	}
}

#ifndef OSI_STRIPPED_LIB
/**
 * @brief Configure Slot function
 *
 * Algorithm: This function will set/reset slot funciton
 *
 * @param[in] pdata: Pointer to private data structure.
 * @param[in] set: Flag to set or reset the Slot function.
 *
 * @note PTP clock driver need to be successfully registered during
 *	initialization and HW need to support PTP functionality.
 *
 * @retval none
 */
static void ether_config_slot_function(struct ether_priv_data *pdata, u32 set)
{
	struct osi_dma_priv_data *osi_dma = pdata->osi_dma;
	struct osi_core_priv_data *osi_core = pdata->osi_core;
	unsigned int ret, i, chan, qinx;
	struct osi_ioctl ioctl_data = {};
	struct osi_core_avb_algorithm *avb = (struct osi_core_avb_algorithm *)&ioctl_data.data.avb;

	/* Configure TXQ AVB mode */
	for (i = 0; i < osi_dma->num_dma_chans; i++) {
		chan = osi_dma->dma_chans[i];
		if (osi_dma->slot_enabled[chan] == OSI_ENABLE) {
			/* Set TXQ AVB info */
			memset(avb, 0,
			       sizeof(struct osi_core_avb_algorithm));
			qinx = osi_core->mtl_queues[i];
			avb->qindex = qinx;
			/* For EQOS harware library code use internally SP(0) and
			   For MGBE harware library code use internally ETS(2) if
			   algo != CBS. */
			avb->algo = OSI_MTL_TXQ_AVALG_SP;
			avb->oper_mode = (set == OSI_ENABLE) ?
					  OSI_MTL_QUEUE_AVB :
					  OSI_MTL_QUEUE_ENABLE;

			ioctl_data.cmd = OSI_CMD_SET_AVB;
			ret = osi_handle_ioctl(osi_core, &ioctl_data);
			if (ret != 0) {
				dev_err(pdata->dev,
					"Failed to set TXQ:%d AVB info\n",
					qinx);
				return;
			}
		}
	}

	/* Call OSI slot function to configure */
	osi_config_slot_function(osi_dma, set);
}
#endif /* !OSI_STRIPPED_LIB */

int ether_handle_hwtstamp_ioctl(struct ether_priv_data *pdata,
		struct ifreq *ifr)
{
	struct osi_core_priv_data *osi_core = pdata->osi_core;
	struct osi_dma_priv_data *osi_dma = pdata->osi_dma;
#ifdef CONFIG_TEGRA_NVPPS
	struct net_device *ndev = pdata->ndev;
#endif
	struct osi_ioctl ioctl_data = {};
	struct hwtstamp_config config;
	unsigned int hwts_rx_en = 1;
	int ret;
	struct timespec64 now;

	if (pdata->hw_feat.tsstssel == OSI_DISABLE) {
		dev_info(pdata->dev, "HW timestamping not available\n");
		return -EOPNOTSUPP;
	}

	if (copy_from_user(&config, ifr->ifr_data,
			   sizeof(struct hwtstamp_config))) {
		return -EFAULT;
	}

	dev_info(pdata->dev, "config.flags = %#x, tx_type = %#x,"
		 "rx_filter = %#x\n", config.flags, config.tx_type,
		 config.rx_filter);

	/* reserved for future extensions */
	if (config.flags) {
		return -EINVAL;
	}

	if (memcmp(&config, &pdata->ptp_config, sizeof(struct hwtstamp_config)) == 0) {
		goto skip;
	}

	switch (config.tx_type) {
	case HWTSTAMP_TX_OFF:
		pdata->hwts_tx_en = OSI_DISABLE;
		break;

	case HWTSTAMP_TX_ON:
	case HWTSTAMP_TX_ONESTEP_SYNC:
		pdata->hwts_tx_en = OSI_ENABLE;
		break;

	default:
		dev_err(pdata->dev, "tx_type is out of range\n");
		return -ERANGE;
	}
	/* Initialize ptp filter to 0 */
	osi_core->ptp_config.ptp_filter = 0;

	switch (config.rx_filter) {
		/* time stamp no incoming packet at all */
	case HWTSTAMP_FILTER_NONE:
		hwts_rx_en = 0;
		break;

		/* PTP v1, UDP, any kind of event packet */
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
		osi_core->ptp_config.ptp_filter = OSI_MAC_TCR_SNAPTYPSEL_1 |
						  OSI_MAC_TCR_TSIPV4ENA    |
						  OSI_MAC_TCR_TSIPV6ENA;
		break;

		/* PTP v1, UDP, Sync packet */
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
		osi_core->ptp_config.ptp_filter =
#ifndef OSI_STRIPPED_LIB
						  OSI_MAC_TCR_TSEVENTENA |
#endif /* !OSI_STRIPPED_LIB */
						  OSI_MAC_TCR_TSIPV4ENA	 |
						  OSI_MAC_TCR_TSIPV6ENA;
		break;

		/* PTP v1, UDP, Delay_req packet */
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
		osi_core->ptp_config.ptp_filter =
#ifndef OSI_STRIPPED_LIB
						  OSI_MAC_TCR_TSMASTERENA |
						  OSI_MAC_TCR_TSEVENTENA  |
#endif /* !OSI_STRIPPED_LIB */
						  OSI_MAC_TCR_TSIPV4ENA   |
						  OSI_MAC_TCR_TSIPV6ENA;
		break;

		/* PTP v2, UDP, any kind of event packet */
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
		osi_core->ptp_config.ptp_filter = OSI_MAC_TCR_SNAPTYPSEL_1 |
						  OSI_MAC_TCR_TSIPV4ENA    |
						  OSI_MAC_TCR_TSIPV6ENA    |
						  OSI_MAC_TCR_TSVER2ENA;
		break;

		/* PTP v2, UDP, Sync packet */
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
		osi_core->ptp_config.ptp_filter =
#ifndef OSI_STRIPPED_LIB
						  OSI_MAC_TCR_TSEVENTENA   |
#endif /* !OSI_STRIPPED_LIB */
						  OSI_MAC_TCR_TSIPV4ENA    |
						  OSI_MAC_TCR_TSIPV6ENA    |
						  OSI_MAC_TCR_TSVER2ENA;
		break;

		/* PTP v2, UDP, Delay_req packet */
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
		osi_core->ptp_config.ptp_filter =
#ifndef OSI_STRIPPED_LIB
						  OSI_MAC_TCR_TSEVENTENA   |
						  OSI_MAC_TCR_TSMASTERENA  |
#endif /* !OSI_STRIPPED_LIB */
						  OSI_MAC_TCR_TSIPV4ENA    |
						  OSI_MAC_TCR_TSIPV6ENA    |
						  OSI_MAC_TCR_TSVER2ENA;
		break;

		/* PTP v2/802.AS1, any layer, any kind of event packet */
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
		osi_core->ptp_config.ptp_filter = OSI_MAC_TCR_TSIPV4ENA    |
						  OSI_MAC_TCR_TSIPV6ENA    |
						  OSI_MAC_TCR_TSVER2ENA    |
						  OSI_MAC_TCR_TSIPENA;

		if ((osi_dma->ptp_flag & OSI_PTP_SYNC_ONESTEP) ==
		    OSI_PTP_SYNC_ONESTEP) {
#ifndef OSI_STRIPPED_LIB
			osi_core->ptp_config.ptp_filter |=
						  (OSI_MAC_TCR_TSEVENTENA |
						   OSI_MAC_TCR_CSC);
			if ((osi_dma->ptp_flag & OSI_PTP_SYNC_MASTER) ==
			    OSI_PTP_SYNC_MASTER) {
				osi_core->ptp_config.ptp_filter |=
						  OSI_MAC_TCR_TSMASTERENA;
			}
#endif /* !OSI_STRIPPED_LIB */
		} else {
			osi_core->ptp_config.ptp_filter |=
						  OSI_MAC_TCR_SNAPTYPSEL_1;
		}
		break;

		/* PTP v2/802.AS1, any layer, Sync packet */
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
		osi_core->ptp_config.ptp_filter = OSI_MAC_TCR_TSIPV4ENA    |
						  OSI_MAC_TCR_TSIPV6ENA    |
						  OSI_MAC_TCR_TSVER2ENA    |
#ifndef OSI_STRIPPED_LIB
						  OSI_MAC_TCR_TSEVENTENA   |
						  OSI_MAC_TCR_AV8021ASMEN  |
#endif /* !OSI_STRIPPED_LIB */
						  OSI_MAC_TCR_TSIPENA;
		break;

		/* PTP v2/802.AS1, any layer, Delay_req packet */
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		osi_core->ptp_config.ptp_filter = OSI_MAC_TCR_TSIPV4ENA    |
						  OSI_MAC_TCR_TSIPV6ENA    |
						  OSI_MAC_TCR_TSVER2ENA    |
#ifndef OSI_STRIPPED_LIB
						  OSI_MAC_TCR_TSEVENTENA   |
						  OSI_MAC_TCR_AV8021ASMEN  |
						  OSI_MAC_TCR_TSMASTERENA  |
#endif /* !OSI_STRIPPED_LIB */
						  OSI_MAC_TCR_TSIPENA;
		break;

#ifndef OSI_STRIPPED_LIB
		/* time stamp any incoming packet */
	case HWTSTAMP_FILTER_ALL:
		osi_core->ptp_config.ptp_filter = OSI_MAC_TCR_TSENALL;
		break;
#endif /* !OSI_STRIPPED_LIB */

	default:
		dev_err(pdata->dev, "rx_filter is out of range\n");
		return -ERANGE;
	}

	if (!pdata->hwts_tx_en && !hwts_rx_en) {
		/* disable the PTP configuration */
		ioctl_data.arg1_u32 = OSI_DISABLE;
		ioctl_data.cmd = OSI_CMD_CONFIG_PTP;
		ret = osi_handle_ioctl(osi_core, &ioctl_data);
		if (ret < 0) {
			dev_err(pdata->dev, "Failure to disable CONFIG_PTP\n");
			return -EFAULT;
		}
#ifndef OSI_STRIPPED_LIB
		ether_config_slot_function(pdata, OSI_DISABLE);
#endif /* !OSI_STRIPPED_LIB */
	} else {
		/* Store default PTP clock frequency, so that we
		 * can make use of it for coarse correction */
		osi_core->ptp_config.ptp_clock = pdata->ptp_ref_clock_speed;
		/* initialize system time */
		ktime_get_real_ts64(&now);
		/* Store sec and nsec */
		osi_core->ptp_config.sec = now.tv_sec;
		osi_core->ptp_config.nsec = now.tv_nsec;
		/* one nsec accuracy */
		osi_core->ptp_config.one_nsec_accuracy = OSI_ENABLE;
		/* Enable the PTP configuration */
		ioctl_data.arg1_u32 = OSI_ENABLE;
		ioctl_data.cmd = OSI_CMD_CONFIG_PTP;
		ret = osi_handle_ioctl(osi_core, &ioctl_data);
		if (ret < 0) {
			dev_err(pdata->dev, "Failure to enable CONFIG_PTP\n");
			return -EFAULT;
		}
#ifdef CONFIG_TEGRA_NVPPS
		/* Register broadcasting MAC timestamp to clients */
		tegra_register_hwtime_source(ether_get_hw_time, ndev);
#endif
#ifndef OSI_STRIPPED_LIB
		ether_config_slot_function(pdata, OSI_ENABLE);
#endif /* !OSI_STRIPPED_LIB */
	}

	memcpy(&pdata->ptp_config, &config, sizeof(struct hwtstamp_config));
skip:
	return (copy_to_user(ifr->ifr_data, &config,
			     sizeof(struct hwtstamp_config))) ? -EFAULT : 0;
}
