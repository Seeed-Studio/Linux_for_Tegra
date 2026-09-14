/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef __COMMON_H__
#define __COMMON_H__

#include <linux/types.h>
#include <linux/bitops.h>
#include <soc/tegra/fuse-helper.h>

#define MODULE_NAME		"nvscic2c-pcie"
#define DRIVER_NAME_EPF		"nvscic2c-pcie-epf"
#define DRIVER_NAME_EPC		"nvscic2c-pcie-epc"

/* STREAM_OBJ_TYPE. */
#define STREAM_OBJ_TYPE_MEM	(0U)
#define STREAM_OBJ_TYPE_SYNC	(1U)

/*
 * This capped number shall be used to derive export descriptor, therefore any
 * change should be evaluated thoroughly.
 */
#define MAX_STREAM_MEMOBJS	(1024)

/*
 * This capped number shall be used to derive export descriptor, therefore any
 * change should be evaluated thoroughly.
 */
#define MAX_STREAM_SYNCOBJS	(1024)

/*
 * In a topology of interconnected Boards + SoCs.
 *
 * This capped number shall be used to derive export descriptor, therefore any
 * change should be evaluated thoroughly.
 */
#define MAX_BOARDS		(16)
#define MAX_SOCS		(16U)
#define MAX_PCIE_CNTRLRS	(16U)

/*
 * Maximum NvSciIpc INTER_CHHIP(NvSciC2cPcie) endpoints that can be supported
 * for single pair of PCIe RP<>EP connection (referred just as 'connection'
 * henceforth). We have specific customer need for a set of Eleven NvSciC2cPcie
 * endpoints for single connection.
 *
 * This capped number shall be used to derive export descriptor, therefore any
 * change should be evaluated thoroughly.
 */
#define MAX_ENDPOINTS		(16U)

/*
 * Each NvSciIpc INTER_CHIP(NvSciC2cPcie) endpoint shall require at least one
 * distinct notification Id (MSI/MSI-X, GIC SPI or NvRmHost1xSyncpointShim).
 * Also, these notification mechanisms: MSI/MSI-X, GIC SPI, SyncpointShim are
 * limited on SoC or per connection (configurable via device-tree).
 *
 * Also, there is a private communication channel between the two ends of a
 * single connection that need notification Ids for message passing. Assuming
 * this private communication channel to be a Queue-Pair (Cmd, Resp), need
 * at least 2 distinct notification Ids for it on a single connection.
 */
#define MIN_NUM_NOTIFY		(MAX_ENDPOINTS + (2))

/* NvRmHost1xSyncpointShim have size: 64KB on Orin.*/
#define SP_SIZE			(0x10000UL)

/*
 * Represents SyncpointShimBase on all T234.
 * Each syncpoint is offset at (syncpt_id * SP_SIZE) on SHIM_BASE.
 */
#define SHIM_BASE_ORIN		(0x60000000)
#define SHIM_BASE_THOR		(0x81C0000000)
/*
 * For our use-case, if only 4 bytes of NvRmHost1xSynpointShim aperture mapped
 * to PCIe device, any writes of (SZ_4B) from remote is enough to increment
 * the Syncpoint. Hence we only map 4KB/PAGE_SIZE instead of full 64KB.
 */
#define SP_MAP_SIZE		(0x1000)

/* With Tegra as PCIe function we can have only one PCIe function. */
#define PCIE_VFNO		(0)

/*
 * NvSciC2c supports three concurrent PCI RP<>EP connection.
 * These are three PCI Function Device ID's to be configured in PCI header
 * when Tegra acting as PCI Function to peer Tegra acting as PCI RP.
 */
#define PCI_DEVICE_ID_C2C_1	(0x22CBU)
#define PCI_DEVICE_ID_C2C_2	(0x22CCU)
#define PCI_DEVICE_ID_C2C_3	(0x22CDU)

/*
 * For NvStreams extensions over NvSciC2cPcie, an endpoint is a producer on
 * one SoC and a corresponding consumer on the remote SoC. The role
 * classification cannot be deduced in KMD.
 */

/*
 * PCIe BAR aperture for Tx to/Rx from peer.
 */
struct pci_aper_t {
	/* physical Pcie aperture.*/
	phys_addr_t aper;

	/* process virtual address for CPU access.*/
	void __iomem *pva;

	/* size of the perture.*/
	size_t size;
};

/*
 * DMA'able memory registered/exported to peer -
 * either allocated by dma_buf API or physical pages pinned to
 * pcie address space(dma_handle).
 */
struct dma_buff_t {
	/* process virtual address for CPU access. */
	void *pva;

	/* iova(iommu=ON) or bus address/physical address for device access. */
	dma_addr_t dma_handle;

	/* physical address.*/
	u64 phys_addr;

	/* size of the memory allocated. */
	size_t size;
};

/*
 * CPU-only accessible memory which is not PCIe aper or PCIe
 * DMA'able memory. This shall contain information of memory
 * allocated via kalloc()/likewise.
 */
struct cpu_buff_t {
	/* process virtual address for CPU access. */
	void *pva;

	/* (va->pa) physical address. */
	u64 phys_addr;

	/* size of the memory allocated. */
	size_t size;
};

/*
 * Callback options for user to register with occurrence of an event.
 */
struct callback_ops {
	/*
	 * User callback to be invoked.
	 * @data: Event-type or likewise data. read-only for user.
	 * @ctx: user ctx returned as-is in the callback.
	 */
	void (*callback)(void *data, void *ctx);

	/* user context that shall be passed with @callback.*/
	void *ctx;
};

/*
 * Node information. A combination of Board + SoC + PCIe controller
 * should be unique within the PCIe controllers/SoCs/Boards interconnected
 for NvSciC2cPcie.
 */
struct node_info_t {
	u32 board_id;
	u32 soc_id;
	u32 cntrlr_id;
};

/*
 * NvSciC2cPcie either works as EndpointClient module - client driver for
 * remote PCIe EP (runs on the PCIe RP SoC) or as EndpointFunction module -
 * PCIe EP function driver (runs on the PCIe EP SoC).
 */
enum drv_mode_t {
	/* Invalid. */
	DRV_MODE_INVALID = 0,

	/* Driver module runs as EndpointClient driver.*/
	DRV_MODE_EPC,

	/* Drive module runs as EndpointFunction driver.*/
	DRV_MODE_EPF,

	/* Maximum.*/
	DRV_MODE_MAXIMUM,
};

/*
 * NvSciC2cPcie the cpu on peer
 */
enum peer_cpu_t {
	NVCPU_ORIN = 0,
	NVCPU_X86_64,
	NVCPU_MAXIMUM,
};

/* Returns aperture offset of syncpoint on SHIM_BASE. */
static inline u64 get_syncpt_shim_offset(u32 id, u8 chip_id)
{
	u64 base = 0U;

	if (chip_id == TEGRA234)
		base = SHIM_BASE_ORIN;
	else
		base = SHIM_BASE_THOR;

	return (base + ((u64)id * SP_SIZE));
}

/* Adition of uint64 variables with overflow detection */
static inline bool
AddU64(uint64_t op1, uint64_t op2, uint64_t *result)
{
	bool e = false;

	if (((U64_MAX - op1) < op2) == false) {
		*result = op1 + op2;
		e = true;
	}

	return e;
}

/* Multiplication of uint64 variables with overflow detection */
static inline bool
MultU64(uint64_t op1, uint64_t op2, uint64_t *result)
{
	bool e = false;

	if ((op1 == 0U) || (op2 == 0U))
		*result = 0U;
	else if ((op1 > (U64_MAX / op2)) == false)
		*result = op1 * op2;
	else
		goto fail;

	e = true;

fail:
	return e;
}

#endif //__COMMON_H__
