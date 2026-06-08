// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/io.h>
#include "nvpps_t26x.h"

/* T26X Registers */
#define T26X_TSC_STSCRSR_OFFSET									0x104
#define T26X_TSC_CAPTURE_CONFIGURATION_PTX_OFFSET				(T26X_TSC_STSCRSR_OFFSET + 0x58)
#define T26X_TSC_CAPTURE_CONTROL_PTX_OFFSET						(T26X_TSC_STSCRSR_OFFSET + 0x5c)
#define T26X_TSC_LOCKING_CONFIGURATION_OFFSET					(T26X_TSC_STSCRSR_OFFSET + 0xe4)
#define T26X_TSC_LOCKING_CONTROL_OFFSET							(T26X_TSC_STSCRSR_OFFSET + 0xe8)
#define T26X_TSC_LOCKING_STATUS_OFFSET							(T26X_TSC_STSCRSR_OFFSET + 0xec)
#define T26X_TSC_LOCKING_REF_FREQUENCY_CONFIGURATION_OFFSET		(T26X_TSC_STSCRSR_OFFSET + 0xf0)
#define T26X_TSC_LOCKING_DIFF_CONFIGURATION_OFFSET				(T26X_TSC_STSCRSR_OFFSET + 0xfc)
//M field is 6:4, 6th bit is addition in Thor
#define T26X_TSC_LOCKING_FAST_ADJUST_CONFIGURATION_OFFSET		(T26X_TSC_STSCRSR_OFFSET + 0x128)
#define T26X_TSC_LOCKING_ADJUST_DELTA_CONTROL_OFFSET				(T26X_TSC_STSCRSR_OFFSET + 0x130)

#define T26X_TSC_LOCKING_CONFIG_PPS_SRC_PTX				0x1

#define T26X_TSC_LOCKING_CONFIG_SRC_SEL_SHIFT			8U
#define T26X_TSC_LOCKING_CONFIG_EDGE_SEL_SHIFT			4U
#define T26X_TSC_LOCKING_CONFIG_EN_SHIFT				0U

#define T26X_TSC_LOCKING_FAST_ADJUST_CONFIGURATION_OFFSET_THRSLD_SHIFT		16U
#define T26X_TSC_LOCKING_FAST_ADJUST_CONFIGURATION_OFFSET_K_INT_SHIFT		8U
#define T26X_TSC_LOCKING_FAST_ADJUST_CONFIGURATION_OFFSET_M_SHIFT			4U
#define T26X_TSC_LOCKING_FAST_ADJUST_CONFIGURATION_OFFSET_EN_SHIFT			0U

#define T26X_TSC_CAPTURE_CONFIG_EDGE_SHIFT				4U
#define T26X_TSC_CAPTURE_CONFIG_EN_SHIFT				0U

#define T26X_TSC_LOCKED_STATUS_BIT_OFFSET				1U
#define T26X_TSC_ALIGNED_STATUS_BIT_OFFSET				0U

#define T26X_TSC_LOCK_CTRL_ALIGN_BIT_OFFSET				0U

#define T26X_CAPTURE_CONFIG_SRC_SELECT_SHIFT			8U

#define CONFIG_RISING_EDGE		0x1

#define T26X_MIN_LOCK_THRESHOLD_1NS				0x1
#define T26X_MAX_LOCK_THRESHOLD_16MS			0xFFFFFF

/* DELTA INCREMENT value to add/remove(in slow convergence case) from current adjust value */
#define T26X_DEFAULT_DELTA_INC_STEP			0x67U
/* THRESHOLD used in determining when FAST ADJUST Convergence is to be applied by HW */
#define T26X_DEFAULT_FAST_ADJ_THRSLD		0x64U

/* Default K_INT nominal value used to calculate gain factor.
 * The actual float Knominal value is 2.485, for calculation purpose this is converted to int by multiplying with 1000
 */
#define T26X_DEFAULT_K_INT_NOM_VAL			2485U

#define T26X_DEFAULT_REF_FREQ_INC_1S		1000000000 /* 1s expressed in ns */

/* Number of MAC interface available to sync with TSC */
#define T26X_MAC_INTERFACE_CNT	5
/* MAC interface physical base addr */
#define T26X_EQOS_MAC_BASE_PA	0xa808910000
#define T26X_MGBE0_MAC_BASE_PA	0xa808a10000
#define T26X_MGBE1_MAC_BASE_PA	0xa808b10000
#define T26X_MGBE2_MAC_BASE_PA	0xa808d10000
#define T26X_MGBE3_MAC_BASE_PA	0xa808e10000

/* HW info mapping table */
typedef struct {
	uint64_t mac_base_pa;
	uint8_t tsc_src_idx;
} t26x_hw_info_t;

static const t26x_hw_info_t t26x_hw_info_arr[T26X_MAC_INTERFACE_CNT] = {
	{T26X_EQOS_MAC_BASE_PA, 0},
	{T26X_MGBE0_MAC_BASE_PA, 1},
	{T26X_MGBE1_MAC_BASE_PA, 2},
	{T26X_MGBE2_MAC_BASE_PA, 3},
	{T26X_MGBE3_MAC_BASE_PA, 4}
};

static int32_t nvpps_t26x_validate_input_params(struct soc_dev_data *soc_data)
{
	int32_t ret = -EINVAL;

	/* Check if PPS frequency is:
	 * - Greater than max allowed value of 8Hz
	 * - Value 0 is invalid (no PPS signal)
	 * - Value 3Hz, 6Hz, 7Hz are invalid (must be power of 2 or 5)
	 * Only frequencies of 1, 2, 4, 5 and 8Hz are supported
	 */
	if ((soc_data->pps_freq > 8U) ||
		(soc_data->pps_freq == 0U) ||
		(soc_data->pps_freq == 3U) ||
		(soc_data->pps_freq == 6U) ||
		(soc_data->pps_freq == 7U)) { // supporting max allow value 1 to 8
		dev_err(soc_data->dev,
				"Invalid PPS(%uHz) freq input provided. Supported PPS frequencies are 1, 2, 4, 5 and 8Hz\n", soc_data->pps_freq);
		goto fail;
	}

	/* Check if lock threshold value is within valid range:
	 * Minimum: 0x1 (1ns)
	 * Maximum: 0xFFFFFF (16ms)
	 */
	if ((soc_data->lock_threshold_val < T26X_MIN_LOCK_THRESHOLD_1NS) ||
		(soc_data->lock_threshold_val > T26X_MAX_LOCK_THRESHOLD_16MS)) {
		dev_err(soc_data->dev,
				"Invalid input provided for ptp_tsc_lock_threshold. Refer binding doc for valid range\n");
		goto fail;
	}

	ret = 0;

fail:
	return ret;
}

static int32_t nvpps_t26x_ptp_tsc_sync_config(struct soc_dev_data *soc_data)
{
	uint32_t reg_val = 0;
	uint32_t k = 0, i;
	uint8_t tsc_src_idx;
	int32_t ret = -EINVAL;

	if (nvpps_t26x_validate_input_params(soc_data)) {
		dev_err(soc_data->dev, "DT param validation failed\n");
		goto fail;
	}

	for (i = 0; i < T26X_MAC_INTERFACE_CNT; i++) {
		if (t26x_hw_info_arr[i].mac_base_pa == soc_data->pri_mac_base_pa) {
			tsc_src_idx = t26x_hw_info_arr[i].tsc_src_idx;
			break;
		}
	}

	if (i == T26X_MAC_INTERFACE_CNT) {
		dev_err(soc_data->dev, "Primary MAC provided is not listed as PPS source to TSC\n");
		goto fail;
	}

	/* Configure LOCKING_CONFIGURATION register */
	/* Select PTX as PPS Source & Rising edge as edge select */
	reg_val = ((T26X_TSC_LOCKING_CONFIG_PPS_SRC_PTX << T26X_TSC_LOCKING_CONFIG_SRC_SEL_SHIFT) |
			   (CONFIG_RISING_EDGE << T26X_TSC_LOCKING_CONFIG_EDGE_SEL_SHIFT));

	writel(reg_val, soc_data->reg_map_base + T26X_TSC_LOCKING_CONFIGURATION_OFFSET);

	/* Configure LOCKING_DIFF_CONFIGURATION register with lock threshold value */
	dev_info(soc_data->dev, "Using Lock threshold value(in ns) : %u\n", soc_data->lock_threshold_val);

	writel(soc_data->lock_threshold_val, soc_data->reg_map_base + T26X_TSC_LOCKING_DIFF_CONFIGURATION_OFFSET);


	/* Configure LOCKING_ADJSUT_DELTA_CONTROL register */
	writel(T26X_DEFAULT_DELTA_INC_STEP, soc_data->reg_map_base + T26X_TSC_LOCKING_ADJUST_DELTA_CONTROL_OFFSET);

	/* Configure LOCKING_FAST_ADJUST_CONFIG register */

	/* round off the calculated K_int value to nearest integer */
	k = ((((T26X_DEFAULT_K_INT_NOM_VAL * soc_data->pps_freq) % 1000) >= 500) ? 1 : 0);
	k = k + ((T26X_DEFAULT_K_INT_NOM_VAL * soc_data->pps_freq) / 1000);

	/* Set THRESHOLD, K_INT, M and ENABLE bits */
	reg_val = ((T26X_DEFAULT_FAST_ADJ_THRSLD << T26X_TSC_LOCKING_FAST_ADJUST_CONFIGURATION_OFFSET_THRSLD_SHIFT) |
			   (k << T26X_TSC_LOCKING_FAST_ADJUST_CONFIGURATION_OFFSET_K_INT_SHIFT) |
			   (0 << T26X_TSC_LOCKING_FAST_ADJUST_CONFIGURATION_OFFSET_M_SHIFT) | /* M = 0 always */
			   (BIT(T26X_TSC_LOCKING_FAST_ADJUST_CONFIGURATION_OFFSET_EN_SHIFT)));

	writel(reg_val, soc_data->reg_map_base + T26X_TSC_LOCKING_FAST_ADJUST_CONFIGURATION_OFFSET);

	/* Configure LOCKING_REF_FREQ_CONFIG register */

	writel((T26X_DEFAULT_REF_FREQ_INC_1S/soc_data->pps_freq), soc_data->reg_map_base + T26X_TSC_LOCKING_REF_FREQUENCY_CONFIGURATION_OFFSET);

	/* Configure CAPTURE_CONFIGURATION_PTX register */
	/* Select PPS src MAC */
	reg_val = (tsc_src_idx << T26X_CAPTURE_CONFIG_SRC_SELECT_SHIFT);
	/* Use PPS Rising EDGE to capture TSC values */
	reg_val = (reg_val | (CONFIG_RISING_EDGE << T26X_TSC_CAPTURE_CONFIG_EDGE_SHIFT));

	writel(reg_val, soc_data->reg_map_base + T26X_TSC_CAPTURE_CONFIGURATION_PTX_OFFSET);


	/* Configure CAPTURE_CONTROL register */
	/* Enable updating capture value registers on PPS edge */
	writel(0x1, soc_data->reg_map_base + T26X_TSC_CAPTURE_CONTROL_PTX_OFFSET);

	/* Configure T26X_TSC_LOCKING_CONFIG register */
	/* Enable HW LOCKING Mechanism */
	reg_val = readl(soc_data->reg_map_base + T26X_TSC_LOCKING_CONFIGURATION_OFFSET);
	reg_val = (reg_val | BIT(T26X_TSC_LOCKING_CONFIG_EN_SHIFT));
	writel(reg_val, soc_data->reg_map_base + T26X_TSC_LOCKING_CONFIGURATION_OFFSET);

	/* Configure T26X_TSC_CAPTURE_CONFIG register */
	/* Enable ability to feed the HW Lock mechanism */
	reg_val = readl(soc_data->reg_map_base + T26X_TSC_CAPTURE_CONFIGURATION_PTX_OFFSET);
	reg_val = (reg_val | BIT(T26X_TSC_CAPTURE_CONFIG_EN_SHIFT));
	writel(reg_val, soc_data->reg_map_base + T26X_TSC_CAPTURE_CONFIGURATION_PTX_OFFSET);

	reg_val = readl(soc_data->reg_map_base + T26X_TSC_CAPTURE_CONFIGURATION_PTX_OFFSET);

	ret = 0;

fail:
	return ret;
}

static bool nvpps_t26x_ptp_tsc_get_is_locked(struct soc_dev_data *soc_data)
{
	bool lck_sts = false;
	uint32_t reg_val = readl(soc_data->reg_map_base + T26X_TSC_LOCKING_STATUS_OFFSET);

	if ((reg_val & BIT(T26X_TSC_LOCKED_STATUS_BIT_OFFSET)) != 0) {
		lck_sts = true;
	}

	return lck_sts;
}

static void nvpps_t26x_ptp_tsc_synchronize(struct soc_dev_data *soc_data)
{
	writel((BIT(T26X_TSC_LOCKED_STATUS_BIT_OFFSET) | BIT(T26X_TSC_ALIGNED_STATUS_BIT_OFFSET)), soc_data->reg_map_base + T26X_TSC_LOCKING_STATUS_OFFSET);
	writel(BIT(T26X_TSC_LOCK_CTRL_ALIGN_BIT_OFFSET), soc_data->reg_map_base + T26X_TSC_LOCKING_CONTROL_OFFSET);
}

static int32_t nvpps_t26x_ptp_tsc_suspend_sync(struct soc_dev_data *soc_data)
{
	/* Stub implementation */
	return 0;
}

static int32_t nvpps_t26x_ptp_tsc_resume_sync(struct soc_dev_data *soc_data)
{
	/* Stub implementation */
	return 0;
}

static int32_t nvpps_t26x_get_tsc_res_ns(struct soc_dev_data *soc_data, uint64_t *tsc_res_ns)
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

static int32_t nvpps_t26x_get_monotonic_tsc_ts(struct soc_dev_data *soc_data, uint64_t *tsc_ts)
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

static int32_t nvpps_t26x_get_ptp_ts_ns(struct device_node *mac_node, uint64_t *ptp_ts)
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

static int32_t nvpps_t26x_get_ptp_tsc_concurrent_ts_ns(struct device_node *mac_node, struct ptp_tsc_data *data)
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

/* Define the tegra264_chip_data structure */
const struct chip_ops tegra264_chip_ops = {
	.ptp_tsc_sync_cfg_fn = &nvpps_t26x_ptp_tsc_sync_config,
	.ptp_tsc_synchronize_fn = &nvpps_t26x_ptp_tsc_synchronize,
	.ptp_tsc_get_is_locked_fn = &nvpps_t26x_ptp_tsc_get_is_locked,
	.ptp_tsc_suspend_sync_fn = &nvpps_t26x_ptp_tsc_suspend_sync,
	.ptp_tsc_resume_sync_fn = &nvpps_t26x_ptp_tsc_resume_sync,
	.get_monotonic_tsc_ts_fn = &nvpps_t26x_get_monotonic_tsc_ts,
	.get_tsc_res_ns_fn = &nvpps_t26x_get_tsc_res_ns,
	.get_ptp_ts_ns_fn = &nvpps_t26x_get_ptp_ts_ns,
	.get_ptp_tsc_concurrent_ts_ns_fn = &nvpps_t26x_get_ptp_tsc_concurrent_ts_ns,
};
