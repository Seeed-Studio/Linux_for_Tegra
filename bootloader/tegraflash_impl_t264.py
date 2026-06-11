#
# SPDX-FileCopyrightText: Copyright (c) 2014-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA Corporation is strictly prohibited.
#

from __future__ import print_function

import binascii
from collections import defaultdict
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
import yaml
from xml.etree import ElementTree

from tegraflash_internal import (run_command, tegraflash_abs_path,
        tegraflash_symlink, tegraflash_generate_index_file, info_print,
        concat_file, getPart_name_by_type, tegraflash_concat_overlay_dtb,
        concat_file_4k, tegraflash_create_backup_file, set_partition_filename,
        get_partition_filename, tegraflash_add_odm_data_to_dtb, strip_string_list,
        parse_indexfile_for_qspi, is_partition_present, remove_partition_filename,
        tegraflash_pubkeyhash_for_sign)
from tegraflash_internal import cmd_environ, paths, start_time, values
from tegraflash_internal import tegraflash_exception

from tegrasign_v3 import (compute_sha, tegrasign, hex_to_str, str_to_hex)


from bootburn_bct import BootburnBrBct, BootburnHpseSbBct, BootburnMb1Bct, BootburnMemBct, BootburnBpmpMemBct, BootburnMb2Bct
from target_config import target_config

""" The following code section is only for t264 chips and newer
    Implementation of Tegraflash Script using OOP design pattern
"""
class TFlashT264_Base(object):
    """ Base Class for Tegraflash functions specific to t264 and newer.

    """

    tegrarcm_values = {
        '--board_info': 'board_info.bin',
        '--chip_info': 'chip_info.bin',
        '--bootdevice_info': 'bootdevice_info.bin',
        '--fuse_info': 'fuse_info.bin',
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
        '--meta_blob': 'meta_blob.txt'
    }
    tegraflash_binaries_v2 = {
        'tegrabct': 'tegrabct_v2',
        'tegradevflash': 'tegradevflash_v2',
        'tegrahost': 'tegrahost_v2',
        'tegraparser': 'tegraparser_v2',
        'tegrarcm': 'tegrarcm_v2',
        'tegrasign': 'tegrasign_v3.py',
        'fiptool': 'fiptool',
    }

    tegraflash_gpt_image_name_map = {
        'nvme_0_master_boot_record': 'mbr_12_0.bin',
        'nvme_0_primary_gpt': 'gpt_primary_12_0.bin',
        'nvme_0_secondary_gpt': 'gpt_secondary_12_0.bin',
        'sdcard_0_master_boot_record': 'mbr_6_0.bin',
        'sdcard_0_primary_gpt': 'gpt_primary_6_0.bin',
        'sdcard_0_secondary_gpt': 'gpt_secondary_6_0.bin',
        'sdmmc_boot_3_secondary_gpt': 'gpt_secondary_0_3.bin',
        'sdmmc_boot_3_secondary_gpt_backup': 'gpt_backup_secondary_0_3.bin',
        'sdmmc_user_3_master_boot_record': 'mbr_1_3.bin',
        'sdmmc_user_3_primary_gpt': 'gpt_primary_1_3.bin',
        'sdmmc_user_3_secondary_gpt': 'gpt_secondary_1_3.bin',
        'spi_0_secondary_gpt': 'gpt_secondary_3_0.bin',
        'spi_0_secondary_gpt_backup': 'gpt_backup_secondary_3_0.bin',
        'ufs_0_secondary_gpt': 'gpt_secondary_7_0.bin',
        'ufs_0_secondary_gpt_backup': 'gpt_backup_secondary_7_0.bin',
        'ufs_user_0_master_boot_record': 'mbr_8_0.bin',
        'ufs_user_0_primary_gpt': 'gpt_primary_8_0.bin',
        'ufs_user_0_secondary_gpt': 'gpt_secondary_8_0.bin',
        'external_0_master_boot_record': 'mbr_9_0.bin',
        'external_0_primary_gpt': 'gpt_primary_9_0.bin',
        'external_0_secondary_gpt': 'gpt_secondary_9_0.bin',
    }

    tegrabct_backup = { '--image' : None }

    # Magic IDs that have BCH header but do not contain complete info required by MB2
    # such as the binary hash appended at the end of the image.
    # So, we need to run tegrahost_v2 with --appendsigheader option that updates
    # stage2 info and also appends the binary hash.
    QUIRK_MAGIC_ID_LIST_LOADED_BY_MB2 = [
        'XUSB',
    ]

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
        self.l4t_flags = None

        # rcmboot_blob and rcmdump_blob
        self.rcmboot_blob_dir = 'rcmboot_blob'
        self.rcmboot_cmd_file = 'rcmbootcmd.txt'
        self.rcmdump_blob_dir = 'rcmdump_blob'
        self.rcmdump_cmd_file = 'rcmdumpcmd.txt'

        # Use the file from the current run
        if os.path.isfile(self.tegrasign_values['--pubkeyhash']):
            os.remove(self.tegrasign_values['--pubkeyhash'])

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
        magic_id = run_command(command).rstrip()
        info_print('partition type ' + partition_type + ', magic id = ' + magic_id)
        return magic_id

    def tegraflash_get_pt_dump(self, pt_file):
        info_print("Get magic id")
        command = self.exec_file('tegraparser')
        command.extend(['--pt', pt_file])
        command.extend(['--dumplayout'])
        pt_dump = run_command(command)
        return pt_dump

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

        compulsory_args = ['--rcmboot_bct_cfg', '--rcmboot_pt_layout']
        for required_arg in compulsory_args:
            if args[required_arg] is None:
                args[required_arg] = input('Input ' + required_arg + ': ')

        values['--cfg'] = values['--rcmboot_pt_layout']
        self.tegraflash_get_key_mode()
        self.tegraflash_parse_partitionlayout()
        bct_dict = self.tegraflash_generate_bct(values['--rcmboot_bct_cfg'], bct_flags=['ENABLE_FLASHING'])
        self.tegraflash_generate_bpmp_mem_bin(values['--rcmboot_bct_cfg'], flags=["BPMP_MEM_CFG"])
        self.tegraflash_process_binaries(bct_dict['mb2_bct_file_name'])
        self.tegraflash_parse_partitionlayout()
        self.tegraflash_fill_mb1_storage_info(bct_dict['br_bct_file_name'])
        if values['--encrypt_key'] is None:
            self.tegraflash_sign_images()
        else:
            self.tegraflash_enc_and_sign_images()

        self.tegraflash_update_br_bct_bl_info(bct_dict['br_bct_file_name'])
        self.tegraflash_update_images()
        self.tegraflash_generate_blob(True, 'blob.bin')
        self.tegraflash_send_to_bootrom(bct_dict)
        # sign image, and for coldboot as it needs to disable bit 'enable_flashing'
        self.tegraflash_send_to_bootloader(bct_dict, 'blob.bin')

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
        if exports != None:
            values.update(exports)

        filename = os.path.basename(in_file)
        info_print(filename)


        if self._is_header_present(in_file):
            info_print('******* nvsign is skipped for file name: %s  ********' %(in_file))
            return in_file
        else:
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
                sha_offset2 = 5216
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
                        enc_buff[sha_offset2:sha_offset2 + sha_size] = sha[:]
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

    def get_bct_configs_dict(self, bct_cfg_file, element_tag="brbct_cfg"):
        if (bct_cfg_file is None or not os.path.exists(bct_cfg_file)):
            raise tegraflash_exception(
                'Error: Bct config file not found {}'.format(bct_cfg_file))

        # Gather all the configs in a dict
        with open(bct_cfg_file, 'r') as f:
            xml_tree = ElementTree.parse(f)

            # Root has to be bct_cfg
            root = xml_tree.getroot()
            if (root.tag != "bct_cfg"):
                raise tegraflash_exception(
                    'Error: Invalid RCM boot bct cfg {}'.format(bct_cfg_file))
            element = root.find(element_tag)
            if element == None:
                # No exception is thrown because this entry is optional unless enforced
                info_print('Invalid element: %s in %s ' %(element_tag, bct_cfg_file))
                return None
            configs_dict = {}
            for child in element:
                if (child.text is not None):
                    configs_dict[child.tag] = child.text.strip()

        return configs_dict

    def tegraflash_preprocess_configs(self, configs_dict=None, preprocess_list=[], bct_flags=[]):
        """ Preprocess BCT configuration files using cpp and dtc tools
            if they are in DTS format
        """

        for config_type, config_file_name in configs_dict.items():
            if (config_type in preprocess_list and '.dtb' not in config_file_name):
                info_print('Pre-processing config: ' + config_file_name)
                config = self.run_cpp_tool(config_file_name, bct_flags)
                config = self.run_dtc_tool(config)
                configs_dict[config_type] = config

    def tegraflash_mkdevimages(self, args, cmd_args):
        values.update(args)

        if values['--coldboot_pt_layout'] is None:
            raise tegraflash_exception(
                'Error: Partition configuration is not specified')

        if values['--chip'] is None:
            raise tegraflash_exception(
                'Error: chip is not specified')
        # Set bct flag to True if bct generation is required
        # BCT flag needs to be passed to tegraflash_sign_images
        # function because it generated BR-BCT before signing
        # MB1 image
        bct_flag = False if "nobct" in cmd_args else True

        self.tegraflash_get_key_mode()
        self.tegraflash_create_secure_fip(values['--coldboot_pt_layout'])
        if (bct_flag):
            self.tegraflash_generate_hpse_sb_pkg(values['--coldboot_bct_cfg'])
        self.tegraflash_parse_partitionlayout()

        # if nobct is specified in the command argument
        # skip bct generation
        if (bct_flag):
            bct_dict = self.tegraflash_generate_bct(values['--coldboot_bct_cfg'], ['CONFIG_ENABLE_SC7'])
            self.tegraflash_generate_bpmp_mem_bin(values['--coldboot_bct_cfg'],
                                              flags=["BPMP_MEM_CFG"])

            self.tegraflash_process_binaries(bct_dict['mb2_bct_file_name'])
            self.tegraflash_parse_partitionlayout()
            self.tegraflash_fill_mb1_storage_info(bct_dict['br_bct_file_name'])
        else:
            # Here nobct needs to be removed from the cmd_args
            # since tegradevflash doesn't require it
            cmd_args.remove("nobct")

        if values['--encrypt_key'] is None:
            self.tegraflash_sign_images()
        else:
            self.tegraflash_enc_and_sign_images(bct_flag=bct_flag)

        if (bct_flag):
            self.tegraflash_update_br_bct_bl_info(bct_dict['br_bct_file_name'])
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

    def tegraflash_fetch_bootdevice_info(self):
        info_print('Retrieving boot device information')
        command = self.exec_file('tegrarcm')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--oem', 'platformdetails', 'bootdeviceinfo', self.tegrarcm_values['--bootdevice_info']])
        try:
            run_command(command)
        except tegraflash_exception as e:
            command[0] = self.exec_file('tegradevflash')[0]
            run_command(command)
        # copy to bootloader folder from process folder
        if os.path.exists(self.tegrarcm_values['--bootdevice_info']):
            out_file = tegraflash_abs_path(self.tegrarcm_values['--bootdevice_info'])
            try:
                shutil.copyfile(self.tegrarcm_values['--bootdevice_info'], out_file)
            except shutil.SameFileError:
                info_print("INFO: bootdevice_info.bin exists and the same\n")
            except:
                info_print("ERROR: failed to copy bootdevice_info.bin\n")

    def tegraflash_parse(self, args, parse_args):
        values.update(args)

        if parse_args[0] == 'fusebypass':
            self.tegraflash_parse_fuse_bypass(parse_args[1:])
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

    def tegraflash_parse_partitionlayout(self):
        info_print('Parsing partition layout')
        command = self.exec_file('tegraparser')
        command.extend(['--pt', values['--cfg']])
        self.tegraparser_values['--pt'] = os.path.splitext(values['--cfg'])[0] + '.bin'
        run_command(command)

        if values['--rawkerneldtb'] is None:
            kernel_dtb = get_partition_filename('kernel-dtb')
            if kernel_dtb == None:
                kernel_dtb = get_partition_filename('A_kernel-dtb')
            values['--rawkerneldtb'] = kernel_dtb

    def tegraflash_oem_enc(self, filename, bct_flag = False, meta_blob_sz = 0):
        if bct_flag == False and values['--duk'] != None:
            command = self.exec_file('tegrahost')
            command.extend(['--chip', values['--chip'], values['--chip_major'], values['--chip_minor']])
            command.extend(['--set_bch_field', 'duk', values['--duk'], filename])
            run_command(command)

        file_base, file_ext = os.path.splitext(filename)
        kdf_yaml = 'kdf_args_%s.yaml' %(file_base)

        chip_info = '%s%s %s'  %(values['--chip'], values['--chip_major'], values['--chip_minor'])
        lines = 'ENC : "OEM"\n'
        lines += 'CHIPID : "%s"\n' %(chip_info)
        if (meta_blob_sz != 0):
            lines += 'COMPRESS : "TRUE"\n'
            lines += 'METABLOBSIZE : "%d"\n' %(meta_blob_sz)
        else:
            lines += 'COMPRESS : "FALSE"\n'
        with open(kdf_yaml, 'w') as f:
            f.write(lines)

        if values['--hsm'] is True:
            self.call_tegrasign(filename, None, None, None, None, \
                                None, None, None, None, None, \
                                False, 0, 0, 0, None, \
                                0, ['kdf_file=' + kdf_yaml], 'sbk')
        else:
            self.call_tegrasign(filename, None, None, values['--encrypt_key'][0], None, \
                                None, None, None, None, None, \
                                False, 0, 0, 0, None, \
                                0, ['kdf_file=' + kdf_yaml])

        os.remove(kdf_yaml)
        return file_base + '_encrypt'  + file_ext

    def tegraflash_generate_signing_list(self, pt_file=None):
        info_print('Creating list of images to be signed')
        if pt_file == None:
            pt_file = self.tegraparser_values['--pt']
        command = self.exec_file('tegrahost')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--partitionlayout', pt_file])
        # External device doesn't need ratchet
        if not values['--external_device']:
            if values['--minratchet_config'] is not None and values['--rcmboot_bct_cfg'] is not None:
                self.tegraflash_generate_ratchet_blob()
                command.extend(['--ratchet_blob',
                                self.tegrahost_values['--ratchet_blob']])
        command.extend(['--list', self.tegrahost_values['--list']])
        mode = self.tegrasign_values['--mode']
        algo_list = {'pkc':'oem-rsa', 'ec':'oem-ecc', 'ec521':'oem-ecc521',
            'eddsa':'oem-eddsa', 'xmss':'oem-xmss'}

        if mode in algo_list:
            mode = algo_list[mode]
        command.extend([mode])
        if len(values['--key']) == 3:
            command.extend(['--nkeys', '3'])
        run_command(command)

    def tegraflash_generate_ratchet_blob(self):
        if not os.path.exists(self.tegrahost_values['--ratchet_blob']):
            info_print('Generating ratchet blob')
            # get ratchet configs dict from rcmboot_bct_cfg
            # for ratchet, the value in rcmboot_bct_cfg and coldboot_bct_cfg are the same
            configs_dict = self.get_bct_configs_dict(values['--rcmboot_bct_cfg'])
            # ratchet blob is generated when generating mb1-bct
            # since the ratchet info is saved in mb1-bct
            self.tegraflash_generate_mb1_bct(configs_dict)

    def tegraflash_sign_images(self, ovewrite_xml=True, pt_file=None):
        if pt_file == None:
            pt_file = self.tegraparser_values['--pt']
        self.tegraflash_generate_signing_list(pt_file)

        info_print('Generating signatures')
        key_val = values['--key']
        list_val = self.tegrahost_values['--list']
        pkh_val = self.tegrasign_values['--pubkeyhash']

        if values['--hsm'] is True:
            l4t_hsm_str = self.parse_hsm_l4t_key_label(key_val)
            self.call_tegrasign(None, None, None, None, None,
                                list_val, None, pkh_val, 'sha512', None, \
                                False, 0, 0, 0, None, \
                                0, None, l4t_hsm_str)

        else:
            self.call_tegrasign(None, None, None, key_val, None,
                                list_val, None, tegraflash_pubkeyhash_for_sign(key_val, pkh_val), 'sha512', None)

    def tegraflash_fill_mb1_storage_info(self, br_bct_file_name, pt_file=None):
        if pt_file == None:
            pt_file = self.tegraparser_values['--pt']
        brbct_arg = '--brbct'
        if  pt_file is not None:
            info_print('Updating bl info')
            command = self.exec_file('tegrabct')
            command.extend([brbct_arg, br_bct_file_name])
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            if values['--blversion'] is not None:
                command.extend(
                    ['--blversion', values['--majorversion'], values['--minorversion']])
            command.extend(
                ['--updateblinfo', pt_file])
            command.extend(['--parse-error-mode', 'quiet-continue'])
            # command.extend(
            #     ['--updatesig', self.tegrahost_values['--signed_list']])
            run_command(command)

    def tegraflash_get_l4t_flags(self, flash_type, bct_flags_file, overlay_file):
        bct_flags = []

        if(os.path.isfile(bct_flags_file) == False):
            print('Platform config path does not exist\n')
            raise tegraflash_exception('Platform config file not found -- not overiding values')

        with open(bct_flags_file) as file:
            l4tCflags = yaml.safe_load(file)

        if ((flash_type == 'rcm_boot') or (flash_type == 'rcm_flash')):
            bct_flags = [flag.strip() for flag in (l4tCflags['bct_defines'][flash_type].keys())]
        # flash-images case
        elif (flash_type == 'flash_images'):
            bct_flags = [flag.strip() for flag in (l4tCflags['bct_defines']['chains']['chain_A'].keys())]
        else:
            raise tegraflash_exception('Invalid flash type: %s' % flash_type)

        # Check if the overlay file is included in the arguments and override the flags present
        if (overlay_file != None):
            if(os.path.isfile(overlay_file) == False):
                print('Overlay platform config path does not exist\n')
                raise tegraflash_exception('Overlay platform config file not found -- not overiding values')

            with open(overlay_file) as file:
                l4tCflags = yaml.safe_load(file)

            if ((flash_type == 'rcm_boot') or (flash_type == 'rcm_flash')):
                if flash_type in list(l4tCflags['bct_defines']):
                    bct_flags = [flag.strip() for flag in (l4tCflags['bct_defines'][flash_type].keys())]
            # flash-images case
            elif (flash_type == 'flash_images'):
                if 'chains' in list(l4tCflags['bct_defines']):
                    bct_flags = [flag.strip() for flag in (l4tCflags['bct_defines']['chains']['chain_A'].keys())]
            else:
                raise tegraflash_exception('Invalid flash type: %s' % flash_type)

        return bct_flags

    def tegraflash_generate_bct(self, bct_cfg=None, bct_flags=[]):
        configs_dict = self.get_bct_configs_dict(bct_cfg)
        bct_dict = {}

        bct_file_name = self.tegraflash_generate_br_bct(configs_dict, bct_flags=bct_flags)

        if values['--external_device']:
            mb1_bct_file_name =  None
        else:
            mb1_bct_file_name = self.tegraflash_generate_mb1_bct(configs_dict, bct_flags=bct_flags)

        # TODO: HACK: figure out better way
        if (values['--cfg'] == values['--coldboot_pt_layout']):
            membct_file_name = self.tegraflash_generate_coldboot_mem_bct(configs_dict, bct_flags=bct_flags)
        else:
            membct_file_name, mem_bcts = self.tegraflash_generate_rcm_mem_bct(configs_dict, bct_flags=bct_flags)
            bct_dict['mem_bcts'] = mem_bcts

        if values['--external_device']:
            mb2_bct_file_name =  None
        else:
            mb2_bct_file_name = self.tegraflash_generate_mb2_bct(configs_dict, bct_flags=bct_flags)

        bct_dict['br_bct_file_name'] = bct_file_name
        bct_dict['mb1_bct_file_name'] = mb1_bct_file_name
        bct_dict['mem_bct_file_name'] = membct_file_name
        bct_dict['mb2_bct_file_name'] = mb2_bct_file_name

        return bct_dict

    def tegraflash_generate_bpmp_mem_bin(self, bct_cfg=None, flags=[]):
        # Only generate bpmp mem bins if parition is present
        if (not is_partition_present('mem_dtb', 'type')):
            info_print("WARNING: BPMP mem-dtb partition is not present in partition layout")
            info_print("WARNING: Skipping generation of BPMP mem dvfs binaries")
            return

        configs_dict = self.get_bct_configs_dict(bct_cfg)
        bpmp_mem_bins = self.generate_bpmp_mem_bins(configs_dict, flags)

        if (not bpmp_mem_bins):
            raise RuntimeError("ERROR: Couldn't generate BPMP mem cfg bianries!!")

        # Compress each binary
        compression_info_bins = []
        for i in range(len(bpmp_mem_bins)):
            compression_info_bins.append(bpmp_mem_bins[i] + ".compression_info")

            # compressed binary will be saved using this
            bin, ext = os.path.splitext(bpmp_mem_bins[i])
            compressed_bin_name = bin + "_compressed" + ext

            command = self.exec_file('tegrahost')
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            command.extend([
                '--compress',
                bpmp_mem_bins[i],
                compressed_bin_name,
                bpmp_mem_bins[i] + ".compression_info"
            ])

            bpmp_mem_bins[i] = compressed_bin_name

            run_command(command)

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
        command.extend(['--magicid', "MDTB"])
        command.extend([
            '--addsigheader_multi',
            bpmp_mem_bins[0], bpmp_mem_bins[1], bpmp_mem_bins[2],
            bpmp_mem_bins[3], bpmp_mem_bins[4], bpmp_mem_bins[5],
            bpmp_mem_bins[6], bpmp_mem_bins[7]
        ])
        run_command(command)
        filename, _ = os.path.splitext(bpmp_mem_bins[0])
        mem_file = 'bpmp_mem_cfg.bin'
        os.rename(filename + '_sigheader.bin', mem_file)

        # Add compression info to the header for each binary
        for i in range(len(bpmp_mem_bins)):
            command = self.exec_file('tegrahost')
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            command.extend(['--update_compression_info', mem_file, compression_info_bins[i]])
            command.extend(['--stage2_component_idx', str(i)])
            run_command(command)

        if values['--encrypt_key'] is not None:
            # Assign dummy value to sz_meta_blob to jump into compressed encryption flow
            # This value will be calculated in tegraopenssl during encryption process
            bpmp_mem_bin_file_name = self.tegraflash_oem_enc_and_sign_file(mem_file, 'MDTB', sz_meta_blob = 4096)
        else:
            bpmp_mem_bin_file_name = self.tegraflash_oem_sign_file(mem_file, 'MDTB')

        # Need to update pt name since file name is changed
        found_name = get_partition_filename('mem_dtb', 'type')
        if found_name != bpmp_mem_bin_file_name:
            set_partition_filename('mem_dtb', bpmp_mem_bin_file_name, 'type', is_ignore=False)

        return bpmp_mem_bin_file_name

    def tegraflash_generate_targetconfig(self, configs_dict, pkg='brbct_cfg'):
        os.environ['PDK_TOP'] = 'dummy'
        target_conf = target_config(True)
        if pkg == 'hpct_cfg':
            target_conf.boardDefaultPaths["f_HpseBrBctDevParam"] = configs_dict['dev_param']
            target_conf.boardDefaultPaths["f_HpseMB1SdRamParam"] = configs_dict['sdram']
            target_conf.boardDefaultPaths["f_HpseMB1wb0SdRamParam"] = ''
        elif pkg == 'sbct_cfg':
            target_conf.boardDefaultPaths["f_SbBrBctDevParam"] = configs_dict['dev_param']
            target_conf.boardDefaultPaths["f_SbMB1SdRamParam"] = configs_dict['sdram']
            target_conf.boardDefaultPaths["f_SbMB1wb0SdRamParam"] = ''
        elif pkg == 'brbct_cfg':
            target_conf.boardDefaultPaths["f_BrBctDevParam"] = configs_dict['dev_param']
            target_conf.boardDefaultPaths["f_MB1SdRamParam"] = configs_dict['sdram']
            target_conf.boardDefaultPaths["f_MB1wb0SdRamParam"] = configs_dict.get('wb0sdram','')
        target_conf.boardDefaultPaths["f_MB2StorageDevices"] = values['--cfg']
        target_conf.boardDefaultPaths["f_MB1BootDevice"] = configs_dict.get('device','')
        target_conf.boardDefaultPaths["f_UFSPhyLaneFile"] = configs_dict.get('uphy','')
        target_conf.boardDefaultPaths["f_MB1Pinmux"] = configs_dict.get('pinmux','')
        target_conf.boardDefaultPaths["f_MB1Pmic"] = configs_dict.get('pmic','')
        target_conf.boardDefaultPaths["f_MB1Pad"] = configs_dict.get('pmc','')
        target_conf.boardDefaultPaths["f_MB1Misc"] = configs_dict.get('misc','')
        target_conf.boardDefaultPaths["f_MB1Prod"] = configs_dict.get('prod','')
        target_conf.boardDefaultPaths["f_MB1GpioInt"] = configs_dict.get('gpioint','')
        target_conf.boardDefaultPaths["f_MB1DeviceProd"] = configs_dict.get('deviceprod','')
        target_conf.boardDefaultPaths["f_MB1MinRatchet"] = configs_dict.get('minratchet','')
        target_conf.boardDefaultPaths["f_BpmpMemCfgParam"] = configs_dict.get('bpmp_mem_cfg','')
        target_conf.boardDefaultPaths["f_MB2Bct"] = configs_dict.get('mb2bctcfg','')
        target_conf.boardDefaultPaths["f_MB2Scr"] = configs_dict.get('scr','')
        target_conf.boardDefaultPaths["s_chipID"] = f"{values['--chip']} {values['--chip_major']}"

        target_conf.filelist = {}
        if 'bpmp_mem_cfg' in configs_dict:
            filename = os.path.splitext(configs_dict['bpmp_mem_cfg'])[0]
            bpmp_mem_bins = [ f"{filename}_{i}.bin" for i in range(1, 9)]
            target_conf.filelist["f_BpmpMembctBin"] = bpmp_mem_bins

        if 'sdram' in configs_dict:
            filename = os.path.splitext(configs_dict['sdram'])[0]
            mem_bcts = [f"{filename}_{i}.bct" for i in range(1, 9)]
            target_conf.filelist["f_MembctBin"] = mem_bcts

        target_conf.s_BootDevice = "qspi"

        target_conf.flashUtils.f_TegraBct = self.exec_file('tegrabct')[0]
        target_conf.f_DTCTool = 'dtc'
        # Ensure sysMonitor is not None and has a .log method
        target_conf.sysMonitor = type(
            '', (),
            {'log': staticmethod(info_print)}
        )()
        return target_conf

    def tegraflash_generate_br_bct(self, configs_dict, bct_flags=[], magicid=None):
        target_conf = self.tegraflash_generate_targetconfig(configs_dict)
        values['--bct'] = 'br_bct.cfg'
        bct_file_name = os.path.splitext(values['--bct'])[0] + '_BR.bct'

        if os.path.islink(bct_file_name):
            os.unlink(bct_file_name)
        bct_flags.append('IN_DTS_CONTEXT')
        br_bct = BootburnBrBct(target_conf, os.getcwd(), bctFileNameSuffix=values['--bct'])
        br_bct.generateBct(bct_flags)

        return bct_file_name

    def addBch(self, filename, magic_id):
        basename = os.path.basename(filename)
        aligned_file = os.path.splitext(basename)[0] + '_aligned' + os.path.splitext(basename)[1]
        sigHdr_file = os.path.splitext(basename)[0] + '_aligned_sigheader' + os.path.splitext(basename)[1]
        if os.path.exists(filename):
            shutil.copyfile(filename, aligned_file)

        command = self.exec_file('tegrahost')
        command.extend(['--chip', values['--chip']])
        command.extend(['--align', aligned_file])
        run_command(command)

        command = self.exec_file('tegrahost')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--magicid', magic_id])
        command.extend(['--appendsigheader', aligned_file, 'zerosbk'])
        run_command(command)

        if os.path.exists(sigHdr_file) == False:
            raise tegraflash_exception(f'Error: {sigHdr_file} not created')

        return sigHdr_file


    def tegraflash_generate_hpse_or_sb_pkg(self, configs_dict, is_hpse):
        if is_hpse:
            bl_name = 'hpse_bl' # partition_name
            bl_type = 'hpse_bl' # partition_type
            om_filename = 'hpseom.pkg'
            om_magic = 'HPOM'
            om_name = 'hpse-om'
            om_type = 'hpse_om'
            pct_filename = 'spvault-hpse-oem-conf.bin'
            pct_magic = 'HPCF'
            pt_xml = 'hpse.xml'
            pkg_name = 'hpse_pkg' # This is saved to pt
            base_name = 'hpse_base'

        else:
            bl_name = 'sb_bl'
            bl_type = 'sb_bl'
            om_filename = 'sbom.pkg'
            om_magic = 'SBOM'
            om_name = 'sb-om'
            om_type = 'sb_om'
            pct_filename = 'spvault-sb-oem-conf.bin'
            pct_magic = 'SBCF'
            pt_xml = 'sb.xml'
            pkg_name = 'sb_pkg' # This is saved to pt
            base_name = 'sb_base'


        # 1. Sign pct file
        # PCT needs to be sign, tegraflash_oem_sign_file() adds BCH and sign
        signed_pct_filename = self.tegraflash_oem_sign_file(pct_filename, pct_magic)

        # 2. Concatenate krnl + pct + cpio as om pkg
        om_parts = [configs_dict['fw'], signed_pct_filename, configs_dict['raw_bin']]
        buff_size = 0
        start = 0

        for part in om_parts:
            buff_size += os.path.getsize(part)
        buff = bytearray(buff_size)
        for part in om_parts:
            with open(part, 'rb') as f:
                part_buff = bytearray(f.read())
                buff[start:start+len(part_buff)] = part_buff[:]
                start += len(part_buff)
        with open(om_filename, 'wb') as f:
            f.write(buff)

        # 3. create pt and update to bct
        bl_filename = configs_dict['bl']
        part_dict = {
            # Name  : [type, size, align_size, file]
            bl_name : [bl_type, str(os.path.getsize(bl_filename)), '262144', bl_filename],
            om_name : [om_type, str(os.path.getsize(om_filename)), '262144', om_filename],
        }

        device_dict = {"type" : "spi", "instance" : "0", "sector_size" : "512", "num_sectors":"131072"}
        misc_dict = {"allocation_policy" : "sequential",
            "filesystem_type" : "basic",
            "file_system_attribute" : "0",
            "allocation_attribute"  : "8",
            "percent_reserved"      : "0",
        }

        root = ElementTree.Element("partition_layout")
        root.set("version", "01.00.0000")
        comment = ElementTree.Comment("Nvidia Tegra Partition Layout Version 1.0.0")
        root.append(comment)
        device_elmt = ElementTree.SubElement(root, "device")

        for key in device_dict:
            device_elmt.set(key, device_dict[key])

        for part in part_dict:
            part_elmt = ElementTree.SubElement(device_elmt, "partition")
            part_elmt.set("name", part)
            part_elmt.set("type", part_dict[part][0])
            part_elmt.set("oem_sign", 'true')
            for misc in misc_dict:
                misc_elmt = ElementTree.SubElement(part_elmt, misc)
                misc_elmt.text = misc_dict[misc]

            size_elmt = ElementTree.SubElement(part_elmt, "size")
            size_elmt.text = part_dict[part][1]
            align_elmt = ElementTree.SubElement(part_elmt, "align_boundary")
            align_elmt.text = part_dict[part][2]
            filename_elmt = ElementTree.SubElement(part_elmt, "filename")
            filename_elmt.text = part_dict[part][3]

        part_tree = ElementTree.ElementTree(root)
        part_tree.write(pt_xml)

        info_print('Parsing partition layout')
        command = self.exec_file('tegraparser')
        command.extend(['--pt', pt_xml])
        pt_bin = os.path.splitext(pt_xml)[0] + '.bin'
        run_command(command)

        # Update storage info
        self.tegraflash_fill_mb1_storage_info(configs_dict['br_bct_file_name'], pt_bin)

        # sign (or enc & sign) bl & om-pkg
        if values['--encrypt_key'] is None:
            self.tegraflash_sign_images(pt_file=pt_bin)
        else:
            self.tegraflash_enc_and_sign_images(bct_flag=True,cfg_file=pt_xml, pt_file=pt_bin)

        self.tegraflash_update_br_bct_bl_info(configs_dict['br_bct_file_name'], pt_bin)
        self.tegraflash_update_images(pt_bin)

        # 4. Final packaging = bct + bl + om-pkg
        part_dict = {
            bl_type : [bl_filename, 'bl_oemsigned', 'bl_size_final'],
            om_type : [om_filename, 'om_oemsigned', 'om_size_final'],
        }

        # Check for the size of various components
        pt_dump = self.tegraflash_get_pt_dump(pt_bin)
        for part in part_dict:
            basename = os.path.splitext(part_dict[part][0])[0]
            m = re.search(fr"{basename}([\w._-]+)", pt_dump)
            if m:
                configs_dict[part_dict[part][1]] = m.group(0)
                configs_dict[part_dict[part][2]] = os.path.getsize(m.group(0))

        configs_dict['br_bct_size_final'] = os.path.getsize(configs_dict['br_bct_file_name'])
        buff_size = configs_dict['br_bct_size_final'] + configs_dict['bl_size_final'] + configs_dict['om_size_final']
        buff = bytearray(buff_size)

        part_dict = {
            configs_dict['br_bct_file_name']: configs_dict['br_bct_size_final'],
            configs_dict['bl_oemsigned']: configs_dict['bl_size_final'],
            configs_dict['om_oemsigned']: configs_dict['om_size_final'],
            }

        # Construct the package
        start = 0
        for part in part_dict:
            info_print('Packaging %s to %s' %(part, pkg_name))
            with open(part, 'rb') as f:
                part_buff = bytearray(f.read())
                buff[start:start+part_dict[part]] = part_buff[:]
                start += part_dict[part]
        if start == 0:
            raise tegraflash_exception("Failed to create %s due to 0 size from partition files" %(configs_dict[base_name]))

        with open(configs_dict[base_name], 'wb') as f:
            f.write(buff)

        magic_id = self.tegraflash_get_magicid(pkg_name)
        # The package doesn't need to be signed but only the BCH with hash
        configs_dict[pkg_name] = self.addBch(configs_dict[base_name], magic_id)

    def tegraflash_add_hpse_or_sb_to_pt(self, configs_dict, is_hpse):
        if is_hpse:
            pkg_name = 'hpse_pkg' # This is saved to pt
        else:
            pkg_name = 'sb_pkg'
        is_ignore = False
        set_partition_filename(pkg_name, configs_dict[pkg_name], 'type', is_ignore)

    def tegraflash_can_build_hpse_or_sb(self, configs_dict):
        file_list = ['dev_param', 'sdram', 'bl', 'fw']
        for filename in file_list:
            if not os.path.exists(configs_dict[filename]):
                info_print('%s is not found' %(configs_dict[filename]))
                return False
        return True

    def tegraflash_generate_hpse_sb_pkg(self, bct_cfg=None, bct_flags=[]):
        # Format:   {cfg_type: [magic, file_prefix, is_hpse]}
        pkg_types = {'hpct_cfg' : ['HPCT', 'hpse_', True], 'sbct_cfg' : ['SBCT', 'sb_', False]}
        bct_flags.append('IN_DTS_CONTEXT')
        for pkg in pkg_types:
            info_print('Generating %spackage' %(pkg_types[pkg][1]))
            configs_dict = self.get_bct_configs_dict(bct_cfg, pkg)
            if configs_dict == None:
                info_print('%s is not defined in %s' %(pkg, bct_cfg))
                continue
            if self.tegraflash_can_build_hpse_or_sb(configs_dict) == False:
               info_print('%spacakge building is skipped. Please check %s and setup files' %(pkg_types[pkg][1], bct_cfg))
               continue

            target_conf = self.tegraflash_generate_targetconfig(configs_dict, pkg)
            BootburnHpseSbBct(target_conf, os.getcwd(), pkg_types[pkg][0]).generateBct(bct_flags)
            bct_file_name = pkg_types[pkg][0] + "_bct_BR.bct"
            configs_dict['br_bct_file_name'] = bct_file_name
            new_bct_file_name = pkg_types[pkg][1] + bct_file_name
            os.rename(bct_file_name, new_bct_file_name)
            configs_dict['br_bct_file_name'] = new_bct_file_name

            self.tegraflash_generate_hpse_or_sb_pkg(configs_dict, pkg_types[pkg][2])
            self.tegraflash_add_hpse_or_sb_to_pt(configs_dict, pkg_types[pkg][2])

    def tegraflash_generate_recovery_blob(self, exports, recovery_args):
        raise tegraflash_exception("ERROR: Not Implemented")

    def get_filename_for_partition_type(self, partition_type):
        with open(values['--cfg'], 'r') as file:
            xml_tree = ElementTree.parse(file)
        root = xml_tree.getroot()
        for node in root.iter('partition'):
            if(node.get('type')  == partition_type):
                file_node = node.find('filename')
                return file_node.text.strip()

        return None

    def tegraflash_generate_blob(self, sign_images, blob_filename):
        info_print('Generating blob for T264')
        root = ElementTree.Element('file_list')
        root.set('mode', 'blob')
        comment = ElementTree.Comment('Auto generated by tegraflash.py')
        root.append(comment)

        with open(values['--cfg'], 'r') as file:
            xml_tree = ElementTree.parse(file)
        rcm_fw_cfg_root = xml_tree.getroot()

        # Special sign cpu_bl partition
        child = ElementTree.SubElement(root, 'file')
        filename = self.get_filename_for_partition_type("bootloader_stage2")

        if not os.path.exists('blob_' + filename):
            tegraflash_symlink(filename, 'blob_' + filename)

        filename = 'blob_' + filename;

        if sign_images:
            if values['--encrypt_key'] is not None:
                filename = self.tegraflash_oem_enc_and_sign_file(filename, 'CPBL')
            else:
                filename = self.tegraflash_oem_sign_file(filename, 'CPBL')

        child.set('name', filename)
        child.set('type', 'bootloader_stage2')

        rcm_dev = None
        for device in rcm_fw_cfg_root.findall('device'):
            if device.get('type') == 'rcm':
                rcm_dev = device

        if rcm_dev is None:
            raise tegraflash_exception('rcm fw configuration is NULL')

        for partition in rcm_dev.iter('partition'):
            img_type = partition.get('type')
            file_node = partition.find('filename')
            if (file_node == None or (img_type == 'bootloader' or img_type == "bootloader_stage2")):
                continue
            if file_node.text == None:
                raise tegraflash_exception("filename is not defined for partition type: {} in {}".format(img_type, values['--cfg']))
            filepath = file_node.text.strip()

            child = ElementTree.SubElement(root, 'file')

            child.set('type', img_type)

            filename = os.path.basename(filepath)

            if img_type == 'fskp_bin' and values['--fuse_info'] is not None:
                if values['--ecid'] is None:
                    raise tegraflash_exception("fskp flow requires --ecid value to continue")
                fuse_info_xml = values['--fuse_info']
                fuse_info_bin = os.path.splitext(fuse_info_xml)[0] + '_fuseinfo.bin'
                info_print('Generating fuse information\n')
                command = self.exec_file('tegraparser')
                command.extend(['--chip', values['--chip'], values['--chip_major']])
                command.extend(['--fuse_info', fuse_info_xml])
                command.extend([fuse_info_bin])
                run_command(command)
                # append fuse_info.bin to fskp binary. XXX: not a mechanical concatenation
                info_print('Concatenate fuse info to fskp\n')
                command = self.exec_file('tegraparser')
                command.extend(['--fskp_info', filename])
                command.extend([fuse_info_bin])
                run_command(command)
                # the fskp comb is named as e.g. fskp_updated.bin
                filename = os.path.splitext(filename)[0] + '_updated.bin'
            else:
                filename = self.tegraflash_concat_partition(img_type, filename, values['--rcmboot_bct_cfg'])

            if not os.path.exists(filename):
                tegraflash_symlink(tegraflash_abs_path(filepath), filename)

            if not os.path.exists('blob_' + filename):
                tegraflash_symlink(filename, 'blob_' + filename)

            filename = 'blob_' + filename;

            if partition.get('oem_sign') == 'true':
                magic_id = self.tegraflash_get_magicid(img_type)
                if values['--encrypt_key'] is not None:
                    filename = self.tegraflash_oem_enc_and_sign_file(filename, magic_id)
                else:
                    filename = self.tegraflash_oem_sign_file(filename, magic_id)

            child.set('name', filename)

        blobtree = ElementTree.ElementTree(root);
        blobtree.write('blob.xml')

        command = self.exec_file('tegrahost')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--generateblob', 'blob.xml', blob_filename])

        run_command(command)

    def tegraflash_create_keylist(self, key_file, index):
        # Only create keylist xml file if --key <file> is valid
        if key_file == 'None' or key_file == None:
            return None

        basename = os.path.splitext(key_file)[0]
        key_list = '%s_list.xml' %(basename)
        if os.path.exists(key_list):
            return key_list

        info_print('Creating %s for key list signing' %(key_list))

        root = ElementTree.Element('entry_list')
        comment = ElementTree.Comment('Auto generated by tegraflash.py')
        root.append(comment)

        bct_attribs = {'active_index':index, 'pcp_file':'%s.pcp' %(basename), \
            'pcps_file':'%s.pcps' %(basename), 'pcps_hash_file':'%s.pcps.hash' %(basename), \
            'chip_id':'%s%s' %(values['--chip'], values['--chip_major'])}
        key_attribs = {'key': key_file}
        bct_child = ElementTree.SubElement(root, 'bct')

        for attrib in bct_attribs:
            bct_child.set(attrib, bct_attribs[attrib])

        for i in range(16):
            # Set index0 and index1 to point to the <file> from --key <file>
            if i in [0, 1]:
                is_hash_only = False
            else:
                is_hash_only = True
            child = ElementTree.SubElement(root, 'entry')
            for attrib in key_attribs:
                child.set(attrib, key_attribs[attrib])
                if  is_hash_only == True:
                    child.set('mode', 'sha')
                else:
                   # Leave mode =='' as tegrasign_v3 will fill in
                    child.set('mode', '')
                child.set('pub_file',  '%s_%d.pub' %(basename, i))
                child.set('hash_file', '%s_%d.pub.hash' %(basename, i))
                child.set('key_id', str(i))

        list_tree = ElementTree.ElementTree(root)
        list_tree.write(key_list)

        return key_list

    def tegraflash_get_signkey(self, file_name=None, is_bct=False):
        # return <key_list_file>, <key_file>

        if is_bct == True:
            active_index = '1' # Hardcoded
            if values['--key'][0] == 'None':
                return None, 'None'

            if values['--key_list'] != None:
                return values['--key_list'], values['--key'][0]

            key_list = self.tegraflash_create_keylist(values['--key'][0], active_index)

            key_path = None
            with open(key_list, 'r') as f:
                xml_tree = ElementTree.parse(f)
                attrib = 'active_index'
                root = xml_tree.getroot()
                bct_child = root.find('bct')
                if bct_child == None or attrib not in bct_child.attrib:
                    raise tegraflash_exception('Could not find active_index')
                active_index = bct_child.attrib[attrib]
                tag = 'key_id' + active_index
                for node in root.findall('entry'):
                    if(node.get('key_id') == active_index):
                        key_path = node.get('key').strip()
            return key_list, key_path
        else:
            return None, values['--key'][0]

    def generate_bct_backup_image(self, bct_file_name):
        info_print('Generating BCT backup image')

        # Concatenate brbct files into one image and then
        # write it into the BCT-boot-chain_backup partition.
        # The BCT-boot-chain_backup partition is 64KiB and it
        # is divided into four blocks whose size is 16KiB.
        # The first block stores the brbct file for chain A,
        # and the second block stores the brbct file for chain B.
        # The other blocks are not used.
        bct_block_size = 16384
        bct_backup_image = "bct_backup.img"
        bct_backup_image_size = 65536

        # Ensure that the size of brbct file is not larger
        # than the "bct_block_size".
        bct_file_size = os.path.getsize(bct_file_name)
        if bct_file_size > bct_block_size:
            raise tegraflash_exception(
                'Error: the brbct size(' + bct_file_size + ' bytes) is larger than ' + bct_block_size + ' bytes')

        # Delete symbol link to bct backup image if it exists
        # to make sure the bct backup image is generated under
        # the temporary directory.
        if os.path.islink(bct_backup_image):
            os.unlink(bct_backup_image)

        # Generate bct backup image by filling zeros
        command = ['dd']
        command.extend(['if=/dev/zero', 'of=' + bct_backup_image, 'bs=1', 'count=' + str(bct_backup_image_size)])
        run_command(command)

        # Write bct backup image
        offset = 0
        for index in range(0, 2):
            command = ['dd']
            command.extend(['if=' + bct_file_name, \
                'of=' + bct_backup_image, 'bs=1', 'seek=' + str(offset), 'conv=notrunc'])
            run_command(command)
            offset = offset + bct_block_size

        self.tegrabct_backup['--image'] = bct_backup_image
        return

    def tegraflash_update_br_bct_bl_info(self, bct_file_name, pt_file=None):
        if values['--cust_info'] is not None:
            info_print('Updating customer data section')
            command = self.exec_file('tegrabct')
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            command.extend(['--brbct', bct_file_name])
            command.extend(['--update_custinfo', values['--cust_info']])
            run_command(command)

        if pt_file == None:
            pt_file = self.tegraparser_values['--pt']

        if pt_file is not None:
            info_print('Updating bl info')
            command = self.exec_file('tegrabct')
            command.extend(['--brbct', bct_file_name])
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            command.extend(['--updateblinfo', pt_file])
            command.extend(['--parse-error-mode', 'quiet-continue'])
            if values['--blversion'] is not None:
                command.extend(
                    ['--blversion', values['--majorversion'], values['--minorversion']])
            command.extend(
                ['--updatesig', self.tegrahost_values['--signed_list']])
            run_command(command)

        self.tegraflash_update_boardinfo(bct_file_name)

        if values['--encrypt_key'] is not None:
            info_print('Perform encryption on bct')
            enc_file = self.tegraflash_oem_enc(bct_file_name, True) # br_bct_BR_encrypt.bct
            shutil.copyfile(enc_file, bct_file_name)

        info_print('Get Signed section of bct')
        command = self.exec_file('tegrabct')
        command.extend(['--brbct', bct_file_name])
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--listbct', self.tegrabct_values['--list']])
        run_command(command)

        signed_key_list = None
        key_list, signkey= self.tegraflash_get_signkey(bct_file_name, is_bct=True)

        if key_list is not None:
            info_print('Calculating BCT pcp hash since either --key or --key_list is defined')
            key_val = key_list
            hash_file = 'pcps.hash'
            pkh_val = [self.tegrasign_values['--pubkeyhash'], hash_file]
            if values['--hsm'] is True:
                l4t_hsm_str = self.parse_hsm_l4t_key_label(signkey)
                # Note: key_val is the key list file, not the key file
                # L4T HSM mode need to pass the key list file to tegrasign
                # to extract the public keys from key list file for SHA calculation
                self.call_tegrasign(None, None, None, key_val, None,
                            None, None, pkh_val, None, None,
                            False, 0, 0, 0, None,
                            0, None, l4t_hsm_str)
            else:
                self.call_tegrasign(None, None, None, key_val, None,
                            None, None, pkh_val, None, None)
            run_command(command)

            fileNm, fileExt = os.path.splitext(key_list)
            signed_key_list = fileNm + '_signed' + fileExt

        info_print('Signing BCT')
        key_val = signkey
        list_val = self.tegrabct_values['--list']
        sha_val = 'sha512'
        pkh_val = self.tegrasign_values['--pubkeyhash']
        if values['--hsm'] is True:
            l4t_hsm_str = self.parse_hsm_l4t_key_label(key_val)
            self.call_tegrasign(None, None, None, None, None, \
                                list_val, None, pkh_val, sha_val, None, \
                                False, 0, 0, 0, None, \
                                0, None, l4t_hsm_str)
        else:
            self.call_tegrasign(None, None, None, key_val, None, \
                                list_val, None, tegraflash_pubkeyhash_for_sign(key_val, pkh_val), sha_val, None)

        info_print('Updating BCT with signature')
        command = self.exec_file('tegrabct')
        command.extend(['--brbct', bct_file_name])
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--updatesig', self.tegrabct_values['--signed_list']])
        if signed_key_list != None and os.path.isfile(signed_key_list):
            command.extend(
                ['--pubkeyhash', signed_key_list])
        run_command(command)

        # Generate and update SHA digest for BR-BCT.
        list_val = self.tegrabct_values['--list']
        info_print('Generating SHA2 Hash')
        if values['--hsm'] is True:
            self.call_tegrasign(None, None, None, 'None', None, \
                                list_val, None, None, sha_val, None, \
                                False, 0, 0, 0, None, \
                                0, None, 'sbk')
        else:
            self.call_tegrasign(None, None, None, 'None', None,
                                list_val, None, None, sha_val, None)
        info_print('Updating BCT with SHA2 Hash')
        command = self.exec_file('tegrabct')
        command.extend(['--brbct', bct_file_name])
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--updatesha',
                        self.tegrabct_values['--signed_list']])
        run_command(command)

        # If BCT-boot-chain_backup partition exists, generate bct backup image
        if values['--bct_backup']:
            self.generate_bct_backup_image(bct_file_name)

    def tegraflash_generate_rcmdump_blob(self, cmd_list = [], binary_list = []):
        # Create a blob directory. If exists, clear all files
        abs_outdir = tegraflash_abs_path(self.rcmdump_blob_dir)
        if os.path.exists(abs_outdir):
            shutil.rmtree(abs_outdir, ignore_errors=True)
        os.makedirs(abs_outdir)

        self.tegraflash_append_rcmdump_blob(cmd_list, binary_list)
        info_print("rcmdump files are saved in " + self.rcmdump_blob_dir + " folder")

    def tegraflash_append_rcmdump_blob(self, cmd_list = [], binary_list = []):
        abs_outdir = tegraflash_abs_path(self.rcmdump_blob_dir)

        with open(self.rcmdump_cmd_file, 'a') as f:
            for cmd in cmd_list:
                f.write(cmd)
                f.write('\n')
        f.close()
        shutil.copyfile(self.rcmdump_cmd_file, abs_outdir + "/" + self.rcmdump_cmd_file)
        info_print(self.rcmdump_cmd_file + ' is copied to ' + self.rcmdump_blob_dir + " folder")

        for bin in binary_list:
            shutil.copyfile(bin, abs_outdir + "/" + bin)
            info_print(bin + ' is copied to ' + self.rcmdump_blob_dir + " folder")

    def tegraflash_send_to_bootrom(self, bct_dict, gen_blob_only = False):
        global uidlog
        # non-secure case generate bct at run time
        if values['--securedev'] and not self.tegrabct_values['--updated']:
            self.tegraflash_update_boardinfo(bct_dict['br_bct_file_name'])

        mb1_bin = self.get_file_name_from_images_list('mb1_bootloader')
        psc_bl1_bin = self.get_file_name_from_images_list('psc_bl1')

        if gen_blob_only == False:
            info_print('Boot Rom communication')

        command = self.exec_file('tegrarcm')
        command.extend(['--new_session'])
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--uid'])
        command.extend(['--download', 'bct_br', bct_dict['br_bct_file_name']])
        command.extend(['--download', 'mb1', mb1_bin])
        command.extend(['--download', 'psc_bl1', psc_bl1_bin])
        command.extend(['--download', 'bct_mb1', bct_dict['mb1_bct_file_name']])

        # if gen_blob_only is True, generate rcmdump_blob only (no running of command)
        if gen_blob_only == True:
            info_print('gen_blob option specified. Skip running Boot Rom communication')
            dumpcmd = ' '.join(command)
            dumpcmd = "./" + dumpcmd
            chkcmd = 'if [ $? -ne 0 ]; then exit 1; fi;'
            self.tegraflash_generate_rcmdump_blob([dumpcmd, chkcmd],
                                                  [bct_dict['br_bct_file_name'], mb1_bin, psc_bl1_bin,
                                                   bct_dict['mb1_bct_file_name']])
            return

        if values['--no_flash']:
            info_print('--no_flash option specified. Skip running Boot Rom communication')
            cmd = ' '.join(command)
            cmd = "./" + cmd
            # rcmcmd1, bct_br, mb1, psc_bl1, bct_mb1
            return cmd, bct_dict['br_bct_file_name'], mb1_bin, psc_bl1_bin, bct_dict['mb1_bct_file_name']

        uidlog = run_command(command, True)
        info_print('Boot Rom communication completed')

    def tegraflash_send_to_bootloader(self, bct_dict, blob_file_name):
        info_print('Sending membct and RCM blob')
        command = self.exec_file('tegrarcm')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--pollbl'])
        command.extend(['--download', 'bct_mem', bct_dict['mem_bct_file_name']])
        command.extend(['--download', 'blob', blob_file_name])
        if values['--no_flash']:
            info_print('--no_flash option specified. Skip sending membct and RCM blob')
            cmd = ' '.join(command)
            cmd = "./" + cmd
            # rcmcmd2, mem_bct, blob_file_name
            return cmd, bct_dict['mem_bcts'], blob_file_name
        run_command(command)
        info_print('completed')

    def tegraflash_boot(self, boot_type):
        command = self.exec_file('tegrarcm')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--boot', boot_type])
        run_command(command)
        if boot_type == 'recovery':
            self.tegraflash_poll_applet_bl()

    def tegraflash_send_mb2_applet(self, bct_dict, gen_blob_only = False):
        filename = None

        filename = self.get_filename_for_partition_type('mb2_applet')

        if filename is None:
            raise tegraflash_exception('mb2 applet not found in --bins')

        if values['--encrypt_key'] is not None:
            filename = self.tegraflash_oem_enc_and_sign_file(filename, 'MB2A')
        else:
            filename = self.tegraflash_oem_sign_file(filename, 'MB2A')

        # Save brbct and applet in applet dir so later can be packaged for auto flow
        if bct_dict != None:
            binary_list = [bct_dict['br_bct_file_name'], filename]
            applet_dir = 'applet'

            # Create applet directory. If exists, clear all files
            output_dir = tegraflash_abs_path(applet_dir)
            if os.path.exists(output_dir):
                shutil.rmtree(output_dir, ignore_errors=True)
            os.makedirs(output_dir)

            for bin in binary_list:
                shutil.copyfile(bin, output_dir + "/" + bin)

            info_print("All applet flow required files are saved in " + applet_dir + " folder")

        if values['--no_flash']:
            info_print('--no_flash option specified. Skip sending applet')
            return

        if gen_blob_only == False:
            info_print('Sending mb2_applet...\n')
        command = self.exec_file('tegrarcm')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--pollbl'])
        command.extend(['--download', 'applet', filename])

        # if gen_blob_only is True, generate rcmdump_blob only (no running of command)
        if gen_blob_only == True:
            dumpcmd = ' '.join(command)
            dumpcmd = "./" + dumpcmd
            chkcmd = 'if [ $? -ne 0 ]; then exit 2; fi;'
            self.tegraflash_append_rcmdump_blob([dumpcmd, chkcmd], [filename])
        else:
            run_command(command)
            info_print('completed')

    def tegraflash_boot_mb2_applet(self, bct_dict=None, gen_blob_only = False):
        filename = self.tegraflash_send_mb2_applet(bct_dict, gen_blob_only)
        if gen_blob_only:
            return

        if values['--no_flash']:
            info_print('--no_flash option specified. Skip sending applet')
            return

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
        if values['--no_flash']:
            info_print('--no_flash option specified. Skip checkinging on applet status')
            return False
        if self.first_time_mb2app_check == True:
            self.first_time_mb2app_check = False
            return False
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

    def tegraflash_create_secure_fip(self, partition_layout_xml):
        required_sp_images = ('tos-optee_t264.img')
        sp_images = {
            'mods_t264.pkg': '1f4bfeb9-0f48-dd1e-119c-2c86c9140322',
            'tos-optee_t264.img': '486178e0-e7f8-11e3-bc5e-0002a5d5c51b',
            'standalonemm_jetson.pkg': '9b12fb8a-6047-ca64-88c8-18864a6caa4c'
        }

        if is_partition_present('secure_partition', 'type') == False:
            info_print("WARNING: secure_partition is not specified, skipping")
            return

        sp_filename = 'sp_t264.fip'
        info_print('Creating: ' + sp_filename)
        command = self.exec_file('fiptool')
        command.extend(['create', '--align', '0x1000'])

        for sp_image_bin_name, uuid in sp_images.items():
            if (os.path.exists(sp_image_bin_name)):
                command.extend(['--blob', 'uuid='+ uuid + ',file=' + sp_image_bin_name])
            else:
                # File doesn't exist, fail if it is required
                if (sp_image_bin_name in required_sp_images):
                    raise tegraflash_exception("Required binary {} not found".format(sp_image_bin_name))

        command.extend([sp_filename])
        run_command(command)

        if os.path.exists(sp_filename):
            set_partition_filename('secure_partition', sp_filename, 'type', is_ignore=False)
        else:
            info_print("WARNING: sip file name is not generated, skipping")
            remove_partition_filename('secure_partition', 'type')

    def tegraflash_flash(self, args):
        global start_time
        start_time = time.time()
        values.update(args)

        if values['--chip'] is None:
            print('Error: chip is not specified')
            return 1

        if values['--rcmboot_pt_layout'] is None or values['--coldboot_pt_layout'] is None:
            print('Error: RCM boot and Cold boot PT layouts are required!!')
            return 1

        if values['--coldboot_bct_cfg'] is None or values['--rcmboot_bct_cfg'] is None:
            print('Error: RCM boot and Cold boot BCT cfgs are required!!')
            return 1

        # Perform rcm boot for flashing
        # Set the initial configuration to be rcm boot configuration
        values['--cfg'] = values['--rcmboot_pt_layout']
        self.tegraflash_generate_hpse_sb_pkg(values['--rcmboot_bct_cfg'])
        self.tegraflash_get_key_mode()
        self.tegraflash_parse_partitionlayout()
        bct_dict = self.tegraflash_generate_bct(values['--rcmboot_bct_cfg'], bct_flags=['ENABLE_FLASHING'])
        self.tegraflash_generate_bpmp_mem_bin(values['--rcmboot_bct_cfg'], flags=["BPMP_MEM_CFG"])
        self.tegraflash_process_binaries(bct_dict['mb2_bct_file_name'])
        self.tegraflash_parse_partitionlayout()
        self.tegraflash_fill_mb1_storage_info(bct_dict['br_bct_file_name'])
        if values['--encrypt_key'] is not None:
            self.tegraflash_enc_and_sign_images()
        else:
            self.tegraflash_sign_images()
        self.tegraflash_update_br_bct_bl_info(bct_dict['br_bct_file_name'])
        self.tegraflash_update_images()
        self.tegraflash_generate_blob(True, 'blob.bin')
        self.tegraflash_send_to_bootrom(bct_dict)
        # sign image, and for coldboot as it needs to disable bit 'enable_flashing'
        self.tegraflash_send_to_bootloader(bct_dict, 'blob.bin')
        self.tegraflash_poll_applet_bl()

        # Perform coldboot flashing
        # Set the configuration to be cold boot configuration
        values['--cfg'] = values['--coldboot_pt_layout']
        self.tegraflash_generate_hpse_sb_pkg(values['--coldboot_bct_cfg'])
        self.tegraflash_create_secure_fip(values['--coldboot_pt_layout'])
        self.tegraflash_parse_partitionlayout()
        bct_dict = self.tegraflash_generate_bct(values['--coldboot_bct_cfg'], ['CONFIG_ENABLE_SC7'])
        self.tegraflash_generate_bpmp_mem_bin(values['--coldboot_bct_cfg'], flags=["BPMP_MEM_CFG"])
        self.tegraflash_process_binaries(bct_dict['mb2_bct_file_name'])
        self.tegraflash_parse_partitionlayout()
        self.tegraflash_fill_mb1_storage_info(bct_dict['br_bct_file_name'])
        self.tegraflash_sign_images()
        self.tegraflash_update_br_bct_bl_info(bct_dict['br_bct_file_name'])
        self.tegraflash_update_images()
        self.tegraflash_get_storage_info()
        self.tegraflash_flash_partitions(values['--skipsanitize'])
        self.tegraflash_flash_bct(bct_dict)
        info_print('Flashing completed\n')

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

    def tegraflash_flash_bct(self, bct_dict = {}):
        command = self.exec_file('tegradevflash')
        command.extend(['--write', 'BCT', bct_dict['br_bct_file_name']]);
        run_command(command)

        if bct_dict['mb1_bct_file_name'] is not None:
            mb1_bct_parts = getPart_name_by_type(values['--cfg'], 'mb1_boot_config_table')
            for name in mb1_bct_parts:
                command = self.exec_file('tegradevflash')
                command.extend(['--write', name, bct_dict['mb1_bct_file_name']]);
                run_command(command)

        if bct_dict['mem_bct_file_name'] is not None:
            mb1_bct_parts = getPart_name_by_type(values['--cfg'], 'mem_boot_config_table')
            for name in mb1_bct_parts:
                command = self.exec_file('tegradevflash')
                command.extend(['--write', name, bct_dict['mem_bct_file_name']]);
                run_command(command)

    def tegraflash_reboot(self, args):
        if values['--no_flash']:
            info_print('--no_flash option specified. Skip reboot %s' %(args[0]))
            return
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

    def tegraflash_process_binaries(self, mb2_bct_file_name):
        # After MB2 BCT is generated it needs to be appended to mb2 binary and
        # MB2 binary name needs to be updated in partition layout
        mb2_bin = get_partition_filename('mb2_bootloader', 'type')

        if (mb2_bin):
            mb2comb_bin = self.concatenate_mb2bct_mb2(mb2_bin, mb2_bct_file_name)
            if (mb2comb_bin):
                self.update_mb2comb_filename(mb2comb_bin)

        # Update dtb with odmdata and overlay dtb, then concatenate
        if values['--odmdata']:
            self.tegraflash_update_bpmp_dtb()
            self.tegraflash_update_cpubl_dtb()
        tegraflash_concat_overlay_dtb()

        if values['--concat_cpubl_bldtb'] is True:
            cpubl_bin = self.concatenate_cpubl_bldtb()
            self.update_cpublcomb_filename(cpubl_bin)

        self.get_dce_with_dtb_filename()

    def tegraflash_generate_rcmboot_blob(self, cmd_list = [], binary_list = []):
        # Create a blob directory. If exists, clear all files
        output_dir = tegraflash_abs_path(self.rcmboot_blob_dir)
        if os.path.exists(output_dir):
            shutil.rmtree(output_dir, ignore_errors=True)
        os.makedirs(output_dir)

        with open(self.rcmboot_cmd_file, 'w') as f:
            for cmd in cmd_list:
                f.write(cmd)
                f.write('\n')
        f.close()
        shutil.copyfile(self.rcmboot_cmd_file, output_dir + "/" + self.rcmboot_cmd_file)

        for bin in binary_list:
            shutil.copyfile(bin, output_dir + "/" + bin)

        tegrarcm_bin = self.tegraflash_binaries_v2['tegrarcm']
        shutil.copy2(tegrarcm_bin, output_dir)

        info_print("All RCM required files are saved in " + self.rcmboot_blob_dir + " folder")


    def tegraflash_rcmboot(self, args):
        if (args['--bct_flags_file'] is not None):
            flags = self.tegraflash_get_l4t_flags('rcm_boot', args['--bct_flags_file'], args['--overlay_bct_flags_file'])

        values.update(args)
        if values['--chip'] is None:
            print('Error: chip is not specified')
            return 1

        if values['--rcmboot_pt_layout'] is None:
            print('Error: RCM boot partition layout is not specified')
            return 1

        if values['--rcmboot_bct_cfg'] is None:
            print('Error: RCM boot bct cfg is not specified')
            return 1

        if values['--securedev']:
            if values['--bct'] is None:
                print('Error: BCT is not specified')
                return 1
            info_print('rcm boot with presigned binaries')
            # send these binary to BR
            command = self.exec_file('tegrarcm')
            command.extend(['--new_session'])
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            command.extend(['--uid'])
            command.extend(['--download', 'bct_br', values['--bct']])
            command.extend(['--download', 'mb1', values['--mb1']])
            command.extend(['--download', 'psc_bl1', values['--psc_bl1']])
            command.extend(['--download', 'bct_mb1', values['--mb1_bct']])
            run_command(command, True)

            time.sleep(10)
            # send these binary to BL
            command = self.exec_file('tegrarcm')
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            command.extend(['--pollbl'])
            command.extend(['--download', 'bct_mem', values['--membct_rcm']])
            command.extend(['--download', 'blob', values['--blob']])

            run_command(command)

        else:
            self.is_rcmboot = True
            values['--cfg'] = values['--rcmboot_pt_layout']

            self.tegraflash_get_key_mode()
            self.tegraflash_create_secure_fip(values['--rcmboot_pt_layout'])
            self.tegraflash_parse_partitionlayout()
            self.tegraflash_generate_hpse_sb_pkg(values['--rcmboot_bct_cfg'])
            bct_dict = self.tegraflash_generate_bct(values['--rcmboot_bct_cfg'], bct_flags=flags)
            self.tegraflash_generate_bpmp_mem_bin(values['--rcmboot_bct_cfg'], flags=flags)
            self.tegraflash_process_binaries(bct_dict['mb2_bct_file_name'])
            self.tegraflash_parse_partitionlayout()
            self.tegraflash_fill_mb1_storage_info(bct_dict['br_bct_file_name'])
            if values['--encrypt_key'] is None:
                 self.tegraflash_sign_images()
            else:
                self.tegraflash_enc_and_sign_images(bct_flag=True)

            self.tegraflash_update_br_bct_bl_info(bct_dict['br_bct_file_name'])
            self.tegraflash_update_images()
            self.tegraflash_generate_blob(True, 'blob.bin')
            bootrom_result = self.tegraflash_send_to_bootrom(bct_dict)
            # sign image, and for coldboot as it needs to disable bit 'enable_flashing'
            bootloader_result = self.tegraflash_send_to_bootloader(bct_dict, 'blob.bin')
            self.is_rcmboot = False # Reset
            if values['--no_flash']:
                rcmcmd1, bct_br, mb1, psc_bl1, bct_mb1 = bootrom_result
                rcmcmd2, mem_bcts, blob_file_name = bootloader_result
                # Create a rcmboot blob
                self.tegraflash_generate_rcmboot_blob(
                        [rcmcmd1, rcmcmd2],
                        [bct_br, mb1, psc_bl1, bct_mb1,
                         blob_file_name, *mem_bcts])
                return


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
        import pyfdt
        #  Supported config list
        uphy_config = ["uphy0-config", "uphy1-config", "mgbe0-speed", "mgbe1-speed",
                       "mgbe2-speed", "mgbe3-speed", "uphy0-eqos-speed", "hsstp-lane-map"]
        misc_config = ["pcie-c2-endpoint-enable", "pcie-c2-endpoint-use-int-refclk",
                       "pcie-c4-endpoint-enable", "pcie-c4-endpoint-use-int-refclk",
                       "pcie-c5-endpoint-enable", "pcie-c5-endpoint-use-int-refclk",
                       "force-ufs-init"]

        try:
            if values['--odmdata'] is None:
                return
            odm_list = strip_string_list(values['--odmdata'].strip().split(','))
            pcie_list = []
            remove_list = []

            # Find the pcie entries and remove them from odm list
            for i in range(0, len(odm_list)):
                if 'pcie@' in odm_list[i]:
                    pcie_list.append(odm_list[i])
                    remove_list.append(i)

            if remove_list != []:
                for i in range(len(remove_list)-1, -1, -1):
                    odm_list.pop(remove_list[i])
                # Update for future use that does not need pcie
                values['--odmdata'] = ','.join(odm_list)

            bpmp_dtb_in_layout = get_partition_filename('bpmp_fw_dtb', 'type')
            if bpmp_dtb_in_layout == None:
                info_print('bpmp_dtb does not exist')
                return

            bpmp_dtb = bpmp_dtb_in_layout;

            # Create the backup dtb
            bpmp_dtb = tegraflash_create_backup_file(bpmp_dtb, '_with_odm')
            if bpmp_dtb_in_layout != None:
                set_partition_filename('bpmp_fw_dtb', bpmp_dtb, 'type')

            with open(bpmp_dtb_in_layout, 'rb') as infile:
                dtb = pyfdt.FdtBlobParse(infile)
            fdt = dtb.to_fdt()

            # Handle pcie entries
            if pcie_list != []:
                pcie_root = fdt.resolve_path("/pcie")
                if not pcie_root:
                    pcie_root = pyfdt.FdtNode('pcie')
                    root_node = fdt.resolve_path("/")
                    root_node.append(pcie_root)
                for pcie in pcie_list:
                    # Example: pcie@1_status=okay_pcie-id=1_max-link-speed=4
                    prop_list = pcie.split('_')
                    pcie_node_str = prop_list.pop(0)
                    pcie_node = fdt.resolve_path('/pcie/' + pcie_node_str)
                    if not pcie_node:
                        try:
                            pcie_node = pyfdt.FdtNode(pcie_node_str)
                            pcie_root.append(pcie_node)
                        except Exception as e:
                            info_print('Encounter exception: {} when trying to insert {}'
                                .format(e, pcie_node_str))
                            pass

                    for prop in prop_list:
                        token_list = prop.split('=')
                        try:
                            pcie_node.remove(token_list[0])
                        except:
                            pass
                        if token_list[1].isdigit():
                            pcie_node.insert(0, pyfdt.FdtPropertyWords(token_list[0], [int(token_list[1])]))
                        else:
                            pcie_node.insert(0, pyfdt.FdtPropertyStrings(token_list[0], [token_list[1]]))

            # Handle uphy entries
            uphy_node = fdt.resolve_path("/uphy")
            if not uphy_node:
                uphy_node = pyfdt.FdtNode('uphy')
                root_node = fdt.resolve_path("/")
                root_node.append(uphy_node)

            for prop in odm_list:
                found = False
                # Handle boolean uphy nodes either by deleting or inserting
                for misc in misc_config:
                    if prop == misc + '-del':
                        try:
                            uphy_node.remove(misc)
                            found = True
                        except:
                            pass
                        finally:
                            break
                    elif misc == prop:
                        misc_node_str = '/uphy/' + misc
                        misc_node = fdt.resolve_path(misc_node_str)
                        if not misc_node:
                            uphy_node.insert(0, pyfdt.FdtProperty(misc))
                        found = True
                        break
                if found == True:
                    continue

                # Handle value-based uphy nodes either deleting or updating value
                for cfg in uphy_config:
                    if prop == cfg + '-del':
                        try:
                            uphy_node.remove(cfg)
                        except:
                            pass
                        finally:
                            break
                    elif prop.startswith(cfg) and re.match(r'{}-\d+$'.format(cfg), prop):
                        try:
                            uphy_node.remove(cfg)
                        except:
                            pass
                        val = int(prop.rsplit('-', 1)[1])
                        uphy_node.insert(0, pyfdt.FdtPropertyWords(cfg, [val]))

            with open(bpmp_dtb,'wb') as outfile:
                outfile.write(fdt.to_dtb())

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
                command.extend(["-c", "t264"])
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

    def tegraflash_generate_mb1_bct(self, configs_dict, bct_flags=[]):
        info_print('Generating mb1-bct')

        # Check if custom MB1 BCT is provided by the user
        # If so, do not generate the MB1 BCT from the config
        # but only sign and encrypt the BCT binary
        if (values["--enable_mods"]) is not None:
            bct_flags.append('DISABLE_PREPROD_FIREWALLS')

        if values['--external_device']:
           info_print('External device is not supported for mb1-bct generation')
           return None

        bct_flags = bct_flags[:]
        bct_flags.append('IN_DTS_CONTEXT')

        if (values["--mb1_bct"] is not None and values["--mb1_bct"].endswith(".bct")):
            self.tegrabct_values['--mb1_bct'] = values["--mb1_bct"]
            mb1_bct_file_name = values["--mb1_bct"]
        else:

            if values['--mb1_bct'] is None:
                values['--mb1_bct'] = 'mb1_bct.cfg'

            target_conf = self.tegraflash_generate_targetconfig(configs_dict)
            BootburnMb1Bct(target_conf, os.getcwd(), values['--mb1_bct'], values['--fb']).generateBct(bct_flags)
            mb1_bct_file_name = os.path.splitext(values['--mb1_bct'])[0] + '_MB1.bct'

            if self.tegraparser_values['--pt'] is not None:
                info_print('Updating mb1-bct with firmware information')
                command = self.exec_file('tegrabct')
                command.extend(['--chip', values['--chip'], values['--chip_major']])
                command.extend(['--mb1bct', mb1_bct_file_name])
                command.extend(['--updatefwinfo', self.tegraparser_values['--pt']])
                run_command(command)

        if values['--encrypt_key'] is not None:
            mb1_bct_file_name = self.tegraflash_oem_enc_and_sign_file(
                mb1_bct_file_name, 'MBCT')
        else:
            mb1_bct_file_name = self.tegraflash_oem_sign_file(
                mb1_bct_file_name, 'MBCT')

        # Need to update pt name if the file name is changed
        # Check if partition table is available before attempting update
        if self.tegraparser_values['--pt'] is not None:
            found_name = get_partition_filename('mb1_boot_config_table', 'type')
            if found_name != mb1_bct_file_name:
                set_partition_filename('mb1_boot_config_table', mb1_bct_file_name, 'type')
        else:
            info_print('Partition table not available, skipping MB1 BCT filename update in partition layout')

        return mb1_bct_file_name


    def tegraflash_oem_enc_fskp(self, in_file):
        info_print('Encrypting ' + in_file)
        FSKP_EK = '4D2D52D8CE2D479A05E2225F8B3B58DC038FC790EAAAA0FDE5EE7A286E8AD72A'
        fskp_ek = 'fskp_ek.key'
        with open(fskp_ek, 'wb') as f:
            f.write(str_to_hex(FSKP_EK))

        len_offset = 5124
        len_buf = bytearray(4)
        payload_len = 0

        tag2_offset = 5200
        tag2_size = 16

        iv2_offset = 5188
        iv2_size = 12

        aad2_offset = 5120
        aad2_size = tag2_offset - aad2_offset #80

        with open(in_file, 'rb') as f:
            src = bytearray(f.read())
            len_buf[:] = src[len_offset:len_offset+4]
            payload_len = struct.unpack('<I', len_buf)[0]
            iv2 = src[iv2_offset:iv2_offset+iv2_size]
            aad2 = src[aad2_offset:aad2_offset+aad2_size]
            self.call_tegrasign(
                in_file, None, None, fskp_ek, str(payload_len), None, '8192', None, None, 'aesgcm', True, hex_to_str(iv2), hex_to_str(aad2), hex_to_str(bytearray(tag2_size)))

        enc_file = os.path.splitext(in_file)[0] + '_encrypt' + os.path.splitext(in_file)[1]
        with open(enc_file, 'rb+') as f:
            src = bytearray(f.read())
            tag_file_name = os.path.splitext(in_file)[0] + '.tag'
            with open(tag_file_name, 'rb') as tag_f:
                src[tag2_offset:tag2_offset+tag2_size] = bytearray(tag_f.read())
            f.seek(0)
            f.write(src)

        return enc_file

    def tegraflash_oem_sign_fskp(self, in_file):
        enc_file = self.tegraflash_oem_enc_fskp(in_file)

        info_print('Signing ' + enc_file)
        FSKP_AK = '6452CC02472038611798C79773260A582A1FE3990C04B0E98B06874935A70486'
        fskp_ak = 'fskp_ak.key'
        with open(fskp_ak, 'wb') as f:
            f.write(str_to_hex(FSKP_AK))

        len_offset = 5124
        len_buf = bytearray(4)
        len_buf = bytearray(4)
        sha_offset = 5216
        sha_size = 64
        payload_len = 0

        # Do the binary digest first
        with open(enc_file, 'rb+') as f:
            src = bytearray(f.read())
            len_buf[:] = src[len_offset:len_offset+4]
            payload_len = struct.unpack('<I', len_buf)[0]

            self.call_tegrasign(
                enc_file, None, None, None, str(payload_len), None, '8192', None, 'sha512', None)
            sha_file_name = os.path.splitext(enc_file)[0] + '.sha'
            # Save the digest value
            with open(sha_file_name, 'rb') as sha_f:
                src[sha_offset:sha_offset+sha_size] = bytearray(sha_f.read())
            f.seek(0)
            f.write(src)

        # Do the stage2 hmac next after binary update
        self.call_tegrasign(
            enc_file, None, None, fskp_ak, str(8192 - 4032), None, '4032', None, None, None, True, 0, 0, 0, 'hmacsha256')

        signed_file = os.path.splitext(enc_file)[0] + '.signed'
        shutil.copyfile(enc_file, signed_file)

        # Do the stage2 digest to stage2 sig digest
        self.call_tegrasign(
            enc_file, None, None, None, str(8192 - 4032), None, '4032', None, 'sha512', None)

        # Save hmac value & digest value
        offset = 144
        off_size = 32
        sha_offset = 80
        sha_size = 64
        with open(signed_file, 'rb+') as f:
            src = bytearray(f.read())
            hash_file_name = os.path.splitext(enc_file)[0] + '.hash'
            with open(hash_file_name, 'rb') as hash_f:
                src[offset:offset+off_size] = bytearray(hash_f.read())
            sha_file_name = os.path.splitext(enc_file)[0] + '.sha'
            with open(sha_file_name, 'rb') as sha_f:
                src[sha_offset:sha_offset+sha_size] = bytearray(sha_f.read())

            f.seek(0)
            f.write(src)

        # Do bch digest and save it
        sha_offset = 4
        self.call_tegrasign(
            signed_file, None, None, None, str(8192 - 68), None, '68', None, 'sha512', None)
        with open(signed_file, 'rb+') as f:
            src = bytearray(f.read())
            sha_file_name = os.path.splitext(enc_file)[0] + '.sha'
            # Save the bch diget
            with open(sha_file_name, 'rb') as sha_f:
                src[sha_offset:sha_offset+sha_size] = bytearray(sha_f.read())
            f.seek(0)
            f.write(src)

        return signed_file

    def tegraflash_oem_sign_file(self, in_file, magic_id, partition_type=None):
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
        elif mode == 'ec521':
            mode = 'oem-ecc521'
        elif mode == 'eddsa':
            mode = 'oem-eddsa'
        elif mode == 'xmss':
            mode = 'oem-xmss'

        command = self.exec_file('tegrahost')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        # fixme : right magicid needs to be passed in recovery path
        command.extend(['--magicid', magic_id])
        # External device doesn't need ratchet
        if not values['--external_device']:
            if values['--minratchet_config'] is not None and values['--rcmboot_bct_cfg'] is not None:
                self.tegraflash_generate_ratchet_blob()
                command.extend(['--ratchet_blob',
                                self.tegrahost_values['--ratchet_blob']])
        # Special handling for compressed image
        if values['--compress'] is not None and (partition_type == values['--compress']):
            command.extend(['--appendsigheader', filename, mode, 'compress'])
            # The finished file name will have name change
            filename = os.path.splitext(filename)[0] + '_aligned_blob_w_bin' \
                +  os.path.splitext(filename)[1]
        else:
            command.extend(['--appendsigheader', filename, mode])
        if values['--ecid'] is not None:
            command.extend(['--ecid', values['--ecid']])

        run_command(command)
        filename = os.path.splitext(
            filename)[0] + '_sigheader' + os.path.splitext(filename)[1]

        # Special handling for FSKP
        if (magic_id == 'FSKP'):
            return self.tegraflash_oem_sign_fskp(filename)

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
        algo_list = {'pkc':'oem-rsa', 'ec':'oem-ecc', 'ec521':'oem-ecc521',
            'eddsa':'oem-eddsa', 'xmss':'oem-xmss'}
        for algo in algo_list.keys():
            node = ElementTree.SubElement(child, algo)
            node.set('signature', filename + '.sig')
            node.set('signed_file', filename + '.signed')
            if algo in ['pkc', 'ec', 'ec521']:
                node.set('digest_type', 'sha512')

        sign_tree = ElementTree.ElementTree(root)
        sign_tree.write(filename + '_list.xml')

        key_val = values['--key']
        list_val = filename + '_list.xml'
        pkh_val = self.tegrasign_values['--pubkeyhash']
        if values['--hsm'] is True:
            l4t_hsm_str = self.parse_hsm_l4t_key_label(key_val)
            self.call_tegrasign(None, None, None, None, None,
                                list_val, None, pkh_val, 'sha512', None,
                                False, 0, 0, 0, None,
                                0, None, l4t_hsm_str)
        else:
            self.call_tegrasign(None, None, None, key_val, None,
                                list_val, None, tegraflash_pubkeyhash_for_sign(key_val, pkh_val), 'sha512', None)
        sign_xml_file = filename + '_list_signed.xml'
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
        # Keep only one "_aligned" in the file name
        newname = signed_file.replace('_aligned', '', 1)
        shutil.copyfile(signed_file, newname)
        signed_file = newname
        return signed_file

    def tegraflash_oem_enc_and_sign_file(self, in_file, magic_id, partition_type = None, sz_meta_blob = 0):
        in_file = os.path.basename(in_file)
        filename = in_file
        info_print(filename)

        info_print('Encrypting and signing ' + in_file)
        is_aligned = False
        algo_list = {'pkc':'oem-rsa', 'ec':'oem-ecc', 'ec521':'oem-ecc521',
            'eddsa':'oem-eddsa', 'xmss':'oem-xmss'}

        # Case 1: File has no BCH header present
        # Case 2: Firmware magic IDs that may lack stage2 info
        if not self._is_header_present(filename) or (magic_id in self.QUIRK_MAGIC_ID_LIST_LOADED_BY_MB2):
            aligned_file = os.path.splitext(
                filename)[0] + '_aligned' + os.path.splitext(filename)[1]
            if os.path.exists(in_file):
                shutil.copyfile(in_file, aligned_file)
            command = self.exec_file('tegrahost')
            command.extend(['--chip', values['--chip']])
            command.extend(['--align', aligned_file])
            run_command(command)
            is_aligned = True
            filename = aligned_file
            mode = self.tegrasign_values['--mode']
            if mode in algo_list:
                mode = algo_list[mode]

            command = self.exec_file('tegrahost') # stage1.sha, stage2.sha, bch.sha
            # Special handling for compressed image
            if values['--compress'] is not None and (partition_type == values['--compress']):
                command.extend(['--appendsigheader', filename, mode, 'compress', self.tegrahost_values['--meta_blob']])
                # The finished file name will have name change
                filename = os.path.splitext(filename)[0] + '_aligned_blob_w_bin' \
                    +  os.path.splitext(filename)[1]
            else:
                command.extend(['--appendsigheader', filename, mode])
            command.extend(['--chip', values['--chip'],
                        values['--chip_major']])
            # fixme : right magicid needs to be passed in recovery path
            command.extend(['--magicid', magic_id])
            # External device doesn't need ratchet
            if not values['--external_device']:
                if values['--minratchet_config'] is not None and values['--rcmboot_bct_cfg'] is not None:
                    self.tegraflash_generate_ratchet_blob()
                    command.extend(['--ratchet_blob',
                                    self.tegrahost_values['--ratchet_blob']])
            if values['--ecid'] is not None:
                command.extend(['--ecid', values['--ecid']])
            run_command(command)

            # Special handling for compressed image to get meta blob size
            if values['--compress'] is not None and (partition_type == values['--compress']):
                with open(self.tegrahost_values['--meta_blob'], "r") as file:
                    content = file.read()

                lines = content.split("\n")
                for line in lines:
                    if line.startswith("METABLOBSIZE"):
                        sz_meta_blob_str = line.split(":")[1].strip()
                        try:
                            sz_meta_blob = int(sz_meta_blob_str)
                        except ValueError:
                            tegraflash_exception(
                                    'Error: Failed to get sz_meta_blob of ' + filename)

            filename = os.path.splitext(
                filename)[0] + '_sigheader' + os.path.splitext(filename)[1]

        enc_file = self.tegraflash_oem_enc(filename, False, sz_meta_blob)

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
            if algo in ['pkc', 'ec', 'ec521']:
                node.set('digest_type', 'sha512')

        sign_tree = ElementTree.ElementTree(root)
        sign_tree.write(enc_file + '_list.xml')

        key_val = values['--key']
        list_val = enc_file + '_list.xml'
        pkh_val = self.tegrasign_values['--pubkeyhash']
        if values['--hsm'] is True:
            l4t_hsm_str = self.parse_hsm_l4t_key_label(key_val)
            self.call_tegrasign(None, None, None, None, None,
                                list_val, None, pkh_val, 'sha512', None,
                                False, 0, 0, 0, None,
                                0, None, l4t_hsm_str)
        else:
            self.call_tegrasign(None, None, None, key_val, None,
                                list_val, None, tegraflash_pubkeyhash_for_sign(key_val, pkh_val), 'sha512', None)
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
            # Keep only one "_aligned" in the file name
            newname = signed_file.replace('_aligned', '', 1)
            shutil.copyfile(signed_file, newname)
            signed_file = newname
        return signed_file

    def tegraflash_update_pt_name(self, pt_name, new_name, cfg_file=None, pt_file=None):
        skip_types = ['boot_config_table', 'mb2_applet', ]
        pt_base, pt_ext = os.path.splitext(pt_name)
        if cfg_file == None:
            cfg_file = values['--cfg']
        if pt_file == None:
            pt_file = self.tegraparser_values['--pt']

        with open(cfg_file, 'rt') as file:
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
                    command.extend(['--pt', pt_file])
                    command.extend(['--update_part_filename', part_name, part_type, new_name])
                    run_command(command)

    def tegraflash_enc_and_sign_images(self, ovewrite_xml=True, bct_flag=False, cfg_file=None, pt_file=None):

        info_print('Creating list of images to be encrypted and signed')
        tmp_files = {}
        if pt_file == None:
            pt_file = self.tegraparser_values['--pt']

        self.tegraflash_generate_signing_list(pt_file)

        with open(self.tegrahost_values['--list'], 'rt') as file:
            xml_tree = ElementTree.parse(file)
            mode = xml_tree.getroot().get('mode')

            for file_nodes in xml_tree.iter('file'):
                filename = file_nodes.get('name')
                is_compress = file_nodes.get('compress')
                if is_compress == '1':
                    is_compress = 4096
                enc_file = self.tegraflash_oem_enc(filename, bct_flag, int(is_compress))
                self.tegraflash_update_pt_name(filename, enc_file, cfg_file, pt_file)
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
        if values['--hsm'] is True:
            l4t_hsm_str = self.parse_hsm_l4t_key_label(key_val)
            self.call_tegrasign(None, None, None, None, None,
                                list_val, None, pkh_val, 'sha512', None,
                                False, 0, 0, 0, None,
                                0, None, l4t_hsm_str)
        else:
            self.call_tegrasign(None, None, None, key_val, None,
                                list_val, None, tegraflash_pubkeyhash_for_sign(key_val, pkh_val), 'sha512', None)
        return

    def generate_mem_bcts(self, configs_dict, bct_flags=[]):
        if 'sdram' not in  configs_dict:
            info_print('Error: Skip generating mem_bct because sdram is not defined')
            return []

        info_print('Generating mem-bct')
        bct_flags.append('IN_DTS_CONTEXT')
        target_config = self.tegraflash_generate_targetconfig(configs_dict)
        BootburnMemBct(target_config, os.getcwd()).generateBct(bct_flags)

        return target_config.filelist["f_MembctBin"]

    def generate_bpmp_mem_bins(self, configs_dict, bct_flags=[]):
        info_print('Generating BPMP memory configuration binary')

        if 'bpmp_mem_cfg' not in  configs_dict:
            info_print('Error: Skip generating bpmp mem bct because bpmp_mem_cfg is not defined')
            return []

        target_config = self.tegraflash_generate_targetconfig(configs_dict)
        bct_flags.append('IN_DTS_CONTEXT')
        BootburnBpmpMemBct(target_config, os.getcwd()).generateBct(bct_flags)
        return target_config.filelist["f_BpmpMembctBin"]

    def tegraflash_generate_rcm_mem_bct(self, configs_dict, bct_flags=[]):
        mem_bcts = self.generate_mem_bcts(configs_dict, bct_flags)

        if (len(mem_bcts) == 0):
            return

        chip_info = tegraflash_abs_path(
            self.tegrarcm_values['--chip_info'])
        # Select 1 bct based on RAMCODE

        ramcode = self.tegraflash_get_ramcode()

        info_print("Using ramcode " + str(ramcode))

        ramcode = ramcode // 2
        membctName = ''
        all_mem_bcts = []

        if values['--encrypt_key'] is not None:
            membctName = "membct_" + str(ramcode)
            shutil.copyfile(mem_bcts[ramcode], membctName + '.bct')
            self.tegrabct_values['--membct_rcm'] = self.tegraflash_oem_enc_and_sign_file(
                membctName + '.bct', 'MEM' + str(ramcode))
            for i in range(0,8):
                shutil.copyfile(mem_bcts[i], 'membct_' + str(i) + '.bct')
                all_mem_bcts.append(self.tegraflash_oem_enc_and_sign_file('membct_' + str(i) + '.bct', 'MEM' + str(i)))
        else:
            membctName = "membct_" + str(ramcode)
            shutil.copyfile(mem_bcts[ramcode], membctName + '.bct')
            self.tegrabct_values['--membct_rcm'] = self.tegraflash_oem_sign_file(
                membctName + '.bct', 'MEM' + str(ramcode))
            for i in range(0,8):
                shutil.copyfile(mem_bcts[i], 'membct_' + str(i) + '.bct')
                all_mem_bcts.append(self.tegraflash_oem_sign_file('membct_' + str(i) + '.bct', 'MEM' + str(i)))

        return self.tegrabct_values['--membct_rcm'], all_mem_bcts

    def tegraflash_generate_coldboot_mem_bct(self, configs_dict, bct_flags=[]):

        mem_bcts = self.generate_mem_bcts(configs_dict, bct_flags)

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
        command.extend(['--addsigheader_multi',
            mem_bcts[0], mem_bcts[1], mem_bcts[2], mem_bcts[3],
            mem_bcts[4], mem_bcts[5], mem_bcts[6], mem_bcts[7]
        ])
        run_command(command)
        filename, _ = os.path.splitext(mem_bcts[0])
        mem_file = 'mem_coldboot.bct'
        os.rename(filename + '_sigheader.bct', mem_file)
        if values['--encrypt_key'] is not None:
            membct_file_name = self.tegraflash_oem_enc_and_sign_file(mem_file, 'MEMB')
        else:
            membct_file_name = self.tegraflash_oem_sign_file(mem_file, 'MEMB')

        # Need to update pt name since file name is changed
        found_name = get_partition_filename('mem_boot_config_table', 'type')
        if found_name != membct_file_name:
            set_partition_filename('mem_boot_config_table', membct_file_name, 'type')

        return membct_file_name

    def tegraflash_update_images(self, pt_file=None):
        info_print('Copying signatures')
        if pt_file == None:
            pt_file = self.tegraparser_values['--pt']

        command = self.exec_file('tegrahost')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--partitionlayout', pt_file])
        command.extend(['--updatesig', self.tegrahost_values['--signed_list']])

        if os.path.isfile(self.tegrasign_values['--pubkeyhash']):
            command.extend(
                ['--pubkeyhash', self.tegrasign_values['--pubkeyhash']])

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

    def tegraflash_softfuses(self, args, fuse_args):
        values.update(args)

        info_print('Applying soft fuses')

        if values['--securedev']:
            print('Error: read partition with --securedev not support yet')
            return
        if not self.check_is_mb2applet():
            values['--cfg'] = values['--rcmboot_pt_layout']
            self.tegraflash_get_key_mode()
            self.tegraflash_generate_hpse_sb_pkg(values['--rcmboot_bct_cfg'])
            self.tegraflash_parse_partitionlayout()
            bct_dict = self.tegraflash_generate_bct(values['--rcmboot_bct_cfg'])
            self.tegraflash_generate_bpmp_mem_bin(values['--rcmboot_bct_cfg'], flags=["BPMP_MEM_CFG"])

            self.tegraflash_process_binaries(bct_dict['mb2_bct_file_name'])
            self.tegraflash_parse_partitionlayout()
            self.tegraflash_fill_mb1_storage_info(bct_dict['br_bct_file_name'])
            if values['--encrypt_key'] is not None:
                self.tegraflash_enc_and_sign_images()
            else:
                self.tegraflash_sign_images()
            self.tegraflash_update_br_bct_bl_info(bct_dict['br_bct_file_name'])
            self.tegraflash_update_images()
            self.tegraflash_generate_blob(True, 'blob.bin')
            self.tegraflash_send_to_bootrom(bct_dict)
            self.tegraflash_boot_mb2_applet(bct_dict)

        if not self.check_is_mb2applet():
            info_print('Error: mb2 applet is not running\n')
            return

        filename = os.path.splitext(fuse_args[0])
        if filename[1] != '.xml':
            raise tegraflash_exception("Not an xml file")
        info_print('Parsing fuse info as per xml file')
        command = self.exec_file('tegraparser')
        command.extend(['--fuse_info', fuse_args[0], self.tegrarcm_values['--fuse_info']])
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        run_command(command)

        command = self.exec_file('tegrarcm')
        command.extend(['--oem', 'softfuses'])
        command.extend([self.tegrarcm_values['--fuse_info']])
        run_command(command)

    def tegraflash_readfuses(self, args, read_args):
        values.update(args)

        info_print('Reading fuses')

        if values['--securedev']:
            print('Error: read partition with --securedev not support yet')
            return
        if not self.check_is_mb2applet():
            values['--cfg'] = values['--rcmboot_pt_layout']
            self.tegraflash_get_key_mode()
            self.tegraflash_parse_partitionlayout()
            bct_dict = self.tegraflash_generate_bct(values['--rcmboot_bct_cfg'])
            self.tegraflash_generate_bpmp_mem_bin(values['--rcmboot_bct_cfg'], flags=["BPMP_MEM_CFG"])

            self.tegraflash_process_binaries(bct_dict['mb2_bct_file_name'])
            self.tegraflash_parse_partitionlayout()
            self.tegraflash_fill_mb1_storage_info(bct_dict['br_bct_file_name'])
            if values['--encrypt_key'] is not None:
                self.tegraflash_enc_and_sign_images()
            else:
                self.tegraflash_sign_images()
            self.tegraflash_update_br_bct_bl_info(bct_dict['br_bct_file_name'])
            self.tegraflash_update_images()
            self.tegraflash_send_to_bootrom(bct_dict)
            self.tegraflash_boot_mb2_applet(bct_dict)

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
        self.tegraflash_parse_partitionlayout()
        bct_dict = self.tegraflash_generate_bct(values['--rcmboot_bct_cfg'])
        self.tegraflash_process_binaries(bct_dict['mb2_bct_file_name'])
        self.tegraflash_parse_partitionlayout()
        self.tegraflash_fill_mb1_storage_info(bct_dict['br_bct_file_name'])
        if values['--encrypt_key'] is not None:
            self.tegraflash_enc_and_sign_images()
        else:
            self.tegraflash_sign_images()
        self.tegraflash_update_br_bct_bl_info(bct_dict['br_bct_file_name'])
        self.tegraflash_update_images()
        self.tegraflash_generate_blob(True, 'blob.bin')
        self.tegraflash_send_to_bootrom(bct_dict)
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

        if values['--securedev']:
            info_print('Error: write partition with --securedev not support yet')
            return
        if not self.check_ismb2():
            # Set the initial configuration to be rcm boot configuration
            values['--cfg'] = values['--rcmboot_pt_layout']
            self.tegraflash_get_key_mode()
            self.tegraflash_generate_hpse_sb_pkg(values['--rcmboot_bct_cfg'])
            self.tegraflash_parse_partitionlayout()
            bct_dict = self.tegraflash_generate_bct(values['--rcmboot_bct_cfg'], bct_flags=['ENABLE_FLASHING'])
            self.tegraflash_generate_bpmp_mem_bin(values['--rcmboot_bct_cfg'], flags=["BPMP_MEM_CFG"])

            self.tegraflash_process_binaries(bct_dict['mb2_bct_file_name'])
            self.tegraflash_parse_partitionlayout()
            self.tegraflash_fill_mb1_storage_info(bct_dict['br_bct_file_name'])
            if values['--encrypt_key'] is not None:
                self.tegraflash_enc_and_sign_images()
            else:
                self.tegraflash_sign_images()
            self.tegraflash_update_br_bct_bl_info(bct_dict['br_bct_file_name'])
            self.tegraflash_update_images()
            self.tegraflash_generate_blob(True, 'blob.bin')
            self.tegraflash_send_to_bootrom(bct_dict)
            # sign image, and for coldboot as it needs to disable bit 'enable_flashing'
            self.tegraflash_send_to_bootloader(bct_dict, 'blob.bin')
            self.tegraflash_poll_applet_bl()

        values['--cfg'] = values['--coldboot_pt_layout']
        self.tegraflash_parse_partitionlayout()
        if partition_name in ['BCT', 'MB1_BCT', 'MEM_BCT']:
            bct_dict = self.tegraflash_generate_bct(values['--coldboot_bct_cfg'], bct_flags=['ENABLE_FLASHING', 'CONFIG_ENABLE_SC7'])
        partition_type = self.get_partition_partition_type(partition_name.lower())
        magic_id = self.tegraflash_get_magicid(partition_type)

        info_print(partition_name  + ' ' + partition_type + ', magic id = ' + magic_id)
        filename = self.tegraflash_concat_partition(partition_name, filename, values['--coldboot_bct_cfg'])

        # Handle special partitions that does not need sign here since already signed
        if partition_name == 'BCT':
            info_print("Updating BCT with the BCT generated by tegraflash")
            self.tegraflash_fill_mb1_storage_info(bct_dict['br_bct_file_name'])
            self.tegraflash_update_br_bct_bl_info(bct_dict['br_bct_file_name'])
            signed_file = bct_dict['br_bct_file_name']
        else:
            if values['--encrypt_key'] is not None:
                signed_file = self.tegraflash_oem_enc_and_sign_file(filename, magic_id, partition_type)
            else:
                signed_file = self.tegraflash_oem_sign_file(filename, magic_id, partition_type)
        self.tegraflash_erase_partition(partition_name)
        self.tegraflash_write_partition('tegradevflash', partition_name, signed_file)

    # This operates on the file based on the partition name, but does not update the partitionlayout
    # returns the filename that is concatenated on
    def tegraflash_concat_partition(self, partition_name, filename, bct_cfg_file=None):
        cpu_bl_list = ['A_cpu-bootloader', 'B_cpu-bootloader']
        dce_list = ['dce-fw', 'A_dce-fw', 'B_dce-fw']
        mb2_list = ['mb2', 'A_mb2', 'B_mb2']

        if partition_name in cpu_bl_list:
            # Check if --concat_cpubl_bldtb is specified, then apply --overlay_dtb
            if values['--concat_cpubl_bldtb'] is True:
                bl_dtb_file = values['--bldtb']
                cpubl_bin_file = filename
                cpubl_with_dtb = cpubl_bin_file.split('.', 1)[0] + '_with_dtb.bin'
                info_print('Concatenating bl dtb:(' + bl_dtb_file + '), to cpubl binary: ' + cpubl_bin_file)
                if not os.path.exists(bl_dtb_file):
                    raise tegraflash_exception('Could not find ' + bl_dtb_file)
                if not os.path.exists(cpubl_bin_file):
                    raise tegraflash_exception('Could not find ' + cpubl_bin_file)
                shutil.copyfile(cpubl_bin_file, cpubl_with_dtb)
                concat_file(cpubl_with_dtb, bl_dtb_file)  # order: outfile, infile
                filename = cpubl_with_dtb
            if values['--overlay_dtb']:
                dtb_files = [f.strip() for f in values['--overlay_dtb'].split(',') if f.strip()]
                concat_file_4k(filename, dtb_files)

        elif partition_name in dce_list:
            filename = self.get_dce_with_dtb_filename(filename, update_cfg=False)

        elif partition_name in mb2_list:
            if bct_cfg_file == None:
                info_print('bctcfg file is not specified hence creating mb2bct is skipped for: ' + filename)
                return filename
            configs_dict = self.get_bct_configs_dict(bct_cfg_file)
            mb2_bct_file = self.tegraflash_generate_mb2_bct(configs_dict)
            filename = self.concatenate_mb2bct_mb2(filename, mb2_bct_file)

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
        if not self.check_ismb2():
            values['--cfg'] = values['--rcmboot_pt_layout']
            self.tegraflash_get_key_mode()
            self.tegraflash_generate_hpse_sb_pkg(values['--rcmboot_bct_cfg'])
            self.tegraflash_parse_partitionlayout()
            bct_dict = self.tegraflash_generate_bct(values['--rcmboot_bct_cfg'], bct_flags=['ENABLE_FLASHING'])
            self.tegraflash_generate_bpmp_mem_bin(values['--rcmboot_bct_cfg'], flags=["BPMP_MEM_CFG"])

            self.tegraflash_process_binaries(bct_dict['mb2_bct_file_name'])
            self.tegraflash_parse_partitionlayout()
            self.tegraflash_fill_mb1_storage_info(bct_dict['br_bct_file_name'])
            if values['--encrypt_key'] is not None:
                self.tegraflash_enc_and_sign_images()
            else:
                self.tegraflash_sign_images()
            self.tegraflash_update_br_bct_bl_info(bct_dict['br_bct_file_name'])
            self.tegraflash_update_images()
            self.tegraflash_generate_blob(True, 'blob.bin')
            self.tegraflash_send_to_bootrom(bct_dict)
            # sign image, and for coldboot as it needs to disable bit 'enable_flashing'
            self.tegraflash_send_to_bootloader(bct_dict, 'blob.bin')
            self.tegraflash_poll_applet_bl()

        self.tegraflash_erase_partition(partition_name)
        self.tegraflash_write_partition('tegradevflash', partition_name, filename)

    def tegraflash_erase_partition(self, partition_name):
        info_print('Erasing partition')
        command = self.exec_file('tegradevflash')
        command.extend(['--erase', partition_name])
        run_command(command)

    def tegraflash_erase(self, args, partition_name):
        values.update(args)

        if values['--securedev']:
            info_print('Error: write partition with --securedev not support yet')
            return
        if not self.check_ismb2():
            # Perform rcm boot for flashing
            # Set the initial configuration to be rcm boot configuration
            values['--cfg'] = values['--rcmboot_pt_layout']
            self.tegraflash_get_key_mode()
            self.tegraflash_parse_partitionlayout()
            bct_dict = self.tegraflash_generate_bct(values['--rcmboot_bct_cfg'], bct_flags=['ENABLE_FLASHING'])
            self.tegraflash_generate_bpmp_mem_bin(values['--rcmboot_bct_cfg'], flags=["BPMP_MEM_CFG"])

            self.tegraflash_process_binaries(bct_dict['mb2_bct_file_name'])
            self.tegraflash_parse_partitionlayout()
            self.tegraflash_fill_mb1_storage_info(bct_dict['br_bct_file_name'])
            if values['--encrypt_key'] is not None:
                self.tegraflash_enc_and_sign_images()
            else:
                self.tegraflash_sign_images()
            self.tegraflash_update_br_bct_bl_info(bct_dict['br_bct_file_name'])
            self.tegraflash_update_images()
            self.tegraflash_generate_blob(True, 'blob.bin')
            self.tegraflash_send_to_bootrom(bct_dict)
            # sign image, and for coldboot as it needs to disable bit 'enable_flashing'
            self.tegraflash_send_to_bootloader(bct_dict, 'blob.bin')
            self.tegraflash_poll_applet_bl()

        self.tegraflash_erase_partition(partition_name)

    def tegraflash_read(self, args, partition_name, filename):
        values.update(args)

        if values['--securedev']:
            info_print('Error: read partition with --securedev not support yet')
            return
        if not self.check_ismb2():
            # Perform rcm boot for flashing
            # Set the initial configuration to be rcm boot configuration
            values['--cfg'] = values['--rcmboot_pt_layout']
            self.tegraflash_get_key_mode()
            self.tegraflash_parse_partitionlayout()
            bct_dict = self.tegraflash_generate_bct(values['--rcmboot_bct_cfg'], bct_flags=['ENABLE_FLASHING'])
            self.tegraflash_generate_bpmp_mem_bin(values['--rcmboot_bct_cfg'], flags=["BPMP_MEM_CFG"])

            self.tegraflash_process_binaries(bct_dict['mb2_bct_file_name'])
            self.tegraflash_parse_partitionlayout()
            self.tegraflash_fill_mb1_storage_info(bct_dict['br_bct_file_name'])
            if values['--encrypt_key'] is not None:
                self.tegraflash_enc_and_sign_images()
            else:
                self.tegraflash_sign_images()
            self.tegraflash_update_br_bct_bl_info(bct_dict['br_bct_file_name'])
            self.tegraflash_update_images()
            self.tegraflash_generate_blob(True, 'blob.bin')
            self.tegraflash_send_to_bootrom(bct_dict)
            # sign image, and for coldboot as it needs to disable bit 'enable_flashing'
            self.tegraflash_send_to_bootloader(bct_dict, 'blob.bin')
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
            values['--cfg'] = values['--rcmboot_pt_layout']
            self.tegraflash_get_key_mode()
            self.tegraflash_parse_partitionlayout()
            bct_dict = self.tegraflash_generate_bct(values['--rcmboot_bct_cfg'], bct_flags=['ENABLE_FLASHING'])
            self.tegraflash_generate_bpmp_mem_bin(values['--rcmboot_bct_cfg'], flags=["BPMP_MEM_CFG"])

            self.tegraflash_process_binaries(bct_dict['mb2_bct_file_name'])
            self.tegraflash_parse_partitionlayout()
            self.tegraflash_fill_mb1_storage_info(bct_dict['br_bct_file_name'])
            if values['--encrypt_key'] is not None:
                self.tegraflash_enc_and_sign_images()
            else:
                self.tegraflash_sign_images()
            self.tegraflash_update_br_bct_bl_info(bct_dict['br_bct_file_name'])
            self.tegraflash_update_images()
            if dump_args[0] == 'gen_blob':
                self.tegraflash_send_to_bootrom(bct_dict, True)
                self.tegraflash_boot_mb2_applet(bct_dict, True)
                return
            else:
                self.tegraflash_send_to_bootrom(bct_dict)
                self.tegraflash_boot_mb2_applet(bct_dict)

        if self.check_is_mb2applet():
            if dump_args[0] == 'eeprom':
                self.tegraflash_dumpeeprom(args, dump_args[1:])
            elif dump_args[0] in ['custinfo', 'try_custinfo']:
                self.tegraflash_dumpcustinfo(dump_args[1:])
            elif dump_args[0] == 'die':
                self.tegraflash_dumpdie(dump_args[1:])
            else:
                 raise tegraflash_exception(dump_args[0] + " is not supported")
        else:
            info_print('Error: mb2 applet is not running\n')

    def tegraflash_dumpcustinfo(self, dump_args):
        info_print('Dumping customer Info')

        tmp_bct = 'tmp.bct'
        if len(dump_args) > 0:
             file_path = tegraflash_abs_path(dump_args[0])
        else:
             file_path = tegraflash_abs_path("custinfo.bin")

        del_list = [tmp_bct, file_path]
        for del_file in del_list:
            if os.path.exists(del_file) == True:
                info_print('Renaming %s to %s' %(del_file, del_file + '.sav'))
                shutil.move(del_file, del_file + '.sav')

        command = self.exec_file('tegrarcm')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--oem', 'dump', 'bct', 'tmp.bct'])
        # special case: if the system has not been flashed, we ignore the error
        run_command(command, ignore=True)

        if os.path.exists(tmp_bct) == False:
            info_print('Continue while dump bct failed')
            return

        command = self.exec_file('tegrabct')
        command.extend(['--brbct', 'tmp.bct'])
        command.extend(['--chip', values['--chip'], values['--chip_major']])

        command.extend(['--custinfo', file_path])
        run_command(command)

    def tegraflash_dumpeeprom(self, args, params):
        values.update(args)

        if len(params) == 0:
            info_print("Error: EEPROM module not specified")
            return

        command = self.exec_file('tegrarcm')
        self.tegraflash_fetch_chip_info()
        self.tegraflash_fetch_bootdevice_info()
        info_print('Retrieving EEPROM data')
        out_file = tegraflash_abs_path(self.tegrarcm_values['--board_info'])
        eeprom_module = params[0]
        if len(params) > 1:
            out_file = tegraflash_abs_path(params[1])
            if os.path.exists(out_file):
                info_print('Renaming %s to %s' %(out_file, out_file + '.sav'))
                shutil.move(out_file, out_file + '.sav')

        command.extend(['--oem', 'platformdetails', 'eeprom', eeprom_module.lower(), out_file])
        try:
            command.extend(['--chip', values['--chip'], values['--chip_major']])
            run_command(command)
        except tegraflash_exception as e:
            command[0] = self.exec_file('tegradevflash')[0]
            run_command(command)

    def tegraflash_dumpdie(self, dump_args):
        info_print('Dumping Die fuse data')
        command = self.exec_file('tegrarcm')
        command.extend(['--chip', values['--chip'], values['--chip_major']])
        command.extend(['--oem', 'dump', 'die', dump_args[0]])

        run_command(command, ignore=True)
        if os.path.exists(dump_args[0]) == False:
            info_print('Continue while dump die failed')
            return


    def tegraflash_encrypt_sign_br_bct(self, bct_cfg):
        self.tegraflash_get_key_mode()
        # The PKC and SBK is not supported yet on t264
        if self.tegrasign_values['--mode'] == "zerosbk":
            configs_dict = self.get_bct_configs_dict(bct_cfg)
            bct_file_name = self.tegraflash_generate_br_bct(configs_dict)
            self.tegraflash_parse_partitionlayout()
            self.tegraflash_update_images()
        else:
            raise tegraflash_exception("PKC and SBK are not supported yet")

        return bct_file_name

    def tegraflash_sign_binary(self, exports, args=None):
        values.update(exports)
        self.tegraflash_get_key_mode()
        partition_type = "data"

        # Get partition type if it exists
        if len(args)>=2:
            partition_type = args[1]

        if (values['--coldboot_bct_cfg']):
            # If cold boot is specified,it should be cold boot
            values['--cfg'] = values['--coldboot_pt_layout']
            bct_cfg = values['--coldboot_bct_cfg']
        elif (values['--rcmboot_bct_cfg']):
            values['--cfg'] = values['--rcmboot_pt_layout']
            bct_cfg = values['--rcmboot_bct_cfg']
        else:
            raise tegraflash_exception('Either cold boot or rcm boot bct config has to be specified!!')

        # Handle signing BCT in special way as BCT needs to be generated dyanmically
        if partition_type == "BCT":
            binary = self.tegraflash_encrypt_sign_br_bct(bct_cfg)
        else:
            # Handle signing CPUBL in special way as cpubl needs to concatenate dtb at first
            # -k and no-flash options with CPUBL case
            # need to handle cpubl concat
            if partition_type == "bootloader_stage2":
                args[0] = self.tegraflash_concat_partition(args[2], args[0])
            magic_id = self.tegraflash_get_magicid(partition_type)
            binary = self.tegraflash_oem_sign_file(args[0], magic_id, partition_type)
        info_print('Copying ' + binary + ' to ' +  paths['WD'])
        if not shutil._samefile(binary, paths['WD'] + "/" + binary):
            shutil.copyfile(binary,  paths['WD'] + "/" + binary)
        return

    def tegraflash_encrypt_sign_binary(self, exports, args):
        values.update(exports)
        partition_type = "data"
        magicid = ""

        info_print('Generating signature')
        file_path = tegraflash_abs_path(args[0])
        if len(args) >= 2:
            partition_type = args[1]
        magicid = self.tegraflash_get_magicid(partition_type)
        if (magicid == 'FSKP'):
            return self.tegraflash_oem_sign_file(file_path, magicid, partition_type)

        if partition_type == "BCT":
            if (values['--coldboot_bct_cfg']):
                # If cold boot is specified,it should be cold boot
                values['--cfg'] = values['--coldboot_pt_layout']
                bct_cfg = values['--coldboot_bct_cfg']
            elif (values['--rcmboot_bct_cfg']):
                values['--cfg'] = values['--rcmboot_pt_layout']
                bct_cfg = values['--rcmboot_bct_cfg']
            else:
                raise tegraflash_exception('Either cold boot or rcm boot bct config has to be specified!!')

            binary = self.tegraflash_encrypt_sign_br_bct(bct_cfg)
            info_print('Copying ' + binary + ' to ' +  paths['WD'])
            shutil.copyfile(binary,  paths['WD'] + "/" + binary)
            return

        self.tegraflash_get_key_mode()

        if not self._is_header_present(file_path) or (magicid in self.QUIRK_MAGIC_ID_LIST_LOADED_BY_MB2):
            # Handle signing CPUBL in special way as cpubl needs to concatenate dtb at first
            # -k and no-flash options with CPUBL case
            # need to handle cpubl concat
            if partition_type == "bootloader_stage2":
                file_path = self.tegraflash_concat_partition(args[2], file_path)

            temp_file = os.path.basename(file_path)
            i = 1
            while os.path.exists(str(i) + "_" + temp_file):
                i = i + 1
            temp_file = str(i) + "_" + temp_file
            tegraflash_symlink(file_path, temp_file)
            if values['--encrypt_key'] is not None:
                info_print('Encrypting file')
                filename = self.tegraflash_oem_enc_and_sign_file(temp_file, magicid, partition_type)
            else:
                filename = self.tegraflash_oem_sign_file(temp_file, magicid, partition_type)
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
        algo_list = {'pkc':'oem-rsa', 'ec':'oem-ecc', 'ec521':'oem-ecc521',
                     'eddsa':'oem-eddsa', 'xmss':'oem-xmss'}
        signed_files = [ ]
        with open(xml_file, 'rt') as file:
            xml_tree = ElementTree.parse(file)

        mode = xml_tree.getroot().get('mode')
        if mode in algo_list:
            list_text = "signed_file"
        else:
            list_text = "encrypt_file"

        for file_nodes in xml_tree.iter('file'):
            file_name = file_nodes.get('name')
            signed_file = file_nodes.find(mode).get(list_text)
            shutil.copyfile(signed_file, output_dir + "/" + os.path.basename(signed_file))
            file_name = file_name.replace('_encrypt', '')
            file_name = file_name.replace('_sigheader', '')
            file_name = file_name.replace('_wbheader', '')
            file_name = file_name.replace('_aligned', '')
            signed_files.extend([file_name, signed_file])

        return signed_files

    def get_all_bins_for_oem_signing(self, layout_cfg):
        # Sign files listed in --bins
        with open(values['--cfg'], 'r') as file:
            xml_tree = ElementTree.parse(file)
            cfg_root = xml_tree.getroot()

        bins_list = []
        for device in cfg_root.findall('device'):
            for partition in device.iter('partition'):
                img_type = partition.get('type')
                file_node = partition.find('filename')
                if not file_node:
                    continue
                filepath = file_node.text.strip()
                filename = os.path.basename(filepath)

                if partition.get('oem_sign') == 'true':
                    bins_list.append('{} {}'.format(img_type, filename))

        return bins_list


    def tegraflash_sign(self, exports, args=None):
        values.update(exports)
        signed_files = []
        if (exports['--bct_flags_file'] is not None):
            flags = self.tegraflash_get_l4t_flags('flash_images', exports['--bct_flags_file'], exports['--overlay_bct_flags_file'])

        self.tegraflash_get_key_mode()

        if values['--encrypt_key'] is None:
            output_dir = tegraflash_abs_path('signed')
        else:
            output_dir = tegraflash_abs_path('enc_signed')
        # Create signed directory. If exists, clear all files
        if os.path.exists(output_dir):
            shutil.rmtree(output_dir, ignore_errors=True)
        os.makedirs(output_dir)

        binaries = []
        bpmp_mem = ""

        if (values['--coldboot_pt_layout']):
            # Assuming it is a cold boot request
            values['--cfg'] = values['--coldboot_pt_layout']
            bct_cfg = values['--coldboot_bct_cfg']
        else:
            values['--cfg'] = values['--rcmboot_pt_layout']
            bct_cfg = values['--rcmboot_bct_cfg']

        if (values['--cfg'] is None):
            raise tegraflash_exception('Either cold boot or rcm boot partition layout has to be specified!!')

        if values['--cfg'] is not None :
            self.tegraflash_generate_hpse_sb_pkg(bct_cfg)
            self.tegraflash_create_secure_fip(values['--cfg'])
            self.tegraflash_parse_partitionlayout()
            bct_dict = self.tegraflash_generate_bct(bct_cfg, bct_flags=flags)
            bpmp_mem = self.tegraflash_generate_bpmp_mem_bin(bct_cfg, flags=flags)
            self.tegraflash_process_binaries(bct_dict['mb2_bct_file_name'])
            self.tegraflash_parse_partitionlayout()
            self.tegraflash_fill_mb1_storage_info(bct_dict['br_bct_file_name'])
            if values['--encrypt_key'] is None:
                self.tegraflash_sign_images()
            else:
                self.tegraflash_enc_and_sign_images()
            self.tegraflash_update_br_bct_bl_info(bct_dict['br_bct_file_name'])
            self.tegraflash_update_images()
            # generate gpt and mbr
            command = self.exec_file('tegraparser')
            command.extend(['--generategpt', '--pt', self.tegraparser_values['--pt']])
            run_command(command)
            import re
            patt = re.compile(r".*(mbr|gpt).*\.bin")
            contents = os.listdir('.')
            for f in contents:
                if patt.match(f):
                    shutil.copyfile(f, output_dir + "/" + f)

        bins = self.get_all_bins_for_oem_signing(values['--cfg'])

        if len(bins) != 0:
            for binary in bins:
                binary = binary.strip(' ')
                binary = binary.replace('  ', ' ')
                tags = binary.split(' ')
                if (len(tags) < 2):
                    raise tegraflash_exception('invalid format ' + binary)

                magic_id = self.tegraflash_get_magicid(tags[0])
                # -k and no-flash options with CPUBL case
                # need to handle cpubl concat
                if magic_id == 'CPBL' and values['--tegraflash_v2'] and values['--bl']:
                     tags[1]= self.tegraflash_concat_partition('A_cpu-bootloader', values['--cpubl'])

                if values['--encrypt_key'] is None:
                    tags[1] = self.tegraflash_oem_sign_file(tags[1], magic_id, tags[0])
                else:
                    tags[1] = self.tegraflash_oem_enc_and_sign_file(tags[1], magic_id, tags[0])
                binaries.extend([tags[1]])

        if values['--cfg'] is not None :
            info_print(f"Copying enc/signed file in {output_dir}")
            signed_files.extend(self.tegraflash_copy_signed_binaries(self.tegrahost_values['--signed_list'], output_dir))
            self.update_file_name_of_compressed_images()
            if 'br_bct_file_name' in bct_dict:
                shutil.copyfile(bct_dict['br_bct_file_name'], output_dir + "/" + bct_dict['br_bct_file_name'])
                if self.tegrabct_backup['--image'] is not None:
                    info_print('Copying ' + self.tegrabct_backup['--image'] + ' to ' + output_dir)
                    shutil.copyfile(self.tegrabct_backup['--image'],
                        output_dir + "/" + self.tegrabct_backup['--image'])
            elif not values['--external_device']:
                raise tegraflash_exception("Unable to find bct file")
            self.tegraflash_update_cfg_file(signed_files, values['--cfg'], output_dir, bct_dict, 0)

        if 'mb1_bct_file_name' in bct_dict and bct_dict['mb1_bct_file_name']:
            shutil.copyfile(bct_dict['mb1_bct_file_name'], output_dir + "/" + bct_dict['mb1_bct_file_name'])

        hpse_pkg = get_partition_filename("hpse_pkg", 'type')
        sb_pkg = get_partition_filename("sb_pkg", 'type')

        file_list = [bpmp_mem, values['--rawkerneldtb'], values['--kerneldtb'], bct_dict['mb2_bct_file_name'],bct_dict['mem_bct_file_name']]

        # If HPSE and/or SB package is present add to the list of files
        if (hpse_pkg):
            info_print("Adding HPSE package: {}".format(hpse_pkg))
            file_list.append(hpse_pkg)

        if (sb_pkg):
            info_print("Adding SB package: {} binaries".format(sb_pkg))
            file_list.append(sb_pkg)

        for _file in file_list:
            if _file is not None and os.path.isfile(_file):
                shutil.copyfile(_file, output_dir + "/" + _file)

        if binaries == [] and signed_files == []:
            info_print('No file was signed. Please check arugments')

        for signed_binary in binaries:
            info_print('Copying ' + signed_binary + ' to ' + output_dir)
            shutil.copyfile(signed_binary, output_dir + "/" + signed_binary)

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

    def tegraflash_update_cfg_file(self, signed_files, cfg_file, output_dir, bct_dict, only_generated=False):
        secondary_gpt_found = None;
        signed_files = dict(zip(signed_files[::2], signed_files[1::2]))
        with open(cfg_file, 'r') as file:
            xml_tree = ElementTree.parse(file)

        root = xml_tree.getroot()
        for device in root.findall('.//device'):

            for node in device.findall('.//partition'):
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
                        file_name = bct_dict["br_bct_file_name"]

                    if part_name == "MB1_BCT" or part_name == "MB1_BCT_b" or part_name == "A_MB1_BCT" or part_name == "B_MB1_BCT":
                        file_name = bct_dict["mb1_bct_file_name"];

                    if part_name == "MEM_BCT" or part_name == "MEM_BCT_b" or part_name == "A_MEM_BCT" or part_name == "B_MEM_BCT":
                        file_name = bct_dict["mem_bct_file_name"]

                    if part_name == "BCT-boot-chain_backup":
                        file_name = self.tegrabct_backup['--image']

                    if part_name == "secondary_gpt" \
                            or part_name == "master_boot_record" \
                            or part_name == "primary_gpt" \
                            or part_name == "secondary_gpt_backup" :
                        idx = device.attrib.get('type').strip() + '_' + \
                            device.attrib.get('instance').strip() + '_' + part_name
                        if idx in self.tegraflash_gpt_image_name_map.keys():
                            file_name = self.tegraflash_gpt_image_name_map[idx]
                        else:
                            continue

                    if file_node is not None and file_name is not None:
                        file_node.text = " " + file_name + " "
                    elif file_name is not None:
                        new_tag = ElementTree.SubElement(node, 'filename')
                        new_tag.text = " " + file_name + " "



        with open (output_dir + "/" + os.path.basename(cfg_file), 'wb+') as file:
            file.write(ElementTree.tostring(root))

    def tegraflash_bpmp_generate_dtb(self, pt_layout_cfg):

        if bool(values['--trim_bpmp_dtb']) == False:
            info_print("Disabled BPMP dtb trim, using default dtb")
            info_print("")
            return

        ramcode = self.tegraflash_get_ramcode()

        info_print("Generating BPMP dtb for ramcode - " + str(ramcode))
        self.get_partition_partition_type('bpmp_fw_dtb')
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

    def tegraflash_get_ramcode(self):
        if values['--ramcode'] is not None:
            ramcode = int(values['--ramcode']);
            info_print("Got ramcode " + str(ramcode) + " from the command line")
            return ramcode

        chip_info = tegraflash_abs_path(self.tegrarcm_values['--chip_info'])
        # Select 1 bct based on RAMCODE

        ramcode = None
        chip_info_file = None

        chip_info_bak = tegraflash_abs_path(
                self.tegrarcm_values['--chip_info'] + '_bak')
        if (os.path.exists(chip_info)):
            chip_info_file = chip_info
        elif (os.path.exists(chip_info_bak)):
            chip_info_file =  chip_info_bak

        if (chip_info_file):
            info_print("Reading ramcode from {} file".format(chip_info_file))
            with open(chip_info_file, 'rb') as f:
                # RAMCODE shall be the last 4 bytes of fuses.bin
                f.seek(52, 0)
                ramcode = struct.unpack('<I',  f.read(4))[0]
                info_print('RAMCODE Read from Device: %x\n' % ramcode)

        if (ramcode is None):
            info_print("No ramcode from file or from command line. Set ramcode = 0")
            ramcode = 0

        return ramcode

    """ Other helper methods """

    def tegraflash_get_key_mode(self):
        if values['--hsm'] is True:
            l4t_hsm_str = self.parse_hsm_l4t_key_label(values['--key'])
            self.call_tegrasign(None, 'mode.txt', None, None, None, \
                                None, None, None, None, None, \
                                False, 0, 0, 0, None, \
                                0, None, l4t_hsm_str)
        else:
            self.call_tegrasign(None, 'mode.txt', None, values['--key'], None, \
                                None, None, None, None, None)

        with open('mode.txt') as mode_file:
            self.tegrasign_values['--mode'] = mode_file.read()

    def concatenate_cpubl_bldtb(self):
        bl_dtb_file = values['--bldtb']
        cpubl_bin_file = get_partition_filename("bootloader_stage2", 'type')
        if cpubl_bin_file == None:
            info_print('Not doing bl dtb to cpubl binary concatenation as cpubl is not specified in '
            + values['--cfg'])
            return None
        info_print('Concatenating bl dtb to cpubl binary')
        cpubl_with_dtb = cpubl_bin_file.split('.', 1)[0] + '_with_dtb.bin'
        if not os.path.exists(bl_dtb_file):
            raise tegraflash_exception('Could not find ' + bl_dtb_file)
        if not os.path.exists(cpubl_bin_file):
            raise tegraflash_exception('Could not find ' + cpubl_bin_file)
        shutil.copyfile(cpubl_bin_file, cpubl_with_dtb)
        concat_file(cpubl_with_dtb, bl_dtb_file)
        return cpubl_with_dtb # order: outfile, infile

    # This is the master dce call which finds kernel-dtb and concatates if it is found
    def get_dce_with_dtb_filename(self, dce_bin=None, update_cfg=True):
        if dce_bin == None:
            dce_bin = self.get_dcebin_filename()
        info_print('dce_bin = ' + str(dce_bin))
        kernel_dtb = self.get_dce_base_dtb_filename()
        if dce_bin is not None and kernel_dtb is not None:
            dce_comb = self.concatenate_dcebin_kerneldtb(dce_bin, kernel_dtb)
            if dce_comb is not None:
                info_print('dce_with_dtb = ' +  dce_comb)
                if update_cfg == True:
                    self.update_dcecomb_filename(dce_comb)
                return dce_comb
        return dce_bin

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
            # values['--rawkerneldtb'] => this is raw kernel dtb passed by flash.sh or specified in kernel-dtb partition
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
        info_print('Concatenating kernel-dtb to dce-fw binary')

        if not os.path.exists(dce_bin):
            raise tegraflash_exception('Could not find dce_bin: ' + dce_bin)
        if not os.path.exists(kernel_dtb):
            raise tegraflash_exception('Could not find kernel_dtb: ' + kernel_dtb)
        # in case if kernel-dtb has a path that needs to be removed for naming purposes
        kernel_dtb_basename = os.path.basename(kernel_dtb)
        dce_with_dtb = os.path.splitext(dce_bin)[0] + '_with_' + os.path.splitext(kernel_dtb_basename)[0] + '.bin'
        info_print('dce_bin = ' + dce_bin)
        info_print('kernel_dtb = ' +  kernel_dtb)
        info_print('dce_with_dtb = ' +  dce_with_dtb)

        shutil.copyfile(dce_bin, dce_with_dtb)
        #comment out concatenation for now.
        info_print('dce {} is concatenated with kernel dtb {} to {}'.format(dce_bin, kernel_dtb, dce_with_dtb))
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

    def update_cpublcomb_filename(self, cpublcomb_bin):
        with open(values['--cfg'], 'r+') as file:
            xml_tree = ElementTree.parse(file)
            root = xml_tree.getroot()
            for node in root.iter('partition'):
                if(node.get('type') == "bootloader_stage2"):
                    cpubl_node = node.find('filename')
                    # in case blank, leave it alone
                    if cpubl_node !=None and cpubl_node.text != None:
                        cpubl_node.text = cpublcomb_bin

            xml_tree.write(values['--cfg'])
        return

    def concatenate_mb2bct_mb2(self, mb2_bin_file, mb2_bct_file):

        if (mb2_bct_file == None):
            info_print("Error: skipping concatenation of MB2 bct with MB2 binary because MB2 bct is not generated")
            return

        if (not os.path.exists(mb2_bct_file)):
            info_print("Error: skipping concatenation of MB2 bct with MB2 binary because MB2 bct {} not found".format(mb2_bct_file))
            return

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

    def tegraflash_generate_mb2_bct(self, configs_dict, bct_flags=[]):
        bct_flags.append("ENABLE_DCE")
        if (is_partition_present('pva_fw', 'type')):
            bct_flags.append("ENABLE_PVA")
        if (values["--enable_mods"]) is not None:
            bct_flags.append("ENABLE_MODS_SP_LOAD")

        target_config = self.tegraflash_generate_targetconfig(configs_dict, bct_flags)

        mb2_bct_preprocess_list = ['mb2bctcfg', 'scr']

        for config_type in mb2_bct_preprocess_list:
            if (config_type != 'scr' and config_type not in configs_dict):
                info_print('Error: skipping mb2 bct generation as required config {} not found'.format(config_type))
                return
        info_print('Generating mb2-bct')

        mb2_bct_file_name = 'mb2_bct.cfg'
        bct_flags.append('IN_DTS_CONTEXT')
        mb2_bct_file_name = BootburnMb2Bct(target_config, os.getcwd(), bctFileNameSuffix=mb2_bct_file_name).generateBct(bct_flags)
        values['--mb2_bct'] = 'mb2_bct_MB2.bct'

        # return mb2bct binary name
        return values['--mb2_bct']

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

        with open(self.tegrahost_values['--list'], 'r') as file:
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

    def update_file_name_of_compressed_images(self):
        with open(self.tegrahost_values['--list'], 'r') as file:
            xml_tree = ElementTree.parse(file)
        root = xml_tree.getroot()
        for file_node in root.findall('file'):
            if file_node.get('compress') == '1':
                typename = file_node.get('type')
                filename = self.get_file_name_from_images_list(typename)
                set_partition_filename(typename, filename, "type")

    def call_tegrasign(self, file_val, getmode, getmont, key,
                       length, list_val, offset, pubkeyhash, sha, skip_enc,
                       verbose=False, iv=0, aad=0, tag=0, sign=None,
                       verify=0, kdf=None, hsm=None, ran=None, block='0',
                       softhsm=True):

        tegrasign(file_val, getmode, getmont, key, length,
                  list_val, offset, pubkeyhash, sha, skip_enc,
                  verbose, iv, aad, tag, sign,
                  verify, kdf, hsm, ran, block,
                  softhsm)

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

    def run_cpp_tool(self, input_file, bct_flags = []):
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

        # Print bct_flags
        info_print("Bct flags: %s " % (", ".join(bct_flags)))

        # Process additional bct flags
        for bct_flag in bct_flags:
            command.extend(["-D", bct_flag])

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

    def parse_hsm_l4t_key_label(self, key_val):
        """
        Parse HSM L4T key name with comprehensive validation
        Parses key path format: <path>/hsm_l4t_<key_label>_<index>.pem
        Handles both string and list inputs (uses first element if list)
        Args:
            key_val (str or list): Key file path to parse (REQUIRED)
                                 If list, uses first element

        Returns:
            str: Formatted HSM L4T string (hsm_l4t_<key_label>_<index>) on success only

        Raises:
            tegraflash_exception: If key name format is invalid, or missing hsm_l4t
        """
        import re
        import os

        # Handle both string and list inputs for key_val
        if isinstance(key_val, list):
            if not key_val:
                info_print('ERROR: Empty key list provided')
                raise tegraflash_exception("Unsupported file name format.")
            # Extract first element from list
            key_name = key_val[0]
            info_print('Using first key from list: ' + key_name)
        else:
            key_name = key_val

        # Validate key_name contains hsm_l4t identifier
        if not key_name or "hsm_l4t" not in key_name.lower():
            info_print('ERROR: HSM mode requires a valid key path in format: <path>/hsm_l4t_<key_label>_<index>.pem')
            raise tegraflash_exception("Unsupported file name format.")

        try:
            # Extract filename from path for parsing
            key_name = os.path.basename(key_name)

            # Strict regex pattern to match hsm_l4t_<key_label>_<index>.pem format
            # Pattern ensures exact format matching to prevent injection attacks
            hsm_pattern = re.compile(r'^hsm_l4t_([A-Z0-9]+)_(\d+)\.pem$', re.IGNORECASE)
            match = hsm_pattern.match(key_name)

            if match:
                # Extract key label and active key index
                key_label = match.group(1)
                index_str = match.group(2)

                result = "hsm_l4t_" + key_label + "_" + index_str
                info_print("HSM L4T: Successfully parsed key index: " + index_str + ", key label: " + key_label)
                return result

            else:
                raise tegraflash_exception("Invalid HSM L4T key name format - (" + key_name + ")")

        except (OSError, TypeError) as e:
            raise tegraflash_exception("HSM L4T key name processing failed - (" + key_name + ")")

class TFlashT264(TFlashT264_Base):
    """ Class for Tegraflash functions and parameters specific to t264.

    Parent Class: TFlashT264_Base
    """

    def __init__(self, chip_id):
        if chip_id == 0x26 or chip_id == 0x268:
            # Error check for Chip ID of t264.
            super(TFlashT264, self).__init__()

            # SHA digest offsets
            self.args_offset = '6784'
            self.args_length = '1408'

            # Fixed BCH offsets
            self.bch_offset = '4032'
            self.bch_length = '4160'

            self.first_time_mb2app_check = True
        else:
            raise tegraflash_exception("Unsupported Chip.")
