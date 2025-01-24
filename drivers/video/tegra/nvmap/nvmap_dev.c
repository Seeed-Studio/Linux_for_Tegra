// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2011-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * User-space interface to nvmap
 */

#include <nvidia/conftest.h>

#include <linux/backing-dev.h>
#include <linux/bitmap.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/oom.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/nvmap.h>
#include <linux/module.h>
#include <linux/resource.h>
#include <linux/security.h>
#include <linux/stat.h>
#include <linux/kthread.h>
#include <linux/highmem.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/of.h>
#include <linux/iommu.h>
#include <linux/version.h>
#include <soc/tegra/fuse.h>
#include <linux/sched/clock.h>
#include <linux/sched/mm.h>
#include <linux/backing-dev.h>
#include <asm/cputype.h>

#define CREATE_TRACE_POINTS
#include <trace/events/nvmap.h>
#include <linux/of_reserved_mem.h>
#include "nvmap_dev.h"
#include "nvmap_alloc.h"
#include "nvmap_dmabuf.h"
#include "nvmap_handle.h"
#include "nvmap_dev_int.h"
#include "nvmap_debug.h"

#include <linux/pagewalk.h>

#define NVMAP_CARVEOUT_KILLER_RETRY_TIME 100 /* msecs */

struct nvmap_device *nvmap_dev;
ulong nvmap_init_time;

extern bool nvmap_convert_iovmm_to_carveout;
extern bool nvmap_convert_carveout_to_iovmm;

static struct device_dma_parameters nvmap_dma_parameters = {
	.max_segment_size = UINT_MAX,
};

static int nvmap_open(struct inode *inode, struct file *filp);
static int nvmap_release(struct inode *inode, struct file *filp);
static long nvmap_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
static int nvmap_map(struct file *filp, struct vm_area_struct *vma);
#if !defined(CONFIG_MMU)
static unsigned nvmap_mmap_capabilities(struct file *filp);
#endif

static const struct file_operations nvmap_user_fops = {
	.owner		= THIS_MODULE,
	.open		= nvmap_open,
	.release	= nvmap_release,
	.unlocked_ioctl	= nvmap_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = nvmap_ioctl,
#endif
	.mmap		= nvmap_map,
#if !defined(CONFIG_MMU)
	.mmap_capabilities = nvmap_mmap_capabilities,
#endif
};

static void nvmap_pid_release_locked(struct kref *kref)
{
	struct nvmap_pid_data *p = container_of(kref, struct nvmap_pid_data,
			refcount);

	nvmap_debug_remove_debugfs_handles_by_pid(p->handles_file);

	rb_erase(&p->node, &nvmap_dev->pids);
	kfree(p);
}

static void nvmap_pid_get_locked(struct nvmap_device *dev, pid_t pid)
{
	struct rb_root *root = &dev->pids;
	struct rb_node **new = &(root->rb_node), *parent = NULL;
	struct nvmap_pid_data *p;
	char name[16];

	while (*new) {
		p = container_of(*new, struct nvmap_pid_data, node);
		parent = *new;

		if (p->pid > pid) {
			new = &((*new)->rb_left);
		} else if (p->pid < pid) {
			new = &((*new)->rb_right);
		} else {
			kref_get(&p->refcount);
			return;
		}
	}

	if (snprintf(name, sizeof(name), "%d", pid) < 0)
		return;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (p == NULL)
		return;

	p->pid = pid;
	kref_init(&p->refcount);

	p->handles_file = nvmap_debug_create_debugfs_handles_by_pid(name,
			dev->handles_by_pid, p);

	if (IS_ERR_OR_NULL(p->handles_file)) {
		kfree(p);
	} else {
		rb_link_node(&p->node, parent, new);
		rb_insert_color(&p->node, root);
	}
}

static struct nvmap_pid_data *nvmap_pid_find_locked(struct nvmap_device *dev,
		pid_t pid)
{
	struct rb_node *node = dev->pids.rb_node;

	while (node) {
		struct nvmap_pid_data *p = container_of(node,
				struct nvmap_pid_data, node);

		if (p->pid > pid)
			node = node->rb_left;
		else if (p->pid < pid)
			node = node->rb_right;
		else
			return p;
	}
	return NULL;
}

static void nvmap_pid_put_locked(struct nvmap_device *dev, pid_t pid)
{
	struct nvmap_pid_data *p = nvmap_pid_find_locked(dev, pid);
	if (p)
		kref_put(&p->refcount, nvmap_pid_release_locked);
}

static struct nvmap_client *__nvmap_create_client(struct nvmap_device *dev,
					   const char *name)
{
	struct nvmap_client *client;
	struct task_struct *task;
	pid_t pid;
	bool is_existing_client = false;

	if (WARN_ON(!dev))
		return NULL;

	get_task_struct(current->group_leader);
	task_lock(current->group_leader);
	/* don't bother to store task struct for kernel threads,
	   they can't be killed anyway */
	if (current->flags & PF_KTHREAD) {
		put_task_struct(current->group_leader);
		task = NULL;
	} else {
		task = current->group_leader;
	}
	task_unlock(current->group_leader);

	pid = task ? task->pid : 0;

	mutex_lock(&dev->clients_lock);
	list_for_each_entry(client, &nvmap_dev->clients, list) {
		if (nvmap_client_pid(client) == pid) {
			/* Increment counter to track number of namespaces of a process */
			atomic_add(1, &client->count);
			put_task_struct(current->group_leader);
			is_existing_client = true;
			goto unlock;
		}
	}
unlock:
	if (is_existing_client) {
		mutex_unlock(&dev->clients_lock);
		return client;
	}

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (client == NULL) {
		mutex_unlock(&dev->clients_lock);
		return NULL;
	}
	client->name = name;
	client->handle_refs = RB_ROOT;
	client->task = task;

	mutex_init(&client->ref_lock);
	atomic_set(&client->count, 1);
	client->kernel_client = false;
	nvmap_id_array_init(&client->id_array);

#ifdef NVMAP_CONFIG_HANDLE_AS_ID
	client->ida = &client->id_array;
#else
	client->ida = NULL;
#endif

	list_add(&client->list, &dev->clients);
	if (!IS_ERR_OR_NULL(dev->handles_by_pid)) {
		pid_t pid = nvmap_client_pid(client);
		nvmap_pid_get_locked(dev, pid);
	}
	mutex_unlock(&dev->clients_lock);
	return client;
}

static void destroy_client(struct nvmap_client *client)
{
	struct rb_node *n;

	if (!client)
		return;

	mutex_lock(&nvmap_dev->clients_lock);
	/*
	 * count field tracks the number of namespaces within a process.
	 * Destroy the client only after all namespaces close the /dev/nvmap node.
	 */
	if (atomic_dec_return(&client->count)) {
		mutex_unlock(&nvmap_dev->clients_lock);
		return;
	}

	nvmap_id_array_exit(&client->id_array);
#ifdef NVMAP_CONFIG_HANDLE_AS_ID
	client->ida = NULL;
#endif
	if (!IS_ERR_OR_NULL(nvmap_dev->handles_by_pid)) {
		pid_t pid = nvmap_client_pid(client);
		nvmap_pid_put_locked(nvmap_dev, pid);
	}
	list_del(&client->list);
	mutex_unlock(&nvmap_dev->clients_lock);

	while ((n = rb_first(&client->handle_refs))) {
		struct nvmap_handle_ref *ref;
		int dupes;

		ref = rb_entry(n, struct nvmap_handle_ref, node);
		smp_rmb();
		if (ref->handle->owner == client)
			ref->handle->owner = NULL;

		if (ref->is_ro)
			dma_buf_put(ref->handle->dmabuf_ro);
		else
			dma_buf_put(ref->handle->dmabuf);
		rb_erase(&ref->node, &client->handle_refs);
		atomic_dec(&ref->handle->share_count);
		dupes = atomic_read(&ref->dupes);
		while (dupes > 0) {
			nvmap_handle_put(ref->handle);
			dupes--;
		}
		kfree(ref);
	}

	if (client->task)
		put_task_struct(client->task);

	kfree(client);
}

static int nvmap_open(struct inode *inode, struct file *filp)
{
	struct miscdevice *miscdev = filp->private_data;
	struct nvmap_device *dev = dev_get_drvdata(miscdev->parent);
	struct nvmap_client *priv;
	int ret;
	__attribute__((unused)) struct rlimit old_rlim, new_rlim;

	ret = nonseekable_open(inode, filp);
	if (unlikely(ret))
		return ret;

	BUG_ON(dev != nvmap_dev);
	priv = __nvmap_create_client(dev, "user");
	if (priv == NULL)
		return -ENOMEM;
	trace_nvmap_open(priv, priv->name);

	filp->private_data = priv;
	return 0;
}

static int nvmap_release(struct inode *inode, struct file *filp)
{
	struct nvmap_client *priv = filp->private_data;

	if (priv == NULL)
		return 0;

	trace_nvmap_release(priv, priv->name);
	destroy_client(priv);
	return 0;
}

static int nvmap_map(struct file *filp, struct vm_area_struct *vma)
{
	char task_comm[TASK_COMM_LEN];

	get_task_comm(task_comm, current);
	pr_debug("error: mmap not supported on nvmap file, pid=%d, %s\n",
		  task_tgid_nr(current), task_comm);
	return -EPERM;
}

static long nvmap_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int err = 0;
	void __user *uarg = (void __user *)arg;

	if (_IOC_TYPE(cmd) != NVMAP_IOC_MAGIC)
		return -ENOTTY;

	if (_IOC_NR(cmd) > NVMAP_IOC_MAXNR)
		return -ENOTTY;

	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !ACCESS_OK(VERIFY_WRITE, uarg, _IOC_SIZE(cmd));
	if (!err && (_IOC_DIR(cmd) & _IOC_WRITE))
		err = !ACCESS_OK(VERIFY_READ, uarg, _IOC_SIZE(cmd));

	if (err)
		return -EFAULT;

	err = -ENOTTY;

	switch (cmd) {
	case NVMAP_IOC_CREATE:
	case NVMAP_IOC_CREATE_64:
	case NVMAP_IOC_FROM_FD:
		err = nvmap_ioctl_create(filp, cmd, uarg);
		break;

	case NVMAP_IOC_FROM_VA:
		err = nvmap_ioctl_create_from_va(filp, uarg);
		break;

	case NVMAP_IOC_GET_FD:
		err = nvmap_ioctl_getfd(filp, uarg);
		break;

	case NVMAP_IOC_GET_IVM_HEAPS:
		err = nvmap_ioctl_get_ivc_heap(filp, uarg);
		break;

	case NVMAP_IOC_FROM_IVC_ID:
		err = nvmap_ioctl_create_from_ivc(filp, uarg);
		break;

	case NVMAP_IOC_GET_IVC_ID:
		err = nvmap_ioctl_get_ivcid(filp, uarg);
		break;

	case NVMAP_IOC_ALLOC:
		err = nvmap_ioctl_alloc(filp, uarg);
		break;

	case NVMAP_IOC_ALLOC_IVM:
		err = nvmap_ioctl_alloc_ivm(filp, uarg);
		break;

	case NVMAP_IOC_VPR_FLOOR_SIZE:
		err = 0;
		break;

	case NVMAP_IOC_FREE:
		err = nvmap_ioctl_free(filp, arg);
		break;

	case NVMAP_IOC_DUP_HANDLE:
		err = nvmap_ioctl_dup_handle(filp, uarg);
		break;

#ifdef CONFIG_COMPAT
	case NVMAP_IOC_WRITE_32:
	case NVMAP_IOC_READ_32:
		pr_warn("NVMAP_IOC_WRITE_32/READ_32 pair are deprecated. "
			"Use the pair NVMAP_IOC_WRITE/READ.\n");
		break;
#endif

	case NVMAP_IOC_WRITE:
	case NVMAP_IOC_READ:
		err = nvmap_ioctl_rw_handle(filp, cmd == NVMAP_IOC_READ, uarg,
			sizeof(struct nvmap_rw_handle));
		break;

#ifdef CONFIG_COMPAT
	case NVMAP_IOC_CACHE_32:
		pr_warn("NVMAP_IOC_CACHE_32 is deprecated. "
			"Use NVMAP_IOC_CACHE instead.\n");
		break;
#endif

	case NVMAP_IOC_CACHE:
		err = nvmap_ioctl_cache_maint(filp, uarg,
			sizeof(struct nvmap_cache_op));
		break;

	case NVMAP_IOC_CACHE_64:
		err = nvmap_ioctl_cache_maint(filp, uarg,
			sizeof(struct nvmap_cache_op_64));
		break;

	case NVMAP_IOC_GUP_TEST:
		err = nvmap_ioctl_gup_test(filp, uarg);
		break;

	case NVMAP_IOC_FROM_ID:
	case NVMAP_IOC_GET_ID:
		pr_warn("NVMAP_IOC_GET_ID/FROM_ID pair is deprecated. "
			"Use the pair NVMAP_IOC_GET_FD/FROM_FD.\n");
		break;

	case NVMAP_IOC_GET_AVAILABLE_HEAPS:
		err = nvmap_ioctl_get_available_heaps(filp, uarg);
		break;

	case NVMAP_IOC_PARAMETERS:
		err = nvmap_ioctl_get_handle_parameters(filp, uarg);
		break;

	case NVMAP_IOC_GET_SCIIPCID:
		err = nvmap_ioctl_get_sci_ipc_id(filp, uarg);
		break;

	case NVMAP_IOC_HANDLE_FROM_SCIIPCID:
		err = nvmap_ioctl_handle_from_sci_ipc_id(filp, uarg);
		break;

	case NVMAP_IOC_QUERY_HEAP_PARAMS:
		err = nvmap_ioctl_query_heap_params(filp, uarg);
		break;

	case NVMAP_IOC_QUERY_HEAP_PARAMS_NUMA:
		err = nvmap_ioctl_query_heap_params_numa(filp, uarg);
		break;

	case NVMAP_IOC_GET_FD_FOR_RANGE_FROM_LIST:
		err = nvmap_ioctl_get_fd_from_list(filp, uarg);
		break;
	default:
		pr_warn("Unknown NVMAP_IOC = 0x%x\n", cmd);
	}
	return err;
}

bool is_nvmap_memory_available(size_t size, uint32_t heap, int numa_nid)
{
	unsigned int carveout_mask = NVMAP_HEAP_CARVEOUT_MASK;
	unsigned int iovmm_mask = NVMAP_HEAP_IOVMM;
	struct nvmap_device *dev = nvmap_dev;
	bool memory_available = false;
	int i;
	unsigned long free_mem = 0;

	if (!heap)
		return false;

	if (nvmap_convert_carveout_to_iovmm) {
		carveout_mask &= ~NVMAP_HEAP_CARVEOUT_GENERIC;
		iovmm_mask |= NVMAP_HEAP_CARVEOUT_GENERIC;
	} else if (nvmap_convert_iovmm_to_carveout) {
		if (heap & NVMAP_HEAP_IOVMM) {
			heap &= ~NVMAP_HEAP_IOVMM;
			heap |= NVMAP_HEAP_CARVEOUT_GENERIC;
		}
	}

	if (heap & iovmm_mask) {
		if (system_heap_free_mem(&free_mem)) {
			pr_debug("Call to system_heap_free_mem failed\n");
			return false;
		}

		if (size > (free_mem & PAGE_MASK)) {
			pr_debug("Requested size is more than available memory\n");
			pr_debug("Requested size : %lu B, Available memory : %lu B\n", size,
					free_mem & PAGE_MASK);
			return false;
		}
		return true;
	}

	for (i = 0; i < dev->nr_carveouts; i++) {
		struct nvmap_heap *h;
		struct nvmap_carveout_node *co_heap = dev->heaps[i];

		if (!(nvmap_get_heap_bit(co_heap) & heap))
			continue;

		h = nvmap_get_heap_ptr(co_heap);
		/*
		 * When user does not specify numa node i.e. in default NUMA_NO_NODE case,
		 * do not consider numa node id. So check for heap instances on all numa
		 * nodes. When numa node is provided by user, then check heap instance only
		 * on that numa node.
		 */
		if (numa_nid == NUMA_NO_NODE) {
			if (size > (nvmap_get_heap_free_size(h) & PAGE_MASK))
				continue;
			memory_available = true;
			goto exit;
		} else {
			if (nvmap_get_heap_nid(h) != numa_nid)
				continue;
			else if (size > (nvmap_get_heap_free_size(h) & PAGE_MASK))
				memory_available = false;
			else
				memory_available = true;

			goto exit;
		}
		break;
	}

exit:
	return memory_available;
}

/*
 * Read disable-debug-support property from tegra-carveouts DT node.
 * If the property is present then don't support any extra APIs other than
 * the APIs mentioned in ICD, by doing BUG_ON in the corresponding ioctls.
 * So it is necessary to be present in the DT of the respective builds
 * (e.g. Prod NSR Build).
 */
static void nvmap_support_debug_apis(struct platform_device *pdev, struct nvmap_device *dev)
{
	struct device_node *np = pdev->dev.of_node;

	if (of_property_read_bool(np, "disable-debug-support"))
		dev->support_debug_features = 0;
	else
		dev->support_debug_features = 1;
}

int __init nvmap_probe(struct platform_device *pdev)
{
	struct nvmap_platform_data *plat;
	struct nvmap_device *dev;
	int i;
	int e;
	int generic_carveout_present = 0;
	ulong start_time = sched_clock();
	ulong result;

	if (WARN_ON(nvmap_dev != NULL)) {
		dev_err(&pdev->dev, "only one nvmap device may be present\n");
		e = -ENODEV;
		goto finish;
	}

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		dev_err(&pdev->dev, "out of memory for device\n");
		e = -ENOMEM;
		goto finish;
	}

	nvmap_init(pdev);

	plat = pdev->dev.platform_data;

	nvmap_dev = dev;
	nvmap_dev->plat = plat;

	/*
	 * dma_parms need to be set with desired max_segment_size to avoid
	 * DMA map API returning multiple IOVA's for the buffer size > 64KB.
	 */
	pdev->dev.dma_parms = &nvmap_dma_parameters;
	dev->dev_user.minor = MISC_DYNAMIC_MINOR;
	dev->dev_user.name = "nvmap";
	dev->dev_user.fops = &nvmap_user_fops;
	dev->dev_user.parent = &pdev->dev;
	dev->handles = RB_ROOT;
	dev->serial_id_counter = 0;

#ifdef NVMAP_CONFIG_PAGE_POOLS
	e = nvmap_page_pool_init(dev);
	if (e)
		goto fail;
#endif

	spin_lock_init(&dev->handle_lock);
	INIT_LIST_HEAD(&dev->clients);
	dev->pids = RB_ROOT;
	mutex_init(&dev->clients_lock);
	INIT_LIST_HEAD(&dev->lru_handles);
	spin_lock_init(&dev->lru_lock);
	dev->tags = RB_ROOT;
	mutex_init(&dev->tags_lock);
	mutex_init(&dev->carveout_lock);

	nvmap_debug_init(&nvmap_dev->debug_root);

	nvmap_dev->dynamic_dma_map_mask = ~0U;
	nvmap_dev->cpu_access_mask = ~0U;

	if (plat)
		for (i = 0; i < plat->nr_carveouts; i++)
			nvmap_create_carveout(&plat->carveouts[i]);

#ifdef NVMAP_CONFIG_DEBUG_MAPS
	nvmap_dev->device_names = RB_ROOT;
#endif /* NVMAP_CONFIG_DEBUG_MAPS */

	platform_set_drvdata(pdev, dev);

	e = nvmap_dmabuf_stash_init();
	if (e)
		goto fail_heaps;

	for (i = 0; i < dev->nr_carveouts; i++)
		if (nvmap_get_heap_bit(dev->heaps[i]) & NVMAP_HEAP_CARVEOUT_GENERIC)
			generic_carveout_present = 1;

	if (generic_carveout_present) {
		if (!of_property_read_bool(pdev->dev.of_node,
			"dont-convert-iovmm-to-carveout"))
			nvmap_convert_iovmm_to_carveout = 1;
	} else {
		nvmap_convert_carveout_to_iovmm = 1;
	}

#ifdef NVMAP_CONFIG_PAGE_POOLS
	if (nvmap_convert_iovmm_to_carveout)
		nvmap_page_pool_fini(dev);
#endif

	e = nvmap_sci_ipc_init();
	if (e)
		goto fail_heaps;

	e = misc_register(&dev->dev_user);
	if (e) {
		dev_err(&pdev->dev, "unable to register miscdevice %s\n",
			dev->dev_user.name);
		goto fail_sci_ipc;
	}

	(void)nvmap_support_debug_apis(pdev, dev);
	goto finish;
fail_sci_ipc:
	nvmap_sci_ipc_exit();
fail_heaps:
	nvmap_debug_free(dev->debug_root);
	for (i = 0; i < dev->nr_carveouts; i++) {
		struct nvmap_carveout_node *node = dev->heaps[i];

		nvmap_heap_destroy(nvmap_get_heap_ptr(node));
		kfree(node);
	}
fail:
#ifdef NVMAP_CONFIG_PAGE_POOLS
	nvmap_page_pool_fini(nvmap_dev);
#endif
	kfree(dev->heaps);
	if (dev->dev_user.minor != MISC_DYNAMIC_MINOR)
		misc_deregister(&dev->dev_user);
	nvmap_dev = NULL;
finish:
	if (check_sub_overflow((ulong)sched_clock(),
				start_time, &result) ||
				check_add_overflow(nvmap_init_time,
				result, &result))
		return -EOVERFLOW;

	nvmap_init_time = result;
	return e;
}

int nvmap_remove(struct platform_device *pdev)
{
	struct nvmap_device *dev = platform_get_drvdata(pdev);
	int i;

	if (dev && !RB_EMPTY_ROOT(&dev->handles)) {
		pr_err("Can't remove nvmap module as handles exist\n");
		return -EBUSY;
	}

#ifdef NVMAP_CONFIG_SCIIPC
	nvmap_sci_ipc_exit();
#endif
	nvmap_dmabuf_stash_deinit();
	nvmap_debug_free(dev->debug_root);
	misc_deregister(&dev->dev_user);
#ifdef NVMAP_CONFIG_PAGE_POOLS
	nvmap_page_pool_clear();
	nvmap_page_pool_fini(nvmap_dev);
#endif

	for (i = 0; i < dev->nr_carveouts; i++) {
		struct nvmap_carveout_node *node = dev->heaps[i];

		nvmap_heap_destroy(nvmap_get_heap_ptr(node));
		kfree(node);
	}
	kfree(dev->heaps);
	of_reserved_mem_device_release(&pdev->dev);

	nvmap_dev = NULL;
	return 0;
}

#ifdef NVMAP_CONFIG_DEBUG_MAPS
struct nvmap_device_list *nvmap_is_device_present(char *device_name, u32 heap_type)
{
	struct rb_node *node = NULL;
	int i;

	if (heap_type == NVMAP_HEAP_IOVMM) {
		node = nvmap_dev->device_names.rb_node;
	} else {
		for (i = 0; i < nvmap_dev->nr_carveouts; i++) {
			if ((heap_type & nvmap_get_heap_bit(nvmap_dev->heaps[i])) &&
					nvmap_get_heap_ptr(nvmap_dev->heaps[i])) {
				node = nvmap_get_device_names(nvmap_dev->heaps[i])->rb_node;
				break;
			}
		}
	}
	while (node) {
		struct nvmap_device_list *dl = container_of(node,
						struct nvmap_device_list, node);
		if (strcmp(dl->device_name, device_name) > 0)
			node = node->rb_left;
		else if (strcmp(dl->device_name, device_name) < 0)
			node = node->rb_right;
		else
			return dl;
	}
	return NULL;
}

void nvmap_add_device_name(char *device_name, u64 dma_mask, u32 heap_type)
{
	struct rb_root *root = NULL;
	struct rb_node **new = NULL, *parent = NULL;
	struct nvmap_device_list *dl = NULL;
	int i;

	if (heap_type == NVMAP_HEAP_IOVMM) {
		root = &nvmap_dev->device_names;
	} else {
		for (i = 0; i < nvmap_dev->nr_carveouts; i++) {
			if ((heap_type & nvmap_get_heap_bit(nvmap_dev->heaps[i])) &&
				nvmap_get_heap_ptr(nvmap_dev->heaps[i])) {
				root = nvmap_get_device_names(nvmap_dev->heaps[i]);
				break;
			}
		}
	}
	if (root) {
		new = &(root->rb_node);
		while (*new) {
			dl = container_of(*new, struct nvmap_device_list, node);
			parent = *new;
			if (strcmp(dl->device_name, device_name) > 0)
				new = &((*new)->rb_left);
			else if (strcmp(dl->device_name, device_name) < 0)
				new = &((*new)->rb_right);
		}
		dl = kzalloc(sizeof(*dl), GFP_KERNEL);
		if (!dl)
			return;
		dl->device_name = kzalloc(strlen(device_name) + 1, GFP_KERNEL);
		if (!dl->device_name)
			return;
		strcpy(dl->device_name, device_name);
		dl->dma_mask = dma_mask;
		rb_link_node(&dl->node, parent, new);
		rb_insert_color(&dl->node, root);
	}
}

void nvmap_remove_device_name(char *device_name, u32 heap_type)
{
	struct nvmap_device_list *dl = NULL;
	int i;

	dl = nvmap_is_device_present(device_name, heap_type);
	if (dl) {
		if (heap_type == NVMAP_HEAP_IOVMM) {
			rb_erase(&dl->node,
				&nvmap_dev->device_names);
			kfree(dl->device_name);
			kfree(dl);
			return;
		}
		for (i = 0; i < nvmap_dev->nr_carveouts; i++) {
			if ((heap_type & nvmap_get_heap_bit(nvmap_dev->heaps[i])) &&
				nvmap_get_heap_ptr(nvmap_dev->heaps[i])) {
				rb_erase(&dl->node, nvmap_get_device_names(nvmap_dev->heaps[i]));
				kfree(dl->device_name);
				kfree(dl);
				return;
			}
		}
	}
}
#endif /* NVMAP_CONFIG_DEBUG_MAPS */
