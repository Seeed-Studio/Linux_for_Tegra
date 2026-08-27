// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2017-2023, NVIDIA CORPORATION.  All rights reserved. */

#include "mods_internal.h"
#include <linux/err.h>

#ifdef MODS_ENABLE_BPMP_MRQ_API
static int tegra_pcie_bpmp_set_ctrl_state(struct mods_smmu_dev *pcie_dev,
					  bool enable)
{
	struct mrq_uphy_response resp;
	struct tegra_bpmp_message msg;
	struct mrq_uphy_request req;

	memset(&req, 0, sizeof(req));
	memset(&resp, 0, sizeof(resp));

	req.cmd = CMD_UPHY_PCIE_CONTROLLER_STATE;
	req.controller_state.pcie_controller = pcie_dev->cid;
	req.controller_state.enable = enable;

	memset(&msg, 0, sizeof(msg));
	msg.mrq = MRQ_UPHY;
	msg.tx.data = &req;
	msg.tx.size = sizeof(req);
	msg.rx.data = &resp;
	msg.rx.size = sizeof(resp);

	return tegra_bpmp_transfer(pcie_dev->bpmp, &msg);
}

static int uphy_bpmp_pcie_controller_state_set(int controller, int enable)
{
	#define MAX_DEV_NAME_LEN 32
	char dev_name[MAX_DEV_NAME_LEN];
	struct mods_smmu_dev *smmu_pdev = NULL;
	int smmudev_idx, n;

	memset(dev_name, 0, MAX_DEV_NAME_LEN);
	n = snprintf(dev_name, MAX_DEV_NAME_LEN, "mods_pcie%d", controller);
	if (n < 0 || n >= MAX_DEV_NAME_LEN)
		return -EINVAL;
	smmudev_idx = get_mods_smmu_device_index(dev_name);
	if (smmudev_idx >= 0)
		smmu_pdev = get_mods_smmu_device(smmudev_idx);
	if (!smmu_pdev || smmudev_idx < 0) {
		mods_error_printk("smmu device %s is not found\n", dev_name);
		return -ENODEV;
	}
	smmu_pdev->cid = controller;
	return tegra_pcie_bpmp_set_ctrl_state(smmu_pdev, enable);
}
#else

static int uphy_bpmp_pcie_controller_state_set(int controller, int enable)
{
	mods_error_printk("bpmp mrq api is not supported\n");
	return -ENODEV;
}
#endif

int esc_mods_bpmp_set_pcie_state(
	struct mods_client *client,
	struct MODS_SET_PCIE_STATE *p
)
{

	return uphy_bpmp_pcie_controller_state_set(p->controller, p->enable);
}

#ifdef MODS_ENABLE_BPMP_MRQ_API
static int tegra_pcie_bpmp_set_pll_state(struct mods_smmu_dev *pcie_dev,
					 bool enable)
{
	struct mrq_uphy_response resp;
	struct tegra_bpmp_message msg;
	struct mrq_uphy_request req;

	memset(&req, 0, sizeof(req));
	memset(&resp, 0, sizeof(resp));

	if (enable) {
		req.cmd = CMD_UPHY_PCIE_EP_CONTROLLER_PLL_INIT;
		req.ep_ctrlr_pll_init.ep_controller = pcie_dev->cid;
	} else {
		req.cmd = CMD_UPHY_PCIE_EP_CONTROLLER_PLL_OFF;
		req.ep_ctrlr_pll_off.ep_controller = pcie_dev->cid;
	}

	memset(&msg, 0, sizeof(msg));
	msg.mrq = MRQ_UPHY;
	msg.tx.data = &req;
	msg.tx.size = sizeof(req);
	msg.rx.data = &resp;
	msg.rx.size = sizeof(resp);

	return tegra_bpmp_transfer(pcie_dev->bpmp, &msg);
}

static int uphy_bpmp_pcie_set_pll_state(int controller, int enable)
{
	#define MAX_DEV_NAME_LEN 32
	char dev_name[MAX_DEV_NAME_LEN];
	struct mods_smmu_dev *smmu_pdev = NULL;
	int smmudev_idx, n;

	memset(dev_name, 0, MAX_DEV_NAME_LEN);
	n = snprintf(dev_name, MAX_DEV_NAME_LEN, "mods_pcie%d", controller);
	if (n < 0 || n >= MAX_DEV_NAME_LEN)
		return -EINVAL;
	smmudev_idx = get_mods_smmu_device_index(dev_name);
	if (smmudev_idx >= 0)
		smmu_pdev = get_mods_smmu_device(smmudev_idx);
	if (!smmu_pdev || smmudev_idx < 0) {
		mods_error_printk("smmu device %s is not found\n", dev_name);
		return -ENODEV;
	}
	smmu_pdev->cid = controller;
	return tegra_pcie_bpmp_set_pll_state(smmu_pdev, enable);
}
#else

static int uphy_bpmp_pcie_set_pll_state(int controller, int enable)
{
	mods_error_printk("bpmp mrq api is not supported\n");
	return -ENODEV;
}
#endif

int esc_mods_bpmp_init_pcie_ep_pll(
	struct mods_client *client,
	struct MODS_INIT_PCIE_EP_PLL *p
)
{
	return uphy_bpmp_pcie_set_pll_state(p->ep_id, 1);
}
