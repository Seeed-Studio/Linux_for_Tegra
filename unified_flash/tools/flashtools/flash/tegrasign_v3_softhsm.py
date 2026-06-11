#
# SPDX-FileCopyrightText: Copyright (c) 2014-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA Corporation is strictly prohibited.
#
from tegrasign_v3_util import *
import hashlib
import pkcs11
from pkcs11.util.rsa import encode_rsa_public_key
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.backends import default_backend
import ctypes
import os

'''
@Description
The purpose of this file is to supply a list of API hooks and reference implementations
that may be useful for running with an HSM server.
As such, the API hooks are identified as required, and will be denoted with [REQUIRE]
tag below in the individual comment's @note section, whereas reference implementations
will be denoted as [REFERENCE].

@Rationale
The API hooks are required because they are used in the tegrasign_v3 scripts and OEMs
can optionally replace with their implementation. This decision is purely based on OEMs.
The reference implementations are used in the API hooks to mimic HSM operations, but
how the actual HSM servers will handle such the operations are HSM-specific.

@Note
The API hooks are secure boot operation related, yet not all are required for an OEM to
overwrite. The actual list is dependent on which secure boot scheme is chosen by the OEM.

Below is a list of API hooks in this file:
    do_hmac_sha256_hsm
    do_random_hsm
    do_aes_gcm_hsm
    do_rsa_pss_hsm
    do_ecc_hsm
    *do_ed25519_hsm - Not supported
    get_rsa_mod_hsm
    get_rsa_mod_from_pubkey_hsm
    get_rsa_mont_hsm
    get_rsa_mont_from_pubkey_hsm
    get_ecc_pub_hsm
    *get_ed25519_pub_hsm - Not supported
    oem_hsm_kdf
    oem_hsm_aes_gcm
    oem_hsm_hmacsha

Below is a list of reference implementations in this file:
    get_key_content
    hsm_server_store_derived_key_to_key_database
    hsm_server_search_key_database
    nist_sp800_108_kdf
    get_fskpkey_hsm_server
    get_sbk_hsm_server
    send_to_hsm_server_kdf
    hsm_server_aes_gcm
    send_to_hsm_server_aes_gcm
    send_to_hsm_server_hmacsha

Below is a simple partial breakdown for clarity:

hsm.py        -->  HSM/some secure host       -->  HSM server
(API hook)         (Reference API)                 (Reference API)
==============     ===========================     =================================================
oem_hsm_kdf()      send_to_hsm_server_kdf()        hsm_server_search_key_database() + key derivation
oem_hsm_aes_gc()   send_to_hsm_server_aes_gcm()    hsm_server_search_key_database() + encryption
oem_hsm_hmacsh()   send_to_hsm_server_hmacsha()    hsm_server_search_key_database() + hmac-sha

Below is a simple flow to illustrate how to enable SoftHSM for image signing/encryption:

1. install softhsm package
2. install pkcs11 python module
3. configure softhsm
4. create token with defining user pin
5. enroll keys
6. set is_softhsm_on=True in tegrasign_v3_softhsm.py
Then refers to README for mode details.

Below is a sample of environment setting for HSM (based on SoftHSM solution):

hsm_token_label = "HSM" # The token name which is in SoftHSM to store keys.
hsm_lib_path = "/usr/lib/softhsm/libsofthsm2.so" # The library path of SoftHSM.
hsm_user_pin = "1234" # The user pin number to access the token and the keys.

'''

# These are env setup variables for SoftHSM connection
hsm_token_label = "HSM"
hsm_lib_path = "/usr/lib/softhsm/libsofthsm2.so"
hsm_user_pin = "1234"

# DER encoding related constants
DER_OCTET_STRING_TAG = 0x04
DER_SEQUENCE_TAG = 0x30
MAX_DER_HEADER_SIZE = 10  # maximum DER header size

# supported curves and corresponding coordinate lengths
# ECC curve OIDs as hex strings
ECC_P256_OID = bytes.fromhex('06082A8648CE3D030107')    # P-256 curve OID: 1.2.840.10045.3.1.7
ECC_P521_OID = bytes.fromhex('06052B81040023')          # P-521 curve OID: 1.3.132.0.35
SUPPORTED_CURVES = {
    ECC_P256_OID: {
        'key_size': 256,
        'allocated_size': ECC_KEY_SIZE,
        'coord_length': 32,
        'name': 'P-256',
    },
    ECC_P521_OID: {
        'key_size': 521,
        'allocated_size': ECC521_KEY_SIZE,
        'coord_length': 66,
        'name': 'P-521',
    }
}

# Crypto function
def aes_gcm_hsm(key_label, iv, aad, plaintext):
    # Load the PKCS#11 library
    pkcs11 = ctypes.CDLL(hsm_lib_path)

    # Define constants
    CKR_OK = 0
    CKF_SERIAL_SESSION = 0x00000004
    CKF_RW_SESSION = 0x00000002
    CKU_USER = 1
    CKK_AES = 0x0000001F
    CKO_SECRET_KEY = 0x00000004
    CKM_AES_GCM = 0x00001087

    class CK_SESSION_HANDLE(ctypes.Structure):
        _fields_ = [("handle", ctypes.c_ulong)]

    class CK_MECHANISM(ctypes.Structure):
        _fields_ = [("mechanism", ctypes.c_ulong), ("pParameter", ctypes.c_void_p), ("ulParameterLen", ctypes.c_ulong)]

    class CK_GCM_PARAMS(ctypes.Structure):
        _fields_ = [("pIv", ctypes.POINTER(ctypes.c_ubyte)), ("ulIvLen", ctypes.c_ulong), ("ulIvBits", ctypes.c_ulong),
                    ("pAAD", ctypes.POINTER(ctypes.c_ubyte)), ("ulAADLen", ctypes.c_ulong), ("ulTagBits", ctypes.c_ulong)]

    class CK_ATTRIBUTE(ctypes.Structure):
        _fields_ = [("type", ctypes.c_ulong), ("pValue", ctypes.c_void_p), ("ulValueLen", ctypes.c_ulong)]

    class CK_TOKEN_INFO(ctypes.Structure):
        _fields_ = [
            ("label", ctypes.c_char * 32),
            ("manufacturerID", ctypes.c_char * 32),
            ("model", ctypes.c_char * 16),
            ("serialNumber", ctypes.c_char * 16),
            ("flags", ctypes.c_ulong),
            ("ulMaxSessionCount", ctypes.c_ulong),
            ("ulSessionCount", ctypes.c_ulong),
            ("ulMaxRwSessionCount", ctypes.c_ulong),
            ("ulRwSessionCount", ctypes.c_ulong),
            ("ulMaxPinLen", ctypes.c_ulong),
            ("ulMinPinLen", ctypes.c_ulong),
            ("ulTotalPublicMemory", ctypes.c_ulong),
            ("ulFreePublicMemory", ctypes.c_ulong),
            ("ulTotalPrivateMemory", ctypes.c_ulong),
            ("ulFreePrivateMemory", ctypes.c_ulong),
            ("hardwareVersion", ctypes.c_ubyte * 2),
            ("firmwareVersion", ctypes.c_ubyte * 2),
            ("utcTime", ctypes.c_char * 16)
        ]

    # Initialize the library
    pkcs11.C_Initialize(None)

    # Find key slot with token name
    slot_list = (ctypes.c_ulong * 256)() # Assume there are at most 256 slots
    slot_count = ctypes.c_ulong(len(slot_list))
    pkcs11.C_GetSlotList(True, slot_list, ctypes.byref(slot_count))

    # Search key slot by token name
    token_name = hsm_token_label
    for slot in slot_list[:slot_count.value]:
        token_info = CK_TOKEN_INFO()
        pkcs11.C_GetTokenInfo(slot, ctypes.byref(token_info))
        if token_info.label.decode('utf-8').strip() == token_name:
            slot_num = slot
            break

    # Open a session with the token
    session = CK_SESSION_HANDLE()
    ret = pkcs11.C_OpenSession(slot_num, CKF_SERIAL_SESSION | CKF_RW_SESSION, None, None, ctypes.byref(session))
    if ret != CKR_OK:
        raise RuntimeError(f"Failed to open session: {ret}")

    # Login to the session
    ret = pkcs11.C_Login(session, CKU_USER, bytes(hsm_user_pin, 'utf-8'), len(bytes(hsm_user_pin, 'utf-8')))
    if ret != CKR_OK:
        raise RuntimeError(f"Failed to login: {ret}")

    # Prepare the search template
    label = bytes(key_label, 'utf-8')
    label_attr = CK_ATTRIBUTE(
        type=0x00000003,  # CKA_LABEL
        pValue=ctypes.cast(ctypes.create_string_buffer(label), ctypes.c_void_p),
        ulValueLen=len(label)
    )

    key = ctypes.c_ulong()

    # Find the AES key
    ret = pkcs11.C_FindObjectsInit(session, ctypes.byref(label_attr), 1)
    if ret != CKR_OK:
        raise RuntimeError(f"Failed to find object init: {ret}")
    ret = pkcs11.C_FindObjects(session, ctypes.byref(key), 1, ctypes.byref(ctypes.c_ulong(1)))
    if ret != CKR_OK:
        raise RuntimeError(f"Failed to find object: {ret}")
    ret = pkcs11.C_FindObjectsFinal(session)
    if ret != CKR_OK:
        raise RuntimeError(f"Failed to find object final: {ret}")

    # Debug code to get key value **************
    CKA_VALUE = 0x00000011  # CKA_VALUE constant

    # Prepare the attribute to hold the value
    value_len = ctypes.c_ulong(0)

    # First, get the size of the key value
    value_attr = CK_ATTRIBUTE(type=CKA_VALUE, pValue=None, ulValueLen=0)
    ret = pkcs11.C_GetAttributeValue(session, key, ctypes.byref(value_attr), 1)
    if ret != CKR_OK:
        raise RuntimeError(f"Failed to get attribute size: {ret}")

    # Allocate buffer to hold the key value
    buffer = ctypes.create_string_buffer(value_attr.ulValueLen)

    # Set the attribute to retrieve the key value
    value_attr.pValue = ctypes.cast(buffer, ctypes.c_void_p)
    value_attr.ulValueLen = len(buffer)

    # Get the key value
    ret = pkcs11.C_GetAttributeValue(session, key, ctypes.byref(value_attr), 1)
    if ret != CKR_OK:
        raise RuntimeError(f"Failed to get key value: {ret}")

    # Prepare GCM parameters
    iv_len = int(len(iv)/2)
    aad_len = int(len(aad)/2)
    iv_buffer = (ctypes.c_ubyte * iv_len).from_buffer_copy(bytes.fromhex(iv))
    aad_buffer = (ctypes.c_ubyte * aad_len).from_buffer_copy(bytes.fromhex(aad))
    gcm_params = CK_GCM_PARAMS(
        pIv=ctypes.cast(iv_buffer, ctypes.POINTER(ctypes.c_ubyte)),
        ulIvLen=iv_len,
        ulIvBits=iv_len * 8,
        pAAD=ctypes.cast(aad_buffer, ctypes.POINTER(ctypes.c_ubyte)),
        ulAADLen=aad_len,
        ulTagBits=128
    )

    mechanism = CK_MECHANISM(CKM_AES_GCM, ctypes.cast(ctypes.byref(gcm_params), ctypes.c_void_p), ctypes.sizeof(gcm_params))

    # Perform AES-GCM encryption
    ciphertext_len = ctypes.c_ulong()

    ret = pkcs11.C_EncryptInit(session, ctypes.byref(mechanism), key)
    if ret != CKR_OK:
        raise RuntimeError(f"EncryptionInit failed with code: {ret}")

    # Get ciphertext length
    ret = pkcs11.C_Encrypt(session, bytes(plaintext), len(plaintext), None, ctypes.byref(ciphertext_len))
    if ret != CKR_OK:
        raise RuntimeError(f"Encryption failed with code: {ret}")

    ciphertext = ctypes.create_string_buffer(ciphertext_len.value)

    # Performa AES GCM encryption
    ret = pkcs11.C_Encrypt(session, bytes(plaintext), len(plaintext), ciphertext, ctypes.byref(ciphertext_len))
    if ret != CKR_OK:
        raise RuntimeError(f"Encryption failed with code: {ret}")

    # Extract the tag from the end of ciphertext
    tag = ciphertext.raw[-16:]
    encrypted_content = ciphertext.raw[:-16]

    # Logout and close the session
    pkcs11.C_Logout(session)
    pkcs11.C_CloseSession(session)
    return encrypted_content, tag.hex()


'''
@brief The routine that calculate montgomery value from rsa public key

@param[in] public_key Public key of PKC extractd from HSM
@param[in] rsa_byte_count It should 384 bytes

@retval mont_buf The buffer after operation
'''
def rsa_montgomery(public_key, rsa_byte_count):
    # Extract modulus (n)
    modulus = public_key.public_numbers().n
    modulus_bytes = modulus.to_bytes(rsa_byte_count, byteorder='big')

    # Create a context for big integer operations
    # Prepare R = 2^(rsa_byte_count * 8)
    R = 1 << (rsa_byte_count * 8)

    # Convert modulus to a big integer
    n = int.from_bytes(modulus_bytes, byteorder='big')

    # Compute R^2 mod n
    R2 = (R * R) % n

    # Compute the modular inverse of R modulo n
    try:
        Ri = pow(R, -1, n)  # R inverse mod n
    except ValueError:
        # If inverse does not exist, return failure
        return None

    # Shift Ri left by rsa_byte_count * 8 bits
    Ri <<= (rsa_byte_count * 8)

    # Subtract 1 from Ri
    Ri -= 1

    # Ni = (R * Ri - 1) / n
    Ni = (Ri) // n

    # Convert Ni and R^2 back to byte arrays
    mprime = Ni.to_bytes(rsa_byte_count, byteorder='little')
    rsquare = R2.to_bytes(rsa_byte_count, byteorder='little')

    mont_buf = mprime + rsquare
    return mont_buf

'''
@brief The routine that reads the sbk/kek0/fskp key content
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] key_file The file to be read

@retval key The buffer that is read in string format
'''
def get_key_content(p_key):
    try:
        # to connect to SoftHSM
        lib = pkcs11.lib(hsm_lib_path)
        token = lib.get_token(token_label=hsm_token_label)
        with token.open(user_pin=hsm_user_pin) as session:
            key = session.get_key(label=p_key.hsm.get_type(), key_type=pkcs11.KeyType.AES)
            key_value = key[pkcs11.Attribute.VALUE]
            key_value_hex = key_value.hex()

        return key_value_hex

    except Exception as e:
        info_print('[HSM] Error to extract content from %s: %s' %(p_key.hsm.get_type(), str(e)))
        return None

'''
@brief The routine that invokes hmacsha on the buffer

@param[in] buf Buffer to be operated on
@param[in] p_key SignKey class which has info needed: filename, mode

@param[in] use_der_key Boolean flag indicating if reading the key from the file path defined for HSM,
           or use the key value from SignKey
           The true flag indicates taking the key value from the SignKey as this value is previously
           derived from an operation
           The false flag indicates reading the key from the file path defined for HSM operation

@retval hmac The buffer after operation
'''
def do_hmac_sha256_hsm(buf, p_key, use_der_key = False):
    if (use_der_key == True):
        key = hex_to_str(p_key.key.aeskey)
    else:
        key_type = p_key.hsm.get_type()
        key = get_key_content(p_key)

    try:
        # to connect to SoftHSM
        lib = pkcs11.lib(hsm_lib_path)
        token = lib.get_token(token_label=hsm_token_label)
        # To create a key from "key" variable for hmac sha256 usage
        # Because SBK key didn't have "sign" attribute,
        # need to import it with sign attribute into HSM
        # This is a temporary key, so no need to set Attribute.TOKEN
        with token.open(user_pin=hsm_user_pin) as session:
            hsm_hmac_label = 'HMAC_KEY'
            key_content = bytes.fromhex(key)
            key = session.create_object({
                pkcs11.Attribute.CLASS: pkcs11.constants.ObjectClass.SECRET_KEY,
                pkcs11.Attribute.KEY_TYPE: pkcs11.KeyType.GENERIC_SECRET,
                pkcs11.Attribute.VALUE: key_content,
                pkcs11.Attribute.LABEL: hsm_hmac_label,
                pkcs11.Attribute.SENSITIVE: True,
                pkcs11.Attribute.EXTRACTABLE: True,
                pkcs11.Attribute.SIGN: True
            })
            hmac_key = session.get_key(label=hsm_hmac_label, \
                                       key_type=pkcs11.KeyType.GENERIC_SECRET)
            hmac = hmac_key.sign(buf, mechanism=pkcs11.Mechanism.SHA256_HMAC)
        return hmac

    except Exception as e:
        info_print('[HSM] Error in calculating hmac sha256: %s' %(str(e)))
        return None

'''
@brief The routine that invokes random string generation

@param[in] p_key SignKey class which has info needed: ran.size, ran.count
@note
   size = byte size of the random string
   count = number of random strings to be generated

@param[out] p_key.ran.buf This holds the random hex arrays of ran.size length by ran.count:

@retval None
'''
def do_random_hsm(p_key, is_random = True):
    p_key.ran.buf = bytearray(p_key.ran.size * p_key.ran.count)

    try:
        # to connect to SoftHSM
        lib = pkcs11.lib(hsm_lib_path)
        token = lib.get_token(token_label=hsm_token_label)
        for i in range(p_key.ran.count):
            if is_random is True:
                with token.open(user_pin=hsm_user_pin) as session:
                    buf = session.generate_random(p_key.ran.size * 8) # length in bits
            else:
                buf = bytearray(p_key.ran.size) # zero byte array
            start = i * p_key.ran.size
            p_key.ran.buf[start:start+p_key.ran.size] = buf[:]
    except Exception as e:
        info_print('[HSM] Error in generating random number: %s' %(str(e)))

'''
@brief The routine that invokes aes-gcm on the buffer
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] buf Buffer to be operated on
@param[in] p_key SignKey class which has info needed: filename, mode, iv, aad, tag, verify
@param[in] iv The existing iv
@param[in] aad The existing aad
@param[in] use_der_key Boolean flag indicating if reading the key from the file path defined and send to HSM for later encryption operation
@note
    If use_der_key is True, the key will be sent to HSM for later encryption operation
    If use_der_key is False, the key label with key_id will be read from HSM directly for encryption operation

@retval buf_enc The buffer after operation
'''
def do_aes_gcm_hsm(buf, p_key, iv, aad, use_der_key=False):
    key_id = p_key.hsm.get_type()
    if (use_der_key == True):
        # Use user derived key as aes gcm encryption key
        # Send the key to HSM for later encryption operation
        key_id = 'USER_DER_KEY'
        hsm_server_store_derived_key_to_key_database(key_id, p_key.key.aeskey)

    buff_sig, tag = aes_gcm_hsm(key_id, iv, aad, bytes(buf))

    p_key.kdf.tag.set_buf(str_to_hex(tag))
    return buff_sig

'''
@brief The routine that invokes aescbc on the buffer

@param[in] buf Buffer to be operated on
@param[in] iv The existing iv
@param[in] p_key SignKey class which has info needed: filename, mode

@retval cmac The buffer after operation
'''
def do_aes_cbc_hsm(buf, iv, p_key):
    try:
        # to connect to SoftHSM
        lib = pkcs11.lib(hsm_lib_path)
        token = lib.get_token(token_label=hsm_token_label)
        key_label = p_key.hsm.get_type()
        with token.open(user_pin=hsm_user_pin) as session:
            # Create zero key for cmac calculation if key content is zero
            if is_zero_aes(p_key):
                key_label = 'ZERO_KEY'
                key_length = len(binascii.hexlify(p_key.key.aeskey))
                key_content = '0' * key_length
                aes_key_value = bytes.fromhex(key_content)
                cmac_key = session.create_object({
                    pkcs11.Attribute.CLASS: pkcs11.constants.ObjectClass.SECRET_KEY,
                    pkcs11.Attribute.KEY_TYPE: KeyType.AES,
                    pkcs11.Attribute.VALUE: aes_key_value,
                    pkcs11.Attribute.LABEL: key_label,
                    pkcs11.Attribute.SENSITIVE: False,
                    pkcs11.Attribute.EXTRACTABLE: True,
                    pkcs11.Attribute.SIGN: True
                })
            key = session.get_key(label=key_label, key_type=pkcs11.KeyType.AES)
            cbc = key.encrypt(buf, mechanism=pkcs11.Mechanism.AES_CBC, mechanism_param=iv)
        return cbc

    except Exception as e:
        info_print('[HSM] Error in do_aes_cbc_hsm: %s' %(str(e)))
        return None

'''
@brief The routine that invokes aescmac on the buffer

@param[in] buf Buffer to be operated on
@param[in] p_key SignKey class which has info needed: filename, mode

@retval cmac The buffer after operation
'''
def do_aes_cmac_hsm(buf, p_key):
    # TODO: to add an interface to import key or get key label from p_key
    # Now it's "sbk" inside HSM
    try:
        # to connect to SoftHSM
        lib = pkcs11.lib(hsm_lib_path)
        token = lib.get_token(token_label=hsm_token_label)
        with token.open(user_pin=hsm_user_pin) as session:
            key = session.get_key(label=p_key.hsm.get_type(), key_type=pkcs11.KeyType.AES)
            cmac = key.sign(buf, mechanism=pkcs11.Mechanism.AES_CMAC)
        return cmac

    except Exception as e:
        info_print('[HSM] Error in do_aes_cmac_hsm: %s' %(str(e)))
        return None

'''
@brief The routine that invokes rsa-pss on the buffer

@param[in] buf Buffer to be operated on
@param[in] p_key SignKey class which has info needed: filename, sha mode

@retval sig_data The buffer after operation
'''
def do_rsa_pss_hsm(buf, p_key):
    if (p_key.key.pkckey.Sha == Sha._256):
        sha_mechanism = pkcs11.Mechanism.SHA256
        sha_mfg = pkcs11.MGF.SHA256
    else:
        sha_mechanism = pkcs11.Mechanism.SHA512
        sha_mfg = pkcs11.MGF.SHA512

    try:
        lib = pkcs11.lib(hsm_lib_path)
        token = lib.get_token(token_label=hsm_token_label)
        with token.open(user_pin=hsm_user_pin) as session:
            hash_data = session.digest(buf, mechanism=sha_mechanism)
            key_label = p_key.hsm.l4t_key_label
            private_key = session.get_key(label=key_label, key_type=pkcs11.KeyType.RSA, \
                                          object_class=pkcs11.ObjectClass.PRIVATE_KEY)
            # Salt length is a fixed value - 32
            signature = private_key.sign(hash_data, mechanism=pkcs11.Mechanism.RSA_PKCS_PSS, \
                                         mechanism_param=(sha_mechanism, sha_mfg, 32))

        sig_data = swapbytes(bytearray(signature))
        return sig_data

    except Exception as e:
        info_print('[HSM] Error in rsa pss signing: %s' %(str(e)))
        return None

'''
@brief The routine that invokes ed25519 on the buffer
@Note: [REQUIRE]
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] buf Buffer to be operated on
@param[in] p_key SignKey class which has info needed: filename

@retval sig_data The buffer after operation
'''
def do_ed25519_hsm(buf, p_key):
    info_print('[HSM] Error do_ed25519_hsm is not supported yet')
    return None

'''
@brief The routine that generates the public modulus for the RSA private key

@param[in] p_key SignKey class which has info needed: filename, and keysize is updated
@param[in] pub_modf RSA public key filename
@param[in] pub_expf RSA public key exponent filename

@retval True for success, False otherwise
'''
def get_rsa_mod_hsm(p_key, pub_modf=None, pub_expf=None):
    try:
        # to connect to SoftHSM
        lib = pkcs11.lib(hsm_lib_path)
        token = lib.get_token(token_label=hsm_token_label)
        with token.open(user_pin=hsm_user_pin) as session:
            key_label = p_key.hsm.l4t_key_label
            pub = session.get_key(label=key_label, key_type=pkcs11.KeyType.RSA,
                               object_class=pkcs11.ObjectClass.PUBLIC_KEY)
            pub = encode_rsa_public_key(pub)
            public_key = serialization.load_der_public_key(pub, backend=default_backend())
            modulus = public_key.public_numbers().n
            modulus_hex = hex(modulus)[2:].upper()
            # Ensure even length for binascii.unhexlify()
            if len(modulus_hex) % 2:
                modulus_hex = '0' + modulus_hex
            rsa_n_bin = swapbytes(bytearray(binascii.unhexlify(str(modulus_hex))))

            if pub_modf:
                with open_file(pub_modf, 'wb') as f:
                    write_file(f, rsa_n_bin)

            if pub_expf:
                try:
                    public_exponent = public_key.public_numbers().e
                    exp_size = 384
                    exp_str = hex(public_exponent)[2:].upper()
                    exp_len = len(exp_str)

                    # Ensure exponent string is at least 8 characters
                    if exp_len < 8:
                        exp_str = exp_str.zfill(8)
                        exp_len = 8

                    exp_bytes = bytearray(str_to_hex(exp_str))
                    swap_bytes = swapbytes(exp_bytes)

                    buf = bytearray(exp_size)
                    buf[exp_size-4:exp_size] = swap_bytes[:]

                    with open(pub_expf, 'wb') as f:
                        f.write(buf)

                except Exception as exp_e:
                    info_print(f"[HSM] Error extracting RSA public exponent: {str(exp_e)}")
                    return False
            return True

    except Exception as e:
        info_print("[HSM] Can't get rsa key with label: %s" %(key_label))
        return False

'''
@brief The routine that generates the Montgomery values from the RSA key passed in
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] p_key SignKey class which has info needed: filename, and keysize is updated
@param[in] pub_montf RSA Montgomery filename

@retval True for success, False otherwise
'''
def get_rsa_mont_hsm(p_key, pub_montf):
    try:
        lib = pkcs11.lib(hsm_lib_path)
        token = lib.get_token(token_label=hsm_token_label)
        with token.open(user_pin=hsm_user_pin) as session:
            key_label = p_key.hsm.l4t_key_label
            pub = session.get_key(label=key_label,
                               key_type=pkcs11.KeyType.RSA,
                               object_class=pkcs11.ObjectClass.PUBLIC_KEY)
            rsa_key_bits = pub.key_length
            pub = encode_rsa_public_key(pub)
            public_key = serialization.load_der_public_key(pub, backend=default_backend())
            # Get rsa key length in byte
            rsa_byte_count = int(rsa_key_bits / 8)
            mont = rsa_montgomery(public_key, rsa_byte_count)

            if pub_montf:
                with open_file(pub_montf, 'wb') as f:
                    f.write(mont)
            return True

    except Exception as e:
        info_print("[HSM] Can't get rsa with label: %s" %(key_label))
        return False

'''
@brief The routine that generates the public modulus for the RSA public key

@param[in] p_key SignKey class which has info needed: filename, and keysize is updated
@param[in] pub_modf RSA public key filename
@param[in] pub_expf RSA public key exponent filename

@retval True for success, False otherwise
'''
def get_rsa_mod_from_pubkey_hsm(p_key, pub_modf=None, pub_expf=None):
    try:
        # to connect to SoftHSM
        lib = pkcs11.lib(hsm_lib_path)
        token = lib.get_token(token_label=hsm_token_label)
        with token.open(user_pin=hsm_user_pin) as session:
            key_label = p_key.hsm.l4t_key_label
            pub = session.get_key(label=key_label,
                               key_type=pkcs11.KeyType.RSA,
                               object_class=pkcs11.ObjectClass.PUBLIC_KEY)
            pub = encode_rsa_public_key(pub)
            public_key = serialization.load_der_public_key(pub, backend=default_backend())
            modulus = public_key.public_numbers().n
            modulus_hex = hex(modulus)[2:].upper()
            # Ensure even length for binascii.unhexlify()
            if len(modulus_hex) % 2:
                modulus_hex = '0' + modulus_hex
            rsa_n_bin = swapbytes(bytearray(binascii.unhexlify(str(modulus_hex))))

            if pub_modf:
                with open_file(pub_modf, 'wb') as f:
                    write_file(f, rsa_n_bin)

            if pub_expf:
                try:
                    public_exponent = public_key.public_numbers().e
                    exp_size = 384
                    exp_str = hex(public_exponent)[2:].upper()
                    exp_len = len(exp_str)

                    #  Ensure exponent string is at least 8 characters
                    if exp_len < 8:
                        exp_str = exp_str.zfill(8)
                        exp_len = 8

                    exp_bytes = bytearray(str_to_hex(exp_str))
                    swap_bytes = swapbytes(exp_bytes)

                    buf = bytearray(exp_size)
                    buf[exp_size-4:exp_size] = swap_bytes[:]

                    # Write exponent file
                    with open(pub_expf, 'wb') as f:
                        f.write(buf)

                except Exception as exp_e:
                    info_print(f"[SoftHSM] Error extracting RSA public exponent: {str(exp_e)}")
                    return False

            return True

    except Exception as e:
        info_print("[HSM] Can't get rsa public key modulus with label: %s" %(key_label))
        return False

'''
@brief The routine that generates the Montgomery values from the RSA public key passed in
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] p_key SignKey class which has info needed: filename, and keysize is updated
@param[in] pub_montf RSA Montgomery filename

@retval True for success, False otherwise
'''
def get_rsa_mont_from_pubkey_hsm(p_key, pub_montf):
    try:
        # to connect to SoftHSM
        lib = pkcs11.lib(hsm_lib_path)
        token = lib.get_token(token_label=hsm_token_label)
        with token.open(user_pin=hsm_user_pin) as session:
            key_label = p_key.hsm.l4t_key_label
            pub = session.get_key(label=key_label,
                                key_type=pkcs11.KeyType.RSA,
                                object_class=pkcs11.ObjectClass.PUBLIC_KEY)
            rsa_key_bits = pub.key_length
            pub = encode_rsa_public_key(pub)
            public_key = serialization.load_der_public_key(pub, backend=default_backend())
            # Get rsa key length in byte
            rsa_byte_count = int(rsa_key_bits / 8)
            mont = rsa_montgomery(public_key, rsa_byte_count)

            if pub_montf:
                with open_file(pub_montf, 'wb') as f:
                    f.write(mont)
            return True

    except Exception as e:
        info_print("[HSM] Can't get rsa mont with label: %s" %(key_label))
        return False

'''
@brief The routine that generates the public key for the ED25519 key passed in
@Note: [REQUIRE]
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] p_key SignKey class which has info needed: filename, and keysize is updated
@param[in] pub_keyf ED25519 public key filename

@retval True for success, False otherwise
'''
def get_ed25519_pub_hsm(p_key, pub_keyf):
    success = False
    info_print('[HSM] Error get_ed25519_pub_hsm is not supported yet')

    return success

'''
@brief The routine stores the [key, value] in the database
@Note: [REFERENCE]
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] output_key_id The key to be stored in the database
@param[in] output_key_val The matching key value to be stored in the database

@retval True for success, False otherwise
'''
def hsm_server_store_derived_key_to_key_database(output_key_id, output_key_val):
    try:
        lib = pkcs11.lib(hsm_lib_path)
        token = lib.get_token(token_label=hsm_token_label)
        with token.open(rw=True, user_pin=hsm_user_pin) as session:
            # Delete the object if it already exists
            for obj in session.get_objects({pkcs11.Attribute.LABEL: output_key_id}):
                obj.destroy()

            key = session.create_object({
                pkcs11.Attribute.CLASS: pkcs11.constants.ObjectClass.SECRET_KEY,
                pkcs11.Attribute.KEY_TYPE: pkcs11.KeyType.AES,
                pkcs11.Attribute.VALUE: output_key_val,
                pkcs11.Attribute.LABEL: output_key_id,
                pkcs11.Attribute.TOKEN: True,
                pkcs11.Attribute.SENSITIVE: False,
                pkcs11.Attribute.EXTRACTABLE: True,
                pkcs11.Attribute.ENCRYPT: True,
                pkcs11.Attribute.DECRYPT: True,
                pkcs11.Attribute.WRAP: True,
                pkcs11.Attribute.UNWRAP: True,
            })

    except Exception as e:
        info_print('[HSM] Error in database writing %s: %s\n' %(output_key_id, str(e)))
        return False

    return True

'''
@brief The routine searches through database for the requesting key

@param[in] in_key_id The key requests in the database

@retval in_key for success, None for not found
'''
def hsm_server_search_key_database(in_key_id):
    try:
        lib = pkcs11.lib(hsm_lib_path)
        token = lib.get_token(token_label=hsm_token_label)
        with token.open(user_pin=hsm_user_pin) as session:
            key = session.get_key(label=in_key_id)
            key_value = key[pkcs11.Attribute.VALUE]
            in_key = key_value.hex()
        return in_key

    except Exception as e:
        info_print('[HSM] Error in searching key %s: %s' %(in_key_id, str(e)))
        return None

'''
@brief The routine implement NIST SP800 108 KDF in counter mode with
# counter encoded as little-endian 32 bit counter
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] input_kdf_key The key value to be operated on
@param[in] in_label Label for the key
@param[in] in_context Context for the key
@param[in] is_blkdk The flag to indicate if the key is a block derivation key

@retval output_key The finished value
'''
def nist_sp800_108_kdf(input_kdf_key, in_label, in_context, is_blkdk):

    HexLabel = True
    HexContext = True
    if is_blkdk == True:
        msgStr = get_composed_msg(in_label, in_context, 256, False, HexContext)
    else:
        msgStr = get_composed_msg(in_label, in_context, 256, HexLabel, HexContext)
    msg = str_to_hex(msgStr)

    backup = SignKey()
    backup.key.aeskey = str_to_hex(input_kdf_key)

    backup.keysize = len(backup.key.aeskey)
    output_key = do_hmac_sha256_hsm(msg, backup, True)

    return output_key

'''
@brief The routine obtains FSKP related key string from the HSM server
@Note: [REFERENCE]
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] key_type The key type options are: FSKP_KDK, FSKP_EK, FSKP_AK

@retval fskp key in string format
'''
def get_fskpkey_hsm_server(key_type):
    p_key = SignKey()
    p_key.hsm.type = key_type

    return get_key_content(p_key)

'''
@brief The routine obtains SBK key string from the HSM server
@Note: [REFERENCE]
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@retval SBK key in string format
'''
def get_sbk_hsm_server():
    p_key = SignKey()
    p_key.hsm.type = KeyType.SBK

    return get_key_content(p_key)

'''
@brief The routine obtains PV_KEY key string from the HSM server

@retval PV_KEY key in string format
'''
def get_pvenckey_hsm_server():
    p_key = SignKey()
    p_key.hsm.type = KeyType.PV_KEY

    return get_key_content(p_key)

'''
@brief The routine requests HSM server to search the key_id in the database
 If it is not found, then HSM server should perform kdf derivation
 and store the [key, value] pair in its database
@Note: [REFERENCE]
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] in_key_id A string key ID and is to be used as input key for the KDF.
                     Type: string in ASCII.
                     Example: "SBK", "SBK_BCT_KDK_71f920fa275127a7b60fa4d4d41432a3"

@param[in] in_label The derivation label, to be used as input for the KDF.
                    Type: string of hex bytes.
                    Example: "00000000000000000000000000000000"

@param[in] in_context The derivation context, to be used as input for the KDF.
                      Type: string of hex bytes.
                      Example: "01010000", can be empty - ""

@param[in] output_key_id The string key ID of newly derived and stored inside OEM HSM key.
                         Type: string in ASCII.
                         Example: "SBK_BCT_DK_43c191bf6d6c3f263a8cd0efd4a058ab"
               About producing the output_key_id below:
               case 1) for in_key_id = SBK, FSKP_KDK, FSKP_EK, FSKP_AK, the stored value is fetched
               Note: due to the nature of the key, thus for FSKP_EK, FSKP_AK,
                     no further derivation is needed, while SBK and FSKP_KDK are
                     used for further derivation
               case 2) for other cases, derivation of in_key_id is performed

@param[in] is_blkdk The flag to indicate if the key is a block derivation key
                     Type: boolean.
                     Example: True for block derivation key, False for other keys

@retval True for success, False otherwise
'''
def send_to_hsm_server_kdf(in_key_id, in_label, in_context, output_key_id, is_blkdk):
    found_cached_key_id = hsm_server_search_key_database(output_key_id)

    # if derived key found then return success
    if found_cached_key_id != None:
        return True

    # SBK - special case - it's imported/generated in OEM
    # TODO: SBK_SB and SBK_HPSE are using the same key with SBK
    if in_key_id == KeyType.SBK or in_key_id == 'SBK_HPSE' or in_key_id == 'SBK_SB':
        input_kdf_key = get_sbk_hsm_server() # load secret SBK root key in the HSM

    # Note: If --hsm fskp_kdk is specified, then we search the database for the derived
    # fskp_ek and fskp_ak entries, and creat them if not found. They are defined as:
    #fskp_ek_md5um(fskp_ek_<label>_) or fskp_ak_md5um(fskp_ak_<label>_)
    # Below is a mock up behavior to be implemented by OEMs based on their use case
    elif in_key_id == KeyType.FSKP_KDK:
        input_kdf_key = get_fskpkey_hsm_server(in_key_id) # load secret FSKP key in the HSM

    # If --hsm fskp_ek or --hsm fskp_ak is specified, then the matching entry is searched
    # first with the above naming style. Or if the entries are not found, then we default
    # to search the key file paths then the value is written to the database
    elif in_key_id == KeyType.FSKP_EK:
        output_derived_key = get_fskpkey_hsm_server(in_key_id) # load secret FSKP_EK key in the HSM
        output_derived_key = str_to_hex(output_derived_key)
        return hsm_server_store_derived_key_to_key_database(output_key_id, output_derived_key)
    elif in_key_id == KeyType.FSKP_AK:
        output_derived_key = get_fskpkey_hsm_server(in_key_id) # load secret FSKP key in the HSM
        output_derived_key = str_to_hex(output_derived_key)
        return hsm_server_store_derived_key_to_key_database(output_key_id, output_derived_key)
    else:
        input_kdf_key = hsm_server_search_key_database(in_key_id)
        if input_kdf_key == None: # if search failed - this is unexpected - return FAILURE
            return False # or raise an exception

    output_derived_key = nist_sp800_108_kdf(input_kdf_key, in_label, in_context, is_blkdk)

    return hsm_server_store_derived_key_to_key_database(output_key_id, output_derived_key)

'''
@brief The routine requests HSM server to search the key_id in the database
 If it is not found, then HSM server should perform kdf derivation
 and store the {key, value} pair in its database
@Note: [REQUIRE]
@Note: This routine is expected to be replaced by OEM's own HSM implementation
 OEM HSM KDF must be implemented via NIST SP800 108 KDF in counter mode with
 counter encoded as little-endian 32 bit counter

@param[in] in_key_id A string key ID and is to be used as input key for the KDF.
                     Type: string in ASCII.
                     Example: "SBK", "SBK_BCT_KDK_71f920fa275127a7b60fa4d4d41432a3"

@param[in] in_label The derivation label, to be used as input for the KDF.
                    Type: string of hex bytes.
                    Example: "00000000000000000000000000000000"

@param[in] in_context The derivation context, to be used as input for the KDF.
                      Type: string of hex bytes.
                      Example: "01010000", can be empty - ""

@param[in] is_blkdk The flag to indicate if the key is a block derivation key
                     Type: boolean.
                     Example: True for block derivation key, False for other keys

@param[in] output_key_id The string key ID of newly derived and stored inside OEM HSM key.
                         Type: string in ASCII.
                         Example: "SBK_BCT_DK_43c191bf6d6c3f263a8cd0efd4a058ab"
@retval True for success, False otherwise
'''
def oem_hsm_kdf(in_key_id, in_label, in_context, output_key_id, is_blkdk):
    # send to OEM HSM device/server
    result_from_hsm_server = send_to_hsm_server_kdf(in_key_id, in_label, in_context, output_key_id, is_blkdk)
    return result_from_hsm_server

'''
@brief The routine performs aesgcm operation on the HSM server
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@retval buff_sig for the encrypted buffer
        tag Tag for the tag value
'''
def hsm_server_aes_gcm(plain_text_buf, iv, in_aad, key_id):
    buff_sig, tag = aes_gcm_hsm(key_id, iv, in_aad, plain_text_buf)
    return buff_sig, tag

'''
@brief The routine sends the aesgcm operation request to the HSM server
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] plain_text_buf The input plain text to be encrypted (AES GCM) in binary (byte array) format.
@param[in] in_iv The existing iv
@param[in] in_aad The additional authenticated data (AAD GCM) in binary (byte array) format.

@param[in] key_id The derived key string for this operation
                      Example: "SBK_BCT_DK_43c191bf6d6c3f263a8cd0efd4a058ab"

@retval
       True for success, False otherwise
       encrypted_buf The encrypted data or None
       iv Either it is generated by HSM server in string, or in_iv, or None
       tag The GCM tag, or None
'''
def send_to_hsm_server_aes_gcm(plain_text_buf, in_iv, in_aad, key_id):
    encrypted_buf, tag = hsm_server_aes_gcm(plain_text_buf, in_iv, in_aad, key_id)
    iv = in_iv

    return True, encrypted_buf, iv, tag

'''
@brief The routine requests HSM server to perform the aesgcm operation
@Note: [REQUIRE]
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] plain_text_buf The input plain text to be encrypted (AES GCM) in binary (byte array) format.
@param[in] in_aad The additional authenticated data (AAD GCM) in binary (byte array) format.

@param[in] key_id The derived key string for this operation
                      Example: "SBK_BCT_DK_43c191bf6d6c3f263a8cd0efd4a058ab"
@param[in] rdm_iv The flag to indicate if iv will be randomly generated
                      Default to True so iv will be randomly generated

@param[in] in_iv The existing iv
                      Default to None so it's randomly generated later and returned as iv

@retval
       True for success, False otherwise
       encrypted_buf The encrypted data
       iv Either it is generated by HSM server in string, or in_iv
       tag The GCM tag
'''
def oem_hsm_aes_gcm(plain_text_buf, in_iv, in_aad, key_id):
    result, encrypted_buf, iv, tag = send_to_hsm_server_aes_gcm(plain_text_buf, in_iv, in_aad, key_id)
    return result, encrypted_buf, iv, tag

'''
@brief The routine mimics the hmacsha handling on the HSM server.
@Note: [REFERENCE]
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] plain_text_buf The input plain text to be operated on in binary (byte array) format.

@param[in] key_id The derived key string for this operation
                      Example: "FSKP_AK_43c191bf6d6c3f263a8cd0efd4a058ab"

@retval
       True for success, False otherwise
       hmac_buf The finished data
'''
def send_to_hsm_server_hmacsha(plain_text_buf, key_id):
    # search for a cached/stored derived key in the HSM device/server key database by key_id
    hmac_key = hsm_server_search_key_database(key_id)
    if hmac_key == None: # if key not found:
        return False, None # return error

    temp_label = 'hmac_tempkey'
    try:
        # to connect to SoftHSM
        lib = pkcs11.lib(hsm_lib_path)
        token = lib.get_token(token_label=hsm_token_label)
        # to create a key from hmac_key value for hmacsha usage
        with token.open(user_pin=hsm_user_pin) as session:
            key_content = bytes.fromhex(hmac_key)
            key = session.create_object({
                pkcs11.Attribute.CLASS: pkcs11.constants.ObjectClass.SECRET_KEY,
                pkcs11.Attribute.KEY_TYPE: pkcs11.KeyType.GENERIC_SECRET,
                pkcs11.Attribute.VALUE: key_content,
                pkcs11.Attribute.LABEL: temp_label,
                pkcs11.Attribute.SENSITIVE: True,
                pkcs11.Attribute.EXTRACTABLE: True,
                pkcs11.Attribute.SIGN: True
            })
            hmac_sha_key = session.get_key(label=temp_label, \
                                           key_type=pkcs11.KeyType.GENERIC_SECRET)
            hmac = hmac_sha_key.sign(plain_text_buf, mechanism=pkcs11.Mechanism.SHA256_HMAC)

        return True, hmac

    except Exception as e:
        info_print('[HSM] Error in hmac sha calculation: %s' %(str(e)))
        return False, None

'''
@brief The routine sends the hmacsha request to the HSM server.
@Note: [REQUIRE]
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] plain_text_buf The input plain text to be operated on in binary (byte array) format.

@param[in] key_id The derived key string for this operation
                      Example: "FSKP_AK_43c191bf6d6c3f263a8cd0efd4a058ab"

@retval
       hmac_buf The finished data or throws exception for failure
'''
def oem_hsm_hmacsha(plain_text_buf, key_id):
    result, hmac_buf = send_to_hsm_server_hmacsha(plain_text_buf, key_id)
    if result == False:
        raise tegrasign_exception('[HSM] Please check %s for % as hmac-sha operation did not complete' \
        %(TegraSign_v3_Keystore , key_id))
    return hmac_buf

'''
@brief The routine that checks if a PKC private key is RSA or ECC type and provides specific details
@Note: [REQUIRE]
@Note: This routine is used to determine the specific type of a PKC private key stored in HSM
@param[in] key_id The key label of the key

@retval "RSA" if the key is RSA 3072-bit type
@retval "ECC" if the key is ECDSA P-256 or P-521 curve
@retval None if any other key type or error
'''
def hsm_get_pkc_key_type(key_id = None):
    try:
        # to connect to SoftHSM
        lib = pkcs11.lib(hsm_lib_path)
        token = lib.get_token(token_label=hsm_token_label)
        with token.open(user_pin=hsm_user_pin) as session:
            try:
                # If key_id is not set, use PKC key type
                # This is for t234 compatibility
                if key_id is None:
                    info_print("[HSM] No key label provided, using default PKC key label for OEM HSM")
                    key_label = KeyType.PKC
                else:
                    info_print("[HSM] Key label provided, using key label: %s" % key_id)
                    key_label = key_id
                key = session.get_key(label=key_label,
                                    object_class=pkcs11.ObjectClass.PRIVATE_KEY)
                # Get key type attribute
                key_type = key[pkcs11.Attribute.KEY_TYPE]

                if key_type == pkcs11.KeyType.RSA:
                    key_size = key.key_length
                    if key_size == 3072:
                        return "RSA"
                    # Return None for any other RSA key type
                    return None

                elif key_type == pkcs11.KeyType.EC:
                    # Get curve parameters
                    ec_params = key[pkcs11.Attribute.EC_PARAMS]

                    # verify the supported curve
                    if ec_params not in SUPPORTED_CURVES:
                        info_print('[HSM] Error: Unsupported ECC curve')
                        return None
                    else:
                        # Get the curve information
                        curve_info = SUPPORTED_CURVES[ec_params]
                        key_size = curve_info['key_size']
                        if key_size == 521:
                            return 'ECC521'
                        elif key_size == 256:
                            return 'ECC'
                        else:
                            return None

                else:
                    info_print(f'[HSM] Unknown PKC key type: {key_type}')
                    return None

            except Exception as e:
                info_print(f'[HSM] PKC private key not found: {str(e)}')
                return None

    except Exception as e:
        info_print(f'[HSM] Error connecting to HSM: {str(e)}')
        return None

"""
@brief The routine that parses the DER encoded EC point data
@Note: [REQUIRE]
@Note: This routine is used to parse the DER encoded EC point data

@param[in] ec_point_data: DER encoded EC point data
@retval (raw_point, error_message) tuple
"""
def parse_der_ec_point(ec_point_data):

    if not ec_point_data or len(ec_point_data) < 4:
        return None, "EC point data too short"

    # verify the DER OCTET STRING tag
    if ec_point_data[0] != DER_OCTET_STRING_TAG:
        return None, "Invalid DER OCTET STRING tag"

    # parse the length field
    length_byte = ec_point_data[1]
    offset = 2

    if length_byte & 0x80:  # long format length
        length_octets = length_byte & 0x7F
        if length_octets > 4 or offset + length_octets >= len(ec_point_data):
            return None, "Invalid DER length encoding"

        content_length = 0
        for i in range(length_octets):
            content_length = (content_length << 8) | ec_point_data[offset]
            offset += 1
    else:  # short format length
        content_length = length_byte

    # verify the content length
    if offset + content_length > len(ec_point_data):
        return None, "DER content length exceeds data size"

    raw_point = ec_point_data[offset:offset + content_length]

    # verify the uncompressed point format
    if len(raw_point) < 1 or raw_point[0] != 0x04:
        return None, "EC point not in uncompressed format"

    return raw_point, None


'''
@brief The routine that specifically identifies the ECC curve type for a PKC key and can extract public key
@Note: [REQUIRE]
@Note: This routine checks the curve parameters of an ECC key and returns numeric values
       If pub_key_file is provided, it will extract the ECC public key in OpenSSL format

@param[in] p_key SignKey class which has info needed: filename, and keysize is updated
@param[in] pub_key_file File path to save extracted ECC public key in OpenSSL format
@retval True if the key is ECDSA P-521 or P-256 curve
@retval False if any other curve type or error
'''
def get_ecc_pub_hsm(p_key, pub_key_file):
    # input validation
    if pub_key_file is None:
        info_print('[HSM] Security: Invalid file path provided')
        return False

    try:
        # to connect to SoftHSM
        lib = pkcs11.lib(hsm_lib_path)
        token = lib.get_token(token_label=hsm_token_label)

        with token.open(user_pin=hsm_user_pin) as session:
            try:
                key_label = p_key.hsm.l4t_key_label
                private_key = session.get_key(label=key_label,
                                            object_class=pkcs11.ObjectClass.PRIVATE_KEY)

                # verify the key type
                key_type = private_key[pkcs11.Attribute.KEY_TYPE]
                if key_type != pkcs11.KeyType.EC:
                    info_print('[HSM] Error: Key is not an ECC key')
                    return False

                # get the curve parameters
                ec_params = private_key[pkcs11.Attribute.EC_PARAMS]

                # verify the supported curve
                if ec_params not in SUPPORTED_CURVES:
                    info_print('[HSM] Error: Unsupported ECC curve')
                    return False

                curve_info = SUPPORTED_CURVES[ec_params]
                key_size = curve_info['key_size']
                allocated_key_size = curve_info['allocated_size']
                expected_coord_len = curve_info['coord_length']
                curve_name = curve_info['name']

                # extract the public key
                try:
                    key_label = p_key.hsm.l4t_key_label
                    pub_key = session.get_key(label=key_label,
                                            object_class=pkcs11.ObjectClass.PUBLIC_KEY)

                    # get the EC point data
                    ec_point_data = pub_key[pkcs11.Attribute.EC_POINT]

                    # parse the DER encoded EC point data
                    raw_point, error_msg = parse_der_ec_point(ec_point_data)
                    if raw_point is None:
                        info_print(f'[HSM] Error parsing EC point: {error_msg}')
                        return False

                    # extract the coordinates (skip the format indicator 0x04)
                    coord_data = raw_point[1:]
                    expected_total_len = expected_coord_len * 2

                    # boundary check
                    if len(coord_data) != expected_total_len:
                        info_print(f'[HSM] Error: Invalid coordinate length. Expected {expected_total_len}, got {len(coord_data)}')
                        return False

                    # extract the x and y coordinates
                    x_coord = coord_data[:expected_coord_len]
                    y_coord = coord_data[expected_coord_len:]

                    # create the destination buffer and perform a secure copy
                    dest_pub_bytes = bytearray(allocated_key_size * 2)

                    # reverse the byte order to match the NVIDIA format
                    r_x_coord = bytearray(x_coord)
                    r_y_coord = bytearray(y_coord)
                    r_x_coord.reverse()
                    r_y_coord.reverse()

                    # secure copy the coordinates to the destination buffer (zero padding)
                    if len(r_x_coord) <= allocated_key_size:
                        dest_pub_bytes[0:len(r_x_coord)] = r_x_coord
                    else:
                        info_print('[HSM] Error: X coordinate too large for buffer')
                        return False

                    if len(r_y_coord) <= allocated_key_size:
                        dest_pub_bytes[allocated_key_size:allocated_key_size+len(r_y_coord)] = r_y_coord
                    else:
                        info_print('[HSM] Error: Y coordinate too large for buffer')
                        return False

                    # write the file
                    try:
                        with open(pub_key_file, 'wb') as fw:
                            fw.write(dest_pub_bytes)
                    except IOError as e:
                        info_print(f'[HSM] Error writing public key file: {str(e)}')
                        return False

                    if key_size == 521:
                        p_key.keysize = NV_ECC521_SIG_STRUCT_SIZE
                    if key_size == 256:
                        p_key.keysize = NV_ECC_SIG_STRUCT_SIZE
                    return True

                except Exception as e:
                    info_print(f'[HSM] Error extracting ECC public key: {str(e)}')
                    return False

            except Exception as e:
                info_print(f'[HSM] PKC private key not found: {str(e)}')
                return False

    except Exception as e:
        info_print(f'[HSM] Error connecting to HSM: {str(e)}')
        return False

'''
@brief The routine that invokes ECDSA signing on the buffer using HSM

@param[in] buf Buffer to be operated on
@param[in] p_key SignKey class which has info needed: filename, sha mode

@retval sig_data The buffer after operation
'''
def do_ecc_hsm(buf, p_key):
    if (p_key.key.pkckey.Sha == Sha._256):
        sha_mechanism = pkcs11.Mechanism.SHA256
    else:
        sha_mechanism = pkcs11.Mechanism.SHA512

    try:
        # to connect to SoftHSM
        lib = pkcs11.lib(hsm_lib_path)
        token = lib.get_token(token_label=hsm_token_label)
        with token.open(user_pin=hsm_user_pin) as session:
            # Calculate hash of the data first
            hash_data = session.digest(buf, mechanism=sha_mechanism)

            # Get the ECC private key from HSM
            key_label = p_key.hsm.l4t_key_label
            private_key = session.get_key(label=key_label,
                                          key_type=pkcs11.KeyType.EC,
                                          object_class=pkcs11.ObjectClass.PRIVATE_KEY)

            # Get curve parameters to determine key size
            ec_params = private_key[pkcs11.Attribute.EC_PARAMS]

            # verify the supported curve
            if ec_params not in SUPPORTED_CURVES:
                info_print('[HSM] Error: Unsupported ECC curve')
                return 0

            # get the curve information
            curve_info = SUPPORTED_CURVES[ec_params]
            allocated_key_size = curve_info['allocated_size']

            # sign the hash with ECDSA
            signature = private_key.sign(hash_data, mechanism=pkcs11.Mechanism.ECDSA)

            # check the signature length and split it into two parts
            sig_len = len(signature)
            half_len = sig_len // 2

            # split the signature into two parts (r and s values)
            r_bytes = signature[:half_len]
            s_bytes = signature[half_len:]

            # convert to integer
            r = int.from_bytes(r_bytes, byteorder='big')
            s = int.from_bytes(s_bytes, byteorder='big')

            # Convert r and s to bytes in big-endian format
            r_bytes = r.to_bytes(allocated_key_size, byteorder='big')
            s_bytes = s.to_bytes(allocated_key_size, byteorder='big')

            # Create buffers for r and s
            pr = bytearray(r_bytes)
            ps = bytearray(s_bytes)

            # Swap endianness (big-endian to little-endian)
            pr.reverse()
            ps.reverse()

            # Create the final signature buffer
            sig_size = allocated_key_size * 2
            sig_data = bytearray(sig_size)

            # Copy the r value to the first half
            sig_data[0:allocated_key_size] = pr

            # Copy the s value to the second half
            sig_data[allocated_key_size:sig_size] = ps

            return sig_data

    except Exception as e:
        info_print(f'[HSM] Error in ECDSA signing: {str(e)}')
        return None
