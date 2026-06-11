# SPDX-FileCopyrightText: Copyright (c) 2023-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

KERNEL_SRC_DIR ?= kernel-noble
KERNEL_DEF_CONFIG ?= defconfig

MAKEFILE_DIR := $(abspath $(shell dirname $(lastword $(MAKEFILE_LIST))))
kernel_source_dir := $(MAKEFILE_DIR)/$(KERNEL_SRC_DIR)

ifdef KERNEL_OUTPUT
O_OPT := O=$(KERNEL_OUTPUT)
$(mkdir -p $(KERNEL_OUTPUT))
kernel_image := $(KERNEL_OUTPUT)/arch/arm64/boot/Image
else
kernel_image := $(kernel_source_dir)/arch/arm64/boot/Image
endif

NPROC ?= $(shell nproc)

# Determine if kernel is RT or not
NV_BUILD_KERNEL_IS_RT := $(shell grep -q "CONFIG_PREEMPT_RT=y" \
    ${kernel_source_dir}/arch/arm64/configs/${KERNEL_DEF_CONFIG} && echo "1" || echo "0")

# LOCALVERSION configuration: priority order is environment variable, then canonical SRU, then default
ifndef LOCALVERSION
    # Check if dpkg-parsechangelog utility exists
    HAS_DPKG_PARSECHANGELOG := $(shell command -v dpkg-parsechangelog >/dev/null 2>&1 && echo "1" || echo "0")

    ifeq ($(HAS_DPKG_PARSECHANGELOG),1)
        ifneq ($(wildcard $(kernel_source_dir)/debian.nvidia-tegra/changelog),)
            # Extract SRU from debian.nvidia-tegra/changelog if it exists
            NV_BUILD_CANONICAL_SRU := $(shell dpkg-parsechangelog -l $(kernel_source_dir)/debian.nvidia-tegra/changelog -S version 2>/dev/null | cut -d'-' -f2 | cut -d'.' -f1)
        endif

        # If NV_BUILD_CANONICAL_SRU was successfully extracted, use it
        ifneq ($(NV_BUILD_CANONICAL_SRU),)
            ifeq ($(NV_BUILD_KERNEL_IS_RT),1)
                LOCALVERSION := -$(NV_BUILD_CANONICAL_SRU)-rt-tegra
            else
                LOCALVERSION := -$(NV_BUILD_CANONICAL_SRU)-tegra
            endif
        endif
    endif # HAS_DPKG_PARSECHANGELOG

    # Fall back to default behavior if LOCALVERSION is still not set
    ifndef LOCALVERSION
        ifeq ($(NV_BUILD_KERNEL_IS_RT),1)
            LOCALVERSION := -rt-tegra
        else
            LOCALVERSION := -tegra
        endif
    endif
endif

.PHONY : kernel install clean help

kernel:
	@echo   "================================================================================"
	@echo   "Building $(KERNEL_SRC_DIR) sources"
	@echo   "================================================================================"
	$(MAKE) \
		ARCH=arm64 \
		-C $(kernel_source_dir) $(O_OPT) \
		LOCALVERSION=$(LOCALVERSION) \
		$(KERNEL_DEF_CONFIG)

	$(MAKE) -j $(NPROC) \
		ARCH=arm64 \
		-C $(kernel_source_dir) $(O_OPT) \
		LOCALVERSION=$(LOCALVERSION) \
		--output-sync=target Image

	$(MAKE) -j $(NPROC) \
		ARCH=arm64 \
		-C $(kernel_source_dir) $(O_OPT) \
		LOCALVERSION=$(LOCALVERSION) \
		--output-sync=target dtbs

	$(MAKE) -j $(NPROC) \
		ARCH=arm64 \
		-C $(kernel_source_dir) $(O_OPT) \
		LOCALVERSION=$(LOCALVERSION) \
		--output-sync=target modules

	@echo   "================================================================================"
	@if [ -f "$(kernel_image)" ] ; then \
		echo   "Kernel Image: $(kernel_image)"; \
	else \
		echo   "Error: Missing kernel image: $(kernel_image)"; \
        false ; \
	fi
	@echo   "Kernel sources compiled successfully."
	@echo   "================================================================================"


install:
	@echo   "================================================================================"
	@echo   "Installing $(KERNEL_SRC_DIR) sources"
	@echo   "================================================================================"
	install $(kernel_image) $(INSTALL_MOD_PATH)/boot/
	$(MAKE) \
		ARCH=arm64 \
		-C $(kernel_source_dir) $(O_OPT) \
		LOCALVERSION=$(LOCALVERSION) \
		INSTALL_MOD_PATH=$(INSTALL_MOD_PATH) \
		modules_install
	@echo   "================================================================================"
	@echo   "Kernel and in-tree modules installed successfully."
	@echo   "================================================================================"


clean:
	@echo   "================================================================================"
	@echo   "Cleaning $(KERNEL_SRC_DIR) sources"
	@echo   "================================================================================"
	$(MAKE) \
		ARCH=arm64 \
		-C $(kernel_source_dir) $(O_OPT) \
		mrproper
	@echo   "================================================================================"
	@echo   "Kernel and in-tree modules installed successfully."
	@echo   "================================================================================"

# make help
help:
	@echo   "================================================================================"
	@echo   "Usage:"
	@echo   "   make or make kernel   # to build kernel"
	@echo   "   make install          # to install kernel image and in-tree modules"
	@echo   "   make clean            # to make clean kernel source"
	@echo   "================================================================================"
