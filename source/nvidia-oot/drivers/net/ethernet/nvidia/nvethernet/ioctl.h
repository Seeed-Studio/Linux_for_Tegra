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
/** Configure ARP offload enable/disable */
#define ETHER_CONFIG_ARP_OFFLOAD	36
#define ETHER_CONFIG_LOOPBACK_MODE	40
/** Configure PTP offload enable/disable */
#define ETHER_CONFIG_PTP_OFFLOAD	42
/** Get current configuration in HW */
#define ETHER_GET_AVB_ALGORITHM		46
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

/* PTP offload mode defines */
/** PTP Ordinary Slave Mode */
#define ETHER_PTP_ORDINARY_SLAVE		1
/** PTP Ordinary Master Mode */
#define ETHER_PTP_ORDINARY_MASTER		2
/** PTP Transparent Slave Mode */
#define ETHER_PTP_TRASPARENT_SLAVE		3
/** PTP Transparent Master Mode */
#define ETHER_PTP_TRASPARENT_MASTER		4
/** PTP Transparent Peer Mode */
#define ETHER_PTP_PEER_TO_PEER_TRANSPARENT	5

/* Remote wakeup filter */
#define EQOS_RWK_FILTER_LENGTH		8
/** @} */

/**
 * @brief struct to support APR offload
 *      NVETHERNET_LINUX_PIF$ETHER_CONFIG_ARP_OFFLOAD command
 */
struct arp_offload_param {
	/**
	 * ip_addr: Byte array for decimal representation of IP address.
	 * - For example, 192.168.1.3 is represented as
	 *   ip_addr[0] = '192' ip_addr[1] = '168' ip_addr[2] = '1'
	 *   ip_addr[3] = '3'
	 */
	unsigned char ip_addr[NUM_BYTES_IN_IPADDR];
};

/**
 * @brief struct to support PTP offload
 *      NVETHERNET_LINUX_PIF$ETHER_CONFIG_PTP_OFFLOAD command
 */
struct ptp_offload_param {
	/** indicates PTP offload status
	 * - Valid values are enable(1) or disable(0) */
	int en_dis;
	/** indicates PTP mode
	 * - Valid values:
         *   - NVETHERNET_LINUX_PIF$ETHER_PTP_ORDINARY_MASTER
         *   - NVETHERNET_LINUX_PIF$ETHER_PTP_ORDINARY_SLAVE
         *   - NVETHERNET_LINUX_PIF$ETHER_PTP_TRASPARENT_MASTER
         *   - NVETHERNET_LINUX_PIF$ETHER_PTP_TRASPARENT_SLAVE
         *   - NVETHERNET_LINUX_PIF$ETHER_PTP_PEER_TO_PEER_TRANSPARENT */
	int mode;
	/** ptp domain
	 * - Valid values: 0  to 0xFF */
	int domain_num;
	/**  The PTP Offload function qualifies received PTP
	 *  packet with unicast Destination  address
	 * - Valid values:
	 *   - 0 for only multicast
         *   - 1 for unicast and multicast */
	int mc_uc;
};


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
