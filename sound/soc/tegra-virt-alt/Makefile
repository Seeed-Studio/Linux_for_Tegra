# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2022-2024, NVIDIA CORPORATION.  All rights reserved.

GCOV_PROFILE := y

subdir-ccflags-y += -Werror

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

snd-soc-tegra210-virt-alt-adsp-objs := tegra210_adsp_virt_alt.o
obj-m += snd-soc-tegra210-virt-alt-adsp.o
