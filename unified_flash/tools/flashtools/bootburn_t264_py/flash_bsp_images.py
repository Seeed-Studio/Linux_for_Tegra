#!/usr/bin/python
# SPDX-FileCopyrightText: Copyright (c) 2017-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

##### Main flashing starts here ####

# Register cleanup as the trap handler for exit of the script
# trap needs to be registered before executing the main body

import os
import sys
import time
import traceback
import bootburn_lib
import io
print_capture = io.StringIO()
sys.stdout = print_capture
from flashtools_nverror import nverror
from target_config import target_config
from bootburn_parser import commandline_parser
from system_monitor import monitor
from flashtools_nverror import AbnormalTermination
from flashtools_nverror import nverror
from bootburn_aurix import Aurix
import multiprocessing
import tempfile

sys.stdout = sys.__stdout__

def flash_bsp_active(bootburnLib, targetConfig, dieId="die0", barrier=None):
    targetConfig.sysMonitor = monitor(f"flash_bsp_{targetConfig.s_BoardName}_{dieId}", targetConfig.p_logs, False)
    targetConfig.sysMonitor.log(f"Beginning flash BSP for {dieId}\n", True)

    # Assign the default die id. It can change later on based on platform detection
    targetConfig.dieId = dieId
    targetConfig.mb2AppletBarrier = barrier

    try:
        DirBeingRunFrom = os.path.realpath(__file__)
        DirBeingRunFrom = os.path.dirname(DirBeingRunFrom)
        cwd = os.getcwd()
        os.chdir(DirBeingRunFrom)

        binaryLocationPath = targetConfig.p_TempOutDir
        n_RcmMode = targetConfig.parsed_commandline["R"]
        n_FaMode  = targetConfig.parsed_commandline["f"]
        #FIXME  update a logic to get the chipID from configuration
        #       when we start supporting later generation version
        n_chipId = "0x26"
        n_vdkOption = targetConfig.parsed_commandline["vdk"]
        n_fpgaOption = targetConfig.parsed_commandline["fpga"]

        from bootburn_thor import bootburn_thor

        bootburnLib.ValidateFlashToolVersion(binaryLocationPath)
        if(n_vdkOption == False):
            # Run start session and ECID commands in temporary directory
            # as tegrarcm_v2 runs from multi die is causing errors
            # when reading rcm_state file
            with tempfile.TemporaryDirectory() as temp_dir:
                cwd = os.getcwd()
                os.chdir(temp_dir)
                targetConfig.sysMonitor.vlog(f'Created temporary directory: {temp_dir}')
                bootburnLib.StartNewSession_t264()
                bootburnLib.GetTargetECID("--chip 0x26")
                os.chdir(cwd)

            bootburnLib.CheckFuseForAuthentication()

            bootburnLib.GenAdbSerialNum()
            bootburnLib.CheckForDeviceType()
            bootburnLib.ParsePlatInfoFile(binaryLocationPath, "TargetInfo.txt")
            bootburnLib.CheckFuseForEncryption()
            bootburnLib.ValidateUserOptionAndDeviceType(True)
            bootburnLib.ValidateFindBoardForFused()

        bootburnThor = bootburn_thor(targetConfig)
        if(targetConfig.n_UFSProvisioningEnable):
            targetConfig.p_OutDirPath = binaryLocationPath
            # Run Platform  Detection
            bootburnThor.runPlatformDetection("rcm-flash-provision", binaryLocationPath)

            # Do rcm boot for ufs provisioning
            bootburnThor.BootRCM("rcm-flash-provision", binaryLocationPath)
            targetConfig.sysMonitor.log("UFS-provisioning starting", True)
            targetConfig.sysMonitor.log("UFS-provision Config file : " + targetConfig.f_UfsProvisionCfg, True)
            bootburnLib.UFSDeviceProvision(binaryLocationPath)
        elif(n_vdkOption == False):
            if(targetConfig.b_findBoardName):
                # Run Platform  Detection
                bootburnThor.runPlatformDetection("rcm-flash", binaryLocationPath, l_findBoardOnly=True)

                # Do rcm boot of flashing kernel for finding board name
                bootburnThor.BootRCM("rcm-flash", binaryLocationPath, l_findBoardOnly=True)
                bootburnLib.updateFindBoardData()
                return nverror.NvError_Success
            elif(n_FaMode or n_RcmMode):
                # Run Platform  Detection
                bootburnThor.runPlatformDetection("rcm-boot", binaryLocationPath)

                # Do rcm-boot
                bootburnLib.BootRCM_Orin_FPGA("rcm-boot", binaryLocationPath)
            else:
                # Run Platform  Detection
                bootburnThor.runPlatformDetection("rcm-flash", binaryLocationPath)

                # Do rcm boot of flashing kernel for cold boot flashing
                bootburnThor.BootRCM("rcm-flash", binaryLocationPath)

                if (targetConfig.dieId == "die1"):
                    return nverror.NvError_Success

                bootburnThor.FlashImages(binaryLocationPath)
        else:
            if(n_FaMode or n_RcmMode):
                bootburnLib.BootRCMPrebuild("rcm-boot", binaryLocationPath)
            else:
                bootburnLib.BootRCMPrebuild("rcm-flash", binaryLocationPath)
                bootburnLib.FlashImages(binaryLocationPath)

        return nverror.NvError_Success
    except Exception as e:
        bootburnLib.shellUtils.printError()
        print("\033[01;31mException raised in flash BSP ACTIVE\033[0m")
        print(str(e))
        traceback.print_exc()
        if(isinstance(e.args[0], int)):
            errorCode = int(e.args[0])
        else:
            errorCode = nverror.NvError_Unknown

        sys.exit(errorCode)
    finally:
        #calling updateFindBoardData here to have the DeviceType and FuseStatus
        #even in the case of find_board details due to MB2Applet boot and fuse read
        bootburnLib.updateFindBoardData()
        os.chdir(cwd)

def flash_bsp(commandLineArguments):

    debug = False
    GenericOptions = ['b', 'f', 'h', 'j', 'm', 'o', 'u', 'v', 'w', 'x', 'y', 'D', 'I', 'P', 'R', 'O', 'U', 'T', 'V',
                      'applet', 'asymmetric', 'board_config', 'clean', 'customer_data', 'devicetype',
                      'find_board_name', 'fpga', 'headers', 'l4t', 'logs', 'multi_die', 'no_firmware_update',
                      'pytest', 'safety', 'skipBctPreserve', 'usb_instance',  'user', 'vdk', 'l4t_boot_chain_select']
    try:
        DirBeingRunFrom = os.path.realpath(__file__)
        DirBeingRunFrom = os.path.dirname(DirBeingRunFrom)
        sys.path.insert(0, DirBeingRunFrom)
        cwd = os.getcwd()
        os.chdir(DirBeingRunFrom)
        flashPath = os.path.join(DirBeingRunFrom, "..", "..","..","tools", "flashtools")
        isPdkPackage = os.path.isdir(flashPath)
        targetConfig = target_config(isPdkPackage)
        targetConfig.p_BurnDir = os.getcwd()

#FIXME If P4ROOT = None then flashing fails in Colossus
        targetConfig.P4ROOT = ""
        if(len(commandLineArguments) <= 1):
            targetConfig.sysMonitor = monitor("USAGE", "/tmp", debug)
            b = bootburn_lib.bootburn_lib(targetConfig)
            b.Usage(GenericOptions)
            targetConfig.sysMonitor.vlog("\nERROR:  " + commandLineArguments[0] + " needs arguments, mandatory argument is board ID\n")
            AbnormalTermination("Missing arguments.  Must give at least the board ID in ", nverror.NvError_InvalidArgument)

        targetConfig.sysMonitor = monitor("flash_bsp" , targetConfig.p_logs, debug)
        startTime = time.time()
        targetConfig.sysMonitor.log("start time  = " + str(startTime), True)
        cmdParser = commandline_parser(commandLineArguments)
        cmdParser.initializeTargetConfig(targetConfig, GenericOptions)

        if (cmdParser.parsed_commandline["h"] == True):
            b = bootburn_lib.bootburn_lib(targetConfig)
            b.Usage(GenericOptions)
            return nverror.NvError_Success

        bootburnLib = bootburn_lib.bootburn_lib(targetConfig)
        boardName = ""
        if(cmdParser.parsed_commandline["h"] == False) and (not cmdParser.parsed_commandline["find_board_name"]):
            boardName = "_" + targetConfig.s_BoardName
        targetConfig.sysMonitor = monitor("flash_bsp" + boardName, targetConfig.p_logs, debug)

        if(targetConfig.s_McuPort != None):
            targetConfig.SetFlash_bsp_Paths()

        if (targetConfig.parsed_commandline["T"] == True):
            sys.settrace(bootburnLib.call_trace)
            targetConfig.sysMonitor.callTraceOut = True

        bootburnLib.CheckRecoveryTargets()
        bootburnLib.SetUsbAutoSuspend("disable")

        die_ids = ["die0"]
        barrier = None
        if (cmdParser.parsed_commandline["multi_die"]):
            die_ids.append("die1")
            barrier = multiprocessing.Barrier(2)

        # Create process(es) for die(s) and wait for process(es) to complete
        processes = []
        for dieId in die_ids:
            processes.append(
                multiprocessing.Process(
                    name=dieId,
                    target=flash_bsp_active, args=(bootburnLib, targetConfig, dieId, barrier)
                )
            )

        for process in processes:
            process.start()

        errorCode = nverror.NvError_Success
        for process in processes:
            process.join()
            if process.exitcode != nverror.NvError_Success:
                errorCode = process.exitcode

        StopTime = time.time()
        targetConfig.sysMonitor.log("start time  = " + str(startTime), True)
        targetConfig.sysMonitor.log("end time  = " + str(StopTime), True)
        targetConfig.sysMonitor.log("Total Time = " + str(StopTime - startTime), True)
        targetConfig.sysMonitor.vlog("Image Flashing took " + str(StopTime - startTime))
        if errorCode == nverror.NvError_Success:
            targetConfig.sysMonitor.vlog("\033[01;32mFlashing finished Successfully!!\033[0m")
        else:
            targetConfig.sysMonitor.vlog("\033[01;31mFlashing finished Unsuccessfully!!\033[0m")
        return errorCode

    except Exception as e:
        print("\033[01;31mException {} raised in flash BSP\033[0m".format(str(e)))
        traceback.print_exc()
        if(isinstance(e.args[0], int)):
            return int(e.args[0])
        else:
            return nverror.NvError_Unknown

def flash_bsp_commandline():
    commandLineArguments = sys.argv
    print("Commandline :: " + str(commandLineArguments))
    return flash_bsp(commandLineArguments)

if __name__ == "__main__":
    print("Please use '../bootburn/flash_bsp_images.py' instead of this file.")
    exit()
    result = flash_bsp_commandline()
    sys.exit(result)

