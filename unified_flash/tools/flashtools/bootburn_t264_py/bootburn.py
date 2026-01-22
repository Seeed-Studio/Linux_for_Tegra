#!/usr/bin/python
#
# SPDX-FileCopyrightText: Copyright (c) 2017-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

#####
# p_* Path
# f_* Filename
# s_* String variable
# n_* Numeric variable
#####

import sys
import io
import os
import fnmatch
import time
import uuid
import json
import traceback
import multiprocessing
import signal
import importlib
try:
    from queue import Empty
except ImportError:
    from Queue import Empty
print_capture = io.StringIO()
sys.stdout = print_capture
from multiprocessing import Process, Queue
from target_config import target_config
from bootburn_parser import commandline_parser
from flashtools_nverror import nverror, AbnormalTermination
from flash_utilities import shell_utilities
from system_monitor import monitor
from bootburn_lib import bootburn_lib
from create_bsp_images import create_bsp
from flash_bsp_images import flash_bsp
from bootburn_aurix import Aurix
from flash_bsp_images import flash_bsp_active
sys.stdout = sys.__stdout__


tempDir = None

def progress_bar(counterUpdateQueue, counter = 0):
    """ Function to update the tqdm progress bar

        Blocks until timeout or there is a value in the input
        queue.
            - If the value is 0, progress bar completes
            - For any other values, proress bar gets updated

    Args:
        counterUpdateQueue (multiprocessing.Queue): Queue for the progress bar update values
        counter (int): Total progress bar counts. Positive value has to be passed by
                       user otherwise exception is raised
    """
    if (counter == 0):
        # throw an exception if progress bar is requested with counter value 0
        AbnormalTermination("Invalid progress bar counter value!", nverror.NvError_BadParameter)

    m_tqdm = importlib.import_module('tqdm')
    pbar = m_tqdm.tqdm(desc='Flashing ', initial=1, total=counter, ncols=70,  bar_format='{desc}{percentage:3.0f}%|{bar}|')

    while(True):
        try:
            # Blocks until timeout or until queue has value in it
            # random timeout of 10s to keep the process from hanging forever
            updateValue = counterUpdateQueue.get(block=True, timeout=10)
        except Empty:
            continue

        # Process end condition is updateValue == 0
        if (updateValue == 0):
            break

        pbar.update(int(updateValue))

    pbar.n = pbar.total
    pbar.close()

def bootburn_active(bootburnLib, p_BurnDir):
    try:
        error = 0
        targetConfig = bootburnLib.targetConfig

        n_chipId = targetConfig.GetChipID()

        n_vdkOption = targetConfig.parsed_commandline["vdk"]
        n_fpgaOption = targetConfig.parsed_commandline["fpga"]

        if (targetConfig.parsed_commandline["T"] == True):
            sys.settrace(bootburnLib.call_trace)
            targetConfig.sysMonitor.callTraceOut = True

        if(n_vdkOption == False):
            bootburnLib.StartNewSession_t264()
            bootburnLib.GetTargetECID("--chip 0x26")
            bootburnLib.CheckFuseForAuthentication()

            bootburnLib.GenAdbSerialNum()
            bootburnLib.CheckForDeviceType()
            bootburnLib.ValidateUserOptionAndDeviceType()
            bootburnLib.CheckForT26xChip()

        bootburnLib.CheckFuseForEncryption()
        bootburnLib.DetectChipVersion()
        n_RcmMode = targetConfig.parsed_commandline["R"]
        n_FaMode  = targetConfig.parsed_commandline["f"]
        bootburnLib.ValidateFindBoardForFused()

        # Fixme ???? Right place?
        if (targetConfig.n_FlashQnx):
            targetConfig.s_StorageDtbImgName = targetConfig.s_QnxDtbImgName
        else:
            targetConfig.s_StorageDtbImgName = targetConfig.s_LinuxDtbImgName

        targetConfig.BoardSetFilePathsAndDefaultValues()

        if((n_vdkOption == False) and (n_chipId == "0x26")):
            from bootburn_thor import bootburn_thor
            bootburnThor = bootburn_thor(targetConfig)

            if(targetConfig.b_findBoardName):
                bootburnThor.BootRCM_CreateFlash("rcm-flash", l_findBoardOnly=True)
                return error
            if(n_FaMode or n_RcmMode):
                bootburnThor.BootRCM_CreateFlash("rcm-boot")
            elif (targetConfig.n_UFSProvisioningEnable):
                bootburnThor.BootRCM_CreateFlash("rcm-flash-provision")
                targetConfig.n_WaitOnQueue = False
                bootburnLib.UFSDeviceProvision(targetConfig.p_OutDirPath)
                targetConfig.sysMonitor.vlog("UFS-provision Config file : " + targetConfig.f_UfsProvisionCfg)
            else:
                targetConfig.sysMonitor.vlog("creating and flashing coldboot images")
                targetConfig.n_WaitOnQueue = False
                bootburnThor.Boot_Create_Flash_Coldboot()

        elif (n_FaMode or n_RcmMode):
            targetConfig.n_WaitOnQueue = False
            bootburnLib.BootRCM("rcm-boot", targetConfig.p_OutDirPath)
        elif (targetConfig.n_UFSProvisioningEnable):
            targetConfig.n_WaitOnQueue = False
            bootburnLib.BootRCM("rcm-flash-provision", targetConfig.p_OutDirPath)
            targetConfig.sysMonitor.vlog("UFS-provisioning starting")
            targetConfig.sysMonitor.log("UFS-provision Config file : " + targetConfig.f_UfsProvisionCfg)
            bootburnLib.UFSDeviceProvision(targetConfig.p_OutDirPath)
        else:
            bootburnLib.BootRCM("rcm-flash", targetConfig.p_OutDirPath)
            targetConfig.p_TopOutDataPath = targetConfig.p_OutDirPath
            targetConfig.n_WaitOnQueue = False
            bootburnLib.CreateFlashImages(targetConfig.f_FlashCfg)
            bootburnLib.FlashImages(bootburnLib.targetConfig.p_OutDirPath)

    except Exception as e:
        bootburnLib.shellUtils.printError()
        print("\n\033[01;31mException {} raised in bootburn_active \033[0m".format(str(e)))
        traceback.print_exc()
        if(isinstance(e.args[0], int)):
            error = int(e.args[0])
        else:
            error = int(nverror.NvError_Unknown)
    finally:
        # if bootburn_active is a separate process return with
        # sys.exit to pass proper exitcode to parent
        bootburnLib.updateFindBoardData()
        if bootburnLib.ProcessIsChild():
            sys.exit(error)
        return error

def bootburn(commandLineArguments):
    print ("********* Starting t264 bootburn py ********")
    signal.signal(signal.SIGINT, interruptHandler)
    global tempDir
    debug = False
    GenericOptions = [
        'b', 'c', 'd', 'e', 'f', 'h', 'j', 'k', 'l', 'm', 'o', 'p', 'q', 's',
        't', 'u', 'x', 'y', 'B', 'C', 'D', 'E', 'I', 'L', 'M', 'P', 'R',
        'T', 'O', 'U', 'V', 'X', 'Y',
        'adbSerial', 'board_config', 'chain', 'clean', 'customer_data', 'devicetype',
        'disable-uart', 'no_du_pvit', 'encrypt', 'encryption_key', 'filesystem', 'find_board_name',
        'hsm', 'init_persistent_partitions', 'logs', 'no_firmware_update', 'platform',
        'pl_encryption_key', 'pytest', 'safety', 'user']

    startTime = time.time()

    shellUtils = shell_utilities()
    exitcode = 0
    platformPkg = None

    if 'platform' not in str(commandLineArguments):
        print("Bootburn Starting with arguments " + str(commandLineArguments))
    else:
        print("NVIDIA DRIVEOS Flashing Tool")
        print("Log file at ./bootburn.txt\n")

    try:
        DirBeingRunFrom = os.path.realpath(__file__)
        DirBeingRunFrom = os.path.dirname(DirBeingRunFrom)
        cwd = os.getcwd()
        os.chdir(DirBeingRunFrom)
        flashPath = os.path.join(DirBeingRunFrom, "..", "..","..","tools", "flashtools")
        isPdkPackage = os.path.isdir(flashPath)
        targetConfig = target_config(isPdkPackage)
        targetConfig.p_BurnDir = os.getcwd()
        if(len(commandLineArguments) <= 1):
            targetConfig.sysMonitor = monitor("USAGE", "/tmp", debug)
            b = bootburn_lib(targetConfig)
            b.Usage(GenericOptions)
            return

        targetConfig.sysMonitor = monitor("bootburn" , targetConfig.p_logs, debug)
        cmdParser = commandline_parser(commandLineArguments)
        cmdParser.initializeTargetConfig(targetConfig, GenericOptions, False)
        shellUtils.setMonitor(targetConfig.sysMonitor)

        bootburnLib = bootburn_lib(targetConfig)

        targetConfig.sysMonitor.log("commandline  arguments " + str(commandLineArguments))

        if(cmdParser.parsed_commandline["h"] == True):
            bootburnLib.Usage(GenericOptions)
            return nverror.NvError_Success

        if (targetConfig.parsed_commandline["T"] == True):
            sys.settrace(bootburnLib.call_trace)
            targetConfig.sysMonitor.callTraceOut = True

        if(not targetConfig.p_OutDirPath):
            targetConfig.p_OutDirPath = os.path.join(os.getcwd(), "__temp")

        shellUtils.makeDirectory(targetConfig.p_OutDirPath)
        tempDir = targetConfig.p_OutDirPath

        bootburnLib.initFindBoardOS(isPdkPackage, DirBeingRunFrom,  commandLineArguments)
        cmdParser = commandline_parser(commandLineArguments)

        if(targetConfig.s_McuPort):
            bootburnLib.aurix.GetInfoRom(targetConfig.s_McuPort)
            bootburnLib.aurix.setTargetConfigt264InfoRomInfo(targetConfig)
            if cmdParser.parsed_commandline["find_board_name"]:
                if (not bootburnLib.findBaseBoardName(targetConfig)):
                    targetConfig.sysMonitor.log("Error in getting baseBoard name")
                bootburnLib.updateFindBoardData()

        bootburnLib.CheckRecoveryTargets()
        bootburnLib.SetUsbAutoSuspend("disable")

        targetConfig.p_FlashFiles = targetConfig.p_OutDirPath
        if (os.path.isdir(targetConfig.p_FlashFiles) == False):
            shellUtils.makeDirectory(targetConfig.p_FlashFiles)

        bootburnLib.targetConfig.d_ProcessInfo["ctpQueue"] = childToParentQueue = Queue()
        bootburnLib.targetConfig.d_ProcessInfo["ptcQueue"] = parentToChildQueue = Queue()
        processID = 0

        for targetName in targetConfig.s_TargetDeviceInfo:
            targetInfo = targetConfig.s_TargetDeviceInfo[targetName]
            uid = str(uuid.uuid4()).replace("-", "")
            boardName = targetConfig.s_BoardName

            cmdParser.overrideBoardName(targetInfo["boardName"])
            tc = target_config(isPdkPackage)
            tc.p_BurnDir = targetConfig.p_BurnDir
            tc.s_McuPort = targetConfig.s_McuPort
            tc.s_baseBoardName = targetConfig.s_baseBoardName
            tc.s_BoardName = targetConfig.s_BoardName
            tc.f_findBoardName =  targetConfig.f_findBoardName
            tc.b_findBoardinNativeQnx = targetConfig.b_findBoardinNativeQnx
            tc.b_findBoardinNativeLinux = targetConfig.b_findBoardinNativeLinux
            tc.sysMonitor = monitor("bootburn" + targetName, tc.p_logs, debug)
            cmdParser.initializeTargetConfig(tc, GenericOptions)
            bbLib = bootburn_lib(tc)

            cmdParser.parsePlatformConfig(tc)
            tc.p_OutDirPath = os.path.join(targetConfig.p_OutDirPath, targetName + "-" + uid[0:11])
            if(targetConfig.s_McuPort != None):
                bootburnLib.aurix.setTargetConfigt264InfoRomInfo(tc)
                if (tc.s_InforomObjType == tc.s_InforomT23xObjName) and (int(tc.s_InforomSYSObjVersion, 16) >= tc.n_InforomT23xSYSObjMinVer):
                    bootburnLib.aurix.validateTargetBoardName(boardName, tc.f_baseBoardSkuMapping, tc.f_chipSkuMapping)

            tc.p_TempDumpPath = tc.p_OutDirPath
            tc.s_TargetDeviceInfo = {}
            tc.s_TargetDeviceInfo[targetName] = targetInfo
            shellUtils.makeDirectory(tc.p_OutDirPath)

            if(targetConfig.parsed_commandline["y"]):
                #Keep in same memory space for debugging
                targetConfig.sysMonitor.log("\033[01;33m y-option selected ... Not creating separate processes \033[0m")
                bbLib.targetConfig.d_ProcessInfo["child"] = False
                exitcode = bootburn_active(bbLib, DirBeingRunFrom)
            else:
                #make thread daemon = true  so that it will die when the main
                #thread exits.  No need for clean up code
                bbLib.targetConfig.d_ProcessInfo["ctpQueue"] = childToParentQueue
                bbLib.targetConfig.d_ProcessInfo["ptcQueue"] = parentToChildQueue
                bbLib.targetConfig.d_ProcessInfo["child"] = True
                bootburnLib.d_ChildrenStatus["exitcode"] = 0
                process = Process(target=bootburn_active, args=(bbLib, DirBeingRunFrom))
                process.start()
                bootburnLib.d_ChildrenStatus["active"].append(process)
                bootburnLib.d_ChildrenStatus["running"] = True
                processID += 1

        bootburnLib.WaitOnImageGeneration(processID)
        if (not targetConfig.parsed_commandline["R"]):
            bootburnLib.AdbServerManager(processID)
        bootburnLib.WaitOnFlashing()

        if (processID > 0):
            exitcode = bootburnLib.d_ChildrenStatus["exitcode"]

        if (bootburnLib.d_ChildrenStatus["timeCounter"] > bootburnLib.n_Timeout):
            targetConfig.sysMonitor.vlog("\n\033[01;31mFlashing timed out -- Exiting\033[0m\n")
            exitcode = nverror.NvError_Timeout
            sys.exit(nverror.NvError_Timeout)

        endTime = time.time()
        totalTime = endTime-startTime

        if(exitcode == 0):
            if(targetConfig.s_McuPort != None) and (not (targetConfig.parsed_commandline["R"] or targetConfig.parsed_commandline["f"])):
                targetConfig.sysMonitor.vlog("Resetting Aurix")
                with bootburnLib.retGVSsync():
                    bootburnLib.aurix.AurixTegraReset(targetConfig.s_McuPort, "x1")
            targetConfig.sysMonitor.vlog("\n\033[01;32mBootburn completed successfully!\033[0m\n")
            targetConfig.sysMonitor.vlog("Bootburn Time " + str(totalTime) + " seconds\n")


    except Exception as e:
        if(isinstance(e.args[0], int)):
            exitcode = int(e.args[0])
        else:
            exitcode = nverror.NvError_Unknown
        print("\n\033[01;31mException caught in bootburn \033[0m")
        traceback.print_exc()
    finally:
        if (exitcode == 0):
            # Check for exception on stack
            etype, value, tb = sys.exc_info()
            # If all are None no exception
            if (etype != None) and (value != None) and (tb != None):
                # Ignore Import Error with Python2.7
                if not issubclass(etype, ImportError):
                    if (value != None):
                        exitcode = value
        os.chdir(cwd)
        if(targetConfig.s_debugOutput == False):
            #clean up files
            interruptHandler()

        return exitcode

def interruptHandler():
    global tempDir
    try:
        shellUtils = shell_utilities()
        if(tempDir != None):
            shellUtils.executeShellCommand("rm -rf " + tempDir, True)
    except Exception as e:
        print("Exception while cleaning up - " + str(e))
        traceback.print_exc()

def bootburnCommandLine():
    commandLine = sys.argv
    return bootburn(commandLine)

if __name__ == "__main__":
    print("Please use '../bootburn/bootburn.py' instead of this file.")
    exit()
    result = bootburnCommandLine()
    sys.exit(result)
