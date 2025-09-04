#!/usr/bin/env python3

# Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

import argparse
import codecs
import os.path
import struct
import sys

from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives import cmac
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from math import ceil

def nist_sp_800_108_with_CMAC(key, context=b"", label=b"", len=16):
    okm = b""
    output_block = b""
    for count in range(ceil(len/16)):
        data = b"".join([bytes([count+1]), label.encode(encoding="utf8"), bytes([0]), context.encode(encoding="utf8"), int(len*8).to_bytes(4, byteorder="big")])
        c = cmac.CMAC(algorithms.AES(key), backend=default_backend())
        c.update(data)
        output_block = c.finalize()
        okm += output_block
    return okm[:len]

def load_file_check_size(f, size=16):
    with open(f, 'rb') as fd:
        content = fd.read().strip()
        if content.startswith(b'0x') or content.startswith(b'0X'):
            content = content[2:]
        key = codecs.decode(content, 'hex')
        if len(key) != size:
            raise Exception("Wrong size")
        return key

def main():
    global verbose

    parser = argparse.ArgumentParser(description='''
    Generates LUKS passphrase by using a key file which indicates a key from EKB.
    The key file includes one user-defined 16-bytes symmetric key.
    ''')

    parser.add_argument('-c', '--context-string', nargs=1, required=True, type=str, help="The context string (max 40 byts) for generating passphrase.")
    parser.add_argument('-e', '--ecid', nargs=1, type=str, help="The ECID (Embedded chip ID) of the chip.")
    parser.add_argument('-k', '--key-file', nargs=1, help="The key (16 bytes)  file  in hex format.")
    parser.add_argument('-u', '--unique-pass', default=False, action='store_true', help="Generate a unique passphrase.")
    parser.add_argument('-g', '--generic-pass', dest='unique-pass', action='store_false', help="Generate a generic passphrase.")

    args = parser.parse_args()

    if len(args.context_string[0]) > 40:
        raise Exception("The max context string length is 40 bytes.\n")

    if args.unique_pass:
        if not args.ecid:
            raise Exception("The ECID is needed for the unique passphrase.\n")
        if len(args.ecid[0]) > 34:
            raise Exception("The max ECID string length is 32 bytes (34 bytes if \"0x\" is included).\n")
        ecid_str = ''.join(str(c) for c in args.ecid)
        ecid_int = int(ecid_str, 16)
        # use the lower 100 bits as ECID
        clear_bits = 0xfffffff
        ecid_int &= ~(clear_bits << 100)
        ecid_str = "%032x" % ecid_int

    if not args.key_file:
        # key file doesn't been specified; use the default key
        key = b'\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'
    elif all(map(os.path.exists, [args.key_file[0]])):
        # load key file
        key = load_file_check_size(args.key_file[0])
    else:
        raise Exception("%s cannot be opened.\n" % args.key_file[0])

    # Derive LUKS key
    if args.unique_pass:
        label_str = "luks-srv-ecid"
        luks_key = nist_sp_800_108_with_CMAC(key, ecid_str, label_str)
    else:
        label_str = "luks-srv-generic"
        context_str = "generic-key"
        luks_key = nist_sp_800_108_with_CMAC(key, context_str, label_str)

    # Generate passphrase
    if args.unique_pass:
        label_str = "luks-srv-passphrase-unique"
    else:
        label_str = "luks-srv-passphrase-generic"
    context_str = ''.join(str(c) for c in args.context_string)
    passphrase = nist_sp_800_108_with_CMAC(luks_key, context_str, label_str)
    print("%s" % passphrase.hex())

if __name__ == "__main__":
    main()

