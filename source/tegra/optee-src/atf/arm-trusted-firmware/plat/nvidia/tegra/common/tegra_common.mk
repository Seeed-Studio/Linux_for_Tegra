#
# Copyright (c) 2015-2020, ARM Limited and Contributors. All rights reserved.
# Copyright (c) 2020, NVIDIA Corporation. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#

PLAT_INCLUDES		:=	-Iplat/nvidia/tegra/include/drivers \
				-Iplat/nvidia/tegra/include/lib \
				-Iplat/nvidia/tegra/include

include lib/xlat_tables_v2/xlat_tables.mk
PLAT_BL_COMMON_SOURCES	+=	${XLAT_TABLES_LIB_SRCS}

TEGRA_COMMON		:=	plat/nvidia/tegra/common
TEGRA_DRIVERS		:=	plat/nvidia/tegra/drivers
TEGRA_LIBS		:=	plat/nvidia/tegra/lib

# Enable support for GIC600
GICV3_SUPPORT_GIC600	:=	1
GICV3_OVERRIDE_DISTIF_PWR_OPS	:=	1

# Include GICv3 driver files
include drivers/arm/gic/v3/gicv3.mk
TEGRA_GICv3_SOURCES	:=	$(GICV3_SOURCES)				\
				plat/common/plat_gicv3.c			\
				${TEGRA_COMMON}/tegra_gicv3.c

# Include GICv2 driver files
include drivers/arm/gic/v2/gicv2.mk

TEGRA_GICv2_SOURCES	:=	${GICV2_SOURCES}				\
				plat/common/plat_gicv2.c			\
				${TEGRA_COMMON}/tegra_gicv2.c

BL31_SOURCES		+=	drivers/delay_timer/delay_timer.c		\
				drivers/io/io_storage.c				\
				plat/common/aarch64/crash_console_helpers.S	\
				${TEGRA_COMMON}/aarch64/tegra_helpers.S		\
				${TEGRA_LIBS}/debug/profiler.c			\
				${TEGRA_COMMON}/tegra_bl31_setup.c		\
				${TEGRA_COMMON}/tegra_delay_timer.c		\
				${TEGRA_COMMON}/tegra_fiq_glue.c		\
				${TEGRA_COMMON}/tegra_io_storage.c		\
				${TEGRA_COMMON}/tegra_platform.c		\
				${TEGRA_COMMON}/tegra_pm.c			\
				${TEGRA_COMMON}/tegra_sip_calls.c		\
				${TEGRA_COMMON}/tegra_sdei.c

ifneq ($(ENABLE_STACK_PROTECTOR), 0)
BL31_SOURCES		+=	${TEGRA_COMMON}/tegra_stack_protector.c
endif
ifeq (${EL3_EXCEPTION_HANDLING},1)
BL31_SOURCES		+=	plat/common/aarch64/plat_ehf.c
endif
