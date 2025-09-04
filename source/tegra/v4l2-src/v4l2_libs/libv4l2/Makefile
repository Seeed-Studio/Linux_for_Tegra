###############################################################################
#
# Copyright (c) 2018-2019, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA Corporation is strictly prohibited.
#
###############################################################################

SO_NAME := libnvv4l2.so

SRCS := libv4l2.c log.c v4l2convert.c v4l2-plugin.c

INCLUDES += -I./ -I../include

OBJS := $(SRCS:.c=.o)

CFLAGS := -fPIC

MACHINE = $(shell uname -m)

ifeq ($(MACHINE),x86_64)
	CFLAGS += -DLIBV4L2_PLUGIN_DIR_PATH_X86
	DEST_DIR ?= /opt/nvidia/deepstream/deepstream-4.0/lib/
	SYM_LINK_DIR = $(DEST_DIR)
else
	DEST_DIR ?= /usr/lib/$(MACHINE)-linux-gnu/tegra
	SYM_LINK_DIR = $(shell realpath $(DEST_DIR)/..)
endif

LDFLAGS := -L$(DEST_DIR) -Wl,-soname,libv4l2.so.0

LIBS := -lv4lconvert -ldl

all: $(SO_NAME)

%.o: %.c
	$(CC) -c $< $(CFLAGS) $(INCLUDES) -o $@

$(SO_NAME): $(OBJS)
	$(CC) -shared -o $(SO_NAME) $(OBJS) $(LIBS) $(LDFLAGS)

.PHONY: install
install: $(SO_NAME)
	cp -vp $(SO_NAME) $(DEST_DIR)
	if [ "$(MACHINE)" = "aarch64" ]; then \
		ln -sf tegra/$(SO_NAME) $(SYM_LINK_DIR)/libv4l2.so.0.0.999999 ; \
	else \
		ln -sf $(SO_NAME) $(SYM_LINK_DIR)/libv4l2.so.0.0.999999 ; \
	fi
	ln -sf libv4l2.so.0.0.999999 \
		$(SYM_LINK_DIR)/libv4l2.so
	ln -sf libv4l2.so.0.0.999999 \
		${SYM_LINK_DIR}/libv4l2.so.0

.PHONY: clean
clean:
	rm -rf $(OBJS) $(SO_NAME)
