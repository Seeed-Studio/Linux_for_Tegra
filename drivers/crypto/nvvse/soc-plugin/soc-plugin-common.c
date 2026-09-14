// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 */

#include "soc_plugin_hal.h"

EXPORT_SYMBOL(soc_plugin_hal_init);
EXPORT_SYMBOL(soc_plugin_hal_deinit);

EXPORT_SYMBOL(soc_plugin_hal_get_sha_capabilities);
EXPORT_SYMBOL(soc_plugin_hal_init_sha_param);
EXPORT_SYMBOL(soc_plugin_hal_allocate_resp_msg_buffer);
EXPORT_SYMBOL(soc_plugin_hal_free_resp_msg_buffer);
EXPORT_SYMBOL(soc_plugin_hal_deinit_sha_param);
EXPORT_SYMBOL(soc_plugin_hal_allocate_sha_msg_buffer);
EXPORT_SYMBOL(soc_plugin_hal_free_sha_msg_buffer);
EXPORT_SYMBOL(soc_plugin_hal_set_sha_msg_to_se);
EXPORT_SYMBOL(soc_plugin_hal_process_sha_msg_from_se);
EXPORT_SYMBOL(soc_plugin_hal_validate_and_set_sha_req_param);
EXPORT_SYMBOL(soc_plugin_hal_validate_and_set_hmac_sha_req_param);
EXPORT_SYMBOL(soc_plugin_hal_allocate_hmac_sha_msg_buffer);
EXPORT_SYMBOL(soc_plugin_hal_free_hmac_sha_msg_buffer);
EXPORT_SYMBOL(soc_plugin_hal_set_hmac_sha_msg_to_se);
EXPORT_SYMBOL(soc_plugin_hal_process_hmac_sha_msg_to_se);
EXPORT_SYMBOL(soc_plugin_hal_init_hmac_sha_param);
EXPORT_SYMBOL(soc_plugin_hal_deinit_hmac_sha_param);
EXPORT_SYMBOL(soc_plugin_hal_get_hmac_sha_capabilities);
