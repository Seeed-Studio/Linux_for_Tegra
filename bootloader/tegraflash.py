#!/usr/bin/env python3
#
# Copyright (c) 2014-2022, NVIDIA Corporation.  All Rights Reserved.
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA Corporation is strictly prohibited.
#

import cmd
import collections
import errno
import getopt
import os
import shlex
import shutil
import string
import subprocess
import sys
from os.path import expanduser

import tegraflash_internal
from tegraflash_internal import (cmd_environ, paths,
                                 tegraflash_abs_path, tegraflash_blowfuses,
                                 tegraflash_burnfuses, tegraflash_ccgupdate,
                                 tegraflash_dump, tegraflash_encrypt_and_sign,
                                 tegraflash_erase, tegraflash_exception,
                                 tegraflash_flash, tegraflash_flush_sata,
                                 tegraflash_generate_recovery_blob,
                                 tegraflash_mkdevimages, tegraflash_nvsign,
                                 tegraflash_os_path, tegraflash_packageccg,
                                 tegraflash_parse,
                                 tegraflash_provision_rollback,
                                 tegraflash_rcmbl, tegraflash_rcmboot,
                                 tegraflash_read, tegraflash_readfuses,
                                 tegraflash_readmrr, tegraflash_reboot,
                                 tegraflash_sata_fwdownload,
                                 tegraflash_secureflash, tegraflash_setverify,
                                 tegraflash_sign, tegraflash_encrypt_sign_binary,
                                 tegraflash_signwrite, tegraflash_symlink,
                                 tegraflash_test, tegraflash_ufs_otp,
                                 tegraflash_update_img_path,
                                 tegraflash_update_rpmb, tegraflash_verify,
                                 tegraflash_write)
try:
    from tegraflash_impl_t234 import (TFlashT23x)
except ImportError:
    print("WARNING: failed to import T23x module")

# insert current working directory
sys.path.insert(1, os.getcwd())

try:
    input = raw_input
except NameError:
    pass

cmd_environ.update(os.environ.copy())

paths.update({'OUT':None, 'BIN':None, 'SCRIPT':None, 'TMP':None, 'WD':os.getcwd()})

exports = {
            "--bct":None, "--bct_cold_boot":None, "--key":'None', "--encrypt_key":None, "--enable_user_kdk":False, "--cfg":None, "--bl":None,
            "--board":None, "--eeprom":None, "--cmd":None, "--instance":None, "--bpfdtb":None,
            "--hostbin":None, "--applet":None,"--dtb":None, "--bldtb":None, "--kerneldtb":None, "--chip":None,
            "--out":None, "--nct":None, "--fb":None, "--odmdata":None, "--overlay_dtb":None, "--ecid":None,
            "--lnx":None, "--tos":None, "--eks":None, "--boardconfig":None,
            "--skipuid":False, "--securedev":False, "--keyindex":None, "--keep":False,
            "--wb":None, "--bl-load":None, "--bins":None, "--dev_params":None,
            "--sdram_config":None, "--ramcode": None, "--misc_config":None, "--mb2bct_cfg":None,
            "--misc_cold_boot_config":None,
            "--pinmux_config":None, "--pmc_config":None, "--pmic_config":None,
            "--gpioint_config":None, "--uphy_config":None, "--scr_config":None,
            "--scr_cold_boot_config":None, "--br_cmd_config":None, "--prod_config":None,
            "--device_config":None, "--applet-cpu":None, "--bpf":None, "--mb1_bct":None, "--mb2_bct":None,
            "--mb1_cold_boot_bct":None, "--mb2_cold_boot_bct":None, "--skipsanitize":False, "--tegraflash_v2":False,
            "--chip_major":"0", "--chip_minor":"0", "--nv_key":None, "--nvencrypt_key":None, "--cl":"39314184",
            "--soft_fuses":None, "--deviceprod_config":None, "--rcm_bct":None, "--secureboot":False,
            "--mem_bct":None, "--mem_bct_cold_boot":None, "--minratchet_config":None,
            "--wb0sdram_config":None, "--blversion":None, "--output_dir":None, "--nv_nvratchet":"0",
            "--nv_oemratchet":"0", "--image_dirs":None, "--trim_bpmp_dtb":False, "--cpubl":None,
            "--concat_cpubl_bldtb":False, "--external_device":False, "--cust_info": None,
            "--sparseupdate": False, "--ratchet_blob":None, "--applet_softfuse":None,
            "--secondary_gpt_backup":False, "--boot_chain":None, "--bct_backup":False,
            "--mb1_bin":None, "--psc_bl1_bin":None,
            "--rcmboot_pt_layout": None, "--coldboot_pt_layout": None, "--rcmboot_bct_cfg": None,
            "--coldboot_bct_cfg": None, "--dce_base_dtb": None, "--dce_overlay_dtb": None,
          }

exit_on_error = False

def usage():
    print( '\n'.join([
    '  Usage: tegraflash [--bct <file>] [--bct_cold_boot <file>] [--cfg <file>] [--bl <file>] [--instance <number>]',
    '                    [--chip <number>] [--ecid <ecid>] [--dtb <file>] [--bldtb <file>] [--kerneldtb <file>]',
    '                    [--key <file>] [--encrypt_key <file>] [--enable_user_kdk] [--cmd \"commands\"] [--bpfldtb <file>]',
    '                    [--applet <file>] [--nct <file>] [--hostbin <dir>] [--out <dir>]',
    '                    [--boardconfig <file>] [--skipuid] [--securedev] [--keyindex <number>]',
    '                    [--bl-load <addr>] [--dev_params <file>] [--sdram_config <file>] [--ramcode <index>]',
    '                    [--bins <image_type> <file> [load_address][;...]]',
    '                    [--misc_config <file>] [--mb1_bct <file>] [--blversion <number> <number>]',
    '                    [--mb2bct_cfg <file>] [--mb2_bct <file>]',
    '                    [--pinmux_config <file>] [--pmc <file>] [--scr_config <file>]',
    '                    [--pmic_config <file>] [--br_cmd_config <file>] [--prod_config <file]',
    '                    [--gpioint_config <file>] [--uphy_config <file>] [--device_config <file>]',
    '                    [--deviceprod_config <file>] [--minratchet_config <file>] [--skipsanitize] [--keep]',
    '                    [--output_dir <dir>] [--external_device], [--odmdata <odmdata|odmdata str>]',
    '                    [--overlay_dtb <dtb files>] [--cust_info <file>] [--sparseupdate]',
    '                    [--secondary_gpt_backup] [--boot_chain <A|B>] [--bct_backup]',
    '                    [--mb1_bin] [--psc_bl1_bin]',
    '                    [--coldboot_pt_layout], [--rcmboot_pt_layout], [--coldboot_bct_cfg], [--rcmboot_bct_cfg]'
    '                    [--dce_base_dtb], [--dce_overlay_dtb]'

    '   ',
    '   --bct           : Bootrom Boot Config Table file',
    '   --bct_cold_boot  : Bootrom Boot Config Table file for cold boot',
    '   --cfg           : Partition layout configuration file',
    '   --bl            : Command line bootloader',
    '   --bl-load       : Bootloader load/entry address',
    '   --chip          : Chip Id',
    '   --ecid          : ECID',
    '   --blversion     : Major and Minor version of bootloader loaded by BOOTROM',
    '   --dtb           : DTB file to be used by both (old implementation, to deprecate in future)',
    '   --dce_base_dtb     : Specify base DTB file specifically used for DCE',
    '   --dce_overlay_dtb  : Specify a list of comma seperated dtbs to be specifically applied to DCE base dtb',
    '   --bldtb         : DTB file to be used by cboot',
    '   --kerneldtb     : DTB file to be used by kernel',
    '   --bpfdtb       : DTB file to be used by BPMP-FW',
    '   --key           : Key for signing required files',
    '   --encrypt_key   : Key for encrypting required files',
    '   --enable_user_kdk : Enable user defined KDK',
    '   --applet        : Applet to be sent to BootRom',
    '   --nct           : NCT file',
    '   --boardconfig   : File containing board configuration',
    '   --skipuid       : Skip reading Chip UID',
    '   --skipsanitize  : Skip SDMMC sanitize',
    '   --securedev     : path for flashing fused devices',
    '   --keyindex      : FSKP key index',
    '   --cmd           : List of comma(;) separated commands',
    '   --dev_params    : Boot device parameters. It supports multiple files divided by comma for different boot chains',
    '   --boot_chain    : Selected boot chain',
    '   --sdram_config  : Sdram configuration',
    '   --ramcode       : The ramcode value',
    '   --bins          : List of binaries to be downloaded separated by commad(;)',
    '   --misc_config   : Misc BCT configuration',
    '   --mb2bct_cfg    : MB2 BCT configuration',
    '   --misc_cold_boot_config : Misc BCT configuration to be used in coldboot',
    '   --pinmux_config : Pinmux BCT configuration',
    '   --scr_config    : SCR BCT configuration',
    '   --scr_cold_boot_config : SCR BCT configuration to be used in coldboot',
    '   --pmc_onfig     : Pad voltage - DPD BCT configuration',
    '   --pmic_config   : PMIC - Rails MMIO/I2C Commands BCT configuration',
    '   --br_cmd_config : BootROM MMIO/I2C Commands BCT configuration',
    '   --prod_config   : Pinmux prod setings BCT configuration',
    '   --gpioint_config : GPIO interrupt routing configurations',
    '   --uphy_config   : Uphy Lane ownership mapping configrations',
    '   --device_config : Device specific platform configurations',
    '   --deviceprod_config : Device specific Prod configurations',
    '   --minratchet_config : Minimum ratchet level of oem-fw',
    '   --mb1_bct       : MB1 BCT file',
    '   --mb2_bct       : MB2 BCT file',
    '   --mb1_cold_boot_bct: MB1 BCT file used in coldboot',
    '   --mb2_cold_boot_bct: MB2 BCT file used in coldboot',
    '   --soft_fuses    : MB1 Soft fuse config',
    '   --hostbin       : Directory contaning host binaries',
    '   --out           : Directory containing device files',
    '   --keep          : Keep temporary directory',
    '   --output_dir    : Directory to store dev_images',
    '   --nv_nvratchet  : NV ratchet level in NV signed binary',
    '   --nv_oemratchet : OEM ratchet level in NV signed binary',
    '   --image_dirs    : Comma separated list of directories for the binary image files',
    '   --trim_bpmp_dtb : Remove unused emc strap data from BPMP dtb',
    '   --cpubl         : CPU Bootloader File',
    '   --concat_cpubl_bldtb : Concatenate CPU Bootloader and its DTB file',
    '   --external_device: Generate images for an external device',
    '   --odmdata       : ODMDATA to write into BCT (Usage for T194 and before)',
    '                   : ODMDATA string that is commma separated (Usage for T234 and onward)',
    '                   : gbe-uphy-config_1,nvhs-uphy-config_2,hsio-uphy-config_3,gbe0-mode-10g (nn T234 example)',
    '   --overlay_dtb   : a list of comma seperated dtbs to be applied to base dtb',
    '   --cust_info     : customer data to be filled into BR-BCT',
    '   --sparseupdate  : only flash partitions that have changed. Currently only support SPI flash memory ',
    '   --secondary_gpt_backup : flash secondary GPT backup partition',
    '   --bct_backup    : flash BCT backup partition as well when flashing BCT partition',
    '   --mb1_bin       : mb1 bootloader binary to download to bootrom in RCM',
    '   --psc_bl1_bin   : psc_bl1 binary to download to bootrom in RCM',
    '   '
    ]))

def tegraflash_err(Errcode):
    if( exit_on_error):
        sys.exit(Errcode)

class tegraflashcmds(cmd.Cmd):
    prompt = 'tegraflash~> '

    def __init__(self):
        print('\n'.join([
            'Welcome to Tegra Flash',
            'version 1.0.0',
            'Type ? or help for help and q or quit to exit',
            'Use ! to execute system commands',
            ' '
            ]))
        cmd.Cmd.__init__(self)
        self.chip_inst = self.chip_parser()

    def do_quit(self, params):
        return True;

    def do_shell(self, params):
        os.system(params)

    def emptyline(self):
        pass

    def default(self,line):
        print('unknown command:' + line)
        tegraflash_err(1)

    def do_q(self, params):
        return True;

    def chip_parser(self):
        """ Chip parser that instantiates the class of the chip
        """
        chip_id = int(exports['--chip'], 0)
        if chip_id == 0x23:
            chip_inst = TFlashT23x(chip_id)
        else:
            chip_inst = tegraflash_internal
        return chip_inst

    def do_mkdevimages(self, param):
        tegraflash_update_env()
        params = param.replace('  ', ' ')
        args = param.split(' ')
        compulsory_args = ['--cfg', '--chip']

        for required_arg in compulsory_args:
            if exports[required_arg] is None:
                exports[required_arg] = input('Input ' + required_arg[2:] + ': ')

        try:
            self.chip_inst.tegraflash_mkdevimages(exports, args)

        except tegraflash_exception as e:
            print('Error: '+ e.value)
            tegraflash_err(1)

    def help_mkdevimages(self):
        print('\n'.join([
        ' ',
        '------------------------------------------------------',
        '  Usage: mkdevimages --bct <file> --cfg <file> [--key <file>] [--output_dir <dir>]',
        '------------------------------------------------------',
        '   --bct    : Boot configuration Table',
        '   --cfg    : Partition layout configuration',
        '   --key    : Key file',
        '   --output_dir : Directory to store dev images',
        '------------------------------------------------------',
        ' ',
        ]))

    def do_flash(self, param):
        tegraflash_update_env()
        params = param.replace('  ', ' ')
        args = param.split(' ')
        exports.update(dict(zip(args[::2], args[1::2])))
        compulsory_args = ['--cfg', '--bl', '--chip', '--applet']

        for required_arg in compulsory_args:
            if exports[required_arg] is None:
                exports[required_arg] = input('Input ' + required_arg[2:] + ': ')

        try:
            self.chip_inst.tegraflash_flash(exports)

        except tegraflash_exception as e:
            print('Error: '+ e.value)
            tegraflash_err(1)

    def help_flash(self):
        print('\n'.join([
        ' ',
        '------------------------------------------------------',
        '  Usage: flash --bct <file> --cfg <file> [--key <file>]',
        '               --applet <file>',
        '------------------------------------------------------',
        '   --bct    : Boot configuration Table',
        '   --cfg    : Partition layout configuration',
        '   --bl     : Command line bootloader',
        '   --key    : Key file',
        '   --applet : Applet to be sent to BootRom',
        '------------------------------------------------------',
        ' ',
        ]))

    def do_secureflash(self, param):
        tegraflash_update_env()
        params = param.replace('  ', ' ')
        args = param.split(' ')
        exports.update(dict(zip(args[::2], args[1::2])))
        compulsory_args = ['--bct', '--cfg', '--bl', '--applet']

        for required_arg in compulsory_args:
            if exports[required_arg] is None:
                exports[required_arg] = input('Input ' + required_arg[2:] + ': ')

        try:
            self.chip_inst.tegraflash_secureflash(exports)

        except tegraflash_exception as e:
            print('Error: '+ e.value)
            tegraflash_err(1)

    def help_secureflash(self):
        print('\n'.join([
        ' ',
        '------------------------------------------------------',
        '  Usage: secureflash --bct <file> --cfg <file> --applet <file>',
        '         --bldtb(dtb in case of mods/l4t) <file> --bl <file>',
        '------------------------------------------------------',
        '   --bct    : Boot configuration Table',
        '   --cfg    : Partition layout configuration',
        '   --bl     : Command line bootloader',
        '   --applet : Applet to be sent to BootRom',
        '   --bldtb  : DTB file for recovery boot',
        '   --dtb    : DTB file for recovery boot',
        '------------------------------------------------------',
        ' ',
        ]))

    def do_rcmbl(self, param):
        print ("\n Entering RCM bootloader\n")
        tegraflash_update_env()
        compulsory_args = ['--chip', '--applet', '--bct', '--bldtb',
            '--applet-cpu',  '--bl']

        for required_arg in compulsory_args:
            if exports[required_arg] is None:
                exports[required_arg] = input('Input ' + required_arg[2:] + ': ')

        try:
            self.chip_inst.tegraflash_rcmbl(exports)
        except tegraflash_exception as e:
            print('Error: '+ e.value)
            tegraflash_err(1)

    def help_rcmbl(self):
        print('\n'.join([
        ' ',
        '------------------------------------------------------',
        '  Usage: rcmbl --chip <chip> --applet <file> --bct <file>',
        '               --bldtb <dtb> --applet-cpu <file>',
        '               --bl <file> [--bl-load <load-addr>]',
        '               [--odmdata <data>] [--boardconfig <file>]',
        '               [--key <file>] [--eks <file>]',
        '------------------------------------------------------',
        '   --chip        : Chip ID',
        '   --applet      : Applet to be sent to Boot ROM',
        '   --bct         : Boot Configuration Table',
        '   --bldtb       : DTB file to pass to nvtboot and bootloader',
        '   --applet-cpu  : CPU-side pre-bootloader binary',
        '   --bl          : Command line bootloader',
        '   --bl-load     : Bootloader load address',
        '   --odmdata     : ODMDATA to write into BCT',
        '   --boardconfig : Board config to write into BCT',
        '   --key         : Key file',
        '   --eks         : eks.dat file',
        '------------------------------------------------------',
        ' ',
        ]))

    def do_rcmboot(self, param):
        print ("\n Entering RCM boot\n")
        tegraflash_update_env()
        compulsory_args = [ '--bl', '--chip', '--applet']

        for required_arg in compulsory_args:
            if exports[required_arg] is None:
                exports[required_arg] = input('Input ' + required_arg[2:] + ': ')

        try:
            self.chip_inst.tegraflash_rcmboot(exports)

        except tegraflash_exception as e:
            print('Error: '+ e.value)
            tegraflash_err(1)

    def help_rcmboot(self):
        print('\n'.join([
        ' ',
        '------------------------------------------------------',
        '  Usage: rcmboot --bct <file> --cfg <file> [--key <file>]',
        '                 --lnx <file> [--tos <file>] [--eks <file>]',
        '                 --bl <file> --applet <file> ',
        '------------------------------------------------------',
        '   --bct    : Boot configuration Table',
        '   --cfg    : Partition layout configuration',
        '   --key    : Key file',
        '   --lnx    : boot.img file used during rcmboot',
        '   --tos    : tos.img file used during rcmboot',
        '   --eks    : eks.dat file used during rcmboot',
        '   --bl     : Command line bootloader',
        '   --applet : Applet to be sent to BootRom',
        '------------------------------------------------------',
        ' ',
        ]))

    def do_read(self, params):
        params = params.replace('  ', ' ')
        args = params.split(' ')
        if len(args) == 2:
            tegraflash_update_env()

            compulsory_args = ['--chip', '--applet']

            for required_arg in compulsory_args:
                if exports[required_arg] is None:
                    exports[required_arg] = input('Input ' + required_arg[2:] + ': ')

            try:
                file_path = tegraflash_abs_path(args[1])
                self.chip_inst.tegraflash_read(exports, args[0], file_path)

            except tegraflash_exception as e:
                print('Error: '+ e.value)
        else:
            self.help_read()

    def help_read(self):
        print('\n'.join([
        ' ',
        '-------------------------------------------',
        '  Usage: read <name> <file>',
        '-------------------------------------------',
        ' ',
        ]))

    def do_write(self, params):
        params = params.replace('  ', ' ')
        args = params.split(' ')
        if len(args) == 2:
            tegraflash_update_env()
            compulsory_args = ['--chip', '--applet']

            for required_arg in compulsory_args:
                if exports[required_arg] is None:
                    exports[required_arg] = input('Input ' + required_arg[2:] + ': ')

            try:
                file_path = tegraflash_abs_path(args[1])
                self.chip_inst.tegraflash_write(exports, args[0], file_path)

            except tegraflash_exception as e:
                print('Error: '+ e.value)
                tegraflash_err(1)

        else:
            self.help_write()

    def help_write(self):
        print('\n'.join([
        ' ',
        '--------------------------------------------',
        '  Usage: write <name> <file>',
        '--------------------------------------------',
        '   ',
        ]))

    def do_packageccg(self, params):
        params = params.replace('  ', ' ')
        args = params.split(' ')
        if len(args) >= 2:
            tegraflash_update_env()
            compulsory_args = ['--chip']
            for required_arg in compulsory_args:
                if exports[required_arg] is None:
                    exports[required_arg] = input('Input ' + required_arg[2:] + ': ')

            try:
                tegraflash_packageccg(exports, args)

            except tegraflash_exception as e:
                print('Error: '+ e.value)
                tegraflash_err(1)
        else:
            self.help_packageccg()

    def help_packageccg(self):
        print('\n'.join([
        ' ',
        '--------------------------------------------',
        '  Usage: ccgpackage <fw1> <fw2> [output]',
        '--------------------------------------------',
        '  default output : ccg-fw.bin',
        '--------------------------------------------',
        '   ',
        ]))

    def do_ccgupdate(self, params):
        params = params.replace('  ', ' ')
        args = params.split(' ')
        if len(args) == 2:
            tegraflash_update_env()
            compulsory_args = ['--bl', '--chip', '--applet']

            for required_arg in compulsory_args:
                if exports[required_arg] is None:
                    exports[required_arg] = input('Input ' + required_arg[2:] + ': ')

            try:
                file_path1 = tegraflash_abs_path(args[0])
                file_path2 = tegraflash_abs_path(args[1])
                self.chip_inst.tegraflash_ccgupdate(exports, file_path1, file_path2)

            except tegraflash_exception as e:
                print('Error: '+ e.value)
                tegraflash_err(1)

        else:
            self.help_ccgupdate()

    def help_ccgupdate(self):
        print('\n'.join([
        ' ',
        '--------------------------------------------',
        '  Usage: ccgupdate <fw1> <fw2>',
        '--------------------------------------------',
        '   ',
        ]))

    def do_signwrite(self, params):
        params = params.replace('  ', ' ')
        args = params.split(' ')
        if len(args) == 2:
            tegraflash_update_env()
            compulsory_args = ['--chip', '--applet']

            for required_arg in compulsory_args:
                if exports[required_arg] is None:
                    exports[required_arg] = input('Input ' + required_arg[2:] + ': ')

            try:
                file_path = tegraflash_abs_path(args[1])
                self.chip_inst.tegraflash_signwrite(exports, args[0], file_path)

            except tegraflash_exception as e:
                print('Error: '+ e.value)
                tegraflash_err(1)

        else:
            self.help_signwrite()

    def help_signwrite(self):
        print('\n'.join([
        ' ',
        '--------------------------------------------',
        '  Usage: signwrite <name> <file> [partition_type]',
        '--------------------------------------------',
        '  signwrite is special command to update partitions for which binaries',
        '  needs to be signed with OEM key',
        '--------------------------------------------',
        '   ',
        ]))

    def do_nvsign(self, params):
        params = params.replace('  ', ' ')
        args = params.split(' ')
        only_sign = False
        if len(args) >= 2:
            tegraflash_update_env()
            compulsory_args = ['--chip', '--nv_key', '--nvencrypt_key']

            for required_arg in compulsory_args:
                if exports[required_arg] is None:
                    exports[required_arg] = input('Input ' + required_arg[2:] + ': ')

            try:
                file_path = tegraflash_abs_path(paths['OUT'] + '/' + args[0])
                print('file path ' + file_path)
                magic = args[1]
                if len(args) == 3:
                    if args[2] == 'only_sign':
                       only_sign = True
                    else:
                       print('wrong option ' + args[2] + ' for nvsign')
                       tegraflash_err(1)
                self.chip_inst.tegraflash_nvsign(exports, file_path, magic, only_sign)

            except tegraflash_exception as e:
                print('Error: '+ e.value)
                tegraflash_err(1)

        else:
            self.help_nvsign()

    def help_nvsign(self):
        print('\n'.join([
        ' ',
        '--------------------------------------------',
        '  Usage: nvsign <name> <file>',
        '--------------------------------------------',
        '  nvsign is special command to generate nvsigned binary',
        '  with given nv sbk and pkc keys',
        '--------------------------------------------',
        '   ',
        ]))

    def do_erase(self, params):
        params = params.replace('  ', ' ')
        params = params.strip(' ')
        args = params.split(' ')
        if params and len(args) == 1:
            tegraflash_update_env()

            compulsory_args = ['--bl', '--chip', '--applet']

            for required_arg in compulsory_args:
                if exports[required_arg] is None:
                    exports[required_arg] = input('Input ' + required_arg[2:] + ': ')

            try:
                self.chip_inst.tegraflash_erase(exports, args[0])

            except tegraflash_exception as e:
                print('Error: '+ e.value)
        else:
            self.help_erase()

    def help_erase(self):
        print('\n'.join([
        ' ',
        '-------------------------------------------',
        '  Usage: erase <name>',
        '-------------------------------------------',
        ' ',
        ]))

    def do_verify(self, params):
        tegraflash_update_env()
        try:
            self.chip_inst.tegraflash_verify(args)
        except tegraflash_exception as e:
            print('Error: '+ e.value)

    def help_verify(self):
        print('\n'.join([
        ' ',
        '-------------------------------------------',
        '  Usage: verify',
        '-------------------------------------------',
        ' ',
        ]))

    def do_setverify(self, params):
        params = params.replace('  ', ' ')
        params = params.strip(' ')
        args = params.split(' ')
        if params and len(args) == 1:
            tegraflash_update_env()

            compulsory_args = ['--bl', '--chip', '--applet']

            for required_arg in compulsory_args:
                if exports[required_arg] is None:
                    exports[required_arg] = input('Input ' + required_arg[2:] + ': ')

            try:
                self.chip_inst.tegraflash_setverify(exports, args[0])

            except tegraflash_exception as e:
                print('Error: '+ e.value)
        else:
            self.help_setverify()

    def help_setverify(self):
        print('\n'.join([
        ' ',
        '-------------------------------------------',
        '  Usage: setverify <name>',
        '         setverify all',
        '-------------------------------------------',
        ' ',
        ]))

    def do_reboot(self, param):
        param = param.replace('  ', ' ')
        args = param.split(' ')

        try:
            if not args[0]:
                args[0] = "coldboot"

            self.chip_inst.tegraflash_reboot(args)

        except tegraflash_exception as e:
            print('Error: '+ e.value)
            tegraflash_err(1)

    def help_reboot(self):
        print('\n'.join([
        ' ',
        '--------------------------------------------',
        '  Usage: reboot [coldboot | recovery]',
        '--------------------------------------------',
        '   ',
        ]))

    def do_sign(self, params):
        tegraflash_update_env()
        args = { }
        if not params == "":
            args = shlex.split(params)

        try:
            if len(args) == 0:
                if not params == "":
                    args = dict(zip(args[::2], args[1::2]))
                    exports.update(args)

                compulsory_args = ['--chip', '--key']

                for required_arg in compulsory_args:
                    if exports[required_arg] is None:
                        exports[required_arg] = input('Input ' + required_arg[2:] + ': ')
                if exports['--encrypt_key'] is None:
                    self.chip_inst.tegraflash_sign(exports)
                else:
                    self.chip_inst.tegraflash_encrypt_and_sign(exports)
            else:
                # Only route the following condition for t234, old chips remain the same path
                if int(exports['--chip'], 0) == 0x23:
                    if exports['--encrypt_key'] is None:
                        self.chip_inst.tegraflash_sign_binary(exports, args)
                    else:
                        self.chip_inst.tegraflash_encrypt_sign_binary(exports, args)
                else:
                    self.chip_inst.tegraflash_encrypt_sign_binary(exports, args)

        except tegraflash_exception as e:
            print('Error: '+ e.value)
            tegraflash_err(1)

    def help_sign(self):
        print('\n'.join([
        ' ',
        '----------------------------------------------------------------------',
        '  Usage: sign [file [type]]',
        '----------------------------------------------------------------------',
        ' If file is given then sign single file as par patition type',
        ' if file is not given then sign all required images from command line &',
        ' partition layout',
        '----------------------------------------------------------------------',
        ' ',
        ]))

    def do_test(self, params):
        params = params.replace('  ', ' ')
        args = params.split(' ')

        if args[0] != '':
            if len(args) > 1 or args[0] == 'eeprom':
                tegraflash_update_env()

                compulsory_args = ['--chip', '--applet']

                for required_arg in compulsory_args:
                    if exports[required_arg] is None:
                        exports[required_arg] = input('Input ' + required_arg[2:] + ': ')
                try:
                    self.chip_inst.tegraflash_test(exports, args)
                except tegraflash_exception as e:
                    print('Error: '+ e.value)
                    tegraflash_err(1)
            else:
                self.help_test()
        else:
            self.help_test()

    def help_test(self):
        print('\n'.join([

        ' ',
        '----------------------------------------------------------------------',
        '  Usage: test <test_name> [parameters]',
        '----------------------------------------------------------------------',
        '  Supported tests',
        ' ',
        '  sdram <mode> [size]  : Mode and size in Mb',
        '                         Verifies sdram by writing and reading specified',
        '                         size. Supported modes are 0: soft Test, 1: Hard Test',
        '                         2: Bus Test',
        ' ',
        '  emmc [loop]          : how many loops will be executed',
        '                         [ ONLY VALID for T210 !!! ]',
        '                         Verifies emmc by reading EXT_CSD in 8 bit data width',
        '                         and comparing to reading EXT_CSD in initial state in 1',
        '                         bit data width',
        ' ',
        '  eeprom               : Verifies the eeprom by reading the CRC value',
        '                         that is stored in byte 255 of eeprom and',
        '                         compares it to a value that is calculated in',
        '                         s/w. This ensure that the data path to and',
        '                         from the EEPROM is good as well as verifies',
        '                         data integrity',
        '----------------------------------------------------------------------',
        ' '
        ]))

    def do_parse(self, params):
        params = params.replace('  ', ' ')
        args = params.split(' ')
        if len(args) > 1:
            tegraflash_update_env()

            compulsory_args = ['--chip', '--applet']

            for required_arg in compulsory_args:
                if exports[required_arg] is None:
                    exports[required_arg] = input('Input ' + required_arg[2:] + ': ')

            try:
                self.chip_inst.tegraflash_parse(exports, args)
            except tegraflash_exception as e:
                print('Error: '+ e.value)
                tegraflash_err(1)
        else:
            self.help_parse()

    def help_parse(self):
        print('\n'.join([
        ' ',
        '---------------------------------------------------------',
        ' Usage: parse <parser> [options]',
        '---------------------------------------------------------',
        ' ',
        ]))

    def do_dump(self, params):
        params = params.replace('  ', ' ')
        args = params.split(' ')
        if len(args) > 0 and len(params) > 0:
            tegraflash_update_env()

            compulsory_args = ['--chip', '--applet']

            for required_arg in compulsory_args:
                if exports[required_arg] is None:
                    exports[required_arg] = input('Input ' + required_arg[2:] + ': ')

            try:
                self.chip_inst.tegraflash_dump(exports, args)
            except tegraflash_exception as e:
                print('Error: '+ e.value)
                tegraflash_err(1)
        else:
            self.help_dump()

    def help_dump(self):
        print('\n'.join([
        ' ',
        '----------------------------------------------------------------------',
        '  Usage: dump <type> [options]',
        '----------------------------------------------------------------------',
        '  Following types can be dumped',
        ' ',
        '  ram [<start offset> <size>] [file] : Dumps the complete ram if start offset and',
        '                                       size are not specified.',
        '  ptm [file]                         : Dumps only the PTM traces.',
        '  custinfo [file]                    : Dumps customer information',
        '                                       ptm & custinfo Not Valid for t186',
        '  eeprom <eeprom_module> [file]      : Dumps the contents of EEPROM',
        '                                       based on module',
        '----------------------------------------------------------------------',
        ' '
        ]))

    def do_burnfuses(self, params):
        params = params.replace('  ', ' ')
        args = params.split(' ')
        if len(args) <= 2:
            tegraflash_update_env()
            compulsory_args = ['--chip', '--applet']

            for required_arg in compulsory_args:
                if exports[required_arg] is None:
                    exports[required_arg] = input('Input ' + required_arg[2:] + ': ')

            try:
                self.chip_inst.tegraflash_burnfuses(exports, args)
            except tegraflash_exception as e:
                print('Error: '+ e.value)
                tegraflash_err(1)
        else:
            self.help_burnfuses()

    def help_burnfuses(self):
        print('\n'.join([
        ' ',
        '----------------------------------------------------------------------',
        '  T194 / T234 Usage: burnfuses <filename.xml> or',
        '  Usage: burnfuses dummy [<filename.xml>] or',
        '  Usage: burnfuses fskp ',
        '----------------------------------------------------------------------',
        '  Takes requests to set values for arbitrary fuses via an xml file input ',
                '  This is unlike the burnfuses command, which sets hard coded fuses ',
                '  like TID/LID/SBK/DK/PKC to mentioned values '
        '----------------------------------------------------------------------',
        ]))

    def do_blowfuses(self, params):
        params = params.replace('  ', ' ')
        args = params.split(' ')
        tegraflash_update_env()
        compulsory_args = ['--chip', '--applet']

        for required_arg in compulsory_args:
            if exports[required_arg] is None:
                exports[required_arg] = input('Input ' + required_arg[2:] + ': ')

        try:
            self.chip_inst.tegraflash_blowfuses(exports, args)
        except tegraflash_exception as e:
            print('Error: '+ e.value)
            tegraflash_err(1)

    def help_blowfuses(self):
        print('\n'.join([
        ' ',
        '----------------------------------------------------------------------',
        '  T210 Usage: blowfuses <filename.xml>',
        '----------------------------------------------------------------------',
        '  Takes requests to set values for arbitrary fuses via an xml file input ',
                '  This is unlike the burnfuses command, which sets hard coded fuses ',
                '  like TID/LID/SBK/DK/PKC to mentioned values '
        '----------------------------------------------------------------------',
        ]))

    def do_readfuses(self, params):
        params = params.replace('  ', ' ')
        args = params.split(' ')
        if len(args) > 0 and len(params) > 0:
            tegraflash_update_env()
        compulsory_args = ['--chip', '--applet']

        for required_arg in compulsory_args:
            if exports[required_arg] is None:
                exports[required_arg] = input('Input ' + required_arg[2:] + ': ')

        try:
            self.chip_inst.tegraflash_readfuses(exports, args)
        except tegraflash_exception as e:
            print('Error: '+ e.value)

    def help_readfuses(self):
        print('\n'.join([
        ' ',
        '----------------------------------------------------------------------',
        '  T210 Usage: readfuses [outputfile]',
        '----------------------------------------------------------------------',
        '  read burnt fuse value of Tid, default output file is dut_fuses.bin',
        '----------------------------------------------------------------------',
        ' ',
        '----------------------------------------------------------------------',
        '  T186 and T194 Usage: readfuses outputfile fuse_xml',
        '----------------------------------------------------------------------',
        '  read the values of the fuses which are defined in fuse_xml',
        '----------------------------------------------------------------------',
        ]))

    def do_flush_sata(self, params):
        tegraflash_update_env()
        try:
            self.chip_inst.tegraflash_flush_sata(args)
        except tegraflash_exception as e:
            print('Error: '+ e.value)
            tegraflash_err(1)

    def help_flush_sata(self):
        print('\n'.join([
        ' ',
        '--------------------------------------------',
        '  Usage: flush_sata',
        '--------------------------------------------',
        '   ',
        ]))

    def do_sata_fwdownload(self, params):
        tegraflash_update_env()
        args = params.split()
        try:
            file_path = tegraflash_abs_path(args[0]) if len(args) >= 1 else None
            self.chip_inst.tegraflash_sata_fwdownload(file_path)
        except tegraflash_exception as e:
            print('Error: '+ e.value)
            tegraflash_err(1)

    def help_sata_fwdownload(self):
        print('\n'.join([
        ' ',
        '--------------------------------------------',
        '  Usage: sata_fwdownload <file>',
        '--------------------------------------------',
        '   ',
        ]))


    def do_setrollback(self, params):
        params = params.replace('  ', ' ')
        args = params.split(' ')
        tegraflash_update_env()
        compulsory_args = ['--chip', '--applet', '--bl']

        for required_arg in compulsory_args:
            if exports[required_arg] is None:
                exports[required_arg] = input('Input ' + required_arg[2:] + ': ')

        try:
            self.chip_inst.tegraflash_provision_rollback(exports, args)
        except tegraflash_exception as e:
            print('Error: '+ e.value)
            tegraflash_err(1)

    def help_setrollback(self):
        print('\n'.join([
        ' ',
        '----------------------------------------------------------------------',
        '  Usage: setrollback ',
        '----------------------------------------------------------------------',
        ]))

    def do_show(self, params):
        args = params.split()
        if len(args) > 1:
            print("Error: More than one arguments")
        elif len(args) == 0:
            for key, value in exports.iteritems():
                print(key[2:] + ' = ' + str(value or 'None'))
        elif len(args) == 1:
            var = '--' + args[0]
            if var in exports:
                val = exports[var]
                if val is None:
                    val = "None"
                print(args[0] + ' = ' + val)
            else:
                print("Invalid " + args[0])

    def help_show(self):
        print('\n'.join([
        ' ',
        '--------------------------------------------------',
        '   Usage: show [variable]',
        '--------------------------------------------------',
        ' '
        ]))

    def do_export(self, params):
        args = params.split()
        if len(args) == 2:
            exports.update({'--'+args[0]:args[1]})
        else:
            print("Error: Invalid number of arguments")

    def do_interact(self, param):
        self.cmdloop()

    def help_export(self):
        print('\n'.join([
            ' ',
            '---------------------------------------------------',
            '  Usage: export <variable> <value>',
            '---------------------------------------------------',
            ' Following variables can be exported',
            '   bct    : Boot Config Table file',
            '   bl     : Command line bootloader',
            '   cfg    : Partition configuration layout',
            '   key    : Key for signing',
            '----------------------------------------------------'
            ' '
        ]))

    def do_readmrr(self, params):
        params = params.replace('  ', ' ')
        args = params.split(' ')
        tegraflash_update_env()

        compulsory_args = ['--chip', '--applet']

        for required_arg in compulsory_args:
            if exports[required_arg] is None:
                exports[required_arg] = input('Input ' + required_arg[2:] + ': ')
        try:
            self.chip_inst.tegraflash_readmrr(exports, args)
        except tegraflash_exception as e:
            print('Error: '+ e.value)
            tegraflash_err(1)

    def do_ufsotp(self, params):
        params = params.replace('  ', ' ')
        params = params.strip(' ')
        args = params.split(' ')
        if params and len(args) == 1:
            tegraflash_update_env()
            compulsory_args = ['--chip', '--applet']

            for required_arg in compulsory_args:
                if exports[required_arg] is None:
                    exports[required_arg] = input('Input ' + required_arg[2:] + ': ')

            try:
                self.chip_inst.tegraflash_ufs_otp(exports, args)
            except tegraflash_exception as e:
                print('Error: '+ e.value)
                tegraflash_err(1)
        else:
            self.help_ufsotp()

    def help_ufsotp(self):
        print('\n'.join([
        ' ',
        '----------------------------------------------------------------------',
        '  Usage: ufsotp <filename.xml> ',
        '----------------------------------------------------------------------',
        '  Takes requests to configure UFS write-once attributes via an xml file ',
        '----------------------------------------------------------------------',
        ]))

    def do_generate_recovery_blob(self, params):
       params = params.replace('  ', ' ')
       args = params.split(' ')
       tegraflash_update_env()

       compulsory_args = ['--chip', '--key', '--bins']
       for required_arg in compulsory_args:
           if exports[required_arg] is None:
               exports[required_arg] = input('Input ' + required_arg[2:] + ': ')

       try:
           self.chip_inst.tegraflash_generate_recovery_blob(exports, args)

       except tegraflash_exception as e:
           print('Error: '+ e.value)
           tegraflash_err(1)


    def help_generate_recovery_blob(self):
        print('\n'.join([
        ' ',
        '----------------------------------------------------------------------',
        '  Usage: generate_recovery_blob <outfilename>',
        '----------------------------------------------------------------------',
        ]))

    def do_updaterpmb(self, params):
        params = params.replace('  ', ' ')
        args = params.split(' ')
        tegraflash_update_env()

        compulsory_args = ['--bl', '--odmdata']

        for required_arg in compulsory_args:
            if exports[required_arg] is None:
                exports[required_arg] = input('Input ' + required_arg[2:] + ': ')

        try:
            self.chip_inst.tegraflash_update_rpmb(exports)
        except tegraflash_exception as e:
            print('Error: '+ e.value)

    def help_updaterpmb(self, params):
        print('\n'.join([
        ' ',
        '----------------------------------------------------------------------',
        '  Usage: updaterpmb',
        '----------------------------------------------------------------------',
        ]))

    def do_help(self, param):
        if (len(param) > 1):
            cmd.Cmd.do_help(self, param)
        else:
            print("");
            print("Commonly used tegraflash Commands")
            print("------------------------------------------")
            print("   flash         : Flash the device")
            print("   secureflash   : Flash device with pre-signed binaries")
            print("   read          : Read a partition")
            print("   write         : Write a partition")
            print("   erase         : Erase a partition or complete storage")
            print("   setverify     : Set Verification for partition")
            print("   verify        : Verify enabled partitions")
            print("   dump          : Dump data from device")
            print("   test          : Run basic tests")
            print("   reboot        : Reboot the device")
            print("   rcmbl         : Boot bootloader without flashing")
            print("   rcmboot       : Boot kernel without flashing")
            print("   sign          : Sign binaries")
            print("   signwrite     : Sign binary and update the partition")
            print("   ufsotp        : Program UFS device pre-configurations")
            print("   export        : Export variables")
            print("   show          : List export variables")
            print("------------------------------------------")
            print(" help <command> gives help on command")
            print(" q or quit to quit terminal")
            print("")

def tegraflash_run_commands():
    global exit_on_error
    commands = exports['--cmd'].split(';')
    interpreter = tegraflashcmds()
    exit_on_error = True
    for command in commands:
        interpreter.onecmd(command)

def tegraflash_update_env():
    path_separator = ":"
    if sys.platform == 'win32':
        path_separator = ";"

    cmd_environ["PATH"] = paths['BIN'] + path_separator + paths['WD'] + path_separator + paths['OUT'] \
                                  + path_separator + paths['SCRIPT'] + path_separator + paths['TMP'] + path_separator + os.environ["PATH"]


if __name__ == '__main__':
    options = ["skipuid", "bct=", "bct_cold_boot=", "cfg=", "bl=", "hostbin=", "cmd=", "key=", "encrypt_key=","instance=",
               "out=", "chip=", "dtb=", "bldtb=", "kerneldtb=", "bpfdtb=", "nct=", "applet=", "fb=", "odmdata=", "overlay_dtb=",
               "lnx=", "tos=", "eks=", "boardconfig=", "securedev", "keyindex=", "wb=", "keep", "secureboot",
               "bl-load=", "bins=", "dev_params=", "sdram_config=", "ramcode=", "misc_config=", "misc_cold_boot_config=",
               "mb1_bct=", "mb2_bct=", "mb2_cold_boot_bct=", "mb2bct_cfg=", "ecid=",
               "pinmux_config=", "scr_config=", "scr_cold_boot_config=",
               "pmc_config=", "pmic_config=", "gpioint_config=", "uphy_config=", "br_cmd_config=",
               "prod_config=", "device_config=", "applet-cpu=", "bpf=", "skipsanitize",
               "encrypt_key=", "enable_user_kdk", "nv_key=", "nvencrypt_key=", "cl=", "soft_fuses=", "cust_info=",
               "deviceprod_config=", "rcm_bct=","mem_bct=", "mem_bct_cold_boot=", "mb1_cold_boot_bct=", "wb0sdram_config=",
               "minratchet_config=", "blversion=", "output_dir=", "nv_nvratchet=", "nv_oemratchet=", "image_dirs=",
               "trim_bpmp_dtb", "cpubl=", "concat_cpubl_bldtb", "external_device", "sparseupdate", "ratchet_blob=",
               "applet_softfuse=", "secondary_gpt_backup", "boot_chain=", "bct_backup",
               "mb1_bin=", "psc_bl1_bin=", "rcmboot_pt_layout=", "coldboot_pt_layout=", "rcmboot_bct_cfg=", "coldboot_bct_cfg=",
               "dce_base_dtb=", "dce_overlay_dtb="]

    try:
      opts, args = getopt.getopt(sys.argv[1:], "h", options)
    except getopt.GetoptError:
        usage()
        sys.exit(1)

    exports.update(dict(opts))

    if '--skipuid' in sys.argv[1:]:
        exports['--skipuid'] = True

    if '--skipsanitize' in sys.argv[1:]:
        exports['--skipsanitize'] = True

    if '--securedev' in sys.argv[1:]:
        exports['--securedev'] = True

    if '--keep' in sys.argv[1:]:
        exports['--keep'] = True

    if '--external_device' in sys.argv[1:]:
        exports['--external_device'] = True

    if '--sparseupdate' in sys.argv[1:]:
        exports['--sparseupdate'] = True

    if '--secureboot' in sys.argv[1:]:
        exports['--secureboot'] = True

    if '--trim_bpmp_dtb' in sys.argv[1:]:
        exports['--trim_bpmp_dtb'] = True

    if '--concat_cpubl_bldtb' in sys.argv[1:]:
        exports['--concat_cpubl_bldtb'] = True

    if '--secondary_gpt_backup' in sys.argv[1:]:
        exports['--secondary_gpt_backup'] = True

    if '--bct_backup' in sys.argv[1:]:
        exports['--bct_backup'] = True

    if '--enable_user_kdk' in sys.argv[1:]:
        exports['--enable_user_kdk'] = True

    abs_path = ['--bct', '--rcm_bct', '--cfg', '--bl', '--hostbin', '--key', '--encrypt_key', '--out', '--dtb', '--bldtb', '--kerneldtb',
                '--nct', '--applet', '--fb', '--lnx', '--tos', '--eks', '--wb', '--bpfdtb', '--applet_softfuse',
                '--boardconfig', '--applet-cpu', '--bpf', '--mb1_bct', '--mb2_bct', '--encrypt_key', '--nvencrypt_key', '--nv_key',
                '--mem_bct', '--mem_bct_cold_boot', '--mb1_cold_boot_bct', '--mb2_cold_boot_bct', '--wb0sdram_config']
    for path in abs_path:
        if exports[path] is not None:
            if os.path.dirname(exports[path]):
                exports[path] = os.path.abspath(exports[path])
                exports[path] = tegraflash_os_path(exports[path])

    paths['SCRIPT'] =  os.path.abspath(os.path.dirname(__file__))
    paths['OUT'] = os.getcwd()
    if exports['--hostbin'] is None:
        paths['BIN'] = os.path.abspath(os.path.dirname(__file__))
    else:
        paths['BIN'] = os.path.abspath(exports['--hostbin'])

    if exports['--out'] is not None:
        paths['OUT'] = os.path.abspath(exports['--out'])

    sys.path.insert(1, paths['OUT'])

    # Create a tmporary directory with pid as name
    # Create symlinks for all the files in current directory
    paths['TMP'] = os.path.abspath(paths['OUT'] + "/" + str(os.getpid()))

    chip = exports['--chip']
    chip = chip.replace('  ', ' ')
    chip = chip.strip(' ')
    chip = chip.split(' ')
    exports['--chip'] = chip[0]

    exports['--key'] = exports['--key'].strip()
    exports['--key'] = exports['--key'].replace("  ", " ")
    exports['--key'] = exports['--key'].split(" ")

    if exports['--encrypt_key'] is not None:
        keys = exports['--encrypt_key']
        keys = keys.strip(' ')
        keys = keys.replace('  ', ' ')
        exports['--encrypt_key'] = keys.split(' ')

    if exports['--rcm_bct'] is None:
        exports['--rcm_bct'] = exports['--bct']

    if len(chip) >= 2:
        exports['--chip_major'] = chip[1]

    if len(chip) >= 3:
        exports['--chip_minor'] = chip[2]

    if (int(exports['--chip'], 0) != 0x21):
        exports['--tegraflash_v2'] = True

    if exports['--blversion'] is not None:
        blversion = exports['--blversion']
        blversion = blversion.replace('  ', ' ')
        blversion = blversion.split(' ')
        exports['--majorversion'] = blversion[0].strip()
        exports['--minorversion'] = blversion[1].strip()

    # Create a list from a comma separated list of dirctories
    if (exports['--image_dirs'] is not None):
        image_dir_list = exports['--image_dirs'].split(",")
        image_dir_list = [os.path.expanduser(path) for path in image_dir_list]
        exports['--image_dirs'] = image_dir_list

    try:
        os.makedirs(paths['TMP'])
    except OSError as e:
        paths['TMP'] = expanduser("~") + '/' + str(os.getpid())
        os.makedirs(paths['TMP'])
    retries = 2

    while retries > 0:
        os.chdir(paths['TMP']);
        try:
            for files in os.listdir(paths['OUT']):
                if os.path.isfile(paths['OUT'] + '/' + files):
                    tegraflash_symlink(paths['OUT'] + '/' + files, files)
            retries = 0;
        except OSError as e:
            shutil.rmtree(paths['TMP'])
            paths['TMP'] = expanduser("~") + '/' + str(os.getpid())
            os.makedirs(paths['TMP'])
            retries = retries - 1

    if exports['--cfg'] is not None:
        exports['--cfg'] = tegraflash_update_img_path(exports['--cfg'], exports['--image_dirs'])

    try:
        if exports["--cmd"] is None:
            tegraflashcmds().cmdloop()
        else:
            tegraflash_run_commands()

    except tegraflash_exception as e:
        print('Error: '+ e.value)

    finally:
        # Delete the temporary directory created
        os.chdir(paths['WD']);
        if exports['--keep'] is True:
            print('Keep temporary directory ' + paths['TMP'])
        else:
            shutil.rmtree(paths['TMP'])
