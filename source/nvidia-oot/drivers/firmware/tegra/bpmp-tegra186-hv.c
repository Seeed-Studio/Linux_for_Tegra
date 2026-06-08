// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <nvidia/conftest.h>

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <soc/tegra/virt/hv-ivc.h>
#include <soc/tegra/bpmp.h>
#include <soc/tegra/fuse.h>
#include <linux/of_platform.h>

#include "bpmp-tegra186-hv.h"

#define MSG_RING	BIT(1)
#define TAG_SZ		32
#define MAX_POSSIBLE_RX_CHANNEL 1
#define TX_CHANNEL_EXACT_COUNT  1

static struct tegra186_hv_bpmp {
	struct tegra_bpmp *parent;
} tegra186_hv_bpmp;

/* utilizing the struct tegra_ivc *ivc in struct tegra_bpmp_channel
 * to store struct tegra_hv_ivc_cookie pointer, being used by this driver
 * to use hypervisor exposed ivc queue implementation, so that we do not
 * need to introduced new member in struct tegra_bpmp_channel which
 * will be unused in upstream kernel*/
#define to_hv_ivc(ivc) (struct tegra_hv_ivc_cookie *)ivc

static struct tegra_hv_ivc_cookie **hv_bpmp_ivc_cookies;
static struct device_node *hv_of_node;

static inline const struct tegra_bpmp_ops *
channel_to_ops(struct tegra_bpmp_channel *channel)
{
	struct tegra_bpmp *bpmp = channel->bpmp;

	return bpmp->soc->ops;
}

static struct tegra_bpmp_mrq *tegra_bpmp_find_mrq(struct tegra_bpmp *bpmp,
						  unsigned int mrq)
{
	struct tegra_bpmp_mrq *entry;

	list_for_each_entry(entry, &bpmp->mrqs, list)
		if (entry->mrq == mrq)
			return entry;

	return NULL;
}

static void tegra_bpmp_handle_mrq(struct tegra_bpmp *bpmp,
				  unsigned int mrq,
				  struct tegra_bpmp_channel *channel)
{
	struct tegra_bpmp_mrq *entry;
	u32 zero = 0;

	spin_lock(&bpmp->lock);

	entry = tegra_bpmp_find_mrq(bpmp, mrq);
	if (!entry) {
		spin_unlock(&bpmp->lock);
		tegra_bpmp_mrq_return(channel, -EINVAL, &zero, sizeof(zero));
		return;
	}

	entry->handler(mrq, channel, entry->data);

	spin_unlock(&bpmp->lock);
}

static void tegra_bpmp_channel_signal(struct tegra_bpmp_channel *channel)
{
#if defined(NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP)
	unsigned long flags = tegra_bpmp_mb_read_field(&channel->ob, flags);
#else
	unsigned long flags = channel->ob->flags;
#endif

	if ((flags & MSG_RING) == 0)
		return;

	complete(&channel->completion);
}

static bool tegra_bpmp_is_response_ready(struct tegra_bpmp_channel *channel)
{
	const struct tegra_bpmp_ops *ops = channel_to_ops(channel);

	return ops->is_response_ready(channel);
}

static bool tegra_bpmp_is_request_ready(struct tegra_bpmp_channel *channel)
{
	const struct tegra_bpmp_ops *ops = channel_to_ops(channel);

	return ops->is_request_ready(channel);
}

void tegra_bpmp_handle_rx(struct tegra_bpmp *bpmp)
{
	struct tegra_bpmp_channel *channel;
	unsigned int i, count;
	unsigned long *busy;

	channel = bpmp->rx_channel;
	count = bpmp->soc->channels.thread.count;
	busy = bpmp->threaded.busy;

	/* If supported incoming channel */
	if (bpmp->soc->channels.cpu_rx.count == MAX_POSSIBLE_RX_CHANNEL) {
#if defined(NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP)
		if (tegra_bpmp_is_request_ready(channel)) {
			unsigned int mrq = tegra_bpmp_mb_read_field(&channel->ib, code);
			tegra_bpmp_handle_mrq(bpmp, mrq, channel);
		}
#else
		if (tegra_bpmp_is_request_ready(channel))
			tegra_bpmp_handle_mrq(bpmp, channel->ib->code, channel);
#endif
        }

	spin_lock(&bpmp->lock);

	for_each_set_bit(i, busy, count) {
		struct tegra_bpmp_channel *channel;

		channel = &bpmp->threaded_channels[i];

		if (tegra_bpmp_is_response_ready(channel)) {
			tegra_bpmp_channel_signal(channel);
			clear_bit(i, busy);
		}
	}

	spin_unlock(&bpmp->lock);
}

static irqreturn_t tegra186_hv_bpmp_rx_handler(int irq, void *ivck)
{
	tegra_bpmp_handle_rx(tegra186_hv_bpmp.parent);
	return IRQ_HANDLED;
}

static int tegra186_hv_bpmp_channel_init(struct tegra_bpmp_channel *channel,
				      struct tegra_bpmp *bpmp,
				      unsigned int queue_id, bool threaded)
{
	static int cookie_idx;
	struct tegra_hv_ivc_cookie *hv_ivc;
	int err;

	hv_ivc = tegra_hv_ivc_reserve(hv_of_node, queue_id, NULL);
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

	hv_bpmp_ivc_cookies[cookie_idx++] = hv_ivc;

	/* init completion */
	init_completion(&channel->completion);
	channel->bpmp = bpmp;

	if (threaded) {
		err = request_threaded_irq(
				hv_ivc->irq,
				tegra186_hv_bpmp_rx_handler, NULL,
				IRQF_NO_SUSPEND,
				"bpmp_irq_handler", &hv_ivc);
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

static bool tegra186_bpmp_hv_is_message_ready(struct tegra_bpmp_channel *channel)
{
	struct tegra_hv_ivc_cookie *hv_ivc = to_hv_ivc(channel->ivc);
	void *frame;

	frame = tegra_hv_ivc_read_get_next_frame(hv_ivc);
#if defined(NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP)
	if (IS_ERR(frame)) {
		iosys_map_clear(&channel->ib);
		return false;
	}
	iosys_map_set_vaddr(&channel->ib, frame);
#else
	if (IS_ERR(frame)) {
		channel->ib = NULL;
		return false;
	}

	channel->ib = frame;
#endif

	return true;
}

static int tegra186_bpmp_hv_ack_message(struct tegra_bpmp_channel *channel)
{
	struct tegra_hv_ivc_cookie *hv_ivc = to_hv_ivc(channel->ivc);

	return tegra_hv_ivc_read_advance(hv_ivc);
}

static bool tegra186_hv_bpmp_is_channel_free(struct tegra_bpmp_channel *channel)
{
	struct tegra_hv_ivc_cookie *hv_ivc = to_hv_ivc(channel->ivc);
	void *frame;

	frame = tegra_hv_ivc_write_get_next_frame(hv_ivc);
#if defined(NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP)
	if (IS_ERR(frame)) {
		iosys_map_clear(&channel->ob);
		return false;
	}
	iosys_map_set_vaddr(&channel->ob, frame);
#else
	if (IS_ERR(frame)) {
		channel->ob = NULL;
		return false;
	}

	channel->ob = frame;
#endif

	return true;
}

static int tegra186_hv_bpmp_post_message(struct tegra_bpmp_channel *channel)
{
	struct tegra_hv_ivc_cookie *hv_ivc = to_hv_ivc(channel->ivc);

	return tegra_hv_ivc_write_advance(hv_ivc);
}

static void tegra186_hv_bpmp_channel_reset(struct tegra_bpmp_channel *channel)
{
	struct tegra_hv_ivc_cookie *hv_ivc = to_hv_ivc(channel->ivc);

	/* reset the channel state */
	tegra_hv_ivc_channel_reset(hv_ivc);

	/* sync the channel state with BPMP */
	while (tegra_hv_ivc_channel_notified(hv_ivc))
		;
}

static int tegra186_hv_bpmp_resume(struct tegra_bpmp *bpmp)
{
	unsigned int i;

	/* reset message channels */
	if (bpmp->soc->channels.cpu_tx.count == TX_CHANNEL_EXACT_COUNT) {
		tegra186_hv_bpmp_channel_reset(bpmp->tx_channel);
	} else {
		pr_err("%s: Error: driver should have single tx channel mandatory\n", __func__);
		return -1;
	}

	if (bpmp->soc->channels.cpu_rx.count == MAX_POSSIBLE_RX_CHANNEL)
		tegra186_hv_bpmp_channel_reset(bpmp->rx_channel);

	for (i = 0; i < bpmp->threaded.count; i++)
		tegra186_hv_bpmp_channel_reset(&bpmp->threaded_channels[i]);

	return 0;
}

static int tegra186_hv_ivc_notify(struct tegra_bpmp *bpmp)
{
	struct tegra_hv_ivc_cookie *hv_ivc = to_hv_ivc(bpmp->tx_channel->ivc);

	tegra_hv_ivc_notify(hv_ivc);
	return 0;
}

static int tegra186_hv_bpmp_init(struct tegra_bpmp *bpmp)
{
	struct tegra186_hv_bpmp *priv;
	struct device_node *of_node = bpmp->dev->of_node;
	int err, index;
	uint32_t first_ivc_queue, num_ivc_queues;

	priv = devm_kzalloc(bpmp->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	bpmp->priv = priv;
	priv->parent = bpmp;
	tegra186_hv_bpmp.parent = bpmp;

	/* get starting ivc queue id & ivc queue count */
	hv_of_node = of_parse_phandle(of_node, "ivc_queue", 0);
	if (!hv_of_node) {
		pr_err("%s: Unable to find hypervisor node\n", __func__);
		return -EINVAL;
	}

	err = of_property_read_u32_index(of_node, "ivc_queue", 1,
			&first_ivc_queue);
	if (err != 0) {
		pr_err("%s: Failed to read start IVC queue\n",
				__func__);
		of_node_put(hv_of_node);
		return -EINVAL;
	}

	err = of_property_read_u32_index(of_node, "ivc_queue", 2,
			&num_ivc_queues);
	if (err != 0) {
		pr_err("%s: Failed to read range of IVC queues\n",
				__func__);
		of_node_put(hv_of_node);
		return -EINVAL;
	}

	/* verify total queue count meets expectations */
	if (num_ivc_queues < (bpmp->soc->channels.thread.count +
			bpmp->soc->channels.cpu_tx.count + bpmp->soc->channels.cpu_rx.count)) {
		pr_err("%s: no of ivc queues in DT < required no of channels \n",
				__func__);
		of_node_put(hv_of_node);
		return -EINVAL;
	}

	hv_bpmp_ivc_cookies = kzalloc(sizeof(struct tegra_hv_ivc_cookie *) *
					num_ivc_queues, GFP_KERNEL);

	if (!hv_bpmp_ivc_cookies) {
		pr_err("%s: Failed to allocate memory\n", __func__);
		of_node_put(hv_of_node);
		return -ENOMEM;
	}

	/* init tx channel */
	if (bpmp->soc->channels.cpu_tx.count == TX_CHANNEL_EXACT_COUNT) {
		err = tegra186_hv_bpmp_channel_init(bpmp->tx_channel, bpmp,
					 bpmp->soc->channels.cpu_tx.offset + first_ivc_queue, false);
		if (err < 0) {
			pr_err("%s: Failed initialize tx channel\n", __func__);
			goto cleanup;
		}
	} else {
		pr_err("%s: Error: driver should have single tx channel mandatory\n", __func__);
		goto cleanup;
	}

	/* init rx channel */
	if (bpmp->soc->channels.cpu_rx.count == MAX_POSSIBLE_RX_CHANNEL) {
		err = tegra186_hv_bpmp_channel_init(bpmp->rx_channel, bpmp,
					 bpmp->soc->channels.cpu_rx.offset + first_ivc_queue, true);
		if (err < 0) {
			pr_err("%s: Failed initialize rx channel\n", __func__);
			goto cleanup;
		}
	}

	for (index = 0; index < bpmp->threaded.count; index++) {
		unsigned int idx = bpmp->soc->channels.thread.offset + index;

		err = tegra186_hv_bpmp_channel_init(&bpmp->threaded_channels[index],
								 bpmp, idx + first_ivc_queue, true);
		if (err < 0) {
			pr_err("%s: Failed initialize tx channel\n", __func__);
			goto cleanup;
		}
	}

	tegra186_hv_bpmp_resume(bpmp);
	of_node_put(hv_of_node);

	return 0;

cleanup:
	for (index = 0; index < num_ivc_queues; index++) {
		if (hv_bpmp_ivc_cookies[index]) {
			tegra_hv_ivc_unreserve(
					hv_bpmp_ivc_cookies[index]);
			hv_bpmp_ivc_cookies[index] = NULL;
		}
	}
	kfree(hv_bpmp_ivc_cookies);
	of_node_put(hv_of_node);

	return -ENOMEM;
}

static void tegra186_hv_bpmp_channel_cleanup(struct tegra_bpmp_channel *channel)
{
	struct tegra_hv_ivc_cookie *hv_ivc = to_hv_ivc(channel->ivc);

	free_irq(hv_ivc->irq, &hv_ivc);
	tegra_hv_ivc_unreserve(hv_ivc);
	kfree(hv_ivc);
}

static void tegra186_hv_bpmp_deinit(struct tegra_bpmp *bpmp)
{
	unsigned int i;

	tegra186_hv_bpmp_channel_cleanup(bpmp->tx_channel);

	if (bpmp->soc->channels.cpu_rx.count == MAX_POSSIBLE_RX_CHANNEL)
		tegra186_hv_bpmp_channel_cleanup(bpmp->rx_channel);

	for (i = 0; i < bpmp->threaded.count; i++) {
		tegra186_hv_bpmp_channel_cleanup(&bpmp->threaded_channels[i]);
	}
}

const struct tegra_bpmp_ops tegra186_bpmp_hv_ops = {
	.init = tegra186_hv_bpmp_init,
	.deinit = tegra186_hv_bpmp_deinit,
	.is_response_ready = tegra186_bpmp_hv_is_message_ready,
	.is_request_ready = tegra186_bpmp_hv_is_message_ready,
	.ack_response = tegra186_bpmp_hv_ack_message,
	.ack_request = tegra186_bpmp_hv_ack_message,
	.is_response_channel_free = tegra186_hv_bpmp_is_channel_free,
	.is_request_channel_free = tegra186_hv_bpmp_is_channel_free,
	.post_response = tegra186_hv_bpmp_post_message,
	.post_request = tegra186_hv_bpmp_post_message,
	.ring_doorbell = tegra186_hv_ivc_notify,
	.resume = tegra186_hv_bpmp_resume,
};

static void tegra_bpmp_mrq_handle_ping(unsigned int mrq,
				       struct tegra_bpmp_channel *channel,
				       void *data)
{
	struct mrq_ping_response response;
#if defined(NV_TEGRA_IVC_STRUCT_HAS_IOSYS_MAP)
	struct mrq_ping_request request;

	tegra_bpmp_mb_read(&request, &channel->ib, sizeof(request));

	memset(&response, 0, sizeof(response));
	response.reply = request.challenge << 1;
#else
	struct mrq_ping_request *request;

	request = (struct mrq_ping_request *)channel->ib->data;

	memset(&response, 0, sizeof(response));
	response.reply = request->challenge << 1;
#endif


	tegra_bpmp_mrq_return(channel, 0, &response, sizeof(response));
}

static int tegra_bpmp_ping(struct tegra_bpmp *bpmp)
{
	struct mrq_ping_response response;
	struct mrq_ping_request request;
	struct tegra_bpmp_message msg;
#if (!IS_ENABLED(CONFIG_PREEMPT_RT))
	unsigned long flags;
#endif
	ktime_t start, end;
	int err;

	memset(&request, 0, sizeof(request));
	request.challenge = 1;

	memset(&response, 0, sizeof(response));

	memset(&msg, 0, sizeof(msg));
	msg.mrq = MRQ_PING;
	msg.tx.data = &request;
	msg.tx.size = sizeof(request);
	msg.rx.data = &response;
	msg.rx.size = sizeof(response);

#if (!IS_ENABLED(CONFIG_PREEMPT_RT))
	local_irq_save(flags);
	start = ktime_get();
	err = tegra_bpmp_transfer_atomic(bpmp, &msg);
	end = ktime_get();
	local_irq_restore(flags);
#else
	start = ktime_get();
	err = tegra_bpmp_transfer(bpmp, &msg);
	end = ktime_get();
#endif

	if (!err)
		dev_dbg(bpmp->dev,
			"ping ok: challenge: %u, response: %u, time: %lld\n",
			request.challenge, response.reply,
			ktime_to_us(ktime_sub(end, start)));

	return err;
}

static int tegra_bpmp_get_firmware_tag(struct tegra_bpmp *bpmp, char *tag,
				       size_t size)
{
	struct mrq_query_fw_tag_response resp;
	struct tegra_bpmp_message msg = {
		.mrq = MRQ_QUERY_FW_TAG,
		.rx = {
			.data = &resp,
			.size = sizeof(resp),
		},
	};
	int err;

	if (size != sizeof(resp.tag))
		return -EINVAL;

	err = tegra_bpmp_transfer(bpmp, &msg);
	if (err)
		return err;
	if (msg.rx.ret < 0)
		return -EINVAL;

	memcpy(tag, resp.tag, sizeof(resp.tag));
	return 0;
}

static int tegra_bpmp_hv_probe(struct platform_device *pdev)
{
	struct tegra_bpmp *bpmp;
	char tag[TAG_SZ];
	size_t size;
	int err;

	bpmp = devm_kzalloc(&pdev->dev, sizeof(*bpmp), GFP_KERNEL);
	if (!bpmp)
		return -ENOMEM;

	bpmp->soc = of_device_get_match_data(&pdev->dev);
	bpmp->dev = &pdev->dev;

	INIT_LIST_HEAD(&bpmp->mrqs);
	spin_lock_init(&bpmp->lock);

	bpmp->threaded.count = bpmp->soc->channels.thread.count;
	sema_init(&bpmp->threaded.lock, bpmp->threaded.count);

	size = BITS_TO_LONGS(bpmp->threaded.count) * sizeof(long);

	bpmp->threaded.allocated = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
	if (!bpmp->threaded.allocated)
		return -ENOMEM;

	bpmp->threaded.busy = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
	if (!bpmp->threaded.busy)
		return -ENOMEM;

	spin_lock_init(&bpmp->atomic_tx_lock);
	bpmp->tx_channel = devm_kzalloc(&pdev->dev, sizeof(*bpmp->tx_channel),
					GFP_KERNEL);
	if (!bpmp->tx_channel)
		return -ENOMEM;

	bpmp->rx_channel = devm_kzalloc(&pdev->dev, sizeof(*bpmp->rx_channel),
	                                GFP_KERNEL);
	if (!bpmp->rx_channel)
		return -ENOMEM;

	bpmp->threaded_channels = devm_kcalloc(&pdev->dev, bpmp->threaded.count,
					       sizeof(*bpmp->threaded_channels),
					       GFP_KERNEL);
	if (!bpmp->threaded_channels)
		return -ENOMEM;

	err = bpmp->soc->ops->init(bpmp);
	if (err < 0)
		return err;

	err = tegra_bpmp_request_mrq(bpmp, MRQ_PING,
				     tegra_bpmp_mrq_handle_ping, bpmp);
	if (err < 0)
		goto deinit;

	err = tegra_bpmp_ping(bpmp);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to ping BPMP: %d\n", err);
		goto free_mrq;
	}

	err = tegra_bpmp_get_firmware_tag(bpmp, tag, sizeof(tag));
	if (err < 0) {
		dev_err(&pdev->dev, "failed to get firmware tag: %d\n", err);
		goto free_mrq;
	}

	dev_err(&pdev->dev, "firmware: %.*s\n", (int)sizeof(tag), tag);

	platform_set_drvdata(pdev, bpmp);

	err = of_platform_default_populate(pdev->dev.of_node, NULL, &pdev->dev);
	if (err < 0)
		goto free_mrq;

	if (of_find_property(pdev->dev.of_node, "#clock-cells", NULL)) {
		err = tegra_bpmp_init_clocks(bpmp);
		if (err < 0)
			goto free_mrq;
	}

	if (of_find_property(pdev->dev.of_node, "#reset-cells", NULL)) {
		err = tegra_bpmp_init_resets(bpmp);
		if (err < 0)
			goto free_mrq;
	}

	if (of_find_property(pdev->dev.of_node, "#power-domain-cells", NULL)) {
		err = tegra_bpmp_init_powergates(bpmp);
		if (err < 0)
			goto free_mrq;
	}

	if (tegra_sku_info.platform != TEGRA_PLATFORM_VDK) {
		err = tegra_bpmp_init_debugfs(bpmp);
		if (err < 0)
			dev_err(&pdev->dev, "debugfs initialization failed: %d\n", err);
	}

	return 0;

free_mrq:
	tegra_bpmp_free_mrq(bpmp, MRQ_PING, bpmp);
deinit:
	if (bpmp->soc->ops->deinit)
		bpmp->soc->ops->deinit(bpmp);

	return err;
}

static int __maybe_unused tegra_bpmp_hv_resume(struct device *dev)
{
	struct tegra_bpmp *bpmp = dev_get_drvdata(dev);

	if (bpmp->soc->ops->resume)
		return bpmp->soc->ops->resume(bpmp);
	else
		return 0;
}

static const struct dev_pm_ops tegra_bpmp_hv_pm_ops = {
	.resume_noirq = tegra_bpmp_hv_resume,
};

static const struct tegra_bpmp_soc tegra186_hv_soc = {
	.channels = {
		.cpu_tx = {
			.offset = 3,
			.count = 1,
			.timeout = 30 * USEC_PER_SEC,
		},
		.thread = {
			.offset = 0,
			.count = 3,
			.timeout = 30 * USEC_PER_SEC,
		},
		.cpu_rx = {
			.offset = 13,
			.count = 1,
			.timeout = 0,
		},
	},
	.ops = &tegra186_bpmp_hv_ops,
	.num_resets = 193,
};

static const struct tegra_bpmp_soc t194_safe_hv_soc = {
	.channels = {
		.cpu_tx = {
			.offset = 3,
			.count = 1,
			.timeout = 30 * USEC_PER_SEC,
		},
		.thread = {
			.offset = 0,
			.count = 3,
			.timeout = 30 * USEC_PER_SEC,
		},
	},
	.ops = &tegra186_bpmp_hv_ops,
	.num_resets = 193,
};

static const struct of_device_id tegra_bpmp_hv_match[] = {
	{ .compatible = "nvidia,tegra186-bpmp-hv", .data = &tegra186_hv_soc },
	{ .compatible = "nvidia,tegra194-safe-bpmp-hv", .data = &t194_safe_hv_soc },
	{ }
};

static struct platform_driver tegra_bpmp_hv_driver = {
	.driver = {
		.name = "tegra-bpmp-hv",
		.of_match_table = tegra_bpmp_hv_match,
		.pm = &tegra_bpmp_hv_pm_ops,
		.suppress_bind_attrs = true,
	},
	.probe = tegra_bpmp_hv_probe,
};

builtin_platform_driver(tegra_bpmp_hv_driver);

MODULE_AUTHOR("Manish Bhardwaj <mbhardwaj@nvidia.com>");
MODULE_DESCRIPTION("virtual bpmp driver");
MODULE_LICENSE("GPL");

