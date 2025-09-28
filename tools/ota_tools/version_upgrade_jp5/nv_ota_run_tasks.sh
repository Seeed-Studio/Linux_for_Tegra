#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2021-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

OTA_MAX_RETRY_COUNT=0
OTA_LOG_FILE=

source /bin/nv_ota_internals.sh
source /bin/nv_ota_log.sh
source /bin/nv_ota_exception_handler.sh
source /bin/nv_ota_utils.func

# Initialize log
# ota_log() will record the message into a temp log file before
# init_ota_log() has been called
ota_log "init_ota_log ${OTA_LOG_DIR}"
if ! init_ota_log "${OTA_LOG_DIR}"; then
	ota_log "Failed to run \"init_ota_log ${OTA_PACKAGE_MOUNTPOINT}/ota_log\""
	exit 1
fi
OTA_LOG_FILE="$(get_ota_log_file)"
ota_log "OTA_LOG_FILE=${OTA_LOG_FILE}"

# Initialize exception handler
ota_log "init_exception_handler ${OTA_PACKAGE_MOUNTPOINT} ${OTA_LOG_FILE} ${OTA_MAX_RETRY_COUNT}"
if ! init_exception_handler "${OTA_PACKAGE_MOUNTPOINT}" "${OTA_LOG_FILE}" "${OTA_MAX_RETRY_COUNT}"; then
	ota_log "Failed to run \"init_exception_handler ${OTA_PACKAGE_MOUNTPOINT} ${OTA_LOG_DIR} ${OTA_MAX_RETRY_COUNT}\""
	exit 1
fi

set -e

# Import customer's configuration
source "${OTA_WORK_DIR}"/nv_ota_customer.conf

if [ "${REQUIRE_ETHERNET}" == "true" ]; then
	# Enable remote access through ssh
	ota_log "enable_remote_access"
	if ! enable_remote_access; then
		ota_log "Failed to run \"enable_remote_access\""
		reboot_system
	fi
fi

for task in "${OTA_TASKS[@]}"
do
	ota_log "Running ${task}"
	source "./${task}"
done

# Store the OTA logs into the "PRESERVED_OTA_LOGS_DIR" directory
# if "PRESERVE_LOGS" is set to "true"
if [ "${PRESERVE_LOGS}" == "true" ]; then
   mv "${OTA_PACKAGE_MOUNTPOINT}"/ota_log "${OTA_PACKAGE_MOUNTPOINT}"/"${PRESERVED_OTA_LOGS_DIR}"
fi

clean_up
