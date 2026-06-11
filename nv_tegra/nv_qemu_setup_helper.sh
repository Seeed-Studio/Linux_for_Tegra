#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

# This script is used to setup QEMU for the L4T BSP.
# It is called by the nv-apply-debs.sh script.

set -e

# Define QEMU binary name globally
qemu_bin_name="qemu-aarch64-static"

function qemu_setup_rootfs() {
	local rootfs_dir="$1"
	if [ -z "${rootfs_dir}" ]; then
		echo "ERROR: rootfs_dir is not set"
		return 1
	fi
	if [ ! -d "${rootfs_dir}" ]; then
		echo "ERROR: ${rootfs_dir} does not exist"
		return 1
	fi
	# Declare script_dir first
	local script_dir
	script_dir="$(dirname "$(realpath "${0}")")"
	local qemu_bin_search_paths=()
	local qemu_binary
	qemu_binary=""
	qemu_bin_search_paths+=("${script_dir}/../..")
	qemu_bin_search_paths+=("${script_dir}")
	qemu_bin_search_paths+=("/usr/bin")
	for qemu_bin_search_path in "${qemu_bin_search_paths[@]}"; do
		if [ -f "${qemu_bin_search_path}/${qemu_bin_name}" ]; then
			echo "Found ${qemu_bin_name} in ${qemu_bin_search_path}"
			qemu_binary="${qemu_bin_search_path}/${qemu_bin_name}"
			break
		fi
	done
	if [ -z "${qemu_binary}" ]; then
		echo "ERROR: ${qemu_bin_name} not found!"
		echo "To install - please run: 'sudo apt-get install qemu-user-static'"
		exit 1
	fi
	echo "Installing QEMU binary in ${rootfs_dir}"
	install --owner=root --group=root "${qemu_binary}" "${rootfs_dir}/usr/bin/"
	mknod -m 444 "${rootfs_dir}/dev/random" c 1 8
	mknod -m 444 "${rootfs_dir}/dev/urandom" c 1 9

	# The qemu-aarch64-static whose version >= 8.1.1 has regression when running
	# ldconfig. Don't run ldconfig to workaround the issue. We will run
	# ldconfig in ldconfig.service anyway.
	# See https://gitlab.com/qemu-project/qemu/-/issues/1913
	# When running this script inside container we need to check QEMU version
	# in the host to decide if we should apply the workaround or not.
	# The environment variable HOST_QEMU_VERSION should be passed to container
	# from host.
	if [ -z "${HOST_QEMU_VERSION}" ]; then
		QEMU_VERSION="$(qemu-aarch64-static --version | grep "qemu-aarch64 version" | cut -d' ' -f3)"
	else
		echo "Environment variable HOST_QEMU_VERSION is set to ${HOST_QEMU_VERSION}"
		QEMU_VERSION="${HOST_QEMU_VERSION}"
	fi
	echo "Host qemu-aarch64-static version: ${QEMU_VERSION}"
	if dpkg --compare-versions "${QEMU_VERSION}" "ge-nl" "8.1.1"; then
		echo "Skip ldconfig because this version of QEMU suffers from a known issue:"
		echo "https://gitlab.com/qemu-project/qemu/-/issues/1913"
		if [ -f "${rootfs_dir}/var/lib/dpkg/triggers/ldconfig" ]; then
			mv "${rootfs_dir}/var/lib/dpkg/triggers/ldconfig" \
				"${rootfs_dir}/var/lib/dpkg/triggers/ldconfig.backup"
		fi
	fi

	echo "QEMU setup done!"
}

function qemu_cleanup_rootfs() {
	local rootfs_dir="$1"
	if [ -z "${rootfs_dir}" ]; then
		echo "ERROR: rootfs_dir is not set"
		return 1
	fi
	if [ ! -d "${rootfs_dir}" ]; then
		echo "ERROR: ${rootfs_dir} does not exist"
		return 1
	fi
	# Use the globally defined qemu_bin_name
	if [ -f "${rootfs_dir}/usr/bin/${qemu_bin_name}" ]; then
		echo "Removing QEMU binary from ${rootfs_dir}"
		rm -f "${rootfs_dir}/usr/bin/${qemu_bin_name}"
	fi
	rm -f "${rootfs_dir}/dev/random"
	rm -f "${rootfs_dir}/dev/urandom"

	if [ -f "${rootfs_dir}/var/lib/dpkg/triggers/ldconfig.backup" ]; then
		echo "Restoring ldconfig trigger in ${rootfs_dir}"
		mv "${rootfs_dir}/var/lib/dpkg/triggers/ldconfig.backup" \
			"${rootfs_dir}/var/lib/dpkg/triggers/ldconfig"
	fi
	echo "QEMU cleanup done!"
}
