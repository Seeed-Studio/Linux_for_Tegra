#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

# This script:
#
# Updates the partitions that are a pair of "${part_name}" and "${part_name}_alt"
# with the specified "${image_file}" in following flow:
# 1. Verify the sha1sum of specified "${image_file}".
# 2. Write the "${image_file}" to "${part_name}_alt" partition.
# 3. Swap the partition name of "${part_name}" and "${part_name}_alt" partitions.
#    Swap the partition type guid as well if "${part_name}" is esp.
# 4. Erase the "${part_name}_alt" partition.

set -e

# Import functions
source ./nv_ota_common.func
source ./nv_ota_update_alt_part.func

if [ "${USER}" != "root" ]; then
    echo "${0} requires root privilege.";
    exit 1;
fi

if [ $# -lt 2 ] || [ $# -gt 4 ];then
    usage
fi

nargs=$#
part_image="${!nargs}"
nargs=$((nargs-1))
part_name="${!nargs}"

opstr="c:"
while getopts "${opstr}" OPTION; do
    case $OPTION in
    c) part_image_sha1sum=${OPTARG}; ;;
    *)
       usage
       ;;
    esac;
done

if [ ! -f "${part_image}" ]; then
    echo "${part_image}: No such file."
    exit 1
fi

# Check whether sha1sum of part_image is passed in:
# If no, generate and store sha1sum for part_image.
# If yes, check whether sha1sum file exist.
if [ "${part_image_sha1sum}" = "" ];then
    sha1_chksum_gen=$( sha1sum "${part_image}" | cut -d\  -f 1 )
    part_image_sha1sum="${part_image}.sha1sum"
    echo "${sha1_chksum_gen}" > "${part_image_sha1sum}"
    echo "Create ${part_image_sha1sum} for ${part_image}."
else
    if ! [ -f "${part_image_sha1sum}" ]; then
        echo "${part_image_sha1sum}: No such file."
        exit 1
    fi
fi

OTA_LOG_DIR=/opt/nvidia/ota_log
if [ ! -d "${OTA_LOG_DIR}" ]; then
    mkdir -p "${OTA_LOG_DIR}"
    chmod a+w "${OTA_LOG_DIR}"
fi

# Initialize log
source ./nv_ota_log.sh
echo "init_ota_log ${OTA_LOG_DIR}"
if ! init_ota_log "${OTA_LOG_DIR}"; then
    echo "Failed to run \"init_ota_log ${OTA_LOG_DIR}\""
    exit 1
fi
OTA_LOG_FILE="$(get_ota_log_file)"
ota_log "OTA_LOG_FILE=${OTA_LOG_FILE}"

ota_log "Start updating partition ${part_name} with image ${part_image}."
prerequisite_check_alt "${part_name}"
update_specified_partitions_alt "${part_name}" "${part_image}" "${part_image_sha1sum}"
