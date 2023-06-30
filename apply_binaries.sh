#!/bin/bash

# Copyright (c) 2011-2023, NVIDIA CORPORATION. All rights reserved.
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
# This script applies the binaries to the rootfs dir pointed to by
# LDK_ROOTFS_DIR variable.
#

set -e
set -o pipefail

# show the usages text
function ShowUsage {
    local ScriptName=$1

    echo "Use: $1 [--bsp|-b PATH] [--root|-r PATH] [--target-overlay] [--help|-h]"
cat <<EOF
    This script installs tegra binaries
    Options are:
    --bsp|-b PATH
                   bsp location (bsp, readme, installer)
    --dgpu
                   only install packages that are suitable for dGPU use
    --factory
                   only install packages that are suitable for factory use
    --root|-r PATH
                   install toolchain to PATH
    --rootless
                   don't require root privilege
    --target-overlay|-t
                   untar NVIDIA target overlay (.tbz2) instead of
				   pre-installing them as Debian packages
    --help|-h
                   show this help
EOF
}

function ShowDebug {
    echo "SCRIPT_NAME     : $SCRIPT_NAME"
    echo "DEB_SCRIPT_NAME : $DEB_SCRIPT_NAME"
    echo "LDK_ROOTFS_DIR  : $LDK_ROOTFS_DIR"
    echo "BOARD_NAME      : $TARGET_BOARD"
}

function ReplaceText {
	sed -i "s/$2/$3/" $1
	if [ $? -ne 0 ]; then
		echo "Error while editing a file. Exiting !!"
		exit 1
	fi
}

function AddSystemGroup {
	# add $1 as a system group and search unused gid decreasingly from
	# SYS_GID_MAX to SYS_GID_MIN
	pushd "${LDK_ROOTFS_DIR}" > /dev/null 2>&1
	if [ -z $(grep "^${1}:" ./etc/group) ]; then
		gids=($(cut -d: -f3 ./etc/group))
		for gid in {999..100}; do
			if [[ ! " ${gids[*]} " =~ " ${gid} " ]]; then
				echo "${1}:x:${gid}:" >> ./etc/group
				echo "${1}:!::" >> ./etc/gshadow
				break
			fi
		done
	fi
	popd > /dev/null 2>&1
}

# script name
SCRIPT_NAME=`basename $0`

# apply .deb script name
DEB_SCRIPT_NAME="nv-apply-debs.sh"

# empty root and no debug
DEBUG=

# flag used to switch between legacy overlay packages and debians
# default is debians, but can be switched to overlay by setting to "true"
USE_TARGET_OVERLAY_DEFAULT=

# parse the command line first
TGETOPT=`getopt -n "$SCRIPT_NAME" --longoptions help,bsp:,debug,dgpu,factory,target-overlay,root:,rootless -o b:dhr:b:t: -- "$@"`

if [ $? != 0 ]; then
    echo "Terminating... wrong switch"
    ShowUsage "$SCRIPT_NAME"
    exit 1
fi

eval set -- "$TGETOPT"

while [ $# -gt 0 ]; do
    case "$1" in
	--dgpu) DGPU="true" ;;
	--factory) FACTORY="true" ;;
	-r|--root) LDK_ROOTFS_DIR="$2"; shift ;;
	-h|--help) ShowUsage "$SCRIPT_NAME"; exit 1 ;;
	-d|--debug) DEBUG="true" ;;
	-t|--target-overlay) TARGET_OVERLAY="true" ;;
	-b|--bsp) BSP_LOCATION_DIR="$2"; shift ;;
	--rootless) ROOTLESS="true" ;;
	--) shift; break ;;
	-*) echo "Terminating... wrong switch: $@" >&2 ; ShowUsage "$SCRIPT_NAME"; exit 1 ;;
    esac
    shift
done

if [ $# -gt 0 ]; then
    ShowUsage "$SCRIPT_NAME"
    exit 1
fi

if [ "${ROOTLESS}" == "true" ]; then
    INSTALL_ROOT_OPTS=""
    FIND_ROOT_OPTS=""
else
    INSTALL_ROOT_OPTS="--owner=root --group=root"
    FIND_ROOT_OPTS="-user root -group root"

    # if the user is not root, there is not point in going forward
    if [ $(id -u) -ne 0 ]; then
        echo "This script requires root privilege"
        exit 1
    fi
fi

# done, now do the work, save the directory
LDK_DIR=$(cd `dirname $0` && pwd)

# use default rootfs dir if none is set
if [ -z "$LDK_ROOTFS_DIR" ]; then
    LDK_ROOTFS_DIR="${LDK_DIR}/rootfs"
fi

echo "Using rootfs directory of: ${LDK_ROOTFS_DIR}"

install ${INSTALL_ROOT_OPTS} -m 0755 -d "${LDK_ROOTFS_DIR}"

# get the absolute path, for LDK_ROOTFS_DIR.
# otherwise, tar behaviour is unknown in last command sets
TOP=$PWD
cd "${LDK_ROOTFS_DIR}"
LDK_ROOTFS_DIR="$PWD"
cd "$TOP"

if [ ! `find "$LDK_ROOTFS_DIR/etc/passwd" ${FIND_ROOT_OPTS}` ]; then
	echo "||||||||||||||||||||||| ERROR |||||||||||||||||||||||"
	echo "-----------------------------------------------------"
	echo "1. The root filesystem, provided with this package,"
	echo "   has to be extracted to this directory:"
	echo "   ${LDK_ROOTFS_DIR}"
	echo "-----------------------------------------------------"
	echo "2. The root filesystem, provided with this package,"
	echo "   has to be extracted with 'sudo' to this directory:"
	echo "   ${LDK_ROOTFS_DIR}"
	echo "-----------------------------------------------------"
	echo "Consult the Development Guide for instructions on"
	echo "extracting and flashing your device."
	echo "|||||||||||||||||||||||||||||||||||||||||||||||||||||"
	exit 1
fi

# assumption: this script is part of the BSP
#             so, LDK_DIR/nv_tegra always exist
LDK_NV_TEGRA_DIR="${LDK_DIR}/nv_tegra"
LDK_KERN_DIR="${LDK_DIR}/kernel"
LDK_TOOLS_DIR="${LDK_DIR}/tools"
LDK_BOOTLOADER_DIR="${LDK_DIR}/bootloader"
DEB_EXTRACTOR="${LDK_TOOLS_DIR}/l4t_extract_deb.sh"

if [ "${DEBUG}" == "true" ]; then
	START_TIME=$(date +%s)
fi

if [ -f "${LDK_BOOTLOADER_DIR}/extlinux.conf" ]; then
	echo "Installing extlinux.conf into /boot/extlinux in target rootfs"
	mkdir -p "${LDK_ROOTFS_DIR}/boot/extlinux/"
	install ${INSTALL_ROOT_OPTS} --mode=644 -D "${LDK_BOOTLOADER_DIR}/extlinux.conf" "${LDK_ROOTFS_DIR}/boot/extlinux/"
fi

TAR_ROOTFS_DIR_OPTS="--keep-directory-symlink -I lbzip2 -xpmf"

if [ "${TARGET_OVERLAY}" != "true" ] &&
	[ "${USE_TARGET_OVERLAY_DEFAULT}" != "true" ]; then
	if [ ! -f "${LDK_NV_TEGRA_DIR}/${DEB_SCRIPT_NAME}" ]; then
		echo "Debian script ${DEB_SCRIPT_NAME} not found"
		exit 1
	fi
	if [ "${FACTORY}" == "true" ]; then
		SUB_OPTIONS="--factory"
	fi
	if [ "${DGPU}" == "true" ]; then
		SUB_OPTIONS="--dgpu"
	fi
	echo "${LDK_NV_TEGRA_DIR}/${DEB_SCRIPT_NAME}";
	eval "${LDK_NV_TEGRA_DIR}/${DEB_SCRIPT_NAME} -r ${LDK_ROOTFS_DIR}" ${SUB_OPTIONS};
else
	# --dgpu/--factory option is not supported with "tar" installation and it is
	# only applicable for "debian" installation.
	if [ "${DGPU}" == "true" ]; then
		echo "Error: --dgpu option is not supported with tar installation"
		exit 1
	fi
	if [ "${FACTORY}" == "true" ]; then
		echo "Error: --factory option is not supported with tar installation"
		exit 1
	fi
	# install standalone debian packages by extracting and dumping them
	# into the rootfs directly for .tbz2 install flow
	pushd "${LDK_TOOLS_DIR}" > /dev/null 2>&1
	debs=($(ls *.deb))
	for deb in "${debs[@]}"; do
		"${DEB_EXTRACTOR}" --dir="${LDK_ROOTFS_DIR}" "${deb}"
	done
	popd > /dev/null 2>&1

	AddSystemGroup gpio
	AddSystemGroup crypto
	AddSystemGroup trusty

	echo "Extracting the NVIDIA user space components to ${LDK_ROOTFS_DIR}"
	pushd "${LDK_ROOTFS_DIR}" > /dev/null 2>&1
	tar ${TAR_ROOTFS_DIR_OPTS} "${LDK_NV_TEGRA_DIR}/nvidia_drivers.tbz2"
	popd > /dev/null 2>&1

	echo "Extracting the BSP test tools to ${LDK_ROOTFS_DIR}"
	pushd "${LDK_ROOTFS_DIR}" > /dev/null 2>&1
	tar ${TAR_ROOTFS_DIR_OPTS} "${LDK_NV_TEGRA_DIR}/nv_tools.tbz2"
	popd > /dev/null 2>&1

	echo "Extracting the OP-TEE target files to ${LDK_ROOTFS_DIR}"
	pushd "${LDK_ROOTFS_DIR}" > /dev/null 2>&1
	tar ${TAR_ROOTFS_DIR_OPTS} "${LDK_NV_TEGRA_DIR}/nv_optee.tbz2"
	popd > /dev/null 2>&1

	echo "Extracting the NVIDIA gst test applications to ${LDK_ROOTFS_DIR}"
	pushd "${LDK_ROOTFS_DIR}" > /dev/null 2>&1
	tar ${TAR_ROOTFS_DIR_OPTS} "${LDK_NV_TEGRA_DIR}/nv_sample_apps/nvgstapps.tbz2"
	popd > /dev/null 2>&1

	echo "Extracting Weston to ${LDK_ROOTFS_DIR}"
	pushd "${LDK_ROOTFS_DIR}" > /dev/null 2>&1
	tar ${TAR_ROOTFS_DIR_OPTS} "${LDK_NV_TEGRA_DIR}/weston.tbz2"
	popd > /dev/null 2>&1

	echo "Extracting the configuration files for the supplied root filesystem to ${LDK_ROOTFS_DIR}"
	pushd "${LDK_ROOTFS_DIR}" > /dev/null 2>&1
	tar ${TAR_ROOTFS_DIR_OPTS} "${LDK_NV_TEGRA_DIR}/config.tbz2"
	popd > /dev/null 2>&1

	echo "Extracting graphics_demos to ${LDK_ROOTFS_DIR}"
	pushd "${LDK_ROOTFS_DIR}" > /dev/null 2>&1
	tar ${TAR_ROOTFS_DIR_OPTS} "${LDK_NV_TEGRA_DIR}/graphics_demos.tbz2"
	popd > /dev/null 2>&1

	echo "Extracting the firmwares and kernel modules to ${LDK_ROOTFS_DIR}"
	( cd "${LDK_ROOTFS_DIR}" ; tar ${TAR_ROOTFS_DIR_OPTS} "${LDK_KERN_DIR}/kernel_supplements.tbz2" )

	if [ -f "${LDK_KERN_DIR}/kernel_display_supplements.tbz2" ]; then
		echo "Extracting display kernel modules to ${LDK_ROOTFS_DIR}"
		( cd "${LDK_ROOTFS_DIR}" ; tar ${TAR_ROOTFS_DIR_OPTS} "${LDK_KERN_DIR}/kernel_display_supplements.tbz2" )
	fi

	echo "Extracting the kernel headers to ${LDK_ROOTFS_DIR}/usr/src"
	# The kernel headers package can be used on the target device as well as on another host.
	# When used on the target, it should go into /usr/src and owned by root.
	# Note that there are multiple linux-headers-* directories; one for use on an
	# x86-64 Linux host and one for use on the L4T target.
	EXTMOD_DIR=ubuntu20.04_aarch64
	KERNEL_HEADERS_A64_DIR="$(tar tf "${LDK_KERN_DIR}/kernel_headers.tbz2" | grep "${EXTMOD_DIR}" | tail -1 | cut -d/ -f1)"
	KERNEL_VERSION="$(echo "${KERNEL_HEADERS_A64_DIR}" | sed -e "s/linux-headers-//" -e "s/-${EXTMOD_DIR}//")"
	KERNEL_SUBDIR="kernel-$(echo "${KERNEL_VERSION}" | cut -d. -f1-2)"
	install ${INSTALL_ROOT_OPTS} -m 0755 -d "${LDK_ROOTFS_DIR}/usr/src"
	pushd "${LDK_ROOTFS_DIR}/usr/src" > /dev/null 2>&1
	# This tar is packaged for the host (all files 666, dirs 777) so that when
	# extracted on the host, the user's umask controls the permissions.
	# However, we're now installing it into the rootfs, and hence need to
	# explicitly set and use the umask to achieve the desired permissions.
	(umask 022 && tar -I lbzip2 --no-same-permissions -xmf "${LDK_KERN_DIR}/kernel_headers.tbz2")
	if [ "${ROOTLESS}" != "true" ]; then
		chown -R root:root linux-headers-*
	fi
	# Link to the kernel headers from /lib/modules/<version>/build
	if [ ! -d "${LDK_ROOTFS_DIR}/usr/src/${KERNEL_HEADERS_A64_DIR}/${KERNEL_SUBDIR}" ] && \
			[ -d "${LDK_ROOTFS_DIR}/usr/src/${KERNEL_HEADERS_A64_DIR}/stable" ]; then
		KERNEL_SUBDIR=stable
	fi
	KERNEL_MODULES_DIR="${LDK_ROOTFS_DIR}/lib/modules/${KERNEL_VERSION}"
	if [ -d "${KERNEL_MODULES_DIR}" ]; then
		echo "Adding symlink ${KERNEL_MODULES_DIR}/build --> /usr/src/${KERNEL_HEADERS_A64_DIR}/${KERNEL_SUBDIR}"
		[ -h "${KERNEL_MODULES_DIR}/build" ] && unlink "${KERNEL_MODULES_DIR}/build" && rm -f "${KERNEL_MODULES_DIR}/build"
		[ ! -h "${KERNEL_MODULES_DIR}/build" ] && ln -s "/usr/src/${KERNEL_HEADERS_A64_DIR}/${KERNEL_SUBDIR}" "${KERNEL_MODULES_DIR}/build"
	fi
	popd > /dev/null

	# Copy kernel related files to rootfs
	"${LDK_DIR}/nv_tools/scripts/nv_apply_kernel_files.sh" "${LDK_KERN_DIR}" \
		"${LDK_ROOTFS_DIR}" "${INSTALL_ROOT_OPTS}"
fi

# Customize rootfs
"${LDK_DIR}/nv_tools/scripts/nv_customize_rootfs.sh" "${LDK_ROOTFS_DIR}"

if [ "${DEBUG}" == "true" ]; then
	END_TIME=$(date +%s)
	TOTAL_TIME=$((${END_TIME}-${START_TIME}))
	echo "Time for applying binaries - $(date -d@${TOTAL_TIME} -u +%H:%M:%S)"
fi
echo "Success!"
