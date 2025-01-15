#!/bin/bash

# Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#
# Individual Contributor License Agreement (CLA):
# https://gist.github.com/alex3165/0d70734579a542ad34495d346b2df6a5

#Overlay for flashing, calling nvautoflash and/or initrd flash for external media

# USAGE:

# sudo ./nvsdkmanager_flash.sh --storage <storage media>
# sudo ./nvsdkmanager_flash.sh --custom <user specified custom command>
# sudo ./nvsdkmanager_flash.sh --nv-auto-config
# sudo ./nvsdkmanager_flash.sh --nv-auto-config --username <username_of_new_account>
#
# If no argument given runs nvautoflash by default
# sudo ./nvsdkmanager_flash.sh

set -o pipefail;
set -o errtrace;
shopt -s extglob;
curdir=$(dirname "$0");
curdir=$(cd "${curdir}" && pwd);
initrd_path="tools/kernel_flash/l4t_initrd_flash.sh";
xml_config="tools/kernel_flash/flash_l4t_external.xml";
file=""

trap cleanup EXIT

cleanup()
{
	rm -f "${curdir}"/bootloader/cbo.dtb
	if [ -f "${file}" ]; then
		rm "${file}"
	fi
}

create_cbo()
{
	file="$(mktemp)"

	cat > "${file}"  <<EOF
/dts-v1/;

/ {
		compatible = "nvidia,cboot-options-v1";
		boot-configuration {
		boot-order = ${1}, "sd", "emmc", "net";
		};
};
EOF
	dtc -I dts -O dtb -o "${curdir}"/bootloader/cbo.dtb "${file}"

}

function help_func
{
	echo "Usage: ./nvsdkmanager [OPTIONS]"

	echo "Where OPTIONS are one of:"
	echo "   --custom [command] - [command] will be run from the bash environment"
	echo
	echo "   --storage VALUE [cli options] - Use initrd flash to flash specified storage media."
	echo "   VALUE can be nvme0n1p1 or sda1. nvme0n1p1 for NVMe SSD, sda1 for USB mass storage. [cli options] will be passed to initrd flash"
	echo
	echo "   [--storage VALUE] --nv-auto-config [--username <username_of_new_account>]"
	echo "        - autoflash and enable oem-config auto configuration"
	echo "        - To specify --username option generates pre-configuration file"
	echo "        - before running autoflash"
	echo
	echo "   --help - displays this message"
	echo
	echo "   [cli options] - Run nvautoflash.sh with [cli options]"
}

function concatenate_args
{
	string=""
	for arg in "$@" # Loop over arguments
	do
		if [[ "${string}" != "" ]]; then
			string+=" " # Delimeter
		fi
		string+="${arg}"
	done
	echo "${string}"
}

function flash_target_with_external_storage
{
	local storage=$1
	local target_board=""
	shift
	exec 5>&1
	# Check for error code and display nvautoflash error
	if OUTPUT=$(./nvautoflash.sh --print_boardid "$@" | tee >(cat - >&5)) ; then
		echo "Parsing boardid successful"
	else
		echo "*** ERROR: Parsing boardid failed" >&2
		exit 1
	fi
	# parse out target_board for initrd flash
	target_board="$(echo "${OUTPUT}" | cut -d " " -f 1 | tail -1)"
	echo "Target board is ${target_board}"
	options=("--external-device" "${storage}" "-c" "${xml_config}" "--showlogs")
	if [ "${storage}" = "sda1" ]; then
		options+=("--network" "usb0")
		create_cbo "\"usb\",\"nvme\""
	else
		create_cbo "\"nvme\",\"usb\""
	fi
	local username=""
	for i in "${@}"
	do
		if [ "${i}" == "--nv-auto-config" ]; then
			options+=("-p" "-C nv-auto-config")
			EXTOPTIONS="-C nv-auto-config"
		elif [ "${i}" == "--username" ]; then
			username=1
		elif [ "${username}" = "1" ]; then
			"${curdir}"/nv_tools/scripts/nv_preseed.sh -u "${i}"
			username=""
		else
			options+=("${i}")
		fi
	done
	if [ "${storage}" = "sda1" ] || [ "${storage}" = "nvme0n1p1" ]; then
		echo "External storage specified ${storage}"
		if [[ "${target_board}" == *"jetson-xavier-nx-devkit"* ]]; then
			echo "Flashing Jetson Xavier NX"
			EXTOPTIONS="${EXTOPTIONS}" NO_ROOTFS=0 "${curdir}"/"${initrd_path}" "${options[@]}" jetson-xavier-nx-devkit-qspi internal
		elif [[ "${target_board}" == *"jetson-agx-xavier"* ]]; then
			echo "Flashing Jetson Xavier"
			EXTOPTIONS="${EXTOPTIONS}" "${curdir}"/"${initrd_path}" "${options[@]}" "${target_board}" "${storage}"
		else
			echo "*** ERROR: Unsupported device" >&2
			exit 3
		fi
	else
		echo "*** ERROR: Invalid storage device" >&2
		echo "Please enter sda1 or nvme0n1p1" >&2
		exit 2
	fi
}

# if the user is not root, there is not point in going forward
THISUSER=$(whoami)
if [ "x$THISUSER" != "xroot" ]; then
	echo "***ERROR: This script requires root privilege" >&2
	exit 4
fi

if [[ $# -eq 0 ]]; then
	echo "Defaulting to autoflash"
	"${curdir}"/nvautoflash.sh
	exit $?
fi

while [ "$1" != "" ];
do
   case $1 in
	--custom )
		shift
		# Concat args given by user to run custom cmd
		args="$(concatenate_args "$@")"
		echo "${args}"
		"$@"
		exit $?
		;;
	--storage )
		shift
		echo "user entered ${*}"
		# calling helper function to handle initrd flash for storage media
		flash_target_with_external_storage "${@}"
		exit $?;
		;;
	--nv-auto-config )
		shift
		if [ "$1" = "--username" ]; then
			shift
			username=$1
			if [ "${username}" = "" ]; then
				echo "*** ERROR Missing username of new account"
				exit 5
			fi
			"${curdir}"/nv_tools/scripts/nv_preseed.sh -u "${username}"
		fi
		"${curdir}"/nvautoflash.sh -C "nv-auto-config"
		exit $?
		;;
	--help )
		help_func
		exit 0
	  ;;
	* )
		"${curdir}"/nvautoflash.sh "$@"
		exit $?
	   ;;
	esac
	shift
done
