#!/usr/bin/python
# SPDX-FileCopyrightText: Copyright (c) 2021-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
# property and proprietary rights in and to this material, related
# documentation and any modifications thereto. Any use, reproduction,
# disclosure or distribution of this material and related documentation
# without an express license agreement from NVIDIA CORPORATION or
# its affiliates is strictly prohibited.
#

import os
import sys
import time
import uuid
import json
import shutil
import struct
import collections
import glob
from threading import BrokenBarrierError
import struct
from collections import OrderedDict
from bootburn_lib import bootburn_lib
from flashtools_nverror import nverror
from flashtools_nverror import AbnormalTermination
from asymmetric_boot_chains import AsymmetricBct
from bootburn_bct import PlatformJsonDefines, LegacyDefines, StorageConfigParser, BootburnBct

def GetBCTDerConstFromCfg(cfg, logger):
    """Extract the derivation constants from bct partition

    Args:
        cfg: the cfg file to extract derivation constants
    return:
        returns extracted derivation constants. if constants are not specifed in cfg
        then return derivations constants filled with zeros.
    """
    bct_der_const_len = 8
    max_const_cnt = 5
    zero_der_str = bytearray(bct_der_const_len)
    derivation_constants = [zero_der_str] * max_const_cnt
    #this is encryption derivation string of BRBCT which is 4bytes.
    derivation_constants.append(bytearray(4))
    lines = []
    with open(cfg, 'r') as cfg_fd:
        lines = cfg_fd.readlines()

    def parse_der_const(hex_str, bytes_str_len):
        if hex_str.startswith('0x') or hex_str.startswith('0X'):
                hex_str = hex_str[2:]
        if len(hex_str) > bytes_str_len:
            raise ValueError('Hex string too long, max len is ' + bytes_str_len)
        hex_str = ('0' * (bytes_str_len - len(hex_str))) + hex_str
        byte_value = bytearray.fromhex(hex_str)
        return byte_value

    curr_partiton = ''
    for ln in lines:
        line =  ln.strip()
        if line.startswith('name='):
            curr_partiton = line.split('=')[1]
            continue

        if line.startswith('derivation_const1_string=') or \
           line.startswith('derivation_const2_string=') or \
           line.startswith('derivation_const_tz_string=') or \
           line.startswith('derivation_const_gp_string=') or \
           line.startswith('derivation_const_fsi_string=') :
            try:
                if curr_partiton != 'bct':
                    logger(line + ' can only be specified on bct partition.')
                    return None
                attr, hex_str = line.split('=')
                der_const_bytes = parse_der_const(hex_str, bct_der_const_len * 2)
                logger('derivation constant ' + attr + ' parsed with value ' + hex_str)

                if attr == 'derivation_const1_string':
                    derivation_constants[0] = der_const_bytes
                elif attr == 'derivation_const2_string':
                    derivation_constants[1] = der_const_bytes
                elif attr == 'derivation_const_tz_string':
                    derivation_constants[2] = der_const_bytes
                elif attr == 'derivation_const_gp_string':
                    derivation_constants[3] = der_const_bytes
                elif attr == 'derivation_const_fsi_string':
                    derivation_constants[4] = der_const_bytes
            except Exception as err:
                logger('Failed to parse ' + line)
                logger('Exception message ' + repr(err))
                return None
        if curr_partiton == 'bct' and line.startswith('encryption_derivation_string='):
            try:
                attr, hex_str = line.split('=')
                eds_bytes = parse_der_const(hex_str, 8)
                derivation_constants[5] = eds_bytes
            except Exception as err:
                logger('Failed to parse ' + line)
                logger('Exception message ' + repr(err))
                return None
    return derivation_constants

class bootburn_thor(bootburn_lib):
    def __init__(self, targetConfig):
        if(sys.hexversion >= 0x3000000):
            super().__init__(targetConfig)
            self.log("python 3 used")
        else:
            super(bootburn_thor, self).__init__(targetConfig)

        if not targetConfig.parsed_commandline:
            return

        caller = targetConfig.sysMonitor.identifier
        targetConfig.BoardSetFilePathsAndDefaultValues()
        targetConfig.p_TopOutDataPath = targetConfig.p_OutDirPath

    def nvSignBinary(self, in_file, magic, only_sign=False):
        import binascii
        #HACK: Add current directory to sys.path
        sys.path.insert(0, os.getcwd())
        from tegrasign_v3 import (compute_sha, tegrasign, set_env, hex_to_str, str_to_hex)

        def call_tegrasign(file_val, getmode, getmont, key,
                       length, list_val, offset, pubkeyhash, sha, skip_enc,
                       verbose=False, iv=0, aad=0, tag=0, sign=None,
                       verify=0, kdf=None, hsm=None):

            tegrasign(file_val, getmode, getmont, key, length,
                    list_val, offset, pubkeyhash, sha, skip_enc,
                    verbose, iv, aad, tag, sign, verify, kdf, hsm)

        header_magic_size = struct.calcsize('>I')
        with open(in_file, 'rb') as f:
            header_magic = struct.unpack('>I', f.read(header_magic_size))[0]
            f.seek(0, 0)

        # Convert decimal to hex
        header_magic = format(header_magic, 'x')
        self.log('header_magic: %s' % header_magic)
        if (header_magic == '4e564441'):
            self.log('******* nvsign is skipped for file name: %s  ********' %(in_file))
            return in_file

        filename = os.path.basename(in_file)
        out_file = os.path.splitext(filename)[0] + '_dev' + os.path.splitext(filename)[1]
        aligned_file = os.path.splitext(filename)[0] + '_aligned' + os.path.splitext(filename)[1]
        if os.path.exists(in_file):
            shutil.copyfile(in_file, aligned_file)
        mode = 'nvidia-rsa'
        command = './tegrahost_v2'
        command += ' --chip' + ' 0x26' + ' 0'
        command += ' --align ' + aligned_file
        result = self.shellUtils.executeShellCommand(command)
        if(result != nverror.NvError_Success):
            AbnormalTermination('Failed to align binary %s' % in_file , nverror.NvError_BadParameter)

        filename = aligned_file
        # Get a copy of the binary, used for aes-gcm op later
        with open(filename, 'rb') as f:
            src = bytearray(f.read())
        enc_file = os.path.splitext(filename)[0] + '_encrypt' + os.path.splitext(filename)[1]
        shutil.copyfile(filename, enc_file)
        filename = os.path.splitext(filename)[0] + '_encrypt' + os.path.splitext(filename)[1]

        mode = 'nvidia-rsa'
        command = './tegrahost_v2'
        command += ' --chip ' + ' 0x26 ' + ' 0 '
        command += ' --ratchet 0 0 '
        command += ' --magicid ' + " " + magic + " "
        command += ' --addmb1nvheader ' + filename + " " + mode + " "
        result = self.shellUtils.executeShellCommand(command)
        if(result != nverror.NvError_Success):
            AbnormalTermination('Failed to add BCH to %s' % filename, nverror.NvError_BadParameter)

        filename = os.path.splitext(filename)[0] + '_sigheader' + os.path.splitext(filename)[1]

        if bool(only_sign) == False:
            # Do encryption
            # Need 1) iv1 == stage1_components[0].enc_params.u8_iv 2) aad = stage1_components[0] before gcm
            with open(filename, 'rb') as f:
                src_and_bch = bytearray(f.read())

            fileNm, fileExt = os.path.splitext(filename)
            enc_file = fileNm + '_encrypt'  + fileExt
            tag_file = fileNm + '.tag'

            # Retrieve iv that will be used for aes encryption.
            iv1_offset = 7956 # = stage1_components[0].enc_params.u8_iv
            iv1_size = 12
            iv1 = src_and_bch[iv1_offset:iv1_offset+iv1_size]
            # Retrieve aad data that is used for AES-GCM.
            # = boot_component_header_t.stage1_components[0]
            aad1_offset = 7904
            aad1_size = 64
            aad1 = src_and_bch[aad1_offset:aad1_offset+aad1_size]

            payload_size = len(src)
            payload_offset = len(src_and_bch) - payload_size

            # Retrieve derivation & version that is used for key wrapping
            der_str_offset = 7936
            der_str_size = 16
            der_str = src_and_bch[der_str_offset:der_str_offset+der_str_size]
            ver_offset = 7920
            ver_size = 4
            ver = src_and_bch[ver_offset:ver_offset+ver_size]
            sha_offset = 7984
            sha_size = 64
            # = boot_component_header_t.stage1_components[0].enc_params.u8_auth_tag
            tag1_offset = 7968
            tag1_size = 16
            tag1 = src_and_bch[tag1_offset:tag1_offset+tag1_size]

            args_offset = '6784'
            args_length = '1408'

            # These 2 will be reverted when psc_bl1 and psc_fw binaries are passed in
            # for bch's u8_stage1_res parsing. Currently we can use 0's b/c these
            # values are 0's in the bch until they are officially stage1 signed
            psc_bl = bytearray(8) #TODO
            psc_fw = bytearray(8) #TODO
            chip_info = '0x260'
            lines = 'IV : "' + hex_to_str(iv1) + '"\n'
            lines += 'AAD : "'+ hex_to_str(aad1) + '"\n'
            lines += 'DERSTR : "' + hex_to_str(der_str) + '"\n'
            lines += 'VER : "' + hex_to_str(ver) + '"\n'
            lines += 'FLAG : "DEV"\n'
            lines += 'CHIPID : "%s"\n' % (chip_info)
            lines += 'MAGICID: "' + magic + '"\n'
            lines += 'BL_DERSTR : "' + hex_to_str(psc_bl) + '"\n'
            lines += 'FW_DERSTR : "' + hex_to_str(psc_fw) + '"\n'

            kdf_yaml = 'kdf_args_%s.yaml' %(fileNm)
            with open(kdf_yaml, 'w') as f:
                f.write(lines)

            call_tegrasign(
                filename, None, None, None, str(payload_size), None, str(payload_offset),
                None, None, None, False, 0, 0, 0, None, 0, ['kdf_file=' + kdf_yaml]
            )

            enc_file_sha = compute_sha('sha512', enc_file, payload_offset, payload_size)

            # Write the binary digest back to bch
            if (os.path.exists(enc_file_sha)):
                with open(enc_file, 'rb') as fe, open(enc_file_sha, 'rb') as fs, open(tag_file, 'rb') as ft:
                    enc_buff = bytearray(fe.read())
                    sha = bytearray(fs.read())
                    tag_buff = bytearray(ft.read())
                    enc_buff[sha_offset:sha_offset + sha_size] = sha[:]
                    enc_buff[tag1_offset:tag1_offset+tag1_size] = tag_buff[:]

                    with open(filename, 'wb') as f:
                        f.write(enc_buff)

            call_tegrasign(
                filename, None, None, self.flashUtils.f_TegraSignDevKeyFiles[1], args_length,
                None, args_offset, None, 'sha512', None
            )

        signed_file = os.path.splitext(filename)[0] + '.sig'
        sig_type = "nvidia-rsa"
        command = './tegrahost_v2'
        command += ' --chip 0x26 0 '
        command += ' --updatesigheader ' + " " + filename + " " + signed_file + " " + sig_type
        result = self.shellUtils.executeShellCommand(command)
        if(result != nverror.NvError_Success):
            AbnormalTermination('Failed to update signature to %s' % filename, nverror.NvError_BadParameter)

        shutil.copyfile(filename, out_file)
        self.log('******* nvsign generated file name: %s  ********' %(out_file))
        self.log('******* Please make sure this is updated in partition layout *******')
        return out_file

    def setBinaryPaths(self, targetConfig):
        targetConfig.f_MB2Applet = self.targetConfig.boardDefaultPaths.get("f_MB2Bin", "applet_t264.bin")
        targetConfig.f_FlashingMB2Applet = self.targetConfig.boardDefaultPaths.get("f_MB2Bin", "applet_t264.bin")

    def CopyFlashingDtbImg(self, p_TempDumpPath):
        l_LinuxDtbImgFound = self.shellUtils.grep(os.path.join(self.targetConfig.p_FlashingCfgPath, self.targetConfig.f_FlashingCfg), self.targetConfig.s_LinuxDtbImgName)

        if (l_LinuxDtbImgFound):
           self.shellUtils.Copy(os.path.join(self.targetConfig.p_FlashingDtbPath, self.targetConfig.boardDefaultPaths["f_FlashingDtbImg"]), os.path.join(p_TempDumpPath, self.targetConfig.s_LinuxDtbImgName))
        else:
           errorMsg = "s_ERROR_TARGET_CONFIGURE -- " + self.targetConfig.s_LinuxDtbImgName + " not found in " + self.targetConfig.f_FlashingCfg
           AbnormalTermination(errorMsg, nverror.NvError_ConfigVarNotFound)

        l_BpmpDtbImgFound = self.shellUtils.grep(os.path.join(self.targetConfig.p_FlashingCfgPath, self.targetConfig.f_FlashingCfg), self.targetConfig.s_BPMPStorageDtbImgName)
        if (l_BpmpDtbImgFound):
            self.shellUtils.Copy(os.path.join(self.targetConfig.p_BpmpFWFlashingDtb, self.targetConfig.boardDefaultPaths["f_FlashingBpmpFwDtb"]), os.path.join(p_TempDumpPath, self.targetConfig.s_BPMPStorageDtbImgName))
        else:
            errorMsg = self.targetConfig.s_BPMPStorageDtbImgName + " not found in " + self.targetConfig.f_FlashingCfg
            AbnormalTermination("ERROR_TARGET_CONFIGURE -- " + errorMsg, nverror.NvError_ConfigVarNotFound)

    def VerifyLowPowerPartitions(self):
        if (self.targetConfig.parsed_commandline["L"] and self.targetConfig.n_SC7_FW_FOUND is False):
            errorStr = "Invalid config file with -L option (Low Power Mode)\n"
            errorStr += "Low power mode config files must specify \"name=sc7-fw\" partition"
            AbnormalTermination(errorStr, nverror.NvError_InvalidConfigVar)

    def create_keylist(self, key_file, index):
        """
        Returns an tegrasign_v3 xml file for key lists
        using the given private key or from hsm server as input
        """
        from xml.etree import ElementTree
        basename = os.path.splitext(key_file)[0]
        key_list = '%s_list.xml' %(basename)
        if os.path.exists(key_list):
            return key_list

        self.log("Creating %s for key list signing" %(key_list))

        root = ElementTree.Element('entry_list')
        comment = ElementTree.Comment('Auto generated by tegraflash.py')
        root.append(comment)

        bct_attribs = {'active_index':index, 'pcp_file':'%s.pcp' %(basename), \
            'pcps_file':'%s.pcps' %(basename), 'pcps_hash_file':'%s.pcps.hash' %(basename), \
            'chip_id':'0x260'}
        key_attribs = {'key': key_file}
        bct_child = ElementTree.SubElement(root, 'bct')

        for attrib in bct_attribs:
            bct_child.set(attrib, bct_attribs[attrib])

        for i in range(16):
            # Set index0 and index1 to point to the <file> from --key <file>
            if i in [0, 1]:
                is_hash_only = False
            else:
                is_hash_only = True
            child = ElementTree.SubElement(root, 'entry')
            for attrib in key_attribs:
                child.set(attrib, key_attribs[attrib])
                if  is_hash_only == True:
                    child.set('mode', 'sha')
                else:
                   # Leave mode =='' as tegrasign_v3 will fill in
                    child.set('mode', '')
                child.set('pub_file',  '%s_%d.pub' %(basename, i))
                child.set('hash_file', '%s_%d.pub.hash' %(basename, i))
                child.set('key_id', str(i))

        list_tree = ElementTree.ElementTree(root)
        list_tree.write(key_list)
        return key_list


    def gen_pcp(self):
        """
        Returns the public key and hash file of pcp digests for the session from the
        given private key or from hsm server
        """
        pcp_fpath = self.targetConfig.getPcpFilePath()
        if (os.path.exists(pcp_fpath)):
            os.remove(pcp_fpath)
        if not(self.targetConfig.n_SecurebootFlash is True and os.path.exists(pcp_fpath) is False):
            return

        hash_fpath = self.targetConfig.getPcpsHashFilePath()
        if (os.path.exists(hash_fpath)):
            os.remove(hash_fpath)
        if not(self.targetConfig.n_SecurebootFlash is True and os.path.exists(hash_fpath) is False):
            return

        args = ""
        if(len(self.targetConfig.HSMStr) != 0):
            args = " --hsm " + self.targetConfig.HSMAuthStr
        else:
            #secure boot flash, keys are provided, pcp is not available.  Generating pcp
            active_index = '1' # hard-coded
            key_list = self.create_keylist(self.targetConfig.getPkcKeyFilePathFromOutData(), active_index)
            args = " --key " + key_list
        l_tegrasign="./tegrasign_v3.py"
        tegraSignCommand = "{} {} --pubkeyhash {} {} --verbose".format(l_tegrasign, args, pcp_fpath, hash_fpath)
        result = self.shellUtils.executeShellCommand(tegraSignCommand)
        if(result != nverror.NvError_Success):
            print('failed to get pcp file: {}, and hash file: {}'.format(pcp_fpath, hash_fpath))
            AbnormalTermination('Failed to get PCP', nverror.NvError_BadParameter)
        # search the dir for *_signed.xml file
        signed_wild = os.path.join(os.path.dirname(pcp_fpath), '*_signed.xml')
        for name in glob.glob(signed_wild):
            signed_key_list = name
            break
        if os.path.exists(signed_key_list) == False:
            raise AbnormalTermination(" %s is not generated. Please check %s content" %(signed_key_list, key_list))
        self.targetConfig.setPcpKeyListFile(signed_key_list)

    def GrepBinPathFromConfig(self, cfgFileDataList, binName, chain='A', newName=None, suppress=False):
        """Grep the binary path using specified binary name for
           specific chain

           Use top level/global storage config and chain specific
           storage config files to look for the binary path in
           specific chain

        Args:
            cfgFileDataList (list): cfg file data list
                                    [[<cfgFileKey>, <cfgFilePath]...]
                                    cfgFileKey helps in determining chain specific
                                    cfgFile
            binName (str): binary name to be searched from config files
            chain (str, optional): chain Id. Defaults to 'A'.
            newName:  Dce is special we combine and rename.  Passing in new binary
                      name
            suppress: Suppress exception raised if binName is not found

        Raises:
            AbnormalTermination: Raises exception if binary is not found in the
                                 cfg files and supress flag is True

        Returns:
            str: Path to the binary
        """
        s_BinRegex = r"filename\s*=\s*(.*" + binName + ".*bin)"
        binPath = None

        # Search in the global file and chain specific file
        cfgFileList = [cfgFileDataList[0][1]] # global/top level config file
        for partitionName, cfgFile in cfgFileDataList:
            if (partitionName.startswith(chain)):
                cfgFileList.append(cfgFile)

        for cfgFile in cfgFileList:
            binRegexMatch = self.shellUtils.grepFileWithRegex(cfgFile, s_BinRegex)
            self.log("Looking for %s binary in the config file %s for %s" % (binName, cfgFile, chain))
            if (binRegexMatch != None):
                matchedBinPath = binRegexMatch.group(0).split('=')[1]
                matchedBinName = os.path.basename(matchedBinPath)
                if newName == None:
                    self.shellUtils.replaceString(matchedBinPath, matchedBinName, cfgFile)
                else:
                    self.shellUtils.replaceString(matchedBinPath, newName, cfgFile)
                binPath = matchedBinPath

        if (binPath == None):
            if suppress == True:
                return None
            raise AbnormalTermination("%s binary for chain %s not found in cfg files: %s" % (binName, chain, cfgFileList))

        self.log("Found %s binary for chain %s: %s" % (binName, chain, binPath))

        return binPath

    def CopyAndPreprocessMb2Binary(self, l_Operation, p_TempDumpPath):
        mb2binImage = os.path.basename(self.targetConfig.f_NvMb2BinImage)
        mb2binImage = os.path.join(p_TempDumpPath, mb2binImage)
        self.shellUtils.Copy(self.targetConfig.f_NvMb2BinImage, p_TempDumpPath)

        if (l_Operation == "flash-images"):
            #currently both non-safe and safe point to same source file and same destination name
            self.gr_mb2_append_bin.append(mb2binImage)

            if(self.targetConfig.boardDefaultPaths["f_MB2Bct"] != None):
                mb2bctOutput = "mb2bct"
                mb2bctOutput = os.path.join(p_TempDumpPath, mb2bctOutput + "_MB2.bct")
                if(os.path.isfile(mb2bctOutput)):
                    self.shellUtils.appendBinary(mb2binImage, mb2bctOutput)
        elif (l_Operation == "rcm-boot" or l_Operation == "rcm-flash"):
            if(self.targetConfig.boardDefaultPaths["f_MB2Bct"] != None):
                self.log("****** Generating mb2 BCT..")
                self.log("MB2 bin image is: " + mb2binImage)
                mb2bctOutput = "mb2bct"
                mb2bctOutput = os.path.join(p_TempDumpPath, mb2bctOutput + "_MB2.bct")
                if(os.path.isfile(mb2bctOutput)):
                    self.shellUtils.appendBinary(mb2binImage, mb2bctOutput)
                    #if(os.path.isfile(debugMb2Bin)):
                    #    self.shellUtils.appendBinary(debugMb2Bin, mb2bctOutput)
        else:
            self.vlog ("Incorrect arguement to CopyAndPreprocessMb2Binary. Exiting")
            AbnormalTermination("s_ERROR_INVALID_ARGUMENT -- " + l_Operation)

    def CopyAndPreprocessDceBinary(self, l_Operation, p_TempDumpPath):
        dceFileName = self.targetConfig.s_dceName
        if (dceFileName == None):
            AbnormalTermination("Didn't Find dce.bin", nverror.NvError_FileNotFound)
        dceDtbName = self.targetConfig.s_dceDtbName
        if (dceDtbName == None):
            AbnormalTermination("Didn't Find a Dce Dtb File", nverror.NvError_FileNotFound)
        newDceFile = os.path.join(p_TempDumpPath, "dce-dtb.bin")
        if(os.path.isfile(dceFileName) is False):
            AbnormalTermination("%s not found" % (dceFileName), nverror.NvError_FileNotFound)
        with open(dceFileName,"rb") as dceFile:
            buffer1 = dceFile.read()
            if(os.path.isfile(dceDtbName) is False):
                AbnormalTermination("Dce dtb File not found -- " + dceDtbName, nverror.NvError_FileNotFound)
            with open(dceDtbName,"rb") as dtbFile:
                buffer2 = dtbFile.read()
                with open(newDceFile, "wb") as f:
                    f.write(buffer1)
                    f.write(buffer2)

    def genBrbctDerConsts(self):
        der_consts = GetBCTDerConstFromCfg(self.targetConfig.f_FlashCfg, self.log)
        if(der_consts == None):
            AbnormalTermination('Derivation constant parsing failed', nverror.NvError_BadParameter)

        der_const_fpath = self.targetConfig.getBCTDerConstFilePath()
        with open(der_const_fpath, 'wb+') as der_const_fd:
            for der_const in der_consts:
                der_const_fd.write(der_const)

    def AddBch(self, filename, magicId):
        basename = os.path.basename(filename)
        alignedFile = os.path.splitext(basename)[0] + '_aligned' + os.path.splitext(basename)[1]
        sigHdrFile = os.path.splitext(basename)[0] + '_aligned_sigheader' + os.path.splitext(basename)[1]
        if os.path.exists(filename):
            shutil.copyfile(filename, alignedFile)
        mode = 'nvidia-rsa'
        command = './tegrahost_v2'
        command += ' --chip' + ' 0x26' + ' 0'
        command += ' --align ' + alignedFile
        result = self.shellUtils.executeShellCommand(command)
        if(result != nverror.NvError_Success):
            AbnormalTermination('Failed to align binary %s' % filename , nverror.NvError_BadParameter)

        command = './tegrahost_v2'
        command += ' --chip' + ' 0x26' + ' 0'
        command += ' --magicid ' + magicId + ' --appendsigheader ' + alignedFile + ' zerosbk' # TODO: change algo
        result = self.shellUtils.executeShellCommand(command)
        if(result != nverror.NvError_Success):
            AbnormalTermination('Failed to add bch to binary %s' %  alignedFile, nverror.NvError_BadParameter)

        if os.path.exists(sigHdrFile) == False:
            self.log("Skipping %s pkg genereation since %s is not found" % pkg, sigHdrFile)
            AbnormalTermination('Failed to add bch to binary %s' %  alignedFile, nverror.NvError_BadParameter)

        return sigHdrFile

    def SignBch(self, filename, magicId):
        if (self.targetConfig.n_SecurebootFlash is False):
            return filename

        l_encrypt = ""

        chipID = str(self.targetConfig.GetChipID())
        l_NvImagegenCmd = " --chip " + chipID + " --chipver A" + str(self.targetConfig.n_ChipVersion).zfill(2)

        # Skip extra encryption for HPSE/SB PCT binary
        pct_magic_ids = ["HPCF", "SBCF"]
        if (self.targetConfig.n_EncryptedbootFlash is True) and (magicId not in pct_magic_ids):
            der_str = "PCTENCRYPTIONSTR"
            l_NvImagegenCmd += " --encrypt "
            l_NvImagegenCmd += " --der_str " + der_str
            l_encrypt = "_encrypt"

        if(len(self.targetConfig.HSMStr) != 0):
            l_NvImagegenCmd += " --hsm " + self.targetConfig.HSMAuthStr
        l_NvImagegenCmd += " --pkc " + self.targetConfig.f_PkcKeyFile
        l_NvImagegenCmd += " --pcp " + self.targetConfig.getPcpFilePath()
        l_MB1_Recovery = "mb1_recovery{}_signed.bin".format(l_encrypt)
        l_NvImagegenCmd += " --bct_der_consts " + self.targetConfig.getBCTDerConstFilePath()

        signCmd = self.flashUtils.f_NvImageGen + l_NvImagegenCmd + " --signbin {} {}".format \
             (os.path.join(os.getcwd(),filename), magicId)

        result = self.shellUtils.executeShellCommand(signCmd)
        if (result != nverror.NvError_Success):
             AbnormalTermination("s_ERROR_TOOL_NVIMAGEGEN", nverror.NvError_NvImagegen)

        basename = os.path.basename(filename)
        signFile = '{}{}_signed{}'.format(os.path.splitext(filename)[0], l_encrypt, os.path.splitext(basename)[1])
        return signFile

    def GenerateHpseSbPkg(self, l_Operation, p_TempDumpPath, chain):
        outputDir = os.path.join(self.targetConfig.p_OutDirPath, l_Operation)
        self.log("Generating HPSE & SB Packages")

        if self.targetConfig.n_EngSample == True:
            f_HpseBlBinSigned = "f_HpseBlDevBin"
            f_SbBlBinSigned   = "f_SbBlDevBin"
            f_HpseKrnlBinSigned  = "f_HpseKrnlDevBin"
            f_SbKrnlBinSigned    = "f_SbKrnlDevBin"
            f_HpseCpioBinSigned = "f_HpseCpioDevBin"
            f_SbCpioBinSigned = "f_SbCpioDevBin"
            ext = "dev"
        else:
            f_HpseBlBinSigned = "f_HpseBlProdBin"
            f_SbBlBinSigned   = "f_SbBlProdBin"
            f_HpseKrnlBinSigned  = "f_HpseKrnlProdBin"
            f_SbKrnlBinSigned    = "f_SbKrnlProdBin"
            f_HpseCpioBinSigned = "f_HpseCpioProdBin"
            f_SbCpioBinSigned = "f_SbCpioProdBin"
            ext = "prod"

        self.targetConfig.SetHpseSbPaths()

        # 1. Check for file existence
        # f_*Bin = source filename
        # f_*BinAbs = source filename with source absolute path
        # f_*BinCwd = final filename in cwd
        pkgDict = {
            'hpse_t264' : [
                [self.targetConfig.p_HpseBlBin, f_HpseBlBinSigned, "f_HpseBlBinAbs"],
                [self.targetConfig.p_HpseKrnlBin, f_HpseKrnlBinSigned, "f_HpseKrnlBinAbs"],
                [self.targetConfig.p_HpseCpioBin, f_HpseCpioBinSigned, "f_HpseCpioBinAbs"],
                [self.targetConfig.p_HpsePctBin, "f_HpsePctBin", "f_HpsePctBinAbs"]],
            'sb_t264' : [
                [self.targetConfig.p_HpseBlBin, f_SbBlBinSigned, "f_SbBlBinAbs"],
                [self.targetConfig.p_HpseKrnlBin, f_SbKrnlBinSigned, "f_SbKrnlBinAbs"],
                [self.targetConfig.p_HpseCpioBin, f_SbCpioBinSigned, "f_SbCpioBinAbs"],
                [self.targetConfig.p_HpsePctBin, "f_SbPctBin", "f_SbPctBinAbs"]],
        }

        p_Oesp = 0
        f_Oesp = 1
        f_OespAbs= 2
        oespDict = {}

        for pkg in pkgDict:
            f_Pkg = self.GrepBinPathFromConfig(self.storageCfgParser.cfgFileList, pkg, suppress=True)
            if f_Pkg == None:
                self.log("Skipping %s pkg genereation since it is not found in %s" %(pkg, \
                    self.storageCfgParser.cfgFileList))
                raise AbnormalTermination("ERROR: Failed to create %s pkg due to pkg file is not defined" %(pkg),
                    nverror.NvError_InvalidState)
            for oesp in pkgDict[pkg]:
                f_OespBinAbs = os.path.join(oesp[p_Oesp], self.targetConfig.filelist[oesp[f_Oesp]])
                if os.path.exists(f_OespBinAbs) == False:
                    self.log("Stopping %s pkg genereation since %s is not found" %(pkg, f_OespBinAbs))
                    raise AbnormalTermination("ERROR: Failed to create %s pkg due to %s is not found" %(pkg, f_OespBinAbs),
                        nverror.NvError_InvalidState)
                oespDict[oesp[f_OespAbs]] = f_OespBinAbs # Save abs path filename

        # 2. Add bch to pct
        f_PctAbs = 0
        f_Pct = 1
        PctMagic = 2

        pkgDict = {
            'hpse_t264' : ["f_HpsePctBinAbs", "f_HpsePctBin", "HPCF"],
            'sb_t264' : ["f_SbPctBinAbs", "f_SbPctBin", "SBCF"]
        }
        for pkg in pkgDict:
            f_BchPct = self.AddBch(oespDict[pkgDict[pkg][f_PctAbs]], pkgDict[pkg][PctMagic])
            f_BchPct = self.SignBch(f_BchPct, pkgDict[pkg][PctMagic])
            oespDict[pkgDict[pkg][f_Pct]] = f_BchPct

        # 3. Concatenate krnl + pct + cpio as om pkg
        pkgDict = {
            'f_HpseOm_t264' : ["f_HpseKrnlBinAbs", "f_HpsePctBin", "f_HpseCpioBinAbs"],
            'f_SbOm_t264' : ["f_SbKrnlBinAbs", "f_SbPctBin", "f_SbCpioBinAbs"]
        }
        omDict = {
            "f_HpseOm_t264": ["f_HpseOm", "hpseOm_T264.pkg", "HPOM"],
            "f_SbOm_t264": ["f_SbOm", "sbOm_T264.pkg",  "SBOM"]}

        f_OmBch = 0 # This is bch + krnl + pct + cpio
        f_OmOnly = 1 # This is krnl + pct + cpio
        OmMagic = 2

        for pkg in pkgDict:
            totalFileSize = 0
            for oesp in pkgDict[pkg]:
                f_OespAbs = oespDict[oesp]
                fileSize = os.path.getsize(f_OespAbs)
                totalFileSize += fileSize
            buff = bytearray(totalFileSize)
            start = 0
            for oesp in pkgDict[pkg]:
                with open(oespDict[oesp], 'rb') as f:
                    oesp_buff = bytearray(f.read())
                    buff[start:start+len(oesp_buff)] = oesp_buff[:]
                    start += len(oesp_buff)
                    self.log('Packaging %s to %s' %(oespDict[oesp], omDict[pkg][f_OmOnly]))
            # Using same 'pkg' define in omDict and pkgDict to ref om file name to save
            with open(omDict[pkg][f_OmOnly], 'wb') as f:
                f.write(buff)

        # 4. Add bch to om pkg
        for om in omDict:
            f_Bch = self.AddBch(omDict[om][f_OmOnly], omDict[om][OmMagic])
            oespDict[omDict[om][f_OmBch]] = f_Bch

        # 5. Final packaging = bct + bl + om-pkg
        pgk_dict = {
             'hpse_t264' :
                 ['hpse_bct', 'hpse-bl', "f_HpseBlBinAbs", 'hpse-om', "f_HpseOm"],
             'sb_t264' :
                 ['sb_bct', 'sb-bl', "f_SbBlBinAbs", 'sb-om', "f_SbOm"]
        }

        bct = 0
        bl = 1
        f_BlAbs = 2
        om = 3
        f_Om = 4

        chipID = str(self.targetConfig.GetChipID())
        uid = str(uuid.uuid4()).replace("-", "")
        enc_str = ""
        if (self.targetConfig.n_EncryptedbootFlash is True):
            enc_str = "encryption=true\n"

        for pkg in pgk_dict:
            f_pkg = self.GrepBinPathFromConfig(self.storageCfgParser.cfgFileList, pkg, suppress=True)

            part_dict = OrderedDict([
                ('pt', ['0x80000', None, ""]),
                ('bct', ['0x80000', None, enc_str]),
                (pgk_dict[pkg][bl], ['0x40000', oespDict[pgk_dict[pkg][f_BlAbs]], enc_str]),
                (pgk_dict[pkg][om], ['0x100000', os.path.join(os.getcwd(), oespDict[pgk_dict[pkg][f_Om]]), enc_str]),
            ])

            lines = "[device]\ntype=spi\ninstance=0\nlinux_name=/dev/block/3270000.spi\nsize=0x840000\n"
            for part in part_dict:
                lines += '[partition]\nname=%s\nallocation_policy=sequential\nfilesystem_type=basic\nsize=%s\npartition_attribute=0\n%s'\
                    %(part, part_dict[part][0], part_dict[part][2])
                if part_dict[part][1] == None:
                    continue
                lines += 'filename=%s\n' %(part_dict[part][1])

            cfg_file = 'hpse_sb_pkg.cfg'
            with open(cfg_file, 'w') as f:
                f.write(lines)

            brBctFile = BootburnBct.getBctObject(pgk_dict[pkg][bct],
                                             self.targetConfig,
                                             p_TempDumpPath,
                                             ).getBctFileName()

            l_NvImagegenCmd = ' --bct ' + brBctFile + ' --generatechain ' + chain + ' --cfg ' + cfg_file

            mb1BctFile = BootburnBct.getBctObject("mb1_bct",
                                             self.targetConfig,
                                             p_TempDumpPath,
                                             ).getBctFileName()
            l_NvImagegenCmd += " --mb1bct " + str(mb1BctFile)
            l_NvImagegenCmd += " --chip " + chipID + " --chipver A" + str(self.targetConfig.n_ChipVersion).zfill(2)

            l_NvImagegenCmd += " --man man_" + uid[0:11] + ".txt"
            l_NvImagegenCmd += " --bct_der_consts " + self.targetConfig.getBCTDerConstFilePath()
            if (len((self.targetConfig.s_buildVersion).strip()) == 0):
                self.targetConfig.s_buildVersion = "012345678ABCDEF"
            l_NvImagegenCmd += " --pvitversion "  + self.targetConfig.s_buildVersion
            if (self.targetConfig.n_NoDuPvit is True):
                l_NvImagegenCmd += " --no_du_pvit  "
            l_NvImagegenCmd = self.flashUtils.f_NvImageGen + " " + l_NvImagegenCmd + " --showpt"

            if (self.targetConfig.n_SecurebootFlash is True):
                if(len(self.targetConfig.HSMStr) != 0):
                    l_NvImagegenCmd += " --hsm " + self.targetConfig.HSMAuthStr
                l_NvImagegenCmd += " --pkc " + self.targetConfig.f_PkcKeyFile
                l_NvImagegenCmd += " --pcp " + self.targetConfig.getPcpFilePath()
                fileType = "signed"
                if (self.targetConfig.n_EncryptedbootFlash is True):
                    l_NvImagegenCmd += " --encrypt "
                    fileType = "encrypt_signed"

            else:
                fileType = "zerosign"
            result = self.shellUtils.executeShellCommand(l_NvImagegenCmd, False, False)
            if (result != nverror.NvError_Success):
                AbnormalTermination("s_ERROR_TOOL_NVIMAGEGEN", nverror.NvError_NvImagegen)

            if pkg == 'hpse_t264':
                brBctFile = '{}_HPCT_bct_BR_{}.bct'.format(chain, fileType)
                bl_file = '3_hpse_bl1_t264_{}_{}.bin'.format(ext, fileType)
                om_file = '4_hpseOm_T264_aligned_sigheader_{}.pkg'.format(fileType)
            else:
                brBctFile = '{}_SBCT_bct_BR_{}.bct'.format(chain, fileType)
                bl_file = '3_sb_bl1_t264_{}_{}.bin'.format(ext, fileType)
                om_file = '4_sbOm_T264_aligned_sigheader_{}.pkg'.format(fileType)

            br_bct_size = os.path.getsize(brBctFile)
            bl_size = os.path.getsize(bl_file)
            om_size = os.path.getsize(om_file)
            buff_size = br_bct_size + bl_size + om_size

            buff = bytearray(buff_size)
            part_dict = {brBctFile: br_bct_size, bl_file:bl_size, om_file:om_size}

            start = 0
            for part in part_dict:
                with open(part, 'rb') as f:
                    part_buff = bytearray(f.read())
                    buff[start:start+part_dict[part]] = part_buff[:]
                    start += part_dict[part]
                    self.log('Packaging %s to %s' %(part, f_pkg))
            if start == 0:
                raise AbnormalTermination("ERROR: Failed to create pkg due to 0 size from partition files!",
                    nverror.NvError_InvalidState)

            with open(f_pkg, 'wb') as f:
                f.write(buff)
            if os.path.exists('1_PT.bin'):
                os.remove('1_PT.bin')

    def CreateVDKImages(self, l_configFile, p_TempDumpPath, l_Operation):
        l_SubOperation = ""
        thor = True
        cwd = os.getcwd()

        if os.path.exists(self.targetConfig.getBCTDerConstFilePath()) is False :
            self.genBrbctDerConsts()

        self.targetConfig.n_SkipSkuValidate = True
        if (l_Operation == "rcm-flash-provision"):
            l_Operation = "rcm-flash"
            l_SubOperation = "provision"

        operationPath = os.path.join(self.targetConfig.p_OutDirPath, l_Operation)
        operationPathProvision =  os.path.join(operationPath, l_SubOperation)
        self.shellUtils.makeDirectory(operationPath)
        self.log("cwd - " + str(os.getcwd()))
        self.log("operationPath :: " + str(operationPath))
        os.chdir(operationPath)
        self.log("cwd - " + str(os.getcwd()))

        self.HostSpaceCheck()
        self.CopyTegraFlashUtilities(operationPath)
        self.CopyFuseByPassBinaries(operationPath)
        self.gen_pcp()

        if(l_Operation == "rcm-flash"):
            self.CopyFlashingDtbImg(operationPath)

            if (l_SubOperation == "provision"):
                self.targetConfig.n_UFSProvisioningEnable = True
                self.ProvisionUFS(operationPath)
                self.log("overlay.dtb path :: " + os.path.join(operationPath, "provision_overlay.dtb"))
                #"Move provision modied dtb to linux dtb"
                os.rename(os.path.join(operationPath, "provision_overlay.dtb"), os.path.join(operationPath, self.targetConfig.s_LinuxDtbImgName))
        elif (l_Operation == "rcm-boot") or (l_Operation == "flash-images"):
            self.EmptyGrBlBinLists()

        self.shellUtils.Copy(self.flashUtils.f_TegraHost, operationPath, False, True)

        #check for pvit enabled status for each chain
        #    update partition list in case of partial update only
        #  This is being called by both create_bsp and bootburn
        if (l_Operation == "flash-images"):
            self.initializePVITEnableStatus()

        newParentCfg = os.path.join(operationPath, "tmp_" + os.path.basename(l_configFile))
        self.storageCfgParser = StorageConfigParser(self.targetConfig)
        self.storageCfgParser.parseConfigFile(l_configFile, newParentCfg)

        # Sign MB1 and PSC BL1, igpu binaries
        mb1_bin = self.GrepBinPathFromConfig(self.storageCfgParser.cfgFileList, "mb1")
        signedFile = self.nvSignBinary(mb1_bin, "MB1B")
        self.GrepBinPathFromConfig(self.storageCfgParser.cfgFileList, "mb1", "A", signedFile)
        if (self.targetConfig.n_FlashLinux != True) and (self.targetConfig.n_FlashQnx != True):
            self.GrepBinPathFromConfig(self.storageCfgParser.cfgFileList, "mb1", "B", signedFile)

        sc7_bin = self.GrepBinPathFromConfig(self.storageCfgParser.cfgFileList, "sc7", suppress=True)
        if sc7_bin == None:
            self.log("Skipping igbfw nvsigning since it is not found in %s" %(
                  self.storageCfgParser.cfgFileList))
        else:
            signedFile = self.nvSignBinary(sc7_bin, "WB0B")
            self.GrepBinPathFromConfig(self.storageCfgParser.cfgFileList, "sc7", "A", signedFile)
            if (self.targetConfig.n_FlashLinux != True) and (self.targetConfig.n_FlashQnx != True):
                self.GrepBinPathFromConfig(self.storageCfgParser.cfgFileList, "sc7", "B", signedFile)

        psc_bl1_bin = self.GrepBinPathFromConfig(self.storageCfgParser.cfgFileList, "psc_bl1")
        signedFile = self.nvSignBinary(psc_bl1_bin, "PSCB")
        self.GrepBinPathFromConfig(self.storageCfgParser.cfgFileList, "psc_bl1", "A", signedFile)

        igpu_bin = self.GrepBinPathFromConfig(self.storageCfgParser.cfgFileList, "igbfw", suppress=True)
        if igpu_bin == None:
            self.log("Skipping igbfw nvsigning since it is not found in %s" %(
                    self.storageCfgParser.cfgFileList))
        else:
            signedFile = self.nvSignBinary(igpu_bin, "GBFW")
            self.GrepBinPathFromConfig(self.storageCfgParser.cfgFileList, "igbfw", "A", signedFile)

        self.GenerateImage(self.storageCfgParser.cfgFileList, l_Operation, operationPath)

        if (l_Operation == "flash-images"):
            if (self.targetConfig.f_refPvitFile and self.targetConfig.s_UpdatePartitions):
                self.UpdatePVITinCreateImages(operationPath, True)
            else:
                self.UpdatePVITinCreateImages(operationPath, False)

        self.CleanupTegraFlashUtilities(l_Operation, operationPath)

        if (l_SubOperation == "provision"):
            operationPathProvision =  operationPath+"-"+l_SubOperation
            if (os.path.isdir(operationPathProvision)):
                shutil.rmtree(operationPathProvision)
            os.rename(operationPath, operationPathProvision)

        os.chdir(cwd)

    def generateBct(self, bctDefines, outputDir):
        bctList = None
        pcpsBctList = None
        if (self.targetConfig.n_Asymmetric):
            # For asymmetric BR BCT is always Marker based for chain specific images
            # Special BCTs (default, fskp, etc... are generated separately from image
            # generation for specific chain)
            bctList = ["br_bct_asymmetric", "hpse_bct", "sb_bct", "mb1_bct", "mb2_bct", "mem_bct"]
            pcpsBctList = ["br_bct_asymmetric", "hpse_bct", "sb_bct"]
        else:
            bctList = ["br_bct", "hpse_bct", "sb_bct", "mb1_bct", "mb2_bct", "mem_bct"]
            pcpsBctList = ["br_bct", "hpse_bct", "sb_bct"]

        if 'f_BpmpMemCfgParam' in self.targetConfig.boardDefaultPaths:
            bctList.append("bpmp_mem_bin")

        if 'ENABLE_MULTI_SKU_SUPPORT' in bctDefines:
            self.targetConfig.b_IsMultiSkuEnabled = True

        if 'f_MB1MultiSkuPlatformParam' in self.targetConfig.boardDefaultPaths and self.targetConfig.b_IsMultiSkuEnabled:
            bctList.append("mb1_multi_sku_platform_bct")

        for bctType in bctList:
            # Get BCT object
            bctOb = BootburnBct.getBctObject(bctType, self.targetConfig, outputDir)
            bctOb.generateBct(bctDefines)

            #call tegrabct to pad the pcps, which are products of gen_pcp()
            if bctType in pcpsBctList:
                bctOb.fillPcpPcpDigests(self.targetConfig.getPcpKeyListFile())

        # Assuming 0th index in bctList above is always for BR BCT type
        brBctFile = BootburnBct.getBctObject(bctList[0], self.targetConfig, outputDir).getBctFileName()
        if(os.path.isfile(brBctFile) is False):
            AbnormalTermination("BCT file " + brBctFile + " not found", nverror.NvError_FileNotFound)

        if (self.targetConfig.n_GenBinsOnly is not True):
            # Not create_bsp_images.py hence preserve target sku information
            self.PreserveTargetSkuInfo(brBctFile)

        if (self.targetConfig.customerData.isLoaded() or self.targetConfig.f_SignedCustomerData != None):
            self.SkuBlobGen(brBctFile)

    def GenerateAllBCTs(self, operation, partitionList, chain = 'A'):
        self.log("Generating BCT files")

        outputDir = os.path.join(self.targetConfig.p_OutDirPath, operation)

        # Choose between Legacy vs Platform Config based defines
        # If native config or rcm-flash in platform config,
        #
        if (self.targetConfig.n_FlashLinux or self.targetConfig.n_FlashQnx):
            definesOb = LegacyDefines()
        elif ("bct_defines" in self.targetConfig.platformConfig):
            definesOb = PlatformJsonDefines()
        else:
            # Default to Legacy defines
            definesOb = LegacyDefines()

        # get chain specific defines
        bctDefines = definesOb.getDefines(operation, self.targetConfig, partitionList, chain)
        self.generateBct(bctDefines, outputDir)

    def DeleteAllBcts(self, operation):
        outputDir = os.path.join(self.targetConfig.p_OutDirPath, operation)
        bctTypes = ["br_bct", "hpse_bct", "sb_bct", "mb1_bct", "mb2_bct", "mem_bct"]

        if 'f_BpmpMemCfgParam' in self.targetConfig.boardDefaultPaths:
            bctTypes.append("bpmp_mem_bin")

        if 'f_MB1MultiSkuPlatformParam' in self.targetConfig.boardDefaultPaths:
            bctTypes.append("mb1_multi_sku_platform_bct")

        for bctType in bctTypes:
            # Get BCT object
            bctOb = BootburnBct.getBctObject(bctType, self.targetConfig, outputDir)
            bctOb.deleteFiles()

    def CreateBSPImages(self):
        self.targetConfig.sysMonitor.vlog("Creating rcm-boot images")
        self.targetConfig.n_SkipSkuValidate = True

        if (not self.targetConfig.parsed_commandline["vdk"]):
            if not self.targetConfig.is_l4t:
                self.CreateVDKImages(self.targetConfig.f_FlashCfg, self.targetConfig.p_OutDirPath, "rcm-boot")

            # rcm-flash images needed for special mb1_bct for applet
            self.targetConfig.sysMonitor.vlog("Creating rcm-flash images")
            flashingCFG = os.path.join(self.targetConfig.p_FlashingCfgPath, self.targetConfig.f_FlashingCfg)
            self.vlog("flashingCFG  :: " + flashingCFG  + "self.targetConfig.p_OutDirPath :: " + self.targetConfig.p_OutDirPath)
            self.CreateVDKImages(flashingCFG, self.targetConfig.p_OutDirPath, "rcm-flash-provision")
            self.CreateVDKImages(flashingCFG, self.targetConfig.p_OutDirPath, "rcm-flash")

        if not self.targetConfig.parsed_commandline["R"] and not self.targetConfig.is_l4t:
            self.targetConfig.sysMonitor.vlog("Creating flash-images")
            self.CreateVDKImages(self.targetConfig.f_FlashCfg, self.targetConfig.p_OutDirPath, "flash-images")
        self.shellUtils.removeFile(self.targetConfig.getBCTDerConstFilePath())
        self.shellUtils.removeFile(self.targetConfig.getPcpFilePath())

    def Mb2AppletReset(self, resetOption):
        # Reset options are "recovery" and "reset"
        self.sendRcmCommand("--chip 0x26 --reboot " + resetOption)

    def CheckIsMb2Applet(self):
        result = self.sendRcmCommand(" --chip 0x26 --ismb2applet")
        if ("MB2 Applet" in result):
            return True
        else:
            return False

    def updateImageName(self, name, bAddType = False):
        l_encrypt = ""
        l_signed = "_zerosign"
        n_Type = ""

        if (self.targetConfig.n_EngSample):
            n_Type = "_dev"
        else:
            n_Type = "_prod"

        if (bAddType is True):
            name = name+"{}".format(n_Type)

        if (self.targetConfig.n_EncryptedbootFlash is True):
            l_encrypt = "_encrypt"

        if (self.targetConfig.n_SecurebootFlash is True):
            l_signed = "_signed"

        updatedName = name+"{}{}".format(l_encrypt, l_signed)
        self.log("updatedName - {} ".format(updatedName))
        return updatedName

    def getmembctName(self):
        self.vlog("RAMCODE %d" % (self.targetConfig.s_RamCode))
        if (self.targetConfig.s_RamCode >= 0  and self.targetConfig.s_RamCode <= 1):
            membctName = "membct_0"
        elif (self.targetConfig.s_RamCode >= 2 and self.targetConfig.s_RamCode <= 3):
            membctName = "membct_1"
        elif (self.targetConfig.s_RamCode >= 4 and self.targetConfig.s_RamCode <= 5):
            membctName = "membct_2"
        elif (self.targetConfig.s_RamCode >= 6 and  self.targetConfig.s_RamCode <= 7):
            membctName = "membct_3"
        elif (self.targetConfig.s_RamCode >= 8  and self.targetConfig.s_RamCode <= 9):
            membctName = "membct_4"
        elif (self.targetConfig.s_RamCode >= 10 and self.targetConfig.s_RamCode <= 11):
            membctName = "membct_5"
        elif (self.targetConfig.s_RamCode >= 12 and self.targetConfig.s_RamCode <= 13):
            membctName = "membct_6"
        elif (self.targetConfig.s_RamCode >= 14 and  self.targetConfig.s_RamCode <= 15):
            membctName = "membct_7"
        else:
            self.vlog("Wrong RAMCODE %d\n Exiting!" % (self.targetConfig.s_RamCode))
            AbnormalTermination("s_ERROR_INVALID_ARGUMENT -- Wrong RAMCODE", nverror.NvError_InvalidArgument)

        self.vlog("Selected %s for memcfg" % (membctName))
        return membctName

    def BootMb2Applet(self, l_Operation, binaryLocation):
        cwd = os.getcwd()
        if (self.targetConfig.parsed_commandline["multi_die"]):
            operationPath = os.path.join(binaryLocation, l_Operation, self.targetConfig.dieId)
        else:
            operationPath = os.path.join(binaryLocation, l_Operation)
        os.chdir(operationPath)

        brbct = self.shellUtils.find('.', "APbct_BR")[0]
        psc_bl_name = self.updateImageName("psc_bl1_t264", True)
        mb1_name = self.updateImageName("mb1_t264", True)
        psc_bl = self.shellUtils.find('.', psc_bl_name)[0]
        mb1 = self.shellUtils.find('.', mb1_name)[0]
        mb1bct_name = self.updateImageName("bct_MB1")

        if (l_Operation == "rcm-boot"):
            if (self.targetConfig.parsed_commandline["multi_die"]):
                mb1bct = self.shellUtils.find('../../rcm-flash', "bct_MB1")[0]
            else:
                mb1bct = self.shellUtils.find('../rcm-flash', "bct_MB1")[0]
        else:
            mb1bct = self.shellUtils.find('.', mb1bct_name)[0]

        self.sendRcmCommand("--new_session --chip 0x26")
        self.targetConfig.s_BR_CID = self.sendRcmCommand("--uid")

        self.sendRcmCommand("--download bct_br " + brbct)
        self.sendRcmCommand("--download mb1 " + mb1)
        self.sendRcmCommand("--download psc_bl1 " + psc_bl)
        self.sendRcmCommand("--download bct_mb1 " + mb1bct)
        self.sendRcmCommand(" --chip 0x26 --pollbl --download applet applet/applet_blob.bin")

        count = 30
        while count != 0:
            if (self.CheckIsMb2Applet()):
                return
            time.sleep(1)
            count = count - 1

        raise AbnormalTermination("ERROR: Couldn't boot mb2 applet!", nverror.NvError_InvalidState)

    def CopyAndPreProcessMb2Applet(self, l_Operation, p_TempDumpPath):
        if (l_Operation == "rcm-flash" or l_Operation == "rcm-boot"):
            mb2_applet = self.resolvePath(self.targetConfig.filelist["f_MB2Applet"], self.targetConfig.f_MB2AppletPath)
            mb2AppletBinImage = os.path.join(p_TempDumpPath, self.targetConfig.f_MB2Applet)
            self.shellUtils.Copy(mb2_applet, mb2AppletBinImage)

    # Read SkuCode
    # 0 = All Good
    # 1 = emc full floorswept
    # 2 = emc lower floorswept
    # 3 = emc uppper floorswept
    def CheckOptFuses(self, findBoard=False):
        #fuse_list = ["OptEmcDisable", "OptGpcDisable", "Sku"]
        fuse_list = ["Sku"]
        result_list = []
        chipID = str(self.targetConfig.GetChipID())
        skuName = "unknown"

        #Read skuMapping
        skuInfoFile = os.path.join(self.targetConfig.p_SocBoardConfigsPath, self.targetConfig.f_skuInfoJson)
        if (not os.path.exists(skuInfoFile)):
            err_msg = ("Can't find Sku Info File %s" % (skuInfoFile))
            AbnormalTermination(err_msg, nverror.NvError_FileNotFound)

        with open(skuInfoFile, "r") as skuInfo:
            skuInfoDict = json.load(skuInfo)

        #Build and Send Command
        for fuse in fuse_list:
            if (os.path.isfile("e.bin")):
                self.shellUtils.removeFile("e.bin")
            if (os.path.isfile("out.bin")):
                self.shellUtils.removeFile("out.bin")

            tegraParsercommand = ("%s --chip %s --read_fusetype %s e.bin" % (self.flashUtils.f_TegraParser, chipID, fuse))
            result = self.shellUtils.executeShellCommand(tegraParsercommand)
            if (result != nverror.NvError_Success):
                AbnormalTermination("s_ERROR_TOOL_TEGRAPARSER", nverror.NvError_TegraParserError)
            self.sendRcmCommand("--oem readfuses out.bin e.bin")

            #Process Output
            with open("out.bin", "rb") as f:
                data=f.read()
                skuCode = struct.unpack("<i", data)[0]
                result_list.append(skuCode)

        n_SkuInfo = result_list[0]

        self.vlog("Sku %s\n" % (hex(n_SkuInfo)))
        self.targetConfig.s_SkuInfo = hex(n_SkuInfo).upper().replace('X', 'x')
        if (n_SkuInfo == 0):
            skuName = "F0-A1"
            self.vlog("%s\n" %skuName)

        elif self.targetConfig.s_SkuInfo in list(skuInfoDict):
            skuName = skuInfoDict[self.targetConfig.s_SkuInfo]
            #For all prod board the truth of source is
            # target fuseinfo. Overwriting the board_config
            # value with the target skuInfo based value
            # This board_config value is used by bootburn_bct
            # in preprocessing the storage config to replace
            # <SKU> token.
            if (self.targetConfig.b_findBoardName == False):
                if (skuName != self.targetConfig.boardDefaultPaths["s_Skuname"]):
                    skuInfomsg = "Mismatched Sku " + skuName + " " + self.targetConfig.boardDefaultPaths["s_Skuname"]
                    AbnormalTermination(skuInfomsg, nverror.NvError_BadParameter)
            else:
                self.vlog("SkuName = [%s]\n" % (skuName))

            self.targetConfig.boardDefaultPaths["s_Skuname"] = skuName.rpartition("-")[0]
            if (len(self.targetConfig.boardDefaultPaths["s_Skuname"]) == 0):
                self.targetConfig.boardDefaultPaths["s_Skuname"] = skuName.rpartition("-")[2]
        else:
            skuInfomsg = "Unknown SkuInfo ::" + self.targetConfig.s_SkuInfo
            if (self.targetConfig.b_SkuCheck == True):
                AbnormalTermination(skuInfomsg, nverror.NvError_BadParameter)
            else:
                self.vlog(skuInfomsg)
        self.log("SkuName = [%s] %s\n" % (skuName, self.targetConfig.s_SkuInfo))

        if (skuName != "unknown"):
             self.targetConfig.s_targetChipSkuName = skuName
             self.targetConfig.s_SocSkuName = skuName

        return
        #confirm the SOC SKU is in the chipsku_mapping.josn
        self.log("target SOC SKU :: " + str(skuName))
        self.log("current working directory :: " + str(os.getcwd()))
        self.log("f_chipskuMapping :: " + self.targetConfig.f_chipSkuMapping)

        with open(self.targetConfig.f_chipSkuMapping, 'r') as fchipMap:
            chipMapDict = json.load(fchipMap)

        for chipsku, skunameList in chipMapDict.items():
            self.log("chipsku :: " + str(chipsku))
            self.log("skunameList :: " + str(skunameList))
            if (skuName in skunameList):
                self.targetConfig.s_SocSkuBoardModifier = chipsku
                self.targetConfig.s_SocSkuName = skuName
                break

        if len(self.targetConfig.s_SocSkuBoardModifier) == 0:
            self.vlog("Unknown SkuName :: " + skuName)
            if (self.targetConfig.b_SkuCheck == True):
                AbnormalTermination("unknown ChipSku, not found in chipskumapping json file", nverror.NvError_BadParameter)

        if findBoard:
            return

        l_SocSkuMatch = False
        l_boardSOCSkuExt =self.targetConfig.s_BoardName.rpartition('-')[2]

        #board_name doesn't have chipskumapping extention for ct00
        #For ct00, the last part of the board name is not a valid ctxx or fx
        if self.targetConfig.s_SocSkuBoardModifier == l_boardSOCSkuExt:
            l_SocSkuMatch = True
            self.log("Target SOC SKU matches with SOCSKU modifier in Board name")

        elif self.targetConfig.s_SocSkuBoardModifier == 'ct00':
            if l_boardSOCSkuExt in list(chipMapDict):
                self.vlog("Target SOC SKU -  {} is not matching with  Board SOC SKU - {} modifier with ct00 target".format(self.targetConfig.s_SocSkuBoardModifier, l_boardSOCSkuExt))
            else:
                l_SocSkuMatch = True
                self.log("SOC SKU validation is successful. SOC SKU board modifier is  ct00. As expected, no valid modifier in the board name")

        if (l_SocSkuMatch == False)  and (self.targetConfig.b_SkuCheck == True):
            self.vlog("Target SOC Sku mismatch with Board SOC Sku modifier and Sku check is enabled in the board config.  Aborting the flashing")
            AbnormalTermination("Target SOC SKU - {},  not matching with Board SOC SKU modifier - {}".format(self.targetConfig.s_SocSkuBoardModifier, l_boardSOCSkuExt), nverror.NvError_BadParameter)
        else:
            self.log("Target SOC SKU - {} :  Board SOC SKU modifier - {}".format(self.targetConfig.s_SocSkuBoardModifier, l_boardSOCSkuExt))

    def BootRCM(self, l_Operation, binaryLocation):
        self.vlog("BootRCM -  " + l_Operation)
        from bootburn_thor import bootburn_thor
        bootburnThor = bootburn_thor(self.targetConfig)
        self.targetConfig.n_SkipSkuValidate = False
        cwd = os.getcwd()

        if (self.targetConfig.parsed_commandline["multi_die"]):
            operationPath = os.path.join(binaryLocation, l_Operation, self.targetConfig.dieId)
        else:
            operationPath = os.path.join(binaryLocation, l_Operation)

        os.chdir(operationPath)

        brbct_name = bootburnThor.updateImageName("A_bct_BR")
        psc_bl_name = bootburnThor.updateImageName("psc_bl1_t264", True)
        mb1_name = bootburnThor.updateImageName("mb1_t264", True)

        mb1bct_multi_sku_bct_name = None
        mb1bct_name = bootburnThor.updateImageName("bct_MB1")

        if (self.targetConfig.b_IsMultiSkuEnabled and self.targetConfig.n_SkuValue != 0):
            mb1bct_multi_sku_bct_name = bootburnThor.updateImageName(f"mb1_multisku_platform_{self.targetConfig.n_SkuValue}")
            multisku_mb1_bct_file = self.shellUtils.find('.', mb1bct_multi_sku_bct_name)[0]
            if (not os.path.exists(multisku_mb1_bct_file)):
                AbnormalTermination(
                    f"MB1 multi sku bct {multisku_mb1_bct_file} for SkuValue: {str(self.targetConfig.n_SkuValue)} not found",
                    nverror.NvError_FileNotFound
                )

        brbct = self.shellUtils.find('.', brbct_name)[0]

        if (self.targetConfig.customerData.isLoaded() or self.targetConfig.f_SignedCustomerData != None):
        # pick up the resigned bct
            brbct_list = glob.glob(brbct_name + "*resigned*.*")
            if len(brbct_list) > 0:
                brbct = brbct_list[0]

        psc_bl = self.shellUtils.find('.', psc_bl_name)[0]
        mb1 = self.shellUtils.find('.', mb1_name)[0]
        mb1bct = self.shellUtils.find('.', mb1bct_name)[0]

        blob = self.shellUtils.find('.', "rcm_blob")[0]

        membct = bootburnThor.updateImageName(self.getmembctName()) + ".bct"

        #get new device
        self.UpdateTargetInstanceInfo()
        self.sendRcmCommand("--new_session --chip 0x26")
        self.targetConfig.s_BR_CID = self.sendRcmCommand("--uid")

        self.sendRcmCommand("--download bct_br " + brbct)
        self.sendRcmCommand("--download mb1 " + mb1)
        self.sendRcmCommand("--download psc_bl1 " + psc_bl)
        self.sendRcmCommand("--download bct_mb1 " + mb1bct)

        self.sendRcmCommand(" --chip 0x26 --pollbl")
        self.sendRcmCommand("--download bct_mem " + membct)

        if (mb1bct_multi_sku_bct_name is not None):
            self.sendRcmCommand("--download multi_sku_platfo_data " + multisku_mb1_bct_file)

        self.sendRcmCommand("--download blob " + blob)

        os.chdir(cwd)

    def getIntValueFromBinaryData(self, bin_data=None):
        int_val = None

        if (bin_data is not None):
            int_val = struct.unpack("<I", bin_data)[0]

        return int_val

    def updateDieIdToTargetInstanceMapping(self, die_id):
        if (self.targetConfig.dieId != "die" + str(die_id)):
            port_path = self.targetConfig.s_TargetDeviceInfo[self.targetConfig.dieId]["portPath"]
            self.targetConfig.dieId = "die" + str(die_id)
            self.targetConfig.s_TargetDeviceInfo[self.targetConfig.dieId]["portPath"] = port_path

    def runPlatformDetection(self, l_Operation, binaryLocation, l_findBoardOnly=False):
        self.vlog("runPlatformDetection -  " + l_Operation)
        cwd = os.getcwd()

        if (self.targetConfig.parsed_commandline["multi_die"]):
            operationPath = os.path.join(binaryLocation, l_Operation, self.targetConfig.dieId)
        else:
            operationPath = os.path.join(binaryLocation, l_Operation)

        # check applet dir before proceeding
        if not os.path.isdir(os.path.join(operationPath, "applet")):
            self.vlog("runPlatformDetection - exit due to %s not found" % (os.path.join(operationPath, "applet")))
            return

        os.chdir(operationPath)

        brbct_name = self.updateImageName("A_bct_BR")
        psc_bl_name = self.updateImageName("psc_bl1_t264", True)
        mb1_name = self.updateImageName("mb1_t264", True)
        mb1bct_name = self.updateImageName("bct_MB1")

        brbct = self.shellUtils.find('.', brbct_name)[0]
        psc_bl = self.shellUtils.find('.', psc_bl_name)[0]
        mb1 = self.shellUtils.find('.', mb1_name)[0]
        mb1bct = self.shellUtils.find('.', mb1bct_name)[0]
        blob = self.shellUtils.find('.', "rcm_blob")[0]

        self.UpdateTargetInstanceInfo()

        self.sendRcmCommand("--new_session --chip 0x26")
        self.targetConfig.s_BR_CID = self.sendRcmCommand("--uid")
        self.BootMb2Applet(l_Operation, binaryLocation)
        self.CheckOptFuses(l_findBoardOnly)
        if (l_findBoardOnly):
            return

        self.SaveSKUInfoOnPrebuilds(l_Operation, operationPath)
        self.GetRamCode()

        # For multi die platforms, detect die id
        if (self.targetConfig.parsed_commandline["multi_die"]):
            self.sendRcmCommand("--oem dump die dump_die.bin")

            # Extract die information from binary
            with open('dump_die.bin', 'rb') as f:
                die_info = f.read()

            die_id = self.getIntValueFromBinaryData(die_info)
            self.log("Die ID: " + str(die_id))

            if die_id is None:
                AbnormalTermination("Unable to extract Die ID from binary", nverror.NvError_BadParameter)

            self.updateDieIdToTargetInstanceMapping(die_id)

        if self.targetConfig.b_IsMultiSkuEnabled:
            self.sendRcmCommand("--oem dump sku_value sku_value.bin")

            # Extract sku information from binary
            with open('sku_value.bin', 'rb') as f:
                sku_bin_data = f.read()

            sku_val = self.getIntValueFromBinaryData(sku_bin_data)
            self.log("Sku Value: " + str(sku_val))

            if sku_val is None:
                AbnormalTermination("Unable to extract Sku Value from binary", nverror.NvError_BadParameter)

            self.targetConfig.n_SkuValue = sku_val

        n_softfused = False
        if self.targetConfig.is_l4t is False:
            n_softfused = self.ApplySoftfusing()

        if (self.targetConfig.mb2AppletBarrier is not None):
            try:
                self.targetConfig.mb2AppletBarrier.wait(timeout=5)
            except BrokenBarrierError as err:
                print(f"Process {os.getpid()} timed out waiting on barrier")
                raise

        if (self.targetConfig.dieId == "die0"):
            if self.targetConfig.s_AppletCmd != "":
                self.Mb2AppletReset(self.targetConfig.s_AppletCmd)
            else:
                self.Mb2AppletReset("recovery")

        if n_softfused == True:
            self.vlog("Exiting out of flashing process because soft fusing is applied. " \
                      "Plesae run flashing command again if needs to")
            exit(0)

        #loop waiting for device to be removed from usb space
        for target in self.targetConfig.s_TargetDeviceInfo:
            s_PortPath = self.targetConfig.s_TargetDeviceInfo[target]["portPath"]
            s_PortPath = s_PortPath.rstrip()
            timeout = 0
            while (timeout <= 5) and (os.path.exists(s_PortPath) == True):
                time.sleep(0.05)
                timeout += 0.05

    def BootRCM_CreateFlash(self, l_operation, l_findBoardOnly=False):
        self.vlog("Creating images  - "  + l_operation)

        if (l_operation == "rcm-boot"):
            self.targetConfig.sysMonitor.vlog("Creating rcm-flash images")
            flashingCFG = os.path.join(self.targetConfig.p_FlashingCfgPath, self.targetConfig.f_FlashingCfg)
            self.targetConfig.n_WaitOnQueue = True
            self.targetConfig.n_GenBinsOnly = True
            self.CreateVDKImages(flashingCFG, self.targetConfig.p_OutDirPath, "rcm-flash")
            self.targetConfig.sysMonitor.vlog("Creating rcm-boot images")
            self.targetConfig.n_WaitOnQueue = False
            self.CreateVDKImages(self.targetConfig.f_FlashCfg, self.targetConfig.p_OutDirPath, "rcm-boot")
            self.targetConfig.n_GenBinsOnly = False
            # Run Platform  Detection
            self.runPlatformDetection(l_operation, self.targetConfig.p_OutDirPath, l_findBoardOnly)
            self.BootRCM_Orin_FPGA("rcm-boot", self.targetConfig.p_OutDirPath)
        elif ("rcm-flash" in l_operation):
            self.targetConfig.sysMonitor.vlog("Creating rcm-flash images")
            flashingCFG = os.path.join(self.targetConfig.p_FlashingCfgPath, self.targetConfig.f_FlashingCfg)
            self.vlog("flashingCFG  :: " + flashingCFG  + "self.targetConfig.p_OutDirPath :: " + self.targetConfig.p_OutDirPath)
            self.targetConfig.n_WaitOnQueue = True
            self.targetConfig.n_GenBinsOnly = True
            self.CreateVDKImages(flashingCFG, self.targetConfig.p_OutDirPath, l_operation)
            self.targetConfig.n_WaitOnQueue = False
            if (not l_findBoardOnly):
                self.CreateVDKImages(self.targetConfig.f_FlashCfg, self.targetConfig.p_OutDirPath, "flash-images")
            self.targetConfig.n_GenBinsOnly = False
            # Run Platform  Detection
            self.runPlatformDetection(l_operation, self.targetConfig.p_OutDirPath, l_findBoardOnly)
            if (not l_findBoardOnly):
                self.BootRCM(l_operation, self.targetConfig.p_OutDirPath)

    def Boot_Create_Flash_Coldboot(self):
        self.vlog("Creating & Flashing images  ")
        self.targetConfig.sysMonitor.vlog("Creating rcm-flash images")
        flashingCFG = os.path.join(self.targetConfig.p_FlashingCfgPath, self.targetConfig.f_FlashingCfg)
        self.vlog("flashingCFG  :: " + flashingCFG  + "self.targetConfig.p_OutDirPath :: " + self.targetConfig.p_OutDirPath)
        self.targetConfig.n_WaitOnQueue = True
        self.targetConfig.n_GenBinsOnly = True
        self.CreateVDKImages(flashingCFG, self.targetConfig.p_OutDirPath, "rcm-flash")
        self.vlog("Generating cold boot flash-images")
        self.targetConfig.n_WaitOnQueue = False
        self.CreateVDKImages(self.targetConfig.f_FlashCfg, self.targetConfig.p_OutDirPath, "flash-images")
        self.targetConfig.n_GenBinsOnly = False
        self.vlog("Booting Flashing Kernel")
        # Run Platform  Detection
        self.runPlatformDetection("rcm-flash", self.targetConfig.p_OutDirPath, l_findBoardOnly=False)
        self.BootRCM("rcm-flash", self.targetConfig.p_OutDirPath)
        self.vlog("Flashing cold boot Images")
        self.FlashImages(self.targetConfig.p_OutDirPath)

    def mergeChains(self):
        """Merge chains given output directories of images for each chain.
           Also generates BR BCT blob layout file for Asymmetric boot chains.

        Raises:
            AbnormalTermination: Raises exception if there are no merge chains specified
        """
        # Copy all the files from second - last chain into first chain
        if (self.targetConfig.mergeChains == None):
            raise AbnormalTermination("Couldn't find anything to merge!", nverror.NvError_InvalidArgument)

        outputDir = self.targetConfig.p_OutDirPath

        # Remove destination directory if it already exists
        if (os.path.exists(outputDir)):
            shutil.rmtree(outputDir)

        primaryChainIndex = 0
        if (self.targetConfig.n_Asymmetric):
            primaryChainIndex = 1

        # Get sorted chain directories
        sortedChainArgsDict =  collections.OrderedDict(sorted(self.targetConfig.mergeChains.items()))

        # Copy everything from first chain to output directory
        shutil.copytree(list(sortedChainArgsDict.items())[0][1], outputDir)

        outputFlashImagesDir = glob.glob(outputDir + os.sep + "[0-9]*")[0]
        outputFlashImagesDir = os.path.join(outputFlashImagesDir, "flash-images")
        outputFileToFlash = os.path.join(outputFlashImagesDir, "FileToFlash.txt")

        # Add main chain FileToFlash.txt to the input FileToFlash.txt list
        if (self.targetConfig.n_Asymmetric):
            # Main chain is chain B for asymmetric
            mainChainFlashImagesDir = glob.glob(list(sortedChainArgsDict.items())[primaryChainIndex][1] + os.sep + "[0-9]*")[0]
        else:
            # Main chain is first chain for default case
            mainChainFlashImagesDir = glob.glob(list(sortedChainArgsDict.items())[primaryChainIndex][1] + os.sep + "[0-9]*")[0]

        mainChainFlashImagesDir = os.path.join(mainChainFlashImagesDir, "flash-images")

        inputFileToFlashList = []

        for chainId, chainDir in list(sortedChainArgsDict.items()):
            # Only copy chain specific images for rest of the chains
            # Get flash-images directory for input and output
            inputFlashImagesDir = glob.glob(chainDir + os.sep + "[0-9]*")[0]
            inputFlashImagesDir = os.path.join(inputFlashImagesDir, "flash-images")
            inputFileToFlashList.append(os.path.join(inputFlashImagesDir, "FileToFlash.txt"))

            if (chainId == list(sortedChainArgsDict.items())[0][0]):
                continue

            for name in os.listdir(inputFlashImagesDir):
                path = os.path.join(inputFlashImagesDir, name)
                if (os.path.isfile(path) and not name.startswith(list(sortedChainArgsDict.items())[0][0])):
                    shutil.copy(path, outputFlashImagesDir)

        cwd = os.getcwd()
        os.chdir(outputFlashImagesDir)

        if (self.targetConfig.n_Asymmetric):
            asymmetric = AsymmetricBct(self)

            src_dict, dst_dict = asymmetric.get_chain_data_from_l1pt(
                os.path.join(os.path.dirname(inputFileToFlashList[0])),
                mainChainFlashImagesDir, # First chain A
                "A",
                "B"
            )

            result = asymmetric.make_chain_compatible(
                os.path.join(os.path.dirname(inputFileToFlashList[0])),
                src_dict,
                dst_dict,
                "A"
            )

            if (not result):
                raise AbnormalTermination(
                    "Asymmetric chain A is not compatible with chain B",
                    nverror.NvError_InvalidState
                )

            asymmetric.generate_bct_blob_layout(outputFlashImagesDir)

        self.MergeFileToFlash(inputFileToFlashList, outputFileToFlash, chr(ord('A') + primaryChainIndex))

        os.chdir(cwd)
