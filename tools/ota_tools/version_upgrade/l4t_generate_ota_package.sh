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

# This script generates OTA package, which used to ota from
# R32.x/R35.x to R35-ToT.
set -e

LINUX_BASE_DIR="$(pwd)"
OTA_DIR="${LINUX_BASE_DIR}/tools/ota_tools/version_upgrade"
BOOTLOADER_DIR="${LINUX_BASE_DIR}/bootloader"
OTA_BASE_DIR_TMP=""
SYSTEM_IMAGE_RAW_FILE="${BOOTLOADER_DIR}/system.img.raw"
SYSTEM_IMAGE_B_RAW_FILE="${BOOTLOADER_DIR}/system.img_b.raw"
SYSTEM_IMAGE_TMP=""
OTA_PACKAGE_FILE="ota_package.tar"
OTA_PACKAGE_SHA1_FILE="ota_package.tar.sha1sum"
OTA_PAYLOAD_PACKAGE_ZIP_FILE="ota_payload_package.tar.gz"
R32_MB1_FILE="mb1_t194_prod_r32.bin"
L4T_LAUNCHER="${BOOTLOADER_DIR}/BOOTAA64.efi"

# For rootfs A/B overlay package
ROOTFS_OVERLAY_PACKAGE_A="rootfs_overlay_a.tar.gz"
ROOTFS_OVERLAY_PACKAGE_B="rootfs_overlay_b.tar.gz"
ROOTFS_OVERLAY_FILES=( "/boot/extlinux" )

# Do not use underscore "_" in these dir name
R35_TOT_IMAGES_DIR="images-R35-ToT"
R32x_R35i_IMAGES_DIR="images-R32x-R35i"
R35A_R35i_IMAGES_DIR="images-R35A-R35i"
R32i_IMAGES_DIR="images-R32i"

COMMON_TAR_OPTIONS=( --warning=none --checkpoint=10000 --one-file-system --xattrs --xattrs-include=* )
PKC_KEY_FILE=""
SBK_KEY_FILE=""
BASE_VERSION=""
TARGET_BOARD=""
# Skip re-generating recovery image if it exists
SKIP_REC_IMG=0
BOARD_SPECS_ARRAY=""
CHIPID=""
# Images on following partitions are board specific.
BOARD_DEP_PARTS=( "BCT" "MB1_BCT" "bootloader-dtb" "bpmp-fw-dtb" "kernel-dtb" "recovery-dtb" "VER" )

# The kernel/kernel-dtb/recovery/recovery-dtb/esp images signing in R35 are
# followed by UEFI secureboot policy, and will be supported in next release.
UNSIGNED_PARTS=( "kernel" "A_kernel" "kernel-dtb" "A_kernel-dtb" "recovery" "recovery-dtb" "esp" )

# Support generating OTA payload package for the case
# with/without partition layout change
LAYOUT_CHANGE=0
# Utilities needed for partition update.
PARTITION_UPDATE_UTILITIES=( "nvbootctrl" )
# Support for specifying the rootfs update script by user
NV_ROOTFS_UPDATER="nv_ota_rootfs_updater.sh"
# Customer configuration file for image-based OTA
OTA_CUSTOMER_CONF="${OTA_DIR}/nv_ota_customer.conf"
OTA_CUSTOMER_CONF_DEFAULT="${OTA_DIR}/nv_ota_customer.conf.default"

# Support NVMe on Jetson Xavier NX eMMC and Jetson Orin NX/Nano
NVME_DEVICE="nvme0n1"
NVME_ROOTDEV="nvme0n1p1"
NVME_DTBO="BootOrderNvme.dtbo"
SDMMC_ROOTDEV="mmcblk0p1"
SUPPORTED_EXTERNAL_DEVICES=(
	'jetson-agx-xavier-devkit:nvme0n1'
	'jetson-agx-xavier-industrial:nvme0n1'
	'jetson-xavier-nx-devkit-emmc:nvme0n1'
	'jetson-agx-orin-devkit:nvme0n1'
	'jetson-agx-orin-devkit-industrial:nvme0n1'
	'jetson-orin-nano-devkit:nvme0n1'
)
T194_NVME_CFG="${LINUX_BASE_DIR}/tools/kernel_flash/flash_l4t_t194_nvme.xml"
T194_NVME_ROOTFS_AB_CFG="${LINUX_BASE_DIR}/tools/kernel_flash/flash_l4t_t194_nvme_rootfs_ab.xml"
T194_NVME_ROOTFS_ENC_CFG="${LINUX_BASE_DIR}/tools/kernel_flash/flash_l4t_t194_nvme_rootfs_enc.xml"
T194_NVME_ROOTFS_AB_ENC_CFG="${LINUX_BASE_DIR}/tools/kernel_flash/flash_l4t_t194_nvme_rootfs_ab_enc.xml"
T234_NVME_CFG="${LINUX_BASE_DIR}/tools/kernel_flash/flash_l4t_t234_nvme.xml"
T234_NVME_ROOTFS_AB_CFG="${LINUX_BASE_DIR}/tools/kernel_flash/flash_l4t_t234_nvme_rootfs_ab.xml"
T234_NVME_ROOTFS_ENC_CFG="${LINUX_BASE_DIR}/tools/kernel_flash/flash_l4t_t234_nvme_rootfs_enc.xml"
T234_NVME_ROOTFS_AB_ENC_CFG="${LINUX_BASE_DIR}/tools/kernel_flash/flash_l4t_t234_nvme_rootfs_ab_enc.xml"

# Support disk encryption
BOOT_IMAGE_RAW_FILE="${BOOTLOADER_DIR}/system_boot.img.raw"
BOOT_IMAGE_B_RAW_FILE="${BOOTLOADER_DIR}/system_boot.img_b.raw"
DM_CRYPT_NAME="dm_crypt_rootfs_temp"
SYSTEM_IMAGE_RAW_ENC_FILE="${BOOTLOADER_DIR}/system_root_encrypted.img.raw"
SYSTEM_IMAGE_B_RAW_ENC_FILE="${BOOTLOADER_DIR}/system_root_encrypted.img_b.raw"
SYSTEM_IMAGE_EXT_RAW_ENC_FILE="${BOOTLOADER_DIR}/system_root_encrypted.img_ext.raw"
SYSTEM_IMAGE_EXT_B_RAW_ENC_FILE="${BOOTLOADER_DIR}/system_root_encrypted.img_ext_b.raw"

function usage()
{
	echo -ne "Usage: sudo $0 [options] <target board> <bsp version>\n"
	echo -ne "\tWhere,\n"
	echo -ne "\t\t<target board>: target board. Supported boards: jetson-agx-xavier-devkit, jetson-xavier-nx-devkit-emmc, jetson-agx-xavier-industrial, jetson-agx-orin-devkit, jetson-agx-orin-devkit-industrial, jetson-orin-nano-devkit.\n"
	echo -ne "\t\t<bsp version>: the version of the base BSP. Supported versions: R32-5, R32-6, R32-7, R35-2, R35-3, R35-4, R35-5.\n"
	echo -ne "\toptions:\n"
	echo -ne "\t\t-u <PKC key file>: PKC key used for odm fused board\n"
	echo -ne "\t\t-v <SBK key file>: Secure Boot Key (SBK) key used for ODM fused board\n"
	echo -ne "\t\t-i <enc rfs key file>: Key for disk encryption support\n"
	echo -ne "\t\t-s\tSkip generating system image.\n"
	echo -ne "\t\t-b\tUpdate bootloader only. Only valid for update without layout change.\n"
	echo -ne "\t\t-r\tUpdate rootfs only. Only valid for update without layout change.\n"
	echo -ne "\t\t-o\tSpecify the script to update rootfs partition.\n"
	echo -ne "\t\t-f\tSpecify the rootfs image to be written to rootfs partition.\n"
	echo -ne "\t\t-p\tSpecify the options directly passed into flash.sh when generating images.\n"
	echo -ne "\t\t--external-device <external device>: Specify the external device to be OTAed. Supported devices: nvme0n1.\n"
	echo -ne "\t\t  This option is only valid for jetson-xavier-nx-devkit-emmc.\n"
	echo -ne "\t\t-S <size>: Specify the size of rootfs partition on external device. Only valid when --external-device option is set. KiB, MiB, GiB short hands are allowed\n"
	echo -ne "\t\t  Ths size set through this option must be the same as the size of rootfs partition on the external device to be OTAed.\n"
	echo -ne "\t\t-E <esp image>: Specify the image to update ESP. Only valid for update without layout change.\n"
	echo -ne "\t\t-T <ext num sectors>: Specify the number of the sectors of the external storage device.\n"
	echo -ne "Example:\n"
	echo -ne "\t1. Upgrade from R32.7.x to R35 on NX\n"
	echo -ne "\tsudo $0 jetson-xavier-nx-devkit-emmc R32-7\n"
	echo -ne "\t2. Upgrade from R35.2 to R35 ToT on AGX Xavier with rootfs A/B enabled\n"
	echo -ne "\tsudo ROOTFS_AB=1 $0 jetson-agx-xavier-devkit R35-2\n"
	echo -ne "\t3. Upgrade from R32.7.x to R35 on NX with NVMe device that has 14GiB APP partition\n"
	echo -ne "\tsudo $0 --external-device nvme0n1 -S 14GiB jetson-xavier-nx-devkit-emmc R32-7\n"
	exit 1
}

function check_intermediate_partitions()
{
	local a_idx_file="${1}"
	local b_idx_file="${2}"

	mapfile -t a_msi_partitions < <(awk '/MSI_EMMC_START/, EOF {print $2 $3}' "${a_idx_file}")
	mapfile -t b_msi_partitions < <(awk '/MSI_EMMC_START/, EOF {print $2 $3}' "${b_idx_file}")
	if [ "${#a_msi_partitions[@]}" != "${#b_msi_partitions[@]}" ]; then
		echo "Error: the MSI partitions layout in ${a_idx_file} and ${b_idx_file} do not match"
		exit 1
	fi

	# Compared each partition of MSI in the 2 intermediate layout file and
	# make sure the offset of each of them is not changed. If the offset changes,
	# some partition might be missed and OTA process might be failed.
	mapfile -t diff < <(paste -d, <(printf '%s\n' "${a_msi_partitions[@]}") <(printf '%s\n' "${b_msi_partitions[@]}") | awk -F, '$2 != $5')
	if [ "${#diff[@]}" -gt 0 ]; then
		for item in "${diff[@]}"
		do
			IFS=, read -r a_part a_part_offset _ b_part b_part_offset <<< "${item}"
			echo "Error: ${a_part##*:}:${a_part_offset} does not match with ${b_part##*:}:${b_part_offset}"
		done
		exit 1
	fi
}

function check_bootchain_a_partitions()
{
	local current_idx_file="${1}"
	local next_idx_file="${2}"

	# Find all bootchain A partition names in the boot device
	mapfile -t b_boot_part_names < \
		<(awk '$2 ~ NR==1,/secondary_gpt/ {if($2 ~ /.*_b,$/ && $2 !~ /VER/) print $2}' "${current_idx_file}" | sed -r 's/(.*:)(.*)(,$)/\2/')
	local a_boot_part_names=("${b_boot_part_names[@]/_b/}")

	# Pick up bootchain A partitions from the current layout
	mapfile -t current_parts < <(awk '{print $2 $3}' "${current_idx_file}")
	local a_boot_parts=()
	for part in "${current_parts[@]}"
	do
		IFS=, read -r name _ <<< "${part}"
		if [[ " ${a_boot_part_names[*]} " == *" ${name##*:} "* ]]; then
			a_boot_parts+=("${part}")
		fi
	done

	# Compare the offset of each bootchain A partition with that in the next
	# intermediate layout.
	for part in "${a_boot_parts[@]}"
	do
		IFS=, read -r name offset <<< "${part}"
		_offset=$(awk -v pattern="^${name},$" '$2 ~ pattern {sub(",", "", $3); print $3}' "${next_idx_file}")
		if [ "${offset}" != "${_offset}" ]; then
			echo "Error: ${name##*:}: ${offset} does not match with ${_offset}"
			exit 1
		fi
	done
}

function init()
{
	OTA_BASE_DIR_TMP="${LINUX_BASE_DIR}/ota_base_dir_tmp"
	SYSTEM_IMAGE_TMP="${OTA_BASE_DIR_TMP}/sysimg_tmp"
	if [ -e "${OTA_BASE_DIR_TMP}" ]; then
		rm "${OTA_BASE_DIR_TMP}" -rf
	fi
	mkdir "${OTA_BASE_DIR_TMP}"

	# Remove existing raw images if "{skip_system_img}" is 0
	if [ "${skip_system_img}" == 0 ] ; then
		rm -f "${SYSTEM_IMAGE_RAW_FILE}" "${SYSTEM_IMAGE_RAW_ENC_FILE}" \
			"${SYSTEM_IMAGE_EXT_RAW_ENC_FILE}"
		if [ "${ROOTFS_AB}" == 1 ]; then
			rm -f "${SYSTEM_IMAGE_B_RAW_FILE}" "${SYSTEM_IMAGE_B_RAW_ENC_FILE}" \
				"${SYSTEM_IMAGE_EXT_B_RAW_ENC_FILE}"
		fi
	fi
}

function construct_board_spec_name()
{
	# Construct the board spec name based on the items:
	# boardid, boardver(fab), boardsku and boardrev
	# Usage:
	#	construct_board_spec_name {return_value}
	local return_value="$1"
	local name=
	local l_boardid=
	local l_boardver=
	local l_boardsku=
	local l_boardrev=

	l_boardid="${boardid}"
	l_boardver="${fab}"
	l_boardsku="${boardsku}"
	l_boardrev="${boardrev}"

	# For t19x devices, the boardid and boardver(fab) are compulsory, so return error
	# if either of them is empty.
	if [ "${CHIPID}" == "0x19" ]; then
		if [ "${l_boardid}" == "" ] || [ "${l_boardver}" == "" ]; then
			echo "Both BOARDID and BOARDVER(FAB) must be set"
			exit 1
		fi
	fi

	# For jetson-agx-orin-devkit, set the boardver to null if boardsku is 0004,
	# or 0005.
	# For jetson-orin-nano-devkit, set the boardver to null if boardsku is 0001,
	# 0003, 0004 or 0005.
	# For jetson-agx-orin-devkit-industrial, set the boardver to null if boardsku
	# is 0008.
	local board_sku_array=(
		'jetson-agx-orin-devkit:0004'
		'jetson-agx-orin-devkit:0005'
		'jetson-orin-nano-devkit:0001'
		'jetson-orin-nano-devkit:0003'
		'jetson-orin-nano-devkit:0004'
		'jetson-orin-nano-devkit:0005'
		'jetson-agx-orin-devkit-industrial:0008'
	)
	local temp="${board}:${boardsku}"
	local entry=
	for entry in "${board_sku_array[@]}"
	do
		if [ "${entry}" == "${temp}" ]; then
			l_boardver=""
			break
		fi
	done

	# Board spec name is composed of boardid, boardver, boardsku and
	# boardrev, like this:
	# board_spec_name=${l_boardid}-${l_boardver}-${l_boardsku}-${l_boardrev}
	name="${l_boardid}-${l_boardver}-${l_boardsku}-${l_boardrev}"
	eval "${return_value}=${name}"
}

function copy_board_dep_files()
{
	# Copy the files for some compatible board spec into the
	# sub-directory named as the compatible board spec name under
	# the "images-XXX-XXX"
	# Usage:
	#	 copy_board_dep_files {src_dir} {dest_dir}
	local src_dir="$1"
	local dest_dir="$2"
	local idx_file="${src_dir}/flash.idx"
	local partition=
	local file_name=

	# Copy the images dependent on board SPEC
	for partition in "${BOARD_DEP_PARTS[@]}"; do
		file_name="$(grep ":${partition}," "${idx_file}" | cut -d, -f 5 | sed 's/^ //' -)"
		if [ "${file_name}" == "" ]; then
			# For the intermedate layout file, the file for the "${partition}"
			# might be empty, then try seach the "${partition}_b"
			file_name="$(grep ":${partition}_b," "${idx_file}" | cut -d, -f 5 | sed 's/^ //' -)"
			if [ "${file_name}" == "" ]; then
				echo "Warning: the file written to partition ${partition} is not found"
				continue
			fi
		fi

		local file_list=("${file_name}")
		if [ "${partition}" == "BCT" ]; then
			file_list+=("${file_name/br_bct/br_bct_b}")
		fi

		for file_name in "${file_list[@]}"
		do
			if [ -f "${src_dir}/${file_name}" ]; then
				cp "${src_dir}/${file_name}" "${dest_dir}/"
			else
				# For some files, such as file for VER/VER_b partition, it is
				# not signed, so it should be under the parent directory of "src_dir"
				if [ -f "${src_dir}/../${file_name}" ]; then
					cp "${src_dir}/../${file_name}" "${dest_dir}/"
				else
					echo "Error: the file ${src_dir}/../${file_name} is not found"
					return 1
				fi
			fi
		done
	done

	# copy images that are dynamically generated each time the command
	# for generating images is executed into the sub-directories.
	# The content of these images are different and their sha1sum are
	# different in the "flash.idx".
	# These images are for partitions: secondary_gpt, primary_gpt
	cp "${src_dir}"/gpt*.bin "${dest_dir}"/

	# Copy the flash.idx file
	cp "${idx_file}" "${dest_dir}/"

	return 0
}

function copy_signed_files()
{
	# Copy all signed images to "images-XXX-XXX" directory and
	# copy board specific images to "images-XXX-XXX/<board_spec>"
	# Usage:
	#  copy_signed_files {dest_dir}
	#
	#  where {dest_dir} is images-XXX-XXX/<board_spec>
	local dest_dir="$1"
	local src_dir=

	# Find the signed source directory
	if [ -s "${SBK_KEY_FILE}" ]; then
		echo "encrypted_signed_dir=${dest_dir}"
		if [ "${CHIPID}" == "0x19" ]; then
			src_dir="${BOOTLOADER_DIR}/encrypted_signed_t19x"
		else
			src_dir="${BOOTLOADER_DIR}/enc_signed"
		fi
	else
		echo "signed_dir=${dest_dir}"
		src_dir="${BOOTLOADER_DIR}/signed"
	fi

	# When PKC key is present, all signed images are board specific
	if [ -s "${PKC_KEY_FILE}" ]; then
		cp -r "${src_dir}"/* "${dest_dir}"/
	else
		# Otherwise, only copy board specific images to "images-XXX-XXX/<board_spec>"
		if ! copy_board_dep_files "${src_dir}" "${dest_dir}"; then
			echo "Error: failed copy boardspec dependent files from ${src_dir} to ${dest_dir}"
			return 1
		fi

		# Copy all images to "images-XXX-XXX".
		# Note: When OTA starts on target, the board specific images will be loaded
		# in from "images-XXX-XXX/<board_spec>" and stored under "images-XXX-XXX"
		dest_dir="${dest_dir%/*}"
		cp -r "${src_dir}"/* "${dest_dir}"/
	fi

	return 0
}

# Copy images to boot partition (APP/APP_b) when ROOTFS_ENC is 1
function copy_images_to_boot()
{
	local src_dir="${1}"
	local dest_dir="${2}"

	if [ "${ROOTFS_ENC}" != 1 ]; then
		return 0
	fi

	local boot_image
	boot_image="${BOOT_IMAGE_RAW_FILE##*/}"
	echo "Copying ${src_dir}/${boot_image} to ${dest_dir}/"
	cp -v "${src_dir}/${boot_image}" "${dest_dir}"/
	if [ "${ROOTFS_AB}" == 1 ]; then
		boot_image="${BOOT_IMAGE_B_RAW_FILE##*/}"
		echo "Copying ${src_dir}/${boot_image} to ${dest_dir}/"
		cp -v "${src_dir}/${boot_image}" "${dest_dir}"/
	fi
}

function copy_unsigned_files()
{
	local dest_dir="$1"
	local src_dir="${BOOTLOADER_DIR}"
	local idx_file=

	# Locate the "flash.idx"
	if [ -s "${SBK_KEY_FILE}" ]; then
		echo "encrypted_signed_dir=${dest_dir}"
		if [ "${CHIPID}" == "0x19" ]; then
			idx_file="${src_dir}/encrypted_signed_t19x/flash.idx"
		else
			idx_file="${src_dir}/enc_signed/flash.idx"
		fi
	else
		echo "signed_dir=${dest_dir}"
		idx_file="${src_dir}/signed/flash.idx"
	fi

	for partition in "${UNSIGNED_PARTS[@]}"
	do
		file_name="$(grep ":${partition}," "${idx_file}" | cut -d, -f 5 | sed 's/^ //' -)"
		if [ "${file_name}" == "" ]; then
			continue
		fi
		# The recovery/recovery-dtb in R35 is loaded in by UEFI.
		# It does not get signed by NV signing tool.
		if [ ! -f "${src_dir}/${file_name}" ]; then
			if [ "${partition}" == "recovery" ] \
				|| [ "${partition}" == "recovery-dtb" ] ; then
				continue;
			fi
			echo "Error: ${src_dir}/${file_name} is not found"
			return 1
		fi
		cp "${src_dir}/${file_name}" "${dest_dir}"
	done

	# Copy images to boot partition
	copy_images_to_boot "${src_dir}" "${dest_dir}"
	return 0
}

function get_esp_image_name()
{
	local target_dir="${1}"
	local _ret_image_name="${2}"
	local idx_file=
	local image_name=

	idx_file="$(find "${target_dir}"/* -name "flash.idx" | tail -n 1)"
	image_name="$(grep ":esp," "${idx_file}" | cut -d, -f 5 | sed 's/^ //' -)"
	if [ "${image_name}" == "" ]; then
		echo "No image is specified for ESP"
		return 1
	fi
	eval "${_ret_image_name}=${image_name}"
	return 0
}

function remove_esp_image()
{
	local target_dir="${1}"
	local idx_file=
	local item=
	local image_name=

	pushd "${target_dir}" > /dev/null 2>&1
	for item in *
	do
		# Remove esp image in each sub-directory
		if [ -d "${item}" ]; then
			idx_file="${item}/flash.idx"
			image_name="$(grep ":esp," "${idx_file}" | cut -d, -f 5 | sed 's/^ //' -)"
			if [ "${image_name}" != "" ]; then
				rm -f "${target_dir}/${item}/${image_name}"
			fi
		fi
	done
	popd > /dev/null 2>&1

}

function update_esp_image_sha1sum()
{
	local target_dir="${1}"
	local esp_image_sha1sum=
	local idx_file=
	local item=
	local sha1_sum=
	local sha1_sum_line=

	# Generate sha1sum for the specified esp image if it exists
	esp_image_sha1sum="$(sha1sum "${esp_image}" | cut -d\  -f 1)"

	pushd "${target_dir}" > /dev/null 2>&1
	for item in *
	do
		# Replace the sha1sum in the index file with the sha1sum
		# for the specified esp image
		if [ -d "${item}" ]; then
			idx_file="${item}/flash.idx"
			sha1_sum="$(grep ":esp," "${idx_file}" | cut -d, -f 8 | sed 's/^ //' -)"
			sha1_sum_line="$(sed -n -e '/esp,/=' "${idx_file}")"
			sed -i ''"${sha1_sum_line}"'s/'"${sha1_sum}"'/'"${esp_image_sha1sum}"'/' "${idx_file}"
		fi
	done
	popd > /dev/null 2>&1
}

function handle_esp_image()
{
	local images_dir=
	local dev=
	local idx_file=

	if [ -n "${external_device}" ]; then
		dev="${EXT_DEV}"
	else
		dev="${INT_DEV}"
	fi
	images_dir="${OTA_BASE_DIR_TMP}/${dev}/${R35_TOT_IMAGES_DIR}"

	local esp_image_name=
	if ! get_esp_image_name "${images_dir}" "esp_image_name"; then
		echo "Failed to run \"get_esp_image_name ${images_dir} esp_image_name\""
		exit 1
	fi

	# Remove the esp image defined in xml
	remove_esp_image "${images_dir}"

	# Copy the specified esp image into ${OTA_BASE_TMP_DIR} if it exists
	local esp_image_dst=
	if [ -n "${esp_image}" ]; then
		if [ -f "${esp_image}" ]; then
			esp_image_dst="${images_dir}/${esp_image_name}"
			cp "${esp_image}" "${esp_image_dst}"
			# Update the sha1sum for esp image in index file
			update_esp_image_sha1sum "${images_dir}"
		else
			echo "The specified esp image ${esp_image} is not found"
			exit 1
		fi
	fi
}

function check_external_device()
{
	local temp="${TARGET_BOARD}:${external_device}"
	local found=0

	for item in "${SUPPORTED_EXTERNAL_DEVICES[@]}"
	do
		if [ "${item}" == "${temp}" ]; then
			found=1
			break
		fi
	done

	if [ "${found}" == 0 ]; then
		echo "OTA with ${external_device} is not supported on ${TARGET_BOARD}"
		usage
	fi
}

function get_layout_for_external_device()
{
	local _ret_layout_file="${1}"
	local layout_file=
	local name=

	if [ "${CHIPID}" == "0x23" ]; then
		name="T234_NVME"
	else
		name="T194_NVME"
	fi
	if [ "${ROOTFS_AB}" == "1" ] && [ "${ROOTFS_ENC}" == 1 ]; then
		name="${name}_ROOTFS_AB_ENC"
	elif [ "${ROOTFS_AB}" == 1 ]; then
		name="${name}_ROOTFS_AB"
	elif [ "${ROOTFS_ENC}" == 1 ]; then
		name="${name}_ROOTFS_ENC"
	fi
	name="${name}_CFG"
	layout_file="${!name}"
	eval "${_ret_layout_file}=${layout_file}"
}

function generate_binaries()
{
	local ret_msg=""
	local src_dir=""
	local dest_dir=""
	local spec=
	local board_specs=
	local fuselevel="fuselevel_production"
	local board_spec_name=
	local build_system_img=
	local ext_cfg_file=

	board_specs="${!BOARD_SPECS_ARRAY}[@]"
	for spec in "${!board_specs}"; do
		eval "${spec}"

		# Report error if "signed_img_dir" is not set in "ota_board_specs.conf"
		if [ "${signed_img_dir}" == "" ]; then
			echo "ERROR: the \"signed_img_dir\" is not set"
			exit 1
		fi

		# Skip generating image for external device if no --external-device
		# option is specified
		if [ "${rootdev}" != "${SDMMC_ROOTDEV}" ] && [ "${external_device}" == "" ]; then
			echo "Skip generating binaries for \"${rootdev}\""
			continue
		fi

		# Generate raw system image only for the configurations that are
		# associated with non-intermediate layout.
		if [ "${board}" == "${TARGET_BOARD}" ]; then
			build_system_img=1
		else
			build_system_img=0
		fi

		board_arg="BOARDID=${boardid} FAB=${fab} BOARDSKU=${boardsku} "
		board_arg+="BOARDREV=${boardrev} FUSELEVEL=${fuselevel} CHIP_SKU=${chipsku} "

		cmd_arg="--no-flash --sign "
		if [ "${PKC_KEY_FILE}" != "" ] && [ -f "${PKC_KEY_FILE}" ]; then
			cmd_arg+="-u \"${PKC_KEY_FILE}\" "
		fi
		if [ "${SBK_KEY_FILE}" != "" ] && [ -f "${SBK_KEY_FILE}" ]; then
			cmd_arg+="-v \"${SBK_KEY_FILE}\" "
		fi
		if [ "${flash_opts}" != "" ]; then
			cmd_arg+="${flash_opts} "
		fi
		if [ "${ENC_RFS_KEY}" != "" ] && [ -f "${ENC_RFS_KEY}" ]; then
			cmd_arg+="-i \"${ENC_RFS_KEY}\" "
		fi
		# Do not generate system.img.raw if
		# 1) Updating bootloader only.
		# 2) Skip generating system image
		# 3) User specified rootfs image is set
		if [ "${update_bl_only}" == 1 ] || \
			[ "${skip_system_img}" == 1 ] || \
			[ -n "${rootfs_image}" ] || \
			[ "${build_system_img}" == 0 ]; then
			sysimg="--no-systemimg"
		else
			sysimg=""
		fi
		cmd_arg+="${sysimg} "

		# Indicate boot order when external device exists.
		# Note: The external device currently supported only includes NVMe.
		if [ -n "${external_device}" ]; then
			# Specify booting from NVMe
			board_arg+="ADDITIONAL_DTB_OVERLAY=\"${NVME_DTBO},\" "

			# For external device, set "BOOTDEV=<device>" to generate
			# rootfs image and add other options for external device only.
			if [ "${rootdev}" == "${NVME_ROOTDEV}" ]; then
				board_arg+="BOOTDEV=${NVME_ROOTDEV} "
				cmd_arg+="--external-device "
				# Get the layout file for external device
				get_layout_for_external_device ext_cfg_file
				cmd_arg+="-c \"${ext_cfg_file}\" "
				if [ -n "${rootfs_size}" ]; then
					cmd_arg+="-S \"${rootfs_size}\" "
				fi
				if [ -n "${ext_num_sector}" ]; then
					cmd_arg+="-T \"${ext_num_sector}\" "
				fi
			fi
		fi

		# Set ENV_ECID if disk encryption is enabled
		if [ "${ROOTFS_ENC}" == 1 ]; then
			cmd_arg+="--generic-passphrase "
		fi

		cmd_arg+="${board} ${rootdev}"

		# Construct the command
		cmd="ROOTFS_ENC=${ROOTFS_ENC} ROOTFS_AB=${ROOTFS_AB} SKIP_REC_IMG=${SKIP_REC_IMG} "
		cmd+="${board_arg} ${LINUX_BASE_DIR}/flash.sh ${cmd_arg}"

		# construct the name of the sub-directory by parsing the board spec
		if ! construct_board_spec_name "board_spec_name"; then
			echo "Failed to construct board spec name"
			exit 1
		fi

		# generate R35 binaries
		echo -e "${cmd}\r\n"
		if eval "${cmd}"; then
			# Create destination directory
			if [ "${rootdev}" == "${SDMMC_ROOTDEV}" ]; then
				dest_dir="${OTA_BASE_DIR_TMP}/${INT_DEV}/${signed_img_dir}/${board_spec_name}"
			else
				dest_dir="${OTA_BASE_DIR_TMP}/${EXT_DEV}/${signed_img_dir}/${board_spec_name}"
			fi
			mkdir -p "${dest_dir}"

			# Copy the signed files into destination directory
			if ! copy_signed_files "${dest_dir}"; then
				echo "Error: failed to copy signed files in the ${dest_dir}"
				exit 1
			fi
			# Copy the unsigned files into destination directory
			if ! copy_unsigned_files "${dest_dir}"; then
				echo "Error: failed to copy unsigned files in the ${dest_dir}"
				exit 1
			fi
			ret_msg+="\r\nSUCCESS: generate binaries for OTA \"${spec}\""
		else
			ret_msg+="\r\nFAILURE: no binaires for OTA \"${spec}\""
			exit 1
		fi

		# Generate recovery image only one time as its content never changes
		# during generating images for the same board. No need to re-generate
		# it every time calling the "flash.sh" to generate images.
		SKIP_REC_IMG=1

		if [ "${board}" == "${TARGET_BOARD}" ]; then
			# Save nv_boot_control.conf to ota_nv_boot_control.conf
			local rootfs_dir="${LINUX_BASE_DIR}/rootfs"
			local nv_boot_control_conf="${rootfs_dir}/etc/nv_boot_control.conf"
			local ota_nv_boot_control_conf="${OTA_DIR}/ota_nv_boot_control.conf"
			if [ ! -f "${nv_boot_control_conf}" ]; then
				echo "The file ${nv_boot_control_conf} is not found"
				exit 1
			fi
			cp "${nv_boot_control_conf}" "${ota_nv_boot_control_conf}"

			# Build rootfs image tar ball based on the generated system.img.raw
			if [ "${update_bl_only}" == 0 ]; then
				if [ "${rootdev}" == "${SDMMC_ROOTDEV}" ]; then
					build_rootfs_image_and_overlay_packages "${INT_DEV}"
				else
					build_rootfs_image_and_overlay_packages "${EXT_DEV}"
				fi
			fi
		fi
	done

	if [ "${LAYOUT_CHANGE}" == 0 ]; then
		# Remove unneeded images
		remove_unneeded_images

		# Use user provided ESP image if specified
		handle_esp_image
		echo -e "${ret_msg}"
		return 0
	fi

	# Check the 2 intermedate layout files to make sure that the offset of
	# the partitions are matched.
	read -r first_msi_interm_dir second_msi_interm_dir <<< "$(get_msi_interm_dirs "${OTA_BASE_DIR_TMP}" "${INT_DEV}")"
	local first_msi_interm_idx_file="${first_msi_interm_dir}/${board_spec_name}/flash.idx"
	local second_msi_interm_idx_file="${second_msi_interm_dir}/${board_spec_name}/flash.idx"
	check_intermediate_partitions "${first_msi_interm_idx_file}" "${second_msi_interm_idx_file}"

	# Special handling for platform like Xavier NX which uses an extra intermediate
	# layout R32i as the first phrase. Need to make sure bootchain a partitions are
	# matched with that in the next intermediate layout.
	local first_interm_dir=
	first_interm_dir=$(get_first_interm_dir "${OTA_BASE_DIR_TMP}" "${INT_DEV}")
	local first_interm_idx_file="${first_interm_dir}/${board_spec_name}/flash.idx"
	if [ "${first_interm_idx_file}" != "${first_msi_interm_idx_file}" ]; then
		check_bootchain_a_partitions "${first_interm_idx_file}" "${first_msi_interm_idx_file}"
	fi
	echo -e "${ret_msg}"
}

function umount_rootfs()
{
	local rootfs_mnt="${1}"
	local umount_timeout=60
	local sleep_interval=3

	# Try unmounting rootfs until it is successful or timeout
	while true
	do
		if ! umount "${rootfs_mnt}"; then
			sleep "${sleep_interval}"
			umount_timeout=$((umount_timeout - sleep_interval))
			if [ "${umount_timeout}" -eq 0 ]; then
				echo "Failed to umount ${rootfs_mnt}"
				exit 1
			fi
		else
			break
		fi
	done

	# Lock it if it is LUKS format and release the loop device
	local rootfs_dev=
	if [ "${ROOTFS_ENC}" == 1 ]; then
		rootfs_dev="$(cryptsetup status "${DM_CRYPT_NAME}" | grep "device:" | cut -d: -f 2 | sed 's/^[ ]\+//g')"
		cryptsetup close "${DM_CRYPT_NAME}"
		losetup -d "${rootfs_dev}"
	fi
	return 0
}

function mount_luks_rootfs()
{
	local rootfs_image="${1}"
	local mount_point="${2}"
	local rootfs_dev=
	local rootfs_uuid=
	local unlock_cmd=

	rootfs_dev="$(losetup --show -f "${rootfs_image}")"
	if [ "${rootfs_dev}" == "" ]; then
		echo "Failed to bind loop device to ${rootfs_image}"
		return 1
	fi

	if ! cryptsetup isLuks "${rootfs_dev}"; then
		echo "The ${rootfs_image} is not LUKS format"
		return 1
	fi
	rootfs_uuid="$(cryptsetup luksUUID "${rootfs_image}")"
	unlock_cmd="${LINUX_BASE_DIR}/tools/disk_encryption/gen_luks_passphrase.py -g -c ${rootfs_uuid} "
	if [ "${ENC_RFS_KEY}" != "" ]; then
		unlock_cmd+="-k \"${ENC_RFS_KEY}\" "
	fi
	unlock_cmd+=" | cryptsetup luksOpen ${rootfs_dev} ${DM_CRYPT_NAME}"
	if ! eval "${unlock_cmd}"; then
		echo "Failed to unlock LUKS image ${rootfs_image}"
		losetup -d "${rootfs_dev}"
		return 1
	fi
	if ! mount "/dev/mapper/${DM_CRYPT_NAME}" "${mount_point}"; then
		echo "Failed to mount /dev/mapper/${DM_CRYPT_NAME}"
		cryptsetup close "${DM_CRYPT_NAME}"
		losetup -d "${rootfs_dev}"
		return 1
	fi
	return 0
}

function mount_rootfs()
{
	local rootfs_image="${1}"
	local mount_point="${2}"

	if [ "${ROOTFS_ENC}" == 1 ]; then
		if ! mount_luks_rootfs "${rootfs_image}" "${mount_point}"; then
			echo "Failed to mount luks rootfs image ${rootfs_image} on ${mount_point}"
			return 1
		fi
	else
		if ! mount "${rootfs_image}" "${mount_point}"; then
			echo "Failed to mount ${rootfs_image} on ${mount_point}"
			return 1
		fi
	fi
	return 0
}

function generate_overlay_package()
{
	local rootfs_image="${1}"
	local overlay_package="${2}"
	local overlay_tmp="/tmp/rootfs_overlay"

	mkdir -p "${SYSTEM_IMAGE_TMP}"

	# Mount raw rootfs image
	if ! mount_rootfs "${rootfs_image}" "${SYSTEM_IMAGE_TMP}"; then
		echo "Failed to run \"mount_rootfs ${rootfs_image} ${SYSTEM_IMAGE_TMP}\""
		rm "${SYSTEM_IMAGE_TMP}" -rf
		exit 1
	fi

	# Copy the required files into overlay directory and
	# generate tar ball as overlay package.
	local item=
	mkdir -p "${overlay_tmp}"
	pushd "${overlay_tmp}" > /dev/null 2>&1
	set +e

	for item in "${ROOTFS_OVERLAY_FILES[@]}"
	do
		# Remove the "/" at the head of item's path before
		# concatenate it to other paths.
		item="${item#*/}"

		if [ -d "${SYSTEM_IMAGE_TMP}/${item}" ]; then
			mkdir -p "${item}"
			cp -rf "${SYSTEM_IMAGE_TMP}/${item}"/* ./"${item}"/
		elif [ -f "${SYSTEM_IMAGE_TMP}/${item}" ]; then
			mkdir -p "${item%/*}"
			cp -f "${SYSTEM_IMAGE_TMP}/${item}" ./"${item%/*}"/
		fi
	done

	# Making overlay package
	if ! tar cpf - "${COMMON_TAR_OPTIONS[@]}" . | gzip -1 > "${overlay_package}"; then
		echo "Failed to make overlay package ${overlay_package}"
		popd > /dev/null 2>&1
		rm "${overlay_tmp}" -rf
		umount_rootfs "${SYSTEM_IMAGE_TMP}"
		rm "${SYSTEM_IMAGE_TMP}" -rf
		exit 1
	fi

	set -e
	popd > /dev/null 2>&1
	sync
	rm "${overlay_tmp}" -rf

	# Try umounting the raw rootfs image
	umount_rootfs "${SYSTEM_IMAGE_TMP}"
	rm "${SYSTEM_IMAGE_TMP}" -rf
}

function generate_rootfs_ab_overlay()
{
	# Generate rootfs A/B overlay
	local device="${1}"
	local rootfs_raw_a="${SYSTEM_IMAGE_RAW_FILE}"
	local rootfs_raw_b="${SYSTEM_IMAGE_B_RAW_FILE}"
	local rootfs_overlay_package_a="${OTA_BASE_DIR_TMP}/${device}/${ROOTFS_OVERLAY_PACKAGE_A}"
	local rootfs_overlay_package_b="${OTA_BASE_DIR_TMP}/${device}/${ROOTFS_OVERLAY_PACKAGE_B}"

	if [ "${ROOTFS_ENC}" == 1 ]; then
		if [ "${device}" == "${EXT_DEV}" ]; then
			rootfs_raw_a="${SYSTEM_IMAGE_EXT_RAW_ENC_FILE}"
			rootfs_raw_b="${SYSTEM_IMAGE_EXT_B_RAW_ENC_FILE}"
		else
			rootfs_raw_a="${SYSTEM_IMAGE_RAW_ENC_FILE}"
			rootfs_raw_b="${SYSTEM_IMAGE_B_RAW_ENC_FILE}"
		fi
	fi

	local rootfs_raw_images=(
		"${rootfs_raw_a}:${rootfs_overlay_package_a}"
		"${rootfs_raw_b}:${rootfs_overlay_package_b}"
	)

	local item=
	local rootfs_image=
	local overlay_package=
	for item in "${rootfs_raw_images[@]}"
	do
		rootfs_image="$(echo "${item}" | cut -d: -f 1)"
		overlay_package="$(echo "${item}" | cut -d: -f 2)"
		generate_overlay_package "${rootfs_image}" "${overlay_package}"
	done
	echo -e "\r\nSUCCESS: generate rootfs A/B overlay package"
}

function generate_rootfs_ab_base_image()
{
	# Generate base rootfs image for rootfs A/B enabled
	local device="${1}"
	local app_system_file="${OTA_BASE_DIR_TMP}/${device}/system.img"
	local app_system_sha1_file="${OTA_BASE_DIR_TMP}/${device}/system.img.sha1sum"
	local base_rootfs_tmp="/tmp/base_rootfs"
	local sha1_chksum=

	if [ ! -f "${app_system_file}" ]; then
		echo "Error: the APP image ${app_system_file} is not found";
		exit 1
	fi

	# Untar the generated APP image
	mkdir -p "${base_rootfs_tmp}"
	if ! tar -xzpf "${app_system_file}" "${COMMON_TAR_OPTIONS[@]}" -C "${base_rootfs_tmp}/"; then
		echo "Failed to extract ${app_system_file} into ${base_rootfs_tmp}"
		rm -rf "${base_rootfs_tmp}"
		exit 1
	fi

	# Remove the specified files and directories
	local item=
	pushd "${base_rootfs_tmp}" > /dev/null 2>&1
	set +e

	for item in "${ROOTFS_OVERLAY_FILES[@]}"
	do
		# Remove the "/" at the head of item's path before
		# concatenate it to other paths.
		item="${item#*/}"

		if [ -d ./"${item}" ] || [ -f ./"${item}" ]; then
			rm -rf ./"${item}"
		fi
	done

	# Making base rootfs image
	if ! tar cpf - "${COMMON_TAR_OPTIONS[@]}" . | gzip -1 > "${app_system_file}"; then
		echo "Failed to make base rootfs image ${app_system_file}"
		popd > /dev/null 2>&1
		rm "${base_rootfs_tmp}" -rf
		exit 1
	fi

	# Generate sha1 checksum for base rootfs image
	sha1_chksum="$(sha1sum "${app_system_file}" | cut -d\  -f 1)"
	echo -n "${sha1_chksum}" > "${app_system_sha1_file}"

	set -e
	popd > /dev/null 2>&1
	rm "${base_rootfs_tmp}" -rf
	echo -e "\r\nSUCCESS: generate base rootfs image for rootfs A/B enabled"
}

function generate_rootfs_image()
{
	# Generate rootfs image for internal device or external device
	local device="${1}"
	local app_system_file="${OTA_BASE_DIR_TMP}/${device}/system.img"
	local app_system_sha1_file="${OTA_BASE_DIR_TMP}/${device}/system.img.sha1sum"
	local sha1_chksum=
	local raw_system_image=

	if [ "${ROOTFS_ENC}" == 1 ]; then
		if [ "${device}" == "${EXT_DEV}" ]; then
			raw_system_image="${SYSTEM_IMAGE_EXT_RAW_ENC_FILE}"
		else
			raw_system_image="${SYSTEM_IMAGE_RAW_ENC_FILE}"
		fi
	else
		raw_system_image="${SYSTEM_IMAGE_RAW_FILE}"
	fi

	# If user specifies his own rootfs image and corresponding rootfs updater,
	# copy the specified rootfs image into "${OTA_BASE_DIR_TMP}" directly
	# If user specifies his own rootfs image, but not specify corresponding
	# rootfs updater, replace "system.img.raw" with user's rootfs image.
	if [ -n "${rootfs_image}" ]; then
		if [ "${rootfs_updater}" != "" ]; then
			cp -f "${rootfs_image}" "${app_system_file}"
			sha1_chksum="$(sha1sum "${app_system_file}" | cut -d\  -f 1)"
			echo -n "${sha1_chksum}" > "${app_system_sha1_file}"
			return 0
		else
			cp -f "${rootfs_image}" "${raw_system_image}"
		fi
	fi
	mkdir -p "${SYSTEM_IMAGE_TMP}"

	# Mount system.img.raw"
	if ! mount_rootfs "${raw_system_image}" "${SYSTEM_IMAGE_TMP}"; then
		echo "Failed to run \"mount_rootfs ${raw_system_image} ${SYSTEM_IMAGE_TMP}\""
		rm "${SYSTEM_IMAGE_TMP}" -rf
		exit 1
	fi

	pushd "${SYSTEM_IMAGE_TMP}" > /dev/null 2>&1
	set +e

	# Making APP image
	if ! tar cpf - "${COMMON_TAR_OPTIONS[@]}" . | gzip -1 > "${app_system_file}"; then
		echo "Failed to make APP image"
		popd > /dev/null 2>&1
		umount_rootfs "${SYSTEM_IMAGE_TMP}"
		rm "${SYSTEM_IMAGE_TMP}" -rf
		exit 1
	fi

	# Generate sha1 checksum for the app image
	sha1_chksum="$(sha1sum "${app_system_file}" | cut -d\  -f 1)"
	echo -n "${sha1_chksum}" > "${app_system_sha1_file}"

	set -e
	popd > /dev/null 2>&1
	sync
	# Try umounting the raw rootfs image
	umount_rootfs "${SYSTEM_IMAGE_TMP}"
	rm "${SYSTEM_IMAGE_TMP}" -rf

	echo -e "\r\nSUCCESS: generate APP image"
}

function build_rootfs_image_and_overlay_packages()
{
	local target_dir="${1}"

	# Generate rootfs image and store it into ${target_dir}
	generate_rootfs_image "${target_dir}"

	# Skip generating overlay package and base image if
	# rootfs image is provided by user as this should be
	# handled by user provided rootfs updater.
	if [ -n "${rootfs_image}" ]; then
		return 0
	fi

	if [ "${ROOTFS_AB}" == 1 ]; then
		# If rootfs A/B is enabled, generate overlay package and
		# base rootfs image.
		generate_rootfs_ab_overlay "${target_dir}"
		generate_rootfs_ab_base_image "${target_dir}"
	fi
}

function copy_ota_tasks()
{
	# Load variable OTA_TASKS and OTA_TASK_NEEDED_FILES defined in nv_ota_customer.conf
	local _declare=
	_declare=$(source "${OTA_CUSTOMER_CONF}" && declare -p OTA_TASKS OTA_TASK_NEEDED_FILES)
	eval "${_declare}"

	if [ -z "${OTA_TASKS}" ]; then
		echo "Error: can not find OTA_TASKS in ${OTA_CUSTOMER_CONF}"
		exit 1
	fi

	pushd "${OTA_DIR}"
	cp -v "${OTA_TASKS[@]}" "${OTA_TASK_NEEDED_FILES[@]}" "${OTA_BASE_DIR_TMP}"/
	popd
}

function copy_files_from_base_bsp()
{
	if [ -z "${BASE_BSP}" ]; then
		echo "Error: \"BASE_BSP\" is not set"
		exit 1
	fi
	echo "Copying mb1 image from ${BASE_BSP} for re-signing"
	cp -f "${BASE_BSP}/bootloader/${R32_MB1_FILE/_r32/}" "${BOOTLOADER_DIR}/${R32_MB1_FILE}"
	echo "Copying cbo.dts from ${BASE_BSP} for specifying boot order"
	cp -f "${BASE_BSP}/bootloader/cbo.dts" "${BOOTLOADER_DIR}/cbo.dts"
}

function update_external_layouts_t194()
{
	# Use the layouts for OTA from R32 to R35 on T194 devices
	T194_NVME_CFG="${OTA_DIR}/flash_l4t_t194_nvme_for_r32.xml"
	T194_NVME_ROOTFS_AB_CFG="${OTA_DIR}/flash_l4t_t194_nvme_rootfs_ab_for_r32.xml"
}

function clean_up_tmp_files()
{
	rm -rf "${OTA_BASE_DIR_TMP}"

	if [ -f "${BOOTLOADER_DIR}/${R32_MB1_FILE}" ]; then
		rm -f "${BOOTLOADER_DIR}/${R32_MB1_FILE}"
	fi
	if [ -f "${BOOTLOADER_DIR}/cbo.dts" ]; then
		rm -f "${BOOTLOADER_DIR}/cbo.dts"
	fi
	if [ -f "${BOOTLOADER_DIR}/cbo.dtb" ]; then
		rm -f "${BOOTLOADER_DIR}/cbo.dtb"
	fi
}

function generate_cbo_dtb()
{
	local cbo_dts_file="${BOOTLOADER_DIR}/cbo.dts"
	local cbo_dts_updated_file="${BOOTLOADER_DIR}/cbo_updated.dts"
	local cbo_dtb_updated_file="${BOOTLOADER_DIR}/cbo_updated.dtb"
	local dtc_bin="${LINUX_BASE_DIR}/kernel/dtc"

	# Set the emmc as the highest priority of booting device and
	# generate dtb file.
	cp "${cbo_dts_file}" "${cbo_dts_updated_file}"
	sed -i 's/boot-order = .*$/boot-order = \"emmc\", \"sd\", \"usb\", \"net\";/' "${cbo_dts_updated_file}"
	"${dtc_bin}" -I dts -O dtb -o "${cbo_dtb_updated_file}" "${cbo_dts_updated_file}"

	# Copy the genreated dtb file into "${OTA_BASE_DIR_TMP}" and
	# rename it as "cbo.dtb".
	cp "${cbo_dtb_updated_file}" "${OTA_BASE_DIR_TMP}"/cbo.dtb

	# Clean up
	rm -f "${cbo_dts_updated_file}" "${cbo_dtb_updated_file}"
}

function copy_common_images()
{
	# Generate base version file and board name file
	echo -n "${TARGET_BOARD}" >"${OTA_BASE_DIR_TMP}/board_name"
	echo -n "${BASE_VERSION}" >"${OTA_BASE_DIR_TMP}/base_version"

	# Pack "nv_ota_preserve_data.sh" into the OTA payload package.
	local nv_ota_preserve_data="nv_ota_preserve_data.sh"
	cp "${OTA_DIR}"/"${nv_ota_preserve_data}" "${OTA_BASE_DIR_TMP}"/

	# Copy NVIDIA provided rootfs updater to ${OTA_BASE_DIR_TMP}
	local nv_rootfs_updater="${OTA_DIR}/${NV_ROOTFS_UPDATER}"
	echo "Copy NVIDIA provided rootfs updater to ${OTA_BASE_DIR_TMP}/${NV_ROOTFS_UPDATER}"
	cp "${nv_rootfs_updater}" "${OTA_BASE_DIR_TMP}"/"${NV_ROOTFS_UPDATER}"

	# Copy customer provided rootfs updater
	if [ "${rootfs_updater}" != "" ]; then
		echo "Copy customer provided rootfs updater to ${OTA_BASE_DIR_TMP}/${rootfs_updater_name}"
		cp "${rootfs_updater}" "${OTA_BASE_DIR_TMP}"/"${rootfs_updater_name}"
	fi

	# Pack "nv_ota_run_tasks.sh" into the OTA payload package.
	local nv_ota_run_tasks="nv_ota_run_tasks.sh"
	cp "${OTA_DIR}"/"${nv_ota_run_tasks}" "${OTA_BASE_DIR_TMP}"/

	# Copy nv_ota_customer.conf to ${OTA_BASE_DIR_TMP} and recover it
	# if this file is modified due to the "-o" option
	cp "${OTA_CUSTOMER_CONF}" "${OTA_BASE_DIR_TMP}"/
	if [ -f "${OTA_CUSTOMER_CONF_DEFAULT}" ]; then
		cp "${OTA_CUSTOMER_CONF_DEFAULT}" "${OTA_CUSTOMER_CONF}"
		rm -f "${OTA_CUSTOMER_CONF_DEFAULT}"
	fi

	# Copy OTA version file and user version file
	local version_file_orig=
	local version_file=
	if [[ "${TARGET_BOARD}" == "jetson-agx-xavier-devkit" ]]; then
		version_file_orig="emmc_bootblob_ver.txt"
	else
		version_file_orig="qspi_bootblob_ver.txt"
	fi
	version_file="version.txt"
	cp "${BOOTLOADER_DIR}/${version_file_orig}" "${OTA_BASE_DIR_TMP}/${version_file}"
	cp "${LINUX_BASE_DIR}/rootfs/etc/user_release_version" "${OTA_BASE_DIR_TMP}/user_release_version"

	# Copy L4T Launcher
	cp "${L4T_LAUNCHER}" "${OTA_BASE_DIR_TMP}"/

	return 0
}

function copy_nv_bootloader_config_files()
{
	# Copy the "/opt/nvidia/l4t-bootloader-config/nv-l4t-bootloader-config.sh"
	# from rootfs to ${OTA_BASE_DIR_TMP}/.
	# For supporting multiple board spec, the "nv-l4t-bootloader-config.sh"
	# and the "${OTA_BASE_DIR_TMP}/ota_nv_boot_control.conf" are used to
	# update the "/etc/nv_boot_contro.conf".
	local rootfs_dir="${LINUX_BASE_DIR}/rootfs"
	local update_boot_control_script=
	update_boot_control_script="${rootfs_dir}/opt/nvidia/l4t-bootloader-config/nv-l4t-bootloader-config.sh"
	if [ ! -f "${update_boot_control_script}" ]; then
		echo "The file ${update_boot_control_script} is not found"
		exit 1
	fi
	cp "${update_boot_control_script}" "${OTA_BASE_DIR_TMP}"/
	cp "${OTA_DIR}/ota_nv_boot_control.conf" "${OTA_BASE_DIR_TMP}"/
}

function update_entry_in_customer_conf()
{
	# Update entry in customer.conf
	local entry_name="${1}"
	local entry_value="${2}"
	local found=

	if [ ! -f "${OTA_CUSTOMER_CONF}" ]; then
		echo "Customer configuration fie ${OTA_CUSTOMER_CONF} is not found"
		exit 1
	fi

	found="$(grep "${1}=" < "${OTA_CUSTOMER_CONF}")"
	if [ "${found}" == "" ]; then
		echo "ERROR: the entry ${entry_name} is not found in ${OTA_CUSTOMER_CONF}"
		exit 1
	fi
	echo "Back up ${OTA_CUSTOMER_CONF} to ${OTA_CUSTOMER_CONF_DEFAULT}"
	cp -f "${OTA_CUSTOMER_CONF}" "${OTA_CUSTOMER_CONF_DEFAULT}"
	echo "Update the value of ${entry_name} to ${entry_value}"
	echo "sed -i s/^${entry_name}=.*/${entry_name}=${entry_value}/g ${OTA_CUSTOMER_CONF}"
	sed -i "s/^${entry_name}=.*/${entry_name}=${entry_value}/g" "${OTA_CUSTOMER_CONF}"
}

function copy_base_recovery_image_and_dtb()
{
	local version="R32x"
	local base_recovery_img=
	local base_recovery_img_sha1=
	local base_recovery_dtb=
	local base_recovery_dtb_sha1=
	base_recovery_img="recovery.img.${version}"
	base_recovery_img_sha1="recovery.img.${version}.sha1sum"
	base_recovery_dtb="recovery.dtb.${version}"
	base_recovery_dtb_sha1="recovery.dtb.${version}.sha1sum"

	# Copy recovery image and recovery-dtb
	if [ ! -f "${BOOTLOADER_DIR}/${base_recovery_img}" ] \
		|| [ ! -f "${BOOTLOADER_DIR}/${base_recovery_img_sha1}" ] \
		|| [ ! -f "${BOOTLOADER_DIR}/${base_recovery_dtb}" ] \
		|| [ ! -f "${BOOTLOADER_DIR}/${base_recovery_dtb_sha1}" ]; then
		echo "ERROR: recovery image or recovery dtb for base system is not valid"
		exit 1
	fi
	cp "${BOOTLOADER_DIR}/${base_recovery_img}" "${OTA_BASE_DIR_TMP}"
	cp "${BOOTLOADER_DIR}/${base_recovery_img_sha1}" "${OTA_BASE_DIR_TMP}"
	cp "${BOOTLOADER_DIR}/${base_recovery_dtb}" "${OTA_BASE_DIR_TMP}"
	cp "${BOOTLOADER_DIR}/${base_recovery_dtb_sha1}" "${OTA_BASE_DIR_TMP}"
}

function generate_package()
{
	# OTA payload package for R32.7 to R35-ToT with layout change:
	# ota_payload_package.tar.gz
	# |-- base_version
	# |-- board_name
	# |-- cbo.dtb
	# |-- kernel_bootctrl.bin.normal
	# |-- kernel_bootctrl.bin.update
	# |-- layout_change
	# |-- nvbootctrl
	# |-- nv-l4t-bootloader-config.sh
	# |-- nv_ota_check_version.sh
	# |-- nv_ota_common_utils.func
	# |-- nv_ota_customer.conf
	# |-- nv_ota_preserve_data.sh
	# |-- nv_ota_rootfs_updater.sh
	# |-- nv_ota_run_tasks.sh
	# |-- nv_ota_update_all_in_recovery.sh
	# |-- nv_ota_update.sh
	# |-- nv_ota_validate.sh
	# |-- ota_nv_boot_control.conf
	# |-- ota_package.tar.sha1sum
	# |-- ota_package.tar
	#     |-- external_device (only for Xavier NX with NVMe device enabled)
	#         |-- images-R35-ToT
	#             |-- compatible spec 1
	#             |-- ...
	#             |-- compatible spec N
	#         |-- rootfs_overlay_a.tar.gz (if rootfs A/B enabled)
	#         |-- rootfs_overlay_b.tar.gz (if rootfs A/B enabled)
	#         |-- system.img
	#         |-- system.img.sha1sum
	#     |-- internal_device
	#         |-- images-R32i (only included for Xavier NX)
	#         |-- images-R32x-R35i
	#             |-- compatible spec 1 (for example, 2888-400-0001-D.0)
	#             |-- ...
	#             |-- compatible spec N (for example, 2888-400-0001-E.0)
	#         |-- images-R35A-R35i
	#             |-- compatible spec 1
	#             |-- ...
	#             |-- compatible spec N
	#         |-- images-R35-ToT
	#             |-- compatible spec 1
	#             |-- ...
	#             |-- compatible spec N
	#         |-- rootfs_overlay_a.tar.gz (if rootfs A/B enabled)
	#         |-- rootfs_overlay_b.tar.gz (if rootfs A/B enabled)
	#         |-- system.img
	#         |-- system.img.sha1sum
	#     |-- l4t_update_partitions.sh
	#     |-- ota_backup_files_list.txt
	#     |-- upgradetasklist.txt
	# |-- version.txt

	# Control files
	control_file_orig="upgradetasklist.txt.${TARGET_BOARD}.R32x_to_R35-ToT_emmc"
	control_file="upgradetasklist.txt"
	update_script="l4t_update_partitions.sh"

	# Binaries
	t19x_binary_list=("${BOOTLOADER_DIR}/xusb_sil_rel_fw")
	if [[ "${TARGET_BOARD}" == jetson-agx-xavier-industrial ]]; then
		t19x_binary_list+=("${BOOTLOADER_DIR}/badpage.bin")
	fi

	# Copy binaries
	for bin in "${t19x_binary_list[@]}"; do
		cp "${bin}" "${OTA_BASE_DIR_TMP}/${INT_DEV}/${R35_TOT_IMAGES_DIR}/"
		if [ -n "${external_device}" ]; then
			cp "${bin}" "${OTA_BASE_DIR_TMP}/${EXT_DEV}/${R35_TOT_IMAGES_DIR}/"
		fi
	done
	if [ "${CHIPID}" == "0x19" ]; then
		for bin in "${t19x_binary_list[@]}"; do
			cp "${bin}" "${OTA_BASE_DIR_TMP}/${INT_DEV}/${R32x_R35i_IMAGES_DIR}/"
			cp "${bin}" "${OTA_BASE_DIR_TMP}/${INT_DEV}/${R35A_R35i_IMAGES_DIR}/"
		done
	fi

	# Copy control files
	cp "${OTA_DIR}/${update_script}" "${OTA_BASE_DIR_TMP}"
	cp "${OTA_DIR}/${control_file_orig}" "${OTA_BASE_DIR_TMP}/${control_file}"
	sed -i "s/R32i/${R32i_IMAGES_DIR}/" "${OTA_BASE_DIR_TMP}/${control_file}"
	sed -i "s/R32x-R35i/${R32x_R35i_IMAGES_DIR}/" "${OTA_BASE_DIR_TMP}/${control_file}"
	sed -i "s/R35A-R35i/${R35A_R35i_IMAGES_DIR}/" "${OTA_BASE_DIR_TMP}/${control_file}"
	sed -i "s/R35-ToT/${R35_TOT_IMAGES_DIR}/" "${OTA_BASE_DIR_TMP}/${control_file}"
	if [ "${ROOTFS_AB}" == 1 ]; then
		# Enable writing APP_b partition
		sed -i 's/#<ROOTFS_B>//g' "${OTA_BASE_DIR_TMP}/${control_file}"
	fi
	# Enable writing NVME devices
	if [ "${external_device}" == "${NVME_DEVICE}" ]; then
		sed -i 's/#<EXTERNAL>//g' "${OTA_BASE_DIR_TMP}/${control_file}"
	fi
	# Enable writing encrypted rootfs partition
	if [ "${ROOTFS_ENC}" == 1 ]; then
		sed -i 's/#<ROOTFS_ENC>//g' "${OTA_BASE_DIR_TMP}/${control_file}"
	fi

	# Copy backup list file
	local ota_backup_list_file="ota_backup_files_list.txt"
	cp "${OTA_DIR}/${ota_backup_list_file}" "${OTA_BASE_DIR_TMP}"

	# Generate package
	pushd "${OTA_BASE_DIR_TMP}" > /dev/null 2>&1
	tar cvf "${OTA_PACKAGE_FILE}" ./* --remove-files
	sha1_chksum="$(sha1sum ${OTA_PACKAGE_FILE} | cut -d\  -f 1)"
	echo -n "${sha1_chksum}" > "${OTA_PACKAGE_SHA1_FILE}"

	# Copy kernel_bootctrl binaries
	cp "${OTA_DIR}/kernel_bootctrl.bin.normal" "${OTA_BASE_DIR_TMP}"/
	cp "${OTA_DIR}/kernel_bootctrl.bin.update" "${OTA_BASE_DIR_TMP}"/

	# Copy base recovery image and recovery dtb
	copy_base_recovery_image_and_dtb

	# Copy common images
	copy_common_images

	# Copy ota tasks listed in customer config file
	copy_ota_tasks

	# echo "1" to layout_change file
	echo -n "1" >"${OTA_BASE_DIR_TMP}/layout_change"

	# Copy nv-l4t-bootloader-config.sh and ota_nv_boot_control.conf
	copy_nv_bootloader_config_files

	# Copy the utilities needed for partition update
	if ! copy_utilities_for_ota_update "${PARTITION_UPDATE_UTILITIES[@]}"; then
		echo "Failed to run \"copy_utilities_for_ota_update\""
		return 1
	fi

	# If external device exists, generate the cbo.dtb that sets the
	# eMMC as the highest priority of boot device and then copy it
	# into ${OTA_BASE_DIR_TMP}.
	if [ "${external_device}" == "${NVME_DEVICE}" ]; then
		generate_cbo_dtb
	fi

	# Generate ota_payload_package.tar.gz
	tar zcvf "${OTA_PAYLOAD_PACKAGE_ZIP_FILE}" ./* --remove-files
	mkdir -p "${BOOTLOADER_DIR}/${TARGET_BOARD}"
	mv "${OTA_PAYLOAD_PACKAGE_ZIP_FILE}" "${BOOTLOADER_DIR}/${TARGET_BOARD}/"
	popd > /dev/null 2>&1

	# Clean up
	clean_up_tmp_files

	echo -e "\r\nSUCCESS: generate OTA package for update with layout changed \"${BOOTLOADER_DIR}/${TARGET_BOARD}/${OTA_PAYLOAD_PACKAGE_ZIP_FILE}\""
}

# Construct board spec entry for generating BUP
function construct_board_spec_entry()
{
	local suffix="${1}"
	local tmp_board_spec_entry="${2}"
	local tmp_board_spec_file="${3}"
	local entry=
	local item=

	if [ "${suffix}" != "" ]; then
		entry="${!BOARD_SPECS_ARRAY}_${suffix}[@]"
	else
		entry="${!BOARD_SPECS_ARRAY}[@]"
	fi
	entry=("${!entry}")
	if [ -f "${tmp_board_spec_file}" ]; then
		rm -f "${tmp_board_spec_file}"
	fi
	echo "${tmp_board_spec_entry}=(" >>"${tmp_board_spec_file}"
	for item in "${entry[@]}"
	do
		# For jetson-orin-nano-devkit, keep all the board spec
		# entries as it only includes entries fo external device.
		# For other devices, only keep the board spec entry for
		# internal device.
		if [[ "${item}" =~ jetson-orin-nano-devkit ]] \
			|| [[ "${item}" =~ mmcblk0p1 ]]; then
			echo "'${item}'" >>"${tmp_board_spec_file}"
		fi
	done
	echo ")" >>"${tmp_board_spec_file}"
}

# Generate BUP
function generate_BUP()
{
	local suffix="${1}"
	local bup_generator="${LINUX_BASE_DIR}"/l4t_generate_soc_bup.sh
	local chipid=
	case ${CHIPID} in
	0x19) chipid=t19x; ;;
	0x23) chipid=t23x; ;;
	*)
		echo "Error: un-supported CHIPID(${CHIPID})"
		exit 1
		;;
	esac;

	local board_name="${TARGET_BOARD}"
	local payload_dir="${BOOTLOADER_DIR}"/payloads_"${chipid}"
	local cmd=

	# Construct the board spec entry used to generate BUP
	local __board_spec_file=/tmp/board_spec_file
	local __board_spec_entry="tmp_board_spec"
	construct_board_spec_entry "${suffix}" "${__board_spec_entry}" "${__board_spec_file}"

	cmd="ROOTFS_AB=${ROOTFS_AB} ROOTFS_ENC= ${bup_generator} -f ${__board_spec_file} -e ${__board_spec_entry} -b ${board_name} "
	if [ "${PKC_KEY_FILE}" != "" ] && [ -f "${PKC_KEY_FILE}" ]; then
		cmd+="-u \"${PKC_KEY_FILE}\" "
	fi
	if [ "${SBK_KEY_FILE}" != "" ] && [ -f "${SBK_KEY_FILE}" ]; then
		cmd+="-v \"${SBK_KEY_FILE}\" "
	fi
	cmd+="${chipid}"
	pushd "${LINUX_BASE_DIR}" > /dev/null 2>&1
	echo "Generate BUP file by running command: ${cmd}"
	if ! eval "${cmd}"; then
		echo "Failed to run ${cmd}"
		exit 1
	fi

	if [ "${suffix}" != "" ]; then
		cp "${payload_dir}/bl_only_payload" "${payload_dir}/bl_only_payload_${suffix}"
	fi
	popd > /dev/null 2>&1
}

# Generate UEFI capsule with the generated BUP
function generate_uefi_capsule()
{
	local suffix="${1}"
	local chipid=
	local target_soc=

	case ${CHIPID} in
	0x19) chipid=t19x; target_soc=t194; ;;
	0x23) chipid=t23x; target_soc=t234; ;;
	*)
		echo "Error: un-supported CHIPID(${CHIPID})"
		exit 1
		;;
	esac;

	local payload_dir="${BOOTLOADER_DIR}"/payloads_"${chipid}"
	local bl_only_payload="${payload_dir}/bl_only_payload"
	local uefi_capsule_generator="${LINUX_BASE_DIR}"/generate_capsule/l4t_generate_soc_capsule.sh
	local uefi_capsule_file="${OTA_BASE_DIR_TMP}"/TEGRA_BL.Cap
	if [ "${suffix}" != "" ]; then
		bl_only_payload="${payload_dir}/bl_only_payload_${suffix}"
		uefi_capsule_file="${OTA_BASE_DIR_TMP}"/TEGRA_BL_${suffix}.Cap
	fi

	local cmd="${uefi_capsule_generator} -i ${bl_only_payload} -o ${uefi_capsule_file} ${target_soc}"

	pushd "${LINUX_BASE_DIR}" > /dev/null 2>&1
	echo "Generate UEFI capsule by running command: ${cmd}"
	if ! eval "${cmd}"; then
		echo "Failed to run \"${cmd}\""
		exit 1
	fi
	popd > /dev/null 2>&1

	echo "UEFI capsule is successfully generated at ${uefi_capsule_file}"
}

copy_utility()
{
	local src="${1}"
	local dst="${2}"

	if [ ! -x "${src}" ]; then
		echo "The utility ${src} is not valid"
		return 1
	fi

	cp "${src}" "${dst}"
	return 0
}

# Copy utilities from rootfs
copy_utilities_for_ota_update()
{
	local utils=("$@")
	local rootfs_dir="${LINUX_BASE_DIR}/rootfs"
	local src=
	local dst=
	local item=
	for item in "${utils[@]}"
	do
		src="${rootfs_dir}/usr/sbin/${item}"
		dst="${OTA_BASE_DIR_TMP}/${item}"
		if ! copy_utility "${src}" "${dst}"; then
			echo "Failed to run \"copy_utility ${src} ${dst}\""
			return 1
		fi
	done
	return 0
}

# Remove unneeded images in "images-R35-ToT" directory
function remove_unneeded_images()
{
	local dev=
	local images_dir=

	# Delete all the images for non-active storage device
	if [ -n "${external_device}" ]; then
		rm -rf "${OTA_BASE_DIR_TMP}/${INT_DEV}/"
		dev="${EXT_DEV}"
	else
		rm -rf "${OTA_BASE_DIR_TMP}/${EXT_DEV}/"
		dev="${INT_DEV}"
	fi

	# Get the path of "images-R35-ToT" directory
	images_dir="${OTA_BASE_DIR_TMP}/${dev}/${R35_TOT_IMAGES_DIR}"
	if [ ! -d "${images_dir}" ]; then
		echo "The directory ${images_dir} is not found"
		exit 1
	fi

	# Delete all the images in the "images-R35-ToT" except the images for
	# kernel, kernel-dtb, recovery, recovery-dtb and esp partitions.
	# It includes the followings steps:
	# 1. Traverse boardspec dependent directory under "images-R35-ToT"
	# 2. Delete all but the reserved images
	local idx_file=
	local partition=
	local image=
	local partitions_reserved=( "${UNSIGNED_PARTS[@]}" )
	local item=
	local boardspec_dir=
	local tmp_dir=/tmp/tmp_dir_for_reserved

	mkdir -p "${tmp_dir}"
	pushd "${images_dir}" > /dev/null 2>&1
	for item in *
	do
		# Delete file if it is not a directory
		if [ ! -d "${item}" ]; then
			rm -f "${item}"
			continue
		fi

		# Enter boardspec directory and reserve required images
		boardspec_dir="${item}"
		pushd "${boardspec_dir}" > /dev/null 2>&1
		idx_file="flash.idx"
		cp -v "${idx_file}" "${tmp_dir}"/
		for partition in "${partitions_reserved[@]}"
		do
			# Copy the reserved images into the temp directory
			image="$(grep "${partition}," <"${idx_file}" | cut -d, -f 5 | sed 's/^ //g' -)"
			if [ -f "${image}" ]; then
				cp -v "${image}" "${tmp_dir}"/
			fi
		done
		# Copy images to boot partition
		copy_images_to_boot ./ "${tmp_dir}"
		# Delete all the files, copy back the reserved images
		# Remove the temp
		rm -f ./*
		cp -v "${tmp_dir}"/* ./
		rm -f "${tmp_dir}"/*
		popd > /dev/null 2>&1
	done
	popd > /dev/null 2>&1
	rm -rf "${tmp_dir}"
}

# Generate UEFI capsule jetson-agx-orin-devkit with boardver(fab)=000
generate_uefi_capsule_for_agx_orin()
{
	# For boardver(fab)=000
	generate_BUP "3701_000"
	generate_uefi_capsule "3701_000"

	# For boardver(fab)=300
	generate_BUP "3701_300"
	generate_uefi_capsule "3701_300"
}

# Generate OTA payload package for the case of updating without layout change
function generate_package_for_no_layout_change()
{
	# OTA payload package for R35.2 to R35-ToT without layout change:
	# ota_payload_package.tar.gz
	# |-- base_version
	# |-- board_name
	# |-- BOOTAA64.efi (L4T Launcher)
	# |-- layout_change
	# |-- nv-l4t-bootloader-config.sh
	# |-- nv_ota_common.func
	# |-- nv_ota_customer.conf
	# |-- nv_ota_preserve_data.sh
	# |-- nv_ota_rootfs_updater.sh
	# |-- nv_ota_update_rootfs_in_recovery.sh
	# |-- ota_nv_boot_control.conf
	# |-- ota_package.tar.sha1sum
	# |-- ota_package.tar
	#     |-- external_device (only for Xavier NX with NVMe device enabled)
	#         |-- images-R35-ToT
	#             |-- compatible spec 1
	#             |-- ...
	#             |-- compatible spec N
	#         |-- rootfs_overlay_a.tar.gz (if rootfs A/B enabled)
	#         |-- rootfs_overlay_b.tar.gz (if rootfs A/B enabled)
	#         |-- system.img
	#         |-- system.img.sha1sum
	#     |-- internal_device
	#         |-- images-R35-ToT
	#             |-- compatible spec 1
	#             |-- ...
	#             |-- compatible spec N
	#         |-- rootfs_overlay_a.tar.gz (if rootfs A/B enabled)
	#         |-- rootfs_overlay_b.tar.gz (if rootfs A/B enabled)
	#         |-- system.img
	#         |-- system.img.sha1sum
	#     |-- ota_backup_files_list.txt
	# |-- TEGRA_BL.Cap (UEFI capsule)
	# |-- update_control
	# |-- version.txt

	# Copy backup list file
	local ota_backup_list_file="ota_backup_files_list.txt"
	cp "${OTA_DIR}/${ota_backup_list_file}" "${OTA_BASE_DIR_TMP}"/

	# Generate "ota_package.tar"
	pushd "${OTA_BASE_DIR_TMP}" > /dev/null 2>&1
	tar cvf "${OTA_PACKAGE_FILE}" ./* --remove-files
	sha1_chksum="$(sha1sum ${OTA_PACKAGE_FILE} | cut -d\  -f 1)"
	echo -n "${sha1_chksum}" > "${OTA_PACKAGE_SHA1_FILE}"

	# Copy common images
	copy_common_images

	# Copy ota tasks listed in customer config file
	copy_ota_tasks

	# Generate UEFI capsule for jetson-agx-orin-devkit
	if [ "${BASE_VERSION}" = "R35-2" ] \
		&& [ "${TARGET_BOARD}" == "jetson-agx-orin-devkit" ]; then
		generate_uefi_capsule_for_agx_orin
	else
		# Generate BUP
		generate_BUP

		# Generate UEFI capsule
		generate_uefi_capsule
	fi

	# Write "update_control" according to the update_bl_only and
	# update_rfs_only.
	if [ "${update_rfs_only}" == 1 ]; then
		echo "rootfs" > "${OTA_BASE_DIR_TMP}/update_control"
	elif [ "${update_bl_only}" == 1 ]; then
		echo "bootloader" > "${OTA_BASE_DIR_TMP}/update_control"
	else
		echo "bootloader" > "${OTA_BASE_DIR_TMP}/update_control"
		echo "rootfs" >> "${OTA_BASE_DIR_TMP}/update_control"
	fi

	# echo "0" to layout_change file
	echo -n "0" >"${OTA_BASE_DIR_TMP}/layout_change"

	# Copy nv-l4t-bootloader-config.sh and ota_nv_boot_control.conf
	copy_nv_bootloader_config_files

	# Copy nv_ota_common.func
	cp "${OTA_DIR}/nv_ota_common.func" "${OTA_BASE_DIR_TMP}"/

	# Generate ota_payload_package.tar.gz
	tar zcvf "${OTA_PAYLOAD_PACKAGE_ZIP_FILE}" ./* --remove-files
	mkdir -p "${BOOTLOADER_DIR}/${TARGET_BOARD}"
	mv "${OTA_PAYLOAD_PACKAGE_ZIP_FILE}" "${BOOTLOADER_DIR}/${TARGET_BOARD}/"
	popd > /dev/null 2>&1

	# Clean up
	clean_up_tmp_files

	echo -e "\r\nSUCCESS: generate OTA package for update without layout change \"${BOOTLOADER_DIR}/${TARGET_BOARD}/${OTA_PAYLOAD_PACKAGE_ZIP_FILE}\""
}

# Make sure that this script is running in root privilege
USERID=$(id -u)
if [ "${USERID}" -ne 0 ]; then
       echo "Please run this program as root."
       exit 0
fi

if [ $# -lt 2 ];then
	usage
fi

# Skip generating system image
skip_system_img=0
# Update bootloader only for update without layout change
update_bl_only=0
# Update rootfs only for update without layout change
update_rfs_only=0
rootfs_updater=""
rootfs_image=
external_device=
rootfs_size=
esp_image=
ext_num_sector=
flash_opts=
opstr+="brsu:v:i:o:f:p:S:E:T:-:"
while getopts "${opstr}" OPTION; do
	case $OPTION in
	u) PKC_KEY_FILE="${OPTARG}"; ;;
	v) SBK_KEY_FILE="${OPTARG}"; ;;
	i) ENC_RFS_KEY="${OPTARG}"; ;;
	s) skip_system_img=1; ;;
	b) update_bl_only=1; ;;
	r) update_rfs_only=1; ;;
	o) rootfs_updater="${OPTARG}"; ;;
	f) rootfs_image="${OPTARG}"; ;;
	p) flash_opts="${OPTARG}"; ;;
	S) rootfs_size="${OPTARG}"; ;;
	E) esp_image="${OPTARG}"; ;;
	T) ext_num_sector="${OPTARG}"; ;;
	-) case ${OPTARG} in
		external-device)
			external_device="${!OPTIND}";
			OPTIND=$((OPTIND + 1));
			;;
		*) usage; ;;
		esac;;
	*) usage; ;;
	esac;
done

if [ "${PKC_KEY_FILE}" != "" ] && [ ! -f "${PKC_KEY_FILE}" ];then
	echo "Specified PKC key file \"${PKC_KEY_FILE}\" is not found"
	usage
fi

if [ "${SBK_KEY_FILE}" != "" ]; then
	if [ ! -f "${SBK_KEY_FILE}" ];then
		echo "Specified SBK key file \"${SBK_KEY_FILE}\" is not found"
		usage
	else
		if [ "${PKC_KEY_FILE}" == "" ]; then
			echo "PKC key must be provided once SBK key is specified"
			usage
		fi
	fi
fi

if [ "${ENC_RFS_KEY}" != "" ] && [ ! -f "${ENC_RFS_KEY}" ];then
	echo "Specified disk encryption key file \"${ENC_RFS_KEY}\" is not found"
	usage
fi

if [ "${update_bl_only}" == 1 ] && [ "${update_rfs_only}" == 1 ]; then
	echo "The option -b is conflicted with option -r"
	usage
fi

if [ -n "${rootfs_updater}" ]; then
	rootfs_updater_name="$(basename "${rootfs_updater}")"
	if [ "${rootfs_updater_name}" == "${NV_ROOTFS_UPDATER}" ]; then
		echo "ERROR: the specified rootfs updater name ${rootfs_updater_name} is reserved by Nvidia"
		usage
	elif [ ! -r "${rootfs_updater}" ]; then
		echo "ERROR: the specified rootfs updater ${rootfs_updater} is not readable"
		usage
	else
		rootfs_updater="$(readlink -f "${rootfs_updater}")"
		if [ ! -x "${rootfs_updater}" ]; then
			chmod a+x "${rootfs_updater}"
		fi
		update_entry_in_customer_conf "ROOTFS_UPDATER" "${rootfs_updater_name}"
	fi
fi

if [ -n "${rootfs_image}" ]; then
	if [ ! -f "${rootfs_image}" ]; then
		echo "ERROR: the specified rootfs image ${rootfs_image} is not readable"
		usage
	else
		rootfs_image="$(readlink -f "${rootfs_image}")"
		# Make sure that the "${SYSTEM_IMAGE_RAW_FILE}" or
		# ${SYSTEM_IMAGE_RAW_ENC_FILE}is to be deleted if
		# it exists.
		skip_system_img=0
	fi
fi

# Support disk encryption
if [ "${ROOTFS_ENC}" == 1 ]; then
	echo "Disk encryption is enabled"
	# The utility cryptsetup is required to operate on the
	# encrypted raw system image.
	if ! which cryptsetup >/dev/null; then
		echo "The cryptsetup is not found, please run \"sudo apt-get install cryptsetup\" to install it"
		exit 1
	fi

	# The ENC_RFS_KEY specified by -i option is required for disk encryption
	if [ "${ENC_RFS_KEY}" == "" ]; then
		echo "Erorr: the key for disk encryption is not specified"
		exit 1
	fi
else
	ROOTFS_ENC=""
fi

# Support ROOTFS A/B
if [ "${ROOTFS_AB}" == 1 ]; then
	# If rootfs A/B is enabled, user needs to provide the
	# corresponding rootfs updater that can handle the
	# provided rootfs image.
	if [ -n "${rootfs_image}" ] && [ "${rootfs_updater}" == "" ]; then
		echo "Error: need to provide rootfs updater if rootfs A/B is enabled"
		usage
	fi

	# For rootfs A/B enabled case, kernel(A_kernel) and kernel_b(B_kernel)
	# partitions use different images with different cmdline, so "kernel_b"
	# and "B_kernel" are added.
	UNSIGNED_PARTS+=( "kernel_b" "B_kernel" )
else
	ROOTFS_AB=""
fi

if [ -z "${rootfs_image}" ] && \
	[ "${skip_system_img}" == 1 ] && \
	[[ ("${ROOTFS_ENC}" == "" && ! -f "${SYSTEM_IMAGE_RAW_FILE}") || \
	("${ROOTFS_ENC}" == 1 && ! -f "${SYSTEM_IMAGE_RAW_ENC_FILE}") ]]; then
	echo "The option -s can not be applied if the raw system image does not exist"
	usage
fi

if [ -n "${esp_image}" ] && [ ! -f "${esp_image}" ]; then
	echo "ERROR: the specified esp image ${esp_image} is not found"
	usage
fi

nargs=$#
BASE_VERSION="${!nargs}"
nargs=$((nargs-1))
TARGET_BOARD="${!nargs}"

# Check the input target board and base version
# If both of them are valid, return the matched board spec;
# Otherwise, report error and exit
BOARD_SPECS_CONFIG_FILE="${OTA_DIR}/ota_board_specs.conf"
if [ ! -f "${BOARD_SPECS_CONFIG_FILE}" ]; then
	echo "The board specs configuration file ${BOARD_SPECS_CONFIG_FILE} is not found"
	usage
fi
source "${OTA_DIR}/ota_validate_params.sh"
if ! ota_validate_params "${TARGET_BOARD}" "${BASE_VERSION}" "internal" "${BOARD_SPECS_CONFIG_FILE}" "BOARD_SPECS_ARRAY" "CHIPID"; then
	echo "Failed to run \"ota_validate_params ${TARGET_BOARD} ${BASE_VERSION} internal ${BOARD_SPECS_CONFIG_FILE} BOARD_SPECS_ARRAY CHIPID\""
	usage
fi

# Check whether sepcified external device is supported
if [ -n "${external_device}" ]; then
	check_external_device
else
	if [ "${TARGET_BOARD}" == "jetson-orin-nano-devkit" ]; then
		echo "The External device must be specified for ${TARGET_BOARD}"
		usage
	fi
fi

source "${OTA_DIR}/nv_ota_common.func"
source "${OTA_DIR}/nv_ota_common_utils.func"

# Check whether layout change exists based on the base version.
if is_layout_change_needed "${BASE_VERSION}"; then
	LAYOUT_CHANGE=1
	if [ "${update_bl_only}" == 1 ]; then
		echo "Error: the \"-b\" option can not be used in the update with layout change"
		usage
	fi
	if [ "${update_rfs_only}" == 1 ]; then
		echo "Error: the \"-r\" option can not be used in the update with layout change"
		usage
	fi

	# Copy files from base bsp for OTA with layout change
	copy_files_from_base_bsp

	# Update the layotus for external device on T194 devices
	if [ -n "${external_device}" ]; then
		update_external_layouts_t194
	fi
fi

init; echo;
generate_binaries; echo;

# Generate different package according to ${LAYOUT_CHANGE}
if [ "${LAYOUT_CHANGE}" == 0 ]; then
	generate_package_for_no_layout_change; echo;
else
	generate_package; echo;
fi
