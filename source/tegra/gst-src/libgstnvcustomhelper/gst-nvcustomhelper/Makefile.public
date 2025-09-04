################################################################################ 
# SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: MIT                                                   
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
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL        
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER        
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING        
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER            
# DEALINGS IN THE SOFTWARE.                                                      
################################################################################ 

CXX:= gcc
SRCS:= gst-nvcustomevent.c

INCS:= $(wildcard *.h)
OUT_DIR ?= .
SO_NAME := libgstnvcustomhelper.so
LIB:= $(OUT_DIR)/$(SO_NAME)
OBJS :=  $(SRCS:%.c=$(OUT_DIR)/%.o)

TARGET_DEVICE = $(shell gcc -dumpmachine | cut -f1 -d -)
ifeq ($(TARGET_DEVICE),aarch64)
  LIB_INSTALL_DIR?=/usr/lib/aarch64-linux-gnu/tegra/
  CFLAGS:=
else
  NVDS_VERSION ?=6.2
  LIB_INSTALL_DIR ?=/opt/nvidia/deepstream/deepstream-$(NVDS_VERSION)/lib/
  CFLAGS:= -DDS_VERSION=\"6.2.1\"
endif

CFLAGS+= -fPIC
 
LIBS := -shared -Wl,-no-undefined
 
LIBS+= -L$(LIB_INSTALL_DIR) -ldl\
        -Wl,-rpath,$(LIB_INSTALL_DIR)

PKGS:= gstreamer-1.0 gstreamer-base-1.0 gstreamer-video-1.0
CFLAGS+= $(shell pkg-config --cflags $(PKGS))
LIBS+= $(shell pkg-config --libs $(PKGS))

all: $(LIB)

$(OUT_DIR)/%.o: %.c $(INCS) Makefile
	@echo $(CFLAGS)
	$(CXX) -c -o $@ $(CFLAGS) $<

$(LIB): $(OBJS) Makefile
	$(CXX) -o $@ $(OBJS) $(LIBS)

install: $(LIB)
	cp -rv $(LIB) $(LIB_INSTALL_DIR)

clean:
	rm -rf $(OBJS) $(LIB)
