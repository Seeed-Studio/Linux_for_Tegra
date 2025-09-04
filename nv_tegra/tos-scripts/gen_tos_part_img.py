#!/usr/bin/python3
# Copyright (c) 2013-2021, NVIDIA CORPORATION. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files
# (the "Software"), to deal in the Software without restriction,
# including without limitation the rights to use, copy, modify, merge,
# publish, distribute, sublicense, and/or sell copies of the Software,
# and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#
# Usage:
# tools/gen_tos_part_img.py [options] --monitor <path_to_monitor.bin>
#                                     --os <path_to_lk.bin> $OUT/tos.img
# Tip: "--os" is required only when tos.img contains Trusted OS
#
import shutil
import sys
import os
import stat
import struct
import binascii
import string
import optparse
import subprocess
import tempfile

# T124 uses a static monitor library
monitor_lib = "libmonitor"

parser = optparse.OptionParser(description="Constructing Trusted OS partition image",
                               usage="usage: %prog [options] <partition image output file>")
parser.add_option('-e', action="store_true",
                  help='partition data is encrypted (default: data is not encrypted)')
parser.add_option('--cipher', action="store_true",
                  help='Encrypt partition data')
parser.add_option('--key', metavar='HEX_KEY',
                  help='32 hex nibbles for cipher key')
parser.add_option('--keyfile',
                  help='file with 32 hex nibbles for cipher key')
parser.add_option('--iv', metavar='HEX_IV',
                  help='32 hex nibbles for IV')
parser.add_option('--eks_master', metavar='HEX_EKS_MASTER',
                  help='32 hex nibbles for EKS master value')
parser.add_option('--monitor', help="monitor file")
parser.add_option('--os', help="os file")
parser.add_option('--dtb', help="TrustedOS dtb file")
parser.add_option('--tostype', help='Trusted OS type: tlk, trusty, tos2, optee')

(args, leftover)=parser.parse_args()
if len(leftover) < 1:
    parser.print_help()
    sys.exit(1)

monitor_name = args.monitor
tos_name = args.os
tos_dtb_name = args.dtb
output_name = os.path.abspath(leftover[0])
image_type = 0

TOS_TYPE_NONE = 0
TOS_TYPE_TLK = 1
TOS_TYPE_TRUSTY = 2
TOS_TYPE_TOS2 = 3
TOS_TYPE_OPTEE = 4
tos_type = TOS_TYPE_NONE
if tos_name:
    tos_type = TOS_TYPE_TRUSTY
if args.tostype:
    if not tos_name:
        print("--tostype specified without --os")
        parser.print_help()
        sys.exit(1)
    tos_map = {
        'tlk': TOS_TYPE_TLK,
        'trusty': TOS_TYPE_TRUSTY,
        'tos2': TOS_TYPE_TOS2,
        'optee': TOS_TYPE_OPTEE,
    }
    tos_type = tos_map.get(args.tostype, None)
    if tos_type is None:
        print("Invalid --tostype " + args.tostype)
        parser.print_help()
        sys.exit(1)

# If we want/need to pad tos.img to block size, please use 512 here
pad_size = 16
tos_size_nopad = 0

if os.path.exists(output_name):
    os.remove(output_name)

# Calculate individual binary sizes for the monitor and trusted OS
if monitor_name:
    monitor_size = os.path.getsize(monitor_name)
else:
    monitor_size = 0

if args.os:
    os_size = os.path.getsize(tos_name)
else:
    os_size = 0

if args.dtb:
    tos_dtb_size = os.path.getsize(tos_dtb_name)
else:
    tos_dtb_size = 0

# calculate the total image size
if monitor_name:
    if monitor_lib in monitor_name:
        # libmonitor is not a separate binary
        monitor_size = 0

tos_size = monitor_size + os_size + tos_dtb_size

# Trusted OS has to be 16-byte aligned for faster relocation, so append the
# monitor binary with zeros
tos_align = 0
if monitor_size % 16:
    tos_align = (16 - (monitor_size % 16))
tos_offset = monitor_size + tos_align
monitor_size += tos_align
tos_size += tos_align

tos_dtb_offset = 0
tos_dtb_align = 0
if args.dtb:
    if os_size % 16:
        tos_dtb_align = (16 - (os_size % 16))
    tos_dtb_offset = monitor_size + os_size + tos_dtb_align
    os_size += tos_dtb_align
    tos_size += tos_dtb_align

# By default the image IV is zero vector.
# If --iv option is used the IV value is saved in TOS TOC for the bootloader
#
iv_hex="00000000000000000000000000000000"
iv = bytearray.fromhex(iv_hex).decode('utf-8')

if args.e or args.cipher:
    image_type = 0x45

    if args.iv:
        iv=args.iv.decode("hex")
        image_type |= 0x20

    if args.cipher:
        print("Generating Trusted OS Partition Image File (encrypting input)")
        tos_size_nopad = tos_size
        if tos_size % pad_size:
            tos_size += (pad_size - tos_size % pad_size)
    else:
        print("Generating Trusted OS Partition Image File (pre-encrypted input)")
else:
    print("Generating Trusted OS Partition Image File")
    image_type = 0x50

#
# Table of Contents (TOC) for the Bootloader
#
# typedef struct tos_image_toc {
#     char name[7];
#     char total_img_size[9];
#     // word aligned at this point
#     uint32_t monitor_offset;
#     uint32_t monitor_size;
#     uint32_t tos_offset;
#     uint32_t tos_size;
#     uint32_t image_info;
#     uint8_t iv[16];
#     uint32_t tos_type;
#     uint32_t tos_dtb_offset;
#     uint32_t tos_dtb_size;
# } tos_image_toc_t;
#
# Could also use the carveout size but maybe not good to fix that here.
# total_img_size contains NUL terminated string.
if len(str(tos_size)) >= 9:
    sys.exit("Combined image size does not fit field")

img_size = tos_size

# calculate size of padding bytes to make final image 16-byte aligned
img_align = 0
if (img_size % 16):
    img_align = (16 - (img_size % 16))

values = (
    str("NVTOSP").encode('utf-8'),              # partition name
    str(img_size + img_align).encode('utf-8'),  # total image size
    0,                                          # monitor_offset
    monitor_size,                               # monitor_size
    tos_offset,                                 # tos_offset
    os_size,                                    # os_size
    image_type,                                 # image type info (ciphered, iv valid, etc)
    iv.encode('utf-8'),                         # AES init vector if specific
    tos_type,                                   # trusted OS type
    tos_dtb_offset,                             # trusted OS dtb offset
    tos_dtb_size                                # trusted OS dtb size
)
s = struct.Struct('< 7s 9s I I I I I 16s I I I')
packed_data = s.pack(*values)
header = '\0' * (512-s.size)                    # align TOC to 512 bytes

# Open new image file

dest = open(output_name, 'wb')
os.chmod(output_name, (stat.S_IWUSR | stat.S_IRUSR) | stat.S_IRGRP | stat.S_IROTH)

# Write out TOS partition header

dest.write(packed_data)                         # write TOC
dest.write(header.encode('utf-8'))              # write padding bytes

dest.flush()

if args.cipher:

    if monitor_lib in monitor_name:
        os.remove(output_name)
        sys.exit("old systems do not support ciphered images")

    # Encrypting with this script is used only for R&D
    # for obvious security reasons.
    #
    # By default the TOS image cipher key is:
    #
    key="58e2fccefa7e3061367f1d57a4e7455a"

    # Read the cipher key from a file
    # or derive the cipher key from EKS master value
    # for the R&D case.
    #
    # Otherwise the TOS image cipher key default value above
    # matches the EKS image master value zero.
    #
    if args.keyfile:
        key = args.keyfile.read()
        args.keyfile.close()
        key = key.rstrip('\r\n')
    else:
        if args.key:
            key=args.key
        else:
            if args.eks_master:
                ziv="00000000000000000000000000000000"

                # XXX Could use pipes
                s = tempfile.NamedTemporaryFile(delete=True)
                s.write("00000000000000000000000000000001".decode("hex"))
                s.flush()
                s.seek(0)
                k = tempfile.NamedTemporaryFile(delete=True)

                subprocess.call(["openssl", "enc", "-e", "-nopad", "-aes-128-cbc", "-K", args.eks_master, "-iv", ziv],
                                stdin=s, stdout=k)

                s.close()
                k.flush()
                k.seek(0)
                key=k.read().encode("hex")
                k.close()

    image_file = tempfile.NamedTemporaryFile(delete=True)
    shutil.copyfileobj(open(monitor_name, 'rb'), image_file) # write monitor.bin to temp image_file
    if args.os:
        shutil.copyfileobj(open(tos_name, 'rb'), image_file) # write lk.bin to temp image_file
    if args.dtb:
        shutil.copyfileobj(open(tos_dtb_name, 'rb'), image_file) # write trusted OS dtb to temp image_file

    # Pad the image file before encrypting it
    if tos_size - tos_size_nopad > 0:
        image_file.write('\0' * (tos_size - tos_size_nopad))

    image_file.flush()
    image_file.seek(0)

    subprocess.call(["openssl", "enc", "-e", "-nopad", "-aes-128-cbc", "-K", key, "-iv", iv.encode("hex")],
                    stdin=image_file, stdout=dest)
    image_file.close()

else:
    if monitor_name:
        if monitor_lib in monitor_name:
          shutil.copyfileobj(open(tos_name, 'rb'), dest)     # write lk.bin
        else:
          shutil.copyfileobj(open(monitor_name, 'rb'), dest) # write monitor.bin
          dest.write(('\0' * tos_align).encode('utf-8'))
    if args.os:
        shutil.copyfileobj(open(tos_name, 'rb'), dest) # write lk.bin
    if args.dtb:
        dest.write(('\0' * tos_dtb_align).encode('utf-8'))
        shutil.copyfileobj(open(tos_dtb_name, 'rb'), dest) # write trusted OS dtb

dest.flush()
# make TOS image as 16-byte aligned for encryption
dest.write(('\0' * img_align).encode('utf-8'))

dest.flush()
dest.close()

print("Generate TOS Image File for boot-wrapper.")

img_out = str(os.path.dirname(output_name)+"/"+"img.bin")

dest = open(img_out, 'wb')
os.chmod(img_out, (stat.S_IWUSR | stat.S_IRUSR) | stat.S_IRGRP | stat.S_IROTH)

if monitor_name:
    if monitor_lib in monitor_name:
      shutil.copyfileobj(open(tos_name, 'rb'), dest)     # write lk.bin
    else:
      shutil.copyfileobj(open(monitor_name, 'rb'), dest) # write monitor.bin
if args.os:
    shutil.copyfileobj(open(tos_name, 'rb'), dest) # write lk.bin
if args.dtb:
    shutil.copyfileobj(open(tos_dtb_name, 'rb'), dest) # write trusted OS dtb

dest.flush()
dest.close()
