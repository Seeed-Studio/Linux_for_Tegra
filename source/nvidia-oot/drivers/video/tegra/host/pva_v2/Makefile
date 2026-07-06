# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2023-2024, NVIDIA CORPORATION.  All rights reserved.

ifndef CONFIG_TEGRA_SYSTEM_TYPE_ACK
ifeq ($(CONFIG_TEGRA_OOT_MODULE),m)

ifeq ($(findstring ack_src,$(NV_BUILD_KERNEL_OPTIONS)),)

GCOV_PROFILE := y
ccflags-y += -I$(srctree.nvidia-oot)/drivers/video/tegra/host/pva
ccflags-y += -I$(srctree.nvidia-oot)/drivers/video/tegra/host/pva/fw_include
ccflags-y += -Werror

ccflags-y += -DCONFIG_TEGRA_HOST1X
ccflags-y += -DCONFIG_TEGRA_FUSE_UPSTREAM
ccflags-y += -DTEGRA_OOT_MODULE

ccflags-y += -DCONFIG_TEGRA_T26X_GRHOST_PVA
ccflags-y += -DNVPVA_CONFIG_T264

nvhost-pva-objs  = \
		pva.o \
		pva_ioctl.o \
		pva_mailbox.o \
		pva_interface_regs_t19x.o \
		pva_version_config_t19x.o \
		pva_mailbox_t19x.o \
		pva_isr.o \
		pva_queue.o \
		pva_debug.o \
		pva_trace.o \
		pva_abort.o \
		pva_ccq_t19x.o \
		nvpva_elf_parser.o \
		pva_vpu_exe.o \
		nvpva_client.o \
		nvpva_queue.o \
		pva_dma.o \
		nvpva_buffer.o \
		pva_vpu_ocd.o \
		pva_sha256.o \
		pva_system_allow_list.o \
		pva_vpu_app_auth.o \
		pva_iommu_context_dev.o \
		nvpva_syncpt.o \
		pva_fw_carveout.o \
		pva_isr_t23x.o \
		pva_mailbox_t23x.o \
		pva_interface_regs_t23x.o \
		pva_version_config_t23x.o \
		pva_ccq_t23x.o \
		pva_sec_ec.o

obj-m += nvhost-pva.o
endif
endif
endif
