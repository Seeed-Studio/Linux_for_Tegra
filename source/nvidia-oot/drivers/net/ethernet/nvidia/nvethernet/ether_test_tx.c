// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>
#include <linux/version.h>
#include <linux/iommu.h>
#ifdef HSI_SUPPORT
#include <linux/tegra-epl.h>
#endif
#include "ether_linux.h"
#include <linux/of.h>

#ifdef BW_TEST
static unsigned long long iterations = 0;
static uint32_t const g_num_frags = 1U;
static bool alloc = 0;
static char xmitbuffer[9000] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x01,
			    0x02, 0x03, 0x04, 0x05,
			    0x08, 0x06, 0x00, 0x01,
   			    0x08, 0x00, 0x06, 0x04, 0x00, 0x01, 0x00, 0x01,
			    0x02, 0x03, 0x04, 0x05, 0xc0, 0xa8, 0x01, 0x01,
   			    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xa8,
			    0x01, 0xc8};

#define TX_BW_RING_SIZE 16384
nveu64_t buf_phy_addr[TX_BW_RING_SIZE];
void* buf_virt_addr[TX_BW_RING_SIZE];

static int alloc_dma(struct device *dev,  struct ether_priv_data *pdata)
{
	void *cpu_addr;
	dma_addr_t dma_handle;

	size_t size = pdata->tx_bandwidth_pkt_size;  // Size of the buffer
	enum dma_data_direction direction = DMA_TO_DEVICE;
	int i = 0;

	for (i = 0 ; i < TX_BW_RING_SIZE; i++) {
		// Allocate a buffer in kernel space
		cpu_addr = kmalloc(size, GFP_KERNEL);
		if (!cpu_addr) {
			printk(KERN_ERR "Failed to allocate memory\n");
			return -1;
		}

		// Clear the buffer
		memset(cpu_addr, 0, size);
		memcpy(cpu_addr, xmitbuffer, size);
		// Map the buffer to a DMA address
		dma_handle = dma_map_single(dev, cpu_addr, size, direction);

		// Check if mapping succeeded
		if (dma_mapping_error(dev, dma_handle)) {
			printk(KERN_ERR "DMA mapping failed\n");
			kfree(cpu_addr);
			return -1;
		}
		buf_phy_addr[i] = dma_handle;
		buf_virt_addr[i] = cpu_addr;
	}
	return 0;
}

static int32_t ether_tx_swcx_populate(struct osi_tx_ring *tx_ring, struct ether_priv_data *pdata)
{
	struct osi_tx_pkt_cx *tx_pkt_cx = &tx_ring->tx_pkt_cx;
	struct osi_tx_swcx *tx_swcx = NULL;
	uint32_t cnt  = 0;
	nveu32_t cur_tx_idx = tx_ring->cur_tx_idx;
	(void) memset((void *)tx_pkt_cx, 0, sizeof(*tx_pkt_cx));

	tx_pkt_cx->payload_len = pdata->tx_bandwidth_pkt_size;
	//tx_pkt_cx->flags |= OSI_PKT_CX_LEN;

	while (cnt < g_num_frags) {
		tx_swcx = tx_ring->tx_swcx + cur_tx_idx;

		if (tx_swcx->len != 0U) {
			/* should not hit this case */
			return -1;
		}

		if (cur_tx_idx > TX_BW_RING_SIZE) {
			printk(KERN_ERR "INVALID RING SIZE\n");
			return -1;
		}
		tx_swcx->len = pdata->tx_bandwidth_pkt_size;
		/*update mbuf to last software context only */
		tx_swcx->buf_virt_addr = (void *) buf_virt_addr[cur_tx_idx];
		tx_swcx->buf_phy_addr =  buf_phy_addr[cur_tx_idx];

		INCR_TX_DESC_INDEX(cur_tx_idx, pdata->osi_dma->tx_ring_sz);
		cnt++;
	}
	tx_pkt_cx->desc_cnt = g_num_frags;

	(void) tx_ring;
	return 0;
}

static void free_tx_dma_resources(struct osi_dma_priv_data *osi_dma,
				  unsigned int qinx)
{
	unsigned int i;
	struct osi_tx_ring *tx_ring = NULL;
	unsigned int chan = osi_dma->dma_chans[qinx];
	nveu32_t cur_tx_idx = 0U;
	struct osi_tx_swcx *tx_swcx = NULL;
  
	tx_ring = osi_dma->tx_ring[chan];
	osi_process_tx_completions(osi_dma, chan, osi_dma->tx_ring_sz);

	for (i = 0; i < osi_dma->tx_ring_sz; i++) {
		tx_swcx = tx_ring->tx_swcx + cur_tx_idx;
		tx_swcx->buf_virt_addr = NULL;
		tx_swcx->buf_phy_addr =  0;
		tx_swcx->len =  0;
		INCR_TX_DESC_INDEX(cur_tx_idx, osi_dma->tx_ring_sz);
	}
}

static uint32_t ether_avail_txdesc_cnt_test(const struct osi_tx_ring *tx_ring,
					    struct osi_dma_priv_data *osi_dma)
{
	if ((tx_ring->clean_idx >= osi_dma->tx_ring_sz) ||
	    (tx_ring->cur_tx_idx >= osi_dma->tx_ring_sz)) {
		return 0;
	}

	return ((tx_ring->clean_idx - tx_ring->cur_tx_idx - 1U) &
		(osi_dma->tx_ring_sz - 1U));
}

static inline int32_t ether_transmit(struct ether_priv_data *pdata, unsigned int qinx)
{
	//struct ethernet *e = &gethernet;
	struct osi_dma_priv_data *osi_dma = pdata->osi_dma;
	struct osi_tx_ring *tx_ring = NULL;
	int32_t count = 0;
	unsigned int chan = osi_dma->dma_chans[qinx];

	tx_ring = osi_dma->tx_ring[chan];
	if (tx_ring == NULL) {
		dev_err(pdata->dev, "Invalid Tx Ring Error %s\n",
			__func__);
		return -2;
	}

	if (ether_avail_txdesc_cnt_test(tx_ring, osi_dma) < g_num_frags) {
		// TODO
		if ((iterations % 100000000) == 0) {
			pr_info("Error Tx Add Wait desc %s %d print_once %llu \n",
				__func__, ether_avail_txdesc_cnt_test(tx_ring, osi_dma), iterations);
		}
		osi_process_tx_completions(osi_dma, chan, 512);
		return -3;
	}

	count = ether_tx_swcx_populate(tx_ring, pdata);
	if (count < 0) {
		return -4;
	}

	if ((iterations % 50) == 0) {
		tx_ring->skip_dmb = 0;
	} else {
		tx_ring->skip_dmb = 1;
	}

	if (osi_hw_transmit(osi_dma, chan) < 0) {
		return -5;
	}

	if ((iterations % 50) == 0) {
		osi_process_tx_completions(osi_dma, chan, 50);
	}
	return 0;
}

static inline void test_transmit(bool test, bool loop, struct ether_priv_data *pdata)
{
	// Sample Code to send transfer data
	struct osi_dma_priv_data *osi_dma = pdata->osi_dma;
	unsigned int qinx = 0;

	while (test && pdata->test_tx_bandwidth) {
		if (ether_transmit(pdata, qinx) < 0) {
			// Retry after delay
			//msleep(1);
		}

		if (!loop) {
			break;
		}
		iterations++;
	}

	// flush all the buffers
	free_tx_dma_resources(osi_dma, qinx);
}

/**
 * @brief start tx bandwidth work
 *
 */
void ether_tx_bandwidth_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct ether_priv_data *pdata = container_of(dwork,
					struct ether_priv_data, tx_bandwidth_work);
	struct device *dev = pdata->dev;
	if (alloc == 0) {
		if (alloc_dma(dev, pdata) < 0) {
			dev_err(pdata->dev, "ALLOCATION Failed %s\n",
				__func__);
		}
		alloc = 1;
	}
	test_transmit(1, 1, pdata);
}
#endif