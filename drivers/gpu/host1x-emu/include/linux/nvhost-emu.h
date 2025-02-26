/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef __LINUX_NVHOST_H
#define __LINUX_NVHOST_H

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/nvhost-emu-type.h>
#include <linux/symbol-emu.h>

static inline
struct nvhost_device_data *nvhost_get_devdata(struct platform_device *pdev)
{
    return (struct nvhost_device_data *)platform_get_drvdata(pdev);
}

extern const struct dev_pm_ops nvhost_module_pm_ops;

HOST1X_EMU_EXPORT_DECL(u32, host1x_readl(struct platform_device *pdev, u32 r));

HOST1X_EMU_EXPORT_DECL(void, host1x_writel(struct platform_device *pdev, u32 r, u32 v));

/* public api to return platform_device ptr to the default host1x instance */
HOST1X_EMU_EXPORT_DECL(struct platform_device*, nvhost_get_default_device(void));

/* common runtime pm and power domain APIs */
HOST1X_EMU_EXPORT_DECL(int, nvhost_module_init(struct platform_device *ndev));

HOST1X_EMU_EXPORT_DECL(void, nvhost_module_deinit(struct platform_device *dev));

HOST1X_EMU_EXPORT_DECL(void, nvhost_module_reset(struct platform_device *dev, bool reboot));

HOST1X_EMU_EXPORT_DECL(int, nvhost_module_busy(struct platform_device *dev));

HOST1X_EMU_EXPORT_DECL(void, nvhost_module_idle(struct platform_device *dev));

HOST1X_EMU_EXPORT_DECL(void, nvhost_module_idle_mult(struct platform_device *pdev, int refs));

/* common device management APIs */
HOST1X_EMU_EXPORT_DECL(int, nvhost_client_device_get_resources(struct platform_device *dev));

HOST1X_EMU_EXPORT_DECL(int, nvhost_client_device_release(struct platform_device *dev));

HOST1X_EMU_EXPORT_DECL(int, nvhost_client_device_init(struct platform_device *dev));

/* public host1x sync-point management APIs */
HOST1X_EMU_EXPORT_DECL(u32, nvhost_get_syncpt_host_managed(struct platform_device *pdev,
                   u32 param, const char *syncpt_name));

HOST1X_EMU_EXPORT_DECL(u32, nvhost_get_syncpt_client_managed(struct platform_device *pdev,
                     const char *syncpt_name));

HOST1X_EMU_EXPORT_DECL(u32, nvhost_get_syncpt_gpu_managed(struct platform_device *pdev,
                  const char *syncpt_name));

HOST1X_EMU_EXPORT_DECL(void, nvhost_syncpt_put_ref_ext(struct platform_device *pdev, u32 id));

HOST1X_EMU_EXPORT_DECL(bool, nvhost_syncpt_is_valid_pt_ext(struct platform_device *dev, u32 id));

HOST1X_EMU_EXPORT_DECL(int, nvhost_syncpt_is_expired_ext(struct platform_device *dev, u32 id,
                 u32 thresh));

HOST1X_EMU_EXPORT_DECL(void, nvhost_syncpt_set_min_update(struct platform_device *pdev, u32 id, u32 val));

HOST1X_EMU_EXPORT_DECL(int, nvhost_syncpt_read_ext_check(struct platform_device *dev, u32 id, u32 *val));

HOST1X_EMU_EXPORT_DECL(u32, nvhost_syncpt_read_maxval(struct platform_device *dev, u32 id));

HOST1X_EMU_EXPORT_DECL(u32, nvhost_syncpt_incr_max_ext(struct platform_device *dev, u32 id, u32 incrs));

HOST1X_EMU_EXPORT_DECL(int, nvhost_syncpt_unit_interface_init(struct platform_device *pdev));

HOST1X_EMU_EXPORT_DECL(void, nvhost_syncpt_unit_interface_deinit(struct platform_device *pdev));

HOST1X_EMU_EXPORT_DECL(dma_addr_t, nvhost_syncpt_address(struct platform_device *engine_pdev, u32 id));

/* public host1x interrupt management APIs */
HOST1X_EMU_EXPORT_DECL(int, nvhost_intr_register_notifier(struct platform_device *pdev,
                  u32 id, u32 thresh,
                  void (*callback)(void *),
                  void *private_data));

/* public host1x sync-point management APIs */
HOST1X_EMU_EXPORT_DECL(struct host1x*, nvhost_get_host1x(struct platform_device *pdev));

HOST1X_EMU_EXPORT_DECL(int, nvhost_syncpt_unit_interface_get_aperture(
                struct platform_device *host_pdev,
                phys_addr_t *base,
                size_t *size));

HOST1X_EMU_EXPORT_DECL(u32, nvhost_syncpt_unit_interface_get_byte_offset(u32 syncpt_id));

HOST1X_EMU_EXPORT_DECL(u32, nvhost_syncpt_unit_interface_get_byte_offset_ext(
                struct platform_device *host_pdev,
                u32 syncpt_id));
#endif
