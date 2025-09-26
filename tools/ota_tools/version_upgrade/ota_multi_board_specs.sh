#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2021-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

# This is a script to provide functions for support multiple board
# specifications in Image-Based OTA (Partition-Based OTA).
_BOARD_SPEC_NAME=
_NV_BOOT_CONTROL_CONF="nv_boot_control.conf"
_INT_DEV="internal_device"
_EXT_DEV="external_device"

# Construct the board spec name used for searching the matching
# sub-directory under "images-XXX-XXX".
construct_board_spec_name()
{
	local board_spec_name="$1"
	local compatible_spec="$2"
	local chipid="$3"
	local name=
	local boardid=
	local boardver=
	local boardsku=
	local boardrev=

	boardid=$( echo "${compatible_spec}" | awk -F"-" '{print $1}' )
	boardver=$( echo "${compatible_spec}" | awk -F"-" '{print $2}' )
	boardsku=$( echo "${compatible_spec}" | awk -F"-" '{print $3}' )
	boardrev=$( echo "${compatible_spec}" | awk -F"-" '{print $4}' )

	# For t19x device, the boardid and boardver(fab) are compulsory, so return error
	# if either of them is empty.
	if [ "${chipid}" == "0x19" ]; then
		if [ "${boardid}" == "" ] || [ "${boardver}" == "" ]; then
			echo "Both BOARDID and BOARDVER(FAB) must be set"
			return 1
		fi
	fi

	# Board spec name is composed of boardid, boardver, boardsku and
	# boardrev, like this:
	# board_spec_name=${boardid}-${boardver}-${boardsku}-${boardrev}
	# For example, for jetson-agx-xavier-devkit, board_spec_name can be 2888-400-0001-D.0
	# or 2888-400-0001-E.0.
	name="${boardid}-${boardver}-${boardsku}-${boardrev}"
	eval "${board_spec_name}=${name}"
	return 0
}

# Copy the images dependent on the compatible board spec from
# maching sub-directory into the corresponding "images-XXX-XXX"
# directory.
copy_board_spec_dep_files()
{
	local work_dir="$1"
	local images_dir=
	local found=0
	local tmp_file="/tmp/ota_images_dir_list.txt"

	pushd "${work_dir}" > /dev/null 2>&1 || return 1

	# Find all "images-XXX-XXX" direcotries and store them into
	# the temp file "/tmp/ota_images_dir_list.txt"
	find . -name "images*" >"${tmp_file}"
	while read -r images_dir
	do
		# Copy the images from matching sub-directory
		if [ -e "${images_dir}/${_BOARD_SPEC_NAME}" ]; then
			ota_log "Copy files from ${images_dir}/${_BOARD_SPEC_NAME}/ to ${images_dir}/"
			cp "${images_dir}/${_BOARD_SPEC_NAME}"/* "${images_dir}"/
			found=1
		fi
	done < "${tmp_file}"
	rm -f "${tmp_file}"
	popd > /dev/null 2>&1 || return 1

	if [ "${found}" == 0 ]; then
		ota_log "No image is found for compatible SPEC ${_BOARD_SPEC_NAME}"
		return 1
	fi
	return 0
}

# Call the "nv-l4t-bootloader-config.sh" to update the TNSPEC and
# COMPATIBLE_SPEC in the "{work_dir}/ota_nv_boot_control.conf".
ota_choose_images()
{
	# This function does the following steps:
	# 1. Get COMPATIBLE_SPEC from "/etc/nv_boot_control.conf".
	# 2. Construct the name of the sub-directory (_BOARD_SPEC_NAME) by
	# parsing COMPATIBLE_SPEC.
	# 3. Find the matched sub-directory with the _BOARD_SPEC_NAME and copy
	# files from it to the corresponding "images-XXX-XXX" directory.

	local work_dir="$1"
	local nv_boot_control_conf="/etc/${_NV_BOOT_CONTROL_CONF}"

	if [ ! -f "${nv_boot_control_conf}" ]; then
		ota_log "The file ${nv_boot_control_conf} is not found"
		return 1
	fi

	# Get COMPATIBLE_SPEC from the updated "${nv_boot_control_conf}"
	local compatible_spec=
	compatible_spec=$( awk '/COMPATIBLE_SPEC/ {print $2}' "${nv_boot_control_conf}" )
	if [ "${compatible_spec}" == "" ]; then
		ota_log "Failed to get COMPATIBLE_SPEC from ${nv_boot_control_conf}"
		return 1
	fi
	ota_log "COMPATIBLE_SPEC=${compatible_spec}"

	# Get CHIPID
	local chipid=
	chipid=$( awk '/TEGRA_CHIPID/ {print $2}' "${nv_boot_control_conf}" )
	if [ "${chipid}" == "" ]; then
		ota_log "Failed to get TEGRA_CHIPID from ${nv_boot_control_conf}"
		return 1
	fi
	ota_log "TEGRA_CHIPID=${chipid}"

	# Construct _BOARD_SPEC_NAME by parsing the ${compatible_spec}
	if ! construct_board_spec_name "_BOARD_SPEC_NAME" "${compatible_spec}" "${chipid}"; then
		ota_log "Failed to construct board spec name"
		return 1
	fi
	ota_log "_BOARD_SPEC_NAME=${_BOARD_SPEC_NAME}"

	# Find the matched sub-directory and copy files from it to the
	# corresponding "images-XXX-XXX" directory.
	local dir=
	for dir in "${work_dir}/${_INT_DEV}" "${work_dir}/${_EXT_DEV}"
	do
		if [ -d "${dir}" ]; then
			if ! copy_board_spec_dep_files "${dir}"; then
				ota_log "Failed to call \"copy_board_spec_dep_files ${dir}\""
				return 1
			fi
		fi
	done

	return 0
}
