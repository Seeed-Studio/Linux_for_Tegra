/*
 * Copyright (c) 2023, NVIDIA Corporation & AFFILIATES. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

//** Introduction
//
// The functions in this file are used for initialization of the interface to the
// MbedTLS library.

//** Defines and Includes

#include "Tpm.h"

#if defined(HASH_LIB_MBEDTLS) || defined(MATH_LIB_MBEDTLS) || defined(SYM_LIB_MBEDTLS)

//*** SupportLibInit()
// This does any initialization required by the support library.
LIB_EXPORT int
SupportLibInit(
    void
    )
{
    return TRUE;
}

#endif // HASH_LIB_MBEDTLS || MATH_LIB_MBEDTLS || SYM_LIB_MBEDTLS
