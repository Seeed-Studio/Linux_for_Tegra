/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 *
 * Cryptographic API.
 */

#ifndef __UAPI_NVVSE_IOCTL_H
#define __UAPI_NVVSE_IOCTL_H

#include <asm-generic/ioctl.h>

#define KEYSLOT_SIZE_BYTES				16
#define NVVSE_IOC_MAGIC				0xF8
#define MAX_NUMBER_MISC_DEVICES				75U
#define MAX_NUMBER_SOC_PARAMS				10U

/* Command ID for various IO Control */
#define NVVSE_CMDID_UPDATE_SHA			6
#define NVVSE_CMDID_HMAC_SHA_SIGN_VERIFY		15
#define NVVSE_CMDID_MAP_MEMBUF			17
#define NVVSE_CMDID_UNMAP_MEMBUF			18
#define NVVSE_CMDID_GET_IVC_DB			12

/** Defines the length of the AES-CBC Initial Vector */
#define NVVSE_AES_IV_LEN				16U

/**
 * \brief Defines SHA Types.
 */
enum nvvse_sha_type {
	/** Defines SHA-256 Type */
	NVVSE_DEV_SHA_TYPE_SHA256 = 0u,
	/** Defines SHA-384 Type */
	NVVSE_DEV_SHA_TYPE_SHA384,
	/** Defines SHA-512 Type */
	NVVSE_DEV_SHA_TYPE_SHA512,
	/** Defines SHA3-256 Type */
	NVVSE_DEV_SHA_TYPE_SHA3_256,
	/** Defines SHA3-384 Type */
	NVVSE_DEV_SHA_TYPE_SHA3_384,
	/** Defines SHA3-512 Type */
	NVVSE_DEV_SHA_TYPE_SHA3_512,
	/** Defines SHAKE-128 Type */
	NVVSE_DEV_SHA_TYPE_SHAKE128,
	/** Defines SHAKE256 Type */
	NVVSE_DEV_SHA_TYPE_SHAKE256,
	/** Defines SM3 Type */
	NVVSE_DEV_SHA_TYPE_SM3,
	/** Defines maximum SHA Type, must be last entry */
	NVVSE_SHA_TYPE_MAX,
};

/**
 * \brief Defines HMAC SHA request type.
 */
enum nvvse_hmac_sha_sv_type {
	/** Defines AES GMAC Sign */
	NVVSE_DEV_HMAC_SHA_SIGN = 0u,
	/** Defines AES GMAC Verify */
	NVVSE_DEV_HMAC_SHA_VERIFY,
};

/**
 * \brief Holds SHA Update Header Params
 */
struct nvvse_sha_update_ctl {
	/** Holds the SHA request type */
	enum nvvse_sha_type sha_type;
	/** Specifies first request */
	uint8_t is_first;
	/** Specifies last request */
	uint8_t is_last;
	/** Specifies if only init is to be performed */
	uint8_t init_only;
	/** Specifies if context is to be reinitialized */
	uint8_t do_reset;
	/**
	 * Holds the pointer of the input buffer
	 * The buffer address is validated by the driver.
	 * Error is returned if the buffer address is invalid.
	 */
	uint8_t *in_buff;
	/** Holds the size of the input buffer */
	uint32_t input_buffer_size;
	/**
	 * Holds the pointer of the digest buffer
	 * The buffer address is validated by the driver.
	 * Error is returned if the buffer address is invalid.
	 */
	uint8_t	*digest_buffer;
	/** Holds the size of the digest buffer */
	uint32_t digest_size;
	/** [in] Flag to indicate Zero copy request.
	 * 0 indicates non-Zero Copy request
	 * non-zero indicates Zero copy request
	 */
	uint8_t  b_is_zero_copy;
	/** [in] Holds the Input buffer IOVA address
	 * Not used when b_is_zero_copy flag is 0.
	 */
	uint64_t in_buff_iova;
};
#define NVVSE_IOCTL_CMDID_UPDATE_SHA _IOW(NVVSE_IOC_MAGIC, NVVSE_CMDID_UPDATE_SHA, \
						struct nvvse_sha_update_ctl)

struct nvvse_hmac_sha_sv_ctl {
	/** [in] Holds the enum which indicates SHA mode */
	enum nvvse_sha_type hmac_sha_mode;
	/** [in] Holds the enum which indicates HMAC SHA Sign or Verify */
	enum nvvse_hmac_sha_sv_type hmac_sha_type;
	/** [in] Holds a Boolean that specifies whether this is first
	 * chunk of message for HMAC-SHA Sign/Verify.
	 * '0' value indicates it is not First call and
	 * Non zero value indicates it is the first call.
	 */
	uint8_t  is_first;
	/** [in] Holds a Boolean that specifies whether this is last
	 * chunk of message for HMAC-SHA Sign/Verify.
	 * '0' value indicates it is not Last call and
	 *  Non zero value indicates it is the Last call.
	 */
	uint8_t  is_last;
	/** [in] Holds a keyslot handle which is used for HMAC-SHA operation */
	uint8_t	key_slot[KEYSLOT_SIZE_BYTES];
	/** [in] Holds the token id */
	uint8_t token_id;
	/** [in] Holds a pointer to the input source buffer for which
	 *  HMAC-SHA is to be calculated/verified.
	 */
	uint8_t *src_buffer;
	/** [in] Holds the Length of the input source buffer.
	 * data_length shall not be "0" supported for single part sign and verify
	 * data_length shall be multiple of hashblock size if it is not the last chunk
	 * i.e when is_last is "0"
	 */
	uint32_t data_length;
	/** Holds the pointer of the digest buffer */
	uint8_t		*digest_buffer;
	/** Holds the digest buffer length */
	uint32_t digest_length;
	/** [out] Holds HMAC-SHA verification result, which the driver updates.
	 * Valid only when hmac_sha_type is TEGRA_NVVSE_HMAC_SHA_VERIFY.
	 * Result values are:
	 * - '0' indicates HMAC-SHA verification success.
	 * - Non-zero value indicates HMAC-SHA verification failure.
	 */
	uint8_t  result;
};
#define NVVSE_IOCTL_CMDID_HMAC_SHA_SIGN_VERIFY _IOWR(NVVSE_IOC_MAGIC, \
						NVVSE_CMDID_HMAC_SHA_SIGN_VERIFY, \
						struct nvvse_hmac_sha_sv_ctl)

/**
 * \brief Holds Map Membuf request parameters
 */
struct nvvse_map_membuf_ctl {
	/** [in] Holds File descriptor ID
	 * Needs to be non-negative/non-zero value.
	 */
	int32_t fd;
	/** [out] Holds IOVA corresponding to mapped memory buffer */
	uint64_t iova;
};
#define NVVSE_IOCTL_CMDID_MAP_MEMBUF _IOWR(NVVSE_IOC_MAGIC, \
					NVVSE_CMDID_MAP_MEMBUF, \
					struct nvvse_map_membuf_ctl)

/**
 * \brief Holds the Unmap Membuf request parameters
 */
struct nvvse_unmap_membuf_ctl {
	/** [in] Holds File descriptor ID
	 * Needs to be a value greater than 0.
	 */
	int32_t fd;
};
#define NVVSE_IOCTL_CMDID_UNMAP_MEMBUF _IOWR(NVVSE_IOC_MAGIC, \
					NVVSE_CMDID_UNMAP_MEMBUF, \
					struct nvvse_unmap_membuf_ctl)

struct nvvse_get_ivc_entry_t {
	/** Holds ivc queue number
	 *  This is extracted by reading (IVCNumber) property field from DT.
	 */
	uint32_t  se_comm_id;
	/** Holds engine domain number*/
	uint32_t se_engine_domain;
	/** Domain Instance ID of the selected virtual SE engine*/
	uint32_t se_engine_domain_instanceId;
	/** Port number of the selected virtual SE engine*/
	uint32_t se_port;
	/** Virtualized Instance ID of the selected virtual SE engine*/
	uint32_t virtualized_instanceId;
	/** Phandle for SMMU instance - used for cases of Zero copy by Client App */
	/** Holds stream id
	 *  This is extracted by reading <<DT_VSE_ENGINE_ID,DT_VSE_ENGINE_ID>> property field
	 *  from DT. The allowed range for stream ID is 0x00 to 0x7Eu
	 */
	uint32_t stream_id;
	/** Field to provide whether its low or high priority channel - To be consumed by
	 *  PKCS11 Lib
	 */
	uint32_t  priority;
	/** Refers to map buffer size that driver allocates buffers for non-zero copy.
	 * This also serves as identifier for Zero copy channels if the value is 0 for this field.
	 */
	uint32_t  mapped_buffer_size;
	/** Holds group id
	 *  This is extracted by reading <<DT_VSE_GID,DT_VSE_GID>> property field from DT.
	 *  The valid value is any value
	 */
	uint32_t  gid;
	/** Holds the pointer to the mmu handle */
	uint32_t phandle;
	/** Holds GCM dec Support flag */
	uint32_t gcm_dec_supported;
	/** Holds the size of the soc_params array */
	uint32_t soc_params_size;
	/** Holds the array of soc_params */
	uint32_t soc_params[MAX_NUMBER_SOC_PARAMS];
};

/**
 * brief Holds IVC databse
 */
struct nvvse_get_ivc_db {
	/* Holds max number of entries present in DT IVC CFG.
	 * This number can never be more than  MAX_IVC_SUPPORTED.
	 */
	uint32_t max_num_ivc;
	/** Holds array of IVC entries that refer to IVC CFG entries in DT */
	struct nvvse_get_ivc_entry_t ivc_entry[MAX_NUMBER_MISC_DEVICES];
};
#define NVVSE_IOCTL_CMDID_GET_IVC_DB _IOW(NVVSE_IOC_MAGIC, NVVSE_CMDID_GET_IVC_DB, \
						struct nvvse_get_ivc_db)

#endif

