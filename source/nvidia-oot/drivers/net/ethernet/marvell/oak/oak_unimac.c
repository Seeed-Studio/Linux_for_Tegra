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
#include "oak_unimac.h"

/* private function prototypes */
static void oak_unimac_set_channel_dma(oak_t *np, int rto, u32 rxs, u32 txs,
				       int chan);
static u32 oak_unimac_ena_ring(oak_t *np, u32 addr, u32 enable);
static void oak_unimac_set_arbit(oak_t *np, u32 ring,
				 u32 weight, u32 reg);
static void oak_unimac_set_dma_addr(oak_t *np, dma_addr_t phys, u32 reg_lo,
				    u32 reg_hi);

/* Name        : set_arbit_priority_based
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring, u32 prio, u32 reg
 * Description : This function set Tx/Rx ring priority
 */
static void oak_unimac_set_arbit_priority(oak_t *np, u32 ring,
					  u32 prio, u32 reg)
{
	u32 val;

	if (ring <= 9) {
		/* For the provided ring number do the following
		 * -Read the register content into a variable
		 * -Set the 11th bit OR with the read value from register
		 * -Write the value into register.
		 */
		val = oak_unimac_io_read_32(np, reg);
		val |= BIT(11);

		oak_unimac_io_write_32(np, reg, val);

		/* If the reg is DMA RX Channel Configuration register then
		 * -Set DMA RX Channel Arbiter Low register
		 * -Else set DMA TX Channel Arbiter Low register
		 *  of the B0 version board.
		 */
		if (reg == OAK_UNI_DMA_RX_CH_CFG)
			oak_unimac_set_arbit(np, ring, prio,
					     OAK_UNI_DMA_RX_CH_ARBIT_B0_LO);
		else
			oak_unimac_set_arbit(np, ring, prio,
					     OAK_UNI_DMA_TX_CH_ARBIT_B0_LO);
	}
}

/* Name        : oak_unimac_disable_and_get_tx_irq_reason
 * Returns     : u32
 * Parameters  : oak_t *np, u32 ring, u32 *dma_ptr
 * Description : This function check the reason for Tx IRQ
 */
u32 oak_unimac_disable_and_get_tx_irq_reason(oak_t *np, u32 ring,
					     u32 *dma_ptr)
{
	oak_tx_chan_t *txc = &np->tx_channel[ring];
	u32 rc = 0;

	oak_unimac_ena_rx_ring_irq(np, ring, 0);
	rc = le32_to_cpup((__le32 *)&txc->mbox->intr_cause);

	/* The possible reasons for the tx IRQ are
	 *
	 * OAK_MBOX_TX_COMP - Ring Process Completion. This interrupt is set
	 * when:
	 * -The number of completed TX descriptors is equal to the
	 *  TX_RING_MAILBOX_WR_THR, or
	 * -The number of completed descriptors is less than
	 *  TX_RING_MAILBOX_WR_THR after a certain time as specified in
	 *  the TX_RING_TIMEOUT.
	 *
	 * OAK_MBOX_TX_LATE_TS - TX Late Time Stamp.
	 * This bit gets set when: At the time the start of frame is picked
	 * from the TX Memory for transmission, the hardware timer has already
	 * surpassed the "TimeStamp" value in the descriptor, and the delta is
	 * larger than the register value defined in TX_RING_MAX_LATE_TS.
	 * Only valid for Ring 2 to 9.
	 *
	 * OAK_MBOX_TX_ERR_HCRED - High Credit Limit Exceeded. Only valid for
	 * ring 2 to 9.
	 */
	oak_unimac_io_write_32(np, OAK_UNI_TX_RING_INT_CAUSE(ring),
			       (OAK_MBOX_TX_COMP | OAK_MBOX_TX_LATE_TS |
			       OAK_MBOX_TX_ERR_HCRED));
	*dma_ptr = le32_to_cpup((__le32 *)&txc->mbox->dma_ptr_rel);

	return rc;
}

/* Name        : oak_unimac_alloc_memory_tx
 * Returns     : int
 * Parameters  : oak_t *np,  oak_tx_chan_t *txc, max_tx_size
 * Description : This function allocate memory for tx channels
 */
static int oak_unimac_alloc_memory_tx(oak_t *np,  oak_tx_chan_t *txc,
				      int max_tx_size)
{
	int retval = 0;

	if (!txc->tbr) {
		txc->tbr_size = max_tx_size;
		/* dma_alloc_coherent routine allocates a region of <size>
		 * bytes of consistent memory.
		 * Consistent memory is memory for which a write by either the
		 * device or the processor can immediately be read by the
		 * processor or device without having to worry about caching
		 * effects.  (You may however need to make sure to flush the
		 * processor's write buffers before telling devices to read
		 * that memory.)
		 */
		txc->tbr = dma_alloc_coherent(np->device, txc->tbr_size *
					      sizeof(oak_txd_t), &txc->tbr_dma,
					      GFP_KERNEL);

		if ((txc->tbr_dma & OAK_DMA_15) != 0)
			retval = -EFAULT;
	}

	if (retval == 0 && !txc->mbox) {
		txc->mbox_size = 1;
		/* dma_alloc_coherent() is used for data transfer between IO
		 * device and system memory. You allocate a kernel buffer for
		 * DMA transaction (in this case dma from device to system
		 * memory) and setup your device for dma transfer.
		 * An IO device usually generates an interrupt when DMA
		 * transfer completes.
		 */
		txc->mbox = dma_alloc_coherent(np->device, sizeof(oak_mbox_t),
					       &txc->mbox_dma, GFP_KERNEL);

		if ((txc->mbox_dma & OAK_DMA_7) != 0)
			retval = -EFAULT;
	}

	if (retval == 0 && !txc->tbi)
		/* Get Free Pages from linux kernel and allocate memory.
		 * The memory is set to zero.
		 */
		txc->tbi = kzalloc(txc->tbr_size * sizeof(oak_txi_t),
				   GFP_KERNEL);

	if (retval == 0 && (!txc->tbr || !txc->mbox || !txc->tbi))
		retval = -ENOMEM;

	return retval;
}

/* Name        : oak_unimac_alloc_memory_rx
 * Returns     : int
 * Parameters  : oak_t *np,  oak_rx_chan_t *rxc, max_rx_size
 * Description : This function allocate memory for rx channels
 */
static int oak_unimac_alloc_memory_rx(oak_t *np,  oak_rx_chan_t *rxc,
				      u32 max_rx_size)
{
	int retval = 0;

	if (rxc->rbr_bpage < 1)
		retval = -EFAULT;

	if (retval == 0 && !rxc->rbr) {
		/* Allocates a region of rbr_size bytes of consistent memory
		 * for rx channel rbr
		 */
		rxc->rbr_size = max_rx_size;
		rxc->rbr = dma_alloc_coherent(np->device, rxc->rbr_size *
					      sizeof(oak_rxd_t), &rxc->rbr_dma,
					      GFP_KERNEL);

		if ((rxc->rbr_dma & OAK_DMA_7) != 0)
			retval = -EFAULT;
	}

	if (retval == 0 && !rxc->rsr) {
		/* Allocates a region of rsr_size bytes of consistent memory
		 * for rx channel rsr
		 */
		rxc->rsr_size = max_rx_size;
		rxc->rsr = dma_alloc_coherent(np->device, rxc->rsr_size *
					      sizeof(oak_rxs_t), &rxc->rsr_dma,
					      GFP_KERNEL);
		if ((rxc->rsr_dma & OAK_DMA_15) != 0)
			retval = -EFAULT;
	}

	if (retval == 0 && !rxc->mbox) {
		/* Allocates a region of mbox_size bytes of consistent memory
		 * for rx channel mbox
		 */
		rxc->mbox_size = 1;
		rxc->mbox = dma_alloc_coherent(np->device, sizeof(oak_mbox_t),
					       &rxc->mbox_dma, GFP_KERNEL);

		if ((rxc->mbox_dma & OAK_DMA_7) != 0)
			retval = -EFAULT;
	}

	if (retval == 0 && !rxc->rba)
		/* Get Free Pages from linux kernel and allocate memory.
		 * The memory is set to zero.
		 */
		rxc->rba = kzalloc(rxc->rbr_size * sizeof(oak_rxa_t),
				   GFP_KERNEL);
	if (retval == 0 && (!rxc->rbr || !rxc->rsr || !rxc->mbox || !rxc->rba))
		retval = -ENOMEM;

	return retval;
}

/* Name        : oak_unimac_alloc_channels
 * Returns     : int
 * Parameters  : oak_t *np, int rxs, int txs, int chan, int rto
 * Description : This function allocate tx and rx channels
 */
int oak_unimac_alloc_channels(oak_t *np, u32 rxs, u32 txs, int chan, int rto)
{
	oak_rx_chan_t *rxc;
	oak_tx_chan_t *txc;
	int i;
	u32 max_rx_size;
	u32 max_tx_size;
	int max_channel;
	int retval = 0;

	if (rxs < OAK_BUFFER_SIZE_16 || rxs > OAK_BUFFER_SIZE_2048)
		rxs = 0;
	else
		rxs = oak_ilog2_kernel_utility(rxs) - 4;

	if (txs < OAK_BUFFER_SIZE_16 || txs > OAK_BUFFER_SIZE_2048)
		txs = 0;
	else
		txs = oak_ilog2_kernel_utility(txs) - 4;

	max_rx_size = XBR_RING_SIZE(rxs);
	max_tx_size = XBR_RING_SIZE(txs);

	if (chan >= MIN_NUM_OF_CHANNELS && chan <= MAX_NUM_OF_CHANNELS) {
		max_channel = chan;
	} else {
		retval = -EFAULT;
		max_channel = 0;
	}

	np->num_rx_chan = 0;
	np->num_tx_chan = 0;
	i = 0;

	while (retval == 0 && i < max_channel) {
		rxc = &np->rx_channel[i];
		/* set backpointer */
		rxc->oak = np;
		/* reset flags */
		rxc->flags = 0;
		atomic_set(&rxc->rbr_pend, 0);
		/* software write buffer index */
		rxc->rbr_widx = 0;
		/* software read buffer index */
		rxc->rbr_ridx = 0;
		rxc->skb = NULL;
		rxc->rbr_bsize = OAK_RX_BUFFER_SIZE;
		if (rxc->rbr_bsize > 0)
			rxc->rbr_bpage = (PAGE_SIZE / rxc->rbr_bsize);

		retval = oak_unimac_alloc_memory_rx(np, rxc, max_rx_size);

		if (retval == 0) {
			++np->num_rx_chan;
			++i;
		}
	}

	i = 0;
	while (retval == 0 && i < max_channel) {
		txc = &np->tx_channel[i];
		/* set backpointer */
		txc->oak = np;
		/* reset flags */
		txc->flags = 0;
		/* number of pending buffers ready 2b processed by hardware */
		txc->tbr_count = 0;
		atomic_set(&txc->tbr_pend, 0);
		/* software write buffer index */
		txc->tbr_widx = 0;
		/* software read buffer index */
		txc->tbr_ridx = 0;

		retval = oak_unimac_alloc_memory_tx(np, txc, max_tx_size);

		if (retval == 0) {
			++np->num_tx_chan;
			++i;
		}
	}

	if (retval == 0)
		oak_unimac_set_channel_dma(np, rto, rxs, txs, chan);
	else
		oak_unimac_free_channels(np);

	return retval;
}

/* Name        : oak_unimac_free_tx_channels
 * Returns     : void
 * Parameters  : oak_t *np
 * Description : This function free unimac tx channels
 */
static void oak_unimac_free_tx_channels(oak_t *np)
{
	u32 num_tx_chan = np->num_tx_chan;

	/* Free all the allocated tx channels */
	while (num_tx_chan > 0) {
		oak_tx_chan_t *chan = &np->tx_channel[num_tx_chan - 1];

		if (chan->tbr) {
			/* Free a region of tbr consistent memory */
			dma_free_coherent(np->device, chan->tbr_size *
					  sizeof(oak_txd_t), chan->tbr,
					  chan->tbr_dma);
			chan->tbr = NULL;
		}
		if (chan->mbox) {
			/* Free a region of mbox consistent memory */
			dma_free_coherent(np->device, sizeof(oak_mbox_t),
					  chan->mbox, chan->mbox_dma);
			chan->mbox = NULL;
		}

		/* Free previously allocated memory for tbi */
		kfree(chan->tbi);
		chan->tbi = NULL;

		--num_tx_chan;
	}
}

/* Name        : oak_unimac_free_rx_channels
 * Returns     : void
 * Parameters  : oak_t *np
 * Description : This function free unimac rx channels
 */
static void oak_unimac_free_rx_channels(oak_t *np)
{
	u32 num_rx_chan = np->num_rx_chan;

	/* Free all the allocated rx channels */
	while (num_rx_chan > 0) {
		oak_rx_chan_t *chan = &np->rx_channel[num_rx_chan - 1];

		if (chan->rbr) {
			/* Free a region of rbr consistent memory */
			dma_free_coherent(np->device, chan->rbr_size *
					  sizeof(oak_rxd_t), chan->rbr,
					  chan->rbr_dma);
			chan->rbr = NULL;
		}
		if (chan->rsr) {
			/* Free a region of rsr consistent memory */
			dma_free_coherent(np->device, chan->rsr_size *
					  sizeof(oak_rxs_t), chan->rsr,
					  chan->rsr_dma);
			chan->rsr = NULL;
		}
		if (chan->mbox) {
			/* Free a region of mbox consistent memory */
			dma_free_coherent(np->device, sizeof(oak_mbox_t),
					  chan->mbox, chan->mbox_dma);
			chan->mbox = NULL;
		}

		/* Free previously allocated memory for rba */
		kfree(chan->rba);
		chan->rba = NULL;

		--num_rx_chan;
	}
}

/* Name        : oak_unimac_free_channels
 * Returns     : void
 * Parameters  : oak_t *np
 * Description : This function free unimac channels
 */
void oak_unimac_free_channels(oak_t *np)
{
	oakdbg(debug, IFDOWN,
	       "np=%p num_rx_chan=%d num_tx_chan=%d", np, np->num_rx_chan,
	       np->num_tx_chan);
	oak_unimac_free_rx_channels(np);
	oak_unimac_free_tx_channels(np);
}

/* Name        : oak_unimac_reset
 * Returns     : int
 * Parameters  : oak_t *np
 * Description : This function reset unimac statistics
 */
int oak_unimac_reset(oak_t *np)
{
	u32 val = BIT(31);
	u32 cnt = 1000;
	int rc = 0;

	/* Set the 31st bit value into AHSI Control register */
	oak_unimac_io_write_32(np, OAK_UNI_CTRL, val);
	while ((cnt > 0) && (val & BIT(31))) {
		val = oak_unimac_io_read_32(np, OAK_UNI_CTRL);
		--cnt;
	}

	if (cnt > 0)
		/* Reset all statistics counters */
		oak_unimac_reset_statistics(np);
	else
		rc = -EFAULT;

	return rc;
}

/* Name        : oak_unimac_reset_misc_counters
 * Returns     : void
 * Parameters  : oak_t *np
 * Description : This function reset good, bad, disc and tx pause counters
 */
static void oak_unimac_reset_misc_counters(oak_t *np)
{
	oak_unimac_io_write_32(np, OAK_UNI_STAT_RX_GOOD_FRAMES, 0);
	oak_unimac_io_write_32(np, OAK_UNI_STAT_RX_BAD_FRAMES, 0);
	oak_unimac_io_write_32(np, OAK_UNI_STAT_RX_DISC_DESC, 0);
	oak_unimac_io_write_32(np, OAK_UNI_STAT_TX_PAUSE, 0);
}

/* Name        : oak_unimac_reset_stall_counters
 * Returns     : void
 * Parameters  : oak_t *np
 * Description : This function reset the stall desc, fifo statistics counters
 */
static void oak_unimac_reset_stall_counters(oak_t *np)
{
	oak_unimac_io_write_32(np, OAK_UNI_STAT_RX_STALL_DESC, 0);
	oak_unimac_io_write_32(np, OAK_UNI_STAT_RX_STALL_FIFO, 0);
	oak_unimac_io_write_32(np, OAK_UNI_STAT_TX_STALL_FIFO, 0);
}

/* Name        : oak_unimac_reset_statistics
 * Returns     : void
 * Parameters  : oak_t *np
 * Description : This function reset the statistics counter
 */
void oak_unimac_reset_statistics(oak_t *np)
{
	u32 i = 0;

	/* Copy tx/rx misc and stall statistics counters */
	oak_unimac_reset_misc_counters(np);
	oak_unimac_reset_stall_counters(np);

	while (i < np->num_rx_chan) {
		memset(&np->rx_channel[i], 0, sizeof(oak_rx_chan_t));
		++i;
	}

	i = 0;

	while (i < np->num_tx_chan) {
		memset(&np->tx_channel[i], 0, sizeof(oak_tx_chan_t));
		++i;
	}
}

/* Name        : oak_unimac_crt_bit_mask
 * Returns     : u32
 * Parameters  : u32 off, u32 len, u32 val, u32 bit_mask
 * Description : This function calculate the crt bit mask
 */
u32 oak_unimac_crt_bit_mask(u32 off, u32 len, u32 val,
			    u32 bit_mask)
{
	u32 ret_7 = 0;
	u32 mask = 0;
	u32 sz = sizeof(val) * 8;

	if ((len + off) >= sz)
		len = sz - off;
	mask = ((1U << len) - 1) << off;
	val = val & ~mask;
	val = val | ((bit_mask << off) & mask);
	ret_7 = val;

	return ret_7;
}

/* Name        : oak_unimac_io_read_32
 * Returns     : u32
 * Parameters  : oak_t * np, u32 addr
 * Description : This function read from register.
 */
u32 oak_unimac_io_read_32(oak_t *np, u32 addr)
{
	u32 val = readl(np->um_base + addr);
	return val;
}

/* Name        : oak_unimac_io_write_32
 * Returns     : void
 * Parameters  : oak_t *np, u32 addr, u32 val
 * Description : This function write value to register.
 */
void oak_unimac_io_write_32(oak_t *np, u32 addr, u32 val)
{
	writel((val), np->um_base + (addr));
}

/* Name        : oak_unimac_set_bit_num
 * Returns     : void
 * Parameters  : oak_t *np, u32 reg, u32 bit_num, int enable
 * Description : This function set a bit number
 */
void oak_unimac_set_bit_num(oak_t *np, u32 reg, u32 bit_num,
			    u32 enable)
{
	u32 val = oak_unimac_io_read_32(np, reg);

	if (enable != 0)
		val |= (1U << bit_num);
	else
		val &= ~(1U << bit_num);
	oak_unimac_io_write_32(np, reg, val);
}

/* Name        : oak_unimac_set_rx_none
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring
 * Description : This function clear the rx ring
 */
void oak_unimac_set_rx_none(oak_t *np, u32 ring)
{
	oak_unimac_io_write_32(np, OAK_UNI_RX_RING_MAP(ring), 0U);
	oakdbg(debug, DRV, "clear np=%p chan=%d", np, ring);
}

/* Name        : oak_unimac_set_rx_8021Q_et
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring, u16 etype, u16 pcp_vid,
 *               int enable
 * Description : This function write ethtype and calls set bit number for
 *               Rx ring
 */
void oak_unimac_set_rx_8021Q_et(oak_t *np, u32 ring, u16 etype,
				u16 pcp_vid, u32 enable)
{
	u32 val_1;

	if (enable != 0) {
		val_1 = (u32)(etype) << 16 | pcp_vid;
		oak_unimac_io_write_32(np, OAK_UNI_RX_RING_ETYPE(ring), val_1);
	}
	oak_unimac_set_bit_num(np, OAK_UNI_RX_RING_MAP(ring), 19U, enable);
	oakdbg(debug, DRV, "np=%p chan=%d etype=0x%x vid=0x%x enable=%d",
	       np, ring, etype, pcp_vid, enable);
}

/* Name        : oak_unimac_set_rx_8021Q_fid
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring, u32 fid, int enable
 * Description : This function set Rx ring Filtering database identifier (FID)
 */
void oak_unimac_set_rx_8021Q_fid(oak_t *np, u32 ring, u32 fid,
				 u32 enable)
{
	u32 val = oak_unimac_io_read_32(np, OAK_UNI_RX_RING_MAP(ring));

	val = oak_unimac_crt_bit_mask(21, 3, val, fid);

	if (enable != 0)
		val |= BIT(20);
	else
		val &= ~(BIT(20));

	oak_unimac_io_write_32(np, OAK_UNI_RX_RING_MAP(ring), val);
	oakdbg(debug, DRV, "np=%p chan=%d fid=0x%x enable=%d",
	       np, ring, fid, enable);
}

/* Name        : oak_unimac_set_rx_8021Q_flow
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring, u32 flow_id, int enable
 * Description : This function set Rx ring flow identifier
 */
void oak_unimac_set_rx_8021Q_flow(oak_t *np, u32 ring, u32 flow_id,
				  u32 enable)
{
	u32 val = oak_unimac_io_read_32(np, OAK_UNI_RX_RING_MAP(ring));

	val = oak_unimac_crt_bit_mask(14, 4, val, flow_id);

	if (enable != 0)
		val |= BIT(12);
	else
		val &= ~(BIT(12));
	oak_unimac_io_write_32(np, OAK_UNI_RX_RING_MAP(ring), val);
	oakdbg(debug, DRV, "np=%p chan=%d flow_id=%d enable=%d",
	       np, ring, flow_id, enable);
}

/* Name        : oak_unimac_set_rx_8021Q_qpri
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring, u32 qpri, int enable
 * Description : This function set Rx ring queue priority.
 */
void oak_unimac_set_rx_8021Q_qpri(oak_t *np, u32 ring, u32 qpri,
				  u32 enable)
{
	u32 val = oak_unimac_io_read_32(np, OAK_UNI_RX_RING_MAP(ring));

	val = oak_unimac_crt_bit_mask(4, 3, val, qpri);

	if (enable != 0)
		val |= BIT(3);
	else
		val &= ~(BIT(3));

	oak_unimac_io_write_32(np, OAK_UNI_RX_RING_MAP(ring), val);
	oakdbg(debug, DRV, "np=%p chan=%d qpri=%d enable=%d",
	       np, ring, qpri, enable);
}

/* Name        : oak_unimac_set_rx_8021Q_spid
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring, u32 spid, int enable
 * Description : This function set Rx ring speed identifier (SPID).
 */
void oak_unimac_set_rx_8021Q_spid(oak_t *np, u32 ring, u32 spid,
				  u32 enable)
{
	u32 val = oak_unimac_io_read_32(np, OAK_UNI_RX_RING_MAP(ring));

	val = oak_unimac_crt_bit_mask(8, 4, val, spid);

	if (enable != 0)
		val |= BIT(7);
	else
		val &= ~(BIT(7));

	oak_unimac_io_write_32(np, OAK_UNI_RX_RING_MAP(ring), val);
	oakdbg(debug, DRV, "np=%p chan=%d spid=0x%x enable=%d",
	       np, ring, spid, enable);
}

/* Name        : oak_unimac_set_rx_da
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring, unsigned char *addr, int enable
 * Description : This function set MAC address to rx ring device address
 * register, then set the rx ring map bit.
 */
void oak_unimac_set_rx_da(oak_t *np, u32 ring, unsigned char *addr,
			  u32 enable)
{
	u32 val_1;
	u32 val_4;

	if (enable != 0) {
		val_4 = (addr[2] << 0) | ((u32)addr[3] << 8) |
			((u32)addr[4] << 16) | ((u32)addr[5] << 24);
		oak_unimac_io_write_32(np, OAK_UNI_RX_RING_DADDR_HI(ring),
				       val_4);
		val_1 = (addr[0] << 0) | ((u32)addr[1] << 8);
		oak_unimac_io_write_32(np, OAK_UNI_RX_RING_DADDR_LO(ring),
				       val_1);
	}

	oak_unimac_set_bit_num(np, OAK_UNI_RX_RING_MAP(ring), 18, enable);

	oakdbg(debug, DRV,
	       "np=%p chan=%d addr=0x%02x%02x%02x%02x%02x%02x enable=%d",
	       np, ring, addr[0] & 0xFFU, addr[1] & 0xFFU, addr[2] & 0xFFU,
	       addr[3] & 0xFFU, addr[4] & 0xFFU, addr[5] & 0xFFU, enable);
}

/* Name        : oak_unimac_set_rx_da_mask
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring, unsigned char *addr, int enable
 * Description : This function set mask to rx ring device address register.
 */
void oak_unimac_set_rx_da_mask(oak_t *np, u32 ring, unsigned char *addr,
			       int enable)
{
	u32 val_1;
	u32 val_4;

	if (enable != 0) {
		val_1 = (addr[2] << 0) | ((u32)addr[3] << 8) |
			((u32)addr[4] << 16) | ((u32)addr[5] << 24);
		oak_unimac_io_write_32(np, OAK_UNI_RX_RING_DADDR_MASK_HI(ring),
				       val_1);
		val_4 = (addr[0] << 0) | ((u32)addr[1] << 8);
		oak_unimac_io_write_32(np, OAK_UNI_RX_RING_DADDR_MASK_LO(ring),
				       val_4);
	} else {
		oak_unimac_io_write_32(np, OAK_UNI_RX_RING_DADDR_MASK_HI(ring),
				       0);
		oak_unimac_io_write_32(np, OAK_UNI_RX_RING_DADDR_MASK_LO(ring),
				       0);
	}
	oakdbg(debug, DRV,
	       "np=%p chan=%d addr=0x%02x%02x%02x%02x%02x%02x enable=%d",
	       np, ring, addr[0] & 0xFFU, addr[1] & 0xFFU, addr[2] & 0xFFU,
	       addr[3] & 0xFFU, addr[4] & 0xFFU, addr[5] & 0xFFU, enable);
}

/* Name        : oak_unimac_set_rx_mgmt
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring, u32 val, int enable
 * Description : This function call set bit number function with
 * value and enable options
 */
void oak_unimac_set_rx_mgmt(oak_t *np, u32 ring, u32 val, u32 enable)
{
	oak_unimac_set_bit_num(np, OAK_UNI_RX_RING_MAP(ring), 1, val);
	oak_unimac_set_bit_num(np, OAK_UNI_RX_RING_MAP(ring), 0, enable);
	oakdbg(debug, DRV, "np=%p chan=%d enable=%d", np, ring, enable);
}

/* Name        : oak_unimac_process_status
 * Returns     : void
 * Parameters  : ldg_t *ldg
 * Description : This function get the process status
 */
void oak_unimac_process_status(ldg_t *ldg)
{
	u32 irq_reason;
	u32 uni_status;

	irq_reason = oak_unimac_io_read_32(ldg->device, OAK_UNI_INTR);

	if ((irq_reason & OAK_UNI_INTR_SEVERE_ERRORS) != 0)
		oakdbg(debug, INTR,
		       "SEVERE unimac irq reason: 0x%x",
		       (u32)(irq_reason & OAK_UNI_INTR_SEVERE_ERRORS));

	if ((irq_reason & OAK_UNI_INTR_NORMAL_ERRORS) != 0)
		oakdbg(debug, INTR,
		       "NORMAL unimac irq reason: 0x%x",
		       (u32)(irq_reason & OAK_UNI_INTR_NORMAL_ERRORS));

	uni_status = oak_unimac_io_read_32(ldg->device, OAK_UNI_STAT);
	oakdbg(debug, INTR, "unimac status: 0x%x", uni_status);
	oak_unimac_io_write_32(ldg->device, OAK_UNI_INTR, irq_reason);
}

/* Name        : oak_unimac_rx_error
 * Returns     : void
 * Parameters  : ldg_t *ldg, u32 ring
 * Description : This function check interrupt cause reason and then if the
 * reason is valid refill the receive ring else increment the rx errors
 * counters.
 */
void oak_unimac_rx_error(ldg_t *ldg, u32 ring)
{
	oak_t *np = ldg->device;
	oak_rx_chan_t *rxc;
	u32 reason;

	rxc = &np->rx_channel[ring];

	reason = le32_to_cpup((__le32 *)&rxc->mbox->intr_cause);

	if ((reason & OAK_MBOX_RX_RES_LOW) != 0) {
		oak_net_rbr_refill(np, ring);
	} else {
		++rxc->stat.rx_errors;
		oakdbg(debug, RX_ERR, "reason=0x%x", reason);
	}
}

/* Name        : oak_unimac_tx_error
 * Returns     : void
 * Parameters  : ldg_t *ldg, u32 ring
 * Description : This function Tx error reason and then count errors
 */
void oak_unimac_tx_error(ldg_t *ldg, u32 ring)
{
	oak_t *np = ldg->device;
	oak_tx_chan_t *txc;
	u32 reason;

	txc = &np->tx_channel[ring];

	/* TX Ring Maximum Late Timestamp (Only Valid for Ring 2 to 9)
	 */
	oak_unimac_io_write_32(np, OAK_UNI_TX_RING_INT_CAUSE(ring),
			       (OAK_MBOX_TX_LATE_TS | OAK_MBOX_TX_ERR_HCRED));
	reason = le32_to_cpup((__le32 *)&txc->mbox->intr_cause);

	++txc->stat.tx_errors;

	oakdbg(debug, TX_ERR, "reason=0x%x", reason);
}

/* Name        : oak_unimac_ena_rx_ring_irq
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring, u32 enable
 * Description : This function enables the Rx ring irq.
 */
void oak_unimac_ena_rx_ring_irq(oak_t *np, u32 ring, u32 enable)
{
	if (enable != 0)
		enable = OAK_MBOX_RX_COMP | OAK_MBOX_RX_RES_LOW;
	oak_unimac_io_write_32(np, OAK_UNI_RX_RING_INT_MASK(ring), enable);
}

/* Name        : oak_unimac_ena_tx_ring_irq
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring, u32 enable
 * Description : This function enables the Tx ring irq.
 */
void oak_unimac_ena_tx_ring_irq(oak_t *np, u32 ring, u32 enable)
{
	if (enable != 0) {
		enable = OAK_MBOX_TX_COMP;

		if (ring >= 2)
			enable |= (OAK_MBOX_TX_LATE_TS | OAK_MBOX_TX_ERR_HCRED);
	}
	oak_unimac_io_write_32(np, OAK_UNI_TX_RING_INT_MASK(ring), enable);
}

/* Name        : oak_unimac_set_tx_ring_rate
 * Returns     : int
 * Parameters  : oak_t *np, u32 ring, u32 sr_class,
 *               u32 hi_credit, u32 r_kbps
 * Description : This function set tx ring rate limit.
 */
int oak_unimac_set_tx_ring_rate(oak_t *np, u32 ring, u32 sr_class,
				u32 hi_credit, u32 r_kbps)
{
	int rc = -EFAULT;
	u32 val;

	/* tx ring rate limit is only applicable for ring 2 to 9 */
	if (ring >= 2 && ring <= 9) {
		if (hi_credit <= OAK_MAX_TX_HI_CREDIT_BYTES) {
			val = ((sr_class & 1U) << 31);
			val |= ((hi_credit & OAK_MAX_TX_HI_CREDIT_BYTES) << 17);
			val |= (r_kbps & 0x1FFFFU);

			oak_unimac_io_write_32(np,
					       OAK_UNI_TX_RING_RATECTRL(ring),
					       val);
			rc = 0;
		}
	}
	oakdbg(debug, DRV,
	       "np=%p ring=%d sr_class=%d hi_credit=%d kbps=%d rc=%d",
	       np, ring, sr_class, hi_credit, r_kbps, rc);

	return rc;
}

/* Name        : oak_unimac_clr_tx_ring_rate
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring
 * Description : This function clear the tx ring rate limit
 */
void oak_unimac_clr_tx_ring_rate(oak_t *np, u32 ring)
{
	if (ring >= 2 && ring <= 9) {
		u32 val = oak_unimac_io_read_32(np,
						OAK_UNI_TX_RING_RATECTRL(ring));
		val &= 0x7FFF0000U;
		oak_unimac_io_write_32(np, OAK_UNI_TX_RING_RATECTRL(ring), val);
	}
}

/* Name        : oak_unimac_set_tx_mac_rate
 * Returns     : int
 * Parameters  : oak_t *np, u32 sr_class, u32 hi_credit,
 *               u32 r_kbps
 * Description : This function set transmision mac rate limit.
 */
int oak_unimac_set_tx_mac_rate(oak_t *np, u32 sr_class, u32 hi_credit,
			       u32 r_kbps)
{
	int rc = -EFAULT;
	u32 val;

	/* Check for High Credit Limit for Ring
	 * Defines the peak credit that can be accumulated while being blocked
	 * from transmission by another flow. This field is in bytes.
	 */
	if (hi_credit <= OAK_MAX_TX_HI_CREDIT_BYTES) {
		val = ((hi_credit & OAK_MAX_TX_HI_CREDIT_BYTES) << 17);
		val |= (r_kbps & 0x1FFFFU);

		/* Check for Stream Reservation Class
		 * 0x0 = Class A
		 * 0x1 = Class B
		 */
		if (sr_class == OAK_MIN_TX_RATE_CLASS_A)
			oak_unimac_io_write_32(np, OAK_UNI_TXRATE_A, val);
		else
			oak_unimac_io_write_32(np, OAK_UNI_TXRATE_B, val);
		rc = 0;
	}
	return rc;
}

/* Name        : oak_unimac_set_priority
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring, u32 prio, u32 reg
 * Description : This function set the priority
 */
static void oak_unimac_set_priority(oak_t *np, u32 ring, u32 prio, u32 reg)
{
	u32 val;

	if (ring <= 9) {
		if (np->pci_class_revision >= 1) {
			if (reg == OAK_UNI_DMA_TX_CH_CFG) {
				if (np->rrs >= 16) {
					val = oak_unimac_io_read_32(np, reg);
					/* Max burst size */
					val &= ~0x7FU;
					val |= ((np->rrs / 8) - 1) & 0x7FU;
					/* RR: bit-10 == 1 */
					val |= BIT(10);

					oak_unimac_io_write_32(np, reg, val);

					oakdbg(debug, DRV,
					       "TX max burst sz: %d, val=0x%x",
					       np->rrs, val);
				}
				oak_unimac_set_arbit(np, ring, prio,
						     OAK_DMA_TX_CH_SCHED_B0_LO);
			}
		} else {
			val = oak_unimac_io_read_32(np, reg);
			val |= (1U << 10);
			oak_unimac_io_write_32(np, reg, val);

			if (reg == OAK_UNI_DMA_RX_CH_CFG)
				oak_unimac_set_arbit(np, ring, prio,
						     OAK_DMA_RX_CH_SCHED_LO);
			else
				oak_unimac_set_arbit(np, ring, prio,
						     OAK_DMA_TX_CH_SCHED_LO);
		}
	}
}

/* Name        : oak_unimac_start_all_txq
 * Returns     : int
 * Parameters  : oak_t *np, u32 enable
 * Description : This function start all transmit queues
 */
int oak_unimac_start_all_txq(oak_t *np, u32 enable)
{
	int rc = 0;
	u32 i = 0;

	while (i < np->num_tx_chan) {
		rc = oak_unimac_start_tx_ring(np, i, enable);
		if (rc) {
			rc = 0;
		} else {
			rc = -EFAULT;
			break;
		}
		++i;
	}

	oakdbg(debug, IFUP, " rc: %d", rc);

	return rc;
}

/* Name        : oak_unimac_start_all_rxq
 * Returns     : int
 * Parameters  : oak_t *np, u32 enable
 * Description : This function start all receive queues
 */
int oak_unimac_start_all_rxq(oak_t *np, u32 enable)
{
	int rc = 0;
	u32 i = 0;

	while (i < np->num_rx_chan) {
		rc = oak_unimac_start_rx_ring(np, i, enable);
		if (rc) {
			rc = 0;
		} else {
			rc = -EFAULT;
			break;
		}
		++i;
	}

	oakdbg(debug, IFUP, " rc: %d", rc);

	return rc;
}

/* Name        : oak_unimac_set_channel_dma
 * Returns     : void
 * Parameters  : oak_t *np, int rto, int rxs, int txs, int chan
 * Description : This function set DMA channel
 */
static void oak_unimac_set_channel_dma(oak_t *np, int rto, u32 rxs, u32 txs,
				       int chan)
{
	u32 i = 0;
	u32 val;

	/* Set DMA for all the rx channel */
	while (i < np->num_rx_chan) {
		oak_unimac_set_dma_addr(np, np->rx_channel[i].rbr_dma,
					OAK_UNI_RX_RING_DBASE_LO(i),
					OAK_UNI_RX_RING_DBASE_HI(i));
		oak_unimac_set_dma_addr(np, np->rx_channel[i].rsr_dma,
					OAK_UNI_RX_RING_SBASE_LO(i),
					OAK_UNI_RX_RING_SBASE_HI(i));
		oak_unimac_set_dma_addr(np, np->rx_channel[i].mbox_dma,
					OAK_UNI_RX_RING_MBASE_LO(i),
					OAK_UNI_RX_RING_MBASE_HI(i));
		val = (np->rx_channel[i].rbr_bsize & 0xFFF8U) << 16;
		val |= rxs;

		oak_unimac_io_write_32(np, OAK_UNI_RX_RING_CFG(i), val);
		oak_unimac_io_write_32(np, OAK_UNI_RX_RING_PREF_THR(i),
				       RX_DESC_PREFETCH_TH);
		val = (np->rx_channel[i].rbr_size / 4);
		val <<= 16;

		oak_unimac_io_write_32(np, OAK_UNI_RX_RING_WATERMARK(i), val);
		val = (np->rx_channel[i].rbr_size / 8);
		val = oak_ilog2_kernel_utility(val);

		/* Set WR_THR to 0x5 (32 descriptors), maximum allowed value.
		 * If the number of completed descriptors is less than this
		 * value after a certain time as defined in the RX_RING_TIMEOUT,
		 * hardware will also write to the mailbox/status and trigger
		 * the interrupt.
		 */
		if (val > 5)
			val = 5;
		oak_unimac_io_write_32(np, OAK_UNI_RX_RING_MBOX_THR(i), val);
		oak_unimac_io_write_32(np, OAK_UNI_RX_RING_TIMEOUT(i),
				       OAK_RING_TOUT_USEC(rto));

		if (np->pci_class_revision >= 1)
			oak_unimac_set_arbit_priority(np, i, 0,
						      OAK_UNI_DMA_RX_CH_CFG);
		else
			oak_unimac_set_priority(np, i, 0,
						OAK_UNI_DMA_RX_CH_CFG);
		++i;
	}

	i = 0;

	/* Set DMA for all the tx channel */
	while (i < np->num_tx_chan) {
		oak_unimac_set_dma_addr(np, np->tx_channel[i].tbr_dma,
					OAK_UNI_TX_RING_DBASE_LO(i),
					OAK_UNI_TX_RING_DBASE_HI(i));
		oak_unimac_set_dma_addr(np, np->tx_channel[i].mbox_dma,
					OAK_UNI_TX_RING_MBASE_LO(i),
					OAK_UNI_TX_RING_MBASE_HI(i));

		oak_unimac_io_write_32(np, OAK_UNI_TX_RING_CFG(i), txs);
		oak_unimac_io_write_32(np, OAK_UNI_TX_RING_PREF_THR(i),
				       TX_DESC_PREFETCH_TH);
		oak_unimac_io_write_32(np, OAK_UNI_TX_RING_MBOX_THR(i),
				       TX_MBOX_WRITE_TH);
		oak_unimac_clr_tx_ring_rate(np, i);
		oak_unimac_io_write_32(np, OAK_UNI_TX_RING_TIMEOUT(i),
				       OAK_RING_TOUT_MSEC(10));

		if (np->pci_class_revision >= 1)
			oak_unimac_set_arbit_priority(np, i, 0,
						      OAK_UNI_DMA_TX_CH_CFG);
		oak_unimac_set_priority(np, i, 0,
					OAK_UNI_DMA_TX_CH_CFG);
		++i;
	}
	/* The below kernel routine help set real number of tx/rx queues.
	 * To avoid skbs mapped to queues greater than real number of tx/rx
	 * queues, stale skbs on the qdisc will be flushed in the below routine
	 */
	netif_set_real_num_tx_queues(np->netdev, np->num_tx_chan);
	netif_set_real_num_rx_queues(np->netdev, np->num_rx_chan);
}

/* Name        : oak_unimac_ena_ring
 * Returns     : u32
 * Parameters  : oak_t *np, u32 addr, u32 enable
 * Description : This function enables the ring
 */
static u32 oak_unimac_ena_ring(oak_t *np, u32 addr, u32 enable)
{
	u32 result;
	u32 count;

	if (enable != 0)
		enable = OAK_UNI_RING_ENABLE_REQ | OAK_UNI_RING_ENABLE_DONE;
	oak_unimac_io_write_32(np, addr, enable & OAK_UNI_RING_ENABLE_REQ);
	count = 1000;

	do {
		result = oak_unimac_io_read_32(np, addr);
		--count;

	} while (((enable & OAK_UNI_RING_ENABLE_DONE) !=
		 (result & OAK_UNI_RING_ENABLE_DONE)) && (count > 0));

#ifdef SIMULATION
	count = 1;
#endif

	return count;
}

/* Name        : oak_unimac_set_arbit
 * Returns     : void
 * Parameters  : oak_t *np, u32 ring, u32 weight, int reg
 * Description : This function set arbitrary value for ring.
 */
static void oak_unimac_set_arbit(oak_t *np, u32 ring,
				 u32 weight, u32 reg)
{
	u32 val;

	if (ring <= 7) {
		ring = ring * 4;
	} else {
		ring = (ring - 8) * 4;
		reg += 4;
	}
	val = oak_unimac_io_read_32(np, reg);
	val &= ~(0xFU << ring);
	val |= (weight << ring);

	oak_unimac_io_write_32(np, reg, val);
}

/* Name        : oak_unimac_start_tx_ring
 * Returns     : u32
 * Parameters  : oak_t *np, int32_t ring, u32 enable
 * Description : This function start rx ring
 */
u32 oak_unimac_start_tx_ring(oak_t *np, int32_t ring, u32 enable)
{
	if (enable != 0) {
		oak_unimac_io_write_32(np, OAK_UNI_TX_RING_INT_CAUSE(ring),
				       (OAK_MBOX_TX_COMP | OAK_MBOX_TX_LATE_TS |
				       OAK_MBOX_TX_ERR_HCRED));
		oak_unimac_io_write_32(np, OAK_UNI_TX_RING_CPU_PTR(ring), 0);
	}
	return oak_unimac_ena_ring(np, OAK_UNI_TX_RING_EN(ring), enable);
}

/* Name        : oak_unimac_start_rx_ring
 * Returns     : u32
 * Parameters  : oak_t *np, u32 ring, u32 enable
 * Description : This function start Rx ring
 */
u32 oak_unimac_start_rx_ring(oak_t *np, u32 ring, u32 enable)
{
	if (enable != 0)
		oak_unimac_io_write_32(np, OAK_UNI_RX_RING_INT_CAUSE(ring),
				       (OAK_MBOX_RX_COMP |
				       OAK_MBOX_RX_RES_LOW));
	return oak_unimac_ena_ring(np, OAK_UNI_RX_RING_EN(ring), enable);
}

/* Name        : oak_unimac_set_dma_addr
 * Returns     : void
 * Parameters  : oak_t *np, dma_addr_t phys, u32 reg_lo, u32 reg_hi
 * Description : This function set the DMA address
 */
static void oak_unimac_set_dma_addr(oak_t *np, dma_addr_t phys, u32 reg_lo,
				    u32 reg_hi)
{
	u32 addr;

	/* Low 32 bit */
	addr = (phys & 0xFFFFFFFFU);
	oak_unimac_io_write_32(np, reg_lo, addr);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	/* High 32 bit */
	addr = ((phys >> 32) & 0xFFFFFFFFU);
#else
	addr = 0;
#endif
	oak_unimac_io_write_32(np, reg_hi, addr);
}
