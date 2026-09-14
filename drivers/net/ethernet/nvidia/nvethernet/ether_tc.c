// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2019-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved */

#include <nvidia/conftest.h>
#include "ether_linux.h"

int ether_tc_setup_taprio(struct ether_priv_data *pdata,
			  struct tc_taprio_qopt_offload *qopt)
{
	struct osi_core_priv_data *osi_core = pdata->osi_core;
	unsigned int fpe_required = OSI_DISABLE;
	struct osi_ioctl tc_ioctl_data = {};
	struct osi_est_config *est = (struct osi_est_config *)&tc_ioctl_data.data.est;
	struct osi_fpe_config *fpe = (struct osi_fpe_config *)&tc_ioctl_data.data.fpe;
	unsigned long cycle_time = 0x0U;
	/* Hardcode width base on current HW config, input parameter validation
	 * will be done by OSI code any way
	 */
	/* total GCL enatry will have 32 valid bits */
	unsigned int wid_val = OSI_MAX_32BITS;
	unsigned int gates = 0x0U;
	/* time is 24 bit long */
	unsigned int wid = 24U;
	struct timespec64 time;
	unsigned long ctr;
	int i, ret = 0;

	if (qopt == NULL) {
		netdev_err(pdata->ndev, "invalid input argument\n");
		return -EINVAL;
	}

	if ((osi_core->hw_feature != OSI_NULL) &&
	    (pdata->hw_feat.est_sel == OSI_DISABLE)) {
		netdev_err(pdata->ndev, "EST not supported in HW\n");
		ret = -EOPNOTSUPP;
		goto done;
	}

	if (qopt->num_entries >= OSI_GCL_SIZE_256) {
		netdev_err(pdata->ndev, "invalid number of GCL entries\n");
		ret = -ERANGE;
		goto done;
	}

	if (!qopt->base_time) {
		netdev_err(pdata->ndev, "invalid base time\n");
		ret = -ERANGE;
		goto done;
	}

	if (!qopt->cycle_time) {
		netdev_err(pdata->ndev, "invalid Cycle time\n");
		ret = -ERANGE;
		goto done;
	}

	memset(est, 0x0, sizeof(struct osi_est_config));
	memset(fpe, 0x0, sizeof(struct osi_fpe_config));

	/* This code is to disable TSN, User space is asking to disable
	 */
#if defined(NV_TC_TAPRIO_QOPT_OFFLOAD_STRUCT_HAS_CMD) /* Linux v6.4.5 */
	if (qopt->cmd == TAPRIO_CMD_DESTROY) {
#else
	if (!qopt->enable) {
#endif //NV_TC_TAPRIO_QOPT_OFFLOAD_STRUCT_HAS_CMD
		goto disable;
	}

	est->llr = qopt->num_entries;
#if defined(NV_TC_TAPRIO_QOPT_OFFLOAD_STRUCT_HAS_CMD) /* Linux v6.4.5 */
	switch (qopt->cmd) {
	case TAPRIO_CMD_REPLACE:
		est->en_dis = true;
		break;
	case TAPRIO_CMD_DESTROY:
		est->en_dis = false;
		break;
	default:
		return -EOPNOTSUPP;
	}
#else
	est->en_dis = qopt->enable;
#endif //NV_TC_TAPRIO_QOPT_OFFLOAD_STRUCT_HAS_CMD

	for (i = 0U; i < est->llr; i++) {
		cycle_time = qopt->entries[i].interval;
		gates = qopt->entries[i].gate_mask;

		switch (qopt->entries[i].command) {
		case TC_TAPRIO_CMD_SET_GATES:
			if (fpe_required == OSI_ENABLE) {
				netdev_err(pdata->ndev,
					   "with set-and-hold/release, only set command is not expected\n");
				ret = -EINVAL;
				goto done;
			}
			break;
		case TC_TAPRIO_CMD_SET_AND_HOLD:
			gates |= OSI_BIT(0);
			fpe_required = OSI_ENABLE;
			break;
		case TC_TAPRIO_CMD_SET_AND_RELEASE:
			gates &= ~OSI_BIT(0);
			fpe_required = OSI_ENABLE;
			break;
		default:
			netdev_err(pdata->ndev, "invalid command\n");
			ret = -EOPNOTSUPP;
			goto done;
		}

		est->gcl[i] = cycle_time | (gates << wid);
		if (est->gcl[i] > wid_val) {
			netdev_err(pdata->ndev, "invalid GCL creation\n");
			ret = -EINVAL;
			goto done;
		}
	}

	/* Adjust for real system time TODO better to have
	 * some offset to avoid BTRE
	 */
	time = ktime_to_timespec64(qopt->base_time);
	est->btr[0] = (unsigned int)time.tv_nsec;
	est->btr[1] = (unsigned int)time.tv_sec;
	est->btr_offset[0] = 0;
	est->btr_offset[1] = 0;

	ctr = qopt->cycle_time;
	est->ctr[0] = do_div(ctr, NSEC_PER_SEC);
	est->ctr[1] = (unsigned int)ctr;

	if ((!pdata->hw_feat.fpe_sel) && (fpe_required == OSI_ENABLE)) {
		netdev_err(pdata->ndev, "FPE not supported in HW\n");
		ret = -EOPNOTSUPP;
		goto done;
	}

	if (fpe_required == OSI_ENABLE) {
		fpe->rq = osi_core->residual_queue;
		fpe->tx_queue_preemption_enable = 0x1;
		tc_ioctl_data.cmd = OSI_CMD_CONFIG_FPE;
		ret = osi_handle_ioctl(osi_core, &tc_ioctl_data);
		if (ret < 0) {
			netdev_err(pdata->ndev,
				   "failed to enable Frame Preemption\n");
			goto done;
		} else {
			netdev_info(pdata->ndev, "configured FPE\n");
		}
	}

	tc_ioctl_data.cmd = OSI_CMD_CONFIG_EST;
	ret = osi_handle_ioctl(osi_core, &tc_ioctl_data);
	if (ret < 0) {
		netdev_err(pdata->ndev, "failed to configure EST\n");
		goto disable;
	}

	netdev_info(pdata->ndev, "configured EST\n");
	return 0;

disable:
	est->en_dis = false;
	tc_ioctl_data.cmd = OSI_CMD_CONFIG_EST;
	ret = osi_handle_ioctl(osi_core, &tc_ioctl_data);
	if ((ret >= 0) && (fpe_required == OSI_ENABLE)) {
		fpe->tx_queue_preemption_enable = 0x0;
		tc_ioctl_data.cmd = OSI_CMD_CONFIG_FPE;
		ret = osi_handle_ioctl(osi_core, &tc_ioctl_data);
	}

done:
	return ret;
}

int ether_tc_setup_cbs(struct ether_priv_data *pdata,
		       struct tc_cbs_qopt_offload *qopt)
{
	struct osi_core_priv_data *osi_core = pdata->osi_core;
	struct phy_device *phydev = pdata->phydev;
	struct osi_ioctl ioctl_data = {};
	struct osi_core_avb_algorithm *avb = (struct osi_core_avb_algorithm *)&ioctl_data.data.avb;
	int queue = qopt->queue;
	unsigned int multiplier, speed_div;
	unsigned long  value;
	int speed;

	/* Queue 0 is not AVB capable */
	if (queue <= 0) {
		netdev_err(pdata->ndev, "%s() invalid queue\n", __func__);
		return -EINVAL;
	}

	if (phydev != NULL) {
		speed = phydev->speed;
	} else {
		speed = pdata->speed;
	}

	switch (speed) {
	case OSI_SPEED_10000:
		multiplier = MULTIPLIER_32;
		speed_div = OSI_SPEED_10000 * ETH_1K;
		break;
	case OSI_SPEED_5000:
		multiplier = MULTIPLIER_32;
		speed_div = OSI_SPEED_5000 * ETH_1K;
		break;
	case OSI_SPEED_2500:
		multiplier = MULTIPLIER_8;
		speed_div = OSI_SPEED_2500 * ETH_1K;
		break;
	case OSI_SPEED_1000:
		multiplier = MULTIPLIER_8;
		speed_div = OSI_SPEED_1000 * ETH_1K;
		break;
	case OSI_SPEED_100:
		multiplier = MULTIPLIER_4;
		speed_div = OSI_SPEED_100 * ETH_1K;
		break;
	default:
		netdev_err(pdata->ndev, "invalid speed\n");
		return -EINVAL;
	}

	avb->qindex = (unsigned int)queue;
	avb->tcindex = (unsigned int)queue;

	if (qopt->enable) {
		avb->algo = OSI_MTL_TXQ_AVALG_CBS;
		avb->oper_mode = OSI_MTL_QUEUE_AVB;
		avb->credit_control = OSI_ENABLE;
	} else {
	/* For EQOS harware library code use internally SP(0) and
	   For MGBE harware library code use internally ETS(2) if
	   algo != CBS. */
		avb->algo = OSI_MTL_TXQ_AVALG_SP;
		avb->oper_mode = OSI_MTL_QUEUE_ENABLE;
		avb->credit_control = OSI_DISABLE;
	}

	/* Final adjustments for HW */
	value = div_s64(qopt->idleslope * 1024ll * multiplier, speed_div);
	avb->idle_slope = (unsigned long)value;

	value = div_s64(-qopt->sendslope * 1024ll * multiplier, speed_div);
	avb->send_slope = (unsigned long)value;

	value = qopt->hicredit * 1024ll * 8;
	avb->hi_credit = (unsigned long)value;

	value = qopt->locredit * 1024ll * 8;
	avb->low_credit = (unsigned long)value;

	ioctl_data.cmd = OSI_CMD_SET_AVB;

	return osi_handle_ioctl(osi_core, &ioctl_data);
}
