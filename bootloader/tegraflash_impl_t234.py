#
# Copyright (c) 2014-2023, NVIDIA Corporation.  All Rights Reserved.
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA Corporation is strictly prohibited.
#

from __future__ import print_function

import binascii
import math
import os.path
import re
import shutil
import fnmatch
import struct
import subprocess
import sys
import tempfile
import time
from xml.etree import ElementTree

from tegraflash_internal import (run_command, info_print, tegraflash_abs_path,
        tegraflash_symlink, tegraflash_os_path, tegraflash_generate_index_file,
        concat_file, getPart_name_by_type, tegraflash_concat_overlay_dtb,
        concat_file_4k, tegraflash_create_backup_file, set_partition_filename,
        get_partition_filename, get_all_partitions, tegraflash_add_odm_data_to_dtb, strip_string_list,
        parse_indexfile_for_qspi, compareGPT, parse_dev_params_multi_chain,
        copy_br_bct_multi_chain, generate_bct_backup_image)
from tegraflash_internal import cmd_environ, paths, start_time, ramcode, values, tegrabct_multi_chain, tegrabct_backup
from tegraflash_internal import tegraflash_exception

from tegrasign_v3 import (compute_sha, tegrasign, set_env, hex_to_str, str_to_hex)

sys.path.insert(0, os.path.join(os.path.dirname(os.path.realpath(__file__)), "pyfdt"))
import pyfdt

""" The following code section is only for t23x chips and newer
    Implementation of Tegraflash Script using OOP design pattern
"""
class TFlashT23x_Base(object):
    """ Base Class for Tegraflash functions specific to t23x and newer.

    """

    tegrarcm_values = {
        '--board_info': 'board_info.bin',
        '--chip_info': 'chip_info.bin',
        '--fuse_info': 'blow_fuse_data.bin',
        '--get_fuse_names': 'read_fuse_names.txt',
        '--list': 'rcm_list.xml',
        '--read_fuse': 'read_fuse.bin',
        '--rollback_data': 'rollback_data.bin',
        '--signed_list': 'rcm_list_signed.xml',
        '--storage_info': 'storage_info.bin',
    }
    tegrabct_values = {
        '--bct': None,
        '--list': 'bct_list.xml',
        '--mb1_bct': None,
        '--mb1_cold_boot_bct': None,
        '--membct_cold_boot': None,
        '--membct_rcm': None,
        '--rcm_bct': None,
        '--signed_list': 'bct_list_signed.xml',
        '--updated': False,
    }
    tegrasign_values = {
        '--getmontgomeryvalues': 'montgomery.bin',
        '--mode': 'zerosbk',
        '--pubkeyhash': 'pub_key.key',
    }
    tegraparser_values = {
        '--pt': None,
        '--ufs_otp': 'ufs_otp_data.bin',
    }
    tegrahost_values = {
        '--list': 'images_list.xml',
        '--ratchet_blob': 'ratchet_blob.bin',
        '--signed_list': 'images_list_signed.xml',
    }
    tegraflash_binaries_v2 = {
        'tegrabct': 'tegrabct_v2',
        'tegradevflash': 'tegradevflash_v2',
        'tegrahost': 'tegrahost_v2',
        'tegraparser': 'tegraparser_v2',
        'tegrarcm': 'tegrarcm_v2',
        'tegrasign': 'tegrasign_v3.py',
    }
    tegraflash_gpt_image_name_map = {
        'nvme_0_master_boot_record': 'mbr_12_0.bin',
        'nvme_0_primary_gpt': 'gpt_primary_12_0.bin',
        'nvme_0_secondary_gpt': 'gpt_secondary_12_0.bin',
        'sdcard_0_master_boot_record': 'mbr_6_0.bin',
        'sdcard_0_primary_gpt': 'gpt_primary_6_0.bin',
        'sdcard_0_secondary_gpt': 'gpt_secondary_6_0.bin',
        'sdmmc_boot_3_secondary_gpt': 'gpt_secondary_0_3.bin',
        'sdmmc_boot_3_secondary_gpt_backup': 'gpt_secondary_0_3.bin',
        'sdmmc_user_3_master_boot_record': 'mbr_1_3.bin',
        'sdmmc_user_3_primary_gpt': 'gpt_primary_1_3.bin',
        'sdmmc_user_3_secondary_gpt': 'gpt_secondary_1_3.bin',
        'spi_0_secondary_gpt': 'gpt_secondary_3_0.bin',
        'spi_0_secondary_gpt_backup': 'gpt_secondary_3_0.bin',
        'ufs_0_secondary_gpt': 'gpt_secondary_7_0.bin',
        'ufs_0_secondary_gpt_backup': 'gpt_secondary_7_0.bin',
        'ufs_user_0_master_boot_record': 'mbr_8_0.bin',
        'ufs_user_0_primary_gpt': 'gpt_primary_8_0.bin',
        'ufs_user_0_secondary_gpt': 'gpt_secondary_8_0.bin',
        'external_0_master_boot_record': 'mbr_9_0.bin',
        'external_0_primary_gpt': 'gpt_primary_9_0.bin',
        'external_0_secondary_gpt': 'gpt_secondary_9_0.bin',
    }

    def __init__(self):

        # Data used below is referred from tegrabl_sigheader.h
        self.GSHV = '4e564441'
        self.header_magic_fmt = '>I'
        self.header_size = 400
        self.is_rcmboot = False

        # SHA digest offsets
        self.args_offset = ''
        self.args_length = ''

        # Fixed BCH offsets
        self.bch_length = None
        self.bch_offset = ''

    def _is_header_present(self, file_path):
        file_size = os.path.getsize(file_path)
        # File size less than header size (400) means header is not present
        if file_size < self.header_size:
            info_print('%s size is less than header size %d \n'
                       % (file_path, self.header_size))
            return False
        header_magic_size = struct.calcsize(self.header_magic_fmt)
        with open(file_path, 'rb') as f:
            header_magic = struct.unpack(
                self.header_magic_fmt, f.read(header_magic_size))[0]
            f.seek(0, 0)
        # Convert decimal to hex
        header_magic = format(header_magic, 'x')
        info_print('header_magic: %s' % header_magic)
        if (header_magic != self.GSHV):
            return False
        return True

    def tegraflash_get_magicid(self, partition_type):
        info_print("Get magic id")
        command = self.exec_file('tegraparser')
        command.extend(['--get_magic', partition_type])
        magic_id = run_command(command)
        magic_id = magic_id.strip()
        info_print('partition type ' + partition_type + ', magic id = ' + magic_id)
        return magic_id

    """ Tegraflash commands and Tools  """

    def tegraflash_ufs_otp(self, args, otp_args):
        values.update(args)
        filename = os.path.basename(otp_args[0])
        if not os.path.exists(filename):
            raise tegraflash_exception('Could not find ' + otp_args[0])
        filename = os.path.splitext(otp_args[0])
        if filename[1] != '.xml':
            raise tegraflash_exception(otp_args[0] + ' is not an xml file')

        if values['--securedev']:
            raise tegraflash_exception('Error: ufs_otp with --securedev not support yet')

        compulsory_args = ['--bl', '--sdram_config']
        for required_arg in compulsory_args:
            if args[required_arg] is None:
                args[required_arg] = input('Input ' + required_arg + ': ')

        self.tegraflash_get_key_mode()
        self.tegraflash_parse_partitionlayout()
        if values['--encrypt_key'] is None:
            self.tegraflash_sign_images()
        else:
            self.tegraflash_enc_and_sign_images()
        self.tegraflash_generate_bct()
        self.tegraflash_update_images()
        self.tegraflash_send_to_bootrom()
        # sign images, and not for coldboot
        self.tegraflash_send_to_bootloader(True, False)

        info_print('Starting configure UFS')
        command = self.exec_file('tegradevflash')
        if otp_args[0] == 'dummy':
            command.extend(['--oem', 'ufsotp', otp_args[0] ])
        else:
            info_print('Parsing UFS configuration data as per xml file')
            command = self.exec_file('tegraparser')
            command.extend(['--ufs_otp', otp_args[0], self.tegraparser_values['--ufs_otp']])
            run_command(command)

            command = self.exec_file('tegradevflash')
            command.extend(['--oem', 'ufsotp'])
            command.extend([self.tegraparser_values['--ufs_otp']])

        run_command(command)

    def tegraflash_nvsign(self, exports, in_file, magic, only_sign):
        values.update(exports)

        filename = os.path.basename(in_file)
        info_print(filename)
        out_file = os.path.splitext(
            filename)[0] + '_dev' + os.path.splitext(filename)[1]
        aligned_file = os.path.splitext(
            filename)[0] + '_aligned' + os.path.splitext(filename)[1]
        if os.path.exists(in_file):
            shutil.copyfile(in_file, aligned_file)
        mode = self.tegrasign_values['--mode']
        command = self.exec_file('tegrahost')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--align', aligned_file])
        run_command(command)

        filename = aligned_file

        if not self._is_header_present(aligned_file):
            # if to do encryption
            if bool(only_sign) == False:
                # Get a copy of the binary, used for aes-gcm op later
                with open(filename, 'rb') as f:
                    src = bytearray(f.read())
                enc_file = os.path.splitext(
                    filename)[0] + '_encrypt' + os.path.splitext(filename)[1]
                shutil.copyfile(filename, enc_file)
                filename = os.path.splitext(
                    filename)[0] + '_encrypt' + os.path.splitext(filename)[1]

            mode = 'nvidia-rsa'
            command = self.exec_file('tegrahost')
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            command.extend(
                ['--ratchet', values['--nv_nvratchet'], values['--nv_oemratchet']])
            command.extend(['--magicid', magic])
            command.extend(['--addmb1nvheader', filename, mode])
            if values['--ecid'] is not None:
                command.extend(['--ecid', values['--ecid']])
            run_command(command)
            filename = os.path.splitext(
                filename)[0] + '_sigheader' + os.path.splitext(filename)[1]

            # if to do encryption
            if bool(only_sign) == False:
                # Need 1) iv1 == stage1_components[0].enc_params.u8_iv 2) aad = stage1_components[0] before gcm
                with open(filename, 'rb') as f:
                    src_and_bch = bytearray(f.read())

                fileNm, fileExt = os.path.splitext(filename)
                enc_file = fileNm + '_encrypt'  + fileExt
                tag_file = fileNm + '.tag'

                # Retrieve iv that will be used for aes encryption.
                iv1_offset = 7956 # = stage1_components[0].enc_params.u8_iv
                iv1_size = 12
                iv1 = src_and_bch[iv1_offset:iv1_offset+iv1_size]
                # Retrieve aad data that is used for AES-GCM.
                # = boot_component_header_t.stage1_components[0]
                aad1_offset = 7904
                aad1_size = 64
                aad1 = src_and_bch[aad1_offset:aad1_offset+aad1_size]

                payload_size = len(src)
                payload_offset = len(src_and_bch) - payload_size

                # Retrieve derivation & version that is used for key wrapping
                der_str_offset = 7936
                der_str_size = 16
                der_str = src_and_bch[der_str_offset:der_str_offset+der_str_size]
                ver_offset = 7920
                ver_size = 4
                ver = src_and_bch[ver_offset:ver_offset+ver_size]
                sha_offset = 7984
                sha_size = 64
                # = boot_component_header_t.stage1_components[0].enc_params.u8_auth_tag
                tag1_offset = 7968
                tag1_size = 16
                tag1 = src_and_bch[tag1_offset:tag1_offset+tag1_size]
                # These 2 will be reverted when psc_bl1 and psc_fw binaries are passed in
                # for bch's u8_stage1_res parsing. Currently we can use 0's b/c these
                # values are 0's in the bch until they are officially stage1 signed
                psc_bl = bytearray(8) #TODO
                psc_fw = bytearray(8) #TODO
                chip_info = values['--chip'] + values['--chip_major']
                lines = 'IV : "' + hex_to_str(iv1) + '"\n'
                lines += 'AAD : "'+ hex_to_str(aad1) + '"\n'
                lines += 'DERSTR : "' + hex_to_str(der_str) + '"\n'
                lines += 'VER : "' + hex_to_str(ver) + '"\n'
                lines += 'FLAG : "DEV"\n'
                lines += 'CHIPID : "%s"\n' %(chip_info)
                lines += 'MAGICID: "' + magic + '"\n'
                lines += 'BL_DERSTR : "' + hex_to_str(psc_bl) + '"\n'
                lines += 'FW_DERSTR : "' + hex_to_str(psc_fw) + '"\n'

                kdf_yaml = 'kdf_args_%s.yaml' %(fileNm)
                with open(kdf_yaml, 'w') as f:
                    f.write(lines)

                self.call_tegrasign(filename, None, None, None, str(payload_size), None, str(payload_offset), None, None, None, False, 0, 0, 0, None, 0, ['kdf_file=' + kdf_yaml])

                enc_file_sha = compute_sha('sha512', enc_file, payload_offset, payload_size)
                # Write the binary digest back to bch
                if (os.path.exists(enc_file_sha)):
                    with open(enc_file, 'rb') as fe, open(enc_file_sha, 'rb') as fs, open(tag_file, 'rb') as ft:
                        enc_buff = bytearray(fe.read())
                        sha = bytearray(fs.read())
                        tag_buff = bytearray(ft.read())
                        enc_buff[sha_offset:sha_offset + sha_size] = sha[:]
                        enc_buff[tag1_offset:tag1_offset+tag1_size] = tag_buff[:]

                        with open(filename, 'wb') as f:
                            f.write(enc_buff)

        self.call_tegrasign(
            filename, None, None, values['--nv_key'], self.args_length, None, self.args_offset, None, 'sha512', None)

        signed_file = os.path.splitext(filename)[0] + '.sig'
        sig_type = "nvidia-rsa"
        command = self.exec_file('tegrahost')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--updatesigheader', filename, signed_file, sig_type])
        run_command(command)

        shutil.copyfile(filename, out_file)
        info_print('******* nvsign generated file name: %s  ********' %(out_file))
        info_print('******* Please make sure this is updated in partition layout *******')
        return out_file

    def tegraflash_preprocess_configs(self):
        """ Preprocess BCT configuration files using cpp and dtc tools
            if they are in DTS format
        """

        # Gather all the configs in a list
        configs_list = ['--br_cmd_config', '--device_config',
                        '--deviceprod_config', '--gpioint_config',
                        '--misc_cold_boot_config', '--misc_config',
                        '--pinmux_config', '--pmc_config', '--pmic_config',
                        '--prod_config', '--scr_cold_boot_config',
                        '--scr_config', '--sdram_config', '--uphy_config',
                        '--wb0sdram_config', '--minratchet_config']

        for config in configs_list:
            if values[config] is not None:
                config_types = [".dts"]
                if any(cf_type in values[config] for cf_type in config_types):
                    info_print(
                        'Pre-processing config: ' + values[config])
                    values[config] = self.run_cpp_tool(
                        values[config])
                    values[config] = self.run_dtc_tool(
                        values[config])

    def tegraflash_mkdevimages(self, args, cmd_args):
        values.update(args)

        if values['--cfg'] is None:
            raise tegraflash_exception(
                'Error: Partition configuration is not specified')

        if values['--chip'] is None:
            raise tegraflash_exception(
                'Error: chip is not specified')

        self.tegraflash_get_key_mode()
        self.tegraflash_parse_partitionlayout()

        # Set bct flag to True if bct generation is required
        # BCT flag needs to be passed to tegraflash_sign_images
        # function because it generated BR-BCT before signing
        # MB1 image
        bct_flag = False if "nobct" in cmd_args else True
        if values['--encrypt_key'] is None:
            self.tegraflash_sign_images(bct_flag=bct_flag)
        else:
            self.tegraflash_enc_and_sign_images(bct_flag=bct_flag)

        # if nobct is specified in the command argument
        # skip bct generation
        if (bct_flag):
            self.tegraflash_generate_bct()
        else:
            # Here nobct needs to be removed from the cmd_args
            # since tegradevflash doesn't require it
            cmd_args.remove("nobct")

        self.tegraflash_update_images()
        self.tegraflash_generate_devimages(cmd_args)
        info_print('Storage images generated\n')

    def tegraflash_get_key_mode(self):
        self.call_tegrasign(None, 'mode.txt', None,
                            values['--key'], None, None, None, None, None, None)
        with open('mode.txt') as mode_file:
            self.tegrasign_values['--mode'] = mode_file.read()

    def tegraflash_fetch_chip_info(self):
        info_print('Retrieving board information')
        command = self.exec_file('tegrarcm')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--oem', 'platformdetails', 'chip', self.tegrarcm_values['--chip_info']])
        try:
            run_command(command)
        except tegraflash_exception as e:
            command[0] = self.exec_file('tegradevflash')[0]
            run_command(command)
        if os.path.exists(self.tegrarcm_values['--chip_info']):
            out_file = tegraflash_abs_path(self.tegrarcm_values['--chip_info'] + '_bak')
            shutil.copyfile(self.tegrarcm_values['--chip_info'], out_file)

    def tegraflash_parse(self, args, parse_args):
        values.update(args)

        if parse_args[0] == 'fusebypass':
            self.tegraflash_parse_fuse_bypass(parse_args[1:])
            if values['--skipuid'] == False:
                args['--skipuid'] = False
        else:
            raise tegraflash_exception(parse_args[0] + " is not supported")

    def tegraflash_parse_fuse_bypass(self, fb_args):
        if len(fb_args) < 2:
            raise tegraflash_exception("Invalid arguments")

        filename = os.path.basename(fb_args[0])
        if not os.path.isfile(paths['TMP'] + '/' + filename):
            tegraflash_symlink(tegraflash_abs_path(fb_args[0]), paths['TMP'] + '/' + filename)
            fb_args[0] = filename

        command = self.exec_file('tegraparser')
        command.extend(['--fuseconfig', fb_args[0]])
        command.extend(['--sku', fb_args[1]])

        if len(fb_args) == 3:
            if fb_args[2] != 'forcebypass':
                raise tegraflash_exception('Invalid ' + fb_args[2])

            command.extend([fb_args[2]])

        info_print('Parsing fuse bypass information')
        run_command(command)

    def tegraflash_parse_partitionlayout(self, dce_comb=True):
        info_print('Parsing partition layout')
        command = self.exec_file('tegraparser')
        command.extend(['--pt', values['--cfg']])
        self.tegraparser_values['--pt'] = os.path.splitext(values['--cfg'])[0] + '.bin'
        run_command(command)

        kernel_dtb = get_partition_filename('kernel-dtb')
        if kernel_dtb == None:
            kernel_dtb = get_partition_filename('A_kernel-dtb')

        values['--rawkerneldtb'] = kernel_dtb

        # Update dtb with odmdata and overlay dtb, then concatenate
        if values['--odmdata']:
            self.tegraflash_update_bpmp_dtb()
            self.tegraflash_update_cpubl_dtb()
        if values['--bldtb'] is not None and not '_overlay' in values['--bldtb']:
            values['--bldtb'] = tegraflash_create_backup_file(values['--bldtb'], '_overlay')
            tegraflash_concat_overlay_dtb()

        if values['--concat_cpubl_bldtb'] is True:
            self.concatenate_cpubl_bldtb()
        if values['--mb2bct_cfg'] is not None:
            mb2_bin = self.get_mb2bin_filename()
            if mb2_bin is not None:
                # prepare mb2comb_bin in cold boot mode
                mb2_bct_file = self.tegraflash_generate_mb2_bct(True)
                # update storage info in mb2bct
                info_print('Updating mb2-bct with storage information')
                command = self.exec_file('tegrabct')
                command.extend(['--chip', values['--chip'], values['--chip_major']])
                command.extend(['--mb2bct', mb2_bct_file])
                command.extend(['--updatestorageinfo', self.tegraparser_values['--pt']])
                run_command(command)
                mb2comb_bin = self.concatenate_mb2bct_mb2(mb2_bin, mb2_bct_file)
                self.update_mb2comb_filename(mb2comb_bin)
        if dce_comb:
            dce_bin = self.get_dcebin_filename()
            kernel_dtb = self.get_dce_base_dtb_filename()
            if kernel_dtb is None:
                info_print("WARNING: dce base dtb is not provided\n")
            if dce_bin is not None and kernel_dtb is not None:
                dce_comb = self.concatenate_dcebin_kerneldtb(dce_bin, kernel_dtb)
                info_print("Update " + dce_comb + " to dce_fw partitions")
                if dce_comb is not None:
                    self.update_dcecomb_filename(dce_comb)

        # unfortunately have to do twice because entry in partition is updated
        info_print('Parsing partition layout')
        command = self.exec_file('tegraparser')
        command.extend(['--pt', values['--cfg']])
        self.tegraparser_values['--pt'] = os.path.splitext( values['--cfg'])[0] + '.bin'
        run_command(command)

    def tegraflash_oem_enc(self, filename, bct_flag = False):
        file_base, file_ext = os.path.splitext(filename)
        kdf_yaml = 'kdf_args_%s.yaml' %(file_base)

        chip_info = '%s%s %s'  %(values['--chip'], values['--chip_major'], values['--chip_minor'])
        if values['--enable_user_kdk'] == True:
            lines = 'ENC : "USER_KDK"\n'
        else:
            lines = 'ENC : "OEM"\n'
        lines += 'CHIPID : "%s"\n' %(chip_info)
        if values['--chip_major'] == '9' and values['--chip_minor'] == '67' and self.is_rcmboot == True:
            lines += 'BOOTMODE : "RCM"\n'
        with open(kdf_yaml, 'w') as f:
            f.write(lines)
        self.call_tegrasign(filename, None, None, values['--encrypt_key'][0], None, None, None, None, None, None, False, 0, 0, 0, None, 0, ['kdf_file=' + kdf_yaml])
        os.remove(kdf_yaml)
        return file_base + '_encrypt'  + file_ext

    def tegraflash_generate_ratchet_blob(self):
        if not os.path.exists(self.tegrahost_values['--ratchet_blob']):
            info_print('Generating ratchet blob')
            # convert ratchet dts to dtb
            self.tegraflash_preprocess_configs()
            # ratchet blob is generated when generating mb1-bct
            # since the ratchet info is saved in mb1-bct
            self.tegraflash_generate_mb1_bct(True)

    def tegraflash_sign_images(self, ovewrite_xml=True, bct_flag=True):

        info_print('Creating list of images to be signed')
        command = self.exec_file('tegrahost')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--partitionlayout', self.tegraparser_values['--pt']])
        if values['--minratchet_config'] is not None:
            self.tegraflash_generate_ratchet_blob()
            command.extend(['--ratchet_blob',
                            self.tegrahost_values['--ratchet_blob']])
        command.extend(['--list', self.tegrahost_values['--list']])
        mode = self.tegrasign_values['--mode']
        if mode == 'pkc':
            mode = 'oem-rsa'
        elif mode == 'xmss':
            mode = 'oem-xmss'
        command.extend([mode])
        if len(values['--key']) == 3:
            command.extend(['--nkeys', '3'])
        run_command(command)

        if bct_flag:
            info_print('Filling MB1 storage info')
            self.tegraflash_generate_br_bct_multi_chain(True, True, False)

        info_print('Generating signatures')
        key_val = values['--key']
        list_val = self.tegrahost_values['--list']
        pkh_val = self.tegrasign_values['--pubkeyhash']
        self.call_tegrasign(None, None, None, key_val, None,
                            list_val, None, pkh_val, 'sha512', None)
        # Special handling for dce_fw binary which has been compressed.
        dce_bin = self.get_file_name_from_images_list('dce_fw')
        if dce_bin is not None:
            self.update_dcecomb_filename(dce_bin)

    def tegraflash_fill_mb1_storage_info(self):

        info_print('Generating br-bct')
        command = self.exec_file('tegrabct')
        if values['--sdram_config'] is not None and values['--sdram_config'].endswith('.dts'):
            values['--sdram_config'] = self.run_cpp_tool(values['--sdram_config'])
            values['--sdram_config'] = self.run_dtc_tool(values['--sdram_config'])

        if values['--bct'] is None:
            values['--bct'] = 'br_bct.cfg'
        info_print('Updating dev and MSS params in BR BCT')
        command.extend(['--dev_param', values['--dev_params']])
        if values['--sdram_config'] is not None:
            command.extend(['--sdram', values['--sdram_config']])
        command.extend(['--brbct', values['--bct']])
        self.tegrabct_values['--bct'] = os.path.splitext(
            values['--bct'])[0] + '_BR.bct'
        if values['--soft_fuses'] is not None:
            command.extend(['--sfuse', values['--soft_fuses']])
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        run_command(command)

        brbct_arg = '--brbct'
        if self.tegraparser_values['--pt'] is not None:
            info_print('Updating bl info')
            command = self.exec_file('tegrabct')
            command.extend([brbct_arg, self.tegrabct_values['--bct']])
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            if values['--blversion'] is not None:
                command.extend(
                    ['--blversion', values['--majorversion'], values['--minorversion']])
            command.extend(
                ['--updateblinfo', self.tegraparser_values['--pt']])
            run_command(command)

    def tegraflash_generate_bct(self):
        is_coldboot = True
        is_recovery = False
        self.tegraflash_preprocess_configs()
        # generate coldboot br-bct for multiple boot chains
        self.tegraflash_generate_br_bct_multi_chain(is_coldboot, False, True)
        # generate coldboot mb1-bct
        self.tegraflash_generate_mb1_bct(is_coldboot)
        # generate recovery mb1-bct
        self.tegraflash_generate_mb1_bct(is_recovery)
        # generate coldboot mem-bct
        self.tegraflash_generate_mem_bct(is_coldboot)
        # generate recovery mem-bct
        self.tegraflash_generate_mem_bct(is_recovery)

    def tegraflash_generate_br_bct(self, coldboot_bct):

        if values['--bct'] is None:
            values['--bct'] = 'br_bct.cfg'

        info_print('Generating br-bct')
        command = self.exec_file('tegrabct')
        brbct_arg = '--brbct'
        info_print('Updating dev and MSS params in BR BCT')
        command.extend(['--dev_param', values['--dev_params']])
        if values['--sdram_config'] is not None:
            command.extend(['--sdram', values['--sdram_config']])
        command.extend(['--brbct', values['--bct']])
        self.tegrabct_values['--bct'] = os.path.splitext(
            values['--bct'])[0] + '_BR.bct'
        if values['--soft_fuses'] is not None:
            command.extend(['--sfuse', values['--soft_fuses']])
        bct_file = self.tegrabct_values['--bct']
        if os.path.islink(bct_file):
            os.unlink(bct_file)
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        run_command(command)

        if values['--cust_info'] is not None:
            info_print('Updating customer data section')
            command = self.exec_file('tegrabct')
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            command.extend(['--brbct', bct_file])
            command.extend(['--update_custinfo', values['--cust_info']])
            run_command(command)

        if self.tegraparser_values['--pt'] is not None:
            info_print('Updating bl info')
            command = self.exec_file('tegrabct')
            command.extend([brbct_arg, bct_file])
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            command.extend(['--updateblinfo', self.tegraparser_values['--pt']])
            if values['--blversion'] is not None:
                command.extend(
                    ['--blversion', values['--majorversion'], values['--minorversion']])
            command.extend(
                ['--updatesig', self.tegrahost_values['--signed_list']])
            run_command(command)

        self.tegraflash_update_boardinfo(bct_file)

        if values['--encrypt_key'] is not None:
            info_print('Perform encryption on bct')
            enc_file = self.tegraflash_oem_enc(bct_file, True) # br_bct_BR_encrypt.bct
            shutil.copyfile(enc_file, bct_file)

        info_print('Get Signed section of bct')
        command = self.exec_file('tegrabct')
        command.extend([brbct_arg, bct_file])
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--listbct', self.tegrabct_values['--list']])
        run_command(command)

        info_print('Signing BCT')
        key_val = values['--key']
        list_val = self.tegrabct_values['--list']
        sha_val = 'sha512'
        pkh_val = self.tegrasign_values['--pubkeyhash']
        self.call_tegrasign(None, None, None, key_val, None,
                            list_val, None, pkh_val, sha_val, None)

        info_print('Updating BCT with signature')
        command = self.exec_file('tegrabct')
        command.extend([brbct_arg, bct_file])
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--updatesig', self.tegrabct_values['--signed_list']])
        if os.path.isfile(self.tegrasign_values['--pubkeyhash']):
            command.extend(
                ['--pubkeyhash', self.tegrasign_values['--pubkeyhash']])
        run_command(command)

        # Generate and update SHA digest for BR-BCT.
        list_val = self.tegrabct_values['--list']
        info_print('Generating SHA2 Hash')
        self.call_tegrasign(None, None, None, 'None', None,
                            list_val, None, None, sha_val, None)
        info_print('Updating BCT with SHA2 Hash')
        command = self.exec_file('tegrabct')
        command.extend([brbct_arg, bct_file])
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--updatesha',
                        self.tegrabct_values['--signed_list']])
        run_command(command)

    # Generate BR BCT for multiple chains
    def tegraflash_generate_br_bct_multi_chain(self, cold_boot=True, fill_mb1=True, gen_br_bct=True):

        # Parse the passed-in dev params
        parse_dev_params_multi_chain()

        idx = 'A'
        while idx in tegrabct_multi_chain.keys():
            if tegrabct_multi_chain[idx]['dev_params'] is not None:
                # Pre-precess the dev params if it is a dts file.
                # Then set values['--dev_params'] to the dev_params
                # for current boot chain before generating br bct
                if tegrabct_multi_chain[idx]['dev_params'].endswith('.dts'):
                    tegrabct_multi_chain[idx]['dev_params'] = \
                        self.run_cpp_tool(tegrabct_multi_chain[idx]['dev_params'])
                    tegrabct_multi_chain[idx]['dev_params'] = \
                         self.run_dtc_tool(tegrabct_multi_chain[idx]['dev_params'])
                values['--dev_params'] = tegrabct_multi_chain[idx]['dev_params']

                # Fill mb1 storage info and generate br bct with the values['--dev_params']
                if fill_mb1:
                    self.tegraflash_fill_mb1_storage_info()
                if gen_br_bct:
                    self.tegraflash_generate_br_bct(cold_boot)

                # Generate br bct file name for tegrabct_multi_chain[idx]['bct_file']
                # and assign file to this name.
                # If br bct file is not generated, report error and exit
                if self.tegrabct_values['--bct'] is not None:
                    tegrabct_multi_chain[idx]['bct_file'] = \
                        self.tegrabct_values['--bct'].replace('_BR', '_' + idx.lower() + '_BR')
                    os.rename(self.tegrabct_values['--bct'], tegrabct_multi_chain[idx]['bct_file'])
                else:
                    raise tegraflash_exception('Failed to generate BR BCT for boot chain %s\n' %s (idx))

            # Got to the next boot chain if br bct is generated
            # Break if reaching the actual number of chains
            idx = chr(ord(idx) + 1)
            if (ord(idx) - ord('A')) >= int(tegrabct_multi_chain['chains']):
                break

        # Use default name for chain A by:
        # 1) Move file br_bct_a_BR.bct to br_bct_BR.bct
        # 2) Assign "br_bct_BR.bct" to chain A
        os.rename(tegrabct_multi_chain['A']['bct_file'] , self.tegrabct_values['--bct'])
        tegrabct_multi_chain['A']['bct_file'] = self.tegrabct_values['--bct']

        # If BR_BCT_A_backup partition exists, generate bct backup image
        # based on these bct image for multiple chains
        if values['--bct_backup']:
            generate_bct_backup_image()

    def tegraflash_generate_recovery_blob(self, exports, recovery_args):
        values.update(exports)
        output_dir = tegraflash_abs_path('dev_images')

        if not os.path.exists(output_dir):
            os.makedirs(output_dir)

        self.tegraflash_get_key_mode()
        if not recovery_args[0]:
            blob_filename =  'blob.bin'
        else:
            blob_filename = recovery_args[0]

        # Sign image, and not coldboot_mb2bct
        self.tegraflash_generate_blob(True, tegraflash_os_path(output_dir + "/" + blob_filename), False)
        info_print(blob_filename + ' saved in ' + output_dir)

    def tegraflash_generate_blob(self, sign_images, blob_filename, is_coldboot_mb2bct, mb2_is_fskp=False):
        bins=''
        info_print('Generating blob for T23x')
        root = ElementTree.Element('file_list')
        root.set('mode', 'blob')
        comment = ElementTree.Comment('Auto generated by tegraflash.py')
        root.append(comment)
        child = ElementTree.SubElement(root, 'file')
        filename = os.path.basename(values['--bl'])

        if not os.path.exists(filename):
            tegraflash_symlink(tegraflash_abs_path(values['--bl']), filename)

        if not os.path.exists('blob_' + filename):
            tegraflash_symlink(filename, 'blob_' + filename)

        filename = 'blob_' + filename;

        if sign_images:
            if values['--encrypt_key'] is not None:
                filename = self.tegraflash_oem_enc_and_sign_file(filename, 'CPBL')
            else:
                filename = self.tegraflash_oem_sign_file(filename, 'CPBL')

        child.set('name', filename)
        child.set('type', 'bootloader')

        images_to_sign = ['dce_fw', 'mts_mce', 'mb2_bootloader', 'fusebypass', 'mb2_applet',
                'bootloader_dtb', 'spe_fw', 'bpmp_fw', 'bpmp_fw_dtb', 'psc_fw', 'tos', 'eks', 'sce_fw', 'ape_fw',
                'tsec_fw', 'nvdec', 'xusb_fw', 'rce_fw', 'fsi_fw', 'bpmp_ist', 'ccplex_ist', 'ist_ucode']

        if values['--bins']:
            bins = values['--bins'].split(';')

        for binary in bins:
            binary = binary.strip(' ')
            binary = binary.replace('  ', ' ')
            tags = binary.split(' ')
            child = ElementTree.SubElement(root, 'file')
            if (len(tags) < 2):
                raise tegraflash_exception('invalid format ' + binary)

            child.set('type', tags[0])

            filename = os.path.basename(tags[1])
            # handle bpmp_fw_dtb when --odmdata is present
            if tags[0] == 'bpmp_fw_dtb' and self.bpmpdtbodm:
                info_print('Using bpmp-dtb concatenated with odmdata in blob')
                filename = os.path.basename(self.bpmpdtbodm)
            # handle bootloader with mb2bct
            if sign_images and tags[0] == 'mb2_bootloader' and not mb2_is_fskp:
                mb2_bct_file = self.tegraflash_generate_mb2_bct(is_coldboot_mb2bct)
                # update storage info in mb2bct
                info_print('Updating mb2-bct with storage information')
                command = self.exec_file('tegrabct')
                command.extend(['--chip', values['--chip'], values['--chip_major']])
                command.extend(['--mb2bct', mb2_bct_file])
                command.extend(['--updatestorageinfo', self.tegraparser_values['--pt']])
                run_command(command)
                mb2comb_file = self.concatenate_mb2bct_mb2(filename, mb2_bct_file)
                filename = mb2comb_file
            # dce fw needs to concatenate with kernel dtb
            if sign_images and tags[0] == 'dce_fw':
                kernel_dtb = self.get_dce_base_dtb_filename()
                if kernel_dtb is None:
                    info_print("WARNING: kernel dtb is not provided\n")
                else:
                    dce_comb = self.concatenate_dcebin_kerneldtb(filename, kernel_dtb)
                    filename = dce_comb

            if not os.path.exists(filename):
                tegraflash_symlink(tegraflash_abs_path(tags[1]), filename)

            if not os.path.exists('blob_' + filename):
                tegraflash_symlink(filename, 'blob_' + filename)

            filename = 'blob_' + filename;

            if sign_images and tags[0] in images_to_sign:
                magic_id = self.tegraflash_get_magicid(tags[0])
                if values['--encrypt_key'] is not None:
                    filename = self.tegraflash_oem_enc_and_sign_file(filename, magic_id)
                else:
                    filename = self.tegraflash_oem_sign_file(filename, magic_id)

            child.set('name', filename)

            if (len(tags) > 2):
                child.set('load_address', tags[2])

        blobtree = ElementTree.ElementTree(root);
        blobtree.write('blob.xml')

        command = self.exec_file('tegrahost')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--generateblob', 'blob.xml', blob_filename])

        run_command(command)

    def tegraflash_flash(self, args):
        global start_time
        start_time = time.time()
        values.update(args)

        if values['--bl'] is None:
            print('Error: Command line bootloader is not specified')
            return 1

        if values['--cfg'] is None:
            print('Error: Partition configuration is not specified')
            return 1

        if values['--chip'] is None:
            print('Error: chip is not specified')
            return 1

        self.tegraflash_get_key_mode()
        # moved to ::tegraflash_generate_bct()
        #self.tegraflash_preprocess_configs()
        self.tegraflash_parse_partitionlayout()
        if values['--encrypt_key'] is not None:
            self.tegraflash_enc_and_sign_images()
        else:
            self.tegraflash_sign_images()
        self.tegraflash_generate_bct()
        self.tegraflash_update_images()
        self.tegraflash_send_to_bootrom()
        # sign images, and not for coldboot
        self.tegraflash_send_to_bootloader(True, False)
        self.tegraflash_get_storage_info()
        self.tegraflash_poll_applet_bl()
        self.tegraflash_flash_partitions(values['--skipsanitize'])
        self.tegraflash_flash_bct()
        info_print('Flashing completed\n')

    def tegraflash_secureflash(self, args):
        values.update(args)
        self.tegrabct_values['--bct'] = values['--bct']
        self.tegrabct_values['--mb1_bct'] = values['--mb1_bct']
        self.tegrabct_values['--mb1_cold_boot_bct'] = values['--mb1_cold_boot_bct']
        self.tegrabct_values['--membct_rcm'] = values['--mem_bct']
        self.tegrabct_values['--membct_cold_boot'] = values['--mem_bct_cold_boot']
        self.tegraflash_parse_partitionlayout(dce_comb=False)
        self.tegraflash_send_to_bootrom()
        # Do not sign images, and not for coldboot
        self.tegraflash_send_to_bootloader(False, False)
        self.tegraflash_get_storage_info()
        self.tegraflash_poll_applet_bl()
        self.tegraflash_flash_partitions(values['--skipsanitize'])
        self.tegraflash_flash_bct()
        info_print('Secure Flashing completed\n')

    def tegraflash_send_to_bootrom(self):
        global uidlog
        # non-secure case generate bct at run time
        if values['--securedev'] and not self.tegrabct_values['--updated']:
            self.tegraflash_update_boardinfo(self.tegrabct_values['--bct'])

        mb1_bin = values['--mb1_bin']
        if mb1_bin == None:
            mb1_bin = self.get_file_name_from_images_list('mb1_bootloader')
            info_print(mb1_bin + " filename is from images_list")
        else:
            info_print(mb1_bin + " filename is from --mb1_bin")

        psc_bl1_bin = values['--psc_bl1_bin']
        if psc_bl1_bin == None:
            psc_bl1_bin = self.get_file_name_from_images_list('psc_bl1')
            info_print(psc_bl1_bin + " filename is from images_list")
        else:
            info_print(psc_bl1_bin + " filename is from --psc_bl1_bin")

        info_print('Boot Rom communication')
        command = self.exec_file('tegrarcm')
        command.extend(['--new_session'])
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--uid'])
        command.extend(['--download', 'bct_br', self.tegrabct_values['--bct']])
        command.extend(['--download', 'mb1', mb1_bin])
        command.extend(['--download', 'psc_bl1', psc_bl1_bin])
        command.extend(['--download', 'bct_mb1', self.tegrabct_values['--mb1_bct']])

        uidlog = run_command(command, True)
        info_print('Boot Rom communication completed')

    def tegraflash_send_to_bootloader(self, sign_images = True, is_coldboot = False, mb2_is_fskp=False):
        self.tegraflash_generate_blob(sign_images, 'blob.bin', is_coldboot, mb2_is_fskp)

        info_print('Sending membct and RCM blob')
        command = self.exec_file('tegrarcm')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--pollbl'])
        command.extend(['--download', 'bct_mem', self.tegrabct_values['--membct_rcm']])
        command.extend(['--download', 'blob', 'blob.bin'])

        run_command(command)
        info_print('completed')

    def tegraflash_boot(self, boot_type):
        command = self.exec_file('tegrarcm')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--boot', boot_type])
        run_command(command)
        if boot_type == 'recovery':
            self.tegraflash_poll_applet_bl()

    def tegraflash_send_mb2_applet(self):
        filename = None
        bins = values['--bins'].split(';')
        for binary in bins:
            binary = binary.strip(' ')
            binary = binary.replace('  ', ' ')
            tags = binary.split(' ')
            if tags[0] == 'mb2_applet':
                filename = tags[1]
                break
        if filename is None:
            raise tegraflash_exception('mb2 applet not found in --bins')

        if values['--encrypt_key'] is not None:
            filename = self.tegraflash_oem_enc_and_sign_file(filename, 'MB2A')
        else:
            filename = self.tegraflash_oem_sign_file(filename, 'MB2A')

        info_print('Sending mb2_applet...\n')
        command = self.exec_file('tegrarcm')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--pollbl'])
        command.extend(['--download', 'applet', filename])
        run_command(command)
        info_print('completed')

    def tegraflash_boot_mb2_applet(self):
        filename = self.tegraflash_send_mb2_applet()
        #self.tegraflash_boot('recovery')

        count = 30
        while count != 0 and not self.check_is_mb2applet():
            time.sleep(1)
            count = count - 1

    def tegraflash_poll_applet_bl(self):
        count = 30;
        enable_print = True;
        while count != 0:
            time.sleep(1)
            count = count - 1
            if self.check_is_mb2applet() or self.check_ismb2():
                return

        if count == 0:
            raise tegraflash_exception('None of the bootloaders are running on device. Check the UART log.')

    def check_is_mb2applet(self):
        try:
            command = self.exec_file('tegrarcm')
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            command.extend(['--ismb2applet'])
            run_command(command)
            return True
        except tegraflash_exception as e:
            return False

    def check_ismb2(self):
        try:
            command = self.exec_file('tegrarcm')
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            command.extend(['--ismb2'])
            run_command(command)
            return True
        except tegraflash_exception as e:
            return False

    def tegraflash_flash_partitions(self, skipsanitize):
        info_print('Flashing the device')

        command = self.exec_file('tegraparser')
        command.extend(['--storageinfo', self.tegrarcm_values['--storage_info']])
        command.extend(['--generategpt', '--pt', self.tegraparser_values['--pt']])
        run_command(command)

        if not values['--sparseupdate']:
            self.tegraflash_just_flash(skipsanitize)
            return

        command = self.exec_file('tegraparser')
        command.extend(['--pt', self.tegraparser_values['--pt']])
        command.extend(['--generateflashindex', 'flash.idx'])
        run_command(command)

        total_devices, qspi_device, partitions, device_info = parse_indexfile_for_qspi('flash.idx')

        if qspi_device:
            for i in range(total_devices):
                if i in qspi_device and self.compareGPTOfQspi(i, device_info):
                    self.sparseUpdateQspi(i, device_info, skipsanitize)
                else:
                    self.tegraflash_just_flash(skipsanitize, device=i+1)
        else:
            self.tegraflash_just_flash(skipsanitize)

    def sparseUpdateQspi(self, ix, device_info, skipsanitize):
        output_dir = tegraflash_abs_path('temp')
        # Create a directory to store file read from device. If exists, clear all files
        if os.path.exists(output_dir):
            shutil.rmtree(output_dir, ignore_errors=True)
        os.makedirs(output_dir)

        info_print("Sparse flash qspi device instance {}".format(device_info[ix]["instance"]))
        for part in device_info[ix]['parts']:
            device, instance, name = part[1].split(':')
            file_to_write = part[4]

            if file_to_write:
                file_read_from_device = "{}/{}_read".format(output_dir, file_to_write)

                command = self.exec_file('tegradevflash')
                command.extend(['--read', "/spi/{}/{}".format(device_info[ix]["instance"], name)])
                command.extend([file_read_from_device])
                run_command(command)

                with open(file_to_write, "rb") as f1, open(file_read_from_device, "rb") as f2:
                    bin1 = f1.read()
                    bin2 = f2.read()
                    if bin1 != bin2[:len(bin1)]:
                        command = self.exec_file('tegradevflash')
                        command.extend(['--erase', "/spi/{}/{}".format(device_info[ix]["instance"], name)])
                        run_command(command)

                        command = self.exec_file('tegradevflash')
                        command.extend(['--write', "/spi/{}/{}".format(device_info[ix]["instance"], name)])
                        command.extend(["{}".format(file_to_write)])
                        run_command(command)

            else:
                command = self.exec_file('tegradevflash')
                command.extend(['--erase', "/spi/{}/{}".format(device_info[ix]["instance"], name)])
                run_command(command)



    def compareGPTOfQspi(self, ix, device_info):
        info_print("Checking partition table of QSPI instance {}".format(device_info[ix]["instance"]))
        output_dir = tegraflash_abs_path('temp')
        # Create a directory to store file read from device. If exists, clear all files
        if os.path.exists(output_dir):
            shutil.rmtree(output_dir, ignore_errors=True)
        os.makedirs(output_dir)
        qspi_gpt_name = "gpt_secondary_3_{}.bin".format(device_info[ix]["instance"])
        try:
            file_read_from_device = "{}/{}_read".format(output_dir, qspi_gpt_name)
            command = self.exec_file('tegradevflash')
            command.extend(['--read', "/spi/{}/secondary_gpt".format(device_info[ix]["instance"])])
            command.extend([file_read_from_device])
            run_command(command)

            return compareGPT(qspi_gpt_name, file_read_from_device)
        except:
            return False

    def tegraflash_flash_secondary_gpt_backup(self):
        if os.path.exists("gpt_secondary_3_0.bin"):
            # On QSPI
            filename = "gpt_secondary_3_0.bin"
        elif os.path.exists("gpt_secondary_0_3.bin"):
            # On eMMC
            filename = "gpt_secondary_0_3.bin"
        elif os.path.exists("gpt_secondary_7_0.bin"):
            # On UFS
            filename = "gpt_secondary_7_0.bin"
        else:
            raise tegraflash_exception("No image is found for secondary_gpt_backup partition")
        command = self.exec_file('tegradevflash')
        command.extend(['--write', 'secondary_gpt_backup', filename])
        run_command(command)
        return

    def tegraflash_just_flash(self, skipsanitize, device=None):

        if device:
            info_print("Start flashing device {}".format(device))
        else:
            info_print("Start flashing")

        command = self.exec_file('tegradevflash')
        command.extend(['--pt', self.tegraparser_values['--pt']])

        if skipsanitize:
            command.extend(['--skipsanitize'])

        command.extend(['--create']);
        if device and type(device) == int:
            command.extend(['--dev',str(device)])
        run_command(command)

        # Flash secondary_gpt_backup partition if required
        if bool(values['--secondary_gpt_backup']) == True:
           self.tegraflash_flash_secondary_gpt_backup()

    def tegraflash_flash_bct(self):
        command = self.exec_file('tegradevflash')
        # Write BCT image for the boot chain accordingly based on option "--boot_chain".
        # Without any option, the default is chain A.
        chain = 'A'
        binary = self.tegrabct_values['--bct']
        if values['--boot_chain'] is not None:
            chain = values['--boot_chain']
        if chain in tegrabct_multi_chain.keys():
            if tegrabct_multi_chain[chain]['bct_file'] is not None:
                binary = tegrabct_multi_chain[chain]['bct_file']
        else:
            raise tegraflash_exception('Invalid boot chain %s\n' %s (chain))
        command.extend(['--write', 'BCT', binary]);
        run_command(command)

        # Write BCT-boot-chain_backup partitions if required.
        if values['--bct_backup'] and tegrabct_backup['--image'] is not None:
            command = self.exec_file('tegradevflash')
            command.extend(['--write', 'BCT-boot-chain_backup', tegrabct_backup['--image']]);
            run_command(command)

        if self.tegrabct_values['--mb1_cold_boot_bct'] is not None:
            mb1_bct_parts = getPart_name_by_type(values['--cfg'], 'mb1_boot_config_table')
            for name in mb1_bct_parts:
                command = self.exec_file('tegradevflash')
                command.extend(['--write', name, self.tegrabct_values['--mb1_cold_boot_bct']]);
                run_command(command)

        if self.tegrabct_values['--membct_cold_boot'] is not None:
            mb1_bct_parts = getPart_name_by_type(values['--cfg'], 'mem_boot_config_table')
            for name in mb1_bct_parts:
                command = self.exec_file('tegradevflash')
                command.extend(['--write', name, self.tegrabct_values['--membct_cold_boot']]);
                run_command(command)

    def tegraflash_reboot(self, args):
        if args[0] == 'coldboot':
            info_print('Coldbooting the device')
        elif args[0] == 'recovery':
            info_print('Rebooting to recovery mode')
        elif args[0] == 'rcm':
            info_print('Rebooting to rcm mode')
        else:
            raise tegraflash_exception(args[0] + " is not supported")

        if self.check_ismb2():
            self.tegraflash_tboot_reset(args)
        elif self.check_is_mb2applet():
            self.tegraflash_mb2applet_reset(args)
        else:
            command = self.exec_file('tegradevflash')
            command.extend(['--reboot', args[0]])
            run_command(command)
            time.sleep(2)

    def tegraflash_mb2applet_reset(self, args):
        if args[0] == 'rcm':
            info_print('Booting rcm mode')
        elif args[0] == 'recovery':
            info_print('Booting to recovery mode')
        else:
            raise tegraflash_exception(args[0] + " is not supported")

        command = self.exec_file('tegrarcm')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--reboot', args[0]])
        run_command(command)
        time.sleep(2)


    def tegraflash_tboot_reset(self, args):
        if args[0] == 'coldboot':
            info_print('Coldbooting the device')
        elif args[0] == 'recovery':
            info_print('Rebooting to recovery mode')
        else:
            raise tegraflash_exception(args[0] + " is not supported")

        command = self.exec_file('tegrarcm')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--reboot', args[0]])
        run_command(command)
        time.sleep(2)

    def tegraflash_rcmboot(self, args):
        global start_time
        start_time = time.time()
        values.update(args)

        if values['--bl'] is None:
            print('Error: Command line bootloader is not specified')
            return 1

        if values['--chip'] is None:
            print('Error: chip is not specified')
            return 1

        if not values['--tegraflash_v2']:
            if values['--bldtb'] is None:
                print('Error: bl dtb is not specified')
                return 1

            if values['--kerneldtb'] is None:
                print('Error: kernel dtb is not specified')
                return 1

        if values['--securedev']:
            if values['--bct'] is None:
                print('Error: BCT is not specified')
                return 1

            mb1_bin = values['--mb1_bin']
            if mb1_bin == None:
                mb1_bin = self.get_file_name_from_images_list('mb1_bootloader')
                info_print(mb1_bin + " filename is from images_list")
            else:
                info_print(mb1_bin + " filename is from --mb1_bin")

            psc_bl1_bin = values['--psc_bl1_bin']
            if psc_bl1_bin == None:
                psc_bl1_bin = self.get_file_name_from_images_list('psc_bl1')
                info_print(psc_bl1_bin + " filename is from images_list")
            else:
                info_print(psc_bl1_bin + " filename is from --psc_bl1_bin")

            info_print('rcm boot with presigned binaries')
            # send these binary to BR
            command = self.exec_file('tegrarcm')
            command.extend(['--new_session'])
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            command.extend(['--uid'])
            command.extend(['--download', 'bct_br', values['--bct']])
            command.extend(['--download', 'mb1', mb1_bin])
            command.extend(['--download', 'psc_bl1', psc_bl1_bin])
            command.extend(['--download', 'bct_mb1', values['--mb1_bct']])
            run_command(command, True)

            self.tegraflash_generate_blob(False, 'blob.bin', True)
            # send these binary to BL
            command = self.exec_file('tegrarcm')
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            command.extend(['--pollbl'])
            command.extend(['--download', 'bct_mem', values['--mem_bct']])
            command.extend(['--download', 'blob', 'blob.bin'])

            run_command(command)

        else:
            self.is_rcmboot = True
            self.tegraflash_get_key_mode()
            args['--skipuid'] = False
            self.tegraflash_preprocess_configs()
            self.tegraflash_parse_partitionlayout()
            if values['--encrypt_key'] is not None:
                self.tegraflash_enc_and_sign_images()
            else:
                self.tegraflash_sign_images()
            self.tegraflash_generate_bct()
            self.tegraflash_update_images()
            self.tegraflash_send_to_bootrom()
            # sign image, and for coldboot as it needs to disable bit 'enable_flashing'
            self.tegraflash_send_to_bootloader(True, True)
            self.is_rcmboot = False # Reset

        info_print('RCM-boot started\n')

    def tegraflash_update_boardinfo(self, bct_file):
        if values['--nct'] is not None:
            info_print('Updating board information into bct')
            command = self.exec_file('tegraparser')
            command.extend(['--nct', values['--nct']])
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            command.extend(['--updatecustinfo', bct_file])
            if values['--securedev']:
                command.extend(['--secure'])
            run_command(command)
            self.tegrabct_values['--updated'] = True
        elif values['--boardconfig'] is not None:
            info_print(
                'Updating board information from board config into bct')
            command = self.exec_file('tegraparser')
            command.extend(['--boardconfig', values['--boardconfig']])
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            command.extend(['--updatecustinfo', bct_file])
            run_command(command)
            self.tegrabct_values['--updated'] = True

    def get_partition_partition_type(self, name_type):
        with open(values['--cfg'], 'r') as file:
            xml_tree = ElementTree.parse(file)

        root = xml_tree.getroot()
        target_bin_file = None

        for node in root.iter('partition'):
            if(node.get('name').lower() == name_type):
                return node.get('type').strip()
        raise tegraflash_exception('Error: Can not find partition type for ' + name_type)

    def tegraflash_update_bpmp_dtb(self):
        #  Supported config list for t234
        uphy_config = ["hsio-uphy-config", "nvhs-uphy-config", "gbe-uphy-config", "hsstp-lane-map"]
        misc_config = ["gbe0-enable-10g", "gbe1-enable-10g", "gbe2-enable-10g", "gbe3-enable-10g"]
        chipid = "t234"

        try:
            if values['--odmdata'] is None:
                return
            odm_list = strip_string_list(values['--odmdata'].strip().split(','))

            bpmp_dtb_in_layout = get_partition_filename('bpmp_fw_dtb', 'type')
            if (values['--bins']):
                m = re.search('bpmp_fw_dtb[\s]+([\w._-]+)', values['--bins'])
                if m:
                    bpmp_dtb = m.group(1)
            if bpmp_dtb == None and bpmp_dtb_in_layout == None:
                info_print('bpmp_dtb does not exist')
                return
            if bpmp_dtb != None and bpmp_dtb_in_layout != None and bpmp_dtb != bpmp_dtb_in_layout:
                info_print('inconsistent bpmp dtb file names')
                return
            if bpmp_dtb == None and bpmp_dtb_in_layout != None:
                bpmp_dtb = bpmp_dtb_in_layout;

            # Create the backup dtb
            bpmp_dtb = tegraflash_create_backup_file(bpmp_dtb, '_with_odm')
            if bpmp_dtb_in_layout != None:
                set_partition_filename('bpmp_fw_dtb', bpmp_dtb, 'type')

            with open(bpmp_dtb, 'rb') as infile:
                dtb = pyfdt.FdtBlobParse(infile)
            fdt = dtb.to_fdt()

            uphy_node = fdt.resolve_path("/uphy")
            if not uphy_node:
                uphy_node = pyfdt.FdtNode('uphy')
                root_node = fdt.resolve_path("/")
                root_node.append(uphy_node)

            # Remove exising gbe config entries.
            for cfg in misc_config:
                try:
                    uphy_node.remove(cfg)
                except:
                    pass
                for prop in odm_list:
                    if prop == cfg:
                        uphy_node.insert(0, pyfdt.FdtProperty(cfg))

            for cfg in uphy_config:
                for prop in odm_list:
                    if prop.startswith(cfg) and re.match(r'{}-\d+$'.format(cfg), prop):
                        try:
                            uphy_node.remove(cfg)
                        except:
                            pass
                        val = int(prop.rsplit('-', 1)[1])
                        uphy_node.insert(0, pyfdt.FdtPropertyWords(cfg, [val]))

            with open(bpmp_dtb,'wb') as outfile:
                outfile.write(fdt.to_dtb())
                self.bpmpdtbodm = bpmp_dtb

            with open(os.path.splitext(bpmp_dtb)[0] + ".dts",'w') as outfile:
                outfile.write(fdt.to_dts())

            # Create temp file for dtbcheck.py
            tmp_dtb = tegraflash_create_backup_file(bpmp_dtb, '_tmp')
            if os.path.exists(tmp_dtb) and os.path.exists('dtbcheck.py'):
                if sys.executable:
                    python_path = sys.executable
                else:
                    python_path = 'python3' # set as default
                command = [python_path, "dtbcheck.py"]
                command.extend(["-c", chipid])
                command.extend(["-o", bpmp_dtb])
                command.extend([tmp_dtb])
                run_command(command, True)
                os.remove(tmp_dtb)
            else:
                raise tegraflash_exception('Unexpected error in updating: ' + bpmp_dtb + ' ' )
        except Exception as e:
            raise tegraflash_exception('Unexpected error in updating: ' + bpmp_dtb + ' ' + str(e))

    def tegraflash_update_cpubl_dtb(self):
        if values['--bldtb'] is None or values['--odmdata'] is None:
            return

        try:
            # Create the backup dtb
            cpubl_dtb = tegraflash_create_backup_file(values['--bldtb'], '_with_odm')
            values['--bldtb'] = cpubl_dtb

            tegraflash_add_odm_data_to_dtb(values['--odmdata'], cpubl_dtb)

        except Exception as e:
            raise tegraflash_exception("Unexpected error in updating: " + cpubl_dtb + ' ' + str(e))

    def tegraflash_generate_mb1_bct(self, is_cold_boot_mb1_bct):
        if bool(is_cold_boot_mb1_bct) == True:
            info_print('Generating coldboot mb1-bct')
        else:
            info_print('Generating recovery mb1-bct')

        command = self.exec_file('tegrabct')
        command.extend(['--chip', values['--chip'], values['--chip_major']])

        tmp = None
        if values['--mb1_bct'] is None:
            values['--mb1_bct'] = 'mb1_bct.cfg'
        tmp = values['--mb1_bct']
        if bool(is_cold_boot_mb1_bct) == True:
            if values['--mb1_cold_boot_bct'] is None:
                values['--mb1_cold_boot_bct'] = 'mb1_cold_boot_bct.cfg'
            tmp = values['--mb1_cold_boot_bct']
        if tmp is not None:
            command.extend(['--mb1bct', tmp])

        tmp = None
        if values['--misc_config'] is not None:
            tmp = values['--misc_config']
        if bool(is_cold_boot_mb1_bct) == True:
            if values['--misc_cold_boot_config'] is not None:
                tmp = values['--misc_cold_boot_config']
        if tmp is not None:
            command.extend(['--misc', tmp])

        if values['--wb0sdram_config'] is not None:
            command.extend(['--wb0sdram', values['--wb0sdram_config']])
        if values['--pinmux_config'] is not None:
            command.extend(['--pinmux', values['--pinmux_config']])
        if values['--pmc_config'] is not None:
            command.extend(['--pmc', values['--pmc_config']])
        if values['--pmic_config'] is not None:
            command.extend(['--pmic', values['--pmic_config']])
        if values['--br_cmd_config'] is not None:
            command.extend(['--brcommand', values['--br_cmd_config']])
        if values['--prod_config'] is not None:
            command.extend(['--prod', values['--prod_config']])
        if values['--gpioint_config'] is not None:
            command.extend(['--gpioint', values['--gpioint_config']])
        if values['--uphy_config'] is not None:
            command.extend(['--uphy', values['--uphy_config']])
        if values['--device_config'] is not None:
            command.extend(['--device', values['--device_config']])
        if values['--deviceprod_config'] is not None:
            command.extend(
                ['--deviceprod', values['--deviceprod_config']])
        if values['--fb'] is not None:
            command.extend(['--fb', values['--fb']])
        if values['--minratchet_config'] is not None:
            command.extend(
                ['--minratchet', values['--minratchet_config']])
            command.extend(
                ['--ratchet_blob', self.tegrahost_values['--ratchet_blob']])

        run_command(command)

        if bool(is_cold_boot_mb1_bct) == True:
            self.tegrabct_values['--mb1_cold_boot_bct'] = os.path.splitext(
                values['--mb1_cold_boot_bct'])[0] + '_MB1.bct'
        else:
            self.tegrabct_values['--mb1_bct'] = os.path.splitext(values['--mb1_bct'])[
                0] + '_MB1.bct'

        if self.tegraparser_values['--pt'] is not None:

            if bool(is_cold_boot_mb1_bct) == True:
                mb1bct_file = self.tegrabct_values['--mb1_cold_boot_bct']
            else:
                mb1bct_file = self.tegrabct_values['--mb1_bct']

            info_print('Updating mb1-bct with firmware information')
            command = self.exec_file('tegrabct')
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            command.extend(['--mb1bct', mb1bct_file])
            if bool(is_cold_boot_mb1_bct) == False:
                command.extend(['--recov'])
            command.extend(['--updatefwinfo', self.tegraparser_values['--pt']])
            run_command(command)

        if bool(is_cold_boot_mb1_bct) == True:
            if values['--encrypt_key'] is not None:
                old_name = self.tegrabct_values['--mb1_cold_boot_bct']
                self.tegrabct_values['--mb1_cold_boot_bct'] = self.tegraflash_oem_enc_and_sign_file(
                    self.tegrabct_values['--mb1_cold_boot_bct'], 'MBCT')
                # Need to update pt name since file name is changed
                self.tegraflash_update_pt_name(old_name, self.tegrabct_values['--mb1_cold_boot_bct'])
            else:
                self.tegrabct_values['--mb1_cold_boot_bct'] = self.tegraflash_oem_sign_file(
                    self.tegrabct_values['--mb1_cold_boot_bct'], 'MBCT')
        else:
            if values['--encrypt_key'] is not None:
                self.tegrabct_values['--mb1_bct'] = self.tegraflash_oem_enc_and_sign_file(
                    self.tegrabct_values['--mb1_bct'], 'MBCT')
            else:
                self.tegrabct_values['--mb1_bct'] = self.tegraflash_oem_sign_file(
                    self.tegrabct_values['--mb1_bct'], 'MBCT')

    def tegraflash_oem_sign_file(self, in_file, magic_id):
        filename = os.path.basename(in_file)
        aligned_file = os.path.splitext(
            filename)[0] + '_aligned' + os.path.splitext(filename)[1]
        if os.path.exists(in_file):
            shutil.copyfile(in_file, aligned_file)
        command = self.exec_file('tegrahost')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--align', aligned_file])
        run_command(command)

        filename = aligned_file
        mode = self.tegrasign_values['--mode']
        if mode == 'pkc':
            mode = 'oem-rsa'
        elif mode == 'ec':
            mode = 'oem-ecc'
        elif mode == 'eddsa':
            mode = 'oem-eddsa'
        elif mode == 'xmss':
            mode = 'oem-xmss'

        command = self.exec_file('tegrahost')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        # fixme : right magicid needs to be passed in recovery path
        command.extend(['--magicid', magic_id])
        if values['--minratchet_config'] is not None:
            self.tegraflash_generate_ratchet_blob()
            command.extend(['--ratchet_blob',
                            self.tegrahost_values['--ratchet_blob']])
        command.extend(['--appendsigheader', filename, mode])
        if values['--ecid'] is not None:
            command.extend(['--ecid', values['--ecid']])

        run_command(command)
        filename = os.path.splitext(
            filename)[0] + '_sigheader' + os.path.splitext(filename)[1]

        root = ElementTree.Element('file_list')
        comment = ElementTree.Comment('Auto generated by tegraflash.py')
        root.append(comment)
        child = ElementTree.SubElement(root, 'file')
        child.set('name', filename)
        # fixed offsets for BCH
        if self.bch_offset is not None:
            child.set('offset', self.bch_offset)
        if self.bch_length is not None:
            child.set('length', self.bch_length)
        sbk = ElementTree.SubElement(child, 'sbk')
        sbk.set('encrypt', '1')
        sbk.set('sign', '1')
        sbk.set('encrypt_file', filename + '.encrypt')
        sbk.set('hash', filename + '.hash')
        pkc = ElementTree.SubElement(child, 'pkc')
        pkc.set('signature', filename + '.sig')
        pkc.set('signed_file', filename + '.signed')
        ecc = ElementTree.SubElement(child, 'ec')
        ecc.set('signature', filename + '.sig')
        ecc.set('signed_file', filename + '.signed')
        eddsa = ElementTree.SubElement(child, 'eddsa')
        eddsa.set('signature', filename + '.sig')
        eddsa.set('signed_file', filename + '.signed')
        xmss = ElementTree.SubElement(child, 'xmss')
        xmss.set('signature', filename + '.sig')
        xmss.set('signed_file', filename + '.signed')

        sign_tree = ElementTree.ElementTree(root)
        sign_tree.write(filename + '_list.xml')

        key_val = values['--key']
        list_val = filename + '_list.xml'
        pkh_val = self.tegrasign_values['--pubkeyhash']
        self.call_tegrasign(None, None, None, key_val, None,
                            list_val, None, pkh_val, 'sha512', None)
        sign_xml_file = filename + '_list_signed.xml'
        with open(sign_xml_file, 'rt') as file:
            xml_tree = ElementTree.parse(file)
        mode = xml_tree.getroot().get('mode')
        if mode == "pkc":
            sig_type = "oem-rsa"
            list_text = "signed_file"
            sig_file = "signature"
        else:
            if mode == "ec":
                sig_type = "oem-ecc"
                list_text = "signed_file"
                sig_file = "signature"
            else:
                if mode == "eddsa":
                    sig_type = "oem-eddsa"
                    list_text = "signed_file"
                    sig_file = "signature"
                elif mode == "xmss":
                    sig_type = "oem-xmss"
                    list_text = "signed_file"
                    sig_file = "signature"
                else:
                    list_text = "encrypt_file"
                    sig_type = "zerosbk"
                    sig_file = "hash"
        signed_file = filename
        for file_nodes in xml_tree.iter('file'):
            signed_file = file_nodes.find(mode).get(list_text)
            sig_file = file_nodes.find(mode).get(sig_file)

        command = self.exec_file('tegrahost')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        if os.path.isfile(self.tegrasign_values['--pubkeyhash']):
            command.extend(
                ['--pubkeyhash', self.tegrasign_values['--pubkeyhash']])
        command.extend(['--updatesigheader', signed_file, sig_file, sig_type])
        run_command(command)

        signed_file = os.path.splitext(
            signed_file)[0] + os.path.splitext(signed_file)[1]
        newname = signed_file.replace('_aligned', '')
        shutil.copyfile(signed_file, newname)
        signed_file = newname
        return signed_file

    def tegraflash_oem_enc_and_sign_file(self, in_file, magic_id):
        filename = in_file
        info_print(filename)

        info_print('Encrypting and signing ' + in_file)
        is_aligned = False
        algo_list = {'pkc':'oem-rsa', 'ec':'oem-ecc', 'eddsa':'oem-eddsa', 'xmss':'oem-xmss'}

        if not self. _is_header_present(filename):
            aligned_file = os.path.splitext(
                filename)[0] + '_aligned' + os.path.splitext(filename)[1]
            if os.path.exists(in_file):
                shutil.copyfile(in_file, aligned_file)
            command = self.exec_file('tegrahost')
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            command.extend(['--align', aligned_file])
            run_command(command)
            is_aligned = True
            filename = aligned_file
            mode = self.tegrasign_values['--mode']
            if mode in algo_list:
                mode = algo_list[mode]

            command = self.exec_file('tegrahost') # stage1.sha, stage2.sha, bch.sha
            command.extend(['--appendsigheader', filename, mode])
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            command.extend(['--magicid', magic_id])
            if values['--minratchet_config'] is not None:
                self.tegraflash_generate_ratchet_blob()
                command.extend(['--ratchet_blob', self.tegrahost_values['--ratchet_blob']])
            if values['--ecid'] is not None:
                command.extend(['--ecid', values['--ecid']])
            run_command(command)
            filename = os.path.splitext(
                filename)[0] + '_sigheader' + os.path.splitext(filename)[1]

        enc_file = self.tegraflash_oem_enc(filename)

        root = ElementTree.Element('file_list')
        comment = ElementTree.Comment('Auto generated by tegraflash.py')
        root.append(comment)
        child = ElementTree.SubElement(root, 'file')
        child.set('name', enc_file)
        # fixed offsets for BCH
        if self.bch_offset is not None:
            child.set('offset', self.bch_offset)
        if self.bch_length is not None:
            child.set('length', self.bch_length)
        sbk = ElementTree.SubElement(child, 'sbk')
        sbk.set('encrypt', '0')
        sbk.set('sign', '1')
        sbk.set('encrypt_file', enc_file)
        sbk.set('hash', enc_file + '.hash')

        for algo in algo_list.keys():
            node = ElementTree.SubElement(child, algo)
            node.set('signature', enc_file + '.sig')
            node.set('signed_file', enc_file + '.signed')
            if algo in ['pkc', 'ec']:
                node.set('digest_type', 'sha512')

        sign_tree = ElementTree.ElementTree(root)
        sign_tree.write(enc_file + '_list.xml')

        key_val = values['--key']
        list_val = enc_file + '_list.xml'
        pkh_val = self.tegrasign_values['--pubkeyhash']
        self.call_tegrasign(None, None, None, key_val, None,
                            list_val, None, pkh_val, 'sha512', None)
        sign_xml_file = enc_file + '_list_signed.xml'

        with open(sign_xml_file, 'rt') as file:
            xml_tree = ElementTree.parse(file)
            mode = xml_tree.getroot().get('mode')
        if mode in algo_list:
            sig_type = algo_list[mode]
            list_text = "signed_file"
            sig_file = "signature"
        else:
            list_text = "encrypt_file"
            sig_type = "zerosbk"
            sig_file = "hash"
        signed_file = enc_file
        for file_nodes in xml_tree.iter('file'):
            signed_file = file_nodes.find(mode).get(list_text)
            sig_file = file_nodes.find(mode).get(sig_file)

        command = self.exec_file('tegrahost')
        command.extend(
            ['--chip', values['--chip'], values['--chip_major']])
        if os.path.isfile(self.tegrasign_values['--pubkeyhash']):
            command.extend(
                ['--pubkeyhash', self.tegrasign_values['--pubkeyhash']])
        command.extend(['--updatesigheader', signed_file, sig_file, sig_type])
        run_command(command)

        if is_aligned:
            newname = signed_file.replace('_aligned', '')
            shutil.copyfile(signed_file, newname)
            signed_file = newname
        return signed_file

    def tegraflash_update_pt_name(self, pt_name, new_name):
        skip_types = ['boot_config_table', 'mb2_applet', ]
        pt_base, pt_ext = os.path.splitext(pt_name)

        with open(values['--cfg'], 'rt') as file:
            xml_tree = ElementTree.parse(file)
            root = xml_tree.getroot()
            for node in root.findall('.//partition'):

                file_node = node.find('filename')
                if file_node is not None and file_node.text is not None:
                    filename = file_node.text.strip()
                    file_base, file_ext = os.path.splitext(filename)

                    if (not file_base.startswith(pt_base) and not pt_base.startswith(file_base)):
                        continue
                    part_type = node.attrib.get('type').strip()
                    if part_type in skip_types:
                        continue
                    part_name = node.attrib.get('name').strip()

                    command = self.exec_file('tegraparser')
                    command.extend(['--pt', self.tegraparser_values['--pt']])
                    command.extend(['--update_part_filename', part_name, part_type, new_name])
                    run_command(command)

    def tegraflash_enc_and_sign_images(self, ovewrite_xml=True, bct_flag=False):

        info_print('Creating list of images to be encrypted and signed')
        tmp_files = {}

        command = self.exec_file('tegrahost')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--partitionlayout', self.tegraparser_values['--pt']])
        if values['--minratchet_config'] is not None:
            self.tegraflash_generate_ratchet_blob()
            command.extend(['--ratchet_blob',
                            self.tegrahost_values['--ratchet_blob']])

        command.extend(['--list', self.tegrahost_values['--list']])

        mode = self.tegrasign_values['--mode']
        algo_list = {'pkc':'oem-rsa', 'ec':'oem-ecc', 'eddsa':'oem-eddsa', 'xmss':'oem-xmss'}

        if mode in algo_list:
            mode = algo_list[mode]

        command.extend([mode])
        run_command(command)

        if bct_flag:
            self.tegraflash_generate_br_bct_multi_chain(True, True, False)

        with open(self.tegrahost_values['--list'], 'rt') as file:
            xml_tree = ElementTree.parse(file)
            mode = xml_tree.getroot().get('mode')

            for file_nodes in xml_tree.getiterator('file'):
                filename = file_nodes.get('name')
                if 'dce' in filename:
                    continue
                enc_file = self.tegraflash_oem_enc(filename, bct_flag)
                self.tegraflash_update_pt_name(filename, enc_file)
                tmp_files.update({filename: enc_file})

        # Need to re-generate images_list.xml with the new file names
        with open(self.tegrahost_values['--list'], "r+") as f:
            content = f.read()
            for tmp in tmp_files:
                content = content.replace(tmp, tmp_files[tmp])
            f.seek(0)
            f.write(content)

        key_val = values['--key']
        list_val = self.tegrahost_values['--list']
        pkh_val = self.tegrasign_values['--pubkeyhash']
        self.call_tegrasign(None, None, None, key_val, None,
                            list_val, None, pkh_val, 'sha512', None)
        return

    def tegraflash_generate_mem_bct(self, is_cold_boot_mb1_bct):
        if values['--sdram_config'] is None:
            info_print('Error: Skip generating mem_bct because sdram_config is not defined')
            return 1
        if bool(is_cold_boot_mb1_bct) == True:
            info_print('Generating coldboot mem-bct')
        else:
            info_print('Generating recovery mem-bct')

        command = self.exec_file('tegrabct')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--sdram', values['--sdram_config']])
        if values['--wb0sdram_config'] is not None:
            command.extend(['--wb0sdram', values['--wb0sdram_config']])

        filename = os.path.splitext(values['--sdram_config'])[0]
        mem_bcts = [filename + "_1.bct", filename + "_2.bct",
                    filename + "_3.bct", filename + "_4.bct", ]
        command.extend(['--membct', mem_bcts[0], mem_bcts[1],
                        mem_bcts[2], mem_bcts[3]])
        run_command(command)

        if bool(is_cold_boot_mb1_bct) == True:
            blocksize = 512
            if self.tegraparser_values['--pt'] is not None:
                info_print('Getting sector size from pt')
                command = self.exec_file('tegraparser')
                command.extend(['--getsectorsize',
                                self.tegraparser_values['--pt'],
                                'sector_info.bin'])
                run_command(command)

                if os.path.isfile('sector_info.bin'):
                    with open('sector_info.bin', 'rb') as f:
                        blocksize = struct.unpack('<I', f.read(4))[0]
                        info_print(
                            'BlockSize read from layout is 0x%x\n' % blocksize)
                    if blocksize not in [512, 4096]:
                        info_print('invalid block size ')
            command = self.exec_file('tegrahost')
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            command.extend(['--blocksize', str(blocksize)])
            command.extend(['--magicid', "MEMB"])
            command.extend(['--addsigheader_multi', mem_bcts[0],
                            mem_bcts[1], mem_bcts[2], mem_bcts[3]])
            run_command(command)
            os.rename(filename + '_1_sigheader.bct', 'mem_coldboot.bct')
            if values['--encrypt_key'] is not None:
                align_sig_file = 'mem_coldboot_aligned_sigheader.bct'
                shutil.copyfile('mem_coldboot.bct', align_sig_file)
                old_name = align_sig_file
                self.tegrabct_values['--membct_cold_boot'] = self.tegraflash_oem_enc_and_sign_file(
                    align_sig_file, 'MEMB')
                # Need to update pt name since file name is changed
                self.tegraflash_update_pt_name(old_name, self.tegrabct_values['--membct_cold_boot'])
            else:
                self.tegrabct_values['--membct_cold_boot'] = self.tegraflash_oem_sign_file(
                    'mem_coldboot.bct', 'MEMB')
        else:
            chip_info = tegraflash_abs_path(
                self.tegrarcm_values['--chip_info'])
            # Select 1 bct based on RAMCODE

            if os.path.isfile(chip_info):
                ramcode = self.tegraflash_get_ramcode(chip_info)
                os.remove(chip_info)
            else:
                chip_info_bak = tegraflash_abs_path(
                    self.tegrarcm_values['--chip_info'] + '_bak')
                if os.path.exists(chip_info_bak):
                    info_print(
                        "Reading ramcode from backup chip_info.bin file")
                    ramcode = self.tegraflash_get_ramcode(chip_info_bak)
                else:
                    if values['--ramcode'] is None:
                        ramcode = 0
                    else:
                        ramcode = int(values['--ramcode']) >> 2

            info_print("Using ramcode " + str(ramcode))

            if bool(values['--trim_bpmp_dtb']) == False:
                info_print("Disabled BPMP dtb trim, using default dtb")
                info_print("")
            else:
                if "bpmp_fw_dtb" in values['--bins']:
                    self.tegraflash_bpmp_generate_dtb(ramcode)

            if values['--encrypt_key'] is not None:
                shutil.copyfile(mem_bcts[ramcode], 'mem_rcm.bct')
                self.tegrabct_values['--membct_rcm'] = self.tegraflash_oem_enc_and_sign_file(
                    'mem_rcm.bct', 'MEM' +  str(ramcode))
            else:
                shutil.copyfile(mem_bcts[ramcode], 'mem_rcm.bct')
                self.tegrabct_values['--membct_rcm'] = self.tegraflash_oem_sign_file(
                    'mem_rcm.bct', 'MEM' + str(ramcode))

    def tegraflash_update_images(self):
        info_print('Copying signatures')
        command = self.exec_file('tegrahost')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--partitionlayout', self.tegraparser_values['--pt']])
        command.extend(['--updatesig', self.tegrahost_values['--signed_list']])

        if os.path.isfile(self.tegrasign_values['--pubkeyhash']):
            command.extend(
                ['--pubkeyhash', self.tegrasign_values['--pubkeyhash']])

        if os.path.exists(self.tegrasign_values['--getmontgomeryvalues']):
            command.extend(
                ['--setmontgomeryvalues', self.tegrasign_values['--getmontgomeryvalues']])

        if len(values['--key']) == 3:
            command.extend(['--nkeys', '3'])

        run_command(command)

    def tegraflash_generate_devimages(self, cmd_args):

        info_print('Creating storage-device images')
        if values['--output_dir'] is None:
            output_dir = tegraflash_abs_path(
                paths['OUT'] + '/dev_images')
        else:
            output_dir = values['--output_dir']
        if not os.path.exists(output_dir):
            os.makedirs(output_dir)

        dirsep = '/'
        if sys.platform == 'win32' or sys.platform == 'cygwin':
            dirsep = '\\'
        command = self.exec_file('tegraparser')
        command.extend(
            ['--generategpt', '--pt', self.tegraparser_values['--pt']])
        command.extend(['--outputdir', output_dir + dirsep])
        run_command(command)

        command = self.exec_file('tegradevflash')
        command.extend(['--pt', self.tegraparser_values['--pt']])
        command.extend(['--mkdevimages', output_dir + dirsep])
        command.extend(cmd_args)
        run_command(command)

    def tegraflash_burnfuses(self, args, fuse_args):
        values.update(args)

        info_print('Burning fuses. Please check the uart to get the result of fuse burning.')

        if values['--securedev']:
            print('Error: read partition with --securedev not support yet')
            return

        if not self.check_is_mb2applet():
            self.tegraflash_get_key_mode()
            args['--skipuid'] = False
            self.tegraflash_preprocess_configs()
            self.tegraflash_parse_partitionlayout()
            if values['--encrypt_key'] is not None:
                self.tegraflash_enc_and_sign_images()
            else:
                self.tegraflash_sign_images()
            self.tegraflash_generate_bct()
            self.tegraflash_update_images()
            self.tegraflash_send_to_bootrom()
            self.tegraflash_send_to_bootloader(True, False, True)
        info_print('Finish')

    def tegraflash_readfuses(self, args, read_args):
        values.update(args)

        info_print('Reading fuses')

        if values['--securedev']:
            print('Error: read partition with --securedev not support yet')
            return
        if not self.check_is_mb2applet():
            self.tegraflash_get_key_mode()
            args['--skipuid'] = False
            self.tegraflash_preprocess_configs()
            self.tegraflash_parse_partitionlayout()
            if values['--encrypt_key'] is not None:
                self.tegraflash_enc_and_sign_images()
            else:
                self.tegraflash_sign_images()
            self.tegraflash_generate_bct()
            self.tegraflash_update_images()
            self.tegraflash_send_to_bootrom()
            self.tegraflash_boot_mb2_applet()

        if self.check_is_mb2applet():
            filename = tegraflash_abs_path(read_args[0])
            fusexml = read_args[1]
            if os.path.splitext(fusexml)[1] != '.xml':
                raise tegraflash_exception("Not an xml file")
            info_print('Parsing fuse info as per xml file')
            command = self.exec_file('tegraparser')
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            command.extend(['--get_fuse_names', fusexml, self.tegrarcm_values['--get_fuse_names']])
            run_command(command)
            info_print('trying to read fuse with MB2 Applet')
            scatter = '__fuse_read_scatter.bin'
            try:
                f_out = open(filename, 'w')
            except:
                raise tegraflash_exception("Open " + filename + ' failed.')
            with open(self.tegrarcm_values['--get_fuse_names']) as f_fuses:
                for fuse in f_fuses:
                    fuse=fuse.rstrip()
                    command = self.exec_file('tegraparser')
                    command.extend(['--chip', values['--chip'], values['--chip_major']])
                    command.extend(['--read_fusetype', fuse, self.tegrarcm_values['--read_fuse']])
                    run_command(command)
                    command = self.exec_file('tegrarcm')
                    command.extend(['--oem', 'readfuses', scatter, self.tegrarcm_values['--read_fuse']])
                    run_command(command)
                    f_scatter = open(scatter, 'rb')
                    f_bytes = bytearray(f_scatter.read())
                    # For fuses which their sizes are 4 bytes, convert to big endian here.
                    # (ARM is little endian).
                    if len(f_bytes) == 4:
                        tmp = f_bytes[0]
                        f_bytes[0] = f_bytes[3]
                        f_bytes[3] = tmp
                        tmp = f_bytes[1]
                        f_bytes[1] = f_bytes[2]
                        f_bytes[2] = tmp
                    f_string = ''.join(['%02x' % b for b in f_bytes])
                    f_out.write(fuse + ': ' + f_string + '\n')
                    f_scatter.close()
            f_out.close()

        else:
            info_print('Error: mb2 applet is not running\n')

    def tegraflash_get_storage_info(self):
        info_print('Retrieving storage infomation')
        try:
            command = self.exec_file('tegrarcm')
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            command.extend(['--oem', 'platformdetails', 'storage', self.tegrarcm_values['--storage_info']])
            run_command(command)
        except tegraflash_exception as e:
            info_print('Error: failed to get storage info')

    def tegraflash_ccgupdate(self, args, filename1, filename2):
        values.update(args)

        self.tegraflash_preprocess_configs()

        if values['--securedev']:
            info_print('Error: write partition with --securedev not support yet')
            return

        self.tegraflash_get_key_mode()
        args['--skipuid'] = False
        self.tegraflash_parse_partitionlayout()
        self.tegraflash_preprocess_configs()
        if values['--encrypt_key'] is not None:
            self.tegraflash_enc_and_sign_images()
        else:
            self.tegraflash_sign_images()
        self.tegraflash_generate_bct()
        self.tegraflash_update_images()
        self.tegraflash_send_to_bootrom()
        self.tegraflash_send_to_bootloader(True, False)
        self.tegraflash_poll_applet_bl()

        self.tegraflash_ccg_update_fw(filename1, filename2)

    def tegraflash_ccg_update_fw(self, filename1, filename2):
        info_print('Package CCG firmware')
        command = self.exec_file('tegrahost')
        command.extend(['--packageccg', filename1, filename2, 'ccg-fw.bin'])
        run_command(command)

        info_print('Update CCG firmware')
        command = self.exec_file('tegradevflash')
        command.extend(['--ccgupdate', 'ccg-fw.bin'])
        run_command(command)

    def tegraflash_signwrite(self, args, partition_name, filename):
        values.update(args)

        if values['--bl'] is None:
            info_print('Error: Command line bootloader is not specified')
            return 1

        if values['--securedev']:
            info_print('Error: write partition with --securedev not support yet')
            return
        # FIXME: check whether mb2 is running
        self.tegraflash_get_key_mode()
        args['--skipuid'] = False
        self.tegraflash_parse_partitionlayout()
        self.tegraflash_preprocess_configs()
        if values['--encrypt_key'] is not None:
            self.tegraflash_enc_and_sign_images()
        else:
            self.tegraflash_sign_images()
        self.tegraflash_generate_bct()
        self.tegraflash_update_images()
        self.tegraflash_send_to_bootrom()
        self.tegraflash_send_to_bootloader(True, False)
        self.tegraflash_poll_applet_bl()

        partition_type = self.get_partition_partition_type(partition_name.lower())
        magic_id = self.tegraflash_get_magicid(partition_type)

        info_print(partition_name  + ' ' + partition_type + ', magic id = ' + magic_id)
        filename = self.tegraflash_concat_partition(partition_name, filename)

        # Handle special partitions
        if fnmatch.fnmatch(partition_name, '*mb2'):
            if values['--mb2bct_cfg'] == None:
                raise tegraflash_exception('Error: "--mb2bct_cfg" needs to be defined for writing mb2 partition')
            # Need to concatenate mb2-bct to mb2
            mb2_bin = filename
            mb2_bct_file = self.tegraflash_generate_mb2_bct(True)
            # update storage info in mb2bct
            info_print('Updating mb2-bct with storage information')
            command = self.exec_file('tegrabct')
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            command.extend(['--mb2bct', mb2_bct_file])
            command.extend(['--updatestorageinfo', self.tegraparser_values['--pt']])
            run_command(command)
            filename = self.concatenate_mb2bct_mb2(mb2_bin, mb2_bct_file)

        if values['--encrypt_key'] is not None:
            signed_file = self.tegraflash_oem_enc_and_sign_file(filename, magic_id)
        else:
            signed_file = self.tegraflash_oem_sign_file(filename, magic_id)
        self.tegraflash_erase_partition(partition_name)
        self.tegraflash_write_partition('tegradevflash', partition_name, signed_file)

    def tegraflash_concat_partition(self, partition_name, filename):
        cpu_bl_list = ['A_cpu-bootloader', 'B_cpu-bootloader']
        dce_list = ['A_dce-fw', 'B_dce-fw']

        if partition_name in cpu_bl_list:
            # Check if filename has '_with_dtb'? If so, it has been concatenated with dtb.
            if '_with_dtb' in filename:
                return filename
            # If no '_with_dtb', concatenate with dtb file specified in values['--bldtb'].
            if values['--concat_cpubl_bldtb'] is True:
                bl_dtb_file = values['--bldtb']
                # Check if bl_dtb_file has '_overlay'? If so, it has been concatenated with dtbo files.
                if values['--overlay_dtb'] and not '_overlay' in bl_dtb_file:
                    info_print(bl_dtb_file + ' has no overlay_dtb files.')
                    info_print('Concatenating dtbo files to ' + bl_dtb_file)
                    # Concatenate overlay_dtb files to bl_dtb_file
                    values['--bldtb'] = tegraflash_create_backup_file(values['--bldtb'], '_overlay')
                    tegraflash_concat_overlay_dtb()
                    bl_dtb_file = values['--bldtb']
                cpubl_bin_file = filename
                cpubl_with_dtb = os.path.splitext(cpubl_bin_file)[0] + '_with_dtb.bin'
                info_print('Concatenating bl dtb:(' + bl_dtb_file + '), to cpubl binary: ' + cpubl_bin_file)
                if not os.path.exists(bl_dtb_file):
                    raise tegraflash_exception('Could not find ' + bl_dtb_file)
                if not os.path.exists(cpubl_bin_file):
                    raise tegraflash_exception('Could not find ' + cpubl_bin_file)
                shutil.copyfile(cpubl_bin_file, cpubl_with_dtb)
                concat_file(cpubl_with_dtb, bl_dtb_file)  # order: outfile, infile
                filename = cpubl_with_dtb

        elif partition_name in dce_list:
            dce_bin = filename
            kernel_dtb = self.get_dce_base_dtb_filename()
            dce_comb = self.concatenate_dcebin_kerneldtb(dce_bin, kernel_dtb)
            return dce_comb

        return filename

    def tegraflash_write_partition(self, executable, partition_name, filename):
        info_print('Writing partition')
        command = self.exec_file(executable)
        command.extend(['--write', partition_name, filename])
        run_command(command)

    def tegraflash_write(self, args, partition_name, filename):
        values.update(args)

        if values['--bl'] is None:
            info_print('Error: Command line bootloader is not specified')
            return 1

        if values['--securedev']:
            info_print('Error: write partition with --securedev not support yet')
            return
        self.tegraflash_get_key_mode()
        args['--skipuid'] = False
        self.tegraflash_parse_partitionlayout()
        self.tegraflash_preprocess_configs()
        if values['--encrypt_key'] is not None:
            self.tegraflash_enc_and_sign_images()
        else:
            self.tegraflash_sign_images()
        self.tegraflash_generate_bct()
        self.tegraflash_update_images()
        self.tegraflash_send_to_bootrom()
        self.tegraflash_send_to_bootloader(True, False)
        self.tegraflash_poll_applet_bl()

        filename = self.tegraflash_concat_partition(partition_name, filename)
        # Handle special partitions
        if fnmatch.fnmatch(partition_name, '*mb2'):
            if values['--mb2bct_cfg'] == None:
                raise tegraflash_exception('Error: "--mb2bct_cfg" needs to be defined for writing mb2 partition')
            # Need to concatenate mb2-bct:
            mb2_bin = filename
            mb2_bct_file = self.tegraflash_generate_mb2_bct(True)
            # update storage info in mb2bct
            info_print('Updating mb2-bct with storage information')
            command = self.exec_file('tegrabct')
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            command.extend(['--mb2bct', mb2_bct_file])
            command.extend(['--updatestorageinfo', self.tegraparser_values['--pt']])
            run_command(command)
            filename = self.concatenate_mb2bct_mb2(mb2_bin, mb2_bct_file)
        elif partition_name == 'BCT':
            if values['--bct_cold_boot'] is not None:
                info_print("Updating BCT with the BCT specified by tegraflash option")
                filename = values['--bct_cold_boot']
            else:
                info_print("Updating BCT with the BCT generated by tegraflash")
                # Use BCT file accordingly based on option "--boot_chain" with default to A.
                chain = 'A'
                if values['--boot_chain'] is not None:
                    chain = values['--boot_chain']
                if chain in tegrabct_multi_chain.keys():
                    if tegrabct_multi_chain[chain]['bct_file'] is None:
                        self.tegraflash_generate_br_bct_multi_chain(True, False, True)
                    filename = tegrabct_multi_chain[chain]['bct_file']
                else:
                    raise tegraflash_exception('Invalid boot chain %s\n' %s (chain))
                # Writing BCT-boot-chain_backup partition with the generated BCT file if required
                if values['--bct_backup']:
                    self.tegraflash_erase_partition('BCT-boot-chain_backup')
                    self.tegraflash_write_partition('tegradevflash', \
                        'BCT-boot-chain_backup', tegrabct_backup['--image'])

        self.tegraflash_erase_partition(partition_name)
        self.tegraflash_write_partition('tegradevflash', partition_name, filename)

    def tegraflash_erase_partition(self, partition_name):
        info_print('Erasing partition')
        command = self.exec_file('tegradevflash')
        command.extend(['--erase', partition_name])
        run_command(command)

    def tegraflash_erase(self, args, partition_name):
        values.update(args)

        self.tegraflash_preprocess_configs()

        if values['--securedev']:
            info_print('Error: write partition with --securedev not support yet')
            return
        if not self.check_ismb2():
            self.tegraflash_get_key_mode()
            args['--skipuid'] = False
            self.tegraflash_parse_partitionlayout()
            if values['--encrypt_key'] is not None:
                self.tegraflash_enc_and_sign_images()
            else:
                self.tegraflash_sign_images()
            self.tegraflash_generate_bct()
            self.tegraflash_update_images()
            self.tegraflash_send_to_bootrom()
            self.tegraflash_send_to_bootloader(True, False)
            self.tegraflash_poll_applet_bl()

        self.tegraflash_erase_partition(partition_name)

    def tegraflash_read(self, args, partition_name, filename):
        values.update(args)

        self.tegraflash_preprocess_configs()

        if values['--securedev']:
            info_print('Error: read partition with --securedev not support yet')
            return
        if not self.check_ismb2():
            self.tegraflash_get_key_mode()
            args['--skipuid'] = False
            self.tegraflash_parse_partitionlayout()
            if values['--encrypt_key'] is not None:
                self.tegraflash_enc_and_sign_images()
            else:
                self.tegraflash_sign_images()
            self.tegraflash_generate_bct()
            self.tegraflash_update_images()
            self.tegraflash_send_to_bootrom()
            self.tegraflash_send_to_bootloader(True, False)
            self.tegraflash_poll_applet_bl()
        self.tegraflash_read_partition('tegradevflash', partition_name, filename)

    def tegraflash_read_partition(self, executable, partition_name, filename):
        info_print('Reading partition')
        command = self.exec_file(executable)
        command.extend(['--read', partition_name, filename])
        run_command(command)

    def tegraflash_dump(self, args, dump_args):
        values.update(args)

        if not self.check_is_mb2applet():
            self.tegraflash_get_key_mode()
            args['--skipuid'] = False
            self.tegraflash_preprocess_configs()
            self.tegraflash_parse_partitionlayout()
            if values['--encrypt_key'] is not None:
                self.tegraflash_enc_and_sign_images()
            else:
                self.tegraflash_sign_images()
            self.tegraflash_generate_bct()
            self.tegraflash_update_images()
            self.tegraflash_send_to_bootrom()

            self.tegraflash_boot_mb2_applet()

        if self.check_is_mb2applet():
            if dump_args[0] == 'eeprom':
                self.tegraflash_dumpeeprom(args, dump_args[1:])
            elif dump_args[0] == 'custinfo':
                self.tegraflash_dumpcustinfo(dump_args[1:])
            else:
                 raise tegraflash_exception(dump_args[0] + " is not supported")
        else:
            info_print('Error: mb2 applet is not running\n')

    def tegraflash_dumpcustinfo(self, dump_args):
        info_print('Dumping customer Info')
        command = self.exec_file('tegrarcm')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--oem', 'dump', 'bct', 'tmp.bct'])
        run_command(command)

        command = self.exec_file('tegrabct')
        command.extend(['--brbct', 'tmp.bct'])
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        if len(dump_args) > 0:
            file_path = tegraflash_abs_path(dump_args[0])
        else:
            file_path = tegraflash_abs_path("custinfo.bin")

        command.extend(['--custinfo', file_path])
        run_command(command)

    def tegraflash_dumpeeprom(self, args, params):
        values.update(args)

        if len(params) == 0:
            info_print("Error: EEPROM module not specified")
            return

        command = self.exec_file('tegrarcm')
        self.tegraflash_fetch_chip_info()
        info_print('Retrieving EEPROM data')
        out_file = tegraflash_abs_path(self.tegrarcm_values['--board_info'])
        eeprom_module = params[0]
        if len(params) > 1:
            out_file = tegraflash_abs_path(params[1])
        command.extend(['--oem', 'platformdetails', 'eeprom', eeprom_module.lower(), out_file])
        try:
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            run_command(command)
        except tegraflash_exception as e:
            command[0] = self.exec_file('tegradevflash')[0]
            run_command(command)

    def tegraflash_sign_br_bct(self):
        self.tegraflash_get_key_mode()
        self.tegraflash_parse_partitionlayout()
        self.tegraflash_sign_images()
        self.tegraflash_generate_br_bct_multi_chain(True, False, True)
        self.tegraflash_update_images()
        return

    def tegraflash_sign_binary(self, exports, args=None):
        values.update(exports)
        self.tegraflash_get_key_mode()
        partition_type = "data"

        # Get partition type if it exists
        if len(args) >= 2:
            partition_type = args[1]

        # Handle signing BCT in special way as BCT needs to be generated dyanmically
        if partition_type == "BCT":
            self.tegraflash_sign_br_bct()
            copy_br_bct_multi_chain(paths['WD'])
        else:
            # Handle signing CPUBL in special way as cpubl needs to concatenate dtb at first
            if partition_type == "bootloader_stage2":
                args[0] = self.tegraflash_concat_partition(args[2], args[0])
            magic_id = self.tegraflash_get_magicid(partition_type)
            binary = tegraflash_abs_path(args[0])
            binary_base = os.path.basename(binary)
            tegraflash_symlink(binary, binary_base)
            binary = self.tegraflash_oem_sign_file(binary_base, magic_id)

            info_print('Copying ' + binary + ' to ' +  paths['WD'])
            if not shutil._samefile(binary, paths['WD'] + "/" + binary):
                shutil.copyfile(binary,  paths['WD'] + "/" + binary)
            info_print("Signed file: " + paths['WD'] + "/" + binary)
        return

    def tegraflash_encrypt_sign_binary(self, exports, args):
        values.update(exports)
        partition_type = "data"
        magicid = ""

        info_print('Generating signature')
        file_path = tegraflash_abs_path(args[0])
        if len(args) >= 2:
            partition_type = args[1]

        if partition_type == "BCT":
            # TODO: will fix when flash.sh with -k option works with SBKPKC
            info_print("Encryption of br_bct is not supported")
            return

        self.tegraflash_get_key_mode()

        if not self._is_header_present(file_path):
            # Handle signing CPUBL in special way as cpubl needs to concatenate dtb at first
            if partition_type == "bootloader_stage2":
                file_path = self.tegraflash_concat_partition(args[2], file_path)
            magicid = self.tegraflash_get_magicid(partition_type)
            temp_file = os.path.basename(file_path)
            i = 1
            while os.path.exists(str(i) + "_" + temp_file):
                i = i + 1
            temp_file = str(i) + "_" + temp_file
            tegraflash_symlink(file_path, temp_file)
            if values['--encrypt_key'] is not None:
                info_print('Encrypting file')
                filename = self.tegraflash_oem_enc_and_sign_file(temp_file, magicid)
            else:
                filename = self.tegraflash_oem_sign_file(temp_file, magicid)
            temp = filename.split("_", 1)
            new_filename = temp[1]
            if os.path.exists(new_filename):
                os.remove(new_filename)
            tegraflash_symlink(filename, new_filename)
            out_file = paths['WD'] + "/" + new_filename
            if not os.path.isfile(out_file) or not os.path.samefile(new_filename, out_file):
                shutil.copyfile(new_filename, paths['WD'] + "/" + new_filename)
            if values['--encrypt_key'] is not None:
                info_print("Signed and encrypted file: " + paths['WD'] + "/" + new_filename)
            else:
                info_print("Signed file: " + paths['WD'] + "/" + new_filename)

    def tegraflash_copy_signed_binaries(self, xml_file, output_dir):
        signed_files = [ ]
        with open(xml_file, 'rt') as file:
            xml_tree = ElementTree.parse(file)

        mode = xml_tree.getroot().get('mode')
        if mode == "pkc":
            list_text = "signed_file"
        else:
            if mode == "ec" or mode == "eddsa":
                list_text = "signed_file"
            else:
                list_text = "encrypt_file"

        for file_nodes in xml_tree.iter('file'):
            file_name = file_nodes.get('name')
            signed_file = file_nodes.find(mode).get(list_text)
            shutil.copyfile(signed_file, output_dir + "/" + os.path.basename(signed_file))
            if int(values['--chip'], 0) != 0x21:
                file_name = file_name.replace('_encrypt', '')
                file_name = file_name.replace('_sigheader', '')
                file_name = file_name.replace('_wbheader', '')
                file_name = file_name.replace('_aligned', '')
                file_name = file_name.replace('_blob_w_bin', '')
            signed_files.extend([file_name, signed_file])

        return signed_files

    def tegraflash_sign(self, exports, args=None):
        values.update(exports)
        signed_files = []

        self.tegraflash_get_key_mode()

        if values['--encrypt_key'] is None:
            output_dir = tegraflash_abs_path('signed')
        else:
            output_dir = tegraflash_abs_path('enc_signed')
        # Create signed directory. If exists, clear all files
        if os.path.exists(output_dir):
            shutil.rmtree(output_dir, ignore_errors=True)
        os.makedirs(output_dir)

        images_to_sign = ['dce_fw', 'mts_mce',
                'mb2_bootloader', 'fusebypass', 'bootloader_dtb', 'spe_fw', 'bpmp_fw',
                'bpmp_fw_dtb', 'psc_fw', 'tos', 'eks', 'sce_fw', 'ape_fw', 'tsec_fw', 'nvdec',
                'mb2_applet', 'xusb_fw', 'rce_fw', 'fsi_fw', 'bpmp_ist', 'ccplex_ist', 'ist_ucode']
        binaries = []

        if values['--cfg'] is not None :
            self.tegraflash_parse_partitionlayout()
            if values['--encrypt_key'] is None:
                self.tegraflash_sign_images()
            else:
                self.tegraflash_enc_and_sign_images()
            self.tegraflash_generate_bct()
            self.tegraflash_update_images()
            # generate gpt and mbr
            command = self.exec_file('tegraparser')
            command.extend(['--generategpt', '--pt', self.tegraparser_values['--pt']])
            run_command(command)

            patt = re.compile(".*(mbr|gpt).*\.bin")
            contents = os.listdir('.')
            for f in contents:
                if patt.match(f):
                    shutil.copyfile(f, output_dir + "/" + f)

        # Sign files listed in --bins
        if values['--bins'] is not None and not values['--external_device']:
            bins = values['--bins'].split(';')
            for binary in bins:
                binary = binary.strip(' ')
                binary = binary.replace('  ', ' ')
                tags = binary.split(' ')
                if (len(tags) < 2):
                    raise tegraflash_exception('invalid format ' + binary)

                if tags[0] in images_to_sign:
                    if tags[0] == 'mb2_bootloader':
                        mb2_bct_file = self.tegraflash_generate_mb2_bct(False)
                        info_print('Updating mb2-bct with storage information for RCM')
                        command = self.exec_file('tegrabct')
                        command.extend(['--chip', values['--chip'], values['--chip_major']])
                        command.extend(['--mb2bct', mb2_bct_file])
                        command.extend(['--updatestorageinfo', self.tegraparser_values['--pt']])
                        run_command(command)
                        mb2comb_file = self.concatenate_mb2bct_mb2(tags[1], mb2_bct_file)
                        tags[1] = mb2comb_file
                    if tags[0] == 'bpmp_fw_dtb' and self.bpmpdtbodm:
                        info_print('Using bpmp-dtb concatenated with odmdata')
                        tags[1] = self.bpmpdtbodm

                    magic_id = self.tegraflash_get_magicid(tags[0])
                    if values['--encrypt_key'] is None:
                        tags[1] = self.tegraflash_oem_sign_file(tags[1], magic_id)
                    else:
                        tags[1] = self.tegraflash_oem_enc_and_sign_file(tags[1], magic_id)
                    binaries.extend([tags[1]])

        if values['--tegraflash_v2'] and values['--bl']:
            values['--bl'] = self.tegraflash_concat_partition('A_cpu-bootloader', values['--cpubl'])
            values['--bl'] = self.tegraflash_oem_sign_file(values['--bl'], 'CPBL')
            binaries.extend([values['--bl']])

        if values['--cfg'] is not None :
            info_print("Copying enc\/signed file in " + output_dir)
            signed_files.extend(self.tegraflash_copy_signed_binaries(self.tegrahost_values['--signed_list'], output_dir))
            if self.tegrabct_values['--bct'] is None and not values['--external_device']:
                raise tegraflash_exception("Unable to find bct file")
            copy_br_bct_multi_chain(output_dir)
            self.tegraflash_update_cfg_file(signed_files, values['--cfg'], output_dir, 0)

        if self.tegrabct_values['--mb1_bct'] is not None:
            shutil.copyfile(self.tegrabct_values['--mb1_bct'], output_dir + "/" + self.tegrabct_values['--mb1_bct'])
        if self.tegrabct_values['--mb1_cold_boot_bct'] is not None:
            shutil.copyfile(self.tegrabct_values['--mb1_cold_boot_bct'], output_dir + "/" + self.tegrabct_values['--mb1_cold_boot_bct'])

        file_list = [values['--mb2_bct'], values['--mb2_cold_boot_bct'], self.tegrabct_values['--membct_rcm'], self.tegrabct_values['--membct_cold_boot']]
        for _file in file_list:
            if _file is not None and os.path.isfile(_file):
                shutil.copyfile(_file, output_dir + "/" + _file)

        if binaries == [] and signed_files == []:
            info_print('No file was signed. Please check arugments')

        for signed_binary in binaries:
            info_print('Copying ' + signed_binary + ' to ' + output_dir)
            shutil.copyfile(signed_binary, output_dir + "/" + signed_binary)
            if values['--encrypt_key'] is None:
                info_print("Signed file: " + output_dir + "/" + signed_binary)
            else:
                info_print("Signed and encrypted file: " + output_dir + "/" + signed_binary)

        if self.tegraparser_values['--pt'] is not None:
            shutil.copyfile(self.tegraparser_values['--pt'], output_dir + "/" + self.tegraparser_values['--pt'])

        if values['--cfg'] is not None:
            # generate flashing index file
            # --pt flash.xml.bin --generateflashindex flash.xml.tmp <out>
            flash_index = "flash.idx"
            tegraflash_generate_index_file(output_dir + "/" + os.path.basename(values['--cfg']), flash_index, self.tegraparser_values['--pt'])
            shutil.copyfile(flash_index, output_dir + "/" + flash_index)

    def tegraflash_encrypt_and_sign(self, exports, args=None):
        # call tegraflash_sign(), but implement encryption handling inside tegraflash_sign()
        self.tegraflash_sign(exports)

    def tegraflash_update_cfg_file(self, signed_files, cfg_file, output_dir, only_generated=False):
        secondary_gpt_found = None;
        signed_files = dict(zip(signed_files[::2], signed_files[1::2]))
        with open(cfg_file, 'r') as file:
            xml_tree = ElementTree.parse(file)

        root = xml_tree.getroot()

        for node in root.findall('.//partition'):
            file_node = node.find('filename')
            part_type = node.attrib.get('type').strip()
            part_name = node.attrib.get('name').strip()
            if file_node is not None and file_node.text is not None and not only_generated:
                file_name = file_node.text.strip()
                if node.get('authentication_group') is not None:
                    if node.get('authentication_group') == node.get('id'):
                        file_name = file_name.replace('.bin','_multisigheader.bin')
                        file_name = file_name.replace('.dtb','_multisigheader.dtb')
                        file_name = file_name.replace('.img','_multisigheader.img')
                    else:
                        file_name = file_name.replace('.bin','_nosigheader.bin')
                        file_name = file_name.replace('.dtb','_nosigheader.dtb')
                        file_name = file_name.replace('.img','_nosigheader.img')
                if (file_name in signed_files and node.get('oem_sign') == "true") \
                        or part_type == "mb1_bootloader" or part_type == "psc_bl1" \
                        or part_type == "wb0" or (part_type == "WB0"):
                    file_node.text = " " + signed_files[file_name] + " "
            else:
                # add filename for partitions that have been created and signed
                file_name = None
                if part_name == "BCT":
                    # Write BCT according to the specified boot chain.
                    # If boot chain is not set, write BCT for defaut boot chain A
                    chain = 'A'
                    if values['--boot_chain'] is not None:
                        chain = values['--boot_chain']
                    if chain in tegrabct_multi_chain.keys():
                        file_name = tegrabct_multi_chain[chain]['bct_file']
                    else:
                        raise tegraflash_exception('Invalid boot chain %s\n' %s (chain))

                if part_name == "BCT-boot-chain_backup":
                    file_name = tegrabct_backup['--image']

                if part_name == "MB1_BCT" or part_name == "MB1_BCT_b" or part_name == "A_MB1_BCT" or part_name == "B_MB1_BCT":
                    file_name = self.tegrabct_values['--mb1_cold_boot_bct'];

                if part_name == "MEM_BCT" or part_name == "MEM_BCT_b" or part_name == "A_MEM_BCT" or part_name == "B_MEM_BCT":
                    file_name = self.tegrabct_values['--membct_cold_boot']

                if part_name == "secondary_gpt" \
                        or part_name == "master_boot_record" \
                        or part_name == "primary_gpt" \
                        or part_name == "secondary_gpt_backup" :
                    for device in root.findall('.//device'):
                        idx = device.attrib.get('type').strip() + '_' + \
                            device.attrib.get('instance').strip() + '_' + part_name
                        if idx in self.tegraflash_gpt_image_name_map.keys():
                            file_name = self.tegraflash_gpt_image_name_map[idx]
                        else:
                            continue

                        # The secondary_gpt parttion exists on both boot device and user device.
                        # For the secondary_gpt partition on boot device, it needs to break out
                        # the circle once its file name is set.
                        if part_name == "secondary_gpt" and secondary_gpt_found is None:
                            secondary_gpt_found = "true"
                            break

                if file_name is not None:
                    new_tag = ElementTree.SubElement(node, 'filename')
                    new_tag.text = " " + file_name + " "

        with open (output_dir + "/" + os.path.basename(cfg_file), 'wb+') as file:
            file.write(ElementTree.tostring(root))

    def tegraflash_bpmp_generate_dtb(self, ramcode):
        info_print("Generating BPMP dtb for ramcode - " + str(ramcode))
        bpmp_bin_key = "bpmp_fw_dtb"
        bins = values['--bins'].split(';')
        for binary in bins:
            binary = binary.strip(' ')
            binary = binary.replace('  ', ' ')
            tags = binary.split(' ')
            if tags[0] == bpmp_bin_key:
                bpmp_dtb = tags[1]
                break
        if bpmp_dtb is None:
            raise tegraflash_exception('BPMP-FW DTB not found in --bins')

        bpmp_dtb_path = tegraflash_abs_path(bpmp_dtb)
        if not os.path.exists(bpmp_dtb_path):
            info_print("Invalid BPMP DTB location - " + bpmp_dtb_path)
            info_print("")
            return

        bpmp_dtb_dir = os.path.dirname(bpmp_dtb_path)
        test_dts = self.tegraflash_bpmp_generate_int_dtsi(
            bpmp_dtb_dir, bpmp_dtb_path)
        if (test_dts == ""):
            info_print("Using existing bpmp_dtb - " + bpmp_dtb)
            info_print("")
            return

        lines = []
        strap_ids_ph_str = []
        lines, strap_ids_ph_str, strap_id_line_num = self.tegraflash_bpmp_get_strap_handles(
            ramcode, test_dts)
        if (lines == -1 or strap_ids_ph_str == -1 or strap_id_line_num == -1):
            info_print("Using existing bpmp_dtb - " + bpmp_dtb)
            info_print("")
            os.remove(test_dts)
            return

        dtb_start_pos, dtb_end_pos = self.tegraflash_bpmp_save_table_pos(lines)
        if (dtb_start_pos == "-1" or dtb_end_pos == "-1"):
            info_print("Using existing bpmp_dtb " + bpmp_dtb)
            info_print("")
            os.remove(test_dts)
            return

        valid_entry = []
        valid_entry = self.tegraflash_bpmp_update_valid_entries(
            ramcode, lines, dtb_start_pos, dtb_end_pos, strap_ids_ph_str)

        lines = self.tegraflash_bpmp_remove_unused_phandles(
            ramcode, lines, test_dts, valid_entry, strap_id_line_num, strap_ids_ph_str, dtb_start_pos, dtb_end_pos)
        if (lines == "-1"):
            os.remove(test_dts)
            info_print("Using existing bpmp_dtb " + bpmp_dtb)
            info_print("")
            return

        bpmp_dtb_size = os.stat(bpmp_dtb_path)
        ret = self.tegraflash_bpmp_dtsi_to_dtb(bpmp_dtb_path, test_dts)
        if not ret:
            info_print("Using existing bpmp_dtb " + bpmp_dtb)
            info_print("")
            return

        # Clean-up
        new_dtb_size = os.path.getsize(bpmp_dtb_path)
        info_print("Old BPMP dtb size - " +
                   str(bpmp_dtb_size.st_size) + " bytes")
        info_print("New BPMP dtb size - " + str(new_dtb_size) + " bytes")
        os.remove(test_dts)
        info_print('')

    # Convert the DTB to DTSI
    def tegraflash_bpmp_generate_int_dtsi(self, bpmp_dtb_dir, bpmp_dtb_path):
        test_dts = bpmp_dtb_dir + "/test.dts"
        if os.path.exists(test_dts):
            os.remove(test_dts)
        command = self.exec_file('dtc')
        command.extend(['-I', 'dtb', bpmp_dtb_path, "-o", test_dts])
        run_command(command, False)
        if not os.path.exists(test_dts):
            info_print("dtc command Failed to create dtsi file from dtb")
            return ""
        return test_dts

    # Convert BPMP DTSI to BPMP DTB
    def tegraflash_bpmp_dtsi_to_dtb(self, bpmp_dtb_path, test_dts):
        if not os.path.exists(test_dts):
            info_print("test dts not present")
            return False
        # Modify file permissions to regenerate the dtb
        os.chmod(bpmp_dtb_path, 0o755)
        try:
            command = self.exec_file('dtc')
            command.extend(['-I', 'dts', '-O', 'dtb', '-f',
                            test_dts, '-o', bpmp_dtb_path])
            run_command(command, False)
            # Set default permissions -rw-rw-r--
            os.chmod(bpmp_dtb_path, 0o664)
        except Exception as _:
            info_print("dtc failed to convert dtsi to dtb")
            return False
        return True

    # Convert the DTB to DTS
    def tegraflash_dtb_to_dts(self, dtb_file):
        dts_file = os.path.splitext(dtb_file)[0] + '.dts'

        if os.path.exists(dts_file):
            os.remove(dts_file)
        command = ['dtc']
        command.extend(['-I', 'dtb', '-O', 'dts', '-f', dtb_file, '-o', dts_file])
        run_command(command, False)
        if not os.path.exists(dts_file):
            raise tegraflash_exception("dtc command Failed to create dts: " + dts_file)
        return dts_file

    def tegraflash_bpmp_remove_unused_phandles(self, ramcode, lines, test_dts, valid_entry,
                                               strap_id_line_num, strap_ids_ph_str, dtb_start_pos,
                                               dtb_end_pos):
        if not os.path.exists(test_dts):
            info_print("dtsi file not present - " + test_dts)
            return False
        # Remove unused strap id phandles
        for line_num in range(0, len(lines)):
            if line_num == strap_id_line_num:
                for phandle in strap_ids_ph_str:
                    if phandle != strap_ids_ph_str[ramcode]:
                        lines[line_num] = re.sub(
                            phandle, '0x0', lines[line_num])
        invalid = False
        try:
            with open(test_dts, "w") as fp:
                for line_num in range(0, len(lines)):
                    for i in range(0, len(dtb_start_pos)):
                        if ((line_num >= dtb_start_pos[i]) and
                            (line_num <= dtb_end_pos[i]) and
                                not valid_entry[i]):
                            invalid = True
                            break
                        else:
                            invalid = False
                    if invalid is False:
                        fp.write(lines[line_num])
        except Exception as _:
            info_print("Could not open dts in write mode - " + test_dts)
            return False
        return lines

    # Saves line position for all external-memory-* entries in bpmp dts file
    def tegraflash_bpmp_save_table_pos(self, lines):
        dtb_start_pos = []
        dtb_end_pos = []
        count = -1

        # DTB syntax uses matching brackets. Leveraging the same to bound the
        # necessary start and end positions strap entries.
        # Example -
        #   external-memory-0 {         -------> start pos for the strap id 0
        #       compatible = "nvidia,t19x-emc-table";
        #       phandle = <0xa>;
        #       ...
        #       foo1 {  ---> neglected(sub node)
        #       ...
        #       };      ---> neglected(sub node)
        #   };                          -------> end pos for the strap id 0
        for i in range(0, len(lines)):
            if "external-memory-" in lines[i]:
                dtb_start_pos.append(i)
                count = count + 1
                continue

            if "{" in lines[i]:
                count = count + 1
            if "}" in lines[i]:
                count = count - 1

            # start pos and end pos are a set of pairs. If the start pos is not
            # detected in the dts file, end pos detected should not be saved.
            if count == 0 and (len(dtb_start_pos) + len(dtb_end_pos)) % 2 == 1:
                dtb_end_pos.append(i + 1)

        if (len(dtb_start_pos) == 0 or
            len(dtb_end_pos) == 0 or
                (len(dtb_start_pos) != len(dtb_end_pos))):
            return -1, -1

        return dtb_start_pos, dtb_end_pos

    # Check for phandle from each of the dtb_start_pos and delete position if
    # phandle is correct.
    def tegraflash_bpmp_update_valid_entries(self, ramcode, lines, dtb_start_pos,
                                             dtb_end_pos, strap_ids_ph_str):
        valid_entry = []
        for i in range(0, len(dtb_start_pos)):
            for line_num in range(dtb_start_pos[i], dtb_end_pos[i]):
                if "phandle" in lines[line_num] and not "linux" in lines[line_num]:
                    cur_strap_id = re.search(
                        '<(.+?)>', lines[line_num]).group(1)
                    if cur_strap_id == strap_ids_ph_str[ramcode]:
                        valid_entry.append(True)
                    else:
                        valid_entry.append(False)
        return valid_entry

    # Get the strap phandle entries
    def tegraflash_bpmp_get_strap_handles(self, ramcode, test_dts):
        lines = []
        strap_ids_ph_str = []
        strap_id_line_num = 0

        if os.path.exists(test_dts):
            with open(test_dts, "r") as fp:
                lines = fp.readlines()
        else:
            info_print("Test dts not present - " + test_dts)
            return -1

        for i in range(0, len(lines)):
            if "emc-strap" in lines[i]:
                line = lines[i + 1]
                strap_id_line_num = i + 1
                # Strip to get the available strap-ids
                #   emc-strap {
                #       select = <0xa 0x0 0x0 0x0>;
                #   };
                strap_ids_ph_str = re.search('<(.+?)>', line).group(1)
                strap_ids_ph_str = strap_ids_ph_str.split(' ')
                break

        if len(strap_ids_ph_str) == 0:
            info_print("No emc strap-id entries present in BPMP dtb")
            os.remove(test_dts)
            return -1

        if ramcode > len(strap_ids_ph_str):
            info_print("BPMP FW DTB does not contain emc-strap " +
                       str(ramcode) + " data")

        return lines, strap_ids_ph_str, strap_id_line_num

    def tegraflash_get_ramcode(self, chip_info):
        with open(chip_info, 'rb') as f:
            # RAMCODE shall be the last 4 bytes of fuses.bin
            f.seek(52, 0)
            ramcode = struct.unpack('<I',  f.read(4))[0]
            info_print('RAMCODE Read from Device: %x\n' % ramcode)
            ramcode = ramcode & 0xC
            ramcode = ramcode >> 2
        return ramcode

    """ Other helper methods """

    def tegraflash_get_key_mode(self):
        self.call_tegrasign(None, 'mode.txt', None, values['--key'], None, None, None, None, None, None)

        with open('mode.txt') as mode_file:
            self.tegrasign_values['--mode'] = mode_file.read()

    def concatenate_cpubl_bldtb(self):
        info_print('Concatenating bl dtb to cpubl binary')
        bl_dtb_file = values['--bldtb']
        cpubl_bin_file = values['--cpubl']
        cpubl_with_dtb = os.path.splitext(cpubl_bin_file)[0] + '_with_dtb.bin'
        if not os.path.exists(bl_dtb_file):
            raise tegraflash_exception('Could not find ' + bl_dtb_file)
        if not os.path.exists(cpubl_bin_file):
            raise tegraflash_exception('Could not find ' + cpubl_bin_file)
        shutil.copyfile(cpubl_bin_file, cpubl_with_dtb)
        concat_file(cpubl_with_dtb, bl_dtb_file)  # order: outfile, infile
        values['--cpubl'] = cpubl_with_dtb

    def get_dcebin_filename(self):
        dce_bin_file = None
        with open(values['--cfg'], 'r') as file:
            xml_tree = ElementTree.parse(file)
        root = xml_tree.getroot()
        for node in root.iter('partition'):
            if(node.get('type') == "dce_fw"):
                dce_node = node.find('filename')
                if dce_node is not None and dce_node.text is not None:
                    dce_bin_file = dce_node.text.strip()
                break
        if dce_bin_file is not None:
            info_print("DCE binary: " + dce_bin_file)
        return dce_bin_file

    def get_dce_base_dtb_filename(self):
        if values['--dce_base_dtb'] == None:
            # Search order and note for each
            # values['--rawkerneldtb'] => this is dtb filed specified in kernel-dtb partition
            # values['--kerneldtb'] => this is passed to kernel, BL should only modify its content
            # values['--bld'] => this is used by BL to setup hw
            kernel_dtb_file = values['--rawkerneldtb']
            if kernel_dtb_file is None:
                kernel_dtb_file = values['--kerneldtb']
                if kernel_dtb_file == None:
                    kernel_dtb_file = values['--bldtb']
            info_print("Kernel DTB used: " + str(kernel_dtb_file))
        else:
            kernel_dtb_file = values['--dce_base_dtb']
            info_print("Use DCE base DTB as Kernel DTB: " + kernel_dtb_file)

        return kernel_dtb_file

    def update_dcecomb_filename(self, dcecomb_bin):
        with open(values['--cfg'], 'r+') as file:
            xml_tree = ElementTree.parse(file)
            root = xml_tree.getroot()
            for node in root.iter('partition'):
                if(node.get('type') == "dce_fw"):
                    dce_node = node.find('filename')
                    # in case blank, leave it alone
                    if dce_node != None and dce_node.text != None:
                        dce_node.text = dcecomb_bin

            xml_tree.write(values['--cfg'])
        return

    def concatenate_dcebin_kerneldtb(self, dce_bin, kernel_dtb):
        if dce_bin is None or kernel_dtb is None or dce_bin == '' or kernel_dtb == '':
            # There is no dce binary to concatenate. Just return.
            return None

        # if --dce_overlay_dtb is defined, then we concatenate dce_base_dtb with fdtoverlay(overlay dtbs)
        # else we default to dce_base_dtb. This can be either explicitly defined or default to kernel dtb

        if values['--dce_overlay_dtb'] != None:
            info_print('Concatenating %s to fdtoverlay(%s)' %(kernel_dtb, values['--dce_overlay_dtb']))
            dtb_files = values['--dce_overlay_dtb'].replace(',', ' ')
            file_name, file_ext = os.path.splitext(kernel_dtb)
            kernel_dce_overlay_dtb = file_name + '_with_dce_overlay' + file_ext
            # fdtoverlay -i <Base device tree>.dtb -o <Output device tree>.dtb [space separated list of overlay device tree Blobs (dtbs)]
            try:
                command = ["fdtoverlay"]
                command.extend(["-i", kernel_dtb])
                command.extend(["-o", kernel_dce_overlay_dtb])
                command.extend([dtb_files])
                run_command(command, False)
                kernel_dtb = kernel_dce_overlay_dtb
            except Exception as e:
                raise tegraflash_exception('Unexpected error in creating: ' + kernel_dce_overlay_dtb + ' ' + str(e))

        info_print('Concatenating kernel-dtb to dce-fw binary')

        if not os.path.exists(dce_bin):
            raise tegraflash_exception('Could not find dce_bin: ' + dce_bin)
        if not os.path.exists(kernel_dtb):
            raise tegraflash_exception('Could not find kernel_dtb: ' + kernel_dtb)
        dce_with_dtb = os.path.splitext(dce_bin)[0] + '_with_' + os.path.splitext(kernel_dtb)[0] + '.bin'
        info_print('dce_bin = ' + dce_bin)
        info_print('kernel_dtb = ' +  kernel_dtb)
        info_print('dce_with_dtb = ' +  dce_with_dtb)

        shutil.copyfile(dce_bin, dce_with_dtb)
        concat_file(dce_with_dtb, kernel_dtb)  # order: outfile, infile
        return dce_with_dtb

    def get_mb2bin_filename(self):
        mb2_bin_file = None
        with open(values['--cfg'], 'r') as file:
            xml_tree = ElementTree.parse(file)
        root = xml_tree.getroot()
        for node in root.iter('partition'):
            if(node.get('type') == "mb2_bootloader"):
                mb2_node = node.find('filename')
                mb2_bin_file = mb2_node.text.strip()
                info_print("MB2 binary: " + mb2_bin_file)
                break
        return mb2_bin_file

    def update_mb2comb_filename(self, mb2comb_bin):
        with open(values['--cfg'], 'r+') as file:
            xml_tree = ElementTree.parse(file)
            root = xml_tree.getroot()
            for node in root.iter('partition'):
                if(node.get('type') == "mb2_bootloader"):
                    mb2_node = node.find('filename')
                    # in case blank, leave it alone
                    if mb2_node !=None and mb2_node.text != None:
                        mb2_node.text = mb2comb_bin

            xml_tree.write(values['--cfg'])
        return

    def concatenate_mb2bct_mb2(self, mb2_bin_file, mb2_bct_file):
        info_print('Concatenating mb2-bct to mb2 binary')

        info_print('mb2_bin_file = ' + mb2_bin_file)
        info_print('mb2_bct_file = ' + mb2_bct_file)
        mb2_with_bct = os.path.splitext(mb2_bin_file)[0] + '_with_' + os.path.splitext(mb2_bct_file)[0] + '.bin'
        if not os.path.exists(mb2_bct_file):
            raise tegraflash_exception('Could not find ' + mb2_bct_file)
        if not os.path.exists(mb2_bin_file):
            raise tegraflash_exception('Could not find ' + mb2_bin_file)
        shutil.copyfile(mb2_bin_file, mb2_with_bct)
        concat_file(mb2_with_bct, mb2_bct_file)  # order: outfile, infile
        return mb2_with_bct

    def tegraflash_generate_mb2_bct(self, is_cold_boot_mb2_bct):
        # Gather all the configs in a list
        configs_list = ['--mb2bct_cfg', '--scr_config', '--scr_cold_boot_config']

        for config in configs_list:
            if values[config] is not None:
                config_types = [".dts"]
                if any(cf_type in values[config] for cf_type in config_types):
                    info_print( 'Pre-processing mb2bct config: ' + values[config])
                    values[config] = self.run_cpp_tool( values[config])
                    values[config] = self.run_dtc_tool( values[config])

        command = self.exec_file('tegrabct')
        command.extend(['--chip', values['--chip'], values['--chip_major']])

        if bool(is_cold_boot_mb2_bct) == True:
            info_print('Generating coldboot mb2-bct')
            if values['--mb2_cold_boot_bct'] is None:
                values['--mb2_cold_boot_bct'] = 'mb2_cold_boot_bct.cfg'
            temp = values['--mb2_cold_boot_bct']
        else:
            info_print('Generating recovery mb2-bct')
            if values['--mb2_bct'] is None:
                values['--mb2_bct'] = 'mb2_bct.cfg'
            temp = values['--mb2_bct']

        command.extend(['--mb2bct', temp])
        if bool(is_cold_boot_mb2_bct) == False:
            command.extend(['--recov'])
        command.extend(['--mb2bctcfg', values['--mb2bct_cfg']])

        tmp = None
        if values['--scr_config'] is not None:
            tmp = values['--scr_config']
        if bool(is_cold_boot_mb2_bct) == True:
            if values['--scr_cold_boot_config'] is not None:
                tmp = values['--scr_cold_boot_config']
        if tmp is not None:
            command.extend(['--scr', tmp])

        run_command(command)

        # return mb2bct binary name
        return os.path.splitext(temp)[0] + '_MB2.bct'

    def get_file_name_from_images_list(self, bin_type):
        bin_file = None
        try:
            if values['--key'][0] == 'None':
                algo_type = 'sbk'
                algo_file = 'encrypt_file'
            else:
                algo_type = 'pkc' # Use this is OK for all algo
                algo_file = 'signed_file'
        except Exception as e:
                algo_type = 'sbk'
                algo_file = 'encrypt_file'
        with open(self.tegrahost_values['--signed_list'], 'r') as file:
            xml_tree = ElementTree.parse(file)
        root = xml_tree.getroot()
        for file_node in root.findall('file'):
            if (file_node.get('type') == bin_type):
                for node in file_node.iter(algo_type):
                    bin_file = node.get(algo_file)
                    if bin_file is not None:
                        bin_file = bin_file.strip()
                    break
        return bin_file

    def call_tegrasign(self, file_val, getmode, getmont, key,
                       length, list_val, offset, pubkeyhash, sha, skip_enc,
                       verbose=False, iv=0, aad=0, tag=0, sign=None,
                       verify=0, kdf=None, hsm=None):

        tegrasign(file_val, getmode, getmont, key, length,
                  list_val, offset, pubkeyhash, sha, skip_enc,
                  verbose, iv, aad, tag, sign, verify, kdf, hsm)

    def exec_file(self, name):
        bin_name = self.tegraflash_binaries_v2[name]
        if sys.platform == 'win32' or sys.platform == 'cygwin':
            bin_name = bin_name + '.exe'

        use_shell = False
        if sys.platform == 'win32':
            use_shell = True
        try:
            subprocess.Popen([bin_name], stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, shell=use_shell, env=cmd_environ)
        except OSError:
            raise tegraflash_exception('Could not find ' + bin_name)

        supports_instance = ['tegrarcm', 'tegradevflash']
        if values['--instance'] is not None and name in supports_instance:
            bin_name = [bin_name, '--instance', values['--instance']]
        else:
            bin_name = [bin_name]

        return bin_name

    def run_cpp_tool(self, input_file):
        """ Run cpp tool on input DTS file and produce preprocessed DTS file
        """

        file_name, _ = os.path.splitext(input_file)
        out_file = file_name + "_cpp.dts"
        # run cpp tool to process any c header includes in DTS
        # do NOT use self.exec_file
        command = ["cpp"]
        command.extend(["-nostdinc"])
        command.extend(["-x", "assembler-with-cpp"])
        command.extend(["-D", "IN_DTS_CONTEXT"])
        command.extend(["-I", os.path.relpath(tegraflash_abs_path(""))])
        command.extend(["-I", os.path.relpath(tegraflash_abs_path("t186ref/BCT"))])
        command.extend([input_file])
        command.extend([out_file])
        run_command(command, False)

        return out_file

    def run_dtc_tool(self, input_file):
        """ Run dtc tool on input DTS file and produce DTB output file
        """

        # get the filename without extension
        file_name, _ = os.path.splitext(input_file)
        dtb_file_name = file_name + ".dtb"

        # run cpp tool to process any c header includes in DTS
        # do NOT use self.exec_file
        command = ["dtc"]
        command.extend(["-I", "dts"])
        command.extend(["-O", "dtb"])
        command.extend(["-o", dtb_file_name])
        command.extend([input_file])
        run_command(command, False)

        return dtb_file_name

class TFlashT23x(TFlashT23x_Base):
    """ Class for Tegraflash functions and parameters specific to t23x.

    Parent Class: TFlashT23x_Base
    """

    def __init__(self, chip_id):

        if chip_id == 0x23:

            # Error check for Chip ID of t23x.
            super(TFlashT23x, self).__init__()

            # SHA digest offsets
            self.args_offset = '6784'
            self.args_length = '1408'

            # Fixed BCH offsets
            self.bch_offset = '4032'
            self.bch_length = '4160'

            self.bpmpdtbodm = None

        else:
            raise tegraflash_exception("Unsupported Chip.")

