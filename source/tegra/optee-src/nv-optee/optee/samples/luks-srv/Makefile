# Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

# Input variables
# CROSS_COMPILE: The cross compiler.
# TA_DEV_KIT_DIR: The base directory of the TA-devkit.
# OPTEE_CLIENT_EXPORT: The base directory points to optee client's
#		       header files and libraries
# O: The base directory for build objects filetree.

TARGET_DIR := $(notdir $(shell pwd))

.PHONY: all
all: early_ta_dir
	$(MAKE) -C ta \
		CROSS_COMPILE=$(CROSS_COMPILE) \
		TA_DEV_KIT_DIR=$(TA_DEV_KIT_DIR) \
		O=$(O)/early_ta/$(TARGET_DIR)
	$(MAKE) -C host \
		CROSS_COMPILE=$(CROSS_COMPILE) \
		OPTEE_CLIENT_EXPORT=$(OPTEE_CLIENT_EXPORT) \
		O=$(O)/ca/$(TARGET_DIR) \
		--no-builtin-variables

early_ta_dir:
	mkdir -p $(O)/early_ta

.PHONY: clean
clean:
	$(MAKE) -C ta \
		TA_DEV_KIT_DIR=$(TA_DEV_KIT_DIR) \
		O=$(O)/early_ta/$(TARGET_DIR) \
		clean
	$(MAKE) -C host \
		OPTEE_CLIENT_EXPORT=$(OPTEE_CLIENT_EXPORT) \
		O=$(O)/ca/$(TARGET_DIR) \
		clean
	rm -rf $(O)/early_ta/$(TARGET_DIR)
	rm -rf $(O)/ca/$(TARGET_DIR)
