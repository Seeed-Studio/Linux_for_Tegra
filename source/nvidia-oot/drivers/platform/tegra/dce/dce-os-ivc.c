// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <dce-ipc.h>
#include <dce-os-ivc.h>
#include <interface/dce-ipc-header.h>

/**
 * dce_os_ivc_write_channel - Writes to an ivc channel.
 *
 * @ch : Pointer to the pertinent channel.
 * @data : Pointer to the data to be written.
 * @size : Size of the data to be written.
 *
 * Return : 0 if successful.
 */
int dce_os_ivc_write_channel(struct dce_ipc_channel *ch,
		const void *data, size_t size)
{
	struct dce_ipc_header *hdr;

	/**
	 * Add actual length information to the top
	 * of the IVC frame
	 */

#if defined(NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP) /* Linux v6.2 */
	if ((ch->flags & DCE_IPC_CHANNEL_MSG_HEADER) != 0U) {
		iosys_map_wr_field(&ch->obuff, 0, struct dce_ipc_header, length,
				   size);
		iosys_map_incr(&ch->obuff, sizeof(*hdr));
	}

	if (data && size > 0)
		iosys_map_memcpy_to(&ch->obuff, 0, data, size);
#else
	if ((ch->flags & DCE_IPC_CHANNEL_MSG_HEADER) != 0U) {
		hdr = (struct dce_ipc_header *)ch->obuff;
		hdr->length = (uint32_t)size;
		ch->obuff = (void *)(hdr + 1U);
	}

	if (data && size > 0)
		memcpy(ch->obuff, data, size);
#endif

	return dce_os_ivc_write_advance(&ch->d_ivc);
}

/**
 * dce_os_ivc_read_channel - Writes to an ivc channel.
 *
 * @ch : Pointer to the pertinent channel.
 * @data : Pointer to the data to be read.
 * @size : Size of the data to be read.
 *
 * Return : 0 if successful.
 */
int dce_os_ivc_read_channel(struct dce_ipc_channel *ch,
		void *data, size_t size)
{
	struct dce_ipc_header *hdr;

	/**
	 * Get actual length information from the top
	 * of the IVC frame
	 */
#if defined(NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP) /* Linux v6.2 */
	if ((ch->flags & DCE_IPC_CHANNEL_MSG_HEADER) != 0U) {
		iosys_map_wr_field(&ch->ibuff, 0, struct dce_ipc_header, length,
				   size);
		iosys_map_incr(&ch->ibuff, sizeof(*hdr));
	}

	if (data && size > 0)
		iosys_map_memcpy_from(data, &ch->ibuff, 0, size);
#else
	if ((ch->flags & DCE_IPC_CHANNEL_MSG_HEADER) != 0U) {
		hdr = (struct dce_ipc_header *)ch->ibuff;
		size = (size_t)(hdr->length);
		ch->ibuff = (void *)(hdr + 1U);
	}

	if (data && size > 0)
		memcpy(data, ch->ibuff, size);
#endif

	return dce_os_ivc_read_advance(&ch->d_ivc);
}

#if defined(NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP) /* Linux v6.2 */

int dce_os_ivc_get_next_write_frame(dce_os_ivc_t *ivc, struct iosys_map *ppframe)
{
	int err = 0;
	struct iosys_map pframe;

	err = tegra_ivc_write_get_next_frame(ivc, &pframe);
	if (err) {
		iosys_map_clear(&pframe);
		goto done;
	}

	*ppframe = pframe;

done:
	return err;
}

/**
 * dce_os_ivc_get_next_read_frame - Get next read frame.
 *
 * @ivc : Pointer to IVC struct
 * @ppFrame : Pointer to frame reference.
 *
 * Return : 0 if successful.
 */
int dce_os_ivc_get_next_read_frame(dce_os_ivc_t *ivc, struct iosys_map *ppframe)
{
	int err = 0;
	struct iosys_map pframe;

	err = tegra_ivc_read_get_next_frame(ivc, &pframe);
	if (err) {
		iosys_map_clear(&pframe);
		goto done;
	}

	*ppframe = pframe;

done:
	return err;
}

#else /* NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP */

int dce_os_ivc_get_next_write_frame(dce_os_ivc_t *ivc, void **ppframe)
{
	int err = 0;
	void *pframe = NULL;

	pframe = tegra_ivc_write_get_next_frame(ivc);
	if (IS_ERR(pframe)) {
		err = -ENOMEM;
		goto done;
	}

	*ppframe = pframe;

done:
	return err;
}

int dce_os_ivc_get_next_read_frame(dce_os_ivc_t *ivc, void **ppframe)
{
	int err = 0;
	void *pframe = NULL;

	pframe = tegra_ivc_read_get_next_frame(ivc);
	if (IS_ERR(pframe)) {
		err = -ENOMEM;
		goto done;
	}

	*ppframe = pframe;

done:
	return err;
}

#endif /* NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP */

bool dce_os_ivc_is_data_available(struct dce_ipc_channel *ch)
{
	bool ret = false;
	int err = 0;

#if defined(NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP) /* Linux v6.2 */
	struct iosys_map frame;
#else
	void *frame;
#endif

	err = dce_os_ivc_get_next_read_frame(&ch->d_ivc, &frame);
	if (err == 0)
		ret = true;

	return ret;
}
