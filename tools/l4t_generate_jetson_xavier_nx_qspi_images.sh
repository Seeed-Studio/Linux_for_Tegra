#!/bin/bash

# Copyright (c) 2020-2022, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

# This script generates factory images for jetson-xavier-nx-qspi sku 0 & 1

set -e

L4T_TOOLS_DIR="$(cd $(dirname "$0") && pwd)"
LINUX_BASE_DIR="${L4T_TOOLS_DIR%/*}"
BOOTLOADER_DIR="${LINUX_BASE_DIR}/bootloader"
SIGNED_IMAGES_DIR="${BOOTLOADER_DIR}/signed"
FLASH_INDEX_FILE="${SIGNED_IMAGES_DIR}/flash.idx"
BL_ENCRYPTED_BY_USERKEY=( 'xusb-fw' 'xusb-fw_b' )
SPI_FLASH_SIZE=33554432
K_BYTES=1024
SPI_IMAGE_NAME=""
BOARD_NAME=""

function usage()
{
	echo -e "
Usage: [env={value},...] $0 [-u <PKC key file>] [-v <SBK key file>] [-k <User key file>] [-b <board>]
Where,
	-u <PKC key file>  PKC key used for odm fused board
	-v <SBK key file>  Secure Boot Key (SBK) key used for ODM fused board
	-k <User key file> User provided key file (16-byte) to encrypt user images, including xusb-fw, kernel, kernel-dtb, recovery image, recovery dtb and initrd
	-b <board>	   Indicate to only generate QSPI image for this board. You can directly
			   use one of the following boards:
			   \"jetson-xavier-nx-devkit\", \"jetson-xavier-nx-devkit-emmc\",
			   If other <board> is provided, the correct env variants for this <board>
			   must be set.
			   All QSPI images for these supported 2 boards will be generated if not
			   enabling this option.

	The following env variant can be used to modify some parameters for the specified board:
	\"BOARDID\", \"FAB\", \"BOARDSKU\", \"BOARDREV\", \"FUSELEVEL\", \"CHIPREV\"

Example:
	1. Generate QSPI image signed by \"rsa_key.pem\" for Jetson Xavier NX (P3668-0000)
		$0 -u rsa_key.pem -b jetson-xavier-nx-devkit
	2. Generate QSPI image for Jetson Xavier NX (P3688-0001)
		FAB=200 $0 -b jetson-xavier-nx-devkit-emmc
	3. Generate QSPI image for XXX board
		BOARDID=XXXX FAB=XXX BOARDSKU=XXXX BOARDREV=X.X FUSELEVEL=fuselevel_production CHIPREV=2 $0 -b XXX
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

	local sha1_chksum_gen=$(sha1sum "${file_image}" | cut -d\  -f 1)
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

	if [ ${size} -eq 0 ];then
		echo "Error: the size of bytes to be read is ${size}"
		return 1
	fi

	local inoffset_align_K=$((${inoffset} % ${K_BYTES}))
	local outoffset_align_K=$((${outoffset} % ${K_BYTES}))
	if [ ${inoffset_align_K} -ne 0 ] || [ ${outoffset_align_K} -ne 0 ];then
		echo "Offset is not aligned to K Bytes, no optimization is applied"
		echo "dd if=${infile} of=${outfile} bs=1 skip=${inoffset} seek=${outoffset} count=${size}"
		dd if="${infile}" of="${outfile}" bs=1 skip=${inoffset} seek=${outoffset} count=${size}
		return 0
	fi

	local block=$((${size} / ${K_BYTES}))
	local remainder=$((${size} % ${K_BYTES}))
	local inoffset_blk=$((${inoffset} / ${K_BYTES}))
	local outoffset_blk=$((${outoffset} / ${K_BYTES}))

	echo "${size} bytes from ${infile} to ${outfile}: 1KB block=${block} remainder=${remainder}"

	if [ ${block} -gt 0 ];then
		echo "dd if=${infile} of=${outfile} bs=1K skip=${inoffset_blk} seek=${outoffset_blk} count=${block}"
		dd if="${infile}" of="${outfile}" bs=1K skip=${inoffset_blk} seek=${outoffset_blk} count=${block} conv=notrunc
		sync
	fi
	if [ ${remainder} -gt 0 ];then
		local block_size=$((${block} * ${K_BYTES}))
		local outoffset_rem=$((${outoffset} + ${block_size}))
		local inoffset_rem=$((${inoffset} + ${block_size}))
		echo "dd if=${infile} of=${outfile} bs=1 skip=${inoffset_rem} seek=${outoffset_rem} count=${remainder}"
		dd if="${infile}" of="${outfile}" bs=1 skip=${inoffset_rem} seek=${outoffset_rem} count=${remainder} conv=notrunc
		sync
	fi
	return 0
}

function update_chksum()
{
	local part_name=
	local file_name=
	local sha1_chksum=
	local sha1_chksum_gen=
	local line=
	local line_num=

	for part_name in "${BL_ENCRYPTED_BY_USERKEY[@]}"
	do
		line="$(grep -n -m 1 "${part_name}," "${FLASH_INDEX_FILE}")"
		if [ "${line}" == "" ]; then
			echo "No ${part_name} partition exists"
			continue
		fi
		line_num="$(echo "${line}" | cut -d: -f 1)"
		file_name="$(echo "${line}" | cut -d, -f 5 | sed 's/^ //')"
		if [ ! -f "${SIGNED_IMAGES_DIR}/${file_name}" ]; then
			echo "Error: the file ${SIGNED_IMAGES_DIR}/${file_name} is not found."
			exit 1
		fi
		sha1_chksum="$(echo "${line}" | cut -d, -f 8 | sed 's/^ //')"

		# Re-generate sha1 checksum and replace the existing one in flash.idx with it
		sha1_chksum_gen="$(sha1sum "${SIGNED_IMAGES_DIR}/${file_name}" | cut -d\  -f 1)"
		sed -i "${line_num}s/${sha1_chksum}/${sha1_chksum_gen}/" "${FLASH_INDEX_FILE}"
		echo "Updated the sha1sum of ${file_name} for parition ${part_name}"
	done
}

function generate_binaries()
{
	local spec="${1}"
	local signed_dir=""

	# remove existing signed images
	if [ -d "${SIGNED_IMAGES_DIR}" ];then
		rm -Rf "${SIGNED_IMAGES_DIR}/*"
	fi

	SPI_IMAGE_NAME=""
	eval "${spec}"

	if [ "${board}" != "jetson-xavier-nx-devkit" ] \
		&& [ "${board}" != "jetson-xavier-nx-devkit-emmc" ]; then
		echo "Unlisted board ${board}"
	fi

	# Skip generating recovery image and esp image as recovery
	# and esp partitions are not located on QSPI device.
	board_arg="NO_RECOVERY_IMG=1 NO_ESP_IMG=1 "
	if [ "${FUSELEVEL}" = "" ];then
		if [ "${fuselevel_s}" = "0" ]; then
			fuselevel="fuselevel_nofuse";
		else
			fuselevel="fuselevel_production";
		fi
		board_arg+="FUSELEVEL=${fuselevel} "
	else
		board_arg+="FUSELEVEL=${FUSELEVEL} "
	fi

	if [ "${BOARDID}" = "" ];then
		board_arg+="BOARDID=${boardid} "
	else
		board_arg+="BOARDID=${BOARDID} "
	fi

	if [ "${FAB}" = "" ];then
		board_arg+="FAB=${fab} "
	else
		board_arg+="FAB=${FAB} "
	fi

	if [ "${BOARDSKU}" = "" ];then
		board_arg+="BOARDSKU=${boardsku} "
	else
		board_arg+="BOARDSKU=${BOARDSKU} "
	fi

	if [ "${BOARDREV}" = "" ];then
		board_arg+="BOARDREV=${boardrev} "
	else
		board_arg+="BOARDREV=${BOARDREV} "
	fi

	if [ "${CHIPREV}" = "" ];then
		board_arg+="CHIPREV=${chiprev} "
	else
		board_arg+="CHIPREV=${CHIPREV} "
	fi

	if [ -n "${BOARD_FAMILY}" ];then
		board_arg+="BOARD_FAMILY=${BOARD_FAMILY} "
	fi

	echo "Generating binaries for board spec: ${board_arg}"

	# Skip generating system image as APP partition is not
	# on the QSPI device.
	# Remove the root privilege check as it is not neccessary..
	cmd_arg="--no-root-check --no-flash --no-systemimg --sign "
	if [ "${PKC_KEY_FILE}" != "" ] && [ -f "${PKC_KEY_FILE}" ];then
		cmd_arg+="-u \"${PKC_KEY_FILE}\" "
	fi
	if [ "${SBK_KEY_FILE}" != "" ] && [ -f "${SBK_KEY_FILE}" ];then
		cmd_arg+="-v \"${SBK_KEY_FILE}\" "
		# For t19x devices, if encryption is enabled, encrypted images
		# are generated under the "bootloader/encrypted_signed_t19x".
		SIGNED_IMAGES_DIR="${BOOTLOADER_DIR}/encrypted_signed_t19x"
		FLASH_INDEX_FILE="${SIGNED_IMAGES_DIR}/flash.idx"
	fi
	if [ "${USER_KEY_FILE}" != "" ] && [ -f "${USER_KEY_FILE}" ];then
		cmd_arg+="--user_key \"${USER_KEY_FILE}\" "
	fi
	cmd_arg+="${board} ${rootdev}"
	cmd="${board_arg} ${LINUX_BASE_DIR}/flash.sh ${cmd_arg}"

	echo -e "${cmd}\r\n"
	if ! eval "${cmd}"; then
		echo "FAILURE: ${cmd}"
		exit 1
	fi

	# For some images, such as xusb-fw image, they are encrypted by
	# userkey after the flash.idx is generated, so need to re-generate
	# the chksum for these images and replace the ones in flash.idx.
	if [ "${SBK_KEY_FILE}" != "" ] && [ -f "${SBK_KEY_FILE}" ]; then
		update_chksum
	fi

	SPI_IMAGE_NAME="${board}.spi.img"
}

function fill_partition_image()
{
	local item="${1}"
	local spi_image="${2}"
	local part_name=$(echo "${item}" | cut -d, -f 2 | sed 's/^ //g' - | cut -d: -f 3)
	local file_name=$(echo "${item}" | cut -d, -f 5 | sed 's/^ //g' -)
	local start_offset=$(echo "${item}" | cut -d, -f 3 | sed 's/^ //g' -)
	local file_size=$(echo "${item}" | cut -d, -f 6 | sed 's/^ //g' -)
	local sha1_chksum=$(echo "${item}" | cut -d, -f 8 | sed 's/^ //g' -)

	if [ "${file_name}" = "" ];then
		echo "Warning: skip writing ${part_name} partition as no image is specified"
		return 0
	fi

	echo "Writing ${file_name} (parittion: ${part_name}) into ${spi_image}"

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

	sha1_verify "${part_image_file}" "${sha1_chksum}"

	echo "Writing ${part_image_file} (${file_size} bytes) into ${spi_image}:${start_offset}"
	rw_part_opt "${part_image_file}" "${spi_image}" 0 "${start_offset}" "${file_size}"

	# Write BCT redundancy
	# BCT image should be written in multiple places: (Block 0, Slot 0), (Block 0, Slot 1) and (Block 1, Slot 0)
	# In this case, block size is 32KB and the slot size is 4KB, so the BCT image should be written at the place
	# where offset is 4096 and 32768
	if [ "${part_name}" = "BCT" ];then
		# Block 0, Slot 1
		start_offset=4096
		echo "Writing ${part_image_file} (${file_size} bytes) into ${spi_image}:${start_offset}"
		rw_part_opt "${part_image_file}" "${spi_image}" 0 "${start_offset}" "${file_size}"

		# Block 1, Slot 0
		start_offset=32768
		echo "Writing ${part_image_file} (${file_size} bytes) into ${spi_image}:${start_offset}"
		rw_part_opt "${part_image_file}" "${spi_image}" 0 "${start_offset}" "${file_size}"
	fi
}

function generate_spi_image()
{
	local image_name="${1}"
	local image_file="${BOOTLOADER_DIR}/${image_name}"

	if [ ! -f "${FLASH_INDEX_FILE}" ];then
		echo "Error: ${FLASH_INDEX_FILE} is not found"
		return 1
	fi

	# create a zero spi image
	dd if=/dev/zero of="${image_file}" bs=1M count=32

	readarray index_array < "${FLASH_INDEX_FILE}"
	echo "Flash index file is ${FLASH_INDEX_FILE}"

	lines_num=${#index_array[@]}
	echo "Number of lines is $lines_num"

	max_index=$((lines_num - 1))
	echo "max_index=${max_index}"

	for i in $(seq 0 ${max_index})
	do
		local item="${index_array[$i]}"

		# break if device type is SDMMC(1) as only generating image for SPI flash(3)
		local device_type=$(echo "${item}" | cut -d, -f 2 | sed 's/^ //g' - | cut -d: -f 1)
		if [ "${device_type}" != 3 ];then
			echo "Reach the end of the SPI device"
			break
		fi

		# fill the partition image into the SPI image
		fill_partition_image "${item}" "${image_file}"
	done

	echo "Generated image for SPI at ${image_file}"
	return 0
}

jetson_xavier_nx_qspi=(
	# jetson-xavier-nx-devkit board
	'boardid=3668;fab=100;boardsku=0000;boardrev=1;fuselevel_s=1;chiprev=2;board=jetson-xavier-nx-devkit;rootdev=mmcblk0p1'

	# jetson-xavier-nx-devkit-emmc board
	'boardid=3668;fab=100;boardsku=0001;boardrev=1;fuselevel_s=1;chiprev=2;board=jetson-xavier-nx-devkit-emmc;rootdev=mmcblk0p1'
)

opstr+="u:v:k:b:"
while getopts "${opstr}" OPTION; do
	case $OPTION in
	u) PKC_KEY_FILE=${OPTARG}; ;;
	v) SBK_KEY_FILE=${OPTARG}; ;;
	k) USER_KEY_FILE=${OPTARG}; ;;
	b) BOARD_NAME=${OPTARG}; ;;
	*)
	   usage
	   ;;
	esac;
done

if [ ! -f "${LINUX_BASE_DIR}/flash.sh" ];then
	echo "Error: ${LINUX_BASE_DIR}/flash.sh is not found"
	exit 1
fi

# Generate spi image for one or all listed board(s)
generated=0
pushd "${LINUX_BASE_DIR}" > /dev/null 2>&1
for spec in "${jetson_xavier_nx_qspi[@]}"; do
	eval "${spec}"
	if [ "${BOARD_NAME}" != "" ] && [ "${BOARD_NAME}" != "${board}" ];then
		continue
	fi
	generate_binaries "${spec}"
	if [ $? -ne 0 ];then
		echo "Error: failed to generate binaries for board ${board}"
		exit 1
	fi

	if [ "${SPI_IMAGE_NAME}" = "" ];then
		echo "Error: SPI image name is NULL"
		exit 1
	fi

	echo "Generating SPI image \"${SPI_IMAGE_NAME}\""
	generate_spi_image "${SPI_IMAGE_NAME}"
	if [ $? -ne 0 ];then
		echo "Error: failed to generate SPI image \"${SPI_IMAGE_NAME}\""
		exit 1
	fi
	generated=1
done

# Generated spi image for unlisted board
if [ "${BOARD_NAME}" != "" ] && [ "${generated}" = "0" ];then
	echo "Check env variants for unlisted board ${BOARD_NAME}"
	if [ "${BOARDID}" = "" ];then
		echo "Error: invalid BOARDID=${BOARDID}"
		usage
	fi

	if [ "${FAB}" = "" ];then
		echo "Error: invalid FAB=${FAB}"
		usage
	fi

	if [ "${BOARDSKU}" = "" ];then
		echo "Error: invalid BOARDSKU=${BOARDSKU}"
		usage
	fi

	if [ "${BOARDREV}" = "" ];then
		echo "Error: invalid BOARDREV=${BOARDREV}"
		usage
	fi

	if [ "${CHIPREV}" = "" ];then
		echo "Error: invalid CHIPREV=${CHIPREV}"
		usage
	fi

	if [ "${FUSELEVEL}" != "fuselevel_nofuse" ] && \
		[ "${FUSELEVEL}" != "fuselevel_production" ] || \
		[ "${FUSELEVEL}" = "" ];then
		echo "Error: invalid FUSELEVEL=${FUSELEVEL}"
		usage
	fi

	spec="boardid=${BOARDID};fab=${FAB};boardsku=${BOARDSKU};boardrev=${BOARDREV};fuselevel_s=${FUSELEVEL};chiprev=${CHIPREV};board=${BOARD_NAME};rootdev=mmcblk0p1"
	generate_binaries "${spec}"
	if [ $? -ne 0 ];then
		echo "Error: failed to generate binaries for board ${BOARD_NAME}"
		exit 1
	fi

	if [ "${SPI_IMAGE_NAME}" = "" ];then
		echo "Error: SPI image name is NULL"
		exit 1
	fi

	echo "Generating SPI image \"${SPI_IMAGE_NAME}\""
	generate_spi_image "${SPI_IMAGE_NAME}"
	if [ $? -ne 0 ];then
		echo "Error: failed to generate SPI image \"${SPI_IMAGE_NAME}\""
		exit 1
	fi
fi

popd > /dev/null 2>&1
