/*
 * Copyright (c) 2023, NVIDIA Corporation & AFFILIATES. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

//** Introduction
// This file contains the structure definitions used for ECC in the MbedTLS
// version of the code. These definitions would change, based on the library.
// The ECC-related structures that cross the TPM interface are defined
// in TpmTypes.h
//

#ifndef MATH_LIB_DEFINED
#define MATH_LIB_DEFINED

#define MATH_LIB_MBEDTLS

#include <tomcrypt_private.h>
#include <mbedtls/bignum.h>

#define MPI_VAR(name)                                       \
    mbedtls_mpi     _##name;                                \
    mbedtls_mpi     *name = MpiInitialize(&_##name);

// Allocate a mbedtls_mpi and initialize with the values in a mbedtls_mpi* initializer
#define MPI_INITIALIZED(name, initializer)                  \
    MPI_VAR(name);                                          \
    BnToMbedtls(name, initializer);

#define MPI_FREE(name)                                      \
    mbedtls_mpi_free(name);

#define POINT_CREATE(name, initializer)                     \
    ecc_point   *name = EcPointInitialized(initializer);

#define POINT_DELETE(name)                                  \
    EcPointFree(name);                                      \
    name = NULL;

typedef ECC_CURVE_DATA bnCurve_t;

typedef bnCurve_t  *bigCurve;

#define AccessCurveData(E)  (E)

#define CURVE_INITIALIZED(name, initializer)                \
    bnCurve_t      *name = (ECC_CURVE_DATA *)GetCurveData(initializer)

#define CURVE_FREE(E)

#include "TpmToMbedtlsSupport_fp.h"

// This definition would change if there were something to report
#define MathLibSimulationEnd()

#endif // MATH_LIB_DEFINED
