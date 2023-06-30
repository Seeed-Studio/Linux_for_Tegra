************************************************************************
                           Linux for Jetson
                   Enabling/Verifying UEFI Secureboot
                                README

                             Version 1.0
************************************************************************


==========================================================================================
Enable UEFI Secureboot overall flow:

  -) Prepare keys:
      *)  Prepare PK, KEK, db keys
          a). Generate PK, KEK, db RSA keypairs and certificates
          b). Create an UEFI keys config file
          c). Generate UefiDefaultSecurityKeys.dtbo and all key's auth files for enrolling keys from target
      *)  Generate signed UEFI payloads

  -)  Enable UEFI Secureboot at flashing time:
      *)  Enable Secureboot through flash.sh during flashing
          a). Use option --uefi-keys <keys_conf> to provide signing keys and enable UEFI secure boot

  -)  Enable UEFI Secureboot at run-time from kernel:
      *)  Enable Secureboot through UEFI utility running from Ubuntu
          a). Download PK, KEK and db auth files from host;
          b). Enroll KEK, db;
          c). Download and write signed UEFI payloads;
          d). Enroll PK.

  -)  Verify UEFI Secureboot:
      *)  Check if UEFI Secureboot is enabled
      *)  Check if system can boot from kernel partition
      *)  Corrupt any bytes in any UEFI payload (or its sig file),
            - if UEFI payload corruption is in the rootfs,
              check if UEFI can detect the corruption and failover to boot from kernel partition
            - if UEFI payload corruption is in the partitions (esp, kernel, or kernel-dtb partition),
              check if UEFI can detect the corruption and boot to UEFI shell
      *)  Check additional db (db_2, if db_2 is flashed during flashing)
          a). Sign UEFI payloads with db_2 on a host
          b). Download and write the signed payloads to target
          c). Check if system can boot

========================================================================================
References:
  - https://wiki.archlinux.org/title/Unified_Extensible_Firmware_Interface/Secure_Boot#Implementing_Secure_Boot
  - https://www.rodsbooks.com/efi-bootloaders/controlling-sb.html


==========================================================================================
Prerequisite:

  *)  Make sure the following utilities are installed in your host:
      - openssl
      - device-tree-compiler
      - efitools
      - uuid-runtime

==========================================================================================
  *)  Prepare keys:
      ------------------------------------------------------------
      a). Generate PK, KEK, db RSA keypairs and certificates
          $ cd to <LDK_DIR>
          $ mkdir uefi_keys
          $ cd uefi_keys

          ### Generate PK RSA keypair and certificate
          $ openssl req -newkey rsa:2048 -nodes -keyout PK.key  -new -x509 -sha256 -days 3650 -subj "/CN=my Platform Key/" -out PK.crt

          ### Generate KEK RSA keypair and certificate
          $ openssl req -newkey rsa:2048 -nodes -keyout KEK.key  -new -x509 -sha256 -days 3650 -subj "/CN=my Key Exchange Key/" -out KEK.crt

          ### Generate db_1 RSA keypair and certificate
          $ openssl req -newkey rsa:2048 -nodes -keyout db_1.key  -new -x509 -sha256 -days 3650 -subj "/CN=my Signature Database key/" -out db_1.crt

          ### Generate db_2 RSA keypair and certificate
          $ openssl req -newkey rsa:2048 -nodes -keyout db_2.key  -new -x509 -sha256 -days 3650 -subj "/CN=my another Signature Database key/" -out db_2.crt

          ###
          ### Note: The .crt generated above are all self-signed certificates and are used for test purpose only.
          ###       For production, please follow your official certificate generation procedure.
          ###

      ------------------------------------------------------------
      b). Create an UEFI keys config file with generated keys
          $ vim uefi_keys.conf
          ### insert following lines:
          UEFI_PK_KEY_FILE="PK.key";
          UEFI_PK_CERT_FILE="PK.crt";
          UEFI_KEK_KEY_FILE="KEK.key";
          UEFI_KEK_CERT_FILE="KEK.crt";
          UEFI_DB_1_KEY_FILE="db_1.key";
          UEFI_DB_1_CERT_FILE="db_1.crt";
          UEFI_DB_2_KEY_FILE="db_2.key";
          UEFI_DB_2_CERT_FILE="db_2.crt";

          ### Note: UEFI_DB_2_XXX entries are optional

     ------------------------------------------------------------
     c). Generate UefiDefaultSecurityKeys.dtbo and all key's auth files for enrolling keys from target
         ### These steps are needed to generate:
             - UefiDefaultSecurityKeys.dtbo which is needed in flash.sh to flash UEFI default security keys to target;
             - All key's esl and auth files (they are generated in uefi_keys/_out folder).
         $ cd ..
         $ sudo tools/gen_uefi_default_keys_dts.sh uefi_keys/uefi_keys.conf
         $ sudo chmod 644 uefi_keys/_out/*.auth

  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  *)  Generate signed UEFI payloads
      ### These steps are performed automatically by flash.sh if enabling Secureboot through flashing.
      ### These steps are needed if you want to enable Secureboot at run-time from kernel.
      ### These steps are also needed if you have new UEFI payload files, or a new key to sign those payload files.
      ### UEFI payloads are:
          - extlinux.conf,
          - initrd,
          - kernel images (in rootfs, and in kernel and recovery partitions),,
          - kernel-dtb images (in rootfs, and in kernel-dtb and recovery-dtb partitions), and
          - BOOTAA64.efi, a.k.a. L4tLauncher, the OS loader.

      ### Following steps assume that you have copied the required unsigned UEFI paylaods to uefi_keys/ folder.

      ### Sign extlinux.conf using db
      ### You can replace db key with db_1 or db_2 (if UEFI_DB_2_XXX is specified in uefi_keys.conf) key in the following steps.
      ### flash.sh script uses db_1 key to generate all UEFI payloads.
      $ openssl cms -sign -signer db.crt -inkey db.key -binary -in extlinux.conf -outform der -out extlinux.conf.sig

      ### Sign initrd using db
      $ openssl cms -sign -signer db.crt -inkey db.key -binary -in initrd -outform der -out initrd.sig

      ### Sign kernel (a.k.a. Image) of rootfs using db
      $ cp Image Image.unsigned
      $ sbsign --key db.key --cert db.crt --output Image Image

      ### Sign kernel-dtb of rootfs using db
      ### The following examples use Concords' SKU 4 kernel-dtb filename.
      ### Replace it with the appropriate kernel-dtb filename of your target.
      $ openssl cms -sign -signer db.crt -inkey db.key -binary -in kernel_tegra234-p3701-0004-p3737-0000.dtb -outform der -out kernel_tegra234-p3701-0004-p3737-0000.dtb.sig

      ### Sign boot.img of kernel partition using db:
      ### Note: Before signing boot.img, the kernel (a.k.a. Image) needs to be signed
      ### If kernel (a.k.a. Image) has been signed in the above step, skip the next 2 commands.
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

      ### Sign kernel-dtb of kernel-dtb partition using db
      ### The following examples use Concords' SKU 4 kernel-dtb filename.
      ### Replace it with the appropriate kernel-dtb filename of your target.
      $ cp tegra234-p3701-0004-p3737-0000.dtb tegra234-p3701-0004-p3737-0000.dtb.unsigned
      $ openssl cms -sign -signer db.crt -inkey db.key -binary -in tegra234-p3701-0004-p3737-0000.dtb -outform der -out tegra234-p3701-0004-p3737-0000.dtb.sig
      $ truncate -s %2048 tegra234-p3701-0004-p3737-0000.dtb
      $ cat tegra234-p3701-0004-p3737-0000.dtb.sig >> tegra234-p3701-0004-p3737-0000.dtb

      ### Sign recovery.img of recovery partition using db:
      ### Note: Before signing recovery.img, the kernel (a.k.a. Image) needs to be signed
      ### If kernel (a.k.a. Image) has been signed in the above step, skip the next 2 commands.
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

      ### Sign recovery kernel-dtb of recovery-dtb partition using db
      ### The following examples use Concords' SKU 4 recovery-dtb filename.
      ### Replace it with the appropriate recovery-dtb filename of your target.
      $ cp tegra234-p3701-0004-p3737-0000.dtb.rec tegra234-p3701-0004-p3737-0000.dtb.rec.unsigned
      $ openssl cms -sign -signer db.crt -inkey db.key -binary -in tegra234-p3701-0004-p3737-0000.dtb.rec -outform der -out tegra234-p3701-0004-p3737-0000.dtb.rec.sig
      $ truncate -s %2048 tegra234-p3701-0004-p3737-0000.dtb.rec
      $ cat tegra234-p3701-0004-p3737-0000.dtb.rec.sig >> tegra234-p3701-0004-p3737-0000.dtb.rec

      ### Sign BOOTAA64.efi using db
      $ cp BOOTAA64.efi BOOTAA64.efi.unsigned
      $ sbsign --key db.key --cert db.crt --output BOOTAA64.efi BOOTAA64.efi


==========================================================================================
  *)  Enable Secureboot through flash.sh during flashing
      $ sudo ./flash.sh --uefi-keys uefi_keys/uefi_keys.conf <target> mmcblk0p1

==========================================================================================
  *)  Enable UEFI Secureboot at run-time from kernel: (AGX Xavier platform is not supported)

      Outline of steps (all steps are done in the target):
      a). Download PK.auth, KEK.auth, db_1.auth and db_2.auth;
      b). Enroll KEK, db keys;
      c). Download and write signed UEFI payloads;
      d). Enroll PK;

     ### Preparations
     $ sudo su
     $ dhclient eth0
     $ apt update
     $ apt install efitools
     $ apt install efivar

     ### Ensure that Secureboot is not enabled: i.e., the following command returns value of 00
     $ efivar -n 8be4df61-93ca-11d2-aa0d-00e098032b8c-SecureBoot

     $ mkdir /uefi_keys
     $ cd /uefi_keys

     ------------------------------------------------------------
     a). Download PK.auth, KEK.auth, db_1.auth and db_2.auth
         $ scp <host_ip>:<LDK_DIR>/<uefi_keys/_out/*.auth .

     ------------------------------------------------------------
     b). Enroll KEK, db keys: (note: PK has to be enrolled last)
         $ efi-updatevar -f /uefi_keys/db.auth db
         $ efi-updatevar -f /uefi_keys/KEK.auth KEK

     ------------------------------------------------------------
     c). Download and write signed UEFI payloads
         ### Download these signed UEFI pyloads from host to their corresponding storage:
         ### (You may want to save copies of the original files)

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

         c.1). Write signed BOOTAA64.efi to 'esp' partition
               ### Issue 'blkid' command to find which partition 'esp' is.
               ### Look for 'PARTLABEL="esp"' in the following blkid command:
               ### If there are multiple devices that have the 'esp' partition, choose the one that is the boot device.
               ### (/dev/mmcblk0p10 in the following example)
               $ blkid | grep esp
               $ mount /dev/mmcblk0p10 /mnt
               $ cd /uefi_keys
               $ cp BOOTAA64.efi /mnt/EFI/BOOT/BOOTAA64.efi
               $ sync
               $ umount /mnt

         c.2). Write signed boot.img to A_kernel partition
               $ blkid | grep kernel
               $ cd /uefi_keys
               #### (A_kernel is mounted as /dev/mmcblk0p2 in the following example)
               $ dd if=boot.img of=/dev/mmcblk0p2 bs=64k

         c.3). Write signed boot.img to B_kernel partition
               #### (B_kernel is mounted as /dev/mmcblk0p5 in the following example)
               $ dd if=boot.img of=/dev/mmcblk0p5 bs=64k

         c.4). Write signed kernel-dtb to A_kernel-dtb partition
               #### (A_kernel-dtb is mounted as /dev/mmcblk0p3 in the following example)
               $ dd if=tegra234-p3701-0004-p3737-0000.dtb of=/dev/mmcblk0p3 bs=64k

         c.5). Write signed kernel-dtb to B_kernel-dtb partition
               #### (B_kernel-dtb is mounted as /dev/mmcblk0p6 in the following example)
               $ dd if=tegra234-p3701-0004-p3737-0000.dtb of=/dev/mmcblk0p6 bs=64k

         c.6). Write signed recovery.img to recovery kernel partition
               $ blkid | grep recovery
               $ cd /uefi_keys
               #### (recovery partition is mounted as /dev/mmcblk0p8 in the following example)
               $ dd if=recovery.img of=/dev/mmcblk0p8 bs=64k

         c.7). Write signed recovery kernel-dtb to recovery-dtb partition
               #### (recovery-dtb partition is mounted as /dev/mmcblk0p9 in the following example)
               $ dd if=tegra234-p3701-0004-p3737-0000.dtb.rec of=/dev/mmcblk0p9 bs=64k

     ------------------------------------------------------------
     d). Enroll PK last:
         $ efi-updatevar -f /uefi_keys/PK.auth PK

     ------------------------------------------------------------
     e). Reboot
         $ reboot

     ------------------------------------------------------------
     f). After boot to Ubuntu prompt, ensure Secureboot is enabled:
         ### following command should return value of 01
         $ efivar -n 8be4df61-93ca-11d2-aa0d-00e098032b8c-SecureBoot


==========================================================================================
Verify UEFI Secureboot

  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  *). Verify if UEFI Secureboot is enabled
      - Verify in UEFI Menu
            - Reboot target
            - Enter UEFI Menu -> Device Manager -> Secure Boot Configuration
            - Ensure that Attempt Secure Boot is checked (with an 'X')
            - <ESC> to the top UEFI menu, and select Continue
            - Target should boot to Ubuntu prompt.

      - Verify in Ubuntu prompt
            $ sudo su
            $ dhclient eth0
            $ apt update
            $ apt install efitools
            $ apt install efivar

            ### Print the entire UEFI secure variable database
            $ efi-readvar

            ### List all UEFI variables
            $ efivar -l

            ### Check if Secureboot is enabled? (SecureBoot value should be 01)
            $ efivar -n 8be4df61-93ca-11d2-aa0d-00e098032b8c-SecureBoot

            ### Check the PK, KEK and db values using the following 'efivar -n' commands:
            $ efivar -n 8be4df61-93ca-11d2-aa0d-00e098032b8c-PK
            $ efivar -n 8be4df61-93ca-11d2-aa0d-00e098032b8c-KEK
            $ efivar -n d719b2cb-3d3a-4596-a3bc-dad00e67656f-db

  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  *). Check booting from kernel partition
      - Reboot
      - Enter UEFI Menu -> Device Manager -> NVIDIA Configuration -> L4T Configuration -> L4T Boot Mode -> Select 'Kernel Partition'
      - Save
      - Back to the top of UEFI menu, and continue
      - Make sure you see 'L4TLauncher: Attempting Kernel Boot' on the screen
      - System boots to Ubuntu
      - After above all verified, set the device default to boot from rootfs by:
        - Reboot
        - Enter UEFI Menu -> Device Manager -> NVIDIA Configuration -> L4T Configuration -> L4T Boot Mode -> Select 'Application Default'

  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  *)  Corrupt any bytes in any UEFI payload (or its sig file), and check if UEFI can detect the corruption and fail over to next boot mode.

      UEFI payloads are:
      - in the rootfs:
           - /boot/Image (Image is a signed file. Its signature is stored along with the file)
           - /boot/initrd (its sig file is /boot/initrd.sig)
           - /boot/dtb/kernel_tegra*.dtb (its sig file is /boot/dtb/kernel_tegra*.dtb.sig)
           - /boot/extlinux/extlinux.conf (its sig file is /boot/extlinux/extlinux.conf.sig)
      - in partitions:
           - boot.img in kernel partition
           - kerenl-dtb in kernel-dtb partition
           - BOOTAA64.efi in esp partition

      Use command to change any bytes in any of UEFI payloads (or their .sig files).

      (Example: the command below changes the byte at 0x10 of 'Image' to 0xa1)
      $ sudo printf '\xa1' | dd conv=notrunc of=/boot/Image bs=1 seek=$((0x10))

      ------------------------------------------------------------
      ### Corrupt an UEFI payload in the rootfs:
      ### Check if UEFI can detect the corruption and failover to boot from kernel partition
      ### Note: save a copy of the file you intend to corrupt.
      Example #1: edit extlinux.conf to add/delete anything to the file
      Example #2: corrupt the signed kernel image

      Reboot the target. UEFI should failover to boot from kernel partition.

      During failover, UEFI should print messages, something like (in the case of extlinux.conf corruption):

          L4TLauncher: Attempting Direct Boot
          OpenAndReadFileToBuffer: boot\extlinux\extlinux.conf failed signature verification: Security Violation
          ProcessExtLinuxConfig:sds Failed to Authenticate boot\extlinux\extlinux.conf (Security Violation)
          L4TLauncher: Unable to process extlinux config: Security Violation
          L4TLauncher: Attempting Kernel Boot
          EFI stub: Booting Linux Kernel...

      To recover after successfully booting from kernel partition, restore the original file, then reboot.

      ------------------------------------------------------------
      ### Corrupt an UEFI payload in a partition:
      ### Note: save a copy of the file you intend to corrupt.
      Exmaple #3: corrupt boot.img, write it to A_kernel partition (see step c.2 in the "Download and write signed UEFI payloads" section),
                  then reboot from kernel partition (see "Check booting from kernel partition" section).

      Reboot should fail and enter UEFI shell.

      To recover, reboot to UEFI menu and restore L4T Boot Mode to 'Application Default', then reboot.
      After reboot, write the original saved file to the partition.

      ### Note: corruption of BOOTAA64.efi requires a re-flash of the target.
      Example #4: corrupt BOOTAA64.efi, write it to esp partition, then reboot

      Reboot should fail and enter UEFI shell.

  ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  *)  Check additional db (db_2, if db_2 is flashed during flashing)
      a). Sign UEFI payloads with db_2 on a host
          ### Follow the "Generate signed UEFI payloads" section above to sign files with db_2 key.

          For example, on a host:
          ### Assuming unsigned extlinux.conf is copied to <LDK_DIR>/uefi_keys folder.
          $ cd <LDK_DIR>/uefi_keys
          $ openssl cms -sign -signer db_2.crt -inkey db_2.key -binary -in extlinux.conf -outform der -out extlinux.conf.sig

      b). Download and write the signed payloads to target
          ### Refer to the steps in (c) of the "Enable Secureboot through UEFI utility running from Ubuntu" section.

          For example, on the target:
          $ scp <host_ip>:<LDK_DIR>/uefi_keys/extlinux.conf* /boot/extlinux/

      c). Check if system can boot from file system
          ### Reboot. Target should be able to boot to Ubuntu


==========================================================================================
Appendix:

1. Steps to generate Keys for enrolling a KEK
   $ openssl req -newkey rsa:2048 -nodes -keyout new_KEK.key  -new -x509 -sha256 -days 3650 -subj "/CN=<any string to identify the new_KEK key/" -out new_KEK.crt
   $ cert-to-efi-sig-list -g "$(uuidgen)" new_KEK.crt new_KEK.esl
   $ sign-efi-sig-list -k PK.key -c PK.crt db new_KEK.esl new_KEK.auth

2. Steps to generate Keys for enrolling a db (or a dbx):
   $ openssl req -newkey rsa:2048 -nodes -keyout new_db.key  -new -x509 -sha256 -days 3650 -subj "/CN=<any string to identify the new_db key/" -out new_db.crt
   $ cert-to-efi-sig-list -g "$(uuidgen)" new_db.crt new_db.esl
   $ sign-efi-sig-list -k KEK.key -c KEK.crt db new_db.esl new_db.auth

