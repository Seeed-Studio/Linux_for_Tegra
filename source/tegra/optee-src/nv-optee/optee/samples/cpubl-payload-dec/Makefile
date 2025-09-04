# Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

# Input variables
# CROSS_COMPILE: The cross compiler.
# TA_DEV_KIT_DIR: The base directory of the TA-devkit.
# OPTEE_CLIENT_EXPORT: The base directory points to optee client's
#		       header files and libraries
# O: The base directory for build objects filetree.

TARGET_DIR := $(notdir $(shell pwd))

.PHONY: all
all:
	$(MAKE) -C ta \
		CROSS_COMPILE=$(CROSS_COMPILE) \
		TA_DEV_KIT_DIR=$(TA_DEV_KIT_DIR) \
		O=$(O)/early_ta/$(TARGET_DIR)

.PHONY: clean
clean:
	$(MAKE) -C ta \
		TA_DEV_KIT_DIR=$(TA_DEV_KIT_DIR) \
		O=$(O)/early_ta/$(TARGET_DIR) \
		clean
