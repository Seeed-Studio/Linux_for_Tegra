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

# This is a script to generate the sample filesystem

set -e

abi=""
distro=""
version=""
verbose=false
source_samplefs=""
script_name="$(basename "${0}")"
script_path="$(readlink -f "${0}")"
script_dir="$(dirname "${script_path}")"
base_tarball="${script_dir}/base.tar.gz"
output_samplefs="${script_dir}/sample_fs.tbz2"
tmpdir=""

function usage()
{
	if [ -n "${1}" ]; then
		echo "${1}"
	fi

	echo "Usage:"
	echo "${script_name} --abi <ABI> --distro <distro> --flavor <flavor> --version <version> [--verbose]"
	echo "	<ABI> 		- The ABI of Linux distro. Such as 'aarch64'"
	echo "	<distro>	- The Linux distro. Such as 'ubuntu'"
	echo "	<flavor>	- The flavor of samplefs. Such as 'desktop'"
	echo "	<version>	- The version of Linux distro. Such as 'focal' for Ubuntu."
	echo "Example:"
	echo "${script_name} --abi aarch64 --distro ubuntu --flavor desktop --version focal"
	echo ""
	echo "${script_name} will download the base image for given Linux distro, install necessary"
	echo "packages, and generate samplefs tarball, so an internet connection is required."
	echo ""
	echo "Generated samplefs tarball will be named 'sample_fs.tbz2' and put under the path"
	echo "executes this script."
	echo ""
	echo "Note: ${script_name} can only run on Ubuntu 20.04."
	exit 1
}

function cleanup() {
	echo "${script_name} - cleanup"
	set +e

	if [ -n "${tmpdir}" ]; then
		for attempt in $(seq 10); do
			mount | grep -q "${tmpdir}/sys" && umount ./sys
			mount | grep -q "${tmpdir}/proc" && umount ./proc
			mount | grep -q "${tmpdir}/dev/pts" && umount ./dev/pts
			mount | grep -q "${tmpdir}/dev" && umount ./dev
			mount | grep -q "${tmpdir}"
			if [ $? -ne 0 ]; then
				break
			fi
			sleep 1
		done

		rm -rf "${tmpdir}"
	fi

	if [ -f "${base_tarball}" ]; then
		rm "${base_tarball}"
	fi
}
trap cleanup EXIT

function check_pre_req()
{
	ubuntu_codename="$(cat /etc/lsb-release | grep CODENAME)"
	if [[ ! "${ubuntu_codename}" =~ "focal" ]]; then
		echo "ERROR: This script can be only run on Ubuntu 20.04" > /dev/stderr
		usage
		exit 1
	fi
	ubuntu_arch="$(arch | grep "x86_64")"
	if [ "${ubuntu_arch}" == "" ]; then
		echo "ERROR: This script can be only run on x86-64 system" > /dev/stderr
		usage
		exit 1
	fi

	this_user="$(whoami)"
	if [ "${this_user}" != "root" ]; then
		echo "ERROR: This script requires root privilege" > /dev/stderr
		usage
		exit 1
	fi

	while [ -n "${1}" ]; do
		case "${1}" in
		--help)
			usage
			;;
		--abi)
			abi="${2}"
			shift 2
			;;
		--distro)
			distro="${2}"
			shift 2
			;;
		--flavor)
			flavor="${2}"
			shift 2
			;;
		--version)
			version="${2}"
			shift 2
			;;
		--verbose)
			verbose=true
			shift 1
			;;
		*)
			usage "Unknown option: ${1}"
			;;
		esac
	done

	if [ -z "${abi}" ] || [ -z "${distro}" ] || [ -z "${flavor}" ] || [ -z "${version}" ]; then
		usage
	fi

	distro_script="${script_dir}/${distro}/nv${distro}_samplefs.sh"
	if [ ! -f "${distro_script}" ]; then
		distro_script="${script_dir}/nv${distro}_samplefs.sh"
		if [ ! -f "${distro_script}" ]; then
			echo "ERROR: distro script - ${distro_script} not found" > /dev/stderr
			exit 1
		fi
	fi

	source "${distro_script}"
	check_pre_req_distro
}

function download_samplefs()
{
	echo "${script_name} - download_samplefs"

	validate_url="$(wget -S --spider "${base_url}" 2>&1 | grep "HTTP/1.1 200 OK" || ret=$?)"
	if [ -z "${validate_url}" ]; then
		echo "ERROR: Cannot download base image, please check internet connection first" > /dev/stderr
		exit 1
	fi

	wget -O "${base_tarball}" "${base_url}" > /dev/null 2>&1
	source_samplefs="${base_tarball}"
}

function extract_samplefs()
{
	echo "${script_name} - extract_samplefs"
	tmpdir="$(mktemp -d)"
	chmod 755 "${tmpdir}"
	pushd "${tmpdir}" > /dev/null 2>&1
	tar xpf "${source_samplefs}" --numeric-owner
	popd > /dev/null
}

function save_samplefs()
{
	echo "${script_name} - save_samplefs"

	pushd "${tmpdir}" > /dev/null 2>&1
	sudo tar --numeric-owner -jcpf "${output_samplefs}" *
	sync
	popd > /dev/null
	rm -rf "${tmpdir}"
	tmpdir=""
}

if [ "${verbose}" = true ]; then
	start_time=$(date +%s)
fi

check_pre_req "${@}"

echo "********************************************"
echo "     Create ${distro} sample filesystem     "
echo "********************************************"

if [ ! -f "${source_samplefs}" ]; then
	download_samplefs
fi
extract_samplefs
create_samplefs
save_samplefs

if [ "${verbose}" = true ]; then
	end_time=$(date +%s)
	total_time=$((end_time-start_time))

	echo "********************************************"
	echo "       Execution time Information           "
	echo "********************************************"
	echo "${script_name} : End time - $(date)"
	echo "${script_name} : Total time - $(date -d@${total_time} -u +%H:%M:%S)"
fi

echo "********************************************"
echo "   ${distro} samplefs Creation Complete     "
echo "********************************************"
echo "Samplefs - ${output_samplefs} was generated."
