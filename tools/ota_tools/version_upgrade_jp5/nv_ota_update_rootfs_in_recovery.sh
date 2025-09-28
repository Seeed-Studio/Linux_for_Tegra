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

# This is a script to update rootfs in recover kernel for the case without
# layout change. It needs to executes the following steps:
# 1. Update rootfs partition by running rootfs updater
# 2. Trigger UEFI Capsule update to update bootloader if needed
# 3. Force booting to normal kernel in next boot
# Supposedly, the device will enter into normal kernel after reboot.

_BASE_RECOVERY_DIR="base_recovery"
_EXT_DEV="external_device"
_INT_DEV="internal_device"
_NVME_DEVICE="nvme0n1"
_TOT_IMAGES_DIR="images-R36-ToT"
_ROOTFS_ENC_ENABLED=0
_ROOTFS_IMAGE_NAME="system.img"
_ROOTFS_UPDATE_SUCCESS="/tmp/rootfs_update_success"
_SDMMC_DEVICE="mmcblk0"
_UPDATE_CONTROL_FILE="update_control"
_current_storage_device=

need_to_update_bootloader()
{
	# Check whether need to update bootlaoder
	# Usage:
	#        need_to_update_bootloader() {work_dir}
	local work_dir="${1}"
	local update_control_file="${work_dir}/${_UPDATE_CONTROL_FILE}"
	local need_update=

	if [ ! -r "${update_control_file}" ]; then
		return 1
	fi

	need_update="$(grep -o "bootloader" <"${update_control_file}")"
	if [ "${need_update}" == "bootloader" ]; then
		return 0
	else
		return 1
	fi

}

update_kernel_and_dtb()
{
	# Update kernel and kernel-dtb partition on the chain A
	# Usage:
	#        update_kernel_and_dtb {work_dir} {storage_device}
	local work_dir="${1}"
	local storage_device="${2}"
	local target_dir=
	local ota_device=

	# Skip updating kernel and kernel-dtb partitions if disk encryption
	# is enabled.
	if [ "${_ROOTFS_ENC_ENABLED}" == 1 ]; then
		ota_log "Skip updating kernel and kernel-dtb for disk encryption"
		return 0
	fi

	if [[ ! "${storage_device}" =~ ${_SDMMC_DEVICE} ]]; then
		target_dir="${work_dir}/${_EXT_DEV}/${_TOT_IMAGES_DIR}"
		ota_device="/dev/${_NVME_DEVICE}"
	else
		target_dir="${work_dir}/${_INT_DEV}/${_TOT_IMAGES_DIR}"
		ota_device="/dev/${_SDMMC_DEVICE}"
	fi

	# Update kernel and kernel-dtb partitions
	local kernel_partition="A_kernel"
	local kernel_dtb_partition="A_kernel-dtb"
	ota_log "install_partition_with_ab ${target_dir} ${kernel_partition} ${ota_device}"
	if ! install_partition_with_ab "${target_dir}" "${kernel_partition}" "${ota_device}"; then
		ota_log "Failed to run \"install_partition_with_ab ${target_dir} ${kernel_partition} ${ota_device}\""
		return 1
	fi
	ota_log "install_partition_with_ab ${target_dir} ${kernel_dtb_partition} ${ota_device}"
	if ! install_partition_with_ab "${target_dir}" "${kernel_dtb_partition}" "${ota_device}"; then
		ota_log "Failed to run \"install_partition_with_ab ${target_dir} ${kernel_dtb_partition} ${ota_device}\""
		return 1
	fi
	return 0
}

update_rootfs_postprocess()
{
	# Post procedure after rootfs is updated
	# Usage:
	#        update_rootfs_postprocess {work_dir} {storage_device}
	local work_dir="${1}"
	local storage_device="${2}"

	# Both of "trigger_uefi_capsule_update" and "foce_booting_to_normal"
	# functions are defined in the "nv_ota_common.func"
	if declare -F -f trigger_uefi_capsule_update > /dev/null 2>&1; then
		source "${work_dir}"/nv_ota_common.func
	fi

	# Update kernel and kernel-dtb partitions
	ota_log "update_kernel_and_dtb ${work_dir} ${storage_device}"
	if ! update_kernel_and_dtb "${work_dir}" "${storage_device}"; then
		ota_log "Failed to run \"update_kernel_and_dtb ${work_dir} ${storage_device}\""
		return 1
	fi

	# Update L4T Launcher
	ota_log "update_l4t_launcher ${work_dir} ${storage_device}"
	if ! update_l4t_launcher "${work_dir}" "${storage_device}"; then
		ota_log "Failed to run \"update_l4t_launcher ${work_dir} ${storage_device}\""
	fi

	# Check and update misc partitions in recovery kernel
	local images_dir=
	local update_misc="false"
	if [ -f "${work_dir}/update_misc_in_recovery" ]; then
		update_misc="$(cat "${work_dir}/update_misc_in_recovery")"
	fi
	if [ "${update_misc}" == "true" ]; then
		if [[ ! "${storage_device}" =~ ${_SDMMC_DEVICE} ]]; then
			images_dir="${work_dir}/${_EXT_DEV}/${_TOT_IMAGES_DIR}"
		else
			images_dir="${work_dir}/${_INT_DEV}/${_TOT_IMAGES_DIR}"
		fi
		# The "update_misc_partitions" is exported
		# by the "nv_ota_common.func"
		ota_log "update_misc_partitions ${work_dir} ${images_dir}"
		if ! update_misc_partitions "${work_dir}" "${images_dir}"; then
			ota_log "Failed to run \"update_misc_partitions ${work_dir} ${images_dir}\""
			return 1
		fi
	fi

	# Check and update bootloader
	ota_log "need_to_update_bootloader ${work_dir}"
	if need_to_update_bootloader "${work_dir}"; then
		ota_log "trigger_uefi_capsule_update ${work_dir} ${storage_device}"
		if ! trigger_uefi_capsule_update "${work_dir}" "${storage_device}"; then
			ota_log "Failed to run \"trigger_uefi_capsule_update ${work_dir} ${storage_device}\""
			return 1
		fi
	fi

	# Force booting to normal kernel on next boot by writing UEFI variable
	ota_log "force_booting_to_normal"
	if ! force_booting_to_normal; then
		ota_log "Failed to run \"force_booting_to_normal\""
		return 1
	fi
	return 0
}

update_rootfs()
{
	# Update rootfs partition by running the rootfs_updater
	# Usage:
	#        update_rootfs ${rootfs_part} ${work_dir} ${system_img_file}
	local work_dir="${1}"
	local rootfs_part="${2}"
	local system_img_file="${3}"

	local ota_log_file=
	ota_log_file="$(get_ota_log_file)"
	if [ "${ota_log_file}" = "" ] || [ ! -f "${ota_log_file}" ];then
		ota_log "Not get the valid ota log path ${ota_log_file}"
		return 1
	fi
	ota_log "Get ota log file at ${ota_log_file}"

	# Set the correct "rootfs_part" if rootfs partition is encrypted
	local dm_crypt_root=
	if ! load_variable "dm_crypt" dm_crypt_root; then
		ota_log "Failed to run \"load_variable dm_crypt dm_crypt_root\""
		return 1
	fi
	if [ "${dm_crypt_root}" != "" ]; then
		# Set "rootfs_part" as unlocked rootfs partition
		rootfs_part="/dev/mapper/${dm_crypt_root}"
		if [ ! -e "${rootfs_part}" ]; then
			ota_log "The unlocked rootfs partition ${rootfs_part} does not exist"
			return 1
		fi
		_ROOTFS_ENC_ENABLED=1
	fi

	# Set rootfs updater and update rootfs partition with it
	local rootfs_updater=
	if ! set_rootfs_updater "${work_dir}" rootfs_updater; then
		ota_log "Failed to run \"set_rootfs_updater ${work_dir} rootfs_updater\""
		return 1
	fi
	ota_log "Updating rootfs partition ${rootfs_part} starts at $(date)"
	ota_log "Run source ${rootfs_updater} -p ${rootfs_part} -d ${work_dir} ${system_img_file} 2>&1 | tee -a ${ota_log_file}"
	source "${rootfs_updater}" -p "${rootfs_part}" -d "${work_dir}" "${system_img_file}" 2>&1 | tee -a "${ota_log_file}"
	if [ ! -e "${_ROOTFS_UPDATE_SUCCESS}" ]; then
		ota_log "Error happens when running \"${rootfs_updater} -p ${rootfs_part} -d ${work_dir} ${system_img_file}\""
		return 1
	fi
	ota_log "Updating rootfs partition ${rootfs_part} ends at $(date)"
}

apply_files_for_base_recovery()
{
	# Apply the files that might be required for doing update.
	# These files have been packed into OTA update payload.
	local work_dir="${1}"
	local base_recovery_dir="${work_dir}/${_BASE_RECOVERY_DIR}"

	# Return directly if this directory does not exist
	if [ ! -d "${base_recovery_dir}" ]; then
		return
	fi
	ota_log "Copy required files for base recovery from ${base_recovery_dir}"
	cp -rf "${base_recovery_dir}"/* /usr/
}

ota_update_rootfs_in_recovery()
{
	# Update rootfs in recovery kernel/initrd.
	# This function is called from "nv_recovery.sh".
	# It runs in following steps:
	# 1. Check whether the specified rootfs partition exists
	# 2. Check whether rootfs image exists and it is valid
	# 3. Run rootfs updater to update rootfs partition
	# 4. Update kernel and kernel-dtb partitions
	# 5. Check whether need to update bootloader
	#  5a. If yes, continue
	#  5b. If no, skip to step 7
	# 6. Trigger UEFI Capsule update to update bootloader
	# 7. Force booting to normal kernel in next boot
	#
	# Usage
	#        ota_update_rootfs_in_recovery ${rootfs_part} {work_dir}
	local rootfs_part="${1}"
	local work_dir="${2}"
	local external_device=

	# Check whether passed-in "rootfs_part" exists
	if [ ! -e "${rootfs_part}" ]; then
		echo "The partition(${rootfs_part}) does not exist"
		return 1
	fi

	# Apply files required for updating in base recovery
	ota_log "apply_files_for_base_recovery ${work_dir}"
	apply_files_for_base_recovery "${work_dir}"

	_current_storage_device="$(echo "${rootfs_part}" | sed 's/[0-9][0-9]*$//g')"
	ota_log "_current_storage_device=${_current_storage_device}"
	if [[ ! "${_current_storage_device}" =~ ${_SDMMC_DEVICE} ]]; then
		external_device="${_NVME_DEVICE}"
	fi

	# Check existence of rootfs image
	local system_img_file=
	if [ -n "${external_device}" ]; then
		system_img_file="${work_dir}/${_EXT_DEV}/${_ROOTFS_IMAGE_NAME}"
	else
		system_img_file="${work_dir}/${_INT_DEV}/${_ROOTFS_IMAGE_NAME}"
	fi
	if [ ! -f "${system_img_file}" ]; then
		ota_log "The rootfs image ${system_img_file} is not found"
		return 1
	fi

	# Update rootfs partition
	ota_log "update_rootfs ${work_dir} ${rootfs_part} ${system_img_file}"
	if ! update_rootfs "${work_dir}" "${rootfs_part}" "${system_img_file}"; then
		ota_log "Failed to run \"update_rootfs ${work_dir} ${rootfs_part} ${system_img_file}\""
		return 1
	fi

	# Post procedure after rootfs is updated
	ota_log "update_rootfs_postprocess ${work_dir} ${_current_storage_device}"
	if ! update_rootfs_postprocess "${work_dir}" "${_current_storage_device}"; then
		ota_log "Failed to run \"update_rootfs_postprocess ${work_dir} ${_current_storage_device}\""
		return 1
	fi

	return 0
}
