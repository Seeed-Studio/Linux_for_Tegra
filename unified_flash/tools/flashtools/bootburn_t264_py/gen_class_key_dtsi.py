#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: Copyright (c) 2023-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA Corporation is strictly prohibited.
#
'''
@Description
The purpose of this file is to provide a way to generate dtsi files with class key entries.

For partition with class key enabled, dtsi files are necessary that are included in main partition dtsi
like MB2 and Kernel. This scripts takes the key information from tegrasign_v3_debug.yaml file and updates that

@Note

'''
import subprocess
import sys
import argparse
import os
import shutil
import tempfile
import post_processing_tool
import logging

logging.basicConfig(
    level=logging.DEBUG,
    format='%(asctime)s [%(name)s] %(levelname)s:%(message)s')
logger = logging.getLogger("Gen class key dtsi")

magic_ids_sif = ['SIF0', 'SIF1', 'SIF2', 'SIF3', 'SIF4', 'SIF5', 'SIF6', 'SIF7']
magic_ids_mb2 = ['MISC', 'PVIT']
magic_ids_ist = ['KIST'] # use KIST for both KIST and RIST since they share the same key
magic_id_to_name_override = {'MISC' : 'disp', 'PVIT' : 'pvit'}

def get_mb2_dtsi_txt(name_str, key_str, revoke_key_str, algo_code):
    key_part_0 = ("        %s@0 {\n            class_key_type = <%s>;\n            class_key_hash = %s\n        };\n" % (name_str, algo_code, key_str))
    key_part_1 = ("        %s@1 {\n            class_key_type = <%s>;\n            class_key_hash = %s\n        };\n" % (name_str, algo_code, revoke_key_str))
    dtsi_txt = ("/ {\n    mb2-misc {\n%s%s\n    };\n};" % (key_part_0, key_part_1))
    return dtsi_txt

def get_sif_dtsi_txt(name_str, key_str, revoke_key_str, algo_code):
    dtsi_txt  = ("/ {\n    %s@0 {\n        class_key_type = <%s>;\n        class_key_hash = %s\n    };\n" % (name_str, algo_code, key_str))
    dtsi_txt += ("    %s@1 {\n        class_key_type = <%s>;\n        class_key_hash = %s\n    };\n};" % (name_str, algo_code, revoke_key_str))
    return dtsi_txt

def get_ist_dtsi_txt(key_str):
    str_list = key_str.split(';')
    dtsi_txt  = ("        ist_oem_auth_keys {\n            %s;\n          %s;\n        };\n" % (str_list[0], str_list[1]))
    return dtsi_txt

def get_mb1_dtsi_txt(key_str):
    dtsi_txt = ("/ {\n    mb1_bct {\n%s\n    };\n};" % (key_str))
    return dtsi_txt

def generate_dtsi(args, magic, magic_type, kdf_yaml=None):
    try:
        kdfr_str = ""
        if kdf_yaml:
            kdfr_str = "--kdf kdf_file=" + kdf_yaml

        exp_str = ""
        is_ist = magic in magic_ids_ist
        if is_ist:
            exp_str = " rsa eee "

        cmd = './tegrasign_v3.py --hsm %s %s  --pubkeyhash ppp hhh %s --verbose %s' %(args.hsm, magic, exp_str, kdfr_str)
        logger.debug(cmd)
        log = subprocess.check_output(cmd, shell=True).decode("utf-8")
        key_str = log.split('\n')[-2].replace('dts format: ', '')

        if not is_ist:
            cmd = './tegrasign_v3.py --hsm ' + args.hsm + ' ' + magic + ' revoke --pubkeyhash ppp hhh --verbose ' + kdfr_str
            logger.debug(cmd)
            log = subprocess.check_output(cmd, shell=True).decode("utf-8")
            revoke_key_str = log.split('\n')[-2].replace('dts format: ', '')
    except Exception as e:
        logger.info(" No dtsi generated for - " + magic)
        logger.debug(e)
        return

    name_str = magic_id_to_name_override.get(magic, magic)
    if args.hsm == 'eddsa':
        algo_code = 4
    elif args.hsm == 'rsa':
        algo_code = 1
    elif args.hsm == 'xmss':
        algo_code = 5
    elif args.hsm == 'ecc':
        algo_code = 2
    elif args.hsm == 'ecc521':
        algo_code = 3

    else:
        raise RuntimeError("Unsupported signing algorithm")
    if magic_type == 0:
        dtsi_txt = get_sif_dtsi_txt(name_str, key_str, revoke_key_str, algo_code)
    elif magic_type == 1:
        dtsi_txt  = get_mb2_dtsi_txt(name_str, key_str, revoke_key_str, algo_code)
    elif magic_type == 2:
        dtsi_txt = get_ist_dtsi_txt(key_str) # hardcode to rsa
        dtsi_txt = get_mb1_dtsi_txt(dtsi_txt)
    dtsi_file_path = os.path.join(args.out_dir, magic + "_class_key.dtsi")
    with open(dtsi_file_path, "w") as f:
        f.write(dtsi_txt)
    logger.info(f" Success -- dtsi generated for - {magic} at {dtsi_file_path}" )

def main(commandline):
    parser = argparse.ArgumentParser(description='Generate class key dtsi files')
    parser.add_argument('-b', '--board', help='Board name', required=True)
    parser.add_argument('--hsm', help='HSM mode algorithm', required=True)
    parser.add_argument('--out_dir', help='HSM mode algorithm')
    parser.add_argument('--debug', help='debug logs', action='store_true')
    parser.add_argument('--keep', help='debug logs', action='store_true')

    args = parser.parse_args(commandline)

    logger.setLevel(logging.INFO)
    if (args.debug):
        logger.setLevel(logging.DEBUG)

    tegra_top = os.getenv('TEGRA_TOP')
    pdk_top = os.getenv('PDK_TOP')
    top = ""

    if tegra_top is not None and len(tegra_top) > 0:
        top = tegra_top
    elif pdk_top is not None and len(pdk_top) > 0:
        top = pdk_top
    else:
        raise RuntimeError("TEGRA_TOP or PDK_TOP not set.")

    DIR_BEING_RUN_FROM = os.path.dirname(os.path.realpath(__file__))
    file_path = os.path.join(DIR_BEING_RUN_FROM, "..", "..", "..", "tools", "flashtools")
    if os.path.isdir(file_path):
        isPdk = True;
    else:
        isPdk = False;

    chipid = "0x230"
    if '264' in DIR_BEING_RUN_FROM:
        chipid = "0x260"

    if args.out_dir is None or len(args.out_dir) < 1:
        if isPdk:
            NV_SDK_NAME_FOUNDATION = 'unified_flash'
            pdk_top = os.path.abspath(os.path.join(DIR_BEING_RUN_FROM, "..", "..", ".."))
            if(os.path.isdir(os.path.join(pdk_top, "..", NV_SDK_NAME_FOUNDATION)) is True):
                pdk_top = os.path.abspath(os.path.join(pdk_top, "..", NV_SDK_NAME_FOUNDATION))
            else:
                raise RuntimeError("Detected Sdk Env but can't find pdk top %s" % (pdk_top))
            args.out_dir = os.path.join(pdk_top, 'out', 'class_key', args.board)
        else:
            nv_out_dir = os.getenv('NV_OUTDIR')
            if nv_out_dir is None or len(nv_out_dir) < 1:
                raise RuntimeError("Detected Dev Env but NV_OUTDIR undefined.  Did you use eenv?")
            args.out_dir = os.path.join(nv_out_dir, 'class_key', args.board)

    logger.debug(args.out_dir)

    if args.out_dir and os.path.exists(args.out_dir):
        logger.debug('clearing out_dir')
        shutil.rmtree(args.out_dir)
    os.makedirs(args.out_dir)
    logger.info(" output directory: " + args.out_dir)

    tempdir = tempfile.mkdtemp()
    os.chdir(tempdir)
    logger.debug(tempdir)
    tegrasign_cmd = post_processing_tool.CopyTegraSignCommand(tempdir)
    tegrasign_cmd.execute()

    kdf_yaml = 'kdf_arg.yaml'
    with open(kdf_yaml, 'w') as f:
        f.write('CHIPID : "%s"\n' %(chipid))

    for magic in magic_ids_sif:
        generate_dtsi(args, magic, 0, kdf_yaml)
    for magic in magic_ids_mb2:
        generate_dtsi(args, magic, 1, kdf_yaml)

    # Back up hsm type since KIST only supports RSA
    hsm_bak = args.hsm
    for magic in magic_ids_ist:
        args.hsm = 'rsa'
        generate_dtsi(args, magic, 2)
    args.hsm = hsm_bak
    if (not args.keep):
            shutil.rmtree(tempdir)

if __name__=='__main__':
    main(sys.argv[1:])
