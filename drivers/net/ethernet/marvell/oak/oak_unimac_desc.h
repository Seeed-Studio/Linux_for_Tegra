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
#ifndef H_OAK_UNIMAC_DESC
#define H_OAK_UNIMAC_DESC

typedef struct oak_rxd_tstruct {
	u32 buf_ptr_lo;
	u32 buf_ptr_hi;
} oak_rxd_t;

typedef struct oak_rxs_tstruct {
	u32 bc : 16;
	u32 es : 1;
	u32 ec : 2;
	u32 res1 : 1;
	u32 first_last : 2;
	u32 ipv4_hdr_ok : 1;
	u32 l4_chk_ok : 1;
	u32 l4_prot : 2;
	u32 res2 : 1;
	u32 l3_ipv4 : 1;
	u32 l3_ipv6 : 1;
	u32 vlan : 1;
	u32 l2_prot : 2;
	u32 timestamp : 32;
	u32 rc_chksum : 16;
	u32 udp_cs_0 : 1;
	u32 res3 : 15;
	u32 mhdr : 16;
	u32 mhok : 1;
	u32 res4 : 15;
} oak_rxs_t;

typedef struct oak_txd_tstruct {
	u32 bc : 16;
	u32 res1 : 4;
	u32 last : 1;
	u32 first : 1;
	u32 gl3_chksum : 1;
	u32 gl4_chksum : 1;
	u32 res2 : 4;
	u32 time_valid : 1;
	u32 res3 : 3;
	u32 timestamp : 32;
	u32 buf_ptr_lo : 32;
	u32 buf_ptr_hi : 32;
} oak_txd_t;

#endif /* #ifndef H_OAK_UNIMAC_DESC */

