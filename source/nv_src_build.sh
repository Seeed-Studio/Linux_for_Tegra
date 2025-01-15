#!/bin/bash

# Copyright (c) 2019-2020, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#  * Neither the name of NVIDIA CORPORATION nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#
# This script calls "nv_*_src_build.sh" scripts in sub-directories.
#

set -e

function usage {
	cat <<EOM
Usage: ${SCRIPT_NAME} [OPTIONS]
This build script will compile/build all src tar components inside of this
directory.
It supports following options.
OPTIONS:
        -h                      displays this help
EOM
}

function parse_input_param {
	# Get input parameters
	while getopts ":h" opt; do
		case $opt in
			h)
				usage
				exit 0
				;;
			*)
				echo "Error: Invalid option!"
				usage
				exit 1
				;;
		esac
	done
}

function build_sources {
	for build_script in $(find -name "nv_*_src_build.sh"); do
		echo "Running ${build_script}"
		"${build_script}" ${@}
	done
}

SCRIPT_NAME="$(basename "${0}")"
SCRIPT_ABS_PATH="$(readlink -f "${0}")"

export BSP_PATH="${SCRIPT_ABS_PATH%/source/${SCRIPT_NAME}}"
export BUILD_DIR="${BSP_PATH}/source/src_out"

parse_input_param ${@}
build_sources ${@}
