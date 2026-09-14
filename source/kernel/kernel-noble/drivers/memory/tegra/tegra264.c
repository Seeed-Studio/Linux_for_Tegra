// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
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
		.id = TEGRA264_MEMORY_CLIENT_PTCR,
		.name = "ptcr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_HOST1XR,
		.name = "host1xr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MPCORER,
		.name = "mpcorer",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PSCR,
		.name = "pscr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PSCW,
		.name = "pscw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_ISP0R,
		.name = "isp0r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MPCOREW,
		.name = "mpcorew",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_ISP0W,
		.name = "isp0w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_ISP1W,
		.name = "isp1w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_ISPFALCONR,
		.name = "ispfalconr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_ISPFALCONW,
		.name = "ispfalconw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MGBE2R,
		.name = "mgbe2r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_OFAR2MC,
		.name = "ofar2mc",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_OFAW2MC,
		.name = "ofaw2mc",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MGBE2W,
		.name = "mgbe2w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MGBE3R,
		.name = "mgbe3r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MGBE3W,
		.name = "mgbe3w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SEU1RD,
		.name = "seu1rd",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SEU1WR,
		.name = "seu1wr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_VICR,
		.name = "vicr",
		.bpmp_id = TEGRA264_BWMGR_VIC,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_VICW,
		.name = "vicw",
		.bpmp_id = TEGRA264_BWMGR_VIC,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_VIW,
		.name = "viw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_XSPI0R,
		.name = "xspi0r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_XSPI0W,
		.name = "xspi0w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_APER,
		.name = "aper",
		.bpmp_id = TEGRA264_BWMGR_APE,
		.type = TEGRA_ICC_ISO_AUDIO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_APEW,
		.name = "apew",
		.bpmp_id = TEGRA264_BWMGR_APE,
		.type = TEGRA_ICC_ISO_AUDIO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SER,
		.name = "ser",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SEW,
		.name = "sew",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_AXIAPR,
		.name = "axiapr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_AXIAPW,
		.name = "axiapw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_ETRR,
		.name = "etrr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_ETRW,
		.name = "etrw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_TSECR,
		.name = "tsecr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_TSECW,
		.name = "tsecw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_BPMPR,
		.name = "bpmpr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_BPMPW,
		.name = "bpmpw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_AONR,
		.name = "aonr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_AONW,
		.name = "aonw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_GPCDMAR,
		.name = "gpcdmar",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_GPCDMAW,
		.name = "gpcdmaw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_APEDMAR,
		.name = "apedmar",
		.bpmp_id = TEGRA264_BWMGR_APEDMA,
		.type = TEGRA_ICC_ISO_AUDIO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_APEDMAW,
		.name = "apedmaw",
		.bpmp_id = TEGRA264_BWMGR_APEDMA,
		.type = TEGRA_ICC_ISO_AUDIO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MIU0R,
		.name = "miu0r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MIU0W,
		.name = "miu0w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MIU1R,
		.name = "miu1r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MIU1W,
		.name = "miu1w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MIU2R,
		.name = "miu2r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MIU2W,
		.name = "miu2w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MIU3R,
		.name = "miu3r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MIU3W,
		.name = "miu3w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MIU4R,
		.name = "miu4r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MIU4W,
		.name = "miu4w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_VIFALCONR,
		.name = "vifalconr",
		.bpmp_id = TEGRA264_BWMGR_VIFAL,
		.type = TEGRA_ICC_ISO_VIFAL,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_VIFALCONW,
		.name = "vifalconw",
		.bpmp_id = TEGRA264_BWMGR_VIFAL,
		.type = TEGRA_ICC_ISO_VIFAL,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_RCER,
		.name = "rcer",
		.bpmp_id = TEGRA264_BWMGR_RCE,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_RCEW,
		.name = "rcew",
		.bpmp_id = TEGRA264_BWMGR_RCE,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_NVENC1SRD2MC,
		.name = "nvenc1srd2mc",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_NVENC1SWR2MC,
		.name = "nvenc1swr2mc",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE0W,
		.name = "pcie0w",
		.bpmp_id = TEGRA264_BWMGR_PCIE_0,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE1R,
		.name = "pcie1r",
		.bpmp_id = TEGRA264_BWMGR_PCIE_1,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE1W,
		.name = "pcie1w",
		.bpmp_id = TEGRA264_BWMGR_PCIE_1,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE2AR,
		.name = "pcie2ar",
		.bpmp_id = TEGRA264_BWMGR_PCIE_2,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE2AW,
		.name = "pcie2aw",
		.bpmp_id = TEGRA264_BWMGR_PCIE_2,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE3R,
		.name = "pcie3r",
		.bpmp_id = TEGRA264_BWMGR_PCIE_3,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE3W,
		.name = "pcie3w",
		.bpmp_id = TEGRA264_BWMGR_PCIE_3,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE4R,
		.name = "pcie4r",
		.bpmp_id = TEGRA264_BWMGR_PCIE_4,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE4W,
		.name = "pcie4w",
		.bpmp_id = TEGRA264_BWMGR_PCIE_4,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE5R,
		.name = "pcie5r",
		.bpmp_id = TEGRA264_BWMGR_PCIE_5,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE5W,
		.name = "pcie5w",
		.bpmp_id = TEGRA264_BWMGR_PCIE_5,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_GPUR02MC,
		.name = "gpur02mc",
		.bpmp_id = TEGRA264_BWMGR_GPU,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_GPUW02MC,
		.name = "gpuw02mc",
		.bpmp_id = TEGRA264_BWMGR_GPU,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_NVDECSRD2MC,
		.name = "nvdecsrd2mc",
		.bpmp_id = TEGRA264_BWMGR_NVDEC,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_NVDECSWR2MC,
		.name = "nvdecswr2mc",
		.bpmp_id = TEGRA264_BWMGR_NVDEC,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MIU5R,
		.name = "miu5r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MIU5W,
		.name = "miu5w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MIU6W,
		.name = "miu6w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_RISTR,
		.name = "ristr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_RISTW,
		.name = "ristw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_OESPR,
		.name = "oespr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_OESPW,
		.name = "oespw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MIU7W,
		.name = "miu7w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MIU8R,
		.name = "miu8r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MIU8W,
		.name = "miu8w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MIU9R,
		.name = "miu9r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MIU9W,
		.name = "miu9w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PMA0AWR,
		.name = "pma0awr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_NVJPG1SRD2MC,
		.name = "nvjpg1srd2mc",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_NVJPG1SWR2MC,
		.name = "nvjpg1swr2mc",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU0CTWR,
		.name = "smmu0ctwr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU0CMDQVR,
		.name = "smmu0cmdqvr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU0CMDQVW,
		.name = "smmu0cmdqvw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU0EVNTQW,
		.name = "smmu0evntqw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU1PTWR,
		.name = "smmu1ptwr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU1CTWR,
		.name = "smmu1ctwr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU1CMDQVR,
		.name = "smmu1cmdqvr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU1CMDQVW,
		.name = "smmu1cmdqvw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU1EVNTQW,
		.name = "smmu1evntqw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU2PTWR,
		.name = "smmu2ptwr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU2CTWR,
		.name = "smmu2ctwr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU2CMDQVR,
		.name = "smmu2cmdqvr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU2CMDQVW,
		.name = "smmu2cmdqvw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU2EVNTQW,
		.name = "smmu2evntqw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU0CMDQR,
		.name = "smmu0cmdqr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU1CMDQR,
		.name = "smmu1cmdqr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU2CMDQR,
		.name = "smmu2cmdqr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_APE1R,
		.name = "ape1r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_APE1W,
		.name = "ape1w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_UFSR,
		.name = "ufsr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_UFSW,
		.name = "ufsw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_XUSB_DEVR,
		.name = "xusb_devr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_XUSB_DEVW,
		.name = "xusb_devw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_XUSB_DEV1R,
		.name = "xusb_dev1r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_XUSB_DEV2W,
		.name = "xusb_dev2w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_XUSB_DEV3R,
		.name = "xusb_dev3r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_XUSB_DEV3W,
		.name = "xusb_dev3w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_XUSB_DEV4R,
		.name = "xusb_dev4r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_XUSB_DEV4W,
		.name = "xusb_dev4w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_XUSB_DEV5R,
		.name = "xusb_dev5r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_XUSB_DEV5W,
		.name = "xusb_dev5w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_DCER,
		.name = "dcer",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_DCEW,
		.name = "dcew",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_HDAR,
		.name = "hdar",
		.bpmp_id = TEGRA264_BWMGR_HDA,
		.type = TEGRA_ICC_ISO_AUDIO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_HDAW,
		.name = "hdaw",
		.bpmp_id = TEGRA264_BWMGR_HDA,
		.type = TEGRA_ICC_ISO_AUDIO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_DISPNISOR,
		.name = "dispnisor",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_DISPNISOW,
		.name = "dispnisow",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_XUSB_DEV1W,
		.name = "xusb_dev1w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_XUSB_DEV2R,
		.name = "xusb_dev2r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_DISPR,
		.name = "dispr",
		.bpmp_id = TEGRA264_BWMGR_DISPLAY,
		.type = TEGRA_ICC_ISO_DISPLAY,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MSSSEQR,
		.name = "mssseqr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MSSSEQW,
		.name = "mssseqw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU3PTWR,
		.name = "smmu3ptwr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU3CTWR,
		.name = "smmu3ctwr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU3CMDQVR,
		.name = "smmu3cmdqvr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU3CMDQVW,
		.name = "smmu3cmdqvw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU3EVNTQW,
		.name = "smmu3evntqw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU3CMDQR,
		.name = "smmu3cmdqr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU4PTWR,
		.name = "smmu4ptwr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU4CTWR,
		.name = "smmu4ctwr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU4CMDQVR,
		.name = "smmu4cmdqvr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU4CMDQVW,
		.name = "smmu4cmdqvw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU4EVNTQW,
		.name = "smmu4evntqw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SMMU4CMDQR,
		.name = "smmu4cmdqr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MGBE0R,
		.name = "mgbe0r",
		.bpmp_id = TEGRA264_BWMGR_EQOS,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MGBE0W,
		.name = "mgbe0w",
		.bpmp_id = TEGRA264_BWMGR_EQOS,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MGBE1R,
		.name = "mgbe1r",
		.bpmp_id = TEGRA264_BWMGR_EQOS,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MGBE1W,
		.name = "mgbe1w",
		.bpmp_id = TEGRA264_BWMGR_EQOS,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_VI1W,
		.name = "vi1w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_VIFALCON1R,
		.name = "vifalcon1r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_VIFALCON1W,
		.name = "vifalcon1w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_ISPFALCON1R,
		.name = "ispfalcon1r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_ISPFALCON1W,
		.name = "ispfalcon1w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_RCE1R,
		.name = "rce1r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_RCE1W,
		.name = "rce1w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SEU2R,
		.name = "seu2r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SEU2W,
		.name = "seu2w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SEU3R,
		.name = "seu3r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SEU3W,
		.name = "seu3w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PVA0R,
		.name = "pva0r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PVA0W,
		.name = "pva0w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PVA1R,
		.name = "pva1r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PVA1W,
		.name = "pva1w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PVA2R,
		.name = "pva2r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PVA2W,
		.name = "pva2w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_ISP3W,
		.name = "isp3w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_ISP2R,
		.name = "isp2r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_ISP2W,
		.name = "isp2w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_EQOSR,
		.name = "eqosr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_EQOSW,
		.name = "eqosw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_FSI0R,
		.name = "fsi0r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_FSI0W,
		.name = "fsi0w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_FSI1R,
		.name = "fsi1r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_FSI1W,
		.name = "fsi1w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SDMMC0R,
		.name = "sdmmc0r",
		.bpmp_id = TEGRA264_BWMGR_SDMMC_1,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SDMMC0W,
		.name = "sdmmc0w",
		.bpmp_id = TEGRA264_BWMGR_SDMMC_1,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SBR,
		.name = "sbr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SBW,
		.name = "sbw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_HSS_MIU0R,
		.name = "hss_miu0r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_HSS_MIU0W,
		.name = "hss_miu0w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_HSS_MIU1R,
		.name = "hss_miu1r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_HSS_MIU1W,
		.name = "hss_miu1w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_HSS_MIU2R,
		.name = "hss_miu2r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_HSS_MIU2W,
		.name = "hss_miu2w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_HSS_MIU3R,
		.name = "hss_miu3r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_HSS_MIU3W,
		.name = "hss_miu3w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_HSS_MIU4R,
		.name = "hss_miu4r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_HSS_MIU4W,
		.name = "hss_miu4w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_HSS_MIU5R,
		.name = "hss_miu5r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_HSS_MIU5W,
		.name = "hss_miu5w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_HSS_MIU6R,
		.name = "hss_miu6r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_HSS_MIU6W,
		.name = "hss_miu6w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_HSS_MIU7R,
		.name = "hss_miu7r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_HSS_MIU7W,
		.name = "hss_miu7w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_GMMUR2MC,
		.name = "gmmur2mc",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_UCFELAR,
		.name = "ucfelar",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_UCFELAW,
		.name = "ucfelaw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SLCR,
		.name = "slcr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SLCW,
		.name = "slcw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_REMOTER,
		.name = "remoter",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_REMOTEW,
		.name = "remotew"
	},
};

static const char *const tegra264_mc_status_names[32] = {
	[6] = "EMEM address decode error",
	[8] = "Security violation",
	[12] = "VPR violation",
	[13] = "Secure carveout violation",
	[16] = "MTS carveout violation",
	[17] = "Generalized carveout violation",
	[20] = "Route Sanity error",
	[21] = "GIC_MSI error",
};

static const char *const tegra_hub_status_names[32] = {
	[0] = "coalescer error",
	[1] = "SMMU BYPASS ALLOW error",
	[2] = "Illegal tbugrp_id error",
	[3] = "Malformed MSI request error",
	[4] = "Read response with poison bit error",
	[5] = "Restricted access violation error",
	[6] = "Reserved PA error",
};

static const char *const tegra264_mc_error_names[4] = {
	[1] = "EMEM decode error",
	[2] = "TrustZone violation",
	[3] = "Carveout violation",
};

static const char *const tegra_rt_error_names[16] = {
	[1] = "DECERR_PARTIAL_POPULATED",
	[2] = "DECERR_SMMU_BYPASS",
	[3] = "DECERR_INVALID_MMIO",
	[4] = "DECERR_INVALID_GIC_MSI",
	[5] = "DECERR_ATOMIC_SYSRAM",
	[9] = "DECERR_REMOTE_REQ_PRE_BOOT",
	[10] = "DECERR_ISO_OVER_C2C",
	[11] = "DECERR_UNSUPPORTED_SBS_OPCODE",
	[12] = "DECERR_SBS_REQ_OVER_SISO_LL",
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

static void mcf_log_fault(struct tegra_mc *mc, u32 channel, unsigned long mcf_ch_intstatus)
{
	unsigned int bit;

	for_each_set_bit(bit, &mcf_ch_intstatus, 32) {
		const char *error = tegra264_mc_status_names[bit] ?: "unknown";
		u32 intmask = BIT(bit);
		u32 status_reg, status1_reg = 0, addr_reg, addr_hi_reg = 0;
		u32 addr_val, value, client_id, i, addr_hi_shift = 0, addr_hi_mask = 0, status1;
		const char *direction, *secure;
		const char *client = "unknown", *desc = "NA";
		phys_addr_t addr = 0;
		bool is_gsc = false, err_type_valid = false, err_rt_type_valid = false;
		u8 type;
		u32 mc_rw_bit = MC_ERR_STATUS_RW, mc_sec_bit = MC_ERR_STATUS_SECURITY;

		switch (intmask) {
		case MC_INT_DECERR_EMEM:
			status_reg = mc->soc->mc_regs->mc_err_status_reg;
			addr_reg = mc->soc->mc_regs->mc_err_add_reg;
			addr_hi_reg = mc->soc->mc_regs->mc_err_add_hi_reg;
			err_type_valid = true;
			break;
		case MC_INT_SECURITY_VIOLATION:
			status_reg = mc->soc->mc_regs->mc_err_status_reg;
			addr_reg = mc->soc->mc_regs->mc_err_add_reg;
			addr_hi_reg = mc->soc->mc_regs->mc_err_add_hi_reg;
			err_type_valid = true;
			break;
		case MC_INT_DECERR_VPR:
			status_reg = mc->soc->mc_regs->mc_err_vpr_status_reg;
			addr_reg = mc->soc->mc_regs->mc_err_vpr_add_reg;
			addr_hi_shift = MC_ERR_STATUS_ADR_HI_SHIFT;
			addr_hi_mask = mc->soc->mc_regs->mc_addr_hi_mask;
			break;
		case MC_INT_SECERR_SEC:
			status_reg = mc->soc->mc_regs->mc_err_sec_status_reg;
			addr_reg = mc->soc->mc_regs->mc_err_sec_add_reg;
			addr_hi_shift = MC_ERR_STATUS_ADR_HI_SHIFT;
			addr_hi_mask = mc->soc->mc_regs->mc_addr_hi_mask;
			break;
		case MC_INT_DECERR_MTS:
			status_reg = mc->soc->mc_regs->mc_err_mts_status_reg;
			addr_reg = mc->soc->mc_regs->mc_err_mts_add_reg;
			addr_hi_shift = MC_ERR_STATUS_ADR_HI_SHIFT;
			addr_hi_mask = mc->soc->mc_regs->mc_addr_hi_mask;
			break;
		case MC_INT_DECERR_GENERALIZED_CARVEOUT:
			status_reg = mc->soc->mc_regs->mc_err_gen_co_status_reg;
			status1_reg = MC_ERR_GENERALIZED_CARVEOUT_STATUS_1_0;
			addr_reg = mc->soc->mc_regs->mc_err_gen_co_add_reg;
			addr_hi_shift = MC_ERR_STATUS_ADR_HI_SHIFT_GSC;
			addr_hi_mask = MC_ERR_STATUS_ADR_HI_MASK_GSC;
			is_gsc = true;
			break;
		case MC_INT_DECERR_ROUTE_SANITY:
			status_reg = mc->soc->mc_regs->mc_err_route_status_reg;
			addr_reg = mc->soc->mc_regs->mc_err_route_add_reg;
			addr_hi_shift = MC_ERR_STATUS_ADR_HI_SHIFT_RT;
			addr_hi_mask = mc->soc->mc_regs->mc_addr_hi_mask;
			mc_sec_bit = MC_ERR_ROUTE_SANITY_SEC;
			mc_rw_bit = MC_ERR_ROUTE_SANITY_RW;
			err_rt_type_valid = true;
			break;
		case MC_INT_DECERR_ROUTE_SANITY_GIC_MSI:
			status_reg = mc->soc->mc_regs->mc_err_route_status_reg;
			addr_reg = mc->soc->mc_regs->mc_err_route_add_reg;
			addr_hi_shift = MC_ERR_STATUS_ADR_HI_SHIFT_RT;
			addr_hi_mask = mc->soc->mc_regs->mc_addr_hi_mask;
			mc_sec_bit = MC_ERR_ROUTE_SANITY_SEC;
			mc_rw_bit = MC_ERR_ROUTE_SANITY_RW;
			err_rt_type_valid = true;
			break;
		default:
			dev_err_ratelimited(mc->dev, "Incorrect MC interrupt mask\n");
			break;
		}
		value = mc_ch_readl(mc, channel, status_reg);
		if (addr_hi_reg) {
			addr = mc_ch_readl(mc, channel, addr_hi_reg);
		} else {
			if (!is_gsc) {
				addr = ((value >> addr_hi_shift) & addr_hi_mask);
			} else {
				status1 = mc_ch_readl(mc, channel, status1_reg);
				addr = ((status1 >> addr_hi_shift) & addr_hi_mask);
			}
		}
		addr <<= 32;
		addr_val = mc_ch_readl(mc, channel, addr_reg);
		addr |= addr_val;

		if (value & mc_rw_bit)
			direction = "write";
		else
			direction = "read";

		if (value & mc_sec_bit)
			secure = "secure";
		else
			secure = "non-secure";

		client_id = value & mc->soc->client_id_mask;
		for (i = 0; i < mc->soc->num_clients; i++) {
			if (mc->soc->clients[i].id == client_id) {
				client = mc->soc->clients[i].name;
				break;
			}
		}

		if (err_type_valid) {
			type = (value & mc->soc->mc_regs->mc_err_status_type_mask) >>
					MC_ERR_STATUS_TYPE_SHIFT;
			desc = tegra264_mc_error_names[type];
		} else if (err_rt_type_valid) {
			type = (value & MC_ERR_STATUS_TYPE_MASK_RT) >>
					MC_ERR_STATUS_TYPE_SHIFT_RT;
			desc = tegra_rt_error_names[type];
		}

		dev_err_ratelimited(mc->dev, "%s: %s %s @%pa: %s (%s)\n",
							client, secure, direction, &addr, error,
							desc);
		if (is_gsc) {
			dev_err_ratelimited(mc->dev, "gsc_apr_id=%u gsc_co_apr_id=%u\n",
					((status1 >> ERR_GENERALIZED_APERTURE_ID_SHIFT)
					& ERR_GENERALIZED_APERTURE_ID_MASK),
					((status1 >> ERR_GENERALIZED_CARVEOUT_APERTURE_ID_SHIFT)
					& ERR_GENERALIZED_CARVEOUT_APERTURE_ID_MASK));
		}
	}

	/* clear interrupts */
	mc_ch_writel(mc, channel, mcf_ch_intstatus, MCF_INTSTATUS_0);
}

static irqreturn_t handle_mcf_irq(int irq, void *data)
{
	struct tegra_mc *mc = data;
	unsigned long mcf_common_intstat, mcf_intstatus;
	unsigned int slice;

	/* Read MCF_COMMON_INTSTATUS0_0_0 from MCB block */
	mcf_common_intstat = mc_ch_readl(mc, MC_BROADCAST_CHANNEL, MCF_COMMON_INTSTATUS0_0_0);
	if (mcf_common_intstat == 0) {
		dev_err(mc->dev, "No interrupt in MCF\n");
		return IRQ_NONE;
	}

	for_each_set_bit(slice, &mcf_common_intstat, 32) {
		/* Find out the slice number on which interrupt occurred */
		if (slice > 4) {
			dev_err(mc->dev, "Invalid value in registeer MCF_COMMON_INTSTATUS0_0_0\n");
			return IRQ_NONE;
		}

		mcf_intstatus = mc_ch_readl(mc, slice, MCF_INTSTATUS_0);
		if (mcf_intstatus != 0)
			mcf_log_fault(mc, slice, mcf_intstatus);
	}

	return IRQ_HANDLED;
}

static void hub_log_fault(struct tegra_mc *mc, u32 hub, unsigned long hub_intstat)
{
	unsigned int bit;

	for_each_set_bit(bit, &hub_intstat, 32) {
		const char *error = tegra_hub_status_names[bit] ?: "unknown";
		u32 intmask = BIT(bit), client_id;
		const char *client = "unknown";
		u32 status_reg, addr_reg = 0, addr_hi_reg = 0;
		u32 value, addr_val, i;
		phys_addr_t addr = 0;

		switch (intmask) {
		case MSS_HUB_COALESCER_ERR_INTMASK:
			status_reg = MSS_HUB_RESERVED_PA_ERR_STATUS_0;
			break;
		case MSS_HUB_SMMU_BYPASS_ALLOW_ERR_INTMASK:
			status_reg = MSS_HUB_RESTRICTED_ACCESS_ERR_STATUS_0;
			break;
		case MSS_HUB_ILLEGAL_TBUGRP_ID_INTMASK:
			status_reg = MSS_HUB_POISON_RSP_STATUS_0;
			break;
		case MSS_HUB_MSI_ERR_INTMASK:
			status_reg = MSS_HUB_MSI_ERR_STATUS_0;
			break;
		case MSS_HUB_POISON_RSP_INTMASK:
			status_reg = MSS_HUB_ILLEGAL_TBUGRP_ID_ERR_STATUS_0;
			break;
		case MSS_HUB_RESTRICTED_ACCESS_ERR_INTMASK:
			status_reg = MSS_HUB_SMMU_BYPASS_ALLOW_ERR_STATUS_0;
			break;
		case MSS_HUB_RESERVED_PA_ERR_INTMASK:
			status_reg = MSS_HUB_COALESCE_ERR_STATUS_0;
			addr_reg = MSS_HUB_COALESCE_ERR_ADR_0;
			addr_hi_reg = MSS_HUB_COALESCE_ERR_ADR_HI_0;
			break;
		default:
			dev_err_ratelimited(mc->dev, "Incorrect HUB interrupt mask\n");
			return;
		}

		value = mc_ch_readl(mc, hub, status_reg);
		if (addr_reg) {
			addr = mc_ch_readl(mc, hub, addr_hi_reg);
			addr <<= 32;
			addr_val = mc_ch_readl(mc, hub, addr_reg);
			addr |= addr_val;
		}

		client_id = value & mc->soc->client_id_mask;
		for (i = 0; i < mc->soc->num_clients; i++) {
			if (mc->soc->clients[i].id == client_id) {
				client = mc->soc->clients[i].name;
				break;
			}
		}

		dev_err_ratelimited(mc->dev, "%s: @%pa: %s status:%u\n",
							client, &addr, error, value);
	}

	/* clear interrupts */
	mc_ch_writel(mc, hub, hub_intstat, MSS_HUB_INTRSTATUS_0);
}

static irqreturn_t handle_hub_irq(int irq, void *data)
{
	struct tegra_mc *mc = data;
	unsigned long hub_global_intstat, hub_intstat, hub_interrupted = 0;
	unsigned int hub_gobal_mask = 0x7F00, hub_gobal_shift = 8, hub;

	/* Read MSS_HUB_GLOBAL_INTSTATUS_0 from MCB block */
	hub_global_intstat = mc_ch_readl(mc, MC_BROADCAST_CHANNEL, MSS_HUB_GLOBAL_INTSTATUS_0);
	if (hub_global_intstat == 0) {
		dev_err(mc->dev, "No interrupt in HUB/HUBC\n");
		return IRQ_NONE;
	}

	/* Handle interrupt from hubc */
	if (hub_global_intstat & MSS_HUBC_INTR) {
		/* Read MSS_HUB_HUBC_INTSTATUS_0 from block MCB */
		hub_intstat = mc_ch_readl(mc, MC_BROADCAST_CHANNEL, MSS_HUB_HUBC_INTSTATUS_0);
		if (hub_intstat != 0) {
			dev_err_ratelimited(mc->dev, "Scrubber operation status:%lu\n",
								hub_intstat);
			/* Clear hubc interrupt */
			mc_ch_writel(mc, MC_BROADCAST_CHANNEL, hub_intstat,
							MSS_HUB_HUBC_INTSTATUS_0);
		}
	}

	hub_interrupted = (hub_global_intstat & hub_gobal_mask) >> hub_gobal_shift;
	/* Handle interrupt from hub */
	for_each_set_bit(hub, &hub_interrupted, 32) {
		/* Read MSS_HUB_INTRSTATUS_0 from block MCi */
		hub_intstat = mc_ch_readl(mc, hub, MSS_HUB_INTRSTATUS_0);
		if (hub_intstat != 0)
			hub_log_fault(mc, hub, hub_intstat);
	}

	/* Clear global interrupt status register */
	mc_ch_writel(mc, MC_BROADCAST_CHANNEL, hub_global_intstat, MSS_HUB_GLOBAL_INTSTATUS_0);
	return IRQ_HANDLED;
}

static irqreturn_t handle_generic_irq(struct tegra_mc *mc, unsigned long intstat_reg)
{
	unsigned long intstat;
	unsigned int i;

	/* Iterate over all MC blocks to read INTSTATUS */
	for (i = 0; i < mc->num_channels; i++) {
		intstat = mc_ch_readl(mc, i, intstat_reg);
		if (intstat) {
			dev_err_ratelimited(mc->dev, "channel:%i status:%lu\n", i, intstat);
			/* Clear interrupt */
			mc_ch_writel(mc, i, intstat, intstat_reg);
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t handle_sbs_irq(int irq, void *data)
{
	return handle_generic_irq((struct tegra_mc *)data, MSS_SBS_INTSTATUS_0);
}

static irqreturn_t handle_channel_irq(int irq, void *data)
{
	return handle_generic_irq((struct tegra_mc *)data, MC_CH_INTSTATUS_0);
}

irq_handler_t tegra264_mc_irq_handlers[8] = {
				handle_mcf_irq, handle_hub_irq, handle_hub_irq,
				handle_hub_irq, handle_hub_irq, handle_hub_irq,
				handle_sbs_irq, handle_channel_irq};

const struct tegra_mc_ops tegra264_mc_ops = {
	.probe = tegra186_mc_probe,
	.remove = tegra186_mc_remove,
	.probe_device = tegra186_mc_probe_device,
	.resume = tegra186_mc_resume,
	.handle_irq = tegra264_mc_irq_handlers,
	.num_interrupts = ARRAY_SIZE(tegra264_mc_irq_handlers),
};

static const struct tegra_mc_icc_ops tegra264_mc_icc_ops = {
	.xlate = tegra_mc_icc_xlate,
	.aggregate = tegra264_mc_icc_aggregate,
	.get_bw = tegra264_mc_icc_get_init_bw,
	.set = tegra264_mc_icc_set,
};

static const struct tegra_mc_regs tegra264_mc_regs = {
	.mc_err_status_reg = 0xbc00,
	.mc_err_add_reg = 0xbc04,
	.mc_err_add_hi_reg = 0xbc08,
	.mc_err_vpr_status_reg = 0xbc20,
	.mc_err_vpr_add_reg = 0xbc24,
	.mc_err_sec_status_reg = 0xbc3c,
	.mc_err_sec_add_reg = 0xbc40,
	.mc_err_mts_status_reg = 0xbc5c,
	.mc_err_mts_add_reg = 0xbc60,
	.mc_err_gen_co_status_reg = 0xbc78,
	.mc_err_gen_co_add_reg = 0xbc7c,
	.mc_err_route_status_reg = 0xbc64,
	.mc_err_route_add_reg = 0xbc68,
	.mc_addr_hi_mask = 0xff,
	.mc_err_status_type_mask = (0x3 << 28),
};

const struct tegra_mc_soc tegra264_mc_soc = {
	.num_clients = ARRAY_SIZE(tegra264_mc_clients),
	.clients = tegra264_mc_clients,
	.num_address_bits = 40,
	.num_channels = 16,
	.cfg_channel_enable = T264_MC_EMEM_ADR_CFG_CHANNEL_ENABLE,
	.client_id_mask = 0x1ff,
	.has_addr_hi_reg = true,
	.ops = &tegra264_mc_ops,
	.icc_ops = &tegra264_mc_icc_ops,
	.ch_intmask = 0x0000ff00,
	.global_intstatus_channel_shift = 8,
	/*
	 * Additionally, there are lite carveouts but those are not currently
	 * supported.
	 */
	.num_carveouts = 32,
	.has_chiplet_arch = true,
	.mc_regs = &tegra264_mc_regs,
	.mcf_intmask = MC_INT_DECERR_ROUTE_SANITY_GIC_MSI |
			MC_INT_DECERR_ROUTE_SANITY |
			MC_INT_DECERR_GENERALIZED_CARVEOUT | MC_INT_DECERR_MTS |
			MC_INT_SECERR_SEC | MC_INT_DECERR_VPR |
			MC_INT_SECURITY_VIOLATION | MC_INT_DECERR_EMEM,
	.hub_intmask = MSS_HUB_COALESCER_ERR_INTMASK | MSS_HUB_SMMU_BYPASS_ALLOW_ERR_INTMASK |
			MSS_HUB_ILLEGAL_TBUGRP_ID_INTMASK | MSS_HUB_MSI_ERR_INTMASK |
			MSS_HUB_POISON_RSP_INTMASK | MSS_HUB_RESTRICTED_ACCESS_ERR_INTMASK |
			MSS_HUB_RESERVED_PA_ERR_INTMASK,
	.hubc_intmask = MSS_HUB_HUBC_SCRUB_DONE_INTMASK,
	.sbs_intmask = MSS_SBS_FILL_FIFO_ISO_OVERFLOW_INTMASK |
			MSS_SBS_FILL_FIFO_SISO_OVERFLOW_INTMASK |
			MSS_SBS_FILL_FIFO_NISO_OVERFLOW_INTMASK,
	.mc_ch_intmask = WCAM_ERR_INTMASK,
};