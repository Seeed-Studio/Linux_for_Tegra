#!/bin/bash

# Copyright (c) 2019-2021, NVIDIA CORPORATION.  All rights reserved.
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
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

# This is a script to handle the exception in OTA process
_OTA_LOG_FILE=
_OTA_RETRY_COUNT_FILE=
_OTA_MAX_RETRY_COUNT=
_OTA_SUCCESS=

source "/bin/nv_ota_internals.sh"

reboot_system()
{
	ota_log "Rebooting system..."
	echo b >/proc/sysrq-trigger
}

clean_up()
{
	if [ -f "${_OTA_RETRY_COUNT_FILE}" ];then
		rm "${_OTA_RETRY_COUNT_FILE}"
	fi
	if [ -f "${_OTA_RETRY_COUNT_FILE}.last" ];then
		rm "${_OTA_RETRY_COUNT_FILE}.last"
	fi

	rm -f "${OTA_DECLARE_TMPFILE}"

	# 1. Change to root directory
	# 2. Delete all the ota related files
	# 3. Unmount the device that contains these files
	cd / || exit 1
	rm -Rf "${_OTA_PACKAGE_MOUNTPOINT}"/ota_*
	sync
	umount -l "${_OTA_PACKAGE_MOUNTPOINT}"
	_OTA_SUCCESS=1
}

enable_wifi()
{
	local timeout=30

	cd /bin/ || return 1
	ln -s kmod insmod
	cd - || return 1
	local kernel_ver=
	kernel_ver="$(dmesg | grep -o -E "Linux version [0-9.]+-tegra" | cut -d\  -f 3)"
	local kernel_modules_dir="/lib/modules/${kernel_ver}/kernel"
	local wlan_conf_file="/etc/wpa_supplicant.conf"
	if [ -f "${kernel_modules_dir}/net/wireless/lib80211.ko" ]; then
		insmod "${kernel_modules_dir}/net/wireless/lib80211.ko"
	fi
	if [ -f "${kernel_modules_dir}/net/wireless/lib80211.ko" ]; then
		insmod "${kernel_modules_dir}/net/wireless/cfg80211.ko"
	fi
	if ! insmod "${kernel_modules_dir}/drivers/net/wireless/bcmdhd/bcmdhd.ko"; then
		ota_log "Failed to install wifi module"
		return 1
	fi
	if ! ifconfig wlan0 >/dev/null 2>&1; then
		ota_log "No wifi device exists"
		return 1
	fi
	if ifconfig wlan0 up; then
		{
			echo
			echo -ne "network={\n"
			echo -ne "\tssid=\"frtest\"\n"
			echo -ne "\tpsk=\"12345678\"\n"
			echo -ne "}\n"
		} >>"${wlan_conf_file}"
		wpa_supplicant -B -i wlan0 -c "${wlan_conf_file}"
		dhclient wlan0 -nw -v -lf /etc/dhcp_wpa.lease
		timeout=30
		while [ ${timeout} -gt 0 ]
		do
			if [ -s "/etc/dhcp_wpa.lease" ];then
				tmp="$(grep "fixed-address" < /etc/dhcp_wpa.lease)"
				if [ -z "${tmp}" ];then
					ota_log "ERROR: wlan0: Failed to obtain ip address"
				else
					tmp="$(echo "${tmp}" | cut -d\  -f 2)"
					ota_log "wlan0: Obtained IP address: ${tmp}"
					break
				fi
			fi
			sleep 1
			timeout=$((timeout - 1))
		done
		if [ "${timeout}" -eq 0 ];then
			ota_log "ERROR: wlan0: timeout for obtaining IP address through DHCP"
			return 1
		fi
	else
		ota_log "ERROR: failed to enable wlan0"
		return 1
	fi
	return 0
}

enable_remote_access()
{
	local tmp=
	local timeout=30 # 30 seconds
	local eth_ready=0
	local wlan_ready=0
	ota_log "enable remote access"

	mkdir -p /var/run

	# enable eth0
	if ifconfig eth0 up; then
		dhclient eth0 -nw -v -lf /etc/dhcp.lease
		while [ ${timeout} -gt 0 ]
		do
			if [ -s "/etc/dhcp.lease" ];then
				tmp="$(grep "fixed-address" < /etc/dhcp.lease)"
				if [ -z "${tmp}" ];then
					ota_log "ERROR: eth0: Failed to obtain ip address"
				else
					tmp="$(echo "${tmp}" | cut -d\  -f 2)"
					ota_log "eth0: Obtained IP address: ${tmp}"
					eth_ready=1
					break
				fi
			fi
			sleep 1
			timeout=$((timeout - 1))
		done
		if [ ${timeout} -eq 0 ];then
			ota_log "ERROR: eth0: timeout for obtaining IP address through DHCP"
		fi
	else
		ota_log "ERROR: failed to enable eth0"
	fi

	# enable wlan0
	if enable_wifi; then
		wlan_ready=1
	fi

	if [ "${eth_ready}" = "0" ] && [ "${wlan_ready}" = "0" ];then
		ota_log "ERROR: Network(ethernet & wireless) is not available, rebooting system"
		return 1
	fi

	local pts_dir="/dev/pts"
	if [ ! -d "${pts_dir}" ];then
		mkdir "${pts_dir}"
	fi
	mount "${pts_dir}"
	mkdir -p /run/sshd
	/bin/sshd -E /tmp/sshd.log
	return 0
}

reach_ota_max_retry()
{
	local retry_count_file="${1}"
	local retry_count=0

	if [ -f "${retry_count_file}" ];then
		ota_log "OTA retry count file is at ${retry_count_file}"
		retry_count="$(cat "${retry_count_file}")"
		ota_log "OTA retries ${retry_count} time(s)"
	fi

	if [ "${retry_count}" -lt "${_OTA_MAX_RETRY_COUNT}" ];then
		retry_count=$((retry_count + 1))
		echo -n "${retry_count}" >"${retry_count_file}"
		sync
		ota_log "Retrying OTA for the ${retry_count} times"
		return 0
	else
		ota_log "Reached OTA max retries (${retry_count} times)"
		return 1
	fi
}

exception_handler()
{
	if [ "${_OTA_SUCCESS}" = "1" ];then
		ota_log "OTA is successfully completed"
		reboot_system
	fi

	set +e
	if reach_ota_max_retry "${_OTA_RETRY_COUNT_FILE}"; then
		ota_log "Reboot system to try again"
		cp "${_OTA_LOG_FILE}" "${_OTA_LOG_FILE}.FAIL"
		rm "${_OTA_LOG_FILE}"
		rm -f "${OTA_DECLARE_TMPFILE}"
		sync
		cd / || exit 1
		umount -l "${_OTA_PACKAGE_MOUNTPOINT}"
		reboot_system
	else
		ota_log "ERROR: Failed to run command and keep in recovery mode to waiting for check"
		cp "${_OTA_LOG_FILE}" "${_OTA_LOG_FILE}.FAIL"
		rm "${_OTA_LOG_FILE}"
		if [ -f "${_OTA_RETRY_COUNT_FILE}" ]; then
			cp "${_OTA_RETRY_COUNT_FILE}" "${_OTA_RETRY_COUNT_FILE}.last"
			rm "${_OTA_RETRY_COUNT_FILE}"
		fi
		sync
		cd / || exit 1
		umount -l "${_OTA_PACKAGE_MOUNTPOINT}"
		/bin/bash
	fi
}

init_exception_handler()
{
	local ota_package_mnt="${1}"
	local ota_log_file="${2}"
	local max_retry_count="${3}"

	if [ "${ota_package_mnt}" = "" ] || [ ! -d "${ota_package_mnt}" ];then
		ota_log "ERROR: Invalid ota package mount point ${ota_package_mnt}"
		return 1
	fi

	if [ "${ota_log_file}" = "" ] || [ ! -f "${ota_log_file}" ];then
		ota_log "ERROR: Invalid ota log file ${ota_log_file}"
		return 1
	fi

	_OTA_PACKAGE_MOUNTPOINT=${ota_package_mnt}
	_OTA_LOG_FILE=${ota_log_file}
	_OTA_RETRY_COUNT_FILE=${ota_package_mnt}/ota_retry_count
	_OTA_MAX_RETRY_COUNT=${max_retry_count}

	# register exception_hanlder to handle signals
	trap exception_handler EXIT SIGHUP SIGINT SIGTERM

	# initialize _OTA_SUCCESS
	_OTA_SUCCESS=0
}
