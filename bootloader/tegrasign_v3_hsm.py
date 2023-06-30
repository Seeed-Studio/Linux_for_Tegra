#
# Copyright (c) 2018-2022, NVIDIA Corporation.  All Rights Reserved.
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA Corporation is strictly prohibited.
#
from tegrasign_v3_util import *
import hashlib

# This is a configuration file that defines NV debug keys
NV_DEBUG_YAML = 'tegrasign_v3_debug.yaml'

'''
@brief The routine that maps the key file to key type
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] p_key SignKey class which has info needed: filename, type
@Note: There are three ways to mimic HSM code path:
1) use tegrasign_v3_debug.yaml, which is NV approach to mimic HSM behavior.
   The file format is of the following:
    {"HSM":
        {
            "SBK_KEY"     : "/media/automotive/sbk_hsm.key",
            "KEK0_KEY"    : "/media/automotive/kek0_hsm.key",
            "FSKP_AK_KEY" : "/media/automotive/fskp_ak_hsm.key",
            "FSKP_EK_KEY" : "/media/automotive/fskp_ek_hsm.key",
            "FSKP_KDK_KEY": "/media/automotive/fskp_kdk_hsm.key",
            "FSKP_KEY"    : "/media/automotive/fskp_hsm.key",
            "PKC_KEY"     : "/media/automotive/pkc_hsm.key,
            "ED25519_KEY" : "/media/automotive/ed25519_hsm.key"
        }
    }
2) specify key path via --key <file_name>
3) ovewrite p_key.filename = p_key.filename in the following routine
'''
def get_key_file_hsm(p_key):
    if (p_key.hsm.is_algo_only()):
        info_print('[HSM] Using defined key: ' + p_key.filename)
        return p_key.filename

    key_type = p_key.hsm.get_type()
    info_print('[HSM] key type ' + key_type)

    # First check if the file is 'None', if so no need to modify
    if (p_key.filename == 'None'):
        info_print('[HSM] Loading zero sbk key ')
        return p_key.filename

    # Next check if NV debug file is present
    yaml_path = search_file(NV_DEBUG_YAML)
    if os.path.isfile(yaml_path):
        try:
            info_print('[HSM] Found ' + yaml_path)
            import yaml
            with open(yaml_path) as f:
                params = yaml.safe_load(f)
                if (key_type == KeyType.SBK):
                    p_key.filename = params['HSM']['SBK_KEY']
                elif (key_type == KeyType.KEK0):
                    p_key.filename = params['HSM']['KEK0_KEY']
                elif (key_type == KeyType.FSKP_AK):
                    p_key.filename = params['HSM']['FSKP_AK_KEY']
                elif (key_type == KeyType.FSKP_EK):
                    p_key.filename = params['HSM']['FSKP_EK_KEY']
                elif (key_type == KeyType.FSKP_KDK):
                    p_key.filename = params['HSM']['FSKP_KDK_KEY']
                elif (key_type == KeyType.FSKP):
                    p_key.filename = params['HSM']['FSKP_KEY']
                elif (key_type == KeyType.PKC):
                    p_key.filename = params['HSM']['PKC_KEY']
                elif (key_type == KeyType.ED25519):
                    p_key.filename = params['HSM']['ED25519_KEY']
            info_print('[HSM] Loading NVIDIA debug key: ' + str(p_key.filename))
        except Exception as e:
            raise tegrasign_exception('Please check file content for ' + key_type + ' define in ' + NV_DEBUG_YAML)
    else:
        if (key_type == KeyType.SBK):
            p_key.filename = p_key.filename
        elif (key_type == KeyType.KEK0):
            p_key.filename = p_key.filename
        elif (key_type == KeyType.FSKP_AK):
            p_key.filename = p_key.filename
        elif (key_type == KeyType.FSKP_EK):
            p_key.filename = p_key.filename
        elif (key_type == KeyType.FSKP_KDK):
            p_key.filename = p_key.filename
        elif (key_type == KeyType.FSKP):
            p_key.filename = p_key.filename
        elif (key_type == KeyType.PKC):
            p_key.filename = p_key.filename
        elif (key_type == KeyType.ED25519):
            p_key.filename = p_key.filename
        info_print('[HSM] Loading HSM key: ' + str(p_key.filename))
    if (p_key.filename == None):
        raise tegrasign_exception('[HSM] ERROR: ' + key_type
            + ' does not have key path specified. Please either specify --key <filename>, or define in get_key_file_hsm(), or in '
            +  NV_DEBUG_YAML)

'''
@brief The routine that reads the sbk/kek0/fskp key content
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] key_file The file to be read

@retval key The buffer that is read in string format
'''
def get_sbk_key_content(p_key):
    key_file = p_key.filename
    key_type = p_key.hsm.get_type()
    if key_file == 'None':
        return hex_to_str(p_key.key.aeskey)
    else:
        with open_file(key_file, 'rb') as f:
            key_ = f.read()
            if key_[:2] == b'0x':
                # The key below is just concatenation of hex literals in the key file
                # key format is printable 0x123456578 0x9abcdef0 ...
                key = key_.decode().strip().replace('0x', '').replace(' ', '')
            else:
                try:
                    key_dec = key_.decode().strip()

                    if (len(key_dec) == 32) or (len(key_dec) == 64):
                        # assume key format is ascii
                        key = key_dec
                    else:
                        # key format is in a binary sequence
                        key = binascii.hexlify(key_).decode('ascii')
                except UnicodeDecodeError:
                    # key format is in a binary sequence
                    key = binascii.hexlify(key_).decode('ascii')
            return key
    raise tegrasign_exception("[HSM] ERROR: can not extract key content for %s" % (key_file))

'''
@brief The routine that invokes hmacsha on the buffer
@Note: This routine is expected to be replaced by OEM's own HSM implementation

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
    tmpf_in = 'tmp_hmacsha.in'
    tmpf_out = 'tmp_hmacsha.mac'

    with open_file(tmpf_in, 'wb') as f:
        write_file(f, buf)

    if (use_der_key == True):
        key = hex_to_str(p_key.key.aeskey)
    else:
        key_type = p_key.hsm.get_type()
        get_key_file_hsm(p_key)
        key = get_sbk_key_content(p_key)

    runcmd = 'openssl dgst -sha256 -mac hmac -macopt hexkey:%s -binary -out %s %s' % (key, tmpf_out, tmpf_in)
    info_print('[HSM] calling %s' % runcmd)
    try:
        subprocess.check_call(runcmd, shell=True)
    except subprocess.CalledProcessError:
        info_print("[HSM] ERROR: failure in running %s" % runcmd)
        exit_routine()
    finally:
        os.remove(tmpf_in)

    with open_file(tmpf_out, 'rb') as f:
        hmac = f.read()

    os.remove(tmpf_out)

    info_print('[HSM] hmacsha256 is done... return')

    return hmac

'''
@brief The routine that invokes random string generation
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] p_key SignKey class which has info needed: ran.size, ran.count
@note
   size = byte size of the random string
   count = number of random strings to be generated

@param[out] p_key.ran.buf This holds the random hex arrays of ran.size length by ran.count:

@retval None
'''

def do_random_hsm(p_key):
    info_print('[HSM] Generating random strings: %d x %d ' %(p_key.ran.size, p_key.ran.count))
    p_key.ran.buf = bytearray(p_key.ran.size * p_key.ran.count)

    for i in range(p_key.ran.count):
        buf = random_gen(p_key.ran.size)
        start = i * p_key.ran.size
        p_key.ran.buf[start:start+p_key.ran.size] = buf[:]

    info_print('[HSM] Generated random strings: %s ' %(hex_to_str(p_key.ran.buf)))

'''
@brief The routine that invokes aes-gcm on the buffer
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] buf Buffer to be operated on
@param[in] p_key SignKey class which has info needed: filename, mode, iv, aad, tag, verify
@note
   iv field is expected to be the random value
   tag field should be filled with the generated value
   verify  This is for verifying.
           If set to 0 means to encrypt, if set to a filename means to decrypt, where the buf
           is the encrypted buffer, and the filename is the original source

@param[in] use_der_key Boolean flag indicating if reading the key from the file path defined for HSM,
           or use the key value from SignKey
           The true flag indicates taking the key value from the SignKey as this value is previously
           derived from an operation
           The false flag indicates reading the key from the file path defined for HSM operation

@retval buf_enc The buffer after operation
'''
def do_aes_gcm_hsm(buf, p_key, use_der_key = False):
    if (use_der_key == True):
        key_str = hex_to_str(p_key.key.aeskey)
    else:
        key_type = p_key.hsm.get_type()
        get_key_file_hsm(p_key)
        key_str = get_sbk_key_content(p_key)

    iv  = p_key.kdf.iv.get_hexbuf()
    aad  = p_key.kdf.aad.get_hexbuf()
    tag  = p_key.kdf.tag.get_hexbuf()

    if (type(p_key.kdf.verify) == int):
        verify_bytes = p_key.kdf.verify
    else:
        verify_bytes = len(p_key.kdf.verify) + 1

    base_name = script_dir + 'v3_gcm_' + pid
    raw_name = base_name + '.raw'
    result_name = base_name + '.out'
    # check tag and verify_file are both defined
    if (type(tag) == int and type(p_key.kdf.verify) == str):
        raise tegrasign_exception('--tag and --verify must both be specified')

    raw_file = open_file(raw_name, 'wb')

    key_bytes = len(key_str)/2
    keysize_bytes = int_2byte_cnt(key_bytes)
    len_bytes = int_2byte_cnt(p_key.len)
    enc_bytes = len(buf)
    dest_bytes = int(p_key.len)
    result_bytes = len(result_name) + 1
    if (type(iv) == type(None)):
        iv_bytes = 0
    else:
        iv_bytes = len(binascii.hexlify(iv))/2
    if (type(aad) == type(None)):
        aad_bytes = 0
    else:
        aad_bytes = len(binascii.hexlify(aad))/2

    if (type(tag) == type(None)):
        tag_bytes = 0
    else:
        tag_bytes = len(binascii.hexlify(tag))/2

    buff_dest = "0" * dest_bytes

    # to write to file in the following order:
    # sizes for: key, keysize, length, buf, buff_dest, result_name, iv, aad, tag, verify,
    # data of: key, key size, length, buffer, buff_dest, result_name, iv, add, tag, verify
    # Note: verify, if non-zero in length, is the original file to be verified against,
    #       so buf will be the encrypted content

    num_list = [key_bytes, keysize_bytes, len_bytes, enc_bytes, dest_bytes, result_bytes, iv_bytes, aad_bytes, tag_bytes, verify_bytes]
    for num in num_list:
        arr = int_2bytes(4, num)
        write_file(raw_file, arr)

    write_file(raw_file, str_to_hex(key_str))
    arr = int_2bytes(keysize_bytes, key_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(len_bytes, p_key.len)
    write_file(raw_file, arr)
    write_file(raw_file, bytes(buf))
    write_file(raw_file, buff_dest.encode("utf-8"))
    write_file(raw_file, result_name.encode("utf-8"))
    nullarr = bytearray(1)
    nullarr[0] = 0          # need this null for char*
    write_file(raw_file, nullarr)
    if iv_bytes > 0:
        write_file(raw_file, iv)
    if aad_bytes > 0:
        write_file(raw_file, aad)
    if tag_bytes > 0:
        write_file(raw_file, tag)
    if verify_bytes > 0:
        write_file(raw_file, p_key.kdf.verify.encode("utf-8"))
        nullarr = bytearray(1)
        nullarr[0] = 0          # need this null for char*
    raw_file.close()

    command = exec_file(TegraOpenssl)
    command.extend(['--aesgcm', raw_name])
    command.extend(['--verbose'])

    ret_str = run_command(command)
    if (isinstance(tag, int) == False) and (type(p_key.kdf.verify) == str):
        info_print ('********* Verification complete. Quitting. *********')
        sys.exit(1)

    else:
        if check_file(result_name):
            result_fh = open_file(result_name, 'rb')
            buff_sig = result_fh.read() # Return data to caller
            result_fh.close()
            os.remove(result_name)
        start = ret_str.find('tag')
        tag_str_len = 4
        if (start > 0):
            if tag_bytes > 0:
                end = start + tag_str_len + int(tag_bytes * 2)
            else:
                end = len(ret_str)
            p_key.kdf.tag.set_buf(str_to_hex(ret_str[start+tag_str_len:end]))
    os.remove(raw_name)
    return buff_sig

'''
@brief The routine that invokes rsa-pss on the buffer
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] buf Buffer to be operated on
@param[in] p_key SignKey class which has info needed: filename, sha mode

@retval sig_data The buffer after operation
'''
def do_rsa_pss_hsm(buf, p_key):

    sha_str = 'sha256' if (p_key.key.pkckey.Sha ==  Sha._256) else 'sha512'
    tmpf_in = 'tmp_rsa_pss.in'
    tmpf_out = 'tmp_rsa_pss.sig'
    tmpf_hash = 'tmp_%s.hash' % (sha_str)

    get_key_file_hsm(p_key)
    priv_keyf = p_key.filename

    with open_file(tmpf_in, 'wb') as f:
        write_file(f, buf)

    # rsa_pss_saltlen:-1 means the same length of hash (sha256|sha512) here
    # single line execution for sha256:
    # runcmd = "openssl dgst -sha256 -sigopt rsa_padding_mode:pss -sigopt rsa_pss_saltlen:-1 -sign %s -out %s %s" % (priv_keyf, tmpf_out, tmpf_in)

    # two separate line execution with intermediate sha256|sha512 output
    runcmd1 = "openssl dgst -%s -binary -out %s %s" % (sha_str, tmpf_hash, tmpf_in)
    runcmd2 = "openssl pkeyutl -sign -pkeyopt rsa_padding_mode:pss -pkeyopt rsa_pss_saltlen:-1 -pkeyopt digest:%s -in %s -out %s -inkey %s" % (sha_str, tmpf_hash, tmpf_out, priv_keyf)
    info_print('[HSM] calling %s\n[HSM] %s' % (runcmd1, runcmd2))
    try:
        subprocess.check_call(runcmd1, shell=True)
        subprocess.check_call(runcmd2, shell=True)
    except subprocess.CalledProcessError:
        print("[HSM] ERROR: failure in running %s, %s" % (runcmd1, runcmd2))
        exit_routine()
    finally:
        os.remove(tmpf_in)

    with open_file(tmpf_out, 'rb') as f:
        sig_data = swapbytes(bytearray(f.read()))

    os.remove(tmpf_hash)
    os.remove(tmpf_out)

    info_print('[HSM] rsa-pss routine is done... return')

    return sig_data

'''
@brief The routine that invokes ed25519 on the buffer
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] buf Buffer to be operated on
@param[in] p_key SignKey class which has info needed: filename

@retval sig_data The buffer after operation
'''
def do_ed25519_hsm(buf, p_key):

    buff_sig = "0" * p_key.keysize
    length = len(buf)

    current_dir_path = os.path.dirname(os.path.realpath(__file__))
    raw_name = current_dir_path + '/ed_raw.bin'
    result_name = current_dir_path + '/ed_out.bin'
    raw_file = open_file(raw_name, 'wb')

    get_key_file_hsm(p_key)
    filename_bytes = len(p_key.filename) + 1 # to account for 0x0
    len_bytes = int_2byte_cnt(length)
    sign_bytes = len(buf)
    sig_bytes = len(buff_sig)
    pkh_bytes = 0
    result_bytes = len(result_name) + 1

    # order: sizes then data for: file name, length, buff_to_sign, buff_sig, pkhfile, result_name
    arr = int_2bytes(4, filename_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, len_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, sign_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, sig_bytes)
    write_file(raw_file, arr)

    arr = int_2bytes(4, pkh_bytes)
    write_file(raw_file, arr)
    arr = int_2bytes(4, result_bytes)
    write_file(raw_file, arr)

    write_file(raw_file, bytes(p_key.filename.encode("utf-8")))
    nullarr = bytearray(1)
    nullarr[0] = 0          # need this null for char*
    write_file(raw_file, nullarr)

    arr = int_2bytes(len_bytes, length)
    write_file(raw_file, arr)

    write_file(raw_file, buf)
    write_file(raw_file, bytes(buff_sig.encode("utf-8")))

    if (pkh_bytes > 0):
        write_file(raw_file, bytes(pkhfile.encode("utf-8")))
        write_file(raw_file, nullarr)

    write_file(raw_file, bytes(result_name.encode("utf-8")))
    write_file(raw_file, nullarr)

    raw_file.close()

    command = exec_file(TegraOpenssl)
    command.extend(['--ed25519', raw_name])

    ret_str = run_command(command)

    if check_file(result_name):
        result_fh = open_file(result_name, 'rb')
        buff_sig = result_fh.read()
        result_fh.close()
        os.remove(result_name)

    os.remove(raw_name)
    return buff_sig

'''
@brief The routine that generates the public key for the RSA key passed in
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] p_key SignKey class which has info needed: filename, and keysize is updated
@param[in] pub_modf RSA public key filename

@retval True for success, False otherwise
'''
def get_rsa_mod_hsm(p_key, pub_modf=None):

    get_key_file_hsm(p_key)
    runcmd = 'openssl rsa -in %s -modulus -noout' % (p_key.filename)
    info_print('[HSM] calling %s' % runcmd)
    try:
        output = subprocess.check_output(runcmd, shell=True).decode("utf-8")
    except subprocess.CalledProcessError:
        info_print("[HSM] ERROR: failure in running %s" % runcmd)
        info_print('[HSM] Done - get_rsa_modulus_hsm. Key is not RSA key')
        return False
    # Check if the output is 'Modulus=963E...'
    if not output.startswith('Modulus='):
        info_print('[HSM] Done - get_rsa_modulus_hsm. Key is not RSA key')
        return False

    rsa_n_bin = swapbytes(bytearray(binascii.unhexlify(output.strip()[len('Modulus='):])))
    p_key.keysize = len(rsa_n_bin)

    success = (p_key.keysize != 0) # Assuming modulus has valid input
    if pub_modf:
        with open_file(pub_modf, 'wb') as f:
            write_file(f, rsa_n_bin)
    info_print('[HSM] Done - get_rsa_modulus_hsm. Key is' + (' RSA key ' if (success == True) else ' not RSA key'))

    return success

'''
@brief The routine that generates the Montgomery values from the RSA key passed in
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] p_key SignKey class which has info needed: filename, and keysize is updated
@param[in] pub_montf RSA Montgomery filename

@retval True for success, False otherwise
'''
def get_rsa_mont_hsm(p_key, pub_montf):

    get_key_file_hsm(p_key)
    pub_modf = p_key.filename + '_public'

    command = exec_file(TegraOpenssl)
    command.extend(['--isPkcKey', p_key.filename, pub_modf, pub_montf])
    ret_str = run_command(command)

    os.remove(pub_modf)
    success = False

    # scan the return string for decimal value
    m = re.search('Key size is (\d+)', ret_str)
    if m:
        keysize = int(m.group(1))
        if (keysize > 0 ) and (keysize < NV_RSA_MAX_KEY_SIZE):
            success = True
    info_print('[HSM] Done - get_rsa_mont_hsm. Montgomery values ' + (' successful ' if (success == True) else ' failed'))

    return success

'''
@brief The routine that generates the public key for the ED25519 key passed in
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] p_key SignKey class which has info needed: filename, and keysize is updated
@param[in] pub_keyf ED25519 public key filename

@retval True for success, False otherwise
'''
def get_ed25519_pub_hsm(p_key, pub_keyf):

    command = exec_file(TegraOpenssl)

    if pub_keyf == None:
        command.extend(['--isEd25519Key', p_key.filename])
    else:
        command.extend(['--isEd25519Key', p_key.filename, pub_keyf])

    success = False
    ret_str = run_command(command)
    if is_ret_ok(ret_str):
        p_key.keysize = ED25519_SIG_SIZE
        success = True

    info_print('[HSM] Done - get_ed25519_pub_hsm. Key is' + (' ED25519 key ' if (success == True) else ' not ED25519 key'))

    return success

'''
@brief The routine that performs key derivation via NIST SP800-108
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] p_key SignKey class which has info needed: label, context

@param[in] use_der_key Boolean flag indicating if reading the key from the file path defined for HSM,
           or use the key value from SignKey
           The true flag indicates taking the key value from the SignKey as this value is previously
           derived from an operation
           The false flag indicates reading the key from the file path defined for HSM operation

@retval buffer The buffer after hmac-sha256 operation
'''
def do_kdf_kdf2_hsm(p_key, use_der_key = False):
    if (use_der_key == True):
        kdk = hex_to_str(p_key.key.aeskey)
    else:
        get_key_file_hsm(p_key)
        kdk = get_sbk_key_content(p_key)

    info_print('[HSM] Derive kdf using ' + kdk)
    kdd = None
    label = p_key.kdf.label.get_strbuf()
    context =  p_key.kdf.context.get_strbuf()
    HexLabel = True
    HexContext = True
    msgStr = get_composed_msg(label,context, 256, HexLabel, HexContext)

    backup = p_key
    if kdd == None:
        backup.key.aeskey = str_to_hex(kdk)
    else:
        backup.key.aeskey = str_to_hex(kdk+kdd)

    info_print('[HSM] Derived key is ' + hex_to_str(backup.key.aeskey))
    backup.keysize = len(backup.key.aeskey)
    msg = str_to_hex(msgStr)
    return do_hmac_sha256_hsm(msg, backup)

'''
@brief The routine that performs key derivation then hmacsha on the file
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] buf Buffer to be operated on
@param[in] p_key SignKey class which has info needed: filename, keytype, nist encoded data, iv

@retval buffer The buffer after hmac-sha256 operation
'''
def do_derive_hmacsha_hsm(buf, p_key):
    key = do_kdf_kdf2_hsm(p_key)
    backup = p_key
    backup.key.aeskey = key
    info_print('[HSM] Perform hmacsha with key is ' + hex_to_str(key))

    use_derivation_key = True
    return do_hmac_sha256_hsm(p_key.get_sign_buf(), backup, use_derivation_key)

'''
@brief The routine that performs one key derivation step then aesgcm on the file
@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] buf Buffer to be operated on
@param[in] p_key SignKey class which has info needed: filename, keytype, nist encoded data, iv

@retval buffer The buffer after aes-gcm operation
'''
def do_derive_aesgcm_hsm(buf, p_key):

    key = do_kdf_kdf2_hsm(p_key)

    backup = p_key
    backup.key.aeskey = key

    info_print('[HSM] Perform aesgcm with key is ' + hex_to_str(key))
    use_derivation_key = True
    return do_aes_gcm_hsm(buf, backup, use_derivation_key)

'''
@brief The routine that performs multiple steps of key derivation then aesgcm on the file.
If block_size is defined, then the given buffer will be encrypted multiple times to result
in the metablob generation

@Note: This routine is expected to be replaced by OEM's own HSM implementation

@param[in] params_slist List array of following items to be used:
              DecKdkKdkKdd: product of counter mode(dec kdk + dec kdkd), to be ran with hmacsha for KDF
              DecKdkIv : iv for dec kdk
              DecKdkAad: aad for dec kdk
              Plaintext: plaintext for sbk, to be swapped out by path defined get_key_file_hsm()
              DecKdkMsg: product of counter mode(dec kdk label), to be ran with hmacsha for KDF
              BlKdkMsg : product of counter mode(bl kdk label), to be ran with hmacsha for KDF
              TzKdkMsg : product of counter mode(tz kdk label), to be ran with hmacsha for KDF
              GpKdkMsg : product of counter mode(gp kdk label), to be ran with hmacsha for KDF
              GptoKdkMsg:product of counter mode(gptos kdk label), to be ran with hmacsha for KDF
              KdkMsg   : product of counter mode(kdk label), to be ran with hmacsha for KDF
              DkMsg    : product of counter mode(dk label), to be ran with hmacsha for KDF

@param[out] p_key SignKey class which has info needed::
              iv : iv used for aesgcm on the --filename buffer
              aad: aad used for aesgcm on the --filename buffer
              block_size: size defined in bytes for meta blob generation
                          If this is none 0, then src_buffer is encrypted with random iv in block_size
              tag[out]: tag produced from aesgcm operation
              flag: flag to indicate if SBK plainttext or SBK wrapped key is used
              src buffer[out]: buffer to be encrypted with DK key

@retval True for success, False otherwise
'''
def do_kdf_oem_hsm(params_slist, p_key):
    info_print('[HSM] Perform key derivation and encryption')
    DecKdkKdkKdd = params_slist[0]
    DecKdkIv = str_to_hex(params_slist[1])
    DecKdkAad = str_to_hex(params_slist[2])
    Plaintext = str_to_hex(params_slist[3])
    DecKdkMsg = str_to_hex(params_slist[4])
    BlKdkMsg = params_slist[5]
    TzKdkMsg = params_slist[6]
    GpKdkMsg = params_slist[7]
    GptoKdkMsg = params_slist[8]
    KdkMsg = str_to_hex(params_slist[9])
    DkMsg = str_to_hex(params_slist[10])
    use_derivation_key = True

    get_key_file_hsm(p_key)
    Plaintext = str_to_hex(get_sbk_key_content(p_key))

    p_key.key.aeskey = str_to_hex(DecKdkKdkKdd)
    p_key.key.aeskey = do_hmac_sha256_hsm(DecKdkMsg, p_key, use_derivation_key)
    p_key.len = len(p_key.key.aeskey)

    # If SBK_WRAP is on, use plaintext as sbk, else derive dec_kdk as following:
    #    1) dec_kdk input = hash = kdf(kdk+kdd, deckdk_msg)
    #    2) dec_kdk key = aes-gcm, stored as DecKdkKey

    if p_key.kdf.flag == DerKey.SBK_WRAP:
        p_key.key.aeskey = do_aes_gcm_hsm(Plaintext, p_key, use_derivation_key)
        p_key.len = len(p_key.key.aeskey)
    else:
        p_key.key.aeskey = Plaintext
        p_key.len = len(p_key.key.aeskey)
    info_print('[HSM] SBK is ' + hex_to_str(p_key.key.aeskey))

    # If BlKdkMsg is defined, need to derive BL kdk
    if (BlKdkMsg != None):
        p_key.key.aeskey = do_hmac_sha256_hsm(str_to_hex(BlKdkMsg), p_key, use_derivation_key)
        info_print('[HSM] SBK_BL_KDK is ' + hex_to_str(p_key.key.aeskey))

    # If TzKdkMsg is defined, need to derive TZ kdk
    if (TzKdkMsg != None):
        p_key.key.aeskey = do_hmac_sha256_hsm(str_to_hex(TzKdkMsg), p_key, use_derivation_key)
        info_print('[HSM] SBK_TZ_KDK is ' + hex_to_str(p_key.key.aeskey))

    # If GpKdkMsg is defined, need to derive GP kdk
    if (GpKdkMsg != None):
        p_key.key.aeskey = do_hmac_sha256_hsm(str_to_hex(GpKdkMsg), p_key, use_derivation_key)
        info_print('[HSM] SBK_GP_KDK is ' + hex_to_str(p_key.key.aeskey))

    # If GptoKdkMsg is defined, need to derive GP_TOS kdk
    if (GptoKdkMsg != None):
        p_key.key.aeskey = do_hmac_sha256_hsm(str_to_hex(GptoKdkMsg), p_key, use_derivation_key)
        info_print('[HSM] SBK_GPTOS_KDK is ' + hex_to_str(p_key.key.aeskey))

    # Pass in DecKdkKey to derive KDK, then DK, and use DK to encrypt buffer via aes-gcm
    p_key.key.aeskey = do_hmac_sha256_hsm(KdkMsg, p_key, use_derivation_key)
    info_print('[HSM] SBK_*_KDK is ' + hex_to_str(p_key.key.aeskey))

    p_key.key.aeskey = do_hmac_sha256_hsm(DkMsg, p_key, use_derivation_key)
    info_print('[HSM] SBK_*_DK is ' + hex_to_str(p_key.key.aeskey))

    p_key.len = len(p_key.src_buf)
    if p_key.block_size == 0:
        p_key.src_buf = do_aes_gcm_hsm(p_key.src_buf, p_key, use_derivation_key)
        return True

    # Use block_size to do metablob encryption
    iv_off = 0
    iv_size = 12
    tag_off = 12
    tag_size = 16
    dgt_off = 28
    dgt_size = 64
    blob_sz = iv_size + tag_size + dgt_size

    last_blk_len = p_key.len % p_key.block_size
    blk_cnt = int(p_key.len / p_key.block_size) + (1 if (last_blk_len != 0) else 0)
    blob_alloc_sz = blob_sz * (blk_cnt + 1)
    blob_buf = bytearray(blob_alloc_sz)

    # Get ramdom strings
    p_key.ran.size = iv_size
    p_key.ran.count = blk_cnt - 1 # Use p_key's IV for the first itereation
    p_key.Sha = Sha._512
    do_random_hsm(p_key)

    # write blk_cnt to the first blob
    blob_buf[0:4] = int_2bytes(4, blk_cnt)

    for i in range(blk_cnt):
        if i+1 == blk_cnt:
            p_key.len = last_blk_len
        else:
            p_key.len = p_key.block_size

        start = i * p_key.block_size
        end = start + p_key.len

        p_key.src_buf[start:end] = do_aes_gcm_hsm(p_key.src_buf[start:end], p_key, use_derivation_key)
        buff = bytearray(hashlib.sha512(p_key.src_buf[start:end]).digest())

        # Start writing to the 2nd blob b/c first blob has the blk_cnt
        start = (i+1) * blob_sz
        end = start + iv_size
        blob_buf[start+iv_off:start+iv_off+iv_size] = p_key.kdf.iv.get_hexbuf()
        blob_buf[start+tag_off:start+tag_off+tag_size] = p_key.kdf.tag.get_hexbuf()
        blob_buf[start+dgt_off:start+dgt_off+dgt_size] = buff[:]

        # Get iv for the next itereation
        start = i * iv_size
        end = start + iv_size
        p_key.kdf.iv.set_buf(p_key.ran.buf[start:end])

    # Save blob to the tag field
    p_key.kdf.tag.set_buf(blob_buf)
    return True
