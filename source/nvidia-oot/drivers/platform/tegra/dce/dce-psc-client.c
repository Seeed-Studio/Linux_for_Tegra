// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

/**
 * @file dce-psc-client.c
 * @brief DCE to PSC mailbox communication client
 *
 * This file implements the DCE side communication with PSC (Power Sequencing
 * Controller) using the Linux mailbox framework and psc_mbox_send_data.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mailbox_client.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <dce.h>
#include <dce-linux-device.h>
#include <dce-os-utils.h>
#include <dce-psc-client-priv.h>
#include <dce-psc-client.h>

#define DCE_FOURCC(a, b, c, d)  (((uint32_t)(a) << 24U) | \
	((uint32_t)(b) << 16U) | ((uint32_t)(c) << 8U) | (uint32_t)(d))

#define DCE_TASK_ID(a, b, c, d) DCE_FOURCC(a, b, c, d)

#define DCE_PSC_MBOX_INITIATOR      DCE_TASK_ID('D', 'C', 'E', 'F')
/* Load dce_fw on boot */
#define DCE_PSC_MBOX_FW_LOAD_CMD    DCE_TASK_ID('F', 'U', 'L', 'L')
/* Resume dce_fw on SC7 */
#define DCE_PSC_MBOX_FW_RESUME_CMD  DCE_TASK_ID('C', 'F', 'G', 'O')

/* EXT_CFG register offset */
#define EXT_CFG_SIDTABLE	0x0
#define EXT_CFG_SIDCONFIG	0x4

/**
 * dce_psc_rx_callback - Mailbox RX callback
 *
 * @cl: Pointer to mailbox client
 * @msg: Received message
 *
 * Called when a message is received from PSC.
 */
static void dce_psc_rx_callback(struct mbox_client *cl, void *msg)
{
	struct dce_psc_client *psc = container_of(cl, struct dce_psc_client, cl);

	dev_err(psc->dev, "%s: Received message from PSC\n", __func__);
	if (!psc || !msg) {
		pr_err("%s: Invalid parameters\n", __func__);
		return;
	}

	memcpy(psc->rx_msg, msg, DCE_PSC_MBOX_MSG_LEN);
	complete(&psc->rx_complete);
}

#define NV(x) "nvidia," #x
static int
dce_psc_setup_extcfg_vm2(struct platform_device *pdev)
{
	struct resource *res;
	struct device *dev = &pdev->dev;
	void __iomem *base;
	u32 value;

	/* second mailbox address */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "extcfg");
	/* we have res == 0 in case of ACPI and not DT */
	if (res == NULL)
		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base)) {
		dev_dbg(dev, "setup_extcfg_vm2 ioremap failed\n");
		return -EINVAL;
	}

	if (!device_property_read_u8_array(&pdev->dev, NV(sidtable),
		(u8 *)&value, sizeof(value))) {
		dev_dbg(dev, "sidtable:%08x\n", value);
		writel(value, base + EXT_CFG_SIDTABLE); /* PSC_EXT_CFG_SIDTABLE_VM2_0 */
	}

	if (!device_property_read_u32(&pdev->dev, NV(sidconfig), &value)) {
		dev_dbg(dev, "sidcfg:%08x\n", value);
		writel(value, base + EXT_CFG_SIDCONFIG); /* PSC_EXT_CFG_SIDCONFIG_VM2_0 */
	}

	return 0;
}

static struct device *psc_dev_from_mbox_chan(struct device *d,
			const struct mbox_chan *ch)
{
	if ((ch == NULL) || (ch->mbox ==  NULL) || (ch->mbox->dev ==  NULL)) {
		pr_err("[%s] failed to get psc device\n", __func__);
		return NULL;
	}

	return ch->mbox->dev;
}

/**
 * dce_psc_is_ready - Check if PSC client is ready
 *
 * @pdev: Platform device pointer
 *
 * Returns: true if initialized, false otherwise
 */
static bool dce_psc_is_ready(struct platform_device *pdev)
{
	struct dce_linux_device *d_dev;
	struct dce_psc_client *psc;

	if (!pdev)
		return false;

	d_dev = dev_get_drvdata(&pdev->dev);
	if (!d_dev)
		return false;

	psc = d_dev->psc;
	return psc && psc->initialized;
}
/**
 * dce_psc_send_message - Send a message to PSC and wait for response
 *
 * @dev: device pointer
 * @opcode0: First opcode word
 * @opcode1: Second opcode word
 * @tx_buf: Transmit buffer (can be NULL)
 * @tx_size: Size of transmit buffer
 * @rx_buf: Receive buffer (can be NULL)
 * @rx_size: Size of receive buffer
 * @response: Buffer to store mailbox response (16 words)
 *
 * Returns: 0 on success, negative error code on failure
 */
static int dce_psc_send_message(struct device *dev,
		u32 opcode0, u32 opcode1,
		void *tx_phys, u32 tx_size,
		void *rx_phys, u32 rx_size,
		u32 *response)
{
	struct dce_linux_device *d_dev = NULL;
	struct dce_psc_client *psc = NULL;
	union dce_psc_mbox_msg msg;
	int ret = 0;

	if (!dev) {
		pr_err("%s: Invalid device\n", __func__);
		return -EINVAL;
	}

	d_dev = dev_get_drvdata(dev);
	if (!d_dev) {
		dev_err(dev, "DCE device data not initialized\n");
		return -ENODEV;
	}

	memset(&msg, 0, sizeof(union dce_psc_mbox_msg));

	psc = d_dev->psc;
	if (!psc || !psc->initialized) {
		pr_err("%s: DCE PSC client not initialized\n", __func__);
		return -ENODEV;
	}

	if (tx_size > DCE_PSC_MAX_DMA_SIZE || rx_size > DCE_PSC_MAX_DMA_SIZE) {
		dev_err(dev, "Buffer size too large\n");
		return -EINVAL;
	}

	mutex_lock(&psc->lock);

	/* Prepare mailbox message */
	msg.opcode[0] = opcode0;
	msg.opcode[1] = opcode1;
	msg.tx_size = tx_size;
	msg.rx_size = rx_size;
	msg.tx_iova = (dma_addr_t)tx_phys;
	msg.rx_iova = (dma_addr_t)rx_phys;

	dev_dbg(dev, "Sending message: op[0x%x, 0x%x], tx[%p:%u], rx[%p:%u]\n",
		opcode0, opcode1, tx_phys, tx_size, rx_phys, rx_size);

	/* Send message and wait for response */
	reinit_completion(&psc->rx_complete);

	ret = mbox_send_message(psc->chan, &msg);
	if (ret < 0) {
		dev_err(dev, "Failed to send message: %d\n", ret);
		goto out_err_send;
	}

	mbox_client_txdone(psc->chan, 0);

	ret = wait_for_completion_timeout(&psc->rx_complete,
					msecs_to_jiffies(DCE_PSC_WAIT_TIMEOUT));
	if (ret == 0) {
		dev_err(dev, "Timeout waiting for PSC response, not failing now\n");
		ret = -ETIMEDOUT;
//		goto out_err_timeout;
	}

	/* Copy mailbox response if requested */
	if (response)
		memcpy(response, psc->rx_msg, DCE_PSC_MBOX_MSG_LEN);

	/* Copy RX data if needed */
	dev_err(dev, "Message sent successfully\n");
	ret = 0;

out_err_send:
//out_err_timeout:
	mutex_unlock(&psc->lock);
	return ret;
}

/**
 * dce_psc_client_init - Initialize DCE PSC client
 *
 * @pdev: Platform device pointer
 *
 * Returns: 0 on success, negative error code on failure
 */
static int dce_psc_client_init(struct platform_device *pdev)
{
	struct dce_linux_device *d_dev;
	struct dce_psc_client *psc;
	struct device *dev = &pdev->dev;
	int ret;

	d_dev = dev_get_drvdata(dev);
	if (!d_dev) {
		dev_err(dev, "DCE device data not initialized\n");
		return -ENODEV;
	}

	if (dce_psc_is_ready(pdev)) {
		dev_warn(dev, "DCE PSC client already initialized\n");
		return 0;
	}

	psc = devm_kzalloc(dev, sizeof(*psc), GFP_KERNEL);
	if (!psc)
		return -ENOMEM;

	psc->dev = dev;
	mutex_init(&psc->lock);

	/* Configure mailbox client */
	psc->cl.dev = dev;
	psc->cl.rx_callback = dce_psc_rx_callback;
	psc->cl.tx_block = false;
	psc->cl.tx_tout = DCE_PSC_TX_TIMEOUT;
	psc->cl.knows_txdone = false;

	/* Request mailbox channel */
	psc->chan = mbox_request_channel_byname(&psc->cl, "dce-psc");
	if (IS_ERR(psc->chan)) {
		ret = PTR_ERR(psc->chan);
		dev_err(dev, "Failed to request mailbox channel: %d\n", ret);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Check device tree for 'mboxes' property\n");
		goto err_free;
	}
	init_completion(&psc->rx_complete);

	psc->initialized = true;
	d_dev->psc = psc;

	dev_info(dev, "DCE PSC client initialized successfully\n");
	return 0;

err_free:
	mutex_destroy(&psc->lock);
	devm_kfree(dev, psc);
	return ret;
}

/**
 * dce_psc_load_fw_by_psc - Load the dce fw
 *
 * Check whether the dce_fw_load_by_psc is set
 * Initialize the dce as psc client to setup mailbox
 * Allocate the carveout and copy the dce fw
 * Send mailbox message to psc to load the dce fw
 *
 * @pdev: Platform device pointer
 * @d:    tegra_dce pointer
 *
 * Returns: 0 on success, negative error code on failure
 */
int dce_psc_load_fw(struct platform_device *pdev, struct tegra_dce *d)
{
	struct dce_psc_client *psc = NULL;
	struct dce_linux_device *d_dev = NULL;
	struct device *dev = &pdev->dev;
	const char *name;
	int ret = 0;

	d_dev = dce_linux_device_from_dce(d);
	/* check whether load by psc flag is set */
	if (d_dev->dce_fw_load_by_psc == false)
		return 0;

	name = pdata_from_dce_linux_device(d)->fw_name;

	/* setup dce_psc_client mbox */
	/*Initialize PSC client - this will defer probe if PSC isn't ready yet */
	ret = dce_psc_client_init(pdev);
	if (ret) {
		if (ret == -EPROBE_DEFER)
			dev_info(dev, "PSC mailbox not ready, deferring probe\n");
		else
			dev_err(dev, "failed to initialize PSC client with err = %d\n", ret);
		goto psc_init_err;
	}

	psc = d_dev->psc;

	ret = dce_psc_setup_extcfg_vm2(pdev);
	if (ret) {
		dev_err(dev, "Failed to setup vm2 config: %d\n", ret);
		goto err_free_chan;
	}

	/* get the psc device struct and use psc dev for dce fw
	 * caveout allocation
	 */
	d_dev->psc_dev = psc_dev_from_mbox_chan(dev, psc->chan);
	if (d_dev->psc_dev == NULL) {
		dev_err(dev, "FW Failed to get psc dev");
		ret = -EBUSY;
		goto err_free_chan;
	}

	d->fw_data = dce_os_request_firmware(d, name);
	if (!d->fw_data) {
		dev_err(dev, "FW Request Failed");
		ret = -EBUSY;
		goto err_free_chan;
	}

	ret = dce_psc_send_message(dev,
			DCE_PSC_MBOX_INITIATOR, DCE_PSC_MBOX_FW_LOAD_CMD,
			(void *)d->fw_data->dma_handle, d->fw_data->size,
			NULL, 0, NULL);
	if (ret) {
		dev_err(dev, "PSC send message Failed");
		ret = -EBUSY;
		goto err_free_chan;
	}

	dce_set_load_fw_status(d, true);
	dev_dbg(dev, "DCE FW loading completed");

	/* TODO: Make sure dce_fw hsp init is completed by checking semaphore register value
	 * before initializing the hsp registers from kmd
	 * Temp use of delay now
	 */
	msleep(500);

	return ret;

err_free_chan:
	mbox_free_channel(psc->chan);
psc_init_err:
	return ret;
}

/**
 * dce_psc_resume_fw - Resume dce fw after sc7
 *
 * @dev: device pointer
 *
 * Resume the dc_fw after sc7 through
 * PSC mailbox registers
 *
 * Returns: 0 on success, negative error code on failure
 */
int dce_psc_resume_fw(struct device *dev, struct tegra_dce *d)
{
	int ret = 0;
	struct dce_linux_device *d_dev = NULL;

	d_dev = dce_linux_device_from_dce(d);
	/* check whether load by psc flag is set */
	if (d_dev->dce_fw_load_by_psc == false)
		return 0;

	ret = dce_psc_send_message(dev,
			DCE_PSC_MBOX_INITIATOR, DCE_PSC_MBOX_FW_RESUME_CMD,
			NULL, 0,
			NULL, 0, NULL);
	if (ret)
		dev_err(dev, "PSC send message Failed");

	return ret;
}

/**
 * dce_psc_client_deinit - Deinitialize DCE PSC client
 *
 * @pdev: Platform device pointer
 */
void dce_psc_client_deinit(struct platform_device *pdev)
{
	struct dce_psc_client *psc = NULL;
	struct device *dev = &pdev->dev;
	struct dce_linux_device *d_dev = NULL;

	d_dev = dev_get_drvdata(dev);
	if (!d_dev) {
		dev_dbg(dev, "DCE device data not initialized\n");
		return;
	}

	/* check whether load by psc flag is set */
	if (d_dev->dce_fw_load_by_psc == false)
		return;

	psc = d_dev->psc;
	if (!psc || !psc->initialized) {
		dev_dbg(dev, "DCE PSC client not initialized\n");
		return;
	}

	mutex_lock(&psc->lock);
	psc->initialized = false;
	mutex_unlock(&psc->lock);

	mbox_free_channel(psc->chan);
	mutex_destroy(&psc->lock);

	devm_kfree(dev, psc);
	d_dev->psc = NULL;
	d_dev->psc_dev = NULL;

	dev_info(dev, "DCE PSC client deinitialized\n");
}

