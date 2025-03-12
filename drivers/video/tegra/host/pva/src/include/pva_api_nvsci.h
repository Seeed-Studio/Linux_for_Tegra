/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_API_NVSCI_H
#define PVA_API_NVSCI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "pva_api_types.h"
#include "nvscibuf.h"
#include "nvscisync.h"

/**
 * @brief Fill NvSciBuf attributes required by PVA.
 *
 * @param[out] scibuf_attr The NvSciBuf attribute list to be filled with PVA-specific attributes.
 */
enum pva_error pva_nvsci_buf_fill_attrs(NvSciBufAttrList scibuf_attr);

/**
 * @brief Fill NvSciSync attributes required by PVA.
 *
 * @param[in] access_mode Access mode for the sync object, determining how PVA
 *                        will interact with the sync object (read, write, etc.)
 * @param[out] attr_list The NvSciSync attribute list to be populated with attributes.
 */
enum pva_error pva_nvsci_sync_fill_attrs(uint32_t access_mode,
					 NvSciSyncAttrList attr_list);

/**
 * @brief Holds the metadata for a NvSci plane.
 */
struct pva_plane_attrs {
	uint32_t line_pitch;
	uint32_t width_in_bytes;
	uint32_t height;
	uint64_t offset;
};

#define PVA_SURFACE_ATTRS_MAX_NUM_PLANES 6U

/**
 * @brief Holds the metadata for a NvSci surface.
 */
struct pva_surface_attrs {
	bool is_surface;
	enum pva_surface_format format;
	uint32_t n_planes;
	uint64_t size;
	struct pva_plane_attrs planes[PVA_SURFACE_ATTRS_MAX_NUM_PLANES];
	uint8_t log2_gobs_per_block_y[PVA_SURFACE_ATTRS_MAX_NUM_PLANES];
};

/**
 * @brief Import an NvSciBuf object into PVA.
 *
 * This function imports an NvSciBuf buffer object into PVA for further
 * operations. It creates a PVA memory object representing the buffer and
 * retrieves surface information about the buffer.
 *
 * The caller is responsible for freeing the PVA memory object.
 *
 * @param[in] obj The NvSciBuf object to be imported.
 * @param[in] access_mode Access mode for the buffer, determining the PVA's permissions for interaction.
 * @param[out] out_obj A pointer to the PVA memory object representing the imported buffer.
 * @param[out] out_surf_info Surface metadata of the buffer
 */
enum pva_error pva_nvsci_buf_import(NvSciBufObj obj, uint32_t access_mode,
				    struct pva_memory **out_obj,
				    struct pva_surface_attrs *out_surf_info);

/**
 * @brief An opaque object representing an imported NvSciSync object.
 */
struct pva_nvsci_syncobj;

/**
 * @brief Describes the attributes of an imported NvSciSync object.
 *
 * This structure contains details about the memory buffers associated with the
 * imported NvSciSync object.
 */
struct pva_nvsci_syncobj_attrs {
	struct pva_memory *
		semaphore_buf; /**< Pointer to the semaphore memory buffer; NULL if syncpoints are used. */
	struct pva_memory *
		timestamp_buf; /**< Pointer to the timestamp memory buffer; NULL if unused. */
	struct pva_memory
		*status_buf; /**< Pointer to the status memory buffer. */
};

/**
 * @brief Import an NvSciSync object into the PVA.
 *
 * This function imports an NvSciSync object into PVA, enabling it to be used
 * for synchronization of operations.
 *
 * @param[in] ctx The PVA context in which the sync object is to be used.
 * @param[in] nvsci_obj The NvSciSync object to be imported.
 * @param[in] access_mode The access mode for the sync object, indicating how PVA will use it.
 * @param[out] out_obj A pointer to the resulting PVA sync object handle.
 */
enum pva_error pva_nvsci_syncobj_import(struct pva_context *ctx,
					NvSciSyncObj nvsci_obj,
					uint32_t access_mode,
					struct pva_nvsci_syncobj **out_obj);

/**
 * @brief Retrieve the attributes of an imported NvSciSync object.
 *
 * This function fills in the provided attribute structure with details from
 * the imported NvSciSync object, including information relevant for semaphores,
 * timestamps, and status.
 *
 * @param[in] syncobj The NvSciSync object whose attributes are to be retrieved.
 * @param[out] out_attrs The structure to be filled with the sync object's attributes.
 */
void pva_nvsci_syncobj_get_attrs(struct pva_nvsci_syncobj const *syncobj,
				 struct pva_nvsci_syncobj_attrs *out_attrs);

/**
 * @brief Free an imported NvSciSync object.
 *
 * This function releases the resources associated with a PVA NvSciSync object,
 * including PVA memory objects for semaphores, timestamps and statuses.
 *
 * @param[in] syncobj The PVA sync object to be freed.
 */
void pva_nvsci_syncobj_free(struct pva_nvsci_syncobj *syncobj);

/**
 * @brief Get the next status slot for a new fence.
 *
 * @param[in] syncobj The imported NvSciSyncObj
 * @param[out] out_status_slot The status slot index for the next fence.
 */
enum pva_error pva_nvsci_syncobj_next_status(struct pva_nvsci_syncobj *syncobj,
					     uint32_t *out_status_slot);

/**
 * @brief Get the next timestamp slot for a new fence.
 *
 * @param[in] syncobj The imported NvSciSyncObj
 * @param[out] out_timestamp_slot The timestamp slot index for the next fence.
 */
enum pva_error
pva_nvsci_syncobj_next_timestamp(struct pva_nvsci_syncobj *syncobj,
				 uint32_t *out_timestamp_slot);

/**
 * @brief Fence data for import and export.
 */
struct pva_nvsci_fence_info {
	uint32_t index; /**< The index of the fence. */
	uint32_t value; /**< The value of the fence. */
	uint32_t status_slot; /**< The slot index for the status. */
	uint32_t timestamp_slot; /**< The slot index for the timestamp. */
};
/**
 * @brief Import a NvSciSync fence into a PVA fence.
 *
 * @param[in] nvsci_fence The NvSciSync fence to be imported.
 * @param[in] pva_syncobj The previously imported NvSciSyncObj that's associated with the fence.
 * @param[out] out_fence_info The information about the NvSci fence. It can be used to fill a pva_fence.
 *
 * @note This function only fills the index and value field of the pva_fence.
 * The user needs to set the semaphore resource ID if the sync object is a
 * semaphore.
 *
 */
enum pva_error
pva_nvsci_fence_import(NvSciSyncFence const *nvsci_fence,
		       struct pva_nvsci_syncobj const *pva_syncobj,
		       struct pva_nvsci_fence_info *out_fence_info);

/**
 * @brief Export a PVA fence into an NvSciSync fence.
 *
 * @param[in] fence_info The information about the fence to be exported.
 * @param[in] syncobj The previously imported NvSciSyncObj that's associated with the fence.
 * @param[out] out_nvsci_fence The resulting NvSciSync fence object.
 */
enum pva_error
pva_nvsci_fence_export(struct pva_nvsci_fence_info const *fence_info,
		       struct pva_nvsci_syncobj const *syncobj,
		       NvSciSyncFence *out_nvsci_fence);

#ifdef __cplusplus
}
#endif

#endif // PVA_API_NVSCI_H
