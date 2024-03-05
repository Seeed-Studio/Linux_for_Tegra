# SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: GPL-2.0-only

objtree = $(srctree)

# Redefine the fixdep command
cmd_and_fixdep =                                                             \
	$(cmd);                                                              \
	$(objtree)/scripts/basic/fixdep $(depfile) $@ '$(make-cmd)' > $(dot-target).cmd;\
	rm -f $(depfile)

include $(oottree)/scripts/Makefile.lib

# Stop warning of the graph port and graph endpoint
DTC_FLAGS += -Wno-graph_port
DTC_FLAGS += -Wno-graph_endpoint

DTC_CPP_FLAGS += $(EXTRA_CPP_FLAGS)

DTC_CPP_FLAGS += -DLINUX_VERSION=600 -DTEGRA_HOST1X_DT_VERSION=2 -DOS_LINUX

ifneq ($(findstring ack_src,$(NV_BUILD_KERNEL_OPTIONS)),)
ifneq ($(TOP),)
SOURCE_TOP := $(TOP)
else
$(error TOP is not defined)
endif
else
ifneq ($(TEGRA_TOP),)
SOURCE_TOP := $(TEGRA_TOP)
else ifneq ($(TOP),)
SOURCE_TOP := $(TOP)
else
$(error TEGRA_TOP or TOP is not defined)
endif
endif

tegra-dtstree := $(SOURCE_TOP)/hardware/nvidia
# Get relative path for tegra-dtstree from this makefile
null :=
space :=$(null) $(null)
obj-path := $(obj)
obj-path-from-top := $(subst $(SOURCE_TOP),,$(obj-path))
rel-top-path := $(subst /, $(space)/,$(obj-path-from-top))
rel-top-path := $(patsubst /%,../,$(rel-top-path))
rel-top-path := $(subst $(space),,$(rel-top-path))
tegra-rel-dtstree := $(rel-top-path)/hardware/nvidia

DTC_INCLUDE :=
# SOC independent common include
DTC_INCLUDE += $(tegra-dtstree)/soc/tegra/kernel-include
DTC_INCLUDE += $(tegra-dtstree)/platform/tegra/common/kernel-dts

# SOC T23X specific common include
DTC_INCLUDE += $(tegra-dtstree)/soc/t23x/kernel-include
DTC_INCLUDE += $(tegra-dtstree)/soc/t23x/kernel-dts
DTC_INCLUDE += $(tegra-dtstree)/platform/t23x/common/kernel-dts
DTC_INCLUDE += $(tegra-dtstree)/platform/t23x/automotive/kernel-dts/common/linux/

dtb-oot-y :=
dtbo-oot-y :=

DTB_LIST := $(dtb-oot-y)
DTBO_LIST := $(dtbo-oot-y)
dtb-oot-y :=
fdts := $(shell find $(tegra-dtstree)/platform -name kernel-dts)

# Remove the DTs from protected soc list
ifneq ($(protected_soc_list),)
kdts := $(foreach dt_path, $(fdts), $(if $(filter $(protected_soc_list),$(patsubst -,$(space),$(subst /, $(space),$(dt_path)))),,$(dt_path)))
kdts := $(filter-out $(space)$(space),$(kdts))
else
kdts := $(fdts)
endif

dts_makefile=$(foreach d,$(wildcard $1*), $(call dts_makefile,$(d)/,$(2)) $(if $(findstring Makefile,$(d)),$(d)))
dts_mfiles = $(call dts_makefile, $(kdts), Makefile)

ifneq ($(dts_mfiles),)
dts-include :=
include $(dts_mfiles)
dtb-oot-y := $(addprefix $(tegra-rel-dtstree)/,$(dtb-oot-y))
dtbo-oot-y := $(addprefix $(tegra-rel-dtstree)/,$(dtbo-oot-y))
ifneq ($(dts-include),)
DTC_INCLUDE += $(addprefix $(tegra-dtstree)/,$(dts-include))
endif
endif

DTB_LIST += $(dtb-oot-y)
DTBO_LIST += $(dtbo-oot-y)
DTB_NEW_RULE_LIST := $(dtb-oot-y)
DTBO_NEW_RULE_LIST := $(dtbo-oot-y)

# Now save DTB_LIST to dtb-oot-y
dtb-oot-y := $(DTB_LIST)
dtbo-oot-y := $(DTBO_LIST)

# Add path of main Makefile to each dtb/dtbo list
DTB_OBJS := $(addprefix $(obj)/,$(DTB_LIST))
DTBO_OBJS := $(addprefix $(obj)/,$(DTBO_LIST))

###
# remove the ../
replace_ddot = $(subst ../,,$(1))

define _define_dtb_rule
$(obj)/$(call replace_ddot,$(1)): $(src)/$(patsubst %.dtb,%.dts,$(1)) FORCE
endef

$(foreach _dtb, $(DTB_NEW_RULE_LIST), $(eval $(call _define_dtb_rule,$(_dtb))))

DTB_OBJS := $(call replace_ddot,$(DTB_OBJS))
DTB_NEW_RULE_LIST := $(addprefix $(obj)/,$(DTB_NEW_RULE_LIST))
DTB_NEW_RULE_LIST := $(call replace_ddot,$(DTB_NEW_RULE_LIST))

$(DTB_NEW_RULE_LIST):
	$(call if_changed_dep,dtc)

define _define_dtbo_rule
$(obj)/$(call replace_ddot,$(1)): $(src)/$(patsubst %.dtbo,%.dts,$(1)) FORCE
endef

$(foreach _dtbo, $(DTBO_NEW_RULE_LIST), $(eval $(call _define_dtbo_rule,$(_dtbo))))

DTBO_OBJS := $(call replace_ddot,$(DTBO_OBJS))
DTBO_NEW_RULE_LIST := $(addprefix $(obj)/,$(DTBO_NEW_RULE_LIST))
DTBO_NEW_RULE_LIST := $(call replace_ddot,$(DTBO_NEW_RULE_LIST))

$(DTBO_NEW_RULE_LIST):
	$(call if_changed_dep,dtc)

DTBS_DTBOS := $(DTB_OBJS)$(DTBO_OBJS)

dtbs: $(DTB_OBJS) $(DTBO_OBJS) FORCE
	if [ ! -d $(obj)/dtbs/ ] ; then \
		mkdir -p $(obj)/dtbs/ ; \
	fi
	if [ ! -z "$(DTBS_DTBOS)" ] ; then \
		cp -u $(DTB_OBJS) $(DTBO_OBJS) $(obj)/dtbs/ ; \
	fi
	if [ -d $(obj)/hardware/ ] ; then \
		rm -rf $(obj)/hardware/ ; \
	fi

clean-files := *.dtb *.dtbo *.tmp
