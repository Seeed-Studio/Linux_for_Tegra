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

#ifndef _NVPKCS11_DT_COMMON_H
#define _NVPKCS11_DT_COMMON_H

#define HARDWARE_TYPE_CCPLEX 0
#define HARDWARE_TYPE_TSEC 1
#define HARDWARE_TYPE_TSECRADAR 2
#define HARDWARE_TYPE_FSI 3
#define HARDWARE_TYPE_MAX 4

#define TOKEN_TYPE_CCPLEX HARDWARE_TYPE_MAX
#define TOKEN_TYPE_TSEC 5
#define TOKEN_TYPE_TSECRADAR 6
#define TOKEN_TYPE_FSI 7

/* Key type indices - Shared across all platforms */
#define NV_KEY_SPEC_NO_KEY                    (0x00000000U)
#define NV_KEY_SPEC_AES_128                   (0x00000001U)
#define NV_KEY_SPEC_AES_256                   (0x00000002U)
#define NV_KEY_SPEC_AES_384                   (0x00000004U)
#define NV_KEY_SPEC_GEN_SECRET_128            (0x00000008U)
#define NV_KEY_SPEC_GEN_SECRET_256            (0x00000010U)
#define NV_KEY_SPEC_GEN_SECRET_384            (0x00000020U)
#define NV_KEY_SPEC_RSA_2048                  (0x00000040U)
#define NV_KEY_SPEC_RSA_3072                  (0x00000080U)
#define NV_KEY_SPEC_RSA_4096                  (0x00000100U)
#define NV_KEY_SPEC_NIST_P256                 (0x00000200U)
#define NV_KEY_SPEC_NIST_P384                 (0x00000400U)
#define NV_KEY_SPEC_NIST_P521                 (0x00000800U)
#define NV_KEY_SPEC_ED25519                   (0x00001000U)
#define NV_KEY_SPEC_ED448                     (0x00002000U)
#define NV_KEY_SPEC_C25519                    (0x00004000U)
#define NV_KEY_SPEC_C488                      (0x00008000U)
#define NV_KEY_SPEC_XMSS_SHA2_20_256          (0x00010000U)
#define NV_KEY_SPEC_UPDATE_SUPPORTED          (0x80000000U)

/* Combined Key defines */
#define NV_KEY_COMB_AES_128_256               (NV_KEY_SPEC_AES_128 | NV_KEY_SPEC_AES_256)
#define NV_KEY_COMB_GEN_SEC_128_256           (NV_KEY_SPEC_GEN_SECRET_128 | NV_KEY_SPEC_GEN_SECRET_256)
#define NV_KEY_COMB_RSA_3072_4096             (NV_KEY_SPEC_RSA_3072 | NV_KEY_SPEC_RSA_4096)
#define NV_KEY_COMB_NIST_P256_C25519          (NV_KEY_SPEC_NIST_P256 | NV_KEY_SPEC_C25519)
#define NV_KEY_COMB_GEN_SEC_128_256_AES_128_256  (NV_KEY_SPEC_GEN_SECRET_128 | NV_KEY_SPEC_GEN_SECRET_256 | NV_KEY_SPEC_AES_128 | NV_KEY_SPEC_AES_256)

#endif /* _NVPKCS11_DT_COMMON_H */
