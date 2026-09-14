# SPDX-License-Identifier: GPL-2.0-only
# SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

ifdef CONFIG_TEGRA_HOST1X
ifneq ($(NV_OOT_NVVSE_SKIP_BUILD),y)

obj-m += nvvse.o
ccflags-nvvse-common-y += -DLINUX_KBUILD \
	-I$(srctree.nvidia-oot)/drivers/crypto/nvvse/nvvse_osi/common/core_unit/public \
	-I$(srctree.nvidia-oot)/drivers/crypto/nvvse/nvvse_osi/common/core_unit \
	-I$(srctree.nvidia-oot)/drivers/crypto/nvvse/nvvse_osi/common/se_server_msg/include \
	-I$(srctree.nvidia-oot)/drivers/crypto/nvvse/nvvse_osi/common/soc_plugin/public \
	-I$(srctree.nvidia-oot)/drivers/crypto/nvvse/nvvse_osi/common/include \
	-I$(srctree.nvidia-oot)/drivers/crypto/nvvse/nvvse_osi/include \
	-I$(srctree.nvidia-oot)/drivers/crypto/nvvse/ \
	-I$(srctree.nvidia-oot)/drivers/crypto/nvvse/job-completion \
	-I$(srctree.nvidia-oot)/drivers/crypto/nvvse/comms-lib \
	-I$(srctree.nvidia-oot)/drivers/crypto/nvvse/memserv \
	-I$(srctree.nvidia-oot)/drivers/crypto/nvvse/core \
	-I$(srctree.nvidia-oot)/drivers/crypto/nvvse/soc-plugin

ccflags-y += $(ccflags-nvvse-common-y)

nvvse-common-y += nvvse-main.o \
	nvvse-dev.o \
	nvvse-common.o \
	comms-lib/nvvse-comms-lib.o \
	memserv/nvvse-mem-serv.o \
	nvvse_osi/common/core_unit/core_common.o \
	nvvse_osi/common/se_server_msg/se_server_msg.o \
	nvvse_osi/common/se_server_msg/se_server_msg_sha.o \
	nvvse_osi/common/se_server_msg/se_server_msg_hmac.o \
	nvvse_osi/common/core_unit/core_sha.o \
	nvvse_osi/common/core_unit/core_hmac.o \
	core/nvvse-core-sha.o \
	core/nvvse-core-hmac-sha.o

nvvse-objs := $(nvvse-common-y)

obj-m += nvvse-soc-plugin-t26x.o
ccflags-nvvse-soc-plugin-t26x-y += -I$(srctree.nvidia-oot)/drivers/crypto/nvvse/nvvse_osi/common/soc_plugin/t26x/public \
	-I$(srctree.nvidia-oot)/drivers/crypto/nvvse/soc-plugin
ccflags-y += $(ccflags-nvvse-soc-plugin-t26x-y)

nvvse-soc-plugin-t26x-y += nvvse_osi/common/soc_plugin/t26x/soc_plugin_sha.o \
	nvvse_osi/common/soc_plugin/t26x/soc_plugin_hmac.o \
	nvvse_osi/common/soc_plugin/t26x/soc_plugin_common.o \
	soc-plugin/soc-plugin-common.o \
	soc-plugin/t26x/t26x-plugin.o \
	job-completion/nvvse-job-completion.o \
	nvvse-common.o

nvvse-soc-plugin-t26x-objs := $(nvvse-soc-plugin-t26x-y)

obj-m += nvvse-soc-plugin-t23x.o
ccflags-nvvse-soc-plugin-t23x-y += -I$(srctree.nvidia-oot)/drivers/crypto/nvvse/nvvse_osi/common/soc_plugin/t23x/public \
	-I$(srctree.nvidia-oot)/drivers/crypto/nvvse/soc-plugin
ccflags-y += $(ccflags-nvvse-soc-plugin-t23x-y)

nvvse-soc-plugin-t23x-y += nvvse_osi/common/soc_plugin/t23x/soc_plugin_sha.o \
	nvvse_osi/common/soc_plugin/t23x/soc_plugin_hmac.o \
	nvvse_osi/common/soc_plugin/t23x/soc_plugin_common.o \
	soc-plugin/soc-plugin-common.o \
	soc-plugin/t23x/t23x-plugin.o \
	job-completion/nvvse-job-completion.o \
	nvvse-common.o

nvvse-soc-plugin-t23x-objs := $(nvvse-soc-plugin-t23x-y)

endif
endif
