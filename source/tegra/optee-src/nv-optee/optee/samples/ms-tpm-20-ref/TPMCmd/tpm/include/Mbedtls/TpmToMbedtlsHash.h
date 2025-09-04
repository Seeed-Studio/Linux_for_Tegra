/*
 * Copyright (c) 2023, NVIDIA Corporation & AFFILIATES. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

//** Introduction
//
// This header file is used to 'splice' the MbedTLS hash code into the TPM code.
//
#ifndef HASH_LIB_DEFINED
#define HASH_LIB_DEFINED

#define HASH_LIB_MBEDTLS

#define HASH_ALIGNMENT  RADIX_BYTES

#include <mbedtls/sha1.h>
#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>

//***************************************************************
//** Links to the MbedTLS HASH code
//***************************************************************

// Redefine the internal name used for each of the hash state structures to the
// name used by the library.
// These defines need to be known in all parts of the TPM so that the structure
// sizes can be properly computed when needed.

#define tpmHashStateSHA1_t        mbedtls_sha1_context
#define tpmHashStateSHA256_t      mbedtls_sha256_context
#define tpmHashStateSHA384_t      mbedtls_sha512_context
#define tpmHashStateSHA512_t      mbedtls_sha512_context

// The defines below are only needed when compiling CryptHash.c or CryptSmac.c.
// This isolation is primarily to avoid name space collision. However, if there
// is a real collision, it will likely show up when the linker tries to put things
// together.

#ifdef _CRYPT_HASH_C_

typedef BYTE            *PBYTE;

// Define the interface between CryptHash.c to the functions provided by the
// library. For each method, define the calling parameters of the method and then
// define how the method is invoked in CryptHash.c.
//
// All hashes are required to have the same calling sequence. If they don't, create
// a simple adaptation function that converts from the "standard" form of the call
// to the form used by the specific hash (and then send a nasty letter to the
// person who wrote the hash function for the library).
//
// The macro that calls the method also defines how the
// parameters get swizzled between the default form (in CryptHash.c)and the
// library form.
//
// Initialize the hash context
#define HASH_START_METHOD_DEF   void (HASH_START_METHOD)(PANY_HASH_STATE state)
#define HASH_START(hashState)                                                   \
                ((hashState)->def->method.start)(&(hashState)->state);

// Add data to the hash
#define HASH_DATA_METHOD_DEF                                                    \
                void (HASH_DATA_METHOD)(PANY_HASH_STATE state,                  \
                                        const BYTE *buffer,                     \
                                        size_t size)
#define HASH_DATA(hashState, dInSize, dIn)                                      \
                ((hashState)->def->method.data)(&(hashState)->state, dIn, dInSize)

// Finalize the hash and get the digest
#define HASH_END_METHOD_DEF                                                     \
                void (HASH_END_METHOD)(PANY_HASH_STATE state, BYTE *buffer)
#define HASH_END(hashState, buffer)                                             \
                ((hashState)->def->method.end)(&(hashState)->state, buffer)

// Copy the hash context
// Note: For import, export, and copy, memcpy() is used since there is no
// reformatting necessary between the internal and external forms.
#define HASH_STATE_COPY_METHOD_DEF                                              \
                void (HASH_STATE_COPY_METHOD)(PANY_HASH_STATE to,               \
                                              PCANY_HASH_STATE from,            \
                                              size_t size)
#define HASH_STATE_COPY(hashStateOut, hashStateIn)                              \
                ((hashStateIn)->def->method.copy)(&(hashStateOut)->state,       \
                                              &(hashStateIn)->state,            \
                                              (hashStateIn)->def->contextSize)

// Copy (with reformatting when necessary) an internal hash structure to an
// external blob
#define  HASH_STATE_EXPORT_METHOD_DEF                                           \
                void (HASH_STATE_EXPORT_METHOD)(BYTE *to,                       \
                                          PCANY_HASH_STATE from,                \
                                          size_t size)
#define  HASH_STATE_EXPORT(to, hashStateFrom)                                   \
                ((hashStateFrom)->def->method.copyOut)                          \
                        (&(((BYTE *)(to))[offsetof(HASH_STATE, state)]),        \
                         &(hashStateFrom)->state,                               \
                         (hashStateFrom)->def->contextSize)

// Copy from an external blob to an internal formate (with reformatting when
// necessary
#define  HASH_STATE_IMPORT_METHOD_DEF                                           \
                void (HASH_STATE_IMPORT_METHOD)(PANY_HASH_STATE to,             \
                                                const BYTE *from,               \
                                                 size_t size)
#define  HASH_STATE_IMPORT(hashStateTo, from)                                   \
                ((hashStateTo)->def->method.copyIn)                             \
                        (&(hashStateTo)->state,                                 \
                         &(((const BYTE *)(from))[offsetof(HASH_STATE, state)]),\
                         (hashStateTo)->def->contextSize)

static inline void mbedtls_sha256_start(mbedtls_sha256_context *ctx)            \
{                                                                               \
        mbedtls_sha256_starts_ret(ctx, 0);                                      \
}

static inline void mbedtls_sha384_start(mbedtls_sha512_context *ctx)            \
{                                                                               \
        mbedtls_sha512_starts_ret(ctx, 1);                                      \
}

static inline void mbedtls_sha512_start(mbedtls_sha512_context *ctx)            \
{                                                                               \
        mbedtls_sha512_starts_ret(ctx, 0);                                      \
}

// Function aliases. The code in CryptHash.c uses the internal designation for the
// functions. These need to be translated to the function names of the library.
#define tpmHashStart_SHA1           mbedtls_sha1_starts_ret   // external name of the
                                                              // initialization method
#define tpmHashData_SHA1            mbedtls_sha1_update_ret
#define tpmHashEnd_SHA1             mbedtls_sha1_finish_ret
#define tpmHashStateCopy_SHA1       memcpy
#define tpmHashStateExport_SHA1     memcpy
#define tpmHashStateImport_SHA1     memcpy
#define tpmHashStart_SHA256         mbedtls_sha256_start
#define tpmHashData_SHA256          mbedtls_sha256_update_ret
#define tpmHashEnd_SHA256           mbedtls_sha256_finish_ret
#define tpmHashStateCopy_SHA256     memcpy
#define tpmHashStateExport_SHA256   memcpy
#define tpmHashStateImport_SHA256   memcpy
#define tpmHashStart_SHA384         mbedtls_sha384_start
#define tpmHashData_SHA384          mbedtls_sha512_update_ret
#define tpmHashEnd_SHA384           mbedtls_sha512_finish_ret
#define tpmHashStateCopy_SHA384     memcpy
#define tpmHashStateExport_SHA384   memcpy
#define tpmHashStateImport_SHA384   memcpy
#define tpmHashStart_SHA512         mbedtls_sha512_start
#define tpmHashData_SHA512          mbedtls_sha512_update_ret
#define tpmHashEnd_SHA512           mbedtls_sha512_finish_ret
#define tpmHashStateCopy_SHA512     memcpy
#define tpmHashStateExport_SHA512   memcpy
#define tpmHashStateImport_SHA512   memcpy

#endif // _CRYPT_HASH_C_

#define LibHashInit()
// This definition would change if there were something to report
#define HashLibSimulationEnd()

#endif // HASH_LIB_DEFINED
