/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef TEGRA_PCIE_DMA_H
#define TEGRA_PCIE_DMA_H

/**
 * @brief T264 supports up to 4 channels and T234 supports up to 2 channels. DMA library ignore
 * channel 2 and 3 arguments for T234 and returns error from tegra_pcie_dma_submit_xfer() if
 * these channels are used.
 */
#define TEGRA_PCIE_DMA_RD_CHNL_NUM	4
#define TEGRA_PCIE_DMA_WR_CHNL_NUM	4

#define TEGRA_PCIE_DMA_DESC_SZ		32

/** MSI IRQ vector number to use on T264 SoC for write and read channels */
#define TEGRA264_PCIE_DMA_MSI_LOCAL_VEC		4
#define TEGRA264_PCIE_DMA_MSI_REMOTE_VEC	5

/**
 * @brief typedef to define various values for xfer status passed for dma_complete_t or
 * tegra_pcie_dma_submit_xfer()
 */
typedef enum {
	/** On successful completion of DMA TX or if API successfully completed its operation. */
	TEGRA_PCIE_DMA_SUCCESS = 0,
	/** Invalid input parameters passed. */
	TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS,
	/** Descriptors not available or memory creation error */
	TEGRA_PCIE_DMA_FAIL_NOMEM,
	/** Time out failure during sync wait for NVPCIE_DMA_SYNC_TO_SEC timeout */
	TEGRA_PCIE_DMA_FAIL_TIMEOUT,
	/** HW abort or SW stop processing in progress. */
	TEGRA_PCIE_DMA_ABORT,
	/**
	 * Status set when DMA is de-initalized via tegra_pcie_dma_deinit(), during
	 * tegra_pcie_dma_submit_xfer() or already intiialized via tegra_pcie_dma_initialize()
	 */
	TEGRA_PCIE_DMA_DEINIT,
	/**
	 * Status set when API is called in wrong operation mode or when pre-condition of API is
	 * not met.
	 */
	TEGRA_PCIE_DMA_STATUS_INVAL_STATE,
} tegra_pcie_dma_status_t;

/** @brief typedef to define various values for xfer type passed tegra_pcie_dma_submit_xfer() */
typedef enum {
	TEGRA_PCIE_DMA_WRITE = 0,
	TEGRA_PCIE_DMA_READ,
} tegra_pcie_dma_xfer_type_t;

/**
 * @brief typedef to define various supported SoC's for DMA
 */
typedef enum {
	TEGRA_PCIE_DMA_CHAN_XFER_SYNC = 0,
	TEGRA_PCIE_DMA_CHAN_XFER_ASYNC,
} tegra_pcie_dma_chan_type_t;

/**
 * @brief typedef to define various supported DMA controller SoCs for channel configuration done
 * in tegra_pcie_dma_initialize()
 */
typedef enum {
	NVPCIE_DMA_SOC_T234 = 0,
	NVPCIE_DMA_SOC_T264,
} nvpcie_dma_soc_t;

/** Forward declaration */
struct tegra_pcie_dma_desc;

/** @brief Tx Async callback function pointer */
typedef void (tegra_pcie_dma_complete_t)(void *priv, tegra_pcie_dma_status_t status);

/** @brief Remote DMA controller details.
 *  @note this is initial revision and expected to be modified.
 */
struct tegra_pcie_dma_remote_info {
	/** EP's DMA PHY base address, which is BAR0 for T264 and BAR4 for T234 */
	phys_addr_t dma_phy_base;
	/** EP's DMA register spec size, which is BAR0 size for T264 and BAR4 size for T234 */
	uint32_t dma_size;
};

/** @brief details of DMA Tx channel configuration */
struct tegra_pcie_dma_chans_info {
	/** Variable to specify if corresponding channel should run in Sync/Async mode. */
	tegra_pcie_dma_chan_type_t ch_type;
	/** Number of descriptors that needs to be configured for this channel. Max value 32K.
	 *  @note
	 *   - If 0 is passed, this channel will be treated un-used.
	 *   - else it must be power of 2.
	 */
	uint32_t num_descriptors;
	/* Below parameter are used, only if remote is present in #tegra_pcie_dma_init_info */
	/**
	 * Descriptor PHY base allocated by client which is part of BAR0 in T234 and BAR1 in T264.
	 * Each descriptor is of size TEGRA_PCIE_DMA_DESC_SZ bytes.
	 */
	phys_addr_t desc_phy_base;
	/** Abosolute IOVA address of desc of desc_phy_base. */
	dma_addr_t desc_iova;
};

/** @brief init data structure to be used for tegra_pcie_dma_init() API */
struct tegra_pcie_dma_init_info {
	/** configuration details for dma Tx channels */
	struct tegra_pcie_dma_chans_info tx[TEGRA_PCIE_DMA_WR_CHNL_NUM];
	/** configuration details for dma Rx channels */
	struct tegra_pcie_dma_chans_info rx[TEGRA_PCIE_DMA_RD_CHNL_NUM];
	/**
	 * If remote dma pointer is not NULL, then library uses remote DMA engine for transfers
	 * else uses local controller DMA engine.
	 */
	struct tegra_pcie_dma_remote_info *remote;
	/**
	 * device node for corresponding dma controller.
	 * This contains &pci_dev.dev pointer of RP's pci_dev for RP DMA write.
	 * This contains &pci_dev.dev pointer of EP's pci_dev for EP remote DMA read.
	 * This contains EP core dev pointer for EP DMA write.
	 */
	struct device *dev;
	/** SoC for which DMA needs to be initialized */
	nvpcie_dma_soc_t soc;
	/**
	 * MSI IRQ number for getting DMA interrupts.
	 * Note:
	 * - This param is applicable only for T264 SoC and ignored for T234 SoC.
	 * - IRQ allocation notes:
	 *  - For RP SoC, pcieport driver pre allocates MSI using pci_alloc_irq_vectors() and
	 *    irq vector for the same is TEGRA_PCIE_DMA_MSI_IRQ_VEC.
	 *  - For EP Soc allocate using platform_msi_domain_alloc_irqs()
	 *
	 */
	uint32_t msi_irq;
	/**
	 * MSI data that needs to be configured in DMA registers.
	 * Note:
	 * - Applicable to MSI interrupt only.
	 * - For EP SoC, this data is not available during init, hence pass this data
	 *   through tegra_pcie_dma_set_msi().
	 */
	uint32_t msi_data;
	/**
	 * MSI address that needs to be configured on DMA registers
	 * Note:
	 * - Applicable to MSI interrupt only.
	 * - For T264 EP SoC, this data is not available during init, hence pass this data
	 *   through tegra_pcie_dma_set_msi().
	 */
	uint64_t msi_addr;
};

/** @brief dma descriptor for data transfer operations */
struct tegra_pcie_dma_desc {
	/** source address of data buffer */
	dma_addr_t src;
	/** destination address where data buffer needs to be transferred */
	dma_addr_t dst;
	/** Size of data buffer */
	uint32_t sz;
};

/** @brief data strcuture needs to be passed for Tx operations */
struct tegra_pcie_dma_xfer_info {
	/** Read or write operation. */
	tegra_pcie_dma_xfer_type_t type;
	/** Channel on which operation needs to be performed.
	 *  Range 0 to (TEGRA_PCIE_DMA_RD_CHNL_NUM-1)/(TEGRA_PCIE_DMA_WR_CHNL_NUM-1)
	 */
	uint8_t channel_num;
	/** DMA descriptor structure with source, destination DMA addr along with its size. */
	struct tegra_pcie_dma_desc *desc;
	/** Number of desc entries. */
	uint8_t nents;
	/** complete callback to be called */
	tegra_pcie_dma_complete_t *complete;
	/** Caller's private data pointer which will be passed as part of dma_complete_t */
	void *priv;
};

#ifdef CONFIG_PCIE_TEGRA_DMA
/**
 * @brief API to perform DMA library initialization.
 *
 * @param[in] info DMA init data structure. Refer struct tegra_pcie_dma_init_info for details.
 * @param[out] cookie DMA cookie double pointer. Must be non NULL. This is populated by API for use
 * in further API calls.
 *
 * @retVal TEGRA_PCIE_DMA_SUCCESS, TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS, TEGRA_PCIE_DMA_FAIL_NOMEM from
 * tegra_pcie_dma_status_t.
 */
tegra_pcie_dma_status_t tegra_pcie_dma_initialize(struct tegra_pcie_dma_init_info *info,
						  void **cookie);

/* @brief API to set MSI addr and data for EPF driver.
 * Need to call this after initialize API for T264 EP contorller only.
 *
 * @param[in] cookie Value at cookie pointer populated in tegra_pcie_dma_initialize().
 * @param[in] msi_addr Refer tegra_pcie_dma_init_info$msi_addr
 * @param[in] msi_data Refer tegra_pcie_dma_init_info$msi_data
 *
 * @retVal TEGRA_PCIE_DMA_SUCCESS, TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS from tegra_pcie_dma_status_t.
 */
tegra_pcie_dma_status_t tegra_pcie_dma_set_msi(void *cookie, u64 msi_addr, u32 msi_data);

/**
 * @brief API to perform transfer operation.
 * @param[in] tx_info DMA Tx data structure. Refer struct tegra_pcie_dma_xfer_info for details.
 * @param[in] cookie Value at cookie pointer populated in tegra_pcie_dma_initialize().
 * @retVal Refer enum tegra_pcie_dma_status_t.
 */
tegra_pcie_dma_status_t tegra_pcie_dma_submit_xfer(void *cookie,
						   struct tegra_pcie_dma_xfer_info *tx_info);

/**
 * @brief API to stop DMA engine,.
 * @param[in] cookie Value at cookie pointer populated in tegra_pcie_dma_initialize().
 * @retVal Returns true on success and false on failure.
 */
/* Note stop API was needed to handle dangling pointer of cookie in EDMA driver. With usage of
 * double pointer cookie in tegra_pcie_dma_initialize(), this API can be deprecated.
 */
bool tegra_pcie_dma_stop(void *cookie);

/**
 * @brief API to perform de-init of DMA library.
 *
 * @param[inout] cookie pointer returned in tegra_pcie_dma_initialize() call. Set *cookie to
 * NULL on successful deinit.
 *
 * @retVal TEGRA_PCIE_DMA_SUCCESS, TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS from tegra_pcie_dma_status_t.
 */
tegra_pcie_dma_status_t tegra_pcie_dma_deinit(void **cookie);
#else
static inline tegra_pcie_dma_status_t
		tegra_pcie_dma_initialize(struct tegra_pcie_dma_init_info *info, void **cookie) {
	return -EOPNOTSUPP;
}

static inline tegra_pcie_dma_status_t tegra_pcie_dma_set_msi(void *cookie, u64 msi_addr,
							     u32 msi_data) {
	return -EOPNOTSUPP;
}

static inline tegra_pcie_dma_status_t
		tegra_pcie_dma_submit_xfer(void *cookie, struct tegra_pcie_dma_xfer_info *tx_info) {
	return -EOPNOTSUPP;
}

static inline bool tegra_pcie_dma_stop(void *cookie)
{
	return -EOPNOTSUPP;
}

static inline tegra_pcie_dma_status_t tegra_pcie_dma_deinit(void **cookie)
{
	return -EOPNOTSUPP;
}
#endif

#endif //TEGRA_PCIE_DMA_H
