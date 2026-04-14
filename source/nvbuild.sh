#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2019-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

# This script builds kernel sources in this directory.
# Usage: ./${SCRIPT_NAME}.sh [OPTIONS]
set -e

SCRIPT_DIR="$(dirname $(readlink -f "${0}"))"
SCRIPT_NAME="$(basename "${0}")"
ENABLE_RT=0
OOT_MODULES_ONLY=0
DO_INSTALL=0
INCREMENTAL_BUILD=0

source "${SCRIPT_DIR}/kernel_src_build_env.sh"

function usage {
        cat <<EOM
Usage: ./${SCRIPT_NAME} [OPTIONS]
This script builds kernel sources in this directory.
It supports following options.
OPTIONS:
        -h              Displays this help
        -r              Enable RT Kernel
        -m              Build/install NVIDIA OOT modules only (skip kernel)
        -i              Perform install to INSTALL_MOD_PATH
        -o <outdir>     Creates kernel build output in <outdir>
        -I              Enable incremental build (only compile modified files)
EOM
}

# parse input parameters
function parse_input_param {
	while [ $# -gt 0 ]; do
		case ${1} in
			-h)
				usage
				exit 0
				;;
			-r)
				ENABLE_RT=1
				shift 1
				;;
			-o)
				KERNEL_OUT_DIR="${2}"
				shift 2
				;;
			-m)
				OOT_MODULES_ONLY=1
				shift 1
				;;
			-i)
				DO_INSTALL=1
				shift 1
				;;
			-I)
				INCREMENTAL_BUILD=1
				shift 1
				;;
			*)
				echo "Error: Invalid option ${1}"
				usage
				exit 1
				;;
			esac
	done
}

# Function to kernel sources 
function sync_kernel_source {
	# Check whether kernel source dir available
	if [ ! -d "kernel/${KERNEL_SRC_DIR}" ]; then
		echo "Directory kernel/${KERNEL_SRC_DIR} is not found, exiting.."
		exit 1
	fi

	if [ "$INCREMENTAL_BUILD" -eq 1 ] && [ -d "${KERNEL_OUT_DIR}/kernel/${KERNEL_SRC_DIR}" ]; then
		echo "Using incremental build mode, syncing only modified files in kernel/${KERNEL_SRC_DIR}"
		# Using rsync without --delete to keep object files that were already built
		rsync -a "kernel/${KERNEL_SRC_DIR}/" "${KERNEL_OUT_DIR}/kernel/${KERNEL_SRC_DIR}/"
		cp -a kernel/Makefile "${KERNEL_OUT_DIR}/kernel/"
	else
		# Full sync kernel source directory to output dir
		echo "Syncing kernel/${KERNEL_SRC_DIR}"
		rsync -a --delete "kernel/${KERNEL_SRC_DIR}" "${KERNEL_OUT_DIR}/kernel/"
		cp -a kernel/Makefile "${KERNEL_OUT_DIR}/kernel/"
	fi
}

# Function to rsync oot modules sources 
function sync_oot_modules {
	# Check whether all source directories all available from kernel source list
	for list in ${OOT_SOURCE_LIST}
	do
		if [ ! -d "${list}" ]; then
			echo "Directory \"${list}\" is not found, exiting.."
			exit 1
		fi
	done

	if [ "$INCREMENTAL_BUILD" -eq 1 ]; then
		# Incremental sync for OOT modules
		echo "Using incremental build mode, syncing only modified files for OOT modules"
		for list in ${OOT_SOURCE_LIST}
		do
			if [ -d "${KERNEL_OUT_DIR}/$(basename ${list})" ]; then
				echo "Incrementally syncing ${list}"
				rsync -a "${list}/" "${KERNEL_OUT_DIR}/$(basename ${list})/"
			else
				# First time for this module, do full sync
				echo "First time syncing ${list}"
				rsync -a "${list}" "${KERNEL_OUT_DIR}"
			fi
		done
		cp -a Makefile "${KERNEL_OUT_DIR}/"
	else
		# Full sync module directories to output
		for list in ${OOT_SOURCE_LIST}
		do
			echo "Syncing ${list}"
			rsync -a --delete "${list}" "${KERNEL_OUT_DIR}"
		done
		cp -a Makefile "${KERNEL_OUT_DIR}/"
	fi
}

function build_arm64_kernel_sources {
	source_dir="${SCRIPT_DIR}/kernel/${KERNEL_SRC_DIR}/"
	kernel_out_dir="${KERNEL_OUT_DIR}/kernel/"

	# Disable RT if it is enabled previously
	if [ "$ENABLE_RT" -eq 0 ] && grep -q "CONFIG_PREEMPT_RT=y" "${source_dir}/arch/arm64/configs/${KERNEL_DEF_CONFIG}"; then
		./generic_rt_build.sh "disable"
	fi

	# Enable RT
	if [ "$ENABLE_RT" -eq 1 ]; then
		./generic_rt_build.sh "enable"
	fi

	# sync kernel source to output dir
	sync_kernel_source

	export KERNEL_SRC_DIR="${KERNEL_SRC_DIR}"
	export KERNEL_DEF_CONFIG="${KERNEL_DEF_CONFIG}"

	echo "Building kernel sources in ${kernel_out_dir}"
	make -C "${kernel_out_dir}"

	image="${kernel_out_dir}/${KERNEL_SRC_DIR}/arch/arm64/boot/Image"
	if [ ! -f "${image}" ]; then
		echo "Error: Missing kernel image ${image}"
		exit 1
	fi
	echo "Kernel sources compiled successfully."

	# Set KERNEL_HEADERS to be used later by the OOT modules build.
	export KERNEL_HEADERS="${kernel_out_dir}/${KERNEL_SRC_DIR}"
}

function build_oot_modules
{
	# sync oot modules sources to output dir
	sync_oot_modules

	if [ "${ENABLE_RT}" -eq 1 ]; then
		export IGNORE_PREEMPT_RT_PRESENCE=1
	fi

	echo "Building NVIDIA OOT modules sources in ${KERNEL_OUT_DIR}"
	echo "Using KERNEL_HEADERS from directory : ${KERNEL_HEADERS}"
	make -C "${KERNEL_OUT_DIR}" modules

	echo "Building NVIDIA DTBs"
	make -C "${KERNEL_OUT_DIR}" dtbs

	gpu_out="${KERNEL_OUT_DIR}/nvgpu/drivers/gpu/nvgpu/nvgpu.ko"
	if [ ! -f "${gpu_out}" ]; then
		echo "Error: Missing nvgpu.ko in \"${gpu_out}\""
		exit 1
	fi
	echo "Modules compiled successfully."
}

function install_kernel {
	kernel_out_dir="${KERNEL_OUT_DIR}/kernel/"

	export KERNEL_SRC_DIR="${KERNEL_SRC_DIR}"

	echo "Installing kernel.."
	sudo -E make -C "${kernel_out_dir}" install

	# Set KERNEL_HEADERS to be used later by the OOT modules install.
	export KERNEL_HEADERS="${kernel_out_dir}/${KERNEL_SRC_DIR}"
}

function install_oot_modules {
	echo "Installing NVIDIA OOT modules.."
	sudo -E make -C "${KERNEL_OUT_DIR}" INSTALL_MOD_DIR=updates modules_install
}

# Function to check build environment
function check_env_vars {
	MACHINE=$(uname -m)
	if [[ "${MACHINE}" =~ "x86" ]]; then
		if [ -z "${CROSS_COMPILE}" ]; then
			echo "Error: Env variable CROSS_COMPILE is not set!!"
			exit 1
		fi
		if [ ! -f "${CROSS_COMPILE}gcc" ]; then
			echo "Error: Path ${CROSS_COMPILE}gcc does not exist."
			exit 1
		fi
		if [ "${OOT_MODULES_ONLY}" -eq 1 ]; then
			if [ -z "${KERNEL_HEADERS}" ]; then
				echo "Error: Env variable KERNEL_HEADERS is not set!!"
				exit 1
			fi
		fi
		if [ "${DO_INSTALL}" -eq 1 ]; then
			if [ -z "${INSTALL_MOD_PATH}" ]; then
				echo "Error: Env variable INSTALL_MOD_PATH is not set!!"
				exit 1
			fi
		fi
	fi
}

# Parse input params
parse_input_param "$@"

# Check the build environment
check_env_vars

# Set the kernel output to out if not defined.
KERNEL_OUT_DIR="${KERNEL_OUT_DIR:=${PWD}/kernel_out}"

# Use absolute path for the out directory
if [ "${KERNEL_OUT_DIR}" == "${KERNEL_OUT_DIR#/}" ]; then
	 echo "The output directory \"${KERNEL_OUT_DIR}\" is not an absolute path"
	 echo "Making the output directory as \"${PWD}/${KERNEL_OUT_DIR}\""
	 KERNEL_OUT_DIR="${PWD}/${KERNEL_OUT_DIR}"
fi
echo "The output directory \"${KERNEL_OUT_DIR}\""

# Linux kernel does not support path with spaces or colons
pattern=" |:"
if [[ "${KERNEL_OUT_DIR}" =~ ${pattern} ]] ; then
	echo "Error: Build directory cannot contain spaces or colons!"
	exit 1
fi

# Perform install
if [ "${DO_INSTALL}" -eq 1 ]; then
	if [ ! -d "${KERNEL_OUT_DIR}" ]; then
		echo "Error: The output directory \"${KERNEL_OUT_DIR}\" is not found!"
		echo "Perform build before install."
		exit 1
	fi

	if [ "${OOT_MODULES_ONLY}" -eq 0 ]; then
		install_kernel
	fi

	install_oot_modules
	exit 0
fi

# Create out directory if not exist
if [ ! -d "${KERNEL_OUT_DIR}" ]; then
	mkdir -p "${KERNEL_OUT_DIR}/kernel"
fi

# Compile kernel sources for "arm64"
if [ "${OOT_MODULES_ONLY}" -eq 0 ]; then
	build_arm64_kernel_sources
fi

# Compile oot modules for "arm64"
if [ "${KERNEL_MODULAR_BUILD}" = "y" ]; then
	build_oot_modules
fi
