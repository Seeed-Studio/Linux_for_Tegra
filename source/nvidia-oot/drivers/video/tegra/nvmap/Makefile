# SPDX-License-Identifier: GPL-2.0-only
# SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

GCOV_PROFILE := y

subdir-ccflags-y += -Werror

include $(srctree.nvidia-oot)/drivers/video/tegra/nvmap/Makefile.memory.configs

# If the kernel enables CONFIG_GENERIC_IOREMAP,
# then pass this flag for building NVMAP.
ifdef CONFIG_GENERIC_IOREMAP
ccflags-y += -DCONFIG_GENERIC_IOREMAP
endif

ifeq ($(NVMAP_CONFIG), y)
# Build NvMap only when NVMAP_CONFIG is set to y
nvmap-y := nvmap_core.o \
	nvmap_alloc.o \
	nvmap_cache.o \
	nvmap_dev.o \
	nvmap_dmabuf.o \
	nvmap_fault.o \
	nvmap_handle.o \
	nvmap_heap.o \
	nvmap_ioctl.o \
	nvmap_init.o \
	nvmap_tag.o \
	nvmap_stats.o \
	nvmap_carveout.o \
	nvmap_kasan_wrapper.o

ifdef CONFIG_DEBUG_FS
nvmap-y += nvmap_debug.o
endif

nvmap-$(NVMAP_CONFIG_HANDLE_AS_ID) += nvmap_id_array.o

nvmap-$(NVMAP_CONFIG_SCIIPC) += nvmap_sci_ipc.o

ifeq ($(NVMAP_CONFIG_PAGE_POOLS), y)
nvmap-y += nvmap_pp.o
endif #NVMAP_CONFIG_PAGE_POOLS

KASAN_SANITIZE_nvmap_kasan_wrapper.o := n

nvmap-y += nvmap_cache_maint.o
obj-m += nvmap.o
endif #NVMAP_CONFIG
