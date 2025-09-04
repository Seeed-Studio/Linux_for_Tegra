#!/bin/bash

# Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

# Required Python modules:
#   sudo apt-get update
#   sudo apt-get install python3-pip
#   python3 -m pip install asn1crypto (version 1.5.1)
#   python3 -m pip install ecdsa (version 1.14.0)
#   python3 -m pip install numpy (version 1.24.4)
#   python3 -m pip install oscrypto (version 1.3.0)
#   python3 -m pip install pyaes (version 1.6.1)
#   python3 -m pip install pycryptodomex (version 3.19.0)

set -e

# Place to store the fTPM output files (e.g. EK CSR files)
FTPM_OUT_PATH="./ftpm_out"

# Variables for EK CSR
SUBJECT_COMMON_NAME="_ftpm-ek-cert"
SUBJECT_ORG_NAME="ftpm corp"
SUBJECT_COUNTRY_NAME="US"

# The prefix of EK CSR file name: ek_csr
PREFIX_EK_CSR_FILE_NAME="ek_csr"

# fTPM Gen EK CSR TOOL
FTPM_GEN_EK_CSR_PYTHON_SCRIPT="./ftpm_manufacturer_gen_ek_csr_tool.py"

SCRIPT_NAME="$(basename "${0}")"

function usage {
	cat << EOM
Usage: ./${SCRIPT_NAME} [OPTIONS]

This script helps to generate fTPM EK CSRs.
The required options:
	-s	The Serial Number of the EK CSR. (Must)
	-e	The EPS for fTPM to generate EK. This should be 64 bytes (128 digits) hex format string. (Must)
	-h	Shows this help.
EOM
}

function gen_ek_csr() {
	local _ek_csr_file

	_ek_csr_file="${PREFIX_EK_CSR_FILE_NAME}"

	# Generate the EK CSR
	echo "[fTPM tool]: === Generating the EK CSR ==="
	${FTPM_GEN_EK_CSR_PYTHON_SCRIPT} --out_path "${FTPM_OUT_PATH}" \
					 --subject_cn "${SN}${SUBJECT_COMMON_NAME}" \
					 --subject_on "${SUBJECT_ORG_NAME}" \
					 --subject_Cn "${SUBJECT_COUNTRY_NAME}" \
					 --sn "${SN}" \
					 --ek_csr "${_ek_csr_file}" \
					 --eps "${EPS}"

	# Dump the EK CSR template
	echo "[fTPM device]: === Dump the RSA EK CSR ==="
	openssl req -inform DER -text -noout -in ${FTPM_OUT_PATH}/${_ek_csr_file}_rsa-${SN}.der
	echo "[fTPM device]: === Dump the EC EK CSR ==="
	openssl req -inform DER -text -noout -in ${FTPM_OUT_PATH}/${_ek_csr_file}_ec-${SN}.der

	# Empty separator line
	echo ""
}

mkdir -p ${FTPM_OUT_PATH}

if [ "${1}" == "" ]; then
	usage
	exit 1
fi

while getopts "hs:e:" OPTION
do
	case $OPTION in
		h)
			usage
			exit 0
		;;
		s)
			SN="${OPTARG}"
		;;
		e)
			EPS="${OPTARG}"
		;;
		*)
			usage
			exit 1
		;;
	esac
done

if [ "${SN}" == "" ] || [ "${EPS}" == "" ]; then
	usage
	exit 1
fi

EPS_LEN=${#EPS}

if [ ${EPS_LEN} != 128 ]; then
	echo "Error: wrong EPS ${EPS_LEN} length."
	exit 1
fi

gen_ek_csr
