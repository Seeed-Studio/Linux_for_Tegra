# SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
# property and proprietary rights in and to this material, related
# documentation and any modifications thereto. Any use, reproduction,
# disclosure or distribution of this material and related documentation
# without an express license agreement from NVIDIA CORPORATION or
# its affiliates is strictly prohibited.

The IGX devkits do not enable the UEFI secure boot by default. To enable the UEFI Secureboot:

  1. Prepare the keys.
     a. Prepare the PK, the Microsoft KEK, and the Microsoft DB esl files.
        i. Generate the PK RSA keypairs and certificates.
        ii. Download the Microsoft KEK and DB esl files.
     b. Prepare the optional KEK and DB RSA keypairs and esl files.
        i. Generate the other KEK and DB RSA keypairs and esl files.
        ii. Append the esl files of the KEK and the DB.

  2. Enable the UEFI Secureboot with a Capsule update.
     a. Create a UEFI keys config file.
     b. To enroll the keys from target, generate the UefiDefaultSecurityKeys.dtbo file.
     c. Generate Capsule payload with the UefiDefaultSecurityKeys.dtbo.
     d. Trigger a Capsule update.
     e. Check the enrolled UEFI SecureBoot keys.

  3. Enable UEFI Secureboot at runtime from the kernel with the UEFI utility that is running from Ubuntu.
     a. Enroll the PK, the Microsoft KEK, the Microsoft DB, and other optional KEK and DB.
        i. Generate the PK.auth file.
        ii. Download the PK.auth and the KEK and DB esl files from the host.
        iii. Before enrollment, check the UEFI Secureboot status.
        iv. Enroll the DB.
        v. Enroll the KEK.
        vi. Enroll the PK.
        vii. Check the UEFI Secureboot status.
        viii. Reboot the target.
        ix. Check the UEFI Secureboot status after enrollment.

  4. Update the KEK/DB/DBX keys with a Capsule update.
     a. Prepare the update keys.
        i. Generate the KEK, the DB, and the DBX keys auth file for the update.
        ii. Create a UEFI update keys config file with the generated keys auth file.
        iii. Generate the UefiUpdateSecurityKeys.dtbo file.
     b. Generate the Capsule payload with UEFI secureboot enabled.
     c. Trigger a Capsule update.
     d. Check the updated UEFI SecureBoot keys.

  5. Disable UEFI Secureboot.
     a. Disable shim level Secureboot.
     b. Disable UEFI Secureboot.

Note: The utilities and parameters are used to generate keys, and the self-signed certificates in following sample commands are
      used only for demonstration and test purposes. For production, follow your official certificate generation procedure.


References:
  - https://wiki.archlinux.org/title/Unified_Extensible_Firmware_Interface/Secure_Boot#Implementing_Secure_Boot
  - https://www.rodsbooks.com/efi-bootloaders/controlling-sb.html
  - https://wiki.archlinux.org/title/Unified_Extensible_Firmware_Interface/Secure_Boot#Microsoft_Windows


Prerequisites:

   Before you begin, ensure that the following utilities are installed in your host:
   - openssl
   - device-tree-compiler
   - efitools
   - uuid-runtime


1. Prepare the keys:

   Note: Step b is optional.

   a. Prepare the PK, the Microsoft KEK, and the Microsoft DB esl files.

      i. Generate the PK RSA keypairs and certificates.
         $ cd to <LDK_DIR>
         $ mkdir uefi_keys
         $ cd uefi_keys
         $ GUID=$(uuidgen)
         $ openssl req -newkey rsa:2048 -nodes -keyout PK.key  -new -x509 -sha256 -days 3650 -subj "/CN=my Platform Key/" -out PK.crt
         $ cert-to-efi-sig-list -g "${GUID}" "PK.crt" PK.esl

         Note: The generated .crt files are self-signed certificates and are used for demonstration purposes only. For production,
               follow your official certificate generation procedure.

      ii. Download the Microsoft KEK and DB esl files. Refer to https://wiki.archlinux.org/title/Unified_Extensible_Firmware_Interface/Secure_Boot#Microsoft_Windows
          for more information.
          (1) To download the Microsoft Corporation KEK CA 2011 certificate, go to https://www.microsoft.com/pkiops/certs/MicCorKEKCA2011_2011-06-24.crt.

          (2) Create an EFI Signature List from Microsoft's DER format KEK certificate using Microsoft's GUID (77fa9abd-0359-4d32-bd60-28f4e78f784b):
              $ sbsiglist --owner 77fa9abd-0359-4d32-bd60-28f4e78f784b --type x509 --output MS_Win_KEK.esl MicCorKEKCA2011_2011-06-24.crt

          (3) To download the Microsoft Corporation UEFI CA 2011 certificate, go to https://www.microsoft.com/pkiops/certs/MicCorUEFCA2011_2011-06-27.crt.

          (4) Create EFI Signature Lists from Microsoft's DER format DB certificates using Microsoft's GUID (77fa9abd-0359-4d32-bd60-28f4e78f784b):
              $ sbsiglist --owner 77fa9abd-0359-4d32-bd60-28f4e78f784b --type x509 --output MS_UEFI_db.esl MicCorUEFCA2011_2011-06-27.crt

   b. Prepare the optional KEK and DB RSA keypairs and esl files.
      i. Generate the other KEK and DB RSA keypairs and esl files.
         $ openssl req -newkey rsa:2048 -nodes -keyout KEK.key -new -x509 -sha256 -days 3650 -subj "/CN=my Key Exchange Key/" -out KEK.crt
         $ cert-to-efi-sig-list -g "${GUID}" KEK.crt KEK.esl

         $ openssl req -newkey rsa:2048 -nodes -keyout db_1.key  -new -x509 -sha256 -days 3650 -subj "/CN=my Signature Database key/" -out db_1.crt
         $ cert-to-efi-sig-list -g "${GUID}" db_1.crt db_1.esl

      ii. Append the esl files of the KEK and the DB.
          $ cat KEK.esl MS_Win_KEK.esl > combined_KEK.esl
          $ cat db_1.esl MS_UEFI_db.esl > combined_db.esl


2. Enable the UEFI Secureboot with a Capsule update.

   a. Create a UEFI keys config file.
      (1) Go to work directory and create a config file:
          $ cd <LDK_DIR>/uefi_keys
          $ vim uefi_keys.conf

      (2) Insert following lines:
          UEFI_DEFAULT_PK_ESL="PK.esl"
          UEFI_DEFAULT_KEK_ESL_0="MS_Win_KEK.esl"
          UEFI_DEFAULT_DB_ESL_0="MS_UEFI_db.esl"

      (3) (Optionally) Add the other KEK and DB:
          UEFI_DEFAULT_KEK_ESL_1="KEK.esl"
          UEFI_DEFAULT_DB_ESL_1="db_1.esl"

   b. To enroll the keys from target, generate the UefiDefaultSecurityKeys.dtbo file.
      $ cd ..
      $ sudo tools/gen_uefi_keys_dts.sh --no-signing-key uefi_keys/uefi_keys.conf
      $ cp uefi_keys/UefiDefaultSecurityKeys.dtbo bootloader/

   c. Generate the Capsule payload with the UefiDefaultSecurityKeys.dtbo file.
      $ cd to <LDK_DIR>
      $ sudo ADDITIONAL_DTB_OVERLAY="UefiDefaultSecurityKeys.dtbo" ./l4t_generate_soc_bup.sh -e t23x_igx_bl_spec t23x
      $ ./generate_capsule/l4t_generate_soc_capsule.sh -i bootloader/payloads_t23x/bl_only_payload -o ./TEGRA_IGX.Cap t234

      Note: Refer to https://docs.nvidia.com/jetson/archives/r35.4.1/DeveloperGuide/text/SD/Bootloader/UpdateAndRedundancy.html#generating-a-multi-spec-capsule-payload
            for more information about generating a Capsule paylod.

   d. Trigger a Capsule update.
      i. Download a Capsule payload to the target device.
         $ cd /opt
         $ sudo scp <host_ip>:<LDK_DIR>/TEGRA_IGX.Cap .

      ii. Call nv_bootloader_capsule_updater.sh to prepare the Capsule update.
          $ sudo nv_bootloader_capsule_updater.sh -q /opt/TEGRA_IGX.Cap

      iii. Reboot the Jetson device. UEFI will use the Capsule payload to update the non-current slot Bootloader and to
           boot from the newly updated slot, reboot again.

   e. Check the enrolled UEFI SecureBoot keys.

      i. Check the UEFI Secureboot status.
         $ mokutil --sb-state

         Note: The output should be "SecureBoot enabled".

      ii. Check the enrolled PK.
          $ mokutil --pk

          Note: The PK.crt file is in the output key list.

      iii. Check the enrolled Microsoft KEK.
           $ mokutil --kek

           Note: The Microsoft KEK is in the output key list.

      iv. Check the enrolled Microsoft DB.
          $ mokutil --db

          Note: The Microsoft DB is in the output key list.


3. Enable UEFI Secureboot at runtime from the kernel with the UEFI utility that is running from Ubuntu.
   a. Enroll the PK, the Microsoft KEK, the Microsoft DB, and other optional KEK and DB.
      i. Generate the PK.auth file.
         $ cd <LDK_DIR>/uefi_keys
         $ sign-efi-sig-list -k PK.key -c PK.crt PK PK.esl PK.auth


      ii. Download the PK.auth, the KEK, and the DB esl files from the host.
          $ cd /opt
          $ sudo mkdir uefi_keys
          $ sudo scp <host_ip>:<LDK_DIR>/uefi_keys/PK.auth ./uefi_keys/
          $ sudo scp <host_ip>:<LDK_DIR>/uefi_keys/*.esl ./uefi_keys/

      iii. Before enrollment, check the UEFI Secureboot status.
           (1) Check the current UEFI Secureboot status.
               $ mokutil --sb-state

               Note: The Secureboot status is "SecureBoot disabled\n Platform is in Setup Mode".

           (2) Check the current UEFI Secureboot keys status.
               $ sudo efi-readvar

               Note: The output of the command is as following:
                     Variable PK has no entries
                     Variable KEK has no entries
                     Variable db has no entries
                     Variable dbx has no entries
                     Variable MokList has no entries

      iv. Enroll the DB.
          • If the optional DB is not needed, only enroll the Microsoft DB:
            $ sudo efi-updatevar -e -f /opt/uefi_keys/MS_UEFI_db.esl db

          • If the optional DB is needed, enroll the Microsoft DB and the other DB:
            $ sudo efi-updatevar -e -f /opt/uefi_keys/combined_db.esl db

      v. Enroll the KEK.
          • If the optional KEK is not needed, only enroll the Microsoft KEK:
            $ sudo efi-updatevar -e -f /opt/uefi_keys/MS_Win_KEK.esl KEK

          • If the optional KEK is needed, enroll the Microsoft KEK and the other KEK:
            $ sudo efi-updatevar -e -f /opt/uefi_keys/combined_KEK.esl KEK

      vi. Enroll the PK.
          $ sudo efi-updatevar -f /opt/uefi_keys/PK.auth PK

      vii. Check the UEFI Secureboot status.
           $ mokutil --sb-state

           Note: The Secureboot status is "SecureBoot disabled".

      viii. Reboot the target.
            $ sudo reboot

            Note: If current env is dev-kit, make sure to use the optional DB to sign L4TLauncher, the kernel,
                  and kernel-dtb before triggering reboot(Refer to developer guide for more information:
                  https://docs.nvidia.com/jetson/archives/r35.4.1/DeveloperGuide/text/SD/Security/SecureBoot.html#generate-signed-uefi-payloads).
                  Otherwise, device will not be able to boot up.

      ix. Check the UEFI Secureboot status after enrollment.
          (1) Check the current UEFI Secureboot status.
              $ mokutil --sb-state

              Note: The Secureboot status is "SecureBoot enabled".

          (2) Check the current UEFI Secureboot keys status.
              $ sudo efi-readvar

              Note: Here is some additional information:
                    • The PK, KEK and DB keys are in the output list.
                    • You can also use the commands in "2. Enable the UEFI Secureboot by Capsule update.", section
                      "e. Check the enrolled UEFI SecureBoot keys." to check the enrolled keys.


4. Update the KEK/DB/DBX keys with a Capsule update.

   a. Prepare the update keys.

      i. Generate the KEK, the DB, and the DBX key auth files for the update.
         $ cd <LDK_DIR>/uefi_keys
         $ GUID=$(uuidgen)

         (1) Generate the KEK RSA keypair and certificate for the update.
             $ openssl req -newkey rsa:2048 -nodes -keyout update_kek_0.key -new -x509 -sha256 -days 3650 -subj "/CN=Update KEK 0/" -out update_kek_0.crt
             $ cert-to-efi-sig-list -g "${GUID}" update_kek_0.crt update_kek_0.esl
             $ sign-efi-sig-list -a -k PK.key -c PK.crt KEK update_kek_0.esl update_kek_0.auth

             Note: Here is some important information:
                   - This step is needed only when a KEK update is required.
                   - The PK.key and PK.crt are the PK private key and PK certificate that were generated when you enrolled the default keys
                     in step "Prepare the PK, the Microsoft KEK and the Microsoft DB esl files." in "1. Prepare the keys".

         (2) Generate the DB RSA keypair and certificate for the update.
             $ openssl req -newkey rsa:2048 -nodes -keyout update_db_0.key -new -x509 -sha256 -days 3650 -subj "/CN=update DB 0/" -out update_db_0.crt
             $ cert-to-efi-sig-list -g "${GUID}" update_db_0.crt update_db_0.esl
             $ sign-efi-sig-list -a -k update_kek_0.key -c update_kek_0.crt db update_db_0.esl update_db_0.auth

             Note: The signing private key (update_kek_0.key) and the certificate (update_kek_0.crt) are generated by running the previous command.
                   They can also be the KEK private key and certificate when you enroll the default keys in step "b. Prepare the optional other KEK
                   and DB RSA keypairs and esl files." in "1. Prepare the keys.".

         (3) Generate another DB RSA keypair and certificate for the update.
             $ openssl req -newkey rsa:2048 -nodes -keyout update_db_1.key -new -x509 -sha256 -days 3650 -subj "/CN=update DB 1/" -out update_db_1.crt
             $ cert-to-efi-sig-list -g "${GUID}" update_db_1.crt update_db_1.esl
             $ sign-efi-sig-list -a -k update_kek_0.key -c update_kek_0.crt db update_db_1.esl update_db_1.auth

             Note: The signing private key (update_kek_0.key) and the certificate (update_kek_0.crt) are generated by running the previous command.
                   They can also be the KEK private key and certificate when you enroll the default keys in step "b. Prepare the optional other KEK
                   and DB RSA keypairs and esl files." in "1. Prepare the keys.".

         (4) Generate db_1 auth for the DBX update.
             $ sign-efi-sig-list -a -k update_kek_0.key -c update_kek_0.crt dbx update_db_1.esl update_dbx_1.auth

      ii. Create a UEFI update keys config file with the generated keys auth file.
          This update keys config file example includes all three key types, and users can update only one or two key types.

          $ vim uefi_update_keys.conf

          Insert following lines:
          UEFI_UPDATE_PRE_SIGNED_KEK_0="update_kek_0.auth"

          UEFI_UPDATE_PRE_SIGNED_DB_0="update_db_0.auth"
          UEFI_UPDATE_PRE_SIGNED_DB_1="update_db_1.auth"

          UEFI_UPDATE_PRE_SIGNED_DBX_0="update_dbx_1.auth"

          Note: Users can specify up to 50 UEFI_UPDATE_PRE_SIGNED_KEK_n, UEFI_UPDATE_PRE_SIGNED_DBX_n, or UEFI_UPDATE_PRE_SIGNED_DB_n.

      iii. Generate the UefiUpdateSecurityKeys.dtbo file.
           $ cd ..
           $ sudo tools/gen_uefi_keys_dts.sh --no-signing-key uefi_keys/uefi_update_keys.conf
           $ cp uefi_keys/UefiUpdateSecurityKeys.dtbo bootloader/

   b. Generate the Capsule payload with UEFI secureboot enabled.
      $ cd to <LDK_DIR>
      $ sudo ADDITIONAL_DTB_OVERLAY="UefiUpdateSecurityKeys.dtbo" ./l4t_generate_soc_bup.sh -e t23x_igx_bl_spec t23x
      $ ./generate_capsule/l4t_generate_soc_capsule.sh -i bootloader/payloads_t23x/bl_only_payload -o ./TEGRA_IGX.Cap t234

   c. Trigger a Capsule update.
      i. Download a Capsule payload to the target device.
         $ cd /opt
         $ sudo scp <host_ip>:<LDK_DIR>/TEGRA_IGX.Cap .

      ii. Call nv_bootloader_capsule_updater.sh to prepare the Capsule update.
          $ sudo nv_bootloader_capsule_updater.sh -q /opt/TEGRA_IGX.Cap

      iii. Reboot the Jetson device. UEFI will use the Capsule payload to update the non-current slot Bootloader, and then reboot again
           to boot from the newly updated slot.

   d. Check the updated UEFI SecureBoot keys.
      Refer to the commands in section "e. Check the enrolled UEFI SecureBoot keys." of chapter "2. Enable the UEFI Secureboot by Capsule update."
      to check the updated keys.


5. Disable UEFI Secureboot.

   a. Disable shim level Secureboot.
      i. Check current UEFI Secureboot status.
         $ mokutil --sb-state

         Note: Output "SecureBoot enabled".

      ii. To disable shim level Secureboot, run the following command.
          $ sudo mokutil --disable-validation

          Note: At the "password length: 8~16\n input password:" prompt, enter a password. Remeber the password,
                because it will be used by the MOKManager at the next boot.

      iii. Reboot the target.
           $ sudo reboot

      iv. After you boot to MOKManager, at the "Press any key to perform MOK management" prompt, press Enter, select
          the option "Change Secure Boot state" option, and press Enter again.

      v. Enter the password that you set in step ii.

      vi. To confirm that you want to disable Secure Boot, select "[YES]".

      vii. To reboot and boot to the kernel, select "Reboot".

      viii. Check the current UEFI and shim level Secureboot status.
            $ mokutil --sb-state

            Note: Here is the output:
                  SecureBoot enabled
                  SecureBoot validation is disabled in shim

      Note: To enable the shim level Secureboot again, run the "--enable-validation" command.

   b. Disable UEFI Secureboot.
      To disable UEFI Secureboot, you must own the platform key.

      i. Prepare the noPK.auth file.
         $ cd <LDK_DIR>/uefi_keys
         $ touch noPK.esl
         $ sign-efi-sig-list -k PK.key -c PK.crt PK noPK.esl noPK.auth

      ii. Download the noPK.auth file from the host.
          $ cd /opt
          $ sudo mkdir -p uefi_keys
          $ sudo scp <host_ip>:<LDK_DIR>/uefi_keys/noPK.auth ./uefi_keys/

      iii. Enroll the noPK.auth file.
           $ sudo chattr -i /sys/firmware/efi/efivars/PK-8be4df61-93ca-11d2-aa0d-00e098032b8c
           $ sudo efi-updatevar -f /opt/uefi_keys/noPK.auth PK

      iv. Reboot the target.
          $ sudo reboot

      v. Check the UEFI Secureboot status.
         $ mokutil --sb-state

         Note: Here is the output:
               SecureBoot disabled
               Platform is in Setup Mode
