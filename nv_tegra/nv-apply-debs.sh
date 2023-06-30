#!/bin/bash

# Copyright (c) 2019-2023, NVIDIA CORPORATION. All rights reserved.
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

#
# This host-side script applies the Debian packages to the rootfs dir
# pointed to by L4T_ROOTFS_DIR/opt/nvidia/l4t-packages.
#

set -e

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
	--root|-r PATH
				   specify root directory
	--help|-h
				   show this help
EOF
}

function AddDebsList {
	local category="${1}"

	if [ -z "${category}" ]; then
		echo "Category not specified"
		exit 1
	fi

	for deb in "${L4T_ROOTFS_DEB_DIR}/${category}"/*.deb; do
		deb_name=$(basename ${deb})
		if [[ "${deb_name}" == "nvidia-l4t-core"* ]]; then
			pre_deb_list+=("${L4T_TARGET_DEB_DIR}/${category}/${deb_name}")
		else
			if [ "$(IsInDebSkipList "${deb_name}")" == "NO" ]; then
				if [ "$(IsInForceInstallDebList "${deb_name}")" == "YES" ]; then
					deb_list_force_install+=("${L4T_TARGET_DEB_DIR}/${category}/${deb_name}")
				else
					deb_list+=("${L4T_TARGET_DEB_DIR}/${category}/${deb_name}")
				fi
			else
				echo "Skipping installation of ${deb_name} ...."
			fi
		fi
	done
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
# if the user is not root, there is not point in going forward
if [ $(id -u) -ne 0 ]; then
	echo "This script requires root privilege"
	exit 1
fi

SCRIPT_NAME=$(basename "$0")

# parse the command line first
TGETOPT=`getopt -n "$SCRIPT_NAME" --longoptions help,dgpu,factory,root: \
-o hr: -- "$@"`

eval set -- "$TGETOPT"

while [ $# -gt 0 ]; do
	case "$1" in
	--dgpu) DGPU="true" ;;
	--factory) FACTORY="true" ;;
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
L4T_NV_TEGRA_DIR=$(cd `dirname $0` && pwd)

# assumption: this script is part of the BSP and under L4T_DIR/nv_tegra
L4T_DIR="${L4T_NV_TEGRA_DIR}/.."
L4T_KERN_DIR="${L4T_DIR}/kernel"
L4T_BOOTLOADER_DIR="${L4T_DIR}/bootloader"

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

# copy debian packages and installation script to rootfs
echo "Copying public debian packages to rootfs"
mkdir -p "${L4T_ROOTFS_DEB_DIR}/userspace"
mkdir -p "${L4T_ROOTFS_DEB_DIR}/kernel"
mkdir -p "${L4T_ROOTFS_DEB_DIR}/bootloader"
mkdir -p "${L4T_ROOTFS_DEB_DIR}/standalone"
# pre_deb_list includes Debian packages which must be installed before
# deb_list
pre_deb_list=()
deb_list=()
deb_list_force_install=()
force_install_deb_list=()

# List of packages to skip for dgpu
dgpu_skiplist=("nvidia-l4t-3d-core")
dgpu_skiplist+=("nvidia-l4t-apt-source")
dgpu_skiplist+=("nvidia-l4t-bootloader")
dgpu_skiplist+=("nvidia-l4t-cuda")
dgpu_skiplist+=("nvidia-l4t-display-kernel")
dgpu_skiplist+=("nvidia-l4t-gbm")
dgpu_skiplist+=("nvidia-l4t-graphics-demos")
dgpu_skiplist+=("nvidia-l4t-jetsonpower-gui-tools")
dgpu_skiplist+=("nvidia-l4t-nvpmodel-gui-tools")
dgpu_skiplist+=("nvidia-l4t-optee")
dgpu_skiplist+=("nvidia-l4t-wayland")
dgpu_skiplist+=("nvidia-l4t-weston")
dgpu_skiplist+=("nvidia-l4t-x11")

if [ "${FACTORY}" == "true" ]; then
	deb_skiplist=("${dgpu_skiplist[@]}")
	deb_skiplist+=("nvidia-l4t-camera")
	deb_skiplist+=("nvidia-l4t-gstreamer")
	deb_skiplist+=("nvidia-l4t-multimedia")
	deb_skiplist+=("nvidia-l4t-pva")
	deb_skiplist+=("nvidia-l4t-dgpu-config")
	deb_skiplist+=("nvidia-l4t-dgpu-apt-source")
elif [ "${DGPU}" == "true" ]; then
	deb_skiplist=("${dgpu_skiplist[@]}")
	# dependent packages at the time of flashing.
	force_install_deb_list+=("nvidia-l4t-camera")
	force_install_deb_list+=("nvidia-l4t-gstreamer")
	force_install_deb_list+=("nvidia-l4t-multimedia")
	force_install_deb_list+=("nvidia-l4t-pva")
else
	deb_skiplist=()
	deb_skiplist+=("nvidia-l4t-dgpu-config")
	deb_skiplist+=("nvidia-l4t-dgpu-apt-source")
fi

cp "${L4T_DIR}/tools"/*.deb "${L4T_ROOTFS_DEB_DIR}/standalone"
AddDebsList "standalone"
cp "${L4T_NV_TEGRA_DIR}/l4t_deb_packages"/*.deb \
"${L4T_ROOTFS_DEB_DIR}/userspace"
AddDebsList "userspace"
debs=(`find "${L4T_KERN_DIR}" -maxdepth 1 -iname "*.deb"`)
if [ "${#debs[@]}" -ne 0 ]; then
	cp "${L4T_KERN_DIR}"/*.deb "${L4T_ROOTFS_DEB_DIR}/kernel"
	AddDebsList "kernel"
else
	echo "Kernel debian package NOT found. Skipping."
fi
debs=(`find "${L4T_BOOTLOADER_DIR}" -maxdepth 1 -iname "*.deb"`)
if [ "${#debs[@]}" -ne 0 ]; then
	cp "${L4T_BOOTLOADER_DIR}"/*.deb "${L4T_ROOTFS_DEB_DIR}/bootloader"
	AddDebsList "bootloader"
else
	echo "Bootloader debian package NOT found. Skipping."
fi

if [ "${#deb_list[@]}" -eq 0 ]; then
	echo "No packages to install. There might be something wrong"
	exit 1
fi

if [ -e "${L4T_BOOTLOADER_DIR}/t186ref/cfg/nv_boot_control.conf" ]; then
	# copy nv_boot_control.conf to rootfs to support bootloader
	# and kernel updates
	echo "Copying nv_boot_control.conf to rootfs"
	cp "${L4T_BOOTLOADER_DIR}/t186ref/cfg/nv_boot_control.conf" \
	"${L4T_ROOTFS_DIR}/etc/"
fi

echo "Start L4T BSP package installation"
# Try the stashed copy which should be packed in customer_release.tbz2 first
if [ -f "${L4T_DIR}/../qemu-aarch64-static" ]; then
	QEMU_BIN="${L4T_DIR}/../qemu-aarch64-static"
elif [ -f "${L4T_NV_TEGRA_DIR}/qemu-aarch64-static" ]; then
	QEMU_BIN="${L4T_NV_TEGRA_DIR}/qemu-aarch64-static"
else
	echo "QEMU binary is not available, looking for QEMU from host system"
	if [ -f "/usr/bin/qemu-aarch64-static" ]; then
		echo "Found /usr/bin/qemu-aarch64-static"
		QEMU_BIN="/usr/bin/qemu-aarch64-static"
	fi

	if [ -z "${QEMU_BIN}" ]; then
		echo "ERROR qemu not found! To install - please run: " \
			"\"sudo apt-get install qemu-user-static\""
		exit 1
	fi
fi
echo "Installing QEMU binary in rootfs"
install --owner=root --group=root "${QEMU_BIN}" "${L4T_ROOTFS_DIR}/usr/bin/"

mknod -m 444 "${L4T_ROOTFS_DIR}/dev/random" c 1 8
mknod -m 444 "${L4T_ROOTFS_DIR}/dev/urandom" c 1 9

pushd "${L4T_ROOTFS_DIR}"
touch "${L4T_ROOTFS_DEB_DIR}/.nv-l4t-disable-boot-fw-update-in-preinstall"
echo "Installing BSP Debian packages in ${L4T_ROOTFS_DIR}"
if [ "${#pre_deb_list[@]}" -ne 0 ]; then
	LC_ALL=C PYTHONHASHSEED=0 chroot . dpkg -i --path-include="/usr/share/doc/*" "${pre_deb_list[@]}"
fi
LC_ALL=C PYTHONHASHSEED=0 chroot . dpkg -i --path-include="/usr/share/doc/*" "${deb_list[@]}"
if [ "${#deb_list_force_install[@]}" -ne 0 ]; then
	LC_ALL=C PYTHONHASHSEED=0 chroot . dpkg -i --path-include="/usr/share/doc/*" --force-depends "${deb_list_force_install[@]}"
fi
rm -f "${L4T_ROOTFS_DEB_DIR}/.nv-l4t-disable-boot-fw-update-in-preinstall"
popd

echo "Removing QEMU binary from rootfs"
rm -f "${L4T_ROOTFS_DIR}/usr/bin/qemu-aarch64-static"

rm -f "${L4T_ROOTFS_DIR}/dev/random"
rm -f "${L4T_ROOTFS_DIR}/dev/urandom"

echo "Removing stashed Debian packages from rootfs"
rm -rf "${L4T_ROOTFS_DEB_DIR}"

echo "L4T BSP package installation completed!"
exit 0
