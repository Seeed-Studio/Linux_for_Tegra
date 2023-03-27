# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.

GCOV_PROFILE := y

LINUXINCLUDE += -I$(srctree.nvidia-oot)/drivers/video/tegra/tsec

obj-m += tsecriscv.o
tsecriscv-y := tsec_comms/tsec_comms.o tsec_t23x.o tsec_t264.o tsec_boot.o tsec.o
