/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#ifndef _NVPKCS11_DT_COMMON_LIB_H
#define _NVPKCS11_DT_COMMON_LIB_H

/* PKCS11 Key defines (CKK_*) */
#define CKK_AES                               (0x0000001FUL)
#define CKK_RSA                               (0x00000000UL)
#define CKK_GENERIC_SECRET                    (0x00000010UL)
#define CKK_VENDOR_DEFINED                    (0x80000000UL)
#define CKK_EC                                (0x00000003UL)
#define CKK_EC_EDWARDS                        (0x00000040UL)
#define CKK_EC_MONTGOMERY                     (0x00000041UL)

/* PKCS11 operational flags (CKF_*) */
#define CKF_MESSAGE_ENCRYPT                   (0x00000002UL)
#define CKF_MESSAGE_DECRYPT                   (0x00000004UL)
#define CKF_MESSAGE_SIGN                      (0x00000008UL)
#define CKF_MESSAGE_VERIFY                    (0x00000010UL)
#define CKF_MULTI_MESSAGE                     (0x00000020UL)
#define CKF_ENCRYPT                           (0x00000100UL)
#define CKF_DECRYPT                           (0x00000200UL)
#define CKF_DIGEST                            (0x00000400UL)
#define CKF_SIGN                              (0x00000800UL)
#define CKF_SIGN_MULTIPART                    (0x00001000UL)
#define CKF_VERIFY                            (0x00002000UL)
#define CKF_VERIFY_MULTIPART                  (0x00004000UL)
#define CKF_GENERATE                          (0x00008000UL)
#define CKF_GENERATE_KEY_PAIR                 (0x00010000UL)
#define CKF_WRAP                              (0x00020000UL)
#define CKF_UNWRAP                            (0x00040000UL)
#define CKF_DERIVE                            (0x00080000UL)
#define CKF_EXTENSION                         (0x80000000UL)

#define CKF_NVIDIA_BATCH_MESSAGE_SIGN         (CKF_EXTENSION | 0x00100000UL)
#define CKF_NVIDIA_BATCH_MESSAGE_VERIFY       (CKF_EXTENSION | 0x00200000UL)

/* Flag Combinations */
#define FLAGS_ENCRYPT_DECRYPT                       (CKF_ENCRYPT | CKF_DECRYPT)
#define FLAGS_SIGN_VERIFY                           (CKF_SIGN | CKF_VERIFY)
#define FLAGS_ALL_ENC_DEC                           (CKF_ENCRYPT | CKF_DECRYPT | CKF_MESSAGE_ENCRYPT | CKF_MESSAGE_DECRYPT)
#define FLAGS_ALL_ENC_DEC_MULTI                     (FLAGS_ALL_ENC_DEC | CKF_MULTI_MESSAGE)
#define FLAGS_ALL_SIGNATURE                         (CKF_SIGN | CKF_VERIFY | CKF_SIGN_MULTIPART | CKF_VERIFY_MULTIPART)
#define FLAGS_MESSAGE_SIGN_VERIFY                   (CKF_MESSAGE_SIGN | CKF_MESSAGE_VERIFY)
#define FLAGS_SIGN_VERIFY_MESSAGE_SIGN_VERIFY       (FLAGS_SIGN_VERIFY | FLAGS_MESSAGE_SIGN_VERIFY)
#define FLAGS_MESSAGE_SIGN_VERIFY_MULTI             (FLAGS_MESSAGE_SIGN_VERIFY | CKF_MULTI_MESSAGE)
#define FLAGS_WRAP_UNWRAP                           (CKF_WRAP | CKF_UNWRAP)
#define FLAGS_WRAP_UNWRAP_ALL_ENCRYPTION_MULTI      (FLAGS_WRAP_UNWRAP | FLAGS_ALL_ENC_DEC_MULTI)
#define FLAGS_UNWRAP_ALL_ENC_DEC                    (CKF_UNWRAP | FLAGS_ALL_ENC_DEC)
#define FLAGS_BATCH_MESSAGE_SIGN_VERIFY             (CKF_NVIDIA_BATCH_MESSAGE_SIGN | CKF_NVIDIA_BATCH_MESSAGE_VERIFY)
#define FLAGS_SIGN_VERIFY_BATCH_MESSAGE_SIGN_VERIFY (FLAGS_SIGN_VERIFY | FLAGS_BATCH_MESSAGE_SIGN_VERIFY)

/* PKCS11 mechanism types (CKM_*) */
#define CKM_RSA_PKCS                          (0x00000001UL)
#define CKM_RSA_PKCS_PSS                      (0x0000000DUL)
#define CKM_RSA_PKCS_OAEP                     (0x00000009UL)

#define CKM_SHA256                            (0x00000250UL)
#define CKM_SHA384                            (0x00000260UL)
#define CKM_SHA512                            (0x00000270UL)
#define CKM_SHA3_256                          (0x000002B0UL)
#define CKM_SHA3_384                          (0x000002C0UL)
#define CKM_SHA3_512                          (0x000002D0UL)
#define CKM_SHA256_HMAC                       (0x00000251UL)

#define CKM_GENERIC_SECRET_KEY_GEN            (0x00000350UL)
#define CKM_SP800_108_COUNTER_KDF             (0x000003ACUL)

#define CKM_TLS12_MAC                         (0x000003D8UL)
#define CKM_TLS12_KDF                         (0x000003D9UL)
#define CKM_TLS12_KEY_AND_MAC_DERIVE          (0x000003E1UL)
#define CKM_TLS12_MASTER_KEY_DERIVE_DH        (0x000003E2UL)
#define CKM_TLS12_KEY_SAFE_DERIVE             (0x000003E3UL)

#define CKM_EC_KEY_PAIR_GEN                   (0x00001040UL)
#define CKM_ECDSA                             (0x00001041UL)
#define CKM_ECDH1_DERIVE                      (0x00001050UL)
#define CKM_EC_EDWARDS_KEY_PAIR_GEN           (0x00001055UL)
#define CKM_EC_MONTGOMERY_KEY_PAIR_GEN        (0x00001056UL)
#define CKM_EDDSA                             (0x00001057UL)
#define CKM_AES_KEY_GEN                       (0x00001080UL)
#define CKM_AES_CBC                           (0x00001082UL)
#define CKM_AES_CBC_PAD                       (0x00001085UL)
#define CKM_AES_CTR                           (0x00001086UL)
#define CKM_AES_GCM                           (0x00001087UL)
#define CKM_AES_CMAC                          (0x0000108AUL)
#define CKM_AES_GMAC                          (0x0000108EUL)
#define CKM_VENDOR_DEFINED                    (0x80000000UL)

/* NVIDIA specifc mechanisms */
#define CKM_NVIDIA_AES_CBC_KEY_DATA_WRAP      (CKM_VENDOR_DEFINED | 0x00000001UL)
#define CKM_NVIDIA_SP800_56C_TWO_STEPS_KDF    (CKM_VENDOR_DEFINED | 0x00000002UL)
#define CKM_NVIDIA_MACSEC_AES_KEY_WRAP        (CKM_VENDOR_DEFINED | 0x00000003UL)
#define CKM_NVIDIA_PSC_AES_CMAC               (CKM_VENDOR_DEFINED | 0x00000004UL)
#define CKM_NVIDIA_AES_GCM_KEY_UNWRAP         (CKM_VENDOR_DEFINED | 0x00000005UL)
#define CKM_NVIDIA_OX5B_SHA256_KEY_DERIVATION (CKM_VENDOR_DEFINED | 0x00000006UL)
#define CKM_NVIDIA_SP800_56A_ONE_STEP_KDF     (CKM_VENDOR_DEFINED | 0x00000007UL)
#define CKM_NVIDIA_TSECRADAR_AES_CMAC         (CKM_VENDOR_DEFINED | 0x00000008UL)

/* NVIDIA allowed signature sizes */
#define NVPKCS11_SHA256_DIGEST_SIZE           (32U)
#define NVPKCS11_SHA384_DIGEST_SIZE           (48U)
#define NVPKCS11_SHA512_DIGEST_SIZE           (64U)
#define NVPKCS11_AES_CMAC_SIGNATURE_SIZE      (16U)
#define NVPKCS11_AES_GMAC_SIGNATURE_SIZE      (16U)
#define NVPKCS11_NO_SIGNATURE                 (0U)

#endif /* _NVPKCS11_DT_COMMON_LIB_H */
