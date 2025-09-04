# SPDX-FileCopyrightText: Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

include ../Rules.mk

APP := camera_sw_encode

ARGUS_UTILS_DIR := $(TOP_DIR)/argus/samples/utils

SRCS := \
	argus_camera_sw_encode_main.cpp \
	$(wildcard $(CLASS_DIR)/*.cpp) \
	$(ARGUS_UTILS_DIR)/Thread.cpp \
	$(ARGUS_UTILS_DIR)/NativeBuffer.cpp \
	$(ARGUS_UTILS_DIR)/nvmmapi/NvNativeBuffer.cpp

OBJS := $(SRCS:.cpp=.o)

CPPFLAGS += \
	-I"$(ARGUS_UTILS_DIR)"

LDFLAGS += \
	-lnvargus_socketclient -lavcodec -lavutil

all: $(APP)

$(CLASS_DIR)/%.o: $(CLASS_DIR)/%.cpp
	$(AT)$(MAKE) -C $(CLASS_DIR)

%.o: %.cpp
	@echo "Compiling: $<"
	$(CPP) $(CPPFLAGS) -c $< -o $@

$(APP): $(OBJS)
	@echo "Linking: $@"
	$(CPP) -o $@ $(OBJS) $(CPPFLAGS) $(LDFLAGS)

clean:
	$(AT)rm -rf $(APP) $(OBJS)
