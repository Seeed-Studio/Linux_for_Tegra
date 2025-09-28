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

# This is a demo script running on the target device to get UUIDs of the
# partitions on target device and then send it to OTA server.
#
# Usage:
#    ./demo_target_ota_uefi_sb.sh
#
set -e

UUIDS_FILE="uuids.txt"
OTA_SERVER_USERNAME="nvidia"
OTA_SERVER_PASSWORD="nvidia"
OTA_SERVER_IP="192.168.55.100"

rootfs_ab_enabled=
rootfs_enc_enabled=
rootfs_a_uuid=
rootfs_b_uuid=
uda_uuid=

function check_prequisites()
{
	if ! which sshpass >/dev/null 2>&1; then
		echo "Error: sshpass is not installed, please install it by running " \
			"\"sudo apt-get install sshpass\""
		exit 1
	fi
}

function check_rootfs_ab()
{
	if blkid -t PARTLABEL="APP_b" >/dev/null 2>&1; then
		rootfs_ab_enabled=1
	else
		rootfs_ab_enabled=0
	fi
}

function check_rootfs_enc()
{
	if blkid -t PARTLABEL="APP_ENC" >/dev/null 2>&1; then
		rootfs_enc_enabled=1
	else
		rootfs_enc_enabled=0
	fi
}

function get_uuid_from_name()
{
	local device="${1}"
	local name="${2}"
	local uuid_type="${3}"

	lsblk -P -n -o PARTLABEL,"${uuid_type}" "${device}" | \
		grep "PARTLABEL=\"${name}\"" | cut -d\  -f 2 | \
		cut -d= -f 2 | sed 's/^\"\(.*\)\"$/\1/'
}

function get_uuids()
{
	local rootfs_part=
	local device=

	# Get the storage device where rootfs locates
	rootfs_part="$( findmnt -n -o SOURCE --target / )"
	if [[ "${rootfs_part}" =~ crypt ]]; then
		# Get the real devnode from the node under /dev/mapper/
		rootfs_part="$(lsblk -n -r -p -s "${rootfs_part}" | grep part | cut -d\  -f 1)"
	fi
	device="${rootfs_part%p*}"

	# UUIDs is only used when disk encryption is enabled.
	# In other cases, PARTUUIDs are used.
	if [ "${rootfs_enc_enabled}" == 1 ]; then
		rootfs_a_uuid="$(get_uuid_from_name "${device}" APP_ENC UUID)"
		if [ "${rootfs_a_uuid}" == "" ];then
			echo "Error: failed get the UUID of APP_ENC"
			exit 1
		fi
		uda_uuid="$(get_uuid_from_name "${device}" UDA UUID)"
		if [ "${rootfs_ab_enabled}" == 1 ]; then
			rootfs_b_uuid="$(get_uuid_from_name "${device}" APP_ENC_b UUID)"
			if [ "${rootfs_b_uuid}" == "" ];then
				echo "Error: failed get the UUID of APP_ENC_b"
				exit 1
			fi
		fi
	else
		rootfs_a_uuid="$(get_uuid_from_name "${device}" APP PARTUUID)"
		if [ "${rootfs_a_uuid}" == "" ];then
			echo "Error: failed get the UUID of APP"
			exit 1
		fi
		if [ "${rootfs_ab_enabled}" == 1 ]; then
			rootfs_b_uuid="$(get_uuid_from_name "${device}" APP_b PARTUUID)"
			if [ "${rootfs_b_uuid}" == "" ];then
				echo "Error: failed get the UUID of APP_b"
				exit 1
			fi
		fi
	fi

	# Store UUIDs in to "uuids.txt" that is formatted as:
	#
	# rootfs_uuid:xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
	# rootfs_b_uuid:xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
	# uda_uuid:xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
	#
	# The "rootfs_uuid" always exists, but "rootfs_b_uuid"
	# and "uda_uuid" may not exist
	echo "rootfs_a_uuid=${rootfs_a_uuid}"
	echo "rootfs_a_uuid:${rootfs_a_uuid}" > "${UUIDS_FILE}"
	if [ "${rootfs_b_uuid}" != "" ]; then
		echo "rootfs_b_uuid=${rootfs_b_uuid}"
		echo "rootfs_b_uuid:${rootfs_b_uuid}" >> "${UUIDS_FILE}"
	fi
	if [ "${uda_uuid}" != "" ]; then
		echo "uda_uuid=${uda_uuid}"
		echo "uda_uuid:${uda_uuid}" >> "${UUIDS_FILE}"
	fi
}

function send_uuids()
{
	# Send "uuids.txt" to OTA server
	echo "Sending ${UUIDS_FILE} to the OTA server"
	sshpass -p "${OTA_SERVER_PASSWORD}" \
		scp -o StrictHostKeyChecking=no "${UUIDS_FILE}" "${OTA_SERVER_USERNAME}"@"${OTA_SERVER_IP}":~/
	echo "Finished"
}

check_prequisites

check_rootfs_ab

check_rootfs_enc

get_uuids

send_uuids

