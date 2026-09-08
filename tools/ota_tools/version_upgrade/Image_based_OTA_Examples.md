<!--
SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
SPDX-License-Identifier: LicenseRef-NvidiaProprietary
-->

# Examples of How to Use Image-Based OTA Update

This file provides several examples of how to use an image-based OTA update for specific use cases.

---

## 1. Jetson Thor: Generating an OTA payload package for OTA updates from R38.4.0 to the latest R39 version

The following examples show you how to generate an OTA payload package for different cases. Please export `BASE_BSP=<R38.4.0 BSP>/Linux_for_Tegra` as an environment variable before running the following commands.

> **Note:** Currently, the latest version of R39 is R39.2.0.

### Case 1: jetson-agx-thor-devkit

An OTA update from R38.4.0 to the latest R39 version on the jetson-agx-thor-devkit.

```bash
sudo -E ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
  --external-device nvme0n1 jetson-agx-thor-devkit R38-4
```

### Case 2: jetson-agx-thor-devkit with disk encryption enabled

An OTA update from R38.4.0 to the latest R39 version on the jetson-agx-thor-devkit with disk encryption enabled.

```bash
sudo -E ROOTFS_ENC=1 ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
  --external-device nvme0n1 -i ekb.key jetson-agx-thor-devkit R38-4
```

The `ekb.key` is the key used for disk encryption. It must be the same as the one used to flash R38.4.0 to the device.

### Case 3: jetson-agx-thor-devkit with rootfs A/B enabled

An OTA update from R38.4.0 to the latest R39 version on jetson-agx-thor-devkit with rootfs A/B enabled.

```bash
sudo -E ROOTFS_AB=1 ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
  --external-device nvme0n1 jetson-agx-thor-devkit R38-4
```

### Case 4: jetson-agx-thor-devkit with rootfs A/B and disk encryption enabled

An OTA update from R38.4.0 to the latest R39 version on jetson-agx-thor-devkit with rootfs A/B and disk encryption enabled.

```bash
sudo -E ROOTFS_AB=1 ROOTFS_ENC=1 ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
  --external-device nvme0n1 -i ekb.key jetson-agx-thor-devkit R38-4
```

---

## 2. Jetson Orin: Generating an OTA payload package for OTA updates from R35.5.0 to the latest R39 version

The following examples show you how to generate an OTA payload package for different cases. Please export `BASE_BSP=<R35.5.0 BSP>/Linux_for_Tegra` as an environment variable before running the following commands.

> **Note:** Currently, the latest version of R39 is R39.2.0.

### Case 5: jetson-agx-orin-devkit

An OTA update from R35.5.0 to the latest R39 version on the jetson-agx-orin-devkit.

```bash
sudo -E ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
  jetson-agx-orin-devkit R35-5
```

### Case 6: jetson-agx-orin-devkit-industrial

An OTA update from R35.5.0 to the latest R39 version on the jetson-agx-orin-devkit-industrial.

```bash
sudo -E ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
  jetson-agx-orin-devkit-industrial R35-5
```

### Case 7: jetson-orin-nano-devkit with NVMe

An OTA update from R35.5.0 to the latest R39 version on jetson-orin-nano-devkit with booting from an NVMe storage device. The size of the APP partition on an NVMe storage device is 24GiB.

```bash
sudo -E ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
  --external-device nvme0n1 -S 24GiB jetson-orin-nano-devkit R35-5
```

> **Note:** The size of the APP partition will be overwritten by user's actual size.

### Case 8: jetson-agx-orin-devkit with disk encryption enabled

An OTA update from R35.5.0 to the latest R39 version on the jetson-agx-orin-devkit with disk encryption enabled.

```bash
sudo -E ROOTFS_ENC=1 ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
  -i ekb.key jetson-agx-orin-devkit R35-5
```

The `ekb.key` is the key used for disk encryption. It must be the same as the one used to flash R35.5.0 to the device.

### Case 9: jetson-agx-orin-devkit-industrial with rootfs A/B enabled

An OTA update from R35.5.0 to the latest R39 version on the jetson-agx-orin-devkit-industrial with rootfs A/B enabled.

```bash
sudo -E ROOTFS_AB=1 ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
  jetson-agx-orin-devkit-industrial R35-5
```

### Case 10: jetson-orin-nano-devkit with NVMe, rootfs A/B and disk encryption enabled

An OTA update from R35.5.0 to the latest R39 version on jetson-orin-nano-devkit, booting from an NVMe storage device. The rootfs A/B and disk encryption are enabled. The size of the APP partition on an NVMe storage device is 24GiB.

```bash
sudo -E ROOTFS_AB=1 ROOTFS_ENC=1 ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
  --external-device nvme0n1 -S 24GiB -i ekb.key jetson-orin-nano-devkit R35-5
```

---

## 3. Jetson Orin: OTA update from R35.5.0 to the latest R39 version

When applying an OTA update from R35.5.0 to the latest R39 version, you need to update both bootloader chains and both rootfs chains if rootfs A/B is enabled to the latest R39 version. The ESP, recovery, and recovery-dtb partitions are unique for both A/B chains.

The following examples show you the steps to perform an OTA update from R35.5.0 to the latest R39 version:

### Case 11: jetson-agx-orin-devkit (full procedure)

To perform an OTA update from R35.5.0 to the latest R39 version on jetson-agx-orin-devkit, complete the following steps:

1. Download the latest R39 BSP, sample rootfs, and OTA tools package, and then extract them.
2. Generate the OTA payload package from R35.5.0 to R39.2.0:

   ```bash
   cd Linux_for_Tegra
   sudo -E BASE_BSP=<R35.5.0 BSP>/Linux_for_Tegra ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
     jetson-agx-orin-devkit R35-5
   ```

3. Copy the OTA tools package `ota_tools_R39.2.0_aarch64.tbz2` and the generated OTA payload package `ota_payload_package.tar.gz` to the device, for example, put them in the `~/` directory.
4. Extract `ota_tools_R39.2.0_aarch64.tbz2` and trigger the OTA update:

   ```bash
   cd ~/
   tar xjpf ota_tools_R39.2.0_aarch64.tbz2
   cd Linux_for_Tegra/tools/ota_tools/version_upgrade
   sudo ./nv_ota_start.sh ~/ota_payload_package.tar.gz
   ```

5. Reboot the device.
   The Capsule update is to be executed once the device boots to UEFI. After the Capsule update is finished, the device is automatically rebooted and then boots from the chain that was newly upgraded to R39.2.0
6. Generate the OTA payload package that updates the bootloader only from R39.2.0 to R39.2.0:

   ```bash
   cd Linux_for_Tegra
   sudo ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
     -b jetson-agx-orin-devkit R39-2
   ```

   > **Note:** The purpose of this step is to update the other boot chains to R39. It is not supported using R35 boot chains to boot the R39 kernel/root FS.

7. Copy the OTA tools package `ota_tools_R39.2.0_aarch64.tbz2` and the generated OTA payload package `ota_payload_package.tar.gz` to the device, for example, put them in the `~/` directory.
8. Extract `ota_tools_R39.2.0_aarch64.tbz2` and trigger the OTA update:

   ```bash
   cd ~/
   tar xjpf ota_tools_R39.2.0_aarch64.tbz2
   cd Linux_for_Tegra/tools/ota_tools/version_upgrade
   sudo ./nv_ota_start.sh ~/ota_payload_package.tar.gz
   ```

9. Reboot the device.
   The Capsule update is to be executed once the device boots to UEFI. After the Capsule update is finished, the device is automatically rebooted and then boots from the chain that was newly upgraded to R39.2.0.
   Now both bootloader chains and the rootfs are upgraded to R39.2.0

### Case 12: jetson-orin-nano-devkit with NVMe, rootfs A/B and disk encryption enabled (full procedure)

To perform an OTA update from R35.5.0 to the latest R39 version on jetson-orin-nano-devkit booting from an NVMe with rootfs A/B and disk encryption enabled, and 24GiB APP/APP_b partitions, complete the following steps:

1. Download the latest R39 BSP, sample rootfs, and OTA tools package, and then extract them.
2. Generate the OTA payload package from R35.5.0 to R39.2.0:

   ```bash
   cd Linux_for_Tegra
   sudo -E ROOTFS_AB=1 ROOTFS_ENC=1 BASE_BSP=<R35.5.0 BSP>/Linux_for_Tegra \
     ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
     -i ekb.key --external-device nvme0n1 -S 24GiB jetson-orin-nano-devkit R35-5
   ```

3. Copy the OTA tools package `ota_tools_R39.2.0_aarch64.tbz2` and the generated OTA payload package `ota_payload_package.tar.gz` to the device, for example, put them in the `~/` directory.
4. Extract `ota_tools_R39.2.0_aarch64.tbz2` and trigger the OTA update:

   ```bash
   cd ~/
   tar xjpf ota_tools_R39.2.0_aarch64.tbz2
   cd Linux_for_Tegra/tools/ota_tools/version_upgrade
   sudo ./nv_ota_start.sh ~/ota_payload_package.tar.gz
   ```

5. Reboot the device.
   The Capsule update is to be executed once the device boots to UEFI. After the Capsule update is finished, the device is automatically rebooted and then boots from the chain that was newly upgraded to R39.2.0
6. Generate the OTA payload package from R39.2.0 to R39.2.0:

   ```bash
   cd Linux_for_Tegra
   sudo -E ROOTFS_AB=1 ROOTFS_ENC=1 ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
     -i ekb.key --external-device nvme0n1 -S 24GiB jetson-orin-nano-devkit R39-2
   ```

7. Copy the OTA tools package `ota_tools_R39.2.0_aarch64.tbz2` and the generated OTA payload package `ota_payload_package.tar.gz` to the device, for example, put them in the `~/` directory.
8. Extract `ota_tools_R39.2.0_aarch64.tbz2` and trigger the OTA update:

   ```bash
   cd ~/
   tar xjpf ota_tools_R39.2.0_aarch64.tbz2
   cd Linux_for_Tegra/tools/ota_tools/version_upgrade
   sudo ./nv_ota_start.sh ~/ota_payload_package.tar.gz
   ```

9. Reboot the device.
   The Capsule update is to be executed once the device boots to UEFI. After the Capsule update is finished, the device is automatically rebooted and then boots from the chain that was newly upgraded to R39.2.0.
   Now both bootloader chains and both rootfs on the NVMe storage device are upgraded to R39.2.0

---

## 4. Jetson Orin: OTA update from a version earlier than R35.5.0 to the latest R39 version

The image-based OTA scripts do not support an OTA update from a version earlier than R35.5.0 to the latest R39 version directly. To upgrade from such version, which includes R35.2.1, R35.3.1 and R35.4.1, you need to upgrade to R35.5.0 first, then upgrade to the latest R39 version.

The following examples show you the steps to perform an OTA update from an early R35 version to the latest R39 version:

### Case 13: R35.2.1 → R39 on jetson-agx-orin-devkit

To perform an OTA update from R35.2.1 to the latest R39 version on jetson-agx-orin-devkit, complete the following steps:

1. Download R35.5.0 BSP, sample rootfs, and OTA tools package, and then extract them.
2. Generate the OTA payload package from R35.2.1 to R35.5.0:

   ```bash
   cd Linux_for_Tegra
   sudo ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
     jetson-agx-orin-devkit R35-2
   ```

3. Copy the OTA tools package `ota_tools_R35.5.0_aarch64.tbz2` and the generated OTA payload package `ota_payload_package.tar.gz` to the device, for example, put them in the `~/` directory.
4. Extract `ota_tools_R35.5.0_aarch64.tbz2` and trigger the OTA update:

   ```bash
   cd ~/
   tar xjpf ota_tools_R35.5.0_aarch64.tbz2
   cd Linux_for_Tegra/tools/ota_tools/version_upgrade
   sudo ./nv_ota_start.sh ~/ota_payload_package.tar.gz
   ```

5. Reboot the device.
   The Capsule update is to be executed once the device boots to UEFI. After the Capsule update is finished, the device is automatically rebooted and then boots from the chain that was newly upgraded to R35.5.0
6. Download the latest R39 BSP, sample rootfs, and OTA tools package, and then extract them.
7. Generate the OTA payload package from R35.5.0 to R39.2.0:

   ```bash
   cd Linux_for_Tegra
   sudo -E BASE_BSP=<R35.5.0 BSP>/Linux_for_Tegra ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
     jetson-agx-orin-devkit R35-5
   ```

8. Copy the OTA tools package `ota_tools_R39.2.0_aarch64.tbz2` and the generated OTA payload package `ota_payload_package.tar.gz` to the device, for example, put them in the `~/` directory.
9. Extract `ota_tools_R39.2.0_aarch64.tbz2` and trigger the OTA update:

   ```bash
   cd ~/
   tar xjpf ota_tools_R39.2.0_aarch64.tbz2
   cd Linux_for_Tegra/tools/ota_tools/version_upgrade
   sudo ./nv_ota_start.sh ~/ota_payload_package.tar.gz
   ```

10. Reboot the device.
    The Capsule update is to be executed once the device boots to UEFI. After the Capsule update is finished, the device is automatically rebooted and then boots from the chain that was newly upgraded to R39.2.0
11. Generate the OTA payload package that updates the bootloader only from R39.2.0 to R39.2.0:

    ```bash
    cd Linux_for_Tegra
    sudo ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
      -b jetson-agx-orin-devkit R39-2
    ```

    > **Note:** The purpose of this step is to update the other boot chains to R39. It is not supported using R35 boot chains to boot the R39 kernel/root FS.

12. Copy the OTA tools package `ota_tools_R39.2.0_aarch64.tbz2` and the generated OTA payload package `ota_payload_package.tar.gz` to the device, for example, put them in the `~/` directory.
13. Extract `ota_tools_R39.2.0_aarch64.tbz2` and trigger the OTA update:

    ```bash
    cd ~/
    tar xjpf ota_tools_R39.2.0_aarch64.tbz2
    cd Linux_for_Tegra/tools/ota_tools/version_upgrade
    sudo ./nv_ota_start.sh ~/ota_payload_package.tar.gz
    ```

14. Reboot the device.
    The Capsule update is to be executed once the device boots to UEFI. After the Capsule update is finished, the device is automatically rebooted and then boots from the chain that was newly upgraded to R39.2.0.
    Now both bootloader chains and the rootfs are upgraded to R39.2.0

### Case 14: R35.2.1 → R39 on jetson-agx-orin-devkit with rootfs A/B and disk encryption enabled

To perform an OTA update from R35.2.1 to the latest R39 version on jetson-agx-orin-devkit with rootfs A/B and disk encryption enabled, complete the following steps:

1. Download R35.5.0 BSP, sample rootfs, and OTA tools package, and then extract them.
2. Generate the OTA payload package from R35.2.1 to R35.5.0:

   ```bash
   cd Linux_for_Tegra
   sudo -E ROOTFS_AB=1 ROOTFS_ENC=1 ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
     -i ekb.key jetson-agx-orin-devkit R35-2
   ```

3. Copy the OTA tools package `ota_tools_R35.5.0_aarch64.tbz2` and the generated OTA payload package `ota_payload_package.tar.gz` to the device, for example, put them in the `~/` directory.
4. Extract `ota_tools_R35.5.0_aarch64.tbz2` and trigger the OTA update:

   ```bash
   cd ~/
   tar xjpf ota_tools_R35.5.0_aarch64.tbz2
   cd Linux_for_Tegra/tools/ota_tools/version_upgrade
   sudo ./nv_ota_start.sh ~/ota_payload_package.tar.gz
   ```

5. Reboot the device.
   The Capsule update is to be executed once the device boots to UEFI. After the Capsule update is finished, the device is automatically rebooted and then boots from the chain that was newly upgraded to R35.5.0
6. Download the latest R39 BSP, sample rootfs, and OTA tools package, and then extract them.
7. Generate the OTA payload package from R35.5.0 to R39.2.0:

   ```bash
   cd Linux_for_Tegra
   sudo -E ROOTFS_AB=1 ROOTFS_ENC=1 BASE_BSP=<R35.5.0 BSP>/Linux_for_Tegra \
     ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
     -i ekb.key jetson-agx-orin-devkit R35-5
   ```

8. Copy the OTA tools package `ota_tools_R39.2.0_aarch64.tbz2` and the generated OTA payload package `ota_payload_package.tar.gz` to the device, for example, put them in the `~/` directory.
9. Extract `ota_tools_R39.2.0_aarch64.tbz2` and trigger the OTA update:

   ```bash
   cd ~/
   tar xjpf ota_tools_R39.2.0_aarch64.tbz2
   cd Linux_for_Tegra/tools/ota_tools/version_upgrade
   sudo ./nv_ota_start.sh ~/ota_payload_package.tar.gz
   ```

10. Reboot the device.
    The Capsule update is to be executed once the device boots to UEFI. After the Capsule update is finished, the device is automatically rebooted and then boots from the chain that was newly upgraded to R39.2.0
11. Generate the OTA payload package from R39.2.0 to R39.2.0:

    ```bash
    cd Linux_for_Tegra
    sudo -E ROOTFS_AB=1 ROOTFS_ENC=1 ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
      -i ekb.key jetson-agx-orin-devkit R39-2
    ```

12. Copy the OTA tools package `ota_tools_R39.2.0_aarch64.tbz2` and the generated OTA payload package `ota_payload_package.tar.gz` to the device, for example, put them in the `~/` directory.
13. Extract `ota_tools_R39.2.0_aarch64.tbz2` and trigger the OTA update:

    ```bash
    cd ~/
    tar xjpf ota_tools_R39.2.0_aarch64.tbz2
    cd Linux_for_Tegra/tools/ota_tools/version_upgrade
    sudo ./nv_ota_start.sh ~/ota_payload_package.tar.gz
    ```

14. Reboot the device.
    The Capsule update is to be executed once the device boots to UEFI. After the Capsule update is finished, the device is automatically rebooted and then boots from the chain that was newly upgraded to R39.2.0.
    Now both bootloader chains and both rootfs are upgraded to R39.2.0

### Case 15: R35.4.1 → R39 on jetson-orin-nano-devkit with NVMe, rootfs A/B and disk encryption enabled

To perform an OTA update from R35.4.1 to the latest R39 version on jetson-orin-nano-devkit booting from an NVMe with rootfs A/B and disk encryption enabled, and 24GiB APP/APP_b partitions, complete the following steps:

1. Download R35.5.0 BSP, sample rootfs, and OTA tools package, and then extract them.
2. Generate the OTA payload package from R35.4.1 to R35.5.0:

   ```bash
   cd Linux_for_Tegra
   sudo -E ROOTFS_AB=1 ROOTFS_ENC=1 ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
     -i ekb.key --external-device nvme0n1 -S 24GiB jetson-orin-nano-devkit R35-4
   ```

3. Copy the OTA tools package `ota_tools_R35.5.0_aarch64.tbz2` and the generated OTA payload package `ota_payload_package.tar.gz` to the device, for example, put them in the `~/` directory.
4. Extract `ota_tools_R35.5.0_aarch64.tbz2` and trigger the OTA update:

   ```bash
   cd ~/
   tar xjpf ota_tools_R35.5.0_aarch64.tbz2
   cd Linux_for_Tegra/tools/ota_tools/version_upgrade
   sudo ./nv_ota_start.sh ~/ota_payload_package.tar.gz
   ```

5. Reboot the device.
   The Capsule update is to be executed once the device boots to UEFI. After the Capsule update is finished, the device is automatically rebooted and then boots from the chain that was newly upgraded to R35.5.0
6. Download the latest R39 BSP, sample rootfs, and OTA tools package, and then extract them.
7. Generate the OTA payload package from R35.5.0 to R39.2.0:

   ```bash
   cd Linux_for_Tegra
   sudo -E ROOTFS_AB=1 ROOTFS_ENC=1 BASE_BSP=<R35.5.0 BSP>/Linux_for_Tegra \
     ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
     -i ekb.key --external-device nvme0n1 -S 24GiB jetson-orin-nano-devkit R35-5
   ```

8. Copy the OTA tools package `ota_tools_R39.2.0_aarch64.tbz2` and the generated OTA payload package `ota_payload_package.tar.gz` to the device, for example, put them in the `~/` directory.
9. Extract `ota_tools_R39.2.0_aarch64.tbz2` and trigger the OTA update:

   ```bash
   cd ~/
   tar xjpf ota_tools_R39.2.0_aarch64.tbz2
   cd Linux_for_Tegra/tools/ota_tools/version_upgrade
   sudo ./nv_ota_start.sh ~/ota_payload_package.tar.gz
   ```

10. Reboot the device.
    The Capsule update is to be executed once the device boots to UEFI. After the Capsule update is finished, the device is automatically rebooted and then boots from the chain that was newly upgraded to R39.2.0
11. Generate the OTA payload package from R39.2.0 to R39.2.0:

    ```bash
    cd Linux_for_Tegra
    sudo -E ROOTFS_AB=1 ROOTFS_ENC=1 ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
      -i ekb.key --external-device nvme0n1 -S 24GiB jetson-orin-nano-devkit R39-2
    ```

12. Copy the OTA tools package `ota_tools_R39.2.0_aarch64.tbz2` and the generated OTA payload package `ota_payload_package.tar.gz` to the device, for example, put them in the `~/` directory.
13. Extract `ota_tools_R39.2.0_aarch64.tbz2` and trigger the OTA update:

    ```bash
    cd ~/
    tar xjpf ota_tools_R39.2.0_aarch64.tbz2
    cd Linux_for_Tegra/tools/ota_tools/version_upgrade
    sudo ./nv_ota_start.sh ~/ota_payload_package.tar.gz
    ```

14. Reboot the device.
    The Capsule update is to be executed once the device boots to UEFI. After the Capsule update is finished, the device is automatically rebooted and then boots from the chain that was newly upgraded to R39.2.0.
    Now both bootloader chains and both rootfs on the NVMe storage device are upgraded to R39.2.0

---

## 5. Using golden image in OTA update

Customers might flash a device with the latest released BSP and then make a golden image dependent on this device. They can deploy the golden image on other devices that were flashed with the previously released BSP through an OTA update.

The `l4t_generate_ota_package.sh` script includes the `-f` and `-o` options so that customers can use their own rootfs updater and update the rootfs partition with their own rootfs image (for example, a golden image).

However, for some customers, the golden image is a raw partition image that is directly obtained from rootfs partition. (These customers might not want to write their own rootfs updater.)

The following examples provide the steps for using this golden image in an image-based OTA.

### Case 16: rootfs A/B disabled, disk encryption disabled

1. Put the target device into recovery mode.
2. Boot the device to initrd by running the following command:

   ```bash
   sudo ./tools/kernel_flash/l4t_initrd_flash.sh --initrd <target board> mmcblk0p1
   ```

3. Enter bash shell on the target device.
4. Mount a USB drive or network storage on the target device.
5. Generate golden image in the mounted storage device by running the following command:

   ```bash
   sudo dd if=<rootfs partition> of=<mount point>/golden.img.raw
   ```

   The `<rootfs partition>` is `/dev/mmcblk0p1` for the internal eMMC storage device or `/dev/nvme0n1p1` for the external NVMe storage device.

6. Copy the golden image into the TARGET_BSP directory on the host machine by running the following command:

   ```bash
   sudo cp golden.img.raw ${TARGET_BSP}/bootloader/system.img.raw
   ```

7. Add the `-s` option when generating the OTA payload package.
   For the internal eMMC storage device, running the following command:

   ```bash
   sudo ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
     -s <target board> <base version>
   ```

   For the external NVMe storage device, running the following command:

   ```bash
   sudo ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
     -s --external-device nvme0n1 -S <rootfs size > <target board> <base version>
   ```

### Case 17: rootfs A/B enabled, disk encryption disabled

1. Put the target device into recovery mode.
2. Boot the device to initrd by running the following command:

   ```bash
   sudo ./tools/kernel_flash/l4t_initrd_flash.sh --initrd <target board> mmcblk0p1
   ```

3. Enter bash shell on the target device.
4. Mount a USB drive or network storage on the target device.
5. Generate golden images in the mounted storage device by running the following commands:

   ```bash
   sudo dd if=<rootfs partition A> of=<mount point>/golden_a.img.raw
   sudo dd if=<rootfs partition B> of=<mount point>/golden_b.img.raw
   ```

   The `<rootfs partition A>` is `/dev/mmcblk0p1` for the internal eMMC storage device or `/dev/nvme0n1p1` for the external NVMe storage device, and the `<rootfs partition B>` is `/dev/mmcblk0p2` for the internal eMMC storage device or `/dev/nvme0n1p2` for the external NVMe storage device.

6. Copy the golden images into the TARGET_BSP directory on the host machine by running the following commands:

   ```bash
   sudo cp golden_a.img.raw ${TARGET_BSP}/bootloader/system.img.raw
   sudo cp golden_b.img.raw ${TARGET_BSP}/bootloader/system.img_b.raw
   ```

7. Add the `-s` option when generating the OTA payload package.
   For the internal eMMC storage device, running the following command:

   ```bash
   sudo -E ROOTFS_AB=1 ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
     -s <target board> <base version>
   ```

   For the external NVMe storage device, running the following command:

   ```bash
   sudo -E ROOTFS_AB=1 ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
     -s --external-device nvme0n1 -S <rootfs size> <target board> <base version>
   ```

### Case 18: rootfs A/B disabled, disk encryption enabled

1. Put the target device into recovery mode.
2. Boot the device to initrd by running the following command:

   ```bash
   sudo ./tools/kernel_flash/l4t_initrd_flash.sh --initrd <target board> mmcblk0p1
   ```

3. Enter bash shell on the target device.
4. Mount a USB drive or network storage on the target device.
5. Generate golden image in the mounted storage device by running the following command:

   ```bash
   sudo dd if=<rootfs partition> of=<mount point>/golden.img.raw
   ```

   The `<rootfs partition>` is `/dev/mmcblk0p2` for the internal eMMC storage device or `/dev/nvme0n1p2` for the external NVMe storage device.

6. Copy the golden image into the TARGET_BSP directory on the host machine.
   For the internal eMMC storage device, running the following command:

   ```bash
   sudo cp golden.img.raw ${TARGET_BSP}/bootloader/system_root_encrypted.img.raw
   ```

   For the external NVMe storage device, running the following command:

   ```bash
   sudo cp golden.img.raw ${TARGET_BSP}/bootloader/system_root_encrypted.img_ext.raw
   ```

   In the following steps, use `${golden_img}` to specify the path of this golden image.

7. Put the target device into recovery mode and dump ECID by running the following command:

   ```bash
   sudo ./flash.sh --no-flash --no-systemimg -Z <target board> internal
   ```

   You can get the ECID from the output.

8. Dump UUID of the golden image by running the command:

   ```bash
   uuid="$(sudo cryptsetup luksUUID "${golden_img}")"
   ```

9. Generate the unique passphrase and store it into a temporary file by running the following command:

   ```bash
   ./tools/disk_encryption/gen_luks_passphrase.py -u -e <ECID> \
     -k <disk encryption key> -c "${uuid}" | awk '{printf "%s", $0}' >/tmp/passphrase_unique.tmp
   ```

   The `<disk encryption key>` is the key used to enable disk encryption when flashing the device.

10. Generate the generic passphrase and store it into a temporary file by running the following command:

    ```bash
    ./tools/disk_encryption/gen_luks_passphrase.py -g \
      -k <disk encryption key> -c "${uuid}" | awk '{printf "%s", $0}' >/tmp/passphrase_generic.tmp
    ```

11. Add generic passphrase on the golden image by running the following command:

    ```bash
    sudo cryptsetup luksAddKey --key-file /tmp/passphrase_unique.tmp \
      "${golden_img}" /tmp/passphrase_generic.tmp
    ```

12. Add the `-s` option when generating the OTA payload package.
    For the internal eMMC storage device, running the following command:

    ```bash
    sudo -E ROOTFS_ENC=1 ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
      -s -i <disk encryption key> <target board> <base version>
    ```

    For the external NVMe storage device, running the following command:

    ```bash
    sudo -E ROOTFS_ENC=1 ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
      -s --external-device nvme0n1 -S <rootfs size > -i <disk encryption key> <target board> <base version>
    ```

### Case 19: rootfs A/B enabled, disk encryption enabled

1. Put the target device into recovery mode.
2. Boot the device to initrd by running the following command:

   ```bash
   sudo ./tools/kernel_flash/l4t_initrd_flash.sh --initrd <target board> mmcblk0p1
   ```

3. Enter bash shell on the target device.
4. Mount a USB drive or network storage on the target device.
5. Generate golden images in the mounted storage device by running the following commands:

   ```bash
   sudo dd if=<rootfs partition A> of=<mount point>/golden_a.img.raw
   sudo dd if=<rootfs partition B> of=<mount point>/golden_b.img.raw
   ```

   The `<rootfs partition A>` is `/dev/mmcblk0p3` for the internal eMMC storage device or `/dev/nvme0n1p3` for the external NVMe storage device, and the `<rootfs partition B>` is `/dev/mmcblk0p4` for the internal eMMC storage device or `/dev/nvme0n1p4` for the external NVMe storage device.

6. Copy the golden images into the TARGET_BSP directory on the host machine.
   For the internal eMMC storage device, running the following commands:

   ```bash
   sudo cp golden_a.img.raw ${TARGET_BSP}/bootloader/system_root_encrypted.img.raw
   sudo cp golden_b.img.raw ${TARGET_BSP}/bootloader/system_root_encrypted.img_b.raw
   ```

   For the external NVMe storage device, running the following commands:

   ```bash
   sudo cp golden_a.img.raw ${TARGET_BSP}/bootloader/system_root_encrypted.img_ext.raw
   sudo cp golden_b.img.raw ${TARGET_BSP}/bootloader/system_root_encrypted.img_ext_b.raw
   ```

   In the following steps, use `${golden_a_img}` and `${golden_b_img}` to specify the paths of golden images for rootfs A and rootfs B respectively.

7. Put the target device into recovery mode and dump ECID by running the following command:

   ```bash
   sudo ./flash.sh --no-flash --no-systemimg -Z <target board> internal
   ```

   You can get the ECID from the output.

8. Dump UUID of the golden images by running the command:

   ```bash
   uuid_a="$(sudo cryptsetup luksUUID "${golden_a_img}")"
   uuid_b="$(sudo cryptsetup luksUUID "${golden_b_img}")"
   ```

9. Generate the unique passphrase and store it into a temporary file by running the following commands:

   ```bash
   ./tools/disk_encryption/gen_luks_passphrase.py -u -e <ECID> \
     -k <disk encryption key> -c "${uuid_a}" | awk '{printf "%s", $0}' >/tmp/passphrase_unique_a.tmp
   ./tools/disk_encryption/gen_luks_passphrase.py -u -e <ECID> \
     -k <disk encryption key> -c "${uuid_b}" | awk '{printf "%s", $0}' >/tmp/passphrase_unique_b.tmp
   ```

   The `<disk encryption key>` is the key used to enable disk encryption when flashing the device.

10. Generate the generic passphrase and store it into a temporary file by running the following commands:

    ```bash
    ./tools/disk_encryption/gen_luks_passphrase.py -g \
      -k <disk encryption key> -c "${uuid_a}" | awk '{printf "%s", $0}' >/tmp/passphrase_generic_a.tmp
    ./tools/disk_encryption/gen_luks_passphrase.py -g \
      -k <disk encryption key> -c "${uuid_b}" | awk '{printf "%s", $0}' >/tmp/passphrase_generic_b.tmp
    ```

11. Add generic passphrase on the golden images by running the following commands:

    ```bash
    sudo cryptsetup luksAddKey --key-file /tmp/passphrase_unique_a.tmp \
      "${golden_a_img}" /tmp/passphrase_generic_a.tmp
    sudo cryptsetup luksAddKey --key-file /tmp/passphrase_unique_b.tmp \
      "${golden_b_img}" /tmp/passphrase_generic_b.tmp
    ```

12. Add the `-s` option when generating the OTA payload package.
    For the internal eMMC storage device, running the following command:

    ```bash
    sudo -E ROOTFS_ENC=1 ROOTFS_AB=1 ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
      -s -i <disk encryption key> <target board> <base version>
    ```

    For the external NVMe storage device, running the following command:

    ```bash
    sudo -E ROOTFS_ENC=1 ROOTFS_AB=1 ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
      -s --external-device nvme0n1 -S <rootfs size> -i <disk encryption key> <target board> <base version>
    ```

The content of golden image is now packed into the OTA payload package and then extracted into the corresponding rootfs partition by the default rootfs updater provided by NVIDIA during the OTA update.

---

## 6. Applying OTA with UEFI secure boot enabled

> **Note:** The steps below are used for reference only.

There are three entities involved in the OTA: the host machine that generates the OTA package, the OTA server that responds to the OTA request, and the target device that issues OTA request and does the OTA on the device.

```text
----------------        --------------         -----------------
| Host Machine |        | OTA Server |         | Target Device |
----------------        --------------         -----------------
       |                       |                       |
       |     OTA packages      |                       |
       |---------------------->|                       |
       |         (1)           |                       |
       |                       |    OTA request with   |
       |                       |      unique UUIDs     |
       |                       |<----------------------|
       |                       |          (2)          |
       |      Unique UUIDs     |                       |
       |<----------------------|                       |
       |         (3)           |                       |
       |                       |                       |
       |    UEFI secure boot   |                       |
       |    overlay package    |                       |
       |---------------------->|                       |
       |         (4)           |                       |
       |                       |      OTA packages     |
       |                       |---------------------->|
       |                       |          (5)          |
       |                       |                       |
```

The host machine generates OTA packages and sends them to the OTA server. The OTA packages include the OTA payload package (`ota_payload_package.tar.gz`), UEFI secure boot overlay package (`uefi_secureboot_overlay_multi_specs.tar.gz`), and OTA tools package (`ota_tools_<version>_aarch64.tbz2`). The UEFI secure boot overlay package can be either generated along with other packages during OTA package creation or dynamically creating after receiving a request from the OTA server with specific UUIDs provided by the OTA request target device.

The OTA server stores and sends the OTA packages to the target device upon receiving an OTA request. The OTA server may receive the device UUIDs from the target device when received OTA request. If these UUIDs do not match the UUIDs in the pre-generated UEFI secure boot overlay package, the OTA server forwards the UUIDs to the host machine so that a device-specific UEFI secure boot overlay package can be generated on the fly.

The target device sends its unique UUIDs in an OTA request to the OTA server, receives the necessary OTA packages, and then triggers the OTA process.

### Example commands to generate OTA payload package with UEFI secure boot enabled on the host machine

You can use the `l4t_generate_ota_package.sh` to generate OTA packages for UEFI secure boot. The following are example commands with sample values:

1. The UEFI secure boot is enabled on the target device.
2. The UUID of the APP (or APP_ENC for disk encryption enabled) partition is `11111111-1111-1111-111111111111`.
3. The UUID of the APP_b (or APP_ENC_b for disk encryption enabled) partition is `22222222-2222-2222-222222222222`.
4. The UUID of the UDA partition that is encrypted is `33333333-3333-3333-333333333333`.
5. The UEFI keys configuration file is `Linux_for_Tegra/uefi_keys/uefi_keys.conf`.
6. The UEFI encryption key is `Linux_for_Tegra/uefi_enc.key`.

### Case 20: Jetson AGX Orin Devkit — UEFI payloads signed only (R35.5.0)

```bash
sudo -E ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
  --uefi-keys ./uefi_keys/uefi_keys.conf \
  --rootfs-uuid 11111111-1111-1111-111111111111 \
  jetson-agx-orin-devkit R35-5
```

### Case 21: Jetson AGX Orin Devkit — UEFI payloads signed and encrypted, rootfs A/B enabled (R36.3.0)

```bash
sudo -E ROOTFS_AB=1 ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
  --uefi-keys ./uefi_keys/uefi_keys.conf \
  --uefi-enc ./uefi_enc.key \
  --rootfs-uuid 11111111-1111-1111-111111111111 \
  --rootfs-b-uuid 22222222-2222-2222-222222222222 \
  jetson-agx-orin-devkit R36-3
```

### Case 22: Jetson Orin NX/Nano (NVMe) — UEFI payloads signed only, disk encryption enabled (R35.5.0)

```bash
sudo -E ROOTFS_ENC=1 ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
  --external-device nvme0n1 \
  --uefi-keys ./uefi_keys/uefi_keys.conf \
  --rootfs-uuid 11111111-1111-1111-111111111111 \
  --uda-uuid 33333333-3333-3333-333333333333 \
  jetson-orin-nano-devkit R35-5
```

### Case 23: Jetson Orin NX/Nano (NVMe) — UEFI payloads signed and encrypted, rootfs A/B and  disk encryption enabled (R36.3.0)

```bash
sudo -E ROOTFS_AB=1 ROOTFS_ENC=1 ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
  --external-device nvme0n1 \
  --uefi-keys ./uefi_keys/uefi_keys.conf \
  --uefi-enc ./uefi_enc.key \
  --rootfs-uuid 11111111-1111-1111-111111111111 \
  --rootfs-b-uuid 22222222-2222-2222-222222222222 \
  --uda-uuid 33333333-3333-3333-333333333333 \
  jetson-orin-nano-devkit R36-3
```

### Case 24: Jetson Thor Devkit — UEFI payloads signed only, disk encryption enabled (R38.4.0)

```bash
sudo -E ROOTFS_ENC=1 ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
  --external-device nvme0n1 \
  --uefi-keys ./uefi_keys/uefi_keys.conf \
  --rootfs-uuid 11111111-1111-1111-111111111111 \
  --uda-uuid 33333333-3333-3333-333333333333 \
  jetson-agx-thor-devkit R38-4
```

### Case 25: Jetson Thor Devkit — UEFI payloads signed only, rootfs A/B and disk encryption enabled (R38.4.0)

```bash
sudo -E ROOTFS_AB=1 ROOTFS_ENC=1 ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
  --external-device nvme0n1 \
  --uefi-keys ./uefi_keys/uefi_keys.conf \
  --rootfs-uuid 11111111-1111-1111-111111111111 \
  --rootfs-b-uuid 22222222-2222-2222-222222222222 \
  --uda-uuid 33333333-3333-3333-333333333333 \
  jetson-agx-thor-devkit R38-4
```

### Example of the OTA update flow

This example assumes the following scenario:

1. The host machine also plays the role of the OTA server.
2. The files necessary for OTA are transferred through an SSH session.
3. The IP address of the target device is 192.168.55.1.
4. The IP address of the host machine is 192.168.55.100.
5. The target device is a Jetson AGX Orin Devkit, and its base version is R36.3.0

### Case 26: OTA update flow

1. The host machine runs `l4t_generate_ota_package.sh` with given UUIDs to generate `ota_payload_package.tar.gz`, `uefi_secureboot_overlay_multi_specs.tar.gz`, and `uefi_base_multi_specs.tar.gz`.

   ```bash
   sudo -E ./tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh \
     --uefi-keys ./uefi_keys/uefi_keys.conf \
     --uefi-enc ./uefi_enc.key \
     --rootfs-uuid 11111111-1111-1111-111111111111 \
     jetson-agx-orin-devkit R36-3
   ```

2. The target device runs a command to collect necessary UUIDs and store the results to file `uuids.txt`. Then sends OTA request along with `uuids.txt` to the host machine.

   ```bash
   rootfs_a_uuid="$(lsblk -P -n -o PARTLABEL,UUID /dev/mmcblk0 \
     | grep APP | cut -d\  -f 2 | cut -d= -f 2 | sed 's/^\"\(.*\)\"$/\1/')"
   echo "rootfs_a_uuid:${rootfs_a_uuid}" >"uuids.txt"
   scp uuids.txt nvidia@192.168.55.100:~/
   ```

   For more details, refer to the demo script `Linux_for_Tegra/tools/ota_tools/version_upgrade/demo_target_ota_uefi_sb.sh`.

3. The host machine gets UUIDs from the received `uuids.txt` and executes the script `l4t_ota_sign_uefi_base.sh` to generate `uefi_secureboot_overlay_multi_specs.tar.gz` with obtained UUIDs as input parameters. Afterward, the host machine sends the `ota_tools_<version>_aarch64.tbz2`, `ota_payload_package.tar.gz`, and `uefi_secureboot_overlay_multi_specs.tar.gz` archives to the target device.

   ```bash
   rootfs_a_uuid="$(grep "rootfs_a_uuid" ~/uuids.txt | cut -d: -f 2 || true)"
   sudo ./tools/ota_tools/version_upgrade/l4t_ota_sign_enc_uefi_base.sh \
     --uefi-keys uefi_keys/uefi_keys.conf \
     --uefi-enc ./uefi_enc.key \
     --rootfs-uuid "${rootfs_a_uuid}"
   scp ../ota_tools_<version>_aarch64.tbz2 nvidia@192.168.55.1:~/
   scp ./bootloader/jetson-agx-orin-devkit/ota_payload_package.tar.gz \
     nvidia@192.168.55.1:~/
   scp ./bootloader/uefi_overlay/uefi_secureboot_overlay_multi_specs.tar.gz \
     nvidia@192.168.55.1:~/
   ```

   For more details, refer to the demo script `Linux_for_Tegra/tools/ota_tools/version_upgrade/demo_host_ota_uefi_sb.sh`

4. The target device receives all OTA packages, extracts `ota_tools_<version>_aarch64.tbz2`, and triggers the OTA update.

   ```bash
   tar xjpf ota_tools_<version>_aarch64.tbz2
   cd Linux_for_Tegra/tools/ota_tools/version_upgrade
   sudo ./nv_ota_start.sh ~/ota_payload_package.tar.gz
   ```
