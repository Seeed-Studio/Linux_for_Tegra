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

# This is a script to do OTA update for w/o partition layout change
# Usage:
#    sudo ./nv_ota_update_implement.sh <device>

# 1. Validate target board
# 2. Enable A/B redundancy if not enabled
# 3. Check whether ROOTFS A/B is enabled or not
# 4. Update rootfs if required
#   4a. Update rootfs by running rootfs updater without booting to recovery
#       kernel if ROOTFS A/B is enabled.
#   4b. Or, update rootfs by booting to recovery kernel if ROOTFS A/B is not enabled
# 5. Update bootloader if required
set -e

BASE_VERSION=
CHIP_ID=
DEFAULT_ROOTFS="APP"
DEFAULT_ROOTFS_B="${DEFAULT_ROOTFS}_b"
DEVICE_NVME="nvme0n1"
EXTERNAL_DEVICE=
EXT_DEV="external_device"
INT_DEV="internal_device"
NVME_USER_DEVICE="/dev/nvme0n1"
TOT_IMAGES_DIR="images-R36-ToT"
REBOOT_TO_RECOVERY=0
ROOTFS_AB_ENABLED=0
ROOTFS_ENC_ENABLED=0
ROOTFS_IMAGE="system.img"
SDMMC_USER_DEVICE="/dev/mmcblk0"
UEFI_SECUREBOOT_OVERLAY_PACKAGE="uefi_secureboot_overlay.tar.gz"
UPDATE_BOOTLOADER=0
UPDATE_MISC_IN_RECOVERY=
UPDATE_ROOTFS=0
UPDATE_SLOT=

get_update_control()
{
	# Determine whether to update bootloader only or update rootfs
	# only or update both bootloader and rootfs
	# Usage:
	#        get_update_control {work_dir} {_ret_update_bl} {_ret_update_rfs}
	local work_dir="${1}"
	local _ret_update_bl="${2}"
	local _ret_update_rfs="${3}"
	local update_control_file="${work_dir}/update_control"

	if [ ! -f "${update_control_file}" ]; then
		ota_log "The file ${update_control_file} is not found"
		return 1
	fi

	local line=
	local update_bl=0
	local update_rfs=0
	while read -r line
	do
		case ${line} in
		bootloader) update_bl=1; ;;
		rootfs) update_rfs=1; ;;
		*) break; ;;
		esac
	done <"${update_control_file}"

	if [ "${_ret_update_bl}" != "" ]; then
		eval "${_ret_update_bl}=${update_bl}"
	fi
	if [ "${_ret_update_rfs}" != "" ]; then
		eval "${_ret_update_rfs}=${update_rfs}"
	fi
	return 0
}

# Update partition with A/B redundency, such as kernel partition
update_partition_with_ab()
{
	# Update partition with A/B redundancy
	# Usage:
	#        update_partition_with_ab {work_dir} {partition}
	local work_dir="${1}"
	local partition="${2}"

	case ${CHIP_ID} in
		0x23)
			if [ "${UPDATE_SLOT}" == "B" ]; then
				partition="B_${partition}"
			else
				partition="A_${partition}"
			fi
			;;
		*)
			ota_log "Unsupported chip id: ${CHIP_ID}"
			return 1
			;;
	esac

	# Update partition
	local target_dir=
	if [ -n "${EXTERNAL_DEVICE}" ]; then
		target_dir="${work_dir}/${EXT_DEV}/${TOT_IMAGES_DIR}"
	else
		target_dir="${work_dir}/${INT_DEV}/${TOT_IMAGES_DIR}"
	fi
	ota_log "install_partition_with_ab ${target_dir} ${partition} ${OTA_DEVICE}"
	if ! install_partition_with_ab "${target_dir}" "${partition}" "${OTA_DEVICE}"; then
		ota_log "Failed to run \"install_partition_with_ab ${target_dir} ${partition} ${OTA_DEVICE}\""
		return 1
	fi
	return 0
}

get_unlocked_devnode()
{
	# Make sure the encrypted partition is unlocked
	# Usage:
	# get_unlocked_devnode {enc_part} {_ret_part}
	local enc_part="${1}"
	local _ret_part="${2}"

	local unlocked_part=
	unlocked_part="$(lsblk -n -r -p -o NAME,TYPE "${enc_part}" | grep crypt | cut -d\  -f 1)"
	if [ "${unlocked_part}" == "" ]; then
		ota_log "Failed to get unlocked encrypted partition"
		return 1
	fi
	ota_log "Encrypted partition is unlocked at ${unlocked_part}"

	# Umount the partition if it is already mounted
	local mount_point=
	mount_point="$(findmnt -n -o TARGET "${unlocked_part}")"
	if [ "${mount_point}" != "" ]; then
		if ! umount "${mount_point}"; then
			ota_log "Failed to run \"umount ${mount_point}\""
			return 1
		fi
	fi
	eval "${_ret_part}=${unlocked_part}"
	return 0
}

update_rootfs_runtime()
{
	# Update non-current rootfs at runtime
	# Usage:
	#        update_rootfs_runtime {work_dir} {update_rootfs_slot} {dev}
	local work_dir="${1}"
	local update_rootfs_slot="${2}"
	local dev="${3}"
	if [ "${ROOTFS_AB_ENABLED}" != 1 ]; then
		return 1
	fi

	local rootfs_image="${work_dir}/${dev}/${ROOTFS_IMAGE}"
	if [ ! -f "${rootfs_image}" ]; then
		ota_log "The rootfs image ${rootfs_image} is not found"
		return 1
	fi

	# Set rootfs updater
	local rootfs_updater=
	if ! set_rootfs_updater "${work_dir}" rootfs_updater; then
		ota_log "Failed to run \"set_rootfs_updater ${work_dir} rootfs_updater\""
		return 1
	fi

	# Set rootfs name according to the "${update_rootfs_slot}"
	local rootfs_name=
	local rootfs_part_devnode=
	local rootfs_storage=
	if [ "${update_rootfs_slot}" == "B" ]; then
		rootfs_name="${DEFAULT_ROOTFS_B}"
	else
		rootfs_name="${DEFAULT_ROOTFS}"
	fi
	# Replace the name of rootfs partition if disk encryption is enabled,
	# e.g: APP to APP_ENC, APP_b to APP_ENC_b.
	if [ "${ROOTFS_ENC_ENABLED}" == 1 ]; then
		rootfs_name="${rootfs_name/APP/APP_ENC}"
	fi
	if [ "${dev}" == "${EXT_DEV}" ]; then
		rootfs_storage="${NVME_USER_DEVICE}"
	else
		rootfs_storage="${SDMMC_USER_DEVICE}"
	fi
	get_devnode_from_name "${rootfs_name}" "${rootfs_storage}" rootfs_part_devnode
	if [ ! -e "${rootfs_part_devnode}" ];  then
		ota_log "Failed to find the ${rootfs_name} partition"
		return 1
	fi

	# Get the unlocked devnode of the encrypted partition
	local encrypted_rootfs_part=
	if [ "${ROOTFS_ENC_ENABLED}" == 1 ]; then
		encrypted_rootfs_part="${rootfs_part_devnode}"
		ota_log "get_unlocked_devnode ${encrypted_rootfs_part} rootfs_part_devnode"
		if ! get_unlocked_devnode "${encrypted_rootfs_part}" rootfs_part_devnode; then
			ota_log "Failed to run \"get_unlocked_devnode ${encrypted_rootfs_part} rootfs_part_devnode\""
			return 1
		fi
	fi

	# Run rootfs updater to update rootfs
	local cmd="${rootfs_updater} -p ${rootfs_part_devnode} -d ${work_dir} ${rootfs_image}"
	ota_log "Updating rootfs by running command: ${cmd}"
	if ! eval "${cmd}"; then
		ota_log "Failed to run command \"${cmd}\""
		return 1
	fi
	return 0
}

switch_current_rootfs()
{
	# Switch to boot from the non-current rootfs that is updated
	# Usage:
	#        switch_current_rootfs
	local _nvbootctrl=
	local update_slot=
	local ret_code=

	if [ "${UPDATE_SLOT}" == "B" ]; then
		update_slot=1
	else
		update_slot=0
	fi
	_nvbootctrl="$(which nvbootctrl)"
	"${_nvbootctrl}" set-active-boot-slot "${update_slot}" >/dev/null 2>&1
	ret_code=$?
	if [ "${ret_code}" == 0 ]; then
		ota_log "Switch to updated non-current rootfs(${UPDATE_SLOT})"
		return 0
	else
		ota_log "Failed to switch to updated non-current rootfs(${UPDATE_SLOT})"
		return 1
	fi
}

update_bootloader()
{
	# Update bootloader partitions
	# Usage:
	#        update_bootloader {work_dir}
	local work_dir="${1}"

	# Update bootloader by triggering UEFI capsule update
	# If need to reboot to recovery kernel, then will trigger UEFI
	# capsule update in recovery kernel
	if [ "${REBOOT_TO_RECOVERY}" == 0 ]; then
		ota_log "trigger_uefi_capsule_update ${OTA_WORK_DIR} ${OTA_DEVICE}"
		if ! trigger_uefi_capsule_update "${OTA_WORK_DIR}" "${OTA_DEVICE}"; then
			ota_log "Failed to run \"trigger_uefi_capsule_update ${OTA_WORK_DIR} ${OTA_DEVICE}\""
			exit 1
		fi
	fi
	ota_log "Bootloader on non-current slot(${UPDATE_SLOT}) is to be updated once device is rebooted"
}

update_rootfs_with_a_b_enabled()
{
	# Update rootfs partition when ROOTFS A/B is enabled.
	# Also, update kernel and kernel-dtb partitions on the
	# non-current chain.
	# Usage:
	#        update_rootfs_with_a_b_enabled {work_dir}
	local work_dir="${1}"
	local dev=

	# Update rootfs on non-current slot by running rootfs updater
	if [ -n "${EXTERNAL_DEVICE}" ]; then
		dev="${EXT_DEV}"
	else
		dev="${INT_DEV}"
	fi
	ota_log "update_rootfs_runtime ${work_dir} ${UPDATE_SLOT} ${dev}"
	if ! update_rootfs_runtime "${work_dir}" "${UPDATE_SLOT}" "${dev}"; then
		ota_log "Failed to run \"update_rootfs_runtime ${work_dir} ${UPDATE_SLOT} ${dev}\""
		return 1
	fi
	ota_log "Rootfs on non-current slot(${UPDATE_SLOT}) is updated"

	# Update kernel and kerne-dtb partitions
	ota_log "update_partition_with_ab ${work_dir} kernel"
	if ! update_partition_with_ab "${work_dir}" "kernel"; then
		ota_log "Failed to run \"update_partition_with_ab ${work_dir} kernel\""
		return 1
	fi
	ota_log "update_partition_with_ab ${work_dir} kernel-dtb"
	if ! update_partition_with_ab "${work_dir}" "kernel-dtb"; then
		ota_log "Failed to run \"update_partition_with_ab ${work_dir} kernel-dtb\""
		return 1
	fi

	# Update L4T Launcher
	ota_log "update_l4t_launcher ${work_dir} ${OTA_DEVICE}"
	if ! update_l4t_launcher "${work_dir}" "${OTA_DEVICE}"; then
		ota_log "Failed to run \"update_l4t_launcher ${work_dir} ${OTA_DEVICE}\""
		return 1
	fi

	# Switch current rootfs if only rootfs is updated
	if [ "${UPDATE_BOOTLOADER}" == 0 ]; then
		ota_log "switch_current_rootfs"
		if ! switch_current_rootfs; then
			ota_log "Failed to run \"switch_current_rootfs\""
			exit 1
		fi
		ota_log "Will switch to boot from rootfs(${UPDATE_SLOT}) once device is rebooted"
	fi

	# Clean rootfs chain status
	ota_log "Reset chain status for rootfs(${UPDATE_SLOT})"
	if ! reset_rootfs_chain_status "${UPDATE_SLOT}"; then
		ota_log "Failed to run \"reset_rootfs_chain_status ${UPDATE_SLOT}\""
		exit 1
	fi
}

update_rootfs_in_recovery()
{
	# Reboot to recovery kernel to update rootfs partition
	# Usage:
	#        update_rootfs_in_recovery {work_dir}
	local work_dir="${1}"

	# Force booting to recovery kernel on next boot by writing UEFI variable
	ota_log "force_booting_to_recovery"
	if ! force_booting_to_recovery; then
		ota_log "Failed to run \"force_booting_to_recovery\""
		return 1
	fi
	REBOOT_TO_RECOVERY=1
	ota_log "Rootfs is to be updated in recovery kernel once device is rebooted."
}

clean_up_boot_partition()
{
	# Clean up unnecessary directories on "/boot"
	# An "efi" directory is created under "/boot" when calling
	# nv-l4t-bootloader-config.sh in the phase of triggering OTA.
	# So clean up the unnecessary directories under "/boot".
	# Usage:
	#        clean_up_boot_partition

	# Skip cleanup if not updating misc partition in recovery kernel
	if [ "${UPDATE_MISC_IN_RECOVERY}" != 1 ]; then
		return 0
	fi

	# Skip cleanup if disk encryption is disabled
	if [ "${ROOTFS_ENC_ENABLED}" == 0 ]; then
		return 0
	fi

	# Skip cleanup if base version is newer than R36.3.0
	local base_version_str=
	local result=
	if [ "${BASE_VERSION}" == "" ]; then
		ota_log "The BASE_VERSION is NULL"
		return 1
	fi
	base_version_str="$(echo "${BASE_VERSION}" | sed -r 's/R([0-9]+)-([0-9]+)/\1.\2/')"
	compare_version "${base_version_str}" "36.3" result
	if [ "${result}" -eq 2 ]; then
		# ${base_version_str} > 36.3
		return 0
	fi

	# Umount "efi"
	if mountpoint "/boot/efi" >/dev/null 2>&1; then
		if ! umount "/boot/efi"; then
			ota_log "Failed to umount /boot/efi"
			return 1
		fi
	fi

	# Remove all subdirectories under "/boot" except "boot"
	pushd /boot > /dev/null 2>&1 || return 1
	if ! find . -maxdepth 1 -type d ! -name "boot" -exec rm -rf "{}" \;; then
		ota_log "Failed to clean up /boot"
		return 1
	fi
	popd > /dev/null 2>&1 || return 1
	return 0
}

update_rootfs_with_a_b_disabled()
{
	# Update rootfs partition in recovery kernel when ROOTFS A/B is disabled
	# Usage:
	#        update_rootfs_with_a_b_disabled {work_dir}
	local work_dir="${1}"

	# To update rootfs partition in recovery
	ota_log "update_rootfs_in_recovery ${work_dir}"
	if ! update_rootfs_in_recovery "${work_dir}"; then
		ota_log "Failed to run \"update_rootfs_in_recovery ${work_dir}\""
		return 1
	fi
	return 0
}

update_rootfs()
{
	# Update rootfs partition
	# Usage:
	#        update_rootfs {work_dir}
	local work_dir="${1}"

	# ROOTFS_AB_ENABLED=1: update rootfs on the non-current slot
	# without booting to recovery kernel.
	# ROOTFS_AB_ENABLED=0: boot to recovery kernel and then update
	# rootfs in recovery kernel.
	if [ "${ROOTFS_AB_ENABLED}" == 1 ]; then
		ota_log "update_rootfs_with_a_b_enabled ${work_dir}"
		if ! update_rootfs_with_a_b_enabled "${work_dir}"; then
			ota_log "Failed to run \"update_rootfs_with_a_b_enabled ${work_dir}\""
			return 1
		fi
	else
		ota_log "update_rootfs_with_a_b_disabled ${work_dir}"
		if ! update_rootfs_with_a_b_disabled "${work_dir}"; then
			ota_log "Failed to run \"update_rootfs_with_a_b_disabled ${work_dir}\""
			return 1
		fi
	fi
}

clean_up_ota_files()
{
	# Clean up OTA related files
	# Usage:
	#        clean_up_ota_files

	# If rootfs A/B is not enabled, clean-up is to be done
	# in recovery kernel, so return directly.
	if [ "${ROOTFS_AB_ENABLED}" != 1 ]; then
		return 0
	fi

	# Delete OTA work directory and logs.
	if [ -d "${OTA_WORK_DIR}" ]; then
		if [ "${OTA_LOG_DIR}" != "" ] && [ -d "${OTA_LOG_DIR}" ];then
			# Preserve logs if required.
			if [ "${PRESERVE_LOGS}" == "true" ]; then
				cp -rf "${OTA_LOG_DIR}" /"${PRESERVED_OTA_LOGS_DIR}"
			fi
			rm -rf "${OTA_LOG_DIR}"
		else
			# Report error if preserving the OTA log that is not found
			if [ "${PRESERVE_LOGS}" == "true" ]; then
				ota_log "Failed to perserve the OTA log as it is not found"
				exit 1
			fi
		fi
		rm -rf "${OTA_WORK_DIR}"
	fi
}

validate_uuid()
{
	# Validate UUID
	# Usage:
	#        validate_uuid {part_name} {uuid}
	local part_name="${1}"
	local uuid="${2}"
	local part_devnode=
	local part_uuid=

	# Get UUID from the partition name
	part_devnode="$(blkid | grep "${OTA_DEVICE}" | grep -m 1 "PARTLABEL=\"${part_name}\"" | cut -d: -f 1)"
	if [ "${part_devnode}" == "" ]; then
		ota_log "Failed to found the ${part_name} partition"
		return 1
	fi
	if [ "${ROOTFS_ENC_ENABLED}" == 1 ]; then
		part_uuid="$(blkid -o value -s UUID "${part_devnode}")"
	else
		part_uuid="$(blkid -o value -s PARTUUID "${part_devnode}")"
	fi

	# Compare UUID
	if [ "${part_uuid}" != "${uuid}" ]; then
		ota_log "The UUID (${part_uuid}) found at part ${part_name} on device does not match with the UUID (${uuid}) prepared in UEFI secureboot overlay package for OTA"
		return 1
	fi
	return 0
}

validate_uuids_in_uefi_overlay()
{
	# Compare UUIDs used in UEFI secureboot overlay package with
	# the UUIDs on the device.
	# Usage:
	#        validate_uuids_in_uefi_overlay {work_dir}
	local work_dir="${1}"
	local uuids_list_file="${work_dir}/uuids_list.txt"
	local rootfs_uuid=
	local rootfs_b_uuid=
	local uda_uuid=

	if [ ! -f "${uuids_list_file}" ]; then
		ota_log "The UUIDs list file ${uuids_list_file} is not found"
		return 1
	fi

	rootfs_uuid="$(grep "rootfs_uuid" "${uuids_list_file}" | cut -d: -f 2 || true)"
	rootfs_b_uuid="$(grep "rootfs_b_uuid" "${uuids_list_file}" | cut -d: -f 2 || true)"
	uda_uuid="$(grep "uda_uuid" "${uuids_list_file}" | cut -d: -f 2 || true)"

	# Validate UUIDs for rootfs partition, rootfs B partition and UDA partition
	local rootfs_name=
	local rootfs_b_name=
	local uda_name=

	if [ "${rootfs_uuid}" == "" ]; then
		ota_log "Rootfs UUID is not found in the ${uuids_list_file}"
		return 1
	fi

	if [ "${ROOTFS_ENC_ENABLED}" == 1 ]; then
		rootfs_name="APP_ENC"
		rootfs_b_name="APP_ENC_b"
	else
		rootfs_name="APP"
		rootfs_b_name="APP_b"
	fi
	uda_name="UDA"
	if ! validate_uuid "${rootfs_name}" "${rootfs_uuid}"; then
		ota_log "Failed to validate UUID ${rootfs_uuid} for ${rootfs_name} partition"
		return 1
	fi

	if [ "${ROOTFS_AB_ENABLED}" == 1 ]; then
		# Report error if the UUID for rootfs B is not found
		if [ "${rootfs_b_uuid}" == "" ]; then
			ota_log "The UUID for rootfs B must be set for a device with rootfs A/B enabled"
			return 1
		fi
		if ! validate_uuid "${rootfs_b_name}" "${rootfs_b_uuid}"; then
			ota_log "Failed to validate UUID ${rootfs_b_uuid} for ${rootfs_b_name} partition"
			return 1
		fi
	else
		# Report error if the UUID for rootfs B exists
		if [ "${rootfs_b_uuid}" != "" ]; then
			ota_log "The UUID for rootfs B should not be set for a device without rootfs A/B enabled"
			return 1
		fi
	fi
	if [ "${uda_uuid}" != "" ]; then
		if ! validate_uuid "${uda_name}" "${uda_uuid}"; then
			ota_log "Failed to validate UUID ${uda_uuid} for ${uda_name} partition"
			return 1
		fi
	fi
	return 0
}


# Make sure that this script is running in root privilege
USERID=$(id -u)
if [ "${USERID}" -ne 0 ]; then
       ota_log "Please run this program as root."
       exit 0
fi

if [ $# -ne 1 ]; then
	usage
fi

nargs=$#;
OTA_DEVICE=${!nargs};

ota_log "Command: ${BASH_SOURCE[0]}"

if [ ! -e "${OTA_DEVICE}" ]; then
	echo "Invalid disk path ${OTA_DEVICE}"
	usage
fi

source ./nv_ota_common.func
source ./nv_ota_customer.conf
source ./ota_multi_board_specs.sh
source ./nv_ota_update_alt_part.func

# Check whether current board matches with OTA package target board
ota_log "check_target_board ${OTA_WORK_DIR} TARGET_BOARD"
if ! check_target_board "${OTA_WORK_DIR}" "TARGET_BOARD"; then
	ota_log "Failed to run \"check_target_board ${OTA_WORK_DIR} TARGET_BOARD\""
	exit 1
fi

# Get the chip id
ota_log "get_chip_id CHIP_ID"
if ! get_chip_id "CHIP_ID"; then
	ota_log "Failed to run \"get_chip_id CHIP_ID\""
	exit 1
fi

# Support NVMe device as external device
if [ "${OTA_DEVICE}" == "${NVME_USER_DEVICE}" ]; then
	EXTERNAL_DEVICE="${DEVICE_NVME}"
fi

# The board specific images are stored under images-XXX-XXX/<board_spec>/ directory.
# Need to parse current target board spec and then move the images from
# corresponding board_spec directory to image-XXX-XXX.
ota_log "ota_choose_images ${OTA_WORK_DIR}"
if ! ota_choose_images "${OTA_WORK_DIR}"; then
	ota_log "Failed to run \"ota_choose_images ${OTA_WORK_DIR}\""
	exit 1
fi

# Check whether ROOTFS A/B is enabled
ota_log "is_rootfs_a_b_enabled ROOTFS_AB_ENABLED ROOTFS_CURRENT_SLOT"
if ! is_rootfs_a_b_enabled "ROOTFS_AB_ENABLED" "ROOTFS_CURRENT_SLOT"; then
	ota_log "Failed to run \"is_rootfs_a_b_enabled ROOTFS_AB_ENABLED ROOTFS_CURRENT_SLOT\""
	exit 1
fi
ota_log "ROOTFS_AB_ENABLED=${ROOTFS_AB_ENABLED}"
ota_log "ROOTFS_CURRENT_SLOT=${ROOTFS_CURRENT_SLOT}"

# Check whether disk encryption is enabled
ota_log "is_rootfs_encryption_enabled ROOTFS_ENC_ENABLED"
if ! is_rootfs_encryption_enabled "ROOTFS_ENC_ENABLED"; then
	ota_log "Failed to run \"is_rootfs_encryption_enabled ROOTFS_ENC_ENABLED\""
	exit 1
fi
ota_log "ROOTFS_ENC_ENABLED=${ROOTFS_ENC_ENABLED}"

# Get the slot to be updated
ota_log "get_update_slot UPDATE_SLOT"
if ! get_update_slot "UPDATE_SLOT"; then
	ota_log "Failed to run \"get_update_slot UPDATE_SLOT\""
	exit 1
fi
ota_log "UPDATE_SLOT=${UPDATE_SLOT}"

# Get the value of "update_control" file to determine whether
# to update bootloader or/and rootfs
ota_log "get_update_control ${OTA_WORK_DIR} UPDATE_BOOTLOADER UPDATE_ROOTFS"
if ! get_update_control "${OTA_WORK_DIR}" "UPDATE_BOOTLOADER" "UPDATE_ROOTFS"; then
	ota_log "Failed to run \"get_update_control ${OTA_WORK_DIR} UPDATE_BOOTLOADER UPDATE_ROOTFS\""
	exit 1
fi
ota_log "UPDATE_BOOTLOADER=${UPDATE_BOOTLOADER}, UPDATE_ROOTFS=${UPDATE_ROOTFS}"

# Update partitions and rootfs on storage device
if [ "${UPDATE_ROOTFS}" == 1 ]; then
	# Make sure the base version built in OTA package matches with version in rootfs.
	ota_log "check_bsp_version ${OTA_WORK_DIR} BASE_VERSION"
	if ! check_bsp_version "${OTA_WORK_DIR}" "BASE_VERSION"; then
		ota_log "Failed to run \"check_bsp_version ${OTA_WORK_DIR} BASE_VERSION\""
		exit 1
	fi

	# Validate the UUIDs used in UEFI secureboot overlay package
	if [ -f "${OTA_WORK_DIR}/${UEFI_SECUREBOOT_OVERLAY_PACKAGE}" ]; then
		if ! validate_uuids_in_uefi_overlay "${OTA_WORK_DIR}"; then
			ota_log "Failed to run \"validate_uuid_in_uefi_overlay ${OTA_WORK_DIR}\""
			exit 1
		fi
	fi

	# Check whether update misc partitions needs in recovery kernel
	# If not, do the update in current context
	if ! is_updating_misc_in_recovery "${OTA_WORK_DIR}"; then
		UPDATE_MISC_IN_RECOVERY=0
		if [ -n "${EXTERNAL_DEVICE}" ]; then
			images_dir="${OTA_WORK_DIR}/${EXT_DEV}/${TOT_IMAGES_DIR}"
		else
			images_dir="${OTA_WORK_DIR}/${INT_DEV}/${TOT_IMAGES_DIR}"
		fi
		ota_log "update_misc_partitions ${OTA_WORK_DIR} ${images_dir}"
		if ! update_misc_partitions "${OTA_WORK_DIR}" "${images_dir}"; then
			ota_log "Failed to run \"update_misc_partitions ${OTA_WORK_DIR} ${images_dir}\""
			exit 1
		fi
	else
		UPDATE_MISC_IN_RECOVERY=1
	fi

	# Clean up boot partition
	ota_log "clean_up_boot_partition"
	if ! clean_up_boot_partition; then
		ota_log "Failed to run \"clean_up_boot_partition\""
		return 1
	fi

	ota_log "update_rootfs ${OTA_WORK_DIR}"
	if ! update_rootfs "${OTA_WORK_DIR}"; then
		ota_log "Failed to run \"update_rootfs ${OTA_WORK_DIR}\""
		exit 1
	fi
fi

# Update bootloader on the non-current slot if UPDATE_BOOTLOADER is 1
if [ "${UPDATE_BOOTLOADER}" == 1 ]; then
	ota_log "check_bootloader_version ${OTA_WORK_DIR}"
	if ! check_bootloader_version "${OTA_WORK_DIR}"; then
		ota_log "Failed to run \"check_bootloader_version ${OTA_WORK_DIR}\""
		exit 1
	fi

	ota_log "update_bootloader ${OTA_WORK_DIR}"
	if ! update_bootloader "${OTA_WORK_DIR}"; then
		ota_log "Failed to run \"update_bootloader ${OTA_WORK_DIR}\""
		exit 1
	fi
fi

# Clean up OTA files
ota_log "clean_up_ota_files"
clean_up_ota_files
