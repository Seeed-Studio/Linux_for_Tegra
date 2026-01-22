#!/usr/bin/python
# SPDX-FileCopyrightText: Copyright (c) 2017-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
# property and proprietary rights in and to this material, related
# documentation and any modifications thereto. Any use, reproduction,
# disclosure or distribution of this material and related documentation
# without an express license agreement from NVIDIA CORPORATION or
# its affiliates is strictly prohibited.

import os
import sys
import shutil
import platform
import subprocess
import time
import re
import glob
import stat
import inspect
import importlib
import uuid
import grp
import json
import fnmatch
import hashlib
import ast

from multiprocessing import Process
from bootburn_aurix import Aurix
from target_config import target_config, CustomerData
from flash_utilities import flash_utilities, shell_utilities
from flash_utilities import shell_utilities
from flashtools_nverror import nverror
from flashtools_nverror import AbnormalTermination
from bootburn_adb import bootburn_adb
from bootburn_gvssync import bootburn_gvscontainer
from asymmetric_boot_chains import AsymmetricFlashing, AsymmetricBct
from bootburn_bct import *

class bootburn_lib(object):

    n_ChipVersion = 0
    n_ChipID = 0
    targetConfig = target_config
    flashUtils = flash_utilities
    shellUtils = shell_utilities()
    gvsSync = None
    aurix = Aurix
    configFileList = []
    d_ChildrenStatus = {"active": [], "running" : False, "timeCounter" : 0, "exitcode" : 0}
    n_Timeout = 0
    s_OptionsHelp = {}
    isPython3 = False
    debugging = False
    gr_qb_append_bin = []
    gr_ist_append_bin = []
    gr_mb2_append_bin = []
    sudoRequired = False
    b_updatedFindBoardData = False

    def __init__(self, targetConfig):
        if(targetConfig.parsed_commandline and targetConfig.parsed_commandline["D"]):
            self.debugging = True
            targetConfig.sysMonitor.debuging = True

        self.board = targetConfig.s_BoardName
        self.targetConfig = targetConfig
        self.shellUtils.setMonitor(targetConfig.sysMonitor)
        self.shellUtils.exceptlog = True
        self.flashUtils = targetConfig.flashUtils
        self.defaultPaths = targetConfig.boardDefaultPaths

        if(self.targetConfig.s_McuPort):
            self.aurix = Aurix(self, self.targetConfig.s_McuPort)
        self.gvsSync = bootburn_gvscontainer(self.targetConfig)
        if(self.targetConfig.TEST_AUTOMATION == True):
            self.gvsSync.GetAutoLockFilePath()

        self.p_PlatformBCT = self.targetConfig.p_PlatformBCT
        self.p_BCT = self.targetConfig.p_bctPath
        self.p_TempDumpPath = self.targetConfig.p_TempDumpPath

        if sys.hexversion >= 0x3000000:
            self.log("python 3 used")
            self.isPython3 = True
        try:
            if(targetConfig.parsed_commandline["user"] == None):
                user = self.GetUser()
                targetConfig.parsed_commandline["user"] = [user]
        except:
            #if no user no files will be chown'ed
            pass

    def retGVSsync(self):
        return self.gvsSync

    def ProcessIsChild(self):
        return self.targetConfig.d_ProcessInfo["child"]

    def WaitOnImageGeneration(self, processes):
        # Assuming worst case transfer rate of 100 KB/s
        transferRate = 100000
        self.n_Timeout = 60 * 30
        loop = True
        while (loop and (processes > 0)):
            i = 0
            self.d_ChildrenStatus["timeCounter"] = 0
            spaceList = []
            self.targetConfig.n_SpaceRequired = 0
            while ((i < processes) and (self.d_ChildrenStatus["running"] == True)):
                if (self.targetConfig.d_ProcessInfo["ctpQueue"].empty() == False):
                    spaceInfo = self.targetConfig.d_ProcessInfo["ctpQueue"].get()
                    spaceList.append(spaceInfo[0])
                    self.targetConfig.n_SpaceRequired += spaceInfo[0]
                    loop = spaceInfo[1]
                    i += 1
                else:
                    # For every 2 seconds check child processes status.
                    self.CheckTimeout(self.n_Timeout)
            if not self.d_ChildrenStatus["running"]:
                break

            self.HostSpaceCheck()
            self.n_Timeout = int(max(spaceList) / transferRate)
            for i in range(processes):
                self.targetConfig.d_ProcessInfo["ptcQueue"].put("OK")

    def AdbServerManager(self, processes):
        check_status = True
        kill_times = 1
        j = 0
        adbObj = bootburn_adb(self.targetConfig, self.flashUtils)
        if self.d_ChildrenStatus["running"] == True:
            self.d_ChildrenStatus["timeCounter"] = 0
            self.n_Timeout = 60 * 5
        while ((self.d_ChildrenStatus["running"] == True) and (check_status == True)):
            i = 0
            device_found = True
            while ((i < processes) and (self.d_ChildrenStatus["running"] == True)):
                if (self.targetConfig.d_ProcessInfo["ctpQueue"].empty() == False):
                    response = self.targetConfig.d_ProcessInfo["ctpQueue"].get()
                    device_found = device_found and response
                    i += 1
                else:
                    # For every 2 seconds check child processes status.
                    self.CheckTimeout(self.n_Timeout)

            if (not self.d_ChildrenStatus["running"]):
                break

            if (device_found == True):
                child_check_adb = False
                check_status = False
            else:
                 # restart ADB server
                if (j < kill_times):
                    adbObj.RestartAdbServer(self.sudoRequired)
                    child_check_adb = True
                else:
                    for p in self.d_ChildrenStatus["active"]:
                        if p.is_alive():
                            p.terminate()
                    AbnormalTermination("${s_ERROR_DEVICE_NOT_FOUND} -- ADB Device not found online", nverror.NvError_DeviceNotFound)
            for i in range(processes):
                self.targetConfig.d_ProcessInfo["ptcQueue"].put(child_check_adb)
            j += 1

    def WaitOnFlashing(self):
        if self.d_ChildrenStatus["running"] == True:
            self.d_ChildrenStatus["timeCounter"] = 0
            self.n_Timeout = 60 * 15
        # For every 2 seconds check child processes status.
        while self.d_ChildrenStatus["running"] == True:
            while (self.targetConfig.d_ProcessInfo["ctpQueue"].empty() == False):
                self.targetConfig.d_ProcessInfo["ctpQueue"].get()
                self.d_ChildrenStatus["timeCounter"] = 0
            self.CheckTimeout(self.n_Timeout)

    def CheckTimeout(self, timeout):
        killAll = False
        itr = 2
        active = []
        try:
            for process in self.d_ChildrenStatus["active"]:
                process.join(timeout=itr)
                if process.exitcode == None:
                    active.append(process)
                    self.d_ChildrenStatus["timeCounter"] += itr
                else:
                    if (process.exitcode != 0):
                        self.d_ChildrenStatus["exitcode"] = process.exitcode
                        killAll = True

            self.d_ChildrenStatus["active"] = active
            if ((len(self.d_ChildrenStatus["active"]) == 0) or (self.d_ChildrenStatus["timeCounter"] > timeout) or (killAll == True)):
                self.d_ChildrenStatus["running"] = False
                for p in self.d_ChildrenStatus["active"]:
                    if p.is_alive():
                        p.terminate()
        except Exception as e:
            AbnormalTermination("s_ERROR_TIMEOUT -- " + str(e), nverror.NvError_Unknown)

    def GetUser(self):
        user = None
        consoleOut = True
        try:
            user = self.shellUtils.executeShellCommand("whoami", True, consoleOut)
            if(isinstance(user, int) or not user):
                return None
            return user.rstrip()
        except Exception as e:
            return None

    def functionName(self):
        return inspect.stack()[1][3]

    def formatLogMessage(self, msg):
        curframe = inspect.currentframe()
        stack = inspect.getouterframes(curframe, 2)
        caller = stack[2][3]
        lineNum = stack[2][2]
        msg ="[" + caller + "(" + str(lineNum) + ")] : " + msg
        return msg

    def log(self, msg):
        msg = self.formatLogMessage(msg)
        if(self.debugging):
            self.targetConfig.sysMonitor.log(msg, True)
        else:
            self.targetConfig.sysMonitor.log(msg, False)
        sys.stdout.flush()

    def vlog(self, msg):
        msg = self.formatLogMessage(msg)
        self.targetConfig.sysMonitor.log(msg, True)
        sys.stdout.flush()

    def call_trace(self, frame, event, arg):
        fmco = frame.f_code
        fnnm = fmco.co_name
        lnnm = frame.f_lineno
        flnm = fmco.co_filename
        clfm = frame.f_back
        if ((event == 'call') and (('bootburn' in flnm) or ('target' in flnm) or ('flash' in flnm)) and (not('external' in flnm))):
            callgraph = '   *** call to function:{} - line:{} in {}'.format(fnnm, lnnm, flnm)
            self.targetConfig.sysMonitor.calltracelog(callgraph)
            if(clfm != None):
                cllnnm = clfm.f_lineno
                clflnm = clfm.f_code.co_filename
                clfnnm = clfm.f_code.co_name
                callergraph = '         from function:{} - line:{} in {}'.format(clfnnm, cllnnm, clflnm)
                self.targetConfig.sysMonitor.calltracelog(callergraph)
            return
        return


    def Usage(self, GenericOptions):

        # self.trace(self.functionName() + " entered")
        self.vlog("      ******Usage Help******    ")
        usageStr=""
        l_help_text_file="bootburn_help_options.txt"

        fid = open(l_help_text_file, "r")
        if(fid == -1):
            AbnormalTermination("Failed to open " + l_help_text_file, nverror.NvError_FileOperationFailed)

        help_data = fid.read()
        fid.close()

        textLines = help_data.splitlines()
        for line in textLines:
            option_n_text = line.split(", ", 1)
            if (not(len(option_n_text))) or (len(option_n_text) == 1):
                continue  #not well formed lines are ignored including comment lines
            if not (option_n_text[0].strip() in GenericOptions) and option_n_text[0].strip() != "customer-data":
                continue
            if (self.targetConfig.SAFETY_BUILD):
                if('Common::' in option_n_text[1]):
                    usageStr0 = option_n_text[0] + " : " + (option_n_text[1].replace("Common::", "",1))
                    usageStr = usageStr + usageStr0 + "\r\n"
            else:
                usageStr0 = option_n_text[0] + " : " +  ((option_n_text[1].replace("Standard::", "",1)).replace("Common::","",1))
                usageStr = usageStr + usageStr0 + "\r\n"
        usageStr = "\r\n" + usageStr
        self.vlog(usageStr)

    def initFindBoardOS(self, isPdkPackage, DirBeingRunFrom,  commandLineArguments):

        boardNameFile = os.path.join(DirBeingRunFrom, "..", "bootburn", self.targetConfig.f_findBoardName)
        self.targetConfig.f_findBoardName = boardNameFile
        if (os.path.isfile(boardNameFile)):
            os.remove(boardNameFile)

        if not ('find_board_name' in str(commandLineArguments)):
            return

        addQnx = False
        if(isPdkPackage):
            pdkQnxPath = os.path.join(DirBeingRunFrom, "..", "..","..", "..", self.targetConfig.NV_SDK_NAME_QNX)
            if os.path.isdir(pdkQnxPath):
                addQnx = True
                self.log("sdk package and qnx found")
        else:
            devOutPath = os.path.join(os.getcwd() + "/../../../../out")
            for entry in os.listdir(devOutPath):
                if(fnmatch.fnmatch(entry, 'embedded-qnx*')):
                    addQnx = True
                    break
        if(addQnx):
            self.targetConfig.b_findBoardinNativeQnx = True
        else:
            self.targetConfig.b_findBoardinNativeLinux = True

    def updateFindBoardData(self):
        baseBoardDetails = ""
        socSkuDetails = ""

        if (self.targetConfig.b_findBoardName == False):
            return
        if (self.b_updatedFindBoardData):
            self.log("already updated the boarddata, so no more update")
            return
        if (len(self.targetConfig.s_baseBoardName) > 0):
            baseBoardDetails = " baseBoard name - " +  str(self.targetConfig.s_baseBoardName)
        if (len(self.targetConfig.s_SocSkuBoardModifier) > 0):
            socSkuDetails = " : chipSku is - " + str(self.targetConfig.s_SocSkuBoardModifier)
        self.vlog("Connected Board Name details :: " + baseBoardDetails + socSkuDetails)

        if (len(self.targetConfig.s_DeviceType) > 0):
            self.targetConfig.s_findBoardDataDict['DeviceType'] =  self.targetConfig.s_DeviceType
        if (len(self.targetConfig.s_FuseStatus) > 0):
            self.targetConfig.s_findBoardDataDict['FuseStatus'] =  self.targetConfig.s_FuseStatus
        if (len(self.targetConfig.s_baseBoardName) > 0):
            self.targetConfig.s_findBoardDataDict['BaseBoardName'] = self.targetConfig.s_baseBoardName
        if (len(self.targetConfig.s_SocSkuName) > 0):
            self.targetConfig.s_findBoardDataDict['SocSkuName'] = self.targetConfig.s_SocSkuName
        if (len(self.targetConfig.s_boardFusedForSigning) > 0):
            self.targetConfig.s_findBoardDataDict['FusedForSigning'] = self.targetConfig.s_boardFusedForSigning
        if (len(self.targetConfig.s_boardFusedForEncryption) > 0):
            self.targetConfig.s_findBoardDataDict['FusedForEncryption'] = self.targetConfig.s_boardFusedForEncryption
        if (self.targetConfig.s_SocSkuName == "TA1080SA"):
            self.targetConfig.s_findBoardDataDict['bind'] = "ENABLE_THOR_U=y"
            split_baseBoardName = self.targetConfig.s_baseBoardName.split(".")
            self.targetConfig.s_findBoardDataDict['config'] = split_baseBoardName[0]+"_thor_u.json"
        else:
            self.targetConfig.s_findBoardDataDict['bind'] = "none"
            self.targetConfig.s_findBoardDataDict['config'] = "none"

        # In Earlier releases, the  perfect sku is denoted by
        # base_board name only. No chip sku extension added to the board name
        #To keep backward compatibility with the earlier release,
        # the chipsku string "ct00" is mmapped to empty string.
        if (self.targetConfig.s_SocSkuBoardModifier == "ct00"):
            self.log("INT board with F0 chip-sku, removing ct00 in the board name detected for backward compatibility")
            l_socSku = ""
        else:
            l_socSku = self.targetConfig.s_SocSkuBoardModifier
        if (len(self.targetConfig.s_baseBoardName) > 0) and (len(l_socSku) > 0):
            self.targetConfig.s_findBoardDataDict['BoardName'] = self.targetConfig.s_baseBoardName + "-" + l_socSku

        elif (len(self.targetConfig.s_baseBoardName) > 0):
            self.targetConfig.s_findBoardDataDict['BoardName'] = self.targetConfig.s_baseBoardName

        for key, val in self.targetConfig.s_findBoardDataDict.items():
            self.targetConfig.s_FindBoardStr += val + "  "
        with open(self.targetConfig.f_findBoardName, 'w') as bfd:
            bfd.write(self.targetConfig.s_FindBoardStr)
        self.b_updatedFindBoardData = True

    def writeToFile(self, fid, data):
        result = 0;
        if(isinstance(data, str) == False):
            data = data.encode("utf-8")
            data = str(data)

        if(self.isPython3 == True):
            result = fid.write(data)
        else:
            result = fid.write(bytes(data))

        return result

    def DisplayHostInfoAndPaths(self):
        l_nvQbDtb = ""
        if("f_NvQbDtbImage" in self.targetConfig.boardDefaultPaths):
            l_nvQbDtb = self.targetConfig.boardDefaultPaths["f_NvQbDtbImage"]

        n_RcmMode = self.targetConfig.parsed_commandline["R"]
        [system, node, release, version, machine, processor] = platform.uname()
        self.vlog("\n")
        self.vlog ("############## Host machine info ##############")
        self.vlog ("Host system " + system)
        self.vlog ("Host node " + node)
        self.vlog ("Host release " + release)
        self.vlog ("Host version " + version)
        self.vlog ("Host machine " + machine)
        self.vlog ("Host processor " + processor)
        self.vlog ("Target ChipId 0x{}  ".format(self.targetConfig.n_ChipID) + "ChipVersion 0x{}".format(self.targetConfig.n_ChipVersion))
        self.vlog ("###############################################")
        self.log ("##############   Using Binaries ###############")
        self.log ("###############################################")
        self.vlog("\n")

        if (self.targetConfig.f_BpmpDtbNameAbs):
            self.log ("BPMP DTB: " + self.targetConfig.f_BpmpDtbNameAbs)
        elif (self.targetConfig.f_BpmpDtbName):
            self.log ("BPMP DTB: " + os.path.join(self.targetConfig.p_BpmpFwDtb, self.targetConfig.f_BpmpDtbName))
        else:
            self.log ("BPMP DTB: " + os.path.join(self.targetConfig.p_BpmpFwDtb, self.targetConfig.boardDefaultPaths["f_BpmpFwDtb"]))

        if (n_RcmMode == False):
            self.log ("RCM_Flashing DTB: " + os.path.join(self.targetConfig.p_FlashingDtbPath, self.targetConfig.boardDefaultPaths["f_FlashingDtbImg"]))
            self.log ("RCM_Flashing Cfg: " + os.path.join(self.targetConfig.p_FlashingCfgPath, self.targetConfig.f_FlashingCfg))

        self.log ("Config file: " + self.targetConfig.f_FlashCfg)
        if (self.targetConfig.n_FlashHypervisor is False):
            if (self.targetConfig.f_StorageDtbNameAbs):
                self.log ("Storage DTB: " + self.targetConfig.f_StorageDtbNameAbs)
            elif (self.targetConfig.f_StorageDtbName):
                self.log ("Storage DTB: " + os.path.join(self.targetConfig.p_StorageDtbPath, self.targetConfig.f_StorageDtbName))
            else:
                self.log ("Storage DTB: " + os.path.join(self.targetConfig.p_StorageDtbPath, self.targetConfig.boardDefaultPaths["f_DtbImg"]))

        else:
            self.log ("HyperVisorImage: " + self.targetConfig.p_HyperVisorCfgsPath)

        if (self.targetConfig.p_OutDirPath):
            self.log ("Generated Image Path: " + self.targetConfig.p_OutDirPath)

        self.log ("SDRAM Config: " + self.targetConfig.boardDefaultPaths["f_MB1SdRamParam"])
        self.log ("ADB tool: " + self.flashUtils.f_AdbTool)
        self.log ("###############################################")

    def HostSpaceCheck(self):
        statvfs = os.statvfs(self.targetConfig.p_OutDirPath)
        HostSpaceAvail = statvfs.f_frsize * statvfs.f_bavail

        if (self.targetConfig.n_SpaceRequired > HostSpaceAvail):
            for p in self.d_ChildrenStatus["active"]:
                if p.is_alive() == True:
                    p.terminate()
            error_msg = "Bootburn requires %s bytes to proceed with image generation but free available space in host is %s\n" % (format(self.targetConfig.n_SpaceRequired, ","), format(HostSpaceAvail, ","))
            error_msg += "Free some space and try again!"
            AbnormalTermination("s_ERROR_INSUFFICIENT_DISK_SPACE\n" + error_msg, nverror.NvError_InsufficientHostSpace)

    def CalculateDirSize(self, path):
        size = 0
        for dirpath, dirname, filenames in os.walk(path):
            for f in filenames:
                fp = os.path.join(dirpath, f)
                if not os.path.islink(fp):
                    size += os.path.getsize(fp)
                else:
                    destFile = os.path.join(dirpath, os.readlink(fp))
                    try:
                        size += os.path.getsize(destFile)
                    except OSError:
                        pass

        return int(size * 1.1) # for the symlinks that were not calculated

    def CalculateRequiredSpace(self, configFiles, n_SkipFS, n_RcmBoot):

        self.targetConfig.n_SpaceRequired = 0
        BCH_size = 4100
        identifiers = ["dirname", "imagepath", "filename", "ramdisk_path", "rcm_filename"]
        bct_list = ["bct", "mb1-bct", "mem-bct", "pt"]
        filesystem_types = ["ext2", "ext3", "ext4", "qnx"]
        isPartition = False
        useCfgSize = False
        filesystem = False

        for cfg in configFiles:
            configData = self.shellUtils.catFile(cfg)
            configData = configData.splitlines()
            cfg_size = 0

            for line in configData:
                if line == "" or line[0] == "#":
                    continue
                if "[partition]" in line:
                    filesystem = False
                    isPartition = True
                    useCfgSize = False
                elif "[device]" in line:
                    isPartition = False
                else:
                    if isPartition == True:
                        key_value = line.split("=")
                        if len(key_value) != 2:
                            continue
                        else :
                            [key, value] = key_value

                        if key == "filesystem_type" and value in filesystem_types:
                            filesystem = True
                            continue

                        if (filesystem and (n_SkipFS or n_RcmBoot)):
                            continue

                        if key == "name" and value in bct_list:
                            useCfgSize = True

                        if key == "size" and useCfgSize == True:
                            cfg_size += int(value, 16)

                        if key in identifiers:
                            if os.path.isfile(value):
                                cfg_size += os.path.getsize(value) + BCH_size

                            if os.path.isdir(value):
                                cfg_size += self.CalculateDirSize(value)

            if n_RcmBoot:
                cfg_size *= 2
            self.targetConfig.n_SpaceRequired += cfg_size

    def convertBase(self, num, b):
        convStr = "0123456789abcdefghijklmnopqrstuvwxyz"
        if num < b:
            return convStr[num]
        else:
            return self.convertBase(num // b, b) + convStr[num % b]

    # Validate Target with bom_data_file.txt
    # confirm the target unique_id is valid for the board
    def ValidateTarget(self):
        bomDataFile = self.targetConfig.p_BoardConfigsPath + "/" + self.targetConfig.f_BomDataTableFile
        minSysObjVer = self.targetConfig.n_minSysVerforTargetValidation

        if(self.targetConfig.s_McuPort != None):
            if(False == self.aurix.aurixValidateTarget(self.board, bomDataFile, minSysObjVer)):
                AbnormalTermination("s_ERROR_TARGET_MISMATCH -- target Validation failed", nverror.NvError_TargetValidationFail)

        self.vlog("target validation is successfully completed")

    # Generate Adb Serial Number from s_ECID
    # Used to identify/connect to ADB device.
    # Follows the same algorithm that QB uses for SerialNum
    def GenAdbSerialNum(self):
        self.targetConfig.s_AdbSerialNum = ""
        l_Base32 = ""
        if(self.targetConfig.s_ECID):
            l_ECID = self.targetConfig.s_ECID[2:]
            l_ECID = int(l_ECID, 16)
            l_Base32 = self.convertBase(l_ECID, 32)
            l_Base32 = l_Base32.upper()

        self.targetConfig.s_AdbSerialNum = self.targetConfig.s_AdbSerialNum + l_Base32
        if(len(self.targetConfig.s_TargetDeviceInfo) == 1):
            for target in self.targetConfig.s_TargetDeviceInfo:
                self.targetConfig.s_TargetDeviceInfo[target]["adbSerial"] = self.targetConfig.s_AdbSerialNum

        self.vlog("ADB serial number is " + self.targetConfig.s_AdbSerialNum)

    def SetOutPutDirName(self, l_OutDirPath):
        l_SkuNum = None
        l_SkuVer = None

        if "flash_bsp" not in self.targetConfig.sysMonitor.identifier:
            customerData = {}
            if (self.targetConfig.f_SignedCustomerData != None):
                with open(self.targetConfig.f_SignedCustomerData, "r") as f_signedcustData:
                    customerData = json.load(f_signedcustData)["customer-data-signed"]

            signedCustomerData = CustomerData(customerData, "customer-data-signed")
            if (not signedCustomerData.hasKey("skuNumber")):
                if(self.targetConfig.parsed_commandline["vdk"]):
                    l_SkuNum = "940-VDK-2200-000"
                elif(self.targetConfig.parsed_commandline["fpga"]):
                    l_SkuNum = "940-FPGA-2200-000"
                else:
                    l_SkuNum = "940-THOR-2200-000"

                l_SkuVer = "VD"
            else:
                l_SkuNum = signedCustomerData.getValue("skuNumber")
                if (isinstance(l_SkuNum, dict)):
                    l_SkuNum = l_SkuNum["value"]
                if signedCustomerData.hasKey("skuVersion"):
                    l_SkuVer = signedCustomerData.getValue("skuVersion")

        if(not l_SkuNum):
            AbnormalTermination("SKU number not set in target configuration", nverror.NvError_InvalidConfigVar)

        if(not l_SkuVer):
            l_SkuVer = "XX"

        self.targetConfig.p_OutDirPath = os.path.join(l_OutDirPath, l_SkuNum.strip() + "_" + l_SkuVer)
        self.targetConfig.f_OutFile = os.path.join(self.targetConfig.p_OutDirPath, self.targetConfig.f_OutFile)

    def CreatePlatInfoFile(self, l_dir, l_Txtle):
        self.vlog ("****  Entering CreatePlatInfoFile ****")
        l_HypConfigTypele = ""
        l_HypConfig = ""
        l_SecdbgctrlInCfg = self.GetBctSecdbgctrl(self.targetConfig.f_FlashCfg)
        l_EcidInCfg = self.GetBctPartEcid(self.targetConfig.f_FlashCfg)
        l_Txtle = os.path.join(l_dir, l_Txtle)
        self.vlog("TagetInfo file :: " + str(l_Txtle))

        l_fileContents = "TargetBoard=" + self.targetConfig.s_BoardName + "\n"
        l_fileContents += "EngSample=" + str(self.targetConfig.n_EngSample) + "\n"
        l_fileContents += "ChipId=" + str(self.targetConfig.n_ChipId) + "\n"
        l_fileContents += "ChipRevision=" + format(self.targetConfig.n_ChipVersion, '02x') + "\n"
        l_fileContents += "SecureTarget=" + str(self.targetConfig.n_SecurebootFlash) + "\n"
        l_fileContents += "EncryptedImages=" + str(self.targetConfig.n_EncryptedbootFlash) + "\n"
        l_fileContents += "ConfigFile=" + self.targetConfig.f_FlashCfg + "\n"
        l_fileContents += "DebugBinUsed=" + str(self.targetConfig.n_UseDebugBins) + "\n"
        l_fileContents += "BuildVersion=" + str(self.targetConfig.s_buildVersion) + "\n"
        l_fileContents += "CustomerDataPreserved=" + str(self.targetConfig.b_IsBrBctSignedCustomerDataPreserved) + "\n"

        if (self.targetConfig.s_UpdatePartitions):
            l_fileContents += "PartialCreate=" + str(' '.join(self.targetConfig.s_UpdatePartitions)) + "\n"
        #data dictionary for partial flashing with PVIT
        l_fileContents +=  "PartialUpdate=" + str(self.targetConfig.d_PvitChainNPartitions) + "\n"

        if (self.targetConfig.b_IsMultiSkuEnabled):
            l_fileContents += "MultiSkuEnabled=" + str(self.targetConfig.b_IsMultiSkuEnabled) + "\n"

        if (self.targetConfig.n_FlashHypervisor is True):
            l_HypConfigTypele = os.path.join(self.targetConfig.p_HyperVisorCfgsPath, "pct_type.txt")
            l_HypConfig = self.shellUtils.catFile(l_HypConfigTypele)
            l_fileContents = l_fileContents + "Hypervisor=" + str(self.targetConfig.n_FlashHypervisor) + "\n"
            l_fileContents = l_fileContents + "HypervisorConfig=" + l_HypConfig

        if ((int(l_SecdbgctrlInCfg, 16) & self.targetConfig.n_EnableJtagBit) > 0  and l_EcidInCfg):
            l_fileContents = l_fileContents + "Ecid=" + l_EcidInCfg + "\n"
            l_fileContents = l_fileContents + "SecureDebugValue=" + str(l_SecdbgctrlInCfg) + "\n"

        if (self.targetConfig.parsed_commandline["customer_data"] != None):
            # dump the values in json file
            customerDataJsonFilePath = os.path.join(l_dir, "create_bsp_customer_data.json")
            if (self.targetConfig.n_SecurebootFlash is False) and (self.targetConfig.n_Asymmetric is False):
                self.targetConfig.signedCustomerData.dumpBothToFile(self.targetConfig, customerDataJsonFilePath)
            else:
                self.targetConfig.customerData.dumpToFile(customerDataJsonFilePath)
            l_fileContents += "PreserveSKUFile=" + "create_bsp_customer_data.json" + "\n"

        if (self.targetConfig.GetChipFamily() == "t264"):
            l_fileContents += "Sku=" + self.targetConfig.boardDefaultPaths["s_Skuname"] + "\n"
            l_fileContents += "Int=" + self.targetConfig.boardDefaultPaths["s_Int"] + "\n"
            if (self.targetConfig.b_SkuCheck == True):
                l_fileContents += "b_SkuCheck=1"
            else:
                l_fileContents += "b_SkuCheck=0"

        fid = open(l_Txtle, "w+", stat.S_IREAD | stat.S_IWRITE | stat.S_IRGRP | stat.S_IWGRP | stat.S_IROTH | stat.S_IWOTH)
        if(fid == -1):
            AbnormalTermination("Failed to open " + l_Txtle, nverror.NvError_FileOperationFailed)

        self.writeToFile(fid, l_fileContents)
        fid.close()

    def ParsePlatInfoFile(self, l_dir, l_Txtle):
        l_Txtle = os.path.join(l_dir, l_Txtle)

        if(not l_dir or not l_Txtle):
            AbnormalTermination("Empty string passed to ParsePlatInfoFile", nverror.NvError_BadParameter)

        if(not os.path.isfile(l_Txtle)):
            AbnormalTermination("Invalid path " + l_Txtle, nverror.NvError_FileNotFound)

        fid = open(l_Txtle, "r")
        if(fid == -1):
            AbnormalTermination("Failed to open " + l_Txtle, nverror.NvError_FileOperationFailed)

        data = fid.read()
        fid.close()

        textLines = data.splitlines()

        for line in textLines:
            if(not line):
                continue

            if(line.find("TargetBoard") != -1):
                self.targetConfig.s_BinBoardName = self.getValue(line)

            elif(line.find("EngSample") != -1):
                self.targetConfig.n_EngSample = self.shellUtils.str2bool(self.getValue(line))

            elif(line.find("ChipId") != -1):
                self.targetConfig.n_BinChipId = self.getValue(line)

            elif(line.find("ChipRevision") != -1):
                self.targetConfig.n_BinChipVersion = int(self.getValue(line))

            elif(line.find("SecureTarget") != -1):
                self.targetConfig.n_SecurebootFlash = self.shellUtils.str2bool(self.getValue(line))

            elif(line.find("EncryptedImages") != -1):
                self.targetConfig.n_EncryptedbootFlash = self.shellUtils.str2bool(self.getValue(line))

            elif(line.find("DebugBinUsed") != -1):
                self.targetConfig.n_UseDebugBins = self.shellUtils.str2bool(self.getValue(line))

            elif(line.find("Ecid") != -1):
                self.targetConfig.s_EcidInImage = self.getValue(line)

            elif(line.find("SecureDebugValue") != -1):
                self.targetConfig.n_SecDbgCtrl = self.getValue(line)

            elif(line.find("ConfigFile") != -1):
                self.targetConfig.f_FlashCfg = self.getValue(line)

            elif(line.find("BuildVersion") != -1):
                self.targetConfig.s_buildVersion = self.getValue(line)
            #-u option data with create_bsp
            elif(line.find("PartialCreate") != -1):
                self.targetConfig.s_CreatePartialPartitions = (self.getValue(line)).split()
            #pvit data dictionary from create_bsp
            elif(line.find("PartialUpdate") != -1):
                self.targetConfig.d_PvitChainNPartitions = ast.literal_eval(self.getValue(line))
            elif(line.find("MultiSkuEnabled") != -1):
                self.targetConfig.b_IsMultiSkuEnabled = self.shellUtils.str2bool(self.getValue(line))
            elif(line.find("CustomerDataPreserved") != -1):
                self.targetConfig.b_IsBrBctSignedCustomerDataPreserved = self.shellUtils.str2bool(self.getValue(line))

            elif (line.find("PreserveSKUFile") != -1):
                try:
                    self.targetConfig.skuArguments["CrossCheckBins"] = True

                    # Check if customer data is already present from command line arguments
                    # if so, don't update values passed from create bsp preserve sku
                    if (not self.targetConfig.customerData.isLoaded()):
                        json_file = os.path.join(l_dir, self.getValue(line))
                        with open(json_file, "r") as f_custData:
                            customerDataJson = json.load(f_custData)
                            if ("customer-data-unsigned" in customerDataJson):
                                customerData = CustomerData(customerDataJson["customer-data-unsigned"], "customer-data-unsigned")
                                self.targetConfig.customerData.updateValuesFrom(customerData)

                            if ("customer-data-signed" in customerDataJson):
                                customerData = CustomerData(customerDataJson["customer-data-signed"], "customer-data-signed")
                                self.targetConfig.signedCustomerData.updateValuesFrom(customerData)
                except Exception as err:
                    self.vlog("Couldn't load PreserveSKUFile: " + self.getValue(line) + str(err))
                    AbnormalTermination("Couldn't load PreserveSKUFile: " + self.getValue(line), nverror.NvError_InvalidArgument)

        if (self.targetConfig.s_EcidInImage and self.targetConfig.s_ECID and self.targetConfig.s_ECID != self.targetConfig.s_EcidInImage):
            self.vlog ("\nNote: ECID supplied in flsahing cfg file does not match the board Chip Id")
            self.vlog ("ECID in image = " + self.targetConfig.s_EcidInImage + " ECID from device is " + self.targetConfig.s_ECID)
            self.targetConfig.s_EcidInImage = ""
            self.targetConfig.n_SecDbgCtrl = "0"

    def CrossCheckBinInfo(self):
        # if CrossCheckBins is not present, return because we don't have information to match with
        if(not "CrossCheckBins" in self.targetConfig.skuArguments):
            return

        if (self.targetConfig.s_BinBoardName != self.targetConfig.s_BoardName):
            self.vlog ("Board name mismatch, board intended s_BoardName")
            self.vlog ("$l_Path binares are for s_BinBoardName")
            AbnormalTermination("s_ERROR_TARGET_BOARD_TYPE", nverror.NvError_ResourceError)

        if (self.targetConfig.n_BinChipVersion != self.targetConfig.n_ChipVersion):
            self.vlog ("Chip Version mismatch, Chip detected is n_ChipVersion")
            self.vlog ("$l_Path binares are for  n_BinChipVersion")
            AbnormalTermination("s_ERROR_TARGET_CHIP_VERSION -- Chip version mismatch", nverror.NvError_ResourceError)

    def sendRcmCommand(self, tegraRcmCommand, formatRCM=True):
        if(formatRCM):
            tegraRCM = self.formatTegraRCM()
        else:
            tegraRCM = self.flashUtils.f_TegraRcm

        tegraRcmCommand = tegraRCM + " " + tegraRcmCommand
        result = self.shellUtils.executeShellCommand(tegraRcmCommand, True)
        if (isinstance(result, int)):
            self.vlog("Unable to execute tegrarcm command -- " + tegraRcmCommand)
            AbnormalTermination("TEGRARCM -- Unable to execute command", nverror.NvError_TegraRcmError)
        return result

    def StartNewSession_t264(self):
        self.UpdateTargetInstanceInfo()
        self.targetConfig.sysMonitor.log("Start New Session of Target comms")
        self.sendRcmCommand("--new_session --chip 0x26", formatRCM=True)

    def GetTargetECID(self, chipnum=""):
        self.UpdateTargetInstanceInfo()
        self.targetConfig.sysMonitor.log("Get UID of chip")
        if (chipnum == "--chip 0x26"):
            self.targetConfig.s_BR_CID = self.sendRcmCommand("--uid  --chip 0x26", formatRCM=True)
        else:
            self.targetConfig.s_BR_CID = self.sendRcmCommand("--uid", formatRCM=True)
        self.targetConfig.s_ECID = "0x" + self.targetConfig.s_BR_CID[17:]
        self.vlog ("Target " + self.targetConfig.s_BR_CID)
        self.vlog ("Target ECID: " + self.targetConfig.s_ECID)

    # bit 126 and 125 of BR_CID corresponds to SECURITY_MODE and OEM_KEY_VALID respectively
    # If SECURITY_MODE or OEM_KEY_VALID is set to 1, signing key is fused
    # Otherwise signing is not enabled on Target
    def CheckFuseForAuthentication(self):
        #This indicates the target is connected
        self.targetConfig.b_boardConnected = True

        #Get Target Autentication state
        mask = 0x6
        if (int(self.targetConfig.s_BR_CID[10], 16) & mask):
            self.targetConfig.b_boardFusedForSigning = True
            self.log("Authentication key is fused on the target\n")
        else:
            self.targetConfig.b_boardFusedForSigning = False
            self.log("Authentication key is not fused on the target\n")

    def CheckFuseForEncryption(self):
        caller = self.targetConfig.sysMonitor.identifier
        encKeyProvided = (not self.targetConfig.f_EncKeyFilePath == None)
        PlencKeyProvided = (not self.targetConfig.f_PlEncKeyFilePath == None)

        if "create_bsp" in caller:
            if encKeyProvided == True:
                self.targetConfig.n_EncryptedbootFlash = True
            if PlencKeyProvided == True:
                self.targetConfig.n_PlEncryptedbootFlash = True
        else:
            nibble = format(int(self.targetConfig.s_BR_CID[11], 16), '04b')
            if nibble[0] == '1':
                self.vlog("Encryption key is fused on the target\n")
                self.targetConfig.b_boardFusedForEncryption = True
                if "flash_bsp" in caller:
                    if self.targetConfig.n_EncryptedbootFlash == False:
                        AbnormalTermination("User tries to flash unencrypted images but encryption key is " +
                                            "burned on the target!\n Try again using the path to images that " +
                                            "have been encrypted with the expected key.", nverror.NvError_OperationNotPermitted)
                if "bootburn" in caller:
                    if encKeyProvided == True:
                        self.targetConfig.n_EncryptedbootFlash = True
                    else:
                        AbnormalTermination("Target expects encrypted images but no encryption key was " +
                                            "provided by the user!\n Specify the key (--encryption_key) to " +
                                            "encrypt the images.", nverror.NvError_FileNotFound)
                    if PlencKeyProvided == True:
                        self.targetConfig.n_PlEncryptedbootFlash = True
            else:
                if "flash_bsp" in caller:
                    if self.targetConfig.n_EncryptedbootFlash == True:
                        AbnormalTermination("User tries to flash encrypted images but no encryption key is " +
                                            "burned on the target!\n Try again using the path to images that " +
                                            "have been generated without any encryption key.", nverror.NvError_OperationNotPermitted)
                if "bootburn" in caller:
                    if encKeyProvided == True:
                        self.log("User provided an encryption key but target expects unencrypted images!\n" +
                                 "Continuing to image generation ignoring the encryption key.")

    def CheckForT26xChip(self):
        self.targetConfig.n_ChipID = self.targetConfig.s_BR_CID[14:16]
        if (self.targetConfig.n_ChipID != "26"):
            self.vlog ("Target in recovery is not T264 chip. Exiting!")
            self.vlog(self.targetConfig.s_BR_CID)
            AbnormalTermination("s_ERROR_TARGET_CHIP_VERSION -- Target not T26X chip", nverror.NvError_ResourceError)


    # check device type and set n_EngSample value accordingly
    #
    # type selected | type detected | result
    # -----------------------------------------------
    # internal      | prod          | exit with error
    # prod(bootburn)| internal      | set device type to internal
    # prod(prebuild)| internal      | exit with error (not handling for now,
    #                                  as this requires flash_bsp command line option change)

    # note: device type is selected with -M option

    def ValidateUserOptionAndDeviceType(self, preBuild=False):
        #No checking, if the user option ("M") and connected board match
        #if user option, not matching with connected board, then implement the above table
        if (self.targetConfig.n_DevBoardConnected != self.targetConfig.n_EngSample):
            if (self.targetConfig.n_DevBoardConnected == False):
                self.vlog("M option with PROD board, abort flashing dev binaries on PROD board")
                AbnormalTermination("Aborting, command option -M not allowed with PROD board", nverror.NvError_DevBinariesonProdTarget)
            elif (self.targetConfig.n_DevBoardConnected) and (not preBuild):
                self.targetConfig.n_EngSample = True
                self.vlog("INT board, generating INT binaries")

    def CheckForDeviceType(self):
        # MSB of BR_CID describes device type
        # 0 for int or 1 for prod
        deviceTypeByte = int(self.targetConfig.s_BR_CID[10], 16)
        self.log("device type = "+ self.targetConfig.s_BR_CID[10] + "  int = " + str(deviceTypeByte))
        l_n_devicetype = int(deviceTypeByte / 8)

        if (l_n_devicetype ==  1):
            # non-engineering device type
            self.vlog ("Detected ES production device type")
            self.targetConfig.n_DevBoardConnected = False
            self.targetConfig.s_DeviceType = "PROD"
        else:
            # internal device type
            self.vlog ("Detected internal engineering device type. Setting device type to internal")
            self.targetConfig.n_DevBoardConnected = True
            self.targetConfig.s_DeviceType = "INT"

    def ValidateFindBoardForFused(self):
        #check the connected board is Fused for Authentication or Encryption
        if (self.targetConfig.b_findBoardName):
            if self.targetConfig.b_boardFusedForSigning:
                self.targetConfig.s_boardFusedForSigning = "SIGNED"
            else:
                self.targetConfig.s_boardFusedForSigning = "UNSIGNED"

            if self.targetConfig.b_boardFusedForEncryption:
                self.targetConfig.s_boardFusedForEncryption = "ENCRYPTED"
            else:
                self.targetConfig.s_boardFusedForEncryption = "UNENCRYPTED"

            if self.targetConfig.b_boardFusedForSigning or self.targetConfig.b_boardFusedForEncryption:
                self.targetConfig.s_FuseStatus =  "SECURED"
                self.vlog("find_board_name is not supported on board fused for authentication and or encryption")
                AbnormalTermination("Aborting, find_board_name is not supported on board fused for authentication and or encryption", nverror.NvError_FindBoardNameonFusedBoard)
            else:
                self.targetConfig.s_FuseStatus = "NONSECURED"

    def CopyTegraSign(self, l_path):
        if (l_path == None):
            self.vlog("Output path not set")
            AbnormalTermination("${s_ERROR_FILE_NOT_EXIST} -- ${p_OutDirPath}", nverror.NvError_FileNotFound)

        if (os.path.isdir(l_path) == False):
            self.log("Output path not set creating it")
            self.shellUtils.makeDirectory(l_path)

        for tegrasignFile in self.targetConfig.flashUtils.f_TegraSign:
            self.shellUtils.Copy(tegrasignFile, l_path, False, True)

        self.shellUtils.touch(os.path.join(l_path, "__init__.py"))

        # Copy nvkey load and yaml
        for tegrasignNvFile in self.targetConfig.flashUtils.f_TegraSignPriv:
            self.shellUtils.Copy(tegrasignNvFile, l_path, False, True)

        # Copy dev keys
        for tegrasignNvKeyFile in self.targetConfig.flashUtils.f_TegraSignDevKeyFiles:
            self.shellUtils.Copy(tegrasignNvKeyFile, l_path, False, True)


    def CopyFirmwareFiles(self, l_p_OutDirPath):
        p_BurnDir = self.targetConfig.p_BurnDir
        dest = os.path.join(l_p_OutDirPath, "tools", "flashtools", "firmware")
        print ("\n \n")
        if(os.path.isfile(self.targetConfig.f_McuUpdtScriptAbs)):
            print("self.targetConfig.f_McuUpdtScriptAbs  :: "  + str(self.targetConfig.f_McuUpdtScriptAbs))
        if(os.path.isfile(self.targetConfig.f_McuFwBinAbs)):
            print("self.targetConfig.f_McuFwBinAbs  :: "  + str(self.targetConfig.f_McuFwBinAbs))

        self.shellUtils.makeDirectory(dest)

        for element in os.listdir(dest):
            print( str(element))
        print ("Before Copying Firmwares")

        if (self.targetConfig.isPDK == True):
            self.vlog("In PDK build ")
            p_FirmwareDir = os.path.join(p_BurnDir, "..", "firmware")
        else:
            p_FirmwareDir = os.path.join(p_BurnDir, "../..", "firmware")
        if (os.path.isdir(p_FirmwareDir) == True):
            self.vlog("   p_FirmwareDir  " + str(p_FirmwareDir))
            self.shellUtils.Copy(p_FirmwareDir, dest, False)
        else:
            AbnormalTermination("Failed to open " + p_FirmwareDir, nverror.NvError_FileOperationFailed)
        print("p_FirmwareDir  :: " + str(p_FirmwareDir))
        #initialize the mcufw updatescript and mcufw binaryi after the above folder copy
        print ("After Copying Firmwares")
        for element in os.listdir(dest):
            print( str(element))
        print ("\n \n")
        self.shellUtils.Copy(self.targetConfig.f_McuUpdtScriptAbs, dest, False, True)
        self.shellUtils.Copy(self.targetConfig.f_McuFwBinAbs, dest, False, True)

    def CopyTegraFlashOfflineUtilities(self, l_p_OutDirPath):
        p_BurnDir = self.targetConfig.p_BurnDir
        p_fsUtils = self.targetConfig.p_fsUtils
        p_SocBoardConfigsPath = self.targetConfig.p_SocBoardConfigsPath
        p_BurnStorageCfgs = self.targetConfig.p_BurnStorageCfgs
        p_TcumuxerSupport = self.targetConfig.p_TcumuxerSupport
        p_TcumuxerDir = self.targetConfig.p_TcumuxerDir

        if(self.targetConfig.isminiPDK or self.targetConfig.isPDK):
            p_BoardConfigsPath = os.path.join(os.getcwd(), '..','board_configs')
        else:
            p_BoardConfigsPath = os.path.join(os.getcwd(), '..', '..', 'board_configs')

        l_p_PdkFlashPath = os.path.join(l_p_OutDirPath, "tools", "flashtools", "flash")
        l_p_PdkBootburnPyPath = os.path.join(l_p_OutDirPath, "tools", "flashtools", "bootburn_t264_py")
        l_p_PdkBootburnCommonPyPath = os.path.join(l_p_OutDirPath, "tools", "flashtools", "bootburn")
        l_p_PdkBoardConfigsPath = os.path.join(l_p_OutDirPath, "tools", "flashtools", "board_configs")

        self.targetConfig.sysMonitor.log("Make" + l_p_PdkFlashPath + "directory for flashing")
        self.shellUtils.makeDirectory(l_p_PdkFlashPath)

        self.targetConfig.sysMonitor.log("Make " + l_p_PdkBootburnPyPath + " directory for flashing")
        self.shellUtils.makeDirectory(l_p_PdkBootburnPyPath)

        self.targetConfig.sysMonitor.log("Make " + l_p_PdkBootburnCommonPyPath + "directory for flashing")
        self.shellUtils.makeDirectory(l_p_PdkBootburnCommonPyPath)

        self.targetConfig.sysMonitor.log("Make " + l_p_PdkBoardConfigsPath + "directory for flashing")
        self.shellUtils.makeDirectory(l_p_PdkBoardConfigsPath)
        self.shellUtils.touch(os.path.join(l_p_PdkBoardConfigsPath, "miniPDK"))

        self.shellUtils.Copy(os.path.join(p_BurnDir, "..","bootburn", "flash_bsp_images.py"), l_p_PdkBootburnCommonPyPath, False, True)
        self.shellUtils.Copy(os.path.join(p_BurnDir, "..","bootburn", "select_socgrp.py"), l_p_PdkBootburnCommonPyPath, False, True)

        dirList = os.listdir(p_BurnDir)
        for file in dirList:
            [base, ext] = os.path.splitext(file)
            if(ext == ".py"):
                self.shellUtils.Copy(os.path.join(p_BurnDir, file), l_p_PdkBootburnPyPath, False, True)
            if ((ext == ".json") or (base == "bootburn_help_options")):
                self.shellUtils.Copy(os.path.join(p_BurnDir, file), l_p_PdkBootburnPyPath)

        dirList = os.listdir(p_BoardConfigsPath)
        for file in dirList:
            self.log("file in p_BoardConfigsPath : " + file)
            if (os.path.isdir(os.path.join(p_BoardConfigsPath, file))):
                tmpDir = os.path.join(l_p_PdkBoardConfigsPath, file)
                self.shellUtils.makeDirectory(tmpDir)
                subDirList = os.listdir(os.path.join(p_BoardConfigsPath, file))
                for tmpFile in subDirList:
                    self.shellUtils.Copy(os.path.join(p_BoardConfigsPath, file, tmpFile), tmpDir)
            else:
                self.shellUtils.Copy(os.path.join(p_BoardConfigsPath, file), l_p_PdkBoardConfigsPath)

        dirList = self.shellUtils.find(p_SocBoardConfigsPath, ".json")
        for file in dirList:
            self.log("file in p_SocBoardConfigsPath: " + file)
            self.shellUtils.Copy(os.path.join(p_SocBoardConfigsPath, file), l_p_PdkBoardConfigsPath)

        self.shellUtils.Copy(self.flashUtils.f_TegraRcm, l_p_PdkFlashPath, False, True)
        self.shellUtils.Copy(self.flashUtils.f_NvImageGen, l_p_PdkFlashPath, False, True)
        self.shellUtils.Copy(self.flashUtils.f_NvSkuInfo, l_p_PdkFlashPath, False, True)
        self.shellUtils.Copy(self.flashUtils.f_NvBchValidate, l_p_PdkFlashPath, False, True)
        self.shellUtils.Copy(self.flashUtils.f_TegraParser, l_p_PdkFlashPath, False, True)

        self.shellUtils.Copy(self.flashUtils.f_AdbTool, l_p_PdkFlashPath, False, True)
        adb_tool_int = self.flashUtils.f_AdbTool + "_int"
        if os.path.exists(adb_tool_int):
            self.shellUtils.Copy(adb_tool_int, l_p_PdkFlashPath, False, True)
        self.shellUtils.Copy(self.flashUtils.f_NvDDTool, l_p_PdkFlashPath, False, True)
        self.shellUtils.Copy(self.flashUtils.f_Nvpt, l_p_PdkFlashPath, False, True)
        self.shellUtils.Copy(os.path.join(p_fsUtils, "losetup"), l_p_PdkFlashPath, False, True)
        self.shellUtils.Copy(os.path.join(p_fsUtils, "e2fsck"), l_p_PdkFlashPath, False, True)
        self.shellUtils.Copy(os.path.join(p_fsUtils, "resize2fs"), l_p_PdkFlashPath, False, True)
        self.shellUtils.Copy(os.path.join(p_fsUtils, "wr_sh.sh"), l_p_PdkFlashPath, False, True)
        self.CopyTegraSign(l_p_PdkFlashPath)

        #Ufs Provision File
        dest = os.path.join(l_p_OutDirPath, "tools", "flashtools", "storage_configs", "t264")
        self.shellUtils.makeDirectory(dest)
        self.shellUtils.Copy(os.path.join(p_BurnStorageCfgs, "ufs-provision-128gb.cfg"), dest, False, True)
        self.shellUtils.Copy(os.path.join(p_BurnStorageCfgs, "ufs-provision-256gb.cfg"), dest, False, True)
        self.shellUtils.Copy(os.path.join(p_BurnStorageCfgs, "ufs-provision-512gb.cfg"), dest, False, True)

        #Copy Tcu Muxer Support Files
        dest = os.path.join(l_p_OutDirPath, "tools", "muxer", "tcu_muxer")
        self.shellUtils.makeDirectory(dest)
        if (os.path.isdir(p_TcumuxerSupport) == True):
            dirList = os.listdir(p_TcumuxerSupport)
            for file in dirList:
                [base, ext] = os.path.splitext(file)
                if ((ext == ".py") or (ext == ".sh") or (ext == ".conf")) and (base != "muxer_sanity_orb"):
                    self.log("file in p_TcumuxerSupport : " + file)
                    self.shellUtils.Copy(os.path.join(p_TcumuxerSupport, file), dest, False, True)
        else:
            self.vlog("Source directory doesn't exist skipping copy %s\n" % (p_TcumuxerSupport))

        #Copy Tcu Muxer Proper
        l_srcfile = os.path.join(p_TcumuxerDir, "tcu_muxer")
        if (os.path.isfile(l_srcfile) == True):
            self.shellUtils.Copy(l_srcfile, dest, False, True)
        else:
            self.vlog("Source file doesn't exist skipping copy %s\n" % (l_srcfile))

    def CopyTegraFlashUtilities(self, p_TempDumpPath):
        self.log("Make temporary directory for flashing : " + p_TempDumpPath)
        self.shellUtils.makeDirectory(p_TempDumpPath)
        p_FsCreationToolPath = self.targetConfig.p_FsCreationToolPath
        p_FsCreationmkfsPath = self.targetConfig.p_FsCreationmkfsPath
        p_FsAndroidToolPath = self.targetConfig.p_FsAndroidToolPath

        # FLashing tools which need to be in the temp dir for nvimagegen execution
        self.shellUtils.Copy(self.flashUtils.f_TegraHost, p_TempDumpPath, False, True)
        self.shellUtils.Copy(self.flashUtils.f_CompressLz4, p_TempDumpPath, False, True)
        self.shellUtils.Copy(self.flashUtils.f_NvImageSign, p_TempDumpPath, False, True)
        self.CopyTegraSign(p_TempDumpPath)

        if (self.targetConfig.n_EncryptedbootFlash is True):
            if self.targetConfig.f_EncKeyFilePath != "":
                self.shellUtils.Copy(self.targetConfig.f_EncKeyFilePath, os.path.join(p_TempDumpPath, self.targetConfig.f_EncKeyFile))
            filename = os.path.join(p_TempDumpPath, 'label.bin')
            with open(filename, 'wb') as f:
                value = "NV_DRIVEOS_SBK_KEY"
                f.write(value.encode('utf-8'))
            filename = os.path.join(p_TempDumpPath, 'context.txt')
            with open(filename, 'w') as f:
                f.write("CPUB_USE")

        if (self.targetConfig.n_PlEncryptedbootFlash is True):
            self.shellUtils.Copy(self.targetConfig.f_PlEncKeyFilePath,
                                 os.path.join(p_TempDumpPath, self.targetConfig.f_PlEncKeyFile))
            filename = os.path.join(p_TempDumpPath, 'os_label.bin')
            with open(filename, 'wb') as f:
                value = "NV_DRIVEOS_KEK0_KEY"
                f.write(value.encode('utf-8'))


        if (self.targetConfig.n_SecurebootFlash is True):
            if (len(self.targetConfig.HSMStr) == 0) and \
                not self.targetConfig.f_PkcKeyFilePath == None:
                    self.targetConfig.copyPkcKey(p_TempDumpPath)

        if (os.path.isfile(self.flashUtils.f_NvImageGen)):
            self.shellUtils.Copy(self.flashUtils.f_NvImageGen, p_TempDumpPath, False, True)
        if (os.path.isfile(self.flashUtils.f_NvImageSign)):
            self.shellUtils.Copy(self.flashUtils.f_NvImageSign, p_TempDumpPath, False, True)
        if (os.path.isfile(self.flashUtils.f_Nvpt)):
            self.shellUtils.Copy(self.flashUtils.f_Nvpt, p_TempDumpPath, False, True)

        if (os.path.isdir(p_FsCreationToolPath)):
            # NvImageGen Tool require this binary to be present in PWD of NvImageGen tool
            self.shellUtils.CopyifSrcisafile(os.path.join(p_FsCreationToolPath, "simg2img"), p_TempDumpPath, executable=True)
            if (os.path.isdir(p_FsCreationmkfsPath)):
                self.shellUtils.CopyifSrcisafile(os.path.join(p_FsCreationmkfsPath, "mkfs"), p_TempDumpPath, executable=True)
            if not (os.path.isfile(os.path.join(p_TempDumpPath, "mkfs"))):
                self.vlog("mkfs not found in the repo or package")

        if (os.path.isdir(p_FsAndroidToolPath)):
            self.shellUtils.CopyifSrcisafile(os.path.join(p_FsAndroidToolPath, "simg2img"), p_TempDumpPath, executable=True)

        if (self.targetConfig.isPDK):
            self.shellUtils.CopyifSrcisafile(os.path.join(self.targetConfig.PDK_TOP, "foundation/ist", "ist_ucode_dev.bin"), os.path.join(p_TempDumpPath, "ist_ucode_dev.bin"))
            self.shellUtils.CopyifSrcisafile(os.path.join(self.targetConfig.PDK_TOP, self.targetConfig.NV_SDK_NAME_FOUNDATION, "firmware/bin/ist", "ist_ucode_prod.bin"), os.path.join(p_TempDumpPath, "ist_ucode_prod.bin"))

        if (os.path.isfile(self.flashUtils.f_NvSimgDump)):
            self.shellUtils.Copy(self.flashUtils.f_NvSimgDump, p_TempDumpPath, False, True)

    def CopyTegraToolsVersionFile(self):

        self.log("l_p_OutputDir :: {}".format(self.targetConfig.p_OutDirPath))
        self.log("p_BurnDir     :: {}" .format(self.targetConfig.p_BurnDir))
        self.shellUtils.Copy(os.path.join(self.targetConfig.p_BurnDir, "ToolsVersion.txt"), self.targetConfig.p_OutDirPath)
        self.shellUtils.Copy(os.path.join(self.targetConfig.p_BurnDir, "ToolsVersion.txt"), os.path.join(self.targetConfig.p_OutDirPath, "../tools"))


    def CopyFuseByPassBinaries(self, p_TempDumpPath):
        if (self.targetConfig.n_EngSample is False):
            # Create Dummy fusebypass file
            self.targetConfig.sysMonitor.log("Create dummy fusebypass file")
            with open(os.path.join(p_TempDumpPath, "fusebypass.bin"), 'w', stat.S_IREAD | stat.S_IWRITE | stat.S_IRGRP | stat.S_IWGRP | stat.S_IROTH | stat.S_IWOTH) as fid:
                self.writeToFile(fid, "00")
            os.chmod(os.path.join(p_TempDumpPath, "fusebypass.bin"), stat.S_IREAD | stat.S_IWRITE | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IWOTH)
            self.log("Not an Engg sample, created dummy file")

        else:
            #t264 and not fpga
            self.log ("s_FuseBypass :: {}".format((self.targetConfig.boardDefaultPaths["s_FuseBypass"])))
            self.log ("p_FuseBypassPrivate :: {}".format(os.path.join(self.targetConfig.p_FuseBypassPrivate, self.targetConfig.boardDefaultPaths["s_FuseBypass"])))
            if (os.path.isfile(os.path.join(self.targetConfig.p_FuseBypassPrivate, self.targetConfig.boardDefaultPaths["s_FuseBypass"]))):
                self.log("fusebypass file is available")

            if (len(self.targetConfig.boardDefaultPaths["s_FuseBypass"])):
                self.shellUtils.Copy(os.path.join(self.targetConfig.p_FuseBypassPrivate, self.targetConfig.boardDefaultPaths["s_FuseBypass"]), os.path.join(p_TempDumpPath, "fusebypass.bin"))
                self.log("copied fusebypass.bin - {}".format(os.path.join(self.targetConfig.p_FuseBypassPrivate, self.targetConfig.boardDefaultPaths["s_FuseBypass"])))

    def CleanupTegraFlashUtilities(self, l_Operation, p_TempDumpPath):
        if(os.path.isdir(p_TempDumpPath) is False):
            AbnormalTermination("Invalid path -- Unknow image path " + p_TempDumpPath, nverror.NvError_BadParameter)

        cwd = os.getcwd()
        os.chdir(p_TempDumpPath)

        self.shellUtils.removeFile("bct_MB1.bct")
        self.shellUtils.removeFile("flash_lz4")
        self.shellUtils.removeFile("nvimagegen")
        self.shellUtils.removeFile("tegrasign*")
        self.shellUtils.removeFile("mkfs")
        self.shellUtils.removeFile("mb1.bin")
        self.shellUtils.removeFile("nvtboot.bin")
        self.shellUtils.removeFile("qb_cpu.bin")
        self.shellUtils.removeFile("preboot_c10_cr.bin")
        self.shellUtils.removeFile("mts_c10_cr.bin")
        self.shellUtils.removeFile("mce_c10_cr.bin")
        self.shellUtils.removeFile("fusebypass.bin")

        self.shellUtils.removeFile("bpmp.dtb")
        self.shellUtils.removeFile("linux.dtb")
        self.shellUtils.removeFile("kernel.def")
        self.shellUtils.removeFile("*.temp")

        self.shellUtils.removeFile("mb1_recovery.bin")
        self.shellUtils.removeFile("adsp-fw.bin")
        self.shellUtils.removeFile("sce-fw.bin")
        self.shellUtils.removeFile("warmboot.bin")

        self.shellUtils.removeFile("nvsimgdump")

        if (self.targetConfig.n_EncryptedbootFlash is True):
            self.shellUtils.removeFile(self.targetConfig.f_EncKeyFile)
            self.shellUtils.removeFile('label.bin')
            self.shellUtils.removeFile('context.txt')

        if (self.targetConfig.n_PlEncryptedbootFlash is True):
            self.shellUtils.removeFile(self.targetConfig.f_PlEncKeyFile)
            self.shellUtils.removeFile('os_label.bin')

        for memBct in self.targetConfig.f_MembctBin:
            self.shellUtils.removeFile(memBct)

        if (l_Operation == "flash-images"):
            self.shellUtils.removeFile("*.img_def")

        if (l_Operation == "rcm-boot" or l_Operation == "rcm-flash"):
            self.shellUtils.removeFile("*.img")
            self.shellUtils.removeFile("system_rcm.bin")
            self.shellUtils.removeFile("tegra_dtb_*.dtb")
            self.shellUtils.removeFile("GPT_*.bin")
            self.shellUtils.removeFile("*_PT.bin")
            self.shellUtils.removeFile("mce_c10_grp.bin")
            self.shellUtils.removeFile("preboot_c10_grp.bin")
            self.shellUtils.removeFile("rcm_list.xml")
            self.shellUtils.removeFile(self.targetConfig.f_MB1SoftfuseBin)

            rcmFiles = glob.glob("rcm_?.rcm")
            for f in rcmFiles:
                self.shellUtils.removeFile(f)

            zerosignList = glob.glob("*_zerosign.*")
            for f in zerosignList:
                if(f.find("bct_BR") != -1):
                    continue
                elif(f.find("bct_MB1") != -1):
                    continue
                elif(f.find("mb1") != -1):
                    continue
                elif(f.find("membct") != -1):
                    continue
                elif(f.find("psc_bl") != -1):
                    continue

                self.shellUtils.removeFile(f)

            if(os.path.isdir("./applet")):
                rcmFiles = glob.glob("applet/rcm_?.rcm")
                for f in rcmFiles:
                    self.shellUtils.removeFile(f)
                self.shellUtils.removeFile("applet/rcm_list.xml")
                self.shellUtils.removeFile(self.targetConfig.f_MB1SoftfuseBin)

        elif (l_Operation == "rcm-flash"):
            self.shellUtils.removeFile("bct_BR.bct")

        self.shellUtils.removeFile("tmp_*.cfg")
        os.chdir(cwd)

    def getToolVersion (self, fileName):
        toolVersionLine = []
        toolVersion = ""

        #get the tool version from images section of create_bsp_output
        with open(fileName, "r") as imageToolVer:
            imageToolVerdata = imageToolVer.read()

        imageToolVerLines = imageToolVerdata.splitlines()
        for lineData in imageToolVerLines:
            lineData = lineData.strip()
            if (not lineData or lineData[0] == '#'):
                continue
            toolVersionLine = lineData.split('=')
            if (toolVersionLine[0].find("ToolsVersion")):
                toolVersion = toolVersionLine[1]
            break
        return toolVersion

    def ValidateFlashToolVersion(self, offlineImagesPath):
        #get the tool version from images section of create_bsp_output
        self.log("offlineImagesPath : " + offlineImagesPath)
        toolsVersionPath = os.path.join(offlineImagesPath, "../tools",  "ToolsVersion.txt")
        imageVersionPath = os.path.join(offlineImagesPath, "ToolsVersion.txt")
        imgToolVer = self.getToolVersion(imageVersionPath)
        flsToolVer = self.getToolVersion(toolsVersionPath)
        self.vlog("ToolsVersion in Images folder  : " + imgToolVer)
        self.vlog("ToolsVersion in Flashing tools folder  : " + flsToolVer)
        if (imgToolVer=="" or flsToolVer == ""):
           self.vlog("Error in Toolversion data in create_bsp_images output, aborting flashing")
           raise Exception (" missing ToolVersion file in Images or in Tools folder")
        if (imgToolVer != flsToolVer):
           self.vlog("Tools are not compatible  with the provided offline binaries, update your flash tools")
           raise Exception ("ToolVersion is changed, Need flash tools to be updated (in container)")

    def VerifyLowPowerPartitions(self):
        if (self.targetConfig.n_LoadSPEFW is False):
            # nothing to do here
            return

        if (self.targetConfig.n_SPE_FW_FOUND is False or self.targetConfig.n_SC7_FW_FOUND is False):
            errorStr = "Invalid config file with -L option (Low Power Mode)\n"
            errorStr += "Low power mode config files must specify \"name=spe-fw\" and \"name=sc7-fw\" partitions"
            AbnormalTermination(errorStr, nverror.NvError_InvalidConfigVar)

    # arg 1 contains cfg file to search for file
    def CopyStorageDtbImage(self, l_configFile, p_TempDumpPath):
        if(os.path.isfile(l_configFile) is False):
            AbnormalTermination("Config file not found -- " + l_configFile, nverror.NvError_FileNotFound)

        if (self.targetConfig.f_StorageDtbNameAbs):
            self.shellUtils.CopyVerbose(self.targetConfig.f_StorageDtbNameAbs, os.path.join(p_TempDumpPath, self.targetConfig.s_StorageDtbImgName))
        elif (self.targetConfig.f_StorageDtbName):
            self.shellUtils.CopyVerbose(os.path.join(self.targetConfig.p_StorageDtbPath, self.targetConfig.f_StorageDtbName), os.path.join(p_TempDumpPath, self.targetConfig.s_StorageDtbImgName))
        else:
            l_OsDtbImgFound = self.shellUtils.grep(l_configFile, self.targetConfig.s_StorageDtbImgName)
            if (len(l_OsDtbImgFound) > 0):
                if(os.path.isfile(os.path.join(self.targetConfig.p_StorageDtbPath, self.targetConfig.boardDefaultPaths["f_DtbImg"]))):
                    self.shellUtils.CopyifSrcisafile(os.path.join(self.targetConfig.p_StorageDtbPath, self.targetConfig.boardDefaultPaths["f_DtbImg"]), os.path.join(p_TempDumpPath, self.targetConfig.s_StorageDtbImgName))
                else:
                    self.shellUtils.CopyifSrcisafile(os.path.join(self.targetConfig.p_StorageGenericDtbPath, self.targetConfig.boardDefaultPaths["f_DtbImg"]), os.path.join(p_TempDumpPath, self.targetConfig.s_StorageDtbImgName))
        if (self.targetConfig.f_BpmpDtbNameAbs):
            self.shellUtils.CopyVerbose(self.targetConfig.f_BpmpDtbNameAbs, os.path.join(p_TempDumpPath, self.targetConfig.s_BPMPStorageDtbImgName))
        elif (self.targetConfig.f_BpmpDtbName):
            self.shellUtils.CopyVerbose(os.path.join(self.targetConfig.p_BpmpFwDtb, self.targetConfig.f_BpmpDtbName), os.path.join(p_TempDumpPath, self.targetConfig.s_BPMPStorageDtbImgName))
        else:
            l_BpmpDtbImgFound = self.shellUtils.grep(l_configFile, str("=" + self.targetConfig.s_BPMPStorageDtbImgName))

            if (len(l_BpmpDtbImgFound) > 0):
                self.shellUtils.CopyVerbose(os.path.join(self.targetConfig.p_BpmpFwDtb, self.targetConfig.boardDefaultPaths["f_BpmpFwDtb"]), os.path.join(p_TempDumpPath, self.targetConfig.s_BPMPStorageDtbImgName))

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

    def resolvePath(self, name, path):
        #if user puts absolute path in the filelist honor it and do not try and
        #join paths together.
        name = self.targetConfig.parseDefaultValues(name, True)
        if(os.path.dirname(name) != '' and os.path.isfile(name)):
            return os.path.abspath(name)

        path = os.path.join(path, name)
        path = os.path.abspath(path)
        return path


    def CopyAndPreprocessICTBinary(self, l_Operation, p_TempDumpPath):

        if(os.path.isfile(self.targetConfig.p_NvICTBinPath)):
            self.shellUtils.Copy(self.targetConfig.p_NvICTBinPath, os.path.join(p_TempDumpPath, self.targetConfig.f_NvICTBinImage))
            self.gr_ist_append_bin.append(os.path.join(p_TempDumpPath, self.targetConfig.f_NvICTBinImage))

    def CopyAndPreprocessISTBinary(self, l_Operation, p_TempDumpPath):
        if(os.path.isfile(self.targetConfig.p_NvISTBinPath)):
            self.shellUtils.Copy(self.targetConfig.p_NvISTBinPath, os.path.join(p_TempDumpPath, self.targetConfig.f_NvISTBinImage))
            self.gr_ist_append_bin.append(os.path.join(p_TempDumpPath, self.targetConfig.f_NvISTBinImage))

    def EmptyGrBlBinLists(self):
        self.gr_qb_append_bin = []
        self.gr_ist_append_bin = []
        self.gr_mb2_append_bin = []

    def SkuBlobGen(self, l_BRBCT):
        self.log ("Updating SkuInfo in " + l_BRBCT)
        chipID = str(self.targetConfig.GetChipID())
        customerDataFile = "customer_data-" + str(uuid.uuid4()) + ".json"
        signedCustomerDataFile = "signed_customer_data-"  + str(uuid.uuid4()) + ".json"
        #update signed customer data in all cases except, where target is  fused for privacy and  key file path is empty )
        #       this covers both HSM mode and local host based signing)
        #       usecase 4, is not a valid usecase and is failed much earlier in the bootburn\flash_bsp flow.

        #       fused_target\pkc_file       AVAIL     NOTAVAIL
        # 1           YES                    YES       NO
        # 2           NO                     YES(N/A)  YES

        updateSignedCustomerData = not (self.targetConfig.b_boardFusedForSigning  and  self.targetConfig.f_PkcKeyFilePath is None)

        skuCmd = self.flashUtils.f_NvSkuInfo + " "
        skuCmd += " --inbct " + l_BRBCT
        skuCmd += " --outbct " + l_BRBCT
        skuCmd += " --chip " + chipID

        if (self.targetConfig.n_SkipSkuValidate is True):
            skuCmd += " --skipValidate "

        if (((self.targetConfig.f_SignedCustomerData != None) or (self.targetConfig.signedCustomerData != {})) and
                (updateSignedCustomerData == True)):
            try:
                #Load Schema
                self.targetConfig.signedcustomerDataProcessor.load_schema(self.targetConfig.f_SignedCustomerDataSchema)
                if (self.targetConfig.f_SignedCustomerData != None):
                    #Load Data
                    with open(self.targetConfig.f_SignedCustomerData, "r") as f_signedcustData:
                        signedUpdateJson = json.load(f_signedcustData)

                        if ("customer-data-signed" not in signedUpdateJson):
                            raise AbnormalTermination("No signed customer data found in %s" % self.targetConfig.f_SignedCustomerData,
                                                        nverror.NvError_InvalidArgument)

                        signedCustomerData = CustomerData(signedUpdateJson["customer-data-signed"], "customer-data-signed")
                else:
                    signedCustomerData = self.targetConfig.signedCustomerData

                if(self.targetConfig.b_boardConnected):

                    inforomSkuInfo = (len(self.targetConfig.s_InforomSkuVersion) != 0 and len(self.targetConfig.s_InforomProdInfo) != 0)
                    if inforomSkuInfo:
                        signedCustomerData.setKeyValue("prodInfo", self.targetConfig.s_InforomProdInfo)
                        signedCustomerData.setKeyValue("skuVersion", self.targetConfig.s_InforomSkuVersion)
                        signedCustomerData.setKeyValue("boardName", self.targetConfig.s_BoardName)
                        self.vlog("*****" + "prodInfo :: "  +  self.targetConfig.s_InforomProdInfo + "******")
                        self.vlog("*****" + "skuVersion :: "  +  self.targetConfig.s_InforomSkuVersion + "******")
                        self.log("*****" + "boardName :: "  +  self.targetConfig.s_BoardName + "******")
                    else:
                        self.log("*****" + "boardName :: "  +  self.targetConfig.s_BoardName + "******")

                    if (len(self.targetConfig.s_SocSkuName) != 0):
                        signedCustomerData.setKeyValue("socSkuVersion", self.targetConfig.s_SocSkuName)
                        if len(signedCustomerData.getValue("socSkuVersion")) > 7:
                            signedCustomerData.setKeyValue(
                                "socSkuVersion",
                                signedCustomerData.getValue("socSkuVersion")[:7]
                            )
                        self.log("*****" + "socSkuVersion :: "  +  self.targetConfig.s_SocSkuName + "******")
                    else:
                        self.log("*****   no update of socSkuVersion in  signed customer data in Preserve SKUInfo ****")

                # Overwrite with command line signed customer data if any
                signedCustomerData.updateValuesFrom(self.targetConfig.signedCustomerData)
                signedCustomerData.dumpToFile(signedCustomerDataFile)

                self.targetConfig.signedcustomerDataProcessor.process(signedCustomerDataFile, self.targetConfig.f_signedcustomerDataBlobFile)
            except Exception as Err:
                self.vlog("Error occured generating customer data blob for BR BCT %s\n" %(str(Err)))
                AbnormalTermination(Err, nverror.NvError_InvalidState)

            signedskuCmd = skuCmd + " -t s --customer-data-blob " + self.targetConfig.f_signedcustomerDataBlobFile

            result = self.shellUtils.executeShellCommand(signedskuCmd)
            if (result != 0):
                self.vlog ("Incorrect argument to nvskuflash. Exiting")
                errorMsg = "s_ERROR_TOOL_NVSKUFLASH - Incorrect argument to nvskuflash"
                AbnormalTermination(errorMsg, nverror.NvError_InvalidArgument)

            self.targetConfig.b_IsBrBctSignedCustomerDataPreserved = True

            #create_bsp updates data from signed config file, but doesn't resign
            #   this is to be resigned later in the flashing cycle
            if self.targetConfig.b_boardFusedForSigning: # and (self.targetConfig.f_PkcKeyFilePath is not None):
                self.log("bootburn with signed images and fused target, resigning")
                self.CopyTegraSign(os.getcwd())
                l_resignCmd = self.flashUtils.f_NvImageSign
                if (len(self.targetConfig.HSMStr) != 0):
                    l_resignCmd += self.targetConfig.HSMStr + "  " + self.targetConfig.HSMAuthStr
                else:
                    #copy the dummy sign file to the NvImageSign folder
                    self.targetConfig.copyPkcKey(os.path.dirname(os.path.realpath(self.flashUtils.f_NvImageSign)))
                    l_resignCmd += "  --sign  " + self.targetConfig.f_PkcKeyFilePath
                l_resignCmd += " --pcp " + self.targetConfig.getPcpFilePath()
                l_BRBCT_Out = l_BRBCT + "_resigned.bct"
                l_resignCmd += "  --chip  " + chipID  + "  --image " + l_BRBCT  +  "  --out_file  " + l_BRBCT_Out

                result = self.shellUtils.executeShellCommand(l_resignCmd)
                if (result != nverror.NvError_Success):
                    self.vlog("Resign with NvImageSign failed ")
                    AbnormalTermination("s_ERROR_TOOL_NVImageSign", nverror.NvError_NvImageSign)
                self.shellUtils.Copy(l_BRBCT + "_resigned.bct", l_BRBCT, True)
        else:
            self.log("*****   signed customer data is not loaded   *****")


        if (self.targetConfig.customerData.isLoaded()):
            # Generate blob file
            try:
                self.vlog (str(self.targetConfig.customerData.customerDataDict))
                # dump the content of the customer data to json file
                self.targetConfig.customerData.dumpToFile(customerDataFile)
                if (self.targetConfig.customerDataProcessor.is_schema_loaded() is False):
                    # Load the default schema
                    self.targetConfig.customerDataProcessor.load_schema(self.targetConfig.f_customerDataSchemaFile)
                self.targetConfig.customerDataProcessor.process(customerDataFile, self.targetConfig.f_customerDataBlobFile)
            except Exception as err:
                self.vlog(
                    "Error occured generating customer data blob for BR BCT")
                AbnormalTermination(str(err), nverror.NvError_InvalidState)

            skuCmd +=" -t u "

            skuCmd += " --customer-data-blob " + self.targetConfig.f_customerDataBlobFile

            result = self.shellUtils.executeShellCommand(skuCmd)
            if (result != 0):
                self.vlog ("Incorrect argument to nvskuflash. Exiting")
                errorMsg = "s_ERROR_TOOL_NVSKUFLASH - Incorrect argument to nvskuflash"
                AbnormalTermination(errorMsg, nverror.NvError_InvalidArgument)


        os.chmod(l_BRBCT, stat.S_IREAD | stat.S_IWRITE | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IWOTH)

    def PreserveTargetSkuInfo(self, l_BRBCT, customerDataType='u'):
        tegraRCM = self.formatTegraRCM()
        l_NvSkuArgs = self.targetConfig.s_NvSkuPreservArgs
        chipID = str(self.targetConfig.GetChipID())

        self.vlog ("Preserving SkuInfo from Target")
        # recover skuinfo from target
        self.log("Obtain SkuInfo by reading previously flashed BCT from target")
        tegraRcmCommand = tegraRCM + " --oem dump bct ./read.bct"
        if (self.targetConfig.skipBctPreserve is False):
            result = self.shellUtils.executeShellCommand(tegraRcmCommand)
        else:
            result = nverror.NvError_Unknown
        if (result != nverror.NvError_Success):
            self.vlog ("Skipped or Unable to Read BCT. The storage may be empty or erased")
            self.vlog ("Flash along with --customer-data option to have a valid images on storage")
            self.vlog("Note, -R option should not be used while flashing a valid bct to storage")

            if (self.targetConfig.customerData.isLoaded() is False):
                # This means skuinfo parameters are not specified via command line
                # so Target BCT is required, if not found return error here
                AbnormalTermination("s_ERROR_TOOL_TEGRARCM_SKUINFO", nverror.NvError_TegraRcmError)
            else:
                self.vlog ("Target BCT not found for preserving sku values from Target!!")
                self.vlog ("Return without preserving!!")
                return

        self.log("Preserve metadata from obtained BCT (inbct) to the targeted BCT (outbct)")

        skuCommand = self.flashUtils.f_NvSkuInfo + " --inbct ./read.bct --outbct " + l_BRBCT + " " + l_NvSkuArgs + " --chip " + chipID
        skuCommand += ' -t ' + customerDataType
        result = self.shellUtils.executeShellCommand(skuCommand)
        if (result != nverror.NvError_Success):
            self.vlog ("Target BCT is corrupt!! Skipping Preserve!!")
            self.vlog ("Return without preserving!!")
            return


        skuCommand = self.flashUtils.f_NvSkuInfo + " --inbct " + l_BRBCT + " -P --chip " + chipID
        skuCommand += ' -t b '
        result = self.shellUtils.executeShellCommand(skuCommand, True)

        if(isinstance(result, int) is True):
            AbnormalTermination("s_ERROR_TOOL_NVSKUINFO", nverror.NvError_TegraSkuInfoError)

        for line in result.splitlines():
            if(line.find("skunum") != -1):
                skunum = line.split(':')[1].rstrip()
                self.targetConfig.skuArguments["s_skuarg_skunum"] = skunum
                break
        self.shellUtils.removeFile("./read.bct")

    def DetectChipVersion(self):
        l_ChipVersion = int(self.targetConfig.s_BR_CID[13])
        self.targetConfig.n_ChipVersion = l_ChipVersion
        self.targetConfig.s_ChipVersion = "{:02}".format(l_ChipVersion)

    def GetRamCode(self):
        chipID = str(self.targetConfig.GetChipID())
        self.targetConfig.sysMonitor.log("Obtain chip information (version, etc.) from target")

        chip_info = self.sendRcmCommand("--oem platformdetails chip chip_info.bin --chip %s" %(chipID))
        self.targetConfig.sysMonitor.log("Read Ram code")

        position = chip_info.find("Ram code:")
        if(position >= 0):
            position += len("Ram code:")
            ending = chip_info.find("\n", position)
            self.targetConfig.s_RamCode = int(chip_info[position:ending].strip(), 16)

    def ApplySoftfusing(self):
        softfuse_xml = "fskp_t264.xml"

        if (os.path.isfile(softfuse_xml) == False):
            self.targetConfig.sysMonitor.log("Exit out because %s is not found" %(softfuse_xml))
            return False

        chipID = str(self.targetConfig.GetChipID())
        fuse_info = "fuse_info.bin"

        self.targetConfig.sysMonitor.log("Attempt to applying soft fusing")
        tegraParsercommand = ("%s --chip %s --fuse_info %s %s" % (self.flashUtils.f_TegraParser, chipID, softfuse_xml, fuse_info))
        result = self.shellUtils.executeShellCommand(tegraParsercommand)
        if (result != nverror.NvError_Success):
            AbnormalTermination("s_ERROR_TOOL_TEGRAPARSER", nverror.NvError_TegraParserError)

        chip_info = self.sendRcmCommand("--oem  softfuses %s" %(fuse_info))
        self.targetConfig.sysMonitor.log("Apply soft fusing")
        return True


    # Function that reads ECID from the supplied flash cfg file.
    # The global variable s_ECID is used to store the value.
    def ReadEcidFromCfg(self, l_Cfgle,):
        l_EcidInCfg = self.GetBctPartEcid(l_Cfgle)

        if (l_EcidInCfg):
            self.targetConfig.s_ECID = self.GetEcidFromBrcid(l_EcidInCfg)

        return self.targetConfig.s_ECID

    # Function to return the ECID from the supplied uid.
    # If the length of the supplied uid is > 25 characters,
    # it implies that BR_CID is supplied, else it is ECID.
    # NOTE:- Do no put any prints in this function, as it
    # is used to return a value.
    def GetEcidFromBrcid(self, l_BrCid):
        l_Length = len(l_BrCid)
        l_ecid = "0x0000000000000000000000000"

        if(l_BrCid.lower()[0:2] == "0x"):
            l_BrCid = l_BrCid[2:]
            l_Length = l_Length - 2

        if (l_Length == self.targetConfig.n_EcidLength + 9):
            l_ecid = "0x" + l_BrCid[9:9 + self.targetConfig.n_EcidLength]
        elif (l_Length == self.targetConfig.n_EcidLength + 7):
            l_ecid = "0x" + l_BrCid[7:7 + self.targetConfig.n_EcidLength]
        elif (l_Length == self.targetConfig.n_EcidLength + 2):
            l_ecid = "0x" + l_BrCid[2:2 + self.targetConfig.n_EcidLength]
        elif (l_Length == self.targetConfig.n_EcidLength):
            l_ecid = "0x" + l_BrCid[0:self.targetConfig.n_EcidLength]

        return l_ecid

#     #Parses the config file for the uid in bct partition
    def GetBctPartEcid(self, l_configFile):
        l_ecid = self.shellUtils.grep(l_configFile, "uid=")
        if(len(l_ecid) > 0):
            parsedECID = self.getValue(l_ecid[0])
            parsedECID = self.GetEcidFromBrcid(parsedECID)
            return parsedECID
        else:
            return ""

    def GetBctSecdbgctrl(self, l_configFile):
        sec_dbg_ctrl = self.shellUtils.grep(l_configFile, "sec_dbg_ctrl=")
        if(len(sec_dbg_ctrl) > 0):
            dbgCtrlValue = self.getValue(sec_dbg_ctrl[0])
            return dbgCtrlValue
        else:
            return "0"

    def testSecureJtagEnabledForRcmBoot(self, l_Operation):
        if(self.targetConfig.parsed_commandline["find_board_name"] != None):
            return nverror.NvError_NotSupported

        if(sys.argv[0].find("flash_bsp_images.py") != -1):
            #Values parsed from TargetInfo.txt
            if not (self.targetConfig.n_SecDbgCtrl is None):
                l_SecdbgctrlInCfg = int(self.targetConfig.n_SecDbgCtrl, 16)
            else:
                l_SecdbgctrlInCfg = 0
            l_EcidInCfg = self.targetConfig.s_EcidInImage
        else:
            l_SecdbgctrlInCfg = int(self.GetBctSecdbgctrl(self.targetConfig.f_FlashCfg), 16)
            l_EcidInCfg = self.GetBctPartEcid(self.targetConfig.f_FlashCfg)

        if (not self.targetConfig.f_FlashCfg):
            if (len(self.targetConfig.s_EcidInImage) >= self.targetConfig.n_EcidLength and l_Operation == "rcm-boot"):
                return nverror.NvError_NotSupported
            else:
                return nverror.NvError_Success

        if (self.targetConfig.n_SecurebootFlash and l_Operation == "rcm-boot"):
            if ((l_SecdbgctrlInCfg & self.targetConfig.n_EnableJtagBit) > 0):
                # Enabling JTAG
                if (sys.argv[0].find("create_bsp_images.py") == -1):
                    if (self.targetConfig.s_ECID != l_EcidInCfg):
                        return nverror.NvError_NotSupported
                    else:
                        return nverror.NvError_Success
                else:
                    return nverror.NvError_Success

        return nverror.NvError_NotSupported

    def GenerateRcmMessage(self, l_MB1Softfuses, l_Operation):
        chipID = str(self.targetConfig.GetChipID())
        l_SfuseOverlay="tmp_sfuse.cfg"

        tegraRCM = self.flashUtils.f_TegraRcm
        if(self.targetConfig.n_GenBinsOnly is False):
            tegraRCM = self.formatTegraRCM()

        l_NvImagegenCmd = " --chip " + chipID + " --chipver A" + str(self.targetConfig.n_ChipVersion).zfill(2)
        l_MB1_Recovery = ""
        l_GetMont = ""
        l_SetMont = ""
        l_JtagInfo = ""
        l_encrypt = ""
        l_SecdbgctrlInCfg = ""
        l_EcidInCfg = ""

        #FIXME::targetConfig.f_FlashCfg is initialized to native config file
        # /drive-foundation-safety/tools/flashtools/storage_configs/t264/quickboot_qspi_xxx.cfg
        #As native storage configs folder is not packaged in safety build of t264,
        # added a check in testSecureJtableEnabledForRcmBoot to return fail
        #  for "find_board_name" option
        if (self.testSecureJtagEnabledForRcmBoot(l_Operation) == nverror.NvError_Success):
            l_SecdbgctrlInCfg = self.GetBctSecdbgctrl(self.targetConfig.f_FlashCfg)
            l_EcidInCfg = self.GetBctPartEcid(self.targetConfig.f_FlashCfg)
            l_JtagInfo = "--ecid " + l_EcidInCfg + " --securedebugcontrol " + l_SecdbgctrlInCfg

        if (self.targetConfig.n_EncryptedbootFlash is True):
            der_str = "MB1ENCRYPTIONSTR"
            l_NvImagegenCmd += " --encrypt "
            l_NvImagegenCmd += " --der_str " + der_str
            l_encrypt = "_encrypt"

        # Sign MB1_Recovery and download the same
        if (self.targetConfig.n_SecurebootFlash is True):
            l_NvImagegenCmd += " --pkc " + self.targetConfig.f_PkcKeyFile
            l_NvImagegenCmd += " --pcp " + self.targetConfig.getPcpFilePath()
            l_MB1_Recovery = "mb1_recovery{}_signed.bin".format(l_encrypt)
        else:
            l_MB1_Recovery = "mb1_recovery{}_zerosign.bin".format(l_encrypt)
        l_NvImagegenCmd += self.targetConfig.HSMStr + " " + self.targetConfig.HSMAuthStr
        l_NvImagegenCmd += " --bct_der_consts " + self.targetConfig.getBCTDerConstFilePath()

        # "Sign MB1 binary"
        signMb1 = self.flashUtils.f_NvImageGen + l_NvImagegenCmd + " --signbin mb1_recovery.bin MB1B"
        # result = self.shellUtils.executeShellCommand(signMb1)
        # if (result != nverror.NvError_Success):
        #     AbnormalTermination("s_ERROR_TOOL_NVIMAGEGEN", nverror.NvError_NvImagegen)

        return nverror.NvError_Success

    def GenerateAllRcmMessage(self, l_Operation, p_TempDumpPath):
        chipID = str(self.targetConfig.GetChipID())
        l_MB1Softfuses = "TMP_softfuse.cfg"
        l_NvImagegenCmd = " "
        l_AppletSignBin = " "
        l_AppletBin = " "
        l_encrypt = ""
        l_AppletStrList = os.path.splitext(self.targetConfig.f_MB2Applet)

        appletDir = os.path.join(p_TempDumpPath, self.targetConfig.p_AppletDir)

        # Generate applet and rcm message with platform detection enabled
        if(os.path.isdir(appletDir) is False):
            os.mkdir(appletDir)

        # self.shellUtils.Copy("mb1_recovery.bin", appletDir)
        cwd = os.getcwd()
        os.chdir(appletDir)
        self.CopyTegraSign(appletDir)
        if (l_Operation == "rcm-flash"):
            l_AppletBin = os.path.join(self.targetConfig.f_FlashingMB2AppletPath, self.targetConfig.f_FlashingMB2Applet)
            self.log("f_FlashingMB2Applet = " + l_AppletBin)
            l_NvImagegenCmd = " --signbin " + l_AppletBin + " " + self.targetConfig.s_Mb2AppletMagicId + " --chip " + chipID + " --mb1bct ../bct_MB1.bct "
        elif (l_Operation == "rcm-boot"):
            l_AppletBin = os.path.join(self.targetConfig.f_MB2AppletPath, self.targetConfig.f_MB2Applet)
            l_NvImagegenCmd = " --signbin " + l_AppletBin + " " + self.targetConfig.s_Mb2AppletMagicId + " --chip " + chipID + " --mb1bct ../bct_MB1.bct "
        else:
            self.vlog("Not support operation " + l_Operation)
            AbnormalTermination("s_ERROR_OPTION_VALUE_INVALID", nverror.NvError_NotSupported)

        if (self.targetConfig.n_EncryptedbootFlash is True):
            if self.targetConfig.f_EncKeyFilePath != "":
                self.shellUtils.Copy(self.targetConfig.f_EncKeyFilePath, os.path.join(appletDir, self.targetConfig.f_EncKeyFile))
            l_NvImagegenCmd += " --encrypt "
            der_str = "APPLETENCRYPTION"
            l_NvImagegenCmd += " --der_str " + der_str
            l_encrypt = "_encrypt"

        if (self.targetConfig.n_SecurebootFlash is True):
            if(len(self.targetConfig.HSMStr) == 0):
                self.targetConfig.copyPkcKey(appletDir)
            l_NvImagegenCmd += " --pkc " + self.targetConfig.f_PkcKeyFile
            l_NvImagegenCmd += " --pcp " + self.targetConfig.getPcpFilePath()
            if (self.targetConfig.GetChipFamily() == "t264"):
                l_AppletSignBin =  "{}{}_signed{}".format(l_AppletStrList[0], l_encrypt, l_AppletStrList[1])
            else:
                l_AppletSignBin = "nvtboot_applet{}_signed.bin".format(l_encrypt)
        else:
            l_AppletSignBin =  "{}{}_zerosign{}".format(l_AppletStrList[0], l_encrypt, l_AppletStrList[1])
        l_NvImagegenCmd += self.targetConfig.HSMStr + " " + self.targetConfig.HSMAuthStr

        l_NvImagegenCmd += " --bct_der_consts " + self.targetConfig.getBCTDerConstFilePath()
        l_NvImagegenCmd = self.flashUtils.f_NvImageGen + l_NvImagegenCmd
        # "Generate MB2 applet binary"
        result = self.shellUtils.executeShellCommand(l_NvImagegenCmd)
        # os.chmod(l_AppletSignBin, 0666)
        if (result != nverror.NvError_Success):
            AbnormalTermination("s_ERROR_TOOL_NVIMAGEGEN", nverror.NvError_NvImagegen)
        os.rename(l_AppletSignBin, self.targetConfig.f_AppletBin)

        if(self.targetConfig.boardDefaultPaths["f_MB1Softfuses"] != None):
            mb1softFuses = os.path.join(appletDir, l_MB1Softfuses)
            self.shellUtils.Copy(self.targetConfig.boardDefaultPaths["f_MB1Softfuses"], mb1softFuses)

            with open(mb1softFuses, "a", 0o666) as fuseFile:
                self.writeToFile(fuseFile, "PlatformDetectionFlow = 1;\n")

            self.GenerateRcmMessage(l_MB1Softfuses, l_Operation)
            self.shellUtils.Copy(self.targetConfig.boardDefaultPaths["f_MB1Softfuses"], os.path.join(p_TempDumpPath, l_MB1Softfuses))
            self.shellUtils.removeFile(l_MB1Softfuses)

        # self.shellUtils.removeFile("mb1_recovery.bin")

        for tegrasignFile in self.flashUtils.f_TegraSign:
            self.shellUtils.removeFile(os.path.basename(tegrasignFile))

        for tegrasignFile in self.flashUtils.f_TegraSignPriv:
            self.shellUtils.removeFile(os.path.basename(tegrasignFile))

        for tegrasignFile in self.flashUtils.f_TegraSignDevKeyFiles:
            self.shellUtils.removeFile(os.path.basename(tegrasignFile))

        if (self.targetConfig.n_EncryptedbootFlash is True):
            if self.targetConfig.f_EncKeyFile != "":
                self.shellUtils.removeFile(self.targetConfig.f_EncKeyFile)
            self.shellUtils.removeFile('label.bin')
            self.shellUtils.removeFile('context.txt')

        if (self.targetConfig.n_PlEncryptedbootFlash is True):
            self.shellUtils.removeFile(self.targetConfig.f_PlEncKeyFile)
            self.shellUtils.removeFile('os_label.bin')

        if (self.targetConfig.n_SecurebootFlash is True):
            self.shellUtils.removeFile(self.targetConfig.f_PkcKeyFile)

        os.chdir(cwd)
        # Generate general rcm message
        self.GenerateRcmMessage(l_MB1Softfuses, l_Operation)

        self.shellUtils.removeFile(l_MB1Softfuses)

    def SendAndBootAppletSigned(self, p_TempDumpPath):
        cwd = os.getcwd()
        chipID = str(self.targetConfig.GetChipID())

        os.chdir(os.path.join(p_TempDumpPath, self.targetConfig.p_AppletDir))

        self.vlog("Sending MB1 & Applet")  # | tee -a self.targetConfig.f_OutFile
        self.sendRcmCommand("--chip " + chipID + " --rcm rcm_list_signed.xml --skipuid")
        self.sendRcmCommand("--download mb2 " + self.targetConfig.f_AppletBin)
        self.sendRcmCommand(" --boot recovery")
        os.chdir(cwd)

    def GenerateImage(self, configFileDataList, l_Image, p_TempDumpPath):
        n_RcmBoot = False
        n_SkipFileSystemParts = self.targetConfig.parsed_commandline["s"]
        chipID = str(self.targetConfig.GetChipID())

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

        # For rcm-flash and rcm-boot, number of chains is always 1 (A chain)
        # For flash-images(cold boot), number of chain varies based on storage
        # configuration
        chainlist = ['A']

        # Handle -u.  -u will fail if partition is not in Chain
        if (l_Image == "flash-images"):
            numberOfChains = definesOb.getNumChains(self.targetConfig)
            chainlist = []
            if (self.targetConfig.s_UpdatePartitions):
                for imageFile in self.targetConfig.s_UpdatePartitions:
                    if ("A_" in imageFile):
                        chainlist.append('A')
                    if ("B_" in imageFile):
                        chainlist.append('B')
                    if ("C_" in imageFile):
                        chainlist.append('C')
                if (len(chainlist) == 0):
                    chainlist = ['A']
                else:
                    chainlist = sorted(list(dict.fromkeys(chainlist)))
            else:
                for i in range(numberOfChains):
                    chainlist.append(chr(ord('A') + i))

        numberOfChains = len(chainlist)
        # Iterate number of chain times to run nvimagegen command for each chain
        for chain in chainlist:

            l_PtCmd = None
            if (self.targetConfig.generateChain != None and l_Image == "flash-images"):
                if (chain != self.targetConfig.generateChain):
                    continue

            #l_NvImagegenCmd = " --bct " + chainPrefix + self.targetConfig.f_BRBCT
            if (self.targetConfig.n_Asymmetric):
                brBctType = "br_bct_asymmetric"
            else:
                brBctType = "br_bct"
            brBctFile = BootburnBct.getBctObject(brBctType,
                                                 self.targetConfig,
                                                 p_TempDumpPath,
                                               ).getBctFileName()
            l_NvImagegenCmd = " --bct " + brBctFile

            mb1BctFile = BootburnBct.getBctObject("mb1_bct",
                                                   self.targetConfig,
                                                   p_TempDumpPath,
                                                 ).getBctFileName()
            l_NvImagegenCmd += " --mb1bct " + mb1BctFile
            l_NvImagegenCmd += " --chip " + chipID + " --chipver A" + str(self.targetConfig.n_ChipVersion).zfill(2)

            l_NvImagegenCmd += " --membct "
            l_NvImagegenCmd += BootburnBct.getBctObject("mem_bct",
                                                self.targetConfig,
                                                p_TempDumpPath,
                                            ).getBctFileName()
            l_NvImagegenCmd += " "

            if 'f_BpmpMemCfgParam' in self.targetConfig.boardDefaultPaths:
                l_NvImagegenCmd += " --bpmp_mem_bin "
                l_NvImagegenCmd += BootburnBct.getBctObject("bpmp_mem_bin",
                                                    self.targetConfig,
                                                    p_TempDumpPath,
                                                ).getBctFileName()
                l_NvImagegenCmd += " "

            # Get chain specific partition list
            partitionList = self.storageCfgParser.partitionDict['g'][:] # for global paritions
            partitionList.extend(self.storageCfgParser.partitionDict[chain])

            # Generate all chain specific BCTs
            self.GenerateAllBCTs(l_Image, partitionList, chain)
            configFileList = [fileList[1] for fileList in configFileDataList]

            if 'f_MB1MultiSkuPlatformParam' in self.targetConfig.boardDefaultPaths:
                bctFileNameStr = BootburnBct.getBctObject("mb1_multi_sku_platform_bct",
                                    self.targetConfig,
                                    p_TempDumpPath,
                                ).getBctFileName()
                bctFiles = bctFileNameStr.split()
                bctFiles = [bctFile for bctFile in bctFiles if os.path.exists(bctFile)]

                if (len(bctFiles) == 0):
                    self.vlog("\033[01;31m Warning: No MB1 Multi SKU BCT file found \033[0m")
                else:
                    l_NvImagegenCmd += " --mb1_multi_sku_bct "
                    l_NvImagegenCmd += " ".join(bctFiles)
                    l_NvImagegenCmd += " "

            # Generate hpse-pkg and sb-pkg
            self.GenerateHpseSbPkg(l_Image, p_TempDumpPath, chain)

            # Preprocess Mb2 binary
            if ("mb2-bootloader" in partitionList):
                self.targetConfig.f_NvMb2BinImage = self.GrepBinPathFromConfig(configFileDataList, "mb2", chain)
                self.CopyAndPreprocessMb2Binary(l_Image, p_TempDumpPath)

            # Preprocess IST config binary
            if ("ist-config" in partitionList):
                if (self.targetConfig.n_GrDump is True):
                     if ((l_Image == "flash-images") or (l_Image == "rcm-boot")):
                        self.GrepBinPathFromConfig(configFileDataList, "ist_ict", chain)

                self.CopyAndPreprocessICTBinary(l_Image, p_TempDumpPath)

            # Preprocess dce config binary
            if ("dce-fw" in partitionList):
                self.GrepBinPathFromConfig(configFileDataList, "dce", chain, "dce-dtb.bin")
                self.CopyAndPreprocessDceBinary(l_Image, p_TempDumpPath)

            if ((l_Image == "rcm-boot") or (l_Image == "flash-images")) and (self.targetConfig.n_GrDump is True):
                from grBlob import grBlob
                GrBlob = grBlob(self.targetConfig.p_FlashPath, p_TempDumpPath)
                grBlob.GenerateGrBlobs(GrBlob, self.targetConfig.mediaCombination, self.targetConfig.p_NvGrCsvPath, self.targetConfig.s_BoardName, self.targetConfig.boardDefaultPaths)
                grBlob.AppendBlBlobs(GrBlob, self.gr_mb2_append_bin, self.gr_ist_append_bin)
            if (l_Image == "flash-images" and numberOfChains != 1):
                l_NvImagegenCmd += "--generatechain " + chain

            if (self.targetConfig.n_EncryptedbootFlash is True):
                l_NvImagegenCmd += " --encrypt "
            if (self.targetConfig.n_PlEncryptedbootFlash is True):
                l_NvImagegenCmd += " --pl-ekc " + self.targetConfig.f_PlEncKeyFile


            if (self.targetConfig.n_SecurebootFlash is True):
                #if(len(self.targetConfig.HSMStr) != 0):
                #    l_NvImagegenCmd += " --hsm " + self.targetConfig.HSMAuthStr
                l_NvImagegenCmd += " --pkc " + self.targetConfig.f_PkcKeyFile
                l_NvImagegenCmd += " --pcp " + self.targetConfig.getPcpFilePath()

                if (self.testSecureJtagEnabledForRcmBoot(l_Image) == nverror.NvError_Success):
                    self.vlog("JTAG enabled and ECID matches, enabling JTAG")
                    try:
                        os.rename("rcm_0_signed.rcm", "target_uid_signed.rcm")
                    except:
                        AbnormalTermination("s_ERROR_TOOL_MV", nverror.NvError_FileOperationFailed)
            l_NvImagegenCmd += self.targetConfig.HSMStr + "  " + self.targetConfig.HSMAuthStr

            if (self.targetConfig.s_DtOdmdata != None):
                l_NvImagegenCmd += " --dtodmdata " + self.targetConfig.s_DtOdmdata

            # Sign recovery mb1 separately as it is not done by using nvimagegen
            if (l_Image == "rcm-flash" or l_Image == "rcm-boot"):
                self.GenerateAllRcmMessage(l_Image, p_TempDumpPath)

            if (l_Image == "rcm-flash"):
                self.vlog("Generating Flashing-RCM Images")

                for l_cur_cfg in configFileList:
                    self.GenerateBadPageBlob(l_cur_cfg, p_TempDumpPath)

                l_NvImagegenCmd += " --cfg " + configFileList[0] + " --rcm"
                n_RcmBoot = True
            else:
                if (l_Image == "rcm-boot"):
                    self.vlog("Generating RCM-Boot Images")  # | tee -a self.targetConfig.f_OutFile
                    l_NvImagegenCmd += " --rcm"
                    n_RcmBoot = True
                elif (l_Image == "flash-images"):
                    if (self.targetConfig.s_UpdatePartitions):
                        l_PtCmd = self.flashUtils.f_NvImageGen + " " + l_NvImagegenCmd + " --ptonly"
                        l_NvImagegenCmd += " --part "
                        for imageFile in self.targetConfig.s_UpdatePartitions:
                            chaincheck = chain + '_'
                            if chaincheck in imageFile:
                                l_NvImagegenCmd += imageFile + " "
                            # Is this a Level 1 image
                            if (("A_" not in imageFile) and
                                ("B_" not in imageFile) and
                                ("C_" not in imageFile)):
                                l_NvImagegenCmd += imageFile + " "

                    self.vlog("Generating Flash Images for chain %s, this may take a few minutes" % chain)  # | tee -a self.targetConfig.f_OutFile
                else:
                    self.vlog("Unknown image operations " + l_Image)
                    AbnormalTermination("s_ERROR_OPTION_VALUE_INVALID -- Unknow image operations $l_Image", nverror.NvError_BadParameter)

                # check sub files in cfg file and copy them over
                for l_cur_cfg in configFileList:
                    self.GenerateBadPageBlob(l_cur_cfg, p_TempDumpPath)
                    self.CopyStorageDtbImage(l_cur_cfg, p_TempDumpPath)

                self.VerifyLowPowerPartitions()

                # PT is needed by BCT and MB1
                if (l_PtCmd != None):
                    l_PtCmd +=  " --cfg " + configFileList[0]
                    result = self.shellUtils.executeShellCommand(l_PtCmd, False, False)
                    if (result != nverror.NvError_Success):
                        AbnormalTermination("s_ERROR_TOOL_NVIMAGEGEN", nverror.NvError_NvImagegen)

                l_NvImagegenCmd += " --cfg " + configFileList[0]
                if (self.targetConfig.b_CreateOptionalPartitions == True):
                    l_NvImagegenCmd += " --init_persistent_partitions "

                if (n_SkipFileSystemParts is True):
                    self.vlog("Skipping file system flashing")
                    l_NvImagegenCmd += " --skipfs"

            uid = str(uuid.uuid4()).replace("-", "")
            l_NvImagegenCmd += " --man man_" + uid[0:11] + ".txt"
            l_NvImagegenCmd += " --bct_der_consts " + self.targetConfig.getBCTDerConstFilePath()
            if (len((self.targetConfig.s_buildVersion).strip()) == 0):
                self.targetConfig.s_buildVersion = "012345678ABCDEF"
            l_NvImagegenCmd += " --pvitversion "  + self.targetConfig.s_buildVersion
            if (self.targetConfig.n_NoDuPvit is True):
                l_NvImagegenCmd += " --no_du_pvit  "
            l_NvImagegenCmd = self.flashUtils.f_NvImageGen + " " + l_NvImagegenCmd + " --showpt"

            self.CalculateRequiredSpace(configFileList, n_SkipFileSystemParts, n_RcmBoot)
            if (not self.ProcessIsChild()):
                self.HostSpaceCheck()
            else:
                self.targetConfig.d_ProcessInfo["ctpQueue"].put([self.targetConfig.n_SpaceRequired, True])
                if (self.targetConfig.d_ProcessInfo["ptcQueue"].get() != "OK"):
                    self.vlog("message received from parent process corrupted! Non Fatal!")

            result = self.shellUtils.executeShellCommand(l_NvImagegenCmd, False, False)

            if (result != nverror.NvError_Success):
                AbnormalTermination("s_ERROR_TOOL_NVIMAGEGEN", nverror.NvError_NvImagegen)

            # Check for asymmetric case
            if (self.targetConfig.n_Asymmetric):
                asymmetric = AsymmetricBct(self)
                asymmetric.generate_special_asymmetric_bct(
                    chain, l_Image, partitionList, p_TempDumpPath
                )
                asymmetric.run_nvimagegen_for_special_bct(
                    chain, l_Image, l_NvImagegenCmd, p_TempDumpPath
                )
                asymmetric.generate_l1pt_metadata(l_Image, chain)

            self.DeleteAllBcts(l_Image)

        self.targetConfig.n_SpaceRequired = 100000000
        if ((self.targetConfig.n_WaitOnQueue == False) and (self.ProcessIsChild())):
            self.targetConfig.d_ProcessInfo["ctpQueue"].put([0, self.targetConfig.n_WaitOnQueue])
            if (self.targetConfig.d_ProcessInfo["ptcQueue"].get() != "OK"):
                self.vlog("message received from parent process corrupted! Non Fatal!")

        if (l_Image == "flash-images" and numberOfChains > 1):
            self.GenerateFileToFlash(numberOfChains, p_TempDumpPath)
            self.GenerateBCHPartitionFiles(p_TempDumpPath)

        if (self.targetConfig.n_SecurebootFlash is True):
            self.shellUtils.removeFile(self.targetConfig.f_PkcKeyFile)
        if (self.targetConfig.n_EncryptedbootFlash is True):
            self.shellUtils.removeFile(self.targetConfig.f_EncKeyFile)
            self.shellUtils.removeFile('label.bin')
            self.shellUtils.removeFile('context.txt')
        if (self.targetConfig.n_PlEncryptedbootFlash is True):
            self.shellUtils.removeFile(self.targetConfig.f_PlEncKeyFile)
            self.shellUtils.removeFile('os_label.bin')

    #Check PVIT enabled in the storage configuration
    #  for each chain. If enabled add PVIT partition to the
    #  update partitions list, unlessf
    #  pvit partition on the command line.
    def initializePVITEnableStatus(self):
        pvitPartitionEnabled = False
        attrib = "size"
        attrVal = ""
        self.initializeChainList()
        #generate partiton data for all boot chains in the list
        newParentCfg = os.path.join(os.getcwd(), "temp_" + os.path.basename(self.targetConfig.f_FlashCfg))
        self.storageCfgParser = StorageConfigParser(self.targetConfig)
        self.storageCfgParser.parseConfigFile(self.targetConfig.f_FlashCfg, newParentCfg)
        for  chain in self.targetConfig.l_selectedChains:
            self.targetConfig.d_PvitChainNPartitions[chain]["selState"] = "Selected"
            for userPvit in self.targetConfig.f_refPvitFile:
                if chain == os.path.basename(userPvit)[0]:
                    self.targetConfig.d_PvitChainNPartitions[chain]["pvitTgtUsrFile"] = userPvit
                    break

            pvitPartitionEnabled, attrVal = self.storageCfgParser.findPartAttribVal(chain + "_" + "pvit", attrib)
            self.log( "chain :: " +  chain + "   ---   pvitPartitionEnabled is " + str(pvitPartitionEnabled) + ",  with size - " + str(attrVal) )
            isBootburnPartial = "bootburn.py" in  sys.argv[0]
            istargetRefProvided = (len( self.targetConfig.d_PvitChainNPartitions[chain]["pvitTgtUsrFile"]) > 0)
            PvitDisableInPartial = (not(isBootburnPartial or istargetRefProvided))  and (len(self.targetConfig.s_UpdatePartitions) > 0)
            if (not pvitPartitionEnabled) or PvitDisableInPartial:
                self.targetConfig.d_PvitChainNPartitions[chain]["pvitState"] = "PvitDisabled"
            else:
                self.targetConfig.d_PvitChainNPartitions[chain]["pvitState"] = "PvitEnabled"

            self.updatePvitDictwithSelectedPartitions(chain)

    def initializeChainList(self):
        #set partition list to flash_bsp list (s_UpdatePartitions) if available, else
        # set to create_bsp list (s_CreatePartialPartitions)
        if self.targetConfig.s_UpdatePartitions:
            updatePartitions = self.targetConfig.s_UpdatePartitions
        elif self.targetConfig.s_CreatePartialPartitions:
            updatePartitions = self.targetConfig.s_CreatePartialPartitions
        else:
            updatePartitions = ""
        self.targetConfig.l_selectedChains = []
        #Getchain information from command line parameters, if provided
        if (self.targetConfig.generateChain != None) and (len(self.targetConfig.generateChain.strip()) != 0):
            self.targetConfig.l_selectedChains.append(self.targetConfig.generateChain.strip())
        elif updatePartitions:
            for partition in updatePartitions:
                if (partition[1] == '_') and partition[0] not in self.targetConfig.l_selectedChains:
                    self.targetConfig.l_selectedChains.append(partition[0])
        elif self.targetConfig.platformConfig != None and  self.targetConfig.platformConfig.get('bct_defines') != None:
                for chain in self.targetConfig.platformConfig['bct_defines']["chains"]:
                    self.targetConfig.l_selectedChains.append(chain.partition('_')[2])
        self.vlog(f"selected chains in initializeChainList: {self.targetConfig.l_selectedChains}")

    def updatePvitDictwithSelectedPartitions(self, chain, cleanPart=False):

        if (cleanPart):
            self.targetConfig.d_PvitChainNPartitions[chain]["partList"].clear()

        if self.targetConfig.s_UpdatePartitions:
            for partition in self.targetConfig.s_UpdatePartitions:
                if chain == partition[0] and ('_' == partition[1]) and (partition not in self.targetConfig.d_PvitChainNPartitions[chain]["partList"]):
                    self.targetConfig.d_PvitChainNPartitions[chain]["partList"].append(partition)
            if self.targetConfig.d_PvitChainNPartitions[chain]["pvitState"] == "PvitDisabled":
                return
            if self.targetConfig.d_PvitChainNPartitions[chain]["pvitPart"] not in self.targetConfig.d_PvitChainNPartitions[chain]["partList"]:
                self.targetConfig.d_PvitChainNPartitions[chain]["partList"].append(self.targetConfig.d_PvitChainNPartitions[chain]["pvitPart"])
            if self.targetConfig.d_PvitChainNPartitions[chain]["pvitPart"] not in self.targetConfig.s_UpdatePartitions:
                self.targetConfig.s_UpdatePartitions.append(self.targetConfig.d_PvitChainNPartitions[chain]["pvitPart"])
    def RenameFilewithSignext(self, fileName, rename=True):
        signstring = ""
        fileNamewithSignExt = ""
        if (self.targetConfig.n_SecurebootFlash == True):
            signstring = "signed"
        else:
            signstring = "zerosign"
        #If it is already done, don't do it again
        if signstring not in os.path.basename(fileName) and os.path.isfile(fileName):
            filenameparts = fileName.rsplit(".", 1)
            if len(filenameparts) > 1:
                fileNamewithSignExt = filenameparts[0]+ "_" + signstring + "." + filenameparts[1]
            else:
                self.vlog(f"No extension found for {fileName}")
                return fileName

            fileNamewithSignExt = os.path.join(os.path.dirname(fileName), fileNamewithSignExt)
            if (rename) and (len(fileNamewithSignExt) != 0):
                os.renames(fileName, fileNamewithSignExt)

            return fileNamewithSignExt
        else:
            return fileName

    def UpdateFileNameWithSignStatus(self, lineParts, fileToFlash=True):
        """UpdatePVIT entry with signed file name and
           with its signed name

        Args:
            lineparts (list): A split input line

        Raises: None
        Retrun value:  updated line
        """
        signstring = ""
        if (self.targetConfig.n_SecurebootFlash == True):
            signstring = "signed"
        else:
            signstring = "zerosign"
        # If we have already done this don't do it again
        if (signstring not in lineParts[2]) and fileToFlash:
            filenameparts = lineParts[2].partition(".")
            filenameparts = filenameparts[0]+ "_" + signstring + filenameparts[1] + filenameparts[2]
            lineParts[2] = filenameparts
        if (signstring not in lineParts) and not fileToFlash:
            filenameparts = lineParts.partition(".")
            lineParts = filenameparts[0]+ "_" + signstring + filenameparts[1] + filenameparts[2]
        return lineParts

    def UpdateFileNameinFileToFlash(self, partName):
        """Update FileName as per signing status
            "zerosign" or "signed"

        Args:
            Entry of the partition Name to be updated

        Raises:

        Return value: True on success. False on Fail
        """
        retVal = 0
        fileToFlash = "FileToFlash.txt"
        with open(fileToFlash, "r") as fp:
            lines = fp.readlines()
        for lineno in range(len(lines)):
            if (len(lines[lineno].strip()) == 0):
                continue
            startChar = (lines[lineno].strip())[0]
            if (startChar  == '#'):
                continue

            lineParts = lines[lineno].split(" ")
            if (partName in lineParts[1]):
                lineParts = self.UpdateFileNameWithSignStatus(lineParts)
                line = ' '.join(lineParts)
                lines[lineno] = line
                retVal += 1
                break
        if (retVal):
            with open(fileToFlash, "w") as fp:
                fp.writelines(lines)
        return retVal

    def UpdateImageMd5SuminFileToFlash(self, partName):   #, flashImagesDirPath):
        """UpdateImageMd5SuminFileToFlash for the
           partition name passed.

        Args:
            partName(str): The partition name to be Md5Sum updated
            filetoFlash(str): path to FileToFlash.txt
        Returns  number of replaced lines

        """
        retval = 0
        fileToFlash = "FileToFlash.txt"
        with open(fileToFlash, "r") as fp:
            lines = fp.readlines()
        for lineno in range(len(lines)):
            if (len(lines[lineno].strip()) == 0):
                continue
            startChar = (lines[lineno].strip())[0]
            if ( startChar  == '#'):
                continue
            lineParts = lines[lineno].split(" ")
            if (partName in lineParts[1]):
                self.log(partName + " partition is found in " + lineParts[2])
                md5cmd = "md5sum " + lineParts[2]
                result = self.shellUtils.executeShellCommand(md5cmd, True)
                lineParts[10] = result.split(" ")[0]
                line = ' '.join(lineParts)
                lines[lineno] = line
                retval += 1
                break
        if (retval):
            with open(fileToFlash, "w") as fp:
                fp.writelines(lines)
        return retval

    def UpdateMd5Sum(self, fileToFlash, lineParts):
        """UpdateMd5Sum If a PT and PVIT  entry is updated and signed then
           insure that the md5sum is correct as if we recreated
           then the signature will change

        Args:
            filetoFlash (file): (file) Output File
            lineParts (list): A split input line

        Raises:
        """
        md5cmd = "md5sum " + lineParts[2]
        result = self.shellUtils.executeShellCommand(md5cmd, True)
        md5sum = result.split(" ")[0]
        lineParts[10] = md5sum
        line = ' '.join(lineParts)
        fileToFlash.write(line)

    def CompareFileswithMD5(self, file1, file2):
        '''Generate MD5 checksum for the partition files
        Args:
            file1 : partition file including bch
            file2 " partition file including bch
        '''
        retVal = False
        #generate MD5 for file-1 & file-2
        md5cmd = "md5sum " + file1
        result = self.shellUtils.executeShellCommand(md5cmd, True)
        md5sum1 = result.split(" ")[0]
        md5cmd = "md5sum " + file2
        result = self.shellUtils.executeShellCommand(md5cmd, True)
        md5sum2 = result.split(" ")[0]
        if (md5sum1 == md5sum2):
            retVal = True
        return retVal


    def GenerateFileToFlash(self, numberOfChains, flashImagesDirPath):
        """Merge chain specific <chain>_FileToFlash.txt files
           into a top level FileToFlash.txt file which is used for
           flashing

        Args:
            numberOfChains (int): Number of chains in the storage config
            flashImagesDirPath (str): flash-images directory path

        Raises:
            AbnormalTermination: raises exception if any of the chain specific
                                 <chain>_FileToFlash.txt files is not found.
        """

        outputFileToFlash = os.path.join(flashImagesDirPath, "FileToFlash.txt")
        inputFileToFlashList = []
        for i in range(numberOfChains):
            chain = chr(ord('A') + i)

            if (self.targetConfig.generateChain):
                if (self.targetConfig.generateChain != chain):
                    continue

            chainFileToFlashPath = os.path.join(flashImagesDirPath, chain + "_" + "FileToFlash.txt")
            if (not os.path.exists(chainFileToFlashPath)):
                raise AbnormalTermination("Chain %s specific file to flash not found: %s" % (chain, chainFileToFlashPath))

            inputFileToFlashList.append(chainFileToFlashPath)

        self.MergeFileToFlash(inputFileToFlashList, outputFileToFlash)
        for fileToFlashFile in inputFileToFlashList:
            os.remove(fileToFlashFile)

    def GenerateBCHPartitionFiles(self, flashImagesDirPath):
        """Generate file BCHPartitions.txt that contains the
        partition names of BCH protected partitions (ImageHeaderType = 12).

        Args:
            flashImagesDirPath (str): flash-images directory path

        Raises:
            AbnormalTermination: raises exception if FileToFlash.txt
            file is not found.
        """
        fileToFlash = os.path.join(flashImagesDirPath, "FileToFlash.txt")

        if (not os.path.exists(fileToFlash)):
            raise AbnormalTermination("File to flash not found: %s" % (fileToFlash))

        outputBCHPartitionsFile = os.path.join(flashImagesDirPath, "BCHPartitions.txt")

        with open(outputBCHPartitionsFile, "w") as bchPartitions:
            with open(fileToFlash, "r") as fp:
                lines = fp.readlines()
                for line in lines:
                    lineParts = line.split(" ")
                    if lineParts[9] == '12':
                        bchPartitions.write(lineParts[1]  + "\n")

    def MergeFileToFlash(self, inputFileToFlashList, outputFileToFlash, l1ptChainId = 'A'):
        """Merge input FileToFlash.txt files into single output FileToFlash.txt
           Copy all the files from first chain and copy only chain specific files
           for rest of the chains

        Args:
            inputFileToFlashList (list): List of input FileToFlash.txt file paths
            outputFileToFlash (str): Output FileToFlash.txt file path
        """
        with open(outputFileToFlash, "w") as fileToFlash:
            for i in range(len(inputFileToFlashList)):
                with open(inputFileToFlashList[i], "r") as fp:
                    lines = fp.readlines()
                    for line in lines:
                        lineParts = line.split(" ")
                        if (chr(ord('A') + i) == l1ptChainId):
                            if ("_PT" in lineParts[2]):
                                self.UpdateMd5Sum(fileToFlash, lineParts)
                            elif (lineParts[1][0] >= 'A' and lineParts[1][0] <= 'Z' and lineParts[1][1] == '_') or lineParts[0][0] == '#':
                                fileToFlash.write(line)
                            else:
                                self.UpdateMd5Sum(fileToFlash, lineParts)
                        else:
                            if (lineParts[1][0] >= 'A' and lineParts[1][0] <= 'Z' and lineParts[1][1] == '_') or lineParts[0][0] == '#':
                                if ("_PT" in lineParts[2]):
                                    self.UpdateMd5Sum(fileToFlash, lineParts)
                                else:
                                    fileToFlash.write(line)
                            elif lineParts[1] == chr(ord('A') + i) + "_bct":
                                fileToFlash.write(line)

    # arg 1 contains cfg file to search for file
    def GenerateBadPageBlob(self, l_configFile, p_TempDumpPath):
        if (os.path.isfile(l_configFile) is False):
            self.vlog("could not find config to parse " + l_configFile)
            AbnormalTermination("s_ERROR_INVALID_PARAMS", nverror.NvError_BadParameter)

        l_BadPageFound = self.shellUtils.grep(l_configFile, self.targetConfig.s_BadPage)
        if (l_BadPageFound):
            # "Generating bad page blob"
            result = self.shellUtils.executeShellCommand("dd if=/dev/zero of=" + os.path.join(p_TempDumpPath, self.targetConfig.s_BadPage) + " bs=4096 count=1")
            if (result != nverror.NvError_Success):
                self.vlog("Unable to Generate bad page blob!")
                AbnormalTermination("s_ERROR_BOOTBURN_INTERNAL", nverror.NvError_BadParameter)

    def GenerateAllBCTs(self, l_Operation):
        self.log("Generating BCT files")
        self.GenerateBCT("br_bct", l_Operation)
        self.targetConfig.f_BRBCT = "bct_BR.bct"

        if(os.path.isfile(self.targetConfig.f_BRBCT) is False):
            AbnormalTermination("f_BRBCT = " + self.targetConfig.f_BRBCT + " not found", -1)

        if (self.targetConfig.n_GenBinsOnly is not True):
            # Not create_bsp_images.py hence preserve target sku information
            self.PreserveTargetSkuInfo(self.targetConfig.f_BRBCT)

        if (self.targetConfig.customerData.isLoaded() or self.targetConfig.f_SignedCustomerData != None):
            self.SkuBlobGen(self.targetConfig.f_BRBCT)


        self.GenerateBCT("mb1_bct", l_Operation)
        self.targetConfig.f_MB1BCT = "bct_MB1.bct"

    def CreateRCMFlashImages(self, l_configFile, l_Operation, p_TempDumpPath):
        self.vlog(" ****  In CreateRCMFlashImages ****")
        l_SubOperation = ""
        if (l_Operation == "rcm-flash-provision"):
            l_Operation = "rcm-flash"
            l_SubOperation = "provision"

        cwd = os.getcwd()
        operationPath = os.path.join(self.targetConfig.p_OutDirPath, l_Operation)
        operationPathProvision =  os.path.join(operationPath, l_SubOperation)
        self.shellUtils.makeDirectory(operationPath)

        self.HostSpaceCheck()
        self.CopyTegraFlashUtilities(operationPath)
        self.CopyMTSAndFuseByPassBinaries(operationPath)

        if (l_Operation == "rcm-flash"):
            self.CopyFlashingDtbImg(operationPath)
            self.CopyAndPreprocessMb2Binary(l_Operation, operationPath)
            if (l_SubOperation == "provision"):
                self.targetConfig.n_UFSProvisioningEnable = True
                self.ProvisionUFS(operationPath)
                self.log("overlay.dtb path :: " + os.path.join(operationPath, "provision_overlay.dtb"))
                #"Move provision modified dtb to linux dtb"
                os.rename(os.path.join(operationPath, "provision_overlay.dtb"), os.path.join(operationPath, self.targetConfig.s_LinuxDtbImgName))

            # TODO: Remove CopyAndPreprocessQbBinaries once we have prebuilt qb binary
            self.CopyAndPreprocessQbBinaries(l_Operation, operationPath)
        elif (l_Operation == "rcm-boot"):
            self.EmptyGrBlBinLists()
            self.CopyAndPreprocessISTBinary(l_Operation, operationPath)
            self.CopyAndPreprocessQbBinaries(l_Operation, operationPath)
            self.CopyAndPreprocessMb2Binary(l_Operation, operationPath)
            if self.targetConfig.n_GrDump is True:
                from grBlob import grBlob
                GrBlob = grBlob(self.targetConfig.p_FlashPath, operationPath)
                grBlob.GenerateGrBlobs(GrBlob, self.targetConfig.mediaCombination, self.targetConfig.p_NvGrCsvPath, self.targetConfig.s_BoardName, self.targetConfig.boardDefaultPaths)
                grBlob.AppendBlBlobs(GrBlob, self.gr_mb2_append_bin, self.gr_ist_append_bin)
        else:
            self.vlog("unknow operation l_Operation")
            AbnormalTermination("s_ERROR_OPTION_UNKNOWN -- unknow operation l_Operation", nverror.NvError_BadParameter)
        os.chdir(operationPath)

        # format of new filename: tmp_<basename>.cfg
        if (l_Operation == "rcm-flash"):
            l_configFile = os.path.join(self.targetConfig.p_FlashingCfgPath, l_configFile)

        self.resetLowPowerPartitions()
        parentCfgDir = os.path.dirname(l_configFile)
        newParentCfg = os.path.join(operationPath, "tmp_" + os.path.basename(l_configFile))
        self.CfgFileParser(newParentCfg, l_configFile)

        configFiles = [newParentCfg]
        self.findAndParseAllSubConfigFiles(newParentCfg, configFiles, parentCfgDir)

        self.GenerateAllBCTs(l_Operation)
        self.GenerateImage(configFiles, l_Operation, operationPath)
        self.CleanupTegraFlashUtilities(l_Operation, operationPath)
        if (l_SubOperation == "provision"):
            operationPathProvision =  operationPath+"-"+l_SubOperation
            if (os.path.isdir(operationPathProvision)):
                shutil.rmtree(operationPathProvision)
            os.rename(operationPath, operationPathProvision)
        os.chdir(cwd)

    def DrvUpdtFileToFlash(self):
        flashfile = "./FileToFlash.txt"

        copyReplaceDict = {}
        partitionTobeReplaced = ""
        partitionToCopy = ""
        lineTobeCopied = ""
        lineTobeReplaced = ""
        replaceFieldsList = [3,11]
        comparePartitionField = 2
        separator = " "
        replacedCount = 0

        self.log('In DrvUpdtFileToFlash start')

        copyReplaceDict["A_[1-9]_mb1-ota"] = "B_mb1-bootloader"
        copyReplaceDict["B_[1-9]_mb1-ota"] = "A_mb1-bootloader"


        with open(flashfile, 'r') as fid:
            flines = list(fid)
            self.log ('created list of lines from FileToFlash.txt file')
        with open(flashfile, 'r') as fid:
            updatedList = list(fid)
            self.log ("updatedList length :: " + str(len(updatedList)))

        for partitionTobeReplaced, partitionToCopy in copyReplaceDict.items():
            self.log("partitionTobeReplaced :: " + partitionTobeReplaced)
            self.log("partitionToCopy       :: " + partitionToCopy)

            lineTobeReplaced = ""
            lineTobeCopied   = ""
            fieldsTobeReplaced = []
            fieldsTobeCopied   = []
            linesTobeReplaced  = ""
            lineNum = 0

            for line in flines:
                lineNum += 1
                if (partitionToCopy in line):
                    lineTobeCopied = line
                    self.log("lineTobeCopied in the loop :: " + line)
                    fieldsTobeCopied = line.split()
                    # Save name for latter
                    origName = fieldsTobeCopied[2]
                    continue
                fieldsTobeReplaced = line.split()
                if(fnmatch.fnmatch(fieldsTobeReplaced[comparePartitionField-1], partitionTobeReplaced)):
                    lineTobeReplaced = line
                    self.log("lineTobeReplaced in the loop :: " + line)
                    fieldsTobeReplaced = lineTobeReplaced.split()

                if (len(lineTobeCopied) != 0) and (len(lineTobeReplaced) != 0):
                    self.log("found the match to copy and replace with lineNum :: " + str(lineNum))
                    self.log("copy the required fields :: " + str(replaceFieldsList))
                    for index in range(len(replaceFieldsList)):
                        if replaceFieldsList[index] == 3:
                            # Rename to avoid file name contention with rm on target
                            newName = origName + os.path.basename(fieldsTobeReplaced[0])
                            fieldsTobeReplaced[int(replaceFieldsList[index])-1] = newName
                            self.shellUtils.Copy(origName, newName)
                        else:
                            fieldsTobeReplaced[int(replaceFieldsList[index])-1] = fieldsTobeCopied[int(replaceFieldsList[index])-1]
                        self.log("selected fieldsTobeReplaced :: " + str(fieldsTobeCopied[int(replaceFieldsList[index])-1] ))
                    updatedLine = separator.join(fieldsTobeReplaced) + "\n"
                    self.log("reconstructed lineTobeReplaced :: " + str(updatedLine))
                    updatedList [lineNum-1] = updatedLine
                    replacedCount += 1
                    break
            self.log("Drive Update MB1 binary completed ")

        if (replacedCount > 0):
            with open(flashfile, 'w+') as fid:
                for line in updatedList:
                    fid.write(line)
                self.log ('created list of lines from FileToFlash.txt file')


    def CreateFlashImages(self, l_configFile):
        l_encrypt = ""
        n_RcmMode = self.targetConfig.parsed_commandline["R"]
        l_Operation = "flash-images"
        cwd = os.getcwd()
        chipID = str(self.targetConfig.GetChipID())

        if (self.targetConfig.n_EncryptedbootFlash is True):
            l_encrypt = "_encrypt"

        if (self.targetConfig.n_SecurebootFlash is True):
            l_SigBRBCT = "A_bct_BR{}_signed.bct".format(l_encrypt)
        else:
            l_SigBRBCT = "A_bct_BR{}_zerosign.bct".format(l_encrypt)

        p_TempDumpPath = os.path.join(self.targetConfig.p_OutDirPath, l_Operation)
        self.shellUtils.makeDirectory(p_TempDumpPath)

        self.HostSpaceCheck()
        self.CopyTegraFlashUtilities(p_TempDumpPath)
        self.CopyMTSAndFuseByPassBinaries(p_TempDumpPath)
        self.EmptyGrBlBinLists()
        self.CopyAndPreprocessISTBinary(l_Operation, p_TempDumpPath)
        self.CopyAndPreprocessQbBinaries(l_Operation, p_TempDumpPath)
        self.CopyAndPreprocessMb2Binary(l_Operation, p_TempDumpPath)
        if self.targetConfig.n_GrDump is True:
            from grBlob import grBlob
            GrBlob = grBlob(self.targetConfig.p_FlashPath, p_TempDumpPath)
            grBlob.GenerateGrBlobs(GrBlob, self.targetConfig.mediaCombination, self.targetConfig.p_NvGrCsvPath, self.targetConfig.s_BoardName, self.targetConfig.boardDefaultPaths)
            grBlob.AppendBlBlobs(GrBlob, self.gr_mb2_append_bin, self.gr_ist_append_bin)
        os.chdir(p_TempDumpPath)

        # format of new filename: tmp_<basename>.cfg
        self.resetLowPowerPartitions()
        parentCfgDir = os.path.dirname(l_configFile)
        newParentCfg = os.path.join(p_TempDumpPath, "tmp_" + os.path.basename(l_configFile))
        self.CfgFileParser(newParentCfg, l_configFile)

        configFiles = [newParentCfg]
        self.findAndParseAllSubConfigFiles(newParentCfg, configFiles, parentCfgDir)

        self.GenerateBCT("br_bct", l_Operation)

        # There is no rcm-flash images geneareted if using create bsp with -R
        # There is no rcm-boot images generated if flashing through bootburn directly
        if (n_RcmMode is True):
            skuinfoCmd = " --chip " + chipID + " --inbct ../rcm-boot/" + l_SigBRBCT + " --outbct ./bct_BR.bct"
            result = self.shellUtils.executeShellCommand(self.flashUtils.f_NvSkuInfo + skuinfoCmd)
            if (result != nverror.NvError_Success):
                AbnormalTermination("s_ERROR_TOOL_NVSKUINFO", nverror.NvError_TegraSkuInfoError)

        else:
            skuinfoCmd  = self.flashUtils.f_NvSkuInfo
            skuinfoCmd += " --chip " + chipID + " --inbct ../rcm-flash/" + l_SigBRBCT + " --outbct ./bct_BR.bct"

            result = self.shellUtils.executeShellCommand(skuinfoCmd)
            if (result != nverror.NvError_Success):
                AbnormalTermination("s_ERROR_TOOL_NVSKUINFO", nverror.NvError_TegraSkuInfoError)

        self.targetConfig.f_BRBCT = "bct_BR.bct"
        self.GenerateBCT("mb1_bct", l_Operation)
        self.targetConfig.f_MB1BCT = "bct_MB1.bct"
        self.GenerateImage(configFiles, l_Operation, p_TempDumpPath)
        self.DrvUpdtFileToFlash()
        self.CleanupTegraFlashUtilities(l_Operation, p_TempDumpPath)
        os.chdir(cwd)

    def UnknownDevice(self, l_FileToFlash):
        l_device = ""
        s_deviceList = []

        fileToFlash = self.shellUtils.catFile(l_FileToFlash)

        for line in fileToFlash.splitlines():
            # remove all characters after comment hashtag
            line = line.split("#")[0]
            # remove leading white space
            line = line.rstrip()
            if(not line):
                continue

            partitionInfo = line.split(" ")

            l_linuxname = partitionInfo[0]
            l_device = os.path.basename(l_linuxname)

            if(l_device.find(':') != -1):
                suffix = l_device.find(':')
                l_device = l_device[:suffix]

            if(l_device.find('_') != -1 and l_device.find('_boot') == -1):
                suffix = l_device.find('_')
                l_device = l_device[:suffix]

            self.targetConfig.s_device_name_list.append(l_device)
            if((l_device in s_deviceList) is False):
                s_deviceList.append(l_device)

        return s_deviceList

    def ParseFilesToFlash(self, l_FileName, l_device, l_OutFile, l_GetSize):
        if(os.path.isfile(l_FileName) is False):
            self.vlog("invalid file path " + l_FileName)
            AbnormalTermination("Invalid File Path", nverror.NvError_FileNameNotExist)

        fid = open(l_FileName, "r")
        output = open(l_OutFile, "w+", stat.S_IREAD | stat.S_IWRITE | stat.S_IRGRP | stat.S_IWGRP | stat.S_IROTH | stat.S_IWOTH)

        for line in fid:

            # remove leading white space
            line = line.strip()
            if(not line or line[0] == '#'):
                continue

            # remove all characters after comment hashtag
            line = line.split("#")[0]

            partitionInfo = line.split()
            partitionDevice = partitionInfo[0]
            partitionName = partitionInfo[1]
            partitionBinary = partitionInfo[2]
            partitionSize = partitionInfo[4]

            partitionDevice = os.path.basename(partitionDevice)
            if (":" in partitionDevice):
                partitionDevice = partitionDevice.split(":")[0]

            if (partitionDevice == l_device):
                if(os.path.isfile(partitionBinary) is False):
                    self.vlog("In " + l_FileName + " Invalid binary file " + partitionBinary)
                    AbnormalTermination("Binary " + partitionName + " not found", nverror.NvError_FileNotFound)

                # For Qspi we need FileSize for erase
                if (l_GetSize is True):
                    if (partitionName == "bct"):
                        FileSize = partitionSize
                    else:
                        FileSize = os.path.getsize(partitionBinary)

                    line = line + " {}".format(FileSize)

                self.writeToFile(output, line + "\n")

        fid.close()
        output.close()

    def ParallelFlashImages(self, flashPath, queue):
        activeProcess = []
        exitcode = 0
        adbObj = bootburn_adb(self.targetConfig, self.flashUtils)

        deviceList = self.UnknownDevice("FileToFlash.txt")

        for device in deviceList:
            if (self.targetConfig.devicetype != None):
                if (self.targetConfig.devicetype not in device):
                    self.log("Skipping " + device)
                    continue

            self.log("Working on " + device)
            l_count = 0
            l_found = 0
            for deviceName in self.targetConfig.s_device_name_list:
                if (device == deviceName):
                    l_found = True
                    if (device == "3270000.spi" or device == "3300000.spi"):
                        l_erase = True
                    else:
                        l_erase = False
                    break
                l_count = l_count + 1
            if (l_found):
                fileToFlash = self.targetConfig.s_device_name_list[l_count]
                self.ParseFilesToFlash("FileToFlash.txt", deviceName, fileToFlash, l_erase)

                # make process daemon = true  so that it will die when the main
                # process exits.  No need for clean up code
                process = Process(target=adbObj.FlashUsingADB, args=(fileToFlash, self.targetConfig.s_UpdatePartitions, flashPath, queue, ))
                #process.daemon = True
                process.start()
                activeProcess.append(process)
            else:
                AbnormalTermination("s_ERROR_DEVICE_NOT_FOUND -- " + device, nverror.NvError_DeviceNotFound)

        for process in activeProcess:
            process.join()
            # Has process exited?
            if (process.exitcode != None):
                # Yes.  Was status an error?
                if (process.exitcode != 0):
                    # Save exit code if we haven't already
                    if (exitcode == 0):
                        exitcode = process.exitcode

        if (exitcode != 0):
            AbnormalTermination("Flashing Failed Propagated Error", exitcode)

    # generates SHA512 of the image and signs using nvimagegen --sign
    def generateSHA512andsignImage(self, imageToSign, imgMagicId):
        bchSize = 0x2000
        binSizeOffset = 0x1404
        l_NvImagegenCmd = self.flashUtils.f_NvImageGen
        cwd = os.getcwd()
        self.CopyTegraSign(cwd)

        with open(imageToSign, 'rb') as ifd:
            dataBuf = ifd.read()
        pvitSize = int.from_bytes(dataBuf[binSizeOffset:binSizeOffset+4], 'little')
        dataBuf = dataBuf[bchSize:bchSize+pvitSize]
        with open(imageToSign, 'wb') as ofd:
            ofd.write(dataBuf)
        imageToResign = ""
        if (self.targetConfig.n_SecurebootFlash is True):
            imageToResign = imageToSign.replace("_signed", "")
            self.gen_pcp()               #generates if pcp is not available
            if(len(self.targetConfig.HSMStr) == 0):
                self.targetConfig.copyPkcKey(cwd)
        else:
            imageToResign = imageToSign.replace("_zerosign", "")
        os.replace(imageToSign, imageToResign)

        if (self.targetConfig.n_SecurebootFlash is True):
            l_NvImagegenCmd += " --pcp " + self.targetConfig.getPcpFilePath()
            if (len(self.targetConfig.HSMStr) == 0) and (self.targetConfig.f_PkcKeyFilePath is not None) and (self.targetConfig.f_PkcKeyFilePath):
                self.shellUtils.Copy(self.targetConfig.f_PkcKeyFilePath, os.path.dirname(os.path.realpath(self.flashUtils.f_NvImageGen)), True, True)
                self.vlog("keys are available for signing,signing the image")
            l_NvImagegenCmd += " --pkc " + self.targetConfig.f_PkcKeyFile
        l_NvImagegenCmd += self.targetConfig.HSMStr + "  " + self.targetConfig.HSMAuthStr
        l_NvImagegenCmd += " --signbin " + imageToResign + "  " + imgMagicId  + " "
        l_NvImagegenCmd += " --chip " + str(self.targetConfig.GetChipID()) + " "
        result = self.shellUtils.executeShellCommand(l_NvImagegenCmd)
        if (result != nverror.NvError_Success):
            AbnormalTermination("s_ERROR_TOOL_NVIMAGEGEN", nverror.NvError_NvImagegen)
        if (self.targetConfig.s_UpdatePartitions) and os.path.isfile(imageToResign):
            os.remove(imageToResign)
        #partition is the partition name in FileToFlash.txt
    def getBrBctCryptoSha(self, partition, flashImagePath, adbObj):
        bctEntries = self.getBrBctEntriesFromFileToFlash(adbObj)

        if partition in  bctEntries:
            flashImgPath = os.path.join(flashImagePath, bctEntries[partition].FileName)
        else:
            AbnormalTermination("Partition not found in FileToFlash.txt: " + partition, nverror.NvError_InvalidState)


        shaCmd = self.flashUtils.f_NvSkuInfo + " --chip " + str(self.targetConfig.GetChipID())
        shaCmd += " --inbct " + flashImgPath
        shaCmd += " --get-sha"
        result = self.shellUtils.executeShellCommand(shaCmd, True)
        print(f"BrBctCryptoSha :: {result}")

        # convert hex string into bytes and return
        return bytes.fromhex(result.strip())

    #update the pvit provided with the current pvit image data
    # Input parameters:
    #    pvitPartition, selected partition list
    def updateTargetPvitInPartial(self,chain, flashImgPath, adbObj=None):
        partMagicSize = 4
        partMagicOffset = 0x1400
        partBinOffset = 0x1404
        NVDAMagicWord = 'NVDA'
        pvitMagicWord = 'PVIT'
        bchSize = 0x2000
        pvitTgtImgSize = 0x4000
        pvitRecordSize = 108
        #change later with the table entry count
        maxNumPvitRecords = 120
        pvitPartreadSize = 0x4000
        maxGuestNum = 0x32      # used for non L3 partitions
        pvitSHASize = 0x40

        #Get the target\user pvit
        pvitImg_UsrRTarget = self.targetConfig.d_PvitChainNPartitions[chain]["pvitTgtUsrFile"]
        #Get the pvit generated (zerosign or signed)
        self.targetConfig.d_PvitChainNPartitions[chain]["pvitImg"] = self.RenameFilewithSignext(self.targetConfig.d_PvitChainNPartitions[chain]["pvitImg"], False)
        pvitImg_generated = os.path.join(flashImgPath, self.targetConfig.d_PvitChainNPartitions[chain]["pvitImg"])
        #Add error capture statements to indicate the exact fail to the user #FIXME
        #read current host pvit and usr/tgt pvit
        with open (pvitImg_generated, mode="rb") as sfd:
            src_data_buf = sfd.read()
        if (NVDAMagicWord in str(src_data_buf[:partMagicSize])) and (pvitMagicWord in str(src_data_buf[partMagicOffset:partMagicOffset+partMagicSize])):
            src_data_buf = src_data_buf[bchSize:]
        with open (pvitImg_UsrRTarget, mode="rb") as dfd:
            dst_data_buf = dfd.read(pvitPartreadSize)
        # update PVIT entries from current
        for partName in self.targetConfig.d_PvitChainNPartitions[chain]["partList"]:
            self.vlog("finding partiton " + partName)
            if(partName[3] == "_"):
                partition = partName[4:] # remvoe chain_prefix and Guest_prefix
            elif (partName[1] == "_"):
                partition = partName[2:]  # remove chain_ prefix
            else:
                partition = partName
            #skip the pvit partition
            if ("pvit" in partition):
                continue

            srcPvitEntry = 1
            srcPartFound = False
            while (not srcPartFound):
                src_buf = src_data_buf[(srcPvitEntry-1)*pvitRecordSize:(srcPvitEntry)*pvitRecordSize]
                src_buf_str = str(src_buf)
                if (7 == src_buf_str.find(partition)):
                    self.vlog("partition " + partition + " found in " + self.targetConfig.d_PvitChainNPartitions[chain]["pvitImg"])
                    srcPartFound = True
                    break;
                srcPvitEntry += 1
                if (srcPvitEntry>maxNumPvitRecords) or (srcPvitEntry*pvitRecordSize > len(src_data_buf)):
                    break
            if (srcPartFound == False):
                self.vlog(" partition " + str(partition) + " not found in  generated PVIT partition image")
                self.vlog(f"src_data_buf :: {src_data_buf}")
                AbnormalTermination("Selected partition " + partition + " not found in partial Flashing PVIT", nverror.NvError_ImageMismatch)
            else:
                self.vlog(" partition " + str(partition) + " is found in  generated PVIT partition image")

            tgtPvitEntry = 1
            tgtPartFound = False
            while (not tgtPartFound):
                dst_buf = str(dst_data_buf[bchSize+(tgtPvitEntry-1)*pvitRecordSize:bchSize+tgtPvitEntry*pvitRecordSize])
                if (7 == dst_buf.find(partition)):
                    self.vlog("partition " + partition + " found in " + pvitImg_UsrRTarget)
                    tgtPartFound = True
                    break
                tgtPvitEntry += 1
                if (tgtPvitEntry>maxNumPvitRecords) or (tgtPvitEntry*pvitRecordSize > len(dst_data_buf)):
                    break
            if (tgtPartFound == False):
                self.vlog(" partition " + str(partition) + " not found in target PVIT partition")
                self.vlog("No bct partition found in target PVIT, skip the bct update")
                self.vlog(f"dst_buf :: {dst_buf}")
                AbnormalTermination("Selected partition " + partition + " not found in Target PVIT", nverror.NvError_ImageMismatch)

             #updte target PVIT with the PVIT entry for the selected partition
            dst_data_buf = dst_data_buf.replace(dst_data_buf[bchSize+(tgtPvitEntry-1)*pvitRecordSize:bchSize+tgtPvitEntry*pvitRecordSize],  src_buf)
            # If signed customer data is preserved, then udate the sha of BCT into PVIT
            # because stored SHA will be invalid in all use cases, both full and partial
            if (partition == "bct" and self.targetConfig.b_IsBrBctSignedCustomerDataPreserved):
                bctWithChain = chain + '_' + partition
                brBctSha = self.getBrBctCryptoSha(bctWithChain, flashImgPath, adbObj)
                if brBctSha:
                    self.vlog(f"BR BCT signed customer data updated for partition {partition}...updating PVIT with new SHA {brBctSha.hex()}")
                    # Copy the sha of BCT into PVIT
                    pvitShaOffsetStart = bchSize + (tgtPvitEntry) * pvitRecordSize - pvitSHASize
                    pvitShaOffsetEnd = bchSize + (tgtPvitEntry) * pvitRecordSize
                    dst_data_buf = dst_data_buf.replace(dst_data_buf[pvitShaOffsetStart:pvitShaOffsetEnd], brBctSha)
                else:
                    AbnormalTermination(f"Failed to get BR BCT SHA for partition bct_nr from FileToFlash.txt",
                                        nverror.NvError_ImageMismatch)

        #update usr-ref/target pvit with the updated pvit data
        with open(pvitImg_generated, mode="wb") as fd:
            if (None == fd.write(dst_data_buf)):
                self.vlog("Error in writing to  " + pvitImg_generated)
                AbnormalTermination("Write failed on (%s)" % str(pvitImg_generated), nverror.NvError_FileWriteFailed)
        self.generateSHA512andsignImage(pvitImg_generated, self.targetConfig.s_PVITImgMagicId)
        self.UpdateFileNameinFileToFlash(chain+ "_" + "pvit")
        self.UpdateImageMd5SuminFileToFlash(chain + "_" + "pvit")

    def getBrBctEntriesFromFileToFlash(self, adbObj):
        bctEntries = {}
        bct_pattern = r'^[A-Z]_bct'

        for partInfo in adbObj.partitionInfoList:
            if (re.match(bct_pattern, partInfo.PartitionName)):
                bctEntries[partInfo.PartitionName] = partInfo
        self.vlog(f"Found BCT entries in FileToFlash.txt: {bctEntries}")
        return bctEntries

    def UpdatePVITFullFlashing(self, adbObj, flashPath):
        # This is to update the SHA of BCT into PVIT where applicable
        partMagicSize = 4
        partMagicOffset = 0x1400
        partBinOffset = 0x1404
        NVDAMagicWord = 'NVDA'
        pvitMagicWord = 'PVIT'
        pvitRecordSize = 108
        pvitSHASize = 0x40
        #a limit to handle corrupted PVIT buffer
        #change this to use table size
        maxNumPvitRecords = 120
        bchSize = 0x2000
        partName = "bct"
        partNameStartingIndex = 7
        #fused board for auth, no parition updates in flash_bsp
        # No signing with flash_bsp_images
        # skip this PVIT update for partial update with fused board
        #    with signed images.
        # In Create_bsp_images user provides reference PVIT and
        #    the PVIT get signed.
        if (( "flash_bsp_images" in sys.argv[0]) and (self.targetConfig.b_boardFusedForSigning)):
            self.vlog("Skipping PVIT update for bct in full flashing with fused board")
            return
        #full create and full flashing , update PVIT with SHA of BCT
        #return if partial update either in create or flash
        if (self.targetConfig.s_UpdatePartitions  or self.targetConfig.s_CreatePartialPartitions):
            self.vlog("Skipping PVIT update for bct  with partial update in UpdatePVITFullFlashing")
            return
        #return if asymmetric flashing
        if ("flash_bsp_images" in sys.argv[0]) and (self.targetConfig.parsed_commandline["asymmetric"]):
            self.vlog("Skipping PVIT update for bct  in Asymmetric Flashing")
            return

        if not (self.targetConfig.b_IsBrBctSignedCustomerDataPreserved):
            self.vlog("Skipping PVIT update for bct in full flashing with unsigned customer data")
            return
        #  Get BR BCT entries from FileToFlash.txt

        adbObj.generatePartitionEntryData("FileToFlash.txt", flashPath)
        bctEntries = self.getBrBctEntriesFromFileToFlash(adbObj)
        for bctPartEntry in bctEntries:
            self.vlog(f"bctPartEntry ::  {bctPartEntry}")
        if len(bctEntries.keys()) == 0:
            self.vlog(f"no BR BCT partition found in FileToFlash.txt")
            return
        for bctWithChain in bctEntries:
            chain = bctWithChain.split('_')[0]
            if (self.targetConfig.d_PvitChainNPartitions[chain]["pvitState"] == "PvitDisabled"):
                self.vlog("PVIT is disabled in configuration,  skipping PVIT update for bct of chain " + str(chain))
                continue
            pvitImg_generated = os.path.join(flashPath, self.targetConfig.d_PvitChainNPartitions[chain]["pvitImg"])
            self.vlog("pvitImg_generated :: " + pvitImg_generated)
            with open (pvitImg_generated, mode="rb") as sfd:
                data_buf = sfd.read()
            if (NVDAMagicWord in str(data_buf[:partMagicSize])) and (pvitMagicWord in str(data_buf[partMagicOffset:partMagicOffset+partMagicSize])):
                data_buf_no_bch = data_buf[bchSize:]
            else:
                data_buf_no_bch = data_buf
            brBctSha = self.getBrBctCryptoSha(bctWithChain, flashPath, adbObj)
            self.vlog(f"BR BCT signed customer data updated for partition bct ...updating PVIT with new SHA {brBctSha.hex()}")
            # Copy the sha of BCT into PVIT
            partNotFound = True
            pvitEntry = 1
            while partNotFound:
                buf_str = str(data_buf_no_bch[(pvitEntry-1)*pvitRecordSize:(pvitEntry)*pvitRecordSize])
                if (partNameStartingIndex == buf_str.find(partName)):
                        partNotFound = False
                        break;
                if (pvitEntry>maxNumPvitRecords) or (pvitEntry*pvitRecordSize > len(data_buf)):
                    break
                pvitEntry += 1
            if partNotFound:
                self.vlog(f"pvitEntry ::  {pvitEntry}")
                self.vlog(f"maxNumPvitRecords ::  {maxNumPvitRecords}")
                self.vlog(f"data_buf ::  {str(data_buf)}")
                AbnormalTermination(f"Selected partition  {partName}  not found in PVIT in Full Flashing", nverror.NvError_ImageMismatch)
            else:
                pvitShaOffsetStart = bchSize + (pvitEntry) * pvitRecordSize - pvitSHASize
                pvitShaOffsetEnd = bchSize + (pvitEntry) * pvitRecordSize
                data_buf = data_buf.replace(data_buf[pvitShaOffsetStart:pvitShaOffsetEnd], brBctSha)
                #update usr-ref/target pvit with the updated pvit data
                with open(pvitImg_generated, mode="wb") as fd:
                    if (None == fd.write(data_buf)):
                        self.vlog("Error in writing to  " + pvitImg_generated)
                        AbnormalTermination("Write failed on (%s)" % str(pvitImg_generated), nverror.NvError_FileWriteFailed)
                self.generateSHA512andsignImage(pvitImg_generated, self.targetConfig.s_PVITImgMagicId)
                self.UpdateFileNameinFileToFlash(chain+ "_" + "pvit")
                self.UpdateImageMd5SuminFileToFlash(chain + "_" + "pvit")

    def UpdatePVITPartialFlashing(self, adbObj, flashPath):
        #fused board for auth, no parition updates in flash_bsp
        # No signing with flash_bsp_images
        # skip this PVIT update for partial update with fused board
        #    with signed images.
        # In Create_bsp_images user provides reference PVIT and
        #    the PVIT get signed.
        if (( "flash_bsp_images" in sys.argv[0]) and (self.targetConfig.b_boardFusedForSigning)):
            return

        #1.no partial on create&flash or bootburn -  full create_bsp, full flash_bsp, bootburn-full
        #2.partial on flash or bootburn           -  full create & partial flash, bootburn-partial
        #3.partial on create and full flash       -  partial create & full flash
        #4.partial on create and flash            -  partial create and partial flash

        #1.FULL FLASH, no  partition selection in both image path and flash command line
        #nothing to do here, return.  Valid for both create+flash and bootburn
        if not(self.targetConfig.s_UpdatePartitions  or self.targetConfig.s_CreatePartialPartitions):
            return

        #2&3. partial create or partial flash, but not both
        if (self.targetConfig.s_UpdatePartitions  or self.targetConfig.s_CreatePartialPartitions)  and  \
           ( not (self.targetConfig.s_UpdatePartitions  and  self.targetConfig.s_CreatePartialPartitions)):
            self.initializeChainList()
            for  chain in self.targetConfig.l_selectedChains:
               self.updatePvitDictwithSelectedPartitions(chain)

        #4.partial create  and partial  flash_bsp, not a valid usecase for bootburn
        if self.targetConfig.s_CreatePartialPartitions  and self.targetConfig.s_UpdatePartitions:
            #both create_bsp and flash_bsp are with -u option,
            # validate partitions with flash_bsp are in create_bsp
            for partition in self.targetConfig.s_UpdatePartitions:
                if (partition not in self.targetConfig.s_CreatePartialPartitions):
                    #user passed a partition which is not in the image path provided
                    #abort flashing
                    errorStr = "partition " + str(partition) + " is not available in image path of -P option "
                    AbnormalTermination(errorStr, nverror.NvError_InvalidArgument)
                self.initializeChainList()
                for  chain in self.targetConfig.l_selectedChains:
                    #clear pvitTgtUsrFile, so the targetPVIT file can be used with flash_bsp partial
                    #instead of userPVIT file, if avaiable. So that PVIT can be updated only for flash_bsp
                    # partition list.
                    self.targetConfig.d_PvitChainNPartitions[chain]["pvitTgtUsrFile"] = ""
                    self.updatePvitDictwithSelectedPartitions(chain, True)
        adbObj.generatePartitionEntryData("FileToFlash.txt", flashPath)
        for chain in self.targetConfig.l_selectedChains:
            pvitList = self.targetConfig.d_PvitChainNPartitions[chain]
            if (pvitList["selState"] == "NotSelected")  or (pvitList["pvitState"] == "PvitDisabled"):
                continue
            if (not len(pvitList["partList"])):
                continue

            targetPVITImage = pvitList["pvitPart"] + ".bin"
            if (len( pvitList["pvitTgtUsrFile"]) == 0):
                tgtPvitImgSize = '0x4000'  # PVIT image size
            else:
                tgtPvitImgSize = str(os.path.getsize(pvitList["pvitTgtUsrFile"]))

            if adbObj.getTargetPartitionData(pvitList["pvitPart"], targetPVITImage, tgtPvitImgSize):
                if (len( pvitList["pvitTgtUsrFile"]) == 0):
                    self.vlog(f"No user provided PVIT image, using target PVIT image for chain {chain}")
                    self.targetConfig.d_PvitChainNPartitions[chain]["pvitTgtUsrFile"] = targetPVITImage
                else:
                    self.vlog(f"Compare MD5 checksum of user provided PVIT image with target PVIT image for chain {chain}")
                    #TODO MD5 comparison of user provided PVIT image with target PVIT image
            else:
                self.log(f" Fail in adbObj.getTargetPartitionData :: {str(adbObj.getTargetPartitionData)} \n\n")
            self.updateTargetPvitInPartial(chain, flashPath, adbObj=adbObj)
                    #add a check to compare MD5checksum of target

    def UpdatePVITfileNamenMD5(self, flashImgPath, chain):
        #updates file name with sign ext
        #updates file name in FileToFlash.txt
        #updates md5 of PVIT in FileToFlash.txt
        #updates "pvitImg" key value to filename with sign ext
        if (self.targetConfig.s_UpdatePartitions):
            pvitImg_generated = os.path.join(flashImgPath, self.targetConfig.d_PvitChainNPartitions[chain]["pvitImg"])
            self.RenameFilewithSignext(pvitImg_generated, True)
        self.UpdateFileNameinFileToFlash(chain+ "_" + "pvit")
        self.targetConfig.d_PvitChainNPartitions[chain]["pvitImg"] = self.UpdateFileNameWithSignStatus(self.targetConfig.d_PvitChainNPartitions[chain]["pvitImg"], False)
        self.UpdateImageMd5SuminFileToFlash(chain + "_" + "pvit")

    def UpdatePVITinCreateImages(self, flashImgPath, updatePvitData):
        for chain in self.targetConfig.d_PvitChainNPartitions:
            pvitList = self.targetConfig.d_PvitChainNPartitions[chain]
            if (pvitList["selState"] == "NotSelected")  or (pvitList["pvitState"] == "PvitDisabled"):
                continue
            adbObj = bootburn_adb(self.targetConfig, self.flashUtils)
            adbObj.generatePartitionEntryData("FileToFlash.txt", flashImgPath)
            if (not updatePvitData):
                #self.vlog(f"Full create, skipping PVIT update for chain {chain} in UpdatePVITinCreateImages")
                # Update PVIT in case of Full Flashing
                # This is to update the SHA of BCT into PVIT where applicable
                self.vlog(f"Full create, calling UpdatePVITfileNamenMD5 for chain {chain} in UpdatePVITinCreateImages")
                self.UpdatePVITfileNamenMD5(flashImgPath, chain)
                #self.UpdatePVITFullFlashing(adbObj, flashImgPath)
            else:
                self.UpdatePVITfileNamenMD5(flashImgPath, chain)
                if len(pvitList["pvitTgtUsrFile"]) != 0:
                    self.updateTargetPvitInPartial(chain, flashImgPath, adbObj=adbObj)


    def FlashImages(self, p_TempDumpPath):
        n_PipelinedOps = True
        flashExceptRetCode = nverror.NvError_Success

        if self.ProcessIsChild():
            queue = self.targetConfig.d_ProcessInfo["ctpQueue"]
        else:
            queue = None

        if(self.targetConfig.parsed_commandline["y"]):
            n_PipelinedOps = False

        cwd = os.getcwd()
        n_SkipFileSystemParts = self.targetConfig.parsed_commandline["s"]

        adbObj = bootburn_adb(self.targetConfig, self.flashUtils)
        self.vlog("\n")
        self.vlog("Flashing-Images starting\n")

        flashPath = os.path.join(p_TempDumpPath, "flash-images")
        os.chdir(flashPath)
        self.shellUtils.Copy(self.flashUtils.f_NvDDTool, flashPath)

        timeout = 100

        if (self.targetConfig.parsed_commandline["fpga"]):
            timeout = 900

        adbObj.CheckUSBServiceInit(timeout, self.sudoRequired)
        adbObj.PushWraperShell()

        #Update PVIT in case of Partial Flashing
        self.UpdatePVITPartialFlashing(adbObj, flashPath)

        # Update PVIT in case of Full Flashing
        # This is to update the SHA of BCT into PVIT where applicable
        self.UpdatePVITFullFlashing(adbObj, flashPath)

        n_FastFlash = self.targetConfig.parsed_commandline["O"]
        if (n_FastFlash):
            adbObj.FastFlashCheck("FileToFlash.txt")

        try:
            # "Flash partitions using ADB"
            if (len(self.targetConfig.s_UpdatePartitions) > 0 or n_PipelinedOps is False):
                n_PipelinedOps = False
                flashExceptRetCode = adbObj.FlashUsingADB("FileToFlash.txt", self.targetConfig.s_UpdatePartitions, flashPath, queue)
            else:
                flashExceptRetCode = self.ParallelFlashImages(flashPath, queue)

        except Exception as e:
            flashExceptRetCode = nverror.NvError_Unknown
            self.vlog("**** FlashImages::Exception in flashing ****")
            if(hasattr(e, '__dict__')):
                self.vlog("exception dictionary - " + str(e.__dict__))
            if(hasattr(e, 'output')):
                self.vlog("exception output - " + str(e.output.decode("utf-8")))
                self.extractError(str(e.output.decode("utf-8")))
            if(hasattr(e, 'returncode')):
                self.vlog("exception return code - " + str(e.returncode))
                flashExceptRetCode = e.returncode

        self.vlog("flashExcept return code - " + str(flashExceptRetCode))

        #update FW in case of successful core FW flashing.
        if (flashExceptRetCode is None) or (flashExceptRetCode == nverror.NvError_Success):
            result = nverror.NvError_Success
            try:
                from bootburn_firmware import flashFirmware
                result = flashFirmware(self)
                if(result == nverror.NvError_NoFirmwareFlashNeeded):
                    self.vlog("\033[01;31m FW Update is not needed  \033[0m")
                elif(result != nverror.NvError_Success):
                    print("\033[01;31m Not all firmware was flashed successfully -- " + str(result) + " Failed! \033[0m")
            except:
                self.vlog("Exception in updating FW - " + str(result))
        else:
            self.vlog("Core FW Flashing Failed, skipping Firmware update: " + str(flashExceptRetCode))

            #Brick the target in case of Core FW flashing fail
            #if (flashExceptRetCode is not None) and (flashExceptRetCode != nverror.NvError_Success):
            if self.targetConfig.s_BrickOnCoreFWFlashFail:
                self.vlog("****  Erase mb1-bootloader partition and brick the target  *****")
                adbObj.EraseUsingADB("FileToFlash.txt", self.targetConfig.s_ErasePartitionsToBrick, flashPath)
                AbnormalTermination("Flashing failed and target was bricked", flashExceptRetCode)
            else:
                self.vlog("**** s_BrickOnCoreFlashingFail is disabled or missing in the configuration ****")
                AbnormalTermination("Flashing failed and target was not bricked as per configuration", flashExceptRetCode)

        # self.AdbCleanup
        os.chdir(cwd)

    def BootRCMImage(self):
        l_encrypt = ""

        if (self.targetConfig.n_EncryptedbootFlash is True):
            l_encrypt = "_encrypt"

        if (self.targetConfig.n_SecurebootFlash is True):
            l_SigBRBCT = "A_bct_BR{}_signed.bct".format(l_encrypt)
            l_SigMB1BCT = "bct_MB1{}_signed.bct".format(l_encrypt)
            l_SigMEMBCT1 = "membct_0{}_signed.bct".format(l_encrypt)
            l_SigMEMBCT2 = "membct_1{}_signed.bct".format(l_encrypt)
            l_SigMEMBCT3 = "membct_2{}_signed.bct".format(l_encrypt)
            l_SigMEMBCT4 = "membct_3{}_signed.bct".format(l_encrypt)
        else:
            l_SigBRBCT = "A_bct_BR{}_zerosign.bct".format(l_encrypt)
            l_SigMB1BCT = "bct_MB1{}_zerosign.bct".format(l_encrypt)
            l_SigMEMBCT1 = "membct_0{}_zerosign.bct".format(l_encrypt)
            l_SigMEMBCT2 = "membct_1{}_zerosign.bct".format(l_encrypt)
            l_SigMEMBCT3 = "membct_2{}_zerosign.bct".format(l_encrypt)
            l_SigMEMBCT4 = "membct_3{}_zerosign.bct".format(l_encrypt)

        if (self.targetConfig.s_RamCode >= 0  and self.targetConfig.s_RamCode <= 3):
            l_SigMEMBCT = l_SigMEMBCT1
        elif (self.targetConfig.s_RamCode >= 4 and self.targetConfig.s_RamCode <= 7):
            l_SigMEMBCT = l_SigMEMBCT2
        elif (self.targetConfig.s_RamCode >= 8 and self.targetConfig.s_RamCode <= 11):
            l_SigMEMBCT = l_SigMEMBCT3
        elif (self.targetConfig.s_RamCode >= 12 and  self.targetConfig.s_RamCode <= 15):
            l_SigMEMBCT = l_SigMEMBCT4
        else:
            self.vlog("Wrong RAMCODE. Exiting!")
            AbnormalTermination("s_ERROR_INVALID_ARGUMENT -- Wrong RAMCODE", nverror.NvError_InvalidArgument)

        self.log("Sending bct and prerequisite binaries")  # | tee -a self.targetConfig.f_OutFile

        self.sendRcmCommand("--download bct_bootrom " + l_SigBRBCT)
        self.sendRcmCommand("--download bct_mb1 " + l_SigMB1BCT)
        self.sendRcmCommand("--download bct_mem " + l_SigMEMBCT)

        self.vlog("Sending Blob for RCM to target")  # | tee -a self.targetConfig.f_OutFile

        self.sendRcmCommand("--download blob rcm_blob.bin")

        with self.retGVSsync():
            self.sendRcmCommand(" --boot rcm")

    # Now that the device has been reset,
    # the Device instance has been changed.
    # So, we need to update the target information.
    def UpdateTargetInstanceInfo(self):
        for target in self.targetConfig.s_TargetDeviceInfo:
            s_PortPath = self.targetConfig.s_TargetDeviceInfo[target]["portPath"]
            s_PortPath = s_PortPath.rstrip()
            timeout = 0
            while (timeout <= 30) and (os.path.exists(s_PortPath) == False):
                time.sleep(0.5)
                timeout += 0.5
                # Add logging to see what's happening
                if timeout % 5 == 0:
                    self.log(f"Still waiting for device path {s_PortPath} (timeout: {timeout}s)")
            if (os.path.exists(s_PortPath) == False):
                AbnormalTermination("Could not get path" + s_PortPath, nverror.NvError_FileOperationFailed)
            n_TargetBus = self.shellUtils.catFile(s_PortPath + "/busnum")
            n_TargetBus = str(int(n_TargetBus)).zfill(3)
            n_TargetDevice = self.shellUtils.catFile(s_PortPath + "/devnum")
            n_TargetDevice = str(int(n_TargetDevice)).zfill(3)

            n_TargetPath = "/dev/bus/usb/" + n_TargetBus + "/" + n_TargetDevice
            commmand = "udevadm info -q path -n " + n_TargetPath
            timeout = 0
            devread = False
            retry = True
            while (timeout <= 5) and (retry):
                time.sleep(0.05)
                timeout += 0.05
                devread = False
                l_Temp = self.shellUtils.executeShellCommand(commmand, True)
                if (isinstance(l_Temp, int) is True):
                # Command failed and returned error code
                    continue

                l_Temp = l_Temp.rstrip()
                devread = True
                if (s_PortPath.find(l_Temp) == -1):
                    continue
                retry = False

            if (retry):
                self.getListTargetsInRecovery()
                if (devread == False):
                    AbnormalTermination("Could not get udev bus information", nverror.NvError_FileOperationFailed)
                else:
                    self.vlog("Target instance mismatch.")
                    self.vlog("Previous and new port paths do not match")
                    AbnormalTermination("s_ERROR_TARGET_PORT_MISMATCH", nverror.NvError_InvalidArgument)

            self.log("number of attempts to UpdateTargetInstance :  " + str(int(timeout/0.05)) + "\n")
            self.targetConfig.s_TargetDeviceInfo[target]["bus"] = n_TargetBus
            self.targetConfig.s_TargetDeviceInfo[target]["device"] = n_TargetDevice
            self.targetConfig.s_TargetDeviceInfo[target]["path"] = n_TargetPath
            self.targetConfig.s_TargetDeviceInfo[target]["adbSerial"] = "TEGRA" + n_TargetBus + n_TargetDevice

    def ProvisionUFS(self, operationPath):
        result =  nverror.NvError_Unknown

        l_UFSProvisioningEnable = self.targetConfig.n_UFSProvisioningEnable

        linuxDtbImgFilePath = os.path.join(operationPath, self.targetConfig.s_LinuxDtbImgName)
        if(not os.path.isfile(linuxDtbImgFilePath)):
            AbnormalTermination("s_ERROR_FILE_NOT_EXIST -- " + linuxDtbImgFilePath, nverror.NvError_FileNotFound)

        # "Moving dtb file"
        os.rename(os.path.join(operationPath, self.targetConfig.s_LinuxDtbImgName), os.path.join(operationPath, "provision.dtb"))

        if (l_UFSProvisioningEnable):
            l_UfsDbNode = self.targetConfig.boardDefaultPaths["f_UFSDebugfsNode"]
            self.log("Call nvdtoverlay to attempt to disable UFS hs mode")

            self.flashUtils.f_Nvdtoverlay = os.path.join(self.targetConfig.p_FlashPath, self.flashUtils.f_Nvdtoverlay)
            shellCommand = self.flashUtils.f_Nvdtoverlay + " --hs-mode 0 --dtb " + os.path.join(operationPath, "provision.dtb")
            shellCommand += " --chipver a{} --node ".format(self.targetConfig.n_ChipVersion) + l_UfsDbNode
            self.vlog("ufsProvision shell command :: " + shellCommand)
            if (not os.path.isfile(self.flashUtils.f_Nvdtoverlay)):
                self.vlog("{} is not a valid file path ".format(self.flashUtils.f_Nvdtoverlay))
            if (not os.path.isfile(os.path.join(operationPath, "provision.dtb"))):
                self.vlog("{} is not a valid file path ".format(self.flashUtils.f_Nvdtoverlay))
            result = self.shellUtils.executeShellCommand(shellCommand)
            if (result != nverror.NvError_Success):
                AbnormalTermination("s_ERROR_UFS_PROVISION",  nverror.NvError_TegraUfsProvisionError)

    # PreservedSKUInfoOnPrebuilds
    # Assumed to be called from rcm-boot or rcm-flash directory
    # under prebuilds directory
    def SaveSKUInfoOnPrebuilds(self, l_Operation, p_TempDumpPath):
        if self.targetConfig.is_l4t is True:
            return

        # If no br_bct partition in partial flashing,
        # no need to SaveSKUInfoOnPrebuilds,
        bct_pattern = r'^[A-Z]_bct'
        if (self.targetConfig.s_UpdatePartitions):
            foundBctBr = False
            for imageFile in self.targetConfig.s_UpdatePartitions:
                if (re.match(bct_pattern, imageFile)):
                    foundBctBr = True
                    self.vlog (f"This partial flashing with - U option with bct partition {imageFile} in the argument list provided by the user ")
                    self.vlog ("Continue saving the SKUInfo from target bct_br")
                    break

            if(foundBctBr == False):
                self.vlog("This partial flashing with - U option. No bct partition in the argument list provided by the user ")
                self.vlog("No need to preserve SKuInfo for this partial flashing")
                return

        if "flash_bsp" in self.targetConfig.sysMonitor.identifier:
            updateSignedCustomerData = not(self.targetConfig.b_boardFusedForSigning) and (self.targetConfig.n_Asymmetric is False)\
                and (self.targetConfig.n_SecurebootFlash is False)
        else:
            unSecuredTargetNunSignedImages = ( (not(self.targetConfig.n_SecurebootFlash))  and  (not(self.targetConfig.b_boardFusedForSigning)) )
            securedTargetNsignedImages = self.targetConfig.n_SecurebootFlash and self.targetConfig.b_boardFusedForSigning
            updateSignedCustomerData = (unSecuredTargetNunSignedImages or securedTargetNsignedImages)

        l_SigBrBCT = glob.glob("[A-D]_bct_BR_*.bct")
        l_NumBct = len(l_SigBrBCT)
        chipID = str(self.targetConfig.GetChipID())

        # Preserve SkuInfo on BCT
        if (l_NumBct == 0):
            AbnormalTermination("No bct BR's -- s_ERROR_SYSTEM_COMMAND_FAILED", nverror.NvError_SystemCommand)

        l_SigBrBCT = l_SigBrBCT[0]

        # Preserve target sku info
        customerDataType = 'u'
        if (updateSignedCustomerData):
            self.targetConfig.b_IsBrBctSignedCustomerDataPreserved = True
            customerDataType = 'b'

        self.PreserveTargetSkuInfo(l_SigBrBCT, customerDataType=customerDataType)

        if "flash_bsp" in self.targetConfig.sysMonitor.identifier:
            # Don't Touch Br Bct in this case
            if (self.targetConfig.skipBctPreserve is True):
                return

        self.log("Preserved Skuinfo on " + os.path.join(p_TempDumpPath, l_SigBrBCT))
        if (self.targetConfig.customerData.isLoaded() or self.targetConfig.f_SignedCustomerData != None  or self.targetConfig.signedCustomerData.isLoaded()):
            self.SkuBlobGen(l_SigBrBCT)
        else:
            self.log("neither Customer loaded nor signed customerdata file ")


        if (l_Operation == "rcm-flash"):
            if (self.targetConfig.n_Asymmetric):
                # Create Asymmetric object and preserve sku info
                if (self.targetConfig.headers):
                    # Update bct blob json file with new BCT headers
                    asymmetric_bct = AsymmetricBct(self)
                    asymmetric_bct.update_bct_blob_json(os.path.join("../flash-images/", self.targetConfig.brBctLayoutJsonFile),
                                                        self.targetConfig.headers)

                asymmetric = AsymmetricFlashing(os.path.join("../flash-images/", self.targetConfig.brBctLayoutJsonFile))
                asymmetric.preserve_sku_info(l_SigBrBCT, chipID, "../flash-images", self)
            else:
                # Preserve SkuInfo on BCT to be flashed
                # Open FileToFlash.txt to check for BCT file name

                if (self.targetConfig.parsed_commandline["multi_die"]):
                    fileToFlashPath = os.path.join(p_TempDumpPath, "..", "..","flash-images", "FileToFlash.txt")
                else:
                    fileToFlashPath = os.path.join(p_TempDumpPath, "..", "flash-images", "FileToFlash.txt")

                    bctMatches = None
                    with open(fileToFlashPath, "r", encoding="utf-8") as fileToFlash:
                        fileToFlashContent = fileToFlash.read()
                        bctMatches = re.findall(r".*((die._)*._bct_BR.*\.bct)", fileToFlashContent, re.MULTILINE)

                    if (bctMatches is None):
                        if (len(self.targetConfig.s_UpdatePartitions) != 0) or (len(self.targetConfig.s_CreatePartialPartitions) != 0):
                            self.log("No BR_BCT found in partial flashing, skipping the preserve SKU data in prebuilds")
                            return
                        self.log("No BR BCT entry found in FileToFlash.txt")
                        AbnormalTermination("s_ERROR_TOOL_ND", nverror.NvError_SystemCommand)

                    for brBctTuple in bctMatches:
                        if (self.targetConfig.parsed_commandline["multi_die"]):
                            l_FlashSigBrBCT = os.path.join(p_TempDumpPath, "..", "..", "flash-images", brBctTuple[0])
                        else:
                            l_FlashSigBrBCT = os.path.join(p_TempDumpPath, "..", "flash-images", brBctTuple[0])

                        # If target is fused and images are signed/target is unfused and images are not signed
                        # try to preserve both signed and unsigned section of customer data based on customer data
                        # version, else only presere unsigned customer data
                        # "Preserve metadata from obtained BCT (inbct) to the targeted BCT (outbct)"
                        skuCommand = self.flashUtils.f_NvSkuInfo + " --chip " + chipID + " --inbct " + l_SigBrBCT + " --outbct " + l_FlashSigBrBCT
                        skuCommand += " -t " + customerDataType
                        result = self.shellUtils.executeShellCommand(skuCommand)
                        if (result != nverror.NvError_Success):
                            AbnormalTermination("s_ERROR_TOOL_NVSKUINFO", nverror.NvError_TegraSkuInfoError)

                        if ((self.targetConfig.skuArguments["s_NvSkuArgs"]) or (self.targetConfig.f_SignedCustomerData != None)):
                            self.SkuBlobGen(l_FlashSigBrBCT)
                            self.targetConfig.b_IsBrBctSignedCustomerDataPreserved = True

                        self.log("Preserved Skuinfo on p_TempDumpPath/l_FlashSigBrBCT")

    # Function to Provision an UFS Device
    def UFSDeviceProvision(self, p_TempDumpPath):
        adbObj = bootburn_adb(self.targetConfig, self.flashUtils)
        cwd = os.getcwd()
        os.chdir(p_TempDumpPath)

        self.shellUtils.Copy(self.flashUtils.f_NvDDTool, p_TempDumpPath)
        adbObj.CheckUSBServiceInit(100, self.sudoRequired)
        adbObj.PushWraperShell()
        # "Provision UFS Device"
        adbObj.ProvisionUFSDevice(p_TempDumpPath)
        os.chdir(cwd)

    def DoesTargetInstanceExist(self, bus, device):
        l_Found = False
        l_Instance = "Bus " + bus + " Device " + device + ": ID " + self.targetConfig.s_Nvidia_USB_ID
        foundDeviceInfo = None

        lsusbData = self.shellUtils.lsusb()
        for devicePath in lsusbData:
            # device info set is organized as follows
            # {path:{ dev, bus, id, tag, path}}
            usbInfo = lsusbData[devicePath]
            usb_bus = usbInfo['bus']
            usb_dev = usbInfo['device']
            if(bus.find(usb_bus) != -1 and device.find(usb_dev) != -1):
                foundDeviceInfo = usbInfo
                l_Found = True
                break

        if(l_Found is False):
            self.vlog("Tegra target \" " + l_Instance + "\" not found")
            self.vlog("The target has been removed from recovery")
            AbnormalTermination("${s_ERROR_DEVICE_NOT_FOUND} -- " + l_Instance, nverror.NvError_DeviceNotFound)

        return foundDeviceInfo

    # Disable the USB autosuspend property of the
    # host before flashing starts and re-enable it
    # while leaving.
    def SetUsbAutoSuspend(self, l_State):
        # First Check if File exists
        if (os.path.isfile("/sys/module/usbcore/parameters/autosuspend") == False):
            return

        # Do we need root access?
        sudo_required = False
        if (os.access("/sys/module/usbcore/parameters/autosuspend", os.W_OK) == False):
            sudo_required = True

        if (l_State.find("disable") != -1):
            # n_UsbAutosuspend needs to be exported as it is used to restore
            # the state of autosuspend which is done inside a spawnned sub-shell
            self.targetConfig.n_UsbAutosuspend = self.shellUtils.catFile("/sys/module/usbcore/parameters/autosuspend")
            self.targetConfig.n_UsbAutosuspend = self.targetConfig.n_UsbAutosuspend.rstrip()
            self.targetConfig.sysMonitor.log("Disable autosuspend on current host")
            if (sudo_required == True):
                shellCommand = ["sudo", "sh", "-c", "echo -1 > /sys/module/usbcore/parameters/autosuspend "]
            else:
                shellCommand = ["sh", "-c", "echo -1 > /sys/module/usbcore/parameters/autosuspend "]
            subprocess.call(shellCommand)

        elif(l_State.find("enable") != -1):
            if (self.targetConfig.n_UsbAutosuspend):
                self.targetConfig.sysMonitor.log("Restore autosuspend on current host")
                if (sudo_required == True):
                    shellCommand = ["sudo", "sh", "-c", "echo " + self.targetConfig.n_UsbAutosuspend + " > /sys/module/usbcore/parameters/autosuspend"]
                else:
                    shellCommand = ["sh", "-c", "echo " + self.targetConfig.n_UsbAutosuspend + " > /sys/module/usbcore/parameters/autosuspend"]

                subprocess.call(shellCommand)
                self.targetConfig.n_UsbAutosuspend = None

    def findBaseBoardName(self, targetConfig):
        baseBoardDetectStatus = False
        f_baseBoardSkuMapping = targetConfig.f_baseBoardSkuMapping

        self.log("finding board : " + targetConfig.s_InforomObjType + ", SYSObjversion :: " + targetConfig.s_InforomSYSObjVersion  + ",  baseBoardSkuMap :: " + f_baseBoardSkuMapping)

        if (targetConfig.s_InforomObjType == targetConfig.s_InforomT23xObjName) and (targetConfig.s_InforomSYSObjVersion >= targetConfig.n_InforomT23xSYSObjMinVer):
            self.aurix.findTargetBaseBoardName(targetConfig, f_baseBoardSkuMapping)
            targetConfig.s_BoardName = targetConfig.s_baseBoardName
            if len(targetConfig.s_BoardName) == 0:
                self.vlog("Base Board detection failed :: ", nverror.NvError_NotSupported)
            else:
                self.vlog("Detected baseboard with default ChipSku  :: " + targetConfig.s_BoardName)
                baseBoardDetectStatus = True
        else:
            errstr = "\n\033[01;33m Inforom obj should be type " + targetConfig.s_InforomT23xObjName + " Inforom SYS obj should be greather or equal to " + str(targetConfig.n_InforomT23xSYSObjMinVer)  +  " for auto detection\033[0m\n"
            self.vlog(errstr)
        return baseBoardDetectStatus

    # Aurix port signifies DPX target and parallel Tegra flashing
    # will be active in that case. In other cases take regular
    # action depending upon -I argument.
    def CheckRecoveryTargets(self):
        if(self.targetConfig.s_McuPort):
            with self.retGVSsync():
                self.aurix.GetTegrasAssocWithAurix(self.targetConfig.s_McuPort)
        elif(self.targetConfig.parsed_commandline["I"] != None or self.targetConfig.parsed_commandline["usb_instance"] != None):
            #should already be set in parser, return
            return
        else:
            names = ["die0"]
            if(self.targetConfig.parsed_commandline["multi_die"]):
                names.append("die1")

            with self.retGVSsync():
                lsusbList = self.getListTargetsInRecovery()

            if(len(lsusbList) == 0):
                self.vlog("No recovery-target found; Make sure the target device is connected to the")
                self.vlog("host and is in recovery mode. Exiting")
                AbnormalTermination("ERROR_TARGET_RECOVERY", nverror.NvError_ResourceError)

            elif(len(lsusbList) > 1 and not self.targetConfig.parsed_commandline["multi_die"]):
                self.vlog("Multiple tegra instances found. Use -I <Bus_No> <Device_No>")
                self.vlog("from lsusb to flash a particular target.")
                AbnormalTermination("s_ERROR_TARGET_INSTANCE", nverror.NvError_ResourceError)

            for i in range(len(names)):
                self.AddInstanceToFlashList(lsusbList[i], names[i], self.targetConfig.s_BoardName)

    def getValue(self, keyValuePair):
        if(not keyValuePair):
            errorStr = "Empty string given as parameter"
            AbnormalTermination(errorStr, nverror.NvError_BadParameter)

        position = keyValuePair.find('=')
        if(position < 0):
            # is this a fatal error?
            return ""

        position = position + 1

        return keyValuePair[position:]

    def getListTargetsInRecovery(self, excludeTegraDevice=False):
        NVIDIA_DEBUG_DONGLE = "7045"
        NVIDIA_DEBUG_DONGLE_RUNNING = "7046"
        lsusbInitList = self.shellUtils.executeShellCommand("lsusb -d " + self.targetConfig.s_Nvidia_USB_ID + ":", True)

        if(isinstance(lsusbInitList, int)):
            # if no devices command fails with return value 1
            # executeShellCommand will return nverror.NvError_SystemCommandFailed
            # no nvidia devices found
            lsusbInitList = []
        else:
            # On Success a string output will be returned
            # split it up per device
            lsusbInitList = lsusbInitList.splitlines()

        deviceList = lsusbInitList[:]
        for device in lsusbInitList[::-1]:
            self.vlog(device)
            try:
                if(device.find(self.targetConfig.s_Nvidia_USB_ID) == -1):
                    deviceList.remove(device)
                elif(device.find(self.targetConfig.s_Tegra_USB_Product_ID) != -1) and (excludeTegraDevice == True) :
                    self.vlog(" Removing this Tegra Device with device ID : " + self.targetConfig.s_Tegra_USB_Product_ID + " - Device : " +  device)
                    deviceList.remove(device)
                elif ((device.find(NVIDIA_DEBUG_DONGLE) != -1) or (device.find(NVIDIA_DEBUG_DONGLE_RUNNING) != -1)):
                    deviceList.remove(device)
            except:
                continue

        return deviceList

    def formatTegraRCM(self):
        tegrarcm = self.flashUtils.f_TegraRcm
        usbInfo = {}
        if(len(self.targetConfig.s_TargetDeviceInfo) == 0):
            self.vlog("No valid target device found")
            AbnormalTermination("No valid device found", nverror.NvError_DeviceNotFound)
        elif(len(self.targetConfig.s_TargetDeviceInfo) == 1):
            for tegra in self.targetConfig.s_TargetDeviceInfo:
                usbInfo = self.targetConfig.s_TargetDeviceInfo[tegra]
        else:
            usbInfo = self.targetConfig.s_TargetDeviceInfo[self.targetConfig.dieId]

        portPath = usbInfo["path"]

        tegrarcm += " --instance " + portPath
        if(os.access(portPath, os.R_OK | os.W_OK) == False):
            self.sudoRequired = True
            # Only give the warning once
            if("AccessWarningGiven" not in usbInfo):
                AccessWarningGiven = False
            else:
                AccessWarningGiven = usbInfo["AccessWarningGiven"]

            if(AccessWarningGiven == False):
                user = self.GetUser()
                if(self.targetConfig.dieId in self.targetConfig.s_TargetDeviceInfo):
                    self.targetConfig.s_TargetDeviceInfo[self.targetConfig.dieId]["AccessWarningGiven"] = True

                self.vlog("\033[01;31m user = " + str(user) + " does not have read/write permissions to " + str(portPath) + " \033[0m")
                self.vlog("\033[01;31m adding sudo access to tegra RCM calls \033[0m")
                self.vlog("\033[01;31m To avoid using SUDO please add the following to your udev rules \033[0m")
                self.vlog("")
                self.vlog("\033[01;31m SUBSYSTEMS==\"usb\", ACTION==\"add\", ATTRS{idVendor}==\"0955\", MODE:=\"0666\" \033[0m")
            tegrarcm = "sudo -E " + tegrarcm

        return tegrarcm

    # Add the Nvidia device with the specified bus and
    # device values to the array of instances to be flashed.
    def AddInstanceToFlashList(self, lsusbData, name, boardName):

        lsusbData = lsusbData.replace(':', '')
        lsusbData = lsusbData.split()
        s_TargetDevPath = "/dev/bus/usb/" + lsusbData[1] + "/" + lsusbData[3]
        attempts = 0
        max_attempts = 30
        self.log("Checking dev path  " + s_TargetDevPath )
        while (os.path.exists(s_TargetDevPath) is False) and (attempts <= max_attempts):
            #self.log("waiting to get " + s_TargetDevPath +  " in attempt " + str(attempts))
            time.sleep(1)
            attempts += 1
        self.log("Waited "  + str(attempts) + " secs to get NVIDIA USB device in dev path " + s_TargetDevPath)

        if(os.path.exists(s_TargetDevPath) is False):
            self.vlog("Tegra target " + s_TargetDevPath + " not found")
            self.vlog("The target has been removed from recovery")
            AbnormalTermination("s_ERROR_DEVICE_NOT_FOUND -- " + s_TargetDevPath, nverror.NvError_DeviceNotFound)

        s_AdbSerialNum = "TEGRA" + lsusbData[1] + lsusbData[3]

        udevCommand = "udevadm info -q path -n " + s_TargetDevPath

        s_PortPath = self.shellUtils.executeShellCommand(udevCommand, True)

        if(isinstance(s_PortPath, int) is True):
            AbnormalTermination("Could not get port path -- " + str(s_PortPath), nverror.NvError_FileOperationFailed)

        s_PortPath = "/sys" + s_PortPath.rstrip()

        if(os.access(s_TargetDevPath, os.R_OK | os.W_OK) == False):
            self.sudoRequired = True

        lsusbData = {"bus":lsusbData[1],
                     "device":lsusbData[3],
                     "path":s_TargetDevPath,
                     "adbSerial":s_AdbSerialNum,
                     "name":name,
                     "portPath":s_PortPath,
                     "AccessWarningGiven":False,
                     "boardName":boardName
                    }
        self.targetConfig.s_TargetDeviceInfo[name] = lsusbData

    def DumpGroupInfo(self, matrix, s_PartNameToCheck=None):
        s_Op = ""
        s_Single = ""
        partToCheck = None

        for row in matrix:
            group = matrix[row]
            group = group.strip()

            if(s_PartNameToCheck and s_PartNameToCheck in group):
                partToCheck = group

            if(len(group.split()) > 1):
                s_Op += "Group: " + group.strip() + "\n"
            elif(len(group) == 0):
                continue
            else:
                s_Single += "Single: " + group + "\n"

        # print all paritions belonging to same group if 2 args provided
        f_GroupPartitionInfo = ""
        if(s_PartNameToCheck):
            if (partToCheck):
                for partition in partToCheck:
                    f_GroupPartitionInfo += " " + partition
            else:
                AbnormalTermination("s_ERROR_DEVICE_NOT_FOUND", nverror.NvError_DeviceNotFound)
        else:
            f_GroupPartitionInfo += s_Op + "\n" + s_Single

        return f_GroupPartitionInfo

    def checkDialoutAccess(self, aurixPort):
        try:
            if(os.access(aurixPort, os.R_OK | os.W_OK)):
                return True
            else:
                user = self.GetUser()
                self.vlog("\033[01;31m " + str(user) + " does not have RW Access to " + str(aurixPort) + " \033[0m")

                dialoutGroup = grp.getgrnam("dialout")
                if((user in dialoutGroup.gr_mem) == False):
                    self.vlog("\033[01;31m " + str(user) + " not in dialout group \033[0m")

                return False
        except:
            return False

    def BootRCM_Orin_FPGA(self, l_Operation, binaryLocation):
        from bootburn_thor import bootburn_thor
        bootburnThor = bootburn_thor(self.targetConfig)
        self.targetConfig.n_SkipSkuValidate = False
        cwd = os.getcwd()
        if (self.targetConfig.parsed_commandline["multi_die"]):
            operationPath = os.path.join(binaryLocation, l_Operation, self.targetConfig.dieId)
        else:
            operationPath = os.path.join(binaryLocation, l_Operation)
        os.chdir(operationPath)
        self.vlog("operationPath :: " + str(operationPath))

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
        # pick up resigned bct if it exists
        if (self.targetConfig.customerData.isLoaded() or self.targetConfig.f_SignedCustomerData != None):
            brbct_list = glob.glob(brbct_name + "*resigned*.*")
            if len(brbct_list) > 0:
                brbct = brbct_list[0]
        psc_bl = self.shellUtils.find('.', psc_bl_name)[0]
        mb1 = self.shellUtils.find('.', mb1_name)[0]
        mb1bct = self.shellUtils.find('.', mb1bct_name)[0]

        blob = "rcm_blob.bin"

        self.UpdateTargetInstanceInfo()
        membct = bootburnThor.updateImageName(bootburnThor.getmembctName()) + ".bct"

        self.sendRcmCommand("--new_session --chip 0x26")

        #FIXME:  Do we need to store this for the FPGA or just read it???
        self.targetConfig.s_BR_CID = self.sendRcmCommand("--uid --chip 0x26")
        sendRcmOutput = self.sendRcmCommand("--download bct_br " + operationPath + "/" +brbct)
        self.vlog("tegraRcm output :: " + str(sendRcmOutput))
        sendRcmOutput = self.sendRcmCommand("--download mb1 " + operationPath + "/" + mb1)
        self.vlog("tegraRcm output :: " + str(sendRcmOutput))
        sendRcmOutput = self.sendRcmCommand("--download psc_bl1 " + operationPath + "/" +  psc_bl)
        self.vlog("tegraRcm output :: " + str(sendRcmOutput))
        sendRcmOutput = self.sendRcmCommand("--download bct_mb1 " + operationPath + "/" + mb1bct)
        self.vlog("tegraRcm output :: " + str(sendRcmOutput))

        self.vlog("tegraRcm output :: " + str(sendRcmOutput))
        sendRcmOutput = self.sendRcmCommand(" --chip 0x26 --pollbl")

        self.vlog("tegraRcm output :: " + str(sendRcmOutput))
        sendRcmOutput = self.sendRcmCommand("--download bct_mem " + operationPath + "/" +  membct)
        self.vlog("tegraRcm output :: " + str(sendRcmOutput))

        if (mb1bct_multi_sku_bct_name is not None):
            sendRcmOutput = self.sendRcmCommand("--download multi_sku_platfo_data " + multisku_mb1_bct_file)
            self.vlog("tegraRcm output :: " + str(sendRcmOutput))

        self.vlog("waiting for 1 sec")
        time.sleep(1)
        sendRcmOutput = self.sendRcmCommand("--download blob " +  operationPath + "/" + blob)
        self.vlog("tegraRcm output :: " + str(sendRcmOutput))

        os.chdir(cwd)

