#!/bin/bash

# Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

set -e

export TPM2TOOLS_TCTI="device:/dev/tpmrm0"

AK_OWNER_PW="ak_test"

AK_ALG="rsa"
HASH_ALG="sha256"
SIGN_ALG="rsassa"
EK_ECC_HANDLE="0x81010002"

FTPM_TEST_OUTPATH="./ftpm_test_out"

SCRIPT_NAME="$(basename "${0}")"

function usage {
	cat << EOM
Usage: ./${SCRIPT_NAME} [OPTIONS]

This script helps to do a local fTPM attestation test.
The required options:
	-p	The fTPM owner password. (Must)
	-h	Shows this help.
EOM
}

function ftpm_attestation_test() {
	local _ak_alg=${1}
	local _hash_alg=${2}
	local _sign_alg=${3}
	local _ek_handle=${EK_ECC_HANDLE}
	local _ak_ctx="${FTPM_TEST_OUTPATH}/ak.ctx"
	local _ak_pub_key_pem="${FTPM_TEST_OUTPATH}/ak_pub_key.pem"
	local _ak_pub_key_data="${FTPM_TEST_OUTPATH}/ak_pub_key.dat"
	local _ek_pub_key_der="${FTPM_TEST_OUTPATH}/ek_pub_key.der"
	local _nonce="${FTPM_TEST_OUTPATH}/nonce_plain"
	local _nonce_enc="${FTPM_TEST_OUTPATH}/nonce_encrypted"
	local _nonce_dec="${FTPM_TEST_OUTPATH}/nonce_decrypted"
	local _sess_ctx="${FTPM_TEST_OUTPATH}/sess.ctx"
	local _result="${FTPM_TEST_OUTPATH}/nonce.diff"
	local _quote_test1_hash_alg="sha512"
	local _quote_test2_hash_alg="sha384"
	local _quote_msg="${FTPM_TEST_OUTPATH}/quote.msg"
	local _quote_sig="${FTPM_TEST_OUTPATH}/quote.sig"
	local _quote_pcr_out="${FTPM_TEST_OUTPATH}/quote_pcr.out"

	echo "[fTPM device]: === local attestation test ==="
	echo "[fTPM device]: Creating ${_ak_alg} AK with sign alg ${_sign_alg} for testing."
	tpm2_createak -C ${_ek_handle} -c ${_ak_ctx} -G ${_ak_alg} -g ${_hash_alg} -s ${_sign_alg} \
			-f pem -u ${_ak_pub_key_pem} -p ${AK_OWNER_PW} -P ${EK_OWNER_PW}
	tpm2_readpublic -c ${_ak_ctx} > ${_ak_pub_key_data}
	_ak_name=$(cat ${_ak_pub_key_data} | grep "^name:" | awk '{print $2}');

	echo "[fTPM device]: Make Credential by ${_ak_alg} EK pub key and associated with AK."
	tpm2_readpublic -c ${_ek_handle} -o ${_ek_pub_key_der} > ${_ak_pub_key_data}
	tpm2_getrandom -o ${_nonce} 64
	tpm2_makecredential -T none -e ${_ek_pub_key_der} -s ${_nonce} -n ${_ak_name} -o ${_nonce_enc}

	echo "[fTPM device]: Active Credential and verify the result."
	tpm2_startauthsession --policy-session -S ${_sess_ctx} -g ${_hash_alg}
	tpm2_policysecret -S ${_sess_ctx} -c 0x4000000B ${EK_OWNER_PW}

	tpm2_activatecredential -c ${_ak_ctx} -C ${_ek_handle} -i ${_nonce_enc} -o ${_nonce_dec} -P "session:${_sess_ctx}" -p ${AK_OWNER_PW};

	tpm2_flushcontext ${_sess_ctx}

	diff ${_nonce} ${_nonce_dec} > ${_result}; sync;
	if [ -s ${_result} ]; then
		echo "[fTPM device]: local attestation test failed!!"
		exti 1
	else
		echo "[fTPM device]: local attestation test successed."
	fi

	# Empty separator line
	echo ""

	echo "[fTPM device]: === local test: Quote verifying PCR SHA512 Bank 0 ==="
	echo "[fTPM device]: Client generating quote."
	tpm2_quote -c ${_ak_ctx} -l ${_quote_test1_hash_alg}:0 -q ${_nonce} -g ${_hash_alg} -m ${_quote_msg} -s ${_quote_sig} \
		-o ${_quote_pcr_out} -p ${AK_OWNER_PW}
	echo "[fTPM device]: Server verifying quote."
	tpm2_checkquote -u ${_ak_pub_key_pem} -g ${_hash_alg} -q ${_nonce} -m ${_quote_msg} -s ${_quote_sig} -f ${_quote_pcr_out}

	# Empty separator line
	echo ""

	echo "[fTPM device]: === local test: Quote verifying PCR SHA384 Bank 0 ~ 7 ==="
	echo "[fTPM device]: Client generating quote."
	tpm2_quote -c ${_ak_ctx} -l ${_quote_test2_hash_alg}:0,1,2,3,4,5,6,7 -q ${_nonce} -g ${_hash_alg} -m ${_quote_msg} -s ${_quote_sig} \
		-o ${_quote_pcr_out} -p ${AK_OWNER_PW}
	echo "[fTPM device]: Server verifying quote."
	tpm2_checkquote -u ${_ak_pub_key_pem} -g ${_hash_alg} -q ${_nonce} -m ${_quote_msg} -s ${_quote_sig} -f ${_quote_pcr_out}

	# Empty separator line
	echo ""
}

if [ "${1}" == "" ]; then
	usage
	exit 1
fi

while getopts "hp:" OPTION
do
	case $OPTION in
		h)
			usage
			exit 0
		;;
		p)
			EK_OWNER_PW="${OPTARG}"
		;;
		*)
			usage
			exit 1
		;;
	esac
done

if [ "${EK_OWNER_PW}" == "" ]; then
	usage
	exit 1
fi

mkdir -p ${FTPM_TEST_OUTPATH}

ftpm_attestation_test "${AK_ALG}" "${HASH_ALG}" "${SIGN_ALG}"
