/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2022-2025, NVIDIA CORPORATION. All rights reserved. */

#ifndef TEGRA_PCIE_DMA_H
#define TEGRA_PCIE_DMA_H

#ifndef DOXYGEN_ICD
/**
 * @brief
 * Number of read channels supported.
 * NVPCIE_DMA_SOC_T264 supports up to 4 channels and NVPCIE_DMA_SOC_T234 supports up to 2 channels.
 * DMA library ignore channel 2 and 3 arguments for NVPCIE_DMA_SOC_T234 and returns error from
 * <a href="#tegra-pcie-dma-submit-xfer">(tegra_pcie_dma_submit_xfer())</a> if
 * these channels are used.
 */
#define TEGRA_PCIE_DMA_RD_CHNL_NUM	4
#endif

/**
 * @brief
 * Number of write channels supported.
 */
#define TEGRA_PCIE_DMA_WR_CHNL_NUM	4
/** Size of DMA descriptor to allocate */
#define TEGRA_PCIE_DMA_DESC_SZ		32

/** MSI IRQ vector number to use on NVPCIE_DMA_SOC_T264 SoC for genrating local interrupt */
#define TEGRA264_PCIE_DMA_MSI_LOCAL_VEC		4
#ifndef DOXYGEN_ICD
/** MSI IRQ vector number to use on NVPCIE_DMA_SOC_T264 SoC for generating remote interrupt */
#define TEGRA264_PCIE_DMA_MSI_REMOTE_VEC	5
#endif

/**
 * @brief typedef to define various values for xfer status passed for dma_complete_t or
 * <a href="#tegra-pcie-dma-submit-xfer">(tegra_pcie_dma_submit_xfer())</a>
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
	 * Status set when DMA is de-initalized via
	 * <a href="#tegra-pcie-dma-deinit">(tegra_pcie_dma_deinit())</a>, during
	 * <a href="#tegra-pcie-dma-submit-xfer">(tegra_pcie_dma_submit_xfer())</a> or already
	 * intiialized via <a href="#tegra-pcie-dma-initialize">(tegra_pcie_dma_initialize())</a>
	 */
	TEGRA_PCIE_DMA_DEINIT,
	/**
	 * Status set when API is called in wrong operation mode or when pre-condition of API is
	 * not met.
	 */
	TEGRA_PCIE_DMA_STATUS_INVAL_STATE,
} tegra_pcie_dma_status_t;

/** @brief typedef to define various values for xfer type passed
 *  <a href="#tegra-pcie-dma-submit-xfer">(tegra_pcie_dma_submit_xfer())</a>
 */
typedef enum {
	TEGRA_PCIE_DMA_WRITE = 0,
#ifndef DOXYGEN_ICD
	TEGRA_PCIE_DMA_READ,
#endif
} tegra_pcie_dma_xfer_type_t;

/**
 * @brief typedef to define various channel type for DMA
 */
typedef enum {
#ifndef DOXYGEN_ICD
	TEGRA_PCIE_DMA_CHAN_XFER_SYNC = 0,
#endif
	TEGRA_PCIE_DMA_CHAN_XFER_ASYNC,
} tegra_pcie_dma_chan_type_t;

/**
 * @brief typedef to define various supported DMA controller SoCs for channel configuration done
 * in <a href="#tegra-pcie-dma-initialize">(tegra_pcie_dma_initialize())</a>
 */
typedef enum {
	NVPCIE_DMA_SOC_T234 = 0,
	NVPCIE_DMA_SOC_T264,
} nvpcie_dma_soc_t;

/** Forward declaration */
struct tegra_pcie_dma_desc;

/** @brief Tx Async callback function pointer */
typedef void (tegra_pcie_dma_complete_t)(void *priv, tegra_pcie_dma_status_t status);

#ifndef DOXYGEN_ICD
/** @brief
 * Remote DMA controller details.
 */
struct tegra_pcie_dma_remote_info {
	/**
	 * EP's DMA PHY base address, which is BAR0 for NVPCIE_DMA_SOC_T264 and BAR4 for
	 * NVPCIE_DMA_SOC_T234
	 */
	phys_addr_t dma_phy_base;
	/**
	 * EP's DMA register spec size, which is BAR0 size for NVPCIE_DMA_SOC_T264 and BAR4 size
	 * for NVPCIE_DMA_SOC_T234
	 */
	uint32_t dma_size;
};
#endif

/** @brief details of DMA Tx channel configuration */
struct tegra_pcie_dma_chans_info {
	/** Variable to specify if corresponding channel type should run. */
	tegra_pcie_dma_chan_type_t ch_type;
	/** Number of descriptors that needs to be configured for this channel. Max value 32K.
	 *  @note
	 *   - If 0 is passed, this channel will be treated un-used.
	 *   - else it must be power of 2.
	 */
	uint32_t num_descriptors;
#ifndef DOXYGEN_ICD
	/* Below parameter are used, only if remote is present in #tegra_pcie_dma_init_info */
	/**
	 * Descriptor PHY base allocated by client which is part of BAR0 in NVPCIE_DMA_SOC_T234 and
	 * BAR1 in T264. Each descriptor is of size TEGRA_PCIE_DMA_DESC_SZ bytes.
	 */
	phys_addr_t desc_phy_base;
	/** Abosolute IOVA address of desc of desc_phy_base. */
	dma_addr_t desc_iova;
#endif
};

/** @brief init data structure to be used for tegra_pcie_dma_init() API */
struct tegra_pcie_dma_init_info {
	/** configuration details for dma Tx channels */
	struct tegra_pcie_dma_chans_info tx[TEGRA_PCIE_DMA_WR_CHNL_NUM];
#ifndef DOXYGEN_ICD
	/** configuration details for dma Rx channels */
	struct tegra_pcie_dma_chans_info rx[TEGRA_PCIE_DMA_RD_CHNL_NUM];
	/**
	 * If remote dma pointer is not NULL, then library uses remote DMA engine for transfers
	 * else uses local controller DMA engine.
	 */
	struct tegra_pcie_dma_remote_info *remote;
#endif
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
	 * - This param is applicable only for NVPCIE_DMA_SOC_T264 SoC and ignored
	 *   for NVPCIE_DMA_SOC_T234 SoC.
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
	 * - For NVPCIE_DMA_SOC_T264 EP SoC, this data is not available during init,
	 *   hence pass this data through tegra_pcie_dma_set_msi().
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

#ifdef DOXYGEN_ICD
/**
 * @dir
 * - forward
 */
#endif
/**
 * @brief
 * API to perform DMA SW and HW initialization.
 *
 * @calib
 *  For NVPCIE_DMA_SOC_T264 in info->soc, "xdma" reg-name in "dev" parameter from
 *  struct tegra_pcie_dma_init_info is used. Refer CAL_NET_PIF$CalPcieEpLinux.
 *
 * @usage
 * - Allowed context for the API call
 *  - Interrupt: No
 *  - Signal handler: No
 *  - Thread-Safe: No
 *  - Sync/Asyc: Sync
 *  - Re-entrant: No
 * - Required Privileges:
 *  - Memory mapping access to PCIe DMA HW register.
 * - Operation Mode
 *  - Initialization: Yes
 *  - Run time: Yes
 *  - De-initialization: Yes
 *
 * @param[in] info DMA init data structure. Refer struct tegra_pcie_dma_init_info for details.
 * @param[out] cookie DMA cookie double pointer. Must be non NULL. This is populated by API for use
 * in further API calls.
 *
 * @return
 * - TEGRA_PCIE_DMA_SUCCESS, TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS, TEGRA_PCIE_DMA_FAIL_NOMEM from
 * <a href="#tegra-pcie-dma-status-t">(tegra_pcie_dma_status_t)</a>.
 */
tegra_pcie_dma_status_t tegra_pcie_dma_initialize(struct tegra_pcie_dma_init_info *info,
						  void **cookie);

#ifdef DOXYGEN_ICD
/**
 * @dir
 * - forward
 */
#endif
/**
 * @brief
 * API to set MSI addr and data.
 *
 * @usage
 * - Allowed context for the API call
 *  - Interrupt: No
 *  - Signal handler: No
 *  - Thread-Safe: No
 *  - Sync/Asyc: Sync
 *  - Re-entrant: No
 * - Required Privileges:
 *  - Memory mapping access to PCIe DMA HW register.
 * - Operation Mode
 *  - Initialization: Yes
 *  - Run time: Yes
 *  - De-initialization: Yes
 *
 * @pre
 * Need to call this after <a href="#tegra-pcie-dma-initialize">(tegra_pcie_dma_initialize())</a>
 * API and applicable for NVPCIE_DMA_SOC_T264 contorller in EP mode only.
 *
 * @param[in] cookie Value at cookie pointer populated in
 * <a href="#tegra-pcie-dma-initialize">(tegra_pcie_dma_initialize())</a>.
 * @param[in] msi_addr Refer tegra_pcie_dma_init_info$msi_addr
 * @param[in] msi_data Refer tegra_pcie_dma_init_info$msi_data
 *
 * @return
 * - TEGRA_PCIE_DMA_SUCCESS, TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS from
 * <a href="#tegra-pcie-dma-status-t">(tegra_pcie_dma_status_t)</a>.
 */
tegra_pcie_dma_status_t tegra_pcie_dma_set_msi(void *cookie, u64 msi_addr, u32 msi_data);

#ifdef DOXYGEN_ICD
/**
 * @dir
 * - forward
 */
#endif
/**
 * @brief
 * API to perform transfer operation by populating requested data to DMA descriptors and
 * triggerring the transfer.
 *
 * @usage
 * - Allowed context for the API call
 *  - Interrupt: No
 *  - Signal handler: No
 *  - Thread-Safe: No
 *  - Sync/Asyc: Sync
 *  - Re-entrant: No
 * - Required Privileges: None
 * - Operation Mode
 *  - Initialization: Yes
 *  - Run time: Yes
 *  - De-initialization: Yes
 *
 * @param[in] tx_info DMA Tx data structure. Refer struct tegra_pcie_dma_xfer_info for details.
 * @param[in] cookie Value at cookie pointer populated in
 * <a href="#tegra-pcie-dma-initialize">(tegra_pcie_dma_initialize())</a>.
 *
 * @return
 * - Refer enum <a href="#tegra-pcie-dma-status-t">(tegra_pcie_dma_status_t)</a>.
 */
tegra_pcie_dma_status_t tegra_pcie_dma_submit_xfer(void *cookie,
						   struct tegra_pcie_dma_xfer_info *tx_info);

#ifdef DOXYGEN_ICD
/**
 * @dir
 * - forward
 */
#endif
/**
 * @brief
 * API to stop all on going DMA transfers and accepting new transfers.
 *
 * @usage
 * - Allowed context for the API call
 *  - Interrupt: No
 *  - Signal handler: No
 *  - Thread-Safe: No
 *  - Sync/Asyc: Sync
 *  - Re-entrant: No
 * - Required Privileges: None
 * - Operation Mode
 *  - Initialization: Yes
 *  - Run time: Yes
 *  - De-initialization: Yes
 *
 * @param[in] cookie Value at cookie pointer populated in
 * <a href="#tegra-pcie-dma-initialize">(tegra_pcie_dma_initialize())</a>.
 *
 * @return Returns true on success and false on failure.
 *
 * @post
 * - Only <a href="#tegra-pcie-dma-deinit">(tegra_pcie_dma_deinit())</a> is accessible after
 *   this API is success.
 */
/* Note stop API was needed to handle dangling pointer of cookie in EDMA driver. With usage of
 * double pointer cookie in <a href="#tegra-pcie-dma-initialize">(tegra_pcie_dma_initialize())</a>,
 * this API can be deprecated.
 */
bool tegra_pcie_dma_stop(void *cookie);

#ifdef DOXYGEN_ICD
/**
 * @dir
 * - forward
 */
#endif
/**
 * @brief API to perform de-init of DMA library.
 *
 * @usage
 * - Allowed context for the API call
 *    - Interrupt: No
 *    - Signal handler: No
 *    - Thread-Safe: No
 *    - Sync/Asyc: Sync
 *    - Re-entrant: No
 * - Required Privileges: None
 * - Operation Mode
 *  - Initialization: Yes
 *  - Run time: Yes
 *  - De-initialization: Yes
 *
 * @param[inout] cookie pointer returned in
 * <a href="#tegra-pcie-dma-initialize">(tegra_pcie_dma_initialize())</a> call. Set *cookie to
 * NULL on successful deinit.
 *
 * @return
 * - TEGRA_PCIE_DMA_SUCCESS, TEGRA_PCIE_DMA_FAIL_INVAL_INPUTS from
 *   <a href="#tegra-pcie-dma-status-t">(tegra_pcie_dma_status_t)</a>.
 *
 * @outcome
 * - DMA HW is stopped.
 */
tegra_pcie_dma_status_t tegra_pcie_dma_deinit(void **cookie);

#endif //TEGRA_PCIE_DMA_H
