
export V?=0

# Input variables
# CROSS_COMPILE: The cross compiler.
# TA_DEV_KIT_DIR: The base directory of the TA-devkit
# OPTEE_OS_DIR: The base directory of the optee_os
# O: The base directory for build objects filetree.

.PHONY: all
all:
	$(MAKE) -C fTPM \
		CROSS_COMPILE=$(CROSS_COMPILE) \
		TA_DEV_KIT_DIR=$(TA_DEV_KIT_DIR) \
		OPTEE_OS_DIR=$(OPTEE_OS_DIR) \
		O=$(O)/early_ta/ms-tpm || exit 1

.PHONY: clean
clean:
	$(MAKE) -C fTPM \
		CROSS_COMPILE=$(CROSS_COMPILE) \
		TA_DEV_KIT_DIR=$(TA_DEV_KIT_DIR) \
		OPTEE_OS_DIR=$(OPTEE_OS_DIR) \
		O=$(O)/early_ta/ms-tpm \
		clean || exit 1
