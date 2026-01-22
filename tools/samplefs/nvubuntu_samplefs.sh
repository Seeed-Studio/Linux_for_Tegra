#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2019-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

base_url=""
qemu_abi="aarch64"
qemu_name="qemu-${qemu_abi}-static"
target_qemu_path="usr/bin/${qemu_name}"
host_qemu_path="/usr/bin/${qemu_name}"

function check_pre_req_distro()
{
	if [ "${abi}" = "arm" ] || [ "${abi}" = "armhf" ]; then
		qemu_abi="arm"
	fi

	samplefs_data_file="${script_dir}/ubuntu/${version}/nvubuntu-${version}-${abi}-samplefs"
	if [ ! -f "${samplefs_data_file}" ]; then
		samplefs_data_file="${script_dir}/nvubuntu-${version}-${abi}-samplefs"
		if [ ! -f "${samplefs_data_file}" ]; then
			echo "ERROR: samplefs file - ${samplefs_data_file} not found" > /dev/stderr
			exit 1
		fi
	fi
	source "${samplefs_data_file}"
	base_url="${BASE_URL}"

	package_list_file="${script_dir}/ubuntu/${version}/nvubuntu-${version}-${flavor}-${abi}-packages"
	if [ ! -f "${package_list_file}" ]; then
		package_list_file="${script_dir}/nvubuntu-${version}-${flavor}-${abi}-packages"
		if [ ! -f "${package_list_file}" ]; then
			echo "ERROR: package list file - ${package_list_file} not found" > /dev/stderr
			exit 1
		fi
	fi

	if [ ! -f "${host_qemu_path}" ]; then
		echo "ERROR: qemu not found. Please run \"sudo apt-get install qemu-user-static\" on your machine" > /dev/stderr
		exit 1
	fi
}

function create_samplefs()
{
	echo "${script_name} - create_samplefs"

	if [ ! -e "${tmpdir}" ]; then
		echo "ERROR: Temporary directory not found" > /dev/srderr
		exit 1
	fi

	pushd "${tmpdir}" > /dev/null 2>&1
	cp "${host_qemu_path}" "${target_qemu_path}"
	chmod 755 "${target_qemu_path}"

	mount /sys ./sys -o bind
	mount /proc ./proc -o bind
	mount /dev ./dev -o bind
	mount /dev/pts ./dev/pts -o bind

	if [ -s etc/resolv.conf ]; then
		sudo mv etc/resolv.conf etc/resolv.conf.saved
	fi
	if [ -e "/run/resolvconf/resolv.conf" ]; then
		sudo cp /run/resolvconf/resolv.conf etc/
	elif [ -e "/etc/resolv.conf" ]; then
		sudo cp /etc/resolv.conf etc/
	fi
	sudo LC_ALL=C chroot . apt-get update

	package_list=$(cat "${package_list_file}")
	if [ ! -z "${package_list}" ]; then
		sudo LC_ALL=C DEBIAN_FRONTEND=noninteractive chroot . apt-get -y --no-install-recommends install ${package_list}
	else
		echo "ERROR: Package list is empty"
	fi

	sudo LC_ALL=C chroot . sync
	sudo LC_ALL=C chroot . apt-get clean
	sudo LC_ALL=C chroot . sync

	if [ -s etc/resolv.conf.saved ]; then
		mv etc/resolv.conf.saved etc/resolv.conf
	else
		if [ -s run/systemd/resolve/stub-resolv.conf ]; then
			rm -f etc/resolv.conf
			ln -s "/run/systemd/resolve/stub-resolv.conf" "etc/resolv.conf"
		fi
	fi

	umount ./sys
	umount ./proc
	umount ./dev/pts
	umount ./dev

	rm "${target_qemu_path}"

	if [ -e etc/machine-id ]; then
		rm -f etc/machine-id
	fi
	if [ -e var/lib/dbus/machine-id ]; then
		rm -f var/lib/dbus/machine-id
	fi

	find var/log -type f -exec rm -f {} \;
	find var/log -type l -exec rm -f {} \;

	rm -rf var/lib/apt/lists/*
	rm -rf dev/*
	rm -rf var/cache/apt/archives/*.deb
	rm -rf var/tmp/*
	rm -rf tmp/*

	popd > /dev/null
}
