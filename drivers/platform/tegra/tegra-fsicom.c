// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/dma-buf.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/mailbox_client.h>
#include <linux/sched/signal.h>
#include <uapi/linux/tegra-fsicom.h>
#include <linux/pm.h>


/* Timeout in milliseconds */
#define TIMEOUT		5U

/* Unique signature for HSP Data */
#define IOVA_UNI_CODE	0xFE0D
#define PM_STATE_UNI_CODE	0xFDED

/* State Management */
#define PM_SUSPEND	6U
#define PM_SHUTDOWN	PM_SUSPEND

/*Data type for mailbox client and channel details*/
struct fsi_hsp_sm {
	struct mbox_client client;
	struct mbox_chan *chan;
};

/* Data type for accessing TOP2 HSP */
struct fsi_hsp {
	struct fsi_hsp_sm rx;
	struct fsi_hsp_sm tx;
	struct device dev;
};

struct fsi_dev_ctx {
	struct platform_device *pdev;
	struct list_head list;
};

static int device_file_major_number;
static const char device_name[] = "fsicom-client";

static struct platform_device *pdev_local;

/* Signaling to Application */
static struct task_struct *task;
static struct fsi_hsp *fsi_hsp_v[MAX_FSI_CORE];

static bool enable_deinit_notify;

static struct fsi_core fsi_core;

static LIST_HEAD(fsi_dev_list);
static DEFINE_MUTEX(fsi_dev_list_mutex);

static int fsicom_fsi_pm_notify(u32 state)
{
	uint32_t pdata[4] = {0};
	int ret = 0;
	int8_t lCoreId;

	pdata[0] = state;
	pdata[1] = state;
	pdata[2] = state;
	pdata[3] = PM_STATE_UNI_CODE;

	/* send pm state to fsi */
	for (lCoreId = fsi_core.notify_cnt - 1; lCoreId >= 0; lCoreId--) {
		ret = mbox_send_message(fsi_hsp_v[lCoreId]->tx.chan,
				(void *)pdata);
	}
	return ret < 0 ? ret : 0;
}

static void fsicom_send_signal(int sig, int32_t data)
{
	struct siginfo info;

	memset(&info, 0, sizeof(struct siginfo));
	info.si_signo = sig;
	info.si_code  = SI_QUEUE;
	info.si_int   = (u32) (unsigned long) data;

	/* Sending signal to app */
	if (task != NULL)
		if (send_sig_info(sig, (struct kernel_siginfo *)&info, task) < 0)
			pr_err("Unable to send signal %d\n", sig);
}

static void tegra_hsp_rx_notify(struct mbox_client *cl, void *msg)
{

	fsicom_send_signal(SIG_FSI_WRITE_EVENT, *((uint32_t *)msg));
}

static void tegra_hsp_tx_empty_notify(struct mbox_client *cl,
					 void *data, int empty_value)
{
	pr_debug("TX empty callback came\n");
}

static int tegra_hsp_mb_init(struct device *dev)
{
	int err;
	uint8_t lCoreId;
	char lTxStr[100] = {0};
	char lRxStr[100] = {0};

	for (lCoreId = 0; lCoreId < fsi_core.notify_cnt; lCoreId++) {
		fsi_hsp_v[lCoreId] = devm_kzalloc(dev, sizeof(*fsi_hsp_v[lCoreId]), GFP_KERNEL);
		if (!fsi_hsp_v[lCoreId])
			return -ENOMEM;

		if (dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32)))
			dev_err(dev, "FsiCom: setting DMA MASK failed!\n");

		fsi_hsp_v[lCoreId]->tx.client.dev = dev;
		fsi_hsp_v[lCoreId]->rx.client.dev = dev;
		fsi_hsp_v[lCoreId]->tx.client.tx_block = true;
		fsi_hsp_v[lCoreId]->tx.client.tx_tout = TIMEOUT;
		fsi_hsp_v[lCoreId]->rx.client.rx_callback = tegra_hsp_rx_notify;
		fsi_hsp_v[lCoreId]->tx.client.tx_done = tegra_hsp_tx_empty_notify;

		(void)snprintf(lTxStr, sizeof(lTxStr), "fsi-tx-cpu%d", lCoreId);
		fsi_hsp_v[lCoreId]->tx.chan = mbox_request_channel_byname(
						&fsi_hsp_v[lCoreId]->tx.client,
						lTxStr);
		if (IS_ERR(fsi_hsp_v[lCoreId]->tx.chan)) {
			err = PTR_ERR(fsi_hsp_v[lCoreId]->tx.chan);
			dev_err(dev, "failed to get tx mailbox: %d\n", err);
			return err;
		}

		(void)snprintf(lRxStr, sizeof(lRxStr), "fsi-rx-cpu%d", lCoreId);
		fsi_hsp_v[lCoreId]->rx.chan = mbox_request_channel_byname(
						&fsi_hsp_v[lCoreId]->rx.client,
						lRxStr);
		if (IS_ERR(fsi_hsp_v[lCoreId]->rx.chan)) {
			err = PTR_ERR(fsi_hsp_v[lCoreId]->rx.chan);
			dev_err(dev, "failed to get rx mailbox: %d\n", err);
			return err;
		}
	}

	return 0;
}

static int smmu_buff_map(unsigned long arg)
{
	u32 val = 0xFF;
	int ret;
	dma_addr_t dma_addr;
	dma_addr_t phys_addr;
	struct rw_data input;
	struct rw_data *user_input;
	struct fsi_dev_ctx *ctx = NULL;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct dma_buf *dmabuf;

	user_input = (struct rw_data *)arg;
	if (copy_from_user(&input, (void __user *)arg,
				sizeof(struct rw_data)))
		return -EACCES;

	list_for_each_entry(ctx, &fsi_dev_list, list) {
		if ((ctx != NULL) && (ctx->pdev != NULL)) {
			ret = of_property_read_u32(ctx->pdev->dev.of_node, "smmu_inst", &val);
			if (ret) {
				pr_err("failed to read smmu_inst\n");
				return -1;
			}
			if (val == input.coreid)
				break;
		}
	}
	if (val == input.coreid) {
		dmabuf = dma_buf_get(input.handle);

		if (IS_ERR_OR_NULL(dmabuf))
			return PTR_ERR(dmabuf);
		attach = dma_buf_attach(dmabuf, &ctx->pdev->dev);

		if (IS_ERR_OR_NULL(attach)) {
			pr_err("error : %lld\n", (signed long long)attach);
			return PTR_ERR(attach);
		}
		sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
		if (IS_ERR_OR_NULL(sgt)) {
			pr_err("error: %lld\n", (signed long long)sgt);
			return PTR_ERR(sgt);
		}
		phys_addr = sg_phys(sgt->sgl);
		dma_addr = sg_dma_address(sgt->sgl);
		if (copy_to_user((void __user *)&user_input->pa,
					(void *)&phys_addr, sizeof(uint64_t)))
			return -EACCES;
		if (copy_to_user((void __user *)&user_input->iova,
					(void *)&dma_addr, sizeof(uint64_t)))
			return -EACCES;
		if (copy_to_user((void __user *)&user_input->dmabuf,
					(void *)&dmabuf, sizeof(uint64_t)))
			return -EACCES;
		if (copy_to_user((void __user *)&user_input->attach,
					(void *)&attach, sizeof(uint64_t)))
			return -EACCES;
		if (copy_to_user((void __user *)&user_input->sgt,
					(void *)&sgt, sizeof(uint64_t)))
			return -EACCES;
	}

	return 0;
}

static ssize_t device_file_ioctl(
		struct file *fp, unsigned int cmd, unsigned long arg)
{
	struct rw_data input;
	int ret = 0;
	uint32_t pdata[4] = {0};
	struct iova_data ldata;
	struct fsi_core *fsi_core_input;
	int8_t hsp_mb_index = 0;

	switch (cmd) {

	case NVMAP_SMMU_MAP:
		ret = smmu_buff_map(arg);
		break;

	case NVMAP_SMMU_UNMAP:
		if (copy_from_user(&input, (void __user *)arg,
					sizeof(struct rw_data)))
			return -EACCES;
		dma_buf_unmap_attachment((struct dma_buf_attachment *)input.attach,
				(struct sg_table *) input.sgt, DMA_BIDIRECTIONAL);
		dma_buf_detach((struct dma_buf *)input.dmabuf,
				(struct dma_buf_attachment *) input.attach);
		dma_buf_put((struct dma_buf *)input.dmabuf);
		break;

	case TEGRA_HSP_WRITE:
		if (copy_from_user(&input, (void __user *)arg,
					sizeof(struct rw_data)))
			return -EACCES;
		while (hsp_mb_index < fsi_core.notify_cnt) {
			if (input.coreid == fsi_core.notify_list[hsp_mb_index])
				break;
			hsp_mb_index++;
		}
		if (hsp_mb_index == fsi_core.notify_cnt)
			return -ECHRNG;
		pdata[0] = input.handle;
		ret = mbox_send_message(fsi_hsp_v[hsp_mb_index]->tx.chan,
				(void *)pdata);
		break;

	case TEGRA_SIGNAL_REG:
		task = get_current();
		fsi_core_input = (struct fsi_core *)arg;
		if (copy_to_user((void __user *)&fsi_core_input->max_core,
					(void *)&fsi_core.max_core, sizeof(uint32_t)))
			return -EACCES;
		if (copy_to_user((void __user *)&fsi_core_input->notify_cnt,
					(void *)&fsi_core.notify_cnt, sizeof(uint32_t)))
			return -EACCES;
		if (copy_to_user((void __user *)&fsi_core_input->polling_cnt,
					(void *)&fsi_core.polling_cnt, sizeof(uint32_t)))
			return -EACCES;
		if (copy_to_user((void __user *)&fsi_core_input->notify_list,
					(void *)&fsi_core.notify_list, sizeof(uint32_t)*MAX_FSI_CORE))
			return -EACCES;
		if (copy_to_user((void __user *)&fsi_core_input->polling_list,
					(void *)&fsi_core.polling_list,  sizeof(uint32_t)*MAX_FSI_CORE))
			return -EACCES;
		break;

	case TEGRA_IOVA_DATA:
		if (copy_from_user(&ldata, (void __user *)arg,
					sizeof(struct iova_data)))
			return -EACCES;
		while (hsp_mb_index < fsi_core.notify_cnt) {
			if (ldata.coreid == fsi_core.notify_list[hsp_mb_index])
				break;
			hsp_mb_index++;
		}
		if (hsp_mb_index == fsi_core.notify_cnt)
			return -ECHRNG;
		pdata[0] = ldata.offset;
		pdata[1] = ldata.iova;
		pdata[2] = ldata.chid;
		pdata[3] = IOVA_UNI_CODE;
		ret = mbox_send_message(fsi_hsp_v[hsp_mb_index]->tx.chan,
				(void *)pdata);
		break;

	default:
		return -EINVAL;
	}
	return ret;
}

/* File operations */
static const struct file_operations fsicom_driver_fops = {
	.owner   = THIS_MODULE,
	.unlocked_ioctl   = device_file_ioctl,
};

static int fsicom_register_device(void)
{
	int result = 0;
	struct class *dev_class;

	result = register_chrdev(0, device_name, &fsicom_driver_fops);
	if (result < 0) {
		pr_err("%s> register_chrdev code = %i\n", device_name, result);
		return result;
	}
	device_file_major_number = result;
#if defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG) /* Linux v6.4 */
	dev_class = class_create("fsicom_client");
#else
	dev_class = class_create(THIS_MODULE, "fsicom_client");
#endif
	if (dev_class == NULL) {
		pr_err("%s> Could not create class for device\n", device_name);
		goto class_fail;
	}

	if ((device_create(dev_class, NULL,
		MKDEV(device_file_major_number, 0),
			 NULL, "fsicom_client")) == NULL) {
		pr_err("%s> Could not create device node\n", device_name);
		goto device_failure;
	}
	return 0;

device_failure:
	class_destroy(dev_class);
class_fail:
	unregister_chrdev(device_file_major_number, device_name);
	return -1;
}

static void fsicom_unregister_device(void)
{
	if (device_file_major_number != 0)
		unregister_chrdev(device_file_major_number, device_name);
}

static const struct of_device_id fsicom_client_dt_match[] = {
	{ .compatible = "nvidia,tegra234-fsicom-client"},
	{}
};

MODULE_DEVICE_TABLE(of, fsicom_client_dt_match);

static int fsicom_client_probe(struct platform_device *pdev)
{
	int ret = 0;
	u32 val;
	struct device *dev = &pdev->dev;
	const struct device_node *np = dev->of_node;
	struct fsi_dev_ctx *ctx;
	int iterator = 0;
	u8 index;
	uint32_t fsi_core_list[MAX_FSI_CORE] = {0};

	ret = of_property_read_u32(np, "smmu_inst", &val);
	if (ret) {
		pr_err("failed to read smmu_inst\n");
		return -1;
	}
	if (val == 0) {
		ret = of_property_read_u32(pdev->dev.of_node, "max_fsi_core", &fsi_core.max_core);
		if (ret) {
			pr_err("failed to read max_core count\n");
			return -1;
		}
		if (fsi_core.max_core > MAX_FSI_CORE) {
			pr_err("wrong fsi max_core count\n");
			return -1;
		}
		ret = of_property_read_variable_u32_array(pdev->dev.of_node, "fsi_core_notify",
							&fsi_core_list[0], 1, fsi_core.max_core);
		if (ret > 0 && ret <= MAX_FSI_CORE) {
			/* configure core 0 as default notification FSI core */
			fsi_core.notify_list[0] = 0;
			index = 1;
			for (iterator = 0; iterator < ret; iterator++) {
				if (0 == fsi_core_list[iterator])
					continue;
				if (fsi_core_list[iterator] >= MAX_FSI_CORE) {
					pr_err("invalid fsi core id configured as notification core\n");
					return -1;
				}
				fsi_core.notify_list[index] = fsi_core_list[iterator];
				index++;
			}
			fsi_core.notify_cnt = index;
		} else {
			/* FSI core 0 and core 1 is configured as notification core by default */
			fsi_core.notify_cnt = fsi_core.max_core;
			for (iterator = 0; iterator < fsi_core.notify_cnt; iterator++) {
				fsi_core.notify_list[iterator] = iterator;
			}
		}
		ret = of_property_read_variable_u32_array(pdev->dev.of_node, "fsi_core_polling",
							&fsi_core.polling_list[0], 1, fsi_core.max_core);
		if (ret > 0 && ret <= MAX_FSI_CORE) {
			fsi_core.polling_cnt = ret;
			for (iterator = 0; iterator < ret; iterator++) {
				if (fsi_core.polling_list[iterator] >= MAX_FSI_CORE) {
					pr_err("invalid fsi core id configured as polling core\n");
					return -1;
				}
			}
		}
		fsicom_register_device();
		ret = tegra_hsp_mb_init(&pdev->dev);
		pdev_local = pdev;
		if (of_property_read_bool(np, "enable-deinit-notify"))
			enable_deinit_notify = true;
	}
	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	INIT_LIST_HEAD(&ctx->list);
	ctx->pdev = pdev;

	mutex_lock(&fsi_dev_list_mutex);
	list_add_tail(&ctx->list, &fsi_dev_list);
	mutex_unlock(&fsi_dev_list_mutex);

	platform_set_drvdata(pdev, ctx);

	return ret;
}

static void fsicom_client_shutdown(struct platform_device *pdev)
{
	u32 val;
	int ret;

	ret = of_property_read_u32(pdev->dev.of_node, "smmu_inst", &val);
	if (ret) {
		pr_err("failed to read smmu_inst\n");
		return;
	}
	if (val == 0) {
		if (enable_deinit_notify)
			if (fsicom_fsi_pm_notify(PM_SHUTDOWN) < 0)
				pr_err("Unable to send notification to fsi\n");

		fsicom_unregister_device();
	}
}

static int fsicom_client_remove(struct platform_device *pdev)
{
	u32 val;
	int ret;
	struct fsi_dev_ctx *ctx = platform_get_drvdata(pdev);

	ret = of_property_read_u32(pdev->dev.of_node, "smmu_inst", &val);
	if (ret) {
		pr_err("failed to read smmu_inst\n");
		return -1;
	}
	if (val == 0) {
		pr_debug("fsicom remove called");
		fsicom_unregister_device();
		mutex_lock(&fsi_dev_list_mutex);
		list_del(&ctx->list);
		mutex_unlock(&fsi_dev_list_mutex);
	}
	return 0;
}

static int __maybe_unused fsicom_client_suspend(struct device *dev)
{
	int ret;
	u32 val;

	dev_dbg(dev, "suspend called\n");
	ret = of_property_read_u32(dev->of_node, "smmu_inst", &val);
	if (ret) {
		pr_err("failed to read smmu_inst\n");
		return -1;
	}
	if (val == 0) {
		if (enable_deinit_notify)
			ret = fsicom_fsi_pm_notify(PM_SUSPEND);
	}
	return ret;
}

static int __maybe_unused fsicom_client_resume(struct device *dev)
{

	int ret;
	u32 val;

	dev_dbg(dev, "resume called\n");
	ret = of_property_read_u32(dev->of_node, "smmu_inst", &val);
	if (ret) {
		pr_err("failed to read smmu_inst\n");
		return -1;
	}
	if (val == 0)
		fsicom_send_signal(SIG_DRIVER_RESUME, 0);

	return 0;
}

static SIMPLE_DEV_PM_OPS(fsicom_client_pm, fsicom_client_suspend, fsicom_client_resume);

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void fsicom_client_remove_wrapper(struct platform_device *pdev)
{
	fsicom_client_remove(pdev);
}
#else
static int fsicom_client_remove_wrapper(struct platform_device *pdev)
{
	return fsicom_client_remove(pdev);
}
#endif

static struct platform_driver fsicom_client = {
	.driver = {
		.name   = "fsicom_client",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(fsicom_client_dt_match),
		.pm = pm_ptr(&fsicom_client_pm),
	},
	.probe		= fsicom_client_probe,
	.shutdown	= fsicom_client_shutdown,
	.remove		= fsicom_client_remove_wrapper,
};

module_platform_driver(fsicom_client);

#if defined(NV_MODULE_IMPORT_NS_CALLS_STRINGIFY)
MODULE_IMPORT_NS(DMA_BUF);
#else
MODULE_IMPORT_NS("DMA_BUF");
#endif
MODULE_DESCRIPTION("FSI-CCPLEX-COM driver");
MODULE_AUTHOR("Prashant Shaw <pshaw@nvidia.com>");
MODULE_LICENSE("GPL v2");
