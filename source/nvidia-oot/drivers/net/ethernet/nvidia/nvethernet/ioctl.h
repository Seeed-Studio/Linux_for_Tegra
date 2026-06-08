/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2019-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved */

#ifndef IOCTL_H
#define IOCTL_H

#include "ether_export.h"
#include "ether_callback_export.h"

/**
 * @addtogroup private IOCTL related info
 *
 * @{
 */
/** Configure loopback mode enable/disable */
/* Get TX channel/queue count */
#define EQOS_GET_TX_QCNT		23
/* Get RX channel/queue count */
#define EQOS_GET_RX_QCNT		24
/** Set L2 DA filtering */
#define EQOS_L2_DA_FILTERING_CMD	35
#define ETHER_CONFIG_LOOPBACK_MODE	40
#define ETHER_READ_REG			53
#define ETHER_WRITE_REG			54
#ifdef OSI_DEBUG
#define ETHER_REGISTER_DUMP		56
#define ETHER_STRUCTURE_DUMP		57
#endif /* OSI_DEBUG */
#ifdef OSI_DEBUG
#define ETHER_DEBUG_INTR_CONFIG		60
#endif
/** @} */

/**
 *@addtogroup IOCTL Helper MACROS
 * @{
 */
#define MAX_IP_ADDR_BYTE	0xFFU
/* class E IP4 addr start range, reserved */
#define CLASS_E_IP4_ADDR_RANGE_START	240U
/* class D multicast addr range */
#define MIN_MC_ADDR_RANGE	224U
#define MAX_MC_ADDR_RANGE	239U

/* Remote wakeup filter */
#define EQOS_RWK_FILTER_LENGTH		8
/** @} */

/**
 * @brief ether_priv_ioctl - Handle private IOCTLs
 *	Algorithm:
 *	1) Copy the priv command data from user space.
 *	2) Check the priv command cmd and invoke handler func.
 *	if it is supported.
 *	3) Copy result back to user space.
 *
 * @param[in] ndev: network device structure
 * @param[in] ifr: Interface request structure used for socket ioctl's.
 *
 * @note Interface should be running (enforced by caller).
 *
 * @retval 0 on success
 * @retval negative value on failure.
 */
int ether_handle_priv_ioctl(struct net_device *ndev,
			    struct ifreq *ifr);
#endif
