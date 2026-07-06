/*
 *
 * If you received this File from Marvell, you may opt to use, redistribute and/or
 * modify this File in accordance with the terms and conditions of the General
 * Public License Version 2, June 1991 (the "GPL License"), a copy of which is
 * available along with the File in the license.txt file or by writing to the Free
 * Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 or
 * on the worldwide web at http://www.gnu.org/licenses/gpl.txt.
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE ARE EXPRESSLY
 * DISCLAIMED. The GPL License provides additional details about this warranty
 * disclaimer.
 *
 */
#include <linux/version.h>

#include "oak.h"

/* private function prototypes */
static int oak_probe(struct pci_dev *pdev, const struct pci_device_id *dev_id);
static void oak_remove(struct pci_dev *pdev);
static void oak_read_set_mac_address(struct pci_dev *pdev);

/* Oak driver name, version, and copyright */
const char oak_driver_name[] = OAK_DRIVER_NAME;
const char oak_driver_version[] = OAK_DRIVER_VERSION;
static const char oak_driver_string[] = OAK_DRIVER_STRING;
static const char oak_driver_copyright[] = OAK_DRIVER_COPYRIGHT;

/* Oak PCI device ID structure */
static struct pci_device_id oak_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_SYSKONNECT, 0x1000)},
	{PCI_DEVICE(0x11AB, 0x0000)}, /* FPGA board */
	{PCI_DEVICE(0x11AB, 0xABCD)}, /* FPGA board */
	{PCI_DEVICE(0x11AB, 0x0f13)},
	{PCI_DEVICE(0x11AB, 0x0a72)}, /* Oak */
	{0,} /* Terminate the table */
};

MODULE_DEVICE_TABLE(pci, oak_pci_tbl);

#ifdef CONFIG_PM_SLEEP
/* Device Power Management (DPM) support */
static const struct dev_pm_ops oak_dpm_ops = {
	.suspend = oak_dpm_suspend,
	.resume = oak_dpm_resume,
};
#endif

/* PCIe - interface structure */
static struct pci_driver oak_driver = {
	.name = oak_driver_name,
	.id_table = oak_pci_tbl,
	.probe = oak_probe,
	.remove = oak_remove,
#ifdef CONFIG_PM_SLEEP
	.driver.pm = &oak_dpm_ops,
#endif
};

/* Oak ethtool operation structure */
static const struct ethtool_ops oak_ethtool_ops = {
	.get_drvinfo = oak_ethtool_get_drvinfo,
	.get_ethtool_stats = oak_ethtool_get_stats,
	.get_strings = oak_ethtool_get_strings,
	.get_sset_count = oak_ethtool_get_sscnt,
	.get_link = ethtool_op_get_link,
	.get_msglevel = oak_dbg_get_level,
	.set_msglevel = oak_dbg_set_level,
	.get_link_ksettings = oak_ethtool_get_link_ksettings,
};

/* Oak netdevice operation structure */
static const struct net_device_ops oak_netdev_ops = {
	.ndo_open = oak_net_open,
	.ndo_stop = oak_net_close,
	.ndo_start_xmit = oak_net_xmit_frame,
	.ndo_do_ioctl = oak_net_ioctl,
	.ndo_set_mac_address = oak_net_set_mac_addr,
	.ndo_select_queue = oak_net_select_queue,
	.ndo_change_mtu = oak_net_esu_set_mtu,
	.ndo_validate_addr      = eth_validate_addr,
};

/* global variable declaration */
u32 debug = 0;
u32 txs = 2048;
u32 rxs = 2048;
int chan = MAX_NUM_OF_CHANNELS;
int rto = 100;
int mhdr;
u32 port_speed = 10;
int napi_wt = NAPI_POLL_WEIGHT;

/* software level defination */
#define SOFTWARE_INIT 10
#define HARDWARE_INIT 20
#define SOFTWARE_STARTED 40

/* Name      : oak_init_module
 * Returns   : int
 * Parameters:
 * Description : This function is the entry point for the driver registration.
 */
int oak_init_module(void)
{
	s32 retval = 0;

	pr_info("%s - (%s) version %s\n",
		oak_driver_string, oak_driver_name, oak_driver_version);
	pr_info("%s\n", oak_driver_copyright);

	/* Register Oak PCI driver with the linux kernel
	 * PCI device drivers call pci_register_driver() during their
	 * initialization with a pointer to a structure describing the
	 * driver (struct pci_driver)
	 */
	retval = pci_register_driver(&oak_driver);
	return retval;
}

/* Name      : exit_module
 * Returns   : void
 * Parameters:
 * Description : This function is the exit point for the driver.
 */
void oak_exit_module(void)
{
	/* Unregister Oak PCI driver from linux kernel */
	pci_unregister_driver(&oak_driver);
}

/* Name      : start_software
 * Returns   : int
 * Parameters:  struct pci_dev * pdev
 * Description: This function registers the oak device to napi
 */
static int oak_start_software(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	int retval = 0;

	netdev->ethtool_ops = &oak_ethtool_ops;

	oak_net_add_napi(netdev);
	/* register netdev will take a completed network device structure and
	 * add it to the kernel interfaces
	 */
	retval = register_netdev(netdev);

	return retval;
}

/* Name      : release_software
 * Returns   : void
 * Parameters:  struct pci_dev * pdev
 * Description : This function release the Ethernet interface.
 */
static void oak_release_software(struct pci_dev *pdev)
{
	int retval;
	struct net_device *netdev = pci_get_drvdata(pdev);
	oak_t *oak;

	oak = netdev_priv(netdev);

#ifdef CONFIG_PM
	/* Set the PCI device power state to D3hot */
	retval = pci_set_power_state(pdev, PCI_D3hot);
	if (retval == 0)
		pr_info("oak: Device power state D%d\n", pdev->current_state);
	else
		pr_err("oak: Failed to set the device power state err: %d\n",
		       retval);

	/* Remove sysfs entry */
	oak_dpm_remove_sysfs(oak);
#endif

	oakdbg(debug, PROBE, "pdev=%p ndev=%p", pdev, pci_get_drvdata(pdev));

	free_netdev(netdev);
}

/* Name        : oak_init_software
 * Returns     : int
 * Parameters  : struct pci_dev *pdev
 * Description : This function initializes the PCIe interface and network device
 */
static int oak_init_software(struct pci_dev *pdev)
{
	struct net_device *netdev = NULL;
	oak_t *oak;
	int retval = 0;

	/* Allocates and sets up an Ethernet device
	 * Fill in the fields of the device structure with Ethernet-generic
	 * values. Basically does everything except registering the device
	 */
	netdev = alloc_etherdev_mq(sizeof(oak_t), chan);

	if (netdev) {
		/* Set the sysfs physical device reference for the network
		 * logical device if set prior to registration will cause a
		 * symlink during initialization.
		 */
		SET_NETDEV_DEV(netdev, &pdev->dev);
		/* Set private driver data pointer for a pci_dev */
		pci_set_drvdata(pdev, netdev);
		oak = netdev_priv(netdev);
		oak->device = &pdev->dev;
		oak->netdev = netdev;
		oak->pdev = pdev;
#ifdef CONFIG_PM
		/* Create sysfs entry for D0, D1, D2 and D3 states testing */
		oak_dpm_create_sysfs(oak);
#endif
		/* Register network device operations */
		netdev->netdev_ops = &oak_netdev_ops;
		/* Set hardware's checksum Offload capabilities */
		netdev->features = oak_chksum_get_config();
		/* Set the min and max MTU size */
		oak_set_mtu_config(netdev);
		spin_lock_init(&oak->lock);

		/* Assign random MAC address */
		eth_hw_addr_random(netdev);
	} else {
		/* If software initialization fails then we need to release the
		 * device and free allocated structure with retnval as ENOMEM
		 */
		oak_release_software(pdev);
		retval = -ENOMEM;
	}

	oakdbg(debug, PROBE, "pdev=%p ndev=%p err=%d",
	       pdev, pci_get_drvdata(pdev), retval);

	return retval;
}

/* Name        : oak_probe
 * Returns     : int
 * Parameters  : struct pci_dev *pdev,  const struct pci_device_id *dev_id
 * Description : This function probe the device and call initialization
 * functions
 */
static int oak_probe(struct pci_dev *pdev, const struct pci_device_id *dev_id)
{
	int retval = 0;
	int err = 0;
	oak_t *adapter = NULL;

#ifdef CONFIG_PM
	/* Set PCI device power state to D0 */
	err = pci_set_power_state(pdev, PCI_D0);
	if (err == 0)
		pr_info("oak: Device power state D%d\n", pdev->current_state);
	else
		pr_err("oak: Failed to set the device power state err: %d\n",
		       err);
#endif
	/* Initialize the oak software */
	err = oak_init_software(pdev);

	if (err == 0) {
		struct net_device *netdev = pci_get_drvdata(pdev);

		adapter = netdev_priv(netdev);
		/* Set level as software initialization */
		adapter->level = SOFTWARE_INIT;

		/* Initialize the Oak hardware */
		err = oak_init_hardware(pdev);

		if (err == 0) {
			/* Read MAC from firmware registers and set to the
			 * device address register.
			 */
			oak_read_set_mac_address(pdev);
			err = oak_start_software(pdev);
			if (err == 0)
				/* Set level as driver software started */
				adapter->level = SOFTWARE_STARTED;
			else
				retval = err;
		} else {
			retval = err;
		}
	} else {
		retval = err;
	}

	if (err == 0) {
		retval = err;
		if (adapter->sw_base)
			pr_info("%s[%d] - ESU register access is supported",
				oak_driver_name, pdev->devfn);
	} else {
		oak_remove(pdev);
	}

	oakdbg(debug, PROBE, "pdev=%p ndev=%p err=%d", pdev,
	       pci_get_drvdata(pdev), err);

	return retval;
}

/* Name        : oak_get_msix_resources
 * Returns     : int
 * Parameters  : struct pci_dev * pdev
 * Description : This function allocates the MSIX resources.
 */
static int oak_get_msix_resources(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	oak_t *adapter = netdev_priv(dev);
	u32 num_irqs;
	u32 num_cpus = num_online_cpus();
	u32 i;
	int cnt;
	int retval = 0;

	num_irqs = sizeof(adapter->gicu.msi_vec) / sizeof(struct msix_entry);
	/* Return the number of device's MSI-X table entries */
	cnt = pci_msix_vec_count(pdev);

	if (cnt <= 0) {
		retval = -EFAULT;
	} else {
		if (cnt <= num_irqs)
			num_irqs = cnt;
		if (num_irqs > num_cpus)
			num_irqs = num_cpus;
		for (i = 0; i < num_irqs; i++) {
			adapter->gicu.msi_vec[i].vector = 0;
			adapter->gicu.msi_vec[i].entry = i;
		}
#ifdef OAK_MSIX_LEGACY
		/* Configure device's MSI-X capability structure - Setup the
		 * MSI-X capability structure of device function with a
		 * maximum possible number of interrupts in the range between
		 * minvec and maxvec upon its software driver call to request
		 * for MSI-X mode enabled on its hardware device function.
		 * It returns a negative errno if an error occurs.
		 * If it succeeds, it returns the actual number of interrupts
		 * allocated and indicates the successful configuration of
		 * MSI-X capability structure with new allocated MSI-X
		 * interrupts.
		 */
		retval = pci_enable_msix_range(pdev, adapter->gicu.msi_vec,
					       num_irqs, num_irqs);
#else
		/* Which allocates up to max_vecs interrupt vectors for a PCI
		 * device.
		 */
		retval = pci_alloc_irq_vectors(pdev, num_irqs, num_irqs,
					       PCI_IRQ_ALL_TYPES);
		if (retval) {
			pr_info("int vec count %d\n", retval);
			num_irqs = retval;
		}
#endif
		if (retval >= 0)
			retval = 0;

		adapter->gicu.num_ldg = num_irqs;
		oakdbg(debug, PROBE, "pdev=%p ndev=%p num_irqs=%d/%d retval=%d",
		       pdev, dev, num_irqs, cnt, retval);
	}
	return retval;
}

/* Name      : release_hardware
 * Returns   : void
 * Parameters:  struct pci_dev * pdev
 * Description : This function de initializes the hardware and
 * release the resources.
 */
void oak_release_hardware(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	oak_t *adapter = netdev_priv(dev);
	int err = 0;

	if (adapter->gicu.num_ldg > 0)
		pci_disable_msix(pdev);
	/* Release reserved PCI I/O and memory resources */
	pci_release_regions(pdev);
	pci_disable_device(pdev);

	oakdbg(debug, PROBE, "pdev=%p ndev=%p err=%d",
	       pdev, pci_get_drvdata(pdev), err);
}

/* Name        : oak_init_map_config
 * Returns     : int
 * Parameters  : struct pci_dev *pdev
 * Description : This function create a virtual mapping cookie for a PCI BAR
 */
static int oak_init_map_config(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	oak_t *adapter = netdev_priv(dev);
	u32 mem_flags;
	int retval = 0;

	/* This function returns the flags associated with the PCI resource.
	 * Resource flags are used to define some features of the individual
	 * resource. For PCI resources associated with PCI I/O regions, the
	 * information is extracted from the base address registers, but can
	 * come from elsewhere for resources not associated with PCI devices.
	 */
	mem_flags = pci_resource_flags(pdev, 2);

	if ((mem_flags & IORESOURCE_MEM) == 0)
		retval = -EINVAL;
	else
		adapter->sw_base = pci_iomap(pdev, 2, 0);

	oakdbg(debug, PROBE,
	       "Device found: dom=%d bus=%d dev=%d fun=%d reg-addr=%p",
	       pci_domain_nr(pdev->bus), pdev->bus->number,
	       PCI_SLOT(pdev->devfn), pdev->devfn, adapter->um_base);

	return retval;
}

/* Name        : oak_init_read_write_config
 * Returns     : int
 * Parameters  : struct pci_dev *pdev
 * Description : This function read and write into config space.
 */
static int oak_init_read_write_config(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	oak_t *adapter = netdev_priv(dev);
	u32 v0, v1;
	u16 devctl;
	int retval = 0;

	/* Create virtual mapping the PCI BAR configuration space before doing
	 * read or write into configuration space
	 */
	retval = oak_init_map_config(pdev);

	if (retval != 0)
		pr_err("PCI config space mapping failed %d\n", retval);

	/* After the driver has detected the device, it usually needs to read
	 * from or write to the three address spaces: memory, port, and
	 * configuration. In particular, accessing the configuration space is
	 * vital to the driver, because it is the only way it can find out
	 * where the device is mapped in memory and in the I/O space.
	 */
	pci_read_config_dword(pdev, 0x10, &v0);
	pci_read_config_dword(pdev, 0x14, &v1);
	v0 &= 0xfffffff0U;
	v0 |= 1U;
	pci_write_config_dword(pdev, 0x944, v1);
	pci_write_config_dword(pdev, 0x940, v0);

	pcie_capability_read_word(pdev, PCI_EXP_DEVCTL,	&devctl);
	/* Calculate and store TX max burst size */
	adapter->rrs =
	(u16)(1U << (((devctl & PCI_EXP_DEVCTL_READRQ) >> 12) + 6));

	if (retval == 0)
		retval = oak_get_msix_resources(pdev);

	return retval;
}

/* Name        : oak_init_pci_config
 * Returns     : int
 * Parameters  : struct pci_dev *pdev
 * Description : This function initialize oak pci config space
 */
static int oak_init_pci_config(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	oak_t *adapter = netdev_priv(dev);
	int err = 0;

	err = pci_request_regions(pdev, OAK_DRIVER_NAME);

	if (err == 0) {
		/* Enables bus-mastering for device */
		pci_set_master(pdev);
		/* Save the PCI configuration space */
		pci_save_state(pdev);

		/* create a virtual mapping cookie for a PCI BAR. Using
		 * this function you will get a __iomem address to your
		 * device BAR. You can access it using ioread*() and
		 * iowrite*(). These functions hide the details if this
		 * is a MMIO or PIO address space and will just do what
		 * you expect from them in the correct way.
		 * void __iomem * pci_iomap(struct pci_dev * dev,
		 * int bar, unsigned long, maxlen);
		 * maxlen specifies the maximum length to map. If you
		 * want to get access to the complete BAR without
		 * checking for its length first, pass 0 here.
		 */
		adapter->um_base = pci_iomap(pdev, 0, 0);

		if (!adapter->um_base)
			err = -ENOMEM;
		else
			err = oak_init_read_write_config(pdev);
	}
	return err;
}

/* Name        : oak_init_hardware
 * Returns     : int
 * Parameters  : struct pci_dev * pdev
 * Description : This function initializes oak hardware
 */
int oak_init_hardware(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	oak_t *adapter = netdev_priv(dev);
	int retval = 0;
	u32 mem_flags;

	/* Initialize device before it's used by a driver */
	retval = pci_enable_device(pdev);

	if (retval == 0) {
		/* This function returns the flags associated with this
		 * resource. Resource flags are used to define some features
		 * of the individual resource. For PCI resources associated with
		 * PCI I/O regions, the information is extracted from the base
		 * address registers, but can come from elsewhere for resources
		 * not associated with PCI devices
		 */
		mem_flags = pci_resource_flags(pdev, 0);

		if ((mem_flags & IORESOURCE_MEM) == 0) {
			retval = -EINVAL;
		} else {
			pci_read_config_dword(pdev, PCI_CLASS_REVISION,
					      &adapter->pci_class_revision);
			adapter->pci_class_revision &= 0x0000000FU;
			if (adapter->pci_class_revision > OAK_REVISION_B0) {
				retval = -EINVAL;
			} else {
				retval =
				dma_set_mask_and_coherent(&pdev->dev,
							  DMA_BIT_MASK(64));
			}
			if (retval == 0)
				retval = oak_init_pci_config(pdev);
		}
	} else {
		pr_err("PCI enable device failed %d\n", retval);
	}

	oakdbg(debug, PROBE, "pdev=%p ndev=%p err=%d", pdev,
	       pci_get_drvdata(pdev), retval);
	return retval;
}

/* Name      : oak_set_mtu_config
 * Returns   : void
 * Parameters:  struct net_device *netdev
 * Description: This function sets the min and max MTU size in the linux netdev.
 */
void oak_set_mtu_config(struct net_device *netdev)
{
	netdev->min_mtu = ETH_MIN_MTU;
	netdev->max_mtu = OAK_MAX_JUMBO_FRAME_SIZE - (ETH_HLEN + ETH_FCS_LEN);
}

/* Name        : oak_read_set_mac_address
 * Returns     : void
 * Parameters  : struct net_device *netdev
 * Description : During startup, firmware writes its own MAC address into the
 * EPU_DATA2 and EPU_DATA3 registers. When loaded, this function obtains MAC
 * address from EPU_DATA registers and increments the NIC specific part of the
 * MAC address by 1.
 * The MAC address consists of two parts:
 * Octet 1-3: Organizationally Unique Identifier (OUI)
 * Octet 4-6: Network Interface Controller (NIC) specific part.
 * The increment shall be done for the NIC specific part only.
 */
static void oak_read_set_mac_address(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	oak_t *np = netdev_priv(netdev);
	/* Octet 1-3 from EPU_DATA2 */
	u32 mac_oui;
	/* Octet 4-6 from EPU_DATA3 */
	u32 mac_nic;
	u32 mac_update;
	u32 oui_be;
	unsigned char device_mac[sizeof(uint64_t)];
	char nic_mac[ETH_ALEN];
	bool rc;

	/* Read MAC address from EPU_DATA2 and EPU_DATA3 registers */
	mac_oui = oak_unimac_io_read_32(np, OAK_EPU_DATA2);
	mac_nic = oak_unimac_io_read_32(np, OAK_EPU_DATA3);
	pr_debug("Device MAC address OUI (EPU_DATA2) : 0x%x\n",
		 cpu_to_be32(mac_oui));
	pr_debug("Device MAC address NIC (EPU_DATA3) : 0x%x\n",
		 cpu_to_be32(mac_nic));

	/* Copy Oak device MAC address and print */
	memcpy((void *)&device_mac, (void *)&mac_oui, sizeof(mac_oui));
	memcpy((void *)&device_mac + 4, (void *)&mac_nic, sizeof(mac_nic));

	rc = is_valid_ether_addr(device_mac);
	if (rc != 0) {
		/* Copy the OUI into network device structure */
		oui_be = (__force u32)cpu_to_be32(mac_oui);

		nic_mac[0] = (char)((oui_be & 0xff000000U) >> 24);
		nic_mac[1] = (char)((oui_be & 0x00ff0000U) >> 16);
		nic_mac[2] = (char)((oui_be & 0x0000ff00U) >> 8);

		/* To construct MAC address we need to takeout one bye from
		 * EPU_DATA2 and two bytes from EPU_DATA3 register
		 * As an example: MAC address is 11:22:33:44:55:66 set by
		 * firmware then Data read from register will give below
		 * little endian output
		 * EPU_DATA2 - 0x44332211
		 * EPU_DATA3 - 0x6655
		 * Hence it is required to convert to endian using cpu_to_be32
		 * EPU_DATA2 - 0x11223344
		 * EPU_DATA3 - 0x55660000
		 * To construct NIC part as 0x445566
		 */
		mac_update = (__force u32)cpu_to_be32(mac_nic) >> 16;
		mac_update = ((__force u32)cpu_to_be32(mac_oui) & 0xFFU) << 16 |
			      mac_update;
		/* Update the NIC part by one */
		mac_update = mac_update + 1;

		/* Copy updated NIC to netdev layer */
		nic_mac[3] = (char)((mac_update & 0xff0000U) >> 16);
		nic_mac[4] = (char)((mac_update & 0x00ff00U) >> 8);
		nic_mac[5] = (char)(mac_update & 0x0000ffU);

		/* Validate locally constructed NIC MAC address and
		 * Copy to mac_address member of oak_t if its valid.
		 */
		rc = is_valid_ether_addr(nic_mac);
		if (rc != 0) {
			pr_info("Device MAC address : %pM\n", device_mac);
			ether_addr_copy(np->mac_address, nic_mac);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
			eth_hw_addr_set(netdev, nic_mac);
#else
			memcpy(netdev->dev_addr, nic_mac, ETH_ALEN);
#endif
			pr_info("MAC address of NIC : %pM\n", nic_mac);
		}
	}
}

/* Name      : stop_software
 * Returns   : void
 * Parameters:  struct pci_dev * pdev
 * Description: This function unregisters the oak driver from netdev
 */
static void oak_stop_software(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);

	oak_net_del_napi(netdev);
	/* This function shuts down a device interface and removes it from
	 * the kernel tables.
	 */
	unregister_netdev(netdev);
}

/* Name        : oak_remove
 * Returns     : void
 * Parameters  : struct pci_dev * pdev
 * Description : This function remove the device from kernel
 */
static void oak_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	oak_t *adapter = NULL;

	if (netdev)
		adapter = netdev_priv(netdev);
	if (adapter) {
		if (adapter->level >= SOFTWARE_STARTED)
			oak_stop_software(pdev);
		if (adapter->level >= HARDWARE_INIT)
			oak_release_hardware(pdev);
		if (adapter->level >= SOFTWARE_INIT)
			oak_release_software(pdev);
	}
	oakdbg(debug, PROBE, "pdev=%p ndev=%p", pdev, pci_get_drvdata(pdev));

#ifndef OAK_MSIX_LEGACY
	/* Free previously allocated IRQs for a device */
	pci_free_irq_vectors(pdev);
#endif
}
