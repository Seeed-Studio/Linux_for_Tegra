#!/bin/bash

# Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
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
#
# This is a script to sign UEFI payloads with a db key

trap "catch_err" ERR

function catch_err {
	echo "l4t_uefi_sign_image.sh: error occurred !!!"
	exit 4
}

image=""
key_file=""
cert_file=""
mode=""

# Function to printout help section of the script.
function Usage {
        cat <<EOF

---------------------------------------------------------------------------
This script is used to generate a UEFI signed image.
---------------------------------------------------------------------------

Usage:
$1 [-h] --image <image> --key <key_file> --cert <crt_file> --mode <sign_mode>

   --image:  The image file from which this script will generate a UEFI signed image (depends on <sign_mode>)
   --key:    UEFI key file
   --cert:   UEFI crt file
   --mode:   [split, nosplit, append]

The UEFI signed image generated depends on the <sign_mode>:
   split:    <image>.sig is generated
   nosplit:  <image> is appended with the key's certificate and signature;
             original <image> is saved as <image>.unsigned
   append:   <image> is first aligned at 2KB boundary, then is appended with the key's certificate and signature;
             orignal <image> is saved as <image>.unsigned.

Notes:
  1. For <image> that is not in PE/COFF format, like extlinux.conf, initrd or kernel-dtb,
     the <sign_mode> should be split

  2. For <image> that is in PE/COFF format, like kernel image, or BOOTAA64.efi,
     the <sign_mode> should be nosplit

  3. For <image> that is to be saved to partitions, like boot.img, kernel-dtb, recovery.img, or recovery-kernel-dtb,
     the <sign_mode> should be append

  4. All signed images are generated on the same directory of <image>

EOF

	exit 1;
}

function chkerr {
	if [ $? -ne 0 ]; then
		if [ "$1" != "" ]; then
			echo "$1";
		else
			echo "failed.";
		fi;
		exit 1;
	fi;
	if [ "$1" = "" ]; then
		echo "done.";
	fi;
}

function check_file {
	if [ ! -f "$1" ]; then
		echo "$1: No such file" >&2
		exit 2
	fi
}

opstr+="h-:";
while getopts "${opstr}" OPTION; do
	case $OPTION in
		h) Usage "$0";;
		-) case ${OPTARG} in
			image)
				image="${!OPTIND}";
				OPTIND=$((OPTIND + 1));
				;;
			key)
				key_file="${!OPTIND}";
				OPTIND=$((OPTIND + 1));
				;;
			cert)
				cert_file="${!OPTIND}";
				OPTIND=$((OPTIND + 1));
				;;
			mode)
				mode="${!OPTIND}";
				OPTIND=$((OPTIND + 1));
				;;
			*) Usage "$0";;
		esac;;
	*) Usage;;
	esac;
done

if [ -z "${image}" ]; then
	echo "--image not specified"
	Usage "$0"
else
	check_file "${image}"
fi

if [ -z "${key_file}" ]; then
	echo "--key not specified"
	Usage "$0"
else
	check_file "${key_file}"
	key_file=$(readlink -f "${key_file}")
fi

if [ -z "${cert_file}" ]; then
	echo "--cert not specified"
	Usage "$0"
else
	check_file "${cert_file}"
	cert_file=$(readlink -f "${cert_file}")
fi

if [ -z "${mode}" ]; then
	echo "--mode not specified"
	Usage "$0"
else
	case ${mode} in
		split)   ;;
		nosplit) ;;
		append)  ;;
		*) Usage "$0";;
	esac;
fi

# cd to ${image}'s directory
image_dir=$(dirname "${image}")
image_base=$(basename "${image}")
pushd "${image_dir}" > /dev/null 2>&1 || exit 3

if [ "${mode}" = "split" ] || [ "${mode}" = "append" ]; then
	# Generate ${image}.sig file
	openssl cms -sign -signer "${cert_file}" -inkey "${key_file}" -binary -in "${image_base}" -outform der -out "${image_base}".sig
	chkerr "openssl generates ${image_base}.sig file failed"
	echo "${image_base}.sig file generated"
	if [ "${mode}" = "append" ]; then
		# The original ${image_base} file will be saved as ${image_base}.unsigned.
		cp "${image_base}" "${image_base}".unsigned
		echo "original ${image_base} is saved to ${image_base}.unsigned"
		# Append the sig file to the end of image (after pad it to %2048).
		truncate -s %2048 "${image_base}" || exit 3
		cat "${image_base}".sig >> "${image_base}"
		echo "${image_base}.sig file appended to ${image_base}"
	fi
elif [ "${mode}" = "nosplit" ]; then
	# Generate a signed image.
	# The original image file will be saved as ${image}.unsigned.
	cp "${image_base}" "${image_base}".unsigned
	echo "original ${image_base} is saved to ${image_base}.unsigned"
	sbsign --key "${key_file}" --cert "${cert_file}" --output "${image_base}" "${image_base}"
	chkerr "sbsign ${image_base} failed"
	echo "${image_base} is signed"
fi;

popd > /dev/null 2>&1 || exit 3
exit 0
