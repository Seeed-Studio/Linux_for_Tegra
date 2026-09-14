// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2019-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

/*
 * This is NvSciIpc kernel driver. At present its only use is to support
 * secure buffer sharing use case across processes.
 */

#include <nvidia/conftest.h>

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/string_helpers.h>
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
#include <linux/uidgid.h>
#if defined(CONFIG_FUNCTION_ERROR_INJECTION) && defined(CONFIG_BPF_KPROBE_OVERRIDE)
#include <linux/error-injection.h>
#endif /* CONFIG_FUNCTION_ERROR_INJECTION && CONFIG_BPF_KPROBE_OVERRIDE */

#include <soc/tegra/virt/syscalls.h>
#include <soc/tegra/virt/hv-ivc.h>
#include <uapi/linux/tegra-ivc-dev.h>

#include "nvsciipc.h"

#if defined(CONFIG_ANDROID) || defined(CONFIG_TEGRA_SYSTEM_TYPE_ACK)
#define SYSTEM_GID 1000U
#endif /* CONFIG_ANDROID || CONFIG_TEGRA_SYSTEM_TYPE_ACK */


/* enable it to debug auth API via ioctl.
 * enable LINUX_DEBUG_KMD_API in test_nvsciipc_nvmap tool either.
 */
#define DEBUG_VALIDATE_TOKEN 0

static DEFINE_MUTEX(nvsciipc_mutex);
static DEFINE_MUTEX(ep_mutex);

static struct platform_device *nvsciipc_pdev;
static struct nvsciipc *s_ctx;
static int32_t s_guestid = -1;
/* UID of SET_DB ioctl client (default root UID) */
static uint32_t s_nvsciipc_uid;

long nvsciipc_dev_ioctl(struct file *filp, unsigned int cmd,
	unsigned long arg);

static int64_t init_dt_gen_vuid(uint32_t socid, uint32_t vmid,
		uint32_t type, uint32_t index)
{
	union nvsciipc_vuid_64 vuid64;

	vuid64.bit.socid = socid;
	vuid64.bit.vmid = vmid;
	vuid64.bit.type = type;
	vuid64.bit.index = index;
	vuid64.bit.reserved = 0;

	return vuid64.value;
}

static int nvsciipc_parse_dt_endpoint(struct device *dev,
                                    struct nvsciipc_config_entry *entry1,
                                    struct nvsciipc_config_entry *entry2,
                                    const char **config,
                                    int config_size)
{
    int ret = 0;
    int required_size;
    static uint32_t VUIDidx = 0;
    static uint32_t VUIDidx_c2c = 0;
    static bool used_qid[1024] = {false};

    /* First check backend type which determines required config size */
    if (!strcmp(config[0], "INTER_THREAD")) {
        entry1->backend = NVSCIIPC_BACKEND_ITC;
        entry2->backend = NVSCIIPC_BACKEND_ITC;
        required_size = 5; // Minimum required size without UID
    } else if (!strcmp(config[0], "INTER_PROCESS")) {
        entry1->backend = NVSCIIPC_BACKEND_IPC;
        entry2->backend = NVSCIIPC_BACKEND_IPC;
        required_size = 5; // Minimum required size without UIDs
    } else if (!strcmp(config[0], "INTER_VM")) {
        entry1->backend = NVSCIIPC_BACKEND_IVC;
        required_size = 3; // Minimum required size without UID
    } else if (!strcmp(config[0], "INTER_CHIP")) {
        entry1->backend = NVSCIIPC_BACKEND_C2C;
        required_size = 3; // Minimum required size without UID
    } else if (!strcmp(config[0], "INTER_CHIP_PCIE")) {
        entry1->backend = NVSCIIPC_BACKEND_C2C;
        required_size = 3; // Minimum required size without UID
    } else if (!strcmp(config[0], "INTER_C2C_NPM")) {
        entry1->backend = NVSCIIPC_BACKEND_C2C_NPM;
        required_size = 8; // Minimum required size without UID
    } else {
        ERR("nvsciipc: Unknown backend type: %s\n", config[0]);
        return -EINVAL;
    }

    /* Validate minimum config size */
    if (config_size < required_size) {
        ERR("nvsciipc: Invalid config size %d, minimum required %d for %s\n",
                config_size, required_size, config[0]);
        return -EINVAL;
    }

    /* Parse configuration based on backend type */
    switch (entry1->backend) {
    case NVSCIIPC_BACKEND_IVC:
        /* Parse endpoint name */
        ret = strscpy(entry1->ep_name, config[1], NVSCIIPC_MAX_EP_NAME);
        if (ret < 0) {
            ERR("nvsciipc: Failed to copy endpoint name\n");
            return ret;
        }
        strscpy(entry1->dev_name, "ivc", NVSCIIPC_MAX_EP_NAME);
		ret = kstrtou32(config[2], 10, &entry1->id);
        if (ret) {
            ERR("nvsciipc: Invalid IVC id: %s\n", config[2]);
            return ret;
        }

		/*
		* Range and duplication check:
		* - Maximum 1000 QUEUEs with ID between 0 to 999
		* - No duplicated QUEUEs
		*/
		if ((entry1->id >= 1024) ||
			used_qid[entry1->id]) {
			ERR("nvsciipc: Invalid IVC id: %s\n", config[2]);
			return -EINVAL;
		}
		used_qid[entry1->id] = true;

        /* Parse UID if available, otherwise use default */
        entry1->uid = 0xFFFFFFFF;  // Default value
        if (config_size >= 4 && config[3][0] != '\0') { // Check for non-empty string
            ret = kstrtou32(config[3], 10, &entry1->uid);
            if (ret) {
                ERR("nvsciipc: Invalid UID: %s, using default\n", config[3]);
                entry1->uid = 0xFFFFFFFF;
            }
        }
        break;

    case NVSCIIPC_BACKEND_C2C:
        /* Parse endpoint name */
        ret = strscpy(entry1->ep_name, config[1], NVSCIIPC_MAX_EP_NAME);
        if (ret < 0) {
            ERR("nvsciipc: Failed to copy endpoint name\n");
            return ret;
        }
        strscpy(entry1->dev_name, config[1], NVSCIIPC_MAX_EP_NAME);
		ret = kstrtou32(config[2], 10, &entry1->id);
        if (ret) {
            ERR("nvsciipc: Invalid IVC id: %s\n", config[2]);
            return ret;
        }

        /* Parse UID if available, otherwise use default */
        entry1->uid = 0xFFFFFFFF;  // Default value
		entry1->vuid = init_dt_gen_vuid(0, 0, entry1->backend, VUIDidx_c2c++);
       if (config_size >= 4 && config[3][0] != '\0') { // Check for non-empty string 
            ret = kstrtou32(config[3], 10, &entry1->uid);
            if (ret) {
                ERR("nvsciipc: Invalid UID: %s, using default\n", config[3]);
                entry1->uid = 0xFFFFFFFF;
            }
        }
        break;

    case NVSCIIPC_BACKEND_C2C_NPM:
         /* Parse endpoint name */
        ret = strscpy(entry1->ep_name, config[1], NVSCIIPC_MAX_EP_NAME);
        if (ret < 0) {
            ERR("nvsciipc: Failed to copy endpoint name\n");
            return ret;
        }
		ret = kstrtou32(config[2], 10, &entry1->nframes);
		if (ret) {
			ERR("Invalid nframes value: %s\n", config[2]);
			return ret;
		}
		ret = kstrtou32(config[3], 10, &entry1->frame_size);
		if (ret) {
			ERR("Invalid frame_size value: %s\n", config[3]);
			return ret;
		}
		strscpy(entry1->rdma_dev_name, config[4], NVSCIIPC_MAX_RDMA_NAME);
		strscpy(entry1->remote_ip, config[5], NVSCIIPC_MAX_IP_NAME);
		entry1->remote_port = kstrtou32(config[6], 10, &entry1->remote_port);
		entry1->local_port = kstrtou32(config[7], 10, &entry1->local_port);

        /* Parse UID if available, otherwise use default */
        entry1->uid = 0xFFFFFFFF;  // Default value
		entry1->vuid = init_dt_gen_vuid(0, 0, entry1->backend, VUIDidx_c2c++);
        if (config_size >= 9 && config[8][0] != '\0') { // Check for non-empty string
            ret = kstrtou32(config[8], 10, &entry1->uid);
            if (ret) {
                ERR("nvsciipc: Invalid UID: %s, using default\n", config[8]);
                entry1->uid = 0xFFFFFFFF;
            }
        }
        break;   
	case NVSCIIPC_BACKEND_ITC:
        /* Parse first endpoint */
        ret = strscpy(entry1->ep_name, config[1], NVSCIIPC_MAX_EP_NAME);
        if (ret < 0) {
            ERR("nvsciipc: Failed to copy endpoint name\n");
            return ret;
        }
		strscpy(entry1->dev_name, config[1], NVSCIIPC_MAX_EP_NAME);

        /* Parse second endpoint */
        ret = strscpy(entry2->ep_name, config[2], NVSCIIPC_MAX_EP_NAME);
        if (ret < 0) {
            ERR("nvsciipc: Failed to copy endpoint name\n");
            return ret;
        }
		strscpy(entry2->dev_name, config[1], NVSCIIPC_MAX_EP_NAME);
        /* Parse frame count - same for both endpoints */
        ret = kstrtou32(config[3], 10, &entry1->nframes);
        if (ret) {
            ERR("nvsciipc: Invalid nframes: %s\n", config[3]);
            return ret;
        }
        entry2->nframes = entry1->nframes;

        /* Parse frame size - same for both endpoints */
        ret = kstrtou32(config[4], 10, &entry1->frame_size);
        if (ret) {
            ERR("nvsciipc: Invalid frame_size: %s\n", config[4]);
            return ret;
        }
        entry2->frame_size = entry1->frame_size;
		entry1->id = 0;
		entry2->id = 1;

		entry1->vuid = init_dt_gen_vuid(0, 0, entry1->backend, VUIDidx++);
		entry2->vuid = init_dt_gen_vuid(0, 0, entry2->backend, VUIDidx++);

        /* Parse single UID for both endpoints if available, otherwise use default */
        entry1->uid = 0xFFFFFFFF;  // Default value
        entry2->uid = 0xFFFFFFFF;  // Default value
        if (config_size >= 6 && config[5][0] != '\0') { // Check for non-empty string
            ret = kstrtou32(config[5], 10, &entry1->uid);
            if (ret) {
                ERR("nvsciipc: Invalid UID: %s, using default\n", config[5]);
                entry1->uid = 0xFFFFFFFF;
            }
            entry2->uid = entry1->uid;  // Same UID for both endpoints
        }

        break;
    case NVSCIIPC_BACKEND_IPC: 
        /* Parse first endpoint */
        ret = strscpy(entry1->ep_name, config[1], NVSCIIPC_MAX_EP_NAME);
        if (ret < 0) {
            ERR("nvsciipc: Failed to copy endpoint name\n");
            return ret;
        }
		strscpy(entry1->dev_name, config[1], NVSCIIPC_MAX_EP_NAME);
        /* Parse second endpoint */
        ret = strscpy(entry2->ep_name, config[2], NVSCIIPC_MAX_EP_NAME);
        if (ret < 0) {
            ERR("nvsciipc: Failed to copy endpoint name\n");
            return ret;
        }
		strscpy(entry2->dev_name, config[1], NVSCIIPC_MAX_EP_NAME);
        /* Parse frame count - same for both endpoints */
        ret = kstrtou32(config[3], 10, &entry1->nframes);
        if (ret) {
            ERR("nvsciipc: Invalid nframes: %s\n", config[3]);
            return ret;
        }
        entry2->nframes = entry1->nframes;
		entry1->id = 0;
		entry2->id = 1;
        /* Parse frame size - same for both endpoints */
        ret = kstrtou32(config[4], 10, &entry1->frame_size);
        if (ret) {
            ERR("nvsciipc: Invalid frame_size: %s\n", config[4]);
            return ret;
        }
        entry2->frame_size = entry1->frame_size;

        /* Parse separate UIDs for each endpoint if available, otherwise use default */
        entry1->uid = 0xFFFFFFFF;  // Default value
        entry2->uid = 0xFFFFFFFF;  // Default value

		entry1->vuid = init_dt_gen_vuid(0, 0, entry1->backend, VUIDidx++);
		entry2->vuid = init_dt_gen_vuid(0, 0, entry2->backend, VUIDidx++); 

		if (config_size >= 6 && config[5][0] != '\0') { // Check for non-empty string
            ret = kstrtou32(config[5], 10, &entry1->uid);
            if (ret) {
                ERR("nvsciipc: Invalid UID1: %s, using default\n", config[5]);
                entry1->uid = 0xFFFFFFFF;
            }
        }

        if (config_size >= 7 && config[6][0] != '\0') { // Check for non-empty string
            ret = kstrtou32(config[6], 10, &entry2->uid);
            if (ret) {
                ERR("nvsciipc: Invalid UID2: %s, using default\n", config[6]);
                entry2->uid = 0xFFFFFFFF;
            }
        }

        break;
    }
    return 0;
}

static int nvsciipc_parse_dt(struct nvsciipc *ctx)
{
    struct device_node *chosen_node;
    struct device_node *nvsciipc_node;
    struct device_node *nvsciipc_user_node;
    const char **channel_db = NULL;
    int num_entries = 0, num_user_entries = 0;
    int total_entries = 0;
    int ret = 0;
    int str_idx = 0, ep_count = 0;
    int strings_per_ep;
    int endpoints_per_entry;
    int offset = 0;
    int ep_idx = 0;
    const char *backend;

    /* Find the nvsciipc node under chosen */
    chosen_node = of_find_node_by_path("/chosen");
    if (!chosen_node) {
        ERR("Failed to find /chosen node\n");
        goto out;
    }

    nvsciipc_node = of_get_child_by_name(chosen_node, "nvsciipc");
    if (!nvsciipc_node) {
        INFO("Failed to find nvsciipc node\n");
        goto out;
    }

    nvsciipc_user_node = of_get_child_by_name(chosen_node, "nvsciipc_user");
    if (!nvsciipc_user_node) {
        INFO("Failed to find nvsciipc_user node\n");
        goto out;
    }
  
    /* Get number of entries in both channel-db properties */
    num_entries = of_property_count_strings(nvsciipc_node, "nvsciipc,channel-db");
    if (num_entries < 0) {
        ERR("No channel-db entries found: %d\n", num_entries);
        num_entries = 0;
    }

    num_user_entries = of_property_count_strings(nvsciipc_user_node, "nvsciipc,channel-db-user");
	if (num_user_entries < 0) {
		if (num_user_entries == -ENODATA)
			INFO("No channel-db-user entries found (empty property)\n");
		else
			ERR("Error reading channel-db-user entries: %d\n", num_user_entries);
		num_user_entries = 0;
	}

    /* Check if at least one property exists */
    if (num_entries == 0 && num_user_entries == 0) {
        ERR("No valid channel database properties found\n");
        ret = -ENODEV;
        goto out;
    }

    total_entries = num_entries + num_user_entries;

    /* Allocate memory for combined channel database */
    channel_db = devm_kcalloc(ctx->dev, total_entries, sizeof(char *), GFP_KERNEL);
    if (!channel_db) {
        ERR("Failed to allocate channel database\n");
        ret = -ENOMEM;
        goto out;
    }

    /* Read strings from channel-db if it exists */
    if (num_entries > 0) {
        ret = of_property_read_string_array(nvsciipc_node, "nvsciipc,channel-db",
                                          channel_db, num_entries);
        if (ret < 0) {
            ERR("Failed to read channel-db strings: %d\n", ret);
            goto out;
        }
    }

    /* Read strings from channel-db-user if it exists */
    if (num_user_entries > 0) {
        ret = of_property_read_string_array(nvsciipc_user_node, "nvsciipc,channel-db-user",
                                          &channel_db[num_entries], num_user_entries);
        if (ret < 0) { 
            ERR("Failed to read channel-db-user strings: %d\n", ret);
            goto out;
        }
    }

    /* Calculate number of endpoints by parsing all configuration strings */
    str_idx = 0;
    while (str_idx < total_entries) {
        /* Read backend type from the combined channel_db array */
        backend = channel_db[str_idx];
        
        if (!backend) {
            ERR("Failed to read backend type at index %d\n", str_idx);
            ret = -EINVAL;
            goto out;
        }

        /* Determine number of strings and endpoints based on backend type */
        if (!strcmp(backend, "INTER_VM") || !strcmp(backend, "INTER_CHIP") || 
            !strcmp(backend, "INTER_CHIP_PCIE")) {
            strings_per_ep = 4;  // backend, name, info, uid
            endpoints_per_entry = 1;
        } else if (!strcmp(backend, "INTER_THREAD")) {
            strings_per_ep = 6;  // backend, ep1, ep2, frames, size, shared_uid
            endpoints_per_entry = 2;
        } else if (!strcmp(backend, "INTER_PROCESS")) {
            strings_per_ep = 7;  // backend, ep1, ep2, frames, size, uid1, uid2
            endpoints_per_entry = 2;
        } else if (!strcmp(backend, "INTER_C2C_NPM")) {
            strings_per_ep = 9;  // backend, epname, Info0-3, rdma_dev_name, remote_ip, uid
            endpoints_per_entry = 1;
        } else {
            ERR("Unknown backend type: %s at index %d\n", backend, str_idx);
            ret = -EINVAL;
            goto out;
        }

        if ((str_idx + strings_per_ep) > total_entries) {
            ERR("Incomplete endpoint configuration at index %d\n", str_idx);
            ret = -EINVAL;
            goto out;
        }

        str_idx += strings_per_ep;
        ep_count += endpoints_per_entry;
    }

    ctx->num_eps = ep_count;
    if (ctx->num_eps > NVSCIIPC_MAX_EP_COUNT) {
        ERR("Too many endpoints: %d\n", ctx->num_eps);
        ret = -EINVAL;
        goto out;
    }

    /* Allocate memory for endpoints */
    ctx->db = devm_kcalloc(ctx->dev, ctx->num_eps,
                          sizeof(struct nvsciipc_config_entry *),
                          GFP_KERNEL);
    if (!ctx->db) {
        ERR("Failed to allocate endpoints\n");
        ret = -ENOMEM;
        goto out;
    }

    ctx->stat = devm_kcalloc(ctx->dev, ctx->num_eps,
                            sizeof(struct nvsciipc_res_stat *),
                            GFP_KERNEL);
    if (!ctx->stat) {
        ERR("Failed to allocate resources\n");
        ret = -ENOMEM;
        goto out;
    }

    /* Parse each endpoint configuration */
    while (offset < total_entries) {
        /* Allocate memory for endpoint(s) */
        ctx->db[ep_idx] = devm_kzalloc(ctx->dev, sizeof(struct nvsciipc_config_entry),
                                      GFP_KERNEL);
        if (!ctx->db[ep_idx]) {
            ret = -ENOMEM;
            goto out;
        }

        ctx->stat[ep_idx] = devm_kzalloc(ctx->dev, sizeof(struct nvsciipc_res_stat),
                                        GFP_KERNEL);
        if (!ctx->stat[ep_idx]) {
            ERR("Failed to allocate resources\n");
            ret = -ENOMEM;
            goto out;
        }

        /* For ITC/IPC, allocate second endpoint */
        if (!strcmp(channel_db[offset], "INTER_THREAD") || 
            !strcmp(channel_db[offset], "INTER_PROCESS")) {
            ctx->db[ep_idx + 1] = devm_kzalloc(ctx->dev, 
                                              sizeof(struct nvsciipc_config_entry),
                                              GFP_KERNEL);
            if (!ctx->db[ep_idx + 1]) {
                ERR("Failed to allocate second endpoint config\n");
                ret = -ENOMEM;
                goto out;
            }

            ctx->stat[ep_idx + 1] = devm_kzalloc(ctx->dev, 
                                                sizeof(struct nvsciipc_res_stat),
                                                GFP_KERNEL);
            if (!ctx->stat[ep_idx + 1]) {
                ERR("Failed to allocate second endpoint stat\n");
                ret = -ENOMEM;
                goto out;
            }
        }

        /* Parse the endpoint(s) */
        if (!strcmp(channel_db[offset], "INTER_VM") || 
            !strcmp(channel_db[offset], "INTER_CHIP") || 
            !strcmp(channel_db[offset], "INTER_CHIP_PCIE")) {
            strings_per_ep = 4;
            ret = nvsciipc_parse_dt_endpoint(ctx->dev, ctx->db[ep_idx], NULL,
                                           &channel_db[offset], strings_per_ep);
            if (ret) {
                ERR("Failed to parse endpoint %d\n", ep_idx);
                goto out;
            }
            ep_idx++;
        } else if (!strcmp(channel_db[offset], "INTER_C2C_NPM")) {
            strings_per_ep = 9;
            ret = nvsciipc_parse_dt_endpoint(ctx->dev, ctx->db[ep_idx], NULL,
                                           &channel_db[offset], strings_per_ep);
            if (ret) {
                ERR("Failed to parse endpoint %d\n", ep_idx);
                goto out;
            }
            ep_idx++;
        } else {
            strings_per_ep = (!strcmp(channel_db[offset], "INTER_THREAD")) ? 6 : 7;
            ret = nvsciipc_parse_dt_endpoint(ctx->dev, ctx->db[ep_idx], 
                                           ctx->db[ep_idx + 1],
                                           &channel_db[offset], strings_per_ep);
            if (ret) {
                ERR("Failed to parse endpoints %d,%d\n", 
                        ep_idx, ep_idx + 1);
                goto out;
            }
            ep_idx += 2;
        }
        
        offset += strings_per_ep;
    }

    ctx->set_db_f = true;
    ret = 0;
    INFO("Successfully parsed %d endpoints\n", ctx->num_eps);

out:
    of_node_put(nvsciipc_user_node);
    of_node_put(nvsciipc_node);
    of_node_put(chosen_node);
    return ret;
}



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
	char node[NVSCIIPC_MAX_EP_NAME + 16];

	if (localUserVuid == NULL) {
		ERR("Invalid parameter\n");
		return NvSciError_BadParameter;
	}

	if ((s_ctx == NULL) || (s_ctx->set_db_f != true)) {
		ERR("not initialized\n");
		return NvSciError_NotInitialized;
	}

	f = fdget((int)authToken);
#if defined(NV_FD_EMPTY_PRESENT) /* Linux v6.12 */
	if (fd_empty(f)) {
#else
	if (!f.file) {
#endif
		ERR("invalid auth token\n");
		return NvSciError_BadParameter;
	}

#if defined(NV_FD_FILE_PRESENT) /* Linux v6.12 */
	filp = fd_file(f);
#else
	filp = f.file;
#endif

	devlen = strlen(filp->f_path.dentry->d_name.name);
#if DEBUG_VALIDATE_TOKEN
	INFO("token: %lld, dev name: %s, devlen: %d\n", authToken,
		filp->f_path.dentry->d_name.name, devlen);
#endif

	for (i = 0; i < s_ctx->num_eps; i++) {
		ret = snprintf(node, sizeof(node), "%s%d",
			s_ctx->db[i]->dev_name, s_ctx->db[i]->id);

		if ((ret < 0) || (ret != devlen))
			continue;

#if DEBUG_VALIDATE_TOKEN
		INFO("node:%s, vuid:0x%llx\n", node, s_ctx->db[i]->vuid);
#endif
		/* compare node name itself only (w/o directory) */
		if (!strncmp(filp->f_path.dentry->d_name.name, node, ret)) {
			*localUserVuid = s_ctx->db[i]->vuid;
			break;
		}
	}

	if (i == s_ctx->num_eps) {
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

	if ((s_ctx == NULL) || (s_ctx->set_db_f != true)) {
		ERR("not initialized\n");
		return NvSciError_NotInitialized;
	}

	for (i = 0; i < s_ctx->num_eps; i++) {
		if (s_ctx->db[i]->vuid == localUserVuid) {
			backend = s_ctx->db[i]->backend;
			entry = s_ctx->db[i];
			break;
		}
	}

	if (i == s_ctx->num_eps) {
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
		for (i = 0; i < ctx->num_eps; i++) {
			kfree(ctx->db[i]);
			kfree(ctx->stat[i]);
		}

		kfree(ctx->db);
		kfree(ctx->stat);
	}

	ctx->num_eps = 0;
}

static int nvsciipc_dev_release(struct inode *inode, struct file *filp)
{
	struct nvsciipc *ctx = filp->private_data;
	pid_t current_pid = current->tgid;
	uint32_t i;

	if ((ctx->num_eps == 0) || (ctx->set_db_f != true))
		goto exit;

	mutex_lock(&ep_mutex);
	/* release all endpoint mutexes of client process forcedly */
	for (i = 0; i < ctx->num_eps; i++) {
		if ((current_pid == ctx->stat[i]->owner_pid) &&
			(ctx->stat[i]->reserved == NVSCIIPC_EP_RESERVE)) {
			INFO("%s[%s:%d] release mutex of %s\n", __func__,
				current->comm, get_current()->tgid, ctx->db[i]->ep_name);
			ctx->stat[i]->reserved = NVSCIIPC_EP_RELEASE;
			ctx->stat[i]->owner_pid = 0;
		}
	}
	mutex_unlock(&ep_mutex);

exit:
	DBG("%s[%s:%d]\n", __func__, current->comm, current_pid);
	filp->private_data = NULL;

	return 0;
}

static int nvsciipc_ioctl_validate_auth_token(struct nvsciipc *ctx,
	unsigned int cmd, unsigned long arg)
{
	struct nvsciipc_validate_auth_token op;
	NvSciError err;
	int32_t ret = 0;

	if ((ctx->num_eps == 0) || (ctx->set_db_f != true)) {
		ERR("%s[%s:%d] need to set endpoint database first\n", __func__,
			current->comm, get_current()->tgid);
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
		ERR("%s[%s:%d] need to set endpoint database first\n", __func__,
			current->comm, get_current()->tgid);
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

static int nvsciipc_ioctl_get_db_by_idx(struct nvsciipc *ctx, unsigned int cmd,
	unsigned long arg)
{
	struct nvsciipc_get_db_by_idx get_db;
	struct cred const *cred = get_current_cred();
	uid_t const uid = cred->uid.val;

	put_cred(cred);
	if ((ctx->num_eps == 0) || (ctx->set_db_f != true)) {
		ERR("%s[%s:%d] need to set endpoint database first\n", __func__,
			current->comm, get_current()->tgid);
		return -EPERM;
	}

#if defined(CONFIG_ANDROID) || defined(CONFIG_TEGRA_SYSTEM_TYPE_ACK)
	if ((uid != SYSTEM_GID) && (uid != 0) && (uid != s_nvsciipc_uid)) {
		ERR("no permission to set db\n");
		return -EPERM;
	}
#else
	/* check root or nvsciipc user */
	if ((uid != 0) && (uid != s_nvsciipc_uid)) {
		ERR("no permission to set db\n");
		return -EPERM;
	}
#endif /* CONFIG_ANDROID || CONFIG_TEGRA_SYSTEM_TYPE_ACK */

	if (copy_from_user(&get_db, (void __user *)arg, _IOC_SIZE(cmd))) {
		ERR("%s : copy_from_user failed\n", __func__);
		return -EFAULT;
	}

	if (get_db.idx >= ctx->num_eps) {
		INFO("%s : no entry (0x%x)\n", __func__, get_db.idx);
		return -ENOENT;
	}

	get_db.entry = *ctx->db[get_db.idx];

	if (copy_to_user((void __user *)arg, &get_db, _IOC_SIZE(cmd))) {
		ERR("%s : copy_to_user failed\n", __func__);
		return -EFAULT;
	}

	return 0;
}

static int nvsciipc_ioctl_reserve_ep(struct nvsciipc *ctx, unsigned int cmd,
		unsigned long arg)
{
	struct nvsciipc_reserve_ep reserve_ep;
	pid_t current_pid = current->tgid;
	int i;

	if ((ctx->num_eps == 0) || (ctx->set_db_f != true)) {
		ERR("%s[%s:%d] need to set endpoint database first\n", __func__,
			current->comm, get_current()->tgid);
		return -EPERM;
	}

	if (copy_from_user(&reserve_ep, (void __user *)arg, _IOC_SIZE(cmd))) {
		ERR("%s : copy_from_user failed\n", __func__);
		return -EFAULT;
	}
	reserve_ep.ep_name[NVSCIIPC_MAX_EP_NAME - 1] = '\0';

	/* read operation */
	for (i = 0; i < ctx->num_eps; i++) {
		if (!strncmp(reserve_ep.ep_name, ctx->db[i]->ep_name,
		NVSCIIPC_MAX_EP_NAME)) {
#if !defined(CONFIG_ANDROID) && !defined(CONFIG_TEGRA_SYSTEM_TYPE_ACK)
			struct cred const *cred = get_current_cred();
			uid_t const uid = cred->uid.val;

			put_cred(cred);
			/* Authenticate the client process with valid UID */
			if ((ctx->db[i]->uid != 0xFFFFFFFF) &&
				(uid != 0) && (uid != ctx->db[i]->uid)) {
				ERR("%s[%s:%d]: Unauthorized access to %s\n",
					__func__, current->comm, uid, reserve_ep.ep_name);
				return -EACCES;
			}
#endif /* !CONFIG_ANDROID && !CONFIG_TEGRA_SYSTEM_TYPE_ACK */
			mutex_lock(&ep_mutex);
			/* reserve */
			if (reserve_ep.action == NVSCIIPC_EP_RESERVE) {
				struct task_struct *task;
				struct pid *pid_struct;

				pid_struct = find_get_pid(ctx->stat[i]->owner_pid);
				task = pid_task(pid_struct, PIDTYPE_PID);

				/* endpoint is reserved and process is running */
				if (ctx->stat[i]->reserved && task) {
					INFO("%s:RES %s is already reserved by (%s:%d)\n", __func__,
						reserve_ep.ep_name, current->comm,
						ctx->stat[i]->owner_pid);
					mutex_unlock(&ep_mutex);
					return -EBUSY;
				}
				if (!task && (ctx->stat[i]->owner_pid != 0)) {
					INFO("%s:RES pid(%d) for %s is NOT running\n", __func__,
						ctx->stat[i]->owner_pid, reserve_ep.ep_name);
				}

				ctx->stat[i]->reserved = NVSCIIPC_EP_RESERVE;
				ctx->stat[i]->owner_pid = current_pid;
			}
			/* release */
			else if (reserve_ep.action == NVSCIIPC_EP_RELEASE) {
				struct task_struct *task;
				struct pid *pid_struct;

				pid_struct = find_get_pid(ctx->stat[i]->owner_pid);
				task = pid_task(pid_struct, PIDTYPE_PID);

				if (ctx->stat[i]->reserved &&
				((ctx->stat[i]->owner_pid != current_pid) && task)) {
					INFO("%s:REL %s is already reserved by (%s:%d)\n", __func__,
						reserve_ep.ep_name, current->comm,
						ctx->stat[i]->owner_pid);
					mutex_unlock(&ep_mutex);
					return -EPERM;
				}
				if (!task && (ctx->stat[i]->owner_pid != 0)) {
					INFO("%s:REL pid(%d) for %s is NOT running\n", __func__,
					ctx->stat[i]->owner_pid, reserve_ep.ep_name);
				}

				ctx->stat[i]->reserved = NVSCIIPC_EP_RELEASE;
				ctx->stat[i]->owner_pid = 0;
			}
			/* unknown action command */
			else {
				mutex_unlock(&ep_mutex);
				return -EINVAL;
			}
			mutex_unlock(&ep_mutex);
			break;
		}
	}

	if (i == ctx->num_eps) {
		INFO("%s: no entry (%s)\n", __func__, reserve_ep.ep_name);
		return -ENOENT;
	} else if (copy_to_user((void __user *)arg, &reserve_ep,
				_IOC_SIZE(cmd))) {
		ERR("%s : copy_to_user failed\n", __func__);
		return -EFAULT;
	}

	return 0;
}

static int nvsciipc_ioctl_get_db_by_name(struct nvsciipc *ctx, unsigned int cmd,
		unsigned long arg)
{
	struct nvsciipc_get_db_by_name get_db;
	int i;

	if ((ctx->num_eps == 0) || (ctx->set_db_f != true)) {
		ERR("%s[%s:%d] need to set endpoint database first\n", __func__,
			current->comm, get_current()->tgid);
		return -EPERM;
	}

	if (copy_from_user(&get_db, (void __user *)arg, _IOC_SIZE(cmd))) {
		ERR("%s : copy_from_user failed\n", __func__);
		return -EFAULT;
	}
	get_db.ep_name[NVSCIIPC_MAX_EP_NAME - 1] = '\0';

	/* read operation */
	for (i = 0; i < ctx->num_eps; i++) {
		if (!strncmp(get_db.ep_name, ctx->db[i]->ep_name,
			NVSCIIPC_MAX_EP_NAME)) {
#if !defined(CONFIG_ANDROID) && !defined(CONFIG_TEGRA_SYSTEM_TYPE_ACK)
			struct cred const *cred = get_current_cred();
			uid_t const uid = cred->uid.val;

			put_cred(cred);
			/* Authenticate the client process with valid UID */
			if ((ctx->db[i]->uid != 0xFFFFFFFF) &&
				(uid != 0) && (uid != ctx->db[i]->uid)) {
				ERR("%s[%s:%d]: Unauthorized access to %s\n",
					__func__, current->comm, uid, get_db.ep_name);
				return -EACCES;
			}
#endif /* !CONFIG_ANDROID && !CONFIG_TEGRA_SYSTEM_TYPE_ACK */
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
		ERR("%s[%s:%d] need to set endpoint database first\n", __func__,
			current->comm, get_current()->tgid);
		return -EPERM;
	}

	if (copy_from_user(&get_db, (void __user *)arg, _IOC_SIZE(cmd))) {
		ERR("%s : copy_from_user failed\n", __func__);
		return -EFAULT;
	}

	/* read operation */
	for (i = 0; i < ctx->num_eps; i++) {
		if (get_db.vuid == ctx->db[i]->vuid) {
#if !defined(CONFIG_ANDROID) && !defined(CONFIG_TEGRA_SYSTEM_TYPE_ACK)
			struct cred const *cred = get_current_cred();
			uid_t const uid = cred->uid.val;

			put_cred(cred);
			/* Authenticate the client process with valid UID */
			if ((ctx->db[i]->uid != 0xFFFFFFFF) &&
				(uid != 0) && (uid != ctx->db[i]->uid)) {
				ERR("%s[%s:%d]: Unauthorized access to endpoint(0x%llx)\n",
					__func__, current->comm, uid, get_db.vuid);
				return -EACCES;
			}
#endif /* !CONFIG_ANDROID && !CONFIG_TEGRA_SYSTEM_TYPE_ACK */
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
		ERR("%s[%s:%d] need to set endpoint database first\n", __func__,
			current->comm, get_current()->tgid);
		return -EPERM;
	}

	if (copy_from_user(&get_vuid, (void __user *)arg, _IOC_SIZE(cmd))) {
		ERR("%s : copy_from_user failed\n", __func__);
		return -EFAULT;
	}
	get_vuid.ep_name[NVSCIIPC_MAX_EP_NAME - 1] = '\0';

	/* read operation */
	for (i = 0; i < ctx->num_eps; i++) {
		if (!strncmp(get_vuid.ep_name, ctx->db[i]->ep_name,
			NVSCIIPC_MAX_EP_NAME)) {
// FIXME: consider android
#if !defined(CONFIG_ANDROID) && !defined(CONFIG_TEGRA_SYSTEM_TYPE_ACK)
			struct cred const *cred = get_current_cred();
			uid_t const uid = cred->uid.val;

			put_cred(cred);
			/* Authenticate the client process with valid UID */
			if ((ctx->db[i]->uid != 0xFFFFFFFF) &&
				(uid != 0) && (uid != ctx->db[i]->uid)) {
				ERR("%s[%s:%d]: Unauthorized access to %s\n",
					__func__, current->comm, uid, get_vuid.ep_name);
				return -EACCES;
			}
#endif /* !CONFIG_ANDROID && !CONFIG_TEGRA_SYSTEM_TYPE_ACK */
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
	struct cred const *cred = get_current_cred();
	uid_t const uid = cred->uid.val;

	put_cred(cred);

	if (ctx->set_db_f) {
		ERR("db is already set\n");
		return -EPERM;
	}

	INFO("set_db start\n");

#if defined(CONFIG_ANDROID) || defined(CONFIG_TEGRA_SYSTEM_TYPE_ACK)
	if ((uid != SYSTEM_GID) &&
	(uid != 0) &&
	(uid != s_nvsciipc_uid)) {
		ERR("no permission to set db\n");
		return -EPERM;
	}
#else
	/* check root or nvsciipc user */
	if ((uid != 0) &&
	(uid != s_nvsciipc_uid)) {
		ERR("no permission to set db\n");
		return -EPERM;
	}
#endif /* CONFIG_ANDROID || CONFIG_TEGRA_SYSTEM_TYPE_ACK */

	if (copy_from_user(&user_db, (void __user *)arg, _IOC_SIZE(cmd))) {
		ERR("copying user db failed\n");
		return -EFAULT;
	}

	if ((user_db.num_eps <= 0) || (user_db.num_eps > NVSCIIPC_MAX_EP_COUNT)) {
		ERR("invalid value passed for num_eps: %d\n", user_db.num_eps);
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

	if (!access_ok(user_db.entry, ctx->num_eps *
		sizeof(struct nvsciipc_config_entry *))) {
		ERR("invalid user-space DB entry ptr: %p\n", user_db.entry);
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

	ctx->stat = (struct nvsciipc_res_stat **)
		kzalloc(ctx->num_eps * sizeof(struct nvsciipc_res_stat *),
			GFP_KERNEL);

	if (ctx->stat == NULL) {
		ERR("memory allocation for ctx->stat failed\n");
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

		if (!access_ok(entry_ptr[i], sizeof(struct nvsciipc_config_entry))) {
			ERR("invalid user-space CFG entry ptr: %p\n", entry_ptr[i]);
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

		ctx->stat[i] = (struct nvsciipc_res_stat *)
			kzalloc(sizeof(struct nvsciipc_res_stat),
				GFP_KERNEL);

		if (ctx->stat[i] == NULL) {
			ERR("memory allocation for ctx->stat[%d] failed\n", i);
			ret = -EFAULT;
			goto ptr_error;
		}
	}

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

	if (ctx->stat != NULL) {
		for (i = 0; i < ctx->num_eps; i++) {
			if (ctx->stat[i] != NULL) {
				memset(ctx->stat[i], 0, sizeof(struct nvsciipc_res_stat));
				kfree(ctx->stat[i]);
			}
		}

		kfree(ctx->stat);
		ctx->stat = NULL;
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
		ERR("%s[%s:%d] need to set endpoint database first\n", __func__,
			current->comm, get_current()->tgid);
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

static int nvsciipc_ioctl_check_dt(struct nvsciipc *ctx, unsigned int cmd,
        unsigned long arg)
{
    struct device_node *chosen_node;
    struct device_node *nvsciipc_node;
    struct device_node *nvsciipc_user_node;
    int ret = 0;

    /* Find the nvsciipc node under chosen */
    chosen_node = of_find_node_by_path("/chosen");
    if (!chosen_node) {
        ERR("Failed to find /chosen node\n");
        ret = -ENODEV;
        goto exit;
    }

    nvsciipc_node = of_get_child_by_name(chosen_node, "nvsciipc");
    if (!nvsciipc_node) {
        INFO("Failed to find nvsciipc node\n");
        ret = -ENODEV;
        of_node_put(chosen_node);
        goto exit;
    }

    nvsciipc_user_node = of_get_child_by_name(chosen_node, "nvsciipc_user");
    if (!nvsciipc_user_node) {
        INFO("Failed to find nvsciipc user node\n");
        ret = -ENODEV;
        of_node_put(nvsciipc_node);
        of_node_put(chosen_node);
        goto exit;
    }

    /* Node exists, check if it has the required properties */
    if (!of_find_property(nvsciipc_node, "nvsciipc,channel-db", NULL)) {
        ERR("nvsciipc,channel-db property not found\n");
        ret = -ENODEV;
    }

    if (!of_find_property(nvsciipc_user_node, "nvsciipc,channel-db-user", NULL)) {
        ERR("nvsciipc,channel-db-user property not found\n");
        ret = -ENODEV;
    }

    of_node_put(nvsciipc_user_node);
    of_node_put(nvsciipc_node);
    of_node_put(chosen_node);

exit:
    if (copy_to_user((void __user *)arg, &ret, sizeof(ret))) {
        ERR("copy_to_user failed\n");
        return -EFAULT;
    }

    return 0;
}

static int nvsciipc_ioctl_get_db(struct nvsciipc *ctx, unsigned int cmd,
        unsigned long arg)
{
    struct nvsciipc_db user_db;
    struct nvsciipc_config_entry __user *user_entry;
    int ret = 0;
    int i;
    struct cred const *cred = get_current_cred();
    uid_t const uid = cred->uid.val;

	put_cred(cred);

    /* Check permissions */
#if defined(CONFIG_ANDROID) || defined(CONFIG_TEGRA_SYSTEM_TYPE_ACK)
    if ((uid != SYSTEM_GID) && (uid != 0) && (uid != s_nvsciipc_uid)) {
        ERR("Permission denied for UID %d\n", uid);
        return -EPERM;
    }
#else
    if ((uid != 0) && (uid != s_nvsciipc_uid)) {
        ERR("Permission denied for UID %d\n", uid);
        return -EPERM;
    }
#endif

    /* Verify database is initialized */
    if (!ctx->set_db_f) {
        ERR("Database not initialized\n");
        return -EPERM;
    }

    /* Copy input structure from user */
    if (copy_from_user(&user_db, (void __user *)arg, sizeof(user_db))) {
        ERR("Failed to copy user_db from user space\n");
        return -EFAULT;
    }

    /* Verify user buffer size matches database size */
    if (user_db.num_eps != ctx->num_eps) {
        ERR("Mismatched db size: user=%d, kernel=%d\n", 
                user_db.num_eps, ctx->num_eps);
        return -EINVAL;
    }

    /* Verify user's entry pointer array is accessible */
    if (!access_ok(user_db.entry, ctx->num_eps * sizeof(struct nvsciipc_config_entry *))) {
        ERR("Invalid user space entry pointer array\n");
        return -EFAULT;
    }

    /* Copy each database entry to user space */
    for (i = 0; i < ctx->num_eps; i++) {
        /* Get user space pointer for this entry */
        if (get_user(user_entry, &user_db.entry[i])) {
			ERR("Failed to get user entry pointer %d\n", i);
            ret = -EFAULT;
            goto cleanup;
        }

        /* Copy entry to user space */
        if (copy_to_user(user_entry, ctx->db[i], sizeof(struct nvsciipc_config_entry))) {
            ERR("Failed to copy entry %d to user space\n", i);
            ret = -EFAULT;
            goto cleanup;
        }
    }

    /* Copy updated user_db structure back to user space */
    if (copy_to_user((void __user *)arg, &user_db, sizeof(user_db))) {
        ERR("Failed to copy database structure back to user\n");
        ret = -EFAULT;
        goto cleanup;
    }

	ctx->set_db_f = false;
    INFO("Successfully copied %d database entries to user space\n",
            ctx->num_eps);

cleanup:
    return ret;
}

long nvsciipc_dev_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	struct nvsciipc *ctx = filp->private_data;
	long ret = 0;

	if (_IOC_TYPE(cmd) != NVSCIIPC_IOCTL_MAGIC) {
		ERR("%s[%s:%d]: not a nvsciipc ioctl\n", __func__,
			current->comm, get_current()->tgid);
		ret = -ENOTTY;
		goto exit;
	}

	if (_IOC_NR(cmd) > NVSCIIPC_IOCTL_NUMBER_MAX) {
		ERR("%s[%s:%d]: wrong nvsciipc ioctl cmd(MAX:%d): 0x%x\n", __func__,
			current->comm, get_current()->tgid, NVSCIIPC_IOCTL_NUMBER_MAX, cmd);
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
	case NVSCIIPC_IOCTL_CHECK_DT_NODE:
		ret = nvsciipc_ioctl_check_dt(ctx, cmd, arg);
		break;
	case NVSCIIPC_IOCTL_GET_DB_BY_NAME:
		ret = nvsciipc_ioctl_get_db_by_name(ctx, cmd, arg);
		break;
	case NVSCIIPC_IOCTL_RESERVE_EP:
		ret = nvsciipc_ioctl_reserve_ep(ctx, cmd, arg);
		break;
	case NVSCIIPC_IOCTL_GET_DB_BY_VUID:
		ret = nvsciipc_ioctl_get_db_by_vuid(ctx, cmd, arg);
		break;
	case NVSCIIPC_IOCTL_GET_DB_BY_IDX:
		ret = nvsciipc_ioctl_get_db_by_idx(ctx, cmd, arg);
		break;
	case NVSCIIPC_IOCTL_GET_DB_SIZE:
		ret = nvsciipc_ioctl_get_dbsize(ctx, cmd, arg);
		break;
	case NVSCIIPC_IOCTL_VALIDATE_AUTH_TOKEN:
		ret = nvsciipc_ioctl_validate_auth_token(ctx, cmd, arg);
		break;
	case NVSCIIPC_IOCTL_MAP_VUID:
		ret = nvsciipc_ioctl_map_vuid(ctx, cmd, arg);
		break;
	case NVSCIIPC_IOCTL_GET_DB:
		ret = nvsciipc_ioctl_get_db(ctx, cmd, arg);
		break;
	case NVSCIIPC_IOCTL_GET_VMID:
		if (copy_to_user((void __user *) arg, &s_guestid,
			sizeof(s_guestid))) {
			ret = -EFAULT;
		}
		break;
	default:
		ERR("%s[%s:%d]: unrecognised ioctl cmd: 0x%x\n", __func__,
			current->comm, get_current()->tgid, cmd);
		ret = -ENOTTY;
		break;
	}

exit:
#if defined(CONFIG_FUNCTION_ERROR_INJECTION) && defined(CONFIG_BPF_KPROBE_OVERRIDE)
	ALLOW_ERROR_INJECTION(nvsciipc_dev_ioctl, ERRNO);
#endif /* CONFIG_FUNCTION_ERROR_INJECTION && defined(CONFIG_BPF_KPROBE_OVERRIDE */
	return ret;
}

static ssize_t nvsciipc_dbg_read(struct file *filp, char __user *buf,
		size_t count, loff_t *f_pos)
{
	struct nvsciipc *ctx = filp->private_data;
	int i;
	struct cred const *cred = get_current_cred();
	uid_t const uid = cred->uid.val;

	put_cred(cred);
	/* check root user */
	if ((uid != 0) && (uid != s_nvsciipc_uid)) {
		ERR("no permission to read db\n");
		return -EPERM;
	}

	if (ctx->set_db_f != true) {
		ERR("%s[%s:%d] need to set endpoint database first\n", __func__,
			current->comm, get_current()->tgid);
		return -EPERM;
	}

	mutex_lock(&nvsciipc_mutex);
	mutex_lock(&ep_mutex);
	for (i = 0; i < ctx->num_eps; i++) {
		INFO("EP[%03d]: ep_name:%s, dev_name:%s, backend:%u, nframes:%u, frame_size:%u, id:%u, noti:%d(TRAP:1,MSI:2), uid:%d, res:%d, pid:%d\n",
			i, ctx->db[i]->ep_name,
			ctx->db[i]->dev_name,
			ctx->db[i]->backend,
			ctx->db[i]->nframes,
			ctx->db[i]->frame_size,
			ctx->db[i]->id,
			ctx->db[i]->noti_type,
			ctx->db[i]->uid,
			ctx->stat[i]->reserved,
			ctx->stat[i]->owner_pid);
	}
	mutex_unlock(&ep_mutex);
	mutex_unlock(&nvsciipc_mutex);

	return 0;
}

static ssize_t nvsciipc_uid_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", s_nvsciipc_uid);
}

static ssize_t nvsciipc_uid_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	static int init_done;
	uint32_t val;
	int ret;

	if (init_done) {
		ERR("UID is already set as %d\n", s_nvsciipc_uid);
		return -EPERM;
	}

	ret = kstrtou32(buf, 0, &val);
	if (ret) {
		ERR("Failed to store nvsciipc UID\n");
		return ret;
	}

	s_nvsciipc_uid = val;
	init_done = 1;
	INFO("nvsciipc_uid is set as %d\n", s_nvsciipc_uid);

	return count;
}

// /sys/devices/platform/nvsciipc/nvsciipc_uid
static DEVICE_ATTR(nvsciipc_uid, 0660, nvsciipc_uid_show, nvsciipc_uid_store);

static struct attribute *nvsciipc_uid_attrs[] = {
	&dev_attr_nvsciipc_uid.attr,
	NULL,
};

static const struct attribute_group nvsciipc_uid_group = {
	.attrs = nvsciipc_uid_attrs,
};


static const struct file_operations nvsciipc_fops = {
	.owner		= THIS_MODULE,
	.open		= nvsciipc_dev_open,
	.release		= nvsciipc_dev_release,
	.unlocked_ioctl	= nvsciipc_dev_ioctl,
#if defined(NV_NO_LLSEEK_PRESENT)
	.llseek		= no_llseek,
#endif
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

	s_ctx = devm_kzalloc(&pdev->dev, sizeof(struct nvsciipc),	GFP_KERNEL);
	if (s_ctx == NULL) {
		ERR("devm_kzalloc failed for nvsciipc\n");
		ret = -ENOMEM;
		goto error;
	}
	s_ctx->set_db_f = false;

	s_ctx->dev = &(pdev->dev);
	platform_set_drvdata(pdev, s_ctx);

#if defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG) /* Linux v6.4 */
	s_ctx->nvsciipc_class = class_create(MODULE_NAME);
#else
	s_ctx->nvsciipc_class = class_create(THIS_MODULE, MODULE_NAME);
#endif
	if (IS_ERR(s_ctx->nvsciipc_class)) {
		ERR("failed to create class: %ld\n",
			PTR_ERR(s_ctx->nvsciipc_class));
		ret = PTR_ERR(s_ctx->nvsciipc_class);
		goto error;
	}

	dev_info(&pdev->dev, "creating nvsciipc_uid sysfs group\n");
	ret = sysfs_create_group(&pdev->dev.kobj, &nvsciipc_uid_group);
	if (ret < 0) {
		dev_err(&pdev->dev, "%s: Failed to reate sysfs group, %d\n",
			__func__, ret);
		goto error;
	}
	dev_info(&pdev->dev, "nvsciipc_uid sysfs group: done\n");

	ret = alloc_chrdev_region(&(s_ctx->dev_t), 0, 1, MODULE_NAME);
	if (ret != 0) {
		ERR("alloc_chrdev_region() failed\n");
		goto error;
	}

	s_ctx->dev_t = MKDEV(MAJOR(s_ctx->dev_t), 0);
	cdev_init(&s_ctx->cdev, &nvsciipc_fops);
	s_ctx->cdev.owner = THIS_MODULE;

	ret = cdev_add(&(s_ctx->cdev), s_ctx->dev_t, 1);
	if (ret != 0) {
		ERR("cdev_add() failed\n");
		goto error;
	}

	if (snprintf(s_ctx->device_name, (MAX_NAME_SIZE - 1), "%s", MODULE_NAME) < 0) {
		pr_err("snprintf() failed\n");
		ret = -ENOMEM;
		goto error;
	}

	s_ctx->device = device_create(s_ctx->nvsciipc_class, NULL,
			s_ctx->dev_t, s_ctx,
			s_ctx->device_name, 0);
	if (IS_ERR(s_ctx->device)) {
		ret = PTR_ERR(s_ctx->device);
		ERR("device_create() failed\n");
		goto error;
	}
	dev_set_drvdata(s_ctx->device, s_ctx);

	if (is_tegra_hypervisor_mode()) {
		ret = hyp_read_gid(&s_guestid);
		if (ret != 0) {
			ERR("Failed to read guest id\n");
			goto error;
		}
		INFO("guestid: %d\n", s_guestid);
	}

	/* Parse device tree configuration */
	ret = nvsciipc_parse_dt(s_ctx);
	if (ret) {
		ERR("Failed to parse device tree\n");
		goto error;
	}

	INFO("loaded module\n");

	return ret;

error:
	nvsciipc_cleanup(s_ctx);

	return ret;
}

static void nvsciipc_cleanup(struct nvsciipc *ctx)
{
	if (ctx == NULL)
		return;

	sysfs_remove_group(&ctx->dev->kobj, &nvsciipc_uid_group);

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
	ERR("Unloaded module\n");

	return 0;
}

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void nvsciipc_remove_wrapper(struct platform_device *pdev)
{
	nvsciipc_remove(pdev);
}
#else
static int nvsciipc_remove_wrapper(struct platform_device *pdev)
{
	return nvsciipc_remove(pdev);
}
#endif

static void nvsciipc_shutdown(struct platform_device *pdev)
{
	dev_err(&pdev->dev, "nvipc: Shutting down");
	nvsciipc_remove(pdev);
}

#ifdef CONFIG_PM
static int nvsciipc_suspend(struct platform_device *pdev, pm_message_t state)
{
	dev_notice(&pdev->dev, "nvipc: Suspended\n");

	return 0;
}

static int nvsciipc_resume(struct platform_device *pdev)
{
	dev_notice(&pdev->dev, "nvipc: Resuming\n");

	return 0;
}
#endif /* CONFIG_PM */

static struct platform_driver nvsciipc_driver = {
	.probe  = nvsciipc_probe,
	.remove = nvsciipc_remove_wrapper,
	.shutdown = nvsciipc_shutdown,
	.driver = {
		.name = MODULE_NAME,
	},
#ifdef CONFIG_PM
	.suspend = nvsciipc_suspend,
	.resume = nvsciipc_resume,
#endif /* CONFIG_PM */
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
	sysfs_remove_group(&s_ctx->dev->kobj, &nvsciipc_uid_group);

	// calls nvsciipc_remove internally
	platform_device_unregister(nvsciipc_pdev);

	platform_driver_unregister(&nvsciipc_driver);
}

module_init(nvsciipc_module_init);
module_exit(nvsciipc_module_deinit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Nvidia Corporation");
