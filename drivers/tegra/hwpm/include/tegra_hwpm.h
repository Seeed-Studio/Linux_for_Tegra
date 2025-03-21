/* SPDX-License-Identifier: MIT */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef TEGRA_HWPM_H
#define TEGRA_HWPM_H

#include <tegra_hwpm_types.h>

#define TEGRA_HWPM_IP_INACTIVE	~(0U)

/* These macro values should match TEGRA_SOC_HWPM_IP_STATUS_* */
#define TEGRA_HWPM_IP_STATUS_VALID	0U
#define TEGRA_HWPM_IP_STATUS_INVALID	1U

/* These macro values should match TEGRA_SOC_HWPM_RESOURCE_STATUS_* */
#define TEGRA_HWPM_RESOURCE_STATUS_INVALID	0U
#define TEGRA_HWPM_RESOURCE_STATUS_VALID	1U

#define TEGRA_HWPM_FUSE_PRODUCTION_MODE_MASK		BIT(0)
#define TEGRA_HWPM_FUSE_SECURITY_MODE_MASK		BIT(1)
#define TEGRA_HWPM_FUSE_HWPM_GLOBAL_DISABLE_MASK	BIT(2)
#define TEGRA_HWPM_FUSE_OPT_HWPM_DISABLE_MASK		BIT(3)

/* Indicate the prescence of HWPM-IP debug interface for devctl calls */
#define TEGRA_HWPM_IP_DEBUG_FD_INVALID          -1
#define TEGRA_HWPM_IP_DEBUG_FD_VALID            1U

/* Indicate max dynamic aperture slots accepted for binary search */
#define TEGRA_HWPM_APERTURE_SLOTS_LIMIT		64U

#ifdef __KERNEL__
struct tegra_hwpm_os_linux;
#else
#define ARRAY_SIZE(a)  (sizeof(a) / sizeof(a[0]))
struct tegra_hwpm_os_qnx;
#endif
struct tegra_hwpm_mem_mgmt;
struct tegra_hwpm_allowlist_map;
struct tegra_soc_hwpm_exec_credit_program;
struct tegra_soc_hwpm_setup_trigger;
enum tegra_soc_hwpm_ip_reg_op;

/*
 * This is a copy of enum tegra_soc_hwpm_ip uapi structure.
 * It is not a hard requirement as tegra_soc_hwpm_ip is translated to
 * tegra_hwpm_ip_enum before each use.
 */
enum tegra_hwpm_ip_enum {
	TEGRA_HWPM_IP_VI,
	TEGRA_HWPM_IP_ISP,
	TEGRA_HWPM_IP_VIC,
	TEGRA_HWPM_IP_OFA,
	TEGRA_HWPM_IP_PVA,
	TEGRA_HWPM_IP_NVDLA,
	TEGRA_HWPM_IP_MGBE,
	TEGRA_HWPM_IP_SCF,
	TEGRA_HWPM_IP_NVDEC,
	TEGRA_HWPM_IP_NVENC,
	TEGRA_HWPM_IP_PCIE,
	TEGRA_HWPM_IP_DISPLAY,
	TEGRA_HWPM_IP_MSS_CHANNEL,
	TEGRA_HWPM_IP_MSS_GPU_HUB,
	TEGRA_HWPM_IP_MSS_ISO_NISO_HUBS,
	TEGRA_HWPM_IP_MSS_MCF,
	TEGRA_HWPM_IP_APE,
	TEGRA_HWPM_IP_C2C,
	TEGRA_HWPM_IP_SMMU,
	TEGRA_HWPM_IP_CL2,
	TEGRA_HWPM_IP_NVLCTRL,
	TEGRA_HWPM_IP_NVLRX,
	TEGRA_HWPM_IP_NVLTX,
	TEGRA_HWPM_IP_MSS_HUB,
	TEGRA_HWPM_IP_MCF_SOC,
	TEGRA_HWPM_IP_MCF_C2C,
	TEGRA_HWPM_IP_MCF_CLINK,
	TEGRA_HWPM_IP_MCF_CORE,
	TEGRA_HWPM_IP_MCF_OCU,
	TEGRA_HWPM_IP_PCIE_XTLQ,
	TEGRA_HWPM_IP_PCIE_XTLRC,
	TEGRA_HWPM_IP_PCIE_XALRC,
	TEGRA_HWPM_IP_UCF_MSW,
	TEGRA_HWPM_IP_UCF_PSW,
	TEGRA_HWPM_IP_UCF_CSW,
	TEGRA_HWPM_IP_UCF_HUB,
	TEGRA_HWPM_IP_UCF_SCB,
	TEGRA_HWPM_IP_CPU,		/* CPU instance 0-31 */
	TEGRA_HWPM_IP_CPU_EXT_0,	/* CPU (extended) instance 32-63 */
	TEGRA_HWPM_IP_CPU_EXT_1,	/* CPU (extended) instance 64-95 */
	TEGRA_HWPM_IP_CPU_EXT_2,	/* CPU (extended) instance 96-127 */
	TEGRA_HWPM_IP_NVTHERM,
	TEGRA_HWPM_IP_CSN,		/* CSN instance 0-31 */
	TEGRA_HWPM_IP_CSN_EXT_0,	/* CSN (extended) instance 32-63 */
	TEGRA_HWPM_IP_CSNH,
	TEGRA_HWPM_IP_SLC,		/* SLC instance 0-31 */
	TEGRA_HWPM_IP_SLC_EXT_0,	/* SLC (extended) instance 32-63 */
	TEGRA_HWPM_IP_SLC_EXT_1,	/* SLC (extended) instance 64-95 */
	TEGRA_HWPM_IP_SLC_EXT_2,	/* SLC (extended) instance 96-127 */
	TEGRA_HWPM_IP_PCIE_CXLB,
	TEGRA_HWPM_IP_C2C_GRS,
	TEGRA_HWPM_IP_C2C_LLIC,
	TEGRA_HWPM_IP_C2C_LLIM,
	TEGRA_HWPM_IP_C2C_LPIC,
	TEGRA_HWPM_IP_C2C_LPIS,
	TEGRA_HWPM_IP_C2C_UPHY,
	TERGA_HWPM_NUM_IPS
};

/*
 * Function to translate IP index into IP name
 * Developer is responsible to update this corresponding to tegra_hwpm_ip_enum
 */
static inline const char *tegra_hwpm_ip_string(enum tegra_hwpm_ip_enum ip_enum)
{
	const char *tegra_hwpm_ip_name[TERGA_HWPM_NUM_IPS + 1] = {
		[TEGRA_HWPM_IP_VI] = "vi",
		[TEGRA_HWPM_IP_ISP] = "isp",
		[TEGRA_HWPM_IP_VIC] = "vic",
		[TEGRA_HWPM_IP_OFA] = "ofa",
		[TEGRA_HWPM_IP_PVA] = "pva",
		[TEGRA_HWPM_IP_NVDLA] = "dla",
		[TEGRA_HWPM_IP_MGBE] = "mgbe",
		[TEGRA_HWPM_IP_SCF] = "scf",
		[TEGRA_HWPM_IP_NVDEC] = "nvdec",
		[TEGRA_HWPM_IP_NVENC] = "nvenc",
		[TEGRA_HWPM_IP_PCIE] = "pcie",
		[TEGRA_HWPM_IP_DISPLAY] = "display",
		[TEGRA_HWPM_IP_MSS_CHANNEL] = "mss_channel",
		[TEGRA_HWPM_IP_MSS_GPU_HUB] = "mss_gpu_hub",
		[TEGRA_HWPM_IP_MSS_ISO_NISO_HUBS] = "mss_iso_niso_hubs",
		[TEGRA_HWPM_IP_MSS_MCF] = "mss_mcf",
		[TEGRA_HWPM_IP_APE] = "ape",
		[TEGRA_HWPM_IP_C2C] = "c2c",
		[TEGRA_HWPM_IP_SMMU] = "smmu",
		[TEGRA_HWPM_IP_CL2] = "cl2",
		[TEGRA_HWPM_IP_NVLCTRL] = "nvlctrl",
		[TEGRA_HWPM_IP_NVLRX] = "nvlrx",
		[TEGRA_HWPM_IP_NVLTX] = "nvltx",
		[TEGRA_HWPM_IP_MSS_HUB] = "mss_hub",
		[TEGRA_HWPM_IP_MCF_SOC] = "mcf_soc",
		[TEGRA_HWPM_IP_MCF_C2C] = "mcf_c2c",
		[TEGRA_HWPM_IP_MCF_CLINK] = "mcf_clink",
		[TEGRA_HWPM_IP_MCF_CORE] = "mcf_core",
		[TEGRA_HWPM_IP_MCF_OCU] = "mcf_ocu",
		[TEGRA_HWPM_IP_PCIE_XTLQ] = "pcie_xtlq",
		[TEGRA_HWPM_IP_PCIE_XTLRC] = "pcie_xltrc",
		[TEGRA_HWPM_IP_PCIE_XALRC] = "pcie_xalrc",
		[TEGRA_HWPM_IP_UCF_MSW] = "ucf_msw",
		[TEGRA_HWPM_IP_UCF_PSW] = "ucf_psw",
		[TEGRA_HWPM_IP_UCF_CSW] = "ucf_csw",
		[TEGRA_HWPM_IP_UCF_HUB] = "ucf_hub",
		[TEGRA_HWPM_IP_UCF_SCB] = "ucf_scb",
		[TEGRA_HWPM_IP_CPU] = "cpu",
		[TEGRA_HWPM_IP_CPU_EXT_0] = "cpu_ext_0",
		[TEGRA_HWPM_IP_CPU_EXT_1] = "cpu_ext_1",
		[TEGRA_HWPM_IP_CPU_EXT_2] = "cpu_ext_2",
		[TEGRA_HWPM_IP_NVTHERM] = "nvtherm",
		[TEGRA_HWPM_IP_CSN] = "csn",
		[TEGRA_HWPM_IP_CSN_EXT_0] = "csn_ext_0",
		[TEGRA_HWPM_IP_CSNH] = "csnh",
		[TEGRA_HWPM_IP_SLC] = "slc",
		[TEGRA_HWPM_IP_SLC_EXT_0] = "slc_ext_0",
		[TEGRA_HWPM_IP_SLC_EXT_1] = "slc_ext_1",
		[TEGRA_HWPM_IP_SLC_EXT_2] = "slc_ext_2",
		[TEGRA_HWPM_IP_PCIE_CXLB] = "pcie_cxlb",
		[TEGRA_HWPM_IP_C2C_GRS] = "c2c_grs",
		[TEGRA_HWPM_IP_C2C_LLIC] = "c2c_llic",
		[TEGRA_HWPM_IP_C2C_LLIM] = "c2c_llim",
		[TEGRA_HWPM_IP_C2C_LPIC] = "c2c_lpic",
		[TEGRA_HWPM_IP_C2C_LPIS] = "c2c_lpis",
		[TEGRA_HWPM_IP_C2C_UPHY] = "c2c_uphy",
		[TERGA_HWPM_NUM_IPS] = "unknown",
	};

	if (ip_enum >= TERGA_HWPM_NUM_IPS) {
		return tegra_hwpm_ip_name[TERGA_HWPM_NUM_IPS];
	}
	return tegra_hwpm_ip_name[ip_enum];
}

/*
 * This is a copy of enum tegra_soc_hwpm_resource uapi structure.
 * It is not a hard requirement as tegra_soc_hwpm_resource is translated to
 * tegra_hwpm_resource_enum before each use.
 */
enum tegra_hwpm_resource_enum {
	TEGRA_HWPM_RESOURCE_VI,
	TEGRA_HWPM_RESOURCE_ISP,
	TEGRA_HWPM_RESOURCE_VIC,
	TEGRA_HWPM_RESOURCE_OFA,
	TEGRA_HWPM_RESOURCE_PVA,
	TEGRA_HWPM_RESOURCE_NVDLA,
	TEGRA_HWPM_RESOURCE_MGBE,
	TEGRA_HWPM_RESOURCE_SCF,
	TEGRA_HWPM_RESOURCE_NVDEC,
	TEGRA_HWPM_RESOURCE_NVENC,
	TEGRA_HWPM_RESOURCE_PCIE,
	TEGRA_HWPM_RESOURCE_DISPLAY,
	TEGRA_HWPM_RESOURCE_MSS_CHANNEL,
	TEGRA_HWPM_RESOURCE_MSS_GPU_HUB,
	TEGRA_HWPM_RESOURCE_MSS_ISO_NISO_HUBS,
	TEGRA_HWPM_RESOURCE_MSS_MCF,
	TEGRA_HWPM_RESOURCE_PMA,
	TEGRA_HWPM_RESOURCE_CMD_SLICE_RTR,
	TEGRA_HWPM_RESOURCE_APE,
	TEGRA_HWPM_RESOURCE_C2C,
	TEGRA_HWPM_RESOURCE_SMMU,
	TEGRA_HWPM_RESOURCE_CL2,
	TEGRA_HWPM_RESOURCE_NVLCTRL,
	TEGRA_HWPM_RESOURCE_NVLRX,
	TEGRA_HWPM_RESOURCE_NVLTX,
	TEGRA_HWPM_RESOURCE_MSS_HUB,
	TEGRA_HWPM_RESOURCE_MCF_SOC,
	TEGRA_HWPM_RESOURCE_MCF_C2C,
	TEGRA_HWPM_RESOURCE_MCF_CLINK,
	TEGRA_HWPM_RESOURCE_MCF_CORE,
	TEGRA_HWPM_RESOURCE_MCF_OCU,
	TEGRA_HWPM_RESOURCE_PCIE_XTLQ,
	TEGRA_HWPM_RESOURCE_PCIE_XTLRC,
	TEGRA_HWPM_RESOURCE_PCIE_XALRC,
	TEGRA_HWPM_RESOURCE_UCF_MSW,
	TEGRA_HWPM_RESOURCE_UCF_PSW,
	TEGRA_HWPM_RESOURCE_UCF_CSW,
	TEGRA_HWPM_RESOURCE_UCF_HUB,
	TEGRA_HWPM_RESOURCE_UCF_SCB,
	TEGRA_HWPM_RESOURCE_CPU,	/* CPU instance 0 -31 */
	TEGRA_HWPM_RESOURCE_CPU_EXT_0,	/* CPU (extended) instance 32-63 */
	TEGRA_HWPM_RESOURCE_CPU_EXT_1,	/* CPU (extended) instance 64-95 */
	TEGRA_HWPM_RESOURCE_CPU_EXT_2,	/* CPU (extended) instance 96-127 */
	TEGRA_HWPM_RESOURCE_NVTHERM,
	TEGRA_HWPM_RESOURCE_CSN,	/* CSN instance 0-31 */
	TEGRA_HWPM_RESOURCE_CSN_EXT_0,	/* CSN (extended) instance 32-63 */
	TEGRA_HWPM_RESOURCE_CSNH,
	TEGRA_HWPM_RESOURCE_SLC,	/* SLC instance 0 -31 */
	TEGRA_HWPM_RESOURCE_SLC_EXT_0,	/* SLC (extended) instance 32-63 */
	TEGRA_HWPM_RESOURCE_SLC_EXT_1,	/* SLC (extended) instance 64-95 */
	TEGRA_HWPM_RESOURCE_SLC_EXT_2,	/* SLC (extended) instance 96-127 */
	TEGRA_HWPM_RESOURCE_PCIE_CXLB,
	TEGRA_HWPM_RESOURCE_C2C_GRS,
	TEGRA_HWPM_RESOURCE_C2C_LLIC,
	TEGRA_HWPM_RESOURCE_C2C_LLIM,
	TEGRA_HWPM_RESOURCE_C2C_LPIC,
	TEGRA_HWPM_RESOURCE_C2C_LPIS,
	TEGRA_HWPM_RESOURCE_C2C_UPHY,
	TERGA_HWPM_NUM_RESOURCES
};

/* Used in Credit Programming */
enum tegra_hwpm_credit_cmd {
	TEGRA_HWPM_CMD_SET_HS_CREDITS,
	TEGRA_HWPM_CMD_GET_HS_CREDITS,
	TEGRA_HWPM_CMD_GET_TOTAL_HS_CREDITS,
	TEGRA_HWPM_CMD_GET_CHIPLET_HS_CREDITS_POOL,
	TEGRA_HWPM_CMD_GET_HS_CREDITS_MAPPING
};

/* Used in Setup Trigger Programming */
enum tegra_hwpm_trigger_session_type {
	TEGRA_HWPM_CMD_INVALID_SESSION,
	TEGRA_HWPM_CMD_START_STOP_SESSION,
	TEGRA_HWPM_CMD_PERIODIC_SESSION
};

/*
 * This structure is copy of struct tegra_soc_hwpm_ip_ops uapi structure.
 * This is not a hard requirement as each value from tegra_soc_hwpm_ip_ops
 * is copied to struct tegra_hwpm_ip_ops.
 */
struct tegra_hwpm_ip_ops {
	/*
	 * Opaque ip device handle used for callback from
	 * SOC HWPM driver to IP drivers. This handle can be used
	 * to access IP driver functionality with the callbacks.
	 */
	void *ip_dev;
	/*
	 * hwpm_ip_pm is callback function to disable/enable
	 * IP driver power management. Before SOC HWPM doing
	 * perf measuremnts, this callback is called with
	 * "disable = true ", so that IP driver will disable IP specific
	 * power management to keep IP driver responsive. Once SOC HWPM is
	 * done with perf measurement, this callaback is called
	 * with "disable = false", so that IP driver can restore back
	 * it's orignal power management.
	 */
	int (*hwpm_ip_pm)(void *dev, bool disable);
	/*
	 * hwpm_ip_reg_op is callback function to do IP
	 * register 32 bit read or write.
	 * For read:
	 *      input : dev - IP device handle
	 *      input : reg_op - TEGRA_SOC_HWPM_IP_REG_OP_READ
	 *      input : inst_element_index - element index within IP instance
	 *      input : reg_offset - register offset
	 *      output: reg_data - u32 read value
	 * For write:
	 *      input : dev - IP device handle
	 *      input : reg_op - TEGRA_SOC_HWPM_IP_REG_OP_WRITE
	 *      input : inst_element_index - element index within IP instance
	 *      input : reg_offset - register offset
	 *      output: reg_data -  u32 write value
	 * Return:
	 *      reg_op success / failure
	 */
	int (*hwpm_ip_reg_op)(void *dev,
				enum tegra_soc_hwpm_ip_reg_op reg_op,
				u32 inst_element_index, u64 reg_offset,
				u32 *reg_data);
	/*
	 * fd - is used to store the file descriptor of the IP devctl node in QNX.
	 * Default is -1, which indicates the IP has no debug node enabled for Reg ops.
	 * Set to 1 if IP has debug node enabled for Reg ops.
	 */
	int fd;
};

/* There are 3 types of HWPM components/apertures */
#define TEGRA_HWPM_APERTURE_TYPE_PERFMUX	0U
#define TEGRA_HWPM_APERTURE_TYPE_BROADCAST	1U
#define TEGRA_HWPM_APERTURE_TYPE_PERFMON	2U
#define TEGRA_HWPM_APERTURE_TYPE_MAX		3U

/*
 * Devices handled by HWPM driver can be divided into 2 categories
 * - HWPM : Components in HWPM device address space
 *          All perfmons, PMA and RTR perfmuxes
 * - IP : Components in IP address space
 *        IP perfmuxes
 *
 * This enum defines MACROS to specify an element to be HWPM or IP
 * and the specific aperture.
 */
enum tegra_hwpm_element_type {
	HWPM_ELEMENT_INVALID,
	HWPM_ELEMENT_PERFMON,
	HWPM_ELEMENT_PERFMUX,
	IP_ELEMENT_PERFMUX,
	IP_ELEMENT_BROADCAST,
};

enum tegra_hwpm_funcs {
	TEGRA_HWPM_INIT_IP_STRUCTURES,
	TEGRA_HWPM_MATCH_BASE_ADDRESS,
	TEGRA_HWPM_FIND_GIVEN_ADDRESS,
	TEGRA_HWPM_UPDATE_IP_INST_MASK,
	TEGRA_HWPM_GET_ALIST_SIZE,
	TEGRA_HWPM_COMBINE_ALIST,
	TEGRA_HWPM_RESERVE_GIVEN_RESOURCE,
	TEGRA_HWPM_BIND_RESOURCES,
	TEGRA_HWPM_UNBIND_RESOURCES,
	TEGRA_HWPM_RELEASE_RESOURCES,
	TEGRA_HWPM_RELEASE_ROUTER,
	TEGRA_HWPM_RELEASE_IP_STRUCTURES
};

struct tegra_hwpm_func_args {
	u64 *alist;
	u64 full_alist_idx;
};

struct allowlist {
	u64 reg_offset;
	bool zero_at_init;
};

struct hwpm_ip_aperture {
	/*
	 * Indicates which domain (HWPM or IP) aperture belongs to,
	 * used for reverse mapping
	 */
	enum tegra_hwpm_element_type element_type;

	/*
	 * Index of the aperture within the instance.
	 * Static structure for this element can be retrieved using this index.
	 */
	u32 aperture_index;

	/*
	 * Element index : Index of this aperture within the instance
	 * This will be used to update element_fs_mask to indicate availability.
	 * This mask also indicates corresponding core element.
	 */
	u32 element_index_mask;

	/*
	 * Index of the element within the IP instance
	 * For perfmux entries, this index is passed to hwpm_ip_reg_op()
	 */
	u32 element_index;

	/*
	 * Device index corresponding to device node aperture address index
	 * in Device tree or ACPI table.
	 * This is used to map HWPM apertures only
	 */
	u32 device_index;

	/* MMIO device tree aperture - only populated for perfmon */
#ifdef __KERNEL__
	void __iomem *dt_mmio;
#else
	void *dt_mmio;
#endif
	/* DT tree name */
	char name[64];

	/*
	 * MMIO address for the aperture. This address range is present
	 * in the device node.
	 * MMIO addresses can be same as virtual aperture addresses.
	 */
	u64 start_pa;
	u64 end_pa;

	/*
	 * Virtual aperture address
	 * Regops addresses should be in this range.
	 */
	u64 start_abs_pa;
	u64 end_abs_pa;

	/*
	 * Base address of Perfmon Block
	 * All perfmon apertures have identical placement of registers
	 * HWPM read/write logic for perfmons refers to registers in the first
	 * perfmon block. Use this base address value to compute register
	 * offset in HWPM read/write functions.
	 */
	u64 base_pa;

	/* Allowlist */
	u64 alist_size;
	struct allowlist *alist;

	/*
	 * Fake registers for simulation where SOC HWPM is not implemented
	 * Use virtual aperture address values for allocation.
	 */
	u32 *fake_registers;
};

struct hwpm_ip_element_info {
	/* Number of elements per instance */
	u32 num_element_per_inst;

	/*
	 * Static elements in this instance corresponding to aperture
	 * Array size: num_element_per_inst
	 */
	struct hwpm_ip_aperture *element_static_array;

	/*
	 * Ascending instance address range corresponding to elements
	 * NOTE: It is possible that address range of elements in the instance
	 * are not in same sequential order as their indexes.
	 * For example, 0th element of an instance can have start address higher
	 * than element 1. In this case, range_start and range_end should be
	 * initialized in increasing order.
	 */
	u64 range_start;
	u64 range_end;

	/* Element physical address stride for each element of IP instance */
	u64 element_stride;

	/*
	 * Elements that can fit into instance address range.
	 * This gives number of indices in element_arr
	 */
	u32 element_slots;

	/*
	 * Flag that indicates if number of slots computed is over limit
	 * If yes, usually, number of valid static slots will be small as
	 * compared to computed dynamic slots. That means allocating huge
	 * memory to store NULL pointers equal to computed element slots
	 * will fail and/or be impractical. Hence, if this flag is set,
	 * driver logic should fallback to brute force approach to match
	 * regops address.
	 */
	bool eslots_overlimit;

	/*
	 * Dynamic elements array corresponding to this element
	 * Array size: element_slots pointers
	 */
	struct hwpm_ip_aperture **element_arr;
};

struct hwpm_ip_inst {
	/*
	 * HW inst index : HW instance index of this instance
	 * This mask builds hwpm_ip.inst_fs_mask indicating availability.
	 */
	u32 hw_inst_mask;

	/*
	 * An IP instance is a group of core elements of the IP
	 * Eg., channels in MSS, controllers in PCIe
	 * Performance tracking is counted by 0, 1 or more HWPM components
	 * (perfmux/perfmon) connected to each IP core element.
	 *
	 */
	u32 num_core_elements_per_inst;

	/* Element details specific to this instance */
	struct hwpm_ip_element_info element_info[TEGRA_HWPM_APERTURE_TYPE_MAX];

	/*
	 * IP ops are specific for an instance, used for perfmux and broadcast
	 * register accesses.
	 */
	struct tegra_hwpm_ip_ops ip_ops;

	/*
	 * Some IPs set fuses to indicate floorsweeping info on platforms.
	 * This mask will contain fuse fs info if any.
	 */
	u32 fuse_fs_mask;

	/*
	 * An IP contains perfmux-perfmon groups that correspond to each other.
	 * If a perfmux is present, it indicates that the corresponding
	 * perfmon is present.
	 * This mask is usually updated based on available perfmuxes.
	 * (except for SCF).
	 */
	u32 element_fs_mask;

	/*
	 * dev_name corresponds to the name of the IP debug node that has been
	 * exposed to perform Regops Read/write operation
	 */
	char dev_name[64];
};

struct hwpm_ip_inst_per_aperture_info {
	/*
	 * Ascending IP address range corresponding to instances
	 * NOTE: It is possible that address range of IP instances
	 * are not in same sequential order as their indexes.
	 * For example, 0th instance of an IP can have start address higher
	 * than instance 1. In this case, range_start and range_end should be
	 * initialized in increasing order.
	 */
	u64 range_start;
	u64 range_end;

	/* Aperture address range for each IP instance */
	u64 inst_stride;

	/*
	 * Aperture instances that can fit into IP aperture address range.
	 * This gives number of entries in inst_arr
	 */
	u32 inst_slots;

	/*
	 * Flag that indicates if number of slots computed is over limit
	 * If yes, usually, number of valid static slots will be small as
	 * compared to computed dynamic slots. That means allocating huge
	 * memory to store NULL pointers equal to computed insyance slots
	 * will fail and/or be impractical. Hence, if this flag is set,
	 * driver logic should fallback to brute force approach to match
	 *  regops address.
	 */
	bool islots_overlimit;

	/* IP inst aperture array */
	struct hwpm_ip_inst **inst_arr;
};

struct hwpm_ip {
	/* Number of instances */
	u32 num_instances;

	/* Static array of IP instances */
	struct hwpm_ip_inst *ip_inst_static_array;

	/* Instance info corresponding to apertures in this IP */
	struct hwpm_ip_inst_per_aperture_info inst_aperture_info[
		TEGRA_HWPM_APERTURE_TYPE_MAX];

	/*
	 * Indicates fuses this IP depends on
	 * If fuse corresponding to the mask is blown,
	 * set override_enable = true
	 */
	u32 dependent_fuse_mask;

	/* Override IP config based on fuse value */
	bool override_enable;

	/*
	 * IP floorsweep info based on hw index of aperture
	 * NOTE: This mask needs to based on hw instance index because
	 * hwpm driver clients use hw instance index to find aperture
	 * info (start/end address) from hw manual.
	 */
	u32 inst_fs_mask;

	/*
	 * Resource status can be: TEGRA_HWPM_RESOURCE_STATUS_*
	 * - invalid:  resource is not available to be reserved
	 * - valid:    resource exists on the chip
	 * - reserved: resource is reserved
	 * - fault:    resource faulted during reservation
	 */
	u32 resource_status;
	bool reserved;
};

struct tegra_soc_hwpm;

struct tegra_soc_hwpm_chip {
	/* Max LA Clock rate */
	u64 la_clk_rate;

	/* Array of pointers to active IP structures */
	struct hwpm_ip **chip_ips;

	/* Chip HALs */
	bool (*validate_secondary_hals)(struct tegra_soc_hwpm *hwpm);
#ifdef __KERNEL__
	int (*clk_rst_prepare)(struct tegra_hwpm_os_linux *hwpm_linux);
	int (*clk_rst_set_rate_enable)(struct tegra_hwpm_os_linux *hwpm_linux);
	int (*clk_rst_disable)(struct tegra_hwpm_os_linux *hwpm_linux);
	void (*clk_rst_release)(struct tegra_hwpm_os_linux *hwpm_linux);
#else
        int (*clk_rst_prepare)(struct tegra_hwpm_os_qnx *hwpm_qnx);
        int (*clk_rst_set_rate_enable)(struct tegra_hwpm_os_qnx *hwpm_qnx);
        int (*clk_rst_disable)(struct tegra_hwpm_os_qnx *hwpm_qnx);
        void (*clk_rst_release)(struct tegra_hwpm_os_qnx *hwpm_qnx);
#endif
	bool (*is_ip_active)(struct tegra_soc_hwpm *hwpm,
	u32 ip_enum, u32 *config_ip_index);
	bool (*is_resource_active)(struct tegra_soc_hwpm *hwpm,
	u32 res_enum, u32 *config_ip_index);

	int (*get_rtr_pma_perfmux_ptr)(struct tegra_soc_hwpm *hwpm,
		struct hwpm_ip_aperture **rtr_perfmux_ptr,
		struct hwpm_ip_aperture **pma_perfmux_ptr);
	u32 (*get_rtr_int_idx)(void);
	u32 (*get_ip_max_idx)(void);

	int (*extract_ip_ops)(struct tegra_soc_hwpm *hwpm,
	u32 resource_enum, u64 base_address,
	struct tegra_hwpm_ip_ops *ip_ops, bool available);
	int (*force_enable_ips)(struct tegra_soc_hwpm *hwpm);
	int (*validate_current_config)(struct tegra_soc_hwpm *hwpm);
	int (*get_fs_info)(struct tegra_soc_hwpm *hwpm,
	u32 ip_enum, u64 *fs_mask, u8 *ip_status);
	int (*get_resource_info)(struct tegra_soc_hwpm *hwpm,
	u32 resource_enum, u8 *status);

	int (*init_prod_values)(struct tegra_soc_hwpm *hwpm);
	int (*disable_cg)(struct tegra_soc_hwpm *hwpm);
	int (*enable_cg)(struct tegra_soc_hwpm *hwpm);
	int (*credit_program)(struct tegra_soc_hwpm *hwpm,
		u32 *num_credits, u8 cblock_idx,
		u8 pma_channel_idx, uint16_t credit_cmd);
	int (*setup_trigger)(struct tegra_soc_hwpm *hwpm,
		u8 enable_cross_trigger, u8 session_type);
	int (*reserve_rtr)(struct tegra_soc_hwpm *hwpm);
	int (*release_rtr)(struct tegra_soc_hwpm *hwpm);

	int (*check_status)(struct tegra_soc_hwpm *hwpm);
	/**
	 * @brief Perform HWPM reset via soft-reset command.
	 *
	 * @returns This function returns 0 on success, negative errno on failure:
	 *   -ETIMEDOUT: soft reset sequence did not complete within timeout.
	 *   -ENOTRECOVERABLE: system is unstable and requires system-wide reset.
	 *   -EAGAIN: the cmd packet for soft reset maybe dropped and requires retry.
	 */
	int (*soft_reset)(struct tegra_soc_hwpm *hwpm);
	int (*disable_triggers)(struct tegra_soc_hwpm *hwpm);
	int (*perfmon_enable)(struct tegra_soc_hwpm *hwpm,
	struct hwpm_ip_aperture *perfmon);
	int (*perfmon_disable)(struct tegra_soc_hwpm *hwpm,
		struct hwpm_ip_aperture *perfmon);
	int (*perfmux_disable)(struct tegra_soc_hwpm *hwpm,
		struct hwpm_ip_aperture *perfmux);

	int (*disable_mem_mgmt)(struct tegra_soc_hwpm *hwpm);
	int (*enable_mem_mgmt)(struct tegra_soc_hwpm *hwpm);
	int (*invalidate_mem_config)(struct tegra_soc_hwpm *hwpm);
	int (*stream_mem_bytes)(struct tegra_soc_hwpm *hwpm);
	int (*disable_pma_streaming)(struct tegra_soc_hwpm *hwpm);
	int (*update_mem_bytes_get_ptr)(struct tegra_soc_hwpm *hwpm,
		u64 mem_bump);
	int (*get_mem_bytes_put_ptr)(struct tegra_soc_hwpm *hwpm,
		u64 *mem_head_ptr);
	int (*membuf_overflow_status)(struct tegra_soc_hwpm *hwpm,
		u32 *overflow_status);

	size_t (*get_alist_buf_size)(struct tegra_soc_hwpm *hwpm);
	int (*zero_alist_regs)(struct tegra_soc_hwpm *hwpm,
		struct hwpm_ip_inst *ip_inst,
		struct hwpm_ip_aperture *aperture);
	int (*copy_alist)(struct tegra_soc_hwpm *hwpm,
		struct hwpm_ip_aperture *aperture,
		u64 *full_alist,
		u64 *full_alist_idx);
	bool (*check_alist)(struct tegra_soc_hwpm *hwpm,
		struct hwpm_ip_aperture *aperture, u64 phys_addr);
};

/* Driver struct */
struct tegra_soc_hwpm {
	/* Active chip info */
	struct tegra_soc_hwpm_chip *active_chip;

	/* Memory Management */
	struct tegra_hwpm_mem_mgmt *mem_mgmt;
	struct tegra_hwpm_allowlist_map *alist_map;

	/* SW State */
	u32 dbg_mask;
	bool ip_config[TERGA_HWPM_NUM_IPS];
	bool bind_completed;
	bool device_opened;
	bool fake_registers_enabled;
	bool dbg_skip_alist;
	bool soft_reset_unstable;
};

#endif /* TEGRA_HWPM_H */
