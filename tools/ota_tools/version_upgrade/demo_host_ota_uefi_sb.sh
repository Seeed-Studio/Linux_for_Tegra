#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

# This is a demo script running on the host machine to generate
# "uefi_secureboot_overlay_multi_specs.tar.gz" based on the received UUIDs
# from target device and send this package to the target device.
#
# Usage:
#    sudo ./tools/ota_tools/version_upgrade/demo_host_ota_uefi_sb.sh
#
set -e
LINUX_BASE_DIR="$(pwd)"
BOOTLOADER_DIR="${LINUX_BASE_DIR}/bootloader"
UUIDS_FILE="uuids.txt"
BOARD_NAME="jetson-agx-orin-devkit" # jetson-orin-nano-devkit or jetson-agx-orin-devkit-industrial
UEFI_OVERLAY_DIR="${LINUX_BASE_DIR}/bootloader/uefi_overlay"
# UEFI security keys
UEFI_SIGNING_KEY_CONF="${LINUX_BASE_DIR}/uefi_keys/uefi_keys.conf"
UEFI_ENCRYPTION_KEY="${LINUX_BASE_DIR}/uefi_enc.key"
OTA_TOOLS_DIR="${LINUX_BASE_DIR}/tools/ota_tools/version_upgrade"
OTA_SIGN_UEFI_BASE_SCRIPT="${OTA_TOOLS_DIR}/l4t_ota_sign_enc_uefi_base.sh"
# OTA packages
OTA_TOOLS_PACKAGE="${LINUX_BASE_DIR}/../ota_tools_R36.4.0_aarch64.tbz2"
OTA_PAYLOAD_PACKAGE="${BOOTLOADER_DIR}/${BOARD_NAME}/ota_payload_package.tar.gz"
OTA_UEFI_SB_OVERLAY_PACKAGE="${UEFI_OVERLAY_DIR}/uefi_secureboot_overlay_multi_specs.tar.gz"

# Target device
DEV_IP="192.168.55.1"
DEV_USERNAME="nvidia"
DEV_PASSWORD="nvidia"

rootfs_a_uuid=
rootfs_b_uuid=
uda_uuid=
chipid=0x23

function check_prequisites()
{
	if ! which sshpass >/dev/null 2>&1; then
		echo "Error: sshpass is not installed, please install it by running "\
			"\"sudo apt-get install sshpass\""
		exit 1
	fi
}

function parse_uuids_file()
{
	# Check whether "uuids.txt" exists in the /home/nvidia/
	local target_uuids_file="/home/nvidia/${UUIDS_FILE}"
	if [ ! -f "${target_uuids_file}" ]; then
		echo "The ${target_uuids_file} is not found"
		exit 1
	fi
	echo "Copying ${target_uuids_file} into ${LINUX_BASE_DIR}"
	cp -vf "${target_uuids_file}" "${LINUX_BASE_DIR}/${UUIDS_FILE}"

	# Get UUIDs from the "uuids.txt"
	rootfs_a_uuid="$(grep "rootfs_a_uuid" "${UUIDS_FILE}" | cut -d: -f 2 || true)"
	rootfs_b_uuid="$(grep "rootfs_b_uuid" "${UUIDS_FILE}" | cut -d: -f 2 || true)"
	uda_uuid="$(grep "uda_uuid" "${UUIDS_FILE}" | cut -d: -f 2 || true)"

	# Check whether "rootfs_a_uuid" exists
	if [ "${rootfs_a_uuid}" == "" ]; then
		echo "Failed to get UUIDs from ${UUIDS_FILE}"
		exit 1
	fi
}

function gen_uefi_sec_overlay_package()
{
	# Generate "uefi_secureboot_overlay_multi_specs.tar.gz"
	local cmd="${OTA_SIGN_UEFI_BASE_SCRIPT} "

	cmd+="--uefi-keys ${UEFI_SIGNING_KEY_CONF} "
	cmd+="--uefi-enc ${UEFI_ENCRYPTION_KEY} --chipid ${chipid} "
	cmd+="--rootfs-uuid ${rootfs_a_uuid} "
	if [ "${rootfs_b_uuid}" ]; then
		cmd+="--rootfs-b-uuid ${rootfs_b_uuid} "
	fi
	if [ "${uda_uuid}" ]; then
		cmd+="--uda-uuid ${uda_uuid} "
	fi

	echo "cmd: ${cmd}"
	if ! eval "${cmd}"; then
		echo "Failed to generate UEFI secureboot overlay package at ${OTA_UEFI_SB_OVERLAY_PACKAGE}"
		exit 1
	fi
}

function send_ota_required_packages()
{
	# Send "ota_tools_R36.4.0_aarch64.tbz2", "ota_payload_package.tar.gz",
	# and "uefi_secureboot_overlay_multi_specs.tar.gz" to target device.

	echo "Sending ${OTA_TOOLS_PACKAGE} to the target device"
	sshpass -p "${DEV_PASSWORD}" \
		scp -o StrictHostKeyChecking=no "${OTA_TOOLS_PACKAGE}" "${DEV_USERNAME}"@"${DEV_IP}":~/
	echo "Sending ${OTA_PAYLOAD_PACKAGE} to the target device"
	sshpass -p "${DEV_PASSWORD}" \
		scp -o StrictHostKeyChecking=no "${OTA_PAYLOAD_PACKAGE}" "${DEV_USERNAME}"@"${DEV_IP}":~/
	echo "Sending ${OTA_UEFI_SB_OVERLAY_PACKAGE} to the target device"
	sshpass -p "${DEV_PASSWORD}" \
		scp -o StrictHostKeyChecking=no "${OTA_UEFI_SB_OVERLAY_PACKAGE}" "${DEV_USERNAME}"@"${DEV_IP}":~/
	echo "Finished"
}

USERID=$(id -u)
if [ "${USERID}" -ne 0 ]; then
	echo "Please run this program as root."
	exit 0
fi

check_prequisites

parse_uuids_file

gen_uefi_sec_overlay_package

send_ota_required_packages
