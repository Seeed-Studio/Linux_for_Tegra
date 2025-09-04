#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2019-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

# This script builds atf sources in this directory.
# Usage: ./${SCRIPT_NAME}.sh [OPTIONS]
set -e

# shellcheck disable=SC2046
SCRIPT_DIR="$(dirname $(readlink -f "${0}"))"
SCRIPT_NAME="$(basename "${0}")"

source "${SCRIPT_DIR}/nvcommon_build.sh"

function usage {
        cat <<EOM
Usage: ./${SCRIPT_NAME} [OPTIONS]
This script builds atf sources in this directory.
It supports following options.
OPTIONS:
        -h              Displays this help
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
			*)
				echo "Error: Invalid option ${1}"
				usage
				exit 1
				;;
			esac
	done
}

function build_atf_sources {
	echo "Building atf sources .."

	# execute building steps
	local source_dir="${SCRIPT_DIR}/arm-trusted-firmware/"
	local target_socs=()
	target_socs=("t194" "t234")

	for target_soc in "${target_socs[@]}"; do
		echo "Building atf sources for ${target_soc}"

		"${MAKE_BIN}" -C "${source_dir}" \
			BUILD_BASE="./${NV_TARGET_BOARD}-${target_soc}" \
			CROSS_COMPILE="${CROSS_COMPILE_AARCH64}"  \
			TARGET_SOC="${target_soc}" \
			DEBUG=0 LOG_LEVEL=20 PLAT=tegra SPD=opteed V=0 \
			-j"${NPROC}" --output-sync=target

		bin="${source_dir}/${NV_TARGET_BOARD}-${target_soc}/tegra/${target_soc}/release/bl31.bin"
		if [ ! -f "${bin}" ]; then
			echo "Error: Missing output binary ${bin}"
			exit 1
		fi
	done

	echo "ATF sources compiled successfully."
}

# shellcheck disable=SC2068
parse_input_param $@
check_vars "NV_TARGET_BOARD"
build_atf_sources
