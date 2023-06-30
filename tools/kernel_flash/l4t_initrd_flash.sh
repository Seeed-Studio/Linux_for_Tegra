#!/bin/bash

# Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
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

# Usage: ./l4t_initrd_flash.sh <options> [ --external-device <ext> -c <cfg> -S <SIZE> ] <target> <rootfs_dir>
# This script flashes the target using initrd

set -eo pipefail
trap cleanup EXIT

clean_up_network_flash()
{
	if [ -n "${network}" ]; then
		if [ -f /etc/exports ]; then
			sed -i -e '/^# Entry added by NVIDIA initrd flash tool/,+1d' /etc/exports
		fi
		if command -v exportfs &> /dev/null; then
			exportfs -ra
		fi
	fi
}

cleanup()
{
	remove_udev_rules
	clean_up_network_flash
}

install_udev_rules()
{
	ln -s "$(realpath "${UDEV_L4T_DIR}/99-l4t-host.rules")" /etc/udev/rules.d/
	ln -s "$(realpath "${UDEV_L4T_DIR}/10-l4t-usb-msd.rules")" /etc/udev/rules.d/
}

remove_udev_rules()
{
	rm -f /etc/udev/rules.d/99-l4t-host.rules
	rm -f /etc/udev/rules.d/10-l4t-usb-msd.rules
}

fill_devpaths()
{
	# Find devices to flash
	devpaths=($(find /sys/bus/usb/devices/usb*/ -name devnum -print0 | {
		found=()
		while read -r -d "" fn_devnum; do
			dir="$(dirname "${fn_devnum}")"
			vendor="$(cat "${dir}/idVendor")"
			if [ "${vendor}" != "0955" ]; then
				continue
			fi
			product="$(cat "${dir}/idProduct")"
			case "${product}" in
			"7018") ;; # TX2i
			"7418") ;; # TX2 4GB
			"7c18") ;; # TX2, TX2 NX
			"7019") ;; # AGX Xavier
			"7819") ;; # AGXi
			"7919") ;; # AGXi
			"7023") ;; # AGX Orin
			"7223") ;; # AGX Orin 32GB
			"7323") ;; # Orin NX 16GB (p3767-0000)
			"7423") ;; # Orin NX 8GB (p3767-0001)
			"7523") ;; # Orin Nano 8GB (p3767-0003)
			"7623") ;; # Orin Nano 4GB (p3767-0004)
			"7e19") ;; # NX
			*)
				continue
				;;
			esac
			fn_busnum="${dir}/busnum"
			if [ ! -f "${fn_busnum}" ]; then
				continue
			fi
			fn_devpath="${dir}/devpath"
			if [ ! -f "${fn_devpath}" ]; then
				continue
			fi
			# Only include devices for which the DEVNAME exists. In a container
			# environment, the DEVNAME for this device may not have been mapped
			# in, which is the case for when a device is in recovery mode, but
			# that device is not mapped into the current container.
			devname=$(udevadm info --query=property "$dir" | grep DEVNAME | cut -d= -f2)
			if [ ! -e "$devname" ]; then
				continue
			fi

			busnum="$(cat "${fn_busnum}")"
			devpath="$(cat "${fn_devpath}")"
			if [ -n "${usb_instance}" ] && [ "${usb_instance}" != "${busnum}-${devpath}" ]; then
				continue
			else
				found+=("${busnum}-${devpath}")
			fi
		done
		echo "${found[@]}"
	}))
	# Handle the "direct" device logically no difference than a device that is connected on Jetson.
	# So add the "direct" device to the Jetson device list.
	if [ -n "${direct}" ]; then
		devpaths+=("direct")
	fi
}

L4T_INITRD_FLASH_DIR="$(cd "$(dirname "${0}")" && pwd)"
L4T_TOOLS_DIR="${L4T_INITRD_FLASH_DIR%/*}"
LINUX_BASE_DIR="${L4T_TOOLS_DIR%/*}"
showlogs=0
flash_only="0"
initrd_only=""
reuse_package=""
external_only=""
no_flash="0"
target_partname=""
max_massflash=""
keep=""
network=""
direct=""
reuse=""
export initrd_flash_step=1
UDEV_L4T_DIR="${L4T_INITRD_FLASH_DIR}/host_udev"
flash_cmd="${L4T_INITRD_FLASH_DIR}/l4t_initrd_flash_internal.sh"
source "${L4T_INITRD_FLASH_DIR}"/l4t_initrd_flash.func
parse_param "$@"

remove_udev_rules

install_udev_rules

fill_devpaths

if [ "${flash_only}" = "0" ]; then
	# Generate flash images for both single flash and massflash
	echo "${flash_cmd} --no-flash $*"
	"${flash_cmd}" --no-flash "$@"
	echo "Finish generating flash package."
fi

if [ "${no_flash}" = "1" ]; then
	echo "Put device in recovery mode, run with option --flash-only to flash device."
	exit 0
fi

# Exit if no devices to flash
if [ "${#devpaths[@]}" -eq 0 ]; then
	echo "No devices to flash"
	exit 1
fi

# If we got here, that means user is doing flash in a single command. Therefore,
# check this condition.
if [ "${flash_only}" = "0" ]  && [ -z "${direct}" ]; then
   if [ ${#devpaths[@]} -gt "1" ]; then
	echo "Error, too many devices in RCM mode"
	echo "For signing and flashing, only one device is supported"
	exit 1
   fi
fi

flash_param="$(cat "${L4T_INITRD_FLASH_DIR}/${INITRD_FLASHPARAM}")"
target_board=$(awk -F" " '{print $(NF-1)}' "${L4T_INITRD_FLASH_DIR}/${INITRD_FLASHPARAM}")
CHIPID=$(LDK_DIR=${LINUX_BASE_DIR}; source "${LDK_DIR}/${target_board}.conf";echo "${CHIPID}")
ts=$(date +%Y%m%d-%H%M%S);
instance=0
get_max_flash

if [ ${#devpaths[@]} -gt "${max_massflash}" ]  && [ -z "${direct}" ]; then
	echo "Too many devices in RCM mode"
	exit 1
fi

mkdir -p "${LINUX_BASE_DIR}/initrdlog/"
for devpath in "${devpaths[@]}"; do
	fn_log="${LINUX_BASE_DIR}/initrdlog/flash_${devpath}_${instance}_${ts}.log"
	cmdarg=()
	[ -n "${keep}" ] && cmdarg+=("--keep")
	[ -n "${reuse}" ] && cmdarg+=("--reuse")
	[ -n "${initrd_only}" ] && cmdarg+=("--initrd")
	[ -n "${reuse_package}" ] && cmdarg+=("--use-backup-image")
	[ -n "${external_only}" ] && cmdarg+=("${external_only}")
	[ -n "${target_partname}" ] && cmdarg+=("-k" "${target_partname}")
	[ -n "${network}" ] && cmdarg+=("--network" "${network}")
	[ -n "${direct}" ] && cmdarg+=("--direct" "${direct}")
	if [[ "${CHIPID}" = "0x19" && "${flash_only}" = "0" && -n "${FAB}" ]]; then
		# For T194, if FAB has already been defined, T194 must not read
		# uid again otherwise, it will hang.
		cmdarg+=("--skipuid")
	fi
	cmd="${flash_cmd} ${cmdarg[*]} --usb-instance ${devpath} --device-instance ${instance} --flash-only ${flash_param}";
	echo "${cmd}"
	if [ "${max_massflash}" -eq 1 ] || [ -n "${direct}" ]; then
		eval "${cmd}" 2>&1 | tee "${fn_log}"
		echo "Log is saved to Linux_for_Tegra/initrdlog/flash_${devpath}_${instance}_${ts}.log "
		exit
	else
		eval "${cmd}" > "${fn_log}" 2>&1 &
	fi
	flash_pid="$!";
	flash_pids+=("${flash_pid}")
	echo "Start flashing device: ${devpath}, rcm instance: ${instance}, PID: ${flash_pid}";
	echo "Log will be saved to Linux_for_Tegra/initrdlog/flash_${devpath}_${instance}_${ts}.log "
	if [ "${showlogs}" -eq 1 ]; then
		gnome-terminal -- /bin/bash -c "tail -f ${fn_log}" -t "${fn_log}" > /dev/null 2>&1 &
	fi;
	if [ "${max_massflash}" -eq 1 ]; then
		break
	fi
	instance=$((instance + 1))
done

 # Wait until all flash processes done
failure=0
while true; do
	running=0
	if [ ${showlogs} -ne 1 ]; then
		echo -n "Ongoing processes:"
	fi;
	new_flash_pids=()
	for flash_pid in "${flash_pids[@]}"; do
		if [ -e "/proc/${flash_pid}" ]; then
			if [ ${showlogs} -ne 1 ]; then
				echo -n " ${flash_pid}"
			fi;
			running=$((running + 1))
			new_flash_pids+=("${flash_pid}")
		else
			wait "${flash_pid}" || failure=1
		fi
	done
	if [ "${showlogs}" -ne 1 ]; then
		echo
	fi;
	if [ "${running}" -eq 0 ]; then
		break
	fi
	flash_pids=("${new_flash_pids[@]}")
	sleep 5
done

if [ ${failure} -ne 0 ]; then
	echo "Flash complete (WITH FAILURES)";
	exit 1
fi
