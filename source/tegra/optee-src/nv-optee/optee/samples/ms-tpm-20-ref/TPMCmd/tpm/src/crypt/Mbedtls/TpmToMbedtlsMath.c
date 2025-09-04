/*
 * Copyright (c) 2023, NVIDIA Corporation & AFFILIATES. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

//** Introduction
// The functions in this file provide the low-level interface between the TPM code
// and the big number and elliptic curve math routines in MbedTLS.

//** Includes and Defines
#include "Tpm.h"

#ifdef MATH_LIB_MBEDTLS
#include "TpmToMbedtlsMath_fp.h"

#define DIV_ROUND_UP(x,y)   ((x + y - 1)/y)

//*** BnFromMbedtls()
// This function converts a mbedtls_mpi to a TPM bignum. In this implementation
// it is assumed that Mbedtls used the same format for a big number as does the
// TPM -- an array of native-endian words in little-endian order.
void
BnFromMbedtls(
    bigNum              bn,
    mbedtls_mpi         *mbedBn
    )
{
    if(bn != NULL)
    {
        mbedtls_mpi_write_binary_le(mbedBn, (unsigned char*)bn->d,
                                    BnGetAllocated(bn) * sizeof(crypt_uword_t));

        BnSetTop(bn, DIV_ROUND_UP(mbedtls_mpi_size(mbedBn), sizeof(crypt_uword_t)));
    }
}

//*** BnToMbedtls()
// This function converts a TPM bignum to a mbedtls_mpi, and has the same
// assumptions as made by BnFromMbedtls().
void
BnToMbedtls(
    mbedtls_mpi         *toInit,
    bigConst            initializer
    )
{
    if ((toInit != NULL) && (initializer != NULL))
        mbedtls_mpi_read_binary_le(toInit, (unsigned char*)initializer->d,
                                   initializer->size * sizeof(crypt_uword_t));
}

//*** MpiInitialize()
// This function initializes an MbedTls mbedtls_mpi.
mbedtls_mpi *
MpiInitialize(
    mbedtls_mpi         *toInit
    )
{
    mbedtls_mpi_init(toInit);
    return toInit;
}

#if LIBRARY_COMPATIBILITY_CHECK
//** MathLibraryCompatibilityCheck()
// This function is only used during development to make sure that the library
// that is being referenced is using the same size of data structures as the TPM.
BOOL
MathLibraryCompatibilityCheck(
    void
    )
{
    BN_VAR(tpmTemp, 64 * 8); // allocate some space for a test value
    crypt_uword_t       i;
    TPM2B_TYPE(TEST, 32);
    TPM2B_TEST          test = {{32, {0x1F, 0x1E, 0x1D, 0x1C, 0x1B, 0x1A, 0x19, 0x18,
                                      0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10,
                                      0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08,
                                      0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00}}};
    // Convert the test TPM2B to a bigNum
    BnFrom2B(tpmTemp, &test.b);
    MPI_INITIALIZED(mbedTemp, tpmTemp);
    //(mbedTemp); // compiler warning
    // Make sure the values are consistent
    VERIFY(mbedTemp->n * sizeof(mbedtls_mpi_uint) == (int)tpmTemp->size * sizeof(crypt_uword_t));
    for(i = 0; i < tpmTemp->size; i++)
        VERIFY(((crypt_uword_t*)mbedTemp->p)[i] == tpmTemp->d[i]);

    MPI_FREE(mbedTemp);
    return TRUE;
Error:
    MPI_FREE(mbedTemp);
    return FALSE;
}
#endif

//*** BnModMult()
// Does multiply and divide returning the remainder of the divide.
LIB_EXPORT BOOL
BnModMult(
    bigNum              result,
    bigConst            op1,
    bigConst            op2,
    bigConst            modulus
    )
{
    BOOL                OK;
    MPI_INITIALIZED(bnOp1, op1);
    MPI_INITIALIZED(bnOp2, op2);
    MPI_INITIALIZED(bnTemp, NULL);
    BN_VAR(temp, LARGEST_NUMBER_BITS * 2);

    pAssert(BnGetAllocated(result) >= BnGetSize(modulus));

    OK = (mbedtls_mpi_mul_mpi(bnTemp, bnOp1, bnOp2) == 0);
    if(OK)
    {
        BnFromMbedtls(temp, bnTemp);
        OK = BnDiv(NULL, result, temp, modulus);
    }

    MPI_FREE(bnOp1);
    MPI_FREE(bnOp2);
    MPI_FREE(bnTemp);

    return OK;
}

//*** BnMult()
// Multiplies two numbers.
LIB_EXPORT BOOL
BnMult(
    bigNum              result,
    bigConst            multiplicand,
    bigConst            multiplier
    )
{
    BOOL                OK;
    MPI_INITIALIZED(bnTemp, NULL);
    MPI_INITIALIZED(bnA, multiplicand);
    MPI_INITIALIZED(bnB, multiplier);

    pAssert(result->allocated >=
            (BITS_TO_CRYPT_WORDS(BnSizeInBits(multiplicand)
                                 + BnSizeInBits(multiplier))));

    OK = (mbedtls_mpi_mul_mpi(bnTemp, bnA, bnB) == 0);
    if(OK)
    {
        BnFromMbedtls(result, bnTemp);
    }

    MPI_FREE(bnTemp);
    MPI_FREE(bnA);
    MPI_FREE(bnB);

    return OK;
}

//*** BnDiv()
// This function divides two bigNum values. The function returns FALSE if
// there is an error in the operation.
LIB_EXPORT BOOL
BnDiv(
    bigNum              quotient,
    bigNum              remainder,
    bigConst            dividend,
    bigConst            divisor
    )
{
    BOOL        OK;
    MPI_INITIALIZED(bnQ, quotient);
    MPI_INITIALIZED(bnR, remainder);
    MPI_INITIALIZED(bnDend, dividend);
    MPI_INITIALIZED(bnSor, divisor);

    pAssert(!BnEqualZero(divisor));

    if(BnGetSize(dividend) < BnGetSize(divisor))
    {
        if(quotient)
            BnSetWord(quotient, 0);
        if(remainder)
            BnCopy(remainder, dividend);
        OK = TRUE;
    }
    else
    {
        pAssert((quotient == NULL)
                || (quotient->allocated >= (unsigned)(dividend->size
                                                      - divisor->size)));
        pAssert((remainder == NULL)
                || (remainder->allocated >= divisor->size));

        OK = (mbedtls_mpi_div_mpi(bnQ, bnR, bnDend, bnSor) == 0);
        if(OK)
        {
            BnFromMbedtls(quotient, bnQ);
            BnFromMbedtls(remainder, bnR);
        }
    }

    MPI_FREE(bnQ);
    MPI_FREE(bnR);
    MPI_FREE(bnDend);
    MPI_FREE(bnSor);

    return OK;
}

#if ALG_RSA
//*** BnGcd()
// Get the greatest common divisor of two numbers.
LIB_EXPORT BOOL
BnGcd(
    bigNum              gcd,            // OUT: the common divisor
    bigConst            number1,        // IN:
    bigConst            number2         // IN:
    )
{
    BOOL                OK;
    MPI_INITIALIZED(bnGcd, gcd);
    MPI_INITIALIZED(bn1, number1);
    MPI_INITIALIZED(bn2, number2);

    pAssert(gcd != NULL);

    OK = (mbedtls_mpi_gcd(bnGcd, bn1, bn2) == 0);
    if(OK)
    {
        BnFromMbedtls(gcd, bnGcd);
    }

    MPI_FREE(bnGcd);
    MPI_FREE(bn1);
    MPI_FREE(bn2);

    return OK;
}

//***BnModExp()
// Do modular exponentiation using bigNum values.
LIB_EXPORT BOOL
BnModExp(
    bigNum              result,         // OUT: the result
    bigConst            number,         // IN: number to exponentiate
    bigConst            exponent,       // IN:
    bigConst            modulus         // IN:
    )
{
    BOOL                OK;
    MPI_INITIALIZED(bnResult, result);
    MPI_INITIALIZED(bnN, number);
    MPI_INITIALIZED(bnE, exponent);
    MPI_INITIALIZED(bnM, modulus);

    OK = (mbedtls_mpi_exp_mod(bnResult, bnN, bnE, bnM, NULL) == 0);
    if(OK)
    {
        BnFromMbedtls(result, bnResult);
    }

    MPI_FREE(bnResult);
    MPI_FREE(bnN);
    MPI_FREE(bnE);
    MPI_FREE(bnM);

    return OK;
}

//*** BnModInverse()
// Modular multiplicative inverse
LIB_EXPORT BOOL
BnModInverse(
    bigNum              result,
    bigConst            number,
    bigConst            modulus
    )
{
    BOOL                OK;
    MPI_INITIALIZED(bnResult, result);
    MPI_INITIALIZED(bnN, number);
    MPI_INITIALIZED(bnM, modulus);

    OK = (mbedtls_mpi_inv_mod(bnResult, bnN, bnM) == 0);
    if(OK)
    {
        BnFromMbedtls(result, bnResult);
    }

    MPI_FREE(bnResult);
    MPI_FREE(bnN);
    MPI_FREE(bnM);

    return OK;
}
#endif // TPM_ALG_RSA

#if ALG_ECC
//*** PointFromMbedtls()
// Function to copy the point result from a mbedtls_mpi to a bigNum
void
PointFromMbedtls(
    bigPoint            pOut,      // OUT: resulting point
    ecc_point           *pIn       // IN: the point to return
    )
{
    BnFromMbedtls(pOut->x, pIn->x);
    BnFromMbedtls(pOut->y, pIn->y);
    BnFromMbedtls(pOut->z, pIn->z);
}

//*** PointToMbedtls()
// Function to copy the point result from a bigNum to a mbedtls_mpi
void
PointToMbedtls(
    ecc_point           *pOut,      // OUT: resulting point
    pointConst          pIn         // IN: the point to return
    )
{
    BnToMbedtls(pOut->x, pIn->x);
    BnToMbedtls(pOut->y, pIn->y);
    BnToMbedtls(pOut->z, pIn->z);
}

//*** EcPointInitialized()
// Allocate and initialize a point.
static ecc_point *
EcPointInitialized(
    pointConst          initializer
    )
{
    ecc_point           *P;

    P = ltc_ecc_new_point();
    pAssert(P != NULL);

    if (P != NULL && initializer != NULL)
    {
        PointToMbedtls(P, initializer);
    }

    return P;
}

//*** EcPointFree()
// Free a point.
static void
EcPointFree(
    ecc_point           *pEcc
    )
{
    ltc_ecc_del_point(pEcc);
}

//*** BnEccModMult()
// This function does a point multiply of the form R = [d]S
// return type: BOOL
//  FALSE       failure in operation; treat as result being point at infinity
LIB_EXPORT BOOL
BnEccModMult(
    bigPoint            R,         // OUT: computed point
    pointConst          S,         // IN: point to multiply by 'd'
    bigConst            d,         // IN: scalar for [d]S
    bigCurve            E
    )
{
    BOOL                OK;
    MPI_INITIALIZED(bnD, d);
    MPI_INITIALIZED(bnPrime, CurveGetPrime(E));
    MPI_INITIALIZED(bnA, CurveGet_a(E));
    POINT_CREATE(pS, NULL);
    POINT_CREATE(pR, NULL);

    if(S == NULL)
        S = CurveGetG(AccessCurveData(E));

    PointToMbedtls(pS, S);

    OK = (ltc_ecc_mulmod(bnD, pS, pR, bnA, bnPrime, 1) == CRYPT_OK);
    if(OK)
    {
        PointFromMbedtls(R, pR);
    }

    POINT_DELETE(pR);
    POINT_DELETE(pS);
    MPI_FREE(bnD);
    MPI_FREE(bnPrime);
    MPI_FREE(bnA);

    return !BnEqualZero(R->z);
}

//*** BnEccModMult2()
// This function does a point multiply of the form R = [d]S + [u]Q
// return type: BOOL
//  FALSE       failure in operation; treat as result being point at infinity
LIB_EXPORT BOOL
BnEccModMult2(
    bigPoint            R,         // OUT: computed point
    pointConst          S,         // IN: first point (optional)
    bigConst            d,         // IN: scalar for [d]S or [d]G
    pointConst          Q,         // IN: second point
    bigConst            u,         // IN: second scalar
    bigCurve            E          // IN: curve
    )
{
    BOOL                OK;
    POINT_CREATE(pR, NULL);
    POINT_CREATE(pS, NULL);
    POINT_CREATE(pQ, Q);
    MPI_INITIALIZED(bnD, d);
    MPI_INITIALIZED(bnU, u);
    MPI_INITIALIZED(bnPrime, CurveGetPrime(E));

    if(S == NULL)
        S = CurveGetG(AccessCurveData(E));

    PointToMbedtls(pS, S);

    OK = (ltc_ecc_mul2add(pS, bnD, pQ, bnU, pR, NULL, bnPrime) == CRYPT_OK);
    if(OK)
    {
        PointFromMbedtls(R, pR);
    }

    POINT_DELETE(pR);
    POINT_DELETE(pS);
    POINT_DELETE(pQ);
    MPI_FREE(bnD);
    MPI_FREE(bnU);
    MPI_FREE(bnPrime);

    return !BnEqualZero(R->z);
}

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
    )
{
    BOOL                OK;
    void                *mp;
    POINT_CREATE(pR, NULL);
    POINT_CREATE(pS, S);
    POINT_CREATE(pQ, Q);
    MPI_INITIALIZED(bnA, CurveGet_a(E));
    MPI_INITIALIZED(bnMod, CurveGetPrime(E));

    OK = (mp_montgomery_setup(bnMod, &mp) == CRYPT_OK);
    OK = OK && (ltc_ecc_projective_add_point(pS, pQ, pR, bnA, bnMod, mp) == CRYPT_OK);
    if(OK)
    {
        PointFromMbedtls(R, pR);
    }

    POINT_DELETE(pR);
    POINT_DELETE(pS);
    POINT_DELETE(pQ);
    MPI_FREE(bnA);
    MPI_FREE(bnMod);
    mp_montgomery_free(mp);

    return !BnEqualZero(R->z);
}

#endif // TPM_ALG_ECC

#endif // MATH_LIB_MBEDTLS
