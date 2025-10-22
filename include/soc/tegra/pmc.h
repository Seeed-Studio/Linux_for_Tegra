/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010 Google, Inc
 * Copyright (c) 2014-2024, NVIDIA CORPORATION. All rights reserved.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 */

#ifndef __SOC_TEGRA_PMC_H__
#define __SOC_TEGRA_PMC_H__

#include <linux/reboot.h>
#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/irq_work.h>
#include <linux/syscore_ops.h>

#include <soc/tegra/pm.h>

struct clk;
struct reset_control;
struct tegra_pmc;

bool tegra_pmc_cpu_is_powered(unsigned int cpuid);
int tegra_pmc_cpu_power_on(unsigned int cpuid);
int tegra_pmc_cpu_remove_clamping(unsigned int cpuid);

/*
 * powergate and I/O rail APIs
 */

#define TEGRA_POWERGATE_CPU	0
#define TEGRA_POWERGATE_3D	1
#define TEGRA_POWERGATE_VENC	2
#define TEGRA_POWERGATE_PCIE	3
#define TEGRA_POWERGATE_VDEC	4
#define TEGRA_POWERGATE_L2	5
#define TEGRA_POWERGATE_MPE	6
#define TEGRA_POWERGATE_HEG	7
#define TEGRA_POWERGATE_SATA	8
#define TEGRA_POWERGATE_CPU1	9
#define TEGRA_POWERGATE_CPU2	10
#define TEGRA_POWERGATE_CPU3	11
#define TEGRA_POWERGATE_CELP	12
#define TEGRA_POWERGATE_3D1	13
#define TEGRA_POWERGATE_CPU0	14
#define TEGRA_POWERGATE_C0NC	15
#define TEGRA_POWERGATE_C1NC	16
#define TEGRA_POWERGATE_SOR	17
#define TEGRA_POWERGATE_DIS	18
#define TEGRA_POWERGATE_DISB	19
#define TEGRA_POWERGATE_XUSBA	20
#define TEGRA_POWERGATE_XUSBB	21
#define TEGRA_POWERGATE_XUSBC	22
#define TEGRA_POWERGATE_VIC	23
#define TEGRA_POWERGATE_IRAM	24
#define TEGRA_POWERGATE_NVDEC	25
#define TEGRA_POWERGATE_NVJPG	26
#define TEGRA_POWERGATE_AUD	27
#define TEGRA_POWERGATE_DFD	28
#define TEGRA_POWERGATE_VE2	29
#define TEGRA_POWERGATE_MAX	TEGRA_POWERGATE_VE2

#define TEGRA_POWERGATE_3D0	TEGRA_POWERGATE_3D

/**
 * enum tegra_io_pad - I/O pad group identifier
 *
 * I/O pins on Tegra SoCs are grouped into so-called I/O pads. Each such pad
 * can be used to control the common voltage signal level and power state of
 * the pins of the given pad.
 */
enum tegra_io_pad {
	TEGRA_IO_PAD_AUDIO,
	TEGRA_IO_PAD_AUDIO_HV,
	TEGRA_IO_PAD_BB,
	TEGRA_IO_PAD_CAM,
	TEGRA_IO_PAD_COMP,
	TEGRA_IO_PAD_CONN,
	TEGRA_IO_PAD_CSIA,
	TEGRA_IO_PAD_CSIB,
	TEGRA_IO_PAD_CSIC,
	TEGRA_IO_PAD_CSID,
	TEGRA_IO_PAD_CSIE,
	TEGRA_IO_PAD_CSIF,
	TEGRA_IO_PAD_CSIG,
	TEGRA_IO_PAD_CSIH,
	TEGRA_IO_PAD_DAP3,
	TEGRA_IO_PAD_DAP5,
	TEGRA_IO_PAD_DBG,
	TEGRA_IO_PAD_DEBUG_NONAO,
	TEGRA_IO_PAD_DMIC,
	TEGRA_IO_PAD_DMIC_HV,
	TEGRA_IO_PAD_DP,
	TEGRA_IO_PAD_DSI,
	TEGRA_IO_PAD_DSIB,
	TEGRA_IO_PAD_DSIC,
	TEGRA_IO_PAD_DSID,
	TEGRA_IO_PAD_EDP,
	TEGRA_IO_PAD_EMMC,
	TEGRA_IO_PAD_EMMC2,
	TEGRA_IO_PAD_EQOS,
	TEGRA_IO_PAD_GPIO,
	TEGRA_IO_PAD_GP_PWM2,
	TEGRA_IO_PAD_GP_PWM3,
	TEGRA_IO_PAD_HDMI,
	TEGRA_IO_PAD_HDMI_DP0,
	TEGRA_IO_PAD_HDMI_DP1,
	TEGRA_IO_PAD_HDMI_DP2,
	TEGRA_IO_PAD_HDMI_DP3,
	TEGRA_IO_PAD_HSIC,
	TEGRA_IO_PAD_HV,
	TEGRA_IO_PAD_LVDS,
	TEGRA_IO_PAD_MIPI_BIAS,
	TEGRA_IO_PAD_NAND,
	TEGRA_IO_PAD_PEX_BIAS,
	TEGRA_IO_PAD_PEX_CLK_BIAS,
	TEGRA_IO_PAD_PEX_CLK1,
	TEGRA_IO_PAD_PEX_CLK2,
	TEGRA_IO_PAD_PEX_CLK3,
	TEGRA_IO_PAD_PEX_CLK_2_BIAS,
	TEGRA_IO_PAD_PEX_CLK_2,
	TEGRA_IO_PAD_PEX_CNTRL,
	TEGRA_IO_PAD_PEX_CTL2,
	TEGRA_IO_PAD_PEX_L0_RST,
	TEGRA_IO_PAD_PEX_L1_RST,
	TEGRA_IO_PAD_PEX_L5_RST,
	TEGRA_IO_PAD_PWR_CTL,
	TEGRA_IO_PAD_SDMMC1,
	TEGRA_IO_PAD_SDMMC1_HV,
	TEGRA_IO_PAD_SDMMC2,
	TEGRA_IO_PAD_SDMMC2_HV,
	TEGRA_IO_PAD_SDMMC3,
	TEGRA_IO_PAD_SDMMC3_HV,
	TEGRA_IO_PAD_SDMMC4,
	TEGRA_IO_PAD_SOC_GPIO10,
	TEGRA_IO_PAD_SOC_GPIO12,
	TEGRA_IO_PAD_SOC_GPIO13,
	TEGRA_IO_PAD_SOC_GPIO53,
	TEGRA_IO_PAD_SPI,
	TEGRA_IO_PAD_SPI_HV,
	TEGRA_IO_PAD_SYS_DDC,
	TEGRA_IO_PAD_UART,
	TEGRA_IO_PAD_UART4,
	TEGRA_IO_PAD_UART5,
	TEGRA_IO_PAD_UFS,
	TEGRA_IO_PAD_USB0,
	TEGRA_IO_PAD_USB1,
	TEGRA_IO_PAD_USB2,
	TEGRA_IO_PAD_USB3,
	TEGRA_IO_PAD_USB_BIAS,
	TEGRA_IO_PAD_AO_HV,
};

struct tegra_io_pad_soc {
	enum tegra_io_pad id;
	unsigned int dpd;
	unsigned int request;
	unsigned int status;
	bool has_int_reg;
	unsigned int e_reg06;
	unsigned int e_reg18;
	unsigned int voltage;
	unsigned int e_33v_ctl;
	const char *name;
};

struct tegra_pmc_regs {
	unsigned int scratch0;
	unsigned int scratch_l0_1_0;
	unsigned int scratch_l0_21_0;
	unsigned int rst_status;
	unsigned int rst_source_shift;
	unsigned int rst_source_mask;
	unsigned int rst_level_shift;
	unsigned int rst_level_mask;
	unsigned int aowake_cntrl;
	unsigned int aowake_mask_w;
	unsigned int aowake_status_w;
	unsigned int aowake_status_r;
	unsigned int aowake_tier2_routing;
	unsigned int aowake_sw_status_w;
	unsigned int aowake_sw_status;
	unsigned int aowake_latch_sw;
	unsigned int aowake_ctrl;
};

struct tegra_wake_event {
	const char *name;
	unsigned int id;
	unsigned int irq;
	struct {
		unsigned int instance;
		unsigned int pin;
	} gpio;
};

#define TEGRA_WAKE_SIMPLE(_name, _id)			\
	{						\
		.name = _name,				\
		.id = _id,				\
		.irq = 0,				\
		.gpio = {				\
			.instance = UINT_MAX,		\
			.pin = UINT_MAX,		\
		},					\
	}

#define TEGRA_WAKE_IRQ(_name, _id, _irq)		\
	{						\
		.name = _name,				\
		.id = _id,				\
		.irq = _irq,				\
		.gpio = {				\
			.instance = UINT_MAX,		\
			.pin = UINT_MAX,		\
		},					\
	}

#define TEGRA_WAKE_GPIO(_name, _id, _instance, _pin)	\
	{						\
		.name = _name,				\
		.id = _id,				\
		.irq = 0,				\
		.gpio = {				\
			.instance = _instance,		\
			.pin = _pin,			\
		},					\
	}

#define TEGRA_PMC_MAX_WAKE_VECTORS	4

struct tegra_pmc_soc {
	unsigned int num_powergates;
	const char *const *powergates;
	unsigned int num_cpu_powergates;
	const u8 *cpu_powergates;

	bool has_tsense_reset;
	bool has_gpu_clamps;
	bool needs_mbist_war;
	bool has_impl_33v_pwr;
	bool maybe_tz_only;

	const struct tegra_io_pad_soc *io_pads;
	unsigned int num_io_pads;

	const struct pinctrl_pin_desc *pin_descs;
	unsigned int num_pin_descs;

	const struct tegra_pmc_regs *regs;
	void (*init)(struct tegra_pmc *pmc);
	void (*setup_irq_polarity)(struct tegra_pmc *pmc,
				   struct device_node *np,
				   bool invert);
	void (*set_wake_filters)(struct tegra_pmc *pmc);
	int (*irq_set_wake)(struct irq_data *data, unsigned int on);
	int (*irq_set_type)(struct irq_data *data, unsigned int type);
	int (*powergate_set)(struct tegra_pmc *pmc, unsigned int id,
			     bool new_state);

	const char * const *reset_sources;
	unsigned int num_reset_sources;
	const char * const *reset_levels;
	unsigned int num_reset_levels;

	/*
	 * These describe events that can wake the system from sleep (i.e.
	 * LP0 or SC7). Wakeup from other sleep states (such as LP1 or LP2)
	 * are dealt with in the LIC.
	 */
	const struct tegra_wake_event *wake_events;
	unsigned int num_wake_events;
	unsigned int max_wake_events;
	unsigned int max_wake_vectors;

	const struct pmc_clk_init_data *pmc_clks_data;
	unsigned int num_pmc_clks;
	bool has_blink_output;
	bool has_usb_sleepwalk;
	bool supports_core_domain;
	bool has_single_mmio_aperture;
};

/**
 * struct tegra_pmc - NVIDIA Tegra PMC
 * @dev: pointer to PMC device structure
 * @base: pointer to I/O remapped register region
 * @wake: pointer to I/O remapped region for WAKE registers
 * @aotag: pointer to I/O remapped region for AOTAG registers
 * @scratch: pointer to I/O remapped region for scratch registers
 * @clk: pointer to pclk clock
 * @soc: pointer to SoC data structure
 * @tz_only: flag specifying if the PMC can only be accessed via TrustZone
 * @rate: currently configured rate of pclk
 * @suspend_mode: lowest suspend mode available
 * @cpu_good_time: CPU power good time (in microseconds)
 * @cpu_off_time: CPU power off time (in microsecends)
 * @core_osc_time: core power good OSC time (in microseconds)
 * @core_pmu_time: core power good PMU time (in microseconds)
 * @core_off_time: core power off time (in microseconds)
 * @corereq_high: core power request is active-high
 * @sysclkreq_high: system clock request is active-high
 * @combined_req: combined power request for CPU & core
 * @cpu_pwr_good_en: CPU power good signal is enabled
 * @lp0_vec_phys: physical base address of the LP0 warm boot code
 * @lp0_vec_size: size of the LP0 warm boot code
 * @powergates_available: Bitmap of available power gates
 * @powergates_lock: mutex for power gate register access
 * @pctl_dev: pin controller exposed by the PMC
 * @domain: IRQ domain provided by the PMC
 * @irq: chip implementation for the IRQ domain
 * @clk_nb: pclk clock changes handler
 * @reboot_notifier: tegra pmc reboot notifier
 * @core_domain_state_synced: flag marking the core domain's state as synced
 * @core_domain_registered: flag marking the core domain as registered
 * @wake_type_level_map: Bitmap indicating level type for non-dual edge wakes
 * @wake_type_dual_edge_map: Bitmap indicating if a wake is dual-edge or not
 * @wake_sw_status_map: Bitmap to hold raw status of wakes without mask
 * @wake_cntrl_level_map: Bitmap to hold wake levels to be programmed in
 *     cntrl register associated with each wake during system suspend.
 */
struct tegra_pmc {
	struct device *dev;
	void __iomem *base;
	void __iomem *wake;
	void __iomem *aotag;
	void __iomem *scratch;
	struct clk *clk;

	const struct tegra_pmc_soc *soc;
	bool tz_only;

	unsigned long rate;

	enum tegra_suspend_mode suspend_mode;
	u32 cpu_good_time;
	u32 cpu_off_time;
	u32 core_osc_time;
	u32 core_pmu_time;
	u32 core_off_time;
	bool corereq_high;
	bool sysclkreq_high;
	bool combined_req;
	bool cpu_pwr_good_en;
	u32 lp0_vec_phys;
	u32 lp0_vec_size;
	DECLARE_BITMAP(powergates_available, TEGRA_POWERGATE_MAX);

	struct mutex powergates_lock;

	struct pinctrl_dev *pctl_dev;

	struct irq_domain *domain;
	struct irq_chip irq;

	struct notifier_block clk_nb;
	struct notifier_block reboot_notifier;

	bool core_domain_state_synced;
	bool core_domain_registered;

	unsigned long *wake_type_level_map;
	unsigned long *wake_type_dual_edge_map;
	unsigned long *wake_sw_status_map;
	unsigned long *wake_cntrl_level_map;
	struct syscore_ops syscore;

	/* Pending wake IRQ processing */
	u32 pending_wake_status[TEGRA_PMC_MAX_WAKE_VECTORS];
	struct irq_work pending_wake_irq_work;
};

/* deprecated, use TEGRA_IO_PAD_{HDMI,LVDS} instead */
#define TEGRA_IO_RAIL_HDMI	TEGRA_IO_PAD_HDMI
#define TEGRA_IO_RAIL_LVDS	TEGRA_IO_PAD_LVDS

#ifdef CONFIG_SOC_TEGRA_PMC
int tegra_powergate_power_on(unsigned int id);
int tegra_powergate_power_off(unsigned int id);
int tegra_powergate_remove_clamping(unsigned int id);

/* Must be called with clk disabled, and returns with clk enabled */
int tegra_powergate_sequence_power_up(unsigned int id, struct clk *clk,
				      struct reset_control *rst);

int tegra_io_pad_power_enable(enum tegra_io_pad id);
int tegra_io_pad_power_disable(enum tegra_io_pad id);
int tegra264_io_pad_power_enable(struct device *dev, enum tegra_io_pad id);
int tegra264_io_pad_power_disable(struct device *dev, enum tegra_io_pad id);

/* deprecated, use tegra_io_pad_power_{enable,disable}() instead */
int tegra_io_rail_power_on(unsigned int id);
int tegra_io_rail_power_off(unsigned int id);

void tegra_pmc_set_suspend_mode(enum tegra_suspend_mode mode);
void tegra_pmc_enter_suspend_mode(enum tegra_suspend_mode mode);

bool tegra_pmc_core_domain_state_synced(void);

void tegra186_pmc_set_wake_filters(struct tegra_pmc *pmc);
int tegra186_pmc_irq_set_type(struct irq_data *data, unsigned int type);
int tegra186_pmc_irq_set_wake(struct irq_data *data, unsigned int on);
int tegra_pmc_irq_init(struct tegra_pmc *pmc);
void tegra186_pmc_setup_irq_polarity(struct tegra_pmc *pmc,
				struct device_node *np, bool invert);
void tegra186_pmc_resume(struct tegra_pmc *pmc);
int tegra186_pmc_suspend(struct tegra_pmc *pmc);

void tegra_pmc_reset_sysfs_init(struct tegra_pmc *pmc);
void tegra_pmc_reset_sysfs_remove(struct tegra_pmc *pmc);

void tegra_pmc_scratch_sysfs_init(struct tegra_pmc *pmc);
void tegra_pmc_scratch_sysfs_remove(struct tegra_pmc *pmc);

int tegra_pmc_pinctrl_init(struct tegra_pmc *pmc);

int tegra_pmc_init(struct tegra_pmc *pmc);

void tegra_pmc_program_reboot_reason(struct tegra_pmc *pmc, const char *cmd);

int tegra186_io_pad_power_enable(struct tegra_pmc *pmc, enum tegra_io_pad id);
int tegra186_io_pad_power_disable(struct tegra_pmc *pmc, enum tegra_io_pad id);
#else
static inline int tegra_powergate_power_on(unsigned int id)
{
	return -ENOSYS;
}

static inline int tegra_powergate_power_off(unsigned int id)
{
	return -ENOSYS;
}

static inline int tegra_powergate_remove_clamping(unsigned int id)
{
	return -ENOSYS;
}

static inline int tegra_powergate_sequence_power_up(unsigned int id,
						    struct clk *clk,
						    struct reset_control *rst)
{
	return -ENOSYS;
}

static inline int tegra_io_pad_power_enable(enum tegra_io_pad id)
{
	return -ENOSYS;
}

static inline int tegra_io_pad_power_disable(enum tegra_io_pad id)
{
	return -ENOSYS;
}

static inline int tegra264_io_pad_power_enable(struct device *dev,
					enum tegra_io_pad id)
{
	return -ENOSYS;
}

static inline int tegra264_io_pad_power_disable(struct device *dev,
					enum tegra_io_pad id)
{
	return -ENOSYS;
}

static inline int tegra_io_pad_get_voltage(enum tegra_io_pad id)
{
	return -ENOSYS;
}

static inline int tegra_io_rail_power_on(unsigned int id)
{
	return -ENOSYS;
}

static inline int tegra_io_rail_power_off(unsigned int id)
{
	return -ENOSYS;
}

static inline void tegra_pmc_set_suspend_mode(enum tegra_suspend_mode mode)
{
}

static inline void tegra_pmc_enter_suspend_mode(enum tegra_suspend_mode mode)
{
}

static inline bool tegra_pmc_core_domain_state_synced(void)
{
	return false;
}

#endif /* CONFIG_SOC_TEGRA_PMC */

#if defined(CONFIG_SOC_TEGRA_PMC) && defined(CONFIG_PM_SLEEP)
enum tegra_suspend_mode tegra_pmc_get_suspend_mode(void);
#else
static inline enum tegra_suspend_mode tegra_pmc_get_suspend_mode(void)
{
	return TEGRA_SUSPEND_NONE;
}
#endif

#endif /* __SOC_TEGRA_PMC_H__ */
