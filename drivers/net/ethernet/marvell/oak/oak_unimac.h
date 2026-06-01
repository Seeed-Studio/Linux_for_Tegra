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
#ifndef H_OAK_UNIMAC
#define H_OAK_UNIMAC

/* Include for relation to classifier linux/etherdevice */
#include "linux/etherdevice.h"
/* Include for relation to classifier linux/pci */
#include "linux/pci.h"
/* Include for relation to classifier oak_gicu */
#include "oak_gicu.h"
/* Include for relation to classifier oak_channel_stat */
#include "oak_channel_stat.h"
/* Include for relation to classifier oak_unimac_stat */
#include "oak_unimac_stat.h"
/* Include for relation to classifier oak_unimac_desc */
#include "oak_unimac_desc.h"
/* Include for relation to classifier oak_ioc_reg */
#include "oak_ioc_reg.h"
/* Include for relation to classifier oak_ioc_lgen */
#include "oak_ioc_lgen.h"
/* Include for relation to classifier oak_ioc_stat */
#include "oak_ioc_stat.h"
/* Include for relation to classifier oak_ioc_set */
#include "oak_ioc_set.h"
/* Include for relation to classifier oak_ioc_flow */
#include "oak_ioc_flow.h"
/* Include for relation to classifier oak_irq */
#include "oak_irq.h"

#define OAK_REVISION_B0 1

#define OAK_PCIE_REGOFF_UNIMAC 0x00050000U

#define OAK_UNI_DMA_RING_BASE (OAK_PCIE_REGOFF_UNIMAC + 0x00000000)

#define OAK_UNI_DMA_TXCH_BASE (OAK_PCIE_REGOFF_UNIMAC + 0x00010000)

#define OAK_UNI_DMA_RXCH_BASE (OAK_PCIE_REGOFF_UNIMAC + 0x00011000)

#define OAK_UNI_DMA_GLOB_BASE (OAK_PCIE_REGOFF_UNIMAC + 0x00012000)

#define OAK_UNI_GLOBAL(o) (OAK_UNI_DMA_GLOB_BASE + (o))

#define OAK_UNI_DMA_TXCH_OFFS(o) (OAK_UNI_DMA_TXCH_BASE + (o))

#define OAK_UNI_DMA_RXCH_OFFS(o) (OAK_UNI_DMA_RXCH_BASE + (o))

#define OAK_UNI_DMA_RING_TX(r, o) (OAK_UNI_DMA_RING_BASE + \
				   0x0000 + 0x1000 * (r) + (o))

#define OAK_UNI_DMA_RING_RX(r, o) (OAK_UNI_DMA_RING_BASE + \
				   0x0800 + 0x1000 * (r) + (o))

#define OAK_UNI_CFG_0 OAK_UNI_GLOBAL(0x00)

#define OAK_UNI_CFG_1 OAK_UNI_GLOBAL(0x04)

#define OAK_UNI_CTRL OAK_UNI_GLOBAL(0x10)

#define OAK_UNI_STAT OAK_UNI_GLOBAL(0x14)

#define OAK_UNI_INTR OAK_UNI_GLOBAL(0x18)

#define OAK_UNI_IMSK OAK_UNI_GLOBAL(0x1C)

#define OAK_UNI_RXEN OAK_UNI_GLOBAL(0x40)

#define OAK_UNI_TXEN OAK_UNI_GLOBAL(0x44)

#define OAK_UNI_STAT_TX_WD_HISTORY BIT(7)

#define OAK_UNI_STAT_TX_WD_EVENT BIT(6)

#define OAK_UNI_STAT_RX_WD_HISTORY BIT(5)

#define OAK_UNI_STAT_RX_WD_EVENT BIT(4)

#define OAK_UNI_STAT_RX_FIFO_EMPTY BIT(3)

#define OAK_UNI_STAT_RX_FIFO_FULL BIT(2)

#define OAK_UNI_STAT_TX_FIFO_EMPTY BIT(1)

#define OAK_UNI_STAT_TX_FIFO_FULL BIT(0)

#define OAK_UNI_INTR_RX_STAT_MEM_UCE BIT(27)

#define OAK_UNI_INTR_RX_DESC_MEM_UCE BIT(26)

#define OAK_UNI_INTR_TX_DESC_MEM_UCE BIT(25)

#define OAK_UNI_INTR_AXI_WR_MEM_UCE BIT(24)

#define OAK_UNI_INTR_TX_DATA_MEM_UCE BIT(23)

#define OAK_UNI_INTR_TX_FIFO_MEM_UCS BIT(22)

#define OAK_UNI_INTR_RX_FIFO_MEM_UCS BIT(21)

#define OAK_UNI_INTR_HCS_MEM_UCE BIT(20)

#define OAK_UNI_INTR_RX_STAT_MEM_CE BIT(19)

#define OAK_UNI_INTR_RX_DESC_MEM_CE BIT(18)

#define OAK_UNI_INTR_TX_DESC_MEM_CE BIT(17)

#define OAK_UNI_INTR_AXI_WR_MEM_CE BIT(16)

#define OAK_UNI_INTR_TX_DATA_MEM_CE BIT(15)

#define OAK_UNI_INTR_TX_FIFO_MEM_CE BIT(14)

#define OAK_UNI_INTR_RX_FIFO_MEM_CE BIT(13)

#define OAK_UNI_INTR_HCS_MEM_CHECK BIT(12)

#define OAK_UNI_INTR_TX_MTU_ERR BIT(11)

#define OAK_UNI_INTR_RX_BERR_WR BIT(10)

#define OAK_UNI_INTR_RX_BERR_RD BIT(9)

#define OAK_UNI_INTR_TX_BERR_WR BIT(8)

#define OAK_UNI_INTR_TX_BERR_RD BIT(7)

#define OAK_UNI_INTR_BERR_HIC_A BIT(6)

#define OAK_UNI_INTR_BERR_HIC_B BIT(5)

#define OAK_UNI_INTR_COUNT_WRAP BIT(4)

#define OAK_UNI_INTR_RX_WATCHDOG BIT(3)

#define OAK_UNI_INTR_TX_WATCHDOG BIT(2)

#define OAK_UNI_INTR_RX_STALL_FIFO BIT(1)

#define OAK_UNI_INTR_TX_STALL_FIFO BIT(0)

#define OAK_UNI_INTR_SEVERE_ERRORS (\
OAK_UNI_INTR_RX_STAT_MEM_UCE | \
OAK_UNI_INTR_RX_DESC_MEM_UCE | \
OAK_UNI_INTR_TX_DESC_MEM_UCE | \
OAK_UNI_INTR_AXI_WR_MEM_UCE | \
OAK_UNI_INTR_TX_DATA_MEM_UCE | \
OAK_UNI_INTR_TX_FIFO_MEM_UCS | \
OAK_UNI_INTR_RX_FIFO_MEM_UCS | \
OAK_UNI_INTR_HCS_MEM_UCE | \
OAK_UNI_INTR_RX_STAT_MEM_CE | \
OAK_UNI_INTR_RX_DESC_MEM_CE | \
OAK_UNI_INTR_TX_DESC_MEM_CE | \
OAK_UNI_INTR_AXI_WR_MEM_CE | \
OAK_UNI_INTR_TX_DATA_MEM_CE | \
OAK_UNI_INTR_TX_FIFO_MEM_CE | \
OAK_UNI_INTR_RX_FIFO_MEM_CE | \
OAK_UNI_INTR_HCS_MEM_CHECK | \
OAK_UNI_INTR_TX_MTU_ERR | \
OAK_UNI_INTR_RX_BERR_WR | \
OAK_UNI_INTR_RX_BERR_RD | \
OAK_UNI_INTR_TX_BERR_WR | \
OAK_UNI_INTR_TX_BERR_RD \
)

#define OAK_UNI_INTR_NORMAL_ERRORS (OAK_UNI_INTR_BERR_HIC_A | \
				    OAK_UNI_INTR_BERR_HIC_B | \
				    OAK_UNI_INTR_COUNT_WRAP | \
				    OAK_UNI_INTR_RX_WATCHDOG | \
				    OAK_UNI_INTR_TX_WATCHDOG | \
				    OAK_UNI_INTR_RX_STALL_FIFO | \
				    OAK_UNI_INTR_TX_STALL_FIFO)

#define OAK_UNI_TXRATE_B OAK_UNI_GLOBAL(0x80)

#define OAK_UNI_TXRATE_A OAK_UNI_GLOBAL(0x84)

#define OAK_UNI_STAT_RX_GOOD_FRAMES OAK_UNI_GLOBAL(0x100)

#define OAK_UNI_STAT_RX_BAD_FRAMES OAK_UNI_GLOBAL(0x104)

#define OAK_UNI_STAT_TX_PAUSE OAK_UNI_GLOBAL(0x108)

#define OAK_UNI_STAT_TX_STALL_FIFO OAK_UNI_GLOBAL(0x10C)

#define OAK_UNI_STAT_RX_STALL_DESC OAK_UNI_GLOBAL(0x110)

#define OAK_UNI_STAT_RX_STALL_FIFO OAK_UNI_GLOBAL(0x114)

#define OAK_UNI_STAT_RX_DISC_DESC OAK_UNI_GLOBAL(0x118)

#define OAK_UNI_PTP_HW_TIME OAK_UNI_GLOBAL(0x17C)

#define OAK_UNI_ECC_ERR_CFG OAK_UNI_GLOBAL(0x1E4)

#define OAK_UNI_ECC_ERR_STAT_0 OAK_UNI_GLOBAL(0x1E8)

#define OAK_UNI_ECC_ERR_STAT_1 OAK_UNI_GLOBAL(0x1EC)

#define OAK_UNI_ECC_ERR_CNT_0 OAK_UNI_GLOBAL(0x1F0)

#define OAK_UNI_ECC_ERR_CNT_1 OAK_UNI_GLOBAL(0x1F4)

#define OAK_UNI_DMA_TX_CH_CFG OAK_UNI_DMA_TXCH_OFFS(0x00)

#define OAK_UNI_DMA_TX_CH_ARBIT_B0_LO OAK_UNI_DMA_TXCH_OFFS(0x04)

#define OAK_UNI_DMA_TX_CH_ARBIT_B0_HI OAK_UNI_DMA_TXCH_OFFS(0x08)

#define OAK_DMA_TX_CH_SCHED_B0_LO OAK_UNI_DMA_TXCH_OFFS(0x0C)

#define OAK_UNI_DMA_TX_CH_SCHED_B0_HI OAK_UNI_DMA_TXCH_OFFS(0x10)

#define OAK_DMA_TX_CH_SCHED_LO OAK_UNI_DMA_TXCH_OFFS(0x04)

#define OAK_UNI_DMA_TX_CH_SCHED_HI OAK_UNI_DMA_TXCH_OFFS(0x08)

#define OAK_UNI_DMA_RX_CH_CFG OAK_UNI_DMA_RXCH_OFFS(0x00)

#define OAK_UNI_DMA_RX_CH_ARBIT_B0_LO OAK_UNI_DMA_RXCH_OFFS(0x04)

#define OAK_UNI_DMA_RX_CH_ARBIT_B0_HI OAK_UNI_DMA_RXCH_OFFS(0x08)

#define OAK_DMA_RX_CH_SCHED_LO OAK_UNI_DMA_RXCH_OFFS(0x04)

#define OAK_UNI_DMA_RX_CH_SCHED_HI OAK_UNI_DMA_RXCH_OFFS(0x08)

#define OAK_UNI_TX_RING_CFG(r) OAK_UNI_DMA_RING_TX(r, 0x00)

#define OAK_UNI_TX_RING_PREF_THR(r) OAK_UNI_DMA_RING_TX(r, 0x04)

#define OAK_UNI_TX_RING_MBOX_THR(r) OAK_UNI_DMA_RING_TX(r, 0x08)

#define OAK_UNI_TX_RING_DMA_PTR(r) OAK_UNI_DMA_RING_TX(r, 0x0C)

#define OAK_UNI_TX_RING_CPU_PTR(r) OAK_UNI_DMA_RING_TX(r, 0x10)

#define OAK_UNI_TX_RING_EN(r) OAK_UNI_DMA_RING_TX(r, 0x14)

#define OAK_UNI_TX_RING_INT_CAUSE(r) OAK_UNI_DMA_RING_TX(r, 0x18)

#define OAK_UNI_TX_RING_INT_MASK(r) OAK_UNI_DMA_RING_TX(r, 0x1C)

#define OAK_UNI_TX_RING_DBASE_LO(r) OAK_UNI_DMA_RING_TX(r, 0x20)

#define OAK_UNI_TX_RING_DBASE_HI(r) OAK_UNI_DMA_RING_TX(r, 0x24)

#define OAK_UNI_TX_RING_MBASE_LO(r) OAK_UNI_DMA_RING_TX(r, 0x28)

#define OAK_UNI_TX_RING_MBASE_HI(r) OAK_UNI_DMA_RING_TX(r, 0x2C)

#define OAK_UNI_TX_RING_TIMEOUT(r) OAK_UNI_DMA_RING_TX(r, 0x30)

#define OAK_UNI_TX_RING_RATECTRL(r) OAK_UNI_DMA_RING_TX(r, 0x34)

#define OAK_UNI_TX_RING_MAXDTIME(r) OAK_UNI_DMA_RING_TX(r, 0x38)

#define OAK_UNI_RING_ENABLE_REQ BIT(0)

#define OAK_UNI_RING_ENABLE_DONE BIT(1)

#define OAK_UNI_RX_RING_CFG(r) OAK_UNI_DMA_RING_RX(r, 0x00)

#define OAK_UNI_RX_RING_PREF_THR(r) OAK_UNI_DMA_RING_RX(r, 0x04)

#define OAK_UNI_RX_RING_MBOX_THR(r) OAK_UNI_DMA_RING_RX(r, 0x08)

#define OAK_UNI_RX_RING_DMA_PTR(r) OAK_UNI_DMA_RING_RX(r, 0x0C)

#define OAK_UNI_RX_RING_CPU_PTR(r) OAK_UNI_DMA_RING_RX(r, 0x10)

#define OAK_UNI_RX_RING_WATERMARK(r) OAK_UNI_DMA_RING_RX(r, 0x14)

#define OAK_UNI_RX_RING_EN(r) OAK_UNI_DMA_RING_RX(r, 0x18)

#define OAK_UNI_RX_RING_INT_CAUSE(r) OAK_UNI_DMA_RING_RX(r, 0x1C)

#define OAK_UNI_RX_RING_INT_MASK(r) OAK_UNI_DMA_RING_RX(r, 0x20)

#define OAK_UNI_RX_RING_DBASE_LO(r) OAK_UNI_DMA_RING_RX(r, 0x24)

#define OAK_UNI_RX_RING_DBASE_HI(r) OAK_UNI_DMA_RING_RX(r, 0x28)

#define OAK_RING_TOUT_USEC(us) (OAK_CLOCK_FREQ_MHZ * 1 * (us))

#define OAK_UNI_RX_RING_SBASE_LO(r) OAK_UNI_DMA_RING_RX(r, 0x2C)

#define OAK_UNI_RX_RING_SBASE_HI(r) OAK_UNI_DMA_RING_RX(r, 0x30)

#define OAK_UNI_RX_RING_MBASE_LO(r) OAK_UNI_DMA_RING_RX(r, 0x34)

#define OAK_UNI_RX_RING_MBASE_HI(r) OAK_UNI_DMA_RING_RX(r, 0x38)

#define OAK_UNI_RX_RING_TIMEOUT(r) OAK_UNI_DMA_RING_RX(r, 0x3C)

#define OAK_UNI_RX_RING_DADDR_HI(r) OAK_UNI_DMA_RING_RX(r, 0x40)

#define OAK_UNI_RX_RING_DADDR_LO(r) OAK_UNI_DMA_RING_RX(r, 0x44)

#define OAK_UNI_RX_RING_ETYPE(r) OAK_UNI_DMA_RING_RX(r, 0x48)

#define OAK_UNI_RX_RING_MAP(r) OAK_UNI_DMA_RING_RX(r, 0x4C)

#define OAK_CLOCK_FREQ_MHZ (250U)

#define OAK_RING_TOUT_MSEC(ms) (OAK_CLOCK_FREQ_MHZ * 1000 * (ms))

#define OAK_MIN_TX_RATE_CLASS_A 0

#define OAK_MIN_TX_RATE_CLASS_B 1

#define OAK_MIN_TX_RATE_IN_KBPS 64

#define OAK_MAX_TX_RATE_IN_KBPS 4194240U

#define OAK_DEF_TX_HI_CREDIT_BYTES 1536U

#define OAK_MAX_TX_HI_CREDIT_BYTES 0x3fffU

#define OAK_UNI_RX_RING_DADDR_MASK_HI(r) OAK_UNI_DMA_RING_RX(r, 0x50)

#define OAK_UNI_RX_RING_DADDR_MASK_LO(r) OAK_UNI_DMA_RING_RX(r, 0x54)

/* Define DMA check numbers */
#define OAK_DMA_15 15U
#define OAK_DMA_7 7U
#define OAK_BUFFER_SIZE_16 16
#define OAK_BUFFER_SIZE_2048 2048

typedef struct oak_mbox_tstruct {
	u32 dma_ptr_rel;
	u32 intr_cause;
} oak_mbox_t;

typedef struct oak_rxa_tstruct {
	struct page *page_virt;
	dma_addr_t page_phys;
	u32 page_offs;
} oak_rxa_t;

typedef struct oak_rx_chan_tstruct {
#define OAK_RX_BUFFER_SIZE 2048
#define OAK_RX_BUFFER_PER_PAGE (PAGE_SIZE / OAK_RX_BUFFER_SIZE)
#define OAK_RX_SKB_ALLOC_SIZE (128 + NET_IP_ALIGN)
#define OAK_RX_LGEN_RX_MODE BIT(0)
#define OAK_RX_REFILL_REQ BIT(1)
	struct oak_tstruct *oak;
	u32 enabled;
	u32 flags;
	u32 rbr_count;
	u32 rbr_size;
	u32 rsr_size;
	u32 mbox_size;
	atomic_t rbr_pend;
	u32 rbr_widx;
	u32 rbr_ridx;
	u32 rbr_len;
	u32 rbr_bsize;		/* Rx buffer size */
	u32 rbr_bpage;		/* Number of descriptors per page */
	dma_addr_t rbr_dma;
	dma_addr_t rsr_dma;
	dma_addr_t mbox_dma;
	oak_rxa_t *rba;
	oak_rxd_t *rbr;
	oak_rxs_t *rsr;
	oak_mbox_t *mbox;
	oak_driver_rx_stat stat;
	struct sk_buff *skb;
} oak_rx_chan_t;

typedef struct oak_txi_tstruct {
#define TX_BUFF_INFO_NONE 0x00000000U
#define TX_BUFF_INFO_ADR_MAPS 0x00000001U
#define TX_BUFF_INFO_ADR_MAPP 0x00000002U
#define TX_BUFF_INFO_EOP 0x00000004U
	struct sk_buff *skb;
	struct page *page;
	dma_addr_t mapping;
	u32 mapsize;
	u32 flags;
} oak_txi_t;

typedef struct oak_tx_chan_tstruct {
#define OAK_RX_LGEN_TX_MODE BIT(0)
	struct oak_tstruct *oak;
	u32 enabled;
	u32 flags;
	u32 tbr_count;
	u32 tbr_compl;
	u32 tbr_size;
	atomic_t tbr_pend;
	u32 tbr_ridx;
	u32 tbr_widx;
	u32 tbr_len;
	dma_addr_t tbr_dma;
	oak_txd_t *tbr;
	oak_txi_t *tbi;
	u32 mbox_size;
	dma_addr_t mbox_dma;
	oak_mbox_t *mbox;
	oak_driver_tx_stat stat;
	/* lock */
	spinlock_t lock;
} oak_tx_chan_t;

typedef struct oak_tstruct {
#define MAX_RBR_RING_ENTRIES 3U
#define MAX_TBR_RING_ENTRIES 3U
#define XBR_RING_SIZE(s) (1U << ((s) + 4U))
#define MAX_RBR_RING_SIZE XBR_RING_SIZE(MAX_RBR_RING_ENTRIES)
#define MAX_TBR_RING_SIZE XBR_RING_SIZE(MAX_TBR_RING_ENTRIES)
#define RX_DESC_PREFETCH_TH 5
#define RX_MBOX_WRITE_TH 5
#define TX_DESC_PREFETCH_TH 5
#define TX_MBOX_WRITE_TH 5
#define OAK_MBOX_RX_RES_LOW BIT(2)
#define OAK_MBOX_RX_COMP BIT(0)
#define OAK_MBOX_TX_ERR_ABORT BIT(4)
#define OAK_MBOX_TX_ERR_HCRED BIT(3)
#define OAK_MBOX_TX_LATE_TS BIT(2)
#define OAK_MBOX_TX_COMP BIT(0)
#define OAK_IVEC_UVEC1 BIT(8)
#define NEXT_IDX(i, sz) (((i) + 1) % (sz))
#define MAX_NUM_OF_CHANNELS 10
#define nw32(np, reg, val) writel((val), (np)->um_base + (reg))
#define MIN_NUM_OF_CHANNELS 1
#define nr32(np, reg) readl((np)->um_base + (reg))
#define sr32(np, reg) readl((np)->um_base + (reg))
#define sw32(np, reg, val) writel((val), (np)->um_base + (reg))
#define oakdbg(debug_var, TYPE, f, a...)     \
	do {				     \
		if (((debug_var) & NETIF_MSG_##TYPE) != 0) {	\
			pr_info("%s:" f, __func__, ##a);	\
		}						\
	} while (0)

	int level;
	u32 pci_class_revision;
	/* lock */
	spinlock_t lock;
	struct net_device *netdev;
	struct device *device;
	struct pci_dev *pdev;
	void __iomem *um_base;
	void __iomem *sw_base;
	u32 page_order;
	u32 page_size;
	oak_gicu gicu;
	u32 num_rx_chan;
	oak_rx_chan_t rx_channel[MAX_NUM_OF_CHANNELS];
	u32 num_tx_chan;
	oak_tx_chan_t tx_channel[MAX_NUM_OF_CHANNELS];
	oak_unimac_stat unimac_stat;
	u16 rrs;
	u32 speed;
	char mac_address[ETH_ALEN];
} oak_t;

int oak_net_rbr_refill(oak_t *np, u32 ring);

/* Name        : oak_unimac_disable_and_get_tx_irq_reason
 * Returns     : u32
 * Parameters  : oak_t *np, u32 ring, u32 *dma_ptr
 * Description : This function check the reason for Tx IRQ
 */
u32 oak_unimac_disable_and_get_tx_irq_reason(oak_t *np, u32 ring,
					     u32 *dma_ptr);

/* Name        : oak_unimac_alloc_channels
 * Returns     : int
 * Parameters  : oak_t *np, int rxs, int txs, int chan, int rto
 * Description : This function allocate tx and rx channels
 */
int oak_unimac_alloc_channels(oak_t *np, u32 rxs, u32 txs, int chan, int rto);

/* Name        : oak_unimac_free_channels
 * Returns     : void
 * Parameters  : oak_t *np
 * Description : This function free the tx and rx channels
 */
void oak_unimac_free_channels(oak_t *np);

/* Name        : oak_unimac_reset
 * Returns     : int
 * Parameters  : oak_t *np
 * Description : This function reset unimac statistics
 */
int oak_unimac_reset(oak_t *np);

/* Name        : oak_unimac_reset_statistics
 * Returns     : void
 * Parameters  : oak_t *np
 * Description : This function reset the statistics counter
 */
void oak_unimac_reset_statistics(oak_t *np);

/* Name        : oak_unimac_crt_bit_mask
 * Returns     : u32
 * Parameters  : u32 off, u32 len, u32 val, u32 bit_mask
 * Description : This function calculate the crt bit mask
 */
u32 oak_unimac_crt_bit_mask(u32 off, u32 len, u32 val,
			    u32 bit_mask);

/* Name        : oak_unimac_io_read_32
 * Returns     : u32
 * Parameters  : oak_t * np, u32 addr
 * Description : This function read from register.
 */
u32 oak_unimac_io_read_32(oak_t *np, u32 addr);

/* Name        : oak_unimac_io_write_32
 * Returns     : void
 * Parameters  : oak_t *np, u32 addr, u32 val
 * Description : This function write value to register.
 */
void oak_unimac_io_write_32(oak_t *np, u32 addr, u32 val);

/* Name        : oak_unimac_set_bit_num
 * Returns     : void
 * Parameters  : oak_t *np, u32 reg, u32 bit_num, int enable
 * Description : This function set a bit number
 */
void oak_unimac_set_bit_num(oak_t *np, u32 reg, u32 bit_num,
			    u32 enable);

/* Name        : oak_unimac_set_rx_none
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring
 * Description : This function clear the rx ring
 */
void oak_unimac_set_rx_none(oak_t *np, u32 ring);

/* Name        : oak_unimac_set_rx_8021Q_et
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring, u16 etype, u16 pcp_vid,
 *               int enable
 * Description : This function write ethtype and calls set bit number for
 *               Rx ring
 */
void oak_unimac_set_rx_8021Q_et(oak_t *np, u32 ring, u16 etype,
				u16 pcp_vid, u32 enable);

/* Name        : oak_unimac_set_rx_8021Q_fid
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring, u32 fid, int enable
 * Description : This function set Rx ring Filtering database identifier (FID)
 */
void oak_unimac_set_rx_8021Q_fid(oak_t *np, u32 ring, u32 fid,
				 u32 enable);

/* Name        : oak_unimac_set_rx_8021Q_flow
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring, u32 flow_id, int enable
 * Description : This function set Rx ring flow identifier
 */
void oak_unimac_set_rx_8021Q_flow(oak_t *np, u32 ring, u32 flow_id,
				  u32 enable);

/* Name        : oak_unimac_set_rx_8021Q_qpri
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring, u32 qpri, int enable
 * Description : This function set Rx ring queue priority.
 */
void oak_unimac_set_rx_8021Q_qpri(oak_t *np, u32 ring, u32 qpri,
				  u32 enable);

/* Name        : oak_unimac_set_rx_8021Q_spid
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring, u32 spid, int enable
 * Description : This function set Rx ring speed identifier (SPID).
 */
void oak_unimac_set_rx_8021Q_spid(oak_t *np, u32 ring, u32 spid,
				  u32 enable);

/* Name        : oak_unimac_set_rx_da
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring, unsigned char *addr, int enable
 * Description : This function set rx ring MAC address
 */
void oak_unimac_set_rx_da(oak_t *np, u32 ring, unsigned char *addr,
			  u32 enable);

/* Name        : oak_unimac_set_rx_da_mask
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring, unsigned char *addr, int enable
 * Description : This function set rx ring MAC address
 */
void oak_unimac_set_rx_da_mask(oak_t *np, u32 ring, unsigned char *addr,
			       int enable);

/* Name        : oak_unimac_set_rx_mgmt
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring, u32 val, int enable
 * Description : This function call set bit number function with
 * value and enable options
 */
void oak_unimac_set_rx_mgmt(oak_t *np, u32 ring, u32 val, u32 enable);

/* Name        : oak_unimac_process_status
 * Returns     : void
 * Parameters  : ldg_t *ldg
 * Description : This function get the process status
 */
void oak_unimac_process_status(ldg_t *ldg);

/* Name        : oak_unimac_rx_error
 * Returns     : void
 * Parameters  : ldg_t *ldg, u32 ring
 * Description : This function check interrupt cause reason and then if the
 * reason is valid refill the receive ring else increment the rx errors
 * counters.
 */
void oak_unimac_rx_error(ldg_t *ldg, u32 ring);

/* Name        : oak_unimac_tx_error
 * Returns     : void
 * Parameters  : ldg_t *ldg, u32 ring
 * Description : This function Tx error reason and then count errors
 */
void oak_unimac_tx_error(ldg_t *ldg, u32 ring);

/* Name        : oak_unimac_ena_rx_ring_irq
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring, u32 enable
 * Description : This function enables the Rx ring irq.
 */
void oak_unimac_ena_rx_ring_irq(oak_t *np, u32 ring, u32 enable);

/* Name        : oak_unimac_ena_tx_ring_irq
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring, u32 enable
 * Description : This function enables the Tx ring irq.
 */
void oak_unimac_ena_tx_ring_irq(oak_t *np, u32 ring, u32 enable);

/* Name        : oak_unimac_set_tx_ring_rate
 * Returns     : int
 * Parameters  : oak_t *np, u32 ring, u32 sr_class,
 *               u32 hi_credit, u32 r_kbps
 * Description : This function set tx ring rate limit.
 */
int oak_unimac_set_tx_ring_rate(oak_t *np, u32 ring, u32 sr_class,
				u32 hi_credit, u32 r_kbps);

/* Name        : oak_unimac_clr_tx_ring_rate
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring
 * Description : This function clear the tx ring rate limit
 */
void oak_unimac_clr_tx_ring_rate(oak_t *np, u32 ring);

/* Name        : oak_unimac_set_tx_mac_rate
 * Returns     : int
 * Parameters  : oak_t *np, u32 sr_class, u32 hi_credit,
 *               u32 r_kbps
 * Description : This function set transmision mac rate limit.
 */
int oak_unimac_set_tx_mac_rate(oak_t *np, u32 sr_class, u32 hi_credit,
			       u32 r_kbps);

/* Name        : oak_unimac_start_all_txq
 * Returns     : int
 * Parameters  : oak_t *np, u32 enable
 * Description : This function start all transmit queues
 */
int oak_unimac_start_all_txq(oak_t *np, u32 enable);

/* Name        : oak_unimac_start_all_rxq
 * Returns     : int
 * Parameters  : oak_t *np, u32 enable
 * Description : This function start all receive queues
 */
int oak_unimac_start_all_rxq(oak_t *np, u32 enable);

/* Name        : oak_unimac_start_tx_ring
 * Returns     : u32
 * Parameters  : oak_t *np, int32_t ring, u32 enable
 * Description : This function start rx ring
 */
u32 oak_unimac_start_tx_ring(oak_t *np, int32_t ring, u32 enable);

/* Name        : oak_unimac_start_rx_ring
 * Returns     : u32
 * Parameters  : oak_t *np, u32 ring, u32 enable
 * Description : This function start Rx ring
 */
u32 oak_unimac_start_rx_ring(oak_t *np, u32 ring, u32 enable);

/* Name        : oak_ilog2_kernel_utility
 * Returns     : u32
 * Parameters  : u64 val
 * Description : This function calls ilog2 kernel macro, ilog2 has complexity
 * 67, the function ilog2 is called by net and unimac component.
 */
static inline u64 oak_ilog2_kernel_utility(u64 val)
{
	return ilog2(val);
}
#endif /* #ifndef H_OAK_UNIMAC */
