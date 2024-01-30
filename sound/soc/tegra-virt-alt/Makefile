# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2022-2023, NVIDIA CORPORATION.  All rights reserved.

GCOV_PROFILE := y

subdir-ccflags-y += -Werror

ifndef CONFIG_TEGRA_SYSTEM_TYPE_ACK
ccflags-y += -I$(overlay)/$(src)/../tegra-virt-alt/include/
ccflags-y += -I$(overlay)/$(src)/../codecs
else
ccflags-y += -I$(srctree.nvidia-oot)/sound/soc/tegra-virt-alt/include/
endif

ccflags-y += -I$(srctree.nvidia-oot)/sound/soc/tegra-virt-alt/nvaudio_ivc/

# Tegra platform Support

snd-soc-tegra210-virt-alt-admaif-objs := tegra210_virt_alt_admaif.o \
					tegra_asoc_xbar_virt_alt.o \
					tegra_asoc_util_virt_alt.o \
					tegra_asoc_machine_virt_alt.o \
					tegra_pcm_virt_alt.o \
					nvaudio_ivc/tegra_virt_alt_ivc.o

snd-soc-tegra-virt-t210ref-pcm-objs := tegra_virt_ref_alt.o
obj-m += snd-soc-tegra210-virt-alt-admaif.o
obj-m += snd-soc-tegra-virt-t210ref-pcm.o
