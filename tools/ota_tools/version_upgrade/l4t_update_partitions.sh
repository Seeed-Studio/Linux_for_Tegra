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

# This is a script to parse control file, such as "upgradetasklist.txt" to
# update partitions in partition-based OTA

# Each line in control file includes 5 elements which are:
# <task_stage>:<action>:<device>:<partition>:<index file>

# Where
#
#  <task_stage>: Update_A, Update_B, Update_B1, Update_B2, ...
#
#  <action>: Write, Write_BCT_A_1, Write_BCT_B_1, Write_BCT_A_2, Write_BCT_B_2, Fill_BCT_A, Write_APP, Write_APP_b, Clean, Clean_BCT1, Read, Set_Boot_Normal_Mode, Set_Boot_Recovery_Mode, Set_Boot_Slot_A, Set_Boot_Slot_B, Move_QSPI
#
#  <device>: sdmmc_boot, sdmmc_user, qspi, sdcard
#
#  <partition>: BCT, mb1, mb1_b, MB1_BCT, MB1_BCT_b, dram-ecc-fw, badpage-fw, badpage-fw_b, spe-fw, spe-fw_b, mb2, mb2_b, mts-preboot, mts-preboot_b, SMD, SMD_b, secondary_gpt, master_boot_record, primary_gpt, APP, mts-bootpack, mts-bootpack_b, cpu-bootloader, cpu-bootloader_b, bootloader-dtb, bootloader-dtb_b, secure-os, secure-os_b, eks, adsp-fw, adsp-fw_b, bpmp-fw, bpmp-fw_b, bpmp-fw-dtb, bpmp-fw-dtb_b, sce-fw, sce-fw_b, sc7, sc7_b, FBNAME, BMP, BMP_b, recovery, recovery-dtb, kernel-bootctrl, kernel-bootctrl_b, kernel, kernel_b, kernel-dtb, kernel-dtb_b, secondary_gpt
#
#  <index file>: <directory of images>/<index file name>, such as R32-ToT/flash.idx, R32_1_2-R32i/flash.idx

ACTIVE_INDEX_FILE=""
ACTIVE_INDEX_ARRAY=
LAST_ACTION_INDEX_FILE="last_action_index"
# For T194 devices, the default block size is set in BCT
BCT_BLOCK_SIZE_QSPI=32768
BCT_BLOCK_SIZE_SDMMC=16384
BCT_ERASABLE_BLOCK_SIZE=
BOOT_DEV_SECTOR_SIZE=
COMMON_IMAGES_DIR=
CURRENT_STAGE=""
K_BYTES=1024
SIZE_8_MiB=8388608
ROOTFS_UPDATE_SUCCESS_FLAG="/tmp/rootfs_update_success"
ROOTFS_PARTITION=
SDMMC_BOOT0_SIZE=4194304
SDMMC_BOOT_DEVICE_0="/dev/mmcblk0boot0"
SDMMC_BOOT_DEVICE_1="/dev/mmcblk0boot1"
SDMMC_USER_DEVICE="/dev/mmcblk0"
QSPI_BOOT_DEVICE="/dev/mtd0"
NVME_USER_DEVICE="/dev/nvme0n1"
OTA_WORK_DIR=
VERIFY_WRITE=1
NEED_DUPLICATE_GPT=0
DEVICE_TYPE_QSPI=3
DEVICE_TYPE_SDCARD=6
DEVICE_TYPE_SDMMC_BOOT=0
DEVICE_TYPE_SDMMC_USER=1
DEVICE_TYPE_NVME_USER=9
# Partitions which are not aligned to BOOT_DEV_SECTOR_SIZE boundary
NON_ALIGNED_PARTITONS=("secondary_gpt")
# Support external device
DEVICE_NVME="nvme"
INT_DEV="internal_device"
EXT_DEV="external_device"

usage()
{
	echo "Usage: $0 [options] <control file> "
	cat << EOF
    options:
	-s ------------------------- skip verifying after writing data. Default: disabled
	-h ------------------------- print this message
EOF
	exit 1
}

# Get the rootfs partition to be updated
get_rootfs_partition()
{
	local name="${1}"
	local device_type="${2}"
	local app_partition="${3}"
	local rootfs_disk=
	local rootfs_part=

	if [ "${device_type}" == "${DEVICE_TYPE_NVME_USER}" ]; then
		rootfs_disk="${NVME_USER_DEVICE}"
	else
		rootfs_disk="${SDMMC_USER_DEVICE}"
	fi
	rootfs_part="$(blkid | grep "${rootfs_disk}" | grep "PARTLABEL=\"${name}\"" | cut -d: -f 1)"
	echo "eval ${app_partition}=${rootfs_part}"
	eval "${app_partition}=${rootfs_part}"
	echo "rootfs_disk=${rootfs_disk} rootfs_part=${rootfs_part}"
}

# Set erasable block size of BCT partition
# @hw_erasable_size: erasable block size from hw
# @sw_erasable_size: erasable block size from sw
set_bct_erasable_block_size()
{
	local hw_erasable_size="${1}"
	local sw_erasable_size="${2}"

	if [ "${hw_erasable_size}" -gt "${sw_erasable_size}" ]; then
		BCT_ERASABLE_BLOCK_SIZE="${hw_erasable_size}"
	else
		BCT_ERASABLE_BLOCK_SIZE="${sw_erasable_size}"
	fi
}

# Verify sha1 checksum for image
# @file_image: file for caculating check sum
# @sha1chksum: sha1 check sum
sha1_verify ()
{
	local file_image=$1
	local sha1_chksum=$2

	if [ -z "${sha1_chksum}" ]; then
		echo "Passed-in sha1 checksum is NULL"
		return 1
	fi

	if [ ! -f "${file_image}" ]; then
		echo "$file_image is not found !!!"
		return 1
	fi

	local sha1_chksum_gen=
	sha1_chksum_gen="$(sha1sum "${file_image}" | cut -d\  -f 1)"
	if [ "${sha1_chksum_gen}" = "${sha1_chksum}" ]; then
		echo "Sha1 checksum matched for ${file_image}"
		return 0
	else
		echo "Sha1 checksum does not match (${sha1_chksum_gen} != ${sha1_chksum}) for ${file_image}"
		return 1
	fi
}

rw_part_opt_qspi()
{
	local infile=$1
	local outfile=$2
	local in_offset=$3
	local out_offset=$4
	local size=$5
	local aligned=$6
	local erase_size=
	local erase_offset=
	local leftover=

	if [ "${infile}" == "${QSPI_BOOT_DEVICE}" ]; then
		# Read from QSPI device
		echo "mtd_debug read ${QSPI_BOOT_DEVICE} ${in_offset} ${size} ${outfile}"
		mtd_debug read "${QSPI_BOOT_DEVICE}" "${in_offset}" "${size}" "${outfile}"
	elif [ "${outfile}" == "${QSPI_BOOT_DEVICE}" ]; then
		# Write to QSPI device
		if [ "${aligned}" -eq 1 ]; then
			erase_offset="${out_offset}"
			leftover=$((out_offset % BOOT_DEV_SECTOR_SIZE))
			if [ "${leftover}" -ne 0 ]; then
				echo "The writing offset is not aligned to ${BOOT_DEV_SECTOR_SIZE}"
				return 1
			fi
		else
			# Ensure the erase offset alignment for the partition which is not aligned to
			# BOOT_DEV_SECTOR_SIZE boundary
			erase_offset=$((out_offset / BOOT_DEV_SECTOR_SIZE * BOOT_DEV_SECTOR_SIZE))
		fi
		erase_size=$(((size + BOOT_DEV_SECTOR_SIZE - 1) / BOOT_DEV_SECTOR_SIZE * BOOT_DEV_SECTOR_SIZE))
		echo "mtd_debug erase ${QSPI_BOOT_DEVICE} ${erase_offset} ${erase_size}"
		mtd_debug erase "${QSPI_BOOT_DEVICE}" "${erase_offset}" "${erase_size}"
		echo "mtd_debug write ${QSPI_BOOT_DEVICE} ${out_offset} ${size} ${infile}"
		mtd_debug write "${QSPI_BOOT_DEVICE}" "${out_offset}" "${size}" "${infile}"
	else
		echo "Warning: neither infile nor outfile is a QSPI device"
	fi
	return 0
}


rw_part_opt()
{
	local infile=$1
	local outfile=$2
	local inoffset=$3
	local outoffset=$4
	local size=$5

	if [ ! -e "${infile}" ]; then
		echo "Input file ${infile} is not found"
		return 1
	fi

	if [ "${size}" == "0" ]; then
		echo "The size of bytes to be read is ${size}"
		return 1
	fi

	# Read/write QSPI device
	if [ "${infile}" == "${QSPI_BOOT_DEVICE}" ] \
		|| [ "${outfile}" == "${QSPI_BOOT_DEVICE}" ]; then
		if ! rw_part_opt_qspi "$@"; then
			echo "Failed to read / write QSPI device"
			return 1
		fi
		return 0
	fi

	# Read/Write eMMC/SD device
	local inoffset_align_K=$((inoffset % K_BYTES))
	local outoffset_align_K=$((outoffset % K_BYTES))
	if [ ${inoffset_align_K} -ne 0 ] || [ ${outoffset_align_K} -ne 0 ]; then
		echo "Offset is not aligned to K Bytes, no optimization is applied"
		echo "dd if=${infile} of=${outfile} bs=1 skip=${inoffset} seek=${outoffset} count=${size}"
		dd if="${infile}" of="${outfile}" bs=1 skip="${inoffset}" seek="${outoffset}" count="${size}"
		return 0
	fi

	local block=$((size / K_BYTES))
	local remainder=$((size % K_BYTES))
	local inoffset_blk=$((inoffset / K_BYTES))
	local outoffset_blk=$((outoffset / K_BYTES))

	echo "${size} bytes from ${infile} to ${outfile}: 1KB block=${block} remainder=${remainder}"

	if [ ${block} -gt 0 ]; then
		echo "dd if=${infile} of=${outfile} bs=1K skip=${inoffset_blk} seek=${outoffset_blk} count=${block}"
		dd if="${infile}" of="${outfile}" bs=1K skip="${inoffset_blk}" seek="${outoffset_blk}" count="${block}" conv=notrunc
		sync
	fi
	if [ ${remainder} -gt 0 ]; then
		local block_size=$((block * K_BYTES))
		local outoffset_rem=$((outoffset + block_size))
		local inoffset_rem=$((inoffset + block_size))
		echo "dd if=${infile} of=${outfile} bs=1 skip=${inoffset_rem} seek=${outoffset_rem} count=${remainder}"
		dd if="${infile}" of="${outfile}" bs=1 skip="${inoffset_rem}" seek="${outoffset_rem}" count="${remainder}" conv=notrunc
		sync
	fi
	return 0
}

is_non_aligned_partition()
{
	local part_name="$1"

	if [[ " ${NON_ALIGNED_PARTITONS[*]} " =~ " ${part_name} " ]]; then
		return 0
	fi
	return 1
}

flash_partition()
{
	local file_name=$1
	local part_name=$2
	local start_offset=$3
	local file_size=$4
	local attributes=$5
	local sha1_chksum=$6
	local dst_device=$7
	local tmp_file=/tmp/tmp.img
	local active_dir=
	local file_image=
	local sha1_chksum_gen=
	local res=0
	local tmp_size=0

	active_dir="$(dirname "${ACTIVE_INDEX_FILE}")"
	file_image="${active_dir}/${file_name}"

	if [ ! -f "${file_image}" ]; then
		file_image=${COMMON_IMAGES_DIR}/${file_name}
	fi

	local aligned=1
	if is_non_aligned_partition "${part_name}"; then
		aligned=0
	fi

	# For BCT, re-generate sha1 checksum as different BCT image will be used
	# for BCT1/BCT2.
	if [ "${part_name}" = "BCT" ]; then
		sha1_chksum="$(sha1sum "${file_image}" | cut -d\  -f 1)"
		if [ "${sha1_chksum}" = "" ]; then
			echo "Failed to generate sha1sum for file ${file_image}"
			return 1
		fi
	fi

	if ! sha1_verify "${file_image}" "${sha1_chksum}"; then
		return 1
	fi

	# verify whether this partition has been writen
	rm "${tmp_file}"
	echo "read-back partition ${part_name} to check whether to update it"
	if [ "${dst_device}" != "" ]; then
		if ! rw_part_opt "${dst_device}" "${tmp_file}" "${start_offset}" 0 "${file_size}" "${aligned}"; then
			echo "Failed to read ${file_size} bytes from ${dst_device}:${start_offset} to ${tmp_file}"
			return 1
		fi
	else
		tmp_size=$((SDMMC_BOOT0_SIZE - start_offset))
		dd if=/dev/mmcblk0boot0 of="${tmp_file}" skip="${start_offset}" bs=1 count="${tmp_size}"
		tmp_size=$((file_size - tmp_size))
		dd if=/dev/mmcblk0boot1 bs=1 count="${tmp_size}" >>"${tmp_file}"
	fi
	sync
	sha1_chksum_gen="$(sha1sum "${tmp_file}" | cut -d\  -f 1)"
	if [ "${sha1_chksum_gen}" = "${sha1_chksum}" ]; then
		echo "Partition ${part_name} has been updated, skip writing"
		return 0
	fi

	# write image
	echo "write ${file_image} into partition ${part_name}"
	if [ "${dst_device}" != "" ]; then
		if ! rw_part_opt "${file_image}" "${dst_device}" 0 "${start_offset}" "${file_size}" "${aligned}"; then
			echo "Failed to write ${file_size} bytes from ${file_image} to ${dst_device}:${start_offset}"
			return 1
		fi
	else
		tmp_size=$((SDMMC_BOOT0_SIZE - start_offset))
		echo "dd if=${file_image} of=${dst_device} seek=${start_offset} bs=1 count=${tmp_size} conv=notrunc"
		dd if="${file_image}" of=/dev/mmcblk0boot0 seek="${start_offset}" bs=1 count="${tmp_size}" conv=notrunc
		dd if="${file_image}" of=/dev/mmcblk0boot1 bs=1 skip="${tmp_size}" conv=notrunc
	fi
	sync

	if [ ${VERIFY_WRITE} -eq 1 ]; then
		rm "${tmp_file}"
		# verify writing
		if [ "${dst_device}" != "" ]; then
			if ! rw_part_opt "${dst_device}" "${tmp_file}" "${start_offset}" 0 "${file_size}" "${aligned}"; then
				echo "Failed to read ${file_size} bytes from ${dst_device}:${start_offset} to ${tmp_file}"
				return 1
			fi
		else
			tmp_size=$((SDMMC_BOOT0_SIZE - start_offset))
			echo "dd if=/dev/mmcblk0boot0 of=${tmp_file} skip=${start_offset} bs=1 count=${tmp_size}"
			dd if=/dev/mmcblk0boot0 of="${tmp_file}" skip="${start_offset}" bs=1 count="${tmp_size}"
			tmp_size=$((file_size - tmp_size))
			echo "dd if=/dev/mmcblk0boot1 bs=1 count=${tmp_size} >>${tmp_file}"
			dd if=/dev/mmcblk0boot1 bs=1 count="${tmp_size}" >>"${tmp_file}"
		fi

		sha1_verify "${tmp_file}" "${sha1_chksum}"
	fi

	return $?
}

flash_sdmmc_boot_partition()
{
	local start_offset=$3
	local file_size=$4
	local sdmmc_device=
	local args=("$@")
	local end_offset=

	start_offset=$((start_offset))
	file_size=$((file_size))
	end_offset=$((start_offset + file_size))
	if [ ${start_offset} -ge ${SDMMC_BOOT0_SIZE} ]; then
		sdmmc_device="${SDMMC_BOOT_DEVICE_1}"
		start_offset=$((start_offset - SDMMC_BOOT0_SIZE))
		args[2]=${start_offset}
	elif [ ${end_offset} -le ${SDMMC_BOOT0_SIZE} ]; then
		sdmmc_device="${SDMMC_BOOT_DEVICE_0}"
	else
		# partition cross over mmcblk0boot0 and mmcblk0boot1 and
		# it should be handled in special way
		sdmmc_device=""
	fi

	flash_partition ${args[@]} "${sdmmc_device}"
	return $?
}

flash_sdmmc_user_partition()
{
	local sdmmc_device="${SDMMC_USER_DEVICE}"

	flash_partition $@ "${sdmmc_device}"
	return $?
}

flash_qspi_partition()
{
	local qspi_device="${QSPI_BOOT_DEVICE}"

	flash_partition $@ "${qspi_device}"
	return $?
}

flash_nvme_user_partition()
{
	local nvme_device="${NVME_USER_DEVICE}"

	flash_partition $@ "${nvme_device}"
	return $?
}

read_partition ()
{
	local file_name=$1
	local part_name=$2
	local start_offset=$3
	local file_size=$4
	local attributes=$5
	local sha1_chksum=$6
	local sdmmc_device=$7
	local active_dir=
	local file_image=
	local tmp_size=0

	active_dir="$(dirname "${ACTIVE_INDEX_FILE}")"
	file_image="${active_dir}/${file_name}"

	local aligned=1
	if is_non_aligned_partition "${part_name}"; then
		aligned=0
	fi

	# read partition
	if [ "${sdmmc_device}" != "" ]; then
		echo "dd if=${sdmmc_device}} of=${file_image}_read skip=${start_offset} \
			bs=1 count=${file_size} conv=notrunc"
		if ! rw_part_opt "${sdmmc_device}" "${file_image}_read" "${start_offset}" 0 "${file_size}" "${aligned}"; then
			echo "Failed to read ${file_size} bytes from ${sdmmc_device}:${start_offset} to ${file_image}_read"
			return 1
		fi
	else
		tmp_size=$((SDMMC_BOOT0_SIZE - start_offset))
		echo "dd if=/dev/mmcblk0boot0 of=${file_image} skip=${start_offset} bs=1 count=${tmp_size}"
		dd if=/dev/mmcblk0boot0 of="${file_image}" skip="${start_offset}" bs=1 count="${tmp_size}"
		tmp_size=$((file_size - tmp_size))
		echo "dd if=/dev/mmcblk0boot1 bs=1 count=${tmp_size} >>${file_image}"
		dd if=/dev/mmcblk0boot1 bs=1 count="${tmp_size}" >>"${file_image}"
	fi
	sync
	return 0
}

read_sdmmc_boot_partition()
{
	local start_offset=$3
	local file_size=$4
	local sdmmc_device=
	local args=("$@")
	local end_offset=

	start_offset=$((start_offset))
	file_size=$((file_size))
	end_offset=$((start_offset + file_size))
	if [ ${start_offset} -ge ${SDMMC_BOOT0_SIZE} ]; then
		sdmmc_device=/dev/mmcblk0boot1
		start_offset=$((start_offset - SDMMC_BOOT0_SIZE))
		args[2]=${start_offset}
	elif [ ${end_offset} -lt ${SDMMC_BOOT0_SIZE} ]; then
		sdmmc_device=/dev/mmcblk0boot0
	else
		# partition cross over mmcblk0boot0 and mmcblk0boot1 and
		# it should be handled in special way
		sdmmc_device=""
	fi

	read_partition ${args[@]}  "${sdmmc_device}"
	return $?
}

read_sdmmc_user_partition()
{
	local sdmmc_device=/dev/mmcblk0

	read_partition $@ "${sdmmc_device}"
	return $?
}

do_read()
{
	local item=$1
	local device_type=
	local part_name=
	local file_name=
	local start_offset=
	local file_size=
	local attributes=
	local sha1_chksum=
	local res=0

	device_type="$(echo "$item" | cut -d, -f 2 | sed 's/^ //g' - | cut -d: -f 1)"
	part_name="$(echo "$item" | cut -d, -f 2 | sed 's/^ //g' - | cut -d: -f 3)"
	file_name="$(echo "$item" | cut -d, -f 5 | sed 's/^ //g' -)"
	start_offset="$(echo "$item" | cut -d, -f 3 | sed 's/^ //g' -)"
	file_size="$(echo "$item" | cut -d, -f 6 | sed 's/^ //g' -)"
	attributes="$(echo "$item" | cut -d, -f 7 | sed 's/^ //g' -)"
	sha1_chksum="$(echo "$item" | cut -d, -f 8 | sed 's/^ //g' -)"

	if [ "${file_name}" = "" ]; then
		echo "Error: file name is not specified for reading ${part_name} partition"
		return 1
	fi

	echo "Reading ${part_name} partition into ${file_name}"
	# device_type 0 means "sdmmc_boot", 1 means "sdmmc_user". Other values are invalid.
	if [ "${device_type}" = "0" ]; then
		res=$(read_sdmmc_boot_partition "${file_name}" "${part_name}" \
			"${start_offset}" "${file_size}" "${attributes}" "${sha1_chksum}")
	elif [ "${device_type}" = "1" ]; then
		res=$(read_sdmmc_user_partition "${file_name}" "${part_name}" \
			"${start_offset}" "${file_size}" "${attributes}" "${sha1_chksum}")
	else
		echo "Error: invalid device type ${device_type}"
		res=1
	fi
	echo "Reading ${part_name} partition done"
	return ${res}
}

do_write()
{
	local item=$1
	local action=$2
	local sha1_chksum=
	local device_type=
	local part_name=
	local file_name=
	local start_offset=
	local file_size=
	local attributes=
	local sha1_chksum=
	local res=0

	sha1_chksum="$(echo "$item" | cut -d, -f 8 | sed 's/^ //g' -)"
	device_type="$(echo "$item" | cut -d, -f 2 | sed 's/^ //g' - | cut -d: -f 1)"
	part_name="$(echo "$item" | cut -d, -f 2 | sed 's/^ //g' - | cut -d: -f 3)"
	file_name="$(echo "$item" | cut -d, -f 5 | sed 's/^ //g' -)"
	start_offset="$(echo "$item" | cut -d, -f 3 | sed 's/^ //g' -)"
	file_size="$(echo "$item" | cut -d, -f 6 | sed 's/^ //g' -)"
	attributes="$(echo "$item" | cut -d, -f 7 | sed 's/^ //g' -)"
	sha1_chksum="$(echo "$item" | cut -d, -f 8 | sed 's/^ //g' -)"

	if [ "${file_name}" = "" ]; then
		echo "Warning: skip writing ${part_name} partition as no image is specified"
		return 0
	fi

	echo "Writing ${part_name} partition with ${file_name}"
	# device_type 0 means "sdmmc_boot", 1 means "sdmmc_user". Other values are invalid.
	if [ "${device_type}" = "${DEVICE_TYPE_SDMMC_BOOT}" ]; then
		if [ "${action}" == "Write_BCT_A_2" ] || [ "${action}" == "Write_BCT_B_2" ]; then
			# Write to the secondary erasable block
			start_offset=$((start_offset + BCT_ERASABLE_BLOCK_SIZE))
		fi
		if [ "${action}" == "Write_BCT_B_1" ] || [ "${action}" == "Write_BCT_B_2" ]; then
			# Write BCT image for booting from chain B
			file_name="${file_name/br_bct/br_bct_b}"
		fi
		flash_sdmmc_boot_partition "${file_name}" "${part_name}" \
			${start_offset} "${file_size}" "${attributes}" "${sha1_chksum}"
		res=$?
		if [ ${res} -ne 0 ]; then
			return ${res}
		fi

		# duplicate the secondary GPT if needed
		if [ "${part_name}" == "secondary_gpt" ] && [ "${NEED_DUPLICATE_GPT}" -eq 1 ]; then
			start_offset=$((start_offset + SDMMC_BOOT0_SIZE))
			flash_sdmmc_boot_partition "${file_name}" "${part_name}" \
				"${start_offset}" "${file_size}" "${attributes}" "${sha1_chksum}"
			res=$?
		fi
	elif [ "${device_type}" = "${DEVICE_TYPE_SDMMC_USER}" ]; then
		flash_sdmmc_user_partition "${file_name}" "${part_name}" \
			${start_offset} "${file_size}" "${attributes}" "${sha1_chksum}"
		res=$?
	elif [ "${device_type}" = "${DEVICE_TYPE_QSPI}" ]; then
		if [ "${action}" == "Write_BCT_A_2" ] || [ "${action}" == "Write_BCT_B_2" ]; then
			# Write to the secondary erasable block
			start_offset=$((start_offset + BCT_ERASABLE_BLOCK_SIZE))
		fi
		if [ "${action}" == "Write_BCT_B_1" ] || [ "${action}" == "Write_BCT_B_2" ]; then
			# Write BCT image for booting from chain B
			file_name="${file_name/br_bct/br_bct_b}"
		fi

		flash_qspi_partition "${file_name}" "${part_name}" \
			${start_offset} "${file_size}" "${attributes}" "${sha1_chksum}"
		res=$?
		if [ ${res} -ne 0 ]; then
			return ${res}
		fi
	elif [ "${device_type}" = "${DEVICE_TYPE_NVME_USER}" ]; then
		flash_nvme_user_partition "${file_name}" "${part_name}" \
			${start_offset} "${file_size}" "${attributes}" "${sha1_chksum}"
		res=$?
	else
		echo "Error: invalid device type ${device_type}"
		return 1
	fi
	echo "Writing ${part_name} partition done"
	return ${res}
}

write_APP()
{
	local file_image="${1}"
	local app_partition="${2}"

	# Set rootfs updater and update rootfs partition with it
	local rootfs_updater=
	if ! set_rootfs_updater "${OTA_WORK_DIR}" rootfs_updater; then
		ota_log "Failed to run \"set_rootfs_updater ${OTA_WORK_DIR} rootfs_updater\""
		return 1
	fi
	echo "eval ${rootfs_updater} -p ${app_partition} -d ${OTA_WORK_DIR} ${file_image}"
	eval "${rootfs_updater}" -p "${app_partition}" -d "${OTA_WORK_DIR}" "${file_image}"
	if [ ! -e "${ROOTFS_UPDATE_SUCCESS_FLAG}" ]; then
		echo "Failed to run \"eval ${rootfs_updater} -p ${app_partition} -d ${OTA_WORK_DIR} ${file_image}\""
		return 1
	fi
	return 0
}

do_write_APP()
{
	local item=$1
	local action=$2
	local partition_name="$3"
	local file_image=
	local sha1_file=
	local sha1_chksum=
	local device_type=
	local dev=

	device_type="$(echo "${item}" | cut -d, -f 2 | sed 's/^ //g' - | cut -d: -f 1)"
	if [ "${device_type}" == "${DEVICE_TYPE_NVME_USER}" ]; then
		dev=${EXT_DEV};
	else
		dev=${INT_DEV};
	fi;
	file_image="${COMMON_IMAGES_DIR}/${dev}/system.img"
	sha1_file="${COMMON_IMAGES_DIR}/${dev}/system.img.sha1sum"

	if [ ! -f "${file_image}" ]; then
		echo "APP image ${file_image} is not found !!!"
		return 1
	fi

	if [ ! -f "${sha1_file}" ]; then
		echo "Sha1 checksum file ${sha1_file} is not found !!!"
		return 1
	fi

	# get the rootfs partition to be updated
	get_rootfs_partition "${partition_name}" "${device_type}" ROOTFS_PARTITION
	if [ ! -e "${ROOTFS_PARTITION}" ]; then
		echo "${ROOTFS_PARTITION} at partition ${partition_name} is not found !"
		return 1
	fi

	# verify sha1 checksum
	sha1_chksum="$(cat "${sha1_file}")"
	if ! sha1_verify "${file_image}" "${sha1_chksum}"; then
		return 1
	fi

	if ! write_APP "${file_image}" "${ROOTFS_PARTITION}"; then
		echo "Error: Failed to write system image ${file_image} to ${ROOTFS_PARTITION}"
		return 1
	fi
	return 0
}

do_set_boot_slot()
{
	local slot=$1

	"${OTA_WORK_DIR}/nvbootctrl" "set-SR-BR" "${slot}"
	return $?
}

do_set_boot_mode()
{
	local item=$1
	local bootmode=$2
	local file_name=
	local active_dir=
	local file_image=
	file_name="$(echo "$item" | cut -d, -f 5 | sed 's/^ //g' -)"
	active_dir="$(dirname "${ACTIVE_INDEX_FILE}")"
	file_image="${active_dir}/${file_name}"

	if [ ! -f "${file_image}.${bootmode}" ]; then
		echo "${file_image}.${bootmode} is not found !!!"
		return 1
	fi
	echo "cp -f \"${file_image}.${bootmode}\" \"${file_image}\""
	cp -f "${file_image}.${bootmode}" "${file_image}"

	do_write "${item}" "Write"
	return $?
}

clean_BCT1_on_qspi()
{
	local item=$1
	local qspi_device="${QSPI_BOOT_DEVICE}"
	local start_offset=
	start_offset=$(echo "$item" | cut -d, -f 3 | sed 's/^ //g' -)
	echo "mtd_debug erase ${qspi_device}  ${start_offset} ${BCT_ERASABLE_BLOCK_SIZE}"
	mtd_debug erase "${qspi_device}" "${start_offset}" "${BCT_ERASABLE_BLOCK_SIZE}"
	res=$?
	sync
	return ${res}
}

clean_BCT1_on_sdmmc_boot()
{
	local item=$1
	local sdmmc_device="${SDMMC_BOOT_DEVICE_0}"
	local start_offset=
	start_offset=$(echo "$item" | cut -d, -f 3 | sed 's/^ //g' -)
	echo "dd if=/dev/zero of=${sdmmc_device} bs=1 seek=${start_offset} count=${BCT_ERASABLE_BLOCK_SIZE}"
	dd if=/dev/zero of="${sdmmc_device}" bs=1 seek="${start_offset}" count="${BCT_ERASABLE_BLOCK_SIZE}"
	res=$?
	sync
	return ${res}
}

do_clean_BCT1()
{
	local item=$1
	local device_type=
	device_type="$(echo "${item}" | cut -d, -f 2 | sed 's/^ //g' - | cut -d: -f 1)"

	# Clean the the first erasable (block 0) of BCT partition
	echo "Clean the first erasable block (block 0) of BCT partition"
	if [ "${device_type}" == "${DEVICE_TYPE_QSPI}" ]; then
		clean_BCT1_on_qspi "${item}"
	else
		clean_BCT1_on_sdmmc_boot "${item}"
	fi
	res=$?
	return ${res}
}

do_clean_qspi()
{
	local item=$1
	local qspi_device="${QSPI_BOOT_DEVICE}"
	local start_offset=
	local part_size=
	start_offset=$(echo "$item" | cut -d, -f 3 | sed 's/^ //g' -)
	part_size=$(echo "${item}" | cut -d, -f 4 | sed 's/^ //g' -)

	mtd_debug erase "${qspi_device}" "${start_offset}" "${part_size}"
	res=$?
	sync
	return ${res}
}

do_clean_sdmmc_user()
{
	local item=$1
	local sdmmc_device="${SDMMC_USER_DEVICE}"
	local start_offset=
	local part_size=
	start_offset=$(echo "$item" | cut -d, -f 3 | sed 's/^ //g' -)
	part_size=$(echo "${item}" | cut -d, -f 4 | sed 's/^ //g' -)

	echo "dd if=/dev/zero of=${sdmmc_device} seek=${start_offset} bs=1 count=${part_size}"
	dd if=/dev/zero of="${sdmmc_device}" seek="${start_offset}" bs=1 count="${part_size}"
	res=$?
	sync
	return ${res}
}

do_clean_nvme_user()
{
	local item=$1
	local nvme_device="${NVME_USER_DEVICE}"
	local start_offset=
	local part_size=
	start_offset=$(echo "$item" | cut -d, -f 3 | sed 's/^ //g' -)
	part_size=$(echo "${item}" | cut -d, -f 4 | sed 's/^ //g' -)

	echo "dd if=/dev/zero of=${nvme_device} seek=${start_offset} bs=1 count=${part_size}"
	dd if=/dev/zero of="${nvme_device}" seek="${start_offset}" bs=1 count="${part_size}"
	res=$?
	sync
	return ${res}
}

do_clean()
{
	local item=$1
	local device_type=
	device_type="$(echo "${item}" | cut -d, -f 2 | sed 's/^ //g' - | cut -d: -f 1)"

	if [ "${device_type}" == "${DEVICE_TYPE_QSPI}" ]; then
		do_clean_qspi "${item}"
	elif [ "${device_type}" == "${DEVICE_TYPE_SDMMC_USER}" ]; then
		do_clean_sdmmc_user "${item}"
	elif [ "${device_type}" == "${DEVICE_TYPE_NVME_USER}" ]; then
		do_clean_nvme_user "${item}"
	else
		ota_log "Unsupported device type ${device_type}"
		return 1
	fi
	res=$?
	return ${res}
}

do_move_qspi()
{
	# Move partition from QSPI to other device (eMMC/SD)
	local item=$1
	local part_name=

	part_name="$(echo "$item" | cut -d, -f 2 | sed 's/^ //g' - | cut -d: -f 3)"

	# Check whether specified partition exist on QSPI
	# In the flash.idx, the name of the partition to be moved out of
	# QSPI is with "_rsv" as suffix.
	local qspi_device_type=3
	local qspi_instance=0
	local search_str=
	local qspi_item=
	search_str="${qspi_device_type}:${qspi_instance}:${part_name}_rsv"
	qspi_item="$(grep -m 1 "${search_str}," <"${ACTIVE_INDEX_FILE}")"
	if [ "${qspi_item}" == "" ]; then
		echo "Warning: partition ${part_name}_rsv is not found on QSPI"
		return 0
	fi

	# Get the source offset on QSPI and the offset on destination device
	local src_offset=
	local dst_offset=
	local partition_size=
	local device_type=
	local src_device="${QSPI_BOOT_DEVICE}"
	local dst_device=
	src_offset="$(echo "${qspi_item}" | cut -d, -f 3 | sed 's/^ //g' -)"
	dst_offset="$(echo "${item}" | cut -d, -f 3 | sed 's/^ //g' -)"
	partition_size="$(echo "${item}" | cut -d, -f 4 | sed 's/^ //g' -)"
	device_type="$(echo "${item}" | cut -d, -f 2 | sed 's/^ //g' - | cut -d: -f 1)"
	if [ "${device_type}" == "${DEVICE_TYPE_SDMMC_USER}" ] || [ "${device_type}" == "${DEVICE_TYPE_SDCARD}" ]; then
		dst_device="${SDMMC_USER_DEVICE}"
	else
		echo "Warning: unsupported device type ${device_type}"
		return 0
	fi

	local aligned=1
	if is_non_aligned_partition "${part_name}"; then
		aligned=0
	fi

	# Move partition on QSPI to the destination device
	# 1. Read partition from QSPI to temporary file
	# 2. Write temporary file into the destination device
	# 3. Do read-after-write to check data consistency
	# 4. Compare sha1sum between source and destination
	local src_img=/tmp/src_img.tmp
	local dst_img=/tmp/dst_img.tmp
	if ! rw_part_opt "${src_device}" "${src_img}" "${src_offset}" 0 "${partition_size}" "${aligned}"; then
		echo "Failed to read ${partition_size} bytes from ${src_device}:${src_offset} to ${src_img}"
		return 1
	fi
	if ! rw_part_opt "${src_img}" "${dst_device}" 0 "${dst_offset}" "${partition_size}" "${aligned}"; then
		echo "Failed to write ${partition_size} bytes from ${src_img} to ${dst_device}:${dst_offset}"
		return 1
	fi
	if ! rw_part_opt "${dst_device}" "${dst_img}" "${dst_offset}" 0 "${partition_size}" "${aligned}"; then
		echo "Failed to read ${partition_size} bytes from ${dst_device}:${dst_offset} to ${dst_img}"
		return 1
	fi
	local src_sha1sum=
	local dst_sha1sum=
	src_sha1sum="$(sha1sum "${src_img}" | cut -d\  -f 1)"
	dst_sha1sum="$(sha1sum "${dst_img}" | cut -d\  -f 1)"
	if [ "${src_sha1sum}" != "${dst_sha1sum}" ]; then
		echo "src_sha1sum=${src_sha1sum} dst_sha1sum=${dst_sha1sum}"
		echo "Failed move partition ${part_name} from ${src_device} to ${dst_device}"
		return 1
	fi

	rm -f "${src_img}"
	rm -f "${dst_img}"

	return 0
}

do_fill_BCT_A()
{
	# Fill BCT partition with multiple BCT images for
	# booting from chain A.
	# Write BCT image into each erasable block except
	# the first one as it has been written before.
	local item=$1
	local action=$2
	local device_type=
	local part_name=
	local file_name=
	local start_offset=
	local file_size=
	local attributes=
	local sha1_chksum=
	local res=0

	device_type="$(echo "$item" | cut -d, -f 2 | sed 's/^ //g' - | cut -d: -f 1)"
	file_name="$(echo "$item" | cut -d, -f 5 | sed 's/^ //g' -)"
	if [ "${file_name}" = "" ]; then
		echo "Warning: skip writing ${part_name} partition as no image is specified"
		return 0
	fi
	if [ "${device_type}" != "${DEVICE_TYPE_SDMMC_BOOT}" ] \
		&& [ "${device_type}" != "${DEVICE_TYPE_QSPI}" ]; then
			echo "Error: invalid device type ${device_type}"
			return 1
	fi

	part_name="$(echo "$item" | cut -d, -f 2 | sed 's/^ //g' - | cut -d: -f 3)"
	part_size=$(echo "${item}" | cut -d, -f 4 | sed 's/^ //g' -)
	start_offset="$(echo "$item" | cut -d, -f 3 | sed 's/^ //g' -)"
	file_size="$(echo "$item" | cut -d, -f 6 | sed 's/^ //g' -)"
	attributes="$(echo "$item" | cut -d, -f 7 | sed 's/^ //g' -)"
	sha1_chksum="$(echo "$item" | cut -d, -f 8 | sed 's/^ //g' -)"


	local num_erasable_blocks=
	local block_num=1

	# Wrting starts from the block 1 of the BCT partition
	num_erasable_blocks=$((part_size / BCT_ERASABLE_BLOCK_SIZE))
	if [ "${num_erasable_blocks}" -le 1 ]; then
		echo "Only one erasable block exists in the BCT partitoin, skip filling it"
		return 0
	fi
	while [ "${block_num}" -lt "${num_erasable_blocks}" ]
	do
		start_offset=$((start_offset + BCT_ERASABLE_BLOCK_SIZE))
		echo "Writing BCT image to block ${block_num} (offset=${start_offset})"
		if [ "${device_type}" = "${DEVICE_TYPE_SDMMC_BOOT}" ]; then
			flash_sdmmc_boot_partition "${file_name}" "${part_name}" \
				${start_offset} "${file_size}" "${attributes}" "${sha1_chksum}"
		else
			flash_qspi_partition "${file_name}" "${part_name}" \
				${start_offset} "${file_size}" "${attributes}" "${sha1_chksum}"
		fi
		res=$?
		if [ ${res} -ne 0 ]; then
			return ${res}
		fi
		block_num=$((block_num + 1))
	done
}

do_action()
{
	local action=$1
	local item=$2
	local res=0

	case ${action} in
		Write|Write_BCT_A_1|Write_BCT_A_2|Write_BCT_B_1|Write_BCT_B_2)
			echo "Do write action"
			do_write "${item}" "${action}"
			return $?
			;;
		Write_APP)
			echo "Do write APP action"
			do_write_APP "${item}" "${action}" "APP"
			return $?
			;;
		Write_APP_b)
			echo "Do write APP_b action"
			do_write_APP "${item}" "${action}" "APP_b"
			return $?
			;;
		Read)
			echo "Do read action"
			do_read "${item}"
			return $?
			;;
		Set_Boot_Recovery_Mode)
			echo "Do set boot to recovery mode"
			do_set_boot_mode "${item}" "update"
			return $?
			;;
		Set_Boot_Normal_Mode)
			echo "Do set boot to normal mode"
			do_set_boot_mode "${item}" "normal"
			return $?
			;;
		Set_Boot_Slot_A)
			echo "Do set boot to slot A"
			do_set_boot_slot "0"
			return $?
			;;
		Set_Boot_Slot_B)
			echo "Do set boot to slot B"
			do_set_boot_slot "1"
			return $?
			;;
		Clean_BCT1)
			echo "Do clean BCT1"
			do_clean_BCT1 "${item}"
			return $?
			;;
		Clean)
			echo "Do clean action"
			do_clean "${item}"
			return $?
			;;
		Move_QSPI)
			echo "Do move QSPI partition"
			do_move_qspi "${item}"
			return $?
			;;
		Fill_BCT_A)
			echo "Fill BCT partition with multiple BCT images for booting from chain A"
			do_fill_BCT_A "${item}"
			return $?
			;;
		*)
			echo "Invalid action ${action}"
			return 1
	esac
}

run_task()
{
	local action=$1
	local device=$2
	local partition=$3
	local index_file=$4
	local device_type=
	local res=0

	if [ "${index_file}" != "${ACTIVE_INDEX_FILE}" ]; then
		readarray ACTIVE_INDEX_ARRAY < "${index_file}"
		ACTIVE_INDEX_FILE="${index_file}"
	fi
	echo "Active index file is ${ACTIVE_INDEX_FILE}"

	local lines_num=${#ACTIVE_INDEX_ARRAY[@]}
	echo "Number of lines is $lines_num"

	local max_index=$((lines_num - 1))
	echo "max_index=${max_index}"

	local item=
	local part_name=
	for i in $(seq 0 ${max_index})
	do
		item=${ACTIVE_INDEX_ARRAY[$i]}

		part_name="$(echo "$item" | cut -d, -f 2 | sed 's/^ //g' - | cut -d: -f 3)"
		if [ "${part_name}" != "${partition}" ]; then
			continue
		fi

		device_type="$(echo "$item" | cut -d, -f 2 | sed 's/^ //g' - | cut -d: -f 1)"
		if { [ "${device_type}" = "${DEVICE_TYPE_SDMMC_BOOT}" ] && [ "${device}" = "sdmmc_boot" ]; } \
			|| { [ "${device_type}" = "${DEVICE_TYPE_SDMMC_USER}" ] && [ "${device}" = "sdmmc_user" ]; } \
			|| { [ "${device_type}" = "${DEVICE_TYPE_QSPI}" ] && [ "${device}" = "qspi" ]; } \
			|| { [ "${device_type}" = "${DEVICE_TYPE_SDCARD}" ] && [ "${device}" = "sdcard" ]; } \
			|| { [ "${device_type}" = "${DEVICE_TYPE_NVME_USER}" ] && [ "${device}" = "nvme" ]; }; then
			echo -n "action=${action} item=${item}"
			do_action "${action}" "${item}"
			return $?
		fi
	done
}


if [ $# -lt 1 ]; then
	usage
fi

nargs=$#;
control_file=${!nargs};
nargs=$((nargs-1))
target_board=${!nargs}

if [ ! -f "${control_file}" ]; then
	echo "Specified control file ${control_file} is not found"
	usage
fi

opstr+="h:s:-:";
while getopts "${opstr}" OPTION; do
	case $OPTION in
	h) usage; ;;
	s) VERIFY_WRITE=0; echo "VERIFY_WRITE=0";;
	*) usage; ;;
	esac;
done

OTA_WORK_DIR="$(cd "$(dirname "${control_file}")" && pwd)"
COMMON_IMAGES_DIR="${OTA_WORK_DIR}"

echo "Control file is ${control_file}, under ${OTA_WORK_DIR}"

readarray controlarray < "${control_file}"

lines_num=${#controlarray[@]}
echo "Number of lines is $lines_num"

max_index=$((lines_num - 1))
echo "max_index=${max_index}"

# make mmcblk0bootX writable
if [ -e "/sys/block/mmcblk0boot0/force_ro" ]; then
	echo 0 >/sys/block/mmcblk0boot0/force_ro
	echo 0 >/sys/block/mmcblk0boot1/force_ro
fi

# get sector size of boot devie and user device
if [ -e "${QSPI_BOOT_DEVICE}" ];then
	BOOT_DEV_SECTOR_SIZE=$(cat /sys/class/mtd/mtd0/erasesize)
	set_bct_erasable_block_size "${BOOT_DEV_SECTOR_SIZE}" "${BCT_BLOCK_SIZE_QSPI}"
else
	BOOT_DEV_SECTOR_SIZE=$(cat /sys/block/mmcblk0boot0/queue/hw_sector_size)
	set_bct_erasable_block_size "${BOOT_DEV_SECTOR_SIZE}" "${BCT_BLOCK_SIZE_SDMMC}"
	SDMMC_BOOT0_SIZE=$(cat /sys/block/mmcblk0boot0/size)
	SDMMC_BOOT0_SIZE=$((SDMMC_BOOT0_SIZE * 1024 / 2))

	# For AGX Xavier, if boot devices have 16 MiB in size (each has 8 MiB),
	# we need to duplicate the secondary GPT at the end of 16 MiB
	if [ "${target_board}" == "jetson-agx-xavier-devkit" ] && [ "${SDMMC_BOOT0_SIZE}" == "${SIZE_8_MiB}" ]; then
		NEED_DUPLICATE_GPT=1
	fi
fi

# Copy "${work_dir}/ota_nv_boot_control.conf" to /etc/ on the initrd fs as
# nvbootctrl needs it.
if [ ! -f "${OTA_WORK_DIR}/ota_nv_boot_control.conf" ]; then
        ota_log "${OTA_WORK_DIR}/ota_nv_boot_control.conf is not found"
        exit 1
fi
cp -f "${OTA_WORK_DIR}/ota_nv_boot_control.conf" "/etc/nv_boot_control.conf"

set +e
# Restart from the last action index if unexpected reset occurs
# in the middle of OTA process.
if [ -e "${OTA_WORK_DIR}/${LAST_ACTION_INDEX_FILE}" ]; then
	start_index=$(cat "${OTA_WORK_DIR}/${LAST_ACTION_INDEX_FILE}")
else
	start_index=0
fi

for i in $(seq ${start_index} ${max_index})
do
	echo "${i}" > "${OTA_WORK_DIR}/${LAST_ACTION_INDEX_FILE}"

	item="${controlarray[$i]}"
	echo -n "line[$i]: ${item}"

	# skip empty line
	if [ "${#item}" = "1" ]; then
		continue
	fi

	# skip comment line
	if [[ "${item}" =~ ^# ]]; then
		continue
	fi

	# skip invalid line
	if [[ ! "${item}" =~ Write|Read|Set_Boot|Clean|Move_QSPI|Restore_BCT ]]; then
		continue
	fi

	stage="$(echo "${item}" | cut -d: -f 1)"
	action="$(echo "${item}" | cut -d: -f 2)"
	device="$(echo "${item}" | cut -d: -f 3)"
	partition="$(echo "${item}" | cut -d: -f 4)"
	index_file="$(echo "${item}" | cut -d: -f 5)"
	if [ "${device}" == "${DEVICE_NVME}" ]; then
		index_file="${OTA_WORK_DIR}/${EXT_DEV}/${index_file}"
	else
		index_file="${OTA_WORK_DIR}/${INT_DEV}/${index_file}"
	fi

	echo "stage=${stage} action=${action} device=${device} partition=${partition} index_file=${index_file}"

	if [ ! -f "${index_file}" ]; then
		echo "Index file ${index_file} is not found"
		continue
	fi

	if [ "${stage}" != "${CURRENT_STAGE}" ]; then
		CURRENT_STAGE=${stage}
		echo "Task stage ${CURRENT_STAGE} starts"
	fi
	if ! run_task "${action}" "${device}" "${partition}" "${index_file}"; then
		echo "Failed to run_task ${action} ${device} ${partition} ${index_file}"
		exit 1
	fi
done

# set mmcblk0bootX readonly
if [ -e "/sys/block/mmcblk0boot0/force_ro" ]; then
	echo 1 >/sys/block/mmcblk0boot0/force_ro
	echo 1 >/sys/block/mmcblk0boot1/force_ro
fi

# Properly set current rootfs slot in SR_RF for L4tlauncher.
# This can be done via nvbootctrl verify command.
"${OTA_WORK_DIR}/nvbootctrl" "verify"

# mark OTA is success
echo 1 >/tmp/ota_success
