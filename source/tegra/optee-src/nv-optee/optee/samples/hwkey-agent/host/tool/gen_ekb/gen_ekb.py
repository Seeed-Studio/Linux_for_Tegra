#!/usr/bin/env python3

# Copyright (c) 2021-2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

# Version History
#
# 1.0: Initial version with T194 support
# 1.1: Add T234 support
# 1.2: Refactored EKB structure to make it more generic
# 1.3: Removed UEFI encryption and authentication keys from EKB (moved to MB2-BCT instead)
# 1.4: Support both OEM_K1 and OEM_K2 keys as the root key of EKB image (T234 only)
# 1.5: Add encoding fTPM serial number (SN), EPS seed and two fTPM EK Certificates (RSA and EC) support
#

import argparse
import codecs
import os.path
import struct
import sys

from Crypto.Cipher import AES
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives import cmac
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from math import ceil

#EKS image version
version_major = 1
version_minor = 0

# EKB key types
ekb_kernel_encryption_key = 1;
ekb_disk_encryption_key = 2;
ekb_uefi_var_auth_key = 4;
ekb_ftpm_sn = 91;
ekb_ftpm_eps_seed = 92;
ekb_ftpm_rsa_ek_cert = 93;
ekb_ftpm_ec_ek_cert = 94;

def pkcs7_padding(plain_text):
    block_size = AES.block_size
    number_of_bytes_to_pad = block_size - len(plain_text) % block_size
    padding_str = number_of_bytes_to_pad * bytes([number_of_bytes_to_pad])
    padded_plain_text =  plain_text + padding_str
    return padded_plain_text

def ekb_cmac_gen(key, msg):
    c = cmac.CMAC(algorithms.AES(key), backend=default_backend())
    c.update(msg)
    return c.finalize()

def ekb_encrypt(content, key, iv):
    cipher = Cipher(algorithms.AES(key), modes.CBC(iv), backend=default_backend())
    encryptor = cipher.encryptor()
    return encryptor.update(content) + encryptor.finalize()

def generateBlob(content, key_ek, key_ak):
    content_header_fmt = "<I4sxxxxxxxx"
    ekb_header_fmt = "<I8sHH"
    maxsize = 131072    # 128KB
    size = 0
    ekb_header_len = 16
    ekb_header_wo_size_field = 12
    ekb_content_header_len = 16
    ekb_cmac_len = 16
    ekb_iv_len = 16
    contentmaxsize = maxsize - ekb_header_len - ekb_content_header_len - ekb_cmac_len - ekb_iv_len;

    # padding the content, then check the length after padding
    if (len(content) % AES.block_size) != 0:
        content = pkcs7_padding(content)

    if len(content) > contentmaxsize:
        raise Exception("Content is too big")

    # generate a random iv, then encrypt the key data content
    # join the iv and content header, calc cmac on the total data
    iv = os.getrandom(16, os.GRND_NONBLOCK)
    encrypted_content = ekb_encrypt(content, key_ek, iv)
    data = b"".join([iv, encrypted_content])
    content_header = struct.pack(content_header_fmt, len(encrypted_content), b"EEKB")
    data = b"".join([content_header, data])
    ekb_cmac = ekb_cmac_gen(key_ak, data)

    # if size is less than 1K, then padding it to 1024
    ekb_size = len(ekb_cmac) + len(content_header) + len(iv) + len(encrypted_content) + ekb_header_wo_size_field
    if ekb_size < 1024:
        pad_char = (1024 - ekb_size) % 256
        encrypted_content += bytes([pad_char]) * (1024 - ekb_size)

    # pack the ekb image with header
    eks_len = ekb_header_wo_size_field + len(ekb_cmac) + len(content_header) + len(iv) + len(encrypted_content)
    header = struct.pack(ekb_header_fmt, eks_len, b"NVEKBP\0\0", version_major, version_minor)
    blob = header + ekb_cmac + content_header + iv + encrypted_content
    return blob

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

def gen_ekb_rk(fuse_key, fv):
    rk = AES.new(fuse_key, AES.MODE_ECB)
    return rk.encrypt(fv)

def load_file_check_size(f, size=16):
    with open(f, 'rb') as fd:
        content = fd.read().strip()
        key = codecs.decode(content, 'hex')
        if len(key) != size:
            raise Exception("Wrong size")
        return key

def pack_key(f, tag):
    fmt = "<II"
    with open(f, 'rb') as fd:
        content = fd.read().strip()
        key = codecs.decode(content, 'hex')
        key_header = struct.pack(fmt, tag, len(key))
        key_blob = key_header + key
        return key_blob

def pack_digits(digit_str, tag):
    fmt = "<II"
    digits_byte = bytes.fromhex(digit_str)
    digits_header = struct.pack(fmt, tag, len(digits_byte))
    digits_blob = digits_header + digits_byte
    return digits_blob

def pack_bin(f, tag):
    fmt = "<II"
    with open(f, "rb") as fd:
        bin = fd.read()
        bin_header = struct.pack(fmt, tag, len(bin))
        bin_blob = bin_header + bin
        return bin_blob

def main():
    parser = argparse.ArgumentParser(description='''
    Generates the EKB image by using KEK2(for T194) or OEM_K1/OEM_K2(for T234) derived key to encrypt user data.
    ''')

    parser.add_argument('-chip', nargs=1, required=False, help="specify chip: t194|t234. default: t194")
    parser.add_argument('-kek2_key', nargs=1, required=False, help="kek2 key (16 bytes) file in hex format [t194 only]")
    parser.add_argument('-oem_k1_key', nargs=1, required=False, help="oem_k1 key (32 bytes) file in hex format [t234 only]")
    parser.add_argument('-oem_k2_key', nargs=1, required=False, help="oem_k2 key (32 bytes) file in hex format [t234 only]")
    parser.add_argument('-fv', nargs=1, required=True, help="fixed vectors (16 bytes) files for EKB in hex format")
    parser.add_argument('-in_sym_key', nargs=1, required=False, help="symmetric key file in hex format [t194: 16 bytes | t234: 32 bytes]")
    parser.add_argument('-in_sym_key2', nargs=1, required=False, help="16-byte symmetric key file in hex format")
    parser.add_argument('-in_auth_key', nargs=1, required=False, help="16-byte symmetric key file in hex format")
    parser.add_argument('-in_ftpm_sn', nargs=1, required=False, help="fTPM serial number")
    parser.add_argument('-in_ftpm_eps_seed', nargs=1, required=False, help="fTPM EPS seed file in hex format")
    parser.add_argument('-in_ftpm_rsa_ek_cert', nargs=1, required=False, help="fTPM RSA EK Cert in DER format")
    parser.add_argument('-in_ftpm_ec_ek_cert', nargs=1, required=False, help="fTPM EC EK Cert in DER format")
    parser.add_argument('-out', nargs=1, required=True, help="where the eks image file is stored")

    args = parser.parse_args()

    chip = "t194"
    if args.chip != None:
        chip = args.chip[0]

    fuse_key_file = ""
    if chip == "t194":
        if args.kek2_key == None:
            raise Exception("kek2_key is not set, it is required for t194")
        if not all(map(os.path.exists, [args.kek2_key[0]])):
            raise Exception("kek2_key file cannot be openned\n")
        fuse_key_file = args.kek2_key[0]
    elif chip == "t234":
        if args.oem_k1_key == None and args.oem_k2_key == None:
            raise Exception("No oem_k1_key or oem_k2_key is set.")
        if args.oem_k1_key != None and args.oem_k2_key != None:
            raise Exception("Only one oem key should be set.")

        if args.oem_k1_key != None and os.path.exists(args.oem_k1_key[0]):
            fuse_key_file = args.oem_k1_key[0]
        if args.oem_k2_key != None and os.path.exists(args.oem_k2_key[0]):
            fuse_key_file = args.oem_k2_key[0]

        if len(fuse_key_file) == 0:
            raise Exception("oem_k1_key or oem_k2_key file cannot be openned")
    else:
        raise Exception("Invalid chip: " + chip)

    # load fuse key
    if chip == "t194":
        fuse_key = load_file_check_size(fuse_key_file, 16)
    if chip == "t234":
        fuse_key = load_file_check_size(fuse_key_file, 32)

    # load fixed vector
    if args.fv != None:
        fv_ekb = load_file_check_size(args.fv[0])
    else:
        raise Exception("fixed vector is not set\n")

    # generate root key
    ekb_rk = gen_ekb_rk(fuse_key, fv_ekb)

    # generate derived keys
    ekb_ek = nist_sp_800_108_with_CMAC(ekb_rk, "ekb", "encryption")
    ekb_ak = nist_sp_800_108_with_CMAC(ekb_rk, "ekb", "authentication")

    # load and pack keys
    in_content = b""
    if args.in_sym_key != None and os.path.exists(args.in_sym_key[0]):
        in_content += pack_key(args.in_sym_key[0], ekb_kernel_encryption_key)
    if args.in_sym_key2 != None and os.path.exists(args.in_sym_key2[0]):
        in_content += pack_key(args.in_sym_key2[0], ekb_disk_encryption_key)
    if args.in_auth_key != None and os.path.exists(args.in_auth_key[0]):
        in_content += pack_key(args.in_auth_key[0], ekb_uefi_var_auth_key)
    if args.in_ftpm_sn != None:
        in_content += pack_digits(args.in_ftpm_sn[0], ekb_ftpm_sn)
    if args.in_ftpm_eps_seed != None and os.path.exists(args.in_ftpm_eps_seed[0]):
        in_content += pack_key(args.in_ftpm_eps_seed[0], ekb_ftpm_eps_seed)
    if args.in_ftpm_rsa_ek_cert != None and os.path.exists(args.in_ftpm_eps_seed[0]):
        in_content += pack_bin(args.in_ftpm_rsa_ek_cert[0], ekb_ftpm_rsa_ek_cert)
    if args.in_ftpm_ec_ek_cert != None and os.path.exists(args.in_ftpm_ec_ek_cert[0]):
        in_content += pack_bin(args.in_ftpm_ec_ek_cert[0], ekb_ftpm_ec_ek_cert)

    # Padding key type and key len with 0 in the end
    end = struct.pack("xxxxxxxx")
    in_content += end

    # generate "eks.img"
    with open(args.out[0], 'wb') as f:
        f.write(generateBlob(in_content, ekb_ek, ekb_ak))

if __name__ == "__main__":
    main()

