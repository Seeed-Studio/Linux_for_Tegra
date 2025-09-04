#!/usr/bin/env python3

# Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

# Required Python modules:
#   sudo apt-get update
#   sudo apt-get install python3-pip
#   sudo agp-get remove python3-cryptography
#   python3 -m pip install asn1crypto (version 1.5.1)
#   python3 -m pip install cryptography (version 41.0.1)
#   python3 -m pip install ecdsa (version 1.14.0)
#   python3 -m pip install numpy (version 1.24.4)
#   python3 -m pip install oscrypto (version 1.3.0)
#   python3 -m pip install pyaes (version 1.6.1)
#   python3 -m pip install pycryptodomex (version 3.19.0)

import argparse
import os
import textwrap

from ecdsa import NIST256p
from lib.aes_drbg import AES_DRBG
from lib.ftpm_ek import RSA2K_Gen_Key, EC_Gen_Key
from lib.ftpm_ek_csr import fTPM_Gen_EK_CSR

class fTPM_EK_CSR_Tool:
    def __init__(self, key_type, eps_seed):
        _magic_purpose_str = "5072696d617279204f626a656374204372656174696f6e00"
        _magic_rsa_name_str = "000b32503929a1287eedaa3e89d932f9b51a6f92abd0fa57721ffa6fc041e04f7498"
        _magic_ec_name_str = "000b0f1277a2f3f382e7f75db466fac234182a8d62f97dfbaae7b06fdf52bda51467"

        if key_type == 'rsa' or key_type == 'ec':
            self.key_type = key_type
        else:
            raise Exception("fTPM_Create_EK: Invalid key type!!")

        self.eps = eps_seed
        self.drbg = AES_DRBG(256)

        _magic_purpose_hex = bytes.fromhex(_magic_purpose_str)
        if key_type == 'rsa':
            _magic_name_hex = bytes.fromhex(_magic_rsa_name_str)
        elif key_type == 'ec':
            _magic_name_hex = bytes.fromhex(_magic_ec_name_str)

        self.drbg.df_instantiate_seeded(self.eps, _magic_purpose_hex, _magic_name_hex)

        if key_type == 'rsa':
            rsa_gen_key = RSA2K_Gen_Key(2048, self.drbg.df_drbg_generate)
            self.ek_priv_key, self.ek_pub_key = rsa_gen_key.generate_key_pair()
        elif key_type == 'ec':
            ec_gen_key = EC_Gen_Key(NIST256p, self.drbg.df_drbg_generate)
            self.ek_priv_key, self.ek_pub_key = ec_gen_key.gen_ecdsa_keypair_by_extra_random_bits(NIST256p, self.drbg.df_drbg_generate)

    def generate_ek_csr(self, subject_cn, subject_on, subject_Cn, ek_csr_file_name):
        ek_csr = fTPM_Gen_EK_CSR(subject_cn, subject_on, subject_Cn, ek_csr_file_name, self.ek_priv_key, self.ek_pub_key)
        ek_csr.build_csr()

def main():
    parser = argparse.ArgumentParser(formatter_class=argparse.RawDescriptionHelpFormatter,
    description=textwrap.dedent('''
    fTPM tool creates the EK CSR.
        To generate a per-device EK CSR, please follow the input requirements.
           * Input: The X.509 common name property.
           * Input: The X.509 organization name property.
           * Input: The X.509 country name property.
           * Input: The serial number.
           * Input: The 64 bytes EPS seed in hex format string.
           * Output: The prefix of EK CSR file name. This will output the EK CSR files (RSA and EC) in DER format.
    '''))

    parser.add_argument('--out_path', type=str, required=True, help="The output folder of the result.")

    parser.add_argument('--subject_cn', type=str, required=True, help="The subject common name of the CSR.")
    parser.add_argument('--subject_on', type=str, required=True, help="The subject organization name of the CSR.")
    parser.add_argument('--subject_Cn', type=str, required=True, help="The subject country name of the CSR.")
    parser.add_argument('--sn', type=str, required=True, help="The serial number.")
    parser.add_argument('--ek_csr', type=str, required=True, help="The prefix of EK CSR file name.")
    parser.add_argument('--eps', type=str, required=True, help="The EPS of the fTPM.")

    args = parser.parse_args()

    _eps = bytes.fromhex(args.eps)
    if (len(_eps) != 64):
        raise Exception("Error: Invalid EPS length.")

    if not os.path.exists(args.out_path):
        os.makedirs(args.out_path)

    _ek_key_type_ec = "ec"
    _ek_key_type_rsa = "rsa"

    print("[fTPM Gen EK CSR TOOL]: Generating fTPM RSA EK CSR")
    ftpm_rsa_ek_csr = fTPM_EK_CSR_Tool(_ek_key_type_rsa, _eps)
    ftpm_rsa_ek_csr.generate_ek_csr(args.subject_cn, args.subject_on, args.subject_Cn, args.out_path + "/" + args.ek_csr + "_" + _ek_key_type_rsa + "-" + args.sn + ".der")

    print("[fTPM Gen EK CSR TOOL]: Generating fTPM EC EK CSR")
    ftpm_ec_ek_csr = fTPM_EK_CSR_Tool(_ek_key_type_ec, _eps)
    ftpm_ec_ek_csr.generate_ek_csr(args.subject_cn, args.subject_on, args.subject_Cn, args.out_path + "/" + args.ek_csr + "_" + _ek_key_type_ec + "-" + args.sn + ".der")

if __name__ == "__main__":
    main()
