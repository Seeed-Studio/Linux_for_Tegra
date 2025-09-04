#
# Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#

# platform configs
ENABLE_CONSOLE_SPE			:= 1
$(eval $(call add_define,ENABLE_CONSOLE_SPE))

ENABLE_STRICT_CHECKING_MODE		:= 0
$(eval $(call add_define,ENABLE_STRICT_CHECKING_MODE))

USE_GPC_DMA				:= 0
$(eval $(call add_define,USE_GPC_DMA))

RESET_TO_BL31				:= 1

COLD_BOOT_SINGLE_CPU			:= 1

# platform settings
PLAT_BL31_BASE				:= 0x50000000
$(eval $(call add_define,PLAT_BL31_BASE))

PLATFORM_CLUSTER_COUNT			:= 3
$(eval $(call add_define,PLATFORM_CLUSTER_COUNT))

PLATFORM_MAX_CPUS_PER_CLUSTER		:= 4
$(eval $(call add_define,PLATFORM_MAX_CPUS_PER_CLUSTER))

MAX_XLAT_TABLES				:= 25
$(eval $(call add_define,MAX_XLAT_TABLES))

MAX_MMAP_REGIONS			:= 30
$(eval $(call add_define,MAX_MMAP_REGIONS))

WORKAROUND_CVE_2017_5715		:= 0

DYNAMIC_WORKAROUND_CVE_2018_3639	:= 1

# Flag to apply erratum 2466780 workaround during reset.
ERRATA_A78_AE_2466780			:= 1

# Flag to apply erratum 2743093 workaround during pwrdown sequence.
ERRATA_A78_AE_2743093			:= 1

# Flag to apply erratum 2743229 workaround during reset.
ERRATA_A78_AE_2743229                   := 1

CTX_INCLUDE_AARCH32_REGS		:= 0

HW_ASSISTED_COHERENCY			:= 1

# Keep RAS disabled until latest DSIM support is released
RAS_EXTENSION				:= 1
HANDLE_EA_EL3_FIRST			:= 1

# Enable secure only access to GICT and GICP registers.
GICV3_RESTRICT_GICT_GICP_ACCESS		:= 1
$(eval $(call add_define,GICV3_RESTRICT_GICT_GICP_ACCESS))

# Enable GIC600AE Fault Management Unit
GICV3_SUPPORT_GIC600AE_FMU		:= 1

# include common makefiles
include plat/nvidia/tegra/common/tegra_common.mk

# platform files
PLAT_INCLUDES		+=	-Iplat/nvidia/tegra/include/t234	\
				-I${SOC_DIR}/drivers/include

BL31_SOURCES		+=	${TEGRA_GICv3_SOURCES}			\
				lib/cpus/aarch64/cortex_a78_ae.S	\
				${TEGRA_DRIVERS}/bpmp_ipc/intf.c	\
				${TEGRA_DRIVERS}/bpmp_ipc/ivc.c		\
				${TEGRA_DRIVERS}/gpcdma/gpcdma.c	\
				${TEGRA_DRIVERS}/memctrl/memctrl_v2.c	\
				${TEGRA_DRIVERS}/psc/psc_mailbox.c	\
				${SOC_DIR}/drivers/mce/mce.c		\
				${SOC_DIR}/drivers/mce/ari.c		\
				${SOC_DIR}/drivers/se/se.c		\
				${SOC_DIR}/plat_memctrl.c		\
				${SOC_DIR}/plat_psci_handlers.c		\
				${SOC_DIR}/plat_setup.c			\
				${SOC_DIR}/plat_secondary.c		\
				${SOC_DIR}/plat_sip_calls.c

ifeq (${ENABLE_CONSOLE_SPE}, 1)
BL31_SOURCES		+=	${TEGRA_DRIVERS}/spe/shared_console.S
else
BL31_SOURCES		+=	drivers/ti/uart/aarch64/16550_console.S
endif

# Pointer Authentication sources
ifeq (${ENABLE_PAUTH},1)
CTX_INCLUDE_PAUTH_REGS	:= 1
BL31_SOURCES		+=	plat/nvidia/tegra/common/tegra_pauth.c	\
				lib/extensions/pauth/pauth_helpers.S
endif

# RAS sources
ifeq (${RAS_EXTENSION},1)
BL31_SOURCES		+=	lib/extensions/ras/std_err_record.c	\
				lib/extensions/ras/ras_common.c		\
				${SOC_DIR}/plat_ras.c
endif
