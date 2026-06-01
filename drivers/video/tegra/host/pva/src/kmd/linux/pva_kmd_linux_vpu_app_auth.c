// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_shim_vpu_app_auth.h"
#include "pva_kmd_linux_device.h"
#include "pva_kmd_linux_device_api.h"
#include <linux/firmware.h>

/**
 * Default path (including filename) of pva vpu elf authentication allowlist file
 */
#define PVA_AUTH_ALLOW_LIST_DEFAULT "pva_auth_allowlist"

/**
 * @brief Loads allowlist into memory
 *
 * Reads the content of allowlist file using request_firmware API
 * Allocate memory of same size using \ref pva_kmd_zalloc,
 * Copies the data content to this allocated memory
 * It is responsibility of caller to free this memory using pva_kmd_free
 *
 * @param[in] pva KMD device structure pointer
 * @param[in] file_name Allow list file name
 * @param[out] hash_keys_data pointer to pointer pointing to data, where read data to be copied
 * @param[out] psize pointer, where size of the data to be updated
 * @return
 * - PVA_SUCCESS On success
 * - PVA_NOENT if unable to read allowlist file
 * - PVA_NOMEM if allocation fails
 */
enum pva_error pva_kmd_auth_allowlist_load(struct pva_kmd_device *pva,
					   const char *file_name,
					   uint8_t **hash_keys_data,
					   uint64_t *psize)
{
	enum pva_error err = PVA_SUCCESS;
	int32_t kerr = 0;
	struct nvpva_device_data *pdata = pva_kmd_linux_device_get_data(pva);
	const struct firmware *pallow_list;

	ASSERT(file_name != NULL);

	kerr = request_firmware(&pallow_list, file_name, &pdata->pdev->dev);

	if (kerr < 0) {
		pva_kmd_log_err("Failed to load the allow list\n");
		err = PVA_NOENT;
		goto out;
	}

	*psize = (uint64_t)pallow_list->size;
	*hash_keys_data = pva_kmd_zalloc((size_t)pallow_list->size);
	if (*hash_keys_data == NULL) {
		pva_kmd_log_err("Unable to allocate memory");
		err = PVA_NOMEM;
		goto release;
	}

	(void)memcpy(*hash_keys_data, pallow_list->data, pallow_list->size);

release:
	release_firmware(pallow_list);

out:
	return err;
}

const char *pva_kmd_get_default_allowlist(void)
{
	return PVA_AUTH_ALLOW_LIST_DEFAULT;
}

void pva_kmd_update_allowlist_path(struct pva_kmd_device *pva,
				   const char *allowlist_path)
{
	//Stub definition
	pva_dbg_printf("Allow list path update ignored in linux");
}
