# SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: GPL-2.0-only


ifndef CONFIG_TEGRA_SYSTEM_TYPE_ACK
ifeq ($(CONFIG_TEGRA_OOT_MODULE),m)

ifeq ($(findstring ack_src,$(NV_BUILD_KERNEL_OPTIONS)),)

obj-m := nvhost-pva.o


PVA_SYS_DIR := .
PVA_SYS_ABSDIR := $(srctree.nvidia-oot)/drivers/video/tegra/host/pva


###### Begin generated section ######
pva_objs += \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_abort.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_block_allocator.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_cmdbuf.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_context.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_debugfs.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_device.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_devmem_pool.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_dma_cfg.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_dma_cfg_binding.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_dma_cfg_validate.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_dma_cfg_write.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_fw_debug.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_fw_profiler.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_hwseq_validate.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_msg.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_op_handler.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_pm.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_queue.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_resource_table.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_sha256.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_shared_buffer.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_silicon_boot.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_silicon_elf_parser.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_silicon_executable.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_silicon_hwpm.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_silicon_isr.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_silicon_misc.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_submitter.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_t23x.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_t26x.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_tegra_stats.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_utils.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_vpu_app_auth.o \
    $(PVA_SYS_DIR)/src/kmd/common/pva_kmd_vpu_ocd.o \
    $(PVA_SYS_DIR)/src/kmd/linux/pva_kmd_linux_debugfs.o \
    $(PVA_SYS_DIR)/src/kmd/linux/pva_kmd_linux_device.o \
    $(PVA_SYS_DIR)/src/kmd/linux/pva_kmd_linux_device_api.o \
    $(PVA_SYS_DIR)/src/kmd/linux/pva_kmd_linux_device_memory.o \
    $(PVA_SYS_DIR)/src/kmd/linux/pva_kmd_linux_driver.o \
    $(PVA_SYS_DIR)/src/kmd/linux/pva_kmd_linux_event_trace.o \
    $(PVA_SYS_DIR)/src/kmd/linux/pva_kmd_linux_ioctl.o \
    $(PVA_SYS_DIR)/src/kmd/linux/pva_kmd_linux_isr.o \
    $(PVA_SYS_DIR)/src/kmd/linux/pva_kmd_linux_misc.o \
    $(PVA_SYS_DIR)/src/kmd/linux/pva_kmd_linux_smmu.o \
    $(PVA_SYS_DIR)/src/kmd/linux/pva_kmd_linux_vpu_app_auth.o \

pva_inc_flags += \
    -I$(PVA_SYS_ABSDIR)/src/kmd/include \
    -I$(PVA_SYS_ABSDIR)/src/kmd/common \
    -I$(PVA_SYS_ABSDIR)/src/kmd/common/shim \
    -I$(PVA_SYS_ABSDIR)/src/kmd/linux/include \
    -I$(PVA_SYS_ABSDIR)/src/libs/pva/include \
    -I$(PVA_SYS_ABSDIR)/src/include \
    -I$(PVA_SYS_ABSDIR)/src/private_api \
    -I$(PVA_SYS_ABSDIR)/src/fw/include \
    -I$(PVA_SYS_ABSDIR)/src/fw/baremetal/include \

pva_def_flags += \
    -DPVA_BUILD_MODE=PVA_BUILD_MODE_L4T \
    -DPVA_BUILD_MODE_BAREMETAL=5 \
    -DPVA_BUILD_MODE_L4T=3 \
    -DPVA_BUILD_MODE_NATIVE=1 \
    -DPVA_BUILD_MODE_QNX=2 \
    -DPVA_BUILD_MODE_SIM=4 \
    -DPVA_DEV_MAIN_COMPATIBLE=1 \
    -DPVA_ENABLE_CUDA=1 \
    -DPVA_ENABLE_R5_OCD=0 \
    -DPVA_IS_DEBUG=0 \
    -DPVA_SAFETY=0 \
    -DPVA_SKIP_SYMBOL_TYPE_CHECK \
    -DPVA_SUPPORT_XBAR_RAW=1 \
    -Dpva_kmd_linux_dummy_EXPORTS \

###### End generated section ######


nvhost-pva-objs += $(pva_objs)
ccflags-y += $(pva_inc_flags)
ccflags-y += $(pva_def_flags)
ccflags-y += -std=gnu11

endif
endif
endif
