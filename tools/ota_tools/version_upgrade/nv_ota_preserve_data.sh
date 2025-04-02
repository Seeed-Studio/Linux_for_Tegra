#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2020-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

# This script is used to backup/restore customer's files

# The file "ota_backup_files_list.txt" lists the files to be preserved,
# please read it and follow its format to add the files that needs to
# be preserved. The file "ota_backup_files_list.txt" is under the same
# directory with this script.
_OTA_BACKUP_FILES_LIST="ota_backup_files_list.txt"
_OTA_BACKUP_TAR_FILE="backup_files.tar"
_OTA_BACKUP_GZ_FILE="${_OTA_BACKUP_TAR_FILE}.gz"

ota_backup_customer_files()
{
	local work_dir="${1}"
	local root_dir="${2}"
	local backup_files_list="${work_dir}/${_OTA_BACKUP_FILES_LIST}"
	local tar_file="${work_dir}/${_OTA_BACKUP_TAR_FILE}"
	local gz_file="${work_dir}/${_OTA_BACKUP_GZ_FILE}"
	local tmp_list_file="/tmp/backup.txt.tmp"

	echo "Backing up specified files listed in ${backup_files_list}"
	if [ ! -f "${backup_files_list}" ]; then
		echo "Backup file list ${backup_files_list} is not found"
		return 0
	fi

	if ! grep -Ev "^$|[#]" "${backup_files_list}" >"${tmp_list_file}"; then
		echo "Nothing needs to be backed up in ${backup_files_list}"
		return 0
	fi

	if [ -f "${gz_file}" ]; then
		if gzip -t "${gz_file}"; then
			echo "Backup tar file has been generated"
			return 0
		else
			echo "Delete the invalid "${gz_file}""
			rm -f "${gz_file}"
		fi
	fi

	local path=
	local files_line=""
	pushd "${root_dir}" > /dev/null 2>&1 || return 1
	while read -r path
	do
		# Remove the first "/" in the absolute path
		path="${path#/*}"

		# Check if the target (file/dir) exists or
		# test for symlink
		if [ -e "${path}" ] || [ -L "${path}" ]; then
			files_line+="${path} "
		else
			echo "Warning: ${path} is not found"
		fi
	done < "${tmp_list_file}"
	echo "files_line=${files_line}"
	if [ "${files_line}" == "" ]; then
		echo "Noting is to be backed up"
		popd > /dev/null 2>&1 || return 1
		return 0
	fi

	# Sometime "tar czvf" command is not successful, so
	# using "tar cvf" command instead.
	echo "tar --xattrs --xattrs-include=* -cvpf ${tar_file} ${files_line}"
	if ! tar --xattrs --xattrs-include=* -cvpf "${tar_file}" ${files_line}; then
		if [ -f "${tar_file}" ]; then
			rm -f "${tar_file}"
		fi
		echo "Failed to run \"tar --xattrs --xattrs-include=* -cvpf ${tar_file} ${files_line}\""
		popd > /dev/null 2>&1 || return 1
		return 1
	fi
	echo "gzip ${tar_file}"
	if ! gzip "${tar_file}"; then
		if [ -f "${gz_file}" ]; then
			rm -f "${gz_file}"
		fi
		echo "Failed to run \"gzip ${tar_file}\""
		popd > /dev/null 2>&1 || return 1
		return 1
	fi
	popd > /dev/null 2>&1 || return 1

	return 0
}

ota_restore_customer_files()
{
	local work_dir="${1}"
	local root_dir="${2}"
	local gz_file="${work_dir}/${_OTA_BACKUP_GZ_FILE}"

	echo "Restoring files from ${gz_file}"
	if [ ! -f "${gz_file}" ]; then
		echo "The tar file for backup ${gz_file} is not found"
		return 0
	fi

	if ! gzip -t "${gz_file}"; then
		echo "Backup tar file ${gz_file} is broken\""
		return 1
	fi

	pushd "${root_dir}" > /dev/null 2>&1 || return 1
	echo "tar --xattrs --xattrs-include=* -xzpvf ${gz_file}"
	if ! tar --xattrs --xattrs-include=* -xzpvf "${gz_file}"; then
		echo "Failed to run \"tar --xattrs --xattrs-include=* -xzpvf ${gz_file}\""
		popd > /dev/null 2>&1 || return 1
		return 1
	fi
	popd > /dev/null 2>&1 || return 1
	return 0
}

