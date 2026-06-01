// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <linux/tegra-camera-rtcpu.h>
#include <linux/tegra-rtcpu-monitor.h>

struct tegra_camrtc_mon {
	struct device *rce_dev;
	int wdt_irq;
	struct work_struct wdt_work;
};

/**
 * @brief Restores the camera RTCPU by rebooting it
 *
 * This function reboots the camera RTCPU which broadcasts rtcpu-down and rtcpu-up
 * events to all IVC channels. It performs the following operations:
 * - Calls @ref tegra_camrtc_reboot to reboot the camera RTCPU
 *
 * @param[in] cam_rtcpu_mon Pointer to the camera RTCPU monitor
 *                           Valid value: non-NULL
 *
 * @retval (int) return value from @ref tegra_camrtc_reboot
 */
int tegra_camrtc_mon_restore_rtcpu(struct tegra_camrtc_mon *cam_rtcpu_mon)
{
	/* (Re)boot the rtcpu */
	/* rtcpu-down and rtcpu-up events are broadcast to all ivc channels */
	return tegra_camrtc_reboot(cam_rtcpu_mon->rce_dev);
}
EXPORT_SYMBOL(tegra_camrtc_mon_restore_rtcpu);

/**
 * @brief Handles camera RTCPU watchdog timeout work
 *
 * This function is called when the camera RTCPU watchdog timer expires.
 * It performs the following operations:
 * - Logs a warning message using @ref dev_info
 * - Restores the camera RTCPU using @ref tegra_camrtc_mon_restore_rtcpu
 * - Re-enables the watchdog IRQ using @ref enable_irq
 *
 * @param[in] work Pointer to the work structure
 *                 Valid value: non-NULL
 */
static void tegra_camrtc_mon_wdt_worker(struct work_struct *work)
{
	struct tegra_camrtc_mon *cam_rtcpu_mon = container_of(work,
					struct tegra_camrtc_mon, wdt_work);

	dev_info(cam_rtcpu_mon->rce_dev,
		"Alert: Camera RTCPU gone bad! restoring it immediately!!\n");

	tegra_camrtc_mon_restore_rtcpu(cam_rtcpu_mon);

	/* Enable WDT IRQ */
	enable_irq(cam_rtcpu_mon->wdt_irq);
}

/**
 * @brief ISR for camera RTCPU watchdog timer
 *
 * This function is the interrupt service routine called when the camera RTCPU
 * watchdog timer expires. It performs the following operations:
 * - Disables the IRQ using @ref disable_irq_nosync to prevent further interrupts
 * - Schedules the watchdog worker using @ref schedule_work to handle the timeout
 *
 * @param[in] irq  Interrupt number
 *                 Valid value: any positive integer
 * @param[in] data Pointer to driver data (tegra_camrtc_mon)
 *                 Valid value: non-NULL
 *
 * @retval IRQ_HANDLED to indicate the interrupt was handled
 */
static irqreturn_t tegra_camrtc_mon_wdt_remote_isr(int irq, void *data)
{
	struct tegra_camrtc_mon *cam_rtcpu_mon = data;

	disable_irq_nosync(irq);

	schedule_work(&cam_rtcpu_mon->wdt_work);

	return IRQ_HANDLED;
}

/**
 * @brief Sets up the camera RTCPU watchdog timer IRQ
 *
 * This function sets up the interrupt request handler for the camera RTCPU
 * watchdog timer. It performs the following operations:
 * - Gets the platform device pointer using @ref to_platform_device
 * - Gets the IRQ number using @ref platform_get_irq_byname
 * - Registers the threaded IRQ handler using @ref devm_request_threaded_irq
 * - Logs information about the IRQ
 *
 * @param[in,out] cam_rtcpu_mon Pointer to the camera RTCPU monitor structure
 *                              Valid value: non-NULL
 *
 * @retval 0        On successful setup
 * @retval -ENODEV  If the IRQ is not found
 * @retval (int)    Error code from @ref devm_request_threaded_irq
 */
static int tegra_camrtc_mon_wdt_irq_setup(
		struct tegra_camrtc_mon *cam_rtcpu_mon)
{
	struct platform_device *pdev =
			to_platform_device(cam_rtcpu_mon->rce_dev);
	int ret;

	cam_rtcpu_mon->wdt_irq = platform_get_irq_byname(pdev, "wdt-remote");
	if (cam_rtcpu_mon->wdt_irq < 0) {
		dev_warn(&pdev->dev, "missing irq wdt-remote\n");
		return -ENODEV;
	}

	ret = devm_request_threaded_irq(&pdev->dev, cam_rtcpu_mon->wdt_irq,
			NULL, tegra_camrtc_mon_wdt_remote_isr, IRQF_ONESHOT,
			dev_name(cam_rtcpu_mon->rce_dev), cam_rtcpu_mon);
	if (ret)
		return ret;

	dev_info(&pdev->dev, "using cam RTCPU IRQ (%d)\n",
			cam_rtcpu_mon->wdt_irq);

	return 0;
}

/**
 * @brief Creates and initializes a camera RTCPU monitor
 *
 * This function creates and initializes a camera RTCPU monitor structure.
 * It performs the following operations:
 * - Allocates memory for the monitor structure using @ref devm_kzalloc
 * - Initializes the RCE device pointer
 * - Initializes the watchdog work using @ref INIT_WORK
 * - Sets up the watchdog IRQ using @ref tegra_camrtc_mon_wdt_irq_setup
 *
 * @param[in] dev Pointer to the device structure
 *                Valid value: non-NULL
 *
 * @retval (struct tegra_camrtc_mon *) Pointer to the created camera RTCPU monitor on success
 * @retval (ERR_PTR(-ENOMEM)) on failure
 */
struct tegra_camrtc_mon *tegra_camrtc_mon_create(struct device *dev)
{
	struct tegra_camrtc_mon *cam_rtcpu_mon;

	cam_rtcpu_mon = devm_kzalloc(dev, sizeof(*cam_rtcpu_mon), GFP_KERNEL);
	if (unlikely(cam_rtcpu_mon == NULL))
		return ERR_PTR(-ENOMEM);

	cam_rtcpu_mon->rce_dev = dev;

	/* Initialize wdt_work */
	INIT_WORK(&cam_rtcpu_mon->wdt_work, tegra_camrtc_mon_wdt_worker);

	tegra_camrtc_mon_wdt_irq_setup(cam_rtcpu_mon);

	dev_info(dev, "tegra_camrtc_mon_create is successful\n");

	return cam_rtcpu_mon;
}
EXPORT_SYMBOL(tegra_camrtc_mon_create);

/**
 * @brief Destroys a camera RTCPU monitor
 *
 * This function destroys a previously created camera RTCPU monitor structure.
 * It performs the following operations:
 * - Validates the input pointer using @ref IS_ERR_OR_NULL
 * - Frees the allocated memory using @ref devm_kfree
 *
 * @param[in] cam_rtcpu_mon Pointer to the camera RTCPU monitor structure
 *                          Valid value: any value including NULL or error pointer
 *
 * @retval 0       When successfully destroyed or input is NULL
 * @retval -EINVAL If the input is an error pointer
 */
int tegra_cam_rtcpu_mon_destroy(struct tegra_camrtc_mon *cam_rtcpu_mon)
{
	if (IS_ERR_OR_NULL(cam_rtcpu_mon))
		return -EINVAL;

	devm_kfree(cam_rtcpu_mon->rce_dev, cam_rtcpu_mon);

	return 0;
}
EXPORT_SYMBOL(tegra_cam_rtcpu_mon_destroy);

MODULE_DESCRIPTION("CAMERA RTCPU monitor driver");
MODULE_AUTHOR("Sudhir Vyas <svyas@nvidia.com>");
MODULE_LICENSE("GPL v2");
