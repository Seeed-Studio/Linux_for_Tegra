subdirs-y += common
subdirs-$(PLATFORM_FLAVOR_t194) += t194
subdirs-$(PLATFORM_FLAVOR_t234) += t234

# Add source files and include header files from $(NV_OPTEE_DIR)
ifneq ("$(wildcard $(NV_OPTEE_DIR))","")
subdirs_ext-y += $(NV_OPTEE_DIR)/core/drivers/tegra
global-incdirs_ext-y += $(NV_OPTEE_DIR)/core/include
endif
