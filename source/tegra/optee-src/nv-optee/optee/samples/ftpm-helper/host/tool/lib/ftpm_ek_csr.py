# Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

from asn1crypto import csr, keys, x509
from oscrypto import asymmetric

class fTPM_Gen_EK_CSR():
    def __init__(self, common_name, org_name, country_name, ek_csr_file, ek_priv_key_der, ek_pub_key_der):
        # setup subject info
        self.cn = common_name
        self.org_name = org_name
        self.Cn = country_name

        # Load EK key pair
        self.ek_priv_key = asymmetric.load_private_key(ek_priv_key_der)
        self.ek_pub_key_raw = keys.PublicKeyInfo.load(ek_pub_key_der)
        self.ek_pub_key = asymmetric.load_public_key(ek_pub_key_der)

        # Open EK CSR file
        self.ek_csr_fout = open(ek_csr_file, "wb")

    def build_csr(self):
        # https://trustedcomputinggroup.org/wp-content/uploads/TCG-EK-Credential-Profile-V-2.5-R2_published.pdf
        _csr_subject = x509.Name.build(
            {
                "common_name" : self.cn,
                "organization_name" : self.org_name,
                "country_name" : self.Cn,
            }
        )

        _csr_pub_key_info = self.ek_pub_key_raw

        # https://github.com/microsoft/ms-tpm-20-ref/blob/main/Samples/ARM32-FirmwareTPM/optee_ta/fTPM/reference/include/VendorString.h
        _csr_subject_alt_name = x509.Name.build(
            {
                "tpm_manufacturer" : u"id:4D534654",
                "tpm_model" : u"SSE fTPM",
                "tpm_version" : u"id:20180710",
            }
        )

        _csr_extensions = []

        if self.ek_pub_key.algorithm == "rsa":
            _csr_extensions.append(
                {
                    "extn_id" : "key_usage",
                    "critical" : True,
                    "extn_value" : x509.KeyUsage(set(['key_encipherment'])),
                }
            )
        elif self.ek_pub_key.algorithm == "ec":
            _csr_extensions.append(
                {
                    "extn_id" : "key_usage",
                    "critical" : True,
                    "extn_value" : x509.KeyUsage(set(['digital_signature', 'key_agreement'])),
                }
            )
        else:
            raise Exception("Error: Unknown TPM EK algorithm.")

        _csr_extensions.extend([
            {
                "extn_id" : "basic_constraints",
                "critical" : True,
                "extn_value" : { "ca" : False },
            },
            {
                "extn_id" : "subject_alt_name",
                "critical" : False,
                "extn_value" : x509.GeneralNames([
                    x509.GeneralName(
                        name = "directory_name",
                        value = _csr_subject_alt_name
                    )
                ])
            }
        ])

        _csr_attributes = []
        _csr_attributes.append(
            {
                "type" : "extension_request",
                "values" : [_csr_extensions],
            }
        )

        _csr_info = csr.CertificationRequestInfo(
            {
                "version" : "v1",
                "subject" : _csr_subject,
                "subject_pk_info" : _csr_pub_key_info,
                "attributes" : _csr_attributes,
            }
        )

        # Generate a temporary EC private key for signing the CSR.
        # The CSR will be resigned later in the TPM helper TA by the silicon ID private key.
        #_csr_temp_pub_key, _csr_temp_priv_key = asymmetric.generate_pair("ec", curve="secp256r1")
        #_signature = asymmetric.ecdsa_sign(_csr_temp_priv_key, _csr_info.dump(), "sha256")

        # Signing the EK CSR
        if self.ek_priv_key.algorithm == 'rsa':
            _sig_algo = "sha256_rsa"
            sign_func = asymmetric.rsa_pkcs1v15_sign
        elif self.ek_priv_key.algorithm == 'ec':
            _sig_algo = "sha256_ecdsa"
            sign_func = asymmetric.ecdsa_sign
        else:
            raise Exception("Error: The EK private key is invalid.")

        _signature = sign_func(self.ek_priv_key, _csr_info.dump(), "sha256")

        self.ek_csr = csr.CertificationRequest(
            {
                "certification_request_info" : _csr_info,
                "signature_algorithm" : { "algorithm" : _sig_algo },
                "signature" : _signature,
            }
        )

        self.ek_csr_fout.write(self.ek_csr.dump())
