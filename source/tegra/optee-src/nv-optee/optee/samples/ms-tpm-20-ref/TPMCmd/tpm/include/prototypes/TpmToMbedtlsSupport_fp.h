/*
 * Copyright (c) 2023, NVIDIA Corporation & AFFILIATES. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef    _TPM_TO_MBEDTLS_SUPPORT_FP_H_
#define    _TPM_TO_MBEDTLS_SUPPORT_FP_H_

#if defined(HASH_LIB_MBEDTLS) || defined(MATH_LIB_MBEDTLS) || defined(SYM_LIB_MBEDTLS)

//*** SupportLibInit()
// This does any initialization required by the support library.
LIB_EXPORT int
SupportLibInit(
    void
);

#endif // HASH_LIB_MBEDTLS || MATH_LIB_MBEDTLS || SYM_LIB_MBEDTLS

#endif  // _TPM_TO_MBEDTLS_SUPPORT_FP_H_
