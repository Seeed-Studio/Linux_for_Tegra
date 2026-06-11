#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
# property and proprietary rights in and to this material, related
# documentation and any modifications thereto. Any use, reproduction,
# disclosure or distribution of this material and related documentation
# without an express license agreement from NVIDIA CORPORATION or
# its affiliates is strictly prohibited.

import argparse
import os
import subprocess
import shutil
import hashlib
import fileinput
import math
import json
import xml.etree.ElementTree as ET
import glob


script_directory =  os.path.dirname(os.path.realpath(__file__))

class Base:
    """This class contains platform specific information"""

    def __init__(self):
        self.device_name_to_dev = {
            "3:0": "/dev/block/810c5b0000.spi",
            "1:3": "/dev/block/3460000.sdhci",
            "8:0": "/dev/block/a80b8d0000.ufshci:0",
            "12:4": "/dev/nvme0n1",
            "12:7": "/dev/nvme0n1"
        }
        self.device_name_to_blocksize = {"3:0": 262144, "1:3": 512, "8:0": 4096, "12:4": 512, "12:7": 512, "9:0": 512}
        self.json_path = "tools/flashtools/board_configs/jetson-t264.json"
        self.bpmp_info = "./tools/flashtools/board_configs/t264/bpmpInfo.json"
        self.json_file_name="jetson-t264"
        self._md5_cache = {}
        self._copied_files = set()

    def update_external_device_info(self, external_device):
        """Update the external device information provided from the parameters"""
        self.device_name_to_dev["9:0"] = f"/dev/{get_disk_name(external_device[0])}"
        self.device_name_to_blocksize["9:0"]= get_block_size(external_device[1])

    def translate_l4t_to_auto(self, flash_index, output_file, flash_config, var_extension, append=False):
        """This function translate from l4t format to automotive format"""
        if not append:
            output_file.write(
                "# LinuxPartitionName, PartitionName, FileName, Start, Size, BlockCount, Resize, sku_dependent, BchPartitionName, ImageHeaderType, MD5, ReadWrite\n"
            )
        dirname_path = os.path.dirname(flash_index.name)
        # Read each line
        for line in flash_index:
            # Split the line into variables
            (
                index,
                device_name_partname,
                start,
                size,
                name,
                file_size,
                extra,
                sha,
            ) = line.strip().split(",")
            device_name, partition_name = parse_devname_partname(device_name_partname)
            resize = 0
            # Use the variables as needed
            if partition_name == "BCT":
                partition_name = partition_name.lower()

            if partition_name.startswith("APP") and partition_name + var_extension in flash_config:
                name = flash_config[partition_name + var_extension]
                resize = 1
                if "expand-" in extra:
                    resize = 2
            if partition_name.startswith("UDA") and partition_name + var_extension in flash_config:
                name = flash_config[partition_name + var_extension]
            if name.strip() == "":
                continue
            realpath = f"{dirname_path}/{name.strip()}"
            output_path = f"./output/{name.strip()}"

            # Only copy if not already copied
            if output_path not in self._copied_files:
                try:
                    if not (os.path.exists(output_path) and os.path.samefile(realpath, output_path)):
                        shutil.move(realpath, output_path)
                except shutil.SameFileError:
                    pass
                self._copied_files.add(output_path)

            # Cache md5sum, skip for partition_name starts with APP
            if output_path in self._md5_cache:
                md5 = self._md5_cache[output_path]
            else:
                md5 = calculate_md5(output_path)
                self._md5_cache[output_path] = md5

            ## LinuxPartitionName, PartitionName, FileName, Start, Size, BlockCount, Resize, sku_dependent, BchPartitionName, ImageHeaderType, MD5, ReadWrite
            elements = [
                self.device_name_to_dev[device_name.strip()],
                partition_name,
                name,
                start,
                size,
                str(
                    int(
                        int(size.strip())
                        // self.device_name_to_blocksize[device_name.strip()]
                    )
                ),
                str(resize),
                "0",
                partition_name,
                "4",
                md5,
                "0",
            ]
            output_file.write(" ".join(e.strip() for e in elements))
            print(" ".join(e.strip() for e in elements))
            output_file.write("\n")

class Die1(Base):
    def __init__(self):
        super().__init__()
        self.device_name_to_dev = {
            "3:0": "/dev/block/1810c5b0000.spi",
        }


profile={
    "base": Base(),
    "die1": Die1()
}

def convert_to_hex(input_string):
    # Remove colons and convert to uppercase
    hex_string = input_string.replace(':', '')

    # Convert to integer and then to hex
    decimal_value = int(hex_string, 16)

    return f"0x{decimal_value:X}"


def get_block_size(file_path):
    # Parse the XML file
    tree = ET.parse(file_path)

    # Get the root element
    root = tree.getroot()

    # Find the device element with type="external"
    device_external = root.find(".//device[@type='external']")

    # Get the sector_size attribute value
    if device_external is not None:
        sector_size = device_external.get('sector_size')
        return int(sector_size)
    else:
        return 512

def parse_text_file_to_dict(file_path):
    # Initialize an empty dictionary
    result_dict = {}

    # Open the file in read mode
    with open(file_path, 'r') as file:
        # Read lines from the file
        lines = file.readlines()

        # Iterate through each line
        for line in lines:
            # Strip any leading/trailing whitespace and split the line by '='
            key, value = line.strip().split('=')
            # Add the key-value pair to the dictionary
            result_dict[key.strip()] = value.strip()

    return result_dict

def parse_devname_partname(input_str):
    # Split the string
    parts = input_str.split(":")

    # Extract the components
    dev_name = parts[0] + ":" + parts[1]
    part_name = parts[2]
    return dev_name, part_name

def calculate_md5(file_path):
    """
    Calculate the MD5 hash of a file.

    Args:
        file_path (str): Path to the file.

    Returns:
        str: MD5 hash of the file.
    """
    # Initialize the hash object
    md5_hash = hashlib.md5()
    # Get the file size in bytes
    file_size = os.path.getsize(file_path)

    # Convert the maximum size from MB to bytes
    max_size_bytes = 10 * 1024 * 1024

    # Terminate early if file is bigger than 10MB
    end_early = file_size > max_size_bytes

    # Open the file in binary mode and read it in chunks
    with open(file_path, 'rb') as file:
        # Read the file in chunks and update the hash
        while True:
            data = file.read(8192)  # Read 8KB at a time
            if not data:
                break
            md5_hash.update(data)
            if end_early:
                break

    # Return the hexadecimal digest of the hash
    return md5_hash.hexdigest()

def replace_file_in_flash_packages(partition_name, flash_package_location):
    if os.path.exists(flash_package_location):
        index_file_list = [ "flash.idx", "flash-upi.idx" ]
        destination = ["internal", "external"]
        file_name = None
        for dest in destination:
            for flash_idx in index_file_list:
                index_file_path = f"./tools/kernel_flash/images/{dest}/{flash_idx}"
                cfg_file_path = f"./tools/kernel_flash/images/{dest}/flash.cfg"
                config = {}
                if os.path.exists(cfg_file_path):
                    config = parse_text_file_to_dict(cfg_file_path)
                var_extention = "_ext" if "external" in dest else ""
                if os.path.exists(index_file_path):
                    with open(
                        index_file_path, "r", encoding="utf-8"
                    ) as flash_index:
                        for line in flash_index:
                            (
                                _,
                                device_name_partname,
                                _,
                                _,
                                name,
                                _,
                                _,
                                _,
                            ) = line.strip().split(",")
                            _, part_name = parse_devname_partname(device_name_partname)
                            if (
                                partition_name.startswith("APP")
                                and partition_name + var_extention in config
                            ):
                                name = config[partition_name + var_extention]
                            if partition_name == part_name:
                                file_name = f"./tools/kernel_flash/images/{dest}/{name.strip()}"
                                break
        if not file_name:
            print(f"Partition {partition_name} not found")
            exit(1)
        for line in fileinput.input(os.path.join(flash_package_location, "./FileToFlash.txt"), inplace=True):
            # 0                   1              2         3      4     5           6       7              8                 9                10   11
            # LinuxPartitionName, PartitionName, FileName, Start, Size, BlockCount, Resize, sku_dependent, BchPartitionName, ImageHeaderType, MD5, ReadWrite
            fields = line.strip().split()
            if fields[1] == partition_name and file_name:
                fields[2] = os.path.basename(file_name)
                fields[10] = calculate_md5(file_name)
                try:
                    shutil.copyfile(file_name, os.path.join(flash_package_location, f"{fields[2]}"))
                except shutil.SameFileError:
                    pass

            print(" ".join(e.strip() for e in fields))

def move_and_remove(src_dir, dst_dir):
    # Ensure destination directory exists
    if not os.path.exists(dst_dir):
        os.makedirs(dst_dir)

    # Move all contents from source to destination
    for item in os.listdir(src_dir):
        src_path = os.path.join(src_dir, item)
        dst_path = os.path.join(dst_dir, item)

        if os.path.isdir(src_path):
            # If it's a directory, move it recursively
            if os.path.exists(dst_path):
                # If the destination directory already exists, merge contents
                move_and_remove(src_path, dst_path)
            else:
                shutil.move(src_path, dst_path)
        else:
            # If it's a file, move it, overwriting if it already exists
            if os.path.exists(dst_path):
                os.remove(dst_path)
            shutil.move(src_path, dst_path)

    # If all moves were successful, remove the source directory
    shutil.rmtree(src_dir)
    print(f"Successfully moved all contents from {src_dir} to {dst_dir} and removed {src_dir}")

def merge_rcm_images(source_dirs, parent_destination_dir):
    for index, parent_src_directory in enumerate(source_dirs):
        for rcm_dir in ["rcm-boot", "rcm-flash"]:
            flash_ws_dir_path = glob.glob(os.path.join(parent_src_directory, "flash_workspace")) + \
                glob.glob(os.path.join(parent_src_directory, "[0-9]*"))

            flash_ws_dir = os.path.basename(flash_ws_dir_path[0])

            src_directory = os.path.join(parent_src_directory, flash_ws_dir, rcm_dir)
            dst_directory = os.path.join(parent_destination_dir, flash_ws_dir, rcm_dir, "die" + str(index))

            if (not os.path.exists(src_directory)):
                print(f"{src_directory} doesn't exist. Skipping merge.")
                continue

            try:
                move_and_remove(src_directory, dst_directory)
            except Exception as e:
                print(f"An error occurred: {str(e)}")

            print(f"All files and subdirectories have been copied from {src_directory} to {dst_directory}")

def merge_flash_images(source_dirs, destination_dir):
    # Copy TargetInfo.txt and ToolsVersion.txt

    flash_ws_dir_path = glob.glob(os.path.join(source_dirs[0], "flash_workspace")) + \
        glob.glob(os.path.join(source_dirs[0], "[0-9]*"))

    flash_ws_dir = os.path.basename(flash_ws_dir_path[0])

    dst_flash_images_dir = os.path.join(destination_dir, flash_ws_dir, "flash-images")

    if os.path.exists(dst_flash_images_dir):
        shutil.rmtree(dst_flash_images_dir)

    for file in ["TargetInfo.txt", "ToolsVersion.txt"]:
        src_file_path = os.path.join(source_dirs[0], flash_ws_dir, file)
        dst_file_path = os.path.join(destination_dir, flash_ws_dir, file)
        if os.path.exists(dst_file_path):
            os.remove(dst_file_path)
        shutil.move(src_file_path, dst_file_path)

    # Copy flash images directory
    for index, src_directory in enumerate(source_dirs):
        src_flash_images_dir = os.path.join(src_directory, flash_ws_dir, "flash-images")
        if (not os.path.exists(src_flash_images_dir)):
            print(f"{src_flash_images_dir} doesn't exist. Skipping merge.")
            continue

        os.makedirs(dst_flash_images_dir, exist_ok=True)

        for file_name in os.listdir(src_flash_images_dir):
            src_file_path = os.path.join(src_flash_images_dir, file_name)
            dst_file_name = f"die{index}_{file_name}"
            dst_file_path = os.path.join(dst_flash_images_dir, dst_file_name)
            shutil.move(src_file_path, dst_file_path)

    # Merge FileToFlash.txt
    if (os.path.exists(dst_flash_images_dir)):
        dst_file_to_flash_path = os.path.join(dst_flash_images_dir, "FileToFlash.txt")
        with open(dst_file_to_flash_path, "w") as d_ff:
            for index in range(len(source_dirs)):
                src_file_to_flash = f"die{index}_FileToFlash.txt"
                src_file_to_flash_path = os.path.join(dst_flash_images_dir, src_file_to_flash)
                with open(src_file_to_flash_path, "r") as s_ff:
                    for line in s_ff.readlines():
                        fields = line.strip().split()
                        fields[2] = f"die{index}_{fields[2]}"
                        line = ' '.join(fields)
                        d_ff.write(f"{line}\n")

def get_disk_name(ext_dev):
    import re
    disk = ""
    if ext_dev.startswith("sd"):
        disk = ext_dev.rstrip('0123456789')
    else:
        disk = re.sub(r'p\d+$', '', ext_dev)
    return disk

# Mapping from l4t to unified name
def translate_image_name(image_name, security_mode, skip_namemapping=False):
    file_extension = ""
    l4t_to_unified_namemapping = {
        "br_bct_BR.bct": "A_bct_BR.bct",
        "blob.bin": "rcm_blob.bin",
        "mb1_bct_MB1": "bct_MB1.bin",
    }
    security_mode_extension_mapping = {
        "NS": "_zerosign",
        "PKC": "_signed",
        "PKCSBK": "_encrypt_signed"
    }

    if not skip_namemapping:
        for file_prefix in l4t_to_unified_namemapping:
            if image_name.startswith(file_prefix):
                image_name = l4t_to_unified_namemapping[file_prefix]
                if file_prefix == "blob.bin":
                    return image_name
                break
        # if the image doesn't need special mapping to the unified format,
        # simply delete the tools-added-tag in the image name
        else:
            tags = ["_sigheader", "_aligned", ".encrypt", ".signed", "_encrypt"]
            for tag in tags:
                image_name = image_name.replace(tag, "")

    image_name, file_extension = os.path.splitext(image_name)
    updatedName = image_name + security_mode_extension_mapping[security_mode] + file_extension

    return updatedName

def main():
    parser = argparse.ArgumentParser(
        description="This program generates a L4T flashing package"
    )

    parser.add_argument(
        "--dest", nargs=1, required=True, help="the flashing output folder copy to"
    )

    parser.add_argument(
        "--external-device", nargs=2, help="the external device dev name"
    )

    parser.add_argument(
        "--info",  action="store_true", help="Print info file"
    )

    parser.add_argument(
        "--rcm-boot",  action="store_true", help="Convert rcm-boot from l4t to automotive"
    )

    parser.add_argument(
        "-k", "--individual-partition", nargs=1, help="flashing individual partition", metavar=('partition_name')
    )

    parser.add_argument(
        "-p", "--profile", nargs=1, help="chip information profile to use", default=["base"])

    parser.add_argument(
        "--merge", nargs=2 ,help="Merge die specific directories into one directory for flashing")

    parser.add_argument(
        "--internal",  action="store_true", help="Internal board mode")

    parser.add_argument(
        "--security-mode", choices=['NS', 'PKC', 'PKCSBK'],
        help=(
            "Specify the security mode for the target board:\n"
            "  - 'NS': Use for boards without fused ODM keys (default)\n"
            "  - 'PKC': Use for ODM fused boards with PKC\n"
            "  - 'PKCSBK': Use for ODM fused board with both PKC and Secure Boot Key (SBK)"
        ),
        default="NS"
    )

    args = parser.parse_args()

    # Merge images into single directory
    if (args.merge):
        source_directories = [os.path.abspath(os.path.expanduser(path)) for path in args.merge]
        destination_directory = os.path.abspath(os.path.expanduser(args.dest[0]))

        # Merge tools directories
        for dir in ["tools"]:
            src_dir = os.path.join(source_directories[0], dir)
            dst_dir = os.path.join(destination_directory, dir)
            move_and_remove(src_dir, dst_dir)

        # Merge rcm directories
        merge_rcm_images(source_directories, destination_directory)

        # Merge flash-images directory
        merge_flash_images(source_directories, destination_directory)

        # Remove source directory after merge is done
        for dir in source_directories:
            shutil.rmtree(dir)

        return 0

    target = profile[args.profile[0]]

    if args.external_device:
        target.update_external_device_info(args.external_device)

    os.chdir(script_directory)

    if args.individual_partition:
        print("Changing individual partition in the flash packages")
        replace_file_in_flash_packages(args.individual_partition[0], args.dest[0])
        exit()

    if args.info:
        # Writing to ToolsVersion.txt
        with open(
            os.path.join(args.dest[0], "tools", "ToolsVersion.txt"),
            "w",
            encoding="utf-8",
        ) as file:
            file.write('#Tool version\n')
            file.write('ToolVersion=1.0\n')
        os.link(
            os.path.join(args.dest[0], "tools", "ToolsVersion.txt"),
            os.path.join(args.dest[0], "flash_workspace", "ToolsVersion.txt"),
        )
        # Preparing content for TargetInfo.txt
        target_board = f'TargetBoard={target.json_file_name}\n'
        eng_sample = 'EngSample=' + ('True' if args.internal else 'False') + '\n'
        secure_target = 'SecureTarget=' + ('True' if (args.security_mode != "NS") else 'False') + '\n'
        encrypt_images = 'EncryptedImages=' + ('True' if (args.security_mode == "PKCSBK") else 'False') + '\n'

        # Writing to TargetInfo.txt
        with open(
            os.path.join(args.dest[0], "flash_workspace", "TargetInfo.txt"),
            "w",
            encoding="utf-8",
        ) as file:
            file.write(target_board)
            file.write(eng_sample)
            file.write(secure_target)
            file.write(encrypt_images)

        # Update jetson-t264.json

        json_path = target.json_path
        with open(
            os.path.join(args.dest[0], json_path),
            "r",
            encoding="utf-8",
        ) as f:
            data = json.load(f)

        ramcode="0"
        ramcode_file = os.path.join("bootloader", "ramcode.txt")
        if os.path.exists(ramcode_file):
            with open(ramcode_file, "r") as f:
                 ramcode = f.read().rstrip()
            os.remove(ramcode_file)

        skuname = ""
        if os.getenv('CHIP_SKU'):
            chipid = convert_to_hex(os.getenv('CHIP_SKU'))
            bpmp_info = os.path.join(args.dest[0],target.bpmp_info)
            # load bpmp_info
            with open(bpmp_info, "r") as f:
                bpmp_info = json.load(f)
                # Loop through the keys of bpmp_info and break if the value contains chipid
                for key, value in bpmp_info.items():
                    if chipid in value:
                        skuname = key
                        break

        # Update the s_RamCode field
        data[target.json_file_name]['s_RamCode'] = ramcode
        data[target.json_file_name]['s_Skuname'] = skuname

        # Save the updated JSON data
        with open(os.path.join(args.dest[0], json_path), 'w') as f:
            json.dump(data, f)

        exit()

    applet_dir = ("bootloader/applet")
    if os.path.exists(applet_dir) == True:
        files = os.listdir(applet_dir)
        if len(files) > 0:
            app_bin_dir = os.path.join(args.dest[0], "applet")
            if not os.path.exists(app_bin_dir):
                os.makedirs(app_bin_dir)

            for fname in files:
                if fname == "br_bct_BR.bct":
                    unified_name = translate_image_name("APbct_BR.bct", args.security_mode, True)
                    os.link(os.path.join(applet_dir, fname), os.path.join(args.dest[0], unified_name))
                else:
                    unified_name = "applet_blob.bin"
                    os.link(os.path.join(applet_dir, fname), os.path.join(args.dest[0], "applet", unified_name))

    if args.rcm_boot:
        print("Translating from L4T rcm-boot to unified format")
        files = os.listdir("bootloader/rcmboot_blob")

        # iterating over all the files in
        # the source directory
        for fname in files:
            if fname == "rcmbootcmd.txt":
                continue
            # copying the files to the
            # destination directory
            unified_name = translate_image_name(fname, args.security_mode)
            os.link(os.path.join("bootloader/rcmboot_blob", fname), os.path.join(args.dest[0], unified_name))

        exit()

    print("Translating from L4T format to unified format")

    if os.path.exists("output") and os.path.isdir("output"):
        shutil.rmtree("output")
    os.mkdir("output")

    index_file_list = [ "flash.idx", "flash-upi.idx" ]
    destination = ["internal", "external"]
    append=False
    file_option="w"
    for dest in destination:
        for flash_idx in index_file_list:
            index_file_path = f"./tools/kernel_flash/images/{dest}/{flash_idx}"
            cfg_file_path = f"./tools/kernel_flash/images/{dest}/flash.cfg"
            print(f"Converting {index_file_path}")
            config = {}
            if os.path.exists(cfg_file_path):
                config = parse_text_file_to_dict(cfg_file_path)
            var_extention = "_ext" if "external" in dest else ""
            if os.path.exists(index_file_path):
                with open(
                    index_file_path, "r", encoding="utf-8"
                ) as flash_index, open("./output/FileToFlash.txt", file_option, encoding="utf-8") as output:
                    target.translate_l4t_to_auto(flash_index, output, config, var_extention, append)
                    append=True
                    file_option="a"

    files = os.listdir("output")

    # iterating over all the files in
    # the source directory
    for fname in files:
        # copying the files to the
        # destination directory
        os.link(os.path.join("output", fname), os.path.join(args.dest[0], fname))


if __name__ == "__main__":
    main()
