#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#
# nv_fuse_read.sh: Read fuses in the target board.
#    This script runs in the target board.
#
# Usage: Boot the board in Linux shell and run:
#
#    sudo ./nv_fuse_read.sh [options] [fusename]
#
#    more detail enter './nv_fuse_read.sh -h'
#
# Examples:
#	sudo ./nv_fuse_read.sh 	           --> Display all Tegra fuses.
#

# Fuse info: name, offset and size
declare -A nv_fuse_info=(
	["odmid"]="0x308:8"
	["odminfo"]="0x19c:4"
	["boot_security_info"]="0x168:4"
	["public_key_hash"]="0x64:32|0x55c:32"
	["reserved_odm0"]="0xc8:4"
	["reserved_odm1"]="0xcc:4"
	["reserved_odm2"]="0xd0:4"
	["reserved_odm3"]="0xd4:4"
	["reserved_odm4"]="0xd8:4"
	["reserved_odm5"]="0xdc:4"
	["reserved_odm6"]="0xe0:4"
	["reserved_odm7"]="0xe4:4"
	["odm_lock"]="0x8:4"
	["pk_h1"]="0x820:64"
	["pk_h2"]="0x860:64"
	["revoke_pk_h0"]="0x8a0:4"
	["revoke_pk_h1"]="0x8a4:4"
	["security_mode"]="0xa0:4"
	["system_fw_field_ratchet0"]="0x420:4"
	["system_fw_field_ratchet1"]="0x424:4"
	["system_fw_field_ratchet2"]="0x428:4"
	["system_fw_field_ratchet3"]="0x42c:4"
	["optin_enable"]="0x4a8:4"
	["ecid"]=""
)

usage ()
{
	cat << EOF
usage: sudo ${script_name} [options] [fusename]
where
    options:
        -l -- list fuses supported
        -h -- help
EOF
	exit 0
}

list_fuses ()
{
	for fuse in "${!nv_fuse_info[@]}"
	do
		echo "${fuse}"
	done
	exit 0
}

get_fuse_value ()
{
	local offset=$1
	local size=$2
	local le="";

	# size 4 needs to be dumped in little-endian because it is
	# handled as a number
	if [ "$(($size))" == 4 ]; then
		le="-e"
	fi

	value=$(dd if=$nv_fuse_nvmem bs="$size" count=1 \
		skip="$(($offset))" iflag="skip_bytes" 2>/dev/null | \
		xxd ${le} -g "$size" | awk '{print $2}' | tr -d '\n')
	echo "${value}"
}

nv_fuse_nvmem=/sys/bus/nvmem/devices/fuse/nvmem
script_name=$0
me=$(whoami)
if [ "${me}" != "root" ]; then
	echo "${script_name} requires root privilege."
	exit 1
fi

while getopts "lh" OPTION
do
	case $OPTION in
	l) list_fuses; ;;
	*) usage; ;;
	esac
done

fusename=""
shift $((OPTIND - 1))
if [ $# -gt 0 ]; then
	fusename=$1;
fi

printed=false
for i in "${!nv_fuse_info[@]}"
do
	info=${nv_fuse_info[$i]}
	if [ -z "${fusename}" ] || [ "${fusename}" == "${i}" ]; then
		if [ "${i}" == "public_key_hash" ]; then
			pkhash_0_7=${info%|*}
			pkhash_8_15=${info#*|}

			offset=${pkhash_0_7%:*}
			size=${pkhash_0_7#*:}
			value=$(get_fuse_value ${offset} ${size})
			echo -n "${i}: 0x${value}"

			offset=${pkhash_8_15%:*}
			size=${pkhash_8_15#*:}
			value=$(get_fuse_value ${offset} ${size})
			echo "${value}"
		elif [ "${i}" == "ecid" ]; then
			echo -n "ecid: 0x"
			echo "obase=16; $(cat /sys/devices/platform/efuse-burn/ecid)" | bc
		else
			offset=${info%:*}
			size=${info#*:}
			value=$(get_fuse_value ${offset} ${size})
			echo "${i}: 0x${value}"
		fi
		printed=true
	fi
done

if [ "${printed}" = false ]; then
	echo "Invalid fuse name: ${fusename}"
	exit 1
fi
exit 0
