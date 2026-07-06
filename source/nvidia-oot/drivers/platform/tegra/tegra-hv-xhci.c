// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * tegra-hv-xhci: Tegra Hypervisor XHCI Driver
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include <nvidia/conftest.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/version.h>
#include <soc/tegra/fuse.h>
#include <linux/wait.h>
#include <linux/phy/phy.h>
#include <linux/phy/tegra/xusb.h>

#define DRV_NAME "tegra_hv_xhci"

#include <soc/tegra/ivc-priv.h>
#include <soc/tegra/ivc_ext.h>
#include <soc/tegra/virt/hv-ivc.h>

struct tegra_hv_ivc {
	uint32_t vf_id;
	uint32_t id;

	struct tegra_hv_ivc_cookie *ivck;
	char ivc_rx[128]; /* Buffer to receive pad ivc message */

	struct tegra_hv_xhci *hv_xhci;
};

struct tegra_hv_xhci {
	struct tegra_xusb_padctl *padctl;
	struct device_node *hv_np;
	struct device *dev;

	struct tegra_hv_ivc *ivc_queues;
	uint32_t ivc_queue_num;
};

static int tegra_hv_xhci_parse_dtb(struct tegra_hv_xhci *tegra)
{
	int ret;
	struct device_node *np;
	uint32_t i, id;
	struct tegra_hv_ivc *ivc;
	struct device *dev = tegra->dev;

	np = dev->of_node;
	if (!np) {
		dev_err(dev, "ivc_init: couldnt get of_node handle\n");
		return -EINVAL;
	}

	tegra->hv_np = of_parse_phandle(np, "ivc", 0);
	if (!tegra->hv_np) {
		dev_err(dev, "ivc_init: couldnt find ivc DT node\n");
		return -EINVAL;
	}

	ret = of_property_read_u32_index(np, "ivc", 1, &tegra->ivc_queue_num);
	if (ret) {
		dev_err(dev, "ivc_init: failed to get ivc num\n");
		return -EINVAL;
	}

	tegra->ivc_queues = devm_kcalloc(dev, tegra->ivc_queue_num,
				sizeof(*tegra->ivc_queues), GFP_KERNEL);
	if (!tegra->ivc_queues)
		return -ENOMEM;

	memset(tegra->ivc_queues, 0, sizeof(*tegra->ivc_queues) * tegra->ivc_queue_num);

	for (i = 0; i < tegra->ivc_queue_num; i++) {
		ivc = &tegra->ivc_queues[i];
		ret = of_property_read_u32_index(np, "ivc", 2 + i, &id);
		if (ret) {
			dev_err(dev, "ivc_init: failed to get ivc index %d\n", i);
			return -EINVAL;
		}
		ivc->vf_id = i + 1;
		ivc->id = id;
		ivc->hv_xhci = tegra;
	}

	return 0;
}

static irqreturn_t tegra_xhci_ivc_irq(int irq, void *data)
{
	struct tegra_hv_ivc *ivc = data;
	struct tegra_hv_xhci *tegra = ivc->hv_xhci;
	int ret;

	if (tegra_hv_ivc_channel_notified(ivc->ivck) != 0) {
		dev_info(tegra->dev, "ivc #%d not usable\n", ivc->id);
		return IRQ_HANDLED;
	}

	if (tegra_hv_ivc_can_read(ivc->ivck)) {
		/* Read the current message for the xhci_server to be
		 * able to send further messages on next pad interrupt
		 */
		ret = tegra_hv_ivc_read(ivc->ivck, ivc->ivc_rx, 128);
		if (ret < 0) {
			dev_err(tegra->dev,
				"ivc read #%d failed: %d\n", ivc->id, ret);
		} else {
			dev_dbg(tegra->dev, "ivc read #%d: vf_id=%d\n",
				ivc->id, ivc->vf_id);
#if defined(CONFIG_PHY_TEGRA_XUSB) && defined(PHY_TEGRA_XUSB_SUPPORT_EVENT)
			tegra_xusb_padctl_event_notify(tegra->padctl,
							ivc->vf_id << 24);
#else
			dev_warn(tegra->dev, "ivc read #%d: vf_id=%d ignored\n",
				ivc->id, ivc->vf_id);
#endif
		}
	} else {
		dev_dbg(tegra->dev, "can not read ivc #%d: %d\n",
			ivc->id, ivc->ivck->irq);
	}

	return IRQ_HANDLED;
}

static int tegra_hv_xhci_init_ivc(struct tegra_hv_xhci *tegra)
{
	int ret;
	uint32_t i;
	struct tegra_hv_ivc *ivc;

	for (i = 0; i < tegra->ivc_queue_num; i++) {
		ivc = &tegra->ivc_queues[i];

		if (ivc->id == 0)
			continue;

		ivc->ivck = tegra_hv_ivc_reserve(tegra->hv_np, ivc->id, NULL);
		if (IS_ERR_OR_NULL(ivc->ivck)) {
			dev_err(tegra->dev, "Failed to reserve ivc channel:%d\n", ivc->id);
			ret = PTR_ERR(ivc->ivck);
			ivc->ivck = NULL;
			return ret;
		}

		dev_info(tegra->dev, "Reserved IVC channel #%d - frame_size=%d irq %d\n",
			ivc->id, ivc->ivck->frame_size, ivc->ivck->irq);

		tegra_hv_ivc_channel_reset(ivc->ivck);

		ret = devm_request_threaded_irq(tegra->dev, ivc->ivck->irq,
				NULL, tegra_xhci_ivc_irq,
				IRQF_ONESHOT, dev_name(tegra->dev), ivc);
		if (ret) {
			dev_err(tegra->dev, "Unable to request irq(%d)\n", ivc->ivck->irq);
			tegra_hv_ivc_unreserve(ivc->ivck);
			return ret;
		}
	}

	of_node_put(tegra->hv_np);
	tegra->hv_np = NULL;

	return 0;
}

static int tegra_hv_xhci_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra_hv_xhci *tegra;
	int ret;

	if (!is_tegra_hypervisor_mode()) {
		dev_info(dev, "Hypervisor is not present\n");
		return -ENODEV;
	}

	tegra = devm_kzalloc(dev, sizeof(*tegra), GFP_KERNEL);
	if (!tegra)
		return -ENOMEM;

#if defined(CONFIG_PHY_TEGRA_XUSB)
	tegra->padctl = tegra_xusb_padctl_get(dev);
	if (IS_ERR(tegra->padctl))
		return PTR_ERR(tegra->padctl);
#endif

	tegra->dev = dev;
	platform_set_drvdata(pdev, tegra);

	ret = tegra_hv_xhci_parse_dtb(tegra);
	if (ret < 0) {
		dev_err(dev, "Failed to parse dtb\n");
		goto put_padctl;
	}

	ret = tegra_hv_xhci_init_ivc(tegra);
	if (ret < 0) {
		dev_err(dev, "Failed to init IVC channel with xhci_server\n");
		goto put_padctl;
	}

	return 0;

put_padctl:
#if defined(CONFIG_PHY_TEGRA_XUSB)
	tegra_xusb_padctl_put(tegra->padctl);
#endif
	return ret;
}

static int tegra_hv_xhci_remove(struct platform_device *pdev)
{
	struct tegra_hv_xhci *tegra = platform_get_drvdata(pdev);
	uint32_t i;
	struct tegra_hv_ivc *ivc;

	for (i = 0; i < tegra->ivc_queue_num; i++) {
		ivc = &tegra->ivc_queues[i];

		if (ivc->id != 0)
			tegra_hv_ivc_unreserve(ivc->ivck);
	}
#if defined(CONFIG_PHY_TEGRA_XUSB)
	tegra_xusb_padctl_put(tegra->padctl);
#endif

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id tegra_hv_xhci_match[] = {
	{ .compatible = "nvidia,tegra-hv-xhci", },
	{},
};
MODULE_DEVICE_TABLE(of, tegra_hv_xhci_match);
#endif /* CONFIG_OF */

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void tegra_hv_xhci_remove_wrapper(struct platform_device *pdev)
{
	tegra_hv_xhci_remove(pdev);
}
#else
static int tegra_hv_xhci_remove_wrapper(struct platform_device *pdev)
{
	return tegra_hv_xhci_remove(pdev);
}
#endif

static struct platform_driver tegra_hv_xhci_platform_driver = {
	.probe	= tegra_hv_xhci_probe,
	.remove	= tegra_hv_xhci_remove_wrapper,
	.driver	= {
		.name		= DRV_NAME,
		.owner		= THIS_MODULE,
		.of_match_table	= of_match_ptr(tegra_hv_xhci_match),
	},
};

static int tegra_hv_xhci_init(void)
{
	return platform_driver_register(&tegra_hv_xhci_platform_driver);
}

static void tegra_hv_xhci_exit(void)
{
	platform_driver_unregister(&tegra_hv_xhci_platform_driver);
}

module_init(tegra_hv_xhci_init);
module_exit(tegra_hv_xhci_exit);

MODULE_DESCRIPTION("Tegra Hyperisor XHCI Driver");
MODULE_LICENSE("GPL");
