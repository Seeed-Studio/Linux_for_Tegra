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

# Usage: ./l4t_initrd_flash_internal.sh [ --external-device <ext> -c <cfg> -S <SIZE> ] <target> <rootfs_dir>
# This script contains the core functionality of initrd flash

set -eo pipefail

L4T_INITRD_FLASH_DIR="$(cd "$(dirname "${0}")" && pwd)"
L4T_TOOLS_DIR="${L4T_INITRD_FLASH_DIR%/*}"
LINUX_BASE_DIR="${L4T_TOOLS_DIR%/*}"
BOOTLOADER_DIR="${LINUX_BASE_DIR}/bootloader"
ROOTFS_DIR="${LINUX_BASE_DIR}/rootfs"
# Change this if you want to use a different Rootfs for initrd nfs flash
NFS_ROOTFS_DIR="${ROOTFS_DIR}"
BOOT_CTRL_CONF="${ROOTFS_DIR}/etc/nv_boot_control.conf"
NFS_IMAGES_DIR="${L4T_INITRD_FLASH_DIR}/images"
INITRDDIR_L4T_DIR="${L4T_INITRD_FLASH_DIR}/initrd_flash"
KERNEL_FLASH_SCRIPT=""
FLASH_IMG_MAP="initrdflashimgmap.txt"
nargs=$#;
target_rootdev=${!nargs};
nargs=$((nargs-1));
target_board=${!nargs};
working_dir=$(mktemp -d)
TEMP_INITRD_FLASH_DIR=""
error_message=
CHIPID=$(LDK_DIR=${LINUX_BASE_DIR}; source "${LDK_DIR}/${target_board}.conf";echo "${CHIPID}")

CREATE_FLASH_SCRIPT="${L4T_INITRD_FLASH_DIR}/l4t_create_images_for_kernel_flash.sh"
export PATH="${L4T_INITRD_FLASH_DIR}/bin:${PATH}"

trap cleanup EXIT

cleanup()
{
	if [ -n "${error_message}" ]; then
		echo -e "${error_message}"
	fi
	echo "Cleaning up..."
	clean_dev_folder
	[ -z "${keep}" ] && rm -rf "${working_dir}"
	if [ -n "${device_instance}" ]; then
		DEST_FOLDER=${LINUX_BASE_DIR}/temp_initrdflash
		[ -z "${keep}" ] && [ -z "${reuse}" ] && rm -rf "${DEST_FOLDER}/bootloader${device_instance}"
	fi
	if [ -n "${keep}" ]; then
		echo "Keeping working dir at ${DEST_FOLDER}/bootloader${device_instance} and ${working_dir}"
	fi
}

clean_dev_folder()
{
	if [ -f "/dev/${mmcblk0}" ]; then
			rm "/dev/${mmcblk0}"
	fi
	if [ -f "/dev/${mmcblk0boot0}" ]; then
			rm "/dev/${mmcblk0boot0}"
	fi
	if [ -f "/dev/${mmcblk0boot1}" ]; then
			rm "/dev/${mmcblk0boot1}"
	fi
	if [ -f "/dev/${ext}" ]; then
			rm "/dev/${ext}"
	fi
}

check_prerequisite()
{

	if ! command -v sshpass &> /dev/null && [ -z "${direct}" ]
	then
		echo "ERROR sshpass not found! To install - please run: " \
				"\"sudo apt-get install sshpass\""
		exit 1
	fi

	if ! command -v abootimg &> /dev/null && [ "${flash_only}" = "0" ]
	then
		echo "ERROR abootimg not found! To install - please run: " \
				"\"sudo apt-get install abootimg\""
		exit 1
	fi

	if [ -n "${external_device}" ] && [ "${flash_only}" = "0" ]; then
		if [ -z "${config_file}" ]; then
			echo "Flashing external device requires -c option to specify device partition layout"
			exit 1
		fi
	fi
	# Temporary disable until secureboot rcm boot is fixed - Bug 200727134
	if ! [ -f "${BOOTLOADER_DIR}/odmsign.func" ]  && [ "${flash_only}" = "0" ]; then
		echo "Please install the Secureboot package to use initrd flash for fused board"
		# exit 1
	fi

	if [ -n "${network}" ]; then
		network_prerequisite "${network}" "${LINUX_BASE_DIR}" "${NFS_ROOTFS_DIR}"
	fi

}

# We used to use "udevadm info --query=property" to get stuff like ID_VENDOR,
# ID_MODEL, ID_VENDOR_ID, ID_MODEL_ID, and ID_SERIAL_SHORT. However, in an lxc
# container, these environment properties are not reported for devices that
# are mapped into the container.
#
# However, these same values are available as device attributes along the
# device hierarchy, and this does work within an lxc container. Therefore, we
# switch to using "udevadm info --attribute-walk" to get these values.
#
# The device attributes that correspond to the environment properties are,
# respectively, vendor, model, idVendor, idProduct, and serial.
get_udev_attribute()
{
	path=$1
	attr=$2

	properties=$(flock -w 60 /var/lock/nvidiainitrdflash udevadm info --attribute-walk "$path")
	echo "${properties}" | sed -n "0,/^[ ]*ATTRS{$attr}==\"\(.*\)\"\$/s//\1/p" | xargs
}

generate_flash_package()
{
	local cmd
	cmd=("${CREATE_FLASH_SCRIPT}")
	cmd+=("--no-flash" "-t")
	if [ -n "${external_device}" ]; then
		cmd+=("--external-device" \
		"${external_device}" "-c" "${config_file}")
		if [ -n "${external_size}" ]; then
			cmd+=("-S" "${external_size}")
		fi
	fi

	if [ -n "${append}" ]; then
		cmd+=("--append")
	fi

	if [ -n "${external_only}" ]; then
		cmd+=("${external_only}")
	fi

	if [ -n "${OPTIONS}" ]; then
		cmd+=("-p" "${OPTIONS}")
	fi

	if [ -n "${KEY_FILE}" ] && [ -f "${KEY_FILE}" ]; then
		cmd+=("-u" "${KEY_FILE}")
	fi

	if [ -n "${SBK_KEY}" ] && [ -f "${SBK_KEY}" ]; then
		cmd+=("-v" "${SBK_KEY}")
	fi

	if [ -n "${user_key}" ] && [ -f "${user_key}" ]; then
		cmd+=("--user_key" "${user_key}")
	fi

	if [ -n "${pv_crt}" ] && [ -f "${pv_crt}" ]; then
		cmd+=("--pv-crt" "${pv_crt}")
	fi

	[ "${sparse_mode}" = "1" ] && cmd+=("--sparse")
	cmd+=("${target_board}" "${target_rootdev}")

	"${cmd[@]}"
}

function get_disk_name
{
	local ext_dev="${1}"
	local disk=
	# ${ext_dev} could be specified as a partition; therefore, removing the
	# number if external storage device is scsi, otherwise, remove the trailing
	# "p[some number]" here
	if [[ "${ext_dev}" = sd* ]]; then
		disk=${ext_dev%%[0-9]*}
	else
		disk="${ext_dev%p*}"
	fi
	echo "${disk}"
}

build_working_dir()
{

	local device_instance=${1}
	DEST_FOLDER=${LINUX_BASE_DIR}/temp_initrdflash

	mkdir -p "${DEST_FOLDER}"

	TEMP_INITRD_FLASH_DIR="${DEST_FOLDER}/bootloader${device_instance}"

	if [ -z "${reuse}" ]; then
		echo "Create flash environment ${device_instance}"

		copy_bootloader "${TEMP_INITRD_FLASH_DIR}/" "${CHIPID}" "$(cat "${BOOTLOADER_DIR}/flashcmd.txt")"


		echo "Finish creating flash environment ${device_instance}."
	else
		echo "Reuse flash environment ${device_instance}"
	fi

}

generate_rcm_bootcmd()
{
	local cmd
	local cmdarg=

	if [ -n "${KEY_FILE}" ] && [ -f "${KEY_FILE}" ]; then
		cmdarg+="-u \"${KEY_FILE}\" "
	fi

	if [ -n "${SBK_KEY}" ] && [ -f "${SBK_KEY}" ]; then
		cmdarg+="-v \"${SBK_KEY}\" "
	fi

	if [ -n "${user_key}" ] && [ -f "${user_key}" ]; then
		cmdarg+="--user_key \"${user_key}\" "
	fi
	export BOARDID
	export FAB
	export BOARDSKU
	export BOARDREV
	export CHIP_SKU
	export RAMCODE_ID
	export RAMCODE
	cmd="${LINUX_BASE_DIR}/flash.sh ${cmdarg} --no-flash --rcm-boot ${target_board} mmcblk0p1"
	echo "${cmd}"
	eval "${cmd}"

	cmd=()

	if [ -n "${append}" ]; then
		# restore external_device var when append option is specified
		if [ -f "${NFS_IMAGES_DIR}/external/flash.cfg" ]; then
			external_device="$(source "${NFS_IMAGES_DIR}/external/flash.cfg"; echo "${external_device}")"
		fi
	fi

	if [ -n "${external_device}" ]; then
		cmd+=("--external-device" \
		"${external_device}" "-c" "\"${config_file}\"")
		if [ -n "${external_size}" ]; then
			cmd+=("-S" "${external_size}")
		fi
	fi

	if [ -n "${target_partname}" ]; then
		cmd+=("-k" "${target_partname}")
	fi

	if [ -n "${initrd_only}" ]; then
		cmd+=("--initrd")
	fi

	if [ -n "${direct}" ]; then
		cmd+=("--direct" "${direct}")
	fi

	if [ -n "${network}" ]; then
		cmd+=("--network" "${network}")
	fi

	echo "${cmd[*]} ${target_board} ${target_rootdev}" > "${L4T_INITRD_FLASH_DIR}/${INITRD_FLASHPARAM}"
	echo "Save initrd flashing command parameters to ${L4T_INITRD_FLASH_DIR}/${INITRD_FLASHPARAM}"
}

ping_device()
{
	while IFS=  read -r; do
		netpath=/sys/class/net/${REPLY}
		netserialnumber=$(get_udev_attribute "${netpath}" serial)
		if [ "${netserialnumber}" = "${serialnumber}" ]; then
			echo "${REPLY}" > "${sshcon}"
			ip a add fc00:1:1:"${device_instance}"::1/64 dev "${REPLY}"
			ip a add fe80::2 dev "${REPLY}"
			ip link set dev "${REPLY}" up
		fi
	done < <(ls /sys/class/net)

	if [ -z "$(cat "${sshcon}")" ]; then
		return 1
	fi
	if ! ping6 -c 1 "fe80::1%$(cat "${sshcon}")" > /dev/null 2>&1;
	then
		return 1
	fi
	return 0
}

run_commmand_on_target()
{
	local OLD_LC_ALL="${LC_ALL}"
	local OLD_LANG="${LANG}"
	local OLD_LANGUAGE="${LANGUAGE}"
	export LC_ALL="" LANG="en_US.UTF-8" LANGUAGE=""
	echo "Run command: ${2} on root@fe80::1%${1}"
	count=0
	maxcount=10
	while ! sshpass -p root ssh "root@fe80::1%${1}" "${SSH_OPT[@]}" "echo SSH ready"
	do
		count=$((count + 1))
		if [ "${count}" -ge "${maxcount}" ]; then
			echo "SSH is not ready"
			return 1
		fi
		sleep 1
	done
	sshpass -p root ssh "${SSH_OPT[@]}" "root@fe80::1%${1}" "$2";
	result=$?
	export LC_ALL="${OLD_LC_ALL}" LANG="${OLD_LANG}" LANGUAGE="${OLD_LANGUAGE}"
	return $result
}

copy_qspi_flash_packages()
{
	if ! grep -qE "^[0-9]+, 3:0" "${NFS_IMAGES_DIR}/internal/flash.idx"; then
		return
	fi
	mkdir -p "${working_dir}/initrd/qspi/internal"

	for i in $(grep -E "^[0-9]+, 3:0" "${NFS_IMAGES_DIR}/internal/flash.idx" | sed 's/[ \t]\?,[ \t]\?/,/g' | awk -F"," '{print $5}' | uniq | grep -v "^$");
	do
		cp "${NFS_IMAGES_DIR}/internal/${i}" "${working_dir}/initrd/qspi/internal"
	done

	cp "${NFS_IMAGES_DIR}/internal/flash.idx" "${working_dir}/initrd/qspi/internal"
	cp "${NFS_IMAGES_DIR}/internal/flash.cfg" "${working_dir}/initrd/qspi/internal"
	cp "${NFS_IMAGES_DIR}/${KERNEL_FLASH_SCRIPT}" "${working_dir}/initrd/qspi"
}

generate_flash_initrd()
{
	local dev_instance="$1"

	pushd "${working_dir}"

	abootimg -x "${BOOTLOADER_DIR}/recovery.img"

	mkdir -p "${working_dir}/initrd"

	pushd "${working_dir}/initrd"

	gunzip -c "${working_dir}/initrd.img" | cpio -i

	cp "${INITRDDIR_L4T_DIR}/"*.sh "${working_dir}/initrd/bin"
	cp "${ROOTFS_DIR}/usr/sbin/flash_erase" "${working_dir}/initrd/usr/sbin"
	cp "${ROOTFS_DIR}/usr/sbin/mtd_debug" "${working_dir}/initrd/usr/sbin"
	cp "${ROOTFS_DIR}/bin/kmod" "${working_dir}/initrd/bin"
	ln -fs /bin/kmod "${working_dir}/initrd/usr/sbin/modprobe"
	cp "${ROOTFS_DIR}/usr/bin/sort" "${working_dir}/initrd/usr/bin"
	cp "${ROOTFS_DIR}/usr/bin/nohup" "${working_dir}/initrd/usr/bin"
	cp "${ROOTFS_DIR}/sbin/blkdiscard" "${working_dir}/initrd/sbin"
	cp "${ROOTFS_DIR}/sbin/partprobe" "${working_dir}/initrd/sbin"
	cp "${ROOTFS_DIR}/bin/mktemp" "${working_dir}/initrd/bin"
	cp "${ROOTFS_DIR}/lib/aarch64-linux-gnu/libsmartcols.so.1" "${working_dir}/initrd/lib/aarch64-linux-gnu"
	cp "${ROOTFS_DIR}/usr/lib/aarch64-linux-gnu/libbsd.so.0" "${working_dir}/initrd/usr/lib/aarch64-linux-gnu/libbsd.so.0"
	cp "${ROOTFS_DIR}/usr/lib/aarch64-linux-gnu/libparted.so.2" "${working_dir}/initrd/usr/lib/aarch64-linux-gnu/libparted.so.2"
	cp "${ROOTFS_DIR}/usr/lib/aarch64-linux-gnu/libzstd.so.1" "${working_dir}/initrd/usr/lib/aarch64-linux-gnu/libzstd.so.1"
	KERNEL_VERSION="$(strings "${LINUX_BASE_DIR}/kernel/Image" | grep -oE "Linux version [0-9a-zA-Z\.\-]+[+]* " | cut -d\  -f 3)"
	mkdir -p "${working_dir}/initrd/lib/modules/${KERNEL_VERSION}/kernel/drivers/"
	cp -r "${ROOTFS_DIR}/lib/modules/${KERNEL_VERSION}/kernel/drivers/mtd"  "${working_dir}/initrd/lib/modules/${KERNEL_VERSION}/kernel/drivers/"
	cp "${ROOTFS_DIR}/lib/modules/${KERNEL_VERSION}/modules"*  "${working_dir}/initrd/lib/modules/${KERNEL_VERSION}/"
	cp -r "${ROOTFS_DIR}/lib/modules/${KERNEL_VERSION}/kernel/drivers/spi"  "${working_dir}/initrd/lib/modules/${KERNEL_VERSION}/kernel/drivers/"
	cp -r "${ROOTFS_DIR}/usr/lib/modules/${KERNEL_VERSION}/kernel/drivers/hwmon" "${working_dir}/initrd/lib/modules/${KERNEL_VERSION}/kernel/drivers/"

	if [ -n "${network}" ]; then
		local arr
		IFS=':' read -r -a arr <<< "${network}"
		{
			echo "nfsnet=${arr[0]}"
			echo "targetip=${arr[1]}"
			echo "hostip=${arr[2]}"
			echo "gateway=${arr[3]}"
			echo "kernel_flash_script=${KERNEL_FLASH_SCRIPT}"
		} >> "${working_dir}/initrd/initrd_flash.cfg"
	fi

	mkdir -p "${working_dir}/initrd/etc"
	tnspec=$( awk '/TNSPEC/ {print $2}' "${BOOT_CTRL_CONF}" )
	echo "${tnspec}" > "${working_dir}/initrd/etc/board_spec.txt"

	if [ -n "${external_device}" ]; then
		echo "external_device=/dev/$(get_disk_name "${external_device}")" >> "${working_dir}/initrd/initrd_flash.cfg"
	fi
	if [ -n "${erase_all}" ]; then
		echo "erase_all=1" >> "${working_dir}/initrd/initrd_flash.cfg"
	fi
	echo "instance=${dev_instance}" >> "${working_dir}/initrd/initrd_flash.cfg"

	# Prepare for QSPI image flashing in initrd if neccessary
	if [ -z "${initrd_only}" ]; then
		copy_qspi_flash_packages
	fi

	find . | cpio -H newc -o | gzip -9 -n > "${working_dir}/initrd.img"

	popd

	cmdline=$(sed -n 's/^cmdline = //p' "${working_dir}/bootimg.cfg")
	"${BOOTLOADER_DIR}/mkbootimg" --kernel "${working_dir}/zImage" \
		--ramdisk "${working_dir}/initrd.img" --cmdline "${cmdline} kernel.hung_task_panic=0" \
		-o "${BOOTLOADER_DIR}/boot${dev_instance}.img"

	OUTPUT_FILE="${BOOTLOADER_DIR}/boot${dev_instance}.img"

	sign_bootimg

	echo "flashimg${dev_instance}=$(basename "${OUTPUT_FILE}")" | tee -a "${L4T_INITRD_FLASH_DIR}/${FLASH_IMG_MAP}"

	popd

}

sign_bootimg()
{
	set +u
	if [ "${CHIPID}" = "0x18" ] && [ -n "${KEY_FILE}" ] && [ -f "${KEY_FILE}" ]; then
		OUTPUT_FILE=$("${LINUX_BASE_DIR}"/l4t_sign_image.sh \
					--file "${OUTPUT_FILE}" --type "data" \
					--key "${KEY_FILE}" --chip "${CHIPID}" -q --split "False");
	fi
}

wait_for_booting()
{
	ext=""
	mmcblk0=""
	mmcblk0boot0=""
	mmcblk0boot1=""
	maxcount=${timeout:-60}
	count=0
	device_instance=$1
	while true
	do
		if [ -n "${network}" ]; then
			while IFS=  read -r; do
				netpath=/sys/class/net/${REPLY}
				netserialnumber=$(get_udev_attribute "${netpath}" serial)
				netconfiguration=$(get_udev_attribute "${netpath}" configuration)
				if [[ "${netconfiguration}" =~ RNDIS\+L4T${device_instance}.* ]]; then
					serialnumber="${netserialnumber}"
					break
				fi
			done < <(ls /sys/class/net)
			if [ -n "${serialnumber}" ]; then
				break
			fi
		fi
		if [ -z "${network}" ] && ls /dev/sd* 1> /dev/null 2>&1; then
			while IFS=  read -r -d $'\0'; do
				path="$(readlink -f "$REPLY")"
				! [ -b "${path}" ] && continue
				dev=$(get_udev_attribute "$path" vendor)
				model=$(get_udev_attribute "$path" model)
				model_id=$(get_udev_attribute "$path" idProduct)
				vendor_id=$(get_udev_attribute "$path" idVendor)

				if [ "${model_id}" != "7035" ] || [ "${vendor_id}" != "0955" ]; then
					continue
				fi

				if ! echo "${model}" | grep -q "${device_instance}"; then
					continue
				fi
				if [ "${dev}" = "mmc0" ]; then
					mmcblk0=$(basename "${path}")
				elif [ "${dev}" = "ext0" ]; then
					ext=$(basename "${path}")
				elif [ "${dev}" = "mmc0b0" ]; then
					mmcblk0boot0=$(basename "${path}")
				elif [ "${dev}" = "mmc0b1" ]; then
					mmcblk0boot1=$(basename "${path}")
				fi

			done < <(find /dev/ -maxdepth 1 -not -name "*[0-9]" -name "sd*" -print0)

			# If external device is not given from parameters, we only look for
			# /dev/mmcblk0, /dev/mmcblk0boot0, and /dev/mmcblk0boot1
			#
			# If external device is defined, then we also look for the external
			# device node file
			#
			# If we only flash external device, then we only need the
			# external device node file
			if [ -z "${external_device}" ] && [ -n "${mmcblk0}" ] && [ -n "${mmcblk0boot0}" ] && [ -n "${mmcblk0boot1}" ]; then
				serialnumber=$(get_udev_attribute "/dev/${mmcblk0}" serial)
				break
			elif [ -n "${ext}" ] && [ -n "${mmcblk0}" ] && [ -n "${mmcblk0boot0}" ] && [ -n "${mmcblk0boot1}" ]; then
				serialnumber=$(get_udev_attribute "/dev/${mmcblk0}" serial)
				break
			elif [ -n "${external_only}" ] && [ -n "${ext}" ]; then
				serialnumber=$(get_udev_attribute "/dev/${ext}" serial)
				break
			fi

		fi
		echo "Waiting for target to boot-up..."
		sleep 1;
		count=$((count + 1))
		if [ "${count}" -ge "${maxcount}" ]; then
			echo "Timeout"
			exit 1
		fi

	done
}

wait_for_ssh()
{

	printf "%s" "Waiting for device to expose ssh ..."
	count=0
	while ! ping_device
	do
		printf "..."
		count=$((count + 1))
		if [ "${count}" -ge "${maxcount}" ]; then
			echo "Timeout"
			exit 1
		fi
		sleep 1
	done
}

flash_direct()
{
	local cmd=()

	if [ -n "${target_partname}" ]; then
		cmd+=("-k" "${target_partname}")
	fi
	EXTDEV_ON_HOST="${direct}" "${NFS_IMAGES_DIR}/${KERNEL_FLASH_SCRIPT}" --direct "${cmd[@]}"
}


flash()
{
	local cmd=()

	if [ -n "${target_partname}" ]; then
		cmd+=("-k" "${target_partname}")
	fi

	if [ -n "${external_only}" ]; then
		cmd+=("${external_only}")
	fi

	MMCBLK0="${mmcblk0}" MMCBLKB0="${mmcblk0boot0}" MMCBLKB1="${mmcblk0boot1}"  \
	EXTDEV_ON_HOST="${ext}" EXTDEV_ON_TARGET="$(get_disk_name "${external_device}")" \
	TARGET_IP="$(cat "${sshcon}")" "${NFS_IMAGES_DIR}/${KERNEL_FLASH_SCRIPT}" --host-mode "${cmd[@]}"
}

flash_qspi()
{
	if [ -z "${external_only}" ]; then
		if [ -n "${target_partname}" ]; then
			cmd+=("-k" "${target_partname}")
		fi
		run_commmand_on_target "$(cat "${sshcon}")" "if [ -f /qspi/${KERNEL_FLASH_SCRIPT} ]; then USER=root /qspi/${KERNEL_FLASH_SCRIPT} --no-reboot --qspi-only ${cmd[*]}; fi"
	fi
}

boot_initrd()
{
	local usb_instance=${1}
	local skipuid=${2}
	local dev_instance=${3}

	pushd "${TEMP_INITRD_FLASH_DIR}"
	local cmd
	if [ -n "${usb_instance}" ]; then
		local var=flashimg${dev_instance}
		cmd="$(sed -e "s/$/ --instance ${usb_instance}/" \
			-e "s/kernel [a-zA-Z0-9._\-]*/kernel $(basename "${!var}")/" "${TEMP_INITRD_FLASH_DIR}/flashcmd.txt")"
	fi
	if [ -n "${skipuid}" ] && [ -z "${initrd_only}" ] && [ -z "${reuse_package}" ]; then
		cmd+=" --skipuid"
	fi
	echo "${cmd}"
	eval "${cmd}"

	popd
}

copy_bootloader()
{
	local temp_bootloader="${1}"
	local tid="${2}"
	local cmdline="${3}"

	mkdir -p "${temp_bootloader}"
	pushd "${BOOTLOADER_DIR}"
	cp tegrabct_v2 "${temp_bootloader}";
	cp tegradevflash_v2 "${temp_bootloader}";
	cp tegraflash_internal.py "${temp_bootloader}";
	cp tegrahost_v2 "${temp_bootloader}";
	cp tegraparser_v2 "${temp_bootloader}";
	cp tegrarcm_v2 "${temp_bootloader}";
	cp -r -t "${temp_bootloader}" ./*.rec ./recovery.img ./*.py pyfdt/ ./*.h ./*.dtsi ./*.dtb ./*.bin ./*.dts;
	cp tegraopenssl "${temp_bootloader}";
	if [ "${tid}" = "0x19" ]; then
		cp sw_memcfg_overlay.pl "${temp_bootloader}";
	fi;


	# Parsing the command line of tegraflash.py, to get all files that tegraflash.py and
	# tegraflash_internal.py needs so copy them to the working directory.
	cmdline=$(echo "${cmdline}" | sed -e s/\;/\ /g -e s/\"//g);
	read -r -a opts <<< "${cmdline}"
	optnum=${#opts[@]};
	for (( i=0; i < optnum; )); do
		opt="${opts[$i]}";
		opt=${opt//\,/\ }
		read -r -a files <<< "${opt}"
		filenum=${#files[@]};
		for (( j=0; j < filenum; )); do
			file="${files[$j]}";
			if [ -f "${file}" ]; then
				folder=$(dirname "${file}");
				if [ "${folder}" != "." ]; then
					mkdir -p "${temp_bootloader}/${folder}";
				fi;
				cp "${file}" "${temp_bootloader}/${folder}";
			fi;
			j=$((j+1));
		done;
		i=$((i+1));
	done;
	cp flashcmd.txt "${temp_bootloader}";
	awk -F= '{print $2}' "${L4T_INITRD_FLASH_DIR}/${FLASH_IMG_MAP}" | xargs cp -t "${temp_bootloader}"
	popd
}

package()
{
	local workdir="${1}"
	local cmdline="${2}"
	local tid="${3}"
	local temp_bootloader="${workdir}/bootloader"
	copy_bootloader "${temp_bootloader}" "${tid}" "${cmdline}"

	local temp_kernelflash="${workdir}/tools/kernel_flash"
	mkdir -p "${temp_kernelflash}"
	cp -a "${L4T_INITRD_FLASH_DIR}"/* "${temp_kernelflash}"
	cp "${LINUX_BASE_DIR}/${target_board}.conf" "${workdir}/"
	cp "${LINUX_BASE_DIR}/"*.common "${workdir}/"
	if [ -n "${network}" ]; then
		cp -a "${LINUX_BASE_DIR}/rootfs" "${workdir}/"
	fi
}

external_device=""
append=""
should_exit=""
qspi=""
config_file=""
external_size=""
external_only=""
user_key=""
pv_crt=""
no_flash="0"
sparse_mode="0"
sshcon="$(mktemp)"
usb_instance=""
flash_only=0
OPTIONS=""
KEY_FILE=""
erase_all=""
device_instance="0"
target_partname=""
max_massflash=""
massflash_mode=""
SBK_KEY=""
keep=""
reuse=""
network=""
timeout=""
skipuid=""
initrd_only=""
reuse_package=""
direct=""

source "${L4T_INITRD_FLASH_DIR}"/l4t_kernel_flash_vars.func
source "${L4T_INITRD_FLASH_DIR}"/l4t_initrd_flash.func
source "${L4T_INITRD_FLASH_DIR}"/l4t_network_flash.func

parse_param "$@"

check_prerequisite

get_max_flash

if [ "${flash_only}" = "0" ]; then
	if [ -z "${initrd_only}" ] && [ -z "${reuse_package}" ]; then
		cat <<EOF
************************************
*                                  *
*  Step ${initrd_flash_step}: Generate flash packages *
*                                  *
************************************
EOF
		generate_flash_package
		((initrd_flash_step+=1))
	fi
cat <<EOF
******************************************
*                                        *
*  Step ${initrd_flash_step}: Generate rcm boot commandline *
*                                        *
******************************************
EOF
	generate_rcm_bootcmd

	rm -f "${L4T_INITRD_FLASH_DIR}/${FLASH_IMG_MAP}"

	for i in $(seq 0 "$((max_massflash - 1))")
	do
		generate_flash_initrd "${i}"
	done

	((initrd_flash_step+=1))
	if [ "${massflash_mode}" = "1" ]; then
		rm -rf "${LINUX_BASE_DIR}/mfi_${target_board}/"
		mkdir -p "${LINUX_BASE_DIR}/mfi_${target_board}/"
		package "${LINUX_BASE_DIR}/mfi_${target_board}/" "$(cat "${BOOTLOADER_DIR}/flashcmd.txt")" "${CHIPID}"
		tar -zcvf "${LINUX_BASE_DIR}/mfi_${target_board}.tar.gz" -C "${LINUX_BASE_DIR}" "./mfi_${target_board}"
		echo "Massflash package is generated at ${LINUX_BASE_DIR}/mfi_${target_board}.tar.gz"
	fi

fi




if [ "${no_flash}" = "0" ]; then

	if [ -n "${direct}" ]; then
cat <<EOF
*************************************************************
*                                                           *
*  Step ${initrd_flash_step}: Start the host connected device flashing process *
*                                                           *
*************************************************************
EOF
		flash_direct
		echo "Success"
		exit
	fi


cat <<EOF
**********************************************
*                                            *
*  Step ${initrd_flash_step}: Build the flashing environment    *
*                                            *
**********************************************
EOF

	source "${L4T_INITRD_FLASH_DIR}/${FLASH_IMG_MAP}"

	build_working_dir "${device_instance}"
	((initrd_flash_step+=1))

cat <<EOF
****************************************************
*                                                  *
*  Step ${initrd_flash_step}: Boot the device with flash initrd image *
*                                                  *
****************************************************
EOF
	((initrd_flash_step+=1))

	boot_initrd "${usb_instance}" "${skipuid}" "${device_instance}"


cat <<EOF
***************************************
*                                     *
*  Step ${initrd_flash_step}: Start the flashing process *
*                                     *
***************************************
EOF

	if [[ "${network}" == eth0* ]]; then
		IFS=':' read -r -a arr <<< "${network}"
		flash_through_ssh "${arr[1]%%/*}"
	fi

	wait_for_booting "${device_instance}"

	wait_for_ssh

	if [ "${network}" = "usb0" ]; then
		# For this mode, we need to wait for the network interface
		# to be set up
		flash_through_ssh "fc00:1:1:${device_instance}::2"
	fi

	if [ -n "${initrd_only}" ]; then
		# This output will be parsed by l4t_backup_restore.sh
		echo "Device has booted into initrd. You can ssh to the target by the command:"
		echo "$ ssh root@fe80::1%$(cat "${sshcon}")"
		exit
	fi

	flash_qspi &
	qspi=$!

	flash &
	normal=$!

	if ! wait "${normal}"; then
		error_message+="Error flashing non-qspi storage\n"
		should_exit=1
	fi

	if ! wait "${qspi}"; then
		error_message+="Error flashing qspi\n"
		should_exit=1
	fi
	wait
	if [ -n "${should_exit}" ]; then
		exit 1
	fi


	echo ""

	echo "Reboot target"
	if ! run_commmand_on_target "$(cat "${sshcon}")" "sync; nohup reboot &>/dev/null & exit"; then
		echo "Reboot failed."
		clean_dev_folder
		exit 1
	fi
fi

echo "Success"
