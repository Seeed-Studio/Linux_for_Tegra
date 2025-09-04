# Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

# Input variables
# CROSS_COMPILE: The cross compiler.
# TA_DEV_KIT_DIR: The base directory of the TA-devkit.
# OPTEE_CLIENT_EXPORT: The base directory points to optee client's
#		       header files and libraries
# O: The base directory for build objects filetree.

SAMPLE_APPS_LIST := $(subst /,,$(dir $(wildcard */Makefile)))

.PHONY: all
all: sample_apps

.PHONY: clean
clean: clean-apps

sample_apps:
	@for apps in $(SAMPLE_APPS_LIST); do \
		$(MAKE) -C $$apps \
			CROSS_COMPILE=$(CROSS_COMPILE) \
			TA_DEV_KIT_DIR=$(TA_DEV_KIT_DIR) \
			OPTEE_CLIENT_EXPORT=$(OPTEE_CLIENT_EXPORT) \
			O=$(O) || exit 1; \
	done

clean-apps:
	@for apps in $(SAMPLE_APPS_LIST); do \
		$(MAKE) -C $$apps \
			TA_DEV_KIT_DIR=$(TA_DEV_KIT_DIR) \
			OPTEE_CLIENT_EXPORT=$(OPTEE_CLIENT_EXPORT) \
			O=$(O) \
			clean || exit 1; \
	done
