/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2019-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved */

#ifndef ETHER_EXPORT_H
#define ETHER_EXPORT_H

#include <nvethernetrm_export.h>
/**
 * @addtogroup private IOCTL related info
 *
 * @brief MACRO are defined for driver supported
 * private IOCTLs. These IOCTLs can be called using
 * SIOCDEVPRIVATE custom ioctl command.
 * @{
 */
/** Private IOCTL number for reading MDIO in the network device. */
#define ETHER_PRV_RMDIO_IOCTL		(SIOCDEVPRIVATE + 2)
/** Private IOCTL number for writing MDIO in the network device. */
#define ETHER_PRV_WMDIO_IOCTL		(SIOCDEVPRIVATE + 3)
/** Get Line speed */
#define EQOS_GET_CONNECTED_SPEED	25
/** Set HW AVB configuration from user application */
#define ETHER_AVB_ALGORITHM		27
/** Set L3/L4 filter */
#define EQOS_L3L4_FILTER_CMD		29
/** Set VLAN filter */
#define EQOS_VLAN_FILTERING_CMD		34
/** Set PTP RxQ in HW */
#define ETHER_PTP_RXQUEUE		48
/** Configure EST(802.1 bv) in HW */
#define ETHER_CONFIG_EST		49
/** Configure FPE (802.1 bu + 803.2 br) in HW */
#define ETHER_CONFIG_FPE		50
/** Configure FRP rule in HW */
#define ETHER_CONFIG_FRP_CMD		51
/** Configure the DMA channel number to route multicast packets in HW */
#define ETHER_MC_DMA_ROUTE		52
/** Configure the PAD calibration in HW */
#define ETHER_PAD_CALIBRATION		55
/** Configure the TSC PTP in HW */
#define ETHER_CAP_TSC_PTP		58
/** Configure the MAC 2 MAC TS sync in HW */
#define ETHER_M2M_TSYNC			59
/** Configure L2 Filter (Only with Ethernet virtualization) */
#define ETHER_L2_ADDR			61
/** To get the AVB performance */
#define ETHER_GET_AVB_PERF		62
/** To get timestamp status */
#define ETHER_VERIFY_TS			63
/** To get regsiter status */
#define ETHER_GET_STATUS		64
/** @} */

/**
 *@addtogroup IOCTL Helper MACROS
 * @{
 */
#define NUM_BYTES_IN_IPADDR	4

/** @} */

/**
 * @brief Structure for L2 filters NVETHERNET_LINUX_PIF$ETHER_L2_ADDR command
 */
struct ether_l2_filter {
	/** indicates L2 filter status
	 * - Expected values are enable(1) or disable(0) */
	nveu32_t en_dis;
	/** Indicates the index of the filter to be modified.
	 * - Filter index must be between 0 to 31 for EQOS and 0 to 47 for MGBE */
	nveu32_t index;
	/** Ethernet MAC address to be added */
	nveu8_t mac_addr[OSI_ETH_ALEN];
	/** Packet duplication support
	 * - Expected values are enable(1) or disable(0) */
	nveu32_t pkt_dup;
	/** dma channel number, values are:
         * - from 0 to NVETHERNETRM_PIF$OSI_EQOS_MAX_NUM_CHANS for EQOS
         * - from 0 to NVETHERNETRM_PIF$OSI_MGBE_MAX_NUM_CHANS for MGBE */
	nveu32_t dma_chan;
};

/**
 * @brief struct ether_exported_ifr_data - Private data of struct ifreq
 */
struct ether_exported_ifr_data {
	/** Flags used for specific ioctl
	 * - Expected values are enable(1) or disable(0) */
	nveu32_t if_flags;
	/** qinx: Queue index to be used for certain ioctls
	 * - Valid range:
	 *  - 0 to NVETHERNETRM_PIF$OSI_MGBE_MAX_NUM_QUEUES for MGBE
	 *  - 0 to NVETHERNETRM_PIF$OSI_EQOS_MAX_NUM_QUEUES for EQOS
	 *  - for ifcmd NVETHERNET_LINUX_PIF$ETHER_PTP_RXQUEUE, RXQ number as input
	 *  - for ifcmd NVETHERNET_LINUX_PIF$ETHER_MC_DMA_ROUTE, DMA hotbit as input
	 *  - for ifcmd NVETHERNET_LINUX_PIF$ETHER_PAD_CALIBRATION, enable(1)/disable(0) as input */
	nveu32_t qinx;
	/** The private ioctl command number
	 * - Valid range: should be defined in private IOCTL group
	 * - Expected values:
	 *    - NVETHERNET_LINUX_PIF$EQOS_GET_CONNECTED_SPEED
	 *    - NVETHERNET_LINUX_PIF$ETHER_AVB_ALGORITHM
	 *    - NVETHERNET_LINUX_PIF$EQOS_L3L4_FILTER_CMD
	 *    - NVETHERNET_LINUX_PIF$EQOS_VLAN_FILTERING_CMD
	 *    - NVETHERNET_LINUX_PIF$ETHER_PTP_RXQUEUE
	 *    - NVETHERNET_LINUX_PIF$ETHER_CONFIG_EST
	 *    - NVETHERNET_LINUX_PIF$ETHER_CONFIG_FPE
	 *    - NVETHERNET_LINUX_PIF$ETHER_CONFIG_FRP_CMD
	 *    - NVETHERNET_LINUX_PIF$ETHER_MC_DMA_ROUTE
	 *    - NVETHERNET_LINUX_PIF$ETHER_PAD_CALIBRATION
	 *    - NVETHERNET_LINUX_PIF$ETHER_CAP_TSC_PTP
	 *    - NVETHERNET_LINUX_PIF$ETHER_M2M_TSYNC
	 *    - NVETHERNET_LINUX_PIF$ETHER_L2_ADDR */
	nveu32_t ifcmd;
	/** Used to query the connected phy link speed
	 */
	nveu32_t connected_speed;
	/** The return value of IOCTL handler func
	 * - Expected values: Linux Error codes
	 *   - for ifcmd NVETHERNET_LINUX_PIF$EQOS_GET_CONNECTED_SPEED connected speed
	 */
	nve32_t command_error;
	/** IOCTL cmd specific structure pointer
	 * - Valid range: A valid pointer to the IOCTL private structure data
	 *   - for ifcmd NVETHERNET_LINUX_PIF$ETHER_AVB_ALGORITHM
	 *       typecast as NVETHERNETRM_PIF$osi_core_avb_algorithm
	 *   - for ifcmd NVETHERNET_LINUX_PIF$EQOS_L3L4_FILTER_CMD
	 *       typecast as NVETHERNETRM_PIF$osi_l3_l4_filter
	 *   - for ifcmd NVETHERNET_LINUX_PIF$EQOS_VLAN_FILTERING_CMD
	 *       typecast as NVETHERNETRM_PIF$osi_vlan_filter
	 *   - for ifcmd NVETHERNET_LINUX_PIF$ETHER_CONFIG_EST
	 *       typecast as NVETHERNETRM_PIF$osi_est_config
	 *   - for ifcmd NVETHERNET_LINUX_PIF$ETHER_CONFIG_FPE
	 *       typecast as NVETHERNETRM_PIF$osi_fpe_config
	 *   - for ifcmd NVETHERNET_LINUX_PIF$ETHER_CONFIG_FRP_CMD
	 *       typecast as NVETHERNETRM_PIF$osi_core_frp_cmd
	 *   - for ifcmd NVETHERNET_LINUX_PIF$ETHER_CAP_TSC_PTP
	 *       typecast as NVETHERNETRM_PIF$osi_core_ptp_tsc_data
	 *   - for ifcmd NVETHERNET_LINUX_PIF$ETHER_M2M_TSYNC
	 *       typecast as NVETHERNETRM_PIF$OSI_ENABLE/NVETHERNETRM_PIF$OSI_DISABLE
	 *   - for ifcmd NVETHERNET_LINUX_PIF$ETHER_L2_ADDR
	 *       typecast as NVETHERNET_LINUX_PIF$ether_l2_filter */
	void *ptr;
	/** MAC instance ID (eqos:0 mgbe0:1 mgbe1:2 mgbe2:3 mgbe3:4) */
	nve32_t mac_id;
};

enum nv_macsec_nl_commands {
	/** MACSEC netlink command for MACSEC HW initilization */
	NV_MACSEC_CMD_INIT,
	/** MACSEC netlink command to get next PN */
	NV_MACSEC_CMD_GET_TX_NEXT_PN,
	/** MACSEC netlink command to set replay protection */
	NV_MACSEC_CMD_SET_REPLAY_PROT,
	/** MACSEC netlink command to set Cipher */
	NV_MACSEC_CMD_SET_CIPHER,
	/** MACSEC netlink command to create TX SA */
	NV_MACSEC_CMD_CREATE_TX_SA,
	/** MACSEC netlink command to enable TX SA */
	NV_MACSEC_CMD_EN_TX_SA,
	/** MACSEC netlink command to disable TX SA */
	NV_MACSEC_CMD_DIS_TX_SA,
	/** MACSEC netlink command to create RX SA */
	NV_MACSEC_CMD_CREATE_RX_SA,
	/** MACSEC netlink command to enable RX SA */
	NV_MACSEC_CMD_EN_RX_SA,
	/** MACSEC netlink command to disable RX SA */
	NV_MACSEC_CMD_DIS_RX_SA,
	/**MACSEC netlink command to TZ config */
	NV_MACSEC_CMD_TZ_CONFIG,
	/**MACSEC netlink command to TZ reset */
	NV_MACSEC_CMD_TZ_KT_RESET,
	/** MACSEC netlink command to de-initialize the MACSEC HW */
	NV_MACSEC_CMD_DEINIT,
};

#endif /* ETHER_EXPORT_H */
