#
# Copyright (c) 2018-2020, Arm Limited. All rights reserved.
# Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#

MAKE_HELPERS_DIRECTORY := ../../make_helpers/
include ${MAKE_HELPERS_DIRECTORY}build_macros.mk
include ${MAKE_HELPERS_DIRECTORY}build_env.mk

SPTOOL  ?= sptool${BIN_EXT}
BUILD_DIR = $(dir $(SPTOOL))/
SOURCES := $(wildcard *.c)
V ?= 0

override CPPFLAGS += -D_GNU_SOURCE -D_XOPEN_SOURCE=700
HOSTCCFLAGS := -Wall -Werror -pedantic -std=c99
ifeq (${DEBUG},1)
  HOSTCCFLAGS += -g -O0 -DDEBUG
else
  HOSTCCFLAGS += -O2
endif

ifeq (${V},0)
  Q := @
else
  Q :=
endif

INCLUDE_PATHS := -I../../include/tools_share

HOSTCC ?= gcc

.PHONY: all clean distclean

all: ${SPTOOL}

HOST_OBJS :=

define MAKE_HOST_OBJS
$(eval HOST_OBJ := $(1)/$(patsubst %.c,%.o,$(notdir $(2))))

$(HOST_OBJ): $(2)
	@echo " HOSTCC $$<"
	$${Q}$${HOSTCC} -c $${CPPFLAGS} ${HOSTCCFLAGS} $${INCLUDE_PATHS} $$< -o $$@
HOST_OBJS += $(HOST_OBJ)
endef

$(eval $(call MAKE_HOST_OBJS, $(BUILD_DIR), $(SOURCES)))

${SPTOOL}: ${HOST_OBJS} Makefile
	@echo "  HOSTLD  $@"
	${Q}${HOSTCC} ${HOST_OBJS} -o $@ ${LDLIBS}
	@${ECHO_BLANK_LINE}
	@echo "Built $@ successfully"
	@${ECHO_BLANK_LINE}

clean:
	$(call SHELL_DELETE_ALL, ${HOST_OBJS} ${SPTOOL})
