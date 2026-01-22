#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2021-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: MIT
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
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

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
xml_config="tools/kernel_flash/flash_l4t_t234_nvme.xml";
t264_nvme_xml_config="tools/kernel_flash/flash_l4t_t264_nvme.xml"
t264_ufs_xml_config="tools/kernel_flash/flash_l4t_t264_ufs.xml"
source "${curdir}/tools/kernel_flash/l4t_kernel_flash_vars.func"

file=""

trap cleanup EXIT

cleanup()
{
	rm -f "${curdir}"/bootloader/cbo.dtb
	if [ -f "${file}" ]; then
		rm "${file}"
	fi
}

# Expects: readonly USB_ID_ALLOWED in l4t_kernel_flash_vars.func
wait_for_usb_product() {
  local timeout=30
  local start=$SECONDS

  # Build a quick lookup of allowed product IDs (lowercase hex)
  local -A _allowed=()
  local _id
  for _id in "${USB_ID_ALLOWED[@]}"; do
    _allowed["${_id,,}"]=1
  done

  # Avoid literal glob when no matches exist, and restore nullglob afterwards
  local _restore_shopt
  _restore_shopt="$(shopt -p nullglob)"
  shopt -s nullglob

  while (( SECONDS - start < timeout )); do
    local f val
    for f in /sys/bus/usb/devices/*/idProduct; do
      # Each file contains the 4-hex-digit product ID (e.g., 7023)
      if read -r val < "$f"; then
        val="${val,,}"
        if [[ -n "${_allowed[$val]}" ]]; then
          eval "$_restore_shopt"
          return 0  # found an allowed product
        fi
      fi
    done
    sleep 1
  done

  eval "$_restore_shopt"
  return 1  # timed out
}


function help_func
{
	echo "Usage: ./nvsdkmanager [OPTIONS]"

	echo "Where OPTIONS are one of:"
	echo "   --custom [command] - [command] will be run from the bash environment"
	echo
	echo "   --storage VALUE [cli options] - Use initrd flash to flash specified storage media."
	echo "   VALUE can be nvme0n1p1, nvme1n1p1 or sda1. nvme0n1p1, nvme1n1p1 for NVMe SSD, sda1 for USB mass storage. [cli options] will be passed to initrd flash"
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
	if [ "${storage}" = "ufshci" ]; then
		storage="block/a80b8d0000.ufshci:0"
	fi
	# Check for error code and display nvautoflash error
	if OUTPUT=$(./nvautoflash.sh --print_boardid "$@" | tee >(cat - >&5)) ; then
		echo "Parsing boardid successful"
	else
		echo "*** ERROR: Parsing boardid failed" >&2
		exit 1
	fi
	echo "Wait 30 seconds for host machine to get Jetson Recovery USB"
	wait_for_usb_product
	# parse out target_board for initrd flash
	target_board="$(echo "${OUTPUT}" | cut -d " " -f 1 | tail -1)"
	echo "Target board is ${target_board}"
	local options=("--external-device" "${storage}" "--showlogs")
	local username=""
	for i in "${@}"
	do
		if [ "${i}" == "--nv-auto-config" ]; then
			options+=("-p" "-C nv-auto-config")
			EXTOPTIONS="-C nv-auto-config"
		elif [ "${i}" == "--username" ]; then
			username=1
		elif [ "${username}" = "1" ]; then
			"${curdir}"/nv_tools/scripts/nv_preseed_oobe.sh -u "${i}"
			username=""
		else
			options+=("${i}")
		fi
	done
	OVERLAY_DTB_FILE=
	if [ "${storage}" = "nvme0n1p1" ] || [ "${storage}" = "nvme1n1p1" ]; then
		OVERLAY_DTB_FILE="BootOrderNvme.dtbo"
	fi
	if [ "${storage}" = "sda1" ]; then
		OVERLAY_DTB_FILE="BootOrderUsb.dtbo"
	fi
	if [ "${storage}" = "mmcblk1p1" ] || [ "${storage}" = "mmcblk0p1" ]; then
		OVERLAY_DTB_FILE="BootOrderSD.dtbo"
	fi
	if [ "${storage}" = "block/a80b8d0000.ufshci:0" ]; then
		OVERLAY_DTB_FILE="BootOrderUfs.dtbo"
	fi
	if [ "${storage}" = "sda1" ] || [ "${storage}" = "nvme0n1p1" ] || [ "${storage}" = "nvme1n1p1" ] || [ "${storage}" = "mmcblk1p1" ] || [ "${storage}" = "mmcblk0p1" ];  then
		echo "External storage specified ${storage}"
		if [[ "${target_board}" == *"p3834-000"* || "${target_board}" == *"jetson-agx-thor"* ]]; then
			options+=("-c" "${t264_nvme_xml_config}")
			echo "Flashing Jetson AGX Thor"
			ADDITIONAL_DTB_OVERLAY_OPT="${OVERLAY_DTB_FILE}" EXTOPTIONS="${EXTOPTIONS}" "${curdir}"/"${initrd_path}" "${options[@]}" "${target_board}" "${storage}"
		elif [[ "${target_board}" == *"jetson-agx-orin"* ]]; then
			options+=("-c" "${xml_config}")
			echo "Flashing Jetson Orin"
			ADDITIONAL_DTB_OVERLAY_OPT="${OVERLAY_DTB_FILE}" EXTOPTIONS="${EXTOPTIONS}" "${curdir}"/"${initrd_path}" "${options[@]}" "${target_board}" "${storage}"
		elif [[ "${target_board}" == *"jetson-orin-nano"* ]]; then
			options+=("-c" "${xml_config}")
			echo "Flashing Jeton Orin Nano"
			ADDITIONAL_DTB_OVERLAY_OPT="${OVERLAY_DTB_FILE}" EXTOPTIONS="${EXTOPTIONS}" "${curdir}"/"${initrd_path}" "${options[@]}" -p "--no-systemimg -c bootloader/generic/cfg/flash_t234_qspi.xml" "${target_board}" internal
		else
			echo "*** ERROR: Unsupported device ${target_board}" >&2
			exit 3
		fi
	elif [ "${storage}" = "block/a80b8d0000.ufshci:0" ];  then
		if [[ "${target_board}" == *"p3834-000"* || "${target_board}" == *"jetson-agx-thor"* ]]; then
			options+=("-c" "${t264_ufs_xml_config}")
			echo "Flashing Jetson AGX Thor"
			ADDITIONAL_DTB_OVERLAY_OPT="${OVERLAY_DTB_FILE}" EXTOPTIONS="${EXTOPTIONS}" "${curdir}"/"${initrd_path}" "${options[@]}" "${target_board}" internal
		else
			echo "*** ERROR: Unsupported device ${target_board}" >&2
			exit 3
		fi
	else
		echo "*** ERROR: Invalid storage device" >&2
		echo "Supported storage devices are: sda1, nvme0n1p1, nvme1n1p1, mmcblk1p1, mmcblk0p1, block/a80b8d0000.ufshci:0" >&2
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
	--help )
		help_func
		exit 0
	  ;;
	* )
		flash_options=
		for i in "${@}"
		do
			if [ "${i}" == "--nv-auto-config" ]; then
				flash_options+=("-C" "nv-auto-config")
			elif [ "${i}" == "--username" ]; then
				username=1
			elif [ "${username}" = "1" ]; then
				"${curdir}"/nv_tools/scripts/nv_preseed_oobe.sh -u "${i}"
				username=""
			else
				flash_options+=("${i}")
			fi
		done

		"${curdir}"/nvautoflash.sh "${flash_options[@]}"
		exit $?
	   ;;
	esac
	shift
done
