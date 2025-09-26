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

# This is a script to make BASE recovery image/dtb for OTA

# 1. Re-pack initrd by adding necessary files
# 2. Build recovery image, sign it and generate sha1 check sum file
# 3. Build recovery dtb, sign it and generate sha1 check sum file
# 4. Copy all these four files to <target BSP L4T dir>/bootloader

# The BASE version can be:
# "R32.5", "R32.6", "R32.7", "R35.2", "R35.3", "R35.4"
set -e

KERNEL_DTB_NAME=
WORKDIR=""
T194_BASE_RECOVERY_CMDLINE="root=/dev/initrd rw rootwait console=ttyTCU0,115200n8 fbcon=map:0 net.ifnames=0 video=tegrafb no_console_suspend=1 earlycon=tegra_comb_uart,mmio32,0x0c168000"
BASE_RECOVERY_IMAGE_FILE=""
BASE_RECOVERY_IMAGE_SHA1SUM_FILE=""
BASE_RECOVERY_DTB_FILE=""
BASE_RECOVERY_DTB_SHA1SUM_FILE=""
BASE_RECOVERY_INITRD_FILE=""
BASE_KERNEL_VERSION=""
REC_COPY_BINLIST_NAME="recovery_copy_binlist.txt"
BASE_VERSION=""
TARGET_BOARD=""
CHIPID=""
BOARD_SPECS_ARRAY=""
# Used by "disk_encryption_helper.func"
rootfs_ab=0

usage()
{
	echo -ne "Usage: sudo $0 [-u <PKC key file>] [--user-key <User key file>] <target board> <bsp version> <base BSP L4T dir> <base BSP rootfs dir> <target BSP L4T dir>\n"
	echo -ne "\t-u <PKC key file>: PKC key used for odm fused board\n"
	echo -ne "\t--user-key <User key file>: User provided key file (16-byte) to encrypt recovery image and recovery dtb.\n"
	echo -ne "\t   This option is only valid on the board flashed with PKC and SBK. And it must be the same as the user key when flashing board.\n"
	echo -ne "\t<target_board>: specify the target board.\n"
	echo -ne "\t   supported target boards: jetson-agx-xavier-devkit, jetson-xavier-nx-devkit-emmc, jetson-agx-xavier-industrial.\n"
	echo -ne "\t<bsp version>: specify the version of the base BSP\n"
	echo -ne "\t   supported versions: R32-5, R32-6, R32-7, R35-2, R35-3, R35-4, R35-5.\n"
	echo -ne "\t<base BSP L4T dir>: specify the path of the L4T dir of the base BSP\n"
	echo -ne "\t<base BSP rootfs dir>: specify the path of the rootfs of the base BSP\n"
	echo -ne "\t<target BSP L4T dir>: specify the path of the L4T dir of the target BSP\n"
	echo -ne "Example:\n"
	echo -ne "\tsudo $0 -u PKCkeyfile --user-key Userkeyfile jetson-agx-xavier-devkit R32-6 <R32.6_BSP>/Linux_for_Tegra <R32.6_BSP>/Linux_for_Tegra/rootfs <R35.2_BSP>/Linux_for_Tegra\n"
	exit 1
}

error_exit()
{
	local message="${1}"
	if [ "${message}" != "" ]; then
		echo "ERROR: ${message}"
	fi
	if [ -d "${WORKDIR}" ];then
		rm -Rf "${WORKDIR}"
	fi
	exit 1
}

check_error()
{
	if [ $? -ne 0 ];then
		if [ "$1" != "" ];then
			error_exit "failed command: $1"
		else
			error_exit "command is failed"
		fi
	fi
}

check_warning()
{
	if [ $? -ne 0 ];then
		if [ "$1" != "" ];then
			echo "Warning: $1"
		fi
	fi
}

init()
{
	local version=
	if is_layout_change_needed "${BASE_VERSION}"; then
		version="R32x"
	else
		version="R35x"
	fi
	WORKDIR="/tmp/${version}_recovery"
	if [ -d "${WORKDIR}" ];then
		rm -Rf "${WORKDIR}"
	fi
	mkdir "${WORKDIR}";check_error

	BASE_RECOVERY_IMAGE_FILE=${WORKDIR}/recovery.img.${version}
	BASE_RECOVERY_IMAGE_SHA1SUM_FILE=${WORKDIR}/recovery.img.${version}.sha1sum
	BASE_RECOVERY_DTB_FILE=${WORKDIR}/recovery.dtb.${version}
	BASE_RECOVERY_DTB_SHA1SUM_FILE=${WORKDIR}/recovery.dtb.${version}.sha1sum
	BASE_RECOVERY_INITRD_FILE=${WORKDIR}/${version}_initrd.img
}

copy_files()
{
	local _rootfs_dir="${1}"
	local _ota_dir="${2}"
	local _initrd_dir="${3}"
	local _binlist_file="${4}"
	local _tmp_list_file="${WORKDIR}/binlist.txt.tmp"
	local _ubuntu_version=

	if [[ "${BASE_VERSION}" =~ "R32" ]]; then
		_ubuntu_version="bionic"
	else
		_ubuntu_version="focal"
	fi

	# The folder which contains the libraries
	local _librariesdir
	_librariesdir=$(find -H "${_rootfs_dir}/lib" -name libc.so.6 | tail -1 | awk -F/ '{ print $(NF-1) }')
	grep -E "^(${_ubuntu_version}|all)" "${_binlist_file}" | sed \
		-e "s|<ARCH>|${_librariesdir}|g" \
		-e "s|<ROOTFS>|${_rootfs_dir}|g" \
		-e "s|<OTA_DIR>|${_ota_dir}|g" \
		-e "s|<KERNEL_VERSION>|${BASE_KERNEL_VERSION}|g" >"${_tmp_list_file}"
		check_error "generate binlist file ${_tmp_list_file}"

	local _src=
	local _dst=
	local _dst_dir=
	# Copy all the binary
	set +e
	while read -r path
	do
		_src=$(echo "${path}" | cut -d ':' -f 2)
		_dst=$(echo "${path}" | cut -d ':' -f 3)
		_dst_dir="${_dst%/*}"
		mkdir -p "${_initrd_dir}/${_dst_dir}"
		cp -f "${_src}" "${_initrd_dir}/${_dst}"
		check_warning "cp -f ${_src} ${_initrd_dir}/${_dst}"
	done < "${_tmp_list_file}"
	set -e

	rm -f "${_tmp_list_file}"
}

prepare_sshd_files()
{
	local initrd_dir="${1}"

	# The default "initrd" is using ld-2.23, but the needed libraries/binaries
	# depend on the ld-2.27 if base version is R32x, so replacement are needed here.
	if [[ "${BASE_VERSION}" =~ "R32" ]]; then
		pushd "${initrd_dir}/lib/" || error_exit "Failed to enter ${initrd_dir}/lib/"
		rm -f ld-linux-aarch64.so.1 aarch64-linux-gnu/ld-2.23.so
		ln -s aarch64-linux-gnu/ld-2.27.so ld-linux-aarch64.so.1
		check_error "ln -s aarch64-linux-gnu/ld-2.27.so ld-linux-aarch64.so.1"

		cd "./aarch64-linux-gnu/" || error_exit "Failed to cd to ./aarch64-linux-gnu/"
		rm -f libc.so.6 libc-2.23.so
		ln -s libc-2.27.so libc.so.6
		check_error "ln -s libc-2.27.so libc.so.6"

		rm -f libdl.so.2 libdl-2.23.so
		ln -s libdl-2.27.so libdl.so.2
		check_error "ln -s libdl-2.27.so libdl.so.2"

		rm -f libm.so.6 libm-2.23.so
		ln -s libm-2.27.so libm.so.6
		check_error "ln -s libm-2.27.so libm.so.6"

		rm -f libnsl.so.1 libnsl-2.23.so
		ln -s libnsl-2.27.so libnsl.so.1
		check_error "ln -s libnsl-2.27.so libnsl.so.1"

		rm -f libnss_files.so.2 libnss_files-2.23.so
		ln -s libnss_files-2.27.so libnss_files.so.2
		check_error "ln -s libnss_files-2.27.so libnss_files.so.2"

		rm -f libnss_nis.so.2 libnss_nis-2.23.so
		ln -s libnss_nis-2.27.so libnss_nis.so.2
		check_error "ln -s libnss_nis-2.27.so libnss_nis.so.2"

		rm -f libpthread.so.0 libpthread-2.23.so
		ln -s libpthread-2.27.so libpthread.so.0
		check_error "ln -s libpthread-2.27.so libpthread.so.0"

		rm -f libresolv.so.2 libresolv-2.23.so
		ln -s libresolv-2.27.so libresolv.so.2
		check_error "ln -s libresolv-2.27.so libresolv.so.2"

		rm -f librt.so.1 librt-2.23.so
		ln -s librt-2.27.so librt.so.1
		check_error "ln -s librt-2.27.so librt.so.1"

		popd  > /dev/null 2>&1 || error_exit "Failed to popd"
	fi

	local ssh_config_dir="/etc/ssh"
	local sshd_config_conf="${ssh_config_dir}/sshd_config.d/initrd.conf"
    mkdir -p "${initrd_dir}/${ssh_config_dir}/sshd_config.d"

	sed -i 's/\/bin\/sh/\/bin\/bash/' "${initrd_dir}/sbin/dhclient-script";check_error

	echo 'Port 22' > "${initrd_dir}/${sshd_config_conf}";check_error
	echo 'PermitRootLogin yes' >> "${initrd_dir}/${sshd_config_conf}";check_error
	echo 'PubkeyAuthentication no' >> "${initrd_dir}/${sshd_config_conf}";check_error
	echo 'StrictModes yes' >> "${initrd_dir}/${sshd_config_conf}";check_error
	echo 'PasswordAuthentication yes' >> "${initrd_dir}/${sshd_config_conf}";check_error
	echo 'PermitEmptyPasswords no' >> "${initrd_dir}/${sshd_config_conf}";check_error
	echo 'UsePAM no' >> "${initrd_dir}/${sshd_config_conf}";check_error
	echo 'HostKey /etc/ssh/ssh_host_rsa_key' >> "${initrd_dir}/${sshd_config_conf}";check_error
	echo 'HostKey /etc/ssh/ssh_host_ecdsa_key' >> "${initrd_dir}/${sshd_config_conf}";check_error
	echo 'HostKey /etc/ssh/ssh_host_ed25519_key' >> "${initrd_dir}/${sshd_config_conf}";check_error
	echo 'SyslogFacility AUTH' >> "${initrd_dir}/${sshd_config_conf}";check_error
	echo 'LogLevel INFO' >> "${initrd_dir}/${sshd_config_conf}";check_error
	echo 'LoginGraceTime 2m' >> "${initrd_dir}/${sshd_config_conf}";check_error

	# generate keys
	rm -f "${initrd_dir}/${ssh_config_dir}/"ssh_host_*_key
	rm -f "${initrd_dir}/${ssh_config_dir}/"ssh_host_*_key.pub
	"${SSH_KEYGEN}" -t dsa -N "" -f "${initrd_dir}/${ssh_config_dir}/ssh_host_dsa_key" >/dev/null 2>&1;check_error
	"${SSH_KEYGEN}" -t rsa -N "" -f "${initrd_dir}/${ssh_config_dir}/ssh_host_rsa_key" >/dev/null 2>&1;check_error
	"${SSH_KEYGEN}" -t ecdsa -N "" -f "${initrd_dir}/${ssh_config_dir}/ssh_host_ecdsa_key" >/dev/null 2>&1;check_error
	"${SSH_KEYGEN}" -t ed25519 -N "" -f "${initrd_dir}/${ssh_config_dir}/ssh_host_ed25519_key" >/dev/null 2>&1;check_error

	# default passwd for root is "root"
	local passwd_file="/etc/passwd"
	local shadow_file="/etc/shadow"
	echo "root:x:0:0:root:/root:/bin/bash" >"${initrd_dir}/${passwd_file}"
	echo "sshd:x:104:65534::/run/sshd:/usr/sbin/nologin" >>"${initrd_dir}/${passwd_file}"
	echo "root:\$6\$qYzNFHlg\$M4RG6AtkTS3kj1/Al2WqoRvUxWL9mFRjUadC74qSBhgWtkRjtVtiZJpJvAaG4DEHnJKSnOVwX4fHCt0A7vtXR/:18192:0:99999:7:::" >"${initrd_dir}/${shadow_file}"
	echo "sshd:\*:17669:0:99999:7:::" >>"${initrd_dir}/${shadow_file}"

	# enable shell
	echo -n "/bin/bash" >"${initrd_dir}/etc/shells"
	echo "none /dev/pts devpts gid=5,mode=620 0 0" >"${initrd_dir}/etc/fstab"
}

repack_initrd()
{
	local base_l4t_dir="${1}"
	local base_rootfs_dir="${2}"
	local new_l4t_dir="${3}"
	local new_l4t_ota_dir="${new_l4t_dir}/tools/ota_tools/version_upgrade"
	local work_dir="${WORKDIR}"
	local initrd_tmp_dir="${work_dir}/initrd_tmp"
	local initrd_file_orig="${base_l4t_dir}/bootloader/l4t_initrd.img"
	local base_recovery_initrd_file="${BASE_RECOVERY_INITRD_FILE}"
	local binlist_file="${new_l4t_ota_dir}/${REC_COPY_BINLIST_NAME}"

	if [ ! -f "${initrd_file_orig}" ];then
		error_exit "${initrd_file_orig} is not found"
	fi
	cp -f "${initrd_file_orig}" "${work_dir}/initrd.img.gz"
	check_error "cp -f ${initrd_file_orig} ${work_dir}/initrd.img.gz"
	pushd "${work_dir}" > /dev/null 2>&1 || error_exit "Failed to enter ${work_dir}"

	echo "Unpacking initrd ..."
	gzip -d initrd.img.gz
	check_error "gzip -d initrd.img.gz"
	mkdir "${initrd_tmp_dir}";check_error
	cd "${initrd_tmp_dir}" || error_exit "Failed to enter ${initrd_tmp_dir}"
	cpio -i <../initrd.img
	check_error "cpio -i <../initrd.img"

	# copy neccessary files
	BASE_KERNEL_VERSION="$(strings "${base_l4t_dir}/kernel/Image" | grep -oE "Linux version [0-9a-zA-Z\.\-]+[+]* " | cut -d\  -f 3)"
	if [ -z "${BASE_KERNEL_VERSION}" ]; then
		echo "ERROR: failed to get kernel version from ${base_l4t_dir}/kernel/Image"
		exit 1
	fi
	copy_files "${base_rootfs_dir}" "${new_l4t_ota_dir}" "${initrd_tmp_dir}" "${binlist_file}"

	# support sshd
	prepare_sshd_files "${initrd_tmp_dir}"

	echo "Packing initrd ..."
	find . |  cpio -H newc --create  | gzip -9 >"${BASE_RECOVERY_INITRD_FILE}"
	check_error "find . |  cpio -H newc --create  | gzip -9 >${BASE_RECOVERY_INITRD_FILE}"

	# enable LUKS disk encryption
	if [ "${ROOTFS_ENC}" == 1 ]; then
		source "${new_l4t_ota_dir}"/nv_ota_disk_enc.func
		prepare_disk_encryption "${new_l4t_dir}" "${BASE_RECOVERY_INITRD_FILE}"
	fi

	popd > /dev/null 2>&1 || error_exit "Failed to leave ${work_dir}"
}

sign_image()
{
	local ldk_dir="${1}"
	local pkc_key_file="${2}"
	local user_key_file="${3}"
	local image_file="${4}"
	local chipid="${5}"
	local _ret_signed_file="${6}"
	local sign_image_script="${ldk_dir}/l4t_sign_image.sh"
	local _signed_file=

	_signed_file="$("${sign_image_script}" -q --file "${image_file}" --key "${pkc_key_file}" --encrypt_key "${user_key_file}" --chip "${chipid}" --split "False" --type "data")"
	if [ ! -f "${_signed_file}" ]; then
		return 1
	else
		eval "${_ret_signed_file}=${_signed_file}"
		return 0
	fi
}

build_recovery_image()
{
	local base_l4t_dir="${1}"
	local new_l4t_dir="${2}"
	local pkc_key_file="${3}"
	local user_key_file="${4}"
	local work_dir="${WORKDIR}"
	local recovery_img_file="${work_dir}/recovery.img"
	local base_recovery_initrd_file="${BASE_RECOVERY_INITRD_FILE}"
	local base_kernel_file="${base_l4t_dir}/kernel/Image"
	local _mkbootimg="${base_l4t_dir}/bootloader/mkbootimg"

	if [ ! -f "${base_recovery_initrd_file}" ];then
		error_exit "BASE initrd file ${base_recovery_initrd_file} is not found"
	fi

	if [ ! -f "${base_kernel_file}" ];then
		error_exit "BASE kernel file ${base_kernel_file} is not found"
	fi

	if [ ! -f "${_mkbootimg}" ];then
		error_exit "mkbootimg ${_mkbootimg} is not found"
	else
		if [ ! -x "${_mkbootimg}" ];then
			echo "mkbootimg ${_mkbootimg} is not executable"
			exit 1
		fi
	fi

	# make recovery.img
	local cmdline=
	cmdline="${T194_BASE_RECOVERY_CMDLINE} base_version=${BASE_VERSION} target_board=${TARGET_BOARD} "
	echo "${_mkbootimg} --kernel ${base_kernel_file} --ramdisk ${base_recovery_initrd_file} --output ${recovery_img_file} --cmdline \"${cmdline}\""
	"${_mkbootimg}" --kernel "${base_kernel_file}" --ramdisk "${base_recovery_initrd_file}"\
		--output "${recovery_img_file}" --cmdline "${cmdline}"
	check_error "${_mkbootimg} --kernel ${base_kernel_file} --ramdisk ${base_recovery_initrd_file} --output ${recovery_img_file} --cmdline ${cmdline}"

	# sign recovery.img
	local signed_file=
	if ! sign_image "${new_l4t_dir}" "${pkc_key_file}" "${user_key_file}" "${recovery_img_file}" "${CHIPID}" signed_file; then
		error_exit "Failed to sign ${recovery_img_file}"
	fi

	# rename recovery image
	local base_recovery_image_signed="${BASE_RECOVERY_IMAGE_FILE}"
	echo "Copy ${signed_file} to ${base_recovery_image_signed}"
	mv -f "${signed_file}" "${base_recovery_image_signed}"
	check_error "mv -f ${signed_file} ${base_recovery_image_signed}"

	# generate sha1 check sum for signed recovery image
	local sha1_chksum=
	sha1_chksum="$(sha1sum "${base_recovery_image_signed}" | cut -d\  -f 1)"
	if [ "${sha1_chksum}" = "" ] || [ ${#sha1_chksum} -ne 40 ];then
		error_exit "Failed to run sha1sum ${base_recovery_image_signed}"
	fi
	echo -n "${sha1_chksum}" >${BASE_RECOVERY_IMAGE_SHA1SUM_FILE}
}

append_bootargs_to_dtb()
{
	local kernel_dtbfile="${1}"
	local cmdline="${2}"
	local recovery_dtbfile="${3}"
	local work_dir="${WORKDIR}"
	local tmpdtsfile="${work_dir}/temp.dts"
	local dtc_util="${TOT_L4T_DIR}/kernel/dtc"

	${dtc_util} -I dtb -O dts "${kernel_dtbfile}" -o "${tmpdtsfile}";check_error
	sed -i '/bootargs/d' "${tmpdtsfile}";check_error
	sed -i "/chosen {/ a \\\t\\tbootargs=\"${cmdline} \";" "${tmpdtsfile}";check_error
	${dtc_util} -I dts -O dtb "${tmpdtsfile}" -o "${recovery_dtbfile}";check_error
	rm "${tmpdtsfile}";check_error
}

build_recovery_dtb()
{
	local base_l4t_dir="${1}"
	local new_l4t_dir="${2}"
	local pkc_key_file="${3}"
	local user_key_file="${4}"
	local work_dir="${WORKDIR}"
	local recovery_dtb_file="${work_dir}/recovery.dtb"
	local kernel_dtb_file=
	local cmdline=

	# modify dtb file by adding cmdline
	# for R32.5 or later version, the bootargs is no longer put into dtb
	kernel_dtb_file="${base_l4t_dir}/kernel/dtb/${KERNEL_DTB_NAME}"
	cp "${kernel_dtb_file}" "${recovery_dtb_file}"

	# sign recovery.dtb
	local signed_file=
	if ! sign_image "${new_l4t_dir}" "${pkc_key_file}" "${user_key_file}" "${recovery_dtb_file}" "${CHIPID}" signed_file; then
		error_exit "Failed to sign ${recovery_dtb_file}"
	fi

	# rename recovery dtb
	local base_recovery_dtb_signed="${BASE_RECOVERY_DTB_FILE}"
	echo "Copy ${signed_file} to ${base_recovery_dtb_signed}"
	mv -f "${signed_file}" "${base_recovery_dtb_signed}"
	check_error "mv -f ${signed_file} ${base_recovery_dtb_signed}"

	# generate sha1 check sum for signed recovery dtb
	local sha1_chksum=
	sha1_chksum="$(sha1sum "${base_recovery_dtb_signed}" | cut -d\  -f 1)"
	if [ "${sha1_chksum}" = "" ] || [ ${#sha1_chksum} -ne 40 ];then
		error_exit "Failed to run sha1sum ${base_recovery_dtb_signed}"
	fi
	echo -n "${sha1_chksum}" >${BASE_RECOVERY_DTB_SHA1SUM_FILE}
}

copy_recovery_files()
{
	local new_l4t_dir="${1}"
	local recovery_image="${BASE_RECOVERY_IMAGE_FILE}"
	local recovery_image_sha1sum="${BASE_RECOVERY_IMAGE_SHA1SUM_FILE}"
	local recovery_dtb="${BASE_RECOVERY_DTB_FILE}"
	local recovery_dtb_sha1sum="${BASE_RECOVERY_DTB_SHA1SUM_FILE}"

	if [ ! -f "${recovery_image}" ];then
		error_exit "BASE recovery image ${recovery_image} is not found"
	fi
	if [ ! -f "${recovery_image_sha1sum}" ];then
		error_exit "BASE recovery image sha1sum ${recovery_image_sha1sum} is not found"
	fi
	if [ ! -f "${recovery_dtb}" ];then
		error_exit "BASE recovery dtb ${recovery_dtb} is not found"
	fi
	if [ ! -f "${recovery_dtb_sha1sum}" ];then
		error_exit "BASE recovery dtb sha1sum ${recovery_dtb_sha1sum} is not found"
	fi

	echo "Copy generated recovery image and dtb into ${new_l4t_dir}/bootloader/"
	cp -f "${recovery_image}" "${new_l4t_dir}/bootloader/";check_error
	cp -f "${recovery_image_sha1sum}" "${new_l4t_dir}/bootloader/";check_error
	cp -f "${recovery_dtb}" "${new_l4t_dir}/bootloader/";check_error
	cp -f "${recovery_dtb_sha1sum}" "${new_l4t_dir}/bootloader/";check_error
}


get_dtb_name()
{
	local r32_l4t_dir="${1}"
	local flash_app="${r32_l4t_dir}/flash.sh"
	local board_arg=
	local fuselevel="fuselevel_production"
	local cmd_arg="--no-flash -Z"
	local cmd=
	local cmd_output=/tmp/cmd_output
	local dtbfile=
	local board_specs=

	# For multiple compatible specs, there should be different dtb files for each of them.
	# However, the name of dtb file is obtained from the first vailid compatible spec
	# in this function because:
	# 1. Jetson TX2 only has one compatible spec
	# 2. Jetson Xaiver has two compatible spec, but they use the same kernel dtb
	# If one day a Jetson device does result in different kernel-dtb due to different
	# compatible board spec, the code here will be updated accordingly.
	board_specs="${!BOARD_SPECS_ARRAY}[@]"
	eval "${!board_specs[@]:0:1}"

	board_arg="BOARDID=${boardid} FAB=${fab} BOARDSKU=${boardsku} "
	board_arg+="BOARDREV=${boardrev} FUSELEVEL=${fuselevel}"

	cmd="${board_arg} ${flash_app} ${cmd_arg} ${board} ${rootdev}"
	pushd "${r32_l4t_dir}" || error_exit "Failed to enter ${r32_l4t_dir}"
	echo "${cmd}"
	if eval "${cmd}" > "${cmd_output}"; then
		dtbfile="$(grep -m 1 "dtbfile=" < "${cmd_output}" | cut -d= -f 2)"
		KERNEL_DTB_NAME="$(basename "${dtbfile}")"
		echo -e "\nSUCCESS: get dtbfile name \"${KERNEL_DTB_NAME}\""
	else
		echo -e "\nError: failed to get dtbfile name"
		popd > /dev/null 2>&1 || error_exit
		exit 1
	fi
	popd > /dev/null 2>&1 || error_exit "Failed to leave ${r32_l4t_dir}"
}

is_base_version_valid()
{
	local base_version="${1}"
	local ret=
	case ${base_version} in
	R32-5|R32-6|R32-7|R35-2|R35-3|R35-4|R35-5) ret=0; ;;
	*) ret=1; ;;
	esac
	return $ret
}

# Make sure that this script is running in root privilege
USERID=$(id -u)
if [ "${USERID}" -ne 0 ]; then
       echo "Please run this program as root."
       exit 0
fi

if [ $# -lt 5 ];then
	usage
fi

PKC_KEY_FILE=""
USER_KEY_FILE=""
BASE_VERSION=""
opstr+="u:-:"
while getopts "${opstr}" OPTION; do
	case $OPTION in
	u) PKC_KEY_FILE="${OPTARG}"; ;;
	-) case ${OPTARG} in
	    user_key)
		USER_KEY_FILE="${!OPTIND}";
		OPTIND=$(($OPTIND + 1));
		;;
	    esac;;
	*)
	   usage
	   ;;
	esac;
done

if [ "${PKC_KEY_FILE}" != "" ]; then
	if [ ! -f "${PKC_KEY_FILE}" ];then
		echo "Specified PKC key file \"${PKC_KEY_FILE}\" is not found"
		usage
	fi
	PKC_KEY_FILE="$(readlink -f "${PKC_KEY_FILE}")"
fi

if [ "${USER_KEY_FILE}" != "" ]; then
	if [ ! -f "${USER_KEY_FILE}" ];then
		echo "Specified user key file \"${USER_KEY_FILE}\" is not found"
		usage
	else
		if [ "${PKC_KEY_FILE}" == "" ]; then
			echo "PKC key must be provided once user key is specified"
			usage
		fi
	fi
	USER_KEY_FILE="$(readlink -f "${USER_KEY_FILE}")"
fi

nargs=$#
TOT_L4T_DIR="${!nargs}"
nargs=$((nargs-1))
BASE_ROOTFS_DIR="${!nargs}"
nargs=$((nargs-1))
BASE_L4T_DIR="${!nargs}"
nargs=$((nargs-1))
BASE_VERSION="${!nargs}"
nargs=$((nargs-1))
TARGET_BOARD="${!nargs}"

if [ ! -d "${BASE_L4T_DIR}" ];then
	echo "Invalid BASE Linux_for_Tegra directory ${BASE_L4T_DIR}"
	usage
fi

if [ ! -d "${BASE_ROOTFS_DIR}" ];then
	echo "Invalid BASE rootfs path ${BASE_ROOTFS_DIR}"
	usage
fi

if [ ! -d "${TOT_L4T_DIR}" ];then
	echo "Invalid NEW Linux_for_Tegra directory ${TOT_L4T_DIR}"
	usage
fi

SSH_KEYGEN="$(which ssh-keygen)"
if [ "${SSH_KEYGEN}" = "" ];then
	echo "ERROR: ssh-keygen is not found, please run 'apt-get install openssh-client' to install it"
	usage
fi

# Check the input target board and base version
# If both of them are valid, return the matched board spec;
# Otherwise, report error and exit
BOARD_SPECS_CONFIG_FILE="${TOT_L4T_DIR}/tools/ota_tools/version_upgrade/ota_board_specs.conf"
if [ ! -f "${BOARD_SPECS_CONFIG_FILE}" ]; then
	echo "The board specs configuration file ${BOARD_SPECS_CONFIG_FILE} is not found"
	usage
fi

# Only R32.5, R32.6, R32.7, R35.2, R35.3 and R35.4 are supported
if ! is_base_version_valid "${BASE_VERSION}"; then
	echo "Invalid base bsp version"
	usage
fi

source "${TOT_L4T_DIR}/tools/ota_tools/version_upgrade/ota_validate_params.sh"
if ! ota_validate_params "${TARGET_BOARD}" "${BASE_VERSION}" "internal" "${BOARD_SPECS_CONFIG_FILE}" "BOARD_SPECS_ARRAY" "CHIPID"; then
	echo "Failed to run \"ota_validate_params ${TARGET_BOARD} ${BASE_VERSION} internal ${BOARD_SPECS_CONFIG_FILE} BOARD_SPECS_ARRAY CHIPID\""
	usage
fi

source "${TOT_L4T_DIR}/tools/ota_tools/version_upgrade/nv_ota_common.func"

init

get_dtb_name  "${BASE_L4T_DIR}"

repack_initrd "${BASE_L4T_DIR}" "${BASE_ROOTFS_DIR}" "${TOT_L4T_DIR}"

build_recovery_image "${BASE_L4T_DIR}" "${TOT_L4T_DIR}" "${PKC_KEY_FILE}" "${USER_KEY_FILE}"

build_recovery_dtb "${BASE_L4T_DIR}" "${TOT_L4T_DIR}" "${PKC_KEY_FILE}" "${USER_KEY_FILE}"

copy_recovery_files "${TOT_L4T_DIR}"

rm -Rf "${WORKDIR}"

echo "Finished"

