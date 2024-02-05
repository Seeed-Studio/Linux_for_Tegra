/* SPDX-License-Identifier: GPL-2.0-only
 * SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * PCIe EDMA Framework
 */

#ifndef PCI_EPF_WRAPPER_H
#define PCI_EPF_WRAPPER_H

#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0))
#define lpci_epf_free_space(A, B, C)	pci_epf_free_space(A, B, C, PRIMARY_INTERFACE)
#define lpci_epf_alloc_space(A, B, C, D)	pci_epf_alloc_space(A, B, C, D, PRIMARY_INTERFACE)
#else
#define lpci_epf_free_space(A, B, C)	pci_epf_free_space(A, B, C)
#define lpci_epf_alloc_space(A, B, C, D)	pci_epf_alloc_space(A, B, C, D)
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION(5, 14, 0))
#define lpci_epc_write_header(A, B, C)	pci_epc_write_header(A, B, 0, C)
#define lpci_epc_raise_irq(A, B, C, D)	pci_epc_raise_irq(A, B, 0, C, D)
#define lpci_epc_clear_bar(A, B, C)	pci_epc_clear_bar(A, B, 0, C)
#define lpci_epc_set_msi(A, B, C)	pci_epc_set_msi(A, B, 0, C)
#define lpci_epc_set_bar(A, B, C)	pci_epc_set_bar(A, B, 0, C)
#define lpci_epc_map_addr(A, B, C, D, E)	pci_epc_map_addr(A, B, 0, C, D, E)
#define lpci_epc_unmap_addr(A, B, C)	pci_epc_unmap_addr(A, B, 0, C)
#else
#define lpci_epc_write_header(A, B, C)	pci_epc_write_header(A, B, C)
#define lpci_epc_raise_irq(A, B, C, D)	pci_epc_raise_irq(A, B, C, D)
#define lpci_epc_clear_bar(A, B, C)	pci_epc_clear_bar(A, B, C)
#define lpci_epc_set_msi(A, B, C)	pci_epc_set_msi(A, B, C)
#define lpci_epc_set_bar(A, B, C)	pci_epc_set_bar(A, B, C)
#define lpci_epc_map_addr(A, B, C, D, E)	pci_epc_map_addr(A, B, C, D, E)
#define lpci_epc_unmap_addr(A, B, C)	pci_epc_unmap_addr(A, B, C)
#endif /* LINUX_VERSION_CODE */

#endif /* PCI_EPF_WRAPPER_H */
