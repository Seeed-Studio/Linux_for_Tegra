#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2020-2021 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

# This script is used by customers to implement the process of validating
# the OTA payload. The customers are reponsible for implementing this scirpt
# according to their own security requirements.

ota_validate_payload()
{
	ota_log "Validating OTA payload"
	return 0
}

base_version=
if ! get_base_version_in_recovery base_version; then
	ota_log "Failed to \"get_base_version\""
	exit 1
fi

target_board=
if ! get_target_board_in_recovery target_board; then
	ota_log "Failed to \"get_target_board\""
	exit 1
fi

# Validate the OTA payload
ota_log "ota_validate_payload ${target_board} ${base_version}"
if ! ota_validate_payload "${target_board}" "${base_version}"; then
	ota_log "Failed to run \"ota_validate_payload ${target_board} ${base_version}\""
	exit 1
fi
