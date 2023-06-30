#!/bin/bash

# Copyright (c) 2018-2022, NVIDIA CORPORATION.  All rights reserved.
#
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

# Usage: ./nvrestore_partitions.sh <nvpartitionmap.txt>
# This script should never be run using the default boot process of the jetson,
# and only through nfs-boot or initrd mode. The script is run on the target board.
# The "nvpartitionmap.txt" file contains the list of partitions that are to be
# restored, as well as important information about each partition.
# Each line represents one partition, and follows the format:
# <filename>,<partitionname>,<start sector>,<number of sectors>,<[flags]>,<sha256>

set -e
SCRIPT_NAME="${0##*/}"
CUR_DIR="$(cd "$(dirname "${0}")" && pwd)"
FORCE_MODE=true
GPT_EXISTS=false
BOARD_SPEC=""
BOARD_MATCH=false
FILE_NAME="nvpartitionmap.txt"
FILE_FOUND=false
MOUNT_DEVICE=""
MOUNT_LOC="/mnt"
TMP_MOUNT="/tmp/mnt"
ERASE_QSPI=0
NETWORK_MODE=""
RAW_IMAGE=
INTERNAL_STORAGE_DEVICE="mmcblk0"
error_message=""

function set_board_spec {
	BOARD_SPEC=$(awk '{print $1}' /etc/board_spec.txt)
}

function usage {
	echo "Usage: ./${SCRIPT_NAME} [options] [nvpartitionmap.txt]"
	echo ""
	echo "This script should only be run through either initrd mode or nfs boot."
	echo "The script only works is run on the target board."
	echo "
options:
	-d | --device <device> ------ specify device to mount, default is sda1
	-h | --help ----------------- print this message
	-i | --interactive ---------- print user prompt for questions
	-n -------------------------- network mode. This script runs from a network mounted folder
	--raw-image ----------------- Specify the path of the raw disk image to be restored into storage devce."
	exit 0
}

function cleanup {
	cd /
	if [ -z "${NETWORK_MODE}" ]; then
		if mountpoint -q "${MOUNT_LOC}" ; then
			umount "${MOUNT_LOC}"
		fi

		if mountpoint -q "${TMP_MOUNT}" ; then
			umount "${TMP_MOUNT}"
		fi
	fi

	if [ -n "${error_message}" ]; then
		echo -e "${error_message}"
	fi

	if [ -f "/sys/block/mmcblk0boot0/force_ro" ]; then
		echo 1 > /sys/block/mmcblk0boot0/force_ro
	fi
	if [ -f "/sys/block/mmcblk0boot1/force_ro" ]; then
		echo 1 > /sys/block/mmcblk0boot1/force_ro
	fi

}
trap cleanup EXIT

find_default_device() {
	local -r dflt=$(ls /dev/sd* 2>/dev/null | tail -1)
	echo "${dflt}"
}

function erase_spi
{
	if [ ${ERASE_QSPI} = "0" ]; then
		flash_erase "${1}" 0 0
		ERASE_QSPI=1
	fi
}

# Check whether index file is specified
nargs=$#;
if [[ "${!nargs}" = *.txt ]]; then
	FILE_NAME="${!nargs}"
	FILE_FOUND=true
	nargs=$(($nargs-1))
	echo "${SCRIPT_NAME}: The index file to be used for flashing is ${FILE_NAME}."
fi

while getopts "d:ih-:n" arg; do
	case $arg in
	d) MOUNT_DEVICE="/dev/${OPTARG}"; ;;
	i) FORCE_MODE=false; ;;
	h) usage; ;;
	n) NETWORK_MODE=1; ;;
	-) case ${OPTARG} in
			help)   usage; ;;
			device) MOUNT_DEVICE="/dev/${!OPTIND}"
					OPTIND=$(($OPTIND + 1)); ;;
			raw-image) RAW_IMAGE="${!OPTIND}"
					OPTIND=$(($OPTIND + 1)); ;;
			*) usage; ;;
		esac ;;
	*) usage; ;;
	esac
done

if [ -z "${NETWORK_MODE}" ]; then
	# If the user provides device
	if [ "${MOUNT_DEVICE}" = "" ]; then
		MOUNT_DEVICE=$(find_default_device)
		if [ "${MOUNT_DEVICE}" != "" ]; then
			echo "${SCRIPT_NAME}: Use device ${MOUNT_DEVICE}"
		fi
	fi

	if [ ! -b "${MOUNT_DEVICE}" ]; then
		echo "${SCRIPT_NAME}: No USB storage device found."
		echo "For more information, run the script with --help option"
		exit 1
	fi
fi

while [ "${FORCE_MODE}" = "false" ]; do
	echo "${SCRIPT_NAME}: Is the USB plugged in and does it contain the backup files?"
	echo ""
	read -r -n 1 -p "${SCRIPT_NAME}: Answer (y) for yes or (n) for no:" yn
	echo ""
	case $yn in
		[Yy]* ) break;;
		[Nn]* ) exit;;
		* ) echo "${SCRIPT_NAME} : Please answer yes or no.";;
	esac
done

# Mount device
if [ -z "${NETWORK_MODE}" ]; then
	mount "${MOUNT_DEVICE}" "${MOUNT_LOC}"
	pushd "${MOUNT_LOC}"
elif [ -n "${RAW_IMAGE}" ]; then
	pushd "${CUR_DIR}"
else
	pushd "${CUR_DIR}/images"
fi

restore_non_qspi_with_raw_image()
{
	# Restore the raw disk image to internal storage device (eMMC)
	local target_device="/dev/${INTERNAL_STORAGE_DEVICE}"
	local raw_image="${RAW_IMAGE}"
	if [ ! -f "${raw_image}" ]; then
		echo "Error: the specified raw disk image ${raw_image} is not found"
		exit 1
	fi

	# Make sure the size of device is not smaller than the size of raw disk image
	local device_size=
	local image_size=
	device_size=$(blockdev --getsize64 "${target_device}")
	image_size=$(ls -l "${raw_image}" | awk '{print $5}')
	if [ "${image_size}" -gt "${device_size}" ]; then
		echo "Error: the size of ${raw_image} (${image_size} bytes) is beyond ${target_device} (${device_size})"
		exit 1
	fi

	# Write the raw disk image into device
	local image_chksum=
	echo "${SCRIPT_NAME} Restoring ${target_device} with image ${raw_image}..."
	image_chksum=$(sha256sum "${raw_image}" | awk '{print $1}')
	if ! dd if="${raw_image}" of="${target_device}" bs=1MB; then
		echo "Error: writing ${raw_image} into ${target_device}"
		exit 1
	fi
	sync

	# Verify the written data by checksum
	local device_chksum=
	if [ "${image_size}" -eq "${device_size}" ]; then
		device_chksum=$(sha256sum "${target_device}" | awk '{print $1}')
	else
		# Read data from the device if the size of the raw image
		# does not equal the size of the device and then verify
		# the read data and the written raw image by comparing the
		# sha256 checksums of them.
		local MB_num=
		local Byte_remaining=
		local tmp_device_image=/tmp/device_image.tmp
		MB_num="$((image_size / (1024 * 1024)))"
		Byte_remaining="$((image_size % (1024 * 1024)))"
		if [ "${MB_num}" != "0" ]; then
			if ! dd if="${target_device}" of="${tmp_device_image}" bs=1M count="${MB_num}"; then
				echo "Error: read from ${target_device} to ${tmp_device_image} by MiB"
				exit 1
			fi
		fi
		if [ "${Byte_remaining}" != "0" ]; then
			MB_num="$((MB_num * 1024 * 1024))"
			if ! dd if="${target_device}" of="${tmp_device_image}" bs=1 skip="${MB_num}" seek="${MB_num}" count="${Byte_remaining}" conv=notrunc; then
				echo "Error: read from ${target_device} to ${tmp_device_image} by bytes"
				exit 1
			fi
		fi
		device_chksum=$(sha256sum "${tmp_device_image}" | awk '{print $1}')
		rm -f "${tmp_device_image}"
	fi
	if [ "${image_chksum}" != "${device_chksum}" ]; then
		echo "Error: failed to verify the written raw disk image ${raw_image} on ${target_device}"
		exit 1
	fi

	echo "Done"

	cleanup
}

# Restore with raw disk image
if [ -n "${RAW_IMAGE}" ]; then
	if restore_non_qspi_with_raw_image; then
		echo "${SCRIPT_NAME}: Successful to restore with raw disk image ${RAW_IMAGE}"
		exit 0
	else
		echo "${SCRIPT_NAME}: Failed to restore with raw disk image ${RAW_IMAGE}"
		exit 1
	fi
fi

# If the user does not provide their own index file, use the default.
if [ ${FILE_FOUND} = false ]; then
	echo "${SCRIPT_NAME}: Use the default ${FILE_NAME} as the index file."
fi

# If index file does not exists, exit the program
if [ ! -e "${FILE_NAME}" ]; then
	echo "${SCRIPT_NAME}: No index file ${FILE_NAME} is found on USB storage device."
	exit 1
fi

# This block will check to make sure the model of the target board matches the
# model of the board the images came from.
for value in $(grep -v -e '(^ *$|^#)' < "${FILE_NAME}"); do
	declare -a FIELDS
	for part in {1..6}; do
		FIELDS[part]=$(echo "$value" | awk -F, -v part=${part} '{print $part}')
	done
	if [ "${FIELDS[1]}" = 'board_spec' ]; then
		set_board_spec
		if [[ "${FIELDS[2]}" == "${BOARD_SPEC}" ]]; then
			BOARD_MATCH=true
		fi
	fi
done
if [ ${BOARD_MATCH} = false ]; then
	echo "${SCRIPT_NAME}: You are trying to flash images from a board model that does not"
	echo "match the current board you're flashing onto."
	exit 1
fi

# The GPT must be the first partition flashed, so this block ensures that the
# GPT exists and is flashed first.
for value in $(grep -v -e '(^ *$|^#)' < "${FILE_NAME}"); do
	declare -a FIELDS
	for part in {1..6}; do
		FIELDS[part]=$(echo "$value" | awk -F, -v part=${part} '{print $part}')
	done
	if [ "${FIELDS[2]}" = 'gpt_1' ]; then
		checksum=$(sha256sum "${FIELDS[1]}" | awk '{print $1}')
		if [ "${checksum}" != "${FIELDS[6]}" ]; then
			echo "${SCRIPT_NAME} Checksum of ${FIELDS[2]} does not match the checksum in the index file."
			exit 1
		fi
		# partx delete must be called before flashing, and partx add after flashing.
		partx -d "/dev/${INTERNAL_STORAGE_DEVICE}"
		dd if="${FIELDS[1]}" of="/dev/${INTERNAL_STORAGE_DEVICE}"
		partx -v -a "/dev/${INTERNAL_STORAGE_DEVICE}"
		GPT_EXISTS=true
		break
	fi
done

# If the GPT does not exist, exit the program.
if [ ${GPT_EXISTS} != true ]; then
	echo "${SCRIPT_NAME} The GPT does not exist in the file: nvpartitionmap.txt"
	exit 1
fi

# The following two lines make mmcblk0boot0 and mmcblk0boot1 writeable.
if [ -f "/sys/block/mmcblk0boot0/force_ro" ]; then
	echo 0 > "/sys/block/mmcblk0boot0/force_ro"
fi
if [ -f "/sys/block/mmcblk0boot1/force_ro" ]; then
	echo 0 > "/sys/block/mmcblk0boot1/force_ro"
fi

restore_non_qspi()
{
	# All partitions except for the GPT will be flashed here.
	grep -v -e '(^$|^#)' < "${FILE_NAME}" | while IFS= read -r value; do
		declare -a FIELDS
		for part in {1..6}; do
			FIELDS[part]=$(echo "$value" | awk -F, -v part=${part} '{print $part}')
		done
		if [ "${FIELDS[2]}" = 'gpt_1' ]; then
			continue;
		elif [ "${FIELDS[1]}" = 'board_spec' ]; then
			continue;
		elif [ "${FIELDS[5]}" = 'tz' ]; then
			# The backup script will only mark ext4 partition with the tz flag
			# So that particular partition will be tar'ed here.
			mkfs.ext4 -F "/dev/${FIELDS[2]}"
			if [ ! -d "${TMP_MOUNT}" ]; then
				mkdir "${TMP_MOUNT}"
			fi
			mount "/dev/${FIELDS[2]}" "${TMP_MOUNT}"
			echo "${SCRIPT_NAME} Restoring ${FIELDS[2]}..."
			tar --xattrs -xpf "${FIELDS[1]}" --checkpoint=10000 --warning=no-timestamp --numeric-owner -C "${TMP_MOUNT}"
			umount "/dev/${FIELDS[2]}"
		else
			echo "${SCRIPT_NAME} Restoring ${FIELDS[2]} with image ${FIELDS[1]}..."
			checksum=$(sha256sum "${FIELDS[1]}" | awk '{print $1}')
			if [ "${checksum}" != "${FIELDS[6]}" ]; then
				echo "${SCRIPT_NAME} Checksum of ${FIELDS[2]} does not match the checksum in the index file."
				exit
			fi
			if [ "${FIELDS[2]}" = 'mmcblk0boot0' ] || [ "${FIELDS[2]}" = 'mmcblk0boot1' ] || [ "${FIELDS[2]}" = 'gpt_2' ]; then
				dd if="${FIELDS[1]}" of="/dev/${FIELDS[2]}" bs=512 seek=$((FIELDS[3])) count=$((FIELDS[4]))
			else
				gzip -c -S img -d "${FIELDS[1]}" | dd of="/dev/${FIELDS[2]}" bs=512 seek=$((FIELDS[3])) count=$((FIELDS[4]))
			fi
		fi
	done
}

restore_qspi ()
{
		# All partitions except for the GPT will be flashed here.
	grep -v -e '(^$|^#)' < "${FILE_NAME}" | while IFS= read -r value; do
		declare -a FIELDS
		for part in {1..6}; do
			FIELDS[part]=$(echo "$value" | awk -F, -v part=${part} '{print $part}')
		done
		if [ "${FIELDS[2]}" = 'qspi0' ]; then
				erase_spi /dev/mtd0
				mtd_debug write /dev/mtd0 "${FIELDS[3]}" "${FIELDS[4]}" "${FIELDS[1]}"
		fi
	done
}

should_exit=""

restore_non_qspi &
non_qspi=$!

restore_qspi &
qspi=$!


if ! wait "${non_qspi}"; then
	error_message+="Error flashing non-qspi storage\n"
	should_exit="1"
fi

if ! wait "${qspi}"; then
	error_message+="Error flashing qspi\n"
	should_exit="1"
fi

wait

popd

if [ -n "${should_exit}" ]; then
	exit 1
fi



# The following function makes mmcblk0boot0 and mmcblk0boot1 read only, as we
# had disabled the read only at the beginning of the script. It will also
# unmount ${MOUNT_LOC} and ${TMP_MOUNT} if they are mounted.
cleanup

echo "${SCRIPT_NAME} Successful restore of partitions on target board."
