#
# nvidia-xconfig: A tool for manipulating X config files,
# specifically for use by the NVIDIA Linux graphics driver.
#
# Copyright (C) 2008 NVIDIA Corporation
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses>.
#
#
# Makefile
#


##############################################################################
# include common variables and functions
##############################################################################

COMMON_UTILS_PCIACCESS = 1
include utils.mk


##############################################################################
# The calling Makefile may export any of the following variables; we
# assign default values if they are not exported by the caller
##############################################################################

NVIDIA_CFG_DIR        ?= .


##############################################################################
# assign variables
##############################################################################

NVIDIA_XCONFIG = $(OUTPUTDIR)/nvidia-xconfig

NVIDIA_XCONFIG_PROGRAM_NAME = "nvidia-xconfig"

NVIDIA_XCONFIG_VERSION := $(NVIDIA_VERSION)

MANPAGE_GZIP ?= 1

MANPAGE_not_gzipped  = $(OUTPUTDIR)/nvidia-xconfig.1
MANPAGE_gzipped      = $(OUTPUTDIR)/nvidia-xconfig.1.gz
ifeq ($(MANPAGE_GZIP),1)
  MANPAGE            = $(MANPAGE_gzipped)
else
  MANPAGE            = $(MANPAGE_not_gzipped)
endif
GEN_MANPAGE_OPTS   = $(OUTPUTDIR_ABSOLUTE)/gen-manpage-opts
OPTIONS_1_INC      = $(OUTPUTDIR)/options.1.inc


##############################################################################
# The common-utils directory may be in one of two places: either
# elsewhere in the driver source tree when building nvidia-xconfig as
# part of the NVIDIA driver build (in which case, COMMON_UTILS_DIR
# should be defined by the calling makefile), or directly in the
# source directory when building from the nvidia-xconfig source
# tarball (in which case, the below conditional assignment should be
# used)
##############################################################################

COMMON_UTILS_DIR          ?= common-utils

include dist-files.mk

include $(COMMON_UTILS_DIR)/src.mk
SRC += $(addprefix $(COMMON_UTILS_DIR)/,$(COMMON_UTILS_SRC))

OBJS = $(call BUILD_OBJECT_LIST,$(SRC))

common_cflags += -I XF86Config-parser
common_cflags += -I $(OUTPUTDIR)
common_cflags += -I $(NVIDIA_CFG_DIR)
common_cflags += -I $(COMMON_UTILS_DIR)
common_cflags += -DPROGRAM_NAME=\"nvidia-xconfig\"

CFLAGS += $(common_cflags)
HOST_CFLAGS += $(common_cflags)

LIBS += -lm -lpciaccess

ifneq ($(TARGET_OS),FreeBSD)
  LIBS += -ldl
endif

ifeq ($(TARGET_OS),SunOS)
  LIBS += -lscf -ldevinfo
endif


##############################################################################
# build rules
##############################################################################

.PNONY: all install NVIDIA_XCONFIG_install MANPAGE_install clean clobber

all: $(NVIDIA_XCONFIG) $(MANPAGE)

install: NVIDIA_XCONFIG_install MANPAGE_install

NVIDIA_XCONFIG_install: $(NVIDIA_XCONFIG)
	$(MKDIR) $(BINDIR)
	$(INSTALL) $(INSTALL_BIN_ARGS) $< $(BINDIR)/$(notdir $<)

MANPAGE_install: $(MANPAGE)
	$(MKDIR) $(MANDIR)
	$(INSTALL) $(INSTALL_DOC_ARGS) $< $(MANDIR)/$(notdir $<)

$(eval $(call DEBUG_INFO_RULES, $(NVIDIA_XCONFIG)))
$(NVIDIA_XCONFIG).unstripped: $(OBJS)
	$(call quiet_cmd,LINK) $(CFLAGS) $(LDFLAGS) $(BIN_LDFLAGS) \
	    $(PCIACCESS_LDFLAGS) -o $@ $(OBJS) $(LIBS)

# make_usable.c includes pciaccess.h
$(call BUILD_OBJECT_LIST,make_usable.c): CFLAGS += $(PCIACCESS_CFLAGS)

# define the rule to build each object file
$(foreach src, $(SRC), $(eval $(call DEFINE_OBJECT_RULE,TARGET,$(src))))

clean clobber:
	$(RM) -rf $(NVIDIA_XCONFIG) $(MANPAGE) *~ \
		$(OUTPUTDIR)/*.o $(OUTPUTDIR)/*.d \
		$(GEN_MANPAGE_OPTS) $(OPTIONS_1_INC)


##############################################################################
# Documentation
##############################################################################

AUTO_TEXT = ".\\\" WARNING: THIS FILE IS AUTO-GENERATED!  Edit $< instead."

doc: $(MANPAGE)

GEN_MANPAGE_OPTS_SRC  = gen-manpage-opts.c
GEN_MANPAGE_OPTS_SRC += $(COMMON_UTILS_DIR)/gen-manpage-opts-helper.c

GEN_MANPAGE_OPTS_OBJS = $(call BUILD_OBJECT_LIST,$(GEN_MANPAGE_OPTS_SRC))

$(foreach src, $(GEN_MANPAGE_OPTS_SRC), \
    $(eval $(call DEFINE_OBJECT_RULE,HOST,$(src))))

$(GEN_MANPAGE_OPTS): $(GEN_MANPAGE_OPTS_OBJS)
	$(call quiet_cmd,HOST_LINK) \
	    $(HOST_CFLAGS) $(HOST_LDFLAGS) $(HOST_BIN_LDFLAGS) $^ -o $@

$(OPTIONS_1_INC): $(GEN_MANPAGE_OPTS)
	@$< > $@

$(MANPAGE_not_gzipped): nvidia-xconfig.1.m4 $(OPTIONS_1_INC) $(VERSION_MK)
	$(call quiet_cmd,M4) -D__HEADER__=$(AUTO_TEXT) -I $(OUTPUTDIR) \
	  -D__VERSION__=$(NVIDIA_VERSION) \
	  -D__DATE__="`$(DATE) +%F`" \
	  -D__BUILD_OS__=$(TARGET_OS) \
	  $< > $@

$(MANPAGE_gzipped): $(MANPAGE_not_gzipped)
	$(GZIP_CMD) -9nf < $< > $@
