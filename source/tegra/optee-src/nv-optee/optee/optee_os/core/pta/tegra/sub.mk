global-incdirs-y += include
srcs-$(CFG_JETSON_FTPM_HELPER_PTA) += jetson_ftpm_helper_pta.c
srcs-$(CFG_JETSON_USER_KEY_PTA) += jetson_user_key_pta.c
srcs-$(PLATFORM_FLAVOR_t234) += jetson_t234_decrypt_cpubl_payload.c
srcs-$(PLATFORM_FLAVOR_t194) += jetson_t194_decrypt_cpubl_payload.c


# Add source files and include header files from $(NV_OPTEE_DIR)
ifneq ("$(wildcard $(NV_OPTEE_DIR))","")
subdirs_ext-y += $(NV_OPTEE_DIR)/core/pta/tegra
global-incdirs_ext-y += $(NV_OPTEE_DIR)/lib/libutee/include
endif
