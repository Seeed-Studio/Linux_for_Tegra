/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include <nvidia/conftest.h>

#include <linux/fs.h>
#include <linux/file.h>
#include <linux/kref.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/xarray.h>
#include <linux/dev_printk.h>
#include <linux/uaccess.h>
#include <linux/sync_file.h>
#include <linux/dma-fence.h>

#include "dev.h"
#include "fence.h"
#include "poll.h"
#include "syncpt.h"
#include "ioctl.h"


#define IFACE_NAME              "host1x-emu"
#define TRACE_MAX_LENGTH        128U

/* convenience,shorter err/fn/dbg_info */
#if defined(HOST1X_EMU_SYNCPT_DEBUG)
extern u32 host1x_emu_dbg_mask;
extern u32 host1x_emu_dbg_ftrace;
#define host1x_emu_dbg(dbg_mask, format, arg...)            \
do {                                                        \
    if (unlikely((dbg_mask) & nvhost_dbg_mask)) {           \
            pr_info("host1x-emu %s: " format "\n",          \
                    __func__, ##arg);                       \
    }                                                       \
} while (0)
#else /* NVHOST_DEBUG */
#define host1x_emu_dbg(dbg_mask, format, arg...)            \
do {                                                        \
    if (0)                                                  \
        printk(KERN_INFO "host1x-emu %s: " format "\n", __func__, ##arg);\
} while (0)
#endif

#define host1x_emu_err(d, fmt, arg...) \
    dev_err(d, "%s: " fmt "\n", __func__, ##arg)

#define host1x_emu_err_ratelimited(d, fmt, arg...) \
    dev_err_ratelimited(d, "%s: " fmt "\n", __func__, ##arg)

#define host1x_emu_warn(d, fmt, arg...) \
    dev_warn(d, "%s: " fmt "\n", __func__, ##arg)

#define host1x_emu_dbg_fn(fmt, arg...) \
    host1x_emu__dbg(dbg_fn, fmt, ##arg)

#define host1x_emu_dbg_info(fmt, arg...) \
    host1x_emu__dbg(dbg_info, fmt, ##arg)

struct host1x_emu_ctrl_userctx {
    struct host1x *host;
    struct mutex   lock;
    struct xarray  syncpoints;
};

/**
 * timeout_abs_to_jiffies - calculate jiffies timeout from absolute time in
 * sec/nsec
 *
 * @timeout_nsec: timeout nsec component in ns, 0 for poll
 */
static signed long timeout_abs_to_jiffies(int64_t timeout_nsec)
{
    ktime_t now;
    ktime_t abs_timeout;
    u64 timeout_ns;
    u64 timeout_jiffies64;

    /* make 0 timeout means poll - absolute 0 doesn't seem valid */
    if (timeout_nsec == 0)
        return 0;

    abs_timeout = ns_to_ktime(timeout_nsec);
    now = ktime_get();

    if (!ktime_after(abs_timeout, now))
        return 0;

    timeout_ns = ktime_to_ns(ktime_sub(abs_timeout, now));

    timeout_jiffies64 = nsecs_to_jiffies64(timeout_ns);
    /*  clamp timeout to avoid infinite timeout */
    if (timeout_jiffies64 >= MAX_SCHEDULE_TIMEOUT - 1)
        return MAX_SCHEDULE_TIMEOUT - 1;

    return timeout_jiffies64 + 1;
}

static int host1x_ctrlopen(struct inode *inode, struct file *filp)
{
    struct host1x *host = container_of(inode->i_cdev, struct host1x, cdev);
    struct host1x_emu_ctrl_userctx *priv = NULL;

    priv = kzalloc(sizeof(*priv), GFP_KERNEL);
    if (priv == NULL) {
        host1x_emu_err(host->dev, "failed to allocate host1x user context");
        kfree(priv);
        return -ENOMEM;
    }
    xa_init(&priv->syncpoints);
    mutex_init(&priv->lock);

    priv->host = host;
    filp->private_data = priv;
    return 0;
}

static int host1x_ctrlrelease(struct inode *inode, struct file *filp)
{
    unsigned long id;
    struct host1x_emu_ctrl_userctx *priv = filp->private_data;
    struct host1x_syncpt *sp;

    filp->private_data = NULL;

    xa_for_each(&priv->syncpoints, id, sp)
        HOST1X_EMU_EXPORT_CALL(host1x_syncpt_put(sp));

    xa_destroy(&priv->syncpoints);
    kfree(priv);
    return 0;
}

static int host1x_ioctl_syncpoint_allocate(
                    struct host1x_emu_ctrl_userctx *ctx,
                    struct host1x_emu_ctrl_alloc_syncpt_args *args)
{
    int err;
    struct host1x *host = ctx->host;
    struct host1x_syncpt *sp;

    if (args->id)
        return -EINVAL;

    sp = HOST1X_EMU_EXPORT_CALL(host1x_syncpt_alloc(host,
                        HOST1X_SYNCPT_CLIENT_MANAGED, current->comm));
    if (!sp)
        return -EBUSY;

    args->id = HOST1X_EMU_EXPORT_CALL(host1x_syncpt_id(sp));

    err = xa_insert(&ctx->syncpoints, args->id, sp, GFP_KERNEL);
    if (err) {
        HOST1X_EMU_EXPORT_CALL(host1x_syncpt_put(sp));
        return err;
    }

    return 0;
}

static int host1x_ioctl_syncpoint_free(
                                struct host1x_emu_ctrl_userctx *ctx,
                                struct host1x_emu_ctrl_free_syncpt_args *args)
{
    struct host1x_syncpt *sp;

    mutex_lock(&ctx->lock);
    sp = xa_erase(&ctx->syncpoints, args->id);
    mutex_unlock(&ctx->lock);
    if (!sp)
        return -EINVAL;

    HOST1X_EMU_EXPORT_CALL(host1x_syncpt_put(sp));
    return 0;
}

static int host1x_ioctl_ctrl_syncpt_read(
                    struct host1x_emu_ctrl_userctx *ctx,
                    struct host1x_emu_ctrl_syncpt_read_args *args)
{
    struct host1x *host = ctx->host;
    struct host1x_syncpt *sp;

    sp = HOST1X_EMU_EXPORT_CALL(host1x_syncpt_get_by_id_noref(host, args->id));
    if (!sp)
        return -EINVAL;

    args->value = HOST1X_EMU_EXPORT_CALL(host1x_syncpt_read(sp));
    return 0;
}

static int host1x_ioctl_ctrl_syncpt_incr(
                    struct host1x_emu_ctrl_userctx *ctx,
                    struct host1x_emu_ctrl_syncpt_incr_args *args)
{
    int err;
    uint32_t idx;
    struct host1x_syncpt *sp;

    sp = xa_load(&ctx->syncpoints, args->id);
    if (!sp)
        return -EINVAL;

    for(idx = 0; idx < args->val; idx++) {
        err = HOST1X_EMU_EXPORT_CALL(host1x_syncpt_incr(sp));
        if (err < 0) {
            return err;
        }
    }
    return err;
}

static int host1x_ioctl_syncpoint_wait(
                    struct host1x_emu_ctrl_userctx *ctx,
                    struct host1x_emu_ctrl_syncpt_wait_args *args)

{
	signed long timeout_jiffies;
	struct host1x_syncpt *sp;
	struct host1x *host = ctx->host;
	ktime_t ts;
	int err;

	sp = HOST1X_EMU_EXPORT_CALL(host1x_syncpt_get_by_id_noref(host, args->id));
	if (!sp)
		return -EINVAL;

	timeout_jiffies = timeout_abs_to_jiffies(args->timeout_ns);
	err = HOST1X_EMU_EXPORT_CALL(host1x_syncpt_wait_ts(sp,
				args->threshold,
				timeout_jiffies,
				&args->value,
				&ts));
	if (err)
		return err;

	args->timestamp = ktime_to_ns(ts);
	return 0;
}

static long host1x_ctrlctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct host1x_emu_ctrl_userctx *priv = filp->private_data;
    u8 buf[HOST1X_EMU_SYNCPT_IOCTL_CTRL_MAX_ARG_SIZE] __aligned(sizeof(u64));
    int err = 0;

    if ((_IOC_TYPE(cmd) != HOST1X_EMU_SYNCPT_IOCTL_MAGIC) ||
        (_IOC_NR(cmd) == 0) ||
        (_IOC_NR(cmd) > HOST1X_EMU_SYNCPT_IOCTL_CTRL_LAST) ||
        (_IOC_SIZE(cmd) > HOST1X_EMU_SYNCPT_IOCTL_CTRL_MAX_ARG_SIZE)) {
        host1x_emu_err_ratelimited(NULL, "invalid cmd 0x%x", cmd);
        return -ENOIOCTLCMD;
    }

    if (_IOC_DIR(cmd) & _IOC_WRITE) {
        if (copy_from_user(buf, (void __user *)arg, _IOC_SIZE(cmd))) {
            host1x_emu_err(NULL, "failed to copy from user arg=%px",
                   (void __user *)arg);
            return -EFAULT;
        }
    }

    switch (cmd) {
    case HOST1X_EMU_SYNCPT_IOCTL_CTRL_ALLOC_SYNCPT:
        err = host1x_ioctl_syncpoint_allocate(priv, (void *)buf);
        break;
    case HOST1X_EMU_SYNCPT_IOCTL_CTRL_FREE_SYNCPT:
        err = host1x_ioctl_syncpoint_free(priv, (void *)buf);
        break;
    case HOST1X_EMU_SYNCPT_IOCTL_CTRL_SYNCPT_READ:
        err = host1x_ioctl_ctrl_syncpt_read(priv, (void *)buf);
        break;
    case HOST1X_EMU_SYNCPT_IOCTL_CTRL_SYNCPT_INCR:
        err = host1x_ioctl_ctrl_syncpt_incr(priv, (void *)buf);
        break;
    case HOST1X_EMU_SYNCPT_IOCTL_CTRL_SYNCPT_WAIT:
        err = host1x_ioctl_syncpoint_wait(priv, (void *)buf);
        break;
    default:
        host1x_emu_err(priv->host->dev, "invalid cmd 0x%x", cmd);
        err = -ENOIOCTLCMD;
        break;
    }

    if ((err == 0) && (_IOC_DIR(cmd) & _IOC_READ)) {
        err = copy_to_user((void __user *)arg, buf, _IOC_SIZE(cmd));
        if (err) {
            host1x_emu_err(priv->host->dev,
                   "failed to copy to user");
            err = -EFAULT;
        }
    }

    return err;
}

static const struct file_operations host1x_ctrlops = {
    .owner          = THIS_MODULE,
    .release        = host1x_ctrlrelease,
    .open           = host1x_ctrlopen,
    .unlocked_ioctl = host1x_ctrlctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl   = host1x_ctrlctl,
#endif
};

#if defined(NV_CLASS_STRUCT_DEVNODE_HAS_CONST_DEV_ARG) /* Linux v6.2 */
static char *host1x_emu_devnode(const struct device *dev, umode_t *mode)
#else
static char *host1x_emu_devnode(struct device *dev, umode_t *mode)
#endif
{
	*mode = 0666;
	return NULL;
}

int host1x_user_init(struct host1x *host)
{
    int err;
    dev_t devno;

#if defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG) /* Linux v6.4 */
    host->host1x_class = class_create(dev_name(host->dev));
#else
    host->host1x_class = class_create(THIS_MODULE, dev_name(host->dev));
#endif
    if (IS_ERR(host->host1x_class)) {
        err = PTR_ERR(host->host1x_class);
        dev_err(host->dev, "failed to create class\n");
        goto fail;
    }

    err = alloc_chrdev_region(&devno, 0, 1, IFACE_NAME);
    if (err < 0) {
        dev_err(host->dev, "failed to reserve chrdev region\n");
        goto fail;
    }
	host->host1x_class->devnode = host1x_emu_devnode;

    host->major = MAJOR(devno);
    host->next_minor += 1;

    cdev_init(&host->cdev, &host1x_ctrlops);
    host->cdev.owner = THIS_MODULE;
    err = cdev_add(&host->cdev, devno, 1);
    if (err < 0) {
        host1x_emu_err(host->dev, "failed to add cdev");
        goto fail;
    }
    host->ctrl = device_create(host->host1x_class, host->dev, devno,
                   NULL, IFACE_NAME "-%s", "ctrl");
    if (IS_ERR(host->ctrl)) {
        err = PTR_ERR(host->ctrl);
        dev_err(host->dev, "failed to create ctrl device\n");
        goto fail;
    }

    return 0;
fail:
    return err;
}
