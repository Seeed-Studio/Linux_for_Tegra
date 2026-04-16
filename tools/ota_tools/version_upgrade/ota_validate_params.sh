#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

# This is a script to provide function that is used to validate the
# parameters, including target board and the base version.

ota_message()
{
	local message="$1"
	if [ "$(type -t ota_log)" == "function" ]; then
		ota_log "${message}"
	else
		echo "${message}"
	fi
}

ota_validate_params()
{
	local target_board="${1}"
	local base_version="${2}"
	local storage_device="${3}"
	local board_specs_conf="${4}"
	local board_spec_name="${5}"
	local chipid_name="${6}"
	local board_ver_alias=

	if [ "${target_board}" == "" ]; then
		ota_message "Target board is NULL"
		return 1
	fi
	if [ "${base_version}" == "" ]; then
		ota_message "Base version is NULL"
		return 1
	fi
	if [ ! -f "${board_specs_conf}" ]; then
		ota_message "The board specs configuration file is not found"
		return 1
	fi

	target_board="${target_board^^}"
	base_version="${base_version^^}"
	target_board="$(echo "${target_board}" | sed 's/\-/_/g' -)"
	base_version="$(echo "${base_version}" | sed 's/\-/_/g' -)"
	if [ "${storage_device}" == "external" ]; then
		storage_device="SD_"
	else
		storage_device=""
	fi
	local board_ver_alias=
	board_ver_alias="${target_board}_${base_version}_${storage_device}ALIAS"

	source "${board_specs_conf}"

	# Check whether the combination of target board and base version
	# is defined in the "${board_specs_conf}"
	if [ -z "${!board_ver_alias}" ] || [ "${!board_ver_alias}" == "" ]; then
		ota_message "Target board(${target_board}) and base version(${base_version}) is not supported"
		return 1
	fi
	if [ "${board_spec_name}" != "" ]; then
		eval "${board_spec_name}=${board_ver_alias}"
	fi

	# Get chipid from board spec if required
	local item=
	if [ "${chipid_name}" != "" ]; then
		for item in "${T23X_DEVICES[@]}"
		do
			if [ "${item}" == "${target_board}" ]; then
				eval "${chipid_name}=0x23"
				return 0
			fi
		done
		ota_message "Failed to get the chip id for ${target_board}"
		return 1
	fi

	return 0
}
