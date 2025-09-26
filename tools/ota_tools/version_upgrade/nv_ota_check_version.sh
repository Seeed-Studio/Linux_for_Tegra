#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2019-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

# This is a script to check the version information and some key partitions
# for OTA process (BCT/MB1/MB1_BCT)
_BCT_BLOCK_SIZE=16384
_BCT_FILE_SIZE=
_BCT_SLOT_SIZE=
_MB1_PARTITION_OFFSET=32768
_MB1_PARTITION_SIZE=262144
_MB1_BCT_PARTITION_OFFSET=557056
_MB1_BCT_PARTITION_SIZE=65536
_MB1_BCT_SLOT_SIZE=65536
_BOOTDEV_SECTOR_SIZE=

# Ideally, the VER_b is put on the -128KB to the end of boot device and
# the VER is put on the -64KB to the end of boot device, and the size of
# both these 2 partitions are 32KB in the latest partition layout.
# However, in some old version, the VER_b/VER does not exist or are not at
# these expected loction and the size of VER_b/VER are different. In this
# case, the offset and size of VER/VER_b needs to be obtained by parsing the
# secondary gpt on the boot device
_VER_PART_OFFSET=
_VER_B_PART_OFFSET=
_VER_PART_SIZE=
_VER_B_PART_SIZE=

# For jetson-agx-xavier-devkit, the VER_b and VER partion might be on
# the "mmcblk0boot0" or "mmcblk0boot1" or both
# For jetson-xavier-nx-devkit-emmc, both VER_b and VER partions are on QSPI
_MMC_BOOT0_DEVICE="/dev/mmcblk0boot0"
_MMC_BOOT1_DEVICE="/dev/mmcblk0boot1"
_MMC_USER_DEVICE="/dev/mmcblk0"
_QSPI_BOOT_BLKDEV="/dev/mtdblock0"

# Version numbers
_OTA_VER_NUM=
_VER_NUM=
_VER_B_NUM=
_MAX_GPT_ENTRIES_NUM=128

# Use file to mark OTA update start
_OTA_UPDATE_START_FILE="ota_update_start"

is_recovery_kernel()
{
	local rootdev=
	rootdev="$(grep "root=/dev/initrd" </proc/cmdline)"
	if [ -z "${rootdev}" ]; then
		return 1
	else
		return 0
	fi
}

get_ver_offset()
{
	local boot_device="$1"
	local boot_bin="/tmp/boot.bin"
	local sec_gpt_header_bin="/tmp/sec_gpt_header.bin"
	local sec_gpt_entries_bin="/tmp/sec_gpt_entries.bin"
	dd if="${boot_device}" of="${boot_bin}"

	local boot_device_size=
	boot_device_size=$(blockdev --getsize64 "${boot_device}")
	ota_log "boot_device_size=${boot_device_size}"

	local sec_gpt_header_size=512
	local sec_gpt_header_offset=$((boot_device_size - sec_gpt_header_size))
	local sec_gpt_offset=$((sec_gpt_header_size + 128 * 128))
	sec_gpt_offset=$((boot_device_size - sec_gpt_offset))
	local sec_gpt_entries_size=$((128 * 128))
	local sec_gpt_entry_size=128
	local full_zero_sha1sum="0ae4f711ef5d6e9d26c611fd2c8c8ac45ecbf9e7"

	dd if=${boot_bin} of=${sec_gpt_header_bin} skip=${sec_gpt_header_offset} bs=1 count=${sec_gpt_header_size} >/dev/null 2>&1
	dd if=${boot_bin} of=${sec_gpt_entries_bin} skip=${sec_gpt_offset} bs=1 count=${sec_gpt_entries_size} >/dev/null 2>&1

	# GPT entry
	local part_name_offset=56
	local part_name_size=72
	local entry_offset=0
	local count=1
	local entry_bin="/tmp/entry.bin"
	local part_name_bin="/tmp/part_name.bin"
	local lba_offset_bin="/tmp/lba_offset.bin"
	local first_lba_offset=32
	local last_lba_offset=40
	local lba_offset_size=8
	local first_lba=
	local last_lba=
	local part_name=
	local lba_size=512
	while [ ${count} -le ${_MAX_GPT_ENTRIES_NUM} ]
	do
		dd if=${sec_gpt_entries_bin} of=${entry_bin} skip=${entry_offset} bs=1 count=${sec_gpt_entry_size} >/dev/null 2>&1
		entry_sha1sum="$(sha1sum ${entry_bin} | cut -d\  -f 1)"

		# Skip empty gpt entry
		if [ "${entry_sha1sum}" == "${full_zero_sha1sum}" ]; then
			count=$((count + 1))
			entry_offset=$((entry_offset + sec_gpt_entry_size))
			continue
		fi
		dd if=${entry_bin} of=${part_name_bin} skip=${part_name_offset} bs=1 count=${part_name_size} >/dev/null 2>&1
		if [ -e "/usr/bin/strings" ]; then
			part_name="$(strings -n 1 -e l ${part_name_bin})"
		else
			# Avoid the warning "ignored null byte in input"
			# when executing "cat" command directly
			sed -i 's/\x00//g' "${part_name_bin}" 2>/dev/null
			part_name="$(cat ${part_name_bin})"
		fi
		# Skip if not VER or VER_b partition
		if [ "${part_name}" != "VER" ] && [ "${part_name}" != "VER_b" ]; then
			count=$((count + 1))
			entry_offset=$((entry_offset + sec_gpt_entry_size))
			continue
		fi
		# Get the first lba of VER/VER_b
		dd if=${entry_bin} of=${lba_offset_bin} skip=${first_lba_offset} bs=1 count=${lba_offset_size} >/dev/null 2>&1
		first_lba=$(xxd -g 1 ${lba_offset_bin} | cut -d\  -f 2-9)
		total=0
		for i in $(seq 8 -1 1)
		do
			num=$(echo "${first_lba}" | cut -d\  -f "$i")
			num=$((0x${num}))
			total=$((total * 256 + num))
		done
		first_lba=${total}
		# Get the first lba of VER/VER_b
		dd if=${entry_bin} of=${lba_offset_bin} skip=${last_lba_offset} bs=1 count=${lba_offset_size} >/dev/null 2>&1
		last_lba=$(xxd -g 1 ${lba_offset_bin} | cut -d\  -f 2-9)
		total=0
		for i in $(seq 8 -1 1)
		do
			num=$(echo "${last_lba}" | cut -d\  -f "$i")
			num=$((0x${num}))
			total=$((total * 256 + num))
		done
		last_lba=${total}
		if [ "${part_name}" == "VER" ]; then
			_VER_PART_OFFSET=$((first_lba * lba_size))
			_VER_PART_SIZE=$(((last_lba - first_lba + 1) * lba_size))
			ota_log "VER's offset is ${_VER_PART_OFFSET} and size is ${_VER_PART_SIZE}"
		else
			_VER_B_PART_OFFSET=$((first_lba * lba_size))
			_VER_B_PART_SIZE=$(((last_lba - first_lba + 1) * lba_size))
			ota_log "VER_b's offset is ${_VER_B_PART_OFFSET} and size is ${_VER_B_PART_SIZE}"
		fi
		count=$((count + 1))
		entry_offset=$((entry_offset + sec_gpt_entry_size))
	done
}

# Get VER/VER_b partition offset from the "flash.idx" file
get_ver_info()
{
	local ota_work_dir=$1
	local ret_ver_part_offset=$2
	local ret_ver_b_part_offset=$3
	local ret_ver_part_size=$4
	local ret_ver_b_part_size=$5
	local index_file=
	local ver_part_offset=
	local ver_b_part_offset=
	local ver_part_size=
	local ver_b_part_size=

	local last_interm_dir=
	last_interm_dir="$(get_last_interm_dir "${ota_work_dir}" "${INT_DEV}")"
	index_file="${last_interm_dir}/flash.idx"
	if [ ! -f "${index_file}" ]; then
		ota_log "The index file ${index_file} is not found"
		return 1
	fi
	ver_part_offset=$(grep "VER," < "${index_file}" | cut -d, -f 3 | sed 's/^ //g')
	if [ "${ver_part_offset}" == "" ]; then
		ota_log "Failed to get the offset of VER partition"
		return 1
	fi
	ver_part_size=$(grep "VER," < "${index_file}" | cut -d, -f 4 | sed 's/^ //g')
	if [ "${ver_part_size}" == "" ]; then
		ota_log "Failed to get the size of VER partition"
		return 1
	fi
	ver_b_part_offset=$(grep "VER_b," < "${index_file}" | cut -d, -f 3 | sed 's/^ //g')
	if [ "${ver_b_part_offset}" == "" ]; then
		ota_log "Failed to get the offset of VER_b partition"
		return 1
	fi
	ver_b_part_size=$(grep "VER_b," < "${index_file}" | cut -d, -f 4 | sed 's/^ //g')
	if [ "${ver_b_part_size}" == "" ]; then
		ota_log "Failed to get the size of VER_b partition"
		return 1
	fi

	eval "${ret_ver_part_offset}=${ver_part_offset}"
	eval "${ret_ver_b_part_offset}=${ver_b_part_offset}"
	eval "${ret_ver_part_size}=${ver_part_size}"
	eval "${ret_ver_b_part_size}=${ver_b_part_size}"
	return 0
}

get_ver_offset_in_recovery_kernel()
{
	local work_dir="$1"
	local boot_device="$2"
	local base_version="$3"
	local ota_update_start_file="${work_dir}/${_OTA_UPDATE_START_FILE}"

	if [ -f "${ota_update_start_file}" ]; then
		# Get the offset of VER_b/VER partitions from index file
		# when OTA update has started.
		if ! get_ver_info "${work_dir}" "_VER_PART_OFFSET" "_VER_B_PART_OFFSET" "_VER_PART_SIZE" "_VER_B_PART_SIZE"; then
			ota_log "Failed to run \"get_ver_info ${work_dir} _VER_PART_OFFSET _VER_B_PART_OFFSET _VER_PART_SIZE _VER_B_PARTI_SIZE\""
			return 1
		fi
	else
		# If the OTA update has not been started yet, follow way of
		# getting offset of VER_b/VER partition in the base system
		get_ver_offset "${boot_device}"
	fi

}

version_crc32_verify()
{
	local version_file=$1

	if [ ! -f "${version_file}" ];then
		ota_log "Version file ${version_file} is not found"
		return 1
	fi

	set +e
	local version_size=
	version_size="$(grep -o -E "BYTES:[0-9]+" < "${version_file}" | cut -d: -f 2)"
	if [ "${version_size}" = "" ];then
		ota_log "Version size is not found"
		return 1
	fi

	local version_crc32=
	version_crc32="$(grep -o -E "CRC32:[0-9A-Z]+" < "${version_file}" | cut -d: -f 2)"
	if [ "${version_crc32}" = "" ];then
		ota_log "Version crc32 is not found"
		return 1
	fi
	set -e

	local version_file_tmp=/tmp/version.txt.tmp
	dd if="${version_file}" of="${version_file_tmp}" bs=1 count="${version_size}" >/dev/null 2>&1
	local version_cksum=
	local version_format=
	version_format=$(head -n 1 "${version_file}")
	if [ "${version_format}" == "NV4" ]; then
		# The CRC32 is calculated via python zlib module in flash.sh, but we don't have python
		# in the recovery kernel. So make use of gzip for CRC32 calculation here.
		version_cksum="$(gzip < "${version_file_tmp}" | tail -c 8 | head -c 4 | xxd -e -u | awk '{sub(/^0*/, "", $2); print $2}')"
	else
		version_cksum="$(cksum "${version_file_tmp}" | cut -d\  -f 1)"
	fi
	if [ "${version_crc32}" != "${version_cksum}" ];then
		ota_log "CRC32 for version file ${version_file} does not match (${version_crc32} != ${version_cksum})"
		return 1
	fi
	return 0
}

# Compare slot A and slot B for BCT, MB1 and MB1_BCT partitions
# to make sure both slots are good for each of these partitions
# before starting a fresh OTA.
check_BCT_MB1_MB1BCT()
{
	local boot_device="$1"
	local tmp_file_a=/tmp/tmp_file_a
	local tmp_file_b=/tmp/tmp_file_b

	# Check BCT
	ota_log "Checking BCT partition"
	dd if="${boot_device}" of=${tmp_file_a} bs=1 count="${_BCT_FILE_SIZE}" >/dev/null 2>&1
	dd if="${boot_device}" of=${tmp_file_b} bs=1 skip="${_BCT_SLOT_SIZE}" count="${_BCT_FILE_SIZE}" >/dev/null 2>&1
	local chksum_a=
	local chksum_b=
	chksum_a="$(sha1sum "${tmp_file_a}" | cut -d\  -f 1)"
	chksum_b="$(sha1sum "${tmp_file_b}" | cut -d\  -f 1)"
	if [ "${chksum_a}" != "${chksum_b}" ];then
		ota_log "BCT check sum does not mactch (${chksum_a} != ${chksum_b})"
		return 1
	fi

	dd if="${boot_device}" of=${tmp_file_a} bs=1 skip=${_BCT_BLOCK_SIZE} count="${_BCT_FILE_SIZE}" >/dev/null 2>&1
	chksum_a="$(sha1sum "${tmp_file_a}" | cut -d\  -f 1)"
	if [ "${chksum_a}" != "${chksum_b}" ];then
		ota_log "BCT check sum does not mactch (${chksum_a} != ${chksum_b})"
		return 1
	fi

	# Check MB1
	ota_log "Checking MB1 partition"
	local tmp_offset=$((_MB1_PARTITION_OFFSET + _MB1_PARTITION_SIZE))
	dd if="${boot_device}" of=${tmp_file_a} bs=1 skip=${_MB1_PARTITION_OFFSET} count=${_MB1_PARTITION_SIZE} >/dev/null 2>&1
	dd if="${boot_device}" of=${tmp_file_b} bs=1 skip=${tmp_offset} count=${_MB1_PARTITION_SIZE} >/dev/null 2>&1
	chksum_a="$(sha1sum "${tmp_file_a}" | cut -d\  -f 1)"
	chksum_b="$(sha1sum "${tmp_file_b}" | cut -d\  -f 1)"
	if [ "${chksum_a}" != "${chksum_b}" ];then
		ota_log "MB1 check sum does not mactch (${chksum_a} != ${chksum_b})"
		return 1
	fi

	# Check MB1_BCT
	ota_log "Checking MB1_BCT partition"
	tmp_offset=$((_MB1_BCT_PARTITION_OFFSET + _MB1_BCT_PARTITION_SIZE))
	dd if="${boot_device}" of="${tmp_file_a}" bs=1 skip="${_MB1_BCT_PARTITION_OFFSET}" count="${_MB1_BCT_SLOT_SIZE}" >/dev/null 2>&1
	dd if="${boot_device}" of="${tmp_file_b}" bs=1 skip="${tmp_offset}" count="${_MB1_BCT_SLOT_SIZE}" >/dev/null 2>&1
	chksum_a="$(sha1sum "${tmp_file_a}" | cut -d\  -f 1)"
	chksum_b="$(sha1sum "${tmp_file_b}" | cut -d\  -f 1)"
	if [ "${chksum_a}" != "${chksum_b}" ];then
		ota_log "MB1_BCT check sum does not mactch (${chksum_a} != ${chksum_b})"
		return 1
	fi
	return 0
}

# Get bsp version from VER/VER_b partitions
get_bsp_ver_from_part()
{
	local boot_device="$1"
	local target="$2"
	local ver_bin="$3"
	local boot_device_size=
	local part_start=
	local part_size=
	local part_end=

	if  [ "${target}" == "VER_b" ]; then
		part_start=${_VER_B_PART_OFFSET}
		part_size=${_VER_B_PART_SIZE}
	else
		part_start=${_VER_PART_OFFSET}
		part_size=${_VER_PART_SIZE}
	fi
	part_end=$((part_start + part_size))
	boot_device_size=$(blockdev --getsize64 "${boot_device}")

	# Determine whether VER/VER_b is on boot0 or boot1 and
	# then read the version information from VER/VER_b.
	# For platforms using QSPI as boot device, VER/VER_b are always on QSPI.
	if [ "${part_start}" -ge "${boot_device_size}" ] || \
		[ "${part_end}" -le "${boot_device_size}" ]; then
		# VER/VER_b is either on boot0 or boot1
		if [ "${boot_device}" != "${_QSPI_BOOT_BLKDEV}" ]; then
			if [ "${part_start}" -ge "${boot_device_size}" ]; then
				# VER/VER_b is on boot1
				boot_device="${_MMC_BOOT1_DEVICE}"
				part_start=$((part_start - boot_device_size))
			else
				# VER/VER_b is on boot0
				boot_device="${_MMC_BOOT0_DEVICE}"
			fi
		fi

		# Read version from VER/VER_b partition
		if ! dd if="${boot_device}" of="${ver_bin}" bs=1 skip=${part_start} count=${part_size} >/dev/null 2>&1; then
			ota_log "Failed to read ${target} partition from ${boot_device} by \"dd\" command"
			return 1
		fi
	else
		# VER/VER_b is located on both "mmcblk0boot0"
		# and "mmcblk0boot1".
		boot_device="${_MMC_BOOT0_DEVICE}"
		tmp_size=$((boot_device - part_start))
		if ! dd if="${boot_device}" of="${ver_bin}" bs=1 skip=${part_start} count=${tmp_size} >/dev/null 2>&1; then
			ota_log "Failed to read ${target} partition from ${boot_device} by \"dd\" command"
			return 1
		fi
		boot_device="${_MMC_BOOT1_DEVICE}"
		part_end=$((part_end - boot_device_size))
		if ! dd if="${boot_device}" of="${ver_bin}" bs=1 seek=${tmp_size} count=${part_end} conv=notrunc >/dev/null 2>&1; then
			ota_log "Failed to read ${target} partition from ${boot_device} by \"dd\" command"
			return 1
		fi

	fi
}

get_bsp_version_number()
{
	local work_dir="$1"
	local boot_device="$2"
	local target="$3"
	local ret="$4"
	local version_file=
	local ver_bin=
	local ver_str=
	local ver_num=
	local bsp_branch=
	local bsp_rev=
	local bsp_major=
	local bsp_minor=

	if [ "${target}" == "OTA_PACKAGE" ]; then
		version_file="${work_dir}/version.txt"
		if [ ! -f "${version_file}" ];then
			ota_log "Version file ${version_file} is not found"
			return 1
		fi
	else
		if [[ ! "${target}" =~ "VER" ]]; then
			ota_log "Invalid parameter ${target}"
			return 1
		fi

		# Get bsp version from VER/VER_b partition
		ver_bin="${work_dir}/ver_file.bin"
		if ! get_bsp_ver_from_part "${boot_device}" "${target}" "${ver_bin}"; then
			ota_log "Failed to run \"get_bsp_ver_from_part ${boot_device} ${target} ${ver_bin}\""
			return 1
		fi

		# Read the version and write it into "${version_file}"
		version_file="${work_dir}/ver_cur.txt"
		ver_str="$(tr -d '\0' < "${ver_bin}")"
		echo -n "${ver_str}" > "${version_file}"
	fi

	if ! version_crc32_verify "${version_file}"; then
		ota_log "Error happens in calling \"version_crc32_verify ${version_file}\""
		ver_num=0
	else
		bsp_branch="$(grep -o -E "# R[0-9]+" < "${version_file}" | cut -dR -f 2)"
		bsp_rev="$(grep -o -E "REVISION: [0-9]\.[0-9]" < "${version_file}" | cut -d\  -f 2)"
		bsp_major="$(echo "${bsp_rev}" | cut -d\.  -f 1)"
		bsp_minor="$(echo "${bsp_rev}" | cut -d\.  -f 2)"
		if [ "${bsp_branch}" != "" ] && \
			[ "${bsp_major}" != "" ] && \
			[ "${bsp_minor}" != "" ]; then
			ota_log "${target} version: branch:${bsp_branch} revision:${bsp_rev} major.minor:${bsp_major}.${bsp_minor}"
			ver_num=$((bsp_branch * 10000 + bsp_major * 100 + bsp_minor))
		else
			ver_num=0
		fi
	fi

	eval "${ret}=${ver_num}"
	return 0
}

ota_check_rollback()
{
	local work_dir="$1"
	if [ ! -d "${work_dir}" ];then
		ota_log "OTA work directory ${work_dir} does not exist"
		return 1
	fi

	local target_board="$2"
	local base_version="$3"
	local ota_device="$4"

	local boot_device=
	case ${target_board} in
		jetson-agx-xavier-devkit)
			_BOOTDEV_SECTOR_SIZE=512
			_BCT_FILE_SIZE=2888
			_BCT_SLOT_SIZE=$(((_BCT_FILE_SIZE + _BOOTDEV_SECTOR_SIZE - 1) / _BOOTDEV_SECTOR_SIZE * _BOOTDEV_SECTOR_SIZE))
			boot_device="${_MMC_BOOT1_DEVICE}"
			;;
		jetson-agx-xavier-industrial)
			if [ "${ota_device}" != "${_MMC_USER_DEVICE}" ]; then
				_BCT_BLOCK_SIZE=32768
			else
				_BCT_BLOCK_SIZE=131072
			fi
			_BCT_FILE_SIZE=2888
			_BCT_SLOT_SIZE=4096
			_MB1_PARTITION_OFFSET=262144
			_MB1_BCT_PARTITION_OFFSET=786432
			_MB1_BCT_PARTITION_SIZE=262144
			boot_device="${_QSPI_BOOT_BLKDEV}"
			;;
		jetson-xavier-nx-devkit-emmc)
			_BOOTDEV_SECTOR_SIZE=$(cat /sys/class/mtd/mtd0/erasesize)
			_BCT_BLOCK_SIZE=65536
			_BCT_FILE_SIZE=2888
			_BCT_SLOT_SIZE=$(((_BCT_FILE_SIZE + _BOOTDEV_SECTOR_SIZE - 1) / _BOOTDEV_SECTOR_SIZE * _BOOTDEV_SECTOR_SIZE))
			_MB1_PARTITION_OFFSET=131072
			_MB1_BCT_PARTITION_OFFSET=655360
			boot_device="${_QSPI_BOOT_BLKDEV}"
			;;
		*)
		   ota_log "Invalid target board ${target_board}"
		   return 1
		   ;;
	esac;

	# Read version information from version file in ota package
	if ! get_bsp_version_number "${work_dir}" "${boot_device}" "OTA_PACKAGE" "_OTA_VER_NUM"; then
		ota_log "Failed to call \"get_bsp_version_number ${work_dir} ${boot_device} OTA_PACKAGE _OTA_VER_NUM\""
		return 1
	fi

	# Get offset of the VER/VER_b partition on the boot device
	# in the base system, VER/VER_b does not exist before R32.3,
	# no need to try getting offset of the VER/VER_b
	# in the recovery kernel, there are many cases:
	# 1. VER/VER_b partition will be created and written
	# 2. VER/VER_b partition will be re-located and written
	# 3. VER/VER_b partition will be written only.
	if ! is_recovery_kernel; then
		get_ver_offset "${boot_device}"
	else
		get_ver_offset_in_recovery_kernel "${work_dir}" "${boot_device}" "${base_version}"
	fi

	# If VER/VER_b partition exist, read version information from them
	# else, directly set "VER_NUM" or "VER_B_NUM" to 0
	if [ -n "${_VER_B_PART_OFFSET}" ]; then
		# read version information from VER_b partition
		if ! get_bsp_version_number "${work_dir}" "${boot_device}" "VER_b" "_VER_B_NUM"; then
			ota_log "Failed to call \"get_bsp_version_number ${work_dir} ${boot_device} VER_b _VER_B_NUM\""
			return 1
		fi
	else
		ota_log "VER_b partition does not exist."
		_VER_B_NUM=0
	fi
	if [ -n "${_VER_PART_OFFSET}" ]; then
		# Read version information from VER partition
		if ! get_bsp_version_number "${work_dir}" "${boot_device}" "VER" "_VER_NUM"; then
			ota_log "Failed to call \"get_bsp_version_number ${work_dir} ${boot_device} VER _VER_NUM\""
			return 1
		fi
	else
		ota_log "VER partition does not exist."
		_VER_NUM=0
	fi

	# The OTA procedure following the this rule:
	# ota_begin: write the "${_OTA_VER_NUM}" into "VER_b" partition
	# ota_update: update each partition except VER_b/VER
	# ota_end: write the "${_OTA_VER_NUM}" into "VER" partition
	# Then version rollback can be prevented by checking the version
	# information read from "VER" and "VER_b"
	local ver_check_res=
	if [ ${_VER_NUM} -eq ${_VER_B_NUM} ];then
		if [ ${_VER_NUM} -eq 0 ];then
			# Both VER and VER_b are invalid
			# This is the case that no VER/VER_b partition exists,so
			# it can start OTA
			ver_check_res=0
		else
			# VER/VER_b are valid
			if [ ${_VER_NUM} -gt "${_OTA_VER_NUM}" ];then
				# System VER > OTA VER
				ota_log "Version rollback is not permitted"
				ver_check_res=1
			else
				# System VER <= OTA VER
				ver_check_res=0
			fi
		fi

		# For fresh OTA, need to check the BCT/MB1/MB1_BCT partitions
		if [ ${ver_check_res} -eq 0 ];then
			ota_log "Check BCT/MB1/MB1_BCT partiton for fresh OTA"
			if ! check_BCT_MB1_MB1BCT "${boot_device}"; then
				ver_check_res=1
			fi
		fi
	else
		if [ ${_VER_B_NUM} -eq 0 ] ;then
			# VER_b is invalid but VER is valid
			# Error happens when writing VER_b and then VER_b is corrupted
			# OTA can be started if OTA version is greater and equal than VER
			if [ ${_VER_NUM} -gt "${_OTA_VER_NUM}" ];then
				# System VER > OTA VER
				ota_log "Version rollback is not permitted"
				ver_check_res=1
			else
				# System VER <= OTA VER
				# Regard this case as fresh OTA as it is failed in
				# writing VER_b
				ota_log "Check BCT/MB1/MB1_BCT partiton for invalid VER_b"
				if ! check_BCT_MB1_MB1BCT "${boot_device}"; then
					ver_check_res=1
				else
					ver_check_res=0
				fi
			fi
		else
			# VER_b is valid but VER invalid, or
			# both VER and VER_b are valid but VER != VER_b
			# OTA (to VER_b) is started but not correctly finished
			# This OTA should be continued before starting any new OTA process
			if [ ${_VER_B_NUM} -lt ${_VER_NUM} ];then
				ota_log "Unknown issue happens, this deivce can not be OTA"
				ota_log "VER:${_VER_NUM}, VER_b:${_VER_B_NUM}"
				ver_check_res=1
			else
				if [ ${_VER_B_NUM} -eq "${_OTA_VER_NUM}" ];then
					ver_check_res=0
				else
					ota_log "Last OTA (to ${_VER_B_NUM}) is not finished, please complete it first"
					ver_check_res=1
				fi
			fi
		fi
	fi

	ota_log "ver_check_res=${ver_check_res}"
	return ${ver_check_res}
}

