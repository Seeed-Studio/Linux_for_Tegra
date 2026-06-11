#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
# This host-side script applies the Debian packages to the rootfs dir
# pointed to by L4T_ROOTFS_DIR/opt/nvidia/l4t-packages.
#

set -e

# Source the deb skiplist functions
source "$(dirname "$(readlink -f "$0")")/nv-deb-skiplist.sh"

# show the usages text
function ShowUsage {
	local ScriptName=$1

	echo "Use: sudo "${ScriptName}" [--root|-r PATH] [--help|-h]"
cat <<EOF
	This host-side script copies over tegra debian packages
	Options are:
	--dgpu
				   only install packages suitable for dGPU use
	--factory
				   only install packages suitable for factory use
	--openrm
				   install packages required for openrm
	--root|-r PATH
				   specify root directory
	--help|-h
				   show this help
EOF
}

function IsInDebSkipList {
	local pkg="${1}"
	for package in "${deb_skiplist[@]}" ; do
		if [[ "${pkg}" == "${package}"* ]]; then
			echo "YES"
			return
		fi
	done
	echo "NO"
}

function IsInForceInstallDebList {
	local pkg="${1}"
	for package in "${force_install_deb_list[@]}" ; do
		if [[ "${pkg}" == "${package}"* ]]; then
			echo "YES"
			return
		fi
	done
	echo "NO"
}

function cleanup {
	# Clean up qemu setup in rootfs if script is sourced
	if [ -d "${L4T_ROOTFS_DIR}" ]; then
		# check if script is sourced and function is defined
		if declare -f qemu_cleanup_rootfs > /dev/null; then
			qemu_cleanup_rootfs "${L4T_ROOTFS_DIR}"
		fi
	fi
}

trap cleanup EXIT

# if the user is not root, there is not point in going forward
if [ $(id -u) -ne 0 ]; then
	echo "This script requires root privilege"
	exit 1
fi

SCRIPT_NAME=$(basename "$0")

# parse the command line first
TGETOPT=`getopt -n "$SCRIPT_NAME" --longoptions help,dgpu,factory,openrm,root: \
-o hr: -- "$@"`

eval set -- "$TGETOPT"

OPENRM="false"
while [ $# -gt 0 ]; do
	case "$1" in
	--dgpu) DGPU="true" ;;
	--factory) FACTORY="true" ;;
	--openrm) OPENRM="true" ;;
	-h|--help) ShowUsage "$SCRIPT_NAME"; exit 1 ;;
	-r|--root) L4T_ROOTFS_DIR="$2"; shift ;;
	--) shift; break ;;
	-*) echo "Terminating... wrong switch: $@" >&2 ; ShowUsage "$SCRIPT_NAME"; \
	exit 1 ;;
	esac
	shift
done

if [ $# -gt 0 ]; then
	ShowUsage "$SCRIPT_NAME"
	exit 1
fi

# done, now do the work, save the directory
L4T_NV_TEGRA_DIR=$(cd $(dirname $0) && pwd)

# assumption: this script is part of the BSP and under L4T_DIR/nv_tegra
L4T_DIR="${L4T_NV_TEGRA_DIR}/.."
L4T_BOOTLOADER_DIR="${L4T_DIR}/bootloader"
L4T_SCRIPTS_DIR="${L4T_DIR}/nv_tools/scripts"

# check if the dir holding Debian packages exists in the BSP
if [ ! -d "${L4T_NV_TEGRA_DIR}/l4t_deb_packages" ]; then
	echo "Debian packages are curently not supported"
	exit 1
fi

# use default rootfs dir if none is set
if [ -z "$L4T_ROOTFS_DIR" ] ; then
	L4T_ROOTFS_DIR="${L4T_DIR}/rootfs"
fi

echo "Root file system directory is ${L4T_ROOTFS_DIR}"

# dir on target rootfs to keep Debian packages prior to installation
L4T_TARGET_DEB_DIR="/opt/nvidia/l4t-packages"
L4T_ROOTFS_DEB_DIR="${L4T_ROOTFS_DIR}${L4T_TARGET_DEB_DIR}"

# Create directory for debian packages
echo "Creating directory for debian packages"
mkdir -p "${L4T_ROOTFS_DEB_DIR}"

# Get list of packages to install
echo "Getting list of packages to install"
PKG_LIST_OPTIONS=""
if [[ "${OPENRM}" == "true" ]]; then
	PKG_LIST_OPTIONS="--openrm"
fi
PACKAGE_LIST=$("${L4T_SCRIPTS_DIR}/nv_l4t_get_package_install_list.sh" \
	--bsp "${L4T_DIR}" \
	${PKG_LIST_OPTIONS} \
	--type deb )

if [ -z "${PACKAGE_LIST}" ]; then
	echo "No packages found to install"
	exit 1
fi

# Get the skiplist based on parameters
UpdateDebSkipList "${DGPU}" "${FACTORY}"

deb_list=()
deb_list_force_install=()

# Copy and process packages from the list
echo "Copying and processing debian packages"
while IFS= read -r package_path; do
	# Skip empty lines
	[ -z "${package_path}" ] && continue

	# Get package name
	deb_name=$(basename "${package_path}")

	# Copy package to rootfs
	cp "${package_path}" "${L4T_ROOTFS_DEB_DIR}/"

	# Process package based on name
	if [ "$(IsInDebSkipList "${deb_name}")" == "NO" ]; then
		if [ "$(IsInForceInstallDebList "${deb_name}")" == "YES" ]; then
			deb_list_force_install+=("${L4T_TARGET_DEB_DIR}/${deb_name}")
		else
			deb_list+=("${L4T_TARGET_DEB_DIR}/${deb_name}")
		fi
	else
		echo "Skipping installation of ${deb_name} ...."
	fi
done <<< "${PACKAGE_LIST}"

if [ "${#deb_list[@]}" -eq 0 ]; then
	echo "No packages to install. There might be something wrong"
	exit 1
fi

if [ -e "${L4T_BOOTLOADER_DIR}/generic/cfg/nv_boot_control.conf" ]; then
	# copy nv_boot_control.conf to rootfs to support bootloader
	# and kernel updates
	echo "Copying nv_boot_control.conf to rootfs"
	cp "${L4T_BOOTLOADER_DIR}/generic/cfg/nv_boot_control.conf" \
	"${L4T_ROOTFS_DIR}/etc/"
fi

echo "Start L4T BSP package installation"

# Source the qemu setup helper
source "${L4T_NV_TEGRA_DIR}/nv_qemu_setup_helper.sh"

# Setup QEMU in rootfs
qemu_setup_rootfs "${L4T_ROOTFS_DIR}"

pushd "${L4T_ROOTFS_DIR}"
touch "${L4T_ROOTFS_DEB_DIR}/.nv-l4t-disable-boot-fw-update-in-preinstall"

echo "Installing BSP Debian packages in ${L4T_ROOTFS_DIR}"
LC_ALL=C PYTHONHASHSEED=0 chroot . dpkg -i --path-include="/usr/share/doc/*" "${deb_list[@]}"
if [ "${#deb_list_force_install[@]}" -ne 0 ]; then
	LC_ALL=C PYTHONHASHSEED=0 chroot . dpkg -i --path-include="/usr/share/doc/*" --force-depends "${deb_list_force_install[@]}"
fi
rm -f "${L4T_ROOTFS_DEB_DIR}/.nv-l4t-disable-boot-fw-update-in-preinstall"
popd

echo "Removing stashed Debian packages from rootfs"
rm -rf "${L4T_ROOTFS_DEB_DIR}"

echo "L4T BSP package installation completed!"
exit 0
