###########################################################################
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#
# Makefile file for NVIDIA Linux conftest
###########################################################################

#
# The parent makefile is expected to define:
#
#  NV_KERNEL_SOURCES : The root of the kernel source tree.
#  NV_KERNEL_OUTPUT  : The kernel's output tree.
#  NV_KERNEL_MODULES : A whitespace-separated list of modules to build.
#  ARCH              : The target CPU architecture: x86_64|arm64|powerpc
#
# Kbuild provides the variables:
#
# $(src) : The directory containing this Kbuild file.
# $(obj) : The directory where the output from this build is written.
#

#
# The conftest.sh script tests various aspects of the target kernel.
# The per-module Kbuild files included above should:
#
# - Append to the NV_CONFTEST_*_COMPILE_TESTS variables to indicate
#   which conftests they require.
# - Append to the NV_OBJECTS_DEPEND_ON_CONFTEST variable any object files
#   that depend on conftest.
#
# The conftest machinery below will run the requested tests and
# generate the appropriate header files.
#

CC ?= cc
LD ?= ld

NV_CONFTEST_SCRIPT := $(src)/conftest.sh
NV_CONFTEST_HEADER := $(obj)/conftest/headers.h

NV_CONFTEST_CMD := /bin/sh $(NV_CONFTEST_SCRIPT) \
  "$(CC)" $(ARCH) $(NV_KERNEL_SOURCES) $(NV_KERNEL_OUTPUT)

NV_CFLAGS_FROM_CONFTEST := $(shell $(NV_CONFTEST_CMD) build_cflags)

NV_CONFTEST_CFLAGS = $(NV_CFLAGS_FROM_CONFTEST) $(EXTRA_CFLAGS) -fno-pie -Werror

NV_CONFTEST_COMPILE_TEST_HEADERS := $(obj)/conftest/macros.h
NV_CONFTEST_COMPILE_TEST_HEADERS += $(obj)/conftest/functions.h
NV_CONFTEST_COMPILE_TEST_HEADERS += $(obj)/conftest/symbols.h
NV_CONFTEST_COMPILE_TEST_HEADERS += $(obj)/conftest/types.h
NV_CONFTEST_COMPILE_TEST_HEADERS += $(obj)/conftest/generic.h

NV_CONFTEST_HEADERS := $(obj)/conftest/patches.h
NV_CONFTEST_HEADERS += $(obj)/conftest/headers.h
NV_CONFTEST_HEADERS += $(NV_CONFTEST_COMPILE_TEST_HEADERS)

all: $(NV_CONFTEST_HEADERS)

#
# Generate a header file for a single conftest compile test. Each compile test
# header depends on conftest.sh, as well as the generated conftest/headers.h
# file, which is included in the compile test preamble.
#

$(obj)/conftest/compile-tests/%.h: $(NV_CONFTEST_SCRIPT) $(NV_CONFTEST_HEADER)
	@mkdir -p $(obj)/conftest/compile-tests
	@echo "  CONFTEST: $(notdir $*)"
	@$(NV_CONFTEST_CMD) compile_tests '$(NV_CONFTEST_CFLAGS)' \
	  $(notdir $*) > $@

#
# Concatenate a conftest/*.h header from its constituent compile test headers
#
# $(1): The name of the concatenated header
# $(2): The list of compile tests that make up the header
#

define NV_GENERATE_COMPILE_TEST_HEADER
  $(obj)/conftest/$(1).h: $(addprefix $(obj)/conftest/compile-tests/,$(addsuffix .h,$(2)))
	@mkdir -p $(obj)/conftest
	@# concatenate /dev/null to prevent cat from hanging when $$^ is empty
	@cat $$^ /dev/null > $$@
endef

#
# Generate the conftest compile test headers from the lists of compile tests
# provided by the module-specific Kbuild files.
#

NV_CONFTEST_FUNCTION_COMPILE_TESTS += __alloc_disk_node_has_lkclass_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += blk_execute_rq_has_no_gendisk_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += blk_mq_alloc_disk_for_queue
NV_CONFTEST_FUNCTION_COMPILE_TESTS += blk_mq_destroy_queue
NV_CONFTEST_FUNCTION_COMPILE_TESTS += block_device_operations_open_has_gendisk_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += block_device_operations_release_has_no_mode_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += bus_type_struct_remove_has_int_return_type
NV_CONFTEST_FUNCTION_COMPILE_TESTS += bus_type_struct_uevent_has_const_dev_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += class_create_has_no_owner_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += class_struct_devnode_has_const_dev_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += define_semaphore_has_number_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += device_add_disk_has_int_return_type
NV_CONFTEST_FUNCTION_COMPILE_TESTS += devm_thermal_of_zone_register
NV_CONFTEST_FUNCTION_COMPILE_TESTS += disk_check_media_change
NV_CONFTEST_FUNCTION_COMPILE_TESTS += dma_slave_config_struct_has_slave_id
NV_CONFTEST_FUNCTION_COMPILE_TESTS += drm_aperture_remove_framebuffers
NV_CONFTEST_FUNCTION_COMPILE_TESTS += drm_driver_struct_has_irq_enabled_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += drm_fb_helper_alloc_info
NV_CONFTEST_FUNCTION_COMPILE_TESTS += drm_fb_helper_prepare_has_preferred_bpp_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += drm_fb_helper_unregister_info
NV_CONFTEST_FUNCTION_COMPILE_TESTS += drm_fb_helper_struct_has_info_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += drm_mode_config_struct_has_fb_base_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += drm_scdc_get_set_has_struct_drm_connector_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += ethtool_ops_get_set_coalesce_has_coal_and_extack_args
NV_CONFTEST_FUNCTION_COMPILE_TESTS += ethtool_ops_get_set_ringparam_has_ringparam_and_extack_args
NV_CONFTEST_FUNCTION_COMPILE_TESTS += get_user_pages
NV_CONFTEST_FUNCTION_COMPILE_TESTS += iio_dev_opaque_has_mlock
NV_CONFTEST_FUNCTION_COMPILE_TESTS += iommu_map_has_gfp_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += kthread_complete_and_exit
NV_CONFTEST_FUNCTION_COMPILE_TESTS += mii_bus_struct_has_read_c45
NV_CONFTEST_FUNCTION_COMPILE_TESTS += mii_bus_struct_has_write_c45
NV_CONFTEST_FUNCTION_COMPILE_TESTS += netif_set_tso_max_size
NV_CONFTEST_FUNCTION_COMPILE_TESTS += netif_napi_add_weight
NV_CONFTEST_FUNCTION_COMPILE_TESTS += pde_data
NV_CONFTEST_FUNCTION_COMPILE_TESTS += register_shrinker_has_fmt_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += snd_soc_card_jack_new_has_no_snd_soc_jack_pins
NV_CONFTEST_FUNCTION_COMPILE_TESTS += snd_soc_component_driver_struct_has_non_legacy_dai_naming
NV_CONFTEST_FUNCTION_COMPILE_TESTS += snd_soc_dai_link_struct_has_c2c_params_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += snd_soc_of_get_dai_name_has_index_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += tc_taprio_qopt_offload_struct_has_cmd
NV_CONFTEST_FUNCTION_COMPILE_TESTS += tegra_dev_iommu_get_stream_id
NV_CONFTEST_FUNCTION_COMPILE_TESTS += tegra_ivc_struct_has_iosys_map
NV_CONFTEST_GENERIC_COMPILE_TESTS += is_export_symbol_present_drm_gem_prime_fd_to_handle
NV_CONFTEST_GENERIC_COMPILE_TESTS += is_export_symbol_present_drm_gem_prime_handle_to_fd
NV_CONFTEST_MACRO_COMPILE_TESTS ?=
NV_CONFTEST_SYMBOL_COMPILE_TESTS ?=
NV_CONFTEST_TYPE_COMPILE_TESTS += request_struct_has_completion_data_arg

$(eval $(call NV_GENERATE_COMPILE_TEST_HEADER,functions,$(NV_CONFTEST_FUNCTION_COMPILE_TESTS)))
$(eval $(call NV_GENERATE_COMPILE_TEST_HEADER,generic,$(NV_CONFTEST_GENERIC_COMPILE_TESTS)))
$(eval $(call NV_GENERATE_COMPILE_TEST_HEADER,macros,$(NV_CONFTEST_MACRO_COMPILE_TESTS)))
$(eval $(call NV_GENERATE_COMPILE_TEST_HEADER,symbols,$(NV_CONFTEST_SYMBOL_COMPILE_TESTS)))
$(eval $(call NV_GENERATE_COMPILE_TEST_HEADER,types,$(NV_CONFTEST_TYPE_COMPILE_TESTS)))

$(obj)/conftest/patches.h: $(NV_CONFTEST_SCRIPT)
	@mkdir -p $(obj)/conftest
	@$(NV_CONFTEST_CMD) patch_check > $@


# Each of these headers is checked for presence with a test #include; a
# corresponding #define will be generated in conftest/headers.h.
NV_HEADER_PRESENCE_TESTS = \
  drm/display/drm_dp_aux_bus.h \
  drm/display/drm_dp_helper.h \
  drm/display/drm_scdc_helper.h \
  drm/dp/drm_dp_aux_bus.h \
  drm/dp/drm_dp_helper.h \
  drm/drm_dp_aux_bus.h \
  generated/autoconf.h \
  linux/kconfig.h \
  linux/iosys-map.h \
  net/gso.h \
  net/page_pool.h

# Filename to store the define for the header in $(1); this is only consumed by
# the rule below that concatenates all of these together.
NV_HEADER_PRESENCE_PART = $(addprefix $(obj)/conftest/header_presence/,$(addsuffix .part,$(1)))

# Define a rule to check the header $(1).
define NV_HEADER_PRESENCE_CHECK
  $$(call NV_HEADER_PRESENCE_PART,$(1)): $$(NV_CONFTEST_SCRIPT) $(obj)/conftest/uts_release
	@mkdir -p $$(dir $$@)
	@$$(NV_CONFTEST_CMD) test_kernel_header '$$(NV_CONFTEST_CFLAGS)' '$(1)' > $$@
endef

# Evaluate the rule above for each header in the list.
$(foreach header,$(NV_HEADER_PRESENCE_TESTS),$(eval $(call NV_HEADER_PRESENCE_CHECK,$(header))))

# Concatenate all of the parts into headers.h.
$(obj)/conftest/headers.h: $(call NV_HEADER_PRESENCE_PART,$(NV_HEADER_PRESENCE_TESTS))
	@cat $^ > $@

clean-dirs := $(obj)/conftest

# For any object files that depend on conftest, declare the dependency here.
$(addprefix $(obj)/,$(NV_OBJECTS_DEPEND_ON_CONFTEST)): | $(NV_CONFTEST_HEADERS)

# Sanity checks of the build environment and target system/kernel

BUILD_SANITY_CHECKS = \
  cc_sanity_check \
  cc_version_check \
  module_symvers_sanity_check

.PHONY: $(BUILD_SANITY_CHECKS)

$(BUILD_SANITY_CHECKS):
	@$(NV_CONFTEST_CMD) $@ full_output

# Perform all sanity checks before generating the conftest headers

$(NV_CONFTEST_HEADERS): | $(BUILD_SANITY_CHECKS)

# Make the conftest headers depend on the kernel version string

$(obj)/conftest/uts_release: NV_GENERATE_UTS_RELEASE
	@mkdir -p $(dir $@)
	@NV_UTS_RELEASE="// Kernel version: `$(NV_CONFTEST_CMD) compile_tests '$(NV_CONFTEST_CFLAGS)' uts_release`"; \
	if ! [ -f "$@" ] || [ "$$NV_UTS_RELEASE" != "`cat $@`" ]; \
	then echo "$$NV_UTS_RELEASE" > $@; fi

.PHONY: NV_GENERATE_UTS_RELEASE

$(NV_CONFTEST_HEADERS): $(obj)/conftest/uts_release
