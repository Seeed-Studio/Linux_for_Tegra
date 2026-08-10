/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <linux/interrupt.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#include <soc/tegra/bpmp.h>
#include <soc/tegra/ivc.h>
#include <soc/tegra/virt/tegra_hv_ivc.h>
#include "bpmp-private.h"

/* utilizing the struct tegra_ivc *ivc in struct tegra_bpmp_channel
 * to store struct tegra_hv_ivc_cookie pointer, being used by this driver
 * to use hypervisor exposed ivc queue implementation, so that we do not
 * need to introduced new member in struct tegra_bpmp_channel which
 * will be unused in upstream kernel
 */
#define to_hv_ivc(ivc) ((struct tegra_hv_ivc_cookie *)ivc)

/**
 * struct tegra_bpmp_hv_priv - per-instance hypervisor BPMP private data
 * @ivc_cookies: Array of IVC cookie pointers for all channels
 * @of_node: Device tree node for hypervisor IVC configuration
 * @num_ivc_queues: Number of IVC queues allocated
 */
struct tegra_bpmp_hv_priv {
	struct tegra_hv_ivc_cookie **ivc_cookies;
	struct device_node *of_node;
	unsigned int num_ivc_queues;
};

static inline struct tegra_bpmp_hv_priv *bpmp_to_hv_priv(struct tegra_bpmp *bpmp)
{
	return (struct tegra_bpmp_hv_priv *)bpmp->priv;
}

static irqreturn_t tegra194_hv_bpmp_rx_handler(int irq, void *bpmp)
{
	tegra_bpmp_handle_rx(bpmp);
	return IRQ_HANDLED;
}

static int tegra194_hv_bpmp_channel_init(struct tegra_bpmp_channel *channel,
				      struct tegra_bpmp *bpmp,
				      unsigned int queue_id, bool threaded,
				      unsigned int cookie_idx)
{
	struct tegra_bpmp_hv_priv *priv = bpmp_to_hv_priv(bpmp);
	struct tegra_hv_ivc_cookie *hv_ivc;
	int err;

	hv_ivc = tegra_hv_ivc_reserve(priv->of_node, queue_id, NULL);
	channel->ivc = (void *)hv_ivc;

	if (IS_ERR_OR_NULL(hv_ivc)) {
		pr_err("%s: Failed to reserve ivc queue @index %d\n",
				__func__, queue_id);
		goto request_cleanup;
	}

	if (hv_ivc->frame_size < MSG_DATA_MIN_SZ) {
		pr_err("%s: Frame size is too small\n", __func__);
		goto request_cleanup;
	}

	priv->ivc_cookies[cookie_idx] = hv_ivc;

	/* init completion */
	init_completion(&channel->completion);
	channel->bpmp = bpmp;

	if (threaded) {
		err = request_threaded_irq(
				hv_ivc->irq,
				tegra194_hv_bpmp_rx_handler, NULL,
				IRQF_NO_SUSPEND,
				"bpmp_irq_handler", bpmp);
	} else {
		err = 0;
	}

	if (err) {
		pr_err("%s: Failed to request irq %d for index %d\n",
				__func__, hv_ivc->irq, queue_id);
		goto request_cleanup;
	}

	return 0;

request_cleanup:
	return -1;
}

static bool tegra194_bpmp_hv_is_message_ready(struct tegra_bpmp_channel *channel)
{
	struct tegra_hv_ivc_cookie *hv_ivc = to_hv_ivc(channel->ivc);
	void *frame;

	frame = tegra_hv_ivc_read_get_next_frame(hv_ivc);
	if (IS_ERR(frame)) {
		iosys_map_clear(&channel->ib);
		return false;
	}
	iosys_map_set_vaddr(&channel->ib, frame);

	return true;
}

static int tegra194_bpmp_hv_ack_message(struct tegra_bpmp_channel *channel)
{
	struct tegra_hv_ivc_cookie *hv_ivc = to_hv_ivc(channel->ivc);

	return tegra_hv_ivc_read_advance(hv_ivc);
}

static bool tegra194_hv_bpmp_is_channel_free(struct tegra_bpmp_channel *channel)
{
	struct tegra_hv_ivc_cookie *hv_ivc = to_hv_ivc(channel->ivc);
	void *frame;

	frame = tegra_hv_ivc_write_get_next_frame(hv_ivc);
	if (IS_ERR(frame)) {
		iosys_map_clear(&channel->ob);
		return false;
	}
	iosys_map_set_vaddr(&channel->ob, frame);

	return true;
}

static int tegra194_hv_bpmp_post_message(struct tegra_bpmp_channel *channel)
{
	struct tegra_hv_ivc_cookie *hv_ivc = to_hv_ivc(channel->ivc);

	return tegra_hv_ivc_write_advance(hv_ivc);
}

static void tegra194_hv_bpmp_channel_reset(struct tegra_bpmp_channel *channel)
{
	struct tegra_hv_ivc_cookie *hv_ivc = to_hv_ivc(channel->ivc);

	/* reset the channel state */
	tegra_hv_ivc_channel_reset(hv_ivc);

	/* sync the channel state with BPMP */
	while (tegra_hv_ivc_channel_notified(hv_ivc));
}

static int tegra194_hv_bpmp_resume(struct tegra_bpmp *bpmp)
{
	unsigned int i;

	/* reset message channels */
	tegra194_hv_bpmp_channel_reset(bpmp->tx_channel);

	for (i = 0; i < bpmp->threaded.count; i++)
		tegra194_hv_bpmp_channel_reset(&bpmp->threaded_channels[i]);

	return 0;
}

static int tegra194_hv_ivc_notify(struct tegra_bpmp *bpmp)
{
	struct tegra_hv_ivc_cookie *hv_ivc = to_hv_ivc(bpmp->tx_channel->ivc);

	tegra_hv_ivc_notify(hv_ivc);
	return 0;
}

static int tegra194_hv_bpmp_init(struct tegra_bpmp *bpmp)
{
	struct device_node *of_node = bpmp->dev->of_node;
	struct tegra_bpmp_hv_priv *priv;
	int err, index, cookie_idx = 0;
	uint32_t first_ivc_queue, num_ivc_queues;

	/* Allocate per-instance private data */
	priv = devm_kzalloc(bpmp->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		pr_err("%s: Failed to allocate private data\n", __func__);
		return -ENOMEM;
	}
	bpmp->priv = priv;

	/* get starting ivc queue id & ivc queue count */
	priv->of_node = of_parse_phandle(of_node, "ivc_queue", 0);
	if (!priv->of_node) {
		pr_err("%s: Unable to find hypervisor node\n", __func__);
		return -EINVAL;
	}

	err = of_property_read_u32_index(of_node, "ivc_queue", 1,
			&first_ivc_queue);
	if (err) {
		pr_err("%s: Failed to read start IVC queue\n",
				__func__);
		of_node_put(priv->of_node);
		return err;
	}

	err = of_property_read_u32_index(of_node, "ivc_queue", 2,
			&num_ivc_queues);
	if (err) {
		pr_err("%s: Failed to read range of IVC queues\n",
				__func__);
		of_node_put(priv->of_node);
		return err;
	}

	/* verify total queue count meets expectations */
	if (num_ivc_queues < (bpmp->soc->channels.thread.count +
			bpmp->soc->channels.cpu_tx.count + bpmp->soc->channels.cpu_rx.count)) {
		pr_err("%s: no of ivc queues in DT < required no of channels\n", __func__);
		of_node_put(priv->of_node);
		return -EINVAL;
	}

	priv->num_ivc_queues = num_ivc_queues;
	priv->ivc_cookies = devm_kcalloc(bpmp->dev, num_ivc_queues,
					 sizeof(*priv->ivc_cookies), GFP_KERNEL);

	if (!priv->ivc_cookies) {
		pr_err("%s: Failed to allocate memory\n", __func__);
		of_node_put(priv->of_node);
		return -ENOMEM;
	}

	err = tegra194_hv_bpmp_channel_init(bpmp->tx_channel, bpmp,
					bpmp->soc->channels.cpu_tx.offset + first_ivc_queue,
					false, cookie_idx++);
	if (err < 0) {
		pr_err("%s: Failed initialize tx channel\n", __func__);
		goto cleanup;
	}

	for (index = 0; index < bpmp->threaded.count; index++) {
		unsigned int idx = bpmp->soc->channels.thread.offset + index;

		err = tegra194_hv_bpmp_channel_init(&bpmp->threaded_channels[index],
						    bpmp, idx + first_ivc_queue,
						    true, cookie_idx++);
		if (err < 0) {
			pr_err("%s: Failed initialize tx channel\n", __func__);
			goto cleanup;
		}
	}

	tegra194_hv_bpmp_resume(bpmp);

	return 0;

cleanup:
	for (index = 0; index < num_ivc_queues; index++) {
		if (priv->ivc_cookies[index]) {
			tegra_hv_ivc_unreserve(priv->ivc_cookies[index]);
			priv->ivc_cookies[index] = NULL;
		}
	}
	of_node_put(priv->of_node);

	return err;
}

static void tegra194_hv_bpmp_channel_cleanup(struct tegra_bpmp_channel *channel)
{
	struct tegra_hv_ivc_cookie *hv_ivc = to_hv_ivc(channel->ivc);

	free_irq(hv_ivc->irq, &hv_ivc);
	tegra_hv_ivc_unreserve(hv_ivc);
	kfree(hv_ivc);
}

static void tegra194_hv_bpmp_deinit(struct tegra_bpmp *bpmp)
{
	struct tegra_bpmp_hv_priv *priv = bpmp_to_hv_priv(bpmp);
	unsigned int i;

	if (!priv)
		return;

	tegra194_hv_bpmp_channel_cleanup(bpmp->tx_channel);

	for (i = 0; i < bpmp->threaded.count; i++)
		tegra194_hv_bpmp_channel_cleanup(&bpmp->threaded_channels[i]);

	/* Release device tree node reference */
	if (priv->of_node)
		of_node_put(priv->of_node);

	/* Note: priv->ivc_cookies and priv itself are devm allocated,
	 * so they will be automatically freed when device is removed
	 */
}

const struct tegra_bpmp_ops tegra194_bpmp_hv_ops = {
	.init = tegra194_hv_bpmp_init,
	.deinit = tegra194_hv_bpmp_deinit,
	.is_response_ready = tegra194_bpmp_hv_is_message_ready,
	.is_request_ready = tegra194_bpmp_hv_is_message_ready,
	.ack_response = tegra194_bpmp_hv_ack_message,
	.ack_request = tegra194_bpmp_hv_ack_message,
	.is_response_channel_free = tegra194_hv_bpmp_is_channel_free,
	.is_request_channel_free = tegra194_hv_bpmp_is_channel_free,
	.post_response = tegra194_hv_bpmp_post_message,
	.post_request = tegra194_hv_bpmp_post_message,
	.ring_doorbell = tegra194_hv_ivc_notify,
	.resume = tegra194_hv_bpmp_resume,
};
