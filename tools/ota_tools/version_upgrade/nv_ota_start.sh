#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

# This is a script to trigger OTA process.
# Usage:
#    sudo ./nv_ota_start.sh <ota payload package>
#
# It supports following two use cases:
# 1. OTA with partition layout change
# 2. OTA without parition layout change
#
# This script executes the followings steps
# 1. Extract the OTA payload package
# 2. Invoke nv_ota_update_implement.sh to trigger OTA update
set -e

OTA_UPDATE_PROG="nv_ota_update_implement.sh"
OTA_DEVICE=/dev/mmcblk0
OTA_PACKAGE_NAME="ota_package.tar"
OTA_WORK_DIR=/ota_work

usage()
{
	echo -ne "Usage: sudo $0 <ota payload package>\n"
	echo -ne "\tWhere <ota payload package> is the ota payload package (ota_payload_package.tar.gz)\n"
	echo -ne "Example:\n"
	echo -ne "\tsudo $0 ~/ota_payload_package.tar.gz\n"
	exit 1
}

# Make sure that this script is running in root privilege
USERID=$(id -u)
if [ "${USERID}" -ne 0 ]; then
       echo "Please run this program as root."
       exit 0
fi

if [ $# -lt 1 ]; then
	usage
fi

nargs=$#;
OTA_PAYLOAD_PACKAGE=${!nargs};
echo "Command: $0 $*"

if [ ! -f "${OTA_PAYLOAD_PACKAGE}" ]; then
	echo "Invalid ota payload package path ${OTA_PAYLOAD_PACKAGE}"
	usage
fi

# Create OTA work directory
if [ ! -d "${OTA_WORK_DIR}" ]; then
	mkdir -p "${OTA_WORK_DIR}"
	chmod a+w "${OTA_WORK_DIR}"
fi
OTA_LOG_DIR=/ota_log

# Export common functions
source ./nv_ota_common.func
source ./nv_ota_decompress_package.sh

# Make sure the required utilities for OTA are installed
if ! check_required_utilities; then
	usage
fi

# Get current OTA_DEVICE that rootfs is located on.
# By default the rootfs is on the internal eMMC, otherwise on the external NVMe.
get_ota_device OTA_DEVICE
echo "Current rootfs is on ${OTA_DEVICE}"

# Fix the potential warning about "last usable lba" in GPT
echo -e "Fix\n" | parted ---pretend-input-tty "${OTA_DEVICE}" print >/dev/null 2>&1

# Initialize log
source ./nv_ota_log.sh
echo "init_ota_log ${OTA_LOG_DIR}"
if ! init_ota_log "${OTA_LOG_DIR}"; then
	echo "Failed to run \"init_ota_log ${OTA_LOG_DIR}\""
	exit 1
fi
OTA_LOG_FILE="$(get_ota_log_file)"
ota_log "OTA_LOG_FILE=${OTA_LOG_FILE}"

# Extract the OTA payload package into OTA work directory
echo "Extract ${OTA_PAYLOAD_PACKAGE}"
if ! tar xzvf "${OTA_PAYLOAD_PACKAGE}" -C "${OTA_WORK_DIR}" >/dev/null 2>&1; then
	ota_log "Failed to run \"tar xzvf ${OTA_PAYLOAD_PACKAGE} -C ${OTA_WORK_DIR}\""
	exit 1
fi

# Update the /etc/nv_boot_control.conf to make sure the TNSPEC
# and COMPATBILE_SPEC are compatbile with the latest naming rules.
ota_log "update_nv_boot_control_in_rootfs ${OTA_WORK_DIR}"
if ! update_nv_boot_control_in_rootfs "${OTA_WORK_DIR}"; then
	ota_log "Failed to run \"nv_update_boot_control_in_rootfs ${OTA_WORK_DIR}\""
	exit 1
fi

# Check prerequisites for applying image-based OTA
ota_log "check_prerequisites"
if ! check_prerequisites; then
	ota_log "Failed to run \"check_prerequisites\""
	exit 1
fi

# Extract the OTA payload package "ota_package.tar"
ota_log "decompress_ota_package ${OTA_PACKAGE_NAME} ${OTA_WORK_DIR}"
if ! decompress_ota_package "${OTA_PACKAGE_NAME}" "${OTA_WORK_DIR}"; then
	ota_log "Failed to run \"decompress_ota_package ${OTA_PACKAGE_NAME} ${OTA_WORK_DIR}\""
	exit 1
fi

# Find "uefi_secureboot_overlay_multi_specs.tar.gz",
# and copy it into OTA work directory if exists.
find_uefi_sb_overlay "${OTA_PAYLOAD_PACKAGE}" "${OTA_WORK_DIR}"

# Call the corresponding handling routine
ota_log "${OTA_UPDATE_PROG}" "${OTA_DEVICE}"
source "${OTA_UPDATE_PROG}" "${OTA_DEVICE}"
