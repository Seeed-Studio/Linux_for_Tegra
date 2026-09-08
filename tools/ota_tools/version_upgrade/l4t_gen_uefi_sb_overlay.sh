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

# This is a script to generate UEFI secureboot overlay package used
# in image-based OTA.
#
# Usage:
#    sudo ./l4t_gen_uefi_sec_overlay.sh [options] \
#	<initrd file> <extlinux.conf> <rootfs_uuid>
#
# It generates the signed extlinux.conf and signed/encrypted initrd, and
# then pack them into "uefi_secureboot_overlay.tar.gz". This package
# is to be used in image-based OTA.
set -e
LINUX_BASE_DIR="$(pwd)"
BOOTLOADER_DIR="${LINUX_BASE_DIR}/bootloader"
UEFI_OVERLAY_DIR="${BOOTLOADER_DIR}/uefi_overlay"
OVERLAY_PACKAGE="${UEFI_OVERLAY_DIR}/uefi_secureboot_overlay.tar.gz"
TMP_WORK_DIR=
TMP_SIGNED_DIR=

# UEFI DB key and cert
uefi_db_key=
uefi_db_cert=
# UEFI encryption key
uefi_enc_key=

function usage()
{
	echo -ne "Usage: sudo $0 [options] [--uefi-keys <keys_conf>] <initrd file> <extlinux.conf> <rootfs_uuid>\n"
	echo -ne "\tWhere,\n"
	echo -ne "\t\t<keys_conf>: Specify UEFI keys configuration file.\n"
	echo -ne "\t\t<initrd file>: Specify the initrd file to be updated and then integrated into package.\n"
	echo -ne "\t\t<extlinux.conf>: Specify the extlinux.conf to be updated and then integrated into package.\n"
	echo -ne "\t\t<rootfs_uuid>: Specify UUID for rootfs partition on the device to be updated.\n"
	echo -ne "\toptions:\n"
	echo -ne "\t\t--rootfs-b-uuid <rootfs B UUID>: Specify UUID for rootfs B partition on the device to be updated.\n"
	echo -ne "\t\t--uda-uuid <uda UUID>: Specify UUID for UDA partition on the device to be updated.\n"
	echo -ne "\t\t--uefi-enc <uefi_enc_key>: Key file (0x19: 16-byte; 0x23: 32-byte) to encrypt UEFI payloads.\n"
	echo -ne "\t\t--chipid <chip id>: Specify chip id. This option must be specified if --uefi-enc is specified. \"0x23\" for T234 devices, and \"0x19\" for T194 devices.\n"
	echo -ne "\t\t--kernel-dtb <kernel DTB>: Specify kernel DTB file to be signed and/or encrypted.\n"
	echo -ne "Example:\n"
	echo -ne "\t1. Generate UEFI secureboot overlay package with specified uefi keys and uefi enc key.\n"
	echo -ne "\tsudo $0 --uefi-keys ./uefi_keys/uefi_keys.conf --uefi-enc uefi_enc.key ./initrd ./extlinux.conf 0e0ce46a-f9c4-4ac0-b5e8-3968c0fd865d\n"
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
	check_file "${initrd_file}"
	check_file "${extlinux_conf}"
	check_file "${uefi_keys}"

	if [ "${uefi_enc}" != "" ]; then
		check_file "${uefi_enc}"

		if [ "${chip_id}" == "" ]; then
			echo "Chip id must be provided once UEFI encryption key is specified."
			usage
		fi
	fi
}

function init()
{
	TMP_WORK_DIR="$(mktemp -d)"
	if [ ! -d "${TMP_WORK_DIR}" ]; then
		echo "ERROR: failed to create temporary work dir"
		exit 1
	fi
	TMP_SIGNED_DIR="${TMP_WORK_DIR}/signed"
	mkdir -p "${TMP_SIGNED_DIR}"
}

function clean_up()
{
	if [ -d "${TMP_WORK_DIR}" ]; then
		rm -rf "${TMP_WORK_DIR}"
	fi
}

function validate_uuid()
{
	# UUID format is 8-4-4-4-12, where "8" repsents 8 hexadecimal value
	local _uuid="${1}"
	if [[ "${_uuid}" =~ ^[A-F0-9a-f]{8}-[A-F0-9a-f]{4}-[A-F0-9a-f]{4}-[A-F0-9a-f]{4}-[A-F0-9a-f]{12}$ ]]; then
		return 0
	else
		return 1
	fi
}

update_rootdev()
{
	# Update "root=" in extlinux.conf
	local ori_extlinux_conf="${1}"
	local updated_extlinux_conf="${2}"
	local uuid="${3}"
	local line=
	local line_num=

	cp -vf "${ori_extlinux_conf}"  "${updated_extlinux_conf}"
	line_num="$(grep -n "^[\t ]*APPEND" "${updated_extlinux_conf}" | cut -d: -f 1)"
	line="$(grep -n "^[\t ]*APPEND" "${updated_extlinux_conf}" | cut -d: -f 2)"
	if [[ "${line}" =~ "root=UUID" ]]; then
		sed -i "${line_num}s/root=[-=/0-9A-Za-z]\+/root=UUID=${uuid}/" "${updated_extlinux_conf}"
	else
		sed -i "${line_num}s/root=[-=/0-9A-Za-z]\+/root=PARTUUID=${uuid}/" "${updated_extlinux_conf}"
	fi
}

update_uuid_in_extlinux_conf()
{
	# Copy the specified exlinux.conf to TMP_WORK_DIR and then
	# update PARTUUID or UUID in it.
	local _extlinux_conf="${TMP_WORK_DIR}/extlinux.conf"
	local line=
	local line_num=

	cp -vf "${extlinux_conf}" "${_extlinux_conf}"
	update_rootdev "${_extlinux_conf}" "${TMP_SIGNED_DIR}/extlinux.conf" \
		"${rootfs_uuid}"

	# Update the extlinux.conf for rootfs B
	if [ "${rootfs_b_uuid}" != "" ]; then
		cp -vf "${extlinux_conf}" "${_extlinux_conf}"
		update_rootdev "${_extlinux_conf}" "${TMP_SIGNED_DIR}/extlinux.conf.B" \
			"${rootfs_b_uuid}"
	fi
}

update_crypttab()
{
	# Update the /etc/crypttab
	local initrd_tmp="${1}"
	local crypt_rootfs_uuid="${2}"
	local crypt_rootfs_other_uuid="${3}"
	local crypt_uda_uuid="${4}"
	local updated_initrd_file="${5}"
	local etc_crypttab="etc/crypttab"

	pushd "${initrd_tmp}" >/dev/null 2>&1
	if [ -f "${etc_crypttab}" ]; then
		sed -i "s/crypt_root UUID=[-0-9A-Za-z]\+/crypt_root UUID=${crypt_rootfs_uuid}/" "${etc_crypttab}"
		if [ "${crypt_rootfs_other_uuid}" != "" ]; then
			sed -i "s/crypt_root_other UUID=[-0-9A-Za-z]\+/crypt_root_other UUID=${crypt_rootfs_other_uuid}/" "${etc_crypttab}"
		fi
		if [ "${crypt_uda_uuid}" != "" ]; then
			sed -i "s/crypt_UDA UUID=[-0-9A-Za-z]\+/crypt_UDA UUID=${crypt_uda_uuid}/" "${etc_crypttab}"
		fi
	fi
	find . | cpio -H newc -o | gzip -9 -n > "${updated_initrd_file}"
	popd > /dev/null 2>&1
}

update_uuid_in_initrd()
{
	# Update UUID in initrd
	# Steps:
	#  1. Copy initrd into TMP_WORK_DIR
	#  2. Un-pack initrd
	#  3. Update UUID in the /etc/crypttab
	#  4. Re-pack initrd
	local _initrd_file="${TMP_WORK_DIR}/initrd.gz"
	local initrd_tmp="${TMP_WORK_DIR}/initrd_tmp"
	local etc_crypttab="etc/crypttab"

	cp -vf "${initrd_file}" "${_initrd_file}"
	mkdir -p "${initrd_tmp}"
	pushd "${initrd_tmp}" >/dev/null 2>&1
	if ! gunzip -c "${_initrd_file}" | cpio -i; then
		echo "Failed to extract ${_initrd_file}"
		popd > /dev/null 2>&1
		return 1
	fi
	popd > /dev/null 2>&1

	update_crypttab "${initrd_tmp}" "${rootfs_uuid}" \
		"${rootfs_b_uuid}" "${uda_uuid}" "${TMP_SIGNED_DIR}/initrd"

	# Update the initrd for rootfs B
	if [ "${rootfs_b_uuid}" != "" ]; then
		update_crypttab "${initrd_tmp}" "${rootfs_b_uuid}" \
			"${rootfs_uuid}" "${uda_uuid}" "${TMP_SIGNED_DIR}/initrd.B"
	fi
	return 0
}

function update_uuid()
{
	# Update UUID

	# Validate rootfs UUID
	if ! validate_uuid "${rootfs_uuid}"; then
		echo "ERROR: specified rootfs UUID ${rootfs_uuid} is invalid"
		return 1
	fi

	# Validate rootfs B UUID if it is specified
	if [ "${rootfs_b_uuid}" != "" ]; then
		if ! validate_uuid "${rootfs_b_uuid}"; then
			echo "ERROR: specified rootfs B UUID ${rootfs_b_uuid} is invalid"
			return 1
		fi
	fi

	# Validate UDA UUID if it is specified
	if [ "${uda_uuid}" != "" ]; then
		if ! validate_uuid "${uda_uuid}"; then
			echo "ERROR: specified UDA UUID ${uda_uuid} is invalid"
			return 1
		fi
	fi

	# Update UUID in the specified exlinux.conf
	if ! update_uuid_in_extlinux_conf; then
		echo "ERROR: failed to update rootfs UUID in the extlinux.conf"
		return 1
	fi

	# Update UUID in the initrd if it is specified
	if ! update_uuid_in_initrd; then
		echo "ERROR: failed to update UUID in the initrd"
		return 1
	fi
	return 0
}

function convert_key_to_hex()
{
	# Convert a key file to an ascii hex string by removing '0x' and ' '.
	result=$(sed -e 's/\(0x\| \)//g' "$1");
	echo "${result}";
}

function validate_uefi_keys()
{
	# Check whether the specified keys in the "uefi_keys" exist.
	local uefi_keys_conf=
	local uefi_keys_conf_dir=

	uefi_keys_conf=$(readlink -f "${uefi_keys}")
	if [ ! -f "${uefi_keys_conf}" ]; then
		echo "ERROR: UEFI keys conf file ${uefi_keys_conf} not found"
		return 1
	fi;
	source "${uefi_keys_conf}"
	uefi_keys_conf_dir="$(dirname "${uefi_keys_conf}")"
	# Use DB1 as signing key
	uefi_db_key="${uefi_keys_conf_dir}/${UEFI_DB_1_KEY_FILE}"
	uefi_db_cert="${uefi_keys_conf_dir}/${UEFI_DB_1_CERT_FILE}"
	if [ ! -f "${uefi_db_key}" ]; then
		echo "ERROR: specified UEFI db key ${uefi_db_key} is not found"
		return 1
	fi
	if [ ! -f "${uefi_db_cert}" ]; then
		echo "ERROR: specified UEFI db cert ${uefi_db_cert} is not found"
		return 1
	fi

	# Check whether the key specified by the "uefi_enc_key" is valid
	local uefi_enc_content=
	local uefi_enc_hex=
	local key_len=
	if [ "${uefi_enc}" != "" ]; then
		uefi_enc_key=$(readlink -f "${uefi_enc}");
		if [ ! -f "${uefi_enc_key}" ]; then
			echo "Error: keyfile ${uefi_enc_key} not found";
			exit 1;
		fi;
		uefi_enc_content=$(cat "$uefi_enc_key")
		echo "uefi_enc_content= ${uefi_enc_content}"
		uefi_enc_hex=$(convert_key_to_hex "${uefi_enc_key}")
		key_len=${#uefi_enc_hex}
		if [ "${chip_id}" = "0x23" ]; then
			if [ "${key_len}" != 64 ]; then
				echo "Error: key size has to be 64"
				return 1
			fi
		elif [ "${chip_id}" = "0x19" ]; then
			if [ "${key_len}" != 32 ]; then
				echo "Error: key size has to be 32"
				return 1
			fi
		else
			echo "ERROR: unsupported chip id ${chip_id}"
			return 1
		fi
	fi

	return 0
}

function uefi_encryptimage()
{
	# call l4t_sign_image.sh to generate encrypted and signed image
	# The image is generated in the same folder of file path specified in --file
	# The encrypted image will replace the original image and will have a BCH.
	local image="$1";
	local ftype="$2";
	local sign_key="$3";
	local enc_key="$4";
	local tmpdir="${TMP_WORK_DIR}/uefi_encrypt"

	mkdir -p "${tmpdir}" > /dev/null 2>&1
	image_file=$(readlink -f "${image}")
	cp "${image_file}" "${tmpdir}/file"

	if [ "${chip_id}" = "0x23" ]; then
		"${LINUX_BASE_DIR}"/l4t_sign_image.sh \
			--file "${tmpdir}/file" --type "${ftype}" \
			--key "${sign_key}" --encrypt_key "${enc_key}" --chip "${chip_id}" --split True \
			--enable_user_kdk True
	elif [ "${chip_id}" = "0x19" ]; then
		"${LINUX_BASE_DIR}"/l4t_sign_image.sh \
			--file "${tmpdir}/file" --type "${ftype}" \
			--key "${sign_key}" --encrypt_key "${enc_key}" --chip "${chip_id}" --split True
	else
		echo "Error: Unsupported chip id ${chip_id}"
		exit 1
	fi

	cat "${tmpdir}/file".sig "${tmpdir}/file" > "${tmpdir}/file".encrypted
	cp "${tmpdir}/file".encrypted "${image_file}"
	rm -rf "${tmpdir}"
}

function uefi_signimage()
{
	local image="$1";
	local key="$2";
	local cert="$3";
	local mode="$4";
	local enable_enc="$5";

	if ! "${LINUX_BASE_DIR}"/l4t_uefi_sign_image.sh \
			--image "${image}" \
			--key "${key}" \
			--cert "${cert}" \
			--mode "${mode}"; then
		echo "Failed to run ${LINUX_BASE_DIR}/l4t_uefi_sign_image.sh to sign ${image}"
		return 1
	fi

	if [ "${uefi_enc_key}" != "" ] && [ "${enable_enc}" == "True" ]; then
		uefi_encryptimage "${image}" "data" "${key}" "${uefi_enc_key}"
	fi
}

pad_file_aligned()
{
	local __file="$1"
	local __alignment="$2"
	local __padvalue="$3"
	local __padstring=""

	filesize=$(stat --format=%s "${__file}")
	rem=$(( filesize % __alignment ))
	if (( rem > 0 )); then
		rem=$(( __alignment - rem ))
		for ((i = 0 ; i < rem ; i++)); do
			__padstring+=${__padvalue}
		done
		echo -e -n "${__padstring}" >> "${__file}"
	fi
}

function uefi_sign_extlinux_conf()
{
	# Sign extlinux.conf in the TMP_SIGNED_DIR
	local _extlinux_conf="${1}"
	local extlinux_dir=
	local extlinux_conf_name=

	extlinux_conf_name="$(basename "${_extlinux_conf}")"
	extlinux_dir="$(dirname "${_extlinux_conf}")"
	pushd "${extlinux_dir}" >/dev/null 2>&1
	if [ ! -f ./"${extlinux_conf_name}" ]; then
		echo "ERROR: ${_extlinux_conf} is not found"
		popd >/dev/null 2>&1
		return 1
	fi
	echo -n -e "\tgenerating sig file of ${_extlinux_conf} ... "
	# Signing tool will pad extlinux.conf with 0x80 to be 16-byte aligned.
	# This pad byte of 0x80 may cause some utilities fail to read the entire
	# extlinux.conf.
	# So, pad extlinux.conf to 16-byte aligned with linefeed.
	pad_file_aligned ./"${extlinux_conf_name}" 16 "\x0a"
	if ! uefi_signimage ./"${extlinux_conf_name}" "${uefi_db_key}" "${uefi_db_cert}" "split" "False"; then
		echo "ERROR: failed to call uefi_signimage to sign ${_extlinux_conf}"
		popd >/dev/null 2>&1
		return 1
	fi
	if [ ! -f ./"${extlinux_conf_name}.sig" ]; then
		echo "ERROR: ${_extlinux_conf}.sig is not generated"
		popd >/dev/null 2>&1
		return 1
	fi
	popd >/dev/null 2>&1
	return 0
}

function uefi_sign_encrypt_image()
{
	# Sign and/or encrypt image in the TMP_SIGNED_DIR
	local _image_file="${1}"
	local image_dir=
	local image_file_name=
	local enc_enabled=

	image_file_name="$(basename "${_image_file}")"
	image_dir="$(dirname "${_image_file}")"
	# Encrypt image or not
	if [ "${uefi_enc_key}" != "" ]; then
		enc_enabled="True"
	else
		enc_enabled="False"
	fi
	pushd "${image_dir}" >/dev/null 2>&1
	if [ ! -f ./"${image_file_name}" ]; then
		echo "ERROR: ${_image_file} is not found"
		popd >/dev/null 2>&1
		return 1
	fi
	echo -n -e "\tgenerating sig file of ${_image_file} ... ";
	if ! uefi_signimage ./"${image_file_name}" "${uefi_db_key}" "${uefi_db_cert}" "split" "${enc_enabled}"; then
		echo "ERROR: failed to call uefi_signimage to sign and/or ${_image_file}"
		popd >/dev/null 2>&1
		return 1
	fi
	if [ ! -f ./"${image_file_name}.sig" ]; then
		echo "ERROR: ${_image_file}.sig is not generated"
		popd >/dev/null 2>&1
		return 1
	fi
	popd >/dev/null 2>&1
	return 0
}

function uefi_sign_encrypt()
{
	# Return directly if uefi keys is not specified
	if [ "${uefi_keys}" == "" ]; then
		return 0
	fi

	# Validate the specified UEFI keys
	if ! validate_uefi_keys; then
		echo "Failed to validate specified UEFI keys"
		return 1
	fi

	# Sign extlinux.conf
	if ! uefi_sign_extlinux_conf "${TMP_SIGNED_DIR}/extlinux.conf"; then
		echo "Failed to sign extlinux.conf"
		return 1
	fi
	# Sign extlinux.conf for rootfs B if it is specified
	if [ "${rootfs_b_uuid}" != "" ]; then
		if ! uefi_sign_extlinux_conf \
			"${TMP_SIGNED_DIR}/extlinux.conf.B"; then
			echo "Failed to sign extlinux.conf for rootfs B"
			return 1
		fi
	fi

	# Sign and/or encrypt initrd
	if ! uefi_sign_encrypt_image "${TMP_SIGNED_DIR}/initrd"; then
		echo "Failed to sign and/or encrypt initrd"
		return 1
	fi
	# Sign initrd for rootfs B if it is specified
	if [ "${rootfs_b_uuid}" != "" ]; then
		if ! uefi_sign_encrypt_image "${TMP_SIGNED_DIR}/initrd.B"; then
			echo "Failed to sign and/or encrypt initrd"
			return 1
		fi
	fi

	# Sign and/or encrypt kernel DTB if it is specified
	local dtb_basename=
	if [ -f "${kernel_dtb}" ]; then
		dtb_basename="$(basename "${kernel_dtb}")"
		cp -vf "${kernel_dtb}" "${TMP_SIGNED_DIR}/${dtb_basename}"
		if ! uefi_sign_encrypt_image "${TMP_SIGNED_DIR}/${dtb_basename}"; then
			echo "Failed to sign and/or encrypt kernel DTB ${dtb_basename}"
			return 1
		fi
	fi

	return 0
}

function generate_package()
{
	mkdir -p "${UEFI_OVERLAY_DIR}"
	pushd "${TMP_SIGNED_DIR}" >/dev/null 2>&1
	if ! tar czpf "${OVERLAY_PACKAGE}" . >/dev/null 2>&1; then
		echo "Failed to create UEFI overlay package at ${OVERLAY_PACKAGE}"
		popd >/dev/null 2>&1
		return 1
	fi
	popd >/dev/null 2>&1
	echo "Generate UEFI secureboot overlay package at ${OVERLAY_PACKAGE}"
}

USERID=$(id -u)
if [ "${USERID}" -ne 0 ]; then
	echo "Please run this program as root."
	exit 0
fi

if [ $# -lt 3 ];then
	usage
fi
nargs=$#
rootfs_uuid="${!nargs}"
nargs=$((nargs-1))
extlinux_conf="${!nargs}"
nargs=$((nargs-1))
initrd_file="${!nargs}"

rootfs_b_uuid=
uda_uuid=
uefi_keys=
uefi_enc=
chip_id=
opstr+=":-:"
while getopts "${opstr}" OPTION; do
	case $OPTION in
	-) case ${OPTARG} in
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
		kernel-dtb)
			kernel_dtb="${!OPTIND}";
			OPTIND=$((OPTIND + 1));
			;;
		*) usage; ;;
		esac;;
	*) usage; ;;
	esac;
done

check_opts

init

if ! update_uuid; then
	echo "ERROR: failed to update PARTUUID or UUID"
	clean_up
	exit 1
fi

if ! uefi_sign_encrypt; then
	echo "ERROR: failed to sign and/or encrypt the extlinux.conf and/or initrd"
	clean_up
	exit 1
fi

if ! generate_package; then
	echo "ERROR: failed to generate UEFI secureboot overlay package"
	clean_up
	exit 1
fi

clean_up
