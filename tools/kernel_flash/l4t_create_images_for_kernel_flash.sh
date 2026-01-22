#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2020-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

# Usage: ./l4t_create_images_for_kernel_flash.sh <board-name> \
# <rootdev>
# This script creates the flash images and copy it to the correct locations. You
# might want to disable hung_task_panic as flashing to external storage could
# take a long time by using 'echo 0 > /proc/sys/kernel/hung_task_panic'
set -eo pipefail

function cleanup()
{
	if [ -n "${tmp_dir}" ] && findmnt -M "${tmp_dir}" > /dev/null; then
		umount "${tmp_dir}"
	fi

	if [ -d "${tmp_dir}" ]; then
		rm -rf "${tmp_dir}"
	fi

}

trap cleanup EXIT

function usage()
{
	echo -e "
Usage: $0 [options] <board-name> <rootdev>
Where,
    -u <PKC key file>            PKC key used for odm fused board.
    -v <SBK key file>            SBK key used for encryptions
    -p <option>                  Pass options to flash.sh when generating the image for internal storage
    <board-name>                 Indicate which board to use.
    <rootdev>                    Indicate what root device to use
    --external-device <dev>      Generate and/or flash images for the indicated external storage
                                 device. If this is used, -c option must be specified.
    --external-only              Skip generating internal storage images
    --usb-instance               Specify the usb port where the flashing cable is plugged (i.e 1-3)
    -c <config file>             The partition layout for the external storage device.
    -S <size>                    External APP partition size in bytes. KiB, MiB, GiB short hands are allowed,
                                 for example, 1GiB means 1024 * 1024 * 1024 bytes. (optional)
    -t                           Skip to generate flash package tarball. This option can be used
                                 for host that is also used for NFS host.
    --user_key <key_file>        User provided key file (16-byte) to encrypt user images, like kernel, kernel-dtb and initrd.
                                 If user_key is specified, SBK key (-v) has to be specified.
                                 For now, user_key file must contain all 0's.
    --pv-crt                     User provided key to sign cpu_bootloader
    -i <enc rfs key file>        Key for disk encryption support.
    -k <target_partname>         Generate only <target_partname> partition to put into the flash package
    --image <target_partfile>    Use <target_partfile> to generate the file for the <target_partname> partition to
                                 put into the flash package
    --with-systemimg             Generate system image even when flashing individual partition
    --uefi-keys <keys_conf>      Specify UEFI keys configuration file.
    --uefi-enc <uefi_enc_key>    Key file (0x19: 16-byte; 0x23: 32-byte) to encrypt UEFI payloads
    --pv-enc                     User provided key to encrypt cpu_bootloader
    --qspi-only                  Only generate the qspi images in the final packages
    --mass-storage-only          Only generate the mass storage images in the final packages
    --read-ramcode               Generate read_ramcode script. When starting an RCM boot, this script is triggered to
                                 read in the board specific RAMCODE, then a corresponding mem-bct is selected and used
                                 for the RCM boot.
    --hsm                        Use HSM for image signing/encryption
    --boot-chain-flash <c>       Flash only a specific boot chain (ex. \"A\", \"B\", \"all\").
                                 Defaults to \"all\". Not suitable for production.
    --boot-chain-select <c>      Specify booting chain (ex. \"A\" or \"B\") after the board is flashed.
                                 Defaults to \"A\".


With --external-device options specified, the supported values for <dev> are
    nvme0n1
    nvme1n1
    sda


	"; echo;
	exit 201
}


function get_value_from_PT_table()
{
	# Usage:
	#	get_value_from_PT_table {__pt_name} \
	#	{__pt_node} \
	#	{__pt_file} \
	local __pt_name="${1}";
	local __pt_node="${2}";
	local __pt_file="${3}";
	local __node_val="";


	# Get node value
	__node_val="$(xmllint --xpath "/partition_layout/device/partition[@name='${__pt_name}']/${__pt_node}/text()" "${__pt_file}")";
	__node_val=$(echo "${__node_val}" | xargs echo);

	echo "${__node_val}";
}

function is_valid_root_for_external()
{
	[[ "${1}" =~ ^internal|external|nvme[0-9]+n[0-9]+(p[0-9]+)|sd[a-z]+[0-9]+|eth0|mmcblk[1-9][0-9]*p[0-9]+$ ]]
}

# This function generate a folder that contains everything neccessary to flash
function generate_flash_images()
{
	echo "Create folder to store images to flash"
	if [[ -z "${append}" && -z "${target_partname}" ]]; then
		rm -rf "${NFS_IMAGES_DIR:?}"/*
		mkdir -p "${NFS_IMAGES_DIR}/${INTERNAL}"
		mkdir -p "${NFS_IMAGES_DIR}/${EXTERNAL}"
		chmod 755 "${NFS_IMAGES_DIR}"
		chmod 755 "${NFS_IMAGES_DIR}/${INTERNAL}"
		chmod 755 "${NFS_IMAGES_DIR}/${EXTERNAL}"
	fi


	# generate all the images needed for flashing
	if [ "${external_only}" = "0" ]; then
		echo "Generate image for internal storage devices"
		if ! generate_signed_images "${OPTIONS}" 0 "${target_rootdev}"; then
			echo "Error: failed to generate images"
			exit 202
		fi

		# relocate the images we just create to the designated folder
		if ! package_images "0"; then
			echo "Error: failed to relocate images to ${NFS_IMAGES_DIR}"
			exit 203
		fi
	fi

	# If flashing to external device, generate external device image here
	if [ -n "${external_device}" ]; then
		echo "Generate image for external storage devices"
		local root;
		root=${external_device};
		if is_valid_root_for_external "${target_rootdev}"; then
			root=${target_rootdev};
		elif ! is_valid_root_for_external "${root}"; then
			echo "External device is ${root} with no partition specified."
			echo "Use \"internal\" as root device when generating images for external device"
			root="internal"
		fi

		# generate all the images needed for flashing external device
		if ! generate_signed_images "${EXTOPTIONS}" "1" "${root}"; then
			echo "Error: Failed to generate images for external device"
			exit 204
		fi

		# relocate the images we just create to the designated folder
		if ! package_images "1"; then
			echo "Error: failed to relocate images to ${NFS_IMAGES_DIR}"
			exit 205
		fi

	fi

	echo "Copy flash script to ${NFS_IMAGES_DIR}"
	cp -afv "${L4T_NFSFLASH_DIR}/${KERNEL_FLASH_SCRIPT}" "${NFS_IMAGES_DIR}"
	if [ -e "${L4T_NFSFLASH_DIR}/bin/aarch64/simg2img" ]; then
		cp "${L4T_NFSFLASH_DIR}/bin/aarch64/simg2img" "${NFS_IMAGES_DIR}"
	fi
	if [ -e "${L4T_NFSFLASH_DIR}/bin/nv_fuse_read.sh" ]; then
		cp "${L4T_NFSFLASH_DIR}/bin/nv_fuse_read.sh" "${NFS_IMAGES_DIR}"
	fi

	# The code below generates the sample flash from nfs systemd service that
	# automatically runs the flash script once the system boot up. Only for
	# testing
	if [ "${TEST}" = "1" ]; then
		copy_service_to_output
	fi

}

function copy_service_to_output()
{
	cp -afv "${L4T_NFSFLASH_DIR}/nv-l4t-flash-from-nfs.service" \
	"${NFS_IMAGES_DIR}"
	sed -i "s/\${board_name}/${target_board}/g" \
	"${NFS_IMAGES_DIR}/nv-l4t-flash-from-nfs.service"
}

function check_prereq()
{
	# Check xmllint
	if ! command -v xmllint &> /dev/null; then
		echo "ERROR xmllint not found! To install - please run: " \
			"\"sudo apt-get install libxml2-utils\""
		exit 206
	fi;
	# Check zstd
	if ! command -v zstd &> /dev/null; then
		echo "ERROR zstd not found! To install - please run: " \
			"\"sudo apt-get install zstd\""
		exit 206
	fi;
}

readonly SDMMC_BOOT_DEVICE="0"
readonly SPI_DEVICE="3"

function is_boot_component()
{
	if [ "${1}" = "${SPI_DEVICE}" ] \
		|| [ "${1}" = "${SDMMC_BOOT_DEVICE}" ]; then
		return 0;
	fi
	return 1;
}

function is_qspi()
{
	if [ "${1}" = "${SPI_DEVICE}" ]; then
		return 1;
	fi
	return 0;
}


# This function finds all the images mentioned in the flash index file and puts
# it in a folder. Pass 1 to package external images, 0 to package internal images
function package_images()
{

	local external="${1}"
	local dest_dir=${NFS_IMAGES_DIR}
	local ext=
	if [ "${external}" = "1" ]; then
		ext="_ext"
		dest_dir="${dest_dir}/${EXTERNAL}"
	else
		dest_dir="${dest_dir}/${INTERNAL}"
	fi

	if [ -n "${target_partname}" ] && [ -z "${UNIFIED_FLASH}" ]; then
		part_file=$(tail -1 "${tmp_log}" | cut -d " " -f 2)
		if [ ! -f "${BOOTLOADER_DIR}/${part_file}" ] ; then
			echo "Error: ${part_file} is not found"
			return 1
		fi
		if [ ! -f "${dest_dir}/flash.idx" ] ; then
			echo "Error: ${dest_dir}/flash.idx is not found"
			return 1
		fi
		cp -avf "${BOOTLOADER_DIR}/${part_file}" "${dest_dir}"
		line="$(grep ":${target_partname}," "${dest_dir}/flash.idx")"
		if [ "${target_partname}" = "APP" ] || [ "${target_partname}" = "APP_b" ] || [ "${target_partname}" = "APP_ENC" ] || [ "${target_partname}" = "APP_ENC_b" ]; then
			var="${target_partname}${ext}"
			echo -e "${var}=${part_file}" >> "${dest_dir}/flash.cfg"
			sha1sum "${dest_dir}/${part_file}" | cut -f 1 -d ' ' \
			> "${dest_dir}/${part_file}.sha1sum"
			return 0
		fi
		file_name=$(echo "${line}" | cut -d, -f 5 | sed 's/^ //g' -)
		file_size=$(echo "${line}" | cut -d, -f 6 | sed 's/^ //g' -)
		sha1_chksum=$(echo "${line}" | cut -d, -f 8 | sed 's/^ //g' -)

		escaped_target_partname=$(printf '%s\n' "${target_partname}" | sed -e 's/[\/&]/\\&/g')
		escaped_part_file=$(printf '%s\n' "${part_file}" | sed -e 's/[\/&]/\\&/g')
		escaped_file_name=$(printf '%s\n' "${file_name}" | sed -e 's/[\/&]/\\&/g')

		new_filesize=$(stat -c%s "${BOOTLOADER_DIR}/${part_file}")
		sha1_chksum_gen=$(sha1sum "${BOOTLOADER_DIR}/${part_file}" | cut -d\  -f 1)

		sed -i "/:${escaped_target_partname},/ { s/ ${escaped_file_name},/ ${escaped_part_file},/g }" "${dest_dir}/flash.idx"
		sed -i "/:${escaped_target_partname},/ { s/ ${file_size},/ ${new_filesize},/g }" "${dest_dir}/flash.idx"
		sed -i "/:${escaped_target_partname},/ { s/ ${sha1_chksum},/ ${sha1_chksum_gen},/g }" "${dest_dir}/flash.idx"
		if [ -z "${with_systemimg}" ]; then
			return 0
		fi
	fi


	if [ ! -f "${FLASH_INDEX_FILE}" ]; then
		echo "Error: ${FLASH_INDEX_FILE} is not found"
		return 1
	fi

	if [ ! -f "${FLASH_XML_FILE}" ]; then
		echo "Error: ${FLASH_XML_FILE} is not found"
		return 1
	fi

	cp -avf "${FLASH_INDEX_FILE}" "${dest_dir}/flash.idx"

	if [ -n "${qspi_only}" ]; then
		sed -i '/, *3:0:/!d' "${dest_dir}/flash.idx"
	fi

	if [ -n "${mass_storage_only}" ]; then
		sed -i '/, *3:0:/d' "${dest_dir}/flash.idx"
	fi

	readarray index_array < "${dest_dir}/flash.idx"
	echo "Flash index file is ${FLASH_INDEX_FILE}"

	lines_num=${#index_array[@]}
	echo "Number of lines is $lines_num"

	max_index=$((lines_num - 1))
	echo "max_index=${max_index}"

	for i in $(seq 0 ${max_index})
	do
		local item="${index_array[$i]}"
		local part_size
		local file_size
		local file_name
		local part_name

		file_name=$(echo "${item}" | cut -d, -f 5 | sed 's/^ //g' -)
		part_name=$(echo "${item}" | cut -d, -f 2 | sed 's/^ //g' - | cut -d: \
		-f 3)
		part_size=$(echo "${item}" | cut -d, -f 4 | sed 's/^ //g' -)
		if [ "${with_systemimg}" = "1" ]; then
			local device_type
			device_type=$(echo "${item}" | cut -d, -f 2 | sed 's/^ //g' - | cut -d: -f 1)
			if is_boot_component "${device_type}"; then
				continue
			fi
		fi

		if [ -n "${UNIFIED_FLASH}" ] && [ -n "${target_partname}" ]  && [ "${part_name}" != "${target_partname}" ]; then
			continue
		fi

		# Prepare images for APP, APP_b, APP_ENC, APP_ENC_b and UDA partitions.
		if [ "${part_name}" = "APP" ] || [ "${part_name}" = "APP_b" ] \
			|| [ "${part_name}" = "APP_ENC" ] || [ "${part_name}" = "APP_ENC_b" ] \
			|| [ "${part_name}" = "UDA" ]; then

			localsysbootfile=$(get_value_from_PT_table "${part_name}" "filename" \
				"${FLASH_XML_FILE}")

			# UDA image might not be specified as the image at UDA is optional.
			if [ "${part_name}" == "UDA" ] && [ "${localsysbootfile}" == "" ]; then
				echo "No image is found for UDA partition"
				continue
			fi

			if [ "${localsysbootfile}" == "" ]; then
				var="${part_name}${ext}"
				echo -e "${var}=${localsysbootfile}" >> "${dest_dir}/flash.cfg"
				echo "No image is found for ${var} partition"
				continue
			fi

			echo "Copying ${part_name} image into " \
			"${dest_dir}/${localsysbootfile}"
			APP_FILE="${localsysbootfile}"
			# For APP and APP_b, use sparse image if "sparse_mode" is set to 1. Otherwise, use tarball.
			# For APP_ENC, APP_ENC_b and UDA, always use sparse image.
			if [[ ("${part_name}" = "APP" || "${part_name}" = "APP_b") && "${sparse_mode}" = "1" ]] \
				|| [[ "${part_name}" = "APP_ENC" || "${part_name}" = "APP_ENC_b" || "${part_name}" = "UDA" ]]; then
				if [ -n "${UNIFIED_FLASH}" ]; then
					cp "${BOOTLOADER_DIR}/${localsysbootfile}" "${dest_dir}/"
					APP_FILE="${localsysbootfile}"
				else
					zstd -T0 "${BOOTLOADER_DIR}/${localsysbootfile}.raw" -o "${dest_dir}/${localsysbootfile}.zst"
					APP_FILE="${localsysbootfile}.zst"
				fi
			else

				if [ -n "${UNIFIED_FLASH}" ]; then
					e2fsck -f -y "${BOOTLOADER_DIR}/${localsysbootfile}.raw"
					resize2fs -M "${BOOTLOADER_DIR}/${localsysbootfile}.raw"
					cp "${BOOTLOADER_DIR}/${localsysbootfile}.raw" "${dest_dir}/${localsysbootfile}"
				else
					tmp_dir=$(mktemp -d -t ci-XXXXXXXXXX)
					# Try to convert system.img.raw to tar format
					mount -o loop "${BOOTLOADER_DIR}/${localsysbootfile}.raw" "${tmp_dir}"

					# create tar images of the system.img and its sha1sum
					tar -c -I 'zstd -T0' -pf "${dest_dir}/${localsysbootfile}" \
					"${COMMON_TAR_OPTIONS[@]}" -C "${tmp_dir}" .

					# Clean up
					umount "${tmp_dir}"
					rm -rf "${tmp_dir}"
				fi
			fi

			sha1sum "${dest_dir}/${APP_FILE}" | cut -f 1 -d ' ' \
			> "${dest_dir}/${APP_FILE}.sha1sum"

			var="${part_name}${ext}"
			echo -e "${var}=${APP_FILE}" >> "${dest_dir}/flash.cfg"

			continue
		fi

		if [ -z "${file_name}" ]; then
			echo "Warning: skip writing ${part_name} partition as no image is \
specified"
			continue
		fi

		# Try searching image in the "ENCRYPTED_SIGNED_DIR" directory and
		# then in "BOOTLOADER_DIR" directory
		local part_image_file="${ENCRYPTED_SIGNED_DIR}/${file_name}"
		if [ ! -f "${part_image_file}" ]; then
			part_image_file="${BOOTLOADER_DIR}/${file_name}"
			if [ ! -f "${part_image_file}" ]; then
				echo "Error: image for partition ${part_name} is not found at "\
				"${part_image_file}"
				return 1
			fi
		fi

		# Copy the image we found or generated into the designated folder
		echo "Copying ${part_image_file} "\
					"${dest_dir}/$(basename "${part_image_file}")"
		cp -avf "${part_image_file}" \
		"${dest_dir}/$(basename "${part_image_file}")"
		file_size=$(stat -c %s "${dest_dir}/$(basename "${part_image_file}")")
		if [ "${file_size}" -gt "${part_size}" ]; then
			echo "Error: file ${part_image_file} size is bigger than partition "\
				"${part_name}"
			return 1
		fi

	done

	# Generate the flash configuration to be included in the flash package
	{
		echo -e "external_device=${external_device}"
		echo -e "CHIPID=${CHIPID}"
	} >> "${dest_dir}/flash.cfg"

}

function get_images_dir()
{
	local CHIPID
	local sbk_keyfile
	CHIPID=$(LDK_DIR=${LINUX_BASE_DIR}; source "${LDK_DIR}/${target_board}.conf" > /dev/null;echo "${CHIPID}")
	# use by odmsign_get_folder
	sbk_keyfile="${SBK_KEY:-${pv_enc}}"
	if [ -f "${BOOTLOADER_DIR}/odmsign.func" ]; then
		echo "$(source "${BOOTLOADER_DIR}/odmsign.func"; odmsign_get_folder)"
	else
		echo "signed"
	fi

}

function process_params()
{
	if [ -n "${qspi_only}" ]; then
		external_device=""
	fi
	if [ -n "${external_device}" ]; then
		if [ -z "${config_file}" ]; then
			usage
		fi
		if [[ ! "${external_device}" =~ ^nvme[0-9]+n[0-9]+(p[0-9]+)?$ \
		&& ! "${external_device}" =~ ^sd[a-z]+[0-9]*
		&& ! "${external_device}" =~ ^mmcblk[0-9][0-9]*(p[0-9]+)?$
		&& ! "${external_device}" =~ ^block.* ]]; then
			echo "${external_device} is not a supported external storage device"
			exit 208
		fi
	fi
	# Check whether the specified partition exists in the external config
	# Choose only one partition in one partition layout to generate the
	# individual image. Priotize external partition layout
	if [ -n "${target_partname}" ]; then
		if xmlstarlet sel -t -v "//partition[@name='${target_partname}']" "${config_file}" > /dev/null; then
			external_only=1
		else
			external_device=""
		fi
	fi
}

# This function issues a command to flash.sh to generate all the neccessary
# images. Needs two arguments:
#       options: flash options to pass to flash.sh
#       external: 1 to generate signed images for external images, 0 for
# internal images
#       rootdev:  rootdev device to flash to
function generate_signed_images()
{
	local options="${1}"
	local external="${2}"
	local rootdev="${3}"
	local board_arg=

	local cmd_arg="--no-flash --sign "
	if [ "${external}" = "1" ]; then
		cmd_arg+="--external-device -c \"${config_file}\" "
		if [ -n "${external_size}" ]; then
			cmd_arg+="-S \"${external_size}\" "
		fi
		if [ "${rootdev}" != "eth0" ]; then
			board_arg="BOOTDEV=${rootdev} "
		fi
	fi
	board_arg+="ADDITIONAL_DTB_OVERLAY=\"${ADDITIONAL_DTB_OVERLAY_OPT}\" "
	if [ -n "${KEY_FILE}" ] && [ -f "${KEY_FILE}" ]; then
		cmd_arg+="-u \"${KEY_FILE}\" "
	fi

	if [ -n "${SBK_KEY}" ] && [ -f "${SBK_KEY}" ]; then
		cmd_arg+="-v \"${SBK_KEY}\" "
	fi

	if [ -n "${pv_crt}" ]; then
		cmd_arg+="--pv-crt \"${pv_crt}\" "
	fi

	if [ -n "${pv_enc}" ]; then
		cmd_arg+="--pv-enc \"${pv_enc}\" "
	fi

	if [ -n "${usb_instance}" ]; then
		cmd_arg+="--usb-instance \"${usb_instance}\" "
	fi

	if [ -n "${ENC_RFS_KEY}" ] && [ -f "${ENC_RFS_KEY}" ]; then
		cmd_arg+="-i \"${ENC_RFS_KEY}\" "
	fi

	if [ -n "${target_partname}" ]; then
		cmd_arg+="-k \"${target_partname}\" "
	fi

	if [ -n "${target_partfile}" ]; then
		cmd_arg+="--image \"${target_partfile}\" "
	fi

	if [ -n "${with_systemimg}" ]; then
		cmd_arg+="--with-systemimg "
	fi

	if [ -n "${UEFI_KEYS_CONF}" ] && [ -f "${UEFI_KEYS_CONF}" ]; then
		cmd_arg+="--uefi-keys \"${UEFI_KEYS_CONF}\" "
	fi

	if [ -n "${UEFI_ENC}" ] && [ -f "${UEFI_ENC}" ]; then
		cmd_arg+="--uefi-enc \"${UEFI_ENC}\" "
	fi

	if [ -n "${qspi_only}" ]; then
		cmd_arg+="--qspi-only "
	fi

	if [ "${gen_read_ramcode}" -eq 1 ]; then
		cmd_arg+="--read-ramcode "
	fi

	if [ "${hsm_enable}" -eq 1 ]; then
		cmd_arg+="--hsm "
	fi

	cmd_arg+="--boot-chain-flash ${boot_chain_flash} "
	cmd_arg+="--boot-chain-select ${boot_chain_select} "

	cmd_arg+="${options} ${target_board} ${rootdev}"

	cmd="${board_arg} ${LINUX_BASE_DIR}/flash.sh ${cmd_arg}"
	export BOARDID
	export FAB
	export BOARDSKU
	export BOARDREV
	export CHIP_SKU
	export RAMCODE_ID

	echo "Generate images to be flashed"
	echo -e "${cmd}\r\n"
	eval "${cmd}" | tee "${tmp_log}"

	# returning the return value of ${cmd}
	return "${PIPESTATUS[0]}"
}


L4T_NFSFLASH_DIR="$(cd "$(dirname "${0}")" && pwd)"
L4T_TOOLS_DIR="${L4T_NFSFLASH_DIR%/*}"
LINUX_BASE_DIR="${L4T_TOOLS_DIR%/*}"
BOOTLOADER_DIR="${LINUX_BASE_DIR}/bootloader"
NFS_IMAGES_DIR="${L4T_NFSFLASH_DIR}/images"
COMMON_TAR_OPTIONS=("--checkpoint=10000" "--warning=no-timestamp" \
"--numeric-owner" "--xattrs" "--xattrs-include=*" )
KERNEL_FLASH_SCRIPT=l4t_flash_from_kernel.sh
OPTIONS=""
nargs=$#;
target_rootdev=${!nargs};
nargs=$((nargs-1));
target_board=${!nargs};
external_device=""
config_file=""
external_size=""
append=""
target_partname=""
tmp_log=$(mktemp)
external_only=0
with_systemimg=
UEFI_KEYS_CONF=""
UEFI_ENC=""
qspi_only=""
mass_storage_only=""
gen_read_ramcode=0
hsm_enable=0
boot_chain_flash="ALL"
boot_chain_select="A"
source "${L4T_NFSFLASH_DIR}"/l4t_kernel_flash_vars.func

if [ "${USER}" != "root" ]; then
	echo "${0} requires root privilege";
	exit 207;
fi

if [ $# -lt 2 ]; then
	usage;
fi;

opstr+="k:u:p:v:c:-:S:i:"
while getopts "${opstr}" OPTION; do
	case $OPTION in
	c) config_file=${OPTARG}; ;;
	p) OPTIONS=${OPTARG}; ;;
	k) target_partname=${OPTARG}; ;;
	u) KEY_FILE=${OPTARG}; ;;
	v) SBK_KEY=${OPTARG}; ;;
	S) external_size=${OPTARG}; ;;
	i) ENC_RFS_KEY=${OPTARG}; ;;
	-) case ${OPTARG} in
	   append) append=1; ;;
	   external-only) external_only=1; ;;
	   external-device)
	    external_device="${!OPTIND}";
	    OPTIND=$((OPTIND + 1));
	   ;;
	   sparse) sparse_mode=1; ;;
	   usb-instance)
		usb_instance="${!OPTIND}";
		OPTIND=$((OPTIND + 1));
		;;
	   pv-crt)
		pv_crt="${!OPTIND}";
		OPTIND=$((OPTIND + 1));
		;;
	   pv-enc)
		pv_enc="${!OPTIND}";
		OPTIND=$((OPTIND + 1));
		;;
	   image)
		target_partfile="${!OPTIND}";
		OPTIND=$((OPTIND + 1));
	   ;;
	   with-systemimg)
		with_systemimg=1
		;;
	   uefi-keys)
			UEFI_KEYS_CONF="${!OPTIND}";
			OPTIND=$((OPTIND + 1));
			;;
	   uefi-enc)
			UEFI_ENC="${!OPTIND}";
			OPTIND=$((OPTIND + 1));
			;;
	   qspi-only)
		qspi_only=1
		;;
	   mass-storage-only)
		mass_storage_only=1
		;;
	   read-ramcode)
		gen_read_ramcode=1
		;;
	   hsm)
		hsm_enable=1
		;;
	   boot-chain-flash)
		boot_chain_flash="${!OPTIND}";
		# store user string in uppercase
		boot_chain_flash="${boot_chain_flash^^}"
		OPTIND=$((OPTIND + 1));
		;;
	   boot-chain-select)
		boot_chain_select="${!OPTIND}";
		# store user string in uppercase
		boot_chain_select="${boot_chain_select^^}"
		OPTIND=$((OPTIND + 1));
		;;
	  *) usage ;;
	   esac;;
	*)
	   usage
	   ;;
	esac;
done
if [ "${external_only}" = "1" ]; then
	EXTOPTIONS="${OPTIONS}"
fi
ENCRYPTED_SIGNED_DIR="${BOOTLOADER_DIR}/$(get_images_dir)"
FLASH_INDEX_FILE="${ENCRYPTED_SIGNED_DIR}/flash.idx"
INTERNAL="internal"
EXTERNAL="external"
FLASH_XML_FILE="${ENCRYPTED_SIGNED_DIR}/flash.xml.tmp"
CHIPID=$(LDK_DIR=${LINUX_BASE_DIR}; source "${LDK_DIR}/${target_board}.conf" > /dev/null;echo "${CHIPID}")


check_prereq

if [ ! -f "${LINUX_BASE_DIR}/flash.sh" ]; then
	echo "Error: ${LINUX_BASE_DIR}/flash.sh is not found"
	exit 210
fi

process_params

# Generate the flash package here
generate_flash_images

echo "Success"
