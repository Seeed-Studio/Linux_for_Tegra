#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

# This is a script to sign and/or encrypt the files in the UEFI base package
# for multi board specs and then pack the signed and/or encrypted files into
# UEFI secureboot overlay package for multi board specs.
#
# Usage:
#    sudo ./l4t_ota_sign_enc_uefi_base.sh [options] \
#        --uefi-keys <keys_conf> \
#        --rootfs-uuid <rootfs_uuid>
#
set -e
LINUX_BASE_DIR="$(pwd)"
OTA_TOOLS_DIR="${LINUX_BASE_DIR}/tools/ota_tools/version_upgrade"
GEN_SB_OVERLAY_PACKAGE_SCRIPT="${OTA_TOOLS_DIR}/l4t_gen_uefi_sb_overlay.sh"
SIGN_IMAGE_SCRIPT="${LINUX_BASE_DIR}/l4t_sign_image.sh"
SIGN_UEFI_IMAGE_SCRIPT="${LINUX_BASE_DIR}/l4t_uefi_sign_image.sh"
UEFI_SB_OVERLAY_PACKAGE="uefi_secureboot_overlay.tar.gz"
UEFI_SB_OVERLAY_PACKAGE_MULTI_SPECS="uefi_secureboot_overlay_multi_specs.tar.gz"
UEFI_OVERLAY_DIR="${LINUX_BASE_DIR}/bootloader/uefi_overlay"
DEFAULT_UEFI_BASE_PACKAGE="${UEFI_OVERLAY_DIR}/uefi_base_multi_specs.tar.gz"

function usage()
{
	echo -ne "Usage: sudo $0 [options] --uefi-keys <keys_conf> --rootfs-uuid <rootfs_uuid>\n"
	echo -ne "\tWhere,\n"
	echo -ne "\t\t<keys_conf>: Specify UEFI keys configuration file.\n"
	echo -ne "\t\t<rootfs_uuid>: Specify UUID for rootfs partition on the device to be updated.\n"
	echo -ne "\t\t<uefi_base_multi_specs>: Specify UEFI base package for multi board specs.\n"
	echo -ne "\toptions:\n"
	echo -ne "\t\t--rootfs-b-uuid <rootfs B UUID>: Specify UUID for rootfs B partition on the device to be updated.\n"
	echo -ne "\t\t--uda-uuid <uda UUID>: Specify UUID for UDA partition on the device to be updated.\n"
	echo -ne "\t\t--uefi-enc <uefi_enc_key>: Key file (0x19: 16-byte; 0x23: 32-byte) to encrypt UEFI payloads.\n"
	echo -ne "\t\t--chipid <chip id>: Specify chip id. This option must be specified if --uefi-enc is specified. \"0x23\" for T234 devices, and \"0x19\" for T194 devices.\n"
	echo -ne "\t\t--uefi-signing-base <uefi_base_package>: Specify UEFI base package including the files to be signed and/or encrypted. If this option is not specified, use \"Linux_for_Tegra/bootloader/uefi_overlay/uefi_base_multi_specs.tar.gz\" in default\n"
	echo -ne "\t\t--extlinux-conf <extlinux.conf>: Specify the extlinux.conf that is used to replace the one in the <uefi_base_multi_specs_package>.\n"
	echo -ne "Example:\n"
	echo -ne "\t1. Generate UEFI secureboot overlay package for multi board specs with specified uefi keys.\n"
	echo -ne "\tsudo $0 --uefi-keys ./uefi_keys/uefi_keys.conf  --rootfs-uuid 0e0ce46a-f9c4-4ac0-b5e8-3968c0fd865d\n"
	echo -ne "\t2. Generate UEFI secureboot overlay package for multi board specs with specified uefi keys and uefi enc keys for the device with rootfs A/B enabled.\n"
	echo -ne "\tsudo $0 --uefi-keys ./uefi_keys/uefi_keys.conf --uefi-enc uefi_enc.key --rootfs-uuid 0e0ce46a-f9c4-4ac0-b5e8-3968c0fd865d  --rootfs-b-uuid 32bdc3cd-68eb-448d-83df-037ed43ec841\n"
	exit 1
}

function check_file()
{
	local file="${1}"

	if [ "${file}" == "" ]; then
		echo "File name is NULL"
		usage
	elif [ ! -f "${file}" ]; then
		echo "Specified \"${file}\" is not found"
		usage
	fi
}

function check_opts()
{
	check_file "${base_package}"
	check_file "${uefi_keys}"
	if [ "${uefi_enc}" != "" ]; then
		check_file "${uefi_enc}"

		if [ "${chip_id}" == "" ]; then
			echo "Chip id must be provided once UEFI encryption key is specified."
			usage
		fi
	fi
	if [ "${extlinux_conf_param}" != "" ] && [ ! -f "${extlinux_conf_param}" ]; then
		echo "The specified \"${extlinux_conf_param}\" is not found"
		usage
	fi
}

function check_dependency()
{
	# Check "l4t_gen_uefi_sb_overlay.sh".
	if [ ! -f "${GEN_SB_OVERLAY_PACKAGE_SCRIPT}" ]; then
		echo "The \"l4t_gen_uefi_sb_overlay.sh\" is not found in the ${LINUX_BASE_DIR}"
		exit 1
	fi

	# Check "l4t_uefi_sign_image.sh" and "l4t_sign_image.sh"
	if [ ! -f "${SIGN_UEFI_IMAGE_SCRIPT}" ]; then
		echo "The \"l4t_uefi_sign_image.sh\" is not found in the ${LINUX_BASE_DIR}"
		exit 1
	fi
	if [ ! -f "${SIGN_IMAGE_SCRIPT}" ]; then
		echo "The \"l4t_sign_image.sh\" is not found in the ${LINUX_BASE_DIR}"
		exit 1
	fi

	# Check "openssl" and "sbsign"
	if ! which openssl >/dev/null 2>&1; then
		echo "The \"openssl\" is not found, please use \"sudo apt-get install openssl\" to install it"
		exit 1
	fi
	if ! which sbsign >/dev/null 2>&1; then
		echo "The \"sbsign\" is not found, please use \"sudo apt-get install sbsigntool\" to install it"
		exit 1
	fi
}

function check_default_uefi_sb_overlay()
{
	# Check whether secureboot package has been pre-generated or not.
	# If yes, check whether the UUIDs used in the package matches with
	# the expected UUIDs. If these UUIDs matches, do not generate new
	# secureboot package but exit directly.

	# Return if --extlinux-conf is specified
	if [ "${extlinux_conf_param}" != "" ]; then
		return 0
	fi

	# Check whether default "uefi_secureboot_overlay_multi_specs.tar.gz"
	# exists in the Linux_for_Tegra/bootloader/uefi_overlay
	local uefi_sb_multi_specs=
	uefi_sb_multi_specs="${UEFI_OVERLAY_DIR}/${UEFI_SB_OVERLAY_PACKAGE_MULTI_SPECS}"
	if [ ! -f "${uefi_sb_multi_specs}" ]; then
		echo "No default \"${uefi_sb_multi_specs}\" is found"
		return 0
	fi

	# Extract the default "${uefi_sb_multi_specs}"
	local uefi_sb_tmp=
	uefi_sb_tmp="$(mktemp -d)"
	if [ ! -d "${uefi_sb_tmp}" ]; then
		echo "Failed to create temp directory to extract ${uefi_sb_multi_specs}"
		exit 1
	fi
	if ! tar xzpf "${uefi_sb_multi_specs}" -C "${uefi_sb_tmp}" >/dev/null 2>&1; then
		echo "Failed to extract \"${uefi_sb_multi_specs}\" to ${uefi_sb_tmp}"
		rm -rf "${uefi_sb_tmp}"
		exit 1
	fi

	# Check whether the UUIDs used in the "${uefi_sb_multi_specs}"
	# matches the ones passed-in as arguments.
	# If all these UUIDs are the same, prompt message and exit
	# Otherwise, generate new UEFI secureboot overlay package with
	# the passed-in UUIDs.
	local uuids_list_file="${uefi_sb_tmp}/uuids_list.txt"
	local rootfs_uuid_in_list=
	local rootfs_b_uuid_in_list=
	local uda_uuid_in_list=
	local uuids_match="false"
	rootfs_uuid_in_list="$(grep "rootfs_uuid" "${uuids_list_file}" | cut -d: -f 2 || true)"
	rootfs_b_uuid_in_list="$(grep "rootfs_b_uuid" "${uuids_list_file}" | cut -d: -f 2 || true)"
	uda_uuid_in_list="$(grep "uda_uuid" "${uuids_list_file}" | cut -d: -f 2 || true)"
	if [ "${rootfs_uuid_in_list}" == "${rootfs_uuid}" ] \
		&& [ "${rootfs_b_uuid_in_list}" == "${rootfs_b_uuid}" ] \
		&& [ "${uda_uuid_in_list}" == "${uda_uuid}" ]; then
		uuids_match="true"
	fi
	rm -rf "${uefi_sb_tmp}"
	if [ "${uuids_match}" == "true" ]; then
		echo "Use the default \"${uefi_sb_multi_specs}\" directly" \
			"instead of re-generating UEFI secureboot overlay package"\
			"for multipe board specs as the passed-in UUIDs are the same"
		exit 0
	fi
}

function validate_board_spec()
{
	local board_spec_dir="${1}"

	# Check whether extlinux.conf exists
	if [ ! -f "${board_spec_dir}"/extlinux.conf ]; then
		echo "The ${board_spec_dir} does not contains extlinux.conf"
		return 1
	fi

	# Check whether extlinux.conf exists
	if [ ! -f "${board_spec_dir}"/initrd ]; then
		echo "The ${board_spec_dir} does not contains initrd"
		return 1
	fi

	# Check whether kernel DTB exists
	local kernel_dtb=
	kernel_dtb="$(ls -al "${board_spec_dir}"/ | grep -o "kernel_.*\.dtb$")"
	if [ "${kernel_dtb}" == "" ]; then
		echo "The ${board_spec_dir} does not contains kernel DTB"
		return 1
	fi
	return 0
}

function build_uefi_sb_overlay()
{
	local src_dir="${1}"
	local dest_dir="${2}"
	local extlinux_conf=
	local initrd_file=
	local kernel_dtb=
	local cmd=

	# Get extlinux.conf, initrd and kernel DTB
	if [ "${extlinux_conf_param}" != "" ]; then
		extlinux_conf="${extlinux_conf_param}"
	else
		extlinux_conf="${src_dir}/extlinux.conf"
	fi
	initrd_file="${src_dir}/initrd"
	kernel_dtb="$(ls -al "${src_dir}"/ | grep -o "kernel_.*\.dtb$")"
	kernel_dtb="${src_dir}/${kernel_dtb}"
	pushd "${LINUX_BASE_DIR}" >/dev/null 2>&1
	cmd="${GEN_SB_OVERLAY_PACKAGE_SCRIPT} "

	if [ "${rootfs_b_uuid}" != "" ]; then
		cmd+="--rootfs-b-uuid ${rootfs_b_uuid} "
	fi
	if [ "${uda_uuid}" != "" ]; then
		cmd+="--uda-uuid ${uda_uuid} "
	fi
	if [ "${uefi_enc}" != "" ]; then
		cmd+="--uefi-enc ${uefi_enc} "
		if [ "${chip_id}" != "" ]; then
			cmd+="--chipid ${chip_id} "
		fi
	fi
	cmd+="--kernel-dtb ${kernel_dtb} "
	cmd+="--uefi-keys ${uefi_keys} "
	cmd+="${initrd_file} ${extlinux_conf} ${rootfs_uuid}"
	echo "Generate UEFI overlay package by running ${cmd}"
	if ! eval "${cmd}"; then
		echo "Failed to generate UEFI overlay package"
		popd >/dev/null 2>&1
		return 1
	fi
	popd >/dev/null 2>&1

	# Copy the genreated UEFI overlay package to "${dest_dir}"
	local uefi_sb_overlay="${UEFI_OVERLAY_DIR}/${UEFI_SB_OVERLAY_PACKAGE}"
	if [ ! -f "${uefi_sb_overlay}" ]; then
		echo "The generated ${uefi_sb_overlay} is not found"
		return 1
	fi
	echo "Copying ${uefi_sb_overlay} into ${dest_dir}"
	cp -vf "${uefi_sb_overlay}" "${dest_dir}"/
	return 0
}

function build_uefi_sb_multi_specs_package()
{
	# Build the "uefi_secureboot_overlay_multi_specs.tar.gz" from
	# the "uefi_base_multi_specs.tar.gz".
	# The steps are:
	# 1. Extract the "uefi_base_multi_specs.tar.gz"
	# 2. Build the "uefi_secureboot_overlay.tar.gz" for each board
	# spec based on the existing exlinux.conf, initrd and kernel DTB.
	# 3. Pack the "uefi_secureboot_overlay.tar.gz" for all the board
	# specs as "uefi_secureboot_overlay_multi_specs.tar.gz"
	local uefi_base_tmp=
	uefi_base_tmp="$(mktemp -d)"
	if [ ! -d "${uefi_base_tmp}" ]; then
		echo "Failed to create temp directory to extract ${base_package}"
		exit 1
	fi

	# Extract the "uefi_base_multi_specs.tar.gz" to "${uefi_base_tmp}"
	if ! tar xzpf "${base_package}" -C "${uefi_base_tmp}" >/dev/null 2>&1; then
		echo "Failed to extract ${base_package}"
		rm -rf "${uefi_base_tmp}"
		exit 1
	fi

	# Create the directory that stores the "uefi_secureboot_overlay.tar.gz"
	# for all the board specs.
	local uefi_sb_tmp=
	uefi_sb_tmp="$(mktemp -d)"
	if [ ! -d "${uefi_sb_tmp}" ]; then
		echo "Failed to create temp directory to store the built packages"
		rm -rf "${uefi_base_tmp}"
		exit 1
	fi


	# Build "uefi_secureboot_overlay.tar.gz" based on each sub-directory
	# that is dependent on board spec under "${uefi_base_tmp}"
	# Each sub-directory must have the extlinux.conf, initrd and the kernel DTB
	# If not, report error and exit
	local sub_dir=
	local sub_dir_name=
	local target_dir=
	for sub_dir in "${uefi_base_tmp}"/*
	do
		# Validate the sub-directory
		if ! validate_board_spec "${sub_dir}"; then
			echo "Failed to validate ${sub_dir}"
			rm -rf "${uefi_base_tmp}" "${uefi_sb_tmp}"
			exit 1
		fi

		# Create sub-directory under "${uefi_sb_tmp}"
		sub_dir_name="$(basename "${sub_dir}")"
		target_dir="${uefi_sb_tmp}/${sub_dir_name}"
		mkdir -p "${target_dir}"

		# Build "uefi_secureboot_overlay.tar.gz" based on the
		# files in the "${sub_dir}", and then store it into
		# the "${target_dir}"
		if ! build_uefi_sb_overlay "${sub_dir}" "${target_dir}"; then
			echo "Failed to build UEFI secureboot overlay package based on ${sub_dir}"
			rm -rf "${uefi_base_tmp}" "${uefi_sb_tmp}"
			exit 1
		fi
	done

	# Add "uuids_list.txt" file
	local uuids_list_file="uuids_list.txt"
	echo "rootfs_uuid:${rootfs_uuid}" > "${uefi_sb_tmp}/${uuids_list_file}"
	if [ "${rootfs_b_uuid}" != "" ]; then
		echo "rootfs_b_uuid:${rootfs_b_uuid}" >> "${uefi_sb_tmp}/${uuids_list_file}"
	fi
	if [ "${uda_uuid}" != "" ]; then
		echo "uda_uuid:${uda_uuid}" >> "${uefi_sb_tmp}/${uuids_list_file}"
	fi

	# Build "uefi_secureboot_overlay_multi_specs.tar.gz"
	local output_package=
	# Put the generated package into Linux_for_Tegra/bootloader/uefi_overlay in default
	output_package="${UEFI_OVERLAY_DIR}/${UEFI_SB_OVERLAY_PACKAGE_MULTI_SPECS}"

	pushd "${uefi_sb_tmp}" >/dev/null 2>&1
	if ! tar czpf "${output_package}" ./* >/dev/null 2>&1; then
		echo "Failed to generate UEFI overlay pacakge for multiple board specs at ${output_package}"
		popd >/dev/null 2>&1
		rm -rf "${uefi_base_tmp}" "${uefi_sb_tmp}"
		return 1
	fi
	popd >/dev/null 2>&1
	echo "Generate UEFI secureboot overlay package for multiple board specs at ${output_package}"
	return 0
}

USERID=$(id -u)
if [ "${USERID}" -ne 0 ]; then
	echo "Please run this program as root."
	exit 0
fi

if [ $# -lt 4 ];then
	usage
fi

rootfs_uuid=
rootfs_b_uuid=
uda_uuid=
uefi_keys=
uefi_enc=
chip_id=
base_package="${DEFAULT_UEFI_BASE_PACKAGE}"
extlinux_conf_param=
opstr+=":-:"
while getopts "${opstr}" OPTION; do
	case $OPTION in
	-) case ${OPTARG} in
		rootfs-uuid)
			rootfs_uuid="${!OPTIND}";
			OPTIND=$((OPTIND + 1));
			;;
		rootfs-b-uuid)
			rootfs_b_uuid="${!OPTIND}";
			OPTIND=$((OPTIND + 1));
			;;
		uda-uuid)
			uda_uuid="${!OPTIND}";
			OPTIND=$((OPTIND + 1));
			;;
		uefi-keys)
			uefi_keys="${!OPTIND}";
			OPTIND=$((OPTIND + 1));
			;;
		uefi-enc)
			uefi_enc="${!OPTIND}";
			OPTIND=$((OPTIND + 1));
			;;
		chipid)
			chip_id="${!OPTIND}";
			OPTIND=$((OPTIND + 1));
			;;
		uefi-signing-base)
			base_package="${!OPTIND}";
			OPTIND=$((OPTIND + 1));
			;;
		extlinux-conf)
			extlinux_conf_param="${!OPTIND}";
			OPTIND=$((OPTIND + 1));
			;;
		*) usage; ;;
		esac;;
	*) usage; ;;
	esac;
done

check_opts

check_dependency

check_default_uefi_sb_overlay

if ! build_uefi_sb_multi_specs_package; then
	echo "ERROR: failed to generate UEFI secureboot overlay package for multiple board specs"
	exit 1
fi
