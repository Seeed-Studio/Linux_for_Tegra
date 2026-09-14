// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2025, NVIDIA CORPORATION.  All rights reserved. */

#ifndef TEGRA_PCIE_DMA_IRQ_H
#define TEGRA_PCIE_DMA_IRQ_H

#ifdef DOXYGEN_ICD
/**
 * @dir
 * - forward
 */
#endif
/**
 * @brief
 * Interrupt callback API to handle PCIe DMA interrupt.
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
 *  - IRQ_WAKE_THREAD - always
 *
 * @outcome
 * - Triggers call to <a href="#tegra-pcie-dma-irq-handler">(tegra_pcie_dma_irq_hadnler())</a>
 */
irqreturn_t tegra_pcie_dma_irq(int irq, void *arg);

#ifdef DOXYGEN_ICD
/**
 * @dir
 * - forward
 */
#endif
/**
 * @brief
 * Threaded interrupt Callback API for PCIe DMA controller interrupt.
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
 *  - IRQ_DONE - always
 *
 * @outcome
 * - PCIe DMA IRQ is processed and status is notified via
 *   <a href="#tegra-pcie-dma-complete-t">(tegra_pcie_dma_complete_t())</a>.
 *
 */
irqreturn_t tegra_pcie_dma_irq_handler(int irq, void *arg);

#endif /* TEGRA_PCIE_DMA_IRQ_H */
