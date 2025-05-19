/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved */

#ifndef ETHER_CALLBACK_EXPORT_H
#define ETHER_CALLBACK_EXPORT_H

#ifdef DOXYGEN
#define __user
#endif /* DOXYGEN */

/**
 * @brief
 * Description:
 * - Ethernet platform driver probe.
 *
 * @param[in] pdev:
 *  - Platform device associated with platform driver.
 *  - Used Structure variables: pdev->dev, pdev->dev.of_node
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: Yes
 *      - Run time: No
 *      - De-initialization: No
 *
 * @return
 * - EOK on success
 * - "-ENOMEM" on memory allocation failure for private data structures.
 * - "-EINVAL" on invalid input data values.
 * - "-ENOMEM" on  NVETHERNETRM_PIF#osi_get_core fail.
 * - "-ENOMEM" on  NVETHERNETRM_PIF#osi_get_dma fail.
 * - Return values of NVETHERNETRM_PIF#osi_handle_ioctl with
 *   the NVETHERNETRM_PIF$OSI_CMD_GET_HW_FEAT command.
 * - Return vlaues of register_netdev() on Net device regitration fail.
 * - Return vlaues of sysfs_create_group() on sysfs group create fail.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - Get the number of channels from DT.
 * - Allocate the network device for those many channels.
 * - Parse MAC and PHY DT.
 * - Get all required clks/reset/IRQ's.
 * - Register MDIO bus and network device.
 * - Initialize spinlock.
 * - Update filter value based on HW feature.
 * - Update osi_core->hw_feature with pdata->hw_feat pointer
 * - Initialize Workqueue to read MMC counters periodically.
 *
 */
#endif /* DOXYGEN_ICD */
int ether_probe(struct platform_device *pdev);

/**
 * @brief
 * Description:
 * - Ethernet platform driver remove.
 *
 * @param[in] pdev:
 *  - Platform device associated with platform driver.
 *  - The ``pdev`` structure variable is used to retrieve pointers to struct net_device and the private data structure.
 *  - Used Structure variables: pdev->dev.of_node
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: No
 *      - De-initialization: Yes
 *
 * @pre Ethernet driver probe NVETHERNET_LINUX_PIF#ether_probe event
 * need to be completed successfully.
 *
 * @return
 * - EOK on after clean-up.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Alogorithm:
 * - Release all the resources
 *
 */
#endif /* DOXYGEN_ICD */
int ether_remove(struct platform_device *pdev);

/**
 * @brief
 * Description:
 * - Ethernet platform driver shutdown.
 *
 * @param[in] pdev:
 *  - Platform device associated with platform driver.
 *  - The pdev structure variable is used to retrieve pointers to struct net_device and the private data structure.
 *  - Used Structure variables: None
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: No
 *      - De-initialization: Yes
 *
 * @pre Ethernet driver probe NVETHERNET_LINUX_PIF#ether_probe event
 * need to be completed successfully with ethernet network device created.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Alogorithm:
 * - Stops and Deinits PHY, MAC, DMA and Clks hardware
 * - Release SW allocated resources(buffers, workqueues etc)
 *
 */
#endif /* DOXYGEN_ICD */
void ether_shutdown(struct platform_device *pdev);

/**
 * @brief
 * Description:
 * - Ethernet platform driver suspend noirq callback.
 *
 * @param[in] dev:
 *  - Platform device associated with platform driver.
 *  - The ``dev`` structure variable is used to retrieve pointers to struct net_device and the private data structure.
 *  - Used Structure variables: None
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: No
 *      - De-initialization: Yes
 *
 * @pre Ethernet driver probe NVETHERNET_LINUX_PIF#ether_probe event
 * need to be completed successfully with ethernet network device created.
 *
 * @return
 * - EOK on success
 * - "-EBUSY" on NVETHERNETRM_PIF#osi_handle_ioctl with NVETHERNETRM_PIF$OSI_CMD_SUSPEND fail.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Alogorithm:
 * - Stops all data queues and PHY if the device
 *  does not wake capable.
 * - Disable TX and NAPI.
 * - Deinit OSI core, DMA and TX/RX interrupts.
 *
 */
#endif /* DOXYGEN_ICD */
int ether_suspend_noirq(struct device *dev);

/**
 * @brief
 * Description:
 * - Ethernet platform driver resume noirq callback.
 *
 * @param[in] dev:
 *  - Net device data structure.
 *  - The ``dev`` structure variable is used to retrieve pointers to struct net_device and the private data structure.
 *  - Used Structure variables: None
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: Yes
 *      - Run time: No
 *      - De-initialization: No
 *
 * @pre Ethernet driver probe NVETHERNET_LINUX_PIF#ether_probe event
 * need to be completed successfully with ethernet network device created.
 *
 * @return
 * - EOK on success
 * - Return values of NVETHERNETRM_PIF#osi_handle_ioctl,
 *   with the NVETHERNETRM_PIF$OSI_CMD_RESUME command data.
 * - Return values of NVETHERNETRM_PIF#osi_handle_ioctl,
 *   with the NVETHERNETRM_PIF$OSI_CMD_PAD_CALIBRATION command data.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Alogorithm:
 * - Enable clocks and perform resume sequence
 *
 */
#endif /* DOXYGEN_ICD */
int ether_resume_noirq(struct device *dev);

/**
 * @brief
 * Description:
 * - Call back to handle bring up of Ethernet interface
 *
 * @param[in] dev:
 *  - Net device data structure.
 *  - The ``dev`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: Yes
 *      - Run time: No
 *      - De-initialization: No
 *
 * @pre Ethernet driver probe NVETHERNET_LINUX_PIF#ether_probe event
 * need to be completed successfully with ethernet network device created.
 *
 * @return
 * - EOK on success
 * - "-EINVAL" on invalid input data values.
 * - Return values of NVETHERNETRM_PIF#osi_hw_core_init value.
 * - Return values of NVETHERNETRM_PIF#osi_handle_ioctl,
 *   with the NVETHERNETRM_PIF$OSI_CMD_L2_FILTER command data.
 * - Return values of NVETHERNETRM_PIF#osi_hw_core_init.
 * - Return values of NVETHERNETRM_PIF#osi_hw_dma_init.
 * - Return values of NVETHERNETRM_PIF#osi_handle_ioctl,
 *   with the NVETHERNETRM_PIF$OSI_CMD_FREE_TS command data.
 * - Return values of NVETHERNETRM_PIF#osi_handle_ioctl,
 *   with the NVETHERNETRM_PIF$OSI_CMD_PAD_CALIBRATION command data.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm: This routine takes care of below
 * - PHY initialization
 * - request tx/rx/common irqs
 * - HW initialization
 * - OSD private data structure initialization
 * - Starting the PHY
 *
 */
#endif /* DOXYGEN_ICD */
int ether_open(struct net_device *dev);

/**
 * @brief
 * Description:
 * - Call back to handle bring down of Ethernet interface
 *
 * @param[in] ndev:
 *  - Net device structure.
 *  - The ``ndev`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: No
 *      - De-initialization: Yes
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @return
 * - EOK on success
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm: This routine takes care of below
 * - Stopping PHY
 * - Freeing tx/rx/common irqs
 *
 */
#endif /* DOXYGEN_ICD */
int ether_close(struct net_device *ndev);

/**
 * @brief
 * Description:
 * - Network layer hook for data transmission.
 *
 * @param[in] skb:
 *  - SKB data structure.
 *  - Used Structure variables: skb->len, skb->gso_type,
 *    skb->gso_size, skb->ip_summed, skb->tx_flags, skb->data
 * @param[in] ndev:
 *  - Net device structure.
 *  - The ``ndev`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @return
 * - NETDEV_TX_OK on successful packet queue insertion.
 * - NETDEV_TX_BUSY if the queue is full.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - Allocate software context (DMA address for the buffer) for the data.
 * - Invoke OSI for data transmission.
 *
 */
#endif /* DOXYGEN_ICD */
int ether_start_xmit(struct sk_buff *skb, struct net_device *ndev);

/**
 * @brief
 * Description:
 * - VM based ISR routine for receive done.
 *
 * @param[in] irq:
 *  - IRQ number
 * @param[in] data:
 *  - VM IRQ data private data structure.
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: Yes
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @return
 * - IRQ_HANDLED on success handling of the TX Done.
 * - IRQ_HANDLED on success handling of the RX packet.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - Get global DMA status (common for all VM IRQ's)
 * + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + +
 * + RX7 + TX7 + RX6 + TX6 + . . . . . . . + RX1 + TX1 + RX0 + TX0 +
 * + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + +
 *
 * - Mask the channels which are specific to VM in global DMA status.
 * - Process all DMA channel interrupts which are triggered the IRQ
 *	a) Find first first set from LSB with ffs
 *	b) The least significant bit is position 1 for ffs. So decremented
 *	by one to start from zero.
 *	c) Get channel number and TX/RX info by using bit position.
 *	d) Invoke OSI layer to clear interrupt source for DMA Tx/Rx at
 *	DMA and wrapper level.
 *	e) Get NAPI instance based on channel number and schedule the same.
 *
 */
#endif /* DOXYGEN_ICD */
irqreturn_t ether_vm_isr(int irq, void *data);

/**
 * @brief
 * Description:
 * - Network stack ndo_eth_ioctl callback hook to driver.
 *
 * @param[in] dev:
 *  - network device structure
 *  - The ``dev`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 * @param[inout] rq:
 *  - Interface request structure used for device.
 *  - The ``rd`` structure variable is used to retrieve pointers
 *    to the mii_ioctl_data data structure and hwtstamp_config structure.
 *  - Used Structure variables:
 *      - For SIOCGMIIPHY: mii_ioctl_data->phy_id, mii_ioctl_data->reg_num
 *      - For SIOCGMIIREG: mii_ioctl_data->phy_id, mii_ioctl_data->reg_num
 *      - For SIOCSMIIREG:: mii_ioctl_data->phy_id, mii_ioctl_data->reg_num
 *      - For SIOCSHWTSTAMP: hwtstamp_config->flags, hwtstamp_config->tx_type,
 *        hwtstamp_config->rx_filter
 * @param[in] cmd:
 *  - Net device IOCTL command.
 *  - SIOCGMIIPHY: read register from the current PHY.
 *  - SIOCGMIIREG: read register from the specified PHY.
 *  - SIOCSMIIREG: set a register on the specified PHY.
 *  - SIOCSHWTSTAMP: Configure hardware time stamping.
 *  - SIOCGHWTSTAMP: Get hardware time stamp configuration.
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @return
 * - EOK on success
 * - "-EINVAL" on invalid input data values.
 * - "-EINVAL" on invalid PHY register read/write operatinos.
 * - Return values of NVETHERNETRM_PIF#osi_read_phy_reg for SIOCGMIIREG cmd value.
 * - Return values of NVETHERNETRM_PIF#osi_read_phy_reg for SIOCSMIIREG cmd value.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - Invokes MII API for phy read/write based on IOCTL command
 *
 */
#endif /* DOXYGEN_ICD */
int ether_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);

/**
 * @brief
 * Description:
 * - Network stack ndo_siocdevprivate callback hook to driver.
 *
 * @param[in] dev:
 *  - network device structure
 *  - The ``dev`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 * @param[inout] rq:
 *  - Interface request structure used for device.
 *  - The ``rd`` structure variable is used to retrieve pointers
 *    to the private ioctl ether_exported_ifr_data structure.
 *  - Used Structure variables:
 *     - For all supported private IOCTLs Refer NVETHERNET_LINUX_PIF$ether_exported_ifr_data
 * @param[in] data: Not used
 *  - Pointer to the user data for the IOCTL.
 * @param[in] cmd:
 *  - Net device IOCTL command, valid values:
 *     - SIOCDEVPRIVATE: For Net device private IOCTLs
 *     - NVETHERNET_LINUX_PIF$ETHER_PRV_RMDIO_IOCTL
 *     - NVETHERNET_LINUX_PIF$ETHER_PRV_WMDIO_IOCTL
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @return
 * - EOK on success
 * - "-EINVAL" on invalid input data values.
 * - "-EINVAL" if called before NVETHERNET_LINUX_PIF#ether_open event success.
 * - "-EOPNOTSUPP" on unsupported valued in cmd or rd
 * - "-EFAULT" on user IOCTL data copy
 * - "-EPERM" on user non admin user call
 * - Return values of NVETHERNETRM_PIF#osi_handle_ioctl for the cmd value SIOCDEVPRIVATE,
 *   with the NVETHERNET_LINUX_PIF$ether_exported_ifr_data.ifcmd in the "rd" input parameter.
 * - Return values of NVETHERNETRM_PIF#osi_read_phy_reg for the cmd value ETHER_PRV_RMDIO_IOCTL.
 * - Return values of NVETHERNETRM_PIF#osi_write_phy_reg for the cmd value ETHER_PRV_WMDIO_IOCTL.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - Handle below Linux NDO private IOCTL's
 *
 */
#endif /* DOXYGEN_ICD */
int ether_siocdevprivate(struct net_device *dev, struct ifreq *rq,
			 void __user *data, int cmd);
/**
 * @brief
 * Description:
 * - TC HW offload support
 *
 * @param[in] ndev:
 *  - Network device structure
 *  - The ``ndev`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 * @param[in] type:
 *  - qdisc type supported values:
 *    - TC_SETUP_QDISC_TAPRIO
 *    - TC_SETUP_QDISC_CBS
 * @param[in] type_data:
 *  - void pointer having user passed configuration struct tc_taprio_qopt_offload
 *  - Used Structure variables: tc_taprio_qopt_offload->num_entries,
 *    tc_taprio_qopt_offload->base_time, tc_taprio_qopt_offload->cycle_time
 *    tc_taprio_qopt_offload->enable, tc_taprio_qopt_offload->cmd
 *    tc_taprio_qopt_offload->entries[i].interval,
 *    tc_taprio_qopt_offload->entries[i].command
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @return
 * - EOK on success
 * - "-ERANGE" invalid number of GCL entries.
 * - "-ERANGE" invalid base time.
 * - "-ERANGE" invalid cycle time.
 * - "-EINVAL" invalid input argument.
 * - "-EOPNOTSUPP" Not supported TC set-up types and commands.
 * - Return values of NVETHERNETRM_PIF#osi_handle_ioctl with
 *   the NVETHERNETRM_PIF$OSI_CMD_SET_AVB command.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - Check the TC setup type
 * - Call appropriate function based on type.
 *
 */
#endif /* DOXYGEN_ICD */
int ether_setup_tc(struct net_device *ndev, enum tc_setup_type type,
		   void *type_data);

/**
 * @brief
 * Description:
 * - Change MAC MTU size
 *
 * @param[in] ndev:
 *  - Network device structure
 *  - The ``ndev`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 * @param[in] new_mtu:
 *  - New MTU size to set.
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre Ethernet interface need to be down with NVETHERNET_LINUX_PIF#ether_close event
 *
 * @return
 * - EOK on success
 * - "-EINVAL" If the new_mtu is more than NVETHERNETRM_PIF$OSI_MTU_SIZE_9000
 * - Return values of NVETHERNETRM_PIF#osi_handle_ioctl with
 *   the NVETHERNETRM_PIF$OSI_CMD_MAC_MTU command.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - Check and return if interface is up.
 * - Stores new MTU size set by user in OSI core data structure.
 */
#endif /* DOXYGEN_ICD */
int ether_change_mtu(struct net_device *ndev, int new_mtu);

/**
 * @brief
 * Description:
 * - Change HW features for the given network device.
 *
 * @param[in] ndev:
 *  - Network device structure
 *  - The ``ndev`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 * @param[in] feat:
 *  - Net device HW features flags type of u64.
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @return
 * - EOK on RX checksum enable/diable success.
 * - Return values of NVETHERNETRM_PIF#osi_handle_ioctl with
 *   the NVETHERNETRM_PIF$OSI_CMD_RXCSUM_OFFLOAD command.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - Check if HW supports feature requested to be changed
 * - If supported, check the current status of the feature and if it
 * needs to be toggled, do so.
 */
#endif /* DOXYGEN_ICD */
int ether_set_features(struct net_device *ndev, netdev_features_t feat);

/**
 * @brief
 * Description:
 * - This function is used to set RX mode.
 *
 * @param[in] dev
 *  - pointer to net_device structure.
 *  - The ``dev`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre MAC and PHY need to be initialized, by invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm: Based on Network interface flag, MAC registers are programmed to
 * set mode.
 */
#endif /* DOXYGEN_ICD */
void ether_set_rx_mode(struct net_device *dev);

/**
 * @brief
 * Description:
 * - Select queue based on user priority
 *
 * @param[in] dev:
 *  - Network device pointer
 *  - The ``dev`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 * @param[in] skb:
 *  - sk_buff pointer, buffer data to send
 *  - Used Structure variables: skb->priority
 * @param[in] sb_dev:
 *  - sk_buff Network device pointer
 *  - Used Structure variables: None
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @return
 * - Transmit queue index on successful
 *    execution of the select queue  operation.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - Select the correct queue index based which has priority of queue
 * same as skb->priority
 * - default select queue array index 0
 *
 */
#endif /* DOXYGEN_ICD */
unsigned short ether_select_queue(struct net_device *dev,
				  struct sk_buff *skb,
				  struct net_device *sb_dev);

/**
 * @brief
 * Description:
 * - This function returns a set of strings that describe
 *   the requested objects.
 *
 * @param[in] dev:
 *  - Pointer to net device structure.
 *  - The ``dev`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 * @param[in] stringset:
 *  - String set value.
 * @param[in] data:
 *  - Pointer in which requested string should be put.
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - return number of strings.
 *
 */
#endif /* DOXYGEN_ICD */
void ether_get_strings(struct net_device *dev, u32 stringset, u8 *data);

/**
 * @brief
 * Description:
 * - Adjust link call back for Linux PHY sus system.
 *
 * @param[in] dev:
 *  - Pointer to net device structure.
 *  - The ``dev`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - Callback function called by the PHY subsystem
 * whenever there is a link detected or link changed on the
 * physical layer.
 *
 */
#endif /* DOXYGEN_ICD */
void ether_adjust_link(struct net_device *dev);

/**
 * @brief
 * Description:
 * - This function is invoked by kernel when user requests to get the
 *   extended statistics about the device.
 *
 * @param[in] dev:
 *  - Pointer to net device structure.
 *  - The ``dev`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 * @param[in] dummy:
 *  - Pointer to the ethtool_stats structure type.
 *  - Used Structure variables: None
 * @param[in] data:
 *  - Pointer in which MMC statistics should be put.
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - Read mmc register and create strings
 *
 */
#endif /* DOXYGEN_ICD */
void ether_get_ethtool_stats(struct net_device *dev,
			     struct ethtool_stats *dummy,
			     u64 *data);

/**
 * @brief
 * Description:
 * - This function gets number of strings
 *
 * @param[in] dev:
 *  - Pointer to net device structure.
 *  - The ``dev`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 * @param[in] sset:
 *  - String set value.
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @return
 * - Size stats when sset is ETH_SS_STATS.
 * - Count of self test when sset is ETH_SS_TEST.
 * - "-EOPNOTSUPP" if sset is other than ETH_SS_TEST or ETH_SS_STATS.

 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - Return number of strings.
 *
 */
#endif /* DOXYGEN_ICD */
int ether_get_sset_count(struct net_device *dev, int sset);

#ifndef OSI_STRIPPED_LIB
/**
 * @brief
 * Description:
 * - This function is invoked by kernel when user request to get report
 *   whether wake-on-lan is enable or not.
 *
 * @param[in] ndev:
 *  - Pointer to net device structure.
 *  - The ``ndev`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 * param[in] wol
 *  – pointer to ethtool_wolinfo structure.
 *  - Used Structure variables: wol->supported, wol->wolopts.
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - Return Wake On Lan status in wol param
 *
 */
#endif /* DOXYGEN_ICD */
void ether_get_wol(struct net_device *ndev, struct ethtool_wolinfo *wol);

/**
 * @brief
 * Description:
 * - This function is invoked by kernel when user request to set
 *   pmt parameters for remote wakeup or magic wakeup
 *
 * @param[in] ndev:
 *  - Pointer to net device structure.
 *  - The ``ndev`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 * param[in] wol
 *  – pointer to ethtool_wolinfo structure.
 *  - Used Structure variables: wol->wolopts.
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @return
 * - EOK on success
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - Enable or Disable Wake On Lan status based on wol param
 *
 */
#endif /* DOXYGEN_ICD */
int ether_set_wol(struct net_device *ndev, struct ethtool_wolinfo *wol);

/**
 * @brief
 * Description:
 * - Ethernet selftests.
 *
 * @param[in] ndev:
 *  - Pointer to net device structure.
 *  - The ``ndev`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 * @param[in] etest:
 *  - Ethernet ethtool test pointer.
 *  - Used Structure variables: etest->flags
 * @param[in] buf:
 *  - Buffer pointer to hold test status.
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - Enable or Disable Wake On Lan status based on wol param
 *
 */
#endif /* DOXYGEN_ICD */
void ether_selftest_run(struct net_device *dev,
			struct ethtool_test *etest, u64 *buf);

/**
 * @brief
 * Description:
 * - Get RX flow classification rules
 *
 * @param[in] ndev:
 *  - Pointer to net device structure.
 *  - The ``ndev`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 * param[in] rxnfc:
 *  - Pointer to rxflow data
 *  - Used Structure variables: rxnfc->cmd
 * param[in] rule_locs:
 *  - Rule location number
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @return
 * - EOK on success
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - Returns RX flow classification rules.
 *
 */
#endif /* DOXYGEN_ICD */
int ether_get_rxnfc(struct net_device *ndev,
		    struct ethtool_rxnfc *rxnfc,
		    u32 *rule_locs);

/**
 * @brief
 * Description:
 * - Get pause frame settings
 *
 * @param[in] ndev:
 *  - Pointer to net device structure.
 *  - The ``ndev`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 * @param[out] pause:
 *  - Pause parameters that are set currently
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @return
 * - EOK on success
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - Gets pause frame configuration
 *
 */
#endif /* DOXYGEN_ICD */
void ether_get_pauseparam(struct net_device *ndev,
			  struct ethtool_pauseparam *pause);

/**
 * @brief
 * Description:
 * - Set pause frame settings
 *
 * @param[in] ndev:
 *  - Pointer to net device structure.
 *  - The ``ndev`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 * @param[in] pause:
 *  - Pause parameters that  needs to set into MAC.
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @return
 * - EOK on success
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - Sets pause frame settings
 *
 */
#endif /* DOXYGEN_ICD */
int ether_set_pauseparam(struct net_device *ndev,
			 struct ethtool_pauseparam *pause);

/**
 * @brief
 * Description:
 * - Get the size of the RX flow hash key
 *
 * @param[in] ndev:
 *  - Pointer to net device structure.
 *  - The ``ndev`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @return
 * - Size of RSS Hash key
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - Returns size of RSS hash key
 *
 */
#endif /* DOXYGEN_ICD */
u32 ether_get_rxfh_key_size(struct net_device *ndev);

/**
 * @brief
 * Description:
 * - Get the size of the RX flow hash indirection table
 *
 * @param[in] ndev:
 *  - Pointer to net device structure.
 *  - The ``ndev`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @return
 * - Size of RSS Hash table
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - Returns size of the RX flow hash indirection table
 *
 */
#endif /* DOXYGEN_ICD */
u32 ether_get_rxfh_indir_size(struct net_device *ndev);

/**
 * @brief
 * Description:
 * - Get the debug message level of the driver
 *
 * @param[in] ndev:
 *  - Pointer to net device structure.
 *  - The ``ndev`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @return
 * - message level set in the driver
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - Returns the message level set in the driver.
 *
 */
#endif /* DOXYGEN_ICD */
unsigned int ether_get_msglevel(struct net_device *ndev);

/**
 * @brief
 * Description:
 * - Set the debug message level into the driver
 *
 * @param[in] ndev:
 *  - Pointer to net device structure.
 *  - The ``ndev`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 * @param[in] level:
 *  - debug message level
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - Returns the message level set in the driver.
 *
 */
#endif /* DOXYGEN_ICD */
void ether_set_msglevel(struct net_device *ndev, u32 level);
#endif /* OSI_STRIPPED_LIB */

/* Work Queue functions */
/**
 * @brief
 * Description:
 * - Work Queue function to call NVETHERNETRM_PIF#osi_handle_ioctl with
 *   NVETHERNETRM_PIF$OSI_CMD_READ_MMC IOCTL command periodically.
 *
 * @param[in] work:
 *  - work structure
 *  - The ``work`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - Calls NVETHERNETRM_PIF#osi_handle_ioctl with
 *   NVETHERNETRM_PIF$OSI_CMD_READ_MMC IOCTL command data
 *   in periodic manner to avoid possibility of
 *   overrun of 32 bit MMC hw registers.
 *
 */
#endif /* DOXYGEN_ICD */
void ether_stats_work_func(struct work_struct *work);

/**
 * @brief
 * Description:
 * - Work Queue function to call set speed.
 *
 * @param[in] work:
 *  - work structure
 *  - The ``work`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 *
 */
#endif /* DOXYGEN_ICD */
void set_speed_work_func(struct work_struct *work);

/**
 * @brief
 * Description:
 * - Gets timestamp and update skb
 *
 * @param[in] work:
 *  - Work to handle SKB list update
 *  - The ``work`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - Parse through tx_ts_skb_head.
 * - Issue osi_handle_ioctl(OSI_CMD_GET_TX_TS) to read timestamp.
 * - Update skb with timestamp and give to network stack
 * - Free skb and node.
 *
 */
#endif /* DOXYGEN_ICD */
void ether_get_tx_ts_work(struct work_struct *work);

/**
 * @brief
 * Description:
 * - Tasklet to restart the lane bring-up.
 *
 * @param[in] t:
 *  - Tasklet structure
 *  - The ``t`` structure variable is used to retrieve pointers to the private data structure.
 *  - Used Structure variables: None
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 *
 */
#endif /* DOXYGEN_ICD */
void ether_restart_lane_bringup_task(struct tasklet_struct *t);

/**
 * @brief
 * Description:
 * - Adjust MAC hardware frequency
 *
 * @param[in] ptp:
 *  - Pointer to ptp_clock_info structure.
 * @param[in] scaled_ppm:
 *  - Desired period change in parts per million.
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @return
 * - EOK on NVETHERNETRM_PIF$OSI_CMD_ADJ_FREQ IOCTL success
 * - Return values of NVETHERNETRM_PIF#osi_handle_ioctl with
 *   the NVETHERNETRM_PIF$OSI_CMD_ADJ_FREQ IOCTL command.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - This function is used to adjust the frequency of the hardware clock.
 *
 */
#endif /* DOXYGEN_ICD */
int ether_adjust_clock(struct ptp_clock_info *ptp, long scaled_ppm);

/**
 * @brief
 * Description:
 * - Adjust MAC hardware time
 *
 * @param[in] ptp:
 *  - Pointer to ptp_clock_info structure.
 * @param[in] nsec_delta:
 *  - Desired change in nanoseconds w.r.t System time
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @return
 * - EOK on NVETHERNETRM_PIF$OSI_CMD_ADJ_TIME IOCTL success
 * - Return values of NVETHERNETRM_PIF#osi_handle_ioctl with
 *   the NVETHERNETRM_PIF$OSI_CMD_ADJ_TIME IOCTL command.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - function is used to shift/adjust the time of the hardware clock.
 *
 */
#endif /* DOXYGEN_ICD */
int ether_adjust_time(struct ptp_clock_info *ptp, s64 nsec_delta);

/**
 * @brief
 * Description:
 * - Gets current hardware time
 *
 * @param[in] ptp:
 *  - Pointer to ptp_clock_info structure.
 * @param[in] ts:
 *  - Pointer to hole time.
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @return
 * - EOK on successful read of the hardware time.
 * - Return values of NVETHERNETCL_PIF#osi_dma_get_systime_from_mac.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - This function is used to read the current time from the
 * hardware clock
 *
 */
#endif /* DOXYGEN_ICD */
int ether_get_time(struct ptp_clock_info *ptp, struct timespec64 *ts);

/**
 * @brief
 * Description:
 * - Set current system time to MAC Hardware
 * - Gets current hardware time
 *
 * @param[in] ptp:
 *  - Pointer to ptp_clock_info structure.
 * @param[in] ts:
 *  - Time value to set.
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @return
 * - EOK on NVETHERNETRM_PIF$OSI_CMD_SET_SYSTOHW_TIME IOCTL success
 * - Return values of NVETHERNETRM_PIF#osi_handle_ioctl with
 *   the NVETHERNETRM_PIF$OSI_CMD_SET_SYSTOHW_TIME command.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/*
 * @note
 * Algorithm:
 * - This function is used to set the current time to the
 * hardware clock.
 *
 */
#endif /* DOXYGEN_ICD */
int ether_set_time(struct ptp_clock_info *ptp, const struct timespec64 *ts);

/**
 * @brief
 * Description:
 * - Implementing the callback for
 *   NVETHERNETRM_PIF$osd_core_ops.restart_lane_bringup to restart the lane bringup process.
 *
 * @param[in] *priv:
 *  - Pointer to private data structure.
 * @param[in] en_disable:
 *  - Flag to enable (1) or disable (0) the bring-up.
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @dir
 * - forward
 *
 */
void osd_restart_lane_bringup(void *priv, unsigned int en_disable);

/**
 * @brief
 * Description:
 * - Implementing the callback for
 *   NVETHERNETRM_PIF$osd_core_ops.padctrl_mii_rx_pins to restart the PAD calibration process.
 *
 * @param[in] priv:
 *  - Pointer to private data structure.
 * @param[in] enable:
 *  - Flag to enable (1) or disable (0) the PAD calibration process.
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @dir
 * - forward
 *
 */
int ether_padctrl_mii_rx_pins(void *priv, unsigned int enable);

/**
 * @brief
 * Description:
 * - Implementing the callback for
 *   NVETHERNETRM_PIF$osd_core_ops.ivc_send to send IVC message.
 *
 * @param[in] priv:
 *  - Pointer to private data structure.
 * @param[in] ivc_buf:
 *  - Pointer to  NVETHERNETRM_PIF$ivc_msg_common data structure.
 * @param[in] len:
 *  - length of data
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: Yes
 *      - Run time: Yes
 *      - De-initialization: Yes
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @return
 * - EOK on success
 * - Data bandwidth on success for NVETHERNET_LINUX_PIF#ether_ioctl$ETHER_GET_AVB_PERF
 * - "-ETIMEDOUT" on readx_poll_timeout_atomic() timeout.
 * - Return vlaues of tegra_hv_ivc_read() on IVC read fail.
 * - Return vlaues of tegra_hv_ivc_write() on IVC write fail.
 *
 * @dir
 * - forward
 *
 */
int osd_ivc_send_cmd(void *priv, ivc_msg_common_t *ivc_buf,
		     unsigned int len);

/**
 * @brief
 * Description:
 * - Implementing the callback for
 *   NVETHERNETCL_PIF$osd_dma_ops.transmit_complete to report TX done.
 *
 * @param[in] priv:
 *  - OSD private data structure.
 * @param[in] swcx:
 *  - Pointer to swcx
 * @param[in] txdone_pkt_cx:
 *  - Pointer to struct which has tx done status info.
 *    This struct has flags to indicate tx error, whether DMA address
 *    is mapped from paged/linear buffer.
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @dir
 * - forward
 *
 */
void osd_transmit_complete(void *priv, const struct osi_tx_swcx *swcx,
			   const struct osi_txdone_pkt_cx
			   *txdone_pkt_cx);

/**
 * @brief
 * Description:
 * - Implementing the callback for
 *   NVETHERNETCL_PIF$osd_dma_ops.receive_packet to report DMA receive packet.
 *
 * @param[in] priv:
 *  - OSD private data structure.
 * @param[in] rx_ring:
 *  - Pointer to DMA channel Rx ring.
 * @param[in] chan:
 *  - DMA Rx channel number.
 * @param[in] dma_buf_len:
 *  - Rx DMA buffer length.
 * @param[in] rx_pkt_cx:
 *  - Received packet context.
 * @param[in] rx_swcx:
 *  - Received packet sw context.
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @dir
 * - forward
 *
 */
#ifndef DOXYGEN_ICD
/**
 *
 * Algorithm:
 * 1) Unmap the DMA buffer address.
 * 2) Updates socket buffer with len and ether type and handover to
 * Linux network stack.
 * 3) Refill the Rx ring based on threshold.
 *
 */
#endif /* DOXYGEN_ICD */
void osd_receive_packet(void *priv, struct osi_rx_ring *rx_ring,
			unsigned int chan, unsigned int dma_buf_len,
			const struct osi_rx_pkt_cx *rx_pkt_cx,
			struct osi_rx_swcx *rx_swcx);

/* MACSEC Callbacks */
/**
 * @brief
 * Description:
 * - Implementing the callback for NVETHERNET_LINUX_PIF$NV_MACSEC_CMD_INIT netlink command.
 *
 * @param[in] skb:
 *  - SKB data structure.
 *  - Used Structure variables: None
 * @param[in] info:
 *  - Pointer to the genl_info structure.
 *  - Used Structure variables: info->attrs
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: Yes
 *      - Run time: No
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @dir
 * - forward
 *
 */
int macsec_init(struct sk_buff *skb, struct genl_info *info);

/**
 * @brief
 * Description:
 * - Implementing the callback for
 *   NVETHERNET_LINUX_PIF$NV_MACSEC_CMD_SET_REPLAY_PROT netlink command.
 *
 * @param[in] skb:
 *  - SKB data structure.
 *  - Used Structure variables: None
 * @param[in] info:
 *  - Pointer to the genl_info structure.
 *  - Used Structure variables: info->attrs
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @dir
 * - forward
 *
 */
int macsec_set_replay_prot(struct sk_buff *skb, struct genl_info *info);

/**
 * @brief
 * Description:
 * - Implementing the callback for
 *   NVETHERNET_LINUX_PIF$NV_MACSEC_CMD_SET_CIPHER netlink command.
 *
 * @param[in] skb:
 *  - SKB data structure.
 *  - Used Structure variables: None
 * @param[in] info:
 *  - Pointer to the genl_info structure.
 *  - Used Structure variables: info->attrs
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @dir
 * - forward
 *
 */
int macsec_set_cipher(struct sk_buff *skb, struct genl_info *info);

/**
 * @brief
 * Description:
 * - Implementing the callback for
 *   NVETHERNET_LINUX_PIF$NV_MACSEC_CMD_DEINIT netlink command.
 *
 * @param[in] skb:
 *  - SKB data structure.
 *  - Used Structure variables: None
 * @param[in] info:
 *  - Pointer to the genl_info structure.
 *  - Used Structure variables: info->attrs
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: No
 *      - De-initialization: Yes
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @dir
 * - forward
 *
 */
int macsec_deinit(struct sk_buff *skb, struct genl_info *info);

/**
 * @brief
 * Description:
 * - Implementing the callback for
 *   NVETHERNET_LINUX_PIF$NV_MACSEC_CMD_EN_TX_SA netlink command.
 *
 * @param[in] skb:
 *  - SKB data structure.
 *  - Used Structure variables: None
 * @param[in] info:
 *  - Pointer to the genl_info structure.
 *  - Used Structure variables: info->attrs
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @dir
 * - forward
 *
 */
int macsec_en_tx_sa(struct sk_buff *skb, struct genl_info *info);

/**
 * @brief
 * Description:
 * - Implementing the callback for
 *   NVETHERNET_LINUX_PIF$NV_MACSEC_CMD_CREATE_TX_SA netlink command.
 *
 * @param[in] skb:
 *  - SKB data structure.
 *  - Used Structure variables: None
 * @param[in] info:
 *  - Pointer to the genl_info structure.
 *  - Used Structure variables: info->attrs
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @dir
 * - forward
 *
 */
int macsec_create_tx_sa(struct sk_buff *skb, struct genl_info *info);

/**
 * @brief
 * Description:
 * - Implementing the callback for
 *   NVETHERNET_LINUX_PIF$NV_MACSEC_CMD_DIS_TX_SA netlink command.
 *
 * @param[in] skb:
 *  - SKB data structure.
 *  - Used Structure variables: None
 * @param[in] info:
 *  - Pointer to the genl_info structure.
 *  - Used Structure variables: info->attrs
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @dir
 * - forward
 *
 */
int macsec_dis_tx_sa(struct sk_buff *skb, struct genl_info *info);

/**
 * @brief
 * Description:
 * - Implementing the callback for
 *   NVETHERNET_LINUX_PIF$NV_MACSEC_CMD_EN_RX_SA netlink command.
 *
 * @param[in] skb:
 *  - SKB data structure.
 *  - Used Structure variables: None
 * @param[in] info:
 *  - Pointer to the genl_info structure.
 *  - Used Structure variables: info->attrs
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @dir
 * - forward
 *
 */
int macsec_en_rx_sa(struct sk_buff *skb, struct genl_info *info);

/**
 * @brief
 * Description:
 * - Implementing the callback for
 *   NVETHERNET_LINUX_PIF$NV_MACSEC_CMD_CREATE_RX_SA netlink command.
 *
 * @param[in] skb:
 *  - SKB data structure.
 *  - Used Structure variables: None
 * @param[in] info:
 *  - Pointer to the genl_info structure.
 *  - Used Structure variables: info->attrs
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @dir
 * - forward
 *
 */
int macsec_create_rx_sa(struct sk_buff *skb, struct genl_info *info);

/**
 * @brief
 * Description:
 * - Implementing the callback for
 *   NVETHERNET_LINUX_PIF$NV_MACSEC_CMD_DIS_RX_SA netlink command.
 *
 * @param[in] skb:
 *  - SKB data structure.
 *  - Used Structure variables: None
 * @param[in] info:
 *  - Pointer to the genl_info structure.
 *  - Used Structure variables: info->attrs
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @dir
 * - forward
 *
 */
int macsec_dis_rx_sa(struct sk_buff *skb, struct genl_info *info);

/**
 * @brief
 * Description:
 * - Implementing the callback for
 *   NVETHERNET_LINUX_PIF$NV_MACSEC_CMD_GET_TX_NEXT_PN netlink command.
 *
 * @param[in] skb:
 *  - SKB data structure.
 *  - Used Structure variables: None
 * @param[in] info:
 *  - Pointer to the genl_info structure.
 *  - Used Structure variables: info->attrs
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None.
 * - API Group:
 *      - Initialization: No
 *      - Run time: Yes
 *      - De-initialization: No
 *
 * @pre This should be invoked following a successful
 *   execution of the NVETHERNET_LINUX_PIF#ether_open event.
 *
 * @dir
 * - forward
 *
 */
int macsec_get_tx_next_pn(struct sk_buff *skb, struct genl_info *info);

#endif /* ETHER_CALLBACK_EXPORT_H */
