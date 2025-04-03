// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "device-group.h"

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/module.h>

struct camrtc_device_group {
	struct device *dev;
	char const *names_name;
	int ndevices;
	struct platform_device *devices[];
};

/**
 * @brief Gets a platform device from a device tree node
 *
 * This function retrieves a platform device from a device tree node specified by
 * a phandle property. It performs the following operations:
 * 1. Gets the device node using @ref of_parse_phandle()
 * 2. Checks if the device is available using @ref of_device_is_available()
 * 3. Finds the platform device using @ref of_find_device_by_node()
 * 4. Stores the device in the group's device array
 *
 * @param[in] grp   Pointer to the device group
 *                  Valid value: non-NULL
 * @param[in] dev   Pointer to the parent device
 *                  Valid value: non-NULL
 * @param[in] name  Name of the phandle property
 *                  Valid value: non-NULL
 * @param[in] index Index in the phandle array
 *                  Valid range: >= 0
 *
 * @retval 0 Device retrieved successfully or skipped if disabled
 */
static int get_grouped_device(struct camrtc_device_group *grp,
			struct device *dev, char const *name, int index)
{
	struct device_node *np;
	struct platform_device *pdev;

	np = of_parse_phandle(dev->of_node, name, index);
	if (np == NULL)
		return 0;

	if (!of_device_is_available(np)) {
		dev_info(dev, "%s[%u] is disabled\n", name, index);
		of_node_put(np);
		return 0;
	}

	pdev = of_find_device_by_node(np);
	of_node_put(np);

	if (pdev == NULL) {
		dev_warn(dev, "%s[%u] node has no device\n", name, index);
		return 0;
	}

	grp->devices[index] = pdev;

	return 0;
}

/**
 * @brief Releases resources associated with a device group
 *
 * This function is called to release resources when a device group is being destroyed.
 * It performs the following operations:
 * 1. Releases the reference to the parent device using @ref put_device()
 * 2. Iterates through all devices in the group
 * 3. Releases each platform device using @ref platform_device_put()
 *
 * @param[in] dev  Pointer to the parent device
 *                 Valid value: non-NULL
 * @param[in] res  Pointer to the device group resource
 *                 Valid value: non-NULL
 */
static void camrtc_device_group_release(struct device *dev, void *res)
{
	const struct camrtc_device_group *grp = res;
	int i;

	put_device(grp->dev);

	for (i = 0; i < grp->ndevices; i++)
		platform_device_put(grp->devices[i]);
}

/**
 * @brief Gets a device group based on device tree properties
 *
 * This function creates and initializes a device group based on device tree properties.
 * It performs the following operations:
 * 1. Validates the device and its device tree node
 * 2. Counts phandle arguments using @ref of_count_phandle_with_args()
 * 3. Allocates memory for the device group using @ref devres_alloc()
 * 4. Gets device references using @ref get_device()
 * 5. Retrieves each device in the group using @ref get_grouped_device()
 * 6. Adds the device group to device resources using @ref devres_add()
 *
 * @param[in] dev                  Pointer to the parent device
 *                                Valid value: non-NULL with valid device tree node
 * @param[in] property_name       Name of the device tree property containing device phandles
 *                                Valid value: non-NULL
 * @param[in] names_property_name Name of the device tree property containing device names
 *                                Valid value: non-NULL
 *
 * @retval struct camrtc_device_group* Device group on success
 * @retval ERR_PTR(-EINVAL)          If device or device tree node is invalid
 * @retval ERR_PTR(-ENOENT)          If no devices are found
 * @retval ERR_PTR(-ENOMEM)          If memory allocation fails
 * @retval ERR_PTR(err)              If device retrieval fails
 */
struct camrtc_device_group *camrtc_device_group_get(
	struct device *dev,
	char const *property_name,
	char const *names_property_name)
{
	int index, err;
	struct camrtc_device_group *grp;
	int ndevices;

	if (!dev || !dev->of_node)
		return ERR_PTR(-EINVAL);

	ndevices = of_count_phandle_with_args(dev->of_node,
			property_name, NULL);
	if (ndevices <= 0)
		return ERR_PTR(-ENOENT);

	grp = devres_alloc(camrtc_device_group_release,
			offsetof(struct camrtc_device_group, devices[ndevices]),
			GFP_KERNEL | __GFP_ZERO);
	if (!grp)
		return ERR_PTR(-ENOMEM);

	grp->dev = get_device(dev);
	grp->ndevices = ndevices;
	grp->names_name = names_property_name;

	for (index = 0; index < grp->ndevices; index++) {
		err = get_grouped_device(grp, dev, property_name, index);
		if (err) {
			devres_free(grp);
			return ERR_PTR(err);
		}
	}

	devres_add(dev, grp);
	return grp;
}
EXPORT_SYMBOL(camrtc_device_group_get);

/**
 * @brief Gets a reference to a platform device
 *
 * This function gets a reference to a platform device by incrementing its
 * reference count using @ref get_device() if the device is not NULL.
 *
 * @param[in] pdev  Pointer to the platform device
 *                  Valid value: any value including NULL
 *
 * @retval struct platform_device* The input platform device pointer
 */
static inline struct platform_device *platform_device_get(
	struct platform_device *pdev)
{
	if (pdev != NULL)
		get_device(&pdev->dev);
	return pdev;
}

/**
 * @brief Gets a platform device from a device group by name
 *
 * This function retrieves a platform device from a device group by matching
 * the device name in the device tree property. It performs the following operations:
 * 1. Validates the device group
 * 2. Checks if the names property exists
 * 3. Finds the device index using @ref of_property_match_string()
 * 4. Gets a reference to the device using @ref platform_device_get()
 *
 * @param[in] grp          Pointer to the device group
 *                         Valid value: non-NULL
 * @param[in] device_name  Name of the device to find
 *                         Valid value: non-NULL
 *
 * @retval struct platform_device* Device pointer on success
 * @retval ERR_PTR(-EINVAL)       If device group is NULL
 * @retval ERR_PTR(-ENOENT)       If names property is not found
 * @retval ERR_PTR(-ENODEV)       If device is not found or index is invalid
 */
struct platform_device *camrtc_device_get_byname(
	struct camrtc_device_group *grp,
	const char *device_name)
{
	int index;

	if (grp == NULL)
		return ERR_PTR(-EINVAL);
	if (grp->names_name == NULL)
		return ERR_PTR(-ENOENT);

	index = of_property_match_string(grp->dev->of_node, grp->names_name,
			device_name);
	if (index < 0)
		return ERR_PTR(-ENODEV);
	if (index >= grp->ndevices)
		return ERR_PTR(-ENODEV);

	return platform_device_get(grp->devices[index]);
}
EXPORT_SYMBOL(camrtc_device_get_byname);

MODULE_LICENSE("GPL v2");
