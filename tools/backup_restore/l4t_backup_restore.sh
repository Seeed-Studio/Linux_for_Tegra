#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2021-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Contributor License Agreement (CLA):
# https://github.com/NVIDIA/DALI_extra/blob/main/NVIDIA_CLA_v1.0.1.docx

set -e

L4T_BACKUP_RESTORE_DIR="$(cd "$(dirname "${0}")" && pwd)"
L4T_TOOLS_DIR="${L4T_BACKUP_RESTORE_DIR%/*}"
L4T_INITRD_FLASH_DIR="${L4T_TOOLS_DIR}/kernel_flash"
INITRD_FLASH_SCRIPT="${L4T_INITRD_FLASH_DIR}/l4t_initrd_flash.sh"
nargs=$#;
target_board=${!nargs};
BACKUP=
CONVERT=
SSH_OPT=(-q -oServerAliveInterval=15 -oServerAliveCountMax=3 -oStrictHostKeyChecking=no -oUserKnownHostsFile=/dev/null)
RAW_IMAGE=

trap clean_up_network_flash EXIT

clean_up_network_flash()
{
	if [ -f /etc/exports ]; then
		sed -i -e '/^# Entry added by NVIDIA initrd flash tool/,+1d' /etc/exports
	fi
	if command -v exportfs &> /dev/null; then
		exportfs -ra
	fi
}

source "${L4T_INITRD_FLASH_DIR}"/l4t_kernel_flash_vars.func
source "${L4T_INITRD_FLASH_DIR}"/l4t_network_flash.func
source "${L4T_BACKUP_RESTORE_DIR}"/l4t_backup_restore.func

usage() {
	echo "Usage: $(basename "${0}") -b [ -c ] <board-name> "
	echo "       $(basename "${0}") -r <board-name> "
	cat <<EOT

	This script creates a backup image of a Jetson device or restores a Jetson device using a backup image.

	This script should be run inside initramfs.

	Options:
		<board-name>                 Indicate which board to use.
		-u <PKC key file>            PKC key used for odm fused board.
		-v <SBK key file>            SBK key used for encryptions
		-h | --help : Display this message
		-b : Generate the backup image and store it in ${L4T_BACKUP_RESTORE_DIR}/images
		-r : Restore the backup image from ${L4T_BACKUP_RESTORE_DIR}/images
		--raw-image ---------------- Specify the path of the raw disk image to be restored into storage devce.
EOT
	exit 0
}

while getopts "brhcu:v:-:" arg; do
	case $arg in
	h) usage;;
	b) BACKUP=1;;
	c) CONVERT=1;;
	r) BACKUP=0;;
	u) KEY_FILE=${OPTARG}; ;;
	v) SBK_KEY=${OPTARG}; ;;
	-) case ${OPTARG} in
		raw-image)
			RAW_IMAGE="${!OPTIND}";
			OPTIND=$((OPTIND + 1)); ;;
		*) usage; ;;
		esac ;;
	*) usage; ;;
	esac
done

cmd=()
if [ -n "${KEY_FILE}" ] && [ -f "${KEY_FILE}" ]; then
	cmd+=("-u" "${KEY_FILE}")
fi

if [ -n "${SBK_KEY}" ] && [ -f "${SBK_KEY}" ]; then
	cmd+=("-v" "${SBK_KEY}")
fi

if [ -z "${BACKUP}" ]; then
	echo "Invalid operation"
	usage
fi

run_command_on_target()
{
	echo "Run command: ${1} on root@fc00:1:1::2"
	sshpass -p root ssh "${SSH_OPT[@]}"  "root@fc00:1:1::2" "$1";
}

# Use network mounted directory for backup and restore
backup_restore_args="-n "

# Use raw disk image to restore
# Enable nfs for the folder including the raw disk images and
# then copy the required scripts into this folder
if [ -n "${RAW_IMAGE}" ]; then
	if [ ! -f "${RAW_IMAGE}" ]; then
		echo "Error: the specified raw disk image ${RAW_IMAGE} is not found"
		exit 1
	fi
	# Get the absolute path of the directory including raw disk image
	raw_image_path="$(readlink -f "${RAW_IMAGE}")"
	nfs_folder="${raw_image_path%/*}"
	cp "${L4T_BACKUP_RESTORE_DIR}"/l4t_backup_restore.func "${nfs_folder}"/
	cp "${L4T_BACKUP_RESTORE_DIR}/${L4T_BACKUP_PARTITIONS_SCRIPT}" "${nfs_folder}"/
	cp "${L4T_BACKUP_RESTORE_DIR}/${L4T_RESTORE_PARTITIONS_SCRIPT}" "${nfs_folder}"/
	raw_image_name="${RAW_IMAGE##*/}"
	backup_restore_args+="--raw-image /mnt/${raw_image_name} "
else
	nfs_folder="${L4T_BACKUP_RESTORE_DIR}"
fi

"${INITRD_FLASH_SCRIPT}" --initrd --showlogs "${cmd[@]}" "${target_board}" mmcblk0p1
enable_nfs_for_folder "${nfs_folder}" "usb0"
if [ "${BACKUP}" = 1 ]; then
	run_command_on_target "
ln -s /proc/self/fd /dev/fd && \
mount -o nolock [fc00:1:1::1]:${nfs_folder} /mnt && \
/mnt/${L4T_BACKUP_PARTITIONS_SCRIPT} ${backup_restore_args} && \
echo Backup image is stored in ${L4T_BACKUP_RESTORE_DIR}/images
"
	if [ -n "${CONVERT}" ]; then
		convert_backup_image_to_initrd_flash "${L4T_BACKUP_RESTORE_DIR}/images" "${L4T_INITRD_FLASH_DIR}/images"
	fi
else
	run_command_on_target "
ln -s /proc/self/fd /dev/fd && \
mount -o nolock [fc00:1:1::1]:${nfs_folder} /mnt && \
/mnt/${L4T_RESTORE_PARTITIONS_SCRIPT} ${backup_restore_args}
"
fi

echo "Operation finishes. You can manually reset the device"
