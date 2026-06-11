#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

# This script identifies missing dependencies, particularly, the files
# with .so extension.
# An example use:
#	./nv_bsp_dependency_check -c ./files_to_be_checked -d ./reference_files
# Explanation: We have extracted the debian to the directory ./reference_files,
# and ./files_to_be_checked is the directory where we pulled files from
# ./reference_files (it keeps a subset of files). In this case, the checker
# will check if the dependencies of the files in ./files_to_be_checked are in
# ./reference_files so that users can get alerted.

# The dependency checker is only for the case when you got a pull file (subset of a Debian)
# 4 cases when dependency is:
# 1. in the same debian -> error
# 2. nvidia dependency -> warning
# 3. samplefs  -> ignore
# 4. nor case 2 or 3 -> error

set -e

trap 'catch $?' EXIT

catch()
{
	if [[ "${1}" == 0 ]]; then
		return
	fi

	# cleanup the file created
	for object in "${created_object_list[@]}"
	do
		rm -rf "${object}"
	done
}

# Initialize global variables
function InitVar
{
	# input args
	check_directory_str=""
	ignore_dependendency_fail_flag=false
	source_directory_str=""
	source_file_str=""

	# data structures for parsed input vars
	check_list=()
	source_file_list=()
	created_object_list=()
	samplefs_so_files_list=()

	# main
	script_name="$(basename "${0}")"
	script_path=$(dirname "$(realpath -s "${0}")")
	verbose_flag=false
	ret_val=0

	# dir Constants
	BSP_BASE_DIR=""
	FindBspLocation "${script_path}"
	USERSPACE_DIR="${BSP_BASE_DIR}/nv_tegra"
	# nv tools
	NV_TOOLS_DIR="${BSP_BASE_DIR}/nv_tools"
	NV_TOOLS_SCRIPTS_DIR="${NV_TOOLS_DIR}/scripts"
	# global file
	SAMPLEFS_FILE_LIST="${NV_TOOLS_SCRIPTS_DIR}/desktop_samplefs_contents.txt"
	NV_FILE_LIST="${USERSPACE_DIR}/nv_tegra_release"
}

# Print message to log with name of this script
function EchoMessage
{
	echo "${script_name} - ${1}"
}

# Print ERROR message to log, and then exit script
function EchoAndExit
{
	EchoMessage "${1}"

	# Set the return code
	ret_val=1
	exit "${ret_val}"
}

# echos that is only when verbose is enabled
function VerboseEcho
{
	if [[ "${verbose_flag}" == true ]]; then
		echo "${1}"
	fi
}

# The usage message
function usage
{
	echo "Usage:"
	echo "./${script_name} [options]"

	cat << EOF

options:
    -c, --check DIR ---------------- This directory is where the dependency
                                     check will be run against. It is a fully
                                     extracted and transformed directory path
                                     which contains all files to be checked if
                                     their dependencies are met.
    -h, --help --------------------- Print this message.
    -i, --ignore-dependency -------- Ignore error and script exit on missing
                                     dependencies. Instead, print warning(s),
                                     and create output.
    -d, --source-directory DIR ----- A directory of an extracted Debian file
                                     can be provided for the checker to see
                                     if the dependency of the file(s) being
                                     checked is within the Debian file. Either
                                     this or the --source-deb option should be
                                     chosen as a reference.
    -s, --source-deb FILE ---------- A Debian file can be provided for the
                                     checker to see if the dependency of the
                                     file(s) being checked is within the
                                     Debian file. Either this or the
                                     --source-directory option should be chosen
                                     as a reference.
    -v, --verbose ------------------ Verbose mode. Useful for debug.
EOF

}

# Parsing the input argument
function ParseArgs
{
	while [ -n "${1}" ]; do
		case "${1}" in
		-c | --check)
			if [ -z "${2}" ]; then
				EchoAndExit "ERROR: Not enough parameters for convert"
			fi
			check_directory_str="${2}"
			shift 2
			;;
		-h | --help)
			usage
			exit "${ret_val}"
			;;
		-i | --ignore-dependendency)
			ignore_dependendency_fail_flag=true
			shift 1
			;;
		-s | --source-file)
			if [ -z "${2}" ]; then
				EchoAndExit "ERROR: Not enough parameters for the source file"
			fi
			source_file_str="${2}"
			shift 2
			;;
		-d | --source-directory)
			if [ -z "${2}" ]; then
				EchoAndExit "ERROR: Not enough parameters for the source directory"
			fi
			source_directory_str="${2}"
			shift 2
			;;
		-v | --verbose)
			verbose_flag=true
			shift 1
			;;
		*)
			EchoAndExit "ERROR: Invalid parameter. Exiting..."
			;;
		esac
	done
}

# Parse and check the format of the check list
function ParseCheckList
{
	VerboseEcho "Starting function ParseCheckList"

	# check if the check directory exists
	if [[ ! -d "${check_directory_str}" ]]; then
		EchoAndExit "ERROR: Check directory \"${check_directory_str}\" does not exists"
	fi

	# do a "find" to check the dependencies for all files under the check file
	# put all the files to the check list
	while read -r file
	do
		check_list+=("${file}")
	done < <(sudo find "${check_directory_str}" -type f)

	if [[ ${#check_list[@]} -eq 0 ]]; then
		EchoMessage "Warning: No files to be checked"
	fi
}

# Create a list of files in the source file/dir for fast source file lookup
function SetupSourceFileLookupTable
{
	local regex="\.so\.[[:digit:]]+"

	# find all the files under the source directory
	# put the files in the lookup list
	while read -r src_file
	do
		if [[ -z "${src_file}" ]]; then
			break
		fi

		# find the so names for the files
		if [[ "${src_file}" == *.so || "${src_file}" =~ $regex ]]; then
			local src_soname

			src_soname="$(objdump -p "${src_file}" | grep SONAME | sed 's/SONAME//g'| sed 's/ //g')"

			source_file_list+=("${src_soname}")
		fi

		source_file_list+=("${src_file}")
	done < <(sudo find "${source_directory_str}" -type f)
}

# Check if all the input args are valid
function PreCheck
{
	VerboseEcho "Starting the function PreCheck"

	# check if the check list is empty
	if [[ -z "${check_directory_str}" ]]; then
		EchoAndExit "ERROR: Check directory is not provided"
	fi

	# Check directory should be a full path
	if [[ "${check_directory_str}" != /* ]]; then
		EchoAndExit "ERROR: Check directory \"${check_directory_str}\" is not an absolute path"
	fi

	# check the source directory or source file
	if [[ -n "${source_file_str}" ]]; then
		if [[ -n "${source_directory_str}" ]]; then
			EchoAndExit "ERROR: Providing both source file and directory is not allowed"
		fi

		# check if the file exists
		if [[ ! -f "${source_file_str}" ]]; then
			EchoAndExit "ERROR: Cannot find file \"${source_file_str}\""
		fi

		# check the file extension
		if [[ "${source_file_str}" != *.deb ]]; then
			EchoAndExit "ERROR: \"${source_file_str}\" is not a Debian file"
		fi

		# check the file is in an absolute path format
		if [[ "${source_file_str}" != /* ]]; then
			EchoAndExit "ERROR: Source file \"${source_file_str}\" is not in an absolute path format"
		fi

		# extract the debian file to a directory
		local extract_dirname="extracted_source"
		source_directory_str="${script_path}/${extract_dirname}"

		# check if the directory exists
		if [[ -d "$source_directory_str" ]]; then
			EchoAndExit "ERROR: The extracted directory name ${extract_dirname} already exists"
		fi

		mkdir -p "${source_directory_str}"
		created_object_list+=("${source_directory_str}")
		dpkg-deb -x "${source_file_str}" "${source_directory_str}"

	elif [[ -n "${source_directory_str}" ]]; then
		# check if the dir exists
		if [[ ! -d "${source_directory_str}" ]]; then
			EchoAndExit "ERROR: Cannot find directory \"${source_directory_str}\""
		fi

		# check the dir is in an absolute path format
		if [[ "${source_directory_str}" != /* ]]; then
			EchoAndExit "ERROR: Source directory \"${source_directory_str}\" is not a absolute path"
		fi
	else
		EchoAndExit "ERROR: Either source directory or source file should be provided"
	fi

	# setup a lookup hashmap of the source directory
	SetupSourceFileLookupTable
}

# Find out where the BSP dir is
function FindBspLocation
{
	local cur_directory="${1}"

	# Stops when reaches to the root directory
	while [[ "${cur_directory}" != "/" ]]; do
		local cur_dir_name
		cur_dir_name=$(basename "${cur_directory}")

		# Find out if current dir is the BSP dir
		if [[ "${cur_dir_name}" == "Linux_for_Tegra" ]]; then
			BSP_BASE_DIR="${cur_directory}"

			return
		fi

		# Strip the end layer
		cur_directory=$(dirname "${cur_directory}")

	done

	EchoAndExit "Cannot find the BSP Location"
}

# Setup the list of so files inside samplefs
function SetupListofSoInSamplefs
{
	if [[ ! -f "${SAMPLEFS_FILE_LIST}" ]]; then
		EchoAndExit "ERROR: Cannot find the samplefs file"
	fi

	# the depending file will only be so files, so only grep them
	while read -r so_file
	do
		samplefs_so_files_list+=("${so_file}")
	done < <(grep "\.so" "${SAMPLEFS_FILE_LIST}")

}

# Check if the dependencies of the check list are in the Debian
function CheckInDebianDependency
{
	# check so name
	local needed_file="${1}"
	local ret_code=1

	for src_file in "${source_file_list[@]}"
	do
		local src_filename
		src_filename="$(basename "${src_file}")"

		if [[ "${needed_file}" == "${src_filename}" ]]; then
			ret_code=0
			break
		fi
	done

	return "${ret_code}"
}

# Check if the dependencies of the check lists are within Nvidia's collections
function CheckNvidiaDependency
{
	local needed_file="${1}"
	local ret_code=1

	# check if the file does exists
	if [[ ! -f "${NV_FILE_LIST}" ]]; then
		EchoAndExit "Cannot find the nv release file"
	fi

	while read -r line
	do
		local so_file
		so_file="$(basename "${line}")"

		if [[ "${needed_file}" == "${so_file}" ]]; then
			ret_code=0
			break
		fi

	done <"${NV_FILE_LIST}"

	return "${ret_code}"
}

# Check if the the dependencies of the check lists are within the samplefs
function CheckSamplefsDependency
{
	local needed_file="${1}"
	local ret_code=1

	for samplefs_so in "${samplefs_so_files_list[@]}"
	do
		local so_file
		so_file="$(basename "${samplefs_so}")"

		if [[ "${needed_file}" == "${so_file}" ]]; then
			ret_code=0
			break
		fi
	done

	return "${ret_code}"
}

# Check the files in the list has required dependecy in either source Debian,
# Nvidia dependency, or samplefs
function CheckDependencyInCheckList
{
	VerboseEcho "Starting function CheckDependencyInCheckList"

	# setup the samplefs file list
	SetupListofSoInSamplefs

	for file_checked in "${check_list[@]}"
	do
		## Get the needed files of a file being checked
		local needed_files

		# check if a file can be "objdump"
		if ! objdump -s "${file_checked}" >/dev/null 2>&1; then
			continue
		fi

		dump="$(objdump -x "${file_checked}")"
		# Remove NEEDED and white spaces, then add new spaces to it
		needed_files=$(echo "${dump}" | grep "NEEDED" | sed 's/NEEDED//g'| sed 's/ //g')

		# Ignore files that does not have dependency
		if [[ -z "${needed_files}" ]]; then
			continue
		fi

		## Remove the files that are already in the list of files being checked (already included)
		non_checked_files=()
		for need_file in ${needed_files}
		do
			local found_flag=false
			for existing_file in "${check_list[@]}"
			do
				local existing_filename
				existing_filename=$(basename "${existing_file}")
				# the file needed is already in the checked list (no need to find it in other places)
				if [[ "${need_file}" == "${existing_filename}" ]]; then
					found_flag=true
					break
				fi
			done

			if [[ "${found_flag}" == false ]]; then
				non_checked_files+=("${need_file}")
			fi

		done

		## Check each non checked files to find where they belong
		for needed_file in "${non_checked_files[@]}"
		do
			# check in debian
			local debian_ret
			if CheckInDebianDependency "${needed_file}"; then
				debian_ret=0

				# case 1: found in debian but not inside the pull list (checked file)
				if [[ "${ignore_dependendency_fail_flag}" == true ]]; then
					local file_checked_name
					file_checked_name=$(basename "${file_checked}")

					EchoMessage "Warning: The dependency of ${file_checked_name}, ${needed_file}, is in the source Debian but not found in the checked files"
				else
					local file_checked_name
					file_checked_name=$(basename "${file_checked}")

					EchoAndExit "Error: The dependency of ${file_checked_name}, ${needed_file}, is in the source Debian but not found in the checked files"
				fi
			else
				debian_ret=1
			fi

			# check in release
			local nv_ret
			if CheckNvidiaDependency "${needed_file}"; then
				local file_checked_name
				file_checked_name=$(basename "${file_checked}")
				nv_ret=0

				# case 2: found in nv release
				EchoMessage "Warning: The dependency of ${file_checked_name}, ${needed_file}, is in the Nvidia releases. Please remember to include it in your package"
			else
				nv_ret=1
			fi

			# check in samplefs
			if CheckSamplefsDependency "${needed_file}"; then
				# case 3: ignore
				continue
			else
				# case 4: cannot find the dependency anywhere
				if [[ "${nv_ret}" == 1 && "${debian_ret}" == 1 ]]; then
					if [[ "${ignore_dependendency_fail_flag}" == true ]]; then
						local file_checked_name
						file_checked_name=$(basename "${file_checked}")

						EchoMessage "Warning: Cannot find where the dependency of ${file_checked_name}, ${needed_file}, resides in"
					else
						local file_checked_name
						file_checked_name=$(basename "${file_checked}")

						EchoAndExit "Error: Cannot find where the dependency of ${file_checked_name}, ${needed_file}, resides in"
					fi
				fi
			fi

		done

	done
}

# main
InitVar
ParseArgs "$@"
PreCheck
ParseCheckList
CheckDependencyInCheckList
