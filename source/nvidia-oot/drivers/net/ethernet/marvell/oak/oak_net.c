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

#include <nvidia/conftest.h>

#include <linux/version.h>

#include "oak_net.h"
#include "oak_ethtool.h"
#include "oak_chksum.h"

/* Try module */
#define TRY_MODULE 40
/* Allocate or free memory */
#define MEM_ALLOC_DONE 41
/* Request or release IRQ */
#define IRQ_REQUEST_DONE 42
/* Enable or disable group */
#define IRQ_GRP_ENABLE_DONE 43
/* Setup is completed */
#define SETUP_DONE 44

/* private function prototypes */
static void oak_net_esu_ena_mrvl_hdr(oak_t *np);
static struct sk_buff *oak_net_tx_prepend_mrvl_hdr(struct sk_buff *skb);
static netdev_tx_t oak_net_tx_packet(oak_t *np, struct sk_buff *skb, u16 txq);
static u32 oak_net_tx_work(ldg_t *ldg, u32 ring, int budget);
static u32 oak_net_rx_work(ldg_t *ldg, u32 ring, int budget);
static int oak_net_process_rx_pkt(oak_rx_chan_t *rxc, u32 desc_num,
				  struct sk_buff **target);
static u32 oak_net_process_channel(ldg_t *ldg, u32 ring, u32 reason,
				   int budget);
static int oak_net_poll(struct napi_struct *napi, int budget);
static int oak_net_poll_core(oak_t *np, ldg_t *ldg, int budget);
static netdev_tx_t oak_net_stop_tx_queue(oak_t *np, u32 nfrags, u16 txq);

/* Name      : esu_ena_mrvl_hdr
 * Returns   : void
 * Parameters:  oak_t * np = np
 * Description : This function enables Marvell header in the Ethernet frame
 */
static void oak_net_esu_ena_mrvl_hdr(oak_t *np)
{
	u32 offs = 0x10000U | (4U << 2) | (0xBU << 7);
	u32 data = 0x007f;

	/* Check if Marvell header is enabled */
	if (mhdr != 0)
		data |= 0x0800U;
	oakdbg(debug, PROBE, "PCI class revision: 0x%x\n",
	       np->pci_class_revision);
	/* sw32 is a macro defined in oak_unimac.h file which will be expand
	 * as writel. The writel is a linux kernel function used to write into
	 * directly mapped IO memory.
	 */
	sw32(np, offs, data);

	if (mhdr != 0 && np->pci_class_revision >= 1) {
		oakdbg(debug, PROBE, "No MRVL header generation in SW");

		mhdr = 0;
	}
}

/* Name      : esu_set_mtu
 * Returns   : int
 * Parameters: struct net_device * net_dev = net_dev,  int new_mtu = new_mtu
 * Description: This function sets the MTU size of the Ethernet interface.
 */
int oak_net_esu_set_mtu(struct net_device *net_dev, int new_mtu)
{
	oak_t *np = netdev_priv(net_dev);
	u32 offs = 0x10000U | (8U << 2) | (0xBU << 7);
	u32 fs = new_mtu + (ETH_HLEN + ETH_FCS_LEN);
	u32 data;
	int retval = 0;

	/* sr32 is a macro defined in oak_unimac.h file which will be expand
	 * as readl. The readl is a linux kernel function used to read from
	 * directly mapped IO memory.
	 */
	data = sr32(np, offs);
	data &= ~(3U << 12);

	/* fs - Frame Size is calculated and initialized new_mtu is provided as
	 * user input, the range is between 1500 to 9000
	 * ETH_HLEN is 14 Total octets in header
	 * ETH_FCS_LEN is 4 Octets in the FCS
	 * The fs value is calculated and used in condition, then the data bit
	 * is set and written into register using sw32 function.
	 */
	if (fs <= 10240) {
		if (fs > 1522) {
			if (fs <= 2048)
				data |= BIT(12);
			else
				data |= (2U << 12);

		} else {
		}
		oakdbg(debug, PROBE, "MTU %d/%d data=0x%x", new_mtu, fs, data);
		net_dev->mtu = new_mtu;
		sw32(np, offs, data);
	} else {
		retval = -EINVAL;
	}

	return retval;
}

/* Name      : esu_ena_speed
 * Returns   : void
 * Parameters:  int gbit = gbit,  oak_t * np = np
 * Description: This function sets speed in the ESU
 */
void oak_net_esu_ena_speed(u32 gbit, oak_t *np)
{
	u32 data;
	u32 offs = 0x10000U | (1U << 2) | (0xBU << 7);

	/* Capture the current PCIe speed of Oak/Spruce */
	gbit = oak_ethtool_cap_cur_speed(np, gbit);
	np->speed = gbit;
	pr_info("oak: device=0x%x speed=%dGbps\n", np->pdev->device, gbit);

	/* We capture PCIe speed in gbit, then by referring to Oak register
	 * specification Interface Configuration Matrix C_Mode, Speed, and
	 * AltSpdValue
	 * C_Mode is R/W for Ports 9 & 10 only so the interface can be
	 * configured from its default setting (via the device’s configuration
	 * pins).
	 *
	 * 0x10 - Speed [3]
	 * 0x20 - MII MAC. speed is Link Partner [6]
	 * 0x30 - Internal Speed is Internal [7]
	 *
	 * [3] - C_Modes 0x0, 0x1, 0x4 and 0x5 on Port 8 (88Q5072) default to
	 * a Speed of 100 but they can be forced to 10 Mbit operation
	 * (see Port offset 0x01). For C_Mode 0x1 and C_Mode 0x0 when in PHY
	 * mode, the Clock Mode's frequency will also change accordingly.
	 * [6] - The Link Partner’s Input clocks determine the actual speed of
	 * the MII. The Speed bits (see Port offset 0x00) will not reflect the
	 * actual speed of the port unless software updates the ForceSpd bits
	 * (Port offset 0x01).
	 * [7] - Port 0 internal port speed is fixed at 1Gbps, Port 11
	 * internal port maximum speed is 5Gbps for 88Q5072 . Minimum speed
	 * is 1Gbps.
	 *
	 * Now, by referring to Port Status Register in Oak Specification
	 *
	 * BIT(13): Alternate Speed Value
	 * This bit is valid when the Link bit, below, is set to a one.
	 * This bit along with the Speed bits below, indicates the current
	 * speed of the link as follows:
	 * 0x0 = 10 Mbps, 100 Mbps, 1000 Mbps or 10 Gbps
	 * 0x1 = 200 Mbps, 2500 Mbps or 5 Gbps
	 *
	 * BIT[9:8]: Speed mode.
	 * These bits are valid when the Link bit, above, is set to a one.
	 * These bits along with the AltSpdValue bit above, indicates the
	 * current speed of the link
	 * as follows:
	 * 0x0 = 10 Mbps
	 * 0x1 = 100 or 200 Mbps
	 * 0x2 = 1000 or 2500 Mbps
	 * 0x3 = 5 Gig
	 * The port’s Speed & AltSpdValue bits comes from the SpdValue &
	 * AltSpeed bits if the speed is being forced by ForcedSpd bit being a
	 * one (Port offset 0x01 – assuming the port can support the selected
	 * speed – else the port’s highest speed is reported). Otherwise the
	 * port’s Speed bits come from the source defined in Table 8 on page
	 * 57. The alternate speed of some of the Speed values is selected if
	 * the port’s AltSpeed is set to a one (Port offset 0x01).
	 * NOTE: These bits will reflect the actual speed of Ports only when an
	 * external PHY is attached to the port (i.e., when the SERDES
	 * interface is in SGMII or when the xMII interface is connected to an
	 * external PHY). Otherwise these bits will reflect the C_Mode speed
	 * of the port (as best it can) if the speed is not being forced.
	 */
	if (gbit == 10) {
		data = 0x201f;
	} else {
		if (gbit == 5)
			data = 0x301f;
		else
			data = 0x1013;
	}
	sw32(np, offs, data);
	msleep(20);

	if (gbit == 10) {
		data = 0x203f;
	} else {
		if (gbit == 5)
			data = 0x303f;
		else
			data = 0x1033;
	}
	sw32(np, offs, data);
	msleep(20);

	oakdbg(debug, PROBE, "Unimac %d Gbit speed enabled",
	       gbit == 1 ? 1 : 10);
}

/* Name      : tx_prepend_mrvl_hdr
 * Returns   : struct sk_buff *
 * Parameters:  struct sk_buff * skb = skb
 * Description: This function adds marvell header in the skb
 */
static struct sk_buff *oak_net_tx_prepend_mrvl_hdr(struct sk_buff *skb)
{
	struct sk_buff *nskb;
	void *hdr;

	/* unlikely is a linux kernel macro they are hint to the compiler to
	 * emit instructions that will cause branch prediction to favour
	 * the "likely" side of a jump instruction.
	 */
	if (unlikely(skb_headroom(skb) < 2)) {
		/* skb_realloc_headroom(skb, newheadroom) is required when the
		 * memory space between skb?gt;data and skb?gt;head is getting
		 * too small. This function can be used to create a new socket
		 * buffer with a headroom corresponding to the size
		 * newheadroom (and not one single byte more).
		 */
		nskb = skb_realloc_headroom(skb, 2);
		/* The macro dev_kfree_skb(), intended for use in drivers,
		 * turns into a call to consume_skb()
		 */
		dev_kfree_skb(skb);
		skb = nskb;
	} else {
	}

	if (skb) {
		/* Add data to the start of a buffer. This function extends
		 * the used data area of the buffer at the buffer start.
		 */
		hdr = skb_push(skb, 2);
		/* memset() is used to fill a block of memory with a
		 * particular value.i.e 2 bytes tobe filled with 0 starting
		 * address is hdr.
		 */
		memset(hdr, 0, 2);

	} else {
	}

	return skb;
}

/* Name        : oak_net_rbr_write_reg
 * Returns     : void
 * Parameters  : oak_rx_chan_t *rxc, oak_t *np,  u32 widx,
 * u32 ring
 * Description : This function writes into receive buffer ring (rbr)
 * hardware registers.
 */
static void oak_net_rbr_write_reg(oak_rx_chan_t *rxc, oak_t *np, u32 widx,
				  u32 ring)
{
		/* write memory barrier */
		wmb();
		rxc->rbr_widx = widx;
		oak_unimac_io_write_32(np, OAK_UNI_RX_RING_CPU_PTR(ring),
				       widx & 0x7ffU);
		oak_unimac_io_write_32(np, OAK_UNI_RX_RING_INT_CAUSE(ring),
				       OAK_MBOX_RX_RES_LOW);
}

/* Name        : oak_net_rbr_refill
 * Returns     : int
 * Parameters  : oak_t *np, u32 ring
 * Description : This function refills the receive buffer
 */
int oak_net_rbr_refill(oak_t *np, u32 ring)
{
	u32 count;
	u32 widx;
	int num;
	u32 sum = 0;
	struct page *page;
	dma_addr_t dma;
	dma_addr_t offs;
	oak_rx_chan_t *rxc = &np->rx_channel[ring];
	int rc = 0;
	u32 loop_cnt;

	num = atomic_read(&rxc->rbr_pend);
	count = rxc->rbr_size - 1;

	if (num >= count) {
		rc = -ENOMEM;
	} else {
		count = (count - num) & ~1U;
		widx = rxc->rbr_widx;
		num = 0;
		oakdbg(debug, PKTDATA,
		       "rbr_size=%d rbr_pend=%d refill cnt=%d widx=%d ridx=%d",
		       rxc->rbr_size, num, count, rxc->rbr_widx, rxc->rbr_ridx);

		/* Receive (RX) ring buffers are shared buffers between the
		 * device driver and network interface card (NIC), and store
		 * incoming packets until the device driver can process them.
		 * The below loop will refill pending buffer into receive
		 * buffer ring so that driver can process them and give it to
		 * upper layer in linux kernel.
		 */
		while ((count > 0) && (rc == 0)) {
			/* Allocate a page */
			page = oak_net_alloc_page(np, &dma, DMA_FROM_DEVICE);
			if (page) {
				offs = dma;
				loop_cnt = 0;
				while ((count > 0) &&
				       (loop_cnt < rxc->rbr_bpage)) {
					oak_rxa_t *rba = &rxc->rba[widx];
					oak_rxd_t *rbr = &rxc->rbr[widx];

					rba->page_virt = page;

					/* Keep track of last allocated descrip-
					 * tor in the  page, count == 1 is used
					 * in cases when num descriptors are
					 * less than rbr_bpage.
					 */
					if (loop_cnt == (rxc->rbr_bpage - 1) ||
					    count == 1)
						rba->page_phys = dma;
					else
						rba->page_phys = 0;

					rba->page_offs = loop_cnt *
						rxc->rbr_bsize;
					rbr->buf_ptr_lo = (offs & 0xFFFFFFFFU);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
					/* High 32 bit */
					rbr->buf_ptr_hi = ((offs >> 32)
							   & 0xFFFFFFFFU);
#else
					rbr->buf_ptr_hi = 0;
#endif
					/* move to next write position */
					widx = NEXT_IDX(widx, rxc->rbr_size);
					--count;
					++num;
					++loop_cnt;
					/* offset to 2nd buffer in page */
					offs += rxc->rbr_bsize;
				}
				++sum;
				++rxc->stat.rx_alloc_pages;
			} else {
				rc = -ENOMEM;
				++rxc->stat.rx_alloc_error;
			}
		}
		/* Add integer to atomic variable */
		atomic_add(num, &rxc->rbr_pend);
		oakdbg(debug, PKTDATA,
		       "%d pages allocated, widx=%d/%d, rc=%d",
		       sum, widx, rxc->rbr_widx, rc);

	if (rc == 0 && num > 0)
		oak_net_rbr_write_reg(rxc, np, widx, ring);
	}

	return rc;
}

/*
 * Name        : oak_net_enable_irq
 * Returns     : int
 * Parameter   : struct net_device *net_dev
 * Description : This function register interrupts.
 */
static int oak_net_enable_irq(struct net_device *net_dev)
{
	oak_t *np = netdev_priv(net_dev);
	u16 qnum;
	int retval;

	/* Request an IRQ vector */
	retval = oak_irq_request_ivec(np);

	if (retval == 0) {
		/* Set level as IRQ request successful */
		np->level = IRQ_REQUEST_DONE;
		/* Enable IRQ for logical device groups (ldg) */
		retval = oak_irq_enable_groups(np);

		if (retval == 0) {
			/* Set level as IRQ group enable successful */
			np->level = IRQ_GRP_ENABLE_DONE;
			/* Enable the Marvell header */
			oak_net_esu_ena_mrvl_hdr(np);
			/* Set MTU in interface structure */
			retval = oak_net_esu_set_mtu(np->netdev,
						     np->netdev->mtu);
			if (retval == 0)
				/* Start all the txq and rxq */
				retval = oak_net_start_all(np);
			if (retval == 0) {
				/* Set level as setup is successful */
				np->level = SETUP_DONE;
				/* Set the port speed */
				oak_net_esu_ena_speed(port_speed, np);
				/* Set carrier, Device has detected that
				 * carrier.
				 */
				netif_carrier_on(net_dev);
				for (qnum = 0; qnum < np->num_tx_chan; qnum++)
					/* Allow transmit, Allow upper layers
					 * to call the device hard_start_xmit
					 * routine.
					 */
					netif_start_subqueue(np->netdev, qnum);
			}
		}
	}
	return retval;
}

/* Name        : oak_net_open
 * Returns     : int
 * Parameters  : struct net_device *net_dev
 * Description : This function initialize the interface
 */
int oak_net_open(struct net_device *net_dev)
{
	oak_t *np = netdev_priv(net_dev);
	struct sockaddr addr;
	int err = -ENODEV;
	bool rc;

	/* Manipulate the module usage count, Before calling into module code,
	 * you should call try_module_get() on that module. if it fails, then
	 * the module is being removed and you should act as if it wasn't there
	 * Otherwise, you can safely enter the module, and call module_put()
	 * when you're finished.
	 */
	rc = try_module_get(THIS_MODULE);

	if (rc != 0) {
		/* Reset the statistics counters */
		err = oak_unimac_reset(np);
		if (err == 0 && np->level == TRY_MODULE) {
			/* Allocate memory for channels */
			err = oak_unimac_alloc_channels(np, rxs, txs,
							chan, rto);
			if (err == 0) {
				/* Set the level as memory allocation done
				 * to indicate allocation is successful
				 */
				np->level = MEM_ALLOC_DONE;
				err = oak_net_enable_irq(net_dev);
			}
		}
	}

	/* If we have valid MAC address in oak_t,
	 * which is read from EPU_DATA registers then
	 * copy to socket address structure and set it
	 * to the NIC.
	 */
	if (err == 0) {
		rc = is_valid_ether_addr(np->mac_address);
		if (rc != 0) {
			ether_addr_copy(addr.sa_data, np->mac_address);
			err = oak_net_set_mac_addr(net_dev, (void *)&addr);
			if (err != 0)
				pr_err("Fail to set MAC address\n");
		}
	} else {
		oak_net_close(net_dev);
	}

	oakdbg(debug, PROBE, "ndev=%p err=%d", net_dev, err);

	return err;
}

/* Name        : oak_net_close
 * Returns     : int
 * Parameters  : struct net_device *net_dev
 * Description : This function close the interface
 */
int oak_net_close(struct net_device *net_dev)
{
	oak_t *np = netdev_priv(net_dev);

	/* All the below function are having void as return values. But the
	 * oak_net_close function is called from kernel and expects int as
	 * return value
	 */
	netif_carrier_off(net_dev);
	netif_tx_disable(net_dev);
	/* When the interface goes down we need to remember the
	 * MAC address of an interface. Because the same MAC
	 * address will be used when we open the interface.
	 * But when we remove the module from kernel and then
	 * load the module, the MAC address in EPU_DATA will be
	 * configured.
	 */
	ether_addr_copy(np->mac_address, net_dev->dev_addr);

	if (np->level >= SETUP_DONE)
		oak_net_stop_all(np);
	if (np->level >= IRQ_GRP_ENABLE_DONE)
		oak_irq_disable_groups(np);
	if (np->level >= IRQ_REQUEST_DONE)
		oak_irq_release_ivec(np);
	if (np->level >= MEM_ALLOC_DONE) {
		oak_unimac_free_channels(np);
		np->level = TRY_MODULE;
		module_put(THIS_MODULE);
	}

	oakdbg(debug, PROBE, "ndev=%p", net_dev);
	/* returns 0 to satisfy return type */
	return 0;
}

/* Name        : oak_net_ioctl
 * Returns     : int
 * Parameters  : struct net_device *net_dev, struct ifreq *ifr,  int cmd
 * Description : This function handles IOCTL request
 */
int oak_net_ioctl(struct net_device *net_dev, struct ifreq *ifr, int cmd)
{
	oak_t *np = netdev_priv(net_dev);
	int retval = -EOPNOTSUPP;
#ifdef CMDTOOL
	if (cmd == OAK_IOCTL_REG_MAC_REQ ||
	    cmd == OAK_IOCTL_REG_ESU_REQ ||
	    cmd == OAK_IOCTL_REG_AHSI_REQ)
		retval = oak_ctl_direct_register_access(np, ifr, cmd);

	if (cmd == OAK_IOCTL_STAT)
		retval = oak_ctl_channel_status_access(np, ifr, cmd);

	if (cmd == OAK_IOCTL_SET_MAC_RATE_A)
		retval = oak_ctl_set_mac_rate(np, ifr, cmd);

	if (cmd == OAK_IOCTL_SET_MAC_RATE_B)
		retval = oak_ctl_set_mac_rate(np, ifr, cmd);

	if (cmd == OAK_IOCTL_RXFLOW)
		retval = oak_ctl_set_rx_flow(np, ifr, cmd);
#endif
	oakdbg(debug, DRV, "np=%p cmd=0x%x", np, cmd);

	return retval;
}

/* Name      : add_napi
 * Returns   : void
 * Parameters:  struct net_device * netdev
 * Description : This function registers to napi interface to receive interrupts
 */
void oak_net_add_napi(struct net_device *netdev)
{
	oak_t *np = netdev_priv(netdev);
	ldg_t *ldg = np->gicu.ldg;
	u32 num_ldg = np->gicu.num_ldg;

	while (num_ldg > 0) {
		/* Initialize a napi context */
#if defined(NV_NETIF_NAPI_ADD_WEIGHT_PRESENT) /* Linux v6.1 */
		netif_napi_add_weight(netdev, &ldg->napi, oak_net_poll, napi_wt);
#else
		netif_napi_add(netdev, &ldg->napi, oak_net_poll, napi_wt);
#endif
		/* Enable NAPI scheduling */
		napi_enable(&ldg->napi);
		++ldg;
		--num_ldg;
	}

	oakdbg(debug, PROBE, "%d napi IF added", np->gicu.num_ldg);
}

/* Name      : del_napi
 * Returns   : void
 * Parameters:  struct net_device * netdev
 * Description : This function disables the Oak driver in napi
 */
void oak_net_del_napi(struct net_device *netdev)
{
	oak_t *np = netdev_priv(netdev);
	ldg_t *ldg = np->gicu.ldg;
	u32 num_ldg = np->gicu.num_ldg;

	while (num_ldg > 0) {
		/* Stop NAPI from being scheduled on this context. Waits till
		 * any outstanding processing completes then disable napi for
		 * all the rings.
		 */
		napi_disable(&ldg->napi);
		/* Unregisters napi structure */
		netif_napi_del(&ldg->napi);
		++ldg;
		--num_ldg;
	}

	oakdbg(debug, PROBE, "%d napi IF deleted", np->gicu.num_ldg);
}

/* Name      : set_mac_addr
 * Returns   : int
 * Parameters:  struct net_device * dev = dev,  void * p_addr = addr
 * Description : This function sets the provided mac address to the AHSI RX DA
 */
int oak_net_set_mac_addr(struct net_device *dev, void *p_addr)
{
	oak_t *np = netdev_priv(dev);
	struct sockaddr *addr = p_addr;
	int rc;

	rc = is_valid_ether_addr(addr->sa_data);

	if (rc == 0) {
		rc = -EINVAL;
	} else {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
		dev_addr_mod(dev, 0, addr->sa_data, ETH_ALEN);
#else
		memcpy(dev->dev_addr, addr->sa_data, ETH_ALEN);
#endif

		/* When an interface come up we need to remember the
		 * MAC address of an interface. Because the same MAC
		 * address will be used when we open the interface.
		 * But when we remove the module from kernel and then
		 * load the module, the MAC address in EPU_DATA will be
		 * configured.
		 */
		ether_addr_copy(np->mac_address, dev->dev_addr);

		rc = netif_running(dev);

		if (rc) {
			rc = 0;
		}
	}
	oakdbg(debug, DRV, "addr=0x%02x%02x%02x%02x%02x%02x rc=%d",
	       dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
	       dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5], rc);

	return rc;
}

/* Name        : oak_net_alloc_page
 * Returns     : struct page *
 * Parameters  : oak_t *np, dma_addr_t *dma, enum dma_data_direction dir
 * Description : This function allocates page
 */
struct page *oak_net_alloc_page(oak_t *np, dma_addr_t *dma,
				enum dma_data_direction dir)
{
	struct page *page;

	/* 0: 4K */
	np->page_order = 0;
	np->page_size = (PAGE_SIZE << np->page_order);

	/* Allocate a single page and return a pointer to its page structure */
	page = alloc_page(GFP_ATOMIC /* | __GFP_COLD */ | __GFP_COMP);

	if (!page) {
		*dma = 0;
	} else {
		*dma = dma_map_page(np->device, page, 0, np->page_size,
				    dir);
		if (dma_mapping_error(np->device, *dma) != 0) {
			__free_page(page);
			*dma = 0;
			page = NULL;
		}
	}

	return page;
}

/* Name        : oak_net_select_queue
 * Returns     : u16
 * Parameters  : struct net_device *dev, struct sk_buff *skb,
 * struct net_device *sb_dev
 * Description : This function pre-seed the SKB by recording the RX queue
 */
u16 oak_net_select_queue(struct net_device *dev, struct sk_buff *skb,
			 struct net_device *sb_dev)
{
	oak_t *np = netdev_priv(dev);
	u32 txq = 0;
	bool rec;
	u16 retval = 0;

	/* The idea is that drivers which implement multiqueue RX
	 * pre-seed the SKB by recording the RX queue selected by
	 * the hardware.
	 * If such a seed is found on TX, we'll use that to select
	 * the outgoing TX queue.
	 * This helps get more consistent load balancing on router
	 * and firewall loads.
	 */
	rec = skb_rx_queue_recorded(skb);

	if (!rec)
		txq = smp_processor_id();
	else
		txq = skb_get_rx_queue(skb);

	if (txq >= np->num_tx_chan && np->num_tx_chan > 0)
		txq %= np->num_tx_chan;

	retval = (u16)txq;

	oakdbg(debug, DRV, "queue=%d of %d", txq, dev->real_num_tx_queues);

	return retval;
}

/* Name      : xmit_frame
 * Returns   : int
 * Parameters:  struct sk_buff * skb,  struct net_device * net_dev
 * Description : This function handles the transmit of skb to the queue
 */
netdev_tx_t oak_net_xmit_frame(struct sk_buff *skb, struct net_device *net_dev)
{
	oak_t *np = netdev_priv(net_dev);
	u16 txq;
	netdev_tx_t rc;
	u16 nfrags;

	txq = skb->queue_mapping;
	/* skb_shinfo(skb)->nr_frags shows the number of paged fragments */
	nfrags = skb_shinfo(skb)->nr_frags + 1;
	rc = oak_net_stop_tx_queue(np, nfrags, txq);

	if (rc == NETDEV_TX_OK)
		rc = oak_net_tx_packet(np, skb, txq);

	oakdbg(debug, TX_DONE, "nfrags=%d txq=%d rc=%d", nfrags, txq, rc);

	return rc;
}

/* Name        : oak_net_unmap_tx_pkt
 * Returns     : void
 * Parameters  : oak_txi_t *tbi, oak_tx_chan_t *txc
 * Description : This function unmaps and frees the received packet
 */
static void oak_net_unmap_tx_pkt(oak_txi_t *tbi, oak_tx_chan_t *txc)
{
	oak_t *np = txc->oak;

	if (txc->tbr_size > 0)
		txc->tbr_ridx = NEXT_IDX(txc->tbr_ridx, txc->tbr_size);

	/* With dma_unmap_single you unmap the memory mapped above.
	 * You should do this when your transfers are over.
	 * You should call dma_unmap_page() when the DMA activity is
	 * finished, e.g., from the interrupt which told you that the
	 * DMA transfer is done.
	 */
	if (tbi->mapping != 0) {
		if ((tbi->flags & TX_BUFF_INFO_ADR_MAPS)
				== TX_BUFF_INFO_ADR_MAPS)
			dma_unmap_single(np->device, tbi->mapping,
					 tbi->mapsize, DMA_TO_DEVICE);
		else
			if ((tbi->flags & TX_BUFF_INFO_ADR_MAPP)
					== TX_BUFF_INFO_ADR_MAPP)
				dma_unmap_page(np->device, tbi->mapping,
					       tbi->mapsize, DMA_TO_DEVICE);
		tbi->mapping = 0;
		tbi->mapsize = 0;
	}
}

/* Name        : oak_net_process_tx_pkt
 * Returns     : int
 * Parameters  : oak_tx_chan_t *txc = txc,  int desc_num = desc_num
 * Description : This function process the received packet to skb layer
 */
static u32 oak_net_process_tx_pkt(oak_tx_chan_t *txc, int desc_num)
{
	oak_txi_t *tbi;
	u32 work_done = 0;

	while (desc_num > 0) {
		tbi = &txc->tbi[txc->tbr_ridx];

		oak_net_unmap_tx_pkt(tbi, txc);

		/* The macro dev_kfree_skb(), intended for use in drivers,
		 * turns into a call to consume_skb(). You free pages with
		 * the free family function.
		 */
		if ((tbi->flags & TX_BUFF_INFO_EOP) == TX_BUFF_INFO_EOP) {
			if (tbi->skb)
				dev_kfree_skb(tbi->skb);
			if (tbi->page)
				__free_page(tbi->page);
			++txc->stat.tx_frame_compl;
		}

		tbi->flags = 0;
		tbi->skb = NULL;
		tbi->page = NULL;
		--desc_num;
		atomic_dec(&txc->tbr_pend);
		++work_done;
	} /* end of while */

	oakdbg(debug, TX_DONE, "work done=%d", work_done);
	return work_done;
}

/* Name      : start_all
 * Returns   : int
 * Parameters:  oak_t * np = np
 * Description : This function starts all the Tx/Rx channels.
 */
int oak_net_start_all(oak_t *np)
{
	u32 i = 0;
	int rc = 0;

	/* Call receive buffer refill for all rx channels */
	while (i < np->num_rx_chan && rc == 0) {
		rc = oak_net_rbr_refill(np, i);
		++i;
	}

	/* Start all the transmit and receive queue */
	if (rc == 0)
		rc = oak_unimac_start_all_txq(np, 1);

	if (rc == 0)
		rc = oak_unimac_start_all_rxq(np, 1);

	if (rc == 0)
		oak_irq_ena_general(np, 1);

	oakdbg(debug, IFDOWN, " ok");

	return rc;
}

/* Name      : stop_all
 * Returns   : void
 * Parameters:  oak_t * np = np
 * Description : This function frees up all the tx & rx transfer buffers
 */
void oak_net_stop_all(oak_t *np)
{
	u32 i = 0;

	oak_unimac_start_all_rxq(np, 0);
	oak_unimac_start_all_txq(np, 0);

	/* Wait for all the Rx descriptors to be free */
	usleep_range(10000, 11000);

	/* Free all the receive channel buffer */
	while (i < np->num_rx_chan) {
		oak_net_rbr_free(&np->rx_channel[i]);
		++i;
	}

	i = 0;

	/* Free all the transmit channel buffer */
	while (i < np->num_tx_chan) {
		oak_net_tbr_free(&np->tx_channel[i]);
		++i;
	}

	oak_irq_ena_general(np, 0);
	oakdbg(debug, IFDOWN, " ok");
}

/* Name      : tx_stats
 * Returns   : void
 * Parameters:  oak_tx_chan_t * txc = txc,  int len = len
 * Description : This function gets tx channel stats for various data lengths
 */
static void oak_net_tx_stats(oak_tx_chan_t *txc, u32 len)
{
	if (len <= 64)
		++txc->stat.tx_64;
	else
		if (len <= 128)
			++txc->stat.tx_128;
		else
			if (len <= 256)
				++txc->stat.tx_256;
			else
				if (len <= 512)
					++txc->stat.tx_512;
				else
					if (len <= 1024)
						++txc->stat.tx_1024;
					else
						++txc->stat.tx_2048;
}

/* Name      : rx_stats
 * Returns   : void
 * Parameters:  oak_rx_chan_t * rxc = rxc,  u32 len = len
 * Description : This function gets rx channel stats for various data lengths
 */
static void oak_net_rx_stats(oak_rx_chan_t *rxc, u32 len)
{
	if (len <= 64)
		++rxc->stat.rx_64;
	else
		if (len <= 128)
			++rxc->stat.rx_128;
		else
			if (len <= 256)
				++rxc->stat.rx_256;
			else
				if (len <= 512)
					++rxc->stat.rx_512;
				else
					if (len <= 1024)
						++rxc->stat.rx_1024;
					else
						++rxc->stat.rx_2048;
}

/* Name      : tbr_free
 * Returns   : void
 * Parameters:  oak_tx_chan_t * txp = txp
 * Description : This function frees tx channel transfer buffer
 */
void oak_net_tbr_free(oak_tx_chan_t *txp)
{
	int cnt;

	cnt = atomic_read(&txp->tbr_pend);
	oak_net_process_tx_pkt(txp, cnt);
	atomic_set(&txp->tbr_pend, 0);
	/* write buffer index */
	txp->tbr_widx = 0;
	txp->tbr_ridx = 0;
}

/* Name        : oak_net_rbr_reset
 * Returns     : void
 * Parameters  : oak_rx_chan_t *rxp = rxp
 * Description : This function reset the rxp, then move to next index
 */
static void oak_net_rbr_reset(oak_rx_chan_t *rxp)
{
	rxp->rba[rxp->rbr_ridx].page_virt = NULL;
	rxp->rbr[rxp->rbr_ridx].buf_ptr_hi = 0;
	rxp->rbr[rxp->rbr_ridx].buf_ptr_lo = 0;

	if (rxp->rbr_size > 0)
		rxp->rbr_ridx = NEXT_IDX(rxp->rbr_ridx, rxp->rbr_size);
}

/* Name        : oak_net_rbr_unmap
 * Returns     : void
 * Parameters  : oak_rx_chan_t *rxp = rxp, struct page *page, dma_addr_t dma
 * Description : This function unmap the receive buffer ring
 */
static void oak_net_rbr_unmap(oak_rx_chan_t *rxp, struct page *page,
			      dma_addr_t dma)
{
	oak_t *np = rxp->oak;

	dma_unmap_page(np->device, dma, np->page_size, DMA_FROM_DEVICE);
	++rxp->stat.rx_unmap_pages;
	/* Reset index, mapping and page_phys */
	rxp->rba[rxp->rbr_ridx].page_phys = 0;
#if defined(NV_PAGE_STRUCT_HAS___FOLIO_INDEX) /* Linux v6.16 */
	page->__folio_index = 0;
#else
	page->index = 0;
#endif
	page->mapping = NULL;
	__free_page(page);
}

/* Name        : oak_net_rbr_free
 * Returns     : void
 * Parameters  : oak_rx_chan_t *rxp = rxp
 * Description : This function free the receive buffer ring
 */
void oak_net_rbr_free(oak_rx_chan_t *rxp)
{
	u32 sum = 0;
	struct page *page;
	dma_addr_t dma;

	while (rxp->rbr_ridx != rxp->rbr_widx) {
		page = rxp->rba[rxp->rbr_ridx].page_virt;

		if (page) {
			dma = rxp->rba[rxp->rbr_ridx].page_phys;
			++sum;

			if (dma != 0)
				/* Unmap the memory */
				oak_net_rbr_unmap(rxp, page, dma);
		}
		/* Reset the buffer index */
		oak_net_rbr_reset(rxp);
	}
	oakdbg(debug, IFDOWN,
	       "totally freed ring buffer size %d kByte (ring entries: %d)",
	       sum, rxp->rbr_size);
	atomic_set(&rxp->rbr_pend, 0);
	/* write buffer index */
	rxp->rbr_widx = 0;
	/* read buffer index */
	rxp->rbr_ridx = 0;
	rxp->rbr_len = 0;
}

/* Name        : oak_net_tx_packet
 * Returns     : int
 * Parameters  : oak_t *np, struct sk_buff *skb, u16 txq
 * Description : This function transmit the packet
 */
static netdev_tx_t oak_net_tx_packet(oak_t *np, struct sk_buff *skb, u16 txq)
{
	oak_tx_chan_t *txc = &np->tx_channel[txq];
	int num = 0;
	u32 frag_idx = 0;
	u32 flags = 0;
	u32 len = 0;
	u32 cs_g3 = 0;
	u32 cs_g4 = 0;
	skb_frag_t *frag;
	u32 nfrags;
	dma_addr_t mapping = 0;
	u32 retval = 0;

	if (mhdr != 0)
		skb = oak_net_tx_prepend_mrvl_hdr(skb);
	if (skb) {
		/* OAK HW does not need padding in the data,
		 * only limitation is zero length packet.
		 * So zero byte transfer should not be programmed
		 * in the descriptor.
		 */
		if (skb->len < OAK_ONEBYTE) {
			/* pad an skbuff up to a minimal size */
			if (skb_padto(skb, OAK_ONEBYTE) == 0)
				len = OAK_ONEBYTE;
		} else {
			/* orphan a buffer
			 * If a buffer currently has an owner then we call the
			 * owner's destructor function and make the skb
			 * unowned. The buffer continues to exist but is no
			 * longer charged to its former owner.
			 */
			skb_orphan(skb);
			/* The skb_headlen() function returns the length of
			 * the data presently in  the kmalloc'd part of the
			 * buffer.
			 */
			len = skb_headlen(skb);
		}
		/* TCP segmentation allows a device to segment a single frame
		 * into multiple frames with a data payload size specified in
		 * skb_shinfo()->nr_frags
		 */
		nfrags = skb_shinfo(skb)->nr_frags;

		if (len > 0) {
			/* When you have a single buffer to transfer, map it
			 * with dma_map_single
			 */
			mapping = dma_map_single(np->device, skb->data, len,
						 DMA_TO_DEVICE);
			flags = TX_BUFF_INFO_ADR_MAPS;
			++num;
		} else {
			if (nfrags > 0) {
				frag = &skb_shinfo(skb)->frags[frag_idx];
				len = skb_frag_size(frag);
				/* Single-page streaming mappings:
				 * set up a mapping on a buffer for which you
				 * have a struct page pointer with user-space
				 * buffers mapped with get_user_pages.
				 */
				mapping = dma_map_page(np->device,
						       skb_frag_page(frag),
						       skb_frag_off(frag),
						       len, DMA_TO_DEVICE);
				flags = TX_BUFF_INFO_ADR_MAPP;
				++num;
				++frag_idx;
			}
		}
		if (num > 0) {
			retval = oak_chksum_get_tx_config(skb, &cs_g3, &cs_g4);
			/* For error case set checksum to zero */
			if (retval) {
				cs_g3 = 0;
				cs_g4 = 0;
			}
			oak_net_set_txd_first(txc, len, cs_g3, cs_g4, mapping,
					      len, flags);
			/* Continue single-page streaming mappings till the
			 * fragmentation index is less than number of fragment
			 * in a while loop.
			 */
			while (frag_idx < nfrags) {
				if (txc->tbr_size > 0)
					txc->tbr_widx = NEXT_IDX(txc->tbr_widx,
								 txc->tbr_size);
				frag = &skb_shinfo(skb)->frags[frag_idx];
				len = skb_frag_size(frag);
				mapping = dma_map_page(np->device,
						       skb_frag_page(frag),
						       skb_frag_off(frag),
						       len, DMA_TO_DEVICE);
				oak_net_set_txd_page(txc, len, mapping, len,
						     TX_BUFF_INFO_ADR_MAPP);
				++num;
				++frag_idx;
			}
			oak_net_set_txd_last(txc, skb, NULL);
			if (txc->tbr_size > 0)
				txc->tbr_widx = NEXT_IDX(txc->tbr_widx,
							 txc->tbr_size);
			atomic_add(num, &txc->tbr_pend);
			/* Write memory barrier */
			wmb();
			++txc->stat.tx_frame_count;
			txc->stat.tx_byte_count += skb->len;
			/* Static Counter: Increment tx stats counter and bytes
			 * for ifconfig
			 */
			np->netdev->stats.tx_packets++;
			np->netdev->stats.tx_bytes += skb->len;
			oak_net_tx_stats(txc, skb->len);
			oak_unimac_io_write_32(np,
					       OAK_UNI_TX_RING_CPU_PTR(txq),
					       txc->tbr_widx & 0x7ffU);
		} else {
			++txc->stat.tx_drop;
		}
	}
	return NETDEV_TX_OK;
}

/* Name        : skb_tx_protocol_type
 * Returns     : int
 * Parameters  : struct sk_buff *skb
 * Description : This function returns the transmit frames protocol type for
 * deciding the checksum offload configuration.
 */
int oak_net_skb_tx_protocol_type(struct sk_buff *skb)
{
	u8 ip_prot = 0;
	int rc = NO_CHKSUM;
	__be16 prot = skb->protocol;

	if (prot == htons(ETH_P_8021Q))
		prot = vlan_eth_hdr(skb)->h_vlan_encapsulated_proto;
	if (prot == htons(ETH_P_IP)) {
		ip_prot = ip_hdr(skb)->protocol;
		rc = L3_CHKSUM;
	} else if (prot == htons(ETH_P_IPV6)) {
		ip_prot = ipv6_hdr(skb)->nexthdr;
		rc = L3_CHKSUM;
	}
	if (ip_prot == IPPROTO_TCP || ip_prot == IPPROTO_UDP)
		rc = L3_L4_CHKSUM;
	return rc;
}

/* Name        : oak_net_tx_update_tbr_len
 * Returns     : void
 * Parameters  : ldg_t *ldg, oak_tx_chan_t *txc, u32 ring
 * Description : This function update buffer len
 */
static void oak_net_tx_update_tbr_len(ldg_t *ldg, oak_tx_chan_t *txc,
				      u32 ring)
{
	oak_t *np = ldg->device;
	u32 reason = 0;
	u32 tidx;

	if (txc->tbr_len == 0) {
		++txc->stat.tx_interrupts;
		reason = oak_unimac_disable_and_get_tx_irq_reason(np, ring,
								  &tidx);
		oakdbg(debug, TX_DONE, "MB ring=%d reason=0x%x tidx=%d", ring,
		       reason, tidx);
		if ((reason & OAK_MBOX_TX_COMP) != 0) {
			if (tidx < txc->tbr_ridx)
				txc->tbr_len = txc->tbr_size - txc->tbr_ridx
					+ tidx;
			else
				txc->tbr_len = tidx - txc->tbr_ridx;
		}
	}
}

/* Name        : oak_net_tx_min_budget
 * Returns     : int
 * Parameters  : oak_tx_chan_t *txc, u32 len,  int budget
 * Description : This function find the min of budget and process packet
 */
static u32 oak_net_tx_min_budget(oak_tx_chan_t *txc, u32 len, int budget)
{
	int todo;
	u32 work_done = 0;

	/* Calculate the min budget and process the tx packet */
	if (len > 0) {
		todo = len;
		todo = min(budget, todo);
		work_done = oak_net_process_tx_pkt(txc, todo);
		txc->tbr_len -= work_done;
	}

	return work_done;
}

/* Name        : oak_net_tx_work
 * Returns     : int
 * Parameters  : ldg_t *ldg = ldg,  u32 ring = ring,  int budget = budget
 * Description : This function process the transmit packet
 */
static u32 oak_net_tx_work(ldg_t *ldg, u32 ring, int budget)
{
	oak_t *np = ldg->device;
	oak_tx_chan_t *txc;
	u32 retval;

	txc = &np->tx_channel[ring];
	/* This function inserts a hardware memory barrier that prevents any
	 * memory access from being moved and executed on the other side of
	 * the barrier. It guarantees that any memory access initiated before
	 * the memory barrier will be complete before passing the barrier,
	 * and all subsequent memory accesses will be executed after the
	 * barrier. This function is the same as the mb() function on
	 * multi-processor systems, and it is the same as the barrier()
	 * function on uni-processor systems.
	 */
	smp_mb();

	oak_net_tx_update_tbr_len(ldg, txc, ring);

	retval = oak_net_tx_min_budget(txc, txc->tbr_len, budget);

	if (txc->tbr_len == 0)
		oak_unimac_ena_tx_ring_irq(np, ring, 1);

	return retval;
}

/* Name        : oak_net_rx_update_counters
 * Returns     : void
 * Parameters  : oak_t *np, struct sk_buff *skb, ldg_t *ldg, oak_rx_chan_t *rxc
 * Description : This function update counters, record rx queue and
 * calls napi gro receive.
 */
static void oak_net_rx_update_counters(oak_t *np, struct sk_buff *skb,
				       ldg_t *ldg,
				       oak_rx_chan_t *rxc)
{
	/* Static Counter: Increment rx stats counter and bytes for ifconfig */
	np->netdev->stats.rx_packets++;
	np->netdev->stats.rx_bytes += skb->len;
	/* Increment rx channel status byte count */
	rxc->stat.rx_byte_count += skb->len;
	/* Update skb protocol */
	skb->protocol = eth_type_trans(skb, np->netdev);
	/* Calling skb_record_rx_queue() to set the rx queue to the queue_index
	 * fixes the association between descriptor and rx queue.
	 */
	skb_record_rx_queue(skb, ldg->msi_grp);
	/* GRO (Generic receive offload) of the Linux kernel network protocol
	 * stack If the driver supported by GRO is processed in this way, read
	 * the data packet in the callback method of NAPI, and then call the
	 * GRO interface napi_gro_receive or napi_gro_frags to feed the data
	 * packet into the protocol stack. The specific GRO work is carried out
	 * in these two functions, they will eventually call __napi_gro_receive
	 * Here is napi_gro_receive, which will eventually call napi_skb_finish
	 * and __napi_gro_receive. Then when will GRO feed the data into the
	 * protocol stack, there will be two exit points, one is in
	 * napi_skb_finish, he will judge the return value of __napi_gro_receive
	 * to determine whether it is necessary to immediately feed the data
	 * packet into the protocol stack or Save.
	 */
	napi_gro_receive(&ldg->napi, skb);
}

/* Name        : oak_net_rx_work
 * Returns     : int
 * Parameters  : ldg_t *ldg, u32 ring, int budget
 * Description : This function implements the receive pkt
 */
static u32 oak_net_rx_work(ldg_t *ldg, u32 ring, int budget)
{
	oak_t *np = ldg->device;
	oak_rx_chan_t *rxc = &np->rx_channel[ring];
	u32 work_done = 0;
	struct sk_buff *skb;
	u32 reason;
	int todo;
	u32 ridx;
	u32 compl;

	if (rxc->rbr_len == 0) {
		/* This function inserts a hardware memory barrier that
		 * prevents any memory access from being moved and executed
		 * on the other side of the barrier. It guarantees that any
		 * memory access initiated before the memory barrier will be
		 * complete before passing the barrier, and all subsequent
		 * memory accesses will be executed after the barrier. This
		 * function is the same as the mb() function on multi-processor
		 * systems, and it is the same as the barrier() function on
		 * uni-processor systems.
		 */
		smp_mb();
		reason = le32_to_cpup((__le32 *)&rxc->mbox->intr_cause);
		++rxc->stat.rx_interrupts;
		oak_unimac_io_write_32(np, OAK_UNI_RX_RING_INT_CAUSE(ring),
				       OAK_MBOX_RX_COMP);
		ridx = le32_to_cpup((__le32 *)&rxc->mbox->dma_ptr_rel);
		reason &= (OAK_MBOX_RX_COMP | OAK_MBOX_RX_RES_LOW);

		if ((reason & OAK_MBOX_RX_COMP) != 0) {
			if (ridx < rxc->rbr_ridx)
				rxc->rbr_len = rxc->rbr_size - rxc->rbr_ridx
					+ ridx;
			else
				rxc->rbr_len = ridx - rxc->rbr_ridx;
		}
	} else {
		reason = 0;
	}

	todo = rxc->rbr_len;
	todo = min(budget, todo);

	while ((todo > 0) && (rxc->rbr_len > 0)) {
		skb = NULL;

		compl = oak_net_process_rx_pkt(rxc, rxc->rbr_len, &skb);

		if (skb)
			oak_net_rx_update_counters(np, skb, ldg, rxc);

		rxc->rbr_len -= compl;
		++work_done;
		--todo;
	}

	if (rxc->rbr_len == 0) {
		oakdbg(debug, RX_STATUS, "irq enabled");
		oak_unimac_io_write_32(np, OAK_UNI_RX_RING_INT_MASK(ring),
				       OAK_MBOX_RX_COMP);
	}

	return work_done;
}

/* Name        : oak_net_process_alloc_skb
 * Returns     : int
 * Parameters  : oak_rx_chan_t *rxc, int *tlen
 * Description : This function allocate skb
 */
static int oak_net_process_alloc_skb(oak_rx_chan_t *rxc, int *tlen)
{
	oak_t *np = rxc->oak;
	int good_frame;

	*tlen = 0;
	if (!rxc->skb) {
		rxc->skb = netdev_alloc_skb(np->netdev, OAK_RX_SKB_ALLOC_SIZE);
		/* Default checksum */
		rxc->skb->ip_summed = CHECKSUM_NONE;
		good_frame = 0;
	} else {
		/* continue last good frame == 1 */
		good_frame = 1;
		++rxc->stat.rx_fragments;
	}

	if (good_frame == 1)
		*tlen = rxc->skb->len;

	return good_frame;
}

/* Name        : oak_net_crc_csum_update
 * Returns     : void
 * Parameters  : oak_rx_chan_t *rxc, oak_rxs_t *rsr, int good_frame
 * Description : This function update the crc, csum counters
 */
static void oak_net_crc_csum_update(oak_rx_chan_t *rxc, oak_rxs_t *rsr,
				    int good_frame)
{
	if (good_frame == 1) {
		if (rsr->es == 0) {
			rxc->skb->ip_summed = oak_chksum_get_rx_config(rxc,
								       rsr);
		} else {
			if (rsr->ec == 0) {
				++rxc->stat.rx_badcrc;
			} else {
				if (rsr->ec == 1) {
					++rxc->stat.rx_badcsum;
				} else {
					if (rsr->ec == 3) {
						++rxc->stat.rx_nores;
					}
				}
			}
		}
	}
}

/* Name        : oak_net_unmap_and_free_page
 * Returns     : void
 * Parameters  : oak_t *np, oak_rxa_t *rba, oak_rx_chan_t *rxc,
 * struct page *page, int good_frame
 * Description : This function unmap and free pages
 */
static void oak_net_unmap_and_free_page(oak_t *np, oak_rxa_t *rba,
					oak_rx_chan_t *rxc,
					struct page *page, int good_frame)
{
	if (rba->page_phys != 0) {
		dma_unmap_page(np->device, rba->page_phys,
			       np->page_size, DMA_FROM_DEVICE);
		++rxc->stat.rx_unmap_pages;

		oakdbg(debug, RX_STATUS,
		       " free page=0x%p dma=0x%llx ",
		       rba->page_virt, rba->page_phys);
		/* Reset index, mapping and page_phys */
		rba->page_phys = 0;
#if defined(NV_PAGE_STRUCT_HAS___FOLIO_INDEX) /* Linux v6.16 */
		page->__folio_index = 0;
#else
		page->index = 0;
#endif
		page->mapping = NULL;
		if (good_frame == 0)
			__free_page(page);
	} else {
		if (good_frame == 1)
			get_page(page);
	}
	rba->page_virt = NULL;
}

/* Name        : oak_net_update_stats
 * Returns     : void
 * Parameters  : oak_rx_chan_t *rxc, oak_rxs_t *rsr, int good_frame,
 * int *good_frame, int *comp_frame
 * Description : This function update skb counter statistics
 */
static void oak_net_update_stats(oak_rx_chan_t *rxc, oak_rxs_t *rsr,
				 int *good_frame, int *comp_frame)
{
	/* From Oak AHSI data sheet,
	 * Bit 20: First.
	 * Indicates the first descriptor of a frame.
	 * The first byte of a frame resides in the buffer pointed to by rsr
	 * descriptor.
	 * Bit 21: Last.
	 * Indicates the last descriptor of a frame. The last byte of a frame
	 * resides in the buffer pointed to by rsr descriptor.
	 * The combination of <FIRST> and <LAST> indicates which part of the
	 * frame rsr descriptor describes.
	 * <FIRST><LAST>
	 * 0x00: middle descriptor of a frame
	 * 0x01: last descriptor of a frame
	 * 0x10: first descriptor of a frame
	 * 0x11: single descriptor describes a complete frame.
	 */
	if (rsr->first_last == 3) {
		if (*good_frame == 1) {
			++rxc->stat.rx_no_eof;
			*good_frame = 0;
		} else {
			*good_frame = 1;
		}
		*comp_frame = 1;
	} else {
		if (rsr->first_last == 2) {
			if (*good_frame == 1) {
				++rxc->stat.rx_no_eof;
				*good_frame = 0;
				*comp_frame = 1;
			} else {
				*good_frame = 1;
			}
		} else {
			if (rsr->first_last == 1) {
				if (*good_frame == 0)
					++rxc->stat.rx_no_sof;
				*comp_frame = 1;
			} else {
				if (*good_frame == 0) {
					++rxc->stat.rx_no_sof;
					*comp_frame = 1;
				}
			}
		}
	}
}

/* Name        : oak_net_process_rx_pkt
 * Returns     : int
 * Parameters  : oak_rx_chan_t * rxc, u32 desc_num, struct sk_buff **target
 * Description : This function process the received packet.
 */
static int oak_net_process_rx_pkt(oak_rx_chan_t *rxc, u32 desc_num,
				  struct sk_buff **target)
{
	oak_t *np = rxc->oak;
	int work_done = 0;
	int comp_frame = 0;
	int tlen = 0;
	int good_frame;
	int blen;
	struct page *page;
	u32 off = 0;
	int retval;

	good_frame = oak_net_process_alloc_skb(rxc, &tlen);

	*target = NULL;

	if (rxc->skb) {
		while ((desc_num > 0) && (comp_frame == 0)) {
			/* Rx status information
			 * Receive status register (rsr)
			 * Receive buffer address (rba)
			 */
			oak_rxs_t *rsr = &rxc->rsr[rxc->rbr_ridx];
			oak_rxa_t *rba = &rxc->rba[rxc->rbr_ridx];

			/* Receive buffer length
			 * Transmit buffer length
			 * Page address
			 */
			blen = rsr->bc;
			tlen += blen;
			page = rba->page_virt;

			/* If the page is valid then, the received frame is a
			 * good frame.  We do the following
			 * - Update the statistics counters by checking
			 *   <FIRST><LAST> combination
			 * - Take out the number of fragments of the frame
			 * - Check for Marvell header and adjust offset.
			 * - Initialise a paged fragment in an skb
			 * - Update skb length, data length and true size
			 * If frame is not good then unmap and free the page.
			 * Set good_frame as 0, To indicate page lookup failure
			 */
			if (page) {
				oak_net_update_stats(rxc, rsr, &good_frame,
						     &comp_frame);
				if (good_frame == 1) {
					int nfrags =
						skb_shinfo(rxc->skb)->nr_frags;

					if (mhdr != 0) {
						if ((rsr->first_last & 2U)
						    == 2) {
							blen -= 2;
							tlen -= 2;
							off = 2;
						} else {
							off = 0;
						}
					}
					skb_fill_page_desc(rxc->skb,
							   nfrags,
							   page,
							   rba->page_offs + off,
							   blen);
					rxc->skb->len += blen;
					rxc->skb->data_len += blen;
					rxc->skb->truesize += blen;
				}
				oak_net_unmap_and_free_page(np, rba, rxc, page,
							    good_frame);
			} else {
				/* Page lookup failure */
				good_frame = 0;
			}

			/* If we received a complete good frame, then we do the
			 * following
			 * - Update the CRC checksum of the frame
			 * - adjust headroom using skb_reserve()
			 * - Pull advance tail of skb header then free skb
			 * - Update rx channel statistics counters for various
			 *   length and set skb as NULL
			 * else its a bad frame. free the skb and set as NULL.
			 * Point to next receive buffer descriptor index
			 * Decrement the descriptor number and increment
			 * work done counter indicating work is completed.
			 *
			 */
			if (comp_frame == 1) {
				oak_net_crc_csum_update(rxc, rsr, good_frame);

				if (good_frame == 1) {
					skb_reserve(rxc->skb, NET_IP_ALIGN);
					if (!__pskb_pull_tail(rxc->skb,
							      min(tlen,
								  ETH_HLEN))) {
						dev_kfree_skb_any(rxc->skb);
					} else {
						++rxc->stat.rx_goodframe;
						*target = rxc->skb;
					}
					oak_net_rx_stats(rxc, rxc->skb->len);
					rxc->skb = NULL;
				} else {
					++rxc->stat.rx_badframe;
					dev_kfree_skb(rxc->skb);
					rxc->skb = NULL;
				}
				oakdbg(debug, RX_STATUS,
				       "page=0x%p good-frame=%d comp_frame-frame=%d ridx=%d tlen=%d",
				       page, good_frame, comp_frame,
				       rxc->rbr_ridx, tlen);
			}
			if (rxc->rbr_size > 0)
				rxc->rbr_ridx = NEXT_IDX(rxc->rbr_ridx,
							 rxc->rbr_size);
			--desc_num;
			atomic_dec(&rxc->rbr_pend);
			++work_done;
		}
	}
	retval = work_done;
	oakdbg(debug, RX_STATUS,
	       " work_done=%d %s",
	       work_done, !rxc->skb ? "" : "(continued)");

	return retval;
}

/* Name        : oak_net_check_irq_mask
 * Returns     : int
 * Parameters  : ldg_t *ldg, u32 ring,  u32 reason, int budget
 * Description : This function check irq reason
 */
static u32 oak_net_check_irq_mask(ldg_t *ldg, u32 ring,  u32 reason,
				  int budget)
{
	u32 retval = 0;

	/* Enable irq for tx/rx ring logical group devices */
	oak_unimac_ena_tx_ring_irq(ldg->device, ring, 0);
	oak_unimac_ena_rx_ring_irq(ldg->device, ring, 0);

	/* Check for IRQ reason and call the respective function
	 * i.e rx work or tx or rx error function.
	 */
	if ((reason & OAK_INTR_MASK_RX_DMA) != 0)
		retval = oak_net_rx_work(ldg, ring, budget);

	if ((reason & OAK_INTR_MASK_RX_ERR) != 0)
		oak_unimac_rx_error(ldg, ring);

	if ((reason & OAK_INTR_MASK_TX_ERR) != 0)
		oak_unimac_tx_error(ldg, ring);

	return retval;
}

/* Name        : oak_net_process_channel
 * Returns     : int
 * Parameters  : ldg_t *ldg, u32 ring, u32 reason, int budget
 * Description : This function process the interrupt for tx/rx channel.
 */
static u32 oak_net_process_channel(ldg_t *ldg, u32 ring, u32 reason,
				   int budget)
{
	u16 qidx = ring;
	u32 work_done;

	work_done = oak_net_check_irq_mask(ldg, ring, reason, budget);
	/* Update the remaining budget */
	budget = budget - work_done;

	/* If the transmit DMA interrupt from any of the ring 0-9 then
	 * - we start the transmit work
	 * - Test status of sub queue i.e. Check individual transmit queue of
	 *   a device. Then allow sending packets on subqueue.
	 */
	if ((reason & OAK_INTR_MASK_TX_DMA) != 0) {
		work_done += oak_net_tx_work(ldg, ring, budget);
		if (ldg->device->level < 45 &&
		    __netif_subqueue_stopped(ldg->device->netdev, qidx) != 0) {
			oak_tx_chan_t *txc = &ldg->device->tx_channel[ring];
			/*  Allow sending packets on subqueue */
			netif_wake_subqueue(ldg->device->netdev, qidx);
			oakdbg(debug, TX_QUEUED, "Wake Queue:%d pend=%d", ring,
			       atomic_read(&txc->tbr_pend));
		}
	}


	oakdbg(debug, PROBE, "chan=%i reason=0x%x work_done=%d", ring, reason,
	       work_done);

	oak_unimac_ena_tx_ring_irq(ldg->device, ring, 1);
	oak_unimac_ena_rx_ring_irq(ldg->device, ring, 1);

	return work_done;
}

/* Name      : poll
 * Returns   : int
 * Parameters:  struct napi_struct * napi,  int budget
 * Description : This function handles the napi bottom half after isr.
 */
static int oak_net_poll(struct napi_struct *napi, int budget)
{
	ldg_t *ldg = container_of(napi, ldg_t, napi);
	oak_t *np = ldg->device;
	int work_done;

	work_done = oak_net_poll_core(np, ldg, budget);

	/* If work_done is less than budget then remove from the poll list
	 * and enable the IRQ
	 */
	if (work_done < budget) {
		napi_complete(napi);
		oak_irq_enable_gicu_64(np, ldg->irq_mask);
		oak_unimac_io_write_32(np, OAK_GICU_INTR_GRP_CLR_MASK,
				       ldg->msi_grp |
				       OAK_GICU_INTR_GRP_MASK_ENABLE);
	}

	return work_done;
}

/* Name        : oak_net_check_irq
 * Returns     : u64
 * Parameters  : oak_t *np, ldg_t *ldg
 * Description : This function checks IRQ type
 */
static u64 oak_net_check_irq(oak_t *np, ldg_t *ldg)
{
	u64 irq_mask = 0;
	u32 mask_0;
	u32 mask_1;

	/* Read 64 bit mask register lower 32 bit stored in mask_0 and
	 * upper 32 bit stored in mask_1
	 */
	mask_0 = oak_unimac_io_read_32(np, OAK_GICU_INTR_FLAG_0);
	mask_1 = oak_unimac_io_read_32(np, OAK_GICU_INTR_FLAG_1);

	/* Concatenate mask_0 and mask_1. Store into irq_mask variable */
	irq_mask = mask_1;
	irq_mask <<= 32;
	irq_mask |= mask_0;
	irq_mask &= ldg->irq_mask;

	/* Check the reason for IRQ */
	if ((mask_1 & OAK_GICU_HOST_UNIMAC_P11_IRQ) != 0) {
		oak_unimac_process_status(ldg);
		oakdbg(debug, INTR, "UNIMAC  P11 IRQ");
	}

	if ((mask_1 & OAK_GICU_HOST_UNIMAC_P11_RESET) != 0)
		oakdbg(debug, INTR, "UNIMAC  P11 RST");
	if ((mask_1 & OAK_GICU_HOST_MASK_E) != 0)
		oakdbg(debug, INTR, "OTHER IRQ");

	return irq_mask;
}

/* Name        : oak_net_poll_core
 * Returns     : int
 * Parameters  : oak_t *np, ldg_t *ldg, int budget
 * Description : This function implement net poll functionality
 */
static int oak_net_poll_core(oak_t *np, ldg_t *ldg, int budget)
{
	u64 irq_mask = 0;
	int work_done = 0;
	int todo;
	u64 ring;
	u32 irq_count;
	u32 irq_reason;
	u64 irq_next;

	/* This function called from oak_net_poll to handle the napi bottom
	 * half after isr. We check for the IRQ mask status here and if its set
	 * then we do the following
	 * -Process channel rings till IRQ count, mask bits become less
	 *  than zero.
	 * - Maintain a work_done counter to check the status on each channel.
	 */
	irq_mask = oak_net_check_irq(np, ldg);
	if (irq_mask != 0) {
		u32 max_bits = sizeof(irq_mask) * 8;

		irq_next = (1UL << ldg->irq_first);
		irq_count = ldg->irq_count;
		todo = budget;

		while (irq_count > 0 && max_bits > 0) {
			if ((irq_mask & irq_next) != 0) {
				/* oak_ilog2_kernel_utility calls internally
				 * ilog2 kernel function.  The ilog2 function
				 * returns log of base 2 of 32-bit or a 64-bit
				 * unsigned value. This can be used to
				 * initialise global variables from constant
				 * data.
				 */
				ring = oak_ilog2_kernel_utility(irq_next);
				ring = ring / 4;
				irq_reason = (u32)(irq_next >> (ring * 4));
				work_done += oak_net_process_channel(ldg, ring,
								     irq_reason,
								     todo);
				irq_count -= 1;
			}
			irq_next <<= 1;
			max_bits -= 1;
		}
	}

	return work_done;
}

/* Name      : oak_net_stop_tx_queue
 * Returns   : int
 * Parameters:  oak_t * np,  u32 nfrags,  u16 txq
 * Desciption : This function stops sending pkt to a queue if
 * sufficient descriptors are not available.
 */
static netdev_tx_t oak_net_stop_tx_queue(oak_t *np, u32 nfrags, u16 txq)
{
	oak_tx_chan_t *txc = &np->tx_channel[txq];
	u32 free_desc;
	netdev_tx_t rc;

	free_desc = atomic_read(&txc->tbr_pend);
	free_desc = txc->tbr_size - free_desc;

	if (free_desc <= nfrags) {
		netif_stop_subqueue(np->netdev, txq);
		++txc->stat.tx_stall_count;

		oakdbg(debug, TX_QUEUED, "Stop Queue:%d pend=%d",
		       txq, atomic_read(&txc->tbr_pend));

		rc = NETDEV_TX_BUSY;
	} else {
		rc = NETDEV_TX_OK;
	}
	return rc;
}

/* Name        : oak_net_set_txd_first
 * Returns     : void
 * Parameters  : oak_tx_chan_t *txc, u16 len, u32 g3, u32 g4,
 * dma_addr_t map, u32 sz, int flags
 * Description : This function sets the first transmit descriptor
 */
void oak_net_set_txd_first(oak_tx_chan_t *txc, u16 len, u32 g3,
			   u32 g4, dma_addr_t map, u32 sz, u32 flags)
{
	oak_txd_t *txd;
	oak_txi_t *tbi;

	/* Initialize tx descriptor and tx buffer index */
	txd = &txc->tbr[txc->tbr_widx];
	tbi = &txc->tbi[txc->tbr_widx];

	/* Reset transmit descriptor structure members */
	txd->bc = len;
	txd->res1 = 0;
	txd->last = 0;
	txd->first = 1;
	txd->gl3_chksum = g3;
	txd->gl4_chksum = g4;
	txd->res2 = 0;
	txd->time_valid = 0;
	txd->res3 = 0;
	txd->buf_ptr_lo = (map & 0xFFFFFFFFU);

#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	txd->buf_ptr_hi = (map >> 32);
#else
	txd->buf_ptr_hi = 0;
#endif
	/* Reset transmit buffer index structure members */
	tbi->skb = NULL;
	tbi->page = NULL;
	tbi->mapping = map;
	tbi->mapsize = sz;
	/* First fragment my be page or single mapped */
	tbi->flags = flags;
	++txc->stat.tx_fragm_count;
}

/* Name      : set_txd_page
 * Returns   : void
 * Parameters:  oak_tx_chan_t * txc,  u16 len,  dma_addr_t map,  u32
 * sz,  int flags
 * Description : This function prepares the tx descriptor
 */
void oak_net_set_txd_page(oak_tx_chan_t *txc, u16 len, dma_addr_t map,
			  u32 sz, u32 flags)
{
	oak_txd_t *txd;
	oak_txi_t *tbi;

	/* Initialize tx descriptor and tx buffer index */
	txd = &txc->tbr[txc->tbr_widx];
	tbi = &txc->tbi[txc->tbr_widx];

	/* Reset transmit descriptor structure members */
	txd->bc = len;
	txd->res1 = 0;
	txd->last = 0;
	txd->first = 0;
	txd->gl3_chksum = 0;
	txd->gl4_chksum = 0;
	txd->res2 = 0;
	txd->time_valid = 0;
	txd->res3 = 0;
	txd->buf_ptr_lo = (map & 0xFFFFFFFFU);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	txd->buf_ptr_hi = (map >> 32);
#else
	txd->buf_ptr_hi = 0;
#endif
	/* Reset transmit buffer index structure members */
	tbi->skb = NULL;
	tbi->page = NULL;
	tbi->mapping = map;
	tbi->mapsize = sz;
	tbi->flags = flags;
	++txc->stat.tx_fragm_count;
}

/* Name      : set_txd_last
 * Returns   : void
 * Parameters:  oak_tx_chan_t * txc,  struct sk_buff * skb,  struct page * page
 * Description : This function updates last tx descriptor
 */
void oak_net_set_txd_last(oak_tx_chan_t *txc, struct sk_buff *skb,
			  struct page *page)
{
	txc->tbr[txc->tbr_widx].last = 1;
	txc->tbi[txc->tbr_widx].skb = skb;
	txc->tbi[txc->tbr_widx].page = page;
	txc->tbi[txc->tbr_widx].flags |= TX_BUFF_INFO_EOP;
	++txc->stat.tx_fragm_count;
}

/* Name      : oak_pcie_get_width_cap
 * Returns   : pcie_link_width
 * Parameters: struct pci_dev * pdev
 * Description : pcie_get_width_cap() API is not available in few platforms,
 * so added a function for the same.
 */
enum pcie_link_width oak_net_pcie_get_width_cap(struct pci_dev *pdev)
{
	u32 lnkcap;
	enum pcie_link_width wdth;

	pcie_capability_read_dword(pdev, PCI_EXP_LNKCAP, &lnkcap);
	if (lnkcap)
		wdth = (lnkcap & PCI_EXP_LNKCAP_MLW) >> 4;
	else
		wdth = PCIE_LNK_WIDTH_UNKNOWN;

	return wdth;
}
