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
#include "oak_ethtool.h"
#include "oak_net.h"

static const char umac_strings[][ETH_GSTRING_LEN] = {
	{"rx_good_frames"},
	{"rx_bad_frames"},
	{"rx_stall_fifo"},
	{"rx_stall_desc"},
	{"rx_discard_desc"},
	{"tx_pause"},
	{"tx_stall_fifo"},
};

static const u8 rx_strings[][ETH_GSTRING_LEN] = {
	{"Rx Channel"},
	{"rx_alloc_pages"},
	{"rx_unmap_pages"},
	{"rx_alloc_error"},
	{"rx_frame_error"},
	{"rx_errors"},
	{"rx_interrupts"},
	{"rx_good_frames"},
	{"rx_byte_count"},
	{"rx_vlan"},
	{"rx_bad_frames"},
	{"rx_no_sof"},
	{"rx_no_eof"},
	{"rx_bad_crc"},
	{"rx_bad_csum"},
	{"rx_l4p_ok"},
	{"rx_ip4_ok"},
	{"rx_bad_nores"},
	{"rx_64"},
	{"rx_128"},
	{"rx_256"},
	{"rx_512"},
	{"rx_1024"},
	{"rx_2048"},
	{"rx_fragments"},
};

static const u8 tx_strings[][ETH_GSTRING_LEN] = {
	{"Tx Channel"},
	{"tx_frame_count"},
	{"tx_frame_compl"},
	{"tx_byte_count"},
	{"tx_fragm_count"},
	{"tx_drop"},
	{"tx_errors"},
	{"tx_interrupts"},
	{"tx_stall_count"},
	{"tx_64"},
	{"tx_128"},
	{"tx_256"},
	{"tx_512"},
	{"tx_1024"},
	{"tx_2048"},
};

/* private function prototypes */
static void oak_ethtool_get_txc_stats(oak_t *np, u64 **data);
static void oak_ethtool_get_rxc_stats(oak_t *np, u64 **data);
static void oak_ethtool_get_stall_stats(oak_t *np);
static void oak_ethtool_get_misc_stats(oak_t *np);

/* Name        : oak_ethtool_get_rxc_stats
 * Returns     : void
 * Parameters  : oak_t *np, u64 **data
 * Description : This function copy Rx channel stats
 */
static void oak_ethtool_get_rxc_stats(oak_t *np, u64 **data)
{
	u32 i;

	**data = 0;
	for (i = 0; i < np->num_rx_chan; i++) {
		oak_rx_chan_t *rxc = &np->rx_channel[i];
		/* Copy rx channel statistics */

		memcpy(*data, &rxc->stat, sizeof(oak_driver_rx_stat));
		**data = i + 1;
		*data += (sizeof(oak_driver_rx_stat) / sizeof(u64));
	}
}

/* Name        : oak_ethtool_get_txc_stats
 * Returns     : void
 * Parameters  : oak_t *np, u64 **data
 * Description : This function copy Tx channel stats
 */
static void oak_ethtool_get_txc_stats(oak_t *np, u64 **data)
{
	u32 i;

	**data = 0;
	for (i = 0; i < np->num_tx_chan; i++) {
		oak_tx_chan_t *txc = &np->tx_channel[i];
		/* Copy tx channel statistics */
		memcpy(*data, &txc->stat, sizeof(oak_driver_tx_stat));
		**data = i + 1;
		*data += (sizeof(oak_driver_tx_stat) / sizeof(u64));
	}
}

/* Name        : oak_ethtool_get_stall_stats
 * Returns     : void
 * Parameters  : oak_t *np
 * Description : This function get the tx or rx stall counter statistics of the
 * Ethernet interface.
 */
static void oak_ethtool_get_stall_stats(oak_t *np)
{
	np->unimac_stat.tx_stall_fifo =
		oak_unimac_io_read_32(np, OAK_UNI_STAT_TX_STALL_FIFO);
	np->unimac_stat.rx_stall_desc =
		oak_unimac_io_read_32(np, OAK_UNI_STAT_RX_STALL_DESC);
	np->unimac_stat.rx_stall_fifo =
		oak_unimac_io_read_32(np, OAK_UNI_STAT_RX_STALL_FIFO);
}

/* Name        : oak_ethtool_get_misc_stats
 * Returns     : void
 * Parameters  : oak_t *np
 * Description : This function get the tx/rx good, bad, pause, disc descriptor
 * statistics of the Ethernet interface.
 */
static void oak_ethtool_get_misc_stats(oak_t *np)
{
	np->unimac_stat.tx_pause =
		oak_unimac_io_read_32(np, OAK_UNI_STAT_TX_PAUSE);
	np->unimac_stat.rx_good_frames =
		oak_unimac_io_read_32(np, OAK_UNI_STAT_RX_GOOD_FRAMES);
	np->unimac_stat.rx_bad_frames =
		oak_unimac_io_read_32(np, OAK_UNI_STAT_RX_BAD_FRAMES);
	np->unimac_stat.rx_discard_desc =
		oak_unimac_io_read_32(np, OAK_UNI_STAT_RX_DISC_DESC);
}

/* Name        : oak_ethtool_get_stats
 * Returns     : void
 * Parameters  : struct net_device *dev, struct ethtool_stats *stats,
 * u64 *data
 * Description : This function reads the statistics of the Ethernet interface.
 */
void oak_ethtool_get_stats(struct net_device *dev,
			   struct ethtool_stats *stats, u64 *data)
{
	oak_t *np = netdev_priv(dev);

	/* Get tx/rx channels stall and misc counters statistics */
	oak_ethtool_get_stall_stats(np);
	oak_ethtool_get_misc_stats(np);

	memcpy(data, &np->unimac_stat, sizeof(np->unimac_stat));
	data += sizeof(np->unimac_stat) / sizeof(u64);
	/* Get rx/tx channel statistics */
	oak_ethtool_get_rxc_stats(np, &data);
	oak_ethtool_get_txc_stats(np, &data);
}

/* Name        : oak_ethtool_get_sscnt
 * Returns     : int
 * Parameters  : struct net_device *dev, int stringset
 * Description : This function read the String Set Count value of the
 * Ethernet interface.
 */
int oak_ethtool_get_sscnt(struct net_device *dev, int stringset)
{
	int retval;
	oak_t *np = netdev_priv(dev);

	/* Get the string set count statistics */
	if (stringset == ETH_SS_STATS) {
		retval = sizeof(np->unimac_stat) / sizeof(u64);
		retval += (np->num_rx_chan *
			sizeof(oak_driver_rx_stat) / sizeof(u64));
		retval += (np->num_tx_chan *
			sizeof(oak_driver_tx_stat) / sizeof(u64));
	} else {
		retval = -EINVAL;
	}

	return retval;
}

/* Name        : oak_ethtool_get_strings
 * Returns     : void
 * Parameters  : struct net_device *dev, u32 stringset, u8 *data
 * Description : This function get the Tx and Rx channel strings value of the
 * Ethernet interface.
 */
void oak_ethtool_get_strings(struct net_device *dev, u32 stringset,
			     u8 *data)
{
	*data = 0;
	if (stringset == ETH_SS_STATS) {
		u32 off = 0;
		u32 i;
		oak_t *np = netdev_priv(dev);

		memcpy(&data[off], umac_strings, sizeof(umac_strings));
		off += sizeof(umac_strings);

		/* Copy statistics data into rx channel structure */
		for (i = 0; i < np->num_rx_chan; i++) {
			memcpy(&data[off], rx_strings, sizeof(rx_strings));
			off += sizeof(rx_strings);
		}

		/* Copy statistics data into tx channel structure */
		for (i = 0; i < np->num_tx_chan; i++) {
			memcpy(&data[off], tx_strings, sizeof(tx_strings));
			off += sizeof(tx_strings);
		}
	}
}

/* Name        : oak_ethtool_get_cur_speed
 * Returns     : int
 * Parameters  : oak_t *np, int pspeed
 * Description : This function caps the current PCIe speed for the Oak/Spruce
 * switch.
 */
u32 oak_ethtool_cap_cur_speed(oak_t *np, u32 pspeed)
{
	enum pcie_link_width wdth;

	wdth = oak_net_pcie_get_width_cap(np->pdev);
	if (wdth == PCIE_LNK_X1) { /* Oak */
		if (pspeed > OAK_MAX_SPEED)
			pspeed = OAK_MAX_SPEED;
	} else if (wdth == PCIE_LNK_X2) {
		if (pspeed > SPRUCE_MAX_SPEED)
			pspeed = SPRUCE_MAX_SPEED;
	}

	return pspeed;
}

/* Name        : ethtool_get_link_ksettings
 * Returns     : int
 * Parameters  : struct net_device *dev,  struct ethtool_link_ksettings *ecmd
 * Description : This function get the current port link settings of the
 * Ethernet interface.
 */
int oak_ethtool_get_link_ksettings(struct net_device *dev,
				   struct ethtool_link_ksettings *ecmd)
{
	oak_t *oak;
	u32 supported, advertising;

	oak = netdev_priv(dev);

	supported = 0;
	advertising = 0;

	memset(ecmd, 0, sizeof(*ecmd));
	if (oak->speed == OAK_SPEED_1000) {
		ecmd->base.speed = SPEED_1000;
	supported |= SUPPORTED_1000baseT_Full |
			SUPPORTED_1000baseT_Half |
			SUPPORTED_100baseT_Full  |
			SUPPORTED_100baseT_Half  |
			SUPPORTED_10baseT_Full   |
			SUPPORTED_10baseT_Half;
	supported |= SUPPORTED_Autoneg;
	advertising |= ADVERTISED_1000baseT_Full |
			ADVERTISED_1000baseT_Half |
			ADVERTISED_100baseT_Full  |
			ADVERTISED_100baseT_Half  |
			ADVERTISED_10baseT_Full   |
			ADVERTISED_10baseT_Half;
	} else if (oak->speed == OAK_SPEED_2500) {
		ecmd->base.speed = SPEED_2500;
		supported = SUPPORTED_10000baseT_Full;
		advertising = ADVERTISED_2500baseX_Full;
	} else if (oak->speed == OAK_SPEED_5000) {
		ecmd->base.speed = SPEED_5000;
		supported = SUPPORTED_10000baseT_Full;
		advertising = ADVERTISED_10000baseT_Full;
	} else {
		ecmd->base.speed = SPEED_10000;
		supported = SUPPORTED_10000baseT_Full;
		supported |= SUPPORTED_TP;
		advertising = ADVERTISED_10000baseT_Full;
	}
	ecmd->base.port = PORT_TP;
	ecmd->base.duplex = DUPLEX_FULL;
	ecmd->base.autoneg = AUTONEG_ENABLE;

	/* This function was added in kernel 4.7 in commit 6d62b4d5fac62 ("net:
	 * ethtool: export conversion function between u32 and link mode")
	 */
	ethtool_convert_legacy_u32_to_link_mode(ecmd->link_modes.supported,
						supported);
	ethtool_convert_legacy_u32_to_link_mode(ecmd->link_modes.advertising,
						advertising);

	return 0;
}

/* Name        : oak_ethtool_get_drvinfo
 * Returns     : void
 * Parameters  : struct net_device *dev, struct ethtool_drvinfo *drvinfo
 * Description : This function copy driver name, version and PCI bus
 * information into ethtool driver information structure.
 */
void oak_ethtool_get_drvinfo(struct net_device *netdev,
			     struct ethtool_drvinfo *drvinfo)
{
	oak_t *adapter = netdev_priv(netdev);

	/* Copy a C-string into a sized buffer
	 * Copy driver name, version and bus information
	 */
	strscpy(drvinfo->driver, oak_driver_name, sizeof(drvinfo->driver));
	strscpy(drvinfo->version, oak_driver_version,
		sizeof(drvinfo->version));
	strscpy(drvinfo->bus_info, pci_name(adapter->pdev),
		sizeof(drvinfo->bus_info));
}
