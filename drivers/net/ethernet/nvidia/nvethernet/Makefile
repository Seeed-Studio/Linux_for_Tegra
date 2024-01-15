#SPDX-License-Identifier: GPL-2.0-only
#Copyright (c) 2019-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

OSI_COMMON := nvethernetrm/osi/common
OSI_CORE := nvethernetrm/osi/core
OSI_DMA := nvethernetrm/osi/dma

obj-m += nvethernet.o

# If CONFIG_TEGRA_NVPPS is not set, enable by default
CONFIG_TEGRA_NVPPS ?= y
ifeq ($(CONFIG_TEGRA_NVPPS),y)
ccflags-y += -DCONFIG_TEGRA_NVPPS
endif

ifndef CONFIG_TEGRA_SYSTEM_TYPE_ACK
# These CFLAGS must not be shared/used in OSI. These are local to Linux
ccflags-y += -DLINUX_OS -DNET30 -DNVPKCS_MACSEC -DLINUX_IVC -mno-outline-atomics -Werror=frame-larger-than=2048 \
	     -I$(srctree.nvidia-oot)/drivers/net/ethernet/nvidia/nvethernet/nvethernetrm/include
else
ccflags-y += -DLINUX_OS -DNET30 -DNVPKCS_MACSEC -DLINUX_IVC \
	     -I$(srctree.nvidia-oot)/drivers/net/ethernet/nvidia/nvethernet/nvethernetrm/include
endif

nvethernet-objs:= ether_linux.o \
		  osd.o \
		  ethtool.o \
		  ether_tc.o \
		  sysfs.o \
		  ioctl.o \
		  ptp.o \
		  macsec.o \
		  selftests.o \
		  $(OSI_CORE)/osi_core.o \
		  $(OSI_CORE)/osi_hal.o \
		  $(OSI_CORE)/macsec.o \
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

# Undefine HSI_SUPPORT if CONFIG_TEGRA_EPL is not defined.
ccflags-y += $(if $(CONFIG_TEGRA_EPL),,-UHSI_SUPPORT)
