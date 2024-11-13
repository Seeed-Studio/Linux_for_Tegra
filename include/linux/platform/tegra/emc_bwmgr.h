// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2022, NVIDIA CORPORATION, All rights reserved.

#ifndef __EMC_BWMGR_H
#define __EMC_BWMGR_H

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/of_address.h>
#include <linux/platform/tegra/iso_client.h>
#include <soc/tegra/bpmp-abi.h>

/* keep in sync with tegra_bwmgr_client_names */
enum tegra_bwmgr_client_id {
	TEGRA_BWMGR_CLIENT_CPU_CLUSTER_0,
	TEGRA_BWMGR_CLIENT_CPU_CLUSTER_1,
	TEGRA_BWMGR_CLIENT_CPU_CLUSTER_2,
	TEGRA_BWMGR_CLIENT_CPU_CLUSTER_3,
	TEGRA_BWMGR_CLIENT_DISP0,
	TEGRA_BWMGR_CLIENT_DISP1,
	TEGRA_BWMGR_CLIENT_DISP2,
	TEGRA_BWMGR_CLIENT_DISP1_LA_EMC,
	TEGRA_BWMGR_CLIENT_DISP2_LA_EMC,
	TEGRA_BWMGR_CLIENT_USBD,
	TEGRA_BWMGR_CLIENT_XHCI,
	TEGRA_BWMGR_CLIENT_SDMMC1,
	TEGRA_BWMGR_CLIENT_SDMMC2,
	TEGRA_BWMGR_CLIENT_SDMMC3,
	TEGRA_BWMGR_CLIENT_SDMMC4,
	TEGRA_BWMGR_CLIENT_MON,
	TEGRA_BWMGR_CLIENT_GPU,
	TEGRA_BWMGR_CLIENT_MSENC,
	TEGRA_BWMGR_CLIENT_NVENC1,
	TEGRA_BWMGR_CLIENT_NVJPG,
	TEGRA_BWMGR_CLIENT_NVDEC,
	TEGRA_BWMGR_CLIENT_NVDEC1,
	TEGRA_BWMGR_CLIENT_TSEC,
	TEGRA_BWMGR_CLIENT_TSECB,
	TEGRA_BWMGR_CLIENT_VI,
	TEGRA_BWMGR_CLIENT_ISPA,
	TEGRA_BWMGR_CLIENT_ISPB,
	TEGRA_BWMGR_CLIENT_CAMERA,
	TEGRA_BWMGR_CLIENT_CAMERA_NON_ISO,
	TEGRA_BWMGR_CLIENT_CAMRTC,
	TEGRA_BWMGR_CLIENT_ISOMGR,
	TEGRA_BWMGR_CLIENT_THERMAL_CAP,
	TEGRA_BWMGR_CLIENT_VIC,
	TEGRA_BWMGR_CLIENT_APE_ADSP,
	TEGRA_BWMGR_CLIENT_APE_ADMA,
	TEGRA_BWMGR_CLIENT_PCIE,
	TEGRA_BWMGR_CLIENT_PCIE_1,
	TEGRA_BWMGR_CLIENT_PCIE_2,
	TEGRA_BWMGR_CLIENT_PCIE_3,
	TEGRA_BWMGR_CLIENT_PCIE_4,
	TEGRA_BWMGR_CLIENT_PCIE_5,
	TEGRA_BWMGR_CLIENT_BBC_0,
	TEGRA_BWMGR_CLIENT_EQOS,
	TEGRA_BWMGR_CLIENT_SE0,
	TEGRA_BWMGR_CLIENT_SE1,
	TEGRA_BWMGR_CLIENT_SE2,
	TEGRA_BWMGR_CLIENT_SE3,
	TEGRA_BWMGR_CLIENT_SE4,
	TEGRA_BWMGR_CLIENT_NVPMODEL,
	TEGRA_BWMGR_CLIENT_DEBUG,
	TEGRA_BWMGR_CLIENT_DLA0,
	TEGRA_BWMGR_CLIENT_DLA1,
	TEGRA_BWMGR_CLIENT_COUNT /* Should always be last */
};

enum tegra_bwmgr_request_type {
	TEGRA_BWMGR_SET_EMC_FLOOR, /* lower bound */
	TEGRA_BWMGR_SET_EMC_CAP, /* upper bound */
	TEGRA_BWMGR_SET_EMC_ISO_CAP, /* upper bound that affects ISO Bw */
	TEGRA_BWMGR_SET_EMC_SHARED_BW, /* shared bw request */
	TEGRA_BWMGR_SET_EMC_SHARED_BW_ISO, /* for use by ISO Mgr only */
	TEGRA_BWMGR_SET_EMC_REQ_COUNT /* Should always be last */
};

enum bwmgr_dram_types {
	DRAM_TYPE_NONE,
	DRAM_TYPE_LPDDR4_16CH_ECC,
	DRAM_TYPE_LPDDR4_8CH_ECC,
	DRAM_TYPE_LPDDR4_4CH_ECC,
	DRAM_TYPE_LPDDR4_2CH_ECC,
	DRAM_TYPE_LPDDR4_16CH,
	DRAM_TYPE_LPDDR4_8CH,
	DRAM_TYPE_LPDDR4_4CH,
	DRAM_TYPE_LPDDR4_2CH,
	DRAM_TYPE_LPDDR3_2CH,
	DRAM_TYPE_DDR3_2CH
};

extern u8 bwmgr_dram_efficiency;
extern u8 bwmgr_dram_num_channels;
/* flag to determine supported memory and channel configuration */
extern u8 bwmgr_dram_config_supported;
extern u32 *bwmgr_dram_iso_eff_table;
extern u32 *bwmgr_dram_noniso_eff_table;
extern u32 *bwmgr_max_nvdis_bw_reqd;
extern u32 *bwmgr_max_vi_bw_reqd;
extern int *bwmgr_slope;
extern u32 *bwmgr_vi_bw_reqd_offset;
extern int bwmgr_iso_bw_percentage;
extern enum bwmgr_dram_types bwmgr_dram_type;
extern int emc_to_dram_freq_factor;
extern struct mrq_emc_dvfs_latency_response bwmgr_emc_dvfs;

struct tegra_bwmgr_client;

struct bwmgr_ops {
	unsigned long (*freq_to_bw)(unsigned long freq);
	unsigned long (*bw_to_freq)(unsigned long bw);
	u32 (*dvfs_latency)(u32 ufreq);
	unsigned long (*bwmgr_apply_efficiency)(
		unsigned long total_bw, unsigned long iso_bw,
		unsigned long max_rate, u64 usage_flags,
		unsigned long *iso_bw_min, unsigned long iso_bw_nvdis,
		unsigned long iso_bw_vi);
	unsigned long (*get_best_iso_freq)(long iso_bw,
		long iso_bw_nvdis, long iso_bw_vi);
	void (*update_efficiency)(unsigned long dram_refresh_rate);
	u32 (*get_max_iso_bw)(enum tegra_iso_client client);
};

struct bwmgr_ops *bwmgr_eff_init_t21x(void);
struct bwmgr_ops *bwmgr_eff_init_t18x(void);
struct bwmgr_ops *bwmgr_eff_init_t19x(void);

static inline struct tegra_bwmgr_client *tegra_bwmgr_register(
		enum tegra_bwmgr_client_id client_id)
{
	static int i;
	/* return a dummy handle to allow client to function
	 * as if bwmgr were enabled.
	 */
	return (struct tegra_bwmgr_client *) &i;
}

static inline void tegra_bwmgr_unregister(struct tegra_bwmgr_client *handle) {}

static inline int bwmgr_init(void)
{
	return 0;
}

static inline void bwmgr_exit(void) {}

static inline u8 tegra_bwmgr_get_dram_num_channels(void)
{
	return 0;
}

static inline unsigned long tegra_bwmgr_get_emc_rate(void)
{
	static struct clk *bwmgr_emc_clk;
	struct device_node *dn;

	if (!bwmgr_emc_clk) {
		dn = of_find_compatible_node(NULL, NULL, "nvidia,bwmgr");
		if (dn == NULL) {
			pr_err("bwmgr: dt node not found.\n");
			return 0;
		}

		bwmgr_emc_clk = of_clk_get(dn, 0);
		if (IS_ERR_OR_NULL(bwmgr_emc_clk)) {
			pr_err("bwmgr: couldn't find emc clock.\n");
			bwmgr_emc_clk = NULL;
			WARN_ON(true);
			return 0;
		}
	}

	return clk_get_rate(bwmgr_emc_clk);
}

static inline unsigned long tegra_bwmgr_get_max_emc_rate(void)
{
	static struct clk *bwmgr_emc_clk;
	struct device_node *dn;

	if (!bwmgr_emc_clk) {
		dn = of_find_compatible_node(NULL, NULL, "nvidia,bwmgr");
		if (dn == NULL) {
			pr_err("bwmgr: dt node not found.\n");
			return 0;
		}

		bwmgr_emc_clk = of_clk_get(dn, 0);
		if (IS_ERR_OR_NULL(bwmgr_emc_clk)) {
			pr_err("bwmgr: couldn't find emc clock.\n");
			bwmgr_emc_clk = NULL;
			WARN_ON(true);
			return 0;
		}
	}

	/* Use LONG_MAX as clk_round_rate treats rate argument as signed */
	return clk_round_rate(bwmgr_emc_clk, LONG_MAX);
}

static inline unsigned long tegra_bwmgr_get_core_emc_rate(void)
{
	return 0;
}
static inline unsigned long tegra_bwmgr_round_rate(unsigned long bw)
{
	static struct clk *bwmgr_emc_clk;
	struct device_node *dn;

	if (!bwmgr_emc_clk) {
		dn = of_find_compatible_node(NULL, NULL, "nvidia,bwmgr");
		if (dn == NULL) {
			pr_err("bwmgr: dt node not found.\n");
			return 0;
		}

		bwmgr_emc_clk = of_clk_get(dn, 0);
		if (IS_ERR_OR_NULL(bwmgr_emc_clk)) {
			pr_err("bwmgr: couldn't find emc clock.\n");
			bwmgr_emc_clk = NULL;
			WARN_ON(true);
			return 0;
		}
	}

	return clk_round_rate(bwmgr_emc_clk, bw);
}

static inline int tegra_bwmgr_set_emc(struct tegra_bwmgr_client *handle,
		unsigned long val, enum tegra_bwmgr_request_type req)
{
	return 0;
}

static inline int tegra_bwmgr_get_client_info(struct tegra_bwmgr_client *handle,
		unsigned long *out_val,
		enum tegra_bwmgr_request_type req)
{
	if (!out_val)
		return -EINVAL;
	*out_val = 0;
	return 0;
}

static inline int tegra_bwmgr_notifier_register(struct notifier_block *nb)
{
	return 0;
}

static inline int tegra_bwmgr_notifier_unregister(struct notifier_block *nb)
{
	return 0;
}

#endif /* __EMC_BWMGR_H */
