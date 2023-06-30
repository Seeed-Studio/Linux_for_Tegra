#!/bin/bash

# Copyright (c) 2020-2021, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
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

set -e

function ShowUsage {
	local ScriptName="$1"

	echo "Use: ${ScriptName} [--dir|-d DIR] [--help|-h] [--verbose|-v] PACKAGE_FILE"
cat <<EOF
	This script extracts L4T Debian packages with multi-threaded
	decompressor.
	Options are:
	--dir|-d DIR
				   specify target directory
	--help|-h
				   show this help
	--verbose|-v
				   enable verbose print
EOF
}

SCRIPT_NAME=$(basename "${0}")

TGETOPT=$(getopt -n "${SCRIPT_NAME}" --longoptions dir:,help,verbose -o d:hv -- "$@")

eval set -- "${TGETOPT}"

while [ $# -gt 0 ]; do
	case "$1" in
	-d|--dir) TARGET_DIR="$2"; shift ;;
	-h|--help) ShowUsage "${SCRIPT_NAME}"; exit 1 ;;
	-v|--verbose) VERBOSE="true" ;;
	--) shift; break ;;
	-*) echo "Unknown option: $@" >&2 ; ShowUsage "${SCRIPT_NAME}"; exit 1 ;;
	esac
	shift
done

if [ ! -f "${1}" ]; then
	echo "ERROR: File not found: ${DEB_FILE}"
	exit 1
fi

DEB_FILE=$(realpath "${1}")

# Use current directory as target if not specified
if [ -z "${TARGET_DIR}" ]; then
	TARGET_DIR="${PWD}"
fi

TARGET_DIR=$(realpath "${TARGET_DIR}")
if [ ! -d "${TARGET_DIR}" ]; then
	echo "ERROR: Directory not found: ${TARGET_DIR}"
	exit 1
fi

AR_OPTS="x"
if [ "${VERBOSE}" == "true" ]; then
	echo "Target direcotry: ${TARGET_DIR}"
	AR_OPTS="${AR_OPTS}v"
fi

NPROC=$(nproc)
TMP_OUTPUT_DIR=$(mktemp -d)
trap "rm -rf ${TMP_OUTPUT_DIR}" EXIT

pushd "${TMP_OUTPUT_DIR}" > /dev/null 2>&1
ar "${AR_OPTS}" "${DEB_FILE}"

if [ -f "data.tar.gz" ]; then
	unpigz --processes "${NPROC}" "data.tar.gz"
elif [ -f "data.tar.xz" ]; then
	unxz -T "${NPROC}" "data.tar.xz"
elif [ -f "data.tar.zst" ]; then
	unzstd --threads="${NPROC}" --quiet "data.tar.zst"
elif [ -f "data.tar.bz2" ]; then
	lbunzip2 -n "${NPROC}" "data.tar.bz2"
elif [ -f "data.tar" ]; then
	echo "Data file un-compressed"
else
	echo "ERROR: Unknown compression format"
	exit 1
fi

tar -C "${TARGET_DIR}" --keep-directory-symlink -xf "data.tar"
popd > /dev/null 2>&1
