#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

# The script is run on the target to flash the QSPI.

QSPI_IMAGE_NAME="qspi.img"
SCRIPT_NAME="kernel_flash_qspi.sh"

function write_qspi() {
	local MTD_DEVICE="$1"
	local qspi_size
	qspi_size=$(mtd_debug info "${1}" | grep "mtd.size" | cut -d ' ' -f 3)

	# sha256sum output: <Hash> <Filename>
	src_hash=$(sha256sum ${QSPI_IMAGE_NAME} | awk '{print $1}')

	echo "Write to QSPI: ......" >/dev/kmsg
	echo "write_hash = ${src_hash}" >/dev/kmsg

	mtd_debug write "${MTD_DEVICE}" 0 "${qspi_size}" ${QSPI_IMAGE_NAME}
	sync

	echo "Write to QSPI done" >/dev/kmsg

	# verify the flashing by read back
	dd if=/dev/mtd0 of=qspi_vf.img bs=4k conv=sync status=progress
	dst_hash=$(sha256sum qspi_vf.img | awk '{print $1}')
	echo "verify QSPI done" >/dev/kmsg
	echo "verify_hash = ${dst_hash}" >/dev/kmsg

	if [ "${src_hash}" == "${dst_hash}" ]; then
		echo "QSPI flash image verify pass" >/dev/kmsg
	else
		echo "QSPI flash image verify failed" >/dev/kmsg
	fi

	sleep 5
	reboot -h 0
}

set -x
echo "executing ${SCRIPT_NAME}" >/dev/kmsg

kernel_ver="$(ls /usr/lib/modules)"
busybox insmod "usr/lib/modules/${kernel_ver}/kernel/drivers/spi/spi-tegra210-quad.ko"

echo "load spi driver" >/dev/kmsg
mtd_debug info /dev/mtd0 >/dev/kmsg

echo "Erasing QSPI ......" >/dev/kmsg

flash_erase /dev/mtd0 0 0
echo "Erasing QSPI Done" >/dev/kmsg
sync

write_qspi /dev/mtd0
/bin/bash
