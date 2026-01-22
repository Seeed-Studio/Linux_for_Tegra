#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2023-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

# Usage: ./l4t_generate_igx_qspi_for_bmcflash.sh orin|thor [-u signkey -v encryptkey]
#
set -e

# Convert $1 to lowercase
lower_param="${1,,}"

if [ "$lower_param" == "orin" ]; then
    BOARD_SPEC=("BOARDID=3701" "FAB=000" "BOARDSKU=0008" "CHIP_SKU=00:00:00:90" \
        "RAMCODE_ID=5")
    # Save the first parameter and shift it out
    platform="igx-orin-devkit"
    shift

elif [ "$lower_param" == "thor" ]; then
    BOARD_SPEC=("BOARDID=3834" "FAB=000" "BOARDSKU=0008" \
        "CHIP_SKU=00:00:00:00" "RAMCODE_ID=12" \
        "FUSELEVEL_PRODUCTION=1")
    # Save the first parameter and shift it out
    platform="igx-thor-devkit"
    shift
else
    echo "Unknown Platform type: it must be either orin or thor"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
L4T_DIR=$(readlink -f "${SCRIPT_DIR}/../..")
pushd "${L4T_DIR}" > /dev/null

sudo "${BOARD_SPEC[@]}" tools/qspi_flash/generate_qspi_for_flash.sh \
        --flash-mode BMC ${platform} "$@"

popd > /dev/null
