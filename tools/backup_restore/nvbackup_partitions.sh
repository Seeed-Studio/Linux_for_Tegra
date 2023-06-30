#!/bin/bash

# Copyright (c) 2018-2021, NVIDIA CORPORATION.  All rights reserved.
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#  * Neither the name of NVIDIA CORPORATION nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# This script is utilized as part of Jetsonbackup and restore process.
# The script is run on the target side by ROOT. It will create backup partitions
# to backup target emmc images to an usb which will be restored onto a
# target board Jetson board.

# For non network mode, The backup script needs a USB with a
# minimum of 32GB storage to store the backup partitions. On the restore side,
# the usb would be plugged into the target board and the target board connected
# to a host via a USB - TTL cable connection.
# Pin assignment:
#	Jetson TX2 J21 Pin 8 (UART 1 TXD) → Cable RXD (White Wire)
#	Jetson TX2 J21 Pin 10 (UART 1 RXD) → Cable TXD (Green Wire)
#	Jetson TX2 J21 Pin 9 (GND) → Cable GND (Black Wire)
# The serial console will be available on /dev/ttyUSB0 , which can be accessed
# using `screen` or `putty`. More informations can be found on:
# 	http://www.jetsonhacks.com/2017/03/24/serial-console-nvidia-jetson-TX2/
#
# Script will run with ./nvbackup_partitions.sh (This mode has been tested)
# Script should only run after booting from either initrd or nfs

set -e

usage() {
	echo "Usage: ${0} [-h][-s][-z][-i][-d <device>|--device <device>]"
	cat <<EOT
	This script backs up the partition on the Jetson.
	The script is run on the target side by ROOT. It will create backup
	partitions to be restored onto a Jetson board along with a text file
	containing the mapping of the backup files and the partitions.

	This script should be run inside initramfs.

	Options:
		-h | --help : Display this message

		-s : Squash all partitions before and after the file system partition
			 into two files, i,e, partition1.img and partition2.img

		-z : Do not use tar to backup the ext4 partitions

		-i : Interactive mode. Ask user input before overwriting existing files
			 if neccessary.

		-d | --device <device_name>: specify a block device to store the backup
			 in. For example: If your USB appears in the /dev folder as
			 /dev/sda1, then use:
			./nvbackup_partitions.sh -d sda1

		-n: network mode. This script runs directy on the target using network drive
		as the current filesystem

	Default behavior are all non ext4 partitions are backed up using dd, all
	ext4 partitions are backed up using tar.
EOT
	exit 0
}

print_message() {
	if [ "${DISPLAY_MESSAGE}" = "true" ]; then
		echo -e "${SCRIPT_NAME}: $1"
	fi
}

# arguments filename,partitionname,start sector, size, [flags], sha256
# addentry is used to store backup file information into index.txt
addentry() {
	if [ "$#" -ne 6 ]; then
		print_message "addentry function needs 6 parameters when adding an entry to partition map."
		return 1
	fi
	echo "${@}" | sed s/" "/,/g >> "${LDK_DIR}/${indexfile}"
}

cleanup() {
	cd /
	if mountpoint -q "${LDK_DIR}/${MOUNT_DIR}"; then
		umount "${LDK_DIR}/${MOUNT_DIR}"
	fi
	if mountpoint -q "${LDK_DIR}"; then
		umount "${LDK_DIR}"
	fi
}
trap cleanup EXIT

isext4() {
	if [ "$#" -ne 1 ]; then
		print_message "isext4 function need 1 parameter that is the name of the storage device"
		return 1;
	fi
	local result
	result="$( blkid "/dev/${1}" | awk '{ print $3 }' | sed -n 's|TYPE="\(.*\)"|\1|p' )"
	if [ "${result}" = "ext4" ]; then
		echo "true"
	else
		echo "false"
	fi
}

find_APP() {
	local arr=("$@")
	local count=0
	for i in "${arr[@]}"; do
		if [ "$( partx -s "${i}" -o NAME | tail -1 )" = "APP" ]; then
			echo "${i},${count}"
			return 0;
		fi
		((count++))
	done
	return 1;
}

array_sum() {
	local IFS=+; read -r <<< "${*}"
	((sum=REPLY))
	echo "$sum"
}

# print an array
print_array() {
	local IFS=$'\n';echo "${*}";
	echo ""
}

checksum() {
	if [ $# -ne 1 ]; then
		print_message "checksum does not have enough arguments"
		exit 1;
	else
		sha256sum "${1}" | awk '{ print $1 }'
	fi
}

find_default_device() {
	local -r dflt=$(ls /dev/sd* 2>/dev/null | tail -1)
	echo "${dflt}"
}

SCRIPT_NAME="${0##*/}"
MAINDEVICE_NAME="mmcblk0"
BOOT_0_NAME="${MAINDEVICE_NAME}boot0"
BOOT_1_NAME="${MAINDEVICE_NAME}boot1"
QSPI0="mtd0"
SQUASH_MODE="false"
FORCE_MODE="true"
DISPLAY_MESSAGE="true"
USE_TAR="true"
LDK_DIR="/mnt"
MOUNT_DIR="tmp/tmp"
NFS_MODE=
CUR_DIR="$(cd "$(dirname "${0}")" && pwd)"


while getopts ":hszqnd:i-:" arg; do
	case $arg in
	h) usage;;
	s) SQUASH_MODE="true";;
	z) USE_TAR="false";;
	i) FORCE_MODE="false";;
	q) DISPLAY_MESSAGE="false";;
	d) BACKUP_LOC="/dev/${OPTARG}";;
	n) NFS_MODE=1; LDK_DIR="${CUR_DIR}/images"; mkdir -p "${LDK_DIR}" ;;
	-) case ${OPTARG} in
		help) usage;;
		device) BACKUP_LOC="/dev/${!OPTIND}"
				OPTIND=$((OPTIND + 1));;
		*) usage;;
	   esac;;
	*) usage;;
	esac
done

if [ "$#" = 0 ] || [[ "${BACKUP_LOC}" == "" ]] && [ -z "${NFS_MODE}" ]; then
	BACKUP_LOC=$(find_default_device)
	if [ -z "${BACKUP_LOC}" ]; then
		echo "Error: Found no devices"
		echo "If you want to specify your device, use -d <DEVICE>."
		echo "For more information, run the script with --help option"
		exit 1;
	fi
fi

if [ -z "${NFS_MODE}" ] && [ ! -b "${BACKUP_LOC}" ]; then
	echo "${SCRIPT_NAME}:  Device ${BACKUP_LOC} does not exist"
	echo "For more information, run the script with --help option"
	exit 1;
fi

[ "${FORCE_MODE}" = "false" ] && print_message "Do you want to store the backup at ${BACKUP_LOC}?"
while [ "${FORCE_MODE}" = "false" ]; do
	read  -r -n 1 -p "Answer yes (y) or no (n) " yn
	echo ""
	case $yn in
	[Yy]* ) break;;
	[Nn]* ) exit 1;;
	* ) print_message "Please answer yes or no.";;
	esac
done

mounted_at_mnt=$( awk '$2 == "${LDK_DIR}" {print $1}'  /proc/mounts )
if [ -z "${NFS_MODE}" ] && [ ! "${mounted_at_mnt}" = "${BACKUP_LOC}" ]; then
	mount "${BACKUP_LOC}" "${LDK_DIR}"
fi
pushd "${LDK_DIR}" > /dev/null 2>&1

# Creating a file format txt "nvpartitionmap.txt"
indexfile="nvpartitionmap.txt"
if [ -e "${LDK_DIR}/${indexfile}" ]; then
	if [ "${FORCE_MODE}" = "true" ]; then
		rm -rf "${LDK_DIR}/${indexfile:?}"
	else
		print_message "Cannot create ${indexfile} : ${indexfile} already exists. "
		print_message "Remove ${indexfile} before creating a backup"
		exit 1;
	fi
fi

BOARD_SPEC="$( cat "/etc/board_spec.txt" )"
echo "board_spec,$BOARD_SPEC" >> "${indexfile}"

all=$(fdisk -l "/dev/${MAINDEVICE_NAME}")
mapfile -t start < <(echo "${all}" | awk '/^Device/{f=1} f{print $2; if (!NF) exit}' | grep "[0-9]")
OIFS=$IFS
IFS=$'\n'
mapfile -t sorted < <( sort -n <<<"${start[*]}")
IFS=$OIFS
mapfile -t size < <( echo "${all}" | awk '/^Device/{f=1} f{print $4; if (!NF) exit}' | grep "[0-9]" )
mapfile -t partition_lists < <(echo "$all" | awk '/^Device/{f=1} f{print $1; if (!NF) exit}' | grep /dev/)

# retrieves the entire size of the partition
partition_size=$( cat "/sys/block/${MAINDEVICE_NAME}/size" )

print_message "The detected partitions in ${MAINDEVICE_NAME} are:"
print_array "${partition_lists[@]}"

while [ "${FORCE_MODE}" = "false" ]; do
	echo ""
	print_message "Do you want to proceed?"
	read -r -n 1 -p "Answer yes (y) or no (n) " yn
	echo ""
	case $yn in
	[Yy]* ) break;;
	[Nn]* ) exit;;
	* ) print_message "Please answer yes or no.";;
	esac
done

if [ -c "/dev/${QSPI0}" ]; then
	print_message "Backing up QSPI0..."
	QSPI0_img="QSPI0.img"
	QSPI0size=$(mtd_debug info "/dev/${QSPI0}" | sed -n 's/^mtd.size = \([0-9]*\).*/\1/p')
	mtd_debug read "/dev/${QSPI0}" 0 "${QSPI0size}" "${QSPI0_img}"
	addentry "QSPI0.img" "qspi0" 0 "${QSPI0size}" "" "$(checksum "${LDK_DIR}/${QSPI0_img}")"
fi

print_message "Backing up GPT..."
# Backing up the gpt partition table as gptmbr.img
pri_gpt_img="gptmbr.img"
dd if=/dev/mmcblk0 of="${LDK_DIR}/${pri_gpt_img}" bs=512 count=$((sorted[0])) status=progress
addentry gptmbr.img gpt_1 0 "${sorted[0]}" "" "$(checksum "${LDK_DIR}/${pri_gpt_img}")"
print_message "Success backing up GPT to ${pri_gpt_img}\n"

print_message "Backing up backup GPT..."
# Backing up the end gpt partition table as gptbackup.img
sec_gpt_img="gptbackup.img"
dd if=/dev/mmcblk0 of="${LDK_DIR}/${sec_gpt_img}" bs=512 skip=$((partition_size - 33)) status=progress
addentry gptbackup.img gpt_2 $((partition_size - 33)) 33 "" "$(checksum "${LDK_DIR}/${sec_gpt_img}")"
print_message "Success backing up backup GPT to ${sec_gpt_img}\n"

if [ -d "/sys/block/${BOOT_0_NAME}/" ]; then
	print_message "Backing up ${BOOT_0_NAME}..."
	# Backing up the mmcblk0boot0 (first boot partition) as boot0.img
	boot0_img="boot0.img"
	boot0size=$( cat "/sys/block/${BOOT_0_NAME}/size" )
	dd if="/dev/${BOOT_0_NAME}" of="${LDK_DIR}/${boot0_img}" status=progress
	addentry boot0.img "${BOOT_0_NAME}" 0 "${boot0size}" "" "$(checksum "${LDK_DIR}/${boot0_img}")"
	print_message "Success backing up ${BOOT_0_NAME} to ${boot0_img}\n"
fi

if [ -d "/sys/block/${BOOT_1_NAME}/" ]; then
	print_message "Backing up ${BOOT_1_NAME}..."
	# Backing up mmcblk0boot1 (second boot partition) as boot1.img
	boot1_img="boot1.img"
	boot1size=$( cat "/sys/block/${BOOT_1_NAME}/size" )
	dd if="/dev/${BOOT_1_NAME}" of="${LDK_DIR}/${boot1_img}" status=progress
	addentry boot1.img "${BOOT_1_NAME}" 0 "${boot1size}" "" "$(checksum "${LDK_DIR}/${boot1_img}")"
	print_message "Success backing up ${BOOT_1_NAME} to ${boot1_img}\n"
fi

COMMON_TAR_OPTION=(--warning=none --checkpoint=10000 --one-file-system .)

if [ "${SQUASH_MODE}" = "true" ]; then
	app_partition=$(find_APP "${partition_lists[@]}")
	if [ $? -ne 0 ]; then
		echo "Cannot find the APP partitions"
		popd > /dev/null 2>&1
		exit 1
	fi
	index_app="${app_partition##*,}"
	app_partition="${app_partition%%,*}"

	if [ "${index_app}" != 0 ]; then
		FIRST_BACKUP_IMG=partition1.img
		print_message "Backup all partitions before APP to ${FIRST_BACKUP_IMG}..."
		# Backing up all partitions other than APP as partition1.img
		bf_count=$( array_sum "${size[@]:0:$index_app}" )
		CMDLINE_DD=(skip="${start[0]}" count="${bf_count}" bs=512 status=progress)
		dd if="/dev/${MAINDEVICE_NAME}" of="${LDK_DIR}/${FIRST_BACKUP_IMG}" "${CMDLINE_DD[@]}"

		addentry "${FIRST_BACKUP_IMG}" \
					"${MAINDEVICE_NAME}" \
					"${start[0]}" \
					"${bf_count}" \
					"" \
					"$( checksum "${LDK_DIR}/${FIRST_BACKUP_IMG}" )"
		print_message "Success backing up ${FIRST_BACKUP_IMG}\n"
	fi

	print_message "Backing up APP partition..."
	if [ ! -d  "${LDK_DIR}/${MOUNT_DIR:?}" ]; then
		mkdir "${MOUNT_DIR:?}"
	fi
	mount "${app_partition}" "${MOUNT_DIR:?}"
	pushd "${MOUNT_DIR:?}" > /dev/null 2>&1
	set +e
	tar cpf - "${COMMON_TAR_OPTION[@]}" | gzip -1 > "${LDK_DIR}/${app_partition##*/}.tar.gz"
	set -e
	popd > /dev/null 2>&1
	addentry "${app_partition##*/}.tar.gz" \
				"${app_partition##*/}" \
				"${start[$index_app]}"  \
				"${size[$index_app]}" \
				"tz" \
				 "$( checksum "${LDK_DIR}/${app_partition##*/}.tar.gz")"
	print_message "Success backing up APP to ${app_partition##*/}.tar.gz"

	if [ "${index_app}" != $((${#partition_lists[@]} - 1)) ]; then
		SECOND_BACKUP_IMG="partition2.img"
		print_message "Backup all partitions after APP to ${SECOND_BACKUP_IMG}..."
		after_index_app=$((index_app + 1))
		after_main_size=$(array_sum "${size[@]:$after_index_app}")
		CMDLINE_DD=(skip="${start[$after_index_app]}" count="${after_main_size}" bs=512 status=progress)
		# Backing up all partitions other than APP as partition2.img
		dd if="/dev/${MAINDEVICE_NAME}" of="${LDK_DIR}/${SECOND_BACKUP_IMG}" "${CMDLINE_DD[@]}"

		addentry "${SECOND_BACKUP_IMG}" \
					"${MAINDEVICE_NAME}" \
					"${start[$after_index_app]}" \
					"${after_main_size}" \
					"" \
					"$( checksum "${LDK_DIR}/${SECOND_BACKUP_IMG}")"
		print_message "Success backup all partitions after APP to ${SECOND_BACKUP_IMG}\n"
	fi
else
	count=0
	# Backing up APP as one partition (backup.tar.gz).
	# APP is too large to be backed up using dd. Currently using tar
	for i in "${partition_lists[@]}";
	do
		tmp="${i##*/}"
		if  [ "${USE_TAR}" = "true" ] && [ "$( isext4 "${tmp}" )" = "true" ]; then
			if ! [ -d "${MOUNT_DIR:?}" ]; then
				mkdir -p "${MOUNT_DIR:?}"
			fi
			mount -t ext4 "${i}" "${MOUNT_DIR:?}"

			print_message "Start backing up ${tmp}..."
			pushd "${MOUNT_DIR:?}" > /dev/null 2>&1
			set +e
			tar cpf - "${COMMON_TAR_OPTION[@]}" | gzip -1 > "${LDK_DIR}/${tmp}.tar.gz"
			set -e
			popd > /dev/null 2>&1
			umount "${MOUNT_DIR:?}"
			addentry "${tmp}.tar.gz" "${tmp}" "0" "${size[$count]}" "tz" "$( checksum "${LDK_DIR}/${tmp}.tar.gz" )"
			print_message "Success backing up ${tmp} to ${tmp}.tar.gz\n"
			count=$((count+1));

			continue
		fi
		print_message "Start backing up ${tmp} ..."
		dd if="$i" conv=sync,noerror bs=1M status=progress | gzip -1 > "${LDK_DIR}/${tmp}_bak.img"
		print_message "Success backing up ${tmp} to ${tmp}_bak.img\n"

		addentry "${tmp}_bak.img" \
			"${MAINDEVICE_NAME}" \
			"${start[$count]}" \
			"${size[$count]}" \
			"" \
			"$( checksum "${LDK_DIR}/${tmp}_bak.img")"
		count=$((count+1));

	done
fi
popd > /dev/null 2>&1

print_message "Backup complete (after command prompt popping up)"
