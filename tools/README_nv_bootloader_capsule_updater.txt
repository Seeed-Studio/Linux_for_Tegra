# SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
# property and proprietary rights in and to this material, related
# documentation and any modifications thereto. Any use, reproduction,
# disclosure or distribution of this material and related documentation
# without an express license agreement from NVIDIA CORPORATION or
# its affiliates is strictly prohibited.

The NVIDIA Public release Package provides a tool to update the QSPI flash partitions
of Jetson devkits. This document provides information about updating the bootloader in
the QSPI flash.


1. Prerequisites:

   - This tool is in the Public Release Package. It must be extracted to the Jetson
     Linux Package work directory (${Your_path}/Linux_for_Tegra/).


2. Preparation.

   - Download the Jetson Linux Package and make it ready for flashing. The work
     directory is "${Your_path}/Linux_for_Tegra/".


3. Generate the QSPI flash bootloader payload.

   Here take the IGX as an example.

   a. Generate the BUP payload.
         $ cd ${Your_path}/Linux_for_Tegra/
         $ sudo ./l4t_generate_soc_bup.sh -e t23x_igx_bl_spec t23x

   b. Pack the generated BUP to the Capsule payload.
          $ ./generate_capsule/l4t_generate_soc_capsule.sh \
              -i ./bootloader/payloads_t23x/bl_only_payload \
              -o ./Tegra_IGX_BL.Cap t234

      Refer to the Jetson-Linux Developer Guide for more information about the Capsule payload.

4. Update the QSPI flash.

   a. Copy the generated QSPI flash payload to the target filesystem.
      $ scp ${Your_host_user_name}@${Your_host_IP}:${Your_path}/Linux_for_Tegra/Tegra_IGX_BL.Cap /opt

   b. Execute the bootloader_updater utility to update the IGX bootloader on QSPI flash.
      $ sudo nv_bootloader_capsule_updater.sh -q /opt/Tegra_IGX_BL.Cap

   c. Reboot the tareget to update the QSPI flash image on non-current slot bootloader.

   d. To check the Capsule update status, run the nvbootctrl command after boot to system:
      $ sudo nvbootctrl dump-slots-info

      Note: Capsule update status value "1" means update successfully. About the Capsule update status,
            please refer to developer guide for more information.

   e. To sync bootloader A/B slots, do the step b to d again.

   f. Then the bootloader partitions on QSPI flash(both A/B slots) are updated.
