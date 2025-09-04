************************************************************************
                           Linux for Jetson
                   Enabling/Verifying UEFI Secureboot
                                README

                             Version 1.0
************************************************************************


Enable UEFI Secureboot overall flow:

  1. Prepare the keys.
     a. Prepare the PK, the KEK, and the db keys.
        i. Generate the PK, KEK, db RSA keypairs and certificates.
        ii. Create a UEFI keys config file.
        iii. To enroll the keys from target, generate the UefiDefaultSecurityKeys.dtbo file and the auth files for the keys.
     b. Generate the signed UEFI payloads.

  2. Enable the UEFI Secureboot at flashing time.
     a. Enable Secureboot through flash.sh during flashing.
        i. Use the --uefi-keys <keys_conf> option to provide signing keys and enable UEFI secure boot.

  3. Enable UEFI Secureboot at run-time from the kernel.
     a. Enable Secureboot by running the UEFI utility running from Ubuntu.
        i. Download the PK, the KEK, and the db auth files from the host.
        ii. Enroll KEK and db.
        iii. Download and write the signed UEFI payloads.
        iv. Enroll PK.

  4. Verify the UEFI Secureboot.
     a. Check whether UEFI Secureboot is enabled.
     b. Check whether the system can boot from the kernel partition.
     c. Corrupt any bytes in any UEFI payload or its sig file.
        i. If the UEFI payload corruption is in the rootfs, check whether UEFI can detect the corruption and failover to boot
           from kernel partition.
        ii. If the UEFI payload corruption is in the partitions, such as esp, kernel, or kernel-dtb, check whether UEFI can
            detect the corruption and boot to UEFI shell.
     d. Check the additional db, including db_2 if db_2 is flashed during flashing.
        i. Sign the UEFI payloads with db_2 on a host.
        ii. Download and write the signed payloads to the target.
        iii. Check whether the system can boot.

  5. Update the db/dbx keys with a capsule update.
     a. Prepare the update keys.
        i. Generate the KEK, the db, and the dbx keys auth file for the update.
        ii. Create a UEFI update keys config file with the generated keys auth file.
        iii. Generate the UefiUpdateSecurityKeys.dtbo file.
     b. Generate the capsule payload with UEFI secureboot enabled.
     c. Trigger a capsule update.
     d. Check and verify update keys.
        i. Check the secureboot status.
        ii. Check the updated KEK.
        iii. Check the updated db.
        iv. Check the updated dbx.
     e. Verify the UEFI payload that was signed by the updated db key.
     f. Verify the UEFI payload that was signed by dbx key.

Note: The utilities and parameters are used to generate keys, and the self-signed certificates in following sample commands are
      used only for demonstration and test purposes. For production, follow your official certificate generation procedure.


References:
  - https://wiki.archlinux.org/title/Unified_Extensible_Firmware_Interface/Secure_Boot#Implementing_Secure_Boot
  - https://www.rodsbooks.com/efi-bootloaders/controlling-sb.html


Prerequisites:

   Before you begin, ensure that the following utilities are installed in your host:
   - openssl
   - device-tree-compiler
   - efitools
   - uuid-runtime


1. Prepare the keys:

   a. Prepare the PK, KEK, and db keys.
      i. Generate the PK, the KEK, and the db RSA keypairs and certificates.
         $ cd to <LDK_DIR>
         $ mkdir uefi_keys
         $ cd uefi_keys

         (1) Generate the PK RSA keypair and certificate.
             $ openssl req -newkey rsa:2048 -nodes -keyout PK.key  -new -x509 -sha256 -days 3650 -subj "/CN=my Platform Key/" -out PK.crt

         (2) Generate the KEK RSA keypair and certificate.
             $ openssl req -newkey rsa:2048 -nodes -keyout KEK.key  -new -x509 -sha256 -days 3650 -subj "/CN=my Key Exchange Key/" -out KEK.crt

         (3) Generate the db_1 RSA keypair and certificate.
             $ openssl req -newkey rsa:2048 -nodes -keyout db_1.key  -new -x509 -sha256 -days 3650 -subj "/CN=my Signature Database key/" -out db_1.crt

         (4) Generate the db_2 RSA keypair and certificate.
             $ openssl req -newkey rsa:2048 -nodes -keyout db_2.key  -new -x509 -sha256 -days 3650 -subj "/CN=my another Signature Database key/" -out db_2.crt

      ii. Create a UEFI keys config file with generated keys.
          $ vim uefi_keys.conf

          Insert following lines:
          UEFI_PK_KEY_FILE="PK.key";
          UEFI_PK_CERT_FILE="PK.crt";
          UEFI_KEK_KEY_FILE="KEK.key";
          UEFI_KEK_CERT_FILE="KEK.crt";
          UEFI_DB_1_KEY_FILE="db_1.key";
          UEFI_DB_1_CERT_FILE="db_1.crt";
          UEFI_DB_2_KEY_FILE="db_2.key";
          UEFI_DB_2_CERT_FILE="db_2.crt";

          Note: UEFI_DB_2_XXX entries are optional.

      iii. To enroll keys from the target, generate the UefiDefaultSecurityKeys.dtbo file and the auth files for the keys.
           To generate, you need the following items:
           - The UefiDefaultSecurityKeys.dtbo file, which is needed in flash.sh to flash UEFI default security keys to target;
           - The esl and auth files for the keys that are generated in uefi_keys/_out folder.
           $ cd ..
           $ sudo tools/gen_uefi_default_keys_dts.sh uefi_keys/uefi_keys.conf
           $ sudo chmod 644 uefi_keys/_out/*.auth

   b. Generate the signed UEFI payloads.
      - These steps are performed automatically by flash.sh if enabling Secureboot through flashing.
      - These steps are needed if you want to enable Secureboot at run-time from kernel.
      - These steps are also needed if you have new UEFI payload files, or a new key to sign those payload files.

      The UEFI payloads are:
      - extlinux.conf,
      - initrd,
      - kernel images (in rootfs, and in kernel and recovery partitions),,
      - kernel-dtb images (in rootfs, and in kernel-dtb and recovery-dtb partitions), and
      - BOOTAA64.efi, a.k.a. L4tLauncher, the OS loader.

      The following steps assume that you have copied the required unsigned UEFI paylaods to uefi_keys/ folder.

      i. Sign extlinux.conf file using db.
         - You can replace db key with db_1 or db_2 (if UEFI_DB_2_XXX is specified in uefi_keys.conf) key in the following steps.
         - The flash.sh script uses db_1 key to generate all UEFI payloads.
         $ openssl cms -sign -signer db.crt -inkey db.key -binary -in extlinux.conf -outform der -out extlinux.conf.sig

      ii. Sign the initrd file using db.
          $ openssl cms -sign -signer db.crt -inkey db.key -binary -in initrd -outform der -out initrd.sig

      iii. Sign the kernel file, also known as Image, of the rootfs using db.
           $ cp Image Image.unsigned
           $ sbsign --key db.key --cert db.crt --output Image Image

      iv. Sign the kernel-dtb file of the rootfs using db.
          - The following examples use Concords' SKU 4 kernel-dtb filename.
          - Replace it with the appropriate kernel-dtb filename of your target.
          $ openssl cms -sign -signer db.crt -inkey db.key -binary -in kernel_tegra234-p3701-0004-p3737-0000.dtb -outform der -out kernel_tegra234-p3701-0004-p3737-0000.dtb.sig

      v. Sign the boot.img file of the kernel partition using db.
         Note: Before signing boot.img, the kernel, also known as Image, needs to be signed.
               If the kernel has been signed in the previous step, skip the next two commands.
         $ cp Image Image.unsigned                                    # issue this command only when Image has not been signed
         $ sbsign --key db.key --cert db.crt --output Image Image     # issue this command only when Image has not been signed
         $ ../bootloader/mkbootimg --kernel Image --ramdisk initrd --board <rootdev> --output boot.img --cmdline <cmdline_string>
         where <cmdline_string> is (when generated in flash.sh):
           for Xavier series: root=/dev/mmcblk0p1 rw rootwait rootfstype=ext4 console=ttyTCU0,115200n8 console=tty0 fbcon=map:0 net.ifnames=0
           for Orin series: root=/dev/mmcblk0p1 rw rootwait rootfstype=ext4 mminit_loglevel=4 console=ttyTCU0,115200 console=ttyAMA0,115200 firmware_class.path=/etc/firmware fbcon=map:0 net.ifnames=0

         $ cp boot.img boot.img.unsigned
         $ openssl cms -sign -signer db.crt -inkey db.key -binary -in boot.img -outform der -out boot.img.sig
         $ truncate -s %2048 boot.img
         $ cat boot.img.sig >> boot.img

      vi. Sign the kernel-dtb file of the kernel-dtb partition using db.
          - The following examples use Concords' SKU 4 kernel-dtb filename.
          - Replace it with the appropriate kernel-dtb filename of your target.
          $ cp tegra234-p3701-0004-p3737-0000.dtb tegra234-p3701-0004-p3737-0000.dtb.unsigned
          $ openssl cms -sign -signer db.crt -inkey db.key -binary -in tegra234-p3701-0004-p3737-0000.dtb -outform der -out tegra234-p3701-0004-p3737-0000.dtb.sig
          $ truncate -s %2048 tegra234-p3701-0004-p3737-0000.dtb
          $ cat tegra234-p3701-0004-p3737-0000.dtb.sig >> tegra234-p3701-0004-p3737-0000.dtb

      vii. Sign the recovery.img file of the recovery partition using db.
           Note: Before signing recovery.img, the kernel needs to be signed. If the kernel has been signed in the previous
                 step, skip the next two commands.
           $ cp Image Image.unsigned                                    # issue this command only when Image has not been signed
           $ sbsign --key db.key --cert db.crt --output Image Image     # issue this command only when Image has not been signed
           $ ../bootloader/mkbootimg --kernel Image --ramdisk ../bootloader/recovery.ramdisk --output recovery.img --cmdline <rec_cmdline_string>
           where <rec_cmdline_string> is:
             for Xavier series: "root=/dev/initrd rw rootwait console=ttyTCU0,115200n8 fbcon=map:0 net.ifnames=0 video=tegrafb no_console_suspend=1 earlycon=tegra_comb_uart,mmio32,0x0c168000 sdhci_tegra.en_boot_part_access=1"
             for Orin series: "root=/dev/initrd rw rootwait mminit_loglevel=4 console=ttyTCU0,115200 firmware_class.path=/etc/firmware fbcon=map:0 net.ifnames=0"

           $ cp recovery.img recovery.img.unsigned
           $ openssl cms -sign -signer db.crt -inkey db.key -binary -in recovery.img -outform der -out recovery.img.sig
           $ truncate -s %2048 recovery.img
           $ cat recovery.img.sig >> recovery.img

      viii. Sign the recovery kernel-dtb file of the recovery-dtb partition using db.
            - The following examples use Concords' SKU 4 recovery-dtb filename.
            - Replace it with the appropriate recovery-dtb filename of your target.
            $ cp tegra234-p3701-0004-p3737-0000.dtb.rec tegra234-p3701-0004-p3737-0000.dtb.rec.unsigned
            $ openssl cms -sign -signer db.crt -inkey db.key -binary -in tegra234-p3701-0004-p3737-0000.dtb.rec -outform der -out tegra234-p3701-0004-p3737-0000.dtb.rec.sig
            $ truncate -s %2048 tegra234-p3701-0004-p3737-0000.dtb.rec
            $ cat tegra234-p3701-0004-p3737-0000.dtb.rec.sig >> tegra234-p3701-0004-p3737-0000.dtb.rec

      ix. Sign the BOOTAA64.efi file using db.
          $ cp BOOTAA64.efi BOOTAA64.efi.unsigned
          $ sbsign --key db.key --cert db.crt --output BOOTAA64.efi BOOTAA64.efi


2. During flashing, enable Secureboot through flash.sh.
   $ sudo ./flash.sh --uefi-keys uefi_keys/uefi_keys.conf <target> mmcblk0p1


3. Enable UEFI Secureboot at run-time from the kernel.

   Note: The AGX Xavier platform is not supported.

   a. Complete the preparations.
      $ sudo su
      $ dhclient eth0
      $ apt update
      $ apt install efitools
      $ apt install efivar

   b. Ensure that Secureboot is not enabled, for example, the following command returns value of 00.
      $ efivar -n 8be4df61-93ca-11d2-aa0d-00e098032b8c-SecureBoot

      $ mkdir /uefi_keys
      $ cd /uefi_keys

   c. Download the PK.auth, the KEK.auth, the db_1.auth and the db_2.auth files.
      $ scp <host_ip>:<LDK_DIR>/<uefi_keys/_out/*.auth .

   d. Enroll the KEK and the db keys. PK has to be enrolled last.
      $ efi-updatevar -f /uefi_keys/db.auth db
      $ efi-updatevar -f /uefi_keys/KEK.auth KEK

   e. Download and write the signed UEFI payloads.

      i. Download these signed UEFI pyloads from host to their corresponding storage.

         Note: You might want to save copies of the original files.

                             filename                                           target's folder
         =================================================================      ===============
         extlinux.conf and extlinux.conf.sig                                    /boot/extlinux/
         initrd and initrd.sig                                                  /boot/
         kernel_tegra234-p3701-0004-p3737-0000.dtb, and
           kernel_tegra234-p3701-0004-p3737-0000.dtb.sig (for Concord SKU 4)   /boot/dtb/
         Image                                                                  /boot/
         BOOTAA64.efi                                                           /uefi_keys/
         boot.img                                                               /uefi_keys/
         tegra234-p3701-0004-p3737-0000.dtb (for Concord SKU 4)                 /uefi_keys/
         recovery.img                                                           /uefi_keys/
         tegra234-p3701-0004-p3737-0000.dtb.rec (for Concord SKU 4)             /uefi_keys/

      ii. Write the signed BOOTAA64.efi to "esp" partition.

          (1) Issue the blkid command and look for PARTLABEL="esp" in the output to determine in which partition "esp" is located.
              $ blkid | grep esp

              Note: If there are multiple devices that have the esp partition, select the one that is the boot device.

          (2) Mount the esp partition.
              $ mount /dev/mmcblk0p10 /mnt

              Note: The esp is mounted as /dev/mmcblk0p10 in this example.

          (3) Copy the BOOTAA64.efi file to the mounted esp directory and then unmount the esp partition.
              $ cd /uefi_keys
              $ cp BOOTAA64.efi /mnt/EFI/BOOT/BOOTAA64.efi
              $ sync
              $ umount /mnt

      iii. Write the signed boot.img to A_kernel partition.
           $ blkid | grep kernel
           $ cd /uefi_keys
           $ dd if=boot.img of=/dev/mmcblk0p2 bs=64k

           Note: In this example, the A_kernel is mounted as /dev/mmcblk0p2.

      iv. Write the signed boot.img to B_kernel partition.
          $ dd if=boot.img of=/dev/mmcblk0p5 bs=64k

          Note: In this example, the B_kernel is mounted as /dev/mmcblk0p5.

      v. Write the signed kernel-dtb to A_kernel-dtb partition.
         $ dd if=tegra234-p3701-0004-p3737-0000.dtb of=/dev/mmcblk0p3 bs=64k

         Note: In this example, the A_kernel-dtb is mounted as /dev/mmcblk0p3.

      vi. Write the signed kernel-dtb to B_kernel-dtb partition.
          $ dd if=tegra234-p3701-0004-p3737-0000.dtb of=/dev/mmcblk0p6 bs=64k

          Note: In this example, the B_kernel-dtb is mounted as /dev/mmcblk0p6.

      vii. Write the signed recovery.img to recovery kernel partition.
           $ blkid | grep recovery
           $ cd /uefi_keys
           $ dd if=recovery.img of=/dev/mmcblk0p8 bs=64k

           Note: In this example, the recovery partition is mounted as /dev/mmcblk0p8.

      viii. Write the signed recovery kernel-dtb to recovery-dtb partition.
            $ dd if=tegra234-p3701-0004-p3737-0000.dtb.rec of=/dev/mmcblk0p9 bs=64k

            Note: In this example, the recovery-dtb partition is mounted as /dev/mmcblk0p9.

   f. Enroll PK last.
      $ efi-updatevar -f /uefi_keys/PK.auth PK

   g. Reboot the target device.
      $ reboot

   h. At the Ubuntu prompt, ensure that Secureboot is enabled, and the following command returns a value of 01.
      $ efivar -n 8be4df61-93ca-11d2-aa0d-00e098032b8c-SecureBoot


4. Verify UEFI Secureboot

   a. Verify that UEFI Secureboot is enabled.
      i. Complete the following steps in the UEFI Menu:
         (1) Reboot target device.
         (2) In the UEFI Menu, click Device Manager -> Secure Boot Configuration.
         (3) Ensure that Attempt Secure Boot is selected (with an 'X').
         (4) Press <ESC> to the top UEFI menu and click Continue.

         The target should now boot to Ubuntu prompt.

      ii. Run the following commands in the Ubuntu prompt:
          $ sudo su
          $ dhclient eth0
          $ apt update
          $ apt install efitools
          $ apt install efivar

         (1) Print the entire UEFI secure variable database.
             $ efi-readvar

         (2) List all UEFI variables.
             $ efivar -l

         (3) Check whether Secureboot is enabled. The SecureBoot value should be 01.
             $ efivar -n 8be4df61-93ca-11d2-aa0d-00e098032b8c-SecureBoot

         (4) Run the following efivar -n commands and check the PK, the KEK, and the db values:
             $ efivar -n 8be4df61-93ca-11d2-aa0d-00e098032b8c-PK
             $ efivar -n 8be4df61-93ca-11d2-aa0d-00e098032b8c-KEK
             $ efivar -n d719b2cb-3d3a-4596-a3bc-dad00e67656f-db

   b. Check the booting from the kernel partition.

      i. Reboot the target device.

      ii. In the UEFI Menu, click Device Manager -> NVIDIA Configuration -> L4T Configuration -> L4T Boot Mode -> Kernel Partition.

      iii. Save.

      iv. Return to the top of UEFI Menu, and Continue.

      v. On the screen, ensure that you see "L4TLauncher: Attempting Kernel Boot".

      vi. The system boots to Ubuntu.

      vii. After the steps above are verified, set the device default to boot from rootfs by completing the following tasks:

           (1) Reboot the target device.

           (2) In the UEFI Menu, click Device Manager -> NVIDIA Configuration -> L4T Configuration -> L4T Boot Mode -> Application Default.

   c. Corrupt any bytes in any UEFI payload (or its sig file), check whether UEFI can detect the corruption, and fail over to the next boot mode.

      Here is a list of the UEFI payloads:
      - In the rootfs:
        - /boot/Image (Image is a signed file. Its signature is stored along with the file)
        - /boot/initrd (its sig file is /boot/initrd.sig)
        - /boot/dtb/kernel_tegra*.dtb (its sig file is /boot/dtb/kernel_tegra*.dtb.sig)
        - /boot/extlinux/extlinux.conf (its sig file is /boot/extlinux/extlinux.conf.sig)
      - In partitions:
        - boot.img in kernel partition
        - kerenl-dtb in kernel-dtb partition
        - BOOTAA64.efi in esp partition

      i. Run the following command to change any bytes in any of UEFI payloads or their .sig files:

         For example, the following command changes the byte at 0x10 of 'Image' to 0xa1:
         $ sudo printf '\xa1' | dd conv=notrunc of=/boot/Image bs=1 seek=$((0x10))

      ii. Corrupt an UEFI payload in the rootfs and check whether UEFI can detect the corruption and failover to boot from the kernel partition:

          Note: Save a copy of the file that you intend to corrupt.

          Example #1: Edit the extlinux.conf file and add or delete content to the file.
          Example #2: Corrupt the signed kernel image.

          (1) Reboot the target. UEFI should failover to boot from kernel partition.

          (2) During failover, if there is a extlinux.conf corruption, UEFI should print messages like the following:

              L4TLauncher: Attempting Direct Boot
              OpenAndReadFileToBuffer: boot\extlinux\extlinux.conf failed signature verification: Security Violation
              ProcessExtLinuxConfig:sds Failed to Authenticate boot\extlinux\extlinux.conf (Security Violation)
              L4TLauncher: Unable to process extlinux config: Security Violation
              L4TLauncher: Attempting Kernel Boot
              EFI stub: Booting Linux Kernel...

          (3) To recover after successfully booting from kernel partition, restore the original file, and then reboot.

      iii. Corrupt an UEFI payload in a partition.

           Note: Save a copy of the file that you intend to corrupt.

           Exmaple #3: Corrupt the boot.img file.

           (1) Corrupt the boot.img file, write it to A_kernel partition (see step iii in the "Download and write signed UEFI payloads"
               section), and then reboot from kernel partition (see "Check booting from kernel partition").

           (2) The reboot should fail and enter the UEFI shell.

           (3) To recover, reboot to the UEFI Menu and restore L4T Boot Mode to "Application Default", and reboot again.

           (4) After the reboot, write the original saved file to the partition.

               Note: The corruption of BOOTAA64.efi file requires a reflash of the target.

           Example #4: Corrupt the BOOTAA64.efi file, write it to the esp partition, and then reboot. The reboot should fail and enter the UEFI shell.

   d. Check the additional db (db_2, if db_2 is flashed during flashing).

      i. Sign the UEFI payloads with db_2 on a host.

         To sign files with db_2 key, follow the steps in "Generate signed UEFI payloads". For example, on a host, assuming that the unsigned
         extlinux.conf is copied to <LDK_DIR>/uefi_keys folder.

         $ cd <LDK_DIR>/uefi_keys
         $ openssl cms -sign -signer db_2.crt -inkey db_2.key -binary -in extlinux.conf -outform der -out extlinux.conf.sig

      ii. Download and write the signed payloads to the target. Refer to the steps in "e. Download and write the signed UEFI payloads." in
          "3. Enable UEFI Secureboot at run-time from the kernel."

          For example, on the target:
          $ scp <host_ip>:<LDK_DIR>/uefi_keys/extlinux.conf* /boot/extlinux/

      iii. Check whether the system can boot from file system and reboot. The target should boot to Ubuntu.


5. Update the db/dbx keys with a capsule update.

   a. Prepare the update keys.

     i. Generate the KEK, the db, and the dbx key auth files for the update.
        $ cd to <LDK_DIR>/uefi_keys
        $ GUID=$(uuidgen)

        (1) Generate the KEK RSA keypair and certificate for the update.
            $ openssl req -newkey rsa:2048 -nodes -keyout update_kek_0.key -new -x509 -sha256 -days 3650 -subj "/CN=Update KEK 0/" -out update_kek_0.crt
            $ cert-to-efi-sig-list -g "${GUID}" update_kek_0.crt update_kek_0.esl
            $ sign-efi-sig-list -a -k PK.key -c PK.crt KEK update_kek_0.esl update_kek_0.auth

            Note: Here is some important information:
                  - This step is needed only when a KEK update is required.
                  - The PK.key and PK.crt are the PK private key and PK certificate that were generated when you enrolled the default keys
                    in step "(1) Generate the PK RSA keypair and certificate" in "1. Prepare keys".

        (2) Generate the db RSA keypair and certificate for the update.
            $ openssl req -newkey rsa:2048 -nodes -keyout update_db_0.key -new -x509 -sha256 -days 3650 -subj "/CN=update DB 0/" -out update_db_0.crt
            $ cert-to-efi-sig-list -g "${GUID}" update_db_0.crt update_db_0.esl
            $ sign-efi-sig-list -a -k update_kek_0.key -c update_kek_0.crt db update_db_0.esl update_db_0.auth

            Note: The signing private key (update_kek_0.key) and the certificate (update_kek_0.crt) are generated by running the previous command.
                  They can also be the KEK private key and certificate when you enroll the default keys in step "(2) Generate the KEK RSA keypair and
                  certificate" in "1. Prepare keys"."

        (3) Generate another db RSA keypair and certificate for the update.
            $ openssl req -newkey rsa:2048 -nodes -keyout update_db_1.key -new -x509 -sha256 -days 3650 -subj "/CN=update DB 1/" -out update_db_1.crt
            $ cert-to-efi-sig-list -g "${GUID}" update_db_1.crt update_db_1.esl
            $ sign-efi-sig-list -a -k KEK.key -c KEK.crt db update_db_1.esl update_db_1.auth

        (4) Generate db_2 auth for the dbx update.
            $ cert-to-efi-sig-list -g "${GUID}" db_2.crt db_2.esl
            $ sign-efi-sig-list -a -k update_kek_0.key -c update_kek_0.crt dbx db_2.esl dbx_db_2.auth

        Note: The db_2 certificate is generated when you enroll the default keys in step "(4) Generate the db_2 RSA keypair and certificate"
              in "1. Prepare keys".

     ii. Create a UEFI update keys config file with the generated key auth files.
         This update keys config file example includes all three key types, and users can update only one or two key types.

         $ vim uefi_update_keys.conf

         Insert following lines:
         UEFI_DB_1_KEY_FILE="update_db_0.key";  # UEFI payload signing key
         UEFI_DB_1_CERT_FILE="update_db_0.crt"; # UEFI payload signing key certificate

         UEFI_UPDATE_PRE_SIGNED_KEK_0="update_kek_0.auth"

         UEFI_UPDATE_PRE_SIGNED_DB_0="update_db_0.auth"
         UEFI_UPDATE_PRE_SIGNED_DB_1="update_db_1.auth"

         UEFI_UPDATE_PRE_SIGNED_DBX_0="dbx_db_2.auth"

         Note: Here is some important information:
               - The UEFI_DB_1_KEY_FILE and UEFI_DB_1_CERT_FILE are used to sign UEFI payloads such as L4TLauncher, kernel, and kernel-dtb.
                 It can be the same signing key db_1.key and db_1.crt used when initially enabling UEFI secure boot or the update key update_db_x
                 defined in this update key conf if UEFI payloads are resigned with update_db_x key which is shown in this example.
               - Users can specify up to 50 UEFI_UPDATE_PRE_SIGNED_KEK_n, UEFI_UPDATE_PRE_SIGNED_DBX_n, or UEFI_UPDATE_PRE_SIGNED_DB_n.

     iii. Generate the UefiUpdateSecurityKeys.dtbo file.
          $ cd ..
          $ sudo tools/gen_uefi_keys_dts.sh uefi_keys/uefi_update_keys.conf
          $ sudo chmod 644 uefi_keys/*.auth

          Note: Users can also run "gen_uefi_keys_dts.sh" to generate default UEFI security keys by using a config file with the UEFI_DEFAULT_PK_ESL,
                UEFI_DEFAULT_KEK_ESL_0 (up to 2), UEFI_DEFAULT_DB_ESL_0 (up to 2), UEFI_DB_1_KEY_FILE and UEFI_DB_1_CERT_FILE settings.

   b. Generate a capsule payload with UEFI secureboot enabled.
      $ cd to <LDK_DIR>

      Complete one of the following tasks:

      - Generate a capsule payload for the Jetson AGX Orin devkits.
        $ sudo ./l4t_generate_soc_bup.sh -e t23x_agx_bl_spec -p "--uefi-keys uefi_keys/uefi_update_keys.conf" t23x
        $ ./generate_capsule/l4t_generate_soc_capsule.sh -i bootloader/payloads_t23x/bl_only_payload -o ./TEGRA_AGX.Cap t234

      - Generate a capsule payload for the Jetson Orin Nano devkits.
        $ sudo ./l4t_generate_soc_bup.sh -e t23x_3767_bl_spec -p "--uefi-keys uefi_keys/uefi_update_keys.conf" t23x
        $ ./generate_capsule/l4t_generate_soc_capsule.sh -i bootloader/payloads_t23x/bl_only_payload -o ./TEGRA_Nano.Cap t234

      Note: Refer to https://docs.nvidia.com/jetson/archives/r35.3.1/DeveloperGuide/text/SD/Bootloader/UpdateAndRedundancy.html#generating-a-multi-spec-capsule-payload
            for more information about generating a capsule payload.

   c. Trigger a capsule update.
      i. Download a capsule payload to the target device.
         $ cd /opt
         $ scp <host_ip>:<LDK_DIR>/TEGRA_AGX.Cap .    ## Take AGX Orin devkit as an example

      ii. Copy the capsule payload to the esp partition.
          $ sudo mkdir -p /opt/nvidia/esp
          $ esp_uuid=$(lsblk -o partlabel,uuid | awk '{ if($1 == "esp") print $2 }')
          $ sudo mount UUID=$esp_uuid /opt/nvidia/esp
          $ sudo mkdir -p /opt/nvidia/esp/EFI/UpdateCapsule
          $ sudo cp /opt/TEGRA_AGX.Cap /opt/nvidia/esp/EFI/UpdateCapsule

      iii. Set the bit2 of the OsIndications UEFI variable.
           $ cd /sys/firmware/efi/efivars/
           $ printf "\x07\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x00" > /tmp/var_tmp.bin
           $ sudo dd if=/tmp/var_tmp.bin of=OsIndications-8be4df61-93ca-11d2-aa0d-00e098032b8c bs=12;sync

      iv. Reboot the Jetson device. UEFI will use the capsule payload to update the non-current slot Bootloader, and then reboot again
          to boot from the newly updated slot.

      Note: Here is important information:
            - Use the AGX Orin devkit as an example.
            - Refer to https://docs.nvidia.com/jetson/archives/r35.3.1/DeveloperGuide/text/SD/Bootloader/UpdateAndRedundancy.html#manually-trigger-the-capsule-update
              for more information about triggering capsule update.

   d. Check and verify the update keys.

      i. Install mokutil on the target device to check the KEK, the db, and the dbx keys.
         If the ethernet has not yet been enabled, enable it now.
         $ sudo dhclient eth0

         $ sudo apt-get update
         $ sudo apt-get install mokutil

         Note: The commands used in step "a. Verify if UEFI Secureboot is enabled" in "4. Verify UEFI Secureboot" can also be used
               to check the updated KEK, db and dbx keys.

      ii. Check the secureboot status.
          $ mok --sb-state

          Note: Output "SecureBoot enabled".

      iii. Check the updated KEK.
           $ mokutil --kek

           Note: The update_kek_0.crt file is in the output key list.

      iv. Check the updated db.
          $ mokutil --db

          Note: The update_db_0.crt and update_db_1.crt files are in the output key list.

      v. Check the updated dbx.
         $ mokutil --dbx

         Note: The db_2.crt file is in the output key list.

   e. Verify the UEFI payload that was signed by the updated db key.

      i. To sign UEFI payload with the update_db_0 or update_db_1 key pair, complete the steps in "d. Check the additional db"
         in "4. Verify UEFI Secureboot".

      ii. Check whether the system can boot from file system after replacing the UEFI payload in file system.

      The target should boot to Ubuntu from the file system.

   f. Verify the UEFI payload that was signed by the dbx key.

      i. To sign UEFI payload with the db_2 key pair, complete the steps in "d. Check the additional db" in "4. Verify UEFI Secureboot".

      ii. Check whether the system can boot from file system after replacing the UEFI payload in file system.

      The target cannot boot to Ubuntu from the file system and should failover to boot from the kernel partition. During failover,
      the UEFI should print following messages:

         L4TLauncher: Attempting Direct Boot
         OpenAndReadFileToBuffer: boot\extlinux\extlinux.conf failed signature verification: Security Violation
         ProcessExtLinuxConfig:sds Failed to Authenticate boot\extlinux\extlinux.conf (Security Violation)
         L4TLauncher: Unable to process extlinux config: Security Violation
         L4TLauncher: Attempting Kernel Boot
         EFI stub: Booting Linux Kernel...


Appendix:

1. Steps to generate Keys for enrolling a KEK
   $ openssl req -newkey rsa:2048 -nodes -keyout new_KEK.key  -new -x509 -sha256 -days 3650 -subj "/CN=<any string to identify the new_KEK key/" -out new_KEK.crt
   $ cert-to-efi-sig-list -g "$(uuidgen)" new_KEK.crt new_KEK.esl
   $ sign-efi-sig-list -k PK.key -c PK.crt db new_KEK.esl new_KEK.auth

2. Steps to generate Keys for enrolling a db (or a dbx):
   $ openssl req -newkey rsa:2048 -nodes -keyout new_db.key  -new -x509 -sha256 -days 3650 -subj "/CN=<any string to identify the new_db key/" -out new_db.crt
   $ cert-to-efi-sig-list -g "$(uuidgen)" new_db.crt new_db.esl
   $ sign-efi-sig-list -k KEK.key -c KEK.crt db new_db.esl new_db.auth

