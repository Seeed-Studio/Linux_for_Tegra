#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
# This script updates the base initrd image Linux_for_Tegra/bootloader/l4t_initrd.img
# and rootfs/boot/initrd using the nv-update-initrd script.
#

set -e
set -o pipefail

function InitVar
{
	L4T_TOOLS_DIR=$(cd "$(dirname "$0")" && pwd);
	L4T_DIR=${L4T_TOOLS_DIR%/*}
	L4T_NV_TEGRA_DIR="${L4T_DIR}/nv_tegra"
	echo "Using L4T_DIR to ${L4T_DIR}"

	L4T_ROOTFS_DIR="${L4T_DIR}/rootfs"
	L4T_BOOTLOADER_DIR="${L4T_DIR}/bootloader"

	ROOTFS_INITRD="${L4T_ROOTFS_DIR}/boot/initrd"
	BASE_INITRD="${L4T_BOOTLOADER_DIR}/l4t_initrd.img"

	ROOTFS_RESOLV_CONF="${L4T_ROOTFS_DIR}/etc/resolv.conf"
	ROOTFS_RESOLV_CONF_SAVED="${L4T_ROOTFS_DIR}/etc/resolv.conf.saved"
	QEMU_BIN=""
	ROOTFS_QEMU_BINARY="${L4T_ROOTFS_DIR}/usr/bin/qemu-aarch64-static"

	LSB_RELEASE_PATH="${L4T_ROOTFS_DIR}/etc/lsb-release"
	if [ ! -f "${LSB_RELEASE_PATH}" ]; then
		echo "ERROR: ${L4T_ROOTFS_DIR} is not populated."
		exit 1
	fi
	echo "Using rootfs directory: ${L4T_ROOTFS_DIR}"

	if [ ! -f "${BASE_INITRD}" ]; then
		echo "ERROR: ${BASE_INITRD} does not exist!"
		exit 1
	fi
}

function LookupQEMU
{
	# 1) Try host-installed QEMU first
	QEMU_BIN=$(command -v qemu-aarch64-static || true)
	if [ -n "${QEMU_BIN}" ]; then
		return 0
	fi

	# 2) Try the one in nv_tegra directory
	if [ -f "${L4T_NV_TEGRA_DIR}/qemu-aarch64-static" ]; then
		QEMU_BIN="${L4T_NV_TEGRA_DIR}/qemu-aarch64-static"
		return 0
	fi

	echo "ERROR: QEMU not found! Please install it, for example:" \
		"\"Ubuntu/Debian: sudo apt-get install qemu-user-static\""
	exit 1
}

#
# Setup target env
#
function PrepareVirEnv
{
	echo "Preparing virtual env"
	cp "${QEMU_BIN}" "${ROOTFS_QEMU_BINARY}"
	mv "${ROOTFS_RESOLV_CONF}" "${ROOTFS_RESOLV_CONF_SAVED}"
	cp /etc/resolv.conf "${ROOTFS_RESOLV_CONF}"
}

#
# Cleanup target env
#
function CleanupVirEnv
{
	echo "Cleaning up virtual env"

# The /etc/resolv.conf (and therefore /etc/resolv.conf.saved) on the target
# rootfs can be a symlink. Moreover, the symlink can point to an absolute path,
# in which case, when resolved from outside of the context of the target rootfs
# (i.e. the host), may or may not be valid. In fact, we should assume that
# an absolute path symlink is invalid on contexts outside of the target rootfs.
#
# When the symlink is invalid (doesn't point to something that exists), the
# bash -f test will fail...even though the symlink actually exists.
#
# Therefore, we need to check if the file is a valid file OR a symlink, and
# in the (likely) case that it does, move it, to successfully restore the
# original resolv.conf here.
	if [ -f "${ROOTFS_RESOLV_CONF_SAVED}" ] ||
	   [ -L "${ROOTFS_RESOLV_CONF_SAVED}" ]; then
		mv "${ROOTFS_RESOLV_CONF_SAVED}" "${ROOTFS_RESOLV_CONF}"
	fi
	rm -f "${ROOTFS_QEMU_BINARY}"
}

function CopyBaseInitrdToRootfs
{
	echo "Copy ${BASE_INITRD} to ${ROOTFS_INITRD}"
	cp -f "${BASE_INITRD}" "${ROOTFS_INITRD}"
}

function UpdateBackToBaseInitrd
{
	echo "Update ${ROOTFS_INITRD} back to ${BASE_INITRD}"
	cp -f "${ROOTFS_INITRD}" "${BASE_INITRD}"
}

function UpdateInitrd
{
	local cmd_args=${1:-}

	# Update the initrd in the rootfs.
	trap CleanupVirEnv EXIT
	PrepareVirEnv
	cmd="LC_ALL=C chroot \"${L4T_ROOTFS_DIR}\" nv-update-initrd ${cmd_args}"
	if ! eval "$cmd"; then
		echo "ERROR: nv-update-initrd failed!"
		exit 1
	else
		echo "nv-update-initrd successful!"
		CleanupVirEnv
		trap - EXIT
	fi
}

function ShowUsage
{
	echo "Usage: ${SCRIPT_NAME} [--help|-h] [--list-files|-f]"
cat <<EOF

This script updates the base initrd image and rootfs initrd image

	Options are:
		--list-files|-f <list files>
			The group of list files to inject into the initrd image
		--help|-h
			show this help

	Examples:
		1. Use default list files to update the initrd
			${SCRIPT_NAME}
		2. Use the specified list files
			${SCRIPT_NAME} --list-files "modules_common,modules_k6.8"
EOF
}

SCRIPT_NAME=$(basename "$(readlink -f "$0")")

TGETOPT=$(getopt -n "${SCRIPT_NAME}" --longoptions list-files:,help -o f:,h -- "$@")

eval set -- "$TGETOPT"

while [ $# -gt 0 ]; do
	case "$1" in
	-f|--list-files) lists_group="$2"; shift 2 ;;
	-h|--help) ShowUsage; exit 0 ;;
	--) shift; break ;;
	*) echo "Unknown option: $1" >&2 ; ShowUsage; exit 0 ;;
	esac
	shift
done

update_initrd_args=""
if [ -n "${lists_group}" ]; then
	update_initrd_args="--list-files ${lists_group}"
fi

InitVar
LookupQEMU

echo "Updating the initrd: ${BASE_INITRD}"
CopyBaseInitrdToRootfs
UpdateInitrd "${update_initrd_args}"
UpdateBackToBaseInitrd
echo "l4t_update_initrd.sh success!"
