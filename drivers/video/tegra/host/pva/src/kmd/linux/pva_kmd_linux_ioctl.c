/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, NVIDIA Corporation.  All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation.  Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */
/* Auto-detected configuration depending kernel version */
#include <nvidia/conftest.h>

#include <linux/err.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/kref.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/version.h>
#include <linux/nvhost.h>

#include "pva_kmd_linux.h"
#include "pva_kmd_linux_device.h"
#include "pva_kmd_op_handler.h"

/**
 * Struct to hold context pertaining to open/close/ioctl calls
*/
struct pva_kmd_linux_ocb {
	struct pva_kmd_context
		*kmd_ctx; /* Stores pva_kmd_context to be assigned per client*/
	u8 req_buffer[PVA_KMD_MAX_OP_BUFFER_SIZE]; /* Buffer to copy request op from user */
	u8 resp_buffer
		[PVA_KMD_MAX_OP_BUFFER_SIZE]; /* Buffer to copy response from kernel to user */
};

static inline bool
is_ioctl_header_valid(const struct pva_kmd_linux_ioctl_header *hdr)
{
	return ((hdr->request.addr != NULL) && (hdr->response.addr != NULL) &&
		(hdr->request.size != 0U) && (hdr->response.size != 0U) &&
		(hdr->request.size <= PVA_KMD_MAX_OP_BUFFER_SIZE) &&
		(hdr->response.size <= PVA_KMD_MAX_OP_BUFFER_SIZE));
}

static long pva_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct pva_kmd_linux_ocb *ocb = file->private_data;
	u8 buf[NVPVA_IOCTL_MAX_SIZE] __aligned(sizeof(u64));
	u32 resp_size = 0;
	enum pva_error op_err = PVA_SUCCESS;
	int err = 0;
	int ret_err = 0;
	u8 cmd_size = _IOC_SIZE(cmd);
	struct pva_kmd_linux_ioctl_header *hdr = NULL;
	int req_ok, resp_ok;

	if ((cmd != PVA_KMD_IOCTL_GENERIC) || (cmd_size > sizeof(buf))) {
		return -ENOIOCTLCMD;
	}

	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		if (copy_from_user(buf, (void __user *)arg, cmd_size)) {
			return -EFAULT;
		}
	}

	hdr = (struct pva_kmd_linux_ioctl_header *)(void *)buf;
	if (!is_ioctl_header_valid(hdr)) {
		return -EINVAL;
	}

	req_ok = access_ok((void __user *)hdr->request.addr,
			   (unsigned long)hdr->request.size);
	resp_ok = access_ok((void __user *)hdr->response.addr,
			    (unsigned long)hdr->response.size);

	if ((req_ok != 1) || (resp_ok != 1)) {
		return -EFAULT;
	}

	err = copy_from_user(ocb->req_buffer, (void __user *)hdr->request.addr,
			     hdr->request.size);
	if (err) {
		return err;
	}

	op_err = pva_kmd_ops_handler(ocb->kmd_ctx, ocb->req_buffer,
				     hdr->request.size, ocb->resp_buffer,
				     hdr->response.size, &resp_size);

	if (op_err != PVA_SUCCESS) {
		if (op_err == PVA_NO_RESOURCE_ID || op_err == PVA_NOMEM) {
			err = -ENOMEM;
		} else {
			err = -EFAULT;
		}
	}

	if (_IOC_DIR(cmd) & _IOC_READ) {
		ret_err = copy_to_user((void __user *)hdr->response.addr,
				       ocb->resp_buffer, resp_size);
	}

	err = (err == 0) ? ret_err : err;
	return err;
}

static int pva_open(struct inode *inode, struct file *file)
{
	int err = 0;

	struct nvhost_device_data *props = container_of(
		inode->i_cdev, struct nvhost_device_data, ctrl_cdev);
	struct pva_kmd_device *kmd_device = props->private_data;
	struct pva_kmd_linux_ocb *ocb = NULL;

	ocb = pva_kmd_zalloc(sizeof(*ocb));
	if (ocb == NULL) {
		pva_kmd_log_err("Failed to allocate memory for PVA context");
		err = -ENOMEM;
		goto out;
	}

	ocb->kmd_ctx = pva_kmd_context_create(kmd_device);
	if (ocb->kmd_ctx == NULL) {
		err = -ENOMEM;
		pva_kmd_log_err("Failed to create PVA context");
		goto free_mem;
	}
	file->private_data = ocb;
	return 0;

free_mem:
	pva_kmd_free(ocb);
out:
	return err;
}

static int pva_release(struct inode *inode, struct file *file)
{
	struct pva_kmd_linux_ocb *ocb = file->private_data;
	pva_kmd_context_destroy(ocb->kmd_ctx);
	pva_kmd_free(ocb);
	return 0;
}

static int pva_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long size = safe_subu64(vma->vm_end, vma->vm_start);
	struct pva_kmd_linux_ocb *ocb = filp->private_data;
	struct pva_kmd_device *pva = ocb->kmd_ctx->pva;
	uint64_t user_ccq_base =
		safe_addu64(pva->reg_phy_base[PVA_KMD_APERTURE_PVA_CLUSTER],
			    pva->regspec.ccq_regs[ocb->kmd_ctx->ccq_id].fifo);
	unsigned long user_ccq_pfn = user_ccq_base >> PAGE_SHIFT;

	if (size != PVA_CFG_CCQ_BLOCK_SIZE) {
		pva_kmd_log_err("Unexpected CCQ map size");
		return -EINVAL;
	}

// TODO: use NV_VM_AREA_STRUCT_HAS_CONST_VM_FLAGS instead of kernel versions
// when the syncpoint change is merged.
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0) /* Linux v6.3 */
	vm_flags_set(vma, VM_IO | VM_DONTEXPAND | VM_DONTDUMP);
#else
	vma->vm_flags |= VM_IO | VM_DONTEXPAND | VM_DONTDUMP;
#endif
	vma->vm_page_prot = pgprot_device(vma->vm_page_prot);
	if (remap_pfn_range(vma, vma->vm_start, user_ccq_pfn, size,
			    vma->vm_page_prot)) {
		pva_kmd_log_err("CCQ map failed");
		return -EAGAIN;
	}

	return 0;
}

const struct file_operations tegra_pva_ctrl_ops = {
	.owner = THIS_MODULE,
#if defined(NV_NO_LLSEEK_PRESENT)
	.llseek = no_llseek,
#endif
	.unlocked_ioctl = pva_ioctl,
	.mmap = pva_mmap,
#ifdef CONFIG_COMPAT
	.compat_ioctl = pva_ioctl,
#endif
	.open = pva_open,
	.release = pva_release,
};
