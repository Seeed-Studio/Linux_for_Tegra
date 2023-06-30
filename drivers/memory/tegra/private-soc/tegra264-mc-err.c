// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
 */

#define pr_fmt(fmt) "tegra264-mc-err: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/export.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/interrupt.h>
#include <dt-bindings/memory/tegra264-mc.h>

#define MC_BROADCAST_CHANNEL			~0

#define MCF_INTSTATUS_0				0xce2c
#define MC_ERR_STATUS_0				0x8700
#define MC_ERR_ADR_0				0x8704
#define MC_ERR_ADR_HI_0				0x8708
#define MC_ERR_VPR_STATUS_0			0x8720
#define MC_ERR_VPR_ADR_0			0x8724
#define MC_ERR_SEC_STATUS_0			0x873c
#define MC_ERR_SEC_ADR_0			0x8740
#define MC_ERR_MTS_STATUS_0			0x875c
#define MC_ERR_MTS_ADR_0			0x8760
#define MC_ERR_GENERALIZED_CARVEOUT_STATUS_0	0x8778
#define MC_ERR_GENERALIZED_CARVEOUT_ADR_0	0x877c
#define MC_ERR_ROUTE_SANITY_STATUS_0		0x8764
#define MC_ERR_ROUTE_SANITY_ADR_0		0x8768
#define MCF_INTMASK_0				0xce30
#define MCF_INTPRIORITY_0			0xce34


#define MC_INT_DECERR_ROUTE_SANITY_GIC_MSI	BIT(21)
#define MC_INT_DECERR_ROUTE_SANITY		BIT(20)
#define MC_INT_SCRUB_ECC_WR_ACK			BIT(18)
#define MC_INT_DECERR_GENERALIZED_CARVEOUT	BIT(17)
#define MC_INT_DECERR_MTS			BIT(16)
#define MC_INT_SECERR_SEC			BIT(13)
#define MC_INT_DECERR_VPR			BIT(12)
#define MC_INT_SECURITY_VIOLATION		BIT(8)
#define MC_INT_DECERR_EMEM			BIT(6)

#define MC_ERR_STATUS_TYPE_MASK			(0x3 << 28)
#define MC_ERR_STATUS_TYPE_SHIFT		28
#define MC_ERR_STATUS_ADR_HI_SHIFT		20
#define MC_ERR_STATUS_ADR_HI_MASK		0x3
#define MC_ERR_STATUS_RW			BIT(16)
#define MC_ERR_STATUS_SECURITY			BIT(17)

#define CLIENT_ID_MASK				0x1ff
#define MAX_MC_CHANNELS				16

struct tegra_mc_client {
	unsigned int id;
	const char *name;
};

static const struct tegra_mc_client clients[] = {
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
	}, {
		.id = TEGRA264_MEMORY_CLIENT_VICW,
		.name = "vicw",
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
		.id = TEGRA264_MEMORY_CLIENT_NVDECSRD2MC,
		.name = "nvdecsrd2mc",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_NVDECSWR2MC,
		.name = "nvdecswr2mc",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_APER,
		.name = "aper",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_APEW,
		.name = "apew",
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
	}, {
		.id = TEGRA264_MEMORY_CLIENT_APEDMAW,
		.name = "apedmaw",
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
		.id = TEGRA264_MEMORY_CLIENT_GPUR02MC,
		.name = "gpur02mc",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_GPUW02MC,
		.name = "gpuw02mc",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_VIFALCONR,
		.name = "vifalconr",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_VIFALCONW,
		.name = "vifalconw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_RCER,
		.name = "rcer",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_RCEW,
		.name = "rcew",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_NVENC1SRD2MC,
		.name = "nvenc1srd2mc",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_NVENC1SWR2MC,
		.name = "nvenc1swr2mc",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE0W,
		.name = "pcie0w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE1R,
		.name = "pcie1r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE1W,
		.name = "pcie1w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE2AR,
		.name = "pcie2ar",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE2AW,
		.name = "pcie2aw",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE3R,
		.name = "pcie3r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE3W,
		.name = "pcie3w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE4R,
		.name = "pcie4r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE4W,
		.name = "pcie4w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE5R,
		.name = "pcie5r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE5W,
		.name = "pcie5w",
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
	}, {
		.id = TEGRA264_MEMORY_CLIENT_HDAW,
		.name = "hdaw",
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
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MGBE0W,
		.name = "mgbe0w",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MGBE1R,
		.name = "mgbe1r",
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MGBE1W,
		.name = "mgbe1w",
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
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SDMMC0W,
		.name = "sdmmc0w",
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

static u32 num_clients = ARRAY_SIZE(clients);

const char *const tegra_mc_status_names[32] = {
	[ 6] = "EMEM address decode error",
	[ 8] = "Security violation",
	[12] = "VPR violation",
	[13] = "Secure carveout violation",
	[16] = "MTS carveout violation",
	[17] = "Generalized carveout violation",
	[20] = "Route Sanity error",
	[21] = "GIC_MSI error",
};

const char *const tegra_mc_error_names[8] = {
	[1] = "EMEM decode error",
	[2] = "TrustZone violation",
	[3] = "Carveout violation",
};

struct tegra_mcerr {
	struct device *dev;
	void __iomem *bcast_ch_regs;
	void __iomem **ch_regs;
	int channels;
	int irq;
	u32 mcf_int_mask;
};

static const struct of_device_id tegra_mcerr_of_ids[] = {
	{ .compatible = "nvidia,tegra-t264-mc-err"},
	{}
};
MODULE_DEVICE_TABLE(of, tegra_mcerr_of_ids);

static inline u32 mc_ch_readl(const struct tegra_mcerr *mc_err, int ch,
                                unsigned long offset)
{
	if (!mc_err->bcast_ch_regs)
		return 0;

	if (ch == MC_BROADCAST_CHANNEL)
		return readl_relaxed(mc_err->bcast_ch_regs + offset);

	return readl_relaxed(mc_err->ch_regs[ch] + offset);
}

static inline void mc_ch_writel(const struct tegra_mcerr *mc_err, int ch,
                                u32 value, unsigned long offset)
{
	if (!mc_err->bcast_ch_regs)
		return;

	if (ch == MC_BROADCAST_CHANNEL)
		writel_relaxed(value, mc_err->bcast_ch_regs + offset);
	else
		writel_relaxed(value, mc_err->ch_regs[ch] + offset);
}

void log_fault(struct tegra_mcerr *mc_err, u32 channel, unsigned long mcf_ch_intstatus)
{
	unsigned int bit;

	for_each_set_bit(bit, &mcf_ch_intstatus, 32) {
		const char *error = tegra_mc_status_names[bit] ?: "unknown";
		u32 intmask = BIT(bit);
		u32 status_reg, addr_reg, addr_hi_reg = 0;
		u32 addr_val, value, client_id, i;
		const char *direction, *secure;
		const char *client = "unknown", *desc;
		phys_addr_t addr = 0;
		u8 type;

		switch (intmask) {
			case MC_INT_DECERR_EMEM:
				status_reg = MC_ERR_STATUS_0;
				addr_reg = MC_ERR_ADR_0;
				addr_hi_reg = MC_ERR_ADR_HI_0;
				break;
			case MC_INT_SECURITY_VIOLATION:
				status_reg = MC_ERR_STATUS_0;
				addr_reg = MC_ERR_ADR_0;
				addr_hi_reg = MC_ERR_ADR_HI_0;
				break;
			case MC_INT_DECERR_VPR:
				status_reg = MC_ERR_VPR_STATUS_0;
				addr_reg = MC_ERR_VPR_ADR_0;
				break;
			case MC_INT_SECERR_SEC:
				status_reg = MC_ERR_SEC_STATUS_0;
				addr_reg = MC_ERR_SEC_ADR_0;
				break;
			case MC_INT_DECERR_MTS:
				status_reg = MC_ERR_MTS_STATUS_0;
				addr_reg = MC_ERR_MTS_ADR_0;
				break;
			case MC_INT_DECERR_GENERALIZED_CARVEOUT:
				status_reg = MC_ERR_GENERALIZED_CARVEOUT_STATUS_0;
				addr_reg = MC_ERR_GENERALIZED_CARVEOUT_ADR_0;
				break;
			case MC_INT_SCRUB_ECC_WR_ACK:
				status_reg = MC_ERR_STATUS_0;
				addr_reg = MC_ERR_ADR_0;
				addr_hi_reg = MC_ERR_ADR_HI_0;
				break;
			case MC_INT_DECERR_ROUTE_SANITY:
				status_reg = MC_ERR_ROUTE_SANITY_STATUS_0;
				addr_reg = MC_ERR_ROUTE_SANITY_ADR_0;
				break;
			case MC_INT_DECERR_ROUTE_SANITY_GIC_MSI:
				status_reg = MC_ERR_ROUTE_SANITY_STATUS_0;
				addr_reg = MC_ERR_ROUTE_SANITY_ADR_0;
				break;
		}

		value = mc_ch_readl(mc_err, channel, status_reg);
		if (addr_hi_reg)
				addr = mc_ch_readl(mc_err, channel, addr_hi_reg);
		else
				addr = ((value >> MC_ERR_STATUS_ADR_HI_SHIFT) &
						MC_ERR_STATUS_ADR_HI_MASK);

		addr <<= 32;
		addr_val = mc_ch_readl(mc_err, channel, addr_reg);
		addr |= addr_val;

		if (value & MC_ERR_STATUS_RW)
			direction = "write";
		else
			direction = "read";

		if (value & MC_ERR_STATUS_SECURITY)
			secure = "secure";
		else
			secure = "non-secure";

		client_id = value & CLIENT_ID_MASK;
		for (i = 0; i < num_clients; i++) {
			if (clients[i].id == client_id) {
				client = clients[i].name;
				break;
			}
		}

		type = (value & MC_ERR_STATUS_TYPE_MASK) >>
				MC_ERR_STATUS_TYPE_SHIFT;
		desc = tegra_mc_error_names[type];

		dev_err_ratelimited(mc_err->dev, "%s: %s%s @%pa: %s (%s)\n",
							client, secure, direction, &addr, error,
							desc);
	}
}

/* Currently this function only handle MCF interrupts, will extend for other components */
irqreturn_t handle_irq(int irq, void *data)
{
	struct tegra_mcerr *mc_err = data;
	unsigned long mcf_intstatus, mcf_ch_intstatus;
	u32 i;

	/* Read MCF_INSTATUS from MCB and if it is set then check for individual channels */
	mcf_intstatus = mc_ch_readl(mc_err, MC_BROADCAST_CHANNEL, MCF_INTSTATUS_0);
	if (mcf_intstatus == 0) {
		dev_err(mc_err->dev, "No interrupt in MCF\n");
		return IRQ_NONE;
	}

	for(i = 0; i < mc_err->channels; i++) {
		mcf_ch_intstatus = mc_ch_readl(mc_err, i, MCF_INTSTATUS_0) & (mc_err->mcf_int_mask);
		if (mcf_ch_intstatus != 0) {
			/* log fault */
			log_fault(mc_err, i, mcf_ch_intstatus);

			/*
			 * clear interrupts
			 * TODO: This code is taken from upstream mc-err driver but looks incorrect
			 * to me. Need to discuss and correct it.
			 */
			mc_ch_writel(mc_err, i, mcf_ch_intstatus, MCF_INTSTATUS_0);
			mc_ch_writel(mc_err, MC_BROADCAST_CHANNEL, mcf_intstatus, MCF_INTSTATUS_0);
			return IRQ_HANDLED;
		}
	}

	return IRQ_NONE;
}

static int tegra_mcerr_probe(struct platform_device *pdev)
{
	struct tegra_mcerr *mc_err;
	u32 i;
	int err;
	struct resource *res;

	mc_err = devm_kzalloc(&pdev->dev, sizeof(*mc_err), GFP_KERNEL);
	if (!mc_err)
		return -ENOMEM;

	platform_set_drvdata(pdev, mc_err);
	mc_err->dev = &pdev->dev;
	mc_err->channels = MAX_MC_CHANNELS;
	mc_err->ch_regs = devm_kcalloc(&pdev->dev, mc_err->channels, sizeof(*mc_err->ch_regs),
									GFP_KERNEL);
	if(!mc_err->ch_regs)
		return -ENOMEM;

	/* Map MCB */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(mc_err->dev, "Missing MCB resource\n");
		return -ENODEV;
	}

	mc_err->bcast_ch_regs = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (IS_ERR(mc_err->bcast_ch_regs)) {
		dev_err(mc_err->dev, "Ioremap failed for MCB\n");
		return PTR_ERR(mc_err->bcast_ch_regs);
	}

	/* Map MC channels */
	for (i = 0; i < mc_err->channels; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i+1);
		if (!res) {
			dev_err(mc_err->dev, "Missing MC channel %u resource\n", i);
			return -ENODEV;
		}

		mc_err->ch_regs[i] = devm_ioremap(&pdev->dev, res->start, resource_size(res));
		if (IS_ERR(mc_err->ch_regs[i])) {
			dev_err(mc_err->dev, "Ioremap failed for MC channel %u\n", i);
			return PTR_ERR(mc_err->ch_regs[i]);
		}
	}

	/* Set interrupt masks */
	mc_err->mcf_int_mask = MC_INT_DECERR_ROUTE_SANITY_GIC_MSI |
			MC_INT_DECERR_ROUTE_SANITY | MC_INT_SCRUB_ECC_WR_ACK |
			MC_INT_DECERR_GENERALIZED_CARVEOUT | MC_INT_DECERR_MTS |
			MC_INT_SECERR_SEC | MC_INT_DECERR_VPR |
			MC_INT_SECURITY_VIOLATION | MC_INT_DECERR_EMEM;
	mc_ch_writel(mc_err, MC_BROADCAST_CHANNEL, mc_err->mcf_int_mask, MCF_INTMASK_0);
	mc_ch_writel(mc_err, MC_BROADCAST_CHANNEL, mc_err->mcf_int_mask, MCF_INTPRIORITY_0);

	/* Request IRQ and register handler */
	mc_err->irq = platform_get_irq(pdev, 0);
	if (mc_err->irq < 0) {
		dev_err(mc_err->dev, "Unable to parse/map MC error interrupt\n");
		return -EINVAL;
	}

	err = devm_request_irq(&pdev->dev, mc_err->irq, handle_irq, IRQF_SHARED,
							dev_name(&pdev->dev), mc_err);
	if (err) {
		dev_err(mc_err->dev, "devm_request_irq failure\n");
		return err;
	}

	return 0;
}

static int tegra_mcerr_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver mcerr_driver = {
	.driver = {
		.name = "nv-tegra-mcerr",
		.of_match_table = tegra_mcerr_of_ids,
		.owner	= THIS_MODULE,
	},
	.probe = tegra_mcerr_probe,
	.remove = tegra_mcerr_remove,
};

static int __init tegra_mc_err_init(void)
{
	return platform_driver_register(&mcerr_driver);
}

static void __exit tegra_mc_err_exit(void)
{
	platform_driver_unregister(&mcerr_driver);
}

module_exit(tegra_mc_err_exit);
module_init(tegra_mc_err_init);

MODULE_AUTHOR("Ketan Patil <ketanp@nvidia.com>");
MODULE_DESCRIPTION("Tegra264 MC-ERR driver");
MODULE_LICENSE("GPL v2");
