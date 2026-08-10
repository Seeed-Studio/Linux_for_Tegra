# SPDX-License-Identifier: GPL-2.0-only
# SPDX-FileCopyrightText: Copyright (c) 2017-2024, NVIDIA CORPORATION.  All rights reserved.

ifeq ($(origin srctree.nvidia), undefined)
srctree.nvidia := $(srctree.nvidia-oot)
endif

ccflags-y += -I$(srctree.nvidia)/include
ccflags-y += -DMODS_HAS_TEGRA

ifeq ($(CONFIG_ARM_FFA_TRANSPORT),m)
    ifdef ALLOW_ARM_FFA_TRANSPORT_AS_MODULE
        HAVE_ARM_FFA = $(CONFIG_ARM_FFA_TRANSPORT)
        ccflags-y += -DMODS_HAS_ARM_FFA
    endif
else
    ifeq ($(CONFIG_ARM_FFA_TRANSPORT),y)
        HAVE_ARM_FFA = $(CONFIG_ARM_FFA_TRANSPORT)
        ccflags-y += -DMODS_HAS_ARM_FFA
    endif
endif
HAVE_ARM_FFA ?=

obj-m += mods.o

mods-y                             := mods_irq.o
mods-y                             += mods_krnl.o
mods-y                             += mods_mem.o

mods-$(CONFIG_ACPI)                += mods_acpi.o
mods-$(CONFIG_ARCH_TEGRA)          += mods_adsp.o
mods-$(HAVE_ARM_FFA)               += mods_arm_ffa.o
mods-$(CONFIG_TEGRA_IVC)           += mods_bpmpipc.o
mods-$(CONFIG_COMMON_CLK)          += mods_clock.o
mods-$(CONFIG_DEBUG_FS)            += mods_debugfs.o
mods-$(CONFIG_DMA_ENGINE)          += mods_dma.o
mods-$(CONFIG_DMA_SHARED_BUFFER)   += mods_dmabuf.o
mods-$(CONFIG_ARCH_TEGRA)          += mods_ipi.o
mods-$(CONFIG_NET)                 += mods_netdevice.o
mods-$(CONFIG_ARCH_TEGRA)          += mods_oist.o
mods-$(CONFIG_OPTEE)               += mods_optee.o
mods-$(CONFIG_PCI)                 += mods_pci.o
mods-$(CONFIG_ARCH_TEGRA_19x_SOC)  += mods_ras.o
mods-$(CONFIG_ARCH_TEGRA)          += mods_smmu_drv.o
mods-$(CONFIG_ARCH_TEGRA)          += mods_tegraprod.o
mods-$(CONFIG_TRUSTY)              += mods_tz.o

mods-objs                          := mods.dtb.o
