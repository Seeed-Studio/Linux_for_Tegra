/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_API_PRIVATE_H
#define PVA_API_PRIVATE_H

#include "pva_api.h"

//For legacy support not exposed by public API
#define PVA_CMD_FLAGS_USE_LEGACY_POINTER 0x1
struct pva_fw_vpu_legacy_ptr_symbol {
	uint64_t base;
	uint32_t offset;
	uint32_t size;
};

enum pva_error_inject_codes {
	PVA_ERR_INJECT_WDT_HW_ERR, // watchdog Hardware error
	PVA_ERR_INJECT_WDT_TIMEOUT, // watchdog Timeout error
	PVA_ERR_INJECT_VMEM_CLEAR, // vmem clear
	PVA_ERR_INJECT_ASSERT_CHECK, // assert check
	PVA_ERR_INJECT_ARMV7_EXCEPTION, // ARMv7 exception
};

struct pva_cmd_run_unit_tests {
#define PVA_CMD_OPCODE_RUN_UNIT_TESTS (PVA_CMD_OPCODE_MAX + 0U)
	struct pva_cmd_header header;
#define PVA_FW_UTESTS_MAX_ARGC 16U
	uint8_t argc;
	uint8_t pad[3];
	uint32_t in_resource_id;
	uint32_t in_offset;
	uint32_t in_size;
	uint32_t out_resource_id;
	uint32_t out_offset;
	uint32_t out_size;
};

struct pva_cmd_err_inject {
#define PVA_CMD_OPCODE_ERR_INJECT (PVA_CMD_OPCODE_MAX + 1U)
	struct pva_cmd_header header;
	uint32_t err_inject_code; // enum pva_error_inject_codes
};

struct pva_cmd_gr_check {
#define PVA_CMD_OPCODE_GR_CHECK (PVA_CMD_OPCODE_MAX + 2U)
	struct pva_cmd_header header;
};

#define PVA_CMD_OPCODE_COUNT (PVA_CMD_OPCODE_MAX + 3U)

#endif // PVA_API_PRIVATE_H
