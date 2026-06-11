#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#
# This script provides functions to get the list of Debian packages to skip
# based on the target configuration (dGPU, factory, etc.)
# This script is designed to be sourced, not executed directly.
#

deb_skiplist=()
force_install_deb_list=()

#
# UpdateDebSkipList - Updates the global deb_skiplist and force_install_deb_list arrays
#                    based on target configuration parameters
#
# Parameters:
#   $1 (dgpu)   - Boolean string ("true"/"false") indicating if target is dGPU
#   $2 (factory) - Boolean string ("true"/"false") indicating if target is factory build
#
# Global Variables Modified:
#   deb_skiplist[]        - Array of package names to skip during installation
#   force_install_deb_list[] - Array of package names to force install (for dGPU targets)
#
# Usage:
#   UpdateDebSkipList "true" "false"   # For dGPU target
#   UpdateDebSkipList "false" "true"   # For factory build
#   UpdateDebSkipList "false" "false"  # For standard target
#
# Behavior:
#   - For factory builds: Skips dGPU packages + additional factory-specific packages
#   - For dGPU targets: Skips standard GPU packages + forces install of multimedia packages
#   - For standard targets: Skips dGPU-specific packages
#   - Always skips IGX packages regardless of target type
#
function UpdateDebSkipList {
    local dgpu="${1}"
    local factory="${2}"
    deb_skiplist=()
    # force_install_deb_list is used by nv-apply-debs.sh which sources this file
    force_install_deb_list=()

    # List of packages to skip for dgpu
    local dgpu_skiplist=("nvidia-l4t-3d-core")
    dgpu_skiplist+=("nvidia-l4t-apt-source")
    dgpu_skiplist+=("nvidia-l4t-camera")
    dgpu_skiplist+=("nvidia-l4t-cuda")
    dgpu_skiplist+=("nvidia-l4t-cuda-nvgpu")
    dgpu_skiplist+=("nvidia-l4t-cuda-openrm")
    dgpu_skiplist+=("nvidia-l4t-cuda-utils")
    dgpu_skiplist+=("nvidia-l4t-firmware-nvgpu")
    dgpu_skiplist+=("nvidia-l4t-firmware-openrm")
    dgpu_skiplist+=("nvidia-l4t-gbm")
    dgpu_skiplist+=("nvidia-l4t-graphics-demos")
    dgpu_skiplist+=("nvidia-l4t-gstreamer")
    dgpu_skiplist+=("nvidia-l4t-init-nvgpu")
    dgpu_skiplist+=("nvidia-l4t-init-openrm")
    dgpu_skiplist+=("nvidia-l4t-jetsonpower-gui-tools")
    dgpu_skiplist+=("nvidia-l4t-kernel-nvgpu")
    dgpu_skiplist+=("nvidia-l4t-kernel-openrm")
    dgpu_skiplist+=("nvidia-l4t-libvulkan")
    dgpu_skiplist+=("nvidia-l4t-multimedia")
    dgpu_skiplist+=("nvidia-l4t-multimedia-nvgpu")
    dgpu_skiplist+=("nvidia-l4t-multimedia-openrm")
    dgpu_skiplist+=("nvidia-l4t-nvml")
    dgpu_skiplist+=("nvidia-l4t-nvpmodel-gui-tools")
    dgpu_skiplist+=("nvidia-l4t-pva")
    dgpu_skiplist+=("nvidia-l4t-video-codec-openrm")
    dgpu_skiplist+=("nvidia-l4t-vulkan-sc-dev")
    dgpu_skiplist+=("nvidia-l4t-vulkan-sc-sdk")
    dgpu_skiplist+=("nvidia-l4t-vulkan-sc-nvgpu")
    dgpu_skiplist+=("nvidia-l4t-vulkan-sc-openrm")
    dgpu_skiplist+=("nvidia-l4t-vulkan-sc-samples")
    dgpu_skiplist+=("nvidia-l4t-wayland")
    dgpu_skiplist+=("nvidia-l4t-weston")
    dgpu_skiplist+=("nvidia-l4t-x11")
    dgpu_skiplist+=("nvidia-l4t-xwayland")

    # remove these deprecated packages
    dgpu_skiplist+=("nvidia-l4t-dgpu-tools")
    dgpu_skiplist+=("nvidia-l4t-dgpu-config")
    dgpu_skiplist+=("nvidia-l4t-dgpu-x11")
    dgpu_skiplist+=("nvidia-l4t-dgpu-apt-source")

    if [ "${factory}" == "true" ]; then
        deb_skiplist=("${dgpu_skiplist[@]}")
        deb_skiplist+=("nvidia-l4t-dgpu-config")
        deb_skiplist+=("nvidia-l4t-dgpu-apt-source")
        deb_skiplist+=("nvidia-l4t-dgpu-x11")
        deb_skiplist+=("nvidia-l4t-dgpu-tools")
    elif [ "${dgpu}" == "true" ]; then
        deb_skiplist=("${dgpu_skiplist[@]}")
        deb_skiplist+=("nvidia-l4t-factory-service")
        deb_skiplist+=("nvidia-l4t-bsp")
    else
        deb_skiplist=()
        deb_skiplist+=("nvidia-l4t-dgpu-config")
        deb_skiplist+=("nvidia-l4t-factory-service")
        deb_skiplist+=("nvidia-l4t-dgpu-apt-source")
        deb_skiplist+=("nvidia-l4t-dgpu-x11")
        deb_skiplist+=("nvidia-l4t-dgpu-tools")
    fi

    # Skip installing empty transitional packages
    deb_skiplist+=("nvidia-l4t-bsp-nvgpu")
    deb_skiplist+=("nvidia-l4t-bsp-openrm")

    # Skip installing igx packages
    deb_skiplist+=("nvidia-igx-bootloader")
    deb_skiplist+=("nvidia-igx-oobe")
    deb_skiplist+=("nvidia-igx-systemd-reboot-hooks")
    deb_skiplist+=("nvidia-l4t-fbdev-x11")
}

# Check if this script is being sourced or executed
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    # Script is being executed directly
    echo "ERROR: This script is meant to be sourced, not executed directly."
    echo "Usage: source $(basename "$0")"
    exit 1
fi
