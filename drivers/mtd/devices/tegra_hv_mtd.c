// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include <nvidia/conftest.h>

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h> /* printk() */
#include <linux/slab.h>   /* kmalloc() */
#include <linux/errno.h> /* error codes */
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/mtd/mtd.h>
#include <linux/completion.h>
#include <linux/mtd/partitions.h>
#include <soc/tegra/ivc-priv.h>
#include <soc/tegra/ivc_ext.h>
#include <soc/tegra/virt/hv-ivc.h>
#include <linux/version.h>
#include <soc/tegra/fuse.h>
#include <tegra_virt_storage_spec.h>
#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
#include <linux/tegra-hsierrrptinj.h>
#endif

#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
#define HSI_QSPI_REPORT_ID(inst)	(0x805B + (inst))
#define HSI_ERROR_MAGIC			0xDEADDEAD

static uint32_t total_instance_id;
#endif

struct vmtd_dev {
	struct vs_config_info config;
	uint64_t size;                   /* Device size in bytes */
	uint32_t ivc_id;
	uint32_t ivm_id;
	struct tegra_hv_ivc_cookie *ivck;
	struct tegra_hv_ivm_cookie *ivmk;
	struct device *device;
	void *shared_buffer;
	struct mutex lock;
	struct completion msg_complete;
	void *cmd_frame;
	struct mtd_info mtd;
	bool is_setup;
#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
	uint32_t epl_id;
	uint32_t epl_reporter_id;
	uint32_t instance_id;
#endif
};

#define IVC_RESET_RETRIES 30

static inline struct vmtd_dev *mtd_to_vmtd(struct mtd_info *mtd)
{
	return container_of(mtd, struct vmtd_dev, mtd);
}

static irqreturn_t ivc_irq_handler(int irq, void *data)
{
	struct vmtd_dev *vmtddev = (struct vmtd_dev *)data;

	complete(&vmtddev->msg_complete);
	return IRQ_HANDLED;
}

static int vmtd_send_cmd(struct vmtd_dev *vmtddev, struct vs_request *vs_req)
{
	/* This while loop exits as long as the remote endpoint cooperates. */
	while (tegra_hv_ivc_channel_notified(vmtddev->ivck) != 0)
		wait_for_completion(&vmtddev->msg_complete);

	while (!tegra_hv_ivc_can_write(vmtddev->ivck))
		wait_for_completion(&vmtddev->msg_complete);

	if (tegra_hv_ivc_write(vmtddev->ivck, vs_req,
		sizeof(struct vs_request)) != sizeof(struct vs_request)) {
		dev_err(vmtddev->device, "Request sending failed!\n");
		return -EIO;
	}

	return 0;
}

static int vmtd_get_resp(struct vmtd_dev *vmtddev, struct vs_request *vs_req)
{
	/* This while loop exits as long as the remote endpoint cooperates. */
	while (tegra_hv_ivc_channel_notified(vmtddev->ivck) != 0)
		wait_for_completion(&vmtddev->msg_complete);

	while (!tegra_hv_ivc_can_read(vmtddev->ivck))
		wait_for_completion(&vmtddev->msg_complete);

	if (tegra_hv_ivc_read(vmtddev->ivck, vs_req,
		sizeof(struct vs_request)) != sizeof(struct vs_request)) {
		dev_err(vmtddev->device, "Response fetching failed!\n");
		return -EIO;
	}

	return 0;
}

static int vmtd_process_request(struct vmtd_dev *vmtddev,
	struct vs_request *vs_req)
{
#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
	uint32_t num_bytes;
	loff_t offset;
#else
	uint32_t num_bytes = vs_req->mtddev_req.mtd_req.size;
	loff_t offset = vs_req->mtddev_req.mtd_req.offset;
#endif
	int32_t ret = 0;

#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
	if (vs_req->req_id != HSI_ERROR_MAGIC) {
		num_bytes = vs_req->mtddev_req.mtd_req.size;
		offset = vs_req->mtddev_req.mtd_req.offset;
	}
#endif

	ret = vmtd_send_cmd(vmtddev, vs_req);
	if (ret != 0) {
		dev_err(vmtddev->device,
			"Sending %d failed!\n",
			vs_req->mtddev_req.req_op);
		goto fail;
	}

	vs_req = (struct vs_request *)vmtddev->cmd_frame;
	ret = vmtd_get_resp(vmtddev, vs_req);
	if (ret != 0) {
		dev_err(vmtddev->device,
			"fetching response failed!\n");
		goto fail;
	}

#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
	if (vs_req->req_id == HSI_ERROR_MAGIC) {
		if (vs_req->status != 0) {
			dev_err(vmtddev->device, "Response status for error injection failed!\n");
			ret = -EIO;
			goto fail;
		} else {
			return ret;
		}
	}
#endif

	if ((vs_req->status != 0) ||
		(vs_req->mtddev_resp.mtd_resp.status != 0)) {
		dev_err(vmtddev->device,
			"Response status for offset %llx size %x failed!\n",
			offset, num_bytes);
		ret = -EIO;
		goto fail;
	}

	if (vs_req->mtddev_resp.mtd_resp.size != num_bytes) {
		dev_err(vmtddev->device,
			"size mismatch for offset %llx size %x returned %x!\n",
			offset, num_bytes,
			vs_req->mtddev_resp.mtd_resp.size);
		ret = -EIO;
		goto fail;
	}

fail:
	return ret;
}

static int vmtd_get_configinfo(struct vmtd_dev *vmtddev,
	struct vs_config_info *config)
{
	struct vs_request *vs_req = (struct vs_request *)vmtddev->cmd_frame;

	/* This while loop exits as long as the remote endpoint cooperates. */
	while (!tegra_hv_ivc_can_read(vmtddev->ivck))
		wait_for_completion(&vmtddev->msg_complete);

	if (!tegra_hv_ivc_read(vmtddev->ivck, vs_req,
				sizeof(struct vs_request))) {
		dev_err(vmtddev->device, "config fetching failed!\n");
		return -EIO;
	}

	if (vs_req->status != 0) {
		dev_err(vmtddev->device, "Config fetch request failed!\n");
		return -EINVAL;
	}

	*config = vs_req->config_info;
	return 0;
}

/*
 * Read an address range from the flash chip.  The address range
 * may be any size provided it is within the physical boundaries.
 */
static int vmtd_read(struct mtd_info *mtd, loff_t from, size_t len,
		size_t *retlen, u_char *buf)
{
	struct vmtd_dev *vmtddev = mtd_to_vmtd(mtd);
	struct vs_request *vs_req;
	size_t remaining_size = len;
	size_t read_size;
	u_char *buf_addr = buf;
	loff_t offset = from;
	int32_t ret = 0;

	dev_dbg(vmtddev->device, "%s from 0x%llx, len %zd\n",
		__func__, offset, remaining_size);

	if (((offset + remaining_size) < offset) ||
			((offset + remaining_size) > vmtddev->mtd.size)) {
		dev_err(vmtddev->device,
			"from %llx len %lx out of range!\n", offset,
			remaining_size);
		return -EPERM;
	}

	mutex_lock(&vmtddev->lock);
	while (remaining_size) {
		read_size = min_t(size_t, (size_t)vmtddev->config.mtd_config.max_read_bytes_per_io,
				remaining_size);
		vs_req = (struct vs_request *)vmtddev->cmd_frame;
		vs_req->type = VS_DATA_REQ;
		vs_req->mtddev_req.req_op = VS_MTD_READ;
		vs_req->mtddev_req.mtd_req.offset = offset;
		vs_req->mtddev_req.mtd_req.size = read_size;
		vs_req->mtddev_req.mtd_req.data_offset = 0;
		vs_req->req_id = 0;

		ret = vmtd_process_request(vmtddev, vs_req);
		if (ret != 0) {
			dev_err(vmtddev->device,
				"Read for offset %llx size %lx failed!\n",
				offset, read_size);
			goto fail;
		}

		memcpy(buf_addr, vmtddev->shared_buffer, read_size);
		buf_addr += read_size;
		offset += read_size;
		remaining_size -= read_size;
	}
	*retlen = len;

fail:
	mutex_unlock(&vmtddev->lock);
	return ret;
}

/*
 * Write an address range from the flash chip.  The address range
 * may be any size provided it is within the physical boundaries.
 */
static int vmtd_write(struct mtd_info *mtd, loff_t to, size_t len,
		size_t *retlen, const u_char *buf)
{
	struct vmtd_dev *vmtddev = mtd_to_vmtd(mtd);
	struct vs_request *vs_req;
	size_t remaining_size = len;
	size_t write_size;
	const u_char *buf_addr = buf;
	loff_t offset = to;
	int32_t ret = 0;

	dev_dbg(vmtddev->device, "%s from 0x%08x, len %zd\n",
		__func__, (u32)offset, remaining_size);

	if (((offset + remaining_size) < offset) ||
		((offset + remaining_size) > vmtddev->mtd.size)) {
		dev_err(vmtddev->device, "to %llx len %lx out of range!\n",
			offset, remaining_size);
		return -EPERM;
	}

	mutex_lock(&vmtddev->lock);
	while (remaining_size) {
		write_size = min_t(size_t,
			(size_t)vmtddev->config.mtd_config.max_write_bytes_per_io,
				remaining_size);
		vs_req = (struct vs_request *)vmtddev->cmd_frame;
		vs_req->type = VS_DATA_REQ;
		vs_req->mtddev_req.req_op = VS_MTD_WRITE;
		vs_req->mtddev_req.mtd_req.offset = offset;
		vs_req->mtddev_req.mtd_req.size = write_size;
		vs_req->mtddev_req.mtd_req.data_offset = 0;
		vs_req->req_id = 0;

		memcpy(vmtddev->shared_buffer, buf_addr, write_size);

		ret = vmtd_process_request(vmtddev, vs_req);
		if (ret != 0) {
			dev_err(vmtddev->device,
				"write for offset %llx size %lx failed!\n",
				offset, write_size);
			goto fail;
		}

		buf_addr += write_size;
		offset += write_size;
		remaining_size -= write_size;
	}
	*retlen = len;

fail:
	mutex_unlock(&vmtddev->lock);
	return ret;
}

/*
 * Erase an address range from the flash chip.  The address range
 * may be any size provided it is within the physical boundaries.
 */
static int vmtd_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct vmtd_dev *vmtddev = mtd_to_vmtd(mtd);
	struct vs_request *vs_req;
	int32_t ret = 0;

	dev_dbg(vmtddev->device, "%s from 0x%08x, len %llx\n",
		__func__, (u32)instr->addr, instr->len);

	if (((instr->addr + instr->len) < instr->addr) ||
			((instr->addr + instr->len) > vmtddev->mtd.size)) {
		dev_err(vmtddev->device, "addr %llx len %llx out of range!\n",
			instr->addr, instr->len);
		return -EPERM;
	}

	mutex_lock(&vmtddev->lock);
	vs_req = (struct vs_request *)vmtddev->cmd_frame;

	vs_req->type = VS_DATA_REQ;
	vs_req->mtddev_req.req_op = VS_MTD_ERASE;
	vs_req->mtddev_req.mtd_req.offset = instr->addr;
	vs_req->mtddev_req.mtd_req.size = instr->len;
	vs_req->mtddev_req.mtd_req.data_offset = 0;
	vs_req->req_id = 0;

	ret = vmtd_process_request(vmtddev, vs_req);
	if (ret != 0) {
		dev_err(vmtddev->device,
			"Erase for offset %llx size %llx failed!\n",
			instr->addr, instr->len);
		mutex_unlock(&vmtddev->lock);
		goto fail;
	}
	mutex_unlock(&vmtddev->lock);

fail:
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int tegra_virt_mtd_suspend(struct device *dev)
{
	struct vmtd_dev *vmtddev = dev_get_drvdata(dev);

	if (vmtddev->is_setup) {
		mutex_lock(&vmtddev->lock);
		disable_irq(vmtddev->ivck->irq);
		/* Reset the channel */
		tegra_hv_ivc_channel_reset(vmtddev->ivck);
	}
	return 0;
}

static int tegra_virt_mtd_resume(struct device *dev)
{
	struct vmtd_dev *vmtddev = dev_get_drvdata(dev);

	if (vmtddev->is_setup) {
		enable_irq(vmtddev->ivck->irq);
		mutex_unlock(&vmtddev->lock);
	}
	return 0;
}

static const struct dev_pm_ops tegra_hv_vmtd_pm_ops = {
	.suspend = tegra_virt_mtd_suspend,
	.resume = tegra_virt_mtd_resume,

};
#endif /* CONFIG_PM_SLEEP */

static int vmtd_setup_device(struct vmtd_dev *vmtddev)
{
	mutex_init(&vmtddev->lock);

	vmtddev->mtd.name = "virt_mtd";
	vmtddev->mtd.type = MTD_NORFLASH;
	vmtddev->mtd.writesize = 1;

	/* Set device read-only if config response say so */
	if (!(vmtddev->config.mtd_config.req_ops_supported &
				VS_MTD_READ_ONLY_MASK)) {
		dev_info(vmtddev->device, "setting device read-only\n");
		vmtddev->mtd.flags = MTD_CAP_ROM;
	} else {
		vmtddev->mtd.flags = MTD_CAP_NORFLASH;
	}

	vmtddev->mtd.size = vmtddev->config.mtd_config.size;
	dev_info(vmtddev->device, "size %lld!\n",
		vmtddev->config.mtd_config.size);
	vmtddev->mtd._erase = vmtd_erase;
	vmtddev->mtd._read = vmtd_read;
	vmtddev->mtd._write = vmtd_write;
	vmtddev->mtd.erasesize = vmtddev->config.mtd_config.erase_size;

	if (vmtddev->ivmk->size <
		vmtddev->config.mtd_config.max_read_bytes_per_io) {
		dev_info(vmtddev->device,
			"Consider increasing mempool size to %d!\n",
			vmtddev->config.mtd_config.max_read_bytes_per_io);
		vmtddev->config.mtd_config.max_read_bytes_per_io =
			vmtddev->ivmk->size;
	}

	if (vmtddev->ivmk->size <
		vmtddev->config.mtd_config.max_write_bytes_per_io) {
		dev_info(vmtddev->device,
			"Consider increasing mempool size to %d!\n",
			vmtddev->config.mtd_config.max_write_bytes_per_io);
		vmtddev->config.mtd_config.max_write_bytes_per_io =
			vmtddev->ivmk->size;
	}

	vmtddev->mtd.dev.parent = vmtddev->device;
	vmtddev->mtd.writebufsize = 1;

	mtd_set_of_node(&vmtddev->mtd, vmtddev->device->of_node);

	return mtd_device_parse_register(&vmtddev->mtd, NULL, NULL,
			NULL, 0);
}

static ssize_t vmtd_phys_dev_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct vmtd_dev *vmtddev = dev_get_drvdata(dev);

	if (vmtddev->config.phys_dev == VSC_DEV_QSPI)
		return snprintf(buf, 16, "QSPI\n");

	return snprintf(buf, 16, "unknown!\n");
}
static DEVICE_ATTR(phys_dev, 0444, vmtd_phys_dev_show, NULL);

static ssize_t vmtd_phys_base_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct vmtd_dev *vmtddev = dev_get_drvdata(dev);

	return snprintf(buf, 16, "0x%x\n", vmtddev->config.phys_base);
}
static DEVICE_ATTR(phys_base, 0444, vmtd_phys_base_show, NULL);

static ssize_t manufacturer_id_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct vmtd_dev *vmtddev = dev_get_drvdata(dev);

	return sprintf(buf, "0x%x\n", vmtddev->config.mtd_config.manufacturer_id);
}
static DEVICE_ATTR_RO(manufacturer_id);

static ssize_t device_id_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct vmtd_dev *vmtddev = dev_get_drvdata(dev);

	return sprintf(buf, "0x%x\n", vmtddev->config.mtd_config.device_id);
}
static DEVICE_ATTR_RO(device_id);

static ssize_t qspi_device_size_bytes_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct vmtd_dev *vmtddev = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n",  (unsigned int)vmtddev->config.mtd_config.qspi_device_size_bytes);
}
static DEVICE_ATTR_RO(qspi_device_size_bytes);

static ssize_t ecc_status_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct vmtd_dev *vmtddev = dev_get_drvdata(dev);
	struct vs_request *vs_req;
	int32_t ret;
	struct vs_mtd_ecc_response ecc_response;

	vs_req = (struct vs_request *)vmtddev->cmd_frame;
	vs_req->type = VS_DATA_REQ;
	vs_req->mtddev_req.req_op = VS_MTD_ECC;
	vs_req->mtddev_req.mtd_req.offset = 0;
	vs_req->mtddev_req.mtd_req.size = 0;
	vs_req->mtddev_req.mtd_req.data_offset = 0;
	vs_req->req_id = 0;

	ret = vmtd_process_request(vmtddev, vs_req);
	if (ret != 0) {
		dev_err(vmtddev->device, "Read ECC Failed\n");
		return snprintf(buf, PAGE_SIZE, "Error reading ECC status\n");
	}

	ecc_response = vs_req->mtddev_resp.ecc_resp;
	vs_req->mtddev_req.stored_ecc_status = ecc_response.status;
	vs_req->mtddev_req.stored_failed_chunk_addr = ecc_response.failed_chunk_addr;

	switch (ecc_response.status) {
	case ECC_NO_ERROR:
		return sprintf(buf, "0x%x\n", ECC_NO_ERROR);
	case ECC_ONE_BIT_CORRECTED:
		return sprintf(buf, "0x%x\n", ECC_ONE_BIT_CORRECTED);
	case ECC_TWO_BIT_ERROR:
		return sprintf(buf, "0x%x\n", ECC_TWO_BIT_ERROR);
	case ECC_DISABLED:
		return sprintf(buf, "0x%x\n", ECC_DISABLED);
	default:
		return snprintf(buf, PAGE_SIZE, "ECC Status: Unknown error\n");
	}
}
static DEVICE_ATTR_RO(ecc_status);

static ssize_t failure_chunk_addr_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct vmtd_dev *vmtddev = dev_get_drvdata(dev);
	struct vs_request *vs_req = (struct vs_request *)vmtddev->cmd_frame;

	if (vs_req->mtddev_req.stored_ecc_status == ECC_NO_ERROR)
		return sprintf(buf, "0x0\n");
	else
		return snprintf(buf, PAGE_SIZE, "0x%x\n", vs_req->mtddev_req.stored_failed_chunk_addr);

}
static DEVICE_ATTR_RO(failure_chunk_addr);

static const struct attribute *vmtd_storage_attrs[] = {
	&dev_attr_phys_dev.attr,
	&dev_attr_phys_base.attr,
	&dev_attr_manufacturer_id.attr,
	&dev_attr_device_id.attr,
	&dev_attr_qspi_device_size_bytes.attr,
	&dev_attr_ecc_status.attr,
	&dev_attr_failure_chunk_addr.attr,
	NULL
};

#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
/* Error report injection test support is included */
static int vmtd_inject_err_fsi(unsigned int inst_id, struct epl_error_report_frame err_rpt_frame,
				void *data)
{
	struct vmtd_dev *vmtddev = (struct vmtd_dev *)data;
	struct vs_request *vs_req;
	int ret = 0;

	/* Sanity check inst_id */
	if (inst_id != vmtddev->instance_id) {
		dev_err(vmtddev->device, "Invalid Input -> Instance ID = 0x%04x\n", inst_id);
		return -EINVAL;
	}

	/* Sanity check reporter_id */
	if (err_rpt_frame.reporter_id != vmtddev->epl_reporter_id) {
		dev_err(vmtddev->device, "Invalid Input -> Reporter ID = 0x%04x\n",
						err_rpt_frame.reporter_id);
		return -EINVAL;
	}

	mutex_lock(&vmtddev->lock);

	vs_req = (struct vs_request *)vmtddev->cmd_frame;
	vs_req->req_id = HSI_ERROR_MAGIC;
	vs_req->type = VS_ERR_INJECT;
	vs_req->error_inject_req.error_id = err_rpt_frame.error_code;

	ret = vmtd_process_request(vmtddev, vs_req);
	if (ret != 0)
		dev_err(vmtddev->device,
			"Error injection failed for mtd device\n");

	mutex_unlock(&vmtddev->lock);

	return ret;
}
#endif

static int32_t vmtd_init_device(struct vmtd_dev *vmtddev)
{
	struct vs_request *vs_req = (struct vs_request *)vmtddev->cmd_frame;
	uint32_t i = 0;
	int32_t ret = 0;
#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
	int err;
#endif

	/* This while loop exits as long as the remote endpoint cooperates. */
	pr_notice("vmtd: send_config wait for ivc channel notified\n");
	while (tegra_hv_ivc_channel_notified(vmtddev->ivck) != 0) {
		if (i++ > IVC_RESET_RETRIES) {
			dev_err(vmtddev->device, "ivc reset timeout\n");
			return -ENOMEDIUM;
		}
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(1));
	}

	vs_req->type = VS_CONFIGINFO_REQ;
	dev_info(vmtddev->device, "send config cmd to ivc #%d\n",
		vmtddev->ivc_id);

	ret = vmtd_send_cmd(vmtddev, vs_req);
	if (ret != 0) {
		dev_err(vmtddev->device, "Sending %d failed!\n",
				vs_req->type);
		return ret;
	}

	ret = vmtd_get_configinfo(vmtddev, &vmtddev->config);
	if (ret != 0) {
		dev_err(vmtddev->device, "fetching configinfo failed!\n");
		return ret;
	}

	if (vmtddev->config.type != VS_MTD_DEV) {
		dev_err(vmtddev->device,
			"Non mtd Config not supported - unexpected response!\n");
		return -EINVAL;
	}

	if (vmtddev->config.mtd_config.size == 0) {
		dev_err(vmtddev->device, "virtual storage device size 0!\n");
		return -EINVAL;
	}

	ret = vmtd_setup_device(vmtddev);
	if (ret != 0) {
		dev_err(vmtddev->device,
			"Setting up vmtd devices failed!\n");
		return ret;
	}

	if (sysfs_create_files(&vmtddev->device->kobj,
			vmtd_storage_attrs) != 0) {
		dev_warn(vmtddev->device,
			"Error Setting up sysfs files!\n");
	}

#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
	if (vmtddev->config.type == VS_MTD_DEV) {
		vmtddev->epl_id = IP_QSPI;
		vmtddev->epl_reporter_id = HSI_QSPI_REPORT_ID(total_instance_id);
		vmtddev->instance_id = total_instance_id++;
	}

	if (vmtddev->epl_id == IP_QSPI) {
		/* Register error reporting callback */
		err = hsierrrpt_reg_cb(vmtddev->epl_id, vmtddev->instance_id,
							vmtd_inject_err_fsi, vmtddev);
		if (err != 0)
			dev_info(vmtddev->device, "Err inj callback registration failed: %d", err);
	}
#endif

	vmtddev->is_setup = true;

	return ret;
}

static int tegra_virt_mtd_probe(struct platform_device *pdev)
{
	struct device_node __maybe_unused *np;
	struct device *dev = &pdev->dev;
	struct vmtd_dev *vmtddev;
	struct tegra_hv_ivm_cookie *ivmk;
	int ret;

	if (!is_tegra_hypervisor_mode()) {
		dev_err(dev, "Not running on Drive Hypervisor!\n");
		return -ENODEV;
	}

	np = dev->of_node;
	if (np == NULL) {
		dev_err(dev, "No of_node data\n");
		return -ENODEV;
	}

	vmtddev = devm_kzalloc(dev, sizeof(struct vmtd_dev), GFP_KERNEL);
	if (vmtddev == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, vmtddev);
	vmtddev->device = dev;

	if (of_property_read_u32_index(np, "ivc", 1,
		&(vmtddev->ivc_id))) {
		dev_err(dev, "Failed to read ivc property\n");
		return -ENODEV;
	}

	if (of_property_read_u32_index(np, "mempool", 0,
		&(vmtddev->ivm_id))) {
		dev_err(dev, "Failed to read mempool property\n");
		return -ENODEV;
	}

	vmtddev->ivck = tegra_hv_ivc_reserve(NULL, vmtddev->ivc_id, NULL);
	if (IS_ERR_OR_NULL(vmtddev->ivck)) {
		dev_err(dev, "Failed to reserve IVC channel %d\n",
			vmtddev->ivc_id);
		vmtddev->ivck = NULL;
		return -ENODEV;
	}

	ivmk = tegra_hv_mempool_reserve(vmtddev->ivm_id);
	if (IS_ERR_OR_NULL(ivmk)) {
		dev_err(dev, "Failed to reserve IVM channel %d\n",
			vmtddev->ivm_id);
		ivmk = NULL;
		ret = -ENODEV;
		goto free_ivc;
	}
	vmtddev->ivmk = ivmk;

	vmtddev->shared_buffer = devm_memremap(vmtddev->device,
			ivmk->ipa, ivmk->size, MEMREMAP_WB);
	if (IS_ERR_OR_NULL(vmtddev->shared_buffer)) {
		dev_err(dev, "Failed to map mempool area %d\n",
				vmtddev->ivm_id);
		ret = -ENOMEM;
		goto free_mempool;
	}

	if (vmtddev->ivck->frame_size < sizeof(struct vs_request)) {
		dev_err(dev, "Frame size %d less than ivc_req %ld!\n",
			vmtddev->ivck->frame_size,
			sizeof(struct vs_request));
		ret = -ENOMEM;
		goto free_mempool;
	}

	vmtddev->cmd_frame = devm_kmalloc(vmtddev->device,
			vmtddev->ivck->frame_size, GFP_KERNEL);
	if (vmtddev->cmd_frame == NULL) {
		ret = -ENOMEM;
		goto free_mempool;
	}

	init_completion(&vmtddev->msg_complete);

	if (devm_request_irq(vmtddev->device, vmtddev->ivck->irq,
		ivc_irq_handler, 0, "vmtd", vmtddev)) {
		dev_err(dev, "Failed to request irq %d\n", vmtddev->ivck->irq);
		ret = -EINVAL;
		goto free_mempool;
	}

	tegra_hv_ivc_channel_reset(vmtddev->ivck);

	if (vmtd_init_device(vmtddev) != 0) {
		dev_err(dev, "Failed to initialize mtd device\n");
		ret = -EINVAL;
		goto free_mempool;
	}

	return 0;

free_mempool:
	tegra_hv_mempool_unreserve(vmtddev->ivmk);

free_ivc:
	tegra_hv_ivc_unreserve(vmtddev->ivck);

	return ret;
}

static int tegra_virt_mtd_remove(struct platform_device *pdev)
{
	struct vmtd_dev *vmtddev = platform_get_drvdata(pdev);

	tegra_hv_ivc_unreserve(vmtddev->ivck);
	tegra_hv_mempool_unreserve(vmtddev->ivmk);
#if (IS_ENABLED(CONFIG_TEGRA_HSIERRRPTINJ))
	if (vmtddev->epl_id == IP_SDMMC)
		hsierrrpt_dereg_cb(vmtddev->epl_id, vmtddev->instance_id);
#endif

	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id tegra_virt_mtd_match[] = {
	{ .compatible = "nvidia,tegra-virt-mtd-storage", },
	{},
};
MODULE_DEVICE_TABLE(of, tegra_virt_mtd_match);
#endif /* CONFIG_OF */

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void tegra_virt_mtd_remove_wrapper(struct platform_device *pdev)
{
	tegra_virt_mtd_remove(pdev);
}
#else
static int tegra_virt_mtd_remove_wrapper(struct platform_device *pdev)
{
	return tegra_virt_mtd_remove(pdev);
}
#endif

static struct platform_driver tegra_virt_mtd_driver = {
	.probe	= tegra_virt_mtd_probe,
	.remove	= tegra_virt_mtd_remove_wrapper,
	.driver	= {
		.name = "Virtual MTD device",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(tegra_virt_mtd_match),
#ifdef CONFIG_PM_SLEEP
		.pm = &tegra_hv_vmtd_pm_ops,
#endif
	},
};

module_platform_driver(tegra_virt_mtd_driver);

MODULE_AUTHOR("Vishal Annapurve <vannapurve@nvidia.com>");
MODULE_DESCRIPTION("VIRT MTD driver");
MODULE_LICENSE("GPL v2");
