#!/bin/bash

# Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

set -e

export TPM2TOOLS_TCTI="device:/dev/tpmrm0"

# fTPM helper CA
NV_FTPM_HELPER_APP="./nvftpm-helper-app"

# fTPM temp out folder
FTPM_TEMP_OUT="ftpm_temp_out"

# fTPM temp files
FTPM_EKB_RSA_EK_PUB_KEY="${FTPM_TEMP_OUT}/ekb_rsa_ek_pub_key.pem"
FTPM_EKB_EC_EK_PUB_KEY="${FTPM_TEMP_OUT}/ekb_ec_ek_pub_key.pem"
FTPM_RSA_EK_PUB_KEY="${FTPM_TEMP_OUT}/rsa_ek_pub_key.pem"
FTPM_EC_EK_PUB_KEY="${FTPM_TEMP_OUT}/ec_ek_pub_key.pem"

# TPM NV handles
RSA_EK_HANDLE="0x81010001"
EC_EK_HANDLE="0x81010002"
RSA_EK_CERT_HANDLE="0x01c00002"
EC_EK_CERT_HANDLE="0x01c0000a"

# TPM EK cert attribute
EK_CERT_ATTR="ppwrite|writedefine|ppread|ownerread|authread|no_da|platformcreate"

SCRIPT_NAME="$(basename "${0}")"

function usage {
	cat << EOM
Usage: ./${SCRIPT_NAME} [OPTIONS]

This script helps to provision fTPM via EKB.
The required options:
	-r	The RSA EK certificate file name that extracts from EKB. (Must)
	-e	The EC EK certificate file name that extracts from EKB. (Must)
	-p	The fTPM owner password. (Must)
	-h	Shows this help.
EOM
}

function handle_null_ekb {
	echo "[ftpm device]: fTPM provisioning failed."
	exit 1
}

function ftpm_provisioning() {
	echo "[fTPM device]: === fTPM device provisioning ==="

	# Extract the EK certificates from EKB
	echo "[fTPM device]: === Extracting the EK Certs from EKB ==="
	trap handle_null_ekb ERR
	${NV_FTPM_HELPER_APP} -a "${FTPM_RSA_EK_CERT}" -b "${FTPM_EC_EK_CERT}"

	# Extract the EK public keys from the certificates
	openssl x509 -inform DER -pubkey -noout -in ${FTPM_RSA_EK_CERT} > ${FTPM_EKB_RSA_EK_PUB_KEY}
	openssl x509 -inform DER -pubkey -noout -in ${FTPM_EC_EK_CERT} > ${FTPM_EKB_EC_EK_PUB_KEY}

	# Clean up fTPM
	echo "[fTPM device]: === Clean up fTPM. ==="
	tpm2_clear

	# Take the ownership of the fTPM
	echo "[fTPM device]: === Take the ownership of fTPM. ==="
	tpm2_changeauth -c o ${FTPM_OWNER_PW}
	tpm2_changeauth -c e ${FTPM_OWNER_PW}

	# Create EK
	tpm2_createek -c ${RSA_EK_HANDLE} -G rsa -w ${FTPM_OWNER_PW} -P ${FTPM_OWNER_PW} -f pem -u ${FTPM_RSA_EK_PUB_KEY}
	tpm2_createek -c ${EC_EK_HANDLE} -G ecc -w ${FTPM_OWNER_PW} -P ${FTPM_OWNER_PW} -f pem -u ${FTPM_EC_EK_PUB_KEY}

	# Validate the EK public keys
	echo "[fTPM device]: === Validate the EK public keys. ==="
	_rsa_pubkey_diff="${FTPM_RSA_EK_PUB_KEY}.diff"
	_ec_pubkey_diff="${FTPM_EC_EK_PUB_KEY}.diff"
	diff ${FTPM_EKB_RSA_EK_PUB_KEY} ${FTPM_RSA_EK_PUB_KEY} > ${_rsa_pubkey_diff}; sync;
	diff ${FTPM_EKB_EC_EK_PUB_KEY} ${FTPM_EC_EK_PUB_KEY} > ${_ec_pubkey_diff}; sync;
	if [ -s ${_rsa_pubkey_diff} ] || [ -s ${_ec_pubkey_diff} ]; then
		echo "[fTPM device]: The EK keys are invalid. Stop provisioning!!"
		exit 1
	fi

	# Store the TPM EK certificate info fTPM NVMem
	echo "[fTPM device]: === Store the EK certificates. ==="
	_rsa_ek_cert_size=$(wc -c < ${FTPM_RSA_EK_CERT})
	tpm2_nvdefine ${RSA_EK_CERT_HANDLE} -C p -a ${EK_CERT_ATTR} -s ${_rsa_ek_cert_size}
	tpm2_nvwrite ${RSA_EK_CERT_HANDLE} -C p -i ${FTPM_RSA_EK_CERT}

	_ec_ek_cert_size=$(wc -c < ${FTPM_EC_EK_CERT})
	tpm2_nvdefine ${EC_EK_CERT_HANDLE} -C p -a ${EK_CERT_ATTR} -s ${_ec_ek_cert_size}
	tpm2_nvwrite ${EC_EK_CERT_HANDLE} -C p -i ${FTPM_EC_EK_CERT}

	# Check the EK certificate in NV
	RSA_EK_CERT_IN_NV="${FTPM_RSA_EK_CERT}.ftpm_nv"
	EC_EK_CERT_IN_NV="${FTPM_EC_EK_CERT}.ftpm_nv"
	tpm2_nvread ${RSA_EK_CERT_HANDLE} -s ${_rsa_ek_cert_size} -o ${RSA_EK_CERT_IN_NV}
	tpm2_nvread ${EC_EK_CERT_HANDLE} -s ${_ec_ek_cert_size} -o ${EC_EK_CERT_IN_NV}

	_rsa_result="${RSA_EK_CERT_IN_NV}.diff"
	_ec_result="${EC_EK_CERT_IN_NV}.diff"
	diff ${FTPM_RSA_EK_CERT} ${RSA_EK_CERT_IN_NV} > ${_rsa_result}; sync;
	diff ${FTPM_EC_EK_CERT} ${EC_EK_CERT_IN_NV} > ${_ec_result}; sync;
	if [ -s ${_rsa_result} ] || [ -s ${_ec_result} ]; then
		echo "[fTPM device]: EK certificate has not been written in NV!!"
		exti 1
	else
		echo "[fTPM device]: EK certificate saved in NV successuflly."
	fi

	# Empty separator line
	echo ""
}

if [ "${1}" == "" ]; then
	usage
	exit 1
fi

while getopts "hr:e:p:" OPTION
do
	case $OPTION in
		h)
			usage
			exit 0
		;;
		r)
			FTPM_RSA_EK_CERT="${OPTARG}"
		;;
		e)
			FTPM_EC_EK_CERT="${OPTARG}"
		;;
		p)
			FTPM_OWNER_PW="${OPTARG}"
		;;
		*)
			usage
			exit 1
		;;
	esac
done

if [ "${FTPM_RSA_EK_CERT}" == "" ] ||
   [ "${FTPM_EC_EK_CERT}" == "" ] ||
   [ "${FTPM_OWNER_PW}" == "" ]; then
	usage
	exit 1
fi

mkdir -p ${FTPM_TEMP_OUT}
ftpm_provisioning
