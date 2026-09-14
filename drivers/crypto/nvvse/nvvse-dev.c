// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <linux/uaccess.h>
#include <linux/nospec.h>
#include <linux/mutex.h>
#include <linux/version.h>
#include <linux/string.h>
#include <linux/platform/tegra/common.h>
#include <soc/tegra/fuse.h>
#include <uapi/misc/nvvse-ioctl.h>
#include <asm/barrier.h>

#include "nvvse-linux-common.h"
#include "nvvse-linux.h"
#include "core_common_hal.h"
#include "core_common_lib_rm.h"
#include "core_sha_functions.h"
#include "core_hmac_functions.h"

#define NVVSE_MAX_ALLOCATED_SHA_RESULT_BUFF_SIZE	256U
#define MISC_DEVICE_NAME_LEN		33U

struct nvvse_devnode {
	struct miscdevice *g_misc_devices;
	struct mutex lock;
	bool node_in_use;
	uint32_t se_comm_id;
} nvvse_devnode[MAX_NUM_IVC];

struct miscdevice *nvvse_info_device;

static int nvvse_dev_get_ivc_db(struct nvvse_get_ivc_db *get_ivc_db)
{
	struct crypto_dev_to_ivc_map *hv_vse_db;
	int ret = 0;
	int i;
	int count = 0;

	hv_vse_db = nvvse_get_crypto_to_ivc_map();
	if (hv_vse_db == NULL)
		return -ENOMEM;

	for (i = 0; i < MAX_NUM_IVC; i++) {
		if (!hv_vse_db[i].node_in_use)
			break;

		get_ivc_db->ivc_entry[i].se_comm_id = hv_vse_db[i].se_comm_id;
		get_ivc_db->ivc_entry[i].se_engine_domain = hv_vse_db[i].se_engine_domain;
		get_ivc_db->ivc_entry[i].se_engine_domain_instanceId =
					hv_vse_db[i].se_engine_domain_instanceId;
		get_ivc_db->ivc_entry[i].se_port = hv_vse_db[i].se_port;
		get_ivc_db->ivc_entry[i].virtualized_instanceId =
					hv_vse_db[i].virtualized_instanceId;
		get_ivc_db->ivc_entry[i].stream_id = hv_vse_db[i].stream_id;
		get_ivc_db->ivc_entry[i].priority = hv_vse_db[i].priority;
		get_ivc_db->ivc_entry[i].gid = hv_vse_db[i].gid;
		get_ivc_db->ivc_entry[i].mapped_buffer_size = hv_vse_db[i].max_buffer_size;
		get_ivc_db->ivc_entry[i].gcm_dec_supported = hv_vse_db[i].gcm_dec_supported;
		get_ivc_db->ivc_entry[i].phandle = UINT32_MAX;
		memcpy(get_ivc_db->ivc_entry[i].soc_params, hv_vse_db[i].soc_params,
					sizeof(get_ivc_db->ivc_entry[i].soc_params));
		get_ivc_db->ivc_entry[i].soc_params_size = hv_vse_db[i].soc_params_size;
		count++;
	}
	get_ivc_db->max_num_ivc = count;
	return ret;
}

static int nvvse_dev_map_membuf(nvvse_ocb_t *ocb,
		struct nvvse_map_membuf_ctl *map_membuf_ctl)
{
	struct nvvse_dev_membuf_ctx membuf_ctx;
	int err = 0;

	membuf_ctx.node_id = ocb->ocb.node_id;
	membuf_ctx.fd = map_membuf_ctl->fd;

	err = nvvse_map_membuf(&membuf_ctx);
	if (err) {
		NVVSE_ERR("%s(): map membuf failed %d\n", __func__, err);
		goto exit;
	}

	map_membuf_ctl->iova = membuf_ctx.iova;

exit:
	return err;
}

static int nvvse_dev_unmap_membuf(nvvse_ocb_t *ocb,
		struct nvvse_unmap_membuf_ctl *unmap_membuf_ctl)
{
	struct nvvse_dev_membuf_ctx membuf_ctx;
	int err = 0;

	membuf_ctx.node_id = ocb->ocb.node_id;
	membuf_ctx.fd = unmap_membuf_ctl->fd;

	err = nvvse_unmap_membuf(&membuf_ctx);
	if (err) {
		NVVSE_ERR("%s(): unmap membuf failed %d\n", __func__, err);
		goto exit;
	}

exit:
	return err;
}

static int nvvse_dev_sha_update(nvvse_ocb_t *ocb,
				struct nvvse_sha_update_ctl *sha_update_ctl)
{
	NvVseInputOutputReqContext devctl_req_context;
	NvVseSHAHeader sha_header;
	int err = 0;

	/* Validate node_id bounds to prevent array overflow */
	if (ocb->ocb.node_id >= MAX_NUM_IVC) {
		NVVSE_ERR("%s(): node_id %u is out of range\n", __func__, ocb->ocb.node_id);
		return -EINVAL;
	}

	devctl_req_context.se_comm_id = nvvse_devnode[ocb->ocb.node_id].se_comm_id;
	devctl_req_context.ocb = ocb;
	devctl_req_context.nvvse_ctx = get_nvvse_context();
	devctl_req_context.input_output_context.msg_header = &sha_header;
	devctl_req_context.input_output_context.msg_header_size = sizeof(sha_header);
	devctl_req_context.input_output_context.src_buffer = sha_update_ctl->in_buff;
	devctl_req_context.input_output_context.src_buffer_size = sha_update_ctl->input_buffer_size;
	devctl_req_context.input_output_context.digest_buffer = sha_update_ctl->digest_buffer;
	devctl_req_context.input_output_context.digest_buffer_size = sha_update_ctl->digest_size;

	memset(&sha_header, 0, sizeof(sha_header));
	sha_header.eSHAType = (NvVseSHAType)sha_update_ctl->sha_type;
	sha_header.uDigestSize = sha_update_ctl->digest_size;
	sha_header.uTotalInputBufferSize = sha_update_ctl->input_buffer_size;
	sha_header.NumofRequests = 1; // Single request for this update
	sha_header.bFirst = sha_update_ctl->is_first ? 1U : 0U;
	sha_header.bLast = sha_update_ctl->is_last ? 1U : 0U;
	sha_header.bInitOnly = sha_update_ctl->init_only ? 1U : 0U;
	sha_header.bReset = sha_update_ctl->do_reset ? 1U : 0U;
	sha_header.bZeroCopy = sha_update_ctl->b_is_zero_copy ? 1U : 0U;
	sha_header.pSrcBuffer = sha_update_ctl->in_buff;
	sha_header.pSrcBufferIova = sha_update_ctl->in_buff_iova;
	sha_header.se_comm_id = nvvse_devnode[ocb->ocb.node_id].se_comm_id;

	err = nvvse_cmd_update_sha(&devctl_req_context);
	if (err != 0) {
		NVVSE_ERR("%s(): nvvse_cmd_update_sha failed: %d\n", __func__, err);
		err = -err;
	}

	return err;
}

static int nvvse_dev_hmac_sha_sign_verify(nvvse_ocb_t *ocb,
				struct nvvse_hmac_sha_sv_ctl *hmac_sha_sv_ctl)
{
	NvVseInputOutputReqContext devctl_req_context;
	NvVseHMACSHASignVerifyHeader hmac_sha_header;
	int err = 0;

	/* Validate node_id bounds to prevent array overflow */
	if (ocb->ocb.node_id >= MAX_NUM_IVC) {
		NVVSE_ERR("%s(): node_id %u is out of range\n", __func__, ocb->ocb.node_id);
		return -EINVAL;
	}

	devctl_req_context.se_comm_id = nvvse_devnode[ocb->ocb.node_id].se_comm_id;
	devctl_req_context.ocb = ocb;
	devctl_req_context.nvvse_ctx = get_nvvse_context();
	devctl_req_context.input_output_context.msg_header = &hmac_sha_header;
	devctl_req_context.input_output_context.msg_header_size = sizeof(hmac_sha_header);
	devctl_req_context.input_output_context.src_buffer = hmac_sha_sv_ctl->src_buffer;
	devctl_req_context.input_output_context.src_buffer_size = hmac_sha_sv_ctl->data_length;
	devctl_req_context.input_output_context.digest_buffer = hmac_sha_sv_ctl->digest_buffer;
	devctl_req_context.input_output_context.digest_buffer_size = 32;
	devctl_req_context.input_output_context.validation_result_buffer = &hmac_sha_sv_ctl->result;
	devctl_req_context.input_output_context.validation_result_buffer_size = sizeof(uint8_t);

	memset(&hmac_sha_header, 0, sizeof(hmac_sha_header));
	memcpy(hmac_sha_header.oKeySlotParams.uKeyId, hmac_sha_sv_ctl->key_slot, NVVSE_KEYSLOT_LEN);
	hmac_sha_header.oKeySlotParams.uTokenId = hmac_sha_sv_ctl->token_id;
	hmac_sha_header.eSHAType = (NvVseSHAType)hmac_sha_sv_ctl->hmac_sha_mode;
	hmac_sha_header.eHMACSHARequestType =
			(NvVseHMACSHARequestType)hmac_sha_sv_ctl->hmac_sha_type;
	hmac_sha_header.uInputBuffersize = hmac_sha_sv_ctl->data_length;
	hmac_sha_header.uDigestSize = hmac_sha_sv_ctl->digest_length;
	hmac_sha_header.bFirst = hmac_sha_sv_ctl->is_first ? 1U : 0U;
	hmac_sha_header.bLast = hmac_sha_sv_ctl->is_last ? 1U : 0U;
	hmac_sha_header.se_comm_id = nvvse_devnode[ocb->ocb.node_id].se_comm_id;

	err = nvvse_cmd_hmac_sha_sign_verify(&devctl_req_context);
	if (err != 0) {
		NVVSE_ERR("%s(): nvvse_cmd_hmac_sha_sign_verify failed: %d\n", __func__, err);
		err = -err;
	}

	return err;
}

static int nvvse_crypto_info_dev_open(struct inode *inode, struct file *filp)
{
	/* No context needed for the info device */
	return 0;
}

static int nvvse_crypto_info_dev_release(struct inode *inode, struct file *filp)
{
	/* No cleanup needed for the info device */
	return 0;
}

static long nvvse_crypto_info_dev_ioctl(struct file *filp,
	unsigned int ioctl_num, unsigned long arg)
{
	struct nvvse_get_ivc_db *get_ivc_db;
	int ret = 0;
	uint64_t err = 0;

	if (ioctl_num == NVVSE_IOCTL_CMDID_GET_IVC_DB) {
		get_ivc_db = kzalloc(sizeof(*get_ivc_db), GFP_KERNEL);
		if (!get_ivc_db) {
			NVVSE_ERR("%s(): failed to allocate memory\n", __func__);
			ret = -ENOMEM;
			goto end;
		}

		ret = nvvse_dev_get_ivc_db(get_ivc_db);
		if (ret) {
			NVVSE_ERR("%s(): Failed to get ivc database get_ivc_db:%d\n", __func__,
					ret);
			kfree(get_ivc_db);
			goto end;
		}

		err = copy_to_user((void __user *)arg, get_ivc_db, sizeof(*get_ivc_db));
		if (err) {
			NVVSE_ERR("%s(): Failed to copy_to_user get_ivc_db\n", __func__);
			kfree(get_ivc_db);
			ret = -EFAULT;
			goto end;
		}

		kfree(get_ivc_db);
	} else {
		NVVSE_ERR("%s(): invalid ioctl code(%d[0x%08x])", __func__, ioctl_num, ioctl_num);
		ret = -EINVAL;
	}

end:
	return ret;
}

static const struct file_operations nvvse_crypto_info_fops = {
	.owner			= THIS_MODULE,
	.open			= nvvse_crypto_info_dev_open,
	.release		= nvvse_crypto_info_dev_release,
	.unlocked_ioctl		= nvvse_crypto_info_dev_ioctl,
};

int nvvse_dev_create_info_node(void)
{
	int ret = 0;

	/* Register the info device node */
	nvvse_info_device = kzalloc(sizeof(struct miscdevice), GFP_KERNEL);
	if (nvvse_info_device == NULL) {
		NVVSE_ERR("%s(): failed to allocate memory for info device\n", __func__);
		return -ENOMEM;
	}

	nvvse_info_device->minor = MISC_DYNAMIC_MINOR;
	nvvse_info_device->fops = &nvvse_crypto_info_fops;
	nvvse_info_device->name = "nvvse-dev-info";

	ret = misc_register(nvvse_info_device);
	if (ret != 0) {
		NVVSE_ERR("%s: info device registration failed err %d\n", __func__, ret);
		kfree(nvvse_info_device);
		return ret;
	}

	return 0;
}

void nvvse_dev_remove_info_node(void)
{
	if (!nvvse_info_device) {
		NVVSE_ERR("%s(): info device is not registered\n", __func__);
		return;
	}

	misc_deregister(nvvse_info_device);
	kfree(nvvse_info_device);
	nvvse_info_device = NULL;
}

static int nvvse_dev_open(struct inode *inode, struct file *filp)
{
	nvvse_ocb_t *ocb = NULL;
	int ret = 0;
	uint32_t node_id;
	struct miscdevice *misc;
	bool is_zero_copy_node;
	struct crypto_dev_to_ivc_map *ivc_db;

	misc = filp->private_data;
	if (!misc) {
		NVVSE_ERR("%s(): misc is NULL\n", __func__);
		return -EPERM;
	}

	if (!misc->this_device) {
		NVVSE_ERR("%s(): misc->this_device is NULL\n", __func__);
		return -EPERM;
	}

	node_id = misc->this_device->id;
	if (node_id >= MAX_NUM_IVC) {
		NVVSE_ERR("%s(): node_id is out of range\n", __func__);
		return -EPERM;
	}

	/* Get the database to properly initialize is_zero_copy_node */
	ivc_db = nvvse_get_crypto_to_ivc_map();
	if (!ivc_db) {
		NVVSE_ERR("%s(): nvvse_get_crypto_to_ivc_map returned NULL\n", __func__);
		return -ENOMEM;
	}

	is_zero_copy_node = ivc_db[node_id].is_zero_copy_node;

	ocb = kzalloc(sizeof(nvvse_ocb_t), GFP_KERNEL);
	if (!ocb) {
		ret = -ENOMEM;
		goto exit;
	}

	ocb->ocb.node_id = node_id;
	ocb->ocb.is_zero_copy_node = is_zero_copy_node;

	ocb->op_in_progress = NvBoolFalse;
	ocb->aes_iv_init = NvBoolFalse;

	filp->private_data = ocb;

	return ret;

exit:
	return ret;
}

static int nvvse_dev_release(struct inode *inode, struct file *filp)
{
	nvvse_ocb_t *ocb = filp->private_data;

	if (!ocb) {
		NVVSE_ERR("%s(): ocb is NULL\n", __func__);
		return 0; /* Return success even if ocb is NULL to avoid blocking file close */
	}

	/* Validate node_id bounds to prevent array overflow */
	if (ocb->ocb.node_id >= MAX_NUM_IVC) {
		NVVSE_ERR("%s(): node_id %u is out of range\n", __func__, ocb->ocb.node_id);
		/* Still free ocb even if node_id is invalid */
		kfree(ocb);
		filp->private_data = NULL;
		return 0;
	}

	if (ocb->ocb.is_zero_copy_node) {
		nvvse_unmap_all_membufs(ocb->ocb.node_id);
		nvvse_devnode[ocb->ocb.node_id].node_in_use = false;
	}

	kfree(ocb);

	filp->private_data = NULL;

	return 0;
}

static long nvvse_dev_ioctl(struct file *filp, unsigned int ioctl_num, unsigned long arg)
{
	nvvse_ocb_t *ocb = filp->private_data;
	struct nvvse_sha_update_ctl *sha_update_ctl;
	struct nvvse_map_membuf_ctl __user *arg_map_membuf_ctl;
	struct nvvse_map_membuf_ctl *map_membuf_ctl;
	struct nvvse_unmap_membuf_ctl __user *arg_unmap_membuf_ctl;
	struct nvvse_unmap_membuf_ctl *unmap_membuf_ctl;
	struct nvvse_hmac_sha_sv_ctl *hmac_sha_sv_ctl;
	struct nvvse_hmac_sha_sv_ctl __user *arg_hmac_sha_sv_ctl;
	int ret = 0;
	uint64_t err = 0;

	/*
	 * Avoid processing ioctl if the file has been closed.
	 * This will prevent crashes caused by NULL pointer dereference.
	 */
	if (!ocb) {
		NVVSE_ERR("%s(): ocb not allocated\n", __func__);
		return -EPERM;
	}

	/* Validate node_id bounds to prevent array overflow */
	if (ocb->ocb.node_id >= MAX_NUM_IVC) {
		NVVSE_ERR("%s(): node_id %u is out of range\n", __func__, ocb->ocb.node_id);
		return -EINVAL;
	}

	mutex_lock(&nvvse_devnode[ocb->ocb.node_id].lock);

	if (ocb->ocb.is_zero_copy_node) {
		switch (ioctl_num) {
		case NVVSE_IOCTL_CMDID_UPDATE_SHA:
		case NVVSE_IOCTL_CMDID_MAP_MEMBUF:
		case NVVSE_IOCTL_CMDID_UNMAP_MEMBUF:
			break;
		default:
			NVVSE_ERR("%s(): unsupported zero copy node command(%08x)\n", __func__,
					ioctl_num);
			ret = -EINVAL;
			break;
		};
	} else {
		switch (ioctl_num) {
		case NVVSE_IOCTL_CMDID_MAP_MEMBUF:
		case NVVSE_IOCTL_CMDID_UNMAP_MEMBUF:
			NVVSE_ERR("%s(): unsupported node command(%08x)\n", __func__,
					ioctl_num);
			ret = -EINVAL;
			break;
		default:
			break;
		};

	}

	if (ret != 0)
		goto release_lock;

	switch (ioctl_num) {
	case NVVSE_IOCTL_CMDID_UPDATE_SHA:
		sha_update_ctl = kzalloc(sizeof(*sha_update_ctl), GFP_KERNEL);
		if (!sha_update_ctl) {
			NVVSE_ERR("%s(): failed to allocate memory\n", __func__);
			ret = -ENOMEM;
			goto release_lock;
		}

		ret = copy_from_user(sha_update_ctl, (void __user *)arg, sizeof(*sha_update_ctl));
		if (ret) {
			NVVSE_ERR("%s(): Failed to copy_from_user sha_update_ctl:%d\n",
			__func__, ret);
			kfree(sha_update_ctl);
			ret = -EFAULT;
			goto release_lock;
		}

		ret = nvvse_dev_sha_update(ocb, sha_update_ctl);

		kfree(sha_update_ctl);
		break;
	case NVVSE_IOCTL_CMDID_HMAC_SHA_SIGN_VERIFY:
		hmac_sha_sv_ctl = kzalloc(sizeof(*hmac_sha_sv_ctl), GFP_KERNEL);
		if (!hmac_sha_sv_ctl) {
			ret = -ENOMEM;
			goto release_lock;
		}

		arg_hmac_sha_sv_ctl = (void __user *)arg;

		ret = copy_from_user(hmac_sha_sv_ctl, arg_hmac_sha_sv_ctl,
				sizeof(*hmac_sha_sv_ctl));
		if (ret) {
			NVVSE_ERR("%s(): Failed to copy_from_user hmac_sha_sv_ctl:%d\n", __func__,
					ret);
			kfree(hmac_sha_sv_ctl);
			goto release_lock;
		}

		ret = nvvse_dev_hmac_sha_sign_verify(ocb, hmac_sha_sv_ctl);
		if (ret) {
			NVVSE_ERR("%s(): Failed in tnvvse_crypto_hmac_sha_sign_verify:%d\n",
			__func__, ret);
			kfree(hmac_sha_sv_ctl);
			goto release_lock;
		}

		if (hmac_sha_sv_ctl->hmac_sha_type == NVVSE_DEV_HMAC_SHA_VERIFY) {
			ret = copy_to_user(&arg_hmac_sha_sv_ctl->result, &hmac_sha_sv_ctl->result,
					sizeof(uint8_t));
			if (ret)
				NVVSE_ERR("%s(): Failed to copy_to_user\n", __func__);
		}

		kfree(hmac_sha_sv_ctl);
		break;
	case NVVSE_IOCTL_CMDID_MAP_MEMBUF:
		map_membuf_ctl = kzalloc(sizeof(*map_membuf_ctl), GFP_KERNEL);
		if (!map_membuf_ctl) {
			ret = -ENOMEM;
			goto release_lock;
		}

		arg_map_membuf_ctl = (void __user *)arg;

		ret = copy_from_user(map_membuf_ctl, arg_map_membuf_ctl,
					sizeof(*map_membuf_ctl));
		if (ret) {
			NVVSE_ERR("%s(): Failed to copy_from_user map_membuf_ctl:%d\n",
						__func__, ret);
			kfree(map_membuf_ctl);
			ret = -EFAULT;
			goto release_lock;
		}

		ret = nvvse_dev_map_membuf(ocb, map_membuf_ctl);
		if (ret) {
			NVVSE_ERR("%s(): Failed to map membuf status:%d\n", __func__, ret);
			kfree(map_membuf_ctl);
			goto release_lock;
		}

		err = copy_to_user(arg_map_membuf_ctl, map_membuf_ctl,
				sizeof(*map_membuf_ctl));
		if (err) {
			NVVSE_ERR("%s(): Failed to copy_to_user map_membuf_ctl\n",
					__func__);
			ret = -EFAULT;
			kfree(map_membuf_ctl);
			goto release_lock;
		}

		kfree(map_membuf_ctl);
		break;

	case NVVSE_IOCTL_CMDID_UNMAP_MEMBUF:
		unmap_membuf_ctl = kzalloc(sizeof(*unmap_membuf_ctl), GFP_KERNEL);
		if (!unmap_membuf_ctl) {
			ret = -ENOMEM;
			goto release_lock;
		}

		arg_unmap_membuf_ctl = (void __user *)arg;

		ret = copy_from_user(unmap_membuf_ctl, arg_unmap_membuf_ctl,
					sizeof(*unmap_membuf_ctl));
		if (ret) {
			NVVSE_ERR("%s(): Failed to copy_from_user unmap_membuf_ctl:%d\n",
						__func__, ret);
			kfree(unmap_membuf_ctl);
			ret = -EFAULT;
			goto release_lock;
		}

		ret = nvvse_dev_unmap_membuf(ocb, unmap_membuf_ctl);
		if (ret) {
			NVVSE_ERR("%s(): Failed to unmap membuf status:%d\n", __func__, ret);
			kfree(unmap_membuf_ctl);
			goto release_lock;
		}

		err = copy_to_user(arg_unmap_membuf_ctl, unmap_membuf_ctl,
				sizeof(*unmap_membuf_ctl));
		if (err) {
			NVVSE_ERR("%s(): Failed to copy_to_user unmap_membuf_ctl\n",
					__func__);
			ret = -EFAULT;
			kfree(unmap_membuf_ctl);
			goto release_lock;
		}

		kfree(unmap_membuf_ctl);
		break;

	default:
		NVVSE_ERR("%s(): invalid ioctl code(%d[0x%08x])",
		__func__, ioctl_num, ioctl_num);
		ret = -EINVAL;
		break;
	}

release_lock:
	mutex_unlock(&nvvse_devnode[ocb->ocb.node_id].lock);

	return ret;
}

static const struct file_operations nvvse_dev_fops = {
	.owner			= THIS_MODULE,
	.open			= nvvse_dev_open,
	.release		= nvvse_dev_release,
	.unlocked_ioctl		= nvvse_dev_ioctl,
};

int nvvse_dev_create_node(uint32_t se_comm_id, uint32_t node_id)
{
	int ret = 0;
	struct miscdevice *misc;
	const char * const node_prefix = "nvvse-dev-";
	char *node_name;
	size_t str_len;
	char const numbers[] = "0123456789";

	if (node_id >= MAX_NUM_IVC) {
		NVVSE_ERR("%s(): node_id is out of range\n", __func__);
		return -EINVAL;
	}

	misc = kzalloc(sizeof(struct miscdevice), GFP_KERNEL);
	if (misc == NULL) {
		NVVSE_ERR("%s(): failed to allocate memory for misc device\n", __func__);
		ret = -ENOMEM;
		goto fail;
	}

	node_name = kzalloc(MISC_DEVICE_NAME_LEN, GFP_KERNEL);
	if (node_name == NULL) {
		NVVSE_ERR("%s(): failed to allocate memory for node name\n", __func__);
		ret = -ENOMEM;
		goto fail;
	}

	if (se_comm_id > 999U) {
		NVVSE_ERR("%s(): se_comm_id out of range: %u\n", __func__, se_comm_id);
		ret = -EINVAL;
		goto fail;
	}

	misc->minor = MISC_DYNAMIC_MINOR;
	misc->fops = &nvvse_dev_fops;
	misc->name = node_name;

	str_len = strlen(node_prefix);
	if (str_len > (MISC_DEVICE_NAME_LEN - 4)) {
		NVVSE_ERR("%s: buffer overflown for misc dev %u\n", __func__, node_id);
		ret = -EINVAL;
		goto fail;
	}
	memcpy(node_name, node_prefix, str_len);
	node_name[str_len++] = numbers[(se_comm_id / 100U)];
	node_name[str_len++] = numbers[(se_comm_id % 100U) / 10U];
	node_name[str_len++] = numbers[(se_comm_id % 10U)];
	node_name[str_len++] = '\0';

	ret = misc_register(misc);
	if (ret != 0) {
		NVVSE_ERR("%s: misc dev %u registration failed err %d\n",
		__func__, node_id, ret);
		goto fail;
	}
	misc->this_device->id = node_id;
	nvvse_devnode[node_id].g_misc_devices = misc;
	nvvse_devnode[node_id].se_comm_id = se_comm_id;
	mutex_init(&nvvse_devnode[node_id].lock);

	return ret;

fail:
	kfree(node_name);
	kfree(misc);
	return ret;
}

void nvvse_dev_remove_node(uint32_t node_id)
{
	if (node_id >= MAX_NUM_IVC) {
		NVVSE_ERR("%s(): node_id is out of range\n", __func__);
		return;
	}

	if (nvvse_devnode[node_id].g_misc_devices != NULL) {
		misc_deregister(nvvse_devnode[node_id].g_misc_devices);
		kfree(nvvse_devnode[node_id].g_misc_devices->name);
		kfree(nvvse_devnode[node_id].g_misc_devices);
		nvvse_devnode[node_id].g_misc_devices = NULL;
		mutex_destroy(&nvvse_devnode[node_id].lock);
	}
}
