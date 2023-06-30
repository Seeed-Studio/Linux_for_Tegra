#!/bin/bash

# Copyright (c) 2020-2022, NVIDIA CORPORATION.  All rights reserved.
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

set -e

SCRIPT_NAME=$(basename "$0")
FILE_SIZE_OFFSET_T19x=8
HEADER_SIZE_T234=8192
HEADER_SIZE_T194=4096
HEADER_SIZE_T186=400

trap cleanup EXIT

function cleanup {
	if [ -d "${L4T_BOOTLOADER_DIR}/__pycache__" ]; then
		rm -rf "${L4T_BOOTLOADER_DIR}/__pycache__"
	fi
}

# Function to printout help section of the script.
function ShowUsage {
	cat <<EOF
---------------------------------------------------------------------------
This script is used to generate
1) The signature header of the provided file in the provided
file's directory, or
2) The original file with the signed header prepended.

This script will encrypt the original file and replace the original file with
encrypted contents when --encrypt_key is specified.
---------------------------------------------------------------------------

Usage:
$1 [-q] --file <file> --chip <chipid> [--key <file> [--encrypt_key <file>]] [--split True|False]
        --type <file type> [--minratchet_config <ratchet config file>]

--key: Key for signing required files (optional)

--encrypt_key: Key for encrypting required files (optional)

--file: The file from which this script will generate a header

--type: The type of the file: kernel|kernel_dtb|data.

--split True|False: Whether to generate a separate signature .sig file or not.
True: generate a separate signature .sig file
False: do not generate .sig file
Default is True

--minratchet_config: The ratchet config file which contains kernel and kernel-dtb ratchet info.

-q: Only print out the output file name

--chip: Chip ID
EOF
}

function write_size_to_sig
{
	# Write size to the signature header for t194 chip. the size is written in little endian
	local sig_file="${1}"
	local size="${2}"
	if [ "${chip}" != "0x19" ]; then
		echo "${SCRIPT_NAME}: chip ${chip}: Don't need to do anything" >&5
		return 0
	fi
	local offset=${FILE_SIZE_OFFSET_T19x}
	if ! echo "${size}" | grep -qE '^[0-9]+$'; then
		echo "${SCRIPT_NAME}: Error: Not a number" >&2
		exit 9
	fi

	# No need to check for maximum size because bash can only support to 2^63 - 1 anyway
	if [ "${size}" -lt 0 ]; then
		echo "${SCRIPT_NAME}: Error: Negative size" >&2
		exit 8
	fi
	local tempfile
	tempfile=$(mktemp)
	echo "${SCRIPT_NAME}: chip ${chip}: add $(printf "0x%x" "${size}") to offset "\
		"$(printf "0x%x" "${offset}") in sig file" >&5
	# Convert size to bytes in little endian
	printf "%16x" "${size}" | tr '[:blank:]' '0' | fold -w2 | tac | tr -d "\n" \
		| xxd -p -r > "${tempfile}"
	# write to header at position 0x8
	dd conv=notrunc if="${tempfile}" of="${sig_file}" bs=1 seek="${offset}" > /dev/null 2>&1;
	rm "${tempfile}"
}

function set_params_using_chipid
{
	if [ "${chip}" = "0x18" ]; then
		offset="${HEADER_SIZE_T186}"
	elif [ "${chip}" = "0x19" ]; then
		offset="${HEADER_SIZE_T194}"
	elif [ "${chip}" = "0x23" ]; then
		offset="${HEADER_SIZE_T234}"
	else
		echo "${SCRIPT_NAME}: Unsupported chip ${chip}" >&2
		exit 7
	fi
}


# assumption: this script is stored inside Linux_for_Tegra folder
L4T_DIR=$(cd "$(dirname "$0")" && pwd)
L4T_BOOTLOADER_DIR="${L4T_DIR}/bootloader"
file=""
ftype=""
ratchet=""
signkey=""
encryptkey=""
chip=""
split="True"
quiet="0"

# if no arguments provided
if [ "$#" = 0 ] || [ "$1" == "-h" ] || [ "$1" == "--help" ]; then
	ShowUsage "$0"
	exit 1;
fi

opstr+="q-:";
while getopts "${opstr}" OPTION; do
	case $OPTION in
	q) quiet="1"; ;;
	-) case ${OPTARG} in
		file)
		file="${!OPTIND}";
		OPTIND=$((OPTIND + 1));
		;;
		type)
		ftype="${!OPTIND}";
		OPTIND=$((OPTIND + 1));
		;;
		minratchet_config)
		ratchet="${!OPTIND}";
		OPTIND=$((OPTIND + 1));
		;;
		key)
		signkey="${!OPTIND}";
		OPTIND=$((OPTIND + 1));
		;;
		encrypt_key)
		encryptkey="${!OPTIND}";
		OPTIND=$((OPTIND + 1));
		;;
		chip)
		chip="${!OPTIND}";
		OPTIND=$((OPTIND + 1));
		;;
		split)
		split="${!OPTIND}";
		OPTIND=$((OPTIND + 1));
		;;
	   esac;;
	*)
	   ShowUsage "$0";
	   ;;
	esac;
done
if [ -z "${file}" ]; then
	ShowUsage "$0"
	exit 1
fi

if ! [ -f "${file}" ]; then
	echo "${SCRIPT_NAME}: ${file}: No such file" >&2
	exit 2
fi
file_size=$(stat --printf="%s" "${file}")


if [ -z "${chip}" ]; then
	ShowUsage "$0"
	exit 1
fi

if [ -n "${signkey}" ] && ! [ -f "${signkey}" ]; then
	echo "${SCRIPT_NAME}: ${signkey}: No such file" >&2
	exit 2
fi

if [ -n "${encryptkey}" ]; then
	if ! [ -f "${encryptkey}" ]; then
		echo "${SCRIPT_NAME}: ${encryptkey}: No such file" >&2
		exit 2
	fi

	# ensure signkey is present
	if [ -z "${signkey}" ]; then
		echo "${SCRIPT_NAME}: The sign key must not be empty" >&2
		exit 5
	fi;
fi

# Create a new file descriptor to duplicate the output to both a variable and
# stdout
if [ ${quiet} -eq 1 ]; then
	exec 5>/dev/null
else
	exec 5>&1
fi
set_params_using_chipid

CMD="${L4T_BOOTLOADER_DIR}/tegraflash.py"
options=(--key "${signkey}")
if [ -n "${encryptkey}" ]; then
	options+=("--encrypt_key" "${encryptkey}")
fi
if [ -n "${ratchet}" ]; then
	options+=("--minratchet_config" "${ratchet}")
fi

echo "${CMD} --chip ${chip} ${options[*]} --cmd sign ${file} ${ftype}" >&5
output="$("${CMD}" --chip "${chip}" "${options[@]}" --cmd "sign \"${file}\" \"${ftype}\"" | tee >(cat - >&5))"
if [ -n "${encryptkey}" ]; then
	signedfile="$(echo "${output}" | grep "Signed and encrypted file:" | \
	sed -n "s/.*Signed and encrypted file: //p")"
else
	signedfile="$(echo "${output}" | grep "Signed file" | sed -n "s/.*Signed file: //p")"
fi
if ! [ -f "${signedfile}" ]; then
	echo "${SCRIPT_NAME}: Error: Unable to find the signed file generated by tegraflash.py" >&2
	exit 6;
fi
echo "${SCRIPT_NAME}: Generate header for $(basename "${signedfile}")" >&5
dd if="${signedfile}" of="${file}.sig" bs="${offset}" count=1 > /dev/null 2>&1;
write_size_to_sig "${file}.sig" "${file_size}"
echo "${SCRIPT_NAME}: Generate 16-byte-size-aligned base file for $(basename "${signedfile}")" >&5
dd if="${signedfile}" of="${file}" bs="${offset}" skip="1" > /dev/null 2>&1;
if [[ "${signedfile}" = *_sigheader.encrypt.signed ]]; then
	filename=$(basename -- "$file")
	extension="${filename##*.}"
	filename_base="${filename%.*}"
	# Fix the case that filename has no extension
	if [ "${extension}" = "${filename_base}" ] && [ "${filename}" != "${filename_base}"."${extension}" ]; then
		newfile="${filename}_sigheader.encrypt.signed"
	else
		newfile="${filename_base}_sigheader.${extension}.encrypt.signed"
	fi
	mv "${signedfile}" "$(dirname "${file}")/${newfile}" 2>/dev/null || true
	newfile=$(realpath "$(dirname "${file}")/${newfile}")
else
	mv "${signedfile}" "$(dirname "${file}")/$(basename "${signedfile}")" 2>/dev/null || true
	newfile=$(realpath "$(dirname "${file}")/$(basename "${signedfile}")")
fi

if [ "${split}" = "True" ]; then
	if [ ${quiet} -eq 1 ]; then
		# Print out realpath of the signature header
		realpath "${file}.sig"
	else
		echo "${SCRIPT_NAME}: the sign header is saved at ${file}.sig"
	fi
	rm "${newfile}"
else
	rm "${file}.sig"
	if [ ${quiet} -eq 1 ]; then
		echo "${newfile}"
	else
		echo "${SCRIPT_NAME}: the signed file is ${newfile}"
	fi
fi;

