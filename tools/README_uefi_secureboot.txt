************************************************************************
                           Linux for Jetson
                   Enabling/Verifying UEFI Secure Boot
                                README

                             Version 2.0
************************************************************************

References:
  - https://wiki.archlinux.org/title/Unified_Extensible_Firmware_Interface/Secure_Boot#Implementing_Secure_Boot
  - https://www.rodsbooks.com/efi-bootloaders/controlling-sb.html


Prerequisites:

   Before you begin, ensure that the following utilities are installed in your host:
   - openssl
   - device-tree-compiler
   - efitools
   - uuid-runtime


1. Prepare the Keys

   To generate the keys, refer to the "Security" -> "Secure Boot" -> "UEFI Secure Boot" -> "Prepare the PK, KEK, db Keys" section
   of the developer guide.


2. Enable UEFI Secure Boot through Flashing

   To enable UEFI Secure Boot through flashing, refer to "Security" -> "Secure Boot" -> "UEFI Secure Boot" -> "Enable the UEFI Secure Boot"
   -> "Method One: Enable UEFI Secure Boot at Flashing Time" section of the developer guide.


3. Enable UEFI Secure Boot via Capsule Update

   To enable UEFI Secure Boot via capsule update, refer to "Security" -> "Secure Boot" -> "UEFI Secure Boot" -> "Enable the UEFI Secure Boot"
   -> "Method Two: Enable UEFI Secure Boot Using Capsule Update" section of the developer guide.


4. Enable UEFI Secure Boot at run-time from the Kernel

   To enable UEFI Secure Boot at run-time from the kernel, refer to "Security" -> "Secure Boot" -> "UEFI Secure Boot"
   -> "Enable the UEFI Secure Boot" -> "Method Three: Enable UEFI Secure Boot Using UEFI Utilities from an Ubuntu Prompt" section of
   the developer guide.


5. Verify UEFI Secure Boot

   a. Verify the UEFI Secure Boot for Jetson Thor

   Corrupt any bytes in any UEFI payload (or its signature file). UEFI can detect the corruption and fail over to the next boot mode.

   Here is a list of the UEFI payloads:
   - In the rootfs:
     - ``/boot/Image``: A signed file; Its signature is stored along with the file.
     - ``/boot/initrd``: Its signature file is ``/boot/initrd.sig``.
     - ``/boot/dtb/kernel_tegra*.dtb``: Its signature file is ``/boot/dtb/kernel_tegra*.dtb.sig``.
     - ``/boot/extlinux/extlinux.conf``: Its signature file is ``/boot/extlinux/extlinux.conf.sig``.

   - In partitions:
     - ``BOOTAA64.efi`` in the esp partition.

   Note: Save a copy of the file that you intend to corrupt.

      i. Run the following command to change any bytes in any of UEFI payloads or their .sig files:

         For example, the following command changes the byte at 0x10 of 'Image' to 0xa1:
         $ sudo printf '\xa1' | dd conv=notrunc of=/boot/Image bs=1 seek=$((0x10))

      ii. Reboot the target. UEFI detects the corruption. After attempting direct boot three times, it boots from the recovery partition.
          During failover, UEFI should print messages like the following:

          L4TLauncher: Attempting Direct Boot
          ExtLinuxBoot: Unable to load image: \boot\Image Access Denied
          L4TLauncher: Unable to boot via extlinux: Access Denied
          L4TLauncher: Attempting Kernel Boot
          ReadAndroidStyleKernelPartition: Unable to locate partition

      Note: The message "ReadAndroidStyleKernelPartition: Unable to locate partition" is printed because, beginning with JetPack 7.1, the Kernel partition is removed for Jetson Thor.

      iii. To recover after successfully booting from recovery partition and entering bash, restore the original file and then reboot.


   b. Verify the UEFI Secure Boot for Jetson Orin

   Corrupt any bytes in any UEFI payload (or its signature file), check whether UEFI can detect the corruption, and fail over to the next boot mode.

   Here is a list of the UEFI payloads:
   - In the rootfs:
     - ``/boot/Image``: A signed file. Its signature is stored along with the file.
     - ``/boot/initrd``: Its signature file is ``/boot/initrd.sig``.
     - ``/boot/dtb/kernel_tegra*.dtb``: Its signature file is ``/boot/dtb/kernel_tegra*.dtb.sig``.
     - ``/boot/extlinux/extlinux.conf``: Its signature file is ``/boot/extlinux/extlinux.conf.sig``.

   - In partitions:
     - ``boot.img`` in the kernel partition.
     - ``kernel-dtb`` in the kernel-dtb partition.
     - ``BOOTAA64.efi`` in the esp partition.

   Note: Save a copy of the file that you intend to corrupt.

      i. Run the following command to change any bytes in any of UEFI payloads or their .sig files:

         For example, the following command changes the byte at 0x10 of 'Image' to 0xa1:
         $ sudo printf '\xa1' | dd conv=notrunc of=/boot/Image bs=1 seek=$((0x10))

      ii. Reboot the target. UEFI detects the corruption and boots from the kernel partition.
          During failover, UEFI should print messages like the following:

          L4TLauncher: Attempting Direct Boot
          ExtLinuxBoot: Unable to load image: \boot\Image Access Denied
          L4TLauncher: Unable to boot via extlinux: Access Denied
          L4TLauncher: Attempting Kernel Boot

      iii. To recover after successfully booting from kernel partition, restore the original file and then reboot.


   c. Boot with UEFI Payload Signed with the Additional ``db_2`` for Jetson Thor and Jetson Orin

      Do the following test steps if the additional ``db_2`` is enrolled.

      i. Sign the UEFI payloads with ``db_2`` on host. Refer to the "Security" -> "Secure Boot" -> "UEFI Secure Boot" -> "Enable the UEFI Secure Boot"
         -> "Method Three: Enable UEFI Secure Boot Using UEFI Utilities from an Ubuntu Prompt" -> "Generate the Signed UEFI Payloads"
         section of the developer guide.

      ii. Download the signed payloads to the target. Refer to the "Security" -> "Secure Boot" -> "UEFI Secure Boot" -> "Enable the UEFI Secure Boot"
          -> "Method Three: Enable UEFI Secure Boot Using UEFI Utilities from an Ubuntu Prompt"
          -> "Download and Enroll the Secure Boot Artifacts Using the Ubuntu Prompt" section of the developer guide.

      iii. Reboot the target. The system should be able to boot to Ubuntu.


6. Update the db/dbx Keys with a Capsule Update

   a. To update the db/dbx keys with a capsule update, refer to "Security" -> "Secure Boot" -> "UEFI Secure Boot"
   -> "Update the db/dbx Keys with a Capsule Update" section of the developer guide.


   b. Verify the UEFI payload that was signed by the updated db key.

      i. To sign the UEFI payload with the update_db_0 or update_db_1 key pair, refer to "Security" -> "Secure Boot" -> "UEFI Secure Boot"
         -> "Method Three: Enable UEFI Secure Boot Using UEFI Utilities from an Ubuntu Prompt" -> "Generate the Signed UEFI Payloads"
         section of the developer guide.

      ii. Replace the UEFI payload in the file system.

      iii. Reboot the target. The system should be able to boot to Ubuntu.


   c. Verify the UEFI payload that was signed by the dbx key.

      i. To sign UEFI payload with the dbx key pair, refer to "Security" -> "Secure Boot" -> "UEFI Secure Boot"
         -> "Method Three: Enable UEFI Secure Boot Using UEFI Utilities from an Ubuntu Prompt" -> "Generate the Signed UEFI Payloads"
         section of the developer guide.

      ii. Replace the UEFI payload in the file system.

      iii. Reboot the target:
           (1) For Jetson Orin, the target fails over to boot from the kernel partition.
           (2) For Jetson Thor, the target fails over to boot from the recovery partition after ``Attempting Direct Boot`` for three times.
