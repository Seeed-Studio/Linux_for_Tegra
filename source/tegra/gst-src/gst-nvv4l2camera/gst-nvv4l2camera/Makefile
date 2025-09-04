# Copyright (c) 2019-2022, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#  * Neither the name of NVIDIA CORPORATION nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

SO_NAME := libgstnvv4l2camerasrc.so

CC := g++

TARGET_DEVICE = $(shell gcc -dumpmachine | cut -f1 -d -)

GST_INSTALL_DIR?=/usr/lib/aarch64-linux-gnu/gstreamer-1.0/
LIB_INSTALL_DIR?=/usr/lib/aarch64-linux-gnu/tegra/
CFLAGS:=
LIBS:= -lnvbufsurface -lnvbufsurftransform


SRCS := $(wildcard *.cpp)

INCLUDES += -I./ -I../

PKGS := gstreamer-1.0 \
	gstreamer-base-1.0 \
	gstreamer-video-1.0 \
	gstreamer-allocators-1.0 \
	glib-2.0 \
	libv4l2

OBJS := $(SRCS:.cpp=.o)

CFLAGS += -fPIC \
	-DEXPLICITLY_ADDED=1 \
        -DGETTEXT_PACKAGE=1 \
        -DHAVE_LIBV4L2=1 \
        -DUSE_V4L2_TARGET_NV=1

CFLAGS += `pkg-config --cflags $(PKGS)`

LDFLAGS = -Wl,--no-undefined -L$(LIB_INSTALL_DIR) -Wl,-rpath,$(LIB_INSTALL_DIR)

LIBS += `pkg-config --libs $(PKGS)`

all: $(SO_NAME)

%.o: %.cpp
	$(CC) -c $< $(CFLAGS) $(INCLUDES) -o $@

$(SO_NAME): $(OBJS)
	$(CC) -shared -o $(SO_NAME) $(OBJS) $(LIBS) $(LDFLAGS)

.PHONY: install
install: $(SO_NAME)
	cp -vp $(SO_NAME) $(GST_INSTALL_DIR)

.PHONY: clean
clean:
	rm -rf $(OBJS) $(SO_NAME)
