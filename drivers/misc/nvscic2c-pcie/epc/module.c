/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <nvidia/conftest.h>

#define pr_fmt(fmt)	"nvscic2c-pcie: epc: " fmt

#include <linux/aer.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of_device.h>
#include <linux/pci.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/tegra-pcie-dma.h>
#include <linux/types.h>

#include "comm-channel.h"
#include "common.h"
#include "endpoint.h"
#include "iova-alloc.h"
#include "module.h"
#include "pci-client.h"
#include "vmap.h"

static const struct pci_device_id nvscic2c_pcie_epc_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_C2C_1) },
	{ PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_C2C_2) },
	{ PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_C2C_3) },
	{},
};

/* wrapper over tegra-pcie-edma init api. */
static int
edma_module_init(struct driver_ctx_t *drv_ctx)
{
	u8 i = 0;
	int ret = 0;
	struct tegra_pcie_dma_init_info info = {0};
	tegra_pcie_dma_status_t dma_status = TEGRA_PCIE_DMA_STATUS_INVAL_STATE;

	if (WARN_ON(!drv_ctx || !drv_ctx->drv_param.edma_np))
		return -EINVAL;

	memset(&info, 0x0, sizeof(info));
	info.dev = drv_ctx->cdev;
	info.remote = NULL;
	if (drv_ctx->chip_id == TEGRA234)
		info.soc = NVPCIE_DMA_SOC_T234;
	else {
		info.soc = NVPCIE_DMA_SOC_T264;
		info.msi_irq = drv_ctx->msi_irq;
		info.msi_addr = drv_ctx->msi_addr;
		info.msi_data = drv_ctx->msi_data;
	}

	for (i = 0; i < TEGRA_PCIE_DMA_WR_CHNL_NUM; i++) {
		info.tx[i].ch_type = TEGRA_PCIE_DMA_CHAN_XFER_ASYNC;
		info.tx[i].num_descriptors = NUM_EDMA_DESC;
	}
	/*No use-case for RD channels.*/

	dma_status = tegra_pcie_dma_initialize(&info, &drv_ctx->edma_h);
	if (dma_status != TEGRA_PCIE_DMA_SUCCESS) {
		ret = -ENODEV;
		pr_err("tegra_pcie_dma_initialize() failed : %d\n", dma_status);
		return ret;
	}

	if (drv_ctx->chip_id == TEGRA264) {
		dma_status = tegra_pcie_dma_set_msi(drv_ctx->edma_h, drv_ctx->msi_addr,
						    drv_ctx->msi_data);
		if (dma_status != TEGRA_PCIE_DMA_SUCCESS) {
			ret = -ENOMEM;
			pr_err("tegra_pcie_dma_set_msi() failed : %d\n", dma_status);
			goto err_set_msi;
		}
	}

	return ret;

err_set_msi:
	tegra_pcie_dma_deinit(&drv_ctx->edma_h);
	drv_ctx->edma_h = NULL;

	return ret;
}

/* should stop any ongoing eDMA transfers.*/
static void
edma_module_stop(struct driver_ctx_t *drv_ctx)
{
	if (!drv_ctx || !drv_ctx->edma_h)
		return;

	tegra_pcie_dma_stop(drv_ctx->edma_h);
}

/* should not have any ongoing eDMA transfers.*/
static void
edma_module_deinit(struct driver_ctx_t *drv_ctx)
{
	if (!drv_ctx || !drv_ctx->edma_h)
		return;

	tegra_pcie_dma_deinit(&drv_ctx->edma_h);
	drv_ctx->edma_h = NULL;
}

static void
free_inbound_area(struct pci_dev *pdev, struct dma_buff_t *self_mem)
{
	struct driver_ctx_t *drv_ctx = NULL;

	if (!pdev || !self_mem)
		return;

	drv_ctx = pci_get_drvdata(pdev);
	if (!drv_ctx)
		return;

	if (self_mem->dma_handle && drv_ctx->ivd_h)
		iova_alloc_deinit(self_mem->dma_handle, self_mem->size,
				  &drv_ctx->ivd_h);
	self_mem->dma_handle = 0x0;
}

/*
 * Allocate area visible to PCIe EP/DRV_MODE_EPF. To have symmetry between the
 * two modules, even PCIe RP/DRV_MODE_EPC allocates an empty area for all writes
 * from PCIe EP/DRV_MODE_EPF to land into. Also, all CPU access from PCIe EP/
 * DRV_MODE_EPF need be for one continguous region.
 */
static int
allocate_inbound_area(struct pci_dev *pdev, size_t win_size,
		      struct dma_buff_t *self_mem)
{
	int ret = 0;
	struct driver_ctx_t *drv_ctx = NULL;

	drv_ctx = pci_get_drvdata(pdev);
	if (!drv_ctx) {
		pr_err("Could not fetch driver data.");
		return -ENOMEM;
	}

	self_mem->size = win_size;
	ret = iova_alloc_init(&pdev->dev, win_size, &self_mem->dma_handle,
			      &drv_ctx->ivd_h);
	if (ret) {
		pr_err("iova_alloc_init() failed for size:(0x%lx)\n",
		       self_mem->size);
	}

	return ret;
}

static void
free_outbound_area(struct pci_dev *pdev, struct pci_aper_t *peer_mem)
{
	if (!pdev || !peer_mem)
		return;

	peer_mem->aper = 0x0;
	peer_mem->size = 0;
}

/* Assign outbound pcie aperture for CPU/eDMA access towards PCIe EP. */
static int
assign_outbound_area(struct pci_dev *pdev, size_t win_size, int bar,
		     struct pci_aper_t *peer_mem)
{
	int ret = 0;

	/*
	 * Added below check to fix CERT ARR30-C violation:
	 * cert_arr30_c_violation: pdev->resource[bar] evaluates to an address
	 * that could be at negative offset of an array.
	 */
	if (bar < 0) {
		pr_err("Invalid BAR index : %d", bar);
		return -EINVAL;
	}
	peer_mem->size = win_size;
	peer_mem->aper = pci_resource_start(pdev, bar);

	return ret;
}

/* Handle link message from @DRV_MODE_EPF. */
static void
link_msg_cb(void *data, void *ctx)
{
	struct comm_msg *msg = (struct comm_msg *)data;
	struct driver_ctx_t *drv_ctx = (struct driver_ctx_t *)ctx;

	if (WARN_ON(!msg || !drv_ctx))
		return;

	if (msg->u.link.status == NVSCIC2C_PCIE_LINK_UP) {
		complete_all(&drv_ctx->epc_ctx->epf_ready_cmpl);
	} else if (msg->u.link.status == NVSCIC2C_PCIE_LINK_DOWN) {
		complete_all(&drv_ctx->epc_ctx->epf_shutdown_cmpl);
	} else {
		pr_err("(%s): spurious link message received from EPF\n",
		       drv_ctx->drv_name);
		return;
	}

	/* inidicate link status to application.*/
	pci_client_change_link_status(drv_ctx->pci_client_h,
				      msg->u.link.status);
}

/*
 * PCIe subsystem invokes .shutdown()/.remove() handler when the PCIe EP
 * is hot-unplugged (gracefully) or @DRV_MODE_EPC(this) is unloaded while
 * the PCIe link was still active or when PCIe EP goes abnormal shutdown/
 * reboot.
 */
#define MAX_EPF_SHUTDOWN_TIMEOUT_MSEC	(5000)
static void
nvscic2c_pcie_epc_remove(struct pci_dev *pdev)
{
	int ret = 0;
	long timeout = 0;
	struct comm_msg msg = {0};
	struct driver_ctx_t *drv_ctx = NULL;

	if (!pdev)
		return;

	drv_ctx = pci_get_drvdata(pdev);
	if (!drv_ctx)
		return;

	/*
	 * send link down message to EPF. EPF apps can stop processing when
	 * they see this, else the EPF apps can continue processing as EPC
	 * waits for its apps to close before sending SHUTDOWN msg.
	 */
	msg.type = COMM_MSG_TYPE_LINK;
	msg.u.link.status = NVSCIC2C_PCIE_LINK_DOWN;
	ret = comm_channel_ctrl_msg_send(drv_ctx->comm_channel_h, &msg);
	if (ret)
		pr_err("(%s): Failed to send LINK(DOWN) message\n",
		       drv_ctx->drv_name);

	/* local apps can stop processing if they see this.*/
	pci_client_change_link_status(drv_ctx->pci_client_h,
				      NVSCIC2C_PCIE_LINK_DOWN);
	/*
	 * stop ongoing and pending edma xfers, this edma module shall not
	 * accept new xfer submissions after this.
	 */
	edma_module_stop(drv_ctx);

	/* wait for @DRV_MODE_EPC (local) endpoints to close. */
	ret = endpoints_waitfor_close(drv_ctx->endpoints_h);
	if (ret)
		pr_err("(%s): Error waiting for endpoints to close\n",
		       drv_ctx->drv_name);

	/*
	 * Jump to local deinit if any of below condition is true:
	 *  => if PCIe EP SoC went away abruptly already.
	 *  => if PCIe AER received.
	 */
	if (!pci_device_is_present(pdev) ||
	    atomic_read(&drv_ctx->epc_ctx->aer_received))
		goto deinit;

	/*
	 * Wait for @DRV_MODE_EPF to ACK it's closure too.
	 *
	 * Before this @DRV_MODE_EPC(this) endpoints must be closed already
	 * as @DRV_MODE_EPF in response to this msg shall free all it's
	 * endpoint mappings.
	 */
	reinit_completion(&drv_ctx->epc_ctx->epf_shutdown_cmpl);
	msg.type = COMM_MSG_TYPE_SHUTDOWN;
	if (comm_channel_ctrl_msg_send(drv_ctx->comm_channel_h, &msg)) {
		pr_err("(%s): Failed to send shutdown message\n",
		       drv_ctx->drv_name);
		goto deinit;
	}

	while (timeout == 0) {
		timeout = wait_for_completion_interruptible_timeout
				(&drv_ctx->epc_ctx->epf_shutdown_cmpl,
				 msecs_to_jiffies(MAX_EPF_SHUTDOWN_TIMEOUT_MSEC));
		if (timeout == -ERESTARTSYS) {
			pr_err("(%s): Wait for nvscic2c-pcie-epf to close - interrupted\n",
			       drv_ctx->drv_name);
		} else if (timeout == 0) {
			/*
			 * continue wait only if PCIe EP SoC is still there. It can
			 * go away abruptly waiting for it's own endpoints to close.
			 * Also check PCIe AER not received.
			 */
			if (!pci_device_is_present(pdev)) {
				pr_debug("(%s): nvscic2c-pcie-epf went away\n",
					 drv_ctx->drv_name);
				break;
			} else if (atomic_read(&drv_ctx->epc_ctx->aer_received)) {
				pr_debug("(%s): PCIe AER received\n",
					 drv_ctx->drv_name);
				break;
			}
			pr_err("(%s): Still waiting for nvscic2c-pcie-epf to close\n",
			       drv_ctx->drv_name);
		} else if (timeout > 0) {
			pr_debug("(%s): nvscic2c-pcie-epf closed\n",
				 drv_ctx->drv_name);
		}
	}

deinit:
	comm_channel_unregister_msg_cb(drv_ctx->comm_channel_h,
				       COMM_MSG_TYPE_LINK);
	endpoints_release(&drv_ctx->endpoints_h);
	edma_module_deinit(drv_ctx);
	vmap_deinit(&drv_ctx->vmap_h);
	comm_channel_deinit(&drv_ctx->comm_channel_h);
	pci_client_deinit(&drv_ctx->pci_client_h);
	free_outbound_area(pdev, &drv_ctx->peer_mem);
	free_inbound_area(pdev, &drv_ctx->self_mem);

	pci_release_regions(pdev);
	pci_clear_master(pdev);
#if defined(NV_PCI_DISABLE_PCIE_ERROR_REPORTING_PRESENT) /* Linux 6.5 */
	pci_disable_pcie_error_reporting(pdev);
#endif
	pci_disable_device(pdev);

	dt_release(&drv_ctx->drv_param);

	pci_set_drvdata(pdev, NULL);
	kfree(drv_ctx->epc_ctx);
	kfree_const(drv_ctx->drv_name);
	kfree(drv_ctx);
}

#define MAX_EPF_SETUP_TIMEOUT_MSEC	(5000)
static int
nvscic2c_pcie_epc_probe(struct pci_dev *pdev,
			const struct pci_device_id *id)
{
	int ret = 0;
	char *name = NULL;
	size_t win_size = 0;
	struct comm_msg msg = {0};
	unsigned long timeout = 0;
	struct callback_ops cb_ops = {0};
	struct driver_ctx_t *drv_ctx = NULL;
	struct epc_context_t *epc_ctx = NULL;
	struct pci_dev *ppdev = NULL;
	struct pci_client_params params = {0};
	u16 val_16;
	u32 val;

	/* allocate module context.*/
	drv_ctx = kzalloc(sizeof(*drv_ctx), GFP_KERNEL);
	if (WARN_ON(!drv_ctx))
		return -ENOMEM;

	name = kasprintf(GFP_KERNEL, "%s-%x", DRIVER_NAME_EPC, id->device);
	if (WARN_ON(!name)) {
		kfree(drv_ctx);
		return -ENOMEM;
	}

	epc_ctx = kzalloc(sizeof(*epc_ctx), GFP_KERNEL);
	if (WARN_ON(!epc_ctx)) {
		kfree(name);
		kfree(drv_ctx);
		return -ENOMEM;
	}

	drv_ctx->chip_id = __tegra_get_chip_id();
	if ((drv_ctx->chip_id != TEGRA234) && (drv_ctx->chip_id != TEGRA264)) {
		pr_err("(%s): NvSciC2C-Pcie not supported in chip\n",
		       name);
		kfree(epc_ctx);
		kfree(name);
		kfree(drv_ctx);
		return -EINVAL;
	}

	if (drv_ctx->chip_id == TEGRA234)
		drv_ctx->bar = 0;
	else
		drv_ctx->bar = 2;
	init_completion(&epc_ctx->epf_ready_cmpl);
	init_completion(&epc_ctx->epf_shutdown_cmpl);
	atomic_set(&epc_ctx->aer_received, 0);

	drv_ctx->drv_mode = DRV_MODE_EPC;
	drv_ctx->drv_name = name;

	ppdev = pcie_find_root_port(pdev);
	if (!ppdev) {
		kfree(epc_ctx);
		kfree(name);
		kfree(drv_ctx);
		return -ENODEV;
	}

	/*
	 * fdev: struct device associated with downstream to access Endpoint memories.
	 * cdev: struct device associated with upstream to access RootPort memories.
	 */
	drv_ctx->fdev = &pdev->dev;
	drv_ctx->cdev = &ppdev->dev;
	if ((!drv_ctx->cdev) || (!drv_ctx->fdev)) {
		kfree(epc_ctx);
		kfree(name);
		kfree(drv_ctx);
		return -ENODEV;
	}

	drv_ctx->epc_ctx = epc_ctx;
	pci_set_drvdata(pdev, drv_ctx);

	/* check for the device tree node against this Id, must be only one.*/
	ret = dt_parse(id->device, drv_ctx->drv_mode, &drv_ctx->drv_param);
	if (ret)
		goto err_dt_parse;

	ret = pcim_enable_device(pdev);
	if (ret)
		goto err_enable_device;
#if defined(NV_PCI_ENABLE_PCIE_ERROR_REPORTING_PRESENT) /* Linux 6.5 */
	pci_enable_pcie_error_reporting(pdev);
#endif
	pci_set_master(pdev);
	ret = pci_request_regions(pdev, MODULE_NAME);
	if (ret)
		goto err_request_region;

	win_size = pci_resource_len(pdev, drv_ctx->bar);
	ret = allocate_inbound_area(pdev, win_size, &drv_ctx->self_mem);
	if (ret)
		goto err_alloc_inbound;

	ret = assign_outbound_area(pdev, win_size, drv_ctx->bar, &drv_ctx->peer_mem);
	if (ret)
		goto err_assign_outbound;

	if (drv_ctx->chip_id == TEGRA264) {
		/* Allocating MSI's for DMA used in thor */
		ret = pci_alloc_irq_vectors(pdev, 16, 16, PCI_IRQ_MSI);
		if (ret < 0) {
			pr_err("Failed to enable MSI interrupt\n");
			ret = -ENODEV;
			goto err_alloc_irq;
		}

		/* Reading the msi_address, to write data and IRQ vector */
		pci_read_config_word(ppdev, ppdev->msi_cap + PCI_MSI_FLAGS, &val_16);
		if (val_16 & PCI_MSI_FLAGS_64BIT) {
			pci_read_config_dword(ppdev, ppdev->msi_cap + PCI_MSI_ADDRESS_HI, &val);
			drv_ctx->msi_addr = val;

			pci_read_config_word(ppdev, ppdev->msi_cap + PCI_MSI_DATA_64, &val_16);
			drv_ctx->msi_data = val_16;
		} else {
			pci_read_config_word(ppdev, ppdev->msi_cap + PCI_MSI_DATA_32, &val_16);
			drv_ctx->msi_data = val_16;
		}
		pci_read_config_dword(ppdev, ppdev->msi_cap + PCI_MSI_ADDRESS_LO, &val);
		drv_ctx->msi_addr = (drv_ctx->msi_addr << 32) | val;
		drv_ctx->msi_irq = pci_irq_vector(ppdev, TEGRA264_PCIE_DMA_MSI_LOCAL_VEC);
		drv_ctx->msi_data += TEGRA264_PCIE_DMA_MSI_LOCAL_VEC;
	}

	params.dev = &pdev->dev;
	params.cdev = drv_ctx->cdev;
	params.self_mem = &drv_ctx->self_mem;
	params.peer_mem = &drv_ctx->peer_mem;
	ret = pci_client_init(&params, &drv_ctx->pci_client_h);
	if (ret) {
		pr_err("(%s): pci_client_init() failed\n",
		       drv_ctx->drv_name);
		goto err_pci_client;
	}
	pci_client_save_driver_ctx(drv_ctx->pci_client_h, drv_ctx);
	pci_client_save_peer_cpu(drv_ctx->pci_client_h, NVCPU_ORIN);

	ret = comm_channel_init(drv_ctx, &drv_ctx->comm_channel_h);
	if (ret) {
		pr_err("(%s): Failed to initialize comm-channel\n",
		       drv_ctx->drv_name);
		goto err_comm_init;
	}

	ret = vmap_init(drv_ctx, &drv_ctx->vmap_h);
	if (ret) {
		pr_err("(%s): Failed to initialize vmap\n",
		       drv_ctx->drv_name);
		goto err_vmap_init;
	}

	ret = edma_module_init(drv_ctx);
	if (ret) {
		pr_err("(%s): Failed to initialize edma module\n",
		       drv_ctx->drv_name);
		goto err_edma_init;
	}

	ret = endpoints_setup(drv_ctx, &drv_ctx->endpoints_h);
	if (ret) {
		pr_err("(%s): Failed to initialize endpoints\n",
		       drv_ctx->drv_name);
		goto err_endpoints_init;
	}

	/* register for link status message from @DRV_MODE_EPF (PCIe EP).*/
	cb_ops.callback = link_msg_cb;
	cb_ops.ctx = (void *)drv_ctx;
	ret = comm_channel_register_msg_cb(drv_ctx->comm_channel_h,
					   COMM_MSG_TYPE_LINK, &cb_ops);
	if (ret) {
		pr_err("(%s): Failed to register for link message\n",
		       drv_ctx->drv_name);
		goto err_register_msg;
	}

	/*
	 * share iova with @DRV_MODE_EPF for it's outbound translation.
	 * This must be send only after comm-channel, endpoint memory backing
	 * is created and mapped to self_mem. @DRV_MODE_EPF on seeing this
	 * message shall send link-up message over comm-channel and possibly
	 * applications can also start endpoint negotiation, therefore.
	 */
	reinit_completion(&drv_ctx->epc_ctx->epf_ready_cmpl);
	msg.type = COMM_MSG_TYPE_BOOTSTRAP;
	msg.u.bootstrap.iova = drv_ctx->self_mem.dma_handle;
	msg.u.bootstrap.peer_cpu = NVCPU_ORIN;
	ret = comm_channel_ctrl_msg_send(drv_ctx->comm_channel_h, &msg);
	if (ret) {
		pr_err("(%s): Failed to send comm bootstrap message\n",
		       drv_ctx->drv_name);
		goto err_msg_send;
	}

	/* wait for @DRV_MODE_EPF to acknowledge it's endpoints are created.*/
	timeout =
	wait_for_completion_timeout(&drv_ctx->epc_ctx->epf_ready_cmpl,
				    msecs_to_jiffies(MAX_EPF_SETUP_TIMEOUT_MSEC));
	if (timeout == 0U) {
		ret = -ENOLINK;
		pr_err("(%s): Timed-out waiting for nvscic2c-pcie-epf\n",
		       drv_ctx->drv_name);
		goto err_epf_ready;
	}

	pci_set_drvdata(pdev, drv_ctx);
	return ret;

err_epf_ready:
err_msg_send:
	comm_channel_unregister_msg_cb(drv_ctx->comm_channel_h,
				       COMM_MSG_TYPE_LINK);
err_register_msg:
	endpoints_release(&drv_ctx->endpoints_h);

err_endpoints_init:
	edma_module_deinit(drv_ctx);

err_edma_init:
	vmap_deinit(&drv_ctx->vmap_h);

err_vmap_init:
	comm_channel_deinit(&drv_ctx->comm_channel_h);

err_comm_init:
	pci_client_deinit(&drv_ctx->pci_client_h);

err_pci_client:
	pci_free_irq_vectors(pdev);
err_alloc_irq:
	free_outbound_area(pdev, &drv_ctx->peer_mem);

err_assign_outbound:
	free_inbound_area(pdev, &drv_ctx->self_mem);

err_alloc_inbound:
	pci_release_regions(pdev);

err_request_region:
	pci_clear_master(pdev);
	pci_disable_device(pdev);

err_enable_device:
	dt_release(&drv_ctx->drv_param);

err_dt_parse:
	pci_set_drvdata(pdev, NULL);
	kfree(drv_ctx->epc_ctx);
	kfree_const(drv_ctx->drv_name);
	kfree(drv_ctx);
	return ret;
}

/*
 * Hot-replug is required to recover for both type of errors.
 * Hence we will return PCI_ERS_RESULT_DISCONNECT in both cases.
 */
static pci_ers_result_t
nvscic2c_pcie_error_detected(struct pci_dev *pdev,
			     pci_channel_state_t state)
{
	struct driver_ctx_t *drv_ctx = NULL;

	if (WARN_ON(!pdev))
		return PCI_ERS_RESULT_DISCONNECT;

	drv_ctx = pci_get_drvdata(pdev);
	if (WARN_ON(!drv_ctx))
		return PCI_ERS_RESULT_DISCONNECT;

	atomic_set(&drv_ctx->epc_ctx->aer_received, 1);
	if (state == pci_channel_io_normal) {
		pr_err("AER(NONFATAL) detected for dev %04x:%02x:%02x.%x\n",
		       pci_domain_nr(pdev->bus),
		       pdev->bus->number,
		       PCI_SLOT(pdev->devfn),
		       PCI_FUNC(pdev->devfn));
		(void)pci_client_set_link_aer_error(drv_ctx->pci_client_h,
						    NVSCIC2C_PCIE_AER_UNCORRECTABLE_NONFATAL);
	} else {
		if (state == pci_channel_io_frozen) {
			pr_err("AER: FATAL detected for dev %04x:%02x:%02x.%x\n",
			       pci_domain_nr(pdev->bus),
			       pdev->bus->number,
			       PCI_SLOT(pdev->devfn),
			       PCI_FUNC(pdev->devfn));
		} else {
			pr_err("Unknown error for dev %04x:%02x:%02x.%x treat as AER: FATAL\n",
			       pci_domain_nr(pdev->bus),
			       pdev->bus->number,
			       PCI_SLOT(pdev->devfn),
			       PCI_FUNC(pdev->devfn));
		}
		(void)pci_client_set_link_aer_error(drv_ctx->pci_client_h,
						    NVSCIC2C_PCIE_AER_UNCORRECTABLE_FATAL);
	}

	/* Mark PCIe Link down and notify all subscribers. */
	pci_client_change_link_status(drv_ctx->pci_client_h,
				      NVSCIC2C_PCIE_LINK_DOWN);

	return PCI_ERS_RESULT_DISCONNECT;
}

static struct pci_error_handlers nvscic2c_pcie_error_handlers = {
	.error_detected = nvscic2c_pcie_error_detected,
};

MODULE_DEVICE_TABLE(pci, nvscic2c_pcie_epc_tbl);
static struct pci_driver nvscic2c_pcie_epc_driver = {
	.name		= DRIVER_NAME_EPC,
	.id_table	= nvscic2c_pcie_epc_tbl,
	.probe		= nvscic2c_pcie_epc_probe,
	.remove		= nvscic2c_pcie_epc_remove,
	.shutdown	= nvscic2c_pcie_epc_remove,
	.err_handler    = &nvscic2c_pcie_error_handlers,
};
module_pci_driver(nvscic2c_pcie_epc_driver);

#define DRIVER_LICENSE		"GPL v2"
#define DRIVER_DESCRIPTION	"NVIDIA Chip-to-Chip transfer module for PCIeRP"
#define DRIVER_AUTHOR		"Nvidia Corporation"
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_LICENSE(DRIVER_LICENSE);
MODULE_AUTHOR(DRIVER_AUTHOR);
