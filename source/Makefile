# SPDX-FileCopyrightText: Copyright (c) 2023-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

KERNEL_HEADERS ?= /lib/modules/$(shell uname -r)/build
KERNEL_OUTPUT ?= ${KERNEL_HEADERS}

MAKEFILE_DIR := $(abspath $(shell dirname $(lastword $(MAKEFILE_LIST))))
NVIDIA_CONFTEST ?= ${MAKEFILE_DIR}/out/nvidia-conftest
NVIDIA_DTS_BUILD_SCRIPTS ?= $(realpath ${MAKEFILE_DIR}/kernel-devicetree)

OPENRM ?= 1
ifeq ($(OPENRM), 1)
NVIDIA_DISPLAY_DIR := unifiedgpudisp
else
NVIDIA_DISPLAY_DIR := nvdisplay
endif

ifneq ($(words $(subst :, ,$(MAKEFILE_DIR))), 1)
$(error source directory cannot contain spaces or colons)
endif

NPROC ?= $(shell nproc)

ifeq ("$(wildcard $(KERNEL_OUTPUT))","")
$(error kernel headers/output directory "$(KERNEL_OUTPUT)" does not exist!)
endif

.PHONY : help modules modules_install clean conftest hwpm nvidia-oot nvgpu

# help is default target!
help:
	@echo   "================================================================================"
	@echo   "Usage:"
	@echo   "   make modules          # to build NVIDIA OOT and display drivers"
	@echo   "   make dtbs             # to build NVIDIA DTBs"
	@echo   "   make modules_install  # to install drivers to the INSTALL_MOD_PATH"
	@echo   "   make clean            # to make clean driver sources"
	@echo   "================================================================================"

modules: hwpm nvidia-oot nvgpu nvidia-display
dtbs: nvidia-dtbs
modules_install: hwpm nvidia-oot nvgpu nvidia-display-install
clean: hwpm nvidia-oot nvgpu nvidia-display-clean nvidia-dtbs-clean conftest-clean


conftest:
ifeq ($(MAKECMDGOALS), modules)
	@echo   "================================================================================"
	@echo   "make $(MAKECMDGOALS) - conftest ..."
	@echo   "================================================================================"
	mkdir -p $(NVIDIA_CONFTEST)/nvidia;
	cp -av $(MAKEFILE_DIR)/nvidia-oot/scripts/conftest/* $(NVIDIA_CONFTEST)/nvidia/;
	$(MAKE) -j $(NPROC) ARCH=arm64 \
		src=$(NVIDIA_CONFTEST)/nvidia obj=$(NVIDIA_CONFTEST)/nvidia \
		CC=$(CROSS_COMPILE)gcc LD=$(CROSS_COMPILE)ld \
		NV_KERNEL_SOURCES=$(KERNEL_HEADERS) \
		NV_KERNEL_OUTPUT=$(KERNEL_OUTPUT) \
		-f $(NVIDIA_CONFTEST)/nvidia/Makefile
endif

hwpm: conftest
	@if [ ! -d "$(MAKEFILE_DIR)/hwpm" ] ; then \
		echo "Directory hwpm is not found, exiting.."; \
		false; \
	fi
	@echo   "================================================================================"
	@echo   "make $(MAKECMDGOALS) - hwpm ..."
	@echo   "================================================================================"
	$(MAKE) -j $(NPROC) ARCH=arm64 \
		-C $(KERNEL_OUTPUT) \
		M=$(MAKEFILE_DIR)/hwpm/drivers/tegra/hwpm \
		CONFIG_TEGRA_OOT_MODULE=m \
		srctree.hwpm=$(MAKEFILE_DIR)/hwpm \
		srctree.nvconftest=$(NVIDIA_CONFTEST) \
		$(MAKECMDGOALS)

nvidia-oot: conftest hwpm
	@if [ ! -d "$(MAKEFILE_DIR)/nvidia-oot" ] ; then \
		echo "Directory nvidia-oot is not found, exiting.."; \
		false; \
	fi
	@echo   "================================================================================"
	@echo   "make $(MAKECMDGOALS) - nvidia-oot ..."
	@echo   "================================================================================"
	$(MAKE) -j $(NPROC) ARCH=arm64 \
		-C $(KERNEL_OUTPUT) \
		M=$(MAKEFILE_DIR)/nvidia-oot \
		CONFIG_TEGRA_OOT_MODULE=m \
		srctree.nvidia-oot=$(MAKEFILE_DIR)/nvidia-oot \
		srctree.hwpm=$(MAKEFILE_DIR)/hwpm \
		srctree.nvconftest=$(NVIDIA_CONFTEST) \
		kernel_name=${kernel_name} \
		system_type=l4t \
		KBUILD_EXTRA_SYMBOLS=$(MAKEFILE_DIR)/hwpm/drivers/tegra/hwpm/Module.symvers \
		$(MAKECMDGOALS)

ifeq ($(OPENRM), 1)
nvgpu: ;
else
nvgpu: conftest nvidia-oot
	if [ ! -d "$(MAKEFILE_DIR)/nvgpu" ] ; then \
		echo "Directory nvgpu is not found, exiting.."; \
		false; \
	fi
	@echo   "================================================================================"
	@echo   "make $(MAKECMDGOALS) - nvgpu ..."
	@echo   "================================================================================"
	$(MAKE) -j $(NPROC) ARCH=arm64 \
		-C $(KERNEL_OUTPUT) \
		M=$(MAKEFILE_DIR)/nvgpu/drivers/gpu/nvgpu \
		CONFIG_TEGRA_OOT_MODULE=m \
		srctree.nvidia=$(MAKEFILE_DIR)/nvidia-oot \
		srctree.nvidia-oot=$(MAKEFILE_DIR)/nvidia-oot \
		srctree.nvconftest=$(NVIDIA_CONFTEST) \
		KBUILD_EXTRA_SYMBOLS=$(MAKEFILE_DIR)/nvidia-oot/Module.symvers \
		$(MAKECMDGOALS)
endif

define display-cmd
	$(MAKE) -j $(NPROC) ARCH=arm64 TARGET_ARCH=aarch64 \
		-C $(MAKEFILE_DIR)/$(NVIDIA_DISPLAY_DIR) \
		LOCALVERSION=$(version) \
		NV_VERBOSE=0 \
		KERNELRELEASE="" \
		SYSSRCNVOOT=$(MAKEFILE_DIR)/nvidia-oot \
		SYSSRC=$(KERNEL_HEADERS) \
		SYSOUT=$(KERNEL_OUTPUT) \
		CC=$(CROSS_COMPILE)gcc \
		LD=$(CROSS_COMPILE)ld.bfd \
		AR=$(CROSS_COMPILE)ar \
		CXX=$(CROSS_COMPILE)g++ \
		OBJCOPY=$(CROSS_COMPILE)objcopy
endef


nvidia-display: nvidia-oot
	@if [ ! -d "$(MAKEFILE_DIR)/$(NVIDIA_DISPLAY_DIR)" ] ; then \
		echo "Directory $(NVIDIA_DISPLAY_DIR) is not found, exiting.."; \
		false; \
	fi
	@echo   "================================================================================"
	@echo   "make $(MAKECMDGOALS) - nvidia-display ..."
	@echo   "================================================================================"
	$(display-cmd) modules
	@echo   "================================================================================"
	@echo   "Display driver compiled successfully."
	@echo   "================================================================================"


nvidia-display-install:
	@echo   "================================================================================"
	@echo   "make $(MAKECMDGOALS) - nvidia-display ..."
	@echo   "================================================================================"
	$(MAKE) -C $(KERNEL_OUTPUT) INSTALL_MOD_DIR=updates/opensrc-disp \
		M=$(MAKEFILE_DIR)/$(NVIDIA_DISPLAY_DIR)/kernel-open modules_install

nvidia-display-clean:
	@echo   "================================================================================"
	@echo   "make $(MAKECMDGOALS) - nvidia-display ..."
	@echo   "================================================================================"
	$(display-cmd) clean

nvidia-dtbs:
	@if [ ! -d "$(NVIDIA_DTS_BUILD_SCRIPTS)" ] ; then \
		echo "Directory kernel-devicetree is not found, exiting.."; \
		false; \
	fi
	@echo   "================================================================================"
	@echo   "make nvidia-dtbs ..."
	@echo   "================================================================================"
	TEGRA_TOP=$(MAKEFILE_DIR) \
	srctree=$(KERNEL_HEADERS) \
	objtree=$(KERNEL_OUTPUT) \
	oottree=$(NVIDIA_DTS_BUILD_SCRIPTS) \
	HOSTCC=gcc \
	$(MAKE) -f $(NVIDIA_DTS_BUILD_SCRIPTS)/scripts/Makefile.build \
		obj=$(NVIDIA_DTS_BUILD_SCRIPTS)/generic-dts \
		dtbs
	@echo   "================================================================================"
	@echo   "DTBs compiled successfully."
	@echo   "================================================================================"

nvidia-dtbs-clean:
	@echo   "================================================================================"
	@echo   "make $(MAKECMDGOALS) - nvidia-dtbs ..."
	@echo   "================================================================================"
	rm -fr $(NVIDIA_DTS_BUILD_SCRIPTS)/generic-dts/dtbs

conftest-clean:
	@echo   "================================================================================"
	@echo   "make $(MAKECMDGOALS) - conftest ..."
	@echo   "================================================================================"
	rm -fr $(NVIDIA_CONFTEST)

