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
#
# ****************************************************************
# Usage:
#	sudo ./gen_uefi_default_keys_dts.sh <uefi_keys.conf>
# ****************************************************************
#
# This script will generate an UEFI default security keys dts based on PK,
# KEK and (upto 2 sets of) db key pairs and certificates. It also generates
# a dtbo file from the dts.
#
# The dts and dtbo files generated are named as:
#    - UefiDefaultSecurityKeys.dts, and
#    - UefiDefaultSecurityKeys.dtbo
#
# The dts and dtbo are generated in the same directory of <uefi_keys.conf>.
#
# User is expected to create these RSA key pairs and certificates for each
# PK, KEK and db keys, and specify those key/certificate filenames in a config
# file <uefi_keys.conf>.
#
# Followings are example steps to generate RSA private key pairs and self-signed
# certificates for PK, KDK, and db:
#
# Generate 2048-bit RSA key without passphrase and create self-signed certificate
# $ openssl req -newkey rsa:2048 -nodes -keyout PK.key  -new -x509 -sha256 \
#	-days 3650 -subj "/CN=my Platform Key/" -out PK.crt
# $ openssl req -newkey rsa:2048 -nodes -keyout KEK.key -new -x509 -sha256 \
#	-days 3650 -subj "/CN=my Key Exchange Key/" -out KEK.crt
# $ openssl req -newkey rsa:2048 -nodes -keyout db.key  -new -x509 -sha256 \
#	-days 3650 -subj "/CN=my Signature Database key/" -out db.crt
#
# The corresponding <uefi_keys.conf> is shown below:
#
#   UEFI_PK_KEY_FILE="PK.key";
#   UEFI_PK_CERT_FILE="PK.crt";
#   UEFI_KEK_KEY_FILE="KEK.key";
#   UEFI_KEK_CERT_FILE="KEK.crt";
#   UEFI_DB_1_KEY_FILE="db_1.key";
#   UEFI_DB_1_CERT_FILE="db_1.crt";
#   UEFI_DB_2_KEY_FILE="db_2.key";
#   UEFI_DB_2_CERT_FILE="db_2.crt";
#
# Notes:
#   1). All files specified in <uefi_keys.conf> must be in the same directory
#       of <uefi_keys.conf>.
#   2). PK, KEK, and db_1 (UEFI_DB_1_XXX) key files are required.
#   3). All UEFI payloads, like kernel, kernel-dtb, etc., are signed by UEFI_DB_1_XXX
#       (not by UEFI_DB_2_XXX).
#   4). UEFI_DB_2_XXX are optional. They are part of the default db variables.
#       User can use the UEFI_DB_2_XXX key to sign their version of UEFI binaries
#       (kernel, kernel-dtb, initrd, or extlinux.conf).
#   5). May need to install efitools.
#
#

trap "catch_err" ERR

catch_err () {
	echo "gen_uefi_default_keys_dts.sh: error occurred !!!"
	exit 1
}

# dts filename
dts_file="UefiDefaultSecurityKeys.dts"

# Temporary output directory
OUT_DIR=_out

if [ "$1" == "" ]; then
	cat << EOF

Usage: sudo ./gen_uefi_default_keys_dts.sh <uefi_keys.conf>

EOF
    exit 1
fi

uefi_keys_conf="$1"

dts_header ()
{
	cat << EOF > ${dts_file}
/** @file
*
*  Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
*
*  SPDX-License-Identifier: BSD-2-Clause-Patent
*
**/
/dts-v1/;
/plugin/;
/ {
    overlay-name = "UEFI default Keys";
    fragment@0 {
        target-path = "/";
        board_config {
            sw-modules = "uefi";
        };
        __overlay__ {
            firmware {
                uefi {
                    variables {
                        gNVIDIAPublicVariableGuid {
                            EnrollDefaultSecurityKeys {
                                data = [01];
                                non-volatile;
                            };
                        };
EOF
}

dbdefault_header ()
{
	cat << EOF >> ${dts_file}
                        gEfiGlobalVariableGuid {
                            dbDefault {
                                data = [
EOF
}

dbdefault_tail ()
{
	cat << EOF >> ${dts_file}
                                ];
                                non-volatile;
                            };
EOF
}

kekdefault_header ()
{
	cat << EOF >> ${dts_file}
                            KEKDefault {
                                data = [
EOF
}

kekdefault_tail ()
{
	cat << EOF >> ${dts_file}
                                ];
                                non-volatile;
                            };
EOF
}

pkdefault_header ()
{
	cat << EOF >> ${dts_file}
                            PKDefault {
                                data = [
EOF
}

pkdefault_tail ()
{
	cat << EOF >> ${dts_file}
                                ];
                                non-volatile;
                            };
                        };
EOF
}

dts_tail ()
{
	cat << EOF >> ${dts_file}
                    };
                };
            };
        };
    };
};
EOF
}

source "${uefi_keys_conf}"
# cd to ${uefi_keys_conf}'s directory
uefi_keys_conf_dir=$(dirname "${uefi_keys_conf}")
pushd "${uefi_keys_conf_dir}" > /dev/null 2>&1 || exit

### Check PK key options:
if [ "${UEFI_PK_KEY_FILE}" = "" ]; then
	echo "UEFI_PK_KEY_FILE is empty"
	exit 1
fi

if [ ! -f "${UEFI_PK_KEY_FILE}" ]; then
	echo "${UEFI_PK_KEY_FILE} does not exist"
	exit 1
fi

if [ "${UEFI_PK_CERT_FILE}" = "" ]; then
	echo "UEFI_PK_CERT_FILE is empty"
	exit 1
fi

if [ ! -f "${UEFI_PK_CERT_FILE}" ]; then
	echo "${UEFI_PK_CERT_FILE} does not exist"
	exit 1
fi

### Check KEK key options:
if [ "${UEFI_KEK_KEY_FILE}" = "" ]; then
	echo "UEFI_KEK_KEY_FILE is empty"
	exit 1
fi

if [ ! -f "${UEFI_KEK_KEY_FILE}" ]; then
	echo "${UEFI_KEK_KEY_FILE} does not exist"
	exit 1
fi

if [ "${UEFI_KEK_CERT_FILE}" = "" ]; then
	echo "UEFI_KEK_CERT_FILE is empty"
	exit 1
fi

if [ ! -f "${UEFI_KEK_CERT_FILE}" ]; then
	echo "${UEFI_KEK_CERT_FILE} does not exist"
	exit 1
fi

### Check db key options:
if [ "${UEFI_DB_1_KEY_FILE}" = "" ]; then
	echo "UEFI_DB_1_KEY_FILE is empty"
	exit 1
fi

if [ ! -f "${UEFI_DB_1_KEY_FILE}" ]; then
	echo "${UEFI_DB_1_KEY_FILE} does not exist"
	exit 1
fi

if [ "${UEFI_DB_1_CERT_FILE}" = "" ]; then
	echo "UEFI_DB_1_CERT_FILE is empty"
	exit 1
fi

if [ ! -f "${UEFI_DB_1_CERT_FILE}" ]; then
	echo "${UEFI_DB_1_CERT_FILE} does not exist"
	exit 1
fi

if [ -f "${UEFI_DB_2_KEY_FILE}" ] || [ -f "{UEFI_DB_2_CERT_FILE}" ]; then
		echo "Another db (DB_2) is specified"
else
	echo "Set both db_2 key and certificate to NULL"
	UEFI_DB_2_KEY_FILE=""
	UEFI_DB_2_CERT_FILE=""
fi

rm -rf ${OUT_DIR}
mkdir ${OUT_DIR}

# Generate all *.cer, *.esl and *.auth files in _out directory
# All .cer, .esl and .auth names are based on the .key name (UEFI_XX_KEY_FILE)
#
# Get basename (without extension) of all files
UEFI_PK_BASENAME=$(basename "${UEFI_PK_KEY_FILE%.*}")
UEFI_KEK_BASENAME=$(basename "${UEFI_KEK_KEY_FILE%.*}")
UEFI_DB_1_BASENAME=$(basename "${UEFI_DB_1_KEY_FILE%.*}")
if [ "${UEFI_DB_2_KEY_FILE}" ]; then
	UEFI_DB_2_BASENAME=$(basename "${UEFI_DB_2_KEY_FILE%.*}")
fi

# Generate PK.cer, PK.esl and PK.auth
openssl x509 -outform DER -in "${UEFI_PK_CERT_FILE}" -out ${OUT_DIR}/"${UEFI_PK_BASENAME}".cer
cert-to-efi-sig-list -g "$(uuidgen)" "${UEFI_PK_CERT_FILE}" ${OUT_DIR}/"${UEFI_PK_BASENAME}".esl
sign-efi-sig-list -k "${UEFI_PK_KEY_FILE}" -c "${UEFI_PK_CERT_FILE}" PK ${OUT_DIR}/"${UEFI_PK_BASENAME}".esl ${OUT_DIR}/"${UEFI_PK_BASENAME}".auth

# Generate KEK.cer, KEK.esl and KEK.auth
openssl x509 -outform DER -in "${UEFI_KEK_CERT_FILE}" -out ${OUT_DIR}/"${UEFI_KEK_BASENAME}".cer
cert-to-efi-sig-list -g "$(uuidgen)" "${UEFI_KEK_CERT_FILE}" ${OUT_DIR}/"${UEFI_KEK_BASENAME}".esl
sign-efi-sig-list -k "${UEFI_PK_KEY_FILE}" -c "${UEFI_PK_CERT_FILE}" KEK ${OUT_DIR}/"${UEFI_KEK_BASENAME}".esl ${OUT_DIR}/"${UEFI_KEK_BASENAME}".auth

# Generate db_1.cer, db_1.esl and db_1.auth
openssl x509 -outform DER -in "${UEFI_DB_1_CERT_FILE}" -out ${OUT_DIR}/"${UEFI_DB_1_BASENAME}".cer
cert-to-efi-sig-list  -g "$(uuidgen)" "${UEFI_DB_1_CERT_FILE}" ${OUT_DIR}/"${UEFI_DB_1_BASENAME}".esl
sign-efi-sig-list -k "${UEFI_KEK_KEY_FILE}" -c "${UEFI_KEK_CERT_FILE}" db ${OUT_DIR}/"${UEFI_DB_1_BASENAME}".esl ${OUT_DIR}/"${UEFI_DB_1_BASENAME}".auth

# Generate db_2.cer, db_2.esl and db_2.auth
if [ "${UEFI_DB_2_KEY_FILE}" ]; then
	openssl x509 -outform DER -in "${UEFI_DB_2_CERT_FILE}" -out ${OUT_DIR}/"${UEFI_DB_2_BASENAME}".cer
	cert-to-efi-sig-list -g "$(uuidgen)" "${UEFI_DB_2_CERT_FILE}" ${OUT_DIR}/"${UEFI_DB_2_BASENAME}".esl
	sign-efi-sig-list -k "${UEFI_KEK_KEY_FILE}" -c "${UEFI_KEK_CERT_FILE}" db ${OUT_DIR}/"${UEFI_DB_2_BASENAME}".esl ${OUT_DIR}/"${UEFI_DB_2_BASENAME}".auth
fi

### Begin generating dts file:
dts_header;

dbdefault_header;
data=$(od -t x1 -An ${OUT_DIR}/"${UEFI_DB_1_BASENAME}".esl)
echo "${data}" >> ${dts_file}
if [ "${UEFI_DB_2_KEY_FILE}" ]; then
	data=$(od -t x1 -An ${OUT_DIR}/"${UEFI_DB_2_BASENAME}".esl)
	echo "${data}" >> ${dts_file}
fi
dbdefault_tail;

kekdefault_header;
data=$(od -t x1 -An ${OUT_DIR}/"${UEFI_KEK_BASENAME}".esl)
echo "${data}" >> ${dts_file}
kekdefault_tail;

pkdefault_header;
data=$(od -t x1 -An ${OUT_DIR}/"${UEFI_PK_BASENAME}".esl)
echo "${data}" >> ${dts_file}
pkdefault_tail;

dts_tail;

echo "dts file is generated to" "${dts_file}"

dts_file_base=$(basename "${dts_file%.*}")
dtc -I dts -O dtb "${dts_file}" -o "${dts_file_base}.dtbo"

echo "dtbo file is generated to" "${dts_file_base}.dtbo"

popd  > /dev/null 2>&1 || exit
exit 0
