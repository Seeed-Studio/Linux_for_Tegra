#!/usr/bin/python
# SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
# property and proprietary rights in and to this material, related
# documentation and any modifications thereto. Any use, reproduction,
# disclosure or distribution of this material and related documentation
# without an express license agreement from NVIDIA CORPORATION or
# its affiliates is strictly prohibited.

""" Module for asymmetric boot chain functionality

    This module implements functionality specific to asymmetric boot chains
    generation and flashing

    Current functions are:
        - Preserve Sku info in BR BCT from the target when flashing
        - Merge BR BCT according to the specified layout of BR BCT blob
        - Generate specialize BCTs for asymmetric chains

"""

from __future__ import print_function

import os.path
import json
import glob
import shutil

from flash_utilities import shell_utilities
from flashtools_nverror import nverror, AbnormalTermination
from bootburn_bct import BootburnAsymmetricDefault, PlatformJsonDefines


class AsymmetricFlashing:
    """ Class to define helper functions for asymmetric boot chain flashing
    """

    def __init__(self, json_bct_config):
        """Initialize AsymmetricFlashing object

        Args:
            json_bct_config (str): Path to JSON BCT blob layout configuration file
        """
        if (not os.path.exists(json_bct_config)):
            AbnormalTermination(
                "BCT configuration for asmmetric boot chain not found: %s".format(json_bct_config),
                nverror.NvError_FileNotFound)

        # Load the JSON config file
        with open(json_bct_config, "r") as bct_cfg:
            self.bct_cfg = json.load(bct_cfg)

    def validate_bct_blob_json(self):
        # TODO: add validation for input json
        pass

    def preserve_sku_info(
            self, input_bct, chip_id, flash_images_path, bootburn_lib=None):
        """Preserve sku info from either supplied arguments or target BCT

        Args:
            input_bct (str): Input BCT file name from the device
            chip_id (str): Chip id hex string
            flash_images_path (str): Output flash images directory path
            bootburn_lib (Object of bootburn_lib): Object of bootburn_lib
        """

        if (bootburn_lib == None):
            AbnormalTermination(
                "Bootburn lib object not given for asymmetric bct handling!!")

        if (bootburn_lib.targetConfig.skuArguments["s_NvSkuArgs"]):
            # If the sku arguments are present either from create bsp or given
            # during flash bsp, preserve those values in BR BCT
            for index in range(len(self.bct_cfg["bct_blob_layout"])):
                bct_file_name = self.bct_cfg["bct_blob_layout"][index][
                    "file_name"]
                bct_file_name = os.path.join(flash_images_path, bct_file_name)
                bootburn_lib.SkuBlobGen(bct_file_name)
        else:
            # "Preserve metadata from obtained BCT (inbct) to the targeted BCT (outbct)"
            for index in range(len(self.bct_cfg["bct_blob_layout"])):
                bct_file_name = self.bct_cfg["bct_blob_layout"][index][
                    "file_name"]
                bct_file_name = os.path.join(flash_images_path, bct_file_name)
                sku_command = bootburn_lib.flashUtils.f_NvSkuInfo + " --chip " + \
                    chip_id + " --inbct " + input_bct + " --outbct " + bct_file_name

                sku_command += " -t u"

                result = bootburn_lib.shellUtils.executeShellCommand(
                    sku_command)
                if (result != nverror.NvError_Success):
                    AbnormalTermination(
                        "s_ERROR_TOOL_NVSKUINFO", nverror.NvError_TegraSkuInfoError)

    def merge_bcts_into_blob(self, output_blob_file_name, chip_id="", newfskpBrBct="", f_NvSkuInfo=""):
        """Generate BR BCT binary blob from BR BCT layout information

        Args:
            output_blob_file_name (str): Output BR BCT blob file path
            chip_id (str): Chip id hex string
            newfskpBrBct (str): New Fskp BR BCT blob file path
        """
        total_bct_file_size = self.bct_cfg["total_num_bct_copies"] * self.bct_cfg["bct_size"]

        # Create zero filed BR BCT binary blob
        dd_cmd = "dd if=/dev/zero of=" + output_blob_file_name + \
            " ibs=1 count=" + str(total_bct_file_size)
        result = shell_utilities().executeShellCommand(dd_cmd)
        if (result != nverror.NvError_Success):
            AbnormalTermination(
                "s_ERROR_INVALID_PARAMS -- dd failed", nverror.NvError_InvalidArgument)

        print("BCT file size: ", total_bct_file_size)

        # Copy BCT files in the blob according to the BCT blob layout
        with open(output_blob_file_name, "rb+") as bct_blob:
            for index in range(len(self.bct_cfg["bct_blob_layout"])):
                bct_file = self.bct_cfg["bct_blob_layout"][index]["file_name"]

                if ("fskp" in bct_file):
                    if (newfskpBrBct != ""):
                        cwd = os.getcwd()
                        new_cwd = os.path.dirname(f_NvSkuInfo)
                        os.chdir(new_cwd)
                        bct_file=os.path.join(cwd, bct_file)
                        sku_command = f_NvSkuInfo + " --chip " + \
                            chip_id + " --inbct " + bct_file + " --outbct " + newfskpBrBct + " -f"
                        sku_command += " -t b"

                        result = shell_utilities().executeShellCommand(
                            sku_command)
                        if (result != nverror.NvError_Success):
                            AbnormalTermination(
                                "s_ERROR_TOOL_NVSKUINFO", nverror.NvError_TegraSkuInfoError)

                        os.chdir(cwd)
                        bct_file = newfskpBrBct

                bct_blob_index_list = self.bct_cfg["bct_blob_layout"][index][
                    "blob_index_list"]

                with open(bct_file, "rb") as in_file:
                    bct_data = in_file.read()

                for blob_index in bct_blob_index_list:
                    offset = self.bct_cfg["bct_size"] * blob_index
                    bct_blob.seek(offset, 0)
                    bct_blob.write(bct_data)

class AsymmetricBct:
    """ Class to define helper functions for asymmetric boot chain BCT
        image generation
    """

    def __init__(self, bootburn_lib):
        self.bootburn_lib =  bootburn_lib

    def generate_special_asymmetric_bct(self, chain, operation, partitionList, outputDir):
        """Function to generate special raw BCT for asymmentric boot chains

        Args:
            chain (str): chain id - A or B or C..
            operation (str): operation from (rcm-flash, rcm-boot, flash-images)
            partitionList (list): List of partitions in the storage config
            outputDir (str): Output directory where generated BCT is stored

        Raises:
            AbnormalTermination: raises exception if invalid arguments are found
        """
        if (chain == None):
            raise AbnormalTermination(
                "Chain needs to be specified for generating special BCTs for asymmetric chains",
                nverror.NvError_InvalidArgument
            )

        if (chain == 'B' and operation == "flash-images"):
            # Generate default BCT
            definesOb = PlatformJsonDefines()
            bctDefines = definesOb.getDefines(operation, self.bootburn_lib.targetConfig, partitionList, chain)

            bctOb = BootburnAsymmetricDefault(self.bootburn_lib.targetConfig, outputDir)
            bctOb.generateBct(bctDefines)

    def generate_l1pt_metadata(self, operation, chain):
        """Generate L1PT metadata for asymmetric chain

        Args:
            operation (str): operation from (rcm-flash, rcm-boot, flash-images)
            chain (str): chain id - A or B or C..
        """
        if (operation == "flash-images"):
            # Generate L1PT metadata for both the chains
            l1pt_metafile_name = chain + "_l1pt_meta" + ".txt"
            # Find PT file matching pattern [0-9]_PT.bin
            pt_files = glob.glob("[0-9]_PT.bin")
            if not pt_files:
                raise AbnormalTermination("No PT file found matching pattern [0-9]_PT.bin",
                    nverror.NvError_FileNotFound)
            if len(pt_files) > 1:
                raise AbnormalTermination("Multiple PT files found. Expected exactly one [0-9]_PT.bin file",
                    nverror.NvError_InvalidState)
            pt_file = pt_files[0]
            nvpt_cmd = self.bootburn_lib.flashUtils.f_Nvpt + " --pt " + pt_file + " --chip 0x26 --out " + l1pt_metafile_name
            pt_meta_output = self.bootburn_lib.shellUtils.executeShellCommand(nvpt_cmd, True)

            with open(l1pt_metafile_name, "w") as l1pt_meta:
                l1pt_meta.write(pt_meta_output)

    def get_chain_data_from_l1pt(self, src_chain_dir, dst_chain_dir, src_chain, dst_chain):
        """Get chain data from L1PT metadata file

        Args:
            src_chain_dir (str): Source chain directory path
            dst_chain_dir (str): Destination chain directory path
            src_chain (str): Srouce chain id
            dst_chain (str): Destination chain id

        Returns:
            (dict, dict): Returns source and destination l1pt data dictionaries
        """

        # Load dest l1pt metafile
        src_l1pt_metafile_path = os.path.join(src_chain_dir, src_chain + "_l1pt_meta" + ".txt")
        dst_l1pt_metafile_path = os.path.join(dst_chain_dir, dst_chain + "_l1pt_meta" + ".txt")
        src_dict = dict()
        dst_dict = dict()
        with open(src_l1pt_metafile_path, "r") as src_l1pt_metafile, open(dst_l1pt_metafile_path, "r") as dst_l1pt_metafile:
            src_lines = src_l1pt_metafile.readlines()
            dst_lines = dst_l1pt_metafile.readlines()

            # Create src dict
            for src_line in src_lines:
                if (src_line.startswith("#")):
                    continue
                line_parts = src_line.split(" ")
                src_dict[line_parts[1]] = (int(line_parts[4]), int(line_parts[5]))

            # Create dst dict
            for dst_line in dst_lines:
                if (dst_line.startswith("#")):
                    continue
                line_parts = dst_line.split(" ")
                dst_dict[line_parts[1]] = (int(line_parts[4]), int(line_parts[5]))

        return src_dict, dst_dict


    def make_chain_compatible(self, src_chain_dir, src_dict, dst_dict, chain_id):
        """Check if the source chain fits into destination chain container and
           make source chain partition offsets compatible with destination chain
           container by adjusting source chain partition offsets based on destination
           chain start offsets

        Args:
            src_chain_dir (str): Source chain directory
            src_dict (str): Source chain L1PT data dictionary
            dst_dict (str): Destination chain L1PT data dictionary
            chain_id (str): Source chain id

        Returns:
            bool: Returns True if source chain is compatible, false otherwise
        """
        for partition in src_dict.keys():
            if (not partition.startswith(chain_id)):
                continue

            if (partition.startswith(chain_id) and partition not in dst_dict):
                return False

            # Make sure chain in src container fits in dst container
            dst_part_size = dst_dict[partition][1] - dst_dict[partition][0] + 1
            src_part_size = src_dict[partition][1] - src_dict[partition][0] + 1

            if (src_part_size > dst_part_size):
                return False

            # Adujust partition/device image offsets if required
            if (partition.startswith(chain_id)):
                self.__adjust_chain_offsets(
                    partition,
                    src_chain_dir,
                    src_dict[partition][0],
                    dst_dict[partition][0]
                )

        return True

    def __adjust_chain_offsets(self, partition, src_dir, src_chain_start_offset, dst_chain_start_offset):
        """If required, adjust source chain partition offsets in FileToFlash.txt for
           given device based on source and destination chain start offsets

        Args:
            partition (str): L1PT chain specific partition type
            src_dir (str): Source chain directory
            src_chain_start_offset (int): Source chain start offset
            dst_chain_start_offset (int): Destination chain start offset
        """

        # Get device based on partition name
        device = None
        if ("qspi" in partition):
            device = "/dev/block/3270000.spi"
        elif ("emmc" in partition):
            device = "/dev/block/3460000.sdhci"
        else:
            device = "/dev/block/a80b8d0000.ufshci"

        # Calculate adjustment in offset to acccomodate src chain to dst chain container
        device_offset = dst_chain_start_offset - src_chain_start_offset

        if (device_offset != 0):
            src_file_to_flash_path = os.path.join(src_dir, "FileToFlash.txt")
            tmp_file_to_flash_path = os.path.join(src_dir, "tmp_FileToFlash.txt")
            with open(src_file_to_flash_path, "r") as file_to_flash:
                with open(tmp_file_to_flash_path, "w") as tmp_file_to_flash:
                    lines = file_to_flash.readlines()

                    for line in lines:
                        if (device in line):
                            # Add offset
                            line_parts = line.split(" ")
                            line_parts[3] = str(int(line_parts[3]) + device_offset)
                            line = " ".join(line_parts)

                        tmp_file_to_flash.write(line)

            shutil.move(tmp_file_to_flash_path, src_file_to_flash_path)

    def run_nvimagegen_for_special_bct(self, chain, operation, nvimagegen_cmd, output_dir):
        """Pass raw BCT through nvimagegen

        Args:
            chain (str): chain id - A or B or C..
            operation (str): operation from (rcm-flash, rcm-boot, flash-images)
            nvimagegen_cmd (str): nvimagegen command for the chain
            output_dir (str): Output directory where bct image is stored
        """

        if (chain == 'B' and operation == "flash-images"):

            # Get special BCT file name
            bctOb = BootburnAsymmetricDefault(self.bootburn_lib.targetConfig, output_dir)
            bct_file_name = bctOb.getBctFileName()

            # process bct through nvimagegen
            self.__process_bct(nvimagegen_cmd, output_dir, bct_file_name, chain)

            # delete raw BCT file
            bctOb.deleteFiles()

        if (chain == 'A' and operation == "flash-images"):

            fskp_bct_path = os.path.join(output_dir, "bct_fskp.bct")

            # Copy FSKP BCT to output directory
            shutil.copyfile(self.bootburn_lib.targetConfig.fskpBctPath, os.path.join(output_dir, fskp_bct_path))

            # process bct through nvimagegen
            self.__process_bct(nvimagegen_cmd, output_dir, 'bct_fskp.bct', chain)

    def __process_bct(self, nvimagegen_cmd, output_dir, bct_file_name, chain):
        """Processes BR BCT through nvimagegen

        Args:
            nvimagegen_cmd (str): nvimagegen command to generate chain
            output_dir (str): Output directory where images are generated
            bct_file_name (str): Input BCT file name to be processed
            chain (str): chain id - A or B or C..
        """

        # Temporary copy FileToFlash.txt if it is there to tmp name
        shutil.move(os.path.join(output_dir, chain + "_FileToFlash.txt"), "tmp_FileToFlash.txt")

        # replace string in nvimagegen command
        nvimagegen_cmd = nvimagegen_cmd.replace("bct_BR.bct", bct_file_name)

        # Generate PT only
        self.bootburn_lib.shellUtils.executeShellCommand(nvimagegen_cmd + " --ptonly", False, False)

        # Generate BCT partition
        self.bootburn_lib.shellUtils.executeShellCommand(nvimagegen_cmd + " --part bct", False, False)

        # Copy original FileToFlash.txt
        shutil.move(os.path.join(output_dir, "tmp_FileToFlash.txt"), chain + "_FileToFlash.txt")

    def generate_bct_blob_layout(self, output_dir):
        """Generate BR BCT blob layout for asymmetric boot chain flashing

        Args:
            output_dir (str): Output directory where blob layout json file is generated
        """

        layout_json_file_path = os.path.join(output_dir, self.bootburn_lib.targetConfig.brBctLayoutJsonFile)

        # Generate BR BCT Json configuration for asymmetric boot chain
        bct_blob_info = {
            "num_bcts": 4,
            "total_num_bct_copies": 4,
            "bct_size": 16384,
            "bct_blob_layout": [
                {
                    "file_name": "A_bct_BR_zerosign.bct",
                    "blob_index_list": [0]
                },
                {
                    "file_name":  glob.glob("A_bct_*fskp*")[0],
                    "blob_index_list": [8]
                },
                {
                    "file_name":  glob.glob("B_bct_BR_*")[0],
                    "blob_index_list": [16]
                },
                {
                    "file_name":  glob.glob("B_bct_*default*")[0],
                    "blob_index_list": [24]
                },
            ]
        }

        with open(layout_json_file_path, "w") as bct_blob_cfg:
            json.dump(bct_blob_info, bct_blob_cfg, indent=4)


    def update_bct_blob_json(self, json_file, headers_dir):
        """Update BCT JSON blob layout with new BCT files
           if present

        Args:
            json_file (str): Path to BCT Json blob layout file
            headers_dir (str): Path to headers directory where new
                               BCT files are located
        """
        with open(json_file, 'r') as bct_layout_json:
            bct_cfg = json.load(bct_layout_json)

        for bct_cfg_entry in bct_cfg['bct_blob_layout']:
            old_file_name = bct_cfg_entry['file_name']
            old_file_base_name, _ = os.path.splitext(os.path.basename(old_file_name))
            new_file_path =  os.path.join(headers_dir, old_file_base_name + "_resigned.bct")

            if (os.path.exists(new_file_path)):
                bct_cfg_entry['file_name'] = new_file_path

        with open(json_file, 'w') as bct_layout_json:
            json.dump(bct_cfg, bct_layout_json, indent=4)
