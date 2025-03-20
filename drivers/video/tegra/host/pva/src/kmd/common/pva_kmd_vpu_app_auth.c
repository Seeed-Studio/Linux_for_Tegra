// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_api_types.h"
#include "pva_kmd_vpu_app_auth.h"
#include "pva_kmd_device.h"
#include "pva_kmd_sha256.h"
#include "pva_kmd_utils.h"

enum pva_error pva_kmd_init_vpu_app_auth(struct pva_kmd_device *pva, bool ena)
{
	enum pva_error err = PVA_SUCCESS;
	const char *default_path = pva_kmd_get_default_allowlist();
	size_t default_path_len;
	struct pva_vpu_auth *pva_auth = pva_kmd_zalloc(sizeof(*pva_auth));
	if (pva_auth == NULL) {
		pva_kmd_log_err("Unable to allocate memory");
		err = PVA_NOMEM;
		goto error;
	}

	pva->pva_auth = pva_auth;
	ASSERT(pva_auth != NULL);

	pva_auth->vpu_hash_keys = NULL;
	pva_auth->pva_auth_allow_list_parsed = false;
	/**TODO - This will be disabled by default. Authentication will be enabled based on 2 things
	 * 1. Debug FS (For non production)
	 * 2. Device tree property (For production)
	 * Either of the 2 conditions if satisfied will enable authentication
	 */
	pva_auth->pva_auth_enable = ena;
	err = pva_kmd_mutex_init(&pva_auth->allow_list_lock);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Failed to initialize allow list lock");
		goto free;
	}

	default_path_len = strnlen(default_path, ALLOWLIST_FILE_LEN);
	if (default_path_len > 0U) {
		(void)memcpy(pva_auth->pva_auth_allowlist_path, default_path,
			     default_path_len);
		pva_auth->pva_auth_allowlist_path[default_path_len] = '\0';
	}

	return PVA_SUCCESS;

free:
	pva_kmd_free(pva_auth);
error:
	pva->pva_auth = NULL;
	return err;
}

/**
 * \brief
 * is_key_match calculates the sha256 key of ELF and checks if it matches with key.
 * \param[in] dataptr Pointer to the data to which sha256 to ba calculated
 * \param[in] size length in bytes of the data to which sha256 to be calculated.
 * \param[in] key the key with which calculated key would be compared for match.
 * \return The completion status of the operation. Possible values are:
 * \ref PVA_SUCCESS Success. Passed in key matched wth calculated key.
 * \ref -EACCES. Passed in Key doesn't match with calcualted key.
 */
static enum pva_error is_key_match(uint8_t *dataptr, size_t size,
				   struct shakey key)
{
	enum pva_error err = PVA_SUCCESS;
	int32_t status = 0;
	uint32_t calc_key[8];
	size_t off;
	struct sha256_ctx ctx1;
	struct sha256_ctx ctx2;

	sha256_init(&ctx1);
	off = (size / 64U) * 64U;
	if (off > 0U) {
		sha256_update(&ctx1, dataptr, off);
	}

	/* clone */
	sha256_copy(&ctx1, &ctx2);

	/* finalize with leftover, if any */
	sha256_finalize(&ctx2, dataptr + off, size % 64U, calc_key);

	status = memcmp((void *)&(key.sha_key), (void *)calc_key,
			NVPVA_SHA256_DIGEST_SIZE);
	if (status != 0) {
		err = PVA_EACCES;
	}

	return err;
}

/**
 * \brief
 * Keeps checking all the keys accociated with match_hash
 * against the calculated sha256 key for dataptr, until it finds a match.
 * \param[in] pallkeys Pointer to array of SHA keys \ref shakey
 * \param[in] dataptr pointer to ELF data
 * \param[in] size length (in bytes) of ELF data
 * \param[in] match_hash pointer to matching hash structure, \ref struct vpu_hash_vector.
 * \return Matching status of the calculated key
 * against the keys asscociated with match_hash. possible values:
 * - 0 Success, one of the keys associated with match_hash
 * matches with the calculated sha256 key.
 * - -EACCES if no match found.
 */
static enum pva_error
check_all_keys_for_match(struct shakey *pallkeys, uint8_t *dataptr, size_t size,
			 const struct vpu_hash_vector *match_hash)
{
	enum pva_error err = PVA_SUCCESS;
	uint32_t idx;
	uint32_t count;
	uint32_t end;
	struct shakey key;
	uint32_t i;

	idx = match_hash->index;
	count = match_hash->count;
	end = idx + count;
	if (end < idx) {
		err = PVA_ERANGE;
		goto fail;
	}

	for (i = 0; i < count; i++) {
		key = pallkeys[idx + i];
		err = is_key_match(dataptr, size, key);
		if (err == PVA_SUCCESS) {
			break;
		}
	}
fail:
	return err;
}

/**
 * @brief
 * Helper function for \ref binary_search.
 * Uses a specific field in @ref pkey to compare with the same filed in @ref pbase.
 * @param[in] pkey pointer to the object that needs to be compared.
 * @param[in] pbase pointer to the starting element of the array.
 * @retval
 * - -1 when @ref pkey is less than starting element of array pointed to by @ref pbase.
 * - 1 when @ref pkey is greater than starting element of array pointed to by @ref pbase.
 * - 0 when @ref pkey is equal to starting element of array pointed to by @ref pbase.
 */
static int32_t compare_hash_value(const struct vpu_hash_vector *pkey,
				  const struct vpu_hash_vector *pbase)
{
	int32_t ret;

	if (pkey->crc32_hash < pbase->crc32_hash) {
		ret = -1;
	} else if (pkey->crc32_hash > pbase->crc32_hash) {
		ret = 1;
	} else {
		ret = 0;
	}

	return ret;
}

/**
 * @brief
 * calculates crc32.
 * @param[in] crc initial crc value. usually 0.
 * @param[in] buf pointer to the buffer whose crc32 to be calculated.
 * @param[in] len length (in bytes) of data at @ref buf.
 * @retval value of calculated crc32.
 */
static uint32_t pva_crc32(uint32_t crc, uint8_t *buf, size_t len)
{
	int32_t k;
	size_t count;

	count = len;
	crc = ~crc;
	while (count != 0U) {
		crc ^= *buf++;
		for (k = 0; k < 8; k++) {
			crc = ((crc & 1U) == 1U) ? (crc >> 1U) ^ 0xedb88320U :
							 crc >> 1U;
		}

		count--;
	}

	return ~crc;
}

static const struct vpu_hash_vector *
binary_search(const struct vpu_hash_vector *key,
	      const struct vpu_hash_vector *base, size_t num_elems,
	      int32_t (*compare)(const struct vpu_hash_vector *pkey,
				 const struct vpu_hash_vector *pbase))
{
	size_t low = 0U;
	size_t high;

	if (num_elems == 0U) {
		return NULL;
	}

	high = num_elems - 1U;
	for (;;) {
		const struct vpu_hash_vector *mid_elem;
		int32_t r;
		size_t mid = low + ((high - low) / 2U);

		mid_elem = &(base[mid]);
		r = compare(key, mid_elem);

		if (r < 0) {
			if (mid == 0U) {
				return NULL;
			}

			high = mid - 1U;
		} else if (r > 0) {
			low = mid + 1U;
			if (low < mid || low > high) {
				return NULL;
			}
		} else {
			return mid_elem;
		}
	}
}

static enum pva_error
pva_kmd_vpu_check_sha256_key(struct vpu_hash_key_pair *vpu_hash_keys,
			     uint8_t *dataptr, size_t size)
{
	enum pva_error err = PVA_SUCCESS;
	struct vpu_hash_vector cal_Hash;
	const struct vpu_hash_vector *match_Hash;

	cal_Hash.crc32_hash = pva_crc32(0L, dataptr, size);

	match_Hash = (const struct vpu_hash_vector *)binary_search(
		&cal_Hash, vpu_hash_keys->pvpu_hash_vector,
		vpu_hash_keys->num_hashes, compare_hash_value);
	if (match_Hash == NULL) {
		pva_kmd_log_err("No Hash Match Found");
		err = PVA_EACCES;
		goto fail;
	}

	err = check_all_keys_for_match(vpu_hash_keys->psha_key, dataptr, size,
				       match_Hash);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Match key not found");
	}
fail:
	return err;
}

enum pva_error pva_kmd_verify_exectuable_hash(struct pva_kmd_device *pva,
					      const uint8_t *dataptr,
					      size_t size)
{
	enum pva_error err = PVA_SUCCESS;
	struct pva_vpu_auth *pva_auth;

	ASSERT(pva != NULL);
	ASSERT(dataptr != NULL);
	pva_auth = pva->pva_auth;
	ASSERT(pva_auth != NULL);

	pva_kmd_mutex_lock(&pva_auth->allow_list_lock);
	if (pva_auth->pva_auth_enable) {
		pva_dbg_printf("App authentication enabled\n");
		if (pva_auth->pva_auth_allow_list_parsed == false) {
			err = pva_kmd_allowlist_parse(pva);
			if (err == PVA_SUCCESS) {
				pva_dbg_printf(
					"App authentication allowlist parsing successfull\n");
			} else {
				pva_dbg_printf(
					"App authentication allowlist parsing failed\n");
			}
		}

		if (err == PVA_SUCCESS) {
			err = pva_kmd_vpu_check_sha256_key(
				pva_auth->vpu_hash_keys, (uint8_t *)dataptr,
				size);
			if (err == PVA_SUCCESS) {
				pva_dbg_printf(
					"App authentication successfull\n");
			} else {
				pva_dbg_printf(
					"App authentication failed : %d\n",
					err);
			}
		}
	} else {
		pva_dbg_printf("App authentication disabled\n");
	}

	pva_kmd_mutex_unlock(&pva_auth->allow_list_lock);

	return err;
}

static void pva_kmd_allowlist_destroy(struct pva_vpu_auth *pva_auth)
{
	if (pva_auth->vpu_hash_keys != NULL) {
		pva_kmd_free(pva_auth->vpu_hash_keys->ptr_file_data);
		pva_kmd_free(pva_auth->vpu_hash_keys);
		pva_auth->vpu_hash_keys = NULL;
	}
}

enum pva_error pva_kmd_allowlist_parse(struct pva_kmd_device *pva)
{
	struct pva_vpu_auth *pva_auth = pva->pva_auth;
	enum pva_error err = PVA_SUCCESS;
	uint8_t *data = NULL;
	uint64_t size = 0;
	struct vpu_hash_key_pair *vhashk;
	size_t vkey_size = 0;
	size_t vhash_size = 0;

	ASSERT(pva_auth != NULL);

	//Destroy previously parsed allowlist data
	pva_kmd_allowlist_destroy(pva_auth);

	pva_dbg_printf("Allowlist path: %s\n",
		       pva_auth->pva_auth_allowlist_path);
	err = pva_kmd_auth_allowlist_load(
		pva, pva_auth->pva_auth_allowlist_path, &data, &size);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Failed to load allowlist\n");
		if (data != NULL) {
			pva_kmd_free(data);
		}
		goto fail;
	}
	vhashk = (struct vpu_hash_key_pair *)pva_kmd_zalloc(
		sizeof(struct vpu_hash_key_pair));
	if (vhashk == NULL) {
		pva_kmd_log_err("Unable to allocate memory");
		pva_kmd_free(data);
		err = PVA_NOMEM;
		goto fail;
	}

	vhashk->ptr_file_data = data;
	vhashk->num_keys = ((uint32_t *)(uintptr_t)data)[0];
	vhashk->psha_key =
		(struct shakey *)(uintptr_t)(data + sizeof(uint32_t));
	vkey_size = sizeof(struct shakey) * (vhashk->num_keys);
	vhashk->num_hashes = ((uint32_t *)(uintptr_t)((char *)vhashk->psha_key +
						      vkey_size))[0];
	vhashk->pvpu_hash_vector =
		(struct vpu_hash_vector
			 *)(uintptr_t)((char *)(vhashk->psha_key) + vkey_size +
				       sizeof(uint32_t));
	vhash_size = sizeof(struct vpu_hash_vector) * (vhashk->num_hashes);
	if ((sizeof(uint32_t) + sizeof(uint32_t) + vkey_size + vhash_size) !=
	    size) {
		pva_kmd_free(data);
		pva_kmd_free(vhashk);
		err = PVA_EACCES;
		goto fail;
	}

	pva_auth->pva_auth_allow_list_parsed = true;
	pva_auth->vpu_hash_keys = vhashk;

fail:
	return err;
}

void pva_kmd_deinit_vpu_app_auth(struct pva_kmd_device *pva)
{
	struct pva_vpu_auth *pva_auth;

	if (pva == NULL)
		return;

	pva_auth = pva->pva_auth;
	if (pva_auth == NULL)
		return;

	pva_kmd_allowlist_destroy(pva_auth);
	pva_kmd_mutex_deinit(&pva_auth->allow_list_lock);
	pva_kmd_free(pva_auth);
	pva->pva_auth = NULL;
}
