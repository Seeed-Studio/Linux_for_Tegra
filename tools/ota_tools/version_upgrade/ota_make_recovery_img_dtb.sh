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

# This is a script to make recovery image and dtb for OTA
_T23x_BASE_RECCMDLINE="root=/dev/initrd rw rootwait mminit_loglevel=4 console=ttyTCU0,115200 firmware_class.path=/etc/firmware fbcon=map:0 net.ifnames=0 efi=runtime"
_T26x_BASE_RECCMDLINE="root=/dev/initrd rw rootwait mminit_loglevel=4 earlycon=tegra_utc,mmio32,0xc5a0000 console=ttyUTC0,115200 clk_ignore_unused firmware_class.path=/etc/firmware fbcon=map:0 net.ifnames=0 efi=runtime"
_REC_COPY_BINLIST_FILE="recovery_copy_binlist.txt"
_TMP_LIST_FILE=/tmp/binlist.txt.tmp
_BASE_KERNEL_VERSION=""

check_error()
{
	if [ $? -ne 0 ];then
		if [ "$1" != "" ];then
			echo "failed command: $1"
		else
			echo "command is failed"
		fi
		if [ -f "${_TMP_LIST_FILE}" ];then
			rm -f "${_TMP_LIST_FILE}"
		fi
		exit 1
	fi
}

check_warning()
{
	if [ $? -ne 0 ];then
		if [ "$1" != "" ];then
			echo "warning: $1"
		fi
	fi
}

copy_files()
{
	local _rootfs_dir="${1}"
	local _ota_dir="${2}"
	local _initrd_dir="${3}"
	local _binlist_file="${4}"
	local _tmp_list_file="${_TMP_LIST_FILE}"

	# The folder which contains the libraries
	local _librariesdir
	_librariesdir="$(find -H "${_rootfs_dir}/lib" -name libc.so.6 | tail -1 | awk -F/ '{ print $(NF-1) }')"
	grep -E "^(noble|all)" "${_binlist_file}" | sed \
		-e "s|<ARCH>|${_librariesdir}|g" \
		-e "s|<ROOTFS>|${_rootfs_dir}|g" \
		-e "s|<OTA_DIR>|${_ota_dir}|g" \
		-e "s|<KERNEL_VERSION>|${_BASE_KERNEL_VERSION}|g" >"${_tmp_list_file}"
		check_error "generate binlist file ${_tmp_list_file}"

	local _src=
	local _dst=
	local _dst_dir=
	# Copy all the binary
	while read -r path
	do
		_src="$(echo "${path}" | cut -d ':' -f 2)"
		_dst="$(echo "${path}" | cut -d ':' -f 3)"
		_dst_dir="${_dst%/*}"
		mkdir -p "${_initrd_dir}/${_dst_dir}"
		cp -f "${_src}" "${_initrd_dir}/${_dst}"
		check_warning "cp -f ${_src} ${_initrd_dir}/${_dst}"
	done < "${_tmp_list_file}"
	rm -f "${_tmp_list_file}"
}

prepare_sshd_files()
{
	local _rootfs_dir="${1}"
	local _initrd_dir="${2}"

	local ssh_config_dir="/etc/ssh"
	local sshd_config_conf="${ssh_config_dir}/sshd_config.d/initrd.conf"
	mkdir -p "${_initrd_dir}/${ssh_config_dir}/sshd_config.d"

	sed -i 's/\/bin\/sh/\/bin\/bash/' "${_initrd_dir}/sbin/dhclient-script";check_error

	echo 'Port 22' > "${_initrd_dir}/${sshd_config_conf}";check_error
	echo 'PermitRootLogin yes' >> "${_initrd_dir}/${sshd_config_conf}";check_error
	echo 'PubkeyAuthentication no' >> "${_initrd_dir}/${sshd_config_conf}";check_error
	echo 'StrictModes yes' >> "${_initrd_dir}/${sshd_config_conf}";check_error
	echo 'PasswordAuthentication yes' >> "${_initrd_dir}/${sshd_config_conf}";check_error
	echo 'PermitEmptyPasswords no' >> "${_initrd_dir}/${sshd_config_conf}";check_error
	echo 'UsePAM no' >> "${_initrd_dir}/${sshd_config_conf}";check_error
	echo 'HostKey /etc/ssh/ssh_host_rsa_key' >> "${_initrd_dir}/${sshd_config_conf}";check_error
	echo 'HostKey /etc/ssh/ssh_host_ecdsa_key' >> "${_initrd_dir}/${sshd_config_conf}";check_error
	echo 'HostKey /etc/ssh/ssh_host_ed25519_key' >> "${_initrd_dir}/${sshd_config_conf}";check_error
	echo 'SyslogFacility AUTH' >> "${_initrd_dir}/${sshd_config_conf}";check_error
	echo 'LogLevel INFO' >> "${_initrd_dir}/${sshd_config_conf}";check_error
	echo 'LoginGraceTime 2m' >> "${_initrd_dir}/${sshd_config_conf}";check_error

	# generate keys
	rm -f "${_initrd_dir}/${ssh_config_dir}/"ssh_host_*_key
	rm -f "${_initrd_dir}/${ssh_config_dir}/"ssh_host_*_key.pub
	ssh-keygen -t dsa -N "" -f "${_initrd_dir}/${ssh_config_dir}/ssh_host_dsa_key" >/dev/null 2>&1;check_error
	ssh-keygen -t rsa -N "" -f "${_initrd_dir}/${ssh_config_dir}/ssh_host_rsa_key" >/dev/null 2>&1;check_error
	ssh-keygen -t ecdsa -N "" -f "${_initrd_dir}/${ssh_config_dir}/ssh_host_ecdsa_key" >/dev/null 2>&1;check_error
	ssh-keygen -t ed25519 -N "" -f "${_initrd_dir}/${ssh_config_dir}/ssh_host_ed25519_key" >/dev/null 2>&1;check_error

	# password for root
	local passwd_file="/etc/passwd"
	local shadow_file="/etc/shadow"
	echo "root:x:0:0:root:/root:/bin/bash" >"${_initrd_dir}/${passwd_file}"
	echo "sshd:x:104:65534::/run/sshd:/usr/sbin/nologin" >>"${_initrd_dir}/${passwd_file}"
	echo "root:\$6\$qYzNFHlg\$M4RG6AtkTS3kj1/Al2WqoRvUxWL9mFRjUadC74qSBhgWtkRjtVtiZJpJvAaG4DEHnJKSnOVwX4fHCt0A7vtXR/:18192:0:99999:7:::" >"${_initrd_dir}/${shadow_file}"
	echo "sshd:\*:17669:0:99999:7:::" >>"${_initrd_dir}/${shadow_file}"

	# enable shell
	echo -n "/bin/bash" >"${_initrd_dir}/etc/shells"
	echo "none /dev/pts devpts gid=5,mode=620 0 0" >"${_initrd_dir}/etc/fstab"
}

ota_make_recovery_img()
{
	local _ldk_dir="${1}"
	local _kernel_fs="${2}"
	local _kernelinitrd="${3}"
	local _localrecfile="${4}"
	local _chipid="${5}"
	local _enable_disk_enc="${6}"
	local _ota_dir="${_ldk_dir}/tools/ota_tools/version_upgrade"
	local _bl_dir="${_ldk_dir}/bootloader"
	local _rootfs_dir="${_ldk_dir}/rootfs"
	local _binlist_file="${_ota_dir}/${_REC_COPY_BINLIST_FILE}"
	local _initrd_int_support="${_ota_dir}/initrd_package_int.sh"

	# Skip re-generating recovery image if required and it has
	# been generated
	if [ "${SKIP_REC_IMG}" == "1" ] && [ -s "${_bl_dir}/${_localrecfile}" ]; then
		echo -n -e "Skip re-generating recovery image...\n"
		return 0
	fi

	# REC_TAG: Recovery image
	#
	echo -n -e "Making recovery ramdisk for recovery image...\n"
	ramdiskfile="${_bl_dir}/recovery.ramdisk"

	# Add necessary binaries into initrd
	echo -n -e "Re-generating recovery ramdisk for recovery image...\n"
	cp -f "${_kernelinitrd}" "${_kernelinitrd}.cpio.gz"
	check_error "cp -f ${_kernelinitrd} ${_kernelinitrd}.cpio.gz"
	gzip -f -d "${_kernelinitrd}.cpio.gz"
	check_error "gzip -f -d ${_kernelinitrd}.cpio.gz"
	if [ -d "ramdisk_tmp" ];then
		rm -Rf "ramdisk_tmp"
	fi
	local CWD="ramdisk_tmp"
	mkdir "${CWD}";check_error
	pushd "${CWD}" || exit 1
	cpio -idu < "${_kernelinitrd}.cpio"
	check_error "cpio -idu < ${_kernelinitrd}.cpio"

	# copy neccessary files
	local _initrd_dir=
	_initrd_dir="$(pwd)"
	if file "${_kernel_fs}" | grep -q 'gzip compressed data'; then
		if  gzip -t "${_kernel_fs}" ; then
			# decompress it if using compressed "Image.gz"
			gzip -d "${_kernel_fs}" -c >/tmp/Image.tmp
			check_error "Failed to decompress ${_kernel_fs}"
			_kernel_fs="/tmp/Image.tmp"
		fi
	fi
	_BASE_KERNEL_VERSION="$(strings "${_kernel_fs}" | grep -oE "Linux version [0-9a-zA-Z\.\-]+[+]* " | cut -d\  -f 3 | head -1)"
	if [ -z "${_BASE_KERNEL_VERSION}" ]; then
		echo "ERROR: failed to get kernel version from ${_kernel_fs}"
		exit 1
	fi
	echo "_BASE_KERNEL_VERSION=${_BASE_KERNEL_VERSION}"
	copy_files "${_rootfs_dir}" "${_ota_dir}" "${_initrd_dir}" "${_binlist_file}"

	# Copy additional bins/libs to initrd image, if applicable
	if [ -e "${_initrd_int_support}" ]; then
		source "${_initrd_int_support}"
	fi

	# enable sshd
	prepare_sshd_files "${_rootfs_dir}" "${_initrd_dir}"

	find . |  cpio -H newc --create  | gzip -9 > "${ramdiskfile}"
	check_error "find . |  cpio -H newc --create  | gzip -9 > ${ramdiskfile}"
	popd  > /dev/null 2>&1 || exit 1
	rm -f "${_kernelinitrd}.cpio";check_error
	rm -rf ramdisk_tmp;check_error

	# enable LUKS disk encryption
	if [ "${_enable_disk_enc}" -eq 1 ]; then
		source "${_ota_dir}"/nv_ota_disk_enc.func
		prepare_disk_encryption "${_ldk_dir}" "${ramdiskfile}"
	fi

	local _mkbootimg="${_bl_dir}/mkbootimg"
	local _cmdline=
	if [ "${CHIPID}" = 0x23 ]; then
		_cmdline="${_T23x_BASE_RECCMDLINE}"
	else
		_cmdline="${_T26x_BASE_RECCMDLINE}"
	fi
	echo -n -e "Making Recovery image...\n"
	"${_mkbootimg}" --kernel "${_kernel_fs}" --ramdisk "${ramdiskfile}" \
		--output "${_bl_dir}/${_localrecfile}" --cmdline "${_cmdline}" > /dev/null 2>&1
	check_error "${_mkbootimg} --kernel ${_kernel_fs} --ramdisk ${ramdiskfile} --output ${_bl_dir}/${_localrecfile} --cmdline \"${_cmdline}\""
}

ota_make_recovery_dtb()
{
	local _ldk_dir="${1}"
	local _recdtbfilename="${2}"
	local _bl_dir="${_ldk_dir}/bootloader"

	# RECDTB_TAG: Recovery Kernel DTB
	#
	if [ "$(type -t cp2local)" != "function" ] || [ "${recdtbfile}" == "" ];then
		echo "ERROR: cp2local function is not defined or recdtbfile is null"
		exit 1
	else
		cp2local recdtbfile "${_bl_dir}/${_recdtbfilename}"
	fi
}
