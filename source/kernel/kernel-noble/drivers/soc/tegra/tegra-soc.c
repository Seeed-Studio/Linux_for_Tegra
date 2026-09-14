#include <linux/init.h>
#include <linux/sys_soc.h>

#include <soc/tegra/common.h>
#include <soc/tegra/fuse.h>

#define TEGRA_SOC_MAJOR_REV_SHIFT	4
#define TEGRA_SOC_MAJOR_REV_MASK	0xf
#define TEGRA_SOC_ID_SHIFT		8
#define TEGRA_SOC_ID_MASK		0xff
#define TEGRA_SOC_MINOR_REV_SHIFT	16
#define TEGRA_SOC_MINOR_REV_MASK	0xf
#define TEGRA_SOC_PLATFORM_SHIFT	20
#define TEGRA_SOC_PLATFORM_MASK		0xf

enum tegra_soc_revision {
	TEGRA_SOC_REVISION_0 = 0,
	TEGRA_SOC_REVISION_1,
	TEGRA_SOC_REVISION_2,
	TEGRA_SOC_REVISION_3,
	TEGRA_SOC_REVISION_4,
	TEGRA_SOC_REVISION_5,
	TEGRA_SOC_REVISION_6,
	TEGRA_SOC_REVISION_7,
	TEGRA_SOC_REVISION_8,
	TEGRA_SOC_REVISION_9,
	TEGRA_SOC_REVISION_10,
	TEGRA_SOC_REVISION_11,
	TEGRA_SOC_REVISION_12,
	TEGRA_SOC_REVISION_13,
	TEGRA_SOC_REVISION_14,
	TEGRA_SOC_REVISION_15,
	TEGRA_SOC_REVISION_MAX,
};

enum tegra_soc_platform {
	TEGRA_SOC_PLATFORM_SILICON = 0,
	TEGRA_SOC_PLATFORM_QT,
	TEGRA_SOC_PLATFORM_SYSTEM_FPGA,
	TEGRA_SOC_PLATFORM_UNIT_FPGA,
	TEGRA_SOC_PLATFORM_ASIM_QT,
	TEGRA_SOC_PLATFORM_ASIM_LINSIM,
	TEGRA_SOC_PLATFORM_DSIM_ASIM_LINSIM,
	TEGRA_SOC_PLATFORM_VERIFICATION_SIMULATION,
	TEGRA_SOC_PLATFORM_VDK,
	TEGRA_SOC_PLATFORM_VSP,
	TEGRA_SOC_PLATFORM_MAX,
};

static const char *tegra_soc_revision_name[TEGRA_SOC_REVISION_MAX] = {
	[TEGRA_SOC_REVISION_0]		= "unknown",
	[TEGRA_SOC_REVISION_1]		= "A01",
	[TEGRA_SOC_REVISION_2]		= "A02",
	[TEGRA_SOC_REVISION_3]		= "A03",
	[TEGRA_SOC_REVISION_4]		= "unknown",
	[TEGRA_SOC_REVISION_5]		= "B01",
	[TEGRA_SOC_REVISION_6]		= "B02",
	[TEGRA_SOC_REVISION_7]		= "B03",
	[TEGRA_SOC_REVISION_8]		= "unknown",
	[TEGRA_SOC_REVISION_9]		= "C01",
	[TEGRA_SOC_REVISION_10]		= "C02",
	[TEGRA_SOC_REVISION_11]		= "C03",
	[TEGRA_SOC_REVISION_12]		= "unknown",
	[TEGRA_SOC_REVISION_13]		= "D01",
	[TEGRA_SOC_REVISION_14]		= "D02",
	[TEGRA_SOC_REVISION_15]		= "D03",
};

static const char *tegra_soc_platform_name[TEGRA_SOC_PLATFORM_MAX] = {
	[TEGRA_SOC_PLATFORM_SILICON]			= "Silicon",
	[TEGRA_SOC_PLATFORM_QT]				= "QT",
	[TEGRA_SOC_PLATFORM_SYSTEM_FPGA]		= "System FPGA",
	[TEGRA_SOC_PLATFORM_UNIT_FPGA]			= "Unit FPGA",
	[TEGRA_SOC_PLATFORM_ASIM_QT]			= "Asim QT",
	[TEGRA_SOC_PLATFORM_ASIM_LINSIM]		= "Asim Linsim",
	[TEGRA_SOC_PLATFORM_DSIM_ASIM_LINSIM]		= "Dsim Asim Linsim",
	[TEGRA_SOC_PLATFORM_VERIFICATION_SIMULATION]	= "Verification Simulation",
	[TEGRA_SOC_PLATFORM_VDK]			= "VDK",
	[TEGRA_SOC_PLATFORM_VSP]			= "VSP",
};

static ssize_t major_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	int major_rev;

	major_rev = (tegra_read_chipid() >> TEGRA_SOC_MAJOR_REV_SHIFT) & TEGRA_SOC_MAJOR_REV_MASK;
	return sprintf(buf, "%d\n", major_rev);
}

static DEVICE_ATTR_RO(major);

static ssize_t minor_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	int minor_rev;

	minor_rev = (tegra_read_chipid() >> TEGRA_SOC_MINOR_REV_SHIFT) & TEGRA_SOC_MINOR_REV_SHIFT;
	return sprintf(buf, "%d\n", minor_rev);
}

static DEVICE_ATTR_RO(minor);

static ssize_t platform_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	return sprintf(buf, "%d\n", tegra_get_platform());
}

static DEVICE_ATTR_RO(platform);

static struct attribute *tegra_soc_custom_attr[] = {
	&dev_attr_major.attr,
	&dev_attr_minor.attr,
	&dev_attr_platform.attr,
	NULL,
};

const struct attribute_group tegra_soc_custom_attr_group = {
	.attrs = tegra_soc_custom_attr,
};

static struct soc_device_attribute *tegra_soc_attr;
static struct soc_device *tegra_soc_dev;

static int __init tegra_soc_init(void)
{
	u32 miscreg = 0;
	u8 platform;
	u8 revision;
	u32 soc_id;

	/* Return if not running on Tegra. */
	if (!soc_is_tegra())
		return 0;

	miscreg = tegra_read_chipid();
	if (!miscreg)
		return -EPROBE_DEFER;

	platform = (miscreg >> TEGRA_SOC_PLATFORM_SHIFT) & TEGRA_SOC_PLATFORM_MASK;
	revision = (miscreg >> TEGRA_SOC_MINOR_REV_SHIFT) & TEGRA_SOC_MINOR_REV_MASK;

	/*
	 * FIXME: From Tegra234 onwards the SOC_ID consists of CHIP_ID + Major revision.
	 * So, soc_id should be (miscreg >> 4) & 0xfff, but most of the userspace
	 * applications and tests are written with an assumption that SOC_ID = CHIP_ID.
	 * This causes failures while identifying platforms from those userspace
	 * applications.
	 *
	 * For now, keep soc_id as (miscreg >> 8) & 0xff, until the userspace
	 * applications are patched.
	 */
	soc_id = (miscreg >> TEGRA_SOC_ID_SHIFT) & TEGRA_SOC_ID_MASK;

	tegra_soc_attr = kzalloc(sizeof(*tegra_soc_attr), GFP_KERNEL);
	if (!tegra_soc_attr)
		return -EINVAL;

	tegra_soc_attr->family = kasprintf(GFP_KERNEL, "Tegra");
	if (tegra_is_silicon())
		tegra_soc_attr->revision = kasprintf(GFP_KERNEL, "%s %s",
						tegra_soc_platform_name[platform],
						tegra_soc_revision_name[revision]);
	else
		tegra_soc_attr->revision = kasprintf(GFP_KERNEL, "%s",
						tegra_soc_platform_name[platform]);

	/*
	 * FIXME: Use hex value for SOC_ID as it is the most widely used notation
	 * for identifying chips.
	 *
	 * Fixing this would require patching all the userspace applications
	 * relying on SOC_ID.
	 */
	tegra_soc_attr->soc_id = kasprintf(GFP_KERNEL, "%u", soc_id);
	tegra_soc_attr->custom_attr_group = &tegra_soc_custom_attr_group;

	tegra_soc_dev = soc_device_register(tegra_soc_attr);
	if (IS_ERR(tegra_soc_dev)) {
		kfree(tegra_soc_attr->soc_id);
		kfree(tegra_soc_attr->revision);
		kfree(tegra_soc_attr->family);
		kfree(tegra_soc_attr);

		return PTR_ERR(tegra_soc_dev);
	}

	return 0;
}
device_initcall(tegra_soc_init);

static void __exit tegra_soc_exit(void)
{
	soc_device_unregister(tegra_soc_dev);

	kfree(tegra_soc_attr->soc_id);
	kfree(tegra_soc_attr->revision);
	kfree(tegra_soc_attr->family);
	kfree(tegra_soc_attr);
}
module_exit(tegra_soc_exit);
