// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2017-2024, NVIDIA CORPORATION.  All rights reserved. */

#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include "mods_internal.h"

static struct device *mods_tegra_dma_dev;

#define MODS_DMA_MAX_CHANNEL 32
struct mods_dma_chan_info {
	rwlock_t lock;
	bool    in_use;
	u32     id;
	struct  dma_chan *pch;
	bool    should_notify;
};

static DECLARE_BITMAP(dma_info_mask, MODS_DMA_MAX_CHANNEL);
static struct mods_dma_chan_info dma_info_chan_list[MODS_DMA_MAX_CHANNEL];
static DEFINE_SPINLOCK(dma_info_lock);

static int mods_get_dma_id(u32 *p_id)
{
	u32 id;

	spin_lock(&dma_info_lock);
	id = find_first_zero_bit(dma_info_mask, MODS_DMA_MAX_CHANNEL);
	if (id >= MODS_DMA_MAX_CHANNEL) {
		spin_unlock(&dma_info_lock);
		return -ENOSPC;
	}
	set_bit(id, dma_info_mask);
	spin_unlock(&dma_info_lock);

	*p_id = id;
	return OK;
}

static void mods_release_dma_id(u32 id)
{
	spin_lock(&dma_info_lock);
	clear_bit(id, dma_info_mask);
	spin_unlock(&dma_info_lock);
}

static int mods_get_chan_by_id(u32 id, struct mods_dma_chan_info **p_dma_chan)
{
	if (id >= MODS_DMA_MAX_CHANNEL)
		return -ERANGE;

	*p_dma_chan = &dma_info_chan_list[id];

	return OK;
}

static void mods_release_channel(u32 id)
{
	struct mods_dma_chan_info *p_mods_chan;
	struct dma_chan *pch = NULL;

	if (mods_get_chan_by_id(id, &p_mods_chan) != OK) {
		mods_error_printk("get dma channel failed, id %d\n", id);
		return;
	}

	write_lock(&(p_mods_chan->lock));
	if (p_mods_chan->in_use) {
		pch = p_mods_chan->pch;
		p_mods_chan->pch = NULL;
		mods_release_dma_id(id);
		p_mods_chan->in_use = false;
	}
	write_unlock(&(p_mods_chan->lock));

	/* do not call this when hold spin lock */
	if (pch) {
		dmaengine_terminate_sync(pch);
		dma_release_channel(pch);
	}
}

static bool mods_chan_is_inuse(struct mods_dma_chan_info *p_mods_chan)
{
	bool in_use = false;

	read_lock(&(p_mods_chan->lock));
	if (p_mods_chan->in_use)
		in_use = true;
	read_unlock(&(p_mods_chan->lock));

	return in_use;
}

static int mods_get_inuse_chan_by_handle(struct MODS_DMA_HANDLE *p_handle,
					struct mods_dma_chan_info **p_mods_chan)
{
	int err;
	bool in_use;
	struct mods_dma_chan_info *p_mods_ch;

	err = mods_get_chan_by_id(p_handle->dma_id, &p_mods_ch);
	if (err != OK) {
		mods_error_printk("get dma channel failed, id %d\n",
				p_handle->dma_id);
		return -ENODEV;
	}

	in_use = mods_chan_is_inuse(p_mods_ch);
	if (!in_use) {
		mods_error_printk("invalid dma channel: %d, not in use\n",
				p_handle->dma_id);
		return -EINVAL;
	}
	*p_mods_chan = p_mods_ch;
	return OK;
}

static int mods_dma_sync_wait(struct MODS_DMA_HANDLE *p_handle,
					mods_dma_cookie_t cookie)
{
	int err = OK;
	struct mods_dma_chan_info *p_mods_chan;

	err = mods_get_inuse_chan_by_handle(p_handle, &p_mods_chan);
	if (err != OK)
		return err;

	mods_debug_printk(DEBUG_TEGRADMA,
			"Wait on chan: %p\n", p_mods_chan->pch);
	read_lock(&(p_mods_chan->lock));
	if (dma_sync_wait(p_mods_chan->pch, cookie) != DMA_COMPLETE)
		err = -1;
	read_unlock(&(p_mods_chan->lock));

	return err;
}

static int mods_dma_async_is_tx_complete(struct MODS_DMA_HANDLE *p_handle,
					mods_dma_cookie_t cookie,
					__u32 *p_is_complete)
{
	int err = OK;
	struct mods_dma_chan_info *p_mods_chan;
	enum dma_status status;

	err = mods_get_inuse_chan_by_handle(p_handle, &p_mods_chan);
	if (err != OK)
		return err;

	read_lock(&(p_mods_chan->lock));
	status = dma_async_is_tx_complete(p_mods_chan->pch, cookie, NULL, NULL);
	read_unlock(&(p_mods_chan->lock));

	if (status == DMA_COMPLETE)
		*p_is_complete = true;
	else if (status == DMA_IN_PROGRESS)
		*p_is_complete = false;
	else
		err = -EINVAL;

	return err;
}

int esc_mods_dma_request_channel_2(struct mods_client *client,
				struct MODS_DMA_HANDLE_2 *p_handle)
{
	struct dma_chan *chan = NULL;
	struct mods_dma_chan_info *p_mods_chan = NULL;
	u32 id = MODS_DMA_MAX_CHANNEL;
	int err = -EINVAL;

	LOG_ENT();

	if (p_handle->dma_type >= MODS_DMA_TX_TYPE_END) {
		cl_error("dma type %d not available\n", p_handle->dma_type);
		goto failed;
	}

	err = mods_get_dma_id(&id);
	if (err != OK) {
		cl_error("no dma handle available\n");
		goto failed;
	}

	err = mods_get_chan_by_id(id, &p_mods_chan);
	if (err != OK) {
		cl_error("get dma channel failed\n");
		goto failed;
	}

	read_lock(&(p_mods_chan->lock));
	if (p_mods_chan->in_use) {
		cl_error("mods dma channel in use\n");
		read_unlock(&(p_mods_chan->lock));
		err = -EBUSY;
		goto failed;
	}
	read_unlock(&(p_mods_chan->lock));

	// dma channel is requested in the old-fashion way
	if (p_handle->ctrl_dir[0] == '\0') {
		dma_cap_mask_t mask;

		dma_cap_zero(mask);
		dma_cap_set(p_handle->dma_type, mask);
		chan = dma_request_channel(mask, NULL, NULL);
	} else if (mods_tegra_dma_dev) {
		// get dma chan from dt node
		cl_debug(DEBUG_TEGRADMA, "dmach is asked for %s\n", p_handle->ctrl_dir);
		chan = dma_request_chan(mods_tegra_dma_dev, p_handle->ctrl_dir);
	}
	if (!chan) {
		cl_error("dma channel is not available\n");
		mods_release_dma_id(id);
		err = -EBUSY;
		goto failed;
	}

	write_lock(&(p_mods_chan->lock));
	p_mods_chan->pch = chan;
	p_mods_chan->in_use = true;
	write_unlock(&(p_mods_chan->lock));

	p_handle->dma_id = id;
	cl_debug(DEBUG_TEGRADMA, "request get dma id: %d\n", id);

failed:
	LOG_EXT();
	return err;
}

int esc_mods_dma_request_channel(struct mods_client *client,
				struct MODS_DMA_HANDLE *p_handle)
{
	int err;
	struct MODS_DMA_HANDLE_2 handle = {0};

	handle.ctrl_dir[0] = '\0';
	handle.dma_type = p_handle->dma_type;
	err = esc_mods_dma_request_channel_2(client, &handle);
	if (err == 0)
		p_handle->dma_id = handle.dma_id;

	return err;
}

int esc_mods_dma_release_channel(struct mods_client *client,
				struct MODS_DMA_HANDLE *p_handle)
{
	mods_release_channel(p_handle->dma_id);
	return OK;
}

int esc_mods_dma_set_config(struct mods_client *client,
				struct MODS_DMA_CHANNEL_CONFIG *p_config)

{
	struct dma_slave_config config;
	struct mods_dma_chan_info *p_mods_chan;
	int err;

	LOG_ENT();

	err = mods_get_inuse_chan_by_handle(&p_config->handle, &p_mods_chan);
	if (err != OK) {
		LOG_EXT();
		return err;
	}

	config.direction = p_config->direction;
	config.src_addr = p_config->src_addr;
	config.dst_addr = p_config->dst_addr;
	config.src_addr_width = p_config->src_addr_width;
	config.dst_addr_width = p_config->dst_addr_width;
	config.src_maxburst = p_config->src_maxburst;
	config.dst_maxburst = p_config->dst_maxburst;
	config.device_fc = (p_config->device_fc == 0) ? false : true;

	cl_debug(DEBUG_TEGRADMA,
		"ch: %d dir [%d], addr[%p -> %p], burst [%d %d] width [%d %d]\n",
		p_config->handle.dma_id,
		config.direction,
		(void *)config.src_addr, (void *)config.dst_addr,
		config.src_maxburst, config.dst_maxburst,
		config.src_addr_width, config.dst_addr_width);

	write_lock(&(p_mods_chan->lock));
	err = dmaengine_slave_config(p_mods_chan->pch, &config);
	write_unlock(&(p_mods_chan->lock));

	LOG_EXT();

	return err;
}


int esc_mods_dma_submit_request(struct mods_client *client,
				struct MODS_DMA_TX_DESC *p_mods_desc)
{
	int err = OK;
	struct mods_dma_chan_info *p_mods_chan;
	struct dma_async_tx_descriptor *desc;
	struct dma_device       *dev;
	enum dma_ctrl_flags     flags;
	mods_dma_cookie_t cookie = 0;

	LOG_ENT();
	flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;

	err = mods_get_inuse_chan_by_handle(&p_mods_desc->handle, &p_mods_chan);
	if (err != OK) {
		LOG_EXT();
		return err;
	}

	if (p_mods_desc->mode != MODS_DMA_SINGLE) {
		cl_error("unsupported mode: %d\n", p_mods_desc->mode);
		LOG_EXT();
		return -EINVAL;
	}

	cl_debug(DEBUG_TEGRADMA, "submit on chan %p\n", p_mods_chan->pch);

	write_lock(&(p_mods_chan->lock));
	if (p_mods_desc->data_dir == MODS_DMA_MEM_TO_MEM) {
		dev = p_mods_chan->pch->device;
		desc = dev->device_prep_dma_memcpy(
						p_mods_chan->pch,
						p_mods_desc->phys,
						p_mods_desc->phys_2,
						p_mods_desc->length,
						flags);
	} else {
		cl_debug(DEBUG_TEGRADMA,
			"Phys Addr [%p], len [%d], dir [%d]\n",
			(void *)p_mods_desc->phys,
			p_mods_desc->length,
			p_mods_desc->data_dir);
		desc = dmaengine_prep_slave_single(p_mods_chan->pch,
							p_mods_desc->phys,
							p_mods_desc->length,
							p_mods_desc->data_dir,
							flags);
	}

	if (desc == NULL) {
		cl_error("unable to get desc for Tx\n");
		err = -EIO;
		goto failed;
	}

	desc->callback = NULL;
	desc->callback_param = NULL;
	cookie = dmaengine_submit(desc);

failed:
	write_unlock(&(p_mods_chan->lock));
	if (dma_submit_error(cookie)) {
		cl_error("submit cookie: %x\n", cookie);
		LOG_EXT();
		return -EIO;
	}

	p_mods_desc->cookie = cookie;

	LOG_EXT();
	return err;
}

int esc_mods_dma_async_issue_pending(struct mods_client *client,
					struct MODS_DMA_HANDLE *p_handle)
{
	int err = OK;
	struct mods_dma_chan_info *p_mods_chan;

	LOG_ENT();
	err = mods_get_inuse_chan_by_handle(p_handle, &p_mods_chan);
	if (err != OK) {
		LOG_EXT();
		return err;
	}

	cl_debug(DEBUG_TEGRADMA, "issue pending on chan: %p\n",
										p_mods_chan->pch);
	read_lock(&(p_mods_chan->lock));
	dma_async_issue_pending(p_mods_chan->pch);
	read_unlock(&(p_mods_chan->lock));
	LOG_EXT();

	return err;
}

int esc_mods_dma_wait(struct mods_client *client,
				struct MODS_DMA_WAIT_DESC *p_wait_desc)
{
	int err;

	LOG_ENT();
	if (p_wait_desc->type == MODS_DMA_SYNC_WAIT)
		err = mods_dma_sync_wait(&p_wait_desc->handle,
							p_wait_desc->cookie);
	else if (p_wait_desc->type == MODS_DMA_ASYNC_WAIT)
		err = mods_dma_async_is_tx_complete(&p_wait_desc->handle,
							p_wait_desc->cookie,
							&p_wait_desc->tx_complete);
	else
		err = -EINVAL;

	LOG_EXT();
	return err;
}

int esc_mods_dma_alloc_coherent(struct mods_client *client,
				struct MODS_DMA_COHERENT_MEM_HANDLE *p)
{
	dma_addr_t    p_phys_addr;
	void          *p_cpu_addr;

	LOG_ENT();

	p_cpu_addr = dma_alloc_coherent(NULL,
					p->num_bytes,
					&p_phys_addr,
					GFP_KERNEL);

	cl_debug(DEBUG_MEM,
		"num_bytes=%d, p_cpu_addr=%p, p_phys_addr=%p\n",
		p->num_bytes,
		(void *)p_cpu_addr,
		(void *)p_phys_addr);

	if (!p_cpu_addr) {
		cl_error(
			"FAILED!!!num_bytes=%d, p_cpu_addr=%p, p_phys_addr=%p\n",
			p->num_bytes,
			(void *)p_cpu_addr,
			(void *)p_phys_addr);
		LOG_EXT();
		return -1;
	}

	memset(p_cpu_addr, 0x00, p->num_bytes);

	p->memory_handle_phys = (u64)p_phys_addr;
	p->memory_handle_virt = (u64)p_cpu_addr;

	LOG_EXT();
	return 0;
}

int esc_mods_dma_free_coherent(struct mods_client *client,
					struct MODS_DMA_COHERENT_MEM_HANDLE *p)
{
	LOG_ENT();

	cl_debug(DEBUG_MEM,
		"num_bytes = %d, p_cpu_addr=%p, p_phys_addr=%p\n",
		p->num_bytes,
		(void *)(p->memory_handle_virt),
		(void *)(p->memory_handle_phys));

	dma_free_coherent(NULL,
		p->num_bytes,
		(void *)(p->memory_handle_virt),
		(dma_addr_t)(p->memory_handle_phys));

	p->memory_handle_phys = (u64)0;
	p->memory_handle_virt = (u64)0;

	LOG_EXT();
	return 0;
}

int esc_mods_dma_copy_to_user(struct mods_client *client,
				struct MODS_DMA_COPY_TO_USER *p)
{
	int retval;

	LOG_ENT();

	cl_debug(DEBUG_MEM,
		"memory_handle_dst=%p, memory_handle_src=%p, num_bytes=%d\n",
		(void *)(p->memory_handle_dst),
		(void *)(p->memory_handle_src),
		p->num_bytes);

	retval = copy_to_user((void __user *)p->memory_handle_dst,
					(void *)p->memory_handle_src,
					p->num_bytes);

	LOG_EXT();

	return retval;
}

static int tegra_dma_driver_probe(struct platform_device *pdev)
{
	LOG_ENT();

	mods_debug_printk(DEBUG_TEGRADMA, "mods_tegra_dma probe\n");
	mods_tegra_dma_dev = get_device(&pdev->dev);

	LOG_EXT();
	return 0;
}

static int tegra_dma_driver_remove(struct platform_device *pdev)
{
	put_device(&pdev->dev);
	mods_tegra_dma_dev = NULL;
	return 0;
}

static const struct of_device_id of_ids[] = {
	{ .compatible = "nvidia,mods_tegra_dma" },
	{ }
};

static struct platform_driver mods_tegra_dma_driver = {
	.probe  = tegra_dma_driver_probe,
	.remove = tegra_dma_driver_remove,
	.driver = {
		.name   = "mods_tegra_dma",
		.owner  = THIS_MODULE,
		.of_match_table = of_ids,
	},
};

int mods_init_dma(void)
{
	int i;
	int err;

	for (i = 0; i < MODS_DMA_MAX_CHANNEL; i++) {
		struct mods_dma_chan_info *p_chan_info;

		p_chan_info = &dma_info_chan_list[i];
		rwlock_init(&(p_chan_info->lock));
		p_chan_info->in_use = false;
	}

	err = platform_driver_register(&mods_tegra_dma_driver);
	if (err < 0)
		mods_error_printk("register mods dma driver failed\n");

	return err;
}

void mods_exit_dma(void)
{
	int i;

	for (i = 0; i < MODS_DMA_MAX_CHANNEL; i++)
		mods_release_channel(i);

	platform_driver_unregister(&mods_tegra_dma_driver);
}
