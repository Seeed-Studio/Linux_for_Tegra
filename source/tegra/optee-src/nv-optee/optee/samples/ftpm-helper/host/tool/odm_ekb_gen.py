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

import argparse
import csv
import os
import sys
import subprocess
import struct
import shutil
from typing import Tuple
from typing import Optional
from io import TextIOWrapper
from cryptography.hazmat.primitives import hmac, hashes
from cryptography.hazmat.primitives.kdf import kbkdf
from cryptography.hazmat.primitives.kdf import hkdf
from cryptography.hazmat.backends import default_backend


SUCCESS = 0
FAILED = 1
output_dir = "odm_out"
output_file_template = output_dir + "/ftpm_eks_{}.img"
output_verify_file = output_dir + "/ftpm_keys.txt"
cmd_gen_ek_csr = "./ftpm_manufacturer_gen_ek_csr.sh"
cmd_gen_ek_certs = "./ftpm_manufacturer_ca_simulator.sh"
cmd_openssl = "openssl"
ca_output_dir = "ca_out"
ek_cert_rsa_template = ca_output_dir + "/ek_cert_rsa-{}.der"
ek_cert_ec_template = ca_output_dir + "/ek_cert_ec-{}.der"


class Context:
    def __init__(self):
        self.kdk_db: str = ""
        self.verify: bool = False
        self.fh_ftpm_keys: Optional[TextIOWrapper] = None


def encode(data: str, is_hex: bool) -> bytes:
    result = b''
    if is_hex:
        # Convert hex string to bytes
        try:
            result = bytes.fromhex(data)
        except Exception as e:
            print("Converting hex string: ", data, " to bytes failed: ", e)
            result = b''
    else:
        # Encode as ASCII bytes
        try:
            result = data.encode("ascii")
        except Exception as e:
            print("Converting non hex string: ", data, " to bytes failed: ", e)
            result = b''
    return result

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

def display_device_sn(sn: bytes) -> str:
    sn_hex = sn.hex()
    return "{}-{}".format(sn_hex[:4], sn_hex[4:])

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

def gen_silicon_id(kdk: str, sn: bytes) -> bytes:
    kdk_nrk = kdf(encode(kdk, True), encode("NRK", False), encode("00", True))
    # Only in PSC_BL1, sn is treated as little-endian when calculating the key
    # For all other places that SN involves, we require SN to be big-endian
    silicon_id = kdf(kdk_nrk, encode("ECA_SEED", False), convert_sn_endian(sn))
    return silicon_id

def gen_ftpm_eps_contents(silicon_id: bytes, sn: bytes) -> Tuple[bytes, bytes]:
    ftpm_seed_origin = kdf(silicon_id, encode("fTPM", False), encode("00", True))
    hm = hmac.HMAC(ftpm_seed_origin, hashes.SHA256(), backend=default_backend())
    hm.update("fTPM".encode(encoding="utf8"))
    ftpm_seed = hm.finalize()

    root_seed_hkdf = hkdf.HKDF(algorithm=hashes.SHA256(), length=64,
                               salt=encode("00", True),
                               info=encode("Root_Seed", False))
    root_seed = root_seed_hkdf.derive(ftpm_seed)

    eps_seed = os.urandom(32)
    eps_hkdf = hkdf.HKDF(algorithm=hashes.SHA256(), length=64, salt=eps_seed, info=sn)
    eps = eps_hkdf.derive(root_seed)
    return eps_seed, eps

def generate_ftpm_ekb_contents(sn: bytes, eps_seed: bytes) -> bytes:
    # File format
    # content size | magic id | version major | version minor
    # tag | length | value
    # tag | length | value
    # ......
    # end-tag(\0\0\0\0) | end-tag-len(\0\0\0\0)

    magic_id = b"NVFTPM\0\0"
    version_major = 1
    version_minor = 0
    tag_sn = 1
    tag_eps_seed = 2
    tag_ek_cert_rsa = 3
    tag_ek_cert_ec = 4
    ek_cert_rsa_file = ek_cert_rsa_template.format(display_device_sn(sn))
    ek_cert_ec_file = ek_cert_ec_template.format(display_device_sn(sn))

    if not os.path.exists(ek_cert_rsa_file):
        raise Exception("RSA EK certificate can't be found.")
    if not os.path.exists(ek_cert_ec_file):
        raise Exception("EC EK certificate can't be found.")

    with open(ek_cert_rsa_file, "rb") as fd:
        ek_cert_rsa = fd.read()
    with open(ek_cert_ec_file, "rb") as fd:
        ek_cert_ec = fd.read()
    content_size = 12    # magic ID + version major + version minor
    content_size += 8 + len(sn)
    content_size += 8 + len(eps_seed)
    content_size += 8 + len(ek_cert_rsa)
    content_size += 8 + len(ek_cert_ec)
    content_size += 8    # end tag and end tag length

    header_fmt = "<I8sHH"
    entry_fmt = "<II"
    file_blob = struct.pack(header_fmt, content_size, magic_id, version_major, version_minor)
    file_blob += struct.pack(entry_fmt, tag_sn, len(sn)) + sn
    file_blob += struct.pack(entry_fmt, tag_eps_seed, len(eps_seed)) + eps_seed
    file_blob += struct.pack(entry_fmt, tag_ek_cert_rsa, len(ek_cert_rsa)) + ek_cert_rsa
    file_blob += struct.pack(entry_fmt, tag_ek_cert_ec, len(ek_cert_ec)) + ek_cert_ec
    file_blob += struct.pack("xxxxxxxx")

    return file_blob

def generate_ftpm_keys(sn: bytes, eps: bytes, context: Context) -> bool:
    sn_string = display_device_sn(sn)
    if context.fh_ftpm_keys == None:
        print("Invalid fTPM keys file handle. Device SN: ", sn_string)
        return False

    ek_cert_rsa_file = ek_cert_rsa_template.format(sn_string)
    ek_cert_ec_file = ek_cert_ec_template.format(sn_string)

    cmd = [cmd_openssl]
    cmd.extend(["x509"])
    cmd.extend(["-inform", "der"])
    cmd.extend(["-in", ek_cert_rsa_file])
    cmd.extend(["-pubkey"])
    cmd.extend(["-noout"])
    rsa_ek_pubkey = run_command(cmd)

    cmd = [cmd_openssl]
    cmd.extend(["x509"])
    cmd.extend(["-inform", "der"])
    cmd.extend(["-in", ek_cert_ec_file])
    cmd.extend(["-pubkey"])
    cmd.extend(["-noout"])
    ec_ek_pubkey = run_command(cmd)

    context.fh_ftpm_keys.write("# device_sn\n")
    context.fh_ftpm_keys.write("{}\n".format(sn_string))
    context.fh_ftpm_keys.write("# eps\n")
    context.fh_ftpm_keys.write("{}\n".format(eps.hex()))
    context.fh_ftpm_keys.write("# rsa_ek_pubkey\n")
    context.fh_ftpm_keys.write(rsa_ek_pubkey)
    context.fh_ftpm_keys.write("# ec_ek_pubkey\n")
    context.fh_ftpm_keys.write(ec_ek_pubkey)
    context.fh_ftpm_keys.write("\n")

    return True

def run_command(cmd) -> str:
    print("Running command: ", " ".join(cmd))

    try:
        # Run the command and capture its output, error, and return code
        result = subprocess.run(cmd, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, universal_newlines=True)
        output = result.stdout
        error = result.stderr
        return_code = result.returncode
    except subprocess.CalledProcessError as e:
        print("Run command error: ", e)
        raise e

    if return_code != 0:
        print(output)
        print(error)
        raise ValueError("Running command failed with error: {}".format(return_code))
    return output

def initialize(kdk_db: str, verify: bool) -> Optional[Context]:
    if kdk_db == None or not os.path.isfile(kdk_db):
        print("The KDK db file can't be found.")
        return None
    kdk_db_path = os.path.abspath(kdk_db)

    # Set the working directory
    try:
        wd = os.path.dirname(os.path.abspath(__file__))
        os.chdir(wd)
    except Exception as e:
        print("Changing current working directory failed.")
        print(e)
        return None

    if os.path.exists(output_dir) and not os.path.isdir(output_dir):
        print("Creating the output folder: ", output_dir, " failed. File exists.")
        return None
    if os.path.exists(output_dir):
        print("Cleaning the output directory...")
        try:
            shutil.rmtree(output_dir)
        except Exception as e:
            print("Cleaning the output folder: ", output_dir, " failed.")
            return None

    try:
        os.makedirs(output_dir)
    except Exception as e:
        print("Creating the output folder: ", output_dir, " failed.")
        return None

    fh_ftpm_keys = None
    if verify:
        try:
            fh_ftpm_keys = open(output_verify_file, 'w', newline='')
        except Exception as e:
            print("Creating the verify output file failed: ", e)
            return None

    context = Context()
    context.kdk_db = kdk_db_path
    context.verify = verify
    if verify:
        context.fh_ftpm_keys = fh_ftpm_keys
    return context

def main() -> int:
    parser = argparse.ArgumentParser(description='The tool used by ODM to generate fTPM SN, EPS Seed and certificates.')
    parser.add_argument('--kdk_db', help='The csv file which contains the KDK list.')
    parser.add_argument("--verify", action="store_const", const=True, help='Save fTPM keys to verify on a Jetson device.')

    args = parser.parse_args()
    context = initialize(args.kdk_db, args.verify)
    if context == None:
        return FAILED

    # fh: file handle, cr: csv reader
    with open(args.kdk_db, mode='r', newline='') as fh_kdk_db:
        cr_kdk_db = csv.reader(fh_kdk_db, delimiter=' ', quotechar='|', quoting=csv.QUOTE_MINIMAL)
        for row in cr_kdk_db:
            try:
                oem_id = int(row[0], 16)
                sn = int(row[1], 16)
            except Exception:
                print("Warning: Illegal oem_id(", row[0], ") or sn(", row[1], "), ignored.")
                continue

            if oem_id.bit_length() > 16:
                print("Warning: oem_id: ", oem_id, " must be a 16-bit integer, ignored.")
                continue
            if sn.bit_length() > 64:
                print("Warning: sn: ", sn, " must be a 64-bit integer, ignored.")
                continue

            device_kdk = row[2]
            # device_sn is a combination of oem_id and sn
            device_sn = oem_id.to_bytes(2, "big")
            sn_high = (sn >> 32).to_bytes(4, "big")
            sn_low = (sn & 0xffffffff).to_bytes(4, "big")
            device_sn += sn_high
            device_sn += sn_low

            print("Creating odm EKB outputs for device: ", display_device_sn(device_sn))
            silicon_id = gen_silicon_id(device_kdk, device_sn)
            ftpm_eps_seed, ftpm_eps = gen_ftpm_eps_contents(silicon_id, device_sn)

            try:
                # Generate EK CSRs: ek_csr_rsa-{device_sn}.der and ek_csr_ec-{device_sn}.der
                cmd = [cmd_gen_ek_csr]
                cmd.extend(["-s", display_device_sn(device_sn)])
                cmd.extend(["-e", ftpm_eps.hex()])
                run_command(cmd)

                # Generate EK certificates by signing the CSRs generated above
                cmd = [cmd_gen_ek_certs]
                cmd.extend(["-s", display_device_sn(device_sn)])
                run_command(cmd)
            except Exception:
                continue

            # Generate a file which contains SN, and corresponding fTPM EPS Seed, the EK RSA Certificate,
            # and the EK EC Certificate. This file will be parsed by oem_ekb_gen.py to
            # generate the final EKB image.
            blob = generate_ftpm_ekb_contents(device_sn, ftpm_eps_seed)
            fn_ftpm_ekb = output_file_template.format(display_device_sn(device_sn))
            with open(fn_ftpm_ekb, "wb") as fh_ftpm_ekb:
                fh_ftpm_ekb.write(blob)

            if context.verify:
                generate_ftpm_keys(device_sn, ftpm_eps, context)

    if context.fh_ftpm_keys != None:
        context.fh_ftpm_keys.close()
    return SUCCESS


if __name__ == '__main__':
    ret_code = main()
    sys.exit(ret_code)
