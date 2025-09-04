#!/bin/bash

# SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
# property and proprietary rights in and to this material, related
# documentation and any modifications thereto. Any use, reproduction,
# disclosure or distribution of this material and related documentation
# without an express license agreement from NVIDIA CORPORATION or
# its affiliates is strictly prohibited.

FTPM_KEYS="ftpm_keys.txt"
CA_APP="nvftpm-helper-app"
RSA_EK_HANDLE="0x81010001"
ECC_EK_HANDLE="0x81010002"
EK_PUBKEY_FTPM="ek_pub_ftpm.key"
EK_PUBKEY_PROVISION="ek_pub_provision.key"

# Running state
# 0: initial
# 1: read device sn
# 2: read eps
# 3: read rsa EK public key
# 4: read ec EK public key
state=0
sn=""
eps=""
rsa_ek_pubkey=""
ec_ek_pubkey=""

while IFS= read -r line; do
	if [ -z "${line}" ]; then
		echo "Injecting EPS for sn: ${sn}..."
		if ${CA_APP} -g "0x${eps}"; then
			tpm2_changeeps
		else
			break
		fi

		echo -n "Verifying the RSA EK public key for sn: ${sn}...    "
		echo -e "${rsa_ek_pubkey}" > ${EK_PUBKEY_PROVISION}
		tpm2_createek -c ${RSA_EK_HANDLE} -G rsa -u ${EK_PUBKEY_FTPM} -f pem
		if diff -B -Z ${EK_PUBKEY_FTPM} ${EK_PUBKEY_PROVISION} >& /dev/null; then
			echo "PASS"
		else
			echo "FAIL"
			break
		fi

		echo -n "Verifying the ECC EK public key for sn: ${sn}...    "
		echo -e "${ec_ek_pubkey}" > ${EK_PUBKEY_PROVISION}
		tpm2_createek -c ${ECC_EK_HANDLE} -G ecc -u ${EK_PUBKEY_FTPM} -f pem
		if diff -B -Z ${EK_PUBKEY_FTPM} ${EK_PUBKEY_PROVISION} >& /dev/null; then
			echo "PASS"
		else
			echo "FAIL"
			break
		fi

		state=0
		sn=""
		eps=""
		rsa_ek_pubkey=""
		ec_ek_pubkey=""
		continue
	fi

	if [ "${line}" == "# device_sn" ]; then
		state=1
		continue
	fi
	if [ "${line}" == "# eps" ]; then
		state=2
		continue
	fi
	if [ "${line}" == "# rsa_ek_pubkey" ]; then
		state=3
		continue
	fi
	if [ "${line}" == "# ec_ek_pubkey" ]; then
		state=4
		continue
	fi

	case ${state} in
		1) sn="${line}";;
		2) eps="${line}";;
		3) rsa_ek_pubkey="${rsa_ek_pubkey}${line}\n";;
		4) ec_ek_pubkey="${ec_ek_pubkey}${line}\n";;
		*) echo "Invalid state found: ${state}."; break;;
	esac

done < "${FTPM_KEYS}"
