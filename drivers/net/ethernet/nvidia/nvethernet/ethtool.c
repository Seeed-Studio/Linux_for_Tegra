// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2019-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include <linux/version.h>
#include "ether_linux.h"

/**
 * @addtogroup MMC Stats array length.
 *
 * @brief Helper macro to find MMC stats array length.
 * @{
 */
#define OSI_ARRAY_SIZE(x)  ((int)sizeof((x)) / (int)sizeof((x)[0]))
#define ETHER_MMC_STATS_LEN OSI_ARRAY_SIZE(ether_mmc)
/** @} */

/**
 * @brief Ethernet stats
 */
struct ether_stats {
	/** Name of the stat */
	char stat_string[ETH_GSTRING_LEN];
	/** size of the stat */
	size_t sizeof_stat;
	/** stat offset */
	size_t stat_offset;
};

#ifndef OSI_STRIPPED_LIB
/**
 * @brief Name of FRP statistics, with length of name not more than
 * ETH_GSTRING_LEN
 */
#define ETHER_PKT_FRP_STAT(y) \
{ (#y), sizeof_field(struct osi_pkt_err_stats, y), \
	offsetof(struct osi_dma_priv_data, pkt_err_stats.y)}

/**
 * @brief FRP statistics
 */
static const struct ether_stats ether_frpstrings_stats[] = {
	ETHER_PKT_FRP_STAT(frp_parsed),
	ETHER_PKT_FRP_STAT(frp_dropped),
	ETHER_PKT_FRP_STAT(frp_err),
	ETHER_PKT_FRP_STAT(frp_incomplete),
};

/**
 * @brief Ethernet FRP statistics array length
 */
#define ETHER_FRP_STAT_LEN OSI_ARRAY_SIZE(ether_frpstrings_stats)

/**
 * @brief Name of pkt_err statistics, with length of name not more than
 * ETH_GSTRING_LEN
 */
#define ETHER_PKT_ERR_STAT(y) \
{ (#y), sizeof_field(struct osi_pkt_err_stats, y), \
	offsetof(struct osi_dma_priv_data, pkt_err_stats.y)}

/**
 * @brief ETHER pkt_err statistics
 */
static const struct ether_stats ether_cstrings_stats[] = {
	ETHER_PKT_ERR_STAT(ip_header_error),
	ETHER_PKT_ERR_STAT(jabber_timeout_error),
	ETHER_PKT_ERR_STAT(pkt_flush_error),
	ETHER_PKT_ERR_STAT(payload_cs_error),
	ETHER_PKT_ERR_STAT(loss_of_carrier_error),
	ETHER_PKT_ERR_STAT(no_carrier_error),
	ETHER_PKT_ERR_STAT(late_collision_error),
	ETHER_PKT_ERR_STAT(excessive_collision_error),
	ETHER_PKT_ERR_STAT(excessive_deferal_error),
	ETHER_PKT_ERR_STAT(underflow_error),
	ETHER_PKT_ERR_STAT(rx_crc_error),
	ETHER_PKT_ERR_STAT(rx_frame_error),
	ETHER_PKT_ERR_STAT(clear_tx_err),
	ETHER_PKT_ERR_STAT(clear_rx_err),
};

/**
 * @brief pkt_err statistics array length
 */
#define ETHER_PKT_ERR_STAT_LEN OSI_ARRAY_SIZE(ether_cstrings_stats)

/**
 * @brief Name of extra DMA stat, with length of name not more than ETH_GSTRING_LEN
 */
#define ETHER_DMA_EXTRA_STAT(a) \
{ (#a), sizeof_field(struct osi_xtra_dma_stat_counters, a), \
	offsetof(struct osi_dma_priv_data, dstats.a)}
/**
 * @brief Ethernet DMA extra statistics
 */
static const struct ether_stats ether_dstrings_stats[] = {
	ETHER_DMA_EXTRA_STAT(tx_clean_n[0]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[1]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[2]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[3]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[4]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[5]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[6]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[7]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[8]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[9]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[10]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[11]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[12]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[13]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[14]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[15]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[16]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[17]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[18]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[19]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[20]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[21]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[22]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[23]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[24]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[25]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[26]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[27]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[28]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[29]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[30]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[31]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[32]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[33]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[34]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[35]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[36]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[37]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[38]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[39]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[40]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[41]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[42]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[43]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[44]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[45]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[46]),
	ETHER_DMA_EXTRA_STAT(tx_clean_n[47]),

	/* Tx/Rx frames */
	ETHER_DMA_EXTRA_STAT(tx_pkt_n),
	ETHER_DMA_EXTRA_STAT(rx_pkt_n),
	ETHER_DMA_EXTRA_STAT(tx_vlan_pkt_n),
	ETHER_DMA_EXTRA_STAT(rx_vlan_pkt_n),
	ETHER_DMA_EXTRA_STAT(tx_tso_pkt_n),

	/* Tx/Rx frames per channels/queues */
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[0]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[1]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[2]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[3]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[4]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[5]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[6]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[7]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[8]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[9]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[10]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[11]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[12]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[13]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[14]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[15]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[16]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[17]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[18]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[19]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[21]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[22]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[23]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[24]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[25]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[26]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[27]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[28]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[29]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[30]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[31]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[32]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[33]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[34]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[35]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[36]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[37]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[38]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[39]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[40]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[41]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[42]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[43]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[44]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[45]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[46]),
	ETHER_DMA_EXTRA_STAT(chan_tx_pkt_n[47]),

	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[0]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[1]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[2]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[3]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[4]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[5]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[6]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[7]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[8]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[9]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[10]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[11]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[12]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[13]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[14]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[15]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[16]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[17]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[18]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[19]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[20]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[21]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[22]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[23]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[24]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[25]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[26]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[27]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[28]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[29]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[30]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[31]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[32]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[33]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[34]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[35]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[36]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[37]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[38]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[39]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[40]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[41]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[42]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[43]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[44]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[45]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[46]),
	ETHER_DMA_EXTRA_STAT(chan_rx_pkt_n[47]),
};

/**
 * @brief Ethernet extra DMA statistics array length
 */
#define ETHER_EXTRA_DMA_STAT_LEN OSI_ARRAY_SIZE(ether_dstrings_stats)
#endif /* OSI_STRIPPED_LIB */

/**
 * @brief Name of extra Ethernet stats, with length of name not more than
 * ETH_GSTRING_LEN MAC
 */
#define ETHER_EXTRA_STAT(b) \
{ #b, sizeof_field(struct ether_xtra_stat_counters, b), \
	offsetof(struct ether_priv_data, xstats.b)}
/**
 * @brief Ethernet extra statistics
 */
static const struct ether_stats ether_gstrings_stats[] = {
	ETHER_EXTRA_STAT(re_alloc_rxbuf_failed[0]),
	ETHER_EXTRA_STAT(re_alloc_rxbuf_failed[1]),
	ETHER_EXTRA_STAT(re_alloc_rxbuf_failed[2]),
	ETHER_EXTRA_STAT(re_alloc_rxbuf_failed[3]),
	ETHER_EXTRA_STAT(re_alloc_rxbuf_failed[4]),
	ETHER_EXTRA_STAT(re_alloc_rxbuf_failed[5]),
	ETHER_EXTRA_STAT(re_alloc_rxbuf_failed[6]),
	ETHER_EXTRA_STAT(re_alloc_rxbuf_failed[7]),
	ETHER_EXTRA_STAT(re_alloc_rxbuf_failed[8]),
	ETHER_EXTRA_STAT(re_alloc_rxbuf_failed[9]),


	/* Tx/Rx IRQ Events */
	ETHER_EXTRA_STAT(tx_normal_irq_n[0]),
	ETHER_EXTRA_STAT(tx_normal_irq_n[1]),
	ETHER_EXTRA_STAT(tx_normal_irq_n[2]),
	ETHER_EXTRA_STAT(tx_normal_irq_n[3]),
	ETHER_EXTRA_STAT(tx_normal_irq_n[4]),
	ETHER_EXTRA_STAT(tx_normal_irq_n[5]),
	ETHER_EXTRA_STAT(tx_normal_irq_n[6]),
	ETHER_EXTRA_STAT(tx_normal_irq_n[7]),
	ETHER_EXTRA_STAT(tx_normal_irq_n[8]),
	ETHER_EXTRA_STAT(tx_normal_irq_n[9]),
	ETHER_EXTRA_STAT(tx_usecs_swtimer_n[0]),
	ETHER_EXTRA_STAT(tx_usecs_swtimer_n[1]),
	ETHER_EXTRA_STAT(tx_usecs_swtimer_n[2]),
	ETHER_EXTRA_STAT(tx_usecs_swtimer_n[3]),
	ETHER_EXTRA_STAT(tx_usecs_swtimer_n[4]),
	ETHER_EXTRA_STAT(tx_usecs_swtimer_n[5]),
	ETHER_EXTRA_STAT(tx_usecs_swtimer_n[6]),
	ETHER_EXTRA_STAT(tx_usecs_swtimer_n[7]),
	ETHER_EXTRA_STAT(tx_usecs_swtimer_n[8]),
	ETHER_EXTRA_STAT(tx_usecs_swtimer_n[9]),
	ETHER_EXTRA_STAT(rx_normal_irq_n[0]),
	ETHER_EXTRA_STAT(rx_normal_irq_n[1]),
	ETHER_EXTRA_STAT(rx_normal_irq_n[2]),
	ETHER_EXTRA_STAT(rx_normal_irq_n[3]),
	ETHER_EXTRA_STAT(rx_normal_irq_n[4]),
	ETHER_EXTRA_STAT(rx_normal_irq_n[5]),
	ETHER_EXTRA_STAT(rx_normal_irq_n[6]),
	ETHER_EXTRA_STAT(rx_normal_irq_n[7]),
	ETHER_EXTRA_STAT(rx_normal_irq_n[8]),
	ETHER_EXTRA_STAT(rx_normal_irq_n[9]),
	ETHER_EXTRA_STAT(link_disconnect_count),
	ETHER_EXTRA_STAT(link_connect_count),
};

/**
 * @brief Ethernet extra statistics array length
 */
#define ETHER_EXTRA_STAT_LEN OSI_ARRAY_SIZE(ether_gstrings_stats)

/**
 * @brief HW MAC Management counters
 * 	  Structure variable name MUST up to MAX length of ETH_GSTRING_LEN
 */
#define ETHER_MMC_STAT(c) \
{ #c, sizeof_field(struct osi_mmc_counters, c), \
	offsetof(struct osi_core_priv_data, mmc.c)}

#ifdef MACSEC_SUPPORT
#define MACSEC_MMC_STAT(c) \
{ #c, sizeof_field(struct osi_macsec_mmc_counters, c), \
	offsetof(struct osi_core_priv_data, macsec_mmc.c)}

#define MACSEC_IRQ_STAT(c) \
{ #c, sizeof_field(struct osi_macsec_irq_stats, c), \
	offsetof(struct osi_core_priv_data, macsec_irq_stats.c)}
#endif /* MACSEC_SUPPORT */

/**
 * @brief MMC statistics
 */
static const struct ether_stats ether_mmc[] = {
	/* MMC TX counters */
	ETHER_MMC_STAT(mmc_tx_octetcount_gb),
	ETHER_MMC_STAT(mmc_tx_framecount_gb),
	ETHER_MMC_STAT(mmc_tx_broadcastframe_g),
	ETHER_MMC_STAT(mmc_tx_multicastframe_g),
	ETHER_MMC_STAT(mmc_tx_64_octets_gb),
	ETHER_MMC_STAT(mmc_tx_65_to_127_octets_gb),
	ETHER_MMC_STAT(mmc_tx_128_to_255_octets_gb),
	ETHER_MMC_STAT(mmc_tx_256_to_511_octets_gb),
	ETHER_MMC_STAT(mmc_tx_512_to_1023_octets_gb),
	ETHER_MMC_STAT(mmc_tx_1024_to_max_octets_gb),
	ETHER_MMC_STAT(mmc_tx_unicast_gb),
	ETHER_MMC_STAT(mmc_tx_multicast_gb),
	ETHER_MMC_STAT(mmc_tx_broadcast_gb),
	ETHER_MMC_STAT(mmc_tx_underflow_error),
	ETHER_MMC_STAT(mmc_tx_singlecol_g),
	ETHER_MMC_STAT(mmc_tx_multicol_g),
	ETHER_MMC_STAT(mmc_tx_deferred),
	ETHER_MMC_STAT(mmc_tx_latecol),
	ETHER_MMC_STAT(mmc_tx_exesscol),
	ETHER_MMC_STAT(mmc_tx_carrier_error),
	ETHER_MMC_STAT(mmc_tx_octetcount_g),
	ETHER_MMC_STAT(mmc_tx_framecount_g),
	ETHER_MMC_STAT(mmc_tx_excessdef),
	ETHER_MMC_STAT(mmc_tx_pause_frame),
	ETHER_MMC_STAT(mmc_tx_vlan_frame_g),

	/* MMC RX counters */
	ETHER_MMC_STAT(mmc_rx_framecount_gb),
	ETHER_MMC_STAT(mmc_rx_octetcount_gb),
	ETHER_MMC_STAT(mmc_rx_octetcount_g),
	ETHER_MMC_STAT(mmc_rx_broadcastframe_g),
	ETHER_MMC_STAT(mmc_rx_multicastframe_g),
	ETHER_MMC_STAT(mmc_rx_crc_error),
	ETHER_MMC_STAT(mmc_rx_align_error),
	ETHER_MMC_STAT(mmc_rx_runt_error),
	ETHER_MMC_STAT(mmc_rx_jabber_error),
	ETHER_MMC_STAT(mmc_rx_undersize_g),
	ETHER_MMC_STAT(mmc_rx_oversize_g),
	ETHER_MMC_STAT(mmc_rx_64_octets_gb),
	ETHER_MMC_STAT(mmc_rx_65_to_127_octets_gb),
	ETHER_MMC_STAT(mmc_rx_128_to_255_octets_gb),
	ETHER_MMC_STAT(mmc_rx_256_to_511_octets_gb),
	ETHER_MMC_STAT(mmc_rx_512_to_1023_octets_gb),
	ETHER_MMC_STAT(mmc_rx_1024_to_max_octets_gb),
	ETHER_MMC_STAT(mmc_rx_unicast_g),
	ETHER_MMC_STAT(mmc_rx_length_error),
	ETHER_MMC_STAT(mmc_rx_outofrangetype),
	ETHER_MMC_STAT(mmc_rx_pause_frames),
	ETHER_MMC_STAT(mmc_rx_fifo_overflow),
	ETHER_MMC_STAT(mmc_rx_vlan_frames_gb),
	ETHER_MMC_STAT(mmc_rx_watchdog_error),
	ETHER_MMC_STAT(mmc_rx_receive_error),
	ETHER_MMC_STAT(mmc_rx_ctrl_frames_g),

	/* LPI */
	ETHER_MMC_STAT(mmc_tx_lpi_usec_cntr),
	ETHER_MMC_STAT(mmc_tx_lpi_tran_cntr),
	ETHER_MMC_STAT(mmc_rx_lpi_usec_cntr),
	ETHER_MMC_STAT(mmc_rx_lpi_tran_cntr),

	/* IPv4 */
	ETHER_MMC_STAT(mmc_rx_ipv4_gd),
	ETHER_MMC_STAT(mmc_rx_ipv4_hderr),
	ETHER_MMC_STAT(mmc_rx_ipv4_nopay),
	ETHER_MMC_STAT(mmc_rx_ipv4_frag),
	ETHER_MMC_STAT(mmc_rx_ipv4_udsbl),

	/* IPV6 */
	ETHER_MMC_STAT(mmc_rx_ipv6_gd_octets),
	ETHER_MMC_STAT(mmc_rx_ipv6_hderr_octets),
	ETHER_MMC_STAT(mmc_rx_ipv6_nopay_octets),

	/* Protocols */
	ETHER_MMC_STAT(mmc_rx_udp_gd),
	ETHER_MMC_STAT(mmc_rx_udp_err),
	ETHER_MMC_STAT(mmc_rx_tcp_gd),
	ETHER_MMC_STAT(mmc_rx_tcp_err),
	ETHER_MMC_STAT(mmc_rx_icmp_gd),
	ETHER_MMC_STAT(mmc_rx_icmp_err),

	/* IPv4 */
	ETHER_MMC_STAT(mmc_rx_ipv4_gd_octets),
	ETHER_MMC_STAT(mmc_rx_ipv4_hderr_octets),
	ETHER_MMC_STAT(mmc_rx_ipv4_nopay_octets),
	ETHER_MMC_STAT(mmc_rx_ipv4_frag_octets),
	ETHER_MMC_STAT(mmc_rx_ipv4_udsbl_octets),

	/* IPV6 */
	ETHER_MMC_STAT(mmc_rx_ipv6_gd),
	ETHER_MMC_STAT(mmc_rx_ipv6_hderr),
	ETHER_MMC_STAT(mmc_rx_ipv6_nopay),

	/* Protocols */
	ETHER_MMC_STAT(mmc_rx_udp_gd_octets),
	ETHER_MMC_STAT(mmc_rx_udp_err_octets),
	ETHER_MMC_STAT(mmc_rx_tcp_gd_octets),
	ETHER_MMC_STAT(mmc_rx_tcp_err_octets),
	ETHER_MMC_STAT(mmc_rx_icmp_gd_octets),
	ETHER_MMC_STAT(mmc_rx_icmp_err_octets),

	/* MGBE stats */
	ETHER_MMC_STAT(mmc_tx_octetcount_gb_h),
	ETHER_MMC_STAT(mmc_tx_framecount_gb_h),
	ETHER_MMC_STAT(mmc_tx_broadcastframe_g_h),
	ETHER_MMC_STAT(mmc_tx_multicastframe_g_h),
	ETHER_MMC_STAT(mmc_tx_64_octets_gb_h),
	ETHER_MMC_STAT(mmc_tx_65_to_127_octets_gb_h),
	ETHER_MMC_STAT(mmc_tx_128_to_255_octets_gb_h),
	ETHER_MMC_STAT(mmc_tx_256_to_511_octets_gb_h),
	ETHER_MMC_STAT(mmc_tx_512_to_1023_octets_gb_h),
	ETHER_MMC_STAT(mmc_tx_1024_to_max_octets_gb_h),
	ETHER_MMC_STAT(mmc_tx_unicast_gb_h),
	ETHER_MMC_STAT(mmc_tx_multicast_gb_h),
	ETHER_MMC_STAT(mmc_tx_broadcast_gb_h),
	ETHER_MMC_STAT(mmc_tx_underflow_error_h),
	ETHER_MMC_STAT(mmc_tx_octetcount_g_h),
	ETHER_MMC_STAT(mmc_tx_framecount_g_h),
	ETHER_MMC_STAT(mmc_tx_pause_frame_h),
	ETHER_MMC_STAT(mmc_tx_vlan_frame_g_h),
	ETHER_MMC_STAT(mmc_rx_framecount_gb_h),
	ETHER_MMC_STAT(mmc_rx_octetcount_gb_h),
	ETHER_MMC_STAT(mmc_rx_octetcount_g_h),
	ETHER_MMC_STAT(mmc_rx_broadcastframe_g_h),
	ETHER_MMC_STAT(mmc_rx_multicastframe_g_h),
	ETHER_MMC_STAT(mmc_rx_crc_error_h),
	ETHER_MMC_STAT(mmc_rx_64_octets_gb_h),
	ETHER_MMC_STAT(mmc_rx_65_to_127_octets_gb_h),
	ETHER_MMC_STAT(mmc_rx_128_to_255_octets_gb_h),
	ETHER_MMC_STAT(mmc_rx_256_to_511_octets_gb_h),
	ETHER_MMC_STAT(mmc_rx_512_to_1023_octets_gb_h),
	ETHER_MMC_STAT(mmc_rx_1024_to_max_octets_gb_h),
	ETHER_MMC_STAT(mmc_rx_unicast_g_h),
	ETHER_MMC_STAT(mmc_rx_length_error_h),
	ETHER_MMC_STAT(mmc_rx_outofrangetype_h),
	ETHER_MMC_STAT(mmc_rx_pause_frames_h),
	ETHER_MMC_STAT(mmc_rx_fifo_overflow_h),
	ETHER_MMC_STAT(mmc_rx_vlan_frames_gb_h),
	ETHER_MMC_STAT(mmc_rx_ipv4_gd_h),
	ETHER_MMC_STAT(mmc_rx_ipv4_hderr_h),
	ETHER_MMC_STAT(mmc_rx_ipv4_nopay_h),
	ETHER_MMC_STAT(mmc_rx_ipv4_frag_h),
	ETHER_MMC_STAT(mmc_rx_ipv4_udsbl_h),
	ETHER_MMC_STAT(mmc_rx_ipv6_gd_octets_h),
	ETHER_MMC_STAT(mmc_rx_ipv6_hderr_octets_h),
	ETHER_MMC_STAT(mmc_rx_ipv6_nopay_octets_h),
	ETHER_MMC_STAT(mmc_rx_udp_gd_h),
	ETHER_MMC_STAT(mmc_rx_udp_err_h),
	ETHER_MMC_STAT(mmc_rx_tcp_gd_h),
	ETHER_MMC_STAT(mmc_rx_tcp_err_h),
	ETHER_MMC_STAT(mmc_rx_icmp_gd_h),
	ETHER_MMC_STAT(mmc_rx_icmp_err_h),
	ETHER_MMC_STAT(mmc_rx_ipv4_gd_octets_h),
	ETHER_MMC_STAT(mmc_rx_ipv4_hderr_octets_h),
	ETHER_MMC_STAT(mmc_rx_ipv4_nopay_octets_h),
	ETHER_MMC_STAT(mmc_rx_ipv4_frag_octets_h),
	ETHER_MMC_STAT(mmc_rx_ipv4_udsbl_octets_h),
	ETHER_MMC_STAT(mmc_rx_ipv6_gd_h),
	ETHER_MMC_STAT(mmc_rx_ipv6_hderr_h),
	ETHER_MMC_STAT(mmc_rx_ipv6_nopay_h),
	ETHER_MMC_STAT(mmc_rx_udp_gd_octets_h),
	ETHER_MMC_STAT(mmc_rx_udp_err_octets_h),
	ETHER_MMC_STAT(mmc_rx_tcp_gd_octets_h),
	ETHER_MMC_STAT(mmc_rx_tcp_err_octets_h),
	ETHER_MMC_STAT(mmc_rx_icmp_gd_octets_h),
	ETHER_MMC_STAT(mmc_rx_icmp_err_octets_h),
	/* FPE */
	ETHER_MMC_STAT(mmc_tx_fpe_frag_cnt),
	ETHER_MMC_STAT(mmc_tx_fpe_hold_req_cnt),
	ETHER_MMC_STAT(mmc_rx_packet_reass_err_cnt),
	ETHER_MMC_STAT(mmc_rx_packet_smd_err_cnt),
	ETHER_MMC_STAT(mmc_rx_packet_asm_ok_cnt),
	ETHER_MMC_STAT(mmc_rx_fpe_fragment_cnt),
#ifdef MACSEC_SUPPORT
	/* MACSEC MMC */
	MACSEC_MMC_STAT(tx_octets_protected),
	MACSEC_MMC_STAT(tx_octets_encrypted),
	MACSEC_MMC_STAT(rx_octets_validated),
	MACSEC_MMC_STAT(rx_octets_decrypted),
	MACSEC_MMC_STAT(rx_pkts_no_tag),
	MACSEC_MMC_STAT(rx_pkts_untagged),
	MACSEC_MMC_STAT(rx_pkts_bad_tag),
	MACSEC_MMC_STAT(rx_pkts_no_sa_err),
	MACSEC_MMC_STAT(rx_pkts_no_sa),
	MACSEC_MMC_STAT(rx_pkts_overrun),
	MACSEC_MMC_STAT(tx_pkts_untaged),
	MACSEC_MMC_STAT(tx_pkts_too_long),
	MACSEC_MMC_STAT(rx_pkts_late[0]),
	MACSEC_MMC_STAT(rx_pkts_late[1]),
	MACSEC_MMC_STAT(rx_pkts_late[2]),
	MACSEC_MMC_STAT(rx_pkts_late[3]),
	MACSEC_MMC_STAT(rx_pkts_late[4]),
	MACSEC_MMC_STAT(rx_pkts_late[5]),
	MACSEC_MMC_STAT(rx_pkts_late[6]),
	MACSEC_MMC_STAT(rx_pkts_late[7]),
	MACSEC_MMC_STAT(rx_pkts_late[8]),
	MACSEC_MMC_STAT(rx_pkts_late[9]),
	MACSEC_MMC_STAT(rx_pkts_late[10]),
	MACSEC_MMC_STAT(rx_pkts_late[11]),
	MACSEC_MMC_STAT(rx_pkts_late[12]),
	MACSEC_MMC_STAT(rx_pkts_late[13]),
	MACSEC_MMC_STAT(rx_pkts_late[14]),
	MACSEC_MMC_STAT(rx_pkts_late[15]),
	MACSEC_MMC_STAT(rx_pkts_late[16]),
	MACSEC_MMC_STAT(rx_pkts_late[17]),
	MACSEC_MMC_STAT(rx_pkts_late[18]),
	MACSEC_MMC_STAT(rx_pkts_late[19]),
	MACSEC_MMC_STAT(rx_pkts_late[20]),
	MACSEC_MMC_STAT(rx_pkts_late[21]),
	MACSEC_MMC_STAT(rx_pkts_late[22]),
	MACSEC_MMC_STAT(rx_pkts_late[23]),
	MACSEC_MMC_STAT(rx_pkts_late[24]),
	MACSEC_MMC_STAT(rx_pkts_late[25]),
	MACSEC_MMC_STAT(rx_pkts_late[26]),
	MACSEC_MMC_STAT(rx_pkts_late[27]),
	MACSEC_MMC_STAT(rx_pkts_late[28]),
	MACSEC_MMC_STAT(rx_pkts_late[29]),
	MACSEC_MMC_STAT(rx_pkts_late[30]),
	MACSEC_MMC_STAT(rx_pkts_late[31]),
	MACSEC_MMC_STAT(rx_pkts_late[32]),
	MACSEC_MMC_STAT(rx_pkts_late[33]),
	MACSEC_MMC_STAT(rx_pkts_late[34]),
	MACSEC_MMC_STAT(rx_pkts_late[35]),
	MACSEC_MMC_STAT(rx_pkts_late[36]),
	MACSEC_MMC_STAT(rx_pkts_late[37]),
	MACSEC_MMC_STAT(rx_pkts_late[38]),
	MACSEC_MMC_STAT(rx_pkts_late[39]),
	MACSEC_MMC_STAT(rx_pkts_late[40]),
	MACSEC_MMC_STAT(rx_pkts_late[41]),
	MACSEC_MMC_STAT(rx_pkts_late[42]),
	MACSEC_MMC_STAT(rx_pkts_late[43]),
	MACSEC_MMC_STAT(rx_pkts_late[44]),
	MACSEC_MMC_STAT(rx_pkts_late[45]),
	MACSEC_MMC_STAT(rx_pkts_late[46]),
	MACSEC_MMC_STAT(rx_pkts_late[47]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[0]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[1]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[2]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[3]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[4]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[5]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[6]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[7]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[8]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[9]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[10]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[11]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[12]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[13]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[14]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[15]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[16]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[17]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[18]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[19]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[20]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[21]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[22]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[23]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[24]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[25]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[26]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[27]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[28]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[29]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[30]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[31]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[32]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[33]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[34]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[35]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[36]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[37]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[38]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[39]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[40]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[41]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[42]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[43]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[44]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[45]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[46]),
	MACSEC_MMC_STAT(rx_pkts_not_valid[47]),
	MACSEC_MMC_STAT(in_pkts_invalid[0]),
	MACSEC_MMC_STAT(in_pkts_invalid[1]),
	MACSEC_MMC_STAT(in_pkts_invalid[2]),
	MACSEC_MMC_STAT(in_pkts_invalid[3]),
	MACSEC_MMC_STAT(in_pkts_invalid[4]),
	MACSEC_MMC_STAT(in_pkts_invalid[5]),
	MACSEC_MMC_STAT(in_pkts_invalid[6]),
	MACSEC_MMC_STAT(in_pkts_invalid[7]),
	MACSEC_MMC_STAT(in_pkts_invalid[8]),
	MACSEC_MMC_STAT(in_pkts_invalid[9]),
	MACSEC_MMC_STAT(in_pkts_invalid[10]),
	MACSEC_MMC_STAT(in_pkts_invalid[11]),
	MACSEC_MMC_STAT(in_pkts_invalid[12]),
	MACSEC_MMC_STAT(in_pkts_invalid[13]),
	MACSEC_MMC_STAT(in_pkts_invalid[14]),
	MACSEC_MMC_STAT(in_pkts_invalid[15]),
	MACSEC_MMC_STAT(in_pkts_invalid[16]),
	MACSEC_MMC_STAT(in_pkts_invalid[17]),
	MACSEC_MMC_STAT(in_pkts_invalid[18]),
	MACSEC_MMC_STAT(in_pkts_invalid[19]),
	MACSEC_MMC_STAT(in_pkts_invalid[20]),
	MACSEC_MMC_STAT(in_pkts_invalid[21]),
	MACSEC_MMC_STAT(in_pkts_invalid[22]),
	MACSEC_MMC_STAT(in_pkts_invalid[23]),
	MACSEC_MMC_STAT(in_pkts_invalid[24]),
	MACSEC_MMC_STAT(in_pkts_invalid[25]),
	MACSEC_MMC_STAT(in_pkts_invalid[26]),
	MACSEC_MMC_STAT(in_pkts_invalid[27]),
	MACSEC_MMC_STAT(in_pkts_invalid[28]),
	MACSEC_MMC_STAT(in_pkts_invalid[29]),
	MACSEC_MMC_STAT(in_pkts_invalid[30]),
	MACSEC_MMC_STAT(in_pkts_invalid[31]),
	MACSEC_MMC_STAT(in_pkts_invalid[32]),
	MACSEC_MMC_STAT(in_pkts_invalid[33]),
	MACSEC_MMC_STAT(in_pkts_invalid[34]),
	MACSEC_MMC_STAT(in_pkts_invalid[35]),
	MACSEC_MMC_STAT(in_pkts_invalid[36]),
	MACSEC_MMC_STAT(in_pkts_invalid[37]),
	MACSEC_MMC_STAT(in_pkts_invalid[38]),
	MACSEC_MMC_STAT(in_pkts_invalid[39]),
	MACSEC_MMC_STAT(in_pkts_invalid[40]),
	MACSEC_MMC_STAT(in_pkts_invalid[41]),
	MACSEC_MMC_STAT(in_pkts_invalid[42]),
	MACSEC_MMC_STAT(in_pkts_invalid[43]),
	MACSEC_MMC_STAT(in_pkts_invalid[44]),
	MACSEC_MMC_STAT(in_pkts_invalid[45]),
	MACSEC_MMC_STAT(in_pkts_invalid[46]),
	MACSEC_MMC_STAT(in_pkts_invalid[47]),
	MACSEC_MMC_STAT(rx_pkts_delayed[0]),
	MACSEC_MMC_STAT(rx_pkts_delayed[1]),
	MACSEC_MMC_STAT(rx_pkts_delayed[2]),
	MACSEC_MMC_STAT(rx_pkts_delayed[3]),
	MACSEC_MMC_STAT(rx_pkts_delayed[4]),
	MACSEC_MMC_STAT(rx_pkts_delayed[5]),
	MACSEC_MMC_STAT(rx_pkts_delayed[6]),
	MACSEC_MMC_STAT(rx_pkts_delayed[7]),
	MACSEC_MMC_STAT(rx_pkts_delayed[8]),
	MACSEC_MMC_STAT(rx_pkts_delayed[9]),
	MACSEC_MMC_STAT(rx_pkts_delayed[10]),
	MACSEC_MMC_STAT(rx_pkts_delayed[11]),
	MACSEC_MMC_STAT(rx_pkts_delayed[12]),
	MACSEC_MMC_STAT(rx_pkts_delayed[13]),
	MACSEC_MMC_STAT(rx_pkts_delayed[14]),
	MACSEC_MMC_STAT(rx_pkts_delayed[15]),
	MACSEC_MMC_STAT(rx_pkts_delayed[16]),
	MACSEC_MMC_STAT(rx_pkts_delayed[17]),
	MACSEC_MMC_STAT(rx_pkts_delayed[18]),
	MACSEC_MMC_STAT(rx_pkts_delayed[19]),
	MACSEC_MMC_STAT(rx_pkts_delayed[20]),
	MACSEC_MMC_STAT(rx_pkts_delayed[21]),
	MACSEC_MMC_STAT(rx_pkts_delayed[22]),
	MACSEC_MMC_STAT(rx_pkts_delayed[23]),
	MACSEC_MMC_STAT(rx_pkts_delayed[24]),
	MACSEC_MMC_STAT(rx_pkts_delayed[25]),
	MACSEC_MMC_STAT(rx_pkts_delayed[26]),
	MACSEC_MMC_STAT(rx_pkts_delayed[27]),
	MACSEC_MMC_STAT(rx_pkts_delayed[28]),
	MACSEC_MMC_STAT(rx_pkts_delayed[29]),
	MACSEC_MMC_STAT(rx_pkts_delayed[30]),
	MACSEC_MMC_STAT(rx_pkts_delayed[31]),
	MACSEC_MMC_STAT(rx_pkts_delayed[32]),
	MACSEC_MMC_STAT(rx_pkts_delayed[33]),
	MACSEC_MMC_STAT(rx_pkts_delayed[34]),
	MACSEC_MMC_STAT(rx_pkts_delayed[35]),
	MACSEC_MMC_STAT(rx_pkts_delayed[36]),
	MACSEC_MMC_STAT(rx_pkts_delayed[37]),
	MACSEC_MMC_STAT(rx_pkts_delayed[38]),
	MACSEC_MMC_STAT(rx_pkts_delayed[39]),
	MACSEC_MMC_STAT(rx_pkts_delayed[40]),
	MACSEC_MMC_STAT(rx_pkts_delayed[41]),
	MACSEC_MMC_STAT(rx_pkts_delayed[42]),
	MACSEC_MMC_STAT(rx_pkts_delayed[43]),
	MACSEC_MMC_STAT(rx_pkts_delayed[44]),
	MACSEC_MMC_STAT(rx_pkts_delayed[45]),
	MACSEC_MMC_STAT(rx_pkts_delayed[46]),
	MACSEC_MMC_STAT(rx_pkts_delayed[47]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[0]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[1]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[2]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[3]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[4]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[5]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[6]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[7]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[8]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[9]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[10]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[11]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[12]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[13]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[14]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[15]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[16]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[17]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[18]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[19]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[20]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[21]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[22]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[23]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[24]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[25]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[26]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[27]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[28]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[29]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[30]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[31]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[32]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[33]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[34]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[35]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[36]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[37]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[38]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[39]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[40]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[41]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[42]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[43]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[44]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[45]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[46]),
	MACSEC_MMC_STAT(rx_pkts_unchecked[47]),
	MACSEC_MMC_STAT(rx_pkts_ok[0]),
	MACSEC_MMC_STAT(rx_pkts_ok[1]),
	MACSEC_MMC_STAT(rx_pkts_ok[2]),
	MACSEC_MMC_STAT(rx_pkts_ok[3]),
	MACSEC_MMC_STAT(rx_pkts_ok[4]),
	MACSEC_MMC_STAT(rx_pkts_ok[5]),
	MACSEC_MMC_STAT(rx_pkts_ok[6]),
	MACSEC_MMC_STAT(rx_pkts_ok[7]),
	MACSEC_MMC_STAT(rx_pkts_ok[8]),
	MACSEC_MMC_STAT(rx_pkts_ok[9]),
	MACSEC_MMC_STAT(rx_pkts_ok[10]),
	MACSEC_MMC_STAT(rx_pkts_ok[11]),
	MACSEC_MMC_STAT(rx_pkts_ok[12]),
	MACSEC_MMC_STAT(rx_pkts_ok[13]),
	MACSEC_MMC_STAT(rx_pkts_ok[14]),
	MACSEC_MMC_STAT(rx_pkts_ok[15]),
	MACSEC_MMC_STAT(rx_pkts_ok[16]),
	MACSEC_MMC_STAT(rx_pkts_ok[17]),
	MACSEC_MMC_STAT(rx_pkts_ok[18]),
	MACSEC_MMC_STAT(rx_pkts_ok[19]),
	MACSEC_MMC_STAT(rx_pkts_ok[20]),
	MACSEC_MMC_STAT(rx_pkts_ok[21]),
	MACSEC_MMC_STAT(rx_pkts_ok[22]),
	MACSEC_MMC_STAT(rx_pkts_ok[23]),
	MACSEC_MMC_STAT(rx_pkts_ok[24]),
	MACSEC_MMC_STAT(rx_pkts_ok[25]),
	MACSEC_MMC_STAT(rx_pkts_ok[26]),
	MACSEC_MMC_STAT(rx_pkts_ok[27]),
	MACSEC_MMC_STAT(rx_pkts_ok[28]),
	MACSEC_MMC_STAT(rx_pkts_ok[29]),
	MACSEC_MMC_STAT(rx_pkts_ok[30]),
	MACSEC_MMC_STAT(rx_pkts_ok[31]),
	MACSEC_MMC_STAT(rx_pkts_ok[32]),
	MACSEC_MMC_STAT(rx_pkts_ok[33]),
	MACSEC_MMC_STAT(rx_pkts_ok[34]),
	MACSEC_MMC_STAT(rx_pkts_ok[35]),
	MACSEC_MMC_STAT(rx_pkts_ok[36]),
	MACSEC_MMC_STAT(rx_pkts_ok[37]),
	MACSEC_MMC_STAT(rx_pkts_ok[38]),
	MACSEC_MMC_STAT(rx_pkts_ok[39]),
	MACSEC_MMC_STAT(rx_pkts_ok[40]),
	MACSEC_MMC_STAT(rx_pkts_ok[41]),
	MACSEC_MMC_STAT(rx_pkts_ok[42]),
	MACSEC_MMC_STAT(rx_pkts_ok[43]),
	MACSEC_MMC_STAT(rx_pkts_ok[44]),
	MACSEC_MMC_STAT(rx_pkts_ok[45]),
	MACSEC_MMC_STAT(rx_pkts_ok[46]),
	MACSEC_MMC_STAT(rx_pkts_ok[47]),
	MACSEC_MMC_STAT(tx_pkts_protected[0]),
	MACSEC_MMC_STAT(tx_pkts_protected[1]),
	MACSEC_MMC_STAT(tx_pkts_protected[2]),
	MACSEC_MMC_STAT(tx_pkts_protected[3]),
	MACSEC_MMC_STAT(tx_pkts_protected[4]),
	MACSEC_MMC_STAT(tx_pkts_protected[5]),
	MACSEC_MMC_STAT(tx_pkts_protected[6]),
	MACSEC_MMC_STAT(tx_pkts_protected[7]),
	MACSEC_MMC_STAT(tx_pkts_protected[8]),
	MACSEC_MMC_STAT(tx_pkts_protected[9]),
	MACSEC_MMC_STAT(tx_pkts_protected[10]),
	MACSEC_MMC_STAT(tx_pkts_protected[11]),
	MACSEC_MMC_STAT(tx_pkts_protected[12]),
	MACSEC_MMC_STAT(tx_pkts_protected[13]),
	MACSEC_MMC_STAT(tx_pkts_protected[14]),
	MACSEC_MMC_STAT(tx_pkts_protected[15]),
	MACSEC_MMC_STAT(tx_pkts_protected[16]),
	MACSEC_MMC_STAT(tx_pkts_protected[17]),
	MACSEC_MMC_STAT(tx_pkts_protected[18]),
	MACSEC_MMC_STAT(tx_pkts_protected[19]),
	MACSEC_MMC_STAT(tx_pkts_protected[20]),
	MACSEC_MMC_STAT(tx_pkts_protected[21]),
	MACSEC_MMC_STAT(tx_pkts_protected[22]),
	MACSEC_MMC_STAT(tx_pkts_protected[23]),
	MACSEC_MMC_STAT(tx_pkts_protected[24]),
	MACSEC_MMC_STAT(tx_pkts_protected[25]),
	MACSEC_MMC_STAT(tx_pkts_protected[26]),
	MACSEC_MMC_STAT(tx_pkts_protected[27]),
	MACSEC_MMC_STAT(tx_pkts_protected[28]),
	MACSEC_MMC_STAT(tx_pkts_protected[29]),
	MACSEC_MMC_STAT(tx_pkts_protected[30]),
	MACSEC_MMC_STAT(tx_pkts_protected[31]),
	MACSEC_MMC_STAT(tx_pkts_protected[32]),
	MACSEC_MMC_STAT(tx_pkts_protected[33]),
	MACSEC_MMC_STAT(tx_pkts_protected[34]),
	MACSEC_MMC_STAT(tx_pkts_protected[35]),
	MACSEC_MMC_STAT(tx_pkts_protected[36]),
	MACSEC_MMC_STAT(tx_pkts_protected[37]),
	MACSEC_MMC_STAT(tx_pkts_protected[38]),
	MACSEC_MMC_STAT(tx_pkts_protected[39]),
	MACSEC_MMC_STAT(tx_pkts_protected[40]),
	MACSEC_MMC_STAT(tx_pkts_protected[41]),
	MACSEC_MMC_STAT(tx_pkts_protected[42]),
	MACSEC_MMC_STAT(tx_pkts_protected[43]),
	MACSEC_MMC_STAT(tx_pkts_protected[44]),
	MACSEC_MMC_STAT(tx_pkts_protected[45]),
	MACSEC_MMC_STAT(tx_pkts_protected[46]),
	MACSEC_MMC_STAT(tx_pkts_protected[47]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[0]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[1]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[2]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[3]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[4]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[5]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[6]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[7]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[8]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[9]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[10]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[11]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[12]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[13]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[14]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[15]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[16]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[17]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[18]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[19]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[20]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[21]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[22]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[23]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[24]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[25]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[26]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[27]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[28]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[29]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[30]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[31]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[32]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[33]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[34]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[35]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[36]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[37]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[38]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[39]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[40]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[41]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[42]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[43]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[44]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[45]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[46]),
	MACSEC_MMC_STAT(tx_pkts_encrypted[47]),
	/* MACSEC IRQ */
	MACSEC_IRQ_STAT(tx_mtu_check_fail),
	MACSEC_IRQ_STAT(tx_mac_crc_error),
	MACSEC_IRQ_STAT(tx_sc_an_not_valid),
	MACSEC_IRQ_STAT(tx_aes_gcm_buf_ovf),
	MACSEC_IRQ_STAT(tx_lkup_miss),
	MACSEC_IRQ_STAT(tx_uninit_key_slot),
	MACSEC_IRQ_STAT(tx_pn_threshold),
	MACSEC_IRQ_STAT(tx_pn_exhausted),
	MACSEC_IRQ_STAT(rx_icv_err_threshold),
	MACSEC_IRQ_STAT(rx_replay_error),
	MACSEC_IRQ_STAT(rx_mtu_check_fail),
	MACSEC_IRQ_STAT(rx_mac_crc_error),
	MACSEC_IRQ_STAT(rx_aes_gcm_buf_ovf),
	MACSEC_IRQ_STAT(rx_lkup_miss),
	MACSEC_IRQ_STAT(rx_uninit_key_slot),
	MACSEC_IRQ_STAT(rx_pn_exhausted),
	MACSEC_IRQ_STAT(secure_reg_viol),
	MACSEC_IRQ_STAT(rx_dbg_capture_done),
	MACSEC_IRQ_STAT(tx_dbg_capture_done),
#endif /* MACSEC_SUPPORT */

};

/**
 * @brief Ethernet extra statistics array length
 */
#define ETHER_CORE_STAT_LEN OSI_ARRAY_SIZE(ether_tstrings_stats)

/**
 * @brief Name of extra Ethernet stats, with length of name not more than
 * ETH_GSTRING_LEN MAC
 */
#define ETHER_CORE_STATS(r) \
{ (#r), sizeof_field(struct osi_stats, r), \
	offsetof(struct osi_core_priv_data, stats.r)}

/**
 * @brief Ethernet extra statistics
 */
static const struct ether_stats ether_tstrings_stats[] = {
	ETHER_CORE_STATS(const_gate_ctr_err),
	ETHER_CORE_STATS(head_of_line_blk_sch),
	ETHER_CORE_STATS(hlbs_q[0]),
	ETHER_CORE_STATS(hlbs_q[1]),
	ETHER_CORE_STATS(hlbs_q[2]),
	ETHER_CORE_STATS(hlbs_q[3]),
	ETHER_CORE_STATS(hlbs_q[4]),
	ETHER_CORE_STATS(hlbs_q[5]),
	ETHER_CORE_STATS(hlbs_q[6]),
	ETHER_CORE_STATS(hlbs_q[7]),
	ETHER_CORE_STATS(head_of_line_blk_frm),
	ETHER_CORE_STATS(hlbf_q[0]),
	ETHER_CORE_STATS(hlbf_q[1]),
	ETHER_CORE_STATS(hlbf_q[2]),
	ETHER_CORE_STATS(hlbf_q[3]),
	ETHER_CORE_STATS(hlbf_q[4]),
	ETHER_CORE_STATS(hlbf_q[5]),
	ETHER_CORE_STATS(hlbf_q[6]),
	ETHER_CORE_STATS(hlbf_q[7]),
	ETHER_CORE_STATS(base_time_reg_err),
	ETHER_CORE_STATS(sw_own_list_complete),

#ifndef OSI_STRIPPED_LIB
	/* Tx/Rx IRQ error info */
	ETHER_CORE_STATS(tx_proc_stopped_irq_n[0]),
	ETHER_CORE_STATS(tx_proc_stopped_irq_n[1]),
	ETHER_CORE_STATS(tx_proc_stopped_irq_n[2]),
	ETHER_CORE_STATS(tx_proc_stopped_irq_n[3]),
	ETHER_CORE_STATS(tx_proc_stopped_irq_n[4]),
	ETHER_CORE_STATS(tx_proc_stopped_irq_n[5]),
	ETHER_CORE_STATS(tx_proc_stopped_irq_n[6]),
	ETHER_CORE_STATS(tx_proc_stopped_irq_n[7]),
	ETHER_CORE_STATS(tx_proc_stopped_irq_n[8]),
	ETHER_CORE_STATS(tx_proc_stopped_irq_n[9]),
	ETHER_CORE_STATS(rx_proc_stopped_irq_n[0]),
	ETHER_CORE_STATS(rx_proc_stopped_irq_n[1]),
	ETHER_CORE_STATS(rx_proc_stopped_irq_n[2]),
	ETHER_CORE_STATS(rx_proc_stopped_irq_n[3]),
	ETHER_CORE_STATS(rx_proc_stopped_irq_n[4]),
	ETHER_CORE_STATS(rx_proc_stopped_irq_n[5]),
	ETHER_CORE_STATS(rx_proc_stopped_irq_n[6]),
	ETHER_CORE_STATS(rx_proc_stopped_irq_n[7]),
	ETHER_CORE_STATS(rx_proc_stopped_irq_n[8]),
	ETHER_CORE_STATS(rx_proc_stopped_irq_n[9]),
	ETHER_CORE_STATS(tx_buf_unavail_irq_n[0]),
	ETHER_CORE_STATS(tx_buf_unavail_irq_n[1]),
	ETHER_CORE_STATS(tx_buf_unavail_irq_n[2]),
	ETHER_CORE_STATS(tx_buf_unavail_irq_n[3]),
	ETHER_CORE_STATS(tx_buf_unavail_irq_n[4]),
	ETHER_CORE_STATS(tx_buf_unavail_irq_n[5]),
	ETHER_CORE_STATS(tx_buf_unavail_irq_n[6]),
	ETHER_CORE_STATS(tx_buf_unavail_irq_n[7]),
	ETHER_CORE_STATS(tx_buf_unavail_irq_n[8]),
	ETHER_CORE_STATS(tx_buf_unavail_irq_n[9]),
	ETHER_CORE_STATS(rx_buf_unavail_irq_n[0]),
	ETHER_CORE_STATS(rx_buf_unavail_irq_n[1]),
	ETHER_CORE_STATS(rx_buf_unavail_irq_n[2]),
	ETHER_CORE_STATS(rx_buf_unavail_irq_n[3]),
	ETHER_CORE_STATS(rx_buf_unavail_irq_n[4]),
	ETHER_CORE_STATS(rx_buf_unavail_irq_n[5]),
	ETHER_CORE_STATS(rx_buf_unavail_irq_n[6]),
	ETHER_CORE_STATS(rx_buf_unavail_irq_n[7]),
	ETHER_CORE_STATS(rx_buf_unavail_irq_n[8]),
	ETHER_CORE_STATS(rx_buf_unavail_irq_n[9]),
	ETHER_CORE_STATS(rx_watchdog_irq_n),
	ETHER_CORE_STATS(fatal_bus_error_irq_n),
	ETHER_CORE_STATS(ts_lock_add_fail),
	ETHER_CORE_STATS(ts_lock_del_fail),

	/* Packet error stats */
	ETHER_CORE_STATS(mgbe_ip_header_err),
	ETHER_CORE_STATS(mgbe_jabber_timeout_err),
	ETHER_CORE_STATS(mgbe_payload_cs_err),
	ETHER_CORE_STATS(mgbe_tx_underflow_err),
#endif /* OSI_STRIPPED_LIB */
};

static u64 counter_helper(u64 sizeof_stat, char *p)
{
	u64 ret = 0;

	if (sizeof_stat == sizeof(u64)) {
		u64 temp = 0;
		(void)memcpy(&temp, (void *)p, sizeof(temp));
		ret = temp;
	} else {
		u32 temp = 0;
		(void)memcpy(&temp, (void *)p, sizeof(temp));
		ret = temp;
	}

	return ret;
}

void ether_get_ethtool_stats(struct net_device *dev,
			     struct ethtool_stats *dummy,
			     u64 *data)
{
	struct ether_priv_data *pdata = netdev_priv(dev);
	struct osi_core_priv_data *osi_core = pdata->osi_core;
#ifndef OSI_STRIPPED_LIB
	struct osi_dma_priv_data *osi_dma = pdata->osi_dma;
#endif /* OSI_STRIPPED_LIB */
	struct osi_ioctl ioctl_data = {};
	int i, j = 0;
	int ret;

	if (!netif_running(dev)) {
		netdev_err(pdata->ndev, "%s: iface not up\n", __func__);
		return;
	}

	if (pdata->hw_feat.mmc_sel == 1U) {
		ioctl_data.cmd = OSI_CMD_READ_MMC;
		ret = osi_handle_ioctl(osi_core, &ioctl_data);
		if (ret == -1) {
			dev_err(pdata->dev, "Error in reading MMC counter\n");
			return;
		}

		if (osi_core->use_virtualization == OSI_ENABLE) {
			ioctl_data.cmd = OSI_CMD_READ_STATS;
			ret = osi_handle_ioctl(osi_core, &ioctl_data);
			if (ret == -1) {
				dev_err(pdata->dev,
					"Fail to read core stats\n");
				return;
			}
		}

#ifdef MACSEC_SUPPORT
		if (pdata->macsec_pdata) {
			ret = osi_macsec_read_mmc(osi_core);
			if (ret == -1) {
				dev_err(pdata->dev,
					"Fail to read macsec stats\n");
				return;
			}
		}
#endif /* MACSEC_SUPPORT */

		for (i = 0; i < ETHER_MMC_STATS_LEN; i++) {
			char *p = (char *)osi_core + ether_mmc[i].stat_offset;
			if (j < OSD_INT_MAX) {
				data[j++] = counter_helper(ether_mmc[i].sizeof_stat, p);
			}
		}

		for (i = 0; i < ETHER_EXTRA_STAT_LEN; i++) {
			char *p = (char *)pdata +
				  ether_gstrings_stats[i].stat_offset;

			if (j < OSD_INT_MAX) {
				data[j++] = counter_helper(ether_gstrings_stats[i].sizeof_stat, p);
			}
		}

#ifndef OSI_STRIPPED_LIB
		for (i = 0; i < ETHER_EXTRA_DMA_STAT_LEN; i++) {
			char *p = (char *)osi_dma +
				  ether_dstrings_stats[i].stat_offset;

			if (j < OSD_INT_MAX) {
				data[j++] = counter_helper(ether_dstrings_stats[i].sizeof_stat, p);
			}
		}

		for (i = 0; i < ETHER_PKT_ERR_STAT_LEN; i++) {
			char *p = (char *)osi_dma +
				  ether_cstrings_stats[i].stat_offset;

			if (j < OSD_INT_MAX) {
				data[j++] = counter_helper(ether_cstrings_stats[i].sizeof_stat, p);
			}
		}

		for (i = 0; ((i < ETHER_FRP_STAT_LEN) &&
			     (pdata->hw_feat.frp_sel == OSI_ENABLE)); i++) {
			char *p = (char *)osi_dma +
				  ether_frpstrings_stats[i].stat_offset;

			if (j < OSD_INT_MAX) {
				data[j++] = counter_helper(ether_frpstrings_stats[i].sizeof_stat, p);
			}
		}
#endif /* OSI_STRIPPED_LIB */

		for (i = 0; i < ETHER_CORE_STAT_LEN; i++) {
			char *p = (char *)osi_core +
				  ether_tstrings_stats[i].stat_offset;

			if (j < OSD_INT_MAX) {
				data[j++] = counter_helper(ether_tstrings_stats[i].sizeof_stat, p);
			}
		}
	}
}

int ether_get_sset_count(struct net_device *dev, int sset)
{
	struct ether_priv_data *pdata = netdev_priv(dev);
	int len = 0;

	if (sset == ETH_SS_STATS) {
		if (pdata->hw_feat.mmc_sel == OSI_ENABLE) {
			if (OSD_INT_MAX < ETHER_MMC_STATS_LEN) {
				/* do nothing*/
			} else {
				len = ETHER_MMC_STATS_LEN;
			}
		}
		if (OSD_INT_MAX - ETHER_EXTRA_STAT_LEN < len) {
			/* do nothing */
		} else {
			len += ETHER_EXTRA_STAT_LEN;
		}
#ifndef OSI_STRIPPED_LIB
		if (OSD_INT_MAX - ETHER_EXTRA_DMA_STAT_LEN < len) {
			/* do nothing */
		} else {
			len += ETHER_EXTRA_DMA_STAT_LEN;
		}
		if (OSD_INT_MAX - ETHER_PKT_ERR_STAT_LEN < len) {
			/* do nothing */
		} else {
			len += ETHER_PKT_ERR_STAT_LEN;
		}
		if (OSD_INT_MAX - ETHER_FRP_STAT_LEN < len) {
			/* do nothing */
		} else {
			if (pdata->hw_feat.frp_sel == OSI_ENABLE) {
				len += ETHER_FRP_STAT_LEN;
			}
		}
#endif /* OSI_STRIPPED_LIB */
		if (OSD_INT_MAX - ETHER_CORE_STAT_LEN < len) {
			/* do nothing */
		} else {
			len += ETHER_CORE_STAT_LEN;
		}
	} else if (sset == ETH_SS_TEST) {
		len = ether_selftest_get_count(pdata);
	} else {
		len = -EOPNOTSUPP;
	}

	return len;
}

void ether_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	struct ether_priv_data *pdata = netdev_priv(dev);
	u8 *p = data;
	u8 *str;
	int i;

	if (stringset == (u32)ETH_SS_STATS) {
		if (pdata->hw_feat.mmc_sel == OSI_ENABLE) {
			for (i = 0; i < ETHER_MMC_STATS_LEN; i++) {
				str = (u8 *)ether_mmc[i].stat_string;
				if (memcpy(p, str, ETH_GSTRING_LEN) ==
				    OSI_NULL) {
					return;
				}
				p += ETH_GSTRING_LEN;
			}

			for (i = 0; i < ETHER_EXTRA_STAT_LEN; i++) {
				str = (u8 *)ether_gstrings_stats[i].stat_string;
				if (memcpy(p, str, ETH_GSTRING_LEN) ==
				    OSI_NULL) {
					return;
				}
				p += ETH_GSTRING_LEN;
			}
#ifndef OSI_STRIPPED_LIB
			for (i = 0; i < ETHER_EXTRA_DMA_STAT_LEN; i++) {
				str = (u8 *)ether_dstrings_stats[i].stat_string;
				if (memcpy(p, str, ETH_GSTRING_LEN) ==
				    OSI_NULL) {
					return;
				}
				p += ETH_GSTRING_LEN;
			}
			for (i = 0; i < ETHER_PKT_ERR_STAT_LEN; i++) {
				str = (u8 *)ether_cstrings_stats[i].stat_string;
				if (memcpy(p, str, ETH_GSTRING_LEN) ==
				    OSI_NULL) {
					return;
				}
				p += ETH_GSTRING_LEN;
			}
			for (i = 0; ((i < ETHER_FRP_STAT_LEN) &&
				     (pdata->hw_feat.frp_sel == OSI_ENABLE));
			     i++) {
				str = (u8 *)
				       ether_frpstrings_stats[i].stat_string;
				if (memcpy(p, str, ETH_GSTRING_LEN) ==
				    OSI_NULL) {
					return;
				}
				p += ETH_GSTRING_LEN;
			}
#endif /* OSI_STRIPPED_LIB */
			for (i = 0; i < ETHER_CORE_STAT_LEN; i++) {
				str = (u8 *)ether_tstrings_stats[i].stat_string;
				if (memcpy(p, str, ETH_GSTRING_LEN) ==
				    OSI_NULL) {
					return;
				}
				p += ETH_GSTRING_LEN;
			}
		}
	} else if (stringset == (u32)ETH_SS_TEST) {
		ether_selftest_get_strings(pdata, p);
	} else {
		dev_err(pdata->dev, "%s() Unsupported stringset\n", __func__);
	}
}

#ifndef OSI_STRIPPED_LIB
void ether_get_pauseparam(struct net_device *ndev,
			  struct ethtool_pauseparam *pause)
{
	struct ether_priv_data *pdata = netdev_priv(ndev);
	struct phy_device *phydev = pdata->phydev;

	if (!netif_running(ndev)) {
		netdev_err(pdata->ndev, "interface must be up\n");
		return;
	}

	/* return if pause frame is not supported */
	if ((pdata->osi_core->pause_frames == OSI_PAUSE_FRAMES_DISABLE) ||
	    (!linkmode_test_bit(ETHTOOL_LINK_MODE_Pause_BIT, phydev->supported) ||
	    !linkmode_test_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT, phydev->supported))) {
		dev_err(pdata->dev, "FLOW control not supported\n");
		return;
	}

	/* update auto negotiation */
	pause->autoneg = phydev->autoneg;

	/* update rx pause parameter */
	if ((pdata->osi_core->flow_ctrl & OSI_FLOW_CTRL_RX) ==
	    OSI_FLOW_CTRL_RX) {
		pause->rx_pause = 1;
	}

	/* update tx pause parameter */
	if ((pdata->osi_core->flow_ctrl & OSI_FLOW_CTRL_TX) ==
	    OSI_FLOW_CTRL_TX) {
		pause->tx_pause = 1;
	}
}

int ether_set_pauseparam(struct net_device *ndev,
			 struct ethtool_pauseparam *pause)
{
	struct ether_priv_data *pdata = netdev_priv(ndev);
	struct osi_ioctl ioctl_data = {};
	struct phy_device *phydev = pdata->phydev;
	int curflow_ctrl = OSI_FLOW_CTRL_DISABLE;
	int ret;

	if (!netif_running(ndev)) {
		netdev_err(pdata->ndev, "interface must be up\n");
		return -EINVAL;
	}

	/* return if pause frame is not supported */
	if ((pdata->osi_core->pause_frames == OSI_PAUSE_FRAMES_DISABLE) ||
	    (!linkmode_test_bit(ETHTOOL_LINK_MODE_Pause_BIT, phydev->supported) ||
	    !linkmode_test_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT, phydev->supported))) {
		dev_err(pdata->dev, "FLOW control not supported\n");
		return -EOPNOTSUPP;
	}

	dev_err(pdata->dev, "autoneg = %d tx_pause = %d rx_pause = %d\n",
		pause->autoneg, pause->tx_pause, pause->rx_pause);

	if (pause->tx_pause)
		curflow_ctrl |= OSI_FLOW_CTRL_TX;

	if (pause->rx_pause)
		curflow_ctrl |= OSI_FLOW_CTRL_RX;

	/* update flow control setting */
	pdata->osi_core->flow_ctrl = curflow_ctrl;
	/* update autonegotiation */
	phydev->autoneg = pause->autoneg;

	/*If autonegotiation is enabled,start auto-negotiation
	 * for this PHY device and return, so that flow control
	 * settings will be done once we receive the link changed
	 * event i.e in ether_adjust_link
	 */
	if (phydev->autoneg && netif_running(ndev)) {
		return phy_start_aneg(phydev);
	}

	/* Configure current flow control settings */
	ioctl_data.cmd = OSI_CMD_FLOW_CTRL;
	ioctl_data.arg1_u32 = pdata->osi_core->flow_ctrl;
	ret = osi_handle_ioctl(pdata->osi_core, &ioctl_data);
	if (ret < 0) {
		dev_err(pdata->dev, "Setting flow control failed\n");
		return -EFAULT;
	}

	return ret;
}
#endif /* OSI_STRIPPED_LIB */

/**
 * @brief Get HW supported time stamping.
 *
 * Algorithm: Function used to query the PTP capabilities for given netdev.
 *
 * @param[in] ndev: Net device data.
 * @param[in] info: Holds device supported timestamping types
 *
 * @note HW need to support PTP functionality.
 *
 * @return zero on success
 */
static int ether_get_ts_info(struct net_device *ndev,
#if defined(NV_ETHTOOL_KERNEL_ETHTOOL_TS_INFO_STRUCT_PRESENT) /* Linux v6.11 */
		struct kernel_ethtool_ts_info *info)
#else
		struct ethtool_ts_info *info)
#endif
{
	struct ether_priv_data *pdata = netdev_priv(ndev);

	info->so_timestamping = SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_TX_SOFTWARE |
				SOF_TIMESTAMPING_RX_SOFTWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE |
				SOF_TIMESTAMPING_SOFTWARE;

	if (pdata->ptp_clock) {
		info->phc_index = ptp_clock_index(pdata->ptp_clock);
	}

	info->tx_types = ((1 << HWTSTAMP_TX_OFF) |
			  (1 << HWTSTAMP_TX_ON) |
			  (1 << HWTSTAMP_TX_ONESTEP_SYNC));

	info->rx_filters |= ((1 << HWTSTAMP_FILTER_PTP_V1_L4_SYNC) |
			     (1 << HWTSTAMP_FILTER_PTP_V2_L2_SYNC) |
			     (1 << HWTSTAMP_FILTER_PTP_V2_L4_SYNC) |
			     (1 << HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ) |
			     (1 << HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ) |
			     (1 << HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ) |
			     (1 << HWTSTAMP_FILTER_PTP_V2_EVENT) |
			     (1 << HWTSTAMP_FILTER_NONE));

	return 0;
}

/**
 * @brief Set interrupt coalescing parameters.
 *
 * Algorithm: This function is invoked by kernel when user request to set
 * interrupt coalescing parameters. This driver maintains same coalescing
 * parameters for all the channels, hence same changes will be applied to
 * all the channels.
 *
 * @param[in] dev: Net device data.
 * @param[in] ec: pointer to ethtool_coalesce structure
 *
 * @note Interface need to be bring down for setting these parameters
 *
 * @retval 0 on Sucess
 * @retval "negative value" on failure.
 */
#if defined(NV_ETHTOOL_OPS_GET_SET_COALESCE_HAS_COAL_AND_EXTACT_ARGS) /* Linux v5.15 */
static int ether_set_coalesce(struct net_device *dev,
			      struct ethtool_coalesce *ec,
			      struct kernel_ethtool_coalesce *kernel_coal,
			      struct netlink_ext_ack *extack)
#else
static int ether_set_coalesce(struct net_device *dev,
			      struct ethtool_coalesce *ec)
#endif
{
	struct ether_priv_data *pdata = netdev_priv(dev);
	struct osi_dma_priv_data *osi_dma = pdata->osi_dma;

	if (netif_running(dev)) {
		netdev_err(dev, "Coalesce parameters can be changed"
			   " only if interface is down\n");
		return -EINVAL;
	}

	/* Check for not supported parameters  */
	if ((ec->rx_coalesce_usecs_irq) ||
	    (ec->rx_max_coalesced_frames_irq) || (ec->tx_coalesce_usecs_irq) ||
	    (ec->use_adaptive_rx_coalesce) || (ec->use_adaptive_tx_coalesce) ||
	    (ec->pkt_rate_low) || (ec->rx_coalesce_usecs_low) ||
	    (ec->rx_max_coalesced_frames_low) || (ec->tx_coalesce_usecs_high) ||
	    (ec->tx_max_coalesced_frames_low) || (ec->pkt_rate_high) ||
	    (ec->tx_coalesce_usecs_low) || (ec->rx_coalesce_usecs_high) ||
	    (ec->rx_max_coalesced_frames_high) ||
	    (ec->tx_max_coalesced_frames_irq)  ||
	    (ec->stats_block_coalesce_usecs)   ||
	    (ec->tx_max_coalesced_frames_high) || (ec->rate_sample_interval)) {
		return -EOPNOTSUPP;
	}

	if (ec->tx_max_coalesced_frames == OSI_DISABLE) {
		osi_dma->use_tx_frames = OSI_DISABLE;
	} else if ((ec->tx_max_coalesced_frames > ETHER_TX_MAX_FRAME(osi_dma->tx_ring_sz)) ||
		(ec->tx_max_coalesced_frames < ETHER_MIN_TX_COALESCE_FRAMES)) {
		netdev_err(dev,
			   "invalid tx-frames, must be in the range of"
#ifdef CONFIG_MAX_SKB_FRAGS
			   " %d to %d frames\n", ETHER_MIN_TX_COALESCE_FRAMES,
#else
			   " %d to %ld frames\n", ETHER_MIN_TX_COALESCE_FRAMES,
#endif
			   ETHER_TX_MAX_FRAME(osi_dma->tx_ring_sz));
		return -EINVAL;
	} else {
		osi_dma->use_tx_frames = OSI_ENABLE;
	}

	if (ec->tx_coalesce_usecs == OSI_DISABLE) {
		osi_dma->use_tx_usecs = OSI_DISABLE;
	} else if ((ec->tx_coalesce_usecs > ETHER_MAX_TX_COALESCE_USEC) ||
		   (ec->tx_coalesce_usecs < ETHER_MIN_TX_COALESCE_USEC)) {
		netdev_err(dev,
			   "invalid tx_usecs, must be in a range of"
			   " %d to %d usec\n", ETHER_MIN_TX_COALESCE_USEC,
			   ETHER_MAX_TX_COALESCE_USEC);
		return -EINVAL;
	} else {
		osi_dma->use_tx_usecs = OSI_ENABLE;
	}

	netdev_err(dev, "TX COALESCING USECS is %s\n", osi_dma->use_tx_usecs ?
		   "ENABLED" : "DISABLED");

	netdev_err(dev, "TX COALESCING FRAMES is %s\n", osi_dma->use_tx_frames ?
		   "ENABLED" : "DISABLED");

	if (ec->rx_max_coalesced_frames == OSI_DISABLE) {
		osi_dma->use_rx_frames = OSI_DISABLE;
	} else if ((ec->rx_max_coalesced_frames > osi_dma->rx_ring_sz) ||
		(ec->rx_max_coalesced_frames < ETHER_MIN_RX_COALESCE_FRAMES)) {
		netdev_err(dev,
			   "invalid rx-frames, must be in the range of"
			   " %d to %d frames\n", ETHER_MIN_RX_COALESCE_FRAMES,
			   osi_dma->rx_ring_sz);
		return -EINVAL;
	} else {
		osi_dma->use_rx_frames = OSI_ENABLE;
	}

	if (ec->rx_coalesce_usecs == OSI_DISABLE) {
		osi_dma->use_riwt = OSI_DISABLE;
	} else if (osi_dma->mac == OSI_MAC_HW_EQOS &&
		   (ec->rx_coalesce_usecs > ETHER_MAX_RX_COALESCE_USEC ||
		    ec->rx_coalesce_usecs < ETHER_EQOS_MIN_RX_COALESCE_USEC)) {
		netdev_err(dev, "invalid rx_usecs, must be in a range of %d to %d usec\n",
			   ETHER_EQOS_MIN_RX_COALESCE_USEC,
			   ETHER_MAX_RX_COALESCE_USEC);
		return -EINVAL;

	} else if (osi_dma->mac != OSI_MAC_HW_EQOS &&
		   (ec->rx_coalesce_usecs > ETHER_MAX_RX_COALESCE_USEC ||
		    ec->rx_coalesce_usecs < ETHER_MGBE_MIN_RX_COALESCE_USEC)) {
		netdev_err(dev,
			   "invalid rx_usecs, must be in a range of %d to %d usec\n",
			   ETHER_MGBE_MIN_RX_COALESCE_USEC,
			   ETHER_MAX_RX_COALESCE_USEC);
		return -EINVAL;
	} else {
		osi_dma->use_riwt = OSI_ENABLE;
	}

	if (osi_dma->use_tx_usecs == OSI_DISABLE &&
	    osi_dma->use_tx_frames == OSI_ENABLE) {
		netdev_err(dev, "invalid settings : tx-frames must be enabled"
			   " along with tx-usecs\n");
		return -EINVAL;
	}
	if (osi_dma->use_riwt == OSI_DISABLE &&
	    osi_dma->use_rx_frames == OSI_ENABLE) {
		netdev_err(dev, "invalid settings : rx-frames must be enabled"
			   " along with rx-usecs\n");
		return -EINVAL;
	}
	netdev_err(dev, "RX COALESCING USECS is %s\n", osi_dma->use_riwt ?
		   "ENABLED" : "DISABLED");

	netdev_err(dev, "RX COALESCING FRAMES is %s\n", osi_dma->use_rx_frames ?
		   "ENABLED" : "DISABLED");

	osi_dma->rx_riwt = ec->rx_coalesce_usecs;
	osi_dma->rx_frames = ec->rx_max_coalesced_frames;
	osi_dma->tx_usecs = ec->tx_coalesce_usecs;
	osi_dma->tx_frames = ec->tx_max_coalesced_frames;
	return 0;
}

/**
 * @brief Set interrupt coalescing parameters.
 *
 * Algorithm: This function is invoked by kernel when user request to get
 * interrupt coalescing parameters. As coalescing parameters are same
 * for all the channels, so this function will get coalescing
 * details from channel zero and return.
 *
 * @param[in] dev: Net device data.
 * @param[in] ec: pointer to ethtool_coalesce structure
 *
 * @note MAC and PHY need to be initialized.
 *
 * @retval 0 on Success.
 */
#if defined(NV_ETHTOOL_OPS_GET_SET_COALESCE_HAS_COAL_AND_EXTACT_ARGS) /* Linux v5.15 */
static int ether_get_coalesce(struct net_device *dev,
			      struct ethtool_coalesce *ec,
			      struct kernel_ethtool_coalesce *kernel_coal,
			      struct netlink_ext_ack *extack)
#else
static int ether_get_coalesce(struct net_device *dev,
			      struct ethtool_coalesce *ec)
#endif
{
	struct ether_priv_data *pdata = netdev_priv(dev);
	struct osi_dma_priv_data *osi_dma = pdata->osi_dma;

	memset(ec, 0, sizeof(struct ethtool_coalesce));
	ec->rx_coalesce_usecs = osi_dma->rx_riwt;
	ec->rx_max_coalesced_frames = osi_dma->rx_frames;
	ec->tx_coalesce_usecs = osi_dma->tx_usecs;
	ec->tx_max_coalesced_frames = osi_dma->tx_frames;

	return 0;
}

#ifndef OSI_STRIPPED_LIB
/*
 * @brief Get current EEE configuration in MAC/PHY
 *
 * Algorithm: This function is invoked by kernel when user request to get
 * current EEE parameters. The function invokes the PHY framework to fill
 * the supported & advertised EEE modes, as well as link partner EEE modes
 * if it is available.
 *
 * @param[in] ndev: Net device data.
 * @param[in] cur_eee: Pointer to struct ethtool_eee
 *
 * @note MAC and PHY need to be initialized.
 *
 * @retval 0 on Success.
 * @retval -ve on Failure
 */
static int ether_get_eee(struct net_device *ndev,
#if defined(NV_ETHTOOL_KEEE_STRUCT_PRESENT) /* Linux v6.9 */
			 struct ethtool_keee *cur_eee)
#else
			 struct ethtool_eee *cur_eee)
#endif
{
	int ret;
	struct ether_priv_data *pdata = netdev_priv(ndev);
	struct phy_device *phydev = pdata->phydev;

	if (!pdata->hw_feat.eee_sel) {
		return -EOPNOTSUPP;
	}

	if (!netif_running(ndev)) {
		netdev_err(pdata->ndev, "interface not up\n");
		return -EINVAL;
	}

	ret = phy_ethtool_get_eee(phydev, cur_eee);
	if (ret) {
		netdev_warn(pdata->ndev, "Cannot get PHY EEE config\n");
		return ret;
	}

	cur_eee->eee_enabled = pdata->eee_enabled;
	cur_eee->tx_lpi_enabled = pdata->tx_lpi_enabled;
	cur_eee->eee_active = pdata->eee_active;
	cur_eee->tx_lpi_timer = pdata->tx_lpi_timer;

	return ret;
}

/**
 * @brief Helper routing to validate EEE configuration requested via ethtool
 *
 * Algorithm: Check for invalid combinations of ethtool_eee fields. If any
 *	invalid combination found, override it.
 *
 * @param[in] ndev: Net device data.
 * @param[in] eee_req: Pointer to struct ethtool_eee configuration requested
 *
 * @retval none
 */
static inline void validate_eee_conf(struct net_device *ndev,
#if defined(NV_ETHTOOL_KEEE_STRUCT_PRESENT) /* Linux v6.9 */
				     struct ethtool_keee *eee_req,
				     struct ethtool_keee cur_eee)
#else
				     struct ethtool_eee *eee_req,
				     struct ethtool_eee cur_eee)
#endif
{
	/* These are the invalid combinations that can be requested.
	 * EEE | Tx LPI | Rx LPI
	 *----------------------
	 * 0   | 0      | 1
	 * 0   | 1      | 0
	 * 0   | 1      | 1
	 * 1   | 0      | 0
	 *
	 * These combinations can be entered from a state where either EEE was
	 * enabled or disabled originally. Hence decide next valid state based
	 * on whether EEE has toggled or not.
	 */
	if (!eee_req->eee_enabled && !eee_req->tx_lpi_enabled &&
#if defined(NV_ETHTOOL_KEEE_STRUCT_PRESENT) /* Linux v6.9 */
	    !linkmode_empty(eee_req->advertised)) {
#else
	    eee_req->advertised) {
#endif
		if (eee_req->eee_enabled != cur_eee.eee_enabled) {
			netdev_warn(ndev, "EEE off. Set Rx LPI off\n");
#if defined(NV_ETHTOOL_KEEE_STRUCT_PRESENT) /* Linux v6.9 */
			linkmode_zero(eee_req->advertised);
#else
			eee_req->advertised = OSI_DISABLE;
#endif
		} else {
			netdev_warn(ndev, "Rx LPI on. Set EEE on\n");
			eee_req->eee_enabled = OSI_ENABLE;
		}
	}

	if (!eee_req->eee_enabled && eee_req->tx_lpi_enabled &&
#if defined(NV_ETHTOOL_KEEE_STRUCT_PRESENT) /* Linux v6.9 */
	    linkmode_empty(eee_req->advertised)) {
#else
	    !eee_req->advertised) {
#endif
		if (eee_req->eee_enabled != cur_eee.eee_enabled) {
			netdev_warn(ndev, "EEE off. Set Tx LPI off\n");
			eee_req->tx_lpi_enabled = OSI_DISABLE;
		} else {
			/* phy_init_eee will fail if Rx LPI advertisement is
			 * disabled. Hence change the adv back to enable,
			 * so that Tx LPI will be set.
			 */
			netdev_warn(ndev, "Tx LPI on. Set EEE & Rx LPI on\n");
			eee_req->eee_enabled = OSI_ENABLE;
#if defined(NV_ETHTOOL_KEEE_STRUCT_PRESENT) /* Linux v6.9 */
			linkmode_copy(eee_req->advertised, eee_req->supported);
#else
			eee_req->advertised = eee_req->supported;
#endif
		}
	}

	if (!eee_req->eee_enabled && eee_req->tx_lpi_enabled &&
#if defined(NV_ETHTOOL_KEEE_STRUCT_PRESENT) /* Linux v6.9 */
	    !linkmode_empty(eee_req->advertised)) {
#else
	    eee_req->advertised) {
#endif
		if (eee_req->eee_enabled != cur_eee.eee_enabled) {
			netdev_warn(ndev, "EEE off. Set Tx & Rx LPI off\n");
			eee_req->tx_lpi_enabled = OSI_DISABLE;
#if defined(NV_ETHTOOL_KEEE_STRUCT_PRESENT) /* Linux v6.9 */
			linkmode_zero(eee_req->advertised);
#else
			eee_req->advertised = OSI_DISABLE;
#endif
		} else {
			netdev_warn(ndev, "Tx & Rx LPI on. Set EEE on\n");
			eee_req->eee_enabled = OSI_ENABLE;
		}
	}

	if (eee_req->eee_enabled && !eee_req->tx_lpi_enabled &&
#if defined(NV_ETHTOOL_KEEE_STRUCT_PRESENT) /* Linux v6.9 */
	    linkmode_empty(eee_req->advertised)) {
#else
	    !eee_req->advertised) {
#endif
		if (eee_req->eee_enabled != cur_eee.eee_enabled) {
			netdev_warn(ndev, "EEE on. Set Tx & Rx LPI on\n");
			eee_req->tx_lpi_enabled = OSI_ENABLE;
#if defined(NV_ETHTOOL_KEEE_STRUCT_PRESENT) /* Linux v6.9 */
			linkmode_copy(eee_req->advertised, eee_req->supported);
#else
			eee_req->advertised = eee_req->supported;
#endif
		} else {
			netdev_warn(ndev, "Tx,Rx LPI off. Set EEE off\n");
			eee_req->eee_enabled = OSI_DISABLE;
		}
	}
}

/**
 * @brief Set current EEE configuration
 *
 * Algorithm: This function is invoked by kernel when user request to Set
 * current EEE parameters.
 *
 * @param[in] ndev: Net device data.
 * @param[in] eee_req: Pointer to struct ethtool_eee
 *
 * @note MAC and PHY need to be initialized.
 *
 * @retval 0 on Success.
 * @retval -ve on Failure
 */
static int ether_set_eee(struct net_device *ndev,
#if defined(NV_ETHTOOL_KEEE_STRUCT_PRESENT) /* Linux v6.9 */
			 struct ethtool_keee *eee_req)
#else
			 struct ethtool_eee *eee_req)
#endif
{
	struct ether_priv_data *pdata = netdev_priv(ndev);
	struct phy_device *phydev = pdata->phydev;
#if defined(NV_ETHTOOL_KEEE_STRUCT_PRESENT) /* Linux v6.9 */
	struct ethtool_keee cur_eee;
#else
	struct ethtool_eee cur_eee;
#endif

	if (!pdata->hw_feat.eee_sel) {
		return -EOPNOTSUPP;
	}

	if (!netif_running(ndev)) {
		netdev_err(pdata->ndev, "interface not up\n");
		return -EINVAL;
	}

	if (ether_get_eee(ndev, &cur_eee)) {
		return -EOPNOTSUPP;
	}

	/* Validate args
	 * 1. Validate the tx lpi timer for acceptable range */
	if (cur_eee.tx_lpi_timer != eee_req->tx_lpi_timer) {
		if (eee_req->tx_lpi_timer == 0) {
			pdata->tx_lpi_timer = OSI_DEFAULT_TX_LPI_TIMER;
		} else if (eee_req->tx_lpi_timer <= OSI_MAX_TX_LPI_TIMER &&
			   eee_req->tx_lpi_timer >= OSI_MIN_TX_LPI_TIMER &&
			   !(eee_req->tx_lpi_timer % OSI_MIN_TX_LPI_TIMER)) {
			pdata->tx_lpi_timer = eee_req->tx_lpi_timer;
		} else {
			netdev_err(ndev, "Tx LPI timer has to be < %u usec "
				   "in %u usec steps\n", OSI_MAX_TX_LPI_TIMER,
				   OSI_MIN_TX_LPI_TIMER);
			return -EINVAL;
		}
	}

	/* 2. Override invalid combinations of requested configuration */
	validate_eee_conf(ndev, eee_req, cur_eee);

	/* First store the requested & validated EEE configuration */
	pdata->eee_enabled = eee_req->eee_enabled;
	pdata->tx_lpi_enabled = eee_req->tx_lpi_enabled;
	pdata->tx_lpi_timer = eee_req->tx_lpi_timer;
	pdata->eee_active = eee_req->eee_active;

	/* If EEE adv has changed, inform PHY framework. PHY will
	 * restart ANEG and the ether_adjust_link callback will take care of
	 * enabling Tx LPI as needed.
	 */
	if (cur_eee.advertised != eee_req->advertised) {
		return phy_ethtool_set_eee(phydev, eee_req);
	}

	/* If no advertisement change, and only local Tx LPI changed, then
	 * configure the MAC right away.
	 */
	if (cur_eee.tx_lpi_enabled != eee_req->tx_lpi_enabled) {
		eee_req->eee_active = ether_conf_eee(pdata,
						     eee_req->tx_lpi_enabled);
		pdata->eee_active = eee_req->eee_active;
	}

	return 0;
}

int ether_set_wol(struct net_device *ndev, struct ethtool_wolinfo *wol)
{
	struct ether_priv_data *pdata = netdev_priv(ndev);
	int ret;

	if (!wol)
		return -EINVAL;

	if (!pdata->phydev) {
		netdev_err(pdata->ndev,
			   "%s: phydev is null check iface up status\n",
			   __func__);
		return -ENOTSUPP;
	}

	if (!phy_interrupt_is_valid(pdata->phydev))
		return -ENOTSUPP;

	ret = phy_ethtool_set_wol(pdata->phydev, wol);
	if (ret < 0)
		return ret;

	if (wol->wolopts) {
		ret = enable_irq_wake(pdata->phydev->irq);
		if (ret) {
			dev_err(pdata->dev, "PHY enable irq wake failed, %d\n",
				ret);
			return ret;
		}
		/* enable device wake on WoL set */
		device_init_wakeup(&ndev->dev, true);
	} else {
		ret = disable_irq_wake(pdata->phydev->irq);
		if (ret) {
			dev_info(pdata->dev,
				 "PHY disable irq wake failed, %d\n",
				 ret);
		}
		/* disable device wake on WoL reset */
		device_init_wakeup(&ndev->dev, false);
	}

	return ret;
}

void ether_get_wol(struct net_device *ndev, struct ethtool_wolinfo *wol)
{
	struct ether_priv_data *pdata = netdev_priv(ndev);

	if (!wol)
		return;

	if (!pdata->phydev) {
		netdev_err(pdata->ndev,
			   "%s: phydev is null check iface up status\n",
			   __func__);
		return;
	}

	wol->supported = 0;
	wol->wolopts = 0;

	if (!phy_interrupt_is_valid(pdata->phydev))
		return;

	phy_ethtool_get_wol(pdata->phydev, wol);
}

int ether_get_rxnfc(struct net_device *ndev,
		    struct ethtool_rxnfc *rxnfc,
		    u32 *rule_locs)
{
	struct ether_priv_data *pdata = netdev_priv(ndev);
	struct osi_core_priv_data *osi_core = pdata->osi_core;

	switch (rxnfc->cmd) {
	case ETHTOOL_GRXRINGS:
		rxnfc->data = osi_core->num_mtl_queues;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

u32 ether_get_rxfh_key_size(struct net_device *ndev)
{
	struct osi_ioctl ioctl_data = {};

	return sizeof(ioctl_data.data.rss.key);
}

u32 ether_get_rxfh_indir_size(struct net_device *ndev)
{
	struct osi_ioctl ioctl_data = {};

	return ARRAY_SIZE(ioctl_data.data.rss.table);
}

/**
 * @brief Get the contents of the RX flow hash indirection table, hash key
 * and/or hash function
 *
 * param[in] ndev: Pointer to net device structure.
 * param[out] indir: Pointer to indirection table
 * param[out] key: Pointer to Hash key
 * param[out] hfunc: Pointer to Hash function
 *
 * @retval 0 on success
 */
#if defined(NV_ETHTOOL_OPS_GET_SET_RXFH_HAS_RXFH_PARAM_ARGS)
static int ether_get_rxfh(struct net_device *ndev,
			  struct ethtool_rxfh_param *rxfh)
#else
static int ether_get_rxfh(struct net_device *ndev, u32 *indir, u8 *key,
			  u8 *hfunc)
#endif
{
	struct ether_priv_data *pdata = netdev_priv(ndev);
	struct osi_core_priv_data *osi_core = pdata->osi_core;
	struct osi_ioctl ioctl_data = {};
	struct osi_core_rss *rss = (struct osi_core_rss *)&ioctl_data.data.rss;
#if defined(NV_ETHTOOL_OPS_GET_SET_RXFH_HAS_RXFH_PARAM_ARGS)
	u32 *indir = rxfh->indir;
	u8 *hfunc = &rxfh->hfunc;
	u8 *key = rxfh->key;
#endif
	int i;
	int ret = 0;

	ioctl_data.cmd = OSI_CMD_GET_RSS;
	ret = osi_handle_ioctl(osi_core, &ioctl_data);
	if (ret != 0) {
		dev_err(pdata->dev, "Failed to get RSS info from registers\n");
		return ret;
	}

	if (indir) {
		for (i = 0; i < ARRAY_SIZE(rss->table); i++)
			indir[i] = rss->table[i];
	}

	if (key) {
		memcpy(key, rss->key, sizeof(rss->key));
	}

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;

	return ret;
}

/**
 * @b	rief Set the contents of the RX flow hash indirection table, hash key
 * and/or hash function
 *
 * param[in] ndev: Pointer to net device structure.
 * param[in] indir: Pointer to indirection table
 * param[in] key: Pointer to Hash key
 * param[hfunc] hfunc: Hash function
 *
 * @retval 0 on success
 * @retval -1 on failure.
 */
#if defined(NV_ETHTOOL_OPS_GET_SET_RXFH_HAS_RXFH_PARAM_ARGS)
static int ether_set_rxfh(struct net_device *ndev,
			  struct ethtool_rxfh_param *rxfh,
			  struct netlink_ext_ack *extack)
#else
static int ether_set_rxfh(struct net_device *ndev, const u32 *indir,
			  const u8 *key, const u8 hfunc)
#endif
{
	struct ether_priv_data *pdata = netdev_priv(ndev);
	struct osi_ioctl ioctl_data = {};
	struct osi_core_rss *rss = (struct osi_core_rss *)&ioctl_data.data.rss;
#if defined(NV_ETHTOOL_OPS_GET_SET_RXFH_HAS_RXFH_PARAM_ARGS)
	u32 *indir = rxfh->indir;
	u8 hfunc = rxfh->hfunc;
	u8 *key = rxfh->key;
#endif
	int i;
	int ret = 0;

	if (!netif_running(ndev)) {
		netdev_err(pdata->ndev, "interface must be up\n");
		return -ENODEV;
	}

	if ((hfunc != ETH_RSS_HASH_NO_CHANGE) && (hfunc != ETH_RSS_HASH_TOP))
		return -EOPNOTSUPP;

	/* First get current RSS configuration and update what ever required */
	ioctl_data.cmd = OSI_CMD_GET_RSS;
	ret = osi_handle_ioctl(pdata->osi_core, &ioctl_data);
	if (ret != 0) {
		dev_err(pdata->dev, "Failed to get current RSS configuration\n");
	        return ret;
	}

	if (indir) {
		for (i = 0; i < ARRAY_SIZE(rss->table); i++)
			rss->table[i] = indir[i];
	}

	if (key) {
		memcpy(rss->key, key, sizeof(rss->key));
	}
	/* RSS need to be enabled for applying the settings */
	rss->enable = 1;

	ioctl_data.cmd = OSI_CMD_CONFIG_RSS;
	return osi_handle_ioctl(pdata->osi_core, &ioctl_data);
}

#if defined(NV_ETHTOOL_OPS_GET_SET_RINGPARAM_HAS_RINGPARAM_AND_EXTACT_ARGS) /* Linux v5.17 */
static void ether_get_ringparam(struct net_device *ndev,
				struct ethtool_ringparam *ring,
				struct kernel_ethtool_ringparam *kernel_ring,
				struct netlink_ext_ack *extack)
#else
static void ether_get_ringparam(struct net_device *ndev,
				struct ethtool_ringparam *ring)
#endif
{
	static const unsigned int tx_max_supported_sz[] = { 1024, 4096, 4096 };
	static const unsigned int rx_max_supported_sz[] = { 1024, 16384, 16384 };
	struct osi_dma_priv_data *osi_dma;
	struct ether_priv_data *pdata;

	if (!ndev || !ring)
		return;

	pdata = netdev_priv(ndev);
	if (!pdata || !pdata->osi_dma)
		return;

	osi_dma = pdata->osi_dma;
	if (osi_dma->mac >= ARRAY_SIZE(tx_max_supported_sz))
		return;

	ring->tx_max_pending = tx_max_supported_sz[osi_dma->mac];
	ring->rx_max_pending = rx_max_supported_sz[osi_dma->mac];
	ring->rx_pending = osi_dma->rx_ring_sz;
	ring->tx_pending = osi_dma->tx_ring_sz;
}

#if defined(NV_ETHTOOL_OPS_GET_SET_RINGPARAM_HAS_RINGPARAM_AND_EXTACT_ARGS) /* Linux v5.17 */
static int ether_set_ringparam(struct net_device *ndev,
			       struct ethtool_ringparam *ring,
			       struct kernel_ethtool_ringparam *kernel_ring,
			       struct netlink_ext_ack *extack)
#else
static int ether_set_ringparam(struct net_device *ndev,
			       struct ethtool_ringparam *ring)
#endif
{
	static const unsigned int tx_ring_sz_max[] = { 1024, 4096, 4096 };
	static const unsigned int rx_ring_sz_max[] = { 1024, 16384, 16384 };
	struct ether_priv_data *pdata;
	struct osi_dma_priv_data *osi_dma;
	int ret = 0;

	if (!ndev || !ring)
		return -EINVAL;

	pdata = netdev_priv(ndev);
	if (!pdata || !pdata->osi_dma)
		return -EINVAL;

	osi_dma = pdata->osi_dma;
	if (osi_dma->mac >= ARRAY_SIZE(tx_ring_sz_max))
		return -EINVAL;

	if (ring->rx_mini_pending ||
	    ring->rx_jumbo_pending ||
	    ring->rx_pending < 64 ||
	    ring->rx_pending > rx_ring_sz_max[osi_dma->mac] ||
	    !is_power_of_2(ring->rx_pending) ||
	    ring->tx_pending < 64 ||
	    ring->tx_pending > tx_ring_sz_max[osi_dma->mac] ||
	    !is_power_of_2(ring->tx_pending))
		return -EINVAL;

	/* Stop the network device */
	if (netif_running(ndev) &&
	    ndev->netdev_ops &&
	    ndev->netdev_ops->ndo_stop) {
		ret = ndev->netdev_ops->ndo_stop(ndev);
		if (ret)
			return ret;
	}

	osi_dma->rx_ring_sz = ring->rx_pending;
	osi_dma->tx_ring_sz = ring->tx_pending;

	/* Start the network device */
	if (netif_running(ndev) &&
	    ndev->netdev_ops &&
	    ndev->netdev_ops->ndo_open) {
		ret = ndev->netdev_ops->ndo_open(ndev);
		if (ret) {
			/* Restore original ring sizes on failure */
			osi_dma->rx_ring_sz = ring->rx_pending;
			osi_dma->tx_ring_sz = ring->tx_pending;
		}
	}

	return ret;
}

unsigned int ether_get_msglevel(struct net_device *ndev)
{
	struct ether_priv_data *pdata = netdev_priv(ndev);

	return pdata->msg_enable;
}

void ether_set_msglevel(struct net_device *ndev, u32 level)
{
	struct ether_priv_data *pdata = netdev_priv(ndev);

	pdata->msg_enable = level;
}
#endif /* OSI_STRIPPED_LIB */

/**
 * @brief Set of ethtool operations
 */
static const struct ethtool_ops ether_ethtool_ops = {
	.get_link = ethtool_op_get_link,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
	.get_ts_info = ether_get_ts_info,
	.get_strings = ether_get_strings,
	.get_ethtool_stats = ether_get_ethtool_stats,
	.get_sset_count = ether_get_sset_count,
	.get_coalesce = ether_get_coalesce,
	.supported_coalesce_params = (ETHTOOL_COALESCE_USECS |
		ETHTOOL_COALESCE_MAX_FRAMES),
	.set_coalesce = ether_set_coalesce,
#ifndef OSI_STRIPPED_LIB
	.get_wol = ether_get_wol,
	.set_wol = ether_set_wol,
	.self_test = ether_selftest_run,
	.get_rxnfc = ether_get_rxnfc,
	.get_pauseparam = ether_get_pauseparam,
	.set_pauseparam = ether_set_pauseparam,
	.get_eee = ether_get_eee,
	.set_eee = ether_set_eee,
	.get_rxfh_key_size = ether_get_rxfh_key_size,
	.get_rxfh_indir_size = ether_get_rxfh_indir_size,
	.get_rxfh = ether_get_rxfh,
	.set_rxfh = ether_set_rxfh,
	.get_ringparam = ether_get_ringparam,
	.set_ringparam = ether_set_ringparam,
	.get_msglevel = ether_get_msglevel,
	.set_msglevel = ether_set_msglevel,
#endif /* OSI_STRIPPED_LIB */
};

void ether_set_ethtool_ops(struct net_device *ndev)
{
	ndev->ethtool_ops = &ether_ethtool_ops;
}
