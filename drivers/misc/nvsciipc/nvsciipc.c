/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

/*
 * This is NvSciIpc kernel driver. At present its only use is to support
 * secure buffer sharing use case across processes.
 */

#include <nvidia/conftest.h>

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/cred.h>
#include <linux/of.h>
#include <linux/fs.h>

#ifdef CONFIG_TEGRA_VIRTUALIZATION
#include <soc/tegra/virt/syscalls.h>
#include <soc/tegra/virt/hv-ivc.h>
#include <uapi/linux/tegra-ivc-dev.h>
#endif /* CONFIG_TEGRA_VIRTUALIZATION */

#include "nvsciipc.h"

#if defined(CONFIG_ANDROID) || defined(CONFIG_TEGRA_SYSTEM_TYPE_ACK)
#define SYSTEM_GID 1000
#endif /* CONFIG_ANDROID || CONFIG_TEGRA_SYSTEM_TYPE_ACK */

/* enable it to debug auth API via ioctl.
 * enable LINUX_DEBUG_KMD_API in test_nvsciipc_nvmap tool either.
 */
#define DEBUG_AUTH_API 1
#define DEBUG_VALIDATE_TOKEN 0

DEFINE_MUTEX(nvsciipc_mutex);

static struct platform_device *nvsciipc_pdev;
static struct nvsciipc *ctx;
#ifdef CONFIG_TEGRA_VIRTUALIZATION
static int32_t s_guestid = -1;
#endif /* CONFIG_TEGRA_VIRTUALIZATION */

NvSciError NvSciIpcEndpointGetAuthToken(NvSciIpcEndpoint handle,
		NvSciIpcEndpointAuthToken *authToken)
{
	INFO("Not supported in KMD, but in userspace library\n");

	return NvSciError_NotSupported;
}
EXPORT_SYMBOL(NvSciIpcEndpointGetAuthToken);

NvSciError NvSciIpcEndpointGetVuid(NvSciIpcEndpoint handle,
		NvSciIpcEndpointVuid *vuid)
{
	INFO("Not supported in KMD, but in userspace library\n");

	return NvSciError_NotSupported;
}
EXPORT_SYMBOL(NvSciIpcEndpointGetVuid);

NvSciError NvSciIpcEndpointValidateAuthTokenLinuxCurrent(
		NvSciIpcEndpointAuthToken authToken,
		NvSciIpcEndpointVuid *localUserVuid)
{
	struct fd f;
	struct file *filp;
	int i, ret, devlen;
	char node[NVSCIIPC_MAX_EP_NAME+16];

	if ((ctx == NULL) || (ctx->set_db_f != true)) {
		ERR("not initialized\n");
		return NvSciError_NotInitialized;
	}

	f = fdget((int)authToken);
	if (!f.file) {
		ERR("invalid auth token\n");
		return NvSciError_BadParameter;
	}
	filp = f.file;

	devlen = strlen(filp->f_path.dentry->d_name.name);
#if DEBUG_VALIDATE_TOKEN
	INFO("token: %lld, dev name: %s, devlen: %d\n", authToken,
		filp->f_path.dentry->d_name.name, devlen);
#endif

	for (i = 0; i < ctx->num_eps; i++) {
		ret = snprintf(node, sizeof(node), "%s%d",
			ctx->db[i]->dev_name, ctx->db[i]->id);

		if ((ret < 0) || (ret != devlen))
			continue;

#if DEBUG_VALIDATE_TOKEN
		INFO("node:%s, vuid:0x%llx\n", node, ctx->db[i]->vuid);
#endif
		/* compare node name itself only (w/o directory) */
		if (!strncmp(filp->f_path.dentry->d_name.name, node, ret)) {
			*localUserVuid = ctx->db[i]->vuid;
			break;
		}
	}

	if (i == ctx->num_eps) {
		fdput(f);
		ERR("wrong auth token passed\n");
		return NvSciError_BadParameter;
	}

	fdput(f);

	return NvSciError_Success;
}
EXPORT_SYMBOL(NvSciIpcEndpointValidateAuthTokenLinuxCurrent);

NvSciError NvSciIpcEndpointMapVuid(NvSciIpcEndpointVuid localUserVuid,
		NvSciIpcTopoId *peerTopoId, NvSciIpcEndpointVuid *peerUserVuid)
{
	uint32_t backend = NVSCIIPC_BACKEND_UNKNOWN;
	struct nvsciipc_config_entry *entry;
	int i;
	NvSciError ret;

	if ((peerTopoId == NULL) || (peerUserVuid == NULL)) {
		ERR("Invalid parameter\n");
		return NvSciError_BadParameter;
	}

	if ((ctx == NULL) || (ctx->set_db_f != true)) {
		ERR("not initialized\n");
		return NvSciError_NotInitialized;
	}

	for (i = 0; i < ctx->num_eps; i++) {
		if (ctx->db[i]->vuid == localUserVuid) {
			backend = ctx->db[i]->backend;
			entry = ctx->db[i];
			break;
		}
	}

	if (i == ctx->num_eps) {
		ERR("wrong localUserVuid passed\n");
		return NvSciError_BadParameter;
	}

	switch (backend) {
	case NVSCIIPC_BACKEND_ITC:
	case NVSCIIPC_BACKEND_IPC:
		peerTopoId->SocId = NVSCIIPC_SELF_SOCID;
		peerTopoId->VmId = NVSCIIPC_SELF_VMID;
		*peerUserVuid = (localUserVuid ^ 1UL);
		ret = NvSciError_Success;
		break;
#if !defined(__x86_64__)
	case NVSCIIPC_BACKEND_IVC:
		{
			union nvsciipc_vuid_64 vuid64;

			peerTopoId->SocId = NVSCIIPC_SELF_SOCID;
			peerTopoId->VmId = entry->peer_vmid;
			vuid64.value = entry->vuid;
			vuid64.bit.vmid = entry->peer_vmid;
			*peerUserVuid = vuid64.value;

			ret = NvSciError_Success;
		}
		break;
#endif /* __x86_64__ */
	default:
		ret = NvSciError_NotSupported;
		break;
	}

	return ret;
}
EXPORT_SYMBOL(NvSciIpcEndpointMapVuid);

static int nvsciipc_dev_open(struct inode *inode, struct file *filp)
{
	struct nvsciipc *ctx = container_of(inode->i_cdev,
			struct nvsciipc, cdev);

	filp->private_data = ctx;

	return 0;
}

static void nvsciipc_free_db(struct nvsciipc *ctx)
{
	int i;

	if ((ctx->num_eps != 0) && (ctx->set_db_f == true)) {
		for (i = 0; i < ctx->num_eps; i++)
			kfree(ctx->db[i]);

		kfree(ctx->db);
	}

	ctx->num_eps = 0;
}

static int nvsciipc_dev_release(struct inode *inode, struct file *filp)
{
	filp->private_data = NULL;

	return 0;
}

#if DEBUG_AUTH_API
static int nvsciipc_ioctl_validate_auth_token(struct nvsciipc *ctx,
	unsigned int cmd, unsigned long arg)
{
	struct nvsciipc_validate_auth_token op;
	NvSciError err;
	int32_t ret = 0;

	if ((ctx->num_eps == 0) || (ctx->set_db_f != true)) {
		ERR("%s[%d] need to set endpoint database first\n", __func__,
			get_current()->pid);
		ret = -EPERM;
		goto exit;
	}

	if (copy_from_user(&op, (void __user *)arg, _IOC_SIZE(cmd))) {
		ERR("%s : copy_from_user failed\n", __func__);
		ret = -EFAULT;
		goto exit;
	}

	err = NvSciIpcEndpointValidateAuthTokenLinuxCurrent(op.auth_token,
		&op.local_vuid);
	if (err != NvSciError_Success) {
		ERR("%s : 0x%x\n", __func__, err);
		ret = -EINVAL;
		goto exit;
	}

	if (copy_to_user((void __user *)arg, &op, _IOC_SIZE(cmd))) {
		ERR("%s : copy_to_user failed\n", __func__);
		ret = -EFAULT;
		goto exit;
	}

exit:
	return ret;
}

static int nvsciipc_ioctl_map_vuid(struct nvsciipc *ctx, unsigned int cmd,
	unsigned long arg)
{
	struct nvsciipc_map_vuid op;
	NvSciError err;
	int32_t ret = 0;

	if ((ctx->num_eps == 0) || (ctx->set_db_f != true)) {
		ERR("%s[%d] need to set endpoint database first\n", __func__,
			get_current()->pid);
		ret = -EPERM;
		goto exit;
	}

	if (copy_from_user(&op, (void __user *)arg, _IOC_SIZE(cmd))) {
		ERR("%s : copy_from_user failed\n", __func__);
		ret = -EFAULT;
		goto exit;
	}

	err = NvSciIpcEndpointMapVuid(op.vuid, (NvSciIpcTopoId *)&op.peer_topoid,
		&op.peer_vuid);
	if (err != NvSciError_Success) {
		ERR("%s : 0x%x\n", __func__, err);
		ret = -EINVAL;
		goto exit;
	}

	if (copy_to_user((void __user *)arg, &op, _IOC_SIZE(cmd))) {
		ERR("%s : copy_to_user failed\n", __func__);
		ret = -EFAULT;
		goto exit;
	}

exit:
	return ret;
}
#endif /* DEBUG_AUTH_API */

static int nvsciipc_ioctl_get_db_by_name(struct nvsciipc *ctx, unsigned int cmd,
		unsigned long arg)
{
	struct nvsciipc_get_db_by_name get_db;
	int i;

	if ((ctx->num_eps == 0) || (ctx->set_db_f != true)) {
		ERR("%s[%d] need to set endpoint database first\n", __func__,
			get_current()->pid);
		return -EPERM;
	}

	if (copy_from_user(&get_db, (void __user *)arg, _IOC_SIZE(cmd))) {
		ERR("%s : copy_from_user failed\n", __func__);
		return -EFAULT;
	}

	/* read operation */
	for (i = 0; i < ctx->num_eps; i++) {
		if (!strncmp(get_db.ep_name, ctx->db[i]->ep_name,
			NVSCIIPC_MAX_EP_NAME)) {
			get_db.entry = *ctx->db[i];
			get_db.idx = i;
			break;
		}
	}

	if (i == ctx->num_eps) {
		INFO("%s: no entry (%s)\n", __func__, get_db.ep_name);
		return -ENOENT;
	} else if (copy_to_user((void __user *)arg, &get_db,
				_IOC_SIZE(cmd))) {
		ERR("%s : copy_to_user failed\n", __func__);
		return -EFAULT;
	}

	return 0;
}

static int nvsciipc_ioctl_get_db_by_vuid(struct nvsciipc *ctx, unsigned int cmd,
		unsigned long arg)
{
	struct nvsciipc_get_db_by_vuid get_db;
	int i;

	if ((ctx->num_eps == 0) || (ctx->set_db_f != true)) {
		ERR("%s[%d] need to set endpoint database first\n", __func__,
			get_current()->pid);
		return -EPERM;
	}

	if (copy_from_user(&get_db, (void __user *)arg, _IOC_SIZE(cmd))) {
		ERR("%s : copy_from_user failed\n", __func__);
		return -EFAULT;
	}

	/* read operation */
	for (i = 0; i < ctx->num_eps; i++) {
		if (get_db.vuid == ctx->db[i]->vuid) {
			get_db.entry = *ctx->db[i];
			get_db.idx = i;
			break;
		}
	}

	if (i == ctx->num_eps) {
		INFO("%s: no entry (0x%llx)\n", __func__, get_db.vuid);
		return -ENOENT;
	} else if (copy_to_user((void __user *)arg, &get_db,
				_IOC_SIZE(cmd))) {
		ERR("%s : copy_to_user failed\n", __func__);
		return -EFAULT;
	}

	return 0;
}

static int nvsciipc_ioctl_get_vuid(struct nvsciipc *ctx, unsigned int cmd,
		unsigned long arg)
{
	struct nvsciipc_get_vuid get_vuid;
	int i;

	if ((ctx->num_eps == 0) || (ctx->set_db_f != true)) {
		ERR("%s[%d] need to set endpoint database first\n", __func__,
			get_current()->pid);
		return -EPERM;
	}

	if (copy_from_user(&get_vuid, (void __user *)arg, _IOC_SIZE(cmd))) {
		ERR("%s : copy_from_user failed\n", __func__);
		return -EFAULT;
	}

	/* read operation */
	for (i = 0; i < ctx->num_eps; i++) {
		if (!strncmp(get_vuid.ep_name, ctx->db[i]->ep_name,
			NVSCIIPC_MAX_EP_NAME)) {
			get_vuid.vuid = ctx->db[i]->vuid;
			break;
		}
	}

	if (i == ctx->num_eps) {
		INFO("%s: no entry (%s)\n", __func__, get_vuid.ep_name);
		return -ENOENT;
	} else if (copy_to_user((void __user *)arg, &get_vuid,
				_IOC_SIZE(cmd))) {
		ERR("%s : copy_to_user failed\n", __func__);
		return -EFAULT;
	}

	return 0;
}

static int nvsciipc_ioctl_set_db(struct nvsciipc *ctx, unsigned int cmd,
		unsigned long arg)
{
	struct nvsciipc_db user_db;
	struct nvsciipc_config_entry **entry_ptr;
	int ret = 0;
	int i;

	INFO("set_db start\n");

#if defined(CONFIG_ANDROID) || defined(CONFIG_TEGRA_SYSTEM_TYPE_ACK)
	if ((current_cred()->uid.val != SYSTEM_GID) &&
	(current_cred()->uid.val != 0)) {
		ERR("no permission to set db\n");
		return -EPERM;
	}
#else
	/* check root user */
	if (current_cred()->uid.val != 0) {
		ERR("no permission to set db\n");
		return -EPERM;
	}
#endif /* CONFIG_ANDROID || CONFIG_TEGRA_SYSTEM_TYPE_ACK */

	if (copy_from_user(&user_db, (void __user *)arg, _IOC_SIZE(cmd))) {
		ERR("copying user db failed\n");
		return -EFAULT;
	}

	if (user_db.num_eps <= 0) {
		ERR("invalid value passed for num_eps\n");
		return -EINVAL;
	}

	ctx->num_eps = user_db.num_eps;

	entry_ptr = (struct nvsciipc_config_entry **)
		kzalloc(ctx->num_eps * sizeof(struct nvsciipc_config_entry *),
			GFP_KERNEL);

	if (entry_ptr == NULL) {
		ERR("memory allocation for entry_ptr failed\n");
		ret = -EFAULT;
		goto ptr_error;
	}

	ret = copy_from_user(entry_ptr, (void __user *)user_db.entry,
			ctx->num_eps * sizeof(struct nvsciipc_config_entry *));
	if (ret < 0) {
		ERR("copying entry ptr failed\n");
		ret = -EFAULT;
		goto ptr_error;
	}

	ctx->db = (struct nvsciipc_config_entry **)
		kzalloc(ctx->num_eps * sizeof(struct nvsciipc_config_entry *),
			GFP_KERNEL);

	if (ctx->db == NULL) {
		ERR("memory allocation for ctx->db failed\n");
		ret = -EFAULT;
		goto ptr_error;
	}

	for (i = 0; i < ctx->num_eps; i++) {
		ctx->db[i] = (struct nvsciipc_config_entry *)
			kzalloc(sizeof(struct nvsciipc_config_entry),
				GFP_KERNEL);

		if (ctx->db[i] == NULL) {
			ERR("memory allocation for ctx->db[%d] failed\n", i);
			ret = -EFAULT;
			goto ptr_error;
		}

		ret = copy_from_user(ctx->db[i], (void __user *)entry_ptr[i],
				sizeof(struct nvsciipc_config_entry));
		if (ret < 0) {
			ERR("copying config entry failed\n");
			ret = -EFAULT;
			goto ptr_error;
		}
	}

#ifdef CONFIG_TEGRA_VIRTUALIZATION
	if (s_guestid != -1) {
		struct nvsciipc_config_entry *entry;
		union nvsciipc_vuid_64 vuid64;

		for (i = 0; i < ctx->num_eps; i++) {
			entry = ctx->db[i];

			/* update vmid field of vuid */
			vuid64.value = entry->vuid;
			vuid64.bit.vmid = s_guestid;
			entry->vuid = vuid64.value;

			/* fill peer vmid */
			if (entry->backend == NVSCIIPC_BACKEND_IVC) {
				/* Sometimes it fails to find vmid due to bad configuration
				 * in PCT but it is not error. Hence ignore result
				 */
				(void)ivc_cdev_get_peer_vmid(entry->id, &entry->peer_vmid);
				(void)ivc_cdev_get_noti_type(entry->id, &entry->noti_type);
			} else {
				entry->noti_type = IVC_INVALID_IPA;
			}
		}
	}
#endif /* CONFIG_TEGRA_VIRTUALIZATION */

	kfree(entry_ptr);

	ctx->set_db_f = true;

	INFO("set_db done\n");

	return ret;

ptr_error:
	if (ctx->db != NULL) {
		for (i = 0; i < ctx->num_eps; i++) {
			if (ctx->db[i] != NULL) {
				memset(ctx->db[i], 0, sizeof(struct nvsciipc_config_entry));
				kfree(ctx->db[i]);
			}
		}

		kfree(ctx->db);
		ctx->db = NULL;
	}

	if (entry_ptr != NULL)
		kfree(entry_ptr);

	ctx->num_eps = 0;

	return ret;
}

static int nvsciipc_ioctl_get_dbsize(struct nvsciipc *ctx, unsigned int cmd,
		unsigned long arg)
{
	int32_t ret = 0;

	if (ctx->set_db_f != true) {
		ERR("%s[%d] need to set endpoint database first\n", __func__,
			get_current()->pid);
		ret = -EPERM;
		goto exit;
	}

	if (copy_to_user((void __user *)arg, (void *)&ctx->num_eps,
	_IOC_SIZE(cmd))) {
		ERR("%s : copy_to_user failed\n", __func__);
		ret = -EFAULT;
		goto exit;
	}

	DBG("%s : entry count: %d\n", __func__, ctx->num_eps);

exit:
	return ret;
}

static long nvsciipc_dev_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	struct nvsciipc *ctx = filp->private_data;
	long ret = 0;

	if (_IOC_TYPE(cmd) != NVSCIIPC_IOCTL_MAGIC) {
		ERR("%s: not a nvsciipc ioctl\n", __func__);
		ret = -ENOTTY;
		goto exit;
	}

	if (_IOC_NR(cmd) > NVSCIIPC_IOCTL_NUMBER_MAX) {
		ERR("%s: wrong nvsciipc ioctl cmd: 0x%x\n", __func__, cmd);
		ret = -ENOTTY;
		goto exit;
	}

	switch (cmd) {
	case NVSCIIPC_IOCTL_SET_DB:
		mutex_lock(&nvsciipc_mutex);
		ret = nvsciipc_ioctl_set_db(ctx, cmd, arg);
		mutex_unlock(&nvsciipc_mutex);
		break;
	case NVSCIIPC_IOCTL_GET_VUID:
		ret = nvsciipc_ioctl_get_vuid(ctx, cmd, arg);
		break;
	case NVSCIIPC_IOCTL_GET_DB_BY_NAME:
		ret = nvsciipc_ioctl_get_db_by_name(ctx, cmd, arg);
		break;
	case NVSCIIPC_IOCTL_GET_DB_BY_VUID:
		ret = nvsciipc_ioctl_get_db_by_vuid(ctx, cmd, arg);
		break;
	case NVSCIIPC_IOCTL_GET_DB_SIZE:
		ret = nvsciipc_ioctl_get_dbsize(ctx, cmd, arg);
		break;
#if DEBUG_AUTH_API
	case NVSCIIPC_IOCTL_VALIDATE_AUTH_TOKEN:
		ret = nvsciipc_ioctl_validate_auth_token(ctx, cmd, arg);
		break;
	case NVSCIIPC_IOCTL_MAP_VUID:
		ret = nvsciipc_ioctl_map_vuid(ctx, cmd, arg);
		break;
#endif /* DEBUG_AUTH_API */
	case NVSCIIPC_IOCTL_GET_VMID:
#ifdef CONFIG_TEGRA_VIRTUALIZATION
		if (copy_to_user((void __user *) arg, &s_guestid,
			sizeof(s_guestid))) {
			ret = -EFAULT;
		}
#else
		ret = -EFAULT;
#endif /* CONFIG_TEGRA_VIRTUALIZATION */
		break;
	default:
		ERR("unrecognised ioctl cmd: 0x%x\n", cmd);
		ret = -ENOTTY;
		break;
	}

exit:
	return ret;
}

static ssize_t nvsciipc_dbg_read(struct file *filp, char __user *buf,
		size_t count, loff_t *f_pos)
{
	struct nvsciipc *ctx = filp->private_data;
	int i;

	/* check root user */
	if (current_cred()->uid.val != 0) {
		ERR("no permission to read db\n");
		return -EPERM;
	}

	if (ctx->set_db_f != true) {
		ERR("%s[%d] need to set endpoint database first\n", __func__,
			get_current()->pid);
		return -EPERM;
	}

	for (i = 0; i < ctx->num_eps; i++) {
		INFO("EP[%03d]: ep:%s,dev:%s,be:%u,nfrm:%u,fsz:%u,id:%u,noti:%d(TRAP:1,MSI:2)\n", i,
			ctx->db[i]->ep_name,
			ctx->db[i]->dev_name,
			ctx->db[i]->backend,
			ctx->db[i]->nframes,
			ctx->db[i]->frame_size,
			ctx->db[i]->id,
			ctx->db[i]->noti_type);
	}

	return 0;
}

static const struct file_operations nvsciipc_fops = {
	.owner		= THIS_MODULE,
	.open		= nvsciipc_dev_open,
	.release		= nvsciipc_dev_release,
	.unlocked_ioctl	= nvsciipc_dev_ioctl,
	.llseek		= no_llseek,
	.read		= nvsciipc_dbg_read,
};

static int nvsciipc_probe(struct platform_device *pdev)
{
	int ret = 0;

	if (pdev == NULL) {
		ERR("invalid platform device\n");
		ret = -EINVAL;
		goto error;
	}

	ctx = devm_kzalloc(&pdev->dev, sizeof(struct nvsciipc),	GFP_KERNEL);
	if (ctx == NULL) {
		ERR("devm_kzalloc failed for nvsciipc\n");
		ret = -ENOMEM;
		goto error;
	}
	ctx->set_db_f = false;

	ctx->dev = &(pdev->dev);
	platform_set_drvdata(pdev, ctx);

#if defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG) /* Linux v6.4 */
	ctx->nvsciipc_class = class_create(MODULE_NAME);
#else
	ctx->nvsciipc_class = class_create(THIS_MODULE, MODULE_NAME);
#endif
	if (IS_ERR(ctx->nvsciipc_class)) {
		ERR("failed to create class: %ld\n",
			PTR_ERR(ctx->nvsciipc_class));
		ret = PTR_ERR(ctx->nvsciipc_class);
		goto error;
	}

	ret = alloc_chrdev_region(&(ctx->dev_t), 0, 1, MODULE_NAME);
	if (ret != 0) {
		ERR("alloc_chrdev_region() failed\n");
		goto error;
	}

	ctx->dev_t = MKDEV(MAJOR(ctx->dev_t), 0);
	cdev_init(&ctx->cdev, &nvsciipc_fops);
	ctx->cdev.owner = THIS_MODULE;

	ret = cdev_add(&(ctx->cdev), ctx->dev_t, 1);
	if (ret != 0) {
		ERR("cdev_add() failed\n");
		goto error;
	}

	if (snprintf(ctx->device_name, (MAX_NAME_SIZE - 1), "%s", MODULE_NAME) < 0) {
		pr_err("snprintf() failed\n");
		ret = -ENOMEM;
		goto error;
	}

	ctx->device = device_create(ctx->nvsciipc_class, NULL,
			ctx->dev_t, ctx,
			ctx->device_name, 0);
	if (IS_ERR(ctx->device)) {
		ret = PTR_ERR(ctx->device);
		ERR("device_create() failed\n");
		goto error;
	}
	dev_set_drvdata(ctx->device, ctx);

#ifdef CONFIG_TEGRA_VIRTUALIZATION
	{
		if (is_tegra_hypervisor_mode()) {
			ret = hyp_read_gid(&s_guestid);
			if (ret != 0) {
				ERR("Failed to read guest id\n");
				goto error;
			}
			INFO("guestid: %d\n", s_guestid);
		}
	}
#endif /* CONFIG_TEGRA_VIRTUALIZATION */

	INFO("loaded module\n");

	return ret;

error:
	nvsciipc_cleanup(ctx);

	return ret;
}

static void nvsciipc_cleanup(struct nvsciipc *ctx)
{
	if (ctx == NULL)
		return;

	nvsciipc_free_db(ctx);

	if (ctx->nvsciipc_class && ctx->dev_t)
		device_destroy(ctx->nvsciipc_class, ctx->dev_t);

	if (ctx->device != NULL) {
		cdev_del(&ctx->cdev);
		ctx->device = NULL;
	}

	if (ctx->dev_t) {
		unregister_chrdev_region(ctx->dev_t, 1);
		ctx->dev_t = 0;
	}

	if (ctx->nvsciipc_class) {
		class_destroy(ctx->nvsciipc_class);
		ctx->nvsciipc_class = NULL;
	}

	devm_kfree(ctx->dev, ctx);
	ctx = NULL;
}

static int nvsciipc_remove(struct platform_device *pdev)
{
	struct nvsciipc *ctx = NULL;

	if (pdev == NULL) {
		ERR("%s: pdev is NULL\n", __func__);
		goto exit;
	}

	ctx = (struct nvsciipc *)platform_get_drvdata(pdev);
	if (ctx == NULL) {
		ERR("%s: ctx is NULL\n", __func__);
		goto exit;
	}

	nvsciipc_cleanup(ctx);

exit:
	INFO("Unloaded module\n");

	return 0;
}

static struct platform_driver nvsciipc_driver = {
	.probe  = nvsciipc_probe,
	.remove = nvsciipc_remove,
	.driver = {
		.name = MODULE_NAME,
	},
};

static int __init nvsciipc_module_init(void)
{
	int ret;

	ret = platform_driver_register(&nvsciipc_driver);
	if (ret) {
		ERR("%s: platform_driver_register: %d\n", __func__, ret);
		return ret;
	}

	nvsciipc_pdev = platform_device_register_simple(MODULE_NAME, -1,
							NULL, 0);
	if (IS_ERR(nvsciipc_pdev)) {
		ERR("%s: platform_device_register_simple\n", __func__);
		platform_driver_unregister(&nvsciipc_driver);
		return PTR_ERR(nvsciipc_pdev);
	}

	return 0;
}

static void __exit nvsciipc_module_deinit(void)
{
	// calls nvsciipc_remove internally
	platform_device_unregister(nvsciipc_pdev);

	platform_driver_unregister(&nvsciipc_driver);
}

module_init(nvsciipc_module_init);
module_exit(nvsciipc_module_deinit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Nvidia Corporation");
