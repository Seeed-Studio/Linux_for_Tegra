// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2022-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "clk-group.h"

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/limits.h>
#include <linux/module.h>

struct camrtc_clk_group {
	struct device *device;
	struct {
		struct clk *slow;
		struct clk *fast;
	} parents;
	u32 nclocks;
	struct {
		struct clk *clk;
		u32 slow;
		u32 fast;
	} clocks[];
};

/**
 * @brief Release function for the camera RTCPU clock group
 *
 * This function releases all the clocks in the clock group.
 * - Iterates through all clocks in the group
 * - Puts back each clock using @ref clk_put()
 * - Puts back the parent clocks (slow and fast)
 *
 * @param[in] dev  Pointer to the device
 *                 Valid value: non-NULL
 * @param[in] res  Pointer to the clock group resource
 *                 Valid value: non-NULL
 */
static void camrtc_clk_group_release(struct device *dev, void *res)
{
	const struct camrtc_clk_group *grp = res;
	int i;

	for (i = 0; i < grp->nclocks; i++) {
		if (grp->clocks[i].clk)
			clk_put(grp->clocks[i].clk);
	}

	if (grp->parents.slow)
		clk_put(grp->parents.slow);
	if (grp->parents.fast)
		clk_put(grp->parents.fast);
}

/**
 * @brief Retrieve a parent clock from device tree
 *
 * This function retrieves a parent clock using the device tree.
 * - Validates the input index
 * - Parses a phandle with arguments using @ref of_parse_phandle_with_args()
 * - Gets the clock from the provider using @ref of_clk_get_from_provider()
 * - Releases the node pointer using @ref of_node_put()
 * - Returns the clock via the return_clk parameter
 *
 * @param[in] np          Pointer to device node
 *                        Valid value: non-NULL
 * @param[in] index       Index of the parent clock
 *                        Valid range: >= 0
 * @param[out] return_clk Pointer to store the retrieved clock
 *                        Valid value: non-NULL
 *
 * @retval 0          On successful retrieval
 * @retval -EINVAL    If index is negative
 * @retval (int)      Error value returned by @ref of_parse_phandle_with_args()
 *                    or @ref of_clk_get_from_provider()
 */
static int camrtc_clk_group_get_parent(
	struct device_node *np,
	int index,
	struct clk **return_clk)
{
	struct of_phandle_args clkspec;
	struct clk *clk;
	int ret;

	if (index < 0)
		return -EINVAL;

	ret = of_parse_phandle_with_args(np,
			"nvidia,clock-parents", "#clock-cells", index,
			&clkspec);
	if (ret < 0)
		return ret;

	clk = of_clk_get_from_provider(&clkspec);
	of_node_put(clkspec.np);

	if (IS_ERR(clk))
		return PTR_ERR(clk);

	*return_clk = clk;

	return 0;
}

/**
 * @brief Get a clock group for a device
 *
 * This function creates and initializes a clock group for a device.
 * - Validates the input device
 * - Gets the device node
 * - Counts the number of clocks, clock rates, and parent clocks
 * - Allocates memory for the clock group using @ref devres_alloc()
 * - Gets each clock using @ref of_clk_get()
 * - Gets the clock rates for each clock
 * - Gets the parent clocks using @ref camrtc_clk_group_get_parent()
 * - Registers the resource with the device using @ref devres_add()
 *
 * @param[in] dev  Pointer to the device
 *                 Valid value: non-NULL
 *
 * @retval (struct camrtc_clk_group*)  Pointer to the clock group on success
 * @retval ERR_PTR(-EINVAL)            If device is invalid or does not have a device node
 * @retval ERR_PTR(-ENOENT)            If no clocks are found
 * @retval ERR_PTR(-ENOMEM)            If memory allocation fails
 * @retval ERR_PTR(ret)                Error returned by @ref camrtc_clk_group_get_parent()
 *                                     or @ref of_clk_get()
 */
struct camrtc_clk_group *camrtc_clk_group_get(
	struct device *dev)
{
	struct camrtc_clk_group *grp;
	struct device_node *np;
	int nclocks;
	int nrates;
	int nparents;
	u32 index;
	int ret;

	if (!dev || !dev->of_node)
		return ERR_PTR(-EINVAL);

	np = dev->of_node;

	nclocks = of_property_count_strings(np, "clock-names");
	if (nclocks < 0 || nclocks > (S32_MAX / sizeof(grp->clocks[0])))
		return ERR_PTR(-ENOENT);

	/* This has pairs of u32s: slow and fast rate for each clock */
	nrates = of_property_count_u64_elems(np, "nvidia,clock-rates");
	/* of_property_count_elems_of_size() already complains about this */
	if (nrates < 0)
		nrates = 0;

	nparents = of_count_phandle_with_args(np, "nvidia,clock-parents",
			"#clock-cells");
	if (nparents > 0 && nparents != 2)
		dev_warn(dev, "expecting exactly two \"%s\"\n",
			"nvidia,clock-parents");

	grp = devres_alloc(camrtc_clk_group_release,
			sizeof(*grp) +
			nclocks * sizeof(grp->clocks[0]),
			GFP_KERNEL);
	if (!grp)
		return ERR_PTR(-ENOMEM);

	grp->nclocks = nclocks;
	grp->device = dev;

	for (index = 0; index < nclocks; index++) {
		struct clk *clk;

		clk = of_clk_get(np, index);
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			goto error;
		}

		grp->clocks[index].clk = clk;

		if (index >= nrates)
			continue;

		if (of_property_read_u32_index(np, "nvidia,clock-rates",
					2 * index, &grp->clocks[index].slow))
			dev_warn(dev, "clock-rates property not found\n");
		if (of_property_read_u32_index(np, "nvidia,clock-rates",
					2 * index + 1, &grp->clocks[index].fast))
			dev_warn(dev, "clock-rates property not found\n");
	}

	if (nparents == 2) {
		ret = camrtc_clk_group_get_parent(np, 0, &grp->parents.slow);
		if (ret < 0)
			goto error;

		ret = camrtc_clk_group_get_parent(np, 1, &grp->parents.fast);
		if (ret < 0)
			goto error;
	}

	devres_add(dev, grp);
	return grp;

error:
	devres_free(grp);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(camrtc_clk_group_get);

/**
 * @brief Reports an error related to a clock in a clock group
 *
 * This function reports an error related to a specific clock in a clock group.
 * - Attempts to get the clock name from the device tree using @ref of_property_read_string_index()
 * - Prints a warning message about the operation that failed using @ref dev_warn()
 *
 * @param[in] grp    Pointer to the clock group
 *                   Valid value: non-NULL
 * @param[in] op     String describing the operation that failed
 *                   Valid value: non-NULL
 * @param[in] index  Index of the clock in the clock group
 *                   Valid range: [0, grp->nclocks-1]
 * @param[in] error  Error code
 *                   Valid value: any
 */
static void camrtc_clk_group_error(
	const struct camrtc_clk_group *grp,
	char const *op,
	int index,
	int error)
{
	const char *name = "unnamed";
	int ret;

	ret = of_property_read_string_index(grp->device->of_node,
				"clock-names", index, &name);

	if (ret < 0)
		dev_warn(grp->device, "Cannot find clock in clock-names\n");

	dev_warn(grp->device, "%s clk %s (at [%d]): failed (%d)\n",
				op, name, index, error);
}

/**
 * @brief Enable all clocks in a clock group
 *
 * This function enables all clocks in the specified clock group.
 * - Checks if the clock group is valid using @ref IS_ERR_OR_NULL()
 * - Iterates through all clocks in the group
 * - Prepares and enables each clock using @ref clk_prepare_enable()
 * - Reports errors using @ref camrtc_clk_group_error() if a clock fails to enable
 *
 * @param[in] grp  Pointer to the clock group
 *                 Valid value: non-NULL and not an error pointer
 *
 * @retval 0        On successful enablement of all clocks
 * @retval -ENODEV  If the clock group is invalid
 * @retval (int)    Error returned by @ref clk_prepare_enable()
 */
int camrtc_clk_group_enable(const struct camrtc_clk_group *grp)
{
	int index, err;

	if (IS_ERR_OR_NULL(grp))
		return -ENODEV;

	for (index = 0; index < grp->nclocks; index++) {
		err = clk_prepare_enable(grp->clocks[index].clk);
		if (err) {
			camrtc_clk_group_error(grp, "enable", index, err);
			return err;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(camrtc_clk_group_enable);

/**
 * @brief Disable all clocks in a clock group
 *
 * This function disables all clocks in the specified clock group.
 * - Checks if the clock group is valid using @ref IS_ERR_OR_NULL()
 * - Iterates through all clocks in the group
 * - Disables and unprepares each clock using @ref clk_disable_unprepare()
 *
 * @param[in] grp  Pointer to the clock group
 *                 Valid value: non-NULL and not an error pointer
 */
void camrtc_clk_group_disable(const struct camrtc_clk_group *grp)
{
	int index;

	if (IS_ERR_OR_NULL(grp))
		return;

	for (index = 0; index < grp->nclocks; index++)
		clk_disable_unprepare(grp->clocks[index].clk);
}
EXPORT_SYMBOL_GPL(camrtc_clk_group_disable);

/**
 * @brief Set the parent clock for all clocks in a clock group
 *
 * This function sets the specified parent clock for all clocks in the group.
 * - Checks if the parent clock is valid using @ref IS_ERR_OR_NULL()
 * - Iterates through all clocks in the group
 * - Sets the parent clock for each clock using @ref clk_set_parent()
 * - Reports errors using @ref pr_err() if parent clock cannot be set
 *
 * @param[in] grp     Pointer to the clock group
 *                    Valid value: non-NULL
 * @param[in] parent  Pointer to the parent clock
 *                    Valid value: non-NULL and not an error pointer
 */
static void camrtc_clk_group_set_parent(const struct camrtc_clk_group *grp,
		struct clk *parent)
{
	int index;
	int ret;

	if (IS_ERR_OR_NULL(parent))
		return;

	for (index = 0; index < grp->nclocks; index++) {
		ret = clk_set_parent(grp->clocks[index].clk, parent);
		if (ret < 0)
			pr_err("%s: unable to set parent clock %d\n",
				__func__, ret);
	}
}

/**
 * @brief Adjust clocks in a clock group to slow mode
 *
 * This function adjusts all clocks in the specified clock group to their slow
 * rates and sets the slow parent clock.
 * - Checks if the clock group is valid using @ref IS_ERR_OR_NULL()
 * - Iterates through all clocks in the group
 * - Sets the rate of each clock to its slow rate using @ref clk_set_rate() if
 *   the slow rate is non-zero
 * - Sets the parent clock to the slow parent using @ref camrtc_clk_group_set_parent()
 *
 * @param[in] grp  Pointer to the clock group
 *                 Valid value: non-NULL and not an error pointer
 *
 * @retval 0        On successful adjustment
 * @retval -ENODEV  If the clock group is invalid
 */
int camrtc_clk_group_adjust_slow(const struct camrtc_clk_group *grp)
{
	int index;

	if (IS_ERR_OR_NULL(grp))
		return -ENODEV;

	for (index = 0; index < grp->nclocks; index++) {
		u32 slow = grp->clocks[index].slow;

		if (slow != 0)
			clk_set_rate(grp->clocks[index].clk, slow);
	}

	camrtc_clk_group_set_parent(grp, grp->parents.slow);

	return 0;
}
EXPORT_SYMBOL_GPL(camrtc_clk_group_adjust_slow);

/**
 * @brief Adjusts all clocks in a clock group to their fast rates
 *
 * This function adjusts all clocks in the specified clock group to their fast rates
 * and sets the fast parent clock. It performs the following operations:
 * - Validates the clock group using @ref IS_ERR_OR_NULL()
 * - Sets the parent clock to the fast parent using @ref camrtc_clk_group_set_parent()
 * - Iterates through all clocks in the group
 * - Sets the rate of each clock to its fast rate using @ref clk_set_rate() if the fast rate is non-zero
 *
 * @param[in] grp  Pointer to the clock group
 *                 Valid value: non-NULL and not an error pointer
 *
 * @retval 0        On successful adjustment
 * @retval -ENODEV  If the clock group is invalid
 */
int camrtc_clk_group_adjust_fast(const struct camrtc_clk_group *grp)
{
	int index;

	if (IS_ERR_OR_NULL(grp))
		return -ENODEV;

	camrtc_clk_group_set_parent(grp, grp->parents.fast);

	for (index = 0; index < grp->nclocks; index++) {
		u32 fast = grp->clocks[index].fast;

		if (fast != 0)
			clk_set_rate(grp->clocks[index].clk, fast);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(camrtc_clk_group_adjust_fast);
MODULE_LICENSE("GPL v2");
