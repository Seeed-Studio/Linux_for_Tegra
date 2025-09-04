/*
 * Copyright (c) 2023, NVIDIA Corporation & AFFILIATES. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef    _TPM_TO_MBEDTLS_MATH_FP_H_
#define    _TPM_TO_MBEDTLS_MATH_FP_H_

#ifdef MATH_LIB_MBEDTLS

//*** BnFromMbedtls()
// This function converts a mbedtls_mpi to a TPM bignum. In this implementation
// it is assumed that Mbedtls used the same format for a big number as does the
// TPM -- an array of native-endian words in little-endian order.
void
BnFromMbedtls(
    bigNum              bn,
    mbedtls_mpi         *mbedBn
);

//*** BnToMbedtls()
// This function converts a TPM bignum to a mbedtls_mpi, and has the same
// assumptions as made by BnFromMbedtls()
void
BnToMbedtls(
    mbedtls_mpi         *toInit,
    bigConst            initializer
);

//*** MpiInitialize()
// This function initializes an Mbedtls mbedtls_mpi.
mbedtls_mpi *
MpiInitialize(
    mbedtls_mpi         *toInit
);

#if LIBRARY_COMPATIBILITY_CHECK
//** MathLibraryCompatibilityCheck()
// This function is only used during development to make sure that the library
// that is being referenced is using the same size of data structures as the TPM.
BOOL
MathLibraryCompatibilityCheck(
    void
);
#endif

//*** BnModMult()
// Does multiply and divide returning the remainder of the divide.
LIB_EXPORT BOOL
BnModMult(
    bigNum              result,
    bigConst            op1,
    bigConst            op2,
    bigConst            modulus
);

//*** BnMult()
// Multiplies two numbers
LIB_EXPORT BOOL
BnMult(
    bigNum              result,
    bigConst            multiplicand,
    bigConst            multiplier
);

//*** BnDiv()
// This function divides two bigNum values. The function returns FALSE if
// there is an error in the operation.
LIB_EXPORT BOOL
BnDiv(
    bigNum              quotient,
    bigNum              remainder,
    bigConst            dividend,
    bigConst            divisor
);

#if ALG_RSA
//*** BnGcd()
// Get the greatest common divisor of two numbers
LIB_EXPORT BOOL
BnGcd(
    bigNum              gcd,            // OUT: the common divisor
    bigConst            number1,        // IN:
    bigConst            number2         // IN:
);

//***BnModExp()
// Do modular exponentiation using bigNum values. The conversion from a mp_int to
// a bigNum is trivial as they are based on the same structure
LIB_EXPORT BOOL
BnModExp(
    bigNum              result,         // OUT: the result
    bigConst            number,         // IN: number to exponentiate
    bigConst            exponent,       // IN:
    bigConst            modulus         // IN:
);

//*** BnModInverse()
// Modular multiplicative inverse
LIB_EXPORT BOOL
BnModInverse(
    bigNum              result,
    bigConst            number,
    bigConst            modulus
);
#endif // TPM_ALG_RSA

#if ALG_ECC
//*** PointFromMbedtls()
// Function to copy the point result from a mbedtls_mpi to a bigNum
void
PointFromMbedtls(
    bigPoint            pOut,      // OUT: resulting point
    ecc_point           *pIn       // IN: the point to return
);

//*** PointToMbedtls()
// Function to copy the point result from a bigNum to a mbedtls_mpi
void
PointToMbedtls(
    ecc_point           *pOut,      // OUT: resulting point
    pointConst          pIn       // IN: the point to return
);

//*** BnEccModMult()
// This function does a point multiply of the form R = [d]S
// return type: BOOL
//  FALSE       failure in operation; treat as result being point at infinity
LIB_EXPORT BOOL
BnEccModMult(
    bigPoint            R,         // OUT: computed point
    pointConst          S,         // IN: point to multiply by 'd' (optional)
    bigConst            d,         // IN: scalar for [d]S
    bigCurve            E
);

//*** BnEccModMult2()
// This function does a point multiply of the form R = [d]G + [u]Q
// return type: BOOL
//  FALSE       failure in operation; treat as result being point at infinity
LIB_EXPORT BOOL
BnEccModMult2(
    bigPoint            R,         // OUT: computed point
    pointConst          S,         // IN: optional point
    bigConst            d,         // IN: scalar for [d]S or [d]G
    pointConst          Q,         // IN: second point
    bigConst            u,         // IN: second scalar
    bigCurve            E          // IN: curve
);

//** BnEccAdd()
// This function does addition of two points.
// return type: BOOL
//  FALSE       failure in operation; treat as result being point at infinity
LIB_EXPORT BOOL
BnEccAdd(
    bigPoint            R,         // OUT: computed point
    pointConst          S,         // IN: point to multiply by 'd'
    pointConst          Q,         // IN: second point
    bigCurve            E          // IN: curve
);
#endif // TPM_ALG_ECC
#endif // MATH_LIB_MBEDTLS

#endif  // _TPM_TO_MBEDTLS_MATH_FP_H_
