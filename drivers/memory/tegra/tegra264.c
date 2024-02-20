// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024, NVIDIA CORPORATION.  All rights reserved.
 */

#include <soc/tegra/mc.h>

#include <dt-bindings/memory/tegra264-mc.h>
#include <linux/interconnect.h>
#include <linux/of_device.h>
#include <linux/tegra-icc.h>
#include <linux/tegra264-bwmgr.h>

#include <soc/tegra/bpmp.h>
#include "mc.h"

/*
 * MC Client entries are sorted in the increasing order of the
 * override and security register offsets.
 */
static const struct tegra_mc_client tegra264_mc_clients[] = {
	{
		.id = TEGRA264_MEMORY_CLIENT_HDAR,
		.name = "hdar",
		.bpmp_id = TEGRA264_BWMGR_HDA,
		.type = TEGRA_ICC_ISO_AUDIO,
	},
};

/*
 * tegra264_mc_icc_set() - Pass MC client info to the BPMP-FW
 * @src: ICC node for Memory Controller's (MC) Client
 * @dst: ICC node for Memory Controller (MC)
 *
 * Passing the current request info from the MC to the BPMP-FW where
 * LA and PTSA registers are accessed and the final EMC freq is set
 * based on client_id, type, latency and bandwidth.
 * icc_set_bw() makes set_bw calls for both MC and EMC providers in
 * sequence. Both the calls are protected by 'mutex_lock(&icc_lock)'.
 * So, the data passed won't be updated by concurrent set calls from
 * other clients.
 */
static int tegra264_mc_icc_set(struct icc_node *src, struct icc_node *dst)
{
	struct tegra_mc *mc = icc_provider_to_tegra_mc(dst->provider);
	struct mrq_bwmgr_int_request bwmgr_req = { 0 };
	struct mrq_bwmgr_int_response bwmgr_resp = { 0 };
	const struct tegra_mc_client *pclient = src->data;
	struct tegra_bpmp_message msg;
	int ret;

	/*
	 * Same Src and Dst node will happen during boot from icc_node_add().
	 * This can be used to pre-initialize and set bandwidth for all clients
	 * before their drivers are loaded. We are skipping this case as for us,
	 * the pre-initialization already happened in Bootloader(MB2) and BPMP-FW.
	 */
	if (src->id == dst->id)
		return 0;

	if (!mc->bwmgr_mrq_supported)
		return 0;

	if (!mc->bpmp) {
		dev_err(mc->dev, "BPMP reference NULL\n");
		return -ENOENT;
	}

	if (pclient->type == TEGRA_ICC_NISO)
		bwmgr_req.bwmgr_calc_set_req.niso_bw = src->avg_bw;
	else
		bwmgr_req.bwmgr_calc_set_req.iso_bw = src->avg_bw;

	bwmgr_req.bwmgr_calc_set_req.client_id = pclient->bpmp_id;

	bwmgr_req.cmd = CMD_BWMGR_INT_CALC_AND_SET;
	bwmgr_req.bwmgr_calc_set_req.mc_floor = src->peak_bw;
	bwmgr_req.bwmgr_calc_set_req.floor_unit = BWMGR_INT_UNIT_KBPS;

	memset(&msg, 0, sizeof(msg));
	msg.mrq = MRQ_BWMGR_INT;
	msg.tx.data = &bwmgr_req;
	msg.tx.size = sizeof(bwmgr_req);
	msg.rx.data = &bwmgr_resp;
	msg.rx.size = sizeof(bwmgr_resp);

	ret = tegra_bpmp_transfer(mc->bpmp, &msg);
	if (ret < 0) {
		dev_err(mc->dev, "BPMP transfer failed: %d\n", ret);
		goto error;
	}

	if (msg.rx.ret < 0) {
		pr_err("failed to set bandwidth for %u: %d\n",
		       bwmgr_req.bwmgr_calc_set_req.client_id, msg.rx.ret);
		ret = -EINVAL;
	}

error:
	return ret;
}

static int tegra264_mc_icc_aggregate(struct icc_node *node, u32 tag, u32 avg_bw,
				     u32 peak_bw, u32 *agg_avg, u32 *agg_peak)
{
	struct icc_provider *p = node->provider;
	struct tegra_mc *mc = icc_provider_to_tegra_mc(p);

	if (!mc->bwmgr_mrq_supported)
		return 0;

	*agg_avg += avg_bw;
	*agg_peak = max(*agg_peak, peak_bw);

	return 0;
}

static int tegra264_mc_icc_get_init_bw(struct icc_node *node, u32 *avg, u32 *peak)
{
	*avg = 0;
	*peak = 0;

	return 0;
}

static const struct tegra_mc_icc_ops tegra264_mc_icc_ops = {
	.xlate = tegra_mc_icc_xlate,
	.aggregate = tegra264_mc_icc_aggregate,
	.get_bw = tegra264_mc_icc_get_init_bw,
	.set = tegra264_mc_icc_set,
};

const struct tegra_mc_soc tegra264_mc_soc = {
	.num_clients = ARRAY_SIZE(tegra264_mc_clients),
	.clients = tegra264_mc_clients,
	.num_address_bits = 40,
	.num_channels = 16,
	.client_id_mask = 0x1ff,
	.intmask = MC_INT_DECERR_ROUTE_SANITY |
		   MC_INT_DECERR_GENERALIZED_CARVEOUT | MC_INT_DECERR_MTS |
		   MC_INT_SECERR_SEC | MC_INT_DECERR_VPR |
		   MC_INT_SECURITY_VIOLATION | MC_INT_DECERR_EMEM,
	.has_addr_hi_reg = true,
	.ops = &tegra186_mc_ops,
	.icc_ops = &tegra264_mc_icc_ops,
	.ch_intmask = 0x0000ff00,
	.global_intstatus_channel_shift = 8,
	/*
	 * Additionally, there are lite carveouts but those are not currently
	 * supported.
	 */
	.num_carveouts = 32,
};
