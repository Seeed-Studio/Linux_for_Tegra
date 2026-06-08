/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_MATH_UTILS_H
#define PVA_MATH_UTILS_H
#if !defined(__KERNEL__)
#include <stdbool.h>
#endif
#include "pva_plat_faults.h"
typedef enum {
	MATH_OP_SUCCESS,
	MATH_OP_ERROR,
} pva_math_error;

#define MAX_UINT32 4294967295u /*(0xFFFFFFFFu)*/
#define MAX_UINT16 65535u
#define MAX_UINT8 255u
#define MAX_INT32 0x7FFFFFFF
#define MIN_INT32 (-0x7FFFFFFF - 1LL)
#define MAX_INT64 0x7FFFFFFFFFFFFFFF
#define MIN_INT64 (-0x7FFFFFFFFFFFFFFF - 1LL)
#define UINT64_MAX_SHIFT_BITS 0x3FU

#define safe_pow2_roundup_u64(val, align)                                      \
	pva_safe_roundup_u64((val), (align), __FILE__, __LINE__)

#define safe_pow2_roundup_u32(val, align)                                      \
	pva_safe_roundup_u32((val), (align), __FILE__, __LINE__)

#define safe_pow2_roundup_u16(val, align)                                      \
	pva_safe_roundup_u16((val), (align), __FILE__, __LINE__)

#define safe_pow2_roundup_u8(val, align)                                       \
	pva_safe_roundup_u8((val), (align), __FILE__, __LINE__)

/**
* @brief Rounds up a uint64_t value to the nearest multiple of a power-of-two align.
*
* @param val   The value to round up.
* @param align The alignment factor (must be a power of two).
* @param file  The source file (for assertion messages).
* @param line  The line number (for assertion messages).
* @return uint64_t The rounded-up value.
*/
static inline uint64_t pva_safe_roundup_u64(uint64_t val, uint64_t align,
					    const char *file, uint32_t line)
{
	uint64_t rounded;
	// Ensure align is a power of two and at least 1
	ASSERT_WITH_LOC((align != 0U) && ((align & (align - 1U)) == 0U), file,
			line);

	// Check for addition overflow
	ASSERT_WITH_LOC(val <= UINT64_MAX - (align - 1U), file, line);

	rounded = (val + (align - 1U)) & ~(align - 1U);

	return rounded;
}

/**
* @brief Rounds up a uint32_t value to the nearest multiple of a power-of-two align.
*
* @param val   The value to round up.
* @param align The alignment factor (must be a power of two).
* @param file  The source file (for assertion messages).
* @param line  The line number (for assertion messages).
* @return uint32_t The rounded-up value.
*/
static inline uint32_t pva_safe_roundup_u32(uint32_t val, uint32_t align,
					    const char *file, uint32_t line)
{
	uint64_t temp;
	uint32_t rounded;
	// Ensure align is a power of two and at least 1
	ASSERT_WITH_LOC((align != 0U) && ((align & (align - 1U)) == 0U), file,
			line);

	temp = (uint64_t)val + ((uint64_t)align - (uint64_t)1U);
	ASSERT_WITH_LOC(temp <= MAX_UINT32, file, line);
	rounded = (uint32_t)temp;
	rounded = (uint32_t)(rounded & ~(align - 1U));

	return rounded;
}

/**
* @brief Rounds up a uint16_t value to the nearest multiple of a power-of-two align.
*
* @param val   The value to round up.
* @param align The alignment factor (must be a power of two).
* @param file  The source file (for assertion messages).
* @param line  The line number (for assertion messages).
* @return uint16_t The rounded-up value.
*/
static inline uint16_t pva_safe_roundup_u16(uint16_t val, uint16_t align,
					    const char *file, uint32_t line)
{
	uint32_t temp;
	uint16_t rounded;
	// Ensure align is a power of two and at least 1
	ASSERT_WITH_LOC((align != 0U) && ((align & (align - 1U)) == 0U), file,
			line);

	temp = (uint32_t)val + ((uint32_t)align - (uint32_t)1U);
	//Overflow check
	ASSERT_WITH_LOC(temp <= MAX_UINT16, file, line);
	rounded = (uint16_t)temp;
	rounded = (uint16_t)(rounded & ~(align - 1U));

	return rounded;
}

/**
* @brief Rounds up a uint8_t value to the nearest multiple of a power-of-two align.
*
* @param val   The value to round up.
* @param align The alignment factor (must be a power of two).
* @param file  The source file (for assertion messages).
* @param line  The line number (for assertion messages).
* @return uint8_t The rounded-up value.
*/
static inline uint8_t pva_safe_roundup_u8(uint8_t val, uint8_t align,
					  const char *file, uint32_t line)
{
	uint32_t temp;
	uint8_t rounded;
	// Ensure align is a power of two and at least 1
	ASSERT_WITH_LOC((align != 0U) && ((align & (align - 1U)) == 0U), file,
			line);

	temp = (uint32_t)val + ((uint32_t)align - (uint32_t)1U);
	ASSERT_WITH_LOC(temp <= MAX_UINT8, file, line);
	rounded = (uint8_t)temp;
	rounded = (uint8_t)(rounded & ~(align - 1U));

	return rounded;
}

static inline uint32_t safe_abs_int32(int32_t value, const char *file,
				      uint32_t line)
{
	ASSERT_WITH_LOC((value != MIN_INT32), file, line);
	return (uint32_t)((value < 0) ? -value : value);
}

static inline uint64_t safe_abs_int64(int64_t value, const char *file,
				      uint32_t line)
{
	ASSERT_WITH_LOC((value != MIN_INT64), file, line);
	return (uint64_t)((value < 0) ? -value : value);
}

#define safe_absint32(value) safe_abs_int32((value), __FILE__, __LINE__)

#define safe_absint64(value) safe_abs_int64((value), __FILE__, __LINE__)

static inline uint64_t safe_add_u64(uint64_t addend1, uint64_t addend2,
				    const char *file, uint32_t line)
{
	uint64_t sum;

	sum = addend1 + addend2;
	ASSERT_WITH_LOC(sum >= addend1, file, line);
	return sum;
}

static inline uint32_t safe_add_u32(uint32_t addend1, uint32_t addend2,
				    const char *file, uint32_t line)
{
	uint32_t sum;

	sum = addend1 + addend2;
	ASSERT_WITH_LOC(sum >= addend1, file, line);
	return sum;
}

static inline uint16_t safe_add_u16(uint16_t addend1, uint16_t addend2,
				    const char *file, uint32_t line)
{
	uint32_t sum;

	sum = (uint32_t)addend1 + (uint32_t)addend2;
	ASSERT_WITH_LOC(sum <= (uint32_t)MAX_UINT16, file, line);
	return (uint16_t)sum;
}

static inline uint8_t safe_add_u8(uint8_t addend1, uint8_t addend2,
				  const char *file, uint32_t line)
{
	uint32_t sum;

	sum = (uint32_t)addend1 + (uint32_t)addend2;
	ASSERT_WITH_LOC(sum <= (uint32_t)MAX_UINT8, file, line);
	return (uint8_t)sum;
}

static inline uint32_t align8_u32(uint32_t val, pva_math_error *err)
{
	if (val > (UINT32_MAX - 7u)) {
		*err = MATH_OP_ERROR;
		return 0u;
	}
	return (val + 7u) & ~(uint32_t)7u;
}

#define safe_addu64(addend1, addend2)                                          \
	safe_add_u64((addend1), (addend2), __FILE__, __LINE__)

#define safe_addu32(addend1, addend2)                                          \
	safe_add_u32((addend1), (addend2), __FILE__, __LINE__)

#define safe_addu16(addend1, addend2)                                          \
	safe_add_u16((addend1), (addend2), __FILE__, __LINE__)

#define safe_addu8(addend1, addend2)                                           \
	safe_add_u8((addend1), (addend2), __FILE__, __LINE__)

static inline uint64_t safe_sub_u64(uint64_t minuend, uint64_t subtrahend,
				    const char *file, uint32_t line)
{
	uint64_t difference;

	ASSERT_WITH_LOC((minuend) >= (subtrahend), file, line);
	difference = ((minuend) - (subtrahend));
	return difference;
}

static inline uint32_t safe_sub_u32(uint32_t minuend, uint32_t subtrahend,
				    const char *file, uint32_t line)
{
	uint32_t difference;

	ASSERT_WITH_LOC((minuend) >= (subtrahend), file, line);
	difference = ((minuend) - (subtrahend));
	return difference;
}

static inline uint16_t safe_sub_u16(uint16_t minuend, uint16_t subtrahend,
				    const char *file, uint32_t line)
{
	uint16_t difference;

	ASSERT_WITH_LOC((minuend) >= (subtrahend), file, line);
	difference = ((minuend) - (subtrahend));
	return difference;
}

static inline uint8_t safe_sub_u8(uint8_t minuend, uint8_t subtrahend,
				  const char *file, uint32_t line)
{
	uint8_t difference;

	ASSERT_WITH_LOC((minuend) >= (subtrahend), file, line);
	difference = ((minuend) - (subtrahend));
	return difference;
}

#define safe_subu64(minuend, subtrahend)                                       \
	safe_sub_u64((minuend), (subtrahend), __FILE__, __LINE__)

#define safe_subu32(minuend, subtrahend)                                       \
	safe_sub_u32((minuend), (subtrahend), __FILE__, __LINE__)

#define safe_subu16(minuend, subtrahend)                                       \
	safe_sub_u16((minuend), (subtrahend), __FILE__, __LINE__)

#define safe_subu8(minuend, subtrahend)                                        \
	safe_sub_u8((minuend), (subtrahend), __FILE__, __LINE__)

static inline uint64_t safe_mul_u64(uint64_t operand1, uint64_t operand2,
				    const char *file, uint32_t line)
{
	uint64_t product;
	if ((operand1 == 0u) || (operand2 == 0u)) {
		product = 0u;
	} else {
		ASSERT_WITH_LOC((operand1) <= (UINT64_MAX / operand2), file,
				line);
		product = (operand1 * operand2);
	}

	return product;
}

static inline uint32_t safe_mul_u32(uint32_t operand1, uint32_t operand2,
				    const char *file, uint32_t line)
{
	uint64_t product;

	product = ((uint64_t)operand1 * (uint64_t)operand2);
	ASSERT_WITH_LOC(product <= UINT32_MAX, file, line);
	return (uint32_t)(product);
}

static inline uint16_t safe_mul_u16(uint16_t operand1, uint16_t operand2,
				    const char *file, uint32_t line)
{
	uint32_t product;

	product = ((uint32_t)operand1 * (uint32_t)operand2);
	ASSERT_WITH_LOC(product <= MAX_UINT16, file, line);
	return (uint16_t)(product);
}

static inline uint8_t safe_mul_u8(uint8_t operand1, uint8_t operand2,
				  const char *file, uint32_t line)
{
	uint32_t product;

	product = ((uint32_t)operand1 * (uint32_t)operand2);
	ASSERT_WITH_LOC(product <= MAX_UINT8, file, line);
	return (uint8_t)(product);
}

#define safe_mulu64(operand1, operand2)                                        \
	safe_mul_u64((operand1), (operand2), __FILE__, __LINE__)

#define safe_mulu32(operand1, operand2)                                        \
	safe_mul_u32((operand1), (operand2), __FILE__, __LINE__)

#define safe_mulu16(operand1, operand2)                                        \
	safe_mul_u16((operand1), (operand2), __FILE__, __LINE__)

#define safe_mulu8(operand1, operand2)                                         \
	safe_mul_u8((operand1), (operand2), __FILE__, __LINE__)

static inline int64_t safe_get_signed_s64(uint64_t value, const char *file,
					  uint32_t line)
{
	ASSERT_WITH_LOC((value <= (uint64_t)MAX_INT64), file, line);
	return (int64_t)value;
}

static inline int32_t safe_get_signed_s32(uint32_t value, const char *file,
					  uint32_t line)
{
	ASSERT_WITH_LOC((value <= (uint32_t)MAX_INT32), file, line);
	return (int32_t)value;
}
#define convert_to_signed_s64(value)                                           \
	safe_get_signed_s64((value), __FILE__, __LINE__)

#define convert_to_signed_s32(value)                                           \
	safe_get_signed_s32((value), __FILE__, __LINE__)

static inline uint8_t safe_cast_u32_to_u8(uint32_t value, const char *file,
					  uint32_t line)
{
	ASSERT_WITH_LOC(value <= MAX_UINT8, file, line);
	return (uint8_t)value;
}

static inline uint16_t safe_cast_u32_to_u16(uint32_t value, const char *file,
					    uint32_t line)
{
	ASSERT_WITH_LOC(value <= MAX_UINT16, file, line);
	return (uint16_t)value;
}

static inline uint8_t safe_cast_u16_to_u8(uint16_t value, const char *file,
					  uint32_t line)
{
	ASSERT_WITH_LOC(value <= MAX_UINT8, file, line);
	return (uint8_t)value;
}

static inline uint32_t safe_cast_u64_to_u32(uint64_t value, const char *file,
					    uint32_t line)
{
	ASSERT_WITH_LOC(value <= MAX_UINT32, file, line);
	return (uint32_t)value;
}

#define safe_u32_to_u8(value) safe_cast_u32_to_u8((value), __FILE__, __LINE__)
#define safe_u32_to_u16(value) safe_cast_u32_to_u16((value), __FILE__, __LINE__)
#define safe_u16_to_u8(value) safe_cast_u16_to_u8((value), __FILE__, __LINE__)
#define safe_u64_to_u32(value) safe_cast_u64_to_u32((value), __FILE__, __LINE__)

static inline uint64_t addu64(uint64_t addend1, uint64_t addend2,
			      pva_math_error *math_flag)
{
	uint64_t sum;

	sum = addend1 + addend2;
	if (sum < addend1) {
		*math_flag = MATH_OP_ERROR;
		sum = 0u;
	}

	return sum;
}

static inline uint32_t addu32(uint32_t addend1, uint32_t addend2,
			      pva_math_error *math_flag)
{
	uint32_t sum;

	sum = addend1 + addend2;
	if (sum < addend1) {
		*math_flag = MATH_OP_ERROR;
		sum = 0u;
	}

	return sum;
}

static inline uint16_t addu16(uint16_t addend1, uint16_t addend2,
			      pva_math_error *math_flag)
{
	uint32_t sum;

	sum = (uint32_t)addend1 + (uint32_t)addend2;
	if (sum > MAX_UINT16) {
		*math_flag = MATH_OP_ERROR;
		sum = 0u;
	}

	return (uint16_t)sum;
}

static inline uint8_t addu8(uint8_t addend1, uint8_t addend2,
			    pva_math_error *math_flag)
{
	uint32_t sum;

	sum = (uint32_t)addend1 + (uint32_t)addend2;
	if (sum > MAX_UINT8) {
		*math_flag = MATH_OP_ERROR;
		sum = 0u;
	}

	return (uint8_t)sum;
}

static inline int64_t adds64(int64_t addend1, int64_t addend2,
			     pva_math_error *math_flag)
{
	int64_t sum = addend1 + addend2;

	/* Check for overflow when both numbers are positive */
	if (((sum < 0) && ((addend2 > 0) && (addend1 > 0))) ||
	    ((sum > 0) && ((addend2 < 0) && (addend1 < 0)))) {
		*math_flag = MATH_OP_ERROR;
		sum = 0;
	}
	return sum;
}

static inline uint64_t subu64(uint64_t minuend, uint64_t subtrahend,
			      pva_math_error *math_flag)
{
	uint64_t difference;

	if (minuend < subtrahend) {
		*math_flag = MATH_OP_ERROR;
		difference = 0u;
	} else {
		difference = ((minuend) - (subtrahend));
	}

	return difference;
}

static inline uint32_t subu32(uint32_t minuend, uint32_t subtrahend,
			      pva_math_error *math_flag)
{
	uint32_t difference;

	if (minuend < subtrahend) {
		*math_flag = MATH_OP_ERROR;
		difference = 0u;
	} else {
		difference = ((minuend) - (subtrahend));
	}

	return difference;
}

static inline uint16_t subu16(uint16_t minuend, uint16_t subtrahend,
			      pva_math_error *math_flag)
{
	uint16_t difference;

	if (minuend < subtrahend) {
		*math_flag = MATH_OP_ERROR;
		difference = 0u;
	} else {
		difference = ((minuend) - (subtrahend));
	}

	return difference;
}

static inline uint8_t subu8(uint8_t minuend, uint8_t subtrahend,
			    pva_math_error *math_flag)
{
	uint8_t difference;

	if (minuend < subtrahend) {
		*math_flag = MATH_OP_ERROR;
		difference = 0u;
	} else {
		difference = ((minuend) - (subtrahend));
	}

	return difference;
}

static inline int64_t subs64(int64_t minuend, int64_t subtrahend,
			     pva_math_error *math_flag)
{
	int64_t difference;

	/* Check for overflow/underflow */
	if (subtrahend > 0) {
		/* Subtracting a positive number - check for underflow */
		if (minuend < (MIN_INT64 + subtrahend)) {
			*math_flag = MATH_OP_ERROR;
			return 0;
		}
	} else {
		/* Subtracting a negative number - check for overflow */
		if (minuend > (MAX_INT64 + subtrahend)) {
			*math_flag = MATH_OP_ERROR;
			return 0;
		}
	}

	difference = minuend - subtrahend;
	return difference;
}

static inline int32_t subs32(int32_t minuend, int32_t subtrahend,
			     pva_math_error *math_flag)
{
	int64_t difference;

	difference = (int64_t)minuend - (int64_t)subtrahend;
	if ((difference > MAX_INT32) || (difference < MIN_INT32)) {
		*math_flag = MATH_OP_ERROR;
		return 0;
	}

	return (int32_t)difference;
}

static inline uint64_t mulu64(uint64_t operand1, uint64_t operand2,
			      pva_math_error *math_flag)
{
	uint64_t product;

	if ((operand1 == 0u) || (operand2 == 0u)) {
		product = 0u;
	} else {
		if ((operand1) > (UINT64_MAX / operand2)) {
			*math_flag = MATH_OP_ERROR;
			product = 0u;
		} else {
			product = (operand1 * operand2);
		}
	}

	return product;
}

static inline uint32_t mulu32(uint32_t operand1, uint32_t operand2,
			      pva_math_error *math_flag)
{
	uint64_t product;

	product = ((uint64_t)operand1 * (uint64_t)operand2);
	if (product > UINT32_MAX) {
		*math_flag = MATH_OP_ERROR;
		product = 0u;
	}

	return (uint32_t)(product);
}

static inline uint16_t mulu16(uint16_t operand1, uint16_t operand2,
			      pva_math_error *math_flag)
{
	uint32_t product;

	product = ((uint32_t)operand1 * (uint32_t)operand2);
	if (product > MAX_UINT16) {
		*math_flag = MATH_OP_ERROR;
		product = 0u;
	}

	return (uint16_t)(product);
}

static inline uint8_t mulu8(uint8_t operand1, uint8_t operand2,
			    pva_math_error *math_flag)
{
	uint32_t product;

	product = ((uint32_t)operand1 * (uint32_t)operand2);
	if (product > MAX_UINT8) {
		*math_flag = MATH_OP_ERROR;
		product = 0u;
	}

	return (uint8_t)(product);
}

/* Checks if int64_t multiplication would overflow/underflow based on operand signs */
static inline bool perform_overflow_check(int64_t operand1, int64_t operand2,
					  bool both_positive,
					  bool both_negative)
{
	bool result = false;

	/* Handle special cases first - multiplication by 0 never overflows */
	if ((operand1 == 0) || (operand2 == 0)) {
		result = false;
		goto out;
	}

	/* Special case: MIN_INT64 * -1 would overflow (or -1 * MIN_INT64) */
	if (((operand1 == MIN_INT64) && (operand2 == -1)) ||
	    ((operand1 == -1) && (operand2 == MIN_INT64))) {
		result = true;
		goto out;
	}

	/* Now safe to do division checks since we've handled 0 and MIN_INT64/-1 cases */
	if (both_positive) {
		if (operand1 > (MAX_INT64 / operand2)) {
			result = true;
		}
		goto out;
	}

	if (both_negative) {
		if (operand1 < (MAX_INT64 / operand2)) {
			result = true;
		}
		goto out;
	}

	/* Mixed signs - check for underflow */
	if (operand1 < 0) {
		if (operand1 < (MIN_INT64 / operand2)) {
			result = true;
		}
	} else {
		if (operand2 < (MIN_INT64 / operand1)) {
			result = true;
		}
	}

out:
	return result;
}

static inline bool check_multiplication_overflow(int64_t operand1,
						 int64_t operand2)
{
	bool both_positive = (operand1 > 0) && (operand2 > 0);
	bool both_negative = (operand1 < 0) && (operand2 < 0);

	return perform_overflow_check(operand1, operand2, both_positive,
				      both_negative);
}

static inline int64_t muls64(int64_t operand1, int64_t operand2,
			     pva_math_error *math_flag)
{
	/* Check for multiplication overflow - handles all special cases including
	 * zero operands and MIN_INT64 * -1 internally */
	if (check_multiplication_overflow(operand1, operand2)) {
		*math_flag = MATH_OP_ERROR;
		return 0;
	}

	return ((int64_t)(operand1) * (int64_t)(operand2));
}

static inline int32_t muls32(int32_t operand1, int32_t operand2,
			     pva_math_error *math_flag)
{
	int64_t product;

	product = (int64_t)(operand1) * (int64_t)(operand2);
	if ((product > MAX_INT32) || (product < MIN_INT32)) {
		*math_flag = MATH_OP_ERROR;
		return 0;
	}

	return (int32_t)(product);
}

static inline uint32_t wrap_add(uint32_t a, uint32_t b, uint32_t size)
{
	uint32_t result = a + b;
	if (result >= size) {
		result -= size;
	}
	return result;
}

static inline uint8_t wrap_add_u8(uint8_t a, uint8_t b, uint8_t size)
{
	uint32_t result = (uint32_t)a + (uint32_t)b;
	if (result >= (uint32_t)size) {
		result -= (uint32_t)size;
	}
	return (uint8_t)result;
}

/* size must be 2^n */
static inline uint8_t wrap_add_pow2(uint8_t a, uint8_t b, uint8_t size)
{
	uint32_t result;
	ASSERT(size > (uint8_t)0);
	result = ((uint32_t)a + (uint32_t)b) & ((uint32_t)size - 1U);
	/*Fix for CERT INT31-C*/
	return (uint8_t)(result & 0xFFU);
}

static inline uint64_t wraparound_sub_u64(uint64_t minuend, uint64_t subtrahend)
{
	if (minuend >= subtrahend) {
		return minuend - subtrahend;
	} else {
		// Calculate the wrap-around value for underflow
		return (UINT64_MAX - subtrahend + minuend + 1U);
	}
}

/**
* @brief Simple counter increment with wrap-around to zero when reaching UINT32_MAX.
*
* This function safely increments a counter by 1 with wrap-around to zero
* when reaching UINT32_MAX.
*
* @param counter The current counter value.
* @return uint32_t The incremented counter value with wrap-around if needed.
*/
static inline uint32_t safe_wraparound_inc_u32(uint32_t counter)
{
	uint64_t result;

	result = ((uint64_t)counter + (uint64_t)1U);

	return (uint32_t)(result & MAX_UINT32);
}

/**
* @brief Simple counter decrement with wrap-around to UINT32_MAX when reaching zero.
*
* This function safely decrements a counter by 1 with wrap-around to UINT32_MAX
* when reaching zero.
*
* @param counter The current counter value.
* @return uint32_t The decremented counter value with wrap-around if needed.
*/
static inline uint32_t safe_wraparound_dec_u32(uint32_t counter)
{
	uint32_t result;

	if (counter == (uint32_t)0U) {
		result = (uint32_t)MAX_UINT32;
	} else {
		result = counter - (uint32_t)1U;
	}

	return result;
}

static inline uint32_t safe_wrap_add_u32(uint32_t a, uint32_t b)
{
	return (uint32_t)(((uint64_t)a + (uint64_t)b) & 0xFFFFFFFFU);
}

static inline uint32_t safe_wrap_sub_u32(uint32_t a, uint32_t b)
{
	return (uint32_t)(((uint64_t)a - (uint64_t)b) & 0xFFFFFFFFU);
}

static inline uint32_t safe_wrap_mul_u32(uint32_t a, uint32_t b)
{
	return (uint32_t)(((uint64_t)a * (uint64_t)b) & 0xFFFFFFFFU);
}

#define SAT_ADD_DEFINE(a, b, name, type)                                       \
	static inline type sat_add##name(type a, type b)                       \
	{                                                                      \
		type result;                                                   \
		result = (a) + (b);                                            \
		if ((result) < (a)) {                                          \
			result = ~((type)0);                                   \
		}                                                              \
		return result;                                                 \
	}
#define SAT_ADD_DEFINE_CUSTOM(a, b, name, type, maxval)                        \
	static inline type sat_add##name(type a, type b)                       \
	{                                                                      \
		uint32_t result;                                               \
		uint32_t max_val = (maxval);                                   \
		result = (uint32_t)(a) + (uint32_t)(b);                        \
		if ((result) > max_val) {                                      \
			result = max_val;                                      \
		}                                                              \
		return (type)result;                                           \
	}
SAT_ADD_DEFINE_CUSTOM(a, b, 8, uint8_t, 0xFFU)
SAT_ADD_DEFINE_CUSTOM(a, b, 16, uint16_t, 0xFFFFU)
SAT_ADD_DEFINE(a, b, 32, uint32_t)
SAT_ADD_DEFINE(a, b, 64, uint64_t)

#define SAT_SUB_DEFINE(a, b, name, type)                                       \
	static inline type sat_sub##name(type a, type b)                       \
	{                                                                      \
		if ((a) >= (b)) {                                              \
			return (a) - (b);                                      \
		} else {                                                       \
			return 0;                                              \
		}                                                              \
	}

SAT_SUB_DEFINE(a, b, 8, uint8_t)
SAT_SUB_DEFINE(a, b, 16, uint16_t)
SAT_SUB_DEFINE(a, b, 32, uint32_t)
SAT_SUB_DEFINE(a, b, 64, uint64_t)

#define MIN_DEFINE(a, b, name, type)                                           \
	static inline type min##name(type a, type b)                           \
	{                                                                      \
		return ((a) < (b) ? (a) : (b));                                \
	}

#define MAX_DEFINE(a, b, name, type)                                           \
	static inline type max##name(type a, type b)                           \
	{                                                                      \
		return ((a) > (b) ? (a) : (b));                                \
	}

MIN_DEFINE(a, b, u32, uint32_t)
MIN_DEFINE(a, b, u64, uint64_t)
MIN_DEFINE(a, b, s32, int32_t)
MIN_DEFINE(a, b, s64, int64_t)

MAX_DEFINE(a, b, u32, uint32_t)
MAX_DEFINE(a, b, u64, uint64_t)
MAX_DEFINE(a, b, s32, int32_t)
MAX_DEFINE(a, b, s64, int64_t)

/**
* @brief Generates a 64-bit mask based on the specified start position, count, and density.
*
* This function computes a mask from 'lsb' to 'msb' by grouping elements together based on 'density'.
* Each bit in the mask will represent 'density' number of elements. For example, if density is 4
* and count is 12, a total of 3 bits will be set in the produced mask starting at index 'start / 4'.
*

* @param start The starting bit position for the mask.
* @param count The number of bits to include in the mask starting from the start position.
* @param density The density factor, assumed to be a power of 2, represents group size.
*
* @return A 64-bit integer representing the mask with bits set between the calculated msb and lsb.
*/

static inline uint64_t pva_mask64(uint16_t start, uint16_t count,
				  uint16_t density)
{
	uint32_t lsb, msb;
	uint64_t lower_mask, upper_mask;
	uint32_t shift, start_32;

	if (count == 0U) {
		return 0U;
	}

	shift = (uint32_t)__builtin_ctz(density);
	if (shift >= 32U) {
		return 0U;
	}
	start_32 = (uint32_t)start;
	lsb = ((start_32 >> shift) & UINT64_MAX_SHIFT_BITS);
	msb = (((start_32 + (uint32_t)count - 1U) >> shift) &
	       UINT64_MAX_SHIFT_BITS);

	lower_mask = ~safe_subu64((uint64_t)((1ULL << lsb)), 1U);
	upper_mask = (safe_subu64(((uint64_t)(1ULL << msb)), 1U) |
		      ((uint64_t)(1ULL << msb)));
	return (lower_mask & upper_mask);
}

/**
* The size of a block linear surface must be a multiple of RoB (row of blocks).
* Therefore, the maximum block linear surface size that a buffer can store
* needs to be rounded down accordingly.
*/
static inline uint64_t pva_max_bl_surface_size(uint64_t buffer_size,
					       uint8_t log2_block_height,
					       uint32_t line_pitch,
					       pva_math_error *math_error)
{
	uint64_t max_bl_surface_size = 0u;
	uint64_t alignment;
	/* Validate shift amount to prevent CERT-C INT34-C violation */
	if (log2_block_height >= 64U) {
		if (math_error != NULL) {
			*math_error = MATH_OP_ERROR;
		}
		return 0u;
	}
	alignment = mulu64(((uint64_t)1ULL << (uint64_t)log2_block_height),
			   (uint64_t)line_pitch, math_error);

	if (alignment != 0u) {
		max_bl_surface_size = mulu64((buffer_size / alignment),
					     alignment, math_error);
	}

	return max_bl_surface_size;
}

static inline uint64_t pva_get_goboffset(uint32_t const x, uint32_t const y,
					 pva_math_error *math_error)
{
	uint32_t const BL_GOBW = 64;
	uint32_t const BL_GOB_PACK_MASK = BL_GOBW >> 1;
	uint32_t const BL_GOB_PACK_STRIDE = 8;
	uint32_t const BL_GOB_SUBPACK_VER_MASK = 6;
	uint32_t const BL_GOB_SECW = 16; // GOB sector width
	uint32_t const BL_GOB_SECH = 2; // GOB sector height
	uint32_t const BL_GOB_SEC_SZ = BL_GOB_SECW * BL_GOB_SECH;
	uint32_t const BL_GOB_SUBPACK_HOR_MASK = BL_GOB_SEC_SZ >> 1;
	uint32_t const BL_GOB_SUBPACK_VER_STRIDE = 32;
	uint32_t const BL_GOB_SUBPACK_HOR_STRIDE = 2;
	uint32_t const BL_GOB_SEC_VER_MASK = BL_GOB_SECH - 1U;
	uint32_t const BL_GOB_SEC_HOR_MASK = BL_GOB_SECW - 1U;
	uint32_t const BL_GOB_SEC_VER_STRIDE = 16;

	uint32_t const maskedXPack = (x & BL_GOB_PACK_MASK);
	uint32_t const packOffset =
		mulu32(maskedXPack, BL_GOB_PACK_STRIDE, math_error);

	uint32_t const maskedYSubpack = (y & BL_GOB_SUBPACK_VER_MASK);
	uint32_t const maskedXSubpack = (x & BL_GOB_SUBPACK_HOR_MASK);
	uint32_t const subpackOffsetY =
		mulu32(maskedYSubpack, BL_GOB_SUBPACK_VER_STRIDE, math_error);
	uint32_t const subpackOffsetX =
		mulu32(maskedXSubpack, BL_GOB_SUBPACK_HOR_STRIDE, math_error);

	uint32_t const maskedYSec = (y & BL_GOB_SEC_VER_MASK);
	uint32_t const maskedXSec = (x & BL_GOB_SEC_HOR_MASK);
	uint32_t const secOffset =
		addu32(mulu32(maskedYSec, BL_GOB_SEC_VER_STRIDE, math_error),
		       maskedXSec, math_error);

	uint64_t gobOffset = addu64((uint64_t)packOffset,
				    (uint64_t)subpackOffsetX, math_error);
	gobOffset = addu64(gobOffset, (uint64_t)subpackOffsetY, math_error);
	gobOffset = addu64(gobOffset, (uint64_t)secOffset, math_error);

	return gobOffset;
}

/** Convert pitch linear offset to block linear offset
*
* @param pl_offset Pitch linear offset in bytes
* @param line_pitch Surface line pitch in bytes
* @param log2_block_height Log2 of block height
* */
static inline uint64_t pva_pl_to_bl_offset(uint64_t pl_offset,
					   uint32_t line_pitch,
					   uint32_t log2_block_height,
					   pva_math_error *math_error)
{
	/* Validate that pl_offset division result fits in uint32_t */
	uint64_t const y_64 = pl_offset / line_pitch;
	uint64_t const x_64 = pl_offset % line_pitch;
	uint32_t x;
	uint32_t y;
	uint32_t const BL_GOBW_LOG2 = 6;
	uint32_t const BL_GOBH = 8;
	uint32_t const BL_GOBH_LOG2 = 3;
	uint32_t const BL_GOB_SZ_LOG2 = BL_GOBW_LOG2 + BL_GOBH_LOG2;
	uint32_t widthInGobs;
	uint32_t blockSizeLog2;
	uint32_t linesPerBlock;
	uint32_t linesPerBlockLog2;
	uint32_t maskedY;
	uint32_t gobRowbase;
	uint32_t gobX;
	uint32_t gobY;
	uint64_t gobOffset;
	uint32_t gobBase;

	/* Validate that pl_offset division result fits in uint32_t */
	if (y_64 > UINT32_MAX) {
		if (math_error != NULL) {
			*math_error = MATH_OP_ERROR;
		}
		return 0;
	}

	if (x_64 > UINT32_MAX) {
		if (math_error != NULL) {
			*math_error = MATH_OP_ERROR;
		}
		return 0;
	}

	x = (uint32_t)x_64;
	y = (uint32_t)y_64;

	/* Validate shift amount to prevent CERT-C INT34-C violation */
	if (log2_block_height >= 32U) {
		if (math_error != NULL) {
			*math_error = MATH_OP_ERROR;
		}
		return 0;
	}

	widthInGobs = line_pitch >> BL_GOBW_LOG2;
	blockSizeLog2 = addu32(BL_GOB_SZ_LOG2, log2_block_height, math_error);
	linesPerBlock = BL_GOBH << log2_block_height;
	linesPerBlockLog2 = addu32(BL_GOBH_LOG2, log2_block_height, math_error);

	if ((blockSizeLog2 >= 32U) || (linesPerBlockLog2 >= 32U)) {
		if (math_error != NULL) {
			*math_error = MATH_OP_ERROR;
		}
		return 0;
	}

	maskedY = y & subu32(linesPerBlock, 1, math_error);
	gobRowbase = (maskedY >> BL_GOBH_LOG2) << BL_GOB_SZ_LOG2;
	gobX = (x >> BL_GOBW_LOG2) << blockSizeLog2;
	gobY = (y >> linesPerBlockLog2) << blockSizeLog2;

	gobOffset = pva_get_goboffset(x, y, math_error);
	gobBase = mulu32(gobY, widthInGobs, math_error);
	gobBase = addu32(gobBase, gobRowbase, math_error);
	gobBase = addu32(gobBase, gobX, math_error);

	return addu64((uint64_t)gobBase, gobOffset, math_error);
}

static inline bool syncobj_reached_threshold(uint32_t value, uint32_t threshold)
{
	/*
	* We're interested in "value >= threshold" but need to take wraparound
	* into account. Ideally signed arithmetic of (value - threshold) >= 0
	* should do, which can handle max wrap difference of half the uint
	* range.
	*/
	uint32_t a = threshold;
	uint32_t b = value;
	uint32_t two_pow_31 = 0x80000000u;
	uint32_t c = 0u;
	uint32_t distance_ab;
	uint32_t distance_ac;

	if (a < two_pow_31) {
		c = a + two_pow_31;
	} else {
		c = a & 0x7FFFFFFFu;
	}

	/* If we imagine numbers between 0 and (1<<32)-1 placed along a circle,
	* then a-b is exactly the distance from b to a along the circle moving
	* clockwise. This test checks that the distance between a and b is
	* strictly smaller than the distance between a and c.
	*/

	/* Underflow of unsigned value, if happens, is intentional. */
	distance_ab = b - a;
	distance_ac = c - a;
	return (distance_ab < distance_ac);
}

#endif
