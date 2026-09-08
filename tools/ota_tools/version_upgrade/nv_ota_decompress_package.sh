#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2019-2021 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

# This is a script to decompress ota package and verify integrity of its content
_COMMON_TAR_OPTIONS="--warning=no-timestamp"
_OTA_FILE_CHECKLIST=file_checklist.txt

decompress_ota_package()
{
	local ota_package_name=$1
	local ota_work_dir=$2
	local ota_package_tar="${ota_work_dir}/${ota_package_name}"
	local ota_package_sha1sum="${ota_package_tar}.sha1sum"
	local ota_file_checklist=${ota_work_dir}/${_OTA_FILE_CHECKLIST}

	local ts=
	ts="$(date)"
	ota_log "decompress_ota_package: start at ${ts}"

	if [ ! -d "${ota_work_dir}" ];then
		ota_log "Invalid directory ${ota_work_dir} for decompressing ota package"
		return 1
	fi

	if [ ! -f "${ota_package_tar}" ];then
		ota_log "${ota_package_tar} is not found"
		return 1
	fi

	if [ ! -f "${ota_package_sha1sum}" ];then
		ota_log "${ota_package_sha1sum} is not found"
		return 1
	fi

	local sha1_chksum_gen=
	local sha1_chksum=
	sha1_chksum_gen="$(sha1sum "${ota_package_tar}" | cut -d\  -f 1)"
	sha1_chksum="$(cat "${ota_package_sha1sum}")"
	if [ "${sha1_chksum_gen}" = "${sha1_chksum}" ];then
		ota_log "Sha1 checksum for ${ota_package_tar} (${sha1_chksum_gen}) matches"
	else
		ota_log "Sha1 checksum for ${ota_package_tar} (${sha1_chksum_gen} != ${sha1_chksum}) does not match"
		return 1
	fi

	if ! tar xvf "${ota_package_tar}" "${_COMMON_TAR_OPTIONS}" -C "${ota_work_dir}/" >"${ota_file_checklist}"; then
		ota_log "Failed to untar ${ota_package_tar}"
		return 1
	fi
	# Delete the "${ota_package_tar}" and "${ota_package_sha1sum}"
	# to save disk space for OTA update.
	rm -f "${ota_package_tar}" "${ota_package_sha1sum}"

	ts="$(date)"
	ota_log "decompress_ota_package: end at ${ts}"
	return 0
}

# Check whether all the files decompressed from the OTA package exist.
# These files/direcotris are recorded in the "${_OTA_FILE_CHECKLIST}"
# when decompressing the OTA package. This function traverses this file
# to determine whether any file is missing.
check_ota_package_files()
{
	local ota_work_dir=$1
	local ota_file_checklist=${ota_work_dir}/${_OTA_FILE_CHECKLIST}
	local res=0

	if [ ! -f "${ota_file_checklist}" ];then
		ota_log "OTA packages file checklist ${ota_file_checklist} is not found"
		return 1
	fi

	local ts=
	ts="$(date)"
	ota_log "check_ota_package_files: start at ${ts}"

	local ota_file_array=
	local ota_file_array_size=
	readarray ota_file_array < "${ota_file_checklist}"
	ota_file_array_size=${#ota_file_array[@]}
	ota_log "Lines of ota file checklist is ${ota_file_array_size}"

	if [ "${ota_file_array_size}" -eq 0 ];then
		ota_log "OTA package file checklist ${ota_file_checklist} is empty"
		return 1
	fi

	local max_index=$((ota_file_array_size - 1))
	local item=
	for i in $(seq 0 ${max_index})
	do
		item="${ota_work_dir}/$(echo -n "${ota_file_array[$i]}")"
		if [ -f "${item}" ];then
			ota_log "File ${item} is found"
		elif [ -d "${item}" ];then
			ota_log "Directory ${item} is found"
		else
			ota_log "File ${item} is not found"
			res=1
			break
		fi
	done
	ts=$(date)
	ota_log "check_ota_package_files: end at ${ts}"
	return ${res}
}
