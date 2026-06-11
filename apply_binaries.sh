#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2011-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
# This script applies the binaries to the rootfs dir pointed to by
# LDK_ROOTFS_DIR variable.
#

set -e
set -o pipefail

# show the usages text
function ShowUsage {
    local ScriptName=$1

    echo "Use: $1 [--bsp|-b PATH] [--root|-r PATH] [--target-overlay] [--openrm] [--help|-h]"
cat <<EOF
    This script installs tegra binaries
    Options are:
    --bsp|-b PATH
                   bsp location (bsp, readme, installer)
    --dgpu
                   only install packages that are suitable for dGPU use
    --factory
                   only install packages that are suitable for factory use
    --openrm
                   installs operm package
    --root|-r PATH
                   install toolchain to PATH
    --rootless
                   don't require root privilege
    --rootfs-tar
                   path to the rootfs tarball
    --target-overlay|-t
                   untar NVIDIA target overlay (.tbz2) instead of
				   pre-installing them as Debian packages
    --extra-pkgs-dir PATH
                   install additional package files (.deb, .run) from DIRECTORY
    --extra-pkg PATH
                   install additional package file (.deb or .run) from PATH (can be repeated)
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

function ExtractRootfs {
	if [ -n "${ROOTFS_TAR}" ]; then
		if [ ! -f "${ROOTFS_TAR}" ]; then
			echo "Rootfs tarball ${ROOTFS_TAR} not found. Exiting."
			exit 1
		fi
		# Check if the directory is exported for mount
		if exportfs -v | grep -q "^${LDK_ROOTFS_DIR}$"; then
			echo "Unexporting ${LDK_ROOTFS_DIR} ..."
			sudo exportfs -u "*:${LDK_ROOTFS_DIR}"
		fi
		echo "Removing ${LDK_ROOTFS_DIR} ..."
		sudo rm -rf "${LDK_ROOTFS_DIR}"
		sudo mkdir -p "${LDK_ROOTFS_DIR}"
		echo "Extracting ${ROOTFS_TAR} to ${LDK_ROOTFS_DIR} ..."
		# TAR_ROOTFS_DIR_OPTS should not be quoted as it contains multiple options
		# and we want to pass it to tar as options
		sudo tar ${TAR_ROOTFS_DIR_OPTS} "${ROOTFS_TAR}" -C "${LDK_ROOTFS_DIR}"
	else
		echo "No rootfs tarball provided."
		echo "Please provide a rootfs tarball using --rootfs-tar parameter."
		exit 1
	fi
}

function InstallExtraPackages {
	# Install additional .deb and .run packages into the target rootfs using QEMU.
	# This works for both debian and tar flows since it operates directly on
	# ${LDK_ROOTFS_DIR}.

	# Collect extra package paths from directory and explicit arguments
	local extra_pkgs=()
	local pkg

	if [ -n "${EXTRA_PKGS_DIR}" ]; then
		shopt -s nullglob
		for pkg in "${EXTRA_PKGS_DIR}"/*.deb "${EXTRA_PKGS_DIR}"/*.run; do
			[ -e "${pkg}" ] || continue
			extra_pkgs+=("${pkg}")
		done
		shopt -u nullglob
	fi

	if [ "${#EXTRA_PKG_FILES[@]}" -ne 0 ]; then
		for pkg in "${EXTRA_PKG_FILES[@]}"; do
			case "${pkg}" in
				*.deb|*.run)
					extra_pkgs+=("${pkg}")
					;;
				*)
					echo "WARNING: Ignoring extra package '${pkg}' (unsupported extension)"
					;;
			esac
		done
	fi

	if [ "${#extra_pkgs[@]}" -eq 0 ]; then
		return
	fi

	# QEMU helper must be available to chroot into the target rootfs
	local qemu_helper="${LDK_NV_TEGRA_DIR}/nv_qemu_setup_helper.sh"
	if [ ! -f "${qemu_helper}" ]; then
		echo "WARNING: QEMU helper '${qemu_helper}' not found; skipping extra package installation"
		return
	fi
	source "${qemu_helper}"
	if ! declare -f qemu_setup_rootfs > /dev/null || \
	   ! declare -f qemu_cleanup_rootfs > /dev/null; then
		echo "WARNING: qemu_setup_rootfs/qemu_cleanup_rootfs not available; skipping extra package installation"
		return
	fi

	local target_dir="/opt/nvidia/extra-packages"
	local extra_debs_in_rootfs=()
	local extra_runs_in_rootfs=()

	install ${INSTALL_ROOT_OPTS} -m 0755 -d "${LDK_ROOTFS_DIR}/${target_dir}"

	for pkg in "${extra_pkgs[@]}"; do
		if [ ! -f "${pkg}" ]; then
			echo "WARNING: Extra package '${pkg}' not found, skipping"
			continue
		fi
		local name
		name=$(basename "${pkg}")
		echo "Staging extra package ${name} into ${LDK_ROOTFS_DIR}/${target_dir}"
		cp "${pkg}" "${LDK_ROOTFS_DIR}/${target_dir}/"
		case "${name}" in
			*.deb)
				extra_debs_in_rootfs+=("${target_dir}/${name}")
				;;
			*.run)
				chmod +x "${LDK_ROOTFS_DIR}/${target_dir}/${name}"
				extra_runs_in_rootfs+=("${target_dir}/${name}")
				;;
		esac
	done

	if [ "${#extra_debs_in_rootfs[@]}" -eq 0 ] && [ "${#extra_runs_in_rootfs[@]}" -eq 0 ]; then
		return
	fi

	echo "Setting up QEMU in rootfs for extra package installation"
	qemu_setup_rootfs "${LDK_ROOTFS_DIR}"
	# Ensure QEMU cleanup is run on script exit, even if an error occurs later
	trap ExtraPackagesCleanup EXIT

	pushd "${LDK_ROOTFS_DIR}" > /dev/null 2>&1

	if [ "${#extra_debs_in_rootfs[@]}" -ne 0 ]; then
		echo "Installing extra .deb packages in ${LDK_ROOTFS_DIR}"
		LC_ALL=C PYTHONHASHSEED=0 chroot . dpkg -i --path-include="/usr/share/doc/*" "${extra_debs_in_rootfs[@]}"
	fi

	if [ "${#extra_runs_in_rootfs[@]}" -ne 0 ]; then
		echo "Running extra .run installers in ${LDK_ROOTFS_DIR}"
		for installer in "${extra_runs_in_rootfs[@]}"; do
			echo "Executing ${installer} in chroot"
			LC_ALL=C PYTHONHASHSEED=0 chroot . "${installer}"
		done
	fi

	popd > /dev/null 2>&1
}

function ExtraPackagesCleanup {
	# Clean up QEMU setup in rootfs if the helper is available
	if declare -f qemu_cleanup_rootfs > /dev/null; then
		echo "Cleaning up QEMU in rootfs after extra package installation"
		qemu_cleanup_rootfs "${LDK_ROOTFS_DIR}" || true
	fi
}

# script name
SCRIPT_NAME=$(basename $0)

# apply .deb script name
DEB_SCRIPT_NAME="nv-apply-debs.sh"

# empty root and no debug
DEBUG=
OPENRM="false"
EXTRA_PKGS_DIR=
EXTRA_PKG_FILES=()

# parse the command line first
TGETOPT=$(getopt -n "$SCRIPT_NAME" --longoptions help,bsp:,debug,dgpu,factory,openrm,target-overlay,root:,rootless,rootfs-tar:,extra-pkgs-dir:,extra-pkg: -o b:r:dht -- "$@")

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
	--openrm) OPENRM="true" ;;
	-r|--root) LDK_ROOTFS_DIR="$2"; shift ;;
	-h|--help) ShowUsage "$SCRIPT_NAME"; exit 1 ;;
	-d|--debug) DEBUG="true" ;;
	-t|--target-overlay) TARGET_OVERLAY="true" ;;
	-b|--bsp) BSP_LOCATION_DIR="$2"; shift ;;
	--rootless) ROOTLESS="true" ;;
	--rootfs-tar) ROOTFS_TAR="$2"; shift ;;
	--extra-pkgs-dir) EXTRA_PKGS_DIR="$2"; shift ;;
	--extra-pkg) EXTRA_PKG_FILES+=("$2"); shift ;;
	--) shift; break ;;
	--*)
		echo "Terminating... wrong switch: $@" >&2
		ShowUsage "$SCRIPT_NAME"
		exit 1
		;;
	-*) echo "Terminating... wrong switch: $@" >&2 ; ShowUsage "$SCRIPT_NAME"; exit 1 ;;
    esac
    shift
done

if [ $# -gt 0 ]; then
    ShowUsage "$SCRIPT_NAME"
    exit 1
fi

# --dgpu and --openrm are mutually exclusive
if [ "${DGPU}" == "true" ] && [ "${OPENRM}" == "true" ]; then
    echo "ERROR: --dgpu and --openrm are mutually exclusive; use only one."
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
LDK_DIR=$(cd $(dirname $0) && pwd)

# use default rootfs dir if none is set
if [ -z "$LDK_ROOTFS_DIR" ]; then
    LDK_ROOTFS_DIR="${LDK_DIR}/rootfs"
fi

echo "Using rootfs directory of: ${LDK_ROOTFS_DIR}"

# get the absolute path, for LDK_ROOTFS_DIR.
# otherwise, tar behaviour is unknown
if [ -n "${LDK_ROOTFS_DIR}" ]; then
    LDK_ROOTFS_DIR="$(realpath "${LDK_ROOTFS_DIR}")"
fi

TAR_ROOTFS_DIR_OPTS="--keep-directory-symlink -I lbzip2 -xpmf"

# Extract rootfs if tarball provided and rootfs not yet present
if [ -n "${ROOTFS_TAR}" ] && [ ! -f "${LDK_ROOTFS_DIR}/etc/passwd" ]; then
	echo "Extracting rootfs from ${ROOTFS_TAR}..."
	ExtractRootfs
fi
# else assume that the rootfs is already extracted

# Check if rootfs is extracted
if [ ! $(find "$LDK_ROOTFS_DIR/etc/passwd" ${FIND_ROOT_OPTS}) ]; then
	echo "||||||||||||||||||||||| ERROR |||||||||||||||||||||||"
	echo "-----------------------------------------------------"
	echo "1. The root filesystem, provided with this package,"
	echo "   has to be extracted to this directory:"
	echo "   ${LDK_ROOTFS_DIR}"
	echo "-----------------------------------------------------"
	echo "2. The root filesystem, provided with this package,"
	echo "   has to be extracted with 'sudo' to this directory:"
	echo "   ${LDK_ROOTFS_DIR}"
	echo "3. Or you can re-run this script with"
	echo "	--rootfs-tar <path_to_rootfs_tarball> parameter."
	echo "-----------------------------------------------------"
	echo "Consult the Development Guide for instructions on"
	echo "extracting and flashing your device."
	echo "|||||||||||||||||||||||||||||||||||||||||||||||||||||"
	exit 1
fi

install ${INSTALL_ROOT_OPTS} -m 0755 -d "${LDK_ROOTFS_DIR}"

# assumption: this script is part of the BSP
#             so, LDK_DIR/nv_tegra always exist
LDK_NV_TEGRA_DIR="${LDK_DIR}/nv_tegra"
LDK_KERN_DIR="${LDK_DIR}/kernel"
LDK_TOOLS_DIR="${LDK_DIR}/tools"
LDK_NV_TOOLS_DIR="${LDK_DIR}/nv_tools"
LDK_NV_SCRIPTS_DIR="${LDK_NV_TOOLS_DIR}/scripts"
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

if [ "${TARGET_OVERLAY}" != "true" ] ; then
	if [ ! -f "${LDK_NV_TEGRA_DIR}/${DEB_SCRIPT_NAME}" ]; then
		echo "Debian script ${DEB_SCRIPT_NAME} not found"
		exit 1
	fi
	SUB_OPTIONS=""
	if [ "${FACTORY}" == "true" ]; then
		SUB_OPTIONS="--factory"
	fi
	if [ "${DGPU}" == "true" ]; then
		SUB_OPTIONS="--dgpu"
	fi
	if [ "${OPENRM}" == "true" ]; then
		SUB_OPTIONS="${SUB_OPTIONS} --openrm"
	fi
	echo "${LDK_NV_TEGRA_DIR}/${DEB_SCRIPT_NAME}";
	# shell check disabled for double quotes as SUB_OPTIONS should not be quoted
	# as it contains multiple options separated by spaces
	eval "${LDK_NV_TEGRA_DIR}/${DEB_SCRIPT_NAME} -r ${LDK_ROOTFS_DIR}" ${SUB_OPTIONS};
else
	# --dgpu/--factory option is not supported with "tar" installation and it is
	# only applicable for "debian" installation.
	if [ "${DGPU}" == "true" ]; then
		echo "ERROR: --dgpu option is not supported with tar installation"
		exit 1
	fi
	if [ "${FACTORY}" == "true" ]; then
		echo "ERROR: --factory option is not supported with tar installation"
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

	# Get list of packages to install
	echo "Getting list of packages to install"
	PACKAGE_LIST_OPTS=
	if [ "${OPENRM}" == "true" ]; then
		PACKAGE_LIST_OPTS="--openrm"
	fi
	PACKAGE_LIST=$("${LDK_NV_SCRIPTS_DIR}/nv_l4t_get_package_install_list.sh" \
		--bsp "${LDK_DIR}" \
		--type tbz2 \
		${PACKAGE_LIST_OPTS})

	if [ -z "${PACKAGE_LIST}" ]; then
		echo "No packages found to install"
		exit 1
	fi

	# Process each package from the list
	echo "Extracting packages to ${LDK_ROOTFS_DIR}"
	while IFS= read -r package_path; do
		# Skip empty lines
		[ -z "${package_path}" ] && continue

		# Get package name
		pkg_name=$(basename "${package_path}")

		# Skip kernel headers packages as they are handled separately
		if [[ "${pkg_name}" == "kernel_headers.tbz2" ]] || \
			[[ "${pkg_name}" == "kernel_oot_headers.tbz2" ]]; then
			continue
		fi

		echo "Extracting ${pkg_name} to ${LDK_ROOTFS_DIR}"
		pushd "${LDK_ROOTFS_DIR}" > /dev/null 2>&1
		tar ${TAR_ROOTFS_DIR_OPTS} "${package_path}"
		popd > /dev/null 2>&1
	done <<< "${PACKAGE_LIST}"

	# Handle kernel headers separately
	echo "Extracting the kernel headers to ${LDK_ROOTFS_DIR}/usr/src"
	# The kernel headers package can be used on the target device as well as on another host.
	# When used on the target, it should go into /usr/src and owned by root.
	# Note that there are multiple linux-headers-* directories; one for use on an
	# x86-64 Linux host and one for use on the L4T target.
	EXTMOD_DIR="ubuntu24.04_aarch64|ubuntu22.04_aarch64"
	KERNEL_HEADERS_A64_DIR="$(tar tf "${LDK_KERN_DIR}/kernel_headers.tbz2" | grep -E "${EXTMOD_DIR}" | tail -1 | cut -d/ -f1)"
	KERNEL_VERSION="$(echo "${KERNEL_HEADERS_A64_DIR}" | sed -E "s/linux-headers-//; s/-(${EXTMOD_DIR})$//")"
	KERNEL_SUBDIR="kernel-$(echo "${KERNEL_VERSION}" | sed -e "s/-debug//" -e "s/-tegra//")"
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
	if [ ! -d "${LDK_ROOTFS_DIR}/usr/src/${KERNEL_HEADERS_A64_DIR}/${KERNEL_SUBDIR}" ]; then
		if [ -d "${LDK_ROOTFS_DIR}/usr/src/${KERNEL_HEADERS_A64_DIR}/3rdparty/canonical/linux-noble" ]; then
			KERNEL_SUBDIR="3rdparty/canonical/linux-noble"
		elif [ -d "${LDK_ROOTFS_DIR}/usr/src/${KERNEL_HEADERS_A64_DIR}/kernel-nvmainline" ]; then
			KERNEL_SUBDIR="kernel-nvmainline"
		elif [ -d "${LDK_ROOTFS_DIR}/usr/src/${KERNEL_HEADERS_A64_DIR}/kernel-oot" ]; then
			KERNEL_SUBDIR="kernel-oot"
		fi
	fi
	KERNEL_MODULES_DIR="${LDK_ROOTFS_DIR}/lib/modules/${KERNEL_VERSION}"
	if [ -d "${KERNEL_MODULES_DIR}" ]; then
		echo "Adding symlink ${KERNEL_MODULES_DIR}/build --> /usr/src/${KERNEL_HEADERS_A64_DIR}/${KERNEL_SUBDIR}"
		[ -h "${KERNEL_MODULES_DIR}/build" ] && unlink "${KERNEL_MODULES_DIR}/build" && rm -f "${KERNEL_MODULES_DIR}/build"
		[ ! -h "${KERNEL_MODULES_DIR}/build" ] && ln -s "/usr/src/${KERNEL_HEADERS_A64_DIR}/${KERNEL_SUBDIR}" "${KERNEL_MODULES_DIR}/build"
	fi
	popd > /dev/null

	if [ -f "${LDK_KERN_DIR}/kernel_oot_headers.tbz2" ];then
		echo "Extracting module headers to ${LDK_ROOTFS_DIR}/usr/src/nvidia"
		pushd "${LDK_ROOTFS_DIR}/usr/src/nvidia" > /dev/null 2>&1
		tar ${TAR_ROOTFS_DIR_OPTS} "${LDK_KERN_DIR}/kernel_oot_headers.tbz2"
		popd > /dev/null 2>&1
	fi

	# Copy kernel related files to rootfs
	"${LDK_DIR}/nv_tools/scripts/nv_apply_kernel_files.sh" "${LDK_KERN_DIR}" \
		"${LDK_ROOTFS_DIR}" "${INSTALL_ROOT_OPTS}"
fi

# Install any extra packages requested by the user (works for both deb and tar flows)
InstallExtraPackages

# Customize rootfs
"${LDK_DIR}/nv_tools/scripts/nv_customize_rootfs.sh" "${LDK_ROOTFS_DIR}"

if [ "${DEBUG}" == "true" ]; then
	END_TIME=$(date +%s)
	TOTAL_TIME=$((${END_TIME}-${START_TIME}))
	echo "Time for applying binaries - $(date -d@${TOTAL_TIME} -u +%H:%M:%S)"
fi

echo "Success!"
