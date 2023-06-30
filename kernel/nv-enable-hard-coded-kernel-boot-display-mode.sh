#!/bin/bash

# Copyright (c) 2018-2021 NVIDIA CORPORATION. All rights reserved.
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

# Purpose:
# This script updates a given DTB to add 'nvidia,fbcon-default-mode'
# and the required mode parameters under the node. This mode will be
# used by Tegra Display driver for fbconsole initialization.
#
# Usage:
# Under Linux_for_Tegra folder, execute:
# ./kernel/nv-enable-hard-coded-kernel-boot-display-mode.sh <dtb_name>
# By default, 720x480p@60Hz CEA mode is specified in the script.
#
# DTBs to be used:
# TX2 DTB: kernel/dtb/tegra186-quill-p3310-1000-c03-00-base.dtb
# TX1 DTB: kernel/dtb/tegra210-jetson-tx1-p2597-2180-a01-devkit.dtb
#
# Variables description:
# dtb_file	- Path to DTB file input by user
# fbcon_node	- Full path to 'nvidia,fbcon-default-mode' node in DT
# properties	- Mode parameters array. Do not edit the names. Values
# can be modified.
#
# 'properties' array format: <mode parameter>:<value>
# To specify a different mode, please update the value corresponding
# of each mode parameter in the 'properties' array.

dtb_file="$1"
if [ -z "${dtb_file}" ]; then
	echo "Missing parameter: DTB filename"
	exit 1
fi
if [ ! -f "${dtb_file}" ]; then
	echo "DTB ${dtb_file} doesn't exist!"
	exit 1
fi

# Is fdtput in the PATH and executable?
hash fdtput
if [ $? -ne 0 ]; then
	echo "The fdtput utility is required, but not installed. On Debian and"
	echo "derivatives, please \"apt install device-tree-compiler\""
	exit 1
fi

set -e

fbcon_node="/host1x/sor2/hdmi-display/nvidia,fbcon-default-mode"
properties=(
	"clock-frequency:27027000"
	"hactive:720"
	"vactive:480"
	"hfront-porch:16"
	"hback-porch:60"
	"hsync-len:62"
	"vfront-porch:9"
	"vback-porch:30"
	"vsync-len:6"
	"nvidia,h-ref-to-sync:1"
	"nvidia,v-ref-to-sync:1"
	"flags:0x00000003"
	"vmode:0x00400000"
)

for prop_val in "${properties[@]}"; do
	property=$(echo "$prop_val" | cut -f1 -d:)
	value=$(echo "$prop_val" | cut -f2 -d:)
	fdtput -p -t i "${dtb_file}" "${fbcon_node}" "${property}" "${value}"
done
