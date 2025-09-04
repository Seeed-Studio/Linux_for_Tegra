/*
 * Copyright (c) 2023, NVIDIA Corporation & AFFILIATES. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

//** Introduction
//
// This header file is used to 'splice' the MbedTLS library into the TPM code.

#ifndef SYM_LIB_DEFINED
#define SYM_LIB_DEFINED

#define SYM_LIB_MBEDTLS

#define SYM_ALIGNMENT   RADIX_BYTES

#if ALG_SM4
#undef ALG_SM4
#define ALG_SM4 ALG_NO
#endif

#if ALG_CAMELLIA
#undef ALG_CAMELLIA
#define ALG_CAMELLIA ALG_NO
#endif

#if ALG_TDES
#undef ALG_TDES
#define ALG_TDES ALG_NO
#endif

#include <mbedtls/aes.h>

//***************************************************************
//** Links to the MbedTLS AES code
//***************************************************************

// Define the order of parameters to the library functions that do block encryption
// and decryption.
typedef void(*TpmCryptSetSymKeyCall_t)(
    void        *keySchedule,
    const BYTE  *in,
    BYTE        *out
    );

// The Crypt functions that call the block encryption function use the parameters
// in the order:
//  1) keySchedule
//  2) in buffer
//  3) out buffer
#define SWIZZLE(keySchedule, in, out)                                   \
    (void *)(keySchedule), (const BYTE *)(in), (BYTE *)(out)

// Macros to set up the encryption/decryption key schedules
//
// AES:
#define TpmCryptSetEncryptKeyAES(key, keySizeInBits, schedule)          \
    mbedtls_aes_setkey_enc((tpmKeyScheduleAES *)(schedule), key, keySizeInBits)
#define TpmCryptSetDecryptKeyAES(key, keySizeInBits, schedule)          \
    mbedtls_aes_setkey_dec((tpmKeyScheduleAES *)(schedule), key, keySizeInBits)

// Macros to alias encryption calls to specific algorithms. This should be used
// sparingly. Currently, only used by CryptRand.c
//
// When using these calls, to call the AES block encryption code, the caller
// should use:
//      TpmCryptEncryptAES(SWIZZLE(keySchedule, in, out));
#define TpmCryptEncryptAES          mbedtls_internal_aes_encrypt
#define TpmCryptDecryptAES          mbedtls_internal_aes_decrypt
#define tpmKeyScheduleAES           mbedtls_aes_context

typedef union tpmCryptKeySchedule_t tpmCryptKeySchedule_t;

// This definition would change if there were something to report
#define SymLibSimulationEnd()

#endif // SYM_LIB_DEFINED
