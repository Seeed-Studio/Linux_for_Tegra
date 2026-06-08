// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/io.h>
#include "nvpps_t23x.h"

/* T23X Registers */
#define T23X_TSC_STSCRSR_OFFSET									0x104
#define T23X_TSC_CAPTURE_CONFIGURATION_PTX_OFFSET				(T23X_TSC_STSCRSR_OFFSET + 0x58)
#define T23X_TSC_CAPTURE_CONTROL_PTX_OFFSET						(T23X_TSC_STSCRSR_OFFSET + 0x5c)
#define T23X_TSC_LOCKING_CONFIGURATION_OFFSET					(T23X_TSC_STSCRSR_OFFSET + 0xe4)
#define T23X_TSC_LOCKING_CONTROL_OFFSET							(T23X_TSC_STSCRSR_OFFSET + 0xe8)
#define T23X_TSC_LOCKING_STATUS_OFFSET							(T23X_TSC_STSCRSR_OFFSET + 0xec)
#define T23X_TSC_LOCKING_REF_FREQUENCY_CONFIGURATION_OFFSET		(T23X_TSC_STSCRSR_OFFSET + 0xf0)
#define T23X_TSC_LOCKING_DIFF_CONFIGURATION_OFFSET				(T23X_TSC_STSCRSR_OFFSET + 0xf4)
#define T23X_TSC_LOCKING_ADJUST_CONFIGURATION_OFFSET			(T23X_TSC_STSCRSR_OFFSET + 0x108)
#define T23X_TSC_LOCKING_FAST_ADJUST_CONFIGURATION_OFFSET		(T23X_TSC_STSCRSR_OFFSET + 0x10c)
#define T23X_TSC_LOCKING_ADJUST_NUM_CONTROL_OFFSET				(T23X_TSC_STSCRSR_OFFSET + 0x110)
#define T23X_TSC_LOCKING_ADJUST_DELTA_CONTROL_OFFSET			(T23X_TSC_STSCRSR_OFFSET + 0x114)


#define T23X_TSC_LOCKING_FAST_ADJUST_CONFIGURATION_OFFSET_THRSLD_SHIFT	16U
#define T23X_TSC_LOCKING_FAST_ADJUST_CONFIGURATION_OFFSET_K_INT_SHIFT	8U
#define T23X_TSC_LOCKING_FAST_ADJUST_CONFIGURATION_OFFSET_M_SHIFT		4U
#define T23X_TSC_LOCKING_FAST_ADJUST_CONFIGURATION_OFFSET_EN_SHIFT		0U

#define T23X_TSC_LOCKED_STATUS_BIT_OFFSET				1U
#define T23X_TSC_ALIGNED_STATUS_BIT_OFFSET				0U
#define T23X_TSC_LOCK_CTRL_ALIGN_BIT_OFFSET				0U

#define T23X_DEFAULT_REF_FREQ_INC_1S			0x1DCD650 /* 1s expressed in tick count */

#define T23X_MIN_LOCK_THRESHOLD_1US				0x1FU
#define T23X_MAX_LOCK_THRESHOLD_2MS				0xFFFFU


#define T23X_TSC_CAPTURE_CONFIG_SRC_SELECT_SHIFT		8U
#define T23X_TSC_CAPTURE_CONFIG_SRC_SELECT_MASK			0xffU

/* Default K_INT nominal value used to calculate gain factor */
#define T23X_DEFAULT_K_INT_NOM_VAL	371U

#define T23X_MAX_K_INT_VAL	0xFF
#define T23X_MAX_M_VAL		0x3

/* Number of MAC interface available to sync with TSC */
#define T23X_MAC_INTERFACE_CNT	5
/* MAC interface physical base addr */
#define T23X_EQOS_MAC_BASE_PA	0x2310000u
#define T23X_MGBE0_MAC_BASE_PA	0x6810000u
#define T23X_MGBE1_MAC_BASE_PA	0x6910000u
#define T23X_MGBE2_MAC_BASE_PA	0x6a10000u
#define T23X_MGBE3_MAC_BASE_PA	0x6b10000u

/* HW info mapping table */
typedef struct {
	uint64_t mac_base_pa;
	uint8_t tsc_src_idx;
} t23x_hw_info_t;

static const t23x_hw_info_t t23x_hw_info_arr[T23X_MAC_INTERFACE_CNT] = {
	{T23X_EQOS_MAC_BASE_PA, 0},
	{T23X_MGBE0_MAC_BASE_PA, 1},
	{T23X_MGBE1_MAC_BASE_PA, 2},
	{T23X_MGBE2_MAC_BASE_PA, 3},
	{T23X_MGBE3_MAC_BASE_PA, 4}
};

static int32_t nvpps_t23x_validate_input_params(struct soc_dev_data *soc_data)
{
	int32_t ret = -EINVAL;

	/* Check if PPS frequency is within valid range:
	 * - Supported frequencies are 1Hz, 2Hz and 4Hz
	 * - Value 0 is invalid (no PPS signal)
	 * - Value 3Hz is invalid (must be power of 2)
	 * - Values > 4Hz are invalid (hardware limitation)
	 */
	if ((soc_data->pps_freq > 4U) ||
		(soc_data->pps_freq == 0U) ||
		(soc_data->pps_freq == 3U)) { // supporting max allow value 1 to 4
		dev_err(soc_data->dev, "Invalid PPS(%uHz) freq input provided. Supported PPS frequencies are 1, 2, and 4Hz\n", soc_data->pps_freq);
		goto fail;
	}

	/* convert to usec */
	soc_data->lock_threshold_val = (soc_data->lock_threshold_val / 1000U);
	/* convert to Reg programmable value */
	soc_data->lock_threshold_val = (soc_data->lock_threshold_val * T23X_MIN_LOCK_THRESHOLD_1US);

	/* Check if lock threshold value is within valid range:
	 * Minimum: 0x1F (1us)
	 * Maximum: 0xFFFF (2.11ms)
	 */
	if ((soc_data->lock_threshold_val < T23X_MIN_LOCK_THRESHOLD_1US) ||
		(soc_data->lock_threshold_val > T23X_MAX_LOCK_THRESHOLD_2MS)) {
		dev_err(soc_data->dev,
				"Invalid input provided for ptp_tsc_lock_threshold. Refer binding doc for valid range\n");
		goto fail;
	}

	ret = 0;

fail:
	return ret;
}

static int32_t nvpps_t23x_ptp_tsc_sync_config(struct soc_dev_data *soc_data)
{
	uint32_t tsc_config_ptx_0;
	uint8_t tsc_src_idx;
	uint32_t k = 0, m = 0;
	int32_t ret = -EINVAL;
	uint32_t i = 0;

	if (nvpps_t23x_validate_input_params(soc_data)) {
		dev_err(soc_data->dev, "DT param validation failed\n");
		goto fail;
	}

	for (i = 0; i < T23X_MAC_INTERFACE_CNT; i++) {
		if (t23x_hw_info_arr[i].mac_base_pa == soc_data->pri_mac_base_pa) {
			tsc_src_idx = t23x_hw_info_arr[i].tsc_src_idx;
			break;
		}
	}

	if (i == T23X_MAC_INTERFACE_CNT) {
		dev_err(soc_data->dev, "Primary MAC provided is not listed as PPS source to TSC\n");
		goto fail;
	}

	/* k is calculated as below and is coded as k = (K_INT << M) */
	k = T23X_DEFAULT_K_INT_NOM_VAL * soc_data->pps_freq;

	while ((k > T23X_MAX_K_INT_VAL) && (m < T23X_MAX_M_VAL)) {
		k = k >> 1;
		m++;
	}

	//onetime config to init PTP TSC Sync logic
	writel(0x119, soc_data->reg_map_base + T23X_TSC_LOCKING_CONFIGURATION_OFFSET);
	writel(soc_data->lock_threshold_val, soc_data->reg_map_base + T23X_TSC_LOCKING_DIFF_CONFIGURATION_OFFSET);
	writel(0x1, soc_data->reg_map_base + T23X_TSC_LOCKING_CONTROL_OFFSET);
	writel((0x50001 | (k << T23X_TSC_LOCKING_FAST_ADJUST_CONFIGURATION_OFFSET_K_INT_SHIFT) |
			(m << T23X_TSC_LOCKING_FAST_ADJUST_CONFIGURATION_OFFSET_M_SHIFT)),
		   soc_data->reg_map_base + T23X_TSC_LOCKING_FAST_ADJUST_CONFIGURATION_OFFSET);
	writel(0x67, soc_data->reg_map_base + T23X_TSC_LOCKING_ADJUST_DELTA_CONTROL_OFFSET);
	writel(0x313, soc_data->reg_map_base + T23X_TSC_CAPTURE_CONFIGURATION_PTX_OFFSET);
	writel(0x1, soc_data->reg_map_base + T23X_TSC_STSCRSR_OFFSET);
	/* Configure LOCKING_REF_FREQ_CONFIG register */
	writel((T23X_DEFAULT_REF_FREQ_INC_1S/soc_data->pps_freq), soc_data->reg_map_base + T23X_TSC_LOCKING_REF_FREQUENCY_CONFIGURATION_OFFSET);

	tsc_config_ptx_0 = readl(soc_data->reg_map_base + T23X_TSC_CAPTURE_CONFIGURATION_PTX_OFFSET);
	/* clear and set the ptp src based on ethernet interface passed
	 * from dt for tsc to lock onto.
	 */
	tsc_config_ptx_0 = tsc_config_ptx_0 & ~(T23X_TSC_CAPTURE_CONFIG_SRC_SELECT_MASK << T23X_TSC_CAPTURE_CONFIG_SRC_SELECT_SHIFT);
	tsc_config_ptx_0 = tsc_config_ptx_0 | (tsc_src_idx << T23X_TSC_CAPTURE_CONFIG_SRC_SELECT_SHIFT);
	writel(tsc_config_ptx_0, soc_data->reg_map_base + T23X_TSC_CAPTURE_CONFIGURATION_PTX_OFFSET);
	tsc_config_ptx_0 = readl(soc_data->reg_map_base + T23X_TSC_CAPTURE_CONFIGURATION_PTX_OFFSET);

	ret = 0;

fail:
	return ret;
}

static bool nvpps_t23x_ptp_tsc_get_is_locked(struct soc_dev_data *soc_data)
{
	bool lck_sts = false;
	uint32_t reg_val = readl(soc_data->reg_map_base + T23X_TSC_LOCKING_STATUS_OFFSET);

	if ((reg_val & BIT(T23X_TSC_LOCKED_STATUS_BIT_OFFSET)) != 0) {
		lck_sts = true;
	}

	return lck_sts;
}

static void nvpps_t23x_ptp_tsc_synchronize(struct soc_dev_data *soc_data)
{
	writel((BIT(T23X_TSC_LOCKED_STATUS_BIT_OFFSET) | BIT(T23X_TSC_ALIGNED_STATUS_BIT_OFFSET)), soc_data->reg_map_base + T23X_TSC_LOCKING_STATUS_OFFSET);
	writel(BIT(T23X_TSC_LOCK_CTRL_ALIGN_BIT_OFFSET), soc_data->reg_map_base + T23X_TSC_LOCKING_CONTROL_OFFSET);
}

static int32_t nvpps_t23x_ptp_tsc_suspend_sync(struct soc_dev_data *soc_data)
{
	/* Stub implementation */
	return 0;
}

static int32_t nvpps_t23x_ptp_tsc_resume_sync(struct soc_dev_data *soc_data)
{
	/* Stub implementation */
	return 0;
}

static int32_t nvpps_t23x_get_tsc_res_ns(struct soc_dev_data *soc_data, uint64_t *tsc_res_ns)
{
	int32_t ret = -EINVAL;

	if (tsc_res_ns == NULL) {
		dev_err(soc_data->dev, "NULL input passed\n");
		goto fail;
	}

#define _PICO_SECS (1000000000000ULL)
	*tsc_res_ns = (_PICO_SECS / (u64)arch_timer_get_cntfrq()) / 1000;
#undef _PICO_SECS

	ret = 0;

fail:
	return ret;
}

static int32_t nvpps_t23x_get_monotonic_tsc_ts(struct soc_dev_data *soc_data, uint64_t *tsc_ts)
{
	int32_t ret = -EINVAL;

	if (tsc_ts == NULL) {
		dev_err(soc_data->dev, "NULL input passed\n");
		goto fail;
	}

	*tsc_ts = __arch_counter_get_cntvct();
	if (*tsc_ts == 0) {
		dev_err(soc_data->dev, "Invalid monotonic Time\n");
		goto fail;
	}

	ret = 0;

fail:
	return ret;
}

static int32_t nvpps_t23x_get_ptp_ts_ns(struct device_node *mac_node, uint64_t *ptp_ts)
{
	int32_t ret = -EINVAL;

	ret = tegra_get_hwtime(mac_node, ptp_ts, PTP_HWTIME);
	if (ret) {
		goto fail;
	}

	ret = 0;

fail:
	return ret;
}

static int32_t nvpps_t23x_get_ptp_tsc_concurrent_ts_ns(struct device_node *mac_node, struct ptp_tsc_data *data)
{
	int32_t ret = -EINVAL;

	ret = tegra_get_hwtime(mac_node, data, PTP_TSC_HWTIME);
	if (ret) {
		goto fail;
	}

	ret = 0;

fail:
	return ret;
}

const struct chip_ops tegra234_chip_ops = {
	.ptp_tsc_sync_cfg_fn = &nvpps_t23x_ptp_tsc_sync_config,
	.ptp_tsc_synchronize_fn = &nvpps_t23x_ptp_tsc_synchronize,
	.ptp_tsc_get_is_locked_fn = &nvpps_t23x_ptp_tsc_get_is_locked,
	.ptp_tsc_suspend_sync_fn = &nvpps_t23x_ptp_tsc_suspend_sync,
	.ptp_tsc_resume_sync_fn = &nvpps_t23x_ptp_tsc_resume_sync,
	.get_monotonic_tsc_ts_fn = &nvpps_t23x_get_monotonic_tsc_ts,
	.get_tsc_res_ns_fn = &nvpps_t23x_get_tsc_res_ns,
	.get_ptp_ts_ns_fn = &nvpps_t23x_get_ptp_ts_ns,
	.get_ptp_tsc_concurrent_ts_ns_fn = &nvpps_t23x_get_ptp_tsc_concurrent_ts_ns,
};
