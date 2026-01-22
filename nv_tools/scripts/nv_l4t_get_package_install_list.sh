#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
# This script generates a list of packages to be installed for a given BSP and
# package type and sw stack (openrm/nvgpu)
#

set -e

# Global variables
L4T_BSP_DIR=""
PACKAGE_TYPE=""

# Default to empty array for package subdirectories
PACKAGE_SUBDIRS=()
PACKAGE_EXT=""
PACKAGE_DIRS=()

function ShowUsage {
	local ScriptName=${1}

	echo "Use: ${ScriptName} [--bsp|-b PATH] [--type|-t TYPE] [--openrm] [--nvgpu] [--help|-h]"
cat <<EOF
	This script generates a list of packages to be installed for a given BSP.
	It scans the specified BSP directory for package files and outputs their paths
	based on the provided parameters.

	Required Options:
	--bsp|-b PATH
				   Root directory of the BSP containing package files.
				   This directory should contain the necessary package directories.

	--type|-t TYPE
				   Type of packages to list. Must be one of:
				   - 'deb': List Debian packages (.deb files)
				   - 'tbz2': List tbz2 packages (.tbz2 files)

	Optional Options:
	--openrm
				   Include packages for OpenRM configuration.
				   Default: disabled

	--nvgpu
				   Include packages for NvGPU configuration.
				   Default: enabled

	--help|-h
				   Show this help message and exit

	Examples:
		${ScriptName} --bsp /path/to/bsp --type deb
			Lists all Debian packages in the BSP directory

		${ScriptName} --bsp /path/to/bsp --type tbz2 --openrm
			Lists all tbz2 packages for openrm configuration

		${ScriptName} --bsp /path/to/bsp --type deb --nvgpu
			Lists all Debian packages for nvgpu configuration

	Note:
		- The script will output the full paths of all matching package files
		- Package organization may vary based on the BSP structure
		- The script supports different package variants based on configuration
EOF
}

function ParseCommandLine {
	local SCRIPT_NAME
	SCRIPT_NAME=$(basename "${0}")

	local TGETOPT
	TGETOPT=$(getopt -n "${SCRIPT_NAME}" --longoptions help,bsp:,type:,openrm,nvgpu -o b:t:h -- "${@}")

	if ! getopt -n "${SCRIPT_NAME}" --longoptions help,bsp:,type:,openrm,nvgpu -o b:t:h -- "${@}" > /dev/null; then
		echo "Terminating... wrong switch"
		ShowUsage "${SCRIPT_NAME}"
		exit 1
	fi

	eval set -- "${TGETOPT}"

	PACKAGE_SUBDIRS=()

	while [ ${#} -gt 0 ]; do
		case "${1}" in
			--bsp|-b) L4T_BSP_DIR="${2}"; shift ;;
			--type|-t) PACKAGE_TYPE="${2}"; shift ;;
			--openrm) PACKAGE_SUBDIRS+=("openrm") ;;
			--nvgpu) PACKAGE_SUBDIRS+=("nvgpu") ;;
			-h|--help) ShowUsage "${SCRIPT_NAME}"; exit 1 ;;
			--) shift; break ;;
			-*) echo "Terminating... wrong switch: ${*}" >&2; ShowUsage "${SCRIPT_NAME}"; exit 1 ;;
		esac
		shift
	done

	# If no options were specified, use nvgpu as default
	if [ ${#PACKAGE_SUBDIRS[@]} -eq 0 ]; then
		PACKAGE_SUBDIRS=("nvgpu")
	fi
}

function ValidateParameters {
	if [ -z "${L4T_BSP_DIR}" ]; then
		echo "Error: --bsp parameter is required"
		ShowUsage "$(basename "${0}")"
		exit 1
	fi

	if [ -z "${PACKAGE_TYPE}" ]; then
		echo "Error: --type parameter is required"
		ShowUsage "$(basename "${0}")"
		exit 1
	fi

	if [ "${PACKAGE_TYPE}" != "deb" ] && [ "${PACKAGE_TYPE}" != "tbz2" ]; then
		echo "Error: --type must be either 'deb' or 'tbz2'"
		ShowUsage "$(basename "${0}")"
		exit 1
	fi
}

function SetupPackageDirectories {
	local BASE_DIR="${L4T_BSP_DIR}/nv_tegra"

	if [ "${PACKAGE_TYPE}" = "deb" ]; then
		PACKAGE_EXT=".deb"
		COMMON_PACKAGE_DIRS=(
			"${BASE_DIR}/l4t_deb_packages"
			"${L4T_BSP_DIR}/tools"
			"${L4T_BSP_DIR}/kernel"
			"${L4T_BSP_DIR}/bootloader"
		)
	else
		PACKAGE_EXT=".tbz2"
		COMMON_PACKAGE_DIRS=(
			"${BASE_DIR}"
			"${L4T_BSP_DIR}/kernel"
		)
	fi

	# Include subdirectories for each package directory
	for dir in "${COMMON_PACKAGE_DIRS[@]}"; do
		if [ -d "${dir}" ]; then
			PACKAGE_DIRS+=("${dir}")
		fi
		for subdir in "${PACKAGE_SUBDIRS[@]}"; do
			if [ -d "${dir}/${subdir}" ]; then
				PACKAGE_DIRS+=("${dir}/${subdir}")
			fi
		done
	done
}

function GetPackageList {
	local dir="${1}"

	# Find and print packages from top level directory
	if [ ! -d "${dir}" ]; then
		return
	fi
	find "${dir}" -maxdepth 1 -type f -name "*${PACKAGE_EXT}" -print
}

function ProcessPackageDirectories {
	# Process each package directory
	for dir in "${PACKAGE_DIRS[@]}"; do
		GetPackageList "${dir}"
	done
}

function Main {
	ParseCommandLine "${@}"
	ValidateParameters
	SetupPackageDirectories
	ProcessPackageDirectories
}

# Execute main function with all command line arguments
Main "${@}"
