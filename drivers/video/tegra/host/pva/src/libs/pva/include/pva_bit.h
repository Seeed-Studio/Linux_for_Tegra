/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_BIT_H
#define PVA_BIT_H
#include "pva_api.h"
/*
 * Bit manipulation macros
 */

/**
 * @brief Number of bits per byte.
 */
#define PVA_BITS_PER_BYTE (8UL)

/**
 * @defgroup PVA_BIT8_HELPER
 *
 * @brief Bit Manipulation macros for number which is of type uint8_t.
 *        Parameter that convey the bit position should be in the range
 *        of 0 to 7 inclusive.
 *        Parameter with respect to MSB and LSB should satisfy the conditions
 *        of both being in the range of 0 to 7 inclusive with MSB greater than LSB.
 * @{
 */
/**
 * @brief Macro to set a given bit position in a variable of type uint8_t.
 */
#define PVA_BIT8(_b_) ((uint8_t)(((uint8_t)1U << (_b_)) & 0xffu))

//! @cond DISABLE_DOCUMENTATION
/**
 * @brief Macro used to generate a bit-mask from MSB to LSB in a uint8_t variable.
 *        This macro sets all the bits from MSB to LSB.
 */
#define PVA_MASK8(_msb_, _lsb_)                                                \
	((uint8_t)((((PVA_BIT8(_msb_) - 1U) | PVA_BIT8(_msb_)) &               \
		    ~(PVA_BIT8(_lsb_) - 1U)) &                                 \
		   0xffu))

//! @cond DISABLE_DOCUMENTATION
/**
 * @brief Macro to extract bits from a 8 bit number.
 * The bits are extracted from the range provided and the extracted
 * number is finally type-casted to the type provided as argument.
 */
#define PVA_EXTRACT8(_x_, _msb_, _lsb_, _type_)                                \
	((_type_)(((_x_)&PVA_MASK8((_msb_), (_lsb_))) >> (_lsb_)))

#define PVA_INSERT8(_x_, _msb_, _lsb_)                                         \
	((((uint8_t)(_x_)) << (_lsb_)) & PVA_MASK8((_msb_), (_lsb_)))
//! @endcond

//! @endcond
/** @} */

/**
 * @defgroup PVA_BIT16_HELPER
 *
 * @brief Bit Manipulation macros for number which is of type uint16_t.
 *        Parameter that convey the bit position should be in the range
 *        of 0 to 15 inclusive.
 *        Parameter with respect to MSB and LSB should satisfy the conditions
 *        of both being in the range of 0 to 15 inclusive with MSB greater than LSB.
 * @{
 */
/**
 * @brief Macro to set a given bit position in a 16 bit number.
 */
#define PVA_BIT16(_b_) ((uint16_t)(((uint16_t)1U << (_b_)) & 0xffffu))

/**
 * @brief Macro to mask a range(MSB to LSB) of bit positions in a 16 bit number.
 * This will set all the bit positions in specified range.
 */
#define PVA_MASK16(_msb_, _lsb_)                                               \
	((uint16_t)((((PVA_BIT16(_msb_) - 1U) | PVA_BIT16(_msb_)) &            \
		     ~(PVA_BIT16(_lsb_) - 1U)) &                               \
		    0xffffu))

//! @cond DISABLE_DOCUMENTATION
/**
 * @brief Macro to extract bits from a 16 bit number.
 * The bits are extracted from the range provided and the extracted
 * number is finally type-casted to the type provided as argument.
 */
#define PVA_EXTRACT16(_x_, _msb_, _lsb_, _type_)                               \
	((_type_)(((_x_)&PVA_MASK16((_msb_), (_lsb_))) >> (_lsb_)))
//! @endcond

/**
 * @brief Macro used to generate a bit-mask from MSB to LSB in a uint16_t variable.
 *        This macro sets all the bits from MSB to LSB.
 */
#define PVA_INSERT16(_x_, _msb_, _lsb_)                                        \
	((((uint16_t)(_x_)) << (_lsb_)) & PVA_MASK16((_msb_), (_lsb_)))
/** @} */

/**
 * @defgroup PVA_BIT32_HELPER
 *
 * @brief Bit Manipulation macros for number which is of type uint32_t.
 *        Parameter that convey the bit position should be in the range
 *        of 0 to 31 inclusive.
 *        Parameter with respect to MSB and LSB should satisfy the conditions
 *        of both being in the range of 0 to 31 inclusive with MSB greater than LSB.
 * @{
 */

/**
 * @brief Macro to set a given bit position in a 32 bit number.
 */
#define PVA_BIT(_b_) ((uint32_t)(((uint32_t)1U << (_b_)) & 0xffffffffu))

/**
 * @brief Macro to mask a range(MSB to LSB) of bit positions in a 32 bit number.
 * This will set all the bit positions in specified range.
 */
#define PVA_MASK(_msb_, _lsb_)                                                 \
	(((PVA_BIT(_msb_) - 1U) | PVA_BIT(_msb_)) & ~(PVA_BIT(_lsb_) - 1U))

/**
 * @brief Macro to extract bits from a 32 bit number.
 * The bits are extracted from the range provided and the extracted
 * number is finally type-casted to the type provided as argument.
 */
#define PVA_EXTRACT(_x_, _msb_, _lsb_, _type_)                                 \
	((_type_)(((_x_)&PVA_MASK((_msb_), (_lsb_))) >> (_lsb_)))

/**
 * @brief Macro to insert a range of bits from a given 32 bit number.
 * Range of bits are derived from the number passed as argument.
 */
#define PVA_INSERT(_x_, _msb_, _lsb_)                                          \
	((((uint32_t)(_x_)) << (_lsb_)) & PVA_MASK((_msb_), (_lsb_)))
/** @} */

/**
 * @defgroup PVA_BIT64_HELPER
 *
 * @brief Bit Manipulation macros for number which is of type uint64_t.
 *        Parameter that convey the bit position should be in the range
 *        of 0 to 63 inclusive.
 *        Parameter with respect to MSB and LSB should satisfy the conditions
 *        of both being in the range of 0 to 63 inclusive with MSB greater than LSB.
 * @{
 */
/**
 * @brief Macro to set a given bit position in a 64 bit number.
 */
#define PVA_BIT64(_b_)                                                         \
	((uint64_t)(((uint64_t)1UL << (_b_)) & 0xffffffffffffffffu))

/**
 * @brief Macro used to generate a bit-mask from (MSB to LSB) in a uint64_t variable.
 *        This macro sets all the bits from MSB to LSB.
 */
#define PVA_MASK64(_msb_, _lsb_)                                               \
	(((PVA_BIT64(_msb_) - (uint64_t)1U) | PVA_BIT64(_msb_)) &              \
	 ~(PVA_BIT64(_lsb_) - (uint64_t)1U))

/**
 * @brief Macro to extract bits from a 64 bit number.
 * The bits are extracted from the range provided and the extracted
 * number is finally type-casted to the type provided as argument.
 */
#define PVA_EXTRACT64(_x_, _msb_, _lsb_, _type_)                               \
	((_type_)(((_x_)&PVA_MASK64((_msb_), (_lsb_))) >> (_lsb_)))

/**
 * @brief Macro to insert a range of bits into a 64 bit number.
 * The bits are derived from the number passed as argument.
 */
#define PVA_INSERT64(_x_, _msb_, _lsb_)                                        \
	((((uint64_t)(_x_)) << (_lsb_)) & PVA_MASK64((_msb_), (_lsb_)))

/**
 * @brief Macro to pack a 64 bit number.
 * A 64 bit number is generated that has first 32 MSB derived from
 * upper 32 bits of passed argument and has lower 32MSB derived from
 * lower 32 bits of another passed argument.
 */
#define PVA_PACK64(_l_, _h_)                                                   \
	(PVA_INSERT64((_h_), 63U, 32U) | PVA_INSERT64((_l_), 31U, 0U))

/**
 * @brief Macro to extract the higher 32 bits from a 64 bit number.
 */
#define PVA_HI32(_x_) ((uint32_t)(((_x_) >> 32U) & 0xFFFFFFFFU))

/**
 * @brief Macro to extract the lower 32 bits from a 64 bit number.
 */
#define PVA_LOW32(_x_) ((uint32_t)((_x_)&0xFFFFFFFFU))
/** @} */

#endif // PVA_BIT_H
