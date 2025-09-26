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

#
# This is a script to update rootfs partition. NVIDIA provides this script
# as an exmple for updating rootfs partition. Customers can write their
# own scripts to update rootfs partition by referring to this script
#
# This script is to be renamed as "rootfs_updater.sh" and packed into
# OTA payload package when executing the script "l4t_generate_ota_package.sh"
# without specifying any customer provided script for updating rootfs
# partition.
#
# Usage:
#     sudo nv_ota_rootfs_updater.sh [-p <rootfs partition devnode>] \
#	[-d <OTA work dir>] <rootfs image>
#
#
set -e

_APP_B_NAME="APP_b"
_APP_ENC_B_NAME="APP_ENC_b"
_BOOT_IMAGE="system_boot.img.raw"
_BOOT_IMAGE_B="system_boot.img_b.raw"
_BOOT_PARTITION_EXIST=0
_COMMON_TAR_OPTIONS=( --checkpoint=10000 --warning=no-timestamp --numeric-owner --xattrs --xattrs-include=* )
_ENTRY_TABLE_FILE="opt/ota_package/entry_table"
_ENTRY_TABLE_BACKUP="entry_table.ota_backup"
_EXTLINUX_CONF_BACKUP="extlinux.conf.ota_backup"
_EXTLINUX_CONF_FILE="boot/extlinux/extlinux.conf"
_FSTAB_BACKUP="fstab.ota_backup"
_FSTAB_FILE="etc/fstab"
_EXT_DEV="external_device"
_INT_DEV="internal_device"
_INITRD_BACKUP="initrd.ota_backup"
_INITRD_FILE="boot/initrd"
_NVME_DEVICE="/dev/nvme0n1"
_NV_BOOT_CONTROL_CONF="etc/nv_boot_control.conf"
_NV_BOOT_CONTROL_CONF_BACKUP="ota_nv_boot_control.conf"
_OTA_WORK_DIR=
_ROOTFS_ENC_ENABLED=0
_ROOTFS_OVERLAY_PACKAGE_A="rootfs_overlay_a.tar.gz"
_ROOTFS_OVERLAY_PACKAGE_B="rootfs_overlay_b.tar.gz"
_ROOTFS_PART_DEV=
_ROOTFS_UPDATE_SUCCESS_FLAG="/tmp/rootfs_update_success"
_SDMMC_USER_DEVICE="/dev/mmcblk0"
_TOT_IMAGES_DIR="images-R35-ToT"

usage()
{
	echo -ne "Usage: sudo $0 [-p <rootfs partition name or devnode>] [-d <OTA work dir>] <rootfs image>\n"
	echo -ne "\t-p <rootfs partition name>: specify the name or devnode of rootfs partition, such as APP or /dev/mmcblk0p1.\n"
	echo -ne "\t-d <OTA work dir>: specify the path of OTA work directory that OTA payload package is decompressed into.\n"
	echo -ne "\t<rootfs image>: specify the compressed rootfs image.\n"
	echo -ne "Example:\n"
	echo -ne "\tsudo $0 -p APP -d /ota_work /ota_work/internal_device/system.img\n"
	exit 1
}

is_rootfs_partition_encrypted()
{
	# Check whether rootfs partition is encrypted
	# Usage:
	#        is_rootfs_partition_encrypted {rootfs_partition}
	local rootfs_partition="${1}"
	if [[ "${rootfs_partition}" =~ "/dev/mapper" ]]; then
		return 0
	else
		return 1
	fi
}

is_rootfs_partition_app_enc()
{
	# Check whether it is APP_ENC/APP_ENC_b
	# Usage:
	#        is_rootfs_partition_app_enc ${rootfs_partition}
	local rootfs_partition="${1}"
	local rootfs_name=
	rootfs_name="$(blkid -o value -s PARTLABEL "${rootfs_partition}")"
	if [[ "${rootfs_name}" =~ "ENC" ]]; then
		return 0
	else
		return 1
	fi
}

back_up_file()
{
	# Back up the specified file from rootfs to OTA work directory
	# Usage:
	#        back_up_file {src} {dst}
	local src="${1}"
	local dst="${2}"

	# If ${dst} exists, return as the file has been
	# backed up.
	# Otherwise, back up the file ${src} to ${dst}
	if [ -f "${dst}" ]; then
		echo "The file ${src} has been backed up to ${dst}"
	elif [ -f "${src}" ]; then
		echo "Backing up ${src} to ${dst}"
		cp -f "${src}" "${dst}"
	else
		echo "Warning: the specified file ${src} is not found"
	fi
}

back_up_specified_files()
{
	# Back up necessary system files and customer specified files
	# Usage:
	#         back_up_specified_files {work_dir} {root_dir}
	local work_dir="${1}"
	local root_dir="${2}"
	local orig_file=
	local backup_file=

	# Backup the extlinux.conf
	orig_file="${root_dir}/${_EXTLINUX_CONF_FILE}"
	backup_file="${work_dir}/${_EXTLINUX_CONF_BACKUP}"
	echo "back_up_file ${orig_file} ${backup_file}"
	back_up_file "${orig_file}" "${backup_file}"

	# Back up the nv_boot_control.conf
	orig_file="${root_dir}/${_NV_BOOT_CONTROL_CONF}"
	backup_file="${work_dir}/${_NV_BOOT_CONTROL_CONF_BACKUP}"
	echo "back_up_file ${orig_file} ${backup_file}"
	back_up_file "${orig_file}" "${backup_file}"

	# Back up the fstab
	orig_file="${root_dir}/${_FSTAB_FILE}"
	backup_file="${work_dir}/${_FSTAB_BACKUP}"
	echo "back_up_file ${orig_file} ${backup_file}"
	back_up_file "${orig_file}" "${backup_file}"

	# Back up customer's files
	echo "ota_backup_customer_files ${work_dir} ${root_dir}"
	if ! ota_backup_customer_files "${work_dir}" "${root_dir}"; then
		echo "Failed to run \"ota_backup_customer_files ${work_dir} ${root_dir}\""
		return 1
	fi
}

restore_file()
{
	# Restore the specified file from OTA work directory to rootfs
	# Usage:
	#     restore_file {src} {dst}
	local src="${1}"
	local dst="${2}"

	if [ ! -f "${src}" ]; then
		echo "Warning: the file ${src} is not found"
	elif [ ! -d "${dst%/*}" ]; then
		echo "Warning: the directory ${dst%/*} is not found"
	else
		echo "Restoring the file ${src} to ${dst}"
		cp -f "${src}" "${dst}"
	fi
}

restore_specified_files()
{
	# Restore necessary system files and customer specified files
	# Usage:
	#         restore_specified_files {work_dir} {root_dir}
	local work_dir="${1}"
	local root_dir="${2}"
	local restore_file=
	local target_file=

	# Restore customer's files
	echo "ota_restore_customer_files ${work_dir} ${root_dir}"
	if ! ota_restore_customer_files "${work_dir}" "${root_dir}"; then
		echo "Failed to run \"ota_restore_customer_files ${work_dir} ${root_dir}\""
		return 1
	fi

	local base_version=
	base_version="$(cat "${work_dir}/base_version")"

	# Save the original fstab to fstab.${base_version}
	restore_file="${work_dir}/${_FSTAB_BACKUP}"
	target_file="${root_dir}/${_FSTAB_FILE}.${base_version}"
	echo "restore_file ${restore_file} ${target_file}"
	restore_file "${restore_file}" "${target_file}"

	# Save the original nv_boot_control.conf to nv_boot_control.conf.${base_version}
	restore_file="${work_dir}/${_NV_BOOT_CONTROL_CONF_BACKUP}"
	target_file="${root_dir}/${_NV_BOOT_CONTROL_CONF}.${base_version}"
	echo "restore_file ${restore_file} ${target_file}"
	restore_file "${restore_file}" "${target_file}"

	# Save the original extlinux.conf to extlinux.conf.${base_version}
	restore_file="${work_dir}/${_EXTLINUX_CONF_BACKUP}"
	target_file="${root_dir}/${_EXTLINUX_CONF_FILE}.${base_version}"
	echo "restore_file ${restore_file} ${target_file}"
	restore_file "${restore_file}" "${target_file}"

	# Restore entry table from OTA work directory if it exists
	# If bootloader is successfully updated, the entry table is backed
	# up at ${work_dir}/${_ENTRY_TABLE_BACKUP}. And it should be
	# restored to ${root_dir}/${_ENTRY_TABLE_FILE}
	restore_file="${work_dir}/${_ENTRY_TABLE_BACKUP}"
	target_file="${root_dir}"/"${_ENTRY_TABLE_FILE}"
	echo "restore_file ${restore_file} ${target_file}"
	restore_file "${restore_file}" "${target_file}"
}

update_kernel_dtb()
{
	# Fix kernel-dtb in /boot/dtb/
	# Usage:
	#        update_kernel_dtb {work_dir} {root_dir}
	local work_dir="${1}"
	local root_dir="${2}"
	local dev="${3}"
	local images_dir
	local idx_file=
	local kernel_dtb_name=

	# Get valid kernel-dtb file
	images_dir="${work_dir}/${dev}/${_TOT_IMAGES_DIR}"
	idx_file="${images_dir}/flash.idx"
	kernel_dtb_name="$(grep -E ":kernel-dtb,|:A_kernel-dtb," "${idx_file}" | cut -d, -f 5 | sed 's/^ //' -)"
	if [ "${kernel_dtb_name}" == "" ]; then
		echo "Failed to found valid kernel-dtb in ${idx_file}"
		return 1
	fi

	# Copy the matched kernel-dtb into /boot/dtb/ based on the board spec
	local kernel_dtb_path="${root_dir}/boot/dtb/${kernel_dtb_name}"
	cp "${images_dir}/${kernel_dtb_name}" "${kernel_dtb_path}"

	# Set FDT line with the dtb under /boot/dtb/
	echo "Update the FDT line to specify /boot/dtb/${kernel_dtb_name}"
	sed -i 's|^\([ \t]*\)FDT /boot/dtb/.*$|\1FDT /boot/dtb/'"${kernel_dtb_name}"'|' "${root_dir}/${_EXTLINUX_CONF_FILE}"
}

get_boot_partition()
{
	# Get boot partition for encrypted rootfs partition
	# Usage:
	#        get_boot_partition {rootfs_partition} {rootfs_disk} {_ret_boot_partition}
	local rootfs_partition="${1}"
	local rootfs_disk="${2}"
	local _ret_boot_partition="${3}"
	local rootfs_name=
	local boot_name=
	local boot_partition=

	# For APP_ENC or APP_ENC_b partition, the boot partition is
	# APP or APP_b respectively
	rootfs_name="$(blkid -o value -s PARTLABEL "${rootfs_partition}")"
	boot_name="${rootfs_name/_ENC/}"
	boot_partition="$(blkid | grep "${rootfs_disk}" | grep -m 1 "PARTLABEL=\"${boot_name}\"" | cut -d: -f 1)"
	eval "${_ret_boot_partition}=${boot_partition}"
}

no_layout_change_in_ota()
{
	# Return 0 if there is no layout change in OTA
	# Usage:
	#     no_layout_change_in_ota {work_dir}
	local work_dir="${1}"
	local layout_change=
	layout_change="$(cat "${work_dir}/layout_change")"
	if [ "${layout_change}" == 0 ]; then
		return 0
	else
		return 1
	fi
}

update_extlinux_conf()
{
	# Update extlinux.conf
	# For OTA with layout change, GPT is updated, the PARTTUUID for rootfs is changed,
	# use the new extlinux.conf that contains "root=" set to the updated PARTUUID.
	# For OTA without layout change, GPT is not updated, the PARTUUID for rootfs is not
	# changed, use the extlinux.conf that contains "root=" set to the original PARTUUID
	# in the base system. Also, a few updates need to be applied for some special OTA cases.
	# Usage:
	#        update_extlinux_conf {work_dir} {root_dir}
	local work_dir="${1}"
	local root_dir="${2}"

	# Use the new extlinux.conf directly if layout change exists
	if ! no_layout_change_in_ota "${work_dir}"; then
		return 0
	fi

	# Restore the original extlinux.conf to boot/extlinux/extlinux.conf
	local orig_extlinux_conf="${work_dir}/${_EXTLINUX_CONF_BACKUP}"
	local extlinux_conf="${root_dir}/${_EXTLINUX_CONF_FILE}"
	echo "restore_file ${orig_extlinux_conf} ${extlinux_conf}"
	restore_file "${orig_extlinux_conf}" "${extlinux_conf}"

	# Add "video=efifb:off" to the APPEND line if it does not exist for T194 devices.
	local board_name=
	local append_line=
	board_name="$(cat "${work_dir}/board_name")"
	if [[ ("${board_name}" == "jetson-agx-xavier-devkit" \
		|| "${board_name}" == "jetson-agx-xavier-industrial" \
		|| "${board_name}" == "jetson-xavier-nx-devkit-emmc") ]]; then
		set +e
		append_line="$(grep -m 1 "^[ \t]*APPEND " "${extlinux_conf}")"
		set -e
		if [ "${append_line}" != "" ] \
			&& [[ ! "${append_line}" =~ "video=efifb:off" ]]; then
			echo "Adding video=efifb:off to the end of the APPEND line"
			sed -i "/^[ \t]*APPEND/s|\$| video=efifb:off|" \
				"${extlinux_conf}"
		fi
	fi
}

update_fstab()
{
	# Update fstab
	# For OTA with layout change, GPT is updated, the PARTTUUID for boot partition
	# is changed, use the new fstab that contains "/boot" set to the updated PARTUUID.
	# For OTA without layout change, GPT is not updated, the PARTUUID for boot partition
	# is not changed, use the fstab that contains "/boot" set to the original PARTUUID
	# in the base system.
	# Usage:
	#        update_fstab {work_dir} {root_dir}
	local work_dir="${1}"
	local root_dir="${2}"

	# Use the new extlinux.conf directly if layout change exists
	if ! no_layout_change_in_ota "${work_dir}"; then
		return 0
	fi

	# Restore the original fstab to etc/fstab
	local orig_fstab="${work_dir}/${_FSTAB_BACKUP}"
	local fstab="${root_dir}/${_FSTAB_FILE}"
	echo "restore_file ${orig_fstab} ${fstab}"
	restore_file "${orig_fstab}" "${fstab}"
}

back_up_boot_part()
{
	# Back up files from boot partition
	# Usage:
	#        back_up_boot_part {work_dir} {boot_part_node} ${boot_mnt_tmp}
	local work_dir="${1}"
	local boot_part_node="${2}"
	local boot_mnt_tmp="${3}"

	# Mount boot partition
	if ! mount "${boot_part_node}" "${boot_mnt_tmp}"; then
		echo "Failed to mount ${boot_part_node} ${boot_mnt_tmp}"
		return 1
	fi

	# Back up the extlinux.conf
	local orig_file=
	local backup_file=
	orig_file="${boot_mnt_tmp}/${_EXTLINUX_CONF_FILE}"
	backup_file="${work_dir}/${_EXTLINUX_CONF_BACKUP}"
	echo "back_up_file ${orig_file} ${backup_file}"
	back_up_file "${orig_file}" "${backup_file}"

	# Back up the /boot/initrd
	orig_file="${boot_mnt_tmp}/${_INITRD_FILE}"
	backup_file="${work_dir}/${_INITRD_BACKUP}"
	echo "back_up_file ${orig_file}" "${backup_file}"
	back_up_file "${orig_file}" "${backup_file}"

	# Umount boot partition
	if ! umount "${boot_mnt_tmp}"; then
		echo "Failed to umount ${boot_mnt_tmp}"
		return 1
	fi
	return 0
}

write_boot_part()
{
	# Get valid boot image and write this image into boot partition
	# Usage:
	#        write_boot_part {work_dir} {dev} {boot_part_node}
	local work_dir="${1}"
	local dev="${2}"
	local boot_part_node="${3}"

	# Get valid boot image
	local boot_name=
	local boot_image=
	boot_name="$(blkid -o value -s PARTLABEL "${boot_part_node}")"
	if [ "${boot_name}" == "${_APP_B_NAME}" ]; then
		boot_image="${work_dir}/${dev}/${_TOT_IMAGES_DIR}/${_BOOT_IMAGE_B}"
	else
		boot_image="${work_dir}/${dev}/${_TOT_IMAGES_DIR}/${_BOOT_IMAGE}"
	fi

	# Write boot partition
	echo "Writing ${boot_image} into ${boot_part_node}"
	if ! dd if="${boot_image}" of="${boot_part_node}" >/dev/null 2>&1; then
		echo "Failed to write ${boot_image} into ${boot_part_node}"
		return 1
	fi
	sync
	return 0
}

restore_files_to_boot_part()
{
	# Restore files to boot partition
	# Usage:
	#        restore_files_to_boot_part {work_dir} {dev} {boot_part_node} \
	#	                            {boot_mnt_tmp}
	local work_dir="${1}"
	local dev="${2}"
	local boot_part_node="${3}"
	local boot_mnt_tmp="${4}"

	# Mount boot partition
	if ! mount "${boot_part_node}" "${boot_mnt_tmp}"; then
		echo "Failed to mount ${boot_part_node} on the ${boot_mnt_tmp}"
		return 1
	fi

	# Restore the /boot/initrd
	local restore_file=
	local target_file=
	restore_file="${work_dir}/${_INITRD_BACKUP}"
	target_file="${boot_mnt_tmp}/${_INITRD_FILE}"
	echo "restore_file ${restore_file}" "${target_file}"
	restore_file "${restore_file}" "${target_file}"

	# Save the original extlinux.conf to extlinux.conf.${base_version}
	local base_version=
	base_version="$(cat "${work_dir}/base_version")"
	restore_file="${work_dir}/${_EXTLINUX_CONF_BACKUP}"
	target_file="${boot_mnt_tmp}/${_EXTLINUX_CONF_FILE}.${base_version}"
	echo "restore_file ${restore_file} ${target_file}"
	restore_file "${restore_file}" "${target_file}"

	# Update extlinux.conf
	update_extlinux_conf "${work_dir}" "${boot_mnt_tmp}"

	# Update kernel-dtb for multiple boardspec
	if ! update_kernel_dtb "${work_dir}" "${boot_mnt_tmp}" "${dev}"; then
		echo "Failed to run \"update_kernel_dtb ${work_dir} ${boot_mnt_tmp} ${dev}\""
		return 1
	fi

	# Umount boot partition
	if ! umount "${boot_mnt_tmp}"; then
		echo "Failed to umount ${boot_mnt_tmp}"
		return 1
	fi
}

update_boot_partition()
{
	# Update boot partition
	# Steps:
	#  1. Get boot partition
	#  2. Back up files from boot partition
	#  3. Write boot image into boot partition
	#  4. Restore files into boot partition
	#  5. Update the /boot entry in /etc/fstab in rootfs
	# Usage:
	#        update_boot_partition {work_dir} {dev} {rootfs_partition} {rootfs_disk} {root_dir}
	local work_dir="${1}"
	local dev="${2}"
	local rootfs_partition="${3}"
	local rootfs_disk="${4}"
	local root_dir="${5}"

	# Get boot partition node
	local boot_part_node=
	get_boot_partition "${rootfs_partition}" "${rootfs_disk}" boot_part_node
	if [ "${boot_part_node}" == "" ]; then
		echo "Failed to get the boot partition"
		return 1
	fi

	# Back up files from boot partition
	local boot_mnt_tmp=/tmp/boot_mnt
	mkdir -p "${boot_mnt_tmp}"
	echo "back_up_boot_part ${work_dir} ${boot_part_node} ${boot_mnt_tmp}"
	if ! back_up_boot_part "${work_dir}" "${boot_part_node}" "${boot_mnt_tmp}"; then
		echo "Failed to run \"back_up_boot_part ${work_dir} ${boot_part_node} ${boot_mnt_tmp}\""
		return 1
	fi

	# Write boot partition
	echo "write_boot_part ${work_dir} ${dev} ${boot_part_node}"
	if ! write_boot_part "${work_dir}" "${dev}" "${boot_part_node}"; then
		echo "Failed to run \"write_boot_part ${work_dir} ${dev} ${boot_part_node}\""
		return 1
	fi

	# Restore files to the updated boot partition
	echo "restore_files_to_boot_part ${work_dir} ${dev} ${boot_part_node}" \
		"${boot_mnt_tmp}"
	if ! restore_files_to_boot_part "${work_dir}" "${dev}" "${boot_part_node}" \
		"${boot_mnt_tmp}"; then
		echo "Failed to run \"restore_files_to_boot_part ${work_dir} ${dev}" \
			"${boot_part_node} ${boot_mnt_tmp}\""
		return 1
	fi

	# Clean up
	rm -rf "${boot_mnt_tmp}"
	return 0
}

apply_rootfs_overlay_package()
{
	# Apply the rootfs overlay package on corresponding
	# rootfs partition if it exists. The rootfs overlay
	# package only exists when ROOTFS_AB is enabled.
	# Usage:
	#        apply_rootfs_overlay_package {work_dir} {rootfs_disk} {root_dir} {dev}
	local work_dir="${1}"
	local rootfs_disk="${2}"
	local root_dir="${3}"
	local dev="${4}"
	local app_b_name=

	# When the boot partition exists, the rootfs partition name
	# is APP_ENC/APP_ENC_b. The boot partition is named APP/APP_b.
	if [ "${_BOOT_PARTITION_EXIST}" -eq 1 ]; then
		app_b_name="${_APP_ENC_B_NAME}"
	else
		app_b_name="${_APP_B_NAME}"
	fi

	# Check whether rootfs partition is APP or APP_b
	local app_b_partition=
	local rootfs_overlay_package=
	app_b_partition="$(blkid | grep "${rootfs_disk}" | grep -m 1 "PARTLABEL=\"${app_b_name}\"" | cut -d: -f 1)"
	if [ "${rootfs_partition}" == "${app_b_partition}" ]; then
		rootfs_overlay_package="${work_dir}/${dev}/${_ROOTFS_OVERLAY_PACKAGE_B}"
	else
		rootfs_overlay_package="${work_dir}/${dev}/${_ROOTFS_OVERLAY_PACKAGE_A}"
	fi

	# Apply the rootfs overlay package into rootfs
	# partition if it exists
	if [ -f "${rootfs_overlay_package}" ]; then
		echo "Extract ${rootfs_overlay_package} into ${rootfs_partition} ..."
		echo "tar -xzpf ${rootfs_overlay_package} ${_COMMON_TAR_OPTIONS[*]} -C ${root_dir}/"
		if ! tar -xzpf "${rootfs_overlay_package}" "${_COMMON_TAR_OPTIONS[@]}" -C "${root_dir}/"; then
			echo "Failed to extract ${rootfs_overlay_package} into ${rootfs_partition}"
			return 1
		fi
	fi
	return 0
}

post_process_for_rootfs()
{
	# Post process after rootfs partition is updated
	# Steps:
	#  1. If rootfs partition is encrypted, get the encrypted
	#    rootfs partition node.
	#  2. Apply the overlay package on the rootfs if it exists
	#  3. If rootfs partition is encrypted, update boot partition
	#    Otherwise, update kernel-dtb in /boot/dtb/
	#
	local work_dir="${1}"
	local rootfs_partition="${2}"
	local root_dir="${3}"
	local rootfs_disk=
	local dev=
	local rootfs_name=

	# Convert it to the real devnode if passed-in rootfs partition
	# is a unlocked encrypted partition under /dev/mapper/.
	local enc_rfs_part=
	if [ "${_ROOTFS_ENC_ENABLED}" -eq 1 ]; then
		# Passed-in rootfs partition is an unlocked partition, e.g: /dev/mapper/dm_root
		enc_rfs_part="$(lsblk -n -s -r -p -o NAME,TYPE "${rootfs_partition}" | grep part | cut -d\  -f 1)"
		if [ "${enc_rfs_part}" == "" ]; then
			echo "Failed to get encrypted rootfs partition from ${rootfs_partition}"
			return 1
		fi
		echo "Get encrypted rootfs partition ${enc_rfs_part} from ${rootfs_partition}"
		# Obtain real devnode, e.g: /dev/mmcblk0p2
		rootfs_partition="${enc_rfs_part}"
	fi

	# Get rootfs name and the device where rootfs partition is located
	rootfs_name="$(blkid -o value -s PARTLABEL "${rootfs_partition}")"
	if [[ "${rootfs_partition}" =~ "nvme0n1" ]]; then
		rootfs_disk="${_NVME_DEVICE}"
		dev="${_EXT_DEV}"
	else
		rootfs_disk="${_SDMMC_USER_DEVICE}"
		dev="${_INT_DEV}"
	fi

	# Apply rootfs overlay package
	if ! apply_rootfs_overlay_package "${work_dir}" "${rootfs_disk}" "${root_dir}" "${dev}"; then
		echo "Failed to run \"apply_rootfs_overlay_package ${work_dir} ${rootfs_disk} ${root_dir} ${dev}\""
		return 1
	fi

	# Update boot partition if it exists
	if [ "${_BOOT_PARTITION_EXIST}" -eq 1 ]; then
		# Update boot partition
		echo "update_boot_partition ${work_dir} ${dev} ${rootfs_partition} ${rootfs_disk} ${root_dir}"
		if ! update_boot_partition "${work_dir}" "${dev}" "${rootfs_partition}" "${rootfs_disk}" "${root_dir}"; then
			echo "Failed to run\"update_boot_partition ${work_dir} ${dev} ${rootfs_partition} ${rootfs_disk} ${root_dir}\""
			return 1
		fi
	else
		# Update extlinux.conf
		update_extlinux_conf "${work_dir}" "${root_dir}"

		# Update kernel-dtb for multiple boardspec
		if ! update_kernel_dtb "${work_dir}" "${root_dir}" "${dev}"; then
			echo "Failed to run \"update_kernel_dtb ${work_dir} ${root_dir} ${dev}\""
			return 1
		fi
	fi

	# Update fstab
	update_fstab "${work_dir}" "${root_dir}"

}

update_rootfs_partition()
{
	# Update rootfs partition in recovery kernel or normal kernel
	# Usage:
	#     update_rootfs_partition {file_image} {rootfs_partition} {work_dir}
	local file_image="${1}"
	local rootfs_partition="${2}"
	local work_dir="${3}"
	local tmp_mnt=/tmp/mnt

	# Check whether rootfs partition is encrypted
	if is_rootfs_partition_encrypted "${rootfs_partition}"; then
		_ROOTFS_ENC_ENABLED=1
		_BOOT_PARTITION_EXIST=1
	elif is_rootfs_partition_app_enc "${rootfs_partition}"; then
		# Rootfs partition is APP_ENC/APP_ENC_b, but it is not encrypted
		_ROOTFS_ENC_ENABLED=0
		_BOOT_PARTITION_EXIST=1
	fi

	echo "Validate ${file_image}"
	if ! gzip -t "${file_image}" >& /dev/null; then
		echo "The rootfs image ${file_image} is invalid."
		return 1
	fi
	mkdir -p "${tmp_mnt}"

	source "${work_dir}"/nv_ota_preserve_data.sh

	# Mount rootfs partition
	echo "Mounting rootfs partition ${rootfs_partition} ..."
	if ! mount "${rootfs_partition}" "${tmp_mnt}"; then
		echo "Failed to mount rootfs partition ${rootfs_partition}"
		return 1
	fi

	# Back up files into OTA work directory
	echo "Backing up files into ${work_dir}"
	if ! back_up_specified_files "${work_dir}" "${tmp_mnt}"; then
		echo "Failed to \"back_up_specified_files ${work_dir} ${tmp_mnt}\""
		return 1
	fi

	# Delete all the dirs/files in the rootfs partition
	# except the ota package and work directory
	echo "Deleting directories/files in the ${rootfs_partition}"
	echo "Entering ${tmp_mnt}"
	pushd "${tmp_mnt}" > /dev/null 2>&1 || return 1
	echo "find \"${tmp_mnt}\" -maxdepth 1 ! -name \"ota*\" -exec rm -rf {} \;"
	if ! find . -maxdepth 1 ! -name "ota*" -exec rm -rf "{}" \;; then
		echo "Failed to delete dirs/files in the ${rootfs_partition}"
		return 1
	fi
	popd > /dev/null 2>&1  || return 1
	echo "Exiting ${tmp_mnt}"

	# Decompress rootfs image into rootfs partition
	echo "Decompress rootfs image into ${rootfs_partition} ..."
	echo "tar -xpf ${file_image} ${_COMMON_TAR_OPTIONS[*]} -C ${tmp_mnt}/"
	if ! tar -xpf "${file_image}" "${_COMMON_TAR_OPTIONS[@]}" -C "${tmp_mnt}/"; then
		echo "Failed to decompress rootfs image into ${rootfs_partition}"
		return 1
	fi

	# Post process for rootfs partition
	if ! post_process_for_rootfs "${work_dir}" "${rootfs_partition}" "${tmp_mnt}"; then
		echo "Failed to \"post_process_for_rootfs ${work_dir} ${rootfs_partition} ${tmp_mnt}\""
		return 1
	fi

	# Restore files from OTA work directory
	echo "Restoring files from ${work_dir}"
	if ! restore_specified_files "${work_dir}" "${tmp_mnt}"; then
		echo "Failed to \"restore_specified_files ${work_dir} ${tmp_mnt}\""
		return 1
	fi

	# Umount rootfs parition
	umount "${tmp_mnt}"
	return 0
}

if [ $# -lt 1 ];then
	usage
fi

nargs=$#
_ROOTFS_IMAGE="${!nargs}"

opstr+="fd:p:"
while getopts "${opstr}" OPTION; do
	case $OPTION in
	d) _OTA_WORK_DIR=${OPTARG}; ;;
	p) _ROOTFS_PART_DEV=${OPTARG}; ;;
	*)
	   usage
	   ;;
	esac;
done

# Check passed-in arguments
if [ ! -f "${_ROOTFS_IMAGE}" ]; then
	echo "Specified rootfs image ${_ROOTFS_IMAGE} is not found"
	exit 1
fi
if [ -n "${_OTA_WORK_DIR}" ] && [ ! -d "${_OTA_WORK_DIR}" ]; then
	echo "Specified OTA work directory ${_OTA_WORK_DIR} is not found"
	exit 1
fi

# Check whether the specified rootfs partition node exist
if [ ! -e "${_ROOTFS_PART_DEV}" ]; then
	echo "Specified rootfs partition ${_ROOTFS_PART_DEV} is not found"
	exit 1
fi

# Update rootfs partition
if [ -f "${_ROOTFS_UPDATE_SUCCESS_FLAG}" ]; then
	rm -f "${_ROOTFS_UPDATE_SUCCESS_FLAG}"
fi
echo "update_rootfs_partition ${_ROOTFS_IMAGE} ${_ROOTFS_PART_DEV} ${_OTA_WORK_DIR}"
if ! update_rootfs_partition "${_ROOTFS_IMAGE}" "${_ROOTFS_PART_DEV}" "${_OTA_WORK_DIR}"; then
	echo "Failed to run \"update_rootfs_partition ${_ROOTFS_IMAGE} ${_ROOTFS_PART_DEV} ${_OTA_WORK_DIR}\""
	exit 1
fi
echo -n "1" >"${_ROOTFS_UPDATE_SUCCESS_FLAG}"
