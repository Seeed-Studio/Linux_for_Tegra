#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2023-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

# This script generates QSPI image for T234 devices

set -e

L4T_TOOLS_DIR="$(cd "$(dirname "$0")" && pwd)"
LINUX_BASE_DIR="${L4T_TOOLS_DIR%/*}"
BOOTLOADER_DIR="${LINUX_BASE_DIR}/bootloader"
SIGNED_IMAGES_DIR="${BOOTLOADER_DIR}/signed"
FLASH_INDEX_FILE="${SIGNED_IMAGES_DIR}/flash.idx"
QSPI_FLASH_SIZE=64    #MiB
ERASABLE_BLOCK_SIZE=65536
K_BYTES=1024
QSPI_IMAGE_NAME=""
BOARD_NAME=""

function usage()
{
	echo -e "
Usage: [env={value},...] $0 [-u <PKC key file>] [-v <SBK key file>] [-p <options>] <board>
Where,
	<board>	           Indicate to generate QSPI image for this board.

Options:
	-u <PKC key file>  PKC key used for odm fused board
	-v <SBK key file>  Secure Boot Key (SBK) key used for ODM fused board
	-p <options>       Options directly passed to flash.sh

Notes:
	If the board is connected and put into recovery mode, the board spec
	is read from the board when running this script.
	If the board is not connected, you must set the following env variants
	that match it when running this script:
	\"BOARDID\", \"FAB\", \"BOARDSKU\", \"BOARDREV\", \"CHIPREV\", \"CHIP_SKU\".

Example:
	1. Generate QSPI image for the connected IGX Orin Devkit
		$0 igx-orin-devkit
	2. Generate QSPI image signed by \"rsa_key.pem\" and encrypted by \"sbk.key\" for the connected IGX Orin Devkit
		$0 -u rsa_key.pem -v sbk.key igx-orin-devkit
	3. Generate QSPI image for the disconnected IGX Orin Devkit
		BOARDID=3701 FAB=000 BOARDSKU=0008 CHIP_SKU=00:00:00:90 $0 igx-orin-devkit
	"; echo;
	exit 1
}

function sha1_verify()
{
	local file_image="${1}"
	local sha1_chksum="${2}"

	if [ -z "${sha1_chksum}" ];then
		echo "Error: passed-in sha1 checksum is NULL"
		return 1
	fi

	if [ ! -f "${file_image}" ];then
		echo "Error: $file_image is not found !!!"
		return 1
	fi

	local sha1_chksum_gen=
	sha1_chksum_gen="$(sha1sum "${file_image}" | cut -d\  -f 1)"
	if [ "${sha1_chksum_gen}" = "${sha1_chksum}" ];then
		echo "sha1 checksum matched for ${file_image}"
		return 0
	else
		echo "Error: sha1 checksum does not match (${sha1_chksum_gen} != ${sha1_chksum}) for ${file_image}"
		return 1
	fi
}

function rw_part_opt()
{
	local infile="${1}"
	local outfile="${2}"
	local inoffset="${3}"
	local outoffset="${4}"
	local size="${5}"

	if [ ! -e "${infile}" ];then
		echo "Error: input file ${infile} is not found"
		return 1
	fi

	if [ "${size}" -eq 0 ];then
		echo "Error: the size of bytes to be read is ${size}"
		return 1
	fi

	local inoffset_align_K=
	local outoffset_align_K=
	inoffset_align_K="$((inoffset % K_BYTES))"
	outoffset_align_K="$((outoffset % K_BYTES))"
	if [ "${inoffset_align_K}" -ne 0 ] || [ "${outoffset_align_K}" -ne 0 ];then
		echo "Offset is not aligned to K Bytes, no optimization is applied"
		echo "dd if=${infile} of=${outfile} bs=1 skip=${inoffset} seek=${outoffset} count=${size} conv=notrunc"
		dd if="${infile}" of="${outfile}" bs=1 skip="${inoffset}" seek="${outoffset}" count="${size}" conv=notrunc
		return 0
	fi

	local block=
	local remainder=
	local inoffset_blk=
	local outoffset_blk=
	block="$((size / K_BYTES))"
	remainder="$((size % K_BYTES))"
	inoffset_blk="$((inoffset / K_BYTES))"
	outoffset_blk="$((outoffset / K_BYTES))"

	echo "${size} bytes from ${infile} to ${outfile}: 1KB block=${block} remainder=${remainder}"

	if [ "${block}" -gt 0 ];then
		echo "dd if=${infile} of=${outfile} bs=1K skip=${inoffset_blk} seek=${outoffset_blk} count=${block} conv=notrunc"
		dd if="${infile}" of="${outfile}" bs=1K skip="${inoffset_blk}" seek="${outoffset_blk}" count="${block}" conv=notrunc
		sync
	fi
	if [ "${remainder}" -gt 0 ];then
		local block_size=
		local outoffset_rem=
		local inoffset_rem=
		block_size="$((block * K_BYTES))"
		outoffset_rem="$((outoffset + block_size))"
		inoffset_rem="$((inoffset + block_size))"
		echo "dd if=${infile} of=${outfile} bs=1 skip=${inoffset_rem} seek=${outoffset_rem} count=${remainder} conv=notrunc"
		dd if="${infile}" of="${outfile}" bs=1 skip="${inoffset_rem}" seek="${outoffset_rem}" count="${remainder}" conv=notrunc
		sync
	fi
	return 0
}

function generate_binaries()
{
	local board="${1}"
	local board_spec=""

	# remove existing signed images
	if [ -d "${SIGNED_IMAGES_DIR}" ];then
		rm -Rf "${SIGNED_IMAGES_DIR}"
	fi

	# Fill "board_spec" if environment settings exist
	if [ "${BOARDID}" != "" ];then
		board_spec+="BOARDID=${BOARDID} "
	fi

	if [ "${FAB}" != "" ];then
		board_spec+="FAB=${FAB} "
	fi

	if [ "${BOARDSKU}" != "" ];then
		board_spec+="BOARDSKU=${BOARDSKU} "
	fi

	if [ "${BOARDREV}" != "" ];then
		board_spec+="BOARDREV=${BOARDREV} "
	fi

	if [ "${CHIPREV}" != "" ];then
		board_spec+="CHIPREV=${CHIPREV} "
	fi

	if [ "${CHIP_SKU}" != "" ];then
		board_spec+="CHIP_SKU=${CHIP_SKU} "
	fi

	# Skip generating recovery image and esp image as recovery
	# and esp partitions are not located on QSPI device.
	env_arg="NO_RECOVERY_IMG=1 NO_ESP_IMG=1 ROOTFS_AB=${ROOTFS_AB} "

	# Skip generating system image as APP partition is not
	# on the QSPI device.
	# Remove the root privilege check as it is not neccessary..
	cmd_arg="--no-root-check --no-flash --no-systemimg --sign "
	if [ "${PKC_KEY_FILE}" != "" ] && [ -f "${PKC_KEY_FILE}" ];then
		cmd_arg+="-u \"${PKC_KEY_FILE}\" "
	fi
	if [ "${SBK_KEY_FILE}" != "" ] && [ -f "${SBK_KEY_FILE}" ];then
		cmd_arg+="-v \"${SBK_KEY_FILE}\" "
		SIGNED_IMAGES_DIR="${BOOTLOADER_DIR}/enc_signed"
		FLASH_INDEX_FILE="${SIGNED_IMAGES_DIR}/flash.idx"
	fi

	cmd_arg+="${board} internal"
	cmd="${env_arg} ${board_spec} ${LINUX_BASE_DIR}/flash.sh ${FLASH_OPTIONS} ${cmd_arg}"

	echo -e "${cmd}\r\n"
	if ! eval "${cmd}"; then
		echo "FAILURE: ${cmd}"
		exit 1
	fi

	QSPI_IMAGE_NAME="${board}.qspi.img"
}

function write_BCT()
{
	local part_image_file="${1}"
	local qspi_image="${2}"
	local start_offset="${3}"
	local file_size="${4}"
	local part_size="${5}"
	local end_offset=

	end_offset="$((start_offset + part_size))"
	while [ "${end_offset}" -gt "${start_offset}" ];
	do
		echo "Writing ${part_image_file} (${file_size} bytes) into ${qspi_image}:${start_offset}"
		rw_part_opt "${part_image_file}" "${qspi_image}" 0 "${start_offset}" "${file_size}"
		start_offset="$((start_offset + ERASABLE_BLOCK_SIZE))"
	done
	echo "Writing BCT done"
}

function fill_partition_image()
{
	local item="${1}"
	local qspi_image="${2}"
	local part_name=
	local file_name=
	local start_offset=
	local part_size=
	local file_size=
	local sha1_chksum=

	part_name=$(echo "${item}" | cut -d, -f 2 | sed 's/^ //g' - | cut -d: -f 3)
	file_name=$(echo "${item}" | cut -d, -f 5 | sed 's/^ //g' -)
	start_offset=$(echo "${item}" | cut -d, -f 3 | sed 's/^ //g' -)
	part_size=$(echo "${item}" | cut -d, -f 4 | sed 's/^ //g' -)
	file_size=$(echo "${item}" | cut -d, -f 6 | sed 's/^ //g' -)
	sha1_chksum=$(echo "${item}" | cut -d, -f 8 | sed 's/^ //g' -)

	if [ "${file_name}" = "" ];then
		echo "Warning: skip writing ${part_name} partition as no image is specified"
		return 0
	fi

	echo "Writing ${file_name} (parittion: ${part_name}) into ${qspi_image}"

	# Try searching image in the "SIGNED_IMAGES_DIR" directory and
	# then in "BOOTLOADER_DIR" directory
	local part_image_file="${SIGNED_IMAGES_DIR}/${file_name}"
	if [ ! -f "${part_image_file}" ];then
		part_image_file="${BOOTLOADER_DIR}/${file_name}"
		if [ ! -f "${part_image_file}" ];then
			echo "Error: image for partition ${part_name} is not found at ${part_image_file}"
			return 1
		fi
	fi

	# Validate the image
	sha1_verify "${part_image_file}" "${sha1_chksum}"

	# Write image
	if [ "${part_name}" = "BCT" ];then
		echo "Writing BCT"
		write_BCT "${part_image_file}" "${qspi_image}" "${start_offset}" "${file_size}" "${part_size}"
	else
		echo "Writing ${part_image_file} (${file_size} bytes) into ${qspi_image}:${start_offset}"
		rw_part_opt "${part_image_file}" "${qspi_image}" 0 "${start_offset}" "${file_size}"
	fi
}

function generate_qspi_image()
{
	local image_name="${1}"
	local image_file="${BOOTLOADER_DIR}/${image_name}"
	local layout_file="${BOOTLOADER_DIR}/${image_name}.layout"

	if [ ! -f "${FLASH_INDEX_FILE}" ];then
		echo "Error: ${FLASH_INDEX_FILE} is not found"
		return 1
	fi

	# create a 0xff qspi image
	if ! dd if=/dev/zero bs=1M count="${QSPI_FLASH_SIZE}" | tr '\000' '\377' > "${image_file}"; then
		echo "Error: fail to generate ${image_file} filled by \"0xff\""
		return 1
	fi

	readarray index_array < "${FLASH_INDEX_FILE}"
	echo "Flash index file is ${FLASH_INDEX_FILE}"

	lines_num=${#index_array[@]}
	echo "Number of lines is $lines_num"

	max_index=$((lines_num - 1))
	echo "max_index=${max_index}"

	local item=
	local device_type=
	for i in $(seq 0 ${max_index})
	do
		item="${index_array[$i]}"

		# break if device type is not QSPI flash(3)
		device_type=$(echo "${item}" | cut -d, -f 2 | sed 's/^ //g' - | cut -d: -f 1)
		if [ "${device_type}" != 3 ];then
			echo "Reach the end of the QSPI device"
			break
		fi

		# fill the partition image into the QSPI image
		fill_partition_image "${item}" "${image_file}"
	done
	cp -f "${FLASH_INDEX_FILE}" "${layout_file}"

	echo "Generated QSPI image at ${image_file} and the image layout at ${layout_file}"
	return 0
}

if [ $# -lt 1 ];then
	usage
fi

nargs=$#;
BOARD_NAME=${!nargs};

opstr+="u:v:p:"
while getopts "${opstr}" OPTION; do
	case $OPTION in
	u) PKC_KEY_FILE="${OPTARG}"; ;;
	v) SBK_KEY_FILE="${OPTARG}"; ;;
	p) FLASH_OPTIONS="${OPTARG}"; ;;
	*)
	   usage
	   ;;
	esac;
done

if [ ! -f "${LINUX_BASE_DIR}/flash.sh" ];then
	echo "Error: ${LINUX_BASE_DIR}/flash.sh is not found"
	exit 1
fi

# Generate qspi image for one or all listed board(s)
pushd "${LINUX_BASE_DIR}" > /dev/null 2>&1
echo "******** Generating images for partitions ********"
if ! generate_binaries "${BOARD_NAME}"; then
	echo "Error: failed to generate binaries for board ${board}"
	exit 1
fi

if [ "${QSPI_IMAGE_NAME}" = "" ];then
	echo "Error: QSPI image name is NULL"
	exit 1
fi

echo -e "\n******** Generating QSPI image \"${QSPI_IMAGE_NAME}\" ********"
if ! generate_qspi_image "${QSPI_IMAGE_NAME}"; then
	echo "Error: failed to generate QSPI image \"${QSPI_IMAGE_NAME}\""
	exit 1
fi

popd > /dev/null 2>&1
