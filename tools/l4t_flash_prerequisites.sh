#!/bin/bash

# Copyright (c) 2021-2022, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

sudo apt-get update && \
sudo apt-get install -y abootimg \
			binfmt-support \
			binutils \
			cpp \
			device-tree-compiler \
			dosfstools \
			lbzip2 \
			libxml2-utils \
			nfs-kernel-server \
			python \
			python3-yaml \
			qemu-user-static \
			sshpass \
			udev \
			uuid-runtime \
			whois \
			openssl \
			cpio

# Install lz4c utility required for compressing bpmp-fw-dtb.
# For Ubuntu 18.04 and older version, run "sudo apt-get install liblz4-tool".
# For Ubuntu 20.04 and newer version, run "sudo apt-get install lz4".
SYSTEM_VER="$(grep "DISTRIB_RELEASE" </etc/lsb-release | cut -d= -f 2 | sed 's/\.//')"
if [ "${SYSTEM_VER}" -lt 2004 ]; then
	sudo apt-get install -y liblz4-tool
else
	sudo apt-get install -y lz4
fi
