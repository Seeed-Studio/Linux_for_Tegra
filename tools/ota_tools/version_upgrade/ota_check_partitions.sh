#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2020-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

_MAX_GPT_ENTRIES_NUM=128
_MMC_BOOT_DEVICE="/dev/mmcblk0boot1"
_QSPI_BOOT_DEVICE="/dev/mtdblock0"

# For "DRAMECCNAME" and "BADPAGENAME" partitions, their names
# are not replaced with "dram-fw-ecc" and "badpage-fw" if
# no image is written into it during flashing.
# Use a table to handle the name difference.
_PART_NAME_COMPATBILE_TABLE=(
	# <partition_name_1>:<partition_name_2>
	# dram-ecc-fw partition
	'dram-ecc-fw:DRAMECCNAME'
	'dram-ecc-fw_b:DRAMECCNAME_b'

	# badpage-fw partition
	'badpage-fw:BADPAGENAME'
	'badpage-fw_b:BADPAGENAME_b'

	# For JAXI only, the bad-page partition exists
	'bad-page:badpage-fw'
	'bad-page_b:badpage-fw_b'
)

# check whether the partition exist in index file
check_partition()
{
	local work_dir="$1"
	local name="$2"
	local first_lba="$3"
	local last_lba="$4"
	local interm_dir=
	local lba_size=512
	local base_version=
	base_version="$(cat "${work_dir}/base_version")"

	ota_log "Checking partition ${name} in the ota index file"
	if [ ! -f "${work_dir}/base_version" ]; then
		ota_log "The file ${work_dir}/base_version is not found"
		return 1
	fi

	# For layout-change case, use the index file in the first intermediate layout dir.
	# For no-layout-change case, ToT is the only one (and also the first) intermediate
	# layout dir.
	interm_dir=$(get_first_interm_dir "${work_dir}" "${INT_DEV}")
	local index_file="${interm_dir}/flash.idx"
	local found_part=
	found_part="$(grep -m 1 -E "${name},|${name}_rsv," "${index_file}")"
	if [ "${found_part}" == "" ]; then
		# Try searching with the other name if the name matches any
		# of these names defined in the entry of _PART_NAME_COMPATBILE_TABLE table.
		local table_entry=
		local name_1=
		local name_2=
		local name_comp=
		for table_entry in "${_PART_NAME_COMPATBILE_TABLE[@]}"
		do
			name_1="$(echo "${table_entry}" | cut -d: -f 1)"
			name_2="$(echo "${table_entry}" | cut -d: -f 2)"
			if [ "${name}" == "${name_1}" ]; then
				name_comp="${name_2}"
				break
			elif [ "${name}" == "${name_2}" ]; then
				name_comp="${name_1}"
				break
			fi
		done
		found_part="$(grep -m 1 -E "${name_comp},|${name_comp}_rsv," "${index_file}")"
		if [ "${found_part}" == "" ]; then
			ota_log "Partition ${name} and ${name_comp} are not found in the ${index_file}."
			return 1
		fi
		name="${name_comp}"
	fi

	local part_name=
	local start_offset=
	local part_size=
	local end_offset=
	part_name="$(echo "${found_part}" | cut -d, -f 2 | sed 's/^ //g' - | cut -d: -f 3)"
	start_offset=$(echo "${found_part}" | cut -d, -f 3 | sed 's/^ //g' -)
	part_size=$(echo "${found_part}" | cut -d, -f 4 | sed 's/^ //g' -)
	end_offset=$((start_offset + part_size))

	# check whether end_offset is sector aligned
	local lba_aligned=$((end_offset % lba_size))
	if [ ${lba_aligned} -ne 0 ]; then
		end_offset=$(((end_offset / lba_size + 1) * lba_size))
	fi

	first_lba=$((first_lba * lba_size))
	last_lba=$(((last_lba + 1) * lba_size))
	if [ "${start_offset}" != "${first_lba}" ]; then
		ota_log "The start offset for ${part_name} partition does not match: ${first_lba}(gpt) != ${start_offset}(flash.idx)"
		return 1
	elif [ "${end_offset}" != "${last_lba}" ]; then
		ota_log "The end offset for ${part_name} partition does not match: ${last_lba}(gpt) != ${end_offset}(flash.idx)"
		return 1
	else
		ota_log "The start and end offset for ${part_name} partition matches"
	fi
	return 0
}

# The boot device only has secondary GPT, but no primary GPT.
check_boot_device()
{
	local work_dir="$1"
	local boot_device="$2"
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
	local base_version=
	local target_board=
	base_version="$(cat "${work_dir}/base_version")"
	target_board="$(cat "${work_dir}/board_name")"

	ota_log "Checking partitions on the boot device through secondary GPT"
	if [ "${base_version}" == "" ]; then
		ota_log "The file ${work_dir}/base_version is not found"
		return 1
	fi
	if [ "${target_board}" == "" ]; then
		ota_log "The file ${work_dir}/board_name is not found"
		return 1
	fi

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
	while [ ${count} -le ${_MAX_GPT_ENTRIES_NUM} ]
	do
		dd if=${sec_gpt_entries_bin} of=${entry_bin} skip=${entry_offset} bs=1 count=${sec_gpt_entry_size} >/dev/null 2>&1
		entry_sha1sum="$(sha1sum ${entry_bin} | cut -d\  -f 1)"

		# skip empty gpt entry
		if [ "${entry_sha1sum}" == "${full_zero_sha1sum}" ]; then
			count=$((count + 1))
			continue
		fi
		dd if=${entry_bin} of=${part_name_bin} skip=${part_name_offset} bs=1 count=${part_name_size} >/dev/null 2>&1
		part_name="$(strings -n 1 -e l ${part_name_bin})"
#		ota_log "Entry $count: ${part_name}"

		if [ "${target_board}" == "jetson-agx-xavier-industrial" ] && [ "${base_version}" == "R32-7" ]; then
			# For JAXi, we need to skip checking BCT/mb1/MB1_BCT_b as the size of BCT partitioin has been increased
			# and is overlapping mb1 partition during the OTA process. Also the original location of MB1_BCT_b is
			# temporally replaced by BCT_tmp.
			if [ "${part_name}" == "BCT" ] || [ "${part_name}" == "mb1" ] || [ "${part_name}" == "MB1_BCT_b" ]; then
				entry_offset=$((entry_offset + sec_gpt_entry_size))
				count=$((count + 1))
				continue
			fi

		fi

		if [ "${part_name}" == "VER_b" ] ||  [ "${part_name}" == "VER" ]; then
			# for jetson-agx-xavier-devkit, skip checking VER_b/VER as their offsets
			# are changed in R35
			if [ "${target_board}" == "jetson-agx-xavier-devkit" ]; then
				entry_offset=$((entry_offset + sec_gpt_entry_size))
				count=$((count + 1))
				continue
			fi
		fi

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

		# check whether this partition exists in the flash index file and
		# the start/end offset of this partition matches the one in flash
		# index file
		if ! check_partition "${work_dir}" "${part_name}" "${first_lba}" "${last_lba}"; then
			ota_log "Partition ${part_name} is missing or does not match in the ota index file, please check it"
			return 1
		fi

		entry_offset=$((entry_offset + sec_gpt_entry_size))
		count=$((count + 1))
	done
}

check_sdmmc_user_device()
{
	local work_dir="$1"
	local user_device="/dev/mmcblk0"
	local gpt_header_bin="/tmp/gpt_header.bin"
	local gpt_entries_bin="/tmp/gpt_entries.bin"
	local gpt_header_size=512
	local gpt_header_offset=512
	local gpt_entries_offset=$((gpt_header_offset + gpt_header_size))
	local gpt_entries_size=$((128 * 128))
	local gpt_entry_size=128
	local full_zero_sha1sum="0ae4f711ef5d6e9d26c611fd2c8c8ac45ecbf9e7"
	local base_version=
	base_version="$(cat "${work_dir}/base_version")"

	ota_log "Checking partitions on the user device through primary GPT"

	if [ "${base_version}" == "" ]; then
		ota_log "The file ${work_dir}/base_version is not found"
		return 1
	fi

	dd if=${user_device} of=${gpt_header_bin} skip=${gpt_header_offset} bs=1 count=${gpt_header_size} >/dev/null 2>&1
	dd if=${user_device} of=${gpt_entries_bin} skip=${gpt_entries_offset} bs=1 count=${gpt_entries_size} >/dev/null 2>&1

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
	while [ ${count} -le ${_MAX_GPT_ENTRIES_NUM} ]
	do
		dd if=${gpt_entries_bin} of=${entry_bin} skip=${entry_offset} bs=1 count=${gpt_entry_size} >/dev/null 2>&1
		entry_sha1sum="$(sha1sum ${entry_bin} | cut -d\  -f 1)"

		# skip empty gpt entry
		if [ "${entry_sha1sum}" == "${full_zero_sha1sum}" ]; then
			count=$((count + 1))
			continue
		fi
		dd if=${entry_bin} of=${part_name_bin} skip=${part_name_offset} bs=1 count=${part_name_size} >/dev/null 2>&1
		part_name="$(strings -n 1 -e l ${part_name_bin})"
#		ota_log "Entry $count: ${part_name}"

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

		# check whether this partition exists in the flash index file and
		# the start/end offset of this partition matches the one in flash
		# index file
		if [ "${part_name}" != "UDA" ] && [ "${part_name}" != "RECROOTFS" ]; then
			if ! check_partition "${work_dir}" "${part_name}" "${first_lba}" "${last_lba}"; then
				ota_log "Partition ${part_name} is missing or does not match in the ota index file, please check it"
				return 1
			fi
		fi

		entry_offset=$((entry_offset + gpt_entry_size))
		count=$((count + 1))
	done
}

ota_check_partitions()
{
	local work_dir="$1"
	if [ ! -d "${work_dir}" ]; then
		ota_log "OTA work dir ${work_dir} is not found"
		return 1
	fi

	if [ ! -f "${work_dir}/board_name" ]; then
		ota_log "The file ${work_dir}/board_name is not found"
		return 1
	fi

	local target_board=
	target_board="$(cat "${work_dir}/board_name")"
	case ${target_board} in
		jetson-agx-xavier-devkit)
			boot_device="${_MMC_BOOT_DEVICE}"
			;;
		jetson-xavier-nx-devkit-emmc|jetson-agx-xavier-industrial)
			boot_device="${_QSPI_BOOT_DEVICE}"
			;;
		*)
			ota_log "Invalid target board ${target_board}"
			return 1
			;;
	esac;

	# check partitions on the boot device
	if ! check_boot_device "${work_dir}" "${boot_device}"; then
		ota_log "Error when checking boot device"
		return 1
	fi

	# check partitions on the user device
	if ! check_sdmmc_user_device "${work_dir}"; then
		ota_log "Error when checking user device"
		return 1
	fi
}

