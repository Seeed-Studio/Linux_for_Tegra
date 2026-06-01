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
#ifndef H_OAK_ETHTOOL
#define H_OAK_ETHTOOL

#include <linux/ethtool.h>
/* Include for relation to classifier oak_unimac */
#include "oak_unimac.h"

/* Oak & Spruce max PCIe speed in Gbps */
#define OAK_MAX_SPEED    5
#define SPRUCE_MAX_SPEED 10

#define OAK_SPEED_1000 1
#define OAK_SPEED_2500 2
#define OAK_SPEED_5000 5

/* External variables declaration */
extern u32 debug;
extern const char oak_driver_name[];
extern const char oak_driver_version[];

/* Name      : get_stats
 * Returns   : void
 * Parameters: struct net_device * dev,  struct ethtool_stats * stats,
 * uint64_t * data
 */
void oak_ethtool_get_stats(struct net_device *dev,
			   struct ethtool_stats *stats, uint64_t *data);

/* Name      : get_sscnt
 * Returns   : int
 * Parameters:  oak_t * np
 */
int oak_ethtool_get_sscnt(struct net_device *dev, int stringset);

/* Name      : get_strings
 * Returns   : void
 * Parameters: struct net_device *dev, u32 stringset, u8 *data
 */
void oak_ethtool_get_strings(struct net_device *dev, u32 stringset,
			     u8 *data);

/* Name      : cap_cur_speed
 * Returns   : int
 * Parameters:  oak_t * np,  int pspeed
 */
u32 oak_ethtool_cap_cur_speed(oak_t *np, u32 pspeed);

/* Name      : get_link_ksettings
 * Returns   : int
 * Parameters:  net_device * dev,  ethtool_link_ksettings * ecmd
 */
int oak_ethtool_get_link_ksettings(struct net_device *dev,
				   struct ethtool_link_ksettings *ecmd);

/* Name        : oak_ethtool_get_drvinfo
 * Returns     : void
 * Parameters  : struct net_device *dev, struct ethtool_drvinfo *drvinfo
 * Description : This function copy driver name, version and PCI bus
 * information into ethtool driver information structure.
 */
void oak_ethtool_get_drvinfo(struct net_device *netdev,
			     struct ethtool_drvinfo *drvinfo);
#endif /* #ifndef H_OAK_ETHTOOL */

