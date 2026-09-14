// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/of_irq.h>
#include <linux/overflow.h>

#include <soc/tegra/virt/syscalls.h>
#include <soc/tegra/virt/tegra_hv.h>

#define ERR(...) pr_err("tegra_hv: " __VA_ARGS__)
#define INFO(...) pr_info("tegra_hv: " __VA_ARGS__)
#define DRV_NAME	"tegra_hv"

/* NVDVMS VM operation constants */
#define NVDVMS_VM_OP_MIN 300U
#define NVDVMS_VM_OP_MAX 363U
#define ENABLE_PM_SERVER_DT_NODE "enable_pm_server"

struct hv_mempool {
	struct tegra_hv_ivm_cookie ivmk;
	const struct ivc_mempool *mpd;
	struct mutex lock;
	int reserved;
};

struct tegra_hv_data {
	const struct ivc_info_page *info;
	int guestid;
	/* array with length info->nr_mempools */
	struct hv_mempool *mempools;
	struct class *hv_class;
	struct device_node *dev;
	uint32_t vcpu_affinity;
};

/*
 * Global HV state for read-only access by tegra_hv_... APIs
 *
 * This should be accessed only through tegra_hv_get_hvd().
 */
static struct tegra_hv_data *tegra_hv_data;

bool is_tegra_hypervisor_mode(void)
{
#ifdef CONFIG_OF
	return of_property_read_bool(of_chosen,
		"nvidia,tegra-hypervisor-mode");
#else
	return false;
#endif
}
EXPORT_SYMBOL(is_tegra_hypervisor_mode);

static struct tegra_hv_data *tegra_hv_get_hvd(void)
{
	if (!tegra_hv_data) {
		INFO("%s: not initialized yet\n", __func__);
		return ERR_PTR(-EPROBE_DEFER);
	} else {
		return tegra_hv_data;
	}
}

const struct ivc_info_page *tegra_hv_get_ivc_info(void)
{
	struct tegra_hv_data *hvd = tegra_hv_get_hvd();

	if (IS_ERR(hvd))
		return (void *)hvd;
	else
		return tegra_hv_data->info;
}
EXPORT_SYMBOL(tegra_hv_get_ivc_info);

int tegra_hv_get_vmid(void)
{
	struct tegra_hv_data *hvd = tegra_hv_get_hvd();
	int ret = 0;

	if (IS_ERR(hvd) || hvd->guestid >= NGUESTS_MAX)
		ret = -EINVAL;
	else
		ret = hvd->guestid;

	return ret;
}
EXPORT_SYMBOL(tegra_hv_get_vmid);

static ssize_t vmid_show(const struct class *class,
			 const struct class_attribute *attr, char *buf)
{
	struct tegra_hv_data *hvd = tegra_hv_get_hvd();

	BUG_ON(!hvd);

	if (hvd->guestid >= NGUESTS_MAX)
		hvd->guestid = -EINVAL;

	return snprintf(buf, PAGE_SIZE, "%d\n", hvd->guestid);
}
static CLASS_ATTR_RO(vmid);

static bool is_device_node_present(const char *node_name)
{
	struct device_node *node;
	bool node_present = false;

	node = of_find_node_by_name(NULL, node_name);
	if (node) {
		node_present = true;
		of_node_put(node);
		INFO("Found %s node in device tree\n", node_name);
	} else {
		INFO("No %s node found in device tree\n", node_name);
	}

	return node_present;
}

static ssize_t update_vm_op_store(const struct class *class,
		const struct class_attribute *attr, const char *buf, size_t count)
{
	unsigned int state;
	int ret;

	/* Parse unsigned int from user input */
	ret = kstrtouint(buf, 0, &state);
	if (ret < 0)
		return ret;

	/* Validate state is within VM operation boundaries */
	if (state < NVDVMS_VM_OP_MIN || state > NVDVMS_VM_OP_MAX) {
		pr_err("tegra_hv: Invalid VM_OP state %u\n", state);
		return -EINVAL;
	}

	/* Trigger HVC to update VM_OP transition completion */
	ret = hyp_guest_enter_vm_op(GUEST_ENTER_VM_OP_CMD, state);
	if (ret < 0) {
		pr_err("tegra_hv: VM_OP transition update failed\n");
		return ret;
	}

	pr_info("tegra_hv: VM_OP transition updated: %u\n", state);
	return count;
}
static CLASS_ATTR_WO(update_vm_op);

static long async_err_diagnostics(void *ptr)
{
	int32_t ret = 0;

	/*
	* Virtualization_System will check SMMU, CBB, and MC errors and report to
	* FSI if any errors are seen. This will be indirectly observed by User code
	* running on FSI.
	*/
	if (!hyp_smmu_diagnostic()) {
		ERR("hyp_smmu_diagnostic failed\n");
		ret = -1;
		goto fail;
	}

	if (!hyp_cbb_err_diagnostic()) {
		ERR("hyp_cbb_err_diagnostic failed\n");
		ret = -1;
		goto fail;
	}

	if (!hyp_mc_err_diagnostic()) {
		ERR("hyp_mc_err_diagnostic failed\n");
		ret = -1;
		goto fail;
	}

fail:
	return ret;
}

static ssize_t async_err_diagnostics_store(const struct class *class,
	const struct class_attribute *attr, const char *buf, size_t len)
{
	struct tegra_hv_data *hvd = tegra_hv_get_hvd();
	int val = 0;
	ssize_t ret = 0;

	ret = kstrtoint(buf, 10, &val);
	if (ret < 0) {
		pr_err("%s: Failed to convert string to uint %ld\n", __func__, ret);
		ret = -EINVAL;
		goto fail;
	}

	if (val != 1) {
		pr_err("%s: Unsupported value, %d\n", __func__, val);
		ret = -EINVAL;
		goto fail;
	}

	if (!cpu_online(hvd->vcpu_affinity)) {
		pr_warn("CPU %u is not online\n", hvd->vcpu_affinity);
		ret = -EINVAL;
		goto fail;
	}

	ret = work_on_cpu(hvd->vcpu_affinity, async_err_diagnostics, NULL);
fail:
	ret = ret ? ret : len;
	return ret;
}
static CLASS_ATTR_WO(async_err_diagnostics);

static void tegra_hv_cleanup(struct tegra_hv_data *hvd)
{
	BUG_ON(tegra_hv_data != NULL);

	kfree(hvd->mempools);
	hvd->mempools = NULL;

	if (hvd->hv_class) {
		class_remove_file(hvd->hv_class, &class_attr_vmid);

		if (is_device_node_present(ENABLE_PM_SERVER_DT_NODE))
			class_remove_file(hvd->hv_class, &class_attr_update_vm_op);

		class_remove_file(hvd->hv_class, &class_attr_async_err_diagnostics);

		class_destroy(hvd->hv_class);
		hvd->hv_class = NULL;
	}
}

/*
 * Safe validation for tainted data from ioremap_cache
 * Returns validated ivc_info_page pointer or NULL on failure
 * Fixes tainted_data_downcast and parm_assign violations
 */
static struct ivc_info_page *validate_ivc_info_page(void __iomem *mapped_mem, size_t size)
{
	struct ivc_info_page *info;
	uint32_t nr_areas, nr_mempools;

	if (!mapped_mem || size < sizeof(struct ivc_info_page)) {
		ERR("validate_ivc_info_page: Invalid parameters\n");
		return NULL;
	}

	/* Temporary access to validate header fields - still tainted at this point */
	info = (struct ivc_info_page *)mapped_mem;

	/* Read and validate critical fields that determine memory layout */
	nr_areas = info->nr_areas;
	nr_mempools = info->nr_mempools;


	/* Validate nr_areas against known safe bounds */
	if (nr_areas > MAX_NUM_GUESTS) {
		ERR("validate_ivc_info_page: Invalid nr_areas: %u\n", nr_areas);
		return NULL;
	}

	/* Validate nr_mempools against known safe bounds */
	if (nr_mempools > PCT_MAX_NUM_MEMPOOLS) {
		ERR("validate_ivc_info_page: Invalid nr_mempools: %u\n", nr_mempools);
		return NULL;
	}

	return info;
}

static int tegra_hv_setup(struct tegra_hv_data *hvd)
{
	void __iomem *mapped_mem;
	uint64_t info_page;
	uint32_t i;
	int ret;
	uint64_t mpsize = 0;

	hvd->dev = of_find_compatible_node(NULL, NULL, "nvidia,tegra-hv");
	if (!hvd->dev) {
		ERR("could not find hv node\n");
		return -ENODEV;
	}

	ret = hyp_read_gid(&hvd->guestid);
	if (ret != 0) {
		ERR("Failed to read guest id\n");
		return -ENODEV;
	}

	if (hvd->guestid >= NGUESTS_MAX) {
		ERR("Failed to read valid guest id\n");
		return -EINVAL;
	}

	hvd->hv_class = class_create("tegra_hv");
	if (IS_ERR(hvd->hv_class)) {
		ERR("class_create() failed\n");
		return PTR_ERR(hvd->hv_class);
	}

	ret = class_create_file(hvd->hv_class, &class_attr_vmid);
	if (ret != 0) {
		ERR("failed to create vmid file: %d\n", ret);
		return ret;
	}

	/* Create sysfs entry only if enable_pm_server DT node is present */
	if (is_device_node_present(ENABLE_PM_SERVER_DT_NODE)) {
		ret = class_create_file(hvd->hv_class, &class_attr_update_vm_op);
		if (ret != 0) {
			ERR("failed to create update_vm_op file: %d\n", ret);
			return ret;
		}
	}

	ret = class_create_file(hvd->hv_class, &class_attr_async_err_diagnostics);
	if (ret != 0) {
		ERR("failed to create async_err_diagnostics file: %d\n", ret);
		return ret;
	}

	ret = hyp_read_ivc_info(&info_page);
	if (ret != 0) {
		ERR("failed to obtain IVC info page: %d\n", ret);
		return ret;
	}

	mapped_mem = ioremap_cache(info_page, IVC_INFO_PAGE_SIZE);
	if (mapped_mem == NULL) {
		ERR("failed to map IVC info page (%llx)\n", info_page);
		return -ENOMEM;
	}

	/* Validate tainted data before assignment - fixes tainted_data_downcast and parm_assign */
	hvd->info = validate_ivc_info_page(mapped_mem, IVC_INFO_PAGE_SIZE);
	if (hvd->info == NULL) {
		ERR("IVC info page validation failed\n");
		iounmap(mapped_mem);
		return -EINVAL;
	}

	hvd->mempools =
		kzalloc(hvd->info->nr_mempools * sizeof(*hvd->mempools),
								GFP_KERNEL);
	if (hvd->mempools == NULL) {
		ERR("failed to allocate %u-entry mempools array\n",
				hvd->info->nr_mempools);
		return -ENOMEM;
	}

	/* Initialize mempools. */
	for (i = 0; i < hvd->info->nr_mempools; i++) {
		const struct ivc_mempool *mpd =
				&ivc_info_mempool_array(hvd->info)[i];
		struct tegra_hv_ivm_cookie *ivmk = &hvd->mempools[i].ivmk;

		hvd->mempools[i].mpd = mpd;
		mutex_init(&hvd->mempools[i].lock);

		ivmk->ipa = mpd->pa;
		ivmk->size = mpd->size;
		ivmk->peer_vmid = mpd->peer_vmid;

		if (check_add_overflow(mpsize, mpd->size, &mpsize)) {
			ERR("%s: operation got overflown.\n", __func__);
			return -EAGAIN;
		}

		BUG_ON(mpsize < mpd->size);
	}

	return 0;
}

struct tegra_hv_ivm_cookie *tegra_hv_mempool_reserve(unsigned int id)
{
	uint32_t i;
	struct hv_mempool *mempool;
	int reserved;

	if (!tegra_hv_data)
		return ERR_PTR(-EPROBE_DEFER);

	/* Locate a mempool with matching id. */
	for (i = 0; i < tegra_hv_data->info->nr_mempools; i++) {
		mempool = &tegra_hv_data->mempools[i];
		if (mempool->mpd->id == id)
			break;
	}

	if (i == tegra_hv_data->info->nr_mempools)
		return ERR_PTR(-ENODEV);

	mutex_lock(&mempool->lock);
	reserved = mempool->reserved;
	mempool->reserved = 1;
	mutex_unlock(&mempool->lock);

	return reserved ? ERR_PTR(-EBUSY) : &mempool->ivmk;
}
EXPORT_SYMBOL(tegra_hv_mempool_reserve);

int tegra_hv_mempool_unreserve(struct tegra_hv_ivm_cookie *ivmk)
{
	int reserved = 0;
	void *vaddr = NULL;
	struct hv_mempool *mempool;

	if (ivmk == NULL) {
		ERR("error: ivmk is NULL\n");
		return -EINVAL;
	}

	mempool = container_of(ivmk, struct hv_mempool, ivmk);
	/* Validate mempool structure */
	if (mempool == NULL || mempool->mpd == NULL) {
		ERR("error: invalid mempool structure\n");
		return -EINVAL;
	}

	/* Validate cookie consistency */
	if (ivmk->size != mempool->mpd->size || mempool->mpd->pa != ivmk->ipa) {
		ERR("error: invalid ivmk cookie\n");
		return -EINVAL;
	}

	/* Get VA for the IPA */
	vaddr = memremap(ivmk->ipa, ivmk->size, MEMREMAP_WB);
	if (vaddr == NULL) {
		ERR("error: failed to map memory\n");
		return -ENOMEM;
	}

	mutex_lock(&mempool->lock);
	/* clear mempool memory before unreserving mempool */
	memset(vaddr, 0, ivmk->size);
	reserved = mempool->reserved;
	mempool->reserved = 0;
	mutex_unlock(&mempool->lock);

	/* Clean up mapped memory */
	memunmap(vaddr);

	return reserved ? 0 : -EINVAL;
}
EXPORT_SYMBOL(tegra_hv_mempool_unreserve);

static long int read_mpidr(void *data)
{
	uint64_t mpidr;
	__asm volatile("MRS %0, MPIDR_EL1 " : "=r"(mpidr) :: "memory");
	return mpidr;
}

static int tegra_hv_probe(struct platform_device *pdev)
{
	struct tegra_hv_data *hvd;
	long int lcpu0_mpidr;
	long int mpidr = 0UL;
	uint32_t num_vcpus = num_present_cpus();
	uint32_t idx;
	int ret;

	if (!is_tegra_hypervisor_mode())
		return -ENODEV;

	hvd = kzalloc(sizeof(*hvd), GFP_KERNEL);
	if (!hvd) {
		ERR("failed to allocate hvd\n");
		return -ENOMEM;
	}

	hvd->vcpu_affinity = num_vcpus;
	lcpu0_mpidr = hyp_lcpu0_mpidr();
	for (idx = 0; idx < num_vcpus; idx++) {
		mpidr = work_on_cpu(idx, read_mpidr, NULL);
		if (mpidr == lcpu0_mpidr) {
			hvd->vcpu_affinity = idx;
			break;
		}
	}

	if (hvd->vcpu_affinity >= num_vcpus)
		INFO("%s: cpu affinity (%d) > online cpus (%d)\n", __func__,
						hvd->vcpu_affinity, num_online_cpus());

	ret = tegra_hv_setup(hvd);
	if (ret != 0) {
		/* Validate hvd->info before cleanup to satisfy MISRA C 2012 Directive 4.14 */
		if (hvd->info && hvd->info->nr_areas <= MAX_NUM_GUESTS) {
			tegra_hv_cleanup(hvd);
		}
		kfree(hvd);
		return ret;
	}

	/*
	 * Ensure that all contents of hvd are visible before they are visible
	 * to other threads.
	 */
	smp_wmb();

	BUG_ON(tegra_hv_data);
	tegra_hv_data = hvd;
	INFO("tegra_hv driver probed successfully\n");

	return 0;
}

static int tegra_hv_remove(struct platform_device *pdev)
{
	if (!is_tegra_hypervisor_mode())
		return 0;

	tegra_hv_cleanup(tegra_hv_data);
	kfree(tegra_hv_data);
	tegra_hv_data = NULL;

	INFO("tegra_hv driver removed successfully\n");

	return 0;
}

static const struct of_device_id tegra_hv_match[] = {
	{ .compatible = "nvidia,tegra-hv", },
	{},
};

static struct platform_driver tegra_hv_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(tegra_hv_match),
	},
	.probe = tegra_hv_probe,
	.remove = tegra_hv_remove,
};

static int __init tegra_hv_init(void)
{
	int ret;

	ret = platform_driver_register(&tegra_hv_driver);
	if (ret)
		pr_err("Error: tegra_hv driver registration failed\n");

	return ret;
}

static void __exit tegra_hv_exit(void)
{
	platform_driver_unregister(&tegra_hv_driver);
}

postcore_initcall(tegra_hv_init);
module_exit(tegra_hv_exit);

MODULE_AUTHOR("Manish Bhardwaj <mbhardwaj@nvidia.com>");
MODULE_DESCRIPTION("Tegra Hypervisor Driver");
MODULE_LICENSE("GPL v2");
