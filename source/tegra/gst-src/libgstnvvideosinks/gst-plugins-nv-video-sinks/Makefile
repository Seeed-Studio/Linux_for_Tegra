###############################################################################
#
# Copyright (c) 2022-2023, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA Corporation is strictly prohibited.
#
###############################################################################

ifneq ($(MAKECMDGOALS),install)
ifeq ($(CUDA_VER),)
  $(error "CUDA_VER is not set. Set it by running - "export CUDA_VER=<cuda-version>"")
endif
endif

SO_NAME := libgstnvvideosinks.so
DEST_DIR ?= /usr/lib/aarch64-linux-gnu/gstreamer-1.0

SRCS := common/context.c \
	common/display.c \
	common/egl/context_egl.c \
	common/renderer.c \
	common/renderer/renderer_gl.c \
	common/window.c \
	common/x11/display_x11.c \
	common/x11/window_x11.c \
	gstnvvideosinks.c \
	nv3dsink/gstnv3dsink.c

INCLUDES += -I./common \
	-I./common/egl \
	-I./common/renderer \
	-I./common/x11 \
	-I/usr/local/include/gstreamer-1.0 \
	-I/usr/local/cuda-$(CUDA_VER)/targets/aarch64-linux/include/ \
	-I../

PKGS := glib-2.0 \
	gstreamer-1.0 \
	gstreamer-base-1.0 \
	gstreamer-video-1.0

OBJS := $(SRCS:.c=.o)

CFLAGS := -fPIC \
	-DNV_VIDEO_SINKS_HAS_EGL \
	-DNV_VIDEO_SINKS_HAS_GL \
	-DNV_VIDEO_SINKS_HAS_NV3DSINK \
	-DNV_VIDEO_SINKS_HAS_X11

CFLAGS += `pkg-config --cflags $(PKGS)`

LDFLAGS = -Wl,--no-undefined -L/usr/lib/aarch64-linux-gnu/tegra -L/usr/local/cuda-$(CUDA_VER)/targets/aarch64-linux/lib/

LIBS = -lnvbufsurface -lGLESv2 -lEGL -lX11 -lm -lcuda -lcudart

LIBS += `pkg-config --libs $(PKGS)`

all: $(SO_NAME)

%.o: %.c
	$(CC) -c $< $(CFLAGS) $(INCLUDES) -o $@

$(SO_NAME): $(OBJS)
	$(CC) -shared -o $(SO_NAME) $(OBJS) $(LIBS) $(LDFLAGS)

.PHONY: install
$(DEST_DIR):
	mkdir -p $(DEST_DIR)
install: $(SO_NAME) | $(DEST_DIR)
	cp -vp $(SO_NAME) $(DEST_DIR)

.PHONY: clean
clean:
	rm -rf $(OBJS) $(SO_NAME)
