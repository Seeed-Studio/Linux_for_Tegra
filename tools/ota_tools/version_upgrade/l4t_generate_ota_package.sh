#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2019-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
# R35.x/R36.x to R36-ToT.
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
OS_LOADER="BOOTAA64"
L4T_LAUNCHER="${BOOTLOADER_DIR}/${OS_LOADER}.efi"

# For rootfs A/B overlay package
ROOTFS_OVERLAY_PACKAGE_A="rootfs_overlay_a.tar.gz"
ROOTFS_OVERLAY_PACKAGE_B="rootfs_overlay_b.tar.gz"
ROOTFS_OVERLAY_FILES=( "/boot/extlinux" )

# Do not use underscore "_" in these dir name
TOT_IMAGES_DIR="images-R36-ToT"
TARGET_VERSION="R36-5"

COMMON_TAR_OPTIONS=( --warning=none --checkpoint=10000 --one-file-system --xattrs --xattrs-include=* )
PKC_KEY_FILE=""
SBK_KEY_FILE=""
BASE_VERSION=""
TARGET_BOARD=""
# Skip re-generating recovery image if it exists
SKIP_REC_IMG=0
BOARD_SPECS_ARRAY=""
CHIPID=""

# The kernel/kernel-dtb/recovery/recovery-dtb/esp images signing in current release
# are followed by UEFI secureboot policy.
UEFI_PAYLOAD_PARTS=( "A_kernel" "A_kernel-dtb" "recovery" "recovery-dtb" "esp" )

# Support generating OTA payload package for the case
# with/without partition layout change
LAYOUT_CHANGE=0
# Support for specifying the rootfs update script by user
NV_ROOTFS_UPDATER="nv_ota_rootfs_updater.sh"
# Customer configuration file for image-based OTA
OTA_CUSTOMER_CONF="${OTA_DIR}/nv_ota_customer.conf"
OTA_CUSTOMER_CONF_DEFAULT="${OTA_DIR}/nv_ota_customer.conf.default"

# Support NVMe on Jetson Orin NX/Nano
NVME_ROOTDEV="nvme0n1p1"
NVME_DTBO="BootOrderNvme.dtbo"
INTERNAL_ROOTDEV="internal"
SUPPORTED_EXTERNAL_DEVICES=(
	'jetson-agx-orin-devkit:nvme0n1'
	'jetson-agx-orin-devkit-industrial:nvme0n1'
	'jetson-orin-nano-devkit:nvme0n1'
	'recomputer-orin-j40mini:nvme0n1'
	'recomputer-orin-j401:nvme0n1'
	'recomputer-orin-super-j401:nvme0n1'
	'recomputer-industrial-orin-j401:nvme0n1'
	'reserver-industrial-orin-j401:nvme0n1'
	'reserver-agx-orin-j501x:nvme0n1'
	'reserver-agx-orin-j501x-gmsl:nvme0n1'
	'recomputer-orin:nvme0n1'
	'recomputer-orin-industrial:nvme0n1'
	'reserver-orin-industrial:nvme0n1'
	'jetson-orin-nano-devkit-super:nvme0n1'
	'jetson-orin-nano-devkit-super-maxn:nvme0n1'
)
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

# The files from base BSP that are to be used in recovery kernel
BASE_RECOVERY_DIR="base_recovery"
BASE_RECOVERY_FILES=(
	'bin/cpio:bin/cpio'
	'usr/bin/findmnt:bin/findmnt'
	'usr/bin/uname:bin/uname'
	'usr/bin/wc:bin/wc'
	'usr/sbin/mtd_debug:bin/mtd_debug'
	'usr/sbin/sfdisk:bin/sfdisk'
	'usr/sbin/sgdisk:bin/sgdisk'
	'usr/sbin/partprobe:bin/partprobe'
	'usr/lib/aarch64-linux-gnu/libfdisk.so.1:lib/aarch64-linux-gnu/libfdisk.so.1'
	'usr/lib/aarch64-linux-gnu/libpopt.so.0:lib/aarch64-linux-gnu/libpopt.so.0'
	'usr/lib/aarch64-linux-gnu/libstdc++.so.6:lib/aarch64-linux-gnu/libstdc++.so.6'
	'usr/lib/aarch64-linux-gnu/libgcc_s.so.1:lib/aarch64-linux-gnu/libgcc_s.so.1'
	'usr/lib/aarch64-linux-gnu/libparted.so.2:lib/aarch64-linux-gnu/libparted.so.2'
)

# UEFI secureboot
ROOTFS_DIR="${LINUX_BASE_DIR}/rootfs"
UEFI_BASE_TMP_DIR=
UEFI_BASE_PACKAGE_MULTI_SPECS="uefi_base_multi_specs.tar.gz"
UEFI_ENC=
UEFI_KEYS=
UEFI_OVERLAY_DIR="${BOOTLOADER_DIR}/uefi_overlay"
UEFI_SECUREBOOT_DIR=
UEFI_SECUREBOOT_OVERLAY_PACKAGE="uefi_secureboot_overlay.tar.gz"
UEFI_SECUREBOOT_SCRIPT="${OTA_DIR}/l4t_gen_uefi_sb_overlay.sh"
UEFI_SECUREBOOT_OVERLAY_PACKAGE_MULTI_SPECS="uefi_secureboot_overlay_multi_specs.tar.gz"

function usage()
{
	echo -ne "Usage: sudo $0 [options] <target board> <bsp version>\n"
	echo -ne "\tWhere,\n"
	echo -ne "\t\t<target board>: target board. Supported boards: jetson-agx-orin-devkit, jetson-agx-orin-devkit-industrial, jetson-orin-nano-devkit, jetson-orin-nano-devkit-super, jetson-orin-nano-devkit-super-maxn, recomputer-orin-j40mini, recomputer-orin-j401, recomputer-orin-super-j401, recomputer-industrial-orin-j401, reserver-industrial-orin-j401, reserver-agx-orin-j501x, reserver-agx-orin-j501x-gmsl.\n"
	echo -ne "\t\t<bsp version>: the version of the base BSP. Supported versions: R35-5, R35-6, R36-3, R36-4, R36-5.\n"
	echo -ne "\toptions:\n"
	echo -ne "\t\t-u <PKC key file>: PKC key used for odm fused board\n"
	echo -ne "\t\t-v <SBK key file>: Secure Boot Key (SBK) key used for ODM fused board\n"
	echo -ne "\t\t-i <enc rfs key file>: Key for disk encryption support\n"
	echo -ne "\t\t-s\tSkip generating system image.\n"
	echo -ne "\t\t-b\tUpdate bootloader only. Only valid if <bsp version> is R36.\n"
	echo -ne "\t\t-r\tUpdate rootfs only. Only valid if <bsp version> is R36.\n"
	echo -ne "\t\t-o\tSpecify the script to update rootfs partition.\n"
	echo -ne "\t\t-f\tSpecify the rootfs image to be written to rootfs partition.\n"
	echo -ne "\t\t-p\tSpecify the options directly passed into flash.sh when generating images.\n"
	echo -ne "\t\t--external-device <external device>: Specify the external device to be OTAed. Supported devices: nvme0n1.\n"
	echo -ne "\t\t  This option is valid for supported NVMe OTA boards.\n"
	echo -ne "\t\t-S <size>: Specify the size of rootfs partition on external device. Only valid when --external-device option is set. KiB, MiB, GiB short hands are allowed\n"
	echo -ne "\t\t  Ths size set through this option must be the same as the size of rootfs partition on the external device to be OTAed.\n"
	echo -ne "\t\t-E <esp image>: Specify the image to update ESP.\n"
	echo -ne "\t\t-T <ext num sectors>: Specify the number of the sectors of the external storage device.\n"
	echo -ne "\t\t--rootfs-uuid <rootfs UUID>: Specify UUID for rootfs tp be updated through OTA\n"
	echo -ne "\t\t--rootfs-b-uuid <rootfs B UUID>: Specify UUID for rootfs B on the device to be updated through OTA.\n"
	echo -ne "\t\t--uda-uuid <uda UUID>: Specify UUID for UDA on the device to be updated through OTA.\n"
	echo -ne "\t\t--uefi-keys <keys_conf>: Specify UEFI keys configuration file.\n"
	echo -ne "\t\t--uefi-enc <UEFI_ENC_key>: Key file (0x19: 16-byte; 0x23: 32-byte) to encrypt UEFI payloads.\n"
	echo -ne "Example:\n"
	echo -ne "\t1. Upgrade from R35.6 to R36 ToT on Jetson AGX Orin Devkit\n"
	echo -ne "\tsudo $0 jetson-agx-orin-devkit R35-6\n"
	echo -ne "\t2. Upgrade from R35.6 to R36 ToT on Jetson Orin NX/Nano with NVMe device that has 24GiB APP partition\n"
	echo -ne "\tsudo $0 --external-device nvme0n1 -S 24GiB jetson-orin-nano-devkit R35-6\n"
	echo -ne "\t3. Upgrade from R35.6 to R36 ToT on Jetson Orin NX/Nano with NVMe device that has 24GiB APP partition and with rootfs A/B enabled\n"
	echo -ne "\tsudo ROOTFS_AB=1 $0 --external-device nvme0n1 -S 24GiB jetson-orin-nano-devkit R35-6\n"
	exit 1
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

	# For jetson-agx-orin-devkit, set the boardver to null if boardsku is 0004,
	# or 0005.
	# For jetson-orin-nano-devkit, jetson-orin-nano-devkit-super,
	# jetson-orin-nano-devkit-super-maxn, and the supported third-party
	# Orin NX/Nano carrier boards, set the boardver to null if boardsku
	# is 0001, 0003, 0004 or 0005.
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
		'recomputer-orin-j40mini:0001'
		'recomputer-orin-j40mini:0003'
		'recomputer-orin-j40mini:0004'
		'recomputer-orin-j40mini:0005'
		'recomputer-orin-j401:0001'
		'recomputer-orin-j401:0003'
		'recomputer-orin-j401:0004'
		'recomputer-orin-j401:0005'
		'recomputer-orin-super-j401:0001'
		'recomputer-orin-super-j401:0003'
		'recomputer-orin-super-j401:0004'
		'recomputer-orin-super-j401:0005'
		'recomputer-industrial-orin-j401:0001'
		'recomputer-industrial-orin-j401:0003'
		'recomputer-industrial-orin-j401:0004'
		'recomputer-industrial-orin-j401:0005'
		'reserver-industrial-orin-j401:0001'
		'reserver-industrial-orin-j401:0003'
		'reserver-industrial-orin-j401:0004'
		'reserver-industrial-orin-j401:0005'
		'reserver-agx-orin-j501x:0004'
		'reserver-agx-orin-j501x:0005'
		'reserver-agx-orin-j501x-gmsl:0004'
		'reserver-agx-orin-j501x-gmsl:0005'
		'recomputer-orin:0001'
		'recomputer-orin:0003'
		'recomputer-orin:0004'
		'recomputer-orin:0005'
		'recomputer-orin-industrial:0001'
		'recomputer-orin-industrial:0003'
		'recomputer-orin-industrial:0004'
		'recomputer-orin-industrial:0005'
		'reserver-orin-industrial:0001'
		'reserver-orin-industrial:0003'
		'reserver-orin-industrial:0004'
		'reserver-orin-industrial:0005'
		'jetson-orin-nano-devkit-super:0001'
		'jetson-orin-nano-devkit-super:0003'
		'jetson-orin-nano-devkit-super:0004'
		'jetson-orin-nano-devkit-super:0005'
		'jetson-orin-nano-devkit-super-maxn:0001'
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

function package_images()
{
	local dest_dir="$1"
	local src_dir="${BOOTLOADER_DIR}"
	local idx_file=

	# Locate the "flash.idx"
	if [ -s "${SBK_KEY_FILE}" ]; then
		echo "encrypted_signed_dir=${dest_dir}"
		idx_file="${src_dir}/enc_signed/flash.idx"
	else
		echo "signed_dir=${dest_dir}"
		idx_file="${src_dir}/signed/flash.idx"
	fi

	local partition=
	for partition in "${UEFI_PAYLOAD_PARTS[@]}"
	do
		file_name="$(grep ":${partition}," "${idx_file}" | cut -d, -f 5 | sed 's/^ //' -)"
		if [ "${file_name}" == "" ]; then
			continue
		fi
		if [ ! -f "${src_dir}/${file_name}" ]; then
			echo "Error: ${src_dir}/${file_name} is not found"
			return 1
		fi

		# Copy images to "dest_dir".
		cp "${src_dir}/${file_name}" "${dest_dir}"/
	done

	# Copy images to boot partition
	copy_images_to_boot "${src_dir}" "${dest_dir}"

	# Copy the flash.idx file
	cp "${idx_file}" "${dest_dir}/"
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
	images_dir="${OTA_BASE_DIR_TMP}/${dev}/${TOT_IMAGES_DIR}"

	# The NVMe partition layout may contain ESP partitions without a filename.
	# In that case there is no ESP payload to replace.  Do not require an image
	# name unless the caller explicitly requests an ESP update with -E.
	if [ -z "${esp_image}" ]; then
		echo "No custom ESP image is specified; preserve the existing ESP"
		return 0
	fi

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

# Verify that every generated board specification has its flash index before
# packaging.  A payload without this file can pass host-side compression but
# cannot select recovery/bootloader images on the OTA target.
function validate_generated_images()
{
	local dev=
	local images_dir=
	local board_specs=
	local spec=
	local board_spec_name=

	if [ -n "${external_device}" ]; then
		dev="${EXT_DEV}"
	else
		dev="${INT_DEV}"
	fi
	images_dir="${OTA_BASE_DIR_TMP}/${dev}/${TOT_IMAGES_DIR}"
	board_specs="${!BOARD_SPECS_ARRAY}[@]"

	for spec in "${!board_specs}"; do
		eval "${spec}"
		if [ "${rootdev}" != "${INTERNAL_ROOTDEV}" ] && [ -z "${external_device}" ]; then
			continue
		fi
		construct_board_spec_name "board_spec_name"
		if [ ! -f "${images_dir}/${board_spec_name}/flash.idx" ]; then
			echo "ERROR: missing OTA image index for board spec ${board_spec_name}"
			return 1
		fi
	done
	return 0
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
	local ext_cfg_file_with_sector_count=

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
		if [ "${rootdev}" != "${INTERNAL_ROOTDEV}" ] && [ "${external_device}" == "" ]; then
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
		if [ "${UEFI_KEYS}" != "" ] && [ -f "${UEFI_KEYS}" ]; then
			cmd_arg+="--uefi-keys \"${UEFI_KEYS}\" "
		fi
		if [ "${UEFI_ENC}" != "" ] && [ -f "${UEFI_ENC}" ]; then
			cmd_arg+="--uefi-enc \"${UEFI_ENC}\" "
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
				# The non-A/B, non-encrypted T234 NVMe layout used by some
				# BSPs hard-codes num_sectors.  flash.sh can apply -T only when
				# the layout contains the EXT_NUM_SECTORS token.  Keep the
				# original layout unchanged for normal flashing, but make a
				# per-OTA layout when the caller explicitly supplied -T.
				if [ -n "${ext_num_sector}" ]; then
					ext_cfg_file_with_sector_count="${OTA_BASE_DIR_TMP}/$(basename "${ext_cfg_file%.xml}")_ota.xml"
					if ! sed -E 's/(<device type="external"[^>]*num_sectors=")[^"]*("[^>]*>)/\1EXT_NUM_SECTORS\2/' \
						"${ext_cfg_file}" > "${ext_cfg_file_with_sector_count}"; then
						echo "Failed to create the external-device OTA partition layout"
						exit 1
					fi
					ext_cfg_file="${ext_cfg_file_with_sector_count}"
				fi
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

		# Generate binaries
		echo -e "${cmd}\r\n"
		if eval "${cmd}"; then
			# Create destination directory
			if [ "${rootdev}" == "${INTERNAL_ROOTDEV}" ]; then
				dest_dir="${OTA_BASE_DIR_TMP}/${INT_DEV}/${signed_img_dir}/${board_spec_name}"
			else
				dest_dir="${OTA_BASE_DIR_TMP}/${EXT_DEV}/${signed_img_dir}/${board_spec_name}"
			fi
			mkdir -p "${dest_dir}"

			# Copy the UEFI payload files into the destination directory, including
			# kernel image, kernel-dtb image, recovery image, recovery-dtb image
			# and esp image.
			if ! package_images "${dest_dir}"; then
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
				if [ "${rootdev}" == "${INTERNAL_ROOTDEV}" ]; then
					build_rootfs_image_and_overlay_packages "${INT_DEV}"
				else
					build_rootfs_image_and_overlay_packages "${EXT_DEV}"
				fi
			fi

			# Build UEFI secureboot package
			if [ "${UEFI_KEYS}" != "" ] && [ "${update_bl_only}" == 0 ]; then
				build_uefi_secureboot_package "${dest_dir}"
			fi
		fi
	done

	# Remove unneeded images
	remove_unneeded_images

	# Use user provided ESP image if specified
	handle_esp_image
	if ! validate_generated_images; then
		echo "Failed to validate generated OTA images"
		exit 1
	fi
	echo -e "${ret_msg}"
	return 0
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

function clean_up_tmp_files()
{
	rm -rf "${OTA_BASE_DIR_TMP}"
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
	local version_file_orig="qspi_bootblob_ver.txt"
	local version_file="version.txt"
	cp "${BOOTLOADER_DIR}/${version_file_orig}" "${OTA_BASE_DIR_TMP}/${version_file}"
	cp "${LINUX_BASE_DIR}/rootfs/etc/user_release_version" "${OTA_BASE_DIR_TMP}/user_release_version"

	# Copy L4T Launcher
	if [ "${UEFI_KEYS}" != "" ]; then
		cp "${L4T_LAUNCHER}.signed" "${OTA_BASE_DIR_TMP}/${OS_LOADER}.efi"
	else
		cp "${L4T_LAUNCHER}" "${OTA_BASE_DIR_TMP}/${OS_LOADER}.efi"
	fi

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
		# For Jetson Orin Nano variants and the supported third-party NVMe
		# boards, keep all board spec entries as they only include entries
		# for external device. For other devices, only keep the board spec
		# entry for internal device.
		if [[ "${item}" =~ jetson-orin-nano-devkit* ]] \
			|| [[ "${item}" =~ board=re ]] \
			|| [[ "${item}" =~ internal ]]; then
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

	local env_var="ROOTFS_AB=${ROOTFS_AB} ROOTFS_ENC= "
	if [ "${UEFI_KEYS}" != "" ]; then
		env_var+="ADDITIONAL_DTB_OVERLAY=\"UefiDefaultSecurityKeys.dtbo\" "
	fi
	cmd="${env_var} ${bup_generator} -f ${__board_spec_file} -e ${__board_spec_entry} -b ${board_name} "
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

# Remove unneeded images in "images-R36-ToT" directory
function remove_unneeded_images()
{
	# Delete all the images for non-active storage device
	if [ -n "${external_device}" ]; then
		rm -rf "${OTA_BASE_DIR_TMP}/${INT_DEV}/"
	else
		rm -rf "${OTA_BASE_DIR_TMP}/${EXT_DEV}/"
	fi
	return 0
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

# Build supporting bin/lib that are used in base recovery kernel from BASE_BSP
function build_bin_lib_for_base_recovery()
{
	local base_recovery_dir="${OTA_BASE_DIR_TMP}/${BASE_RECOVERY_DIR}"
	local base_rootfs=
	local src_file=
	local dst_file=
	local item=
	local tmp_str=

	if [[ ! "${BASE_VERSION}" =~ R35 ]]; then
		return 0
	fi

	echo "Building bin/lib from base BSP(${BASE_BSP})"
	base_rootfs="${BASE_BSP}/rootfs"
	mkdir -p "${base_recovery_dir}"
	pushd "${base_recovery_dir}" > /dev/null 2>&1
	for item in "${BASE_RECOVERY_FILES[@]}"
	do
		# Check whether source file exists
		tmp_str="$(echo "${item}" | cut -d: -f 1)"
		src_file="${base_rootfs}/${tmp_str}"
		if [ ! -f "${src_file}" ]; then
			echo "The file ${src_file} is not found"
			exit 1
		fi

		# Create parent directoris for destination file
		dst_file="$(echo "${item}" | cut -d: -f 2)"
		tmp_str="${dst_file%/*}"
		if [ ! -d "${tmp_str}" ]; then
			mkdir -p "${tmp_str}"
		fi

		# Copy file
		cp -fv "${src_file}" "${dst_file}"
	done
	popd > /dev/null 2>&1
}

# Copy extlinux.conf and initrd into ${UEFI_SECUREBOOT_DIR}
prepare_uefi_secureboot_binaries()
{
	local image=

	if [ "${ROOTFS_ENC}" == 1 ]; then
		# Use system_boot.img.raw"
		image="${BOOT_IMAGE_RAW_FILE}"
	else
		# Use system.img.raw"
		image="${SYSTEM_IMAGE_RAW_FILE}"
	fi
	mkdir -p "${SYSTEM_IMAGE_TMP}"
	# Mount system.img.raw or system_boot.img.raw
	if ! mount "${image}" "${SYSTEM_IMAGE_TMP}"; then
		echo "Failed to run \"mount_rootfs ${image} ${SYSTEM_IMAGE_TMP}\""
		rm "${SYSTEM_IMAGE_TMP}" -rf
		exit 1
	fi
	# Extract extlinux.conf to ${UEFI_SECUREBOOT_DIR}
	# The extlinux.conf in the system.img.raw and system_boot.img.raw
	# is updated, so extract the extlinux from them.
	cp -vf "${SYSTEM_IMAGE_TMP}/boot/extlinux/extlinux.conf" "${UEFI_SECUREBOOT_DIR}"/

	# Get the name of the kernel DTB from ${SYSTEM_IMAGE_TMP}/boot/dtb/.
	local kernel_dtb=
	kernel_dtb="$(ls -al "${SYSTEM_IMAGE_TMP}"/boot/dtb/ | grep -o "kernel_.*\.dtb$")"

	# Try unmounting rootfs until it is successful or timeout
	local umount_timeout=60
	local sleep_interval=3
	while true
	do
		if ! umount "${SYSTEM_IMAGE_TMP}"; then
			sleep "${sleep_interval}"
			umount_timeout=$((umount_timeout - sleep_interval))
			if [ "${umount_timeout}" -eq 0 ]; then
				echo "Failed to umount ${SYSTEM_IMAGE_TMP}"
				exit 1
			fi
		else
			break
		fi
	done
	rm -rf "${SYSTEM_IMAGE_TMP}"

	# Extract initrd to "${UEFI_SECUREBOOT_DIR}"
	# If --uefi-enc is specified, the intrd in the system.img.raw or
	# system_boot.img.raw is encrypted, so extract the initrd from "${ROOTFS_DIR}".
	cp -vf "${ROOTFS_DIR}/boot/initrd" "${UEFI_SECUREBOOT_DIR}"/

	# Extract kernel DTB to "${UEFI_SECUREBOOT_DIR}"
	# If --uefi-enc is specified, the kernel DTB in the system.img.raw or
	# system_boot.img.raw is encrypted, so extract the kernel DTB from "${ROOTFS_DIR}".
	if [ "${kernel_dtb}" == "" ]; then
		echo "No kernel DTB is found at /boot/dtb/"
		exit 1
	else
		cp "${ROOTFS_DIR}/boot/${kernel_dtb}" "${UEFI_SECUREBOOT_DIR}"/
	fi
}

prepare_uefi_signing_base()
{
	# Copy the required and unsigned files from
	# UEFI_SECUREBOOT_DIR to UEFI_BASE_TMP_DIR
	local dest_dir="${1}"
	local dest_name=
	dest_name="$(basename "${dest_dir}")"
	echo "Copy files from ${UEFI_SECUREBOOT_DIR} into ${UEFI_BASE_TMP_DIR}"
	UEFI_BASE_TMP_DIR="${OTA_BASE_DIR_TMP}/uefi_base"
	mkdir -p "${UEFI_BASE_TMP_DIR}/${dest_name}"
	cp -vf "${UEFI_SECUREBOOT_DIR}"/* "${UEFI_BASE_TMP_DIR}/${dest_name}"/
}

# Build uefi secureboot package
build_uefi_secureboot_package()
{
	local dest_dir="${1}"
	local cmd="${UEFI_SECUREBOOT_SCRIPT} "
	local initrd_file=
	local extlinux_conf=
	local kernel_dtb=

	UEFI_SECUREBOOT_DIR="${OTA_BASE_DIR_TMP}/uefi_secureboot"
	mkdir -p "${UEFI_SECUREBOOT_DIR}"

	# Copy required files into "${UEFI_SECUREBOOT_DIR}"
	if ! prepare_uefi_secureboot_binaries; then
		echo "Failed to copy files into ${UEFI_SECUREBOOT_DIR}"
		exit 1
	fi
	initrd_file="${UEFI_SECUREBOOT_DIR}/initrd"
	if [ ! -f "${initrd_file}" ]; then
		echo "The ${initrd_file} is not found"
		exit 1
	fi
	extlinux_conf="${UEFI_SECUREBOOT_DIR}/extlinux.conf"
	if [ ! -f "${extlinux_conf}" ]; then
		echo "The ${extlinux_conf} is not found"
		exit 1
	fi
	kernel_dtb="$(ls -al "${UEFI_SECUREBOOT_DIR}/" |grep -o "kernel_.*\.dtb$")"
	if [ "${kernel_dtb}" == "" ]; then
		echo "No kernel DTB is found in ${UEFI_SECUREBOOT_DIR}"
		exit 1
	fi

	# Create sub-directory with the same name as "${dest_dir}
	# in the "${UEFI_BASE_TMP_DIR}" and then copy exlinux.conf, initrd
	# and kernel DTB into it
	prepare_uefi_signing_base "${dest_dir}"

	cmd+="--uefi-keys \"${UEFI_KEYS}\" "
	if [ "${UEFI_ENC}" != "" ]; then
		cmd+="--uefi-enc \"${UEFI_ENC}\" --chipid ${CHIPID} "
	fi
	if [ "${uefi_rootfs_b_uuid}" != "" ]; then
		cmd+="--rootfs-b-uuid ${uefi_rootfs_b_uuid} "
	fi
	if [ "${uefi_uda_uuid}" != "" ]; then
		cmd+="--uda-uuid ${uefi_uda_uuid} "
	fi
	cmd+="--kernel-dtb ${UEFI_SECUREBOOT_DIR}/${kernel_dtb} "
	cmd+="${initrd_file} ${extlinux_conf} ${uefi_rootfs_uuid}"

	# Build uefi_secureboot_overlay.tar.gz
	echo "Building UEFI secureboot package in ${UEFI_SECUREBOOT_DIR}"
	pushd "${LINUX_BASE_DIR}" > /dev/null 2>&1
	echo "${cmd}"
	if ! eval "${cmd}"; then
		echo "Failed to build UEFI secureboot package"
		exit 1
	fi
	popd > /dev/null 2>&1
	rm -rf "${UEFI_SECUREBOOT_DIR}"

	# Copy bootloader/uefi_overlay/uefi_secureboot_overlay.tar.gz into ${dest_dir}
	echo "Copy ${UEFI_OVERLAY_DIR}/${UEFI_SECUREBOOT_OVERLAY_PACKAGE} into ${dest_dir}"
	cp -fv "${UEFI_OVERLAY_DIR}/${UEFI_SECUREBOOT_OVERLAY_PACKAGE}" "${dest_dir}"/
}

create_uefi_sb_packge_for_multi_specs()
{
	# Create UEFI secureboot overlay package for multiple board specs
	# This package is to be put under the Linux_for_Tegra/bootloader/uefi_overlay
	# uefi_secureboot_overlay_package_multi_specs.tar.gz
	# |-- compatible spec 1
	#     |-- uefi_secureboot_overlay.tar.gz
	# |-- ...
	# |-- compatible spec N
	#     |-- uefi_secureboot_overlay.tar.gz
	# |-- uuids_list.txt
	local uefi_sb_tmp_dir=
	local images_dir=
	uefi_sb_tmp_dir="$(mktemp -d)"
	if [ ! -d "${uefi_sb_tmp_dir}" ]; then
		echo "Failed to create temporary directory"
		exit 1
	fi
	if [ -n "${external_device}" ]; then
		images_dir="${OTA_BASE_DIR_TMP}/${EXT_DEV}/${TOT_IMAGES_DIR}"
	else
		images_dir="${OTA_BASE_DIR_TMP}/${INT_DEV}/${TOT_IMAGES_DIR}"
	fi

	# Move the "uefi_secureboot_ovelay.tar.gz" for each
	# board spec to the "${uefi_sb_tmp_dir}"
	pushd "${images_dir}" >/dev/null 2>&1
	for sub_dir in *
	do
		# Create board spec directory in "${uefi_sb_tmp_dir}"
		# and then move the "uefi_secureboot_overlay.tar.gz" into it
		if [ -d "${sub_dir}" ]; then
			mkdir -p "${uefi_sb_tmp_dir}/${sub_dir}"
			mv "${images_dir}/${sub_dir}/${UEFI_SECUREBOOT_OVERLAY_PACKAGE}" "${uefi_sb_tmp_dir}/${sub_dir}"/
		fi
	done
	popd >/dev/null 2>&1

	# Add "uuids_list.txt" file to list the UUIDs used in
	# the UEFI secureboot overlay package
	local uuids_list_file="uuids_list.txt"
	echo "rootfs_uuid:${uefi_rootfs_uuid}" > "${uefi_sb_tmp_dir}/${uuids_list_file}"
	if [ "${uefi_rootfs_b_uuid}" != "" ]; then
		echo "rootfs_b_uuid:${uefi_rootfs_b_uuid}" >> "${uefi_sb_tmp_dir}/${uuids_list_file}"
	fi
	if [ "${uefi_uda_uuid}" != "" ]; then
		echo "uda_uuid:${uefi_uda_uuid}" >> "${uefi_sb_tmp_dir}/${uuids_list_file}"
	fi

	# Create the "uefi_secureboot_overlay_package_multi_specs.tar.gz"
	local uefi_sb_multi_specs_pack="${UEFI_OVERLAY_DIR}/${UEFI_SECUREBOOT_OVERLAY_PACKAGE_MULTI_SPECS}"
	mkdir -p "${UEFI_OVERLAY_DIR}"
	pushd "${uefi_sb_tmp_dir}" >/dev/null 2>&1
	if ! tar czpf "${uefi_sb_multi_specs_pack}" ./* >/dev/null 2>&1; then
		echo "Failed to generate ${UEFI_SECUREBOOT_OVERLAY_PACKAGE_MULTI_SPECS}"
		popd >/dev/null 2>&1
		rm -rf "${uefi_sb_tmp_dir}"
		exit 1
	fi
	popd >/dev/null 2>&1
	rm -rf "${uefi_sb_tmp_dir}"
	echo -e "\nGenerated UEFI secureboot overlay package for multiple board specs at \"${uefi_sb_multi_specs_pack}\""

	return 0
}

create_uefi_signing_base_for_multi_specs()
{
	# Create UEFI base package for multiple board specs
	# This package is to be put under the Linux_for_Tegra/bootloader/uefi_overlay
	# uefi_base_multi_specs.tar.gz
	# |-- compatible spec 1
	#     |-- extlinux.conf
	#     |-- initrd
	#     |-- kernel dtb
	# |-- ...
	# |-- compatible spec N
	#     |-- extlinux.conf
	#     |-- initrd
	#     |-- kernel dtb
	local uefi_base_package="${UEFI_OVERLAY_DIR}/${UEFI_BASE_PACKAGE_MULTI_SPECS}"

	if [ "${UEFI_BASE_TMP_DIR}" == "" ] \
		|| [ ! -d "${UEFI_BASE_TMP_DIR}" ]; then
		echo "The ${UEFI_BASE_TMP_DIR} is not ready for building UEFI base package"
		exit 1
	fi
	echo "Building UEFI base package"
	mkdir -p "${UEFI_OVERLAY_DIR}"
	pushd "${UEFI_BASE_TMP_DIR}" > /dev/null 2>&1
	if ! tar czpf "${uefi_base_package}" ./* --remove-files; then
		echo "Failed to create ${UEFI_BASE_PACKAGE_MULTI_SPECS}"
		exit 1
	fi
	popd > /dev/null 2>&1
	rm -rf "${UEFI_BASE_TMP_DIR}"
	echo -e "\nGenerated UEFI base package for multiple board specs at \"${uefi_base_package}\""
}

# Generate OTA payload package
function generate_package()
{
	# OTA payload package for R35.x/R36.x to R36-ToT:
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
	# |-- nv_ota_update_alt_part.func
	# |-- nv_ota_update_rootfs_in_recovery.sh
	# |-- ota_nv_boot_control.conf
	# |-- ota_package.tar.sha1sum
	# |-- ota_package.tar
	#     |-- external_device
	#         |-- images-R36-ToT
	#             |-- compatible spec 1
	#                 |-- ...
	#             |-- ...
	#             |-- compatible spec N
	#                 |-- ...
	#         |-- rootfs_overlay_a.tar.gz (if rootfs A/B enabled)
	#         |-- rootfs_overlay_b.tar.gz (if rootfs A/B enabled)
	#         |-- system.img
	#         |-- system.img.sha1sum
	#     |-- internal_device
	#         |-- images-R36-ToT
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

	# Create UEFI secureboot overlay package for multiple board specs
	if [ "${UEFI_KEYS}" != "" ] && [ "${update_bl_only}" == 0 ]; then
		create_uefi_sb_packge_for_multi_specs
		create_uefi_signing_base_for_multi_specs
	fi

	# Generate "ota_package.tar"
	pushd "${OTA_BASE_DIR_TMP}" > /dev/null 2>&1
	tar cvf "${OTA_PACKAGE_FILE}" ./* --remove-files
	sha1_chksum="$(sha1sum ${OTA_PACKAGE_FILE} | cut -d\  -f 1)"
	echo -n "${sha1_chksum}" > "${OTA_PACKAGE_SHA1_FILE}"

	# Copy common images
	copy_common_images

	# Copy ota tasks listed in customer config file
	copy_ota_tasks

	# Generate BUP
	generate_BUP

	# Generate UEFI capsule
	generate_uefi_capsule

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

	# echo "LAYOUT_CHANGE" to layout_change file
	echo -n "${LAYOUT_CHANGE}" >"${OTA_BASE_DIR_TMP}/layout_change"

	# Copy nv-l4t-bootloader-config.sh and ota_nv_boot_control.conf
	copy_nv_bootloader_config_files

	# Copy nv_ota_common.func
	cp "${OTA_DIR}/nv_ota_common.func" "${OTA_BASE_DIR_TMP}"/

	# Copy nv_ota_update_alt_part.func
	cp "${OTA_DIR}/nv_ota_update_alt_part.func" "${OTA_BASE_DIR_TMP}"/

	# Build supporting bin/lib for the base recovery kernel
	# These bin/lib will be extracted once booting into the base recovery kernel
	build_bin_lib_for_base_recovery

	# Generate ota_payload_package.tar.gz
	tar zcvf "${OTA_PAYLOAD_PACKAGE_ZIP_FILE}" ./* --remove-files
	mkdir -p "${BOOTLOADER_DIR}/${TARGET_BOARD}"
	mv "${OTA_PAYLOAD_PACKAGE_ZIP_FILE}" "${BOOTLOADER_DIR}/${TARGET_BOARD}/"
	popd > /dev/null 2>&1

	# Clean up
	clean_up_tmp_files

	echo -e "\r\nSUCCESS: generate OTA package at \"${BOOTLOADER_DIR}/${TARGET_BOARD}/${OTA_PAYLOAD_PACKAGE_ZIP_FILE}\""
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
# Update bootloader only
update_bl_only=0
# Update rootfs only
update_rfs_only=0
rootfs_updater=""
rootfs_image=
external_device=
rootfs_size=
esp_image=
ext_num_sector=
flash_opts=
uefi_rootfs_uuid=
uefi_rootfs_b_uuid=
uefi_uda_uuid=
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
		rootfs-uuid)
			uefi_rootfs_uuid="${!OPTIND}";
			OPTIND=$((OPTIND + 1));
			;;
		rootfs-b-uuid)
			uefi_rootfs_b_uuid="${!OPTIND}";
			OPTIND=$((OPTIND + 1));
			;;
		uda-uuid)
			uefi_uda_uuid="${!OPTIND}";
			OPTIND=$((OPTIND + 1));
			;;
		uefi-keys)
			UEFI_KEYS="${!OPTIND}";
			OPTIND=$((OPTIND + 1));
			;;
		uefi-enc)
			UEFI_ENC="${!OPTIND}";
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

	# For rootfs A/B enabled case, A_kernel and B_kernel partitions
	# use different images with different cmdline, so "B_kernel" is added.
	UEFI_PAYLOAD_PARTS+=( "B_kernel" )
else
	ROOTFS_AB=""
fi

if [ -z "${rootfs_image}" ] && \
	[ "${skip_system_img}" == 1 ] && \
	[[ ("${ROOTFS_ENC}" == "" && ! -f "${SYSTEM_IMAGE_RAW_FILE}") || \
	("${ROOTFS_ENC}" == 1 && ! -f "${SYSTEM_IMAGE_RAW_ENC_FILE}" && "${external_device}" == "") || \
	("${ROOTFS_ENC}" == 1 && ! -f "${SYSTEM_IMAGE_EXT_RAW_ENC_FILE}" && "${external_device}" != "") ]]; then
	echo "The option -s can not be applied if the raw system image does not exist"
	usage
fi

if [ -n "${esp_image}" ] && [ ! -f "${esp_image}" ]; then
	echo "ERROR: the specified esp image ${esp_image} is not found"
	usage
fi

# Check UEFI keys
if [ "${UEFI_KEYS}" != "" ] && [ ! -f "${UEFI_KEYS}" ];then
	echo "Specified UEFI keys conf \"${UEFI_KEYS}\" is not found"
	usage
fi
if [ "${UEFI_ENC}" != "" ]; then
	if [ ! -f "${UEFI_ENC}" ];then
		echo "Specified UEFI encryption key \"${UEFI_ENC}\" is not found"
		usage
	fi
	if [ "${UEFI_KEYS}" == "" ]; then
		echo "UEFI keys conf must be provided once UEFI encryption key is specified"
		usage
	fi
fi
# The --rootfs-uuid must be specified if UEFI_KEYS is specified and not updating bootloader only
# The --rootfs-b-uuid must be specified if UEFI_KEYS is specified, ROOTFS_AB is set to 1 and
# not updating bootloader only
if [ "${UEFI_KEYS}" != "" ] && [ "${update_bl_only}" == 0 ]; then
	if [ "${uefi_rootfs_uuid}" == "" ]; then
		echo "The --rootfs-uuid must be specified if --uefi-keys is not NULL"
		usage
	fi
	if [ "${ROOTFS_AB}" == 1 ] && [ "${uefi_rootfs_b_uuid}" == "" ]; then
		echo "The --rootfs-b-uuid must be specified if --uefi-keys is not NULL and ROOTFS_AB is set to 1"
		usage
	fi
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
	# The supported NVMe OTA boards require the external device to be specified.
	if [[ "${TARGET_BOARD}" =~ jetson-orin-nano-devkit* ]] \
		|| [[ "${TARGET_BOARD}" =~ ^re ]]; then
		echo "The External device must be specified for ${TARGET_BOARD}"
		usage
	fi
fi

# Check whether BASE_BSP is set if BASE_VERSION is R35.x
if [[ "${BASE_VERSION}" =~ R35 ]] && [ -z "${BASE_BSP}" ]; then
	echo "Error: \"BASE_BSP\" is not set"
	exit 1
fi

# Update bootloader or rootfs only is only valid when BASE_VERSION is R36
base_version_branch="$(echo "${BASE_VERSION}" | cut -d- -f 1)"
target_version_branch="$(echo "${TARGET_VERSION}" | cut -d- -f 1)"
if [[ "${update_bl_only}" == 1 || "${update_rfs_only}" == 1 ]] \
	&& [[ "${base_version_branch}" != "${target_version_branch}" ]]; then
	echo "Update bootloader or rootfs only is valid only if the version branch does not change."
	usage
fi

source "${OTA_DIR}/nv_ota_common.func"
source "${OTA_DIR}/nv_ota_common_utils.func"

# Check whether layout change in user storage device exists based on
# the base version, such as eMMC or NVMe.
# Layout change in QSPI device is handled by UEFI Capsule update, no
# need to handle it here.
if is_layout_changed_in_user_storage_device "${BASE_VERSION}"; then
	LAYOUT_CHANGE=1
fi

init; echo;
generate_binaries; echo;
generate_package; echo;
