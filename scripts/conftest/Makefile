###########################################################################
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: Copyright (c) 2023-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

NV_CONFTEST_CFLAGS = $(NV_CFLAGS_FROM_CONFTEST) $(EXTRA_CFLAGS) -fno-pie -Werror -Wno-address-of-packed-member

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
NV_CONFTEST_FUNCTION_COMPILE_TESTS += __alloc_pages_bulk_has_no_page_list_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += __assign_str_has_no_src_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += aperture_remove_all_conflicting_devices
NV_CONFTEST_FUNCTION_COMPILE_TESTS += bin_attribute_struct_mmap_has_const_bin_attribute_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += blk_execute_rq_has_no_gendisk_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += blk_mq_alloc_disk_for_queue
NV_CONFTEST_FUNCTION_COMPILE_TESTS += blk_mq_alloc_queue
NV_CONFTEST_FUNCTION_COMPILE_TESTS += blk_mq_destroy_queue
NV_CONFTEST_FUNCTION_COMPILE_TESTS += blk_queue_max_hw_sectors
NV_CONFTEST_FUNCTION_COMPILE_TESTS += blk_rq_map_sg_has_no_queue_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += block_device_operations_open_has_gendisk_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += block_device_operations_release_has_no_mode_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += queue_limits_struct_has_features
NV_CONFTEST_FUNCTION_COMPILE_TESTS += blk_mq_f_should_merge
NV_CONFTEST_FUNCTION_COMPILE_TESTS += bus_type_struct_match_has_const_drv_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += bus_type_struct_remove_has_int_return_type
NV_CONFTEST_FUNCTION_COMPILE_TESTS += bus_type_struct_uevent_has_const_dev_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += class_attribute_struct_has_const_struct_class_attribute
NV_CONFTEST_FUNCTION_COMPILE_TESTS += class_create_has_no_owner_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += class_struct_devnode_has_const_dev_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += clk_hw_determine_rate_no_reparent
NV_CONFTEST_FUNCTION_COMPILE_TESTS += define_semaphore_has_number_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += devfreq_dev_profile_has_is_cooling_device
NV_CONFTEST_FUNCTION_COMPILE_TESTS += devfreq_has_freq_table
NV_CONFTEST_FUNCTION_COMPILE_TESTS += devfreq_has_suspend_freq
NV_CONFTEST_FUNCTION_COMPILE_TESTS += device_add_disk_has_int_return_type
NV_CONFTEST_FUNCTION_COMPILE_TESTS += devm_pm_domain_attach_list
NV_CONFTEST_FUNCTION_COMPILE_TESTS += devm_of_icc_get
NV_CONFTEST_FUNCTION_COMPILE_TESTS += devm_spi_alloc_host
NV_CONFTEST_FUNCTION_COMPILE_TESTS += devm_tegra_core_dev_init_opp_table_common
NV_CONFTEST_FUNCTION_COMPILE_TESTS += devm_thermal_of_zone_register
NV_CONFTEST_FUNCTION_COMPILE_TESTS += disk_check_media_change
NV_CONFTEST_FUNCTION_COMPILE_TESTS += dma_slave_config_struct_has_slave_id
NV_CONFTEST_FUNCTION_COMPILE_TESTS += drm_aperture_remove_framebuffers
NV_CONFTEST_FUNCTION_COMPILE_TESTS += drm_connector_helper_funcs_struct_mode_valid_has_const_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += drm_connector_has_override_edid
NV_CONFTEST_FUNCTION_COMPILE_TESTS += drm_debugfs_remove_files_has_root_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += drm_display_info_struct_has_source_physical_address
NV_CONFTEST_FUNCTION_COMPILE_TESTS += drm_driver_has_fbdev_probe
NV_CONFTEST_FUNCTION_COMPILE_TESTS += drm_driver_struct_has_date
NV_CONFTEST_FUNCTION_COMPILE_TESTS += drm_driver_struct_has_irq_enabled_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += drm_edid_connector_add_modes
NV_CONFTEST_FUNCTION_COMPILE_TESTS += drm_fb_helper_alloc_info
NV_CONFTEST_FUNCTION_COMPILE_TESTS += drm_fb_helper_prepare_has_preferred_bpp_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += drm_fb_helper_unregister_info
NV_CONFTEST_FUNCTION_COMPILE_TESTS += drm_fb_helper_struct_has_info_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += drm_mode_config_struct_has_fb_base_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += drm_plane_helper_funcs_struct_atomic_async_check_has_bool_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += drm_scdc_get_set_has_struct_drm_connector_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += ethtool_keee_struct_present
NV_CONFTEST_FUNCTION_COMPILE_TESTS += ethtool_kernel_ethtool_ts_info_struct_present
NV_CONFTEST_FUNCTION_COMPILE_TESTS += ethtool_ops_get_set_coalesce_has_coal_and_extack_args
NV_CONFTEST_FUNCTION_COMPILE_TESTS += ethtool_ops_get_set_ringparam_has_ringparam_and_extack_args
NV_CONFTEST_FUNCTION_COMPILE_TESTS += ethtool_ops_get_set_rxfh_has_rxfh_param_args
NV_CONFTEST_FUNCTION_COMPILE_TESTS += fd_empty
NV_CONFTEST_FUNCTION_COMPILE_TESTS += fd_file
NV_CONFTEST_FUNCTION_COMPILE_TESTS += file_struct_has_f_ref
NV_CONFTEST_FUNCTION_COMPILE_TESTS += folio_entire_mapcount
NV_CONFTEST_FUNCTION_COMPILE_TESTS += follow_pfn
NV_CONFTEST_FUNCTION_COMPILE_TESTS += follow_pfnmap_start
NV_CONFTEST_FUNCTION_COMPILE_TESTS += fop_unsigned_offset
NV_CONFTEST_FUNCTION_COMPILE_TESTS += genpd_xlate_t_has_const_of_phandle_args
NV_CONFTEST_FUNCTION_COMPILE_TESTS += get_file_rcu_has_double_ptr_file_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += get_user_pages
NV_CONFTEST_FUNCTION_COMPILE_TESTS += hrtimer_setup
NV_CONFTEST_FUNCTION_COMPILE_TESTS += icc_get
NV_CONFTEST_FUNCTION_COMPILE_TESTS += icc_put
NV_CONFTEST_FUNCTION_COMPILE_TESTS += icc_set_bw
NV_CONFTEST_FUNCTION_COMPILE_TESTS += i2c_new_client_device
NV_CONFTEST_FUNCTION_COMPILE_TESTS += i2c_driver_struct_probe_without_i2c_device_id_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += i2c_driver_struct_has_probe_new
NV_CONFTEST_FUNCTION_COMPILE_TESTS += i2c_driver_struct_remove_return_type_int
NV_CONFTEST_FUNCTION_COMPILE_TESTS += i2c_mux_add_adapter_has_no_class_argument
NV_CONFTEST_FUNCTION_COMPILE_TESTS += iio_dev_opaque_has_mlock
NV_CONFTEST_FUNCTION_COMPILE_TESTS += iommu_map_has_gfp_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += iommu_paging_domain_alloc
NV_CONFTEST_FUNCTION_COMPILE_TESTS += ioremap_prot_has_pgprot_t_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += irq_get_nr_irqs
NV_CONFTEST_FUNCTION_COMPILE_TESTS += kthread_complete_and_exit
NV_CONFTEST_FUNCTION_COMPILE_TESTS += mii_bus_struct_has_read_c45
NV_CONFTEST_FUNCTION_COMPILE_TESTS += mii_bus_struct_has_write_c45
NV_CONFTEST_FUNCTION_COMPILE_TESTS += media_entity_remote_pad
NV_CONFTEST_FUNCTION_COMPILE_TESTS += module_import_ns_calls_stringify
NV_CONFTEST_FUNCTION_COMPILE_TESTS += mrq_pcie_request_struct_present
NV_CONFTEST_FUNCTION_COMPILE_TESTS += msi_get_virq
NV_CONFTEST_FUNCTION_COMPILE_TESTS += netif_set_tso_max_size
NV_CONFTEST_FUNCTION_COMPILE_TESTS += netif_napi_add_weight
NV_CONFTEST_FUNCTION_COMPILE_TESTS += no_llseek
NV_CONFTEST_FUNCTION_COMPILE_TESTS += of_get_named_gpio_flags
NV_CONFTEST_FUNCTION_COMPILE_TESTS += gpio_chip_struct_has_of_node_present
NV_CONFTEST_FUNCTION_COMPILE_TESTS += gpio_device_find
NV_CONFTEST_FUNCTION_COMPILE_TESTS += gpio_device_get_chip
NV_CONFTEST_FUNCTION_COMPILE_TESTS += of_property_for_each_u32_removed_internal_args
NV_CONFTEST_FUNCTION_COMPILE_TESTS += pde_data
NV_CONFTEST_FUNCTION_COMPILE_TESTS += platform_msi_domain_alloc_irqs
NV_CONFTEST_FUNCTION_COMPILE_TESTS += platform_msi_domain_free_irqs
NV_CONFTEST_FUNCTION_COMPILE_TESTS += pwm_chip_struct_has_base_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += pwm_chip_struct_has_struct_device
NV_CONFTEST_FUNCTION_COMPILE_TESTS += pwm_ops_struct_has_owner
NV_CONFTEST_FUNCTION_COMPILE_TESTS += pwm_ops_struct_has_config
NV_CONFTEST_FUNCTION_COMPILE_TESTS += pci_epc_deinit_notify
NV_CONFTEST_FUNCTION_COMPILE_TESTS += pci_epc_event_ops_struct_has_core_deinit
NV_CONFTEST_FUNCTION_COMPILE_TESTS += pci_epc_event_ops_struct_has_epc_deinit
NV_CONFTEST_FUNCTION_COMPILE_TESTS += pci_epc_event_ops_struct_has_epc_init
NV_CONFTEST_FUNCTION_COMPILE_TESTS += pci_epc_features_struct_has_bar
NV_CONFTEST_FUNCTION_COMPILE_TESTS += pci_epc_features_struct_has_core_init_notifier
NV_CONFTEST_FUNCTION_COMPILE_TESTS += pci_epc_write_header_has_vfn_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += pci_disable_pcie_error_reporting
NV_CONFTEST_FUNCTION_COMPILE_TESTS += pci_enable_pcie_error_reporting
NV_CONFTEST_FUNCTION_COMPILE_TESTS += pci_epf_alloc_space_has_epc_features_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += pci_epf_driver_struct_probe_has_id_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += pci_epc_irq_type_enum_present
NV_CONFTEST_FUNCTION_COMPILE_TESTS += pci_irq_intx
NV_CONFTEST_FUNCTION_COMPILE_TESTS += phy_loopback_has_speed_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += platform_driver_struct_remove_returns_void
NV_CONFTEST_FUNCTION_COMPILE_TESTS += register_shrinker_has_fmt_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += shrinker_alloc
NV_CONFTEST_FUNCTION_COMPILE_TESTS += slab_mem_spread
NV_CONFTEST_FUNCTION_COMPILE_TESTS += snd_soc_card_jack_new_has_no_snd_soc_jack_pins
NV_CONFTEST_FUNCTION_COMPILE_TESTS += snd_soc_component_driver_struct_has_non_legacy_dai_naming
NV_CONFTEST_FUNCTION_COMPILE_TESTS += snd_soc_dai_link_struct_has_c2c_params_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += snd_soc_dai_ops_struct_has_probe
NV_CONFTEST_FUNCTION_COMPILE_TESTS += snd_soc_dai_struct_has_symmetric_prefix
NV_CONFTEST_FUNCTION_COMPILE_TESTS += snd_soc_of_get_dai_name_has_index_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += snd_soc_rtd_to_codec
NV_CONFTEST_FUNCTION_COMPILE_TESTS += simple_util_dai_init
NV_CONFTEST_FUNCTION_COMPILE_TESTS += soc_single_value_has_xmin_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += spi_driver_struct_remove_return_type_int
NV_CONFTEST_FUNCTION_COMPILE_TESTS += spi_get_chipselect
NV_CONFTEST_FUNCTION_COMPILE_TESTS += tc_taprio_qopt_offload_struct_has_cmd
NV_CONFTEST_FUNCTION_COMPILE_TESTS += tegra264_chip_id
NV_CONFTEST_FUNCTION_COMPILE_TESTS += tegra264_io_pad_power_enable
NV_CONFTEST_FUNCTION_COMPILE_TESTS += tegra_dev_iommu_get_stream_id
NV_CONFTEST_FUNCTION_COMPILE_TESTS += tegra_ivc_struct_has_iosys_map
NV_CONFTEST_FUNCTION_COMPILE_TESTS += tegra_ivc_use_ivc_ext_driver
NV_CONFTEST_FUNCTION_COMPILE_TESTS += thermal_zone_device_priv
NV_CONFTEST_FUNCTION_COMPILE_TESTS += thermal_zone_device_ops_struct_has_get_trip_type
NV_CONFTEST_FUNCTION_COMPILE_TESTS += thermal_zone_for_each_trip
NV_CONFTEST_FUNCTION_COMPILE_TESTS += timer_delete
NV_CONFTEST_FUNCTION_COMPILE_TESTS += tty_operations_struct_send_xchar_has_u8_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += tty_operations_struct_write_has_u8_ptr_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += tty_operations_struct_set_termios_has_const_ktermios_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += uart_state_has_xmit
NV_CONFTEST_FUNCTION_COMPILE_TESTS += ufs_hba_variant_ops_has_set_dma_mask
NV_CONFTEST_FUNCTION_COMPILE_TESTS += ufs_hba_variant_ops_pwr_change_notify_has_const_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += ufs_hba_variant_ops_suspend_has_status_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += ufshcd_quirks_enum_has_ufshcd_quirk_broken_64bit_address
NV_CONFTEST_FUNCTION_COMPILE_TESTS += ufshcd_quirks_enum_has_ufshcd_quirk_broken_power_seqeunce
NV_CONFTEST_FUNCTION_COMPILE_TESTS += ufshcd_ufs_hba_struct_has_bool_is_ufs_already_enabled
NV_CONFTEST_FUNCTION_COMPILE_TESTS += v4l2_async_connection_struct_present
NV_CONFTEST_FUNCTION_COMPILE_TESTS += v4l2_async_match_type_enum_present
NV_CONFTEST_FUNCTION_COMPILE_TESTS += v4l2_async_subdev_nf_init
NV_CONFTEST_FUNCTION_COMPILE_TESTS += v4l2_async_notifier_init
NV_CONFTEST_FUNCTION_COMPILE_TESTS += v4l2_async_nf_init_has_v4l2_dev_arg
NV_CONFTEST_FUNCTION_COMPILE_TESTS += __v4l2_async_nf_add_subdev
NV_CONFTEST_FUNCTION_COMPILE_TESTS += v4l2_fwnode_endpoint_struct_has_v4l2_mbus_config_mipi_csi2
NV_CONFTEST_FUNCTION_COMPILE_TESTS += v4l2_subdev_pad_ops_struct_has_get_set_frame_interval
NV_CONFTEST_FUNCTION_COMPILE_TESTS += v4l2_subdev_pad_ops_struct_has_dv_timings
NV_CONFTEST_FUNCTION_COMPILE_TESTS += vm_area_struct_has_const_vm_flags
NV_CONFTEST_FUNCTION_COMPILE_TESTS += of_property_read_reg
NV_CONFTEST_GENERIC_COMPILE_TESTS += is_export_symbol_present_drm_gem_prime_fd_to_handle
NV_CONFTEST_GENERIC_COMPILE_TESTS += is_export_symbol_present_drm_gem_prime_handle_to_fd
NV_CONFTEST_GENERIC_COMPILE_TESTS += is_export_symbol_present_queue_limits_set
NV_CONFTEST_FUNCTION_COMPILE_TESTS += crypto_engine_ctx_struct_removed_test
NV_CONFTEST_MACRO_COMPILE_TESTS ?=
NV_CONFTEST_SYMBOL_COMPILE_TESTS ?=

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
  drm/clients/drm_client_setup.h \
  drm/display/drm_dp_aux_bus.h \
  drm/display/drm_dp_helper.h \
  drm/display/drm_scdc_helper.h \
  drm/dp/drm_dp_aux_bus.h \
  drm/dp/drm_dp_helper.h \
  drm/drm_client_setup.h \
  drm/drm_connector.h \
  drm/drm_crtc.h \
  drm/drm_dp_aux_bus.h \
  drm/drm_eld.h \
  generated/autoconf.h \
  linux/aperture.h \
  linux/kconfig.h \
  linux/iosys-map.h \
  linux/tegra_prod.h \
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
