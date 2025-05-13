/* SPDX-License-Identifier: GPL-2.0-only*/
/* SPDX-FileCopyrightText: Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.*/

#ifndef PCIE_TEGRAT264_EP_H
#define PCIE_TEGRAT264_EP_H

#ifdef DOXYGEN_ICD
/**
 * @dir
 * - forward
 */
#endif
/**
 * @brief
 * callback API from Linux System to perform Tegra264 SoC's PCIe EP controller
 * platform driver probe.
 *
 * @param[in] pdev Platform device associated with platform driver.
 *
 * @calib
 * - CAL_NET_PIF$CalPcieEp
 *
 * @usage
 * - Allowed context for the API call
 *  - Interrupt: No
 *  - Signal handler: No
 *  - Thread-Safe: No
 *  - Sync/Async: Sync
 *  - Re-entrant: No
 * - Required Privileges: None.
 * - Operation Mode
 *  - Initialization: Yes
 *  - Run time: No
 *  - De-initialization: No
 *
 * @return
 * - 0 on success
 * - "negative value" on failure.
 *
 */
static int tegra264_pcie_ep_probe(struct platform_device *pdev);

#ifdef DOXYGEN_ICD
/**
 * @dir
 * - forward
 */
#endif
/**
 * @brief
 * callback API from Linux System to perform Tegra264 SoC's PCIe EP controller
 * platform driver remove.
 *
 * @param[in] pdev Platform device associated with platform driver.
 *
 * @usage
 * - Allowed context for the API call
 *  - Interrupt: No
 *  - Signal handler: No
 *  - Thread-Safe: No
 *  - Sync/Async: Sync
 *  - Re-entrant: No
 * - Required Privileges: None.
 * - Operation Mode
 *  - Initialization: No
 *  - Run time: No
 *  - De-initialization: Yes
 *
 * @return
 * - 0 always
 *
 * @outcome
 * - EP controller driver is de-registered.
 * - EP controller is de-initialized(if initialized).
 */
#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void tegra264_pcie_ep_remove_wrapper(struct platform_device *pdev);
#else
static int tegra264_pcie_ep_remove_wrapper(struct platform_device *pdev);
#endif

#ifdef DOXYGEN_ICD
/**
 * @dir
 * - forward
 */
#endif
/**
 * @brief
 * callback API from Linux System to perform Tegra264 SoC's PCIe EP controller
 * driver suspend.
 *
 * @param[in] dev Device structure associated with driver.
 *
 * @usage
 * - Allowed context for the API call
 *  - Interrupt: No
 *  - Signal handler: No
 *  - Thread-Safe: No
 *  - Sync/Async: Sync
 *  - Re-entrant: No
 * - Required Privileges: None.
 * - Operation Mode
 *  - Initialization: No
 *  - Run time: No
 *  - De-initialization: Yes
 *
 * @return
 * - 0 on success
 * - -EPERM if PCIE EP controller is enabled.
 *
 */
static int tegra264_pcie_ep_suspend(struct device *dev);

#ifdef DOXYGEN_ICD
/**
 * @dir
 * - forward
 */
#endif
/**
 * @brief
 * callback API from Linux System to perform Tegra264 SoC's PCIe EP controller
 * driver resume.
 *
 * @param[in] dev Device structure associated with driver.
 *
 * @usage
 * - Allowed context for the API call
 *  - Interrupt: No
 *  - Signal handler: No
 *  - Thread-Safe: No
 *  - Sync/Async: Sync
 *  - Re-entrant: No
 * - Required Privileges: None.
 * - Operation Mode
 *  - Initialization: Yes
 *  - Run time: No
 *  - De-initialization: No
 *
 * @return
 * - 0 always
 *
 * @outcome
 * - Reset IRQ is enabled.
 */
static int tegra264_pcie_ep_resume(struct device *dev);

#ifdef DOXYGEN_ICD
/**
 * @dir
 * - forward
 */
#endif
/**
 * @brief
 * Callback API from PCIe EP controller framework to provide config space header info.
 *
 * @param[in] epc EP controller structure pointer. This is used to retrieve Drive specific
 * internal data structures.
 * @param[in] fn Function number. Unused parameter.
 * @param[in] vfn Virtual Function number. Unused parameter.
 * @param[in] hdr Pointer to the struct pci_epf_header containing header info.
 * Header information supported are:
 *  - LSB 7 bits of deviceid.
 *  - baseclass_code, subclass_code, progif_code.
 *  - Other not supported and ignored.
 *
 * @usage
 * - Allowed context for the API call
 *  - Interrupt: No
 *  - Signal handler: No
 *  - Thread-Safe: No
 *  - Sync/Async: Sync
 *  - Re-entrant: No
 * - Required Privileges: None.
 * - Operation Mode
 *  - Initialization: Yes
 *  - Run time: Yes
 *  - De-initialization: Yes
 *
 * @return
 * - 0 always.
 *
 * @pre
 * - Must be called before <a href="#tegra264-pcie-ep-start">(tegra264_pcie_ep_start())</a>.
 * - Failure to call will result in HW power on reset values for header info.
 *
 * @outcome
 * - Header information is configured during EP controller initialization.
 */
static int tegra264_pcie_ep_write_header(struct pci_epc *epc, u8 fn,
#if defined(NV_PCI_EPC_WRITE_HEADER_HAS_VFN_ARG)
					 u8 vfn,
#endif
					 struct pci_epf_header *hdr);

#ifdef DOXYGEN_ICD
/**
 * @dir
 * - forward
 */
#endif
/**
 * @brief
 * Callback API from PCIe EP controller framework to set Bar configuration.
 *
 * @param[in] epc EP controller structure pointer. This is used to retrieve Drive specific
 * internal data structures.
 * @param[in] fn Function number. Unused parameter.
 * @param[in] vfn Virtual Function number. Unused parameter.
 * @param[in] epf_bar Pointer to the struct pci_epf_bar containing BAR configuration.
 * Supported configuration are:
 *  - Only 64-bit pre-fetch memory type
 *  - Bar number 1 and 2.
 *  - Bar 1 must of minimum 64MB and Max 64GB, must be power of 2.
 *  - Bar 2 must be of size 32MB.
 *
 * @usage
 * - Allowed context for the API call
 *  - Interrupt: No
 *  - Signal handler: No
 *  - Thread-Safe: No
 *  - Sync/Async: Sync
 *  - Re-entrant: No
 * - Required Privileges: None.
 * - Operation Mode
 *  - Initialization: Yes
 *  - Run time: Yes
 *  - De-initialization: Yes
 *
 * @pre
 * - Must be called before <a href="#tegra264-pcie-ep-start">(tegra264_pcie_ep_start())</a>.
 * - Failure to call will result in
 *   <a href="#tegra264-pcie-ep-start">(tegra264_pcie_ep_start())</a> failure.
 *
 * @return
 * - 0 on If Bar details are in range..
 * - -EINVAL if Bar configuration is not supported or called once link is established.
 */
static int tegra264_pcie_ep_set_bar(struct pci_epc *epc, u8 fn,
#if defined(NV_PCI_EPC_WRITE_HEADER_HAS_VFN_ARG)
				    u8 vfn,
#endif
				    struct pci_epf_bar *epf_bar);

#ifdef DOXYGEN_ICD
/**
 * @dir
 * - forward
 */
#endif
/**
 * @brief
 * Callback API from PCIe EP controller framework to clear BAR configuration.
 *
 * @param[in] epc EP controller structure pointer. This is used to retrieve Drive specific
 * internal data structures.
 * @param[in] fn Function number. Unused parameter.
 * @param[in] vfn Virtual Function number. Unused parameter.
 * @param[in] epf_bar Pointer to the struct pci_epf_bar containing BAR configuration.
 * Supported configuration are:
 *  - BAR number 0 and 1.
 *
 * @usage
 * - Allowed context for the API call
 *  - Interrupt: No
 *  - Signal handler: No
 *  - Thread-Safe: No
 *  - Sync/Async: Sync
 *  - Re-entrant: No
 * - Required Privileges: None.
 * - Operation Mode
 *  - Initialization: Yes
 *  - Run time: Yes
 *  - De-initialization: Yes
 *
 * @outcome
 * - BAR mapping details of supported BAR number are cleared.
 * - No AI taken for unsupported BAR number or when called before controller
 *   is initialized.
 *
 */
static void tegra264_pcie_ep_clear_bar(struct pci_epc *epc, u8 fn,
#if defined(NV_PCI_EPC_WRITE_HEADER_HAS_VFN_ARG)
				       u8 vfn,
#endif
				       struct pci_epf_bar *epf_bar);

#ifdef DOXYGEN_ICD
/**
 * @dir
 * - forward
 */
#endif
/**
 * @brief
 * Callback API from PCIe EP controller framework to map outbound address to controller aperture.
 *
 * @param[in] epc EP controller structure pointer. This is used to retrieve Drive specific
 * internal data structures.
 * @param[in] func_no Function number. Unused parameter
 * @param[in] vfunc_no Virtual Function number. Unused parameter
 * @param[in] addr Host memory address to map.
 * @param[in] pci_addr Aperture controller address to map 'addr' parameter.
 * @param[in] size The size of the mapping.
 *
 * @usage
 * - Allowed context for the API call
 *  - Interrupt: No
 *  - Signal handler: No
 *  - Thread-Safe: No
 *  - Sync/Async: Sync
 *  - Re-entrant: No
 * - Required Privileges: None.
 * - Operation Mode
 *  - Initialization: Yes
 *  - Run time: Yes
 *  - De-initialization: Yes
 *
 * @return
 * - 0 on success.
 * - EINVAL if there are no aperture windows available to program the mapping or
 *   if called before controller is initialized.
 *
 * @outcome
 * - On success, outbound address is mapped to controller aperture and caller can access
 *   remote memory.
 */
static int tegra264_pcie_ep_map_addr(struct pci_epc *epc, u8 func_no,
#if defined(NV_PCI_EPC_WRITE_HEADER_HAS_VFN_ARG)
				     u8 vfunc_no,
#endif
				     phys_addr_t addr, u64 pci_addr, size_t size);

#ifdef DOXYGEN_ICD
/**
 * @dir
 * - forward
 */
#endif
/**
 * @brief
 * Callback API from PCIe EP controller framework to un-map outbound address to
 * controller aperture.
 *
 * @param[in] epc EP controller structure pointer. This is used to retrieve Drive specific
 * internal data structures.
 * @param[in] func_no Function number. Unused parameter
 * @param[in] vfunc_no Virtual Function number. Unused parameter
 * @param[in] addr Aperture controller address to unmap.
 *
 * @usage
 * - Allowed context for the API call
 *  - Interrupt: No
 *  - Signal handler: No
 *  - Thread-Safe: No
 *  - Sync/Async: Sync
 *  - Re-entrant: No
 * - Required Privileges: None.
 * - Operation Mode
 *  - Initialization: Yes
 *  - Run time: Yes
 *  - De-initialization: Yes
 *
 * @outcome
 * - If controller is initialized and Aperture is mapped, then mapping details in Tegra264 EP
 *   controller register is cleared. Else, no AI taken.
 */
static void tegra264_pcie_ep_unmap_addr(struct pci_epc *epc, u8 func_no,
#if defined(NV_PCI_EPC_WRITE_HEADER_HAS_VFN_ARG)
					u8 vfunc_no,
#endif
					phys_addr_t addr);

#ifndef DOXYGEN_ICD
/** MSI is expected not to be decomposed, hence no ICD documentation planned yet */
static int tegra264_pcie_ep_set_msi(struct pci_epc *epc, u8 fn,
#if defined(NV_PCI_EPC_WRITE_HEADER_HAS_VFN_ARG)
				    u8 vfn,
#endif
				    u8 mmc);

static int tegra264_pcie_ep_get_msi(struct pci_epc *epc,
#if defined(NV_PCI_EPC_WRITE_HEADER_HAS_VFN_ARG)
				    u8 fn, u8 vfn);
#else
				    u8 fn);
#endif
#endif

static int tegra264_pcie_ep_raise_irq(struct pci_epc *epc, u8 fn,
#if defined(NV_PCI_EPC_WRITE_HEADER_HAS_VFN_ARG)
				      u8 vfn,
#endif
#if defined(PCI_EPC_IRQ_TYPE_ENUM_PRESENT) /* Dropped from Linux 6.8 */
				      enum pci_epc_irq_type type,
#else
				      unsigned int type,
#endif
				      u16 irq_num);

#ifdef DOXYGEN_ICD
/**
 * @dir
 * - forward
 */
#endif
/**
 * @brief
 * Callback API from PCIe EP controller framework to start EP controller functionality.
 *
 * @param[in] epc EP controller structure pointer. This is used to retrieve Drive specific
 * internal data structures.
 *
 * @usage
 * - Allowed context for the API call
 *  - Interrupt: No
 *  - Signal handler: No
 *  - Thread-Safe: No
 *  - Sync/Async: Sync
 *  - Re-entrant: No
 * - Required Privileges: None.
 * - Operation Mode
 *  - Initialization: Yes
 *  - Run time: Yes
 *  - De-initialization: Yes
 *
 * @return
 * - 0 on success.
 * - -EINVAL if controller is failed to initialize
 *
 * @pre
 * - Must be called after <a href="#tegra264-pcie-ep-set-bar">(tegra264_pcie_ep_set_bar())</a>.
 *
 * @outcome
 * - Tegra264 EP controller is initialized, if
 *   - reset GPIO is already de-asserted.
 *   - pre-conditions met.
 * - pex-prsnt GPIO is de-asserted if specified in CAL_NET_PIF$CalPcieEp.
 * - pex-rst-irq is enabled.
 */
static int tegra264_pcie_ep_start(struct pci_epc *epc);

#ifdef DOXYGEN_ICD
/**
 * @dir
 * - forward
 */
#endif
/**
 * @brief
 * Callback API from PCIe EP controller framework to stop EP controller functionality.
 *
 * @param[in] epc EP controller structure pointer. This is used to retrieve Drive specific
 * internal data structures.
 *
 * @usage
 * - Allowed context for the API call
 *  - Interrupt: No
 *  - Signal handler: No
 *  - Thread-Safe: No
 *  - Sync/Async: Sync
 *  - Re-entrant: No
 * - Required Privileges: None.
 * - Operation Mode
 *  - Initialization: Yes
 *  - Run time: Yes
 *  - De-initialization: Yes
 *
 * @outcome
 * - pex-rst-irq is disabled.
 * - pex-prsnt GPIO is de-asserted if specified in CAL_NET_PIF$CalPcieEp.
 * - Tegra264 EP controller is de-initialized, if controller is initialized.
 */
static void tegra264_pcie_ep_stop(struct pci_epc *epc);

#ifdef DOXYGEN_ICD
/**
 * @dir
 * - forward
 */
#endif
/**
 * @brief
 * Interrupt Callback API for PCIe EP controller interrupt.
 *
 * @param[in] irq un-used data
 * @param[in] arg This is used to retrieve Drive specific internal data structures.
 *
 * @usage
 * - Allowed context for the API call
 *  - Interrupt: Yes
 *  - Signal handler: No
 *  - Thread-Safe: No
 *  - Sync/Async: Sync
 *  - Re-entrant: No
 * - Required Privileges: None.
 * - Operation Mode
 *  - Initialization: Yes
 *  - Run time: Yes
 *  - De-initialization: Yes
 *
 * @return
 *  - IRQ_WAKE_THREAD - If interrupt notifies about EP reset state
 *  - IRQ_HANDLED - everything else
 *
 */
static irqreturn_t tegra264_pcie_ep_irq_handler(int irq, void *arg);

#ifdef DOXYGEN_ICD
/**
 * @dir
 * - forward
 */
#endif
/**
 * @brief
 * Threaded interrupt Callback API for PCIe EP controller interrupt.
 *
 * @param[in] irq un-used data
 * @param[in] arg This is used to retrieve Drive specific internal data structures.
 *
 * @usage
 * - Allowed context for the API call
 *  - Interrupt: Yes
 *  - Signal handler: No
 *  - Thread-Safe: No
 *  - Sync/Async: Sync
 *  - Re-entrant: No
 * - Required Privileges: None.
 * - Operation Mode
 *  - Initialization: Yes
 *  - Run time: Yes
 *  - De-initialization: Yes
 *
 * @return
 *  - IRQ_HANDLED - always
 *
 * @outcome
 * - PCIe EP controller is de-initialized and initialized.
 *
 */
static irqreturn_t tegra264_pcie_ep_irq_thread(int irq, void *arg);

#ifdef DOXYGEN_ICD
/**
 * @dir
 * - forward
 */
#endif
/**
 * @brief
 * Interrupt Callback API for PCIe EP reset-gpio changes.
 *
 * @param[in] irq un-used data
 * @param[in] arg This is used to retrieve Drive specific internal data structures.
 *
 * @usage
 * - Allowed context for the API call
 *  - Interrupt: Yes
 *  - Signal handler: No
 *  - Thread-Safe: No
 *  - Sync/Async: Sync
 *  - Re-entrant: No
 * - Required Privileges: None.
 * - Operation Mode
 *  - Initialization: Yes
 *  - Run time: Yes
 *  - De-initialization: Yes
 *
 * @return
 *  - IRQ_HANDLED - always
 *
 * @outcome
 * - PCIe EP controller is de-initialized if reset GPIO status is asserted.
 * - PCIe EP controller is Initialized if reset GPIO status and pex-prsnt GPIO
 *   are de-asserted.
 *
 */
static irqreturn_t tegra264_pcie_ep_rst_irq(int irq, void *arg);
#endif /* PCIE_TEGRAT264_EP_H */
