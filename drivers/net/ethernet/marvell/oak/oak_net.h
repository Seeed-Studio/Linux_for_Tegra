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

#ifndef H_OAK_NET
#define H_OAK_NET

/* Include for relation to classifier oak_unimac */
#include "oak_unimac.h"
#ifdef CMDTOOL
/* Include for relation to classifier oak_ctl */
#include "oak_ctl.h"
#endif
/* Include for relation to classifier linux/ip.h */
#include "linux/ip.h"
/* Include for relation to classifier linux/ipv6.h */
#include "linux/ipv6.h"
/* Include for relation to classifier linux/if_vlan.h */
#include "linux/if_vlan.h"

#include <linux/version.h>

#define OAK_ONEBYTE 1

extern u32 rxs;
extern u32 txs;
extern int chan;
extern int rto;
extern int mhdr;
extern u32 port_speed;
extern int napi_wt;

/* Name      : esu_set_mtu
 * Returns   : int
 * Parameters:  struct net_device * net_dev = net_dev,  int new_mtu = new_mtu
 * Description: This function set the MTU size of the Ethernet interface.
 */
int oak_net_esu_set_mtu(struct net_device *net_dev, int new_mtu);

/* Name      : oak_set_mtu_config
 * Returns   : void
 * Parameters:  struct net_device *netdev
 * Description: This function sets the min and max MTU size in the linux netdev.
 */
void oak_set_mtu_config(struct net_device *netdev);

/* Name      : esu_ena_speed
 * Returns   : void
 * Parameters:  int gbit = gbit,  oak_t * np = np
 */
void oak_net_esu_ena_speed(u32 gbit, oak_t *np);

/* Name        : oak_net_open
 * Returns     : int
 * Parameters  : struct net_device * net_dev
 * Description : This function initialize the interface
 */
int oak_net_open(struct net_device *net_dev);

/* Name        : oak_net_close
 * Returns     : int
 * Parameters  : struct net_device *net_dev
 * Description : This function close the interface
 */
int oak_net_close(struct net_device *net_dev);

/* Name        : oak_net_ioctl
 * Returns     : int
 * Parameters  : struct net_device *net_dev, struct ifreq *ifr,  int cmd
 * Description : This function handles IOCTL request
 */
int oak_net_ioctl(struct net_device *net_dev, struct ifreq *ifr, int cmd);

/* Name      : add_napi
 * Returns   : void
 * Parameters:  struct net_device * netdev
 */
void oak_net_add_napi(struct net_device *netdev);

/* Name      : del_napi
 * Returns   : void
 * Parameters:  struct net_device * netdev
 */
void oak_net_del_napi(struct net_device *netdev);

/* Name      : set_mac_addr
 * Returns   : int
 * Parameters:  struct net_device * dev = dev,  void * p_addr = addr
 */
int oak_net_set_mac_addr(struct net_device *dev, void *p_addr);

/* Name        : alloc_page
 * Returns     : struct page *
 * Parameters  : oak_t *np, dma_addr_t * dma, nt direction
 * Description : This function allocate page
 */
struct page *oak_net_alloc_page(oak_t *np, dma_addr_t *dma,
				enum dma_data_direction dir);

/* Name        : oak_net_select_queue
 * Returns     : u16
 * Parameters  : struct net_device *dev, struct sk_buff *skb,
 * struct net_device *sb_dev
 * Description : This function pre-seed the SKB by recording the RX queue
 */
u16 oak_net_select_queue(struct net_device *dev, struct sk_buff *skb,
			 struct net_device *sb_dev);

/* Name      : xmit_frame
 * Returns   : int
 * Parameters:  struct sk_buff * skb,  struct net_device * net_dev
 */
netdev_tx_t oak_net_xmit_frame(struct sk_buff *skb, struct net_device *net_dev);

/* Name      : start_all
 * Returns   : int
 * Parameters:  oak_t * np = np
 */
int oak_net_start_all(oak_t *np);

/* Name      : stop_all
 * Returns   : void
 * Parameters:  oak_t * np = np
 */
void oak_net_stop_all(oak_t *np);

/* Name      : tbr_free
 * Returns   : void
 * Parameters:  oak_tx_chan_t * txp = txp
 */
void oak_net_tbr_free(oak_tx_chan_t *txp);

/* Name        : oak_net_rbr_free
 * Returns     : void
 * Parameters  : oak_rx_chan_t *rxp = rxp
 * Description : This function free the receive buffer ring
 */
void oak_net_rbr_free(oak_rx_chan_t *rxp);

/* Name      : add_txd_length
 * Returns   : void
 * Parameters:  oak_tx_chan_t * txc,  u16 len
 */
void oak_net_add_txd_length(oak_tx_chan_t *txc, u16 len);

/* Name        : oak_net_set_txd_first
 * Returns     : void
 * Parameters  : oak_tx_chan_t *txc, u16 len, u32 g3, u32 g4,
 * dma_addr_t map, u32 sz, int flags
 * Description : This function set the transmit descriptor
 */
void oak_net_set_txd_first(oak_tx_chan_t *txc, u16 len, u32 g3,
			   u32 g4, dma_addr_t map, u32 sz, u32 flags);

/* Name      : set_txd_page
 * Returns   : void
 * Parameters:  oak_tx_chan_t * txc,  u16 len,  dma_addr_t map,  u32
 * sz,  int flags
 */
void oak_net_set_txd_page(oak_tx_chan_t *txc, u16 len, dma_addr_t map,
			  u32 sz, u32 flags);

/* Name      : set_txd_last
 * Returns   : void
 * Parameters:  oak_tx_chan_t * txc,  struct sk_buff * skb,  struct page * page
 */
void oak_net_set_txd_last(oak_tx_chan_t *txc, struct sk_buff *skb,
			  struct page *page);

/* Name      : oak_net_pcie_get_width_cap
 * Returns   : enum pcie_link_width
 * Parameters: struct pci_dev *
 */
enum pcie_link_width oak_net_pcie_get_width_cap(struct pci_dev *dev);

/* Name        : oak_net_skb_tx_protocol_type
 * Returns     : int
 * Parameters  : struct sk_buff *skb
 * Description : This function returns the transmit frames protocol type for
 * deciding the checksum offload configuration.
 */
int oak_net_skb_tx_protocol_type(struct sk_buff *skb);

#endif /* #ifndef H_OAK_NET */

