#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

source /bin/nv_ota_common.func

source "${OTA_WORK_DIR}"/nv_ota_customer.conf

# Invoke ota_update_rootfs_in_recovery() which updates rootfs partition via
# the rootfs updater and trigger Capsule update if needed
source "${OTA_WORK_DIR}"/nv_ota_update_rootfs_in_recovery.sh

rootfs_part=
if ! load_variable "rootfs_part" rootfs_part; then
	ota_log "Failed to load variable \"rootfs_part\""
	exit 1
fi

ota_log "ota_update_rootfs_in_recovery ${rootfs_part} ${OTA_WORK_DIR}"
if ! ota_update_rootfs_in_recovery "${rootfs_part}" "${OTA_WORK_DIR}"; then
	ota_log "Failed to run \"ota_update_rootfs_in_recovery ${rootfs_part} ${OTA_WORK_DIR}\""
	exit 1
fi
