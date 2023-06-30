#!/usr/bin/python
#
# Copyright (c) 2017-2022, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.
#

"""

Appendix:
-------
    1) if blob-type = update then entry tuple format is:
           <binary_name partition_name version binary_operating_mode spec_info>

       for each partition, the script supports multiple binaries for different device:
           if the binary is not FAB specific, spec_info should be "common"
           if the binary is FAB specific, spec_info should be valid spec info for target device

           if the binary is not specific to operating mode, op_mode should be 0
           if the binary is specific to operating mode, op_mode should be valid one as per its mode

       example: BUP_generator.py -t update -r rollback.bin -e "cboot.bin EBT 2 0 <spec_info>"

    2) if blob-type = bmp then entry tuple format is:
           <filename bmp-type bmp-resolution>

       currently supported bmp-types: nvidia, lowbattery, charging, charged, fullycharged
       currently supported bmp-resolutions: 480, 720, 810, 1080, 4k, 1200_p. The 'p' of 1200_p
       means the panel is portrait.

       example: BUP_generator.py -t bmp -e "nvidia.bmp nvidia 720"

    3) if blob-type = update then blob header information and entry list information can be
           shown. the "-c|--contents" option indicates blob inspection mode. all or some of the binaries
           can also be extracted to <OUT> with the "-x" option. binaries are saved with the naming scheme
           "<part_name>[_<op_str>][_<tnspec>].raw.bin" where "op_str" is not present for "op_mode = 0",
           "dev" for "op_mode = 1" and "prod" for "op_mode = 2". "tnspec" is not present if "spec_info"
           is "common" or null.

        examples: BUP_generator.py -c ota.blob
                  (display blob information for "ota.blob")

                  OUT=$(pwd) ./BUP_generator.py -c -x "mb1; mb2" ota.blob
                  (display blob information for "ota.blob" and extract partition bins "mb1" and "mb2" to
                   "OUT/mb1[_<op_str>][_<tnspec>].raw.bin" and "OUT/mb2[_<op_str>][_<tnspec>].raw.bin")

                  OUT=$(pwd) ./BUP_generator.py -x all ota.blob
                  (extract all partition bins for "ota.blob" to "OUT/*.raw.bin" without displaying blob info)
"""

from __future__ import print_function

import copy
import sys

if sys.hexversion < 0x02070000:
  print >> sys.stderr, "Python 2.7 or newer is required."
  sys.exit(1)

import os
import sys
import struct
import argparse
import textwrap

top_var = "TOP"

# BUP magic:
# Bump it with the BUP version.
# * V2: The starting version to support bct_ver format
# * V3: 128 length spec info.
bup_magic = "NVIDIA__BLOB__V3"
# BUP vesioin:
# Bump it when the BUP blob structure updated.
# * 3.1: 128 length spec info.
bup_bcd_ver_maj = 0x3
bup_bcd_ver_min = 0x1
# bcdBUP release version (binary coded decimal number):
# Bump it in yymm when BUP is changed in the new release.
# Use 2106 for the initial release with yymm in version format.
bup_bcd_ver_yy = 0x22
bup_bcd_ver_mm = 0x6
# BUP release revision:
# It supports upto 4, and should be reset to 0 once the bcdBUP version is updated.
# Bump it when BUP released within same month.
bup_rev = 0x0

def parse_args():
    parser = argparse.ArgumentParser(description=textwrap.dedent('''\
                                                 Package a blob and copies into <OUT>
                                                 Alternatively, inspect or extract a packaged blob's contents
                                                 '''), epilog=__doc__, formatter_class=argparse.RawTextHelpFormatter
                                    )

    parser.add_argument('-t', dest='blob_type', default='update', choices=['update', 'bmp'],
                        help=textwrap.dedent('''\
                             payload package type, default: update
                             for OTA : update
                             for BMP : bmp
                             ''')
                       )
    parser.add_argument('-r', dest='accessory',
                        help=textwrap.dedent('''\
                             header accessory binary
                             for OTA : used to embed rollback config info currently
                             for BMP : not used, hence invalid now
                             ''')
                       )
    parser.add_argument('-e', dest='entry_list',
                        help=textwrap.dedent('''\
                             binary entry list, format should be like:
                             "<entry tuple 1>; <entry tuple 2>; ... ; <entry tuple N>"
                             ''')
                       )
    parser.add_argument('-c', '--contents', dest='inspect_mode', action='store_true', default=False,
                        help=textwrap.dedent('''\
                             enables blob inspect mode which prints out blob header information
                             and entry table
                             blob path must be the last argument to the entire command
                             ''')
                       )
    parser.add_argument('-m', nargs='?', dest='inspect_max_entries', type=int, default=256,
                        help=textwrap.dedent('''\
                             maximum number of partition entries to display (default 256)
                             only valid if '-c|--contents' is used
                             ''')
                       )
    parser.add_argument('-k', '--check', dest='check_entries', action='store_true', default=False,
                        help=textwrap.dedent('''\
                             check if missed entries in BUP or not
                             only valid if '-c|--contents' is used
                             ''')
                       )
    parser.add_argument('-x', nargs='?', dest='inspect_extract_bin_list',
                        help=textwrap.dedent('''\
                             extracts binaries in the BUP into <OUT>, format should be like:
                             "<part name 1>; <part name 2>; ... ; <part name N>"
                             passing in "all" will extract all available binaries
                             ''')
                       )
    parser.add_argument('inspect_blob', type=argparse.FileType(mode='rb'), nargs='?', default=None,
                        help=textwrap.dedent('''\
                             file path to the blob file to inspect
                             only valid if '-c|--contents' is used
                             ''')
                       )

    params = parser.parse_args()
    return params

def generate_BUP(arg):
    global top_var

    # Check "TOP" variable is set and is valid
    if not 'TOP' in os.environ or not os.path.isdir(os.environ["TOP"]):
        if not 'ANDROID_BUILD_TOP' in os.environ or \
                not os.path.isdir(os.environ["ANDROID_BUILD_TOP"]):
            sys.stderr.write("Environment variable TOP not set or invalid.\n")
            return
        else:
            top_var = "ANDROID_BUILD_TOP"

    # Check "OUT" variable is set and is valid
    if not 'OUT' in os.environ or not os.path.isdir(os.environ["OUT"]):
        sys.stderr.write("Environment variable OUT not set or invalid.\n")
        return

    print ('PARTITION INFO   :', arg.entry_list)

    if arg.blob_type == 'update':
        payload_obj = update_payload(arg)
    elif arg.blob_type == 'bmp':
        payload_obj = bmp_payload(arg)
    else:
        return

    blob = open(payload_obj.outfile, "wb")
    payload_obj.parse_entry_list()
    payload_obj.fill_header(blob)
    payload_obj.fill_entry(blob)
    payload_obj.fill_image(blob)
    payload_obj.update_blob_size(blob)
    payload_obj.update_entry(blob)
    blob.close()

def inspect_BUP(arg):
    if arg.inspect_blob is None:
        sys.stderr.write("Error. Last argument must be a path to a valid blob file. Exiting...\r\n")
        sys.exit(1)

    print ('BLOB PATH:')
    print (os.path.realpath(arg.inspect_blob.name))

    if arg.blob_type == 'update':
        payload_obj = inspect_update_payload(arg)
    elif arg.blob_type == 'bmp':
        sys.stderr.write("Error. BMP blob inspecting is not currently supported. Exiting...\r\n")
        sys.exit(1)
    else:
        sys.stderr.write("Error. Unknown blob type. Exiting...\r\n")
        sys.exit(1)

    if arg.inspect_mode is True and arg.check_entries is True:
        print ()
        payload_obj.check_entry_table()
    elif arg.inspect_mode is True:
        print ()
        payload_obj.print_blob_header()
        print ()
        payload_obj.print_entry_table()

    print ()
    if arg.inspect_extract_bin_list is not None:
        # Check "OUT" variable is set and is valid if binary extraction is specified
        if not 'OUT' in os.environ or not os.path.isdir(os.environ["OUT"]):
            sys.stderr.write("Environment variable OUT not set or invalid.\r\n" \
                             "Error. Cannot save binaries. Exiting...\r\n"
                            )
            sys.exit(1)
        payload_obj.extract_binaries()

    arg.inspect_blob.close()

class payload():
    gpt_part_name_len_max = 36

    def gen_version(self):
        # bit[27:24]: BUP minor version in bcd:
        # bit[23:16]: BUP major version in bcd:
        # bit[15:14]: BUP release revision:
        # bit[12:8]: BUP release version in bcdMonth:
        # bit[7:0]: BUP release version in bcdYear:
        version = bup_bcd_ver_yy | bup_bcd_ver_mm << 8 | bup_rev << 14
        version = version | bup_bcd_ver_maj << 16 | bup_bcd_ver_min << 24
        return version

    def __init__(self, args):
        self.magic = bup_magic
        self.version = self.gen_version()
        self.blob_size_pos = struct.calcsize('=16sI')
        self.header_packing = '=16sIIIIII'
        self.uncomp_size_pos = struct.calcsize('=16sIIIII')
        self.accessory = args.accessory
        self.entry_list = args.entry_list

    def get_binary_name(self, filename):
        if (filename == ""): return filename;
        out_path = os.environ.get("OUT")
        if os.path.isfile(filename):
            binary_name = filename
        else:
            binary_name = os.path.join(out_path, filename)
            if not os.path.isfile(binary_name):
                sys.stderr.write("File %s does not exist\n" % binary_name)
                return
        return binary_name

    def parse_entry_list(self):
        entry_list = self.entry_list.split(';')
        self.entry_info_list = []
        for entry in entry_list:
            entry = entry.strip()
            entry_info = entry.split(' ')
            self.entry_info_list.append(entry_info)

    def fill_header(self, blob):
        self.header_size = struct.calcsize(self.header_packing)

        if self.accessory and os.path.isfile(self.accessory):
            accessory_handle = open(self.accessory, 'rb')
            accessory_handle.seek(0, os.SEEK_END)
            self.header_size += accessory_handle.tell()

        header_tuple = (self.magic.encode('utf-8'), self.version, 0, self.header_size,
                        len(self.entry_info_list), self.blob_type, 0)

        header = struct.pack(self.header_packing, *header_tuple)
        blob.write(header)

        if self.accessory and os.path.isfile(self.accessory):
            accessory_handle.seek(0, os.SEEK_SET)
            blob.write(accessory_handle.read())

    def update_blob_size(self, blob):
        blob.seek(self.blob_size_pos, os.SEEK_SET)
        blobsize = struct.pack('=I', self.blob_size)
        blob.write(blobsize)
        blob.seek(self.uncomp_size_pos, os.SEEK_SET)
        blob.write(blobsize)

    def fill_entry(self, blob):
        empty_entry = struct.pack(self.entry_packing, *self.entry_tuple)
        for i in range(0, len(self.entry_info_list)):
            blob.write(empty_entry)

    def fill_image(self, blob):
        self.entry_update_list = []
        for entry_info in self.entry_info_list:
            binary_handle = open(entry_info[0], 'rb')
            binary_handle.seek(0, os.SEEK_END)
            length = binary_handle.tell()
            offset = blob.tell()

            binary_handle.seek(0, os.SEEK_SET)
            blob.write(binary_handle.read())
            binary_handle.close()

            entry_update = (offset, length)
            self.entry_update_list.append(entry_update)

        self.blob_size = blob.tell()


class update_payload(payload):
    spec_len_max = 128

    def __init__(self, arg):
        payload.__init__(self, arg)
        self.blob_type = 0
        self.entry_packing = '=40sIIII128s'
        self.entry_tuple = (''.encode('utf-8'), 0, 0, 0, 0, ''.encode('utf-8'))
        self.param_c = 5
        self.outfile = 'ota.blob'

    def parse_entry_list(self):
        payload.parse_entry_list(self)

        for i in range(0, len(self.entry_info_list)):

            entry_info = self.entry_info_list[i]
            if len(entry_info) != self.param_c:
                print ('Invalid entry tuple:', entry_info)
                return

            binary_name = payload.get_binary_name(self, entry_info[0])

            if len(entry_info[1]) > payload.gpt_part_name_len_max:
                sys.stderr.write("ERROR:Partition name too long(>%s) %s\n"\
                    % (payload.gpt_part_name_len_max, entry_info[1]));
                sys.exit(-1);
            part_name = entry_info[1]

            try:
                # store entry list version in bcd number
                version = int(entry_info[2], 16)
            except ValueError:
                version = 0

            try:
                op_mode = int(entry_info[3])
            except ValueError:
                op_mode = 0

            if len(entry_info[4]) > self.spec_len_max:
                sys.stderr.write("ERROR:Spec too long(>%s) %s\n"\
                    % (self.spec_len_max, entry_info[4]));
                sys.exit(-1);
            spec_info = entry_info[4]
            if (spec_info == "common"):
                spec_info = ""

            entry_info = (binary_name, part_name, version, op_mode, spec_info)
            self.entry_info_list[i] = entry_info

    def update_entry(self, blob):
        blob.seek(self.header_size, os.SEEK_SET)
        for i in range(0, len(self.entry_info_list)):
            entry_info = self.entry_info_list[i]
            entry_update = self.entry_update_list[i]
            part_name = entry_info[1]
            version = entry_info[2]
            op_mode = entry_info[3]
            spec_info = entry_info[4]
            offset = entry_update[0]
            length = entry_update[1]
            entry_tuple = (part_name.encode('utf-8'), offset, length, version, op_mode, spec_info.encode('utf-8'))
            updated_entry = struct.pack(self.entry_packing, *entry_tuple)
            blob.write(updated_entry)

class bmp_payload(payload):
    tp_table = {'nvidia':0, 'lowbattery':1, 'charging':2, 'charged':3,
                'fullycharged':4, 'sata_fw_ota':5, 'verity_yellow_pause':6,
                'verity_yellow_continue':7, 'verity_orange_pause':8,
                'verity_orange_continue':9, 'verity_red_pause':10,
                'verity_red_continue':11, 'verity_red_stop':12,  }

    res_table = {'480':0, '720':1, '810':2, '1080':3, '4k':4, '1200_p':5, }

    def __init__(self, arg):
        payload.__init__(self, arg)
        self.blob_type = 1
        self.entry_packing = '=IIII36s'
        self.entry_tuple = (0, 0, 0, 0, ''.encode('utf-8'))
        self.param_c = 3
        self.outfile = 'bmp.blob'

    def parse_entry_list(self):

        payload.parse_entry_list(self)

        for i in range(0, len(self.entry_info_list)):

            entry_info = self.entry_info_list[i]
            if len(entry_info) != self.param_c:
                print ('Invalid entry tuple:', entry_info)
                return

            binary_name = payload.get_binary_name(self, entry_info[0])

            try:
                tp = self.tp_table[entry_info[1]]
            except KeyError:
                tp = 13

            try:
                res = self.res_table[entry_info[2]]
            except KeyError:
                res = 6

            entry_info = (binary_name, tp, res)
            self.entry_info_list[i] = entry_info

    def update_entry(self, blob):
        blob.seek(self.header_size, os.SEEK_SET)
        for i in range(0, len(self.entry_info_list)):
            entry_info = self.entry_info_list[i]
            tp = entry_info[1]
            res = entry_info[2]
            entry_update = self.entry_update_list[i]
            offset = entry_update[0]
            length = entry_update[1]
            entry_tuple = (tp, offset, length, res, ''.encode('utf-8'))
            updated_entry = struct.pack(self.entry_packing, *entry_tuple)
            blob.write(updated_entry)

# Update blob parsing
class inspect_update_payload(update_payload):
    def __init__(self, arg):
        update_payload.__init__(self, arg)
        self.header_accessory_packing = '=Q'
        self.header_name_tuple = ("magic", "version", "blob_size", "header_size", "entry_count", "type", "uncomp_blob_size", "accessory")
        self.entry_name_tuple = ("part_name", "offset", "part_size", "version", "op_mode", "tnspec")
        self.accessory_present = False

        self.blob_file = arg.inspect_blob
        self.raw_extract_bin_list = arg.inspect_extract_bin_list

        self.blob_header_tuple = struct.unpack(self.header_packing, self.blob_file.read(struct.calcsize(self.header_packing)))
        self.blob_header_dict = dict(zip(self.header_name_tuple, self.blob_header_tuple))

        # # Detect if optional accessory field (8 bytes) is present
        if self.blob_header_dict['header_size'] > struct.calcsize(self.header_packing):
            self.blob_header_dict['accessory'] = struct.unpack(self.header_accessory_packing, self.blob_file.read(struct.calcsize(self.header_accessory_packing)))[0]
            self.accessory_present = True

        if not self._valid():
            sys.stderr.write("Warning. Invalid input blob file. Results may be unexpected.\r\n" \
                             "      Input magic: " + self.blob_header_dict['magic'] + "\r\n" \
                             "   Expected magic: " + self.magic + "\r\n" \
                             "    Input version: " + format(self.blob_header_dict['version'], "#010x") + "\r\n" \
                             " Expected version: " + format(self.version, "#010x") + "\r\n" \
                             "       Input type: " + str(self.blob_header_dict['type']) + "\r\n" \
                             "    Expected type: " + str(self.blob_type) + " (type 1 BMP blobs are not currently supported)\r\n" \
                            )

        if self.blob_header_dict['entry_count'] > arg.inspect_max_entries:
            print ()
            print ("Blob header indicates " + str(self.blob_header_dict['entry_count']) + " partitions in this blob.\r\n" \
                  "Limiting display to the first " + str(arg.inspect_max_entries) + " partitions.\r\n" \
                  "Use the '--max-entries' or '-m' option to specify otherwise.")
            self.blob_entry_list = list(range(arg.inspect_max_entries))
        else:
            self.blob_entry_list = list(range(self.blob_header_dict['entry_count']))

        self.blob_entry_max_width_list = list(range(len(self.entry_name_tuple)))
        for i in range(len(self.blob_entry_max_width_list)):
            self.blob_entry_max_width_list[i] = len(self.entry_name_tuple[i])

        self.blob_file.seek(self.blob_header_dict['header_size'], os.SEEK_SET)
        self._generate_entry_list()

    def _valid(self):
        if (self.blob_header_dict['magic'].decode('utf-8') == self.magic) and (self.blob_header_dict['type'] == self.blob_type):
            return 1
        else:
            return 0

    def _generate_entry_list(self):
        for idx, blob_entry in enumerate(self.blob_entry_list):
            try:
                blob_entry_tuple = struct.unpack(self.entry_packing, self.blob_file.read(struct.calcsize(self.entry_packing)))
                blob_entry_dict = dict(zip(self.entry_name_tuple, blob_entry_tuple))
                self.blob_entry_list[idx] = blob_entry_dict

                for n in range(len(self.blob_entry_max_width_list)):
                    try:
                        if isinstance(blob_entry_tuple[n], int):
                            if len(str(blob_entry_tuple[n]).strip(' \t\n\0')) > self.blob_entry_max_width_list[n]:
                                self.blob_entry_max_width_list[n] = len(str(blob_entry_tuple[n]).strip(' \t\n\0'))
                        else:
                            if len(str(blob_entry_tuple[n].decode('utf-8')).strip(' \t\n\0')) > self.blob_entry_max_width_list[n]:
                                self.blob_entry_max_width_list[n] = len(str(blob_entry_tuple[n].decode('utf-8')).strip(' \t\n\0'))
                    except:
                        pass
            except:
                sys.stderr.write("Warning. Cannot parse partition number " + str(idx) + ".\r\n" \
                                 "Payload blob may be corrupt.\r\n" \
                                )

    def show_readable_version(self):
        # readable version: maj.min-yy.mm-rev
        ver = self.blob_header_dict['version']
        bcd_ver_min = (ver & 0x0f000000) >> 24
        bcd_ver_maj = (ver & 0x00ff0000) >> 16
        rev = (ver & 0x0000c000) >> 14
        bcd_ver_mm = (ver & 0x00001f00) >> 8
        bcd_ver_yy = ver & 0x000000ff
        version = "v"
        version = version + "{:x}".format(bcd_ver_maj)
        version = version + "."
        version = version + "{:x}".format(bcd_ver_min)
        version = version + "-"
        version = version + "20" + "{:x}".format(bcd_ver_yy)
        version = version + "."
        version = version + "{:x}".format(bcd_ver_mm)
        version = version + "-"
        version = version + "{:x}".format(rev)
        return version

    def print_blob_header(self):
        print ("BLOB HEADER:")
        print ("       Magic: " + str(self.blob_header_dict['magic'].decode('utf-8')))
        print ("     Version: " + self.show_readable_version() \
                               + " (" + format(self.blob_header_dict['version'], "#010x") + ")")
        print ("   Blob Size: " + "{:,}".format(self.blob_header_dict['blob_size']) + " bytes")
        print (" Header Size: " + "{:,}".format(self.blob_header_dict['header_size']) + " bytes")
        print (" Entry Count: " + str(self.blob_header_dict['entry_count']) + " partition(s)")
        print ("        Type: " + str(self.blob_header_dict['type']) + " (0 for update, 1 for BMP)")
        print ("Uncompressed\r\n" \
              "   Blob Size: " + "{:,}".format(self.blob_header_dict['uncomp_blob_size']) + " bytes")
        print ("   Accessory:", end=" ")
        if self.accessory_present == True:
            print (format(self.blob_header_dict['accessory'], "#018x"))
        else:
            print ("Not Present")
        return

    def print_entry_table(self):
        print ("ENTRY TABLE:")
        print ("|", end=" ")
        for idx, entry_name in enumerate(self.entry_name_tuple):
            print (entry_name.center(self.blob_entry_max_width_list[idx]) + " |", end=" ")
        print ()
        for blob_entry in self.blob_entry_list:
            print ("|", end=" ")
            try:
                print (str(blob_entry['part_name'].decode('utf-8')).strip(' \t\n\0').rjust(self.blob_entry_max_width_list[0]) + " |", end=" ")
                print (str(blob_entry['offset']).rjust(self.blob_entry_max_width_list[1]) + " |", end=" ")
                print (str(blob_entry['part_size']).rjust(self.blob_entry_max_width_list[2]) + " |", end=" ")
                print (str("{:x}".format(blob_entry['version'])).center(self.blob_entry_max_width_list[3]) + " |", end=" ")
                print (str(blob_entry['op_mode']).center(self.blob_entry_max_width_list[4]) + " |", end=" ")
                print (str(blob_entry['tnspec'].decode('utf-8')).strip(' \t\n\0').ljust(self.blob_entry_max_width_list[5]) + " |", end=" ")
                print ()
            except:
                print ("SKIPPED".center(sum(self.blob_entry_max_width_list) + (len(self.blob_entry_max_width_list)*2) + 3) + " |")
                pass
        return

    def check_entry_table(self):
        print ("Checking entry table ...")

        # Find out all partitions that have tnspec
        # Find out all tnspec
        part_name_list = list()
        spec_info_list = list()
        for blob_entry in self.blob_entry_list:
            part_name = str(blob_entry['part_name']).strip(' \t\n\0')
            tnspec = str(blob_entry['tnspec']).strip(' \t\n\0')

            if tnspec == "":
                continue

            if len(part_name_list):
                for idx, name in enumerate(part_name_list):
                    if name == part_name:
                        break
                    else:
                        if idx == (len(part_name_list) - 1):
                            part_name_list.append(part_name)
                            break
            else:
                part_name_list.append(part_name)

            if len(spec_info_list):
                for idx, spec in enumerate(spec_info_list):
                    if spec == tnspec:
                        break
                    else:
                        if idx == (len(spec_info_list) - 1):
                            spec_info_list.append(tnspec)
                            break
            else:
                spec_info_list.append(tnspec)

        # Verify every entry in spec_info_list must have an entry in part_name_list
        valid = True
        for part in part_name_list:
            blob_entry = filter(lambda entry: (str(entry['part_name']).strip(' \t\n\0') == part), self.blob_entry_list)
            blob_entry = copy.deepcopy(blob_entry)

            for spec in spec_info_list:
                found = False
                for idx, entry in enumerate(blob_entry):
                    e_spec = str(entry['tnspec']).strip(' \t\n\0')
                    if spec == e_spec:
                        found = True
                        blob_entry.remove(blob_entry[idx])
                        break
                if (found == False):
                    valid = False
                    sys.stderr.write("Error: " + "The " + part + " missed SPEC " + spec + ".\r\n")

        if (valid == False):
            sys.exit(1)
        else:
            print ("Check entry table successful")

    def extract_binaries(self):
        extract_bin_list = [extract_bin.strip(' \t\n\0') for extract_bin in self.raw_extract_bin_list.split(';')]
        extract_bin_set = set(extract_bin_list)
        extract_bin_missing_set = extract_bin_set.copy()
        out_path = os.environ.get("OUT")
        out_ext = ".raw.bin"
        out_delim = "_"

        print ("Saving partitions to \"" + out_path + "\"")
        print ("File names are of format \"<part_name>[" + out_delim + "<op_str>][" + out_delim + "<tnspec>]" + out_ext + "\"")
        print

        for blob_entry in self.blob_entry_list:
            part_name = str(blob_entry['part_name'].decode('utf-8')).strip(' \t\n\0')
            op_mode = blob_entry['op_mode']
            tnspec = str(blob_entry['tnspec'].decode('utf-8')).strip(' \t\n\0')

            if op_mode == 0:
                op_str = ""
            elif op_mode == 1:
                op_str = "dev"
            elif op_mode == 2:
                op_str = "prod"
            else:
                op_str = op_mode

            if tnspec == "common":
                tnspec = ""

            if (part_name in extract_bin_set) or ("all" in extract_bin_set):
                # Binary will be saved to <OUT>/<part_name>[_<op_str>][_<tnspec>].raw.bin
                binary_name = out_delim.join(filter(None, [part_name, op_str, tnspec])) + out_ext
                binary_path = os.path.join(out_path, binary_name)

                try:
                    save_file = open(binary_path, "wb+")
                except IOError as e:
                    sys.stderr.write("Error. " + str(e) + "\r\nCannot save binary. Exiting...\r\n")
                    sys.exit(1)

                # Seek to the offset of the partition data within the blob
                self.blob_file.seek(int(blob_entry['offset']), os.SEEK_SET)

                # Write from the blob to the output binary "part_size" number of bytes
                # from "offset" of the specified partition name
                save_file.write(self.blob_file.read(int(blob_entry['part_size'])))

                print ("Saved file \"" + binary_name + "\"")

                # Remove partition name that was just saved from the list of
                # missing binaries (e.g. in extraction list but not in payload)
                extract_bin_missing_set.discard(part_name)

        # Remove the "all" keyword from the set of missing binaries after
        # the for loop going through the payload entry list is done
        extract_bin_missing_set.discard("all")

        # Check if the set of missing binaries to extract is not empty, which indicates
        # that there was a partition in the extract list that was not present in the payload entry list
        if len(extract_bin_missing_set) > 0:
            for missing_bin in extract_bin_missing_set:
                sys.stderr.write("Warning. Could not find \"" + missing_bin + "\" in the blob entry table.\r\n")
            sys.stderr.write("Verify that the exact partition name(s) exist in the entry table output.\r\n")
        return

# Main function
def main(arg):
    if (arg.inspect_mode is True) or (arg.inspect_extract_bin_list is not None):
        inspect_BUP(arg)
    elif arg.entry_list is not None:
        generate_BUP(arg)
    else:
        sys.stderr.write("Error. Arguments required. See: ./" + os.path.basename(__file__) + " -h. Exiting...\r\n")
        sys.exit(1)

if __name__ == '__main__':
    param = parse_args()
    main(param)
    sys.exit(0)
