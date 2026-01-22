# SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
# property and proprietary rights in and to this material, related
# documentation and any modifications thereto. Any use, reproduction,
# disclosure or distribution of this material and related documentation
# without an express license agreement from NVIDIA CORPORATION or
# its affiliates is strictly prohibited.

********************************************************************************
                    NVIDIA Jetson Linux (L4T) UEFI Binaries
********************************************************************************

--------------------------------------------------------------------------------
 Introduction
--------------------------------------------------------------------------------
- General UEFI: Full-feature UEFI used by default in public release.

- Minimal UEFI: The best boot performance with limited UEFI features supported.
  Emulated UEFI variables are stored in DRAM. UEFI image can be stored in the
  same storage device as file system image. However, UEFI secure boot is
  supported only at flashing time. UEFI boot mode and boot order selection are
  not enabled. Capsule update and rootfs A/B selection are not available.

- Simple UEFI: Combines features from both General UEFI and minimal UEFI. It
  inherits the essential features such as switching/failover A/B, capsule
  update, and secure boot from General UEFI. It has also optimized boot
  performance inherited from Minimal UEFI through limiting boot device to single
  boot device. Simple UEFI can be considered as the ideal candidate for building
  an embedded device.

--------------------------------------------------------------------------------
 UEFI binaries
--------------------------------------------------------------------------------
For t26x:
- uefi_t26x_general.bin: used for Jetson AGX Thor devkit with NVMe or UFS storage.
- uefi_t26x_minimal.bin: used for Jetson AGX Thor devkit with NVMe or UFS storage.
- uefi_t26x_simple_pcie.bin: used for Jetson AGX Thor devkit with NVMe storage.
- uefi_t26x_simple_ufs.bin: used for Jetson AGX Thor devkit with UFS storage.

For t23x:
- uefi_t23x_general.bin: used for Jetson AGX Orin devkit or Orin Nano devkit.
- uefi_t23x_minimal.bin: used for Jetson AGX Orin devkit.
- uefi_t23x_simple_emmc.bin: used for Jetson AGX Orin devkit.
- uefi_t23x_simple_pcie.bin: used for Jetson Orin Nano devkit.

--------------------------------------------------------------------------------
 Use Minimal UEFI and simple UEFI binaries
--------------------------------------------------------------------------------
Back up the original general UEFI binary and copy the minimal UEFI or simple
UEFI binary to replace the general UEFI binary in bootloader/uefi_bins/
directory. For example:

$ cd <your_path>/Linux_for_Tegra/
$ cp bootloader/uefi_bins/uefi_t26x_general.bin bootloader/uefi_bins/uefi_t26x_general.bin.bak
$ cp bootloader/uefi_bins/uefi_t26x_minimal.bin bootloader/uefi_bins/uefi_t26x_general.bin

Then flash the Jetson device with the command-line parameter specifying the boot
device:
$ sudo ADDITIONAL_DTB_OVERLAY_OPT=<BootOrder.dtbo> ./l4t_initrd_flash.sh <devkit> internal

<BootOrder.dtbo> is BootOrderNvme.dtbo, BootOrderUfs.dtbo, or BootOrderEmmc.dtbo.

For example:
- Jetson AGX Thor flash with minimal UEFI or simple UEFI:
  $ sudo ADDITIONAL_DTB_OVERLAY_OPT="BootOrderNvme.dtbo" ./l4t_initrd_flash.sh jetson-agx-thor-devkit internal
- Jetson AGX Orin flash with minimal UEFI or simple UEFI:
  $ sudo ADDITIONAL_DTB_OVERLAY_OPT="BootOrderEmmc.dtbo" ./l4t_initrd_flash.sh jetson-agx-orin-devkit internal

--------------------------------------------------------------------------------
 UEFI Boot Time and Supported Features
--------------------------------------------------------------------------------
For jetson-agx-thor-devkit:
+---------------------------+---------+-------------+-------------+---------+
|                           | General | Simple UEFI | Simple UEFI | Minimal |
|                           |   UEFI  |    (UFS)    |    (NVMe)   |   UEFI  |
+===========================+=========+=============+=============+=========+
| Boot time(NVMe)           |  12.43s |     N/A     |     5.31s   |  1.42s  |
+---------------------------+---------+-------------+-------------+---------+
| Persistent UEFI variable  |   YES   |     YES     |      YES    |    NO   |
+---------------------------+---------+-------------+-------------+---------+
| A/B fail-over/switching   |   YES   |     YES     |      YES    |    NO   |
+---------------------------+---------+-------------+-------------+---------+
| Capsule update            |   YES   |     YES     |      YES    |    NO   |
+---------------------------+---------+-------------+-------------+---------+
| UEFI secure boot          |   YES   |     YES     |      YES    |    NO   |
+---------------------------+---------+-------------+-------------+---------+

For jetson-agx-orin-devkit and jetson-orin-nano-devkit:
+---------------------------+---------+-------------+-------------+---------+
|                           | General | Simple UEFI | Simple UEFI | Minimal |
|                           |   UEFI  |    (eMMC)   |    (NVMe)   |   UEFI  |
+===========================+=========+=============+=============+=========+
| Boot time(eMMC) on Orin*1 | 16.54s  |    8.48s    |      N/A    |  4.44s  |
+---------------------------+---------+-------------+-------------+---------+
| Boot time(NVMe) on Nano*2 | 13.94s  |     N/A     |     5.93s   |   N/A   |
+---------------------------+---------+-------------+-------------+---------+
| Persistent UEFI variable  |   YES   |     YES     |      YES    |    NO   |
+---------------------------+---------+-------------+-------------+---------+
| A/B fail-over/switching   |   YES   |     YES     |      YES    |    NO   |
+---------------------------+---------+-------------+-------------+---------+
| Capsule update            |   YES   |     YES     |      YES    |    NO   |
+---------------------------+---------+-------------+-------------+---------+
| UEFI secure boot          |   YES   |     YES     |      YES    |    NO   |
+---------------------------+---------+-------------+-------------+---------+

*1: UEFI boot time is measured on Jetson AGX Orin devkit with eMMC storage.
*2: UEFI boot time is measured on Jetson Orin Nano devkit with NVMe storage.

Notes:
- The UEFI boot time is measured during the subsequent boot after flashing.
  The first boot after flashing UEFI takes longer because UEFI performs
  initial configuration.
- The CONFIG_BOOT_DEFAULT_TIMEOUT of general UEFI is 5, so the UEFI boot time
  of the General UEFI in the table includes the 5s of timeout time.
- The UEFI boot time is tested in the laboratory with devkit for reference.
  Measure the UEFI boot time on your board after customization.
