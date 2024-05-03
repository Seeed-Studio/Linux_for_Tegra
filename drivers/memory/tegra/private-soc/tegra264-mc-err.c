// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2024, NVIDIA CORPORATION.  All rights reserved.
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

#define MCF_COMMON_INTSTATUS0_0_0		0xce04
#define MSS_HUB_GLOBAL_INTSTATUS_0 		0x6000
#define MCF_INTSTATUS_0				0xce2c
#define MSS_HUB_HUBC_INTSTATUS_0		0x6008
#define MSS_HUB_INTRSTATUS_0			0x600c
#define MSS_SBS_INTSTATUS_0			0xec08
#define MC_CH_INTSTATUS_0			0x82d4
#define MC_ERR_STATUS_0				0xbc00
#define MC_ERR_ADR_0				0xbc04
#define MC_ERR_ADR_HI_0				0xbc08
#define MC_ERR_VPR_STATUS_0			0xbc20
#define MC_ERR_VPR_ADR_0			0xbc24
#define MC_ERR_SEC_STATUS_0			0xbc3c
#define MC_ERR_SEC_ADR_0			0xbc40
#define MC_ERR_MTS_STATUS_0			0xbc5c
#define MC_ERR_MTS_ADR_0			0xbc60
#define MC_ERR_GENERALIZED_CARVEOUT_STATUS_0	0xbc78
#define MC_ERR_GENERALIZED_CARVEOUT_STATUS_1_0	0xbc74
#define MC_ERR_GENERALIZED_CARVEOUT_ADR_0	0xbc7c
#define MC_ERR_ROUTE_SANITY_STATUS_0		0xbc64
#define MC_ERR_ROUTE_SANITY_ADR_0		0xbc68
#define MCF_INTMASK_0				0xce30
#define MCF_INTPRIORITY_0			0xce34
#define MSS_HUB_INTRMASK_0			0x6018
#define MSS_HUB_INTRPRIORITY_0			0x601c
#define MSS_HUB_HUBC_INTMASK_0			0x6010
#define MSS_HUB_HUBC_INTPRIORITY_0		0x6014
#define MSS_SBS_INTMASK_0			0xec0c
#define MC_CH_INTMASK_0				0x82d8
#define MSS_HUB_RESERVED_PA_ERR_STATUS_0	0x6390
#define MSS_HUB_RESTRICTED_ACCESS_ERR_STATUS_0	0x638c
#define MSS_HUB_POISON_RSP_STATUS_0		0x6028
#define MSS_HUB_MSI_ERR_STATUS_0		0x6024
#define MSS_HUB_ILLEGAL_TBUGRP_ID_ERR_STATUS_0	0x63b0
#define MSS_HUB_SMMU_BYPASS_ALLOW_ERR_STATUS_0	0x6020
#define MSS_HUB_COALESCE_ERR_STATUS_0		0x60e0
#define MSS_HUB_COALESCE_ERR_ADR_HI_0		0x60e4
#define MSS_HUB_COALESCE_ERR_ADR_0		0x60e8

/* Bit fields of MCF_INTSTATUS_0 register */
#define MC_INT_DECERR_ROUTE_SANITY_GIC_MSI	BIT(21)
#define MC_INT_DECERR_ROUTE_SANITY		BIT(20)
#define MC_INT_SCRUB_ECC_WR_ACK			BIT(18)
#define MC_INT_DECERR_GENERALIZED_CARVEOUT	BIT(17)
#define MC_INT_DECERR_MTS			BIT(16)
#define MC_INT_SECERR_SEC			BIT(13)
#define MC_INT_DECERR_VPR			BIT(12)
#define MC_INT_SECURITY_VIOLATION		BIT(8)
#define MC_INT_DECERR_EMEM			BIT(6)

/* Bit fields of MSS_HUB_INTRMASK_0 register */
#define COALESCER_ERR_INTMASK			BIT(0)
#define SMMU_BYPASS_ALLOW_ERR_INTMASK		BIT(1)
#define ILLEGAL_TBUGRP_ID_INTMASK		BIT(2)
#define MSI_ERR_INTMASK				BIT(3)
#define POISON_RSP_INTMASK			BIT(4)
#define RESTRICTED_ACCESS_ERR_INTMASK		BIT(5)
#define RESERVED_PA_ERR_INTMASK			BIT(6)

/* Bit fields of MSS_HUB_HUBC_INTMASK_0 register */
#define SCRUB_DONE_INTMASK			BIT(0)

/* Bit fields of MSS_SBS_INTMASK_0 register */
#define FILL_FIFO_ISO_OVERFLOW_INTMASK		BIT(0)
#define FILL_FIFO_SISO_OVERFLOW_INTMASK		BIT(1)
#define FILL_FIFO_NISO_OVERFLOW_INTMASK		BIT(2)

/* Bit fields of MC_CH_INTMASK_0 register */
#define WCAM_ERR_INTMASK			BIT(19)
#define ARBITRATION_EMEM_INTMASK		BIT(9)

/* Bit fields of MCF_COMMON_INTSTATUS0_0_0 register */
#define MCF_SECURITY0_INT0			BIT(0)
#define MCF_SECURITY1_INT0			BIT(1)
#define MCF_SECURITY2_INT0			BIT(2)
#define MCF_SECURITY3_INT0			BIT(3)
#define MCF_SECURITY4_INT0			BIT(4)

/* Bit fields of MSS_HUB_GLOBAL_INTSTATUS_0 register */
#define HUBC_INTR				BIT(0)
#define HUB0_INTR				BIT(8)
#define HUB1_INTR				BIT(9)
#define HUB2_INTR				BIT(10)
#define HUB3_INTR				BIT(11)
#define HUB4_INTR				BIT(12)
#define HUB5_INTR				BIT(13)
#define HUB6_INTR				BIT(14)

#define MC_ERR_STATUS_TYPE_MASK			(0x3 << 28)
#define MC_ERR_STATUS_TYPE_SHIFT		28
#define MC_ERR_STATUS_TYPE_MASK_RT		(0xf << 28)
#define MC_ERR_STATUS_TYPE_SHIFT_RT		28
#define MC_ERR_STATUS_ADR_HI_SHIFT		20
#define MC_ERR_STATUS_ADR_HI_MASK		0xff
#define MC_ERR_STATUS_ADR_HI_SHIFT_RT		15
#define MC_ERR_STATUS_ADR_HI_SHIFT_GSC		16
#define MC_ERR_STATUS_ADR_HI_MASK_GSC		0xffff
#define MC_ERR_STATUS_RW			BIT(16)
#define MC_ERR_STATUS_SECURITY			BIT(17)
#define MC_ERR_ROUTE_SANITY_RW			BIT(12)
#define MC_ERR_ROUTE_SANITY_SEC			BIT(13)
#define ERR_GENERALIZED_APERTURE_ID_SHIFT	0
#define ERR_GENERALIZED_APERTURE_ID_MASK	0x1F
#define ERR_GENERALIZED_CARVEOUT_APERTURE_ID_SHIFT 5
#define ERR_GENERALIZED_CARVEOUT_APERTURE_ID_MASK 0x1F
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

const char *const tegra_hub_status_names[32] = {
	[0] = "coalescer error",
	[1] = "SMMU BYPASS ALLOW error",
	[2] = "Illegal tbugrp_id error",
	[3] = "Malformed MSI request error",
	[4] = "Read response with poison bit error",
	[5] = "Restricted access violation error",
	[6] = "Reserved PA error",
};

const char *const tegra_mc_error_names[4] = {
	[1] = "EMEM decode error",
	[2] = "TrustZone violation",
	[3] = "Carveout violation",
};

const char *const tegra_rt_error_names[16] = {
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

struct tegra_mcerr {
	struct device *dev;
	void __iomem *bcast_ch_regs;
	void __iomem **ch_regs;
	int channels;
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

static void set_interrupt_masks(struct tegra_mcerr *mc_err)
{
	u32 mask_value;

	/* Unmask MCF interrupts */
	mask_value = MC_INT_DECERR_ROUTE_SANITY_GIC_MSI |
			MC_INT_DECERR_ROUTE_SANITY |
			MC_INT_DECERR_GENERALIZED_CARVEOUT | MC_INT_DECERR_MTS |
			MC_INT_SECERR_SEC | MC_INT_DECERR_VPR |
			MC_INT_SECURITY_VIOLATION | MC_INT_DECERR_EMEM;
	mc_ch_writel(mc_err, MC_BROADCAST_CHANNEL, mask_value, MCF_INTMASK_0);
	mc_ch_writel(mc_err, MC_BROADCAST_CHANNEL, mask_value, MCF_INTPRIORITY_0);

	/* Unmask HUB and HUBC interrupts */
	mask_value = COALESCER_ERR_INTMASK | SMMU_BYPASS_ALLOW_ERR_INTMASK |
			ILLEGAL_TBUGRP_ID_INTMASK | MSI_ERR_INTMASK | POISON_RSP_INTMASK |
			RESTRICTED_ACCESS_ERR_INTMASK | RESERVED_PA_ERR_INTMASK;
	mc_ch_writel(mc_err, MC_BROADCAST_CHANNEL, mask_value, MSS_HUB_INTRMASK_0);
	mc_ch_writel(mc_err, MC_BROADCAST_CHANNEL, mask_value, MSS_HUB_INTRPRIORITY_0);

	mask_value = SCRUB_DONE_INTMASK;
	mc_ch_writel(mc_err, MC_BROADCAST_CHANNEL, mask_value, MSS_HUB_HUBC_INTMASK_0);
	mc_ch_writel(mc_err, MC_BROADCAST_CHANNEL, mask_value, MSS_HUB_HUBC_INTPRIORITY_0);

	/* Unmask SBS interrupt */
	mask_value = FILL_FIFO_ISO_OVERFLOW_INTMASK | FILL_FIFO_SISO_OVERFLOW_INTMASK |
				FILL_FIFO_NISO_OVERFLOW_INTMASK;
	mc_ch_writel(mc_err, MC_BROADCAST_CHANNEL, mask_value, MSS_SBS_INTMASK_0);

	/* Unmask MC channel interrupt */
	mask_value = WCAM_ERR_INTMASK | ARBITRATION_EMEM_INTMASK;
	mc_ch_writel(mc_err, MC_BROADCAST_CHANNEL, mask_value, MC_CH_INTMASK_0);
}

static void hub_log_fault(struct tegra_mcerr *mc_err, u32 hub, unsigned long hub_intstat)
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
			case COALESCER_ERR_INTMASK:
				status_reg = MSS_HUB_RESERVED_PA_ERR_STATUS_0;
				break;
			case SMMU_BYPASS_ALLOW_ERR_INTMASK:
				status_reg = MSS_HUB_RESTRICTED_ACCESS_ERR_STATUS_0;
				break;
			case ILLEGAL_TBUGRP_ID_INTMASK:
				status_reg = MSS_HUB_POISON_RSP_STATUS_0;
				break;
			case MSI_ERR_INTMASK:
				status_reg = MSS_HUB_MSI_ERR_STATUS_0;
				break;
			case POISON_RSP_INTMASK:
				status_reg = MSS_HUB_ILLEGAL_TBUGRP_ID_ERR_STATUS_0;
				break;
			case RESTRICTED_ACCESS_ERR_INTMASK:
				status_reg = MSS_HUB_SMMU_BYPASS_ALLOW_ERR_STATUS_0;
				break;
			case RESERVED_PA_ERR_INTMASK:
				status_reg = MSS_HUB_COALESCE_ERR_STATUS_0;
				addr_reg = MSS_HUB_COALESCE_ERR_ADR_0;
				addr_hi_reg = MSS_HUB_COALESCE_ERR_ADR_HI_0;
				break;
			default:
				dev_err_ratelimited(mc_err->dev, "Incorrect HUB interrupt mask\n");
				return;
		}

		value = mc_ch_readl(mc_err, hub, status_reg);
		if (addr_reg) {
			addr = mc_ch_readl(mc_err, hub, addr_hi_reg);
			addr <<= 32;
			addr_val = mc_ch_readl(mc_err, hub, addr_reg);
			addr |= addr_val;
		}

		client_id = value & CLIENT_ID_MASK;
		for (i = 0; i < num_clients; i++) {
			if (clients[i].id == client_id) {
				client = clients[i].name;
				break;
			}
		}

		dev_err_ratelimited(mc_err->dev, "%s: @%pa: %s status:%u\n",
							client, &addr, error, value);
	}

	/* clear interrupts */
	mc_ch_writel(mc_err, hub, hub_intstat, MSS_HUB_INTRSTATUS_0);
	mc_ch_writel(mc_err, MC_BROADCAST_CHANNEL, hub_intstat, MSS_HUB_INTRSTATUS_0);
}

static void mcf_log_fault(struct tegra_mcerr *mc_err, u32 channel, unsigned long mcf_ch_intstatus)
{
	unsigned int bit;

	for_each_set_bit(bit, &mcf_ch_intstatus, 32) {
		const char *error = tegra_mc_status_names[bit] ?: "unknown";
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
				status_reg = MC_ERR_STATUS_0;
				addr_reg = MC_ERR_ADR_0;
				addr_hi_reg = MC_ERR_ADR_HI_0;
				err_type_valid = true;
				break;
			case MC_INT_SECURITY_VIOLATION:
				status_reg = MC_ERR_STATUS_0;
				addr_reg = MC_ERR_ADR_0;
				addr_hi_reg = MC_ERR_ADR_HI_0;
				err_type_valid = true;
				break;
			case MC_INT_DECERR_VPR:
				status_reg = MC_ERR_VPR_STATUS_0;
				addr_reg = MC_ERR_VPR_ADR_0;
				addr_hi_shift = MC_ERR_STATUS_ADR_HI_SHIFT;
				addr_hi_mask = MC_ERR_STATUS_ADR_HI_MASK;
				break;
			case MC_INT_SECERR_SEC:
				status_reg = MC_ERR_SEC_STATUS_0;
				addr_reg = MC_ERR_SEC_ADR_0;
				addr_hi_shift = MC_ERR_STATUS_ADR_HI_SHIFT;
				addr_hi_mask = MC_ERR_STATUS_ADR_HI_MASK;
				break;
			case MC_INT_DECERR_MTS:
				status_reg = MC_ERR_MTS_STATUS_0;
				addr_reg = MC_ERR_MTS_ADR_0;
				addr_hi_shift = MC_ERR_STATUS_ADR_HI_SHIFT;
				addr_hi_mask = MC_ERR_STATUS_ADR_HI_MASK;
				break;
			case MC_INT_DECERR_GENERALIZED_CARVEOUT:
				status_reg = MC_ERR_GENERALIZED_CARVEOUT_STATUS_0;
				status1_reg = MC_ERR_GENERALIZED_CARVEOUT_STATUS_1_0;
				addr_reg = MC_ERR_GENERALIZED_CARVEOUT_ADR_0;
				addr_hi_shift = MC_ERR_STATUS_ADR_HI_SHIFT_GSC;
				addr_hi_mask = MC_ERR_STATUS_ADR_HI_MASK_GSC;
				is_gsc = true;
				break;
			case MC_INT_DECERR_ROUTE_SANITY:
				status_reg = MC_ERR_ROUTE_SANITY_STATUS_0;
				addr_reg = MC_ERR_ROUTE_SANITY_ADR_0;
				addr_hi_shift = MC_ERR_STATUS_ADR_HI_SHIFT_RT;
				addr_hi_mask = MC_ERR_STATUS_ADR_HI_MASK;
				mc_sec_bit = MC_ERR_ROUTE_SANITY_SEC;
				mc_rw_bit = MC_ERR_ROUTE_SANITY_RW;
				err_rt_type_valid = true;
				break;
			case MC_INT_DECERR_ROUTE_SANITY_GIC_MSI:
				status_reg = MC_ERR_ROUTE_SANITY_STATUS_0;
				addr_reg = MC_ERR_ROUTE_SANITY_ADR_0;
				addr_hi_shift = MC_ERR_STATUS_ADR_HI_SHIFT_RT;
				addr_hi_mask = MC_ERR_STATUS_ADR_HI_MASK;
				mc_sec_bit = MC_ERR_ROUTE_SANITY_SEC;
				mc_rw_bit = MC_ERR_ROUTE_SANITY_RW;
				err_rt_type_valid = true;
				break;
			default:
				dev_err_ratelimited(mc_err->dev, "Incorrect MC interrupt mask\n");
				return;
		}

		value = mc_ch_readl(mc_err, channel, status_reg);
		if (addr_hi_reg) {
			addr = mc_ch_readl(mc_err, channel, addr_hi_reg);
		}
		else {
			if (!is_gsc) {
				addr = ((value >> addr_hi_shift) & addr_hi_mask);
			} else {
				status1 = mc_ch_readl(mc_err, channel, status1_reg);
				addr = ((status1 >> addr_hi_shift) & addr_hi_mask);
			}
		}
		addr <<= 32;
		addr_val = mc_ch_readl(mc_err, channel, addr_reg);
		addr |= addr_val;

		if (value & mc_rw_bit)
			direction = "write";
		else
			direction = "read";

		if (value & mc_sec_bit)
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

		if (err_type_valid) {
			type = (value & MC_ERR_STATUS_TYPE_MASK) >>
					MC_ERR_STATUS_TYPE_SHIFT;
			desc = tegra_mc_error_names[type];
		} else if (err_rt_type_valid) {
			type = (value & MC_ERR_STATUS_TYPE_MASK_RT) >>
					MC_ERR_STATUS_TYPE_SHIFT_RT;
			desc = tegra_rt_error_names[type];
		}

		dev_err_ratelimited(mc_err->dev, "%s: %s %s @%pa: %s (%s)\n",
							client, secure, direction, &addr, error,
							desc);
		if (is_gsc) {
			dev_err_ratelimited(mc_err->dev, "gsc_apr_id=%u gsc_co_apr_id=%u\n",
						((status1 >> ERR_GENERALIZED_APERTURE_ID_SHIFT)
						& ERR_GENERALIZED_APERTURE_ID_MASK),
						((status1 >> ERR_GENERALIZED_CARVEOUT_APERTURE_ID_SHIFT)
						& ERR_GENERALIZED_CARVEOUT_APERTURE_ID_MASK));
		}
	}

	/* clear interrupts */
	mc_ch_writel(mc_err, channel, mcf_ch_intstatus, MCF_INTSTATUS_0);
	mc_ch_writel(mc_err, MC_BROADCAST_CHANNEL, mcf_ch_intstatus, MCF_INTSTATUS_0);
}

#define SBS_IRQ 1
#define MC_CHANNEL_IRQ 2

static irqreturn_t handle_generic_irq(void *data, int type)
{
	struct tegra_mcerr *mc_err = data;
	unsigned long intstat_bc, intstat_reg, intstat;
	int i;

	if (type == SBS_IRQ) {
		intstat_reg = MSS_SBS_INTSTATUS_0;
	} else if (type == MC_CHANNEL_IRQ) {
		intstat_reg = MC_CH_INTSTATUS_0;
	} else {
		dev_err(mc_err->dev, "Incorrect IRQ type\n");
		return IRQ_NONE;
	}

	/* Read INTSTATUS reg from MCB block */
	intstat_bc = mc_ch_readl(mc_err, MC_BROADCAST_CHANNEL, intstat_reg);
	if (intstat_bc == 0) {
		dev_err(mc_err->dev, "No interrupt bit set in INTSTATUS reg\n");
		return IRQ_NONE;
	}

	/* Iterate over all MC blocks to read INTSTATUS */
	for(i = 0; i < MAX_MC_CHANNELS; i++) {
		intstat = mc_ch_readl(mc_err, i, intstat_reg);
		dev_err_ratelimited(mc_err->dev, "status:%lu\n", intstat);
		/* clear interrupt */
		mc_ch_writel(mc_err, i, intstat, intstat_reg);
	}

	/* clear interrupt */
	mc_ch_writel(mc_err, MC_BROADCAST_CHANNEL, intstat_bc, intstat_reg);
	return IRQ_HANDLED;
}

/* Interrupt handler for MC channel */
static irqreturn_t handle_channel_irq(int irq, void *data)
{
	return handle_generic_irq(data, MC_CHANNEL_IRQ);
}

/* Interrupt handler for SBS */
static irqreturn_t handle_sbs_irq(int irq, void *data)
{
	return handle_generic_irq(data, SBS_IRQ);
}

/* Interrupt handler for HUB and HUBC */
static irqreturn_t handle_hub_irq(int irq, void *data)
{
	struct tegra_mcerr *mc_err = data;
	unsigned long hub_global_intstat, hub_intstat;
	int hub;
	bool is_hubc = false;

	/* Read MSS_HUB_GLOBAL_INTSTATUS_0 from MCB block */
	hub_global_intstat = mc_ch_readl(mc_err, MC_BROADCAST_CHANNEL, MSS_HUB_GLOBAL_INTSTATUS_0);
	if (hub_global_intstat == 0) {
		dev_err(mc_err->dev, "No interrupt in HUB/HUBC\n");
		return IRQ_NONE;
	}

	/* Find out the hub or hubc number on which interrupt occurred */
	if (hub_global_intstat & HUBC_INTR) {
		is_hubc = true;
	} else if (hub_global_intstat & HUB0_INTR) {
		hub = 0;
	} else if (hub_global_intstat & HUB1_INTR) {
		hub = 1;
	} else if (hub_global_intstat & HUB2_INTR) {
		hub = 2;
	} else if (hub_global_intstat & HUB3_INTR) {
		hub = 3;
	} else if (hub_global_intstat & HUB4_INTR) {
		hub = 4;
	} else if (hub_global_intstat & HUB5_INTR) {
		hub = 5;
	} else if (hub_global_intstat & HUB6_INTR) {
		hub = 6;
	} else {
		dev_err(mc_err->dev, "No interrupt in HUB/HUBC\n");
		return IRQ_NONE;
	}

	if (is_hubc) {
		/* Read MSS_HUB_HUBC_INTSTATUS_0 from block MCB */
		hub_intstat = mc_ch_readl(mc_err, MC_BROADCAST_CHANNEL, MSS_HUB_HUBC_INTSTATUS_0);
	} else {
		/* Read MSS_HUB_INTRSTATUS_0 from block MCi */
		hub_intstat = mc_ch_readl(mc_err, hub, MSS_HUB_INTRSTATUS_0);
	}

	if (hub_intstat != 0) {
		if (is_hubc) {
			dev_err_ratelimited(mc_err->dev, "Scrubber operation status:%lu\n",
					hub_intstat);
			/*clear hubc interrupt */
			mc_ch_writel(mc_err, MC_BROADCAST_CHANNEL, hub_intstat,
					MSS_HUB_HUBC_INTSTATUS_0);
		} else {
			hub_log_fault(mc_err, hub, hub_intstat);
		}
	}
	return IRQ_HANDLED;
}

/* Interrupt handler for MCF */
static irqreturn_t handle_mcf_irq(int irq, void *data)
{
	struct tegra_mcerr *mc_err = data;
	unsigned long mcf_common_intstat, mcf_intstatus;
	int slice;

	/* Read MCF_COMMON_INTSTATUS0_0_0 from MCB block */
	mcf_common_intstat = mc_ch_readl(mc_err, MC_BROADCAST_CHANNEL, MCF_COMMON_INTSTATUS0_0_0);
	if (mcf_common_intstat == 0) {
		dev_err(mc_err->dev, "No interrupt in MCF\n");
		return IRQ_NONE;
	}

	/* Find out the slice number on which interrupt occurred */
	if (mcf_common_intstat & MCF_SECURITY0_INT0) {
		slice = 0;
	} else if (mcf_common_intstat & MCF_SECURITY1_INT0) {
		slice = 1;
	} else if (mcf_common_intstat & MCF_SECURITY2_INT0) {
		slice = 2;
	} else if (mcf_common_intstat & MCF_SECURITY3_INT0) {
		slice = 3;
	} else if (mcf_common_intstat & MCF_SECURITY4_INT0) {
		slice = 4;
	} else {
		dev_err(mc_err->dev, "No interrupt in MCF slice\n");
		return IRQ_NONE;
	}

	/* Read MCF_INTSTATUS_0 from MCi */
	mcf_intstatus = mc_ch_readl(mc_err, slice, MCF_INTSTATUS_0);
	if (mcf_intstatus != 0) {
		mcf_log_fault(mc_err, slice, mcf_intstatus);
	}
	return IRQ_HANDLED;
}

static int register_irq_handlers(struct platform_device *pdev, struct tegra_mcerr *mc_err)
{
	int err, irq_num, i;

	/* Register handler for MCF interrupt */
	irq_num = platform_get_irq(pdev, 0);
	if (irq_num< 0) {
		dev_err(mc_err->dev, "Unable to parse/map MC error interrupt\n");
		return -EINVAL;
	}

	err = devm_request_irq(&pdev->dev, irq_num, handle_mcf_irq, IRQF_SHARED,
							dev_name(&pdev->dev), mc_err);
	if (err) {
		dev_err(mc_err->dev, "devm_request_irq failure\n");
		return err;
	}

	/* Register handler for HUB and HUBC interrupts */
	for (i = 1; i <= 5; i++) {
		irq_num = platform_get_irq(pdev, i);
		if (irq_num< 0) {
			dev_err(mc_err->dev, "Unable to parse/map MC error interrupt\n");
			return -EINVAL;
		}

		err = devm_request_irq(&pdev->dev, irq_num, handle_hub_irq, IRQF_SHARED,
							dev_name(&pdev->dev), mc_err);
		if (err) {
			dev_err(mc_err->dev, "devm_request_irq failure\n");
			return err;
		}
	}

	/*Register handler for SBS interrupt */
	irq_num = platform_get_irq(pdev, i);
	if (irq_num< 0) {
		dev_err(mc_err->dev, "Unable to parse/map MC error interrupt\n");
		return -EINVAL;
	}

	err = devm_request_irq(&pdev->dev, irq_num, handle_sbs_irq, IRQF_SHARED,
							dev_name(&pdev->dev), mc_err);
	if (err) {
		dev_err(mc_err->dev, "devm_request_irq failure\n");
		return err;
	}

	i++;
	/*Register handler for MC channel interrupt */
	irq_num = platform_get_irq(pdev, i);
	if (irq_num< 0) {
		dev_err(mc_err->dev, "Unable to parse/map MC error interrupt\n");
		return -EINVAL;
	}

	err = devm_request_irq(&pdev->dev, irq_num, handle_channel_irq, IRQF_SHARED,
							dev_name(&pdev->dev), mc_err);
	if (err) {
		dev_err(mc_err->dev, "devm_request_irq failure\n");
		return err;
	}

	return 0;
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

	/* Request IRQ and register handler */
	err = register_irq_handlers(pdev, mc_err);
	if (err) {
		dev_err(mc_err->dev, "Irq handler registeration failed\n");
		return err;
	}

	/* Set interrupt masks */
	set_interrupt_masks(mc_err);
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
