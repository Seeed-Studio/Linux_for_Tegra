// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 */

/* Support for NVIDIA specific attributes. */

#include <linux/module.h>
#include <linux/topology.h>

#include "arm_cspmu.h"

#define PMCNTENSET					0xC00
#define PMCNTENCLR					0xC20
#define PMCR						0xE04

#define PMCR_E						BIT(0)

#define NV_PCIE_PORT_COUNT           10ULL
#define NV_PCIE_FILTER_ID_MASK       GENMASK_ULL(NV_PCIE_PORT_COUNT - 1, 0)

#define NV_NVL_C2C_PORT_COUNT        2ULL
#define NV_NVL_C2C_FILTER_ID_MASK    GENMASK_ULL(NV_NVL_C2C_PORT_COUNT - 1, 0)

#define NV_CNVL_PORT_COUNT           4ULL
#define NV_CNVL_FILTER_ID_MASK       GENMASK_ULL(NV_CNVL_PORT_COUNT - 1, 0)

#define NV_UCF_FILTER_ID_MASK        GENMASK_ULL(4, 0)

#define NV_UPHY_FILTER_ID_MASK       GENMASK_ULL(16, 0)

#define NV_VISION_FILTER_ID_MASK     GENMASK_ULL(19, 0)

#define NV_DISPLAY_FILTER_ID_MASK    BIT(0)

#define NV_UCF_GPU_FILTER_ID_MASK    BIT(0)

#define NV_GENERIC_FILTER_ID_MASK    GENMASK_ULL(31, 0)

#define NV_PRODID_MASK		(ARM_CSPMU_PMIIDR_PRODUCTID |	\
				 ARM_CSPMU_PMIIDR_VARIANT |	\
				 ARM_CSPMU_PMIIDR_REVISION)

#define NV_FORMAT_NAME_GENERIC	0

#define to_nv_cspmu_ctx(cspmu)	((struct nv_cspmu_ctx *)(cspmu->impl.ctx))

#define NV_CSPMU_EVENT_ATTR_4_INNER(_pref, _num, _suff, _config)	\
	ARM_CSPMU_EVENT_ATTR(_pref##_num##_suff, _config)

#define NV_CSPMU_EVENT_ATTR_4(_pref, _suff, _config)			\
	NV_CSPMU_EVENT_ATTR_4_INNER(_pref, _0_, _suff, _config),	\
	NV_CSPMU_EVENT_ATTR_4_INNER(_pref, _1_, _suff, _config + 1),	\
	NV_CSPMU_EVENT_ATTR_4_INNER(_pref, _2_, _suff, _config + 2),	\
	NV_CSPMU_EVENT_ATTR_4_INNER(_pref, _3_, _suff, _config + 3)

struct nv_cspmu_ctx {
	const char *name;
	u32 filter_mask;
	u32 filter_default_val;
	struct attribute **event_attr;
	struct attribute **format_attr;
	u32 *pmcnten;
};

static struct attribute *scf_pmu_event_attrs[] = {
	ARM_CSPMU_EVENT_ATTR(bus_cycles,			0x1d),

	ARM_CSPMU_EVENT_ATTR(scf_cache_allocate,		0xF0),
	ARM_CSPMU_EVENT_ATTR(scf_cache_refill,			0xF1),
	ARM_CSPMU_EVENT_ATTR(scf_cache,				0xF2),
	ARM_CSPMU_EVENT_ATTR(scf_cache_wb,			0xF3),

	NV_CSPMU_EVENT_ATTR_4(socket, rd_data,			0x101),
	NV_CSPMU_EVENT_ATTR_4(socket, dl_rsp,			0x105),
	NV_CSPMU_EVENT_ATTR_4(socket, wb_data,			0x109),
	NV_CSPMU_EVENT_ATTR_4(socket, ev_rsp,			0x10d),
	NV_CSPMU_EVENT_ATTR_4(socket, prb_data,			0x111),

	NV_CSPMU_EVENT_ATTR_4(socket, rd_outstanding,		0x115),
	NV_CSPMU_EVENT_ATTR_4(socket, dl_outstanding,		0x119),
	NV_CSPMU_EVENT_ATTR_4(socket, wb_outstanding,		0x11d),
	NV_CSPMU_EVENT_ATTR_4(socket, wr_outstanding,		0x121),
	NV_CSPMU_EVENT_ATTR_4(socket, ev_outstanding,		0x125),
	NV_CSPMU_EVENT_ATTR_4(socket, prb_outstanding,		0x129),

	NV_CSPMU_EVENT_ATTR_4(socket, rd_access,		0x12d),
	NV_CSPMU_EVENT_ATTR_4(socket, dl_access,		0x131),
	NV_CSPMU_EVENT_ATTR_4(socket, wb_access,		0x135),
	NV_CSPMU_EVENT_ATTR_4(socket, wr_access,		0x139),
	NV_CSPMU_EVENT_ATTR_4(socket, ev_access,		0x13d),
	NV_CSPMU_EVENT_ATTR_4(socket, prb_access,		0x141),

	NV_CSPMU_EVENT_ATTR_4(ocu, gmem_rd_data,		0x145),
	NV_CSPMU_EVENT_ATTR_4(ocu, gmem_rd_access,		0x149),
	NV_CSPMU_EVENT_ATTR_4(ocu, gmem_wb_access,		0x14d),
	NV_CSPMU_EVENT_ATTR_4(ocu, gmem_rd_outstanding,		0x151),
	NV_CSPMU_EVENT_ATTR_4(ocu, gmem_wr_outstanding,		0x155),

	NV_CSPMU_EVENT_ATTR_4(ocu, rem_rd_data,			0x159),
	NV_CSPMU_EVENT_ATTR_4(ocu, rem_rd_access,		0x15d),
	NV_CSPMU_EVENT_ATTR_4(ocu, rem_wb_access,		0x161),
	NV_CSPMU_EVENT_ATTR_4(ocu, rem_rd_outstanding,		0x165),
	NV_CSPMU_EVENT_ATTR_4(ocu, rem_wr_outstanding,		0x169),

	ARM_CSPMU_EVENT_ATTR(gmem_rd_data,			0x16d),
	ARM_CSPMU_EVENT_ATTR(gmem_rd_access,			0x16e),
	ARM_CSPMU_EVENT_ATTR(gmem_rd_outstanding,		0x16f),
	ARM_CSPMU_EVENT_ATTR(gmem_dl_rsp,			0x170),
	ARM_CSPMU_EVENT_ATTR(gmem_dl_access,			0x171),
	ARM_CSPMU_EVENT_ATTR(gmem_dl_outstanding,		0x172),
	ARM_CSPMU_EVENT_ATTR(gmem_wb_data,			0x173),
	ARM_CSPMU_EVENT_ATTR(gmem_wb_access,			0x174),
	ARM_CSPMU_EVENT_ATTR(gmem_wb_outstanding,		0x175),
	ARM_CSPMU_EVENT_ATTR(gmem_ev_rsp,			0x176),
	ARM_CSPMU_EVENT_ATTR(gmem_ev_access,			0x177),
	ARM_CSPMU_EVENT_ATTR(gmem_ev_outstanding,		0x178),
	ARM_CSPMU_EVENT_ATTR(gmem_wr_data,			0x179),
	ARM_CSPMU_EVENT_ATTR(gmem_wr_outstanding,		0x17a),
	ARM_CSPMU_EVENT_ATTR(gmem_wr_access,			0x17b),

	NV_CSPMU_EVENT_ATTR_4(socket, wr_data,			0x17c),

	NV_CSPMU_EVENT_ATTR_4(ocu, gmem_wr_data,		0x180),
	NV_CSPMU_EVENT_ATTR_4(ocu, gmem_wb_data,		0x184),
	NV_CSPMU_EVENT_ATTR_4(ocu, gmem_wr_access,		0x188),
	NV_CSPMU_EVENT_ATTR_4(ocu, gmem_wb_outstanding,		0x18c),

	NV_CSPMU_EVENT_ATTR_4(ocu, rem_wr_data,			0x190),
	NV_CSPMU_EVENT_ATTR_4(ocu, rem_wb_data,			0x194),
	NV_CSPMU_EVENT_ATTR_4(ocu, rem_wr_access,		0x198),
	NV_CSPMU_EVENT_ATTR_4(ocu, rem_wb_outstanding,		0x19c),

	ARM_CSPMU_EVENT_ATTR(gmem_wr_total_bytes,		0x1a0),
	ARM_CSPMU_EVENT_ATTR(remote_socket_wr_total_bytes,	0x1a1),
	ARM_CSPMU_EVENT_ATTR(remote_socket_rd_data,		0x1a2),
	ARM_CSPMU_EVENT_ATTR(remote_socket_rd_outstanding,	0x1a3),
	ARM_CSPMU_EVENT_ATTR(remote_socket_rd_access,		0x1a4),

	ARM_CSPMU_EVENT_ATTR(cmem_rd_data,			0x1a5),
	ARM_CSPMU_EVENT_ATTR(cmem_rd_access,			0x1a6),
	ARM_CSPMU_EVENT_ATTR(cmem_rd_outstanding,		0x1a7),
	ARM_CSPMU_EVENT_ATTR(cmem_dl_rsp,			0x1a8),
	ARM_CSPMU_EVENT_ATTR(cmem_dl_access,			0x1a9),
	ARM_CSPMU_EVENT_ATTR(cmem_dl_outstanding,		0x1aa),
	ARM_CSPMU_EVENT_ATTR(cmem_wb_data,			0x1ab),
	ARM_CSPMU_EVENT_ATTR(cmem_wb_access,			0x1ac),
	ARM_CSPMU_EVENT_ATTR(cmem_wb_outstanding,		0x1ad),
	ARM_CSPMU_EVENT_ATTR(cmem_ev_rsp,			0x1ae),
	ARM_CSPMU_EVENT_ATTR(cmem_ev_access,			0x1af),
	ARM_CSPMU_EVENT_ATTR(cmem_ev_outstanding,		0x1b0),
	ARM_CSPMU_EVENT_ATTR(cmem_wr_data,			0x1b1),
	ARM_CSPMU_EVENT_ATTR(cmem_wr_outstanding,		0x1b2),

	NV_CSPMU_EVENT_ATTR_4(ocu, cmem_rd_data,		0x1b3),
	NV_CSPMU_EVENT_ATTR_4(ocu, cmem_rd_access,		0x1b7),
	NV_CSPMU_EVENT_ATTR_4(ocu, cmem_wb_access,		0x1bb),
	NV_CSPMU_EVENT_ATTR_4(ocu, cmem_rd_outstanding,		0x1bf),
	NV_CSPMU_EVENT_ATTR_4(ocu, cmem_wr_outstanding,		0x1c3),

	ARM_CSPMU_EVENT_ATTR(ocu_prb_access,			0x1c7),
	ARM_CSPMU_EVENT_ATTR(ocu_prb_data,			0x1c8),
	ARM_CSPMU_EVENT_ATTR(ocu_prb_outstanding,		0x1c9),

	ARM_CSPMU_EVENT_ATTR(cmem_wr_access,			0x1ca),

	NV_CSPMU_EVENT_ATTR_4(ocu, cmem_wr_access,		0x1cb),
	NV_CSPMU_EVENT_ATTR_4(ocu, cmem_wb_data,		0x1cf),
	NV_CSPMU_EVENT_ATTR_4(ocu, cmem_wr_data,		0x1d3),
	NV_CSPMU_EVENT_ATTR_4(ocu, cmem_wb_outstanding,		0x1d7),

	ARM_CSPMU_EVENT_ATTR(cmem_wr_total_bytes,		0x1db),

	ARM_CSPMU_EVENT_ATTR(cycles, ARM_CSPMU_EVT_CYCLES_DEFAULT),
	NULL,
};

static struct attribute *mcf_pmu_event_attrs[] = {
	ARM_CSPMU_EVENT_ATTR(rd_bytes_loc,			0x0),
	ARM_CSPMU_EVENT_ATTR(rd_bytes_rem,			0x1),
	ARM_CSPMU_EVENT_ATTR(wr_bytes_loc,			0x2),
	ARM_CSPMU_EVENT_ATTR(wr_bytes_rem,			0x3),
	ARM_CSPMU_EVENT_ATTR(total_bytes_loc,			0x4),
	ARM_CSPMU_EVENT_ATTR(total_bytes_rem,			0x5),
	ARM_CSPMU_EVENT_ATTR(rd_req_loc,			0x6),
	ARM_CSPMU_EVENT_ATTR(rd_req_rem,			0x7),
	ARM_CSPMU_EVENT_ATTR(wr_req_loc,			0x8),
	ARM_CSPMU_EVENT_ATTR(wr_req_rem,			0x9),
	ARM_CSPMU_EVENT_ATTR(total_req_loc,			0xa),
	ARM_CSPMU_EVENT_ATTR(total_req_rem,			0xb),
	ARM_CSPMU_EVENT_ATTR(rd_cum_outs_loc,			0xc),
	ARM_CSPMU_EVENT_ATTR(rd_cum_outs_rem,			0xd),
	ARM_CSPMU_EVENT_ATTR(cycles, ARM_CSPMU_EVT_CYCLES_DEFAULT),
	NULL,
};

static struct attribute *ucf_pmu_event_attrs[] = {
	ARM_CSPMU_EVENT_ATTR(slc_allocate,			0xf0),
	ARM_CSPMU_EVENT_ATTR(slc_refill,			0xf1),
	ARM_CSPMU_EVENT_ATTR(slc_access,			0xf2),
	ARM_CSPMU_EVENT_ATTR(slc_wb,				0xf3),
	ARM_CSPMU_EVENT_ATTR(slc_hit,				0x118),
	ARM_CSPMU_EVENT_ATTR(slc_access_wr,			0x112),
	ARM_CSPMU_EVENT_ATTR(slc_access_rd,			0x111),
	ARM_CSPMU_EVENT_ATTR(slc_refill_wr,			0x10a),
	ARM_CSPMU_EVENT_ATTR(slc_refill_rd,			0x109),
	ARM_CSPMU_EVENT_ATTR(slc_hit_wr,			0x11a),
	ARM_CSPMU_EVENT_ATTR(slc_hit_rd,			0x119),
	ARM_CSPMU_EVENT_ATTR(slc_access_dataless,		0x183),
	ARM_CSPMU_EVENT_ATTR(slc_access_atomic,			0x184),
	ARM_CSPMU_EVENT_ATTR(local_snoop,			0x180),
	ARM_CSPMU_EVENT_ATTR(ext_snp_access,			0x181),
	ARM_CSPMU_EVENT_ATTR(ext_snp_evict,			0x182),

	ARM_CSPMU_EVENT_ATTR(ucf_bus_cycles,			0x1d),

	ARM_CSPMU_EVENT_ATTR(any_access_wr,			0x112),
	ARM_CSPMU_EVENT_ATTR(any_access_rd,			0x111),
	ARM_CSPMU_EVENT_ATTR(any_byte_wr,			0x114),
	ARM_CSPMU_EVENT_ATTR(any_byte_rd,			0x113),
	ARM_CSPMU_EVENT_ATTR(any_outstanding_rd,		0x115),

	ARM_CSPMU_EVENT_ATTR(local_dram_access_wr,		0x122),
	ARM_CSPMU_EVENT_ATTR(local_dram_access_rd,		0x121),
	ARM_CSPMU_EVENT_ATTR(local_dram_byte_wr,		0x124),
	ARM_CSPMU_EVENT_ATTR(local_dram_byte_rd,		0x123),

	ARM_CSPMU_EVENT_ATTR(mmio_access_wr,			0x132),
	ARM_CSPMU_EVENT_ATTR(mmio_access_rd,			0x131),
	ARM_CSPMU_EVENT_ATTR(mmio_byte_wr,			0x134),
	ARM_CSPMU_EVENT_ATTR(mmio_byte_rd,			0x133),
	ARM_CSPMU_EVENT_ATTR(mmio_outstanding_rd,		0x135),

	ARM_CSPMU_EVENT_ATTR(cycles, ARM_CSPMU_EVT_CYCLES_DEFAULT),

	NULL,
};

static struct attribute *display_pmu_event_attrs[] = {
	ARM_CSPMU_EVENT_ATTR(rd_bytes_loc,			0x0),
	ARM_CSPMU_EVENT_ATTR(rd_req_loc,			0x6),
	ARM_CSPMU_EVENT_ATTR(rd_cum_outs_loc,			0xc),

	ARM_CSPMU_EVENT_ATTR(cycles, ARM_CSPMU_EVT_CYCLES_DEFAULT),

	NULL,
};

static struct attribute *ucf_gpu_pmu_event_attrs[] = {
	ARM_CSPMU_EVENT_ATTR(rd_bytes_loc_rem,			0x0),
	ARM_CSPMU_EVENT_ATTR(wr_bytes_loc,			0x2),
	ARM_CSPMU_EVENT_ATTR(wr_bytes_rem,			0x3),
	ARM_CSPMU_EVENT_ATTR(rd_req_loc_rem,			0x6),
	ARM_CSPMU_EVENT_ATTR(wr_req_loc,			0x8),
	ARM_CSPMU_EVENT_ATTR(wr_req_rem,			0x9),
	ARM_CSPMU_EVENT_ATTR(rd_cum_outs_loc_rem,		0xc),

	ARM_CSPMU_EVENT_ATTR(cycles, ARM_CSPMU_EVT_CYCLES_DEFAULT),

	NULL,
};

static struct attribute *generic_pmu_event_attrs[] = {
	ARM_CSPMU_EVENT_ATTR(cycles, ARM_CSPMU_EVT_CYCLES_DEFAULT),
	NULL,
};

static struct attribute *scf_pmu_format_attrs[] = {
	ARM_CSPMU_FORMAT_EVENT_ATTR,
	NULL,
};

static struct attribute *pcie_pmu_format_attrs[] = {
	ARM_CSPMU_FORMAT_EVENT_ATTR,
	ARM_CSPMU_FORMAT_ATTR(root_port, "config1:0-9"),
	NULL,
};

static struct attribute *nvlink_c2c_pmu_format_attrs[] = {
	ARM_CSPMU_FORMAT_EVENT_ATTR,
	NULL,
};

static struct attribute *cnvlink_pmu_format_attrs[] = {
	ARM_CSPMU_FORMAT_EVENT_ATTR,
	ARM_CSPMU_FORMAT_ATTR(rem_socket, "config1:0-3"),
	NULL,
};

static struct attribute *ucf_pmu_format_attrs[] = {
	ARM_CSPMU_FORMAT_EVENT_ATTR,
	ARM_CSPMU_FORMAT_ATTR(src_loc_noncpu, "config1:0"),
	ARM_CSPMU_FORMAT_ATTR(src_loc_cpu, "config1:1"),
	ARM_CSPMU_FORMAT_ATTR(src_rem, "config1:2"),
	ARM_CSPMU_FORMAT_ATTR(dst_loc, "config1:3"),
	ARM_CSPMU_FORMAT_ATTR(dst_rem, "config1:4"),
	NULL,
};

static struct attribute *display_pmu_format_attrs[] = {
	ARM_CSPMU_FORMAT_EVENT_ATTR,
	NULL,
};

static struct attribute *ucf_gpu_pmu_format_attrs[] = {
	ARM_CSPMU_FORMAT_EVENT_ATTR,
	NULL,
};

static struct attribute *uphy_pmu_format_attrs[] = {
	ARM_CSPMU_FORMAT_EVENT_ATTR,
	ARM_CSPMU_FORMAT_ATTR(pcie_rp_1, "config1:0"),
	ARM_CSPMU_FORMAT_ATTR(pcie_rp_2, "config1:1"),
	ARM_CSPMU_FORMAT_ATTR(pcie_rp_3, "config1:2"),
	ARM_CSPMU_FORMAT_ATTR(pcie_rp_4, "config1:3"),
	ARM_CSPMU_FORMAT_ATTR(pcie_rp_5, "config1:4"),
	ARM_CSPMU_FORMAT_ATTR(xusb, "config1:5-10"),
	ARM_CSPMU_FORMAT_ATTR(mgbe_0, "config1:11"),
	ARM_CSPMU_FORMAT_ATTR(mgbe_1, "config1:12"),
	ARM_CSPMU_FORMAT_ATTR(mgbe_2, "config1:13"),
	ARM_CSPMU_FORMAT_ATTR(mgbe_3, "config1:14"),
	ARM_CSPMU_FORMAT_ATTR(eqos, "config1:15"),
	ARM_CSPMU_FORMAT_ATTR(ufs, "config1:16"),
	NULL,
};

static struct attribute *vision_pmu_format_attrs[] = {
	ARM_CSPMU_FORMAT_EVENT_ATTR,
	ARM_CSPMU_FORMAT_ATTR(vi_0, "config1:0-1"),
	ARM_CSPMU_FORMAT_ATTR(vi_1, "config1:2-3"),
	ARM_CSPMU_FORMAT_ATTR(isp_0, "config1:4-7"),
	ARM_CSPMU_FORMAT_ATTR(isp_1, "config1:8-11"),
	ARM_CSPMU_FORMAT_ATTR(vic, "config1:12-13"),
	ARM_CSPMU_FORMAT_ATTR(pva, "config1:14-19"),
	NULL,
};

static struct attribute *generic_pmu_format_attrs[] = {
	ARM_CSPMU_FORMAT_EVENT_ATTR,
	ARM_CSPMU_FORMAT_FILTER_ATTR,
	NULL,
};

static struct attribute **
nv_cspmu_get_event_attrs(const struct arm_cspmu *cspmu)
{
	const struct nv_cspmu_ctx *ctx = to_nv_cspmu_ctx(cspmu);

	return ctx->event_attr;
}

static struct attribute **
nv_cspmu_get_format_attrs(const struct arm_cspmu *cspmu)
{
	const struct nv_cspmu_ctx *ctx = to_nv_cspmu_ctx(cspmu);

	return ctx->format_attr;
}

static const char *
nv_cspmu_get_name(const struct arm_cspmu *cspmu)
{
	const struct nv_cspmu_ctx *ctx = to_nv_cspmu_ctx(cspmu);

	return ctx->name;
}

static u32 nv_cspmu_event_filter(const struct perf_event *event)
{
	const struct nv_cspmu_ctx *ctx =
		to_nv_cspmu_ctx(to_arm_cspmu(event->pmu));

	if (ctx->filter_mask == 0 || event->attr.config1 == 0)
		return ctx->filter_default_val;

	return event->attr.config1 & ctx->filter_mask;
}

/*
 * UCF leakage workaround:
 * Disables PMCR and PMCNTEN for each counter before running a
 * dummy experiment. This clears the internal state and prevents
 * event leakage from the previous experiment. PMCNTEN is then
 * re-enabled.
 */
static void ucf_pmu_stop_counters_leakage(struct arm_cspmu *cspmu)
{
	int reg_id;
	u32 cntenclr_offset = PMCNTENCLR;
	u32 cntenset_offset = PMCNTENSET;
	struct nv_cspmu_ctx *ctx = to_nv_cspmu_ctx(cspmu);

	/* Step 1: Disable PMCR.E */
	writel(0, cspmu->base0 + PMCR);

	/* Step 2: Clear PMCNTEN for all counters */
	for (reg_id = 0; reg_id < cspmu->num_set_clr_reg; ++reg_id) {
		ctx->pmcnten[reg_id] = readl(cspmu->base0 + cntenclr_offset);
		writel(ctx->pmcnten[reg_id], cspmu->base0 + cntenclr_offset);
		cntenclr_offset += sizeof(u32);
	}

	/* Step 3: Enable PMCR.E */
	writel(PMCR_E, cspmu->base0 + PMCR);

	/* Step 4: Disable PMCR.E */
	writel(0, cspmu->base0 + PMCR);

	/* Step 5: Enable back PMCNTEN for counters cleared in step 2 */
	for (reg_id = 0; reg_id < cspmu->num_set_clr_reg; ++reg_id) {
		writel(ctx->pmcnten[reg_id], cspmu->base0 + cntenset_offset);
		cntenset_offset += sizeof(u32);
	}
}

enum nv_cspmu_name_fmt {
	NAME_FMT_GENERIC,
	NAME_FMT_SOCKET
};

struct nv_cspmu_match {
	u32 prodid;
	u32 prodid_mask;
	u64 filter_mask;
	u32 filter_default_val;
	const char *name_pattern;
	enum nv_cspmu_name_fmt name_fmt;
	struct attribute **event_attr;
	struct attribute **format_attr;
	void (*stop_counters)(struct arm_cspmu *cspmu);
};

static const struct nv_cspmu_match nv_cspmu_match[] = {
	{
	  .prodid = 0x10300000,
	  .prodid_mask = NV_PRODID_MASK,
	  .filter_mask = NV_PCIE_FILTER_ID_MASK,
	  .filter_default_val = NV_PCIE_FILTER_ID_MASK,
	  .name_pattern = "nvidia_pcie_pmu_%u",
	  .name_fmt = NAME_FMT_SOCKET,
	  .event_attr = mcf_pmu_event_attrs,
	  .format_attr = pcie_pmu_format_attrs
	},
	{
	  .prodid = 0x10400000,
	  .prodid_mask = NV_PRODID_MASK,
	  .filter_mask = 0x0,
	  .filter_default_val = NV_NVL_C2C_FILTER_ID_MASK,
	  .name_pattern = "nvidia_nvlink_c2c1_pmu_%u",
	  .name_fmt = NAME_FMT_SOCKET,
	  .event_attr = mcf_pmu_event_attrs,
	  .format_attr = nvlink_c2c_pmu_format_attrs
	},
	{
	  .prodid = 0x10500000,
	  .prodid_mask = NV_PRODID_MASK,
	  .filter_mask = 0x0,
	  .filter_default_val = NV_NVL_C2C_FILTER_ID_MASK,
	  .name_pattern = "nvidia_nvlink_c2c0_pmu_%u",
	  .name_fmt = NAME_FMT_SOCKET,
	  .event_attr = mcf_pmu_event_attrs,
	  .format_attr = nvlink_c2c_pmu_format_attrs
	},
	{
	  .prodid = 0x10600000,
	  .prodid_mask = NV_PRODID_MASK,
	  .filter_mask = NV_CNVL_FILTER_ID_MASK,
	  .filter_default_val = NV_CNVL_FILTER_ID_MASK,
	  .name_pattern = "nvidia_cnvlink_pmu_%u",
	  .name_fmt = NAME_FMT_SOCKET,
	  .event_attr = mcf_pmu_event_attrs,
	  .format_attr = cnvlink_pmu_format_attrs
	},
	{
	  .prodid = 0x2CF00000,
	  .prodid_mask = NV_PRODID_MASK,
	  .filter_mask = 0x0,
	  .filter_default_val = 0x0,
	  .name_pattern = "nvidia_scf_pmu_%u",
	  .name_fmt = NAME_FMT_SOCKET,
	  .event_attr = scf_pmu_event_attrs,
	  .format_attr = scf_pmu_format_attrs
	},
	{
	  .prodid = 0x2CF10000,
	  .prodid_mask = NV_PRODID_MASK,
	  .filter_mask = NV_UCF_FILTER_ID_MASK,
	  .filter_default_val = NV_UCF_FILTER_ID_MASK,
	  .name_pattern = "nvidia_ucf_pmu_%u",
	  .name_fmt = NAME_FMT_SOCKET,
	  .event_attr = ucf_pmu_event_attrs,
	  .format_attr = ucf_pmu_format_attrs,
	  .stop_counters = ucf_pmu_stop_counters_leakage
	},
	{
	  .prodid = 0x10800000,
	  .prodid_mask = NV_PRODID_MASK,
	  .filter_mask = NV_UPHY_FILTER_ID_MASK,
	  .filter_default_val = NV_UPHY_FILTER_ID_MASK,
	  .name_pattern = "nvidia_uphy_pmu_%u",
	  .name_fmt = NAME_FMT_SOCKET,
	  .event_attr = mcf_pmu_event_attrs,
	  .format_attr = uphy_pmu_format_attrs
	},
	{
	  .prodid = 0x10a00000,
	  .prodid_mask = NV_PRODID_MASK,
	  .filter_mask = 0,
	  .filter_default_val = NV_UCF_GPU_FILTER_ID_MASK,
	  .name_pattern = "nvidia_ucf_gpu_pmu_%u",
	  .name_fmt = NAME_FMT_SOCKET,
	  .event_attr = ucf_gpu_pmu_event_attrs,
	  .format_attr = ucf_gpu_pmu_format_attrs
	},
	{
	  .prodid = 0x10d00000,
	  .prodid_mask = NV_PRODID_MASK,
	  .filter_mask = 0,
	  .filter_default_val = NV_DISPLAY_FILTER_ID_MASK,
	  .name_pattern = "nvidia_display_pmu_%u",
	  .name_fmt = NAME_FMT_SOCKET,
	  .event_attr = display_pmu_event_attrs,
	  .format_attr = display_pmu_format_attrs
	},
	{
	  .prodid = 0x10e00000,
	  .prodid_mask = NV_PRODID_MASK,
	  .filter_mask = NV_VISION_FILTER_ID_MASK,
	  .filter_default_val = NV_VISION_FILTER_ID_MASK,
	  .name_pattern = "nvidia_vision_pmu_%u",
	  .name_fmt = NAME_FMT_SOCKET,
	  .event_attr = mcf_pmu_event_attrs,
	  .format_attr = vision_pmu_format_attrs
	},
	{
	  .prodid = 0,
	  .prodid_mask = 0,
	  .filter_mask = NV_GENERIC_FILTER_ID_MASK,
	  .filter_default_val = NV_GENERIC_FILTER_ID_MASK,
	  .name_pattern = "nvidia_uncore_pmu_%u",
	  .name_fmt = NAME_FMT_GENERIC,
	  .event_attr = generic_pmu_event_attrs,
	  .format_attr = generic_pmu_format_attrs
	},
};

static char *nv_cspmu_format_name(const struct arm_cspmu *cspmu,
				  const struct nv_cspmu_match *match)
{
	char *name;
	struct device *dev = cspmu->dev;

	static atomic_t pmu_generic_idx = {0};

	switch (match->name_fmt) {
	case NAME_FMT_SOCKET: {
		const int cpu = cpumask_first(&cspmu->associated_cpus);
		const int socket = cpu_to_node(cpu);

		name = devm_kasprintf(dev, GFP_KERNEL, match->name_pattern,
				       socket);
		break;
	}
	case NAME_FMT_GENERIC:
		name = devm_kasprintf(dev, GFP_KERNEL, match->name_pattern,
				       atomic_fetch_inc(&pmu_generic_idx));
		break;
	default:
		name = NULL;
		break;
	}

	return name;
}

static int nv_cspmu_init_ops(struct arm_cspmu *cspmu)
{
	u32 prodid;
	struct nv_cspmu_ctx *ctx;
	struct device *dev = cspmu->dev;
	struct arm_cspmu_impl_ops *impl_ops = &cspmu->impl.ops;
	const struct nv_cspmu_match *match = nv_cspmu_match;

	ctx = devm_kzalloc(dev, sizeof(struct nv_cspmu_ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	prodid = cspmu->impl.pmiidr;

	/* Find matching PMU. */
	for (; match->prodid; match++) {
		const u32 prodid_mask = match->prodid_mask;

		if ((match->prodid & prodid_mask) == (prodid & prodid_mask))
			break;
	}

	ctx->name		= nv_cspmu_format_name(cspmu, match);
	ctx->filter_mask	= match->filter_mask;
	ctx->filter_default_val = match->filter_default_val;
	ctx->event_attr		= match->event_attr;
	ctx->format_attr	= match->format_attr;

	cspmu->impl.ctx = ctx;

	/* NVIDIA specific callbacks. */
	impl_ops->event_filter			= nv_cspmu_event_filter;
	impl_ops->get_event_attrs		= nv_cspmu_get_event_attrs;
	impl_ops->get_format_attrs		= nv_cspmu_get_format_attrs;
	impl_ops->get_name			= nv_cspmu_get_name;
	if (match->stop_counters != NULL) {
		ctx->pmcnten = devm_kzalloc(dev, cspmu->num_set_clr_reg *
					     sizeof(u32), GFP_KERNEL);
		if (!ctx->pmcnten)
			return -ENOMEM;
		impl_ops->stop_counters		= match->stop_counters;
	}

	return 0;
}

/* Match all NVIDIA Coresight PMU devices */
static const struct arm_cspmu_impl_match nv_cspmu_param = {
	.pmiidr_val	= ARM_CSPMU_IMPL_ID_NVIDIA,
	.module		= THIS_MODULE,
	.impl_init_ops	= nv_cspmu_init_ops
};

static int __init nvidia_cspmu_init(void)
{
	int ret;

	ret = arm_cspmu_impl_register(&nv_cspmu_param);
	if (ret)
		pr_err("nvidia_cspmu backend registration error: %d\n", ret);

	return ret;
}

static void __exit nvidia_cspmu_exit(void)
{
	arm_cspmu_impl_unregister(&nv_cspmu_param);
}

module_init(nvidia_cspmu_init);
module_exit(nvidia_cspmu_exit);

MODULE_LICENSE("GPL v2");
