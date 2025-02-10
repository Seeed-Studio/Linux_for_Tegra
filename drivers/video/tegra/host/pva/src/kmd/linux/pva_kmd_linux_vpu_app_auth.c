/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, NVIDIA Corporation.  All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation.  Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */

#include "pva_kmd_shim_vpu_app_auth.h"
#include "pva_kmd_linux_device.h"
#include <linux/firmware.h>
#include <linux/nvhost.h>

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
	struct pva_kmd_linux_device_data *device_data =
		pva_kmd_linux_device_get_data(pva);
	struct nvhost_device_data *device_props =
		device_data->pva_device_properties;

	const struct firmware *pallow_list;

	ASSERT(file_name != NULL);

	kerr = request_firmware(&pallow_list, file_name,
				&device_props->pdev->dev);

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
