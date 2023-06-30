#!/usr/bin/python
#
# Copyright (c) 2017-2022, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA Corporation is strictly prohibited.
#

from __future__ import print_function

import sys
import os
import string
import struct
import argparse
if sys.version_info.major == 2:
    import ConfigParser
else:
    import configparser

params = {}
ROLLBACK_BIN_SIZE = 8

def parse_args():
    global params
    parser = argparse.ArgumentParser(description='Generate a blob to include rollback \
                                     info defined in rollback config file')
    parser.add_argument('target', help='build target for the product')
    parser.add_argument('config', default='rollback.cfg',
                        help='rollback config file for the target')
    parser.add_argument('--output', dest='output', default='rollback.bin',
                        help='output binary file including rollback info')
    params = parser.parse_args()

def main():
    if sys.version_info.major == 2:
        rollback = ConfigParser.ConfigParser()
    else:
        rollback = configparser.ConfigParser()
    rollback.read(params.config)

    rlbk_list = ['mb1_ratchet_level', 'mts_ratchet_level', 'rollback_fuse_level']
    packing_list = {'mb1_ratchet_level': '=B', 'mts_ratchet_level': '=B',
                    'rollback_fuse_level': '=B'}

    blob = open(params.output, "wb")
    print ('rollback info is parsed and saved in', params.output)

    for e in rlbk_list:
        try:
            level = rollback.getint(params.target, e)
        except:
            print ('WARNING: failed to find rollback config for \"', params.target, '\"')
            break
        bin_value = struct.pack(packing_list[e], level)
        blob.write(bin_value)

    # pad zeros
    print ('pad ZEROs to the end of', params.output)
    pad_value = struct.pack("=B", 0)
    for i in range(blob.tell(), ROLLBACK_BIN_SIZE):
        blob.write(pad_value)

    blob.close()

if __name__ == '__main__':
    parse_args()
    main()
    sys.exit(0)

