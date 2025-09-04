#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
# property and proprietary rights in and to this material, related
# documentation and any modifications thereto. Any use, reproduction,
# disclosure or distribution of this material and related documentation
# without an express license agreement from NVIDIA CORPORATION or
# its affiliates is strictly prohibited.
#


import argparse
import csv
import os,binascii
import errno
from cryptography.hazmat.primitives import hmac, hashes
from cryptography.hazmat.primitives.kdf import kbkdf
from cryptography.hazmat.backends import default_backend
from ecdsa import NIST256p, SigningKey
from lib.aes_drbg import AES_DRBG


def encode(data: str, is_hex: bool, desc: str) -> bytes:
    encoded_str = b"\x00"
    if is_hex:
        # Convert hex string to bytes
        try:
            encoded_str = bytes.fromhex(data)
        except ValueError as e:
            raise ValueError("Assumed hex=True, but {}:{} is invalid hex!".format(desc, e)) from e
    else:
        # Encode as ASCII bytes
        try:
            encoded_str = data.encode("ascii")
        except UnicodeError as e:
            raise ValueError("Assumed hex=False, but {}:{} is invalid ASCII!".format(desc, e)) from e
    return encoded_str

def kdf(kdk: bytes, label: bytes, context: bytes, L: int = 256, rlen: int = 32, order = None) -> bytes:
    """Derive KDF from root key and NIST SP800-108 input (label and context)."""
    if not context:
        context = b"\x00"

    if order is None:
        order = kbkdf.CounterLocation.BeforeFixed

    hkdf = kbkdf.KBKDFHMAC(
        algorithm=hashes.SHA256(),
        mode=kbkdf.Mode.CounterMode,
        length=L // 8,
        rlen=rlen // 8,
        llen=4,
        location=order,
        label=label,
        context=context,
        fixed=None,
        backend=default_backend(),
    )
    return hkdf.derive(kdk)

# This function uses the algorithm defined in NIST SP800-56Ar3,
# 5.6.1.2.2 Key Pair Generation by Testing Candidates,
# to generate an ECDSA private key from a given seed
def gen_ecdsa_private_key(seed: bytes):
    if seed is None:
        raise ValueError("No seed is provided when generating ECDSA private key!")

    drbg = AES_DRBG(256)
    # For AES-256 CTR-DRBG, the seed size is 48 bytes
    # Refer to NIST SP800-90Ar1, 10.2.1 CTR_DRBG
    if len(seed) < 48:
        seed = seed + b"\x00" * (48 - len(seed))
    drbg.instantiate(seed)

    retries = 100
    i = 0
    while True:
        key = drbg.generate(64)
        key = key[0:32]
        int_key = int.from_bytes(key, "big")
        if int_key > 0 and int_key < NIST256p.order:
            # print("An ECDSA private key is generated: " + key.hex() + ". Rounds: " + str(i + 1))
            break

        i += 1
        if i >= retries:
            return b''

    return key

def convert_sn_endian(sn: bytes) -> bytes:
    # The device SN is 10 bytes, and has the format:
    # <oem_id, 2 bytes><sn, 8 bytes>
    # oem_id is stored in fuse "odm_info" which is a 4-byte fuse
    # All 4-byte or less Tegra fuses are stored in little-endian so we need
    # to convert the endian of oem_id in device SN before doing the key calculation
    # The left 8 bytes don't have an endian issue - it is big-endian everywhere
    result = bytearray(sn)
    result[0] = sn[1]
    result[1] = sn[0]
    return bytes(result)

def gen_silicon_id_pubkey(kdk: str, sn: bytes):
    # Prepare the SILICON ID key pair
    kdk_nrk = kdf(encode(kdk, True, "KDK"), encode("NRK", False, "Label"), encode("00", True, "Context"))
    silicon_id = kdf(kdk_nrk, encode("ECA_SEED", False, "Label"), convert_sn_endian(sn))

    silicon_id_asym_origin = kdf(silicon_id, encode("Asym", False, "Label"),
                                 encode("00", True, "Context"))
    hm = hmac.HMAC(silicon_id_asym_origin, hashes.SHA256(), backend=default_backend())
    hm.update("Asym".encode(encoding="utf8"))
    silicon_id_asym = hm.finalize()

    ecdsa_key = gen_ecdsa_private_key(silicon_id_asym)
    if len(ecdsa_key) == 0:
        return ""
    silicon_id_private_key = SigningKey.from_string(ecdsa_key, curve=NIST256p)
    silicon_id_public_key = silicon_id_private_key.get_verifying_key()
    # print("Silicon ID private key: \n" + silicon_id_private_key.to_pem().decode("ascii"))
    # print("Silicon ID public key: \n" + silicon_id_public_key.to_pem().decode("ascii"))
    return silicon_id_public_key.to_string().hex()

def clean(input):
    tmpFile = "tmp.csv"
    with open(input, "r") as file, open(tmpFile, "w") as outFile:
        reader = csv.reader(file, delimiter=' ')
        writer = csv.writer(outFile, delimiter=' ')
        for row in reader:
            colValues = []
            for col in row:
                colValues.append(col.lower())
            del colValues[-1]
            writer.writerow(colValues)
    os.rename(tmpFile, input)

def get_device_sn(oem_id: int, sn: int) -> bytes:
    device_sn = oem_id.to_bytes(2, "big")
    sn_high = (sn >> 32).to_bytes(4, "big")
    sn_low = (sn & 0xffffffff).to_bytes(4, "big")
    device_sn += sn_high
    device_sn += sn_low
    return device_sn

def main():
    parser = argparse.ArgumentParser(description='The KDK db generation tool.')
    parser.add_argument('--oem_id', help='The ID of the OEM vendor. A positive 16-bit integer in hex format')
    parser.add_argument('--sn', help='The beginning of device serial number. A positive 64-bit unsigned integer in hex format')
    parser.add_argument('--num_devices', type=int, help='the number of devices')
    parser.add_argument("--destroy", action='store', help='destroy the kdk database')

    args = parser.parse_args()
    if args.destroy != None and (args.oem_id or args.sn):
        parser.print_help()
        return

    if not (args.destroy or args.oem_id or args.sn or args.num_devices):
        parser.print_help()
        return

    if not args.destroy:
        if not args.oem_id.lower().startswith("0x") or not args.sn.lower().startswith("0x"):
            raise ValueError("parameter in hex format needs to start with \"0x\"")
        args.oem_id = int(args.oem_id, 16)
        args.sn = int(args.sn, 16)
        if args.oem_id < 0 or args.oem_id.bit_length() > 16:
            raise ValueError("oem_id must be a 16-bit integer")
        if args.sn < 0 or args.sn.bit_length() > 64:
            raise ValueError("sn must be a 64-bit integer")

    if args.destroy:
        clean(args.destroy)
        return

    # OEM_ID: <oem_id>
    # Starting device SN: <args.sn>
    # Num_devices: <args.num_devices>
    output_dir="ftpm_kdk"
    if not os.path.exists(output_dir):
        try:
            os.makedirs(output_dir)
        except OSError as e:
            if e.errno != errno.EEXIST:
                raise e
    elif not os.path.isdir(output_dir):
        print(str(output_dir) + " is not a directory or does not exist")
        raise NotADirectoryError
    elif not os.access(output_dir, os.W_OK):
        print("Write access denied on ", output_dir)
        raise PermissionError

    device_sn = get_device_sn(args.oem_id, args.sn)
    kdkfilename='ftpm_kdk/kdk_db_{}-{}-{}.csv'.format(device_sn.hex()[:4], device_sn.hex()[4:], args.num_devices)
    pubkeyfilename='ftpm_kdk/pubkey_db_{}-{}-{}.csv'.format(device_sn.hex()[:4], device_sn.hex()[4:], args.num_devices)
    with open(kdkfilename, 'w', newline='') as kdkfile, open(pubkeyfilename, 'w', newline='') as pubkeyfile:
        writer = csv.writer(kdkfile, delimiter=' ',
                            quotechar='|', quoting=csv.QUOTE_MINIMAL)
        pubkeywriter=csv.writer(pubkeyfile, delimiter=' ',
                            quotechar='|', quoting=csv.QUOTE_MINIMAL)
        for i in range(0, args.num_devices):
            device_kdk = binascii.b2a_hex(os.urandom(32)).decode()

            # The device SN is 10 bytes: <OEM_ID><High 32 bits of SN><Low 32 bits of SN>
            # OEM_ID is burnt into the fuse ODM_INFO
            # High 32 bits of SN is burnt into the fuse ODM_ID0
            # Low 32 bits of SN is burnt into the fuse ODM_ID1
            device_sn = get_device_sn(args.oem_id, args.sn + i)

            while True:
                device_pubkey = gen_silicon_id_pubkey(device_kdk, device_sn)
                if len(device_pubkey) == 0:
                    print("Generate ECDSA key pair using KDK: " + device_kdk + " failed. Will start over.")
                    device_kdk = binascii.b2a_hex(os.urandom(32)).decode()
                else:
                    break

            writer.writerow(["{}".format(device_sn.hex()[:4]),
                             "{}".format(device_sn.hex()[4:]), device_kdk])
            pubkeywriter.writerow(["{}".format(device_sn.hex()[:4]),
                                   "{}".format(device_sn.hex()[4:]), device_pubkey])


if __name__ == '__main__':
    main()
