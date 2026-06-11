# SPDX-FileCopyrightText: Copyright (c) 2023-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

KERNEL_HEADERS ?= /lib/modules/$(shell uname -r)/build
KERNEL_OUTPUT ?= $(KERNEL_HEADERS)

MAKEFILE_DIR := $(abspath $(shell dirname $(lastword $(MAKEFILE_LIST))))
NVIDIA_CONFTEST ?= $(MAKEFILE_DIR)/out/nvidia-conftest
NVIDIA_DTS_BUILD_SCRIPTS ?= $(realpath $(MAKEFILE_DIR)/build/nvidia-public/devicetree)

ifdef OE_BUILD
  LD_BFD := $(LD)
else
  AR := $(CROSS_COMPILE)ar
  CC := $(CROSS_COMPILE)gcc
  CXX := $(CROSS_COMPILE)g++
  LD := $(CROSS_COMPILE)ld
  LD_BFD := $(CROSS_COMPILE)ld.bfd
  OBJCOPY := $(CROSS_COMPILE)objcopy
  NPROC ?= $(shell nproc)
  PARALLEL := -j$(NPROC)
endif

HOSTCC ?= gcc
V ?= 0

NVIDIA_GPU_DISPLAY_SRC_DIR := unifiedgpudisp
NVIDIA_GPU_DISPLAY_INSTALL_DIR := updates/opensource-gpu-disp
NVIDIA_DISPLAY_SRC_DIR := nvdisplay
NVIDIA_DISPLAY_INSTALL_DIR := updates/opensrc-disp
NVIDIA_DISPLAY_MODULE_TARGETS ?= nvidia-display nvidia-gpu-display
NVIDIA_DISPLAY_MODULE_INSTALL_TARGETS := $(addsuffix -install,$(NVIDIA_DISPLAY_MODULE_TARGETS))
NVIDIA_DISPLAY_MODULE_CLEAN_TARGETS := $(addsuffix -clean,$(NVIDIA_DISPLAY_MODULE_TARGETS))

ifneq ($(words $(subst :, ,$(MAKEFILE_DIR))), 1)
  $(error source directory cannot contain spaces or colons)
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

modules: hwpm nvidia-oot nvgpu $(NVIDIA_DISPLAY_MODULE_TARGETS)
dtbs: nvidia-dtbs
modules_install: hwpm nvidia-oot nvgpu \
	$(NVIDIA_DISPLAY_MODULE_INSTALL_TARGETS)
clean: hwpm nvidia-oot nvgpu $(NVIDIA_DISPLAY_MODULE_CLEAN_TARGETS) \
	nvidia-dtbs-clean conftest-clean


conftest:
ifeq ($(MAKECMDGOALS), modules)
	@echo   "================================================================================"
	@echo   "make $(MAKECMDGOALS) - conftest ..."
	@echo   "================================================================================"
	mkdir -p $(NVIDIA_CONFTEST)/nvidia;
	cp -av $(MAKEFILE_DIR)/nvidia-oot/scripts/conftest/* $(NVIDIA_CONFTEST)/nvidia/;
	$(MAKE) $(PARALLEL) ARCH=arm64 \
		src=$(NVIDIA_CONFTEST)/nvidia obj=$(NVIDIA_CONFTEST)/nvidia \
		CC="$(CC)" LD="$(LD)" \
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
	$(MAKE) $(PARALLEL) ARCH=arm64 \
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
	$(MAKE) $(PARALLEL) ARCH=arm64 \
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

nvgpu: conftest nvidia-oot
	if [ ! -d "$(MAKEFILE_DIR)/nvgpu" ] ; then \
		echo "Directory nvgpu is not found, exiting.."; \
		false; \
	fi
	@echo   "================================================================================"
	@echo   "make $(MAKECMDGOALS) - nvgpu ..."
	@echo   "================================================================================"
	$(MAKE) $(PARALLEL) ARCH=arm64 \
		-C $(KERNEL_OUTPUT) \
		M=$(MAKEFILE_DIR)/nvgpu/drivers/gpu/nvgpu \
		CONFIG_TEGRA_OOT_MODULE=m \
		srctree.nvidia=$(MAKEFILE_DIR)/nvidia-oot \
		srctree.nvidia-oot=$(MAKEFILE_DIR)/nvidia-oot \
		srctree.nvconftest=$(NVIDIA_CONFTEST) \
		KBUILD_EXTRA_SYMBOLS=$(MAKEFILE_DIR)/nvidia-oot/Module.symvers \
		$(MAKECMDGOALS)

define display-cmd
	$(MAKE) $(PARALLEL) ARCH=arm64 TARGET_ARCH=aarch64 \
		-C $(MAKEFILE_DIR)/$(1) \
		NV_VERBOSE=$(V) \
		KERNELRELEASE="" \
		SYSSRCNVOOT=$(MAKEFILE_DIR)/nvidia-oot \
		OOTSRC=$(MAKEFILE_DIR)/nvidia-oot \
		SYSSRC=$(KERNEL_HEADERS) \
		SYSOUT=$(KERNEL_OUTPUT) \
		KBUILD_EXTRA_SYMBOLS=$(MAKEFILE_DIR)/nvidia-oot/Module.symvers \
		CC="$(CC)" \
		LD="$(LD_BFD)" \
		AR="$(AR)" \
		CXX="$(CXX)" \
		OBJCOPY="$(OBJCOPY)"
endef

nvidia-display: nvidia-oot
	@if [ ! -d "$(MAKEFILE_DIR)/$(NVIDIA_DISPLAY_SRC_DIR)" ] ; then \
		echo "Directory $(NVIDIA_DISPLAY_SRC_DIR) is not found, exiting.."; \
		false; \
	fi
	@echo   "================================================================================"
	@echo   "make $(MAKECMDGOALS) - nvidia-display ..."
	@echo   "================================================================================"
	$(call display-cmd,$(NVIDIA_DISPLAY_SRC_DIR)) modules
	@echo   "================================================================================"
	@echo   "Display driver compiled successfully."
	@echo   "================================================================================"

nvidia-display-install:
	@echo   "================================================================================"
	@echo   "make $(MAKECMDGOALS) - nvidia-display ..."
	@echo   "================================================================================"
	$(MAKE) -C $(KERNEL_OUTPUT) INSTALL_MOD_DIR=$(NVIDIA_DISPLAY_INSTALL_DIR) \
		M=$(MAKEFILE_DIR)/$(NVIDIA_DISPLAY_SRC_DIR)/kernel-open modules_install

nvidia-display-clean:
	@echo   "================================================================================"
	@echo   "make $(MAKECMDGOALS) - nvidia-display ..."
	@echo   "================================================================================"
	$(call display-cmd,$(NVIDIA_DISPLAY_SRC_DIR)) clean

nvidia-gpu-display: nvidia-oot
	@if [ ! -d "$(MAKEFILE_DIR)/$(NVIDIA_GPU_DISPLAY_SRC_DIR)" ] ; then \
		echo "Directory $(NVIDIA_GPU_DISPLAY_SRC_DIR) is not found, exiting.."; \
		false; \
	fi
	@echo   "================================================================================"
	@echo   "make $(MAKECMDGOALS) - nvidia-gpu-display ..."
	@echo   "================================================================================"
	$(call display-cmd,$(NVIDIA_GPU_DISPLAY_SRC_DIR)) modules
	@echo   "================================================================================"
	@echo   "GPU display driver compiled successfully."
	@echo   "================================================================================"

nvidia-gpu-display-install:
	@echo   "================================================================================"
	@echo   "make $(MAKECMDGOALS) - nvidia-gpu-display ..."
	@echo   "================================================================================"
	$(MAKE) -C $(KERNEL_OUTPUT) INSTALL_MOD_DIR=$(NVIDIA_GPU_DISPLAY_INSTALL_DIR) \
		M=$(MAKEFILE_DIR)/$(NVIDIA_GPU_DISPLAY_SRC_DIR)/kernel-open modules_install

nvidia-gpu-display-clean:
	@echo   "================================================================================"
	@echo   "make $(MAKECMDGOALS) - nvidia-gpu-display ..."
	@echo   "================================================================================"
	$(call display-cmd,$(NVIDIA_GPU_DISPLAY_SRC_DIR)) clean

nvidia-dtbs:
	@if [ ! -d "$(NVIDIA_DTS_BUILD_SCRIPTS)" ] ; then \
		echo "Directory $(NVIDIA_DTS_BUILD_SCRIPTS) is not found, exiting.."; \
		false; \
	fi
	@echo   "================================================================================"
	@echo   "make nvidia-dtbs ..."
	@echo   "================================================================================"
	TEGRA_TOP=$(MAKEFILE_DIR) \
	srctree=$(KERNEL_HEADERS) \
	objtree=$(KERNEL_OUTPUT) \
	HOSTCC="$(HOSTCC)" \
	DT_FLAVOR=generic \
	$(MAKE) -f $(NVIDIA_DTS_BUILD_SCRIPTS)/Makefile.build \
		obj=$(NVIDIA_DTS_BUILD_SCRIPTS) \
		dtbs
	@echo   "================================================================================"
	@echo   "DTBs compiled successfully."
	@echo   "================================================================================"

nvidia-dtbs-clean:
	@echo   "================================================================================"
	@echo   "make $(MAKECMDGOALS) - nvidia-dtbs ..."
	@echo   "================================================================================"
	rm -fr $(NVIDIA_DTS_BUILD_SCRIPTS)/generic-dtbs

conftest-clean:
	@echo   "================================================================================"
	@echo   "make $(MAKECMDGOALS) - conftest ..."
	@echo   "================================================================================"
	rm -fr $(NVIDIA_CONFTEST)
