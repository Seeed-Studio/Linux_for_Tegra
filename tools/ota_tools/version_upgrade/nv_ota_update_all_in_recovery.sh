#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2019-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

# This is a script to update the images in ota package into partitions
_OTA_CONTROL_FILE=upgradetasklist.txt
_OTA_UPDATE_SCRIPT=l4t_update_partitions.sh
_OTA_SUCCESS_FILE=/tmp/ota_success
_OTA_VERSION_FILE=version.txt
_MMC_BOOT0_DEVICE="mmcblk0boot0"
_MMC_BOOT1_DEVICE="mmcblk0boot1"
_QSPI_BOOT_DEVICE="/dev/mtd0"

# For jeston-xavier, the VER_b/VER are not at the end of boot device, so
# the offset of them need to be obtained from the "flash.idx" file in the
# ota package.
_VER_PARTITION_OFFSET=
_VER_B_PARTITION_OFFSET=
_VER_PARTITION_SIZE=
_VER_B_PARTITION_SIZE=

# File marked OTA update is started
_OTA_UPDATE_START_FILE="ota_update_start"

write_ver_qspi()
{
	local ota_version_file="$1"
	local offset="$2"
	local size="$3"
	local erase_size=

	local boot_dev_sector_size=
	boot_dev_sector_size=$(cat /sys/class/mtd/mtd0/erasesize)
	local leftover=
	leftover=$((offset % boot_dev_sector_size))
	if [ ${leftover} -ne 0 ]; then
		ota_log "The writing offset is not aligned to ${boot_dev_sector_size}"
		return 1
	fi
	erase_size=$(((size + boot_dev_sector_size - 1) / boot_dev_sector_size * boot_dev_sector_size))
	echo "mtd_debug erase ${_QSPI_BOOT_DEVICE} ${offset} ${erase_size}"
	if ! mtd_debug erase "${_QSPI_BOOT_DEVICE}" "${offset}" "${erase_size}"; then
		ota_log "Failed to erase ${_QSPI_BOOT_DEVICE}"
		return 1
	fi
	echo "mtd_debug write ${_QSPI_BOOT_DEVICE} ${offset} ${size} ${ota_version_file}"
	if ! mtd_debug write "${_QSPI_BOOT_DEVICE}" "${offset}" "${size}" "${ota_version_file}"; then
		ota_log "Failed to write ${_QSPI_BOOT_DEVICE}"
		return 1
	fi
	return 0
}

# Write VER/VER_b partition
write_ver()
{
	local ota_version_file="$1"
	local target_board="$2"
	local part_name="$3"
	local ota_ver_start="$4"
	local ota_ver_size="$5"
	local ota_ver_end=
	local ota_version_file_size=
	ota_ver_end=$((ota_ver_start + ota_ver_size))
	ota_version_file_size=$(du -b "${ota_version_file}" | sed -r 's/[\t ][\t ]*/ /g' | cut -d\  -f 1)

	if [ -z "${ota_version_file_size}" ]; then
		ota_log "Failed to get the size of ${ota_version_file}"
		return 1
	fi

	if [ "${ota_version_file_size}" -gt "${ota_ver_size}" ]; then
		ota_log "The size of ${ota_version_file} is larger than the size of ${part_name} partition"
		return 1
	fi

	if [[ "${target_board}" == "jetson-xavier-nx-devkit-emmc" ]] \
		|| [[ "${target_board}" == "jetson-agx-xavier-industrial" ]]; then
		if ! write_ver_qspi "${ota_version_file}" "${ota_ver_start}" "${ota_version_file_size}"; then
			ota_log "Failed to write ${ota_version_file} into ${part_name} partition on qspi"
			return 1
		fi
		return 0
	fi

	# The offset of VER/VER_b from "flash.idx" file is for the whole
	# boot device that is composed of two parts "mmcblk0boot0" and
	# "mmcblk0boot1" with equal sizes, so need to determine which
	# device the VER/VER_b is located on.
	local boot_device_name=
	local boot_device_size=
	boot_device_size=$(cat /sys/block/${_MMC_BOOT1_DEVICE}/size)
	boot_device_size=$((boot_device_size * 512))

	# Make boot device writable and write version file into VER or VER_b partition
	echo 0 >/sys/block/${_MMC_BOOT0_DEVICE}/force_ro
	echo 0 >/sys/block/${_MMC_BOOT1_DEVICE}/force_ro

	ota_log "Writing ${part_name} partition"
	local tmp_size=
	if [ "${ota_ver_start}" -ge "${boot_device_size}" ] \
		|| [ "${ota_ver_end}" -le "${boot_device_size}" ]; then
		# VER or VER_b is either located on "mmcblk0boot0" or "mmcblk0boot1"
		if [ "${ota_ver_start}" -ge "${boot_device_size}" ]; then
			# VER or VER_b is located on "mmcblk0boot1"
			boot_device_name="${_MMC_BOOT1_DEVICE}"
			ota_ver_start=$((ota_ver_start - boot_device_size))
		else
			# VER or VER_b is located on "mmcblk0boot0"
			boot_device_name="${_MMC_BOOT0_DEVICE}"
		fi

		# Write version file into VER/VER_b partition
		if ! dd if="${ota_version_file}" of=/dev/${boot_device_name} bs=1 seek="${ota_ver_start}" conv=notrunc >/dev/null 2>&1; then
			ota_log "Failed to write ${ota_version_file} into ${part_name} partition by \"dd\" command"
			return 1
		fi
	else
		# VER or VER_b is located on both "mmcblk0boot0"
		# and "mmcblk0boot1".
		# Check the size of "${ota_version_file}" to determine
		# whether it needs to be written into only "mmcblk0boot0"
		# or both of "mmcblk0boot0" and "mmcblk0boot1"
		boot_device_name="${_MMC_BOOT0_DEVICE}"
		tmp_size=$((boot_device_size - ota_ver_start))
		if [ "${ota_version_file_size}" -le "${tmp_size}" ]; then
			# "${ota_version_file}" is written into "mmcblk0boot0" only
			if ! dd if="${ota_version_file}" of=/dev/${boot_device_name} bs=1 seek="${ota_ver_start}" conv=notrunc >/dev/null 2>&1; then
				ota_log "Failed to write ${ota_version_file} into ${part_name} partition by \"dd\" command"
				return 1
			fi
		else
			# "${ota_version_file}" is written into both "mmcblk0boot0" and "mmcblk0boot1"
			if ! dd if="${ota_version_file}" of=/dev/${boot_device_name} bs=1 seek="${ota_ver_start}" count="${tmp_size}" >/dev/null 2>&1; then
				ota_log "Failed to write ${ota_version_file} into ${part_name} partition by \"dd\" command"
				return 1
			fi
			boot_device_name="${_MMC_BOOT1_DEVICE}"
			if ! dd if="${ota_version_file}" of=/dev/${boot_device_name} bs=1 skip="${tmp_size}" conv=notrunc >/dev/null 2>&1; then
				ota_log "Failed to write ${ota_version_file} into ${part_name} partition by \"dd\" command"
				return 1
			fi
		fi
	fi
	ota_log "${part_name}(${target_board}): ota_ver_start=${ota_ver_start} ota_ver_end=${ota_ver_end}, ota_version_file_size=${ota_version_file_size}"

	sync
	return 0
}

ota_update_all_in_recovery()
{
	local ota_work_dir=$1
	local ota_update_script=${_OTA_UPDATE_SCRIPT}
	local ota_control_file=${_OTA_CONTROL_FILE}
	local ota_success_file=${_OTA_SUCCESS_FILE}

	if [ ! -d "${ota_work_dir}" ]; then
		ota_log "Invalid directory ${ota_work_dir}"
		return 1
	fi

	pushd "${ota_work_dir}" > /dev/null 2>&1 || return 1
	if [ ! -f "./${ota_update_script}" ]; then
		ota_log "OTA update script ${ota_update_script} is not found"
		popd || return 1
		return 1
	fi

	if [ ! -f "./${ota_control_file}" ]; then
		ota_log "OTA control file ${ota_control_file} is not found"
		popd || return 1
		return 1
	fi

	local ota_log_file=
	ota_log_file="$(get_ota_log_file)"
	if [ "${ota_log_file}" = "" ] || [ ! -f "${ota_log_file}" ];then
		ota_log "Not get the valid ota log path ${ota_log_file}"
		return 1
	fi
	ota_log "Get ota log file at ${ota_log_file}"

	if [ ! -f "${_OTA_VERSION_FILE}" ]; then
		ota_log "OTA version file ${_OTA_VERSION_FILE} is not found"
		return 1
	fi

	# Get target board
	local target_board=
	if [ -f "${ota_work_dir}/board_name" ]; then
		target_board="$(cat "${ota_work_dir}/board_name")"
	else
		ota_log "Target board file ${ota_work_dir}/board_name is not found"
		return 1
	fi

	# Get the offset of VER_b/VER partition
	if ! get_ver_info "${ota_work_dir}" "_VER_PARTITION_OFFSET" "_VER_B_PARTITION_OFFSET" "_VER_PARTITION_SIZE" "_VER_B_PARTITION_SIZE"; then
		ota_log "Failed to run \"get_ver_info ${ota_work_dir} _VER_PARTITION_OFFSET _VER_B_PARTITION_OFFSET _VER_PARTITION_SIZE _VER_B_PARTITION_SIZE\""
		return 1
	fi

	# Write VER_b partition
	if ! write_ver "${_OTA_VERSION_FILE}" "${target_board}" "VER_b" "${_VER_B_PARTITION_OFFSET}" "${_VER_B_PARTITION_SIZE}"; then
		ota_log "Failed to run \"write_ver ${_OTA_VERSION_FILE} ${target_board} VER_b ${_VER_B_PARTITION_OFFSET} ${_VER_B_PARTITION_SIZE}\""
		return 1
	fi

	# Marked OTA update is started
	if [ ! -f  "${ota_work_dir}/${_OTA_UPDATE_START_FILE}" ]; then
		echo 1 >"${ota_work_dir}/${_OTA_UPDATE_START_FILE}"
		sync
	fi

	ota_log "Updating partitions starts at $(date)"
	ota_log "Run source ${ota_update_script} ${target_board} ${ota_control_file} 2>&1 | tee -a ${ota_log_file}"
	source "${ota_update_script}" "${target_board}" "${ota_control_file}" 2>&1 | tee -a "${ota_log_file}"
	if [ ! -e "${ota_success_file}" ]; then
		ota_log "Error happens when running \"${ota_update_script} ${target_board} ${ota_control_file}\""
		popd || return 1
		return 1
	fi
	ota_log "Updating partitions ends at $(date)"
	sync

	# Write VER partition
	if ! write_ver "${_OTA_VERSION_FILE}" "${target_board}" "VER" "${_VER_PARTITION_OFFSET}" "${_VER_PARTITION_SIZE}"; then
		ota_log "Failed to run \"write_ver ${_OTA_VERSION_FILE} ${target_board} VER ${_VER_PARTITION_OFFSET} ${_VER_PARTITION_SIZE}\""
		return 1
	fi
	sync

	# Set boot device readonly
	if [ -e "/sys/block/${_MMC_BOOT0_DEVICE}/force_ro" ]; then
		echo 1 >/sys/block/${_MMC_BOOT0_DEVICE}/force_ro
		echo 1 >/sys/block/${_MMC_BOOT1_DEVICE}/force_ro
	fi

	# Remove the file ${_OTA_UPDATE_START_FILE}
	rm -f "${ota_work_dir}/${_OTA_UPDATE_START_FILE}"

	popd > /dev/null 2>&1 || return 1
	ota_log "Finished ./${ota_update_script} ${ota_control_file}"
	return 0
}
