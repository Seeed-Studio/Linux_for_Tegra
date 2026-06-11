#!/usr/bin/python
# SPDX-FileCopyrightText: Copyright (c) 2021-2025, NVIDIA CORPORATION.  All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

import os
import sys
import importlib
import time
import traceback
import fnmatch
import json
import re
from flashtools_nverror import nverror
from flashtools_nverror import AbnormalTermination
from flash_utilities import shell_utilities


class GeneralInfoRomHeader(object):
    type = None
    version = None
    subversion = None
    sizeBytes = 0
    checkSum = None  # 8-bit 2s comp of all other used bytes in ROM (optional at this writing)
    LastUpdated = None
    rawData = []


class ROM_header(GeneralInfoRomHeader):
    lastUpdated = None  # 13:10
    SYS_location = 0  # 18:17
    RWK_location = 0  # 23:22
    SW_location = 0  # 28:27


class SYS_header(GeneralInfoRomHeader):
    objectVersion = None # 3
    buildDate = None  # 11:8
    skuNumber = None  # 35:12
    PCB_revision = None  # 37:36
    BOM_revision = None  # 39:38
    serial = None  # 55:40
    mode = None  # 56
    bootMedia = None  # 58 -- 0: UFS 1: QSPI 2: EMMC
    tegraA_bootmedia = None
    tegraB_bootmedia = None
    AurixMAC = None  # 66:59
    tegraA_MAC = None  # 74:67
    tegraB_MAC = None  # 82:75
    swID = None # 116:115


class  RWK_header(GeneralInfoRomHeader):
    boardReworkFlag = None  # 23:8
    boardRelease = None  # 25:24



class ObjectAllocationTable(object):

      Objecttype = None # 2-0
      Objectversion = None # 3
      Objectsubversion = None #4
      Objectsizebytes = None #6:5
      rawData = []

class Object_Location_Table_Object(ObjectAllocationTable):
      Objecttype = None # 2-0
      Objectversion = None # 3
      Objectsubversion = None #4
      Objectsizebytes = None #6:5


    #ROM Information
      ROMsizeBytes = 0 # 8:7
      Object1Type = None #11:9   "SYS"
      Object1Location = 0 # 13:12 "Byte offset for this object in ROM"
      #SYSO_location
      Object2Type = None # 16:14 "SM1"
      Object2Location = 0 #18:17
      Object3Type = None # 21:19 "SW"
      Object3Location = 0 # 23:22
      Object4Type = None # 26:24 "Customer Object"
      Object4Location = 0 #28:27

    #there are placehorders for total 10 objects
    #add parameters as and when we needed
    #All unused object types and their location
    #  contains 0xFFFFFF  & 0xFFFF

      Object5Type = None #11:9
      Object5Location = 0 # 13:12
      Object6Type = None # 16:14
      Object6Location = 0 #18:17
      Object7Type = None # 21:19
      Object7Location = 0 # 23:22
      Object8Type = None # 26:24
      Object8Location = 0 #28:27
      Object9Type = None #11:9
      Object9Location = 0 # 13:12
      Object10Type = None # 16:14
      Object11ocation = 0 #18:17

      Reserved = None
      CheckSum = None  # 8-bit 2s comp of all other used bytes in ROM (optional at this writing)


#class Object_Header(ObjectAllocationTable):
#    Objecttype = None # 2-0
#    Objectversion = None # 3
#    Objectsubversion = None #4
#    Objectsizebytes = None #6:5

class SYS_Object(ObjectAllocationTable):

    Objecttype = None # 2-0
    Objectversion = None # 3
    Objectsubversion = None #4
    Objectsizebytes = None #6:5

    Systembuilddate = None  # 10:7
    ProductPartNumber = None  # 34:11
    BOMMajorRev = None  # 36:35
    SystemSerialnumber = None # 52:37
    SystemReleaseLevel = None # 54:53
    SWID = None # 56-55
    RWK = None # 60:57
    Reserved = None # 62-61
    Checksum = None # 63

    def __repr__(self):
        return ("SYS_Object:%s, Objecttype:%s, Objectversion:%s, Objectsubversion:%s, \
                  Objectsizebytes:%s, Systembuilddate:%s, ProductPartNumber:%s, \
                  BOMMajorRev:%s, SystemSerialnumber:%s,SystemReleaseLevel:%s, \
                  SWID:%s, RWK:%s, Reserved:%s, Checksum:%s" % (self.Objecttype,
                      self.Objectversion,
                      self.Objectsubversion,
                      self.Objectsizebytes,
                      self.Systembuilddate,
                      self.ProductPartNumber,
                      self.BOMMajorRev,
                      self.SystemSerialnumber,
                      self.SystemReleaseLevel,
                      self.SWID,
                      self.Reserved,
                      self.Checksum))

class Sub_Module_Information_Object(ObjectAllocationTable):
    Objecttype = None # 2-0 "SM1"
    Objectversion = None # 3
    Objectsubversion = None #4
    Objectsizebytes = None #6:5
    #Sub_module_Information_data
    ModulePartNumber = None # 30:7
    PCBRevision = None # 32:31
    BOMMajorrevision = None  # 34:33
    BoardSerialnumber = None # 50:35
    RWK = None # 54:51
    BoardReleaseLevel = None #56:55
    StartingMACAddress = 0 # 64:57
    TotalNumberofMACAddresses = 0 #65
    ModuleSpecificDefine = None # 118:66
    Checksum = 0 #119

    #Module specific define block of SM1 object
class Memory_Configuration_Object  (ObjectAllocationTable):
    MemType = 0 #66
    PowerConfig = 0 #67
    MISCConfig = 0 #68
    Reserved = None #118-69

class Sub_Module_Software_Object(ObjectAllocationTable):
    Objecttype = None # 2-0  "SW"
    Objectversion = None # 3
    Objectsubversion = None #4
    Objectsize = 0 #6:5
    #SW Information
    SWInformation = None # 30:7
    Checksum = None # 31

class Sub_Module_Customer_Block_Object(ObjectAllocationTable):
    Objecttype = None # 2-0 "CUS"
    Objectversion = None # 3
    Objectsubversion = None #4
    Objectsize = 0 #6:5
    #Customer Information
    CustomerInformation = None # 38:7
    Checksum = None # 39

class Sub_Module_T234_Customer_Block_Object(ObjectAllocationTable):
    BlockSignature = None # 10:7
    Length = None # 12:11
    Version = None # 14:13
    TypeSignature = None # 16:15
    WIFIMACAddress = None # 22:17
    BTMACAddress = None # 28:23
    StartingMACAddress = None # 34:29
    TotalMACAddresses = 0 # 35
    I2CMACAddress = None #36 7 bit address
    I2CMACType = 0 # 37
    Reserved = None # 38



class InfoRom(object):

    #xavier Inforom - obj type:ROM
    ROM = ROM_header
    SYS = SYS_header
    RWK = RWK_header

    #thor Inforom - obj type:ROM
    #OHO  = Object_Header
    #OATO = ObjectAllocationTable
    OLTO = Object_Location_Table_Object
    SYSO = SYS_Object
    SMIO = Sub_Module_Information_Object
    MCO  = Memory_Configuration_Object
    SMSO = Sub_Module_Software_Object
    SMCBO = Sub_Module_Customer_Block_Object
    SMTCBO = Sub_Module_T234_Customer_Block_Object
    ROMObjType = None
    SYSObjVersion = None

    def infoRomDataToSting(self, data):
        data = list(reversed(data))
        data = [int(x, 16) for x in data]

        # remove zero padding
        data = [x for x in data if 0 != x]

        data = [chr(y) for y in data]
        data = "".join(data)
        return data

    def infoRomDataToInt(self, data):
        data = list(reversed(data))
        data = "".join(data)
        data = int(data, 16)
        return data

    # Parse methods for  Xavier Inforom(object-type-ROM)  objects
    def parseHeaderInfo(self, data, header):
        header.type = self.infoRomDataToSting(data[0:3])
        header.version = int(data[3], 16)
        header.subversion = int(data[4], 16)
        header.size = self.infoRomDataToInt(data[5:7])
        header.checksum = data[7]

    def parse_ROM_header(self, data):
        rom = ROM_header
        self.parseHeaderInfo(data, rom)
        rom.lastUpdated = list(reversed(data[10:14]))
        rom.SYS_location = self.infoRomDataToInt(data[17:19])
        rom.RWK_location = self.infoRomDataToInt(data[22:24])
        rom.SW_location = self.infoRomDataToInt(data[27:29])
        return rom
    def parse_SYS_header(self, data):
        sh = self.SYS
        self.parseHeaderInfo(data, sh)
        sh.objectVersion = int(data[3], 16)
        sh.buildDate = list(reversed(data[8:11 + 1]))
        sh.skuNumber = self.infoRomDataToSting(data[12:36])
        sh.PCB_revision = chr(int(data[37], 16)) + "0" + str(int(data[36], 16))
        sh.BOM_revision = self.infoRomDataToSting(data[38:40])
        sh.serial = self.infoRomDataToSting(data[40:56])
        sh.mode = int(data[56], 16)
        sh.tegraA_bootMedia = (int(data[58], 16) & 3)
        sh.tegraB_bootMedia = ((int(data[58], 16) >> 4) & 3)
        sh.AurixMAC = ":".join(list(reversed(data[59:67])))
        sh.tegraA_MAC = ":".join(list(reversed(data[67:73])))
        sh.tegraB_MAC = ":".join(list(reversed(data[75:81])))
        sh.swID = self.infoRomDataToSting(data[115:117])
        if(not sh.swID):
            sh.swID="0"
            print("SW_ID is 0 in Inforom")
        return sh

    def parse_RWK_header(self, data):
        rwk = RWK_header
        self.parseHeaderInfo(data, rwk)
        rwk.boardReworkFlag = list(reversed(data[8:24]))
        rwk.boardRelease = self.infoRomDataToSting(data[24:26])
        return rwk


   ####parse methods of Inforom data of Orin (obj-type:OAT)####

    def parseObjectHeaderInfo(self, data, header):
        header.type = self.infoRomDataToSting(data[0:3])
        header.version = int(data[3], 16)
        header.subversion = int(data[4], 16)
        header.size = self.infoRomDataToInt(data[5:7])

    def parse_OAT_Objects_location(self, data):
        rom = self.OLTO
        self.parseObjectHeaderInfo(data, rom)
        rom.Object1Location = self.infoRomDataToInt(data[12:14])  #SYSO
        rom.Object2Location = self.infoRomDataToInt(data[17:19])  #SMIO
        rom.Object3Location = self.infoRomDataToInt(data[22:24])  #SMSO
        return rom

    def parse_SYS_Object(self, data):
        syh = self.SYSO
        self.parseObjectHeaderInfo(data, syh)
        syh.buildDate = list(reversed(data[7:10 + 1]))
        syh.skuNumber = self.infoRomDataToSting(data[11:34+1]) #Product Part Number
        syh.BOM_revision = self.infoRomDataToSting(data[35:36+1])
        syh.SystemSerialNumber = self.infoRomDataToSting(data[37:52+1])
        syh.SystemReleaseLevel = self.infoRomDataToSting(data[53:54+1])
        syh.swID = self.infoRomDataToSting(data[55:56+1])
        syh.RWK = self.infoRomDataToInt(data[57:60+1])
        syh.Checksum = int(data[63],16)
        return syh

    def parse_SMI_Object(self, data):
        smh = self.SMIO
        self.parseHeaderInfo(data, smh)
        smh.ModulePartNumber = self.infoRomDataToSting(data[7:30+1])
        smh.PCB_revision = chr(int(data[31], 16)) + "0" + str(int(data[32], 16))
        smh.BOM_revision = self.infoRomDataToSting(data[33:34+1])
        smh.BoardSerialNumber = self.infoRomDataToSting(data[35:50+1])
        smh.RWK = self.infoRomDataToInt(data[51:54+1])
        smh.BoardReleaseLevel = self.infoRomDataToSting(data[55:56+1])
        smh.StartingMACAddress = self.infoRomDataToSting(data[57:64+1])
        smh.TotalNumberofMACAddresses = int(data[65], 16)
        smh.ModuleSpecificDefine = self.infoRomDataToSting(data[66:118+1])
        smh.Checksum = int(data[119],16)
        return smh

class Aurix(object):

    # targetConfig = target_config.target_config
    # bootburnLib = bootburn_lib.bootburn_lib
    s_McuPort = None
    shellUtils = shell_utilities()
    pyserial = None
    info_rom = None

    def __init__(self, lib, aurixPort=None):
        self.targetConfig = lib.targetConfig
        self.bootburnLib = lib

        if(aurixPort == None):
            aurixPort = self.findT23xAurix()
            if(aurixPort == None):
                AbnormalTermination("Invalid Aurix port", nverror.NvError_ResourceError)
            self.targetConfig.s_McuPort = aurixPort

        if(self.bootburnLib.checkDialoutAccess(aurixPort) == False):
            print("\n\n\033[01;31mNo dialout group access\033[0m\n\n")
            msg  = "\033[01;31m\nTo use -x option or Aurix control you must be a member of the dialout group or be root user\n"
            msg += "To add yourself to the dialout group use the following commands\n"
            msg += "    sudo adduser $USER dialout\n"
            msg += "    logout/login or reboot for this to take affect\033[0m\n"
            self.bootburnLib.vlog(msg)
            AbnormalTermination("Invalid group access rights", nverror.NvError_ResourceError)

        self.s_McuPort = aurixPort
        self.info_rom = InfoRom()

        try:
            self.bootburnLib.log ("***   Aurix Init - p_PyFlashPath :: " + self.targetConfig.p_PyFlashPath + " *** ")
            self.pyserial =  os.path.join(self.targetConfig.p_PyFlashPath, "external", "pyserial")
            sys.path.insert(0, self.pyserial)
        except Exception as e:
            print("Failed to initialize Aurix communications")
            print(str(e))
            AbnormalTermination("Failed to import pyserial", nverror.NvError_ResourceError)

    def log(self, msg):
        self.targetConfig.sysMonitor.log(msg, False)

    # Make sure no other process apart from bootburn
    # are currently accessing the Aurix port.
    def CheckAurixExclusiveAccess(self, l_Aurix):
        ROOT = 0
        if(os.path.exists(l_Aurix) == False):
            AbnormalTermination("Invalid Aurix port given", nverror.NvError_ResourceError)

        if(self.bootburnLib.checkDialoutAccess(l_Aurix) == False):
            self.bootburnLib.vlog("We don't have permissions to access " + l_Aurix)
            return False

        if(os.getuid == ROOT):
            shellCommand = "sudo lsof -w " + l_Aurix
            result = self.shellUtils.executeShellCommand(shellCommand, True)
            if(isinstance(result, str)):
                # In this case there are other users that have that port open
                self.bootburnLib.vlog("We don't have exclusive rights to the tty port " + l_Aurix)
                self.bootburnLib.vlog(result)
                return False
        else:
            #if you are not root just try and see if you have permissions from above
            #just try and open it.  If you can you have exclusive access
            try:
                serial = importlib.import_module("serial")
                aurix = serial.Serial(l_Aurix, 115200, timeout=1)
                aurix.flush()
                aurix.close()
            except:
                self.bootburnLib.vlog("We don't have exclusive rights to the tty port " + l_Aurix)
                return False

        return True

    # Wrapper to execute commands over Aurix. To confirm
    # the successful execution we redirect the output of
    # the serial port to a log file "temporarily". Sleep
    # for 0.5 seconds before executing next command on aurix.
    def ExecuteAurixCommand(self, l_Aurix, l_Command, l_NoCmdExecInResp=False):
        try:
            self.targetConfig.sysMonitor.log(sys._getframe().f_code.co_name + " entered")
            l_readTime = 25
            l_sendAttempts = 3
            skipWrite = False

            if(self.CheckAurixExclusiveAccess(l_Aurix) == False):
                AbnormalTermination("\nAurix port " + l_Aurix + " is not available", nverror.NvError_ResourceError)

            serial = importlib.import_module("serial")
            aurix = serial.Serial(l_Aurix, 115200, timeout=1)
            aurix.flush()
            l_Command = f"\n{l_Command}\n"
            j = 0
            while (j < l_sendAttempts):
                # Sleep for 250 ms when issuing subsequent commands to Aurix.
                # It takes nearly 120 ms for Aurix Power State Manger to publish Reset
                # state to other SW modules and be ready to receive next Request command.
                startTime = time.time()
                endTime = startTime
                l_CmdFailed = False
                time.sleep(.250)
                if (skipWrite == False):
                    self.bootburnLib.log("Send command on Aurix serial port: " + repr(l_Command))
                    aurix.write(bytes(l_Command.encode("ascii")))
                    aurix.flush()
                i = 0
                buffer = ""
                while(endTime - startTime < l_readTime):
                    line = str(aurix.readline())
                    self.bootburnLib.log("Aurix: " + line)
                    buffer += line
                    lineLower = line.lower()
                    # command failed, clear all messages from Aurix and retry
                    if(lineLower.find("command fail") != -1 or lineLower.find("invalid") != -1):
                        while line != b'':
                            line = aurix.readline()
                            self.bootburnLib.log("Aurix: " + str(line))
                        l_CmdFailed = True
                        break
                    elif((lineLower.find("command executed") != -1) or (line==b'' and l_Command=="help\n") or (lineLower.find("succeeded") != -1) or (lineLower.find("end of data") != -1)):
                        self.bootburnLib.log("Aurix command " + repr(l_Command) + " success")
                        aurix.flush()
                        aurix.close()
                        return buffer
                    elif(lineLower.find("cmd executing") != -1):
                        skipWrite=True
                    endTime = time.time()

                self.bootburnLib.vlog("Aurix command " + repr(l_Command) + " failed on " + str(j + 1) + " attempt")
                self.bootburnLib.vlog("Resending command...")
                j += 1
            aurix.flush()
            aurix.close()
            if l_NoCmdExecInResp and (l_CmdFailed == False):
                return buffer
            self.bootburnLib.vlog("      ****  Aurix response  ****")
            self.bootburnLib.vlog(buffer)
            self.bootburnLib.vlog("      ****  End of Aurix response  ****")
            AbnormalTermination("Maximum send attempts reached!\n"
            + " Aurix command " + repr(l_Command) + " failed ... timed out with no valid response\n"
            + " Check log file '" + self.targetConfig.sysMonitor.logfile + "' for more info", nverror.NvError_ResourceError)
        except Exception as e:
            print("Aurix command failed -- " + l_Command)
            print(str(e))
            traceback.print_exc()
            AbnormalTermination("Aurix command failed with exception " + str(e), nverror.NvError_ResourceError)

    # Parse Tegra Device Instances associated to Aurix.
    # This is achieved by putting all associated Tegra
    # in off state and then putting them in recovery one
    # by one. Diffs of lsusb logs are taken to get the
    # associated Tegra Device instance to provided Aurix console.
    def GetTegrasAssocWithAurix(self, l_Aurix):
        self.targetConfig.sysMonitor.log(sys._getframe().f_code.co_name + " entered")
        self.targetConfig.sysMonitor.log("Remove previous instances of files")
        tegrasRequested = []

        tegrasRequested.append(("Tegra-A", "x1", True, self.targetConfig.s_BoardName))

        '''
        # self.bootburnLib.vlog("Disabling SIGINT <Ctrl+C> temporarily")
        self.bootburnLib.vlog("Setting Tegra-A on hold... ")
        self.ExecuteAurixCommand(l_Aurix, "tegrareset x1 h")
        self.bootburnLib.vlog("Done")

        if (self.targetConfig.s_Tegra_B == True):
            self.bootburnLib.vlog("Setting Tegra-B on hold... ")
            self.ExecuteAurixCommand(l_Aurix, "tegrareset x2 h")
            self.bootburnLib.vlog("Done")
        '''
        self.targetConfig.sysMonitor.log("Gather list of Tegras in OFF state")
        lsusbInitList = self.bootburnLib.getListTargetsInRecovery(True)
        self.targetConfig.s_TargetDeviceInfo = {}

        for (name, tegra, setRecovery, boardName) in tegrasRequested:
            if (setRecovery == True):
                self.bootburnLib.vlog("Setting {} in recovery... ".format(name))
                self.ExecuteAurixCommand(l_Aurix, "tegrarecovery {} on".format(tegra))
                self.ExecuteAurixCommand(l_Aurix, "tegrareset {}".format(tegra))

                self.bootburnLib.vlog("Done")
                self.targetConfig.sysMonitor.log("Gather list of {} in ON state".format(name))

                # FIXME -- race condition between Aurix setting the state and Linux enumerating the device
                # is there a better way of doing this in a more event driven fashion rather than sleeping
                # certain amount of time.  Can we react on something?

                max_retry = 10
                for i in range(1, max_retry + 1):
                    lsusbList = self.bootburnLib.getListTargetsInRecovery(True)
                    if(len(lsusbList) != 0):
                        break
                    if(i == max_retry):
                        AbnormalTermination("Could not put {} in recovery".format(name), nverror.NvError_ResourceError)
                    self.bootburnLib.vlog("retrying board recovery check...")
                    time.sleep(0.5)

                tegraDetected = None
                detectedCount = 0
                for target in lsusbList:
                    target.rstrip()
                    if(not target):
                        continue
                    # check if it was in our initial list of devices
                    if(target in lsusbInitList):
                        continue
                    # if we got here this is our target
                    tegraDetected = target
                    detectedCount += 1
                    break

                if(tegraDetected == None):
                    self.bootburnLib.vlog("No valid {} ... must have a primary SOC".format(name))
                    AbnormalTermination("Could not put {} in recovery".format(name), nverror.NvError_ResourceError)
                if(detectedCount > 1):
                    self.bootburnLib.vlog("Found more than one valid device  for {} recovery  ...".format(name))
                    AbnormalTermination("Could not detect {} in recovery".format(name), nverror.NvError_ResourceError)


                # add it to our init list
                lsusbInitList.append(tegraDetected)
                self.bootburnLib.AddInstanceToFlashList(tegraDetected, name, boardName)

    # Execute commands on Aurix to reset a tegra
    def AurixTegraReset(self, s_McuPort, l_TegraInstance):
        self.targetConfig.sysMonitor.log(sys._getframe().f_code.co_name + " entered")
        self.bootburnLib.vlog("Re-setting " + l_TegraInstance)
        self.ExecuteAurixCommand(s_McuPort, "tegrarecovery " + l_TegraInstance + " off")
        self.ExecuteAurixCommand(s_McuPort, "tegrareset " + l_TegraInstance)
        self.bootburnLib.vlog("Done")

    def CheckFirmwareisAFW(self, s_McuPort):
        self.targetConfig.sysMonitor.log(sys._getframe().f_code.co_name + " entered")
        versionStrResponse = self.ExecuteAurixCommand(s_McuPort, "version", True) #Command Executed is not expected in the response text
        verLines = versionStrResponse.splitlines()
        for line in verLines:
            if "SW Version" in line:
                self.tgtAurixFwVersion = line
                break

        if ("AFW" in versionStrResponse):
            self.bootburnLib.vlog("AFW firmware found")
            return True
        self.bootburnLib.vlog("IFW firmware found")
        return False

    def SetDefaultBootChain(self, s_McuPort, l_TegraInstance, chainName):
        self.targetConfig.sysMonitor.log(sys._getframe().f_code.co_name + " entered")
        helpStr = self.ExecuteAurixCommand(s_McuPort, "help", True)
        if "setdfltbtchain" in helpStr:
            self.bootburnLib.vlog("setting default boot-chain: " + l_TegraInstance + " " + chainName)
            self.bootburnLib.vlog("Executing setdfltbtchain")
            self.ExecuteAurixCommand(s_McuPort, "setdfltbtchain " + l_TegraInstance + " " + chainName)
        else:
            self.bootburnLib.vlog("Skipping setdfltbtchain as command not supported in current firmware")
        self.bootburnLib.vlog("Done")

    def GetInfoRom(self, l_Aurix):
        try:
            serial = importlib.import_module("serial")
            self.targetConfig.sysMonitor.log(sys._getframe().f_code.co_name + " entered")
            self.bootburnLib.vlog("Read skuinfo from InfoRom...")
            if(self.bootburnLib.checkDialoutAccess(l_Aurix) == False):
                msg  = "\033[01;31m\nTo use -x option or Aurix control you must be a member have RW permissions \n"
                msg += "to " + str(l_Aurix) + " or either be root or part of the the dialout group\n"
                msg += "To add yourself to the dialout group use the following commands\n"
                msg += "    sudo adduser $USER dialout\n"
                msg += "    logout/login or reboot for this to take affect\033[0m\n"
                self.bootburnLib.vlog(msg)
                AbnormalTermination("Invalid group access rights", nverror.NvError_ResourceError)

            aurix = serial.Serial(l_Aurix, 115200, timeout=1)
            aurix.flush()
            self.targetConfig.sysMonitor.vlog("Execute command on Aurix serial port")

            aurixCommand = "inforom print\n"

            aurix.write(aurixCommand.encode("ascii"))
            self.InfoRomData = aurix.read(size=2048)
            aurix.close()
            self.InfoRomData = str(self.InfoRomData).rstrip().replace("\\r\\n", "\n")
            self.bootburnLib.log(str(self.InfoRomData))
            if(not self.InfoRomData):
                AbnormalTermination("Aurix inforom command failed", nverror.NvError_ResourceError)

            self.ParseInfoRom_Print(self.InfoRomData)
            return self.info_rom

        except Exception as e:
            self.bootburnLib.vlog("Exception in getting info-ROM data " + str(e))
            traceback.print_exc()
            AbnormalTermination("Aurix inforom command failed with exception", nverror.NvError_ResourceError)

        return self.info_rom

    def get_pattern_value(self, pattern, inforom_output, all_Str=False):
        """
        Extract the Pattern value from INFOROM status output.

        Args:
            inforom_output (str): The INFOROM status output text
            all_Str (bool): If True, return all matches, otherwise return the first match

        Returns:
            str: The Product Part Number if found, None otherwise
        """
        # Look for the line containing patterns
        if all_Str:
            match = re.findall(pattern, inforom_output)
        else:
            match = re.search(pattern, inforom_output)

        if match:
            if all_Str:
                return match
            else:
                return match.group(1).strip()

        msg = ("Could not find pattern in inforom output: " + pattern)
        AbnormalTermination(msg, nverror.NvError_ResourceError)
        return None

    def ParseInfoRom_Print(self, infoRomData):
        try:
            pattern = r"Object type\s*:\s*([^\n]+)"
            self.targetConfig.s_InforomObjType = self.get_pattern_value(pattern, infoRomData)

            pattern = r"Object version\s*:\s*([^\n]+)"
            version = self.get_pattern_value(pattern, infoRomData, all_Str=True)
            version = version[1]
            self.targetConfig.s_InforomSYSObjVersion = version

            pattern = r"Product Part Number\s*:\s*([^\n]+)"
            self.targetConfig.s_Product_Part_Number = self.get_pattern_value(pattern, infoRomData)

            self.bootburnLib.vlog("Product Part Number: " + self.targetConfig.s_Product_Part_Number)

        except Exception as e:
            print("Exception in parsing info-ROM data " + str(e))
            traceback.print_exc()
            AbnormalTermination("Aurix inforom parsing failed with exception", nverror.NvError_ResourceError)

    def ParseInfoRom_ROM(self, infoRomData):
        try:
            if(infoRomData.find("4D") == -1):
                AbnormalTermination("Could not find start of inforom", nverror.NvError_ResourceError)

            index = infoRomData.find("4D")
            infoRomData = infoRomData[index:]

            infoRomData = infoRomData.split()

            self.info_rom.ROM = self.info_rom.parse_ROM_header(infoRomData)

            sysIndex = self.info_rom.ROM.SYS_location
            rwkIndex = self.info_rom.ROM.RWK_location
            self.info_rom.SYS = self.info_rom.parse_SYS_header(infoRomData[sysIndex:])
            #Getting error in Colossus p2663  - cl1-2237
            #self.info_rom.RWK = self.info_rom.parse_RWK_header(infoRomData[rwkIndex:])
            self.info_rom.ROMObjType = str(self.info_rom.ROM.type)
            self.info_rom.SYSObjVersion = str(self.info_rom.SYS.version)
            self.bootburnLib.log(self.info_rom.ROMObjType)
            self.bootburnLib.log(self.info_rom.SYSObjVersion)

        except Exception as e:
            print("Exception in parsing info-ROM data " + str(e))
            traceback.print_exc()
            AbnormalTermination("Aurix inforom parsing failed with exception", nverror.NvError_ResourceError)

    def ParseInfoRom_OAT(self, infoRomData):
        try:
            if(infoRomData.find("54") == -1):
                AbnormalTermination("Could not find start of inforom", nverror.NvError_ResourceError)

            index = infoRomData.find("54")
            infoRomData = infoRomData[index:]
            infoRomData = infoRomData.split()
            infoList = []
            for item in infoRomData:
                if len(item) == 4:
                    infoList.append(item)

            self.info_rom.OLTO = self.info_rom.parse_OAT_Objects_location(infoList)
            self.bootburnLib.log("***retrieved the object locations***")
            self.bootburnLib.log(str(self.info_rom.OLTO))

            sysIndex = self.info_rom.OLTO.Object1Location
            sm1Index = self.info_rom.OLTO.Object2Location
            self.bootburnLib.log("sysIndex :: " + str(sysIndex))
            self.bootburnLib.log("sm1Index :: " + str(sm1Index))

            self.info_rom.SYSO = self.info_rom.parse_SYS_Object(infoRomData[sysIndex:])
            self.info_rom.SMIO = self.info_rom.parse_SMI_Object(infoRomData[sm1Index:])
            self.info_rom.ROMObjType = str(self.info_rom.OLTO.type)
            self.info_rom.SYSObjVersion = str(self.info_rom.SYSO.version)
            self.bootburnLib.log(self.info_rom.ROMObjType)
            self.bootburnLib.log(self.info_rom.SYSObjVersion)
        except Exception as e:
            print("Exception in parsing info-ROM data " + str(e))
            traceback.print_exc()
            AbnormalTermination("Aurix inforom parsing failed with exception", nverror.NvError_ResourceError)

    def setTargetConfigt264InfoRomInfo(self, tc):
        return

        skunumber = ""
        tc.s_InforomObjType = str(self.info_rom.ROMObjType)
        tc.s_InforomSYSObjVersion = str(self.info_rom.SYSObjVersion)
        if(self.info_rom.ROMObjType == "OAT"):
            self.bootburnLib.vlog(str( self.info_rom.SYSO.skuNumber))
            self.bootburnLib.vlog(str( self.info_rom.SMIO.PCBRevision))
            skunumber = self.info_rom.SYSO.skuNumber
        else:
            skunumber = self.info_rom.SYS.skuNumber
        skunumber = skunumber.strip()
        tc.s_InforomProdInfo = skunumber.rpartition("-")[0]
        tc.s_InforomSkuVersion = skunumber.rpartition("-")[2]
        self.bootburnLib.vlog("******  s_InforomSkuVersion   " + str(tc.s_InforomSkuVersion))
        self.bootburnLib.vlog("******  s_InforomProdInfo   " + str(tc.s_InforomProdInfo))
        tc.s_tgtAurixFwVersion = self.tgtAurixFwVersion

    def validateTargetBoardName(self, boardName, boardMapFile, socSkuMapFile):
        #Validate the boardName matches with nforom data

        with open(socSkuMapFile, 'r') as fsocSkuMap:
            socSkuMapDict = json.load(fsocSkuMap)

        socSkuBoardModifier = boardName.rpartition("-")[2]
        if (socSkuBoardModifier in socSkuMapDict):
            boardName = boardName.rpartition("-")[0]

        with open(boardMapFile, 'r') as fboardMap:
            boardMapDict = json.load(fboardMap)

        if (boardName in boardMapDict):
            prodInfoList = boardMapDict[boardName]
        else:
             self.bootburnLib.vlog("boardName " + boardName + " is not in the baseboardmap")
             AbnormalTermination("error-boardName-not-found-in-baseboard-map-file", nverror.NvError_BomDataBoardMapFileNotFound)

        if(len(prodInfoList) != 0):
            if (self.info_rom.SYSO.skuNumber in prodInfoList):
                self.bootburnLib.vlog("boardName " + boardName + " matches with the connected board")
            else:
                self.bootburnLib.vlog("boardName " + boardName + "  is not matching the connected board")
                AbnormalTermination("error-target-prodInfo-board-mismatch", nverror.NvError_BomDataBoardMapFileNotFound)
        else:
             self.bootburnLib.vlog("boardName " + boardName + " is having empty list in the baseboardmap")
             AbnormalTermination("error-empty prodinfo list for boardName in baseboard-map-file", nverror.NvError_BomDataBoardMapFileNotFound)

    def findTargetBaseBoardName(self, tc, boardMapFile):
        boardMapVal = tc.s_Product_Part_Number
        with open(boardMapFile, 'r') as fboardMap:
            boardMapDict = json.load(fboardMap)
        for baseBoardName, ProdSkuList in boardMapDict.items():
            if(boardMapVal in ProdSkuList):
                tc.s_baseBoardName = baseBoardName
                self.bootburnLib.vlog("baseBoardName found :: " + baseBoardName)
                break

    def setTargetConfigSkuInfo(self, tc, targetName="Tegra-A"):
        tc.skuArguments["s_NvSkuArgs"] = "--skunum " + str(self.info_rom.SYS.skuNumber)
        tc.skuArguments["s_NvSkuArgs"] += "  --setprodinfo  " +  str(self.info_rom.SYS.skuNumber) + "  " + str(self.info_rom.SYS.BOM_revision)
        # Use this dictionary for legacy flow to add version information
        data_dict = {
            "version": 1,
            "value": None
        }

        data_dict["value"] = str(self.info_rom.SYS.skuNumber)
        tc.customerData.setKeyValue("skuNumber", dict(data_dict))

        data_dict["value"] = str(self.info_rom.SYS.skuNumber) + "  " + str(self.info_rom.SYS.BOM_revision)
        tc.customerData.setKeyValue("prodInfo", dict(data_dict))
        if (self.info_rom.SYS.objectVersion >= tc.n_minSysVerforTargetValidation):
            tc.skuArguments["s_NvSkuArgs"] += "  --setskuversion " + str(self.info_rom.SYS.swID)
            tc.customerData.setKeyValue("skuVersion", str(self.info_rom.SYS.swID))
        else:
            tc.skuArguments["s_NvSkuArgs"] += "  --setskuversion " + str(self.info_rom.SYS.BOM_revision)
            tc.customerData.setKeyValue("skuVersion", str(self.info_rom.SYS.BOM_revision))

        # Set board serial number from InfoROM
        boardSerial = str(self.info_rom.SYS.serial)
        self.bootburnLib.vlog("Board serial number from Aurix InfoROM is " + boardSerial)
        if (len(boardSerial) >= 4):
            # Get the last four characters
            boardSerial = boardSerial[-4:]
            self.bootburnLib.vlog("Extracting last four characters as board serial " + boardSerial)

        if (not boardSerial.isdigit()):
            self.bootburnLib.vlog("Extracted Aurix board serial is not a number..defaulting to 1234")
            boardSerial = "1234"

        tc.skuArguments["s_NvSkuArgs"] += "  --setboardserial " + boardSerial
        data_dict["value"] = boardSerial
        tc.customerData.setKeyValue("boardSerial", dict(data_dict))

        if (targetName == "Tegra-A"):
            if (tc.TEST_AUTOMATION is False):
                tc.skuArguments["s_NvSkuArgs"] += "  --setmacid mac0 0x" + str(self.info_rom.SYS.tegraA_MAC).replace(':', '')
                data_dict["value"] = "mac0 0x" + str(self.info_rom.SYS.tegraA_MAC).replace(':', '')
                tc.customerData.setKeyValue("macId0", dict(data_dict))
            else:
                tc.skuArguments["s_NvSkuArgs"] += "  --setmacid mac0 " + "0xaabbccddeeff"
                data_dict["value"] = "mac0 " + "0xaabbccddeeff"
                tc.customerData.setKeyValue("macId0", dict(data_dict))
        elif (targetName == "Tegra-B"):
            if (tc.TEST_AUTOMATION is False):
                tc.skuArguments["s_NvSkuArgs"] += "  --setmacid mac0 0x" + str(self.info_rom.SYS.tegraB_MAC).replace(':', '')
                data_dict["value"] = "mac0 0x" + str(self.info_rom.SYS.tegraB_MAC).replace(':', '')
                tc.customerData.setKeyValue("macId0", dict(data_dict))
            else:
                tc.skuArguments["s_NvSkuArgs"] += "  --setmacid mac0 " + "0xaabbccddaaff"
                data_dict["value"] = "mac0 " + "0xaabbccddaaff"
                tc.customerData.setKeyValue("macId0", dict(data_dict))
        elif (targetName == "Tegra-C"):
            tc.skuArguments["s_NvSkuArgs"] += "  --setmacid mac0 " + "0xaabbccddbbff"
            data_dict["value"] = "mac0 " + "0xaabbccddbbff"
            tc.customerData.setKeyValue("macId0", dict(data_dict))

        self.bootburnLib.vlog("s_skuargs :" + str(tc.skuArguments["s_NvSkuArgs"]))

    def getBoardFromBomDataTable(self, bomDataFile, connectedTargetUniqueId):

        valid_target_str = ""
        valid_target_board_list = list()
        boards  = list()
        columns = list()
        stripped_column = ""
        try:
            with open(bomDataFile, 'r') as fid:
                flines = list(fid)
                self.bootburnLib.vlog('created list of lines from bom_data_table.txt file')
        except:
            print('error in opening bom_data_file.txt')
            AbnormalTermination("error-tool-no-bomdata-board-mapfile", nverror.NvError_BomDataBoardMapFileNotFound)

        for line in range(len(flines)):
            columns = str.split(flines[line], ':')
            if(len(columns) >= 10):
                valid_target_str = columns[1].strip() + '-' + columns[9].strip()
                if(valid_target_str == connectedTargetUniqueId):
                    stripped_column = columns[10].strip()
                    if(stripped_column.count('p2888')):
                         boards = str.split(stripped_column, '|')
                         for board in range(len(boards)):
                             valid_target_board_list.append(boards[board].strip())
                    else:
                        valid_target_board_list.append(stripped_column)

        return valid_target_board_list

    def getRevFromBoardList(self, board_list):

        base = "e3550"
        rev = ""
        for board in board_list:
            if (base in board):
                rev = board[len(base):]

        return rev

    def aurixValidateTarget(self, boardName, bomDataFile, minSysObjVer):
        if ("useFullBoardName" in self.targetConfig.boardDefaultPaths):
            matchName = boardName
        else:
            boardName = boardName.split("-")
            matchName = boardName[0]
        board_count=0
        stacked_board = False
        if(self.info_rom.SYS.objectVersion < minSysObjVer):
            print('Not performing target validation.  SYS OBJ version of the target is ' + str(self.info_rom.SYS.objectVersion) + ' and less  than min SYS OBJ version  ' + str(minSysObjVer) )
            return True
        self.bootburnLib.log('peforming target validation')
        #customizing ValidateTarget for p3479. SW_ID is zero for p3479
        connectedTargetUniqueId = self.info_rom.SYS.skuNumber + '-' + str(self.info_rom.SYS.swID)
        self.bootburnLib.log('Connected Target Unique ID : ' + connectedTargetUniqueId)
        #find the targetUniqueId in the valid target list for the given board

        valid_target_board_list = self.getBoardFromBomDataTable(bomDataFile, connectedTargetUniqueId)

        if ('p2888' in valid_target_board_list):
            stacked_board = True

        if(((len(valid_target_board_list))>1) and (stacked_board == False)):
            self.bootburnLib.log('Corrupted bom_data_table.txt file? Mutliple board types found matching in the file for the connected target. ')
            return False

        target_found_count = valid_target_board_list.count(matchName)

        if(target_found_count > 1):
            self.bootburnLib.log('Corrupted bom_data_table.txt file? Multiple matching entries are found in the file for the connected target. ')
            return False
        elif(target_found_count == 0):
            self.bootburnLib.log('Connected target is not listed in bom_data_table file, update the file for successful target validation')
            if (len(valid_target_board_list) > 0):
                print("Might the board be: ")
                print(valid_target_board_list)
                print("\n")
            return False
        else:
            if(target_found_count == 1):
                self.bootburnLib.log('target validation is successful')
                return True
            else:
                self.bootburnLib.log('Connected target is marked for board ' + valid_target_board_list[0] + '.  Update with correct bom_data_table.txt')
                return False

    def findAurix(self, port):
        deviceDir = os.listdir("/dev/")
        serial = importlib.import_module("serial")
        aurixPort = None
        for device in reversed(deviceDir):
            if(device.find(port) != -1):
                try:
                    device = "/dev/" + device
                    aurix = serial.Serial(device, 115200, timeout=0.25)
                    aurixCommand = "\n"
                    aurix.write(aurixCommand.encode("ascii"))
                    infoRomData = str(aurix.read(size=16))
                    aurix.close()
                    if(infoRomData.lower().find("shell") != -1):
                        aurixPort = device
                        break
                except Exception as e:
                    print(str(e))
                    continue

        return aurixPort

    def findT23xAurix(self):
        aurixPort = ""
        aurixT23xPortDict = {"p3710":"ttyACM1", "p3663":"ttyUSB1"}
        for board,port in aurixT23xPortDict:
            if(self.targetConfig.s_BoardName.find(board)):
                aurixPort = port
                break;
        aurixPort = findAurix(port)
        if(aurixPort is None):
            print("Aurix port not found for {} board".format(self.targetConfig.s_BoardName))
            AbnormalTermination("find T264 Aurix port for board {}".format(self.targetConfig.s_BoardName), nverror.NvError_ResourceError)
        else:
            print("t264 aurix port : {}".format(aurixPort))
        return aurixPort
