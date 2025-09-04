#!/usr/bin/env python3

# Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

#
# Required Python modules:
#	sudo apt-get update
#	sudo apt-get install python3-pip
# 	python3 -m pip install asn1crypto (version 1.5.1)
#	python3 -m pip install oscrypto (version 1.3.0)
#

import argparse
import datetime
import filecmp
import os
import sys
import textwrap

from asn1crypto import csr, pem, util, x509
from oscrypto import asymmetric, errors

class fTPM_CA_Sign_EK_CSR:
    def __init__(self, ek_csr, ca_cert, ca_priv_key, ek_cert_file_prefix, out_path):
        # Loading the EK CSR
        self.ek_csr = csr.CertificationRequest.load(ek_csr)
        self.ek_csr_cri = self.ek_csr['certification_request_info']
        self.ek_pub_key = asymmetric.load_public_key(self.ek_csr_cri['subject_pk_info'])
        self.ek_algo = self.ek_pub_key.algorithm

        # Loading CA properties
        if pem.detect(ca_cert):
            _, _, _ca_cert_der = pem.unarmor(ca_cert)
        else:
            _ca_cert_der = ca_cert
        self.ca_cert = x509.Certificate.load(_ca_cert_der)
        self.ca_priv_key = asymmetric.load_private_key(ca_priv_key)

        # Preparing the EK cert file
        self.ek_cert_file = out_path + "/" + ek_cert_file_prefix + ".der"
        self.ek_cert_fout = open(self.ek_cert_file, "wb")

    def verify_ek_csr(self):
        _sig = self.ek_csr['signature'].native

        if self.ek_algo == 'rsa':
            verify_func = asymmetric.rsa_pkcs1v15_verify
        elif self.ek_algo == 'ec':
            verify_func = asymmetric.ecdsa_verify
        else:
            raise Exception("[CA Sim]: *** fTPM Manufacturer CA simulation: The EK CSR public key is invalid!!")

        try:
            verify_func(self.ek_pub_key, _sig, self.ek_csr_cri.dump(), "sha256")
        except(errors.SignatureError):
            raise Exception("[CA Sim]: *** fTPM Manufacturer CA simulation: The EK CSR verification failed!!")

    def gen_ek_cert(self):
        # https://trustedcomputinggroup.org/wp-content/uploads/TCG-EK-Credential-Profile-V-2.5-R2_published.pdf

        # EK cert subject name
        _ek_subject = self.ek_csr['certification_request_info']['subject']
        # EK public key info
        _ek_pk_info = self.ek_csr['certification_request_info']['subject_pk_info']
        # Authority key identifier
        _ca_key_id = self.ca_cert.key_identifier_value

        # Extensions
        _extensions = self.ek_csr['certification_request_info']['attributes'][0]['values'][0]
        _ek_ext = []
        for i in range(len(_extensions)):
            if _extensions[i]['extn_id'].native == "subject_alt_name":
                _alt_names = _extensions[i]['extn_value'].native
                _sub_alt_name = x509.Name.build(
                    {
                        "tpm_manufacturer" : _alt_names[0]['tpm_manufacturer'],
                        "tpm_model" : _alt_names[0]['tpm_model'],
                        "tpm_version" : _alt_names[0]['tpm_version']
                    }
                )

                _extn_value = x509.GeneralNames([
                    x509.GeneralName(
                        name = "directory_name",
                        value = _sub_alt_name,
                    )
                ])
            else:
                _extn_value = _extensions[i]['extn_value'].native

            _ek_ext.append({
                "extn_id" : _extensions[i]['extn_id'].native,
                "critical" : _extensions[i]['critical'].native,
                "extn_value": _extn_value
            })

        _ek_ext.extend([
            {
                "extn_id" : "authority_key_identifier",
                "extn_value" : { "key_identifier" : _ca_key_id },
            }
        ])

        # Signature Algorithm
        if self.ca_priv_key.algorithm == 'rsa':
            _sig_algo = "sha256_rsa"
        elif self.ca_priv_key.algorithm == 'ec':
            _sig_algo = "sha256_ecdsa"
        else:
            raise Exception("Error: The CA private key is invalid.")

        _ek_tbs = x509.TbsCertificate(
            {
                "version" : "v3",
                "serial_number" : util.int_from_bytes(_ek_subject.sha1),
                "signature" : { "algorithm" : _sig_algo },
                "issuer" : self.ca_cert.subject,
                "validity" : {
                    "not_before" : x509.UTCTime(datetime.datetime(2023, 6, 1, 0, 0, tzinfo=datetime.timezone.utc)),
                    "not_after" : x509.GeneralizedTime(datetime.datetime(2033, 12, 31, 23, 59, 59, tzinfo=datetime.timezone.utc)),
                },
                "subject" : _ek_subject,
                "subject_public_key_info" : _ek_pk_info,
                "extensions" : _ek_ext,
            }
        )

        # Signing the EK certificate
        if self.ca_priv_key.algorithm == 'rsa':
           sign_func = asymmetric.rsa_pkcs1v15_sign
        elif self.ca_priv_key.algorithm == 'ec':
            sign_func = asymmetric.ecdsa_sign

        _signature = sign_func(self.ca_priv_key, _ek_tbs.dump(), "sha256")

        self.ek_cert = x509.Certificate(
            {
                "tbs_certificate" : _ek_tbs,
                "signature_algorithm" : { "algorithm" : _sig_algo },
                "signature_value" : _signature,
            }
        )

        self.ek_cert_fout.write(self.ek_cert.dump())
        self.ek_cert_fout.close()

        return self.ek_cert_file

def main():
    parser = argparse.ArgumentParser(formatter_class=argparse.RawDescriptionHelpFormatter,
    description=textwrap.dedent('''
    fTPM Manufacturer CA Simulator for signing the EK CSR.
            * Input: The EK CSR.
            * Input: The CA Certificate.
            * Input: The CA private key.
            * Input: The output path to store the EK certificate.
            * Output: The EK certificate.
    '''))

    parser.add_argument('--ek_csr', type=str, required=True, help="The EK CSR file in DER format.")
    parser.add_argument('--ca_cert', type=str, required=True, help="The CA certificate file in PEM format.")
    parser.add_argument('--ca_private_key', type=str, required=True, help="The fTPM CA private key.")
    parser.add_argument('--ek_cert', type=str, required=True, help="The prefix of EK certificate file name. This outputs the certificate in DER format.")
    parser.add_argument('--out_path', type=str, required=True, help="The output folder of the result.")

    args = parser.parse_args()

    if not os.path.exists(args.out_path):
        os.makedirs(args.out_path)

    ftpm_ca = fTPM_CA_Sign_EK_CSR(ek_csr = open(args.ek_csr, "rb").read(),
                                  ca_cert = open(args.ca_cert, "rb").read(),
                                  ca_priv_key = open(args.ca_private_key, "rb").read(),
                                  ek_cert_file_prefix = args.ek_cert,
                                  out_path = args.out_path)
    ftpm_ca.verify_ek_csr()

    output_ek_cert_file = ftpm_ca.gen_ek_cert()
    sys.stdout.write(output_ek_cert_file)


if __name__ == "__main__":
    main()
