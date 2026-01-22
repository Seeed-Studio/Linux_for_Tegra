#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

# This script generates bootloader and kernel multi-specification BUP
# update payloads for Jetson boards

set -e

#l4t_generate_soc_capsule.sh "${@}"
function validate_guid()
{
	local guid="${1}"
	# GUID format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx (8-4-4-4-12 hex digits)
	local guid_pattern="^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"

	if [[ ! "${guid}" =~ ${guid_pattern} ]]; then
		usage "Error. Invalid GUID format: ${guid}. Expected format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
	fi
}

function usage()
{
	if [ -n "${1}" ]; then
		echo "${1}"
		echo ""
	fi

	echo "This is a help script to add the 3 FMP capsule image header"
	echo "to a payload."
	echo "The advanced developer can use the edk2 tools to do it directly."
	echo ""
	echo "Usage:"
	echo "  ${script_name} [-h|--help] [-o <output file> [-i <input file>] <target_soc>"
	echo "  -h|--help      Displays this help prompt."
	echo ""
	echo "  Positional arguments:"
	echo "  -o <output file> Output capsule image name."
	echo "  -i <input file> Input payload image name."
	echo "  <target_soc>   Must be \"t234\" or \"t264\"."
	echo ""
	echo "  Optional arguments:"
	echo "  --fmp-guid <guid>"
	echo "      FMP image type GUID (format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx)."
	echo "      Use this to override the default GUID set by target_soc."
	echo "  --signer-private-cert <signer private cert>"
	echo "      OpenSSL signer private certificate filename."
	echo "      Use the public TestCert.pem by default."
	echo "  --other-public-cert <other public cert>"
	echo "      OpenSSL other public certificate filename."
	echo "      Use the public TestSub.pub.pem by default."
	echo "  --trusted-public-cert <trusted public cert>"
	echo "      OpenSSL trusted public certificate filename."
	echo "      Use the public TestRoot.pub.pem by default."
	echo ""
	echo "Examples:"
	echo "  ${script_name} -i bl_only_payload -o TEGRA-T264.Cap t264"
	echo "  ${script_name} -i bl_only_payload -o TEGRA-T264.Cap --fmp-guid 12345678-1234-1234-1234-123456789abc t264"
	echo ""

	exit 1
}

function parse_options()
{
	if [ -z "$3" ]; then
		usage "Error. Arguments required"
	fi

	while [ -n "${1}" ]; do
		case "${1}" in
			-h | --help)
				usage
				;;
			-o)
				[ -n "${2}" ] || usage "Not enough parameters"
				outputfile="${2}"
				shift 2
				;;
			-i)
				[ -n "${2}" ] || usage "Not enough parameters"
				inputfile="${2}"
				shift 2
				;;
			--signer-private-cert)
				[ -n "${2}" ] || usage "Not enough parameters"
				cap_signer_private_cert="${2}"
				is_cap_signer_private_cert="true"
				shift 2
				;;
			--other-public-cert)
				[ -n "${2}" ] || usage "Not enough parameters"
				cap_other_public_cert="${2}"
				is_cap_other_public_cert="true"
				shift 2
				;;
			--trusted-public-cert)
				[ -n "${2}" ] || usage "Not enough parameters"
				cap_trusted_public_cert="${2}"
				is_cap_trusted_public_cert="true"
				shift 2
				;;
			--fmp-guid)
				[ -n "${2}" ] || usage "Not enough parameters"
				validate_guid "${2}"
				set_fmp_guid="${2}"
				shift 2
				;;
			t234)
				if [[ "${target_soc}" != "" ]]; then
					usage "Error. target_soc is specified multiple times"
				fi
				target_soc="t234"
				cap_guid="${guid_t234}"
				shift 1
				;;
			t264)
				if [[ "${target_soc}" != "" ]]; then
					usage "Error. target_soc is specified multiple times"
				fi
				target_soc="t264"
				cap_guid="${guid_t264}"
				shift 1
				;;
			*)
				usage "Error. Unknown option: ${1}"
				;;
		esac
	done

	if [[ "${outputfile}" == "" ]] || [[ "${inputfile}" == "" ]]; then
		usage "Error. Missing required arguments: -o <output file> and -i <input file>."
	fi

	if [[ "${target_soc}" == "" ]]; then
		usage "Error. Missing required argument: <target_soc> (t234 or t264)."
	fi

	# Set the FMP image type GUID if --fmp-guid is specified
	if [[ "${set_fmp_guid}" != "" ]]; then
		cap_guid="${set_fmp_guid}"
	fi
}

script_name="$(basename "${0}")"
l4t_dir="$(cd "$(dirname "${0}")/../" && pwd)"
build_capsule_dir="$(cd "$(dirname "${0}")" && pwd)"
bsp_version_file="${l4t_dir}/nv_tegra/bsp_version"

# Test key/certs.
# 'test' keys/certs that are public in the edk2 source
# They are enabled in the uefi build.
def_signer_private_cert="${build_capsule_dir}/Pkcs7Sign/TestCert.pem"
def_other_public_cert="${build_capsule_dir}/Pkcs7Sign/TestSub.pub.pem"
def_trusted_public_cert="${build_capsule_dir}/Pkcs7Sign/TestRoot.pub.pem"

# FW version.
if [ -f "${bsp_version_file}" ]; then
	source "${bsp_version_file}"
	# Note: default text values for variables are treated as zeros
	BSP_VERSION32=$( printf "0x%x" $(( (BSP_BRANCH<<16) | (BSP_MAJOR<<8) | BSP_MINOR )) )
else
	BSP_VERSION32="0x00000000"
fi
def_fw_version="${BSP_VERSION32}"
def_lsv="${BSP_VERSION32}"

# Default FMP Image Type ID GUIDs.
guid_t234="bf0d4599-20d4-414e-b2c5-3595b1cda402"
guid_t264="3c834404-d2d7-4912-8c1c-6e850b5f2824"

cap_signer_private_cert="${def_signer_private_cert}"
cap_other_public_cert="${def_other_public_cert}"
cap_trusted_public_cert="${def_trusted_public_cert}"

is_cap_signer_private_cert="false"
is_cap_other_public_cert="false"
is_cap_trusted_public_cert="false"
set_fmp_guid=""

parse_options "${@}"

# Check and warn the user if the certificates are not set
if [ "${is_cap_signer_private_cert}" != "true" ]; then
	echo "Warning. The signer private certificate is not set, the test certificate TestCert.pem is used!"
fi
if [ "${is_cap_other_public_cert}" != "true" ]; then
	echo "Warning. The other public certificate is not set, the test certificate TestSub.pub.pem is used!"
fi
if [ "${is_cap_trusted_public_cert}" != "true" ]; then
	echo "Warning. The trusted public certificate is not set, the test certificate TestRoot.pub.pem is used!"
fi

python "${build_capsule_dir}"/Capsule/GenerateCapsule.py -v --encode --monotonic-count 1 \
	--fw-version "${def_fw_version}" \
	--lsv "${def_lsv}" \
	--guid "${cap_guid}" \
	--signer-private-cert "${cap_signer_private_cert}" \
	--other-public-cert "${cap_other_public_cert}" \
	--trusted-public-cert "${cap_trusted_public_cert}" \
	-o "${outputfile}" "${inputfile}"
