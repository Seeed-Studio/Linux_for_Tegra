#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

if [ -z "$1" ]; then
    echo "Usage: ./read_eeprom.sh <chipid>  # <chipid> is required: 0x23 or 0x26"
    echo ""
    echo "program terminates."
    exit 1
fi

chipid="$1"
ret=0;
tmpdir=$(mktemp -d);
mkdir -p "${tmpdir}" > /dev/null 2>&1;
if ! tar xf rcmdump_blob.tar --directory="${tmpdir}"; then
    echo "Failed to extract rcmdump_blob.tar";
    ret=1;
fi;

pushd "${tmpdir}" || exit;

if [ -f read_eeprom.func ]; then
    source read_eeprom.func;
    read_eeprom "${chipid}";
else
    echo "read_eeprom.func not found."
    ret=2;
fi;
popd || exit;
rm -rf "${tmpdir}";
exit ${ret};
