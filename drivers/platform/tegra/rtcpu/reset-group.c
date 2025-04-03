// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2023-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "reset-group.h"

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/reset.h>
#include <linux/module.h>

struct camrtc_reset_group {
	struct device *device;
	const char *group_name;
	int nresets;
	struct reset_control *resets[];
};

/**
 * @brief Releases resources associated with a reset group
 *
 * This function is called when a reset group is being destroyed.
 * It performs the following operations:
 * - Iterates through all resets in the group
 * - For each reset, if it exists, calls @ref reset_control_put() to release it
 *
 * @param[in] dev  Pointer to the device
 *                 Valid value: non-NULL
 * @param[in] res  Pointer to the reset group resource
 *                 Valid value: non-NULL
 */
static void camrtc_reset_group_release(struct device *dev, void *res)
{
	const struct camrtc_reset_group *grp = res;
	int i;

	for (i = 0; i < grp->nresets; i++) {
		if (grp->resets[i])
			reset_control_put(grp->resets[i]);
	}
}

/**
 * @brief Gets a reset group based on device tree properties
 *
 * This function creates and initializes a reset group based on device tree properties.
 * It performs the following operations:
 * - Validates the device and its device tree node
 * - Determines the appropriate property name based on the group_name parameter
 * - Counts reset strings using @ref of_property_count_strings()
 * - Checks for size overflows using @ref __builtin_add_overflow()
 * - Allocates memory for the reset group using @ref devres_alloc()
 * - Initializes group properties (nresets, device, group_name)
 * - Iterates through all reset names in the group
 * - Gets each reset control using @ref of_reset_control_get()
 * - Adds the reset group to device resources using @ref devres_add()
 *
 * @param[in] dev         Pointer to the parent device
 *                        Valid value: non-NULL with valid device tree node
 * @param[in] group_name  Name of the reset group property
 *                        Valid value: NULL (defaults to "reset-names") or a valid string
 *
 * @retval struct camrtc_reset_group* Reset group on success
 * @retval ERR_PTR(-EINVAL)          If device or device tree node is invalid
 * @retval ERR_PTR(-ENOENT)          If reset names property is not found
 * @retval ERR_PTR(-EOVERFLOW)       If size calculations overflow
 * @retval ERR_PTR(-ENOMEM)          If memory allocation fails
 * @retval ERR_PTR(ret)              If reset control retrieval fails
 */
struct camrtc_reset_group *camrtc_reset_group_get(
	struct device *dev,
	const char *group_name)
{
	struct camrtc_reset_group *grp;
	struct device_node *np;
	const char *group_property;
	size_t group_name_len;
	int index;
	int ret;
	size_t sum = 0U;

	if (!dev || !dev->of_node)
		return ERR_PTR(-EINVAL);

	np = dev->of_node;

	group_property = group_name ? group_name : "reset-names";
	group_name_len = group_name ? strlen(group_name) : 0;

	ret = of_property_count_strings(np, group_property);
	if (ret < 0)
		return ERR_PTR(-ENOENT);

	if (__builtin_add_overflow(offsetof(struct camrtc_reset_group, resets[ret]),
			group_name_len, &sum)) {
		dev_err(dev, "Reset group size overflow\n");
		return ERR_PTR(-EOVERFLOW);
	}


	if (__builtin_add_overflow(sum, 1U, &sum)) {
		dev_err(dev, "Reset group size overflow\n");
		return ERR_PTR(-EOVERFLOW);
	}

	grp = devres_alloc(camrtc_reset_group_release,
			sum,
			GFP_KERNEL);
	if (!grp)
		return ERR_PTR(-ENOMEM);

	grp->nresets = ret;
	grp->device = dev;
	grp->group_name = (char *)&grp->resets[grp->nresets];
	memcpy((char *)grp->group_name, group_name, group_name_len);

	for (index = 0; index < grp->nresets; index++) {
		char const *name;
		struct reset_control *reset;

		ret = of_property_read_string_index(np, group_property,
						index, &name);
		if (ret < 0)
			goto error;

		reset = of_reset_control_get(np, name);
		if (IS_ERR(reset)) {
			ret = PTR_ERR(reset);
			goto error;
		}

		grp->resets[index] = reset;
	}

	devres_add(dev, grp);
	return grp;

error:
	devres_free(grp);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(camrtc_reset_group_get);

/**
 * @brief Reports an error related to a reset in a reset group
 *
 * This function reports an error related to a specific reset in a reset group.
 * It performs the following operations:
 * - Attempts to get the reset name from the device tree using @ref of_property_read_string_index()
 * - Prints a warning message about the operation that failed using @ref dev_warn()
 *
 * @param[in] grp    Pointer to the reset group
 *                   Valid value: non-NULL
 * @param[in] op     String describing the operation that failed
 *                   Valid value: non-NULL
 * @param[in] index  Index of the reset in the reset group
 *                   Valid range: [0, grp->nresets-1]
 * @param[in] error  Error code
 *                   Valid value: [INT_MIN, INT_MAX]
 */
static void camrtc_reset_group_error(
	const struct camrtc_reset_group *grp,
	char const *op,
	int index,
	int error)
{
	const char *name = "unnamed";
	int ret;

	ret = of_property_read_string_index(grp->device->of_node,
				grp->group_name, index, &name);
	if (ret < 0)
		dev_warn(grp->device, "Cannot find reset in %s\n", grp->group_name);

	dev_warn(grp->device, "%s reset %s (at %s[%d]): %d\n",
		op, name, grp->group_name, index, error);
}

/**
 * @brief Asserts all resets in a reset group
 *
 * This function asserts all resets in the specified reset group in reverse order.
 * It performs the following operations:
 * - Checks if the reset group is valid using @ref IS_ERR_OR_NULL()
 * - Iterates through all resets in the group in reverse order
 * - Asserts each reset using @ref reset_control_assert()
 * - Reports errors using @ref camrtc_reset_group_error() if a reset fails to assert
 *
 * @param[in] grp  Pointer to the reset group
 *                 Valid value: any value including NULL or error pointer
 */
void camrtc_reset_group_assert(const struct camrtc_reset_group *grp)
{
	int index, index0, err;

	if (IS_ERR_OR_NULL(grp))
		return;

	for (index = 1; index <= grp->nresets; index++) {
		index0 = grp->nresets - index;
		err = reset_control_assert(grp->resets[index0]);
		if (err < 0)
			camrtc_reset_group_error(grp, "assert", index0, err);
	}
}
EXPORT_SYMBOL_GPL(camrtc_reset_group_assert);

/**
 * @brief Deasserts all resets in a reset group
 *
 * This function deasserts all resets in the specified reset group in forward order.
 * It performs the following operations:
 * - Checks if the reset group is valid using null check and @ref IS_ERR()
 * - Iterates through all resets in the group in forward order
 * - Deasserts each reset using @ref reset_control_deassert()
 * - Reports errors using @ref camrtc_reset_group_error() if a reset fails to deassert
 * - Returns an error code if any reset fails to deassert
 *
 * @param[in] grp  Pointer to the reset group
 *                 Valid value: any value including NULL or error pointer
 *
 * @retval 0         On successful deassertion of all resets
 * @retval -ENODEV   If the reset group is an error pointer
 * @retval (int)     Error code from @ref reset_control_deassert()
 */
int camrtc_reset_group_deassert(const struct camrtc_reset_group *grp)
{
	int index, err;

	if (!grp)
		return 0;
	if (IS_ERR(grp))
		return -ENODEV;

	for (index = 0; index < grp->nresets; index++) {
		err = reset_control_deassert(grp->resets[index]);
		if (err < 0) {
			camrtc_reset_group_error(grp, "deassert", index, err);
			return err;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(camrtc_reset_group_deassert);
MODULE_LICENSE("GPL v2");
