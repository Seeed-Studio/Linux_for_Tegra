#!/bin/bash
set -eo pipefail
# SPDX-FileCopyrightText: Copyright (c) 2021-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
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

package_list=(abootimg binfmt-support binutils cpio cpp
	device-tree-compiler dosfstools file gdisk
	iproute2 iputils-ping lbzip2 libxml2-utils
	netcat-openbsd nfs-kernel-server openssl
	parted python3-yaml qemu-user-static rsync sshpass
	udev usbutils uuid-runtime whois xmlstarlet xxd zstd zlib1g python3-usb)

# Install lz4c utility required for compressing bpmp-fw-dtb.
# For Ubuntu 18.04 and older version, run "sudo apt-get install liblz4-tool".
# For Ubuntu 20.04 and newer version, run "sudo apt-get install lz4".
SYSTEM_VER="$(grep "DISTRIB_RELEASE" </etc/lsb-release | cut -d= -f 2 | sed 's/\.//')"
if [ "${SYSTEM_VER}" -lt 2004 ]; then
	package_list+=(liblz4-tool)
else
	package_list+=(lz4 python-is-python3)
fi

sudo -E apt-get update
sudo -E apt-get install -y "${package_list[@]}"


for element in "${package_list[@]}"; do
	if [ -z "$(sudo apt -qq list "${element}" 2>/dev/null)" ]; then
		echo "Error: ${element} is not installed"
		exit 1
	fi
done


