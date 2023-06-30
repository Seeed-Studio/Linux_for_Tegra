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
#

# This script repackages files in a debian into tar files. It gives the users
# the freedom to choose which files to be packed (by pull option) and listing
# the dependencies of each so or application being packaged (by print
# dependencies). Also, the path of files being packed can be transformed as
# well (by transform option).
# Note: Making the entire script within 80 char could be a future improvement

set -e

trap 'catch $?' EXIT

catch()
{
	if [[ "${1}" == 0 ]]; then
		return
	fi

	VerboseEcho "Error ${1} occurred"

	# if the output dir is created, delete all
	if [[ "${user_specified_output_dir}" == false && -n "${final_directory}" ]]; then
		VerboseEcho "Removing the created staging directory ${final_directory}"
		rm -rf "${verbose_option}" "${final_directory}"
	elif [[ -n "${final_directory}" ]]; then
		# only remove the files we created
		for created_file_dir in "${created_file_list[@]}"
		do
			if [[ "${created_file_dir}" == *tbz2 ]]; then
				local created_file_abs="${final_directory}/${created_file_dir}"
				rm -rf "${verbose_option}" "${created_file_abs}"
			elif [[ -d "${created_file_dir}" ]]; then
				# if a dir is in our "create" list, that means the directory has not been pop
				rm -rf "${verbose_option}" "${created_file_dir}"
			else
				EchoMessage "WARNING: Cleaup - Cannot delete ${created_file_dir}"
			fi
		done
	fi
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

# Print usage message
function usage
{
	echo "Usage:"
	echo "./${script_name} [options]"

	cat << EOF
options:
    -c, --convert=FILE(S) ---------- Convert a Debian file into .tbz2 file.
                                     Multiple files may be specified by adding
                                     comma(s) in between without space. (e.g
                                     "abc.deb,xyz.deb,123.deb")
        --convert-all -------------- Convert all the Debian files of BSP
                                     into .tbz2 file. This command roughly
                                     takes 10 minutes to complete depending on
                                     the machine.
    -d, --print-dependencies=FILE -- Print dependent files to the standard
                                     output unless optional output file is
                                     specified, then only outputs to the file.
    -h, --help --------------------- Print this message.
    -i, --ignore-dependency -------- Ignore all the missing dependency error.
                                     Instead, only the warnings are printed.
    -o, --output-dir=DIR ----------- Converted debian are placed here, or else
                                     a default directory in the name format of
                                     “converted_MM-DD-YYYY-THHMMSS”
    -p, --pull=FILE ---------------- A link to a text file list which specifies
                                     which files from inside the debian to move
                                     into the .tbz2. Each specified file should
                                     be seperated with a newline and in a
                                     relative path in a Debian file. This
                                     option is only applicable on converting
                                     one debian file.
    -t, --transform=PATH_MAP ------- Transform all the old path to the new
                                     specified path. Every mapping should have
                                     a colon seperating the old and new path.
                                     Multiple path maps may be specified by
                                     adding comma(s) in between without space.
                                     (e.g "usr/lib:usr/lib_new,usr/bin:usr/sbin
                                     ") The transformation is done in the order
                                     as specified, which is done with "sed" to
                                     replace iteratively. Therefore, it is
                                     suggested to use a full path
                                     (e.g. path/used/to/be:path/going/to/be) in
                                     case the file renames.
   -v, --verbose ------------------- Explain what is being done

EOF

}

# Initialize global variables
function InitVar
{
	# Constant
	# Permissions for target files
	TARGET_MODE="u+rwX,go+rX,go-w"

	# Input arguments
	bsp_directory=""
	convert_all_flag=false
	convert_list_str=""
	transform_path_map=""
	final_directory=""
	user_specified_output_dir=false
	print_dependency_flag=false
	pull_file_str=""
	dependencies_output_file=""
	ignore_dependendency_fail_flag=false
	verbose_flag=false
	verbose_option=""

	# Convert data structures
	# A list of files waiting to be converted
	convert_list=()
	# A list files/directories being created that has been converted
	created_file_list=()
	# A list of dependency of each so or app in the convert deb
	dependency_list=()
	# The path map for transform
	old_paths=()
	new_paths=()

	# main
	script_name="$(basename "${0}")"
	script_path=$(dirname "$(realpath -s "${0}")")
	cur_working_path=$(pwd)
	ret_val=0

	# Constant
	BSP_DIR_NAME="Linux_for_Tegra"
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
		if [[ "${cur_dir_name}" == "${BSP_DIR_NAME}" ]]; then
			bsp_directory="${cur_directory}"

			return
		fi

		# Strip the end layer
		cur_directory=$(dirname "${cur_directory}")

	done

	EchoAndExit "ERROR: Cannot find the BSP Location"
}

# Parse the arguments given
function ParseArgs
{
	VerboseEcho "Starting the function parse_args"

	while [ -n "${1}" ]; do
		case "${1}" in
		-c)
			if [ -z "${2}" ]; then
				EchoAndExit "ERROR: Not enough parameters for convert"
			fi
			convert_list_str="${2}"
			shift 2
			;;
		--convert=*)
			convert_list_str="${1#*=}"
			if [ -z "${convert_list_str}" ]; then
				EchoAndExit "ERROR: Not enough parameters for convert"
			fi

			shift 1
			;;
		--convert-all)
			convert_all_flag=true
			shift 1
			;;
		-d)
			# Check next positional parameter
			shift 1
			local nextopt=""
			eval "nextopt=\${$OPTIND}"

			# check if next opt exists and does not start with dash
			if [[ -n "${nextopt}" && "${nextopt}" != -* ]] ; then
				OPTIND=$((OPTIND + 1))
				dependencies_output_file="${nextopt}"
				shift 1
			else
				dependencies_output_file=""
			fi
			print_dependency_flag=true
			;;
		--print-dependencies=*)
			# Check next parameter
			if [[ "${1}" == *=* ]]; then
				dependencies_output_file="${1#*=}"
			fi
			print_dependency_flag=true

			shift 1
			;;
		-h | --help)
			usage
			exit "${ret_val}"
			;;
		-i | --ignore-dependendency)
			ignore_dependendency_fail_flag=true
			shift 1
			;;
		-o)
			if [ -z "${2}" ]; then
				EchoMessage "ERROR: Not enough parameters"
				usage
			fi
			user_specified_output_dir=true

			# striping end slash
			local sanitized_path
			local sanitized_dirname
			sanitized_path="$(dirname "${2}")"
			sanitized_dirname="$(basename "${2}")"
			final_directory="${sanitized_path}/${sanitized_dirname}"

			shift 2
			;;
		--output-dir=*)
			if [ -z "${1#*=}" ]; then
				EchoAndExit "ERROR: Not enough parameters for output-dir"
			fi
			user_specified_output_dir=true

			# striping end slash
			local sanitized_path
			local sanitized_dirname
			sanitized_path="$(dirname "${1#*=}")"
			sanitized_dirname="$(basename "${1#*=}")"
			final_directory="${sanitized_path}/${sanitized_dirname}"

			shift 1
			;;
		-p)
			if [ -z "${2}" ]; then
				EchoMessage "ERROR: Not enough parameters"
				usage
			fi
			pull_file_str="${2}"
			shift 2
			;;
		--pull=*)
			pull_file_str="${1#*=}"
			if [ -z "${pull_file_str}" ]; then
				EchoAndExit "ERROR: Not enough parameters for pull"
			fi

			shift 1
			;;
		-t)
			if [ -z "${2}" ]; then
				EchoMessage "ERROR: Not enough parameters"
				usage
			fi
			transform_path_map="${2}"
			shift 2
			;;
		--transform=*)
			transform_path_map="${1#*=}"
			if [ -z "${transform_path_map}" ]; then
				EchoAndExit "ERROR: Not enough parameters for transform"
			fi

			shift 1
			;;
		-v | --verbose)
			verbose_flag=true
			verbose_option="-v"
			shift 1
			;;
		*)
			EchoAndExit "ERROR: Invalid parameter ${1}. Exiting..."
			;;
		esac
	done
}


# Parse and turn convert list into an array
function ParseConvertList
{
	declare -A convert_filename_to_path_map

	# Check if there's consequent comma
	if [[ "${convert_list_str}" == *",,"* ]]; then
		EchoAndExit "ERROR: invalid usage - malformed line in convert option with more than one comma in a row"
	fi

	if [[ "${convert_all_flag}" == true ]]; then
		while read -r file
		do
			if [[ -z "${file}" ]]; then
				break
			fi

			# ex: [foo.deb]=/path/to/foo.deb
			convert_filename_to_path_map["${file##*/}"]="${file}"
		done < <(sudo find "${bsp_directory}" -type f -name "*.deb")
	else
		while read -r file
		do
			# do a "find" to check if this deb is included
			if [[ "${file}" != /* ]]; then
				# use grep's return code to check if we really found the file
				if sudo find "${bsp_directory}" -type f -name "${file}" | grep -q "."; then
					while read -r filepath_abs
					do
						file="${filepath_abs}"
					done < <(sudo find "${bsp_directory}" -type f -name "${file}")
				else
					EchoAndExit "ERROR: Could not find ${file}"
				fi
			fi

			# check if the file exist
			if [[ ! -f "${file}" ]]; then
				EchoAndExit "ERROR: Could not find ${file}"
			fi

			# check file extension
			if [[ ! "${file}" == *.deb ]]; then
				EchoAndExit "ERROR: ${file} is not a Debian file"
			fi

			# ex: [foo.deb]=/path/to/foo.deb
			convert_filename_to_path_map["${file##*/}"]="${file}"
		done < <(echo "${convert_list_str}" | tr "," "\n")
	fi

	if [[ ${#convert_filename_to_path_map[@]} -eq 0 ]]; then
		return
	fi

	# Sort the element into the convert list
	while IFS= read -rd '' key
	do
		# add to convert list
		convert_list+=("${convert_filename_to_path_map[${key}]}")
	done < <(printf '%s\0' "${!convert_filename_to_path_map[@]}" | sort -z)
}

# Parse and turn path map string into an array
function ParsePathMap
{
	local old_dir
	local new_dir

	if [[ -z "${transform_path_map}" ]]; then
		return
	fi

	# Check if there's consequent comma
	if [[ "${transform_path_map}" == *",,"* ]]; then
		EchoAndExit "ERROR: Invalid usage - malformed line in transform option with more than one comma in a row"
	fi

	while read -r mapping
	do
		local colons
		local number_of_colons

		colons="${mapping//[^:]}"
		number_of_colons="${#colons}"

		# check if the string contains colon
		if [[ "${number_of_colons}" -eq 0 ]]; then
			EchoAndExit "ERROR: Invalid usage - malformed line in transform option without colons"
		# check duplicated colon
		elif [[ ${number_of_colons} -gt 1 ]]; then
			EchoAndExit "ERROR: Invalid usage - malformed line in transform option with more than one colon in an instance"
		fi

		old_dir=$(echo "${mapping}" | cut -d ":" -f 1)
		new_dir=$(echo "${mapping}" | cut -d ":" -f 2)

		# check if old and new directory are not empty
		if [ -z "${old_dir}" ]; then
			EchoAndExit "ERROR: Invalid usage - malformed line in transform option that does not specify an old path"
		elif [ -z "${new_dir}" ]; then
			EchoAndExit "ERROR: Invalid usage - malformed line in transform option that does not specify a new path"
		fi

		# check if path is absolute
		if [[ "${old_dir}" == /* ]]; then
			EchoMessage "WARNING: Old path is an absolute path, ignoring the instance in transform option"
			continue
		elif [[ "${new_dir}" == /* ]]; then
			EchoAndExit "ERROR: Absolute path is not allowed to be a new path in the instance of transform option"
		fi

		# Warn is old dir is same as the new one
		if [[ "${old_dir}" == "${new_dir}" ]]; then
			EchoMessage "WARNING: Old path and new path are identical"
			continue
		fi

		# add to the map
		old_paths+=("${old_dir}")
		new_paths+=("${new_dir}")
	done < <(echo "${transform_path_map}" | tr "," "\n")
}

# Run through the convert list and see if there's possibility that tar already
# exists
function CheckExistedTarFile
{
	# The directory is not created yet, no need to check
	if [[ "${user_specified_output_dir}" == false ]]; then
		return
	fi

	for deb_file in "${convert_list[@]}"
	do
		local deb_filename

		deb_filename=$(basename "${deb_file}" .deb)

		# Found an existing tar
		if sudo find "${final_directory}" -type f -name "${deb_filename}.tbz2" | grep -q "." ; then
			EchoAndExit "ERROR: The file to be converted into, ${deb_filename}.tbz2, is already in ${final_directory}"
		fi
	done
}


# Check to make sure all the params are valid
function PreCheck
{
	VerboseEcho "Starting the function PreCheck"

	# Check if specified convert files
	if [ -z "${convert_list_str}" ]; then
		#  no convert option is not allowed
		if [[ "${convert_all_flag}" == false ]]; then
			EchoAndExit "ERROR: No convert file specified"
		fi
	else
		if [[ "${convert_all_flag}" == true ]]; then
			EchoMessage "WARNING: Convert-all overrides convert option"
		fi
	fi

	# Check if existing dependency file exist or not
	if [[ "${print_dependency_flag}" == true ]]; then
		# file exists or non-file
		if [[ -f "${dependencies_output_file}" ]]; then
			EchoAndExit "ERROR: Output dependency file name already exists"
		elif [[ -d "${dependencies_output_file}" ]]; then
			EchoAndExit "ERROR: Output dependency file should be a name, not a directory"
		fi
	fi

	# Parse Check the format of the convert list
	ParseConvertList

	# Check pull file existance
	if [[ -n "${pull_file_str}" && ! -f "${pull_file_str}" ]]; then
		EchoAndExit "ERROR: Could not find ${pull_file_str}"
	fi

	# Check if pull file comes with a list of convert files
	if [[ -n "${pull_file_str}" && "${#convert_list[@]}" -gt 1 ]]; then
		EchoAndExit "ERROR: Invalid usage - Pull file can only be paired with one convert file"
	fi

	# Parse and Check the format of the transform_path_map
	ParsePathMap

	# Check if there's an existing tar file
	CheckExistedTarFile
}

# Setup the directory that we put all converted files into output directory
function SetupFinalOutputDirectory
{
	local timestamp
	local output_directory="${1}"
	timestamp="$(date +"%Y-%m-%dT%H%M%S")"

	VerboseEcho "Starting the function SetupFinalOutputDirectory"
	VerboseEcho "    Function argument1: ${output_directory}"

	# Check output dir, if not specified create a default
	if [ -z "${output_directory}" ]; then
		final_directory="${cur_working_path}/converted_${timestamp}"
	else
		# if already a full path, no need to transform to full
		if [[ "${output_directory}" != /* ]]; then
			final_directory="${cur_working_path}/${output_directory}"
		fi
	fi

	# must record if the directory is created by the script
	if [[ ! -d "${final_directory}" ]]; then
		mkdir -p "${final_directory}"
		created_file_list+=("${final_directory}")
	else
		mkdir -p "${final_directory}"
	fi

}

# Remove a element from an array
function RemoveConvertedFileArrayElement
{
	local new_created_file_list=()
	local element_to_be_removed="${1}"

	for value in "${created_file_list[@]}"
	do
		if [[ "${value}" != "${element_to_be_removed}" ]]; then
			new_created_file_list+=("${value}")
		fi
	done
	created_file_list=("${new_created_file_list[@]}")
}

# This is for the print-dependencies, we print out a list ".so" file
# dependencies of ".so" file or applications
function FindSoDependencyInDeb
{
	# Formating vars
	local newline=$'\n'
	local fourspaces="    "
	local eightspaces="        "

	local source_dir="${1}"
	local debian_name="${2}"
	shift 2
	local converting_files=("$@") # take the rest elements as array items
	local final_dependency_of_a_deb="${debian_name}"
	declare -A dependency_map_of_a_deb  #key: so, value: dependency
	local dependency_list_of_a_deb=()
	local sorted_converting_files=()

	VerboseEcho "Starting the function FindSoDependencyInDeb"
	VerboseEcho "    Function argument2: ${source_dir}"
	VerboseEcho "    Function argument2: ${debian_name}"
	VerboseEcho "    Function argument3: ${converting_files[*]}"

	if [[ "${print_dependency_flag}" != true ]]; then
		return
	fi

	pushd "${source_dir}" > /dev/null

	# sort the array
	readarray -t sorted_converting_files < <(for file in "${converting_files[@]}"; do echo "${file}"; done | sort)

	for file in "${sorted_converting_files[@]}"
	do
		local so_filename
		so_filename=$(basename "${file}")
		local dependency_of_a_so="${newline}${fourspaces}${so_filename}"

		# check if a file can be "objdump" instead of look for .so, cause some
		# application also have dependencies
		if ! objdump -s "${file}" >/dev/null 2>&1; then
			continue
		fi

		# dump and grep and find result (cannot find return code, or else
		# either shellcheck or the objdump will print result)
		dump="$(objdump -x "${file}")"
		# Remove NEEDED and white spaces, then add new spaces to it
		local needed_files
		needed_files=$(echo "${dump}" | grep "NEEDED" | sed 's/NEEDED//g'| sed 's/ //g' | sort)

		# Ignore excutable or so that is without file extension that does not
		# have dependency
		if [[ -z "${needed_files}" ]]; then
			dependency_of_a_so+="${newline}${eightspaces}None"
			dependency_map_of_a_deb["${so_filename}"]="${dependency_of_a_so}"
			continue
		fi

		for depending_so_file in ${needed_files}
		do
			dependency_of_a_so+="${newline}${eightspaces}${depending_so_file}"
		done

		dependency_map_of_a_deb["${so_filename}"]="${dependency_of_a_so}"
		dependency_list_of_a_deb+=("${dependency_of_a_so}")
	done

	popd > /dev/null

	# No .so files under the debian
	if [[ ${#dependency_map_of_a_deb[@]} -eq 0 ]]; then
		final_dependency_of_a_deb+="${newline}${fourspaces}None"
		dependency_list+=("${final_dependency_of_a_deb}")

		return
	fi

	local sorted_dependency_list_of_a_deb=()
	while IFS= read -rd '' key
	do
		sorted_dependency_list_of_a_deb+=("${dependency_map_of_a_deb[${key}]}")
	done < <(printf '%s\0' "${!dependency_map_of_a_deb[@]}" | sort -z)

	# Assemble to a big string
	for so_file in "${sorted_dependency_list_of_a_deb[@]}"
	do
		final_dependency_of_a_deb+="${so_file}"
	done

	dependency_list+=("${final_dependency_of_a_deb}")
}

# Check if the pull files has missing dependencies
function BspDependencyCheck
{
	# Assume the dependecy checker lives along the repackager
	BSP_DEPENDENCY_CHECK_PATH="${script_path}/nv_bsp_dependency_check.sh"
	local check_option=""
	local pull_file_dir="${1}"
	local source_file_dir="${2}"

	if [[ -z "${pull_file_dir}" || -z "${source_file_dir}" ]]; then
		return
	fi

	# fill the options
	set +e
	if [[ "${ignore_dependendency_fail_flag}" == true ]]; then
		check_option="-i"
	fi

	# unquote the options to avoid empty option
	"${BSP_DEPENDENCY_CHECK_PATH}" -c "${pull_file_dir}" --source-directory "${source_file_dir}" ${verbose_option} ${check_option}

	local check_result=$?
	set -e
	if [[ "${check_result}" != 0 ]]; then
		EchoAndExit "ERROR: Dependency check fail"
	fi

}

# Convert one file
function ConvertOneFile
{
	local stage_dir
	local final_tar_dir
	local filename_abs="${1}"
	local filename_without_extension
	local lib_files=()
	local original_converted_deb_files_array=()
	local converted_deb_files_array=()

	filename_without_extension=$(basename "${filename_abs}" .deb)
	stage_filename="${filename_without_extension}_stage"
	stage_dir="${final_directory}/${stage_filename}"

	VerboseEcho "Begin converting file - ${filename_without_extension}.deb"

	# Extract to a temporary file
	mkdir -p "${stage_dir}"
	dpkg-deb -x "${filename_abs}" "${stage_dir}"

	created_file_list+=("${stage_dir}")

	# Repackage
	# Check Pull file, if specified, use it
	if [[ -n "${pull_file_str}" ]]; then
		# check if the pull files paths are valid
		while read -r pull_file
		do
			if [[ -z "${pull_file}" ]]; then
				break
			fi

			if [[ ! -f "${stage_dir}/${pull_file}" ]]; then
				EchoAndExit "ERROR: File \"${pull_file}\" in pullfile cannot be found"
			fi
			lib_files+=("${pull_file}")
		done < <(cat "${pull_file_str}")
	fi

	pushd "${stage_dir}" > /dev/null

	if [[ -z "${pull_file_str}" ]]; then
		# Get all files in deb and store them
		while read -r pull_file
		do
			if [[ -z "${pull_file}" ]]; then
				break
			fi

			lib_files+=("${pull_file}")
		done < <(sudo find . -type f)
	fi

	# Store the original form
	for org_lib in "${lib_files[@]}"
	do
		original_converted_deb_files_array+=("${org_lib}")
	done


	# Transform the path, if no transform option, old path arr will be empty
	# which this step is skipped
	for i in ${!old_paths[*]}
	do
		local old_p="${old_paths[${i}]}"
		local new_p="${new_paths[${i}]}"

		for idx in "${!lib_files[@]}"
		do
			lib_files[${idx}]="${lib_files[${idx}]//${old_p}/${new_p}}"
		done

		# Check if new path really used by grep return code
		if grep -q "${new_p}" <<< "${lib_files[@]}" ; then
			continue
		else
			EchoMessage "Warning: Transformation relation ${old_p}:${new_p} is not used in converting ${filename_without_extension}.deb"
		fi
	done

	# Put them in array
	for lib in "${lib_files[@]}"
	do
		converted_deb_files_array+=("${lib}")
	done

	# Move to the new paths
	# have transform option or pull option
	if [[ ${#old_paths[@]} -gt 0 || -n "${pull_file_str}" ]]; then
		local tar_stage_dirname="${filename_without_extension}_pull_stage"
		local tar_stage_dir="${final_directory}/${tar_stage_dirname}"

		# create a pull stage directory, which is the one that will actually
		# be package into tar
		# without tranform option and pull should not create this
		mkdir -p "${tar_stage_dir}"
		created_file_list+=("${tar_stage_dir}")

		for i in ${!converted_deb_files_array[*]}
		do
			local org_lib="${original_converted_deb_files_array[${i}]}"
			local trans_lib="${converted_deb_files_array[${i}]}"
			local trans_lib_abs="${tar_stage_dir}/${trans_lib}"

			# create directory on requested
			mkdir -p "$(dirname "${trans_lib_abs}")"

			cp ${verbose_option} "${stage_dir}/${org_lib}" "${trans_lib_abs}"
		done

		# the final stage file for transform or pull options should be that
		# intermediary tar stage dir
		final_tar_dir="${tar_stage_dir}"
	else
		final_tar_dir="${stage_dir}"
	fi

	# Find the .so dependency
	FindSoDependencyInDeb "${final_tar_dir}" "${filename_without_extension}.deb" "${lib_files[@]}"

	# check dependency
	if [[ -n "${pull_file_str}" ]]; then
		BspDependencyCheck "${final_tar_dir}" "${stage_dir}"
	fi

	# Package it
	local tar_option="-cf"
	tar \
		--numeric-owner \
		--owner 0 \
		--group 0 \
		--mode "${TARGET_MODE}" \
		-I lbzip2 \
		--format=posix \
		${verbose_option} "${tar_option}" "${final_directory}/${filename_without_extension}.tbz2" \
		-C "${final_tar_dir}/" "${converted_deb_files_array[@]}"

	# Add to converted list on success
	created_file_list+=("${filename_without_extension}.tbz2")

	popd > /dev/null

	VerboseEcho "Completed converting file - ${filename_without_extension}.deb"

	# Remove staging dir and file
	rm -rf "${verbose_option}" "${stage_dir}"
	RemoveConvertedFileArrayElement "${stage_dir}"
	if [[ ${#old_paths[@]} -gt 0 || -n "${pull_file_str}" ]]; then
		local tar_stage_dirname="${filename_without_extension}_pull_stage"
		local tar_stage_dir="${final_directory}/${tar_stage_dirname}"

		rm -rf "${verbose_option}" "${tar_stage_dir}"
		RemoveConvertedFileArrayElement "${tar_stage_dir}"
	fi
}

# Convert the files in convert list
function ConvertFilesInConvertList
{
	VerboseEcho "Starting the function ConvertFilesInConvertList"
	local count=1

	# Convert files one by one
	for convertfile in "${convert_list[@]}"
	do
		if [[ "${convert_all_flag}" == true && "${verbose_flag}" == false ]]; then
			printf "\r%s" "Converting file ${count}/${#convert_list[@]}"
			if (( count >= ${#convert_list[@]} )); then
				printf "\n"
			fi
		fi
		ConvertOneFile "${convertfile}"

		count="$((count + 1))"
	done
}

# Print out the dependency relation for each .so in the Debians
function PrintDependencyMessage
{
	VerboseEcho "Starting the function PrintDependencyMessage"
	if [[ "${print_dependency_flag}" != true ]]; then
		return
	fi

	if [[ -z "${dependencies_output_file}" ]]; then
		printf "\nDependencies listing:\n"
		for print_lines in "${dependency_list[@]}"
		do
			echo "${print_lines}"
		done
	else
		printf "Dependencies listing:\n" > "${dependencies_output_file}"
		created_file_list+=("${dependencies_output_file}")

		for print_lines in "${dependency_list[@]}"
		do
			echo "${print_lines}" >> "${dependencies_output_file}"
		done
	fi

}

# Print out final directory message
function PrintFinalResultMessage
{
	# Only print out output dir msg if convert action was made
	if [[ -n "${final_directory}" ]]; then
		printf "\n%s\n" "Converted files are stored in: ${final_directory}"
	fi
}

# main
InitVar
FindBspLocation "${script_path}"
ParseArgs "$@"
SetupFinalOutputDirectory "${final_directory}"
PreCheck
ConvertFilesInConvertList
PrintDependencyMessage
PrintFinalResultMessage
