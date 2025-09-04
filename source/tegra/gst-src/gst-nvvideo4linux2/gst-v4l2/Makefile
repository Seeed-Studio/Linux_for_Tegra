###############################################################################
#
# Copyright (c) 2018-2023, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA Corporation is strictly prohibited.
#
###############################################################################

SO_NAME := libgstnvvideo4linux2.so

TARGET_DEVICE = $(shell gcc -dumpmachine | cut -f1 -d -)

NVDS_VERSION:=6.0

ifeq ($(TARGET_DEVICE),aarch64)
  GST_INSTALL_DIR?=/usr/lib/aarch64-linux-gnu/gstreamer-1.0/
  LIB_INSTALL_DIR?=/usr/lib/aarch64-linux-gnu/tegra/
  CFLAGS:=
else
  GST_INSTALL_DIR?=/opt/nvidia/deepstream/deepstream-$(NVDS_VERSION)/lib/gst-plugins/
  LIB_INSTALL_DIR?=/opt/nvidia/deepstream/deepstream-$(NVDS_VERSION)/lib/
  CFLAGS:= -DUSE_V4L2_TARGET_NV_CODECSDK=1 -DUSE_V4L2_TARGET_NV_X86=1 -DUSE_V4L2_GST_HEADER_VER_1_8
endif

LIBS:= -lnvbufsurface -lnvbufsurftransform -lgstnvdsseimeta -lgstnvcustomhelper
SRCS := $(wildcard *.c)

INCLUDES += -I./ -I../
INCLUDES += -I/usr/src/jetson_multimedia_api/include/

PKGS := gstreamer-1.0 \
	gstreamer-base-1.0 \
	gstreamer-video-1.0 \
	gstreamer-allocators-1.0 \
	glib-2.0 \
	libv4l2

OBJS := $(SRCS:.c=.o)

CFLAGS += -fPIC \
	-DEXPLICITLY_ADDED=1 \
        -DGETTEXT_PACKAGE=1 \
        -DHAVE_LIBV4L2=1 \
        -DUSE_V4L2_TARGET_NV=1

CFLAGS += `pkg-config --cflags $(PKGS)`

LDFLAGS = -Wl,--no-undefined -L$(LIB_INSTALL_DIR) -Wl,-rpath,$(LIB_INSTALL_DIR)

LIBS += `pkg-config --libs $(PKGS)`

all: $(SO_NAME)

%.o: %.c
	$(CC) -c $< $(CFLAGS) $(INCLUDES) -o $@

$(SO_NAME): $(OBJS)
	$(CC) -shared -o $(SO_NAME) $(OBJS) $(LIBS) $(LDFLAGS)

.PHONY: install
install: $(SO_NAME)
	cp -vp $(SO_NAME) $(GST_INSTALL_DIR)

.PHONY: clean
clean:
	rm -rf $(OBJS) $(SO_NAME)
