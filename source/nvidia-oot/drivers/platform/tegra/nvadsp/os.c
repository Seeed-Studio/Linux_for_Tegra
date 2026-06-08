// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2014-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <nvidia/conftest.h>

#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/tegra_nvadsp.h>
#include <linux/version.h>
#include <soc/tegra/fuse-helper.h>
#include <soc/tegra/virt/hv-ivc.h>
#include <linux/elf.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/clk/tegra.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/tegra-firmwares.h>
#include <linux/reset.h>
#include <linux/poll.h>

#include <linux/uaccess.h>

#include "os.h"
#include "dev.h"
#include "dram_app_mem_manager.h"
#include "adsp_console_dbfs.h"
#include "hwmailbox.h"

#define MAILBOX_REGION		".mbox_shared_data"
#define DEBUG_RAM_REGION	".debug_mem_logs"

#define EOT	0x04 /* End of Transmission */
#define SOH	0x01 /* Start of Header */
#define BELL	0x07 /* Bell character */

#define ADSP_TAG	"\n[ADSP OS]"

#define UART_BAUD_RATE	9600

/* Intiialize with FIXED rate, once OS boots up DFS will set required freq */
#define ADSP_TO_APE_CLK_RATIO	2
/* 13.5 MHz, should be changed at bringup time */
#define APE_CLK_FIX_RATE	13500
/*
 * ADSP CLK = APE_CLK * ADSP_TO_APE_CLK_RATIO
 * or
 * ADSP CLK = APE_CLK >> ADSP_TO_APE_CLK_RATIO
 */
#define ADSP_CLK_FIX_RATE (APE_CLK_FIX_RATE * ADSP_TO_APE_CLK_RATIO)

/* total number of crashes allowed on adsp */
#define ALLOWED_CRASHES	1

#define LOGGER_TIMEOUT		1 /* in ms */
#define ADSP_WFI_TIMEOUT	800 /* in ms */
#define LOGGER_COMPLETE_TIMEOUT	500 /* in ms */

#define SEARCH_SOH_RETRY	2

#define DUMP_BUFF 128

struct nvadsp_debug_log {
	struct device		*dev;
	char			*debug_ram_rdr;
	int			debug_ram_sz;
	int			ram_iter;
	atomic_t		is_opened;
	wait_queue_head_t	wait_queue;
	struct completion	complete;
};

struct nvadsp_os_data {
	const struct firmware	*os_firmware;
	struct platform_device	*pdev;
	struct global_sym_info	*adsp_glo_sym_tbl;
	struct nvadsp_debug_log	logger;
	struct nvadsp_cnsl   console;
	struct work_struct	restart_os_work;
	int			adsp_num_crashes;
	bool			adsp_os_fw_loaded;
	struct mutex		fw_load_lock;
	bool			os_running;
	struct mutex		os_run_lock;
	dma_addr_t		adsp_os_addr;
	size_t			adsp_os_size;
	dma_addr_t		app_alloc_addr;
	size_t			app_size;
	int			num_start; /* registers number of time start called */
	bool			cold_start;
	struct nvadsp_mbox	adsp_com_mbox;
	struct completion	entered_wfi;
	void			(*adma_dump_ch_reg)(void);
};

static void __nvadsp_os_stop(struct nvadsp_os_data *, bool);
static irqreturn_t adsp_wdt_handler(int irq, void *arg);
static irqreturn_t adsp_wfi_handler(int irq, void *arg);

#ifdef CONFIG_DEBUG_FS
static int adsp_logger_open(struct inode *inode, struct file *file)
{
	struct nvadsp_os_data *priv = inode->i_private;
	struct nvadsp_debug_log *logger = &priv->logger;
	int ret = -EBUSY;
	char *start;
	int i;

	mutex_lock(&priv->os_run_lock);
	if (!priv->num_start) {
		mutex_unlock(&priv->os_run_lock);
		goto err_ret;
	}
	mutex_unlock(&priv->os_run_lock);

	/*
	 * checks if is_opened is 0, if yes, set 1 and proceed,
	 * else return -EBUSY
	 */
	if (atomic_cmpxchg(&logger->is_opened, 0, 1))
		goto err_ret;

	/* loop till writer is initilized with SOH */
	for (i = 0; i < SEARCH_SOH_RETRY; i++) {

		ret = wait_event_interruptible_timeout(logger->wait_queue,
			memchr(logger->debug_ram_rdr, SOH,
			logger->debug_ram_sz),
			msecs_to_jiffies(LOGGER_TIMEOUT));
		if (ret == -ERESTARTSYS)  /* check if interrupted */
			goto err;

		start = memchr(logger->debug_ram_rdr, SOH,
			logger->debug_ram_sz);
		if (start)
			break;
	}

	if (i == SEARCH_SOH_RETRY) {
		ret = -EINVAL;
		goto err;
        }

	/* maxdiff can be 0, therefore valid */
	logger->ram_iter = start - logger->debug_ram_rdr;

	file->private_data = logger;
	return 0;
err:
	/* reset to 0 so as to mention the node is free */
	atomic_set(&logger->is_opened, 0);
err_ret:
	return ret;
}


static int adsp_logger_flush(struct file *file, fl_owner_t id)
{
	struct nvadsp_debug_log *logger = file->private_data;
	struct device *dev = logger->dev;

	dev_dbg(dev, "%s\n", __func__);

	/* reset to 0 so as to mention the node is free */
	atomic_set(&logger->is_opened, 0);
	return 0;
}

static int adsp_logger_release(struct inode *inode, struct file *file)
{
	struct nvadsp_debug_log *logger = inode->i_private;

	atomic_set(&logger->is_opened, 0);
	return 0;
}

static ssize_t adsp_logger_read(struct file *file, char __user *buf,
			 size_t count, loff_t *ppos)
{
	struct nvadsp_debug_log *logger = file->private_data;
	struct device *dev = logger->dev;
	ssize_t ret_num_char = 1;
	char last_char;

	last_char = logger->debug_ram_rdr[logger->ram_iter];

	if ((last_char != EOT) && (last_char != 0)) {
#if CONFIG_ADSP_DRAM_LOG_WITH_TAG
		if ((last_char == '\n') || (last_char == '\r')) {
			size_t num_char = min(count, sizeof(ADSP_TAG) - 1);

			if (copy_to_user(buf, ADSP_TAG, num_char)) {
				dev_err(dev, "%s failed in copying tag\n", __func__);
				ret_num_char = -EFAULT;
				goto exit;
			}
			ret_num_char = num_char;

		} else
#endif
		if (copy_to_user(buf, &last_char, 1)) {
			dev_err(dev, "%s failed in copying character\n", __func__);
			ret_num_char = -EFAULT;
			goto exit;
		}

		logger->ram_iter =
			(logger->ram_iter + 1) % logger->debug_ram_sz;
		goto exit;
	}

	complete(&logger->complete);
	ret_num_char = wait_event_interruptible_timeout(logger->wait_queue,
		logger->debug_ram_rdr[logger->ram_iter] != EOT,
		msecs_to_jiffies(LOGGER_TIMEOUT));
	if (ret_num_char == -ERESTARTSYS) {
		goto exit;
	}

	last_char = BELL;
	if (copy_to_user(buf, &last_char, 1)) {
		dev_err(dev, "%s failed in copying bell character\n", __func__);
		ret_num_char = -EFAULT;
		goto exit;
	}
	ret_num_char = 1;
exit:
	return ret_num_char;
}

static const struct file_operations adsp_logger_operations = {
	.read		= adsp_logger_read,
	.open		= adsp_logger_open,
	.release	= adsp_logger_release,
	.llseek		= generic_file_llseek,
	.flush		= adsp_logger_flush,
};

static int adsp_create_debug_logger(struct nvadsp_os_data *priv,
				struct dentry *adsp_debugfs_root)
{
	struct nvadsp_debug_log *logger = &priv->logger;
	int ret = 0;

	if (IS_ERR_OR_NULL(adsp_debugfs_root)) {
		ret = -ENOENT;
		goto err_out;
	}

	atomic_set(&logger->is_opened, 0);
	init_waitqueue_head(&logger->wait_queue);
	init_completion(&logger->complete);
	if (!debugfs_create_file("adsp_logger", S_IRUGO,
					adsp_debugfs_root, priv,
					&adsp_logger_operations)) {
		pr_err("unable to create adsp logger debug fs file\n");
		ret = -ENOENT;
	}

err_out:
	return ret;
}
#endif

static bool is_adsp_dram_addr(struct nvadsp_drv_data *drv_data, u64 addr)
{
	int i;

	for (i = 0; i < MAX_DRAM_MAP; i++) {
		struct nvadsp_reg_map *dram = &drv_data->dram_map[i];

		if (dram->size == 0)
			break;

		if ((addr >= dram->addr) &&
			(addr < (dram->addr + dram->size)))
			return true;
	}

	return false;
}

static int is_cluster_mem_addr(struct nvadsp_drv_data *drv_data, u64 addr)
{
	int clust_id;

	for (clust_id = 0; clust_id < MAX_CLUSTER_MEM; clust_id++) {
		struct nvadsp_cluster_mem *cluster_mem;

		cluster_mem = &drv_data->cluster_mem[clust_id];

		if (cluster_mem->size == 0)
			break;

		if ((addr >= cluster_mem->dsp_addr) &&
			(addr < (cluster_mem->dsp_addr + cluster_mem->size)))
			return clust_id;
	}

	return -1;
}

int nvadsp_add_load_mappings(struct nvadsp_drv_data *drv_data,
			phys_addr_t pa, void *mapping, int len)
{
	int map_idx = drv_data->map_idx;

	if (map_idx < 0 || map_idx >= NM_LOAD_MAPPINGS)
		return -EINVAL;

	drv_data->adsp_map[map_idx].da = pa;
	drv_data->adsp_map[map_idx].va = mapping;
	drv_data->adsp_map[map_idx].len = len;
	drv_data->map_idx++;
	return 0;
}

void *nvadsp_da_to_va_mappings(struct nvadsp_drv_data *drv_data,
				u64 da, int len)
{
	void *ptr = NULL;
	int i;

	for (i = 0; i < drv_data->map_idx; i++) {
		int offset = da - drv_data->adsp_map[i].da;

		/* try next carveout if da is too small */
		if (offset < 0)
			continue;

		/* try next carveout if da is too large */
		if (offset + len > drv_data->adsp_map[i].len)
			continue;

		ptr = drv_data->adsp_map[i].va + offset;
		break;
	}
	return ptr;
}

static void *_nvadsp_alloc_coherent(struct nvadsp_handle *nvadsp_handle,
			size_t size, dma_addr_t *da, gfp_t flags)
{
	struct nvadsp_drv_data *drv_data =
				(struct nvadsp_drv_data *)nvadsp_handle;
	struct nvadsp_os_data *priv;
	struct device *dev;
	void *va = NULL;

	if (!drv_data || !drv_data->pdev || !drv_data->os_priv) {
		pr_err("ADSP Driver is not initialized\n");
		goto end;
	}

	priv = drv_data->os_priv;
	dev = &priv->pdev->dev;

	va = dma_alloc_coherent(dev, size, da, flags);
	if (!va) {
		dev_err(dev, "unable to allocate the memory for size %lu\n",
				size);
		goto end;
	}
	WARN(!is_adsp_dram_addr(drv_data, *da), "bus addr %llx beyond %x\n",
				*da, UINT_MAX);
end:
	return va;
}

static void _nvadsp_free_coherent(struct nvadsp_handle *nvadsp_handle,
			size_t size, void *va, dma_addr_t da)
{
	struct nvadsp_drv_data *drv_data =
				(struct nvadsp_drv_data *)nvadsp_handle;
	struct nvadsp_os_data *priv;
	struct device *dev;

	if (!drv_data || !drv_data->pdev || !drv_data->os_priv) {
		pr_err("ADSP Driver is not initialized\n");
		return;
	}

	priv = drv_data->os_priv;
	dev = &priv->pdev->dev;

	dma_free_coherent(dev, size, va, da);
}

struct elf32_shdr *
nvadsp_get_section(const struct firmware *fw, char *sec_name)
{
	int i;
	const u8 *elf_data = fw->data;
	struct elf32_hdr *ehdr = (struct elf32_hdr *)elf_data;
	struct elf32_shdr *shdr;
	const char *name_table;

	/* look for the resource table and handle it */
	shdr = (struct elf32_shdr *)(elf_data + ehdr->e_shoff);
	name_table = elf_data + shdr[ehdr->e_shstrndx].sh_offset;

	for (i = 0; i < ehdr->e_shnum; i++, shdr++)
		if (!strcmp(name_table + shdr->sh_name, sec_name)) {
			pr_debug("found the section %s\n",
					name_table + shdr->sh_name);
			return shdr;
		}
	return NULL;
}

static inline void __maybe_unused dump_global_symbol_table(
					struct nvadsp_os_data *priv)
{
	struct device *dev = &priv->pdev->dev;
	struct global_sym_info *table = priv->adsp_glo_sym_tbl;
	int num_ent;
	int i;

	if (!table) {
		dev_err(dev, "no table not created\n");
		return;
	}
	num_ent = table[0].addr;
	dev_info(dev, "total number of entries in global symbol table %d\n",
			num_ent);

	pr_info("NAME ADDRESS TYPE\n");
	for (i = 1; i < num_ent; i++)
		pr_info("%s %x %s\n", table[i].name, table[i].addr,
			ELF32_ST_TYPE(table[i].info) == STT_FUNC ?
				"STT_FUNC" : "STT_OBJECT");
}

#ifdef CONFIG_ANDROID
static int
__maybe_unused create_global_symbol_table(struct nvadsp_os_data *priv,
					const struct firmware *fw)
{
	int i;
	struct device *dev = &priv->pdev->dev;
	struct elf32_shdr *sym_shdr = nvadsp_get_section(fw, ".symtab");
	struct elf32_shdr *str_shdr = nvadsp_get_section(fw, ".strtab");
	const u8 *elf_data = fw->data;
	const char *name_table;
	/* The first entry stores the number of entries in the array */
	int num_ent = 1;
	struct elf32_sym *sym;
	struct elf32_sym *last_sym;

	if (!sym_shdr || !str_shdr) {
		dev_dbg(dev, "section symtab/strtab not found!\n");
		return -EINVAL;
	}

	sym = (struct elf32_sym *)(elf_data + sym_shdr->sh_offset);
	name_table = elf_data + str_shdr->sh_offset;

	num_ent += sym_shdr->sh_size / sizeof(struct elf32_sym);
	priv->adsp_glo_sym_tbl = devm_kzalloc(dev,
		sizeof(struct global_sym_info) * num_ent, GFP_KERNEL);
	if (!priv->adsp_glo_sym_tbl)
		return -ENOMEM;

	last_sym = sym + num_ent;

	for (i = 1; sym < last_sym; sym++) {
		unsigned char info = sym->st_info;
		unsigned char type = ELF32_ST_TYPE(info);
		if ((ELF32_ST_BIND(sym->st_info) == STB_GLOBAL) &&
		((type == STT_OBJECT) || (type == STT_FUNC))) {
			char *name = priv->adsp_glo_sym_tbl[i].name;

			strscpy(name, name_table + sym->st_name, SYM_NAME_SZ);
			priv->adsp_glo_sym_tbl[i].addr = sym->st_value;
			priv->adsp_glo_sym_tbl[i].info = info;
			i++;
		}
	}
	priv->adsp_glo_sym_tbl[0].addr = i;
	return 0;
}
#endif /* CONFIG_ANDROID */

struct global_sym_info * __maybe_unused find_global_symbol(
					struct nvadsp_drv_data *drv_data,
					const char *sym_name)
{
	struct nvadsp_os_data *priv = drv_data->os_priv;
	struct device *dev = &priv->pdev->dev;
	struct global_sym_info *table = priv->adsp_glo_sym_tbl;
	int num_ent;
	int i;

	if (unlikely(!table)) {
		dev_err(dev, "symbol table not present\n");
		return NULL;
	}
	num_ent = table[0].addr;

	for (i = 1; i < num_ent; i++) {
		if (!strncmp(table[i].name, sym_name, SYM_NAME_SZ))
			return &table[i];
	}
	return NULL;
}

static void *get_mailbox_shared_region(struct nvadsp_os_data *priv,
					const struct firmware *fw)
{
	struct device *dev;
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(priv->pdev);
	struct elf32_shdr *shdr;
	int addr;
	int size;

	if (!priv->pdev) {
		pr_err("ADSP Driver is not initialized\n");
		return ERR_PTR(-EINVAL);
	}

	dev = &priv->pdev->dev;

	shdr = nvadsp_get_section(fw, MAILBOX_REGION);
	if (!shdr) {
		dev_dbg(dev, "section %s not found\n", MAILBOX_REGION);
		return ERR_PTR(-EINVAL);
	}

	dev_dbg(dev, "the shared section is present at 0x%x\n", shdr->sh_addr);
	addr = shdr->sh_addr;
	size = shdr->sh_size;
	drv_data->shared_adsp_os_data_iova  = addr;
	return nvadsp_da_to_va_mappings(drv_data, addr, size);
}

static int nvadsp_os_elf_load(struct nvadsp_os_data *priv,
				const struct firmware *fw)
{
	struct device *dev = &priv->pdev->dev;
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(priv->pdev);
	struct elf32_hdr *ehdr;
	struct elf32_phdr *phdr;
	int i, ret = 0, clust_id;
	const u8 *elf_data = fw->data;
	void *cluster_mem_va[MAX_CLUSTER_MEM] = {NULL};
	void *shadow_buf[MAX_CLUSTER_MEM] = {NULL};
	bool cluster_mem_active[MAX_CLUSTER_MEM] = {false};

	/*
	 * For RAMs inside the cluster, direct copy of ELF segments
	 * is not feasible due to device type memory expecting aligned
	 * accesses. Therefore, temporary shadow buffers are allocated
	 * for each such memory and ELF segments will be copied into
	 * those. Finally the shadow buffer will be copied in one shot
	 * into its respective cluster memory.
	 */
	for (i = 0; i < MAX_CLUSTER_MEM; i++) {
		struct nvadsp_cluster_mem *cluster_mem;

		cluster_mem = &drv_data->cluster_mem[i];

		if (cluster_mem->size == 0)
			break;

		cluster_mem_va[i] = devm_memremap(dev, cluster_mem->ccplex_addr,
						cluster_mem->size, MEMREMAP_WT);
		if (IS_ERR(cluster_mem_va[i])) {
			dev_err(dev, "unable to map cluster mem: %llx\n",
				cluster_mem->ccplex_addr);
			ret = -ENOMEM;
			goto end;
		}

		shadow_buf[i] = devm_kzalloc(dev, cluster_mem->size, GFP_KERNEL);
		if (!shadow_buf[i]) {
			dev_err(dev, "alloc for cluster mem failed\n");
			ret = -ENOMEM;
			goto end;
		}

		nvadsp_add_load_mappings(drv_data,
				(phys_addr_t)cluster_mem->dsp_addr,
				shadow_buf[i], (int)cluster_mem->size);
	}

	ehdr = (struct elf32_hdr *)elf_data;
	phdr = (struct elf32_phdr *)(elf_data + ehdr->e_phoff);

	/* go through the available ELF segments */
	for (i = 0; i < ehdr->e_phnum; i++, phdr++) {
		void *va;
		u32 da = phdr->p_paddr;
		u32 memsz = phdr->p_memsz;
		u32 filesz = phdr->p_filesz;
		u32 offset = phdr->p_offset;

		if (phdr->p_type != PT_LOAD)
			continue;

		dev_dbg(dev, "phdr: type %d da 0x%x memsz 0x%x filesz 0x%x\n",
				phdr->p_type, da, memsz, filesz);

		va = nvadsp_da_to_va_mappings(drv_data, da, filesz);
		if (!va) {
			dev_err(dev, "no va for da 0x%x filesz 0x%x\n",
					da, filesz);
			ret = -EINVAL;
			break;
		}

		if (filesz > memsz) {
			dev_err(dev, "bad phdr filesz 0x%x memsz 0x%x\n",
					filesz, memsz);
			ret = -EINVAL;
			break;
		}

		if (offset + filesz > fw->size) {
			dev_err(dev, "truncated fw: need 0x%x avail 0x%zx\n",
					offset + filesz, fw->size);
			ret = -EINVAL;
			break;
		}

		/* put the segment where the remote processor expects it */
		if (filesz) {
			clust_id = is_cluster_mem_addr(drv_data, da);
			if (clust_id != -1) {
				cluster_mem_active[clust_id] = true;
				memcpy(va, elf_data + offset, filesz);
			} else if (is_adsp_dram_addr(drv_data, da)) {
				memcpy(va, elf_data + offset, filesz);
			} else if ((da == drv_data->evp_base[ADSP_EVP_BASE]) &&
				(filesz == drv_data->evp_base[ADSP_EVP_SIZE])) {

				drv_data->state.evp_ptr = va;
				memcpy(drv_data->state.evp,
					elf_data + offset, filesz);
			} else {
				dev_err(dev, "can't load mem pa:0x%x va:%p\n",
						da, va);
				ret = -EINVAL;
				break;
			}
		}
	}

	/* Copying from shadow buffers into cluster memories */
	for (i = 0; i < MAX_CLUSTER_MEM; i++) {
		struct nvadsp_cluster_mem *cluster_mem;

		cluster_mem = &drv_data->cluster_mem[i];

		if (cluster_mem->size == 0)
			break;

		if (cluster_mem_active[i])
			memcpy(cluster_mem_va[i],
				shadow_buf[i],
				cluster_mem->size);
	}

end:
	for (i = 0; i < MAX_CLUSTER_MEM; i++) {
		if (!IS_ERR_OR_NULL(cluster_mem_va[i]))
			devm_memunmap(dev, cluster_mem_va[i]);
		if (shadow_buf[i])
			devm_kfree(dev, shadow_buf[i]);
	}

	return ret;
}

#ifdef CONFIG_IOMMU_API_EXPORTED
/**
 * Allocate a dma buffer and map it to a specified iova
 * Return valid cpu virtual address on success or NULL on failure
 */
static void *nvadsp_dma_alloc_and_map_at(struct platform_device *pdev,
					 size_t size, dma_addr_t iova,
					 gfp_t flags)
{
	struct iommu_domain *domain = iommu_get_domain_for_dev(&pdev->dev);
	unsigned long align_mask = ~0UL << fls_long(size - 1);
	struct device *dev = &pdev->dev;
	dma_addr_t aligned_iova = iova & align_mask;
	dma_addr_t end = iova + size;
	dma_addr_t tmp_iova, offset;
	phys_addr_t pa, pa_new;
	void *cpu_va;
	int ret;
	unsigned long shift = 0, pg_size = 0, mp_size = 0;

	if (!domain) {
		dev_err(dev, "Unable to get iommu_domain\n");
		return NULL;
	}

	shift = __ffs(domain->pgsize_bitmap);
	pg_size = 1UL << shift;
	mp_size = pg_size;

	/*
	 * Reserve iova range using aligned size: adsp memory might not start
	 * from an aligned address by power of 2, while iommu_dma_alloc_iova()
	 * would shift the allocation off the target iova so as to align start
	 * address by power of 2. To prevent this shifting, use aligned size.
	 * It might allocate an excessive iova region but it would be handled
	 * by IOMMU core during iommu_dma_free_iova().
	 */
	tmp_iova = iommu_dma_alloc_iova(domain,
				(end - aligned_iova), (end - pg_size), dev);
	if (tmp_iova != aligned_iova) {
		dev_err(dev, "failed to reserve iova range [%llx, %llx]\n",
			aligned_iova, end);
		return NULL;
	}

	dev_dbg(dev, "Reserved iova region [%llx, %llx]\n", aligned_iova, end);

	/* Allocate a memory first and get a tmp_iova */
	cpu_va = dma_alloc_coherent(dev, size, &tmp_iova, flags);
	if (!cpu_va)
		goto fail_dma_alloc;

	/* Use tmp_iova to remap non-contiguous pages to the desired iova */
	for (offset = 0; offset < size; offset += mp_size) {
		dma_addr_t cur_iova = tmp_iova + offset;

		mp_size = pg_size;
		pa = iommu_iova_to_phys(domain, cur_iova);
		/* Checking if next physical addresses are contiguous */
		for ( ; offset + mp_size < size; mp_size += pg_size) {
			pa_new = iommu_iova_to_phys(domain, cur_iova + mp_size);
			if (pa + mp_size != pa_new)
				break;
		}

		/* Remap the contiguous physical addresses together */
		ret = iommu_map(domain, iova + offset, pa, mp_size,
#if defined(NV_IOMMU_MAP_HAS_GFP_ARG)
				IOMMU_READ | IOMMU_WRITE, GFP_KERNEL);
#else
				IOMMU_READ | IOMMU_WRITE);
#endif
		if (ret) {
			dev_err(dev, "failed to map pa %llx va %llx size %lx\n",
				pa, iova + offset, mp_size);
			goto fail_map;
		}

		/* Verify if the new iova is correctly mapped */
		if (pa != iommu_iova_to_phys(domain, iova + offset)) {
			dev_err(dev, "mismatched pa 0x%llx <-> 0x%llx\n",
				pa, iommu_iova_to_phys(domain, iova + offset));
			goto fail_map;
		}
	}

	/* Unmap and free the tmp_iova since target iova is linked */
	iommu_unmap(domain, tmp_iova, size);
	iommu_dma_free_iova(domain->iova_cookie, tmp_iova, size, NULL);

	return cpu_va;

fail_map:
	iommu_unmap(domain, iova, offset);
	dma_free_coherent(dev, size, cpu_va, tmp_iova);
fail_dma_alloc:
	iommu_dma_free_iova(domain->iova_cookie,
			(end - aligned_iova), (end - pg_size), NULL);

	return NULL;
}
#endif

static int allocate_memory_for_adsp_os(struct nvadsp_os_data *priv)
{
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(priv->pdev);
	struct device *dev = &priv->pdev->dev;
	struct resource *co_mem = &drv_data->co_mem;
#ifdef CONFIG_IOMMU_API_EXPORTED
	dma_addr_t addr;
#else
	phys_addr_t addr;
#endif
	void *dram_va;
	size_t size;
	int ret = 0;

	addr = priv->adsp_os_addr;
	size = priv->adsp_os_size;

	if (size == 0)
		return 0;

	if (co_mem->start) {
		dram_va = devm_memremap(dev, co_mem->start,
					size, MEMREMAP_WT);
		if (IS_ERR(dram_va)) {
			dev_err(dev, "unable to map CO mem: %pR\n", co_mem);
			ret = -ENOMEM;
			goto end;
		}
		dev_info(dev, "Mapped CO mem: %pR\n", co_mem);
		goto map_and_end;
	}

#ifdef CONFIG_IOMMU_API_EXPORTED
	dram_va = nvadsp_dma_alloc_and_map_at(priv->pdev, size, addr,
							GFP_KERNEL);
	if (!dram_va) {
		dev_err(dev, "unable to allocate SMMU pages\n");
		ret = -ENOMEM;
		goto end;
	}
#else
	dram_va = ioremap(addr, size);
	if (!dram_va) {
		dev_err(dev, "remap failed for addr 0x%llx\n", addr);
		ret = -ENOMEM;
		goto end;
	}
#endif

map_and_end:
	nvadsp_add_load_mappings(drv_data, addr, dram_va, size);
end:
	return ret;
}

static void deallocate_memory_for_adsp_os(struct nvadsp_os_data *priv)
{
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(priv->pdev);
	struct device *dev = &priv->pdev->dev;
	size_t size = priv->adsp_os_size;
	void *va;

	if (size == 0)
		return;

	va = nvadsp_da_to_va_mappings(drv_data, priv->adsp_os_addr, size);

	if (drv_data->co_mem.start) {
		devm_memunmap(dev, va);
		return;
	}

#ifdef CONFIG_IOMMU_API_EXPORTED
	dma_free_coherent(dev, priv->adsp_os_size, va, priv->adsp_os_addr);
#endif
}

static void nvadsp_set_shared_mem(struct nvadsp_os_data *priv,
				  struct nvadsp_shared_mem *shared_mem,
				  uint32_t dynamic_app_support)
{
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(priv->pdev);
	struct device *dev = &priv->pdev->dev;
	struct nvadsp_os_args *os_args;
	u32 chip_id;

	shared_mem->os_args.dynamic_app_support = dynamic_app_support;
	/* set logger strcuture with required properties */
	priv->logger.debug_ram_rdr = shared_mem->os_args.logger;
	priv->logger.debug_ram_sz = sizeof(shared_mem->os_args.logger);
	priv->logger.dev = dev;
	priv->adsp_os_fw_loaded = true;

	chip_id = (u32)__tegra_get_chip_id();
	if (drv_data->chip_data->chipid_ext)
		chip_id = (chip_id << 4) | drv_data->chip_data->chipid_ext;

	os_args = &shared_mem->os_args;
	/* Chip id info is communicated twice to ADSP
	 * TODO::clean up the redundant comm.
	 */
	os_args->chip_id = chip_id;

	/*
	 * Tegra platform is encoded in the upper 16 bits
	 * of chip_id; can be improved to make this a
	 * separate member in nvadsp_os_args
	 */
	os_args->chip_id |= (drv_data->tegra_platform << 16);

	drv_data->shared_adsp_os_data = shared_mem;
}

static int __nvadsp_os_secload(struct nvadsp_os_data *priv)
{
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(priv->pdev);
	dma_addr_t addr = drv_data->adsp_mem[ACSR_ADDR];
	size_t size = drv_data->adsp_mem[ACSR_SIZE];
	struct device *dev = &priv->pdev->dev;
	struct nvadsp_handle *nvadsp_handle = &drv_data->nvadsp_handle;
	void *dram_va;

	if (size == 0) {
		dev_warn(dev, "no shared mem requested\n");
		return 0;
	}

	if (drv_data->chip_data->adsp_shared_mem_hwmbox != 0) {
		dram_va = _nvadsp_alloc_coherent(nvadsp_handle,
						size, &addr, GFP_KERNEL);
		if (dram_va == NULL) {
			dev_err(dev, "unable to allocate shared region\n");
			return -ENOMEM;
		}
	} else {
#ifdef CONFIG_IOMMU_API_EXPORTED
		dram_va = nvadsp_dma_alloc_and_map_at(priv->pdev, size, addr,
								GFP_KERNEL);
		if (dram_va == NULL) {
			dev_err(dev, "unable to allocate shared region\n");
			return -ENOMEM;
		}
#else
		dram_va = ioremap((phys_addr_t)addr, size);
		if (dram_va == NULL) {
			dev_err(dev, "remap failed on shared region\n");
			return -ENOMEM;
		}
#endif
	}

	drv_data->shared_adsp_os_data_iova = addr;
	nvadsp_set_shared_mem(priv, dram_va, 0);

	return 0;
}

static int __nvadsp_firmware_load(struct nvadsp_os_data *priv)
{
	struct nvadsp_shared_mem *shared_mem;
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(priv->pdev);
	dma_addr_t shrd_mem_iova = 0;
	size_t acsr_size = drv_data->adsp_mem[ACSR_SIZE];
	struct device *dev = &priv->pdev->dev;
	struct nvadsp_handle *nvadsp_handle = &drv_data->nvadsp_handle;
	const struct firmware *fw;
	int ret = 0;

	ret = request_firmware(&fw, drv_data->adsp_elf, dev);
	if (ret < 0) {
		dev_err(dev, "reqest firmware for %s failed with %d\n",
				drv_data->adsp_elf, ret);
		goto end;
	}
#ifdef CONFIG_ANDROID
	ret = create_global_symbol_table(priv, fw);
	if (ret) {
		dev_err(dev, "unable to create global symbol table\n");
		goto release_firmware;
	}
#endif
	ret = allocate_memory_for_adsp_os(priv);
	if (ret) {
		dev_err(dev, "unable to allocate memory for adsp os\n");
		goto release_firmware;
	}

	dev_info(dev, "Loading ADSP OS firmware %s\n", drv_data->adsp_elf);

	ret = nvadsp_os_elf_load(priv, fw);
	if (ret) {
		dev_err(dev, "failed to load %s\n", drv_data->adsp_elf);
		goto deallocate_os_memory;
	}

	shared_mem = get_mailbox_shared_region(priv, fw);
	if (IS_ERR_OR_NULL(shared_mem) && acsr_size) {
		if (drv_data->adsp_mem[ACSR_ADDR] == 0) {
			shared_mem = _nvadsp_alloc_coherent(nvadsp_handle,
					acsr_size, &shrd_mem_iova, GFP_KERNEL);
			if (!shared_mem) {
				dev_err(dev, "unable to allocate shared region\n");
				ret = -ENOMEM;
				goto deallocate_os_memory;
			}
			drv_data->shared_adsp_os_data_iova = shrd_mem_iova;
		} else {
			/*
			 * If not dynamic memory allocation then assume shared
			 * memory to be placed at the start of OS memory
			 */
			shared_mem = nvadsp_da_to_va_mappings(drv_data,
					priv->adsp_os_addr, priv->adsp_os_size);
			if (!shared_mem) {
				dev_err(dev, "Failed to get VA for ADSP OS\n");
				ret = -ENOMEM;
				goto deallocate_os_memory;
			}
			drv_data->shared_adsp_os_data_iova = priv->adsp_os_addr;
		}
	}
	if (!IS_ERR_OR_NULL(shared_mem))
		nvadsp_set_shared_mem(priv, shared_mem, 1);
	else
		dev_warn(dev, "no shared mem requested\n");

#ifdef CONFIG_ADSP_DYNAMIC_APP
	if (priv->app_size) {
		ret = dram_app_mem_init(priv->app_alloc_addr, priv->app_size);
		if (ret) {
			dev_err(dev, "Memory allocation dynamic apps failed\n");
			goto deallocate_os_memory;
		}
	}
#endif /* CONFIG_ADSP_DYNAMIC_APP */

	priv->os_firmware = fw;

	return 0;

deallocate_os_memory:
	if (shrd_mem_iova)
		_nvadsp_free_coherent(nvadsp_handle,
				acsr_size, shared_mem, shrd_mem_iova);
	deallocate_memory_for_adsp_os(priv);
release_firmware:
	release_firmware(fw);
end:
	return ret;

}

#ifdef CONFIG_TEGRA_ADSP_MULTIPLE_FW

#define MFW_MAX_OTHER_CORES    3
dma_addr_t mfw_smem_iova[MFW_MAX_OTHER_CORES];
void *mfw_hsp_va[MFW_MAX_OTHER_CORES];

static int nvadsp_load_multi_fw(struct nvadsp_os_data *priv)
{
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(priv->pdev);
	struct device *dev = &priv->pdev->dev;
	struct nvadsp_handle *nvadsp_handle = &drv_data->nvadsp_handle;
	int i, j, ret;
	void *dram_va, *hsp_va;
	dma_addr_t shrd_mem_iova;
	size_t acsr_size = drv_data->adsp_mem[ACSR_SIZE];
	struct device_node *hsp_node;
	struct resource hsp_inst;
	u32 hsp_int, hsp_int_targ;

	hsp_node = of_get_child_by_name(dev->of_node, "ahsp_sm_multi");
	if (!hsp_node) {
		dev_err(dev, "missing ahsp_sm addr\n");
		return -ENOENT;
	}

	for (i = 0; !of_address_to_resource(hsp_node, i, &hsp_inst); i++) {
		if (i >= MFW_MAX_OTHER_CORES) {
			dev_err(dev, "core out of bound\n");
			break;
		}

		if (drv_data->adsp_os_secload) {
			/* For front door boot MB2 would have loaded
			 * the FW for all the cores; only shared
			 * memory neeeds to be allocated and set
			 */
			dram_va = _nvadsp_alloc_coherent(nvadsp_handle,
					acsr_size, &shrd_mem_iova, GFP_KERNEL);
			if (!dram_va) {
				dev_err(dev,
					"mem alloc failed for adsp %d\n", i);
				continue;
			}
		} else {
			const struct firmware *fw;
			const char *adsp_elf;
			u32 os_mem, os_size;

			ret = of_property_read_string_index(
				dev->of_node, "nvidia,adsp_elf_multi",
				i, &adsp_elf);
			if (ret) {
				dev_err(dev, "err reading adsp FW %d: %d\n",
						(i + 1), ret);
				continue;
			}

			if (!strcmp(adsp_elf, ""))
				continue;

			ret = request_firmware(&fw, adsp_elf, dev);
			if (ret < 0) {
				dev_err(dev, "request FW failed for %s: %d\n",
						adsp_elf, ret);
				continue;
			}

			os_size = drv_data->adsp_mem[ADSP_OS_SIZE];
			os_mem  = drv_data->adsp_mem[ADSP_OS_ADDR] +
						((i + 1) * os_size);

#ifdef CONFIG_IOMMU_API_EXPORTED
			dram_va = nvadsp_dma_alloc_and_map_at(pdev,
					(size_t)os_size, (dma_addr_t)os_mem,
					GFP_KERNEL);
			if (!dram_va) {
				dev_err(dev,
					"dma_alloc failed for 0x%x\n", os_mem);
				continue;
			}
#else
			dram_va = ioremap((phys_addr_t)os_mem, (size_t)os_size);
			if (!dram_va) {
				dev_err(dev,
					"remap failed for addr 0x%x\n", os_mem);
				continue;
			}
#endif

			nvadsp_add_load_mappings(drv_data, (phys_addr_t)os_mem,
						dram_va, (size_t)os_size);

			dev_info(dev, "Loading ADSP OS firmware %s\n", adsp_elf);
			ret = nvadsp_os_elf_load(priv, fw);
			if (ret) {
				dev_err(dev, "failed to load %s\n", adsp_elf);
				continue;
			}

			if (drv_data->adsp_mem[ACSR_ADDR] == 0) {
				dram_va = _nvadsp_alloc_coherent(nvadsp_handle,
					acsr_size, &shrd_mem_iova, GFP_KERNEL);
				if (!dram_va) {
					dev_err(dev,
						"mem alloc failed for adsp %d\n", i);
					continue;
				}
			} else {
				/* Shared mem is at the start of OS memory */
				shrd_mem_iova = (dma_addr_t)os_mem;
			}
		}

		nvadsp_set_shared_mem(priv, dram_va, 0);

		/* Store shared mem IOVA for writing into MBOX (for ADSP) */
		hsp_va = devm_ioremap_resource(dev, &hsp_inst);
		if (IS_ERR(hsp_va)) {
			dev_err(dev, "ioremap failed for HSP %d\n", (i + 1));
			continue;
		}
		mfw_smem_iova[i] = shrd_mem_iova;
		mfw_hsp_va[i]    = hsp_va;

#ifdef CONFIG_AGIC_EXT_APIS
		/*
		 * Interrupt routing of AHSP1-3 is only for the
		 * sake of completion; CCPLEX<->ADSP communication
		 * is limited to AHSP0, i.e. ADSP core-0
		 */
		for (j = 0; j < 8; j += 2) {
			if (of_property_read_u32_index(hsp_node,
					"nvidia,ahsp_sm_interrupts",
					(i * 8) + j, &hsp_int)) {
				dev_err(dev,
					"no HSP int config for core %d\n",
					(i + 1));
				break;
			}
			if (of_property_read_u32_index(hsp_node,
					"nvidia,ahsp_sm_interrupts",
					(i * 8) + j + 1, &hsp_int_targ)) {
				dev_err(dev,
					"no HSP int_targ config for core %d\n",
					(i + 1));
				break;
			}

			/*
			 * DT definition decrements SPI IRQs
			 * by 32, so restore the same here
			 */
			ret = tegra_agic_route_interrupt(hsp_int + 32,
							 hsp_int_targ);
			if (ret) {
				dev_err(dev,
					"HSP routing for core %d failed: %d\n",
					(i + 1), ret);
				break;
			}
		}
#endif // CONFIG_AGIC_EXT_APIS

		if (j == 8)
			dev_info(dev, "Setup done for core %d FW\n", (i + 1));
	}

	of_node_put(hsp_node);

	return 0;
}
#endif // CONFIG_TEGRA_ADSP_MULTIPLE_FW

static int _nvadsp_os_load(struct nvadsp_handle *nvadsp_handle)
{
	struct nvadsp_drv_data *drv_data =
				(struct nvadsp_drv_data *)nvadsp_handle;
	struct nvadsp_os_data *priv;
	struct device *dev;
	int ret = 0;

	if (!drv_data || !drv_data->pdev || !drv_data->os_priv) {
		pr_err("ADSP Driver is not initialized\n");
		return -EINVAL;
	}

	priv = drv_data->os_priv;
	dev = &priv->pdev->dev;

	mutex_lock(&priv->fw_load_lock);
	if (priv->adsp_os_fw_loaded)
		goto end;

#ifdef CONFIG_TEGRA_ADSP_MULTIPLE_FW
	dev_info(dev, "Loading multiple ADSP FW....\n");
	nvadsp_load_multi_fw(priv);
#endif // CONFIG_TEGRA_ADSP_MULTIPLE_FW

	if (drv_data->adsp_os_secload) {
		dev_info(dev, "ADSP OS firmware already loaded\n");
		ret = __nvadsp_os_secload(priv);
	} else {
		ret = __nvadsp_firmware_load(priv);
	}

	if (ret == 0) {
		priv->adsp_os_fw_loaded = true;
#ifdef CONFIG_DEBUG_FS
		wake_up(&priv->logger.wait_queue);
#endif
	}
end:
	mutex_unlock(&priv->fw_load_lock);
	return ret;
}

/*
 * Static adsp freq to emc freq lookup table
 *
 * arg:
 *	adspfreq - adsp freq in KHz
 * return:
 *	0 - min emc freq
 *	> 0 - expected emc freq at this adsp freq
 */
#ifdef CONFIG_TEGRA_ADSP_DFS
u32 adsp_to_emc_freq(u32 adspfreq)
{
	/*
	 * Vote on memory bus frequency based on adsp frequency
	 * cpu rate is in kHz, emc rate is in Hz
	 */
	if (adspfreq >= 204800)
		return 102000;	/* adsp >= 204.8 MHz, emc 102 MHz */
	else
		return 0;		/* emc min */
}
#endif

static int nvadsp_set_ape_emc_freq(struct nvadsp_drv_data *drv_data)
{
	unsigned long ape_emc_freq;
	struct device *dev = &drv_data->pdev->dev;
	int ret;

#ifdef CONFIG_TEGRA_ADSP_DFS
	 /* pass adsp freq in KHz. adsp_emc_freq in Hz */
	ape_emc_freq = adsp_to_emc_freq(drv_data->adsp_freq / 1000) * 1000;
#else
	ape_emc_freq = drv_data->ape_emc_freq * 1000; /* in Hz */
#endif
	dev_dbg(dev, "requested adsp cpu freq %luKHz",
		 drv_data->adsp_freq / 1000);
	dev_dbg(dev, "emc freq %luHz\n", ape_emc_freq / 1000);

	/*
	 * ape_emc_freq is not required to set if adsp_freq
	 * is lesser than 204.8 MHz
	 */

	if (!ape_emc_freq)
		return 0;

	ret = nvadsp_set_bw(drv_data, ape_emc_freq);
	dev_dbg(dev, "ape.emc freq %luKHz\n",
		tegra_bwmgr_get_emc_rate() / 1000);

	return ret;
}

static int nvadsp_set_ape_freq(struct nvadsp_drv_data *drv_data)
{
	unsigned long ape_freq = drv_data->ape_freq * 1000; /* in Hz*/
	struct device *dev = &drv_data->pdev->dev;
	int ret;

#ifdef CONFIG_TEGRA_ADSP_ACTMON
	ape_freq = drv_data->adsp_freq / ADSP_TO_APE_CLK_RATIO;
#endif
	dev_dbg(dev, "ape freq %luKHz", ape_freq / 1000);

	if (!ape_freq)
		return 0;

	ret = clk_set_rate(drv_data->ape_clk, ape_freq);

	dev_dbg(dev, "ape freq %luKHz\n",
		clk_get_rate(drv_data->ape_clk) / 1000);
	return ret;
}

static int __deassert_adsp(struct nvadsp_drv_data *d)
{
	struct platform_device *pdev = d->pdev;
	struct device *dev = &pdev->dev;
	int ret = 0;

	/*
	 * The ADSP_ALL reset in BPMP-FW is overloaded to de-assert
	 * all 7 resets i.e. ADSP, ADSPINTF, ADSPDBG, ADSPNEON, ADSPPERIPH,
	 * ADSPSCU and ADSPWDT resets. The BPMP-FW also takes care
	 * of specific de-assert sequence and delays between them.
	 * So de-resetting only ADSP reset is sufficient to de-reset
	 * all ADSP sub-modules.
	 */
	ret = reset_control_deassert(d->adspall_rst);
	if (ret)
		dev_err(dev, "failed to deassert adsp\n");

	return ret;
}

static int nvadsp_deassert_adsp(struct nvadsp_drv_data *drv_data)
{
	int ret = -EINVAL;

	if (drv_data->deassert_adsp)
		ret = drv_data->deassert_adsp(drv_data);

	return ret;
}

static int __assert_adsp(struct nvadsp_drv_data *d)
{
	struct platform_device *pdev = d->pdev;
	struct device *dev = &pdev->dev;
	int ret = 0;

	/*
	 * The ADSP_ALL reset in BPMP-FW is overloaded to assert
	 * all 7 resets i.e. ADSP, ADSPINTF, ADSPDBG, ADSPNEON,
	 * ADSPPERIPH, ADSPSCU and ADSPWDT resets. So resetting
	 * only ADSP reset is sufficient to reset all ADSP sub-modules.
	 */
	ret = reset_control_assert(d->adspall_rst);
	if (ret)
		dev_err(dev, "failed to assert adsp\n");

	return ret;
}

static int nvadsp_assert_adsp(struct nvadsp_drv_data *drv_data)
{
	int ret = -EINVAL;

	if (drv_data->assert_adsp)
		ret = drv_data->assert_adsp(drv_data);

	return ret;
}

static int nvadsp_set_boot_vec(struct nvadsp_drv_data *drv_data)
{
	if (drv_data->set_boot_vec)
		return drv_data->set_boot_vec(drv_data);

	return 0;
}

static int nvadsp_set_boot_freqs(struct nvadsp_drv_data *drv_data)
{
	int ret = 0;

	/* on Unit-FPGA do not set clocks, return success */
	if (drv_data->adsp_unit_fpga)
		return 0;

	if (drv_data->set_boot_freqs) {
		ret = drv_data->set_boot_freqs(drv_data);
		if (ret)
			goto end;
	}

	if (drv_data->ape_clk) {
		ret = nvadsp_set_ape_freq(drv_data);
		if (ret)
			goto end;
	}
	if (drv_data->bwmgr) {
		ret = nvadsp_set_ape_emc_freq(drv_data);
		if (ret)
			goto end;
	}
end:
	return ret;
}

static void nvadsp_set_config_hwmboxes(struct nvadsp_os_data *priv)
{
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(priv->pdev);

	if (drv_data->chip_data->adsp_shared_mem_hwmbox != 0) {
#ifdef CONFIG_TEGRA_ADSP_MULTIPLE_FW
		int i;
		for (i = 0; i < MFW_MAX_OTHER_CORES; i++) {
			if (mfw_hsp_va[i]) {
				writel((uint32_t)mfw_smem_iova[i],
					mfw_hsp_va[i] +
						drv_data->chip_data->
						adsp_shared_mem_hwmbox
					);
			}
		}
#endif // CONFIG_TEGRA_ADSP_MULTIPLE_FW

		/* Pass shared memory base */
		hwmbox_writel(drv_data,
			(uint32_t)drv_data->shared_adsp_os_data_iova,
			drv_data->chip_data->adsp_shared_mem_hwmbox);
	}

	if (priv->cold_start) {
		if (!is_tegra_hypervisor_mode() &&
		    (drv_data->chip_data->adsp_os_config_hwmbox != 0)) {
			/* Set ADSP to do decompression */
			uint32_t val = (ADSP_CONFIG_DECOMPRESS_EN <<
						ADSP_CONFIG_DECOMPRESS_SHIFT);

			/* Write decompr enable flag only once */
			hwmbox_writel(drv_data, val,
				drv_data->chip_data->adsp_os_config_hwmbox);
		}
	}

	if (drv_data->chip_data->adsp_boot_config_hwmbox != 0) {
		/*
		 * Pass boot config to ADSP, so firmware can do any
		 * specifc setings. E.g. firmware may set up AST in
		 * case booting is in non-secure mode.
		 * Presently this MBOX register encodes just the
		 * adsp_os_secload flag. It could be extended in
		 * future to pass more information.
		 */
		hwmbox_writel(drv_data,
			(uint32_t)drv_data->adsp_os_secload,
			drv_data->chip_data->adsp_boot_config_hwmbox);
	}

	if (drv_data->adsp_clk &&
	    (drv_data->chip_data->adsp_cpu_freq_hwmbox != 0)) {
		/* Pass CPU frequency */
		hwmbox_writel(drv_data,
			(uint32_t)clk_get_rate(drv_data->adsp_clk),
			drv_data->chip_data->adsp_cpu_freq_hwmbox);
	}
}

static int wait_for_adsp_os_load_complete(struct nvadsp_os_data *priv)
{
	struct device *dev = &priv->pdev->dev;
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(priv->pdev);
	struct nvadsp_handle *nvadsp_handle = &drv_data->nvadsp_handle;
	uint32_t timeout, data;
	status_t ret;

	timeout = drv_data->adsp_load_timeout;
	if (!timeout)
		timeout = ADSP_OS_LOAD_TIMEOUT;

	ret = nvadsp_handle->mbox_recv(nvadsp_handle, &priv->adsp_com_mbox,
					&data, true, timeout);
	if (ret) {
		dev_err(dev, "ADSP OS loading timed out\n");
		goto end;
	}
	dev_dbg(dev, "ADSP has been %s\n",
		data == ADSP_OS_BOOT_COMPLETE ? "BOOTED" : "RESUMED");

	switch (data) {
	case ADSP_OS_BOOT_COMPLETE:
		ret = load_adsp_static_apps(drv_data);
		break;
	case ADSP_OS_RESUME:
	default:
		break;
	}
end:
	return ret;
}

static int __nvadsp_os_start(struct nvadsp_os_data *priv)
{
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(priv->pdev);
	struct device *dev = &priv->pdev->dev;
	int ret = 0;

	dev_dbg(dev, "ADSP is booting on %s\n",
		drv_data->adsp_unit_fpga ? "UNIT-FPGA" : "SILICON");

	nvadsp_assert_adsp(drv_data);

	if (!drv_data->adsp_os_secload) {
		ret = nvadsp_set_boot_vec(drv_data);
		if (ret) {
			dev_err(dev, "failed to set boot vector\n");
			goto end;
		}
	}

	dev_dbg(dev, "Setting freqs\n");
	ret = nvadsp_set_boot_freqs(drv_data);
	if (ret) {
		dev_err(dev, "failed to set boot freqs\n");
		goto end;
	}

	nvadsp_set_config_hwmboxes(priv);

	dev_dbg(dev, "De-asserting adsp\n");
	ret = nvadsp_deassert_adsp(drv_data);
	if (ret) {
		dev_err(dev, "failed to deassert ADSP\n");
		goto end;
	}

	dev_dbg(dev, "Waiting for ADSP OS to boot up...\n");

	ret = wait_for_adsp_os_load_complete(priv);
	if (ret) {
		dev_err(dev, "Unable to start ADSP OS\n");
		goto end;
	}
	dev_dbg(dev, "ADSP OS boot up... Done!\n");

#ifdef CONFIG_TEGRA_ADSP_DFS
	ret = adsp_dfs_core_init(priv->pdev);
	if (ret) {
		dev_err(dev, "adsp dfs initialization failed\n");
		goto err;
	}
#endif

#ifdef CONFIG_TEGRA_ADSP_ACTMON
	ret = ape_actmon_init(priv->pdev);
	if (ret) {
		dev_err(dev, "ape actmon initialization failed\n");
		goto err;
	}
#endif

#ifdef CONFIG_TEGRA_ADSP_CPUSTAT
	ret = adsp_cpustat_init(priv->pdev);
	if (ret) {
		dev_err(dev, "adsp cpustat initialisation failed\n");
		goto err;
	}
#endif
end:
	return ret;

#if defined(CONFIG_TEGRA_ADSP_DFS) || defined(CONFIG_TEGRA_ADSP_CPUSTAT)
err:
	__nvadsp_os_stop(priv, true);
	return ret;
#endif
}

static void dump_adsp_logs(struct nvadsp_os_data *priv)
{
	int i = 0;
	char buff[DUMP_BUFF] = { };
	int buff_iter = 0;
	char last_char;
	struct nvadsp_debug_log *logger = &priv->logger;
	struct device *dev = &priv->pdev->dev;
	char *ptr = logger->debug_ram_rdr;

	dev_err(dev, "Dumping ADSP logs ........\n");

	for (i = 0; i < logger->debug_ram_sz; i++) {
		last_char = *(ptr + i);
		if ((last_char != EOT) && (last_char != 0)) {
			if ((last_char == '\n') || (last_char == '\r') ||
					(buff_iter == DUMP_BUFF)) {
				dev_err(dev, "[ADSP OS] %s\n", buff);
				memset(buff, 0, sizeof(buff));
				buff_iter = 0;
			} else {
				buff[buff_iter++] = last_char;
			}
		}
	}
	dev_err(dev, "End of ADSP log dump  .....\n");
}

#ifdef CONFIG_AGIC_EXT_APIS
static void print_agic_irq_states(struct nvadsp_os_data *priv)
{
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(priv->pdev);
	int start_irq = drv_data->chip_data->start_irq;
	int end_irq = drv_data->chip_data->end_irq;
	struct device *dev = &priv->pdev->dev;
	int i;

	for (i = start_irq; i < end_irq; i++) {
		dev_info(dev, "irq %d is %s and %s\n", i,
		tegra_agic_irq_is_pending(i) ?
			"pending" : "not pending",
		tegra_agic_irq_is_active(i) ?
			"active" : "not active");
	}
}
#endif // CONFIG_AGIC_EXT_APIS

static void _nvadsp_dump_adsp_sys(struct nvadsp_handle *nvadsp_handle)
{
	struct nvadsp_drv_data *drv_data =
				(struct nvadsp_drv_data *)nvadsp_handle;
	struct nvadsp_os_data *priv;

	if (!drv_data || !drv_data->pdev || !drv_data->os_priv) {
		pr_err("ADSP Driver is not initialized\n");
		return;
	}

	priv = drv_data->os_priv;

	dump_adsp_logs(priv);
	dump_mailbox_regs(drv_data);

	if (drv_data->dump_core_state)
		drv_data->dump_core_state(drv_data);

	if (priv->adma_dump_ch_reg)
		(*priv->adma_dump_ch_reg)();

#ifdef CONFIG_AGIC_EXT_APIS
	print_agic_irq_states(priv);
#endif
}

static void nvadsp_free_os_interrupts(struct nvadsp_os_data *priv)
{
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(priv->pdev);
	unsigned int wdt_virq = drv_data->agic_irqs[WDT_VIRQ];
	unsigned int wfi_virq = drv_data->agic_irqs[WFI_VIRQ];
	struct device *dev = &priv->pdev->dev;

	devm_free_irq(dev, wdt_virq, priv);

	if (!(drv_data->chip_data->no_wfi_irq))
		devm_free_irq(dev, wfi_virq, priv);
}

static int nvadsp_setup_os_interrupts(struct nvadsp_os_data *priv)
{
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(priv->pdev);
	unsigned int wdt_virq = drv_data->agic_irqs[WDT_VIRQ];
	unsigned int wfi_virq = drv_data->agic_irqs[WFI_VIRQ];
	struct device *dev = &priv->pdev->dev;
	int ret;

	ret = devm_request_irq(dev, wdt_virq, adsp_wdt_handler,
			IRQF_TRIGGER_RISING, "adsp watchdog", priv);
	if (ret) {
		dev_err(dev, "failed to get adsp watchdog interrupt\n");
		goto end;
	}

	if (!(drv_data->chip_data->no_wfi_irq)) {
		ret = devm_request_irq(dev, wfi_virq, adsp_wfi_handler,
				IRQF_TRIGGER_RISING, "adsp wfi", priv);
		if (ret) {
			dev_err(dev, "cannot request for wfi interrupt\n");
			goto free_interrupts;
		}
	}

 end:

	return ret;

 free_interrupts:
	nvadsp_free_os_interrupts(priv);
	return ret;
}

static void free_interrupts(struct nvadsp_os_data *priv)
{
	nvadsp_free_os_interrupts(priv);
	nvadsp_free_hwmbox_interrupts(priv->pdev);
	nvadsp_free_amc_interrupts(priv->pdev);
}

static int setup_interrupts(struct nvadsp_os_data *priv)
{
	int ret;

	ret = nvadsp_setup_os_interrupts(priv);
	if (ret)
		goto err;

	ret = nvadsp_setup_hwmbox_interrupts(priv->pdev);
	if (ret)
		goto free_os_interrupts;
	ret = nvadsp_setup_amc_interrupts(priv->pdev);
	if (ret)
		goto free_hwmbox_interrupts;

	return ret;

 free_hwmbox_interrupts:
	nvadsp_free_hwmbox_interrupts(priv->pdev);
 free_os_interrupts:
	nvadsp_free_os_interrupts(priv);
 err:
	return ret;
}

static void _nvadsp_set_adma_dump_reg(struct nvadsp_handle *nvadsp_handle,
					void (*cb_adma_regdump)(void))
{
	struct nvadsp_drv_data *drv_data =
				(struct nvadsp_drv_data *)nvadsp_handle;
	struct nvadsp_os_data *priv;

	if (!drv_data || !drv_data->pdev || !drv_data->os_priv) {
		pr_err("ADSP Driver is not initialized\n");
		return;
	}

	priv = drv_data->os_priv;

	priv->adma_dump_ch_reg = cb_adma_regdump;
	pr_info("%s: callback for adma reg dump is sent to %p\n",
		__func__, priv->adma_dump_ch_reg);
}

static int _nvadsp_os_start(struct nvadsp_handle *nvadsp_handle)
{
	struct nvadsp_drv_data *drv_data =
				(struct nvadsp_drv_data *)nvadsp_handle;
	struct nvadsp_os_data *priv;
	struct device *dev;
	int ret = 0;

	if (!drv_data || !drv_data->pdev || !drv_data->os_priv) {
		pr_err("ADSP Driver is not initialized\n");
		ret = -EINVAL;
		goto end;
	}

	priv = drv_data->os_priv;
	dev = &priv->pdev->dev;

	/* check if fw is loaded then start the adsp os */
	if (!priv->adsp_os_fw_loaded) {
		dev_err(dev, "Call to nvadsp_os_load not made\n");
		ret = -EINVAL;
		goto end;
	}

	mutex_lock(&priv->os_run_lock);
	/* if adsp is started/running exit gracefully */
	if (priv->os_running)
		goto unlock;

#ifdef CONFIG_PM
	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		goto unlock;
#endif
	ret = setup_interrupts(priv);
	if (ret < 0)
		goto unlock;

	ret = __nvadsp_os_start(priv);
	if (ret) {
		priv->os_running = drv_data->adsp_os_running = false;
		/* if start fails call pm suspend of adsp driver */
		dev_err(dev, "adsp failed to boot with ret = %d\n", ret);
		_nvadsp_dump_adsp_sys(nvadsp_handle);
		free_interrupts(priv);
#ifdef CONFIG_PM
		pm_runtime_put_sync(dev);
#endif
		goto unlock;

	}
	priv->cold_start = false;
	priv->os_running = drv_data->adsp_os_running = true;
	priv->num_start++;
#if defined(CONFIG_TEGRA_ADSP_FILEIO)
	if ((drv_data->adsp_os_secload) && (!drv_data->adspff_init)) {
		int adspff_status = adspff_init(priv->pdev);

		if (adspff_status) {
			if (adspff_status != -ENOENT) {
				priv->os_running = drv_data->adsp_os_running = false;
				dev_err(dev,
					"adsp boot failed at adspff init with ret = %d",
					adspff_status);
				_nvadsp_dump_adsp_sys(nvadsp_handle);
				free_interrupts(priv);
#ifdef CONFIG_PM
				pm_runtime_put_sync(dev);
#endif
				ret = adspff_status;
				goto unlock;
			}
		} else
			drv_data->adspff_init = true;
	}
#endif

#ifdef CONFIG_TEGRA_ADSP_LPTHREAD
	if (!drv_data->lpthread_initialized) {
		ret = adsp_lpthread_entry(priv->pdev);
		if (ret)
			dev_err(dev, "adsp_lpthread_entry failed ret = %d\n",
					ret);
	}
#endif

	drv_data->adsp_os_suspended = false;
#ifdef CONFIG_DEBUG_FS
	wake_up(&priv->logger.wait_queue);
#endif

#ifdef CONFIG_TEGRA_ADSP_LPTHREAD
	adsp_lpthread_set_suspend(drv_data->adsp_os_suspended);
#endif

unlock:
	mutex_unlock(&priv->os_run_lock);
end:
	return ret;
}

static bool nvadsp_check_wfi_status(struct nvadsp_drv_data *drv_data)
{
	if (drv_data->check_wfi_status)
		return drv_data->check_wfi_status(drv_data);

	return true;
}

static int __nvadsp_os_suspend(struct nvadsp_os_data *priv)
{
	struct device *dev = &priv->pdev->dev;
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(priv->pdev);
	struct nvadsp_handle *nvadsp_handle = &drv_data->nvadsp_handle;
	unsigned long wfi_timeout;
	int ret;
	bool status = false;

#ifdef CONFIG_TEGRA_ADSP_ACTMON
	ape_actmon_exit(priv->pdev);
#endif

#ifdef CONFIG_TEGRA_ADSP_DFS
	adsp_dfs_core_exit(priv->pdev);
#endif

#ifdef CONFIG_TEGRA_ADSP_CPUSTAT
	adsp_cpustat_exit(priv->pdev);
#endif

	ret = nvadsp_handle->mbox_send(nvadsp_handle, &priv->adsp_com_mbox,
				ADSP_OS_SUSPEND,
				NVADSP_MBOX_SMSG, true, UINT_MAX);
	if (ret) {
		dev_err(dev, "failed to send with adsp com mbox\n");
		goto out;
	}

	dev_dbg(dev, "Waiting for ADSP OS suspend...\n");

	if (!(drv_data->chip_data->no_wfi_irq)) {
		ret = wait_for_completion_timeout(&priv->entered_wfi,
			msecs_to_jiffies(ADSP_WFI_TIMEOUT));
		if (WARN_ON(ret <= 0)) {
			dev_err(dev, "Unable to suspend ADSP OS err = %d\n", ret);
			ret = (ret < 0) ? ret : -ETIMEDOUT;
			goto out;
		}
	}

	wfi_timeout = jiffies + msecs_to_jiffies(ADSP_WFI_TIMEOUT);
	do {
		status = nvadsp_check_wfi_status(drv_data);
		mdelay(2);
	} while (time_before(jiffies, wfi_timeout) && !status);

	if (!status) {
		dev_err(dev, "Core WFI failed\n");
		ret = -EDEADLK;
		goto out;
	}

	ret = 0;
	dev_dbg(dev, "ADSP OS suspended!\n");

	drv_data->adsp_os_suspended = true;

#ifdef CONFIG_TEGRA_ADSP_LPTHREAD
	adsp_lpthread_set_suspend(drv_data->adsp_os_suspended);
#endif

	nvadsp_assert_adsp(drv_data);

 out:
	return ret;
}

static void __nvadsp_os_stop(struct nvadsp_os_data *priv, bool reload)
{
	const struct firmware *fw = priv->os_firmware;
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(priv->pdev);
	struct device *dev = &priv->pdev->dev;
	struct nvadsp_handle *nvadsp_handle = &drv_data->nvadsp_handle;
	int err = 0;

#ifdef CONFIG_TEGRA_ADSP_ACTMON
	ape_actmon_exit(priv->pdev);
#endif

#ifdef CONFIG_TEGRA_ADSP_DFS
	adsp_dfs_core_exit(priv->pdev);
#endif

#ifdef CONFIG_TEGRA_ADSP_CPUSTAT
	adsp_cpustat_exit(priv->pdev);
#endif
#if defined(CONFIG_TEGRA_ADSP_FILEIO)
	if (drv_data->adspff_init) {
		adspff_exit();
		drv_data->adspff_init = false;
	}
#endif

	err = nvadsp_handle->mbox_send(nvadsp_handle, &priv->adsp_com_mbox,
				ADSP_OS_STOP,
				NVADSP_MBOX_SMSG, true, UINT_MAX);
	if (err)
		dev_err(dev, "failed to send stop msg to adsp\n");

	if (!(drv_data->chip_data->no_wfi_irq)) {
		err = wait_for_completion_timeout(&priv->entered_wfi,
			msecs_to_jiffies(ADSP_WFI_TIMEOUT));
		if (err <= 0)
			err = (err < 0) ? err : -ETIMEDOUT;
	} else {
		unsigned long wfi_timeout;
		bool status = false;

		wfi_timeout = jiffies + msecs_to_jiffies(ADSP_WFI_TIMEOUT);
		do {
			status = nvadsp_check_wfi_status(drv_data);
			mdelay(2);
		} while (time_before(jiffies, wfi_timeout) && !status);

		if (!status)
			err = -EDEADLK;
	}

	/*
	 * ADSP needs to be in WFI/WFE state to properly reset it.
	 * However, when ADSPOS is getting stopped on error path,
	 * it cannot gaurantee that ADSP is in WFI/WFE state.
	 * Reset it in either case. On failure, whole APE reset is
	 * required (happens on next APE power domain cycle).
	 */
	nvadsp_assert_adsp(drv_data);

	/* Don't reload ADSPOS if ADSP state is not WFI/WFE */
	if (WARN_ON(err < 0)) {
		dev_err(dev, "%s: unable to enter wfi state err = %d\n",
			__func__, err);
		goto end;
	}

	if (reload && !drv_data->adsp_os_secload) {
		struct nvadsp_debug_log *logger = &priv->logger;

#ifdef CONFIG_DEBUG_FS
		wake_up(&logger->wait_queue);
		/* wait for LOGGER_TIMEOUT to complete filling the buffer */
		wait_for_completion_timeout(&logger->complete,
			msecs_to_jiffies(LOGGER_COMPLETE_TIMEOUT));
#endif
		/*
		 * move ram iterator to 0, since after restart the iterator
		 * will be pointing to initial position of start.
		 */
		logger->debug_ram_rdr[0] = EOT;
		logger->ram_iter = 0;
		/* load a fresh copy of adsp.elf */
		if (nvadsp_os_elf_load(priv, fw))
			dev_err(dev, "failed to reload %s\n",
				drv_data->adsp_elf);
	}

 end:
	return;
}

static void _nvadsp_os_stop(struct nvadsp_handle *nvadsp_handle)
{
	struct nvadsp_drv_data *drv_data =
				(struct nvadsp_drv_data *)nvadsp_handle;
	struct nvadsp_os_data *priv;
	struct device *dev;

	if (!drv_data || !drv_data->pdev || !drv_data->os_priv) {
		pr_err("ADSP Driver is not initialized\n");
		return;
	}

	priv = drv_data->os_priv;
	dev = &priv->pdev->dev;

	mutex_lock(&priv->os_run_lock);
	/* check if os is running else exit */
	if (!priv->os_running)
		goto end;

	__nvadsp_os_stop(priv, true);

	priv->os_running = drv_data->adsp_os_running = false;

	free_interrupts(priv);
#ifdef CONFIG_PM
	if (pm_runtime_put_sync(dev) < 0)
		dev_err(dev, "failed in pm_runtime_put_sync\n");
#endif
end:
	mutex_unlock(&priv->os_run_lock);
}

static int _nvadsp_os_suspend(struct nvadsp_handle *nvadsp_handle)
{
	struct nvadsp_drv_data *drv_data =
				(struct nvadsp_drv_data *)nvadsp_handle;
	struct nvadsp_os_data *priv;
	struct device *dev;
	int ret = -EINVAL;

	if (!drv_data || !drv_data->pdev || !drv_data->os_priv) {
		pr_err("ADSP Driver is not initialized\n");
		goto end;
	}

	priv = drv_data->os_priv;
	dev = &priv->pdev->dev;

	mutex_lock(&priv->os_run_lock);
	/* check if os is running else exit */
	if (!priv->os_running) {
		ret = 0;
		goto unlock;
	}
	ret = __nvadsp_os_suspend(priv);
	if (!ret) {
#ifdef CONFIG_PM
		free_interrupts(priv);
		ret = pm_runtime_put_sync(dev);
		if (ret < 0)
			dev_err(dev, "failed in pm_runtime_put_sync\n");
#endif
		priv->os_running = drv_data->adsp_os_running = false;
	} else {
		dev_err(dev, "suspend failed with %d\n", ret);
		_nvadsp_dump_adsp_sys(nvadsp_handle);
	}
unlock:
	mutex_unlock(&priv->os_run_lock);
end:
	return ret;
}

static void nvadsp_os_restart(struct work_struct *work)
{
#ifdef CONFIG_AGIC_EXT_APIS
	struct nvadsp_os_data *data =
		container_of(work, struct nvadsp_os_data, restart_os_work);
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(data->pdev);
	unsigned int wdt_virq = drv_data->agic_irqs[WDT_VIRQ];
	int wdt_irq = drv_data->chip_data->wdt_irq;
	struct device *dev = &data->pdev->dev;

	disable_irq(wdt_virq);
	dump_adsp_sys();
	nvadsp_os_stop();

	if (tegra_agic_irq_is_active(wdt_irq)) {
		dev_info(dev, "wdt interrupt is active hence clearing\n");
		tegra_agic_clear_active(wdt_irq);
	}

	if (tegra_agic_irq_is_pending(wdt_irq)) {
		dev_info(dev, "wdt interrupt is pending hence clearing\n");
		tegra_agic_clear_pending(wdt_irq);
	}

	dev_info(dev, "wdt interrupt is not pending or active...enabling\n");
	enable_irq(wdt_virq);

	data->adsp_num_crashes++;
	if (data->adsp_num_crashes >= ALLOWED_CRASHES) {
		/* making pdev NULL so that externally start is not called */
		data->pdev = NULL;
		dev_crit(dev, "ADSP has crashed too many times(%d)\n",
			 data->adsp_num_crashes);
		return;
	}

	if (nvadsp_os_start())
		dev_crit(dev, "Unable to restart ADSP OS\n");
#endif // CONFIG_AGIC_EXT_APIS
}

static irqreturn_t adsp_wfi_handler(int irq, void *arg)
{
	struct nvadsp_os_data *data = arg;
	struct device *dev = &data->pdev->dev;

	dev_dbg(dev, "%s\n", __func__);
	complete(&data->entered_wfi);

	return IRQ_HANDLED;
}

static irqreturn_t adsp_wdt_handler(int irq, void *arg)
{
	struct nvadsp_os_data *data = arg;
	struct nvadsp_drv_data *drv_data;
	struct device *dev = &data->pdev->dev;

	drv_data = platform_get_drvdata(data->pdev);

	drv_data->adsp_crashed = true;
	wake_up_interruptible(&drv_data->adsp_health_waitq);

	if (!drv_data->adsp_unit_fpga) {
		dev_crit(dev, "ADSP OS Hanged or Crashed! Restarting...\n");
		schedule_work(&data->restart_os_work);
	} else {
		dev_crit(dev, "ADSP OS Hanged or Crashed!\n");
	}
	return IRQ_HANDLED;
}

static void _nvadsp_get_os_version(struct nvadsp_handle *nvadsp_handle,
				char *buf, int buf_size)
{
	struct nvadsp_drv_data *drv_data =
				(struct nvadsp_drv_data *)nvadsp_handle;
	struct nvadsp_shared_mem *shared_mem;
	struct nvadsp_os_info *os_info;

	memset(buf, 0, buf_size);

	if (!drv_data || !drv_data->pdev || !drv_data->os_priv)
		return;

	shared_mem = drv_data->shared_adsp_os_data;
	if (shared_mem) {
		os_info = &shared_mem->os_info;
		strscpy(buf, os_info->version, buf_size);
	} else {
		strscpy(buf, "unavailable", buf_size);
	}
}

#ifdef CONFIG_DEBUG_FS
static int show_os_version(struct seq_file *s, void *data)
{
	struct nvadsp_os_data *priv = s->private;
	struct nvadsp_drv_data *drv_data;
	struct nvadsp_handle *nvadsp_handle;
	char ver_buf[MAX_OS_VERSION_BUF] = "";

	drv_data = platform_get_drvdata(priv->pdev);
	nvadsp_handle = &drv_data->nvadsp_handle;

	_nvadsp_get_os_version(nvadsp_handle, ver_buf, MAX_OS_VERSION_BUF);
	seq_printf(s, "version=\"%s\"\n", ver_buf);

	return 0;
}

static int os_version_open(struct inode *inode, struct file *file)
{
	return single_open(file, show_os_version, inode->i_private);
}

static const struct file_operations version_fops = {
	.open = os_version_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

#define RO_MODE S_IRUSR

static int adsp_create_os_version(struct nvadsp_os_data *priv,
				struct dentry *adsp_debugfs_root)
{
	struct device *dev = &priv->pdev->dev;
	struct dentry *d;

	d = debugfs_create_file("adspos_version", RO_MODE, adsp_debugfs_root,
				priv, &version_fops);
	if (!d) {
		dev_err(dev, "failed to create adsp_version\n");
		return -EINVAL;
	}
	return 0;
}

static int adsp_health_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static __poll_t adsp_health_poll(struct file *file,
			poll_table *wait)
{
	struct nvadsp_os_data *priv = file->private_data;
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(priv->pdev);

	poll_wait(file, &drv_data->adsp_health_waitq, wait);

	if (drv_data->adsp_crashed)
		return POLLIN | POLLRDNORM;

	return 0;
}

static const struct file_operations adsp_health_fops = {
	.open = adsp_health_open,
	.poll = adsp_health_poll,
};

static int adsp_create_adsp_health(struct nvadsp_os_data *priv,
				struct dentry *adsp_debugfs_root)
{
	struct device *dev = &priv->pdev->dev;
	struct dentry *d;

	d = debugfs_create_file("adsp_health", RO_MODE, adsp_debugfs_root,
				priv, &adsp_health_fops);
	if (!d) {
		dev_err(dev, "failed to create adsp_health\n");
		return -EINVAL;
	}
	return 0;
}
#endif

static ssize_t tegrafw_read_adsp(struct device *dev,
				char *data, size_t size)
{
	/* TBD - address the NULL below */
	_nvadsp_get_os_version(NULL, data, size);
	return strlen(data);
}

int nvadsp_os_probe(struct platform_device *pdev)
{
	struct nvadsp_drv_data *drv_data = platform_get_drvdata(pdev);
	struct nvadsp_handle *nvadsp_handle = &drv_data->nvadsp_handle;
	struct device *dev = &pdev->dev;
	struct nvadsp_os_data *priv;
	uint16_t com_mid = ADSP_COM_MBOX_ID;
	int ret = 0;

	priv = devm_kzalloc(dev,
			sizeof(struct nvadsp_os_data), GFP_KERNEL);
	if (!priv) {
		dev_err(dev, "Failed to allocate os_priv\n");
		ret = -ENOMEM;
		goto end;
	}
	drv_data->os_priv = priv;

	priv->adsp_os_addr = drv_data->adsp_mem[ADSP_OS_ADDR];
	priv->adsp_os_size = drv_data->adsp_mem[ADSP_OS_SIZE];
	priv->app_alloc_addr = drv_data->adsp_mem[ADSP_APP_ADDR];
	priv->app_size = drv_data->adsp_mem[ADSP_APP_SIZE];

	priv->cold_start = true;
	init_completion(&priv->entered_wfi);

	if (of_device_is_compatible(dev->of_node, "nvidia,tegra210-adsp")) {
		drv_data->assert_adsp = __assert_adsp;
		drv_data->deassert_adsp = __deassert_adsp;
	}

	ret = nvadsp_os_init(pdev);
	if (ret) {
		dev_err(dev, "failed to init os\n");
		goto end;
	}

	ret = nvadsp_handle->mbox_open(nvadsp_handle, &priv->adsp_com_mbox,
				&com_mid, "adsp_com_mbox", NULL, NULL);
	if (ret) {
		dev_err(dev, "failed to open adsp com mbox\n");
		goto end;
	}

	INIT_WORK(&priv->restart_os_work, nvadsp_os_restart);
	mutex_init(&priv->fw_load_lock);
	mutex_init(&priv->os_run_lock);

	priv->pdev = pdev;
#ifdef CONFIG_DEBUG_FS
	priv->logger.dev = &pdev->dev;
	if (adsp_create_debug_logger(priv, drv_data->adsp_debugfs_root))
		dev_err(dev, "unable to create adsp debug logger file\n");

	priv->console.dev = &pdev->dev;
	if (adsp_create_cnsl(drv_data->adsp_debugfs_root, &priv->console))
		dev_err(dev, "unable to create adsp console file\n");

	if (adsp_create_os_version(priv, drv_data->adsp_debugfs_root))
		dev_err(dev, "unable to create adsp_version file\n");

	if (adsp_create_adsp_health(priv, drv_data->adsp_debugfs_root))
		dev_err(dev, "unable to create adsp_health file\n");

	drv_data->adsp_crashed = false;
	init_waitqueue_head(&drv_data->adsp_health_waitq);

#endif /* CONFIG_DEBUG_FS */

	devm_tegrafw_register(dev, NULL, TFW_DONT_CACHE,
			tegrafw_read_adsp, NULL);
end:
	if (ret == 0) {
		nvadsp_handle->get_os_version     = _nvadsp_get_os_version;
		nvadsp_handle->os_load            = _nvadsp_os_load;
		nvadsp_handle->os_start           = _nvadsp_os_start;
		nvadsp_handle->os_suspend         = _nvadsp_os_suspend;
		nvadsp_handle->os_stop            = _nvadsp_os_stop;
		nvadsp_handle->set_adma_dump_reg  = _nvadsp_set_adma_dump_reg;
		nvadsp_handle->dump_adsp_sys      = _nvadsp_dump_adsp_sys;
		nvadsp_handle->alloc_coherent     = _nvadsp_alloc_coherent;
		nvadsp_handle->free_coherent      = _nvadsp_free_coherent;
	}

	return ret;
}
