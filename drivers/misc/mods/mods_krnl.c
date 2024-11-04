// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2008-2024, NVIDIA CORPORATION.  All rights reserved. */

#include "mods_internal.h"

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/poll.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#ifdef MODS_HAS_CONSOLE_LOCK
#   include <linux/console.h>
#   include <linux/kd.h>
#   include <linux/tty.h>
#   include <linux/console_struct.h>
#   include <linux/vt_kern.h>
#endif
#ifdef CONFIG_X86
#   include <linux/screen_info.h>
#   include <asm/msr.h>
#endif

/***********************************************************************
 * mods_krnl_* functions, driver interfaces called by the Linux kernel *
 ***********************************************************************/
static int mods_krnl_open(struct inode *, struct file *);
static int mods_krnl_close(struct inode *, struct file *);
static POLL_TYPE mods_krnl_poll(struct file *, poll_table *);
static int mods_krnl_mmap(struct file *, struct vm_area_struct *);
static long mods_krnl_ioctl(struct file *, unsigned int, unsigned long);

/* character driver entry points */
static const struct file_operations mods_fops = {
	.owner          = THIS_MODULE,
	.open           = mods_krnl_open,
	.release        = mods_krnl_close,
	.poll           = mods_krnl_poll,
	.mmap           = mods_krnl_mmap,
	.unlocked_ioctl = mods_krnl_ioctl,
#if defined(HAVE_COMPAT_IOCTL)
	.compat_ioctl   = mods_krnl_ioctl,
#endif
};

#define DEVICE_NAME "mods"

static struct miscdevice mods_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = DEVICE_NAME,
	.fops  = &mods_fops
};

#if defined(CONFIG_PCI)
static pci_ers_result_t mods_pci_error_detected(struct pci_dev *,
						pci_channel_state_t);
static pci_ers_result_t mods_pci_mmio_enabled(struct pci_dev *);
static void mods_pci_resume(struct pci_dev *);

static struct pci_error_handlers mods_pci_error_handlers = {
	.error_detected	= mods_pci_error_detected,
	.mmio_enabled	= mods_pci_mmio_enabled,
	.resume		= mods_pci_resume,
};

static const struct pci_device_id mods_pci_table[] = {
	{
		.vendor		= PCI_VENDOR_ID_NVIDIA,
		.device		= PCI_ANY_ID,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.class		= (PCI_CLASS_DISPLAY_VGA << 8),
		.class_mask	= ~0
	},
	{
		.vendor		= PCI_VENDOR_ID_NVIDIA,
		.device		= PCI_ANY_ID,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.class		= (PCI_CLASS_DISPLAY_3D << 8),
		.class_mask	= ~0
	},
	{
		.vendor		= PCI_VENDOR_ID_NVIDIA,
		.device		= PCI_ANY_ID,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.class		= (PCI_CLASS_BRIDGE_OTHER << 8),
		.class_mask	= ~0
	},
	{ 0 }
};

static int mods_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct en_dev_entry *dpriv;

	dpriv = kzalloc(sizeof(*dpriv), GFP_KERNEL | __GFP_NORETRY);
	if (unlikely(!dpriv))
		return -ENOMEM;

	dpriv->dev = pci_dev_get(dev);
	init_completion(&dpriv->client_completion);
	pci_set_drvdata(dev, dpriv);

	mods_debug_printk(DEBUG_PCI,
			  "probed dev %04x:%02x:%02x.%x vendor %04x device %04x\n",
			  pci_domain_nr(dev->bus),
			  dev->bus->number,
			  PCI_SLOT(dev->devfn),
			  PCI_FUNC(dev->devfn),
			  dev->vendor, dev->device);

	return 0;
}

static void mods_pci_remove(struct pci_dev *dev)
{
	struct en_dev_entry *dpriv = pci_get_drvdata(dev);

	WARN_ON(!dpriv);

	while (true) {
		mutex_lock(mods_get_irq_mutex());

		if (!is_valid_client_id(dpriv->client_id))
			break;

		mods_info_printk("removing dev %04x:%02x:%02x.%x, waiting for client %u\n",
					pci_domain_nr(dev->bus),
					dev->bus->number,
					PCI_SLOT(dev->devfn),
					PCI_FUNC(dev->devfn),
					dpriv->client_id);

		mutex_unlock(mods_get_irq_mutex());
		wait_for_completion(&dpriv->client_completion);
	}

	pci_dev_put(dpriv->dev);
	pci_set_drvdata(dev, NULL);
	kfree(dpriv);

	mutex_unlock(mods_get_irq_mutex());

	mods_debug_printk(DEBUG_PCI,
			  "removed dev %04x:%02x:%02x.%x vendor %04x device %04x\n",
			  pci_domain_nr(dev->bus),
			  dev->bus->number,
			  PCI_SLOT(dev->devfn),
			  PCI_FUNC(dev->devfn),
			  dev->vendor, dev->device);
}

#if defined(CONFIG_PCI) && defined(MODS_HAS_SRIOV)
static int mods_pci_sriov_configure(struct pci_dev *dev, int numvfs);
#endif

static struct pci_driver mods_pci_driver = {
	.name            = DEVICE_NAME,
	.id_table        = mods_pci_table,
	.probe           = mods_pci_probe,
	.remove          = mods_pci_remove,
	.err_handler     = &mods_pci_error_handlers,
#ifdef MODS_HAS_SRIOV
	.sriov_configure = mods_pci_sriov_configure,
#endif
};
#endif

/***********************************************
 * module wide parameters and access functions *
 * used to avoid globalization of variables    *
 ***********************************************/

#ifdef MODS_HAS_TEGRA
#       define MODS_MULTI_INSTANCE_DEFAULT_VALUE 1
#else
#       define MODS_MULTI_INSTANCE_DEFAULT_VALUE 0
#endif

static int debug;
static int multi_instance = MODS_MULTI_INSTANCE_DEFAULT_VALUE;
static u32 access_token   = MODS_ACCESS_TOKEN_NONE;

#if defined(CONFIG_PCI) && defined(MODS_HAS_SRIOV)
static int mods_pci_sriov_configure(struct pci_dev *dev, int numvfs)
{
	int totalvfs;
	int err = 0;

	LOG_ENT();

	totalvfs = pci_sriov_get_totalvfs(dev);

	if (numvfs > 0) {
		err = pci_enable_sriov(dev, numvfs);

		if (unlikely(err)) {
			mods_error_printk(
				"failed to enable sriov on dev %04x:%02x:%02x.%x %s numvfs=%d (totalvfs=%d), err=%d\n",
				pci_domain_nr(dev->bus),
				dev->bus->number,
				PCI_SLOT(dev->devfn),
				PCI_FUNC(dev->devfn),
				dev->is_physfn ? "physfn" : "virtfn",
				numvfs,
				totalvfs,
				err);
			numvfs = err;
		} else {
			mods_info_printk(
				"enabled sriov on dev %04x:%02x:%02x.%x %s numvfs=%d (totalvfs=%d)\n",
				pci_domain_nr(dev->bus),
				dev->bus->number,
				PCI_SLOT(dev->devfn),
				PCI_FUNC(dev->devfn),
				dev->is_physfn ? "physfn" : "virtfn",
				numvfs,
				totalvfs);
		}

	} else {
		pci_disable_sriov(dev);

		numvfs = 0;

		mods_info_printk(
			"disabled sriov on dev %04x:%02x:%02x.%x %s (totalvfs=%d)\n",
			pci_domain_nr(dev->bus),
			dev->bus->number,
			PCI_SLOT(dev->devfn),
			PCI_FUNC(dev->devfn),
			dev->is_physfn ? "physfn" : "virtfn",
			totalvfs);
	}

	/* If this function has been invoked via an ioctl, remember numvfs */
	if (!err) {
		struct en_dev_entry *dpriv = pci_get_drvdata(dev);

		if (dpriv)
			dpriv->num_vfs = numvfs;
	}

	LOG_EXT();
	return numvfs;
}

static int esc_mods_set_num_vf(struct mods_client     *client,
			       struct MODS_SET_NUM_VF *p)
{
	int                  err;
	struct pci_dev      *dev = NULL;
	struct en_dev_entry *dpriv;

	LOG_ENT();

	if (p->numvfs > 0xFFFFU) {
		cl_error("invalid input numfs %u\n", p->numvfs);
		err = -EINVAL;
		goto error;
	}

	/* Get the PCI device structure for the specified device from kernel */
	err = mods_find_pci_dev(client, &p->dev, &dev);
	if (unlikely(err)) {
		if (err == -ENODEV)
			cl_error("dev %04x:%02x:%02x.%x not found\n",
				 p->dev.domain,
				 p->dev.bus,
				 p->dev.device,
				 p->dev.function);
		goto error;
	}

	dpriv = pci_get_drvdata(dev);
	if (!dpriv || !is_valid_client_id(dpriv->client_id)) {
		cl_error(
			"failed to enable sriov, dev %04x:%02x:%02x.%x was not enabled\n",
			pci_domain_nr(dev->bus),
			dev->bus->number,
			PCI_SLOT(dev->devfn),
			PCI_FUNC(dev->devfn));
		err = -EINVAL;
		goto error;
	}
	if (dpriv->client_id != client->client_id) {
		cl_error(
			"invalid client for dev %04x:%02x:%02x.%x, expected %u\n",
			pci_domain_nr(dev->bus),
			dev->bus->number,
			PCI_SLOT(dev->devfn),
			PCI_FUNC(dev->devfn),
			dpriv->client_id);
		err = -EBUSY;
		goto error;
	}

	err = mods_pci_sriov_configure(dev, (u16)p->numvfs);

error:
	pci_dev_put(dev);
	LOG_EXT();
	return err;
}

static int esc_mods_set_total_vf(struct mods_client     *client,
				 struct MODS_SET_NUM_VF *p)
{
	int                  err;
	struct pci_dev      *dev = NULL;
	struct en_dev_entry *dpriv;

	LOG_ENT();

	if (p->numvfs > 0xFFFFU) {
		cl_error("invalid input numfs %u\n", p->numvfs);
		err = -EINVAL;
		goto error;
	}

	/* Get the PCI device structure for the specified device from kernel */
	err = mods_find_pci_dev(client, &p->dev, &dev);
	if (unlikely(err)) {
		if (err == -ENODEV)
			cl_error("dev %04x:%02x:%02x.%x not found\n",
				 p->dev.domain,
				 p->dev.bus,
				 p->dev.device,
				 p->dev.function);
		goto error;
	}

	dpriv = pci_get_drvdata(dev);
	if (!dpriv || !is_valid_client_id(dpriv->client_id)) {
		cl_error(
			"failed to enable sriov, dev %04x:%02x:%02x.%x was not enabled\n",
			pci_domain_nr(dev->bus),
			dev->bus->number,
			PCI_SLOT(dev->devfn),
			PCI_FUNC(dev->devfn));
		err = -EBUSY;
		goto error;
	}
	if (dpriv->client_id != client->client_id) {
		cl_error(
			"invalid client for dev %04x:%02x:%02x.%x, expected %u\n",
			pci_domain_nr(dev->bus),
			dev->bus->number,
			PCI_SLOT(dev->devfn),
			PCI_FUNC(dev->devfn),
			dpriv->client_id);
		err = -EBUSY;
		goto error;
	}

	err = pci_sriov_set_totalvfs(dev, (u16)p->numvfs);

	if (unlikely(err)) {
		cl_error(
			"failed to set totalvfs=%d on dev %04x:%02x:%02x.%x, err=%d\n",
			p->numvfs,
			p->dev.domain,
			p->dev.bus,
			p->dev.device,
			p->dev.function,
			err);
	} else
		cl_info("set totalvfs %d on dev %04x:%02x:%02x.%x\n",
			p->numvfs,
			p->dev.domain,
			p->dev.bus,
			p->dev.device,
			p->dev.function);

error:
	pci_dev_put(dev);
	LOG_EXT();
	return err;
}
#endif

#if defined(CONFIG_PPC64)
static int ppc_tce_bypass = MODS_PPC_TCE_BYPASS_ON;

void mods_set_ppc_tce_bypass(int bypass)
{
	ppc_tce_bypass = bypass;
}

int mods_get_ppc_tce_bypass(void)
{
	return ppc_tce_bypass;
}
#endif

void mods_set_debug_level(int mask)
{
	debug = mask;
}

int mods_get_debug_level(void)
{
	return debug;
}

int mods_check_debug_level(int mask)
{
	return ((debug & mask) == mask) ? 1 : 0;
}

void mods_set_multi_instance(int mi)
{
	multi_instance = (mi > 0) ? 1 : -1;
}

int mods_get_multi_instance(void)
{
	return multi_instance > 0;
}

/*********************
 * CLIENT MANAGEMENT *
 *********************/
static struct mods_priv mp;

static struct mods_client *alloc_client(void)
{
	unsigned int idx;
	unsigned int max_clients = 1;

	LOG_ENT();

	if (mods_get_multi_instance() ||
	    (mods_get_access_token() != MODS_ACCESS_TOKEN_NONE))
		max_clients = MODS_MAX_CLIENTS;

	for (idx = 1; idx <= max_clients; idx++) {
		if (!test_and_set_bit(idx - 1U, &mp.client_flags)) {
			struct mods_client *client = mods_client_from_id(idx);

			memset(client, 0, sizeof(*client));
			client->client_id    = idx;
			client->access_token = MODS_ACCESS_TOKEN_NONE;
			atomic_set(&client->last_bad_dbdf, -1);

			cl_debug(DEBUG_IOCTL,
				 "open client (bit mask 0x%lx)\n",
				 mp.client_flags);

			mutex_init(&client->mtx);
			spin_lock_init(&client->irq_lock);
			init_waitqueue_head(&client->interrupt_event);
			INIT_LIST_HEAD(&client->irq_list);
			INIT_LIST_HEAD(&client->mem_alloc_list);
			INIT_LIST_HEAD(&client->enabled_devices);
			INIT_LIST_HEAD(&client->mem_map_list);
			INIT_LIST_HEAD(&client->free_mem_list);
#if defined(CONFIG_PPC64)
			INIT_LIST_HEAD(&client->ppc_tce_bypass_list);
			INIT_LIST_HEAD(&client->nvlink_sysmem_trained_list);
#endif

			LOG_EXT();
			return client;
		}
	}

	LOG_EXT();
	return NULL;
}

static void free_client(u8 client_id)
{
	struct mods_client *client = mods_client_from_id(client_id);

	LOG_ENT();

	memset(client, 0, sizeof(*client));

	/* Indicate the client_id is free */
	clear_bit((unsigned int)client_id - 1U, &mp.client_flags);

	cl_debug(DEBUG_IOCTL, "closed client\n");
	LOG_EXT();
}

struct mods_client *mods_client_from_id(u8 client_id)
{
	return &mp.clients[client_id - 1];
}

int mods_is_client_enabled(u8 client_id)
{
	return test_bit(client_id - 1, &mp.client_flags);
}

u32 mods_get_access_token(void)
{
	return access_token;
}

static int validate_client(struct mods_client *client)
{
	if (!client) {
		mods_error_printk("invalid client\n");
		return false;
	}

	if (client->client_id < 1 ||
	    client->client_id > MODS_MAX_CLIENTS ||
	    !mods_is_client_enabled(client->client_id)) {
		cl_error("invalid client id\n");
		return false;
	}

	return true;
}

static int mods_set_access_token(u32 tok)
{
	/* When setting a null token, the existing token must match the
	 * provided token, when setting a non-null token the existing token
	 * must be null, use atomic compare/exchange to set it
	 */
	u32 req_old_token =
	    (tok == MODS_ACCESS_TOKEN_NONE) ?
		access_token : MODS_ACCESS_TOKEN_NONE;

	if (cmpxchg(&access_token, req_old_token, tok) != req_old_token)
		return -EFAULT;
	return OK;
}

static int mods_check_access_token(struct mods_client *client)
{
	if (client->access_token != mods_get_access_token()) {
		cl_error("invalid access token %u\n", client->access_token);
		return -EFAULT;
	}

	return OK;
}

/******************************
 * INIT/EXIT MODULE FUNCTIONS *
 ******************************/
static int __init mods_init_module(void)
{
	int rc;

	LOG_ENT();

	memset(&mp, 0, sizeof(mp));

	mods_init_irq();

	rc = misc_register(&mods_dev);
	if (rc < 0)
		return -EBUSY;

#if defined(CONFIG_PCI)
	rc = pci_register_driver(&mods_pci_driver);
	if (rc < 0)
		return -EBUSY;
#endif

#if defined(MODS_HAS_TEGRA) && defined(CONFIG_COMMON_CLK)
	mods_init_clock_api();
#endif

	rc = mods_create_debugfs(&mods_dev);
	if (rc < 0)
		return rc;

	rc = mods_init_dmabuf();
	if (rc < 0)
		return rc;

#if defined(MODS_HAS_TEGRA)
	rc = smmu_driver_init();
	if (rc < 0)
		return rc;

#if defined(CONFIG_DMA_ENGINE)
	rc = mods_init_dma();
	if (rc < 0)
		return rc;
#endif
#endif

#if defined(MODS_HAS_ARM_FFA)
	rc = mods_ffa_abi_register();
	if (rc < 0)
		mods_warning_printk("error on mods_ffa_abi_register returned %d\n", rc);
#endif

	mods_info_printk("*** WARNING: DIAGNOSTIC DRIVER LOADED ***\n");
	mods_info_printk("driver loaded, version %x.%02x\n",
			 (MODS_DRIVER_VERSION >> 8),
			 (MODS_DRIVER_VERSION & 0xFF));

	if (debug)
		mods_info_printk("debug level 0x%x\n", debug);

	LOG_EXT();
	return OK;
}

static void __exit mods_exit_module(void)
{
	int i;

	LOG_ENT();

	mods_exit_dmabuf();

	mods_remove_debugfs();

	for (i = 0; i < MODS_MAX_CLIENTS; i++) {
		if (mp.client_flags & (1U << i))
			free_client(i + 1);
	}

#if defined(MODS_HAS_TEGRA)
#if defined(CONFIG_DMA_ENGINE)
	mods_exit_dma();
#endif
	smmu_driver_exit();
#endif

#if defined(CONFIG_PCI)
	pci_unregister_driver(&mods_pci_driver);
#endif

	misc_deregister(&mods_dev);

#if defined(MODS_HAS_TEGRA) && defined(CONFIG_COMMON_CLK)
	mods_shutdown_clock_api();
#endif

#if defined(MODS_HAS_ARM_FFA)
	mods_ffa_abi_unregister();
#endif
	mods_free_mem_reservations();
	mods_info_printk("driver unloaded\n");
	LOG_EXT();
}

/***************************
 * KERNEL INTERFACE SET UP *
 ***************************/
module_init(mods_init_module);
module_exit(mods_exit_module);

MODULE_LICENSE("GPL");

#define STRING_VALUE(x) #x
#define MAKE_MODULE_VERSION(x, y) STRING_VALUE(x) "." STRING_VALUE(y)
MODULE_VERSION(MAKE_MODULE_VERSION(MODS_DRIVER_VERSION_MAJOR,
				   MODS_DRIVER_VERSION_MINOR));

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug,
	"debug bitflags (2=ioctl 4=pci 8=acpi 16=irq 32=mem 64=fun +256=detailed)");

module_param(multi_instance, int, 0644);
MODULE_PARM_DESC(multi_instance,
	"allows more than one client to simultaneously open the driver");

#if defined(CONFIG_PPC64)
module_param(ppc_tce_bypass, int, 0644);
MODULE_PARM_DESC(ppc_tce_bypass,
	"PPC TCE bypass (0=sys default, 1=force bypass, 2=force non bypass)");
#endif

/********************
 * HELPER FUNCTIONS *
 ********************/
static void mods_disable_all_devices(struct mods_client *client)
{
#ifdef CONFIG_PCI
	struct list_head *head = &client->enabled_devices;
	struct en_dev_entry *entry;
	struct en_dev_entry *tmp;

#ifdef MODS_HAS_SRIOV
	mutex_lock(mods_get_irq_mutex());
	list_for_each_entry_safe(entry, tmp, head, list) {
		struct en_dev_entry *dpriv = pci_get_drvdata(entry->dev);

		if (dpriv->num_vfs == 0) {
			mods_disable_device(client, entry->dev);
			list_del(&entry->list);
		}
	}
	mutex_unlock(mods_get_irq_mutex());

	list_for_each_entry(entry, head, list) {
		pci_disable_sriov(entry->dev);
	}
#endif

	mutex_lock(mods_get_irq_mutex());
	list_for_each_entry_safe(entry, tmp, head, list) {
		mods_disable_device(client, entry->dev);
		list_del(&entry->list);
	}
	mutex_unlock(mods_get_irq_mutex());

	if (client->cached_dev) {
		pci_dev_put(client->cached_dev);
		client->cached_dev = NULL;
	}
#else
	WARN_ON(!list_empty(&client->enabled_devices));
#endif
}

#if defined(MODS_HAS_CONSOLE_LOCK)
static int mods_resume_console(struct mods_client *client);
#else
static inline int mods_resume_console(struct mods_client *client) { return 0; }
#endif

/*********************
 * MAPPING FUNCTIONS *
 *********************/
static int register_mapping(struct mods_client    *client,
			    struct MODS_MEM_INFO  *p_mem_info,
			    phys_addr_t            phys_addr,
			    struct SYS_MAP_MEMORY *p_map_mem,
			    unsigned long          virtual_address,
			    unsigned long          mapping_offs,
			    unsigned long          mapping_length)
{
	LOG_ENT();

	p_map_mem->phys_addr      = phys_addr;
	p_map_mem->virtual_addr   = virtual_address;
	p_map_mem->mapping_offs   = mapping_offs;
	p_map_mem->mapping_length = mapping_length;
	p_map_mem->p_mem_info     = p_mem_info;

	list_add(&p_map_mem->list, &client->mem_map_list);

	cl_debug(DEBUG_MEM_DETAILED,
		 "map alloc %p as %p: phys 0x%llx, virt 0x%lx, size 0x%lx\n",
		 p_mem_info,
		 p_map_mem,
		 (unsigned long long)phys_addr,
		 virtual_address,
		 mapping_length);

	LOG_EXT();
	return OK;
}

static pgprot_t get_prot(struct mods_client *client,
			 u8                  mem_type,
			 pgprot_t            prot)
{
	switch (mem_type) {
	case MODS_ALLOC_CACHED:
		return prot;

	case MODS_ALLOC_UNCACHED:
		return MODS_PGPROT_UC(prot);

	case MODS_ALLOC_WRITECOMBINE:
		return MODS_PGPROT_WC(prot);

	default:
		cl_warn("unsupported memory type: %u\n", mem_type);
		return prot;
	}
}

static int get_prot_for_range(struct mods_client *client,
			      phys_addr_t         phys_addr,
			      unsigned long       size,
			      pgprot_t           *prot)
{
	const phys_addr_t req_end     = phys_addr + size;
	const phys_addr_t range_begin = client->mem_type.phys_addr;
	const phys_addr_t range_end   = range_begin + client->mem_type.size;

	/* Check overlap with set memory type range */
	if (phys_addr < range_end && req_end > range_begin) {

		/* Check if requested range is completely inside */
		if (likely(phys_addr >= range_begin && req_end <= range_end)) {
			*prot = get_prot(client, client->mem_type.type, *prot);
			return 0;
		}

		cl_error("mmap range [0x%llx, 0x%llx] does not match set memory type range [0x%llx, 0x%llx]\n",
			 (unsigned long long)phys_addr,
			 (unsigned long long)req_end,
			 (unsigned long long)range_begin,
			 (unsigned long long)range_end);
		return -EINVAL;
	}

	return 0;
}

const char *mods_get_prot_str(u8 mem_type)
{
	switch (mem_type) {
	case MODS_ALLOC_CACHED:
		return "WB";

	case MODS_ALLOC_UNCACHED:
		return "UC";

	case MODS_ALLOC_WRITECOMBINE:
		return "WC";

	default:
		return "unknown";
	}
}

static const char *get_prot_str_for_range(struct mods_client *client,
					  phys_addr_t         phys_addr,
					  unsigned long       size)
{
	const phys_addr_t req_end     = phys_addr + size;
	const phys_addr_t range_begin = client->mem_type.phys_addr;
	const phys_addr_t range_end   = range_begin + client->mem_type.size;

	/* Check overlap with set memory type range */
	if (phys_addr < range_end && req_end > range_begin)
		return mods_get_prot_str(client->mem_type.type);

	return "default";
}

/********************
 * PCI ERROR FUNCTIONS *
 ********************/
#if defined(CONFIG_PCI)
static pci_ers_result_t mods_pci_error_detected(struct pci_dev *dev,
						pci_channel_state_t state)
{
	mods_debug_printk(DEBUG_PCI,
			  "pci_error_detected dev %04x:%02x:%02x.%x\n",
			  pci_domain_nr(dev->bus),
			  dev->bus->number,
			  PCI_SLOT(dev->devfn),
			  PCI_FUNC(dev->devfn));

	return PCI_ERS_RESULT_CAN_RECOVER;
}

static pci_ers_result_t mods_pci_mmio_enabled(struct pci_dev *dev)
{
	mods_debug_printk(DEBUG_PCI,
			  "pci_mmio_enabled dev %04x:%02x:%02x.%x\n",
			  pci_domain_nr(dev->bus),
			  dev->bus->number,
			  PCI_SLOT(dev->devfn),
			  PCI_FUNC(dev->devfn));

	return PCI_ERS_RESULT_NEED_RESET;
}

static void mods_pci_resume(struct pci_dev *dev)
{
	mods_debug_printk(DEBUG_PCI,
			  "pci_resume dev %04x:%02x:%02x.%x\n",
			  pci_domain_nr(dev->bus),
			  dev->bus->number,
			  PCI_SLOT(dev->devfn),
			  PCI_FUNC(dev->devfn));
}
#endif

/********************
 * KERNEL FUNCTIONS *
 ********************/
static void mods_krnl_vma_open(struct vm_area_struct *vma)
{
	struct SYS_MAP_MEMORY *p_map_mem;

	LOG_ENT();

	mods_debug_printk(DEBUG_MEM_DETAILED,
			  "open vma, virt 0x%lx, size 0x%lx, phys 0x%llx\n",
			  vma->vm_start,
			  vma->vm_end - vma->vm_start,
			  (unsigned long long)vma->vm_pgoff << PAGE_SHIFT);

	p_map_mem = vma->vm_private_data;
	if (p_map_mem)
		atomic_inc(&p_map_mem->usage_count);

	LOG_EXT();
}

static void mods_krnl_vma_close(struct vm_area_struct *vma)
{
	struct SYS_MAP_MEMORY *p_map_mem;

	LOG_ENT();

	p_map_mem = vma->vm_private_data;

	if (p_map_mem && atomic_dec_and_test(&p_map_mem->usage_count)) {
		struct mods_client *client = p_map_mem->client;

		if (p_map_mem->mapping_length) {
			mutex_lock(&client->mtx);
			list_del(&p_map_mem->list);
			mutex_unlock(&client->mtx);
		}

		mods_debug_printk(DEBUG_MEM_DETAILED,
				  "closed vma, virt 0x%lx\n",
				  vma->vm_start);

		vma->vm_private_data = NULL;

		kfree(p_map_mem);
		atomic_dec(&client->num_allocs);
	}

	LOG_EXT();
}

#ifdef CONFIG_HAVE_IOREMAP_PROT
static int mods_krnl_vma_access(struct vm_area_struct *vma,
				unsigned long          addr,
				void                  *buf,
				int                    len,
				int                    write)
{
	struct SYS_MAP_MEMORY *p_map_mem = vma->vm_private_data;
	struct mods_client    *client;
	unsigned long          map_offs;
	int                    err  = OK;

	LOG_ENT();

	if (!p_map_mem) {
		LOG_EXT();
		return -EINVAL;
	}

	client = p_map_mem->client;

	cl_debug(DEBUG_MEM_DETAILED,
		 "access vma [virt 0x%lx, size 0x%lx, phys 0x%llx] at virt 0x%lx, len 0x%x\n",
		 vma->vm_start,
		 vma->vm_end - vma->vm_start,
		 (unsigned long long)vma->vm_pgoff << PAGE_SHIFT,
		 addr,
		 len);

	if (unlikely(mutex_lock_interruptible(&client->mtx))) {
		LOG_EXT();
		return -EINTR;
	}

	if (unlikely(!p_map_mem || addr < p_map_mem->virtual_addr ||
		     addr + len > p_map_mem->virtual_addr +
				  p_map_mem->mapping_length)) {
		cl_error("mapped range [virt 0x%lx, size 0x%x] does not match vma [virt 0x%lx, size 0x%lx]\n",
			 addr, len, vma->vm_start, vma->vm_end - vma->vm_start);
		mutex_unlock(&client->mtx);
		LOG_EXT();
		return -ENOMEM;
	}

	map_offs = addr - vma->vm_start + p_map_mem->mapping_offs;

	if (p_map_mem->p_mem_info) {
		struct MODS_MEM_INFO *p_mem_info = p_map_mem->p_mem_info;
		struct scatterlist   *sg         = NULL;
		const u32             num_chunks = get_num_chunks(p_mem_info);
		u32                   i;

		for_each_sg(p_mem_info->sg, sg, num_chunks, i) {

			if (map_offs < sg->length)
				break;

			map_offs -= sg->length;
		}

		if (unlikely(!sg))
			err = -ENOMEM;
		else {
			void        *ptr;
			struct page *p_page = sg_page(sg) +
					      (map_offs >> PAGE_SHIFT);

			map_offs &= ~PAGE_MASK;

			if (map_offs + len > PAGE_SIZE)
				len = (int)(PAGE_SIZE - map_offs);

			ptr = MODS_KMAP(p_page);
			if (ptr) {
				char *bptr = (char *)ptr + map_offs;

				if (write)
					memcpy(bptr, buf, len);
				else
					memcpy(buf, bptr, len);

				MODS_KUNMAP(ptr);

				err = len;
			} else
				err = -ENOMEM;
		}
	} else if (!write) {
		char __iomem *ptr;
		phys_addr_t   pa;

		map_offs += vma->vm_pgoff << PAGE_SHIFT;
		pa       =  map_offs & PAGE_MASK;
		map_offs &= ~PAGE_MASK;

		if (map_offs + len > PAGE_SIZE)
			len = (int)(PAGE_SIZE - map_offs);

		ptr = ioremap(pa, PAGE_SIZE);

		if (ptr) {
			memcpy_fromio(buf, ptr + map_offs, len);

			iounmap(ptr);

			err = len;
		} else
			err = -ENOMEM;
	} else
		/* Writing to device memory from gdb is not supported */
		err = -ENOMEM;

	mutex_unlock(&client->mtx);

	LOG_EXT();
	return err;
}
#endif

static const struct vm_operations_struct mods_krnl_vm_ops = {
#ifdef CONFIG_HAVE_IOREMAP_PROT
	.access = mods_krnl_vma_access,
#endif
	.open	= mods_krnl_vma_open,
	.close	= mods_krnl_vma_close
};

static int mods_krnl_open(struct inode *ip, struct file *fp)
{
	struct mods_client *client;

	LOG_ENT();

	client = alloc_client();
	if (client == NULL) {
		mods_error_printk("too many clients\n");
		LOG_EXT();
		return -EBUSY;
	}

	fp->private_data = client;

	cl_info("driver opened, pid=%d\n", current->pid);
	LOG_EXT();
	return OK;
}

static int mods_krnl_close(struct inode *ip, struct file *fp)
{
	struct mods_client *client    = fp->private_data;
	int                 final_err = OK;
	int                 err       = OK;
	u8                  client_id;

	LOG_ENT();

	if (!validate_client(client)) {
		LOG_EXT();
		return -EINVAL;
	}

	client_id = client->client_id;

	mods_free_client_interrupts(client);

	mods_resume_console(client);

	/* All memory mappings should be gone before close */
	if (unlikely(!list_empty(&client->mem_map_list)))
		cl_error("not all memory mappings have been freed\n");

	err = mods_unregister_all_alloc(client);
	if (err)
		cl_error("failed to free all memory\n");
	final_err = err;

#if defined(CONFIG_PPC64)
	err = mods_unregister_all_ppc_tce_bypass(client);
	if (err)
		cl_error("failed to restore dma bypass\n");
	if (!final_err)
		final_err = err;

	err = mods_unregister_all_nvlink_sysmem_trained(client);
	if (err)
		cl_error("failed to free nvlink trained\n");
	if (!final_err)
		final_err = err;
#endif

#if defined(CONFIG_TEGRA_IVC)
	mods_bpmpipc_cleanup();
#endif

	mods_disable_all_devices(client);

	{
		const int num_allocs = atomic_read(&client->num_allocs);
		const int num_pages  = atomic_read(&client->num_pages);

		if (num_allocs || num_pages) {
			cl_error(
				"not all allocations have been freed, allocs=%d, pages=%d\n",
				num_allocs, num_pages);
			if (!final_err)
				final_err = -ENOMEM;
		}
	}

	if (client->work_queue) {
		destroy_workqueue(client->work_queue);
		client->work_queue = NULL;
	}

	free_client(client_id);

	pr_info("mods [%d]: driver closed\n", client_id);

	LOG_EXT();
	return final_err;
}

static POLL_TYPE mods_krnl_poll(struct file *fp, poll_table *wait)
{
	struct mods_client *client = fp->private_data;
	POLL_TYPE           mask   = 0;
	int                 err;

	if (!validate_client(client))
		return POLLERR;

	err = mods_check_access_token(client);
	if (err < 0)
		return POLLERR;

	if (!(fp->f_flags & O_NONBLOCK)) {
		cl_debug(DEBUG_ISR_DETAILED, "poll wait\n");
		poll_wait(fp, &client->interrupt_event, wait);
	}

	/* if any interrupts pending then check intr, POLLIN on irq */
	mask |= mods_irq_event_check(client->client_id);

	cl_debug(DEBUG_ISR_DETAILED, "poll mask 0x%x\n", mask);

	return mask;
}

static int map_internal(struct mods_client    *client,
			struct vm_area_struct *vma);

static int mods_krnl_mmap(struct file *fp, struct vm_area_struct *vma)
{
	struct SYS_MAP_MEMORY *p_map_mem;
	struct mods_client    *client = fp->private_data;
	int err;

	LOG_ENT();

	if (!validate_client(client)) {
		LOG_EXT();
		return -EINVAL;
	}

	err = mods_check_access_token(client);
	if (err < 0) {
		LOG_EXT();
		return err;
	}

	vma->vm_ops = &mods_krnl_vm_ops;

	p_map_mem = kzalloc(sizeof(*p_map_mem), GFP_KERNEL | __GFP_NORETRY);
	if (unlikely(!p_map_mem)) {
		LOG_EXT();
		return -ENOMEM;
	}
	atomic_inc(&client->num_allocs);

	p_map_mem->client    = client;
	vma->vm_private_data = p_map_mem;

	mods_krnl_vma_open(vma);

	err = mutex_lock_interruptible(&client->mtx);
	if (likely(!err)) {
		err = map_internal(client, vma);
		mutex_unlock(&client->mtx);
	}

	if (unlikely(err))
		mods_krnl_vma_close(vma);

	LOG_EXT();
	return err;
}

static int map_system_mem(struct mods_client    *client,
			  struct vm_area_struct *vma,
			  struct MODS_MEM_INFO  *p_mem_info)
{
	struct scatterlist *sg          = NULL;
	const char         *cache_str   = mods_get_prot_str(p_mem_info->cache_type);
	const phys_addr_t   req_pa = (phys_addr_t)vma->vm_pgoff << PAGE_SHIFT;
	phys_addr_t         reg_pa      = 0;
	const unsigned long vma_size    = vma->vm_end - vma->vm_start;
	unsigned long       size_to_map = vma_size;
	unsigned long       skip_size   = 0;
	unsigned long       map_va      = 0;
	const u32           num_chunks  = get_num_chunks(p_mem_info);
	u32                 map_chunks;
	u32                 i           = 0;
	pgprot_t            prot        = get_prot(client,
						   p_mem_info->cache_type,
						   vma->vm_page_prot);

#ifdef MODS_HAS_PGPROT_DECRYPT
	if (p_mem_info->decrypted_mmap)
		prot = pgprot_decrypted(prot);
#endif

	/* Find the beginning of the requested range */
	for_each_sg(p_mem_info->sg, sg, num_chunks, i) {
		const phys_addr_t  phys_addr = sg_phys(sg);
		const unsigned int size      = sg->length;

		if ((req_pa >= phys_addr) &&
		    (req_pa <  phys_addr + size)) {
			break;
		}

		skip_size += size;
	}

	if (i == num_chunks) {
		cl_error("can't satisfy requested mapping\n");
		return -EINVAL;
	}

	if (((skip_size + vma_size) >> PAGE_SHIFT) > p_mem_info->num_pages) {
		cl_error("requested mapping exceeds bounds\n");
		return -EINVAL;
	}

	/* Map pages into VA space */
	map_va     = vma->vm_start;
	map_chunks = num_chunks - i;
	for_each_sg(sg, sg, map_chunks, i) {

		const phys_addr_t chunk_pa = sg_phys(sg);
		phys_addr_t       map_pa   = chunk_pa;
		unsigned int      map_size = sg->length;

		if (i == 0) {
			const phys_addr_t aoffs = req_pa - chunk_pa;

			map_pa    += aoffs;
			map_size  -= aoffs;
			skip_size += aoffs;
			reg_pa     = chunk_pa;
		}

		if (map_size > size_to_map)
			map_size = (unsigned int)size_to_map;

		cl_debug(DEBUG_MEM_DETAILED,
			 "remap va 0x%lx pfn 0x%lx size 0x%x pages %u %s\n",
			 map_va,
			 (unsigned long)(map_pa >> PAGE_SHIFT),
			 map_size,
			 map_size >> PAGE_SHIFT,
			 cache_str);

		if (remap_pfn_range(vma,
				    map_va,
				    (unsigned long)(map_pa >> PAGE_SHIFT),
				    map_size,
				    prot)) {
			cl_error("failed to map memory\n");
			return -EAGAIN;
		}

		map_va      += map_size;
		size_to_map -= map_size;
		if (!size_to_map)
			break;
	}

	register_mapping(client,
			 p_mem_info,
			 reg_pa,
			 vma->vm_private_data,
			 vma->vm_start,
			 skip_size,
			 vma_size);

	return OK;
}

static int map_device_mem(struct mods_client    *client,
			  struct vm_area_struct *vma)
{
	/* device memory */
	const phys_addr_t   req_pa   = (phys_addr_t)vma->vm_pgoff << PAGE_SHIFT;
	const unsigned long vma_size = vma->vm_end - vma->vm_start;
	pgprot_t            prot     = vma->vm_page_prot;
	int                 err;

	cl_debug(DEBUG_MEM,
		 "map dev: phys 0x%llx, virt 0x%lx, size 0x%lx, %s\n",
		 (unsigned long long)req_pa,
		 vma->vm_start,
		 vma_size,
		 get_prot_str_for_range(client, req_pa, vma_size));

	err = get_prot_for_range(client, req_pa, vma_size, &prot);
	if (unlikely(err))
		return err;

	if (unlikely(io_remap_pfn_range(vma,
					vma->vm_start,
					vma->vm_pgoff,
					vma_size,
					prot))) {
		cl_error("failed to map device memory\n");
		return -EAGAIN;
	}

	register_mapping(client,
			 NULL,
			 req_pa,
			 vma->vm_private_data,
			 vma->vm_start,
			 0,
			 vma_size);

	return OK;
}

static int map_internal(struct mods_client    *client,
			struct vm_area_struct *vma)
{
	const phys_addr_t     req_pa     = (phys_addr_t)vma->vm_pgoff << PAGE_SHIFT;
	struct MODS_MEM_INFO *p_mem_info = mods_find_alloc(client, req_pa);
	const unsigned long   vma_size   = vma->vm_end - vma->vm_start;

	if (unlikely((vma_size & ~PAGE_MASK) != 0)) {
		cl_error("requested mapping is not page-aligned\n");
		return -EINVAL;
	}

	if (p_mem_info)
		return map_system_mem(client, vma, p_mem_info);
	else
		return map_device_mem(client, vma);
}

#if defined(CONFIG_X86)
static void mods_get_screen_info(struct MODS_SCREEN_INFO *p)
{
	p->orig_video_mode = screen_info.orig_video_mode;
	p->orig_video_is_vga = screen_info.orig_video_isVGA;
	p->lfb_width = screen_info.lfb_width;
	p->lfb_height = screen_info.lfb_height;
	p->lfb_depth = screen_info.lfb_depth;
	p->lfb_base = screen_info.lfb_base;
	p->lfb_size = screen_info.lfb_size;
	p->lfb_linelength = screen_info.lfb_linelength;
}
#endif

/*************************
 * ESCAPE CALL FUNCTIONS *
 *************************/

static int esc_mods_get_api_version(struct mods_client      *client,
				    struct MODS_GET_VERSION *p)
{
	p->version = MODS_DRIVER_VERSION;
	return OK;
}

static int esc_mods_get_kernel_version(struct mods_client      *client,
				       struct MODS_GET_VERSION *p)
{
	p->version = MODS_KERNEL_VERSION;
	return OK;
}

#if defined(CONFIG_X86)
static int esc_mods_get_screen_info(struct mods_client      *client,
				    struct MODS_SCREEN_INFO *p)
{
	mods_get_screen_info(p);

#if defined(VIDEO_CAPABILITY_64BIT_BASE)
	if (screen_info.ext_lfb_base)
		return -EOVERFLOW;
#endif

	return OK;
}

static int esc_mods_get_screen_info_2(struct mods_client        *client,
				      struct MODS_SCREEN_INFO_2 *p)
{
#if defined(CONFIG_FB) && defined(MODS_HAS_FB_SET_SUSPEND)
	unsigned int i;
	bool         found = false;
#endif

	mods_get_screen_info(&p->info);

#if defined(VIDEO_CAPABILITY_64BIT_BASE)
	p->ext_lfb_base = screen_info.ext_lfb_base;
#else
	p->ext_lfb_base = 0;
#endif

#if defined(CONFIG_FB) && defined(MODS_HAS_FB_SET_SUSPEND)
	if (screen_info.orig_video_isVGA != VIDEO_TYPE_EFI)
		return OK;

	/* With pci=realloc on the kernel command line, GPU BAR1 can be
	 * reassigned after the OS console is allocated.  When this
	 * occurs the lfb_base variable is *not* updated for an EFI
	 * console.  The incorrect lfb_base variable will prevent other
	 * drivers or user space applications from identifying memory
	 * in use by the console and potentially using it themselves.

	 * For an EFI console, pull the FB base address from the FB
	 * driver registered_fb data instead of screen_info.
	 * Note: on kernel 6.1 and up registered_fb is not available.
	 */
	for (i = 0; i < ARRAY_SIZE(registered_fb); i++) {
		bool skipped = true;

		if (!registered_fb[i])
			continue;

		if (!strcmp(registered_fb[i]->fix.id, "EFI VGA") && !found) {
			p->info.lfb_base =
			 registered_fb[i]->fix.smem_start & 0xFFFFFFFF;
			p->ext_lfb_base =
			 registered_fb[i]->fix.smem_start >> 32;
			found = true;
			skipped = false;
		}

		cl_info("%s fb%d '%s' @0x%llx\n",
			skipped ? "skip" : "found",
			i, registered_fb[i]->fix.id,
			(unsigned long long)registered_fb[i]->fix.smem_start);
	}
#endif

	return OK;
}
#endif

#if defined(MODS_HAS_CONSOLE_LOCK)

#if defined(CONFIG_FB) && defined(MODS_HAS_FB_SET_SUSPEND)
static int suspend_fb(struct mods_client *client)
{
	unsigned int i;
	int          err = -EINVAL;

	/* tell the os to block fb accesses */
	for (i = 0; i < ARRAY_SIZE(registered_fb); i++) {
		bool suspended = false;

		if (!registered_fb[i])
			continue;

		console_lock();
		if (registered_fb[i]->state != FBINFO_STATE_SUSPENDED) {
			fb_set_suspend(registered_fb[i], 1);
			client->mods_fb_suspended[i] = 1;
			suspended = true;
		}
		console_unlock();
		err = OK;

		if (suspended)
			cl_info("suspended fb%u '%s'\n", i,
				registered_fb[i]->fix.id);
	}

	return err;
}

static int resume_fb(struct mods_client *client)
{
	unsigned int i;
	int          err = -EINVAL;

	for (i = 0; i < ARRAY_SIZE(registered_fb); i++) {
		bool resumed = false;

		if (!registered_fb[i])
			continue;

		console_lock();
		if (client->mods_fb_suspended[i]) {
			fb_set_suspend(registered_fb[i], 0);
			client->mods_fb_suspended[i] = 0;
			resumed = true;
		}
		console_unlock();
		err = OK;

		if (resumed)
			cl_info("resumed fb%u\n", i);
	}

	return err;
}
#else
#define suspend_fb(client) (-EINVAL)
#define resume_fb(client) (-EINVAL)
#endif

static atomic_t console_is_locked;
static atomic_t console_suspend_client_id;

static int esc_mods_lock_console(struct mods_client *client)
{
	if (atomic_cmpxchg(&console_is_locked, 0, 1)) {
		cl_error("console is already locked\n");
		return -EINVAL;
	}

	atomic_set(&client->console_is_locked, 1);
	console_lock();
	return OK;
}

static int esc_mods_unlock_console(struct mods_client *client)
{
	if (!atomic_cmpxchg(&client->console_is_locked, 1, 0)) {
		cl_error("console is not locked by this client\n");
		return -EINVAL;
	}

	console_unlock();
	atomic_set(&console_is_locked, 0);
	return OK;
}

static int esc_mods_suspend_console(struct mods_client *client)
{
	int err = -EINVAL;
	int other_id;

	LOG_ENT();

	other_id = atomic_cmpxchg(&console_suspend_client_id, 0, client->client_id);
	if (other_id) {
		if (other_id == client->client_id)
			cl_error("console already suspended by this client\n");
		else
			cl_error("console already suspended by client %u\n", other_id);
		LOG_EXT();
		return -EINVAL;
	}

	if (atomic_cmpxchg(&console_is_locked, 0, 1)) {
		atomic_set(&console_suspend_client_id, 0);
		cl_error("cannot suspend console, console is locked\n");
		LOG_EXT();
		return -EINVAL;
	}

	err = suspend_fb(client);

#if defined(MODS_HAS_CONSOLE_BINDING)
	if (&vga_con == vc_cons[fg_console].d->vc_sw) {
		/* if the current console is the vga console driver,
		 * have the dummy driver take over.
		 */
		console_lock();
		do_take_over_console(&dummy_con, 0, 0, 0);
		console_unlock();
		err = OK;

		cl_info("switched console to dummy\n");
	}
#endif

	if (err) {
		atomic_set(&console_suspend_client_id, 0);
		cl_warn("no methods to suspend console available\n");
	}

	atomic_set(&console_is_locked, 0);

	LOG_EXT();

	return err;
}

static int esc_mods_resume_console(struct mods_client *client)
{
	if (atomic_read(&console_suspend_client_id) != client->client_id) {
		cl_error("console was not suspended by this client\n");
		return -EINVAL;
	}

	return mods_resume_console(client);
}

static int mods_resume_console(struct mods_client *client)
{
	bool need_lock = true;
	int  err       = -EINVAL;

	LOG_ENT();

	if (atomic_cmpxchg(&client->console_is_locked, 1, 0)) {
		cl_warn("console was not properly unlocked\n");
		console_unlock();
		need_lock = false;
	}

	/* If we got here on close(), check if this client suspended the console. */
	if (atomic_read(&console_suspend_client_id) != client->client_id) {
		if (!need_lock)
			atomic_set(&console_is_locked, 0);
		LOG_EXT();
		return -EINVAL;
	}

	if (need_lock && atomic_cmpxchg(&console_is_locked, 0, 1)) {
		cl_error("cannot resume console, console is locked\n");
		LOG_EXT();
		return -EINVAL;
	}

	/* Another thread resumed the console before we took the lock */
	if (atomic_read(&console_suspend_client_id) != client->client_id) {
		atomic_set(&console_is_locked, 0);
		LOG_EXT();
		return OK;
	}

	err = resume_fb(client);

#if defined(MODS_HAS_CONSOLE_BINDING)
	if (&dummy_con == vc_cons[fg_console].d->vc_sw) {
		/* try to unbind the dummy driver,
		 * the system driver should take over.
		 */
		console_lock();
		do_unbind_con_driver(vc_cons[fg_console].d->vc_sw, 0, 0, 0);
		console_unlock();
		err = OK;

		cl_info("restored vga console\n");
	}
#endif
	atomic_set(&console_is_locked, 0);
	atomic_set(&console_suspend_client_id, 0);

	LOG_EXT();

	return err;
}
#endif

static int esc_mods_acquire_access_token(struct mods_client       *client,
					 struct MODS_ACCESS_TOKEN *ptoken)
{
	int err = -EINVAL;

	LOG_ENT();

	if (mods_get_multi_instance()) {
		cl_error(
			"access token ops not supported with multi_instance=1\n");
		LOG_EXT();
		return err;
	}

	get_random_bytes(&ptoken->token, sizeof(ptoken->token));
	err = mods_set_access_token(ptoken->token);
	if (err)
		cl_error("unable to set access token\n");
	else {
		cl_info("set access token %u\n", ptoken->token);
		client->access_token = ptoken->token;
	}

	LOG_EXT();

	return err;
}

static int esc_mods_release_access_token(struct mods_client       *client,
					 struct MODS_ACCESS_TOKEN *ptoken)
{
	int err = -EINVAL;

	LOG_ENT();

	if (mods_get_multi_instance()) {
		cl_error(
			"access token ops not supported with multi_instance=1\n");
		LOG_EXT();
		return err;
	}

	err = mods_set_access_token(MODS_ACCESS_TOKEN_NONE);
	if (err)
		cl_error("unable to clear access token\n");
	else {
		cl_info("released access token %u\n", client->access_token);
		client->access_token = MODS_ACCESS_TOKEN_NONE;
	}

	LOG_EXT();

	return err;
}

static int esc_mods_verify_access_token(struct mods_client       *client,
					struct MODS_ACCESS_TOKEN *ptoken)
{
	int err = -EINVAL;

	LOG_ENT();

	if (ptoken->token == mods_get_access_token()) {
		client->access_token = ptoken->token;
		err = OK;
	} else
		cl_error("invalid access token %u\n", client->access_token);

	LOG_EXT();

	return err;
}

struct mods_file_work {
	struct work_struct work;
	const char        *path;
	const char        *data;
	u32                data_size;
	ssize_t            err;
};

static void sysfs_write_task(struct work_struct *w)
{
	struct mods_file_work *task = container_of(w,
						   struct mods_file_work,
						   work);
	struct file *f;

	LOG_ENT();

	task->err = -EINVAL;

	f = filp_open(task->path, O_WRONLY, 0);
	if (IS_ERR(f))
		task->err = PTR_ERR(f);
	else {
#ifndef MODS_HAS_KERNEL_WRITE
		mm_segment_t old_fs = get_fs();
#endif

		f->f_pos = 0;
#ifdef MODS_HAS_KERNEL_WRITE
		task->err = kernel_write(f,
					 task->data,
					 task->data_size,
					 &f->f_pos);
#else
		set_fs(KERNEL_DS);

		task->err = vfs_write(f,
				      (__force const char __user *)task->data,
				      task->data_size,
				      &f->f_pos);

		set_fs(old_fs);
#endif
		filp_close(f, NULL);
	}

	LOG_EXT();
}

static int create_work_queue(struct mods_client *client)
{
	int err = 0;

	if (unlikely(mutex_lock_interruptible(&client->mtx)))
		return -EINTR;

	if (!client->work_queue) {
		client->work_queue = create_singlethread_workqueue("mods_wq");
		if (!client->work_queue) {
			cl_error("failed to create work queue\n");
			err = -ENOMEM;
		}
	}

	mutex_unlock(&client->mtx);

	return err;
}

static int run_write_task(struct mods_client    *client,
			  struct mods_file_work *task)
{
	int err = create_work_queue(client);

	if (err)
		return err;

	cl_info("write %.*s to %s\n", task->data_size, task->data, task->path);

	INIT_WORK(&task->work, sysfs_write_task);
	queue_work(client->work_queue, &task->work);
	flush_workqueue(client->work_queue);

	if (task->err < 0)
		cl_error("failed to write %.*s to %s\n",
			 task->data_size, task->data, task->path);

	return (task->err > 0) ? 0 : (int)task->err;
}

static int esc_mods_write_sysfs_node(struct mods_client     *client,
				     struct MODS_SYSFS_NODE *pdata)
{
	int                   err;
	struct mods_file_work task;

	LOG_ENT();

	if (pdata->size > MODS_MAX_SYSFS_FILE_SIZE) {
		cl_error("invalid data size %u, max allowed is %u\n",
			 pdata->size, MODS_MAX_SYSFS_FILE_SIZE);
		LOG_EXT();
		return -EINVAL;
	}

	memmove(&pdata->path[5], pdata->path, sizeof(pdata->path) - 5);
	memcpy(pdata->path, "/sys/", 5);
	pdata->path[sizeof(pdata->path) - 1] = 0;

	memset(&task, 0, sizeof(task));
	task.path      = pdata->path;
	task.data      = pdata->contents;
	task.data_size = pdata->size;

	err = run_write_task(client, &task);

	LOG_EXT();
	return err;
}

static int esc_mods_sysctl_write_int(struct mods_client     *client,
				     struct MODS_SYSCTL_INT *pdata)
{
	int                   err;
	struct mods_file_work task;
	char                  data[21];
	int                   data_size;

	LOG_ENT();

	memmove(&pdata->path[10], pdata->path, sizeof(pdata->path)  - 10);
	memcpy(pdata->path, "/proc/sys/", 10);
	pdata->path[sizeof(pdata->path) - 1] = 0;

	data_size = snprintf(data, sizeof(data),
			     "%lld", (long long)pdata->value);

	if (unlikely(data_size < 0)) {
		err = data_size;
		goto error;
	}

	memset(&task, 0, sizeof(task));
	task.path      = pdata->path;
	task.data      = data;
	task.data_size = data_size;

	err = run_write_task(client, &task);

error:
	LOG_EXT();
	return err;
}

#ifdef CONFIG_X86
static int esc_mods_read_msr(struct mods_client *client, struct MODS_MSR *p)
{
	int err = -EINVAL;

	LOG_ENT();

	err = rdmsr_safe_on_cpu(p->cpu_num, p->reg, &p->low, &p->high);
	if (err)
		cl_error("could not read MSR %u\n", p->reg);

	LOG_EXT();
	return err;
}

static int esc_mods_write_msr(struct mods_client *client, struct MODS_MSR *p)
{
	int err = -EINVAL;

	LOG_ENT();

	err = wrmsr_safe_on_cpu(p->cpu_num, p->reg, p->low, p->high);
	if (err)
		cl_error("could not write MSR %u\n", p->reg);

	LOG_EXT();
	return err;
}
#endif

#if defined(CONFIG_ARM) || defined(CONFIG_ARM64)
static int esc_mods_idle(struct mods_client *client, struct MODS_IDLE *p)
{
	u32 i;

	LOG_ENT();

	switch (p->idle_method) {

	case MODS_IDLE_METHOD_ARM_WFI:
		dsb(st);
		for (i = 0; i < p->num_loops; i++)
			wfi();
		break;

	case MODS_IDLE_METHOD_ARM_WFE:
		dsb(st);
		for (i = 0; i < p->num_loops; i++)
			wfe();
		break;

	default:
		cl_error("unsupported idle method %u\n", p->idle_method);
		LOG_EXT();
		return -EINVAL;
	}

	LOG_EXT();
	return OK;
}
#endif

static int esc_mods_get_driver_stats(struct mods_client *client,
				     struct MODS_GET_DRIVER_STATS *p)
{
	int num_allocs;
	int num_pages;

	LOG_ENT();

	num_allocs = atomic_read(&client->num_allocs);
	num_pages  = atomic_read(&client->num_pages);

	memset(p, 0, sizeof(*p));
	p->version    = MODS_DRIVER_STATS_VERSION;
	p->num_allocs = (num_allocs < 0) ? ~0U : num_allocs;
	p->num_pages  = (num_pages  < 0) ? ~0U : num_pages;

	LOG_EXT();
	return 0;
}

/**************
 * IO control *
 **************/

static long mods_krnl_ioctl(struct file  *fp,
			    unsigned int  cmd,
			    unsigned long i_arg)
{
	int                 err      = 0;
	void               *arg_copy = NULL;
	void        __user *arg      = (void __user *)i_arg;
	struct mods_client *client   = fp->private_data;
	int                 arg_size;
	char                buf[64];

	LOG_ENT();

	if (!validate_client(client)) {
		LOG_EXT();
		return -EINVAL;
	}

	if ((cmd != MODS_ESC_VERIFY_ACCESS_TOKEN) &&
	    (cmd != MODS_ESC_GET_API_VERSION)) {
		err = mods_check_access_token(client);
		if (err) {
			LOG_EXT();
			return err;
		}
	}

	arg_size = _IOC_SIZE(cmd);

	if (arg_size > (int)sizeof(buf)) {
		arg_copy = kzalloc(arg_size, GFP_KERNEL | __GFP_NORETRY);
		if (unlikely(!arg_copy)) {
			LOG_EXT();
			return -ENOMEM;
		}
		atomic_inc(&client->num_allocs);
	} else if (arg_size > 0)
		arg_copy = buf;

	if ((arg_size > 0) && copy_from_user(arg_copy, arg, arg_size)) {
		cl_error("failed to copy ioctl data\n");
		if (arg_size > (int)sizeof(buf)) {
			kfree(arg_copy);
			atomic_dec(&client->num_allocs);
		}
		LOG_EXT();
		return -EFAULT;
	}

#define MODS_IOCTL(code, function, argtype)\
	({\
	do {\
		cl_debug(DEBUG_IOCTL, "ioctl(" #code ")\n");\
		if (arg_size != sizeof(struct argtype)) {\
			err = -EINVAL;\
			cl_error("invalid parameter passed to ioctl " #code\
				 "\n");\
		} else {\
			err = function(client, (struct argtype *)arg_copy);\
			if ((err == OK) && \
			    copy_to_user(arg, arg_copy, arg_size)) {\
				err = -EFAULT;\
				cl_error("copying return value for ioctl " \
					 #code " to user space failed\n");\
			} \
		} \
	} while (0);\
	})

#define MODS_IOCTL_NORETVAL(code, function, argtype)\
	({\
	do {\
		cl_debug(DEBUG_IOCTL, "ioctl(" #code ")\n");\
		if (arg_size != sizeof(struct argtype)) {\
			err = -EINVAL;\
			cl_error("invalid parameter passed to ioctl " #code\
				 "\n");\
		} else {\
			err = function(client, (struct argtype *)arg_copy);\
		} \
	} while (0);\
	})

#define MODS_IOCTL_VOID(code, function)\
	({\
	do {\
		cl_debug(DEBUG_IOCTL, "ioctl(" #code ")\n");\
		if (arg_size != 0) {\
			err = -EINVAL;\
			cl_error("invalid parameter passed to ioctl " #code\
				 "\n");\
		} else {\
			err = function(client);\
		} \
	} while (0);\
	})

	switch (cmd) {

#ifdef CONFIG_PCI
	case MODS_ESC_FIND_PCI_DEVICE:
		MODS_IOCTL(MODS_ESC_FIND_PCI_DEVICE,
			   esc_mods_find_pci_dev, MODS_FIND_PCI_DEVICE);
		break;

	case MODS_ESC_FIND_PCI_DEVICE_2:
		MODS_IOCTL(MODS_ESC_FIND_PCI_DEVICE_2,
			   esc_mods_find_pci_dev_2,
			   MODS_FIND_PCI_DEVICE_2);
		break;

	case MODS_ESC_FIND_PCI_CLASS_CODE:
		MODS_IOCTL(MODS_ESC_FIND_PCI_CLASS_CODE,
			   esc_mods_find_pci_class_code,
			   MODS_FIND_PCI_CLASS_CODE);
		break;

	case MODS_ESC_FIND_PCI_CLASS_CODE_2:
		MODS_IOCTL(MODS_ESC_FIND_PCI_CLASS_CODE_2,
			   esc_mods_find_pci_class_code_2,
			   MODS_FIND_PCI_CLASS_CODE_2);
		break;

	case MODS_ESC_PCI_GET_BAR_INFO:
		MODS_IOCTL(MODS_ESC_PCI_GET_BAR_INFO,
			   esc_mods_pci_get_bar_info,
			   MODS_PCI_GET_BAR_INFO);
		break;

	case MODS_ESC_PCI_GET_BAR_INFO_2:
		MODS_IOCTL(MODS_ESC_PCI_GET_BAR_INFO_2,
			   esc_mods_pci_get_bar_info_2,
			   MODS_PCI_GET_BAR_INFO_2);
		break;

	case MODS_ESC_PCI_GET_IRQ:
		MODS_IOCTL(MODS_ESC_PCI_GET_IRQ,
			   esc_mods_pci_get_irq,
			   MODS_PCI_GET_IRQ);
		break;

	case MODS_ESC_PCI_GET_IRQ_2:
		MODS_IOCTL(MODS_ESC_PCI_GET_IRQ_2,
			   esc_mods_pci_get_irq_2,
			   MODS_PCI_GET_IRQ_2);
		break;

	case MODS_ESC_PCI_READ:
		MODS_IOCTL(MODS_ESC_PCI_READ, esc_mods_pci_read, MODS_PCI_READ);
		break;

	case MODS_ESC_PCI_READ_2:
		MODS_IOCTL(MODS_ESC_PCI_READ_2,
			   esc_mods_pci_read_2, MODS_PCI_READ_2);
		break;

	case MODS_ESC_PCI_WRITE:
		MODS_IOCTL_NORETVAL(MODS_ESC_PCI_WRITE,
				    esc_mods_pci_write, MODS_PCI_WRITE);
		break;

	case MODS_ESC_PCI_WRITE_2:
		MODS_IOCTL_NORETVAL(MODS_ESC_PCI_WRITE_2,
				    esc_mods_pci_write_2,
				    MODS_PCI_WRITE_2);
		break;

	case MODS_ESC_PCI_BUS_RESCAN:
		MODS_IOCTL_NORETVAL(MODS_ESC_PCI_BUS_RESCAN,
				    esc_mods_pci_bus_rescan,
				    MODS_PCI_BUS_RESCAN);
		break;

	case MODS_ESC_PCI_BUS_ADD_DEVICES:
		MODS_IOCTL_NORETVAL(MODS_ESC_PCI_BUS_ADD_DEVICES,
				    esc_mods_pci_bus_add_dev,
				    MODS_PCI_BUS_ADD_DEVICES);
		break;

	case MODS_ESC_PCI_BUS_REMOVE_DEV:
		MODS_IOCTL_NORETVAL(MODS_ESC_PCI_BUS_REMOVE_DEV,
			   esc_mods_pci_bus_remove_dev,
			   MODS_PCI_BUS_REMOVE_DEV);
		break;

	case MODS_ESC_PIO_READ:
		MODS_IOCTL(MODS_ESC_PIO_READ,
			   esc_mods_pio_read, MODS_PIO_READ);
		break;

	case MODS_ESC_PIO_WRITE:
		MODS_IOCTL_NORETVAL(MODS_ESC_PIO_WRITE,
				    esc_mods_pio_write, MODS_PIO_WRITE);
		break;

	case MODS_ESC_DEVICE_NUMA_INFO:
		MODS_IOCTL(MODS_ESC_DEVICE_NUMA_INFO,
			   esc_mods_device_numa_info,
			   MODS_DEVICE_NUMA_INFO);
		break;

	case MODS_ESC_DEVICE_NUMA_INFO_2:
		MODS_IOCTL(MODS_ESC_DEVICE_NUMA_INFO_2,
			   esc_mods_device_numa_info_2,
			   MODS_DEVICE_NUMA_INFO_2);
		break;

	case MODS_ESC_DEVICE_NUMA_INFO_3:
		MODS_IOCTL(MODS_ESC_DEVICE_NUMA_INFO_3,
			   esc_mods_device_numa_info_3,
			   MODS_DEVICE_NUMA_INFO_3);
		break;

	case MODS_ESC_GET_IOMMU_STATE:
		MODS_IOCTL(MODS_ESC_GET_IOMMU_STATE,
			   esc_mods_get_iommu_state,
			   MODS_GET_IOMMU_STATE);
		break;

	case MODS_ESC_GET_IOMMU_STATE_2:
		MODS_IOCTL(MODS_ESC_GET_IOMMU_STATE_2,
			   esc_mods_get_iommu_state_2,
			   MODS_GET_IOMMU_STATE);
		break;

	case MODS_ESC_PCI_SET_DMA_MASK:
		MODS_IOCTL(MODS_ESC_PCI_SET_DMA_MASK,
			   esc_mods_pci_set_dma_mask,
			   MODS_PCI_DMA_MASK);
		break;

	case MODS_ESC_PCI_RESET_FUNCTION:
		MODS_IOCTL(MODS_ESC_PCI_RESET_FUNCTION,
			   esc_mods_pci_reset_function,
			   mods_pci_dev_2);
		break;

#ifdef MODS_HAS_DEV_PROPS
	case MODS_ESC_READ_DEV_PROPERTY:
		MODS_IOCTL(MODS_ESC_READ_DEV_PROPERTY,
			   esc_mods_read_dev_property,
			   MODS_READ_DEV_PROPERTY);
		break;
#endif
#endif

	case MODS_ESC_ALLOC_PAGES:
		MODS_IOCTL(MODS_ESC_ALLOC_PAGES,
			   esc_mods_alloc_pages, MODS_ALLOC_PAGES);
		break;

	case MODS_ESC_DEVICE_ALLOC_PAGES:
		MODS_IOCTL(MODS_ESC_DEVICE_ALLOC_PAGES,
			   esc_mods_device_alloc_pages,
			   MODS_DEVICE_ALLOC_PAGES);
		break;

	case MODS_ESC_DEVICE_ALLOC_PAGES_2:
		MODS_IOCTL(MODS_ESC_DEVICE_ALLOC_PAGES_2,
			   esc_mods_device_alloc_pages_2,
			   MODS_DEVICE_ALLOC_PAGES_2);
		break;

	case MODS_ESC_ALLOC_PAGES_2:
		MODS_IOCTL(MODS_ESC_ALLOC_PAGES_2,
			   esc_mods_alloc_pages_2,
			   MODS_ALLOC_PAGES_2);
		break;

	case MODS_ESC_FREE_PAGES:
		MODS_IOCTL(MODS_ESC_FREE_PAGES,
			   esc_mods_free_pages, MODS_FREE_PAGES);
		break;

	case MODS_ESC_MERGE_PAGES:
		MODS_IOCTL(MODS_ESC_MERGE_PAGES,
			   esc_mods_merge_pages, MODS_MERGE_PAGES);
		break;

	case MODS_ESC_RESERVE_ALLOCATION:
		MODS_IOCTL(MODS_ESC_RESERVE_ALLOCATION,
			   esc_mods_reserve_allocation,
			   MODS_RESERVE_ALLOCATION);
		break;

	case MODS_ESC_GET_RESERVED_ALLOCATION:
		MODS_IOCTL(MODS_ESC_GET_RESERVED_ALLOCATION,
			   esc_mods_get_reserved_allocation,
			   MODS_RESERVE_ALLOCATION);
		break;

	case MODS_ESC_RELEASE_RESERVED_ALLOCATION:
		MODS_IOCTL(MODS_ESC_RELEASE_RESERVED_ALLOCATION,
			   esc_mods_release_reserved_allocation,
			   MODS_RESERVE_ALLOCATION);
		break;

	case MODS_ESC_GET_PHYSICAL_ADDRESS:
		MODS_IOCTL(MODS_ESC_GET_PHYSICAL_ADDRESS,
			   esc_mods_get_phys_addr,
			   MODS_GET_PHYSICAL_ADDRESS);
		break;

	case MODS_ESC_GET_PHYSICAL_ADDRESS_2:
		MODS_IOCTL(MODS_ESC_GET_PHYSICAL_ADDRESS_2,
			   esc_mods_get_phys_addr_2,
			   MODS_GET_PHYSICAL_ADDRESS_3);
		break;

	case MODS_ESC_GET_MAPPED_PHYSICAL_ADDRESS:
		MODS_IOCTL(MODS_ESC_GET_MAPPED_PHYSICAL_ADDRESS,
			   esc_mods_get_mapped_phys_addr,
			   MODS_GET_PHYSICAL_ADDRESS);
		break;

	case MODS_ESC_GET_MAPPED_PHYSICAL_ADDRESS_2:
		MODS_IOCTL(MODS_ESC_GET_MAPPED_PHYSICAL_ADDRESS_2,
			   esc_mods_get_mapped_phys_addr_2,
			   MODS_GET_PHYSICAL_ADDRESS_2);
		break;

	case MODS_ESC_GET_MAPPED_PHYSICAL_ADDRESS_3:
		MODS_IOCTL(MODS_ESC_GET_MAPPED_PHYSICAL_ADDRESS_3,
			   esc_mods_get_mapped_phys_addr_3,
			   MODS_GET_PHYSICAL_ADDRESS_3);
		break;

	case MODS_ESC_SET_MEMORY_TYPE:
		MODS_IOCTL_NORETVAL(MODS_ESC_SET_MEMORY_TYPE,
				    esc_mods_set_mem_type,
				    MODS_MEMORY_TYPE);
		break;

	case MODS_ESC_VIRTUAL_TO_PHYSICAL:
		MODS_IOCTL(MODS_ESC_VIRTUAL_TO_PHYSICAL,
			   esc_mods_virtual_to_phys,
			   MODS_VIRTUAL_TO_PHYSICAL);
		break;

	case MODS_ESC_PHYSICAL_TO_VIRTUAL:
		MODS_IOCTL(MODS_ESC_PHYSICAL_TO_VIRTUAL,
			   esc_mods_phys_to_virtual, MODS_PHYSICAL_TO_VIRTUAL);
		break;

#if defined(CONFIG_PPC64)
	case MODS_ESC_PCI_HOT_RESET:
		MODS_IOCTL_NORETVAL(MODS_ESC_PCI_HOT_RESET,
				    esc_mods_pci_hot_reset,
				    MODS_PCI_HOT_RESET);
		break;

	case MODS_ESC_SET_PPC_TCE_BYPASS:
		MODS_IOCTL(MODS_ESC_SET_PPC_TCE_BYPASS,
			   esc_mods_set_ppc_tce_bypass,
			   MODS_SET_PPC_TCE_BYPASS);
		break;

	case MODS_ESC_GET_ATS_ADDRESS_RANGE:
		MODS_IOCTL(MODS_ESC_GET_ATS_ADDRESS_RANGE,
			   esc_mods_get_ats_address_range,
			   MODS_GET_ATS_ADDRESS_RANGE);
		break;

	case MODS_ESC_SET_NVLINK_SYSMEM_TRAINED:
		MODS_IOCTL(MODS_ESC_SET_NVLINK_SYSMEM_TRAINED,
			   esc_mods_set_nvlink_sysmem_trained,
			   MODS_SET_NVLINK_SYSMEM_TRAINED);
		break;

	case MODS_ESC_GET_NVLINK_LINE_RATE:
		MODS_IOCTL(MODS_ESC_GET_NVLINK_LINE_RATE,
			   esc_mods_get_nvlink_line_rate,
			   MODS_GET_NVLINK_LINE_RATE);
		break;
#endif

#ifdef CONFIG_PCI
	case MODS_ESC_DMA_MAP_MEMORY:
		MODS_IOCTL(MODS_ESC_DMA_MAP_MEMORY,
			   esc_mods_dma_map_memory,
			   MODS_DMA_MAP_MEMORY);
		break;

	case MODS_ESC_DMA_UNMAP_MEMORY:
		MODS_IOCTL(MODS_ESC_DMA_UNMAP_MEMORY,
			   esc_mods_dma_unmap_memory,
			   MODS_DMA_MAP_MEMORY);
		break;
#endif

	case MODS_ESC_IRQ_REGISTER:
	case MODS_ESC_MSI_REGISTER:
		err = -EINVAL;
		break;

#if defined(MODS_HAS_TEGRA) && defined(CONFIG_OF) && defined(CONFIG_OF_IRQ)
	case MODS_ESC_MAP_INTERRUPT:
		MODS_IOCTL(MODS_ESC_MAP_INTERRUPT,
				esc_mods_map_irq, MODS_DT_INFO);
		break;

	case MODS_ESC_MAP_GPIO:
		MODS_IOCTL(MODS_ESC_MAP_GPIO,
		esc_mods_map_irq_to_gpio, MODS_GPIO_INFO);
		break;
#endif

	case MODS_ESC_REGISTER_IRQ:
		MODS_IOCTL_NORETVAL(MODS_ESC_REGISTER_IRQ,
				esc_mods_register_irq, MODS_REGISTER_IRQ);
		break;

	case MODS_ESC_REGISTER_IRQ_2:
		MODS_IOCTL_NORETVAL(MODS_ESC_REGISTER_IRQ_2,
				esc_mods_register_irq_2, MODS_REGISTER_IRQ_2);
		break;

	case MODS_ESC_REGISTER_IRQ_3:
		MODS_IOCTL_NORETVAL(MODS_ESC_REGISTER_IRQ_3,
				esc_mods_register_irq_3, MODS_REGISTER_IRQ_3);
		break;

	case MODS_ESC_UNREGISTER_IRQ:
		MODS_IOCTL_NORETVAL(MODS_ESC_UNREGISTER_IRQ,
				    esc_mods_unregister_irq, MODS_REGISTER_IRQ);
		break;

	case MODS_ESC_UNREGISTER_IRQ_2:
		MODS_IOCTL_NORETVAL(MODS_ESC_UNREGISTER_IRQ_2,
				    esc_mods_unregister_irq_2,
				    MODS_REGISTER_IRQ_2);
		break;

	case MODS_ESC_QUERY_IRQ:
		MODS_IOCTL(MODS_ESC_QUERY_IRQ,
			   esc_mods_query_irq, MODS_QUERY_IRQ);
		break;

	case MODS_ESC_QUERY_IRQ_2:
		MODS_IOCTL(MODS_ESC_QUERY_IRQ_2,
			   esc_mods_query_irq_2, MODS_QUERY_IRQ_2);
		break;

	case MODS_ESC_IRQ_HANDLED:
		MODS_IOCTL_NORETVAL(MODS_ESC_IRQ_HANDLED,
				    esc_mods_irq_handled, MODS_REGISTER_IRQ);
		break;

	case MODS_ESC_IRQ_HANDLED_2:
		MODS_IOCTL_NORETVAL(MODS_ESC_IRQ_HANDLED_2,
				    esc_mods_irq_handled_2,
				    MODS_REGISTER_IRQ_2);
		break;

#ifdef CONFIG_ACPI
	case MODS_ESC_EVAL_ACPI_METHOD:
		MODS_IOCTL(MODS_ESC_EVAL_ACPI_METHOD,
			   esc_mods_eval_acpi_method, MODS_EVAL_ACPI_METHOD);
		break;

	case MODS_ESC_EVAL_DEV_ACPI_METHOD:
		MODS_IOCTL(MODS_ESC_EVAL_DEV_ACPI_METHOD,
			   esc_mods_eval_dev_acpi_method,
			   MODS_EVAL_DEV_ACPI_METHOD);
		break;

	case MODS_ESC_EVAL_DEV_ACPI_METHOD_2:
		MODS_IOCTL(MODS_ESC_EVAL_DEV_ACPI_METHOD_2,
			   esc_mods_eval_dev_acpi_method_2,
			   MODS_EVAL_DEV_ACPI_METHOD_2);
		break;

	case MODS_ESC_EVAL_DEV_ACPI_METHOD_3:
		MODS_IOCTL(MODS_ESC_EVAL_DEV_ACPI_METHOD_3,
			   esc_mods_eval_dev_acpi_method_3,
			   MODS_EVAL_DEV_ACPI_METHOD_3);
		break;

	case MODS_ESC_ACPI_GET_DDC:
		MODS_IOCTL(MODS_ESC_ACPI_GET_DDC,
			   esc_mods_acpi_get_ddc, MODS_ACPI_GET_DDC);
		break;

	case MODS_ESC_ACPI_GET_DDC_2:
		MODS_IOCTL(MODS_ESC_ACPI_GET_DDC_2,
			   esc_mods_acpi_get_ddc_2, MODS_ACPI_GET_DDC_2);
		break;

	case MODS_ESC_GET_ACPI_DEV_CHILDREN:
		MODS_IOCTL(MODS_ESC_GET_ACPI_DEV_CHILDREN,
			   esc_mods_get_acpi_dev_children,
			   MODS_GET_ACPI_DEV_CHILDREN);
		break;

#ifdef MODS_HAS_PXM_TO_NODE
	case MODS_ESC_PROXIMITY_TO_NUMA_NODE:
		MODS_IOCTL(MODS_ESC_PROXIMITY_TO_NUMA_NODE,
			   esc_mods_proximity_to_numa_node,
			   MODS_PROXIMITY_TO_NUMA_NODE);
		break;
#endif
#else
	case MODS_ESC_EVAL_ACPI_METHOD:
		/* fallthrough */
	case MODS_ESC_EVAL_DEV_ACPI_METHOD:
		/* fallthrough */
	case MODS_ESC_EVAL_DEV_ACPI_METHOD_2:
		/* fallthrough */
	case MODS_ESC_EVAL_DEV_ACPI_METHOD_3:
		/* fallthrough */
	case MODS_ESC_ACPI_GET_DDC:
		/* fallthrough */
	case MODS_ESC_ACPI_GET_DDC_2:
		/* fallthrough */
	case MODS_ESC_GET_ACPI_DEV_CHILDREN:
		/* Silent failure to avoid clogging kernel log */
		err = -EINVAL;
		break;
#endif
	case MODS_ESC_GET_API_VERSION:
		MODS_IOCTL(MODS_ESC_GET_API_VERSION,
			   esc_mods_get_api_version, MODS_GET_VERSION);
		break;

	case MODS_ESC_GET_KERNEL_VERSION:
		MODS_IOCTL(MODS_ESC_GET_KERNEL_VERSION,
			   esc_mods_get_kernel_version, MODS_GET_VERSION);
		break;

#if defined(MODS_HAS_TEGRA) && defined(CONFIG_COMMON_CLK)
	case MODS_ESC_GET_CLOCK_HANDLE:
		MODS_IOCTL(MODS_ESC_GET_CLOCK_HANDLE,
			   esc_mods_get_clock_handle, MODS_GET_CLOCK_HANDLE);
		break;

	case MODS_ESC_SET_CLOCK_RATE:
		MODS_IOCTL_NORETVAL(MODS_ESC_SET_CLOCK_RATE,
				    esc_mods_set_clock_rate, MODS_CLOCK_RATE);
		break;

	case MODS_ESC_GET_CLOCK_RATE:
		MODS_IOCTL(MODS_ESC_GET_CLOCK_RATE,
			   esc_mods_get_clock_rate, MODS_CLOCK_RATE);
		break;

	case MODS_ESC_GET_CLOCK_MAX_RATE:
		MODS_IOCTL(MODS_ESC_GET_CLOCK_MAX_RATE,
			   esc_mods_get_clock_max_rate, MODS_CLOCK_RATE);
		break;

	case MODS_ESC_SET_CLOCK_MAX_RATE:
		MODS_IOCTL_NORETVAL(MODS_ESC_SET_CLOCK_MAX_RATE,
				    esc_mods_set_clock_max_rate,
				    MODS_CLOCK_RATE);
		break;

	case MODS_ESC_SET_CLOCK_PARENT:
		MODS_IOCTL_NORETVAL(MODS_ESC_SET_CLOCK_PARENT,
				    esc_mods_set_clock_parent,
				    MODS_CLOCK_PARENT);
		break;

	case MODS_ESC_GET_CLOCK_PARENT:
		MODS_IOCTL(MODS_ESC_GET_CLOCK_PARENT,
			   esc_mods_get_clock_parent, MODS_CLOCK_PARENT);
		break;

	case MODS_ESC_ENABLE_CLOCK:
		MODS_IOCTL_NORETVAL(MODS_ESC_ENABLE_CLOCK,
				    esc_mods_enable_clock, MODS_CLOCK_HANDLE);
		break;

	case MODS_ESC_DISABLE_CLOCK:
		MODS_IOCTL_NORETVAL(MODS_ESC_DISABLE_CLOCK,
				    esc_mods_disable_clock, MODS_CLOCK_HANDLE);
		break;

	case MODS_ESC_IS_CLOCK_ENABLED:
		MODS_IOCTL(MODS_ESC_IS_CLOCK_ENABLED,
			   esc_mods_is_clock_enabled, MODS_CLOCK_ENABLED);
		break;

	case MODS_ESC_CLOCK_RESET_ASSERT:
		MODS_IOCTL_NORETVAL(MODS_ESC_CLOCK_RESET_ASSERT,
				    esc_mods_clock_reset_assert,
				    MODS_CLOCK_HANDLE);
		break;

	case MODS_ESC_CLOCK_RESET_DEASSERT:
		MODS_IOCTL_NORETVAL(MODS_ESC_CLOCK_RESET_DEASSERT,
				    esc_mods_clock_reset_deassert,
				    MODS_CLOCK_HANDLE);
		break;

	case MODS_ESC_RESET_ASSERT:
		MODS_IOCTL_NORETVAL(MODS_ESC_RESET_ASSERT,
				    esc_mods_reset_assert,
				    MODS_RESET_HANDLE);
		break;

	case MODS_ESC_GET_RESET_HANDLE:
		MODS_IOCTL(MODS_ESC_GET_RESET_HANDLE,
			   esc_mods_get_rst_handle,
			   MODS_GET_RESET_HANDLE);
		break;
#endif
#if defined(MODS_HAS_TEGRA)
	case MODS_ESC_BPMP_SET_PCIE_STATE:
		MODS_IOCTL(MODS_ESC_BPMP_SET_PCIE_STATE,
			   esc_mods_bpmp_set_pcie_state,
			   MODS_SET_PCIE_STATE);
		break;

	case MODS_ESC_BPMP_INIT_PCIE_EP_PLL:
		MODS_IOCTL(MODS_ESC_BPMP_INIT_PCIE_EP_PLL,
			   esc_mods_bpmp_init_pcie_ep_pll,
			   MODS_INIT_PCIE_EP_PLL);
		break;
	case MODS_ESC_DMA_ALLOC_COHERENT:
		MODS_IOCTL(MODS_ESC_DMA_ALLOC_COHERENT,
			   esc_mods_dma_alloc_coherent,
			   MODS_DMA_COHERENT_MEM_HANDLE);
		break;
	case MODS_ESC_DMA_FREE_COHERENT:
		MODS_IOCTL(MODS_ESC_DMA_FREE_COHERENT,
			   esc_mods_dma_free_coherent,
			   MODS_DMA_COHERENT_MEM_HANDLE);
		break;
	case MODS_ESC_DMA_COPY_TO_USER:
		MODS_IOCTL(MODS_ESC_DMA_COPY_TO_USER,
			   esc_mods_dma_copy_to_user,
			   MODS_DMA_COPY_TO_USER);
		break;
	case MODS_ESC_IOMMU_DMA_MAP_MEMORY:
		MODS_IOCTL(MODS_ESC_IOMMU_DMA_MAP_MEMORY,
			   esc_mods_iommu_dma_map_memory,
			   MODS_IOMMU_DMA_MAP_MEMORY);
		break;

	case MODS_ESC_IOMMU_DMA_UNMAP_MEMORY:
		MODS_IOCTL(MODS_ESC_IOMMU_DMA_UNMAP_MEMORY,
			   esc_mods_iommu_dma_unmap_memory,
			   MODS_IOMMU_DMA_MAP_MEMORY);
		break;
#if defined(CONFIG_DMA_ENGINE)
	case MODS_ESC_DMA_REQUEST_HANDLE:
		MODS_IOCTL(MODS_ESC_DMA_REQUEST_HANDLE,
			   esc_mods_dma_request_channel,
			   MODS_DMA_HANDLE);
		break;
	case MODS_ESC_DMA_REQUEST_HANDLE_2:
		MODS_IOCTL(MODS_ESC_DMA_REQUEST_HANDLE_2,
			   esc_mods_dma_request_channel_2,
			   MODS_DMA_HANDLE_2);
		break;
	case MODS_ESC_DMA_RELEASE_HANDLE:
		MODS_IOCTL_NORETVAL(MODS_ESC_DMA_RELEASE_HANDLE,
			   esc_mods_dma_release_channel,
			   MODS_DMA_HANDLE);
		break;
	case MODS_ESC_DMA_ISSUE_PENDING:
		MODS_IOCTL_NORETVAL(MODS_ESC_DMA_ISSUE_PENDING,
				    esc_mods_dma_async_issue_pending,
				    MODS_DMA_HANDLE);
		break;
	case MODS_ESC_DMA_SET_CONFIG:
		MODS_IOCTL_NORETVAL(MODS_ESC_DMA_SET_CONFIG,
				    esc_mods_dma_set_config,
				    MODS_DMA_CHANNEL_CONFIG);
		break;
	case MODS_ESC_DMA_TX_SUBMIT:
		MODS_IOCTL(MODS_ESC_DMA_TX_SUBMIT,
			   esc_mods_dma_submit_request,
			   MODS_DMA_TX_DESC);
		break;
	case MODS_ESC_DMA_TX_WAIT:
		MODS_IOCTL(MODS_MODS_ESC_DMA_TX_WAIT,
			   esc_mods_dma_wait,
			   MODS_DMA_WAIT_DESC);
		break;
#endif
#if defined(MODS_HAS_TEGRA) && defined(CONFIG_NET)
	case MODS_ESC_NET_FORCE_LINK:
		MODS_IOCTL(MODS_ESC_NET_FORCE_LINK,
			   esc_mods_net_force_link, MODS_NET_DEVICE_NAME);
		break;
#endif
#endif
#ifdef CONFIG_ARM
	case MODS_ESC_MEMORY_BARRIER:
		MODS_IOCTL_VOID(MODS_ESC_MEMORY_BARRIER,
				esc_mods_memory_barrier);
		break;
#endif
#ifdef CONFIG_ARM64
	case MODS_ESC_FLUSH_CPU_CACHE_RANGE:
		MODS_IOCTL_NORETVAL(MODS_ESC_FLUSH_CPU_CACHE_RANGE,
				    esc_mods_flush_cpu_cache_range,
				    MODS_FLUSH_CPU_CACHE_RANGE);
		break;
#endif
#if defined(CONFIG_ARM) || defined(CONFIG_ARM64)
	case MODS_ESC_IDLE:
		MODS_IOCTL_NORETVAL(MODS_ESC_IDLE, esc_mods_idle, MODS_IDLE);
		break;
#endif
#if defined(MODS_HAS_TEGRA) && defined(CONFIG_DMA_SHARED_BUFFER)
	case MODS_ESC_DMABUF_GET_PHYSICAL_ADDRESS:
		MODS_IOCTL(MODS_ESC_DMABUF_GET_PHYSICAL_ADDRESS,
			   esc_mods_dmabuf_get_phys_addr,
			   MODS_DMABUF_GET_PHYSICAL_ADDRESS);
		break;
#endif
#ifdef MODS_HAS_TEGRA
	case MODS_ESC_ADSP_LOAD:
		MODS_IOCTL_NORETVAL(MODS_ESC_ADSP_LOAD,
				esc_mods_adsp_load,
				MODS_ADSP_INIT_INFO);
		break;

	case MODS_ESC_ADSP_START:
		MODS_IOCTL_NORETVAL(MODS_ESC_ADSP_START,
				esc_mods_adsp_start,
				MODS_ADSP_INIT_INFO);
		break;

	case MODS_ESC_ADSP_STOP:
		MODS_IOCTL_NORETVAL(MODS_ESC_ADSP_STOP,
				esc_mods_adsp_stop,
				MODS_ADSP_INIT_INFO);
		break;

	case MODS_ESC_ADSP_RUN_APP:
		MODS_IOCTL_NORETVAL(MODS_ESC_ADSP_RUN_APP,
				    esc_mods_adsp_run_app,
				    MODS_ADSP_RUN_APP_INFO);
		break;
#endif

#ifdef CONFIG_X86
	case MODS_ESC_GET_SCREEN_INFO:
		MODS_IOCTL(MODS_ESC_GET_SCREEN_INFO,
			   esc_mods_get_screen_info, MODS_SCREEN_INFO);
		break;
	case MODS_ESC_GET_SCREEN_INFO_2:
		MODS_IOCTL(MODS_ESC_GET_SCREEN_INFO_2,
			   esc_mods_get_screen_info_2, MODS_SCREEN_INFO_2);
		break;
#endif

#if defined(MODS_HAS_CONSOLE_LOCK)
	case MODS_ESC_LOCK_CONSOLE:
		MODS_IOCTL_VOID(MODS_ESC_LOCK_CONSOLE,
			   esc_mods_lock_console);
		break;
	case MODS_ESC_UNLOCK_CONSOLE:
		MODS_IOCTL_VOID(MODS_ESC_UNLOCK_CONSOLE,
			   esc_mods_unlock_console);
		break;
	case MODS_ESC_SUSPEND_CONSOLE:
		MODS_IOCTL_VOID(MODS_ESC_SUSPEND_CONSOLE,
			   esc_mods_suspend_console);
		break;
	case MODS_ESC_RESUME_CONSOLE:
		MODS_IOCTL_VOID(MODS_ESC_RESUME_CONSOLE,
			   esc_mods_resume_console);
		break;
#endif

#if defined(MODS_HAS_TEGRA)

#ifdef CONFIG_TRUSTY
	case MODS_ESC_SEND_TZ_MSG:
		MODS_IOCTL(MODS_ESC_SEND_TZ_MSG,
			esc_mods_send_trustzone_msg, MODS_TZ_PARAMS);
		break;
#endif

#ifdef CONFIG_OPTEE
	case MODS_ESC_INVOKE_OPTEE_TA:
		MODS_IOCTL(MODS_ESC_INVOKE_OPTEE_TA,
			esc_mods_invoke_optee_ta, MODS_OPTEE_PARAMS);
		break;
#endif

	case MODS_ESC_OIST_STATUS:
		MODS_IOCTL(MODS_ESC_OIST_STATUS,
			   esc_mods_oist_status, MODS_TEGRA_OIST_STATUS);
		break;

	case MODS_ESC_MODS_SEND_IPI:
		MODS_IOCTL(MODS_ESC_MODS_SEND_IPI,
			   esc_mods_send_ipi, MODS_SEND_IPI);
		break;
#endif

	case MODS_ESC_FFA_CMD:
#if defined(MODS_HAS_ARM_FFA)
		MODS_IOCTL(MODS_ESC_FFA_CMD, esc_mods_arm_ffa_cmd, MODS_FFA_PARAMS);
#else
		cl_debug(DEBUG_IOCTL, "ioctl(MODS_ESC_FFA_CMD is not supported)\n");
		err = -EINVAL;
#endif
		break;

	case MODS_ESC_ACQUIRE_ACCESS_TOKEN:
		MODS_IOCTL(MODS_ESC_ACQUIRE_ACCESS_TOKEN,
			   esc_mods_acquire_access_token,
			   MODS_ACCESS_TOKEN);
		break;

	case MODS_ESC_RELEASE_ACCESS_TOKEN:
		MODS_IOCTL_NORETVAL(MODS_ESC_RELEASE_ACCESS_TOKEN,
				    esc_mods_release_access_token,
				    MODS_ACCESS_TOKEN);
		break;

	case MODS_ESC_VERIFY_ACCESS_TOKEN:
		MODS_IOCTL_NORETVAL(MODS_ESC_VERIFY_ACCESS_TOKEN,
				    esc_mods_verify_access_token,
				    MODS_ACCESS_TOKEN);
		break;

	case MODS_ESC_WRITE_SYSFS_NODE:
		MODS_IOCTL_NORETVAL(MODS_ESC_WRITE_SYSFS_NODE,
				    esc_mods_write_sysfs_node,
				    MODS_SYSFS_NODE);
		break;

	case MODS_ESC_SYSCTL_WRITE_INT:
		MODS_IOCTL_NORETVAL(MODS_ESC_SYSCTL_WRITE_INT,
				    esc_mods_sysctl_write_int,
				    MODS_SYSCTL_INT);
		break;

	case MODS_ESC_REGISTER_IRQ_4:
		MODS_IOCTL_NORETVAL(MODS_ESC_REGISTER_IRQ_4,
				esc_mods_register_irq_4, MODS_REGISTER_IRQ_4);
		break;

	case MODS_ESC_QUERY_IRQ_3:
		MODS_IOCTL(MODS_ESC_QUERY_IRQ_3,
			   esc_mods_query_irq_3, MODS_QUERY_IRQ_3);
		break;

#if defined(CONFIG_PCI) && defined(MODS_HAS_SRIOV)
	case MODS_ESC_SET_NUM_VF:
		MODS_IOCTL_NORETVAL(MODS_ESC_SET_NUM_VF,
			   esc_mods_set_num_vf, MODS_SET_NUM_VF);
		break;

	case MODS_ESC_SET_TOTAL_VF:
		MODS_IOCTL_NORETVAL(MODS_ESC_SET_TOTAL_VF,
			   esc_mods_set_total_vf, MODS_SET_NUM_VF);
		break;
#endif

#ifdef CONFIG_X86
	case MODS_ESC_READ_MSR:
		MODS_IOCTL(MODS_ESC_READ_MSR,
			   esc_mods_read_msr, MODS_MSR);
		break;

	case MODS_ESC_WRITE_MSR:
		MODS_IOCTL_NORETVAL(MODS_ESC_WRITE_MSR,
			   esc_mods_write_msr, MODS_MSR);
		break;
#endif

	case MODS_ESC_MODS_GET_DRIVER_STATS:
		MODS_IOCTL(MODS_ESC_MODS_GET_DRIVER_STATS,
			   esc_mods_get_driver_stats, MODS_GET_DRIVER_STATS);
		break;

#ifdef CONFIG_TEGRA_IVC
	case MODS_ESC_BPMP_UPHY_LANE_EOM_SCAN:
		MODS_IOCTL(MODS_ESC_BPMP_UPHY_LANE_EOM_SCAN,
			   esc_mods_bpmp_uphy_lane_eom_scan,
			   MODS_BPMP_UPHY_LANE_EOM_SCAN_PARAMS);
		break;
#endif

	default:
		cl_error(
			"unrecognized ioctl 0x%x, dir %u, type 0x%x, nr %u, size 0x%x\n",
			cmd,
			_IOC_DIR(cmd),
			_IOC_TYPE(cmd),
			_IOC_NR(cmd),
			_IOC_SIZE(cmd));
		err = -EINVAL;
		break;
	}

	if (arg_size > (int)sizeof(buf)) {
		kfree(arg_copy);
		atomic_dec(&client->num_allocs);
	}

	LOG_EXT();
	return err;
}
