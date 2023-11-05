# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2022-2023, NVIDIA CORPORATION.  All rights reserved.

ifdef CONFIG_TEGRA_HOST1X
obj-m += tegra-hv-vse-safety.o
obj-m += tegra-nvvse-cryptodev.o
ifdef CONFIG_CRYPTO_ENGINE
ifndef CONFIG_SKIP_CRYPTO
obj-m += tegra/
endif
endif
endif
obj-m += tegra-se-nvrng.o
