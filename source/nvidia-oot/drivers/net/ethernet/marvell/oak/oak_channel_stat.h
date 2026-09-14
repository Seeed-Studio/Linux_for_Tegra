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
#ifndef H_OAK_CHANNEL_STAT
#define H_OAK_CHANNEL_STAT

typedef struct oak_chan_infostruct {
	u32 flags;
	u32 r_count;
	u32 r_size;
	u32 r_pend;
	u32 r_widx;
	u32 r_ridx;
	u32 r_len;
} oak_chan_info;

typedef struct oak_driver_rx_statstruct {
	u64 channel;
	u64 rx_alloc_pages;
	u64 rx_unmap_pages;
	u64 rx_alloc_error;
	u64 rx_frame_error;
	u64 rx_errors;
	u64 rx_interrupts;
	u64 rx_goodframe;
	u64 rx_byte_count;
	u64 rx_vlan;
	u64 rx_badframe;
	u64 rx_no_sof;
	u64 rx_no_eof;
	u64 rx_badcrc;
	u64 rx_badcsum;
	u64 rx_l4p_ok;
	u64 rx_ip4_ok;
	u64 rx_nores;
	u64 rx_64;
	u64 rx_128;
	u64 rx_256;
	u64 rx_512;
	u64 rx_1024;
	u64 rx_2048;
	u64 rx_fragments;
} oak_driver_rx_stat;

typedef struct oak_driver_tx_statstruct {
	u64 channel;
	u64 tx_frame_count;
	u64 tx_frame_compl;
	u64 tx_byte_count;
	u64 tx_fragm_count;
	u64 tx_drop;
	u64 tx_errors;
	u64 tx_interrupts;
	u64 tx_stall_count;
	u64 tx_64;
	u64 tx_128;
	u64 tx_256;
	u64 tx_512;
	u64 tx_1024;
	u64 tx_2048;
} oak_driver_tx_stat;

#endif /* #ifndef H_OAK_CHANNEL_STAT */

