/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2016-2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef PHY_TEGRA_XUSB_H
#define PHY_TEGRA_XUSB_H

struct tegra_xusb_padctl;
struct device;
struct notifier_block;
enum usb_device_speed;

#define PHY_TEGRA_XUSB_SUPPORT_EVENT

#define XUSB_EVENT_VF_OFFSET		24
#define XUSB_EVENT_VF_MASK		0xff

struct tegra_xusb_padctl *tegra_xusb_padctl_get(struct device *dev);
void tegra_xusb_padctl_put(struct tegra_xusb_padctl *padctl);
int tegra_xusb_padctl_event_register(struct tegra_xusb_padctl *padctl,
				     struct notifier_block *nb);
int tegra_xusb_padctl_event_unregister(struct tegra_xusb_padctl *padctl,
				     struct notifier_block *nb);
int tegra_xusb_padctl_event_notify(struct tegra_xusb_padctl *padctl,
				     unsigned long val);
int tegra_xusb_padctl_usb3_save_context(struct tegra_xusb_padctl *padctl,
					unsigned int port);
int tegra_xusb_padctl_hsic_set_idle(struct tegra_xusb_padctl *padctl,
				    unsigned int port, bool idle);
int tegra_xusb_padctl_usb3_set_lfps_detect(struct tegra_xusb_padctl *padctl,
					   unsigned int port, bool enable);
int tegra_xusb_padctl_set_vbus_override(struct tegra_xusb_padctl *padctl,
					bool val);
void tegra_phy_xusb_utmi_pad_power_on(struct phy *phy);
void tegra_phy_xusb_utmi_pad_power_down(struct phy *phy);
int tegra_phy_xusb_utmi_port_reset(struct phy *phy);
int tegra_xusb_padctl_get_usb3_companion(struct tegra_xusb_padctl *padctl,
					 unsigned int port);
int tegra_xusb_padctl_get_port_number(struct phy *phy);
int tegra_xusb_padctl_enable_phy_sleepwalk(struct tegra_xusb_padctl *padctl, struct phy *phy,
					   enum usb_device_speed speed);
int tegra_xusb_padctl_disable_phy_sleepwalk(struct tegra_xusb_padctl *padctl, struct phy *phy);
int tegra_xusb_padctl_enable_phy_wake(struct tegra_xusb_padctl *padctl, struct phy *phy);
int tegra_xusb_padctl_disable_phy_wake(struct tegra_xusb_padctl *padctl, struct phy *phy);
bool tegra_xusb_padctl_remote_wake_detected(struct tegra_xusb_padctl *padctl, struct phy *phy);

#endif /* PHY_TEGRA_XUSB_H */
