#!/usr/bin/python
# SPDX-FileCopyrightText: Copyright (c) 2017-2025, NVIDIA CORPORATION.  All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary

#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.


import os
import shutil
import platform
import subprocess
import time
import re
import glob
import stat
import inspect
import importlib
import traceback
import abc
import re
import json
from multiprocessing import Process
import sys
from bootburn_aurix import Aurix
from bootburn_lib import bootburn_lib
from target_config import target_config
from flash_utilities import flash_utilities
from flash_utilities import shell_utilities
from flashtools_nverror import nverror
from flashtools_nverror import AbnormalTermination
from bootburn_adb import bootburn_adb

#____________________________________________________________________
#                        BASE CLASS
#____________________________________________________________________
class bootburn_firmware():
    #Base class of FW flashing
    bootburnLib=bootburn_lib
    targetConfig=target_config
    flashUtils = flash_utilities
    adbObj = bootburn_adb
    debugging = False

    #Common variables

    firmwareMapDict = None  # this will be populated by target_config

    def __init__(self, bootburnLibrary, initConfigDataOnly=False):
        self.bootburnLib = bootburnLibrary
        self.targetConfig=self.bootburnLib.targetConfig
        self.flashUtils = self.targetConfig.flashUtils
        self.shellUtils = self.targetConfig.shellUtils
        self.adbObj = bootburn_adb(self.targetConfig, self.flashUtils)
        if(initConfigDataOnly == False):
            self.adbObj.PushWraperShell()

    def log(self, msg):
        self.bootburnLib.log(msg)

    def vlog(self, msg):
        self.bootburnLib.vlog(msg)

    def getPciBusDomainFunction(self, deviceToSearchFor):
        busDomainFunction = None

        lspci = self.adbObj.AdbShell(" lspci", True)
        if(isinstance(lspci, int)):
            self.vlog("Could not get PCI listing from target -- error = " + str(lspci))
            AbnormalTermination("Invalid PCI listing", nverror.NvError_AdbShell)

        for line in lspci.splitlines():
            if(not line or not line.strip()):
                continue
            elif(line.find(deviceToSearchFor) != -1):
                line = line.split()
                busDomainFunction = line[0]
                break
        return busDomainFunction

    def pushDirectory(self, src, dest):
        print("pushing " + src + " to " + dest)
        adbCommand = self.targetConfig.flashUtils.f_AdbTool + " -s " + self.targetConfig.s_AdbSerialNum + " push " + src + "  " + dest
        print(adbCommand)
        result = nverror.NvError_Success

        try:
            result = subprocess.check_output(adbCommand, shell=True)
        except Exception as e:
            print("Exception pushing directory -- " + str(e))
            return nverror.NvError_AdbPush

        return nverror.NvError_Success

    def loadDriver(self, driverName):
        self.vlog("Loading " + driverName + " driver")
        result = self.adbObj.AdbShell("insmod /lib/modules/" + driverName)
        if(isinstance(result, int) == True and result != nverror.NvError_Success):
            self.vlog("Failed to load " + driverName + " driver -- error code = " + str(result))
            return nverror.NvError_AdbShell

        return nverror.NvError_Success

    def parseAdbExitCode(self, adbOutput):
        index = adbOutput.find('EXITCODE=')
        if index != -1 :
            adbOutput = adbOutput[index + len('EXITCODE='):]
            EXITCODE = int(adbOutput.rstrip())
        else:
            self.vlog(adbOutput)
            EXITCODE = 0

        self.log("EXITCODE = " + str(EXITCODE))
        return EXITCODE

    def brickTheTarget(self, pTempDir):
        if (self.targetConfig.s_BrickOnFirmwareUpdateFail == True):
            self.vlog("Atomic Flashing is enabled on Firmware Flashing Fail. Erasing boot component to brick the target")
            flashdir = os.path.join(pTempDir, "flash-images")
            self.vlog("self.targetConfig.s_ErasePartitionsToBrick :: " + str(self.targetConfig.s_ErasePartitionsToBrick))
            self.adbObj.EraseUsingADB("FileToFlash.txt", self.targetConfig.s_ErasePartitionsToBrick,  flashdir)
            AbnormalTermination("Firmware Update failed and target was bricked", nverror.NvError_Unknown)
        else:
            self.vlog("****  s_BrickOnFirmwareUpdateFail is not set to True, skipping Atomic Flashing(Bricking) on Firmware Update fail *****")

    @abc.abstractmethod
    def flashFirmware(self):
        #Abstract function.
        print("Flashing Firmware")
#____________________________________________________________________
#             MCU Firmware update
#____________________________________________________________________
class MCU_Firmware(bootburn_firmware):

    baudrate = "  921600  "
    mcuFwUpdateCmd = ""
    majorVersion = ""
    minorVersion = ""
    revisionNumber = ""
    snapshotNumber = ""

    tgtMajorVersion = ""
    tgtMinorVersion = ""
    tgtRevisionNumber = ""
    tgtSnapshotNumbe = ""

    pFirmwarePath = ""
    mcufwKey ="MCU_FW"
    mcufwUpdtScriptKey = "FWupdateScript"
    mcufwBinKey = "MCUFW"
    tegraUsbPortKey = "tegraUsbPortName"
    forceUpdtkey = "forceUpdate"
    f_mcufwUpdateScript = ""
    f_mcuFwBin = ""
    s_tegraUsbPort = "   /dev/ttyACM0  "
    forceUpdate = "NO"

    def loadMcuFwValuesfromJson(self):
        #import pdb; pdb.set_trace()
        pFirmwareFile = self.targetConfig.p_boardFirmware
        if (os.path.isfile(pFirmwareFile) == True):
             with open(pFirmwareFile, 'r') as firmwareMap:
                self.firmwareMapDict = json.load(firmwareMap)
        else:
             AbnormalTermination("s_ERROR_FILE_NOT_EXIST -- " + pFirmwareFile, nverror.NvError_FileNotFound)
        print(f"mapdictionary :: {self.firmwareMapDict}   ")
        if self.mcufwKey in self.firmwareMapDict:
            self.mcufwDictData = self.firmwareMapDict[self.mcufwKey]
            if self.mcufwUpdtScriptKey in self.mcufwDictData:
                self.f_mcufwUpdateScript = self.mcufwDictData[self.mcufwUpdtScriptKey]
            else:
                self.vlog("f_mcufwUpdateScript not available " + str(self.mcufwUpdtScriptKey))
                return nverror.NvError_FirmwareConfigReadFail

            if self.mcufwBinKey in self.mcufwDictData:
                self.f_mcuFwBin = self.mcufwDictData[self.mcufwBinKey]
            else:
                self.vlog("f_mcuFwBin not available " + str(self.mcufwBinKey))
                return nverror.NvError_FirmwareConfigReadFail

            if self.forceUpdtkey in self.mcufwDictData:
                self.forceUpdate = self.mcufwDictData[self.forceUpdtkey]
            else:
                self.vlog("forceUpdate not available " + str(self.forceUpdtkey))
                return nverror.NvError_FirmwareConfigReadFail

            if self.tegraUsbPortKey in self.mcufwDictData:
                self.s_tegraUsbPort = self.mcufwDictData[self.tegraUsbPortKey]
            else:
                self.vlog("tegraUsbPortKey not available " + str(self.tegraUsbPortKey))
                return nverror.NvError_FirmwareConfigReadFail
            return nverror.NvError_Success
        else:
            self.vlog("\n MCU-FW key is not available in firmware.json")
            return nverror.NvError_FirmwareConfigReadFail

    def ReadMcuFwParams(self):
        if (self.loadMcuFwValuesfromJson() != nverror.NvError_Success):
            return nverror.NvError_FirmwareFlashFail
        if(("mcu-fw" in self.targetConfig.parsed_commandline) == True) and (self.targetConfig.parsed_commandline["mcu-fw"] != None):
            self.f_mcuFwBin = self.targetConfig.parsed_commandline["mcu-fw"]
        self.targetConfig.f_McuFwBinAbs = os.path.join(self.targetConfig.p_McuFwBin, self.f_mcuFwBin)
        self.targetConfig.f_McuUpdtScriptAbs = os.path.join(self.targetConfig.p_McuUpdtScript, self.f_mcufwUpdateScript)
        return nverror.NvError_Success

    def generateMcuFwUpdateCmd(self):
        self.mcuFwUpdateCmd = "python3  " + self.targetConfig.f_McuUpdtScriptAbs + "    " +  self.baudrate +  "    " + self.targetConfig.s_McuPort + "    " + self.targetConfig.s_tegraUsbPort + "  " + str(self.targetConfig.f_McuFwBinAbs)

    def getLatestVersion(self):
        latestVersion = None
        try:
            McuFWbinaryVersionCmd = self.mcuFwUpdateCmd + str("  --get_fw_version_hex_file  ")
            self.log("*****    " + str(McuFWbinaryVersionCmd) + "      ********")
            output = self.shellUtils.executeShellCommand(McuFWbinaryVersionCmd, True, True)
            outputLines = re.split('\n', output)
            for line in outputLines:
                if "Major version" in line:
                    self.majorVersion = line.split()[2]
                elif "Minor version" in line:
                    self.minorVersion = line.split()[2]
                elif "Revision Number" in line:
                    self.revisionNumber = line.split()[2]
                elif "Snapshot Number" in line:
                    self.snapshotNumber = line.split()[2]
            self.log ("*****    Major version:" + self.majorVersion)
            self.log ( "minorVersion:" + self.minorVersion)
            self.log ( "revisionNumber:"+ self.revisionNumber)
            self.log ( "snapshotNumber:"+ self.snapshotNumber)
            return outputLines
        except Exception as e:
            self.vlog("Exception thrown reading  MCU FW  version\n" + str(e))
            return None

    def getTgtMcufwVersion(self):
        #Get MCU FIRMWARE version from the target
        try:
            McuTgtFWVersionCmd =  self.mcuFwUpdateCmd  + str("  --get_fw_version ")
            self.log("*****    " + str(McuTgtFWVersionCmd) + "      ********")
            output = self.shellUtils.executeShellCommand(McuTgtFWVersionCmd, True, True)
            outputLines = re.split('\n', output)
            self.log("*****    " + "Target MCU FW Version" + "      ********")
            for line in outputLines:
                if "Major version" in line:
                    self.tgtMajorVersion = line.split()[2]
                elif "Minor version" in line:
                    self.tgtMinorVersion = line.split()[2]
                elif "Revision Number" in line:
                    self.tgtRevisionNumber = line.split()[2]
                elif "Snapshot Number" in line:
                    self.tgtSnapshotNumber = line.split()[2]
            print (self.tgtMajorVersion, self.tgtMinorVersion, self.tgtRevisionNumber, self.tgtSnapshotNumber )
            return outputLines
        except Exception as e:
            self.vlog("Exception thrown reading  MCU FW  version\n" + str(e))
            return None
    def isFWtobeUpdated(self):
        #compare FW binary version with  FW version on the target
        #update only if the FW version on target is of older version
        #update always irrespective of the target FW version
        if (self.majorVersion == self.tgtMajorVersion) and (self.minorVersion == self.tgtMinorVersion) and (self.revisionNumber == self.tgtRevisionNumber):
            self.vlog("File Firmware version is same as Target Firmware version ")
            return False
        if (self.majorVersion == self.tgtMajorVersion):
            if (self.minorVersion > self.tgtMinorVersion):
                return True
            elif (self.minorVersion == self.tgtMinorVersion) and (self.revisionNumber > self.tgtRevisionNumber):
                return True
            else:
                return False
        else:
            #major version represents SW release, autoupdate in case of different releases.
            # it is irrelevant whether release is older version than the release version on the target.
            return True
        self.vlog("Target Firmware version is later version than FW binary version ")
        return False

    def updateMCUfw(self):
        #update MCU Fw
        output = ""
        try:
            self.vlog("In UpdateMCUfw")
            output = self.shellUtils.executeShellCommand(self.mcuFwUpdateCmd, True, True)
            splitoutput = output.splitlines()
            for line in splitoutput:
                if "FW update successful" in line:
                    self.vlog("updateMCUfw  completed successfully")
                    return True
            self.log (f'*** output ****  :: \n {output}')
            return False
        except Exception as e:
            self.vlog ("  *** Exception in  MCU Firmware Update  *** ")
            self.vlog (e)
            self.vlog (re.split('\n', output))
            return False

    def flashMCU(self):
        if (nverror.NvError_Success != self.ReadMcuFwParams()):
            return nverror.NvError_FirmwareVersionCheckFail
        self.generateMcuFwUpdateCmd()
        self.log(self.mcuFwUpdateCmd)
        latestVersion = self.getLatestVersion()
        if(latestVersion == None):
            self.vlog("Could not get the latest FW version")
            return nverror.NvError_FirmwareVersionCheckFail
        self.log ("\n    *****   Fw version in the FW binary file :: " + self.majorVersion + "." + self.minorVersion + "." + self.revisionNumber + "." + "." + self.snapshotNumber + " ****\n")
        tgtVersion = self.getTgtMcufwVersion()
        if(tgtVersion == None):
            self.vlog("Could not get the target FW version at path")
            return nverror.NvError_FirmwareVersionCheckFail
        self.vlog ("\n    *****  Fw version in the target MCU FW :: " + self.tgtMajorVersion + "." + self.tgtMinorVersion + "." + self.tgtRevisionNumber + "." + self.tgtSnapshotNumber + "****\n") 
        if not self.isFWtobeUpdated():
            self.vlog("\n    ***** No mcufw upate is required, target mcu fw version matches****   ")
            if ("Yes" == self.forceUpdate):
                self.vlog("\n    ***** force update is set in configuration, flashing MCU FW ****   ")
            else:
                return nverror.NvError_NoFirmwareFlashNeeded
        else:
            self.log("\n    ***** version check completed successfully ****   ")
        self.vlog("\n   starting MCU FW flashing\n")
        self.log(self.mcuFwUpdateCmd)
        if not self.updateMCUfw():
            self.vlog("MCU Firmware update failed")
            return nverror.NvError_FirmwareFlashFail

        return nverror.NvError_Success

    def flashFirmware(self):
        print("Flashing MCU Firmware")
        try:
            return self.flashMCU()
        except Exception as e:
            print("Exception thrown while flashing MCU Firmware\n" + str(e) + "\n")
            return nverror.NvError_Unknown
#____________________________________________________________________
#                         Emmc Firmware
#____________________________________________________________________
class EmmcUfsFirmware(bootburn_firmware):
    roms = None

    def get_field(self, info, name, size=None):
        lines = info.split("\n")
        field = None
        for line in lines:
            if(line.find(name) != -1):
                array = line.split(" ")
                if (size is None):
                    doNext = False
                    for i in array:
                        i = i.strip()
                        if (doNext == True):
                            field = i
                            break
                        if (i == name):
                            doNext = True
                else:
                    for i in array:
                        i = i.strip()
                        loc=i.find(name)
                        if (loc != -1):
                            field=i[loc+len(name):loc+len(name)+size]
                            break
        return field

    def getVersion(self, device):
        self.vlog("Getting Device version")
        command = "mnand_hs -d " + device + " -fwver";
        adbOutput = self.adbObj.AdbShell(command, True)
        field = self.get_field(adbOutput, "UFS")
        if (field is not None):
            partNum = self.get_field(adbOutput, "PID:")
        else:
            partNum = self.get_field(adbOutput, "PRN=", 6)

        version = self.get_field(adbOutput, "FW_VERSION:")
        return version, partNum

    def UpdateFirmware(self, device):

        version, partNum = self.getVersion(device)
        self.vlog("%s %s" %(version, partNum))
        version = "0302"
        if(not version or not partNum):
            if (version is None) and (partNum is None):
                self.vlog("No Device Information\n")
            if (version is None):
                self.vlog("No Version Info.  Part is %s\n" %(partNum))
            else:
                self.vlog("No Part Info.  Version is %s\n" %(version))

            AbnormalTermination("Device Query Failed", nverror.NvError_BadParameter)

        pFirmwareFile = self.targetConfig.p_boardFirmware
        if (os.path.isfile(pFirmwareFile) == True):
            with open(pFirmwareFile, 'r') as fFirmwareMap:
                firmwareMapDict = json.load(fFirmwareMap)
        else:
            self.vlog("PartNum [%s] is not in firmware.json [%s]\n" %(partNum, pFirmwareFile))
            return nverror.NvError_NoFirmwareAvailable

        if partNum in firmwareMapDict:
            pUpdateFile = firmwareMapDict[partNum]
        else:
            AbnormalTermination("Can't find partNum -- " + partNum, nverror.NvError_BadParameter)

        if (self.targetConfig.isPDK):
            pFirmwarePath = os.path.join(self.targetConfig.p_FlashPath, "..", "firmware")
        else:
            pFirmwarePath = os.path.join(self.targetConfig.TEGRA_TOP, "flash-tools/legacy", "firmware")

        self.vlog("Using firmware " + pUpdateFile)
        self.adbObj.AdbPush(os.path.join(pFirmwarePath, "..", pUpdateFile), "/tmp/" + os.path.basename(pUpdateFile))

        command = "mnand_hs -d " + device + " -ffu" + " /tmp/" + os.path.basename(pUpdateFile)
        print("%s\n" %(command))
        result = self.adbObj.AdbShell(command, True)

        if (isinstance(result, int) == True):
            self.vlog ("mnand error -- report is as follows\n" + str(result) + "\n")
            AbnormalTermination("Flashing Emmc/Ufs FAILED", nverror.NvError_Adb)
        else:
            self.vlog("\nSuccessfully flashed the Emmc/Ufs")
            return nverror.NvError_Success

    def flashFirmware(self, device=""):
        try:
            self.vlog("Flashing device")
            return self.UpdateFirmware(device)
        except Exception as e:
            self.vlog("Exception thrown while flashing Ufs/Emmc\n" + str(e))
            return nverror.NvError_Unknown

def flashFirmware(bootburnLib):
    flashSuccess = 0
    parsed_commandline = bootburnLib.targetConfig.parsed_commandline
    result = nverror.NvError_Success
    bootburnLib.vlog("     in flashFirmware ")

    if(bootburnLib.targetConfig.TEST_AUTOMATION):
        bootburnLib.vlog("\033[01;33m This is a Automation(GVS) Run, Skipping FW flashing\033[0m")
        return nverror.NvError_Success

    if(bootburnLib.targetConfig.is_sim):
        bootburnLib.vlog("\033[01;33m This is a sim(VDK) Run, Skipping FW flashing\033[0m")
        return nverror.NvError_Success

    if(bootburnLib.targetConfig.is_l4t):
        bootburnLib.vlog("\033[01;33m This is a l4t Run, Skipping FW flashing\033[0m")
        return nverror.NvError_Success

    if(parsed_commandline["no_firmware_update"] == True):
        bootburnLib.vlog("\033[01;33m found command line option --no_firmware_update, Skipping FW flashing\033[0m")
        return nverror.NvError_Success

    if (bootburnLib.targetConfig.n_FlashFirmware == True):
        bootburnLib.vlog("\033[01;33m n_FlashFirmware is  set True in board configuration\033[0m")
        fwUpdateinCfg = True
    else:
        bootburnLib.vlog("\033[01;33m n_FlashFirmware is not set True in board configuration\033[0m")
        bootburnLib.vlog("\033[01;33m Skipping FW flashing\033[0m")
        return nverror.NvError_Success

    #FLASHING UFS Firmware
    print("\033[01;33m Flashing Ufs Firmware\033[0m")
    updater = EmmcUfsFirmware(bootburnLib)
    result = updater.flashFirmware("/dev/block/sda")
    if(result == nverror.NvError_DeviceNotFound):
        updater.vlog("No Ufs found")
    elif (result == nverror.NvError_NoFirmwareAvailable):
        updater.vlog("\033[01;33m Ufs Firmware Unavailable!!\033[0m")
    elif(result != nverror.NvError_Success):
        updater.vlog("\033[01;31m Ufs Firmware UPDATE FAILED!! error code = " + str(result) + "\033[0m")
        flashSuccess += 1
        updater.brickTheTarget(bootburnLib.targetConfig.p_OutDirPath)
    else:
        updater.vlog("\033[01;32m Ufs Firmware UPDATE SUCCESS!!\033[0m")

    if (bootburnLib.targetConfig.s_McuPort == None):
        bootburnLib.vlog("\033[01;33m No Aurix port is provided on command line, Skipping FW flashing\033[0m")
        return nverror.NvError_Success

    #FLASHING MCU FW
    bootburnLib.vlog("\033[01;33m Flashing MCU Firmware\033[0m")
    mcufw = MCU_Firmware(bootburnLib, initConfigDataOnly=False)
    result = mcufw.flashFirmware()
    if (result == nverror.NvError_NoFirmwareFlashNeeded):
        mcufw.vlog("\033[01;31m Target MCU FW is with latest version, no UPDATE NEEDED!! " + str(result) + "\033[0m")
    elif(result == nverror.NvError_FirmwareFlashFail):
        mcufw.vlog("\033[01;31m MCU FW UPDATE FAILED!!, bricking the target " + str(result) + "\033[0m")
        mcufw.brickTheTarget(bootburnLib.targetConfig.p_OutDirPath)
    elif(result == nverror.NvError_Success):
        mcufw.vlog("\033[01;32m MCU FIRMWARE UPDATE SUCCESS!!\033[0m")
    else:
        mcufw.vlog("\033[01;32m unknown error inMCU FIRMWARE UPDATE!!\033[0m")
    return result
