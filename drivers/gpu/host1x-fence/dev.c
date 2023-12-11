// SPDX-License-Identifier: GPL-2.0-only
/*
 * Host1x fence UAPI
 *
 * Copyright (c) 2022-2024, NVIDIA CORPORATION.  All rights reserved.
 */

#include <nvidia/conftest.h>

#include <linux/anon_inodes.h>
#include <linux/cdev.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/host1x-next.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/sync_file.h>

#include "include/uapi/linux/host1x-fence.h"

static struct host1x_uapi {
	struct class *class;

	struct cdev cdev;
	struct device *dev;
	dev_t dev_num;
} uapi_data;

static int dev_file_open(struct inode *inode, struct file *file)
{
	struct platform_device *host1x_pdev;
	struct device_node *np;

	static const struct of_device_id host1x_match[] = {
		{ .compatible = "nvidia,tegra186-host1x", },
		{ .compatible = "nvidia,tegra194-host1x", },
		{ .compatible = "nvidia,tegra234-host1x", },
		{},
	};

	np = of_find_matching_node(NULL, host1x_match);
	if (!np) {
		return -ENODEV;
	}

	host1x_pdev = of_find_device_by_node(np);
	if (!host1x_pdev) {
		return -EAGAIN;
	}

	file->private_data = platform_get_drvdata(host1x_pdev);

	return 0;
}

static int host1x_fence_create_fd(struct host1x_syncpt *sp, u32 threshold)
{
	struct sync_file *file;
	struct dma_fence *f;
	int fd;

	f = host1x_fence_create(sp, threshold, true);
	if (IS_ERR(f))
		return PTR_ERR(f);

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		dma_fence_put(f);
		return fd;
	}

	file = sync_file_create(f);
	dma_fence_put(f);
	if (!file)
		return -ENOMEM;

	fd_install(fd, file->file);

	return fd;
}

static int dev_file_ioctl_create_fence(struct host1x *host1x, void __user *data)
{
	struct host1x_create_fence args;
	struct host1x_syncpt *syncpt;
	unsigned long copy_err;
	int fd;

	copy_err = copy_from_user(&args, data, sizeof(args));
	if (copy_err)
		return -EFAULT;

	if (args.reserved[0])
		return -EINVAL;

	syncpt = host1x_syncpt_get_by_id_noref(host1x, args.id);
	if (!syncpt)
		return -EINVAL;

	fd = host1x_fence_create_fd(syncpt, args.threshold);
	if (fd < 0)
		return fd;

	args.fence_fd = fd;

	copy_err = copy_to_user(data, &args, sizeof(args));
	if (copy_err)
		return -EFAULT;

	return 0;
}

static int dev_file_ioctl_fence_extract(struct host1x *host1x, void __user *data)
{
	struct host1x_fence_extract_fence __user *fences_user_ptr;
	struct dma_fence *fence, **fences;
	struct host1x_fence_extract args;
	struct dma_fence_array *array;
	unsigned int num_fences, i, j;
	unsigned long copy_err;
	int err;

	copy_err = copy_from_user(&args, data, sizeof(args));
	if (copy_err)
		return -EFAULT;

	fences_user_ptr = u64_to_user_ptr(args.fences_ptr);

	if (args.reserved[0] || args.reserved[1])
		return -EINVAL;

	fence = sync_file_get_fence(args.fence_fd);
	if (!fence)
		return -EINVAL;

	array = to_dma_fence_array(fence);
	if (array) {
		fences = array->fences;
		num_fences = array->num_fences;
	} else {
		fences = &fence;
		num_fences = 1;
	}

	for (i = 0, j = 0; i < num_fences; i++) {
		struct host1x_fence_extract_fence f;

		err = host1x_fence_extract(fences[i], &f.id, &f.threshold);
		if (err == -EINVAL && dma_fence_is_signaled(fences[i])) {
			/* Likely stub fence */
			continue;
		} else if (err) {
			goto put_fence;
		}

		if (j < args.num_fences) {
			copy_err = copy_to_user(fences_user_ptr + j, &f, sizeof(f));
			if (copy_err) {
				err = -EFAULT;
				goto put_fence;
			}
		}

		j++;
	}

	args.num_fences = j;

	copy_err = copy_to_user(data, &args, sizeof(args));
	if (copy_err) {
		err = -EFAULT;
		goto put_fence;
	}

	return 0;

put_fence:
	dma_fence_put(fence);

	return err;
}

struct host1x_pollfd_fence {
	struct list_head list;

	wait_queue_head_t *wq;

	struct dma_fence *fence;
	struct dma_fence_cb callback;
	bool callback_set;
};

struct host1x_pollfd {
	struct kref ref;
	struct mutex lock;
	wait_queue_head_t wq;

	struct list_head fences;
};

static int host1x_pollfd_release(struct inode *inode, struct file *file)
{
	struct host1x_pollfd *pollfd = file->private_data;
	struct host1x_pollfd_fence *pfd_fence;

	mutex_lock(&pollfd->lock);

	list_for_each_entry(pfd_fence, &pollfd->fences, list) {
		if (pfd_fence->callback_set) {
			if (dma_fence_remove_callback(pfd_fence->fence, &pfd_fence->callback))
				host1x_fence_cancel(pfd_fence->fence);
		}
		dma_fence_put(pfd_fence->fence);
	}

	mutex_unlock(&pollfd->lock);

	return 0;
}

static unsigned int host1x_pollfd_poll(struct file *file, poll_table *wait)
{
	struct host1x_pollfd *pollfd = file->private_data;
	struct host1x_pollfd_fence *pfd_fence, *pfd_fence_temp;
	unsigned int mask = 0;

	poll_wait(file, &pollfd->wq, wait);

	mutex_lock(&pollfd->lock);

	list_for_each_entry_safe(pfd_fence, pfd_fence_temp, &pollfd->fences, list) {
		if (dma_fence_is_signaled(pfd_fence->fence)) {
			mask = POLLPRI | POLLIN;
			dma_fence_put(pfd_fence->fence);
			list_del(&pfd_fence->list);
		}
	}

	mutex_unlock(&pollfd->lock);

	return mask;
}

static const struct file_operations host1x_pollfd_ops = {
	.release = host1x_pollfd_release,
	.poll = host1x_pollfd_poll,
};

static int dev_file_ioctl_create_pollfd(struct host1x *host1x, void __user *data)
{
	struct host1x_create_pollfd args;
	struct host1x_pollfd *pollfd;
	unsigned long copy_err;
	struct file *file;
	int fd, err;

	copy_err = copy_from_user(&args, data, sizeof(args));
	if (copy_err)
		return -EFAULT;

	pollfd = kzalloc(sizeof(*pollfd), GFP_KERNEL);
	if (!pollfd)
		return -ENOMEM;

	file = anon_inode_getfile("host1x_pollfd", &host1x_pollfd_ops, pollfd, 0);
	if (IS_ERR(file)) {
		err = PTR_ERR(file);
		goto free_pollfd;
	}

	init_waitqueue_head(&pollfd->wq);
	mutex_init(&pollfd->lock);
	kref_init(&pollfd->ref);
	INIT_LIST_HEAD(&pollfd->fences);

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		err = fd;
		goto put_file;
	}

	fd_install(fd, file);

	args.fd = fd;

	copy_err = copy_to_user(data, &args, sizeof(args));
	if (copy_err)
		return -EFAULT;

	return 0;

put_file:
	fput(file);
free_pollfd:
	kfree(pollfd);

	return err;
}

static void host1x_pollfd_callback(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	struct host1x_pollfd_fence *pfd_fence = container_of(cb, struct host1x_pollfd_fence, callback);

	wake_up_all(pfd_fence->wq);
}

static int dev_file_ioctl_trigger_pollfd(struct host1x *host1x, void __user *data)
{
	struct host1x_pollfd_fence *pfd_fence;
	struct host1x_trigger_pollfd args;
	struct host1x_syncpt *syncpt;
	struct host1x_pollfd *pollfd;
	struct dma_fence *fence;
	unsigned long copy_err;
	struct file *file;
	int err;

	copy_err = copy_from_user(&args, data, sizeof(args));
	if (copy_err)
		return -EFAULT;

	file = fget(args.fd);
	if (!file)
		return -EINVAL;

	if (file->f_op != &host1x_pollfd_ops) {
		err = -EINVAL;
		goto put_file;
	}

	pollfd = file->private_data;

	syncpt = host1x_syncpt_get_by_id_noref(host1x, args.id);
	if (!syncpt) {
		err = -EINVAL;
		goto put_file;
	}

	pfd_fence = kzalloc(sizeof(*pfd_fence), GFP_KERNEL);
	if (!pfd_fence) {
		err = -ENOMEM;
		goto put_file;
	}

	fence = host1x_fence_create(syncpt, args.threshold, false);
	if (IS_ERR(fence)) {
		err = PTR_ERR(fence);
		goto free_pfd_fence;
	}

	pfd_fence->fence = fence;
	pfd_fence->wq = &pollfd->wq;

	mutex_lock(&pollfd->lock);
	list_add(&pfd_fence->list, &pollfd->fences);

	pfd_fence->callback_set = false;
	err = dma_fence_add_callback(fence, &pfd_fence->callback, host1x_pollfd_callback);
	if (err == -ENOENT) {
		/*
		 * We don't free the fence here -- it will be done from the poll
		 * handler. This way the logic is same whether the through callback
		 * or this shortcut.
		 */
		wake_up_all(&pollfd->wq);
	} else if (err != 0) {
		mutex_unlock(&pollfd->lock);
		goto remove_fence;
	}
	pfd_fence->callback_set = true;

	mutex_unlock(&pollfd->lock);

	return 0;

remove_fence:
	list_del(&pfd_fence->list);
	dma_fence_put(fence);
free_pfd_fence:
	kfree(pfd_fence);
put_file:
	fput(file);

	return err;
}

static long dev_file_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	void __user *data = (void __user *)arg;
	long err;

	switch (cmd) {
	case HOST1X_IOCTL_CREATE_FENCE:
		err = dev_file_ioctl_create_fence(file->private_data, data);
		break;

	case HOST1X_IOCTL_CREATE_POLLFD:
		err = dev_file_ioctl_create_pollfd(file->private_data, data);
		break;

	case HOST1X_IOCTL_TRIGGER_POLLFD:
		err = dev_file_ioctl_trigger_pollfd(file->private_data, data);
		break;

	case HOST1X_IOCTL_FENCE_EXTRACT:
		err = dev_file_ioctl_fence_extract(file->private_data, data);
		break;

	default:
		err = -ENOTTY;
	}

	return err;
}

static const struct file_operations dev_file_fops = {
	.owner = THIS_MODULE,
	.open = dev_file_open,
	.unlocked_ioctl = dev_file_ioctl,
	.compat_ioctl = dev_file_ioctl,
};

#if defined(NV_CLASS_STRUCT_DEVNODE_HAS_CONST_DEV_ARG) /* Linux v6.2 */
static char *host1x_fence_devnode(const struct device *dev, umode_t *mode)
#else
static char *host1x_fence_devnode(struct device *dev, umode_t *mode)
#endif
{
	*mode = 0666;
	return NULL;
}

static int host1x_uapi_init(struct host1x_uapi *uapi)
{
	int err;
	dev_t dev_num;

	err = alloc_chrdev_region(&dev_num, 0, 1, "host1x-fence");
	if (err)
		return err;

#if defined(NV_CLASS_CREATE_HAS_NO_OWNER_ARG) /* Linux v6.4 */
	uapi->class = class_create("host1x-fence");
#else
	uapi->class = class_create(THIS_MODULE, "host1x-fence");
#endif
	if (IS_ERR(uapi->class)) {
		err = PTR_ERR(uapi->class);
		goto unregister_chrdev_region;
	}
	uapi->class->devnode = host1x_fence_devnode;

	cdev_init(&uapi->cdev, &dev_file_fops);
	err = cdev_add(&uapi->cdev, dev_num, 1);
	if (err)
		goto destroy_class;

	uapi->dev = device_create(uapi->class, NULL,
				  dev_num, NULL, "host1x-fence");
	if (IS_ERR(uapi->dev)) {
		err = PTR_ERR(uapi->dev);
		goto del_cdev;
	}

	cdev_add(&uapi->cdev, dev_num, 1);

	uapi->dev_num = dev_num;

	return 0;

del_cdev:
	cdev_del(&uapi->cdev);
destroy_class:
	class_destroy(uapi->class);
unregister_chrdev_region:
	unregister_chrdev_region(dev_num, 1);

	return err;
}

static const struct of_device_id host1x_fence_of_match[] = {
	{ .compatible = "nvidia,tegra234-host1x" },
	{ },
};
MODULE_DEVICE_TABLE(of, host1x_fence_of_match);

static int __init tegra_host1x_init(void)
{
	return host1x_uapi_init(&uapi_data);
}
module_init(tegra_host1x_init);

static void __exit tegra_host1x_exit(void)
{
}
module_exit(tegra_host1x_exit);

MODULE_DESCRIPTION("Host1x fence UAPI");
MODULE_LICENSE("GPL");
