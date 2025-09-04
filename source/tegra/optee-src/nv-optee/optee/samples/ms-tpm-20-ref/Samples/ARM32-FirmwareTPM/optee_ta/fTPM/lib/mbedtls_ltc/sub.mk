# Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

# This links to the LTC ECC functions from the OPTEE OS.
# The under laying of the ECC functions are the MP wrappers that are provided by MbedTLS MPI.
# See TPMCmd/tpm/src/crypt/Mbedtls/Mbedtls_mpi_Ltc_desc.c

LTC_ROOT := $(OPTEE_OS_DIR)/core/lib/libtomcrypt/src/

USER_CUSTOM_INCLUDES = -include ./user_custom.h

cflags-y += -DARGTYPE=4 -DLTC_MAX_ECC=521 $(USER_CUSTOM_INCLUDES)

global-incdirs-y += ./
global-incdirs_ext-y += $(LTC_ROOT)/headers

srcs-y += $(LTC_ROOT)/math/multi.c
srcs-y += $(LTC_ROOT)/pk/ecc/ltc_ecc_is_point_at_infinity.c
srcs-y += $(LTC_ROOT)/pk/ecc/ltc_ecc_map.c
srcs-y += $(LTC_ROOT)/pk/ecc/ltc_ecc_mul2add.c
srcs-y += $(LTC_ROOT)/pk/ecc/ltc_ecc_mulmod_timing.c
srcs-y += $(LTC_ROOT)/pk/ecc/ltc_ecc_points.c
srcs-y += $(LTC_ROOT)/pk/ecc/ltc_ecc_projective_add_point.c
srcs-y += $(LTC_ROOT)/pk/ecc/ltc_ecc_projective_dbl_point.c
