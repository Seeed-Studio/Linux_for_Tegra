#!/bin/bash

# Copyright (c) 2019-2022, NVIDIA CORPORATION.  All rights reserved.
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
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

# This is a script to run tasks automatically in recovery mode
set -e

source /bin/nv_ota_internals.sh
source /bin/nv_ota_log.sh
source /bin/nv_ota_utils.func

OTA_RUN_TASKS_SCRIPT="nv_ota_run_tasks.sh"
INTERNAL_DEVICE="/dev/mmcblk0p1"
# OTA update rootfs on devices other than eMMC such as SD, USB, and NVMe
EXTERNAL_DEVICES=(
	"/dev/mmcblk?p1"
	"/dev/sd?1"
	"/dev/nvme?n1p1"
)

rootfs_part=

function find_ota_work_dir()
{
	local device="${1}"

	if ! mount "${device}" "${OTA_PACKAGE_MOUNTPOINT}"; then
		ota_log "Failed to mount ${device} to ${OTA_PACKAGE_MOUNTPOINT}"
		return 1
	fi

	if [ ! -d "${OTA_WORK_DIR}" ];then
		ota_log "OTA work directory ${OTA_WORK_DIR} is not found on ${device}"
		umount "${OTA_PACKAGE_MOUNTPOINT}"
		return 1
	fi

	return 0
}

function find_ota_work_dir_on_external()
{
	ota_log "Finding OTA work dir on external storage devices"
	for ext_dev in "${EXTERNAL_DEVICES[@]}"; do
		echo "Checking whether device ${ext_dev} exist"
		ext_devices="$(eval ls "${ext_dev}" 2>/dev/null || true)"
		if [ "${ext_devices}" == "" ]; then
			echo "Device ${ext_dev} does not exist"
			continue
		fi
		echo "Looking for OTA work directory on the device(s): ${ext_devices}"
		ext_devices=(${ext_devices// /})
		for device in "${ext_devices[@]}"; do
			if [ "${device}" == "/dev/mmcblk0p1" ]; then
			    # Skip internal device
			    continue
			fi
			if ! find_ota_work_dir "${device}"; then
				continue
			fi
			# OTA work directory is always stored on the rootfs partition
			ota_log "Set rootfs=${device}"
			rootfs_part="${device}"
			store_variable "rootfs_part"
			return 0
		done
	done

	return 1
}


function find_ota_work_dir_on_internal()
{
	ota_log "Finding OTA work dir on internal storage device"
	if ! find_ota_work_dir "${INTERNAL_DEVICE}"; then
		return 1
	fi

	ota_log "Set rootfs=${INTERNAL_DEVICE}"
	rootfs_part="${INTERNAL_DEVICE}"
	store_variable "rootfs_part"

	return 0
}

function enter_bash_shell()
{
	local message="${1}"

	if [ "${message}" != "" ]; then
		ota_log "${message}"
	fi
	/bin/bash
}

set +e
if ! find_ota_work_dir_on_external; then
	if ! find_ota_work_dir_on_internal; then
		enter_bash_shell "OTA work directory is not found on internal and external storage devices"
	fi
fi

if [ -x "${OTA_WORK_DIR}/${OTA_RUN_TASKS_SCRIPT}" ]; then
	pushd "${OTA_WORK_DIR}"
	eval "./${OTA_RUN_TASKS_SCRIPT}"
	if [ $? -ne 0 ]; then
		enter_bash_shell "Failed to run ${OTA_RUN_TASKS_SCRIPT}"
	fi
	popd
else
	enter_bash_shell "OTA task runner ${OTA_RUN_TASKS_SCRIPT} is not found"
fi
