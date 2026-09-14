/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
 *
 * PVA carveout handling
 */

#ifndef PVA_FW_CO_H
#define PVA_FW_CO_H

struct nvpva_carveout_info {
	dma_addr_t	base;
	dma_addr_t	base_pa;
	void		*base_va;
	size_t		size;
	bool		initialized;
};

struct nvpva_carveout_info *pva_fw_co_get_info(struct pva *pva);
bool pva_fw_co_initialized(struct pva *pva);

#endif
