#SPDX-License-Identifier: GPL-2.0-only
#Copyright (c) 2019-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

OSI_COMMON := nvethernetrm/osi/common
OSI_CORE := nvethernetrm/osi/core
OSI_DMA := nvethernetrm/osi/dma
OSI_MACSEC := nvethernetrm/osi/nvmacsecrm
OSI_XPCS := nvethernetrm/osi/nvxpcsrm

obj-m += nvethernet.o

# If CONFIG_TEGRA_NVPPS is not set, enable by default
CONFIG_TEGRA_NVPPS ?= y
ifeq ($(CONFIG_TEGRA_NVPPS),y)
ccflags-y += -DCONFIG_TEGRA_NVPPS
endif

# These CFLAGS must not be shared/used in OSI. These are local to Linux
ccflags-y += -DLINUX_OS -DNET30 -DNVPKCS_MACSEC -DLINUX_IVC -mno-outline-atomics -Werror \
	     -I$(srctree.nvidia-oot)/drivers/net/ethernet/nvidia/nvethernet/nvethernetrm/include

ccflags-y += -Wframe-larger-than=4096

#ccflags-y += -DOSI_DEBUG -DMACSEC_SUPPORT -DDEBUG_MACSEC -DMACSEC_KEY_PROGRAM
ccflags-y += -DMACSEC_SUPPORT
ccflags-y += -DBW_TEST

nvethernet-objs:= ether_linux.o \
		  osd.o \
		  ethtool.o \
		  ether_tc.o \
		  sysfs.o \
		  ioctl.o \
		  ptp.o \
		  macsec.o \
		  selftests.o \
		  ether_test_tx.o \
		  $(OSI_CORE)/osi_core.o \
		  $(OSI_CORE)/osi_hal.o \
		  $(OSI_CORE)/common_macsec.o \
		  $(OSI_MACSEC)/macsec.o \
		  $(OSI_XPCS)/nvxpcs.o \
		  $(OSI_DMA)/osi_dma.o \
		  $(OSI_DMA)/osi_dma_txrx.o \
		  $(OSI_CORE)/eqos_core.o \
		  $(OSI_CORE)/ivc_core.o \
		  $(OSI_CORE)/mgbe_core.o \
		  $(OSI_CORE)/core_common.o \
		  $(OSI_CORE)/xpcs.o \
		  $(OSI_DMA)/mgbe_dma.o \
		  $(OSI_CORE)/eqos_mmc.o \
		  $(OSI_DMA)/eqos_dma.o \
		  $(OSI_DMA)/eqos_desc.o \
		  $(OSI_DMA)/mgbe_desc.o \
		  $(OSI_DMA)/debug.o \
		  $(OSI_CORE)/mgbe_mmc.o \
		  $(OSI_CORE)/frp.o \
		  $(OSI_CORE)/vlan_filter.o \
		  $(OSI_CORE)/debug.o

include $(srctree.nvidia-oot)/drivers/net/ethernet/nvidia/nvethernet/nvethernetrm/include/config.tmk

ifeq ($(NV_BUILD_CONFIGURATION_IS_VLTEST),1)
ccflags-y += -DNV_VLTEST_BUILD
endif

# Undefine HSI_SUPPORT if CONFIG_TEGRA_EPL is not defined.
ccflags-y += $(if $(CONFIG_TEGRA_EPL),,-UHSI_SUPPORT)
