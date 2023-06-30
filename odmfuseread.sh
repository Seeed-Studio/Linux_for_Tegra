#!/bin/bash

# Copyright (c) 2020-2023, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
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
# odmfuseread.sh: Read the fuse info from the target board.
#                 It only supports T186 and T194 platforms now.
#
# Usage: Place the board in recovery mode and run:
#
#	./odmfuseread.sh -i <chip_id> [options] target_board
#
#	for more detail enter './odmfuseread.sh -h'
#

usage ()
{
	cat << EOF
Usage:
  ./odmfuseread.sh -i <chip_id> [options] target_board

  where:
    chip_id --------------- Jetson TX2: 0x18, Jetson Xavier: 0x19

  options:
    -k <key_file> --------- The public key file.
    -S <sbk_file> --------- The SBK file.

EOF
    exit 1;
}

cd "$(dirname $0)";
source ./odmfuse.func;

while getopts "i:k:S:" OPTION
do
	case $OPTION in
	i) tid="${OPTARG}"; ;;
	k) KEYFILE="${OPTARG}"; ;;
	S) SBKFILE="${OPTARG}"; ;;
	*) usage; ;;
	esac
done

if [ "${SBKFILE}" != "" ] && [ "${KEYFILE}" = "" ]; then
	echo "L4T doesn't support SBK by itself. Make sure your public key is set."
	exit 1;
fi;

if [ "${tid}" = "" ]; then
	echo "Error: chip_id is missing.";
	usage;
fi;
if [ "${tid}" != "0x18" ] && [ "${tid}" != "0x19" ] && [ "${tid}" != "0x23" ]; then
	echo "Error: Unsupported chip_id: ${tid}";
	usage;
fi;

shift $(($OPTIND - 1));
if [ $# -ne 1 ]; then
	echo "Error: target_board is not set correctly."
	usage;
fi;
cmd_target_board=${1};
if [ ! -r "${cmd_target_board}".conf ]; then
	echo -n "Error: Invalid target board - ";
	echo "${cmd_target_board}.conf is not found.";
	exit 1;
fi;

LDK_DIR="$(pwd)";
LDK_DIR=`readlink -f "${LDK_DIR}"`;
source ${cmd_target_board}.conf
BL_DIR="${LDK_DIR}/bootloader";
TARGET_DIR="${BL_DIR}/${target_board}";
KERNEL_DIR="${LDK_DIR}/kernel";
DTB_DIR="${KERNEL_DIR}/dtb";

odmfuse_init "${tid}" "${usb_instance}" "${CHIPMAJOR}" "${BL_DIR}" "${TARGET_DIR}" "${LDK_DIR}" "${SBKFILE}" "${KEYFILE}";
read_fuse_values;
