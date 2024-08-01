// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#define pr_fmt(fmt)	"nvscic2c-pcie: epf: " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pci-epc.h>
#include <linux/pci-epf.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/tegra-pcie-edma.h>
#include <linux/types.h>

#include "comm-channel.h"
#include "common.h"
#include "endpoint.h"
#include "iova-alloc.h"
#include "module.h"
#include "pci-client.h"
#include "vmap.h"

static const struct pci_epf_device_id nvscic2c_pcie_epf_ids[] = {
	{
		.name = "nvscic2c_epf_22CB",
		.driver_data = (kernel_ulong_t)PCI_DEVICE_ID_C2C_1,
	},
	{
		.name = "nvscic2c_epf_22CC",
		.driver_data = (kernel_ulong_t)PCI_DEVICE_ID_C2C_2,
	},
	{
		.name = "nvscic2c_epf_22CD",
		.driver_data = (kernel_ulong_t)PCI_DEVICE_ID_C2C_3,
	},
	{},
};

/* wrapper over tegra-pcie-edma init api. */
static int
edma_module_init(struct driver_ctx_t *drv_ctx)
{
	u8 i = 0;
	int ret = 0;
	struct tegra_pcie_edma_init_info info = {0};

	if (WARN_ON(!drv_ctx || !drv_ctx->drv_param.edma_np))
		return -EINVAL;

	memset(&info, 0x0, sizeof(info));
	info.np = drv_ctx->drv_param.edma_np;
	info.edma_remote = NULL;

	for (i = 0; i < DMA_WR_CHNL_NUM; i++) {
		info.tx[i].ch_type = EDMA_CHAN_XFER_ASYNC;
		info.tx[i].num_descriptors = NUM_EDMA_DESC;
	}
	/*No use-case for RD channels.*/

	drv_ctx->edma_h = tegra_pcie_edma_initialize(&info);
	if (!drv_ctx->edma_h)
		ret = -ENODEV;

	return ret;
}

/* should stop any ongoing eDMA transfers.*/
static void
edma_module_stop(struct driver_ctx_t *drv_ctx)
{
	if (!drv_ctx || !drv_ctx->edma_h)
		return;

	tegra_pcie_edma_stop(drv_ctx->edma_h);
}

/* should not have any ongoing eDMA transfers.*/
static void
edma_module_deinit(struct driver_ctx_t *drv_ctx)
{
	if (!drv_ctx || !drv_ctx->edma_h)
		return;

	tegra_pcie_edma_deinit(drv_ctx->edma_h);
	drv_ctx->edma_h = NULL;
}

static void
free_inbound_area(struct pci_epf *epf, struct dma_buff_t *self_mem)
{
	struct driver_ctx_t *drv_ctx = NULL;

	if (!epf || !self_mem)
		return;

	drv_ctx = epf_get_drvdata(epf);
	if (!drv_ctx)
		return;

	if (self_mem->dma_handle && drv_ctx->ivd_h)
		iova_alloc_deinit(self_mem->dma_handle, self_mem->size,
				  &drv_ctx->ivd_h);
	self_mem->dma_handle = 0x0;
}

/*
 * Allocate BAR backing iova region. Writes from peer SoC shall
 * land in this region for it to be forwarded to system iommu to eventually
 * land in BAR backing physical region.
 */
static int
allocate_inbound_area(struct pci_epf *epf, size_t win_size,
		      struct dma_buff_t *self_mem)
{
	int ret = 0;
	struct driver_ctx_t *drv_ctx = NULL;

	drv_ctx = epf_get_drvdata(epf);
	if (!drv_ctx)
		return -ENOMEM;

	self_mem->size = win_size;
	ret = iova_alloc_init(epf->epc->dev.parent, win_size,
			      &self_mem->dma_handle, &drv_ctx->ivd_h);
	if (ret) {
		pr_err("iova_domain_init() failed for size:(0x%lx)\n",
		       self_mem->size);
	}

	return ret;
}

static void
free_outbound_area(struct pci_epf *epf, struct pci_aper_t *peer_mem)
{
	if (!epf || !peer_mem || !peer_mem->pva)
		return;

	pci_epc_mem_free_addr(epf->epc, peer_mem->aper,
			      peer_mem->pva,
			      peer_mem->size);
	peer_mem->pva = NULL;
}

/*
 * Allocate outbound pcie aperture for CPU access towards PCIe RP.
 * It is assumed that PCIe RP shall also allocate an equivalent size of inbound
 * area as PCIe EP (it's BAR0 length).
 */
static int
allocate_outbound_area(struct pci_epf *epf, size_t win_size,
		       struct pci_aper_t *peer_mem)
{
	int ret = 0;

	peer_mem->size = win_size;
	peer_mem->pva = pci_epc_mem_alloc_addr(epf->epc,
					       &peer_mem->aper,
					       peer_mem->size);
	if (!peer_mem->pva) {
		ret = -ENOMEM;
		pr_err("pci_epc_mem_alloc_addr() fails for size:(0x%lx)\n",
		       peer_mem->size);
	}

	return ret;
}

#if defined(NV_PCI_EPC_EVENT_OPS_STRUCT_HAS_EPC_DEINIT) || \
    defined(NV_PCI_EPC_EVENT_OPS_STRUCT_HAS_CORE_DEINIT) /* Linux v6.11 || Nvidia Internal */
static void
clear_inbound_translation(struct pci_epf *epf)
{
	struct pci_epf_bar *epf_bar = &epf->bar[BAR_0];

	pci_epc_clear_bar(epf->epc, epf->func_no, PCIE_VFNO, epf_bar);

	/* no api to clear epf header.*/
}
#endif

static int
set_inbound_translation(struct pci_epf *epf)
{
	int ret = 0;
	struct pci_epc *epc = epf->epc;
	struct pci_epf_bar *epf_bar = &epf->bar[BAR_0];

	ret = pci_epc_write_header(epc, epf->func_no, PCIE_VFNO, epf->header);
	if (ret < 0) {
		pr_err("Failed to write PCIe header\n");
		return ret;
	}

	/* BAR:0 setttings are already done in _bind().*/
	ret = pci_epc_set_bar(epc, epf->func_no, PCIE_VFNO, epf_bar);
	if (ret) {
		pr_err("pci_epc_set_bar() failed\n");
		return ret;
	}

	ret = pci_epc_set_msi(epc, epf->func_no, PCIE_VFNO,
			      epf->msi_interrupts);
	if (ret) {
		pr_err("pci_epc_set_msi() failed\n");
		return ret;
	}

	return ret;
}

static void
clear_outbound_translation(struct pci_epf *epf, struct pci_aper_t *peer_mem)
{
	return pci_epc_unmap_addr(epf->epc, epf->func_no, PCIE_VFNO,
				  peer_mem->aper);
}

static int
set_outbound_translation(struct pci_epf *epf, struct pci_aper_t *peer_mem,
			 u64 peer_iova)
{
	return pci_epc_map_addr(epf->epc, epf->func_no, PCIE_VFNO,
				peer_mem->aper,	peer_iova, peer_mem->size);
}

static void
edma_rx_desc_iova_send(struct driver_ctx_t *drv_ctx)
{
	int ret;
	struct comm_msg msg = {0};

	msg.type = COMM_MSG_TYPE_EDMA_RX_DESC_IOVA_RETURN;
	msg.u.edma_rx_desc_iova.iova = pci_client_get_edma_rx_desc_iova(drv_ctx->pci_client_h);

	ret = comm_channel_ctrl_msg_send(drv_ctx->comm_channel_h, &msg);
	if (ret)
		pr_err("failed sending COMM_MSG_TYPE_EDMA_CH_DESC_IOVA_RETURN  message\n");
}

/* Handle bootstrap message from @DRV_MODE_EPC. */
static void
bootstrap_msg_cb(void *data, void *ctx)
{
	int ret = 0;
	struct driver_ctx_t *drv_ctx = NULL;
	struct pci_epf *epf = (struct pci_epf *)ctx;
	struct comm_msg *msg = (struct comm_msg *)data;

	if (WARN_ON(!msg || !epf))
		return;

	drv_ctx = epf_get_drvdata(epf);
	if (!drv_ctx)
		return;

	/*
	 * setup outbound translation for CPU access from @DRV_MODE_EPF ->
	 * @DRV_MODE_EPC using the iova received from @DRV_MODE_EPC in
	 * bootstrap message.
	 *
	 * Must be done here, as return of the comm-channel message callback
	 * shall use CPU on @DRV_MODE_EPF to indicate message read.
	 */
	ret = set_outbound_translation(epf, &drv_ctx->peer_mem,
				       msg->u.bootstrap.iova);
	if (ret) {
		pr_err("Failed to set outbound (peer) memory translation\n");
		return;
	}

	pci_client_save_peer_cpu(drv_ctx->pci_client_h, msg->u.bootstrap.peer_cpu);

	/* send edma rx desc iova  to x86 peer(rp) */
	if (msg->u.bootstrap.peer_cpu == NVCPU_X86_64)
		edma_rx_desc_iova_send(drv_ctx);

	/*
	 * schedule initialization of remaining interfaces as it could not
	 * be done in _notifier()(PCIe EP controller is still uninitialized
	 * then). Also abstraction: vmap registers with comm-channel, such
	 * callback registrations cannot happen while in the context of
	 * another comm-channel callback (this function).
	 */
	schedule_work(&drv_ctx->epf_ctx->initialization_work);
}

/*
 * tasklet/scheduled work for initialization of remaining interfaces
 * (that which could not be done in _bind(), Reason: endpoint abstraction
 *  requires:
 *   - peer iova - not available unless bootstrap message.
 *   - edma cookie - cannot be done during _notifier() - interrupt context).
 * )
 */
static void
init_work(struct work_struct *work)
{
	int ret = 0;
	struct comm_msg msg = {0};
	struct epf_context_t *epf_ctx =
		container_of(work, struct epf_context_t, initialization_work);
	struct driver_ctx_t *drv_ctx = (struct driver_ctx_t *)epf_ctx->drv_ctx;

	if (atomic_read(&drv_ctx->epf_ctx->epf_initialized)) {
		pr_err("(%s): Already initialized\n", drv_ctx->drv_name);
		goto err;
	}

	ret = vmap_init(drv_ctx, &drv_ctx->vmap_h);
	if (ret) {
		pr_err("(%s): vmap_init() failed\n", drv_ctx->drv_name);
		goto err;
	}

	ret = edma_module_init(drv_ctx);
	if (ret) {
		pr_err("(%s): edma_module_init() failed\n", drv_ctx->drv_name);
		goto err_edma_init;
	}

	ret = endpoints_setup(drv_ctx, &drv_ctx->endpoints_h);
	if (ret) {
		pr_err("(%s): endpoints_setup() failed\n", drv_ctx->drv_name);
		goto err_endpoint;
	}

	/*
	 * this is an acknowledgment to @DRV_MODE_EPC in response to it's
	 * bootstrap message to indicate @DRV_MODE_EPF endpoints are ready.
	 */
	msg.type = COMM_MSG_TYPE_LINK;
	msg.u.link.status = NVSCIC2C_PCIE_LINK_UP;
	ret = comm_channel_ctrl_msg_send(drv_ctx->comm_channel_h, &msg);
	if (ret) {
		pr_err("(%s): Failed to send LINK(UP) message\n",
		       drv_ctx->drv_name);
		goto err_msg_send;
	}

	/* inidicate link-up to applications.*/
	atomic_set(&drv_ctx->epf_ctx->epf_initialized, 1);
	pci_client_change_link_status(drv_ctx->pci_client_h,
				      NVSCIC2C_PCIE_LINK_UP);
	return;

err_msg_send:
	endpoints_release(&drv_ctx->endpoints_h);
err_endpoint:
	edma_module_deinit(drv_ctx);
err_edma_init:
	vmap_deinit(&drv_ctx->vmap_h);
err:
	return;
}

/*
 * PCIe subsystem calls struct pci_epc_event_ops.core_init
 * when PCIe hot-plug is initiated and before link trainig
 * starts with PCIe RP SoC (before @DRV_MODE_EPC .probe() handler is invoked).
 *
 * Because, CORE_INIT impacts link training timeout, it shall do only minimum
 * required for @DRV_MODE_EPF for PCIe EP initialization.
 *
 * This is received in interrupt context.
 */
static int
nvscic2c_pcie_epf_core_init(struct pci_epf *epf)
{
	int ret = 0;
	struct driver_ctx_t *drv_ctx = NULL;

	drv_ctx = epf_get_drvdata(epf);
	if (!drv_ctx)
		return -EINVAL;

	if (atomic_read(&drv_ctx->epf_ctx->core_initialized)) {
		pr_err("(%s): Received CORE_INIT callback again\n",
		       drv_ctx->drv_name);
		return -EINVAL;
	}

	ret = set_inbound_translation(epf);
	if (ret) {
		pr_err("(%s): set_inbound_translation() failed\n",
		       drv_ctx->drv_name);
		return ret;
	}

	atomic_set(&drv_ctx->epf_ctx->core_initialized, 1);

	return ret;
}

/* Handle link message from @DRV_MODE_EPC. */
static void
shutdown_msg_cb(void *data, void *ctx)
{
	struct driver_ctx_t *drv_ctx = NULL;
	struct pci_epf *epf = (struct pci_epf *)ctx;
	struct comm_msg *msg = (struct comm_msg *)data;

	if (WARN_ON(!msg || !epf))
		return;

	drv_ctx = epf_get_drvdata(epf);
	if (!drv_ctx)
		return;

	if (!atomic_read(&drv_ctx->epf_ctx->epf_initialized)) {
		pr_err("(%s): Unexpected shutdown msg from nvscic2c-pcie-epc\n",
		       drv_ctx->drv_name);
		return;
	}

	atomic_set(&drv_ctx->epf_ctx->shutdown_msg_received, 1);
	/* schedule deinitialization of epf interfaces. */
	schedule_work(&drv_ctx->epf_ctx->deinitialization_work);
}

/*
 * tasklet/scheduled work for de-initialization of @DRV_MODE_EPF(this)
 * interfaces. It is done in a tasklet for the following scenario:
 * @DRV_MODE_EPC can get unloaded(rmmod) and reinserted(insmod) while the
 * PCIe link with PCIe EP SoC still active. So before we receive
 * bootstrap message again when @DRV_MODE_EPC is reinserted, we would need
 * to clean-up all abstractions before they can be reinit again.
 *
 * In case of abnormal shutdown of PCIe RP SoC, @DRV_MODE_EPF shall receive
 * CORE_DEINIT directly from PCIe sub-system without any comm-message from
 * @DRV_MODE_EPC.
 */
static void
deinit_work(struct work_struct *work)
{
	int ret = 0;
	struct comm_msg msg = {0};
	struct epf_context_t *epf_ctx =
		container_of(work, struct epf_context_t, deinitialization_work);
	struct driver_ctx_t *drv_ctx = (struct driver_ctx_t *)epf_ctx->drv_ctx;

	if (!atomic_read(&drv_ctx->epf_ctx->epf_initialized))
		return;

	/* local apps can stop processing if they see this.*/
	pci_client_change_link_status(drv_ctx->pci_client_h,
				      NVSCIC2C_PCIE_LINK_DOWN);
	/*
	 * stop ongoing and pending edma xfers, this edma module shall not
	 * accept new xfer submissions after this.
	 */
	edma_module_stop(drv_ctx);

	/* wait for @DRV_MODE_EPF (local)endpoints to close. */
	ret = endpoints_waitfor_close(drv_ctx->endpoints_h);
	if (ret) {
		pr_err("(%s): Error waiting for endpoints to close\n",
		       drv_ctx->drv_name);
	}
	/* Even in case of error, continue to deinit - cannot be recovered.*/

	/*
	 * Acknowledge @DRV_MODE_EPC that @DRV_MODE_EPF(this) endpoints are
	 * closed if shutdown message was received from @DRV_MODE_EPC.
	 * If @DRV_MODE_EPC went abruptly or AER was generated, @DRV_MODE_EPC
	 * will not send shutdown message.
	 */
	if (atomic_read(&drv_ctx->epf_ctx->shutdown_msg_received)) {
		msg.type = COMM_MSG_TYPE_LINK;
		msg.u.link.status = NVSCIC2C_PCIE_LINK_DOWN;
		ret = comm_channel_ctrl_msg_send(drv_ctx->comm_channel_h, &msg);
		if (ret)
			pr_err("(%s): Failed to send LINK (DOWN) message\n",
			       drv_ctx->drv_name);
		atomic_set(&drv_ctx->epf_ctx->shutdown_msg_received, 0);
	}

	endpoints_release(&drv_ctx->endpoints_h);
	edma_module_deinit(drv_ctx);
	vmap_deinit(&drv_ctx->vmap_h);
	clear_outbound_translation(drv_ctx->epf_ctx->epf, &drv_ctx->peer_mem);
	atomic_set(&drv_ctx->epf_ctx->epf_initialized, 0);
}

/*
 * Graceful shutdown: PCIe subsystem calls struct pci_epc_event_ops.core_deinit
 * when @DRV_MODE_EPC .remove() or .shutdown() handlers are completed/exited.
 * Abnormal shutdown (when PCIe RP SoC - gets halted, or it's kernel oops):
 * PCIe subsystem also struct pci_epc_event_ops.core_deinit but
 * @DRV_MODE_EPC would have already gone then by the time
 * struct pci_epc_event_ops.core_deinit is called.
 */
#if defined(NV_PCI_EPC_EVENT_OPS_STRUCT_HAS_EPC_DEINIT) || \
    defined(NV_PCI_EPC_EVENT_OPS_STRUCT_HAS_CORE_DEINIT) /* Linux v6.11 || Nvidia Internal */
static int
nvscic2c_pcie_epf_core_deinit(struct pci_epf *epf)
{
	struct driver_ctx_t *drv_ctx = NULL;

	drv_ctx = epf_get_drvdata(epf);
	if (!drv_ctx)
		return -EINVAL;

	if (atomic_read(&drv_ctx->epf_ctx->core_initialized)) {
		/*
		 * in case of PCIe RP SoC abnormal shutdown, comm-channel
		 * shutdown message from @DRV_MODE_EPC won't come and
		 * therefore scheduling the deinit work here is required
		 * If its already scheduled, it won't be scheduled again.
		 * Wait for deinit work to complete in either case.
		 */
		schedule_work(&drv_ctx->epf_ctx->deinitialization_work);
		flush_work(&drv_ctx->epf_ctx->deinitialization_work);

		clear_inbound_translation(epf);
		atomic_set(&drv_ctx->epf_ctx->core_initialized, 0);
	}
	wake_up_interruptible_all(&drv_ctx->epf_ctx->core_initialized_waitq);

	return 0;
}

#if defined(NV_PCI_EPC_EVENT_OPS_STRUCT_HAS_EPC_DEINIT)
static void nvscic2c_pcie_epf_epc_deinit(struct pci_epf *epf)
{
	WARN_ON(nvscic2c_pcie_epf_core_deinit(epf));
}
#endif
#endif

/* Handle link message from @DRV_MODE_EPC. */
static void
link_msg_cb(void *data, void *ctx)
{
	struct pci_epf *epf = (struct pci_epf *)ctx;
	struct comm_msg *msg = (struct comm_msg *)data;
	struct driver_ctx_t *drv_ctx = (struct driver_ctx_t *)ctx;

	if (WARN_ON(!msg || !epf))
		return;

	drv_ctx = epf_get_drvdata(epf);
	if (!drv_ctx)
		return;

	if (msg->u.link.status != NVSCIC2C_PCIE_LINK_DOWN) {
		pr_err("(%s): spurious link message received from EPC\n",
		       drv_ctx->drv_name);
		return;
	}

	/* inidicate link status to application.*/
	pci_client_change_link_status(drv_ctx->pci_client_h,
				      msg->u.link.status);
}

/*
 * ASSUMPTION: applications on and @DRV_MODE_EPC(PCIe RP) must have stopped
 * communicating with application and @DRV_MODE_EPF (this) before this point.
 */
static void
nvscic2c_pcie_epf_unbind(struct pci_epf *epf)
{
	long ret = 0;
	struct driver_ctx_t *drv_ctx = NULL;

	if (!epf)
		return;

	drv_ctx = epf_get_drvdata(epf);
	if (!drv_ctx)
		return;

	/* timeout should be higher than that of endpoints to close.*/
	ret = wait_event_interruptible
			(drv_ctx->epf_ctx->core_initialized_waitq,
			 !(atomic_read(&drv_ctx->epf_ctx->core_initialized)));
	if (ret == -ERESTARTSYS)
		pr_err("(%s): Interrupted waiting for CORE_DEINIT to complete\n",
		       drv_ctx->drv_name);

	comm_channel_unregister_msg_cb(drv_ctx->comm_channel_h,
				       COMM_MSG_TYPE_SHUTDOWN);
	comm_channel_unregister_msg_cb(drv_ctx->comm_channel_h,
				       COMM_MSG_TYPE_BOOTSTRAP);
	comm_channel_deinit(&drv_ctx->comm_channel_h);
	pci_client_deinit(&drv_ctx->pci_client_h);
	free_outbound_area(epf, &drv_ctx->peer_mem);
	free_inbound_area(epf, &drv_ctx->self_mem);
}

static int
nvscic2c_pcie_epf_bind(struct pci_epf *epf)
{
	int ret = 0;
	size_t win_size = 0;
	struct pci_epf_bar *epf_bar = NULL;
	struct driver_ctx_t *drv_ctx = NULL;
	struct pci_client_params params = {0};
	struct callback_ops cb_ops = {0};

	if (!epf)
		return -EINVAL;

	drv_ctx = epf_get_drvdata(epf);
	if (!drv_ctx)
		return -EINVAL;

	/*
	 * device-tree node has edma phandle, user must bind
	 * the function to the same pcie controller.
	 */
	if (drv_ctx->drv_param.edma_np != epf->epc->dev.parent->of_node) {
		pr_err("epf:(%s) is not bounded to correct controller\n",
		       epf->name);
		return -EINVAL;
	}

	win_size = drv_ctx->drv_param.bar_win_size;
	ret = allocate_inbound_area(epf, win_size, &drv_ctx->self_mem);
	if (ret)
		goto err_alloc_inbound;

	ret = allocate_outbound_area(epf, win_size, &drv_ctx->peer_mem);
	if (ret)
		goto err_alloc_outbound;

	params.dev = epf->epc->dev.parent;
	params.self_mem = &drv_ctx->self_mem;
	params.peer_mem = &drv_ctx->peer_mem;
	ret = pci_client_init(&params, &drv_ctx->pci_client_h);
	if (ret) {
		pr_err("pci_client_init() failed\n");
		goto err_pci_client;
	}
	pci_client_save_driver_ctx(drv_ctx->pci_client_h, drv_ctx);
	/*
	 * setup of comm-channel must be done in bind() for @DRV_MODE_EPC
	 * to share bootstrap message. register for message from @DRV_MODE_EPC
	 * (PCIe RP).
	 */
	ret = comm_channel_init(drv_ctx, &drv_ctx->comm_channel_h);
	if (ret) {
		pr_err("Failed to initialize comm-channel\n");
		goto err_comm_init;
	}

	/* register for bootstrap message from @DRV_MODE_EPC (PCIe RP).*/
	cb_ops.callback = bootstrap_msg_cb;
	cb_ops.ctx = (void *)epf;
	ret = comm_channel_register_msg_cb(drv_ctx->comm_channel_h,
					   COMM_MSG_TYPE_BOOTSTRAP, &cb_ops);
	if (ret) {
		pr_err("Failed to register for bootstrap message from RP\n");
		goto err_register_msg;
	}

	/* register for shutdown message from @DRV_MODE_EPC (PCIe RP).*/
	memset(&cb_ops, 0x0, sizeof(cb_ops));
	cb_ops.callback = shutdown_msg_cb;
	cb_ops.ctx = (void *)epf;
	ret = comm_channel_register_msg_cb(drv_ctx->comm_channel_h,
					   COMM_MSG_TYPE_SHUTDOWN, &cb_ops);
	if (ret) {
		pr_err("Failed to register for shutdown message from RP\n");
		goto err_register_msg;
	}

	/* register for link message from @DRV_MODE_EPC (PCIe RP).*/
	memset(&cb_ops, 0x0, sizeof(cb_ops));
	cb_ops.callback = link_msg_cb;
	cb_ops.ctx = (void *)epf;
	ret = comm_channel_register_msg_cb(drv_ctx->comm_channel_h,
					   COMM_MSG_TYPE_LINK, &cb_ops);
	if (ret) {
		pr_err("Failed to register for link message from RP\n");
		goto err_register_msg;
	}

	/* BAR:0 settings. - done here to save time in CORE_INIT.*/
	epf_bar = &epf->bar[BAR_0];
	epf_bar->phys_addr = drv_ctx->self_mem.dma_handle;
	epf_bar->size = drv_ctx->self_mem.size;
	epf_bar->barno = BAR_0;
	epf_bar->flags |= PCI_BASE_ADDRESS_SPACE_MEMORY |
			  PCI_BASE_ADDRESS_MEM_TYPE_64 |
			  PCI_BASE_ADDRESS_MEM_PREFETCH;

	return ret;

err_register_msg:
	comm_channel_deinit(&drv_ctx->comm_channel_h);

err_comm_init:
	pci_client_deinit(&drv_ctx->pci_client_h);

err_pci_client:
	free_outbound_area(epf, &drv_ctx->peer_mem);

err_alloc_outbound:
	free_inbound_area(epf, &drv_ctx->self_mem);

err_alloc_inbound:
	return ret;
}

static void
nvscic2c_pcie_epf_remove(struct pci_epf *epf)
{
	struct driver_ctx_t *drv_ctx = epf_get_drvdata(epf);

	if (!drv_ctx)
		return;

	cancel_work_sync(&drv_ctx->epf_ctx->deinitialization_work);
	cancel_work_sync(&drv_ctx->epf_ctx->initialization_work);
	epf->header = NULL;
	kfree(drv_ctx->epf_ctx);

	dt_release(&drv_ctx->drv_param);

	epf_set_drvdata(epf, NULL);
	kfree_const(drv_ctx->drv_name);
	kfree(drv_ctx);
}

static kernel_ulong_t
get_driverdata(const struct pci_epf_device_id *id,
	       const struct pci_epf *epf)
{
	while (id->name[0]) {
		if (strcmp(epf->name, id->name) == 0)
			return id->driver_data;
		id++;
	}

	return 0;
}

static const struct pci_epc_event_ops nvscic2c_event_ops = {
#if defined(NV_PCI_EPC_EVENT_OPS_STRUCT_HAS_EPC_INIT) /* Linux v6.11 */
	.epc_init = nvscic2c_pcie_epf_core_init,
#else
	.core_init = nvscic2c_pcie_epf_core_init,
#endif
#if defined(NV_PCI_EPC_EVENT_OPS_STRUCT_HAS_EPC_DEINIT) /* Linux v6.11 */
	.epc_deinit = nvscic2c_pcie_epf_epc_deinit,
#elif defined(NV_PCI_EPC_EVENT_OPS_STRUCT_HAS_CORE_DEINIT) /* Nvidia Internal */
	.core_deinit = nvscic2c_pcie_epf_core_deinit,
#endif
};

static int
#if defined(NV_PCI_EPF_DRIVER_STRUCT_PROBE_HAS_ID_ARG) /* Linux 6.4 */
nvscic2c_pcie_epf_probe(struct pci_epf *epf, const struct pci_epf_device_id *id)
#else
nvscic2c_pcie_epf_probe(struct pci_epf *epf)
#endif
{
	int ret = 0;
	char *name = NULL;
	u32 pci_dev_id = 0x0;
	struct driver_ctx_t *drv_ctx = NULL;
	struct epf_context_t *epf_ctx = NULL;

	/* get pci device id from epf name.*/
	pci_dev_id = (u32)get_driverdata(nvscic2c_pcie_epf_ids, epf);
	if (!pci_dev_id)
		return -EINVAL;

	/* allocate module context.*/
	drv_ctx = kzalloc(sizeof(*drv_ctx), GFP_KERNEL);
	if (WARN_ON(!drv_ctx))
		return -ENOMEM;

	name = kasprintf(GFP_KERNEL, "%s-%x", DRIVER_NAME_EPF, pci_dev_id);
	if (WARN_ON(!name)) {
		kfree(drv_ctx);
		return -ENOMEM;
	}

	drv_ctx->drv_mode = DRV_MODE_EPF;
	drv_ctx->drv_name = name;
	epf_set_drvdata(epf, drv_ctx);

	/* check for the device tree node against this Id, must be only one.*/
	ret = dt_parse(pci_dev_id, drv_ctx->drv_mode, &drv_ctx->drv_param);
	if (ret)
		goto err_dt_parse;

	/* allocate nvscic2c-pcie epf only context.*/
	epf_ctx = kzalloc(sizeof(*epf_ctx), GFP_KERNEL);
	if (WARN_ON(!epf_ctx)) {
		ret = -ENOMEM;
		goto err_alloc_epf_ctx;
	}
	drv_ctx->epf_ctx = epf_ctx;
	epf_ctx->header.vendorid = PCI_VENDOR_ID_NVIDIA;
	epf_ctx->header.deviceid = pci_dev_id;
	epf_ctx->header.baseclass_code = PCI_BASE_CLASS_COMMUNICATION;
	epf_ctx->header.interrupt_pin = PCI_INTERRUPT_INTA;

	epf->event_ops = &nvscic2c_event_ops;
	epf->header = &epf_ctx->header;

	/* to initialize NvSciC2cPcie interfaces on bootstrap msg.*/
	epf_ctx->drv_ctx = drv_ctx;
	epf_ctx->epf = epf;
	INIT_WORK(&epf_ctx->initialization_work, init_work);
	INIT_WORK(&epf_ctx->deinitialization_work, deinit_work);

	/* to synchronize deinit, unbind.*/
	atomic_set(&epf_ctx->core_initialized, 0);
	atomic_set(&drv_ctx->epf_ctx->epf_initialized, 0);
	init_waitqueue_head(&epf_ctx->core_initialized_waitq);

	/* to check if shutdown message response required. */
	atomic_set(&epf_ctx->shutdown_msg_received, 0);

	return ret;

err_alloc_epf_ctx:
	dt_release(&drv_ctx->drv_param);

err_dt_parse:
	epf_set_drvdata(epf, NULL);
	kfree_const(drv_ctx->drv_name);
	kfree(drv_ctx);

	return ret;
}

static struct pci_epf_ops ops = {
	.unbind = nvscic2c_pcie_epf_unbind,
	.bind   = nvscic2c_pcie_epf_bind,
};

static struct pci_epf_driver nvscic2c_pcie_epf_driver = {
	.driver.name = DRIVER_NAME_EPF,
	.probe       = nvscic2c_pcie_epf_probe,
	.remove      = nvscic2c_pcie_epf_remove,
	.id_table    = nvscic2c_pcie_epf_ids,
	.ops         = &ops,
	.owner       = THIS_MODULE,
};

static int
__init nvscic2c_pcie_epf_init(void)
{
	return pci_epf_register_driver(&nvscic2c_pcie_epf_driver);
}
module_init(nvscic2c_pcie_epf_init);

static void
__exit nvscic2c_pcie_epf_deinit(void)
{
	pci_epf_unregister_driver(&nvscic2c_pcie_epf_driver);
}
module_exit(nvscic2c_pcie_epf_deinit);

#define DRIVER_LICENSE		"GPL v2"
#define DRIVER_DESCRIPTION	"NVIDIA Chip-to-Chip transfer module for PCIeEP"
#define DRIVER_AUTHOR		"Nvidia Corporation"
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_LICENSE(DRIVER_LICENSE);
MODULE_AUTHOR(DRIVER_AUTHOR);
