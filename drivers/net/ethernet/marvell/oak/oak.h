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
#ifndef H_OAK
#define H_OAK

/* Include for relation to classifier oak_unimac */
#include "oak_unimac.h"
/* Include for relation to classifier oak_net */
#include "oak_net.h"
/* Include for relation to classifier oak_ethtool */
#include "oak_ethtool.h"
#include "oak_debug.h"
/* Include for relation to classifier oak_module */
#include "oak_module.h"
#include "oak_chksum.h"
#include "oak_dpm.h"

#define OAK_DRIVER_NAME "oak"

#define OAK_DRIVER_STRING "Marvell PCIe Switch Driver"

#define OAK_DRIVER_VERSION "3.01.0001"

#define OAK_DRIVER_COPYRIGHT "Copyright (c) Marvell - 2018"

#define OAK_MAX_JUMBO_FRAME_SIZE (10 * 1024)

/* EPU data register offset definition */
#define OAK_EPU_DATA2 0xF208
#define OAK_EPU_DATA3 0xF20C

u32 debug;
u32 txs;
u32 rxs;
int chan;
int rto;
int mhdr;
u32 port_speed;

#endif /* #ifndef H_OAK */

