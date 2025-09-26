#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2019-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

# This is a script to do preparation for OTA process

# 1. Validate base version and target board
# 2. Check key partitions (BCT/MB1/MB1_BCT) and avoid version rollback
# 3. Check whehter there is enough space for storing partitions of backup
#    boot path on the disk
# 4. Check partitons layout to avoid any partition in current system is
#    missing
# 5. Enable A/B redundancy if not enabled and check whether both slots are valid
# 6. Write base recovery image/dtb into "kernel"/"kernel-dtb" partitions
# 7. Set active to A
set -e

BASE_VERSION=
MSI_EMMC_MIN_SIZE=
MSI_EMMC_MIN_SIZE_TABLE=(
	# board:msi_emmc_min_size
	'jetson-agx-xavier-devkit:335544320' # 320MB
	'jetson-agx-xavier-industrial:241172480' # 230MB
	'jetson-xavier-nx-devkit-emmc:241172480' # 230MB
)
NVME_USER_DEVICE=/dev/nvme0n1
SDMMC_USER_DEVICE=/dev/mmcblk0
TARGET_BOARD=

usage()
{
	echo -ne "Usage: sudo $0 <disk for storing backup boot path>\n"
	echo -ne "\t<disk for storing backup boot path>:\n"
	echo -ne "\t    the device for storing the partitions as backup boot path.\n"
	echo -ne "Example:\n"
	echo -ne "\tsudo $0 /dev/mmcblk0\n"
	exit 1
}

both_slots_valid()
{
	# Check whether both slots are bootable
	# Usage:
	#        both_slots_valid
	local _nvbootctrl=
	_nvbootctrl="$(which nvbootctrl)"

	if [ "${_nvbootctrl}" = "" ]; then
		ota_log "nvbootctrl is not found"
		return 1
	fi

	local i=0
	while [ "$i" -lt 2 ]
	do
		if ! nvbootctrl is-slot-bootable $i; then
			ota_log "Slot $i is not bootable, device is not ready for OTA. Please do RCM flash to recover it"
			return 1
		fi
		if ! nvbootctrl is-slot-marked-successful $i; then
			ota_log "Slot $i is not marked successful, device is not ready for OTA. Please do RCM flash to recover it"
			return 1
		fi
		i=$((i + 1))
	done
	return 0
}

ota_check_free_space_on_emmc()
{
	# Get free space size on emmc
	# it's assumed that the free space can include 3 parts:
	# the first part is the real free space that is not allocated for any partition
	# the second part is the UDA partition that is not used in default case
	# the third part is the RECROOTFS partition that is not used in default case
	local Free_size=
	Free_size=$(parted "${SDMMC_USER_DEVICE}" unit "B" print free | grep "Free Space" | tail -1 | sed 's/^[ \t]*//g' | sed 's/[ ][ ]*/ /g' | sed 's/B / /g' - | cut -d\  -f 3)
	if [ "${Free_size}" = "" ];then
		ota_log "Failed to get current free space on eMMC storage device"
		return 1
	fi
	local UDA_size=
	UDA_size=$(parted "${SDMMC_USER_DEVICE}" unit "B" print | grep -m 1 "UDA" | sed 's/^[ ]*//g' | sed 's/[ ][ ]*/ /g' | cut -d\  -f 4 | sed 's/B$//g')
	if [ "${UDA_size}" = "" ];then
		ota_log "Failed to get size of UDA partition on eMMC storage device"
		return 1
	fi
	local RECROOTFS_size=
	RECROOTFS_size=$(parted "${SDMMC_USER_DEVICE}" unit "B" print | grep -m 1 "RECROOTFS" | sed 's/^[ ]*//g' | sed 's/[ ][ ]*/ /g' | cut -d\  -f 4 | sed 's/B$//g')
	if [ "${RECROOTFS_size}" = "" ];then
		ota_log "Failed to get size of RECROOTFS partition on eMMC storage device"
		return 1
	fi

	local total_free_size=$((Free_size + UDA_size + RECROOTFS_size))
	if [ "${total_free_size}" -ge "${MSI_EMMC_MIN_SIZE}" ]; then
		ota_log "There is enough free space(${total_free_size} bytes > ${MSI_EMMC_MIN_SIZE} bytes) on eMMC"
		return 0
	else
		ota_log "Warning: no enough free space (${total_free_size} bytes < ${MSI_EMMC_MIN_SIZE} bytes) on eMMC"
		return 1
	fi
}

set_msi_emmc_min_size()
{
	# Set the MSI_EMMC_MIN_SIZE
	# Usage:
	#    set_msi_emmc_min_size {target_board} {_ret_msi_min_size}
	local target_board="${1}"
	local _ret_msi_min_size="${2}"
	local msi_min_size=

	local item=
	local board=
	local msi_min_size=
	for item in "${MSI_EMMC_MIN_SIZE_TABLE[@]}"
	do
		board="$(echo "${item}" | cut -d: -f 1)"
		if [ "${board}" == "${target_board}" ]; then
			msi_min_size="$(echo "${item}" | cut -d: -f 2)"
			eval "${_ret_msi_min_size}=${msi_min_size}"
			return 0
		fi
	done
	ota_log "Error: invalid target board(${target_board})"
	return 1
}

check_app()
{
	# Check whether the locations and sizes of APP in the base system
	# and OTA system are the same. OTA can be only applied when the
	# location and size of APP is not changed
	# Usage:
	#    check_app {work_dir}
	local work_dir="${1}"

	# Get the location of the APP in base system
	local base_app_offset=
	local app_partition="/dev/disk/by-partlabel/APP"
	local device=
	local hw_sector=
	device=${OTA_DEVICE##*/}
	hw_sector="$(cat /sys/block/"${device}"/queue/hw_sector_size)"
	base_app_offset="$(partx -g -o START "${app_partition}")"
	base_app_offset="$((base_app_offset * hw_sector))"

	# Get the location of the APP in OTAed system
	local idx_file="${work_dir}/external_device/images-R35-ToT/flash.idx"
	local ota_app_offset=
	ota_app_offset="$(grep ":APP," "${idx_file}" | cut -d, -f 3 | sed 's/^ //' -)"

	# Compare APP's location
	if [ "${base_app_offset}" != "${ota_app_offset}" ]; then
		ota_log "APP location is changed from ${base_app_offset} to ${ota_app_offset}"
		return 1
	fi

	# Get size of the APP in base system
	local app_devnode=
	local base_app_size=
	app_devnode="$(readlink -f "${app_partition}")"
	app_devnode="${app_devnode##*/}"
	base_app_size="$(cat /sys/block/"${device}"/"${app_devnode}"/size)"
	base_app_size="$((base_app_size * hw_sector))"

	# Get size of the APP in OTAed system
	local ota_app_size
	ota_app_size="$(grep ":APP," "${idx_file}" | cut -d, -f 4 | sed 's/^ //' -)"

	# Compare APP's size
	if [ "${base_app_size}" == "${ota_app_size}" ]; then
		return 0
	else
		ota_log "APP size is changed from ${base_app_size} to ${ota_app_size}"
		return 1
	fi
}

# Make sure that this script is running in root privilege
USERID=$(id -u)
if [ "${USERID}" -ne 0 ]; then
       echo "Please run this program as root."
       exit 0
fi

if [ $# -ne 1 ]; then
	usage
fi

nargs=$#;
OTA_DEVICE=${!nargs};

ota_log "Command: ${BASH_SOURCE[0]} $*"

if [ ! -e "${OTA_DEVICE}" ]; then
	echo "Invalid disk path ${OTA_DEVICE}"
	usage
fi

_parted="$(which parted)"
if [ "${_parted}" = "" ]; then
	echo "Please install the tool parted before running this script"
	exit 1
fi

_strings="$(which strings)"
if [ "${_strings}" = "" ]; then
	echo "Please install the tool strings (sudo apt-get install binutils) before running this script"
	exit
fi

_part_table="$(parted ${OTA_DEVICE} print | grep "Partition Table" | cut -d: -f 2 | sed 's/^[ ]\+//')"
if [ "${_part_table}" != "gpt" ]; then
	echo "Error: only gpt partition table is supported," \
		"please re-format disk with gpt partition table"
	exit 1
fi

source ./nv_ota_common_utils.func
source ./nv_ota_check_version.sh
source ./ota_multi_board_specs.sh
source ./ota_check_partitions.sh
source ./nv_ota_common.func

if [ "${OTA_DEVICE}" != "${SDMMC_USER_DEVICE}" ]; then
	# Check APP's location and size
	# OTA can not be applied if APP's location or size is changed after OTA.
	ota_log "check_app ${OTA_WORK_DIR}"
	if ! check_app "${OTA_WORK_DIR}"; then
		ota_log "The location or size of APP partition is changed, so can not apply OTA"
		exit 1
	fi

fi

# Check whether bsp version matches with OTA package version
ota_log "check_bsp_version ${OTA_WORK_DIR} BASE_VERSION"
if ! check_bsp_version "${OTA_WORK_DIR}" "BASE_VERSION"; then
	ota_log "Failed to run \"check_bsp_version ${OTA_WORK_DIR} BASE_VERSION\""
	exit 1
fi

# Check whether current board matches with OTA package target board
ota_log "check_target_board ${OTA_WORK_DIR} TARGET_BOARD"
if ! check_target_board "${OTA_WORK_DIR}" "TARGET_BOARD"; then
	ota_log "Failed to run \"check_target_board ${OTA_WORK_DIR} TARGET_BOARD\""
	exit 1
fi

# Create the redundant copy of BCT in case it is not in place
if [ "${OTA_DEVICE}" != "${SDMMC_USER_DEVICE}" ]; then
	ota_log "duplicate_bct_copy ${TARGET_BOARD} ${BASE_VERSION}"
	if ! duplicate_bct_copy "${TARGET_BOARD}" "${BASE_VERSION}"; then
		ota_log "Failed to run \"dupclicate_bct_copy ${TARGET_BOARD} ${BASE_VERSION}\""
		exit 1
	fi
fi

# Set MSI_EMMC_MIN_SIZE for corresponding board
ota_log "set_msi_emmc_min_size ${TARGET_BOARD} MSI_EMMC_MIN_SIZE"
if ! set_msi_emmc_min_size "${TARGET_BOARD}" "MSI_EMMC_MIN_SIZE"; then
	ota_log "Failed to run \"set_msi_emmc_min_size ${TARGET_BOARD} MSI_EMMC_MIN_SIZE\""
	exit 1
fi

# Check VER/VER_b partitions to avoid version rollback
ota_log "ota_check_rollback ${OTA_WORK_DIR} ${TARGET_BOARD} ${BASE_VERSION} ${OTA_DEVICE}"
if ! ota_check_rollback "${OTA_WORK_DIR}" "${TARGET_BOARD}" "${BASE_VERSION}" "${OTA_DEVICE}"; then
	ota_log "Failed to run \"ota_check_rollback ${OTA_WORK_DIR} ${TARGET_BOARD} ${BASE_VERSION} ${OTA_DEVICE}\""
	exit 1
fi

# The board specific images are stored under images-XXX-XXX/<board_spec>/ directory.
# Need to parse current target board spec and then move the images from
# corresponding board_spec directory to image-XXX-XXX.
ota_log "ota_choose_images ${OTA_WORK_DIR}"
if ! ota_choose_images "${OTA_WORK_DIR}"; then
	ota_log "Failed to run \"ota_choose_images ${OTA_WORK_DIR}\""
	exit 1
fi

# Check whether there is enough free space at the end of internal eMMC
# to store the MSI
ota_log "ota_check_free_space_on_emmc"
if ! ota_check_free_space_on_emmc; then
	ota_log "Failed to run \"ota_check_free_space_on_emmc\""
	exit 1
fi

# Check whether the offset of each partition in the "flash.idx" in the
# OTA payload package "ota_pakcage.tar.gz" matches the offset of each
# partition in current GPTs. For boot device, secondary GPT is used and
# for user device, primary GPT is used.
ota_log "ota_check_partitions ${OTA_WORK_DIR}"
if ! ota_check_partitions "${OTA_WORK_DIR}"; then
	ota_log "Failed to run \"ota_check_partitions ${OTA_WORK_DIR}\""
	exit 1
fi

# Enable A/B redundancy
ota_log "enable_a_b_redundancy"
if ! enable_a_b_redundancy; then
	ota_log "Failed to run \"enable_a_b_redundancy\""
	exit 1
fi

# Make sure both slots are bootable
ota_log "both_slots_valid"
if ! both_slots_valid; then
	ota_log "Failed to run \"both_slots_valid\""
	exit 1
fi

# Write recovery and recovery-dtb partition
ota_log "write_base_recovery ${OTA_WORK_DIR} ${SDMMC_USER_DEVICE}"
if ! write_base_recovery "${OTA_WORK_DIR}" "${SDMMC_USER_DEVICE}"; then
	ota_log "Failed to run \"write_base_recovery ${OTA_WORK_DIR} ${SDMMC_USER_DEVICE}\""
	exit 1
fi

ota_log "write_kernel_bootctrl ${OTA_WORK_DIR}"
if ! write_kernel_bootctrl "${OTA_WORK_DIR}"; then
	ota_log "Failed to run \"write_kernel_bootctrl ${OTA_WORK_DIR}\""
	exit 1
fi

# Support NVMe device as external device
if [ "${OTA_DEVICE}" == "${NVME_USER_DEVICE}" ]; then
	# Force to boot from eMMC because the intermediate partition layout is on the eMMC device
	ota_log "force_booting_from_emmc ${OTA_WORK_DIR} ${TARGET_BOARD}"
	if ! force_booting_from_emmc "${OTA_WORK_DIR}" "${TARGET_BOARD}"; then
		ota_log "Failed to run \"force_booting_from_emmc ${OTA_WORK_DIR} ${TARGET_BOARD}\""
		exit 1
	fi
fi

ota_log "OTA preprocess has been completed. OTA will be automatically started once device is rebooted"
