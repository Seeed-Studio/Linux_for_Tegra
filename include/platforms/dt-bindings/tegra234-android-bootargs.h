/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES.  All rights reserved. */

// Definitions for tegra234 android bootargs

// Make sure ANDROID_BOOTARGS and ANDROID_KDUMP_BOOTARGS are consistent except the later has "crashkernel=512M enforcing=0 androidboot.selinux=permissive" appended
#define ANDROID_BOOTARGS       "bootconfig console=ttyTCU0,115200 rootfstype=ext4 mminit_loglevel=4 loop.max_part=7 firmware_class.path=/vendor/firmware"

#define ANDROID_KDUMP_BOOTARGS "bootconfig console=ttyTCU0,115200 rootfstype=ext4 mminit_loglevel=4 isabled loop.max_part=7 firmware_class.path=/vendor/firmware crashkernel=512M enforcing=0 androidboot.selinux=permissive"

#define ANDROID_BOOTCONFIG "androidboot.boot_devices=bus@0/3460000.mmc\nandroidboot.hypervisor=disabled\nandroidboot.xudc=3550000.usb\nandroidboot.hardware=t234ref\n"

// Make sure ANDROID_FIRESPRAY_BOOTARGS and ANDROID_FIRESPRAY_KDUMP_BOOTARGS are consistent except the later has "crashkernel=512M" appended
#define ANDROID_FIRESPRAY_BOOTARGS       "bootconfig console=ttyTCU0,115200 rootfstype=ext4 mminit_loglevel=4 enforcing=0 loop.max_part=7 firmware_class.path=/vendor/firmware"

#define ANDROID_FIRESPRAY_KDUMP_BOOTARGS "bootconfig console=ttyTCU0,115200 rootfstype=ext4 mminit_loglevel=4 enforcing=0 loop.max_part=7 firmware_class.path=/vendor/firmware crashkernel=512M"

#define ANDROID_FIRESPRAY_BOOTCONFIG "androidboot.boot_devices=bus@0/3460000.mmc\nandroidboot.hypervisor=disabled\nandroidboot.xudc=3550000.usb\nandroidboot.hardware=t234ref\n"
