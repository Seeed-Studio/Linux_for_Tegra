#!/bin/bash

# Copyright (c) 2019-2022, NVIDIA CORPORATION. All rights reserved.
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

function install_package()
{
	retry=0
	retry_max=5

	echo "Install ${1}"
	while true
	do
		ret=0
		sudo LC_ALL=C DEBIAN_FRONTEND=noninteractive chroot . apt-get -y --no-install-recommends --allow-downgrades install "${1}" || ret=$?
		if [ "${ret}" == "0" ]; then
			return 0
		else
			retry=$( expr $retry + 1 )
			if [ "${retry}" == "${retry_max}" ]; then
				return 1
			else
				sleep 1
				echo "Retrying ${1} package install"
			fi
		fi
	done
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
		for package in ${package_list}
		do
			if ! install_package "${package}"; then
				package_name="$(echo "${package}" | cut -d'=' -f1)"
				if [ "${package_name}" = "${package}" ]; then
					echo "ERROR: Failed to install ${package}"
				else
					echo "Try to install ${package_name} the latest version"
					if ! install_package "${package_name}"; then
						echo "ERROR: Failed to install ${package_name}"
					fi
				fi
			fi
		done
	fi

	sudo LC_ALL=C chroot . sync
	sudo LC_ALL=C chroot . apt-get clean
	sudo LC_ALL=C chroot . sync

	if [ -s etc/resolv.conf.saved ]; then
		sudo mv etc/resolv.conf.saved etc/resolv.conf
	fi

	umount ./sys
	umount ./proc
	umount ./dev/pts
	umount ./dev

	rm "${target_qemu_path}"

	rm -rf var/lib/apt/lists/*
	rm -rf dev/*
	rm -rf var/log/*
	rm -rf var/cache/apt/archives/*.deb
	rm -rf var/tmp/*
	rm -rf tmp/*

	popd > /dev/null
}
