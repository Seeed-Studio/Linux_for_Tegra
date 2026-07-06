// SPDX-License-Identifier: MIT
/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <tegra_hwpm_log.h>
#include <tegra_hwpm.h>
#include <tegra_hwpm_common.h>
#include <tegra_hwpm_static_analysis.h>

int tegra_hwpm_perfmux_disable(struct tegra_soc_hwpm *hwpm,
	struct hwpm_ip_aperture *perfmux)
{
	tegra_hwpm_fn(hwpm, " ");

	return 0;
}

int tegra_hwpm_reserve_rtr(struct tegra_soc_hwpm *hwpm)
{
	int err = 0;

	struct tegra_soc_hwpm_chip *active_chip = hwpm->active_chip;

	tegra_hwpm_fn(hwpm, " ");

	err = tegra_hwpm_func_single_ip(hwpm, NULL,
		TEGRA_HWPM_RESERVE_GIVEN_RESOURCE,
		active_chip->get_rtr_int_idx());
	if (err != 0) {
		tegra_hwpm_err(hwpm, "failed to reserve IP %d",
			active_chip->get_rtr_int_idx());
		return err;
	}
	return err;
}

int tegra_hwpm_release_rtr(struct tegra_soc_hwpm *hwpm)
{
	int err = 0;
	struct tegra_soc_hwpm_chip *active_chip = hwpm->active_chip;

	tegra_hwpm_fn(hwpm, " ");

	err = tegra_hwpm_func_single_ip(hwpm, NULL,
		TEGRA_HWPM_RELEASE_ROUTER,
		active_chip->get_rtr_int_idx());
	if (err != 0) {
		tegra_hwpm_err(hwpm, "failed to release IP %d",
			active_chip->get_rtr_int_idx());
		return err;
	}
	return err;
}

int tegra_hwpm_reserve_resource(struct tegra_soc_hwpm *hwpm, u32 resource)
{
	struct tegra_soc_hwpm_chip *active_chip = hwpm->active_chip;
	u32 ip_idx = TEGRA_HWPM_IP_INACTIVE;
	int err = 0;

	tegra_hwpm_fn(hwpm, " ");

	tegra_hwpm_dbg(hwpm, hwpm_info | hwpm_dbg_reserve_resource,
		"User requesting to reserve resource %d", resource);

	/* Translate resource to ip_idx */
	if (!active_chip->is_resource_active(hwpm, resource, &ip_idx)) {
		tegra_hwpm_err(hwpm, "Requested resource %d is unavailable",
			resource);
		return -EINVAL;
	}

	err = tegra_hwpm_func_single_ip(hwpm, NULL,
		TEGRA_HWPM_RESERVE_GIVEN_RESOURCE, ip_idx);
	if (err != 0) {
		tegra_hwpm_err(hwpm, "failed to reserve IP %d", ip_idx);
		return err;
	}

	return 0;
}

int tegra_hwpm_bind_resources(struct tegra_soc_hwpm *hwpm)
{
	int err = 0;

	tegra_hwpm_fn(hwpm, " ");

	/**
	 * TODO: use soft reset after configuring DG map and avoid zeroing
	 * the perfmons.
	 */

	err = tegra_hwpm_func_all_ip(hwpm, NULL, TEGRA_HWPM_BIND_RESOURCES);
	if (err != 0) {
		tegra_hwpm_err(hwpm, "failed to bind resources");
		return err;
	}

	return err;
}

int tegra_hwpm_release_resources(struct tegra_soc_hwpm *hwpm)
{
	int ret = 0;

	tegra_hwpm_fn(hwpm, " ");

	ret = tegra_hwpm_func_all_ip(hwpm, NULL, TEGRA_HWPM_UNBIND_RESOURCES);
	if (ret != 0) {
		tegra_hwpm_err(hwpm, "failed to release resources");
		return ret;
	}

	ret = tegra_hwpm_func_all_ip(hwpm, NULL, TEGRA_HWPM_RELEASE_RESOURCES);
	if (ret != 0) {
		tegra_hwpm_err(hwpm, "failed to release resources");
		return ret;
	}

	return 0;
}
