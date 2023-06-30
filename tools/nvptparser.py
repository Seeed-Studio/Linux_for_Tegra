#!/usr/bin/env python3
#
# Copyright (c) 2019-2022, NVIDIA Corporation.  All Rights Reserved.
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA Corporation is strictly prohibited.
#

import sys
import os.path
import xml.etree.ElementTree as ET

# The default partition type is Linux filesystem
DEFAULT_PART_TYPE = '8300'

def do_parse(config_file, storage):
    tree = ET.parse(config_file)
    root = tree.getroot()

    partitions = []
    mbr = ()
    for p in tree.findall("./device[@type='%s']/partition" % storage):
        name = p.get('name')
        size = p.find('size')
        if name == 'UDA' and size is None:
            size = "134217728"
        else:
            size = size.text.strip()
        found = p.find('filename')
        if found is not None:
            filename = found.text.strip()
        else:
            filename = ''

        found = p.find('partition_type_guid')
        if found is not None:
            part_type = found.text.strip()
        else:
            part_type = DEFAULT_PART_TYPE

        # Skip partition tables
        if name in ['GP1', 'GPT', 'primary_gpt', 'secondary_gpt']:
            continue

        if name == 'master_boot_record':
            mbr = (name, size, filename, part_type)
            continue

        if name == 'APP':
            partitions.insert(0, (name, size, filename, part_type))
        else:
            partitions.append((name, size, filename, part_type))

    if mbr:
        partitions.append(mbr)

    result = []
    for i, (name, size, filename, part_type) in enumerate(partitions):
        result.append((i + 1, name, size, filename, part_type))

    # Keep APP partition in the bottom
    result.append(result.pop(0))
    return result


def usage():
    print('Usage: nvptparser.py <config.xml> <storage>')
    sys.exit(0)

if __name__ == '__main__':
    if len(sys.argv) != 3 or not os.path.isfile(sys.argv[1]):
        usage()

    result = do_parse(sys.argv[1], sys.argv[2])
    for (i, n, s, f, t) in result:
        print('part_num=%d;part_name=%s;part_size=%s;part_file=%s;part_type=%s' % (i, n, s, f, t))
