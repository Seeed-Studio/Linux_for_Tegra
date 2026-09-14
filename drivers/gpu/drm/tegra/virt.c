// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2025, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
 */

#include <nvidia/conftest.h>

#include <linux/debugfs.h>
#include <linux/host1x-next.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/seq_file.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>

#include <soc/tegra/bpmp.h>
#include <soc/tegra/virt/hv-ivc.h>

#include "drm.h"

#ifndef CONFIG_TEGRA_SYSTEM_TYPE_ACK
#include <uapi/linux/tegra-soc-hwpm-uapi.h>
#endif

#define TEGRA_VHOST_CMD_CONNECT			0
#define TEGRA_VHOST_CMD_DISCONNECT		1
#define TEGRA_VHOST_CMD_SUSPEND			2
#define TEGRA_VHOST_CMD_RESUME			3
#define TEGRA_VHOST_CMD_GET_CONNECTION_ID	4
#define TEGRA_VHOST_CMD_POWER_MODE_CHANGE	5

struct tegra_vhost_connect_params {
	u32 module;
	u64 connection_id;
};

struct tegra_vhost_power_mode_change_params {
	u32 ip_op_mode;
};

struct tegra_vhost_cmd_msg {
	u32 cmd;
	int ret;
	u64 connection_id;
	struct tegra_vhost_connect_params connect;
	struct tegra_vhost_power_mode_change_params pw_mode;
};

struct virt_engine {
	struct device *dev;
	int connection_id;
	struct tegra_drm_client client;
#ifdef CONFIG_DEBUG_FS
	struct dentry *actmon_debugfs_dir;
#endif
	struct kobject kobj;
	struct kobj_attribute powermode_attr;
	u32 powermode;
	const char *name;
	unsigned long rate;
};

static DEFINE_MUTEX(ivc_cookie_lock);
static struct tegra_hv_ivc_cookie *ivc_cookie;
static struct kref ivc_ref;

static inline struct virt_engine *to_virt_engine(struct tegra_drm_client *client)
{
	return container_of(client, struct virt_engine, client);
}

static int virt_engine_init(struct host1x_client *client)
{
	struct tegra_drm_client *drm = host1x_to_drm_client(client);
	struct drm_device *dev = dev_get_drvdata(client->host);
	struct tegra_drm *tegra = dev->dev_private;
	int err;

	err = tegra_drm_register_client(tegra, drm);
	if (err < 0)
		return err;

	/*
	 * Inherit the DMA parameters (such as maximum segment size) from the
	 * parent host1x device.
	 */
	client->dev->dma_parms = client->host->dma_parms;

	return 0;
}

static int virt_engine_exit(struct host1x_client *client)
{
	struct tegra_drm_client *drm = host1x_to_drm_client(client);
	struct drm_device *dev = dev_get_drvdata(client->host);
	struct tegra_drm *tegra = dev->dev_private;
	int err;

	/* avoid a dangling pointer just in case this disappears */
	client->dev->dma_parms = NULL;

	err = tegra_drm_unregister_client(tegra, drm);
	if (err < 0)
		return err;

	return 0;
}

static const struct host1x_client_ops virt_engine_client_ops = {
	.init = virt_engine_init,
	.exit = virt_engine_exit,
};

static int virt_engine_open_channel(struct tegra_drm_client *client,
				    struct tegra_drm_context *context)
{
	return -EINVAL;
}

static void virt_engine_close_channel(struct tegra_drm_context *context)
{
}

static int virt_engine_can_use_memory_ctx(struct tegra_drm_client *client, bool *supported)
{
	*supported = true;

	return 0;
}

static int virt_engine_has_job_timestamping(struct tegra_drm_client *client, bool *supported,
				    u32 *timestamp_shift)
{
	*supported = true;
	*timestamp_shift = 5;

	if (of_machine_is_compatible("nvidia,tegra264"))
		*timestamp_shift = 0;

	return 0;
}

static int virt_skip_bl_swizzling(struct tegra_drm_client *client, bool *skip)
{
	if (of_machine_is_compatible("nvidia,tegra264"))
		*skip = true;

	return 0;
}

static const struct tegra_drm_client_ops virt_engine_ops = {
	.open_channel = virt_engine_open_channel,
	.close_channel = virt_engine_close_channel,
	.submit = tegra_drm_submit,
	.get_streamid_offset = tegra_drm_get_streamid_offset_thi,
	.can_use_memory_ctx = virt_engine_can_use_memory_ctx,
	.has_job_timestamping = virt_engine_has_job_timestamping,
	.skip_bl_swizzling = virt_skip_bl_swizzling,
};

static int virt_engine_setup_ivc(struct virt_engine *virt)
{
	struct device_node *host1x_dn = virt->dev->parent->of_node;
	struct device_node *hv;
	u32 ivc_instance;
	int err;

	mutex_lock(&ivc_cookie_lock);

	if (ivc_cookie) {
		err = PTR_ERR_OR_ZERO(ivc_cookie);
		if (!err)
			kref_get(&ivc_ref);
		mutex_unlock(&ivc_cookie_lock);
		return err;
	}

	hv = of_parse_phandle(host1x_dn, "nvidia,server-ivc", 0);
	if (!hv) {
		dev_err(virt->dev, "nvidia,server-ivc not configured\n");
		return -EINVAL;
	}

	err = of_property_read_u32_index(host1x_dn, "nvidia,server-ivc", 1, &ivc_instance);
	if (err) {
		dev_err(virt->dev, "nvidia,server-ivc not configured\n");
		return -EINVAL;
	}

	ivc_cookie = tegra_hv_ivc_reserve(hv, ivc_instance, NULL);
	if (IS_ERR(ivc_cookie)) {
		dev_err(virt->dev, "IVC channel reservation failed: %ld\n", PTR_ERR(ivc_cookie));
		return PTR_ERR(ivc_cookie);
	}

	tegra_hv_ivc_channel_reset(ivc_cookie);

	while (tegra_hv_ivc_channel_notified(ivc_cookie))
		;

	kref_init(&ivc_ref);

	mutex_unlock(&ivc_cookie_lock);

	return 0;
}

static void release_ivc_cookie(struct kref *ref)
{
	(void)ref;

	tegra_hv_ivc_unreserve(ivc_cookie);
	mutex_unlock(&ivc_cookie_lock);
}

static void virt_engine_cleanup(void)
{
	kref_put_mutex(&ivc_ref, release_ivc_cookie, &ivc_cookie_lock);
}

static int virt_engine_transfer(void *data, u32 size)
{
	int err;

	mutex_lock(&ivc_cookie_lock);

	while (!tegra_hv_ivc_can_write(ivc_cookie))
		;

	err = tegra_hv_ivc_write(ivc_cookie, data, size);
	if (err != size) {
		mutex_unlock(&ivc_cookie_lock);
		return -ENOMEM;
	}

	while (!tegra_hv_ivc_can_read(ivc_cookie))
		;

	err = tegra_hv_ivc_read(ivc_cookie, data, size);
	if (err != size) {
		mutex_unlock(&ivc_cookie_lock);
		return -EIO;
	}

	mutex_unlock(&ivc_cookie_lock);
	return 0;
}

static int virt_engine_connect(struct virt_engine *virt, u32 module_id)
{
	struct tegra_vhost_cmd_msg msg = { 0 };
	int err;

	msg.cmd = TEGRA_VHOST_CMD_CONNECT;
	msg.connect.module = module_id;

	err = virt_engine_transfer(&msg, sizeof(msg));
	if (err < 0)
		return err;

	return msg.connect.connection_id;
}

#ifdef CONFIG_DEBUG_FS
static int mrq_debug_open(struct tegra_bpmp *bpmp, const char *name,
			  u32 *fd, u32 *len, bool write)
{
	struct mrq_debug_request req = {
		.cmd = cpu_to_le32(write ? CMD_DEBUG_OPEN_WO : CMD_DEBUG_OPEN_RO),
	};
	struct mrq_debug_response resp;
	struct tegra_bpmp_message msg = {
		.mrq = MRQ_DEBUG,
		.tx = {
			.data = &req,
			.size = sizeof(req),
		},
		.rx = {
			.data = &resp,
			.size = sizeof(resp),
		},
	};
	ssize_t sz_name;
	int err = 0;

	sz_name = strscpy(req.fop.name, name, sizeof(req.fop.name));
	if (sz_name < 0) {
		pr_err("File name too large: %s\n", name);
		return -EINVAL;
	}

	err = tegra_bpmp_transfer(bpmp, &msg);
	if (err < 0)
		return err;
	else if (msg.rx.ret < 0)
		return -EINVAL;

	*len = resp.fop.datalen;
	*fd = resp.fop.fd;

	return 0;
}

static int mrq_debug_close(struct tegra_bpmp *bpmp, u32 fd)
{
	struct mrq_debug_request req = {
		.cmd = cpu_to_le32(CMD_DEBUG_CLOSE),
		.frd = {
			.fd = fd,
		},
	};
	struct mrq_debug_response resp;
	struct tegra_bpmp_message msg = {
		.mrq = MRQ_DEBUG,
		.tx = {
			.data = &req,
			.size = sizeof(req),
		},
		.rx = {
			.data = &resp,
			.size = sizeof(resp),
		},
	};
	int err = 0;

	err = tegra_bpmp_transfer(bpmp, &msg);
	if (err < 0)
		return err;
	else if (msg.rx.ret < 0)
		return -EINVAL;

	return 0;
}

static int mrq_debug_read(struct tegra_bpmp *bpmp, const char *name,
			  char *data, size_t sz_data, u32 *nbytes)
{
	struct mrq_debug_request req = {
		.cmd = cpu_to_le32(CMD_DEBUG_READ),
	};
	struct mrq_debug_response resp;
	struct tegra_bpmp_message msg = {
		.mrq = MRQ_DEBUG,
		.tx = {
			.data = &req,
			.size = sizeof(req),
		},
		.rx = {
			.data = &resp,
			.size = sizeof(resp),
		},
	};
	u32 fd = 0, len = 0;
	int remaining, err, close_err;

	err = mrq_debug_open(bpmp, name, &fd, &len, 0);
	if (err)
		goto out;

	if (len > sz_data) {
		err = -EFBIG;
		goto close;
	}

	req.frd.fd = fd;
	remaining = len;

	while (remaining > 0) {
		err = tegra_bpmp_transfer(bpmp, &msg);
		if (err < 0) {
			goto close;
		} else if (msg.rx.ret < 0) {
			err = -EINVAL;
			goto close;
		}

		if (resp.frd.readlen > remaining) {
			pr_err("%s: read data length invalid\n", __func__);
			err = -EINVAL;
			goto close;
		}

		memcpy(data, resp.frd.data, resp.frd.readlen);
		data += resp.frd.readlen;
		remaining -= resp.frd.readlen;
	}

	*nbytes = len;

close:
	close_err = mrq_debug_close(bpmp, fd);
	if (!err)
		err = close_err;
out:
	return err;
}

static const struct of_device_id tegra_bpmp_hv_match[] = {
	{ .compatible = "nvidia,tegra186-bpmp-hv" },
	{ .compatible = "nvidia,tegra194-safe-bpmp-hv" },
	{ }
};

static struct tegra_bpmp *get_bpmp(void)
{
	struct platform_device *pdev;
	struct device_node *bpmp_dev;
	struct tegra_bpmp *bpmp;

	/* Check for bpmp device status in DT */
	bpmp_dev = of_find_matching_node_and_match(NULL, tegra_bpmp_hv_match, NULL);
	if (!bpmp_dev) {
		bpmp = ERR_PTR(-ENODEV);
		goto err_out;
	}
	if (!of_device_is_available(bpmp_dev)) {
		bpmp = ERR_PTR(-ENODEV);
		goto err_put;
	}

	pdev = of_find_device_by_node(bpmp_dev);
	if (!pdev) {
		bpmp = ERR_PTR(-ENODEV);
		goto err_put;
	}

	bpmp = platform_get_drvdata(pdev);
	if (!bpmp) {
		bpmp = ERR_PTR(-EPROBE_DEFER);
		put_device(&pdev->dev);
		goto err_put;
	}

	return bpmp;
err_put:
	of_node_put(bpmp_dev);
err_out:
	return bpmp;
}

static unsigned long get_clk_rate(const char *name)
{
	char path[50], data[50] = {0};
	struct tegra_bpmp *bpmp;
	unsigned long rate;
	uint32_t len;
	int err;

	bpmp = get_bpmp();
	if (IS_ERR(bpmp))
		return PTR_ERR(bpmp);

	sprintf(path, "clk/%s/rate", name);
	err = mrq_debug_read(bpmp, path, data, ARRAY_SIZE(data)-1, &len);
	if (err < 0)
		goto put_bpmp;

	err = kstrtoul(data, 10, &rate);
	if (err < 0)
		goto put_bpmp;

	return rate;

put_bpmp:
	tegra_bpmp_put(bpmp);
	return 0;
}

static int actmon_debugfs_usage_show(struct seq_file *s, void *unused)
{
	struct virt_engine *virt = s->private;
	unsigned long rate;
	int cycles_per_actmon_sample;
	int count;

	if (virt->rate) {
		rate = virt->rate;
	} else {
		rate = get_clk_rate(virt->name);
		if (rate == 0) {
			seq_printf(s, "0\n");
			return 0;
		}
		virt->rate = rate;
	}

	count = host1x_actmon_read_avg_count(&virt->client.base);
	if (count < 0)
		return count;

	/* Based on configuration in NvHost Server */
#define ACTMON_SAMPLE_PERIOD_US		100
	/* Rate in MHz cancels out microseconds */
	cycles_per_actmon_sample = (rate / 1000000) * ACTMON_SAMPLE_PERIOD_US;

	seq_printf(s, "%d\n", (count * 1000) / cycles_per_actmon_sample);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(actmon_debugfs_usage);

static void virt_engine_setup_actmon_debugfs(struct virt_engine *virt)
{
	const char *dir;

	switch (virt->client.base.class) {
	case HOST1X_CLASS_VIC:
		virt->name = "vic";
		dir = "vic";
		break;
	case HOST1X_CLASS_NVENC:
		virt->name = "nvenc";
		dir = "msenc";
		break;
	case HOST1X_CLASS_NVDEC:
		virt->name = "nvdec";
		dir = "nvdec";
		break;
	default:
		return;
	}

	virt->actmon_debugfs_dir = debugfs_create_dir(dir, NULL);
	debugfs_create_file("usage", S_IRUGO, virt->actmon_debugfs_dir, virt, &actmon_debugfs_usage_fops);
}

static void virt_engine_cleanup_actmon_debugfs(struct virt_engine *virt)
{
	debugfs_remove_recursive(virt->actmon_debugfs_dir);
}
#endif

#ifdef CONFIG_TEGRA_HOST1X_POWERMODE_CONTROL
static int virt_engine_power_mode_change(struct virt_engine *virt, u32 ip_op_mode)
{
	struct tegra_vhost_cmd_msg msg = { 0 };
	struct tegra_vhost_power_mode_change_params *p = &msg.pw_mode;

	msg.cmd = TEGRA_VHOST_CMD_POWER_MODE_CHANGE;
	p->ip_op_mode = ip_op_mode;
	return virt_engine_transfer(&msg, sizeof(msg));
}

static ssize_t virt_engine_powermode_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct virt_engine *virt = container_of(kobj, struct virt_engine, kobj);
	return sprintf(buf, "%d\n", virt->powermode);
}

static ssize_t virt_engine_powermode_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct virt_engine *virt;
	int ret;

	virt = container_of(kobj, struct virt_engine, kobj);
	ret = kstrtoint(buf, 0, &virt->powermode);
	if (ret < 0)
		return ret;

	/* Send Power Mode transition command to NvHost_Server VM */
	virt_engine_power_mode_change(virt, virt->powermode);
	return count;
}

static struct kobj_type ktype_powermode = {
	.sysfs_ops = &kobj_sysfs_ops,
};

static int virt_engine_sysfs_init(struct virt_engine *virt)
{
	int ret;

	if (virt == NULL)
		return -EINVAL;

	/* Create sysfs file */
	virt->powermode_attr.attr.name = "mode";
	virt->powermode_attr.attr.mode = 0644;
	virt->powermode_attr.show = virt_engine_powermode_show;
	virt->powermode_attr.store = virt_engine_powermode_store;
	sysfs_attr_init(&virt->powermode_attr.attr);

	/* Initialize kobject */
	ret = kobject_init_and_add(&virt->kobj, &ktype_powermode, &virt->dev->kobj, "powerprofile");
	if (ret) {
		kobject_put(&virt->kobj);
		return ret;
	}

	ret = sysfs_create_file(&virt->kobj, &virt->powermode_attr.attr);
	if (ret)
		kobject_put(&virt->kobj);

	return ret;
}

static void virt_engine_sysfs_exit(struct virt_engine *virt)
{
	sysfs_remove_file(&virt->kobj, &virt->powermode_attr.attr);
	kobject_put(&virt->kobj);
}
#endif

static const struct of_device_id tegra_virt_engine_of_match[] = {
	{ .compatible = "nvidia,tegra234-host1x-virtual-engine" },
	{ .compatible = "nvidia,tegra264-host1x-virtual-engine" },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_virt_engine_of_match);

#ifndef CONFIG_TEGRA_SYSTEM_TYPE_ACK
static u32 virt_engine_get_ip_index(const char *name)
{
	if (strstr(name, "vic")) {
		return (u32)TEGRA_SOC_HWPM_RESOURCE_VIC;
	} else if (strstr(name, "nvenc")) {
		return (u32)TEGRA_SOC_HWPM_RESOURCE_NVENC;
	} else if (strstr(name, "ofa")) {
		return (u32)TEGRA_SOC_HWPM_RESOURCE_OFA;
	} else if (strstr(name, "nvdec")) {
		return (u32)TEGRA_SOC_HWPM_RESOURCE_NVDEC;
	}
	return (u32)TERGA_SOC_HWPM_NUM_IPS;
}

static u64 virt_engine_extract_base_addr(struct platform_device *pdev)
{
	u32 hwpm_ip_index;
	u64 base_address = 0U;

	hwpm_ip_index = virt_engine_get_ip_index(pdev->name);
	switch (hwpm_ip_index) {
	case TEGRA_SOC_HWPM_RESOURCE_VIC:
		if (of_machine_is_compatible("nvidia,tegra234")) {
			base_address = 0x15340000;
		} else if (of_machine_is_compatible("nvidia,tegra264")) {
			base_address = 0x8188050000;
		}
		break;
	case TEGRA_SOC_HWPM_RESOURCE_NVENC:
		if (of_machine_is_compatible("nvidia,tegra234")) {
			base_address = 0x154c0000;
		}
		break;
	case TEGRA_SOC_HWPM_RESOURCE_OFA:
		if (of_machine_is_compatible("nvidia,tegra234")) {
			base_address = 0x15a50000;
		}
		break;
	case TEGRA_SOC_HWPM_RESOURCE_NVDEC:
		if (of_machine_is_compatible("nvidia,tegra234")) {
			base_address = 0x15480000;
		}
		break;
	default:
		dev_err(&pdev->dev, "IP Base address not found");
		break;
	}

	return base_address;
}

static int virt_engine_ip_pm(void *ip_dev, bool disable)
{
	int err = 0;
	struct platform_device *pdev = (struct platform_device *)ip_dev;

	if (disable) {
		dev_warn(&pdev->dev, "Power Mgmt not impleted in vhost_engine."
				"IP expected to be ON");
	} else {
		dev_warn(&pdev->dev, "Power Mgmt not impleted in vhost_engine."
				"IP expected to be ON");
	}

	return err;
}

static int virt_engine_ip_reg_op(void *ip_dev,
		enum tegra_soc_hwpm_ip_reg_op reg_op,
		u32 inst_element_index, u64 reg_offset, u32 *reg_data)
{
	struct platform_device *pdev = (struct platform_device *)ip_dev;
	u64 base_address;

	base_address = virt_engine_extract_base_addr(pdev);
	if (base_address <= 0) {
		dev_err(&pdev->dev, "IP Base address not found. HWPM Reg_OP failed");
		return -ENOMEM;
	}

	if (reg_op == TEGRA_SOC_HWPM_IP_REG_OP_READ) {
		void __iomem *ptr = NULL;

		reg_offset = reg_offset + base_address;
		ptr = ioremap(reg_offset, 0x4);
		if (!ptr) {
			dev_err(&pdev->dev, "Failed to map IP Perfmux register (0x%llx)",
					reg_offset);
		}
		*reg_data = __raw_readl(ptr);
		iounmap(ptr);
	} else if (reg_op == TEGRA_SOC_HWPM_IP_REG_OP_WRITE) {
		void __iomem *ptr = NULL;

		reg_offset = reg_offset + base_address;

		ptr = ioremap(reg_offset, 0x4);
		if (!ptr) {
			dev_err(&pdev->dev, "Failed to map IP Perfmux register (0x%llx)",
					reg_offset);
		}
		__raw_writel(*reg_data, ptr);
		iounmap(ptr);
	}

	return 0;
}
#endif

static int virt_engine_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	u32 module_id, class;
	struct virt_engine *virt;
	int err;
#ifndef CONFIG_TEGRA_SYSTEM_TYPE_ACK
        struct tegra_soc_hwpm_ip_ops hwpm_ip_ops;
        u32 hwpm_ip_index;
#endif

	err = of_property_read_u32(pdev->dev.of_node, "nvidia,module-id", &module_id);
	if (err < 0) {
		dev_err(dev, "could not read property nvidia,module-id: %d\n", err);
		return err;
	}

	err = of_property_read_u32(pdev->dev.of_node, "nvidia,class", &class);
	if (err < 0) {
		dev_err(dev, "could not read property nvidia,class: %d\n", err);
		return err;
	}

	/* inherit DMA mask from host1x parent */
	err = dma_coerce_mask_and_coherent(dev, *dev->parent->dma_mask);
	if (err < 0) {
		dev_err(dev, "failed to set DMA mask: %d\n", err);
		return err;
	}

	virt = devm_kzalloc(dev, sizeof(*virt), GFP_KERNEL);
	if (!virt)
		return -ENOMEM;

	platform_set_drvdata(pdev, virt);

	INIT_LIST_HEAD(&virt->client.base.list);
	virt->client.base.ops = &virt_engine_client_ops;
	virt->client.base.dev = dev;
	virt->client.base.class = class;
	virt->client.base.syncpts = NULL;
	virt->client.base.num_syncpts = 0;
	virt->dev = dev;

	INIT_LIST_HEAD(&virt->client.list);
	virt->client.version = 0x23;
	virt->client.ops = &virt_engine_ops;

	err = host1x_client_register(&virt->client.base);
	if (err < 0) {
		dev_err(dev, "failed to register host1x client: %d\n", err);
		return err;
	}

	err = virt_engine_setup_ivc(virt);
	if (err < 0)
		goto unregister_client;

	/* Connect to HOST module. This doesn't really do anything but we need to do it first. */
	virt_engine_connect(virt, 1);

	virt->connection_id = virt_engine_connect(virt, module_id);
	if (virt->connection_id < 0) {
		dev_err(dev, "failed to register with server\n");
		err = -EIO;
		goto cleanup_ivc;
	}

#ifdef CONFIG_DEBUG_FS
	virt_engine_setup_actmon_debugfs(virt);
#endif

#ifdef CONFIG_TEGRA_HOST1X_POWERMODE_CONTROL
	virt_engine_sysfs_init(virt);
#endif

#ifndef CONFIG_TEGRA_SYSTEM_TYPE_ACK
	hwpm_ip_index = virt_engine_get_ip_index(pdev->name);
	if (hwpm_ip_index != TERGA_SOC_HWPM_NUM_IPS) {
		hwpm_ip_ops.ip_dev = (void *)pdev;
		hwpm_ip_ops.ip_base_address = virt_engine_extract_base_addr(pdev);
		if (hwpm_ip_ops.ip_base_address <= 0) {
			dev_err(&pdev->dev,
					"IP Base address not found. HWPM-IP registration failed");
			return 0;
		}
		hwpm_ip_ops.resource_enum = hwpm_ip_index;
		hwpm_ip_ops.hwpm_ip_pm = &virt_engine_ip_pm;
		hwpm_ip_ops.hwpm_ip_reg_op = &virt_engine_ip_reg_op;
		tegra_soc_hwpm_ip_register(&hwpm_ip_ops);
	}
#endif
	return 0;

cleanup_ivc:
	virt_engine_cleanup();
unregister_client:
	host1x_client_unregister(&virt->client.base);

	return err;
}

static int virt_engine_remove(struct platform_device *pdev)
{
	struct virt_engine *virt_engine = platform_get_drvdata(pdev);
#ifndef CONFIG_TEGRA_SYSTEM_TYPE_ACK
	struct tegra_soc_hwpm_ip_ops hwpm_ip_ops;
	u32 hwpm_ip_index = virt_engine_get_ip_index(pdev->name);

	if (hwpm_ip_index != TERGA_SOC_HWPM_NUM_IPS) {
		hwpm_ip_ops.ip_dev = (void *)pdev;
		hwpm_ip_ops.ip_base_address = virt_engine_extract_base_addr(pdev);
		if (hwpm_ip_ops.ip_base_address <= 0) {
			dev_err(&pdev->dev,
					"IP Base address not found. HWPM-IP Un-register failed");
			return 0;
		}
		hwpm_ip_ops.resource_enum = hwpm_ip_index;
		tegra_soc_hwpm_ip_unregister(&hwpm_ip_ops);
	}
#endif
#ifdef CONFIG_DEBUG_FS
	virt_engine_cleanup_actmon_debugfs(virt_engine);
#endif

#ifdef CONFIG_TEGRA_HOST1X_POWERMODE_CONTROL
	virt_engine_sysfs_exit(virt_engine);
#endif
	virt_engine_cleanup();

	host1x_client_unregister(&virt_engine->client.base);

	return 0;
}

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void virt_engine_remove_wrapper(struct platform_device *pdev)
{
	virt_engine_remove(pdev);
}
#else
static int virt_engine_remove_wrapper(struct platform_device *pdev)
{
	return virt_engine_remove(pdev);
}
#endif

struct platform_driver tegra_virt_engine_driver = {
	.driver = {
		.name = "tegra-host1x-virtual-engine",
		.of_match_table = tegra_virt_engine_of_match,
	},
	.probe = virt_engine_probe,
	.remove = virt_engine_remove_wrapper,
};
