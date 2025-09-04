###############################################################################
#
# Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#
# makefile for libv4l2_nvargus library
#
###############################################################################

LIBRARY_NAME:= libv4l2_nvargus.so

# Clear the flags from env
CPPFLAGS :=
LDFLAGS :=
LIBS :=

INSTALL_DIR?=/usr/lib/aarch64-linux-gnu/tegra/
LINK_DIR?=/usr/lib/aarch64-linux-gnu/libv4l/plugins/nv/

# Verbose flag
ifeq ($(VERBOSE), 1)
AT =
else
AT = @
endif

# ARM ABI of the target platform
TEGRA_ARMABI ?= aarch64-linux-gnu

# Location of the target rootfs
ifeq ($(shell uname -m), aarch64)
TARGET_ROOTFS :=
CROSS_COMPILE :=
else
CROSS_COMPILE ?= aarch64-unknown-linux-gnu-
ifeq ($(TARGET_ROOTFS),)
$(error "TARGET_ROOTFS" is not set. Please set TARGET_ROOTFS if you are cross-compiling.)
endif
endif

ARGUS_UTILS_GTK_PATH := $(TARGET_ROOTFS)/usr/src/jetson_multimedia_api/argus/samples/utils/gtk
ARGUS_UTILS_SRC_PATH := $(TARGET_ROOTFS)/usr/src/jetson_multimedia_api/argus/samples/utils

CC             = $(AT) $(CROSS_COMPILE)gcc
CPP            = $(AT) $(CROSS_COMPILE)gcc
RM             = $(AT) $(CROSS_COMPILE)rm -f

PKGS := gtk+-3.0 \
		glx \
		glib-2.0

CPPFLAGS += \
	-I"$(TARGET_ROOTFS)/usr/include" \
	-I"$(TARGET_ROOTFS)/usr/include/$(TEGRA_ARMABI)" \
	-I"$(TARGET_ROOTFS)/usr/src/argus/include" \
	-I"$(TARGET_ROOTFS)/usr/src/jetson_multimedia_api/argus/samples/utils" \
	-I"$(TARGET_ROOTFS)/usr/src/jetson_multimedia_api/argus/samples/utils/gtk" \
	-I"$(TARGET_ROOTFS)/usr/src/jetson_multimedia_api/include" \
	-I./inc

CPPFLAGS += \
	-pthread \
	-fno-rtti -std=gnu++14 -Wall -fPIC

CPPFLAGS += `pkg-config --cflags $(PKGS)`

LIBS += \
	-shared -lnvv4l2 -lEGL -lGLESv2 -lX11 -lpthread -Wl,--no-undefined \
	-lnvbufsurface -lnvbufsurftransform -lnvargus_socketclient -ldl -lstdc++ -lm

LDFLAGS += -L"/usr/lib" \
	-L"/usr/lib/$(TEGRA_ARMABI)/" \
	-L"/usr/lib/$(TEGRA_ARMABI)/tegra"

LIBS += `pkg-config --libs $(PKGS)`

SRCS := \
	libv4l2_nvargus.cpp \
	nvargusv4l2.cpp \
	nvargusv4l2_context.cpp \
	nvargusv4l2_ioctl.cpp \
	nvargusv4l2_argus.cpp \
	nvargusv4l2_os.cpp \
	nvargusv4l2_nvqueue.cpp \
	$(wildcard $(ARGUS_UTILS_GTK_PATH)/*.cpp) \
	$(ARGUS_UTILS_SRC_PATH)/nvmmapi/NvNativeBuffer.cpp \
	$(filter-out $(ARGUS_UTILS_SRC_PATH)/CUDAHelper.cpp, \
	$(wildcard $(ARGUS_UTILS_SRC_PATH)/*.cpp))

OBJS = $(patsubst %.cpp,%.o,$(SRCS))

all: $(LIBRARY_NAME)

.o: .cpp
	@echo "Compiling: $(CPP) $(CPPFLAGS) $<"
	$(CPP) $(CPPFLAGS) -c $<

$(LIBRARY_NAME): $(OBJS)
	@echo "Linking: $@"
	$(CPP) -o $@ $(OBJS) $(CPPFLAGS) $(LIBS) $(LDFLAGS)

.PHONY: install
install: $(LIBRARY_NAME)
	sudo cp -vp $(LIBRARY_NAME) $(INSTALL_DIR)
	sudo ln -sf $(INSTALL_DIR)$(LIBRARY_NAME) $(LINK_DIR)$(LIBRARY_NAME)

clean:
	$(RM) *.o $(LIBRARY_NAME) *~
