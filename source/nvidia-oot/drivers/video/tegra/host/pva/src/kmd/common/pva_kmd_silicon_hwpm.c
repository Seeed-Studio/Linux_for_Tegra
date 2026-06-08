// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#include "pva_kmd_device.h"
#include "pva_kmd_silicon_hwpm.h"
#include "pva_kmd_silicon_utils.h"

#ifndef TEGRA_SOC_HWPM_IP_REG_OP_READ
#define TEGRA_SOC_HWPM_IP_REG_OP_READ 0x1
#endif
#ifndef TEGRA_SOC_HWPM_IP_REG_OP_WRITE
#define TEGRA_SOC_HWPM_IP_REG_OP_WRITE 0x2
#endif
int pva_kmd_hwpm_ip_reg_op(void *ip_dev, uint32_t reg_op,
			   uint32_t inst_element_index, uint64_t reg_offset,
			   uint32_t *reg_data)
{
	struct pva_kmd_device *pva = ip_dev;

	if (reg_offset > UINT32_MAX)
		return PVA_INVAL;

	switch (reg_op) {
	case TEGRA_SOC_HWPM_IP_REG_OP_READ:
		*reg_data =
			pva_kmd_read(pva, safe_addu32(pva->regspec.cfg_perf_mon,
						      (uint32_t)reg_offset));
		break;
	case TEGRA_SOC_HWPM_IP_REG_OP_WRITE:
		pva_kmd_write(pva,
			      safe_addu32(pva->regspec.cfg_perf_mon,
					  (uint32_t)reg_offset),
			      *reg_data);
		break;
	default:
		pva_kmd_log_err("Invalid HWPM operation");
		return PVA_INVAL;
	}

	return PVA_SUCCESS;
}

int pva_kmd_hwpm_ip_pm(void *ip_dev, bool disable)
{
	struct pva_kmd_device *dev = ip_dev;
	enum pva_error err = PVA_SUCCESS;
	int ret = 0;

	if (disable) {
		err = pva_kmd_device_busy(dev);
		if (err != PVA_SUCCESS) {
			pva_kmd_log_err("Failed to busy");
		}
	} else {
		pva_kmd_device_idle(dev);
	}

	if (err != PVA_SUCCESS) {
		ret = -1;
	}

	return ret;
}